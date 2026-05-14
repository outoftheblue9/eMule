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
#include "PartFileAllocThread.h"
#include "emule.h"
#include "PartFile.h"
#include "PartFileWriteThread.h"
#include "OtherFunctions.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// ---- DbgWrite: forwards to Debug() so output lands in the VS Output window
// via OutputDebugString (timestamped by Debug() in _DEBUG builds). Thread id
// is prepended here because Debug() does not include it.
void DbgWrite(LPCTSTR fmt, ...)
{
	TCHAR body[1024];
	va_list args;
	va_start(args, fmt);
	_vsntprintf_s(body, _TRUNCATE, fmt, args);
	va_end(args);

	Debug(_T("[tid=%lu] %s"), ::GetCurrentThreadId(), body);
}

IMPLEMENT_DYNCREATE(CPartFileAllocThread, CWinThread)

CPartFileAllocThread::CPartFileAllocThread()
	: m_eventThreadEnded(FALSE, TRUE)   // manual-reset, initially non-signaled
	, m_eventWakeup(FALSE, FALSE)       // auto-reset, initially non-signaled
	, m_Run(1)                          // optimistic: set before AfxBeginThread so
	                                    // IsRunning() is true immediately. Cleared
	                                    // on thread exit, or here if start fails.
	, m_bStop(0)
{
	DbgWrite(_T("eMule PartFileAllocThread: ctor entered\n"));
	CWinThread *pThr = AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_BELOW_NORMAL);
	if (!pThr) {
		InterlockedExchange(&m_Run, 0);
		DbgWrite(_T("eMule PartFileAllocThread: AfxBeginThread FAILED\n"));
	} else {
		DbgWrite(_T("eMule PartFileAllocThread: AfxBeginThread OK\n"));
	}
}

CPartFileAllocThread::~CPartFileAllocThread()
{
	ASSERT(!m_Run);
}

UINT AFX_CDECL CPartFileAllocThread::RunProc(LPVOID pParam)
{
	DbgSetThreadName("PartAllocThread");
	InitThreadLocale();
	return pParam ? static_cast<CPartFileAllocThread*>(pParam)->RunInternal() : 1;
}

void CPartFileAllocThread::EndThread()
{
	InterlockedExchange(&m_bStop, 1);
	m_eventWakeup.SetEvent();
	m_eventThreadEnded.Lock();
}

void CPartFileAllocThread::EnqueueAlloc(CPartFile *pFile, uint64 newLength)
{
	if (!pFile)
		return;
	{
		CSingleLock lock(&m_lockList, TRUE);
		m_list.AddTail(AllocRequest{pFile, newLength});
	}
	m_eventWakeup.SetEvent();
}

UINT CPartFileAllocThread::RunInternal()
{
	DbgWrite(_T("eMule PartFileAllocThread: RunInternal entered\n"));
	// NOTE: this thread is on the critical path for *every* file extension.
	// Data writes are gated on m_nAllocPending until the extend completes,
	// so throttling here directly stalls download throughput. AfxBeginThread
	// already runs us at THREAD_PRIORITY_BELOW_NORMAL, which is enough
	// courtesy. THREAD_MODE_BACKGROUND_BEGIN was previously set here, but
	// its I/O + memory page-priority drop starved the VDL zero-fill (the
	// 1-byte WriteFile at newLength-1 triggers a synchronous zero-fill of
	// every byte from the prior valid-data-length up to newLength, which on
	// a fresh ~16 MB extend is 16 MB of writes). Symptoms when enabled:
	// .part files do not grow on disk until shutdown, and downloads flip to
	// PS_ERROR around the first cap-sized flush.

	// m_Run was set to 1 in the ctor to avoid a startup race with FlushBuffer.
	while (!m_bStop) {
		AllocRequest req{};
		bool bHave = false;
		{
			CSingleLock lock(&m_lockList, TRUE);
			if (!m_list.IsEmpty()) {
				req = m_list.RemoveHead();
				bHave = true;
			}
		}
		if (bHave) {
			ProcessOne(req);
			continue;
		}
		::WaitForSingleObject(m_eventWakeup, INFINITE);
	}

	// Drain any remaining requests without doing I/O so callers don't
	// wait forever on m_nAllocPending; just decrement the counters.
	{
		CSingleLock lock(&m_lockList, TRUE);
		while (!m_list.IsEmpty()) {
			AllocRequest req = m_list.RemoveHead();
			if (req.pFile)
				InterlockedDecrement(&req.pFile->m_nAllocPending);
		}
	}

	InterlockedExchange(&m_Run, 0);
	m_eventThreadEnded.SetEvent();
	return 0;
}

void CPartFileAllocThread::ProcessOne(AllocRequest &req)
{
	CPartFile *pFile = req.pFile;
	if (!pFile) {
		ASSERT(0);
		return;
	}

	DWORD dwError = 0;
	HANDLE h = INVALID_HANDLE_VALUE;
	FILETIME ftPrevWrite{};
	bool bSavedMtime = false;
	const DWORD tStart = ::GetTickCount();
	DbgWrite(_T("eMule PartFileAllocThread: extending \"%s\" to %I64u bytes\n"),
		(LPCTSTR)pFile->GetFileName(), req.newLength);
	try {
		const CString sPartFile(RemoveFileExtension(pFile->GetFullName()));
		h = ::CreateFile(sPartFile, GENERIC_WRITE,
			FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			dwError = ::GetLastError();
		} else {
			// Snapshot mtime before any modifications so we can restore it
			// after extension. This prevents a stale m_tUtcLastModified in
			// the .part.met from triggering a false-positive rehash at the
			// next startup (SetEndOfFile and WriteFile both update mtime).
			FILETIME ftC{}, ftA{};
			bSavedMtime = ::GetFileTime(h, &ftC, &ftA, &ftPrevWrite) != FALSE;

			LARGE_INTEGER liSize{};
			if (!::GetFileSizeEx(h, &liSize)) {
				dwError = ::GetLastError();
			} else if ((uint64)liSize.QuadPart < req.newLength) {
				// Extend the file (metadata-only — fast).
				LARGE_INTEGER liNew;
				liNew.QuadPart = (LONGLONG)req.newLength;
				if (!::SetFilePointerEx(h, liNew, NULL, FILE_BEGIN) || !::SetEndOfFile(h)) {
					dwError = ::GetLastError();
				} else {
					// Force VDL extension by writing one byte at the new end.
					// This is the slow synchronous zero-fill on NTFS — runs
					// on this dedicated low-priority thread, not on the
					// write or network threads.
					LARGE_INTEGER liLast;
					liLast.QuadPart = (LONGLONG)(req.newLength - 1);
					if (!::SetFilePointerEx(h, liLast, NULL, FILE_BEGIN)) {
						dwError = ::GetLastError();
					} else {
						BYTE zero = 0;
						DWORD dwWritten = 0;
						if (!::WriteFile(h, &zero, 1, &dwWritten, NULL) || dwWritten != 1)
							dwError = ::GetLastError();
					}
				}
			}
		}
	} catch (...) {
		ASSERT(0);
		dwError = ERROR_GEN_FAILURE;
	}
	if (h != INVALID_HANDLE_VALUE) {
		// Restore the mtime we captured before extending. The saved value in
		// part.met stays valid, so the next startup won't see a mtime mismatch
		// and won't trigger a spurious rehash of the still-downloading file.
		if (bSavedMtime && dwError == 0)
			::SetFileTime(h, NULL, NULL, &ftPrevWrite);
		::CloseHandle(h);
	}

	const DWORD tElapsed = ::GetTickCount() - tStart;
	if (dwError) {
		DbgWrite(_T("eMule PartFileAllocThread: FAILED to extend \"%s\" to %I64u bytes after %lums: error %lu\n"),
			(LPCTSTR)pFile->GetFileName(), req.newLength, tElapsed, dwError);
		// Stash the error for the main thread. FlushBuffer consumes this and
		// throws CFileException, which the existing handler converts to
		// PS_INSUFFICIENT (for ERROR_DISK_FULL / ERROR_HANDLE_DISK_FULL) or
		// PS_ERROR otherwise. Setting PS_ERROR here directly would block
		// resume-after-free-space, which the legacy sync path supported.
		InterlockedExchange(&pFile->m_dwAllocError, (LONG)dwError);
	} else {
		DbgWrite(_T("eMule PartFileAllocThread: extended \"%s\" to %I64u bytes in %lums\n"),
			(LPCTSTR)pFile->GetFileName(), req.newLength, tElapsed);
	}

	InterlockedDecrement(&pFile->m_nAllocPending);

	// Wake the write thread so it re-checks gated items for this file.
	if (theApp.m_pPartFileWriteThread && theApp.m_pPartFileWriteThread->IsRunning())
		theApp.m_pPartFileWriteThread->WakeUpCall();
}
