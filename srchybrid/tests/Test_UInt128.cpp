#include "pch.h"
#include "../kademlia/utils/UInt128.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Kademlia::CUInt128;

namespace emule_tests
{
	TEST_CLASS(Test_UInt128)
	{
	public:
		TEST_METHOD(DefaultIsZero)
		{
			CUInt128 a;
			Assert::AreEqual(0, a.CompareTo(0UL));
		}

		TEST_METHOD(FillCtorAllOnes)
		{
			CUInt128 a(true);
			for (UINT i = 0; i < 128; ++i)
				Assert::AreEqual(1u, a.GetBitNumber(i));
		}

		TEST_METHOD(SetGetBit_RoundTrip)
		{
			CUInt128 a;
			a.SetBitNumber(0, 1);
			a.SetBitNumber(63, 1);
			a.SetBitNumber(127, 1);
			Assert::AreEqual(1u, a.GetBitNumber(0));
			Assert::AreEqual(1u, a.GetBitNumber(63));
			Assert::AreEqual(1u, a.GetBitNumber(127));
			Assert::AreEqual(0u, a.GetBitNumber(64));
		}

		TEST_METHOD(SetBitToZero_Clears)
		{
			CUInt128 a(true);
			a.SetBitNumber(42, 0);
			Assert::AreEqual(0u, a.GetBitNumber(42));
			Assert::AreEqual(1u, a.GetBitNumber(41));
			Assert::AreEqual(1u, a.GetBitNumber(43));
		}

		TEST_METHOD(Xor_IsCommutative)
		{
			byte rawA[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
			                  0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE };
			byte rawB[16] = { 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
			                  0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
			CUInt128 a(rawA), b(rawB);
			CUInt128 ab = a; ab.Xor(b);
			CUInt128 ba = b; ba.Xor(a);
			Assert::AreEqual(0, ab.CompareTo(ba));
		}

		TEST_METHOD(Xor_WithSelfIsZero)
		{
			byte raw[16] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED, 0xFA, 0xCE,
			                 0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD, 0xC0, 0xDE };
			CUInt128 a(raw);
			CUInt128 b = a;
			a.Xor(b);
			Assert::AreEqual(0, a.CompareTo(0UL));
		}

		TEST_METHOD(CompareTo_TotalOrder)
		{
			CUInt128 zero;
			CUInt128 one;       one.SetValue(1UL);
			CUInt128 two;       two.SetValue(2UL);
			Assert::IsTrue(zero.CompareTo(one) < 0);
			Assert::IsTrue(one.CompareTo(two) < 0);
			Assert::IsTrue(zero.CompareTo(two) < 0);
			Assert::IsTrue(one.CompareTo(zero) > 0);
			Assert::IsTrue(one.CompareTo(one) == 0);
		}

		TEST_METHOD(Add_BasicAndCarryAcrossWords)
		{
			CUInt128 a; a.SetValue(0xFFFFFFFFUL);
			a.Add(1UL);
			// Should now equal 0x100000000 — carry into m_uData[2].
			CUInt128 expected;
			expected.SetBitNumber(95, 1); // bit 95 = 1 << 32 in the low qword.
			Assert::AreEqual(0, a.CompareTo(expected));
		}

		TEST_METHOD(SubtractInversesAdd)
		{
			CUInt128 a; a.SetValue(0x12345UL);
			CUInt128 b; b.SetValue(0x6789UL);
			CUInt128 c = a; c.Add(b); c.Subtract(b);
			Assert::AreEqual(0, c.CompareTo(a));
		}

		TEST_METHOD(SetValueBE_ToByteArray_RoundTrip)
		{
			byte input[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
			                   0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
			CUInt128 a;
			a.SetValueBE(input);
			byte output[16];
			a.ToByteArray(output);
			for (int i = 0; i < 16; ++i)
				Assert::AreEqual((int)input[i], (int)output[i]);
		}

		TEST_METHOD(ShiftLeft_OneBit)
		{
			CUInt128 a; a.SetValue(1UL);
			a.ShiftLeft(1);
			CUInt128 expected; expected.SetValue(2UL);
			Assert::AreEqual(0, a.CompareTo(expected));
		}

		TEST_METHOD(ShiftLeft_OverflowZeroes)
		{
			CUInt128 a(true);
			a.ShiftLeft(128);
			Assert::AreEqual(0, a.CompareTo(0UL));
		}
	};
}
