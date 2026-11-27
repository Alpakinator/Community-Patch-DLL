// proxy_livereload.h - the EXE's ice and coast waves rebuilt after a live terrain change (epic 0465, ticket 0586).
// Written 2026-11-19 for this repository (GPL-3.0-or-later). Hand-written. Layout of the section and the protocol:
// proxy/include/vp_livereload.h. Findings: research/05-graphics-composition/live-terrain.md section 10.
//
// Where it runs: ProxyLiveReload::OnFrame is the Lua bridge's frame Lua hook (proxy_ui_bridge.h SetFrameLuaHook): on the
// EXE's main thread, from the bridge's listener of the InGame context's Events.LocalMachineAppUpdate, once per in-game
// frame, with that listener's Lua state. Per frame it costs one read of the section's two sequence words; the section is
// looked for every kOpenEveryFrames frames until server.exe has made it.
//
// For a request (reqSeq != ackSeq) it:
//   1. checks the EXE once per process: the build from the registry (PE stamp and SizeOfImage, vp_exe_build.h), the
//      symbols ice_reload, ice_ice and cam_grid carried to it (the build table, exebuild/symbols/shell.tsv; ticket 0619),
//      and Ice_Reload's code at ice_reload (push of the rebased cam_grid, mov ecx of the rebased ice_ice, a call): read
//      only. Any check failing refuses every request of the process, fail-closed (kRefusedExe / kRefusedPattern; D200:
//      no work for a build the patch does not know);
//   2. ice (kReqIce): the grid's W and H (Ice + 0x94 / + 0x98) must be the request's map; the grid range must be committed
//      PAGE_READWRITE memory inside the EXE's image; the plots whose bit 0x80 differs from the request's bitmap must
//      number exactly expectFlips (else kGridMismatch: nothing written). Then bit 0x80 of each such byte is set or
//      cleared (the other bits kept; a data write, D4) and Ice_Reload() is called;
//   3. waves (kReqWaves): Waves_Reload() is called;
//   4. the answer is written and ackSeq set.
// The calls are by name, in protected mode, from the InGame context's environment (then the thread's globals): the EXE's
// own Lua globals (live-terrain.md 4.2, 4.8), never by address. A global that is not a function is kNoFunction. Every
// failure is one "LOUD: livereload:" line (D118: the server then keeps drawing the ice and waves it has, which match the
// field it draws until the reload; the request is answered so the server never waits forever).
#pragma once
#include <stddef.h>
#include <stdint.h>

struct lua_State;

namespace ProxyLiveReload
{
struct Hooks
{
	void (*log)(const char* line);   // one line, no newline; NULL = no log
};
void SetHooks(const Hooks& h);

// The drain's frame Lua hook (main thread).
void OnFrame(lua_State* L);
enum { kOpenEveryFrames = 60 };

struct Stats
{
	uint32_t frames, opens, requests;
	uint32_t iceOk, wavesOk, failures;
	uint32_t bitsSet, bitsCleared;
	int32_t lastIce, lastWaves;
	uint32_t iceMicrosMax, wavesMicrosMax;
};
Stats GetStats();
void StatusLine(char* out, size_t cap);

// ---- tests only ----
// A stand-in EXE image (PE header, the code site, the ice object at their offsets from `base`); NULL = the process's
// own EXE (GetModuleHandle(NULL)). The section is the real name with this process's id.
void SetImageForTest(uint8_t* base, size_t size);
void ResetForTest();
}  // namespace ProxyLiveReload
