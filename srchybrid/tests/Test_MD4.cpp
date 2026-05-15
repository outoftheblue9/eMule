#include "pch.h"
#include "../MD4.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	// Hex helper — compares the 16-byte MD4 output against an RFC 1320 §A.5 vector.
	void AssertDigestEquals(const byte* pDigest, const char* pszExpectedHex)
	{
		char szActual[2 * MD4_DIGEST_SIZE + 1];
		for (int i = 0; i < MD4_DIGEST_SIZE; ++i)
			sprintf_s(szActual + i * 2, 3, "%02x", pDigest[i]);
		szActual[2 * MD4_DIGEST_SIZE] = '\0';
		Assert::AreEqual(pszExpectedHex, szActual);
	}
}

namespace emule_tests
{
	// RFC 1320 §A.5 known-answer vectors.
	TEST_CLASS(Test_MD4)
	{
	public:
		TEST_METHOD(EmptyString)
		{
			CMD4 md4;
			md4.Add("", 0);
			md4.Finish();
			AssertDigestEquals(md4.GetHash(), "31d6cfe0d16ae931b73c59d7e0c089c0");
		}

		TEST_METHOD(SingleChar_a)
		{
			CMD4 md4;
			md4.Add("a", 1);
			md4.Finish();
			AssertDigestEquals(md4.GetHash(), "bde52cb31de33e46245e05fbdbd6fb24");
		}

		TEST_METHOD(Abc)
		{
			CMD4 md4;
			md4.Add("abc", 3);
			md4.Finish();
			AssertDigestEquals(md4.GetHash(), "a448017aaf21d8525fc10ae87aa6729d");
		}

		TEST_METHOD(MessageDigest)
		{
			CMD4 md4;
			md4.Add("message digest", 14);
			md4.Finish();
			AssertDigestEquals(md4.GetHash(), "d9130a8164549fe818874806e1c7014b");
		}

		TEST_METHOD(LowercaseAlphabet)
		{
			CMD4 md4;
			md4.Add("abcdefghijklmnopqrstuvwxyz", 26);
			md4.Finish();
			AssertDigestEquals(md4.GetHash(), "d79e1c308aa5bbcdeea8ed63df412da9");
		}

		TEST_METHOD(IncrementalEqualsOneShot)
		{
			const char szInput[] = "The quick brown fox jumps over the lazy dog";
			const size_t nLen = sizeof(szInput) - 1;

			CMD4 oneShot;
			oneShot.Add(szInput, nLen);
			oneShot.Finish();

			CMD4 incremental;
			for (size_t i = 0; i < nLen; ++i)
				incremental.Add(szInput + i, 1);
			incremental.Finish();

			for (int i = 0; i < MD4_DIGEST_SIZE; ++i)
				Assert::AreEqual(oneShot.GetHash()[i], incremental.GetHash()[i]);
		}

		TEST_METHOD(ResetReusesObject)
		{
			CMD4 md4;
			md4.Add("garbage", 7);
			md4.Finish();

			md4.Reset();
			md4.Add("abc", 3);
			md4.Finish();
			AssertDigestEquals(md4.GetHash(), "a448017aaf21d8525fc10ae87aa6729d");
		}
	};
}
