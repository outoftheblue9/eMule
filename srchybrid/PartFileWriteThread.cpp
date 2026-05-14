//this file is part of eMule
//Copyright (C)2020-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "StdAfx.h"
#include "updownclient.h"
#include "PartFileWriteThread.h"
#include "emule.h"
#include "partfile.h"
#include "log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define RUN_STOP	0
#define RUN_IDLE	1
#define RUN_WORK	2
#define WAKEUP		((ULONG_PTR)(~0))

IMPLEMENT_DYNCREATE(CPartFileWriteThread, CWinThread)

CPartFileWriteThread::CPartFileWriteThread()
	: m_eventThreadEnded(FALSE, TRUE)
	, m_hPort()
	, m_Run(RUN_STOP)
	, m_bNewData()
{
	AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_BELOW_NORMAL);
}

CPartFileWriteThread::~CPartFileWriteThread()
{
	ASSERT(!m_hPort && !m_Run);
}

UINT AFX_CDECL CPartFileWriteThread::RunProc(LPVOID pParam)
{
	DbgSetThreadName("PartWriteThread");
	InitThreadLocale();
	return pParam ? static_cast<CPartFileWriteThread*>(pParam)->RunInternal() : 1;
}

void CPartFileWriteThread::EndThread()
{
	m_Run = RUN_STOP;
	PostQueuedCompletionStatus(m_hPort, 0, 0, NULL);
	m_eventThreadEnded.Lock();
}

UINT CPartFileWriteThread::RunInternal()
{
	m_hPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
	if (!m_hPort)
		return ::GetLastError();

	DWORD dwWrite = 0;
	ULONG_PTR completionKey = 0;
	OverlappedWrite_Struct *pCurIO = NULL;
	m_Run = RUN_IDLE;
	while (m_Run
		&& ::GetQueuedCompletionStatus(m_hPort, &dwWrite, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE)
		&& completionKey)
	{
		m_Run = RUN_WORK;
		//move buffer lists into the local storage
		if (!m_FlushList.IsEmpty()) {
			m_lockFlushList.Lock();
			while (!m_FlushList.IsEmpty())
				m_listToWrite.AddTail(m_FlushList.RemoveHead());
			InterlockedExchange8(&m_bNewData, 0);
			m_lockFlushList.Unlock();
		}
		//start new I/O
		WriteBuffers();
		//completed I/O
		do {
			if (!completionKey)
				break;
			if (completionKey != WAKEUP) //ignore wakeups
				WriteCompletionRoutine(dwWrite, pCurIO);
		} while (::GetQueuedCompletionStatus(m_hPort, &dwWrite, &completionKey, (LPOVERLAPPED*)&pCurIO, 0));

		if (!completionKey) //thread termination
			break;
		m_Run = RUN_IDLE;
		if (InterlockedExchange8(&m_bNewData, 0) && m_listPendingIO.IsEmpty())
			PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	}
	m_Run = RUN_STOP;

	//Improper termination of asynchronous I/O follows...
	//close file handles to release I/O completion port
	while (!m_listPendingIO.IsEmpty())
		WriteCompletionRoutine(0, m_listPendingIO.RemoveHead());

	//drain any items that never made it to WriteFile (m_listToWrite + m_FlushList)
	//so MergedWrite buffers and allocation-marker PartFileBufferedData are freed
	auto drainQueued = [](CList<ToWrite> &list) {
		while (!list.IsEmpty()) {
			ToWrite item = list.RemoveHead();
			if (item.pMerge) {
				delete[] item.pMerge->data;
				delete item.pMerge;
			} else if (item.pBuffer && !item.pBuffer->data) {
				//allocation marker — owned by the queue, not by CPartFile::m_BufferedData_list
				delete item.pBuffer;
			}
		}
	};
	drainQueued(m_listToWrite);
	m_lockFlushList.Lock();
	drainQueued(m_FlushList);
	m_lockFlushList.Unlock();

	::CloseHandle(m_hPort);
	m_hPort = 0;

	m_eventThreadEnded.SetEvent();
	return 0;
}

void CPartFileWriteThread::WriteBuffers()
{
	//process internal list
	CList<ToWrite> deferred;
	while (!m_listToWrite.IsEmpty() && m_Run) {
		const ToWrite item = m_listToWrite.RemoveHead();
		ASSERT(!(item.pBuffer && item.pMerge)); //mutually exclusive

		// Defer writes for files whose CPartFileAllocThread extension is
		// still in flight. The alloc thread will WakeUpCall() us when it
		// completes; we'll re-enter and try again then.
		if (item.pFile && item.pFile->m_nAllocPending != 0) {
			deferred.AddTail(item);
			continue;
		}

		// If the alloc thread reported an error for this file (e.g. disk
		// full), abort the queued write rather than issuing WriteFile past
		// EOF. Return source items to PB_READY so they stay in
		// CPartFile::m_BufferedData_list. The main thread's FlushBuffer
		// will consume m_dwAllocError, throw CFileException::diskFull, and
		// route the file to PS_INSUFFICIENT. On resume, the next flush
		// re-enqueues alloc and these items get retried.
		if (item.pFile && item.pFile->m_dwAllocError != 0) {
			if (item.pMerge) {
				for (PartFileBufferedData *src : item.pMerge->sources)
					src->flushed = PB_READY;
				delete[] item.pMerge->data;
				delete item.pMerge;
			} else if (item.pBuffer && item.pBuffer->data) {
				item.pBuffer->flushed = PB_READY;
			} else if (item.pBuffer) {
				// allocation marker — owned by the queue
				delete item.pBuffer;
			}
			continue;
		}

		PartFileBufferedData *pBuffer = item.pBuffer;
		MergedWrite *pMerge = item.pMerge;
		const uint64 uStart = pMerge ? pMerge->start : pBuffer->start;
		const uint64 uEnd = pMerge ? pMerge->end : pBuffer->end;
		const BYTE *pData = pMerge ? pMerge->data : pBuffer->data;
		ASSERT(uEnd >= uStart && (pData || uEnd == uStart)); //verifies allocation requests too

		CPartFile *pFile = item.pFile;
		if (AddFile(pFile)) {
			//initiate write
			OverlappedWrite_Struct *pOvWrite = new OverlappedWrite_Struct;
			pOvWrite->oOverlap.Internal = 0;
			pOvWrite->oOverlap.InternalHigh = 0;
			*(uint64*)&pOvWrite->oOverlap.Offset = uStart;
			pOvWrite->oOverlap.hEvent = 0;
			pOvWrite->pFile = pFile;
			pOvWrite->pBuffer = pBuffer;
			pOvWrite->pMerge = pMerge;

			static const BYTE zero = 0;
			if (!::WriteFile(pFile->m_hWrite, pData ? pData : &zero, (DWORD)(uEnd - uStart + 1), NULL, (LPOVERLAPPED)pOvWrite)) {
				DWORD dwError = ::GetLastError();
				if (dwError != ERROR_IO_PENDING) {
					delete pOvWrite;
					if (pMerge) {
						for (PartFileBufferedData *src : pMerge->sources) {
							src->dwError = dwError;
							src->flushed = PB_ERROR;
						}
						theApp.QueueDebugLogLineEx(LOG_WARNING,
							_T("WriteBuffers sync error (merged x%Iu) file=\"%s\" off=%I64u len=%I64u err=%lu"),
							pMerge->sources.size(),
							pFile ? (LPCTSTR)pFile->GetFileName() : _T("?"),
							uStart, uEnd - uStart + 1, dwError);
						delete[] pMerge->data;
						delete pMerge;
					} else if (pBuffer->data) { //check for an allocation request
						pBuffer->dwError = dwError;
						pBuffer->flushed = PB_ERROR;
						theApp.QueueDebugLogLineEx(LOG_WARNING,
							_T("WriteBuffers sync error file=\"%s\" off=%I64u len=%I64u err=%lu"),
							pFile ? (LPCTSTR)pFile->GetFileName() : _T("?"),
							uStart, uEnd - uStart + 1, dwError);
					}
					RemFile(pFile);
					// re-add deferred items below before returning
					while (!deferred.IsEmpty())
						m_listToWrite.AddHead(deferred.RemoveTail());
					return;
				}
			}
			pOvWrite->pos = m_listPendingIO.AddTail(pOvWrite);
			++pFile->m_iWrites;
		} else {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WriteBuffers error: CPartFile cannot be written"));
			if (pMerge) {
				delete[] pMerge->data;
				delete pMerge;
			}
		}
	}

	// Put deferred (alloc-pending) items back at the head, preserving
	// their original order. They'll be retried on the next pulse — the
	// alloc thread fires WakeUpCall() on completion to trigger it.
	while (!deferred.IsEmpty())
		m_listToWrite.AddHead(deferred.RemoveTail());
}

void CPartFileWriteThread::WriteCompletionRoutine(DWORD dwBytesWritten, const OverlappedWrite_Struct *pOvWrite)
{
	if (pOvWrite == NULL) {
		ASSERT(0);
		return;
	}
	CPartFile *pFile = pOvWrite->pFile;
	if (m_Run) {
		PartFileBufferedData *pBuffer = pOvWrite->pBuffer;
		MergedWrite *pMerge = pOvWrite->pMerge;
		const uint64 uStart = pMerge ? pMerge->start : pBuffer->start;
		const uint64 uEnd = pMerge ? pMerge->end : pBuffer->end;
		const DWORD dwWrite = (DWORD)(uEnd - uStart + 1);

		ASSERT(pOvWrite->pos);
		m_listPendingIO.RemoveAt(pOvWrite->pos);
		if (dwBytesWritten && dwWrite == dwBytesWritten) {
			if (pFile) {
				--pFile->m_iWrites;
				if (pMerge) { //fan completion across all source items
					ASSERT(pFile->m_iWrites >= 0);
					for (PartFileBufferedData *src : pMerge->sources) {
						ASSERT(src->flushed == PB_PENDING);
						src->flushed = PB_WRITTEN;
					}
				} else if (pBuffer->data) { //write data
					ASSERT(pBuffer->flushed == PB_PENDING && pFile->m_iWrites >= 0);
					pBuffer->flushed = PB_WRITTEN;
				} else { //full file allocation
					ASSERT(dwBytesWritten == 1);
					::FlushFileBuffers(pFile->m_hWrite);
					pFile->m_hpartfile.SetLength(pBuffer->start); //truncate the extra byte
					delete pBuffer;
				}
			}
		} else {
			// OVERLAPPED::Internal holds the NTSTATUS of the failed I/O. Log it
			// so the cause of the PS_ERROR transition is identifiable instead
			// of opaque (the throw site only has access to dwError on the item).
			const DWORD dwNt = (DWORD)pOvWrite->oOverlap.Internal;
			if (pMerge) {
				for (PartFileBufferedData *src : pMerge->sources)
					src->flushed = PB_ERROR;
				theApp.QueueDebugLogLineEx(LOG_WARNING,
					_T("WriteCompletion ERROR (merged) file=\"%s\" off=%I64u len=%lu written=%lu NTSTATUS=0x%08lx sources=%Iu"),
					pFile ? (LPCTSTR)pFile->GetFileName() : _T("?"),
					uStart, dwWrite, dwBytesWritten, dwNt, pMerge->sources.size());
			} else {
				pBuffer->flushed = PB_ERROR; //error code is unknown
				theApp.QueueDebugLogLineEx(LOG_WARNING,
					_T("WriteCompletion ERROR file=\"%s\" off=%I64u len=%lu written=%lu NTSTATUS=0x%08lx"),
					pFile ? (LPCTSTR)pFile->GetFileName() : _T("?"),
					uStart, dwWrite, dwBytesWritten, dwNt);
			}
			Debug(_T("  Completed write size: expected %lu, written %lu, NTSTATUS=0x%08lx\n"), dwWrite, dwBytesWritten, dwNt);
		}

		if (pMerge) {
			delete[] pMerge->data;
			delete pMerge;
		}
	} else {
		//shutdown: free MergedWrite buffer and allocation-marker PartFileBufferedData
		if (pOvWrite->pMerge) {
			delete[] pOvWrite->pMerge->data;
			delete pOvWrite->pMerge;
		} else if (pOvWrite->pBuffer && !pOvWrite->pBuffer->data) {
			delete pOvWrite->pBuffer;
		}
		if (pFile)
			RemFile(pFile);
	}

	delete pOvWrite;
}

bool CPartFileWriteThread::AddFile(CPartFile *pFile)
{
	ASSERT(m_hPort && m_Run);
	if (pFile && pFile->m_hWrite == INVALID_HANDLE_VALUE) {
		const CString sPartFile(RemoveFileExtension(pFile->GetFullName()));
		pFile->m_hWrite = ::CreateFile(sPartFile, GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (pFile->m_hWrite == INVALID_HANDLE_VALUE) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to open \"%s\" for overlapped write: %s"), (LPCTSTR)sPartFile, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			pFile->SetStatus(PS_ERROR);
			return false;
		}
		if (m_hPort != ::CreateIoCompletionPort(pFile->m_hWrite, m_hPort, (ULONG_PTR)pFile, 0)) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("Failed to associate \"%s\" with IOCP: %s"), (LPCTSTR)sPartFile, (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
			RemFile(pFile);
			pFile->SetStatus(PS_ERROR);
			return false;
		}
	}
	return true;
}

void CPartFileWriteThread::RemFile(CPartFile *pFile)
{
	ASSERT(pFile);
	if (pFile->m_hWrite != INVALID_HANDLE_VALUE) {
		VERIFY(::CloseHandle(pFile->m_hWrite));
		pFile->m_hWrite = INVALID_HANDLE_VALUE;
	}
}

void CPartFileWriteThread::WakeUpCall()
{
	//pending I/O makes posting unnecessary
	if (m_Run == RUN_IDLE && m_listPendingIO.IsEmpty())
		PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	else
		InterlockedExchange8(&m_bNewData, 1);
}