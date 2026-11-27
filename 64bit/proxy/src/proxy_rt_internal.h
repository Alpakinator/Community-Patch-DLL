// proxy_rt_internal.h - shared internals of the x86 proxy runtime (epic 2300, task 2302). Hand-written.
// Not seen by generated code (that includes proxy_rt.h only). Toolchain: clang-cl -m32, VC9 headers, msvcr90,
// C++11 language at most, no <atomic>/<thread>/<mutex> (Win32 primitives instead).
#pragma once
#include "proxy_rt.h"
#include "vp_ids.gen.h"
#include <string>

class ICvEngineUtility1;
class ICvEngineScriptSystem1;
namespace Vp64Opt
{
struct Options;
}

namespace ProxyRtInt
{
// ---- log (vpproxy.log) ----------------------------------------------------------------------------------------
void Log(const char* fmt, ...);
void LogFlush();
// Ticket 0691 (D242): the composite reports the server HUNG (no frame for lockstep_hung_ms): the failure route of a server
// that failed in a game (report + helper once, a D33 session ends). Any thread; reached through ProxyShell::g_serverHung.
void ServerHung(const char* reason);
// Seconds since the proxy's clock started (boot).
double Now();

// ---- state ------------------------------------------------------------------------------------------------------
bool Booted();
// The engine utility the engine passed to SetDLLIFace (NULL before / after).
void SetEngineUtility(ICvEngineUtility1* p);
ICvEngineUtility1* EngineUtility();  // issue 2502: what SetEngineUtility stored (for VpProxy_EngineUtility, 2505)
// The engine's script system, from the engine utility's GetScriptSystem(); NULL when unknown.
ICvEngineScriptSystem1* EngineScriptSystem();
// vpproxy.ini db= ("" when not set).
const char* DbPath();
// After SetDLLIFace(NULL) was forwarded: kRt_Shutdown, wait for the server, close, write the counters.
void Shutdown();
// Index of the channel the calling thread is bound to (0 ui, 1 core, 2 aux). A thread that has made no call yet
// answers aux (its first call decides: only GetGame or Update can make it core, issue 2502).
uint32_t CurrentChannelIndex();

// ---- issue 2502 -------------------------------------------------------------------------------------------------
// Appends one block to vpproxy-snapshots.tsv beside the log (frames, frame times, per-interface calls by direction and
// channel since the previous snapshot), labelled. Any thread; also triggered by the named event
// Local\VPProxySnapshot_<pid>. No-op with counters=0 or before the boot.
void Snapshot(const char* label);
// Called by ProxyRt::Begin on the calling (game-core) thread at the top of each ICvGame1::Update, before the Update is
// forwarded (proxy_update.cpp: polls the harness Lua channel, ProxyLuaExec::Poll, issue 2505).
void BeforeUpdate();
// Called by ProxyRt::End on the game-core thread after the outermost ICvGame1::Update has returned and its epoch has
// ended (every return path, not while an exception unwinds): D165 option 2's write (ticket 4665, proxy_pacing.h);
// nothing when the option is off.
void AfterUpdate();

// ---- served runtime methods (kRtE_*), proxy_lua.cpp ---------------------------------------------------------------
// Handles a kRtE_* method; false = unknown method.
bool LuaServe(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);

// ---- issue 2700 (local answers, proxy_local.cpp) ---------------------------------------------------------------
// Counts a call the proxy answered without a crossing: per method, traffic direction "local", the frame marker.
void NoteLocal(uint32_t method);
// The calling thread's channel; false while the thread has made no call (its first call decides).
bool ThreadChannel(uint32_t& chan);
// Logs to the log and stderr.
void LogErrPublic(const char* fmt, ...);
// A local-answer check failed: under strict=1 the process ends with exitCode (after the log), else nothing.
void StrictViolation(int exitCode, const char* why);

// ---- counters ---------------------------------------------------------------------------------------------------
// A value the Lua codec could not carry (logged once per type name, counted).
void CountUnsupported(const char* luaType);

// ---- payload copies ---------------------------------------------------------------------------------------------
// Copies what is left in src into a malloc'd buffer and points dst at it; the caller frees the result with free().
// Used before decoding anything that can make a nested call (which invalidates the channel payload).
uint8_t* CopyRest(const vpipc::Reader& src, vpipc::Reader& dst);

// ---- one-off 9042: the options file (proxy_options.cpp, options/include/vp64_options.h) ---------------------------
// At a mode=on boot, after the log is open: finds VP64Options.ini (VP64_OPTIONS, else the Documents folder), creates it
// with the commented defaults when missing, reads it, logs warnings and values. Returns its path in UTF-8 for the
// server.cfg line options_file= ("" when there is no path). Ticket 0624: in a D33 session (d33, sessionWhy from
// ProxySession::SessionKind) a missing file is created with the release's option set <x64Dir>\release-options.txt and
// ServerRenderer = 1, or not at all when that set is missing or unusable (proxy_options.h).
std::string OptionsBoot(const std::string& x64Dir, bool d33, const std::string& sessionWhy);
// Ticket 0624, the renderer guard (its one call: Boot in proxy_rt.cpp, right after OptionsBoot): "" = the boot may go
// on; else why a D33 session with ServerRenderer = 0 must not start (a degraded world). The test hooks
// server_config= (writesServerCfg false) and crashlogs\shell.helper skip it with one LOUD line.
std::string OptionsGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg);
// Ticket 0624 item 5 (MC #8762), the composite guard (its one call: Boot, right after ProxyShell::Configure): "" = go
// on; else why a D33 session with ServerRenderer = 1 and a refused product composite (refused = ProxyShell::
// RefusedComposite()) must not start. The same test hooks skip it with one LOUD line.
std::string CompositeGuard(bool d33, const std::string& sessionWhy, bool writesServerCfg, const char* refused);
// The values in force in the proxy (the defaults before OptionsBoot).
const Vp64Opt::Options& ProxyOptions();
}  // namespace ProxyRtInt

// ---- ticket 4662 (D165 option 3, proxy_pacing.h): for DetachOnUnload, which does not include proxy_pacing.h ------
namespace ProxyPacing
{
bool OnUnload(char* line, size_t cap);
}
// ---- ticket 4783 (route S, proxy_route_s.h): the EXE's Combat bit back as found, at Shutdown and DetachOnUnload -------
namespace ProxyRouteS
{
bool OnUnload(char* line, size_t cap);
}
