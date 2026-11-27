// forest_tap.cpp - the forest tap inside shell_composite.dll (ticket 0420; contract and keys: forest_tap.h).
// Written 2026-11-16 for this repository (GPL-3.0-or-later). What the EXE's Forest system is and why each byte pattern
// below is checked: research/05-graphics-composition/trees.md sections 2.2, 2.3, 2.5 and 7 (behaviour in own words,
// D83; nothing of the EXE is in this file but the patterns' few bytes, as in decal_tap.cpp and ice_tap.cpp).
//
// What it does: every forest_poll_ms while a map is loaded, one guarded read of the Forest object's header (+0x78..+0xA3
// and +0x128..+0x13F). While the ready flag (+0x13C, set when the game start made the layouts) is 0 nothing else is read.
// Then the layouts (the vector at +0x98: count +0x9C, each entry {first, count, capacity} of 28-byte trees) and the
// record list (an indexed list: records of 0x8C bytes at +0x80, first index +0x90, a record's next index at +0x00, the
// end 0x0FFFFFFF, the length at +0x8C) are read and checked (layout count 1..64, trees per layout 0..4096, a record's
// layout index below the layout count, its kind 1..4, the walk exactly as long as the list says). A set that differs from
// the last file's and stays the same for forest_settle_polls polls is written as one file (below). A failed check is a
// torn read or a layout the tap does not know: read again at the next poll; LOUD once per reason and load.
//
// The file (little-endian), <outDir>forest_tap\forest_<pid>_<load>_<generation>.bin:
//   header, 80 bytes: u32 magic 'VPFT' (0x54465056), version 1, headerBytes 80, pid, load (the camera block's gridSeq),
//     generation (per process, from 1), layoutCount, recordCount, randomTrees (+0x128), griddedTrees (+0x12C), builtCount,
//     treeBytes 28, recordBytes 32, layoutsOffset, recordsOffset, fileBytes; u64 FNV-1a 64 of the bytes after the
//     header; u64 the write time in ms since 1970 (UTC)
//   layouts at layoutsOffset: per layout u32 treeCount, then treeCount x 28 bytes as the EXE holds them (7 floats:
//     +0x00..+0x08 the tree's offset from the plot in the units HexToWorld takes, +0x0C 1.0, +0x10/+0x14 its point in
//     the 96 x 96 placement square, +0x18 its size 0.5-1.0; trees.md 2.3 and 7)
//   records at recordsOffset, in list order, 32 bytes each: u32 index (the record's slot), u32 layout, i32 the three
//     cube coordinates (+0x7C, +0x80, +0x84), u32 kind (+0x88: 1 forest, 2 jungle, 3 / 4 the same on the flag's plots,
//     very likely hills), u32 built (+0x08: the plot's instance blocks and shadow decal were built, i.e. it was on
//     screen), u32 decalCount (+0x2C: the shadow decal handles the record holds)
//
// Safety: every read of EXE memory goes through SafeRead (bounds + SEH): a fault disables the tap for the process
// (LOUD). A failed pattern check, an EXE build the patch does not know, a symbol its table does not carry, or an EXE the
// composite did not validate, refuses the EXE for good (LOUD). It calls no EXE function and writes nothing into the EXE.
// Addresses: the EXE build table (epic 0590, D202; shell_exe.h): forest_system and the pattern sites below, by name.
//
// Test seam (as decal_test): forest_test=<mode> (ini or VPSHELL_FOREST_TEST) builds a synthetic Forest object in this
// DLL's memory and runs the same code on it (BuildTestWorld; shell/tests/test_forest_tap.py recomputes it). A file
// `shell_composite.forestchange` beside the DLL flips the built flag of the test world's second record (deleted when
// seen). Modes: ok, notready, badcount (1,000 layouts), badkind (a record of kind 9), badindex (a layout index past the
// count), cycle (the list loops), short (the list is one shorter than its length field), fault (the record array is
// unmapped memory), patterns (the real pattern check on a fake image of the build forest_test_build=<steam build>
// (default 4390913; "0": a header no build has) holding every pattern, one of them broken by forest_test_pattern=<k>,
// -1 = none broken; forest_test_drop=<symbol> makes that symbol not carried).
#include "forest_tap.h"
#include "shell_exe.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---- the EXE (DX11; addresses from the build table, epic 0590: shell_exe.h) ------------------------------------------------
// forest_system: Forest_System, a static object.
#define FT_HDR_A 0x78u             // +0x78..+0xA3: the record list (+0x78) and the layout vector (+0x98)
#define FT_HDR_A_BYTES 0x2Cu
#define FT_HDR_B 0x128u            // +0x128..+0x13F: tree counts and the ready flag
#define FT_HDR_B_BYTES 0x18u
#define FT_REC 0x8Cu
#define FT_TREE 28u
#define FT_NIL 0x0FFFFFFFu
#define FT_MAX_LAYOUTS 64u
#define FT_MAX_TREES 4096u
#define FT_MAX_RECORDS 65536u
#define FT_MAX_INDEX 0x100000u
#define FT_FILE_HDR 80u
#define FT_FILE_REC 32u
#define FT_MAGIC 0x54465056u       // 'VPFT'
#define FT_VERSION 1u

// Byte patterns checked before the first read of the real EXE (R(name) = 4 bytes holding the symbol's address in the
// running build; gen_table.py checks every row on both builds, exebuild/patterns.txt). "The per-frame update" and "the
// layouts at game start" are the research's names (trees.md); the addresses are the build table's.
#define X(sym, name, text) { sym, name, text },
static const ShellExePat kFtSigs[] = {
	X(VPX_forest_update_call, "UpdateWorldCamera: ECX = Forest_System for the per-frame update", "B9 R(forest_system) E8 F0 E0 D3 FF")
	X(VPX_forest_ready, "the per-frame update: the ready flag +0x13C", "80 BE 3C 01 00 00 00")
	X(VPX_forest_first, "the per-frame update: the record list's first index +0x90, the end marker (all ones but the top nibble)", "8B BE 90 00 00 00 89 7C 24 14 81 FF FF FF FF 0F")
	X(VPX_forest_records, "the per-frame update: records at +0x80, 0x8C bytes each, cube coordinates +0x7C/+0x80/+0x84", "8B 86 80 00 00 00 69 FF 8C 00 00 00 F3 0F 2A 44 38 7C F3 0F 2A 8C 38 80 00 00 00 F3 0F 2A 94 38 84 00 00 00")
	X(VPX_forest_built, "the per-frame update: the built flag record+0x08", "80 7C 3A 08 00")
	X(VPX_forest_kind, "the per-frame update: the kind record+0x88 (1 or 3: forest)", "8B 4C 24 14 8B 81 88 00 00 00 83 F8 01 74 05 83 F8 03")
	X(VPX_forest_trees, "the per-frame update: trees per plot from +0x12C and +0x128", "8B 8E 2C 01 00 00 03 8E 28 01 00 00")
	X(VPX_forest_layout_idx, "the per-frame update: the layout index record+0x78 into the layouts +0x98, 12 bytes each", "8B 45 78 2B CA D1 E9 03 CA 8B 96 98 00 00 00 C1 E9 06 51 8D 0C 40 8D 04 8A")
	X(VPX_forest_next, "the per-frame update: a record's next index at +0x00", "8B 96 80 00 00 00 8B 0C 17 89 4C 24 1C")
	X(VPX_forest_layout_count, "the layouts at game start: the layout count +0x9C", "8B 93 9C 00 00 00")
	X(VPX_forest_layout, "the layouts at game start: a layout {first, count, capacity} of 28-byte trees", "8B 0E 8B 2C 0F 8D 04 0F 8B 48 04 8D 14 CD 00 00 00 00 2B D1")
	X(VPX_forest_tree, "one layout: a tree is 7 floats, +0x0C = 1.0, square point +0x10/+0x14, size +0x18", "F3 0F 10 44 24 38 F3 0F 11 00 F3 0F 10 44 24 3C F3 0F 11 40 04 F3 0F 10 44 24 40 F3 0F 11 40 08 F3 0F 10 05 R(forest_k_one) F3 0F 11 40 0C F3 0F 10 44 24 28 F3 0F 11 40 10 F3 0F 10 44 24 2C F3 0F 11 40 14 F3 0F 10 44 24 10 F3 0F 11 40 18")
};
#undef X
#define FT_NSIGS ((int)(sizeof(kFtSigs) / sizeof(kFtSigs[0])))
static const int kFtNeed[] = { VPX_forest_system };   // the symbols read besides the pattern sites
static int g_build = kVpExeBuildUnknown;               // the EXE build the patterns matched on

// ---- state ------------------------------------------------------------------------------------------------------------
struct Rec { uint32_t index, layout; int32_t c[3]; uint32_t kind, built, decals; };

static bool g_init, g_dead, g_checked, g_patternsOk;
static void (*g_log)(const char*, ...);
static uint32_t g_logs;
static char g_lastLoud[256]; static uint32_t g_loudRepeat;
static wchar_t g_dir[MAX_PATH];
static uint32_t g_pollMs = 500, g_settlePolls = 2, g_minWriteMs = 5000, g_maxFiles = 200;
static uint64_t g_lastPoll, g_lastWrite;
static uint32_t g_load = 0xFFFFFFFFu;
static uint64_t g_pendingHash, g_writtenHash; static uint32_t g_samePolls;
static uint32_t g_generation, g_files, g_refused, g_polls;
static bool g_notReadyLogged, g_maxLogged;
static char g_lastReason[160];
static unsigned char* g_lay; static uint32_t g_layBytes, g_layCap;   // the layouts as written to the file
static Rec* g_rec; static uint32_t g_nRec, g_recCap;
static uint32_t g_layoutCount, g_treeTotal, g_random, g_gridded;
static double g_pollCostMax, g_pollCostSum; static uint32_t g_pollCostN;

#define FLOG(...) do { if (g_log && g_logs < 2000) { g_logs++; g_log(__VA_ARGS__); } } while (0)

static uint64_t NowMs() { return GetTickCount64(); }

static void Loud(const char* fmt, ...)
{
	char b[400];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, ap); va_end(ap);
	if (!strcmp(b, g_lastLoud) && (++g_loudRepeat % 1000) != 0) return;
	if (strcmp(b, g_lastLoud)) { strncpy_s(g_lastLoud, b, _TRUNCATE); g_loudRepeat = 0; }
	if (g_log) g_log("forest_tap: LOUD: %s", b);
}

// ---- guarded reads (decal_tap.cpp: a loop under SEH, never memcpy on a wild address) ------------------------------------
#if defined(__SANITIZE_ADDRESS__)
__declspec(no_sanitize_address)
#endif
static bool SafeRead(uintptr_t a, void* out, size_t n, bool* fault)
{
	if (a < 0x10000 || n == 0 || n > 0x800000 || a + n < a || (uint64_t)a + n > 0xFFFF0000ull) return false;
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
static void Put32(unsigned char* b, uint32_t off, uint32_t v) { memcpy(b + off, &v, 4); }
static void PutF(unsigned char* b, uint32_t off, float v) { memcpy(b + off, &v, 4); }

static uint64_t Fnv(uint64_t h, const void* p, size_t n)
{
	const unsigned char* c = (const unsigned char*)p;
	for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 0x100000001b3ull; }
	return h;
}

// ---- the synthetic EXE (test mode) ------------------------------------------------------------------------------------
// shell/tests/test_forest_tap.py recomputes it: 20 layouts, layout k holds 180 - (k % 3) trees, float j of tree i of
// layout k = k * 1000 + i + j * 0.125; a pool of 16 records, the list 3, 0, 7, 12, 5, 9, 1, 14, 2, 11 (10 records);
// the record in list position p has layout (p * 7) % 20, cube coordinates (p, -2 p, p), kind 1 + p % 4, built p % 2,
// decal count p % 3; random trees 180, gridded 0.
enum { T_NONE, T_OK, T_NOTREADY, T_BADCOUNT, T_BADKIND, T_BADINDEX, T_CYCLE, T_SHORT, T_FAULT, T_PATTERNS, T_COUNT };
static const char* kTestNames[T_COUNT] = { "", "ok", "notready", "badcount", "badkind", "badindex", "cycle", "short",
	"fault", "patterns" };
static const uint32_t kTestList[] = { 3, 0, 7, 12, 5, 9, 1, 14, 2, 11 };
#define TEST_LIST_N 10u
#define TEST_POOL 16u
#define TEST_LAYOUTS 20u
static int g_test; static int g_testPattern = -1;
static char g_testBuild[32], g_testDrop[96];   // forest_test=patterns: the fake image's build, a symbol made "not carried"
struct TestWorld { unsigned char obj[0x200]; unsigned char* layoutVec; unsigned char* trees[TEST_LAYOUTS]; unsigned char* pool;
	unsigned char* image; void* unmapped; };
static TestWorld* g_tw;

static void FreeTestWorld()
{
	if (!g_tw) return;
	for (uint32_t k = 0; k < TEST_LAYOUTS; ++k) free(g_tw->trees[k]);
	free(g_tw->layoutVec); free(g_tw->pool);
	if (g_tw->image) VirtualFree(g_tw->image, 0, MEM_RELEASE);
	free(g_tw); g_tw = nullptr;
}

static void BuildTestWorld()
{
	g_tw = (TestWorld*)calloc(1, sizeof(TestWorld));
	if (!g_tw) return;
	unsigned char* o = g_tw->obj;
	g_tw->layoutVec = (unsigned char*)calloc(TEST_LAYOUTS, 12);
	for (uint32_t k = 0; k < TEST_LAYOUTS; ++k)
	{
		const uint32_t n = 180 - (k % 3);
		g_tw->trees[k] = (unsigned char*)calloc(n + 4, FT_TREE);
		for (uint32_t i = 0; i < n; ++i)
			for (uint32_t j = 0; j < 7; ++j) PutF(g_tw->trees[k], i * FT_TREE + 4 * j, (float)(k * 1000 + i) + 0.125f * (float)j);
		Put32(g_tw->layoutVec, k * 12, (uint32_t)(uintptr_t)g_tw->trees[k]);
		Put32(g_tw->layoutVec, k * 12 + 4, n);
		Put32(g_tw->layoutVec, k * 12 + 8, n + 4);
	}
	g_tw->pool = (unsigned char*)calloc(TEST_POOL, FT_REC);
	for (uint32_t s = 0; s < TEST_POOL; ++s) Put32(g_tw->pool, s * FT_REC, FT_NIL);
	for (uint32_t p = 0; p < TEST_LIST_N; ++p)
	{
		unsigned char* r = g_tw->pool + kTestList[p] * FT_REC;
		Put32(r, 0, p + 1 < TEST_LIST_N ? kTestList[p + 1] : FT_NIL);
		Put32(r, 4, p ? kTestList[p - 1] : FT_NIL);
		r[8] = (unsigned char)(p % 2);
		Put32(r, 0x2C, p % 3);
		Put32(r, 0x78, (p * 7) % TEST_LAYOUTS);
		Put32(r, 0x7C, p); Put32(r, 0x80, (uint32_t)(-2 * (int32_t)p)); Put32(r, 0x84, p);
		Put32(r, 0x88, 1 + p % 4);
	}
	Put32(o, 0x80, (uint32_t)(uintptr_t)g_tw->pool);
	Put32(o, 0x84, TEST_POOL);
	Put32(o, 0x8C, TEST_LIST_N);
	Put32(o, 0x90, kTestList[0]);
	Put32(o, 0x94, kTestList[TEST_LIST_N - 1]);
	Put32(o, 0x98, (uint32_t)(uintptr_t)g_tw->layoutVec);
	Put32(o, 0x9C, TEST_LAYOUTS);
	Put32(o, 0xA0, TEST_LAYOUTS);
	Put32(o, 0x128, 180);
	Put32(o, 0x12C, 0);
	o[0x13C] = g_test == T_NOTREADY ? 0 : 1;
	if (g_test == T_BADCOUNT) Put32(o, 0x9C, 1000);
	if (g_test == T_BADKIND) Put32(g_tw->pool + kTestList[4] * FT_REC, 0x88, 9);
	if (g_test == T_BADINDEX) Put32(g_tw->pool + kTestList[2] * FT_REC, 0x78, TEST_LAYOUTS);
	if (g_test == T_CYCLE) Put32(g_tw->pool + kTestList[6] * FT_REC, 0, kTestList[1]);
	if (g_test == T_SHORT) Put32(o, 0x8C, TEST_LIST_N + 1);
	if (g_test == T_FAULT)
	{
		void* v = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);   // reserved, never committed: a read faults
		g_tw->unmapped = v;
		Put32(o, 0x80, (uint32_t)(uintptr_t)v);
	}
}

static void TestChange()
{
	if (!g_tw || !g_tw->pool) return;
	wchar_t f[MAX_PATH]; _snwprintf_s(f, _TRUNCATE, L"%sshell_composite.forestchange", g_dir);
	if (GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES) return;
	DeleteFileW(f);
	unsigned char* r = g_tw->pool + kTestList[1] * FT_REC;
	r[8] ^= 1;
	FLOG("forest_tap: test: the built flag of record %u flipped to %u", kTestList[1], r[8]);
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
	get(L"forest_poll_ms", L"VPSHELL_FOREST_POLL_MS", v, 96); if (v[0]) g_pollMs = (uint32_t)clamp(_wtoi(v), 0, 600000);
	get(L"forest_settle_polls", L"VPSHELL_FOREST_SETTLE_POLLS", v, 96); if (v[0]) g_settlePolls = (uint32_t)clamp(_wtoi(v), 1, 1000);
	get(L"forest_min_write_ms", L"VPSHELL_FOREST_MIN_WRITE_MS", v, 96); if (v[0]) g_minWriteMs = (uint32_t)clamp(_wtoi(v), 0, 3600000);
	get(L"forest_max_files", L"VPSHELL_FOREST_MAX_FILES", v, 96); if (v[0]) g_maxFiles = (uint32_t)clamp(_wtoi(v), 0, 100000);
	v[0] = 0;
	get(L"forest_test", L"VPSHELL_FOREST_TEST", v, 96);
	char a[96]; WideCharToMultiByte(CP_ACP, 0, v, -1, a, 96, nullptr, nullptr);
	g_test = T_NONE;
	for (int i = 1; i < T_COUNT; ++i) if (!_stricmp(a, kTestNames[i])) g_test = i;
	if (a[0] && !g_test) FLOG("forest_tap: forest_test=%s is not a known mode - ignored", a);
	get(L"forest_test_pattern", L"VPSHELL_FOREST_TEST_PATTERN", v, 96);
	g_testPattern = v[0] ? _wtoi(v) : -1;
	get(L"forest_test_build", L"VPSHELL_FOREST_TEST_BUILD", v, 96);
	WideCharToMultiByte(CP_ACP, 0, v, -1, g_testBuild, sizeof(g_testBuild), nullptr, nullptr);
	get(L"forest_test_drop", L"VPSHELL_FOREST_TEST_DROP", v, 96);
	WideCharToMultiByte(CP_ACP, 0, v, -1, g_testDrop, sizeof(g_testDrop), nullptr, nullptr);
}

// The build of the image at base (its header), every symbol the tap reads carried, every pattern matching: g_build set.
static bool CheckPatterns(uint32_t base, char* why, size_t whyN)
{
	const int build = ShellExeResolve(base, kFtNeed, (int)(sizeof(kFtNeed) / sizeof(kFtNeed[0])), why, whyN);
	if (build < 0 || !ShellExeCheck(build, base, kFtSigs, FT_NSIGS, why, whyN)) return false;
	g_build = build;
	return true;
}

// forest_test=patterns: a fake image of the build forest_test_build (default 4390913; "0": a header no build has) with every
// pattern's bytes at that build's addresses (pattern k broken), checked as the EXE; forest_test_drop names a symbol the
// table then does not carry.
static bool TestPatterns(char* why, size_t whyN)
{
	const bool forget = !strcmp(g_testBuild, "0");
	const int build = ShellExeBuildBySteam(g_testBuild[0] && !forget ? g_testBuild : "4390913");
	if (build < 0) { _snprintf_s(why, whyN, _TRUNCATE, "forest_test_build=%s names no known build", g_testBuild); return false; }
	if (g_testDrop[0] && !ShellExeTestDrop(g_testDrop)) { _snprintf_s(why, whyN, _TRUNCATE, "forest_test_drop=%s names no symbol", g_testDrop); return false; }
	const uint32_t size = ShellExeSpan(build, kFtSigs, FT_NSIGS);
	unsigned char* img = size ? (unsigned char*)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : nullptr;
	if (!img) { _snprintf_s(why, whyN, _TRUNCATE, "test image could not be made"); return false; }
	g_tw->image = img;
	const uint32_t base = (uint32_t)(uintptr_t)img;
	if (!ShellExeLay(build, base, size, kFtSigs, FT_NSIGS, g_testPattern)) { _snprintf_s(why, whyN, _TRUNCATE, "test image could not be laid out"); return false; }
	if (forget) ShellExeTestForgetBuild(base);
	FLOG("forest_tap: test image of %s at 0x%08X (%u bytes)%s", VpExeBuildLabel(build), base, size, forget ? ", its header changed to no known build" : "");
	return CheckPatterns(base, why, whyN);
}

// ---- one poll ----------------------------------------------------------------------------------------------------------
// 0 = a set read (g_lay, g_rec), 1 = not ready (nothing to read), 2 = a check failed (reason in why), 3 = a fault
static int ReadSet(uintptr_t forest, char* why, size_t whyN)
{
	bool fault = false;
	unsigned char a[FT_HDR_A_BYTES], b[FT_HDR_B_BYTES];
	if (!SafeRead(forest + FT_HDR_B, b, sizeof(b), &fault)) { _snprintf_s(why, whyN, _TRUNCATE, "the header +0x128 is not readable"); return fault ? 3 : 2; }
	if (!b[0x13C - FT_HDR_B]) return 1;
	if (!SafeRead(forest + FT_HDR_A, a, sizeof(a), &fault)) { _snprintf_s(why, whyN, _TRUNCATE, "the header +0x78 is not readable"); return fault ? 3 : 2; }
	const uint32_t recs = U32(a, 0x80 - FT_HDR_A), listLen = U32(a, 0x8C - FT_HDR_A), head = U32(a, 0x90 - FT_HDR_A);
	const uint32_t lays = U32(a, 0x98 - FT_HDR_A), nLay = U32(a, 0x9C - FT_HDR_A);
	g_random = U32(b, 0); g_gridded = U32(b, 4);
	if (nLay == 0 || nLay > FT_MAX_LAYOUTS) { _snprintf_s(why, whyN, _TRUNCATE, "layout count %u (1..%u expected)", nLay, FT_MAX_LAYOUTS); return 2; }
	if (listLen > FT_MAX_RECORDS) { _snprintf_s(why, whyN, _TRUNCATE, "record list length %u (at most %u)", listLen, FT_MAX_RECORDS); return 2; }
	// layouts
	unsigned char vec[FT_MAX_LAYOUTS * 12];
	if (!SafeRead(lays, vec, nLay * 12, &fault)) { _snprintf_s(why, whyN, _TRUNCATE, "the layout vector 0x%08X is not readable", lays); return fault ? 3 : 2; }
	uint32_t need = 0;
	for (uint32_t k = 0; k < nLay; ++k)
	{
		const uint32_t n = U32(vec, k * 12 + 4), cap = U32(vec, k * 12 + 8);
		if (n > FT_MAX_TREES || n > cap) { _snprintf_s(why, whyN, _TRUNCATE, "layout %u: %u trees, capacity %u", k, n, cap); return 2; }
		need += 4 + n * FT_TREE;
	}
	if (need > g_layCap)
	{
		unsigned char* p = (unsigned char*)realloc(g_lay, need);
		if (!p) { _snprintf_s(why, whyN, _TRUNCATE, "no memory for %u layout bytes", need); return 2; }
		g_lay = p; g_layCap = need;
	}
	uint32_t at = 0, trees = 0;
	for (uint32_t k = 0; k < nLay; ++k)
	{
		const uint32_t first = U32(vec, k * 12), n = U32(vec, k * 12 + 4);
		Put32(g_lay, at, n); at += 4;
		if (n && !SafeRead(first, g_lay + at, n * FT_TREE, &fault))
		{
			_snprintf_s(why, whyN, _TRUNCATE, "layout %u's trees at 0x%08X are not readable", k, first);
			return fault ? 3 : 2;
		}
		at += n * FT_TREE; trees += n;
	}
	g_layBytes = at; g_layoutCount = nLay; g_treeTotal = trees;
	// records
	if (listLen + 1 > g_recCap)
	{
		const uint32_t cap = listLen + 64;
		Rec* p = (Rec*)realloc(g_rec, cap * sizeof(Rec));
		if (!p) { _snprintf_s(why, whyN, _TRUNCATE, "no memory for %u records", cap); return 2; }
		g_rec = p; g_recCap = cap;
	}
	uint32_t n = 0, idx = head;
	unsigned char r[FT_REC];
	while (idx != FT_NIL)
	{
		if (n >= listLen) { _snprintf_s(why, whyN, _TRUNCATE, "the list is longer than its length %u (a loop or a torn read)", listLen); return 2; }
		if (idx >= FT_MAX_INDEX) { _snprintf_s(why, whyN, _TRUNCATE, "record index %u", idx); return 2; }
		if (!SafeRead(recs + (uintptr_t)idx * FT_REC, r, FT_REC, &fault))
		{
			_snprintf_s(why, whyN, _TRUNCATE, "record %u at 0x%08X is not readable", idx, (uint32_t)(recs + idx * FT_REC));
			return fault ? 3 : 2;
		}
		Rec& o = g_rec[n];
		o.index = idx; o.layout = U32(r, 0x78);
		o.c[0] = (int32_t)U32(r, 0x7C); o.c[1] = (int32_t)U32(r, 0x80); o.c[2] = (int32_t)U32(r, 0x84);
		o.kind = U32(r, 0x88); o.built = r[8] ? 1u : 0u; o.decals = U32(r, 0x2C);
		if (o.layout >= nLay) { _snprintf_s(why, whyN, _TRUNCATE, "record %u: layout index %u of %u", idx, o.layout, nLay); return 2; }
		if (o.kind < 1 || o.kind > 4) { _snprintf_s(why, whyN, _TRUNCATE, "record %u: kind %u (1..4 expected)", idx, o.kind); return 2; }
		++n;
		idx = U32(r, 0);
	}
	if (n != listLen) { _snprintf_s(why, whyN, _TRUNCATE, "the list holds %u records, its length says %u", n, listLen); return 2; }
	g_nRec = n;
	return 0;
}

static uint64_t SetHash()
{
	uint64_t h = Fnv(0xcbf29ce484222325ull, g_lay, g_layBytes);
	h = Fnv(h, &g_random, 4); h = Fnv(h, &g_gridded, 4);
	return Fnv(h, g_rec, (size_t)g_nRec * sizeof(Rec));
}

static bool WriteFile_(uint32_t pid, const VPShellCamera* cam, const wchar_t* outDir, uint64_t* bytesOut, wchar_t* nameOut, size_t nameN)
{
	const uint32_t layOff = FT_FILE_HDR, recOff = layOff + g_layBytes, total = recOff + g_nRec * FT_FILE_REC;
	unsigned char* f = (unsigned char*)calloc(1, total);
	if (!f) return false;
	uint32_t built = 0;
	memcpy(f + layOff, g_lay, g_layBytes);
	for (uint32_t i = 0; i < g_nRec; ++i)
	{
		const Rec& o = g_rec[i];
		unsigned char* d = f + recOff + i * FT_FILE_REC;
		Put32(d, 0, o.index); Put32(d, 4, o.layout);
		Put32(d, 8, (uint32_t)o.c[0]); Put32(d, 12, (uint32_t)o.c[1]); Put32(d, 16, (uint32_t)o.c[2]);
		Put32(d, 20, o.kind); Put32(d, 24, o.built); Put32(d, 28, o.decals);
		built += o.built;
	}
	const uint32_t gen = g_generation + 1;
	const uint32_t h[16] = { FT_MAGIC, FT_VERSION, FT_FILE_HDR, pid, cam->gridSeq, gen, g_layoutCount, g_nRec, g_random, g_gridded,
		built, FT_TREE, FT_FILE_REC, layOff, recOff, total };
	memcpy(f, h, sizeof(h));
	const uint64_t fnv = Fnv(0xcbf29ce484222325ull, f + FT_FILE_HDR, total - FT_FILE_HDR);
	FILETIME ft; GetSystemTimeAsFileTime(&ft);
	const uint64_t ms = ((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000ull;
	memcpy(f + 64, &fnv, 8); memcpy(f + 72, &ms, 8);
	wchar_t dir[MAX_PATH], tmp[MAX_PATH], fin[MAX_PATH];
	_snwprintf_s(dir, _TRUNCATE, L"%sforest_tap", outDir);
	CreateDirectoryW(dir, nullptr);
	_snwprintf_s(nameOut, nameN, _TRUNCATE, L"forest_%lu_%u_%u.bin", (unsigned long)pid, cam->gridSeq, gen);
	_snwprintf_s(fin, _TRUNCATE, L"%s\\%s", dir, nameOut);
	_snwprintf_s(tmp, _TRUNCATE, L"%s.tmp", fin);
	bool ok = false;
	HANDLE fh = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (fh != INVALID_HANDLE_VALUE)
	{
		DWORD w = 0;
		ok = ::WriteFile(fh, f, total, &w, nullptr) && w == total;
		CloseHandle(fh);
		ok = ok && MoveFileExW(tmp, fin, MOVEFILE_REPLACE_EXISTING);
		if (!ok) DeleteFileW(tmp);
	}
	free(f);
	if (ok) { g_generation = gen; *bytesOut = total; }
	return ok;
}

void ForestTap_OnPresent(const ForestTapEnv& env)
{
	if (!env.enabled || g_dead) return;
	g_log = env.log;
	if (!g_init)
	{
		g_init = true;
		ReadKeys();
		FLOG("forest_tap: on (poll %u ms, settle %u polls, min write %u ms, max files %u%s%s); files to %lsforest_tap",
			g_pollMs, g_settlePolls, g_minWriteMs, g_maxFiles, g_test ? ", TEST " : "", g_test ? kTestNames[g_test] : "",
			env.outDir ? env.outDir : L"?");
		if (g_test) BuildTestWorld();
	}
	if (!env.cam || !env.cam->gridValid || !env.outDir) return;
	const uint64_t now = NowMs();
	if (g_lastPoll && now - g_lastPoll < g_pollMs) return;
	g_lastPoll = now;
	char why[200] = "";
	if (!g_checked)
	{
		g_checked = true;
		if (g_test == T_PATTERNS) g_patternsOk = g_tw && TestPatterns(why, sizeof(why));
		else if (g_test) g_patternsOk = g_tw != nullptr;
		else if (!env.exeBase || !env.exeValidated) { g_patternsOk = false; _snprintf_s(why, _TRUNCATE, "the composite did not validate the EXE"); }
		else g_patternsOk = CheckPatterns(env.exeBase, why, sizeof(why));
		if (!g_patternsOk)
		{
			Loud("refused for good: %s", why);
			g_dead = true;
			return;
		}
		FLOG("forest_tap: %d byte patterns match%s", FT_NSIGS, g_test ? " (test world)" : "");
	}
	if (g_test == T_PATTERNS) return;   // the check was the test
	if (g_test) TestChange();
	if (env.cam->gridSeq != g_load)
	{
		if (g_load != 0xFFFFFFFFu) FLOG("forest_tap: load %u -> %u", g_load, env.cam->gridSeq);
		g_load = env.cam->gridSeq; g_writtenHash = 0; g_pendingHash = 0; g_samePolls = 0; g_notReadyLogged = false;
		g_lastReason[0] = 0;
	}
	const uintptr_t forest = g_test ? (uintptr_t)g_tw->obj : ShellExeAddr(g_build, VPX_forest_system, env.exeBase);
	LARGE_INTEGER q0, q1, fq; QueryPerformanceCounter(&q0);
	const int rc = ReadSet(forest, why, sizeof(why));
	QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&fq);
	const double ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)fq.QuadPart;
	g_polls++;
	if (rc == 0) { if (ms > g_pollCostMax) g_pollCostMax = ms; g_pollCostSum += ms; g_pollCostN++; }
	if (rc == 3)
	{
		Loud("a read of EXE memory faulted (%s): the tap is off for this process", why);
		g_dead = true;
		return;
	}
	if (rc == 1)
	{
		if (!g_notReadyLogged) { g_notReadyLogged = true; FLOG("forest_tap: load %u: the Forest system is not ready (no layouts; Forest off?)", g_load); }
		return;
	}
	if (rc == 2)
	{
		g_refused++; g_samePolls = 0; g_pendingHash = 0;
		if (strcmp(why, g_lastReason)) { strncpy_s(g_lastReason, why, _TRUNCATE); Loud("load %u: a check failed (read again at the next poll): %s", g_load, why); }
		return;
	}
	const uint64_t h = SetHash();
	if (h != g_pendingHash) { g_pendingHash = h; g_samePolls = 1; } else g_samePolls++;
	if (g_samePolls < g_settlePolls || h == g_writtenHash) return;
	if (g_lastWrite && now - g_lastWrite < g_minWriteMs) return;
	if (g_files >= g_maxFiles)
	{
		if (!g_maxLogged) { g_maxLogged = true; Loud("forest_max_files (%u) reached: no more files in this process", g_maxFiles); }
		return;
	}
	uint64_t bytes = 0; wchar_t name[96] = L"";
	if (!WriteFile_(GetCurrentProcessId(), env.cam, env.outDir, &bytes, name, 96))
	{
		Loud("load %u: the file could not be written (error %lu)", g_load, GetLastError());
		g_lastWrite = now;
		return;
	}
	g_files++; g_writtenHash = h; g_lastWrite = now;
	uint32_t built = 0, kinds[5] = {}, decals = 0;
	for (uint32_t i = 0; i < g_nRec; ++i) { built += g_rec[i].built; kinds[g_rec[i].kind]++; decals += g_rec[i].decals; }
	FLOG("forest_tap: load %u generation %u: %u layouts (%u trees; random %u, gridded %u), %u records (built %u, decal handles %u; "
		"kinds 1/2/3/4 %u/%u/%u/%u) -> %ls (%llu bytes); poll %.3f ms (max %.3f, mean %.3f over %u), %u refused",
		g_load, g_generation, g_layoutCount, g_treeTotal, g_random, g_gridded, g_nRec, built, decals, kinds[1], kinds[2], kinds[3],
		kinds[4], name, (unsigned long long)bytes, ms, g_pollCostMax, g_pollCostN ? g_pollCostSum / g_pollCostN : 0.0, g_pollCostN,
		g_refused);
}

void ForestTap_Shutdown(const char* why)
{
	// Nothing of the tap depends on the device. The test world is freed and built again at the next call (with the keys
	// read again and the patterns checked again); the load, the last file's set and the counters stay.
	if (!g_init) return;
	if (g_tw && g_tw->unmapped) VirtualFree(g_tw->unmapped, 0, MEM_RELEASE);
	FreeTestWorld();
	g_init = false; g_checked = false;
	FLOG("forest_tap: shutdown (%s): %u files, %u polls, %u refused", why ? why : "?", g_files, g_polls, g_refused);
}
