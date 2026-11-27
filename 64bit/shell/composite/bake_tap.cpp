// bake_tap.cpp - the tile-cache tap inside shell_composite.dll (epic 3200, issue 3204; decision D58 point 1a).
// Written 2026-11-11 for this repository (GPL-3.0-or-later).
// EXE addresses in the comments are build 4390913's (research labels); the code reads every address of the running build
// from the build table (ticket 4964, epic 0590: bake_* in exebuild/symbols/shell.tsv).
//
// What it does. The EXE bakes each terrain tile's colour caches (TerrainShader t0 = tile+0x198, t8 = tile+0x19C) on the
// GPU in 0x694A60. In the D33 configuration that bake runs once, at load, for the tiles of the start view only
// (research/05-graphics-composition/terrain-rendering-inventory.md section 5). At every Present of the target this file
//   1. finds the tile grid: TerrainSystem 0xD165E0 -> TR = [TS+0x75FA4]; grid TR+0x1ADC (TR+0x2318 tile pointers,
//      column-major, TR+0x2310 x TR+0x2314 tiles); per tile the cache handles +0x198/+0x19C, the state word +0x310,
//      the origin +0xDC/+0xE0 and the size +0xD4/+0xD8;
//   2. resolves a handle h through the EXE's view table 0x414C348 (12 bytes per handle, [0] = a D3D11 view of the
//      texture) after checking the object's vtable against the vtables of D3D11 objects this file created itself;
//   3. creates per tile two shared textures of the cache's format/size/mips (VPSHELL_BAKE_TEXTURE_FMT, NT handles),
//      copies the caches in with CopyResource on the EXE's immediate context, and marks a slot valid only when an event
//      query issued after the copy has completed;
//   4. describes every slot in the manifest section (VPSHELL_BAKE_SECTION_FMT) and publishes VPShellBakeTap.
// Copy on update: a full scan of the grid at load (spread over Presents), then a cheap per-Present check of the slots
// (handle, view pointer or state word changed => re-copy) and a rolling rescan of the grid for new caches.
//
// Safety. Every read of EXE memory goes through SafeRead (bounds check + SEH). An access violation disables the tap for
// the life of the process (state FAILED, VPBAKEF_EXCEPTION, logged). A layout that fails a bounds check fails the tap
// for this load (VPBAKEF_LAYOUT) and is retried when the camera block's gridSeq changes. Nothing of the EXE is written.
//
// Test seam (like camera_test): `bake_test=<mode>` in [composite] of shell_composite.ini (or VPSHELL_BAKE_TEST) feeds
// the same scan code a synthetic TerrainSystem / tile grid / view table built in this DLL's own memory with real test
// textures: ok, missing (a handle with no view), nullview, badvtbl (a view pointer whose vtable is not D3D11's),
// garbage (an unmapped tile pointer), hugecount (absurd tile counts), badtile (absurd tile fields), nulltr (no
// terrain yet). `bake_manifest=<name>` (test only) replaces the manifest section name. A file `shell_composite.bakeload`
// next to the DLL simulates a new load (loadSeq + 1) in test mode and is deleted when seen.
#include "bake_tap.h"
#include "exe_camera.h"
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

// ---- EXE layout (DX11 EXE; addresses are build-table symbols, ticket 4964: exebuild/symbols/shell.tsv bake_*) -----------
// bake_ts (TerrainSystem, a static object) and pxw_tex_views (texture handle -> {view, view2, ?}, 12 bytes per handle);
// VpExeVa(VPCamBuild(), VPX_bake_ts) etc. for the build the composite validated. Structure offsets are the same in both
// builds.
#define BT_TS_TR 0x75FA4u           // TerrainRenderer* (TR)
#define BT_VIEWTAB_MAX 0x3000u      // handles are < 0x3000 (disable-systems-and-masks.md, the texture loader)
#define BT_TR_GRID 0x1ADCu          // tile pointer grid (column-major)
#define BT_TR_TX 0x2310u
#define BT_TR_TY 0x2314u
#define BT_TR_N 0x2318u
#define BT_TR_BYTES 0x2340u
#define BT_T_W 0xD4u
#define BT_T_H 0xD8u
#define BT_T_SX 0xDCu
#define BT_T_SY 0xE0u
#define BT_T_C0 0x198u
#define BT_T_C1 0x19Cu
#define BT_T_STATE 0x310u
#define BT_T_BYTES 0x318u
#define BT_MAX_TILES_XY 1024u
#define BT_SCAN_PER_PRESENT 512u    // grid entries looked at per Present by the rolling scan (2048: 0.28 ms per Present live)
#define BT_COPY_BUDGET 24           // texture copies issued per Present at most
#define BT_COPY_MS_BUDGET 3.0       // ... or until this much CPU time went into copies
#define BT_QUERIES 8

// Byte patterns checked before the first read of the real EXE: X(symbol, description, bytes), R(symbol) = 4 bytes
// holding that symbol's VA in the running build, rebased (exe_camera.h; checked on both builds' golden EXEs by
// tools/exebuild/gen_table.py through exebuild/patterns.txt).
#define BT_SIGNATURES(X) \
	X(bake_ts_site, "TerrainSystem object (mov ecx, &TerrainSystem)", "B9 R(bake_ts)") \
	X(bake_tr_site, "TR = TS+0x75FA4, condition byte TS+0x75FDC", "56 8B F1 8B 8E A4 5F 07 00 85 C9 74 2C 80 BE DC 5F 07 00 00") \
	X(bake_prebake_tiles, "pre-bake: tile list TR+0x1AE4", "8B 8E E4 1A 00 00 8B 0C 81") \
	X(bake_prebake_flag, "pre-bake: tile+0x314 = 1", "C7 81 14 03 00 00 01 00 00 00") \
	X(bake_copy_198, "bake: copy into tile+0x198", "8B 95 98 01 00 00") \
	X(bake_copy_19c, "bake: copy into tile+0x19C", "8B 8D 9C 01 00 00") \
	X(bake_texcreate_viewtab, "texture creation: the view table, 12 bytes per handle", "89 8C 00 R(pxw_tex_views) 89 94 00 R(bake_viewtab_view2)")
static const VPExeSig kBtSigs[] = { BT_SIGNATURES(VPEXE_SIG_ENTRY) };

// ---- state -------------------------------------------------------------------------------------------------------------
struct Slot
{
	uintptr_t tile;          // tile object address (EXE memory or the synthetic table)
	uint32_t index, tx, ty;
	uint32_t h[2];           // cache handles seen at the last check
	uintptr_t view[2];       // view pointers seen at the last check
	uint32_t state;          // tile+0x310 at the last check
	ID3D11Texture2D* src[2]; // the EXE's textures (referenced)
	ID3D11Texture2D* dst[2]; // our shared copies
	HANDLE nt[2];            // their NT handles (keep the names alive)
	D3D11_TEXTURE2D_DESC desc[2];
	int pendingQuery;        // index of the query that completes the copy in flight, -1 = none
	uint32_t pendingState;   // VPBAKESLOT_* to publish when it completes
	uint32_t generation;
	bool used;
};

static Slot* g_slots;               // VPSHELL_BAKE_MAX_SLOTS
static uint32_t g_nSlots;
static int g_mode;                  // 0 = not initialised, 1 = active, 2 = off (disabled for good), 3 = failed this load
static uint32_t g_fail;
static bool g_patternsChecked, g_patternsOk;
static uint32_t g_loadSeq, g_lastGridSeq;
static bool g_haveLoad, g_reload;
static HANDLE g_mapH;
static VPShellBakeManifest* g_man;
static uint32_t g_capacity;
static wchar_t g_manName[64];
static uint32_t g_manSeq;
static uintptr_t g_refVt[3];        // vtables of a D3D11 texture, SRV and RTV made by this file
static ID3D11Query* g_q[BT_QUERIES];
static bool g_qBusy[BT_QUERIES];
static int64_t g_copies;
static double g_copyMs;
static uint32_t g_scanPos;          // rolling scan position in the grid
static uint32_t g_scanned;          // grid entries scanned since the load (first full pass = g_tilesN)
static bool g_fullScanDone;
static uint32_t g_tilesX, g_tilesY, g_tilesN;
static uintptr_t g_grid;
static int64_t g_loadQpc;
static double g_loadCopyMs, g_maxPresentCopyMs; static int g_loadCopies, g_reportedCopies = -1;
static uint32_t g_logs;
static void (*g_log)(const char*, ...);
static int g_test;                  // bake_test mode, 0 = real EXE
static bool g_testInit;
static wchar_t g_dir[MAX_PATH];
static double g_qpcMs;

enum { T_NONE, T_OK, T_MISSING, T_NULLVIEW, T_BADVTBL, T_GARBAGE, T_HUGECOUNT, T_BADTILE, T_NULLTR };
static const char* kTestNames[] = { "", "ok", "missing", "nullview", "badvtbl", "garbage", "hugecount", "badtile", "nulltr" };

#define BLOG(...) do { if (g_log && g_logs < 400) { g_logs++; g_log(__VA_ARGS__); } } while (0)

// ---- guarded reads -----------------------------------------------------------------------------------------------------
// No memcpy (ASan would check the shadow of a wild address first): a byte loop under SEH. Returns false on a bad range
// or a fault; *fault is set on a fault.
#if defined(__SANITIZE_ADDRESS__)
__declspec(no_sanitize_address)
#endif
static bool SafeRead(uintptr_t a, void* out, size_t n, bool* fault)
{
	if (a < 0x10000 || n == 0 || n > 0x100000 || a + n < a || a + n > 0xFFFF0000u) return false;
	__try
	{
		volatile const unsigned char* s = (volatile const unsigned char*)a;
		unsigned char* d = (unsigned char*)out;
		for (size_t i = 0; i < n; ++i) d[i] = s[i];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*fault = true;
		return false;
	}
}

static inline uint32_t U32(const unsigned char* b, uint32_t off) { uint32_t v; memcpy(&v, b + off, 4); return v; }

// The vtable call chain on an object whose vtable matched one of ours: QI(ID3D11View) -> GetResource -> QI(Texture2D).
// Under SEH: the object may have been released in between (a stale pointer of a correct type).
static ID3D11Texture2D* TextureOfView(uintptr_t obj, bool* fault)
{
	ID3D11Texture2D* tex = nullptr;
	__try
	{
		IUnknown* u = (IUnknown*)obj;
		ID3D11View* v = nullptr;
		if (SUCCEEDED(u->QueryInterface(__uuidof(ID3D11View), (void**)&v)) && v)
		{
			ID3D11Resource* r = nullptr;
			v->GetResource(&r);
			v->Release();
			if (r)
			{
				if (FAILED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) tex = nullptr;
				r->Release();
			}
		}
		else if (SUCCEEDED(u->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex) {}
		else tex = nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*fault = true;
		tex = nullptr;
	}
	return tex;
}

// ---- synthetic EXE (test mode) ------------------------------------------------------------------------------------------
struct TestWorld
{
	unsigned char ts[BT_TS_TR + 8];
	unsigned char tr[BT_TR_BYTES];
	uintptr_t grid[16];
	unsigned char tiles[16][BT_T_BYTES];
	uintptr_t viewtab[3 * 64];
	ID3D11Texture2D* tex[32];
	ID3D11ShaderResourceView* srv[32];
};
static TestWorld* g_tw;

static void Put32(unsigned char* b, uint32_t off, uint32_t v) { memcpy(b + off, &v, 4); }

static bool BuildTestWorld(ID3D11Device* dev)
{
	g_tw = (TestWorld*)calloc(1, sizeof(TestWorld));
	if (!g_tw) return false;
	TestWorld& w = *g_tw;
	// 4 x 2 tiles (column-major), 64 x 64 BC1 caches with 3 mips; tiles 0..5 have both caches, 6 has t0 only, 7 none
	const uint32_t TX = 4, TY = 2;
	for (int i = 0; i < 14; ++i)
	{
		D3D11_TEXTURE2D_DESC d = {};
		d.Width = d.Height = 64; d.MipLevels = 3; d.ArraySize = 1; d.Format = DXGI_FORMAT_BC1_UNORM;
		d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		static unsigned char blocks[16 * 16 * 8];
		for (int k = 0; k < 16 * 16; ++k)
		{
			// colour0 = colour1 = a per-texture RGB565 value, so every texel of texture i has one known colour
			uint16_t c = (uint16_t)(((i * 2) & 31) << 11 | ((i * 4 + 8) & 63) << 5 | ((31 - i) & 31));
			memcpy(blocks + k * 8, &c, 2); memcpy(blocks + k * 8 + 2, &c, 2); memset(blocks + k * 8 + 4, 0, 4);
		}
		D3D11_SUBRESOURCE_DATA sd[3] = { { blocks, 16 * 8, 0 }, { blocks, 8 * 8, 0 }, { blocks, 4 * 8, 0 } };
		if (FAILED(dev->CreateTexture2D(&d, sd, &w.tex[i])) || FAILED(dev->CreateShaderResourceView(w.tex[i], nullptr, &w.srv[i])))
			return false;
	}
	Put32(w.tr, BT_TR_TX, TX); Put32(w.tr, BT_TR_TY, TY); Put32(w.tr, BT_TR_N, TX * TY);
	uintptr_t grid = (uintptr_t)w.grid;
	memcpy(w.tr + BT_TR_GRID, &grid, 4);
	uintptr_t trp = (uintptr_t)w.tr;
	memcpy(w.ts + BT_TS_TR, &trp, 4);
	for (uint32_t i = 0; i < TX * TY; ++i)
	{
		unsigned char* t = w.tiles[i];
		uint32_t x = i / TY, y = i % TY;
		Put32(t, BT_T_W, 65); Put32(t, BT_T_H, 65); Put32(t, BT_T_SX, 64 * x); Put32(t, BT_T_SY, 64 * y);
		uint32_t h0 = i < 7 ? 0x10 + 2 * i : 0, h1 = i < 6 ? 0x11 + 2 * i : 0;
		Put32(t, BT_T_C0, h0); Put32(t, BT_T_C1, h1); Put32(t, BT_T_STATE, i < 6 ? 1 : 3);
		w.grid[i] = (uintptr_t)t;
		if (h0) w.viewtab[3 * h0] = (uintptr_t)w.srv[2 * i];
		if (h1) w.viewtab[3 * h1] = (uintptr_t)w.srv[2 * i + 1];
	}
	// hostile variants
	switch (g_test)
	{
	case T_MISSING: Put32(w.tiles[1], BT_T_C1, 0x3F); break;                     // a handle whose table entry is empty
	case T_NULLVIEW: Put32(w.tiles[2], BT_T_C0, 0x7FFF); break;                   // a handle past the table
	case T_BADVTBL: w.viewtab[3 * 0x10] = (uintptr_t)&w.viewtab[100]; w.viewtab[100] = (uintptr_t)w.tiles; break;
	case T_GARBAGE: w.grid[3] = 0x00001234u; w.grid[4] = 0xDEAD0000u; break;   // below 64 KB, then unmapped
	case T_HUGECOUNT: Put32(w.tr, BT_TR_TX, 100000); Put32(w.tr, BT_TR_N, 0x7FFFFFFF); break;
	case T_BADTILE: Put32(w.tiles[5], BT_T_W, 0); Put32(w.tiles[6], BT_T_SX, 0x7FFFFFF0); break;
	case T_NULLTR: memset(w.ts + BT_TS_TR, 0, 4); break;
	}
	return true;
}

// ---- addresses --------------------------------------------------------------------------------------------------------
static uintptr_t g_trSlot, g_viewTab;
static uint32_t g_viewMax;

// The pattern checker's reader: the tap's own guarded read (ctx: a bool set on a fault).
static int BtRead(void* ctx, uintptr_t a, void* out, uint32_t n)
{
	bool fault = false;
	const bool ok = SafeRead(a, out, n, &fault);
	if (fault && ctx) *(bool*)ctx = true;
	return ok ? 1 : 0;
}

static bool CheckPatterns(int build, uint32_t base)
{
	bool fault = false;
	char why[256] = "";
	if (VPExeCheckSigs(build, base, kBtSigs, (int)(sizeof(kBtSigs) / sizeof(kBtSigs[0])), BtRead, &fault, why, sizeof(why)) < 0)
		return true;
	BLOG("bake_tap: REFUSED: %s%s - the tap never reads this EXE", why, fault ? " (fault)" : "");
	return false;
}

// ---- manifest -----------------------------------------------------------------------------------------------------------
static VPShellBakeSlot* ManSlot(uint32_t i) { return (VPShellBakeSlot*)((char*)g_man + sizeof(VPShellBakeManifest) + i * sizeof(VPShellBakeSlot)); }

static bool OpenManifest(uint32_t capacity, const wchar_t* overrideName)
{
	if (g_man) return true;
	if (overrideName && overrideName[0]) wcsncpy_s(g_manName, overrideName, _TRUNCATE);
	else _snwprintf_s(g_manName, _TRUNCATE, VPSHELL_BAKE_SECTION_FMT, GetCurrentProcessId());
	uint32_t bytes = (uint32_t)(sizeof(VPShellBakeManifest) + capacity * sizeof(VPShellBakeSlot));
	g_mapH = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, g_manName);
	if (!g_mapH) { g_fail = VPBAKEF_SECTION; BLOG("bake_tap: CreateFileMapping(%S, %u) failed: %lu", g_manName, bytes, GetLastError()); return false; }
	bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
	void* v = MapViewOfFile(g_mapH, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0);
	if (!v) { g_fail = VPBAKEF_SECTION; BLOG("bake_tap: MapViewOfFile(%S) failed: %lu", g_manName, GetLastError()); CloseHandle(g_mapH); g_mapH = nullptr; return false; }
	MEMORY_BASIC_INFORMATION mbi = {};
	VirtualQuery(v, &mbi, sizeof(mbi));
	if (mbi.RegionSize < bytes)
	{
		g_fail = VPBAKEF_SECTION_SMALL;
		BLOG("bake_tap: section %S already exists with %lu bytes, the manifest needs %u - refused", g_manName, (unsigned long)mbi.RegionSize, bytes);
		UnmapViewOfFile(v); CloseHandle(g_mapH); g_mapH = nullptr;
		return false;
	}
	g_man = (VPShellBakeManifest*)v;
	memset(g_man, 0, bytes);
	g_man->magic = VPSHELL_BAKE_MAGIC; g_man->version = 1; g_man->slotBytes = sizeof(VPShellBakeSlot);
	g_man->exePid = GetCurrentProcessId(); g_man->capacity = capacity;
	g_capacity = capacity;
	BLOG("bake_tap: manifest section %S, %u bytes, %u slots%s", g_manName, bytes, capacity, existed ? " (name existed)" : "");
	return true;
}

static void PublishSlot(uint32_t i, uint32_t state)
{
	Slot& s = g_slots[i];
	VPShellBakeSlot* m = ManSlot(i);
	InterlockedIncrement((volatile LONG*)&m->seq);
	MemoryBarrier();
	m->state = state;
	m->tileIndex = s.index; m->tileX = s.tx; m->tileY = s.ty; m->lod = s.state; m->generation = s.generation;
	m->width = s.desc[0].Width; m->height = s.desc[0].Height; m->mips = s.desc[0].MipLevels;
	m->dxgiFormat[0] = s.desc[0].Format; m->dxgiFormat[1] = s.dst[1] ? s.desc[1].Format : 0;
	LARGE_INTEGER q; QueryPerformanceCounter(&q); m->copiedQpc = q.QuadPart;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&m->seq);
	g_manSeq++;
}

// ---- slot textures -------------------------------------------------------------------------------------------------------
static void ReleaseSlot(Slot& s)
{
	for (int k = 0; k < 2; ++k)
	{
		if (s.src[k]) { s.src[k]->Release(); s.src[k] = nullptr; }
		if (s.dst[k]) { s.dst[k]->Release(); s.dst[k] = nullptr; }
		if (s.nt[k]) { CloseHandle(s.nt[k]); s.nt[k] = nullptr; }
	}
}

static bool EnsureDst(ID3D11Device* dev, uint32_t slot, int k)
{
	Slot& s = g_slots[slot];
	D3D11_TEXTURE2D_DESC d;
	s.src[k]->GetDesc(&d);
	if (s.dst[k])
	{
		if (d.Width == s.desc[k].Width && d.Height == s.desc[k].Height && d.MipLevels == s.desc[k].MipLevels && d.Format == s.desc[k].Format && d.ArraySize == s.desc[k].ArraySize)
			return true;
		s.dst[k]->Release(); s.dst[k] = nullptr;
		if (s.nt[k]) { CloseHandle(s.nt[k]); s.nt[k] = nullptr; }
	}
	D3D11_TEXTURE2D_DESC n = d;
	n.Usage = D3D11_USAGE_DEFAULT; n.BindFlags = D3D11_BIND_SHADER_RESOURCE; n.CPUAccessFlags = 0;
	n.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	if (n.SampleDesc.Count != 1 || n.ArraySize != 1) { BLOG("bake_tap: slot %u t%d: unsupported cache (array %u, samples %u)", slot, k, n.ArraySize, n.SampleDesc.Count); return false; }
	HRESULT hr = dev->CreateTexture2D(&n, nullptr, &s.dst[k]);
	if (FAILED(hr)) { BLOG("bake_tap: slot %u t%d: CreateTexture2D %ux%u fmt %u mips %u failed 0x%08X", slot, k, n.Width, n.Height, n.Format, n.MipLevels, hr); s.dst[k] = nullptr; return false; }
	IDXGIResource1* r = nullptr;
	wchar_t name[80];
	_snwprintf_s(name, _TRUNCATE, VPSHELL_BAKE_TEXTURE_FMT, GetCurrentProcessId(), slot, (unsigned)k);
	hr = s.dst[k]->QueryInterface(__uuidof(IDXGIResource1), (void**)&r);
	if (SUCCEEDED(hr)) { hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, name, &s.nt[k]); r->Release(); }
	if (FAILED(hr)) { BLOG("bake_tap: slot %u t%d: CreateSharedHandle(%S) failed 0x%08X", slot, k, name, hr); s.dst[k]->Release(); s.dst[k] = nullptr; s.nt[k] = nullptr; return false; }
	s.desc[k] = d;
	return true;
}

// ---- tile reading ---------------------------------------------------------------------------------------------------------
struct TileRead { uint32_t w, h, sx, sy, c[2], state; uintptr_t view[2]; };

// false + *fault on an access violation; false without fault when the tile fails a bounds check.
static bool ReadTile(uintptr_t tile, TileRead& t, bool* fault, const char** why)
{
	uint32_t a[4], c[2];
	if (!SafeRead(tile + BT_T_W, a, 16, fault) || !SafeRead(tile + BT_T_C0, c, 8, fault) || !SafeRead(tile + BT_T_STATE, &t.state, 4, fault))
	{
		*why = *fault ? "fault reading the tile" : "tile pointer out of range";
		return false;
	}
	t.w = a[0]; t.h = a[1]; t.sx = a[2]; t.sy = a[3]; t.c[0] = c[0]; t.c[1] = c[1];
	if (t.w < 2 || t.w > 1025 || t.h < 2 || t.h > 1025) { *why = "tile size out of range"; return false; }
	if (t.sx > 64u * BT_MAX_TILES_XY || t.sy > 64u * BT_MAX_TILES_XY) { *why = "tile origin out of range"; return false; }
	for (int k = 0; k < 2; ++k)
	{
		t.view[k] = 0;
		if (!t.c[k] || t.c[k] >= g_viewMax) continue;
		uint32_t v = 0;
		if (!SafeRead(g_viewTab + 12u * t.c[k], &v, 4, fault)) { *why = "fault reading the view table"; return false; }
		t.view[k] = v;
	}
	return true;
}

// A view pointer -> the EXE's texture, after the vtable check. nullptr when the pointer is not a D3D11 object of ours.
static ID3D11Texture2D* ResolveView(uintptr_t view, bool* fault)
{
	if (!view) return nullptr;
	uint32_t vt = 0;
	if (!SafeRead(view, &vt, 4, fault)) return nullptr;
	if (vt != g_refVt[0] && vt != g_refVt[1] && vt != g_refVt[2]) return nullptr;
	return TextureOfView(view, fault);
}

// ---- lifecycle --------------------------------------------------------------------------------------------------------------
static void Disable(uint32_t fail, uint32_t mode, const char* why)
{
	if (g_mode != (int)mode || g_fail != fail)
		BLOG("bake_tap: %s (%s)", mode == 2 ? "DISABLED for the life of the process" : "FAILED for this load", why);
	g_mode = (int)mode; g_fail = fail;
}

static void ResetLoad()
{
	for (uint32_t i = 0; i < g_nSlots; ++i)
	{
		ReleaseSlot(g_slots[i]);
		if (g_man) PublishSlot(i, VPBAKESLOT_EMPTY);
		g_slots[i].used = false;
	}
	g_nSlots = 0;
	for (int q = 0; q < BT_QUERIES; ++q) g_qBusy[q] = false;
	g_scanPos = 0; g_scanned = 0; g_fullScanDone = false; g_grid = 0; g_tilesN = 0;
	g_loadCopyMs = 0; g_maxPresentCopyMs = 0; g_loadCopies = 0; g_reportedCopies = -1;
	LARGE_INTEGER q; QueryPerformanceCounter(&q); g_loadQpc = q.QuadPart;
	if (g_man) { g_man->slotCount = 0; g_man->loadSeq = g_loadSeq; }
	g_manSeq++;
}

static bool InitDevice(ID3D11Device* dev)
{
	// reference vtables: the D3D11 runtime's texture, SRV and RTV objects
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = d.Height = 4; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	ID3D11Texture2D* t = nullptr; ID3D11ShaderResourceView* s = nullptr; ID3D11RenderTargetView* r = nullptr;
	bool ok = SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t)) && SUCCEEDED(dev->CreateShaderResourceView(t, nullptr, &s)) &&
		SUCCEEDED(dev->CreateRenderTargetView(t, nullptr, &r));
	if (ok) { g_refVt[0] = *(uintptr_t*)t; g_refVt[1] = *(uintptr_t*)s; g_refVt[2] = *(uintptr_t*)r; }
	if (r) r->Release(); if (s) s->Release(); if (t) t->Release();
	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	for (int q = 0; ok && q < BT_QUERIES; ++q) ok = SUCCEEDED(dev->CreateQuery(&qd, &g_q[q]));
	if (!ok) BLOG("bake_tap: D3D11 reference objects or queries could not be created");
	return ok;
}

static void ReadTestKeys()
{
	wchar_t v[96] = L"";
	if (!g_dir[0])
	{
		HMODULE m = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ReadTestKeys, &m);
		GetModuleFileNameW(m, g_dir, MAX_PATH);
		wchar_t* sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0;
	}
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", g_dir);
	if (!GetEnvironmentVariableW(L"VPSHELL_BAKE_TEST", v, 96)) GetPrivateProfileStringW(L"composite", L"bake_test", L"", v, 96, ini);
	g_test = T_NONE;
	char a[96]; WideCharToMultiByte(CP_ACP, 0, v, -1, a, 96, nullptr, nullptr);
	for (int i = 1; i < (int)(sizeof(kTestNames) / sizeof(kTestNames[0])); ++i) if (!_stricmp(a, kTestNames[i])) g_test = i;
	if (a[0] && !g_test) BLOG("bake_tap: bake_test=%s is not a known mode - ignored", a);
}

static void ManifestOverride(wchar_t* out, size_t n)
{
	out[0] = 0;
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", g_dir);
	if (!GetEnvironmentVariableW(L"VPSHELL_BAKE_MANIFEST", out, (DWORD)n)) GetPrivateProfileStringW(L"composite", L"bake_manifest", L"", out, (DWORD)n, ini);
}

static void PublishControl(VPShellHeader* hdr)
{
	if (!hdr) return;
	VPShellBakeTap* b = VPSHELL_BAKETAP(hdr);
	InterlockedIncrement((volatile LONG*)&b->seq);
	MemoryBarrier();
	b->state = g_mode == 1 ? VPBAKE_ACTIVE : g_mode == 2 ? VPBAKE_FAILED : g_mode == 3 ? VPBAKE_FAILED : VPBAKE_NONE;
	b->failReason = g_fail; b->exePid = GetCurrentProcessId(); b->loadSeq = g_loadSeq; b->slotCount = g_nSlots;
	b->tilesX = g_tilesX; b->tilesY = g_tilesY; b->tilePlotsX = b->tilePlotsY = 1;
	b->manifestBytes = g_man ? sizeof(VPShellBakeManifest) + (uint64_t)g_capacity * sizeof(VPShellBakeSlot) : 0;
	b->copiesTotal = g_copies; b->copyMsTotal = (float)g_copyMs;
	wcsncpy_s(b->manifestName, g_man ? g_manName : L"", _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&b->seq);
	b->manifestSeq = g_manSeq;
}

// ---- the per-Present work -------------------------------------------------------------------------------------------------
static uint16_t* g_slotByIdx;          // grid index -> slot (0xFFFF = none), g_tilesN entries
static double g_workMs, g_workMaxMs; static uint32_t g_workN;

static uint32_t SlotOfIdx(uint32_t idx) { return g_slotByIdx && g_slotByIdx[idx] != 0xFFFFu ? g_slotByIdx[idx] : 0xFFFFFFFFu; }

static int FreeQuery()
{
	for (int q = 0; q < BT_QUERIES; ++q) if (!g_qBusy[q]) return q;
	return -1;
}

static void Work(const BakeTapEnv& env)
{
	bool fault = false;
	const char* why = "";
	// the grid
	uintptr_t tr = 0;
	if (!SafeRead(g_trSlot, &tr, 4, &fault)) { if (fault) Disable(VPBAKEF_EXCEPTION, 2, "fault reading TerrainSystem+0x75FA4"); return; }
	if (!tr) return;   // no terrain renderer yet (front end, loading)
	unsigned char trb[BT_TR_BYTES];
	if (!SafeRead(tr, trb, sizeof(trb), &fault)) { Disable(fault ? VPBAKEF_EXCEPTION : VPBAKEF_LAYOUT, fault ? 2 : 3, "TR unreadable"); return; }
	uint32_t tx = U32(trb, BT_TR_TX), ty = U32(trb, BT_TR_TY), n = U32(trb, BT_TR_N);
	uintptr_t grid = U32(trb, BT_TR_GRID);
	if (!tx || !ty || tx > BT_MAX_TILES_XY || ty > BT_MAX_TILES_XY || n != tx * ty || !grid)
	{
		char m[128]; _snprintf_s(m, _TRUNCATE, "tile grid out of range: %u x %u tiles, count %u, grid 0x%08X", tx, ty, n, (uint32_t)grid);
		Disable(VPBAKEF_LAYOUT, 3, m);
		return;
	}
	if (g_grid && (grid != g_grid || n != g_tilesN))
	{
		// a new tile grid without a new gridSeq (no server, so no camera block): a new load all the same
		BLOG("bake_tap: tile grid moved (0x%08X -> 0x%08X): new load", (uint32_t)g_grid, (uint32_t)grid);
		g_reload = true;
		g_grid = 0;
		return;
	}
	if (!g_grid)
	{
		g_grid = grid; g_tilesN = n; g_tilesX = tx; g_tilesY = ty; g_scanPos = 0; g_scanned = 0; g_fullScanDone = false;
		free(g_slotByIdx);
		g_slotByIdx = (uint16_t*)malloc(n * sizeof(uint16_t));
		if (!g_slotByIdx) { Disable(VPBAKEF_D3D, 2, "out of memory (slot index)"); return; }
		memset(g_slotByIdx, 0xFF, n * sizeof(uint16_t));
		for (uint32_t i = 0; i < g_nSlots; ++i) if (g_slots[i].used && g_slots[i].index < n) g_slotByIdx[g_slots[i].index] = (uint16_t)i;
		if (g_man) { g_man->tilesX = tx; g_man->tilesY = ty; }
		BLOG("bake_tap: tile grid %u x %u (%u tiles) at 0x%08X, TR 0x%08X, load %u", tx, ty, n, (uint32_t)grid, (uint32_t)tr, g_loadSeq);
	}
	LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
	int copiesThisPresent = 0;
	double copyMsThisPresent = 0;
	const VPShellCamera* cam = env.cam;

	// 1. completed copies -> publish
	for (int q = 0; q < BT_QUERIES; ++q)
	{
		if (!g_qBusy[q]) continue;
		if (env.ctx->GetData(g_q[q], nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
		g_qBusy[q] = false;
		for (uint32_t i = 0; i < g_nSlots; ++i)
		{
			Slot& s = g_slots[i];
			if (s.pendingQuery != q) continue;
			s.pendingQuery = -1; s.generation++;
			PublishSlot(i, s.pendingState);
		}
	}

	// 2. scan: every slot each Present (cheap: three small reads per tile), then a window of the grid
	uint32_t nSlotChecks = g_nSlots;
	uint32_t window = BT_SCAN_PER_PRESENT < n ? BT_SCAN_PER_PRESENT : n;
	uint32_t stepsDone = 0;
	bool budgetHit = false;
	for (uint32_t step = 0; step < nSlotChecks + window; ++step)
	{
		bool isSlot = step < nSlotChecks;
		uint32_t idx;
		uintptr_t tile = 0;
		if (isSlot) { idx = g_slots[step].index; tile = g_slots[step].tile; }
		else
		{
			idx = (g_scanPos + (step - nSlotChecks)) % n;
			if (!SafeRead(grid + 4u * idx, &tile, 4, &fault)) { Disable(fault ? VPBAKEF_EXCEPTION : VPBAKEF_LAYOUT, fault ? 2 : 3, "grid entry unreadable"); return; }
			stepsDone = step - nSlotChecks + 1;
			if (!tile) continue;
			if (SlotOfIdx(idx) != 0xFFFFFFFFu) continue;   // checked above
			uint32_t c0 = 0;
			if (SafeRead(tile + BT_T_C0, &c0, 4, &fault) && !c0) continue;   // no cache: one read per tile
			fault = false;   // a fault here is seen again (and handled) by ReadTile below
		}
		TileRead t;
		if (!ReadTile(tile, t, &fault, &why))
		{
			if (fault) { char m[128]; _snprintf_s(m, _TRUNCATE, "%s (tile %u at 0x%08X)", why, idx, (uint32_t)tile); Disable(VPBAKEF_EXCEPTION, 2, m); return; }
			static uint32_t warned; if (warned++ < 8) BLOG("bake_tap: tile %u at 0x%08X skipped: %s", idx, (uint32_t)tile, why);
			continue;
		}
		uint32_t si = isSlot ? step : SlotOfIdx(idx);
		if (!t.c[0] && si == 0xFFFFFFFFu) continue;   // no cache and no slot: nothing to do
		if (si == 0xFFFFFFFFu)
		{
			if (g_nSlots >= g_capacity) { static bool full; if (!full) { full = true; BLOG("bake_tap: all %u slots used - tile %u not tapped", g_capacity, idx); } continue; }
			si = g_nSlots++;
			Slot& s = g_slots[si];
			memset(&s, 0, sizeof(s));
			g_slotByIdx[idx] = (uint16_t)si;
			s.used = true; s.tile = tile; s.index = idx; s.tx = t.sx / 64; s.ty = t.sy / 64; s.pendingQuery = -1;
			VPShellBakeSlot* m = ManSlot(si);
			m->sampleX = t.sx; m->sampleY = t.sy; m->samplesW = t.w; m->samplesH = t.h;
			if (cam && cam->gridValid)
			{
				m->worldMin[0] = t.sx * cam->sx - cam->ox; m->worldMax[0] = (t.sx + t.w - 1) * cam->sx - cam->ox;
				m->worldMin[1] = t.sy * cam->sy - cam->oy; m->worldMax[1] = (t.sy + t.h - 1) * cam->sy - cam->oy;
			}
			g_man->slotCount = g_nSlots;
			s.h[0] = s.h[1] = 0xFFFFFFFFu;   // force the first copy
		}
		Slot& s = g_slots[si];
		bool changed = t.c[0] != s.h[0] || t.c[1] != s.h[1] || t.view[0] != s.view[0] || t.view[1] != s.view[1] || t.state != s.state;
		if (!changed || s.pendingQuery >= 0) continue;
		int q = FreeQuery();
		if (copiesThisPresent >= BT_COPY_BUDGET || copyMsThisPresent >= BT_COPY_MS_BUDGET || q < 0)
		{
			budgetHit = true;   // next Present: the slot keeps its old handles, so it is still "changed" then
			if (isSlot) continue;
			stepsDone = step - nSlotChecks;   // resume the grid scan at this tile
			break;
		}
		// resolve the EXE's textures
		ID3D11Texture2D* src[2] = { nullptr, nullptr };
		for (int k = 0; k < 2; ++k)
		{
			src[k] = ResolveView(t.view[k], &fault);
			if (fault) { Disable(VPBAKEF_EXCEPTION, 2, "fault resolving a cache view"); return; }
			if (t.c[k] && !src[k]) { static uint32_t w2; if (w2++ < 8) BLOG("bake_tap: tile %u: cache t%d handle 0x%X has no D3D11 texture (view 0x%08X) - missing", idx, k * 8, t.c[k], (uint32_t)t.view[k]); }
		}
		s.h[0] = t.c[0]; s.h[1] = t.c[1]; s.view[0] = t.view[0]; s.view[1] = t.view[1]; s.state = t.state;
		for (int k = 0; k < 2; ++k)
		{
			if (s.src[k]) s.src[k]->Release();
			s.src[k] = src[k];
			if (!src[k] && s.dst[k]) { s.dst[k]->Release(); s.dst[k] = nullptr; if (s.nt[k]) { CloseHandle(s.nt[k]); s.nt[k] = nullptr; } }
		}
		uint32_t newState = s.src[0] ? (s.src[1] ? VPBAKESLOT_BOTH : VPBAKESLOT_T0) : VPBAKESLOT_EMPTY;
		bool ok = newState != VPBAKESLOT_EMPTY;
		for (int k = 0; ok && k < 2; ++k) if (s.src[k] && !EnsureDst(env.dev, si, k)) ok = false;
		if (!ok) { PublishSlot(si, VPBAKESLOT_EMPTY); continue; }
		PublishSlot(si, VPBAKESLOT_EMPTY);   // in flight: readers skip it until the query completes
		LARGE_INTEGER c0, c1; QueryPerformanceCounter(&c0);
		for (int k = 0; k < 2; ++k) if (s.src[k]) { env.ctx->CopyResource(s.dst[k], s.src[k]); copiesThisPresent++; g_copies++; g_loadCopies++; }
		env.ctx->End(g_q[q]);
		QueryPerformanceCounter(&c1);
		double ms = (c1.QuadPart - c0.QuadPart) * g_qpcMs;
		copyMsThisPresent += ms; g_copyMs += ms; g_loadCopyMs += ms;
		g_qBusy[q] = true; s.pendingQuery = q; s.pendingState = newState;
	}
	g_scanPos = (g_scanPos + stepsDone) % n;
	g_scanned += stepsDone;
	if (!g_fullScanDone && g_scanned >= n && !budgetHit) g_fullScanDone = true;

	if (g_fullScanDone && g_reportedCopies != g_loadCopies)
	{
		bool pending = false;
		for (uint32_t i = 0; i < g_nSlots; ++i) if (g_slots[i].pendingQuery >= 0) pending = true;
		if (!pending)
		{
			g_reportedCopies = g_loadCopies;
			LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
			uint32_t both = 0, t0only = 0;
			for (uint32_t i = 0; i < g_nSlots; ++i) { VPShellBakeSlot* m = ManSlot(i); both += m->state == VPBAKESLOT_BOTH; t0only += m->state == VPBAKESLOT_T0; }
			BLOG("bake_tap: load %u tapped: %u slots (%u both, %u t0 only), %d copies, %.3f ms CPU in CopyResource (%.4f ms per copy, at most %.3f ms in one Present), %.0f ms after the load began",
				g_loadSeq, g_nSlots, both, t0only, g_loadCopies, g_loadCopyMs, g_loadCopies ? g_loadCopyMs / g_loadCopies : 0.0, g_maxPresentCopyMs,
				(t1.QuadPart - g_loadQpc) * g_qpcMs);
		}
	}
	if (copyMsThisPresent > g_maxPresentCopyMs) g_maxPresentCopyMs = copyMsThisPresent;
	LARGE_INTEGER t2; QueryPerformanceCounter(&t2);
	double wms = (t2.QuadPart - t0.QuadPart) * g_qpcMs;
	g_workMs += wms; g_workN++; if (wms > g_workMaxMs) g_workMaxMs = wms;
	if (g_workN >= 1000)
	{
		BLOG("bake_tap: cost over %u Presents: mean %.4f ms, max %.3f ms per Present (render thread, scan + copies); %u slots, %lld copies so far",
			g_workN, g_workMs / g_workN, g_workMaxMs, g_nSlots, (long long)g_copies);
		g_workMs = 0; g_workMaxMs = 0; g_workN = 0;
	}
}

void BakeTap_OnPresent(const BakeTapEnv& env)
{
	if (!env.enabled || g_mode == 2) { if (env.hdr && g_mode == 2) PublishControl(env.hdr); return; }
	g_log = env.log;
	if (!g_qpcMs) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcMs = 1000.0 / (double)f.QuadPart; }
	if (!env.dev || !env.ctx) return;
	if (!g_testInit)
	{
		g_testInit = true;
		ReadTestKeys();
		if (g_test) BLOG("bake_tap: TEST MODE bake_test=%s: synthetic tile grid in this DLL, the EXE is not read", kTestNames[g_test]);
	}
	if (!g_slots)
	{
		g_slots = (Slot*)calloc(VPSHELL_BAKE_MAX_SLOTS, sizeof(Slot));
		if (!g_slots) { Disable(VPBAKEF_D3D, 2, "out of memory"); return; }
	}
	if (!g_refVt[0] && !InitDevice(env.dev)) { Disable(VPBAKEF_D3D, 2, "D3D11 set-up failed"); PublishControl(env.hdr); return; }
	if (!g_patternsChecked)
	{
		g_patternsChecked = true;
		if (g_test)
		{
			g_patternsOk = BuildTestWorld(env.dev);
			if (!g_patternsOk) { Disable(VPBAKEF_D3D, 2, "test world could not be built"); return; }
			g_trSlot = (uintptr_t)g_tw->ts + BT_TS_TR; g_viewTab = (uintptr_t)g_tw->viewtab; g_viewMax = 64;
		}
		else
		{
			const int build = VPCamBuild();   // the build the composite validated (ticket 4964)
			g_patternsOk = env.exeValidated && env.exeBase && CheckPatterns(build, env.exeBase);
			if (!env.exeValidated) BLOG("bake_tap: REFUSED: the composite did not validate the EXE image - the tap never reads it");
			g_trSlot = VpExeAddr(build, VPX_bake_ts, env.exeBase) + BT_TS_TR;
			g_viewTab = VpExeAddr(build, VPX_pxw_tex_views, env.exeBase); g_viewMax = BT_VIEWTAB_MAX;
			if (g_patternsOk) BLOG("bake_tap: EXE patterns match (%u), TerrainSystem at 0x%08X - tap enabled", (unsigned)(sizeof(kBtSigs) / sizeof(kBtSigs[0])), (uint32_t)(g_trSlot - BT_TS_TR));
		}
		if (!g_patternsOk) { Disable(VPBAKEF_REFUSED, 2, "EXE refused"); PublishControl(env.hdr); return; }
	}
	if (!g_man)
	{
		wchar_t ov[64]; ManifestOverride(ov, 64);
		if (!OpenManifest(g_test ? 64 : VPSHELL_BAKE_MAX_SLOTS, ov)) { Disable(g_fail, 2, "manifest section"); PublishControl(env.hdr); return; }
	}
	// the load: the camera block's gridSeq (real) or the test's trigger file
	// loadSeq is the tap's own count of loads: +1 when the camera block's gridSeq changes, when the tile grid moves,
	// or (test mode) when the trigger file appears. The manifest carries the camera gridSeq beside it.
	bool newLoad = !g_haveLoad || g_reload;
	if (g_test)
	{
		wchar_t trig[MAX_PATH]; _snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.bakeload", g_dir);
		if (GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig)) newLoad = true;   // once per file
	}
	else if (env.cam && env.cam->gridValid && env.cam->gridSeq != g_lastGridSeq)
	{
		if (g_lastGridSeq) newLoad = true;   // the first valid gridSeq of the process describes the grid already scanned
		g_lastGridSeq = env.cam->gridSeq;
		if (g_man) g_man->gridSeq = g_lastGridSeq;
	}
	if (newLoad)
	{
		g_haveLoad = true;
		g_reload = false;
		g_loadSeq++;
		ResetLoad();
		if (g_mode == 3) g_fail = VPBAKEF_NONE;
		g_mode = 1;
		BLOG("bake_tap: load %u: slots reset, scanning the tile grid", g_loadSeq);
	}
	if (g_mode == 1) Work(env);
	if (g_man) g_man->loadSeq = g_loadSeq;
	PublishControl(env.hdr);
}

void BakeTap_OnOMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
	// Not needed: in D33 the EXE re-bakes no tile during play (issue 3204, live), and the per-Present check sees a
	// re-bake through the tile's handle, view pointer and state word. Kept as the interface's hook for later.
	(void)c; (void)n; (void)rtvs; (void)dsv;
}

void BakeTap_Shutdown(const char* why)
{
	if (!g_slots) return;
	uint32_t had = g_nSlots;
	for (uint32_t i = 0; i < g_nSlots; ++i) { ReleaseSlot(g_slots[i]); if (g_man) PublishSlot(i, VPBAKESLOT_EMPTY); g_slots[i].used = false; }
	g_nSlots = 0;
	for (int q = 0; q < BT_QUERIES; ++q) { if (g_q[q]) { g_q[q]->Release(); g_q[q] = nullptr; } g_qBusy[q] = false; }
	if (g_man) { g_man->slotCount = 0; UnmapViewOfFile(g_man); g_man = nullptr; }
	if (g_mapH) { CloseHandle(g_mapH); g_mapH = nullptr; }
	if (g_tw)
	{
		for (int i = 0; i < 32; ++i) { if (g_tw->srv[i]) g_tw->srv[i]->Release(); if (g_tw->tex[i]) g_tw->tex[i]->Release(); }
		free(g_tw); g_tw = nullptr;
	}
	g_refVt[0] = g_refVt[1] = g_refVt[2] = 0;
	g_patternsChecked = false; g_haveLoad = false; g_grid = 0;
	if (g_mode != 2) g_mode = 0;
	if (had) BLOG("bake_tap: released %u slots (%s)", had, why ? why : "");
}
