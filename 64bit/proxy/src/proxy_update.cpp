// proxy_update.cpp - the proxy's Update hook (issue 2502). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ProxyRt::Begin calls ProxyRtInt::BeforeUpdate() on the calling thread (the EXE's game-core thread; with host=headless
// the host's only thread) at the top of each ICvGame1::Update, before the Update is staged and forwarded. It polls the
// harness Lua channel the proxy serves against the engine VM (issue 2505), as the DLL's own channel polls at the top of
// CvGame::update. The Lua channel may make calls of its own from there (engine Lua -> DLL Lua functions -> kRt_LuaCall on
// the same channel): the Update has not begun yet, so they are ordinary top-level calls.
//
// Why here and not in a generated hook: on the proxy side the generator treats ICvGame1::Update as a special method
// (proxy/gen/gen_runtime.py, Meth.special = 'Update'): it emits no ProxyHook_ declaration for it, and a hand-written
// ProxyHook_<I>_Update would be called by the generated body without a declaration and is listed as "ignored" in the
// generator's summary. ProxyRt::Begin sees every outbound method id, and the runtime knows Update's ids by model key
// (proxy_rt_logic.cpp RoleOfKey), so the hook needs no generator change.
//
// ProxyLuaExec::Poll is 2505's (proxy/src/proxy_luaexec.cpp, declared in proxy/src/proxy_luaexec.h as
// `namespace ProxyLuaExec { void Poll(); }`). Until that file is linked, the linker's /alternatename below resolves the
// call to ProxyUpdateDefault::Poll; once proxy_luaexec.cpp defines the real function, the real one wins (an
// alternate name is used only for a symbol nobody defines).
#include "proxy_rt_internal.h"
#include "proxy_ifaces.gen.h"
#include "proxy_rt_logic.h"
#include "proxy_local.h"
#include "proxy_luaexec.h"
#include "proxy_shell.h"
#include "proxy_ui_bridge_rt.h"
#include "proxy_leaderscene.h"
#include "proxy_pacing.h"
#include <stdio.h>
#include <string.h>
#include <string>

extern "C" ICvGameContext1* DllGetGameContext();  // proxy_rt.cpp: after the boot, the context wrapper

namespace ProxyLuaExec
{
void Poll();  // the declaration agreed with 2505 (proxy_luaexec.h)
}

namespace ProxyUpdateDefault
{
void Poll()
{
	static bool s_logged = false;
	if (!s_logged)
	{
		s_logged = true;
		ProxyRtInt::Log("Update hook: no ProxyLuaExec::Poll linked (issue 2505 not in this build); the default does nothing");
	}
}
}  // namespace ProxyUpdateDefault

// void __cdecl f(void) in a namespace mangles the same for x86 and x64.
#pragma comment(linker, "/alternatename:?Poll@ProxyLuaExec@@YAXXZ=?Poll@ProxyUpdateDefault@@YAXXZ")

// ================================================================================================ ini settings
// Issue 2805: the vpproxy.ini settings as this process reads them, read once (proxy_rt.cpp's own copy is private to it;
// the same file: VPPROXY_INI, else <proxy dir>\\vpproxy.ini). First called at the boot (ProxyLocal::InitTables).
namespace
{
bool ReadFileText(const std::string& path, std::string& out)
{
	FILE* f = NULL;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
	char buf[4096];
	size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

std::string ModuleFile(HMODULE h)
{
	char p[MAX_PATH * 2] = {};
	GetModuleFileNameA(h, p, sizeof(p));
	return p;
}
}  // namespace

const ProxyLogic::Settings& ProxyIniSettings()
{
	static ProxyLogic::Settings s_settings;
	static bool s_read = false;
	if (s_read) return s_settings;
	s_read = true;
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCSTR>(&ProxyIniSettings), &self);
	const std::string proxyDir = ProxyLogic::DirOf(ModuleFile(self));
	const std::string exePath = ModuleFile(NULL);
	char env[MAX_PATH * 2];
	const DWORD n = GetEnvironmentVariableA("VPPROXY_INI", env, sizeof(env));
	const std::string iniPath = n > 0 && n < sizeof(env) ? std::string(env) : proxyDir + "\\vpproxy.ini";
	std::string text;
	const bool found = ReadFileText(iniPath, text);
	ProxyLogic::ParseSettings(text, found, iniPath, proxyDir, ProxyLogic::DirOf(exePath), s_settings);
	return s_settings;
}

// ================================================================================================ test slot poll
// Issue 2805 (finding 6 of the epic-2700 verification): the 32-bit headless host has no slot poll, so a headless verify
// run (local_answers=1 local_verify=1 strict=1) checked only 47 mirror answers, while the real EXE asks the 24 mirrored
// getters about 575 times per frame. vpproxy.ini test_slot_poll=N makes the proxy itself play the EXE's slot poll N
// times at the top of every ICvGame1::Update (before the Update is staged, where the Lua channel polls): it asks the
// context for the game, pregame and game-asynch wrappers and, for every player slot, the player wrapper, and calls the
// mirrored getters on them as virtual calls, so each call takes exactly the path of the EXE's calls (generated proxy
// method -> proxy_hooks_local.cpp hook -> VpMirrored / canonical handles, the lock-want count). The first pass of an
// Update reads (the previous Update moved the epoch), the next N-1 are mirror hits, each verified with local_verify=1.
//
// Never in a live game: the key is honoured only with host=headless (the 32-bit headless host; the real EXE needs
// host=exe for its thread binding) AND when the process's EXE image name does not start with "CivilizationV" (every
// game EXE: CivilizationV.exe, CivilizationV_DX11.exe, CivilizationV_Tablet.exe). Otherwise the key is logged as refused
// and ignored. The proxy reads the key from the same vpproxy.ini as proxy_rt.cpp (its settings object is private there).
namespace
{
uint32_t g_pollPasses = 0;  // test_slot_poll= when honoured, else 0
bool g_pollDecided = false;
uint64_t g_pollUpdates = 0, g_pollGetters = 0, g_pollLua = 0, g_pollLuaFail = 0;

void DecidePoll()
{
	g_pollDecided = true;
	const ProxyLogic::Settings& s = ProxyIniSettings();
	const std::string exePath = ModuleFile(NULL);
	if (s.testSlotPoll == 0) return;
	const size_t slash = exePath.find_last_of("\\/");
	const std::string exeName = slash == std::string::npos ? exePath : exePath.substr(slash + 1);
	const bool gameExe = _strnicmp(exeName.c_str(), "CivilizationV", 13) == 0;
	if (!s.singleThreadHost || gameExe)
	{
		ProxyRtInt::LogErrPublic("test_slot_poll=%u REFUSED (ignored): it runs only with host=headless outside the game "
		                         "EXE (host=%s, EXE %s)", (unsigned)s.testSlotPoll, s.singleThreadHost ? "headless" : "exe",
		                         exeName.c_str());
		return;
	}
	g_pollPasses = s.testSlotPoll;
	ProxyRtInt::Log("test_slot_poll=%u: the proxy plays the EXE's slot poll %u times at the top of every ICvGame1::Update "
	                "(%d player slots; a TEST setting, headless only; EXE %s)", (unsigned)g_pollPasses,
	                (unsigned)g_pollPasses, (int)MAX_PLAYERS, exeName.c_str());
}

// One pass: the getters of proxy_hooks_local.cpp's mirrored list, as the EXE calls them. Returns the getter calls made.
uint32_t PollPass(ICvGameContext1* ctx)
{
	uint32_t calls = 0;
	ICvGame1* g = ctx->GetGame();  // held for the whole pass, as the EXE's poll holds it: player wrappers lock no deeper
	if (!g) return 0;
	ICvPreGame1* pg = ctx->GetPreGame();
	ICvGameAsynch1* ga = ctx->GetGameAsynch();
	volatile int sink = 0;  // the results are not used; the calls are what is tested
	sink += ctx->IsGraphicsInitialized() ? 1 : 0;
	sink += (int)g->GetActivePlayer();
	sink += (int)g->GetActiveTeam();
	sink += g->GetGameTurn();
	sink += g->CountNumHumanGameTurnActive();
	sink += (int)g->GetGameState();
	sink += g->GetNumGameTurnActive();
	sink += g->IsFinalInitialized() ? 1 : 0;
	sink += g->IsNetworkMultiPlayer() ? 1 : 0;
	sink += g->IsOption(GAMEOPTION_END_TURN_TIMER_ENABLED) ? 1 : 0;
	sink += g->IsOption(GAMEOPTION_QUICK_COMBAT) ? 1 : 0;
	sink += g->IsPaused() ? 1 : 0;
	calls += 12;
	if (ga)
	{
		sink += (int)ga->GetActiveTeam();
		sink += ga->IsFinalInitialized() ? 1 : 0;
		calls += 2;
	}
	if (pg)
	{
		sink += pg->isNetworkMultiplayerGame() ? 1 : 0;
		++calls;
	}
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		const PlayerTypes p = (PlayerTypes)i;
		ICvPlayer1* pl = g->GetPlayer(p);
		if (pl)
		{
			sink += pl->IsHuman() ? 1 : 0;
			sink += pl->IsAlive() ? 1 : 0;
			sink += pl->IsEverAlive() ? 1 : 0;
			sink += pl->IsTurnActive() ? 1 : 0;
			sink += (int)pl->GetID();
			sink += (int)pl->GetTeam();
			calls += 6;
			// The EXE asks the option through ICvPlayer2::IsOptionKey (the mirrored, pure key); ICvPlayer1::IsOption
			// is another method, not mirrored and impure by default (it moved the epoch once per slot in the first
			// test run, 2026-11-10 23:25). The QueryInterface of a local wrapper is answered in the proxy.
			ICvPlayer2* p2 = pl->QueryInterface<ICvPlayer2>();
			if (p2)
			{
				sink += p2->IsOptionKey(PLAYEROPTION_NO_UNIT_CYCLING) ? 1 : 0;
				++calls;
				p2->Destroy();
			}
			pl->Destroy();
		}
		if (pg)
		{
			sink += (int)pg->slotStatus(p);
			sink += (int)pg->civilization(p);
			sink += (int)pg->playerColor(p);
			calls += 3;
		}
	}
	if (ga) ga->Destroy();
	if (pg) pg->Destroy();
	g->Destroy();
	return calls;
}

// Issue 2805: the Lua leg of the test poll. The headless host runs no UI Lua, and its own Lua (the map script) runs
// inside DLL calls (phase in_flight), so the UI-Lua mirror would never answer headless and its negative test could not
// fail there. This chunk plays a UI that reads the game from Lua between impure calls: it runs on the proxy's
// persistent engine Lua thread (proxy_luaexec.cpp) at the top of every Update, in the "Main" environment, and asks pure
// Lua bindings N times in a row (the first round reads, the next N-1 can be mirror hits, verified with local_verify=1).
// Values that change between Updates (the turn, plot visibility, unit damage, city population) make a stale mirror
// visible at the next Update; calls returning objects (Map.GetPlot, Players[i]:Units()) cross every time.
// Only pure-listed functions (lua-pure-bindings.tsv): with lua_mirror=1 none of them moves the epoch, so the leg
// leaves the getter mirror and its stale test intact. Objects (plots by Map.GetPlot, capitals by GetCapitalCity:
// instance results, never kept, crossing every time) are collected once per Update; then n rounds of scalar reads
// (n = the test_slot_poll value, prepended by RunTestChunk). With lua_mirror=0 every Lua call moves the epoch as
// before, so this leg hides local_test=stale there, exactly as other impure calls hide it in the real EXE.
const char* const kTestLuaLeg =
    "local plots, cities, calls = {}, {}, 0\n"
    "for k = 0, 63 do\n"
    "  local pl = Map.GetPlot((k * 7) % 32, (k * 5) % 20) ; calls = calls + 1\n"
    "  if pl ~= nil then plots[#plots + 1] = pl end\n"
    "end\n"
    "for i = 0, 63 do\n"
    "  local p = Players[i]\n"
    "  if p ~= nil then\n"
    "    local c = p:GetCapitalCity() ; calls = calls + 1\n"
    "    if c ~= nil then cities[#cities + 1] = c end\n"
    "  end\n"
    "end\n"
    "for pass = 1, n do\n"
    "  Game.GetGameTurn() ; Game.GetActivePlayer() ; calls = calls + 2\n"
    "  for i = 0, 63 do\n"
    "    local p = Players[i]\n"
    "    if p ~= nil then p:IsAlive() ; p:IsEverAlive() ; p:GetTeam() ; p:IsMinorCiv() ; calls = calls + 4 end\n"
    "  end\n"
    "  for _, c in ipairs(cities) do c:GetOwner() ; c:IsCapital() ; c:GetMaxHitPoints() ; c:IsHasBuilding(0) ; calls = calls + 4 end\n"
    "  for _, pl in ipairs(plots) do pl:GetIndex() ; pl:IsRevealed(0, false) ; pl:IsRevealed(1, false) ; pl:GetNumUnits() ; calls = calls + 4 end\n"
    "end\n"
    "return calls\n";

void TestSlotPoll()
{
	if (!g_pollDecided) DecidePoll();
	if (!g_pollPasses) return;
	ICvGameContext1* ctx = DllGetGameContext();
	if (!ctx) return;
	uint32_t calls = 0;
	for (uint32_t k = 0; k < g_pollPasses; ++k) calls += PollPass(ctx);
	// Issue 2805: the Lua leg (one chunk per Update, N rounds inside it).
	{
		int luaCalls = -1;
		std::string err;
		if (ProxyLuaExec::RunTestChunk(kTestLuaLeg, (int)g_pollPasses, luaCalls, err))
		{
			if (luaCalls > 0) g_pollLua += (uint64_t)luaCalls;
		}
		else if (++g_pollLuaFail <= 3)
			ProxyRtInt::Log("test_slot_poll: the Lua leg failed: %s", err.c_str());
	}
	g_pollGetters += calls;
	if ((++g_pollUpdates % 100) == 1)
		ProxyRtInt::Log("test_slot_poll: %I64u Updates polled, %I64u getter calls and %I64u Lua binding calls so far (%I64u "
		                "Lua leg failures)", g_pollUpdates, g_pollGetters, g_pollLua, g_pollLuaFail);
}
}  // namespace

namespace ProxyRtInt
{
void BeforeUpdate()
{
	static bool s_logged = false;
	if (!s_logged)
	{
		s_logged = true;
		Log("Update hook: first ICvGame1::Update on thread %u; polling the Lua channel before each Update",
		    (unsigned)GetCurrentThreadId());
	}
	ProxyShell::OnUpdate();  // issue 9040: the view-mask re-check of the D33 configuration (proxy_shell.h)
	ProxyPacing::OnUpdate();  // ticket 4662 (D165 option 3): [Performance] AiUpdateTickMs; off = nothing (proxy_pacing.h)
	ProxyLuaExec::Poll();
	ProxyUiBridgeRt::OnUpdate();  // issue 0178: the Lua bridge's heartbeat (no Lua here; proxy_ui_bridge_rt.h)
	ProxyLeaderScene::OnUpdate();  // ticket 0381: 3D leader scenes for majors above slot 21 (proxy_leaderscene.h)
	TestSlotPoll();  // issue 2805: one flag test unless test_slot_poll= is set and honoured
}

void AfterUpdate()
{
	ProxyPacing::OnAfterUpdate();  // ticket 4665 (D165 option 2): [Performance] AiSkipVisualDrain; off = nothing
}
}  // namespace ProxyRtInt

// For the Lua channel (2505, proxy_luaexec.h): the engine utility the engine passed to SetDLLIFace (NULL before / after).
extern "C" ICvEngineUtility1* VpProxy_EngineUtility()
{
	return ProxyRtInt::EngineUtility();
}

// For the Lua channel (2505, proxy_luaexec.h), added by the team-2500 coordinator 2026-11-10: the server thread serving
// this thread's channel releases every level of the DLL's game-core lock it holds (the CvDllGame wrapper of the engine's
// GetGame took it) while a chunk runs in the engine VM, as the DLL's own channel does (CvLuaSupport.cpp
// PollExternalLuaRequest: HasGameCoreLock / ReleaseGameCoreLock / GetGameCoreLock around CallCFunction). Returns the
// depth released, or -1 when the call failed (the chunk then runs with the lock held; proxy_luaexec.cpp logs it).
extern "C" int VpProxy_CoreLockYield()
{
	vpipc::Channel& ch = ProxyRt::Chan();
	ProxyRt::Begin(ch, vpw::kRt_CoreLockYield);
	vpipc::Reader r;
	if (!ProxyRt::End(ch, vpw::kRt_CoreLockYield, r)) return -1;
	const int32_t d = r.i32();
	return r.ok() ? (int)d : -1;
}

extern "C" void VpProxy_CoreLockResume(int depth)
{
	if (depth <= 0) return;
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ProxyRt::Begin(ch, vpw::kRt_CoreLockResume);
	w.i32(depth);
	vpipc::Reader r;
	ProxyRt::End(ch, vpw::kRt_CoreLockResume, r);
}
