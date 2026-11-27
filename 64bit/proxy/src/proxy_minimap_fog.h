// proxy_minimap_fog.h - the EXE minimap's unexplored cells after a plot is set back to unexplored (ticket 0727, route R1
// of ticket 0552). Hand-written. SPDX-License-Identifier: GPL-3.0-or-later
//
// The stock bug (research/05-graphics-composition/observer-switch-minimap.md 6.1, 6.2): the EXE's Minimap system ignores a
// fog change to "unexplored", so a plot that a mod or script un-reveals keeps its minimap cell, its edges and its share of
// the minimap's box (its scale) until the next load.
//
// What it does (written from the knowledge base's description of the EXE's behaviour, D83: observer-switch-minimap.md
// 6.2 and 6.6):
//   - the note: right after the EXE served a carried ICvEngineUtility{1..4}::GameplayFOWChanged (proxy_rt.cpp OnCall, on
//     the thread that served it), the plot is remembered when the DLL's mode is FOGOFWARMODE_UNEXPLORED and forgotten when
//     it is any other mode (the served order is the EXE's own queue order, so a later reveal of the same plot wins);
//   - the apply: on the EXE's main thread, from the Lua bridge's frame hook (proxy_ui_bridge_rt.cpp Init), each remembered
//     plot is applied at kApplyFrames frames in a row (the EXE may run its own handler for an earlier reveal of that plot
//     after the first of them: whichever order the EXE's queue and the frame hook run in, the cell ends unexplored). An
//     apply sets the active block's cell bit 0 (unexplored), recomputes the edge bits between the cell and its six
//     neighbours as the EXE's handler does, and when a cell changed, recomputes the box of explored plots from all cells
//     and sets the system's redraw bits (+0x1C bits 0 and 2), as the handler does when it widens the box;
//   - nothing else: no EXE function is called and no code byte is read or written (D228 base rule); the four .data items
//     come from the build table (exebuild/symbols/proxy.tsv: mmf_slot, mmf_blocks, mmf_system, mmf_wrap; ProxyExe::Addr);
//     the offsets below are layout.
//
// When it is on: [Extras] FixMinimapUnexplored (a bugfix, default 1, also needs EnableBugfixes = 1; D169) on a build whose
// layout is verified (kLayoutVerified in the .cpp: 4390913 and 26577625). Off: nothing is read or written; the note
// returns at its first test.
//
// Fail loud (D118, the ticket): an unknown build, a build without a verified layout, a symbol the build table does not
// carry, unreadable .data, or a failed check of the block at an apply (its size, wrap byte, transform (it must take the
// cube coordinates (x, y, -x-y) to (x, y)), the list walk) turns
// the fix off for the rest of this proxy load with one LOUD line. No report and no popup: the minimap then behaves as in
// stock, which is the whole cost of the failure.
//
// Parts: the core (the note, the table of plots, the apply, the box, the checks) works on a Target of plain addresses and
// is tested on its own (proxy/tests/minimap_fog/); the binding (TargetOf) is the only part that reads the build table
// (proxy_exe.h); the glue (AfterServed, Init) is the only part that needs the proxy runtime.
//
// Toolchain: clang-cl -m32 with VC9 headers (the proxy's), C++03; offline test with VP_MINIMAP_FOG_OFFLINE defined (no
// proxy runtime; with VP_MINIMAP_FOG_BINDING also the binding, against the build table).
#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "vp_exe_build.h"  // the builds' identities (no address)

namespace ProxyMinimapFog
{
// ---- the layout (observer-switch-minimap.md 6.2 and 6.6 [C]; verified on 4390913 and 26577625) ----------------------
// The block list: mmf_blocks holds the pointer to the sentinel node; node = {+0 next, +4 prev, +8 slot}, block at +0xC.
const DWORD kNodeOffNext = 0x0;
const DWORD kNodeOffSlot = 0x8;
const DWORD kNodeOffBlock = 0xC;
const int kMaxNodes = 80;            // 65 slots at most (0..64); the walk gives up after this many nodes
const int kMaxSlot = 64;             // the active-player handler keeps the slot in 0..64
// A block: a 4 x 4 int transform at +0 (hex to (q, r)), width +0x94, height +0x98, wrap byte +0x9C, cells from +0xA0.
const DWORD kBlkOffTransform = 0x0;
const DWORD kBlkOffWidth = 0x94;
const DWORD kBlkOffHeight = 0x98;
const DWORD kBlkOffWrap = 0x9C;
const DWORD kBlkOffCells = 0xA0;     // 4 bytes a cell, index = row * 256 + column; byte 2 of a cell: bit 0 unexplored,
const int kCellsPerRow = 256;        // bits 2..7 "my neighbour in direction d differs" (d = 0..5, bit d + 2)
const int kMaxRows = 256;
const DWORD kCellOffFlags = 2;
const DWORD kBlkOffSpare = 0x400A0;  // the one cell an out-of-range lookup gives
const DWORD kBlkOffFirst = 0x400B0;  // the box of explored plots: first plot (column, row), low corner, high corner
const DWORD kBlkOffLow = 0x400B8;
const DWORD kBlkOffHigh = 0x400C0;
const DWORD kBlkOffNone = 0x400C8;   // byte: 1 = no explored plot yet (the next explored plot starts the box)
const DWORD kBlkSize = 0x400CC;
// The system object (mmf_system): +0x18 bit 0 = its handlers defer their events; +0x1C = the redraw bits the render reads.
const DWORD kSysOffDefer = 0x18;
const DWORD kSysOffRedraw = 0x1C;
const DWORD kRedrawCells = 0x1;      // the handler sets it after every fog event
const DWORD kRedrawBox = 0x4;        // ... and this one when the box changed (the view step runs again)
// The handler's six neighbour directions, in cube coordinates (dx, dy, dz), in the order of the edge bits 2..7; the
// neighbour in direction d keeps its bit for the opposite direction (d + 3) % 6.
const int kDirections[6][3] = {{0, 1, -1}, {-1, 0, 1}, {1, -1, 0}, {0, -1, 1}, {1, 0, -1}, {-1, 1, 0}};
// The DLL's FogOfWarModeTypes (CvEnums.h): 0 off (visible), 1 unexplored, 2 no visibility (seen).
const int kDllModeUnexplored = 1;
// Frames each noted plot is applied at (see the apply above). At about 60 frames a second, 8 frames are about 130 ms.
const int kApplyFrames = 8;

enum State
{
	kOff = 0,      // the option is off, or not armed yet: nothing is read or written
	kActive = 1,   // armed, the build and its layout checked: plots are noted and applied
	kRefused = 2   // a check failed: nothing more in this proxy load
};

// Sink: one complete line without a newline; loud = a LOUD line.
typedef void (*LogFn)(const char* line, bool loud);
void SetSinks(LogFn log);

// ---- arming ---------------------------------------------------------------------------------------------------------
// What the fix needs of the running EXE, resolved by the binding (TargetOf, through the build table) or given by a test.
struct Target
{
	int build;               // kVpExeBuild* of vp_exe_build.h, or kVpExeBuildUnknown
	uint32_t stamp;          // the image header's TimeDateStamp and SizeOfImage (named in the unknown build's line)
	uint32_t sizeOfImage;
	ULONG_PTR slot;          // mmf_slot in this process; 0 = not carried
	ULONG_PTR blocks;        // mmf_blocks
	ULONG_PTR system;        // mmf_system
	ULONG_PTR wrap;          // mmf_wrap
	const char* notCarried;  // the binding's line when a symbol is not carried (NULL or "" = none)
};
// The option on: the target is checked (a known build, its layout verified, the four symbols carried, their memory
// readable). False when the fix cannot work here (one LOUD line). Any thread.
bool Arm(const Target& t);
// Off: nothing more is noted or applied (plots noted are forgotten). Any thread.
void Disarm();
// The builds whose layout is verified (kLayoutVerified in the .cpp): the evidence, or NULL.
const char* LayoutVerified(int build);
#if !defined(VP_MINIMAP_FOG_OFFLINE) || defined(VP_MINIMAP_FOG_BINDING)
// ---- the binding (needs the build table: proxy_exe.h) ----------------------------------------------------------------
Target TargetOf(ULONG_PTR base);
// The product's switch: on = Arm(TargetOf(the EXE's image)), off = Disarm(). The answer of Arm (true when off).
bool SetEnabled(bool on);
#endif
State CurrentState();

// ---- the note, the apply ----------------------------------------------------------------------------------------------
// After the EXE served GameplayFOWChanged(x, y, mode, setAll) (any thread). setAll: every plot of the map.
void OnFowChanged(int x, int y, int dllMode, bool setAll);
// The EXE's main thread, once per frame: applies the noted plots.
void OnFrame();
// The frame hook the bridge calls: the chained hook first, then OnFrame.
void FrameHook();
void Chain(void (*existing)());

// "minimap fog: state active, noted N (out of range N), pending N, frames N (deferred N, no block N), applied N (cells
// marked N, already unexplored N, outside the map N), boxes N (empty N)"
void CountersLine(char* out, size_t cap);

// ---- the product glue (proxy_minimap_fog.cpp, not in the offline test) ------------------------------------------------
#ifndef VP_MINIMAP_FOG_OFFLINE
// proxy_rt.cpp OnCall, after EngDispatchGenerated served `method` (handled, not failed); args = the CALL payload (the
// self handle, then the in-parameters).
void AfterServed(uint32_t method, const uint8_t* args, uint32_t argsSize);
// proxy_ui_bridge_rt.cpp Init: the sink, the option, the chain. Once per proxy load.
void Init(void (*existingFrameHook)());
#endif

namespace Test
{
void Reset();               // forgets everything (state, plots, counters, chain; the sink stays)
long Noted();
long Pending();             // plots noted and not yet applied kApplyFrames times
long Marked();              // cells set to unexplored by an apply
long AlreadyUnexplored();   // applies to a cell that was unexplored already
long OutsideMap();
long Deferred();            // frames skipped because the system defers its events
long NoBlock();             // frames without a block for the active slot
long Boxes();               // box recomputes
long EmptyBoxes();          // ... that found no explored plot
long Frames();
}  // namespace Test
}  // namespace ProxyMinimapFog
