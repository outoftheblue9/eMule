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
#pragma once

class CPartFile;

// Writes to OutputDebugString and to a log file (fixed path) with timestamp.
// Used for diagnosing connect-time freezes when in-app log queue is unreliable.
void DbgWrite(LPCTSTR fmt, ...);

struct AllocRequest
{
	CPartFile *pFile;
	uint64    newLength;
};

// Dedicated single-threaded allocator for part-file size extensions. Each
// extension forces NTFS to extend the Valid Data Length, which is a
// synchronous zero-fill on non-sparse files and pins a thread for the
// duration of the fill (potentially many GB). Doing this work on the
// network/UI threads or on CPartFileWriteThread starves every other
// download. This thread isolates that work and runs at low CPU + I/O
// priority via THREAD_MODE_BACKGROUND_BEGIN.
class CPartFileAllocThread : public CWinThread
{
	DECLARE_DYNCREATE(CPartFileAllocThread)
public:
	CPartFileAllocThread();
	virtual ~CPartFileAllocThread();
	CPartFileAllocThread(const CPartFileAllocThread&) = delete;
	CPartFileAllocThread& operator=(const CPartFileAllocThread&) = delete;

	void EndThread();
	bool IsRunning() const { return m_Run != 0; }
	void EnqueueAlloc(CPartFile *pFile, uint64 newLength);

private:
	static UINT AFX_CDECL RunProc(LPVOID pParam);
	UINT RunInternal();
	void ProcessOne(AllocRequest &req);

	CEvent              m_eventThreadEnded;
	CEvent              m_eventWakeup;
	CCriticalSection    m_lockList;
	CList<AllocRequest> m_list;
	volatile LONG       m_Run;
	volatile LONG       m_bStop;
};
