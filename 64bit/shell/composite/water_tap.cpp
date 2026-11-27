// water_tap.cpp - the water mesh tap inside shell_composite.dll (epic 3600, issue 3603; decision 1 route a).
// Written 2026-11-13 for this repository (GPL-3.0-or-later). Contract: water_tap.h; layout: shell/common/shell_shared.h.
// What the EXE's objects are, and why each byte pattern below is checked: research/05-graphics-composition/
// water-system.md section 9 (behaviour, D83; nothing of the EXE is in this file but the patterns' few bytes, as in
// bake_tap.cpp).
//
// What it does, once per map load (the camera block's gridSeq), after the load is over:
//   1. GATHER (one Present): reads the river object (RiverVisSystem 0x1A0C694), the Water object (0xD3DD0C) and
//      Wave_System (0x1A0C6C8) with guarded reads; resolves each vertex / index buffer handle through the renderer's
//      tables (handle -> ID3D11Buffer*, handle -> the EXE's own buffer description, vertex format -> stride) after a
//      vtable check against a D3D11 buffer this file created itself; checks every count against the EXE's own fields;
//      issues one CopyResource per buffer into a STAGING buffer on the EXE's device (IMMUTABLE buffers do not share
//      across processes, GATE2).
//   2. MAP (the next Presents): maps each staging buffer with DO_NOT_WAIT (no stall on the render thread) into the
//      image in this process's heap; checks every index against its vertex buffer; re-reads the objects (a load that
//      started meanwhile fails the copy, CHANGED); hashes; writes the image into the named section
//      Local\VPShellWater.<pid>.<loadSeq>.<copySeq> through views of at most 4 MiB, unmapped at once; frees the heap
//      image; publishes READY.
//   3. WAIT: closes the section on the server's ack (RELEASED), on the ack timeout or a new load (FAILED), or when the
//      kill switch is set (FAILED KILLED). requestSeq asks for a new copy of the same load (height-field protocol).
// Safety: every read of EXE memory goes through SafeRead (bounds + SEH); a fault disables the tap for the process
// (EXCEPTION); a failed pattern check refuses the EXE for good (REFUSED); a layout check fails this load only (LAYOUT).
// It calls no EXE function. Under D33 the wave mesh does not exist (water-system.md 9: StartupGame skips its builder
// when `Waves` is disabled): the image then holds the Wave_System object only.
//
// Test seam (like bake_test): water_test=<mode> (ini or VPSHELL_WATER_TEST) builds a synthetic EXE (river, water, wave
// objects, renderer tables, real IMMUTABLE D3D11 buffers of known content) in this DLL's memory and runs the same code
// on it. A file `shell_composite.waterload` next to the DLL simulates a new load in test mode (deleted when seen).
#include "water_tap.h"
#include "exe_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---- EXE layout (DX11 EXE, both known builds; VAs at ImageBase 0x400000; water-system.md 9) ------------------------
// Ticket 4964 (epic 0590): every address is a build-table symbol (exebuild/symbols/shell.tsv), read for the build the
// composite validated: water_river (RiverVisSystem, static object), water_water (Water = TerrainSystem + 0x2772C),
// water_wave (Wave_System, static object), water_vbtab / water_ibtab (VB / IB handle -> ID3D11Buffer*), water_vbdesc /
// water_ibdesc (handle -> the EXE's 0x30-byte description, +0x2C DXGI format), water_stride (vertex format id -> stride
// dword), pxw_drawobj_pool (draw record handle -> 0x80-byte record), water_passlevel (the river render takes pass 1 when
// this dword is > 2, else pass 4). EXE addresses in this file's comments are build 4390913's.
#define WT_BUF_MAX 0x1800u
#define WT_REC_MAX 0x20000u
#define WT_DESC_BYTES 0x30u
#define WT_REC_BYTES 0x80u
#define WT_RIVER_BYTES 0x30u
#define WT_WATER_BYTES 0xECu        // TerrainSystem+0x2772C .. +0x27818 (the next member)
#define WT_WAVE_BYTES 0xB0u
#define WT_CHUNK_BYTES 36u
#define WT_INST_BYTES 16u
#define WT_MAX_CHUNKS 512u
#define WT_INST_PER_CHUNK 248u
#define WT_MAT_WATER 91u
#define WT_MAT_WAVE 92u
#define WT_VIEW_BYTES (4u * 1024 * 1024)
#define WT_MAP_TRIES 240            // Presents of DO_NOT_WAIT maps before one blocking map

// Byte patterns checked before the first read of the real EXE: X(symbol, description, bytes), R(symbol) = 4 bytes
// holding that symbol's VA in the running build, rebased (exe_camera.h; checked on both builds' golden EXEs by
// tools/exebuild/gen_table.py through exebuild/patterns.txt).
#define WT_SIGNATURES(X) \
	X(water_river_build, "river build: RiverVisSystem (mov ecx before a call)", "B9 R(water_river) E8") \
	X(water_river_upload_vb, "river upload: VB handle River+0x1C, the VB pool", "8B 46 1C 57 50 B9 R(water_vbpool)") \
	X(water_river_upload_ib, "river upload: IB handle River+0x20, the IB pool", "8B 4E 20 51 B9 R(water_ibpool)") \
	X(water_river_upload_rec, "river upload: draw record River+0x18, IB handle stored at River+0x20", "8B 56 18 8D 7E 18 89 46 20") \
	X(water_river_upload_mat, "river upload: draw record material 91 (WaterShader)", "6A 5B") \
	X(water_river_upload_tris, "river upload: River+0x2C = 1, triangle count River+0x28", "C6 46 2C 01 89 56 28") \
	X(water_river_render_pass, "river render: pass chosen from the pass-level dword", "A1 R(water_passlevel)") \
	X(water_vbcreate_desc, "VB create: description table, 0x30 bytes per handle", "8D 34 7F C1 E6 04 81 C6 R(water_vbdesc)") \
	X(water_vbcreate_stride, "VB create: stride table", "8B 14 8D R(water_stride)") \
	X(water_vbcreate_buf, "VB create: buffer table", "8D 0C BD R(water_vbtab)") \
	X(water_ibcreate_desc, "IB create: description table", "8D 34 7F C1 E6 04 81 C6 R(water_ibdesc)") \
	X(water_ibcreate_buf, "IB create: buffer table", "8D 14 BD R(water_ibtab)") \
	X(water_ibcreate_fmt, "IB create: DXGI format at description+0x2C", "89 46 2C") \
	X(water_pool_cap, "VB/IB handle pool: capacity 0x1800", "3D 00 18 00 00") \
	X(water_reccreate, "draw record create: the records table, 0x80 bytes each", "C1 E0 07 68 80 00 00 00 8D B0 R(pxw_drawobj_pool)") \
	X(water_recpool_cap, "draw record pool: capacity 0x20000", "3D 00 00 02 00") \
	X(water_terrain_render, "terrain render: Water = TerrainSystem+0x2772C", "8D 8E 2C 77 02 00 E8") \
	X(water_ocean_cap, "ocean: Water+0x5C quad capacity", "89 4F 5C") \
	X(water_ocean_vb, "ocean: Water+0x54 VB handle (per frame)", "89 47 54") \
	X(water_ocean_ib, "ocean: Water+0x58 IB handle (per frame)", "89 47 58") \
	X(water_passa_wave, "pass A: Wave_System render", "B9 R(water_wave) E8") \
	X(water_wave_render_count, "wave render: chunk count Wave+0x58", "8B B3 90 00 00 00 8B 0D R(cam_numviews) 33 FF 89 74 24 18 89 7C 24 14 39 7B 58") \
	X(water_wave_render_waves, "wave render: chunk +0x0C first wave, +0x10 waves, 16 bytes per wave", "8D 0C 17 8B 51 10 8B 49 0C C1 E1 04") \
	X(water_wavechunk_array, "wave chunk build: chunks at Wave+0x54", "8B 47 54 03 F6 89 54 06") \
	X(water_wavechunk_verts, "wave chunk build: 0x1170 vertices", "68 70 11 00 00 89 48 0C") \
	X(water_wavechunk_fmt, "wave chunk build: vertex format 0x19", "6A 19") \
	X(water_wavechunk_idx, "wave chunk build: 0x2E80 indices, VB handle at chunk+4", "68 80 2E 00 00 89 44 0E 04") \
	X(water_wave_update_count, "wave update: wave count Wave+0x60", "8B 5E 04 33 ED 39 6E 60") \
	X(water_wave_update_waves, "wave update: waves at Wave+0x5C", "8B 46 5C F3 0F 10")
static const VPExeSig kWtSigs[] = { WT_SIGNATURES(VPEXE_SIG_ENTRY) };

// ---- state -----------------------------------------------------------------------------------------------------------
enum { PH_IDLE, PH_MAP, PH_WAIT };
struct Item
{
	VPShellWaterRecord r;
	unsigned char* obj;          // OBJECT: our copy of the EXE's bytes
	ID3D11Buffer* staging;       // VB / IB: the staging copy in flight
	uint32_t vbCount;            // DRAW: vertices of its VB (index check)
};

static Item* g_items;
static uint32_t g_nItems, g_capItems;
static unsigned char* g_image;
static uint64_t g_imageBytes;
static int g_phase;
static bool g_init, g_dead, g_patternsOk;
static uint32_t g_fail;
static uint32_t g_copySeq, g_curLoad, g_runSeq, g_okRun, g_sceneRun;
static bool g_started; static uint32_t g_startedLoad, g_startedReq;
static uint32_t g_mapTries;
static HANDLE g_sec;
static wchar_t g_secName[64];
static uint64_t g_secBytes, g_fnv;
static int64_t g_copyQpc, g_readyQpc;
static double g_copyCpuMs; static uint32_t g_copyPresents;
static float g_lastCopyCpuMs;
static int64_t g_t0;           // QPC at the start of this Present's work
static float g_copyMs;
static uint32_t g_systems;
static uintptr_t g_refVt[3];   // vtables of a D3D11 vertex buffer, index buffer and staging buffer made by this file
static void (*g_log)(const char*, ...);
static uint32_t g_logs, g_costLogs;
static double g_qpcMs;
static int g_test;
static uint32_t g_testBump;
static wchar_t g_dir[MAX_PATH];
static uint32_t g_delayFrames = 60, g_sceneFrames = 30, g_ackTimeoutMs = VPSHELL_WATERTAP_ACK_TIMEOUT_MS;
static uint64_t g_maxBytes = VPSHELL_WATER_MAX_BYTES;
static VPShellHeader* g_pubHdr; static uint32_t g_pubState = 0xFFFFFFFFu, g_pubVersion, g_wantVersion;
static uint32_t g_state = VPWTAP_NONE;
static double g_workMs, g_workMaxMs; static uint32_t g_workN;
static char g_why[256];

struct Addr { uintptr_t river, water, wave, vbTab, vbDesc, ibTab, ibDesc, stride, recTab, passLevel, waterZ; };
static Addr A;
static unsigned char g_riverSnap[WT_RIVER_BYTES], g_waveSnap[WT_WAVE_BYTES];
static unsigned char* g_chunkSnap; static uint32_t g_chunkSnapBytes;

enum { T_NONE, T_OK, T_NORIVER, T_BADHANDLE, T_BADVTBL, T_BADSIZE, T_BADINDEX, T_HUGECOUNT, T_BADRECORD, T_GARBAGE,
       T_CHANGE, T_BADSECTION, T_COUNT };
static const char* kTestNames[T_COUNT] = { "", "ok", "noriver", "badhandle", "badvtbl", "badsize", "badindex", "hugecount",
                                           "badrecord", "garbage", "change", "badsection" };
static const char* kFailNames[] = { "NONE", "REFUSED", "EXCEPTION", "LAYOUT", "SECTION", "D3D", "TOO_BIG", "ACK_TIMEOUT",
                                    "CHANGED", "KILLED" };

#define WLOG(...) do { if (g_log && g_logs < 400) { g_logs++; g_log(__VA_ARGS__); } } while (0)

static int64_t Qpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static double Ms(int64_t d) { return d * g_qpcMs; }

// ---- guarded reads (as bake_tap.cpp: a byte loop under SEH, never memcpy on a wild address) --------------------------
#if defined(__SANITIZE_ADDRESS__)
__declspec(no_sanitize_address)
#endif
static bool SafeRead(uintptr_t a, void* out, size_t n, bool* fault)
{
	if (a < 0x10000 || n == 0 || n > 0x400000 || a + n < a || (uint64_t)a + n > 0xFFFF0000ull) return false;
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
static inline uint16_t U16(const unsigned char* b, uint32_t off) { uint16_t v; memcpy(&v, b + off, 2); return v; }
static void Put32(unsigned char* b, uint32_t off, uint32_t v) { memcpy(b + off, &v, 4); }

static uint64_t Fnv(uint64_t h, const unsigned char* p, size_t n)
{
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
	return h;
}

// An object whose vtable matched one of ours: QI(ID3D11Buffer) and GetDesc, under SEH (a stale pointer of a right type).
static ID3D11Buffer* BufferOf(uintptr_t obj, D3D11_BUFFER_DESC* d, bool* fault)
{
	ID3D11Buffer* b = nullptr;
	__try
	{
		IUnknown* u = (IUnknown*)obj;
		if (FAILED(u->QueryInterface(__uuidof(ID3D11Buffer), (void**)&b))) b = nullptr;
		if (b) b->GetDesc(d);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*fault = true;
		b = nullptr;
	}
	return b;
}

// ---- the synthetic EXE (test mode) -----------------------------------------------------------------------------------
// Content (shell/tests/test_water_tap.py recomputes it): buffer id b (river VB 1, river IB 2, wave chunk k VB 3 + 2k,
// IB 4 + 2k); VB byte j = (31 j + 17 b + 5) & 0xFF; IB index i = (5 i + b) % vertices. River: VB format 0xA stride 32,
// 12 vertices, IB 32-bit 18 indices, 6 triangles. Waves: 2 chunks, VB format 0x19 stride 12, 8 vertices, IB 16-bit 12
// indices, 4 triangles each; 3 waves (chunk 0: waves 0-1, chunk 1: wave 2). Handles: river record 5, VB 7, IB 9;
// chunk k record 6 + k, VB 10 + k, IB 12 + k. Pass level 3 (river pass 1). Water z 10.117.
struct TestWorld
{
	unsigned char river[WT_RIVER_BYTES], water[WT_WATER_BYTES], wave[WT_WAVE_BYTES];
	unsigned char chunks[2 * WT_CHUNK_BYTES], inst[3 * WT_INST_BYTES];
	uintptr_t vbTab[16], ibTab[16];
	unsigned char vbDesc[16 * WT_DESC_BYTES], ibDesc[16 * WT_DESC_BYTES];
	uint32_t stride[64];
	unsigned char recTab[16 * WT_REC_BYTES];
	uint32_t passLevel;
	float grid[2];
	uintptr_t fakeObj[4];
	ID3D11Buffer* bufs[8];
	void* reserved;
};
static TestWorld* g_tw;

static ID3D11Buffer* TestBuffer(ID3D11Device* dev, uint32_t id, bool ib, uint32_t bytes, uint32_t verts, uint32_t idxSize)
{
	unsigned char* p = (unsigned char*)calloc(1, bytes);
	if (!p) return nullptr;
	if (!ib) for (uint32_t j = 0; j < bytes; ++j) p[j] = (unsigned char)((31 * j + 17 * id + 5) & 0xFF);
	else
		for (uint32_t i = 0; i < bytes / idxSize; ++i)
		{
			uint32_t v = (5 * i + id) % verts;
			if (g_test == T_BADINDEX && id == 2 && i == 7) v = verts + 3;   // an index past the vertex buffer
			memcpy(p + i * idxSize, &v, idxSize);
		}
	D3D11_BUFFER_DESC d = {};
	d.ByteWidth = bytes; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = ib ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA sd = { p, 0, 0 };
	ID3D11Buffer* b = nullptr;
	if (FAILED(dev->CreateBuffer(&d, &sd, &b))) b = nullptr;
	free(p);
	return b;
}

static void TestRecord(unsigned char* rec, uint16_t material, uint32_t ib, uint32_t vb, uint32_t prims)
{
	memset(rec, 0, WT_REC_BYTES);
	memcpy(rec, &material, 2); Put32(rec, 4, ib); Put32(rec, 8, vb); Put32(rec, 0xC, prims);
}

static bool BuildTestWorld(ID3D11Device* dev)
{
	g_tw = (TestWorld*)calloc(1, sizeof(TestWorld));
	if (!g_tw) return false;
	TestWorld& w = *g_tw;
	w.stride[0xA] = 32; w.stride[0x19] = 12; w.passLevel = 3; w.grid[1] = 10.117f;
	// river
	w.bufs[0] = TestBuffer(dev, 1, false, 32 * 12, 12, 0);
	w.bufs[1] = TestBuffer(dev, 2, true, 18 * 4, 12, 4);
	w.vbTab[7] = (uintptr_t)w.bufs[0]; w.ibTab[9] = (uintptr_t)w.bufs[1];
	Put32(w.vbDesc + 7 * WT_DESC_BYTES, 0, 32 * 12); Put32(w.vbDesc + 7 * WT_DESC_BYTES, 4, 1); Put32(w.vbDesc + 7 * WT_DESC_BYTES, 0x24, 0xA);
	Put32(w.ibDesc + 9 * WT_DESC_BYTES, 0, 18 * 4); Put32(w.ibDesc + 9 * WT_DESC_BYTES, 4, 1); Put32(w.ibDesc + 9 * WT_DESC_BYTES, 0x2C, 42);
	TestRecord(w.recTab + 5 * WT_REC_BYTES, WT_MAT_WATER, 9, 7, 6);
	Put32(w.river, 0x18, 5); Put32(w.river, 0x1C, 7); Put32(w.river, 0x20, 9); Put32(w.river, 0x28, 6); w.river[0x2C] = 1;
	// waves: two chunks
	for (uint32_t k = 0; k < 2; ++k)
	{
		w.bufs[2 + 2 * k] = TestBuffer(dev, 3 + 2 * k, false, 12 * 8, 8, 0);
		w.bufs[3 + 2 * k] = TestBuffer(dev, 4 + 2 * k, true, 12 * 2, 8, 2);
		w.vbTab[10 + k] = (uintptr_t)w.bufs[2 + 2 * k]; w.ibTab[12 + k] = (uintptr_t)w.bufs[3 + 2 * k];
		Put32(w.vbDesc + (10 + k) * WT_DESC_BYTES, 0, 12 * 8); Put32(w.vbDesc + (10 + k) * WT_DESC_BYTES, 0x24, 0x19);
		Put32(w.ibDesc + (12 + k) * WT_DESC_BYTES, 0, 12 * 2); Put32(w.ibDesc + (12 + k) * WT_DESC_BYTES, 0x2C, 57);
		TestRecord(w.recTab + (6 + k) * WT_REC_BYTES, WT_MAT_WAVE, 12 + k, 10 + k, 4);
		unsigned char* c = w.chunks + k * WT_CHUNK_BYTES;
		Put32(c, 0, 6 + k); Put32(c, 4, 10 + k); Put32(c, 8, 12 + k); Put32(c, 0xC, k ? 2 : 0); Put32(c, 0x10, k ? 1 : 2);
		float box[4] = { 10.0f * k, 5.0f, 10.0f * k + 8.0f, 9.0f }; memcpy(c + 0x14, box, 16);
	}
	for (uint32_t i = 0; i < 3 * WT_INST_BYTES; ++i) w.inst[i] = (unsigned char)(i * 3 + 1);
	Put32(w.wave, 0x54, (uint32_t)(uintptr_t)w.chunks); Put32(w.wave, 0x58, 2); Put32(w.wave, 0x5C, (uint32_t)(uintptr_t)w.inst);
	Put32(w.wave, 0x60, 3); Put32(w.wave, 0x68, 0x123);
	for (uint32_t i = 0; i < WT_WATER_BYTES; ++i) w.water[i] = (unsigned char)(i ^ 0x5A);
	for (int i = 0; i < 6; ++i) if (!w.bufs[i]) return false;
	// hostile variants
	switch (g_test)
	{
	case T_NORIVER: w.river[0x2C] = 0; break;
	case T_BADHANDLE: Put32(w.river, 0x1C, 0x1900); Put32(w.recTab + 5 * WT_REC_BYTES, 8, 0x1900); break;   // past the handle pool
	case T_BADVTBL: w.fakeObj[0] = (uintptr_t)&w.fakeObj[2]; w.vbTab[7] = (uintptr_t)w.fakeObj; break;   // not a D3D11 object
	case T_BADSIZE: Put32(w.vbDesc + 7 * WT_DESC_BYTES, 0, 32 * 13); break;                 // the EXE's size disagrees with D3D's
	case T_HUGECOUNT: Put32(w.wave, 0x58, 100000); break;
	case T_BADRECORD: { uint16_t m = 90; memcpy(w.recTab + 5 * WT_REC_BYTES, &m, 2); } break;   // not WaterShader
	case T_GARBAGE:                                                                          // chunks in uncommitted memory
		w.reserved = VirtualAlloc(nullptr, 1 << 16, MEM_RESERVE, PAGE_NOACCESS);
		Put32(w.wave, 0x54, (uint32_t)(uintptr_t)w.reserved);
		break;
	default: break;
	}
	return true;
}

static void FreeTestWorld()
{
	if (!g_tw) return;
	for (int i = 0; i < 8; ++i) if (g_tw->bufs[i]) g_tw->bufs[i]->Release();
	if (g_tw->reserved) VirtualFree(g_tw->reserved, 0, MEM_RELEASE);
	free(g_tw); g_tw = nullptr;
}

// ---- set-up --------------------------------------------------------------------------------------------------------
// The pattern checker's reader: the tap's own guarded read (ctx: a bool set on a fault).
static int WtRead(void* ctx, uintptr_t a, void* out, uint32_t n)
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
	if (VPExeCheckSigs(build, base, kWtSigs, (int)(sizeof(kWtSigs) / sizeof(kWtSigs[0])), WtRead, &fault, why, sizeof(why)) < 0)
		return true;
	WLOG("water_tap: REFUSED: %s%s - the tap never reads this EXE", why, fault ? " (fault)" : "");
	return false;
}

static bool InitDevice(ID3D11Device* dev)
{
	D3D11_BUFFER_DESC d = {};
	d.ByteWidth = 64; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	ID3D11Buffer* b[3] = {};
	bool ok = SUCCEEDED(dev->CreateBuffer(&d, nullptr, &b[0]));
	d.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ok = ok && SUCCEEDED(dev->CreateBuffer(&d, nullptr, &b[1]));
	d.BindFlags = 0; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ok = ok && SUCCEEDED(dev->CreateBuffer(&d, nullptr, &b[2]));
	for (int i = 0; i < 3; ++i) if (b[i]) { g_refVt[i] = *(uintptr_t*)b[i]; b[i]->Release(); }
	if (!ok) WLOG("water_tap: D3D11 reference buffers could not be created");
	return ok;
}

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
	get(L"water_delay_frames", L"VPSHELL_WATER_DELAY_FRAMES", v, 96); if (v[0]) g_delayFrames = (uint32_t)clamp(_wtoi(v), 1, 100000);
	get(L"water_scene_frames", L"VPSHELL_WATER_SCENE_FRAMES", v, 96); if (v[0]) g_sceneFrames = (uint32_t)clamp(_wtoi(v), 0, 100000);
	get(L"water_ack_timeout_ms", L"VPSHELL_WATER_ACK_TIMEOUT_MS", v, 96); if (v[0]) g_ackTimeoutMs = (uint32_t)clamp(_wtoi(v), 100, 3600000);
	get(L"water_max_kb", L"VPSHELL_WATER_MAX_KB", v, 96);
	if (v[0]) g_maxBytes = (uint64_t)clamp(_wtoi(v), 1, (int)(VPSHELL_WATER_MAX_BYTES / 1024)) * 1024;
}

static void ReadTestKey()
{
	ReadKeys();
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", g_dir);
	wchar_t v[96] = L"";
	if (!GetEnvironmentVariableW(L"VPSHELL_WATER_TEST", v, 96)) GetPrivateProfileStringW(L"composite", L"water_test", L"", v, 96, ini);
	char a[96]; WideCharToMultiByte(CP_ACP, 0, v, -1, a, 96, nullptr, nullptr);
	g_test = T_NONE;
	for (int i = 1; i < T_COUNT; ++i) if (!_stricmp(a, kTestNames[i])) g_test = i;
	if (a[0] && !g_test) WLOG("water_tap: water_test=%s is not a known mode - ignored", a);
}

// ---- the control block -----------------------------------------------------------------------------------------------
static void Publish(VPShellHeader* hdr)
{
	if (!hdr) return;
	VPShellWaterTap* t = VPSHELL_WATERTAP(hdr);
	InterlockedIncrement((volatile LONG*)&t->seq);
	MemoryBarrier();
	t->state = g_state; t->loadSeq = g_curLoad; t->failReason = g_fail; t->exePid = GetCurrentProcessId();
	t->copySeq = g_copySeq; t->recordCount = g_state == VPWTAP_READY || g_state == VPWTAP_RELEASED ? g_nItems : 0;
	t->systems = g_systems; t->imageBytes = g_imageBytes; t->sectionBytes = g_secBytes; t->fnv1a64 = g_fnv;
	t->readyQpc = g_readyQpc; t->copyMs = g_copyMs;
	wcsncpy_s(t->sectionName, g_secName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&t->seq);
	g_pubHdr = hdr; g_pubState = g_state; g_pubVersion = g_wantVersion;
}

static void SetState(uint32_t st) { g_state = st; g_wantVersion++; }

static void ReleaseCopy()
{
	for (uint32_t i = 0; i < g_nItems; ++i)
	{
		if (g_items[i].staging) { g_items[i].staging->Release(); g_items[i].staging = nullptr; }
		free(g_items[i].obj); g_items[i].obj = nullptr;
	}
	free(g_image); g_image = nullptr;
	free(g_chunkSnap); g_chunkSnap = nullptr; g_chunkSnapBytes = 0;
	if (g_sec) { CloseHandle(g_sec); g_sec = nullptr; }
}

// The copy ends without READY (or a READY section is withdrawn): mode 2 = disabled for the process.
static void Fail(uint32_t fail, const char* why, bool forGood)
{
	ReleaseCopy();
	g_phase = PH_IDLE;
	g_fail = fail;
	SetState(VPWTAP_FAILED);
	WLOG("water_tap: FAILED %s (load %u copy %u): %s%s", fail < 10 ? kFailNames[fail] : "?", g_curLoad, g_copySeq, why,
		forGood ? " - DISABLED for the life of the process" : "");
	if (forGood) g_dead = true;
}

// ---- gathering ---------------------------------------------------------------------------------------------------------
static Item* NewItem(uint32_t system, uint32_t role)
{
	if (g_nItems >= g_capItems)
	{
		uint32_t cap = g_capItems ? g_capItems * 2 : 64;
		Item* n = (Item*)realloc(g_items, cap * sizeof(Item));
		if (!n) return nullptr;
		g_items = n; g_capItems = cap;
	}
	Item* it = &g_items[g_nItems++];
	memset(it, 0, sizeof(*it));
	it->r.system = system; it->r.role = role; it->r.vbRecord = it->r.ibRecord = 0xFFFFFFFFu;
	uint32_t idx = 0;
	for (uint32_t i = 0; i + 1 < g_nItems; ++i) if (g_items[i].r.system == system && g_items[i].r.role == role) idx++;
	it->r.index = idx;
	return it;
}

static uint32_t VaOf(uintptr_t a, uint32_t base) { return g_test ? 0 : (uint32_t)(a - base + VPEXE_IMAGE_BASE); }

// false with *fault on an access violation; false with g_why set on a failed check
static bool AddObject(uint32_t system, uintptr_t addr, uint32_t bytes, uint32_t exeOffset, uint32_t flags, uint32_t base, bool* fault)
{
	Item* it = NewItem(system, VPWROLE_OBJECT);
	if (!it) { _snprintf_s(g_why, _TRUNCATE, "out of memory (records)"); return false; }
	it->obj = (unsigned char*)malloc(bytes);
	if (!it->obj) { _snprintf_s(g_why, _TRUNCATE, "out of memory (%u bytes)", bytes); return false; }
	if (!SafeRead(addr, it->obj, bytes, fault))
	{
		_snprintf_s(g_why, _TRUNCATE, "%s reading %u bytes at 0x%08X", *fault ? "fault" : "address out of range", bytes, (uint32_t)addr);
		return false;
	}
	it->r.dataBytes = bytes; it->r.exeAddress = VaOf(addr, base); it->r.exeOffset = exeOffset; it->r.flags = flags;
	return true;
}

// A buffer handle -> VB or IB record with a staging copy issued. Returns the record index or -1 (g_why / *fault).
static int AddBuffer(const WaterTapEnv& env, uint32_t system, uint32_t h, bool ib, uint32_t exeOffset, bool* fault)
{
	const char* what = ib ? "IB" : "VB";
	if (!h || h >= WT_BUF_MAX) { _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X out of range (1..0x%X)", what, h, WT_BUF_MAX - 1); return -1; }
	uint32_t ptr = 0;
	if (!SafeRead((ib ? A.ibTab : A.vbTab) + 4u * h, &ptr, 4, fault) || !ptr)
	{ _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: %s", what, h, *fault ? "fault reading the buffer table" : "no buffer"); return -1; }
	uint32_t vt = 0;
	if (!SafeRead(ptr, &vt, 4, fault) || (vt != g_refVt[0] && vt != g_refVt[1] && vt != g_refVt[2]))
	{
		_snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: object 0x%08X (vtable 0x%08X) is not a D3D11 buffer%s", what, h, ptr, vt, *fault ? " (fault)" : "");
		*fault = false;   // an unreadable object pointer is a layout failure, not an EXE fault
		return -1;
	}
	unsigned char desc[WT_DESC_BYTES];
	if (!SafeRead((ib ? A.ibDesc : A.vbDesc) + WT_DESC_BYTES * h, desc, sizeof(desc), fault))
	{ _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: description unreadable", what, h); return -1; }
	D3D11_BUFFER_DESC bd = {};
	ID3D11Buffer* buf = BufferOf(ptr, &bd, fault);
	if (!buf) { _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: QueryInterface(ID3D11Buffer) failed%s", what, h, *fault ? " (fault)" : ""); return -1; }
	uint32_t stride = 0, count = 0, fmt = 0;
	const char* bad = nullptr;
	if (U32(desc, 0) != bd.ByteWidth) bad = "the EXE's size differs from the buffer's";
	else if (!(bd.BindFlags & (ib ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER))) bad = "wrong bind flags";
	else if (bd.ByteWidth == 0 || bd.ByteWidth > g_maxBytes) bad = "size 0 or above the image limit";
	else if (ib)
	{
		fmt = U32(desc, 0x2C);
		uint32_t e = fmt == DXGI_FORMAT_R16_UINT ? 2 : fmt == DXGI_FORMAT_R32_UINT ? 4 : 0;
		if (!e || bd.ByteWidth % e) bad = "index format not R16/R32 or size not a multiple of it";
		else { stride = e; count = bd.ByteWidth / e; }
	}
	else
	{
		uint32_t fid = U32(desc, 0x24);
		if (fid >= 0x100 || !SafeRead(A.stride + 4u * fid, &stride, 4, fault)) bad = *fault ? "fault reading the stride table" : "vertex format id out of range";
		else if (stride < 4 || stride > 256 || bd.ByteWidth % stride) bad = "stride out of range or size not a multiple of it";
		else count = bd.ByteWidth / stride;
		fmt = 0;
	}
	if (bad)
	{
		_snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: %s (EXE size %u, D3D size %u, bind 0x%X, stride %u, format %u)", what, h, bad,
			U32(desc, 0), bd.ByteWidth, bd.BindFlags, stride, ib ? U32(desc, 0x2C) : U32(desc, 0x24));
		buf->Release();
		return -1;
	}
	D3D11_BUFFER_DESC sd = {};
	sd.ByteWidth = bd.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Buffer* st = nullptr;
	HRESULT hr = env.dev->CreateBuffer(&sd, nullptr, &st);
	if (FAILED(hr)) { _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: staging buffer of %u bytes failed 0x%08X", what, h, bd.ByteWidth, hr); buf->Release(); return -2; }
	env.ctx->CopyResource(st, buf);
	buf->Release();
	Item* it = NewItem(system, ib ? VPWROLE_IB : VPWROLE_VB);
	if (!it) { st->Release(); _snprintf_s(g_why, _TRUNCATE, "out of memory (records)"); return -1; }
	it->staging = st;
	it->r.dataBytes = bd.ByteWidth; it->r.stride = ib ? 0 : stride; it->r.count = count; it->r.dxgiFormat = ib ? fmt : 0;
	it->r.flags = bd.Usage == D3D11_USAGE_IMMUTABLE ? 1u : bd.Usage == D3D11_USAGE_DYNAMIC ? 2u : 0u;
	it->r.exeAddress = VaOf((ib ? A.ibTab : A.vbTab) + 4u * h, env.exeBase); it->r.exeOffset = exeOffset;
	return (int)(g_nItems - 1);
}

// A draw record handle -> OBJECT record (128 bytes) after its checks. false on a failed check (g_why / *fault).
static bool AddDrawRecord(uint32_t system, uint32_t h, uint32_t material, uint32_t vb, uint32_t ib, uint32_t exeOffset,
                          uint32_t base, unsigned char* out, bool* fault)
{
	if (!h || h >= WT_REC_MAX) { _snprintf_s(g_why, _TRUNCATE, "draw record handle 0x%X out of range", h); return false; }
	uintptr_t a = A.recTab + WT_REC_BYTES * h;
	if (!AddObject(system, a, WT_REC_BYTES, exeOffset, 0, base, fault)) return false;
	memcpy(out, g_items[g_nItems - 1].obj, WT_REC_BYTES);
	if (U16(out, 0) != material || U32(out, 4) != ib || U32(out, 8) != vb)
	{
		_snprintf_s(g_why, _TRUNCATE, "draw record 0x%X: material %u IB 0x%X VB 0x%X, expected %u / 0x%X / 0x%X", h, U16(out, 0),
			U32(out, 4), U32(out, 8), material, ib, vb);
		return false;
	}
	return true;
}

static int AddDraw(uint32_t system, int vbRec, int ibRec, uint32_t indexCount, uint32_t material, uint32_t pass)
{
	Item* it = NewItem(system, VPWROLE_DRAW);
	if (!it) return -1;
	it->r.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	it->r.vbRecord = (uint32_t)vbRec; it->r.ibRecord = (uint32_t)ibRec;
	it->r.vertexCount = g_items[vbRec].r.count; it->r.indexCount = indexCount; it->r.material = material; it->r.pass = pass;
	it->vbCount = g_items[vbRec].r.count;
	return (int)(g_nItems - 1);
}

// 0 ok; else a VPShellWaterTapFail with g_why
static uint32_t Gather(const WaterTapEnv& env)
{
	bool fault = false;
	g_nItems = 0; g_systems = 0;
	const uint32_t base = env.exeBase;
	unsigned char river[WT_RIVER_BYTES], wave[WT_WAVE_BYTES], rec[WT_REC_BYTES];
	if (!SafeRead(A.river, river, sizeof(river), &fault) || !SafeRead(A.wave, wave, sizeof(wave), &fault))
	{ _snprintf_s(g_why, _TRUNCATE, "fault reading the river or wave object"); return fault ? VPWTAPF_EXCEPTION : VPWTAPF_LAYOUT; }
	memcpy(g_riverSnap, river, sizeof(river)); memcpy(g_waveSnap, wave, sizeof(wave));
#define WT_CHECK(ok) do { if (!(ok)) return fault ? VPWTAPF_EXCEPTION : VPWTAPF_LAYOUT; } while (0)
	// ---- rivers ----
	WT_CHECK(AddObject(VPWSYS_RIVER, A.river, WT_RIVER_BYTES, 0, 0, base, &fault));
	if (river[0x2C] == 1)
	{
		const uint32_t hr = U32(river, 0x18), hv = U32(river, 0x1C), hi = U32(river, 0x20), tris = U32(river, 0x28);
		WT_CHECK(AddDrawRecord(VPWSYS_RIVER, hr, WT_MAT_WATER, hv, hi, 0x18, base, rec, &fault));
		if (U32(rec, 0xC) != tris) { _snprintf_s(g_why, _TRUNCATE, "river draw record: %u triangles, River+0x28 says %u", U32(rec, 0xC), tris); return VPWTAPF_LAYOUT; }
		int vb = AddBuffer(env, VPWSYS_RIVER, hv, false, 0x1C, &fault);
		if (vb < 0) return vb == -2 ? VPWTAPF_D3D : fault ? VPWTAPF_EXCEPTION : VPWTAPF_LAYOUT;
		int ib = AddBuffer(env, VPWSYS_RIVER, hi, true, 0x20, &fault);
		if (ib < 0) return ib == -2 ? VPWTAPF_D3D : fault ? VPWTAPF_EXCEPTION : VPWTAPF_LAYOUT;
		if (!tris || (uint64_t)tris * 3 > g_items[ib].r.count)
		{ _snprintf_s(g_why, _TRUNCATE, "river: %u triangles, the index buffer holds %u indices", tris, g_items[ib].r.count); return VPWTAPF_LAYOUT; }
		uint32_t level = 0;
		WT_CHECK(SafeRead(A.passLevel, &level, 4, &fault) || (_snprintf_s(g_why, _TRUNCATE, "pass level unreadable"), false));
		if (AddDraw(VPWSYS_RIVER, vb, ib, tris * 3, WT_MAT_WATER, level > 2 ? 1u : 4u) < 0) { _snprintf_s(g_why, _TRUNCATE, "out of memory"); return VPWTAPF_LAYOUT; }
		g_systems |= 1u << VPWSYS_RIVER;
	}
	// ---- ocean: the Water object only (its geometry is made per frame by the draw, water-system.md 9) ----
	WT_CHECK(AddObject(VPWSYS_OCEAN, A.water, WT_WATER_BYTES, 0, 0, base, &fault));
	// ---- waves ----
	WT_CHECK(AddObject(VPWSYS_WAVE, A.wave, WT_WAVE_BYTES, 0, 0, base, &fault));
	const uint32_t chunks = U32(wave, 0x58), chunkPtr = U32(wave, 0x54), nInst = U32(wave, 0x60), instPtr = U32(wave, 0x5C);
	if (chunks)
	{
		if (chunks > WT_MAX_CHUNKS || !chunkPtr || nInst > chunks * WT_INST_PER_CHUNK || (nInst && !instPtr))
		{
			_snprintf_s(g_why, _TRUNCATE, "wave chunks out of range: %u chunks at 0x%08X, %u waves at 0x%08X", chunks, chunkPtr, nInst, instPtr);
			return VPWTAPF_LAYOUT;
		}
		WT_CHECK(AddObject(VPWSYS_WAVE, chunkPtr, chunks * WT_CHUNK_BYTES, 0x54, 0, base, &fault));
		g_chunkSnapBytes = chunks * WT_CHUNK_BYTES;
		g_chunkSnap = (unsigned char*)malloc(g_chunkSnapBytes);
		if (!g_chunkSnap) { _snprintf_s(g_why, _TRUNCATE, "out of memory"); return VPWTAPF_LAYOUT; }
		memcpy(g_chunkSnap, g_items[g_nItems - 1].obj, g_chunkSnapBytes);
		if (nInst) WT_CHECK(AddObject(VPWSYS_WAVE, instPtr, nInst * WT_INST_BYTES, 0x5C, 2u, base, &fault));
		for (uint32_t k = 0; k < chunks; ++k)
		{
			const unsigned char* c = g_chunkSnap + k * WT_CHUNK_BYTES;
			const uint32_t hr = U32(c, 0), hv = U32(c, 4), hi = U32(c, 8), first = U32(c, 0xC), n = U32(c, 0x10);
			if (n > WT_INST_PER_CHUNK || (uint64_t)first + n > nInst)
			{ _snprintf_s(g_why, _TRUNCATE, "wave chunk %u: waves %u + %u past the %u waves", k, first, n, nInst); return VPWTAPF_LAYOUT; }
			WT_CHECK(AddDrawRecord(VPWSYS_WAVE, hr, WT_MAT_WAVE, hv, hi, 0x54, base, rec, &fault));
			int vb = AddBuffer(env, VPWSYS_WAVE, hv, false, 0x54, &fault);
			if (vb < 0) return vb == -2 ? VPWTAPF_D3D : fault ? VPWTAPF_EXCEPTION : VPWTAPF_LAYOUT;
			int ib = AddBuffer(env, VPWSYS_WAVE, hi, true, 0x54, &fault);
			if (ib < 0) return ib == -2 ? VPWTAPF_D3D : fault ? VPWTAPF_EXCEPTION : VPWTAPF_LAYOUT;
			const uint32_t prims = U32(rec, 0xC);
			if (!prims || (uint64_t)prims * 3 > g_items[ib].r.count)
			{ _snprintf_s(g_why, _TRUNCATE, "wave chunk %u: %u triangles, the index buffer holds %u indices", k, prims, g_items[ib].r.count); return VPWTAPF_LAYOUT; }
			if (AddDraw(VPWSYS_WAVE, vb, ib, prims * 3, WT_MAT_WAVE, 0) < 0) { _snprintf_s(g_why, _TRUNCATE, "out of memory"); return VPWTAPF_LAYOUT; }
		}
		g_systems |= 1u << VPWSYS_WAVE;
	}
#undef WT_CHECK
	return VPWTAPF_NONE;
}

// Offsets of every data block and the image size; false (TOO_BIG) above the limits.
static bool Layout()
{
	if (g_nItems > VPSHELL_WATER_MAX_RECORDS)
	{ _snprintf_s(g_why, _TRUNCATE, "%u records, at most %u", g_nItems, VPSHELL_WATER_MAX_RECORDS); return false; }
	uint64_t off = sizeof(VPShellWaterImage) + (uint64_t)g_nItems * sizeof(VPShellWaterRecord);
	for (uint32_t i = 0; i < g_nItems; ++i)
	{
		off = (off + 15) & ~15ull;
		g_items[i].r.dataOffset = g_items[i].r.dataBytes ? off : 0;
		off += g_items[i].r.dataBytes;
	}
	g_imageBytes = off;
	if (off > g_maxBytes) { _snprintf_s(g_why, _TRUNCATE, "image of %llu bytes, the limit is %llu", (unsigned long long)off, (unsigned long long)g_maxBytes); return false; }
	g_image = (unsigned char*)calloc(1, (size_t)off);
	if (!g_image) { _snprintf_s(g_why, _TRUNCATE, "out of memory for an image of %llu bytes", (unsigned long long)off); return false; }
	for (uint32_t i = 0; i < g_nItems; ++i)
		if (g_items[i].obj) { memcpy(g_image + g_items[i].r.dataOffset, g_items[i].obj, (size_t)g_items[i].r.dataBytes); free(g_items[i].obj); g_items[i].obj = nullptr; }
	return true;
}

static void StartCopy(const WaterTapEnv& env, uint32_t loadSeq, uint32_t req)
{
	g_copySeq++;
	g_curLoad = loadSeq;
	g_started = true; g_startedLoad = loadSeq; g_startedReq = req;
	g_fail = VPWTAPF_NONE; g_secName[0] = 0; g_secBytes = 0; g_fnv = 0; g_imageBytes = 0; g_readyQpc = 0; g_copyMs = 0;
	g_copyQpc = Qpc(); g_copyCpuMs = 0; g_copyPresents = 0; g_mapTries = 0;
	ReadKeys();
	SetState(VPWTAP_WRITING);
	uint32_t f = Gather(env);
	if (f) { Fail(f, g_why, f == VPWTAPF_EXCEPTION); return; }
	if (!Layout()) { Fail(VPWTAPF_TOO_BIG, g_why, false); return; }
	if (g_test == T_CHANGE) Put32(g_tw->river, 0x28, U32(g_tw->river, 0x28) + 1);   // "a load started" during the copy
	g_phase = PH_MAP;
	uint32_t bufs = 0; for (uint32_t i = 0; i < g_nItems; ++i) bufs += g_items[i].staging != nullptr;
	WLOG("water_tap: copy %u of load %u started: %u records (%u buffers on staging copies), image %llu bytes, systems 0x%X",
		g_copySeq, g_curLoad, g_nItems, bufs, (unsigned long long)g_imageBytes, g_systems);
}

// After the maps: indices, a re-read of the objects, hashes, the section. false = failed (published).
static bool Finish(const WaterTapEnv& env)
{
	// every index of a draw below its VB's vertex count
	for (uint32_t i = 0; i < g_nItems; ++i)
	{
		const VPShellWaterRecord& d = g_items[i].r;
		if (d.role != VPWROLE_DRAW) continue;
		VPShellWaterRecord& ib = g_items[d.ibRecord].r;
		const unsigned char* p = g_image + ib.dataOffset;
		for (uint32_t k = 0; k < d.indexCount; ++k)
		{
			uint32_t v = ib.dxgiFormat == DXGI_FORMAT_R32_UINT ? U32(p, 4 * k) : U16(p, 2 * k);
			if (v >= d.vertexCount)
			{
				_snprintf_s(g_why, _TRUNCATE, "system %u draw %u: index %u = %u, the vertex buffer holds %u vertices", d.system, d.index, k, v, d.vertexCount);
				Fail(VPWTAPF_LAYOUT, g_why, false);
				return false;
			}
		}
		ib.flags |= 4u;
	}
	// the objects must not have changed while the copies were in flight
	bool fault = false;
	unsigned char river[WT_RIVER_BYTES], wave[WT_WAVE_BYTES];
	unsigned char* chunks = g_chunkSnapBytes ? (unsigned char*)malloc(g_chunkSnapBytes) : nullptr;
	bool same = SafeRead(A.river, river, sizeof(river), &fault) && SafeRead(A.wave, wave, sizeof(wave), &fault) &&
		(!g_chunkSnapBytes || (chunks && SafeRead(U32(g_waveSnap, 0x54), chunks, g_chunkSnapBytes, &fault)));
	if (!same && fault) { free(chunks); Fail(VPWTAPF_EXCEPTION, "fault re-reading the objects after the copy", true); return false; }
	same = same && !memcmp(river, g_riverSnap, sizeof(river)) && !memcmp(wave + 0x54, g_waveSnap + 0x54, 8) &&
		(!g_chunkSnapBytes || !memcmp(chunks, g_chunkSnap, g_chunkSnapBytes));
	free(chunks);
	if (!same) { Fail(VPWTAPF_CHANGED, "the river or wave objects changed during the copy: a load started", false); return false; }
	// header, records, hashes
	VPShellWaterImage h = {};
	h.magic = VPSHELL_WATER_MAGIC; h.version = VPSHELL_WATER_IMAGE_VERSION; h.headerBytes = sizeof(VPShellWaterImage);
	h.recordBytes = sizeof(VPShellWaterRecord); h.recordCount = g_nItems; h.loadSeq = g_curLoad; h.exePid = GetCurrentProcessId();
	if (env.cam && env.cam->gridValid) { h.mapW = env.cam->samplesX / 64; h.mapH = env.cam->samplesY / 64; }
	float wz = 0; if (SafeRead(A.waterZ, &wz, 4, &fault)) h.waterZ = wz;
	h.imageBytes = g_imageBytes;
	for (uint32_t i = 0; i < g_nItems; ++i)
	{
		VPShellWaterRecord& r = g_items[i].r;
		r.fnv1a64 = r.dataBytes ? Fnv(0xCBF29CE484222325ull, g_image + r.dataOffset, (size_t)r.dataBytes) : 0;
		memcpy(g_image + sizeof(h) + (size_t)i * sizeof(VPShellWaterRecord), &r, sizeof(r));
	}
	memcpy(g_image, &h, sizeof(h));
	g_fnv = Fnv(0xCBF29CE484222325ull, g_image, (size_t)g_imageBytes);
	// the section, written through short views
	g_secBytes = (g_imageBytes + 0xFFFF) & ~0xFFFFull;
	if (g_test == T_BADSECTION) _snwprintf_s(g_secName, _TRUNCATE, L"Local\\VPShellWater\\no\\such\\%u", g_copySeq);
	else _snwprintf_s(g_secName, _TRUNCATE, VPSHELL_WATER_SECTION_FMT, GetCurrentProcessId(), g_curLoad, g_copySeq);
	g_sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, (DWORD)(g_secBytes >> 32), (DWORD)g_secBytes, g_secName);
	DWORD err = GetLastError();
	if (!g_sec || err == ERROR_ALREADY_EXISTS)
	{
		_snprintf_s(g_why, _TRUNCATE, g_sec ? "section '%ls' already exists (another holder)" : "CreateFileMapping('%ls', %llu bytes) failed, error %lu",
			g_secName, (unsigned long long)g_secBytes, err);
		g_secName[0] = 0;
		Fail(VPWTAPF_SECTION, g_why, false);
		return false;
	}
	uint32_t views = 0;
	for (uint64_t off = 0; off < g_imageBytes; off += WT_VIEW_BYTES)
	{
		const uint64_t n = g_imageBytes - off < WT_VIEW_BYTES ? g_imageBytes - off : WT_VIEW_BYTES;
		void* v = MapViewOfFile(g_sec, FILE_MAP_WRITE, (DWORD)(off >> 32), (DWORD)off, (SIZE_T)n);
		if (!v) { _snprintf_s(g_why, _TRUNCATE, "MapViewOfFile at %llu failed, error %lu", (unsigned long long)off, GetLastError()); Fail(VPWTAPF_SECTION, g_why, false); return false; }
		memcpy(v, g_image + off, (size_t)n);
		UnmapViewOfFile(v);
		views++;
	}
	free(g_image); g_image = nullptr;
	free(g_chunkSnap); g_chunkSnap = nullptr; g_chunkSnapBytes = 0;
	g_readyQpc = Qpc();
	g_copyCpuMs += Ms(g_readyQpc - g_t0);   // this Present's share (the earlier ones are added at their end)
	g_copyMs = (float)Ms(g_readyQpc - g_copyQpc);
	g_phase = PH_WAIT;
	SetState(VPWTAP_READY);
	uint32_t vb = 0, ib = 0, dr = 0; uint64_t bufBytes = 0;
	for (uint32_t i = 0; i < g_nItems; ++i)
	{
		const VPShellWaterRecord& r = g_items[i].r;
		vb += r.role == VPWROLE_VB; ib += r.role == VPWROLE_IB; dr += r.role == VPWROLE_DRAW;
		if (r.role == VPWROLE_VB || r.role == VPWROLE_IB) bufBytes += r.dataBytes;
	}
	WLOG("water_tap: READY load %u copy %u: %u records (%u VB, %u IB, %u draws; %llu buffer bytes), systems 0x%X, image %llu bytes in '%ls' "
		"(%u views); fnv1a64 %016llX; water z %.3f; wall %.1f ms over %u Presents, composite CPU %.3f ms",
		g_curLoad, g_copySeq, g_nItems, vb, ib, dr, (unsigned long long)bufBytes, g_systems, (unsigned long long)g_imageBytes, g_secName,
		views, (unsigned long long)g_fnv, h.waterZ, g_copyMs, g_copyPresents + 1, g_copyCpuMs);
	g_lastCopyCpuMs = (float)g_copyCpuMs;
	return true;
}

static void MapStep(const WaterTapEnv& env)
{
	g_mapTries++;
	bool pending = false;
	for (uint32_t i = 0; i < g_nItems; ++i)
	{
		Item& it = g_items[i];
		if (!it.staging) continue;
		D3D11_MAPPED_SUBRESOURCE m = {};
		HRESULT hr = env.ctx->Map(it.staging, 0, D3D11_MAP_READ, g_mapTries > WT_MAP_TRIES ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
		if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { pending = true; continue; }
		if (FAILED(hr) || !m.pData)
		{
			_snprintf_s(g_why, _TRUNCATE, "Map of the staging copy of record %u failed 0x%08X", i, hr);
			Fail(VPWTAPF_D3D, g_why, false);
			return;
		}
		memcpy(g_image + it.r.dataOffset, m.pData, (size_t)it.r.dataBytes);
		env.ctx->Unmap(it.staging, 0);
		it.staging->Release(); it.staging = nullptr;
	}
	if (!pending) Finish(env);
}

static void WaitStep(const WaterTapEnv& env)
{
	VPShellWaterTap* t = VPSHELL_WATERTAP(env.hdr);
	if (t->ackLoadSeq == g_curLoad && t->ackCopySeq == g_copySeq)
	{
		MemoryBarrier();
		const int32_t res = t->ackResult; const uint32_t pid = t->ackServerPid;
		CloseHandle(g_sec); g_sec = nullptr;
		g_phase = PH_IDLE;
		SetState(VPWTAP_RELEASED);
		WLOG("water_tap: RELEASED load %u copy %u: ack from pid %u, result %d%s, %.0f ms after READY; section closed", g_curLoad, g_copySeq, pid, res,
			res == 0 ? "" : " (the consumer refused the copy)", Ms(Qpc() - g_readyQpc));
		return;
	}
	if (Ms(Qpc() - g_readyQpc) > g_ackTimeoutMs)
	{
		_snprintf_s(g_why, _TRUNCATE, "no ack within %u ms; section closed", g_ackTimeoutMs);
		Fail(VPWTAPF_ACK_TIMEOUT, g_why, false);
	}
}

// ---- the per-Present entry ----------------------------------------------------------------------------------------------
void WaterTap_OnPresent(const WaterTapEnv& env)
{
	if (!g_qpcMs) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcMs = 1000.0 / (double)f.QuadPart; }
	g_log = env.log;
	if (!env.enabled)
	{
		if (g_phase != PH_IDLE) Fail(VPWTAPF_KILLED, "water_tap=0", false);
		if (g_state != VPWTAP_OFF) { SetState(VPWTAP_OFF); g_fail = VPWTAPF_NONE; g_started = false; WLOG("water_tap: OFF (water_tap=0)"); }
		if (env.hdr && (g_pubHdr != env.hdr || g_pubVersion != g_wantVersion)) Publish(env.hdr);
		return;
	}
	if (g_state == VPWTAP_OFF) SetState(VPWTAP_NONE);
	if (g_dead || !env.dev || !env.ctx)
	{
		if (env.hdr && (g_pubHdr != env.hdr || g_pubVersion != g_wantVersion)) Publish(env.hdr);
		return;
	}
	const int64_t t0 = Qpc();
	g_t0 = t0;
	if (!g_init)
	{
		g_init = true;
		ReadTestKey();
		if (!InitDevice(env.dev)) { Fail(VPWTAPF_D3D, "D3D11 set-up failed", true); Publish(env.hdr); return; }
		if (g_test)
		{
			WLOG("water_tap: TEST MODE water_test=%s: synthetic objects in this DLL, the EXE is not read", kTestNames[g_test]);
			g_patternsOk = BuildTestWorld(env.dev);
			if (!g_patternsOk) { Fail(VPWTAPF_D3D, "test world could not be built", true); Publish(env.hdr); return; }
			TestWorld& w = *g_tw;
			A.river = (uintptr_t)w.river; A.water = (uintptr_t)w.water; A.wave = (uintptr_t)w.wave;
			A.vbTab = (uintptr_t)w.vbTab; A.vbDesc = (uintptr_t)w.vbDesc; A.ibTab = (uintptr_t)w.ibTab; A.ibDesc = (uintptr_t)w.ibDesc;
			A.stride = (uintptr_t)w.stride; A.recTab = (uintptr_t)w.recTab; A.passLevel = (uintptr_t)&w.passLevel; A.waterZ = (uintptr_t)&w.grid[1];
		}
		else
		{
			if (!env.exeValidated) WLOG("water_tap: REFUSED: the composite did not validate the EXE image - the tap never reads it");
			const int build = VPCamBuild();   // the build the composite validated (ticket 4964)
			g_patternsOk = env.exeValidated && env.exeBase && CheckPatterns(build, env.exeBase);
			if (!g_patternsOk) { Fail(VPWTAPF_REFUSED, "EXE refused", true); Publish(env.hdr); return; }
			const uintptr_t e = env.exeBase;
			A.river = VpExeAddr(build, VPX_water_river, e); A.water = VpExeAddr(build, VPX_water_water, e);
			A.wave = VpExeAddr(build, VPX_water_wave, e);
			A.vbTab = VpExeAddr(build, VPX_water_vbtab, e); A.vbDesc = VpExeAddr(build, VPX_water_vbdesc, e);
			A.ibTab = VpExeAddr(build, VPX_water_ibtab, e); A.ibDesc = VpExeAddr(build, VPX_water_ibdesc, e);
			A.stride = VpExeAddr(build, VPX_water_stride, e); A.recTab = VpExeAddr(build, VPX_pxw_drawobj_pool, e);
			A.passLevel = VpExeAddr(build, VPX_water_passlevel, e); A.waterZ = VpExeAddr(build, VPX_cam_grid, e) + 4;
			WLOG("water_tap: EXE patterns match (%u) - tap enabled (River 0x%08X, Water 0x%08X, Wave_System 0x%08X)",
				(unsigned)(sizeof(kWtSigs) / sizeof(kWtSigs[0])), (uint32_t)A.river, (uint32_t)A.water, (uint32_t)A.wave);
		}
	}
	// the load: the camera block's gridSeq (+ the test's trigger file)
	if (g_test)
	{
		wchar_t trig[MAX_PATH]; _snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.waterload", g_dir);
		if (GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig)) { g_testBump++; WLOG("water_tap: test: simulated new load (+%u)", g_testBump); }
	}
	const uint32_t gridSeq = env.cam && env.cam->gridValid ? env.cam->gridSeq : 0;
	const uint32_t loadSeq = g_test ? (gridSeq ? gridSeq : 1) + g_testBump : gridSeq;
	if (g_phase != PH_IDLE && loadSeq != g_curLoad)
	{
		_snprintf_s(g_why, _TRUNCATE, "a new load (seq %u) superseded the %s copy; section closed", loadSeq, g_phase == PH_WAIT ? "unacknowledged" : "unfinished");
		Fail(VPWTAPF_CHANGED, g_why, false);
	}
	if (g_phase == PH_MAP) { g_copyPresents++; MapStep(env); }
	else if (g_phase == PH_WAIT) { if (env.hdr) WaitStep(env); }
	else if (loadSeq && env.hdr)
	{
		if (loadSeq != g_runSeq || !env.camOk) { g_okRun = 0; g_sceneRun = 0; g_runSeq = loadSeq; }
		if (env.camOk)
		{
			if (env.sceneThisFrame) g_sceneRun++;
			g_okRun++;
			const uint32_t req = VPSHELL_WATERTAP(env.hdr)->requestSeq;
			// ticket 0408: on a process's first load (the composite's load gate, env.firstLoadOpen) the copy starts under the
			// load screen; on a reload it waits for the Presents and scene-clear frames as before
			const bool gate = env.firstLoadOpen || (g_okRun >= g_delayFrames && g_sceneRun >= g_sceneFrames);
			if (gate && !(g_started && g_startedLoad == loadSeq && g_startedReq == req))
			{
				if (env.firstLoadOpen && !(g_started && g_startedLoad == loadSeq))
					WLOG("water_tap: load %u: first-load gate (camera OK for %u Presents, %u scene-clear frames)", loadSeq, g_okRun, g_sceneRun);
				StartCopy(env, loadSeq, req);
			}
		}
	}
	const double ms = Ms(Qpc() - t0);
	if (g_phase == PH_MAP) g_copyCpuMs += ms;   // the READY Present adds its own share in Finish
	g_workMs += ms; g_workN++; if (ms > g_workMaxMs) g_workMaxMs = ms;
	if (g_workN >= 10000)
	{
		if (g_log && g_costLogs++ < 200) g_log("water_tap: cost over %u Presents: mean %.4f ms, max %.3f ms per Present (render thread); state %u, %u copies so far",
			g_workN, g_workMs / g_workN, g_workMaxMs, g_state, g_copySeq);
		g_workMs = 0; g_workMaxMs = 0; g_workN = 0;
	}
	if (env.hdr && (g_pubHdr != env.hdr || g_pubVersion != g_wantVersion)) Publish(env.hdr);
}

void WaterTap_Shutdown(const char* why)
{
	// A copy on staging buffers dies with the device; a READY section does not depend on it and keeps waiting for its ack.
	if (g_phase == PH_MAP)
	{
		WLOG("water_tap: copy %u of load %u dropped (%s)", g_copySeq, g_curLoad, why ? why : "");
		Fail(VPWTAPF_D3D, "the composite released its device resources during the copy", false);
		g_started = false;   // copy again on the new device
	}
	if (g_phase == PH_IDLE) ReleaseCopy();
	FreeTestWorld();
	g_refVt[0] = g_refVt[1] = g_refVt[2] = 0;
	if (!g_dead) g_init = false;
}
