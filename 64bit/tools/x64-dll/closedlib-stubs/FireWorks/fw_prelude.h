// Include prelude for the closed-library replacements of issue 1006.
//
// The Firaxis headers (FirePlace/include/FireWorks, CvGameCoreDLLUtil/include, ...) are not
// self-contained: in the DLL they compile only after CvGameCoreDLLPCH.h has pulled in Windows.h,
// the STL and a few typedefs. This file reproduces that subset of the PCH, in the same order, so
// that the replacement sources see the same declarations (and the same class layouts) as the DLL.
// Include paths needed: the fork's CvGameCoreDLLUtil/include, FirePlace/include,
// FirePlace/include/FireWorks, CvWorldBuilderMap/include, ThirdPartyLibs/Lua51/include.
#pragma once

#include <assert.h>

#ifndef NDEBUG
#define ASSERT_DIALOGUE(msg) _wassert(_CRT_WIDE(msg), _CRT_WIDE(__FILE__), __LINE__)
#else
#define ASSERT_DIALOGUE(msg) ((void)0)
#endif

#if defined(__clang__)
#define CLOSED_ENUM __attribute__((enum_extensibility(closed)))
#define OPEN_ENUM __attribute__((enum_extensibility(open)))
#define FLAG_ENUM __attribute__((flag_enum))
#define BUILTIN_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#include <intrin.h>
#define CLOSED_ENUM
#define OPEN_ENUM
#define FLAG_ENUM
#define BUILTIN_TRAP() do { __ud2(); __assume(0); } while(0)
#endif
#if __cplusplus >= 201103L
#define ENUM_META_VALUE [[maybe_unused]]
#else
#define ENUM_META_VALUE
#endif
#define UNUSED_VARIABLE(x) ((void)(x))

#include "CvGameCoreDLLUtil_Win32Headers.h"
#include <MMSystem.h>

#include <algorithm>
#include <vector>
#include <list>
#include <math.h>
#include <tchar.h>
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <limits>

typedef unsigned char    byte;
typedef unsigned int     uint;
typedef wchar_t          wchar;

#include <FireWorks/FDefNew.h>
#include <FireWorks/FFireTypes.h>
#include <FireWorks/FAssert.h>
#include <FireWorks/FILogFile.h>
#include <FireWorks/FDataStream.h>

#include "../common/stublog.h"
