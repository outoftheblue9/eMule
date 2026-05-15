#include "pch.h"
#include "../PartFileWriteCoalesce.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace emule_tests
{
	namespace
	{
		// Sentinel non-null data pointer for "mergeable" fragments. The coalesce
		// helper never dereferences `data` — it only checks for null vs non-null
		// as the allocation-marker signal — so any stable address is fine.
		const BYTE kAnyData = 0;
		const void *const D = &kAnyData;

		MergeFragment F(uint64_t start, uint64_t end, const void *data = D)
		{
			return MergeFragment{start, end, data};
		}
	}

	TEST_CLASS(Test_PartFileWriteCoalesce)
	{
	public:
		TEST_METHOD(Empty)
		{
			std::vector<MergeFragment> in;
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(0, out.size());
		}

		TEST_METHOD(Single)
		{
			std::vector<MergeFragment> in{F(0, 99)};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(1, out.size());
			Assert::AreEqual<size_t>(0, out[0].firstIdx);
			Assert::AreEqual<size_t>(1, out[0].count);
		}

		TEST_METHOD(TwoContiguous)
		{
			std::vector<MergeFragment> in{F(0, 99), F(100, 199)};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(1, out.size());
			Assert::AreEqual<size_t>(0, out[0].firstIdx);
			Assert::AreEqual<size_t>(2, out[0].count);
		}

		TEST_METHOD(TwoNonContiguous)
		{
			std::vector<MergeFragment> in{F(0, 99), F(200, 299)};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(2, out.size());
			Assert::AreEqual<size_t>(1, out[0].count);
			Assert::AreEqual<size_t>(1, out[1].count);
			Assert::AreEqual<size_t>(1, out[1].firstIdx);
		}

		TEST_METHOD(MixedRuns)
		{
			std::vector<MergeFragment> in{
				F(0, 9), F(10, 19), F(20, 29), F(1000, 1009), F(1010, 1019)
			};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(2, out.size());
			Assert::AreEqual<size_t>(0, out[0].firstIdx);
			Assert::AreEqual<size_t>(3, out[0].count);
			Assert::AreEqual<size_t>(3, out[1].firstIdx);
			Assert::AreEqual<size_t>(2, out[1].count);
		}

		TEST_METHOD(LargeContiguousRun)
		{
			std::vector<MergeFragment> in;
			for (uint64_t i = 0; i < 16; ++i)
				in.push_back(F(i * 65536, i * 65536 + 65535));
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(1, out.size());
			Assert::AreEqual<size_t>(16, out[0].count);
		}

		TEST_METHOD(BoundaryNeighbours)
		{
			std::vector<MergeFragment> in{F(0, 4095), F(4096, 8191)};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(1, out.size());
			Assert::AreEqual<size_t>(2, out[0].count);
		}

		TEST_METHOD(OffByOne)
		{
			std::vector<MergeFragment> in{F(0, 99), F(101, 199)};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(2, out.size());
			Assert::AreEqual<size_t>(1, out[0].count);
			Assert::AreEqual<size_t>(1, out[1].count);
		}

		TEST_METHOD(ZeroLengthFragmentNeverMerges)
		{
			// A zero-length fragment (end < start) is treated as an allocation
			// marker and must remain isolated even when surrounded by mergeable
			// neighbours.
			std::vector<MergeFragment> in{
				F(0, 99),                   // mergeable
				F(100, 99),                 // zero-length / allocation marker
				F(100, 199),                // mergeable, but the marker breaks the run
			};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(3, out.size());
			Assert::AreEqual<size_t>(1, out[0].count);
			Assert::AreEqual<size_t>(1, out[1].count);
			Assert::AreEqual<size_t>(1, out[2].count);
		}

		TEST_METHOD(NullDataFragmentNeverMerges)
		{
			// data==NULL marks an allocation request. Must never merge with
			// neighbours, even when offsets line up perfectly.
			std::vector<MergeFragment> in{
				F(0, 99),                   // mergeable
				F(100, 199, nullptr),       // null data — allocation marker
				F(200, 299),                // mergeable, would have been contiguous
			};
			std::vector<MergedRun> out;
			CoalesceContiguous(in, out);
			Assert::AreEqual<size_t>(3, out.size());
			for (const MergedRun &r : out)
				Assert::AreEqual<size_t>(1, r.count);
		}
	};
}
