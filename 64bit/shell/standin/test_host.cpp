// Origin (rule 13, decision D22): the project's fork of Community-Patch-DLL, branch claude/shell-gates-d8e422,
// path experiments/shell-gates/compositor/src/test_host.cpp, copied at branch head c60138b82 (file last changed in a9d8328e2).
// Copied into shell/standin/test_host.cpp on 2026-11-10 (issue 3001); later changes are in this repository's history.
// test_host.exe - x86 stand-in for CivilizationV_DX11.exe.
//
// A window with a D3D11 device and a blit- or flip-model swap chain. Each
// frame: clear the back buffer to dark grey, draw opaque and translucent "UI"
// rectangles plus text-like noise with alpha blending, Present. Loads
// shell_composite.dll with LoadLibraryW before or after creating the device.
//
// Usage: test_host.exe [options]
//   --swap blit|flip|flipseq   swap effect (default blit = DXGI_SWAP_EFFECT_DISCARD)
//   --buffers N                buffer count (default 1 for blit, 2 for flip)
//   --format rgba|bgra|srgb    back-buffer format (default rgba = R8G8B8A8_UNORM)
//   --msaa N                   back-buffer sample count (blit only, default 1)
//   --scene-msaa N             (ticket 0804, with --game) the clear and units lists draw into an N-sampled (N >= 1) target of the
//                              back buffer's size with an N-sampled depth, resolved (N = 1: copied) into the back buffer before the UI
//   --vsync 0|1                (default 1)
//   --load before|after|none   when to LoadLibrary the DLL (default after)
//   --load-frames N            frames rendered before an "after" load (default 30)
//   --dll PATH                 (default shell_composite.dll next to the exe)
//   --size WxH                 client size (default 1280x720)
//   --pos X,Y                  window position (default -1900,380 = left monitor)
//   --bb WxH                   fixed back-buffer size, independent of the window
//   --borderless               WS_POPUP window
//   --seconds N                quit after N seconds
//   --scenario robust          scripted minimize/restore/resize/ResizeBuffers(/fullscreen)
//   --allow-fullscreen         let the scenario / F2 use SetFullscreenState
//   --deferred                 record the clear on a deferred context and run it with
//                              ExecuteCommandList (Civ 5 style); the UI stays on the immediate context
//   --churn N                  every N seconds ResizeBuffers to the next of 1280x720/1920x1080/2560x1440 (soak)
//   --label TEXT --summary PATH  append a CSV summary line at exit
//   --log PATH                 (default test_host.log next to the exe)
// Issue 3006 (game-like frame, like Civ 5's DX11 renderer: everything recorded on deferred contexts):
//   --game                     each frame records lists on deferred contexts and runs them with ExecuteCommandList:
//                                clear   : binds back buffer + D24S8 depth, clears both (the scene clear)
//                                terrain : offscreen R11G11B10 target + D32 depth (a pre-pass; nothing on the back buffer)
//                                units   : back buffer + depth, depth test LESS: a yellow "landmark" (z 0.8), a cyan unit
//                                          (z 0.6) and an orange unit (z 0.3) overlapping
//                                ui      : back buffer without depth, the usual alpha-blended UI
//                                misc    : 64x64 offscreen target, cleared (stand-in for shadow/reflection lists)
//   --order LIST               comma list of clear,terrain,units,ui (default terrain,clear,units,ui)
//   --vary 0|1                 1 = per frame: terrain before or after the clear, and 0-3 misc lists at random
//                              positions, so list indices change frame to frame (default 0)
//   --mt 0|1                   1 = record clear/terrain/units/ui on four worker threads (default 1)
//   --seed N                   random seed for --vary (default 1)
//   --recreate-at S            at S seconds destroy the window and swap chain and create new ones (as the game's
//                              resolution change does); works with or without --game
//   --recreate-same 1          (issue 3017) --recreate-at keeps the window: only a new swap chain, as the game does
//   --recreate-same-address N  (issue 0141) with --recreate-same 1: the new swap chain must get the released old chain's
//                              address (heap reuse, the case that made the composite miss the new chain); a chain at
//                              another address is kept (never presented) and another created, up to N chains, then
//                              the kept ones are released. Logs "recreate: old address reused after K tries"; else
//                              "recreate: old address NOT reused in N tries", and the newest chain presents and is
//                              recreated again 0.5 s later, up to 8 rounds ("... no more rounds" after the last)
//   --extra-chains N           (issue 3017) at 2 s, N more windows with swap chains made through the factory, presenting
//                              after the main chain every frame (an overlay or second window; must not take the target)
//   --occluded-sleep MS        sleep after an occluded Present (default 16; 0 on a hidden desktop, where every
//                              Present reports DXGI_STATUS_OCCLUDED, to run at full rate)
// Issue 3007 (a fake EXE camera for shell_composite.dll's camera read; see FakeExeInit):
//   --fake-exe                 lay out a fake CivilizationV_DX11.exe image (patterns, camera globals, GVS, Tiny-map grid
//                              constants) and pass its base to the DLL in VPSHELL_EXE_BASE
//   --fake-cam-x X --fake-cam-y Y   camera target in world units (default 0, 0)
//   --fake-cam-pan U           pan speed in +x, units per second (default 0)
//   --fake-cam-views N         g_NumViews 1..3 (2: one wrap copy toward the nearer seam, 3: both)
//   --fake-cam-writer frame|thread  write the camera before each frame (default) or from a thread, nonstop
//   --fake-cam-freeze-at S     from S seconds the view globals stop updating while camWorld keeps moving
//   --fake-cam-mode-at S:M     at S seconds GVS+0x758 becomes M (2 = Strategic View)
//   --fake-exe-build B         ticket 4964: the Steam build the fake image is laid out as (4390913, the default, or
//                              26577625): its PE stamp and SizeOfImage and every address from the build table compiled in
//   --fake-exe-bad tds|sig|mz|pe|soi  a TimeDateStamp of no known build, one wrong pattern byte, no MZ, e_lfanew past 4 KB,
//                              or (4964) the build's stamp with another SizeOfImage (the DLL must refuse; mz and pe: 3013)
//   --fake-cam-bad KIND        issue 3013: one broken piece of camera state (null-app: g_Civ5App 0; bad-vptr: GVS+0x48 is
//                              not the FCamera vtable; unmapped-app: g_Civ5App = 0x10, the read faults; views0 / views9:
//                              g_NumViews out of range; wrap: a wrap view that is not a +-period copy; grid-not-ready)
//   --format rgb10a2           (issue 3013) a back-buffer format the .shot dump does not support
// Issue 3304 (lockstep; with --fake-exe):
//   --fake-uv                  a fake UpdateViews: the fake image gets the bytes the composite's UpdateViews hook checks
//                              (ls_loop - 12 .. ls_ret + 1 of the build table: push esi; xor esi,esi; lea ecx,
//                              [esi+&g_View[0]]; push ecx; push 0; lea eax,[esi+&g_InvView[0]]; push eax; call the thunk;
//                              pop esi; ret), the import thunk ls_thunk (jmp [slot]) and the import slot ls_iat_matinv =
//                              D3DXMatrixInverse of d3dx9_42.dll (build 4390913) or d3dx9_43.dll (26577625), loaded from
//                              the system (ticket 4964; build 4390913's VAs: 0x51E1B0, 0x8A86B6, 0x9D6B74). Called once per frame after the camera is written,
//                              before the frame's lists: the publish point of lockstep=1 on the stand-in.
//   --fake-uv-every N          call it only every Nth frame (the others have no UpdateViews: publish at Present)
//   --uv-work-us N             spin N us after the fake UpdateViews (the EXE's work before its scene-clear list)
//   --fake-cam-move-after-uv   shift view 0 by one unit after the fake UpdateViews (premise 3's negative case: the camera
//                              at Present differs from the one at UpdateViews)
//   --fake-uv-thread           call the fake UpdateViews on another thread (the main thread waits): must not publish
//   --depth-dump PATH          (with --game) once, at --depth-dump-at seconds, after the frame's lists and before its
//   --depth-dump-at S          Present: the D24S8 depth buffer read back into PATH ("C5DD", width, height, then
//                              width*height uint32: depth in bits 0..23, stencil in 24..31)
//   --depth-dump-on FILE       (ticket 0494) instead of --depth-dump-at: at the first frame after FILE exists (looked
//                              for every 5 ms): the test makes it at a time after the composite's hook commit, which
//                              took seconds under load and is counted in this host's clock
// Ticket 0697 (the proxy's Fatal on a hung server):
//   --fatal-sink               pass the composite a hung sink (VPShell_SetHungSink) that does what the proxy's failure
//                              route does in a D33 session: VPShell_FlushTotals("fatal", ...), then TerminateProcess
//                              with exit code 3 (no DLL_PROCESS_DETACH); without the export only the exit
// Keys: F1 ResizeBuffers to the next resolution, F2 fullscreen toggle,
//       F3 resize the window, Esc quit.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <share.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include "VSHostUi.h"
#include "PSHostUi.h"
#include "exe_camera.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "psapi.lib")

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static FILE* g_log;
static LARGE_INTEGER g_freq;
static int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double QpcToMs(int64_t d) { return (double)d * 1000.0 / (double)g_freq.QuadPart; }

static void Log(const char* fmt, ...)
{
	char buf[1024];
	SYSTEMTIME st; GetLocalTime(&st);
	int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap; va_start(ap, fmt);
	_vsnprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, fmt, ap);
	va_end(ap);
	if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
	fputs(buf, stdout); fputc('\n', stdout);
}

struct UiRect { float rect[4]; float color[4]; float style[4]; };
struct HostCB { float vp[2]; float time; float pad; UiRect rects[64]; };

struct Opts
{
	int swap = 0; // 0 blit, 1 flip discard, 2 flip sequential
	int buffers = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	UINT msaa = 1;
	UINT sceneMsaa = 0;         // ticket 0804: --scene-msaa N (with --game: the scene in an N-sampled target, resolved)
	int vsync = 1;
	int load = 1; // 0 before, 1 after, 2 none
	int loadFrames = 30;
	wchar_t dll[MAX_PATH] = L"";
	UINT w = 1280, h = 720;
	int x = -1900, y = 380;
	UINT bbW = 0, bbH = 0;
	bool borderless = false;
	double seconds = 0;
	bool scenario = false;
	bool allowFullscreen = false;
	double churn = 0;
	bool deferred = false;
	bool game = false;
	wchar_t order[128] = L"terrain,clear,units,ui";
	int vary = 0;
	int mt = 1;
	unsigned seed = 1;
	double recreateAt = 0;
	int recreateSame = 0;       // issue 3017: 1 = --recreate-at keeps the window (the game's resolution change)
	int recreateSameAddress = 0; // issue 0141: tries to get the old chain's address for the new chain (0 = do not try)
	int extraChains = 0;        // issue 3017: N more windows + swap chains created through the factory at 2 s, all presenting
	int occludedSleep = 16;
	bool fatalSink = false;     // ticket 0697: the hung sink ends the process as the proxy's Fatal(3) does
	char label[64] = "run";
	wchar_t summary[MAX_PATH] = L"";
	wchar_t logPath[MAX_PATH] = L"";
} O;

static HWND g_hwnd;
static ID3D11Device* g_dev;
static ID3D11DeviceContext* g_ctx;
static ID3D11DeviceContext* g_def;
static IDXGISwapChain* g_sc;
static ID3D11RenderTargetView* g_rtv;
static ID3D11VertexShader* g_vs;
static ID3D11PixelShader* g_ps;
static ID3D11Buffer* g_cb;
static ID3D11BlendState* g_bs;
static ID3D11RasterizerState* g_rs;
static ID3D11DepthStencilState* g_dss;
static IDXGIAdapter3* g_adapter3;
static UINT g_bbW, g_bbH;
static UINT g_bbSamples = 1;
static DXGI_FORMAT g_bbFormat = DXGI_FORMAT_UNKNOWN;
static ID3D11Texture2D* g_dsTex; static ID3D11DepthStencilView* g_dsv;
// ticket 0804 (--scene-msaa N, with --game): as the EXE of build 4390913 with MSAASamples = N, the swap chain stays
// single-sampled and the clear and units lists draw into an N-sampled target of the back buffer's size (the depth is
// N-sampled too), which the frame resolves into the back buffer before the UI list
static ID3D11Texture2D* g_sceneTex; static ID3D11RenderTargetView* g_sceneRtv;
static ID3D11RenderTargetView* SceneRtv() { return g_sceneRtv ? g_sceneRtv : g_rtv; }
static bool g_quit;
static void CreateDepth();
static IDXGIFactory2* g_factory;
static HMODULE g_dllModule;

static void CreateRtv()
{
	ID3D11Texture2D* bb = nullptr;
	HRESULT hr = g_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
	if (FAILED(hr)) { Log("GetBuffer failed 0x%08X", hr); return; }
	D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
	g_bbW = td.Width; g_bbH = td.Height; g_bbSamples = td.SampleDesc.Count; g_bbFormat = td.Format;
	hr = g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
	if (FAILED(hr)) Log("CreateRenderTargetView failed 0x%08X", hr);
	bb->Release();
}

static void ResizeBuffersTo(UINT w, UINT h, const char* why)
{
	if (!g_sc || w == 0 || h == 0) return;
	SafeRelease(g_rtv);
	if (O.game) { SafeRelease(g_dsv); SafeRelease(g_dsTex); SafeRelease(g_sceneRtv); SafeRelease(g_sceneTex); }
	g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
	g_ctx->ClearState();
	g_ctx->Flush();
	int64_t t0 = Qpc();
	HRESULT hr = g_sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
	Log("ResizeBuffers(%u x %u) [%s] -> 0x%08X (%.2f ms)", w, h, why, hr, QpcToMs(Qpc() - t0));
	CreateRtv();
	if (O.game) CreateDepth();
}

static void ToggleFullscreen()
{
	BOOL fs = FALSE; g_sc->GetFullscreenState(&fs, nullptr);
	HRESULT hr = g_sc->SetFullscreenState(!fs, nullptr);
	Log("SetFullscreenState(%d) -> 0x%08X", !fs, hr);
}

static void ResizeWindowClient(UINT w, UINT h)
{
	RECT r = { 0, 0, (LONG)w, (LONG)h };
	DWORD style = (DWORD)GetWindowLongW(g_hwnd, GWL_STYLE);
	AdjustWindowRect(&r, style, FALSE);
	SetWindowPos(g_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	Log("window client resized to %ux%u", w, h);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_SIZE:
		if (wp == SIZE_MINIMIZED) { Log("WM_SIZE minimized"); return 0; }
		if (g_sc && !O.bbW && h == g_hwnd)   // not for the --extra-chains windows
		{
			UINT w = LOWORD(lp), hh = HIWORD(lp);
			if (w && hh && (w != g_bbW || hh != g_bbH)) ResizeBuffersTo(w, hh, "WM_SIZE");
		}
		return 0;
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) g_quit = true;
		else if (wp == VK_F1)
		{
			static const UINT res[3][2] = { { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 } };
			static int i = 0; i = (i + 1) % 3;
			ResizeBuffersTo(res[i][0], res[i][1], "F1");
		}
		else if (wp == VK_F2) ToggleFullscreen();
		else if (wp == VK_F3) { static bool big = false; big = !big; ResizeWindowClient(big ? 1600 : 1024, big ? 900 : 640); }
		return 0;
	case WM_CLOSE: g_quit = true; return 0;
	case WM_DESTROY: if (h == g_hwnd) PostQuitMessage(0); return 0; // not for the old window of --recreate-at
	}
	return DefWindowProcW(h, m, wp, lp);
}

// Ticket 0697: the proxy's route for a hung server in a D33 session, as far as the composite sees it (proxy_rt.cpp
// ServerHungRoute -> Fatal(3) -> proxy_shell.cpp FlushCompositeTotals -> TerminateProcess). Called by the composite's
// control thread.
static void __cdecl FatalSink(const char* reason)
{
	Log("fatal-sink: ServerHung(\"%s\"): Fatal(3) as the proxy's", reason ? reason : "(null)");
	typedef int(__cdecl* FlushFn)(const char*, const char*, unsigned);
	FlushFn flush = g_dllModule ? (FlushFn)GetProcAddress(g_dllModule, "VPShell_FlushTotals") : nullptr;
	const int64_t t0 = Qpc();
	const int r = flush ? flush("fatal", "exit=3 stage=server-hung code=no-frame: test_host --fatal-sink", 1000) : -1;
	Log("fatal-sink: VPShell_FlushTotals %s %d in %.1f ms", flush ? "returned" : "missing:", r, QpcToMs(Qpc() - t0));
	// a second Fatal's call (once per process: returns 0 at once, writes nothing)
	const int64_t t1 = Qpc();
	const int r2 = flush ? flush("fatal", "exit=3: the second call", 1000) : -1;
	Log("fatal-sink: the second VPShell_FlushTotals returned %d in %.1f ms; TerminateProcess(3)", r2, QpcToMs(Qpc() - t1));
	if (g_log) fflush(g_log);
	TerminateProcess(GetCurrentProcess(), 3);
}

static void LoadCompositeDll(const char* when)
{
	if (O.load == 2 || g_dllModule) return;
	int64_t t0 = Qpc();
	g_dllModule = LoadLibraryW(O.dll);
	Log("LoadLibraryW('%ls') %s -> %p (err %u) in %.2f ms", O.dll, when, g_dllModule, g_dllModule ? 0 : GetLastError(), QpcToMs(Qpc() - t0));
	if (O.fatalSink && g_dllModule)   // ticket 0697
	{
		typedef void(__cdecl* SetSinkFn)(void(__cdecl*)(const char*));
		SetSinkFn set = (SetSinkFn)GetProcAddress(g_dllModule, "VPShell_SetHungSink");
		if (set) set(&FatalSink);
		Log("fatal-sink: %s", set ? "passed to the composite" : "the composite has no VPShell_SetHungSink");
	}
}

static void LogMemory(const char* tag)
{
	PROCESS_MEMORY_COUNTERS_EX pmc = { sizeof(pmc) };
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	unsigned long long vid = 0;
	if (g_adapter3)
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO vi = {};
		if (SUCCEEDED(g_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi))) vid = vi.CurrentUsage;
	}
	Log("mem %s: private=%.2f MB working=%.2f MB video_local=%.2f MB handles=%u", tag,
		pmc.PrivateUsage / 1048576.0, pmc.WorkingSetSize / 1048576.0, vid / 1048576.0, [] { DWORD n = 0; GetProcessHandleCount(GetCurrentProcess(), &n); return n; }());
}

static void BuildUi(HostCB& cb, float t, int64_t frame)
{
	float W = (float)g_bbW, H = (float)g_bbH;
	int n = 0;
	auto add = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a, float style, float seed) {
		if (n >= 64) return;
		UiRect& u = cb.rects[n++];
		u.rect[0] = x0; u.rect[1] = y0; u.rect[2] = x1; u.rect[3] = y1;
		u.color[0] = r; u.color[1] = g; u.color[2] = b; u.color[3] = a;
		u.style[0] = style; u.style[1] = seed; u.style[2] = u.style[3] = 0;
	};
	// top bar (opaque) with text noise and a host-frame ticker
	add(0, 0, W, 36, 0.18f, 0.20f, 0.28f, 1.0f, 0, 0);
	add(10, 10, W - 420, 34, 1.0f, 0.85f, 0.4f, 1.0f, 1, 1);
	float tick = fmodf((float)frame * 2.0f, 300.0f);
	add(W - 400 + tick, 6, W - 392 + tick, 30, 0.2f, 1.0f, 0.3f, 1.0f, 0, 0);
	// left panel (translucent) with text noise
	add(20, 60, 380, H - 60, 0.03f, 0.03f, 0.06f, 0.70f, 0, 0);
	add(32, 76, 370, H - 76, 0.95f, 0.95f, 0.95f, 1.0f, 1, 2);
	// minimap box bottom right (opaque)
	add(W - 320, H - 240, W - 20, H - 20, 0.08f, 0.08f, 0.08f, 1.0f, 0, 0);
	add(W - 310, H - 230, W - 30, H - 30, 0.25f, 0.45f, 0.25f, 1.0f, 0, 0);
	// centre dialog (half transparent): the composite must show through it
	add(W * 0.5f - 260, H * 0.5f - 130, W * 0.5f + 260, H * 0.5f + 130, 0.35f, 0.28f, 0.10f, 0.55f, 0, 0);
	add(W * 0.5f - 240, H * 0.5f - 110, W * 0.5f + 240, H * 0.5f + 110, 1.0f, 1.0f, 0.8f, 0.9f, 1, 3);
	// bottom bar (opaque)
	add(0, H - 28, W - 340, H, 0.18f, 0.20f, 0.28f, 1.0f, 0, 0);
	add(8, H - 22, W - 360, H - 4, 0.7f, 0.9f, 1.0f, 1.0f, 1, 4);
	cb.vp[0] = W; cb.vp[1] = H; cb.time = t; cb.pad = (float)n;
}

// ===========================================================================
// Game-like frame (issue 3006): lists recorded on deferred contexts, executed with ExecuteCommandList
// ===========================================================================
enum { L_CLEAR, L_TERRAIN, L_UNITS, L_UI, L_MISC0, L_MISC1, L_MISC2, L_COUNT };
static const char* kListNames[L_COUNT] = { "clear", "terrain", "units", "ui", "misc", "misc", "misc" };
struct GameList { ID3D11DeviceContext* def; ID3D11Buffer* cb; ID3D11CommandList* cl; HANDLE go, done, thread; };
static GameList g_gl[L_COUNT];
static ID3D11Texture2D* g_terrTex; static ID3D11RenderTargetView* g_terrRtv;
static ID3D11Texture2D* g_terrDsTex; static ID3D11DepthStencilView* g_terrDsv;
static ID3D11Texture2D* g_miscTex; static ID3D11RenderTargetView* g_miscRtv;
static ID3D11DepthStencilState* g_dssUnits;
static ID3D11BlendState* g_bsOpaque;
static float g_gameTime; static int64_t g_gameFrame;
static volatile bool g_gameQuit;
static int g_baseOrder[8]; static int g_nBase;
static unsigned g_rng = 1;
static unsigned Rand() { g_rng = g_rng * 1103515245u + 12345u; return (g_rng >> 16) & 0x7FFF; }

static void CreateDepth()
{
	SafeRelease(g_dsv); SafeRelease(g_dsTex); SafeRelease(g_sceneRtv); SafeRelease(g_sceneTex);
	const UINT samples = O.sceneMsaa >= 1 ? O.sceneMsaa : g_bbSamples;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = g_bbW; td.Height = g_bbH; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	td.SampleDesc.Count = samples; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	HRESULT hr = g_dev->CreateTexture2D(&td, nullptr, &g_dsTex);
	if (SUCCEEDED(hr)) hr = g_dev->CreateDepthStencilView(g_dsTex, nullptr, &g_dsv);
	if (FAILED(hr)) Log("depth buffer creation failed 0x%08X", hr);
	if (O.sceneMsaa >= 1)
	{
		D3D11_TEXTURE2D_DESC sd = td;
		sd.Format = g_bbFormat; sd.BindFlags = D3D11_BIND_RENDER_TARGET;
		hr = g_dev->CreateTexture2D(&sd, nullptr, &g_sceneTex);
		if (SUCCEEDED(hr)) hr = g_dev->CreateRenderTargetView(g_sceneTex, nullptr, &g_sceneRtv);
		Log("scene target: %u-sample %ux%u format %u, resolved into the back buffer before the UI list (ticket 0804) -> 0x%08X", samples, g_bbW, g_bbH, g_bbFormat, hr);
	}
}

static bool GameInit()
{
	HRESULT hr;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = 512; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
	td.Format = DXGI_FORMAT_R11G11B10_FLOAT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(hr = g_dev->CreateTexture2D(&td, nullptr, &g_terrTex)) || FAILED(hr = g_dev->CreateRenderTargetView(g_terrTex, nullptr, &g_terrRtv))) { Log("terrain RT failed 0x%08X", hr); return false; }
	td.Format = DXGI_FORMAT_D32_FLOAT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(hr = g_dev->CreateTexture2D(&td, nullptr, &g_terrDsTex)) || FAILED(hr = g_dev->CreateDepthStencilView(g_terrDsTex, nullptr, &g_terrDsv))) { Log("terrain DSV failed 0x%08X", hr); return false; }
	td.Width = td.Height = 64; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (FAILED(hr = g_dev->CreateTexture2D(&td, nullptr, &g_miscTex)) || FAILED(hr = g_dev->CreateRenderTargetView(g_miscTex, nullptr, &g_miscRtv))) { Log("misc RT failed 0x%08X", hr); return false; }
	D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
	if (FAILED(hr = g_dev->CreateDepthStencilState(&dd, &g_dssUnits))) return false;
	D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(hr = g_dev->CreateBlendState(&bd, &g_bsOpaque))) return false;
	CreateDepth();
	for (int i = 0; i < L_COUNT; ++i)
	{
		if (FAILED(hr = g_dev->CreateDeferredContext(0, &g_gl[i].def))) { Log("CreateDeferredContext failed 0x%08X", hr); return false; }
		D3D11_BUFFER_DESC b = { sizeof(HostCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		if (FAILED(hr = g_dev->CreateBuffer(&b, nullptr, &g_gl[i].cb))) return false;
	}
	// base order
	wchar_t buf[128]; wcsncpy_s(buf, O.order, _TRUNCATE);
	wchar_t* ctx = nullptr;
	for (wchar_t* t = wcstok_s(buf, L",", &ctx); t && g_nBase < 8; t = wcstok_s(nullptr, L",", &ctx))
	{
		int k = !wcscmp(t, L"clear") ? L_CLEAR : !wcscmp(t, L"terrain") ? L_TERRAIN : !wcscmp(t, L"units") ? L_UNITS : !wcscmp(t, L"ui") ? L_UI : -1;
		if (k >= 0) g_baseOrder[g_nBase++] = k;
	}
	g_rng = O.seed;
	Log("game mode: %d deferred contexts, order '%ls', vary %d, mt %d, seed %u", L_COUNT, O.order, O.vary, O.mt, O.seed);
	return true;
}

static void RecordList(int i)
{
	GameList& L = g_gl[i];
	ID3D11DeviceContext* c = L.def;
	static const FLOAT grey[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
	static const FLOAT dark[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	static const FLOAT bf[4] = {};
	HostCB cb = {};
	int n = 0;
	float W = (float)g_bbW, H = (float)g_bbH;
	auto add = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a, float z) {
		if (n >= 64) return;
		UiRect& u = cb.rects[n++];
		u.rect[0] = x0; u.rect[1] = y0; u.rect[2] = x1; u.rect[3] = y1;
		u.color[0] = r; u.color[1] = g; u.color[2] = b; u.color[3] = a;
		u.style[0] = 0; u.style[1] = 0; u.style[2] = z; u.style[3] = 0;
	};
	ID3D11DepthStencilView* drawDsv = nullptr;
	ID3D11RenderTargetView* drawRtv = nullptr;
	ID3D11BlendState* bs = g_bs;
	ID3D11DepthStencilState* dss = g_dss;
	switch (i)
	{
	case L_CLEAR:
	{
		ID3D11RenderTargetView* sc = SceneRtv();   // ticket 0804: the N-sampled scene target with --scene-msaa
		c->OMSetRenderTargets(1, &sc, g_dsv);
		c->ClearRenderTargetView(sc, grey);
		c->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		break;
	}
	case L_TERRAIN:
		c->OMSetRenderTargets(1, &g_terrRtv, g_terrDsv);
		c->ClearRenderTargetView(g_terrRtv, dark);
		c->ClearDepthStencilView(g_terrDsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
		W = H = 512;
		add(32, 32, 480, 480, 0.8f, 0.1f, 0.1f, 1.0f, 0.5f);
		drawRtv = g_terrRtv; drawDsv = g_terrDsv; dss = g_dssUnits; bs = g_bsOpaque;
		break;
	case L_UNITS:
		drawRtv = SceneRtv();
		c->OMSetRenderTargets(1, &drawRtv, g_dsv);
		add(520, 540, 640, 640, 0.0f, 1.0f, 1.0f, 1.0f, 0.6f);   // cyan unit, drawn first
		add(420, 510, 560, 600, 1.0f, 1.0f, 0.0f, 1.0f, 0.8f);   // yellow landmark, behind the cyan unit
		add(600, 560, 700, 660, 1.0f, 0.5f, 0.0f, 1.0f, 0.3f);   // orange unit, in front of the cyan unit
		drawDsv = g_dsv; dss = g_dssUnits; bs = g_bsOpaque;
		break;
	case L_UI:
		BuildUi(cb, g_gameTime, g_gameFrame);
		n = (int)cb.pad;
		c->OMSetRenderTargets(1, &g_rtv, nullptr);
		drawRtv = g_rtv;
		break;
	default:
		c->OMSetRenderTargets(1, &g_miscRtv, nullptr);
		c->ClearRenderTargetView(g_miscRtv, dark);
		break;
	}
	if (n)
	{
		cb.vp[0] = W; cb.vp[1] = H; cb.time = g_gameTime; cb.pad = (float)n;
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(c->Map(L.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cb, sizeof(cb)); c->Unmap(L.cb, 0); }
		D3D11_VIEWPORT vp = { 0, 0, W, H, 0, 1 };
		c->RSSetViewports(1, &vp);
		c->RSSetState(g_rs);
		c->OMSetBlendState(bs, bf, 0xFFFFFFFF);
		c->OMSetDepthStencilState(dss, 0);
		c->IASetInputLayout(nullptr);
		c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		c->VSSetShader(g_vs, nullptr, 0);
		c->VSSetConstantBuffers(0, 1, &L.cb);
		c->PSSetShader(g_ps, nullptr, 0);
		c->PSSetConstantBuffers(0, 1, &L.cb);
		c->DrawInstanced(6, n, 0, 0);
	}
	if (FAILED(c->FinishCommandList(FALSE, &L.cl))) L.cl = nullptr;
}

static DWORD WINAPI ListWorker(LPVOID p)
{
	int i = (int)(intptr_t)p;
	for (;;)
	{
		WaitForSingleObject(g_gl[i].go, INFINITE);
		if (g_gameQuit) break;
		RecordList(i);
		SetEvent(g_gl[i].done);
	}
	return 0;
}

static void GameStartWorkers()
{
	if (!O.mt) return;
	for (int i = L_CLEAR; i <= L_UI; ++i)
	{
		g_gl[i].go = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		g_gl[i].done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		g_gl[i].thread = CreateThread(nullptr, 0, ListWorker, (LPVOID)(intptr_t)i, 0, nullptr);
	}
}

static void GameStopWorkers()
{
	if (!O.mt) return;
	g_gameQuit = true;
	for (int i = L_CLEAR; i <= L_UI; ++i) if (g_gl[i].thread) { SetEvent(g_gl[i].go); WaitForSingleObject(g_gl[i].thread, 2000); CloseHandle(g_gl[i].thread); }
}

static void GameFrame(float t, int64_t frame)
{
	g_gameTime = t; g_gameFrame = frame;
	int ord[16]; int n = 0;
	for (int i = 0; i < g_nBase; ++i) ord[n++] = g_baseOrder[i];
	if (O.vary)
	{
		// terrain anywhere before the units list
		int ti = -1, ui = -1;
		for (int i = 0; i < n; ++i) { if (ord[i] == L_TERRAIN) ti = i; }
		if (ti >= 0)
		{
			for (int i = ti; i + 1 < n; ++i) ord[i] = ord[i + 1];
			n--;
			for (int i = 0; i < n; ++i) if (ord[i] == L_UNITS) ui = i;
			int pos = (int)(Rand() % (unsigned)((ui >= 0 ? ui : n) + 1));
			for (int i = n; i > pos; --i) ord[i] = ord[i - 1];
			ord[pos] = L_TERRAIN; n++;
		}
		int k = (int)(Rand() % 4);
		for (int j = 0; j < k && j < 3; ++j)
		{
			int pos = (int)(Rand() % (unsigned)(n + 1));
			for (int i = n; i > pos; --i) ord[i] = ord[i - 1];
			ord[pos] = L_MISC0 + j; n++;
		}
	}
	bool used[L_COUNT] = {};
	for (int i = 0; i < n; ++i) used[ord[i]] = true;
	if (O.mt)
	{
		HANDLE wait[4]; int nw = 0;
		for (int i = L_CLEAR; i <= L_UI; ++i) if (used[i]) { SetEvent(g_gl[i].go); wait[nw++] = g_gl[i].done; }
		for (int i = L_MISC0; i < L_COUNT; ++i) if (used[i]) RecordList(i);
		if (nw) WaitForMultipleObjects(nw, wait, TRUE, INFINITE);
	}
	else
		for (int i = 0; i < L_COUNT; ++i) if (used[i]) RecordList(i);
	bool resolved = false;
	auto resolve = [&]() {   // ticket 0804: the scene target into the back buffer, once per frame, before the UI list
		if (resolved || !g_sceneTex || !g_rtv) return;
		resolved = true;
		ID3D11Resource* bb = nullptr; g_rtv->GetResource(&bb);
		if (bb) { if (O.sceneMsaa > 1) g_ctx->ResolveSubresource(bb, 0, g_sceneTex, 0, g_bbFormat); else g_ctx->CopyResource(bb, g_sceneTex); bb->Release(); }
	};
	for (int i = 0; i < n; ++i)
	{
		if (ord[i] == L_UI) resolve();
		ID3D11CommandList*& cl = g_gl[ord[i]].cl;
		if (cl) { g_ctx->ExecuteCommandList(cl, FALSE); cl->Release(); cl = nullptr; }
	}
	resolve();
	if (frame < 3 || (frame % 600) == 0)
	{
		char s[256] = "";
		for (int i = 0; i < n; ++i) { strcat_s(s, kListNames[ord[i]]); if (i + 1 < n) strcat_s(s, ","); }
		Log("game frame %lld order %s", frame, s);
	}
}

static void GameRelease()
{
	GameStopWorkers();
	for (auto& L : g_gl) { SafeRelease(L.cl); SafeRelease(L.cb); SafeRelease(L.def); if (L.go) CloseHandle(L.go); if (L.done) CloseHandle(L.done); }
	SafeRelease(g_dsv); SafeRelease(g_dsTex); SafeRelease(g_terrRtv); SafeRelease(g_terrTex); SafeRelease(g_terrDsv); SafeRelease(g_terrDsTex);
	SafeRelease(g_miscRtv); SafeRelease(g_miscTex); SafeRelease(g_dssUnits); SafeRelease(g_bsOpaque);
}

// ---------------------------------------------------------------------------------------------------------------
// Fake EXE camera (issue 3007, --fake-exe). A reserved region laid out like CivilizationV_DX11.exe loaded at that
// base: a PE header with the EXE's TimeDateStamp, the byte patterns of exe_camera.h at their RVAs (relocated
// immediates rebased on this base), the camera globals, a Civ5App and a GameViewState on the heap, and the grid
// constants of the Tiny ground-truth map (56 x 36, D:/c5-64bit-work/i2-ground-truth/tiny, read live 2026-11-11). Its
// base goes to the DLL in VPSHELL_EXE_BASE (set in this process before the DLL loads). The camera looks down at 45 deg
// from 600 units like the game's default zoom; it pans, freezes, changes view mode or is written from another thread on
// request, so the DLL's read path, wrap views, stale detection and kill switch can be tested without the game.
struct FakeOpts
{
	bool on = false;
	double x = 0, y = 0, pan = 0;   // camera target (world) and pan speed (units/s, +x)
	int views = 1;                  // g_NumViews: 2 = one wrap copy (toward the nearer seam), 3 = both
	bool writerThread = false;      // write the camera from a separate thread, as fast as possible (torn reads)
	double freezeAt = 0;            // from this time on the view globals stop updating while camWorld keeps moving
	double modeAt = 0; int mode = 2;// at this time GVS+0x758 becomes `mode` (2 = Strategic View)
	int bad = 0;                    // 1 = wrong TimeDateStamp, 2 = one pattern byte wrong, 3 = no MZ, 4 = e_lfanew too large,
	                                // 5 = the build's stamp with another SizeOfImage (ticket 4964)
	int exeBuild = kVpExeBuild4390913;   // ticket 4964: --fake-exe-build
	int camBad = 0;                 // issue 3013, --fake-cam-bad (see kCamBadNames): one broken piece of the camera state
	bool uv = false;                // issue 3304: --fake-uv
	int uvEvery = 1;                // --fake-uv-every
	double uvWorkUs = 0;            // --uv-work-us
	bool moveAfterUv = false;       // --fake-cam-move-after-uv
	bool uvThread = false;          // --fake-uv-thread
	wchar_t depthDump[MAX_PATH] = L"";   // --depth-dump
	double depthDumpAt = 3.0;       // --depth-dump-at
	wchar_t depthDumpOn[MAX_PATH] = L"";   // --depth-dump-on (0494)
} FO;
// --fake-cam-bad (issue 3013): what the DLL must report for each (shell/tests/test_errors.py)
enum { CB_NONE, CB_NULL_APP, CB_BAD_VPTR, CB_UNMAPPED_APP, CB_VIEWS0, CB_VIEWS9, CB_WRAP, CB_GRID_NOT_READY, CB_COUNT };
static const wchar_t* kCamBadNames[CB_COUNT] = { L"none", L"null-app", L"bad-vptr", L"unmapped-app", L"views0", L"views9", L"wrap", L"grid-not-ready" };

static uint8_t* g_fakeBase;
static uint8_t* g_fakeApp;
static uint8_t* g_fakeGvs;
static volatile bool g_fakeQuit;
static HANDLE g_fakeThread;
static int64_t g_fakeStart;

static inline uint8_t* FA(uint32_t va) { return g_fakeBase + (va - VPEXE_IMAGE_BASE); }

static bool FakeCommit(uint32_t va, size_t n)
{
	uintptr_t a = (uintptr_t)FA(va) & ~(uintptr_t)0xFFF;
	uintptr_t e = ((uintptr_t)FA(va) + n + 0xFFF) & ~(uintptr_t)0xFFF;
	return VirtualAlloc((void*)a, e - a, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

template <class T> static void FW(uintptr_t a, T v) { *(volatile T*)a = v; }

static void FakeCamWrite(double el)
{
	bool views = !(FO.freezeAt > 0 && el >= FO.freezeAt);
	double tx = FO.x + FO.pan * el, ty = FO.y;
	const double dist = 600.0, c = cos(3.14159265358979 / 4), s = sin(3.14159265358979 / 4);
	float cw[16] = { 1, 0, 0, 0,   0, (float)c, (float)s, 0,   0, (float)-s, (float)c, 0,   (float)tx, (float)(ty - dist * s), (float)(dist * c), 1 };
	const float fov = 29.0145f, nz = 50.0f, fz = 4000.0f;
	float aspect = g_bbH ? (float)g_bbW / (float)g_bbH : 16.0f / 9.0f;
	uintptr_t gvs = (uintptr_t)g_fakeGvs;
	memcpy((void*)(gvs + VPCAM_GVS_CAMWORLD), cw, 64);
	FW<float>(gvs + VPCAM_GVS_FOV, fov); FW<float>(gvs + VPCAM_GVS_ASPECT, aspect);
	FW<float>(gvs + VPCAM_GVS_NEAR, nz); FW<float>(gvs + VPCAM_GVS_FAR, fz);
	if (FO.modeAt > 0 && el >= FO.modeAt) FW<uint32_t>(gvs + VPCAM_GVS_VIEWMODE, (uint32_t)FO.mode);
	if (!views) return;
	// view 0 = inverse(camWorld) (rigid: transposed rotation, -p R^T)
	float v[16] = {};
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) v[i * 4 + j] = cw[j * 4 + i];
	for (int j = 0; j < 3; ++j) v[12 + j] = -(cw[12] * cw[j * 4 + 0] + cw[13] * cw[j * 4 + 1] + cw[14] * cw[j * 4 + 2]);
	v[15] = 1;
	double ys = 1.0 / tan(fov * 3.14159265358979 / 360.0);
	float p[16] = { (float)(ys / aspect), 0, 0, 0,   0, (float)ys, 0, 0,   0, 0, fz / (nz - fz), -1,   0, 0, nz * fz / (nz - fz), 0 };
	memcpy(FA(VPCAM_VA_PROJ), p, 64);
	memcpy(FA(VPCAM_VA_VIEW), v, 64);
	float period = *(float*)FA(VPCAM_VA_PERIOD);
	int n = 1;
	const int nv = FO.camBad == CB_WRAP ? std::max(2, FO.views) : FO.views;
	for (int k = 1; k < nv && k < 3; ++k)
	{
		double sign = (nv == 2) ? (tx >= 0 ? 1.0 : -1.0) : (k == 1 ? 1.0 : -1.0);
		float w[16]; memcpy(w, v, 64);
		for (int cc = 0; cc < 4; ++cc) w[12 + cc] = v[12 + cc] + (float)(sign * period) * v[cc];
		if (FO.camBad == CB_WRAP) w[12] += 50.0f;   // not a +-period copy of view 0
		memcpy(FA(VPCAM_VA_VIEW) + 64 * n, w, 64);
		n++;
	}
	if (FO.camBad == CB_VIEWS0) n = 0;
	if (FO.camBad == CB_VIEWS9) n = 9;
	FW<uint32_t>((uintptr_t)FA(VPCAM_VA_NUMVIEWS), (uint32_t)n);
	uint32_t vp[4] = { 0, 0, g_bbW, g_bbH }; float vz[2] = { 0, 1 };
	memcpy(FA(VPCAM_VA_VIEWPORT), vp, 16); memcpy(FA(VPCAM_VA_VIEWPORT) + 16, vz, 8);
}

static DWORD WINAPI FakeWriter(LPVOID)
{
	uint64_t n = 0;
	while (!g_fakeQuit) { FakeCamWrite(QpcToMs(Qpc() - g_fakeStart) / 1000.0); if ((++n & 1023) == 0) Sleep(0); }
	Log("fake EXE: writer thread wrote the camera %llu times", n);
	return 0;
}

static bool FakeExeInit()
{
	// Ticket 4964: the image is laid out as the build --fake-exe-build names, every address from the build table.
	const int build = FO.exeBuild;
	VPCamSetBuild(build);   // this module's VPCAM_VA_* (the DLL sets its own from the header it validates)
	uint32_t end = std::max<uint32_t>(VPCAM_VA_VIEWPORT + 24, VpExeSigsEnd(build, kVPCamSigs, kVPCamNumSigs));
	const int lsSyms[] = { VPX_ls_loop, VPX_ls_thunk, VPX_ls_iat_matinv, VPX_ls_invview0 };
	for (int s : lsSyms) end = std::max<uint32_t>(end, VpExeVa(build, s) + 0x100);
	if (!VPCAM_VA_VIEWPORT || !VpExeSigsEnd(build, kVPCamSigs, kVPCamNumSigs))
	{ Log("fake EXE: %s does not carry every camera symbol (the build table compiled in is incomplete)", VpExeBuildLabel(build)); return false; }
	const size_t span = (end + 0x1000) - VPEXE_IMAGE_BASE;
	g_fakeBase = (uint8_t*)VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS);
	if (!g_fakeBase) { Log("fake EXE: reserving %zu bytes failed %u", span, GetLastError()); return false; }
	bool ok = FakeCommit(VPEXE_IMAGE_BASE, 0x1000);
	for (int i = 0; i < kVPCamNumSigs; ++i) ok = ok && FakeCommit(VpExeVa(build, kVPCamSigs[i].sym), 96);
	ok = ok && FakeCommit(VPCAM_VA_APP, 4) && FakeCommit(VPCAM_VA_NUMVIEWS, 0x400) && FakeCommit(VPCAM_VA_PROJ, 0x140) && FakeCommit(VPCAM_VA_VIEWPORT, 24);
	ok = ok && FakeCommit(VPCAM_VA_WRAPX, 4) && FakeCommit(VPCAM_VA_GRIDPTR, 4) && FakeCommit(VPCAM_VA_AABB, 24) && FakeCommit(VPCAM_VA_PERIOD, 4);
	ok = ok && FakeCommit(VPCAM_VA_WORLDH, 4) && FakeCommit(VPCAM_VA_W2H, 64) && FakeCommit(VPCAM_VA_H2W, 64) && FakeCommit(VPCAM_VA_GRID, 0x100);
	ok = ok && FakeCommit(VPCAM_VA_VIEW, 0x100) && FakeCommit(VPCAM_VA_FCAM_VTBL, 12);
	if (!ok) { Log("fake EXE: commit failed %u", GetLastError()); return false; }
	uint8_t* b = g_fakeBase;
	VpExeFakeHeader(b, build);
	if (FO.bad == 3) b[0] = 'X';
	if (FO.bad == 4) *(uint32_t*)(b + 0x3C) = 0x2000;
	if (FO.bad == 1) *(uint32_t*)(b + 0x88) += 1;          // a stamp of no known build
	if (FO.bad == 5) *(uint32_t*)(b + 0x98 + 56) += 0x1000; // the build's stamp, another SizeOfImage
	uint32_t base = (uint32_t)(uintptr_t)g_fakeBase;
	if (!VpExeLaySigs(build, base, g_fakeBase, (uint32_t)span, kVPCamSigs, kVPCamNumSigs, -1)) { Log("fake EXE: a camera pattern does not expand for %s", VpExeBuildLabel(build)); return false; }
	if (FO.bad == 2) FA(VpExeVa(build, kVPCamSigs[3].sym))[1] ^= 0x01;   // a relocated immediate that points elsewhere
	g_fakeApp = (uint8_t*)calloc(1, 0x200);
	g_fakeGvs = (uint8_t*)calloc(1, VPCAM_GVS_BYTES);
	*(uint32_t*)(g_fakeApp + VPCAM_APP_GVS) = (uint32_t)(uintptr_t)g_fakeGvs;
	*(uint32_t*)FA(VPCAM_VA_APP) = (uint32_t)(uintptr_t)g_fakeApp;
	*(uint32_t*)(g_fakeGvs + VPCAM_GVS_FCAMERA) = (uint32_t)(uintptr_t)FA(VPCAM_VA_FCAM_VTBL);
	*(uint32_t*)(g_fakeGvs + VPCAM_GVS_VIEWMODE) = 1;
	// issue 3013: broken camera state the DLL must report (never read as OK)
	if (FO.camBad == CB_NULL_APP) *(uint32_t*)FA(VPCAM_VA_APP) = 0;                         // NO_GVS
	if (FO.camBad == CB_UNMAPPED_APP) *(uint32_t*)FA(VPCAM_VA_APP) = 0x10;                  // app+0xC0 = 0xD0: access violation
	if (FO.camBad == CB_BAD_VPTR) *(uint32_t*)(g_fakeGvs + VPCAM_GVS_FCAMERA) = (uint32_t)(uintptr_t)FA(VPCAM_VA_PROJ); // BAD_GVS
	// grid constants of the Tiny ground-truth map (56 x 36 plots, wraps)
	static const float h2w[16] = { 63.9986f, 0.000938416f, 0, 0,  32.0f, 64.0019f, 0, 0,  0, 0, 0, 0,  32.0007f, 128.0033f, 0, 1 };
	static const float w2h[16] = { 0.0156255f, -2.29105e-7f, -0.0156252f, 0,  -0.0078125f, 0.0156247f, -0.00781216f, 0,  0, 0, 0, 0,  0.5f, -2.0f, 1.5f, 1 };
	*(int32_t*)FA(VPCAM_VA_WRAPX) = 1;
	float aabb[6] = { -1791.5f, -1108.047f, 0, 1791.5f, 1108.047f, 255 };
	memcpy(FA(VPCAM_VA_AABB), aabb, 24);
	*(float*)FA(VPCAM_VA_PERIOD) = 3583.0f;
	*(float*)FA(VPCAM_VA_WORLDH) = 2216.094f;
	memcpy(FA(VPCAM_VA_W2H), w2h, 64);
	memcpy(FA(VPCAM_VA_H2W), h2w, 64);
	uint8_t* grid = FA(VPCAM_VA_GRID);
	grid[VPCAM_GRID_READY] = FO.camBad == CB_GRID_NOT_READY ? 0 : 1;
	*(uint32_t*)(grid + VPCAM_GRID_SAMPLES_X) = 3584; *(uint32_t*)(grid + VPCAM_GRID_SAMPLES_Y) = 2560;
	float sc[5] = { 1.0f, 0.866f, 0.2f, 1792.0f, 1108.48f };
	memcpy(grid + VPCAM_GRID_SX, sc, 20);
	*(uint32_t*)FA(VPCAM_VA_GRIDPTR) = (uint32_t)(uintptr_t)grid;
	g_fakeStart = Qpc();
	FakeCamWrite(0);
	wchar_t hex[16]; _snwprintf_s(hex, _TRUNCATE, L"%08X", base);
	SetEnvironmentVariableW(L"VPSHELL_EXE_BASE", hex);
	Log("fake EXE: %s image at 0x%08X (%zu bytes reserved), camera target (%.1f, %.1f) pan %.1f/s views %d writer %s freeze-at %.1f mode-at %.1f:%d bad %d",
		VpExeBuildLabel(build), base, span, FO.x, FO.y, FO.pan, FO.views, FO.writerThread ? "thread" : "frame", FO.freezeAt, FO.modeAt, FO.mode, FO.bad);
	if (FO.writerThread) g_fakeThread = CreateThread(nullptr, 0, FakeWriter, nullptr, 0, nullptr);
	return true;
}

static bool FakeUvInit();   // issue 3304, below

// --fake-uv (issue 3304): UpdateViews' inverse-loop call site, the import thunk and the import slot, laid out in the fake
// image as the composite's hook validation (LsValidate) expects them, and made executable. Returns the stub to call per
// frame, or nullptr (logged).
typedef void (__cdecl* FakeUvFn)();
static FakeUvFn g_fakeUv;
static bool FakeUvInit()
{
	// Ticket 4964: the build's addresses (ls_* and cam_view of the build table); the stub starts 12 bytes before the lea.
	const int build = FO.exeBuild;
	const uint32_t kUvLea = VpExeVa(build, VPX_ls_loop), kUvStub = kUvLea - 12, kUvRet = VpExeVa(build, VPX_ls_ret);
	const uint32_t kUvThunk = VpExeVa(build, VPX_ls_thunk), kUvIat = VpExeVa(build, VPX_ls_iat_matinv);
	const uint32_t kViewVa = VPCAM_VA_OF(build, cam_view), kInvViewVa = VpExeVa(build, VPX_ls_invview0);
	// The DLL the build's import slot names: build 4390913 imports d3dx9_42.dll, 26577625 d3dx9_43.dll (the 2026-11-19
	// update; exe-build-table.md section 3). The composite hooks whatever module the slot points into.
	const wchar_t* dxName = build == kVpExeBuild26577625 ? L"d3dx9_43.dll" : L"d3dx9_42.dll";
	HMODULE dx = LoadLibraryW(dxName);   // the June 2010 runtime the game ships with; system32/SysWOW64 copy
	void* inv = dx ? (void*)GetProcAddress(dx, "D3DXMatrixInverse") : nullptr;
	if (!inv) { Log("fake EXE: --fake-uv needs %S!D3DXMatrixInverse (module %p, error %u)", dxName, dx, GetLastError()); return false; }
	if (!FakeCommit(kUvStub, 0x20) || !FakeCommit(kUvThunk, 8) || !FakeCommit(kUvIat, 4) || !FakeCommit(kInvViewVa, 0x100))
	{ Log("fake EXE: --fake-uv commit failed %u", GetLastError()); return false; }
	uint8_t* s = FA(kUvStub);
	uint32_t view = (uint32_t)(uintptr_t)FA(kViewVa), invv = (uint32_t)(uintptr_t)FA(kInvViewVa);
	int32_t rel = (int32_t)(kUvThunk - kUvRet);
	uint8_t code[26] = { 0x56, 0x31, 0xF6, 0x8D, 0x8E, 0, 0, 0, 0, 0x51, 0x6A, 0x00,      // push esi; xor esi,esi; lea ecx,[esi+view]; push ecx; push 0
		0x8D, 0x86, 0, 0, 0, 0, 0x50, 0xE8, 0, 0, 0, 0, 0x5E, 0xC3 };                         // lea eax,[esi+invview]; push eax; call thunk; pop esi; ret
	memcpy(code + 5, &view, 4); memcpy(code + 14, &invv, 4); memcpy(code + 20, &rel, 4);
	memcpy(s, code, sizeof(code));
	uint8_t* t = FA(kUvThunk);
	uint32_t iat = (uint32_t)(uintptr_t)FA(kUvIat);
	t[0] = 0xFF; t[1] = 0x25; memcpy(t + 2, &iat, 4);
	*(void**)FA(kUvIat) = inv;
	DWORD old = 0;
	if (!VirtualProtect(s, 0x20, PAGE_EXECUTE_READWRITE, &old) || !VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old))
	{ Log("fake EXE: VirtualProtect failed %u", GetLastError()); return false; }
	FlushInstructionCache(GetCurrentProcess(), s, 0x20);
	FlushInstructionCache(GetCurrentProcess(), t, 8);
	g_fakeUv = (FakeUvFn)(void*)s;
	Log("fake EXE: fake UpdateViews at 0x%08X (lea at 0x%08X, return 0x%08X), thunk 0x%08X -> [0x%08X] = %S!D3DXMatrixInverse %p, every %d frame(s), work %.0f us%s",
		(uint32_t)(uintptr_t)s, (uint32_t)(uintptr_t)FA(kUvLea), (uint32_t)(uintptr_t)FA(kUvRet), (uint32_t)(uintptr_t)t, iat, dxName, inv, FO.uvEvery,
		FO.uvWorkUs, FO.moveAfterUv ? ", view 0 moved after it" : "");
	return true;
}

// --fake-uv-thread: the fake UpdateViews runs on another thread (the main thread waits for it): the composite must refuse
// to publish there (it publishes at Present instead).
static HANDLE g_uvGo, g_uvDone, g_uvThread;
static volatile bool g_uvQuit;
static DWORD WINAPI UvWorker(LPVOID)
{
	for (;;)
	{
		WaitForSingleObject(g_uvGo, INFINITE);
		if (g_uvQuit) return 0;
		g_fakeUv();
		SetEvent(g_uvDone);
	}
}

static void FakeUvFrame(int64_t frame)
{
	if (!g_fakeUv || (FO.uvEvery > 1 && frame % FO.uvEvery)) return;
	if (FO.uvThread)
	{
		if (!g_uvThread)
		{
			g_uvGo = CreateEventW(nullptr, FALSE, FALSE, nullptr); g_uvDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			g_uvThread = CreateThread(nullptr, 0, UvWorker, nullptr, 0, nullptr);
		}
		SetEvent(g_uvGo);
		WaitForSingleObject(g_uvDone, 5000);
	}
	else g_fakeUv();
	if (FO.moveAfterUv) *(float*)(FA(VPCAM_VA_VIEW) + 48) += 1.0f;   // view 0 translation x: Present now reads another camera
	if (FO.uvWorkUs > 0)
	{
		int64_t end = Qpc() + (int64_t)(FO.uvWorkUs * (double)g_freq.QuadPart / 1e6);
		while (Qpc() < end) YieldProcessor();
	}
}

// --depth-dump (issue 3304): the D24S8 depth buffer after the frame's lists, before Present.
static bool g_depthDumped;
static void DumpDepthBuffer(const wchar_t* path)
{
	g_depthDumped = true;
	if (!g_dsTex) { Log("depth dump: no depth buffer (needs --game)"); return; }
	D3D11_TEXTURE2D_DESC td; g_dsTex->GetDesc(&td);
	td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	HRESULT hr = g_dev->CreateTexture2D(&td, nullptr, &st);
	if (FAILED(hr)) { td.Format = DXGI_FORMAT_R24G8_TYPELESS; hr = g_dev->CreateTexture2D(&td, nullptr, &st); }
	if (FAILED(hr)) { Log("depth dump: staging texture failed 0x%08X", hr); return; }
	g_ctx->CopyResource(st, g_dsTex);
	D3D11_MAPPED_SUBRESOURCE m;
	hr = g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
	if (FAILED(hr)) { Log("depth dump: Map failed 0x%08X", hr); st->Release(); return; }
	FILE* f = nullptr;
	if (!_wfopen_s(&f, path, L"wb") && f)
	{
		fwrite("C5DD", 1, 4, f);
		uint32_t wh[2] = { td.Width, td.Height }; fwrite(wh, 4, 2, f);
		for (UINT y = 0; y < td.Height; ++y) fwrite((const uint8_t*)m.pData + (size_t)y * m.RowPitch, 4, td.Width, f);
		fclose(f);
		Log("depth dump: %ux%u format %u written to %ls", td.Width, td.Height, td.Format, path);
	}
	else Log("depth dump: cannot write %ls", path);
	g_ctx->Unmap(st, 0);
	st->Release();
}

static void FakeExeStop()
{
	g_fakeQuit = true;
	if (g_fakeThread) { WaitForSingleObject(g_fakeThread, 5000); CloseHandle(g_fakeThread); g_fakeThread = nullptr; }
}

static int ParseArgs()
{
	int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i < argc; ++i)
	{
		const wchar_t* a = argv[i];
		const wchar_t* v = (i + 1 < argc) ? argv[i + 1] : L"";
		bool used = true;
		if (!wcscmp(a, L"--swap")) O.swap = !wcscmp(v, L"flip") ? 1 : !wcscmp(v, L"flipseq") ? 2 : 0;
		else if (!wcscmp(a, L"--buffers")) O.buffers = _wtoi(v);
		else if (!wcscmp(a, L"--format")) O.format = !wcscmp(v, L"bgra") ? DXGI_FORMAT_B8G8R8A8_UNORM : !wcscmp(v, L"srgb") ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB :
			!wcscmp(v, L"rgb10a2") ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;   // rgb10a2: issue 3013, a format the .shot dump refuses
		else if (!wcscmp(a, L"--msaa")) O.msaa = std::max(1, _wtoi(v));
		else if (!wcscmp(a, L"--scene-msaa")) O.sceneMsaa = (UINT)std::max(0, _wtoi(v));   // ticket 0804
		else if (!wcscmp(a, L"--vsync")) O.vsync = _wtoi(v);
		else if (!wcscmp(a, L"--load")) O.load = !wcscmp(v, L"before") ? 0 : !wcscmp(v, L"none") ? 2 : 1;
		else if (!wcscmp(a, L"--load-frames")) O.loadFrames = _wtoi(v);
		else if (!wcscmp(a, L"--dll")) wcsncpy_s(O.dll, v, _TRUNCATE);
		else if (!wcscmp(a, L"--size")) swscanf_s(v, L"%ux%u", &O.w, &O.h);
		else if (!wcscmp(a, L"--pos")) swscanf_s(v, L"%d,%d", &O.x, &O.y);
		else if (!wcscmp(a, L"--bb")) swscanf_s(v, L"%ux%u", &O.bbW, &O.bbH);
		else if (!wcscmp(a, L"--seconds")) O.seconds = _wtof(v);
		else if (!wcscmp(a, L"--churn")) O.churn = _wtof(v);
		else if (!wcscmp(a, L"--scenario")) O.scenario = !wcscmp(v, L"robust");
		else if (!wcscmp(a, L"--label")) { size_t c; wcstombs_s(&c, O.label, v, _TRUNCATE); }
		else if (!wcscmp(a, L"--summary")) wcsncpy_s(O.summary, v, _TRUNCATE);
		else if (!wcscmp(a, L"--log")) wcsncpy_s(O.logPath, v, _TRUNCATE);
		else if (!wcscmp(a, L"--order")) wcsncpy_s(O.order, v, _TRUNCATE);
		else if (!wcscmp(a, L"--vary")) O.vary = _wtoi(v);
		else if (!wcscmp(a, L"--mt")) O.mt = _wtoi(v);
		else if (!wcscmp(a, L"--seed")) O.seed = (unsigned)_wtoi(v);
		else if (!wcscmp(a, L"--recreate-at")) O.recreateAt = _wtof(v);
		else if (!wcscmp(a, L"--recreate-same")) O.recreateSame = _wtoi(v);
		else if (!wcscmp(a, L"--recreate-same-address")) O.recreateSameAddress = std::max(0, std::min(100000, _wtoi(v)));
		else if (!wcscmp(a, L"--extra-chains")) O.extraChains = std::max(0, std::min(30, _wtoi(v)));
		else if (!wcscmp(a, L"--occluded-sleep")) O.occludedSleep = _wtoi(v);
		else if (!wcscmp(a, L"--fake-cam-x")) FO.x = _wtof(v);
		else if (!wcscmp(a, L"--fake-cam-y")) FO.y = _wtof(v);
		else if (!wcscmp(a, L"--fake-cam-pan")) FO.pan = _wtof(v);
		else if (!wcscmp(a, L"--fake-cam-views")) FO.views = std::min(3, std::max(1, _wtoi(v)));
		else if (!wcscmp(a, L"--fake-cam-writer")) FO.writerThread = !wcscmp(v, L"thread");
		else if (!wcscmp(a, L"--fake-cam-freeze-at")) FO.freezeAt = _wtof(v);
		else if (!wcscmp(a, L"--fake-cam-mode-at")) swscanf_s(v, L"%lf:%d", &FO.modeAt, &FO.mode);
		else if (!wcscmp(a, L"--fake-uv-every")) FO.uvEvery = std::max(1, _wtoi(v));
		else if (!wcscmp(a, L"--uv-work-us")) FO.uvWorkUs = std::max(0.0, _wtof(v));
		else if (!wcscmp(a, L"--depth-dump")) wcsncpy_s(FO.depthDump, v, _TRUNCATE);
		else if (!wcscmp(a, L"--depth-dump-at")) FO.depthDumpAt = _wtof(v);
		else if (!wcscmp(a, L"--depth-dump-on")) wcsncpy_s(FO.depthDumpOn, v, _TRUNCATE);
		else if (!wcscmp(a, L"--fake-exe-bad"))
		{
			FO.bad = !wcscmp(v, L"tds") ? 1 : !wcscmp(v, L"sig") ? 2 : !wcscmp(v, L"mz") ? 3 : !wcscmp(v, L"pe") ? 4 : !wcscmp(v, L"soi") ? 5 : -1;
			if (FO.bad < 0) { fwprintf(stderr, L"bad --fake-exe-bad %s\n", v); return 2; }
		}
		else if (!wcscmp(a, L"--fake-exe-build"))
		{
			FO.exeBuild = kVpExeBuildUnknown;
			for (int b = 0; b < kVpExeBuildCount; ++b)
			{
				wchar_t sb[16]; _snwprintf_s(sb, _TRUNCATE, L"%S", VpExeBuildInfoOf(b)->steamBuild);
				if (!wcscmp(v, sb)) FO.exeBuild = b;
			}
			if (FO.exeBuild < 0) { fwprintf(stderr, L"bad --fake-exe-build %s (known: 4390913, 26577625)\n", v); return 2; }
		}
		else if (!wcscmp(a, L"--fake-cam-bad"))
		{
			FO.camBad = -1;
			for (int k = 0; k < CB_COUNT; ++k) if (!wcscmp(v, kCamBadNames[k])) FO.camBad = k;
			if (FO.camBad < 0) { fwprintf(stderr, L"bad --fake-cam-bad %s\n", v); return 2; }
		}
		else
		{
			used = false;
			if (!wcscmp(a, L"--borderless")) O.borderless = true;
			else if (!wcscmp(a, L"--allow-fullscreen")) O.allowFullscreen = true;
			else if (!wcscmp(a, L"--deferred")) O.deferred = true;
			else if (!wcscmp(a, L"--game")) O.game = true;
			else if (!wcscmp(a, L"--fake-exe")) FO.on = true;
			else if (!wcscmp(a, L"--fake-uv")) FO.uv = true;
			else if (!wcscmp(a, L"--fake-cam-move-after-uv")) FO.moveAfterUv = true;
			else if (!wcscmp(a, L"--fake-uv-thread")) FO.uvThread = true;
			else if (!wcscmp(a, L"--fatal-sink")) O.fatalSink = true;
			else { fwprintf(stderr, L"unknown argument %s\n", a); return 2; }
		}
		if (used) ++i;
	}
	LocalFree(argv);
	if (!O.buffers) O.buffers = O.swap ? 2 : 1;
	wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
	wchar_t* s = wcsrchr(dir, L'\\'); if (s) s[1] = 0;
	if (!O.dll[0]) _snwprintf_s(O.dll, _TRUNCATE, L"%sshell_composite.dll", dir);
	if (!O.logPath[0]) _snwprintf_s(O.logPath, _TRUNCATE, L"%stest_host.log", dir);
	return 0;
}

// Swap chain on g_hwnd through the factory (also used by --recreate-at)
static HRESULT CreateSwapChainNow()
{
	HRESULT hr;
	IDXGIFactory2* f2 = g_factory;
	UINT bw = O.bbW ? O.bbW : O.w, bh = O.bbH ? O.bbH : O.h;
	if (O.swap == 0)
	{
		DXGI_SWAP_CHAIN_DESC d = {};
		d.BufferDesc.Width = bw; d.BufferDesc.Height = bh; d.BufferDesc.Format = O.format;
		d.SampleDesc.Count = O.msaa; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = O.buffers;
		d.OutputWindow = g_hwnd; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		hr = f2->CreateSwapChain(g_dev, &d, &g_sc);
	}
	else
	{
		DXGI_SWAP_CHAIN_DESC1 d = {};
		d.Width = bw; d.Height = bh; d.Format = O.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM : O.format;
		d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = std::max(2, O.buffers);
		d.SwapEffect = O.swap == 1 ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		d.Scaling = DXGI_SCALING_STRETCH;
		d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		IDXGISwapChain1* s1 = nullptr;
		hr = f2->CreateSwapChainForHwnd(g_dev, g_hwnd, &d, nullptr, nullptr, &s1);
		g_sc = s1;
	}
	return hr;
}

static HWND g_oldHwnd;
static HWND CreateHostWindow(HINSTANCE inst)
{
	DWORD style = O.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
	RECT r = { 0, 0, (LONG)O.w, (LONG)O.h }; AdjustWindowRect(&r, style, FALSE);
	HWND h = CreateWindowExW(0, L"VPShellTestHost", L"VP shell test host", style, O.x, O.y, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
	ShowWindow(h, SW_SHOWNOACTIVATE); // never steal the foreground from whatever the user is doing
	return h;
}

// --recreate-at: a new window and a new swap chain, as the game's resolution change does
// returns true when --recreate-same-address wants another round (the old address did not come back)
static int g_reuseRounds;
static bool RecreateWindowAndSwapChain(HINSTANCE inst)
{
	bool again = false;
	int64_t t0 = Qpc();
	SafeRelease(g_rtv);
	if (O.game) { SafeRelease(g_dsv); SafeRelease(g_dsTex); }
	g_ctx->ClearState(); g_ctx->Flush();
	IDXGISwapChain* old = g_sc; g_sc = nullptr;
	g_oldHwnd = g_hwnd;
	HRESULT hr;
	if (O.recreateSame)
	{
		// --recreate-same 1: the new swap chain on the same window, as the game's resolution change does (3008 [L])
		if (old) old->Release();
		g_ctx->Flush();
		hr = CreateSwapChainNow();
		if (O.recreateSameAddress > 0)
		{
			// issue 0141: force the heap-reuse case. A chain at another address is kept until the end: released at once,
			// its block would be the next one handed out (2026-11-13: 5000 release-and-retry tries all got the first
			// try's address), so keeping it moves the next try on to another free block, the old one among them.
			std::vector<IDXGISwapChain*> kept;
			int k = 1;
			while (SUCCEEDED(hr) && g_sc != old && k < O.recreateSameAddress)
			{
				kept.push_back(g_sc); g_sc = nullptr;
				hr = CreateSwapChainNow();
				k++;
			}
			for (auto* s : kept) s->Release();
			g_ctx->Flush();
			// 2026-11-13: one round in 15 kept 100 chains without getting the old address back (the block was taken by
			// another allocation meanwhile); a new round starts from the new chain, after it has presented
			g_reuseRounds++;
			if (SUCCEEDED(hr) && g_sc == old) Log("recreate: old address reused after %d tries in round %d (%p)", k, g_reuseRounds, (void*)old);
			else
			{
				again = SUCCEEDED(hr) && g_reuseRounds < 8;
				Log("recreate: old address NOT reused in %d tries in round %d (old %p, new %p, 0x%08X)%s", k, g_reuseRounds, (void*)old,
					(void*)g_sc, hr, again ? ": another round in 0.5 s" : ": no more rounds");
			}
		}
	}
	else
	{
		g_hwnd = CreateHostWindow(inst);
		hr = CreateSwapChainNow();
		if (old) old->Release();
		DestroyWindow(g_oldHwnd);
	}
	CreateRtv();
	if (O.game) CreateDepth();
	Log("recreate: new window %p, new swap chain %p (0x%08X) in %.2f ms", g_hwnd, g_sc, hr, QpcToMs(Qpc() - t0));
	return again;
}

// --extra-chains N (issue 3017): other windows with their own swap chains, created through the factory (as an overlay
// or a second window of the process would), presenting after the main chain every frame. The composite must stay on
// the main chain, and the DLL's log must not grow with every Present of the others.
struct ExtraChain { HWND w; IDXGISwapChain* sc; ID3D11RenderTargetView* rtv; };
static std::vector<ExtraChain> g_extra;
static void CreateExtraChains(HINSTANCE inst)
{
	for (int i = 0; i < O.extraChains; ++i)
	{
		ExtraChain e = {};
		e.w = CreateWindowExW(WS_EX_NOACTIVATE, L"VPShellTestHost", L"VP shell test host extra", WS_OVERLAPPEDWINDOW,
			O.x + 20 * (i + 1), O.y + 20 * (i + 1), 340, 240, nullptr, nullptr, inst, nullptr);
		if (!e.w) { Log("extra chain %d: CreateWindowExW failed %u", i, GetLastError()); continue; }
		ShowWindow(e.w, SW_SHOWNOACTIVATE);
		DXGI_SWAP_CHAIN_DESC d = {};
		d.BufferDesc.Width = 320; d.BufferDesc.Height = 200; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 1;
		d.OutputWindow = e.w; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		HRESULT hr = g_factory->CreateSwapChain(g_dev, &d, &e.sc);
		if (FAILED(hr)) { Log("extra chain %d: CreateSwapChain failed 0x%08X", i, hr); DestroyWindow(e.w); continue; }
		ID3D11Texture2D* bb = nullptr;
		if (SUCCEEDED(e.sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) { g_dev->CreateRenderTargetView(bb, nullptr, &e.rtv); bb->Release(); }
		Log("extra chain %d: sc %p hwnd %p 320x200", i, e.sc, e.w);
		g_extra.push_back(e);
	}
}

static void PresentExtraChains(int frame)
{
	for (size_t i = 0; i < g_extra.size(); ++i)
	{
		const FLOAT col[4] = { (float)((i * 37 + frame) % 255) / 255.f, 0.3f, 0.5f, 1.0f };
		if (g_extra[i].rtv) g_ctx->ClearRenderTargetView(g_extra[i].rtv, col);
		g_extra[i].sc->Present(0, 0);
	}
}

static void ReleaseExtraChains()
{
	for (auto& e : g_extra) { SafeRelease(e.rtv); SafeRelease(e.sc); DestroyWindow(e.w); }
	g_extra.clear();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
	if (ParseArgs()) return 2;
	QueryPerformanceFrequency(&g_freq);
	g_log = _wfsopen(O.logPath, L"a", _SH_DENYNO); // shared: readable while the host runs (shell/tests)
	Log("==== test_host start pid %u label '%s' swap %s buffers %d format %u msaa %u vsync %d load %s size %ux%u bb %ux%u",
		GetCurrentProcessId(), O.label, O.swap == 0 ? "blit(DISCARD)" : O.swap == 1 ? "flip(FLIP_DISCARD)" : "flip(FLIP_SEQUENTIAL)",
		O.buffers, O.format, O.msaa, O.vsync, O.load == 0 ? "before" : O.load == 1 ? "after" : "none", O.w, O.h, O.bbW, O.bbH);

	if (FO.on && !FakeExeInit()) return 1;
	if (FO.uv && (!FO.on || !FakeUvInit())) { Log("--fake-uv needs --fake-exe and the build's d3dx9_4x.dll"); return 1; }
	if (O.load == 0) LoadCompositeDll("before device creation");

	WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"VPShellTestHost";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&wc);
	g_hwnd = CreateHostWindow(inst);

	D3D_FEATURE_LEVEL fl;
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_dev, &fl, &g_ctx);
	if (FAILED(hr)) { Log("D3D11CreateDevice failed 0x%08X", hr); return 1; }
	IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f2 = nullptr;
	g_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd); xd->GetAdapter(&ad); ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2);
	ad->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&g_adapter3);
	DXGI_ADAPTER_DESC adesc; ad->GetDesc(&adesc);
	Log("device FL %x on '%ls' LUID %08X%08X", fl, adesc.Description, adesc.AdapterLuid.HighPart, adesc.AdapterLuid.LowPart);
	g_factory = f2;
	hr = CreateSwapChainNow();
	SafeRelease(ad); SafeRelease(xd);
	if (FAILED(hr)) { Log("swap chain creation failed 0x%08X", hr); return 1; }
	CreateRtv();

	g_dev->CreateVertexShader(g_VSHostUi, sizeof(g_VSHostUi), nullptr, &g_vs);
	g_dev->CreatePixelShader(g_PSHostUi, sizeof(g_PSHostUi), nullptr, &g_ps);
	D3D11_BUFFER_DESC bd = { sizeof(HostCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	g_dev->CreateBuffer(&bd, nullptr, &g_cb);
	D3D11_BLEND_DESC bld = {};
	bld.RenderTarget[0].BlendEnable = TRUE;
	bld.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	g_dev->CreateBlendState(&bld, &g_bs);
	D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.MultisampleEnable = O.msaa > 1;
	g_dev->CreateRasterizerState(&rd, &g_rs);
	D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = FALSE; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
	g_dev->CreateDepthStencilState(&dd, &g_dss);
	if (!g_vs || !g_ps || !g_cb || !g_bs || !g_rs || !g_dss) { Log("resource creation failed"); return 1; }
	if (O.deferred && FAILED(g_dev->CreateDeferredContext(0, &g_def))) { Log("CreateDeferredContext failed"); return 1; }
	if (O.game) { if (!GameInit()) return 1; GameStartWorkers(); }
	bool recreated = false, extraMade = false;
	LogMemory("start");

	std::vector<float> ft; ft.reserve(1 << 20);
	std::vector<float> sec; sec.reserve(1 << 16);
	const int64_t start = Qpc();
	int64_t prev = 0, secStart = start, lastMem = start;
	int64_t frame = 0;
	int occluded = 0;
	int step = 0;
	HostCB cbData;

	while (!g_quit)
	{
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) g_quit = true; TranslateMessage(&msg); DispatchMessageW(&msg); }
		if (g_quit) break;
		int64_t now = Qpc();
		double el = QpcToMs(now - start) / 1000.0;
		if (O.seconds > 0 && el > O.seconds) break;
		if (O.load == 1 && frame == O.loadFrames) LoadCompositeDll("after device creation (frame N)");

		if (O.scenario)
		{
			// t (s), action
			static const double T[] = { 6, 9, 12, 15, 18, 21, 24, 30, 36 };
			if (step < (int)(sizeof(T) / sizeof(T[0])) && el >= T[step])
			{
				switch (step)
				{
				case 0: Log("scenario: minimize"); ShowWindow(g_hwnd, SW_SHOWMINNOACTIVE); break;
				case 1: Log("scenario: restore"); ShowWindow(g_hwnd, SW_SHOWNOACTIVATE); break;
				case 2: Log("scenario: window resize 1024x640"); ResizeWindowClient(1024, 640); break;
				case 3: Log("scenario: ResizeBuffers to 1920x1080 (window unchanged)"); ResizeBuffersTo(1920, 1080, "scenario"); break;
				case 4: Log("scenario: ResizeBuffers to 2560x1440"); ResizeBuffersTo(2560, 1440, "scenario"); break;
				case 5: Log("scenario: window resize back to %ux%u", O.w, O.h); ResizeWindowClient(O.w, O.h); break;
				case 6: if (O.allowFullscreen) { Log("scenario: fullscreen ON"); ToggleFullscreen(); } else Log("scenario: fullscreen skipped (no --allow-fullscreen)"); break;
				case 7: if (O.allowFullscreen) { Log("scenario: fullscreen OFF"); ToggleFullscreen(); } break;
				case 8: Log("scenario: done"); break;
				}
				step++;
			}
		}

		if (O.churn > 0)
		{
			static double nextChurn = O.churn;
			static int ci = 0;
			if (el >= nextChurn)
			{
				static const UINT res[3][2] = { { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 } };
				ci = (ci + 1) % 3;
				ResizeBuffersTo(res[ci][0], res[ci][1], "churn");
				nextChurn += O.churn;
			}
		}

		if (prev) { float d = (float)QpcToMs(now - prev); if (el > 3.0) ft.push_back(d); sec.push_back(d); }
		prev = now;

		if (O.recreateAt > 0 && !recreated && el >= O.recreateAt)
		{
			recreated = true;
			if (RecreateWindowAndSwapChain(inst)) { recreated = false; O.recreateAt = el + 0.5; }   // issue 0141: another round
		}
		if (O.extraChains > 0 && !extraMade && el >= 2.0) { extraMade = true; CreateExtraChains(inst); }

		if (FO.on && !FO.writerThread) FakeCamWrite(QpcToMs(Qpc() - g_fakeStart) / 1000.0);
		FakeUvFrame(frame);   // issue 3304: --fake-uv (nothing otherwise)
		if (g_rtv && O.game)
		{
			GameFrame((float)el, frame);
			if (FO.depthDump[0] && !g_depthDumped)
			{
				static double nextLook = 0;
				bool dumpNow = false;
				if (!FO.depthDumpOn[0]) dumpNow = el >= FO.depthDumpAt;
				else if (el >= nextLook) { nextLook = el + 0.005; dumpNow = GetFileAttributesW(FO.depthDumpOn) != INVALID_FILE_ATTRIBUTES; }
				if (dumpNow) DumpDepthBuffer(FO.depthDump);
			}
		}
		else if (g_rtv)
		{
			static const FLOAT grey[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
			if (g_def)
			{
				ID3D11CommandList* cl = nullptr;
				g_def->OMSetRenderTargets(1, &g_rtv, nullptr);
				g_def->ClearRenderTargetView(g_rtv, grey);
				if (SUCCEEDED(g_def->FinishCommandList(FALSE, &cl))) { g_ctx->ExecuteCommandList(cl, FALSE); cl->Release(); }
			}
			else
				g_ctx->ClearRenderTargetView(g_rtv, grey);
			D3D11_MAPPED_SUBRESOURCE m;
			BuildUi(cbData, (float)el, frame);
			int nRects = (int)cbData.pad;
			if (SUCCEEDED(g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cbData, sizeof(cbData)); g_ctx->Unmap(g_cb, 0); }
			D3D11_VIEWPORT vp = { 0, 0, (float)g_bbW, (float)g_bbH, 0, 1 };
			g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
			g_ctx->RSSetViewports(1, &vp);
			g_ctx->RSSetState(g_rs);
			static const FLOAT bf[4] = {};
			g_ctx->OMSetBlendState(g_bs, bf, 0xFFFFFFFF);
			g_ctx->OMSetDepthStencilState(g_dss, 0);
			g_ctx->IASetInputLayout(nullptr);
			g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			g_ctx->VSSetShader(g_vs, nullptr, 0);
			g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
			g_ctx->PSSetShader(g_ps, nullptr, 0);
			g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
			g_ctx->DrawInstanced(6, nRects, 0, 0);
		}
		hr = g_sc->Present(O.vsync ? 1 : 0, 0);
		if (hr == DXGI_STATUS_OCCLUDED) { if (!occluded++) Log("Present: occluded"); if (O.occludedSleep > 0) Sleep(O.occludedSleep); }
		else if (occluded) { Log("Present: visible again after %d occluded presents", occluded); occluded = 0; }
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			Log("Present: device removed 0x%08X reason 0x%08X", hr, g_dev->GetDeviceRemovedReason());
			break;
		}
		if (!g_extra.empty()) PresentExtraChains((int)frame);
		frame++;

		now = Qpc();
		if (QpcToMs(now - secStart) >= 1000.0)
		{
			double s = 0, mx = 0; for (float v : sec) { s += v; mx = std::max(mx, (double)v); }
			Log("1s: frames %zu mean %.3f ms max %.3f ms bb %ux%u", sec.size(), sec.empty() ? 0.0 : s / sec.size(), mx, g_bbW, g_bbH);
			sec.clear(); secStart = now;
		}
		if (QpcToMs(now - lastMem) >= 10000.0) { LogMemory("periodic"); lastMem = now; }
	}

	// summary (first 3 s excluded)
	double mean = 0, p50 = 0, p99 = 0, mx = 0;
	if (!ft.empty())
	{
		double s = 0; for (float v : ft) { s += v; mx = std::max(mx, (double)v); }
		mean = s / ft.size();
		std::vector<float> c = ft;
		std::nth_element(c.begin(), c.begin() + c.size() / 2, c.end()); p50 = c[c.size() / 2];
		size_t k = (size_t)((c.size() - 1) * 0.99); std::nth_element(c.begin(), c.begin() + k, c.end()); p99 = c[k];
	}
	Log("summary label=%s frames=%zu mean=%.4f p50=%.4f p99=%.4f max=%.3f fps=%.1f", O.label, ft.size(), mean, p50, p99, mx, mean > 0 ? 1000.0 / mean : 0.0);
	LogMemory("end");
	if (O.summary[0])
	{
		FILE* f = nullptr;
		if (!_wfopen_s(&f, O.summary, L"a") && f)
		{
			fprintf(f, "%s,%zu,%.4f,%.4f,%.4f,%.3f,%.1f\n", O.label, ft.size(), mean, p50, p99, mx, mean > 0 ? 1000.0 / mean : 0.0);
			fclose(f);
		}
	}
	if (FO.on) FakeExeStop();
	if (g_uvThread) { g_uvQuit = true; SetEvent(g_uvGo); WaitForSingleObject(g_uvThread, 2000); CloseHandle(g_uvThread); CloseHandle(g_uvGo); CloseHandle(g_uvDone); }
	BOOL fs = FALSE; if (g_sc) { g_sc->GetFullscreenState(&fs, nullptr); if (fs) g_sc->SetFullscreenState(FALSE, nullptr); }
	if (O.game) GameRelease();
	ReleaseExtraChains();
	SafeRelease(g_factory);
	SafeRelease(g_sceneRtv); SafeRelease(g_sceneTex);
	SafeRelease(g_rtv); SafeRelease(g_sc); SafeRelease(g_cb); SafeRelease(g_bs); SafeRelease(g_rs); SafeRelease(g_dss);
	SafeRelease(g_vs); SafeRelease(g_ps); SafeRelease(g_adapter3); SafeRelease(g_def); SafeRelease(g_ctx); SafeRelease(g_dev);
	if (g_log) fclose(g_log);
	return 0;
}
