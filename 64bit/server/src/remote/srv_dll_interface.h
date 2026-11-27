// srv_dll_interface.h - the interface version between the DLL and the server (ticket 4719, plan 4.5 of the upstream
// export; design: research/03-dll-engine-boundary/upstream-export-e4-dll-commit.md section 8).
//
// The DLL source holds one number, VP_OUT_OF_PROCESS_INTERFACE_VERSION in CvGameCoreDLL_Expansion2/
// CvOutOfProcessInterface.h, raised whenever the DLL side of the contract with the server changes, and exports it in
// the x64 build (VP_OUT_OF_PROCESS) as DllGetOutOfProcessInterfaceVersion. This header holds the number this server
// expects and checks it twice:
// - at compile time: when the DLL source on the include path has the header, a different number fails the build
//   (static_assert);
// - at load time (SrvDllInterface::Check, called by kRt_Boot and by the direct mode right after LoadLibrary): a DLL
//   with another number is refused, and kRt_Boot's refusal takes the proxy's dll-boot route to the D118/D119 error
//   report and popup.
// A DLL without the export: refused when this server was built against a DLL source that has the header (from the pin
// batch that carries it on, and always in the 64bit folder); accepted with a LOUD line when the source has none (the
// pins up to x64-batch4). The MC's answer to question #5517 on 4719, 2026-11-18: decided at compile time, no runtime
// switch.
#pragma once

#include <windows.h>
#include <stdio.h>
#include <string>

// The number this server is written for. Raise it together with the DLL's VP_OUT_OF_PROCESS_INTERFACE_VERSION.
#define SRV_DLL_INTERFACE_VERSION_EXPECTED 1u
#define SRV_DLL_INTERFACE_EXPORT "DllGetOutOfProcessInterfaceVersion"

#if defined(__has_include)
#if __has_include("CvOutOfProcessInterface.h")
#include "CvOutOfProcessInterface.h"
#define SRV_DLL_SOURCE_VERSIONED 1
static_assert(VP_OUT_OF_PROCESS_INTERFACE_VERSION == SRV_DLL_INTERFACE_VERSION_EXPECTED,
              "interface version mismatch: the DLL source's VP_OUT_OF_PROCESS_INTERFACE_VERSION "
              "(CvOutOfProcessInterface.h) differs from SRV_DLL_INTERFACE_VERSION_EXPECTED "
              "(server/src/remote/srv_dll_interface.h): the server and the DLL source belong to different versions");
#endif
#endif
#ifndef SRV_DLL_SOURCE_VERSIONED
#define SRV_DLL_SOURCE_VERSIONED 0
#endif

namespace SrvDllInterface
{
enum Verdict
{
	kMatch,             // the DLL's number is the expected one
	kMismatch,          // another number: refused
	kMissingRefused,    // no export, server built against a versioned DLL source: refused
	kMissingAccepted,   // no export, server built against an unversioned DLL source: accepted, LOUD
};

struct Result
{
	Verdict verdict;
	bool ok;             // false: the server refuses the DLL
	bool hasExport;
	unsigned dllVersion;  // 0 without the export
	unsigned expected;
	std::string text;     // one line for server.log, server-report.txt and (on a refusal) kRt_Boot's message
};

inline const char* VerdictName(Verdict v)
{
	switch(v)
	{
	case kMatch: return "match";
	case kMismatch: return "mismatch";
	case kMissingRefused: return "missing-refused";
	case kMissingAccepted: return "missing-accepted";
	}
	return "?";
}

// The decision alone (the unit test runs it for every combination).
inline Result Decide(bool hasExport, unsigned dllVersion, unsigned expected, bool sourceVersioned)
{
	Result r;
	r.hasExport = hasExport;
	r.dllVersion = hasExport ? dllVersion : 0u;
	r.expected = expected;
	char sz[512];
	if(hasExport && dllVersion == expected)
	{
		r.verdict = kMatch;
		r.ok = true;
		sprintf_s(sz, "interface version: the DLL's is %u, as this server expects", dllVersion);
	}
	else if(hasExport)
	{
		r.verdict = kMismatch;
		r.ok = false;
		sprintf_s(sz, "interface version mismatch: the DLL's interface version is %u, this server expects %u: the DLL "
		              "and the server come from different builds", dllVersion, expected);
	}
	else if(sourceVersioned)
	{
		r.verdict = kMissingRefused;
		r.ok = false;
		sprintf_s(sz, "interface version missing: the DLL has no %s export, this server expects interface version %u: "
		              "the DLL comes from a build without it", SRV_DLL_INTERFACE_EXPORT, expected);
	}
	else
	{
		r.verdict = kMissingAccepted;
		r.ok = true;
		sprintf_s(sz, "interface version: LOUD: the DLL has no %s export; accepted because this server was built "
		              "against a DLL source without one (it expects %u once the DLL has it)", SRV_DLL_INTERFACE_EXPORT,
		          expected);
	}
	r.text = sz;
	return r;
}

// The check on a loaded DLL. sourceVersioned: whether this server was compiled against a DLL source with the header.
inline Result Check(HMODULE hDll, unsigned expected = SRV_DLL_INTERFACE_VERSION_EXPECTED,
                    bool sourceVersioned = SRV_DLL_SOURCE_VERSIONED != 0)
{
	typedef unsigned int (*PFN)();
	PFN pfn = hDll ? reinterpret_cast<PFN>(GetProcAddress(hDll, SRV_DLL_INTERFACE_EXPORT)) : NULL;
	return Decide(pfn != NULL, pfn ? pfn() : 0u, expected, sourceVersioned);
}
}  // namespace SrvDllInterface
