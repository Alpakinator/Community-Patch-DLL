// cut_tap.cpp - the cut-mask tap inside shell_composite.dll (ticket 4553; contract, layout and keys: cut_tap.h).
// Written 2026-11-16 for this repository (GPL-3.0-or-later). What the EXE's plot mask grid is and why each byte pattern
// below is checked: research/05-graphics-composition/trees.md section 14 and cities-and-landmarks.md 2.7 (behaviour in own
// words, D83; nothing of the EXE is in this file but the patterns' few bytes, as in forest_tap.cpp and ice_tap.cpp).
//
// Threads: CutTap_OnPresent (the composite's render thread) only stores the environment into the variables below and
// starts the worker once; with the tap off from the start it makes the control block and publishes OFF itself, and no
// worker runs. The worker (below-normal priority) does everything else: the pattern check, the guarded reads, the checks,
// the comparison with the last image, the image section, the control block. Once the worker exists only it publishes.
//
// One poll (every cut_poll_ms): GridHeightmap pointer [cam_gridptr] (0: no map: NO_GRID); the grid's header cut_grid+0x00..
// +0x2F (made byte 0: NO_GRID); the header's checks (sizes multiples of 16, tiles = sizes / 16, 1..1024 per axis, in use
// <= blocks <= capacity, blocks <= tiles, the directory pointer set, the pool pointer set when blocks > 0); the
// GridHeightmap's sample counts (+0x0C / +0x10: roundup16(samples >> 1) = the grid's size) and origin (+0xB4 / +0xB8,
// finite); the directory and the pool copied; the header read again (it must be byte-equal: else a torn read); every
// directory entry's state 0 or 1, every pooled index below the blocks, the pooled tiles = the in-use count. A failed check
// is a race with the EXE stamping at that moment (a plain line, read again at the next poll) or, when the same reason
// repeats for cut_loud_polls polls in a row, a layout the tap does not know (LOUD once per run); counted. A torn read (the
// header changed during the copy) says nothing about the layout: it has its own run, LOUD only when every poll for
// cut_torn_loud_s (120 s, three times the about 40 s of growth after a huge load; trees.md 14.8) was torn (ticket 0500).
// A copy whose FNV-1a 64 differs from the published image's, the same for cut_settle_polls polls, is published (also when
// only the load changed).
//
// Safety: every read of EXE memory goes through SafeRead (bounds + SEH): a fault turns the tap off for the process
// (FAILED EXCEPTION, LOUD), except a fault while copying the directory or the pool under a header that changed since the
// first read: the EXE replaced the block meanwhile (the pool's growth), a torn read (ticket 0690, K28). A failed pattern check, an EXE build the patch does not know, a symbol the build's table does
// not carry, or an EXE the composite did not validate, refuses the EXE for good (FAILED PATTERN, LOUD). It calls no EXE
// function and writes nothing into the EXE. Addresses: the EXE build table (epic 0590, D202; shell_exe.h): the symbols
// cut_grid and cam_gridptr and the pattern sites below, by name; their VAs are in the local per-build tables only.
//
// Test seam (as forest_test): cut_test=<mode> (ini or VPSHELL_CUT_TEST) builds a synthetic GridHeightmap and grid in this
// DLL's memory and runs the same code on them (BuildTestWorld; shell/tests/test_cut_tap.py recomputes them). A file
// `shell_composite.cutchange` beside the DLL flips bit 0 of one pooled cell of the test grid (deleted when seen); a file
// `shell_composite.cutclear` makes the test grid "not made" (byte +0 = 0) and back (deleted when seen). Modes: ok,
// notmade, nohm (GridHeightmap pointer 0), badstate (a directory entry of state 7), badindex (a pool index past the
// blocks), badused (the in-use count one more than the pooled tiles), badsize (the width not a multiple of 16), badsamples
// (the sample count gives another width), badorigin (ox not finite), fault (the directory is unmapped memory), patterns
// (the real pattern check on a fake image of the build cut_test_build=<steam build> (default 4390913) holding every
// pattern, one broken by cut_test_pattern=<k>, -1 = none broken; cut_test_drop=<symbol> makes that symbol not carried;
// cut_test_build=0 lays out an image whose header names no known build), torn
// (the header's free head changed between the two header reads for cut_test_torn=<n> polls, 30 by default, -1 = always;
// then the grid of ok; with cut_test_torn_free=1|2|3 the pool is replaced and released during the copy instead:
// TestTornFree, ticket 0690).
#include <windows.h>
#include "cut_tap.h"
#include "shell_exe.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---- the EXE (DX11; addresses from the build table, epic 0590: shell_exe.h) ------------------------------------------------
// cut_grid: the plot mask grid, a static object of the Terrain system (trees.md 14.1); cam_gridptr: the pointer to the
// GridHeightmap (0 before a map loads).
#define CT_HM_SAMPLES_X 0x0Cu
#define CT_HM_SAMPLES_Y 0x10u
#define CT_HM_OX 0xB4u
#define CT_HM_OY 0xB8u
#define CT_VIEW_BYTES (1u << 20)

// Byte patterns checked before the first read of the real EXE (R(name) = 4 bytes holding the symbol's address in the
// running build; gen_table.py checks every row on both builds, exebuild/patterns.txt). Function names are the research's
// (trees.md 14); the addresses are the build table's.
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kCtSigs[] = {
	X(VPX_cut_forest_lookup, "the forest's blocks: ECX = the grid for the lookup", "B9 R(cut_grid) F3 0F 11 84 24 80 00 00 00 E8 19 EE D5 FF")
	X(VPX_cut_shadow_lookup, "the tree shadow quads: ECX = the grid for the lookup", "B9 R(cut_grid) F3 0F 11 84 24 9C 00 00 00 E8 09 54 CC FF")
	X(VPX_cut_lookup, "the lookup: origin GridHeightmap +0xB4/+0xB8, row x 1.1547344, >> 1, bounds grid +0x08/+0x0C", "A1 R(cam_gridptr) 83 EC 10 85 C0 74 7B 8B 55 08 F3 0F 10 12 F3 0F 10 80 B4 00 00 00 F3 0F 10 5A 04 F3 0F 10 88 B8 00 00 00 F3 0F 58 C2 F3 0F 58 CB F3 0F 59 0D R(cut_k_row) 0F 57 D2 F3 0F 10 D0 0F 28 C2 F3 0F 2C C0 0F 57 C0 D1 F8 F3 0F 10 C1 F3 0F 2C D0 D1 FA 83 C1 08 89 44 24 08 89 54 24 0C 85 C0 7C 23 85 D2 7C 1F 3B 01 7D 1B 3B 51 04 7D 16")
	X(VPX_cut_cell, "a cell: tile = (row >> 4) x tilesX (+0x10) + (col >> 4); directory +0x18, state word 0 = pooled; pool +0x24, block x 256 + (row & 15) x 16 + (col & 15)", "C1 EE 04 0F AF 71 08 57 8B FA C1 EF 04 03 F7 8B 79 10 66 83 3C F7 00 8D 34 F7 74 08 8A 46 04 5F 5E C2 04 00 8B 76 04 C1 E6 04 83 E0 0F 03 F0 8B 41 1C C1 E6 04 83 E2 0F 03 F2 8A 04 06")
	X(VPX_cut_size, "the grid's size: width and height rounded up to 16 (+0x08/+0x0C), tiles = / 16 (+0x14 here)", "8D 48 FF 83 E1 0F 2B C1 8D 48 0F 8B C2 8D 50 FF 83 E2 0F 2B C2 83 C0 0F 89 46 04 C1 E8 04 89 0E C1 E9 04 89 46 0C 0F AF C1")
	X(VPX_cut_write_state, "a cell write: the directory entry's state 0 (pooled) or 1 (uniform, the byte at +4)", "0F AF 47 08 8B 4D 00 8B 57 10 C1 E9 04 03 C1 8D 1C C2 8A 43 04 88 44 24 10 0F B7 03 33 F6 2B C6 74 4A 83 E8 01")
	X(VPX_cut_write_pool, "a cell write: a block from the pool (free list +0x1C), its index at the entry's +4, x 256", "8D 4F 14 E8 4B E2 02 00 8B 4C 24 10 89 43 04 C1 E0 08")
	X(VPX_cut_write_block, "a cell write: a pooled tile's block = index x 256 + pool +0x24", "8B 73 04 C1 E6 08 03 77 1C")
	X(VPX_cut_pool_block, "a pool block: free head none (all ones but the top nibble), blocks +0x28, capacity +0x2C", "8B 45 00 56 57 3D FF FF FF 0F 75 54 8B 45 0C 53 8D 5D 08 89 44 24 10 8B 43 08 39 43 04 75 08 50 8B CB E8 75 9B FA FF 8B 7B 04 C1 E7 08")
	X(VPX_cut_freed, "the grid freed: +0x08/+0x0C/+0x18/+0x20/+0x28 cleared, free head none (all ones but the top nibble), made byte 0", "8B 46 18 33 DB 50 89 5E 08 89 5E 0C E8 FB 38 10 00 89 5E 18 C7 46 1C FF FF FF 0F 89 5E 28 83 C4 04 8B CE 89 5E 20 E8 51 6C D8 FF 88 1E 5E 5B")
	X(VPX_cut_made, "the grid made: the GridHeightmap's samples +0x0C/+0x10 halved", "A1 R(cam_gridptr) 56 8B F1 8B 48 0C 8B 40 10 D1 F8 89 44 24 0C 99 83 E2 0F 03 C2 57 8B F8 D1 F9")
	X(VPX_cut_create_call, "CreateTerrain: ECX = the grid, call of the grid's make", "B9 R(cut_grid) E8 4D D3 FE FF")
	X(VPX_cut_shutdown_call, "TerrainSystem::ShutdownGame: ECX = the grid, call of the grid's free", "B9 R(cut_grid) E8 5D 7E 0A 00")
};
#undef X
#define CT_NSIGS ((int)(sizeof(kCtSigs) / sizeof(kCtSigs[0])))
// the symbols the tap reads besides its pattern sites
static const int kCtNeed[] = { VPX_cut_grid, VPX_cam_gridptr };
static int g_build = kVpExeBuildUnknown;   // the EXE build the patterns matched on (set once by CheckPatterns)

// ---- shared with the render thread -------------------------------------------------------------------------------------
static volatile LONG g_envBase, g_envValidated, g_envGridValid, g_envGridSeq, g_envEnabled;
static void (*volatile g_log)(const char*, ...);
static HANDLE g_worker, g_stop;
static volatile LONG g_workerStarted;
static bool g_offPublished;        // render thread: OFF published with no worker

// ---- the worker's state ------------------------------------------------------------------------------------------------
static bool g_init, g_dead, g_checked, g_patternsOk;
static uint32_t g_logs;
static char g_lastLoud[256]; static uint32_t g_loudRepeat;
static wchar_t g_dir[MAX_PATH];
static uint32_t g_pollMs = 500, g_settlePolls = 2;
static uint64_t g_maxBytes = VPSHELL_CUT_MAX_BYTES;
static HANDLE g_ctl; static VPShellCutTapControl* g_blk;
static HANDLE g_img, g_prevImg;           // the newest generation's section and the one before it
static wchar_t g_imgName[64];
static uint64_t g_imgBytes, g_imgFnv;
static uint32_t g_imgLoad, g_imgTilesX, g_imgTilesY, g_imgBlocks;
static uint32_t g_state = VPCUT_NONE, g_fail;
static uint32_t g_gen, g_polls, g_refused;
static int64_t g_readyQpc;
static float g_lastPollMs, g_maxPollMs;
static char g_why[256];
static uint32_t g_load = 0xFFFFFFFFu;
static uint64_t g_pendingHash, g_publishedHash; static uint32_t g_samePolls, g_pendingLoad;
static bool g_havePublished;
static char g_lastReason[200];
static uint32_t g_reasonStreak, g_loudPolls = 4; static bool g_reasonLoud;
static uint32_t g_tornStreak, g_torn, g_tornLoudS = 120; static bool g_tornLoud; static int64_t g_tornStartQpc;
static uint32_t g_ackLogged;
static double g_qpcMs;
// the copy of one poll
static unsigned char g_hdr[VPSHELL_CUT_GRID_HDR], g_hdr2[VPSHELL_CUT_GRID_HDR];
static unsigned char* g_dirBuf; static uint32_t g_dirCap, g_dirBytes;
static unsigned char* g_poolBuf; static uint32_t g_poolCap, g_poolBytes;
static uint32_t g_samplesX, g_samplesY; static float g_ox, g_oy;
static uint32_t g_pooled;

#define CLOG(...) do { void (*lg_)(const char*, ...) = g_log; if (lg_ && g_logs < 4000) { g_logs++; lg_(__VA_ARGS__); } } while (0)

static int64_t Qpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }

static void Loud(const char* fmt, ...)
{
	char b[400];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, ap); va_end(ap);
	if (!strcmp(b, g_lastLoud) && (++g_loudRepeat % 1000) != 0) return;
	if (strcmp(b, g_lastLoud)) { strncpy_s(g_lastLoud, b, _TRUNCATE); g_loudRepeat = 0; }
	void (*lg)(const char*, ...) = g_log;
	if (lg) lg("cut_tap: LOUD: %s", b);
}

// ---- guarded reads (forest_tap.cpp: a loop under SEH, never memcpy on a wild address) -----------------------------------
#if defined(__SANITIZE_ADDRESS__)
__declspec(no_sanitize_address)
#endif
static bool SafeRead(uintptr_t a, void* out, size_t n, bool* fault)
{
	if (a < 0x10000 || n == 0 || n > 0x4000000 || a + n < a || (uint64_t)a + n > 0xFFFF0000ull) return false;
	__try
	{
		unsigned char* d = (unsigned char*)out;
		size_t i = 0;
		if (!(a & 3) && n >= 64)
		{
			volatile const uint32_t* s4 = (volatile const uint32_t*)a;
			for (; i + 4 <= n; i += 4) { uint32_t v = s4[i / 4]; memcpy(d + i, &v, 4); }
		}
		volatile const unsigned char* s = (volatile const unsigned char*)a;
		for (; i < n; ++i) d[i] = s[i];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*fault = true;
		return false;
	}
}

static inline uint32_t U32(const unsigned char* b, uint32_t off) { uint32_t v; memcpy(&v, b + off, 4); return v; }
static inline uint16_t U16(const unsigned char* b, uint32_t off) { uint16_t v; memcpy(&v, b + off, 2); return v; }
static void Put32(unsigned char* b, uint32_t off, uint32_t v) { memcpy(b + off, &v, 4); }
static void Put16(unsigned char* b, uint32_t off, uint16_t v) { memcpy(b + off, &v, 2); }
static void PutF(unsigned char* b, uint32_t off, float v) { memcpy(b + off, &v, 4); }
static uint32_t RoundUp16(uint32_t v) { return (v + 15u) & ~15u; }

// ---- the synthetic EXE (test mode) ------------------------------------------------------------------------------------
// shell/tests/test_cut_tap.py recomputes it. GridHeightmap: samples 200 x 72 (+0x0C/+0x10), ox 1.5, oy -2.25. The grid:
// width roundup16(100) = 112, height roundup16(36) = 48, tiles 7 x 3 = 21. Tile t (row-major) is pooled when t % 3 == 0
// (7 tiles), with block index 6 - t / 3 (the blocks in reverse order), else uniform with the byte (t x 37 + 5) & 0xFF.
// Block k holds byte (k x 31 + i x 7) & 0xFF at cell index i (row x 16 + col). The pool has 8 blocks (block 7 free: its
// first dword 0x0FFFFFFF, u16 at +4 = 1, the rest 0xEE), the free head 7, 7 in use, capacity 12. The cutchange file flips
// bit 0 of cell 0x21 of block 2 (tile 12).
enum { T_NONE, T_OK, T_NOTMADE, T_NOHM, T_BADSTATE, T_BADINDEX, T_BADUSED, T_BADSIZE, T_BADSAMPLES, T_BADORIGIN, T_FAULT,
	T_PATTERNS, T_TORN, T_COUNT };
static const char* kTestNames[T_COUNT] = { "", "ok", "notmade", "nohm", "badstate", "badindex", "badused", "badsize",
	"badsamples", "badorigin", "fault", "patterns", "torn" };
static const char* kStateNames[] = { "NONE", "?", "READY", "FAILED", "OFF", "NO_GRID" };
#define TEST_TX 7u
#define TEST_TY 3u
#define TEST_BLOCKS 8u
static int g_test; static int g_testPattern = -1; static int g_testTorn = 30, g_testTornFree;
static char g_testBuild[32], g_testDrop[96];   // cut_test=patterns: the fake image's build, a symbol made "not carried"
struct TestWorld { unsigned char hm[0x100]; unsigned char grid[0x40]; uint32_t hmPtr; unsigned char* dir; unsigned char* pool;
	unsigned char* image; void* unmapped; bool poolVa; unsigned char* latePool; };
#define TEST_POOL_BYTES (12u * VPSHELL_CUT_BLOCK)
static TestWorld* g_tw;

static void FreeTestWorld()
{
	if (!g_tw) return;
	free(g_tw->dir);
	if (g_tw->poolVa) { if (g_tw->pool) VirtualFree(g_tw->pool, 0, MEM_RELEASE); } else free(g_tw->pool);
	if (g_tw->image) VirtualFree(g_tw->image, 0, MEM_RELEASE);
	if (g_tw->unmapped) VirtualFree(g_tw->unmapped, 0, MEM_RELEASE);
	free(g_tw); g_tw = nullptr;
}

static void BuildTestWorld()
{
	g_tw = (TestWorld*)calloc(1, sizeof(TestWorld));
	if (!g_tw) return;
	unsigned char* hm = g_tw->hm; unsigned char* g = g_tw->grid;
	Put32(hm, CT_HM_SAMPLES_X, 200); Put32(hm, CT_HM_SAMPLES_Y, 72);
	PutF(hm, CT_HM_OX, 1.5f); PutF(hm, CT_HM_OY, -2.25f);
	g_tw->hmPtr = g_test == T_NOHM ? 0u : (uint32_t)(uintptr_t)hm;
	const uint32_t tiles = TEST_TX * TEST_TY;
	g_tw->dir = (unsigned char*)calloc(tiles, VPSHELL_CUT_DIR_ENTRY);
	g_tw->poolVa = g_test == T_TORN && g_testTornFree;   // ticket 0690: a pool the "EXE" can release (a read then faults)
	g_tw->pool = g_tw->poolVa ? (unsigned char*)VirtualAlloc(nullptr, TEST_POOL_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
		: (unsigned char*)calloc(12, VPSHELL_CUT_BLOCK);
	if (!g_tw->dir || !g_tw->pool) return;
	memset(g_tw->dir, 0xCD, tiles * VPSHELL_CUT_DIR_ENTRY);   // the EXE leaves bytes +2, +3 and +5..+7 as they were
	for (uint32_t t = 0; t < tiles; ++t)
	{
		unsigned char* e = g_tw->dir + t * VPSHELL_CUT_DIR_ENTRY;
		if (t % 3 == 0) { Put16(e, 0, 0); Put32(e, 4, 6 - t / 3); }
		else { Put16(e, 0, 1); e[4] = (unsigned char)((t * 37 + 5) & 0xFF); }
	}
	for (uint32_t k = 0; k < 7; ++k)
		for (uint32_t i = 0; i < VPSHELL_CUT_BLOCK; ++i) g_tw->pool[k * VPSHELL_CUT_BLOCK + i] = (unsigned char)((k * 31 + i * 7) & 0xFF);
	unsigned char* fb = g_tw->pool + 7 * VPSHELL_CUT_BLOCK;
	memset(fb, 0xEE, VPSHELL_CUT_BLOCK); Put32(fb, 0, VPCUT_NIL); Put16(fb, 4, 1);
	g[VPCUT_G_MADE] = g_test == T_NOTMADE ? 0 : 1;
	Put32(g, VPCUT_G_WIDTH, 112); Put32(g, VPCUT_G_HEIGHT, 48); Put32(g, VPCUT_G_TILESX, TEST_TX); Put32(g, VPCUT_G_TILESY, TEST_TY);
	Put32(g, VPCUT_G_DIR, (uint32_t)(uintptr_t)g_tw->dir); Put32(g, VPCUT_G_FREE, 7); Put32(g, VPCUT_G_USED, 7);
	Put32(g, VPCUT_G_POOL, (uint32_t)(uintptr_t)g_tw->pool); Put32(g, VPCUT_G_BLOCKS, TEST_BLOCKS); Put32(g, VPCUT_G_CAPACITY, 12);
	if (g_test == T_BADSTATE) Put16(g_tw->dir + 4 * VPSHELL_CUT_DIR_ENTRY, 0, 7);
	if (g_test == T_BADINDEX) Put32(g_tw->dir + 9 * VPSHELL_CUT_DIR_ENTRY, 4, TEST_BLOCKS);
	if (g_test == T_BADUSED) Put32(g, VPCUT_G_USED, 8);
	if (g_test == T_BADSIZE) Put32(g, VPCUT_G_WIDTH, 113);
	if (g_test == T_BADSAMPLES) Put32(hm, CT_HM_SAMPLES_X, 240);
	if (g_test == T_BADORIGIN) { const uint32_t nan = 0x7FC00000u; memcpy(hm + CT_HM_OX, &nan, 4); }
	if (g_test == T_FAULT)
	{
		void* v = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);   // reserved, never committed: a read faults
		g_tw->unmapped = v;
		Put32(g, VPCUT_G_DIR, (uint32_t)(uintptr_t)v);
	}
}

// cut_test=torn with cut_test_torn_free=<k> (ticket 0690): instead of the free head, the "EXE" replaces the pool between
// the directory's and the pool's copy, as its growth after a huge load does (trees.md 14.2: the data pointer moves), and
// releases the old block, so the copy faults. k = 1: the new pointer is in the header before the copy (a torn read);
// 2: the old block is released first and the new pointer is stored only at the tap's first re-check of the header after
// the fault (an EXE that frees before it stores; a torn read); 3: the block is released and the header keeps its pointer
// (a dangling pointer, the negative: FAILED EXCEPTION). k = 1 and 2 for cut_test_torn polls (-1 = always), then the grid
// of ok at its new address.
static void TestTornFree()
{
	if (g_test != T_TORN || !g_tw || !g_tw->poolVa || !g_tw->pool || g_testTorn == 0) return;
	if (g_testTornFree == 3)
	{
		VirtualFree(g_tw->pool, 0, MEM_RELEASE); g_tw->pool = nullptr;
		return;
	}
	unsigned char* np = (unsigned char*)VirtualAlloc(nullptr, TEST_POOL_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!np) return;
	memcpy(np, g_tw->pool, TEST_POOL_BYTES);
	VirtualFree(g_tw->pool, 0, MEM_RELEASE);
	g_tw->pool = np;
	if (g_testTornFree == 2) g_tw->latePool = np;
	else Put32(g_tw->grid, VPCUT_G_POOL, (uint32_t)(uintptr_t)np);
	if (g_testTorn > 0) g_testTorn--;
}

static void TestTornFreeLate()
{
	if (g_tw && g_tw->latePool) { Put32(g_tw->grid, VPCUT_G_POOL, (uint32_t)(uintptr_t)g_tw->latePool); g_tw->latePool = nullptr; }
}

static void TestChanges()
{
	if (!g_tw || !g_tw->pool) return;
	wchar_t f[MAX_PATH];
	_snwprintf_s(f, _TRUNCATE, L"%sshell_composite.cutchange", g_dir);
	if (GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES)
	{
		DeleteFileW(f);
		g_tw->pool[2 * VPSHELL_CUT_BLOCK + 0x21] ^= 1;
		CLOG("cut_tap: test: bit 0 of cell 0x21 of block 2 flipped to 0x%02X", g_tw->pool[2 * VPSHELL_CUT_BLOCK + 0x21]);
	}
	_snwprintf_s(f, _TRUNCATE, L"%sshell_composite.cutclear", g_dir);
	if (GetFileAttributesW(f) != INVALID_FILE_ATTRIBUTES)
	{
		DeleteFileW(f);
		g_tw->grid[VPCUT_G_MADE] ^= 1;
		CLOG("cut_tap: test: the grid's made byte is %u", g_tw->grid[VPCUT_G_MADE]);
	}
}

// ---- set-up ------------------------------------------------------------------------------------------------------------
static void ReadKeys()
{
	if (!g_dir[0])
	{
		HMODULE m = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ReadKeys, &m);
		GetModuleFileNameW(m, g_dir, MAX_PATH);
		wchar_t* sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0;
	}
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", g_dir);
	auto get = [&](const wchar_t* key, const wchar_t* env, wchar_t* out, DWORD n) {
		out[0] = 0;
		if (!GetEnvironmentVariableW(env, out, n)) GetPrivateProfileStringW(L"composite", key, L"", out, n, ini);
	};
	auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
	wchar_t v[96];
	get(L"cut_poll_ms", L"VPSHELL_CUT_POLL_MS", v, 96); if (v[0]) g_pollMs = (uint32_t)clamp(_wtoi(v), 1, 600000);
	get(L"cut_settle_polls", L"VPSHELL_CUT_SETTLE_POLLS", v, 96); if (v[0]) g_settlePolls = (uint32_t)clamp(_wtoi(v), 1, 1000);
	get(L"cut_max_kb", L"VPSHELL_CUT_MAX_KB", v, 96); if (v[0]) g_maxBytes = (uint64_t)clamp(_wtoi(v), 1, 1 << 20) * 1024ull;
	get(L"cut_loud_polls", L"VPSHELL_CUT_LOUD_POLLS", v, 96); if (v[0]) g_loudPolls = (uint32_t)clamp(_wtoi(v), 1, 1000);
	get(L"cut_torn_loud_s", L"VPSHELL_CUT_TORN_LOUD_S", v, 96); if (v[0]) g_tornLoudS = (uint32_t)clamp(_wtoi(v), 1, 86400);
	get(L"cut_test", L"VPSHELL_CUT_TEST", v, 96);
	char a[96]; WideCharToMultiByte(CP_ACP, 0, v, -1, a, 96, nullptr, nullptr);
	g_test = T_NONE;
	for (int i = 1; i < T_COUNT; ++i) if (!_stricmp(a, kTestNames[i])) g_test = i;
	if (a[0] && !g_test) CLOG("cut_tap: cut_test=%s is not a known mode - ignored", a);
	get(L"cut_test_pattern", L"VPSHELL_CUT_TEST_PATTERN", v, 96);
	g_testPattern = v[0] ? _wtoi(v) : -1;
	get(L"cut_test_torn", L"VPSHELL_CUT_TEST_TORN", v, 96);
	g_testTorn = v[0] ? _wtoi(v) : 30;
	get(L"cut_test_torn_free", L"VPSHELL_CUT_TEST_TORN_FREE", v, 96);
	g_testTornFree = v[0] ? clamp(_wtoi(v), 0, 3) : 0;
	get(L"cut_test_build", L"VPSHELL_CUT_TEST_BUILD", v, 96);
	WideCharToMultiByte(CP_ACP, 0, v, -1, g_testBuild, sizeof(g_testBuild), nullptr, nullptr);
	get(L"cut_test_drop", L"VPSHELL_CUT_TEST_DROP", v, 96);
	WideCharToMultiByte(CP_ACP, 0, v, -1, g_testDrop, sizeof(g_testDrop), nullptr, nullptr);
}

// The build of the image at base (its header), every symbol the tap reads carried, every pattern matching: g_build set.
static bool CheckPatterns(uint32_t base, char* why, size_t whyN)
{
	const int build = ShellExeResolve(base, kCtNeed, (int)(sizeof(kCtNeed) / sizeof(kCtNeed[0])), why, whyN);
	if (build < 0 || !ShellExeCheck(build, base, kCtSigs, CT_NSIGS, why, whyN)) return false;
	g_build = build;
	return true;
}

// cut_test=patterns: a fake image of the build cut_test_build (default 4390913; "0": a header no build has) with every
// pattern's bytes at that build's addresses (pattern k broken), checked as the EXE; cut_test_drop names a symbol the
// table then does not carry.
static bool TestPatterns(char* why, size_t whyN)
{
	const bool forget = !strcmp(g_testBuild, "0");
	const int build = ShellExeBuildBySteam(g_testBuild[0] && !forget ? g_testBuild : "4390913");
	if (build < 0) { _snprintf_s(why, whyN, _TRUNCATE, "cut_test_build=%s names no known build", g_testBuild); return false; }
	if (g_testDrop[0] && !ShellExeTestDrop(g_testDrop)) { _snprintf_s(why, whyN, _TRUNCATE, "cut_test_drop=%s names no symbol", g_testDrop); return false; }
	const uint32_t size = ShellExeSpan(build, kCtSigs, CT_NSIGS);
	unsigned char* img = size ? (unsigned char*)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
	if (!img) { _snprintf_s(why, whyN, _TRUNCATE, "test image could not be made"); return false; }
	g_tw->image = img;
	const uint32_t base = (uint32_t)(uintptr_t)img;
	if (!ShellExeLay(build, base, size, kCtSigs, CT_NSIGS, g_testPattern)) { _snprintf_s(why, whyN, _TRUNCATE, "test image could not be laid out"); return false; }
	if (forget) ShellExeTestForgetBuild(base);
	CLOG("cut_tap: test image of %s at 0x%08X (%u bytes)%s", VpExeBuildLabel(build), base, size, forget ? ", its header changed to no known build" : "");
	return CheckPatterns(base, why, whyN);
}

// ---- the control block -----------------------------------------------------------------------------------------------
static bool OpenControl()
{
	if (g_blk) return true;
	wchar_t name[64]; _snwprintf_s(name, _TRUNCATE, VPSHELL_CUTTAP_CONTROL_FMT, GetCurrentProcessId());
	g_ctl = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, VPSHELL_CUTTAP_CONTROL_BYTES, name);
	if (!g_ctl) { Loud("CreateFileMapping('%ls') failed, error %lu - the tap cannot publish", name, GetLastError()); return false; }
	g_blk = (VPShellCutTapControl*)MapViewOfFile(g_ctl, FILE_MAP_WRITE, 0, 0, VPSHELL_CUTTAP_CONTROL_BYTES);
	if (!g_blk) { Loud("MapViewOfFile('%ls') failed, error %lu", name, GetLastError()); CloseHandle(g_ctl); g_ctl = nullptr; return false; }
	return true;
}

static void Publish()
{
	if (!OpenControl()) return;
	VPShellCutTapControl* t = g_blk;
	InterlockedIncrement((volatile LONG*)&t->seq);
	MemoryBarrier();
	const bool img = g_img != nullptr;
	t->magic = VPSHELL_CUT_MAGIC; t->version = VPSHELL_CUT_VERSION; t->headerBytes = sizeof(VPShellCutTapControl);
	t->state = g_state; t->failReason = g_fail; t->exePid = GetCurrentProcessId();
	t->generation = img ? g_gen : 0; t->loadSeq = img ? g_imgLoad : (uint32_t)g_envGridSeq;
	t->tilesX = img ? g_imgTilesX : 0; t->tilesY = img ? g_imgTilesY : 0; t->poolBlocks = img ? g_imgBlocks : 0;
	t->imageBytes = img ? g_imgBytes : 0; t->fnv1a64 = img ? g_imgFnv : 0; t->readyQpc = g_readyQpc;
	t->polls = g_polls; t->refusedReads = g_refused; t->lastPollMs = g_lastPollMs; t->maxPollMs = g_maxPollMs;
	wcsncpy_s(t->imageName, img ? g_imgName : L"", _TRUNCATE);
	if (g_state == VPCUT_FAILED || g_fail) strncpy_s(t->failText, g_why, _TRUNCATE); else t->failText[0] = 0;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&t->seq);
}

static void CloseImages()
{
	if (g_prevImg) { CloseHandle(g_prevImg); g_prevImg = nullptr; }
	if (g_img) { CloseHandle(g_img); g_img = nullptr; }
	g_imgName[0] = 0; g_imgBytes = 0; g_imgFnv = 0;
	g_havePublished = false; g_publishedHash = 0;
}

// forGood: the tap is off for the process (the published image goes: it may be another EXE's state than the server needs).
static void Fail(uint32_t fail, const char* why, bool forGood)
{
	g_fail = fail;
	if (why != g_why) _snprintf_s(g_why, _TRUNCATE, "%s", why);
	if (forGood)
	{
		CloseImages();
		g_state = VPCUT_FAILED;
		g_dead = true;
	}
	else if (!g_img) g_state = VPCUT_FAILED;   // nothing published yet: the reader has no grid (a READY later replaces it)
	Publish();
	Loud("FAILED (%u): %s%s", fail, g_why, forGood ? " - DISABLED for the life of the process" : " (this image; the last one stays)");
}

static void SetState(uint32_t st)
{
	if (g_state == st) return;
	g_state = st;
	if (st != VPCUT_FAILED) { g_fail = VPCUTF_NONE; g_why[0] = 0; }
	Publish();
	CLOG("cut_tap: state %s (generation %u, load %u)", st < 6 ? kStateNames[st] : "?", g_gen, (uint32_t)g_envGridSeq);
}

// ---- one poll ----------------------------------------------------------------------------------------------------------
static bool Grow(unsigned char** p, uint32_t* cap, uint32_t need)
{
	if (need <= *cap) return true;
	unsigned char* q = (unsigned char*)realloc(*p, need);
	if (!q) return false;
	*p = q; *cap = need;
	return true;
}

// Ticket 0690 (K28): a fault while copying the directory or the pool. While the grid grows after a huge load the EXE
// replaces the pool (capacity doubles, the data pointer moves; trees.md 14.2) and releases the old one: a copy that
// started at the old pointer faults. That is the race of a torn read, not a broken layout: the header is read again
// (and, while it still equals the first read, up to CT_FAULT_RECHECKS more times CT_FAULT_RECHECK_MS apart, for an EXE
// that releases the old block before it stores the new pointer); a changed header is a torn read (4), an unchanged one
// stays a fault (3, FAILED EXCEPTION for the process).
#define CT_FAULT_RECHECKS 3
#define CT_FAULT_RECHECK_MS 2
static uint32_t g_tornFaults;   // torn reads found after a fault (counted in g_torn too)
static bool g_tornFault;        // the last ReadGrid's 4 came after a fault
static bool HeaderMovedAfterFault(uintptr_t gridVa)
{
	for (int i = 0; i <= CT_FAULT_RECHECKS; ++i)
	{
		if (i) { TestTornFreeLate(); Sleep(CT_FAULT_RECHECK_MS); }
		bool fault = false;
		if (!SafeRead(gridVa, g_hdr2, sizeof(g_hdr2), &fault)) return false;   // the header itself: a fault stays a fault
		if (memcmp(g_hdr, g_hdr2, sizeof(g_hdr)) != 0) return true;
	}
	return false;
}

// 0 = a grid copied and checked (g_hdr, g_dirBuf, g_poolBuf), 1 = no grid (no map, not made), 2 = a check failed (why),
// 3 = a fault (why), 4 = a torn read: the header changed during the copy (why; also after a fault of the directory or
// the pool, ticket 0690)
static int ReadGrid(uintptr_t gridVa, uintptr_t hmPtrVa, char* why, size_t whyN)
{
	bool fault = false;
	uint32_t hm = 0;
	g_tornFault = false;
	if (!SafeRead(hmPtrVa, &hm, 4, &fault)) { _snprintf_s(why, whyN, _TRUNCATE, "the GridHeightmap pointer is not readable"); return fault ? 3 : 2; }
	if (!hm) return 1;
	if (!SafeRead(gridVa, g_hdr, sizeof(g_hdr), &fault)) { _snprintf_s(why, whyN, _TRUNCATE, "the grid's header is not readable"); return fault ? 3 : 2; }
	if (g_hdr[VPCUT_G_MADE] != 1) return 1;
	const uint32_t w = U32(g_hdr, VPCUT_G_WIDTH), h = U32(g_hdr, VPCUT_G_HEIGHT), tx = U32(g_hdr, VPCUT_G_TILESX), ty = U32(g_hdr, VPCUT_G_TILESY);
	const uint32_t dir = U32(g_hdr, VPCUT_G_DIR), used = U32(g_hdr, VPCUT_G_USED), pool = U32(g_hdr, VPCUT_G_POOL);
	const uint32_t blocks = U32(g_hdr, VPCUT_G_BLOCKS), cap = U32(g_hdr, VPCUT_G_CAPACITY);
	if (!w || !h || (w & 15) || (h & 15) || tx != w / 16 || ty != h / 16 || tx > VPSHELL_CUT_MAX_TILES || ty > VPSHELL_CUT_MAX_TILES)
	{
		_snprintf_s(why, whyN, _TRUNCATE, "grid size %u x %u cells, %u x %u tiles (multiples of 16, tiles = cells / 16, at most %u)",
			w, h, tx, ty, VPSHELL_CUT_MAX_TILES);
		return 2;
	}
	const uint32_t tiles = tx * ty;
	if (used > blocks || blocks > tiles || blocks > cap || !dir || (blocks && !pool))
	{
		_snprintf_s(why, whyN, _TRUNCATE, "pool: %u in use, %u blocks, capacity %u, %u tiles, directory 0x%08X, pool 0x%08X",
			used, blocks, cap, tiles, dir, pool);
		return 2;
	}
	unsigned char hmb[8], hmo[8];
	if (!SafeRead((uintptr_t)hm + CT_HM_SAMPLES_X, hmb, 8, &fault) || !SafeRead((uintptr_t)hm + CT_HM_OX, hmo, 8, &fault))
	{
		_snprintf_s(why, whyN, _TRUNCATE, "the GridHeightmap at 0x%08X is not readable", hm);
		return fault ? 3 : 2;
	}
	g_samplesX = U32(hmb, 0); g_samplesY = U32(hmb, 4);
	memcpy(&g_ox, hmo, 4); memcpy(&g_oy, hmo + 4, 4);
	if ((int32_t)g_samplesX <= 0 || (int32_t)g_samplesY <= 0 || RoundUp16(g_samplesX >> 1) != w || RoundUp16(g_samplesY >> 1) != h)
	{
		_snprintf_s(why, whyN, _TRUNCATE, "GridHeightmap samples %u x %u do not give the grid's %u x %u cells", g_samplesX, g_samplesY, w, h);
		return 2;
	}
	if (!isfinite(g_ox) || !isfinite(g_oy)) { _snprintf_s(why, whyN, _TRUNCATE, "the origin (%g, %g) is not finite", g_ox, g_oy); return 2; }
	g_dirBytes = tiles * VPSHELL_CUT_DIR_ENTRY;
	g_poolBytes = blocks * VPSHELL_CUT_BLOCK;
	if (!Grow(&g_dirBuf, &g_dirCap, g_dirBytes) || !Grow(&g_poolBuf, &g_poolCap, g_poolBytes ? g_poolBytes : 1))
	{
		_snprintf_s(why, whyN, _TRUNCATE, "no memory for %u + %u bytes", g_dirBytes, g_poolBytes);
		return 2;
	}
	if (!SafeRead(dir, g_dirBuf, g_dirBytes, &fault))
	{
		if (fault && HeaderMovedAfterFault(gridVa))
		{
			g_tornFaults++; g_tornFault = true;
			_snprintf_s(why, whyN, _TRUNCATE, "the grid's header changed during the copy (a torn read; the directory at 0x%08X faulted)", dir);
			return 4;
		}
		_snprintf_s(why, whyN, _TRUNCATE, "the directory at 0x%08X is not readable", dir);
		return fault ? 3 : 2;
	}
	TestTornFree();   // test only: the "EXE" replaces the pool now (cut_test=torn with cut_test_torn_free)
	if (g_poolBytes && !SafeRead(pool, g_poolBuf, g_poolBytes, &fault))
	{
		if (fault && HeaderMovedAfterFault(gridVa))
		{
			g_tornFaults++; g_tornFault = true;
			_snprintf_s(why, whyN, _TRUNCATE, "the grid's header changed during the copy (a torn read; the pool at 0x%08X (%u bytes) faulted)",
				pool, g_poolBytes);
			return 4;
		}
		_snprintf_s(why, whyN, _TRUNCATE, "the pool at 0x%08X (%u bytes) is not readable", pool, g_poolBytes);
		return fault ? 3 : 2;
	}
	// test torn: the "EXE" writes the free head during the copy and back after it (a stamp taking a block, then the next)
	const bool tear = g_test == T_TORN && g_tw && g_testTorn != 0 && !g_testTornFree;
	if (tear) g_tw->grid[VPCUT_G_FREE] ^= 1;
	const bool hdrRead = SafeRead(gridVa, g_hdr2, sizeof(g_hdr2), &fault);
	if (tear) { g_tw->grid[VPCUT_G_FREE] ^= 1; if (g_testTorn > 0) g_testTorn--; }
	if (!hdrRead) { _snprintf_s(why, whyN, _TRUNCATE, "the grid's header is not readable"); return fault ? 3 : 2; }
	if (memcmp(g_hdr, g_hdr2, sizeof(g_hdr)) != 0)
	{
		_snprintf_s(why, whyN, _TRUNCATE, "the grid's header changed during the copy (a torn read)");
		return 4;
	}
	uint32_t pooled = 0;
	for (uint32_t t = 0; t < tiles; ++t)
	{
		const unsigned char* e = g_dirBuf + t * VPSHELL_CUT_DIR_ENTRY;
		const uint16_t st = U16(e, 0);
		if (st == 0)
		{
			const uint32_t k = U32(e, 4);
			if (k >= blocks) { _snprintf_s(why, whyN, _TRUNCATE, "tile %u: pool index %u of %u blocks", t, k, blocks); return 2; }
			pooled++;
		}
		else if (st != 1) { _snprintf_s(why, whyN, _TRUNCATE, "tile %u: state %u (0 or 1 expected)", t, st); return 2; }
	}
	if (pooled != used) { _snprintf_s(why, whyN, _TRUNCATE, "%u pooled tiles, the grid says %u blocks in use", pooled, used); return 2; }
	g_pooled = pooled;
	return 0;
}

static uint64_t CopyHash(uint32_t load)
{
	uint64_t h = VPCutFnv(VPCUT_FNV_INIT, g_hdr + VPCUT_G_WIDTH, 16);   // sizes and tiles (not the pointers)
	h = VPCutFnv(h, &g_samplesX, 4); h = VPCutFnv(h, &g_samplesY, 4); h = VPCutFnv(h, &g_ox, 4); h = VPCutFnv(h, &g_oy, 4);
	h = VPCutFnv(h, g_dirBuf, g_dirBytes);
	h = VPCutFnv(h, g_poolBuf, g_poolBytes);
	return VPCutFnv(h, &load, 4);
}

// The image of the current copy into a new section; READY. Returns false on a failure (FAILED for this image, the last
// image stays published).
static bool PublishImage(uint32_t load)
{
	VPShellCutImage hd = {};
	hd.magic = VPSHELL_CUT_MAGIC; hd.version = VPSHELL_CUT_VERSION; hd.headerBytes = sizeof(VPShellCutImage);
	hd.generation = g_gen + 1; hd.loadSeq = load; hd.exePid = GetCurrentProcessId();
	hd.widthCells = U32(g_hdr, VPCUT_G_WIDTH); hd.heightCells = U32(g_hdr, VPCUT_G_HEIGHT);
	hd.tilesX = U32(g_hdr, VPCUT_G_TILESX); hd.tilesY = U32(g_hdr, VPCUT_G_TILESY);
	hd.samplesX = g_samplesX; hd.samplesY = g_samplesY; hd.ox = g_ox; hd.oy = g_oy;
	hd.poolBlocks = U32(g_hdr, VPCUT_G_BLOCKS); hd.pooledTiles = g_pooled;
	hd.dirOffset = (uint32_t)((sizeof(VPShellCutImage) + 15) & ~15u); hd.dirBytes = g_dirBytes;
	hd.poolOffset = (hd.dirOffset + g_dirBytes + 15) & ~15u; hd.poolBytes = g_poolBytes;
	hd.imageBytes = (uint64_t)hd.poolOffset + g_poolBytes;
	memcpy(hd.gridHeader, g_hdr, VPSHELL_CUT_GRID_HDR);
	// information: cells with bits, counted over the grid as the lookup reads it
	uint32_t tree = 0, set = 0;
	for (uint32_t t = 0; t < hd.tilesX * hd.tilesY; ++t)
	{
		const unsigned char* e = g_dirBuf + t * VPSHELL_CUT_DIR_ENTRY;
		if (U16(e, 0)) { const unsigned char b = e[4]; if (b) set += 256; if (b & 7) tree += 256; }
		else
		{
			const unsigned char* blk = g_poolBuf + (size_t)U32(e, 4) * VPSHELL_CUT_BLOCK;
			for (uint32_t i = 0; i < VPSHELL_CUT_BLOCK; ++i) { if (blk[i]) set++; if (blk[i] & 7) tree++; }
		}
	}
	hd.treeCells = tree; hd.setCells = set;
	if (hd.imageBytes > g_maxBytes)
	{
		char w[200];
		_snprintf_s(w, _TRUNCATE, "image of %llu bytes, the limit is %llu (cut_max_kb)", (unsigned long long)hd.imageBytes, (unsigned long long)g_maxBytes);
		Fail(VPCUTF_TOO_BIG, w, false);
		return false;
	}
	unsigned char* img = (unsigned char*)calloc(1, (size_t)hd.imageBytes);
	if (!img) { Fail(VPCUTF_TOO_BIG, "out of memory for the image", false); return false; }
	memcpy(img, &hd, sizeof(hd));
	memcpy(img + hd.dirOffset, g_dirBuf, g_dirBytes);
	if (g_poolBytes) memcpy(img + hd.poolOffset, g_poolBuf, g_poolBytes);
	const uint64_t fnv = VPCutFnv(VPCUT_FNV_INIT, img, (size_t)hd.imageBytes);
	wchar_t name[64]; _snwprintf_s(name, _TRUNCATE, VPSHELL_CUT_IMAGE_FMT, GetCurrentProcessId(), hd.generation);
	const uint64_t secBytes = (hd.imageBytes + 0xFFFF) & ~0xFFFFull;
	HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, (DWORD)(secBytes >> 32), (DWORD)secBytes, name);
	const DWORD err = GetLastError();
	bool ok = sec && err != ERROR_ALREADY_EXISTS;
	char w[220] = "";
	if (!ok) _snprintf_s(w, _TRUNCATE, sec ? "section '%ls' already exists (another holder)" : "CreateFileMapping('%ls', %llu bytes) failed, error %lu",
		name, (unsigned long long)secBytes, err);
	uint32_t views = 0;
	for (uint64_t off = 0; ok && off < hd.imageBytes; off += CT_VIEW_BYTES)
	{
		const uint64_t n = hd.imageBytes - off < CT_VIEW_BYTES ? hd.imageBytes - off : CT_VIEW_BYTES;
		void* v = MapViewOfFile(sec, FILE_MAP_WRITE, (DWORD)(off >> 32), (DWORD)off, (SIZE_T)n);
		if (!v) { _snprintf_s(w, _TRUNCATE, "MapViewOfFile at %llu failed, error %lu", (unsigned long long)off, GetLastError()); ok = false; break; }
		memcpy(v, img + off, (size_t)n);
		UnmapViewOfFile(v);
		views++;
	}
	free(img);
	if (!ok)
	{
		if (sec) CloseHandle(sec);
		Fail(VPCUTF_SECTION, w, false);
		return false;
	}
	if (g_prevImg) CloseHandle(g_prevImg);
	g_prevImg = g_img; g_img = sec;
	g_gen = hd.generation; g_imgLoad = load; g_imgBytes = hd.imageBytes; g_imgFnv = fnv;
	g_imgTilesX = hd.tilesX; g_imgTilesY = hd.tilesY; g_imgBlocks = hd.poolBlocks;
	wcsncpy_s(g_imgName, name, _TRUNCATE);
	g_readyQpc = Qpc();
	g_state = VPCUT_READY; g_fail = VPCUTF_NONE; g_why[0] = 0;
	Publish();
	CLOG("cut_tap: READY generation %u (load %u): %u x %u cells (%u x %u tiles), samples %u x %u, origin (%.4f, %.4f); %u pooled tiles "
		"of %u blocks; %u cells with bits 0-2, %u cells set; image %llu bytes in '%ls' (%u views), fnv1a64 %016llX; poll %.3f ms "
		"(max %.3f), %u polls, %u refused reads",
		g_gen, load, hd.widthCells, hd.heightCells, hd.tilesX, hd.tilesY, g_samplesX, g_samplesY, g_ox, g_oy, g_pooled, hd.poolBlocks,
		tree, set, (unsigned long long)g_imgBytes, g_imgName, views, (unsigned long long)fnv, g_lastPollMs, g_maxPollMs, g_polls, g_refused);
	return true;
}

static void LogAck()
{
	if (!g_blk || !g_gen) return;
	const uint32_t ack = g_blk->ackGeneration;
	if (ack && ack != g_ackLogged)
	{
		g_ackLogged = ack;
		CLOG("cut_tap: generation %u acked by pid %u, result %d", ack, g_blk->ackServerPid, (int)g_blk->ackResult);
	}
}

static void Poll()
{
	if (!g_init)
	{
		g_init = true;
		ReadKeys();
		LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcMs = 1000.0 / (double)f.QuadPart;
		CLOG("cut_tap: worker on (poll %u ms, settle %u polls, max %llu KB, alarm after %u polls of one failed check or %u s "
			"of torn reads%s%s)", g_pollMs, g_settlePolls, (unsigned long long)(g_maxBytes / 1024), g_loudPolls, g_tornLoudS,
			g_test ? ", TEST " : "", g_test ? kTestNames[g_test] : "");
		if (g_test) BuildTestWorld();
		OpenControl();
		Publish();
	}
	if (g_dead) return;
	if (!g_envEnabled)
	{
		if (g_state != VPCUT_OFF)
		{
			CloseImages();
			SetState(VPCUT_OFF);
			CLOG("cut_tap: cut_tap=0: OFF published, nothing read (the server draws with an empty mask)");
		}
		return;
	}
	char why[220] = "";
	if (!g_checked)
	{
		g_checked = true;
		if (g_test == T_PATTERNS) g_patternsOk = g_tw && TestPatterns(why, sizeof(why));
		else if (g_test) g_patternsOk = g_tw != nullptr;
		else if (!g_envBase || !g_envValidated) { g_patternsOk = false; _snprintf_s(why, _TRUNCATE, "the composite did not validate the EXE"); }
		else g_patternsOk = CheckPatterns((uint32_t)g_envBase, why, sizeof(why));
		if (!g_patternsOk)
		{
			char w[260]; _snprintf_s(w, _TRUNCATE, "refused: %s", why);
			Fail(VPCUTF_PATTERN, w, true);
			return;
		}
		CLOG("cut_tap: %d byte patterns match%s", CT_NSIGS, g_test ? " (test world)" : "");
	}
	if (g_test == T_PATTERNS) return;   // the check was the test
	if (g_test) TestChanges();
	LogAck();
	const uint32_t load = (uint32_t)g_envGridSeq;
	if (load != g_load)
	{
		if (g_load != 0xFFFFFFFFu) CLOG("cut_tap: load %u -> %u", g_load, load);
		g_load = load; g_samePolls = 0; g_pendingHash = 0; g_lastReason[0] = 0; g_tornStreak = 0;
	}
	if (!g_envGridValid) { SetState(g_img ? g_state : VPCUT_NO_GRID); return; }
	const uintptr_t grid = g_test ? (uintptr_t)g_tw->grid : ShellExeAddr(g_build, VPX_cut_grid, (uint32_t)g_envBase);
	const uintptr_t hmPtr = g_test ? (uintptr_t)&g_tw->hmPtr : ShellExeAddr(g_build, VPX_cam_gridptr, (uint32_t)g_envBase);
	const int64_t q0 = Qpc();
	const int rc = ReadGrid(grid, hmPtr, why, sizeof(why));
	g_polls++;
	if (rc == 3)
	{
		char w[260]; _snprintf_s(w, _TRUNCATE, "a read of EXE memory faulted (%s): the tap is off for this process", why);
		Fail(VPCUTF_EXCEPTION, w, true);
		return;
	}
	if (rc == 1)
	{
		g_tornStreak = 0;
		if (g_state != VPCUT_NO_GRID) { CloseImages(); SetState(VPCUT_NO_GRID); }
		return;
	}
	if (rc == 4)
	{
		// A torn read: the EXE wrote the header while the tap copied (its stamps take pool blocks; after a huge load it
		// stamps for about 40 s, 0414's arm 7-sleep had 4 torn polls in a row in it). It says nothing about the layout,
		// so it has its own run: it ends any other reason's run (as any other reason would) and is LOUD only when every
		// poll for cut_torn_loud_s was torn, three times the whole growth: the EXE never settles (ticket 0500).
		g_refused++; g_torn++; g_samePolls = 0; g_pendingHash = 0;
		g_reasonStreak = 0; g_lastReason[0] = 0;
		if (++g_tornStreak == 1)
		{
			g_tornStartQpc = q0; g_tornLoud = false;
			CLOG("cut_tap: load %u: a check failed (read again at the next poll): %s", load, why);
		}
		// ticket 0690: a fault under a moved header (the EXE replaced the pool) is a torn read; each one gets a plain line
		// (the first 20, then every 100th), since it was a permanent failure before
		else if (g_tornFault && (g_tornFaults <= 20 || g_tornFaults % 100 == 0))
			CLOG("cut_tap: load %u: torn read %u of this run after a fault (%u in this process): %s", load, g_tornStreak, g_tornFaults, why);
		const double s = (double)(q0 - g_tornStartQpc) * g_qpcMs / 1000.0;
		if (!g_tornLoud && g_tornStreak >= g_loudPolls && s >= (double)g_tornLoudS)
		{
			g_tornLoud = true;
			Loud("load %u: every poll for %.1f s (%u in a row) was a torn read: the grid's header changed during the copy "
				"at each, the EXE does not settle (cut_torn_loud_s %u; nothing published since)", load, s, g_tornStreak,
				g_tornLoudS);
		}
		Publish();   // the counters (refusedReads, polls)
		return;
	}
	g_tornStreak = 0;
	if (rc == 2)
	{
		// A read that races the EXE's own writes (a stamp moving the pool or the counts) fails once and passes at the next
		// poll: a plain line. The same reason cut_loud_polls polls in a row is a layout the tap does not know: LOUD.
		g_refused++; g_samePolls = 0; g_pendingHash = 0;
		if (strcmp(why, g_lastReason)) { strncpy_s(g_lastReason, why, _TRUNCATE); g_reasonStreak = 0; g_reasonLoud = false; }
		if (++g_reasonStreak == 1) CLOG("cut_tap: load %u: a check failed (read again at the next poll): %s", load, why);
		if (g_reasonStreak >= g_loudPolls && !g_reasonLoud)
		{
			g_reasonLoud = true;
			Loud("load %u: a check failed %u polls in a row (read again at every poll, nothing published): %s", load, g_reasonStreak, why);
		}
		Publish();   // the counters (refusedReads, polls) for a reader that watches them
		return;
	}
	g_reasonStreak = 0; g_lastReason[0] = 0;
	const uint64_t h = CopyHash(load);
	g_lastPollMs = (float)((Qpc() - q0) * g_qpcMs);
	if (g_lastPollMs > g_maxPollMs) g_maxPollMs = g_lastPollMs;
	if (g_havePublished && h == g_publishedHash)
	{
		g_samePolls = 0; g_pendingHash = 0;
		if (g_polls % 10 == 0) Publish();   // the counters, every 10th unchanged poll
		return;
	}
	if (h != g_pendingHash) { g_pendingHash = h; g_samePolls = 1; } else g_samePolls++;
	if (g_samePolls < g_settlePolls) return;
	if (PublishImage(load)) { g_publishedHash = h; g_havePublished = true; g_samePolls = 0; g_pendingHash = 0; }
	else { g_publishedHash = h; g_havePublished = true; }   // a refused image is not retried until the grid changes again
}

static DWORD WINAPI Worker(LPVOID)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	do { Poll(); } while (WaitForSingleObject(g_stop, g_pollMs) == WAIT_TIMEOUT);
	Publish();   // the final counters
	CLOG("cut_tap: worker stopped: %u polls, %u refused reads (%u torn), generation %u; %u torn reads after a fault (0690)",
		g_polls, g_refused, g_torn, g_gen, g_tornFaults);
	return 0;
}

void CutTap_OnPresent(const CutTapEnv& env)
{
	g_log = env.log;
	InterlockedExchange(&g_envBase, (LONG)env.exeBase);
	InterlockedExchange(&g_envValidated, (LONG)env.exeValidated);
	InterlockedExchange(&g_envGridSeq, (LONG)env.gridSeq);
	InterlockedExchange(&g_envGridValid, (LONG)env.gridValid);
	InterlockedExchange(&g_envEnabled, env.enabled ? 1 : 0);
	if (g_workerStarted) return;
	if (!env.enabled)
	{
		if (!g_offPublished)   // no worker: the render thread publishes OFF once
		{
			g_offPublished = true;
			g_state = VPCUT_OFF;
			Publish();
			CLOG("cut_tap: cut_tap=0: OFF published, nothing read (the server draws with an empty mask)");
		}
		return;
	}
	InterlockedExchange(&g_workerStarted, 1);
	g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	g_worker = g_stop ? CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr) : nullptr;
	if (!g_worker) Loud("the worker thread could not be started (error %lu): nothing is published", GetLastError());
}

void CutTap_Shutdown(const char* why)
{
	// Nothing of the tap depends on the device: the worker and the published image stay.
	if (g_workerStarted) CLOG("cut_tap: composite resources released (%s): the worker goes on", why ? why : "?");
}

void CutTap_StopForTest(uint32_t ms)
{
	if (!g_worker) return;
	SetEvent(g_stop);
	WaitForSingleObject(g_worker, ms);
	CloseHandle(g_worker); g_worker = nullptr;
	FreeTestWorld();
}
