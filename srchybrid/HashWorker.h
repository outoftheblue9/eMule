#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <functional>

// Persistent single-job worker thread. Submit() hands a callable to the
// worker and returns immediately; Wait() blocks until the previous job is
// done. Used to overlap the SHA-1 (AICH) pass with MD4 on the same buffer
// during CKnownFile::CreateHash.
class CSerialWorker
{
public:
	CSerialWorker()
	{
		m_thread = std::thread(&CSerialWorker::Loop, this);
	}
	~CSerialWorker()
	{
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_quit = true;
			m_pending = true;
		}
		m_cv.notify_all();
		if (m_thread.joinable())
			m_thread.join();
	}

	CSerialWorker(const CSerialWorker&) = delete;
	CSerialWorker& operator=(const CSerialWorker&) = delete;

	void Submit(std::function<void()> job)
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_cv.wait(lk, [&] { return !m_pending; });
		if (m_excPtr)
			std::rethrow_exception(m_excPtr);
		m_job = std::move(job);
		m_pending = true;
		m_cv.notify_all();
	}

	void Wait()
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		m_cv.wait(lk, [&] { return !m_pending; });
		if (m_excPtr)
			std::rethrow_exception(m_excPtr);
	}

private:
	void Loop()
	{
		for (;;) {
			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lk(m_mtx);
				m_cv.wait(lk, [&] { return m_pending; });
				if (m_quit)
					return;
				job = std::move(m_job);
			}
			try {
				job();
			} catch (...) {
				std::lock_guard<std::mutex> lk(m_mtx);
				m_excPtr = std::current_exception();
			}
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				m_pending = false;
			}
			m_cv.notify_all();
		}
	}

	std::function<void()> m_job;
	bool m_pending = false;
	bool m_quit = false;
	std::exception_ptr m_excPtr;
	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::thread m_thread;
};
