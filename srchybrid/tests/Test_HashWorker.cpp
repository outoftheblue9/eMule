#include "pch.h"
#include "../HashWorker.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace emule_tests
{
	TEST_CLASS(Test_HashWorker)
	{
	public:
		TEST_METHOD(SubmitWaitRunsJob)
		{
			CSerialWorker w;
			std::atomic<int> n{0};
			w.Submit([&] { n.store(42, std::memory_order_release); });
			w.Wait();
			Assert::AreEqual(42, n.load(std::memory_order_acquire));
		}

		TEST_METHOD(MultipleSubmitsRunInOrder)
		{
			CSerialWorker w;
			std::atomic<int> sum{0};
			for (int i = 1; i <= 50; ++i) {
				w.Submit([&, i] { sum.fetch_add(i, std::memory_order_acq_rel); });
			}
			w.Wait();
			// 1 + 2 + ... + 50 = 1275
			Assert::AreEqual(1275, sum.load(std::memory_order_acquire));
		}

		TEST_METHOD(SubmitWaitsForPreviousJob)
		{
			// If Submit doesn't block on the prior job, the second job's
			// observation of "ran" will race with the first job's still-asleep
			// completion. Use a long-ish sleep on job A and a quick read on
			// job B; B must observe A's write.
			CSerialWorker w;
			std::atomic<int> stage{0};
			w.Submit([&] {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				stage.store(1, std::memory_order_release);
			});
			int observed = -1;
			w.Submit([&] { observed = stage.load(std::memory_order_acquire); });
			w.Wait();
			Assert::AreEqual(1, observed);
		}

		TEST_METHOD(ExceptionRethrownOnWait)
		{
			CSerialWorker w;
			w.Submit([] { throw std::runtime_error("boom"); });
			bool caught = false;
			try {
				w.Wait();
			} catch (const std::runtime_error &e) {
				caught = std::string(e.what()) == "boom";
			}
			Assert::IsTrue(caught, L"Wait() must rethrow worker exception");
		}

		TEST_METHOD(ExceptionRethrownOnNextSubmit)
		{
			CSerialWorker w;
			w.Submit([] { throw std::runtime_error("fail-on-submit"); });
			// Give worker a moment to observe + record the exception.
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			bool caught = false;
			try {
				w.Submit([] {});
			} catch (const std::runtime_error&) {
				caught = true;
			}
			Assert::IsTrue(caught, L"Submit() must rethrow prior worker exception");
		}

		TEST_METHOD(DtorJoinsIdleWorker)
		{
			// Construct + destruct with no work; must not hang or crash.
			{ CSerialWorker w; }
		}

		TEST_METHOD(DtorJoinsAfterCompletedJob)
		{
			std::atomic<int> n{0};
			{
				CSerialWorker w;
				w.Submit([&] { n.store(7, std::memory_order_release); });
				w.Wait();
			}
			Assert::AreEqual(7, n.load(std::memory_order_acquire));
		}
	};
}
