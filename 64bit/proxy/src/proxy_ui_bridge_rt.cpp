// proxy_ui_bridge_rt.cpp - the proxy glue of the Lua bridge (issue 0178). Hand-written. The bridge itself:
// proxy_ui_bridge.h/.cpp (no proxy runtime dependency, unit-tested in proxy/tests/ui_bridge/).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What this file adds to the proxy:
//   - the switches: vpproxy.ini ui_bridge=0|1 (default 0) and ui_bridge_test=0|1 (proxy_rt_logic.h);
//   - the main thread: the thread bound to the ui channel (the EXE's boot thread, which runs the UI's Lua);
//   - the post hook: a record made on the main thread goes on the ui channel as a vpipc POST (vp_wire.h kRt_UiRecord),
//     flushed at once. A full ring makes end_post wait for the server to drain it (ipc/PROTOCOL.md section 4), so no
//     record is dropped and no synchronous fallback is needed; a post that fails (no server attached, peer gone) is
//     counted and logged, never retried as a call (a failed call would run the proxy's failure policy);
//   - ProxyUiBridgeRt::OnStateRegistered: called by proxy_lua.cpp's RegisterScriptLibraries hook (main thread) after the
//     DLL's globals were set in the state;
//   - ProxyUiBridgeRt::OnUpdate: called by proxy_update.cpp's BeforeUpdate (game-core thread): the heartbeat and a stats
//     line every 3,600 Updates;
//   - issue 4754: ProxyUiBridgeRt::RequestUnitPositions, the unit option's positions-only install (4759 calls it), and
//     its TEST ONLY switch, the environment variable VPPROXY_TEST_UI_POSITIONS=1 of the EXE's process (live runs before
//     4759 lands), read once with the ini.
//   - ticket 0199 part 2: ProxyUiBridgeRt::RequestArrows, the arc option's arrows install (proxy_shell.cpp's ArcLines
//     calls it with [Renderer] ServerArc = 1);
//   - ticket 0300 part 2: ProxyUiBridgeRt::RequestPath, the path option's install (proxy_shell.cpp's PathLines calls it
//     with [Renderer] ServerPath = 1), and its tests hook PathTests (the EXE's DLL calls, path-models.md 7.1);
//   - ticket 0487: the growth of the EXE's UI draw buffers (proxy_uidraw.h), always on since one-off 9081 (the owner,
//     2026-11-22; it was VP64Options.ini [Renderer] UiDrawListGrowth, default 1): its frame hook is the bridge's drain (installed alone when ui_bridge=0), and its
//     failure is reported here as the D119 report with the non-fatal stage "uidraw" and the helper's popup, once per
//     session, on the game-core thread (never on the main thread inside a Lua listener); the game goes on.
//     Interim form (MC 2026-11-17, #0487 thread): the report is built from ProxyIniSettings(); a shared
//     ProxyRtInt::ReportNonFatal in proxy_rt.cpp (the same Input as ReportFailure) replaces it after epic 0437 merges.
#include "proxy_rt_internal.h"
#include "proxy_rt_logic.h"
#include "proxy_report.h"
#include "proxy_session.h"
#include "proxy_ui_bridge.h"
#include "proxy_ui_bridge_rt.h"
#include "proxy_uidraw.h"
#include "proxy_livereload.h"   // ticket 0586
namespace ProxyCombatHold  // proxy_combat_hold.cpp (ticket 4773, route S): its drop runs in the bridge's frame hook
{
void Init(void (*existingFrameHook)());
void FrameHook();
}
namespace ProxySoundExec  // proxy_sound_exec.cpp (ticket 4723, route S): the server's sound records, in the frame hook
{
void Init(void (*existingFrameHook)());
void FrameHook();
}
namespace ProxyEffectPlayer  // proxy_effect_player.cpp (ticket 4727, route S): the server's effect records, in the frame hook
{
void Init(void (*existingFrameHook)());
void FrameHook();
}
namespace ProxyRouteS  // proxy_route_s.cpp (ticket 4783, MC #8543): the three parts arm as one, at the server's question
{
void Init(void (*existingFrameHook)());
void FrameHook();
}
namespace ProxyMinimapFog  // proxy_minimap_fog.cpp (ticket 0727): the minimap's unexplored cells, in the frame hook
{
void Init(void (*existingFrameHook)());
void FrameHook();
void CountersLine(char* out, size_t cap);
namespace Test
{
long Noted();
}
}
#include "vp64_options.h"

const ProxyLogic::Settings& ProxyIniSettings();  // proxy_update.cpp
void ProxyBuildIdText(HMODULE self, char* out, size_t outSize);  // proxy_buildid.cpp

namespace
{
volatile LONG g_init = 0;
volatile LONG g_postFailures = 0;
uint32_t g_updates = 0;  // core thread only
volatile LONG g_uiThread = 0;   // issue 4754: the thread bound to the ui channel, seen at a registration (0 = not yet)

// Ticket 0487: the uidraw failure, handed from the main thread (Fail) to the core thread (OnUpdate).
volatile LONG g_uidrawReport = 0;  // 0 none, 1 pending, 2 written
char g_uidrawCode[64];
char g_uidrawDetail[400];

void LogLine(const char* line)
{
	ProxyRtInt::Log("%s", line);
}

void PostRecord(const UiBridge::Record& r)
{
	uint32_t chan = 0;
	if (!ProxyRtInt::ThreadChannel(chan) || chan != vpipc::kChannelUi)
	{
		// The bridge calls the hook on its main thread only, which is the ui thread; anything else is a bug.
		if (InterlockedIncrement(&g_postFailures) <= 3)
			ProxyRtInt::Log("ui bridge: post refused: the calling thread %lu is not bound to the ui channel",
			                GetCurrentThreadId());
		return;
	}
	vpipc::Channel& ch = ProxyRt::Chan();
	vpipc::Writer& w = ch.begin_post(vpw::kRt_UiRecord);
	w.bytes(&r, (uint32_t)sizeof(r));
	const vpipc::Status st = ch.end_post();
	if (st == vpipc::Status::Ok)
	{
		ch.flush();
		return;
	}
	const LONG n = InterlockedIncrement(&g_postFailures);
	if (n <= 3 || (n % 1000) == 0)
		ProxyRtInt::Log("ui bridge: post of record seq %u (kind %u) failed: %s (%ld failures)", r.seq, (unsigned)r.kind,
		                vpipc::status_name(st), n);
}

// ---- ticket 0487 ---------------------------------------------------------------------------------------------------
void UiDrawReportHook(const char* code, const char* detail)
{
	if (InterlockedCompareExchange(&g_uidrawReport, 1, 0) != 0) return;
	strcpy_s(g_uidrawCode, sizeof(g_uidrawCode), code ? code : "?");
	strcpy_s(g_uidrawDetail, sizeof(g_uidrawDetail), detail ? detail : "");
	MemoryBarrier();
}

HMODULE SelfModule()
{
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCSTR>(&SelfModule), &self);
	return self;
}

std::string ModuleFile(HMODULE h)
{
	char p[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameA(h, p, sizeof(p));
	return n > 0 && n < sizeof(p) ? std::string(p) : std::string();
}

// This boot's run folder: the newest folder under the run root named for this process (ProxyLogic::RunFolderName:
// <date>-<time>-<pid>-<n>), as proxy_rt.cpp's ReadConfig/MakeRunFolder place it. "" when none is found.
std::string FindRunFolder(const ProxyLogic::Settings& s, const std::string& cfgIni)
{
	std::string root = s.runRoot;
	if (root == s.x64Dir + "\\run")
	{
		const std::string logs = ProxyLogic::UserLogsDir(root, cfgIni);
		if (!logs.empty()) root = logs + "\\run";
	}
	char tag[32];
	_snprintf_s(tag, sizeof(tag), _TRUNCATE, "-%lu-", GetCurrentProcessId());
	std::string best;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return std::string();
	do
	{
		const std::string name = fd.cFileName;
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ProxyLogic::IsRunFolderName(name) &&
		    name.find(tag) == 15 && name > best)
			best = name;
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return best.empty() ? std::string() : root + "\\" + best;
}

// The D119 report of the non-fatal stage "uidraw" and the helper's popup (a stock-session popup: the game goes on).
void WriteUiDrawReport()
{
	const ProxyLogic::Settings& s = ProxyIniSettings();
	ProxyReport::Input in;
	in.stage = "uidraw";
	in.code = g_uidrawCode;
	in.details.push_back(g_uidrawDetail);
	in.details.push_back("non-fatal (ticket 0487, D118): the game goes on with the EXE's stock 1 MB / 4,096-command UI "
	                     "draw buffers; more than about 450 unit flags in view can crash it");
	in.d33 = false;
	in.nonFatal = true;
	in.summary = "The patch could not enlarge the game's interface drawing buffers (uidraw: " + in.code +
	             "); the game goes on, but many unit flags on screen may crash it.";
	{
		char id[1024];
		ProxyBuildIdText(SelfModule(), id, sizeof(id));
		in.patch = id;
	}
	ProxyReport::Hashed h;
	h.label = "proxy";
	h.path = ModuleFile(SelfModule());
	in.hashes.push_back(h);
	h = ProxyReport::Hashed();
	h.label = "x64 DLL";
	h.path = s.x64Dll;
	in.hashes.push_back(h);
	h = ProxyReport::Hashed();
	h.label = "server.exe (server_exe)";
	h.path = s.serverExe;
	in.hashes.push_back(h);
	std::string cfgIni;
	std::wstring cfgIniW;
	if (ProxySession::ConfigIniPath(&cfgIniW))
	{
		char a[MAX_PATH * 2];
		if (WideCharToMultiByte(CP_ACP, 0, cfgIniW.c_str(), -1, a, sizeof(a), NULL, NULL)) cfgIni = a;
	}
	const std::string runDir = FindRunFolder(s, cfgIni);
	if (!s.logPath.empty()) in.tails.push_back(s.logPath);
	if (!runDir.empty())
	{
		in.runDir = runDir;
		WIN32_FIND_DATAA fd;
		HANDLE fh = FindFirstFileA((runDir + "\\*.log").c_str(), &fd);
		if (fh != INVALID_HANDLE_VALUE)
		{
			do
			{
				const std::string p = runDir + "\\" + fd.cFileName;
				if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && _stricmp(p.c_str(), s.logPath.c_str()) != 0)
					in.tails.push_back(p);
			} while (FindNextFileA(fh, &fd));
			FindClose(fh);
		}
	}
	in.copies.push_back(s.iniPath);
	{
		wchar_t opt[1024];
		if (Vp64Opt::DefaultPath(opt, sizeof(opt) / sizeof(opt[0])) != Vp64Opt::kPathNone) in.copiesW.push_back(opt);
	}
	{
		char line[400];
		ProxyUiDraw::StatusLine(line, sizeof(line));
		in.session.push_back(line);
		in.session.push_back(runDir.empty() ? "run folder: not found (the log tail may be missing)"
		                                    : "run folder: " + runDir);
	}
	if (!cfgIniW.empty())
	{
		if (!ProxyReport::ActivatedMods(cfgIniW, &in.mods, &in.modsWhy)) in.mods.clear();
	}
	else
		in.modsWhy = "the Documents folder could not be found";
	std::wstring root, dir;
	std::vector<std::string> lines;
	if (!ProxyReport::Root(&root))
		lines.push_back("report: FAILED: the Documents folder could not be found; no report");
	else
		ProxyReport::Write(in, root, &dir, &lines);
	for (size_t i = 0; i < lines.size(); ++i) ProxyRtInt::Log("uidraw: %s", lines[i].c_str());
	if (dir.empty()) dir = root;
	std::string helperLine;
	const bool started = ProxySession::StartHelper(ProxySession::Widen(s.proxyDir), L"popup", dir,
	                                               s.iniFound ? ProxySession::Widen(s.iniPath) : std::wstring(),
	                                               &helperLine);
	ProxyRtInt::Log("uidraw: %s%s; the game goes on (non-fatal)", started ? "" : "FAILED: ",
	                ProxyReport::Scrub(helperLine).c_str());
}

void Init()
{
	if (InterlockedCompareExchange(&g_init, 1, 0) != 0) return;
	const ProxyLogic::Settings& s = ProxyIniSettings();
	UiBridge::Hooks h = { PostRecord, LogLine };
	UiBridge::SetHooks(h);
	UiBridge::Configure(s.uiBridge, s.uiBridge && s.uiBridgeTest);
	// Ticket 0487: the UI draw buffers' growth, the bridge's frame hook.
	ProxyUiDraw::Hooks uh = { LogLine, UiDrawReportHook, NULL };
	ProxyUiDraw::SetHooks(uh);
	// One-off 9081 (the owner, in person, 2026-11-22): no longer an option; always on (its own checks still decide whether it
	// can be installed on this EXE build).
	ProxyUiDraw::Configure(1);
	// Ticket 4773: the combat hold's drop runs in the frame hook, after ProxyUiDraw's when that is on. Always set (the
	// drain is installed anyway for ProxyLiveReload below); it returns at its first test while no hold is held.
	ProxyCombatHold::Init(ProxyUiDraw::Enabled() ? ProxyUiDraw::OnFrame : NULL);
	// Ticket 4723 (MC #8210): the sound executor after the hold's drop, one chain in the plain slot (combat-choreography.md
	// 3.5.8 point 1); off until route S arms it, it returns at its first test.
	ProxySoundExec::Init(ProxyCombatHold::FrameHook);
	// Ticket 4727 (lead-0590 #8368): the effect player after the sound executor, the same chain; off until route S arms it.
	ProxyEffectPlayer::Init(ProxySoundExec::FrameHook);
	// Ticket 4783 (MC #8543): route S's common state last in the chain (the parts' steps first, then the server's question,
	// the arming and the answer); idle until the server asks, one test per frame and one section look every 60 frames.
	ProxyRouteS::Init(ProxyEffectPlayer::FrameHook);
	// Ticket 0727: the minimap's unexplored cells after route S's state (FixMinimapUnexplored; off: one test a frame).
	ProxyMinimapFog::Init(ProxyRouteS::FrameHook);
	UiBridge::SetFrameHook(ProxyMinimapFog::FrameHook);
	// Ticket 0586: the EXE's ice and coast waves after a live terrain change (proxy_livereload.h). Always set: it does
	// nothing until server.exe makes its section (live terrain on), and costs one section look every 60 frames until then.
	ProxyLiveReload::Hooks lh = { LogLine };
	ProxyLiveReload::SetHooks(lh);
	UiBridge::SetFrameLuaHook(ProxyLiveReload::OnFrame);
	// TEST ONLY: the live negative test of the loud failure (report + popup); never set by the product or the installer.
	char tf[160];
	const DWORD n = GetEnvironmentVariableA("VP64_UIDRAW_TEST_FAIL", tf, sizeof(tf));
	if (n > 0 && n < sizeof(tf) && ProxyUiDraw::Enabled()) ProxyUiDraw::FailForTest(tf);
	// Issue 4754: TEST ONLY, the positions-only install without the product option (4759).
	char v[8] = "";
	const DWORD np = GetEnvironmentVariableA("VPPROXY_TEST_UI_POSITIONS", v, sizeof(v));
	if (np > 0 && np < sizeof(v))
	{
		const bool on = strcmp(v, "1") == 0;
		ProxyRtInt::Log("LOUD: TEST ONLY: VPPROXY_TEST_UI_POSITIONS=%s: the bridge's unit positions %s (issue 4754; "
		                "never set by the product)", v, on ? "asked for" : "not asked for (only 1 turns them on)");
		if (on) UiBridge::ConfigurePositions(true);
	}
}

// ---- ticket 0300 part 2: the EXE's path tests through the DLL interfaces (path-models.md 7.1) -------------------------
// The same DLL calls the EXE's MovementPath placement makes (embark 0x768E50, disembark 0x5DEA50, attack 0x48B290 ->
// 0x6D8BF0, the cursor's swap test 0x6182B0), written from the knowledge base's description (D83), on the main thread
// inside the bridge's listener, through the context wrapper as proxy_leaderscene.cpp uses it. Every interface object
// taken here is released (Destroy). A test that cannot get its objects answers false, as the EXE's does.
extern "C" ICvGameContext1* DllGetGameContext();  // proxy_rt.cpp: after the boot, the context wrapper

template <class T> void Release(T*& p)
{
	if (p) p->Destroy();
	p = NULL;
}

struct PathEnv
{
	ICvGameContext1* ctx;
	ICvGame1* game;
	ICvMap1* map;
	ICvUnit1* unit;
	TeamTypes activeTeam;
	TeamTypes unitTeam;
};

// The attack test on plot p (0x6D8BF0): visible to the unit's team, then a war-declaring move, an enemy city, or a unit
// of a team at war with the unit's team together with CanMoveInto(p, MOVEFLAG_ATTACK).
bool AttackTest(PathEnv& e, ICvPlot1* p)
{
	if (!p || !p->IsVisible(e.unitTeam, false)) return false;
	if (e.unit->GetDeclareWarMove(p) != NO_TEAM) return true;
	if (p->IsEnemyCity(e.unit)) return true;
	ICvTeam1* team = e.ctx->GetTeam(e.unitTeam);
	if (!team) return false;
	bool hit = false;
	const int n = p->GetNumUnits();
	for (int i = 0; i < n && !hit; ++i)
	{
		ICvUnit1* v = p->GetUnitByIndex(i);
		if (!v) continue;
		ICvPlayer1* owner = e.game->GetPlayer(v->GetOwner());
		if (owner && team->IsAtWar(owner->GetTeam()) && e.unit->CanMoveInto(p, 1)) hit = true;
		Release(owner);
		Release(v);
	}
	Release(team);
	return hit;
}

void PathTests(const UiBridge::PathQuery& q, UiBridge::PathAnswer& a)
{
	PathEnv e;
	memset(&e, 0, sizeof(e));
	e.ctx = DllGetGameContext();
	if (!e.ctx || q.unitOwner < 0) return;
	e.game = e.ctx->GetGame();
	if (!e.game) return;
	const PlayerTypes active = e.game->GetActivePlayer();
	ICvPlayer1* player = NULL;
	if ((PlayerTypes)q.unitOwner == active && (player = e.game->GetPlayer(active)) != NULL)
		e.unit = player->GetUnit(q.unitId);
	Release(player);
	e.map = e.unit ? e.ctx->GetMap() : NULL;
	if (e.unit && e.map)
	{
		a.tested = true;
		e.activeTeam = e.game->GetActiveTeam();
		e.unitTeam = e.unit->GetTeam();
		if (q.count == 0)
		{
			ICvPlot1* c = e.map->GetPlot(q.mouseX, q.mouseY);
			if (c)
			{
				a.cursorSwap = e.unit->CanSwapWithUnitHere(c);
				a.cursorAttack = !a.cursorSwap && AttackTest(e, c);
			}
			Release(c);
		}
		else
		{
			ICvPlot1* prev = e.map->GetPlot(q.x[0], q.y[0]);
			for (int i = 1; i < q.count; ++i)
			{
				ICvPlot1* cur = e.map->GetPlot(q.x[i], q.y[i]);
				const bool last = i == q.count - 1;
				if (last) a.lastAttack = AttackTest(e, cur);
				if (!(last && a.lastAttack) && prev && cur && cur->IsRevealed(e.activeTeam, false))
				{
					const bool prevWater = prev->IsWater();
					a.embark[i] = (uint8_t)(((!prevWater && e.unit->CanEmbarkOnto(prev, cur, true)) ||
					                         (prevWater && e.unit->CanDisembarkOnto(prev, cur, true))) ? 1 : 0);
					// the first 20 land / water crossings are logged with their answer (live diagnosis, ticket 0300)
					static volatile LONG s_crossLogs = 0;
					const bool curWater = cur->IsWater();
					if (curWater != prevWater && InterlockedIncrement(&s_crossLogs) <= 20)
						ProxyRtInt::Log("ui bridge: path test: unit %d/%d, step (%d, %d) -> (%d, %d) %s: %s", q.unitOwner, q.unitId,
						                q.x[i - 1], q.y[i - 1], q.x[i], q.y[i], prevWater ? "water to land" : "land to water",
						                a.embark[i] ? "embark marker" : "no embark marker");
				}
				Release(prev);
				prev = cur;
			}
			Release(prev);
		}
	}
	Release(e.map);
	Release(e.unit);
	Release(e.game);
}

// The main thread and the install check, once the thread bound to the ui channel is known and something is on.
void Arm()
{
	const LONG tid = g_uiThread;
	if (!tid || (!UiBridge::Active() && !UiBridge::PositionsOn() && !UiBridge::ArrowsOn() && !UiBridge::PathOn())) return;   // Active: ui_bridge=1 or 0487's hook
	UiBridge::SetMainThread((uint32_t)tid);
	InterlockedExchange(&ProxyUiBridgeRt::g_on, 1);   // from here every Lua call of a DLL function on the main thread may install
}
}  // namespace

namespace ProxyUiBridgeRt
{
volatile long g_on = 0;

void OnStateRegistered(lua_State* L)
{
	Init();
	uint32_t chan = 0;
	if (ProxyRtInt::ThreadChannel(chan) && chan == vpipc::kChannelUi)
		InterlockedExchange(&g_uiThread, (LONG)GetCurrentThreadId());
	if (!UiBridge::Active() && !UiBridge::PositionsOn() && !UiBridge::ArrowsOn() && !UiBridge::PathOn()) return;
	Arm();
	UiBridge::OnStateRegistered(L);   // the test table only (ui_bridge=1 ui_bridge_test=1)
}

void RequestUnitPositions()
{
	Init();
	if (UiBridge::PositionsOn()) return;
	ProxyRtInt::Log("ui bridge: the unit option asks for the unit positions (issue 4754)");
	UiBridge::ConfigurePositions(true);
	Arm();   // when the ui thread is known already; else at the next registration
}

void RequestArrows()
{
	Init();
	if (UiBridge::ArrowsOn()) return;
	ProxyRtInt::Log("ui bridge: the arc option asks for the arrows' end points (ticket 0199)");
	UiBridge::ConfigureArrows(true);
	Arm();   // when the ui thread is known already; else at the next registration
}

void RequestPath()
{
	Init();
	if (UiBridge::PathOn()) return;
	ProxyRtInt::Log("ui bridge: the path option asks for the move-order path's nodes and the EXE's tests (ticket 0300)");
	UiBridge::SetPathTests(PathTests);
	UiBridge::ConfigurePath(true);
	Arm();   // when the ui thread is known already; else at the next registration
}

void OnLuaEntrySlow(lua_State* L)
{
	UiBridge::OnLuaEntry(L);
}

void OnUpdate()
{
	if (!g_init || (!UiBridge::Active() && !UiBridge::PositionsOn() && !UiBridge::ArrowsOn() && !UiBridge::PathOn())) return;
	UiBridge::OnUpdate();
	if (g_uidrawReport == 1 && InterlockedCompareExchange(&g_uidrawReport, 2, 1) == 1) WriteUiDrawReport();
	if ((++g_updates % 3600) == 0)
	{
		char line[1024];
		UiBridge::FormatStats(line, sizeof(line));
		ProxyRtInt::Log("%s; post failures %ld (after %u Updates)", line, g_postFailures, g_updates);
		if (ProxyUiDraw::Enabled())
		{
			char ud[400];
			ProxyUiDraw::StatusLine(ud, sizeof(ud));
			ProxyRtInt::Log("%s", ud);
		}
		if (ProxyMinimapFog::Test::Noted() > 0)   // ticket 0727: once a plot was set back to unexplored
		{
			char mf[600];
			ProxyMinimapFog::CountersLine(mf, sizeof(mf));
			ProxyRtInt::Log("%s", mf);
		}
		const ProxyLiveReload::Stats ls = ProxyLiveReload::GetStats();   // ticket 0586: once it has work
		if (ls.opens || ls.requests)
		{
			char lr[400];
			ProxyLiveReload::StatusLine(lr, sizeof(lr));
			ProxyRtInt::Log("%s", lr);
		}
	}
}
}  // namespace ProxyUiBridgeRt
