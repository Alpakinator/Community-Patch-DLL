// minimap_fog_test.cpp - offline tests of proxy/src/proxy_minimap_fog.cpp (ticket 0727), linked with it (built with
// VP_MINIMAP_FOG_OFFLINE: no proxy runtime). Built and run by test_minimap_fog.py with the proxy's toolchain (clang-cl
// -m32, VC9 headers) and once under MSVC /fsanitize=address (x86). SPDX-License-Identifier: GPL-3.0-or-later
//
// A fake Minimap system stands in for the EXE's (research/05-graphics-composition/observer-switch-minimap.md 6.2 and 6.6,
// written from that description; nothing of the EXE is read): a ".data" page with the active slot, the block list's
// sentinel pointer, the wrap byte and the system object (+0x18 defer, +0x1C redraw); per slot a node {next, prev, slot}
// and its block (transform, width, height, wrap byte, 256 x 256 cells, the spare cell, the box, the "no plot yet" byte),
// each in committed read-write memory of its own. The fake handler does what 6.6 says the EXE's fog handler does for an
// explored plot (clear bit 0, widen the box by its rule, the edges with the six neighbours, the spare's edge bits, the
// redraw bits); the edge check uses the test's own neighbour table per row parity (not the code's hex arithmetic).
//
// Cases: off (nothing noted, nothing read); the arm refusals (unknown build, a layout not verified, a symbol not carried,
// unreadable .data); a block of plots revealed then un-revealed (cells, edges, box, redraw bits); the box's first plot
// moved; nothing explored left (the "no plot yet" byte); a later reveal of the same plot forgets the note; the EXE's own
// handler running an earlier reveal after the first apply (the next frames win); the defer bit (no apply, then the apply);
// a wrapping map (edges across the seam, the box round the seam and its first plot's side); the active slot switched; no
// block for the slot; setAll; out-of-range plots; every block check (size, wrap bytes, transform, the list) refused with
// one LOUD line; Disarm; the chained frame hook; two threads noting while the main thread applies.
// With VP_MINIMAP_FOG_BINDING also the binding against a fake image header and the build table.
// Exit code 0 when every check passes, 1 otherwise; "--expect-fail" adds one check that must fail.
#include "proxy_minimap_fog.h"
#ifdef VP_MINIMAP_FOG_BINDING
#include "proxy_exe.h"
#endif
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace MF = ProxyMinimapFog;

namespace
{
int g_pass = 0, g_fail = 0;
bool g_verbose = false;
void Check(bool ok, const char* name, int line)
{
	if (ok)
		++g_pass;
	else
	{
		++g_fail;
		printf("FAIL line %d: %s\n", line, name);
	}
}
#define CHECK(x) Check((x), #x, __LINE__)

// ---- the sink ----------------------------------------------------------------------------------------------------------
CRITICAL_SECTION g_logLock;
std::string g_log;
int g_loud = 0;
void LogSink(const char* line, bool loud)
{
	EnterCriticalSection(&g_logLock);
	g_log += line;
	g_log += "\n";
	if (loud) ++g_loud;
	if (g_verbose) printf("    log%s: %s\n", loud ? " LOUD" : "", line);
	LeaveCriticalSection(&g_logLock);
}
bool LogHas(const char* s) { return g_log.find(s) != std::string::npos; }

// ---- the fake Minimap system ---------------------------------------------------------------------------------------------
// ".data": the slot at +0, the list's sentinel pointer at +8, the wrap byte at +0x230, the system object at +0x338 (the
// distances of build 4390913's four items from the slot), the sentinel node at +0x800.
BYTE* g_data = NULL;
const DWORD kDataSlot = 0x0, kDataBlocks = 0x8, kDataWrap = 0x230, kDataSystem = 0x338, kDataSentinel = 0x800;
const DWORD kNodeBytes = MF::kNodeOffBlock + MF::kBlkSize;
std::vector<BYTE*> g_nodes;

DWORD& D(BYTE* p, DWORD off) { return *reinterpret_cast<DWORD*>(p + off); }
int& I(BYTE* p, DWORD off) { return *reinterpret_cast<int*>(p + off); }
BYTE* Sentinel() { return g_data + kDataSentinel; }
DWORD& Redraw() { return D(g_data, kDataSystem + MF::kSysOffRedraw); }
DWORD& Defer() { return D(g_data, kDataSystem + MF::kSysOffDefer); }

void FreeNodes()
{
	for (size_t i = 0; i < g_nodes.size(); ++i) VirtualFree(g_nodes[i], 0, MEM_RELEASE);
	g_nodes.clear();
	D(Sentinel(), 0) = D(Sentinel(), 4) = (DWORD)(ULONG_PTR)Sentinel();
}

// A new block for `slot`: w x h, every cell unexplored (byte 2 = 1), the spare unexplored, "no plot yet" set, a transform
// that takes (x, y, -x-y) to (x, y): the one build 26577625's blocks hold (live, 2026-11-25: q = -y - z, r = -x - z),
// or the 4 x 4 identity (g_identityTransform). Appended at the list's end (as the EXE's block of a slot does).
bool g_identityTransform = false;
const int kLiveTransform[16] = {0, -1, -1, 0, -1, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 1};
BYTE* AddBlock(int slot, int w, int h, bool wrap)
{
	BYTE* n = static_cast<BYTE*>(VirtualAlloc(NULL, kNodeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!n) return NULL;
	BYTE* b = n + MF::kNodeOffBlock;
	for (int k = 0; k < 16; ++k)
		I(b, MF::kBlkOffTransform + (DWORD)k * 4) = g_identityTransform ? (k % 5 == 0 ? 1 : 0) : kLiveTransform[k];
	I(b, MF::kBlkOffWidth) = w;
	I(b, MF::kBlkOffHeight) = h;
	b[MF::kBlkOffWrap] = wrap ? 1 : 0;
	for (int i = 0; i < MF::kCellsPerRow * MF::kMaxRows; ++i) b[MF::kBlkOffCells + i * 4 + 2] = 1;
	b[MF::kBlkOffSpare + 2] = 1;
	b[MF::kBlkOffNone] = 1;
	D(n, MF::kNodeOffSlot) = (DWORD)slot;
	BYTE* last = reinterpret_cast<BYTE*>((ULONG_PTR)D(Sentinel(), 4));
	D(n, 0) = (DWORD)(ULONG_PTR)Sentinel();
	D(n, 4) = (DWORD)(ULONG_PTR)last;
	D(last, 0) = (DWORD)(ULONG_PTR)n;
	D(Sentinel(), 4) = (DWORD)(ULONG_PTR)n;
	g_nodes.push_back(n);
	return b;
}

void SetUp(int w, int h, bool wrap)
{
	if (!g_data) g_data = static_cast<BYTE*>(VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	memset(g_data, 0, 0x1000);
	D(g_data, kDataBlocks) = (DWORD)(ULONG_PTR)Sentinel();
	FreeNodes();
	g_data[kDataWrap] = wrap ? 1 : 0;
	AddBlock(0, w, h, wrap);
}
BYTE* BlockOf(int i) { return g_nodes[i] + MF::kNodeOffBlock; }

MF::Target TargetHere()
{
	MF::Target t;
	memset(&t, 0, sizeof(t));
	t.build = kVpExeBuild4390913;
	t.slot = (ULONG_PTR)(g_data + kDataSlot);
	t.blocks = (ULONG_PTR)(g_data + kDataBlocks);
	t.system = (ULONG_PTR)(g_data + kDataSystem);
	t.wrap = (ULONG_PTR)(g_data + kDataWrap);
	return t;
}

void Fresh(int w, int h, bool wrap, bool arm = true)
{
	MF::Test::Reset();
	MF::SetSinks(LogSink);
	g_log.clear();
	g_loud = 0;
	SetUp(w, h, wrap);
	if (arm) CHECK(MF::Arm(TargetHere()) && MF::CurrentState() == MF::kActive);
}

// ---- the test's own geometry: (column, row) neighbours per row parity (6.6's six directions, written as offsets) ---------
const int kEven[6][2] = {{0, 1}, {-1, 0}, {0, -1}, {-1, -1}, {1, 0}, {-1, 1}};
const int kOdd[6][2] = {{1, 1}, {-1, 0}, {1, -1}, {0, -1}, {1, 0}, {0, 1}};
BYTE* Flags(BYTE* b, int c, int r)
{
	const int w = I(b, MF::kBlkOffWidth), h = I(b, MF::kBlkOffHeight);
	const bool wrap = b[MF::kBlkOffWrap] != 0;
	if (r < 0 || r >= h || (!wrap && (c < 0 || c >= w))) return b + MF::kBlkOffSpare + 2;
	c = ((c % w) + w) % w;
	return b + MF::kBlkOffCells + (r * 256 + c) * 4 + 2;
}
void Nb(int c, int r, int d, int* nc, int* nr)
{
	const int(*t)[2] = (r & 1) ? kOdd : kEven;
	*nc = c + t[d][0];
	*nr = r + t[d][1];
}
bool IsExplored(BYTE* b, int c, int r) { return (*Flags(b, c, r) & 1) == 0; }

// The fake EXE handler for an explored plot (6.6).
void Reveal(BYTE* b, int c, int r)
{
	const int w = I(b, MF::kBlkOffWidth);
	*Flags(b, c, r) &= 0xFE;
	int* box = reinterpret_cast<int*>(b + MF::kBlkOffFirst);
	if (b[MF::kBlkOffNone])
	{
		box[0] = box[2] = box[4] = c;
		box[1] = box[3] = box[5] = r;
		b[MF::kBlkOffNone] = 0;
		Redraw() |= 4;
	}
	if (box[5] <= r) box[5] = r;
	if (r <= box[3]) box[3] = r;
	if (!g_data[kDataWrap])
	{
		if (c < box[2]) box[2] = c;
		if (box[4] < c) box[4] = c;
	}
	else if (c < box[0])
	{
		const int alt = c + w;
		if (c <= box[2] && box[4] <= alt)
		{
			if (box[2] - c < alt - box[4])
				box[2] = c;
			else
				box[4] = alt;
		}
	}
	else
	{
		const int alt = c - w;
		if (box[4] <= c && alt <= box[2])
		{
			if (c - box[4] < box[2] - alt)
				box[4] = c;
			else
				box[2] = alt;
		}
	}
	for (int d = 0; d < 6; ++d)
	{
		int nc, nr;
		Nb(c, r, d, &nc, &nr);
		BYTE* own = Flags(b, c, r);
		BYTE* n = Flags(b, nc, nr);
		const BYTE ob = (BYTE)(1 << (d + 2)), nbit = (BYTE)(1 << (((d + 3) % 6) + 2));
		if (((*own ^ *n) & 1) == 0)
		{
			*own &= (BYTE)~ob;
			*n &= (BYTE)~nbit;
		}
		else
		{
			*own |= ob;
			*n |= nbit;
		}
	}
	b[MF::kBlkOffSpare + 2] &= 1;
	Redraw() |= 1;
}

// Every cell's six edge bits say whether that neighbour's bit 0 differs (the invariant both handlers keep).
int EdgeErrors(BYTE* b)
{
	const int w = I(b, MF::kBlkOffWidth), h = I(b, MF::kBlkOffHeight);
	int bad = 0;
	for (int r = 0; r < h; ++r)
		for (int c = 0; c < w; ++c)
			for (int d = 0; d < 6; ++d)
			{
				int nc, nr;
				Nb(c, r, d, &nc, &nr);
				const BYTE f = *Flags(b, c, r), g = *Flags(b, nc, nr);
				const bool edge = (f >> (d + 2)) & 1;
				if (edge != (((f ^ g) & 1) != 0))
				{
					if (bad < 5 && g_verbose) printf("    edge error at %d,%d dir %d\n", c, r, d);
					++bad;
				}
			}
	return bad;
}

int* Box(BYTE* b) { return reinterpret_cast<int*>(b + MF::kBlkOffFirst); }

// The box's properties after a recompute: every explored cell inside it (a column as itself or shifted by the width on a
// wrapping map), its rows tight, its columns tight (no wrap) or of the shortest possible length (wrap), the first plot
// explored and inside.
bool BoxFits(BYTE* b, bool wrap)
{
	const int w = I(b, MF::kBlkOffWidth), h = I(b, MF::kBlkOffHeight);
	int* box = Box(b);
	int minR = 1 << 30, maxR = -1, minC = 1 << 30, maxC = -1;
	bool col[256] = {false};
	for (int r = 0; r < h; ++r)
		for (int c = 0; c < w; ++c)
			if (IsExplored(b, c, r))
			{
				if (r < minR) minR = r;
				if (r > maxR) maxR = r;
				if (c < minC) minC = c;
				if (c > maxC) maxC = c;
				col[c] = true;
				const bool in = (c >= box[2] && c <= box[4]) || (wrap && ((c + w >= box[2] && c + w <= box[4]) ||
				                                                          (c - w >= box[2] && c - w <= box[4])));
				if (!in) return false;
			}
	if (maxR < 0) return b[MF::kBlkOffNone] == 1;
	if (b[MF::kBlkOffNone] != 0 || box[3] != minR || box[5] != maxR) return false;
	if (!IsExplored(b, box[0], box[1])) return false;
	const bool firstIn = (box[0] >= box[2] && box[0] <= box[4]);
	if (!firstIn) return false;
	if (!wrap) return box[2] == minC && box[4] == maxC;
	// the shortest arc: width minus the longest run of empty columns round the map
	int best = 0;
	for (int s = 0; s < w; ++s)
	{
		int run = 0;
		while (run < w && !col[(s + run) % w]) ++run;
		if (run > best) best = run;
	}
	return box[4] - box[2] + 1 == w - best;
}

void Frames(int n)
{
	for (int i = 0; i < n; ++i) MF::OnFrame();
}

// ---- cases ---------------------------------------------------------------------------------------------------------------
void CaseOff()
{
	Fresh(20, 10, false, false);
	CHECK(MF::CurrentState() == MF::kOff);
	MF::OnFowChanged(3, 3, MF::kDllModeUnexplored, false);
	Frames(3);
	CHECK(MF::Test::Noted() == 0 && MF::Test::Pending() == 0 && MF::Test::Frames() == 0 && Redraw() == 0);
}

void CaseArmRefusals()
{
	Fresh(20, 10, false, false);
	MF::Target t = TargetHere();
	t.build = kVpExeBuildUnknown;
	t.stamp = 0x12345678;
	CHECK(!MF::Arm(t) && MF::CurrentState() == MF::kRefused && g_loud == 1 && LogHas("check minimap-build failed") &&
	      LogHas("0x12345678"));
	Fresh(20, 10, false, false);
	t = TargetHere();
	t.build = kVpExeBuildCount;  // a build without a verified layout
	CHECK(!MF::Arm(t) && LogHas("check minimap-layout failed"));
	CHECK(MF::LayoutVerified(kVpExeBuild4390913) != NULL && MF::LayoutVerified(kVpExeBuild26577625) != NULL &&
	      MF::LayoutVerified(kVpExeBuildCount) == NULL);
	Fresh(20, 10, false, false);
	t = TargetHere();
	t.notCarried = "off: symbol mmf_wrap is not carried";
	t.wrap = 0;
	CHECK(!MF::Arm(t) && LogHas("check minimap-symbol failed: off: symbol mmf_wrap is not carried"));
	Fresh(20, 10, false, false);
	t = TargetHere();
	t.system = 0;
	CHECK(!MF::Arm(t) && LogHas("check minimap-symbol failed"));
	// unreadable .data (a reserved page)
	BYTE* noAccess = static_cast<BYTE*>(VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS));
	Fresh(20, 10, false, false);
	t = TargetHere();
	t.slot = (ULONG_PTR)noAccess;
	CHECK(!MF::Arm(t) && LogHas("check minimap-data failed") && g_loud == 1);
	// refused: nothing is noted afterwards
	MF::OnFowChanged(3, 3, MF::kDllModeUnexplored, false);
	CHECK(MF::Test::Noted() == 0);
	VirtualFree(noAccess, 0, MEM_RELEASE);
	// a read-only system object (its redraw word cannot be written)
	Fresh(20, 10, false, false);
	DWORD oldProt = 0;
	CHECK(VirtualProtect(g_data, 0x1000, PAGE_READONLY, &oldProt));
	CHECK(!MF::Arm(TargetHere()) && LogHas("check minimap-data failed"));
	CHECK(VirtualProtect(g_data, 0x1000, PAGE_READWRITE, &oldProt));
}

// 0552's repro: a block of plots revealed, then the same plots un-revealed.
void CaseRevealUnreveal()
{
	Fresh(66, 42, false);
	BYTE* b = BlockOf(0);
	for (int r = 21; r <= 25; ++r)
		for (int c = 11; c <= 17; ++c) Reveal(b, c, r);   // the start area
	for (int r = 20; r <= 25; ++r)
		for (int c = 42; c <= 49; ++c) Reveal(b, c, r);   // the far block
	CHECK(EdgeErrors(b) == 0 && Box(b)[2] == 11 && Box(b)[4] == 49 && Box(b)[3] == 20 && Box(b)[0] == 11 && Box(b)[1] == 21);
	Redraw() = 0;
	const int spareBefore = b[MF::kBlkOffSpare + 2];
	for (int r = 20; r <= 25; ++r)
		for (int c = 42; c <= 49; ++c) MF::OnFowChanged(c, r, MF::kDllModeUnexplored, false);
	CHECK(MF::Test::Noted() == 48 && MF::Test::Pending() == 48);
	MF::OnFrame();
	int still = 0;
	for (int r = 20; r <= 25; ++r)
		for (int c = 42; c <= 49; ++c) still += IsExplored(b, c, r);
	CHECK(still == 0 && MF::Test::Marked() == 48);
	CHECK(EdgeErrors(b) == 0);
	CHECK(Box(b)[0] == 11 && Box(b)[1] == 21 && Box(b)[2] == 11 && Box(b)[3] == 21 && Box(b)[4] == 17 && Box(b)[5] == 25);
	CHECK(BoxFits(b, false));
	CHECK(Redraw() == (MF::kRedrawCells | MF::kRedrawBox) && b[MF::kBlkOffSpare + 2] == (spareBefore & 1));
	CHECK(LogHas("slot 0: 48 cell(s) marked unexplored (of 48 this frame); box first 11,21 low 11,21 high 17,25"));
	// the next frames apply again (already unexplored: nothing written), then the plots are done
	Redraw() = 0;
	Frames(MF::kApplyFrames - 1);
	CHECK(MF::Test::Pending() == 0 && MF::Test::AlreadyUnexplored() == 48 * (MF::kApplyFrames - 1) && Redraw() == 0 &&
	      MF::Test::Boxes() == 1);
	// with nothing pending a frame reads nothing more
	const long fr = MF::Test::Frames();
	Frames(5);
	CHECK(MF::Test::Frames() == fr);
	// revealed again by the EXE: the stock handler's result, the fix not involved
	for (int r = 20; r <= 25; ++r)
		for (int c = 42; c <= 49; ++c) Reveal(b, c, r);
	CHECK(EdgeErrors(b) == 0 && Box(b)[4] == 49);
	CHECK(g_loud == 0);
}

// Un-revealing a plot in the middle: edges with explored neighbours; the box does not shrink; the first plot moves.
void CaseMiddleAndFirst()
{
	Fresh(30, 20, false);
	BYTE* b = BlockOf(0);
	for (int r = 5; r <= 9; ++r)
		for (int c = 5; c <= 9; ++c) Reveal(b, c, r);
	MF::OnFowChanged(7, 7, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(!IsExplored(b, 7, 7) && EdgeErrors(b) == 0 && Box(b)[2] == 5 && Box(b)[4] == 9 && Box(b)[3] == 5 &&
	      Box(b)[5] == 9);
	CHECK((*Flags(b, 7, 7) & 0xFC) == 0xFC);   // all six neighbours differ now
	// the first plot (5,5) un-revealed: the first explored cell by rows becomes the first plot
	MF::OnFowChanged(5, 5, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(Box(b)[0] == 6 && Box(b)[1] == 5 && BoxFits(b, false) && EdgeErrors(b) == 0);
	// a whole column and row at the box's edge: the box shrinks
	for (int r = 5; r <= 9; ++r) MF::OnFowChanged(9, r, MF::kDllModeUnexplored, false);
	for (int c = 5; c <= 9; ++c) MF::OnFowChanged(c, 9, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(Box(b)[4] == 8 && Box(b)[5] == 8 && Box(b)[2] == 5 && BoxFits(b, false) && EdgeErrors(b) == 0);
	// everything: "no plot yet" set, the box left; the next reveal starts a new box, as in a new block
	for (int r = 0; r < 20; ++r)
		for (int c = 0; c < 30; ++c) MF::OnFowChanged(c, r, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(b[MF::kBlkOffNone] == 1 && MF::Test::EmptyBoxes() == 1 && EdgeErrors(b) == 0);
	CHECK(LogHas("(no explored plot left)"));
	Frames(MF::kApplyFrames);
	Reveal(b, 20, 15);
	CHECK(Box(b)[0] == 20 && Box(b)[2] == 20 && Box(b)[4] == 20 && Box(b)[3] == 15 && Box(b)[5] == 15 &&
	      b[MF::kBlkOffNone] == 0);
}

// A later mode for the same plot wins over an earlier unexplored one; out-of-range plots; setAll.
void CaseOrderAndAll()
{
	Fresh(20, 10, false);
	BYTE* b = BlockOf(0);
	for (int c = 0; c < 6; ++c) Reveal(b, c, 4);
	MF::OnFowChanged(2, 4, MF::kDllModeUnexplored, false);
	MF::OnFowChanged(2, 4, 2, false);   // seen again (FOGOFWARMODE_NOVIS) before the frame
	CHECK(MF::Test::Pending() == 0);
	MF::OnFowChanged(3, 4, MF::kDllModeUnexplored, false);
	MF::OnFowChanged(3, 4, 0, false);   // visible again (FOGOFWARMODE_OFF)
	Frames(2);
	CHECK(IsExplored(b, 2, 4) && IsExplored(b, 3, 4) && MF::Test::Marked() == 0);
	// out of range: counted, nothing noted; a plot outside the block's size: counted at the apply
	MF::OnFowChanged(-1, 4, MF::kDllModeUnexplored, false);
	MF::OnFowChanged(4, 256, MF::kDllModeUnexplored, false);
	CHECK(MF::Test::Pending() == 0);
	MF::OnFowChanged(40, 4, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(MF::Test::OutsideMap() == 1 && MF::Test::Marked() == 0);
	char line[600];
	MF::CountersLine(line, sizeof(line));
	CHECK(strstr(line, "out of range 2") != NULL && strstr(line, "forgotten by a later mode 2") != NULL);
	Frames(MF::kApplyFrames);
	// setAll unexplored: every cell; then setAll visible forgets it before the frame
	MF::OnFowChanged(0, 0, MF::kDllModeUnexplored, true);
	MF::OnFowChanged(0, 0, 0, true);
	CHECK(MF::Test::Pending() == 0);
	MF::OnFowChanged(0, 0, MF::kDllModeUnexplored, true);
	MF::OnFowChanged(1, 4, 2, false);   // ... and one plot seen again after it
	MF::OnFrame();
	int explored = 0;
	for (int r = 0; r < 10; ++r)
		for (int c = 0; c < 20; ++c) explored += IsExplored(b, c, r);
	CHECK(explored == 1 && IsExplored(b, 1, 4) && EdgeErrors(b) == 0 && Box(b)[0] == 1 && Box(b)[2] == 1 &&
	      Box(b)[4] == 1 && BoxFits(b, false));
	Frames(MF::kApplyFrames);
	CHECK(MF::Test::Pending() == 0);
}

// The EXE's own handler runs an earlier reveal of the plot after the first apply: the next frames mark it again.
void CaseLateReveal()
{
	Fresh(20, 10, false);
	BYTE* b = BlockOf(0);
	for (int c = 0; c < 6; ++c) Reveal(b, c, 4);
	MF::OnFowChanged(5, 4, MF::kDllModeUnexplored, false);   // served after the reveal; the EXE's queue still holds
	MF::OnFrame();                                           // the reveal's event
	CHECK(!IsExplored(b, 5, 4) && Box(b)[4] == 4);
	Reveal(b, 5, 4);                                         // the EXE dispatches the earlier reveal now
	CHECK(IsExplored(b, 5, 4) && Box(b)[4] == 5);
	MF::OnFrame();
	CHECK(!IsExplored(b, 5, 4) && Box(b)[4] == 4 && EdgeErrors(b) == 0 && BoxFits(b, false));
}

// The defer bit: no apply while it is set; the plots keep their frames.
void CaseDefer()
{
	Fresh(20, 10, false);
	BYTE* b = BlockOf(0);
	Reveal(b, 3, 3);
	Reveal(b, 4, 3);
	MF::OnFowChanged(4, 3, MF::kDllModeUnexplored, false);
	Defer() = 1;
	Frames(20);
	CHECK(IsExplored(b, 4, 3) && MF::Test::Deferred() == 20 && MF::Test::Pending() == 1);
	Defer() = 0;
	MF::OnFrame();
	CHECK(!IsExplored(b, 4, 3) && EdgeErrors(b) == 0 && Box(b)[4] == 3);
}

// A wrapping map: edges across the seam, the box round the seam on the first plot's side.
void CaseWrap()
{
	Fresh(30, 12, true);
	BYTE* b = BlockOf(0);
	Reveal(b, 2, 5);                                          // the first plot
	for (int c = 0; c <= 4; ++c) Reveal(b, c, 5);
	for (int c = 26; c <= 29; ++c) Reveal(b, c, 5);           // west of the seam: the box runs to -4
	CHECK(Box(b)[2] == -4 && Box(b)[4] == 4 && EdgeErrors(b) == 0);
	MF::OnFowChanged(0, 5, MF::kDllModeUnexplored, false);    // a hole at the seam: no shrink, edges across it
	MF::OnFrame();
	CHECK(!IsExplored(b, 0, 5) && EdgeErrors(b) == 0 && Box(b)[2] == -4 && Box(b)[4] == 4 && BoxFits(b, true));
	for (int c = 26; c <= 29; ++c) MF::OnFowChanged(c, 5, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(EdgeErrors(b) == 0 && Box(b)[2] == 1 && Box(b)[4] == 4 && Box(b)[0] == 2 && BoxFits(b, true));
	// east of the seam from a first plot near the right edge: the box runs past the width
	Fresh(30, 12, true);
	b = BlockOf(0);
	Reveal(b, 27, 6);
	for (int c = 25; c <= 29; ++c) Reveal(b, c, 6);
	for (int c = 0; c <= 3; ++c) Reveal(b, c, 6);
	CHECK(Box(b)[2] == 25 && Box(b)[4] == 33 && EdgeErrors(b) == 0);
	Reveal(b, 12, 2);                                         // a far plot: the box is now most of the map
	const int lo = Box(b)[2], hi = Box(b)[4];
	CHECK(hi - lo + 1 > 15);
	MF::OnFowChanged(12, 2, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(Box(b)[2] == 25 && Box(b)[4] == 33 && Box(b)[3] == 6 && Box(b)[5] == 6 && BoxFits(b, true) && EdgeErrors(b) == 0);
	// the first plot's column un-revealed: the first explored cell by rows, the box written round it
	MF::OnFowChanged(27, 6, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(Box(b)[0] == 0 && Box(b)[1] == 6 && BoxFits(b, true) && Box(b)[2] == -5 && Box(b)[4] == 3);
	// explored columns in two runs: the longest empty run is left out
	Fresh(30, 12, true);
	b = BlockOf(0);
	for (int c = 0; c <= 2; ++c) Reveal(b, c, 1);
	for (int c = 10; c <= 12; ++c) Reveal(b, c, 1);
	Reveal(b, 20, 1);
	MF::OnFowChanged(20, 1, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(BoxFits(b, true) && Box(b)[2] == 0 && Box(b)[4] == 12 && EdgeErrors(b) == 0);
}

// The active slot switched; no block for the slot; the list's walk.
void CaseSlots()
{
	Fresh(20, 10, false);
	BYTE* b0 = BlockOf(0);
	BYTE* b3 = AddBlock(3, 20, 10, false);
	Reveal(b0, 5, 5);
	Reveal(b0, 6, 5);
	Reveal(b3, 5, 5);
	Reveal(b3, 6, 5);
	D(g_data, kDataSlot) = 3;
	MF::OnFowChanged(6, 5, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(IsExplored(b0, 6, 5) && !IsExplored(b3, 6, 5) && EdgeErrors(b3) == 0 && EdgeErrors(b0) == 0);
	CHECK(LogHas("slot 3: 1 cell(s) marked"));
	Frames(MF::kApplyFrames);
	D(g_data, kDataSlot) = 7;   // no block yet for slot 7
	MF::OnFowChanged(5, 5, MF::kDllModeUnexplored, false);
	Frames(MF::kApplyFrames);
	CHECK(MF::Test::NoBlock() == MF::kApplyFrames && MF::Test::Pending() == 0 && IsExplored(b0, 5, 5) &&
	      IsExplored(b3, 5, 5) && MF::CurrentState() == MF::kActive);
}

// Every block check refuses with one LOUD line and turns the fix off.
void CaseBlockChecks()
{
	struct Bad
	{
		const char* name;
		const char* check;
	};
	const Bad bads[] = {{"width 0", "minimap-block"},         {"height 300", "minimap-block"},
	                    {"block wrap byte 2", "minimap-wrap"}, {"handler wrap byte 5", "minimap-wrap"},
	                    {"transform", "minimap-transform"},    {"slot 65", "minimap-slot"},
	                    {"list without end", "minimap-list"},  {"node pointer 0", "minimap-list"},
	                    {"sentinel 0", "minimap-list"},       {"transform shifted", "minimap-transform"}};
	for (size_t i = 0; i < sizeof(bads) / sizeof(bads[0]); ++i)
	{
		Fresh(20, 10, false);
		BYTE* b = BlockOf(0);
		Reveal(b, 4, 4);
		switch (i)
		{
		case 0: I(b, MF::kBlkOffWidth) = 0; break;
		case 1: I(b, MF::kBlkOffHeight) = 300; break;
		case 2: b[MF::kBlkOffWrap] = 2; break;
		case 3: g_data[kDataWrap] = 5; break;
		case 4: I(b, MF::kBlkOffTransform + 4 * 4) = 1; break;   // m[4]: y feeds q
		case 5: D(g_data, kDataSlot) = 65; break;
		case 6:
			D(g_nodes[0], MF::kNodeOffSlot) = 9;          // the slot is not found ...
			D(g_nodes[0], 0) = (DWORD)(ULONG_PTR)g_nodes[0];   // ... and the list loops
			break;
		case 7: D(g_nodes[0], MF::kNodeOffSlot) = 9; D(g_nodes[0], 0) = 0; break;
		case 8: D(g_data, kDataBlocks) = 0; break;
		case 9: I(b, MF::kBlkOffTransform + 13 * 4) = 1; break;   // m[13]: r shifted by one row
		}
		MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
		MF::OnFrame();
		std::string want = std::string("check ") + bads[i].check + " failed";
		const bool ok = MF::CurrentState() == MF::kRefused && g_loud == 1 && LogHas(want.c_str()) && MF::Test::Marked() == 0;
		if (!ok) printf("  block check case %s: state %d, loud %d\n", bads[i].name, (int)MF::CurrentState(), g_loud);
		CHECK(ok);
		// off for the rest of the load: nothing noted, nothing read
		MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
		MF::OnFrame();
		CHECK(MF::Test::Pending() == 0 && g_loud == 1);
	}
	// unreadable defer word after arming
	Fresh(20, 10, false);
	MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
	DWORD oldProt = 0;
	CHECK(VirtualProtect(g_data, 0x1000, PAGE_NOACCESS, &oldProt));
	MF::OnFrame();
	CHECK(VirtualProtect(g_data, 0x1000, PAGE_READWRITE, &oldProt));
	CHECK(MF::CurrentState() == MF::kRefused && LogHas("check minimap-data failed"));
	// an unwritable block
	Fresh(20, 10, false);
	Reveal(BlockOf(0), 4, 4);
	MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
	CHECK(VirtualProtect(g_nodes[0], kNodeBytes, PAGE_READONLY, &oldProt));
	MF::OnFrame();
	CHECK(VirtualProtect(g_nodes[0], kNodeBytes, PAGE_READWRITE, &oldProt));
	CHECK(MF::CurrentState() == MF::kRefused && LogHas("check minimap-block failed") && IsExplored(BlockOf(0), 4, 4));
}

void CaseDisarm()
{
	Fresh(20, 10, false);
	MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
	CHECK(MF::Test::Pending() == 1);
	MF::Disarm();
	CHECK(MF::CurrentState() == MF::kOff && MF::Test::Pending() == 0 && LogHas("minimap fog: off"));
	MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
	CHECK(MF::Test::Pending() == 0);
	CHECK(MF::Arm(TargetHere()) && MF::CurrentState() == MF::kActive && LogHas("minimap fog: on"));
}

int g_chainCalls = 0;
void Chained() { ++g_chainCalls; }

void CaseChain()
{
	Fresh(20, 10, false);
	BYTE* b = BlockOf(0);
	Reveal(b, 4, 4);
	MF::Chain(Chained);
	MF::OnFowChanged(4, 4, MF::kDllModeUnexplored, false);
	MF::FrameHook();
	CHECK(g_chainCalls == 1 && !IsExplored(b, 4, 4));
	MF::Chain(NULL);
	MF::FrameHook();
	CHECK(g_chainCalls == 1);
}

// Two threads note plots (unexplored and seen in turn) while the main thread applies.
volatile LONG g_stop = 0;
unsigned __stdcall Noter(void* arg)
{
	const int seed = (int)(ULONG_PTR)arg;
	unsigned x = (unsigned)seed * 2654435761u + 1;
	while (!g_stop)
	{
		x = x * 1103515245u + 12345u;
		const int c = (int)((x >> 8) % 40), r = 10 + (int)((x >> 16) % 20);   // rows 0-9 stay explored (a box to fit)
		MF::OnFowChanged(c, r, (x & 1) ? MF::kDllModeUnexplored : 2, false);
	}
	return 0;
}

void CaseRace()
{
	Fresh(40, 30, false);
	BYTE* b = BlockOf(0);
	for (int r = 0; r < 30; ++r)
		for (int c = 0; c < 40; ++c) Reveal(b, c, r);
	g_stop = 0;
	HANDLE t1 = (HANDLE)_beginthreadex(NULL, 0, Noter, (void*)1, 0, NULL);
	HANDLE t2 = (HANDLE)_beginthreadex(NULL, 0, Noter, (void*)2, 0, NULL);
	// at least 400 frames, and until the noters have noted 20,000 times (at most 20 s)
	const DWORD t0 = GetTickCount();
	for (int i = 0; i < 400 || (MF::Test::Noted() < 20000 && GetTickCount() - t0 < 20000); ++i) MF::OnFrame();
	InterlockedExchange(&g_stop, 1);
	WaitForSingleObject(t1, INFINITE);
	WaitForSingleObject(t2, INFINITE);
	CloseHandle(t1);
	CloseHandle(t2);
	Frames(MF::kApplyFrames);
	CHECK(MF::Test::Pending() == 0 && MF::CurrentState() == MF::kActive && g_loud == 0);
	CHECK(EdgeErrors(b) == 0);
	CHECK(BoxFits(b, false) && b[MF::kBlkOffNone] == 0 && Box(b)[3] == 0 && Box(b)[2] == 0 && Box(b)[4] == 39);
	CHECK(MF::Test::Marked() > 0);
	if (g_verbose)
		printf("    race: marked %ld, box %d,%d %d,%d %d,%d none %d\n", MF::Test::Marked(), Box(b)[0], Box(b)[1], Box(b)[2],
		       Box(b)[3], Box(b)[4], Box(b)[5], (int)b[MF::kBlkOffNone]);
}

#ifdef VP_MINIMAP_FOG_BINDING
// ---- the binding: a fake image header, the build table ---------------------------------------------------------------
void WriteHeader(BYTE* base, uint32_t stamp, uint32_t soi)
{
	memset(base, 0, 0x400);
	base[0] = 'M';
	base[1] = 'Z';
	*reinterpret_cast<DWORD*>(base + 0x3C) = 0x80;
	memcpy(base + 0x80, "PE\0\0", 4);
	*reinterpret_cast<WORD*>(base + 0x84) = 0x14C;
	*reinterpret_cast<DWORD*>(base + 0x80 + 8) = stamp;
	*reinterpret_cast<WORD*>(base + 0x80 + 24) = 0x10B;
	*reinterpret_cast<DWORD*>(base + 0x80 + 24 + 56) = soi;
}

void CaseBinding()
{
	const VpExeBuildInfo* old = VpExeBuildInfoOf(kVpExeBuild4390913);
	const VpExeBuildInfo* nb = VpExeBuildInfoOf(kVpExeBuild26577625);
	BYTE* base = static_cast<BYTE*>(VirtualAlloc(NULL, old->sizeOfImage, MEM_RESERVE, PAGE_NOACCESS));
	CHECK(base != NULL);
	if (!base) return;
	CHECK(VirtualAlloc(base, 0x1000, MEM_COMMIT, PAGE_READWRITE) != NULL);
	WriteHeader(base, old->timeDateStamp, old->sizeOfImage);
	MF::Target t = MF::TargetOf((ULONG_PTR)base);
	CHECK(t.build == kVpExeBuild4390913);
	CHECK(t.slot == (ULONG_PTR)base + VpExeRva(kVpExeBuild4390913, VPX_mmf_slot) && t.slot > (ULONG_PTR)base);
	CHECK(t.blocks == t.slot + 8);                     // 0x1A0C340 - 0x1A0C338 (the anchors)
	CHECK(t.system == t.slot + 0x338 && t.wrap == t.slot + 0x230);
	CHECK(!t.notCarried || !*t.notCarried);
	// arm on it: the .data pages are not committed in the fake image, so the readable check refuses
	Fresh(20, 10, false, false);
	CHECK(!MF::Arm(t) && LogHas("check minimap-data failed"));
	// with the page committed: armed
	CHECK(VirtualAlloc(reinterpret_cast<void*>(t.slot & ~0xFFFu), 0x2000, MEM_COMMIT, PAGE_READWRITE) != NULL);
	Fresh(20, 10, false, false);
	CHECK(MF::Arm(t) && MF::CurrentState() == MF::kActive);
	// an apply on zeroed .data: the slot 0, the list's sentinel pointer 0: refused, nothing written
	MF::OnFowChanged(1, 1, MF::kDllModeUnexplored, false);
	MF::OnFrame();
	CHECK(MF::CurrentState() == MF::kRefused && LogHas("check minimap-list failed"));
	// a symbol not carried
	const int syms[4] = {VPX_mmf_slot, VPX_mmf_blocks, VPX_mmf_system, VPX_mmf_wrap};
	const char* names[4] = {"mmf_slot", "mmf_blocks", "mmf_system", "mmf_wrap"};
	for (int i = 0; i < 4; ++i)
	{
		ProxyExe::Test::DropSymbol(syms[i]);
		t = MF::TargetOf((ULONG_PTR)base);
		CHECK(t.notCarried && strstr(t.notCarried, names[i]) != NULL);
		Fresh(20, 10, false, false);
		CHECK(!MF::Arm(t) && LogHas("check minimap-symbol failed"));
	}
	ProxyExe::Test::DropSymbol(-1);
	// the new build's header: known, its layout verified, the four names carried
	WriteHeader(base, nb->timeDateStamp, nb->sizeOfImage);
	t = MF::TargetOf((ULONG_PTR)base);
	CHECK(t.build == kVpExeBuild26577625 && t.slot > (ULONG_PTR)base && t.blocks > (ULONG_PTR)base &&
	      t.system > (ULONG_PTR)base && t.wrap > (ULONG_PTR)base && (!t.notCarried || !*t.notCarried));
	Fresh(20, 10, false, false);
	{
		const bool armed = MF::Arm(t);
		CHECK(armed || (!LogHas("check minimap-layout failed") && !LogHas("check minimap-symbol failed")));
	}
	// an unknown header, and no image at all
	WriteHeader(base, 0x11111111u, 0x2000);
	t = MF::TargetOf((ULONG_PTR)base);
	CHECK(t.build == kVpExeBuildUnknown && t.stamp == 0x11111111u && t.slot == 0 && t.wrap == 0);
	Fresh(20, 10, false, false);
	CHECK(!MF::Arm(t) && LogHas("check minimap-build failed"));
	t = MF::TargetOf(0);
	CHECK(t.build == kVpExeBuildUnknown);
	VirtualFree(base, 0, MEM_RELEASE);
}
#endif
}  // namespace

int main(int argc, char** argv)
{
	bool expectFail = false;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "-v") == 0) g_verbose = true;
		if (strcmp(argv[i], "--expect-fail") == 0) expectFail = true;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	InitializeCriticalSection(&g_logLock);
	CaseOff();
	CaseArmRefusals();
	CaseRevealUnreveal();
	g_identityTransform = true;   // the plain identity is accepted too
	CaseRevealUnreveal();
	g_identityTransform = false;
	CaseMiddleAndFirst();
	CaseOrderAndAll();
	CaseLateReveal();
	CaseDefer();
	CaseWrap();
	CaseSlots();
	CaseBlockChecks();
	CaseDisarm();
	CaseChain();
	CaseRace();
#ifdef VP_MINIMAP_FOG_BINDING
	CaseBinding();
	printf("minimap_fog_test: binding cases included\n");
#endif
	FreeNodes();
	if (expectFail) CHECK(!"the runner's negative test: this check must fail");
	printf("minimap_fog_test: %d/%d checks passed\n", g_pass, g_pass + g_fail);
	return g_fail ? 1 : 0;
}
