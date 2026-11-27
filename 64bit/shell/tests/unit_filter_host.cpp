// unit_filter_host.cpp - the stand-in host of the unit-draw filter (epic 0040, issue 4752; shell/tests/test_unit_filter.py).
// Written 2026-11-17 for this repository (GPL-3.0-or-later). Test program only, never shipped.
//
// A D3D11 program like the EXE in the respects the filter depends on, and its own "server": it creates the main section
// (VPShellHeader + the mode word, as server.exe does), loads shell_composite.dll, waits until the composite's inline hooks
// are in place, then draws every frame the same twelve cells on the immediate context and twelve on a deferred context
// (recorded, finished, executed), each cell a triangle into its own 8 x 8 part of a render target with one (VS, PS) pair:
//
//   cell entry                      VS (program)            PS (program)            dropped while the latch says so
//   0    DrawIndexedInstanced       0 VS 29a0b061           2 PS 60d270e1 (90)      yes
//   1    DrawIndexed                0                       3 PS 5def4f99 (89)      yes
//   2    Draw                       1 VS 82e59838           4 PS 0e7a3566 (87)      yes
//   3    DrawInstanced              0                       2                       yes
//   4    DrawIndexedInstancedIndirect 0                     2                       yes
//   5    DrawInstancedIndirect      0                       3                       yes
//   6    DrawIndexedInstanced       other                   other                   no (unmarked)
//   7    DrawIndexedInstanced       5 VS 8cb52b96 (mask)    6 PS 72c27aea (mask)    no (the mask pass)
//   8    DrawIndexedInstanced       other                   2                       no (second check: not its pair's VS)
//   9    DrawIndexedInstanced       1                       2                       no (second check)
//   10   DrawIndexedInstanced       10 VS da2382af (sel.)   11 PS 8d853988 (sel.)   no (the selection effect)
//   11   DrawIndexedInstanced       1                       other                   no (material 88's case: VS shared, PS not in the table)
// and one DrawAuto on the immediate context with the pair (0, 2) bound (no pixel: counted only). After Present it reads
// the target back: a cell holding its PS's colour passed ('P'), one still cleared was dropped ('D'), anything else is 'X'.
// One log line per frame: "F <n> imm=<12 chars> def=<12 chars> latched=<w> lpresent=<n> dropped=<n> passed_set=<n>
// fstate=0x<filterState>" (filterState: issue 4761).
//
// The programs' shaders are this host's own (compiled at run time by d3dcompiler_47, distinct per program); their SHA-256
// digests go to the composite in a test table (unit_filter_table, written by the test from --dump's files). Recognition:
//   default     path 1: the shaders are created after the composite's hooks are in place;
//   --fake-exe  path 2: the shaders are created before the composite is loaded, and a fake EXE image (reserved region,
//               VPSHELL_EXE_BASE) holds the camera validation's patterns (exe_camera.h), the filter's create sites, handle
//               globals, shader level, device, renderer counts and per-stage object arrays (unit-draw-filter.md 3.2), the
//               program blobs at their VAs; --fake-bad KIND breaks one piece (the composite must refuse, nothing marked).
// Options: --dll P --log P --section NAME --frames N --hold-ms N --word F:W[:mid] (repeatable: at frame F write word W at
// the frame's start or after the immediate cells) --server ok|pid0|pidother|stale|nomagic|none|zeroblock --stale-until F
// --dump DIR (write the 24 shaders (14 units + other, 10 model)' bytecode files and exit) --wait-hooks all|present (present: only the Present hook is
// awaited, for runs with hook_units=0) --ring ok|stale|pidother|failed|stopped|nomagic|rebuilding (issue 4761: the
// liveness in the frame section's server block, the main header's magic and serverPid 0; --stale-until applies to it).
// --tailread ROUNDS [--tr-adapter N] (ticket 0758): only the tail read's offline cases (TailReadBench), then exit.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include "shell_shared.h"
#include "shell_model_tap.h"
#include "shell_model_scene.h"
#include "vp_frame.h"
#include "exe_camera.h"
#include "../composite/tail_read.h"   // ticket 0758: --tailread (the composite's tail read, offline)

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static FILE* g_log;
static LARGE_INTEGER g_freq;
static int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static void Log(const char* fmt, ...)
{
	char buf[1024];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, _TRUNCATE, fmt, ap); va_end(ap);
	SYSTEMTIME st; GetLocalTime(&st);
	if (g_log) { fprintf(g_log, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf); fflush(g_log); }
}

// ---------------------------------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------------------------------
struct WordAt { int frame; uint32_t word; bool mid; };
// zeroblock: a live server whose mode block is all zero (a producer older than 4753: no writerPid, no word)
enum { SRV_OK, SRV_PID0, SRV_PIDOTHER, SRV_STALE, SRV_NOMAGIC, SRV_NONE, SRV_ZEROBLOCK, SRV_COUNT };
static const wchar_t* kSrvNames[] = { L"ok", L"pid0", L"pidother", L"stale", L"nomagic", L"none", L"zeroblock" };
// --ring KIND (issue 4761): the product's handover=ring form of the server: the main header's magic and serverPid stay 0
// and the liveness is in the <section>.Frame section's server block (vp_frame.h): ok (magic, version, serverPid = this
// process, state READY, a fresh heartbeat), stale (heartbeat 10 s old), pidother (serverPid is not the mode block's
// writerPid), failed / stopped (state FAILED / STOPPED), nomagic (magic 0), rebuilding (state REBUILDING: still live)
enum { RING_NONE, RING_OK, RING_STALE, RING_PIDOTHER, RING_FAILED, RING_STOPPED, RING_NOMAGIC, RING_REBUILDING, RING_COUNT };
static const wchar_t* kRingNames[] = { L"none", L"ok", L"stale", L"pidother", L"failed", L"stopped", L"nomagic", L"rebuilding" };
enum { BAD_NONE, BAD_LEVEL, BAD_BLOB, BAD_PUSH, BAD_CALL, BAD_STORE, BAD_PATTERN, BAD_HANDLE, BAD_NULLOBJ, BAD_STAGE, BAD_VSPTR, BAD_DEVICE,
	BAD_MBLOB, BAD_MSTORE, BAD_COUNT };   // ticket 4874: mblob, mstore break a model program's site only
static const wchar_t* kBadNames[] = { L"none", L"level", L"blob", L"push", L"call", L"store", L"pattern", L"handle", L"nullobj", L"stage", L"vsptr", L"device",
	L"mblob", L"mstore" };
// ticket 4874: --mset KIND, the identity set the host (as the server) publishes
enum { MSET_OK, MSET_NONE, MSET_TORN, MSET_BADHASH, MSET_BADMAGIC, MSET_PIDOTHER, MSET_BADVERSION, MSET_BOUNDS, MSET_GEN, MSET_COUNT };
static const wchar_t* kMsetNames[] = { L"ok", L"none", L"torn", L"badhash", L"badmagic", L"pidother", L"badversion", L"bounds", L"gen" };
static struct
{
	wchar_t dll[MAX_PATH] = L"", log[MAX_PATH] = L"", section[128] = L"", dump[MAX_PATH] = L"";
	int frames = 12, holdMs = 1500, server = SRV_OK, staleUntil = -1, bad = BAD_NONE, ring = RING_NONE;
	bool fakeExe = false;
	bool waitPresentOnly = false;   // --wait-hooks present: hook_units=0 runs (the filter's own hooks are not expected)
	std::vector<WordAt> words;
	// ticket 4874: the model scene (see the model section below)
	bool models = false;            // --models: draw the model cells, play the server's part of the model tap
	int mset = 0;                   // --mset KIND (MSET_*)
	uint32_t want = 0;              // --want BITS: the server's wantBits
	bool deferEarly = false;        // --defer-early: the deferred list is recorded before the immediate context's Maps
	int minst = 2;                  // --minst N: instances of the block cell (a big N exhausts the tap's budget)
	int setAt = 0;                  // --set-at F: the identity set is published at frame F (before: none)
	bool early = false;              // --early: the model resources are created before the composite is loaded
	int writeAt = -1;               // --write-at F: UpdateSubresource of t0 (texture 0) at frame F (its identity is cleared)
	// ticket 4879: --cpu draws two CPU-built cells (R32, immutable: block programs with 32-B vertices, material 3 with
	// 20-B ones) on the immediate context; --cpu-set held|never|always: when the host (as the server) puts their keys into
	// the identity set (held: once it has read both cells' buffers from the geometry section and found their bytes equal)
	bool cpu = false;
	int cpuSet = 0;
	int cpuStop = -1;                // ticket 0597: --cpu-stop F (see g_cStopped)
	int cpuLate = 0;                 // ticket 0597: --cpu-late F: cell 1 is drawn only from frame F
	int cpuPad = 0;                  // ticket 0597: --cpu-pad N: cell 1's vertex buffer is 60 + N bytes (3 vertices used)
	bool cpuFrag = false;            // ticket 0597: --cpu-frag: cell 0's vertex buffer is 112 bytes, its last 16 chosen so that the
	                                 // compositor copies it before the index buffer (lower table slot: unit_filter.cpp GeomSlot)
	// ticket 0719 (the composite's cached map): --map-mode whole|split|noover: how the slot-1 instance buffer is written each
	// frame (whole: one WRITE_DISCARD map; split: WRITE_DISCARD of its first half, then WRITE_NO_OVERWRITE of the rest; noover:
	// created with initial data and only ever mapped WRITE_NO_OVERWRITE); --map-pad N: N bytes after the instances that no
	// draw reads, written every frame (past the copy's high-water mark); --map-def: the deferred context maps the ids and the
	// palette buffers (WRITE_DISCARD, same contents) before its cells; --map-check: after each frame's command list ran, the
	// three buffers are copied to staging buffers and compared with what the host wrote ("B <f> inst=<r> ids=<r> pal=<r>",
	// r = ok or bad@<byte>)
	int mapMode = 0;
	int mapPad = 0;
	bool mapDef = false;
	bool mapCheck = false;
	int tailRead = 0;                // ticket 0758: --tailread ROUNDS (TailReadBench below; no composite, no section)
	int trAdapter = -1;              // ticket 0758: --tr-adapter N (DXGI order; -1 = the default adapter)
} O;
enum { MM_WHOLE, MM_SPLIT, MM_NOOVER, MM_COUNT };
static const wchar_t* kMapModeNames[] = { L"whole", L"split", L"noover" };
enum { CS_HELD, CS_NEVER, CS_ALWAYS, CS_COUNT };
static const wchar_t* kCpuSetNames[] = { L"held", L"never", L"always" };

// ---------------------------------------------------------------------------------------------------------------------
// Shaders: 12 programs (unit_programs.h order: 0..4 DROP, 5..11 KEEP) and the "other" pair (ids 12 VS, 13 PS); ticket
// 4874: ids 14..23 = the ten model programs (shell_model_scene.h order: 0 VS, 1 PS, 2 PS, 3 VS, 4 VS, 5 VS, 6 PS, 7 PS,
// 8 VS, 9 VS), compiled and created always (the fake image holds their sites too), drawn only with --models
// ---------------------------------------------------------------------------------------------------------------------
enum { NSH = 24, MSH0 = 14 };
static const bool kIsVs[NSH] = { true, true, false, false, false, true, false, false, true, false, true, false, true, false,
	true, false, false, true, true, true, false, false, true, true };
static ID3DBlob* g_code[NSH];
static ID3D11VertexShader* g_vsObj[NSH];
static ID3D11PixelShader* g_psObj[NSH];

static void Colour(int id, float c[3])
{
	if (id >= MSH0) { c[0] = (float)(20 + (id - MSH0) * 23) / 255.0f; c[1] = 90.0f / 255.0f; c[2] = (float)(240 - (id - MSH0) * 20) / 255.0f; return; }
	c[0] = (float)((id + 1) * 17) / 255.0f; c[1] = 200.0f / 255.0f; c[2] = (float)(250 - id * 13) / 255.0f;
}

static bool Compile()
{
	for (int id = 0; id < NSH; ++id)
	{
		char src[512];
		if (kIsVs[id])
			_snprintf_s(src, _TRUNCATE, "float4 main(uint v : SV_VertexID) : SV_Position { float2 t = float2((v << 1) & 2, v & 2); "
				"return float4(t * float2(2, -2) + float2(-1, 1), %.3f, 1); }", 0.05 + 0.01 * id);
		else
		{
			float c[3]; Colour(id, c);
			_snprintf_s(src, _TRUNCATE, "float4 main(float4 p : SV_Position) : SV_Target { return float4(%.6f, %.6f, %.6f, 1); }", c[0], c[1], c[2]);
		}
		ID3DBlob* err = nullptr;
		HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", kIsVs[id] ? "vs_4_0" : "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &g_code[id], &err);
		if (FAILED(hr)) { Log("compile %d failed 0x%08X: %s", id, hr, err ? (const char*)err->GetBufferPointer() : ""); SafeRelease(err); return false; }
		SafeRelease(err);
	}
	return true;
}

static bool CreateShaders(ID3D11Device* dev)
{
	for (int id = 0; id < NSH; ++id)
	{
		HRESULT hr = kIsVs[id] ? dev->CreateVertexShader(g_code[id]->GetBufferPointer(), g_code[id]->GetBufferSize(), nullptr, &g_vsObj[id])
		                       : dev->CreatePixelShader(g_code[id]->GetBufferPointer(), g_code[id]->GetBufferSize(), nullptr, &g_psObj[id]);
		if (FAILED(hr)) { Log("create %d failed 0x%08X", id, hr); return false; }
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fake EXE image for path 2 (the facts of unit-draw-filter.md 3.2, written down here independently of unit_filter.cpp)
// ---------------------------------------------------------------------------------------------------------------------
struct Site { uint32_t va, pushAt, callAt, storeAt, handleGlobal; };
// 0..11 the units' programs; 12..21 (ticket 4874) the model programs, from render/evidence/0561/4874/p5_sites.txt
static const int kNSites = 22;
static inline int SiteShader(int i) { return i < 12 ? i : MSH0 + (i - 12); }   // the host's shader id of site i
static const Site kSites[kNSites] = {
	{ 0x6803140u, 0x4ee8e2u, 0x4ee8edu, 0x4ee907u, 0x2de1d5cu }, { 0x68005d0u, 0x4ee885u, 0x4ee890u, 0x4ee89au, 0x2de1d50u },
	{ 0x6813630u, 0x4eea2bu, 0x4eea36u, 0x4eea4bu, 0x2de1d8cu }, { 0x6811108u, 0x4ee9f5u, 0x4eea00u, 0x4eea15u, 0x2de1d84u },
	{ 0x680cc38u, 0x4ee989u, 0x4ee994u, 0x4ee9a9u, 0x2de1d74u }, { 0x6804e48u, 0x4ee902u, 0x4ee90du, 0x4ee922u, 0x2de1d60u },
	{ 0x6815328u, 0x4eea46u, 0x4eea51u, 0x4eea67u, 0x2de1d90u }, { 0x6812da0u, 0x4eea10u, 0x4eea1bu, 0x4eea30u, 0x2de1d88u },
	{ 0x6802938u, 0x4ee8c2u, 0x4ee8cdu, 0x4ee8e7u, 0x2de1d58u }, { 0x680e7f0u, 0x4ee9a4u, 0x4ee9afu, 0x4ee9c4u, 0x2de1d78u },
	{ 0x677f838u, 0x4edb5cu, 0x4edb67u, 0x4edb81u, 0x2de1a94u }, { 0x678a7e0u, 0x4edc03u, 0x4edc0eu, 0x4edc23u, 0x2de1aacu },
	{ 0x619f1e8u, 0x4ebbebu, 0x4ebbf6u, 0x4ebc0du, 0x2de158cu }, { 0x61a7d00u, 0x4ebcb4u, 0x4ebcbfu, 0x4ebcd4u, 0x2de15a8u },
	{ 0x61a56e8u, 0x4ebc7eu, 0x4ebc89u, 0x4ebc9eu, 0x2de15a0u }, { 0x619be88u, 0x4ebb91u, 0x4ebb9cu, 0x4ebbb6u, 0x2de1580u },
	{ 0x61a0cb0u, 0x4ebc28u, 0x4ebc33u, 0x4ebc4du, 0x2de1594u }, { 0x61a0320u, 0x4ebc08u, 0x4ebc13u, 0x4ebc2du, 0x2de1590u },
	{ 0x61a9c98u, 0x4ebccfu, 0x4ebcdau, 0x4ebcf1u, 0x2de15acu }, { 0x61a76f8u, 0x4ebc99u, 0x4ebca4u, 0x4ebcb9u, 0x2de15a4u },
	{ 0x619e9b0u, 0x4ebbceu, 0x4ebbd9u, 0x4ebbf0u, 0x2de1588u }, { 0x61a2bb8u, 0x4ebc48u, 0x4ebc53u, 0x4ebc68u, 0x2de1598u },
};
static const uint32_t kCreateVs = 0x8bbc40u, kCreatePs = 0x8bbbf0u, kRenderer = 0x3010570u, kDevice = 0x301057cu, kLevel = 0x2e49edcu,
	kPsObj = 0x300df68u, kVsObj = 0x300ef68u, kVsBc = 0x3006f68u, kVsSize = 0x3005f68u, kImageEnd = 0x6848000u;

static uint8_t* g_fake;
static uint8_t* g_renderer;
static inline uint8_t* FA(uint32_t va) { return g_fake + (va - VPEXE_IMAGE_BASE); }
static bool Commit(uint32_t va, size_t n)
{
	uintptr_t a = (uintptr_t)FA(va) & ~(uintptr_t)0xFFF, e = ((uintptr_t)FA(va) + n + 0xFFF) & ~(uintptr_t)0xFFF;
	return VirtualAlloc((void*)a, e - a, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}
static void Put(uint32_t va, const void* p, size_t n) { memcpy(FA(va), p, n); }
static void Put32(uint32_t va, uint32_t v) { memcpy(FA(va), &v, 4); }
static uint32_t Abs(uint32_t va) { return (uint32_t)(uintptr_t)FA(va); }

static bool FakeExe(ID3D11Device* dev)
{
	g_fake = (uint8_t*)VirtualAlloc(nullptr, kImageEnd - VPEXE_IMAGE_BASE, MEM_RESERVE, PAGE_NOACCESS);
	if (!g_fake) { Log("fake EXE: reserve failed %u", GetLastError()); return false; }
	const uint32_t base = (uint32_t)(uintptr_t)g_fake;
	// Ticket 4964 (minimal, until 4965 converts this host): the camera part is laid out from the build table for build
	// 4390913, the build this host's other addresses are.
	const int build = kVpExeBuild4390913;
	VPCamSetBuild(build);
	bool ok = Commit(VPEXE_IMAGE_BASE, 0x1000);
	for (int i = 0; i < kVPCamNumSigs; ++i) ok = ok && Commit(VpExeVa(build, kVPCamSigs[i].sym), 96);
	ok = ok && Commit(VPCAM_VA_APP, 4) && Commit(VPCAM_VA_NUMVIEWS, 0x100) && Commit(VPCAM_VA_GRID, 0x100) && Commit(VPCAM_VA_PROJ, 0x140) && Commit(VPCAM_VA_VIEWPORT, 24);
	// the filter's pieces
	ok = ok && Commit(0x8bbbf0u, 0xC0) && Commit(0x4e5c20u, 8) && Commit(0x4e6099u, 8) && Commit(0x4edb50u, 0xE0) && Commit(0x4ee880u, 0x200);
	ok = ok && Commit(0x2de1a90u, 0x400) && Commit(kLevel, 4) && Commit(kVsSize, 0x1000) && Commit(kVsBc, 0x1000) && Commit(kPsObj, 0x2000) && Commit(kRenderer, 0x10);
	ok = ok && Commit(0x4ebb80u, 0x180) && Commit(0x2de1580u, 0x40);   // ticket 4874: the model programs' sites and handles
	for (int i = 0; i < kNSites; ++i) ok = ok && Commit(kSites[i].va, g_code[SiteShader(i)]->GetBufferSize());
	if (!ok) { Log("fake EXE: commit failed %u", GetLastError()); return false; }
	uint8_t* b = g_fake;
	VpExeFakeHeader(b, build);
	if (!VpExeLaySigs(build, base, g_fake, kImageEnd - VPEXE_IMAGE_BASE, kVPCamSigs, kVPCamNumSigs, -1)) { Log("fake EXE: a camera pattern does not expand"); return false; }
	// instruction patterns: the create functions, the init's renderer and level reads
	struct P { uint32_t va; uint8_t n; uint8_t bytes[6]; int reloc; uint32_t relocVa; };
	const P pats[] = {
		{ 0x8bbbf0u, 1, { 0xA1 }, 1, kDevice }, { 0x8bbc11u, 6, { 0x8B, 0x8E, 0x5C, 0x04, 0x00, 0x00 }, -1, 0 }, { 0x8bbc1bu, 3, { 0x89, 0x14, 0x8D }, 3, kPsObj },
		{ 0x8bbc40u, 1, { 0xA1 }, 1, kDevice }, { 0x8bbc67u, 6, { 0x8B, 0x8E, 0x58, 0x04, 0x00, 0x00 }, -1, 0 }, { 0x8bbc73u, 3, { 0x89, 0x14, 0x8D }, 3, kVsObj },
		{ 0x8bbc8fu, 3, { 0x89, 0x1C, 0x95 }, 3, kVsBc }, { 0x8bbc9cu, 3, { 0x89, 0x3C, 0x85 }, 3, kVsSize },
		{ 0x4e5c20u, 2, { 0x8B, 0x0D }, 2, kRenderer }, { 0x4e6099u, 1, { 0xA1 }, 1, kLevel },
	};
	int pi = 0;
	for (const P& p : pats)
	{
		Put(p.va, p.bytes, p.n);
		if (p.reloc >= 0) Put32(p.va + p.reloc, Abs(p.relocVa));
		if (O.bad == BAD_PATTERN && pi == 2) FA(p.va)[1] ^= 0x01;
		++pi;
	}
	// the renderer, the device, the level
	g_renderer = (uint8_t*)calloc(1, 0x500);
	*(uint32_t*)(g_renderer + 0x458) = 128;   // VS count
	*(uint32_t*)(g_renderer + 0x45c) = 362;   // PS count
	Put32(kRenderer, (uint32_t)(uintptr_t)g_renderer);
	Put32(kDevice, O.bad == BAD_DEVICE ? 0x12345678u : (uint32_t)(uintptr_t)dev);
	Put32(kLevel, O.bad == BAD_LEVEL ? 9u : 10u);
	// the programs: create site, handle, object, blob
	uint16_t nextVs = 40, nextPs = 100;
	for (int i = 0; i < kNSites; ++i)
	{
		const Site& s = kSites[i];
		const int sh = SiteShader(i);
		const bool vs = kIsVs[sh];
		uint8_t push[5] = { 0x68 }; uint32_t a = Abs(s.va); memcpy(push + 1, &a, 4);
		if (O.bad == BAD_PUSH && i == 3) push[2] ^= 0x10;
		Put(s.pushAt, push, 5);
		uint8_t call[5] = { 0xE8 }; int32_t rel = (int32_t)((vs ? kCreateVs : kCreatePs) - (s.callAt + 5)); memcpy(call + 1, &rel, 4);
		if (O.bad == BAD_CALL && i == 1) call[1] ^= 0x04;
		Put(s.callAt, call, 5);
		uint8_t store[6] = { 0x66, 0xA3 }; uint32_t g = Abs(s.handleGlobal); memcpy(store + 2, &g, 4);
		if (O.bad == BAD_STORE && i == 5) store[0] = 0x90;
		Put(s.storeAt, store, 6);
		const uint16_t h = vs ? nextVs++ : nextPs++;
		Put(s.handleGlobal, &h, 2);
		const size_t size = g_code[sh]->GetBufferSize();
		Put(s.va, g_code[sh]->GetBufferPointer(), size);
		if (O.bad == BAD_BLOB && i == 2) FA(s.va)[size - 3] ^= 0x01;
		if (O.bad == BAD_MBLOB && i == 14) FA(s.va)[size - 3] ^= 0x01;   // ticket 4874: a model program's blob (PS f0e1f0a6's site)
		if (O.bad == BAD_MSTORE && i == 16) store[0] = 0x90, Put(s.storeAt, store, 6);   // a model program's handle store
		if (vs)
		{
			Put32(kVsObj + 4 * h, (uint32_t)(uintptr_t)g_vsObj[sh]);
			Put32(kVsBc + 4 * h, O.bad == BAD_VSPTR && i == 0 ? Abs(s.va) + 4 : Abs(s.va));
			Put32(kVsSize + 4 * h, (uint32_t)size);
		}
		else
		{
			void* obj = g_psObj[sh];
			if (O.bad == BAD_NULLOBJ && i == 6) obj = nullptr;
			if (O.bad == BAD_STAGE && i == 2) obj = g_vsObj[0];
			Put32(kPsObj + 4 * h, (uint32_t)(uintptr_t)obj);
			if (O.bad == BAD_HANDLE && i == 4) { uint16_t big = 362; Put(s.handleGlobal, &big, 2); }
		}
	}
	wchar_t hex[16]; _snwprintf_s(hex, _TRUNCATE, L"%08X", base);
	SetEnvironmentVariableW(L"VPSHELL_EXE_BASE", hex);
	Log("fake EXE: image at 0x%08X, bad=%ls", base, kBadNames[O.bad]);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The section ("the server")
// ---------------------------------------------------------------------------------------------------------------------
static HANDLE g_sec;
static VPShellHeader* g_hdr;
static VPShellMode* Mode() { return g_hdr ? VPSHELL_MODE(g_hdr) : nullptr; }
static HANDLE g_fsec;                // --ring: the <section>.Frame section (issue 4761)
static VPFrameServer* g_fsrv;

static bool MakeSection()
{
	if (O.server == SRV_NONE) return true;
	g_sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, O.section);
	if (!g_sec) { Log("section: CreateFileMapping failed %u", GetLastError()); return false; }
	g_hdr = (VPShellHeader*)MapViewOfFile(g_sec, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if (!g_hdr) return false;
	const uint32_t pid = GetCurrentProcessId();
	g_hdr->version = VPSHELL_VERSION;
	g_hdr->serverPid = O.server == SRV_PID0 ? 0 : pid;
	g_hdr->qpcFrequency = g_freq.QuadPart;
	g_hdr->heartbeatQpc = Qpc();
	Mode()->writerPid = O.server == SRV_PIDOTHER ? pid + 1 : O.server == SRV_ZEROBLOCK ? 0 : pid;
	Mode()->word = 0;
	MemoryBarrier();
	g_hdr->magic = O.server == SRV_NOMAGIC ? 0 : VPSHELL_MAGIC;
	if (O.ring != RING_NONE)
	{
		// the product's ring form: nothing of the liveness in the main header (4757 #4248)
		g_hdr->magic = 0;
		g_hdr->serverPid = 0;
		wchar_t n[192];
		_snwprintf_s(n, _TRUNCATE, L"%ls%ls", O.section, VPFRAME_SECTION_SUFFIX);
		g_fsec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPFRAME_SECTION_BYTES, n);
		if (!g_fsec) { Log("frame section: CreateFileMapping failed %u", GetLastError()); return false; }
		void* p = MapViewOfFile(g_fsec, FILE_MAP_ALL_ACCESS, 0, 0, VPFRAME_SECTION_BYTES);
		if (!p) return false;
		g_fsrv = VPFRAME_SERVER(p);
		g_fsrv->version = VPFRAME_VERSION;
		g_fsrv->serverPid = O.ring == RING_PIDOTHER ? pid + 1 : pid;
		g_fsrv->state = O.ring == RING_FAILED ? VPFS_FAILED : O.ring == RING_STOPPED ? VPFS_STOPPED : O.ring == RING_REBUILDING ? VPFS_REBUILDING : VPFS_READY;
		g_fsrv->qpcFrequency = g_freq.QuadPart;
		g_fsrv->heartbeatQpc = Qpc();
		MemoryBarrier();
		g_fsrv->magic = O.ring == RING_NOMAGIC ? 0 : VPFRAME_MAGIC;
		Log("frame section %ls: ring=%ls magic=%08X serverPid=%u state=%u", n, kRingNames[O.ring], g_fsrv->magic, g_fsrv->serverPid, g_fsrv->state);
	}
	Log("section %ls: server=%ls serverPid=%u writerPid=%u magic=%08X", O.section, kSrvNames[O.server], g_hdr->serverPid, Mode()->writerPid, g_hdr->magic);
	return true;
}

static void Heartbeat(int frame)
{
	if (!g_hdr) return;
	const bool stale = O.server == SRV_STALE && (O.staleUntil < 0 || frame < O.staleUntil);
	InterlockedExchange64(&g_hdr->heartbeatQpc, stale ? Qpc() - 10 * g_freq.QuadPart : Qpc());
	if (g_fsrv)
	{
		const bool rstale = O.ring == RING_STALE && (O.staleUntil < 0 || frame < O.staleUntil);
		InterlockedExchange64(&g_fsrv->heartbeatQpc, rstale ? Qpc() - 10 * g_freq.QuadPart : Qpc());
	}
}

static void ApplyWords(int frame, bool mid)
{
	for (const WordAt& w : O.words)
		if (w.frame == frame && w.mid == mid && Mode())
		{
			Mode()->word = w.word;
			InterlockedIncrement((volatile LONG*)&Mode()->changes);
			Log("word 0x%X written at frame %d (%s)", w.word, frame, mid ? "mid-frame" : "frame start");
		}
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------
enum { E_DII, E_DI, E_D, E_DINST, E_DIIIND, E_DINSTIND };
struct Cell { int entry, vs, ps; };
static const Cell kCells[12] = {
	{ E_DII, 0, 2 }, { E_DI, 0, 3 }, { E_D, 1, 4 }, { E_DINST, 0, 2 }, { E_DIIIND, 0, 2 }, { E_DINSTIND, 0, 3 },
	{ E_DII, 12, 13 }, { E_DII, 5, 6 }, { E_DII, 12, 2 }, { E_DII, 1, 2 }, { E_DII, 10, 11 }, { E_DII, 1, 13 } };
static const int kCellPx = 8, kCells2 = 24;

static ID3D11Device* g_dev;
static ID3D11DeviceContext* g_imm;
static ID3D11DeviceContext* g_def;
static IDXGISwapChain* g_sc;
static ID3D11Texture2D* g_rt;
static ID3D11RenderTargetView* g_rtv;
static ID3D11Texture2D* g_stage;
static ID3D11Buffer* g_ib;
static ID3D11Buffer* g_argsII;
static ID3D11Buffer* g_argsInst;
static ID3D11Buffer* g_soVb;

static bool Resources()
{
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = kCellPx * kCells2; td.Height = kCellPx; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_rt)) || FAILED(g_dev->CreateRenderTargetView(g_rt, nullptr, &g_rtv))) return false;
	td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_stage))) return false;
	const uint16_t idx[3] = { 0, 1, 2 };
	D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 8; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA sd = { idx };
	if (FAILED(g_dev->CreateBuffer(&bd, &sd, &g_ib))) return false;
	const UINT a1[5] = { 3, 1, 0, 0, 0 }, a2[4] = { 3, 1, 0, 0 };
	bd.ByteWidth = 20; bd.BindFlags = 0; bd.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS; sd.pSysMem = a1;
	if (FAILED(g_dev->CreateBuffer(&bd, &sd, &g_argsII))) return false;
	bd.ByteWidth = 16; sd.pSysMem = a2;
	if (FAILED(g_dev->CreateBuffer(&bd, &sd, &g_argsInst))) return false;
	bd.ByteWidth = 256; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT; bd.MiscFlags = 0;
	if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_soVb))) return false;
	return true;
}

static void DrawCells(ID3D11DeviceContext* c, int firstCell)
{
	c->OMSetRenderTargets(1, &g_rtv, nullptr);
	c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	c->IASetInputLayout(nullptr);
	c->IASetIndexBuffer(g_ib, DXGI_FORMAT_R16_UINT, 0);
	for (int i = 0; i < 12; ++i)
	{
		const Cell& k = kCells[i];
		D3D11_VIEWPORT vp = { (float)((firstCell + i) * kCellPx), 0, (float)kCellPx, (float)kCellPx, 0, 1 };
		c->RSSetViewports(1, &vp);
		c->VSSetShader(g_vsObj[k.vs], nullptr, 0);
		c->PSSetShader(g_psObj[k.ps], nullptr, 0);
		switch (k.entry)
		{
		case E_DII: c->DrawIndexedInstanced(3, 1, 0, 0, 0); break;
		case E_DI: c->DrawIndexed(3, 0, 0); break;
		case E_D: c->Draw(3, 0); break;
		case E_DINST: c->DrawInstanced(3, 1, 0, 0); break;
		case E_DIIIND: c->DrawIndexedInstancedIndirect(g_argsII, 0); break;
		case E_DINSTIND: c->DrawInstancedIndirect(g_argsInst, 0); break;
		}
	}
}

static void Classify(const uint8_t* row, int firstCell, char* out)
{
	for (int i = 0; i < 12; ++i)
	{
		const uint8_t* p = row + ((firstCell + i) * kCellPx + kCellPx / 4) * 4;   // a pixel inside the cell's triangle
		float c[3]; Colour(kCells[i].ps, c);
		bool col = true;
		for (int k = 0; k < 3; ++k) if (abs((int)p[k] - (int)(c[k] * 255.0f + 0.5f)) > 1) col = false;
		out[i] = col ? 'P' : (p[0] == 0 && p[1] == 0 && p[2] == 0) ? 'D' : 'X';
	}
	out[12] = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ticket 4874: the model scene (--models). Nine cells per context, on the immediate and the deferred context, into a
// render target of their own with a depth view (cells 7 and 8 without one: the shadow mask's form):
//   cell  class    VS / PS (model program)   mesh  t0   set bits    dropped while
//   0     block    0 7ec4e9a5 / 1 c704c80b   0     0    cities      cities latched
//   1     single   3 c4bb25da / 2 f0e1f0a6   1     0    cities      cities latched (DrawIndexed; cb0 mapped per draw)
//   2     inst     0 / 2                     2     0    landmarks   landmarks latched
//   3     skinned  4 7a9e17f6 / 2            3     0    landmarks   landmarks latched (slot 1 ids, palette t27)
//   4     skinned  4 / 2                     4     0    cities      cities latched (two instances)
//   5     inst     0 / 2                     5     0    -           never (not in the set)
//   6     inst     0 / 2                     2     1    -           never (the same buffers and range, another t0)
//   7     inst     0 / 2                     2     0    landmarks   never (no depth view bound)
//   8     mask     5 88c2b7d4 / 7 e12e9eaf   2     0    -           never (pass 1, kept)
// The host also plays the server's part: the identity set section (keys computed here with the header's own hash
// functions over the bytes it uploaded), the control block's wantBits and setGeneration, and the reader of the tap's
// frames, which checks each frame's records against what it drew (keys, instance rows, cb0, palette, cb1, the PS t2-t4
// identities) and acks. One line per frame: "M <f> imm=<9> def=<9> fs2=0x.. dc= dl= pc= pl= seq= nd= ndrop= check=..".
// ---------------------------------------------------------------------------------------------------------------------
enum { MC_BLOCK, MC_SINGLE, MC_INST, MC_SKIN, MC_MASK };
struct MCell { int cls; int vsM, psM; int mesh; int tex0; int dsv; uint8_t setBits; };
static const int kMCells = 9;
static const MCell kMCellsT[kMCells] = {
	{ MC_BLOCK, 0, 1, 0, 0, 1, VPMODE_CITIES }, { MC_SINGLE, 3, 2, 1, 0, 1, VPMODE_CITIES }, { MC_INST, 0, 2, 2, 0, 1, VPMODE_LANDMARKS },
	{ MC_SKIN, 4, 2, 3, 0, 1, VPMODE_LANDMARKS }, { MC_SKIN, 4, 2, 4, 0, 1, VPMODE_CITIES }, { MC_INST, 0, 2, 5, 0, 1, 0 },
	{ MC_INST, 0, 2, 2, 1, 1, 0 }, { MC_INST, 0, 2, 2, 0, 0, VPMODE_LANDMARKS }, { MC_MASK, 5, 7, 2, 0, 0, 0 } };
static const int kMMeshes = 6;
static ID3D11Buffer* g_mIb[kMMeshes];
static ID3D11Buffer* g_mVb[kMMeshes];
static uint8_t g_mIbBytes[kMMeshes][16], g_mVbBytes[kMMeshes][64];
static ID3D11Texture2D* g_mTex[6];             // t0 (two), t1, t2, t3, t4
static ID3D11ShaderResourceView* g_mSrv[6];
static uint64_t g_mTexId[6];
static ID3D11Buffer* g_mInst;                  // slot 1, stride 64 (block instances, then the landmark instance)
static ID3D11Buffer* g_mIds;                   // slot 1, stride 4 (skinned ids)
static ID3D11Buffer* g_mPal;                   // t27: 3 ids x 96 rows
static ID3D11ShaderResourceView* g_mPalSrv;
static ID3D11Buffer* g_mCb0;                   // VS b0 (256 B), mapped per single draw on the drawing context
static ID3D11Buffer* g_mCb1;                   // VS b1 and PS b1 (352 B), mapped per frame on the immediate context
static ID3D11Texture2D* g_mRt;
static ID3D11RenderTargetView* g_mRtv;
static ID3D11Texture2D* g_mDs;
static ID3D11DepthStencilView* g_mDsv;
static ID3D11Texture2D* g_mStage;
static std::vector<float> g_instData, g_palData;   // this frame's contents (the reader's expectation)
static std::vector<uint8_t> g_instPad;              // ticket 0719: this frame's --map-pad bytes (after the instances)
static ID3D11Buffer* g_bStage;                      // ticket 0719: --map-check's read-back buffer
static float g_cb1Data[88];
static float g_cb0Data[2][16];                      // [0] immediate, [1] deferred: rows 0-3 of the single cell
static uint32_t g_idsData[3] = { 1, 0, 2 };         // cell 3: id 1; cell 4: ids 0, 2
static const int kMInstLand = 1;                    // instances of the landmark cells
static HANDLE g_msetMap;
static VPModelSetHeader* g_mset;
static HANDLE g_mctlMap;
static VPShellModelTapControl* g_mctl;
static uint32_t g_msetGen;

static bool CResources();        // ticket 4879 (the CPU-built cells, below)
static void CPublish(int f);
static bool MBuf(UINT bytes, UINT bind, D3D11_USAGE usage, const void* init, ID3D11Buffer** out, UINT misc = 0)
{
	D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = bytes; bd.Usage = usage; bd.BindFlags = bind; bd.MiscFlags = misc;
	bd.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
	D3D11_SUBRESOURCE_DATA sd = { init };
	return SUCCEEDED(g_dev->CreateBuffer(&bd, init ? &sd : nullptr, out));
}

// a texture with a full mip chain of RGBA8 bytes filled from `seed`; its identity computed with the header's rule
static bool MTex(int i, UINT w, UINT h, UINT mips, int seed)
{
	std::vector<std::vector<uint8_t>> lv(mips);
	D3D11_SUBRESOURCE_DATA sd[8] = {};
	for (UINT m = 0; m < mips; ++m)
	{
		const UINT mw = std::max(1u, w >> m), mh = std::max(1u, h >> m);
		lv[m].resize((size_t)mw * mh * 4);
		for (size_t k = 0; k < lv[m].size(); ++k) lv[m][k] = (uint8_t)(seed * 31 + m * 7 + k * 13);
		sd[m].pSysMem = lv[m].data(); sd[m].SysMemPitch = mw * 4;
	}
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w; td.Height = h; td.MipLevels = mips; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(g_dev->CreateTexture2D(&td, sd, &g_mTex[i])) || FAILED(g_dev->CreateShaderResourceView(g_mTex[i], nullptr, &g_mSrv[i]))) return false;
	const uint32_t m = VPModelTextureMip(w, h, mips);
	g_mTexId[i] = VPModelTextureHash(lv[m].data(), std::max(1u, w >> m) * 4, std::max(1u, w >> m), std::max(1u, h >> m), DXGI_FORMAT_R8G8B8A8_UNORM);
	return true;
}

static bool MResources()
{
	for (int m = 0; m < kMMeshes; ++m)
	{
		uint16_t idx[8] = { (uint16_t)(100 + m), 0, 1, 2, (uint16_t)m, (uint16_t)m, 7, 9 };
		float vb[16];
		for (int k = 0; k < 16; ++k) vb[k] = (float)(m * 10 + k);
		memcpy(g_mIbBytes[m], idx, 16); memcpy(g_mVbBytes[m], vb, 64);
		if (!MBuf(16, D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, idx, &g_mIb[m]) || !MBuf(64, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, vb, &g_mVb[m])) return false;
	}
	// t0 (two: 256 x 256 with 3 mips, the identity is mip 2), t1 64 x 64, t2-t4 8 x 8
	if (!MTex(0, 256, 256, 3, 1) || !MTex(1, 256, 256, 3, 2) || !MTex(2, 64, 64, 1, 3) || !MTex(3, 8, 8, 1, 4) || !MTex(4, 8, 8, 1, 5) || !MTex(5, 8, 8, 1, 6)) return false;
	if (O.cpu && !CResources()) return false;   // ticket 4879
	const UINT instBytes = 64u * (O.minst + kMInstLand) + (UINT)O.mapPad;   // ticket 0719: --map-pad
	std::vector<uint8_t> instInit(O.mapMode == MM_NOOVER ? instBytes : 0, 0);
	if (O.mapCheck)   // ticket 0719: one staging buffer the size of the largest checked buffer
	{
		D3D11_BUFFER_DESC sd = {}; sd.ByteWidth = std::max(instBytes, 3u * 96u * 16u); sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		if (FAILED(g_dev->CreateBuffer(&sd, nullptr, &g_bStage))) return false;
	}
	if (!MBuf(instBytes, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, instInit.empty() ? nullptr : instInit.data(), &g_mInst) ||
	    !MBuf(64, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, nullptr, &g_mIds) ||
	    !MBuf(3 * 96 * 16, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, nullptr, &g_mPal) ||
	    !MBuf(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, nullptr, &g_mCb0) ||
	    !MBuf(352, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, nullptr, &g_mCb1)) return false;
	D3D11_SHADER_RESOURCE_VIEW_DESC vd = {}; vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	vd.Buffer.FirstElement = 0; vd.Buffer.NumElements = 3 * 96;
	if (FAILED(g_dev->CreateShaderResourceView(g_mPal, &vd, &g_mPalSrv))) return false;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = kCellPx * kMCells * 2; td.Height = kCellPx; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_mRt)) || FAILED(g_dev->CreateRenderTargetView(g_mRt, nullptr, &g_mRtv))) return false;
	td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_mDs)) || FAILED(g_dev->CreateDepthStencilView(g_mDs, nullptr, &g_mDsv))) return false;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_mStage))) return false;
	return true;
}

static void MKey(int cell, VPModelKey& k)
{
	const MCell& c = kMCellsT[cell];
	memset(&k, 0, sizeof(k));
	k.ib = VPModelHash64(g_mIbBytes[c.mesh] + 2, 14);
	k.vb = VPModelHash64(g_mVbBytes[c.mesh], 64);
	k.t0 = g_mTexId[c.tex0]; k.t1 = g_mTexId[2];
	k.startIndex = 1; k.indexCount = 3; k.baseVertex = 0; k.flags = 0;
}

// a write map of [at, at + n) of b (the bytes from p)
static void MMap(ID3D11DeviceContext* c, ID3D11Buffer* b, const void* p, size_t n, D3D11_MAP type = D3D11_MAP_WRITE_DISCARD, size_t at = 0)
{
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(c->Map(b, 0, type, 0, &m))) { memcpy((uint8_t*)m.pData + at, p, n); c->Unmap(b, 0); }
	else Log("Map type %d failed", (int)type);
}

// ticket 0719: the instance buffer as --map-mode writes it (the instances, then the --map-pad bytes)
static void MInstMaps(int f)
{
	std::vector<uint8_t> all(g_instData.size() * 4 + g_instPad.size());
	memcpy(all.data(), g_instData.data(), g_instData.size() * 4);
	if (!g_instPad.empty()) memcpy(all.data() + g_instData.size() * 4, g_instPad.data(), g_instPad.size());
	if (O.mapMode == MM_WHOLE) MMap(g_imm, g_mInst, all.data(), all.size());
	else if (O.mapMode == MM_NOOVER) MMap(g_imm, g_mInst, all.data(), all.size(), D3D11_MAP_WRITE_NO_OVERWRITE);
	else
	{
		const size_t h = (all.size() / 2) & ~(size_t)15;
		MMap(g_imm, g_mInst, all.data(), h);
		MMap(g_imm, g_mInst, all.data() + h, all.size() - h, D3D11_MAP_WRITE_NO_OVERWRITE, h);
	}
	(void)f;
}

// ticket 0719: --map-check: what the driver holds of [0, n) of b, against the bytes the host wrote; "ok" or "bad@<byte>"
static void MCheckOne(ID3D11Buffer* b, const void* want, UINT n, char* out, size_t outN)
{
	const D3D11_BOX box = { 0, 0, 0, n, 1, 1 };
	g_imm->CopySubresourceRegion(g_bStage, 0, 0, 0, 0, b, 0, &box);
	D3D11_MAPPED_SUBRESOURCE m;
	if (FAILED(g_imm->Map(g_bStage, 0, D3D11_MAP_READ, 0, &m))) { _snprintf_s(out, outN, _TRUNCATE, "mapfail"); return; }
	const uint8_t* g = (const uint8_t*)m.pData; const uint8_t* w = (const uint8_t*)want;
	UINT k = 0;
	while (k < n && g[k] == w[k]) ++k;
	g_imm->Unmap(g_bStage, 0);
	if (k == n) _snprintf_s(out, outN, _TRUNCATE, "ok"); else _snprintf_s(out, outN, _TRUNCATE, "bad@%u", k);
}

static void MCheckBuffers(int f)
{
	std::vector<uint8_t> inst(g_instData.size() * 4 + g_instPad.size());
	memcpy(inst.data(), g_instData.data(), g_instData.size() * 4);
	if (!g_instPad.empty()) memcpy(inst.data() + g_instData.size() * 4, g_instPad.data(), g_instPad.size());
	char a[32], b[32], c[32];
	MCheckOne(g_mInst, inst.data(), (UINT)inst.size(), a, sizeof(a));
	MCheckOne(g_mIds, g_idsData, (UINT)sizeof(g_idsData), b, sizeof(b));
	MCheckOne(g_mPal, g_palData.data(), (UINT)(g_palData.size() * 4), c, sizeof(c));
	Log("B %d inst=%s ids=%s pal=%s", f, a, b, c);
}

// the immediate context's once-per-frame writes (as the EXE's streams 850, 857, 860 and the cb1s)
static void MFrameMaps(int f)
{
	g_instData.assign((size_t)16 * (O.minst + kMInstLand), 0.0f);
	for (size_t i = 0; i < g_instData.size(); ++i) g_instData[i] = (float)(f * 1000 + (int)i);
	g_instPad.assign((size_t)O.mapPad, 0);
	for (size_t k = 0; k < g_instPad.size(); ++k) g_instPad[k] = (uint8_t)(f * 7 + k * 13 + 1);
	g_palData.assign(3 * 96 * 4, 0.0f);
	for (size_t i = 0; i < g_palData.size(); ++i) g_palData[i] = (float)(f * 100000 + (int)i) * 0.5f;
	for (int i = 0; i < 88; ++i) g_cb1Data[i] = (float)(f * 7 + i) * 0.25f;
	MInstMaps(f);
	MMap(g_imm, g_mIds, g_idsData, sizeof(g_idsData));
	MMap(g_imm, g_mPal, g_palData.data(), g_palData.size() * 4);
	MMap(g_imm, g_mCb1, g_cb1Data, sizeof(g_cb1Data));
}

static void MDrawCells(ID3D11DeviceContext* c, int firstCell, int f, int ctxIdx)
{
	if (O.mapDef && ctxIdx == 1)   // ticket 0719: the deferred context's own WRITE_DISCARD maps (this frame's contents; not with --defer-early)
	{
		MMap(c, g_mIds, g_idsData, sizeof(g_idsData));
		MMap(c, g_mPal, g_palData.data(), g_palData.size() * 4);
	}
	c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	c->IASetInputLayout(nullptr);
	ID3D11Buffer* cbs[2] = { g_mCb0, g_mCb1 };
	c->VSSetConstantBuffers(0, 2, cbs);
	c->PSSetConstantBuffers(1, 1, &g_mCb1);
	ID3D11ShaderResourceView* env[3] = { g_mSrv[3], g_mSrv[4], g_mSrv[5] };
	c->PSSetShaderResources(2, 3, env);
	c->PSSetShaderResources(1, 1, &g_mSrv[2]);
	for (int i = 0; i < kMCells; ++i)
	{
		const MCell& k = kMCellsT[i];
		c->OMSetRenderTargets(1, &g_mRtv, k.dsv ? g_mDsv : nullptr);
		D3D11_VIEWPORT vp = { (float)((firstCell + i) * kCellPx), 0, (float)kCellPx, (float)kCellPx, 0, 1 };
		c->RSSetViewports(1, &vp);
		c->VSSetShader(g_vsObj[MSH0 + k.vsM], nullptr, 0);
		c->PSSetShader(g_psObj[MSH0 + k.psM], nullptr, 0);
		c->IASetIndexBuffer(g_mIb[k.mesh], DXGI_FORMAT_R16_UINT, 0);
		c->PSSetShaderResources(0, 1, &g_mSrv[k.tex0]);
		const UINT stride0 = 16, off0 = 0;
		c->IASetVertexBuffers(0, 1, &g_mVb[k.mesh], &stride0, &off0);
		if (k.cls == MC_SINGLE)
		{
			for (int r = 0; r < 16; ++r) g_cb0Data[ctxIdx][r] = (float)(f * 50 + ctxIdx * 20 + r);
			MMap(c, g_mCb0, g_cb0Data[ctxIdx], 64);   // per draw on the drawing context (as the EXE's 256-B cb0)
			c->DrawIndexed(3, 1, 0);
			continue;
		}
		if (k.cls == MC_SKIN)
		{
			const UINT stride1 = 4, off1 = i == 3 ? 0u : 4u;
			c->IASetVertexBuffers(1, 1, &g_mIds, &stride1, &off1);
			c->VSSetShaderResources(27, 1, &g_mPalSrv);
			c->DrawIndexedInstanced(3, i == 3 ? 1 : 2, 1, 0, 0);
			continue;
		}
		const UINT stride1 = 64, off1 = k.cls == MC_BLOCK ? 0u : 64u * O.minst;
		c->IASetVertexBuffers(1, 1, &g_mInst, &stride1, &off1);
		c->DrawIndexedInstanced(3, k.cls == MC_BLOCK ? O.minst : kMInstLand, 1, 0, 0);
	}
	ID3D11Buffer* nb[2] = {};
	const UINT z[2] = {};
	c->IASetVertexBuffers(0, 2, nb, z, z);
}

static void MClassify(const uint8_t* row, int firstCell, char* out)
{
	for (int i = 0; i < kMCells; ++i)
	{
		const uint8_t* p = row + ((firstCell + i) * kCellPx + kCellPx / 4) * 4;
		float c[3]; Colour(MSH0 + kMCellsT[i].psM, c);
		bool col = true;
		for (int k = 0; k < 3; ++k) if (abs((int)p[k] - (int)(c[k] * 255.0f + 0.5f)) > 1) col = false;
		out[i] = col ? 'P' : (p[0] == 0 && p[1] == 0 && p[2] == 0) ? 'D' : 'X';
	}
	out[kMCells] = 0;
}

static uint32_t ServerPid() { return GetCurrentProcessId(); }

// the identity set section (the server's): keys of the cells with set bits (cell 7 shares cell 2's key)
static bool MMakeSet()
{
	if (O.mset == MSET_NONE) return true;
	wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELSET_FMT, GetCurrentProcessId());
	const uint32_t bytes = 64 + 64 * 8;
	g_msetMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, n);
	if (!g_msetMap) return false;
	g_mset = (VPModelSetHeader*)MapViewOfFile(g_msetMap, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
	if (!g_mset) return false;
	VPModelSetKey* keys = (VPModelSetKey*)((uint8_t*)g_mset + 64);
	uint32_t nk = 0;
	for (int i = 0; i < 5; ++i)
	{
		VPModelSetKey& k = keys[nk++];
		memset(&k, 0, sizeof(k));
		MKey(i, k.key);
		k.bits = kMCellsT[i].setBits;
		k.cls = VPMODEL_CLASS_NONE;
	}
	g_msetGen = 1;
	g_mset->seq = O.mset == MSET_TORN ? 1u : 0u;
	g_mset->magic = O.mset == MSET_BADMAGIC ? 0x12345678u : VPSHELL_MODELSET_MAGIC;
	g_mset->version = O.mset == MSET_BADVERSION ? 99u : VPSHELL_MODELSET_VERSION;
	g_mset->headerBytes = sizeof(VPModelSetHeader); g_mset->keyBytes = sizeof(VPModelSetKey);
	g_mset->generation = O.mset == MSET_GEN ? 7u : g_msetGen;
	g_mset->keyCount = O.mset == MSET_BOUNDS ? 100000u : nk;
	g_mset->keyOffset = 64;
	g_mset->serverPid = O.mset == MSET_PIDOTHER ? ServerPid() + 1 : ServerPid();
	g_mset->fnv1a64 = VPModelHash64(keys, (size_t)nk * sizeof(VPModelSetKey)) ^ (O.mset == MSET_BADHASH ? 1u : 0u);
	Log("model set %ls: %u keys, kind %ls", n, nk, kMsetNames[O.mset]);
	if (O.cpu && O.cpuSet == CS_ALWAYS) CPublish(-1);   // ticket 4879: the CPU keys before any buffer is held
	return true;
}

// the control block (the compositor's): open it once it exists, write wantBits and setGeneration
static void MControl(int f)
{
	if (!g_mctl)
	{
		wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELTAP_CONTROL_FMT, GetCurrentProcessId());
		g_mctlMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, n);
		if (!g_mctlMap) return;
		g_mctl = (VPShellModelTapControl*)MapViewOfFile(g_mctlMap, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_MODELTAP_CONTROL_BYTES);
		if (!g_mctl) return;
		Log("model control open at frame %d: magic %08X slots %u slot %u bytes", f, g_mctl->magic, g_mctl->slots, g_mctl->slotBytes);
	}
	g_mctl->wantBits = O.want;
	if (O.mset != MSET_NONE && f >= O.setAt && g_mctl->setGeneration != g_msetGen)
	{
		g_mctl->setGeneration = g_msetGen;
		Log("model set generation %u published at frame %d", g_msetGen, f);
	}
}

static bool RowsEq(const float* got, const float* want, size_t floats) { return !memcmp(got, want, floats * 4); }

// ---------------------------------------------------------------------------------------------------------------------
// Ticket 4879: the CPU-built cells (--cpu). Two cells on the immediate context into a target of their own with a depth
// view: C0 the block programs (VS 0 / PS 1) with an R32 index buffer and a 32-B vertex buffer, one slot-1 instance (a
// wall's form); C1 material 3 (VS 3 / PS 2) with the same R32 index buffer and a 20-B vertex buffer, VS cb0 mapped per
// draw (the Great Wall's and a bridge's form). All three buffers IMMUTABLE with initial data. The host as the server reads
// the geometry section (Local\VPShellModelGeom.<pid>), compares each entry's bytes with what it uploaded, and (--cpu-set
// held) publishes the two keys in a new identity set generation once all three buffers are entries.
// One line per frame: "C <f> cells=<2> held=<n> geom_gen=<g> set_gen=<s> check=<ok|...>".
// ---------------------------------------------------------------------------------------------------------------------
static ID3D11Buffer* g_cIb;
static ID3D11Buffer* g_cVb[2];
static uint8_t g_cIbBytes[24], g_cVbBytes[2][96 + 4096];
static UINT g_cVbN[2] = { 96, 60 };   // ticket 0597: the vertex buffers' sizes (--cpu-pad)
static const UINT kCStride[2] = { 32, 20 };
static ID3D11Texture2D* g_cRt;
static ID3D11RenderTargetView* g_cRtv;
static ID3D11Texture2D* g_cDs;
static ID3D11DepthStencilView* g_cDsv;
static ID3D11Texture2D* g_cStage;
static char g_cCells[3] = "PP";
static bool g_cPublished;
static HANDLE g_cGeomMap;
static const uint8_t* g_cGeom;
// ticket 0597: --cpu-stop <frame>: from that frame on the cell whose vertex buffer is an entry of the section is no longer
// drawn (its buffer goes unnamed: the LRU may free it when the section is full); -1 = none chosen yet
static unsigned g_cHeldMask;
static int g_cStopped = -1;
static bool g_cLateNow;   // ticket 0597: this frame's cell 1 was not drawn (--cpu-late)
static uint32_t g_cOfs[3] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };   // ticket 0597: the entries' offsets (ib, vb0, vb1)
// unit_filter.cpp's GeomSlot (the record table's slot of a buffer; the compositor's copy order is the slot order)
static unsigned CSlot(uint64_t h, uint32_t kind) { return (unsigned)((h ^ (h >> 29) ^ kind) * 2654435761u) & 4095u; }

static bool CResources()
{
	const uint32_t idx[6] = { 0, 1, 2, 2, 1, 0 };
	memcpy(g_cIbBytes, idx, sizeof(idx));
	for (int c = 0; c < 2; ++c)
		for (UINT k = 0; k < g_cVbN[c]; ++k) g_cVbBytes[c][k] = (uint8_t)(c * 50 + k * 3 + 1);
	if (O.cpuFrag)   // ticket 0597: the 16 pad bytes of cell 0's vertex buffer chosen for a slot well below the index buffer's
	{
		const unsigned si = CSlot(VPModelGeomHash(VPMGEOM_KIND_INDEX32, g_cIbBytes, 24), VPMGEOM_KIND_INDEX32);
		for (uint32_t seed = 0; seed < 100000; ++seed)
		{
			memcpy(g_cVbBytes[0] + 96, &seed, 4);
			const unsigned sv = CSlot(VPModelGeomHash(VPMGEOM_KIND_VERTEX, g_cVbBytes[0], g_cVbN[0]), VPMGEOM_KIND_VERTEX);
			if (sv + 8 < si) { Log("cpu-frag: seed %u, slots vb0 %u ib %u", seed, sv, si); break; }
		}
	}
	if (!MBuf(24, D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, g_cIbBytes, &g_cIb)) return false;
	for (int c = 0; c < 2; ++c)
		if (!MBuf(g_cVbN[c], D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, g_cVbBytes[c], &g_cVb[c])) return false;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = kCellPx * 2; td.Height = kCellPx; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_cRt)) || FAILED(g_dev->CreateRenderTargetView(g_cRt, nullptr, &g_cRtv))) return false;
	td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_cDs)) || FAILED(g_dev->CreateDepthStencilView(g_cDs, nullptr, &g_cDsv))) return false;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	return SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &g_cStage));
}

static void CKey(int c, VPModelKey& k)
{
	memset(&k, 0, sizeof(k));
	k.ib = VPModelGeomHash(VPMGEOM_KIND_INDEX32, g_cIbBytes, 24);
	k.vb = VPModelGeomHash(VPMGEOM_KIND_VERTEX, g_cVbBytes[c], g_cVbN[c]);
	k.t0 = g_mTexId[0]; k.t1 = g_mTexId[2];
	k.startIndex = 0; k.indexCount = 3; k.baseVertex = 0; k.flags = VPMKEY_INDEX32;
}
static int CCellOf(const VPModelKey& k)
{
	for (int c = 0; c < 2; ++c) { VPModelKey w; CKey(c, w); if (!memcmp(&w, &k, sizeof(w))) return c; }
	return -1;
}

static void CDraw(int f)
{
	ID3D11DeviceContext* c = g_imm;
	const float black[4] = { 0, 0, 0, 0 };
	c->ClearRenderTargetView(g_cRtv, black);
	c->ClearDepthStencilView(g_cDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	c->OMSetRenderTargets(1, &g_cRtv, g_cDsv);
	c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	c->IASetInputLayout(nullptr);
	ID3D11Buffer* cbs[2] = { g_mCb0, g_mCb1 };
	c->VSSetConstantBuffers(0, 2, cbs);
	c->PSSetConstantBuffers(1, 1, &g_mCb1);
	ID3D11ShaderResourceView* env[3] = { g_mSrv[3], g_mSrv[4], g_mSrv[5] };
	c->PSSetShaderResources(2, 3, env);
	c->PSSetShaderResources(0, 1, &g_mSrv[0]);
	c->PSSetShaderResources(1, 1, &g_mSrv[2]);
	c->IASetIndexBuffer(g_cIb, DXGI_FORMAT_R32_UINT, 0);
	for (int i = 0; i < 2; ++i)
	{
		if (i == g_cStopped) continue;   // ticket 0597: --cpu-stop
		if (i == 1 && f < O.cpuLate) continue;   // ticket 0597: --cpu-late
		D3D11_VIEWPORT vp = { (float)(i * kCellPx), 0, (float)kCellPx, (float)kCellPx, 0, 1 };
		c->RSSetViewports(1, &vp);
		c->VSSetShader(g_vsObj[MSH0 + (i == 0 ? 0 : 3)], nullptr, 0);
		c->PSSetShader(g_psObj[MSH0 + (i == 0 ? 1 : 2)], nullptr, 0);
		const UINT off0 = 0;
		c->IASetVertexBuffers(0, 1, &g_cVb[i], &kCStride[i], &off0);
		if (i == 0)
		{
			const UINT stride1 = 64, off1 = 0;
			c->IASetVertexBuffers(1, 1, &g_mInst, &stride1, &off1);
			c->DrawIndexedInstanced(3, 1, 0, 0, 0);
		}
		else
		{
			float cb0[16];
			for (int r = 0; r < 16; ++r) cb0[r] = (float)(f * 3 + r) * 0.5f;
			MMap(c, g_mCb0, cb0, 64);
			c->DrawIndexed(3, 0, 0);
		}
	}
	ID3D11Buffer* nb[2] = {};
	const UINT z[2] = {};
	c->IASetVertexBuffers(0, 2, nb, z, z);
}

static void CClassify()
{
	D3D11_MAPPED_SUBRESOURCE m;
	g_cCells[0] = g_cCells[1] = 'X';
	if (FAILED(g_imm->Map(g_cStage, 0, D3D11_MAP_READ, 0, &m))) return;
	const uint8_t* row = (const uint8_t*)m.pData + (size_t)m.RowPitch * (kCellPx / 2);
	for (int i = 0; i < 2; ++i)
	{
		const uint8_t* p = row + (i * kCellPx + kCellPx / 4) * 4;
		float col[3]; Colour(MSH0 + (i == 0 ? 1 : 2), col);
		bool same = true;
		for (int k = 0; k < 3; ++k) if (abs((int)p[k] - (int)(col[k] * 255.0f + 0.5f)) > 1) same = false;
		g_cCells[i] = same ? 'P' : (p[0] == 0 && p[1] == 0 && p[2] == 0) ? 'D' : 'X';
	}
	if (g_cStopped >= 0) g_cCells[g_cStopped] = 'S';   // ticket 0597: not drawn (--cpu-stop)
	if (g_cLateNow) g_cCells[1] = 'L';   // ticket 0597: not drawn yet (--cpu-late)
	g_imm->Unmap(g_cStage, 0);
}

// the identity set's CPU keys: a new generation with the five model keys and the two CPU keys
static void CPublish(int f)
{
	if (!g_mset || g_cPublished) return;
	VPModelSetKey* keys = (VPModelSetKey*)((uint8_t*)g_mset + 64);
	uint32_t nk = g_mset->keyCount;
	g_mset->seq++;
	for (int c = 0; c < 2; ++c)
	{
		VPModelSetKey& k = keys[nk++];
		memset(&k, 0, sizeof(k));
		CKey(c, k.key);
		k.bits = VPMODE_CITIES;
		k.cls = VPMODEL_CLASS_NONE;
	}
	std::sort(keys, keys + nk, [](const VPModelSetKey& a, const VPModelSetKey& b) { return memcmp(&a.key, &b.key, sizeof(VPModelKey)) < 0; });
	g_msetGen++;
	g_mset->generation = g_msetGen;
	g_mset->keyCount = nk;
	g_mset->fnv1a64 = VPModelHash64(keys, (size_t)nk * sizeof(VPModelSetKey));
	g_mset->seq++;
	g_cPublished = true;
	Log("cpu keys published in set generation %u at frame %d (%u keys)", g_msetGen, f, nk);
}

// the geometry section, as the server reads it: how many of the three buffers are entries with equal bytes
static int CHeld(uint32_t& gen, char* why, size_t whyN)
{
	gen = 0;
	if (!g_cGeom)
	{
		wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELGEOM_FMT, GetCurrentProcessId());
		g_cGeomMap = OpenFileMappingW(FILE_MAP_READ, FALSE, n);
		if (!g_cGeomMap) return 0;
		g_cGeom = (const uint8_t*)MapViewOfFile(g_cGeomMap, FILE_MAP_READ, 0, 0, 0);
		if (!g_cGeom) return 0;
	}
	const VPModelGeomHeader* h = (const VPModelGeomHeader*)g_cGeom;
	if (h->seq & 1) return 0;
	if (h->magic != VPSHELL_MODELGEOM_MAGIC || h->version != VPSHELL_MODELGEOM_VERSION) { _snprintf_s(why, whyN, _TRUNCATE, "geom header"); return 0; }
	gen = h->generation;
	const VPModelGeomEntry* e = (const VPModelGeomEntry*)(g_cGeom + sizeof(VPModelGeomHeader));
	const uint8_t* data = g_cGeom + h->dataOffset;
	struct B { uint32_t kind; const uint8_t* p; uint32_t n, stride; } bufs[3] = {
		{ VPMGEOM_KIND_INDEX32, g_cIbBytes, 24, 4 }, { VPMGEOM_KIND_VERTEX, g_cVbBytes[0], g_cVbN[0], 32 }, { VPMGEOM_KIND_VERTEX, g_cVbBytes[1], g_cVbN[1], 20 } };
	int held = 0;
	g_cHeldMask = 0;
	g_cOfs[0] = g_cOfs[1] = g_cOfs[2] = 0xFFFFFFFFu;
	for (int bi = 0; bi < 3; ++bi)
	{
		const B& b = bufs[bi];
		const uint64_t hash = VPModelGeomHash(b.kind, b.p, b.n);
		for (uint32_t i = 0; i < h->maxEntries; ++i)
		{
			if (e[i].kind != b.kind || e[i].hash != hash) continue;
			if (e[i].bytes != b.n || e[i].stride != b.stride || memcmp(data + e[i].offset, b.p, b.n))
				{ _snprintf_s(why, whyN, _TRUNCATE, "geom entry %u: bytes or stride differ", i); return held; }
			held++;
			g_cHeldMask |= 1u << bi;   // ticket 0597: 1 the index buffer, 2 C0's vertex buffer, 4 C1's
			g_cOfs[bi] = e[i].offset;
			break;
		}
	}
	return held;
}

// read this frame's tap (copy, then check), check the records against what was drawn; the result goes to the M line
static uint32_t g_mHeldRecs;   // ticket 0788: HELD records of the last tap read (the "H" line)
static void MReadTap(int f, const char* imm, const char* def, char* res, size_t resN, uint64_t& seqOut, uint32_t& ndOut, uint32_t& ndropOut)
{
	seqOut = 0; ndOut = ndropOut = 0; g_mHeldRecs = 0;
	_snprintf_s(res, resN, _TRUNCATE, "noctl");
	if (!g_mctl) return;
	const uint32_t s0 = g_mctl->seq;
	MemoryBarrier();
	const uint64_t seq = g_mctl->frameSeq; const uint32_t slot = g_mctl->slot, bytes = g_mctl->frameBytes, slotBytes = g_mctl->slotBytes;
	const uint64_t fnv = g_mctl->fnv1a64;
	MemoryBarrier();
	if ((s0 & 1) || g_mctl->seq != s0) { _snprintf_s(res, resN, _TRUNCATE, "torn"); return; }
	seqOut = seq;
	if (!seq) { _snprintf_s(res, resN, _TRUNCATE, "noframe"); return; }
	wchar_t n[64]; _snwprintf_s(n, _TRUNCATE, VPSHELL_MODELTAP_DATA_FMT, GetCurrentProcessId());
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, n);
	if (!h) { _snprintf_s(res, resN, _TRUNCATE, "nodata"); return; }
	const uint64_t at = (uint64_t)slot * slotBytes;
	const uint8_t* v = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, (DWORD)(at >> 32), (DWORD)at, slotBytes);
	std::vector<uint8_t> img(bytes);
	if (v) { memcpy(img.data(), v, bytes); UnmapViewOfFile(v); }
	CloseHandle(h);
	if (!v || bytes < sizeof(VPModelTapFrame)) { _snprintf_s(res, resN, _TRUNCATE, "noview"); return; }
	const VPModelTapFrame* fr = (const VPModelTapFrame*)img.data();
	if (fr->magic != VPSHELL_MODELTAP_MAGIC || fr->version != VPSHELL_MODELTAP_VERSION || fr->frameSeq != seq || fr->imageBytes != bytes) { _snprintf_s(res, resN, _TRUNCATE, "header"); return; }
	if (VPModelHash64(img.data() + fr->headerBytes, bytes - fr->headerBytes) != fnv || fr->fnv1a64 != fnv) { _snprintf_s(res, resN, _TRUNCATE, "hash"); return; }
	if ((uint64_t)fr->drawOffset + (uint64_t)fr->drawCount * sizeof(VPModelTapDraw) > bytes || (uint64_t)fr->rowOffset + (uint64_t)fr->rowCount * 16 > bytes ||
	    (uint64_t)fr->constOffset + (uint64_t)fr->constCount * 16 > bytes || (uint64_t)fr->targetOffset + (uint64_t)fr->targetCount * 32 > bytes)
		{ _snprintf_s(res, resN, _TRUNCATE, "bounds"); return; }
	const VPModelTapDraw* d = (const VPModelTapDraw*)(img.data() + fr->drawOffset);
	const float (*rows)[4] = (const float (*)[4])(img.data() + fr->rowOffset);
	const VPModelTapConst* k = (const VPModelTapConst*)(img.data() + fr->constOffset);
	ndOut = fr->drawCount;
	// every dropped cell has exactly one DROPPED record with its key and its context; every DROPPED record is a dropped cell
	int wantDrops = 0;
	for (int i = 0; i < kMCells; ++i) wantDrops += (imm[i] == 'D') + (def[i] == 'D');
	if (O.cpu) wantDrops += (g_cCells[0] == 'D') + (g_cCells[1] == 'D');   // ticket 4879
	uint32_t ndrop = 0;
	for (uint32_t r = 0; r < fr->drawCount; ++r)
	{
		const VPModelTapDraw& x = d[r];
		if (x.flags & VPMTAP_DRAW_HELD) g_mHeldRecs++;   // ticket 0788 (a held record is passed: never DROPPED as well)
		if (!(x.flags & VPMTAP_DRAW_DROPPED)) continue;
		ndrop++;
		if (x.key.flags & VPMKEY_INDEX32)   // ticket 4879: a CPU-built cell's record
		{
			const int cc = CCellOf(x.key);
			if (!O.cpu || cc < 0 || g_cCells[cc] != 'D') { _snprintf_s(res, resN, _TRUNCATE, "record %u: an R32 record of no dropped CPU cell", r); return; }
			if (x.instanceCount != 1 || x.instRows != 4 || (x.flags & VPMTAP_DRAW_DEFERRED)) { _snprintf_s(res, resN, _TRUNCATE, "record %u (cpu cell %d): form", r, cc); return; }
			if (cc == 0 && !RowsEq(rows[x.instRow], g_instData.data(), 16)) { _snprintf_s(res, resN, _TRUNCATE, "record %u (cpu cell 0): instance row", r); return; }
			continue;
		}
		const bool deferred = (x.flags & VPMTAP_DRAW_DEFERRED) != 0;
		const char* cells = deferred ? def : imm;
		int cell = -1;
		for (int i = 0; i < kMCells && cell < 0; ++i)
		{
			VPModelKey want; MKey(i, want);
			if (cells[i] == 'D' && !memcmp(&want, &x.key, sizeof(want))) cell = i;
		}
		if (cell < 0) { _snprintf_s(res, resN, _TRUNCATE, "record %u: no dropped cell with its key", r); return; }
		const MCell& c = kMCellsT[cell];
		if (x.env[0] != g_mTexId[3] || x.env[1] != g_mTexId[4] || x.env[2] != g_mTexId[5]) { _snprintf_s(res, resN, _TRUNCATE, "record %u: env", r); return; }
		const uint32_t total = x.instRows * x.instanceCount;
		if ((uint64_t)x.instRow + total > fr->rowCount) { _snprintf_s(res, resN, _TRUNCATE, "record %u: rows out of bounds", r); return; }
		if (c.cls == MC_BLOCK || c.cls == MC_INST)
		{
			const size_t first = c.cls == MC_BLOCK ? 0 : (size_t)16 * O.minst;
			if (x.instRows != 4 || x.instanceCount != (uint32_t)(c.cls == MC_BLOCK ? O.minst : kMInstLand) || !RowsEq(rows[x.instRow], g_instData.data() + first, (size_t)4 * total))
				{ _snprintf_s(res, resN, _TRUNCATE, "record %u (cell %d): instance rows", r, cell); return; }
		}
		else if (c.cls == MC_SINGLE)
		{
			if (x.instRows != 4 || x.instanceCount != 1 || !RowsEq(rows[x.instRow], g_cb0Data[deferred ? 1 : 0], 16))
				{ _snprintf_s(res, resN, _TRUNCATE, "record %u (cell %d): cb0 rows", r, cell); return; }
		}
		else if (c.cls == MC_SKIN)
		{
			const int n0 = cell == 3 ? 0 : 1, cnt = cell == 3 ? 1 : 2;
			if (x.instRows != 96 || x.instanceCount != (uint32_t)cnt || x.skinId0 != g_idsData[n0]) { _snprintf_s(res, resN, _TRUNCATE, "record %u (cell %d): skinned form", r, cell); return; }
			for (int q = 0; q < cnt; ++q)
				if (!RowsEq(rows[x.instRow + 96 * q], g_palData.data() + (size_t)96 * 4 * g_idsData[n0 + q], 96 * 4))
					{ _snprintf_s(res, resN, _TRUNCATE, "record %u (cell %d): palette rows", r, cell); return; }
		}
		if (x.vsCb1 >= fr->constCount || x.psCb1 != x.vsCb1 || k[x.vsCb1].rows != 22 || !RowsEq(rows[k[x.vsCb1].firstRow], g_cb1Data, 88))
			{ _snprintf_s(res, resN, _TRUNCATE, "record %u (cell %d): cb1", r, cell); return; }
	}
	ndropOut = ndrop;
	if ((int)ndrop != wantDrops) { _snprintf_s(res, resN, _TRUNCATE, "%u dropped records for %d dropped cells", ndrop, wantDrops); return; }
	if (fr->dropped[0] + fr->dropped[1] != ndrop) { _snprintf_s(res, resN, _TRUNCATE, "header dropped %u+%u != %u", fr->dropped[0], fr->dropped[1], ndrop); return; }
	_snprintf_s(res, resN, _TRUNCATE, "ok");
	g_mctl->ackServerPid = ServerPid();
	g_mctl->ackResult = 0;
	MemoryBarrier();
	g_mctl->ackFrameSeq = seq;
	(void)f;
}

// the first 5 bytes of each implementation the composite must hook (MinHook writes a jump there)
struct Prologue { const char* what; const uint8_t* addr; uint8_t orig[5]; };
static Prologue g_pro[8];
static int g_nPro;
static void Remember(const char* what, void* obj, int slot)
{
	Prologue& p = g_pro[g_nPro++];
	p.what = what; p.addr = (const uint8_t*)(*(void***)obj)[slot]; memcpy(p.orig, p.addr, 5);
}
static bool AllHooked() { for (int i = 0; i < g_nPro; ++i) if (!memcmp(g_pro[i].addr, g_pro[i].orig, 5)) return false; return true; }

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static int Run()
{
	if (!Compile()) return 5;
	if (O.dump[0])
	{
		for (int id = 0; id < NSH; ++id)
		{
			wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%ls\\shader%02d.bin", O.dump, id);
			FILE* f = nullptr;
			if (_wfopen_s(&f, p, L"wb") || !f) return 7;
			fwrite(g_code[id]->GetBufferPointer(), 1, g_code[id]->GetBufferSize(), f);
			fclose(f);
		}
		Log("dumped %d shaders to %ls", NSH, O.dump);
		return 0;
	}
	WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VPUnitFilterHost";
	RegisterClassW(&wc);
	HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"unit_filter_host", WS_OVERLAPPEDWINDOW, 0, 0, 256, 96, nullptr, nullptr, wc.hInstance, nullptr);
	ShowWindow(wnd, SW_SHOWNOACTIVATE);
	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferDesc.Width = 192; sd.BufferDesc.Height = 32; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.OutputWindow = wnd; sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	D3D_FEATURE_LEVEL fl;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, &fl, &g_imm);
	if (FAILED(hr)) { Log("device failed 0x%08X", hr); return 4; }
	if (FAILED(g_dev->CreateDeferredContext(0, &g_def))) { Log("deferred context failed"); return 4; }
	Log("device %p FL %x immediate %p deferred %p", g_dev, fl, g_imm, g_def);
	if (!Resources()) { Log("resources failed"); return 4; }
	if (!MakeSection()) return 4;
	if (O.fakeExe && (!CreateShaders(g_dev) || !FakeExe(g_dev))) return 4;

	// slots: ID3D11Device CreateVertexShader 12, CreatePixelShader 15; ID3D11DeviceContext PSSetShader 9, DrawIndexedInstanced 20;
	// IDXGISwapChain Present 8 (d3d11.h / dxgi.h declaration order; the composite takes its slots from the SDK's C declarations)
	Remember("Present", g_sc, 8);
	if (!O.waitPresentOnly)
	{
		Remember("CreateVertexShader", g_dev, 12);
		Remember("CreatePixelShader", g_dev, 15);
		Remember("immediate PSSetShader", g_imm, 9);
		Remember("immediate DrawIndexedInstanced", g_imm, 20);
		Remember("deferred PSSetShader", g_def, 9);
		Remember("deferred DrawIndexedInstanced", g_def, 20);
	}
	// ticket 4874: --early creates the model meshes and textures before the composite is loaded: not in its creation table (the read-back finds them)
	if (O.models && O.early && !MResources()) { Log("model resources failed"); return 4; }
	HMODULE dll = LoadLibraryW(O.dll);
	if (!dll) { Log("LoadLibrary(%ls) failed %u", O.dll, GetLastError()); return 6; }
	Log("loaded %ls", O.dll);
	const int64_t t0 = Qpc();
	while (!AllHooked())
	{
		if (Qpc() - t0 > 20 * g_freq.QuadPart)
		{
			for (int i = 0; i < g_nPro; ++i) if (!memcmp(g_pro[i].addr, g_pro[i].orig, 5)) Log("not hooked: %s %p", g_pro[i].what, g_pro[i].addr);
			return 3;
		}
		Heartbeat(0);
		g_sc->Present(0, 0);
		Sleep(10);
	}
	Log("hooks in place after %.0f ms", (double)(Qpc() - t0) * 1000.0 / g_freq.QuadPart);
	if (!O.fakeExe && !CreateShaders(g_dev)) return 4;
	// ticket 4874: the model meshes and textures are created after the composite's hooks are in place (as the EXE's game
	// load comes after the composite), so their creations are hashed
	if (O.models && ((!O.early && !MResources()) || !MMakeSet())) { Log("model resources or set section failed"); return 4; }
	if (O.models)
	{
		// one Present before frame 0, so the composite's control block exists and the server's set and want bits are in
		// it before frame 0's Present latches the first word (a server sets its bits only after its set is applied)
		Heartbeat(0);
		g_sc->Present(0, 0);
		MControl(0);
	}

	for (int f = 0; f < O.frames; ++f)
	{
		Heartbeat(f);
		ApplyWords(f, false);
		const float black[4] = { 0, 0, 0, 0 };
		g_imm->ClearRenderTargetView(g_rtv, black);
		if (O.models)   // ticket 4874: the model scene's immediate part (its deferred part follows the units' deferred cells)
		{
			MControl(f);
			if (f == O.writeAt)   // a later write into t0 (texture 0): the composite clears its identity
			{
				std::vector<uint8_t> z((size_t)256 * 256 * 4, 0x55);
				g_imm->UpdateSubresource(g_mTex[0], 0, nullptr, z.data(), 256 * 4, 0);
				Log("t0 written by UpdateSubresource at frame %d", f);
			}
			if (O.deferEarly) MDrawCells(g_def, kMCells, f, 1);   // recorded before the immediate context's Maps
			MFrameMaps(f);
			g_imm->ClearRenderTargetView(g_mRtv, black);
			g_imm->ClearDepthStencilView(g_mDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			MDrawCells(g_imm, 0, f, 0);
			if (O.cpu) { g_cLateNow = f < O.cpuLate; CDraw(f); }   // ticket 4879 (0597: --cpu-late)
		}
		DrawCells(g_imm, 0);
		ApplyWords(f, true);
		// DrawAuto with the pair (0, 2): counted (dropped or passed), draws nothing (the buffer was never a stream-output target)
		const UINT stride = 16, off = 0;
		g_imm->IASetVertexBuffers(0, 1, &g_soVb, &stride, &off);
		g_imm->VSSetShader(g_vsObj[0], nullptr, 0);
		g_imm->PSSetShader(g_psObj[2], nullptr, 0);
		g_imm->DrawAuto();
		ID3D11Buffer* nullVb = nullptr;
		g_imm->IASetVertexBuffers(0, 1, &nullVb, &stride, &off);
		DrawCells(g_def, 12);
		if (O.models && !O.deferEarly) MDrawCells(g_def, kMCells, f, 1);
		ID3D11CommandList* cl = nullptr;
		if (FAILED(g_def->FinishCommandList(FALSE, &cl))) { Log("FinishCommandList failed"); return 4; }
		g_imm->ExecuteCommandList(cl, FALSE);
		cl->Release();
		if (O.models && O.mapCheck) MCheckBuffers(f);   // ticket 0719
		g_imm->CopyResource(g_stage, g_rt);
		if (O.models) g_imm->CopyResource(g_mStage, g_mRt);
		if (O.models && O.cpu) g_imm->CopyResource(g_cStage, g_cRt);
		g_sc->Present(0, 0);
		if (O.models && O.cpu) CClassify();
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(g_imm->Map(g_stage, 0, D3D11_MAP_READ, 0, &m))) { Log("Map failed"); return 4; }
		const uint8_t* row = (const uint8_t*)m.pData + (size_t)m.RowPitch * (kCellPx / 2);
		char imm[16], def[16];
		Classify(row, 0, imm);
		Classify(row, 12, def);
		g_imm->Unmap(g_stage, 0);
		VPShellMode* md = Mode();
		Log("F %d imm=%s def=%s latched=%u lpresent=%u dropped=%u passed_set=%u fstate=0x%X", f, imm, def, md ? md->latchedWord : 0,
			md ? md->latchedPresent : 0, md ? md->droppedTotal : 0, md ? md->passedSetTotal : 0, md ? md->filterState : 0);
		if (O.models)
		{
			D3D11_MAPPED_SUBRESOURCE mm;
			if (FAILED(g_imm->Map(g_mStage, 0, D3D11_MAP_READ, 0, &mm))) { Log("Map (models) failed"); return 4; }
			const uint8_t* mrow = (const uint8_t*)mm.pData + (size_t)mm.RowPitch * (kCellPx / 2);
			char mi[16], mdf[16];
			MClassify(mrow, 0, mi);
			MClassify(mrow, kMCells, mdf);
			g_imm->Unmap(g_mStage, 0);
			char res[160]; uint64_t seq; uint32_t nd, ndrop;
			MReadTap(f, mi, mdf, res, sizeof(res), seq, nd, ndrop);
			Log("M %d imm=%s def=%s fs2=0x%X dc=%u dl=%u pc=%u pl=%u seq=%llu nd=%u ndrop=%u check=%s", f, mi, mdf, md ? VPMODE_FILTERSTATE2(md) : 0,
				md ? VPMODE_DROPPED_CITIES(md) : 0, md ? VPMODE_DROPPED_LANDMARKS(md) : 0, md ? VPMODE_PASSEDSET_CITIES(md) : 0, md ? VPMODE_PASSEDSET_LANDMARKS(md) : 0,
				(unsigned long long)seq, nd, ndrop, res);
			Log("H %d nheld=%u", f, g_mHeldRecs);   // ticket 0788
			if (O.cpu)   // ticket 4879: the server's part for the CPU-built cells
			{
				uint32_t ggen = 0; char why[96] = "ok";
				const int held = CHeld(ggen, why, sizeof(why));
				if (held == 3 && O.cpuSet == CS_HELD) CPublish(f);
				Log("C %d cells=%s held=%d geom_gen=%u set_gen=%u check=%s", f, g_cCells, held, ggen, g_mset ? g_mset->generation : 0u, why);
				if (O.cpuStop >= 0 && f >= O.cpuStop && g_cStopped < 0)   // ticket 0597: stop the cell whose vertex buffer is held
				{
					g_cStopped = (g_cHeldMask & 2u) ? 0 : (g_cHeldMask & 4u) ? 1 : 0;
					Log("Cstop %d cell=%d mask=%u ofs=%d/%d/%d", f, g_cStopped, g_cHeldMask, (int)g_cOfs[0], (int)g_cOfs[1], (int)g_cOfs[2]);
				}
			}
		}
		MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
	}
	const int64_t h0 = Qpc();
	while ((Qpc() - h0) * 1000 < (int64_t)O.holdMs * g_freq.QuadPart)
	{
		Heartbeat(O.frames);
		g_sc->Present(0, 0);
		Sleep(15);
		MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
	}
	Log("done");
	return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ticket 0758: --tailread ROUNDS. The composite's tail read (shell/composite/tail_read.h, used by unit_filter.cpp OnUnmap)
// over the cases of 0757's bench (shell/evidence/0757/tailread/tailread.cpp): a 256 KB DYNAMIC vertex buffer mapped
// WRITE_DISCARD, its first half written (the EXE's part), then [half + soff, half + soff + n) read, at every level this CPU
// has (memcpy, sse4.1, avx2), into a destination at offset doff of a guarded buffer. Each read is compared with memcpy's of
// the same map, read before or after it alternately (bad), the bytes around the destination must stay untouched (guard),
// and with fill=full (the whole buffer written in this map: the tail is the host's bytes) memcpy's read must equal what the
// host wrote (pattern). src=cached repeats it on ordinary memory. The bench's tails (4, 16, 40, 84, 128 KB, page-aligned
// source, doff 0 and 8: the shadow is realloc'd) run ROUNDS rounds with the median time; the edge cases (unaligned source,
// lengths around 16, 32, 64 and a page, destinations at offset 1 and 8) run once each. Lines:
//   "TR cpu=<best level> adapter=<name> vendor=0x<id>"
//   "TC src=dyn|cached fill=half|full|- level=<name> soff=<o> n=<bytes> doff=<o> rounds=<r> med_us=<t> bad=<k> guard=<k> pattern=<k>"
//   "TR cases=<c> bad=<k> guard=<k> pattern=<k>"
// Exit 0 when all three are 0, 1 otherwise, 3 when the device or the buffer cannot be made.
// ---------------------------------------------------------------------------------------------------------------------
static int TailReadBench(int rounds)
{
	const int best = TailReadLevel();
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* imm = nullptr;
	D3D_FEATURE_LEVEL fl;
	IDXGIAdapter1* pick = nullptr;   // --tr-adapter N: DXGI's adapter N (the laptop's P3000 is not the default adapter)
	if (O.trAdapter >= 0)
	{
		IDXGIFactory1* fac = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fac)) || FAILED(fac->EnumAdapters1((UINT)O.trAdapter, &pick)))
		{ SafeRelease(fac); Log("TR adapter %d not found", O.trAdapter); return 3; }
		SafeRelease(fac);
	}
	HRESULT hr = D3D11CreateDevice(pick, pick ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &imm);
	SafeRelease(pick);
	if (FAILED(hr)) { Log("TR D3D11CreateDevice failed 0x%08lX", hr); return 3; }
	DXGI_ADAPTER_DESC ad = {};
	IDXGIDevice* xd = nullptr;
	IDXGIAdapter* xa = nullptr;
	if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && SUCCEEDED(xd->GetAdapter(&xa))) xa->GetDesc(&ad);
	SafeRelease(xa);
	SafeRelease(xd);
	char name[128] = "";
	WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, (int)sizeof(name), nullptr, nullptr);
	for (char* c = name; *c; ++c) if (*c == ' ') *c = '_';
	Log("TR cpu=%s adapter=%s vendor=0x%04X", kTailReadNames[best], name, ad.VendorId);
	const UINT kBuf = 256u * 1024u, kHalf = kBuf / 2, kGuard = 64u;
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = kBuf; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	ID3D11Buffer* b = nullptr;
	if (FAILED(dev->CreateBuffer(&bd, nullptr, &b))) { Log("TR CreateBuffer failed"); SafeRelease(imm); SafeRelease(dev); return 3; }
	std::vector<uint8_t> src(kBuf), cached(kBuf), ref(kBuf), dst(kBuf + 2 * kGuard + 16);
	for (UINT i = 0; i < kBuf; ++i) { src[i] = (uint8_t)(i * 131u + 7u); cached[i] = (uint8_t)((i * 29u + 3u) ^ (i >> 12)); }
	struct Case { uint32_t soff, n, doff; int rounds; };
	std::vector<Case> cases;
	const uint32_t tails[] = { 4096, 16384, 40960, 86016, 131072 };   // the bench's (0754's moving / rest tail: 41 / 86 KB)
	for (uint32_t n : tails) for (uint32_t d : { 0u, 8u }) cases.push_back({ 0u, n, d, rounds });
	const uint32_t soffs[] = { 0, 4, 16, 20, 36, 4084 };
	const uint32_t lens[] = { 1, 15, 16, 17, 31, 33, 63, 64, 65, 255, 257, 4084, 4196, 86116 };
	for (uint32_t so : soffs) for (uint32_t n : lens) for (uint32_t d : { 0u, 1u, 8u }) cases.push_back({ so, n, d, 1 });
	unsigned nCases = 0, allBad = 0, allGuard = 0, allPat = 0;
	for (int kind = 0; kind < 3; ++kind)   // 0 dyn/half, 1 dyn/full, 2 cached
	{
		for (int level = TAILREAD_COPY; level <= best; ++level)
		{
			for (const Case& c : cases)
			{
				std::vector<double> t;
				unsigned bad = 0, guard = 0, pat = 0;
				for (int i = 0; i < c.rounds; ++i)
				{
					const uint8_t* s = nullptr;
					bool mapped = false;
					if (kind < 2)
					{
						D3D11_MAPPED_SUBRESOURCE mp = {};
						if (FAILED(imm->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) { Log("TR Map failed"); SafeRelease(b); SafeRelease(imm); SafeRelease(dev); return 3; }
						mapped = true;
						uint8_t* p = (uint8_t*)mp.pData;
						memcpy(p, src.data(), kind == 1 ? kBuf : kHalf);
						s = p + kHalf + c.soff;
					}
					else s = cached.data() + kHalf + c.soff;
					memset(dst.data(), 0xCD, dst.size());
					uint8_t* const d = dst.data() + kGuard + c.doff;
					const bool refFirst = (i & 1) != 0;
					if (refFirst) memcpy(ref.data(), s, c.n);
					const int64_t t0 = Qpc();
					TailRead(level, d, s, c.n);
					const int64_t t1 = Qpc();
					if (!refFirst) memcpy(ref.data(), s, c.n);
					if (memcmp(d, ref.data(), c.n)) ++bad;
					for (size_t k = 0; k < dst.size(); ++k)
						if ((k < kGuard + c.doff || k >= kGuard + c.doff + c.n) && dst[k] != 0xCD) { ++guard; break; }
					if (kind == 1 && memcmp(ref.data(), src.data() + kHalf + c.soff, c.n)) ++pat;
					if (kind == 2 && memcmp(ref.data(), cached.data() + kHalf + c.soff, c.n)) ++pat;
					if (mapped) imm->Unmap(b, 0);
					if ((i & 15) == 15) imm->Flush();
					t.push_back((double)(t1 - t0) * 1e6 / (double)g_freq.QuadPart);
				}
				std::sort(t.begin(), t.end());
				Log("TC src=%s fill=%s level=%s soff=%u n=%u doff=%u rounds=%d med_us=%.2f bad=%u guard=%u pattern=%u", kind < 2 ? "dyn" : "cached",
				    kind == 0 ? "half" : kind == 1 ? "full" : "-", kTailReadNames[level], c.soff, c.n, c.doff, c.rounds, t[t.size() / 2], bad, guard, pat);
				++nCases; allBad += bad; allGuard += guard; allPat += pat;
			}
			imm->Flush();
		}
	}
	Log("TR cases=%u bad=%u guard=%u pattern=%u", nCases, allBad, allGuard, allPat);
	SafeRelease(b); SafeRelease(imm); SafeRelease(dev);
	return allBad || allGuard || allPat ? 1 : 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	QueryPerformanceFrequency(&g_freq);
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i];
		const wchar_t* v = i + 1 < argc ? argv[i + 1] : L"";
		auto pick = [&](const wchar_t* const* names, int n) { for (int k = 0; k < n; ++k) if (!_wcsicmp(v, names[k])) return k; return -1; };
		if (!wcscmp(a, L"--dll")) { wcsncpy_s(O.dll, v, _TRUNCATE); ++i; }
		else if (!wcscmp(a, L"--log")) { wcsncpy_s(O.log, v, _TRUNCATE); ++i; }
		else if (!wcscmp(a, L"--section")) { wcsncpy_s(O.section, v, _TRUNCATE); ++i; }
		else if (!wcscmp(a, L"--dump")) { wcsncpy_s(O.dump, v, _TRUNCATE); ++i; }
		else if (!wcscmp(a, L"--frames")) { O.frames = _wtoi(v); ++i; }
		else if (!wcscmp(a, L"--hold-ms")) { O.holdMs = _wtoi(v); ++i; }
		else if (!wcscmp(a, L"--stale-until")) { O.staleUntil = _wtoi(v); ++i; }
		else if (!wcscmp(a, L"--fake-exe")) O.fakeExe = true;
		else if (!wcscmp(a, L"--wait-hooks")) { if (!_wcsicmp(v, L"present")) O.waitPresentOnly = true; else if (_wcsicmp(v, L"all")) return 2; ++i; }
		else if (!wcscmp(a, L"--server")) { O.server = pick(kSrvNames, SRV_COUNT); ++i; if (O.server < 0) return 2; }
		else if (!wcscmp(a, L"--ring")) { O.ring = pick(kRingNames, RING_COUNT); ++i; if (O.ring < 0) return 2; }
		else if (!wcscmp(a, L"--fake-bad")) { O.bad = pick(kBadNames, BAD_COUNT); ++i; if (O.bad < 0) return 2; }
		else if (!wcscmp(a, L"--models")) O.models = true;
		else if (!wcscmp(a, L"--mset")) { O.mset = pick(kMsetNames, MSET_COUNT); ++i; if (O.mset < 0) return 2; }
		else if (!wcscmp(a, L"--want")) { O.want = (uint32_t)wcstoul(v, nullptr, 0); ++i; }
		else if (!wcscmp(a, L"--defer-early")) O.deferEarly = true;
		else if (!wcscmp(a, L"--minst")) { O.minst = _wtoi(v); ++i; if (O.minst < 1 || O.minst > 8192) return 2; }
		else if (!wcscmp(a, L"--set-at")) { O.setAt = _wtoi(v); ++i; }
		else if (!wcscmp(a, L"--early")) O.early = true;
		else if (!wcscmp(a, L"--write-at")) { O.writeAt = _wtoi(v); ++i; }
		else if (!wcscmp(a, L"--cpu")) O.cpu = true;
		else if (!wcscmp(a, L"--cpu-set")) { O.cpuSet = pick(kCpuSetNames, CS_COUNT); ++i; if (O.cpuSet < 0) return 2; }
		else if (!wcscmp(a, L"--cpu-stop")) { O.cpuStop = _wtoi(v); ++i; if (O.cpuStop < 0) return 2; }
		else if (!wcscmp(a, L"--cpu-frag")) { O.cpuFrag = true; g_cVbN[0] = 112u; }
		else if (!wcscmp(a, L"--cpu-late")) { O.cpuLate = _wtoi(v); ++i; if (O.cpuLate < 0) return 2; }
		else if (!wcscmp(a, L"--map-mode")) { O.mapMode = pick(kMapModeNames, MM_COUNT); ++i; if (O.mapMode < 0) return 2; }
		else if (!wcscmp(a, L"--map-pad")) { O.mapPad = _wtoi(v); ++i; if (O.mapPad < 0 || O.mapPad > (1 << 22) || (O.mapPad & 15)) return 2; }
		else if (!wcscmp(a, L"--map-def")) O.mapDef = true;
		else if (!wcscmp(a, L"--map-check")) O.mapCheck = true;
		else if (!wcscmp(a, L"--tailread")) { O.tailRead = _wtoi(v); ++i; if (O.tailRead < 1 || O.tailRead > 100000) return 2; }
		else if (!wcscmp(a, L"--tr-adapter")) { O.trAdapter = _wtoi(v); ++i; if (O.trAdapter < 0 || O.trAdapter > 15) return 2; }
		else if (!wcscmp(a, L"--cpu-pad")) { O.cpuPad = _wtoi(v); ++i; if (O.cpuPad < 0 || O.cpuPad > 4096 - 36) return 2; g_cVbN[1] = 60u + (UINT)O.cpuPad; }
		else if (!wcscmp(a, L"--word"))
		{
			WordAt w = {};
			wchar_t* e = nullptr;
			w.frame = (int)wcstol(v, &e, 10);
			if (!e || *e != L':') return 2;
			w.word = (uint32_t)wcstoul(e + 1, &e, 0);
			w.mid = e && !_wcsicmp(e, L":mid");
			O.words.push_back(w);
			++i;
		}
		else return 2;
	}
	if (O.log[0]) _wfopen_s(&g_log, O.log, L"w");
	if (O.tailRead)   // ticket 0758
	{
		const int trc = TailReadBench(O.tailRead);
		Log("exit %d", trc);
		if (g_log) fclose(g_log);
		return trc;
	}
	if (!O.dump[0] && (!O.dll[0] || (O.server != SRV_NONE && !O.section[0]))) { Log("--dll and --section are needed"); return 2; }
	int rc = Run();
	Log("exit %d", rc);
	if (g_log) fclose(g_log);
	return rc;
}
