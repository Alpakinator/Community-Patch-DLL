// ice_tap.cpp - the ice tap inside shell_composite.dll (epic 0169, issue 4307; route (a) of polar-ice.md 5).
// Written 2026-11-15 for this repository (GPL-3.0-or-later). Contract: ice_tap.h; layout: shell/common/shell_ice_tap.h.
// What the EXE's objects are, and why each byte pattern below is checked: research/05-graphics-composition/polar-ice.md
// sections 3, 5 and 9 (behaviour, D83; nothing of the EXE is in this file but the patterns' few bytes, as in water_tap.cpp).
//
// What it does:
//   WATCH (every map Present): reads the Ice object's built flag (+0xFEA5), VB / IB handles (+0xFEE4, +0xFEE8) and chunk
//      array (+0xFF2C, +0xFF30), 20 bytes. When they (or the camera block's gridSeq) differ from the last copied state and
//      have stayed the same for ice_settle_frames map Presents, a copy starts (or NO_MESH is published when the object is
//      not built or has no chunks).
//   GATHER (one Present): the fields Ice+0xFE80..+0x1001F, the chunk array, each chunk's draw record (material 0x3B, its
//      IB / VB handles = the object's, triangles, start index, base vertex); the VB and IB handles resolved through the
//      renderer's tables (handle -> ID3D11Buffer*, handle -> the EXE's own description, vertex format -> stride) after a
//      vtable check against D3D11 buffers this file created itself; every description and range checked; one
//      CopyResource per buffer into a STAGING buffer on the EXE's device.
//   MAP (the next Presents): maps both staging buffers with DO_NOT_WAIT; checks every chunk's indices against the VB;
//      re-reads the object, the chunk array and the records (a load or an Ice_Reload meanwhile: CHANGED, copied again);
//      builds the image, hashes it, writes it into Local\VPShellIce.<pid>.<generation> through views of at most 4 MiB,
//      unmapped at once; closes the previous generation's section; publishes READY.
// Safety: every read of EXE memory goes through SafeRead (bounds + SEH); a fault disables the tap for the process
// (EXCEPTION); a failed pattern check refuses the EXE for good (PATTERN, with the pattern's name in failText for D125); a
// refused buffer description (DESC) or another layout check (LAYOUT) fails this mesh only. It calls no EXE function.
//
// Test seam (as water_test): ice_test=<mode> (ini or VPSHELL_ICE_TEST) builds a synthetic EXE (the Ice object, a chunk
// array, draw records, the renderer's tables, IMMUTABLE D3D11 buffers of known content) in this DLL's memory and runs the
// same code on it; ice_test=patterns checks the real pattern list against a synthetic image (ice_test_pattern=k breaks
// pattern k). A file `shell_composite.iceload` next to the DLL simulates an Ice_Reload in test mode (deleted when seen).
#include "ice_tap.h"
#include "exe_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---- EXE layout (DX11 EXE, both known builds; VAs at ImageBase 0x400000; polar-ice.md 3, water-system.md 9.1) -------
// Ticket 4964 (epic 0590): every address is a build-table symbol read for the build the composite validated: ice_ice
// (Ice = TerrainSystem + 0x529BC) and the renderer tables the water tap reads too, water_vbtab / water_ibtab (handle ->
// ID3D11Buffer*), water_vbdesc (VB description, 0x30 bytes: +0 ByteWidth, +0x24 vertex format), water_ibdesc (+0x2C DXGI
// format), water_stride, pxw_drawobj_pool (0x80-byte draw records). EXE addresses in this file's comments are build 4390913's.
#define IT_OFF_BUILT 0xFEA5u
#define IT_OFF_VB 0xFEE4u
#define IT_OFF_IB 0xFEE8u
#define IT_OFF_CHUNKS 0xFF2Cu
#define IT_OFF_COUNT 0xFF30u
#define IT_CHUNK_BYTES 0x14u
#define IT_VFMT 0x16u
#define IT_MATERIAL 0x3Bu
#define IT_BUF_MAX 0x1800u
#define IT_REC_MAX 0x20000u
#define IT_DESC_BYTES 0x30u
#define IT_REC_BYTES 0x80u
#define IT_VIEW_BYTES (4u * 1024 * 1024)
#define IT_MAP_TRIES 240            // Presents of DO_NOT_WAIT maps before one blocking map

// Byte patterns checked before the first read of the real EXE (R(X) = 4 bytes holding VA X rebased). Checked against the
// EXE on disk by shell/tests/test_ice_tap.py (test_ice_signatures_match_exe_on_disk).
// X(symbol, description, bytes), R(symbol) = 4 bytes holding that symbol's VA in the running build, rebased
// (exe_camera.h; checked on both builds' golden EXEs by tools/exebuild/gen_table.py through exebuild/patterns.txt).
#define IT_SIGNATURES(X) \
	X(ice_createterrain, "CreateTerrain: the Ice object TerrainSystem+0x529BC passed to the Ice step", "8D 8B BC 29 05 00 E8 9F 5C E9 FF") \
	X(ice_reload, "Ice_Reload: the Ice object and the grid", "68 R(cam_grid) B9 R(ice_ice) E8") \
	X(ice_step_built, "Ice step: the builder, then the built flag Ice+0xFEA5 = 1", "E8 E8 4A EF FF 5F C6 86 A5 FE 00 00 01") \
	X(ice_builder_vb, "ice builder: the VB of vertex format 0x16 made by the VB create", "6A 16 E8 5C 05 43 00") \
	X(ice_builder_vbhandle, "ice builder: the VB handle stored at Ice+0xFEE4", "89 86 E4 FE 00 00") \
	X(ice_builder_ib, "ice builder: the IB made by the IB create, its handle stored at Ice+0xFEE8", "E8 EE 06 43 00 89 86 E8 FE 00 00") \
	X(ice_builder_chunksize, "ice builder: chunk array entries of 0x14 bytes", "BA 14 00 00 00 F7 E2") \
	X(ice_builder_chunks, "ice builder: the chunk array at Ice+0xFF2C, its count at Ice+0xFF30", "89 96 2C FF 00 00 89 86 30 FF 00 00") \
	X(ice_builder_record, "ice builder: a draw record of material 0x3B (IceShader) per chunk, its handle at chunk+0x10", "6A 3B 8D 54 19 10 8B 0D R(pxw_renderer) 52 8D 44 24 18 50 E8 C0 0C 43 00") \
	X(ice_builder_rec_ibvb, "ice builder: record +0x04 the IB handle, +0x08 the VB handle", "8B 96 E8 FE 00 00 8B 44 24 10 89 50 04 8B 8E E4 FE 00 00 8B 54 24 10 89 4A 08") \
	X(ice_builder_rec_tris, "ice builder: record +0x0C triangles = indices / 3", "B8 AB AA AA AA F7 64 39 0C 8B 44 24 10 D1 EA 89 50 0C") \
	X(ice_builder_rec_start, "ice builder: record +0x10 start index, +0x14 base vertex", "8B 54 39 04 8B 44 24 10 89 50 10 8B 4C 24 18 8B 14 39 8B 44 24 10 89 50 14") \
	X(water_vbcreate_desc, "VB create: description table, 0x30 bytes per handle", "8D 34 7F C1 E6 04 81 C6 R(water_vbdesc)") \
	X(water_vbcreate_stride, "VB create: stride table", "8B 14 8D R(water_stride)") \
	X(water_vbcreate_buf, "VB create: buffer table", "8D 0C BD R(water_vbtab)") \
	X(water_ibcreate_desc, "IB create: description table", "8D 34 7F C1 E6 04 81 C6 R(water_ibdesc)") \
	X(water_ibcreate_buf, "IB create: buffer table", "8D 14 BD R(water_ibtab)") \
	X(water_ibcreate_fmt, "IB create: DXGI format at description+0x2C", "89 46 2C") \
	X(water_pool_cap, "VB/IB handle pool: capacity 0x1800", "3D 00 18 00 00") \
	X(water_reccreate, "draw record create: the records table, 0x80 bytes each", "C1 E0 07 68 80 00 00 00 8D B0 R(pxw_drawobj_pool)") \
	X(water_recpool_cap, "draw record pool: capacity 0x20000", "3D 00 00 02 00")
static const VPExeSig kItSigs[] = { IT_SIGNATURES(VPEXE_SIG_ENTRY) };
#define IT_NSIGS ((int)(sizeof(kItSigs) / sizeof(kItSigs[0])))

// ---- state -----------------------------------------------------------------------------------------------------------
enum { PH_IDLE, PH_MAP, PH_WORK };   // PH_WORK (ticket 0385): the staging buffers mapped, the worker builds the image
struct Key { uint32_t load, built, vb, ib, chunks, count; };
struct Buf { ID3D11Buffer* staging; uint32_t bytes; uint32_t count; };

// Ticket 0385 / 0384: the worker's input (owned by the copy; the render thread does not touch it while the worker runs) and
// its result (read by the render thread only after the worker ended).
struct WorkJob
{
	const unsigned char* vb; const unsigned char* ib;   // the mapped staging memory (unmapped by the render thread afterwards)
	uint32_t generation, load, vbHandle, ibHandle;
	uint64_t maxBytes;
	bool badSection;                                      // ice_test=badsection
	// result
	uint32_t fail;                                        // VPShellIceTapFail; 0 = the section is written
	char why[256];
	HANDLE sec; wchar_t name[64];
	uint64_t imageBytes, fnv;
	uint32_t views;
	double ms;                                            // the worker's wall time
};

static bool g_init, g_dead, g_patternsOk;
static int g_phase;
static uint32_t g_state = VPICE_NONE, g_fail;
static uint32_t g_gen, g_copies;
static Key g_runKey, g_doneKey, g_copyKey;
static bool g_haveDone;
static uint32_t g_stable;
static HANDLE g_ctl; static VPShellIceTapControl* g_blk;
static HANDLE g_img;                       // the current READY generation's section (kept until replaced)
static wchar_t g_imgName[64];
static uint64_t g_imgBytes, g_imgFnv;
static uint32_t g_imgLoad, g_imgChunks, g_imgVb, g_imgIb;
static Buf g_vb, g_ib;
static unsigned char g_fields[VPSHELL_ICE_FIELDS_BYTES];
static unsigned char* g_chunkSnap; static uint32_t g_chunkSnapBytes;
static unsigned char* g_recSnap;            // count x 0x80 bytes (the chunks' draw records)
static VPShellIceChunk* g_chunks; static uint32_t g_nChunks;
static uint32_t g_mapTries, g_copyPresents;
static int64_t g_copyQpc, g_readyQpc, g_t0;
static double g_copyCpuMs, g_qpcMs;
static float g_copyMs, g_lastCpuMs;
static uintptr_t g_refVt[3];
static void (*g_log)(const char*, ...);
static uint32_t g_logs;
static int g_test, g_testPattern = -1;
static uint32_t g_testReload;
static wchar_t g_dir[MAX_PATH];
static uint32_t g_settleFrames = 30;
static uint64_t g_maxBytes = VPSHELL_ICE_MAX_BYTES;
static uint32_t g_ackLogged;
static bool g_dirty = true;
static char g_why[256];
// ticket 0385: the worker (0384) and the reader gate
static HANDLE g_worker;
static WorkJob g_job;
static ID3D11DeviceContext* g_mapCtx;      // the context the staging buffers are mapped on (PH_WORK)
static double g_maxPresentMs, g_presentMs; // the copy's most expensive Present on the render thread; this Present's so far
static bool g_copyActive;                 // a copy's Presents are being timed (from StartCopy to READY or a failure)
static int g_useWorker = 1;               // ice_worker: 0 = the image built on the render thread (the cost before 0385)
static uint32_t g_unreadMs = 10000;
static uint32_t g_offerPid, g_offerGen;    // the server process offered the newest image (its pid, 0 = not named), the generation
static bool g_offered;                     // that image is still published (CloseImage clears it: nothing can ack it then)
static int64_t g_offerQpc;
static const VPShellHeader* g_hdrNow;     // env.hdr of this Present
static bool g_deferred; static Key g_deferKey;
static char g_deferWhy[200];
struct Addr { uintptr_t ice, vbTab, vbDesc, ibTab, ibDesc, stride, recTab; };
static Addr A;

enum { T_NONE, T_OK, T_PATTERNS, T_NOBUILT, T_NULLVB, T_BADVFMT, T_BADSIZE, T_IBFORMAT, T_CHUNKPAST, T_BASEPAST, T_BADRECORD,
       T_HUGECOUNT, T_GARBAGE, T_CHANGE, T_BADINDEX, T_BADSECTION, T_COUNT };
static const char* kTestNames[T_COUNT] = { "", "ok", "patterns", "nobuilt", "nullvb", "badvfmt", "badsize", "ibformat", "chunkpast",
                                           "basepast", "badrecord", "hugecount", "garbage", "change", "badindex", "badsection" };
static const char* kFailNames[] = { "NONE", "PATTERN", "EXCEPTION", "LAYOUT", "DESC", "D3D", "SECTION", "TOO_BIG", "CHANGED", "KILLED" };
static const char* kStateNames[] = { "NONE", "WRITING", "READY", "FAILED", "OFF", "NO_MESH" };

#define ILOG(...) do { if (g_log && g_logs < 400) { g_logs++; g_log(__VA_ARGS__); } } while (0)

static int64_t Qpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static double Ms(int64_t d) { return d * g_qpcMs; }

// ---- guarded reads (as water_tap.cpp: a byte loop under SEH, never memcpy on a wild address) -------------------------
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
static void PutF(unsigned char* b, uint32_t off, float v) { memcpy(b + off, &v, 4); }

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
// Content (shell/tests/test_ice_tap.py recomputes it): VB 160 vertices (1280 bytes), byte j = (31 j + 17 b + 5) & 0xFF with
// b = 1; IB 30 R16 indices: chunk 0 indices 0..17 = (5 i + 1) % 20 (6 triangles, start 0, base 0), chunk 1 indices
// 18..29 = (5 i + 2) % 20 (4 triangles, start 18, base 20). Twin handles of the same content: VB 7 / 8, IB 9 / 10 (an
// Ice_Reload swaps them); records 5 and 6; vertex format 0x16 stride 8. Fields: byte i of Ice+0xFE80+i = (7 i + 3) &
// 0xFF, then the floats the draw reads (TestFloat) and the head (built, handles, chunk array, count) written over it.
struct TestWorld
{
	unsigned char* ice;          // the object: ice + 0xFE80 .. + 0x10020 used
	unsigned char chunks[2 * IT_CHUNK_BYTES];
	uintptr_t vbTab[16], ibTab[16];
	unsigned char vbDesc[16 * IT_DESC_BYTES], ibDesc[16 * IT_DESC_BYTES];
	uint32_t stride[64];
	unsigned char recTab[8 * IT_REC_BYTES];
	uintptr_t fakeObj[4];
	ID3D11Buffer* bufs[4];
	void* reserved;
	unsigned char* image;        // ice_test=patterns: the synthetic image the patterns are checked against
};
static TestWorld* g_tw;

static ID3D11Buffer* TestBuffer(ID3D11Device* dev, bool ib)
{
	unsigned char p[1280];
	uint32_t bytes = ib ? 60u : 1280u;
	if (!ib) for (uint32_t j = 0; j < bytes; ++j) p[j] = (unsigned char)((31 * j + 17 * 1 + 5) & 0xFF);
	else
		for (uint32_t i = 0; i < 30; ++i)
		{
			uint16_t v = (uint16_t)(i < 18 ? (5 * i + 1) % 20 : (5 * i + 2) % 20);
			if (g_test == T_BADINDEX && i == 25) v = 140;  // + base 20 = vertex 160: past the VB
			memcpy(p + 2 * i, &v, 2);
		}
	D3D11_BUFFER_DESC d = {};
	d.ByteWidth = bytes; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = ib ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA sd = { p, 0, 0 };
	ID3D11Buffer* b = nullptr;
	if (FAILED(dev->CreateBuffer(&d, &sd, &b))) b = nullptr;
	return b;
}

// The floats the draw reads (polar-ice.md 7): plausible values, all finite, the box min <= max.
static float TestFloat(uint32_t off, bool* is)
{
	*is = true;
	switch (off)
	{
	case 0xFF0C: return 0.18431f;  case 0xFF14: return 10.1167f;  case 0xFF18: return 25.0f;
	case 0xFF1C: return -1793.0f;  case 0xFF20: return -1098.5f;  case 0xFF24: return 1823.9f;  case 0xFF28: return 1043.1f;
	case 0xFF38: return 0.433f;    case 0xFF3C: return -0.25f;    case 0xFF40: return 0.866f;
	case 0xFF44: return 0.0f;      case 0xFF48: return -0.4226f;  case 0xFF4C: return 0.9063f;
	case 0xFFC0: return 25.0f;     case 0xFFC4: return 8.0f;      case 0xFFD8: return 0.005f;   case 0xFFDC: return 0.009f;
	case 0xFFE0: return 150.0f;    case 0xFFE4: case 0xFFE8: return 0.005f;  case 0xFFEC: case 0xFFF0: return 0.05f;
	case 0xFFF4: return 0.0033f;   case 0xFFF8: return 0.2758f;   case 0xFFFC: return 0.6187f;
	case 0x10000: case 0x1000C: return 0.0329f;  case 0x10004: case 0x10010: return 0.3297f;  case 0x10008: case 0x10014: return 0.145f;
	case 0x10018: return 0.1f;     case 0x1001C: return 0.5f;
	default: *is = false; return 0;
	}
}

static void TestDesc(unsigned char* d, uint32_t bytes, uint32_t usage, uint32_t off, uint32_t v)
{
	memset(d, 0, IT_DESC_BYTES); Put32(d, 0, bytes); Put32(d, 4, usage); Put32(d, off, v);
}

static void TestRecord(unsigned char* rec, uint16_t material, uint32_t ib, uint32_t vb, uint32_t tris, uint32_t start, int32_t base)
{
	memset(rec, 0, IT_REC_BYTES);
	memcpy(rec, &material, 2); Put32(rec, 4, ib); Put32(rec, 8, vb); Put32(rec, 0xC, tris); Put32(rec, 0x10, start);
	Put32(rec, 0x14, (uint32_t)base); rec[0x7C] = 0x0B;
}

static unsigned char* IceAt(uint32_t off) { return g_tw->ice + off; }

static bool BuildTestWorld(ID3D11Device* dev)
{
	g_tw = (TestWorld*)calloc(1, sizeof(TestWorld));
	if (!g_tw) return false;
	TestWorld& w = *g_tw;
	w.ice = (unsigned char*)calloc(1, 0x10100);
	if (!w.ice) return false;
	for (uint32_t i = 0; i < VPSHELL_ICE_FIELDS_BYTES; ++i) w.ice[VPSHELL_ICE_FIELDS_BASE + i] = (unsigned char)((7 * i + 3) & 0xFF);
	for (uint32_t off = VPSHELL_ICE_FIELDS_BASE; off < VPSHELL_ICE_FIELDS_BASE + VPSHELL_ICE_FIELDS_BYTES; off += 4)
	{
		bool is; float v = TestFloat(off, &is);
		if (is) PutF(w.ice, off, v);
	}
	w.stride[IT_VFMT] = 8;
	for (int k = 0; k < 2; ++k)
	{
		w.bufs[k] = TestBuffer(dev, false);       // VB 7, 8
		w.bufs[2 + k] = TestBuffer(dev, true);    // IB 9, 10
		if (!w.bufs[k] || !w.bufs[2 + k]) return false;
		w.vbTab[7 + k] = (uintptr_t)w.bufs[k]; w.ibTab[9 + k] = (uintptr_t)w.bufs[2 + k];
		TestDesc(w.vbDesc + (7 + k) * IT_DESC_BYTES, 1280, 1, 0x24, IT_VFMT);
		TestDesc(w.ibDesc + (9 + k) * IT_DESC_BYTES, 60, 1, 0x2C, DXGI_FORMAT_R16_UINT);
	}
	TestRecord(w.recTab + 5 * IT_REC_BYTES, IT_MATERIAL, 9, 7, 6, 0, 0);
	TestRecord(w.recTab + 6 * IT_REC_BYTES, IT_MATERIAL, 9, 7, 4, 18, 20);
	const float box0[4] = { -1793.0f, 600.0f, 1823.9f, 1043.1f }, box1[4] = { -1793.0f, -1098.5f, 1823.9f, -700.0f };
	memcpy(w.chunks, box0, 16); Put32(w.chunks, 0x10, 5);
	memcpy(w.chunks + IT_CHUNK_BYTES, box1, 16); Put32(w.chunks + IT_CHUNK_BYTES, 0x10, 6);
	IceAt(IT_OFF_BUILT)[0] = 1;
	Put32(w.ice, IT_OFF_VB, 7); Put32(w.ice, IT_OFF_IB, 9);
	Put32(w.ice, IT_OFF_CHUNKS, (uint32_t)(uintptr_t)w.chunks); Put32(w.ice, IT_OFF_COUNT, 2);
	switch (g_test)   // hostile variants
	{
	case T_NOBUILT: IceAt(IT_OFF_BUILT)[0] = 0; break;
	case T_NULLVB: Put32(w.ice, IT_OFF_VB, 0); Put32(w.recTab + 5 * IT_REC_BYTES, 8, 0); Put32(w.recTab + 6 * IT_REC_BYTES, 8, 0); break;
	case T_BADVFMT: Put32(w.vbDesc + 7 * IT_DESC_BYTES, 0x24, 0x17); w.stride[0x17] = 12; break;
	case T_BADSIZE: Put32(w.vbDesc + 7 * IT_DESC_BYTES, 0, 1288); break;                      // the EXE's size disagrees with D3D's
	case T_IBFORMAT: Put32(w.ibDesc + 9 * IT_DESC_BYTES, 0x2C, DXGI_FORMAT_R32_UINT); break;
	case T_CHUNKPAST: Put32(w.recTab + 6 * IT_REC_BYTES, 0x10, 22); break;                   // 22 + 12 > 30 indices
	case T_BASEPAST: Put32(w.recTab + 6 * IT_REC_BYTES, 0x14, 160); break;                   // base vertex = the VB's count
	case T_BADRECORD: { uint16_t m = 0x3A; memcpy(w.recTab + 6 * IT_REC_BYTES, &m, 2); } break;
	case T_HUGECOUNT: Put32(w.ice, IT_OFF_COUNT, 100000); break;
	case T_GARBAGE:                                                                          // the chunk array in uncommitted memory
		w.reserved = VirtualAlloc(nullptr, 1 << 16, MEM_RESERVE, PAGE_NOACCESS);
		Put32(w.ice, IT_OFF_CHUNKS, (uint32_t)(uintptr_t)w.reserved);
		break;
	default: break;
	}
	return true;
}

static void FreeTestWorld()
{
	if (!g_tw) return;
	for (int i = 0; i < 4; ++i) if (g_tw->bufs[i]) g_tw->bufs[i]->Release();
	if (g_tw->reserved) VirtualFree(g_tw->reserved, 0, MEM_RELEASE);
	if (g_tw->image) VirtualFree(g_tw->image, 0, MEM_RELEASE);
	free(g_tw->ice);
	free(g_tw); g_tw = nullptr;
}

// ---- set-up --------------------------------------------------------------------------------------------------------
// The pattern checker's reader: the tap's own guarded read (ctx: a bool set on a fault).
static int ItRead(void* ctx, uintptr_t a, void* out, uint32_t n)
{
	bool fault = false;
	const bool ok = SafeRead(a, out, n, &fault);
	if (fault && ctx) *(bool*)ctx = true;
	return ok ? 1 : 0;
}

static bool CheckPatterns(int build, uint32_t base)
{
	bool fault = false;
	char why[200] = "";
	const int k = VPExeCheckSigs(build, base, kItSigs, IT_NSIGS, ItRead, &fault, why, sizeof(why));
	if (k < 0) return true;
	// the block's failText (160 bytes) gets the symbol and the reason; the log line also the description
	ILOG("ice_tap: pattern %d: %s%s", k, why, fault ? " (fault)" : "");
	const char* at = strstr(why, "' at ");
	_snprintf_s(g_why, _TRUNCATE, "pattern %d%s%s", k, at ? at + 1 : why, fault ? " (fault)" : "");
	return false;
}

// ice_test=patterns: an image of zeros with every pattern's bytes at its place (pattern k broken), checked as the EXE;
// laid out for the build the composite validated (the test host's fake EXE; build 4390913 when none was validated).
static bool TestPatterns()
{
	const int build = VPCamBuild() >= 0 ? VPCamBuild() : kVpExeBuild4390913;
	const uint32_t end = VpExeSigsEnd(build, kItSigs, IT_NSIGS);
	if (!end) { _snprintf_s(g_why, _TRUNCATE, "test image: a pattern is not carried to %s", VpExeBuildLabel(build)); return false; }
	const uint32_t size = (end - VPEXE_IMAGE_BASE + 0xFFFFu) & ~0xFFFFu;
	unsigned char* img = (unsigned char*)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!img) { _snprintf_s(g_why, _TRUNCATE, "test image could not be made"); return false; }
	g_tw->image = img;
	const uint32_t base = (uint32_t)(uintptr_t)img;
	if (!VpExeLaySigs(build, base, img, size, kItSigs, IT_NSIGS, g_testPattern))
	{
		_snprintf_s(g_why, _TRUNCATE, "test image: a pattern does not expand for %s", VpExeBuildLabel(build));
		return false;
	}
	return CheckPatterns(build, base);
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
	get(L"ice_settle_frames", L"VPSHELL_ICE_SETTLE_FRAMES", v, 96); if (v[0]) g_settleFrames = (uint32_t)clamp(_wtoi(v), 1, 100000);
	get(L"ice_max_kb", L"VPSHELL_ICE_MAX_KB", v, 96);
	if (v[0]) g_maxBytes = (uint64_t)clamp(_wtoi(v), 1, (int)(VPSHELL_ICE_MAX_BYTES / 1024)) * 1024;
	get(L"ice_unread_ms", L"VPSHELL_ICE_UNREAD_MS", v, 96); g_unreadMs = v[0] ? (uint32_t)clamp(_wtoi(v), 0, 3600000) : 10000;
	get(L"ice_worker", L"VPSHELL_ICE_WORKER", v, 96); g_useWorker = v[0] ? _wtoi(v) != 0 : 1;
}

static void ReadTestKeys()
{
	ReadKeys();
	wchar_t ini[MAX_PATH]; _snwprintf_s(ini, _TRUNCATE, L"%sshell_composite.ini", g_dir);
	wchar_t v[96] = L"";
	if (!GetEnvironmentVariableW(L"VPSHELL_ICE_TEST", v, 96)) GetPrivateProfileStringW(L"composite", L"ice_test", L"", v, 96, ini);
	char a[96]; WideCharToMultiByte(CP_ACP, 0, v, -1, a, 96, nullptr, nullptr);
	g_test = T_NONE;
	for (int i = 1; i < T_COUNT; ++i) if (!_stricmp(a, kTestNames[i])) g_test = i;
	if (a[0] && !g_test) ILOG("ice_tap: ice_test=%s is not a known mode - ignored", a);
	v[0] = 0;
	if (!GetEnvironmentVariableW(L"VPSHELL_ICE_TEST_PATTERN", v, 96)) GetPrivateProfileStringW(L"composite", L"ice_test_pattern", L"-1", v, 96, ini);
	g_testPattern = _wtoi(v);
}

// ---- the control block -----------------------------------------------------------------------------------------------
static bool OpenControl()
{
	if (g_blk) return true;
	wchar_t name[64]; _snwprintf_s(name, _TRUNCATE, VPSHELL_ICETAP_CONTROL_FMT, GetCurrentProcessId());
	g_ctl = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, 0, VPSHELL_ICETAP_CONTROL_BYTES, name);
	if (!g_ctl) { ILOG("ice_tap: CreateFileMapping('%ls') failed, error %lu - the tap cannot publish", name, GetLastError()); return false; }
	g_blk = (VPShellIceTapControl*)MapViewOfFile(g_ctl, FILE_MAP_WRITE, 0, 0, VPSHELL_ICETAP_CONTROL_BYTES);
	if (!g_blk) { ILOG("ice_tap: MapViewOfFile('%ls') failed, error %lu", name, GetLastError()); CloseHandle(g_ctl); g_ctl = nullptr; return false; }
	return true;
}

static void Publish()
{
	if (!g_blk) return;
	VPShellIceTapControl* t = g_blk;
	InterlockedIncrement((volatile LONG*)&t->seq);
	MemoryBarrier();
	t->magic = VPSHELL_ICE_MAGIC; t->version = VPSHELL_ICE_VERSION; t->headerBytes = sizeof(VPShellIceTapControl);
	t->state = g_state; t->failReason = g_fail; t->exePid = GetCurrentProcessId();
	t->generation = g_img ? g_gen : 0; t->loadSeq = g_img ? g_imgLoad : g_runKey.load; t->chunkCount = g_img ? g_imgChunks : 0;
	t->imageBytes = g_img ? g_imgBytes : 0; t->fnv1a64 = g_img ? g_imgFnv : 0; t->readyQpc = g_readyQpc;
	t->copyMs = g_copyMs; t->copyCpuMs = g_lastCpuMs; t->copies = g_copies;
	t->vbHandle = g_img ? g_imgVb : 0; t->ibHandle = g_img ? g_imgIb : 0;
	wcsncpy_s(t->imageName, g_img ? g_imgName : L"", _TRUNCATE);
	if (g_state == VPICE_FAILED) strncpy_s(t->failText, g_why, _TRUNCATE); else t->failText[0] = 0;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&t->seq);
	g_dirty = false;
}

static void SetState(uint32_t st) { g_state = st; g_dirty = true; }

// Ticket 0385: a worker still running is waited for (it reads the mapped staging memory; at most the image's build, hash and
// section write, 7-21 ms), then the maps are undone on the render thread. A section the worker made but nobody published
// is closed.
static void JoinWorker()
{
	if (g_worker)
	{
		WaitForSingleObject(g_worker, INFINITE);
		CloseHandle(g_worker); g_worker = nullptr;
		if (g_job.sec) { CloseHandle(g_job.sec); g_job.sec = nullptr; }
	}
	if (g_mapCtx)
	{
		if (g_vb.staging) g_mapCtx->Unmap(g_vb.staging, 0);
		if (g_ib.staging) g_mapCtx->Unmap(g_ib.staging, 0);
		g_mapCtx = nullptr;
	}
}

static void DropCopy()
{
	JoinWorker();
	if (g_vb.staging) { g_vb.staging->Release(); g_vb.staging = nullptr; }
	if (g_ib.staging) { g_ib.staging->Release(); g_ib.staging = nullptr; }
	free(g_chunkSnap); g_chunkSnap = nullptr; g_chunkSnapBytes = 0;
	free(g_recSnap); g_recSnap = nullptr;
	free(g_chunks); g_chunks = nullptr; g_nChunks = 0;
	g_phase = PH_IDLE;
}

static void CloseImage()
{
	if (g_img) { CloseHandle(g_img); g_img = nullptr; }
	g_imgName[0] = 0; g_imgBytes = 0; g_imgFnv = 0; g_imgChunks = 0;
	g_offered = false;   // ticket 0385: no image left to be acked (Collect sets the offer of the next one)
}

// The copy ends without READY. forGood: the tap is off for the process. keepTried: this object state is not copied again.
static void Fail(uint32_t fail, const char* why, bool forGood, bool keepTried)
{
	DropCopy();
	CloseImage();   // the previous image belongs to an older mesh than the EXE's now
	g_fail = fail;
	if (why != g_why) _snprintf_s(g_why, _TRUNCATE, "%s", why);
	SetState(VPICE_FAILED);
	if (keepTried) { g_doneKey = g_copyKey; g_haveDone = true; }
	ILOG("ice_tap: FAILED %s (load %u, copy %u): %s%s", fail < 10 ? kFailNames[fail] : "?", g_copyKey.load, g_copies, g_why,
		forGood ? " - DISABLED for the life of the process" : "");
	if (forGood) g_dead = true;
}

// ---- gathering ---------------------------------------------------------------------------------------------------------
static bool ReadKey(uint32_t load, Key* k, bool* fault)
{
	unsigned char h[8];
	k->load = load;
	if (!SafeRead(A.ice + IT_OFF_BUILT, h, 1, fault)) return false;
	k->built = h[0];
	if (!SafeRead(A.ice + IT_OFF_VB, h, 8, fault)) return false;
	k->vb = U32(h, 0); k->ib = U32(h, 4);
	if (!SafeRead(A.ice + IT_OFF_CHUNKS, h, 8, fault)) return false;
	k->chunks = U32(h, 0); k->count = U32(h, 4);
	return true;
}

static bool SameKey(const Key& a, const Key& b) { return !memcmp(&a, &b, sizeof(Key)); }

// A buffer handle -> staging copy issued. 0 ok; else a VPShellIceTapFail with g_why (and *fault).
static uint32_t TakeBuffer(const IceTapEnv& env, uint32_t h, bool ib, Buf* out, bool* fault)
{
	const char* what = ib ? "IB" : "VB";
	if (!h || h >= IT_BUF_MAX) { _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X out of range (1..0x%X)", what, h, IT_BUF_MAX - 1); return VPICEF_LAYOUT; }
	uint32_t ptr = 0;
	if (!SafeRead((ib ? A.ibTab : A.vbTab) + 4u * h, &ptr, 4, fault) || !ptr)
	{
		_snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: %s", what, h, *fault ? "fault reading the buffer table" : "no buffer");
		return *fault ? VPICEF_EXCEPTION : VPICEF_LAYOUT;
	}
	uint32_t vt = 0;
	if (!SafeRead(ptr, &vt, 4, fault) || (vt != g_refVt[0] && vt != g_refVt[1] && vt != g_refVt[2]))
	{
		_snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: object 0x%08X (vtable 0x%08X) is not a D3D11 buffer%s", what, h, ptr, vt, *fault ? " (fault)" : "");
		*fault = false;   // an unreadable object pointer is a layout failure, not an EXE fault
		return VPICEF_LAYOUT;
	}
	unsigned char desc[IT_DESC_BYTES];
	if (!SafeRead((ib ? A.ibDesc : A.vbDesc) + IT_DESC_BYTES * h, desc, sizeof(desc), fault))
	{ _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: description unreadable", what, h); return *fault ? VPICEF_EXCEPTION : VPICEF_LAYOUT; }
	D3D11_BUFFER_DESC bd = {};
	ID3D11Buffer* buf = BufferOf(ptr, &bd, fault);
	if (!buf) { _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: QueryInterface(ID3D11Buffer) failed%s", what, h, *fault ? " (fault)" : ""); return *fault ? VPICEF_EXCEPTION : VPICEF_LAYOUT; }
	uint32_t stride = 0, fmtOrId = ib ? U32(desc, 0x2C) : U32(desc, 0x24);
	const char* bad = nullptr;
	if (U32(desc, 0) != bd.ByteWidth) bad = "the EXE's size differs from the buffer's";
	else if (!(bd.BindFlags & (ib ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER))) bad = "wrong bind flags";
	else if (bd.Usage != D3D11_USAGE_IMMUTABLE) bad = "not IMMUTABLE";
	else if (bd.ByteWidth == 0) bad = "size 0";
	else if (ib)
	{
		if (fmtOrId != DXGI_FORMAT_R16_UINT || bd.ByteWidth % 2) bad = "index format not R16 or size not whole indices";
		else stride = 2;
	}
	else
	{
		if (fmtOrId != IT_VFMT) bad = "vertex format not 0x16";
		else if (!SafeRead(A.stride + 4u * IT_VFMT, &stride, 4, fault)) bad = "stride table unreadable";
		else if (stride != VPSHELL_ICE_STRIDE || bd.ByteWidth % stride) bad = "stride not 8 or size not whole vertices";
	}
	if (bad)
	{
		_snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: %s (EXE size %u, D3D size %u, bind 0x%X, usage %u, %s %u, stride %u)", what, h, bad,
			U32(desc, 0), bd.ByteWidth, bd.BindFlags, (unsigned)bd.Usage, ib ? "format" : "vertex format", fmtOrId, stride);
		buf->Release();
		return *fault ? VPICEF_EXCEPTION : VPICEF_DESC;
	}
	D3D11_BUFFER_DESC sd = {};
	sd.ByteWidth = bd.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Buffer* st = nullptr;
	HRESULT hr = env.dev->CreateBuffer(&sd, nullptr, &st);
	if (FAILED(hr)) { _snprintf_s(g_why, _TRUNCATE, "%s handle 0x%X: staging buffer of %u bytes failed 0x%08X", what, h, bd.ByteWidth, hr); buf->Release(); return VPICEF_D3D; }
	env.ctx->CopyResource(st, buf);
	buf->Release();
	out->staging = st; out->bytes = bd.ByteWidth; out->count = bd.ByteWidth / stride;
	return VPICEF_NONE;
}

// The object's fields, the chunk array and records, both buffers. 0 ok; else a VPShellIceTapFail with g_why.
static uint32_t Gather(const IceTapEnv& env, const Key& k)
{
	bool fault = false;
#define IT_FAULT(what) do { _snprintf_s(g_why, _TRUNCATE, "%s%s", what, fault ? " (fault)" : " (address out of range)"); \
		return fault ? VPICEF_EXCEPTION : VPICEF_LAYOUT; } while (0)
	if (!SafeRead(A.ice + VPSHELL_ICE_FIELDS_BASE, g_fields, VPSHELL_ICE_FIELDS_BYTES, &fault)) IT_FAULT("reading the Ice object's fields");
	if (k.count > VPSHELL_ICE_MAX_CHUNKS || !k.chunks)
	{ _snprintf_s(g_why, _TRUNCATE, "chunk array out of range: %u chunks at 0x%08X (at most %u)", k.count, k.chunks, VPSHELL_ICE_MAX_CHUNKS); return VPICEF_LAYOUT; }
	g_chunkSnapBytes = k.count * IT_CHUNK_BYTES;
	g_chunkSnap = (unsigned char*)malloc(g_chunkSnapBytes);
	g_recSnap = (unsigned char*)malloc((size_t)k.count * IT_REC_BYTES);
	g_chunks = (VPShellIceChunk*)calloc(k.count, sizeof(VPShellIceChunk));
	if (!g_chunkSnap || !g_recSnap || !g_chunks) { _snprintf_s(g_why, _TRUNCATE, "out of memory"); return VPICEF_LAYOUT; }
	g_nChunks = k.count;
	if (!SafeRead(k.chunks, g_chunkSnap, g_chunkSnapBytes, &fault))
	{
		_snprintf_s(g_why, _TRUNCATE, "%s reading %u chunk bytes at 0x%08X", fault ? "fault" : "address out of range", g_chunkSnapBytes, k.chunks);
		return fault ? VPICEF_EXCEPTION : VPICEF_LAYOUT;
	}
	for (uint32_t c = 0; c < k.count; ++c)
	{
		const unsigned char* e = g_chunkSnap + c * IT_CHUNK_BYTES;
		const uint32_t h = U32(e, 0x10);
		unsigned char* rec = g_recSnap + (size_t)c * IT_REC_BYTES;
		if (!h || h >= IT_REC_MAX) { _snprintf_s(g_why, _TRUNCATE, "chunk %u: draw record handle 0x%X out of range", c, h); return VPICEF_LAYOUT; }
		if (!SafeRead(A.recTab + IT_REC_BYTES * h, rec, IT_REC_BYTES, &fault)) IT_FAULT("reading a draw record");
		if (U16(rec, 0) != IT_MATERIAL || U32(rec, 4) != k.ib || U32(rec, 8) != k.vb)
		{
			_snprintf_s(g_why, _TRUNCATE, "chunk %u: draw record 0x%X: material %u IB 0x%X VB 0x%X, expected %u / 0x%X / 0x%X", c, h,
				U16(rec, 0), U32(rec, 4), U32(rec, 8), IT_MATERIAL, k.ib, k.vb);
			return VPICEF_LAYOUT;
		}
		VPShellIceChunk& o = g_chunks[c];
		memcpy(o.box, e, 16);
		o.triangles = U32(rec, 0xC); o.startIndex = U32(rec, 0x10); o.baseVertex = (int32_t)U32(rec, 0x14); o.recordHandle = h;
	}
	uint32_t f = TakeBuffer(env, k.vb, false, &g_vb, &fault);
	if (f) return f;
	f = TakeBuffer(env, k.ib, true, &g_ib, &fault);
	if (f) return f;
	for (uint32_t c = 0; c < k.count; ++c)
	{
		const VPShellIceChunk& o = g_chunks[c];
		if (!o.triangles || (uint64_t)o.startIndex + 3ull * o.triangles > g_ib.count)
		{
			_snprintf_s(g_why, _TRUNCATE, "chunk %u: %u triangles from index %u, the index buffer holds %u indices", c, o.triangles, o.startIndex, g_ib.count);
			return VPICEF_LAYOUT;
		}
		if (o.baseVertex < 0 || (uint32_t)o.baseVertex >= g_vb.count)
		{
			_snprintf_s(g_why, _TRUNCATE, "chunk %u: base vertex %d, the vertex buffer holds %u vertices", c, o.baseVertex, g_vb.count);
			return VPICEF_LAYOUT;
		}
	}
#undef IT_FAULT
	return VPICEF_NONE;
}

static void StartCopy(const IceTapEnv& env, const Key& k)
{
	g_copies++;
	g_copyKey = k;
	g_copyQpc = Qpc(); g_copyCpuMs = 0; g_copyPresents = 0; g_mapTries = 0; g_maxPresentMs = 0; g_copyActive = true;
	ReadKeys();
	SetState(VPICE_WRITING);
	uint32_t f = Gather(env, k);
	if (f) { Fail(f, g_why, f == VPICEF_EXCEPTION, true); return; }
	if (g_test == T_CHANGE && g_copies == 1)   // an Ice_Reload while the copy is in flight: the twin handles
	{
		Put32(g_tw->ice, IT_OFF_VB, 8); Put32(g_tw->ice, IT_OFF_IB, 10);
		for (int r = 5; r <= 6; ++r) { Put32(g_tw->recTab + r * IT_REC_BYTES, 4, 10); Put32(g_tw->recTab + r * IT_REC_BYTES, 8, 8); }
	}
	g_phase = PH_MAP;
	ILOG("ice_tap: copy %u of load %u started: VB 0x%X (%u bytes), IB 0x%X (%u bytes), %u chunks", g_copies, k.load, k.vb, g_vb.bytes,
		k.ib, g_ib.bytes, k.count);
}

// ---- the reader gate (ticket 0385) ------------------------------------------------------------------------------------
// No server at all needs no rule here: without the main section the composite publishes no camera (CamOnPresent returns
// before the read), so camOk stays 0 and the watch never asks for a copy. A server without the ice reader does: the
// image offered to it is not acked. The section's magic and serverPid are no sign of a server: with the ring hand-over
// (the product path, handover=ring) server.exe never writes the texture block, so both stay 0 (seen live on oldpc
// 2026-11-16, job 20261116-050355-c4ab, with a first form of this gate that waited for the magic).
// Why the copy of a settled object state waits now (the reason in g_deferWhy), or null: copy.
static const char* GateWhy()
{
	// the same server: the section names the same pid (0 = not named: the ring hand-over writes no serverPid)
	if (g_unreadMs && g_offered && g_blk && (g_hdrNow ? g_hdrNow->serverPid : 0) == g_offerPid)
	{
		const uint32_t ack = g_blk->ackGeneration;
		const double age = Ms(Qpc() - g_offerQpc);
		if (ack < g_offerGen && age >= g_unreadMs)
		{
			char who[48];
			if (g_offerPid) _snprintf_s(who, _TRUNCATE, "the server (pid %u)", g_offerPid); else strcpy_s(who, "the server");
			_snprintf_s(g_deferWhy, _TRUNCATE, "%s has not acked generation %u in %.0f ms (ack %u; it reads no ice, "
				"render_ice=0?); copied when it acks, or for another server process", who, g_offerGen, age, ack);
			return g_deferWhy;
		}
	}
	return nullptr;
}

// After the maps (render thread): the object must not have changed while the copies were in flight. false = it changed or
// faulted (failure published).
static bool ObjectUnchanged()
{
	bool fault = false;
	Key now = {};
	bool same = ReadKey(g_copyKey.load, &now, &fault) && SameKey(now, g_copyKey);
	if (same)
	{
		unsigned char* tmp = (unsigned char*)malloc(g_chunkSnapBytes > IT_REC_BYTES ? g_chunkSnapBytes : IT_REC_BYTES);
		same = tmp && SafeRead(g_copyKey.chunks, tmp, g_chunkSnapBytes, &fault) && !memcmp(tmp, g_chunkSnap, g_chunkSnapBytes);
		for (uint32_t c = 0; same && c < g_nChunks; ++c)
			same = SafeRead(A.recTab + IT_REC_BYTES * g_chunks[c].recordHandle, tmp, IT_REC_BYTES, &fault) &&
				!memcmp(tmp, g_recSnap + (size_t)c * IT_REC_BYTES, 0x18);
		free(tmp);
	}
	if (!same && fault) { Fail(VPICEF_EXCEPTION, "fault re-reading the Ice object after the copy", true, true); return false; }
	if (!same) { Fail(VPICEF_CHANGED, "the Ice object changed during the copy (a load or Ice_Reload): copied again when it settles", false, false); return false; }
	return true;
}

// The worker (ticket 0385 / 0384): every index, the image, its hash, the section. Reads the mapped staging memory and the
// copy's snapshots (g_chunks, g_fields), writes only g_job's result; touches no D3D object and no EXE memory.
static DWORD WINAPI BuildImage(LPVOID)
{
	WorkJob& j = g_job;
	const int64_t t0 = Qpc();
	j.fail = VPICEF_NONE; j.why[0] = 0; j.sec = nullptr; j.name[0] = 0; j.imageBytes = 0; j.fnv = 0; j.views = 0;
	for (uint32_t c = 0; c < g_nChunks && !j.fail; ++c)
	{
		const VPShellIceChunk& o = g_chunks[c];
		for (uint32_t i = 0; i < 3 * o.triangles; ++i)
		{
			const uint32_t v = U16(j.ib, 2 * (o.startIndex + i)) + (uint32_t)o.baseVertex;
			if (v >= g_vb.count)
			{
				_snprintf_s(j.why, _TRUNCATE, "chunk %u: index %u = %u + base %d, the vertex buffer holds %u vertices", c, o.startIndex + i,
					v - (uint32_t)o.baseVertex, o.baseVertex, g_vb.count);
				j.fail = VPICEF_LAYOUT;
				break;
			}
		}
	}
	unsigned char* img = nullptr;
	VPShellIceImage h = {};
	if (!j.fail)
	{
		h.magic = VPSHELL_ICE_MAGIC; h.version = VPSHELL_ICE_VERSION; h.headerBytes = sizeof(h); h.chunkBytes = sizeof(VPShellIceChunk);
		h.generation = j.generation; h.loadSeq = j.load; h.exePid = GetCurrentProcessId(); h.chunkCount = g_nChunks;
		h.fieldsBase = VPSHELL_ICE_FIELDS_BASE; h.fieldsBytes = VPSHELL_ICE_FIELDS_BYTES; h.vbStride = VPSHELL_ICE_STRIDE;
		h.ibFormat = VPSHELL_ICE_INDEX_FORMAT; h.vbBytes = g_vb.bytes; h.ibBytes = g_ib.bytes;
		h.fieldsOffset = sizeof(h);
		h.chunkOffset = (h.fieldsOffset + VPSHELL_ICE_FIELDS_BYTES + 15) & ~15u;
		h.vbOffset = (uint32_t)((h.chunkOffset + (uint64_t)g_nChunks * sizeof(VPShellIceChunk) + 15) & ~15ull);
		h.ibOffset = (h.vbOffset + g_vb.bytes + 15) & ~15u;
		h.imageBytes = (uint64_t)h.ibOffset + g_ib.bytes;
		h.vbHandle = j.vbHandle; h.ibHandle = j.ibHandle;
		if (h.imageBytes > j.maxBytes)
		{
			_snprintf_s(j.why, _TRUNCATE, "image of %llu bytes, the limit is %llu", (unsigned long long)h.imageBytes, (unsigned long long)j.maxBytes);
			j.fail = VPICEF_TOO_BIG;
		}
		else if (!(img = (unsigned char*)calloc(1, (size_t)h.imageBytes)))
		{
			_snprintf_s(j.why, _TRUNCATE, "out of memory for the image");
			j.fail = VPICEF_TOO_BIG;
		}
	}
	if (!j.fail)
	{
		memcpy(img, &h, sizeof(h));
		memcpy(img + h.fieldsOffset, g_fields, VPSHELL_ICE_FIELDS_BYTES);
		memcpy(img + h.chunkOffset, g_chunks, (size_t)g_nChunks * sizeof(VPShellIceChunk));
		memcpy(img + h.vbOffset, j.vb, g_vb.bytes);
		memcpy(img + h.ibOffset, j.ib, g_ib.bytes);
		j.fnv = VPIceFnv(VPICE_FNV_INIT, img, (size_t)h.imageBytes);
		j.imageBytes = h.imageBytes;
		// the section, written through short views
		if (j.badSection) _snwprintf_s(j.name, _TRUNCATE, L"Local\\VPShellIce\\no\\such\\%u", h.generation);
		else _snwprintf_s(j.name, _TRUNCATE, VPSHELL_ICE_IMAGE_FMT, GetCurrentProcessId(), h.generation);
		const uint64_t secBytes = (h.imageBytes + 0xFFFF) & ~0xFFFFull;
		HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, (DWORD)(secBytes >> 32), (DWORD)secBytes, j.name);
		DWORD err = GetLastError();
		if (!sec || err == ERROR_ALREADY_EXISTS)
		{
			_snprintf_s(j.why, _TRUNCATE, sec ? "section '%ls' already exists (another holder)" : "CreateFileMapping('%ls', %llu bytes) failed, error %lu",
				j.name, (unsigned long long)secBytes, err);
			if (sec) CloseHandle(sec);
			j.fail = VPICEF_SECTION;
		}
		else
		{
			for (uint64_t off = 0; off < h.imageBytes; off += IT_VIEW_BYTES)
			{
				const uint64_t n = h.imageBytes - off < IT_VIEW_BYTES ? h.imageBytes - off : IT_VIEW_BYTES;
				void* v = MapViewOfFile(sec, FILE_MAP_WRITE, (DWORD)(off >> 32), (DWORD)off, (SIZE_T)n);
				if (!v)
				{
					_snprintf_s(j.why, _TRUNCATE, "MapViewOfFile at %llu failed, error %lu", (unsigned long long)off, GetLastError());
					j.fail = VPICEF_SECTION;
					break;
				}
				memcpy(v, img + off, (size_t)n);
				UnmapViewOfFile(v);
				j.views++;
			}
			if (j.fail) CloseHandle(sec);
			else j.sec = sec;
		}
	}
	free(img);
	j.ms = Ms(Qpc() - t0);
	return 0;
}

// The worker ended: unmap, then publish READY or its failure. Render thread.
static void Collect()
{
	const bool onWorker = g_worker != nullptr;
	if (g_worker) { CloseHandle(g_worker); g_worker = nullptr; }
	g_mapCtx->Unmap(g_vb.staging, 0); g_mapCtx->Unmap(g_ib.staging, 0);
	g_mapCtx = nullptr;
	WorkJob& j = g_job;
	if (j.fail)
	{
		_snprintf_s(g_why, _TRUNCATE, "%s", j.why);
		Fail(j.fail, g_why, false, true);
		return;
	}
	CloseImage();   // the previous generation
	g_img = j.sec; j.sec = nullptr;
	g_gen = j.generation; g_imgLoad = j.load; g_imgChunks = g_nChunks; g_imgBytes = j.imageBytes; g_imgFnv = j.fnv;
	g_imgVb = j.vbHandle; g_imgIb = j.ibHandle;
	wcsncpy_s(g_imgName, j.name, _TRUNCATE);
	g_doneKey = g_copyKey; g_haveDone = true;
	g_readyQpc = Qpc();
	g_copyMs = (float)Ms(g_readyQpc - g_copyQpc);
	g_offerPid = g_hdrNow ? g_hdrNow->serverPid : 0; g_offerGen = g_gen; g_offerQpc = g_readyQpc; g_offered = true;   // the gate
	{   // this Present's share (the end of IceTap_OnPresent does not count it again)
		const double dt = Ms(g_readyQpc - g_t0);
		g_copyCpuMs += dt; if (dt > g_maxPresentMs) g_maxPresentMs = dt;
		g_copyActive = false;
		g_lastCpuMs = (float)g_copyCpuMs;
	}
	g_fail = VPICEF_NONE; g_why[0] = 0;
	const uint32_t vbBytes = g_vb.bytes, ibBytes = g_ib.bytes, chunks = g_nChunks;
	uint64_t tris = 0; for (uint32_t c = 0; c < g_nChunks; ++c) tris += g_chunks[c].triangles;
	DropCopy();
	SetState(VPICE_READY);
	ILOG("ice_tap: READY generation %u (load %u, copy %u): VB %u bytes, IB %u bytes, %u chunks, %llu triangles; image %llu bytes in '%ls' "
		"(%u views); fnv1a64 %016llX; wall %.1f ms over %u Presents, composite CPU %.3f ms (most in one Present %.3f ms), image built in "
		"%.3f ms %s",
		g_gen, g_imgLoad, g_copies, vbBytes, ibBytes, chunks, (unsigned long long)tris, (unsigned long long)g_imgBytes, g_imgName, j.views,
		(unsigned long long)g_imgFnv, g_copyMs, g_copyPresents + 1, g_copyCpuMs, g_maxPresentMs, j.ms,
		onWorker ? "by the worker" : "on the render thread");
}

static void MapStep(const IceTapEnv& env)
{
	g_mapTries++;
	D3D11_MAPPED_SUBRESOURCE mv = {}, mi = {};
	const UINT flags = g_mapTries > IT_MAP_TRIES ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT;
	HRESULT hv = env.ctx->Map(g_vb.staging, 0, D3D11_MAP_READ, flags, &mv);
	if (hv == DXGI_ERROR_WAS_STILL_DRAWING) return;
	if (FAILED(hv) || !mv.pData) { _snprintf_s(g_why, _TRUNCATE, "Map of the VB's staging copy failed 0x%08X", hv); Fail(VPICEF_D3D, g_why, false, true); return; }
	HRESULT hi = env.ctx->Map(g_ib.staging, 0, D3D11_MAP_READ, flags, &mi);
	if (hi == DXGI_ERROR_WAS_STILL_DRAWING) { env.ctx->Unmap(g_vb.staging, 0); return; }
	if (FAILED(hi) || !mi.pData)
	{
		env.ctx->Unmap(g_vb.staging, 0);
		_snprintf_s(g_why, _TRUNCATE, "Map of the IB's staging copy failed 0x%08X", hi);
		Fail(VPICEF_D3D, g_why, false, true);
		return;
	}
	g_mapCtx = env.ctx;   // from here DropCopy unmaps both
	if (!ObjectUnchanged()) return;   // Fail -> DropCopy unmapped
	WorkJob& j = g_job;
	memset(&j, 0, sizeof(j));
	j.vb = (const unsigned char*)mv.pData; j.ib = (const unsigned char*)mi.pData;
	j.generation = g_gen + 1; j.load = g_copyKey.load; j.vbHandle = g_copyKey.vb; j.ibHandle = g_copyKey.ib;
	j.maxBytes = g_maxBytes; j.badSection = g_test == T_BADSECTION;
	if (g_useWorker)
	{
		g_worker = CreateThread(nullptr, 0, BuildImage, nullptr, 0, nullptr);
		if (g_worker) SetThreadPriority(g_worker, THREAD_PRIORITY_BELOW_NORMAL);
		else ILOG("ice_tap: CreateThread failed, error %lu - the image is built on the render thread", GetLastError());
	}
	g_phase = PH_WORK;
	if (!g_worker)
	{   // ice_worker=0 or no thread: the same work here, in this Present (the cost before 0385; the A/B's base arm)
		BuildImage(nullptr);
		Collect();
	}
}

// PH_WORK: the worker's end is collected at the first Present after it.
static void WorkStep()
{
	if (WaitForSingleObject(g_worker, 0) != WAIT_OBJECT_0) return;
	Collect();
}

// ---- the per-Present entry ----------------------------------------------------------------------------------------------
void IceTap_OnPresent(const IceTapEnv& env)
{
	if (!g_qpcMs) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcMs = 1000.0 / (double)f.QuadPart; }
	g_log = env.log;
	static bool s_noControl;
	if (s_noControl) return;
	if (!OpenControl()) { s_noControl = true; return; }   // logged; without its section the tap cannot publish anything
	if (!env.enabled)
	{
		if (g_phase != PH_IDLE) Fail(VPICEF_KILLED, "ice_tap=0", false, false);
		if (g_state != VPICE_OFF)
		{
			CloseImage();
			g_fail = VPICEF_NONE; g_why[0] = 0; g_haveDone = false; g_deferred = false;
			SetState(VPICE_OFF);
			ILOG("ice_tap: OFF (ice_tap=0)");
		}
		if (g_dirty) Publish();
		return;
	}
	if (g_state == VPICE_OFF) SetState(VPICE_NONE);
	if (g_dead || !env.dev || !env.ctx)
	{
		if (g_dirty) Publish();
		return;
	}
	const int64_t t0 = Qpc();
	g_t0 = t0;
	g_hdrNow = env.hdr;
	if (!g_init)
	{
		g_init = true;
		ReadTestKeys();
		if (!InitDevice(env.dev)) { Fail(VPICEF_D3D, "D3D11 reference buffers could not be created", true, false); Publish(); return; }
		if (g_test)
		{
			ILOG("ice_tap: TEST MODE ice_test=%s: synthetic objects in this DLL, the EXE is not read", kTestNames[g_test]);
			if (!BuildTestWorld(env.dev)) { Fail(VPICEF_D3D, "test world could not be built", true, false); Publish(); return; }
			if (g_test == T_PATTERNS && !TestPatterns())
			{
				ILOG("ice_tap: REFUSED: %s - the tap never reads this EXE", g_why);
				Fail(VPICEF_PATTERN, g_why, true, false); Publish(); return;
			}
			g_patternsOk = true;
			A.ice = (uintptr_t)g_tw->ice; A.vbTab = (uintptr_t)g_tw->vbTab; A.vbDesc = (uintptr_t)g_tw->vbDesc;
			A.ibTab = (uintptr_t)g_tw->ibTab; A.ibDesc = (uintptr_t)g_tw->ibDesc; A.stride = (uintptr_t)g_tw->stride;
			A.recTab = (uintptr_t)g_tw->recTab;
		}
		else
		{
			if (!env.exeValidated) _snprintf_s(g_why, _TRUNCATE, "the composite did not validate the EXE image");
			const int build = VPCamBuild();   // the build the composite validated (ticket 4964)
			g_patternsOk = env.exeValidated && env.exeBase && CheckPatterns(build, env.exeBase);
			if (!g_patternsOk)
			{
				ILOG("ice_tap: REFUSED: %s - the tap never reads this EXE", g_why);
				Fail(VPICEF_PATTERN, g_why, true, false); Publish(); return;
			}
			const uintptr_t e = env.exeBase;
			A.ice = VpExeAddr(build, VPX_ice_ice, e); A.vbTab = VpExeAddr(build, VPX_water_vbtab, e);
			A.vbDesc = VpExeAddr(build, VPX_water_vbdesc, e); A.ibTab = VpExeAddr(build, VPX_water_ibtab, e);
			A.ibDesc = VpExeAddr(build, VPX_water_ibdesc, e); A.stride = VpExeAddr(build, VPX_water_stride, e);
			A.recTab = VpExeAddr(build, VPX_pxw_drawobj_pool, e);
			ILOG("ice_tap: EXE patterns match (%d) - tap enabled (Ice 0x%08X)", IT_NSIGS, (uint32_t)A.ice);
		}
	}
	if (g_test)
	{
		wchar_t trig[MAX_PATH]; _snwprintf_s(trig, _TRUNCATE, L"%sshell_composite.iceload", g_dir);
		if (GetFileAttributesW(trig) != INVALID_FILE_ATTRIBUTES && DeleteFileW(trig))
		{   // an Ice_Reload: the mesh is made again under the twin handles (same bytes)
			g_testReload++;
			const uint32_t vb = (g_testReload & 1) ? 8 : 7, ib = (g_testReload & 1) ? 10 : 9;
			Put32(g_tw->ice, IT_OFF_VB, vb); Put32(g_tw->ice, IT_OFF_IB, ib);
			for (int r = 5; r <= 6; ++r) { Put32(g_tw->recTab + r * IT_REC_BYTES, 4, ib); Put32(g_tw->recTab + r * IT_REC_BYTES, 8, vb); }
			ILOG("ice_tap: test: simulated Ice_Reload (%u): VB 0x%X, IB 0x%X", g_testReload, vb, ib);
		}
	}
	const uint32_t gridSeq = env.cam && env.cam->gridValid ? env.cam->gridSeq : 0;
	const uint32_t load = g_test ? (gridSeq ? gridSeq : 1) : gridSeq;
	if (g_phase == PH_MAP)
	{
		g_copyPresents++;
		MapStep(env);
	}
	else if (g_phase == PH_WORK)
	{
		g_copyPresents++;
		WorkStep();
	}
	else if (load && env.camOk)
	{
		bool fault = false;
		Key k = {};
		if (!ReadKey(load, &k, &fault))
		{
			if (fault) { g_copyKey = k; Fail(VPICEF_EXCEPTION, "fault reading the Ice object's head", true, false); }
		}
		else
		{
			if (!SameKey(k, g_runKey)) { g_runKey = k; g_stable = 0; }
			if (g_stable < 0x7FFFFFFF) g_stable++;
			if (g_stable >= g_settleFrames && !(g_haveDone && SameKey(k, g_doneKey)))
			{
				if (!k.built || !k.count)
				{
					g_copyKey = k; g_doneKey = k; g_haveDone = true;
					CloseImage();
					g_fail = VPICEF_NONE; g_why[0] = 0;
					SetState(VPICE_NO_MESH);
					ILOG("ice_tap: NO_MESH (load %u): built flag %u, %u chunks - this load has no ice mesh", k.load, k.built, k.count);
				}
				else
				{
					const bool newState = !g_deferred || !SameKey(k, g_deferKey);
					if (newState) ReadKeys();   // the gate's keys, once per object state
					const char* why = GateWhy();
					if (why && newState)
					{
						g_deferred = true; g_deferKey = k;   // the unread image stays: if its server acks it after all, the gate opens
						ILOG("ice_tap: copy of load %u deferred (the reader gate): %s", k.load, why);
					}
					else if (!why)
					{
						if (g_deferred) ILOG("ice_tap: deferred copy of load %u starts: %s", k.load,
							SameKey(k, g_deferKey) ? "the reader gate is open" : "a new object state");
						g_deferred = false;
						StartCopy(env, k);
					}
				}
			}
		}
	}
	if (g_blk && g_img && g_blk->ackGeneration == g_gen && g_ackLogged != g_gen)
	{
		MemoryBarrier();
		g_ackLogged = g_gen;
		ILOG("ice_tap: generation %u acked by pid %u, result %d%s", g_gen, g_blk->ackServerPid, g_blk->ackResult,
			g_blk->ackResult == 0 ? "" : " (the consumer refused the image)");
	}
	if (g_copyActive)
	{   // this Present's share of the copy on the render thread (a READY Present counted itself in Collect)
		const double dt = Ms(Qpc() - t0);
		g_copyCpuMs += dt; if (dt > g_maxPresentMs) g_maxPresentMs = dt;
		if (g_phase == PH_IDLE) g_copyActive = false;   // the copy ended without READY (a failure)
	}
	if (g_dirty) Publish();
}

void IceTap_Shutdown(const char* why)
{
	// A copy on staging buffers dies with the device: drop it and copy again on the new device. A READY section stays.
	// PH_WORK (ticket 0385): DropCopy waits for the worker, then unmaps.
	if (g_phase == PH_MAP || g_phase == PH_WORK)
	{
		g_copyActive = false;
		ILOG("ice_tap: copy %u of load %u dropped (%s)", g_copies, g_copyKey.load, why ? why : "");
		DropCopy();
		SetState(g_img ? VPICE_READY : VPICE_NONE);
		g_haveDone = false;   // the dropped copy's object state is copied again on the new device
		if (g_dirty) Publish();
	}
	FreeTestWorld();
	g_refVt[0] = g_refVt[1] = g_refVt[2] = 0;
	if (!g_dead) g_init = false;
	(void)kStateNames;
}
