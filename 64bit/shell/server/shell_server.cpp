// Origin (rule 13, decision D22): the project's fork of Community-Patch-DLL, branch claude/shell-gates-d8e422,
// path experiments/shell-gates/compositor/src/shell_server.cpp, copied at branch head c60138b82 (file last changed in a9d8328e2).
// Copied into shell/server/shell_server.cpp on 2026-11-10 (issue 3001); later changes are in this repository's history.
// shell_server.cpp - x64 producer for the Gate 3 compositor prototype.
//
// Renders an animated scene into a private render target and publishes each
// frame into a shared BGRA8 texture (NT handle, keyed mutex, named) that a
// 32-bit process opens by name. A named shared-memory section (shell_shared.h)
// carries the texture name/size, frame counter and frame QPC, and the client's
// resize requests.
//
// Keyed-mutex protocol (per published frame):
//   server: AcquireSync(0) [or AcquireSync(1) if the client did not take the
//           previous frame: that frame is overwritten] -> CopyResource ->
//           write frame block -> ReleaseSync(1)
//   client: AcquireSync(1, timeout) -> read frame block -> CopyResource ->
//           ReleaseSync(0)
//
// Usage: shell_server.exe [--size WxH] [--rate HZ|0] [--luid HEX] [--section NAME]
//                         [--name BASE] [--seconds N] [--log PATH] [--gpu-sync 1|0]
//                         [--scene test|grid] [grid options] [--dump PATH --dump-after N] [--print-layout]
//   --scene test (default): the Gate 3 test scene (animated hex grid, quad, bar, frame barcode), no camera.
//   --scene grid (issue 3007): a hex outline grid in world coordinates drawn with the EXE's camera from the section's
//       camera block (shell_shared.h VPShellCamera, written by shell_composite.dll), one pass per view i < numViews
//       (map wrap); background transparent black. Colours: view 0 yellow, 1 cyan, 2 green, 3 white; STALE = red.
//     --grid-scale F     hex corner scale (1 = true corners; the EXE's own highlight outline sits at about 0.9)
//     --heightmap PATH   u16 little-endian heightmap dump (shell/tools/gt_reader.py heights) of the same map: z of
//                        every vertex from the terrain; without it, --flat-z Z (default 12) for every vertex
//     --edge-div N       segments per hex edge with a heightmap (default 4)
//     --water-z Z        heightmap z below Z is raised to Z (default 10: the height of the EXE's hex outline over
//                        water, where the heightmap holds 0; issue 3012, measured 2026-11-12 by epic 3200, was 12);
//                        a negative Z turns the clamp off
//     --stale-frames N   the DLL counts Presents in the block; when it is N frames past the last published block the
//                        camera is STALE (default 15)
//     --grid-stale tint|hide  STALE: draw the last good camera in red (default) or draw nothing
//     --bad-frames N     INCONSISTENT / TORN blocks for fewer than N frames keep the last good camera (default 3)
//   --dump PATH --dump-after N: write the server's own frame as a 32-bit BMP after N frames (grid scene: N frames with
//       the grid drawn) and exit (offline accuracy test, shell/tests/test_camera_offline.py).
//       Exit 1 when the dump was requested but not written (issue 3013).
//   --print-layout PATH: write the VPShellCamera field offsets as JSON to PATH and exit (shell/tests/camblock.py checks
//       them); exit 1 when PATH cannot be written.
//   --test-abandon N (tests only, issue 3013): at published frame N, terminate while holding the keyed mutex.
//   --tex-format bgra|rgba (tests only, issue 3018 V5): format of the shared texture, B8G8R8A8_UNORM (default) or
//       R8G8B8A8_UNORM (a consumer must cope with a format change at the same size; --dump assumes bgra).
//   --scene terrain (epic 3200, issue 3205; decisions D58 4, 6, 7): the terrain surface drawn with c5r::terrain (the
//       EXE's own TerrainShader programs, render/) from the tapped height field and the camera block's view[0] / proj.
//       Inputs: the section's tap block (VPShellTap: open the C5HF section by name, check FNV-1a 64, readTap, map size
//       against the tap block and the camera block's grid, copy, close, write ackResult then ackLoadSeq; a new loadSeq
//       reloads) or --tap-file; the bake-tap block (VPShellBakeTap, ACTIVE: open the manifest, open each slot's two
//       shared cache textures by name and pass them with setTileCache; only slots whose generation changed are
//       re-opened). Pacing (D58 6): a frame is drawn and published only for a new camera block (frameStamp changed),
//       a new field or a cache change, so the server never draws faster than the game presents; --rate HZ (> 0, given
//       explicitly) adds an upper cap. A STALE camera (presentCount - frameStamp > --stale-frames) or a camera that
//       is not OK keeps the last published frame (counted). Draw errors are logged and counted, never fatal.
//     --exe PATH         CivilizationV_DX11.exe to read the TerrainShader programs from (read-only; default C5R_EXE)
//     --tap-file PATH    a C5HF file (hfbake / terrainshot format) instead of the tap block (offline, stand-in)
//     --overlay-grid 1   the grid scene's hex outlines (--grid-scale, --heightmap, --water-z, --edge-div) drawn over the
//                        terrain with the terrain's camera (pan-lag measurement against the EXE's highlight, 3207)
//     --save-tap PATH    issue 3206 (definition of done 4): every tap payload the server acknowledged with ackResult 0
//                        is also written, as read from the section, to PATH_l<loadSeq>.c5hf (after the ack; a failed
//                        write is logged and counted as a tap error, never fatal)
//     --tile-plots N     plots per tile 1..3 (default: the bake-tap block's tilePlotsX when ACTIVE, else 2)
//     --tile-d4 N        Options::tileD4 (cache uv; 0 = the module's default)
//     --color caches|material|constant|height|normal   colour source (default caches)
//     --fallback material|constant|height|normal       colour of tiles without a cache (default material; for the
//                        colour judgement on cached tiles only, MC 2026-11-11 13:36: constant with a colour the EXE
//                        never draws, e.g. --constant-rgba 0xFFFF00FF = magenta, which terrain_compare --fallback-rgb
//                        removes from the colour mask)
//     --constant-rgba HEX the Constant colour, 0xAABBGGRR (R in the low byte; default 0xFF808080)
//     --pass N --tess F  TerrainShader pass (-1 = the EXE's at the owner's settings) and tess factor (0 = the EXE's)
//     --water 1|0        the flat water placeholder at the field's waterZ (D58 7; default 1)
//     --depth 1|0        publish the server's depth as a second shared texture (VPShellDepth, R32_FLOAT, NDC z;
//                        with --dump also written raw to DUMP.depth.f32)
//     Issue 3407 (epic 3400 scope 5): the server bakes and keeps the tiles' caches itself (the library
//     render/include/c5r/bake/residency.h, the calls of epic 3300's TileCacheSource seam):
//     --bake tap|pipeline|standin  where tile caches come from: tap (default: the EXE's caches through the bake-tap
//                        block), pipeline (c5r::bake::PipelineBaker, issue 3404: the EXE's shaders from --exe and the
//                        install from --game-dir or C5R_GAME_DIR) or standin (c5r::bake::StandinBaker, one flat colour per
//                        tile). Both need tile plots 1 (the EXE's tile = one 64-sample plot); the bake tap is then not read
//     --game-dir DIR     the install root for --bake pipeline (default C5R_GAME_DIR); read-only
//     --cache-arrays 1|0 t0 / t8 in BC1 texture arrays (default 1) or one texture per cache
//     --bake-memory-mb F the arrays' video memory cap (0 = none; beyond it: out of memory, the radius drops)
//     --bake-radius N    BakedTileCameraRadius -1..128 for this run (default: VP64Options.ini through options/)
//     --options-file P   the options file (default: VP64_OPTIONS, else Documents\My Games\...\VP64Options.ini);
//                        never created by the server; a missing or bad file gives the defaults (logged)
//     --bake-rate F      background bakes per second (default 400); --bake-burst N visible tiles baked before a draw
//                        (default 16); --bake-pump-ms F CPU cap of one bake call (default 4)
//     --bake-catch-up 0|1  issue 0499: ResidencyOptions::catchUpVisible (default 1 since 0409: a frame with more visible
//                        land tiles without a bake than the burst bakes all of them before the draw); 0 = the burst alone,
//                        for tests that must see gray frames
//     --tilegen NAME     the tile content-generation block (server/tilegen_block.h) to poll for re-bakes
//     --bake-trace N     log the first N bakes (tile, ring, visible): the order, for tests
//     --bake-gpu-ms-per-s F  at most F ms of the baker's GPU time (BakedTile::gpuMs) per second for the background bake
//                        (0 = off, default; the visible tiles before a draw are not limited by it)
//     --standin-fail X,Y / --standin-vram-mb M / --standin-ms F   stand-in faults (tests only, c5r/bake/standin.h)
//   --scene bakefake (tests only, issue 3205): a stand-in producer of the bake-tap block: creates the manifest section
//       and one solid-colour shared texture pair per slot (--fake-tiles WxH tiles, --tile-plots N, colours alternate
//       red / blue by tile parity), writes VPShellBakeTap ACTIVE and waits for --seconds. Negative variants:
//       --fake-missing SLOT (that slot's textures are not created), --fake-manifest-version N.
//   Exit codes: 0 done, 1 runtime failure (adapter, device, section, texture, dump), 2 bad command line (unknown
//   option, missing / malformed / out-of-range value; issue 3013), 3 --test-abandon, 4 terrain renderer init failed
//   (EXE missing or without its shaders), 5 --tap-file unreadable or invalid (the c5r check is logged).
//   --gpu-sync 1 (default): wait for the GPU to finish each frame before ReleaseSync(1),
//   so the client never waits on the GPU for our work; frameQpc is then GPU-complete time.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <timeapi.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <share.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include "shell_shared.h"
#include "exe_camera.h"
#include <algorithm>
#include <string>
#include <vector>
#include "c5r/terrain/terrain.h"
#include "c5r/heightfield/heightfield.h"
#include <memory>
#include "c5r/bake/residency.h"
#include "c5r/bake/standin.h"
#include "c5r/bake/pipeline.h"
#include "c5r/bake/layers.h"
#include "c5r/gamefs/gamefiles.h"
#include "c5r/shaders/shadersection.h"
#include "tilegen_block.h"
#include "vp64_options.h"
#include "VSFullscreen.h"
#include "PSServer.h"
#include "VSGrid.h"
#include "PSGrid.h"
#include "../../server/src/srv_power.h"   // bug 0118: opt out of power throttling (header only)

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static FILE* g_log = nullptr;
static LARGE_INTEGER g_freq;
static volatile LONG g_quit = 0;

static void Log(const char* fmt, ...)
{
	char buf[1024];
	SYSTEMTIME st; GetLocalTime(&st);
	int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap; va_start(ap, fmt);
	_vsnprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, fmt, ap);
	va_end(ap);
	fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
	if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

static int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double QpcToMs(int64_t d) { return (double)d * 1000.0 / (double)g_freq.QuadPart; }

static BOOL WINAPI CtrlHandler(DWORD) { InterlockedExchange(&g_quit, 1); return TRUE; }

struct ServerCB
{
	float time, pad0;
	float sizeX, sizeY;
	uint32_t frameLo, frameHi;
	float pad1[2];
};

struct Server
{
	IDXGIAdapter1* adapter = nullptr;
	ID3D11Device* dev = nullptr;
	ID3D11Device1* dev1 = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	ID3D11Buffer* cb = nullptr;
	ID3D11Query* inflight[3] = {};
	ID3D11Query* syncQuery = nullptr;
	DXGI_FORMAT texFormat = DXGI_FORMAT_B8G8R8A8_UNORM;   // --tex-format (issue 3018 V5 test: rgba = R8G8B8A8_UNORM)

	// per generation
	ID3D11Texture2D* privTex = nullptr;
	ID3D11RenderTargetView* privRtv = nullptr;
	ID3D11Texture2D* sharedTex = nullptr;
	IDXGIKeyedMutex* km = nullptr;
	HANDLE sharedHandle = nullptr;
	UINT width = 0, height = 0;
	uint32_t generation = 0;
	wchar_t texName[128] = {};

	HANDLE section = nullptr;
	VPShellHeader* hdr = nullptr;
	uint64_t luid = 0;
	// issue 3206: why the last cache texture could not be used (logged with the missing slot)
	const char* lastCacheErr = "";
	HRESULT lastCacheHr = 0;
};

static void ReleaseTextures(Server& s)
{
	SafeRelease(s.privRtv);
	SafeRelease(s.privTex);
	SafeRelease(s.km);
	SafeRelease(s.sharedTex);
	if (s.sharedHandle) { CloseHandle(s.sharedHandle); s.sharedHandle = nullptr; }
}

static void PublishTextureBlock(Server& s)
{
	VPShellHeader* h = s.hdr;
	InterlockedIncrement((volatile LONG*)&h->texSeq); // odd
	MemoryBarrier();
	h->serverPid = GetCurrentProcessId();
	h->generation = s.generation;
	h->width = s.width;
	h->height = s.height;
	h->dxgiFormat = s.texFormat;
	h->adapterLuid = s.luid;
	wcsncpy_s(h->textureName, s.texName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&h->texSeq); // even
}

static bool CreateTextures(Server& s, UINT w, UINT h, const wchar_t* baseName)
{
	ReleaseTextures(s);
	s.width = w; s.height = h;
	s.generation++;

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = s.texFormat;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	HRESULT hr = s.dev->CreateTexture2D(&td, nullptr, &s.privTex);
	if (FAILED(hr)) { Log("CreateTexture2D(private) failed 0x%08X", hr); return false; }
	hr = s.dev->CreateRenderTargetView(s.privTex, nullptr, &s.privRtv);
	if (FAILED(hr)) { Log("CreateRenderTargetView failed 0x%08X", hr); return false; }

	td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
	hr = s.dev->CreateTexture2D(&td, nullptr, &s.sharedTex);
	if (FAILED(hr)) { Log("CreateTexture2D(shared) failed 0x%08X", hr); return false; }
	hr = s.sharedTex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&s.km);
	if (FAILED(hr)) { Log("QI IDXGIKeyedMutex failed 0x%08X", hr); return false; }

	IDXGIResource1* res1 = nullptr;
	hr = s.sharedTex->QueryInterface(__uuidof(IDXGIResource1), (void**)&res1);
	if (FAILED(hr)) { Log("QI IDXGIResource1 failed 0x%08X", hr); return false; }
	_snwprintf_s(s.texName, _countof(s.texName), _TRUNCATE, L"%s_%u_%u", baseName, GetCurrentProcessId(), s.generation);
	hr = res1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, s.texName, &s.sharedHandle);
	SafeRelease(res1);
	if (FAILED(hr)) { Log("CreateSharedHandle('%ls') failed 0x%08X", s.texName, hr); return false; }

	// The texture starts with key 0 free. Fill it once so a client never sees garbage.
	Log("texture generation %u: %ux%u %s shared as '%ls'", s.generation, w, h, s.texFormat == DXGI_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : "RGBA8", s.texName);
	PublishTextureBlock(s);
	return true;
}

static bool PickAdapter(Server& s, uint64_t wantLuid)
{
	IDXGIFactory1* f = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return false;
	IDXGIAdapter1* best = nullptr; SIZE_T bestVram = 0;
	for (UINT i = 0;; ++i)
	{
		IDXGIAdapter1* a = nullptr;
		if (f->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
		DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d);
		uint64_t l = ((uint64_t)(uint32_t)d.AdapterLuid.HighPart << 32) | d.AdapterLuid.LowPart;
		Log("adapter %u: '%ls' LUID %016llX VRAM %llu MB%s", i, d.Description, l,
			(unsigned long long)(d.DedicatedVideoMemory >> 20), (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (software)" : "");
		bool pick = false;
		if (wantLuid) pick = (l == wantLuid);
		else pick = !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > bestVram;
		if (pick) { SafeRelease(best); best = a; bestVram = d.DedicatedVideoMemory; s.luid = l; }
		else a->Release();
	}
	f->Release();
	if (!best) { Log("no matching adapter"); return false; }
	DXGI_ADAPTER_DESC1 d; best->GetDesc1(&d);
	Log("using adapter '%ls' LUID %016llX", d.Description, s.luid);
	s.adapter = best;
	return true;
}

static void LogMemory(Server& s, const char* tag)
{
	PROCESS_MEMORY_COUNTERS_EX pmc = { sizeof(pmc) };
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	unsigned long long vid = 0;
	IDXGIAdapter3* a3 = nullptr;
	if (SUCCEEDED(s.adapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)))
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO vi = {};
		if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi))) vid = vi.CurrentUsage;
		a3->Release();
	}
	Log("mem %s: private=%.2f MB working=%.2f MB video_local=%.2f MB", tag,
		pmc.PrivateUsage / 1048576.0, pmc.WorkingSetSize / 1048576.0, vid / 1048576.0);
}

// ===========================================================================
// Grid scene (issue 3007): a hex outline grid in world coordinates, drawn with the EXE's camera that
// shell_composite.dll publishes in the section's camera block (shell_shared.h VPShellCamera).
// ===========================================================================
enum { GS_NO_CAMERA, GS_OK, GS_STALE, GS_HIDDEN, GS_SRV_INCONSISTENT, GS_NO_GRID, GS_COUNT };
static const char* kGsNames[GS_COUNT] = { "NO_CAMERA", "OK", "STALE", "HIDDEN", "SRV_INCONSISTENT", "NO_GRID" };

struct GridOpts
{
	double scale = 1.0;            // corner scale: 1 = the true hex corners (0.9 = the EXE's highlight outline)
	double flatZ = 12.0;           // z of every vertex without a heightmap (world units; a typical land sample is ~12)
	int edgeDiv = 4;               // segments per hex edge (heights follow the terrain along the edge)
	double waterZ = 10.0;          // heightmap z below this is raised to it (issue 3012: the EXE draws its hex
	                               // outline over water at z 10.0 (fit of epic 3200's live shots; was 12, which put
	                               // the grid 5 px above it), where the heightmap holds 0); <0 = off
	int staleFrames = 15;          // presentCount - frameStamp above this = the publisher stopped (STALE)
	int badFrames = 3;             // INCONSISTENT / TORN for fewer frames than this: keep drawing the last good camera
	bool staleTint = true;         // STALE: draw the last good camera in red (true) or draw nothing (false)
	wchar_t heightmap[MAX_PATH] = L"";
};

struct GridCB { float vp[16]; float color[4]; };

struct GridState
{
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	ID3D11InputLayout* il = nullptr;
	ID3D11Buffer* cb = nullptr;
	ID3D11Buffer* vb = nullptr;
	UINT nVerts = 0;
	uint32_t builtSeq = 0xFFFFFFFFu;
	uint16_t* hm = nullptr; size_t hmCount = 0;
	VPShellCamera good = {};
	bool haveGood = false;
	uint32_t lastSeq = 0;
	int64_t badSince = -1;
	int state = -1;
	uint32_t lastStatus = 0xFFFFFFFFu;
	// per second
	int drawn = 0, drawnStale = 0, blocks = 0, srvIncons = 0, snapFail = 0, transient = 0;
	int64_t lagMax = 0;
	int64_t srvInconsTotal = 0;
	int64_t drawnTotal = 0;
};

static bool SnapCamera(VPShellHeader* hdr, VPShellCamera& out)
{
	VPShellCamera* c = VPSHELL_CAMERA(hdr);
	for (int i = 0; i < 16; ++i)
	{
		uint32_t s1 = c->seq;
		if (s1 == 0) return false;
		if (s1 & 1) { YieldProcessor(); continue; }
		MemoryBarrier();
		memcpy(&out, (const void*)c, sizeof(out));
		MemoryBarrier();
		if (c->seq == s1) { out.seq = s1; return true; }
	}
	return false;
}

static void MulD(const float* a, const float* b, float* out) { VPCamMul(a, b, out); }

static bool LoadHeightmap(GridState& g, const wchar_t* path)
{
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"rb") || !f) { Log("grid: cannot open heightmap '%ls' - flat z", path); return false; }
	_fseeki64(f, 0, SEEK_END);
	int64_t n = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	g.hmCount = (size_t)(n / 2);
	g.hm = (uint16_t*)malloc(g.hmCount * 2);
	size_t got = g.hm ? fread(g.hm, 2, g.hmCount, f) : 0;
	fclose(f);
	if (got != g.hmCount) { Log("grid: short read of heightmap '%ls' - flat z", path); free(g.hm); g.hm = nullptr; g.hmCount = 0; return false; }
	Log("grid: heightmap '%ls' loaded: %zu samples", path, g.hmCount);
	return true;
}

// HeightAtGrid 0x6D8B60 on a dumped heightmap: integer sample coordinates (truncated), x wrapped, outside -> 0;
// z = sample / 255 * heightScale (research camera-and-picking.md section 9).
static double HeightAt(const GridState& g, const VPShellCamera& c, bool useHm, double flatZ, double waterZ, double x, double y)
{
	if (!useHm) return flatZ;
	int64_t X = c.samplesX, Y = c.samplesY;
	int64_t ix = (int64_t)((x + c.ox) / c.sx), iy = (int64_t)((y + c.oy) / c.sy);
	if (c.wrapX == 1) ix = ((ix % X) + X) % X;
	double z = (ix < 0 || iy < 0 || ix >= X || iy >= Y) ? 0.0 : (double)g.hm[iy * X + ix] * (1.0 / 255.0) * c.heightScale;
	return (waterZ >= 0.0 && z < waterZ) ? waterZ : z;   // issue 3012: water plots at the EXE's outline height
}

static void HexToWorldXY(const VPShellCamera& c, double hx, double hy, double& x, double& y)
{
	double wx = hx * c.h2w[0] + hy * c.h2w[4] + c.h2w[12];
	double wy = hx * c.h2w[1] + hy * c.h2w[5] + c.h2w[13];
	x = wx * c.sx - c.ox;
	y = wy * c.sy - c.oy;
}

static const int kMaxGridW = 256, kMaxGridH = 160;

// Map plots from the heightmap's sample counts: W = samplesX / 64, H = samplesY / 64 - 4 (four plot rows of margin,
// research section 9 [I from two maps]).
static bool BuildGrid(Server& s, GridState& g, const GridOpts& o, const VPShellCamera& c)
{
	SafeRelease(g.vb); g.nVerts = 0;
	int W = (int)(c.samplesX / 64), H = (int)(c.samplesY / 64) - 4;
	// Issue 3018 V6 (2026-11-13): at most 256 x 160 plots, twice the game's largest map (Huge, 128 x 80) in each
	// direction, room for map scripts of mods; was 1024 x 1024 (up to ~50 M line vertices, ~600 MB per grid change).
	if (W <= 0 || H <= 0 || W > kMaxGridW || H > kMaxGridH) { Log("grid: implausible map size %d x %d (samples %u x %u) - not drawn", W, H, c.samplesX, c.samplesY); return false; }
	bool useHm = g.hm && g.hmCount == (size_t)c.samplesX * c.samplesY;
	if (g.hm && !useHm) Log("grid: heightmap has %zu samples, the map needs %u x %u - flat z %.2f", g.hmCount, c.samplesX, c.samplesY, o.flatZ);
	int div = useHm ? std::max(1, o.edgeDiv) : 1;
	size_t nv = (size_t)W * H * 6 * div * 2;
	float* v = (float*)malloc(nv * 3 * sizeof(float));
	if (!v) return false;
	static const int nb[6][2] = { {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1} };
	size_t k = 0;
	for (int gy = 0; gy < H; ++gy)
		for (int gx = 0; gx < W; ++gx)
		{
			double hx = gx - (gy >> 1), hy = gy, cx, cy, nx[6], ny[6], px[6], py[6];
			HexToWorldXY(c, hx, hy, cx, cy);
			for (int i = 0; i < 6; ++i) HexToWorldXY(c, hx + nb[i][0], hy + nb[i][1], nx[i], ny[i]);
			for (int i = 0; i < 6; ++i)
			{
				int j = (i + 1) % 6;
				px[i] = cx + o.scale * ((cx + nx[i] + nx[j]) / 3.0 - cx);
				py[i] = cy + o.scale * ((cy + ny[i] + ny[j]) / 3.0 - cy);
			}
			for (int i = 0; i < 6; ++i)
			{
				int j = (i + 1) % 6;
				for (int d = 0; d < div; ++d)
				{
					double t0 = (double)d / div, t1 = (double)(d + 1) / div;
					double x0 = px[i] + (px[j] - px[i]) * t0, y0 = py[i] + (py[j] - py[i]) * t0;
					double x1 = px[i] + (px[j] - px[i]) * t1, y1 = py[i] + (py[j] - py[i]) * t1;
					v[k++] = (float)x0; v[k++] = (float)y0; v[k++] = (float)HeightAt(g, c, useHm, o.flatZ, o.waterZ, x0, y0);
					v[k++] = (float)x1; v[k++] = (float)y1; v[k++] = (float)HeightAt(g, c, useHm, o.flatZ, o.waterZ, x1, y1);
				}
			}
		}
	D3D11_BUFFER_DESC bd = { (UINT)(nv * 3 * sizeof(float)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
	D3D11_SUBRESOURCE_DATA sd = { v, 0, 0 };
	HRESULT hr = s.dev->CreateBuffer(&bd, &sd, &g.vb);
	free(v);
	if (FAILED(hr)) { Log("grid: vertex buffer (%zu vertices) failed 0x%08X", nv, hr); return false; }
	g.nVerts = (UINT)nv;
	g.builtSeq = c.gridSeq;
	Log("grid: built for grid seq %u: map %d x %d plots, %u line vertices, corner scale %.3f, %s, wrap %d period %.1f",
		c.gridSeq, W, H, g.nVerts, o.scale, useHm ? "z from the heightmap" : "flat z", c.wrapX, c.wrapPeriod);
	return true;
}

static bool GridInit(Server& s, GridState& g, const GridOpts& o)
{
	if (FAILED(s.dev->CreateVertexShader(g_VSGrid, sizeof(g_VSGrid), nullptr, &g.vs)) ||
		FAILED(s.dev->CreatePixelShader(g_PSGrid, sizeof(g_PSGrid), nullptr, &g.ps)))
	{ Log("grid: shader creation failed"); return false; }
	D3D11_INPUT_ELEMENT_DESC ie = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	if (FAILED(s.dev->CreateInputLayout(&ie, 1, g_VSGrid, sizeof(g_VSGrid), &g.il))) { Log("grid: input layout failed"); return false; }
	D3D11_BUFFER_DESC bd = { sizeof(GridCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	if (FAILED(s.dev->CreateBuffer(&bd, nullptr, &g.cb))) { Log("grid: constant buffer failed"); return false; }
	if (o.heightmap[0]) LoadHeightmap(g, o.heightmap);
	Log("grid scene: corner scale %.3f, flat z %.2f, edge segments %d, stale after %d frames (%s), bad camera tolerated for %d frames",
		o.scale, o.flatZ, o.edgeDiv, o.staleFrames, o.staleTint ? "tint red" : "hide", o.badFrames);
	return true;
}

static bool ServerConsistent(const VPShellCamera& c, float& rot, float& trans, float& wrapErr)
{
	VPCamConsistency(c.camWorld, c.view[0], rot, trans);
	wrapErr = 0;
	for (uint32_t i = 1; i < c.numViews && i < 4; ++i) wrapErr = std::max(wrapErr, VPCamWrapError(c.view[0], c.view[i], c.wrapPeriod));
	bool finite = true;
	for (int k = 0; k < 16; ++k) if (!(fabsf(c.proj[k]) < 1e30f)) finite = false;
	// issue 3015: the server's own check of every matrix it draws with (a NaN view used to pass the DLL's and this check)
	uint32_t nv = c.numViews >= 1 && c.numViews <= 4 ? c.numViews : 1;
	if (!VPCamAllFinite(c.camWorld, 16) || !VPCamAllFinite(&c.view[0][0], 16 * (int)nv)) finite = false;
	return finite && c.numViews >= 1 && c.numViews <= 4 && rot <= VPCAM_TOL_ROT && trans <= VPCAM_TOL_TRANS && wrapErr <= VPCAM_TOL_WRAP;
}

// Decide what to draw this server frame and draw it into the bound target. Returns the number of views drawn.
static int GridDrawCam(Server& s, GridState& g, const GridOpts& o, const VPShellCamera& use, bool tint);

static int GridFrame(Server& s, GridState& g, const GridOpts& o)
{
	VPShellCamera c;
	bool got = SnapCamera(s.hdr, c);
	int64_t pc = InterlockedCompareExchange64(&VPSHELL_CAMERA(s.hdr)->presentCount, 0, 0);
	int st = GS_NO_CAMERA;
	bool tint = false;
	const VPShellCamera* use = nullptr;
	float rot = 0, trans = 0, wrapErr = 0;
	uint32_t status = 0;
	if (!got)
	{
		if (VPSHELL_CAMERA(s.hdr)->seq != 0) g.snapFail++;
		st = g.haveGood && g.state == GS_OK ? GS_OK : GS_NO_CAMERA;   // a torn snapshot: keep the last state for a frame
		if (st == GS_OK) use = &g.good;
	}
	else
	{
		if (c.seq != g.lastSeq) { g.blocks++; g.lastSeq = c.seq; }
		status = c.status;
		if (status == VPCAM_OK && !ServerConsistent(c, rot, trans, wrapErr))
		{
			g.srvIncons++; g.srvInconsTotal++;
			status = VPCAM_INCONSISTENT;
			if (g.state != GS_SRV_INCONSISTENT)
				Log("grid: camera block says OK but fails the server's own check: rot %.2e trans %.3f wrap %.3f views %u (frame %lld)",
					rot, trans, wrapErr, c.numViews, c.frameStamp);
			st = GS_SRV_INCONSISTENT;
		}
		int64_t lag = pc - c.frameStamp;
		if (lag > g.lagMax) g.lagMax = lag;
		if (lag > o.staleFrames)
		{
			st = GS_STALE;
			if (o.staleTint && g.haveGood) { use = &g.good; tint = true; }
		}
		else if (st == GS_SRV_INCONSISTENT) { /* hidden */ }
		else if (status == VPCAM_OK)
		{
			g.good = c; g.haveGood = true; g.badSince = -1;
			st = GS_OK; use = &g.good;
		}
		else if ((status == VPCAM_INCONSISTENT || status == VPCAM_TORN) && g.haveGood)
		{
			if (g.badSince < 0) g.badSince = c.frameStamp;
			if (c.frameStamp - g.badSince < o.badFrames) { st = GS_OK; use = &g.good; g.transient++; }
			else st = GS_HIDDEN;
		}
		else st = GS_HIDDEN;
	}
	if (use && (!use->gridValid || !use->samplesX)) { st = GS_NO_GRID; use = nullptr; }
	if (st != g.state || (got && status != g.lastStatus && st == GS_HIDDEN))
	{
		Log("grid: state %s -> %s (camera status %s, present %lld, block frame %lld, seq %u, views %u, view mode %u)",
			g.state < 0 ? "-" : kGsNames[g.state], kGsNames[st], got ? VPCamStatusName(c.status) : "-", pc, got ? c.frameStamp : -1,
			got ? c.seq : 0, got ? c.numViews : 0, got ? c.viewMode : 0);
		g.state = st;
	}
	if (got) g.lastStatus = status;
	if (!use) return 0;
	return GridDrawCam(s, g, o, *use, tint);
}

// The hex outlines of the camera `use` into the bound target (grid scene; and over the terrain with --overlay-grid,
// issue 3206/3207: the pan-lag measurement of camera-and-picking.md 10 needs a server-drawn outline beside the EXE's
// highlight, drawn with the camera the terrain was drawn with).
static int GridDrawCam(Server& s, GridState& g, const GridOpts& o, const VPShellCamera& useRef, bool tint)
{
	const VPShellCamera* use = &useRef;
	if (use->gridSeq != g.builtSeq && !BuildGrid(s, g, o, *use)) { g.builtSeq = use->gridSeq; return 0; }
	if (!g.vb) return 0;

	float bw = (float)(use->bbW ? use->bbW : (use->vpW ? use->vpW : s.width));
	float bh = (float)(use->bbH ? use->bbH : (use->vpH ? use->vpH : s.height));
	float kx = (float)s.width / bw, ky = (float)s.height / bh;
	D3D11_VIEWPORT vp = { 0, 0, (float)s.width, (float)s.height, 0, 1 };
	if (use->vpW && use->vpH) vp = { use->vpX * kx, use->vpY * ky, use->vpW * kx, use->vpH * ky, 0, 1 };
	s.ctx->RSSetViewports(1, &vp);
	UINT stride = 12, off = 0;
	s.ctx->IASetInputLayout(g.il);
	s.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	s.ctx->IASetVertexBuffers(0, 1, &g.vb, &stride, &off);
	s.ctx->VSSetShader(g.vs, nullptr, 0);
	s.ctx->VSSetConstantBuffers(0, 1, &g.cb);
	s.ctx->PSSetShader(g.ps, nullptr, 0);
	s.ctx->PSSetConstantBuffers(0, 1, &g.cb);
	static const float kColors[4][4] = { {1, 1, 0, 1}, {0, 1, 1, 1}, {0, 1, 0, 1}, {1, 1, 1, 1} };
	uint32_t n = std::min<uint32_t>(use->numViews, 4);
	for (uint32_t i = 0; i < n; ++i)
	{
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(s.ctx->Map(g.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) continue;
		GridCB* cb = (GridCB*)m.pData;
		MulD(use->view[i], use->proj, cb->vp);
		const float* col = tint ? kColors[0] : kColors[i];
		cb->color[0] = col[0]; cb->color[1] = tint ? 0.0f : col[1]; cb->color[2] = tint ? 0.0f : col[2]; cb->color[3] = 1.0f;
		s.ctx->Unmap(g.cb, 0);
		s.ctx->Draw(g.nVerts, 0);
	}
	s.ctx->IASetInputLayout(nullptr);
	s.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	if (tint) g.drawnStale++; else g.drawn++;
	g.drawnTotal++;
	return (int)n;
}

static void GridSecond(GridState& g)
{
	Log("grid 1s: state=%s drawn=%d drawn_stale=%d blocks=%d transient=%d srv_incons=%d snap_fail=%d lag_max=%lld views=%u status=%s grid_seq=%u",
		g.state < 0 ? "-" : kGsNames[g.state], g.drawn, g.drawnStale, g.blocks, g.transient, g.srvIncons, g.snapFail, g.lagMax,
		g.haveGood ? g.good.numViews : 0, g.lastStatus == 0xFFFFFFFFu ? "-" : VPCamStatusName(g.lastStatus), g.builtSeq == 0xFFFFFFFFu ? 0 : g.builtSeq);
	g.drawn = g.drawnStale = g.blocks = g.transient = g.srvIncons = g.snapFail = 0;
	g.lagMax = 0;
}

// ===========================================================================
// Terrain scene (epic 3200, issue 3205): the terrain surface drawn by c5r::terrain with the camera block's camera, from
// the tapped height field (tap block or --tap-file) and the tapped tile caches (bake-tap block).
// ===========================================================================
namespace c5t = c5r::terrain;
namespace c5h = c5r::heightfield;

enum { TS_NO_CAMERA, TS_OK, TS_STALE, TS_HIDDEN, TS_SRV_INCONSISTENT, TS_NO_FIELD, TS_COUNT };
static const char* kTsNames[TS_COUNT] = { "NO_CAMERA", "OK", "STALE", "HIDDEN", "SRV_INCONSISTENT", "NO_FIELD" };
static const char* kTapStateNames[] = { "NONE", "WRITING", "READY", "RELEASED", "FAILED", "OFF" };
static const char* kBakeStateNames[] = { "NONE", "ACTIVE", "OFF", "FAILED" };
// ackResult values the server writes (shell_shared.h VPShellTap: 0 ok, > 0 c5r::Err, -1 hash, -2 open); -3 is this
// server's addition: the tap belongs to another load than the camera block's grid (gridSeq != loadSeq), not used.
enum { TAPACK_OK = 0, TAPACK_HASH = -1, TAPACK_OPEN = -2, TAPACK_STALE = -3 };

struct TerrainOpts
{
	wchar_t exe[MAX_PATH] = L"";
	wchar_t tapFile[MAX_PATH] = L"";
	wchar_t saveTap[MAX_PATH] = L"";   // issue 3206: every acknowledged tap's payload to <saveTap>_l<loadSeq>.c5hf
	int tilePlots = 0;             // 0 = the bake-tap block's when ACTIVE, else c5t::kDefaultTilePlots
	int tileD4 = 0;
	c5t::ColorSource color = c5t::ColorSource::Caches;
	c5t::ColorSource fallback = c5t::ColorSource::Material;   // tiles without a cache (MC 2026-11-11 13:26)
	uint32_t constantRgba = 0xFF808080u;                        // colour of Constant (and of a Constant fallback)
	int pass = -1;
	double tess = 0;
	bool water = true;
	bool depth = false;
	double rateCap = 0;             // > 0: at most this many draws per second (--rate given explicitly)
	// bakefake (tests only)
	int fakeTilesX = 0, fakeTilesY = 0, fakeMissing = -1, fakeManifestVersion = 1;
	bool fakeTypeless = false;      // --fake-typeless 1: R8G8B8A8_TYPELESS textures, as the live bake tap's BC1_TYPELESS
	bool overlayGrid = false;       // --overlay-grid 1: the grid scene's hex outlines over the terrain, same camera
	// issue 3407: residency (--bake pipeline|standin)
	bool residency = false;
	bool pipeline = false;          // --bake pipeline (else the stand-in)
	int bakeRadius = -2;            // -2 = from the options file
	wchar_t optionsFile[MAX_PATH] = L"";
	wchar_t gameDir[MAX_PATH] = L"";
	c5r::bake::ResidencyOptions res;
	double pumpMs = 4.0;            // the CPU budget of one beforeDraw / idle call
	wchar_t tilegen[128] = L"";
	c5r::bake::StandinFaults faults;
};

struct BakeSlotState
{
	ID3D11Texture2D* tex[2] = {};
	ID3D11ShaderResourceView* srv[2] = {};
	uint32_t generation = 0;
	bool opened = false, applied = false;
	uint32_t tileX = 0, tileY = 0;
};

struct TerrainState
{
	c5t::Renderer r;
	c5h::HeightGrid grid;           // kept: a change of tile size (bake tap) rebuilds the tiles from it
	bool haveField = false;
	uint32_t fieldTilePlots = 0;
	uint32_t fieldLoadSeq = 0;      // tap loadSeq of the field (0 = --tap-file)
	// camera
	VPShellCamera cam = {};
	bool haveCam = false;
	uint32_t lastCamSeq = 0;
	int64_t lastDrawnStamp = -1;
	int64_t lastPc = -1;
	bool dirty = false;             // field or caches changed: draw again with the last good camera
	int state = -1;
	uint32_t lastStatus = 0xFFFFFFFFu;
	int64_t lastDrawQpc = 0;
	// tap
	bool tapAckedAny = false;
	uint32_t tapAcked = 0;
	uint32_t tapAckedCopy = 0;
	bool tapRequested = false;      // step 4: a new copy was requested for (tapRequestLoad, tapRequestCopy)
	uint32_t tapRequestLoad = 0, tapRequestCopy = 0;
	int64_t tapRequests = 0;
	int32_t tapLastResult = 0;
	uint32_t tapLoggedState = 0xFFFFFFFFu;
	// bake tap
	HANDLE manSec = nullptr;
	const uint8_t* manView = nullptr;
	size_t manSize = 0;
	wchar_t manName[64] = {};
	uint32_t manExePid = 0;
	uint32_t appliedManifestSeq = 0;
	bool manApplied = false;
	bool manRefusedLogged = false;
	uint32_t bakeLoggedState = 0xFFFFFFFFu;
	std::vector<BakeSlotState> slots;
	// depth (VPShellDepth)
	ID3D11Texture2D* depthTex = nullptr;     // R32_TYPELESS, DSV D32_FLOAT
	ID3D11DepthStencilView* dsv = nullptr;
	ID3D11Texture2D* depthShared = nullptr;  // R32_FLOAT, shared by NT handle
	HANDLE depthHandle = nullptr;
	uint32_t depthGen = 0, depthW = 0, depthH = 0;
	wchar_t depthName[128] = {};
	// counters (total; the *Sec ones reset every second)
	int64_t drawnTotal = 0, drawErrTotal = 0, staleTotal = 0, hiddenTotal = 0, camBlocksTotal = 0;
	int64_t tapLoads = 0, tapErrors = 0, fieldErrors = 0;
	int64_t cacheOpened = 0, cacheMissing = 0, cacheRefused = 0, manifestErrors = 0, manifestsApplied = 0;
	int drawnSec = 0, drawErrSec = 0, staleSec = 0, hiddenSec = 0, blocksSec = 0, cappedSec = 0;
	double gpuSumSec = 0, gpuMaxSec = 0, cpuSumSec = 0; int gpuNSec = 0;
	c5t::FrameStats lastStats;
	std::string lastDrawError;
	// issue 3407: residency over a TileBaker (the library c5r::bake::Residency; pipeline or stand-in baker)
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	c5r::gamefs::GameFiles fs;               // --bake pipeline: the install (outlives the baker)
	c5r::shaders::ShaderSection shaderSec;   // --bake pipeline: the EXE's .shader section
	c5r::bake::LayerTables tables;
	std::unique_ptr<c5r::bake::TileBaker> baker;
	c5r::bake::StandinBaker* standin = nullptr;   // the baker when it is the stand-in (its live bytes in the log)
	c5r::bake::Residency res;
	TileGenReader tilegen;
	int64_t resLastBakes = 0;
	double budgetMs = 4.0;
	bool pipeline = false;
};

static void ResLog(const char* line);
static void ResLogCtx(void*, const char* line) { ResLog(line); }

template <class T> static bool SnapBlock(const T* src, T& out)
{
	const volatile uint32_t* seq = (const volatile uint32_t*)src;
	for (int i = 0; i < 16; ++i)
	{
		uint32_t s1 = *seq;
		if (s1 == 0) return false;
		if (s1 & 1) { YieldProcessor(); continue; }
		MemoryBarrier();
		memcpy(&out, (const void*)src, sizeof(T));
		MemoryBarrier();
		if (*seq == s1) { *(uint32_t*)&out = s1; return true; }
	}
	return false;
}

static uint64_t Fnv1a64(const uint8_t* p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
	return h;
}

// Maps a whole named section read-only; size = the view's region size. False when it cannot be opened or mapped.
static bool MapNamed(const wchar_t* name, HANDLE& h, const uint8_t*& view, size_t& size)
{
	h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
	if (!h) return false;
	view = (const uint8_t*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
	if (!view) { CloseHandle(h); h = nullptr; return false; }
	MEMORY_BASIC_INFORMATION mbi = {};
	size = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
	return true;
}

static void UnmapNamed(HANDLE& h, const uint8_t*& view)
{
	if (view) { UnmapViewOfFile(view); view = nullptr; }
	if (h) { CloseHandle(h); h = nullptr; }
}

static void ReleaseSlots(TerrainState& t)
{
	for (auto& sl : t.slots)
		for (int k = 0; k < 2; ++k) { SafeRelease(sl.srv[k]); SafeRelease(sl.tex[k]); }
	t.slots.clear();
}

static uint32_t WantTilePlots(const TerrainState& t, const TerrainOpts& o, const VPShellHeader* hdr)
{
	if (o.tilePlots) return (uint32_t)o.tilePlots;
	if (o.residency) return 1;   // issue 3407: the bake's tile is the EXE's, one 64-sample plot
	VPShellBakeTap bt;
	if (hdr && SnapBlock(VPSHELL_BAKETAP(hdr), bt) && bt.state == VPBAKE_ACTIVE && bt.tilePlotsX == bt.tilePlotsY &&
		bt.tilePlotsX >= 1 && bt.tilePlotsX <= c5t::kMaxTilePlots)
		return bt.tilePlotsX;
	(void)t;
	return c5t::kDefaultTilePlots;
}

// Builds the renderer's tiles from t.grid. Errors are logged and counted (the field is then not drawn).
static bool ApplyField(TerrainState& t, uint32_t tilePlots)
{
	int64_t q0 = Qpc();
	c5r::Status st = t.r.setHeightField(t.grid, tilePlots);
	if (!st.ok())
	{
		t.fieldErrors++;
		t.haveField = false;
		Log("terrain: setHeightField(%ux%u plots, tile plots %u) failed: %s", t.grid.mapW, t.grid.mapH, tilePlots, st.str().c_str());
		return false;
	}
	c5t::TileGrid tg = t.r.tileGrid();
	t.haveField = true;
	t.fieldTilePlots = tilePlots;
	t.dirty = true;
	for (auto& sl : t.slots) sl.applied = false;   // setHeightField dropped every cache
	t.manApplied = false;
	Log("terrain: field %ux%u plots (%ux%u samples, wrap %d, water z %.3f) as %ux%u tiles of %u plots in %.1f ms",
		t.grid.mapW, t.grid.mapH, t.grid.samplesX, t.grid.samplesY, (int)t.grid.wrapX, t.grid.waterZ, tg.tilesX, tg.tilesY,
		tilePlots, QpcToMs(Qpc() - q0));
	if (t.baker)
	{
		// issue 3407: the residency compares the new field with the last (same size: only changed tiles get a new
		// generation), then sets the caches it still holds again (setHeightField dropped them in the renderer)
		c5r::bake::MapInputs mi;
		mi.tables = t.pipeline ? &t.tables : nullptr;
		mi.wrapX = t.grid.wrapX;
		c5r::Status rs = t.res.onField(t.dev, t.grid, tilePlots, t.fieldLoadSeq, &mi);
		if (!rs.ok()) Log("terrain: residency refused the field: %s", rs.str().c_str());
	}
	return true;
}

static int TerrainLoadTapFile(TerrainState& t, const TerrainOpts& o, const VPShellHeader* hdr)
{
	FILE* f = nullptr;
	if (_wfopen_s(&f, o.tapFile, L"rb") || !f) { Log("terrain: cannot open --tap-file '%ls'", o.tapFile); return 5; }
	_fseeki64(f, 0, SEEK_END);
	int64_t n = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	std::vector<uint8_t> b(n > 0 ? (size_t)n : 0);
	size_t got = b.empty() ? 0 : fread(b.data(), 1, b.size(), f);
	fclose(f);
	if (got != b.size()) { Log("terrain: short read of --tap-file '%ls'", o.tapFile); return 5; }
	c5r::Status st = c5h::readTap(b.data(), b.size(), t.grid);
	if (!st.ok()) { Log("terrain: --tap-file '%ls' refused: %s", o.tapFile, st.str().c_str()); return 5; }
	t.fieldLoadSeq = 0;
	t.tapLoads++;
	Log("terrain: --tap-file '%ls': %lld bytes, C5HF %ux%u plots", o.tapFile, n, t.grid.mapW, t.grid.mapH);
	if (!ApplyField(t, WantTilePlots(t, o, hdr))) return 5;
	return 0;
}

static void WriteTapAck(VPShellHeader* hdr, uint32_t loadSeq, uint32_t copySeq, int32_t result)
{
	VPShellTap* tap = VPSHELL_TAP(hdr);
	tap->ackServerPid = GetCurrentProcessId();
	InterlockedExchange((volatile LONG*)&tap->ackResult, result);
	// issue 3206 [L 2026-11-11]: the DLL accepts an ack only when ackCopySeq == copySeq (shell_shared.h, step 3); without
	// it the live DLL waited 60 s and closed the section with FAILED ACK_TIMEOUT although the server had read the field
	InterlockedExchange((volatile LONG*)&tap->ackCopySeq, (LONG)copySeq);
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&tap->ackLoadSeq, (LONG)loadSeq);   // written last
}

// The tap block: a READY copy of a load we have not acknowledged yet is opened, checked, copied, closed and
// acknowledged (ackResult first, then ackLoadSeq), then its tiles are built.
static void TapPoll(Server& s, TerrainState& t, const TerrainOpts& o)
{
	if (o.tapFile[0]) return;
	VPShellTap tb;
	if (!SnapBlock(VPSHELL_TAP(s.hdr), tb)) return;
	if (tb.state != t.tapLoggedState)
	{
		Log("terrain: tap block state %s (load seq %u, fail reason %u, %llu payload bytes, section '%.64ls')",
			tb.state < 6 ? kTapStateNames[tb.state] : "?", tb.loadSeq, tb.failReason, (unsigned long long)tb.payloadBytes,
			tb.sectionName);
		t.tapLoggedState = tb.state;
	}
	// Step 4 of the protocol (issue 3206): a server that has no field of the current load while the DLL's copy is gone
	// (RELEASED to an earlier server, or FAILED after an ack timeout) asks once per copy for a new one (requestSeq).
	if ((tb.state == VPTAP_RELEASED || (tb.state == VPTAP_FAILED && tb.failReason == VPTAPF_ACK_TIMEOUT)) &&
		!(t.haveField && t.fieldLoadSeq == tb.loadSeq) && !(t.tapRequested && t.tapRequestLoad == tb.loadSeq && t.tapRequestCopy == tb.copySeq))
	{
		VPShellCamera c2;
		if (SnapCamera(s.hdr, c2) && c2.gridValid == 1 && c2.gridSeq == tb.loadSeq)
		{
			uint32_t r = (uint32_t)InterlockedIncrement((volatile LONG*)&VPSHELL_TAP(s.hdr)->requestSeq);
			t.tapRequested = true; t.tapRequestLoad = tb.loadSeq; t.tapRequestCopy = tb.copySeq; t.tapRequests++;
			Log("terrain: tap of load seq %u is %s (copy %u) and this server has no field of it: new copy requested (requestSeq %u)",
				tb.loadSeq, tb.state == VPTAP_RELEASED ? "RELEASED" : "FAILED ACK_TIMEOUT", tb.copySeq, r);
		}
	}
	if (tb.state != VPTAP_READY) return;
	if (t.tapAckedAny && tb.loadSeq == t.tapAcked && tb.copySeq == t.tapAckedCopy) return;
	int64_t q0 = Qpc();
	int32_t result = TAPACK_OK;
	std::string why;
	tb.sectionName[63] = 0;
	VPShellCamera cam;
	bool haveCam = SnapCamera(s.hdr, cam) && cam.gridValid == 1;
	if (haveCam && cam.gridSeq != tb.loadSeq)
	{
		result = TAPACK_STALE;
		why = "stale: the camera block's grid seq is " + std::to_string(cam.gridSeq) + ", the tap's load seq " + std::to_string(tb.loadSeq);
	}
	HANDLE h = nullptr;
	const uint8_t* view = nullptr;
	size_t size = 0;
	c5h::HeightGrid g;
	if (result == TAPACK_OK && !MapNamed(tb.sectionName, h, view, size))
	{
		result = TAPACK_OPEN;
		DWORD le = GetLastError();
		char nm[160] = {};
		WideCharToMultiByte(CP_UTF8, 0, tb.sectionName, -1, nm, (int)sizeof(nm) - 1, nullptr, nullptr);
		why = std::string("cannot open section '") + nm + "' (error " + std::to_string(le) + ")";
	}
	if (result == TAPACK_OK && (tb.payloadBytes > size || tb.payloadBytes < c5h::kTapHeaderSize))
	{
		result = (int32_t)c5r::Err::Truncated;
		why = "payload " + std::to_string(tb.payloadBytes) + " bytes, section view " + std::to_string(size) + " bytes";
	}
	if (result == TAPACK_OK && tb.fnv1a64)
	{
		uint64_t hsh = Fnv1a64(view, (size_t)tb.payloadBytes);
		if (hsh != tb.fnv1a64)
		{
			result = TAPACK_HASH;
			char b[96]; _snprintf_s(b, sizeof(b), _TRUNCATE, "FNV-1a 64 %016llX, the block says %016llX", hsh, tb.fnv1a64);
			why = b;
		}
	}
	if (result == TAPACK_OK)
	{
		c5r::Status st = c5h::readTap(view, (size_t)tb.payloadBytes, g);
		if (!st.ok()) { result = (int32_t)st.code; why = st.str(); }
	}
	std::vector<uint8_t> saved;   // --save-tap: the payload as read, written after the ack (the ack's timing unchanged)
	if (result == TAPACK_OK && o.saveTap[0]) saved.assign(view, view + (size_t)tb.payloadBytes);
	UnmapNamed(h, view);
	if (result == TAPACK_OK && (g.mapW != tb.mapW || g.mapH != tb.mapH || g.samplesX != tb.samplesX || g.samplesY != tb.samplesY))
	{
		result = (int32_t)c5r::Err::BadLayout;
		why = "map size: C5HF " + std::to_string(g.mapW) + "x" + std::to_string(g.mapH) + " plots, the tap block " +
			std::to_string(tb.mapW) + "x" + std::to_string(tb.mapH) + " (samples " + std::to_string(tb.samplesX) + "x" + std::to_string(tb.samplesY) + ")";
	}
	if (result == TAPACK_OK && haveCam && (cam.samplesX != g.samplesX || cam.samplesY != g.samplesY))
	{
		result = (int32_t)c5r::Err::BadLayout;
		why = "map size: C5HF " + std::to_string(g.samplesX) + "x" + std::to_string(g.samplesY) + " samples, the camera block's grid " +
			std::to_string(cam.samplesX) + "x" + std::to_string(cam.samplesY);
	}
	WriteTapAck(s.hdr, tb.loadSeq, tb.copySeq, result);   // the DLL may close its section now
	t.tapAckedAny = true;
	t.tapAcked = tb.loadSeq;
	t.tapAckedCopy = tb.copySeq;
	t.tapLastResult = result;
	if (result != TAPACK_OK)
	{
		t.tapErrors++;
		Log("terrain: tap load seq %u refused (ackResult %d): %s", tb.loadSeq, result, why.c_str());
		return;
	}
	t.tapLoads++;
	if (!saved.empty())
	{
		wchar_t path[MAX_PATH + 32];
		_snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls_l%u.c5hf", o.saveTap, tb.loadSeq);
		FILE* f = nullptr;
		bool okw = !_wfopen_s(&f, path, L"wb") && f && fwrite(saved.data(), 1, saved.size(), f) == saved.size();
		if (f && fclose(f)) okw = false;
		Log("terrain: --save-tap '%ls': %s (%llu bytes)", path, okw ? "written" : "NOT written", (unsigned long long)saved.size());
		if (!okw) t.tapErrors++;
	}
	Log("terrain: tap load seq %u read: %ux%u plots, %llu bytes%s, copied and acknowledged in %.1f ms", tb.loadSeq, g.mapW, g.mapH,
		(unsigned long long)tb.payloadBytes, tb.fnv1a64 ? ", hash ok" : ", no hash", QpcToMs(Qpc() - q0));
	t.grid = std::move(g);
	t.fieldLoadSeq = tb.loadSeq;
	ApplyField(t, WantTilePlots(t, o, s.hdr));
}

// The typed view format of a typeless texture format (the formats the EXE's caches and the stand-in use); a typed format
// is returned unchanged; UNKNOWN for a typeless format without a mapping here.
static DXGI_FORMAT TypedViewFormat(DXGI_FORMAT f)
{
	switch (f)
	{
	case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
	case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
	case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_UNORM;
	case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_UNKNOWN;
	default: return f;
	}
}

static bool OpenCacheTexture(Server& s, const wchar_t* name, ID3D11Texture2D*& tex, ID3D11ShaderResourceView*& srv)
{
	SafeRelease(srv); SafeRelease(tex);
	if (!s.dev1) return false;
	HRESULT hr = s.dev1->OpenSharedResourceByName(name, DXGI_SHARED_RESOURCE_READ, __uuidof(ID3D11Texture2D), (void**)&tex);
	if (FAILED(hr)) { tex = nullptr; s.lastCacheErr = "OpenSharedResourceByName"; s.lastCacheHr = hr; return false; }
	// Issue 3206 [L 2026-11-11]: the bake tap's copies are created in the EXE cache's own format, BC1_TYPELESS (70);
	// a view with a null description fails on a typeless texture (every live slot was "missing"). The view takes the
	// typed format of the EXE's own view record (BC1_UNORM, terrain-rendering-inventory.md 5.2).
	D3D11_TEXTURE2D_DESC td;
	tex->GetDesc(&td);
	DXGI_FORMAT vf = TypedViewFormat(td.Format);
	if (vf == DXGI_FORMAT_UNKNOWN) { SafeRelease(tex); s.lastCacheErr = "typeless format without a typed view"; s.lastCacheHr = (HRESULT)td.Format; return false; }
	D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
	vd.Format = vf;
	vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	vd.Texture2D.MostDetailedMip = 0;
	vd.Texture2D.MipLevels = (UINT)-1;
	hr = s.dev->CreateShaderResourceView(tex, &vd, &srv);
	if (FAILED(hr)) { SafeRelease(tex); s.lastCacheErr = "CreateShaderResourceView"; s.lastCacheHr = hr; return false; }
	return true;
}

// The bake-tap block: when ACTIVE and its manifestSeq moved, (re)read the manifest; slots whose generation changed are
// re-opened by name and handed to the renderer.
static void BakePoll(Server& s, TerrainState& t, const TerrainOpts& o)
{
	if (o.residency) return;   // issue 3407: the server's own bakes, the EXE's caches are not read
	VPShellBakeTap bt;
	if (!SnapBlock(VPSHELL_BAKETAP(s.hdr), bt)) return;
	if (bt.state != t.bakeLoggedState)
	{
		Log("terrain: bake-tap block state %s (load seq %u, %u slots, tiles %ux%u of %ux%u plots, manifest '%.64ls')",
			bt.state < 4 ? kBakeStateNames[bt.state] : "?", bt.loadSeq, bt.slotCount, bt.tilesX, bt.tilesY, bt.tilePlotsX,
			bt.tilePlotsY, bt.manifestName);
		t.bakeLoggedState = bt.state;
	}
	if (bt.state != VPBAKE_ACTIVE || !t.haveField) return;
	uint32_t mseq = VPSHELL_BAKETAP(s.hdr)->manifestSeq;
	if (t.manApplied && mseq == t.appliedManifestSeq) return;
	auto refuse = [&](const std::string& why) {
		t.manifestErrors++;
		if (!t.manRefusedLogged) Log("terrain: bake manifest (seq %u) refused: %s", mseq, why.c_str());
		t.manRefusedLogged = true;
		t.manApplied = true;          // not retried before the next manifestSeq
		t.appliedManifestSeq = mseq;
	};
	if (bt.tilePlotsX != bt.tilePlotsY || bt.tilePlotsX < 1 || bt.tilePlotsX > c5t::kMaxTilePlots)
		return refuse("tile plots " + std::to_string(bt.tilePlotsX) + "x" + std::to_string(bt.tilePlotsY) + " outside 1..3 or not square");
	if (bt.tilePlotsX != t.fieldTilePlots)
	{
		if (o.tilePlots) return refuse("tile plots " + std::to_string(bt.tilePlotsX) + " against --tile-plots " + std::to_string(o.tilePlots));
		Log("terrain: the bake tap's tiles are %u plots, the field's %u: rebuilding the field's tiles", bt.tilePlotsX, t.fieldTilePlots);
		if (!ApplyField(t, bt.tilePlotsX)) return;
	}
	bt.manifestName[63] = 0;
	if (!t.manView || wcscmp(t.manName, bt.manifestName) || t.manExePid != bt.exePid)
	{
		UnmapNamed(t.manSec, t.manView);
		ReleaseSlots(t);
		wcsncpy_s(t.manName, bt.manifestName, _TRUNCATE);
		t.manExePid = bt.exePid;
		if (!MapNamed(t.manName, t.manSec, t.manView, t.manSize))
			return refuse("cannot open the manifest section (error " + std::to_string(GetLastError()) + ")");
	}
	VPShellBakeManifest mh;
	if (t.manSize < sizeof(mh)) return refuse("manifest section of " + std::to_string(t.manSize) + " bytes");
	memcpy(&mh, t.manView, sizeof(mh));
	if (mh.magic != VPSHELL_BAKE_MAGIC) return refuse("manifest magic " + std::to_string(mh.magic));
	if (mh.version != 1) return refuse("manifest version " + std::to_string(mh.version) + " (want 1)");
	if (mh.slotBytes != sizeof(VPShellBakeSlot)) return refuse("slot size " + std::to_string(mh.slotBytes));
	if (mh.slotCount > VPSHELL_BAKE_MAX_SLOTS || mh.slotCount != bt.slotCount ||
		sizeof(mh) + (uint64_t)mh.slotCount * sizeof(VPShellBakeSlot) > t.manSize)
		return refuse("slot count " + std::to_string(mh.slotCount) + " (block " + std::to_string(bt.slotCount) + ", section " + std::to_string(t.manSize) + " bytes)");
	t.manRefusedLogged = false;
	if (t.slots.size() != mh.slotCount) { ReleaseSlots(t); t.slots.resize(mh.slotCount); }
	c5t::TileGrid tg = t.r.tileGrid();
	int opened = 0, missing = 0, cleared = 0;
	for (uint32_t i = 0; i < mh.slotCount; ++i)
	{
		const VPShellBakeSlot* src = (const VPShellBakeSlot*)(t.manView + sizeof(mh) + (size_t)i * sizeof(VPShellBakeSlot));
		VPShellBakeSlot sl;
		if (!SnapBlock(src, sl)) continue;           // never written or being written: next manifestSeq
		BakeSlotState& me = t.slots[i];
		if (sl.state == 0)
		{
			if (me.opened) { t.r.setTileCache(me.tileX, me.tileY, c5t::TileCache()); cleared++; }
			for (int k = 0; k < 2; ++k) { SafeRelease(me.srv[k]); SafeRelease(me.tex[k]); }
			me = BakeSlotState();
			continue;
		}
		if (sl.tileX >= tg.tilesX || sl.tileY >= tg.tilesY)
		{
			t.cacheRefused++;
			Log("terrain: bake slot %u: tile (%u, %u) outside the field's %ux%u tiles", i, sl.tileX, sl.tileY, tg.tilesX, tg.tilesY);
			continue;
		}
		bool changed = !me.opened || sl.generation != me.generation;
		if (changed)
		{
			if (me.opened && (me.tileX != sl.tileX || me.tileY != sl.tileY))
				t.r.setTileCache(me.tileX, me.tileY, c5t::TileCache());
			me.opened = false;
			bool ok = true;
			int want = sl.state == 1 ? 2 : 1;
			for (int k = 0; k < 2; ++k)
			{
				if (k >= want) { SafeRelease(me.srv[k]); SafeRelease(me.tex[k]); continue; }
				wchar_t name[96];
				_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_BAKE_TEXTURE_FMT, bt.exePid, i, (unsigned)k);
				if (!OpenCacheTexture(s, name, me.tex[k], me.srv[k]))
				{
					ok = false;   // counted once per slot
					t.cacheMissing++; missing++;
					if (t.cacheMissing <= 8) Log("terrain: bake slot %u: cache texture '%ls' missing (%s failed, 0x%08X): tile (%u, %u) uses the fallback", i, name, s.lastCacheErr, (unsigned)s.lastCacheHr, sl.tileX, sl.tileY);
					break;
				}
			}
			if (!ok) { for (int k = 0; k < 2; ++k) { SafeRelease(me.srv[k]); SafeRelease(me.tex[k]); } me.applied = false; continue; }
			me.opened = true;
			me.generation = sl.generation;
			me.tileX = sl.tileX; me.tileY = sl.tileY;
			me.applied = false;
			t.cacheOpened++; opened++;
		}
		if (me.opened && !me.applied)
		{
			c5t::TileCache tc;
			tc.color = me.srv[0]; tc.second = me.srv[1]; tc.generation = me.generation;
			c5r::Status st = t.r.setTileCache(me.tileX, me.tileY, tc);
			if (!st.ok()) { t.cacheRefused++; Log("terrain: setTileCache(%u, %u) failed: %s", me.tileX, me.tileY, st.str().c_str()); }
			else me.applied = true;
		}
	}
	t.manApplied = true;
	t.appliedManifestSeq = mseq;
	t.manifestsApplied++;
	t.dirty = true;
	VPShellBakeTap* w = VPSHELL_BAKETAP(s.hdr);
	w->ackServerPid = GetCurrentProcessId();
	MemoryBarrier();
	InterlockedExchange((volatile LONG*)&w->ackManifestSeq, (LONG)mseq);
	Log("terrain: bake manifest seq %u applied: %u slots, %d opened, %d missing, %d cleared", mseq, mh.slotCount, opened, missing, cleared);
}

static void ReleaseDepth(TerrainState& t)
{
	SafeRelease(t.dsv); SafeRelease(t.depthTex); SafeRelease(t.depthShared);
	if (t.depthHandle) { CloseHandle(t.depthHandle); t.depthHandle = nullptr; }
}

static void PublishDepthBlock(VPShellHeader* hdr, const TerrainState& t, int64_t frameCounter, uint32_t state)
{
	VPShellDepth* d = VPSHELL_DEPTH(hdr);
	InterlockedIncrement((volatile LONG*)&d->seq);   // odd
	MemoryBarrier();
	d->state = state;
	d->generation = t.depthGen;
	d->width = t.depthW; d->height = t.depthH;
	d->dxgiFormat = DXGI_FORMAT_R32_FLOAT;
	d->encoding = VPDEPTH_NDC_Z;
	d->frameCounter = frameCounter;
	wcsncpy_s(d->textureName, t.depthName, _TRUNCATE);
	// issue 9039: the camera this frame was drawn with (shell_shared.h documents these fields; until 9039 the server left
	// them 0). proj is D3DX row-major: [2][2] = proj[10], [3][2] = proj[14].
	d->camFrameStamp = t.lastDrawnStamp;
	d->proj22 = t.cam.proj[10]; d->proj32 = t.cam.proj[14];
	d->nearZ = t.cam.nearZ; d->farZ = t.cam.farZ;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&d->seq);   // even
}

// The depth buffer the terrain is drawn with (always ours when --depth) and its shared copy, at the target's size.
static bool EnsureDepth(Server& s, TerrainState& t, const wchar_t* baseName)
{
	if (t.depthTex && t.depthW == s.width && t.depthH == s.height) return true;
	ReleaseDepth(t);
	t.depthW = s.width; t.depthH = s.height; t.depthGen++;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = s.width; td.Height = s.height; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.Format = DXGI_FORMAT_R32_TYPELESS;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	HRESULT hr = s.dev->CreateTexture2D(&td, nullptr, &t.depthTex);
	if (FAILED(hr)) { Log("terrain: depth texture failed 0x%08X", hr); return false; }
	D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
	dd.Format = DXGI_FORMAT_D32_FLOAT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	hr = s.dev->CreateDepthStencilView(t.depthTex, &dd, &t.dsv);
	if (FAILED(hr)) { Log("terrain: depth view failed 0x%08X", hr); return false; }
	td.Format = DXGI_FORMAT_R32_FLOAT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	hr = s.dev->CreateTexture2D(&td, nullptr, &t.depthShared);
	if (FAILED(hr)) { Log("terrain: shared depth texture failed 0x%08X", hr); return false; }
	IDXGIResource1* res1 = nullptr;
	hr = t.depthShared->QueryInterface(__uuidof(IDXGIResource1), (void**)&res1);
	if (FAILED(hr)) { Log("terrain: QI IDXGIResource1 (depth) failed 0x%08X", hr); return false; }
	_snwprintf_s(t.depthName, _countof(t.depthName), _TRUNCATE, L"%sDepth_%u_%u", baseName, GetCurrentProcessId(), t.depthGen);
	hr = res1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, t.depthName, &t.depthHandle);
	SafeRelease(res1);
	if (FAILED(hr)) { Log("terrain: CreateSharedHandle('%ls') failed 0x%08X", t.depthName, hr); return false; }
	Log("terrain: depth generation %u: %ux%u R32_FLOAT (NDC z) shared as '%ls'", t.depthGen, t.depthW, t.depthH, t.depthName);
	PublishDepthBlock(s.hdr, t, 0, 1);
	return true;
}

static void ResLog(const char* line) { Log("%s", line); }
static void OptWarn(void*, const char* line) { Log("options: %s", line); }

// issue 3407: BakedTileCameraRadius from --bake-radius, else from VP64Options.ini through options/ (the proxy creates the
// file; the server only reads it: a missing, unreadable or bad file gives the default, logged, never an exit).
static int ResolveRadius(const TerrainOpts& o)
{
	if (o.bakeRadius >= -1) { Log("residency: BakedTileCameraRadius = %d (--bake-radius)", o.bakeRadius); return o.bakeRadius; }
	wchar_t path[MAX_PATH] = L"";
	const char* src = "--options-file";
	if (o.optionsFile[0]) wcsncpy_s(path, o.optionsFile, _TRUNCATE);
	else
	{
		Vp64Opt::PathSource ps = Vp64Opt::DefaultPath(path, MAX_PATH);
		src = ps == Vp64Opt::kPathEnv ? "VP64_OPTIONS" : ps == Vp64Opt::kPathDocuments ? "Documents" : "none";
		if (ps == Vp64Opt::kPathNone) path[0] = 0;
	}
	Vp64Opt::Options op;
	unsigned warns = 0;
	Vp64Opt::LoadStatus ls = Vp64Opt::kMissing;
	if (path[0]) ls = Vp64Opt::LoadFile(path, op, OptWarn, nullptr, &warns);
	static const char* kLs[] = { "loaded", "missing", "unreadable", "too large" };
	int r = op.BakedTileCameraRadius();
	Log("residency: options file '%ls' (%s): %s, %u warnings; BakedTileCameraRadius = %d (%s)", path, src,
		(unsigned)ls < 4 ? kLs[ls] : "?", warns, r, op.fromFile[Vp64Opt::kBakedTileCameraRadius] ? "from the file" : "default");
	return r;
}

static bool TerrainInit(Server& s, TerrainState& t, const TerrainOpts& o, int& rc)
{
	int64_t q0 = Qpc();
	c5r::Status st = t.r.init(s.dev, o.exe, L"");
	if (!st.ok()) { Log("terrain: renderer init with '%ls' failed: %s", o.exe, st.str().c_str()); rc = 4; return false; }
	if (o.residency)
	{
		c5r::bake::ResidencyOptions ro = o.res;
		ro.radius = ResolveRadius(o);
		t.dev = s.dev;
		t.ctx = s.ctx;
		t.budgetMs = o.pumpMs;
		t.pipeline = o.pipeline;
		c5r::Status bs;
		int64_t b0 = Qpc();
		if (o.pipeline)
		{
			// the install (TerrainDef.csv, layer textures, style cubes, LUT) and the EXE's shader section, read-only (D83)
			bs = t.fs.open(o.gameDir);
			if (bs.ok()) bs = c5r::bake::loadTerrainDef(t.fs, t.tables);
			if (bs.ok()) bs = t.shaderSec.open(o.exe);
			if (bs.ok())
			{
				c5r::bake::PipelineConfig cfg;
				cfg.fs = &t.fs;
				cfg.exe = &t.shaderSec;
				// The residency budgets by gpuMs; Deferred never makes the render thread wait for the GPU (3404 wave 3).
				cfg.gpuTiming = c5r::bake::GpuTiming::Deferred;
				t.baker.reset(new c5r::bake::PipelineBaker(cfg));
			}
		}
		else
		{
			t.standin = new c5r::bake::StandinBaker(o.faults);
			t.baker.reset(t.standin);
		}
		if (bs.ok()) bs = t.baker->init(s.dev, s.ctx);
		if (bs.ok()) bs = t.res.init(t.baker.get(), ro, ResLogCtx, nullptr);
		if (!bs.ok())
		{
			Log("terrain: residency init (%s baker, install '%ls') failed: %s", o.pipeline ? "pipeline" : "stand-in", o.gameDir,
				bs.str().c_str());
			t.baker.reset(); t.standin = nullptr; rc = 4; return false;
		}
		if (o.tilegen[0]) t.tilegen.setName(o.tilegen);
		Log("terrain scene: residency on, baker %s (set up in %.1f ms; stand-in faults: fail tile %d,%d, vram budget %.1f MB, %.2f ms "
			"per bake), radius %d, rate %.0f/s, burst %d, gpu %.0f ms/s, budget %.1f ms per call, size class %u, arrays %d (chunk %u, "
			"cap %.1f MB), tile generation block '%ls'", o.pipeline ? "pipeline" : "stand-in", QpcToMs(Qpc() - b0), o.faults.failX,
			o.faults.failY, o.faults.vramBudgetMb, o.faults.cpuMs, ro.radius, ro.rate, ro.burst, ro.gpuMsPerS, o.pumpMs, ro.sizeClass,
			(int)ro.arrays, ro.arrayChunk, ro.memoryLimitMb, o.tilegen[0] ? o.tilegen : L"-");
	}
	Log("terrain scene: fallback colour source %d, constant 0x%08X", (int)o.fallback, o.constantRgba);
	Log("terrain scene: EXE '%ls' (shaders read in %.1f ms), colour %d, pass %d, tess %.2f, water %d, depth %d, tile plots %d, tile d4 %d, rate cap %.1f",
		o.exe, QpcToMs(Qpc() - q0), (int)o.color, o.pass, o.tess, (int)o.water, (int)o.depth, o.tilePlots, o.tileD4, o.rateCap);
	if (o.tapFile[0]) { rc = TerrainLoadTapFile(t, o, s.hdr); if (rc) return false; }
	return true;
}

// Decides whether this loop iteration draws, and draws into the private target. Returns true when a new frame was drawn
// (publish it), false to keep the last published frame.
static bool TerrainFrame(Server& s, TerrainState& t, const TerrainOpts& o, const GridOpts& go, const wchar_t* baseName)
{
	TapPoll(s, t, o);
	BakePoll(s, t, o);
	VPShellCamera c;
	bool got = SnapCamera(s.hdr, c);
	int64_t pc = InterlockedCompareExchange64(&VPSHELL_CAMERA(s.hdr)->presentCount, 0, 0);
	int st = TS_NO_CAMERA;
	bool newBlock = false;
	if (got)
	{
		if (c.seq != t.lastCamSeq) { t.lastCamSeq = c.seq; t.camBlocksTotal++; t.blocksSec++; newBlock = true; }
		float rot = 0, trans = 0, wrapErr = 0;
		int64_t lag = pc - c.frameStamp;
		if (lag > go.staleFrames) st = TS_STALE;
		else if (c.status != VPCAM_OK) st = TS_HIDDEN;
		else if (!ServerConsistent(c, rot, trans, wrapErr))
		{
			st = TS_SRV_INCONSISTENT;
			if (t.state != TS_SRV_INCONSISTENT)
				Log("terrain: camera block says OK but fails the server's own check: rot %.2e trans %.3f wrap %.3f views %u (frame %lld)",
					rot, trans, wrapErr, c.numViews, c.frameStamp);
		}
		else { st = TS_OK; t.cam = c; t.haveCam = true; }
		t.lastStatus = c.status;
	}
	else if (t.haveCam) st = t.state;   // torn snapshot: keep the state for one iteration
	if (st == TS_OK && !t.haveField) st = TS_NO_FIELD;
	// a field from the tap belongs to one load: once the camera block's grid is another load's (map change), the old
	// field is not drawn until the new load's tap arrives (--tap-file fields, load seq 0, are not checked)
	if (st == TS_OK && t.fieldLoadSeq && t.cam.gridValid == 1 && t.cam.gridSeq != t.fieldLoadSeq) st = TS_NO_FIELD;
	// presents the game showed with a kept (old) frame
	if (t.lastPc >= 0 && pc > t.lastPc && (st == TS_STALE || st == TS_HIDDEN || st == TS_SRV_INCONSISTENT))
	{
		int64_t d = pc - t.lastPc;
		if (st == TS_STALE) { t.staleTotal += d; t.staleSec += (int)d; }
		else { t.hiddenTotal += d; t.hiddenSec += (int)d; }
	}
	t.lastPc = pc;
	if (st != t.state)
	{
		Log("terrain: state %s -> %s (camera status %s, present %lld, block frame %lld, seq %u)", t.state < 0 ? "-" : kTsNames[t.state],
			kTsNames[st], got ? VPCamStatusName(c.status) : "-", pc, got ? c.frameStamp : -1, got ? c.seq : 0);
		t.state = st;
	}
	(void)newBlock;
	if (st != TS_OK) return false;
	bool want = t.cam.frameStamp != t.lastDrawnStamp || t.dirty;
	if (!want) return false;
	int64_t now = Qpc();
	if (o.rateCap > 0 && t.lastDrawQpc && QpcToMs(now - t.lastDrawQpc) < 1000.0 / o.rateCap) { t.cappedSec++; return false; }
	if (o.depth && !EnsureDepth(s, t, baseName)) { t.drawErrTotal++; t.drawErrSec++; return false; }

	c5t::Camera cam;
	memcpy(cam.view, t.cam.view[0], sizeof(cam.view));
	memcpy(cam.proj, t.cam.proj, sizeof(cam.proj));
	memcpy(cam.camWorld, t.cam.camWorld, sizeof(cam.camWorld));
	if (t.baker && t.res.hasField())
	{
		// issue 3407: the frame's visible tiles, rings and eviction; visible tiles without a current bake first (up to
		// --bake-burst before this draw); changed caches handed to the renderer; the frame counted (gray / stale)
		c5r::bake::ResidencyView rv;
		rv.frameId = t.cam.frameStamp;
		memcpy(rv.camWorld, cam.camWorld, sizeof(rv.camWorld));
		memcpy(rv.view, cam.view, sizeof(rv.view));
		memcpy(rv.proj, cam.proj, sizeof(rv.proj));
		rv.width = s.width; rv.height = s.height;
		t.res.beforeDraw(s.ctx, t.r, rv, t.budgetMs);
		t.res.takeVisibleChanged();   // this draw shows them
	}
	cam.width = s.width; cam.height = s.height;   // the target; the projection maps the EXE's back buffer onto it
	c5t::Options op;
	op.pass = o.pass;
	op.tessFactor = (float)o.tess;
	op.tileD4 = (uint32_t)o.tileD4;
	op.color = o.color;
	op.fallback = o.fallback;
	op.constantRgba = o.constantRgba;
	op.water = o.water;
	op.clearRgba = 0;
	op.clearDepth = true;
	const float clear[4] = { 0, 0, 0, 0 };   // transparent outside the terrain
	s.ctx->ClearRenderTargetView(s.privRtv, clear);
	c5t::FrameStats fs;
	c5r::Status ds = t.r.draw(s.ctx, s.privRtv, o.depth ? t.dsv : nullptr, cam, op, &fs);
	t.lastDrawQpc = now;
	t.lastDrawnStamp = t.cam.frameStamp;
	t.dirty = false;
	if (!ds.ok())
	{
		t.drawErrTotal++; t.drawErrSec++;
		std::string e = ds.str();
		if (e != t.lastDrawError || t.drawErrTotal <= 3) Log("terrain: draw failed (frame %lld): %s", t.cam.frameStamp, e.c_str());
		t.lastDrawError = e;
		return false;
	}
	t.lastDrawError.clear();
	t.lastStats = fs;
	t.drawnTotal++; t.drawnSec++;
	t.cpuSumSec += fs.cpuMs;
	if (fs.gpuMs >= 0) { t.gpuSumSec += fs.gpuMs; t.gpuMaxSec = std::max(t.gpuMaxSec, fs.gpuMs); t.gpuNSec++; }
	return true;
}

static void TerrainSecond(Server& s, TerrainState& t)
{
	VPShellTap tb = {}; VPShellBakeTap bt = {};
	bool tapOk = SnapBlock(VPSHELL_TAP(s.hdr), tb), bakeOk = SnapBlock(VPSHELL_BAKETAP(s.hdr), bt);
	Log("terrain 1s: state=%s drawn=%d blocks=%d stale=%d hidden=%d capped=%d draw_err=%d gpu_ms=%.3f gpu_max_ms=%.3f cpu_ms=%.3f "
		"tiles=%u culled=%u nocache=%u | tap=%s load_seq=%u loads=%lld tap_err=%lld ack=%d field_err=%lld | bake=%s slots=%zu "
		"cache_open=%lld cache_missing=%lld cache_refused=%lld manifest_err=%lld manifests=%lld | tot_drawn=%lld tot_draw_err=%lld "
		"tot_stale=%lld tot_hidden=%lld tot_blocks=%lld",
		t.state < 0 ? "-" : kTsNames[t.state], t.drawnSec, t.blocksSec, t.staleSec, t.hiddenSec, t.cappedSec, t.drawErrSec,
		t.gpuNSec ? t.gpuSumSec / t.gpuNSec : -1.0, t.gpuNSec ? t.gpuMaxSec : -1.0, t.drawnSec ? t.cpuSumSec / t.drawnSec : -1.0,
		t.lastStats.tilesDrawn, t.lastStats.tilesCulled, t.lastStats.tilesWithoutCache,
		tapOk && tb.state < 6 ? kTapStateNames[tb.state] : "-", tapOk ? tb.loadSeq : 0, t.tapLoads, t.tapErrors, t.tapLastResult,
		t.fieldErrors, bakeOk && bt.state < 4 ? kBakeStateNames[bt.state] : "-", t.slots.size(), t.cacheOpened, t.cacheMissing,
		t.cacheRefused, t.manifestErrors, t.manifestsApplied, t.drawnTotal, t.drawErrTotal, t.staleTotal, t.hiddenTotal, t.camBlocksTotal);
	t.drawnSec = t.drawErrSec = t.staleSec = t.hiddenSec = t.blocksSec = t.cappedSec = 0;
	t.gpuSumSec = t.gpuMaxSec = t.cpuSumSec = 0; t.gpuNSec = 0;
	if (t.baker)
	{
		char line[1400];
		t.res.statsLine(line, sizeof(line));
		const c5r::bake::ResidencyStats& r = t.res.stats();
		Log("residency 1s: %s bakes_sec=%lld standin_live_mb=%.1f tilegen_snaps=%lld tilegen_raised=%lld tilegen_err=%lld", line,
			(long long)(r.bakes - t.resLastBakes), t.standin ? t.standin->liveBytes() / 1048576.0 : 0.0,
			(long long)t.tilegen.snapshots, (long long)t.tilegen.raised, (long long)t.tilegen.errors);
		t.resLastBakes = r.bakes;
	}
}

// issue 3407: the tile generation block and the background bake (Residency::idle), once per loop iteration after the
// publish (its GPU work never delays a published frame). A visible tile that got its cache asks for a redraw.
static void TerrainPump(TerrainState& t)
{
	if (!t.baker || !t.res.hasField()) return;
	double now = QpcToMs(Qpc());
	t.tilegen.poll(t.res, now, ResLog);
	t.res.idle(t.ctx, t.budgetMs);
	if (t.res.takeVisibleChanged()) t.dirty = true;
}

static void TerrainRelease(TerrainState& t)
{
	t.res.release();
	t.tilegen.close();
	ReleaseSlots(t);
	UnmapNamed(t.manSec, t.manView);
	ReleaseDepth(t);
	t.r.release();
}

// Raw float32 rows of the shared depth copy (offline check of the depth texture, issue 3208's conversion).
static bool DumpDepth(Server& s, TerrainState& t, const wchar_t* path)
{
	if (!t.depthShared) return false;
	D3D11_TEXTURE2D_DESC td; t.depthShared->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	if (FAILED(s.dev->CreateTexture2D(&td, nullptr, &st))) return false;
	s.ctx->CopyResource(st, t.depthShared);   // what a consumer of the shared texture reads
	D3D11_MAPPED_SUBRESOURCE m;
	bool ok = false;
	if (SUCCEEDED(s.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
	{
		FILE* f = nullptr;
		if (!_wfopen_s(&f, path, L"wb") && f)
		{
			ok = true;
			for (UINT y = 0; y < td.Height; ++y) ok = ok && fwrite((const char*)m.pData + (size_t)y * m.RowPitch, 4, td.Width, f) == td.Width;
			ok = (fclose(f) == 0) && ok;
		}
		s.ctx->Unmap(st, 0);
	}
	st->Release();
	Log("depth dump to '%ls' %s", path, ok ? "written" : "FAILED");
	return ok;
}

// ---- --scene bakefake (tests only): a stand-in producer of the bake-tap block and its shared cache textures ----
static int FakeBakeMain(Server& s, const wchar_t* sectionName, const TerrainOpts& o, double seconds)
{
	HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, sectionName);
	if (!sec) { Log("bakefake: section '%ls' failed %u", sectionName, GetLastError()); return 1; }
	VPShellHeader* hdr = (VPShellHeader*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if (!hdr) { CloseHandle(sec); return 1; }
	uint32_t pid = GetCurrentProcessId();
	uint32_t n = (uint32_t)(o.fakeTilesX * o.fakeTilesY);
	size_t bytes = sizeof(VPShellBakeManifest) + (size_t)n * sizeof(VPShellBakeSlot);
	wchar_t manName[64];
	_snwprintf_s(manName, _countof(manName), _TRUNCATE, VPSHELL_BAKE_SECTION_FMT, pid);
	HANDLE man = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)bytes, manName);
	uint8_t* mv = man ? (uint8_t*)MapViewOfFile(man, FILE_MAP_ALL_ACCESS, 0, 0, bytes) : nullptr;
	if (!mv) { Log("bakefake: manifest section failed %u", GetLastError()); return 1; }
	std::vector<ID3D11Texture2D*> texs;
	std::vector<HANDLE> handles;
	VPShellBakeManifest* mh = (VPShellBakeManifest*)mv;
	mh->magic = VPSHELL_BAKE_MAGIC; mh->version = (uint32_t)o.fakeManifestVersion; mh->slotCount = n; mh->slotBytes = sizeof(VPShellBakeSlot);
	uint32_t tp = o.tilePlots ? (uint32_t)o.tilePlots : c5t::kDefaultTilePlots;
	for (uint32_t i = 0; i < n; ++i)
	{
		VPShellBakeSlot* sl = (VPShellBakeSlot*)(mv + sizeof(VPShellBakeManifest) + (size_t)i * sizeof(VPShellBakeSlot));
		uint32_t tx = i % (uint32_t)o.fakeTilesX, ty = i / (uint32_t)o.fakeTilesX;
		bool red = ((tx + ty) & 1) == 0;
		if ((int)i != o.fakeMissing)
		{
			uint32_t px[16 * 16];
			for (auto& p : px) p = red ? 0xFF0000FFu : 0xFFFF0000u;   // R8G8B8A8: red or blue
			for (int k = 0; k < 2; ++k)
			{
				D3D11_TEXTURE2D_DESC td = {};
				td.Width = 16; td.Height = 16; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
				td.Format = o.fakeTypeless ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;   // typeless: as the live tap (3206)
				td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
				D3D11_SUBRESOURCE_DATA sd = { px, 64, 0 };
				ID3D11Texture2D* tex = nullptr;
				HRESULT hr = s.dev->CreateTexture2D(&td, &sd, &tex);
				IDXGIResource1* r1 = nullptr;
				if (SUCCEEDED(hr)) hr = tex->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
				wchar_t name[96];
				_snwprintf_s(name, _countof(name), _TRUNCATE, VPSHELL_BAKE_TEXTURE_FMT, pid, i, (unsigned)k);
				HANDLE h = nullptr;
				if (SUCCEEDED(hr)) hr = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, name, &h);
				SafeRelease(r1);
				if (FAILED(hr)) { Log("bakefake: texture '%ls' failed 0x%08X", name, hr); return 1; }
				texs.push_back(tex); handles.push_back(h);
			}
		}
		sl->seq = 1;
		sl->state = 1; sl->tileIndex = i; sl->tileX = tx; sl->tileY = ty; sl->lod = 0; sl->generation = 1;
		sl->width = 16; sl->height = 16; sl->mips = 1;
		sl->dxgiFormat[0] = sl->dxgiFormat[1] = o.fakeTypeless ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
		sl->copiedQpc = Qpc();
		MemoryBarrier();
		sl->seq = 2;
	}
	s.ctx->Flush();
	VPShellBakeTap* bt = VPSHELL_BAKETAP(hdr);
	InterlockedIncrement((volatile LONG*)&bt->seq);
	MemoryBarrier();
	bt->state = VPBAKE_ACTIVE; bt->failReason = 0; bt->exePid = pid; bt->loadSeq = 1; bt->slotCount = n;
	bt->tilesX = (uint32_t)o.fakeTilesX; bt->tilesY = (uint32_t)o.fakeTilesY; bt->tilePlotsX = bt->tilePlotsY = tp;
	bt->manifestBytes = bytes; bt->copiesTotal = (int64_t)n * 2; bt->copyMsTotal = 0;
	wcsncpy_s(bt->manifestName, manName, _TRUNCATE);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&bt->seq);
	InterlockedIncrement((volatile LONG*)&bt->manifestSeq);
	Log("bakefake: %u slots (%dx%d tiles of %u plots, missing slot %d, manifest version %d) in '%ls'; ACTIVE", n, o.fakeTilesX,
		o.fakeTilesY, tp, o.fakeMissing, o.fakeManifestVersion, manName);
	int64_t start = Qpc();
	while (!g_quit && (seconds <= 0 || QpcToMs(Qpc() - start) < seconds * 1000.0)) Sleep(20);
	Log("bakefake: done (server ack manifest seq %u)", bt->ackManifestSeq);
	for (auto* t : texs) t->Release();
	for (auto h : handles) CloseHandle(h);
	UnmapViewOfFile(mv); CloseHandle(man);
	UnmapViewOfFile(hdr); CloseHandle(sec);
	return 0;
}

static bool DumpTarget(Server& s, const wchar_t* path)
{
	D3D11_TEXTURE2D_DESC td; s.privTex->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	if (FAILED(s.dev->CreateTexture2D(&td, nullptr, &st))) return false;
	s.ctx->CopyResource(st, s.privTex);
	D3D11_MAPPED_SUBRESOURCE m;
	bool ok = false;
	if (SUCCEEDED(s.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
	{
		FILE* f = nullptr;
		if (!_wfopen_s(&f, path, L"wb") && f)
		{
			BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
			ih.biSize = sizeof(ih); ih.biWidth = (LONG)td.Width; ih.biHeight = -(LONG)td.Height; ih.biPlanes = 1; ih.biBitCount = 32;
			ih.biCompression = BI_RGB; ih.biSizeImage = td.Width * td.Height * 4;
			fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + ih.biSizeImage;
			fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
			for (UINT y = 0; y < td.Height; ++y) fwrite((const char*)m.pData + (size_t)y * m.RowPitch, 4, td.Width, f);
			fclose(f);
			ok = true;
		}
		s.ctx->Unmap(st, 0);
	}
	st->Release();
	Log("dump of the server's frame to '%ls' %s", path, ok ? "written" : "FAILED");
	return ok;
}

static bool PrintLayout(const wchar_t* path)
{
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"w") || !f) { fwprintf(stderr, L"--print-layout: cannot write '%s'\n", path); return false; }
#define F(name) fprintf(f, "\"%s\": %u, ", #name, (unsigned)offsetof(VPShellCamera, name))
	fprintf(f, "{");
	F(seq); F(status); F(presentCount); F(frameStamp); F(readQpc); F(exeBase); F(viewMode); F(numViews); F(flags); F(proj); F(view);
	F(camWorld); F(fovDeg); F(aspect); F(nearZ); F(farZ); F(vpX); F(vpY); F(vpW); F(vpH); F(vpMinZ); F(vpMaxZ); F(bbW); F(bbH);
	F(consistRot); F(consistTrans); F(tornRetries); F(readsOk); F(readsBad); F(gridSeq); F(gridValid); F(wrapX); F(wrapPeriod);
	F(aabbMin); F(aabbMax); F(worldHeight); F(w2h); F(h2w); F(sx); F(sy); F(heightScale); F(ox); F(oy); F(samplesX); F(samplesY);
	F(heightmapAddr); F(pad0);
#undef F
	// version 3 blocks (epic 3200): {"name": offset, ..., "sizeof": n, "offset": block offset in the section}
#define G(S, name) fprintf(f, "\"%s\": %u, ", #name, (unsigned)offsetof(S, name))
	fprintf(f, "\"tap\": {");
	G(VPShellTap, seq); G(VPShellTap, state); G(VPShellTap, loadSeq); G(VPShellTap, failReason); G(VPShellTap, mapW); G(VPShellTap, mapH);
	G(VPShellTap, samplesX); G(VPShellTap, samplesY); G(VPShellTap, flags); G(VPShellTap, exePid); G(VPShellTap, payloadBytes);
	G(VPShellTap, sectionBytes); G(VPShellTap, fnv1a64); G(VPShellTap, readyQpc); G(VPShellTap, copyMs); G(VPShellTap, sectionName);
	G(VPShellTap, ackLoadSeq); G(VPShellTap, ackResult); G(VPShellTap, ackServerPid);
	fprintf(f, "\"sizeof\": %u, \"offset\": %u}, ", (unsigned)sizeof(VPShellTap), VPSHELL_TAP_OFFSET);
	fprintf(f, "\"baketap\": {");
	G(VPShellBakeTap, seq); G(VPShellBakeTap, state); G(VPShellBakeTap, failReason); G(VPShellBakeTap, exePid); G(VPShellBakeTap, loadSeq);
	G(VPShellBakeTap, slotCount); G(VPShellBakeTap, tilesX); G(VPShellBakeTap, tilesY); G(VPShellBakeTap, tilePlotsX); G(VPShellBakeTap, tilePlotsY);
	G(VPShellBakeTap, manifestSeq); G(VPShellBakeTap, manifestBytes); G(VPShellBakeTap, copiesTotal); G(VPShellBakeTap, copyMsTotal);
	G(VPShellBakeTap, manifestName); G(VPShellBakeTap, ackManifestSeq); G(VPShellBakeTap, ackServerPid);
	fprintf(f, "\"sizeof\": %u, \"offset\": %u}, ", (unsigned)sizeof(VPShellBakeTap), VPSHELL_BAKETAP_OFFSET);
	fprintf(f, "\"bakeslot\": {");
	G(VPShellBakeSlot, seq); G(VPShellBakeSlot, state); G(VPShellBakeSlot, tileIndex); G(VPShellBakeSlot, tileX); G(VPShellBakeSlot, tileY);
	G(VPShellBakeSlot, lod); G(VPShellBakeSlot, generation); G(VPShellBakeSlot, width); G(VPShellBakeSlot, height); G(VPShellBakeSlot, mips);
	G(VPShellBakeSlot, dxgiFormat); G(VPShellBakeSlot, copiedQpc); G(VPShellBakeSlot, worldMin); G(VPShellBakeSlot, worldMax);
	fprintf(f, "\"sizeof\": %u, \"offset\": %u}, ", (unsigned)sizeof(VPShellBakeSlot), (unsigned)sizeof(VPShellBakeManifest));
	fprintf(f, "\"depth\": {");
	G(VPShellDepth, seq); G(VPShellDepth, state); G(VPShellDepth, generation); G(VPShellDepth, width); G(VPShellDepth, height);
	G(VPShellDepth, dxgiFormat); G(VPShellDepth, encoding); G(VPShellDepth, frameCounter); G(VPShellDepth, textureName);
	fprintf(f, "\"sizeof\": %u, \"offset\": %u}, ", (unsigned)sizeof(VPShellDepth), VPSHELL_DEPTH_OFFSET);
#undef G
	fprintf(f, "\"sizeof\": %u, \"offset\": %u, \"section_bytes\": %u, \"version\": %u}\n", (unsigned)sizeof(VPShellCamera), VPSHELL_CAMERA_OFFSET,
		VPSHELL_SECTION_BYTES, VPSHELL_VERSION);
	return fclose(f) == 0;
}

// Strict number parsing for the command line (issue 3013): the whole value must be a number inside [lo, hi], else the
// server refuses to start (exit 2) instead of running with 0 or a default.
static bool ArgD(const wchar_t* a, const wchar_t* v, double lo, double hi, double& out)
{
	wchar_t* end = nullptr;
	double d = wcstod(v, &end);
	if (!v[0] || !end || *end || !(d >= lo && d <= hi)) { fwprintf(stderr, L"bad value for %s: '%s' (want a number in %g..%g)\n", a, v, lo, hi); return false; }
	out = d; return true;
}
static bool ArgI(const wchar_t* a, const wchar_t* v, int lo, int hi, int& out)
{
	double d;
	if (!ArgD(a, v, lo, hi, d)) return false;
	if (d != floor(d)) { fwprintf(stderr, L"bad value for %s: '%s' (want an integer)\n", a, v); return false; }
	out = (int)d; return true;
}

int wmain(int argc, wchar_t** argv)
{
	UINT width = 1920, height = 1080;
	double rate = 60.0;
	uint64_t wantLuid = 0;
	const wchar_t* sectionName = VPSHELL_SECTION_DEFAULT;
	const wchar_t* baseName = VPSHELL_TEXTURE_BASE_DEFAULT;
	double seconds = 0;
	const wchar_t* logPath = L"shell_server.log";
	bool gpuSync = true;
	bool sceneGrid = false, sceneTerrain = false, sceneBakeFake = false;
	bool rateGiven = false;
	GridOpts go;
	TerrainOpts to;
	const wchar_t* dumpPath = nullptr;
	int dumpAfter = 3;
	int testAbandon = 0;
	DXGI_FORMAT texFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	// Every option takes a value; a missing, malformed or out-of-range value refuses the start with exit 2 (issue 3013:
	// before, `--size abc` ran at 1920x1080 and `--rate x` at full speed without a word).
	for (int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i];
		if (wcsncmp(a, L"--", 2)) { fwprintf(stderr, L"unknown argument %s\n", a); return 2; }
		if (i + 1 >= argc) { fwprintf(stderr, L"%s needs a value\n", a); return 2; }
		const wchar_t* v = argv[++i];
		bool ok = true;
		if (!wcscmp(a, L"--size"))
		{
			unsigned w = 0, h = 0; wchar_t extra = 0;
			ok = swscanf_s(v, L"%ux%u%c", &w, &h, &extra, 1) == 2 && w >= 16 && h >= 16 && w <= 16384 && h <= 16384;
			if (ok) { width = w; height = h; } else fwprintf(stderr, L"bad value for --size: '%s' (want WxH, 16..16384 each)\n", v);
		}
		else if (!wcscmp(a, L"--rate")) { ok = ArgD(a, v, 0, 10000, rate); rateGiven = true; }
		else if (!wcscmp(a, L"--luid"))
		{
			wchar_t* end = nullptr;
			wantLuid = _wcstoui64(v, &end, 16);
			ok = v[0] && end && !*end && wantLuid;
			if (!ok) fwprintf(stderr, L"bad value for --luid: '%s' (want a non-zero hex LUID)\n", v);
		}
		else if (!wcscmp(a, L"--section")) { sectionName = v; ok = v[0] != 0; }
		else if (!wcscmp(a, L"--name")) { baseName = v; ok = v[0] != 0; }
		else if (!wcscmp(a, L"--seconds")) ok = ArgD(a, v, 0, 1e7, seconds);
		else if (!wcscmp(a, L"--log")) { logPath = v; ok = v[0] != 0; }
		else if (!wcscmp(a, L"--gpu-sync")) { int g = 0; ok = ArgI(a, v, 0, 1, g); gpuSync = g != 0; }
		else if (!wcscmp(a, L"--scene"))
		{
			ok = !wcscmp(v, L"grid") || !wcscmp(v, L"test") || !wcscmp(v, L"terrain") || !wcscmp(v, L"bakefake");
			sceneGrid = !wcscmp(v, L"grid"); sceneTerrain = !wcscmp(v, L"terrain"); sceneBakeFake = !wcscmp(v, L"bakefake");
		}
		else if (!wcscmp(a, L"--exe")) { ok = v[0] != 0 && wcslen(v) < MAX_PATH; if (ok) wcsncpy_s(to.exe, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--tap-file")) { ok = v[0] != 0 && wcslen(v) < MAX_PATH; if (ok) wcsncpy_s(to.tapFile, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--save-tap")) { ok = v[0] != 0 && wcslen(v) < MAX_PATH; if (ok) wcsncpy_s(to.saveTap, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--tile-plots")) ok = ArgI(a, v, 1, (int)c5t::kMaxTilePlots, to.tilePlots);
		else if (!wcscmp(a, L"--tile-d4")) ok = ArgI(a, v, 0, 1024, to.tileD4);
		else if (!wcscmp(a, L"--pass")) ok = ArgI(a, v, -1, 2, to.pass);
		else if (!wcscmp(a, L"--tess")) ok = ArgD(a, v, 0, 64, to.tess);
		else if (!wcscmp(a, L"--water")) { int w = 0; ok = ArgI(a, v, 0, 1, w); to.water = w != 0; }
		else if (!wcscmp(a, L"--depth")) { int d = 0; ok = ArgI(a, v, 0, 1, d); to.depth = d != 0; }
		else if (!wcscmp(a, L"--color") || !wcscmp(a, L"--fallback"))
		{
			bool fb = !wcscmp(a, L"--fallback");
			c5t::ColorSource cs = c5t::ColorSource::Caches;
			ok = true;
			if (!wcscmp(v, L"caches") && !fb) cs = c5t::ColorSource::Caches;
			else if (!wcscmp(v, L"material")) cs = c5t::ColorSource::Material;
			else if (!wcscmp(v, L"constant")) cs = c5t::ColorSource::Constant;
			else if (!wcscmp(v, L"height")) cs = c5t::ColorSource::Height;
			else if (!wcscmp(v, L"normal")) cs = c5t::ColorSource::Normal;
			else ok = false;
			if (ok) (fb ? to.fallback : to.color) = cs;
		}
		else if (!wcscmp(a, L"--constant-rgba"))
		{
			wchar_t* end = nullptr;
			unsigned long long c = _wcstoui64(v, &end, 16);
			ok = v[0] && end && !*end && c <= 0xFFFFFFFFull;
			if (ok) to.constantRgba = (uint32_t)c;
		}
		else if (!wcscmp(a, L"--fake-tiles"))
		{
			unsigned w = 0, h = 0; wchar_t extra = 0;
			ok = swscanf_s(v, L"%ux%u%c", &w, &h, &extra, 1) == 2 && w >= 1 && h >= 1 && w * h <= VPSHELL_BAKE_MAX_SLOTS;
			if (ok) { to.fakeTilesX = (int)w; to.fakeTilesY = (int)h; }
		}
		else if (!wcscmp(a, L"--overlay-grid")) { int og = 0; ok = ArgI(a, v, 0, 1, og); to.overlayGrid = og != 0; }
		else if (!wcscmp(a, L"--fake-typeless")) { int ft = 0; ok = ArgI(a, v, 0, 1, ft); to.fakeTypeless = ft != 0; }
		else if (!wcscmp(a, L"--fake-missing")) ok = ArgI(a, v, 0, (int)VPSHELL_BAKE_MAX_SLOTS - 1, to.fakeMissing);
		else if (!wcscmp(a, L"--fake-manifest-version")) ok = ArgI(a, v, 0, 1000, to.fakeManifestVersion);
		else if (!wcscmp(a, L"--bake"))
		{
			ok = !wcscmp(v, L"tap") || !wcscmp(v, L"standin") || !wcscmp(v, L"pipeline");
			to.residency = !wcscmp(v, L"standin") || !wcscmp(v, L"pipeline");
			to.pipeline = !wcscmp(v, L"pipeline");
		}
		else if (!wcscmp(a, L"--game-dir")) { ok = v[0] != 0 && wcslen(v) < MAX_PATH; if (ok) wcsncpy_s(to.gameDir, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--cache-arrays")) { int ca = 1; ok = ArgI(a, v, 0, 1, ca); to.res.arrays = ca != 0; }
		else if (!wcscmp(a, L"--bake-memory-mb")) ok = ArgD(a, v, 0, 1e6, to.res.memoryLimitMb);
		else if (!wcscmp(a, L"--bake-radius")) ok = ArgI(a, v, -1, 128, to.bakeRadius);
		else if (!wcscmp(a, L"--options-file")) { ok = v[0] != 0 && wcslen(v) < MAX_PATH; if (ok) wcsncpy_s(to.optionsFile, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--bake-rate")) ok = ArgD(a, v, 0.001, 1e6, to.res.rate);
		else if (!wcscmp(a, L"--bake-burst")) ok = ArgI(a, v, 0, 100000, to.res.burst);
		else if (!wcscmp(a, L"--bake-catch-up")) { int cu = 1; ok = ArgI(a, v, 0, 1, cu); if (ok) to.res.catchUpVisible = cu != 0; }
		else if (!wcscmp(a, L"--bake-pump-ms")) ok = ArgD(a, v, 0.01, 1000, to.pumpMs);
		else if (!wcscmp(a, L"--bake-trace")) ok = ArgI(a, v, 0, 100000, to.res.trace);
		else if (!wcscmp(a, L"--bake-gpu-ms-per-s")) ok = ArgD(a, v, 0, 1000, to.res.gpuMsPerS);
		else if (!wcscmp(a, L"--tilegen")) { ok = v[0] != 0 && wcslen(v) < 128; if (ok) wcsncpy_s(to.tilegen, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--standin-fail"))
		{
			int fx = -1, fy = -1; wchar_t extra = 0;
			ok = swscanf_s(v, L"%d,%d%c", &fx, &fy, &extra, 1) == 2 && fx >= 0 && fy >= 0 && fx < 4096 && fy < 4096;
			if (ok) { to.faults.failX = fx; to.faults.failY = fy; }
		}
		else if (!wcscmp(a, L"--standin-vram-mb")) ok = ArgD(a, v, 0.001, 1e6, to.faults.vramBudgetMb);
		else if (!wcscmp(a, L"--standin-ms")) ok = ArgD(a, v, 0, 1000, to.faults.cpuMs);
		else if (!wcscmp(a, L"--grid-scale")) ok = ArgD(a, v, 0.01, 10, go.scale);
		else if (!wcscmp(a, L"--flat-z")) ok = ArgD(a, v, -1e5, 1e5, go.flatZ);
		else if (!wcscmp(a, L"--water-z")) ok = ArgD(a, v, -1e5, 1e5, go.waterZ);
		else if (!wcscmp(a, L"--heightmap")) { ok = v[0] != 0 && wcslen(v) < MAX_PATH; if (ok) wcsncpy_s(go.heightmap, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--edge-div")) ok = ArgI(a, v, 1, 64, go.edgeDiv);
		else if (!wcscmp(a, L"--stale-frames")) ok = ArgI(a, v, 1, 1000000, go.staleFrames);
		else if (!wcscmp(a, L"--bad-frames")) ok = ArgI(a, v, 0, 1000000, go.badFrames);
		else if (!wcscmp(a, L"--grid-stale")) { ok = !wcscmp(v, L"hide") || !wcscmp(v, L"tint"); go.staleTint = wcscmp(v, L"hide") != 0; }
		else if (!wcscmp(a, L"--dump")) { dumpPath = v; ok = v[0] != 0; }
		else if (!wcscmp(a, L"--dump-after")) ok = ArgI(a, v, 1, 1000000, dumpAfter);
		else if (!wcscmp(a, L"--test-abandon")) ok = ArgI(a, v, 1, 1000000, testAbandon);
		else if (!wcscmp(a, L"--tex-format"))
		{
			ok = !wcscmp(v, L"bgra") || !wcscmp(v, L"rgba");
			texFormat = !wcscmp(v, L"rgba") ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
		}
		else if (!wcscmp(a, L"--print-layout")) return PrintLayout(v) ? 0 : 1;
		else { fwprintf(stderr, L"unknown argument %s\n", a); return 2; }
		if (!ok) { if (!v[0] || !wcscmp(a, L"--section") || !wcscmp(a, L"--name") || !wcscmp(a, L"--log") || !wcscmp(a, L"--scene") ||
			!wcscmp(a, L"--grid-stale") || !wcscmp(a, L"--dump") || !wcscmp(a, L"--heightmap") || !wcscmp(a, L"--exe") ||
			!wcscmp(a, L"--tap-file") || !wcscmp(a, L"--color") || !wcscmp(a, L"--fake-tiles") || !wcscmp(a, L"--fallback") ||
			!wcscmp(a, L"--constant-rgba") || !wcscmp(a, L"--save-tap") || !wcscmp(a, L"--tex-format") || !wcscmp(a, L"--bake") ||
			!wcscmp(a, L"--options-file") || !wcscmp(a, L"--tilegen") || !wcscmp(a, L"--standin-fail") || !wcscmp(a, L"--game-dir"))
				fwprintf(stderr, L"bad value for %s: '%s'\n", a, v);
			return 2; }
	}
	if (to.pipeline && !to.gameDir[0])
	{
		DWORD n = GetEnvironmentVariableW(L"C5R_GAME_DIR", to.gameDir, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) { to.gameDir[0] = 0; fwprintf(stderr, L"--bake pipeline needs --game-dir DIR or C5R_GAME_DIR (the install, read-only)\n"); return 2; }
	}
	if (!to.pipeline && to.gameDir[0]) { fwprintf(stderr, L"--game-dir needs --bake pipeline\n"); return 2; }
	if (to.residency && !sceneTerrain) { fwprintf(stderr, L"--bake standin needs --scene terrain\n"); return 2; }
	if (to.residency && to.tilePlots && to.tilePlots != 1)
	{ fwprintf(stderr, L"--bake standin needs tile plots 1 (the EXE's tile is one 64-sample plot); --tile-plots %d refused\n", to.tilePlots); return 2; }
	if (to.residency && to.color != c5t::ColorSource::Caches)
	{ fwprintf(stderr, L"--bake standin draws the caches: --color must be caches\n"); return 2; }
	if (!to.residency && (to.bakeRadius != -2 || to.optionsFile[0] || to.tilegen[0] || to.faults.failX >= 0 || to.faults.vramBudgetMb > 0 ||
		to.faults.cpuMs > 0))
	{ fwprintf(stderr, L"--bake-radius, --options-file, --tilegen and --standin-* need --bake standin\n"); return 2; }
	if (sceneTerrain && !to.exe[0])
	{
		DWORD n = GetEnvironmentVariableW(L"C5R_EXE", to.exe, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
		{
			to.exe[0] = 0; fwprintf(stderr, L"--scene terrain needs --exe PATH or C5R_EXE (CivilizationV_DX11.exe, read-only)\n"); return 2; }
	}
	if (sceneBakeFake && !to.fakeTilesX) { fwprintf(stderr, L"--scene bakefake needs --fake-tiles WxH\n"); return 2; }
	if (sceneTerrain) to.rateCap = rateGiven ? rate : 0;
	g_log = _wfsopen(logPath, L"a", _SH_DENYNO); // shared: readable while the server runs (shell/tests)
	QueryPerformanceFrequency(&g_freq);
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
	timeBeginPeriod(1);
	// Bug 0118 (tracker 4000): this process has no visible window (hidden desktop; none at all on a player's machine).
	// Windows 11 then stops honouring timeBeginPeriod(1) after a few seconds and every wait that rests on the system
	// tick runs at 64 Hz (measured 2026-11-13, research/05-graphics-composition/lockstep-probe.md 3.2); opting out of
	// power throttling (execution speed and timer resolution) keeps the 1 ms tick.
	char szPower[320];
	SrvPowerOptOut(szPower, sizeof(szPower));
	Log("shell_server start pid %u size %ux%u rate %s%.1f gpu-sync %d scene %s", GetCurrentProcessId(), width, height, rate <= 0 ? "max (" : "", rate, (int)gpuSync,
		sceneGrid ? "grid" : sceneTerrain ? "terrain (draws per new camera block)" : sceneBakeFake ? "bakefake" : "test");
	Log("%s", szPower);

	Server s;
	s.texFormat = texFormat;
	if (!PickAdapter(s, wantLuid)) return 1;
	D3D_FEATURE_LEVEL fl;
	const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	HRESULT hr = D3D11CreateDevice(s.adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, fls, 2, D3D11_SDK_VERSION, &s.dev, &fl, &s.ctx);
	if (FAILED(hr)) { Log("D3D11CreateDevice failed 0x%08X", hr); return 1; }
	s.dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&s.dev1);
	Log("device created, feature level %x", fl);
	if (sceneBakeFake)
	{
		int frc = FakeBakeMain(s, sectionName, to, seconds);
		SafeRelease(s.ctx); SafeRelease(s.dev1); SafeRelease(s.dev); SafeRelease(s.adapter);
		timeEndPeriod(1);
		if (g_log) fclose(g_log);
		return frc;
	}

	if (FAILED(s.dev->CreateVertexShader(g_VSFullscreen, sizeof(g_VSFullscreen), nullptr, &s.vs)) ||
		FAILED(s.dev->CreatePixelShader(g_PSServer, sizeof(g_PSServer), nullptr, &s.ps)))
	{ Log("shader creation failed"); return 1; }
	D3D11_BUFFER_DESC bd = { sizeof(ServerCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	if (FAILED(s.dev->CreateBuffer(&bd, nullptr, &s.cb))) { Log("CreateBuffer failed"); return 1; }
	D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
	for (auto& q : s.inflight) s.dev->CreateQuery(&qd, &q);
	s.dev->CreateQuery(&qd, &s.syncQuery);

	// Shared section: create or open (a client may still hold the previous server's section).
	s.section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, VPSHELL_SECTION_BYTES, sectionName);
	if (!s.section) { Log("CreateFileMapping('%ls') failed %u", sectionName, GetLastError()); return 1; }
	bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
	s.hdr = (VPShellHeader*)MapViewOfFile(s.section, FILE_MAP_ALL_ACCESS, 0, 0, VPSHELL_SECTION_BYTES);
	if (!s.hdr) { Log("MapViewOfFile failed %u", GetLastError()); return 1; }
	Log("section '%ls' %s", sectionName, existed ? "opened (already existed: client or previous server holds it)" : "created");
	if (s.hdr->magic != VPSHELL_MAGIC) { s.hdr->texSeq = 0; s.hdr->reqSeq = 0; s.hdr->ackSeq = 0; }
	s.hdr->qpcFrequency = g_freq.QuadPart;
	s.hdr->frameCounter = 0;
	s.hdr->frameQpc = 0;
	s.hdr->framesOverwritten = 0;
	s.hdr->version = VPSHELL_VERSION;
	GridState grid;
	if ((sceneGrid || (sceneTerrain && to.overlayGrid)) && !GridInit(s, grid, go)) return 1;
	TerrainState terrain;
	if (sceneTerrain)
	{
		int trc = 0;
		if (!TerrainInit(s, terrain, to, trc))
		{
			Log("exit %d: terrain scene not started", trc);
			s.hdr->magic = 0;
			TerrainRelease(terrain);
			if (g_log) fclose(g_log);
			return trc;
		}
	}
	// honour a resize request a client made before we (re)started
	// Start at the size the client last asked for (pending or already acked by a
	// previous server instance), so a restart does not cost a texture recreation.
	uint32_t pendingSeq = s.hdr->reqSeq;
	// (the section only outlives a server while a client holds it, so these values are that client's)
	{
		UINT rw = s.hdr->reqWidth, rh = s.hdr->reqHeight;
		if (rw >= 16 && rh >= 16 && rw <= 16384 && rh <= 16384)
		{
			width = rw; height = rh;
			Log("starting at the client's last requested size %ux%u (seq %u)", width, height, pendingSeq);
		}
	}
	s.hdr->ackSeq = pendingSeq;
	if (!CreateTextures(s, width, height, baseName)) return 1;
	MemoryBarrier();
	s.hdr->magic = VPSHELL_MAGIC;
	LogMemory(s, "start");

	const int64_t start = Qpc();
	const int64_t period = rate > 0 ? (int64_t)(g_freq.QuadPart / rate) : 0;
	int64_t next = start;
	int64_t frame = 0, published = 0, overwritten = 0, skipped = 0;
	int64_t secStart = start, secRendered = 0, secPublished = 0, secOverwritten = 0, secSkipped = 0;
	double secAcquireMaxMs = 0;
	int64_t lastMemLog = start;
	uint32_t lastClientPid = 0;
	bool dumped = false;
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

	while (!g_quit)
	{
		int64_t now = Qpc();
		if (seconds > 0 && QpcToMs(now - start) > seconds * 1000.0) break;
		InterlockedExchange64(&s.hdr->heartbeatQpc, now);

		// resize request from the client?
		uint32_t rs = s.hdr->reqSeq;
		if (rs != s.hdr->ackSeq)
		{
			MemoryBarrier();
			UINT rw = s.hdr->reqWidth, rh = s.hdr->reqHeight;
			if (rw >= 16 && rh >= 16 && rw <= 16384 && rh <= 16384 && (rw != s.width || rh != s.height))
			{
				Log("client requested %ux%u (seq %u)", rw, rh, rs);
				if (!CreateTextures(s, rw, rh, baseName)) { Log("resize failed, exiting"); break; }
				terrain.dirty = true;   // the new texture is empty: draw again with the last camera
			}
			s.hdr->ackSeq = rs;
		}
		if (s.hdr->clientPid != lastClientPid)
		{
			lastClientPid = s.hdr->clientPid;
			uint64_t cl = s.hdr->clientLuid;
			Log("client pid %u, client adapter LUID %016llX%s", lastClientPid, cl, (cl && cl != s.luid) ? "  ** MISMATCH: shared texture will not open on that adapter **" : "");
		}

		// limit GPU work in flight to 2 frames (no swap chain to throttle us)
		ID3D11Query* q = s.inflight[frame % 3];
		if (frame >= 3) { while (s.ctx->GetData(q, nullptr, 0, 0) == S_FALSE) YieldProcessor(); }

		// render the scene into the private target
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(s.ctx->Map(s.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			ServerCB* c = (ServerCB*)m.pData;
			c->time = (float)(QpcToMs(now - start) / 1000.0);
			c->sizeX = (float)s.width; c->sizeY = (float)s.height;
			c->frameLo = (uint32_t)(frame + 1); c->frameHi = (uint32_t)((frame + 1) >> 32);
			s.ctx->Unmap(s.cb, 0);
		}
		D3D11_VIEWPORT vp = { 0, 0, (float)s.width, (float)s.height, 0, 1 };
		s.ctx->OMSetRenderTargets(1, &s.privRtv, nullptr);
		s.ctx->RSSetViewports(1, &vp);
		bool dumpNow = false;
		bool publishThis = true;
		if (sceneTerrain)
		{
			publishThis = TerrainFrame(s, terrain, to, go, baseName);
			if (publishThis && to.overlayGrid)
			{
				// the terrain draw leaves its tessellation stages and states bound: reset them for the line list
				s.ctx->HSSetShader(nullptr, nullptr, 0); s.ctx->DSSetShader(nullptr, nullptr, 0); s.ctx->GSSetShader(nullptr, nullptr, 0);
				s.ctx->RSSetState(nullptr); s.ctx->OMSetDepthStencilState(nullptr, 0); s.ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
				s.ctx->OMSetRenderTargets(1, &s.privRtv, nullptr);
				GridDrawCam(s, grid, go, terrain.cam, false);
			}
			if (publishThis && dumpPath && terrain.drawnTotal >= dumpAfter) dumpNow = true;
		}
		else if (sceneGrid)
		{
			const float clear[4] = { 0, 0, 0, 0 };   // transparent: composite with blend=alpha shows only the lines
			s.ctx->ClearRenderTargetView(s.privRtv, clear);
			if (GridFrame(s, grid, go) > 0 && dumpPath && grid.drawnTotal >= dumpAfter) dumpNow = true;
		}
		else
		{
			s.ctx->IASetInputLayout(nullptr);
			s.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			s.ctx->VSSetShader(s.vs, nullptr, 0);
			s.ctx->PSSetShader(s.ps, nullptr, 0);
			s.ctx->PSSetConstantBuffers(0, 1, &s.cb);
			s.ctx->Draw(3, 0);
			if (dumpPath && frame + 1 >= dumpAfter) dumpNow = true;
		}
		s.ctx->OMSetRenderTargets(0, nullptr, nullptr);
		if (publishThis) secRendered++;
		if (dumpNow)
		{
			dumped = DumpTarget(s, dumpPath);
			if (sceneTerrain && to.depth)
			{
				// the depth copy the publish would make, then its raw dump next to the colour dump
				s.ctx->CopyResource(terrain.depthShared, terrain.depthTex);
				std::wstring dp = std::wstring(dumpPath) + L".depth.f32";
				dumped = DumpDepth(s, terrain, dp.c_str()) && dumped;
			}
			break;
		}

		// publish: take the texture from whichever side holds it free
		int64_t a0 = Qpc();
		HRESULT ar = S_OK;
		bool got = false, wasUnconsumed = false;
		for (; publishThis;)
		{
			ar = s.km->AcquireSync(0, 0);
			if (ar == S_OK) { got = true; break; }
			ar = s.km->AcquireSync(1, 0);
			if (ar == S_OK) { got = true; wasUnconsumed = true; break; }
			if (ar != (HRESULT)WAIT_TIMEOUT && FAILED(ar)) { Log("AcquireSync error 0x%08X", ar); break; }
			if (QpcToMs(Qpc() - a0) > 50.0) break; // client holds it for too long: skip this frame
			Sleep(0);
		}
		double aMs = QpcToMs(Qpc() - a0);
		if (aMs > secAcquireMaxMs) secAcquireMaxMs = aMs;
		if (got)
		{
			s.ctx->CopyResource(s.sharedTex, s.privTex);
			if (sceneTerrain && to.depth && terrain.depthShared) s.ctx->CopyResource(terrain.depthShared, terrain.depthTex);
			if (gpuSync)
			{
				// Wait until the GPU has finished this frame before handing it over, so the
				// client's GPU queue (the game's) never stalls on our work after AcquireSync.
				s.ctx->End(s.syncQuery);
				s.ctx->Flush();
				while (s.ctx->GetData(s.syncQuery, nullptr, 0, 0) == S_FALSE) YieldProcessor();
			}
			frame++;
			s.hdr->frameCounter = frame;
			s.hdr->frameQpc = Qpc();
			if (sceneTerrain && to.depth && terrain.depthShared) PublishDepthBlock(s.hdr, terrain, frame, 1);
			if (testAbandon && frame >= testAbandon)
			{
				// --test-abandon N (issue 3013, tests only): die holding the keyed mutex with a new frame announced, so the
				// client's next AcquireSync meets an abandoned mutex (its WAIT_ABANDONED path)
				Log("test-abandon: terminating at frame %lld while holding the keyed mutex", frame);
				TerminateProcess(GetCurrentProcess(), 3);
			}
			if (wasUnconsumed) { overwritten++; secOverwritten++; s.hdr->framesOverwritten = overwritten; }
			s.km->ReleaseSync(1);
			s.ctx->End(q);
			s.ctx->Flush();
			published++; secPublished++;
		}
		else if (publishThis) { skipped++; secSkipped++; }
		if (sceneTerrain) TerrainPump(terrain);

		now = Qpc();
		if (QpcToMs(now - secStart) >= 1000.0)
		{
			double heldBy = 0;
			Log("1s: rendered %lld published %lld overwritten(unconsumed) %lld skipped %lld | acquire max %.2f ms | client consumed total %lld | gen %u %ux%u",
				secRendered, secPublished, secOverwritten, secSkipped, secAcquireMaxMs,
				s.hdr->clientFramesConsumed, s.generation, s.width, s.height);
			(void)heldBy;
			secStart = now; secRendered = secPublished = secOverwritten = secSkipped = 0; secAcquireMaxMs = 0;
			if (sceneGrid) GridSecond(grid);
			if (sceneTerrain) TerrainSecond(s, terrain);
		}
		if (QpcToMs(now - lastMemLog) >= 10000.0) { LogMemory(s, "periodic"); lastMemLog = now; }

		// pacing
		if (sceneTerrain)
		{
			// D58 6: no fixed rate; poll the camera block about every 0.5 ms (a new block is drawn within ~0.5 ms of
			// its publication) - the draw itself is gated on a new frameStamp in TerrainFrame.
			if (timer)
			{
				LARGE_INTEGER due; due.QuadPart = -5000;   // 0.5 ms
				SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
				WaitForSingleObject(timer, 5);
			}
			else Sleep(1);
		}
		else if (period > 0)
		{
			next += period;
			int64_t remain = next - Qpc();
			if (remain < -period) { next = Qpc(); remain = 0; } // fell behind: resync
			if (remain > 0)
			{
				double ms = QpcToMs(remain);
				if (timer && ms > 1.5)
				{
					LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((ms - 1.0) * 10000.0);
					SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
					WaitForSingleObject(timer, 100);
				}
				while (Qpc() < next) YieldProcessor();
			}
		}
	}

	Log("exiting: published %lld overwritten %lld skipped %lld", published, overwritten, skipped);
	LogMemory(s, "end");
	s.hdr->magic = 0; // tell clients there is no live server behind this section
	if (sceneTerrain)
	{
		Log("terrain totals: drawn %lld draw errors %lld stale presents %lld hidden presents %lld camera blocks %lld tap loads %lld tap errors %lld "
			"field errors %lld cache opened %lld cache missing %lld cache refused %lld manifest errors %lld",
			terrain.drawnTotal, terrain.drawErrTotal, terrain.staleTotal, terrain.hiddenTotal, terrain.camBlocksTotal, terrain.tapLoads,
			terrain.tapErrors, terrain.fieldErrors, terrain.cacheOpened, terrain.cacheMissing, terrain.cacheRefused, terrain.manifestErrors);
		if (terrain.baker)
		{
			const c5r::bake::ResidencyStats& r = terrain.res.stats();
			Log("residency arrays: array kb %llu arrays %u copies %lld", (unsigned long long)(r.arrayBytes / 1024), r.arrays,
				(long long)r.copies);
			Log("residency totals: land %u class1 %u class2 %u class3 %u resident %u resident kb %llu bakes %lld rebakes %lld failed %lld "
				"oom %lld evicted %lld sink errors %lld radius %d stopped %d ahead done %d ahead ms %lld frames %lld gray frames %lld "
				"gray after ahead %lld gray tile frames %lld stale frames %lld raises %lld latency n %lld latency max us %lld "
				"tilegen raised %lld tilegen errors %lld fields %lld field changed tiles %lld catch up %d catch up draws %lld "
				"catch up bakes %lld",
				r.land, r.class1, r.class2, r.class3, r.resident, (unsigned long long)(r.cacheBytes / 1024), (long long)r.bakes,
				(long long)r.rebakes, (long long)r.failures, (long long)r.oomEvents, (long long)r.evicted, (long long)r.sinkErrors,
				r.radiusInForce, (int)r.stopped, (int)r.aheadDone, (long long)r.aheadMs, (long long)r.framesCounted, (long long)r.grayFrames,
				(long long)r.grayFramesAfterAhead, (long long)r.grayTileFrames, (long long)r.staleFrames, (long long)r.raises,
				(long long)r.latencyN, (long long)(r.latencyMaxMs * 1000.0), (long long)terrain.tilegen.raised,
				(long long)terrain.tilegen.errors, (long long)r.fields, (long long)r.fieldChangedTiles, (int)to.res.catchUpVisible,
				(long long)r.catchUpDraws, (long long)r.catchUpBakes);
		}
		if (to.depth) PublishDepthBlock(s.hdr, terrain, frame, 0);
		TerrainRelease(terrain);
	}
	ReleaseTextures(s);
	for (auto& q : s.inflight) SafeRelease(q);
	SafeRelease(s.syncQuery);
	SafeRelease(s.cb); SafeRelease(s.ps); SafeRelease(s.vs);
	SafeRelease(grid.vb); SafeRelease(grid.cb); SafeRelease(grid.il); SafeRelease(grid.ps); SafeRelease(grid.vs); free(grid.hm);
	SafeRelease(s.ctx); SafeRelease(s.dev1); SafeRelease(s.dev); SafeRelease(s.adapter);
	UnmapViewOfFile(s.hdr); CloseHandle(s.section);
	if (timer) CloseHandle(timer);
	timeEndPeriod(1);
	// issue 3013: a requested dump that was never written (no camera, nothing drawn, unwritable path) is a failure
	int rc = (dumpPath && !dumped) ? 1 : 0;
	if (rc) Log("exit 1: --dump '%ls' requested but not written", dumpPath);
	if (g_log) fclose(g_log);
	return rc;
}
