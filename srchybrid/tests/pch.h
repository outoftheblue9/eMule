#pragma once

// Test DLL precompiled header.
//
// Pulls in eMule's full StdAfx.h so the .cpp files we reference from srchybrid/
// (MD4.cpp, UInt128.cpp, ...) see exactly the same header environment they get
// inside emule.vcxproj.
//
// Adds CppUnitTest.h on top for the test files themselves.

#include "../StdAfx.h"

#include <CppUnitTest.h>
