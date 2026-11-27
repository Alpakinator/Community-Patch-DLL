// savemods_unit.cpp - the test program of ticket 0744 (server/tests/remote/test_savemods.py): it links
// server/src/remote/srv_remote_savemods.cpp and srv_exe_crash.cpp with stand-ins for the server functions they use and
// plays the DLL's CustomMods.log lines of a load without the DLL and without the server:
//   savemods_unit.exe case <name> <run dir>    one load as the server's log service hands it over (cases below); the
//                                              end hook is a stand-in for SrvRemote::ReportAndExit (the fatal record,
//                                              then TerminateProcess with the code), unless the case is direct mode
//   savemods_unit.exe fit                      the proxy's ProxyReport::UseFatalRecord (proxy/src/proxy_report.h) over a
//                                              fixed table: "FIT <name> <result> <why>"
// Cases: end, end-nohash (the hook ends the process at the first "Deserialization Error"); direct, direct-nohash (no
// hook: the load reads on; a later render end gets the save-mod-mismatch record); render-nosentinel (the mismatch alone:
// a render end keeps its code); noload (a sentinel line before any load: no end); same (no mismatch, no sentinel: nothing
// changes, D239); second (a hook that returns: called once for two sentinel lines).
// Server functions stood in: SrvLog (stdout and <run dir>\server.log), SrvRunDir, SrvCfgStr, SrvNow, SrvBuildIdText, and
// the save container's Civ5* readers (no save file is read here: the load has no file name, its body prefix is short).
// Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
#include "../../../proxy/src/proxy_report.h"   // first: its STL settings (pure inline UseFatalRecord only)
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "remote/srv_exe_crash.h"
#include "remote/srv_remote_savemods.h"
#include "srv_civ5save.h"

namespace
{
std::string g_runDir;
LARGE_INTEGER g_t0, g_freq;
CRITICAL_SECTION g_cs;
int g_hookCalls = 0;
}  // namespace

void SrvLog(const char* szFormat, ...)
{
	char sz[8192];
	va_list v;
	va_start(v, szFormat);
	vsnprintf(sz, sizeof(sz), szFormat, v);
	va_end(v);
	EnterCriticalSection(&g_cs);
	printf("[log] %s\n", sz);
	fflush(stdout);
	if(!g_runDir.empty())
	{
		FILE* f = NULL;
		if(!fopen_s(&f, (g_runDir + "\\server.log").c_str(), "ab") && f)
		{
			fprintf(f, "[%9.3f] %s\n", 0.0, sz);
			fclose(f);
		}
	}
	LeaveCriticalSection(&g_cs);
}

const char* SrvRunDir() { return g_runDir.c_str(); }
std::string SrvCfgStr(const char*, const char* szDefault) { return szDefault ? szDefault : ""; }
double SrvNow()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)(t.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart;
}
void SrvBuildIdText(char* out, size_t outSize) { _snprintf_s(out, outSize, _TRUNCATE, "build: savemods_unit (ticket 0744 test)"); }

// The save container's readers: never reached here (no file name, a body prefix under 64 bytes); false if they were.
Civ5SaveHeader::Civ5SaveHeader() : saveVersion(8), turn(0), gameType(0), tail(0) { memset(dllGuid, 0, sizeof(dllGuid)); }
bool Civ5ParseHeader(const std::string&, Civ5SaveHeader&, size_t&, std::string& why) { return why = "stand-in", false; }
bool Civ5Split(const std::string&, Civ5SaveParts&, std::string& why) { return why = "stand-in", false; }
bool Civ5Inflate(const std::string&, const Civ5SaveParts&, std::string&, std::string& why) { return why = "stand-in", false; }
bool Civ5ReadFile(const char*, std::string&) { return false; }

namespace
{
// SrvRemote::ReportAndExit's steps that matter here: the fatal record for every code but 0 and 3, the result line, the end
void StandinReportAndExit(int code, const char* szWhy)
{
	++g_hookCalls;
	printf("END %d\n", code);
	fflush(stdout);
	if(code != 0 && code != 3)
		SrvExeCrash::WriteFatalRecord(code, szWhy ? szWhy : "");
	SrvLog("result: FAILED: %s", szWhy ? szWhy : "");
	SrvLog("remote: exit code %d (%s)", code, szWhy ? szWhy : "");
	fflush(stdout);
	TerminateProcess(GetCurrentProcess(), (UINT)code);
	for(;;)
		Sleep(1000);
}

void ReturningHook(int code, const char*)
{
	++g_hookCalls;
	printf("HOOK %d\n", code);
	fflush(stdout);
}

// The DLL's lines as its CUSTOMLOG writes them (CvGame::Read's warning, CheckSentinel's error)
const char* kMismatch = "CvGame.cpp[11010]: CvGame::Read - WARNING - Save data hash mismatch! Load will be attempted but "
                        "corruption or crash is likely. Save Hash = [0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49] Game Hash = "
                        "[0x4510836f-0x6b3aac37-0x0fa52c26-0x20ec3e4f]";
const char* kSentinel = "CustomMods.cpp[618]: CheckSentinel - Deserialization Error\n";
const char* kOther = "CustomMods.cpp[100]: CustomMods::prefetchCache - Cached 412 options";

void Begin()
{
	// the hash bytes of the save (as in its body at 4..19) and a short body prefix (the save is never searched for)
	const unsigned char hash[16] = {0xf9, 0x10, 0x1c, 0x9b, 0xa1, 0xb0, 0x3d, 0xbd,
	                                0xfe, 0xec, 0x27, 0x27, 0x49, 0x6a, 0x65, 0x06};
	SrvSaveMods::BeginLoad(NULL, hash, "Release-5.4.8-unit", std::string("short prefix"));
}

void Log(const char* text) { SrvSaveMods::OnDllLog("CustomMods.log", text); }

void Fatal(int code, const char* why)
{
	const bool wrote = SrvExeCrash::WriteFatalRecord(code, why);
	printf("WROTE %d\n", wrote ? 1 : 0);
	fflush(stdout);
}

int Case(const std::string& name)
{
	printf("PID %lu\n", GetCurrentProcessId());
	fflush(stdout);
	if(name == "end" || name == "end-nohash")
	{
		SrvSaveMods::SetEndHook(&StandinReportAndExit);
		Begin();
		Log(kOther);
		if(name == "end")
			Log(kMismatch);
		Log(kSentinel);
		printf("RETURNED\n");   // must not be reached
		return 0;
	}
	if(name == "direct" || name == "direct-nohash")
	{
		Begin();
		if(name == "direct")
			Log(kMismatch);
		Log(kSentinel);
		Log(kSentinel);
		printf("RETURNED\n");
		Fatal(5, name == "direct" ? "ice: the render pass faulted" : "units: the unit readiness was lost");
		return 0;
	}
	if(name == "render-nosentinel")
	{
		SrvSaveMods::SetEndHook(&StandinReportAndExit);
		Begin();
		Log(kMismatch);
		printf("RETURNED\n");
		Fatal(5, "ice: the render pass faulted");
		return 0;
	}
	if(name == "noload")
	{
		SrvSaveMods::SetEndHook(&StandinReportAndExit);
		Log(kSentinel);
		printf("RETURNED\n");
		return 0;
	}
	if(name == "same")
	{
		SrvSaveMods::SetEndHook(&StandinReportAndExit);
		Begin();
		Log(kOther);
		Log("CvGame.cpp[11020]: CvGame::Read - Potential savefile format mismatch");
		printf("RETURNED\n");
		Fatal(6, "exe_exception: the engine threw a C++ exception in ICvPlayer1::Read");
		return 0;
	}
	if(name == "second")
	{
		SrvSaveMods::SetEndHook(&ReturningHook);
		Begin();
		Log(kMismatch);
		Log(kSentinel);
		Log(kSentinel);
		printf("RETURNED hooks=%d\n", g_hookCalls);
		return 0;
	}
	printf("unknown case %s\n", name.c_str());
	return 2;
}

int Fit()
{
	struct Row
	{
		const char* name;
		bool ended;
		unsigned long exit, pid, recExit;
		bool recHasPid;
		unsigned long recPid;
	};
	static const Row rows[] = {
	    {"match", true, 5, 100, 5, true, 100},
	    {"match-nopid", true, 5, 100, 5, false, 0},
	    {"running", false, 259, 100, 5, false, 0},
	    {"av-pid", true, 0xC0000005ul, 100, 5, true, 100},
	    {"cxx-pid", true, 0xE06D7363ul, 100, 6, true, 100},
	    {"bp-pid", true, 0x80000003ul, 100, 5, true, 100},
	    {"av-nopid", true, 0xC0000005ul, 100, 5, false, 0},
	    {"av-otherpid", true, 0xC0000005ul, 100, 5, true, 104},
	    {"av-pid-unknown", true, 0xC0000005ul, 0, 5, true, 0},
	    {"ctrlc-pid", true, 0xC000013Aul, 100, 5, true, 100},
	    {"exit1-pid", true, 1, 100, 5, true, 100},
	    {"exit6-pid", true, 6, 100, 5, true, 100},
	};
	for(size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
	{
		const Row& r = rows[i];
		std::string why;
		const ProxyReport::FatalUse u = ProxyReport::UseFatalRecord(r.ended, r.exit, r.pid, r.recExit, r.recHasPid, r.recPid, &why);
		printf("FIT %s %d %s\n", r.name, (int)u, why.empty() ? "-" : why.c_str());
	}
	fflush(stdout);
	return 0;
}
}  // namespace

int main(int argc, char** argv)
{
	InitializeCriticalSection(&g_cs);
	QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&g_t0);
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	if(argc >= 4 && !strcmp(argv[1], "case"))
	{
		g_runDir = argv[3];
		return Case(argv[2]);
	}
	if(argc >= 2 && !strcmp(argv[1], "fit"))
		return Fit();
	printf("usage: savemods_unit case <name> <run dir> | fit\n");
	return 2;
}
