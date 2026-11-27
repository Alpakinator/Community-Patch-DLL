// proxy_uidraw.h - the growth of the EXE's per-frame UI draw-list buffers on the product route (ticket 0487; the effect
// of upstream LoneGazebo/Community-Patch-DLL pull request #13406). Hand-written. Design and findings:
// research/03-dll-engine-boundary/upstream-13406-product.md.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The EXE's UI manager (g_pUIManager) holds up to three command streams (+0x6F0 the one in use, +0x6F4 / +0x6F8 left
// and right, distinct only with stereo 3D). Each DX11 stream has two halves, swapped every frame: 1 MB of data and
// 4,096 32-byte commands per half, written with no bound check. About 440-450 unit flags in view overflow them and the
// EXE crashes (research/02-exe-internals/message-queue-and-fixed-buffers.md section 2). This file replaces both halves
// of every stream with blocks four times larger (4 MB, 16,384 commands), allocated and freed with the EXE's own
// allocators, by writing the stream objects' pointer and size fields: no EXE code byte, no import slot (D4).
//
// Where it runs: ProxyUiDraw::OnFrame is the Lua bridge's frame hook (proxy_ui_bridge.h SetFrameHook): it is called on
// the EXE's main thread from the bridge's C listener of the InGame context's Events.LocalMachineAppUpdate, once per
// shown in-game frame, in the same phase as upstream's Lua trigger (the Lua update pass, before that frame's UI draw).
//
// Decisions (MC, 2026-11-17 14:45, tracker 0487): (1) the four EXE allocator functions are called by address (D136,
// D144), only after the known-EXE check and every read-only cross-check below pass; (2) every failure is non-fatal and
// loud (D118, D125): one "uidraw: FAILED: <why>" line, the D119 report with the non-fatal stage "uidraw" and the
// helper's popup once per session (the glue does both, proxy_ui_bridge_rt.cpp), then the game goes on with the stock
// buffers; a growth still pending after kPendingFrames frames is a failure; (3) VP64Options.ini [Renderer]
// UiDrawListGrowth, default 1 (a crash fix, not an Extras flag); 0 = the stock buffers. Since one-off 9081 (the owner, in
// person, 2026-11-22) no longer an option: the proxy always configures 1; Configure(0) remains for the tests.
//
// The checks before any write or call (each failure turns the growth off for the process):
//   - the PE time stamp of the DX11 Steam EXE (0x546CD0A8). Another build: one line, off, NOT loud (the product needs
//     that build; ticket 0492's precedent);
//   - seven code patterns (the stream start-up site, the read of g_pUIManager, the stream's constructor, destructor and
//     ClearStream, the two allocators) and 22 cross-checks of the offsets and call targets read out of those functions,
//     all from the pull request (VirtualQuery before every read): loud on a mismatch;
//   - per stream, before the growth starts: each half pair (data, commands) is one block of two adjacent halves at the
//     stock sizes: loud on a mismatch.
// The growth (#13406's DX11 two-step swap): one 8 MB data block (the EXE's aligned allocator, 16-aligned) and one 1 MB
// command block (the EXE's operator new[]); when the current half is empty (data used, command count and set count all
// 0) it is pointed at the matching new half; the EXE swaps the halves each frame, so the other half follows a frame
// later; then the sizes are written (4 MB, 16,384) and the old blocks freed (the lower pointer of each old pair, with
// the matching EXE free). A stream found at the stock sizes again after a growth (a renderer restart re-creates the
// streams) is grown again, at most kMaxRegrowths times.
// Usage: the high-water of the last frame's data use (+0x2074), logged at each 10 % step of the data
// capacity, and in the status line.
#pragma once
#include <stddef.h>

namespace ProxyUiDraw
{
enum
{
	kPendingFrames = 600,    // frames a growth may stay pending (the current half never seen empty) before it fails
	kMaxRegrowths = 4,       // streams found back at the stock sizes and grown again, per process, before it fails
	kGrowth = 4,             // the factor (#13406)
	kStockDataSize = 0x100000,
	kStockCommandCap = 4096,
	kCommandSize = 32
};

// The hooks the glue gives (the unit test gives its own). log: one line without newline (NULL: none). report: called
// once per process on the first failure, with a short code ("exe-check", "layout", "allocation", "pending",
// "regrowth") and one line of detail; the glue writes the D119 report and starts the helper's popup, not on the main
// thread. now: milliseconds (NULL: GetTickCount).
struct Hooks
{
	void (*log)(const char* line);
	void (*report)(const char* code, const char* detail);
	unsigned long (*now)();
};
void SetHooks(const Hooks& h);

// 1 = on, 0 = off (the stock buffers; logged once). Called once by the glue, always with 1 since one-off 9081 (the owner,
// 2026-11-22: [Renderer] UiDrawListGrowth is no longer an option); 0 only in the tests.
void Configure(int option);
bool Enabled();

// The bridge's frame hook (the EXE's main thread, once per in-game frame). Cheap once grown: three pointer reads and
// two compares per stream.
void OnFrame();

// One line for the log and the reports: "uidraw: <state>; streams N grown ...; data high-water ...".
void StatusLine(char* out, size_t cap);

// Ticket 0765 (Vox Populi 5.5 ships #13406's Lua: InGame.lua calls Exe.TryGrowUICommandStream() each frame until it
// returns ok or a refusal other than "pending"): the answers of the proxy's own Exe table (proxy_ui_bridge_rt.cpp) for
// the UI draw list come from this growth's state, so VP's Lua and the frame hook agree and no stream is grown twice
// (the Lua call grows nothing; the frame hook does). Each returns NULL for "ok", else upstream's refusal name
// (LuaCATS/Exe.d.lua): "bin_hooks_off" (Configure(0): the tests only), "unsupported_exe" (another EXE build, or the
// EXE checks failed), "unavailable" (a failure after the checks, or the stream is not readable now), and for LuaTryGrow
// "pending" (not grown yet: before the first frame hook, waiting for the streams, or between the two swap steps).
// LuaUsage gives the in-use stream's last-frame data use and data capacity (upstream's TryGetUICommandStreamUsage).
// The EXE's main thread (the caller checks it).
const char* LuaCanGrow();
const char* LuaTryGrow();
const char* LuaUsage(unsigned long* used, unsigned long* capacity);

// TEST ONLY (the live negative test of the loud failure): the next frame after the EXE checks fails with code "test"
// and this reason, through the same path as a real failure (FAILED line, report hook). The glue calls it only when the
// environment variable VP64_UIDRAW_TEST_FAIL is set (never set by the product or the installer).
void FailForTest(const char* why);

// ---- for the offline tests (proxy/tools/rt_tests/uidraw_unit.cpp); the product calls none of these ----------------
namespace Test
{
// The EXE's allocators as the pull request calls them: cdecl, (size, alignment, file, line, heap, tag); the frees
// cdecl (pointer). Stand-ins in the tests.
typedef void*(__cdecl* AllocFn)(unsigned int size, unsigned int align, const char* file, int line, int heap, int tag);
typedef void(__cdecl* FreeFn)(void* p);
struct Allocators
{
	AllocFn newArray;        // operator new[] (the command blocks)
	AllocFn mallocAligned;   // FireMallocAlignedNoTracking (the data blocks)
	FreeFn deleteArray;      // operator delete[]
	FreeFn freeAligned;      // FireFreeAlignedNoTracking
};
// Runs the checks against the image at `base` (a fake image in the tests) and, when they pass, uses `alloc` in place
// of the EXE's functions. NULL = ready, else the reason (the same text as the log line).
const char* InstallAt(const void* base, const Allocators& alloc);
// Forgets everything (state, counters, growths in flight; nothing is freed) and turns the growth on.
void Reset();

// The layout the checks read, so that the tests build their fake image from the same numbers.
struct Pattern
{
	const char* name;
	unsigned long va;        // DX11 VA at ImageBase 0x400000
	const char* bytes;       // "8B 44 ?? ..." ("??" = any byte)
};
struct CrossCheck
{
	int kind;                // 0 abs32 (the address of `target`), 1 rel32 (a call/jmp to `target`), 2 val8, 3 val32
	unsigned long va;        // where the operand (abs32, val8, val32) or the call instruction (rel32) is
	unsigned long target;    // abs32 / rel32: a VA; val8 / val32: the value
};
// Ticket 4963: the layout is a build's, from the build table. UseBuild(build) resolves it for that build (false when
// its table lacks a symbol); an install against an image resolves it for the image's own build. PatternAt,
// CrossCheckAt, Timestamp, SizeOfImage and VaUiManager are the resolved build's.
bool UseBuild(int build);
size_t PatternCount();
const Pattern& PatternAt(size_t i);
size_t CrossCheckCount();
const CrossCheck& CrossCheckAt(size_t i);
unsigned long Timestamp();
unsigned long SizeOfImage();
unsigned long ImageBase();
unsigned long VaUiManager();   // g_pUIManager
// Makes the table report the catalogue symbol `name` as not carried (NULL: none); proxy_exe.h's test hook.
void DropSymbol(const char* name);
unsigned long OffStream(int which);  // 0 = +0x6F0 (in use), 1 = left, 2 = right
// The stream's field offsets.
enum Field
{
	kSetCount = 0, kCommandCap, kCommandCount, kCommands, kData, kDataSize, kDataUsed, kDataOther, kCommandsOther,
	kDataUsedOther, kFieldCount
};
unsigned long Off(Field f);
unsigned long StreamBytes();   // the bytes of a stream object the growth reads and writes

// State.
enum State { kOff = 0, kWaiting, kGrowing, kGrown, kFailed };
State GetState();
const char* Why();             // the failure or "off" reason ("" when none)
unsigned long Grown();         // growths finished (all streams, regrowths included)
unsigned long Regrowths();
unsigned long PendingFrames(); // frames the current growth has been pending
unsigned long Reports();       // calls of the report hook
unsigned long DataHighWater();
}  // namespace Test
}  // namespace ProxyUiDraw
