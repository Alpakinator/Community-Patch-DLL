// proxy_minimap_fog.cpp - the EXE minimap's unexplored cells after a plot is set back to unexplored (ticket 0727). See
// proxy_minimap_fog.h. SPDX-License-Identifier: GPL-3.0-or-later
//
// Written from the knowledge base's description of the EXE's behaviour (D83): research/05-graphics-composition/
// observer-switch-minimap.md 6.2 (the Minimap system's data) and 6.6 (the fog handler's rules, both builds). No EXE
// function is called and no EXE code byte is read, compared or written (D228 base rule). Read: the active slot, the block
// list and the active block (heap), the system's defer bit, the wrap byte (.data). Written: cells' flag bytes, the box and
// its "no plot yet" byte in the active block (heap), the system's redraw bits (.data, by compare-exchange).
// Toolchain: clang-cl -m32, VC9 headers (the proxy's); offline test proxy/tests/minimap_fog/ (the same and MSVC
// /fsanitize=address, x86) with VP_MINIMAP_FOG_OFFLINE defined (no proxy runtime glue).
#ifndef VP_MINIMAP_FOG_OFFLINE
#include "proxy_rt_internal.h"  // first: the fork's headers set up <windows.h> their way
#include "vp64_options.h"
#endif
#include "proxy_minimap_fog.h"
#if !defined(VP_MINIMAP_FOG_OFFLINE) || defined(VP_MINIMAP_FOG_BINDING)
#include "proxy_exe.h"  // the build table (mmf_slot, mmf_blocks, mmf_system, mmf_wrap); the binding only
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace ProxyMinimapFog
{
namespace
{
// ---- the verified layouts ------------------------------------------------------------------------------------------
// The builds on which the layout of proxy_minimap_fog.h (the list's nodes, the block's fields and cells, the box, the
// system's two words, the directions) was verified. A build is added only with its own verification written into
// observer-switch-minimap.md 6.6, never by analogy.
struct LayoutRow
{
	int build;
	const char* evidence;
};
const LayoutRow kLayoutVerified[] = {
	{kVpExeBuild4390913, "ticket 0552 decompile 2026-11-24 [C], observer-switch-minimap.md 6.2 and 6.6"},
	{kVpExeBuild26577625, "ticket 0727 decompile of the fog handler and byte compare of the cell, block, active-player, "
	                      "view and render functions 2026-11-25 [C], observer-switch-minimap.md 6.6"},
};

enum { kCells = kCellsPerRow * kMaxRows };

CRITICAL_SECTION g_cs;  // the table of noted plots (never held while the EXE's memory is written)
volatile LONG g_csInit = 0;
volatile LONG g_state = kOff;
LogFn g_log = NULL;
ULONG_PTR g_slot = 0, g_blocks = 0, g_system = 0, g_wrap = 0;
int g_build = kVpExeBuildUnknown;
unsigned char g_pending[kCells];  // frames left to apply each noted plot (0 = not noted); under g_cs
volatile LONG g_pendingCount = 0;
unsigned short g_work[kCells];    // the main thread's list of this frame's plots (OnFrame only)
volatile LONG g_noted = 0, g_outOfRange = 0, g_forgotten = 0, g_frames = 0, g_deferred = 0, g_noBlock = 0, g_applied = 0,
              g_marked = 0, g_already = 0, g_outside = 0, g_boxes = 0, g_emptyBoxes = 0, g_logged = 0;
void (*g_chained)() = NULL;
const LONG kMaxChangeLines = 40;  // log lines for applies that changed cells (then the counters line only)

void EnsureInit()
{
	if (g_csInit == 2) return;
	if (InterlockedCompareExchange(&g_csInit, 1, 0) == 0)
	{
		InitializeCriticalSection(&g_cs);
		InterlockedExchange(&g_csInit, 2);
		return;
	}
	while (g_csInit != 2) Sleep(0);
}

void Log(bool loud, const char* fmt, ...)
{
	char line[1200];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
	va_end(ap);
	if (g_log) g_log(line, loud);
}

void ClearPending()
{
	EnsureInit();
	EnterCriticalSection(&g_cs);
	memset(g_pending, 0, sizeof(g_pending));
	g_pendingCount = 0;
	LeaveCriticalSection(&g_cs);
}

// A failed check: nothing more in this proxy load; one LOUD line. Always false.
bool Refuse(const char* check, const char* fmt, ...)
{
	char why[700];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(why, sizeof(why), _TRUNCATE, fmt, ap);
	va_end(ap);
	InterlockedExchange(&g_state, kRefused);
	ClearPending();
	char counters[600];
	CountersLine(counters, sizeof(counters));
	Log(true, "LOUD: minimap fog: OFF, check %s failed: %s; the minimap keeps re-hidden plots until a load, as in the stock "
	          "game (FixMinimapUnexplored has no effect in this proxy load); %s",
	    check, why, counters);
	return false;
}

// True if [addr, addr + n) lies in committed memory with all of `want` (PAGE_* readable or writable masks).
bool MemoryIs(ULONG_PTR addr, size_t n, DWORD want)
{
	if (addr < 0x10000 || addr + n < addr) return false;
	ULONG_PTR at = addr;
	while (at < addr + n)
	{
		MEMORY_BASIC_INFORMATION mi;
		if (VirtualQuery(reinterpret_cast<const void*>(at), &mi, sizeof(mi)) != sizeof(mi)) return false;
		if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
		if ((mi.Protect & want) == 0) return false;
		const ULONG_PTR end = reinterpret_cast<ULONG_PTR>(mi.BaseAddress) + mi.RegionSize;
		if (end <= at) return false;
		at = end;
	}
	return true;
}
const DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY;
const DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
bool Readable(ULONG_PTR addr, size_t n) { return MemoryIs(addr, n, kReadable); }
bool Writable(ULONG_PTR addr, size_t n) { return MemoryIs(addr, n, kWritable); }

// ---- guarded reads: VirtualQuery first (clang-cl's __try does not catch a fault raised in its own body, only in a callee:
// the test's unreadable page crashed the clang build, 2026-11-25), then the read under SEH (MSVC; leaf functions without
// stack objects for MSVC ASan, as proxy_combat_hold.cpp) --------------------------------------------------------------------
bool ReadDword(ULONG_PTR at, DWORD* out)
{
	if (!Readable(at, sizeof(DWORD))) return false;
	__try
	{
		*out = *reinterpret_cast<const volatile DWORD*>(at);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

bool ReadByte(ULONG_PTR at, BYTE* out)
{
	if (!Readable(at, 1)) return false;
	__try
	{
		*out = *reinterpret_cast<const volatile BYTE*>(at);
		return true;
	}
	__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		return false;
	}
}

const char* BuildLabel(int build)
{
	const VpExeBuildInfo* b = VpExeBuildInfoOf(build);
	return b ? b->label : "an unknown build";
}

// ---- the block ---------------------------------------------------------------------------------------------------------
// The active block, checked; the plain view of its fields. The cells are reached only through Cell(), the EXE's lookup.
struct Block
{
	ULONG_PTR base;
	int w, h;
	bool wrapCells;  // the block's wrap byte: the lookup takes any column (modulo the width)
	bool wrapBox;    // the wrap byte the handler's box rule reads (mmf_wrap)
	int slot;
};

inline int FloorHalf(int v) { return v >= 0 ? (v >> 1) : -((-v + 1) >> 1); }  // the EXE's sar 1

// The cell of (column, row), as the EXE's lookup (6.2): in range (any column when the block wraps), else the spare cell.
inline volatile BYTE* CellFlags(const Block& b, int col, int row)
{
	if ((b.wrapCells || (col >= 0 && col < b.w)) && row >= 0 && row < b.h)
	{
		if (b.wrapCells)
		{
			col %= b.w;
			if (col < 0) col += b.w;
		}
		return reinterpret_cast<volatile BYTE*>(b.base + kBlkOffCells + (DWORD)(row * kCellsPerRow + col) * 4 +
		                                        kCellOffFlags);
	}
	return reinterpret_cast<volatile BYTE*>(b.base + kBlkOffSpare + kCellOffFlags);
}

// The neighbour of (col, row) in direction d (6.6: the hex of the cell, plus the direction, back to (column, row)).
inline void Neighbour(int col, int row, int d, int* ncol, int* nrow)
{
	const int hx = col - FloorHalf(row);
	const int nr = row + kDirections[d][1];
	*nrow = nr;
	*ncol = hx + kDirections[d][0] + FloorHalf(nr);
}

// The handler's edge rule between the cell and each of its six neighbours (6.6), after the cell's bit 0 changed.
void Edges(const Block& b, int col, int row)
{
	volatile BYTE* c = CellFlags(b, col, row);
	for (int d = 0; d < 6; ++d)
	{
		int nc = 0, nr = 0;
		Neighbour(col, row, d, &nc, &nr);
		volatile BYTE* n = CellFlags(b, nc, nr);
		const BYTE own = (BYTE)(1u << (d + 2));
		const BYTE opp = (BYTE)(1u << (((d + 3) % 6) + 2));
		if (((*c ^ *n) & 1) == 0)
		{
			*c = (BYTE)(*c & ~own);
			*n = (BYTE)(*n & ~opp);
		}
		else
		{
			*c = (BYTE)(*c | own);
			*n = (BYTE)(*n | opp);
		}
	}
}

inline bool Explored(const Block& b, int col, int row) { return (*CellFlags(b, col, row) & 1) == 0; }

// The box of explored plots from all cells (6.6, "the box after a re-fog"): rows from the lowest to the highest explored
// row; columns from the lowest to the highest explored column, or, when the map wraps, the shortest run of columns (round
// the map) that holds every explored column, written so that it holds the first plot's column. The first plot stays when
// it is still explored, else it is the first explored cell by rows. No explored cell: "no plot yet" is set and the box is
// left as it is (the next explored plot starts a new one, as in a new block).
void Box(const Block& b)
{
	InterlockedIncrement(&g_boxes);
	static bool colHas[kCellsPerRow];
	memset(colHas, 0, sizeof(colHas));
	int minR = kMaxRows, maxR = -1, fc = -1, fr = -1;
	for (int r = 0; r < b.h; ++r)
		for (int c = 0; c < b.w; ++c)
			if (Explored(b, c, r))
			{
				colHas[c] = true;
				if (r < minR) minR = r;
				if (r > maxR) maxR = r;
				if (fc < 0)
				{
					fc = c;
					fr = r;
				}
			}
	volatile int* box = reinterpret_cast<volatile int*>(b.base + kBlkOffFirst);
	volatile BYTE* none = reinterpret_cast<volatile BYTE*>(b.base + kBlkOffNone);
	if (maxR < 0)
	{
		InterlockedIncrement(&g_emptyBoxes);
		*none = 1;
		return;
	}
	{
		const int oc = box[0], orow = box[1];
		if (oc >= 0 && oc < b.w && orow >= 0 && orow < b.h && Explored(b, oc, orow))
		{
			fc = oc;
			fr = orow;
		}
	}
	int lo = b.w, hi = -1;
	for (int c = 0; c < b.w; ++c)
		if (colHas[c])
		{
			if (c < lo) lo = c;
			hi = c;
		}
	if (b.wrapBox)
	{
		// The largest run of empty columns round the map, looked for from the first plot's column on (one lap: the first
		// plot's column is explored, so no run crosses it); the box is the rest. Ties: the first such run.
		int bestLen = 0, bestEnd = -1, run = 0;
		for (int k = 1; k <= b.w; ++k)
		{
			const int c = (fc + k) % b.w;
			if (!colHas[c])
			{
				if (++run > bestLen)
				{
					bestLen = run;
					bestEnd = c;
				}
			}
			else
				run = 0;
		}
		if (bestLen > 0)
		{
			const int start = (bestEnd + 1) % b.w;     // the first explored column after the gap
			lo = start;
			hi = start + (b.w - bestLen) - 1;          // may run past the map's right edge
			if (fc < lo)
			{
				lo -= b.w;
				hi -= b.w;
			}
		}
		else
		{
			lo = 0;
			hi = b.w - 1;
		}
	}
	box[0] = fc;
	box[1] = fr;
	box[2] = lo;
	box[3] = minR;
	box[4] = hi;
	box[5] = maxR;
	*none = 0;
}

// The system's redraw bits (+0x1C), by compare-exchange (the render clears the word).
void Redraw(DWORD bits)
{
	volatile LONG* w = reinterpret_cast<volatile LONG*>(g_system + kSysOffRedraw);
	for (int i = 0; i < 1000; ++i)
	{
		const LONG old = *w;
		if (InterlockedCompareExchange(w, (LONG)((DWORD)old | bits), old) == old) return;
	}
}

// The active block, or base 0 when the active slot has none yet. False (refused) when a check fails.
bool FindBlock(Block* out)
{
	memset(out, 0, sizeof(*out));
	DWORD slot = 0, sentinel = 0, node = 0;
	if (!ReadDword(g_slot, &slot) || !ReadDword(g_blocks, &sentinel))
		return Refuse("minimap-data", "the active slot or the block list cannot be read");
	if ((int)slot < 0 || (int)slot > kMaxSlot)
		return Refuse("minimap-slot", "the active slot is %ld (the EXE keeps it in 0..%d)", (long)(int)slot, kMaxSlot);
	if (!sentinel || !ReadDword(sentinel + kNodeOffNext, &node))
		return Refuse("minimap-list", "the block list's sentinel node cannot be read");
	out->slot = (int)slot;
	for (int i = 0; i < kMaxNodes; ++i)
	{
		if (node == sentinel) return true;  // no block for this slot yet
		DWORD s = 0, next = 0;
		if (!node || !ReadDword(node + kNodeOffSlot, &s) || !ReadDword(node + kNodeOffNext, &next))
			return Refuse("minimap-list", "node %d of the block list cannot be read", i);
		if (s == slot)
		{
			out->base = node + kNodeOffBlock;
			break;
		}
		node = next;
	}
	if (!out->base) return Refuse("minimap-list", "the block list does not end after %d nodes", kMaxNodes);
	const ULONG_PTR b = out->base;
	if (!Readable(b, kBlkSize) || !Writable(b, kBlkSize))
		return Refuse("minimap-block", "the active block (slot %d) is not readable and writable memory", out->slot);
	const volatile int* f = reinterpret_cast<const volatile int*>(b);
	out->w = f[kBlkOffWidth / 4];
	out->h = f[kBlkOffHeight / 4];
	if (out->w < 1 || out->w > kCellsPerRow || out->h < 1 || out->h > kMaxRows)
		return Refuse("minimap-block", "the active block's size is %d x %d (1..%d x 1..%d)", out->w, out->h, kCellsPerRow,
		              kMaxRows);
	const BYTE wrapCells = *reinterpret_cast<const volatile BYTE*>(b + kBlkOffWrap);
	BYTE wrapBox = 0;
	if (wrapCells > 1 || !ReadByte(g_wrap, &wrapBox) || wrapBox > 1)
		return Refuse("minimap-wrap", "the wrap bytes are %u (block) and %u (handler) (0 or 1)", (unsigned)wrapCells,
		              (unsigned)wrapBox);
	out->wrapCells = wrapCells != 0;
	out->wrapBox = wrapBox != 0;
	// The transform must take the cube coordinates (x, y, -x-y) to (q, r) = (x, y): (column, row) of a plot is then its own
	// (x, y) (6.4 [L]) and a neighbour is the plot's hex plus the direction (6.6). The matrix itself need not be the
	// identity: build 26577625's blocks hold q = -y - z, r = -x - z (live, 2026-11-25), which is (x, y) for z = -x-y.
	const volatile int* m = reinterpret_cast<const volatile int*>(b + kBlkOffTransform);
	if (m[0] - m[8] != 1 || m[4] - m[8] != 0 || m[12] != 0 || m[1] - m[9] != 0 || m[5] - m[9] != 1 || m[13] != 0)
		return Refuse("minimap-transform", "the active block's hex transform does not take (x, y, -x-y) to (x, y) (q row "
		                                   "%d %d %d %d, r row %d %d %d %d)",
		              m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13]);
	return true;
}
}  // namespace

// ---- arming ------------------------------------------------------------------------------------------------------------
void SetSinks(LogFn log) { g_log = log; }

const char* LayoutVerified(int build)
{
	for (size_t i = 0; i < sizeof(kLayoutVerified) / sizeof(kLayoutVerified[0]); ++i)
		if (kLayoutVerified[i].build == build) return kLayoutVerified[i].evidence;
	return NULL;
}

State CurrentState() { return (State)g_state; }

bool Arm(const Target& t)
{
	EnsureInit();
	if (t.build == kVpExeBuildUnknown)
		return Refuse("minimap-build", "not an EXE build the patch knows (PE timestamp 0x%08lX, SizeOfImage 0x%08lX)",
		              (unsigned long)t.stamp, (unsigned long)t.sizeOfImage);
	const char* ev = LayoutVerified(t.build);
	if (!ev) return Refuse("minimap-layout", "the Minimap system's layout is not verified on %s", BuildLabel(t.build));
	if ((t.notCarried && *t.notCarried) || !t.slot || !t.blocks || !t.system || !t.wrap)
		return Refuse("minimap-symbol", "%s", t.notCarried && *t.notCarried ? t.notCarried : "a symbol is not carried");
	if (!Readable(t.slot, 4) || !Readable(t.blocks, 4) || !Readable(t.system, kSysOffRedraw + 4) ||
	    !Writable(t.system + kSysOffRedraw, 4) || !Readable(t.wrap, 1))
		return Refuse("minimap-data", "the Minimap system's .data cannot be read (or its redraw word written)");
	g_slot = t.slot;
	g_blocks = t.blocks;
	g_system = t.system;
	g_wrap = t.wrap;
	g_build = t.build;
	ClearPending();
	InterlockedExchange(&g_state, kActive);
	Log(false, "minimap fog: on (FixMinimapUnexplored; %s, layout: %s): plots the game sets back to unexplored leave the "
	           "minimap at the next frames",
	    BuildLabel(t.build), ev);
	return true;
}

void Disarm()
{
	if (InterlockedCompareExchange(&g_state, kOff, kActive) == kActive)
	{
		ClearPending();
		Log(false, "minimap fog: off");
	}
}

#if !defined(VP_MINIMAP_FOG_OFFLINE) || defined(VP_MINIMAP_FOG_BINDING)
// ---- the binding: the only reader of the build table ---------------------------------------------------------------------
Target TargetOf(ULONG_PTR base)
{
	static char s_line[300];  // the not-carried line (one binding call at a time: arming is once per load)
	Target t;
	memset(&t, 0, sizeof(t));
	t.build = ProxyExe::BuildOfImage(base, &t.stamp, &t.sizeOfImage);
	if (t.build == kVpExeBuildUnknown) return t;
	static const int kSyms[4] = {VPX_mmf_slot, VPX_mmf_blocks, VPX_mmf_system, VPX_mmf_wrap};
	ULONG_PTR* const outs[4] = {&t.slot, &t.blocks, &t.system, &t.wrap};
	for (int i = 0; i < 4; ++i)
	{
		*outs[i] = ProxyExe::Addr(t.build, kSyms[i], base);
		if (!*outs[i] && !t.notCarried) t.notCarried = ProxyExe::NotCarried(s_line, sizeof(s_line), "", t.build, kSyms[i]);
	}
	return t;
}

bool SetEnabled(bool on)
{
	if (!on)
	{
		Disarm();
		return true;
	}
	return Arm(TargetOf(reinterpret_cast<ULONG_PTR>(GetModuleHandleA(NULL))));
}
#endif

// ---- the note ------------------------------------------------------------------------------------------------------------
void OnFowChanged(int x, int y, int dllMode, bool setAll)
{
	if (g_state != kActive) return;
	EnsureInit();
	const bool unexplored = dllMode == kDllModeUnexplored;
	EnterCriticalSection(&g_cs);
	if (setAll)
	{
		memset(g_pending, unexplored ? kApplyFrames : 0, sizeof(g_pending));
		g_pendingCount = unexplored ? kCells : 0;
		if (unexplored) InterlockedIncrement(&g_noted);
	}
	else if (x < 0 || y < 0 || x >= kCellsPerRow || y >= kMaxRows)
		InterlockedIncrement(&g_outOfRange);
	else
	{
		unsigned char& p = g_pending[y * kCellsPerRow + x];
		if (unexplored)
		{
			if (!p) ++g_pendingCount;
			p = (unsigned char)kApplyFrames;
			InterlockedIncrement(&g_noted);
		}
		else if (p)
		{
			p = 0;
			--g_pendingCount;
			InterlockedIncrement(&g_forgotten);
		}
	}
	LeaveCriticalSection(&g_cs);
}

// ---- the apply (main thread) --------------------------------------------------------------------------------------------
void OnFrame()
{
	if (g_state != kActive || g_pendingCount == 0) return;
	InterlockedIncrement(&g_frames);
	DWORD defer = 0;
	if (!ReadDword(g_system + kSysOffDefer, &defer))
	{
		Refuse("minimap-data", "the Minimap system's defer word cannot be read");
		return;
	}
	if (defer & 1)
	{
		InterlockedIncrement(&g_deferred);  // the system queues its own events now: try again at the next frame
		return;
	}
	Block b;
	if (!FindBlock(&b)) return;
	// This frame's plots (each loses one of its frames).
	int n = 0;
	EnterCriticalSection(&g_cs);
	for (int i = 0; i < kCells && g_pendingCount > 0; ++i)
	{
		if (!g_pending[i]) continue;
		g_work[n++] = (unsigned short)i;
		if (--g_pending[i] == 0) --g_pendingCount;
	}
	LeaveCriticalSection(&g_cs);
	if (!b.base)
	{
		InterlockedIncrement(&g_noBlock);  // the active slot has no block yet: nothing on the minimap to mark
		return;
	}
	int marked = 0;
	for (int k = 0; k < n; ++k)
	{
		const int col = g_work[k] % kCellsPerRow, row = g_work[k] / kCellsPerRow;
		InterlockedIncrement(&g_applied);
		if (col >= b.w || row >= b.h)
		{
			InterlockedIncrement(&g_outside);
			continue;
		}
		volatile BYTE* c = CellFlags(b, col, row);
		if (*c & 1)
		{
			InterlockedIncrement(&g_already);
			continue;
		}
		*c = (BYTE)(*c | 1);
		Edges(b, col, row);
		++marked;
		InterlockedIncrement(&g_marked);
	}
	if (!marked) return;
	Box(b);
	volatile BYTE* spare = reinterpret_cast<volatile BYTE*>(b.base + kBlkOffSpare + kCellOffFlags);
	*spare = (BYTE)(*spare & 1);  // as the handler leaves it
	Redraw(kRedrawCells | kRedrawBox);
	if (g_logged < kMaxChangeLines)
	{
		InterlockedIncrement(&g_logged);
		const volatile int* box = reinterpret_cast<const volatile int*>(b.base + kBlkOffFirst);
		Log(false, "minimap fog: slot %d: %d cell(s) marked unexplored (of %d this frame); box first %d,%d low %d,%d high "
		           "%d,%d%s",
		    b.slot, marked, n, box[0], box[1], box[2], box[3], box[4], box[5],
		    *reinterpret_cast<const volatile BYTE*>(b.base + kBlkOffNone) ? " (no explored plot left)" : "");
	}
}

void FrameHook()
{
	void (*chained)() = g_chained;
	if (chained) chained();
	OnFrame();
}

void Chain(void (*existing)()) { g_chained = existing; }

void CountersLine(char* out, size_t cap)
{
	static const char* const kNames[] = {"off", "active", "refused"};
	const LONG st = g_state;
	_snprintf_s(out, cap, _TRUNCATE,
	            "minimap fog: state %s, noted %ld (out of range %ld, forgotten by a later mode %ld), pending %ld, frames %ld "
	            "(deferred %ld, no block %ld), applied %ld (cells marked %ld, already unexplored %ld, outside the map %ld), "
	            "boxes %ld (empty %ld)",
	            st >= 0 && st <= 2 ? kNames[st] : "?", (long)g_noted, (long)g_outOfRange, (long)g_forgotten,
	            (long)g_pendingCount, (long)g_frames, (long)g_deferred, (long)g_noBlock, (long)g_applied, (long)g_marked,
	            (long)g_already, (long)g_outside, (long)g_boxes, (long)g_emptyBoxes);
}

namespace Test
{
void Reset()
{
	ClearPending();
	g_state = kOff;
	g_slot = g_blocks = g_system = g_wrap = 0;
	g_build = kVpExeBuildUnknown;
	g_noted = g_outOfRange = g_forgotten = g_frames = g_deferred = g_noBlock = g_applied = g_marked = g_already = g_outside =
	    g_boxes = g_emptyBoxes = g_logged = 0;
	g_chained = NULL;
}
long Noted() { return g_noted; }
long Pending() { return g_pendingCount; }
long Marked() { return g_marked; }
long AlreadyUnexplored() { return g_already; }
long OutsideMap() { return g_outside; }
long Deferred() { return g_deferred; }
long NoBlock() { return g_noBlock; }
long Boxes() { return g_boxes; }
long EmptyBoxes() { return g_emptyBoxes; }
long Frames() { return g_frames; }
}  // namespace Test

// ---- the product glue ----------------------------------------------------------------------------------------------------
#ifndef VP_MINIMAP_FOG_OFFLINE
}  // namespace ProxyMinimapFog

namespace ProxyRtInt
{
const Vp64Opt::Options& ProxyOptions();  // proxy_options.cpp
}

namespace ProxyMinimapFog
{
namespace
{
void GlueLog(const char* line, bool loud)
{
	if (loud)
		ProxyRtInt::LogErrPublic("%s", line);
	else
		ProxyRtInt::Log("%s", line);
}
volatile LONG g_badArgs = 0;
}  // namespace

void AfterServed(uint32_t method, const uint8_t* args, uint32_t argsSize)
{
	if (g_state != kActive) return;  // the option off, or refused: nothing at all
	if (method != kM_ICvEngineUtility1_GameplayFOWChanged && method != kM_ICvEngineUtility2_GameplayFOWChanged &&
	    method != kM_ICvEngineUtility3_GameplayFOWChanged && method != kM_ICvEngineUtility4_GameplayFOWChanged)
		return;
	// The CALL payload: the self handle, then x, y, mode (i32) and setAll (b), as the generated dispatcher reads them.
	vpipc::Reader r;
	r.reset(args, argsSize);
	r.handle();
	const int x = (int)r.i32();
	const int y = (int)r.i32();
	const int mode = (int)r.i32();
	const bool all = r.b();
	if (!r.ok())
	{
		if (InterlockedIncrement(&g_badArgs) <= 3)
			ProxyRtInt::LogErrPublic("LOUD: minimap fog: a served GameplayFOWChanged (0x%08lX) had %lu bytes of arguments "
			                         "that do not read as (handle, x, y, mode, setAll): not noted",
			                         (unsigned long)method, (unsigned long)argsSize);
		return;
	}
	OnFowChanged(x, y, mode, all);
}

void Init(void (*existingFrameHook)())
{
	EnsureInit();
	SetSinks(GlueLog);
	Chain(existingFrameHook);
	const Vp64Opt::ExtraState e = ProxyRtInt::ProxyOptions().Extra(Vp64Opt::kFixMinimapUnexplored);
	if (!e.on)
	{
		ProxyRtInt::Log("minimap fog: off (FixMinimapUnexplored not in effect: %s); the minimap keeps re-hidden plots "
		                "until a load, as in the stock game",
		                e.why == Vp64Opt::kExtraCategoryOff ? "EnableBugfixes = 0" : "FixMinimapUnexplored = 0");
		return;
	}
	SetEnabled(true);
}
#endif  // VP_MINIMAP_FOG_OFFLINE
}  // namespace ProxyMinimapFog
