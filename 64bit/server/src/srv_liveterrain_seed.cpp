// srv_liveterrain_seed.cpp - the map seed for live terrain (epic 0615, ticket 4912). Written 2026-11-20 by w3-4912 for
// this repository. SPDX-License-Identifier: GPL-3.0-or-later
//
// The coastline jitter of the EXE's region fill is the first rand() after srand(CvPreGame::mapRandomSeed) in CreateTerrain
// (research/05-graphics-composition/live-terrain.md 8.10), so the server's region producer needs the load's map seed. The
// render units (server/src/render/) are also built into test and tool binaries that have no DLL and no DLL interface
// headers; this file, which only server.exe compiles (server/src/*.cpp), registers the reader with
// liveterrain::SetMapSeedSource at static initialisation, and the scene (srv_liveterrain_scene.cpp) calls it on the render
// thread when a load's field is handed over to LiveTerrain.
//
// The read: the module of this process that exports DllGetGameContext (the DLL the server hosts, in direct and in remote
// mode; found once and kept, the DLL is never unloaded while the server runs), its singleton context, GetPreGame(),
// mapRandomSeed(), Destroy(). Thread safety: GetPreGame() and Destroy() allocate and free a small wrapper on the DLL's own
// heap (CvDllGameContext::Allocate: HeapCreate without HEAP_NO_SERIALIZE, so any thread may use it); mapRandomSeed() reads
// one aligned 32-bit static (CvPreGame's s_mapRandomSeed), written by the game thread only in PreGame::read / reset /
// setMapRandomSeed, i.e. while a load or a new game is set up, before the EXE builds the field the render thread is
// handed. A load that starts while the render thread still takes the previous load's field could give the newer seed
// [I: not seen; the field of the newer load follows within the same poll sequence and is read again].
//
// The D118 report (the MC's #8552 condition 1): a seed that cannot be read (or reads 0) makes the scene refuse that load's
// land/water changes live and call the writer registered here once per session, which writes the server's non-fatal
// report (srv_exe_crash.h ReportServerFailure, as ticket 0573's: the game goes on).
#include "srv_common.h"
#include "remote/srv_exe_crash.h"
#include <psapi.h>
#include <string>

namespace srvrender
{
namespace liveterrain
{
// as declared in render/srv_liveterrain.h (not included: its include path is the render builders'); a mismatch is a link
// error (the parameter types are in the mangled name)
typedef bool (*MapSeedFn)(unsigned int& seed, std::string& why);
void SetMapSeedSource(MapSeedFn fn);
typedef void (*MapSeedFaultFn)(const std::string& why);
void SetMapSeedFaultReport(MapSeedFaultFn fn);
}  // namespace liveterrain
}  // namespace srvrender

namespace
{
typedef ICvGameContext1* (*GetContextFn)();

GetContextFn FindContextExport()
{
	static GetContextFn s_fn = NULL;   // render thread only
	if(s_fn)
		return s_fn;
	HMODULE mods[512];
	DWORD need = 0;
	if(!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
		return NULL;
	const DWORD n = (need / sizeof(HMODULE)) < 512 ? need / sizeof(HMODULE) : 512;
	for(DWORD i = 0; i < n; ++i)
		if(FARPROC p = GetProcAddress(mods[i], "DllGetGameContext"))
		{
			s_fn = reinterpret_cast<GetContextFn>(p);
			break;
		}
	return s_fn;
}

bool ReadMapSeed(unsigned int& seed, std::string& why)
{
	const GetContextFn fn = FindContextExport();
	if(!fn)
	{
		why = "no module of this process exports DllGetGameContext";
		return false;
	}
	ICvGameContext1* ctx = fn();
	if(!ctx)
	{
		why = "DllGetGameContext returned NULL";
		return false;
	}
	ICvPreGame1* p = ctx->GetPreGame();
	if(!p)
	{
		why = "the context's GetPreGame returned NULL";
		return false;
	}
	seed = p->mapRandomSeed();
	p->Destroy();
	return true;
}

void ReportSeedFault(const std::string& line)
{
	SrvExeCrash::ServerFailure f;
	f.stage = "live-terrain-seed";
	f.title = "the map seed of the loaded game could not be read, ticket 4912";
	f.code = 0;
	f.codeName = "none (not a crash: the game goes on)";
	f.summary = "The 64-bit server could not read the map's random seed. The game goes on; terrain that changes between "
	            "land and water during play is shown only after the game is loaded again. The 64-bit server wrote this report.";
	f.intro = "The 64-bit server reads the map's random seed from the game DLL when a game is loaded, so that it can draw\n"
	          "coasts that change during play as the game does. It could not read it for this game. Nothing else is\n"
	          "wrong: the game goes on, and land that turns to water (or water to land) is drawn after the next load.\n"
	          "Please send this folder in.\n";
	f.failure = line + "\n";
	bool on = false;
	const std::string folder = SrvExeCrash::ReportServerFailure(f, &on);
	SrvLog("live terrain: %s", on ? ("D118 report: " + (folder.empty() ? std::string("(the folder could not be made)") : folder)).c_str()
	                              : "the D118 report is off (exe_crash_report)");
}

struct Register
{
	Register()
	{
		srvrender::liveterrain::SetMapSeedSource(&ReadMapSeed);
		srvrender::liveterrain::SetMapSeedFaultReport(&ReportSeedFault);
	}
};
Register s_register;
}  // namespace
