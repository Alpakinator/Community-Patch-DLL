// Origin (rule 13, decision D22): the project's fork of Community-Patch-DLL, branch claude/shell-gates-d8e422,
// path experiments/shell-gates/compositor/src/shell_composite.cpp, copied at branch head c60138b82 (file last changed in b9ffe3d99).
// Copied into shell/composite/shell_composite.cpp on 2026-11-10 (issue 3001); later changes are in this repository's history.
// Issue 3006 (2026-11-10): vtable patching replaced by MinHook inline hooks on the implementation functions in
// d3d11.dll / dxgi.dll; command-list tagging and the tagged insertion rules; swap-chain adoption at creation;
// per-hook kill switches.
//
// shell_composite.dll - x86 consumer of shell_server.exe's shared texture.
//
// Loaded into a D3D11 process (normally long after the process created its device), it installs inline hooks
// (MinHook, built from source in shell/third_party/minhook) on the implementation functions behind the swap chain,
// factory, device and context vtables. The targets are read from the vtables of real objects: a dummy device at load,
// then the host's own device, immediate context, a deferred context created on the host's device, and every deferred
// context the host creates. Immediate and deferred contexts have different implementation functions, and so do devices
// of different feature levels (probe 2026-11-10): every distinct target is hooked once (up to 4 per method).
// Only targets inside d3d11.dll and dxgi.dll are hooked; the EXE image is never written (decision D4).
//
// Once per frame at a configurable insertion point it copies the latest frame of the shared texture (keyed mutex)
// into a local texture and draws it as a fullscreen triangle, saving and restoring every piece of pipeline state it
// touches. Insertion points (insert=): the prototype's clear / nthclear / omset / exec / present on the immediate
// context, and `tagged`: every deferred context's back-buffer clears, depth clears and render-target bindings are
// recorded while it records, attached to its command list at FinishCommandList, and known again at
// ExecuteCommandList on the immediate context; tag_rule = after (after the executed list that matches tag_match,
// default the back-buffer clear), before (before the first list after tag_anchor that matches tag_match, default the
// first list after the clear that binds the back buffer with a depth buffer: the 3D pass), inlist (recorded into the
// deferred context right after its back-buffer clear, on the recording thread, sampling the local copy the immediate
// context filled before that list executes; keyed-mutex sync cannot happen inside a deferred list).
//
// Threads
//   * DllMain only spawns the control thread (nothing heavy under the loader lock).
//   * Control thread: reads the config, finds hook targets with a dummy device on hidden windows, hooks them, then
//     polls every 500 ms for config changes (shell_composite.ini: mode, insertion rule, kill switches, all live) and
//     trigger files (.shot, .trace).
//   * Present / ExecuteCommandList / immediate-context hooks run on the host's render thread; the deferred-context
//     hooks run on whatever thread records.
//
// Never crash the host: every hook body runs under SEH; on any exception or unexpected failure the composite is
// disabled and the hooks degrade to plain pass-through calls.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <psapi.h>          // ticket 0688: GetProcessMemoryInfo (K32GetProcessMemoryInfo in kernel32: PSAPI_VERSION 2)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <share.h>
#include <intrin.h>
#include <algorithm>
#include <new>
#include "shell_shared.h"
#include "exe_camera.h"
#include "bake_tap.h"
#include "water_tap.h"       // epic 3600, issue 3603: the water mesh tap
#include "overlay_tap.h"     // epic 0167, issue 3703: the Terrain Overlay target tap
#include "shadow_tap.h"      // epic 0169, issue 4304: the model shadow map tap (ticket 0312)
#include "decal_tap.h"       // epic 0166, issue 4102: the decal tap (TerrainDecalSystem records into shared sections)
#include "ice_tap.h"         // epic 0169, issue 4307: the ice tap (the EXE's ice mesh once per load)
#include "forest_tap.h"      // ticket 0420: the forest tap (the Forest system's layouts and plot records into a file per load)
#include "cut_tap.h"         // ticket 4553: the cut-mask tap (the EXE's plot mask grid 0xBA392C into a section per change)
#include "fow_models.h"      // ticket 0339: the server's fog mask behind the white handle for the EXE's model draws
#include "unit_filter.h"     // epic 0040, issue 4752: the unit-draw filter (the EXE's pass-0 unit draws dropped while the server draws units)
#include "farm_guard.h"      // ticket 0527: the farm builder guard (the builder's listener word; the free-list check after each call)
#include "vp_frame.h"        // issue 3304: the publish block, lockstep
#include "frame_source.h"    // issue 3304: the hand-over behind vpframe::FrameSource (v3 here, ring in shell/frame)
#include "ring_source.h"     // issue 3302's ring source, selected by handover=ring
#include "lk_late.h"         // issue 0150: late starts and overruns of the lockstep wait
#include "vp_pacing.h"       // ticket 0688: the frame-pacing instruments (epic 0686 P1)
#include "VSFullscreen.h"
#include "PSComposite.h"
#include "PSCompositeDepth.h"
#include "MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

extern "C" const unsigned VT_SwapChain_Present;
extern "C" const unsigned VT_SwapChain_GetBuffer;
extern "C" const unsigned VT_SwapChain_ResizeBuffers;
extern "C" const unsigned VT_SwapChain1_Present1;
extern "C" const unsigned VT_Factory_CreateSwapChain;
extern "C" const unsigned VT_Factory2_CreateSwapChainForHwnd;
extern "C" const unsigned VT_Device_CreateDeferredContext;
extern "C" const unsigned VT_Device1_CreateDeferredContext1;
extern "C" const unsigned VT_Device2_CreateDeferredContext2;
extern "C" const unsigned VT_Device3_CreateDeferredContext3;
extern "C" const unsigned VT_Context_ClearRenderTargetView;
extern "C" const unsigned VT_Context_ClearDepthStencilView;
extern "C" const unsigned VT_Context_OMSetRenderTargets;
extern "C" const unsigned VT_Context_ExecuteCommandList;
extern "C" const unsigned VT_Context_FinishCommandList;
extern "C" const unsigned VT_Context_OMSetRenderTargetsAndUAVs;
// epic 0040, issue 4752: the unit-draw filter's hooks (unit_filter.cpp)
extern "C" const unsigned VT_Device_CreateVertexShader, VT_Device_CreatePixelShader, VT_Context_VSSetShader, VT_Context_PSSetShader,
	VT_Context_DrawIndexedInstanced, VT_Context_DrawIndexed, VT_Context_Draw, VT_Context_DrawInstanced,
	VT_Context_DrawIndexedInstancedIndirect, VT_Context_DrawInstancedIndirect, VT_Context_DrawAuto;
// epic 0561, ticket 4874: the model tap's hooks (unit_filter.cpp), under hook_models
extern "C" const unsigned VT_Device_CreateBuffer, VT_Device_CreateTexture2D, VT_Context_Map, VT_Context_Unmap,
	VT_Context_UpdateSubresource, VT_Context_CopyResource, VT_Context_CopySubresourceRegion;

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ASan build only (shell\build.cmd asan, issue 3013). A hardware exception (an access violation in the camera read)
// caught by __except unwinds instrumented frames without their epilogues, so their stack redzones stay poisoned below
// the handler's frame. Uninstrumented code that later uses that stack (d3d11.dll's memcpy, seen 2026-11-11 under
// CreateTexture2D) then trips ASan with a false report, and ASan's own description of it fails a CHECK. Every handler
// therefore unpoisons the dead stack below it first. (RaiseException is intercepted by ASan, which does the same.)
// In the normal build this is empty.
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
static __declspec(noinline) void AsanAfterSeh()
{
	volatile char probe = 0;
	char* p = (char*)&probe;
	__asan_unpoison_memory_region(p - 65536, 65536);
}
#else
static inline void AsanAfterSeh() {}
#endif

// ===========================================================================
// Config
// ===========================================================================
enum { MODE_OFF = 0, MODE_PASSTHROUGH = 1, MODE_COMPOSITE = 2 };
enum { INS_CLEAR = 0, INS_NTHCLEAR = 1, INS_OMSET = 2, INS_PRESENT = 3, INS_EXEC = 4, INS_TAGGED = 5 };
enum { RULE_AFTER = 0, RULE_BEFORE = 1, RULE_INLIST = 2 };
static const char* kModeNames[] = { "off", "passthrough", "composite" };
static const char* kInsNames[] = { "clear", "nthclear", "omset", "present", "exec", "tagged" };
static const char* kRuleNames[] = { "after", "before", "inlist" };

// Kill switches (one per hooked method group, ini key hook_<name>)
enum { SW_PRESENT, SW_RESIZE, SW_CREATESC, SW_CREATEDEF, SW_CLEAR, SW_CLEARDSV, SW_OMSET, SW_FINISH, SW_EXEC, SW_UNITS, SW_MODELS, SW_COUNT };
static const char* kSwNames[SW_COUNT] = { "present", "resize", "create_swapchain", "create_deferred", "clear", "clear_dsv", "omset", "finish", "exec",
	"units",     // issue 4752: hook_units, every hook of the unit-draw filter (also the model filter's draws and binds)
	"models" };  // ticket 4874: hook_models, the model tap's own hooks (CreateBuffer, CreateTexture2D, Map, Unmap)

// Tags recorded per deferred context and attached to its command list
enum : uint32_t
{
	TAG_CLEAR_BB = 1u << 0,    // ClearRenderTargetView of the back buffer
	TAG_CLEAR_RT = 1u << 1,    // ClearRenderTargetView of another target
	TAG_CLEAR_DSV = 1u << 2,   // ClearDepthStencilView
	TAG_BIND_BB = 1u << 3,     // OMSetRenderTargets with the back buffer, no DSV
	TAG_BIND_BB_DSV = 1u << 4, // OMSetRenderTargets with the back buffer and a DSV
	TAG_BIND_RT = 1u << 5,     // another render target, no DSV
	TAG_BIND_RT_DSV = 1u << 6, // another render target with a DSV
	TAG_NESTED = 1u << 7,      // a command list executed inside this one
	TAG_INSERTED = 1u << 8,    // inlist: our draw was recorded into this list
	TAG_WOULD = 1u << 9,       // inlist: the insertion point was reached, nothing drawn (passthrough, stale, no content)
	TAG_NOTREADY = 1u << 10,   // inlist: the insertion point was reached but our resources did not exist yet
};
static const char* kTagNames[] = { "clear_bb", "clear_rt", "clear_dsv", "bind_bb", "bind_bb_dsv", "bind_rt", "bind_rt_dsv", "nested", "inserted", "would", "notready" };
static const int kNumTags = 11;

struct Cond                      // a conjunction parsed from tag_match / tag_anchor
{
	bool empty = true;
	bool impossible = false;     // an unknown token: matches nothing (logged)
	char bad[96] = "";           // issue 3016: why a present rule is unusable (empty after parsing, not ASCII); matches nothing
	uint32_t req = 0, forbid = 0;
	int dsvFmt = -1, rtFmt = -1, ctxIdx = -1, minClears = 0;
	char text[128] = "";
};

// Issue 3014: the longest a Present may wait for the server's frame (AcquireSync), and the number of timeouts in a row
// after which the DLL stops waiting at all until the config is applied again. The presenting thread is the game's
// main thread: an unbounded wait there hangs the game.
static const DWORD kMaxTimeoutMs = 50;
static const int kTimeoutGiveUp = 8;

enum { HO_V3 = 0, HO_RING = 1 };
static const char* kHandoverNames[] = { "v3", "ring" };

// Ticket 0490 (2026-11-17): one truth. Every default below equals the value of the same key in the shipped
// shell/composite/shell_composite.ini, so a compositor without its ini (or with a key left out) runs the configuration the
// tests and the product path run. shell/tests/test_config_defaults.py fails when either changes alone (it compares the
// values the DLL logs with the shipped ini and without any ini). Before 0490 insert, tap, lockstep, handover,
// depth_write, shadow_tap and ice_tap defaulted to the old test values (clear, 0, 0, v3, 0, 0, 0).
struct Config
{
	int mode = MODE_COMPOSITE;
	int insert = INS_TAGGED;    // 0490: was INS_CLEAR
	int nth = 1;
	int tagRule = RULE_AFTER;
	Cond match, anchor;
	int tagNth = 1;
	int tagKeep = 0;            // 1 = keep a command list's tags after it executed (lists executed more than once)
	int adoptOnCreate = 1;      // adopt a swap chain created through the factory at its first Present (no 2 s gap)
	int hookSw[SW_COUNT];
	DWORD timeoutMs = 0;        // issue 3014: clamped to 0 .. kMaxTimeoutMs; timeoutNote says when the ini value was not used
	int timeoutNote = 0;        // 0 = as given, 1 = not a number (0 used), 2 = out of range (clamped)
	wchar_t timeoutText[24] = L"";
	int alphaBlend = 0;
	int hideWhenStale = 0;
	int staleMs = 1000;
	int requestResize = 1;
	int statsMs = 1000;
	int traceFrames = 2;
	int camera = 1;             // issue 3007: read the EXE's camera at every Present and publish it (kill switch: 0)
	int cameraTest = 0;         // test only: 1 = stall (count Presents, publish nothing), 2 = corrupt (shift view 0),
	                            // 3 = late (ticket 0408: after each new valid grid seq the first camera_test_late reads are
	                            // reported INCONSISTENT, as on a first load, where the map view comes up after the grid)
	int cameraTestLate = 20;    // test only: reads held back by camera_test=late (0 .. 100000)
	int faultTest = 0;          // test only (issue 3013): one access violation inside 1 = Composite, 2 = OnPresent, 3 = the
	                            // ExecuteCommandList hook; the SEH guard must set DISABLED and the host must run on
	int hookTest = 0;           // test only (issue 3013): 1 = also try to hook functions outside d3d11/dxgi (must be refused)
	int bakeTap = 0;            // epic 3200, issue 3204: tap the EXE's baked tile caches into shared textures (bake_tap.cpp)
	int waterTap = 1;           // epic 3600, issue 3603: copy the EXE's river and wave meshes once per load (water_tap.cpp); 1 since 3608 (was 0)
	int overlayTap = 1;         // epic 0167, issue 3703: copy the EXE's Terrain Overlay target every Present (overlay_tap.cpp); kill switch 0
	int decalTap = 1;           // epic 0166, issue 4102: copy the EXE's decal records on change (decal_tap.cpp); kill switch 0
	int shadowTap = 1;          // epic 0169, issue 4304: copy the EXE's ShadowBlur every map frame (shadow_tap.cpp); 1 since 0490 (the ini's since 4308)
	int iceTap = 1;             // epic 0169, issue 4307: copy the EXE's ice mesh once per load (ice_tap.cpp); 1 since 0490 (the ini's since 4308)
	int forestTap = 0;          // ticket 0420: write the EXE's forest layouts and plot records to a file per load (forest_tap.cpp); test configurations
	int farmGuard = 1;          // ticket 0527: the farm builder guard (farm_guard.cpp); 0 = the builder's own listener word again
	int farmBuilder = 1;        // ticket 4853: 1 = the guard calls the EXE's farm builder; 0 = never (the server draws the farm fields: the proxy sets VPSHELL_FARM_BUILDER=0 with [Renderer] ServerFarmFields = 1)
	int cutTap = 1;             // ticket 4553: copy the EXE's plot mask grid on change into a section (cut_tap.cpp); 1 since 4554 binds it (the server's trees, MC queue #2985); 0 = OFF published (a test switch)
	int modelFow = FOW_MODELS_DEFAULT;   // ticket 0339's model_fow, read here only to log and compare it (fow_models.cpp reads its own)
	int unitFilter = UF_MODE_AUTO;       // issue 4752: unit_filter = auto (the mode word) | on | off (TEST ONLY); unitFilterNote 1 = not a name
	int unitFilterNote = 0;
	wchar_t unitFilterText[24] = L"";
	int modelFilter = UF_MODE_AUTO;      // ticket 4874: model_filter = auto (the mode word's bits 1, 2) | on | off (TEST ONLY)
	int modelFilterNote = 0;             // 1 = not a name (auto used, CONFIG ERROR)
	wchar_t modelFilterText[24] = L"";
	int modelTapPassed = 0;              // ticket 4874: model_tap_passed (TEST ONLY): tap the passed family draws as well
	int modelTapSlotKb = VPSHELL_MODELTAP_SLOT_KB_DEFAULT;   // ticket 4874: model_tap_slot_kb, a multiple of 64 in the header's range
	int modelTapSlotNote = 0;            // 1 = not used as given (CONFIG ERROR, the default used)
	wchar_t modelTapSlotText[24] = L"";
	int tap = 1;               // epic 3200, issue 3203: copy the EXE's height field once per load into a C5HF section; 1 since 0490 (the ini's since 3313)
	int tapTest = 0;            // test only (issue 3203): TAPT_* - a synthetic or hostile grid object instead of the EXE's
	int tapTestLoad = 0;        // test only: a change of this value (with tap_test set) simulates a new map load
	DWORD tapAckTimeoutMs = VPSHELL_TAP_ACK_TIMEOUT_MS;
	int tapDelayFrames = 30;    // Presents with camera OK and an unchanged grid seq before the copy starts
	int tapSceneFrames = 30;    // and frames with the scene-clear list (clear_bb + clear_dsv) since the grid seq changed; 0 = not needed
	int tapFirstLoad = 1;       // ticket 0408: on the first load in a process (the camera was never OK on a valid grid before,
	                            // and it comes up only after this grid became valid) neither count above applies, only
	                            // tapFirstDelayFrames; 0 = the counts above on every load (as before 0408)
	int tapFirstDelayFrames = 3;   // Presents with camera OK on such a first load before the copy starts (1 .. 100000)
	int tapByteLayer = 1;       // 1 = the material byte layer goes into the tap too (C5HF flag bit 1)
	int tapChunkKb = 4096;      // largest view of the tap section mapped at once (KiB, 1024 .. 65536)
	int depthProbe = 0;         // epic 3200, issue 3208: log the EXE's depth clears and depth-stencil states (diagnostic)
	int lockstepProbe = 0;      // issue 9039: time from UpdateViews to the scene-clear list, per frame (diagnostic)
	// issue 3304 (lockstep, hand-over, depth; vp_frame.h, research/05-graphics-composition/renderer-in-server-design.md 3-5)
	int lockstep = 1;           // 1 = publish the camera at UpdateViews (the 9039 hook) and wait for that frame at the insertion; 1 since 0490
	uint32_t boundUs = VPFRAME_BOUND_US_DEFAULT;   // lockstep_bound_us, clamped 0..VPFRAME_BOUND_US_MAX (0 = never wait)
	int giveup = VPFRAME_GIVEUP_DEFAULT;           // lockstep_giveup: misses in a row before no wait, clamped 1..1000
	// ticket 0691 (D242): lockstep_mode=exact|bounded (exact: no bound, no give-up; bounded: the two keys above, tests and
	// A/B only); lockstep_hung_ms ends an exact wait (VPFRAME_HUNG_MS_*); hung_sink_test=1 logs the proxy call (tests)
	int lockstepExact = 1, hungMs = VPFRAME_HUNG_MS_DEFAULT, hungSinkTest = 0, modeNote = 0, hungNote = 0;
	wchar_t modeText[24] = L"", hungText[24] = L"";
	int handover = HO_RING;     // HO_V3 (keyed mutex + copy, shell_shared.h) or HO_RING (shell/frame ring_source); HO_RING since 0490
	int depthWrite = 1;         // 1 = write the server's depth of the drawn frame into the EXE's scene-clear DSV; 1 since 0490
	int lockstepCsv = 0;        // 1 = per-frame records into shell_composite.lockstep.csv
	int lockstepCheck = 0;      // 1 = premise 3: read the camera again at Present and compare it with the UpdateViews read
	int pacing = 1;             // ticket 0688: 1 = the pacing instruments (vp_pacing.h; "pacing 1s:", "pacing totals:", freezes, vidmem); 0 = off
	int ringSlots = VPFRAME_DEFAULT_SLOTS;         // handover=ring: ring_slots, clamped VPFRAME_MIN_SLOTS..VPFRAME_MAX_SLOTS
	int ringSync = VPSYNC_FENCE;                   // handover=ring: ring_sync=fence|cpu
	int boundNote = 0, giveupNote = 0, handoverNote = 0, ringSlotsNote = 0, ringSyncNote = 0;   // 1 = not a number/name, 2 = clamped
	wchar_t boundText[24] = L"", giveupText[24] = L"", handoverText[24] = L"", ringSlotsText[24] = L"", ringSyncText[24] = L"";
	wchar_t section[128] = VPSHELL_SECTION_DEFAULT;
	wchar_t shotDir[MAX_PATH] = L"";
	Config() { for (int& s : hookSw) s = 1; }
};

static HMODULE g_module;
static wchar_t g_dir[MAX_PATH];
// Issue 0148: the folder of every file this DLL writes (the log, the per-frame CSVs, shots): VPSHELL_OUT_DIR when set (the
// proxy sets it to a folder outside the game's MODS tree), else the DLL's folder. The ini and the trigger files stay in g_dir
// (they are only read; a trigger is deleted once when a test made it). The EXE watches its MODS tree for changes and answers
// every write under it with a stall of its main thread of ~150 ms on a huge VP game
// (research/05-graphics-composition/renderer-in-server-design.md section 4.14).
static wchar_t g_outDir[MAX_PATH];
static const char* g_outNote = "the DLL's folder (VPSHELL_OUT_DIR not set)";
static wchar_t g_logNote[MAX_PATH + 96];   // ticket 0385: where the previous log went (PrepareDefaultLog), logged at the start
static wchar_t g_keepNote[200];            // ticket 0747: what PruneKeptLogs did, logged at the start beside g_logNote
static wchar_t g_iniPath[MAX_PATH];
static CRITICAL_SECTION g_cfgLock;
static Config g_cfgShared;
static volatile LONG g_cfgVersion = 0;
static Config g_cfg;                 // render-thread copy (worker threads read it too: plain values, benign races)
static LONG g_cfgApplied = -1;

static int ParseEnum(const wchar_t* v, const char* const* names, int n, int def)
{
	for (int i = 0; i < n; ++i)
	{
		wchar_t w[32]; size_t c; mbstowcs_s(&c, w, names[i], _TRUNCATE);
		if (!_wcsicmp(v, w)) return i;
	}
	if (iswdigit(v[0])) return _wtoi(v);
	return def;
}

// Issue 3016 (verification 9031, V3): a rule that is present but parses to no token (" , "), or that holds a character
// outside printable ASCII (an ini saved as UTF-8: wcstombs_s in the C locale failed with EILSEQ and left the text
// empty), used to stay `empty` and match every list. Now the text is converted by hand (no locale), and both cases
// make the rule impossible (matches nothing) with the reason in `bad`, logged once per config load (LogConfig).
static void ParseCond(const wchar_t* w, Cond& c)
{
	c = Cond();
	size_t i = 0;
	for (; w[i] && i < sizeof(c.text) - 1; ++i)
	{
		wchar_t ch = w[i];
		if (ch < 0x20 || ch > 0x7E)
		{
			_snprintf_s(c.bad, _TRUNCATE, "character U+%04X at position %u is not printable ASCII", (unsigned)ch, (unsigned)i + 1);
			c.text[i] = '?';
			continue;
		}
		c.text[i] = (char)ch;
	}
	c.text[i] = 0;
	if (c.bad[0]) { c.empty = false; c.impossible = true; return; }
	char buf[128]; strcpy_s(buf, c.text);
	char* ctx = nullptr;
	for (char* tok = strtok_s(buf, ", +", &ctx); tok; tok = strtok_s(nullptr, ", +", &ctx))
	{
		c.empty = false;
		bool neg = tok[0] == '!';
		const char* t = neg ? tok + 1 : tok;
		int bit = -1;
		for (int i = 0; i < kNumTags; ++i) if (!_stricmp(t, kTagNames[i])) bit = i;
		if (bit >= 0) { (neg ? c.forbid : c.req) |= 1u << bit; continue; }
		if (!_strnicmp(t, "dsv_fmt=", 8)) { c.dsvFmt = atoi(t + 8); continue; }
		if (!_strnicmp(t, "rt_fmt=", 7)) { c.rtFmt = atoi(t + 7); continue; }
		if (!_strnicmp(t, "ctx=", 4)) { c.ctxIdx = atoi(t + 4); continue; }
		if (!_strnicmp(t, "nclears>=", 9)) { c.minClears = atoi(t + 9); continue; }
		c.impossible = true; // unknown token
	}
	if (c.empty && c.text[0])
	{
		// present, but nothing left after parsing (only separators): matches nothing, not everything
		strcpy_s(c.bad, "no token after parsing");
		c.empty = false; c.impossible = true;
	}
}

// issue 3304: an integer key clamped to lo..hi. note = 0 as given, 1 = not a number (def used), 2 = out of range (clamped).
// The whole value must be a number (a trailing word is "not a number", unlike timeout_ms's older parse).
static long long ParseClampedInt(const wchar_t* v, long long def, long long lo, long long hi, int& note, wchar_t* text, size_t textN)
{
	wcsncpy_s(text, textN, v, _TRUNCATE);
	wchar_t* end = nullptr;
	errno = 0;
	long long t = wcstoll(v, &end, 10);
	while (end && *end && iswspace(*end)) ++end;
	if (end == v || !end || *end || errno == ERANGE) { note = 1; return def; }
	if (t < lo || t > hi) { note = 2; return std::max(lo, std::min(hi, t)); }
	note = 0;
	return t;
}

// tap_test values (issue 3203, test only; see the tap section below)
enum { TAPT_NONE, TAPT_SYNTH, TAPT_SMALL, TAPT_BAD_SIZE, TAPT_BAD_TILES, TAPT_BAD_STATE, TAPT_BAD_INDEX, TAPT_NULL_DIR,
	TAPT_WRAP_PTR, TAPT_BAD_DIR, TAPT_BAD_POOL, TAPT_TOO_BIG, TAPT_CHANGE, TAPT_BAD_SECTION, TAPT_NOT_READY, TAPT_COUNT };
static const char* kTapTestNames[TAPT_COUNT] = { "-", "synth", "small", "bad-size", "bad-tiles", "bad-state", "bad-index",
	"null-dir", "wrap-ptr", "bad-dir", "bad-pool", "too-big", "change", "bad-section", "not-ready" };

// Ticket 0490: where ReadConfig takes values from. CFG_ENV | CFG_INI is the configuration the DLL runs; CFG_INI alone is
// compared with CFG_NONE (the built-in defaults, through the same parse) to log how the ini differs from the defaults.
enum { CFG_NONE = 0, CFG_ENV = 1, CFG_INI = 2, CFG_ALL = 3 };

static void ReadConfig(Config& c, int sources = CFG_ALL)
{
	Config d;
	c = d;
	wchar_t v[MAX_PATH];
	auto get = [&](const wchar_t* key, wchar_t* out, DWORD n) -> bool {
		out[0] = 0;
		wchar_t env[64] = L"VPSHELL_";
		for (const wchar_t* k = key; *k && wcslen(env) < 63; ++k) { size_t l = wcslen(env); env[l] = (wchar_t)towupper(*k); env[l + 1] = 0; }
		if ((sources & CFG_ENV) && GetEnvironmentVariableW(env, out, n) > 0) return true;
		out[0] = 0;
		if (!(sources & CFG_INI)) return false;
		GetPrivateProfileStringW(L"composite", key, L"\x01", out, n, g_iniPath);
		if (out[0] == 1) { out[0] = 0; return false; }
		return true; // present, possibly empty
	};
	if (get(L"mode", v, MAX_PATH) && v[0]) c.mode = ParseEnum(v, kModeNames, 3, d.mode);
	if (get(L"insert", v, MAX_PATH) && v[0]) c.insert = ParseEnum(v, kInsNames, 6, d.insert);
	if (get(L"nth", v, MAX_PATH) && v[0]) c.nth = std::max(1, _wtoi(v));
	if (get(L"tag_rule", v, MAX_PATH) && v[0]) c.tagRule = ParseEnum(v, kRuleNames, 3, d.tagRule);
	if (c.tagRule < 0 || c.tagRule > 2) c.tagRule = d.tagRule;
	bool haveMatch = get(L"tag_match", v, MAX_PATH) && v[0];
	ParseCond(haveMatch ? v : (c.tagRule == RULE_BEFORE ? L"bind_bb_dsv" : L"clear_bb"), c.match);
	bool haveAnchor = get(L"tag_anchor", v, MAX_PATH);
	if (haveAnchor && v[0] == L'-') v[0] = 0; // "-" = explicitly no anchor
	ParseCond(haveAnchor ? v : (c.tagRule == RULE_BEFORE ? L"clear_bb" : L""), c.anchor);
	if (get(L"tag_nth", v, MAX_PATH) && v[0]) c.tagNth = std::max(1, _wtoi(v));
	if (get(L"tag_keep", v, MAX_PATH) && v[0]) c.tagKeep = _wtoi(v);
	if (get(L"adopt_on_create", v, MAX_PATH) && v[0]) c.adoptOnCreate = _wtoi(v);
	for (int s = 0; s < SW_COUNT; ++s)
	{
		wchar_t key[48]; _snwprintf_s(key, _TRUNCATE, L"hook_%S", kSwNames[s]);
		if (get(key, v, MAX_PATH) && v[0]) c.hookSw[s] = _wtoi(v) != 0;
	}
	if (get(L"timeout_ms", v, MAX_PATH) && v[0])
	{
		// Issue 3014 (verification 9031, V2): `(DWORD)_wtoi("-1")` was INFINITE, and the presenting thread then waited for
		// good on a texture the server had replaced. Parsed as a 64-bit number and clamped to 0 .. kMaxTimeoutMs.
		wcsncpy_s(c.timeoutText, v, _TRUNCATE);
		wchar_t* end = nullptr;
		long long t = wcstoll(v, &end, 10);
		if (end == v) { c.timeoutMs = 0; c.timeoutNote = 1; }
		else
		{
			c.timeoutMs = (DWORD)std::max(0LL, std::min((long long)kMaxTimeoutMs, t));
			if (t < 0 || t > (long long)kMaxTimeoutMs) c.timeoutNote = 2;
		}
	}
	if (get(L"blend", v, MAX_PATH) && v[0]) c.alphaBlend = !_wcsicmp(v, L"alpha") || !wcscmp(v, L"1");
	if (get(L"hide_when_stale", v, MAX_PATH) && v[0]) c.hideWhenStale = _wtoi(v);
	if (get(L"stale_ms", v, MAX_PATH) && v[0]) c.staleMs = _wtoi(v);
	if (get(L"request_resize", v, MAX_PATH) && v[0]) c.requestResize = _wtoi(v);
	if (get(L"stats_ms", v, MAX_PATH) && v[0]) c.statsMs = std::max(100, _wtoi(v));
	if (get(L"trace_frames", v, MAX_PATH) && v[0]) c.traceFrames = _wtoi(v);
	if (get(L"camera", v, MAX_PATH) && v[0]) c.camera = _wtoi(v) != 0;
	if (get(L"camera_test", v, MAX_PATH)) c.cameraTest = !_wcsicmp(v, L"stall") ? 1 : !_wcsicmp(v, L"corrupt") ? 2 : !_wcsicmp(v, L"late") ? 3 : 0;
	if (get(L"camera_test_late", v, MAX_PATH) && v[0]) c.cameraTestLate = std::max(0, std::min(100000, _wtoi(v)));
	if (get(L"fault_test", v, MAX_PATH)) c.faultTest = !_wcsicmp(v, L"composite") ? 1 : !_wcsicmp(v, L"present") ? 2 : !_wcsicmp(v, L"exec") ? 3 : !_wcsicmp(v, L"uv") ? 4 : 0;   // 4 = the UpdateViews publish (issue 3304)
	if (get(L"hook_test", v, MAX_PATH)) c.hookTest = !_wcsicmp(v, L"foreign");
	if (get(L"bake_tap", v, MAX_PATH) && v[0]) c.bakeTap = _wtoi(v) != 0;
	if (get(L"water_tap", v, MAX_PATH) && v[0]) c.waterTap = _wtoi(v) != 0;   // epic 3600, issue 3603
	if (get(L"overlay_tap", v, MAX_PATH) && v[0]) c.overlayTap = _wtoi(v) != 0;   // epic 0167, issue 3703
	if (get(L"decal_tap", v, MAX_PATH) && v[0]) c.decalTap = _wtoi(v) != 0;   // epic 0166, issue 4102
	if (get(L"shadow_tap", v, MAX_PATH) && v[0]) c.shadowTap = _wtoi(v) != 0;   // epic 0169, issue 4304
	if (get(L"ice_tap", v, MAX_PATH) && v[0]) c.iceTap = _wtoi(v) != 0;   // epic 0169, issue 4307
	if (get(L"forest_tap", v, MAX_PATH) && v[0]) c.forestTap = _wtoi(v) != 0;   // ticket 0420
	if (get(L"cut_tap", v, MAX_PATH) && v[0]) c.cutTap = _wtoi(v) != 0;   // ticket 4553
	if (get(L"farm_guard", v, MAX_PATH) && v[0]) c.farmGuard = _wtoi(v) != 0;   // ticket 0527
	if (get(L"farm_builder", v, MAX_PATH) && v[0]) c.farmBuilder = _wtoi(v) != 0;   // ticket 4853
	if (get(L"model_fow", v, MAX_PATH) && v[0]) c.modelFow = _wtoi(v) != 0;   // ticket 0490 (logged and compared; fow_models.cpp applies it)
	if (get(L"unit_filter", v, MAX_PATH) && v[0])   // issue 4752
	{
		wcsncpy_s(c.unitFilterText, v, _TRUNCATE);
		c.unitFilter = !_wcsicmp(v, L"auto") ? UF_MODE_AUTO : !_wcsicmp(v, L"on") ? UF_MODE_ON : !_wcsicmp(v, L"off") ? UF_MODE_OFF : -1;
		if (c.unitFilter < 0) { c.unitFilter = UF_MODE_AUTO; c.unitFilterNote = 1; }
	}
	if (get(L"model_filter", v, MAX_PATH) && v[0])   // ticket 4874
	{
		wcsncpy_s(c.modelFilterText, v, _TRUNCATE);
		c.modelFilter = !_wcsicmp(v, L"auto") ? UF_MODE_AUTO : !_wcsicmp(v, L"on") ? UF_MODE_ON : !_wcsicmp(v, L"off") ? UF_MODE_OFF : -1;
		if (c.modelFilter < 0) { c.modelFilter = UF_MODE_AUTO; c.modelFilterNote = 1; }
	}
	if (get(L"model_tap_passed", v, MAX_PATH) && v[0]) c.modelTapPassed = _wtoi(v) != 0;   // ticket 4874 (TEST ONLY)
	if (get(L"model_tap_slot_kb", v, MAX_PATH) && v[0])   // ticket 4874
	{
		wcsncpy_s(c.modelTapSlotText, v, _TRUNCATE);
		wchar_t* end = nullptr;
		const long k = wcstol(v, &end, 10);
		if (end == v || *end || k < (long)VPSHELL_MODELTAP_SLOT_KB_MIN || k > (long)VPSHELL_MODELTAP_SLOT_KB_MAX || k % 64)
			{ c.modelTapSlotKb = VPSHELL_MODELTAP_SLOT_KB_DEFAULT; c.modelTapSlotNote = 1; }
		else c.modelTapSlotKb = (int)k;
	}
	if (get(L"tap", v, MAX_PATH) && v[0]) c.tap = _wtoi(v) != 0;
	if (get(L"tap_test", v, MAX_PATH) && v[0])
	{
		c.tapTest = TAPT_NONE;
		for (int i = 1; i < TAPT_COUNT; ++i) { wchar_t w[32]; size_t n; mbstowcs_s(&n, w, kTapTestNames[i], _TRUNCATE); if (!_wcsicmp(v, w)) c.tapTest = i; }
	}
	if (get(L"tap_test_load", v, MAX_PATH) && v[0]) c.tapTestLoad = _wtoi(v);
	if (get(L"tap_ack_timeout_ms", v, MAX_PATH) && v[0]) c.tapAckTimeoutMs = (DWORD)std::max(100, std::min(3600000, _wtoi(v)));
	if (get(L"tap_delay_frames", v, MAX_PATH) && v[0]) c.tapDelayFrames = std::max(1, std::min(100000, _wtoi(v)));
	if (get(L"tap_scene_frames", v, MAX_PATH) && v[0]) c.tapSceneFrames = std::max(0, std::min(100000, _wtoi(v)));
	if (get(L"tap_first_load", v, MAX_PATH) && v[0]) c.tapFirstLoad = _wtoi(v) != 0;   // ticket 0408
	if (get(L"tap_first_delay_frames", v, MAX_PATH) && v[0]) c.tapFirstDelayFrames = std::max(1, std::min(100000, _wtoi(v)));
	if (get(L"tap_byte_layer", v, MAX_PATH) && v[0]) c.tapByteLayer = _wtoi(v) != 0;
	if (get(L"tap_chunk_kb", v, MAX_PATH) && v[0]) c.tapChunkKb = std::max(1024, std::min(65536, _wtoi(v)));
	if (get(L"depth_probe", v, MAX_PATH) && v[0]) c.depthProbe = _wtoi(v) != 0;
	if (get(L"lockstep_probe", v, MAX_PATH) && v[0]) c.lockstepProbe = _wtoi(v) != 0;
	if (get(L"lockstep", v, MAX_PATH) && v[0]) c.lockstep = _wtoi(v) != 0;
	if (get(L"lockstep_bound_us", v, MAX_PATH) && v[0])
		c.boundUs = (uint32_t)ParseClampedInt(v, VPFRAME_BOUND_US_DEFAULT, 0, VPFRAME_BOUND_US_MAX, c.boundNote, c.boundText, _countof(c.boundText));
	if (get(L"lockstep_giveup", v, MAX_PATH) && v[0])
		c.giveup = (int)ParseClampedInt(v, VPFRAME_GIVEUP_DEFAULT, 1, 1000, c.giveupNote, c.giveupText, _countof(c.giveupText));
	if (get(L"lockstep_mode", v, MAX_PATH) && v[0])   // ticket 0691
	{
		wcsncpy_s(c.modeText, v, _TRUNCATE);
		if (!_wcsicmp(v, L"exact")) c.lockstepExact = 1;
		else if (!_wcsicmp(v, L"bounded")) c.lockstepExact = 0;
		else { c.lockstepExact = 1; c.modeNote = 1; }
	}
	if (get(L"lockstep_hung_ms", v, MAX_PATH) && v[0])
		c.hungMs = (int)ParseClampedInt(v, VPFRAME_HUNG_MS_DEFAULT, VPFRAME_HUNG_MS_MIN, VPFRAME_HUNG_MS_MAX, c.hungNote, c.hungText, _countof(c.hungText));
	if (get(L"hung_sink_test", v, MAX_PATH) && v[0]) c.hungSinkTest = _wtoi(v) != 0;
	if (get(L"handover", v, MAX_PATH) && v[0])
	{
		wcsncpy_s(c.handoverText, v, _TRUNCATE);
		if (!_wcsicmp(v, L"v3")) c.handover = HO_V3;
		else if (!_wcsicmp(v, L"ring")) c.handover = HO_RING;
		else { c.handover = HO_V3; c.handoverNote = 1; }
	}
	if (get(L"depth_write", v, MAX_PATH) && v[0]) c.depthWrite = _wtoi(v) != 0;
	if (get(L"lockstep_csv", v, MAX_PATH) && v[0]) c.lockstepCsv = _wtoi(v) != 0;
	if (get(L"lockstep_check", v, MAX_PATH) && v[0]) c.lockstepCheck = _wtoi(v) != 0;
	if (get(L"pacing", v, MAX_PATH) && v[0]) c.pacing = _wtoi(v) != 0;   // ticket 0688
	if (get(L"ring_slots", v, MAX_PATH) && v[0])
		c.ringSlots = (int)ParseClampedInt(v, VPFRAME_DEFAULT_SLOTS, VPFRAME_MIN_SLOTS, VPFRAME_MAX_SLOTS, c.ringSlotsNote, c.ringSlotsText, _countof(c.ringSlotsText));
	if (get(L"ring_sync", v, MAX_PATH) && v[0])
	{
		wcsncpy_s(c.ringSyncText, v, _TRUNCATE);
		if (!_wcsicmp(v, L"fence")) c.ringSync = VPSYNC_FENCE;
		else if (!_wcsicmp(v, L"cpu")) c.ringSync = VPSYNC_CPU;
		else { c.ringSync = VPSYNC_FENCE; c.ringSyncNote = 1; }
	}
	if (get(L"section", v, MAX_PATH) && v[0]) wcsncpy_s(c.section, v, _TRUNCATE);
	if (get(L"shot_dir", v, MAX_PATH) && v[0]) wcsncpy_s(c.shotDir, v, _TRUNCATE);
	if (c.mode < 0 || c.mode > 2) c.mode = d.mode;
	if (c.insert < 0 || c.insert > 5) c.insert = d.insert;
}

// ===========================================================================
// Logging
// ===========================================================================
static CRITICAL_SECTION g_logLock;
static FILE* g_log;
static LARGE_INTEGER g_freq;

static int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double QpcToMs(int64_t d) { return (double)d * 1000.0 / (double)g_freq.QuadPart; }

static void Log(const char* fmt, ...)
{
	if (!g_log) return;
	char buf[2048];
	SYSTEMTIME st; GetLocalTime(&st);
	int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02d:%02d:%02d.%03d [%5u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
	va_list ap; va_start(ap, fmt);
	_vsnprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, fmt, ap);
	va_end(ap);
	EnterCriticalSection(&g_logLock);
	fputs(buf, g_log); fputc('\n', g_log); fflush(g_log);
	LeaveCriticalSection(&g_logLock);
}

static void TagsText(uint32_t bits, char* out, size_t n)
{
	out[0] = 0;
	for (int i = 0; i < kNumTags; ++i)
		if (bits & (1u << i)) { if (out[0]) strcat_s(out, n, "|"); strcat_s(out, n, kTagNames[i]); }
	if (!out[0]) strcpy_s(out, n, "-");
}

// ===========================================================================
// Render-thread state
// ===========================================================================
static __declspec(thread) int t_inComposite;   // our own D3D calls pass through every hook untouched
static __declspec(thread) int t_inPresent;
static __declspec(thread) int t_inCreateSc;
static volatile LONG g_disabled;
static volatile LONG g_shotRequest;
static volatile LONG g_traceRequest;
static volatile LONG g_presentSeen;
static volatile LONG g_scCreatedTick;           // GetTickCount of the first swap chain created through the hooked factory (V9)
static volatile LONG g_discovered;              // dummy-device discovery done

static IDXGISwapChain* g_target;
static IDXGISwapChain* volatile g_pendingAdopt; // created through the factory, adopted at its first Present
// Issue 0141: a chain's identity is its address AND its creation number. A chain recreated on the same window (the
// game's resolution change) can get the released old chain's address (heap reuse); by address alone it then looked
// like the current target and was never adopted, so the per-chain state (back buffer, views) stayed the old chain's.
static volatile LONG g_scSeq;                   // swap chains created through the hooked factory so far
static volatile LONG g_pendingSeq;              // creation number of g_pendingAdopt (written before the pointer)
static LONG g_targetSeq;                        // creation number of g_target; 0 = adopted without a seen creation
static ID3D11Device* g_dev;
static ID3D11Device1* g_dev1;
static ID3D11DeviceContext* volatile g_ctx;     // the target's immediate context
static ID3D11Resource* volatile g_bbRes;        // raw pointer of buffer 0 (identity only)
static UINT g_bbW, g_bbH;
static DXGI_FORMAT g_bbFormat;
static UINT g_bbSamples;
static uint64_t g_clientLuid;
static int64_t g_lastTargetPresent;
static IDXGISwapChain* g_otherChains[8];
static HWND g_targetHwnd;                       // issue 3017: the target's window (the game's main window)
static int64_t g_otherUnlisted, g_otherUnlistedLogged, g_otherUnlistedLogQpc;   // Presents of other chains beyond the 8 listed

// our D3D objects; the deferred-context draw (inlist) reads them on recording threads under g_resLock (shared)
static SRWLOCK g_resLock = SRWLOCK_INIT;
static ID3D11VertexShader* g_vs;
static ID3D11PixelShader* g_ps;
static ID3D11SamplerState* g_smp;
static ID3D11BlendState* g_bsOpaque;
static ID3D11BlendState* g_bsAlpha;
static ID3D11DepthStencilState* g_dssOff;
static ID3D11RasterizerState* g_rs;
static ID3D11PixelShader* g_psDepth;             // issue 3304: colour + SV_Depth in one pass (depth_write=1)
static ID3D11DepthStencilState* g_dssWrite;      // depth on, func ALWAYS, write ALL, stencil off (untouched)
static ID3D11SamplerState* g_smpPoint;           // the depth texture (point, clamp)
static ID3D11RenderTargetView* g_bbRtv;
static volatile bool g_resourcesOk;
// issue 3304: the server's depth of the version-3 hand-over (VPShellDepth: a second shared texture, R32_FLOAT, copied
// under the colour's keyed-mutex cycle into a local texture; main thread only)
static ID3D11Texture2D* g_depthShared;
static ID3D11Texture2D* g_localDepth;
static ID3D11ShaderResourceView* g_srvDepthLocal;
static uint32_t g_depthGen, g_depthPid;
static int64_t g_depthNextTry;
static int64_t g_colourFc = -1, g_depthFc = -1;  // the server frame (frameCounter) the local colour / depth copy holds

// shared texture
static HANDLE g_section;
static VPShellHeader* g_hdr;
static int64_t g_nextSectionTry;
static ID3D11Texture2D* g_sharedTex;
static IDXGIKeyedMutex* g_km;
static ID3D11Texture2D* g_localTex;
static ID3D11ShaderResourceView* g_srvUnorm;
static ID3D11ShaderResourceView* g_srvSrgb;
static UINT g_localW, g_localH;
static DXGI_FORMAT g_localFmt;
static uint32_t g_openPid, g_openGen;
static int64_t g_nextOpenTry;
static volatile bool g_haveContent;
static int64_t g_lastServerFrame = -1;
static int64_t g_contentQpc;
static volatile bool g_stale;
static bool g_noSectionLogged;
static uint32_t g_reqPid; static UINT g_reqW, g_reqH;
static int64_t g_consumed;

// per-frame (immediate context)
static int g_clearCount, g_omsetCount, g_omsetBbCount, g_execCount;
static bool g_inserted;
static bool g_freshThisFrame;
static bool g_acquiredThisFrame;
static int64_t g_prevPresentQpc;
static int g_traceLeft;
static volatile int64_t g_frameIndex;
// tagged-insertion frame state
static bool g_anchorSeen, g_clearSeen, g_3dStarted, g_sawNotReady;
static int g_matchCount;
// issue 3005: the trace keeps every format, the clear and bind counts and the sizes of the first non-back-buffer
// target and the first depth view (sizes recorded only while a trace is pending: g_traceWanted)
struct FrameList { ID3D11CommandList* cl; uint32_t bits; int ctxIdx; int rt0, ds0; bool known; char ev; bool wrong;
	uint8_t nClears, nBinds, nRt, nDs; int16_t rt[4], ds[4]; uint16_t rtW, rtH, dsW, dsH; };
static volatile LONG g_traceWanted;
static volatile LONG g_listShotRequest;   // issue 3005: first k requested by the listshot trigger
static int g_listShotK, g_listShotTo;
static bool g_listShotDone;
static FrameList g_fl[128];
static int g_nFl;

// per-interval statistics
static const int kMaxSamples = 16384;
static float g_ft[kMaxSamples]; static int g_nFt;
static float g_cc[kMaxSamples]; static int g_nCc;
static float g_lat[kMaxSamples]; static int g_nLat;
static float g_acq[kMaxSamples]; static int g_nAcq;
static float g_gpu[kMaxSamples]; static int g_nGpu;
static CRITICAL_SECTION g_recLock;              // deferred-record samples come from recording threads
static float g_rec[kMaxSamples]; static int g_nRec;

struct GpuTimer { ID3D11Query* dj; ID3D11Query* t0; ID3D11Query* tm; ID3D11Query* t1; bool pending; bool fresh; };
static GpuTimer* g_gtCur;
static float g_gpuW[kMaxSamples]; static int g_nGpuW;
static float g_gpuF[kMaxSamples]; static int g_nGpuF;
static GpuTimer g_gt[8];
static int g_gtNext;
static int g_noNewFrame;
static double g_ageSum; static int g_nAge;
static int g_srvNew, g_srvDrop, g_dups, g_timeouts, g_missedIns, g_clearsTotal, g_framesInt;
static int g_missNoMatch, g_missNoAnchor, g_missNoExec, g_missNotReady, g_insWrong, g_insChecked, g_listsTotal, g_untagged, g_noDraw;
static int64_t g_intervalStart;

static void Push(float* a, int& n, double v) { if (n < kMaxSamples) a[n++] = (float)v; }

// fault_test (issue 3013, tests only): one access violation raised inside the hook body named by `where`, once per
// process, so the test sees exactly what a real fault there does (the SEH guard, DISABLED, the host running on).
static volatile LONG g_faultFired;
static void MaybeFault(int where)
{
	if (g_cfg.faultTest == where && InterlockedExchange(&g_faultFired, 1) == 0)
		RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
}

static void MeanP99(float* a, int n, double& mean, double& p99, double& mx)
{
	mean = p99 = mx = 0;
	if (!n) return;
	double s = 0; for (int i = 0; i < n; ++i) { s += a[i]; if (a[i] > mx) mx = a[i]; }
	mean = s / n;
	int k = (int)((n - 1) * 0.99);
	std::nth_element(a, a + k, a + n);
	p99 = a[k];
}

// ===========================================================================
// Shared section / shared texture
// ===========================================================================
static void CloseShared()
{
	SafeRelease(g_km);
	SafeRelease(g_sharedTex);
	g_openPid = 0; g_openGen = 0;
	g_lastServerFrame = -1;
}

static void ReleaseLocalLocked()
{
	SafeRelease(g_srvUnorm);
	SafeRelease(g_srvSrgb);
	SafeRelease(g_localTex);
	g_localW = g_localH = 0;
	g_haveContent = false;
	g_colourFc = -1;
}

static void CloseDepthShared()
{
	SafeRelease(g_depthShared);
	AcquireSRWLockExclusive(&g_resLock);
	SafeRelease(g_srvDepthLocal); SafeRelease(g_localDepth);
	ReleaseSRWLockExclusive(&g_resLock);
	g_depthGen = g_depthPid = 0;
	g_depthFc = -1;
}

static void LkCloseSource(const char* why);   // issue 3304, below: the frame source lets go of every D3D object first

static void ReleaseAll(const char* why)
{
	LkCloseSource(why);
	CloseShared();
	CloseDepthShared();
	AcquireSRWLockExclusive(&g_resLock);
	ReleaseLocalLocked();
	SafeRelease(g_bbRtv);
	SafeRelease(g_vs); SafeRelease(g_ps); SafeRelease(g_smp);
	SafeRelease(g_bsOpaque); SafeRelease(g_bsAlpha); SafeRelease(g_dssOff); SafeRelease(g_rs);
	SafeRelease(g_psDepth); SafeRelease(g_dssWrite); SafeRelease(g_smpPoint);
	g_resourcesOk = false;
	ReleaseSRWLockExclusive(&g_resLock);
	for (auto& q : g_gt) { SafeRelease(q.dj); SafeRelease(q.t0); SafeRelease(q.tm); SafeRelease(q.t1); q.pending = false; }
	g_bbRes = nullptr;
	BakeTap_Shutdown(why);   // epic 3200, issue 3204
	WaterTap_Shutdown(why);  // epic 3600, issue 3603
	OverlayTap_Shutdown(why);   // epic 0167, issue 3703
	DecalTap_Shutdown(why);     // epic 0166, issue 4102
	ShadowTap_Shutdown(why);    // epic 0169, issue 4304
	IceTap_Shutdown(why);       // epic 0169, issue 4307
	ForestTap_Shutdown(why);    // ticket 0420
	CutTap_Shutdown(why);       // ticket 4553: nothing depends on the device; the worker goes on
	FowModels_Shutdown(why);    // ticket 0339: the white view and FOWAnim put back
	Log("released all composite resources (%s)", why);
}

static bool EnsureResources()
{
	if (g_resourcesOk) return true;
	HRESULT hr;
	AcquireSRWLockExclusive(&g_resLock);
	bool ok = false;
	do
	{
		if (FAILED(hr = g_dev->CreateVertexShader(g_VSFullscreen, sizeof(g_VSFullscreen), nullptr, &g_vs)) ||
			FAILED(hr = g_dev->CreatePixelShader(g_PSComposite, sizeof(g_PSComposite), nullptr, &g_ps)))
		{ Log("shader creation failed 0x%08X - disabling composite", hr); InterlockedExchange(&g_disabled, 1); break; }
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		D3D11_BLEND_DESC bd = {};
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_BLEND_DESC ba = bd;
		ba.RenderTarget[0].BlendEnable = TRUE;
		ba.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; ba.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; ba.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		ba.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; ba.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; ba.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		D3D11_DEPTH_STENCIL_DESC dd = {};
		dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS; dd.StencilEnable = FALSE;
		D3D11_RASTERIZER_DESC rd = {};
		rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
		if (FAILED(hr = g_dev->CreateSamplerState(&sd, &g_smp)) ||
			FAILED(hr = g_dev->CreateBlendState(&bd, &g_bsOpaque)) ||
			FAILED(hr = g_dev->CreateBlendState(&ba, &g_bsAlpha)) ||
			FAILED(hr = g_dev->CreateDepthStencilState(&dd, &g_dssOff)) ||
			FAILED(hr = g_dev->CreateRasterizerState(&rd, &g_rs)))
		{ Log("state creation failed 0x%08X - disabling composite", hr); InterlockedExchange(&g_disabled, 1); break; }
		// issue 3304 (depth_write): a failure here only turns the depth write off (logged), never the composite
		D3D11_SAMPLER_DESC sp = sd; sp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		D3D11_DEPTH_STENCIL_DESC dw = {};
		dw.DepthEnable = TRUE; dw.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dw.DepthFunc = D3D11_COMPARISON_ALWAYS; dw.StencilEnable = FALSE;
		if (FAILED(hr = g_dev->CreatePixelShader(g_PSCompositeDepth, sizeof(g_PSCompositeDepth), nullptr, &g_psDepth)) ||
			FAILED(hr = g_dev->CreateDepthStencilState(&dw, &g_dssWrite)) ||
			FAILED(hr = g_dev->CreateSamplerState(&sp, &g_smpPoint)))
		{
			Log("depth-write objects not created 0x%08X - depth_write has no effect", hr);
			SafeRelease(g_psDepth); SafeRelease(g_dssWrite); SafeRelease(g_smpPoint);
		}
		ok = true;
	} while (0);
	g_resourcesOk = ok;
	ReleaseSRWLockExclusive(&g_resLock);
	if (!ok) return false;
	D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }, qt = { D3D11_QUERY_TIMESTAMP, 0 };
	for (auto& q : g_gt)
	{
		if (FAILED(g_dev->CreateQuery(&qd, &q.dj)) || FAILED(g_dev->CreateQuery(&qt, &q.t0)) || FAILED(g_dev->CreateQuery(&qt, &q.tm)) || FAILED(g_dev->CreateQuery(&qt, &q.t1)))
		{ SafeRelease(q.dj); SafeRelease(q.t0); SafeRelease(q.tm); SafeRelease(q.t1); }
		q.pending = false;
	}
	Log("composite resources created");
	return true;
}

static bool IsSrgb(DXGI_FORMAT f)
{
	return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
		f == DXGI_FORMAT_BC1_UNORM_SRGB || f == DXGI_FORMAT_BC2_UNORM_SRGB || f == DXGI_FORMAT_BC3_UNORM_SRGB || f == DXGI_FORMAT_BC7_UNORM_SRGB;
}

static bool OpenSharedTexture(const VPShellHeader& snap)
{
	CloseShared();
	if (!g_dev1) return false;
	ID3D11Texture2D* tex = nullptr;
	HRESULT hr = g_dev1->OpenSharedResourceByName(snap.textureName, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
		__uuidof(ID3D11Texture2D), (void**)&tex);
	if (FAILED(hr))
	{
		static uint32_t lastPid, lastGen; static HRESULT lastHr; static int repeats;
		if (snap.serverPid != lastPid || snap.generation != lastGen || hr != lastHr)
		{
			Log("OpenSharedResourceByName('%ls') failed 0x%08X (server LUID %016llX, ours %016llX)%s", snap.textureName, hr, snap.adapterLuid, g_clientLuid,
				repeats ? " [earlier failure repeated]" : "");
			lastPid = snap.serverPid; lastGen = snap.generation; lastHr = hr; repeats = 0;
		}
		else repeats++;
		return false;
	}
	IDXGIKeyedMutex* km = nullptr;
	if (FAILED(hr = tex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&km)))
	{ Log("shared texture has no keyed mutex 0x%08X", hr); tex->Release(); return false; }
	D3D11_TEXTURE2D_DESC td; tex->GetDesc(&td);
	// issue 3018 V5: a new format at the same size needs a new local texture too (CopyResource between textures of
	// different format groups is invalid and copies nothing)
	if (td.Width != g_localW || td.Height != g_localH || td.Format != g_localFmt || !g_localTex)
	{
		if (g_localTex && td.Width == g_localW && td.Height == g_localH)
			Log("shared texture format changed %u -> %u at the same size %ux%u: local texture recreated", g_localFmt, td.Format, td.Width, td.Height);
		AcquireSRWLockExclusive(&g_resLock);
		ReleaseLocalLocked();
		D3D11_TEXTURE2D_DESC ld = td;
		bool bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
		ld.Format = bgra ? DXGI_FORMAT_B8G8R8A8_TYPELESS : td.Format;
		ld.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ld.MiscFlags = 0; ld.CPUAccessFlags = 0; ld.Usage = D3D11_USAGE_DEFAULT;
		bool ok = SUCCEEDED(hr = g_dev->CreateTexture2D(&ld, nullptr, &g_localTex));
		if (ok)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
			sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
			sv.Format = bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : td.Format;
			g_dev->CreateShaderResourceView(g_localTex, &sv, &g_srvUnorm);
			if (bgra) { sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; g_dev->CreateShaderResourceView(g_localTex, &sv, &g_srvSrgb); }
			if (!g_srvUnorm) { ReleaseLocalLocked(); ok = false; }
			else { g_localW = td.Width; g_localH = td.Height; g_localFmt = td.Format; }
		}
		ReleaseSRWLockExclusive(&g_resLock);
		if (!ok) { Log("local texture / SRV creation failed 0x%08X", hr); km->Release(); tex->Release(); return false; }
	}
	g_sharedTex = tex; g_km = km;
	g_openPid = snap.serverPid; g_openGen = snap.generation;
	g_lastServerFrame = -1;
	Log("opened shared texture '%ls' %ux%u fmt %u (server pid %u gen %u)", snap.textureName, td.Width, td.Height, td.Format, snap.serverPid, snap.generation);
	return true;
}

static bool SnapshotTextureBlock(VPShellHeader& out)
{
	for (int i = 0; i < 8; ++i)
	{
		uint32_t s1 = g_hdr->texSeq;
		if (s1 & 1) { YieldProcessor(); continue; }
		MemoryBarrier();
		memcpy(&out, (const void*)g_hdr, sizeof(out));
		MemoryBarrier();
		// issue 3013: the name comes from another process; never let a missing terminator run into the next fields
		out.textureName[_countof(out.textureName) - 1] = 0;
		if (g_hdr->texSeq == s1) return true;
	}
	return false;
}

static void SetStale(bool stale, const char* why)
{
	if (stale != g_stale) Log("server %s (%s)", stale ? "STALE" : "alive again", why);
	g_stale = stale;
}

// Map the section if it is not mapped yet (at most one attempt per second). Render thread only.
static bool EnsureSection(int64_t now)
{
	if (g_hdr) return true;
	if (now < g_nextSectionTry) return false;
	g_nextSectionTry = now + g_freq.QuadPart;
	g_section = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, g_cfg.section);
	if (!g_section)
	{
		if (!g_noSectionLogged) { Log("no server: section '%ls' does not exist (error %u); retrying once per second", g_cfg.section, GetLastError()); g_noSectionLogged = true; }
		SetStale(true, "no section");
		return false;
	}
	g_hdr = (VPShellHeader*)MapViewOfFile(g_section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_SECTION_BYTES);
	if (!g_hdr) { CloseHandle(g_section); g_section = nullptr; return false; }
	Log("opened section '%ls'", g_cfg.section);
	return true;
}

static void PollServer()
{
	int64_t now = Qpc();
	if (!EnsureSection(now)) return;
	g_hdr->clientPid = GetCurrentProcessId();
	g_hdr->clientLuid = g_clientLuid;
	if (g_hdr->magic != VPSHELL_MAGIC) { SetStale(true, "section not initialised"); return; }

	int64_t hb = InterlockedCompareExchange64(&g_hdr->heartbeatQpc, 0, 0);
	char why[64]; _snprintf_s(why, _TRUNCATE, "heartbeat age %.0f ms", QpcToMs(now - hb));
	SetStale(QpcToMs(now - hb) > g_cfg.staleMs, why);

	if (g_hdr->serverPid != g_openPid || g_hdr->generation != g_openGen)
	{
		if (now >= g_nextOpenTry)
		{
			g_nextOpenTry = now + g_freq.QuadPart / 4;
			VPShellHeader snap;
			if (SnapshotTextureBlock(snap) && snap.serverPid) OpenSharedTexture(snap);
		}
	}

	if (g_cfg.requestResize && g_bbW && g_bbH && g_openPid &&
		(g_hdr->width != g_bbW || g_hdr->height != g_bbH) &&
		(g_reqPid != g_openPid || g_reqW != g_bbW || g_reqH != g_bbH))
	{
		g_hdr->reqWidth = g_bbW; g_hdr->reqHeight = g_bbH;
		MemoryBarrier();
		InterlockedIncrement((volatile LONG*)&g_hdr->reqSeq);
		g_reqPid = g_openPid; g_reqW = g_bbW; g_reqH = g_bbH;
		Log("requested server texture %ux%u (server has %ux%u)", g_bbW, g_bbH, g_hdr->width, g_hdr->height);
	}
}

static void LsOnAcquired(int64_t fc, int64_t fq);   // lockstep probe (issue 9039), below
// Issue 3014: the wait this frame may make. Never while the server is STALE, while the section names another texture
// than the one open (a new generation or a new server: the old key never comes back), while a resize is pending, or
// after kTimeoutGiveUp timeouts in a row (g_waitGaveUp: no wait until an AcquireSync succeeds again, which the
// zero-timeout path does as soon as the server publishes a new frame).
static int g_timeoutsInRow;
static bool g_waitGaveUp, g_waitGaveUpLogged;
static int g_waitGaveUpCount;    // per stats interval (wait_off)
static DWORD WaitThisFrame()
{
	DWORD to = g_cfg.timeoutMs;
	if (!to || g_waitGaveUp || g_stale) return 0;
	if (g_hdr->serverPid != g_openPid || g_hdr->generation != g_openGen) return 0;
	if (g_bbW && g_bbH && (g_hdr->width != g_bbW || g_hdr->height != g_bbH)) return 0;
	return std::min(to, kMaxTimeoutMs);
}

static void AcquireAndCopy()
{
	g_acquiredThisFrame = true;
	if (!g_km) return;
	DWORD to = WaitThisFrame();
	if (to == 0 && g_lastServerFrame >= 0 &&
		*(volatile uint32_t*)&g_hdr->frameCounter == (uint32_t)g_lastServerFrame)
	{
		g_noNewFrame++;
		return;
	}
	int64_t a0 = Qpc();
	HRESULT hr = g_km->AcquireSync(1, to);
	Push(g_acq, g_nAcq, QpcToMs(Qpc() - a0));
	if (hr == (HRESULT)WAIT_TIMEOUT && to)
	{
		if (++g_timeoutsInRow >= kTimeoutGiveUp && !g_waitGaveUp)
		{
			g_waitGaveUp = true;
			g_waitGaveUpCount++;
			if (!g_waitGaveUpLogged)
				Log("AcquireSync timed out %d times in a row (timeout_ms=%u): frames no longer wait for the server until it delivers a frame again "
					"(logged once; later fallbacks are counted as wait_off)", g_timeoutsInRow, to);
			g_waitGaveUpLogged = true;
		}
	}
	else if (hr == S_OK) { g_timeoutsInRow = 0; g_waitGaveUp = false; }
	if (hr == S_OK)
	{
		int64_t fc = g_hdr->frameCounter;
		int64_t fq = g_hdr->frameQpc;
		LsOnAcquired(fc, fq);
		if (g_gtCur) g_ctx->End(g_gtCur->tm);
		g_ctx->CopyResource(g_localTex, g_sharedTex);
		g_hdr->clientFramesConsumed = ++g_consumed;
		g_km->ReleaseSync(0);
		if (g_lastServerFrame >= 0)
		{
			int64_t d = fc - g_lastServerFrame;
			if (d > 1) g_srvDrop += (int)(d - 1);
		}
		g_lastServerFrame = fc;
		g_contentQpc = fq;
		g_haveContent = true;
		g_freshThisFrame = true;
		g_srvNew++;
	}
	else if (hr == (HRESULT)WAIT_TIMEOUT)
	{
		g_timeouts++;
	}
	else
	{
		Log("AcquireSync(1) returned 0x%08X (%s) - closing the shared texture", hr, hr == (HRESULT)WAIT_ABANDONED ? "abandoned: server died holding it" : "error");
		CloseShared();
	}
}

// ===========================================================================
// EXE camera (issue 3007)
// ===========================================================================
// Option B of camera-and-picking.md section 6: poll the EXE's view/projection globals at every Present of the target
// swap chain, read twice, check them against the map camera (g_View[0] == inverse(GVS camWorld), wrap views are
// +-period copies of view 0, view mode 1) and publish them into the section's camera block under a seqlock. Nothing of
// the EXE is ever written, not even on the heap (option A's vptr swap was not taken: research section 10 says why).
// The EXE is validated once (its build from the registry, stamp + SizeOfImage, and the byte patterns of exe_camera.h
// for that build, every address rebased on the module base; ticket 4964) before the first read; a refused EXE is never
// read. Every read runs under SEH. The validated build is the one every tap reads its addresses for (VPCamBuild()).
static volatile LONG g_camValidated;    // 0 = not yet, 1 = valid, -1 = refused
static uint32_t g_exeBase;
static int64_t g_camPresents;           // Presents of the target (render thread)
static uint32_t g_camLastStatus = 0xFFFFFFFFu;
static int g_camExceptions;
static int64_t g_camRetryAfter;         // after an exception: no read before this QPC
static bool g_camDead;                  // 5 exceptions: disabled for good
static bool g_camFirstOk;
static int g_camOkI, g_camBadI, g_camRetryI, g_camLogI, g_camLogSuppressed;   // per stats interval
static uint32_t g_camRetryTotal, g_camOkTotal, g_camBadTotal;
static VPShellCamera g_camBlk;           // working copy (render thread)
static uint32_t g_gridSeq;
struct GridKey { uint32_t ptr, ready, sX, sY; float ox, oy; };
static GridKey g_gridKey;
static bool g_gridHaveKey;
static int g_gridRecheck;

static inline uintptr_t CamA(uint32_t va) { return (uintptr_t)(va - VPEXE_IMAGE_BASE + g_exeBase); }
template <class T> static inline T CamRd(uintptr_t a) { return *(const volatile T*)a; }

// Ticket 4964 (epic 0590): the build comes from the build registry (stamp + SizeOfImage, vp_exe_build.h), the
// addresses and patterns from the build table (exe_camera.h, vp_exe_patterns.h). *build gets the build (or unknown).
static bool CamValidateImage(uint32_t base, int* build, char* why, size_t n)
{
	*build = kVpExeBuildUnknown;
	__try
	{
		const unsigned char* b = (const unsigned char*)(uintptr_t)base;
		if (b[0] != 'M' || b[1] != 'Z') { _snprintf_s(why, n, _TRUNCATE, "no MZ header at 0x%08X", base); return false; }
		uint32_t lfanew = *(const uint32_t*)(b + 0x3C);
		if (lfanew > 0x1000 || *(const uint32_t*)(b + lfanew) != 0x4550) { _snprintf_s(why, n, _TRUNCATE, "no PE header (e_lfanew 0x%X)", lfanew); return false; }
		uint32_t tds = 0, soi = 0;
		const int bld = VpExeBuildOfImage(b, 0x1000, &tds, &soi);
		if (bld == kVpExeBuildUnknown)
		{
			_snprintf_s(why, n, _TRUNCATE, "TimeDateStamp 0x%08X SizeOfImage 0x%08X is no EXE build this reader knows (known: %s, %s)",
				tds, soi, VpExeBuildLabel(kVpExeBuild4390913), VpExeBuildLabel(kVpExeBuild26577625));
			return false;
		}
		if (!VpExeBuildSupported(bld))
		{
			_snprintf_s(why, n, _TRUNCATE, "%s: the build table compiled into this DLL is incomplete (regenerate it: tools/exebuild/gen_table.py)", VpExeBuildLabel(bld));
			return false;
		}
		if (VPExeCheckSigs(bld, base, kVPCamSigs, kVPCamNumSigs, nullptr, nullptr, why, n) >= 0)
			return false;
		*build = bld;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		_snprintf_s(why, n, _TRUNCATE, "exception 0x%08X while reading the image", GetExceptionCode());
		return false;
	}
}

// Control thread, once, before the hooks are installed.
static void CamValidate()
{
	wchar_t env[32] = L"";
	uint32_t base = 0;
	bool fake = GetEnvironmentVariableW(L"VPSHELL_EXE_BASE", env, 32) > 0 && env[0];
	if (fake) base = (uint32_t)wcstoul(env, nullptr, 16);
	else base = (uint32_t)(uintptr_t)GetModuleHandleW(nullptr);
	char why[256] = "no module base (VPSHELL_EXE_BASE is 0 or not hex)";   // issue 3013: the refusal always names a reason
	int build = kVpExeBuildUnknown;
	bool ok = base && CamValidateImage(base, &build, why, sizeof(why));
	g_exeBase = base;
	if (ok)
		VPCamSetBuild(build);   // ticket 4964: every tap and VPCAM_VA_* read the addresses of this build from now on
	MemoryBarrier();
	InterlockedExchange(&g_camValidated, ok ? 1 : -1);
	if (ok)
		Log("camera: EXE image at 0x%08X%s: %s, %d byte patterns match - camera read enabled (delta 0x%08X)",
			base, fake ? " (VPSHELL_EXE_BASE: a test host's fake image)" : "", VpExeBuildLabel(build), kVPCamNumSigs, base - VPEXE_IMAGE_BASE);
	else
		Log("camera: REFUSED: image at 0x%08X%s: %s - the camera is never read in this process", base, fake ? " (VPSHELL_EXE_BASE)" : "", why);
}

struct CamRaw
{
	uint32_t gvs, vptr, mode, n;
	float proj[16];
	float view[4][16];
	float camWorld[16];
	float fov, aspect, nearZ, farZ;
	uint32_t vp[4];
	float vpz[2];
	float period;
};

static void CamReadRaw(CamRaw& r)
{
	uint32_t app = CamRd<uint32_t>(CamA(VPCAM_VA_APP));
	r.gvs = app ? CamRd<uint32_t>(app + VPCAM_APP_GVS) : 0;
	r.n = CamRd<uint32_t>(CamA(VPCAM_VA_NUMVIEWS));
	memcpy(r.proj, (const void*)CamA(VPCAM_VA_PROJ), 64);
	memcpy(r.view, (const void*)CamA(VPCAM_VA_VIEW), 256);
	memcpy(r.vp, (const void*)CamA(VPCAM_VA_VIEWPORT), 16);
	memcpy(r.vpz, (const void*)(CamA(VPCAM_VA_VIEWPORT) + 16), 8);
	r.period = CamRd<float>(CamA(VPCAM_VA_PERIOD));
	if (r.gvs)
	{
		r.vptr = CamRd<uint32_t>(r.gvs + VPCAM_GVS_FCAMERA);
		r.fov = CamRd<float>(r.gvs + VPCAM_GVS_FOV);
		r.aspect = CamRd<float>(r.gvs + VPCAM_GVS_ASPECT);
		memcpy(r.camWorld, (const void*)(uintptr_t)(r.gvs + VPCAM_GVS_CAMWORLD), 64);
		r.nearZ = CamRd<float>(r.gvs + VPCAM_GVS_NEAR);
		r.farZ = CamRd<float>(r.gvs + VPCAM_GVS_FAR);
		r.mode = CamRd<uint32_t>(r.gvs + VPCAM_GVS_VIEWMODE);
	}
	else { r.vptr = 0; r.fov = r.aspect = r.nearZ = r.farZ = 0; memset(r.camWorld, 0, 64); r.mode = 0; }
}

// Grid constants: a cheap key every frame; the whole block when the key changes (a map load) or every 600 frames.
static void CamReadGrid(VPShellCamera& b)
{
	uintptr_t grid = CamA(VPCAM_VA_GRID);
	GridKey k;
	k.ptr = CamRd<uint32_t>(CamA(VPCAM_VA_GRIDPTR));
	k.ready = k.ptr == (uint32_t)grid ? CamRd<uint8_t>(grid + VPCAM_GRID_READY) : 0;
	k.sX = CamRd<uint32_t>(grid + VPCAM_GRID_SAMPLES_X);
	k.sY = CamRd<uint32_t>(grid + VPCAM_GRID_SAMPLES_Y);
	k.ox = CamRd<float>(grid + VPCAM_GRID_SX + 12);
	k.oy = CamRd<float>(grid + VPCAM_GRID_SX + 16);
	bool keyChanged = !g_gridHaveKey || memcmp(&k, &g_gridKey, sizeof(k)) != 0;
	if (!keyChanged && ++g_gridRecheck < 600) { b.gridSeq = g_gridSeq; return; }
	g_gridRecheck = 0;
	g_gridKey = k; g_gridHaveKey = true;
	const size_t from = offsetof(VPShellCamera, gridValid), to = sizeof(VPShellCamera);
	unsigned char before[sizeof(VPShellCamera)];
	memcpy(before, (const char*)&b + from, to - from);
	b.gridValid = (k.ptr == (uint32_t)grid && k.ready == 1) ? 1u : 0u;
	b.wrapX = CamRd<int32_t>(CamA(VPCAM_VA_WRAPX));
	b.wrapPeriod = CamRd<float>(CamA(VPCAM_VA_PERIOD));
	memcpy(b.aabbMin, (const void*)CamA(VPCAM_VA_AABB), 12);
	memcpy(b.aabbMax, (const void*)(CamA(VPCAM_VA_AABB) + 12), 12);
	b.worldHeight = CamRd<float>(CamA(VPCAM_VA_WORLDH));
	memcpy(b.w2h, (const void*)CamA(VPCAM_VA_W2H), 64);
	memcpy(b.h2w, (const void*)CamA(VPCAM_VA_H2W), 64);
	memcpy(&b.sx, (const void*)(grid + VPCAM_GRID_SX), 20);
	b.samplesX = k.sX; b.samplesY = k.sY;
	b.heightmapAddr = (uint32_t)grid;
	b.pad0 = 0;
	if (memcmp(before, (const char*)&b + from, to - from) != 0)
	{
		g_gridSeq++;
		Log("camera: grid constants %s (grid seq %u): valid %u ptr 0x%08X ready %u samples %ux%u (map %ux%u plots) wrap %d period %.1f "
			"sx %.4f sy %.4f hscale %.3f ox %.3f oy %.3f h2w (%.4f %.4f / %.4f %.4f / %.4f %.4f)%s",
			keyChanged ? "read" : "CHANGED without a key change", g_gridSeq, b.gridValid, k.ptr, k.ready, b.samplesX, b.samplesY,
			b.samplesX / 64, b.samplesY >= 256 ? b.samplesY / 64 - 4 : 0, b.wrapX, b.wrapPeriod, b.sx, b.sy, b.heightScale, b.ox, b.oy,
			b.h2w[0], b.h2w[1], b.h2w[4], b.h2w[5], b.h2w[12], b.h2w[13], b.gridValid ? "" : " (no map: grid not drawn)");
	}
	b.gridSeq = g_gridSeq;
}

static uint32_t CamRead(VPShellCamera& b)
{
	CamRaw r[2];
	bool same = false;
	for (int a = 0; a < 4 && !same; ++a)
	{
		CamReadRaw(r[0]);
		CamReadRaw(r[1]);
		same = memcmp(&r[0], &r[1], sizeof(CamRaw)) == 0;
		if (!same) { g_camRetryI++; g_camRetryTotal++; }
	}
	const CamRaw& c = r[1];
	b.viewMode = c.mode;
	b.numViews = c.n <= 4 ? c.n : 0;
	memcpy(b.proj, c.proj, 64);
	memcpy(b.view, c.view, 256);
	memcpy(b.camWorld, c.camWorld, 64);
	b.fovDeg = c.fov; b.aspect = c.aspect; b.nearZ = c.nearZ; b.farZ = c.farZ;
	b.vpX = c.vp[0]; b.vpY = c.vp[1]; b.vpW = c.vp[2]; b.vpH = c.vp[3]; b.vpMinZ = c.vpz[0]; b.vpMaxZ = c.vpz[1];
	b.flags = 0;
	b.tornRetries = g_camRetryTotal;
	CamReadGrid(b);
	VPCamConsistency(b.camWorld, b.view[0], b.consistRot, b.consistTrans);
	if (!same) return VPCAM_TORN;
	if (!c.gvs) return VPCAM_NO_GVS;
	if (c.vptr != (uint32_t)CamA(VPCAM_VA_FCAM_VTBL)) return VPCAM_BAD_GVS;
	if (c.mode != 1) return VPCAM_NOT_MAP;
	if (c.n < 1 || c.n > 4) return VPCAM_INCONSISTENT;
	// issue 3015: every published matrix that the server draws with must be finite (NaN / inf: not OK)
	if (!VPCamAllFinite(b.proj, 16) || !VPCamAllFinite(b.camWorld, 16) || !VPCamAllFinite(&b.view[0][0], 16 * (int)c.n))
		return VPCAM_INCONSISTENT;
	if (!(b.consistRot <= VPCAM_TOL_ROT) || !(b.consistTrans <= VPCAM_TOL_TRANS)) return VPCAM_INCONSISTENT;
	for (uint32_t i = 1; i < c.n; ++i)
		if (!(VPCamWrapError(b.view[0], b.view[i], c.period) <= VPCAM_TOL_WRAP)) return VPCAM_INCONSISTENT;
	// information only: does g_Proj equal PerspectiveFovRH(fov, aspect, near, far) of the GVS camera?
	double ys = 1.0 / tan(c.fov * 3.14159265358979 / 360.0);
	double want[4] = { ys / c.aspect, ys, c.farZ / (c.nearZ - c.farZ), c.nearZ * c.farZ / (c.nearZ - c.farZ) };
	double got[4] = { c.proj[0], c.proj[5], c.proj[10], c.proj[14] };
	for (int k = 0; k < 4; ++k) if (!(fabs(got[k] - want[k]) <= 1e-3 * (fabs(want[k]) + 1e-6))) b.flags |= VPCAM_FLAG_PROJ_MISMATCH;
	return VPCAM_OK;
}

static uint32_t SafeCamRead(VPShellCamera& b, int64_t now)
{
	if (g_camDead) return VPCAM_EXCEPTION;
	if (now < g_camRetryAfter) return VPCAM_EXCEPTION;
	__try { return CamRead(b); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		g_camExceptions++;
		g_camRetryAfter = now + g_freq.QuadPart;
		if (g_camExceptions >= 5) g_camDead = true;
		Log("camera: EXCEPTION 0x%08X while reading the EXE's camera (%d so far) - %s", GetExceptionCode(), g_camExceptions,
			g_camDead ? "camera read disabled for good" : "no read for 1 s");
		return VPCAM_EXCEPTION;
	}
}

static void CamLogStatus(uint32_t st, const VPShellCamera& b)
{
	if (st == g_camLastStatus) return;
	if (g_camLogI >= 20) { g_camLogSuppressed++; g_camLastStatus = st; return; }
	g_camLogI++;
	Log("camera: status %s -> %s (present %lld, view mode %u, views %u, consist rot %.2e trans %.3f, grid seq %u valid %u%s)",
		g_camLastStatus == 0xFFFFFFFFu ? "-" : VPCamStatusName(g_camLastStatus), VPCamStatusName(st), g_camPresents, b.viewMode, b.numViews,
		b.consistRot, b.consistTrans, b.gridSeq, b.gridValid, (b.flags & VPCAM_FLAG_PROJ_MISMATCH) ? ", g_Proj != PerspectiveFovRH(GVS fov/aspect/near/far)" : "");
	g_camLastStatus = st;
	if (st == VPCAM_OK && !g_camFirstOk)
	{
		g_camFirstOk = true;
		Log("camera: first OK read on thread %u: fov %.3f deg aspect %.5f near %.1f far %.1f position (%.2f, %.2f, %.2f) viewport %ux%u at %u,%u back buffer %ux%u base 0x%08X",
			GetCurrentThreadId(), b.fovDeg, b.aspect, b.nearZ, b.farZ, b.camWorld[12], b.camWorld[13], b.camWorld[14], b.vpW, b.vpH, b.vpX, b.vpY,
			b.bbW, b.bbH, b.exeBase);
	}
}

// seqlock publish of the camera block (the EXE's main thread is the only writer: at Present, or at UpdateViews)
static void CamWriteBlock(VPShellCamera* cb, const VPShellCamera& b)
{
	LONG s = InterlockedIncrement((volatile LONG*)&cb->seq);
	if (!(s & 1)) s = InterlockedIncrement((volatile LONG*)&cb->seq);   // a writer died mid-write earlier: make it odd
	MemoryBarrier();
	cb->status = b.status;
	memcpy((char*)cb + offsetof(VPShellCamera, frameStamp), (const char*)&b + offsetof(VPShellCamera, frameStamp),
		sizeof(VPShellCamera) - offsetof(VPShellCamera, frameStamp));
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&cb->seq);
}

// Read the camera into b (status logic of issue 3007); false = nothing to publish yet (validation not finished).
static bool CamReadForPublish(VPShellCamera& b, int64_t now, uint32_t& st)
{
	LONG valid = g_camValidated;
	if (!g_cfg.camera) st = VPCAM_OFF;
	else if (valid < 0) st = VPCAM_REFUSED;
	else if (valid == 0) return false;                      // validation not finished yet
	else st = SafeCamRead(b, now);
	if (st == VPCAM_OK && g_cfg.cameraTest == 2) { b.view[0][12] += 40.0f; b.flags |= VPCAM_FLAG_TEST_CORRUPT; }
	// ticket 0408, test only: camera_test=late holds the camera back for camera_test_late reads after each new valid grid
	// (a new grid seq, also tap_test_load's), as on a first load, where the map view comes up long after the grid
	if (g_cfg.cameraTest == 3 && b.gridValid == 1)
	{
		static uint32_t s_lateSeq; static int s_lateN;
		if (b.gridSeq != s_lateSeq) { s_lateSeq = b.gridSeq; s_lateN = 0; }
		if (s_lateN < g_cfg.cameraTestLate) { s_lateN++; if (st == VPCAM_OK) st = VPCAM_INCONSISTENT; }
	}
	b.status = st;
	b.readQpc = now;
	b.exeBase = g_exeBase;
	b.bbW = g_bbW; b.bbH = g_bbH;
	if (st == VPCAM_OK) { g_camOkI++; g_camOkTotal++; } else { g_camBadI++; g_camBadTotal++; }
	b.readsOk = g_camOkTotal; b.readsBad = g_camBadTotal;
	CamLogStatus(st, b);
	return true;
}

// issue 3304: the lockstep part of the Present (below): true = the camera of this frame was already published at
// UpdateViews (Present keeps only presentCount); false = publish at Present as before. *stamp = the frame id to use.
static bool LkPresentPublish(int64_t& stamp);
static void LkPresentPublished(int64_t frameId, uint32_t st);

// Render thread, every Present of the target swap chain.
static void CamOnPresent()
{
	g_camPresents++;
	int64_t now = Qpc();
	if (!EnsureSection(now)) return;
	VPShellCamera* cb = VPSHELL_CAMERA(g_hdr);
	InterlockedExchange64(&cb->presentCount, g_camPresents);
	if (g_cfg.cameraTest == 1) return;                      // test: a publisher that stopped while frames are presented
	int64_t stamp = g_camPresents;
	if (LkPresentPublish(stamp)) return;                    // issue 3304: published at UpdateViews this frame
	VPShellCamera& b = g_camBlk;
	uint32_t st;
	if (!CamReadForPublish(b, now, st)) return;
	b.frameStamp = stamp;
	CamWriteBlock(cb, b);
	LkPresentPublished(stamp, st);                          // issue 3304: the publish block (source VPPUB_PRESENT), no wait
}

static float g_camT[16384]; static int g_nCamT;   // per-Present cost of the camera read + publish (ms)

static void SafeCamOnPresent()
{
	int64_t t0 = Qpc();
	__try { CamOnPresent(); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		g_camDead = true;
		Log("camera: EXCEPTION 0x%08X while publishing - camera read disabled", GetExceptionCode());
	}
	if (g_nCamT < 16384) g_camT[g_nCamT++] = (float)QpcToMs(Qpc() - t0);
}

// ===========================================================================
// Lockstep probe (issue 9039; ini lockstep_probe=1, off by default; diagnostic only)
// ===========================================================================
// For decision D81 (lockstep of the EXE's frame and the server's picture): per presented frame, the time from the
// EXE's GameViewState::UpdateViews (0x51DF70, main thread; the frame's camera is final there) to the execution of the
// scene-clear list (the composite's insertion point), research/05-graphics-composition/lockstep-probe.md.
// The UpdateViews timestamp [C, dumpbin of the DX11 EXE]: UpdateViews computes g_InvView[i] = inverse(g_View[i]) in a
// loop at 0x51E1B0 (`lea eax,[esi+0xDA1410]; push eax; call 0x8A86B6`, return address 0x51E1C8); 0x8A86B6 is the import
// thunk `jmp [0x9D6B74]` of d3dx9_42!D3DXMatrixInverse. The probe hooks that export inside d3dx9_42.dll with MinHook (a
// Microsoft DLL, like the d3d11/dxgi hooks; the EXE image and its import table are never written, D4) and takes QPC when
// pOut is &g_InvView[0] and the return address is 0x51E1C8 (both rebased): that is UpdateViews, after view 0 and the
// wrap views are written. Validated before hooking: the camera validation (the build and its 11 patterns), the bytes at
// the loop and the thunk, the import slot holding the export's address. Anything else: refused and logged.
// Ticket 4964 (epic 0590): the addresses above are build 4390913's; every build's come from the build table (ls_* in
// exebuild/symbols/shell.tsv), and the hooked DLL is the one the import slot points at (d3dx9_42.dll on build 4390913,
// d3dx9_43.dll on 26577625), never a fixed name.
// Rows (one per Present of the target) go to shell_composite.lsprobe.csv next to the DLL, written by the control thread.
#define LS_SIGNATURES(X) \
	X(ls_loop, "UpdateViews' inverse loop: lea eax,[esi+&g_InvView[0]]; push eax; call the thunk", "8D 86 R(ls_invview0) 50 E8 ?? ?? ?? ??") \
	X(ls_thunk, "the D3DXMatrixInverse import thunk: jmp [import slot]", "FF 25 R(ls_iat_matinv)")
static const VPExeSig kLsSigs[] = { LS_SIGNATURES(VPEXE_SIG_ENTRY) };
static char g_lsDll[64] = "d3dx9_4x.dll";   // the module the import slot points at (logs)

typedef void* (WINAPI* PFN_MatInv)(void*, float*, const void*);
static PFN_MatInv volatile g_lsInvTr;
static void* g_lsTarget;
static volatile LONG g_lsState;          // 0 = not tried, 1 = hooked, -1 = refused
static volatile LONG g_lsOn;             // hook enabled and rows recorded
static uintptr_t g_lsInvView0, g_lsRetUv;
static volatile LONG g_lsUvCount;
static volatile LONGLONG g_lsUvQpc;
static volatile LONG g_lsUvTid;
static volatile LONGLONG g_lsDefFirst;   // first deferred-context Clear/OMSet since the last Present (any thread)
static int64_t g_lsPrevRet;              // when the target's previous Present returned (render thread)
static int64_t g_lsFirstExec, g_lsScBefore, g_lsScAfter, g_lsInsEnd, g_lsUvAtScQpc;
static LONG g_lsUvAtSc, g_lsUvAtPrev;
static int g_lsScIdx; static DWORD g_lsScTid;
static int64_t g_lsSrvFrame, g_lsSrvRel, g_lsSrvCamRead, g_lsSrvStamp;
struct LsCam { int64_t stamp, readQpc; };
static LsCam g_lsCam[256];
struct LsRow
{
	int64_t frame, tPrevRet, tDefFirst, tUv, tFirstExec, tScBefore, tScAfter, tInsEnd, tPresent;
	int32_t uvBeforeSc, uvAfterSc, execN, scIdx;
	uint32_t uvTid, presTid, scTid, inserted, fresh, mode;
	int64_t srvFrame, srvRel, srvCamRead, srvStamp, stampNow;
	float cx, cy, cz;
};
static const LONG kLsRing = 16384;
static LsRow g_lsRing[kLsRing];
static volatile LONG g_lsHead;           // rows written (render thread)
static LONG g_lsTail;                    // rows flushed (control thread)
static LONG g_lsDropped;
static float g_lsD[16384]; static int g_nLsD;   // per-interval: scene clear executed - UpdateViews (ms)
static int g_lsNoUv, g_lsNoSc, g_lsUvMulti;
// Issue 3304: the hook is also the lockstep publish point. g_lsOn = the hook is enabled (lockstep_probe=1 or lockstep=1);
// the probe's rows are recorded only with lockstep_probe=1 (LsRows).
static inline bool LsRows() { return g_lsOn && g_cfg.lockstepProbe; }
static volatile LONG g_lkWant;           // the render thread's verdict at its last Present: publish at UpdateViews
static void SafeLkPublishAtUv();         // below (lockstep section)

static void* WINAPI LsInvDetour(void* out, float* det, const void* m)
{
	void* r = g_lsInvTr(out, det, m);
	if ((uintptr_t)out == g_lsInvView0 && (uintptr_t)_ReturnAddress() == g_lsRetUv)
	{
		InterlockedExchange64(&g_lsUvQpc, Qpc());
		g_lsUvTid = GetCurrentThreadId();
		InterlockedIncrement(&g_lsUvCount);
		if (g_lkWant) SafeLkPublishAtUv();
	}
	return r;
}

static bool LsValidate(void*& target, char* why, size_t n)
{
	__try
	{
		if (g_camValidated <= 0) { _snprintf_s(why, n, _TRUNCATE, "the camera validation did not pass (state %ld)", g_camValidated); return false; }
		const int build = VPCamBuild();
		if (VPExeCheckSigs(build, g_exeBase, kLsSigs, (int)(sizeof(kLsSigs) / sizeof(kLsSigs[0])), nullptr, nullptr, why, n) >= 0)
			return false;
		const uintptr_t loop = VpExeAddr(build, VPX_ls_loop, g_exeBase), thunk = VpExeAddr(build, VPX_ls_thunk, g_exeBase);
		const uintptr_t ret = VpExeAddr(build, VPX_ls_ret, g_exeBase), slotA = VpExeAddr(build, VPX_ls_iat_matinv, g_exeBase);
		int32_t rel; memcpy(&rel, (const void*)(loop + 8), 4);
		if (loop + 12 + rel != thunk || loop + 12 != ret)
		{
			_snprintf_s(why, n, _TRUNCATE, "UpdateViews' inverse loop at ls_loop 0x%08X: the call goes to 0x%08X, want the thunk ls_thunk 0x%08X; "
				"its return 0x%08X, want ls_ret 0x%08X", (uint32_t)loop, (uint32_t)(loop + 12 + rel), (uint32_t)thunk, (uint32_t)(loop + 12), (uint32_t)ret);
			return false;
		}
		// The hooked DLL is whatever module the import slot points into (d3dx9_42.dll / d3dx9_43.dll by build).
		void* inSlot = *(void* const volatile*)slotA;
		HMODULE dx = nullptr;
		if (!inSlot || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)inSlot, &dx))
			dx = nullptr;
		wchar_t path[MAX_PATH] = L"";
		if (dx) GetModuleFileNameW(dx, path, MAX_PATH);
		const wchar_t* base = wcsrchr(path, L'\\'); base = base ? base + 1 : path;
		const bool d3dx = _wcsnicmp(base, L"d3dx9_", 6) == 0 && wcslen(base) == 12 && _wcsicmp(base + 8, L".dll") == 0;
		void* exp = (dx && d3dx) ? (void*)GetProcAddress(dx, "D3DXMatrixInverse") : nullptr;
		if (!exp || inSlot != exp)
		{
			_snprintf_s(why, n, _TRUNCATE, "import slot ls_iat_matinv holds %p (module %p %S), not that module's D3DXMatrixInverse %p",
				inSlot, dx, base[0] ? base : L"none", exp);
			return false;
		}
		WideCharToMultiByte(CP_UTF8, 0, base, -1, g_lsDll, (int)sizeof(g_lsDll), nullptr, nullptr);
		target = exp;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		_snprintf_s(why, n, _TRUNCATE, "exception 0x%08X while reading the image", GetExceptionCode());
		return false;
	}
}

// Control thread: install (once) and enable / disable the hook to follow lockstep_probe (and mode != off).
static void LsApply(const Config& c, bool mhOk)
{
	bool want = mhOk && c.mode != MODE_OFF && (c.lockstepProbe || c.lockstep);   // issue 3304: lockstep publishes here
	if (want && g_lsState == 0)
	{
		char why[256] = "";
		void* target = nullptr;
		if (!LsValidate(target, why, sizeof(why)))
		{
			InterlockedExchange(&g_lsState, -1);
			Log("lsprobe: REFUSED: %s - no hook", why);
			return;
		}
		const unsigned char* b = (const unsigned char*)target;
		MH_STATUS st = MH_CreateHook(target, (void*)&LsInvDetour, (LPVOID*)&g_lsInvTr);
		if (st != MH_OK) { InterlockedExchange(&g_lsState, -1); Log("lsprobe: MH_CreateHook(%p) failed: %s", target, MH_StatusToString(st)); return; }
		g_lsTarget = target;
		g_lsInvView0 = VpExeAddr(VPCamBuild(), VPX_ls_invview0, g_exeBase);
		g_lsRetUv = VpExeAddr(VPCamBuild(), VPX_ls_ret, g_exeBase);
		InterlockedExchange(&g_lsState, 1);
		Log("lsprobe: %s!D3DXMatrixInverse %p (bytes %02X %02X %02X %02X %02X) hook created; UpdateViews return 0x%08X, &g_InvView[0] 0x%08X",
			g_lsDll, target, b[0], b[1], b[2], b[3], b[4], (uint32_t)g_lsRetUv, (uint32_t)g_lsInvView0);
	}
	if (g_lsState != 1) return;
	bool on = g_lsOn != 0;
	if (want == on) return;
	MH_STATUS st = want ? MH_EnableHook(g_lsTarget) : MH_DisableHook(g_lsTarget);
	if (st == MH_OK) InterlockedExchange(&g_lsOn, want ? 1 : 0);
	Log("lsprobe: hook %s: %s", want ? "ENABLED" : "DISABLED", MH_StatusToString(st));
}

// Render thread, ExecuteCommandList on the immediate context: t0 before the original, t1 after it, t2 after our own
// handling (the insertion, if this list is the one).
static void LsOnExec(uint32_t bits, int64_t t0, int64_t t1, int64_t t2)
{
	if (!g_lsFirstExec) g_lsFirstExec = t0;
	if ((bits & (TAG_CLEAR_BB | TAG_CLEAR_DSV)) == (TAG_CLEAR_BB | TAG_CLEAR_DSV) && !g_lsScBefore)
	{
		g_lsScBefore = t0; g_lsScAfter = t1; g_lsInsEnd = t2;
		g_lsScIdx = g_execCount;
		g_lsScTid = GetCurrentThreadId();
		g_lsUvAtSc = g_lsUvCount;
		g_lsUvAtScQpc = InterlockedCompareExchange64(&g_lsUvQpc, 0, 0);
	}
}

static inline void LsOnDeferred()
{
	if (LsRows() && !g_lsDefFirst) InterlockedCompareExchange64(&g_lsDefFirst, Qpc(), 0);
}

// Render thread, at the target's Present, after the camera publish. One row per frame.
static void LsOnPresent(int64_t now)
{
	if (g_camBlk.frameStamp > 0) { LsCam& e = g_lsCam[g_camBlk.frameStamp & 255]; e.stamp = g_camBlk.frameStamp; e.readQpc = g_camBlk.readQpc; }
	LONG uvc = g_lsUvCount;
	if (LsRows())
	{
		LsRow r = {};
		r.frame = g_frameIndex; r.tPrevRet = g_lsPrevRet; r.tDefFirst = InterlockedExchange64(&g_lsDefFirst, 0);
		bool sc = g_lsScBefore != 0;
		r.uvBeforeSc = (sc ? g_lsUvAtSc : uvc) - g_lsUvAtPrev;
		r.uvAfterSc = sc ? uvc - g_lsUvAtSc : 0;
		r.tUv = sc ? (r.uvBeforeSc > 0 ? g_lsUvAtScQpc : 0) : (r.uvBeforeSc > 0 ? InterlockedCompareExchange64(&g_lsUvQpc, 0, 0) : 0);
		r.tFirstExec = g_lsFirstExec; r.tScBefore = g_lsScBefore; r.tScAfter = g_lsScAfter; r.tInsEnd = g_lsInsEnd; r.tPresent = now;
		r.execN = g_execCount; r.scIdx = sc ? g_lsScIdx : 0;
		r.uvTid = (uint32_t)g_lsUvTid; r.presTid = GetCurrentThreadId(); r.scTid = sc ? g_lsScTid : 0;
		r.inserted = g_inserted ? 1 : 0; r.fresh = g_freshThisFrame ? 1 : 0; r.mode = (uint32_t)g_cfg.mode;
		r.srvFrame = g_lsSrvFrame; r.srvRel = g_lsSrvRel; r.srvCamRead = g_lsSrvCamRead; r.srvStamp = g_lsSrvStamp; r.stampNow = g_camPresents;
		r.cx = g_camBlk.camWorld[12]; r.cy = g_camBlk.camWorld[13]; r.cz = g_camBlk.camWorld[14];
		LONG h = g_lsHead;
		if (h - g_lsTail >= kLsRing) g_lsDropped++;   // the control thread fell behind: the oldest row is overwritten
		g_lsRing[h % kLsRing] = r;
		MemoryBarrier();
		InterlockedExchange(&g_lsHead, h + 1);
		if (!sc) g_lsNoSc++;
		else if (r.uvBeforeSc <= 0) g_lsNoUv++;
		else
		{
			if (r.uvBeforeSc > 1) g_lsUvMulti++;
			if (g_nLsD < 16384) g_lsD[g_nLsD++] = (float)QpcToMs(r.tScAfter - r.tUv);
		}
	}
	g_lsUvAtPrev = uvc;
	g_lsFirstExec = g_lsScBefore = g_lsScAfter = g_lsInsEnd = g_lsUvAtScQpc = 0;
	g_lsScIdx = 0; g_lsScTid = 0;
	g_lsSrvFrame = g_lsSrvRel = g_lsSrvCamRead = g_lsSrvStamp = 0;
}

// Render thread, in AcquireAndCopy after AcquireSync(1) succeeded: which camera the server drew this frame with (the
// depth block's camFrameStamp, written by the server before ReleaseSync(1) when it runs with --depth 1).
static void LsOnAcquired(int64_t fc, int64_t fq)
{
	if (!LsRows() || !g_hdr) return;
	const VPShellDepth* d = VPSHELL_DEPTH(g_hdr);
	uint32_t s1 = d->seq;
	if (s1 & 1) return;
	MemoryBarrier();
	int64_t dfc = d->frameCounter, stamp = d->camFrameStamp;
	uint32_t state = d->state;
	MemoryBarrier();
	if (d->seq != s1 || state != 1 || dfc != fc) return;
	const LsCam& e = g_lsCam[stamp & 255];
	if (e.stamp != stamp) return;
	g_lsSrvFrame = fc; g_lsSrvRel = fq; g_lsSrvCamRead = e.readQpc; g_lsSrvStamp = stamp;
}

// Control thread: append the rows written since the last call to shell_composite.lsprobe.csv.
static void LsFlush()
{
	LONG h = g_lsHead;
	if (h == g_lsTail) return;
	MemoryBarrier();
	wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%sshell_composite.lsprobe.csv", g_outDir);
	bool fresh = GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES;
	FILE* f = _wfsopen(p, L"a", _SH_DENYNO);
	if (!f) return;
	if (fresh)
		fprintf(f, "# qpc_freq=%lld pid=%u\nframe,t_prev_ret,t_def_first,t_uv,t_first_exec,t_sc_before,t_sc_after,t_ins_end,t_present,"
			"uv_before_sc,uv_after_sc,exec_n,sc_idx,uv_tid,present_tid,sc_tid,inserted,fresh,mode,srv_frame,srv_rel,srv_cam_read,srv_stamp,"
			"stamp_now,cam_x,cam_y,cam_z\n", g_freq.QuadPart, GetCurrentProcessId());
	if (h - g_lsTail > kLsRing) g_lsTail = h - kLsRing;
	for (; g_lsTail != h; ++g_lsTail)
	{
		const LsRow& r = g_lsRing[g_lsTail % kLsRing];
		fprintf(f, "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d,%d,%d,%u,%u,%u,%u,%u,%u,%lld,%lld,%lld,%lld,%lld,%.2f,%.2f,%.2f\n",
			r.frame, r.tPrevRet, r.tDefFirst, r.tUv, r.tFirstExec, r.tScBefore, r.tScAfter, r.tInsEnd, r.tPresent,
			r.uvBeforeSc, r.uvAfterSc, r.execN, r.scIdx, r.uvTid, r.presTid, r.scTid, r.inserted, r.fresh, r.mode,
			r.srvFrame, r.srvRel, r.srvCamRead, r.srvStamp, r.stampNow, r.cx, r.cy, r.cz);
	}
	fclose(f);
}

// ===========================================================================
// Lockstep, hand-over and depth (issue 3304; ini lockstep, lockstep_bound_us, lockstep_giveup, handover, depth_write,
// lockstep_csv, lockstep_check, ring_slots, ring_sync; all off by default)
// ===========================================================================
// The protocol is shell/common/vp_frame.h; the reasons research/05-graphics-composition/renderer-in-server-design.md 3-5.
//   * Publish (the EXE's main thread): with lockstep=1 the 9039 hook (d3dx9_42!D3DXMatrixInverse called by UpdateViews
//     for g_InvView[0]) reads the camera right after view 0, the projection and the wrap views became final, writes the
//     version-3 camera block with frameStamp = F, then the publish block (publishQpc, deadline = publish + bound), then
//     sets <base>.Cam. F only grows: max(previous F + 1, presentCount + 1), so frameStamp stays the presentCount of the
//     Present that ends the frame (shell_server.exe's stale rule presentCount - frameStamp keeps working) [contract:
//     vp_frame.h says "previous F + 1"; frames without UpdateViews (front end, Strategic View) make the gap].
//     Frames without an UpdateViews publish (hook refused or not installed, Strategic View, front end) publish at Present
//     as before with source VPPUB_PRESENT and no wait.
//   * Insert (main thread, at the tagged insertion): want = the F published at UpdateViews in this frame. If the source's
//     newest frame is older, wait for it until the deadline (event + high-resolution timer + a spin over the last 0.5 ms),
//     never past it. Take F (exact) or the newest older frame (a miss: the previous picture, counted with its age). After
//     lockstep_giveup misses in a row no wait until a frame is ready at the insertion without one (wait_off frames).
//   * The hand-over is a vpframe::FrameSource (frame_source.h): V3Source below wraps the version-3 keyed mutex + copy (the
//     depth block's camFrameStamp names the camera a frame was drawn with: shell_server.exe --depth 1 writes it);
//     handover=ring uses shell/frame's RingSource (request() before the first publish, close() before ResizeBuffers).
//   * depth_write=1: one full-screen pass writes colour (SV_Target) and the frame's depth (SV_Depth) into the DSV that the
//     scene-clear list cleared (recorded at its ClearDepthStencilView), depth func ALWAYS, write on, stencil untouched.
//   * Per-frame records (lockstep_csv=1) and a "lockstep 1s:" line next to the stats line.
// Nothing here runs unless lockstep, handover=ring or depth_write is set (SourcePath); the version-3 path without them is
// the code above, unchanged.
enum { LK_EXACT = 0, LK_OLDER = 1, LK_NONE = 2, LK_NOPUB = 3, LK_NOINS = 4, LK_HELD = 5 };   // 5: ticket 0691 (MC #11169)

static inline bool SourcePathWanted() { return g_cfg.lockstep || g_cfg.handover == HO_RING || g_cfg.depthWrite; }
static inline bool SourcePath()
{
	// the inlist rule draws on recording threads from the local copy; it keeps the version-3 path (LogConfig says so)
	return SourcePathWanted() && !(g_cfg.insert == INS_TAGGED && g_cfg.tagRule == RULE_INLIST);
}

// ---- <base>.Frame and <base>.Cam ----
static HANDLE g_fsMap, g_fsCam;
static HANDLE g_fsTap;                     // ticket 0713: <base>.Tap (the prepare phase); null = not opened (no event set)
static HANDLE g_lkFrm, g_lkTimer;          // the insertion's wait: <base>.Frm and a high-resolution timer (main thread)
static void* g_fs;
static int64_t g_fsNextTry;
static bool g_fsFailLogged;

static void CloseFrameSection()
{
	if (g_fs) UnmapViewOfFile(g_fs);
	if (g_fsMap) CloseHandle(g_fsMap);
	if (g_fsCam) CloseHandle(g_fsCam);
	if (g_fsTap) CloseHandle(g_fsTap);
	if (g_lkFrm) CloseHandle(g_lkFrm);
	g_fs = nullptr; g_fsMap = nullptr; g_fsCam = nullptr; g_fsTap = nullptr; g_lkFrm = nullptr;
	g_fsNextTry = 0;
}

static bool EnsureFrameSection(int64_t now)
{
	if (g_fs) return true;
	if (now < g_fsNextTry) return false;
	g_fsNextTry = now + g_freq.QuadPart;
	g_fs = vpframe::ring::MapSection(g_cfg.section, g_fsMap);
	if (g_fs) g_fsCam = vpframe::ring::OpenEvent_(g_cfg.section, VPFRAME_CAM_EVENT_SUFFIX);
	if (!g_fs || !g_fsCam)
	{
		DWORD e = GetLastError();
		if (!g_fsFailLogged) Log("lockstep: section '%ls%ls' or event '%ls%ls' not created or opened (error %u); retrying once per second",
			g_cfg.section, VPFRAME_SECTION_SUFFIX, g_cfg.section, VPFRAME_CAM_EVENT_SUFFIX, e);
		g_fsFailLogged = true;
		CloseFrameSection();
		g_fsNextTry = now + g_freq.QuadPart;
		return false;
	}
	VPFRAME_PUBLISH(g_fs)->exePid = GetCurrentProcessId();
	Log("lockstep: section '%ls%ls' and event '%ls%ls' open", g_cfg.section, VPFRAME_SECTION_SUFFIX, g_cfg.section, VPFRAME_CAM_EVENT_SUFFIX);
	// ticket 0713: the prepare phase's event; without it the server polls at the camera as before (no failure)
	g_fsTap = vpframe::ring::OpenEvent_(g_cfg.section, VPFRAME_TAP_EVENT_SUFFIX);
	if (g_fsTap) Log("lockstep: event '%ls%ls' open: set after the last tap of each Present (ticket 0713)", g_cfg.section, VPFRAME_TAP_EVENT_SUFFIX);
	else Log("lockstep: event '%ls%ls' not created or opened (error %u): the server polls at the camera (ticket 0713)", g_cfg.section,
		VPFRAME_TAP_EVENT_SUFFIX, GetLastError());
	return true;
}

// Ticket 0713 (the prepare phase, vp_frame.h <base>.Tap): every tap of this Present is written; the server drains its feed and
// polls the taps now, before the next frame's camera, and at that camera goes straight to decide while tapPresent still
// equals the number it prepared for. Main thread, the Present hook, after the last tap. Nothing when the section is closed.
static void LkTapsDone()
{
	if (!g_fs || !g_fsTap) return;
	MemoryBarrier();   // the taps' writes before the number (the interlocked write is a full barrier too)
	InterlockedExchange64(&VPFRAME_PUBLISH(g_fs)->tapPresent, g_camPresents);
	SetEvent(g_fsTap);
}

// ---- state (main thread unless noted) ----
static int64_t g_lkLastF;                  // newest frame id published (camera block frameStamp)
static int64_t g_lkUvF;                    // F published at UpdateViews in this frame (0 = none); reset at Present
static bool g_lkUvWait;                    // that publish asked for a wait (lockstep, camera OK, bound > 0, not given up)
static int64_t g_lkUvQpc, g_lkUvDeadline;
static DWORD g_presentTid;                 // the thread that presents the target; UpdateViews must run on it
static volatile bool g_lkUvDead;           // an exception in the UpdateViews publish: Present publishes from then on
static bool g_lkWrongThreadLogged, g_lkPresentNoteLogged, g_lkSrcFailLogged;
static bool g_lkGaveUp;
static int g_lkMissRow;
static int64_t g_lkCntDrawn, g_lkCntExact, g_lkCntMiss, g_lkCntWaitOff;   // cumulative, mirrored into the publish block
static VPShellCamera g_lkUvCam;            // the UpdateViews read (lockstep_check)
static int g_lkCmpLogged;
struct LkPub { int64_t f, qpc; };
static LkPub g_lkPubTab[256];               // frame id -> its publish QPC
struct LkStats
{
	int pubUv, pubPresent, exact, older, none, nopub, noins, waits, waitOff, giveups, recoveries;
	int depthN, depthNoDsv, depthRefused, depthNoSrc, cmpN, cmpDiff, wrongThread, srcFail;
	int unbakedN, grayFrames; int64_t grayTiles;   // issue 0139: taken frames with a count; of them with a gray tile; their sum
	int hungEnds, deadEnds;                        // ticket 0691: exact waits ended hung / dead
	int heldSkips, heldFaults;                     // ticket 0691 (MC #11169): frames the server held (not waited for); stuck holds
};
static LkStats g_lkI;                       // per stats interval
static float g_lkWaitUs[16384]; static int g_nLkWait;
static float g_lkUvUs[16384]; static int g_nLkUv;   // cost of the UpdateViews publish (camera read + both blocks), us
static lklate::Counts g_lkLate;             // per stats interval (issue 0150)

// per-frame records (lockstep_csv=1): one row per Present of the target, written by the main thread, flushed by the
// control thread into shell_composite.lockstep.csv
struct LkRow
{
	int64_t frame, pubF, pubQpc, deadline, insQpc, wait0, wait1, takenF, srvCamPub, srvWake, srvSubmit, srvReady, presentQpc;
	int32_t pubSrc, kind, age, slot, waitOff, depth, cmp;
	int32_t unbaked;   // issue 0139: the taken slot's land tiles drawn without their bake (VPFrameSlot); -1 = not counted / none taken
	float srvCpuMs, srvGpuMs, cx, cy, cz, pad2;
	int32_t wu;          // ticket 0688: 1 = this Present showed a world picture newer than the Present before (a world update)
	float worldAgeMs;    // ticket 0688: Present - the shown picture's camera publish, ms; -1 = unknown or none shown
};
static const LONG kLkRing = 16384;
static LkRow g_lkRing[kLkRing];
static volatile LONG g_lkHead;
static LONG g_lkTail;
static volatile LONG g_lkDropped;
static LkRow g_lkCur;                      // this frame's row
static bool g_lkCurIns;                    // this frame had an insertion on the source path
static const char* kLkKindNames[] = { "exact", "older", "none", "nopub", "noins", "held" };

static int64_t LkPubQpc(int64_t f) { const LkPub& e = g_lkPubTab[f & 255]; return e.f == f ? e.qpc : 0; }

// Writes the publish block (its seqlock), then sets <base>.Cam. g_fs is mapped.
static void LkWritePublish(int64_t F, uint32_t src, bool wait, int64_t& pubQpc, int64_t& deadline)
{
	VPFramePublish* p = VPFRAME_PUBLISH(g_fs);
	const uint32_t bound = g_cfg.lockstepExact ? 0 : g_cfg.boundUs;   // ticket 0691: exact = no bound
	const bool ls = wait && (g_cfg.lockstepExact || bound > 0);
	pubQpc = Qpc();
	deadline = !ls ? 0 : g_cfg.lockstepExact ? pubQpc + (int64_t)g_cfg.hungMs * g_freq.QuadPart / 1000   // the server's Busy retry
	                                          : pubQpc + (int64_t)bound * g_freq.QuadPart / 1000000;
	LONG s = InterlockedIncrement((volatile LONG*)&p->seq);
	if (!(s & 1)) InterlockedIncrement((volatile LONG*)&p->seq);
	MemoryBarrier();
	p->source = src; p->frameId = F; p->publishQpc = pubQpc; p->deadlineQpc = deadline; p->boundUs = bound; p->lockstep = ls ? 1u : 0u;
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&p->seq);
	LkPub& e = g_lkPubTab[F & 255]; e.f = F; e.qpc = pubQpc;
	SetEvent(g_fsCam);
}

// A bounded wait: until ready() or the deadline. Sleeps on `ev` (may be null) plus a high-resolution timer in slices of at
// most 0.25 ms (a server that never sets the event is polled), spins over the last 0.5 ms; never waits past the deadline
// by more than one slice's overshoot. Main thread.
template <class Pred> static bool LkBoundedWait(Pred ready, int64_t deadline, HANDLE ev, HANDLE timer)
{
	const int64_t spin = g_freq.QuadPart / 2000, slice = g_freq.QuadPart / 4000;
	for (;;)
	{
		if (ready()) return true;
		int64_t now = Qpc();
		if (now >= deadline) return ready();
		int64_t left = deadline - now;
		if (left <= spin || !timer) { YieldProcessor(); continue; }
		int64_t d = std::min(slice, left - spin);
		LARGE_INTEGER due; due.QuadPart = -std::max<int64_t>(1, d * 10000000 / g_freq.QuadPart);
		HANDLE h[2]; DWORD n = 0;
		if (ev) h[n++] = ev;
		if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) h[n++] = timer;
		if (!n) { YieldProcessor(); continue; }
		WaitForMultipleObjects(n, h, FALSE, (DWORD)(left * 1000 / g_freq.QuadPart) + 1);
	}
}

static bool SnapDepth(VPShellDepth& out)
{
	if (!g_hdr) return false;
	const VPShellDepth* d = VPSHELL_DEPTH(g_hdr);
	for (int i = 0; i < 8; ++i)
	{
		uint32_t s1 = d->seq;
		if (s1 & 1) { YieldProcessor(); continue; }
		MemoryBarrier();
		memcpy(&out, (const void*)d, sizeof(out));
		MemoryBarrier();
		out.textureName[_countof(out.textureName) - 1] = 0;
		if (d->seq == s1) return true;
	}
	return false;
}

// ---- the version-3 source: today's keyed mutex + copy (AcquireAndCopy) behind vpframe::FrameSource ----
// Which camera a frame was drawn with: the depth block's camFrameStamp, written by the server before ReleaseSync(1)
// together with frameCounter (shell_server.exe --depth 1; 3301's v3 sink). Without it the ids are unknown (newest() 0):
// lockstep then never finds its frame, misses until it gives up (logged once).
class V3Source : public vpframe::FrameSource
{
public:
	~V3Source() override { if (m_timer) CloseHandle(m_timer); if (m_frm) CloseHandle(m_frm); }
	bool open(ID3D11Device*, ID3D11DeviceContext*, const wchar_t*) override { m_newest = m_taken = m_takenPub = 0; return true; }
	void request(uint32_t, uint32_t) override {}   // PollServer asks for the back buffer's size (request_resize=1, as before)
	int64_t newest() override
	{
		if (!g_hdr || g_hdr->magic != VPSHELL_MAGIC) return m_newest;
		const VPShellDepth* d = VPSHELL_DEPTH(g_hdr);
		for (int i = 0; i < 4; ++i)
		{
			uint32_t s1 = d->seq;
			if (s1 & 1) { YieldProcessor(); continue; }
			MemoryBarrier();
			uint32_t st = d->state; int64_t fc = d->frameCounter, stamp = d->camFrameStamp;
			MemoryBarrier();
			if (d->seq != s1) continue;
			int64_t hfc = *(volatile int64_t*)&g_hdr->frameCounter;
			if (st == 1 && fc > 0 && fc == hfc && stamp > m_newest) m_newest = stamp;
			break;
		}
		if (!m_newest && !m_noIdsLogged && g_hdr->frameCounter > 30)
		{
			m_noIdsLogged = true;
			Log("lockstep: v3 source: %lld server frames without a depth block naming their camera (shell_server.exe needs --depth 1): "
				"frame ids unknown, lockstep cannot find its frame", (long long)g_hdr->frameCounter);
		}
		return m_newest;
	}
	bool waitFor(int64_t frameId, int64_t deadlineQpc) override
	{
		if (!m_timer) m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!m_timer) m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		// <base>.Frm: set by a server that speaks vp_frame.h; shell_server.exe never sets it (the timer slices poll it)
		if (!m_frm) m_frm = vpframe::ring::OpenEvent_(g_cfg.section, VPFRAME_FRM_EVENT_SUFFIX);
		return LkBoundedWait([&]() { return newest() >= frameId; }, deadlineQpc, m_frm, m_timer);
	}
	vpframe::Take acquire(ID3D11DeviceContext* ctx, int64_t frameId, vpframe::SourceFrame& out) override
	{
		out = vpframe::SourceFrame();
		bool fresh = false;
		g_acquiredThisFrame = true;
		if (g_km && g_localTex && g_sharedTex)
		{
			HRESULT hr = g_km->AcquireSync(1, 0);
			if (hr == (HRESULT)WAIT_TIMEOUT && frameId > m_taken && newest() >= frameId)
			{
				// announced (frame and depth block written) but ReleaseSync(1) not made yet: microseconds in shell_server
				int64_t end = Qpc() + g_freq.QuadPart / 2000;
				while (hr == (HRESULT)WAIT_TIMEOUT && Qpc() < end) { YieldProcessor(); hr = g_km->AcquireSync(1, 0); }
			}
			if (hr == S_OK)
			{
				int64_t fc = g_hdr->frameCounter, fq = g_hdr->frameQpc;
				VPShellDepth d;
				bool dOk = SnapDepth(d) && d.state == 1 && d.frameCounter == fc;
				ctx->CopyResource(g_localTex, g_sharedTex);
				bool depthCopied = false;
				if (dOk && g_depthShared && g_localDepth && d.generation == g_depthGen && g_hdr->serverPid == g_depthPid)
				{
					ctx->CopyResource(g_localDepth, g_depthShared);
					depthCopied = true;
				}
				g_hdr->clientFramesConsumed = ++g_consumed;
				g_km->ReleaseSync(0);
				if (g_lastServerFrame >= 0 && fc - g_lastServerFrame > 1) g_srvDrop += (int)(fc - g_lastServerFrame - 1);
				g_lastServerFrame = fc;
				g_contentQpc = fq;
				g_haveContent = true;
				g_freshThisFrame = true;
				g_srvNew++;
				g_colourFc = fc;
				g_depthFc = depthCopied ? fc : -1;
				m_taken = dOk ? d.camFrameStamp : 0;
				// ticket 0705: the picture's publish QPC is looked up once, when it is taken, and kept with it: a picture
				// shown again more than 256 Presents after its publish (a long stall) is no longer lost to g_lkPubTab
				m_takenPub = m_taken ? LkPubQpc(m_taken) : 0;
				if (m_taken > m_newest) m_newest = m_taken;
				m_proj22 = dOk ? d.proj22 : 0; m_proj32 = dOk ? d.proj32 : 0; m_near = dOk ? d.nearZ : 0; m_far = dOk ? d.farZ : 0;
				m_readyQpc = fq;
				fresh = true;
			}
			else if (hr != (HRESULT)WAIT_TIMEOUT)
			{
				Log("AcquireSync(1) returned 0x%08X (%s) - closing the shared texture", hr, hr == (HRESULT)WAIT_ABANDONED ? "abandoned: server died holding it" : "error");
				CloseShared();
			}
			else g_noNewFrame++;
		}
		if (!g_haveContent || !g_srvUnorm) return vpframe::Take::None;
		out.frameId = m_taken;
		out.colour = g_srvUnorm;
		out.depth = (g_depthFc >= 0 && g_depthFc == g_colourFc) ? g_srvDepthLocal : nullptr;
		out.proj22 = m_proj22; out.proj32 = m_proj32; out.nearZ = m_near; out.farZ = m_far;
		out.readyQpc = m_readyQpc;
		out.camPublishQpc = m_takenPub;
		if (frameId > 0) return m_taken == frameId ? vpframe::Take::Exact : vpframe::Take::Older;
		return fresh ? vpframe::Take::Exact : vpframe::Take::Older;
	}
	void release(ID3D11DeviceContext*, const vpframe::SourceFrame&) override {}   // the copy is local
	void poll(int64_t) override { PollServer(); PollDepth(); }
	void close() override
	{
		CloseShared();
		CloseDepthShared();
		AcquireSRWLockExclusive(&g_resLock);
		ReleaseLocalLocked();
		ReleaseSRWLockExclusive(&g_resLock);
		m_newest = m_taken = m_takenPub = 0;
	}
	const char* name() const override { return "v3"; }

private:
	// The server's depth texture (only for depth_write): opened by name when the depth block names a new one.
	void PollDepth()
	{
		if (!g_cfg.depthWrite || !g_hdr || g_hdr->magic != VPSHELL_MAGIC || !g_dev1) return;
		VPShellDepth d;
		if (!SnapDepth(d)) return;
		if (d.state != 1 || d.dxgiFormat != DXGI_FORMAT_R32_FLOAT || !d.width || !d.height || !d.textureName[0])
		{
			if (g_depthShared) { Log("lockstep: the server's depth block is gone (state %u): depth closed", d.state); CloseDepthShared(); }
			return;
		}
		if (g_depthShared && d.generation == g_depthGen && g_hdr->serverPid == g_depthPid) return;
		int64_t now = Qpc();
		if (now < g_depthNextTry) return;
		g_depthNextTry = now + g_freq.QuadPart / 4;
		CloseDepthShared();
		ID3D11Texture2D* tex = nullptr;
		HRESULT hr = g_dev1->OpenSharedResourceByName(d.textureName, DXGI_SHARED_RESOURCE_READ, __uuidof(ID3D11Texture2D), (void**)&tex);
		if (FAILED(hr))
		{
			if (d.generation != m_depthFailGen) Log("lockstep: OpenSharedResourceByName('%ls') (server depth) failed 0x%08X", d.textureName, hr);
			m_depthFailGen = d.generation;
			return;
		}
		D3D11_TEXTURE2D_DESC td; tex->GetDesc(&td);
		D3D11_TEXTURE2D_DESC ld = {};
		ld.Width = td.Width; ld.Height = td.Height; ld.MipLevels = 1; ld.ArraySize = 1; ld.Format = DXGI_FORMAT_R32_FLOAT;
		ld.SampleDesc.Count = 1; ld.Usage = D3D11_USAGE_DEFAULT; ld.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		AcquireSRWLockExclusive(&g_resLock);
		hr = g_dev->CreateTexture2D(&ld, nullptr, &g_localDepth);
		if (SUCCEEDED(hr)) hr = g_dev->CreateShaderResourceView(g_localDepth, nullptr, &g_srvDepthLocal);
		if (FAILED(hr)) { SafeRelease(g_srvDepthLocal); SafeRelease(g_localDepth); }
		ReleaseSRWLockExclusive(&g_resLock);
		if (FAILED(hr)) { Log("lockstep: local depth texture %ux%u failed 0x%08X", td.Width, td.Height, hr); tex->Release(); return; }
		g_depthShared = tex; g_depthGen = d.generation; g_depthPid = g_hdr->serverPid; g_depthFc = -1;
		Log("lockstep: opened the server's depth '%ls' %ux%u fmt %u gen %u (server pid %u)", d.textureName, td.Width, td.Height, td.Format, d.generation, g_depthPid);
	}
	int64_t m_newest = 0, m_taken = 0, m_takenPub = 0, m_readyQpc = 0;
	float m_proj22 = 0, m_proj32 = 0, m_near = 0, m_far = 0;
	bool m_noIdsLogged = false;
	uint32_t m_depthFailGen = 0xFFFFFFFFu;
	HANDLE m_timer = nullptr, m_frm = nullptr;
};

// ---- the source in use ----
static vpframe::FrameSource* g_src;
static vpframe::RingSource* g_ring;        // == g_src when handover=ring (its counters for the 1 s line)
static int g_srcKind = -1, g_srcSlots, g_srcSync;
static bool g_srcOpen;
static uint32_t g_srcReqW, g_srcReqH;
static int64_t g_srcNextTry;

static void RingLog(void*, const char* line) { Log("%s", line); }

static void LkCloseSource(const char* why)
{
	if (!g_src || !g_srcOpen) return;
	g_src->close();
	g_srcOpen = false;
	g_srcReqW = g_srcReqH = 0;
	g_srcNextTry = 0;
	Log("lockstep: %s source closed (%s)", g_src->name(), why);
}

static void LkDeleteSource(const char* why)
{
	LkCloseSource(why);
	delete g_src;
	g_src = nullptr; g_ring = nullptr; g_srcKind = -1;
}

// Main thread. Creates, opens and sizes the source; false = nothing to draw from this frame (retried once per second).
static bool LkEnsureSource()
{
	if (!g_dev || !g_ctx) return false;
	if (g_src && (g_srcKind != g_cfg.handover || (g_ring && (g_srcSlots != g_cfg.ringSlots || g_srcSync != g_cfg.ringSync))))
		LkDeleteSource("handover changed");
	if (!g_src)
	{
		if (g_cfg.handover == HO_RING)
		{
			g_ring = new (std::nothrow) vpframe::RingSource((uint32_t)g_cfg.ringSlots, (uint32_t)g_cfg.ringSync);
			if (!g_ring) return false;
			g_ring->setLog(RingLog, nullptr);
			g_src = g_ring;
		}
		else if (!(g_src = new (std::nothrow) V3Source())) return false;
		g_srcKind = g_cfg.handover; g_srcSlots = g_cfg.ringSlots; g_srcSync = g_cfg.ringSync;
		Log("lockstep: frame source '%s'%s", g_src->name(), g_ring ? (g_cfg.ringSync == VPSYNC_CPU ? " (slots wanted from ring_slots, sync cpu)" : " (slots wanted from ring_slots, sync fence)") : "");
	}
	if (!g_srcOpen)
	{
		int64_t now = Qpc();
		if (now < g_srcNextTry) return false;
		g_srcNextTry = now + g_freq.QuadPart;
		if (!g_src->open(g_dev, g_ctx, g_cfg.section))
		{
			g_lkI.srcFail++;
			if (!g_lkSrcFailLogged) Log("lockstep: %s source: open('%ls') failed; retrying once per second", g_src->name(), g_cfg.section);
			g_lkSrcFailLogged = true;
			return false;
		}
		g_srcOpen = true;
		Log("lockstep: %s source open on '%ls'", g_src->name(), g_cfg.section);
	}
	if (g_bbW && g_bbH && (g_bbW != g_srcReqW || g_bbH != g_srcReqH))
	{
		g_src->request(g_bbW, g_bbH);
		g_srcReqW = g_bbW; g_srcReqH = g_bbH;
		Log("lockstep: %s source: %ux%u requested", g_src->name(), g_bbW, g_bbH);
	}
	return true;
}

// ---- publish ----
// UpdateViews (the D3DXMatrixInverse hook), main thread, after view 0 / projection / wrap views became final.
static void LkPublishAtUv()
{
	int64_t t0 = Qpc();
	if (GetCurrentThreadId() != g_presentTid)
	{
		g_lkI.wrongThread++;
		if (!g_lkWrongThreadLogged) Log("lockstep: UpdateViews ran on thread %u, the target presents on %u: not published there (counted wrong_thread)", GetCurrentThreadId(), g_presentTid);
		g_lkWrongThreadLogged = true;
		return;
	}
	if (!EnsureSection(t0) || !EnsureFrameSection(t0)) return;   // the Present publishes instead
	MaybeFault(4);                                                // fault_test=uv (tests only)
	LkEnsureSource();                                             // the ring: request() before the first publish
	VPShellCamera& b = g_camBlk;
	uint32_t st;
	if (!CamReadForPublish(b, t0, st)) return;
	const int64_t F = std::max(g_lkLastF + 1, g_camPresents + 1);
	b.frameStamp = F;
	CamWriteBlock(VPSHELL_CAMERA(g_hdr), b);
	g_lkLastF = F;
	const bool wait = g_cfg.lockstep && st == VPCAM_OK && (g_cfg.lockstepExact || (!g_lkGaveUp && g_cfg.boundUs > 0));   // 0691
	LkWritePublish(F, VPPUB_UPDATEVIEWS, wait, g_lkUvQpc, g_lkUvDeadline);
	g_lkUvF = F;
	g_lkUvWait = wait;
	if (g_cfg.lockstepCheck) g_lkUvCam = b;
	g_lkI.pubUv++;
	if (g_nLkUv < 16384) g_lkUvUs[g_nLkUv++] = (float)(QpcToMs(Qpc() - t0) * 1000.0);
}

static void SafeLkPublishAtUv()
{
	if (t_inComposite || g_lkUvDead) return;
	t_inComposite = 1;   // the source's own D3D calls (ring open) pass every hook untouched
	__try { LkPublishAtUv(); t_inComposite = 0; }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		t_inComposite = 0;
		g_lkUvDead = true;
		InterlockedExchange(&g_lkWant, 0);
		Log("lockstep: EXCEPTION 0x%08X in the UpdateViews publish - the camera is published at Present from now on (no wait)", GetExceptionCode());
	}
}

// lockstep_check=1 (premise 3 of the design): the camera globals read again at Present against the UpdateViews read.
static bool SafeCamReadRaw(CamRaw& r)
{
	__try { CamReadRaw(r); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); return false; }
}

static const char* kCmpFieldNames[] = { "proj", "numViews", "view", "camWorld", "viewMode", "viewport", "fov/aspect", "near/far" };

static void LkCompareAtPresent()
{
	CamRaw r;
	if (g_camDead || g_camValidated <= 0 || !SafeCamReadRaw(r)) return;
	const VPShellCamera& u = g_lkUvCam;
	uint32_t diff = 0, n = r.n <= 4 ? r.n : 0;
	if (memcmp(r.proj, u.proj, 64)) diff |= 1;
	if (n != u.numViews) diff |= 2;
	else if (n && memcmp(r.view, u.view, 64 * n)) diff |= 4;
	if (memcmp(r.camWorld, u.camWorld, 64)) diff |= 8;
	if (r.mode != u.viewMode) diff |= 16;
	if (r.vp[0] != u.vpX || r.vp[1] != u.vpY || r.vp[2] != u.vpW || r.vp[3] != u.vpH || r.vpz[0] != u.vpMinZ || r.vpz[1] != u.vpMaxZ) diff |= 32;
	if (r.fov != u.fovDeg || r.aspect != u.aspect) diff |= 64;
	if (r.nearZ != u.nearZ || r.farZ != u.farZ) diff |= 128;
	g_lkI.cmpN++;
	g_lkCur.cmp = (int32_t)diff;
	if (!diff) return;
	g_lkI.cmpDiff++;
	if (g_lkCmpLogged >= 10) return;
	g_lkCmpLogged++;
	char names[160] = "";
	for (int i = 0; i < 8; ++i) if (diff & (1u << i)) { if (names[0]) strcat_s(names, "|"); strcat_s(names, kCmpFieldNames[i]); }
	Log("lockstep_check: frame %lld (F %lld): the camera read at Present differs from the UpdateViews read in %s (logged up to 10 times)",
		g_frameIndex, g_lkUvF, names);
}

// Present (CamOnPresent): true = this frame's camera was published at UpdateViews; else stamp = the frame id to publish.
static bool LkPresentPublish(int64_t& stamp)
{
	if (!SourcePath()) return false;   // the version-3 path: frameStamp = presentCount, no publish block (unchanged)
	if (g_lkUvF)
	{
		if (g_cfg.lockstepCheck) LkCompareAtPresent();
		return true;
	}
	stamp = std::max(g_lkLastF + 1, g_camPresents);
	return false;
}

static void LkPresentPublished(int64_t F, uint32_t st)
{
	if (!SourcePath()) return;
	g_lkLastF = F;
	int64_t now = Qpc();
	if (!EnsureFrameSection(now)) return;
	LkEnsureSource();
	int64_t q = 0, d = 0;
	LkWritePublish(F, VPPUB_PRESENT, false, q, d);
	g_lkCur.pubF = F; g_lkCur.pubSrc = VPPUB_PRESENT; g_lkCur.pubQpc = q;
	g_lkI.pubPresent++;
	if (g_cfg.lockstep && !g_lkPresentNoteLogged && g_frameIndex > 30)
	{
		g_lkPresentNoteLogged = true;
		Log("lockstep: frame %lld's camera (F %lld, status %s) was published at Present: hook state %ld enabled %ld, UpdateViews calls %ld, "
			"publish %s - such frames do not wait (logged once)", g_frameIndex, F, VPCamStatusName(st), g_lsState, g_lsOn, g_lsUvCount,
			g_lkUvDead ? "disabled by an exception" : "not reached this frame");
	}
}

// Control thread: append the rows written since the last call to shell_composite.lockstep.csv.
static void LkFlush()
{
	LONG h = g_lkHead;
	if (h == g_lkTail) return;
	MemoryBarrier();
	wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%sshell_composite.lockstep.csv", g_outDir);
	bool fresh = GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES;
	FILE* f = _wfsopen(p, L"a", _SH_DENYNO);
	if (!f) return;
	if (fresh)
		fprintf(f, "# qpc_freq=%lld pid=%u kinds=0 exact,1 older,2 none,3 nopub,4 noins pub_src=1 updateviews,2 present\n"
			"frame,pub_f,pub_src,pub_qpc,deadline_qpc,ins_qpc,wait0_qpc,wait1_qpc,taken_f,kind,age,slot,wait_off,depth,cmp,"
			"srv_cam_pub_qpc,srv_wake_qpc,srv_submit_qpc,srv_ready_qpc,srv_cpu_ms,srv_gpu_ms,present_qpc,cam_x,cam_y,cam_z,unbaked,"
			"wu,world_age_ms\n",   // ticket 0688: the last two (wu = -1 with pacing=0)
			g_freq.QuadPart, GetCurrentProcessId());
	if (h - g_lkTail > kLkRing) g_lkTail = h - kLkRing;
	for (; g_lkTail != h; ++g_lkTail)
	{
		const LkRow& r = g_lkRing[g_lkTail % kLkRing];
		fprintf(f, "%lld,%lld,%d,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d,%d,%d,%d,%d,%lld,%lld,%lld,%lld,%.4f,%.4f,%lld,%.2f,%.2f,%.2f,%d,%d,%.3f\n",
			r.frame, r.pubF, r.pubSrc, r.pubQpc, r.deadline, r.insQpc, r.wait0, r.wait1, r.takenF, r.kind, r.age, r.slot, r.waitOff,
			r.depth, r.cmp, r.srvCamPub, r.srvWake, r.srvSubmit, r.srvReady, r.srvCpuMs, r.srvGpuMs, r.presentQpc, r.cx, r.cy, r.cz, r.unbaked,
			r.wu, r.worldAgeMs);
	}
	fclose(f);
}

// ===========================================================================
// Frame-pacing instruments (ticket 0688, epic 0686 P1; shell/common/vp_pacing.h;
// research/08-performance/frame-pacing-instruments.md). ini pacing (1 default; 0 = none of this runs: the cost arm)
// ===========================================================================
// The main thread only counts: PcPresent at every Present of the target (F1: the Present-to-Present interval), PcWorld
// from LkOnPresent on the source path (F2-F4: world updates, their intervals, the world age, freezes), PcSecond from
// StatsTick (the second's numbers into a ring; every 10 s the run-long histograms copied for the control thread). Every
// line is written by the control thread (PcFlush, every 500 ms), never on the main thread (MC #10822 condition 1). A freeze
// is a run of Presents without a world update while the camera moves (vp_pacing.h WorldTracker); one "pacing freeze:"
// line for each of kPcFreezeLogMs or more (at most kPcFreezeLinesPerSec a second; the rest counted).
static const double kPcFreezeLogMs = 50.0;
static const int kPcFreezeLinesPerSec = 32;
struct PcSec
{
	int presents, wu, moving, freezes, freezes50, unlogged, world;
	double ftMax, wuiMax, freezeMs, freezeMax, ageSum, ageMax;
	int64_t selfQpc;   // the main thread's time in PcPresent and PcWorld this second
	int ageN;
	int64_t endQpc;
};
struct PcFreeze { int64_t q0, q1, takenBefore, takenAfter; double ms; int frames; };
struct PcMsg { int kind; PcSec s; PcFreeze f; };   // kind 1 = a second, 2 = a freeze
struct PcRun
{
	vppacing::Hist ft, wui, age;
	uint64_t presents, worldUpdates, freezes, freezes50, freezes100;
	double movingMs, freezeMs, freezeMax;
};
static const LONG kPcRing = 512;
static PcMsg g_pcRing[kPcRing];
static volatile LONG g_pcHead;              // written by the main thread
static LONG g_pcTail;                       // the control thread's
static volatile LONG g_pcDropped;
static PcRun g_pcRun;                       // main thread
static PcRun g_pcSnap;                      // the control thread's copy, handed over by g_pcSnapReady
static volatile LONG g_pcSnapReady;
static int64_t g_pcSnapQpc;                 // main thread: the last copy
static PcSec g_pcSec;                       // main thread: this second
static vppacing::WorldTracker g_pcWt;
static double g_pcLastFtMs;                 // this Present's interval (PcPresent), for PcWorld's moving time
static float g_pcCam[3];
static bool g_pcHaveCam;
static ID3D11Device* g_pcAdDev;             // main thread: the device whose adapter was handed over
static IDXGIAdapter3* volatile g_pcAdNew;   // main -> control: an adapter reference to take over
static IDXGIAdapter3* g_pcAd;               // the control thread's
static int64_t g_pcVidLast;                 // the control thread's last vidmem line
static volatile LONG g_pcStop;              // ticket 0697: 1 = the process ends (VPShell_FlushTotals): nothing more is counted
static volatile LONG g_pcRingBusy;          // ticket 0697: 1 = the ring is being drained (the control thread or the final call)

static void PcPush(const PcMsg& m)
{
	LONG h = g_pcHead;
	if (h - g_pcTail >= kPcRing) { InterlockedIncrement(&g_pcDropped); return; }   // the control thread fell behind
	g_pcRing[h % kPcRing] = m;
	MemoryBarrier();
	InterlockedExchange(&g_pcHead, h + 1);
}

// Main thread, at every Present of the target (before LkOnPresent).
static void PcPresentBody(int64_t now)
{
	g_pcLastFtMs = 0;
	if (!g_cfg.pacing) return;
	g_pcSec.presents++;
	g_pcRun.presents++;
	if (g_prevPresentQpc)
	{
		const double ms = QpcToMs(now - g_prevPresentQpc);
		g_pcLastFtMs = ms;
		g_pcRun.ft.add(ms);
		if (ms > g_pcSec.ftMax) g_pcSec.ftMax = ms;
	}
	// the adapter for the control thread's vidmem line: once per device (no D3D call on the main thread after that)
	if (g_dev && g_dev != g_pcAdDev)
	{
		g_pcAdDev = g_dev;
		IDXGIDevice* dd = nullptr; IDXGIAdapter* a = nullptr; IDXGIAdapter3* a3 = nullptr;
		if (SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd)) && dd)
		{
			if (SUCCEEDED(dd->GetAdapter(&a)) && a)
			{
				if (FAILED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3))) a3 = nullptr;
				a->Release();
			}
			dd->Release();
		}
		if (a3)
		{
			IDXGIAdapter3* old = (IDXGIAdapter3*)InterlockedExchangePointer((void* volatile*)&g_pcAdNew, a3);
			if (old) old->Release();   // never taken by the control thread
		}
	}
}

// Main thread, from LkOnPresent on the source path: the row of this Present is complete.
static void PcWorldBody(LkRow& r)
{
	r.wu = -1; r.worldAgeMs = -1.0f;
	if (!g_cfg.pacing) return;
	g_pcSec.world = 1;
	const bool moved = g_pcHaveCam && (r.cx != g_pcCam[0] || r.cy != g_pcCam[1] || r.cz != g_pcCam[2]);
	g_pcCam[0] = r.cx; g_pcCam[1] = r.cy; g_pcCam[2] = r.cz; g_pcHaveCam = true;
	if (moved) { g_pcSec.moving++; g_pcRun.movingMs += g_pcLastFtMs; }
	const bool shown = r.kind == LK_EXACT || r.kind == LK_OLDER || r.kind == LK_NOPUB;
	const vppacing::PresentResult p = g_pcWt.onPresent(r.presentQpc, shown ? r.takenF : 0, shown ? r.srvCamPub : 0, moved, g_freq.QuadPart);
	r.wu = p.worldUpdate ? 1 : 0;
	r.worldAgeMs = (float)p.ageMs;
	if (p.ageMs >= 0)
	{
		g_pcRun.age.add(p.ageMs);
		g_pcSec.ageSum += p.ageMs; g_pcSec.ageN++;
		if (p.ageMs > g_pcSec.ageMax) g_pcSec.ageMax = p.ageMs;
	}
	if (!p.worldUpdate) return;
	g_pcSec.wu++;
	g_pcRun.worldUpdates++;
	if (p.wuIntervalMs >= 0)
	{
		g_pcRun.wui.add(p.wuIntervalMs);
		if (p.wuIntervalMs > g_pcSec.wuiMax) g_pcSec.wuiMax = p.wuIntervalMs;
	}
	if (!p.freezeEnded) return;
	g_pcSec.freezes++;
	g_pcSec.freezeMs += p.freezeMs;
	if (p.freezeMs > g_pcSec.freezeMax) g_pcSec.freezeMax = p.freezeMs;
	g_pcRun.freezes++;
	g_pcRun.freezeMs += p.freezeMs;
	if (p.freezeMs > g_pcRun.freezeMax) g_pcRun.freezeMax = p.freezeMs;
	if (p.freezeMs >= 100.0) g_pcRun.freezes100++;
	if (p.freezeMs < kPcFreezeLogMs) return;
	g_pcRun.freezes50++;
	if (++g_pcSec.freezes50 > kPcFreezeLinesPerSec) { g_pcSec.unlogged++; return; }
	PcMsg m; memset(&m, 0, sizeof(m));
	m.kind = 2;
	m.f.q0 = p.freezeStartQpc; m.f.q1 = p.freezeEndQpc; m.f.ms = p.freezeMs; m.f.frames = p.freezeFrames;
	m.f.takenAfter = r.takenF;
	PcPush(m);
}

// The instruments' own time on the main thread (the two calls above, their QPC reads included), into "pacing 1s:"
// self_us: the cost the player's frame pays for them, measured in every run (MC #10822 condition 2).
static void PcPresent(int64_t now)
{
	if (!g_cfg.pacing || g_pcStop) { g_pcLastFtMs = 0; return; }
	const int64_t s0 = Qpc();
	PcPresentBody(now);
	g_pcSec.selfQpc += Qpc() - s0;
}

static void PcWorld(LkRow& r)
{
	if (!g_cfg.pacing || g_pcStop) { r.wu = -1; r.worldAgeMs = -1.0f; return; }
	const int64_t s0 = Qpc();
	PcWorldBody(r);
	g_pcSec.selfQpc += Qpc() - s0;
}

// Main thread, from StatsTick: the second's numbers into the ring, and every 10 s a copy of the run for the control thread.
static void PcSecond(int64_t now)
{
	if (!g_cfg.pacing || g_pcStop) return;
	PcMsg m; memset(&m, 0, sizeof(m));
	m.kind = 1;
	m.s = g_pcSec;
	m.s.endQpc = now;
	PcPush(m);
	memset(&g_pcSec, 0, sizeof(g_pcSec));
	if (!g_pcSnapQpc) g_pcSnapQpc = now;
	if (now - g_pcSnapQpc >= 10 * g_freq.QuadPart && !g_pcSnapReady)
	{
		memcpy(&g_pcSnap, &g_pcRun, sizeof(g_pcRun));   // about 100 KB, every 10 s
		g_pcSnapQpc = now;
		MemoryBarrier();
		InterlockedExchange(&g_pcSnapReady, 1);
	}
}

static int PcSumText(char* out, size_t n, const char* k, const vppacing::Summary& s)
{
	char bins[160]; int b = 0; bins[0] = 0;
	for (int i = 0; i < vppacing::kBins; ++i) b += _snprintf_s(bins + b, sizeof(bins) - b, _TRUNCATE, "%s%llu", i ? "/" : "", (unsigned long long)s.bins[i]);
	return _snprintf_s(out, n, _TRUNCATE, " %s_n=%llu %s_mean=%.3f %s_p50=%.1f %s_p99=%.1f %s_p999=%.1f %s_max=%.3f %s_low1=%.2f %s_low01=%.2f "
		"%s_over2x=%llu %s_over50=%llu %s_over100=%llu %s_bins=%s",
		k, (unsigned long long)s.n, k, s.mean, k, s.p50, k, s.p99, k, s.p999, k, s.maxMs, k, s.low1Fps, k, s.low01Fps,
		k, (unsigned long long)s.over2xMedian, k, (unsigned long long)s.over50, k, (unsigned long long)s.over100, k, bins);
}

// The run-long line (the control thread, or the process's end): F1 (ft), F2 (wui), F4 (age), F3's totals.
static void PcTotalsLine(const PcRun& r, const char* why)
{
	char a[600], b[600], c[600];
	PcSumText(a, sizeof(a), "ft", vppacing::Summarize(r.ft));
	PcSumText(b, sizeof(b), "wui", vppacing::Summarize(r.wui));
	PcSumText(c, sizeof(c), "age", vppacing::Summarize(r.age));
	const double perMin = r.movingMs > 0 ? r.freezeMs / (r.movingMs / 60000.0) : 0.0;
	Log("pacing totals: why=%s presents=%llu world_updates=%llu moving_ms=%.0f freezes=%llu freezes50=%llu freezes100=%llu freeze_ms=%.1f "
		"freeze_max=%.1f freeze_ms_per_min=%.1f qpc_now=%lld%s%s%s",
		why, (unsigned long long)r.presents, (unsigned long long)r.worldUpdates, r.movingMs, (unsigned long long)r.freezes,
		(unsigned long long)r.freezes50, (unsigned long long)r.freezes100, r.freezeMs, r.freezeMax, perMin, Qpc(), a, b, c);
}

// The lines of the ring (the caller holds g_pcRingBusy: the control thread, or VPShell_FlushTotals at a fatal end).
static void PcDrain()
{
	LONG h = g_pcHead;
	MemoryBarrier();
	for (; g_pcTail != h; ++g_pcTail)
	{
		const PcMsg& m = g_pcRing[g_pcTail % kPcRing];
		if (m.kind == 1)
		{
			const PcSec& s = m.s;
			Log("pacing 1s: world=%d presents=%d ft_max=%.2f wu=%d wui_max=%.2f moving=%d freezes=%d freeze_ms=%.1f freeze_max=%.1f "
				"freezes50=%d unlogged=%d age_mean=%.2f age_max=%.2f self_us=%.1f self_ns_per_present=%.0f dropped=%ld qpc_end=%lld qpc_now=%lld",
				s.world, s.presents, s.ftMax, s.wu, s.wuiMax, s.moving, s.freezes, s.freezeMs, s.freezeMax, s.freezes50, s.unlogged,
				s.ageN ? s.ageSum / s.ageN : -1.0, s.ageN ? s.ageMax : -1.0, QpcToMs(s.selfQpc) * 1000.0,
				s.presents ? QpcToMs(s.selfQpc) * 1e6 / s.presents : 0.0, (long)g_pcDropped, s.endQpc, Qpc());
		}
		else if (m.kind == 2)
			Log("pacing freeze: ms=%.1f frames=%d qpc0=%lld qpc1=%lld taken_after=%lld", m.f.ms, m.f.frames, m.f.q0, m.f.q1, m.f.takenAfter);
	}
}

// Control thread: the lines of the ring, the run-long line when a copy is ready, the vidmem line every 10 s.
static void PcFlush()
{
	if (InterlockedCompareExchange(&g_pcRingBusy, 1, 0) == 0)   // ticket 0697: taken by the final call only at a fatal end
	{
		PcDrain();
		InterlockedExchange(&g_pcRingBusy, 0);
	}
	if (g_pcSnapReady)
	{
		MemoryBarrier();
		PcTotalsLine(g_pcSnap, "periodic");
		InterlockedExchange(&g_pcSnapReady, 0);
	}
	IDXGIAdapter3* n = (IDXGIAdapter3*)InterlockedExchangePointer((void* volatile*)&g_pcAdNew, nullptr);
	if (n) { if (g_pcAd) g_pcAd->Release(); g_pcAd = n; }
	const int64_t now = Qpc();
	if (g_cfg.pacing && (!g_pcVidLast || now - g_pcVidLast >= 10 * g_freq.QuadPart))
	{
		g_pcVidLast = now;
		PROCESS_MEMORY_COUNTERS_EX pmc; ZeroMemory(&pmc, sizeof(pmc)); pmc.cb = sizeof(pmc);
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
		DXGI_QUERY_VIDEO_MEMORY_INFO lo = {}, nl = {};
		const bool okL = g_pcAd && SUCCEEDED(g_pcAd->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &lo));
		const bool okN = g_pcAd && SUCCEEDED(g_pcAd->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nl));
		const double mb = 1048576.0;
		Log("vidmem 10s: process=exe private_mb=%.1f working_mb=%.1f adapter=%d local_usage_mb=%.1f local_budget_mb=%.1f local_reserved_mb=%.1f "
			"nonlocal_usage_mb=%.1f nonlocal_budget_mb=%.1f qpc_now=%lld",
			pmc.PrivateUsage / mb, pmc.WorkingSetSize / mb, g_pcAd ? 1 : 0, okL ? lo.CurrentUsage / mb : -1.0, okL ? lo.Budget / mb : -1.0,
			okL ? lo.CurrentReservation / mb : -1.0, okN ? nl.CurrentUsage / mb : -1.0, okN ? nl.Budget / mb : -1.0, now);
	}
}

// ---- ticket 0697: the run-long line at a fatal end ----------------------------------------------------------------------
// The proxy's Fatal (proxy_rt.cpp) ends the process with TerminateProcess: no DLL_PROCESS_DETACH, so the why=exit line
// never came in the runs that need it most (a hung or failed server). Before it, the proxy calls this export on a thread of
// its own and waits at most its own bound (proxy_shell.cpp FlushCompositeTotals). Here: the main thread stops counting
// (g_pcStop), the ring's lines, one "pacing: the process ends" line with the proxy's detail, then "pacing totals: why=<why>"
// (the format of the other two) from a copy of the run.
// Once per process. Every lock is taken by a bounded try within timeoutMs (the ring first, then the log, the control
// thread's order); without the log lock nothing is written. Returns 1 when the lines were written.
static volatile LONG g_pcFinal;             // 1 = the one final call of this process is claimed
static PcRun g_pcFinalRun;                  // the copy the final line is made from

static bool PcTryUntil(volatile LONG* flag, DWORD t0, DWORD timeoutMs)
{
	for (;;)
	{
		if (InterlockedCompareExchange(flag, 1, 0) == 0) return true;
		if (GetTickCount() - t0 >= timeoutMs) return false;
		Sleep(1);
	}
}

static bool PcTryLogLock(DWORD t0, DWORD timeoutMs)
{
	for (;;)
	{
		if (TryEnterCriticalSection(&g_logLock)) return true;   // Log() enters it again on this thread (recursive)
		if (GetTickCount() - t0 >= timeoutMs) return false;
		Sleep(1);
	}
}

static void PcRecount(vppacing::Hist& h)
{
	uint64_t c = 0;
	for (int b = 0; b < vppacing::Hist::kN; ++b) c += h.n[b];
	h.count = c;   // a copy taken while the main thread counted: its buckets decide (the bins can never underflow)
}

extern "C" __declspec(dllexport) int __cdecl VPShell_FlushTotals(const char* why, const char* detail, unsigned timeoutMs)
{
	if (InterlockedCompareExchange(&g_pcFinal, 1, 0) != 0) return 0;
	const DWORD t0 = GetTickCount();
	const char* w = why && *why ? why : "fatal";
	InterlockedExchange(&g_pcStop, 1);
	Sleep(2);   // a PcPresent / PcWorld already past its g_pcStop check ends (microseconds)
	const bool ring = PcTryUntil(&g_pcRingBusy, t0, timeoutMs);
	int wrote = 0;
	if (PcTryLogLock(t0, timeoutMs))
	{
		if (ring) PcDrain();
		Log("pacing: the process ends (why=%s): %s; the ring %s (ticket 0697)", w, detail && *detail ? detail : "(no detail)",
			ring ? "drained" : "busy: its last lines are lost");
		memcpy(&g_pcFinalRun, (const void*)&g_pcRun, sizeof(g_pcRun));
		PcRecount(g_pcFinalRun.ft); PcRecount(g_pcFinalRun.wui); PcRecount(g_pcFinalRun.age);
		if (g_pcFinalRun.presents) PcTotalsLine(g_pcFinalRun, w);
		else Log("pacing: why=%s: no Present counted: no totals line", w);
		LeaveCriticalSection(&g_logLock);
		wrote = 1;
	}
	if (ring) InterlockedExchange(&g_pcRingBusy, 0);
	return wrote;
}

// ===========================================================================
// Height-field tap (epic 3200, issue 3203)
// ===========================================================================
// Once per map load a worker thread copies the EXE's GridHeightmap (object 0xC8 B at 0xD91270, both FDataField layers:
// directories and pools; layout in render/include/c5r/heightfield/heightfield.h and asset-pipeline.md 4.2/4.3) into a
// pagefile-backed named section in the C5HF format and publishes it in the section's tap block (shell_shared.h
// VPShellTap: states, ack protocol). When: the camera block's grid seq changed, gridValid is 1, and the camera has been
// OK for tap_delay_frames Presents (GridHeightmap::Init sets the ready byte before CreateTerrain fills the field; the
// camera is OK only once the map is up). The worker:
//   * reads the object once and checks it (the checks of c5r::heightfield::parseGridObject plus pointer ranges and the
//     size limit), copies both directories into its own memory and checks every entry (state 0 or 1, pool index inside
//     the pool) BEFORE any pool read; every read of EXE memory runs under SEH (a fault = FAILED EXCEPTION, never a crash);
//   * flattens one band of 16 sample rows at a time into a local buffer, hashes it (FNV-1a 64 over the payload in
//     order) and writes it through a view of at most tap_chunk_kb, unmapped as soon as the next band lies outside it;
//   * re-reads the object (and [0xD90FC4]) at the end: any change = FAILED CHANGED (a load started meanwhile);
//   * publishes READY, waits for the ack (or the timeout, or a newer load) and closes the section handle.
// Nothing of the EXE is written. Test seam (tap_test=, test_host has no field): the object comes from this DLL's own
// memory instead of the EXE's, synthetic (synth, small) or hostile (the bad-* modes, too-big, change, bad-section,
// not-ready); tap_test_load=N changes simulate a new load (the grid seq is bumped).
struct TapJob
{
	uint32_t loadSeq, copySeq, reqSeq;
	int test;
	int byteLayer;
	DWORD ackTimeoutMs;
	uint32_t chunkBytes;
	uint32_t camSamplesX, camSamplesY;   // the camera block's grid size (synthetic fields take it)
	int32_t camWrapX;
	float camH2w[16];
	wchar_t mainSection[128];
};
struct TapLayer { uint32_t w, h, tx, ty, dir, freeHead, inUse, pool, poolSize, poolCap; };

static TapJob g_tapJob;                  // written by the render thread before the worker starts, read-only after
static volatile LONG g_tapBusy;          // 1 while the worker runs (it is the only writer of the tap block then)
static volatile LONG g_tapLastState;     // last state the worker published (stats line)
static volatile LONG g_tapLastFail;
static volatile LONG g_tapCopies;        // copies started (copySeq)
static uint32_t g_tapStartedSeq, g_tapStartedReq;
static bool g_tapStartedAny;
static int g_tapTestLoadSeen;
static bool g_tapDead;                   // an exception on the render thread's part: off for this process
static bool g_sceneThisFrame;            // this frame executed the scene-clear list (clear_bb + clear_dsv): a map frame
static const char* kTapStateNames[] = { "NONE", "WRITING", "READY", "RELEASED", "FAILED", "OFF" };
static const char* kTapFailNames[] = { "NONE", "GRID_INVALID", "SECTION", "EXCEPTION", "TOO_BIG", "ACK_TIMEOUT", "CHANGED", "NO_SECTION" };

static inline const char* TapStateName(uint32_t s) { return s < 6 ? kTapStateNames[s] : "?"; }
static inline const char* TapFailName(uint32_t f) { return f < 8 ? kTapFailNames[f] : "?"; }

static inline uint32_t TapRd32(const uint8_t* p, size_t at) { uint32_t v; memcpy(&v, p + at, 4); return v; }
static inline float TapRdF(const uint8_t* p, size_t at) { float v; memcpy(&v, p + at, 4); return v; }

static void TapFnv(uint64_t& h, const uint8_t* p, size_t n)
{
	uint64_t x = h;
	for (size_t i = 0; i < n; ++i) { x ^= p[i]; x *= 0x100000001B3ull; }
	h = x;
}

// SEH-guarded copy from EXE memory (no C++ objects in this frame). false = access violation (or any exception).
static bool TapSafeCopy(void* dst, uintptr_t src, size_t n)
{
	__try { memcpy(dst, (const void*)src, n); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); return false; }
}

// One band (16 sample rows) of a layer, flattened from the directory copy and the EXE's pool. Entries were checked
// before (state 0/1, index < poolSize). SEH-guarded: false on a fault.
static bool TapReadBand(const uint8_t* dir, const TapLayer& L, uint32_t ty, uint32_t bps, uint8_t* band)
{
	__try
	{
		const uint32_t rowBytes = L.w * bps, tileRow = 16 * bps, tileBytes = 256 * bps;
		for (uint32_t tx = 0; tx < L.tx; ++tx)
		{
			const uint8_t* e = dir + ((size_t)ty * L.tx + tx) * 8;
			uint16_t state; memcpy(&state, e, 2);
			uint32_t payload = TapRd32(e, 4);
			uint8_t* dst = band + (size_t)tx * tileRow;
			if (state == 1)
			{
				for (uint32_t y = 0; y < 16; ++y)
				{
					uint8_t* row = dst + (size_t)y * rowBytes;
					if (bps == 2) { uint16_t v = (uint16_t)(payload & 0xFFFF); for (uint32_t x = 0; x < 16; ++x) memcpy(row + x * 2, &v, 2); }
					else memset(row, (int)(payload & 0xFF), 16);
				}
			}
			else
			{
				const uint8_t* src = (const uint8_t*)(uintptr_t)(L.pool + payload * tileBytes);
				for (uint32_t y = 0; y < 16; ++y) memcpy(dst + (size_t)y * rowBytes, src + y * tileRow, tileRow);
			}
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); return false; }
}

// Seqlock publish of the producer part [state .. sectionName] (the worker or, when no worker runs, the render thread).
static void TapPublish(VPShellTap* t, const VPShellTap& v)
{
	LONG s = InterlockedIncrement((volatile LONG*)&t->seq);
	if (!(s & 1)) InterlockedIncrement((volatile LONG*)&t->seq);
	MemoryBarrier();
	memcpy((char*)t + 4, (const char*)&v + 4, offsetof(VPShellTap, ackLoadSeq) - 4);
	MemoryBarrier();
	InterlockedIncrement((volatile LONG*)&t->seq);
	InterlockedExchange(&g_tapLastState, (LONG)v.state);
	InterlockedExchange(&g_tapLastFail, (LONG)v.failReason);
}

static void TapLayerFrom(const uint8_t* o, size_t at, TapLayer& L)
{
	L.w = TapRd32(o, at + 0x00); L.h = TapRd32(o, at + 0x04); L.tx = TapRd32(o, at + 0x08); L.ty = TapRd32(o, at + 0x0C);
	L.dir = TapRd32(o, at + 0x10); L.freeHead = TapRd32(o, at + 0x14); L.inUse = TapRd32(o, at + 0x18);
	L.pool = TapRd32(o, at + 0x1C); L.poolSize = TapRd32(o, at + 0x20); L.poolCap = TapRd32(o, at + 0x24);
}

static bool TapFinitePos(float f) { return f == f && f > 0.0f && f < 1e30f; }

// The object checks (those of c5r::heightfield::parseGridObject, plus null and wrapping pointers). 0 = ok, else a fail code.
static uint32_t TapCheckObject(const uint8_t* o, bool byteLayer, TapLayer& h16, TapLayer& b8, uint64_t& payload, char* why, size_t n)
{
	if (o[0x09] != 1) { _snprintf_s(why, n, _TRUNCATE, "ready byte +0x09 is %u", o[0x09]); return VPTAPF_GRID_INVALID; }
	const uint32_t sX = TapRd32(o, 0xA0), sY = TapRd32(o, 0xA4), maxS = 256 * 64;
	if (!sX || !sY || sX % 64 || sY % 64 || sX > maxS || sY > maxS)
	{ _snprintf_s(why, n, _TRUNCATE, "samples %ux%u are not a map of 64-sample plots (at most 256)", sX, sY); return VPTAPF_GRID_INVALID; }
	if (!TapFinitePos(TapRdF(o, 0xA8)) || !TapFinitePos(TapRdF(o, 0xAC)) || !TapFinitePos(TapRdF(o, 0xB0)))
	{ _snprintf_s(why, n, _TRUNCATE, "scale sx/sy/sz not finite and positive"); return VPTAPF_GRID_INVALID; }
	const uint64_t cnt = (uint64_t)sX * sY;
	payload = 128 + cnt * 2 + (byteLayer ? cnt : 0);
	if (payload > VPSHELL_TAP_MAX_BYTES)
	{ _snprintf_s(why, n, _TRUNCATE, "payload %llu bytes above the limit %llu", payload, (unsigned long long)VPSHELL_TAP_MAX_BYTES); return VPTAPF_TOO_BIG; }
	TapLayerFrom(o, 0x0C, h16);
	TapLayerFrom(o, 0x4C, b8);
	for (int k = 0; k < (byteLayer ? 2 : 1); ++k)
	{
		const TapLayer& L = k ? b8 : h16;
		const char* what = k ? "byte layer +0x4C" : "ushort layer +0x0C";
		const uint64_t tileBytes = k ? 256 : 512;
		if (L.w != sX || L.h != sY) { _snprintf_s(why, n, _TRUNCATE, "%s: layer %ux%u != samples %ux%u", what, L.w, L.h, sX, sY); return VPTAPF_GRID_INVALID; }
		if (L.tx != sX / 16 || L.ty != sY / 16) { _snprintf_s(why, n, _TRUNCATE, "%s: tiles %ux%u do not cover %ux%u", what, L.tx, L.ty, sX, sY); return VPTAPF_GRID_INVALID; }
		if (!L.dir || !L.pool) { _snprintf_s(why, n, _TRUNCATE, "%s: directory 0x%08X or pool 0x%08X is null", what, L.dir, L.pool); return VPTAPF_GRID_INVALID; }
		const uint64_t dirEnd = (uint64_t)L.dir + (uint64_t)L.tx * L.ty * 8, poolEnd = (uint64_t)L.pool + (uint64_t)L.poolSize * tileBytes;
		if (dirEnd > 0xFFFFFFFFull || poolEnd > 0xFFFFFFFFull || L.poolSize == 0)
		{ _snprintf_s(why, n, _TRUNCATE, "%s: directory 0x%08X..0x%llX or pool 0x%08X..0x%llX (%u tiles) outside the address space", what, L.dir, dirEnd, L.pool, poolEnd, L.poolSize); return VPTAPF_GRID_INVALID; }
	}
	return VPTAPF_NONE;
}

// Every directory entry: state 0 (pooled, index < poolSize) or 1 (constant). Checked before any pool read.
static uint32_t TapCheckDir(const uint8_t* dir, const TapLayer& L, const char* what, char* why, size_t n)
{
	for (uint32_t ty = 0; ty < L.ty; ++ty)
		for (uint32_t tx = 0; tx < L.tx; ++tx)
		{
			const uint8_t* e = dir + ((size_t)ty * L.tx + tx) * 8;
			uint16_t state; memcpy(&state, e, 2);
			uint32_t payload = TapRd32(e, 4);
			if (state > 1) { _snprintf_s(why, n, _TRUNCATE, "%s: tile %u,%u directory state %u", what, tx, ty, state); return VPTAPF_GRID_INVALID; }
			if (state == 0 && payload >= L.poolSize) { _snprintf_s(why, n, _TRUNCATE, "%s: tile %u,%u pool index %u past the pool of %u tiles", what, tx, ty, payload, L.poolSize); return VPTAPF_GRID_INVALID; }
		}
	return VPTAPF_NONE;
}

// ---- the synthetic / hostile source of tap_test (this DLL's own memory; never the EXE's) ----
struct TapSynth { uint8_t obj[0xC8]; uint8_t* dir16; uint8_t* pool16; uint8_t* dir8; uint8_t* pool8; void* reserved; };

// Content (shell/tests/tapcheck.py recomputes it): u16 tile (tx, ty) constant when (tx + 3 ty) % 11 == 0, value
// (7 tx + 131 ty) & 0xFFFF, else sample (x, y) = (31 x + 17 y + (x >> 4) (y >> 4)) & 0xFFFF; u8 tile constant when
// (5 tx + ty) % 13 == 0, value (tx + ty) & 0xFF, else (x ^ 3 y) & 0xFF. Pooled tiles sit in the pool in the order
// k -> (k * 7919) % nPooled (k = row-major rank among the pooled tiles), so the directory is really followed.
static void TapSynthFree(TapSynth& s)
{
	uint8_t* p[4] = { s.dir16, s.pool16, s.dir8, s.pool8 };
	for (uint8_t* q : p) if (q) VirtualFree(q, 0, MEM_RELEASE);
	if (s.reserved) VirtualFree(s.reserved, 0, MEM_RELEASE);
	memset(&s, 0, sizeof(s));
}

static bool TapSynthBuild(const TapJob& j, TapSynth& s)
{
	memset(&s, 0, sizeof(s));
	uint32_t sX = j.camSamplesX, sY = j.camSamplesY;
	if (j.test == TAPT_SMALL) { sX = 8 * 64; sY = 6 * 64; }
	if (!sX || !sY || sX % 64 || sY % 64 || sX > 256 * 64 || sY > 256 * 64) { sX = 8 * 64; sY = 6 * 64; }
	const uint32_t TX = sX / 16, TY = sY / 16;
	uint8_t* o = s.obj;
	float one = 1.0f, sc[5] = { 1.0f, 0.866f, 0.2f, sX * 0.5f, sY * 0.5f * 0.866f }, water = 12.0f;
	memcpy(o + 0x00, &one, 4); memcpy(o + 0x04, &water, 4); o[0x08] = 1; o[0x09] = j.test == TAPT_NOT_READY ? 0 : 1;
	memcpy(o + 0xA0, &sX, 4); memcpy(o + 0xA4, &sY, 4); memcpy(o + 0xA8, sc, 20);
	float ww = sX * 1.0f, wh = sY * 0.866f; memcpy(o + 0xC0, &ww, 4); memcpy(o + 0xC4, &wh, 4);
	if (j.test == TAPT_TOO_BIG)
	{
		const uint32_t big = 256 * 64;
		memcpy(o + 0xA0, &big, 4); memcpy(o + 0xA4, &big, 4);
		for (size_t at : { (size_t)0x0C, (size_t)0x4C }) { uint32_t t = big / 16; memcpy(o + at, &big, 4); memcpy(o + at + 4, &big, 4); memcpy(o + at + 8, &t, 4); memcpy(o + at + 12, &t, 4); }
		return true;   // nothing allocated: the size check must refuse it before any read
	}
	for (int k = 0; k < 2; ++k)
	{
		const uint32_t bps = k ? 1 : 2;
		uint8_t* dir = (uint8_t*)VirtualAlloc(nullptr, (size_t)TX * TY * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!dir) { TapSynthFree(s); return false; }
		uint32_t nPooled = 0;
		for (uint32_t ty = 0; ty < TY; ++ty) for (uint32_t tx = 0; tx < TX; ++tx)
			if (k ? (5 * tx + ty) % 13 != 0 : (tx + 3 * ty) % 11 != 0) ++nPooled;
		uint8_t* pool = (uint8_t*)VirtualAlloc(nullptr, (size_t)std::max(1u, nPooled) * 256 * bps, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!pool) { VirtualFree(dir, 0, MEM_RELEASE); TapSynthFree(s); return false; }
		const bool perm = nPooled % 7919 != 0;
		uint32_t rank = 0;
		for (uint32_t ty = 0; ty < TY; ++ty)
			for (uint32_t tx = 0; tx < TX; ++tx)
			{
				uint8_t* e = dir + ((size_t)ty * TX + tx) * 8;
				bool constant = k ? (5 * tx + ty) % 13 == 0 : (tx + 3 * ty) % 11 == 0;
				if (constant)
				{
					uint16_t st = 1; uint32_t v = k ? ((tx + ty) & 0xFF) : ((7 * tx + 131 * ty) & 0xFFFF);
					memcpy(e, &st, 2); memcpy(e + 4, &v, 4);
					continue;
				}
				uint32_t idx = perm ? (uint32_t)(((uint64_t)rank * 7919) % nPooled) : rank;
				++rank;
				uint16_t st = 0; memcpy(e, &st, 2); memcpy(e + 4, &idx, 4);
				uint8_t* t = pool + (size_t)idx * 256 * bps;
				for (uint32_t y = 0; y < 16; ++y)
					for (uint32_t x = 0; x < 16; ++x)
					{
						uint32_t X = tx * 16 + x, Y = ty * 16 + y;
						if (k) t[y * 16 + x] = (uint8_t)((X ^ (3 * Y)) & 0xFF);
						else { uint16_t v = (uint16_t)((31 * X + 17 * Y + (X >> 4) * (Y >> 4)) & 0xFFFF); memcpy(t + (y * 16 + x) * 2, &v, 2); }
					}
			}
		const size_t at = k ? 0x4C : 0x0C;
		uint32_t f[10] = { sX, sY, TX, TY, (uint32_t)(uintptr_t)dir, 0x0FFFFFFF, nPooled, (uint32_t)(uintptr_t)pool, nPooled, nPooled };
		memcpy(o + at, f, 40);
		(k ? s.dir8 : s.dir16) = dir; (k ? s.pool8 : s.pool16) = pool;
	}
	// hostile variants
	s.reserved = VirtualAlloc(nullptr, 1 << 20, MEM_RESERVE, PAGE_NOACCESS);   // reserved, never committed: reads fault
	uint32_t resv = (uint32_t)(uintptr_t)s.reserved;
	auto put = [&](size_t at, uint32_t v) { memcpy(o + at, &v, 4); };
	switch (j.test)
	{
	case TAPT_BAD_SIZE: put(0xA0, sX + 16); break;                                    // samples not a multiple of 64
	case TAPT_BAD_TILES: put(0x0C + 0x08, TX - 1); break;                              // tiles do not cover the samples
	case TAPT_BAD_STATE: { uint16_t st = 5; memcpy(s.dir16 + 7 * 8, &st, 2); } break;  // unknown directory state
	case TAPT_BAD_INDEX:                                                              // pooled index past the pool
	{
		uint32_t n16 = TapRd32(o, 0x0C + 0x20), bad = n16 + 3; uint16_t st = 0;
		memcpy(s.dir16 + 9 * 8, &st, 2); memcpy(s.dir16 + 9 * 8 + 4, &bad, 4);
	} break;
	case TAPT_NULL_DIR: put(0x0C + 0x10, 0); break;
	case TAPT_WRAP_PTR: put(0x4C + 0x1C, 0xFFFFF000u); break;                          // pool range wraps past 4 GB
	case TAPT_BAD_DIR: put(0x0C + 0x10, resv); break;                                  // directory in uncommitted memory
	case TAPT_BAD_POOL: put(0x4C + 0x1C, resv); break;                                 // byte pool in uncommitted memory
	default: break;
	}
	return true;
}

static void TapFill(const TapJob& j, VPShellTap& v)
{
	memset(&v, 0, sizeof(v));
	v.loadSeq = j.loadSeq; v.copySeq = j.copySeq; v.exePid = GetCurrentProcessId();
}

// The whole copy for one job, on the worker thread. Publishes into t; returns nothing (state says it all).
static void TapRun(const TapJob& j, VPShellTap* t)
{
	int64_t t0 = Qpc();
	VPShellTap v; TapFill(j, v);
	char why[320] = "";
	TapSynth syn; memset(&syn, 0, sizeof(syn));
	uint8_t* dir16 = nullptr; uint8_t* dir8 = nullptr; uint8_t* band = nullptr;
	HANDLE sec = nullptr; uint8_t* view = nullptr; uint64_t viewOff = 0, viewLen = 0;
	uint32_t fail = VPTAPF_NONE;
	double hashMs = 0, readMs = 0; uint32_t views = 0;
	auto finish = [&](uint32_t f, const char* text) {
		if (view) { UnmapViewOfFile(view); view = nullptr; }
		if (sec) { CloseHandle(sec); sec = nullptr; }
		v.state = VPTAP_FAILED; v.failReason = f;
		TapPublish(t, v);
		Log("tap: FAILED %s (load %u copy %u): %s", TapFailName(f), j.loadSeq, j.copySeq, text);
	};
	// ---- the object ----
	uintptr_t grid;
	int32_t wrapX = j.camWrapX;
	float h2w[16]; memcpy(h2w, j.camH2w, 64);
	if (j.test)
	{
		if (!TapSynthBuild(j, syn)) { finish(VPTAPF_SECTION, "tap_test: could not allocate the synthetic field"); return; }
		grid = (uintptr_t)syn.obj;
	}
	else
	{
		grid = CamA(VPCAM_VA_GRID);
		uint32_t gp = 0;
		if (!TapSafeCopy(&gp, CamA(VPCAM_VA_GRIDPTR), 4) || !TapSafeCopy(&wrapX, CamA(VPCAM_VA_WRAPX), 4) || !TapSafeCopy(h2w, CamA(VPCAM_VA_H2W), 64))
		{ finish(VPTAPF_EXCEPTION, "fault reading the grid pointer, wrap flag or M_h2w"); return; }
		if (gp != (uint32_t)grid) { _snprintf_s(why, _TRUNCATE, "[cam_gridptr] = 0x%08X is not the GridHeightmap 0x%08X (no map)", gp, (uint32_t)grid); finish(VPTAPF_GRID_INVALID, why); return; }
	}
	uint8_t obj[0xC8], obj2[0xC8];
	if (!TapSafeCopy(obj, grid, sizeof(obj))) { finish(VPTAPF_EXCEPTION, "fault reading the GridHeightmap object"); TapSynthFree(syn); return; }
	TapLayer h16, b8; uint64_t payload = 0;
	fail = TapCheckObject(obj, j.byteLayer != 0, h16, b8, payload, why, sizeof(why));
	if (fail) { finish(fail, why); TapSynthFree(syn); return; }
	const uint32_t sX = TapRd32(obj, 0xA0), sY = TapRd32(obj, 0xA4);
	v.mapW = sX / 64; v.mapH = sY / 64; v.samplesX = sX; v.samplesY = sY;
	v.flags = (wrapX == 1 ? 1u : 0u) | (j.byteLayer ? 2u : 0u);
	v.payloadBytes = payload;
	v.sectionBytes = (payload + 0xFFFF) & ~0xFFFFull;
	// ---- directories: copied into our memory and checked before any pool read ----
	const size_t dirBytes = (size_t)h16.tx * h16.ty * 8;
	dir16 = (uint8_t*)VirtualAlloc(nullptr, dirBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	dir8 = j.byteLayer ? (uint8_t*)VirtualAlloc(nullptr, dirBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
	band = (uint8_t*)VirtualAlloc(nullptr, (size_t)sX * 2 * 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	auto cleanup = [&]() {
		if (dir16) VirtualFree(dir16, 0, MEM_RELEASE);
		if (dir8) VirtualFree(dir8, 0, MEM_RELEASE);
		if (band) VirtualFree(band, 0, MEM_RELEASE);
		dir16 = dir8 = band = nullptr;
		TapSynthFree(syn);
	};
	if (!dir16 || !band || (j.byteLayer && !dir8)) { finish(VPTAPF_SECTION, "out of memory for the directory copies"); cleanup(); return; }
	if (!TapSafeCopy(dir16, h16.dir, dirBytes) || (dir8 && !TapSafeCopy(dir8, b8.dir, dirBytes)))
	{ _snprintf_s(why, _TRUNCATE, "fault reading a tile directory (0x%08X / 0x%08X, %u bytes each)", h16.dir, b8.dir, (uint32_t)dirBytes); finish(VPTAPF_EXCEPTION, why); cleanup(); return; }
	fail = TapCheckDir(dir16, h16, "ushort layer", why, sizeof(why));
	if (!fail && dir8) fail = TapCheckDir(dir8, b8, "byte layer", why, sizeof(why));
	if (fail) { finish(fail, why); cleanup(); return; }
	// ---- the section ----
	if (j.test == TAPT_BAD_SECTION) _snwprintf_s(v.sectionName, _TRUNCATE, L"Local\\VPShellTap\\no\\such\\%u", j.copySeq);
	else _snwprintf_s(v.sectionName, _TRUNCATE, VPSHELL_TAP_SECTION_FMT, v.exePid, j.loadSeq, j.copySeq);
	sec = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT, (DWORD)(v.sectionBytes >> 32), (DWORD)v.sectionBytes, v.sectionName);
	DWORD cerr = GetLastError();
	if (!sec) { _snprintf_s(why, _TRUNCATE, "CreateFileMapping('%ls', %llu bytes) failed, error %u", v.sectionName, v.sectionBytes, cerr); finish(VPTAPF_SECTION, why); cleanup(); return; }
	if (cerr == ERROR_ALREADY_EXISTS) { _snprintf_s(why, _TRUNCATE, "section '%ls' already exists (another holder)", v.sectionName); finish(VPTAPF_SECTION, why); cleanup(); return; }
	v.state = VPTAP_WRITING;
	TapPublish(t, v);
	uint64_t h = 0xCBF29CE484222325ull;
	uint32_t peakView = 0;
	// write [off, off + n) of the payload from src, mapping views of at most chunkBytes (64 KiB aligned), unmapped at once
	auto put = [&](uint64_t off, const uint8_t* src, size_t n) -> bool {
		if (!view || off < viewOff || off + n > viewOff + viewLen)
		{
			if (view) { UnmapViewOfFile(view); view = nullptr; }
			viewOff = off & ~0xFFFFull;
			viewLen = std::min<uint64_t>(std::max<uint64_t>(j.chunkBytes, off + n - viewOff), v.sectionBytes - viewOff);
			view = (uint8_t*)MapViewOfFile(sec, FILE_MAP_WRITE, (DWORD)(viewOff >> 32), (DWORD)viewOff, (SIZE_T)viewLen);
			if (!view) return false;
			views++; peakView = std::max(peakView, (uint32_t)viewLen);
		}
		memcpy(view + (off - viewOff), src, n);
		return true;
	};
	// C5HF header (c5r::heightfield::writeTap)
	uint8_t hd[128]; memset(hd, 0, sizeof(hd));
	memcpy(hd, "C5HF", 4);
	uint32_t hv[6] = { 1u, v.mapW, v.mapH, sX, sY, v.flags };
	memcpy(hd + 4, hv, 24);
	memcpy(hd + 28, obj + 0xA8, 20);           // sx, sy, sz, ox, oy
	memcpy(hd + 48, obj + 0x04, 4);            // water z (0xD91274)
	memcpy(hd + 56, h2w, 64);                  // mapSeed (52) stays 0: not tapped
	TapFnv(h, hd, 128);
	if (!put(0, hd, 128)) { _snprintf_s(why, _TRUNCATE, "MapViewOfFile failed, error %u", GetLastError()); finish(VPTAPF_SECTION, why); cleanup(); return; }
	uint64_t off = 128;
	for (int k = 0; k < (j.byteLayer ? 2 : 1) && !fail; ++k)
	{
		const TapLayer& L = k ? b8 : h16;
		const uint32_t bps = k ? 1 : 2;
		const size_t bandBytes = (size_t)sX * bps * 16;
		for (uint32_t ty = 0; ty < L.ty; ++ty)
		{
			int64_t r0 = Qpc();
			if (!TapReadBand(k ? dir8 : dir16, L, ty, bps, band))
			{ _snprintf_s(why, _TRUNCATE, "fault reading the %s pool (0x%08X, %u tiles) in tile row %u", k ? "byte" : "ushort", L.pool, L.poolSize, ty); fail = VPTAPF_EXCEPTION; break; }
			int64_t r1 = Qpc();
			TapFnv(h, band, bandBytes);
			int64_t r2 = Qpc();
			readMs += QpcToMs(r1 - r0); hashMs += QpcToMs(r2 - r1);
			if (!put(off, band, bandBytes)) { _snprintf_s(why, _TRUNCATE, "MapViewOfFile at %llu failed, error %u", viewOff, GetLastError()); fail = VPTAPF_SECTION; break; }
			off += bandBytes;
			if (j.test == TAPT_CHANGE && k == 0 && ty == L.ty / 2) { float ox = TapRdF(syn.obj, 0xB4) + 1.0f; memcpy(syn.obj + 0xB4, &ox, 4); }
		}
	}
	if (fail) { finish(fail, why); cleanup(); return; }
	if (view) { UnmapViewOfFile(view); view = nullptr; }
	// ---- the field must not have changed meanwhile ----
	uint32_t gp2 = (uint32_t)grid;
	if (!TapSafeCopy(obj2, grid, sizeof(obj2)) || (!j.test && !TapSafeCopy(&gp2, CamA(VPCAM_VA_GRIDPTR), 4)))
	{ finish(VPTAPF_EXCEPTION, "fault re-reading the GridHeightmap object after the copy"); cleanup(); return; }
	if (memcmp(obj, obj2, sizeof(obj)) != 0 || gp2 != (uint32_t)grid)
	{
		int at = 0; while (at < (int)sizeof(obj) && obj[at] == obj2[at]) ++at;
		_snprintf_s(why, _TRUNCATE, "the GridHeightmap object changed during the copy (first differing byte +0x%02X; grid pointer 0x%08X): a load started", at, gp2);
		finish(VPTAPF_CHANGED, why); cleanup(); return;
	}
	cleanup();
	v.fnv1a64 = h;
	v.readyQpc = Qpc();
	v.copyMs = (float)QpcToMs(v.readyQpc - t0);
	v.state = VPTAP_READY;
	TapPublish(t, v);
	Log("tap: READY load %u copy %u: map %ux%u plots (samples %ux%u, wrap %d, byte layer %d), %llu bytes in section '%ls' (%llu bytes); "
		"copy %.1f ms (pool reads %.1f ms, hash %.1f ms), %u views of at most %u KiB; fnv1a64 %016llX; pools 0x%08X (%u tiles) / 0x%08X (%u tiles)",
		j.loadSeq, j.copySeq, v.mapW, v.mapH, sX, sY, wrapX, j.byteLayer, v.payloadBytes, v.sectionName, v.sectionBytes, v.copyMs, readMs, hashMs,
		views, peakView / 1024, h, h16.pool, h16.poolSize, b8.pool, b8.poolSize);
	// ---- wait for the ack, a timeout or a newer load ----
	const int64_t deadline = v.readyQpc + (int64_t)j.ackTimeoutMs * g_freq.QuadPart / 1000;
	for (;;)
	{
		if (t->ackLoadSeq == j.loadSeq && t->ackCopySeq == j.copySeq)
		{
			MemoryBarrier();
			int32_t res = t->ackResult; uint32_t pid = t->ackServerPid;
			CloseHandle(sec); sec = nullptr;
			v.state = VPTAP_RELEASED;
			TapPublish(t, v);
			Log("tap: RELEASED load %u copy %u: ack from pid %u, result %d%s, %.0f ms after READY; section closed", j.loadSeq, j.copySeq, pid, res,
				res == 0 ? "" : " (the consumer refused the copy)", QpcToMs(Qpc() - v.readyQpc));
			return;
		}
		if (Qpc() > deadline)
		{
			_snprintf_s(why, _TRUNCATE, "no ack within %u ms; section closed", j.ackTimeoutMs);
			finish(VPTAPF_ACK_TIMEOUT, why);
			return;
		}
		if (g_gridSeq != j.loadSeq)
		{
			_snprintf_s(why, _TRUNCATE, "a new load (grid seq %u) superseded the unacknowledged copy; section closed", g_gridSeq);
			finish(VPTAPF_CHANGED, why);
			return;
		}
		Sleep(10);
	}
}

static DWORD WINAPI TapWorker(LPVOID)
{
	TapJob j = g_tapJob;
	HANDLE ms = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, j.mainSection);
	VPShellHeader* hdr = ms ? (VPShellHeader*)MapViewOfFile(ms, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, VPSHELL_SECTION_BYTES) : nullptr;
	if (!hdr)
	{
		Log("tap: the worker could not map the main section '%ls' (error %u): load %u not tapped", j.mainSection, GetLastError(), j.loadSeq);
		if (ms) CloseHandle(ms);
		InterlockedExchange(&g_tapLastState, VPTAP_FAILED);
		InterlockedExchange(&g_tapLastFail, VPTAPF_NO_SECTION);
		InterlockedExchange(&g_tapBusy, 0);
		return 0;
	}
	__try { TapRun(j, VPSHELL_TAP(hdr)); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); Log("tap: EXCEPTION 0x%08X in the tap worker (load %u): copy abandoned", GetExceptionCode(), j.loadSeq); }
	UnmapViewOfFile(hdr);
	CloseHandle(ms);
	InterlockedExchange(&g_tapBusy, 0);
	return 0;
}

// The load gate (ticket 0408; research/05-graphics-composition/terrain-surface-verification.md 14, cause 1). When may a
// copy of the current load start? The field is complete from the return of CreateTerrain (load stage 2) until the next
// load, but GridHeightmap::Init sets the ready byte (gridValid) at its start, so gridValid alone is not enough.
//  * A reload in the same process: the camera block stays OK through the load screen (the view globals keep the last
//    map's camera, shared-texture-and-compositor.md 9.4), so the camera says nothing about the load. The copy waits for
//    tap_delay_frames Presents with the camera OK and tap_scene_frames frames with the scene-clear list, which the load
//    screen does not have: in practice it starts after the player's click.
//  * The first load in a process: the camera has never been OK on a valid grid before, and it is not OK when the new
//    grid becomes valid (the front end has no map view: views 0); it comes up only when the map view is set up, at
//    StartGPUInitWork (load stage 8, which calls UpdateViews), long after CreateTerrain, with only IAmReady and the wait
//    for the player's click after it. So on such a load the copy starts after tap_first_delay_frames Presents with the
//    camera OK, under the load screen. The composite cannot see the load screen's button or the DLL's first Update (only
//    the proxy sees Update); the camera coming up on the new grid is the signal it has.
// Anything else (the camera already OK when the grid became valid, a map shown earlier in this process) takes the
// reload rule. tap_first_load=0 gives the reload rule on every load (as before 0408). The water tap follows the same
// decision (WaterTapEnv::firstLoadOpen).
enum { TAPG_UNDECIDED = 0, TAPG_CAM_AFTER_GRID = 1, TAPG_CAM_BEFORE_GRID = 2 };
static const char* kTapGateOrder[] = { "undecided", "the camera came up after the grid", "the camera was already OK when the grid became valid" };
static uint32_t g_tapGateSeq;            // the grid seq the counters below belong to
static bool g_tapGateMapBefore;          // an earlier grid seq had the camera OK on its valid grid: every later load is a reload
static bool g_tapGateRunMap;             // the current grid seq has had the camera OK on its valid grid
static int g_tapGateOrder;               // TAPG_*: the camera's state at the first Present that saw the current grid valid
static int g_tapGateLate;                // Presents of the current grid with the grid valid and the camera not OK yet
static bool g_tapGateFirst;              // the current grid takes the first-load rule (decided when the camera came up)
static int g_tapGateOkRun;               // Presents in a row with the camera OK on the current valid grid
static int g_tapGateSceneRun;            // of them, frames with the scene-clear list

// Render thread, every Present (also with tap=0: the water tap reads the decision), after the camera was published.
static void TapGateUpdate()
{
	const VPShellCamera& b = g_camBlk;
	const bool camOk = g_camLastStatus == VPCAM_OK;
	if (b.gridSeq != g_tapGateSeq)
	{
		if (g_tapGateRunMap) g_tapGateMapBefore = true;
		g_tapGateSeq = b.gridSeq; g_tapGateRunMap = false; g_tapGateOrder = TAPG_UNDECIDED; g_tapGateLate = 0;
		g_tapGateFirst = false; g_tapGateOkRun = 0; g_tapGateSceneRun = 0;
	}
	if (b.gridValid != 1) { g_tapGateOkRun = 0; g_tapGateSceneRun = 0; return; }
	if (g_tapGateOrder == TAPG_UNDECIDED) g_tapGateOrder = camOk ? TAPG_CAM_BEFORE_GRID : TAPG_CAM_AFTER_GRID;
	if (!camOk)
	{
		if (!g_tapGateRunMap) g_tapGateLate++;
		g_tapGateOkRun = 0; g_tapGateSceneRun = 0;
		return;
	}
	if (!g_tapGateRunMap)
	{
		g_tapGateRunMap = true;
		g_tapGateFirst = g_cfg.tapFirstLoad && !g_tapGateMapBefore && g_tapGateOrder == TAPG_CAM_AFTER_GRID;
		if (g_tapGateFirst)
			Log("tap: load gate (grid seq %u): FIRST LOAD - no map before in this process and %s (%d Presents with the grid valid "
				"before): copies start after %d Presents with the camera OK, no scene-clear frames needed",
				b.gridSeq, kTapGateOrder[g_tapGateOrder], g_tapGateLate, g_cfg.tapFirstDelayFrames);
		else
			Log("tap: load gate (grid seq %u): RELOAD rule - %s: copies wait for %d Presents with the camera OK and %d scene-clear frames",
				b.gridSeq, !g_cfg.tapFirstLoad ? "tap_first_load=0" : g_tapGateMapBefore ? "a map was shown before in this process"
				: kTapGateOrder[g_tapGateOrder], g_cfg.tapDelayFrames, g_cfg.tapSceneFrames);
	}
	g_tapGateOkRun++;
	if (g_sceneThisFrame) g_tapGateSceneRun++;
}

// The first-load rule holds for the current grid and its Presents are done (the water tap starts on it too).
static bool TapGateFirstOpen()
{
	return g_tapGateFirst && g_tapGateSeq == g_camBlk.gridSeq && g_camLastStatus == VPCAM_OK && g_camBlk.gridValid == 1 &&
		g_tapGateOkRun >= g_cfg.tapFirstDelayFrames;
}

// Render thread, every Present of the target, after the camera block was published.
static void TapOnPresent()
{
	if (!g_hdr) return;
	if (g_camValidated > 0) TapGateUpdate();
	VPShellTap* t = VPSHELL_TAP(g_hdr);
	if (!g_cfg.tap)
	{
		if (!g_tapBusy && t->state != VPTAP_OFF)
		{
			VPShellTap v; memset(&v, 0, sizeof(v)); v.state = VPTAP_OFF; v.exePid = GetCurrentProcessId();
			TapPublish(t, v);
			g_tapStartedAny = false;
			Log("tap: OFF (tap=0)");
		}
		return;
	}
	if (g_camValidated <= 0) return;
	// tap_test_load: a change simulates a new map load (test only)
	if (g_cfg.tapTest && g_cfg.tapTestLoad != g_tapTestLoadSeen) { g_tapTestLoadSeen = g_cfg.tapTestLoad; g_gridSeq++; g_camBlk.gridSeq = g_gridSeq; Log("tap: tap_test_load=%d: simulated new load, grid seq %u", g_cfg.tapTestLoad, g_gridSeq); }
	const VPShellCamera& b = g_camBlk;
	bool ok = g_camLastStatus == VPCAM_OK && b.gridValid == 1;
	if (!ok || b.gridSeq != g_tapGateSeq) return;   // (a tap_test_load bump this Present: the gate sees it next Present)
	// the load gate above: a first load needs tap_first_delay_frames Presents with the camera OK; a reload the camera OK
	// for tap_delay_frames Presents and tap_scene_frames frames with the scene clear (the camera block stays OK through
	// the load screen, which has no scene clear: shared-texture-and-compositor.md 9.4)
	if (g_tapGateFirst ? !TapGateFirstOpen() : (g_tapGateOkRun < g_cfg.tapDelayFrames || g_tapGateSceneRun < g_cfg.tapSceneFrames)) return;
	if (g_tapBusy) return;
	const uint32_t req = t->requestSeq;
	if (g_tapStartedAny && b.gridSeq == g_tapStartedSeq && req == g_tapStartedReq && t->state != VPTAP_NONE) return;
	TapJob& j = g_tapJob;
	memset(&j, 0, sizeof(j));
	j.loadSeq = b.gridSeq; j.reqSeq = req; j.copySeq = (uint32_t)InterlockedIncrement(&g_tapCopies);
	j.test = g_cfg.tapTest; j.byteLayer = g_cfg.tapByteLayer; j.ackTimeoutMs = g_cfg.tapAckTimeoutMs;
	j.chunkBytes = (uint32_t)g_cfg.tapChunkKb * 1024u;
	j.camSamplesX = b.samplesX; j.camSamplesY = b.samplesY; j.camWrapX = b.wrapX; memcpy(j.camH2w, b.h2w, 64);
	wcsncpy_s(j.mainSection, g_cfg.section, _TRUNCATE);
	g_tapStartedAny = true; g_tapStartedSeq = b.gridSeq; g_tapStartedReq = req;
	InterlockedExchange(&g_tapBusy, 1);
	HANDLE th = CreateThread(nullptr, 0, TapWorker, nullptr, 0, nullptr);
	if (!th) { InterlockedExchange(&g_tapBusy, 0); Log("tap: CreateThread failed (error %u)", GetLastError()); return; }
	CloseHandle(th);
	Log("tap: copy %u of load %u started (%s%s, camera OK for %d Presents, %d scene-clear frames, %s, request %u, grid %ux%u samples)", j.copySeq, j.loadSeq,
		j.test ? "tap_test=" : "the EXE's field", j.test ? kTapTestNames[j.test] : "", g_tapGateOkRun, g_tapGateSceneRun,
		g_tapGateFirst ? "first-load gate" : "reload gate", req, b.samplesX, b.samplesY);
}

static void SafeTapOnPresent()
{
	if (g_tapDead) return;
	__try { TapOnPresent(); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); g_tapDead = true; Log("tap: EXCEPTION 0x%08X on the render thread - tap switched off for this process", GetExceptionCode()); }
}

// ===========================================================================
// Drawing with full state save/restore
// ===========================================================================
struct SavedState
{
	ID3D11InputLayout* il; D3D11_PRIMITIVE_TOPOLOGY topo;
	ID3D11VertexShader* vs; ID3D11ClassInstance* vsCI[256]; UINT vsN;
	ID3D11PixelShader* ps; ID3D11ClassInstance* psCI[256]; UINT psN;
	ID3D11GeometryShader* gs; ID3D11ClassInstance* gsCI[256]; UINT gsN;
	ID3D11HullShader* hs; ID3D11ClassInstance* hsCI[256]; UINT hsN;
	ID3D11DomainShader* ds; ID3D11ClassInstance* dsCI[256]; UINT dsN;
	ID3D11ShaderResourceView* psSrv0; ID3D11SamplerState* psSmp0;
	ID3D11ShaderResourceView* psSrv1; ID3D11SamplerState* psSmp1;   // issue 3304: the depth pass uses t1 / s1
	ID3D11RasterizerState* rs; UINT nVp; D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT nSc; D3D11_RECT sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	ID3D11BlendState* bs; FLOAT bf[4]; UINT sampleMask;
	ID3D11DepthStencilState* dss; UINT stencilRef;
	ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; ID3D11DepthStencilView* dsv;
	ID3D11Predicate* pred; BOOL predVal;
};
static SavedState g_saved;                       // immediate context (render thread)
static __declspec(thread) SavedState* t_saved;   // deferred contexts (recording threads), allocated on first use

static void ReleaseCI(ID3D11ClassInstance** a, UINT n) { for (UINT i = 0; i < n; ++i) SafeRelease(a[i]); }

static void SaveState(ID3D11DeviceContext* c, SavedState& s)
{
	memset(&s, 0, sizeof(s));
	c->IAGetInputLayout(&s.il);
	c->IAGetPrimitiveTopology(&s.topo);
	s.vsN = 256; c->VSGetShader(&s.vs, s.vsCI, &s.vsN);
	s.psN = 256; c->PSGetShader(&s.ps, s.psCI, &s.psN);
	s.gsN = 256; c->GSGetShader(&s.gs, s.gsCI, &s.gsN);
	s.hsN = 256; c->HSGetShader(&s.hs, s.hsCI, &s.hsN);
	s.dsN = 256; c->DSGetShader(&s.ds, s.dsCI, &s.dsN);
	c->PSGetShaderResources(0, 1, &s.psSrv0);
	c->PSGetSamplers(0, 1, &s.psSmp0);
	c->PSGetShaderResources(1, 1, &s.psSrv1);
	c->PSGetSamplers(1, 1, &s.psSmp1);
	c->RSGetState(&s.rs);
	s.nVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&s.nVp, s.vp);
	s.nSc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&s.nSc, s.sc);
	c->OMGetBlendState(&s.bs, s.bf, &s.sampleMask);
	c->OMGetDepthStencilState(&s.dss, &s.stencilRef);
	c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s.rtv, &s.dsv);
	c->GetPredication(&s.pred, &s.predVal);
}

static void RestoreState(ID3D11DeviceContext* c, SavedState& s)
{
	c->IASetInputLayout(s.il);
	c->IASetPrimitiveTopology(s.topo);
	c->VSSetShader(s.vs, s.vsCI, s.vsN);
	c->PSSetShader(s.ps, s.psCI, s.psN);
	c->GSSetShader(s.gs, s.gsCI, s.gsN);
	c->HSSetShader(s.hs, s.hsCI, s.hsN);
	c->DSSetShader(s.ds, s.dsCI, s.dsN);
	c->PSSetShaderResources(0, 1, &s.psSrv0);
	c->PSSetSamplers(0, 1, &s.psSmp0);
	c->PSSetShaderResources(1, 1, &s.psSrv1);
	c->PSSetSamplers(1, 1, &s.psSmp1);
	c->RSSetState(s.rs);
	c->RSSetViewports(s.nVp, s.vp);
	c->RSSetScissorRects(s.nSc, s.sc);
	c->OMSetBlendState(s.bs, s.bf, s.sampleMask);
	c->OMSetDepthStencilState(s.dss, s.stencilRef);
	UINT nRtv = 0;
	for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (s.rtv[i]) nRtv = i + 1;
	c->OMSetRenderTargets(nRtv, s.rtv, s.dsv);
	c->SetPredication(s.pred, s.predVal);

	SafeRelease(s.il);
	SafeRelease(s.vs); ReleaseCI(s.vsCI, s.vsN);
	SafeRelease(s.ps); ReleaseCI(s.psCI, s.psN);
	SafeRelease(s.gs); ReleaseCI(s.gsCI, s.gsN);
	SafeRelease(s.hs); ReleaseCI(s.hsCI, s.hsN);
	SafeRelease(s.ds); ReleaseCI(s.dsCI, s.dsN);
	SafeRelease(s.psSrv0); SafeRelease(s.psSmp0);
	SafeRelease(s.psSrv1); SafeRelease(s.psSmp1);
	SafeRelease(s.rs); SafeRelease(s.bs); SafeRelease(s.dss);
	for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) SafeRelease(s.rtv[i]);
	SafeRelease(s.dsv);
	SafeRelease(s.pred);
}

static bool RtvSize(ID3D11RenderTargetView* rtv, UINT& w, UINT& h, DXGI_FORMAT& viewFmt, UINT& samples)
{
	D3D11_RENDER_TARGET_VIEW_DESC rd; rtv->GetDesc(&rd);
	viewFmt = rd.Format;
	ID3D11Resource* r = nullptr; rtv->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	bool ok = r && SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t));
	if (ok)
	{
		D3D11_TEXTURE2D_DESC td; t->GetDesc(&td);
		UINT mip = (rd.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D) ? rd.Texture2D.MipSlice :
			(rd.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY) ? rd.Texture2DArray.MipSlice : 0;
		w = std::max(1u, td.Width >> mip); h = std::max(1u, td.Height >> mip);
		samples = td.SampleDesc.Count;
		t->Release();
	}
	SafeRelease(r);
	return ok;
}

// Draw the local copy into rtv on context c (immediate or deferred). Caller holds g_resLock (shared or exclusive
// owner thread) so the objects cannot go away.
static void DrawQuadOn(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, SavedState& saved)
{
	UINT w, h, samples; DXGI_FORMAT vf;
	if (!RtvSize(rtv, w, h, vf, samples)) return;
	ID3D11ShaderResourceView* srv = (IsSrgb(vf) && g_srvSrgb) ? g_srvSrgb : g_srvUnorm;
	if (!srv) return;
	SaveState(c, saved);
	D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
	static const FLOAT kBf[4] = { 0, 0, 0, 0 };
	c->IASetInputLayout(nullptr);
	c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	c->VSSetShader(g_vs, nullptr, 0);
	c->PSSetShader(g_ps, nullptr, 0);
	c->GSSetShader(nullptr, nullptr, 0);
	c->HSSetShader(nullptr, nullptr, 0);
	c->DSSetShader(nullptr, nullptr, 0);
	c->PSSetShaderResources(0, 1, &srv);
	c->PSSetSamplers(0, 1, &g_smp);
	c->RSSetState(g_rs);
	c->RSSetViewports(1, &vp);
	c->OMSetBlendState(g_cfg.alphaBlend ? g_bsAlpha : g_bsOpaque, kBf, 0xFFFFFFFF);
	c->OMSetDepthStencilState(g_dssOff, 0);
	c->OMSetRenderTargets(1, &rtv, nullptr);
	c->SetPredication(nullptr, FALSE);
	c->Draw(3, 0);
	RestoreState(c, saved);
}

static bool IsBackBuffer(ID3D11RenderTargetView* rtv)
{
	ID3D11Resource* bb = g_bbRes;
	if (!rtv || !bb) return false;
	ID3D11Resource* r = nullptr;
	rtv->GetResource(&r);
	bool is = (r == bb);
	SafeRelease(r);
	return is;
}

// ---- issue 3304: the insertion on the source path ----
static ID3D11DepthStencilView* g_insDsv;     // the DSV the scene-clear list cleared (tag_rule=after), set around one insertion
static ID3D11DepthStencilView* g_dsvChecked; static UINT g_dsvCheckedW, g_dsvCheckedH; static bool g_dsvOk;
static int g_dsvRefusedLogged;

// The DSV must be a 2D view of the target's size and sample count (ticket 0804: a TEXTURE2DMS view of the target's
// sample count too, as the EXE makes with MSAASamples > 1: the depth pass writes one SV_Depth per pixel, which D3D11
// writes to every covered sample) and writable; checked once per view and target size.
static bool LkDsvUsable(ID3D11DepthStencilView* dsv, ID3D11RenderTargetView* rtv)
{
	UINT w = 0, h = 0, samples = 0; DXGI_FORMAT vf;
	if (!RtvSize(rtv, w, h, vf, samples)) return false;
	if (dsv == g_dsvChecked && w == g_dsvCheckedW && h == g_dsvCheckedH) return g_dsvOk;
	char why[128] = "";
	D3D11_DEPTH_STENCIL_VIEW_DESC d; dsv->GetDesc(&d);
	ID3D11Resource* r = nullptr; dsv->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	D3D11_TEXTURE2D_DESC td = {};
	if (r && SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) { t->GetDesc(&td); t->Release(); }
	SafeRelease(r);
	const bool ms = d.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMS;
	if (!td.Width) strcpy_s(why, "not a 2D texture");
	else if (!ms && (d.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || d.Texture2D.MipSlice != 0)) _snprintf_s(why, _TRUNCATE, "view dimension %d mip %u", d.ViewDimension, d.Texture2D.MipSlice);
	else if (d.Flags & D3D11_DSV_READ_ONLY_DEPTH) strcpy_s(why, "a read-only depth view");
	else if (td.SampleDesc.Count != samples) _snprintf_s(why, _TRUNCATE, "of another sample count (depth %u, target %u)", td.SampleDesc.Count, samples);
	else if (ms != (td.SampleDesc.Count > 1)) _snprintf_s(why, _TRUNCATE, "view dimension %d on a %u-sample texture", d.ViewDimension, td.SampleDesc.Count);
	else if (td.Width != w || td.Height != h) _snprintf_s(why, _TRUNCATE, "%ux%u, the target is %ux%u", td.Width, td.Height, w, h);
	g_dsvChecked = dsv; g_dsvCheckedW = w; g_dsvCheckedH = h; g_dsvOk = !why[0];
	if (g_dsvOk) Log("lockstep: depth target: the scene-clear DSV %p (format %d, %ux%u, %u samples) is written with the server's depth", dsv, d.Format, td.Width, td.Height, td.SampleDesc.Count);
	else if (g_dsvRefusedLogged++ < 5) Log("lockstep: depth NOT written: the scene-clear DSV %p is %s", dsv, why);
	return g_dsvOk;
}

// Draws frame f over rtv (colour, straight alpha or opaque as today) and, with dsv, its depth (SV_Depth, ALWAYS, write on,
// stencil off) in the same pass. Saves and restores every piece of state it touches.
static void DrawSourceOn(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, const vpframe::SourceFrame& f, ID3D11DepthStencilView* dsv, SavedState& saved)
{
	UINT w, h, samples; DXGI_FORMAT vf;
	if (!RtvSize(rtv, w, h, vf, samples) || !f.colour) return;
	// the version-3 local copy has an sRGB view too; a ring slot (B8G8R8A8_UNORM) is sampled as it is
	ID3D11ShaderResourceView* srv = (IsSrgb(vf) && f.colour == g_srvUnorm && g_srvSrgb) ? g_srvSrgb : f.colour;
	const bool depth = dsv && f.depth && g_psDepth && g_dssWrite && g_smpPoint;
	SaveState(c, saved);
	D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
	static const FLOAT kBf[4] = { 0, 0, 0, 0 };
	c->IASetInputLayout(nullptr);
	c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	c->VSSetShader(g_vs, nullptr, 0);
	c->PSSetShader(depth ? g_psDepth : g_ps, nullptr, 0);
	c->GSSetShader(nullptr, nullptr, 0);
	c->HSSetShader(nullptr, nullptr, 0);
	c->DSSetShader(nullptr, nullptr, 0);
	c->PSSetShaderResources(0, 1, &srv);
	c->PSSetSamplers(0, 1, &g_smp);
	if (depth) { ID3D11ShaderResourceView* ds = f.depth; c->PSSetShaderResources(1, 1, &ds); c->PSSetSamplers(1, 1, &g_smpPoint); }
	c->RSSetState(g_rs);
	c->RSSetViewports(1, &vp);
	c->OMSetBlendState(g_cfg.alphaBlend ? g_bsAlpha : g_bsOpaque, kBf, 0xFFFFFFFF);
	c->OMSetDepthStencilState(depth ? g_dssWrite : g_dssOff, 0);
	c->OMSetRenderTargets(1, &rtv, depth ? dsv : nullptr);
	c->SetPredication(nullptr, FALSE);
	c->Draw(3, 0);
	RestoreState(c, saved);
}

// ---- ticket 0691 (D242): lockstep_mode=exact -------------------------------------------------------------------------
// The insertion waits for its own frame with no bound and no give-up. The wait ends only when the frame is ready, the
// server is dead (its process ended, another pid in the section, ring state FAILED / STOPPED, magic 0) or HUNG for
// lockstep_hung_ms: its heartbeat older than that (ring: VPFrameServer.heartbeatQpc; v3: the v3 header's; server.exe's
// V3Sink::tick writes both), or no new frame of any id for that long while the heartbeat ticks (younger than 1 s). A hung
// end: one LOUD line, the counter hungEnds, no wait until a frame is ready at the insertion again, and the proxy's
// failure route (D118/D240) through the sink the proxy sets (VPShell_SetHungSink), called by the control thread.
// No message pump in the wait (lead-0673's choice after test (d), 2026-11-23): the window procedure never runs inside a
// frame; a stall over 5 s shows Windows' "Not Responding" mark until the frame ends. A PeekMessageW(PM_NOREMOVE |
// PM_QS_POSTMESSAGE) every 500 ms was tried: it did not prevent the mark and it ran a cross-thread SendMessage's
// window procedure mid-frame (research/05-graphics-composition/renderer-in-server-design.md, 0691 section).
enum { LKX_READY = 0, LKX_DEAD = 1, LKX_HUNG_HB = 2, LKX_HUNG_NOPROG = 3 };
typedef void(__cdecl* VPShellHungSink)(const char* reason);
static PVOID volatile g_lkxSink;           // set by the proxy (any thread)
static volatile LONG g_lkxSinkState;       // 1 = the one call of this process is claimed
static volatile LONG g_lkxSinkPending;     // 1 = the control thread makes it
static char g_lkxReason[256];
static HANDLE g_lkxProc; static uint32_t g_lkxProcPid; static DWORD g_lkxProcErr;
static uint32_t g_lkxDeadLogged = ~0u;     // the server pid whose death was logged
static bool g_lkxHung;                     // after a hung end: no wait until a frame is ready at the insertion
static int64_t g_lkxHungEnds;              // cumulative, mirrored into the publish block (hungEnds)
extern "C" __declspec(dllexport) void __cdecl VPShell_SetHungSink(VPShellHungSink fn)
{
	InterlockedExchangePointer(&g_lkxSink, (PVOID)fn);
	Log("lockstep: hung sink %s by the proxy (ticket 0691)", fn ? "set" : "cleared");
}

// True when the server is dead (*why says how); hb = its heartbeat QPC (0 = none). pid0 = the pid at the wait's start.
static bool LkxServerDead(uint32_t pid0, int64_t& hb, const char*& why)
{
	hb = 0; why = nullptr;
	uint32_t pid;
	if (g_srcKind == HO_RING)
	{
		if (!g_fs) { why = "no frame section"; return true; }
		VPFrameServer* s = VPFRAME_SERVER(g_fs);
		if (s->magic != VPFRAME_MAGIC) { why = "frame section magic 0 (not started or stopped)"; return true; }
		if (s->state == VPFS_FAILED || s->state == VPFS_STOPPED) { why = s->state == VPFS_FAILED ? "server state FAILED" : "server state STOPPED"; return true; }
		pid = s->serverPid; hb = InterlockedCompareExchange64((volatile LONGLONG*)&s->heartbeatQpc, 0, 0);
	}
	else
	{
		if (!g_hdr || g_hdr->magic != VPSHELL_MAGIC) { why = "section magic 0 (not started or stopped)"; return true; }
		pid = g_hdr->serverPid; hb = InterlockedCompareExchange64(&g_hdr->heartbeatQpc, 0, 0);
	}
	if (!pid) { why = "no server pid"; return true; }
	if (pid0 && pid != pid0) { why = "another server pid in the section"; return true; }
	if (pid != g_lkxProcPid)
	{
		if (g_lkxProc) CloseHandle(g_lkxProc);
		g_lkxProc = OpenProcess(SYNCHRONIZE, FALSE, pid);
		g_lkxProcErr = g_lkxProc ? 0 : GetLastError();
		g_lkxProcPid = pid;
	}
	if (g_lkxProc && WaitForSingleObject(g_lkxProc, 0) == WAIT_OBJECT_0) { why = "the server process ended"; return true; }
	if (!g_lkxProc && g_lkxProcErr == ERROR_INVALID_PARAMETER) { why = "the server process does not exist"; return true; }
	return false;
}

template <class Pred> static int LkExactWait(Pred ready, HANDLE ev, HANDLE timer, double& stallMs, double& hbAgeMs)
{
	const int64_t f = g_freq.QuadPart, slice = f / 4000, checkEvery = f / 100;
	const int64_t hung = (int64_t)g_cfg.hungMs * f / 1000;
	const int64_t t0 = Qpc();
	int64_t lastCheck = 0, progQpc = t0, seen = g_src->newest();
	uint32_t pid0 = 0;
	for (bool first = true;; first = false)
	{
		if (ready()) return LKX_READY;
		int64_t now = Qpc();
		if (first || now - lastCheck >= checkEvery)   // the slow checks every 10 ms
		{
			lastCheck = now;
			int64_t hb; const char* why;
			if (LkxServerDead(pid0, hb, why))
			{
				if (g_lkxDeadLogged != g_lkxProcPid)
					Log("lockstep: exact: the server is dead (%s; pid %u): no wait while it is (the proxy's route for a dead server applies)", why, g_lkxProcPid);
				g_lkxDeadLogged = g_lkxProcPid;
				return LKX_DEAD;
			}
			if (first) pid0 = g_lkxProcPid;
			const int64_t n = g_src->newest();
			if (n != seen) { seen = n; progQpc = now; }
			stallMs = QpcToMs(now - progQpc);
			hbAgeMs = hb ? QpcToMs(now - hb) : -1.0;
			if (hb && now - hb > hung) return LKX_HUNG_HB;
			if (now - progQpc > hung && (!hb || now - hb < f)) return LKX_HUNG_NOPROG;
		}
		LARGE_INTEGER due; due.QuadPart = -std::max<int64_t>(1, slice * 10000000 / f);
		HANDLE h[2]; DWORD nh = 0;
		if (ev) h[nh++] = ev;
		if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) h[nh++] = timer;
		if (nh) WaitForMultipleObjects(nh, h, FALSE, 2); else Sleep(0);
	}
}

// The one call of the proxy's sink per process, made by the control thread (LkxSinkPoll)
static void LkxSinkAsk(const char* reason)
{
	if (InterlockedCompareExchange(&g_lkxSinkState, 1, 0) == 0)
	{
		strcpy_s(g_lkxReason, reason);
		InterlockedExchange(&g_lkxSinkPending, 1);
	}
}

static void LkxHungEnd(int r, double stallMs, double hbAgeMs, int64_t want)
{
	g_lkxHung = true;
	g_lkxHungEnds++;
	g_lkI.hungEnds++;
	char reason[256];
	if (r == LKX_HUNG_HB)
		_snprintf_s(reason, _TRUNCATE, "server drew no frame for %.1f s (heartbeat age %.1f s)", stallMs / 1000.0, hbAgeMs / 1000.0);
	else
		_snprintf_s(reason, _TRUNCATE, "no frame delivered for %.1f s, heartbeat alive (heartbeat age %.1f s)", stallMs / 1000.0, hbAgeMs / 1000.0);
	Log("LOUD: lockstep: the server is HUNG: %s; frame %lld not drawn, the wait ended (lockstep_hung_ms=%d, hung end %lld); no wait until a frame "
		"is ready at the insertion; the proxy's failure route (D118/D240) is asked for", reason, want, g_cfg.hungMs, g_lkxHungEnds);
	LkxSinkAsk(reason);
}

// Control thread: the one call of the proxy's sink (never on the EXE's main thread in the middle of a frame).
static void LkxSinkPoll()
{
	if (!InterlockedExchange(&g_lkxSinkPending, 0)) return;
	VPShellHungSink fn = (VPShellHungSink)InterlockedCompareExchangePointer(&g_lkxSink, nullptr, nullptr);
	if (fn)
	{
		Log("lockstep: hung: the proxy is told: %s", g_lkxReason);
		fn(g_lkxReason);
		Log("lockstep: hung: the proxy's failure route returned (the game goes on: a stock session)");
	}
	else if (g_cfg.hungSinkTest) Log("hung_sink_test: ServerHung(\"%s\"): the call the proxy would get (no proxy sink set)", g_lkxReason);
	else Log("lockstep: hung: no proxy sink set (the composite was not loaded by the proxy): nobody told");
}

// ---- ticket 0691 (MC #11169): frames the server HOLDS for EXE inputs (vp_frame.h VPFrameHoldReason) ----------------------
// A live load deadlocked: the server held the first map frame for inputs only later EXE frames bring, while the EXE's frame
// waited for it (research/05-graphics-composition/renderer-in-server-design.md 3.2). Such a frame is not waited for and
// shows no server world (as at a load; never an older world): held_skip, each LOUD. A run of held frames longer than
// lockstep_hung_ms (the EXE presenting meanwhile) is a fault: one LOUD line, held_faults, the proxy's failure route.
static int64_t g_lkCntHeld;                // cumulative held skips, mirrored into the publish block (heldSkips)
static int64_t g_lkHeldRunQpc;             // the first held skip of the current run (0: no run); a lockstep frame not held ends it
static int64_t g_lkHeldRunFrames, g_lkHeldFaults, g_lkHeldLogQpc;
static bool g_lkHeldRunFault;

// The hold reasons of frame `want` (0: not held). Main thread.
static uint32_t LkHeld(int64_t want)
{
	if (!g_fs || want <= 0) return 0;
	VPFrameServer* s = VPFRAME_SERVER(g_fs);
	if (InterlockedCompareExchange64(&s->heldFrameId, 0, 0) != want) return 0;
	const uint32_t r = (uint32_t)InterlockedCompareExchange((volatile LONG*)&s->holdReasons, 0, 0);
	return InterlockedCompareExchange64(&s->heldFrameId, 0, 0) == want ? r : 0;
}

static void LkHoldText(uint32_t r, char* out, size_t n)
{
	static const char* kNames[] = { "fog-no-table", "fog-table-size", "fog-no-revealed", "fog-no-pair", "fog-install", "no-field" };
	_snprintf_s(out, n, _TRUNCATE, "0x%08X", r);
	for (int i = 0; i < (int)_countof(kNames); ++i)
		if (r & (1u << i)) { strcat_s(out, n, " "); strcat_s(out, n, kNames[i]); }
	if (r & VPHOLD_TEST) strcat_s(out, n, " test");
}

static void LkHeldSkip(int64_t want, uint32_t reasons, double waitedMs)
{
	const int64_t now = Qpc();
	if (!g_lkHeldRunQpc) { g_lkHeldRunQpc = now; g_lkHeldRunFrames = 0; g_lkHeldRunFault = false; g_lkHeldLogQpc = 0; }
	g_lkHeldRunFrames++; g_lkCntHeld++; g_lkI.heldSkips++;
	const double runMs = QpcToMs(now - g_lkHeldRunQpc);
	char why[160]; LkHoldText(reasons, why, sizeof(why));
	// each held skip LOUD; after the run's fault once per second (the proxy ends the game there)
	if (!g_lkHeldRunFault || QpcToMs(now - g_lkHeldLogQpc) >= 1000.0)
	{
		g_lkHeldLogQpc = now;
		Log("LOUD: lockstep: held_skip: the server holds frame %lld for EXE inputs (reasons %s): not waited for (%.2f ms until the hold "
			"was seen), no server world in this frame (held run: %lld frames, %.0f ms)", want, why, waitedMs, g_lkHeldRunFrames, runMs);
	}
	if (!g_lkHeldRunFault && runMs > (double)g_cfg.hungMs)
	{
		g_lkHeldRunFault = true; g_lkHeldFaults++; g_lkI.heldFaults++;
		char reason[256];
		_snprintf_s(reason, _TRUNCATE, "server held frame %lld for EXE inputs for %.1f s while the EXE presented %lld frames (hold reasons %s)",
			want, runMs / 1000.0, g_lkHeldRunFrames, why);
		Log("LOUD: lockstep: the server's HOLD is STUCK: %s; lockstep_hung_ms=%d, held fault %lld; the proxy's failure route (D118/D240) "
			"is asked for", reason, g_cfg.hungMs, g_lkHeldFaults);
		LkxSinkAsk(reason);
	}
}

// A lockstep frame the server did not hold: the run of held frames (if any) ended
static void LkHeldRunEnd(int64_t want)
{
	if (!g_lkHeldRunQpc) return;
	Log("lockstep: held run ended at frame %lld: %lld frames held, %.0f ms%s", want, g_lkHeldRunFrames, QpcToMs(Qpc() - g_lkHeldRunQpc),
		g_lkHeldRunFault ? " (after its fault)" : "");
	g_lkHeldRunQpc = 0;
}

// The insertion with lockstep / a FrameSource (SourcePath()). Main thread; under SafeComposite's SEH guard.
static void CompositeSource(ID3D11RenderTargetView* rtv)
{
	const int64_t t0 = Qpc();
	LkRow& row = g_lkCur;
	g_lkCurIns = true;
	row.insQpc = t0;
	row.kind = LK_NONE;
	if (!EnsureResources()) return;
	if (!LkEnsureSource()) { row.kind = LK_NONE; g_lkI.none++; g_noDraw++; return; }
	g_src->poll(t0);
	const int64_t want = g_lkUvF;
	bool waitNow = want && g_lkUvWait;
	int64_t newest = g_src->newest();
	if (want && g_lkGaveUp)
	{
		if (newest >= want)
		{
			Log("lockstep: recovered: frame %lld was ready at the insertion without a wait - waiting again", want);
			g_lkGaveUp = false; g_lkMissRow = 0; g_lkI.recoveries++;
		}
		else { row.waitOff = 1; g_lkI.waitOff++; g_lkCntWaitOff++; waitNow = false; }
	}
	if (want && g_lkxHung && waitNow)   // ticket 0691: after a hung end, as the give-up above
	{
		if (newest >= want) { Log("lockstep: exact: recovered: frame %lld was ready at the insertion without a wait - waiting again", want); g_lkxHung = false; g_lkI.recoveries++; }
		else { row.waitOff = 1; g_lkI.waitOff++; g_lkCntWaitOff++; waitNow = false; }
	}
	// ticket 0691 (MC #11169): a frame the server holds for EXE inputs is not waited for (checked again in the wait: the
	// server reports the hold once its render thread has read this frame's camera)
	uint32_t held = want && g_lkUvWait && newest < want ? LkHeld(want) : 0;
	if (held) waitNow = false;
	if (waitNow && newest < want)
	{
		// One wait loop for every source (LkBoundedWait: <base>.Frm + 0.25 ms high-resolution timer slices + a spin over
		// the last 0.5 ms), the source's newest() as the condition; the ring's poll() gives back slots whose use by our
		// GPU ended meanwhile (the server may need one to draw this frame), as its own waitFor does. Not the source's
		// waitFor: RingSource::waitFor ended up to 1.1 ms after its deadline under ASan with CPU sync (5 of 74 waits,
		// 2026-11-13 08:08, design 3.1), over the contract's 0.5 ms; this loop stayed within 0.38 ms in 1,447 waits.
		row.wait0 = Qpc();
		if (!g_lkTimer) g_lkTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!g_lkTimer) g_lkTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		if (!g_lkFrm && g_fs) g_lkFrm = vpframe::ring::OpenEvent_(g_cfg.section, VPFRAME_FRM_EVENT_SUFFIX);
		vpframe::FrameSource* src = g_src;
		const bool ring = g_srcKind == HO_RING;
		// The ring's poll() calls GetData on the EXE's immediate context: at most every 0.25 ms and never in the last
		// 0.5 ms before the deadline (with it in every spin the ring cpu arm ended 14 of 183 waits up to 49 ms late on a
		// machine loaded by two games, 2026-11-13 08:50 [L stand-in; that GetData is the cause is inferred])
		const int64_t pollEvery = g_freq.QuadPart / 4000, quiet = g_lkUvDeadline - g_freq.QuadPart / 2000;
		int64_t lastPoll = 0;
		auto ready = [&]() {
			if (ring)
			{
				int64_t now = Qpc();
				if (now < quiet && now - lastPoll >= pollEvery) { src->poll(now); lastPoll = now; }
			}
			if (src->newest() >= want) return true;
			return (held = LkHeld(want)) != 0;   // ticket 0691
		};
		if (g_cfg.lockstepExact)   // ticket 0691
		{
			double stallMs = 0, hbAgeMs = -1;
			const int r = LkExactWait(ready, g_lkFrm, g_lkTimer, stallMs, hbAgeMs);
			if (r == LKX_DEAD) g_lkI.deadEnds++;
			else if (r != LKX_READY) LkxHungEnd(r, stallMs, hbAgeMs, want);
		}
		else LkBoundedWait(ready, g_lkUvDeadline, g_lkFrm, g_lkTimer);
		row.wait1 = Qpc();
		g_lkI.waits++;
		if (g_nLkWait < 16384) g_lkWaitUs[g_nLkWait++] = (float)(QpcToMs(row.wait1 - row.wait0) * 1000.0);
		lklate::Count(g_lkLate, row.wait0, row.wait1, g_lkUvDeadline, g_freq.QuadPart);
	}
	if (held && g_src->newest() < want)   // ticket 0691: no server world in a held frame (never an older one)
	{
		LkHeldSkip(want, held, row.wait0 ? QpcToMs(row.wait1 - row.wait0) : 0.0);
		row.kind = LK_HELD; row.takenF = 0; row.age = 0; row.unbaked = -1;
		g_noDraw++;
		if (g_fs) InterlockedExchange64(&VPFRAME_PUBLISH(g_fs)->heldSkips, g_lkCntHeld);
		Push(g_cc, g_nCc, QpcToMs(Qpc() - t0));
		return;
	}
	if (want && g_lkUvWait) LkHeldRunEnd(want);
	vpframe::SourceFrame f;
	const int64_t ask = want ? want : g_src->newest();
	vpframe::Take tk = g_src->acquire(g_ctx, ask, f);
	if (g_srcKind == HO_V3 && want && waitNow && tk != vpframe::Take::Exact && g_src->newest() >= want)
	{
		// Version 3: the depth block names our frame but the keyed mutex is still the server's (it is copying the next
		// camera's frame over the unconsumed one and waits for its GPU; seen 2026-11-13 08:24 with two cameras per frame
		// on a loaded machine: 28 of 2,521 frames took a 100-frame-old copy without waiting). Retry until the deadline.
		if (!row.wait0) row.wait0 = Qpc();
		const int64_t retryEnd = g_cfg.lockstepExact ? Qpc() + g_freq.QuadPart / 10 : g_lkUvDeadline;   // 0691: exact: 100 ms (a server gone mid-hand-over)
		while (tk != vpframe::Take::Exact && f.frameId < want && Qpc() < retryEnd) { YieldProcessor(); tk = g_src->acquire(g_ctx, ask, f); }
		row.wait1 = Qpc();
	}
	int kind;
	if (tk == vpframe::Take::None) kind = LK_NONE;
	else if (!want) kind = LK_NOPUB;
	else kind = f.frameId == want ? LK_EXACT : LK_OLDER;
	switch (kind) { case LK_EXACT: g_lkI.exact++; break; case LK_OLDER: g_lkI.older++; break; case LK_NONE: g_lkI.none++; break; default: g_lkI.nopub++; break; }
	if (want)
	{
		if (kind == LK_EXACT) { g_lkMissRow = 0; g_lkCntExact++; }
		else
		{
			// a miss counts toward the give-up only when the composite waited for the frame and it did not come (a frame
			// that was ready but not drawable, e.g. while the shared texture opens, is no sign of a slow server)
			g_lkCntMiss++;
			if (row.wait0 && !g_cfg.lockstepExact && !g_lkGaveUp && ++g_lkMissRow >= g_cfg.giveup)   // 0691: bounded only
			{
				g_lkGaveUp = true;
				g_lkI.giveups++;
				Log("lockstep: %d misses in a row (frame %lld wanted, %lld taken): no wait until a frame is ready at the insertion without one "
					"(the frames meanwhile count as wait_off)", g_lkMissRow, want, f.frameId);
			}
		}
	}
	const bool hide = g_srcKind == HO_V3 && g_stale && g_cfg.hideWhenStale;
	if (tk != vpframe::Take::None)
	{
		if (!hide)
		{
			ID3D11DepthStencilView* dsv = nullptr;
			if (g_cfg.depthWrite)
			{
				if (!f.depth) g_lkI.depthNoSrc++;
				else if (!g_insDsv) g_lkI.depthNoDsv++;
				else if (!LkDsvUsable(g_insDsv, rtv)) g_lkI.depthRefused++;
				else dsv = g_insDsv;
			}
			DrawSourceOn(g_ctx, rtv, f, dsv, g_saved);
			row.depth = dsv && g_psDepth ? 1 : 0;
			if (row.depth) g_lkI.depthN++;
			g_lkCntDrawn++;
		}
		else g_noDraw++;
		g_src->release(g_ctx, f);
	}
	else g_noDraw++;
	row.takenF = f.frameId; row.kind = kind; row.slot = f.slot;
	row.age = (want && tk != vpframe::Take::None) ? (int32_t)std::max<int64_t>(-1000000, std::min<int64_t>(1000000, want - f.frameId)) : 0;
	row.srvCamPub = f.camPublishQpc; row.srvWake = f.wakeQpc; row.srvSubmit = f.submitQpc; row.srvReady = f.readyQpc;
	row.srvCpuMs = f.serverCpuMs; row.srvGpuMs = f.serverGpuMs;
	row.unbaked = tk != vpframe::Take::None ? f.tilesUnbaked : -1;
	if (row.unbaked >= 0) { g_lkI.unbakedN++; if (row.unbaked > 0) { g_lkI.grayFrames++; g_lkI.grayTiles += row.unbaked; } }
	if (g_fs)
	{
		VPFramePublish* p = VPFRAME_PUBLISH(g_fs);
		InterlockedExchange64(&p->framesDrawn, g_lkCntDrawn);
		InterlockedExchange64(&p->framesExact, g_lkCntExact);
		InterlockedExchange64(&p->misses, g_lkCntMiss);
		InterlockedExchange64(&p->waitOffFrames, g_lkCntWaitOff);
		InterlockedExchange64(&p->hungEnds, g_lkxHungEnds);   // ticket 0691
	}
	Push(g_cc, g_nCc, QpcToMs(Qpc() - t0));
}

static void Composite(ID3D11RenderTargetView* rtv)
{
	g_inserted = true;
	if (g_disabled || !rtv) return;
	MaybeFault(1);
	if (SourcePath()) { CompositeSource(rtv); return; }   // issue 3304
	int64_t t0 = Qpc();
	if (EnsureResources())
	{
		GpuTimer* gt = &g_gt[g_gtNext];
		if (gt->pending || !gt->dj) gt = nullptr;
		if (gt) { g_ctx->Begin(gt->dj); g_ctx->End(gt->t0); }
		g_gtCur = gt;
		PollServer();
		AcquireAndCopy();
		if (g_haveContent && !(g_stale && g_cfg.hideWhenStale)) DrawQuadOn(g_ctx, rtv, g_saved);
		else g_noDraw++;
		g_gtCur = nullptr;
		if (gt) { g_ctx->End(gt->t1); g_ctx->End(gt->dj); gt->pending = true; gt->fresh = g_freshThisFrame; g_gtNext = (g_gtNext + 1) % (int)_countof(g_gt); }
	}
	Push(g_cc, g_nCc, QpcToMs(Qpc() - t0));
}

static void SafeComposite(ID3D11RenderTargetView* rtv)
{
	t_inComposite = 1;
	__try { Composite(rtv); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		InterlockedExchange(&g_disabled, 1);
		Log("EXCEPTION 0x%08X inside composite - composite disabled, hooks pass through", GetExceptionCode());
	}
	t_inComposite = 0;
}

// inlist: the immediate-context part (poll, acquire + copy into the local texture) before the list that samples it runs
static void AcquireOnly()
{
	int64_t t0 = Qpc();
	if (EnsureResources()) { PollServer(); AcquireAndCopy(); }
	Push(g_cc, g_nCc, QpcToMs(Qpc() - t0));
}

static void SafeAcquireOnly()
{
	t_inComposite = 1;
	__try { AcquireOnly(); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		InterlockedExchange(&g_disabled, 1);
		Log("EXCEPTION 0x%08X in the inlist acquire - composite disabled", GetExceptionCode());
	}
	t_inComposite = 0;
}

// ===========================================================================
// Back-buffer dump (debug: "shell_composite.shot" trigger file)
// ===========================================================================
// listK >= 0 (issue 3005, the listshot trigger): the dump is taken right after ExecuteCommandList #listK of the frame
static void DumpBackBuffer(IDXGISwapChain* sc, int listK = -1)
{
	ID3D11Texture2D* bb = nullptr;
	if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
	D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
	bool rgba = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
	bool bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || td.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
		td.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
	if (!rgba && !bgra) { Log("shot: unsupported back-buffer format %u", td.Format); bb->Release(); return; }
	ID3D11Texture2D* src = bb; src->AddRef();
	if (td.SampleDesc.Count > 1)
	{
		D3D11_TEXTURE2D_DESC rd = td; rd.SampleDesc.Count = 1; rd.SampleDesc.Quality = 0; rd.BindFlags = 0;
		ID3D11Texture2D* res = nullptr;
		if (SUCCEEDED(g_dev->CreateTexture2D(&rd, nullptr, &res)))
		{
			DXGI_FORMAT rf = td.Format;
			if (rf == DXGI_FORMAT_R8G8B8A8_TYPELESS) rf = DXGI_FORMAT_R8G8B8A8_UNORM;
			if (rf == DXGI_FORMAT_B8G8R8A8_TYPELESS) rf = DXGI_FORMAT_B8G8R8A8_UNORM;
			g_ctx->ResolveSubresource(res, 0, bb, 0, rf);
			src->Release(); src = res;
		}
	}
	D3D11_TEXTURE2D_DESC sd = td; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
	sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
	ID3D11Texture2D* st = nullptr;
	if (SUCCEEDED(g_dev->CreateTexture2D(&sd, nullptr, &st)))
	{
		g_ctx->CopyResource(st, src);
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			static int n = 0;
			wchar_t path[MAX_PATH];
			const wchar_t* dir = g_cfg.shotDir[0] ? g_cfg.shotDir : g_outDir;
			SYSTEMTIME t; GetLocalTime(&t);
			if (listK >= 0)
				_snwprintf_s(path, _TRUNCATE, L"%s%slistshot_k%03d_%02d%02d%02d_%d.bmp", dir,
					(dir[wcslen(dir) - 1] == L'\\') ? L"" : L"\\", listK, t.wHour, t.wMinute, t.wSecond, n++);
			else
				_snwprintf_s(path, _TRUNCATE, L"%s%sbackbuffer_%02d%02d%02d_%d.bmp", dir,
					(dir[wcslen(dir) - 1] == L'\\') ? L"" : L"\\", t.wHour, t.wMinute, t.wSecond, n++);
			FILE* f = nullptr;
			if (!_wfopen_s(&f, path, L"wb") && f)
			{
				BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
				DWORD bytes = td.Width * td.Height * 4;
				fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + bytes;
				ih.biSize = sizeof(ih); ih.biWidth = td.Width; ih.biHeight = -(LONG)td.Height; ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
				fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
				unsigned char* row = (unsigned char*)malloc(td.Width * 4);
				for (UINT y = 0; y < td.Height && row; ++y)
				{
					const unsigned char* s = (const unsigned char*)m.pData + (size_t)y * m.RowPitch;
					memcpy(row, s, td.Width * 4);
					if (rgba) for (UINT x = 0; x < td.Width; ++x) std::swap(row[x * 4], row[x * 4 + 2]);
					fwrite(row, 1, td.Width * 4, f);
				}
				free(row);
				fclose(f);
				Log("shot: wrote %ls (%ux%u fmt %u samples %u) frame %lld%s", path, td.Width, td.Height, td.Format, td.SampleDesc.Count, g_frameIndex, listK >= 0 ? " (listshot)" : "");
			}
			g_ctx->Unmap(st, 0);
		}
		st->Release();
	}
	src->Release();
	bb->Release();
}

// ===========================================================================
// Inline hooks (MinHook): targets, detours, kill switches
// ===========================================================================
enum HookId { H_PRESENT, H_PRESENT1, H_RESIZE, H_CREATESC, H_CREATESC_HWND, H_CREATEDEF, H_CLEAR_RTV, H_CLEAR_DSV, H_OMSET, H_FINISH, H_EXEC,
	// issue 4752: the unit-draw filter (unit_filter.cpp), all under the kill switch hook_units
	H_CREATE_VS, H_CREATE_PS, H_VSSET, H_PSSET, H_DRAW_II, H_DRAW_I, H_DRAW, H_DRAW_INST, H_DRAW_IIIND, H_DRAW_INSTIND, H_DRAW_AUTO,
	// ticket 4874: the model tap (unit_filter.cpp), under the kill switch hook_models
	H_CREATE_BUF, H_CREATE_TEX2D, H_MAP, H_UNMAP, H_UPDATESUB, H_COPYRES, H_COPYSUB, H_COUNT };
static const char* kHookNames[H_COUNT] = { "IDXGISwapChain::Present", "IDXGISwapChain1::Present1", "IDXGISwapChain::ResizeBuffers",
	"IDXGIFactory::CreateSwapChain", "IDXGIFactory2::CreateSwapChainForHwnd", "ID3D11Device::CreateDeferredContext[1-3]",
	"ID3D11DeviceContext::ClearRenderTargetView", "ID3D11DeviceContext::ClearDepthStencilView", "ID3D11DeviceContext::OMSetRenderTargets",
	"ID3D11DeviceContext::FinishCommandList", "ID3D11DeviceContext::ExecuteCommandList",
	"ID3D11Device::CreateVertexShader", "ID3D11Device::CreatePixelShader", "ID3D11DeviceContext::VSSetShader", "ID3D11DeviceContext::PSSetShader",
	"ID3D11DeviceContext::DrawIndexedInstanced", "ID3D11DeviceContext::DrawIndexed", "ID3D11DeviceContext::Draw", "ID3D11DeviceContext::DrawInstanced",
	"ID3D11DeviceContext::DrawIndexedInstancedIndirect", "ID3D11DeviceContext::DrawInstancedIndirect", "ID3D11DeviceContext::DrawAuto",
	"ID3D11Device::CreateBuffer", "ID3D11Device::CreateTexture2D", "ID3D11DeviceContext::Map", "ID3D11DeviceContext::Unmap",
	"ID3D11DeviceContext::UpdateSubresource", "ID3D11DeviceContext::CopyResource", "ID3D11DeviceContext::CopySubresourceRegion" };
static const int kHookSw[H_COUNT] = { SW_PRESENT, SW_PRESENT, SW_RESIZE, SW_CREATESC, SW_CREATESC, SW_CREATEDEF, SW_CLEAR, SW_CLEARDSV, SW_OMSET, SW_FINISH, SW_EXEC,
	SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS, SW_UNITS,
	SW_MODELS, SW_MODELS, SW_MODELS, SW_MODELS, SW_MODELS, SW_MODELS, SW_MODELS };
static const int kVar = 4;                     // distinct implementation functions per method (immediate, deferred, FL variants)
static void* volatile g_tr[H_COUNT][kVar];     // trampolines (the original functions)

struct Target { void* addr; int id; int var; unsigned char orig[8]; bool enabled; bool queued; char mod[64]; };
static Target g_tg[192];                       // issue 4752: 64 -> 128 (11 more methods, each on the immediate and deferred contexts); 4874: 160 (4 more)
static int g_nTg;
static CRITICAL_SECTION g_hookLock;
static bool g_swOn[SW_COUNT];
static bool g_mhOk;

typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present1)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_ResizeBuffers)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChainForHwnd)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateDeferred)(ID3D11Device*, UINT, ID3D11DeviceContext**);
typedef void(STDMETHODCALLTYPE* PFN_ClearRTV)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
typedef void(STDMETHODCALLTYPE* PFN_ClearDSV)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
typedef void(STDMETHODCALLTYPE* PFN_OMSetRT)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Finish)(ID3D11DeviceContext*, BOOL, ID3D11CommandList**);
typedef void(STDMETHODCALLTYPE* PFN_ExecCL)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);

// ---- detour bodies (one shared body per method; each variant has its own tiny detour that passes its trampoline) ----
static HRESULT Do_Present(PFN_Present tr, IDXGISwapChain* sc, UINT sync, UINT flags);
static HRESULT Do_Present1(PFN_Present1 tr, IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* pp);
static HRESULT Do_Resize(PFN_ResizeBuffers tr, IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl);
static HRESULT Do_CreateSc(PFN_CreateSwapChain tr, IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* d, IDXGISwapChain** out);
static HRESULT Do_CreateScHwnd(PFN_CreateSwapChainForHwnd tr, IDXGIFactory2* f, IUnknown* dev, HWND w, const DXGI_SWAP_CHAIN_DESC1* d, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* o, IDXGISwapChain1** out);
static HRESULT Do_CreateDeferred(PFN_CreateDeferred tr, ID3D11Device* dev, UINT flags, ID3D11DeviceContext** out);
static void Do_ClearRTV(PFN_ClearRTV tr, ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, const FLOAT col[4]);
static void Do_ClearDSV(PFN_ClearDSV tr, ID3D11DeviceContext* c, ID3D11DepthStencilView* dsv, UINT fl, FLOAT d, UINT8 s);
static void Do_OMSet(PFN_OMSetRT tr, ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv);
static HRESULT Do_Finish(PFN_Finish tr, ID3D11DeviceContext* c, BOOL restore, ID3D11CommandList** out);
static void Do_Exec(PFN_ExecCL tr, ID3D11DeviceContext* c, ID3D11CommandList* cl, BOOL restore);

template <int V> static HRESULT STDMETHODCALLTYPE D_Present(IDXGISwapChain* sc, UINT a, UINT b) { return Do_Present((PFN_Present)g_tr[H_PRESENT][V], sc, a, b); }
template <int V> static HRESULT STDMETHODCALLTYPE D_Present1(IDXGISwapChain1* sc, UINT a, UINT b, const DXGI_PRESENT_PARAMETERS* p) { return Do_Present1((PFN_Present1)g_tr[H_PRESENT1][V], sc, a, b, p); }
template <int V> static HRESULT STDMETHODCALLTYPE D_Resize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) { return Do_Resize((PFN_ResizeBuffers)g_tr[H_RESIZE][V], sc, n, w, h, f, fl); }
template <int V> static HRESULT STDMETHODCALLTYPE D_CreateSc(IDXGIFactory* f, IUnknown* d, DXGI_SWAP_CHAIN_DESC* s, IDXGISwapChain** o) { return Do_CreateSc((PFN_CreateSwapChain)g_tr[H_CREATESC][V], f, d, s, o); }
template <int V> static HRESULT STDMETHODCALLTYPE D_CreateScHwnd(IDXGIFactory2* f, IUnknown* d, HWND w, const DXGI_SWAP_CHAIN_DESC1* s, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* o, IDXGISwapChain1** out) { return Do_CreateScHwnd((PFN_CreateSwapChainForHwnd)g_tr[H_CREATESC_HWND][V], f, d, w, s, fs, o, out); }
template <int V> static HRESULT STDMETHODCALLTYPE D_CreateDeferred(ID3D11Device* d, UINT fl, ID3D11DeviceContext** o) { return Do_CreateDeferred((PFN_CreateDeferred)g_tr[H_CREATEDEF][V], d, fl, o); }
template <int V> static void STDMETHODCALLTYPE D_ClearRTV(ID3D11DeviceContext* c, ID3D11RenderTargetView* r, const FLOAT col[4]) { Do_ClearRTV((PFN_ClearRTV)g_tr[H_CLEAR_RTV][V], c, r, col); }
template <int V> static void STDMETHODCALLTYPE D_ClearDSV(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT fl, FLOAT d, UINT8 s) { Do_ClearDSV((PFN_ClearDSV)g_tr[H_CLEAR_DSV][V], c, v, fl, d, s); }
template <int V> static void STDMETHODCALLTYPE D_OMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* r, ID3D11DepthStencilView* d) { Do_OMSet((PFN_OMSetRT)g_tr[H_OMSET][V], c, n, r, d); }
template <int V> static HRESULT STDMETHODCALLTYPE D_Finish(ID3D11DeviceContext* c, BOOL r, ID3D11CommandList** o) { return Do_Finish((PFN_Finish)g_tr[H_FINISH][V], c, r, o); }
template <int V> static void STDMETHODCALLTYPE D_Exec(ID3D11DeviceContext* c, ID3D11CommandList* l, BOOL r) { Do_Exec((PFN_ExecCL)g_tr[H_EXEC][V], c, l, r); }
// issue 4752: the unit-draw filter's detours (bodies in unit_filter.cpp)
template <int V> static HRESULT STDMETHODCALLTYPE D_CreateVS(ID3D11Device* d, const void* b, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** o) { return UnitFilter_CreateVS((PFN_UfCreateVS)g_tr[H_CREATE_VS][V], d, b, n, l, o); }
template <int V> static HRESULT STDMETHODCALLTYPE D_CreatePS(ID3D11Device* d, const void* b, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** o) { return UnitFilter_CreatePS((PFN_UfCreatePS)g_tr[H_CREATE_PS][V], d, b, n, l, o); }
template <int V> static void STDMETHODCALLTYPE D_VSSet(ID3D11DeviceContext* c, ID3D11VertexShader* s, ID3D11ClassInstance* const* i, UINT n) { UnitFilter_VSSet((PFN_UfVSSet)g_tr[H_VSSET][V], c, s, i, n); }
template <int V> static void STDMETHODCALLTYPE D_PSSet(ID3D11DeviceContext* c, ID3D11PixelShader* s, ID3D11ClassInstance* const* i, UINT n) { UnitFilter_PSSet((PFN_UfPSSet)g_tr[H_PSSET][V], c, s, i, n); }
template <int V> static void STDMETHODCALLTYPE D_DrawII(ID3D11DeviceContext* c, UINT a, UINT b, UINT s, INT v, UINT si) { UnitFilter_DrawII((PFN_UfDrawII)g_tr[H_DRAW_II][V], c, a, b, s, v, si); }
template <int V> static void STDMETHODCALLTYPE D_DrawI(ID3D11DeviceContext* c, UINT a, UINT s, INT v) { UnitFilter_DrawI((PFN_UfDrawI)g_tr[H_DRAW_I][V], c, a, s, v); }
template <int V> static void STDMETHODCALLTYPE D_Draw(ID3D11DeviceContext* c, UINT a, UINT s) { UnitFilter_Draw((PFN_UfDraw)g_tr[H_DRAW][V], c, a, s); }
template <int V> static void STDMETHODCALLTYPE D_DrawInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT s, UINT si) { UnitFilter_DrawInst((PFN_UfDrawInst)g_tr[H_DRAW_INST][V], c, a, b, s, si); }
template <int V> static void STDMETHODCALLTYPE D_DrawIIInd(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) { UnitFilter_DrawIIIndirect((PFN_UfDrawIndirect)g_tr[H_DRAW_IIIND][V], c, b, o); }
template <int V> static void STDMETHODCALLTYPE D_DrawInstInd(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) { UnitFilter_DrawInstIndirect((PFN_UfDrawIndirect)g_tr[H_DRAW_INSTIND][V], c, b, o); }
template <int V> static void STDMETHODCALLTYPE D_DrawAuto(ID3D11DeviceContext* c) { UnitFilter_DrawAuto((PFN_UfDrawAuto)g_tr[H_DRAW_AUTO][V], c); }
// ticket 4874: the model tap's detours (bodies in unit_filter.cpp)
template <int V> static HRESULT STDMETHODCALLTYPE D_CreateBuf(ID3D11Device* d, const D3D11_BUFFER_DESC* bd, const D3D11_SUBRESOURCE_DATA* sd, ID3D11Buffer** o) { return UnitFilter_CreateBuffer((PFN_UfCreateBuffer)g_tr[H_CREATE_BUF][V], d, bd, sd, o); }
template <int V> static HRESULT STDMETHODCALLTYPE D_CreateTex2D(ID3D11Device* d, const D3D11_TEXTURE2D_DESC* td, const D3D11_SUBRESOURCE_DATA* sd, ID3D11Texture2D** o) { return UnitFilter_CreateTexture2D((PFN_UfCreateTex2D)g_tr[H_CREATE_TEX2D][V], d, td, sd, o); }
template <int V> static HRESULT STDMETHODCALLTYPE D_Map(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s, D3D11_MAP t, UINT f, D3D11_MAPPED_SUBRESOURCE* m) { return UnitFilter_Map((PFN_UfMap)g_tr[H_MAP][V], c, r, s, t, f, m); }
template <int V> static void STDMETHODCALLTYPE D_Unmap(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s) { UnitFilter_Unmap((PFN_UfUnmap)g_tr[H_UNMAP][V], c, r, s); }
template <int V> static void STDMETHODCALLTYPE D_UpdateSub(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s, const D3D11_BOX* b, const void* p, UINT rp, UINT dp) { UnitFilter_UpdateSubresource((PFN_UfUpdateSub)g_tr[H_UPDATESUB][V], c, r, s, b, p, rp, dp); }
template <int V> static void STDMETHODCALLTYPE D_CopyRes(ID3D11DeviceContext* c, ID3D11Resource* d, ID3D11Resource* s) { UnitFilter_CopyResource((PFN_UfCopyRes)g_tr[H_COPYRES][V], c, d, s); }
template <int V> static void STDMETHODCALLTYPE D_CopySub(ID3D11DeviceContext* c, ID3D11Resource* d, UINT ds, UINT x, UINT y, UINT z, ID3D11Resource* s, UINT ss, const D3D11_BOX* b) { UnitFilter_CopySubresourceRegion((PFN_UfCopySubRegion)g_tr[H_COPYSUB][V], c, d, ds, x, y, z, s, ss, b); }

#define DV(f) { (void*)&f<0>, (void*)&f<1>, (void*)&f<2>, (void*)&f<3> }
static void* const kDetours[H_COUNT][kVar] = {
	DV(D_Present), DV(D_Present1), DV(D_Resize), DV(D_CreateSc), DV(D_CreateScHwnd), DV(D_CreateDeferred),
	DV(D_ClearRTV), DV(D_ClearDSV), DV(D_OMSet), DV(D_Finish), DV(D_Exec),
	DV(D_CreateVS), DV(D_CreatePS), DV(D_VSSet), DV(D_PSSet), DV(D_DrawII), DV(D_DrawI), DV(D_Draw), DV(D_DrawInst), DV(D_DrawIIInd),
	DV(D_DrawInstInd), DV(D_DrawAuto), DV(D_CreateBuf), DV(D_CreateTex2D), DV(D_Map), DV(D_Unmap), DV(D_UpdateSub), DV(D_CopyRes), DV(D_CopySub) };

static bool ModuleOf(void* p, char* name, size_t n, HMODULE* mod)
{
	HMODULE m = nullptr;
	name[0] = 0;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m)) return false;
	char path[MAX_PATH]; GetModuleFileNameA(m, path, MAX_PATH);
	const char* s = strrchr(path, '\\');
	strcpy_s(name, n, s ? s + 1 : path);
	if (mod) *mod = m;
	return true;
}

// Hook one implementation function. Only d3d11.dll and dxgi.dll are allowed (never the EXE image, D4; never our own
// module; never another hooker's or a wrapper's module such as RenderDoc's, which is logged and left alone).
static bool HookTarget(int id, void* addr)
{
	if (!addr || !g_mhOk) return false;
	EnterCriticalSection(&g_hookLock);
	bool ok = false;
	do
	{
		bool known = false;
		for (int i = 0; i < g_nTg; ++i)
			if (g_tg[i].addr == addr)
			{
				known = true;
				if (g_tg[i].id != id) Log("hook note: %s target %p is already hooked as %s (shared implementation)", kHookNames[id], addr, kHookNames[g_tg[i].id]);
			}
		if (known) { ok = true; break; }
		char mod[64]; HMODULE m = nullptr;
		if (!ModuleOf(addr, mod, sizeof(mod), &m)) { Log("hook %s: target %p is in no module - not hooked", kHookNames[id], addr); break; }
		if (m == GetModuleHandleW(nullptr) || m == g_module || (_stricmp(mod, "d3d11.dll") && _stricmp(mod, "dxgi.dll")))
		{
			Log("hook %s: target %p is in %s, not in d3d11.dll/dxgi.dll - NOT hooked (a wrapper or another hooker owns this object)", kHookNames[id], addr, mod);
			break;
		}
		int var = 0;
		for (int i = 0; i < g_nTg; ++i) if (g_tg[i].id == id) var++;
		if (var >= kVar || g_nTg >= (int)_countof(g_tg)) { Log("hook %s: more than %d implementations - %p not hooked", kHookNames[id], kVar, addr); break; }
		Target& t = g_tg[g_nTg];
		t.addr = addr; t.id = id; t.var = var; t.enabled = false;
		memcpy(t.orig, addr, sizeof(t.orig));
		strcpy_s(t.mod, mod);
		MH_STATUS st = MH_CreateHook(addr, kDetours[id][var], (LPVOID*)&g_tr[id][var]);
		if (st != MH_OK) { Log("hook %s: MH_CreateHook(%p) failed: %s", kHookNames[id], addr, MH_StatusToString(st)); break; }
		g_nTg++;
		// enabled in one batch by CommitHooks (each MH_EnableHook freezes every thread: ~0.1 s on this machine)
		t.queued = g_swOn[kHookSw[id]] && MH_QueueEnableHook(addr) == MH_OK;
		Log("hook %s var %d target %p (%s+0x%X) orig %02X %02X %02X %02X %02X switch hook_%s %s", kHookNames[id], var, addr, mod,
			(unsigned)((char*)addr - (char*)m), t.orig[0], t.orig[1], t.orig[2], t.orig[3], t.orig[4], kSwNames[kHookSw[id]], t.queued ? "created, enable queued" : "created, disabled");
		ok = true;
	} while (0);
	LeaveCriticalSection(&g_hookLock);
	return ok;
}

// Enable the hooks HookTarget queued, with one thread freeze.
static void CommitHooks(const char* where)
{
	if (!g_mhOk) return;
	EnterCriticalSection(&g_hookLock);
	int n = 0;
	for (int i = 0; i < g_nTg; ++i) if (g_tg[i].queued) n++;
	if (n)
	{
		int64_t t0 = Qpc();
		MH_STATUS st = MH_ApplyQueued();
		for (int i = 0; i < g_nTg; ++i)
			if (g_tg[i].queued) { g_tg[i].queued = false; g_tg[i].enabled = st == MH_OK; }
		Log("hooks committed (%s): %d enabled in %.1f ms: %s", where, n, QpcToMs(Qpc() - t0), MH_StatusToString(st));
	}
	LeaveCriticalSection(&g_hookLock);
}

// Apply the kill switches (and mode=off) to every hook: one MH_ApplyQueued, i.e. one thread freeze per change.
static void ApplySwitches(const Config& c)
{
	if (!g_mhOk) return;
	EnterCriticalSection(&g_hookLock);
	bool on[SW_COUNT];
	for (int s = 0; s < SW_COUNT; ++s) on[s] = c.mode != MODE_OFF && c.hookSw[s];
	int changes = 0;
	for (int i = 0; i < g_nTg; ++i)
	{
		bool want = on[kHookSw[g_tg[i].id]];
		g_tg[i].queued = false;
		if (want != g_tg[i].enabled) { (want ? MH_QueueEnableHook : MH_QueueDisableHook)(g_tg[i].addr); changes++; }
	}
	MH_STATUS st = MH_OK;
	if (changes) st = MH_ApplyQueued();
	if (st == MH_OK)
		for (int i = 0; i < g_nTg; ++i)
		{
			bool want = on[kHookSw[g_tg[i].id]];
			if (want != g_tg[i].enabled)
			{
				g_tg[i].enabled = want;
				Log("hookstate %s var %d target %p %s", kHookNames[g_tg[i].id], g_tg[i].var, g_tg[i].addr, want ? "ENABLED" : "DISABLED");
			}
		}
	else Log("MH_ApplyQueued failed: %s", MH_StatusToString(st));
	memcpy(g_swOn, on, sizeof(on));
	char buf[256] = "";
	for (int s = 0; s < SW_COUNT; ++s) { char one[40]; _snprintf_s(one, _TRUNCATE, "%s%s=%d", s ? " " : "", kSwNames[s], (int)on[s]); strcat_s(buf, one); }
	Log("hooks: %s (%d targets, %d changed)", buf, g_nTg, changes);
	LeaveCriticalSection(&g_hookLock);
}

// ---- discovery: implementation addresses read from the vtables of real objects ----
static void DiscoverContext(ID3D11DeviceContext* c)
{
	void** vt = *(void***)c;
	HookTarget(H_CLEAR_RTV, vt[VT_Context_ClearRenderTargetView]);
	HookTarget(H_CLEAR_DSV, vt[VT_Context_ClearDepthStencilView]);
	HookTarget(H_OMSET, vt[VT_Context_OMSetRenderTargets]);
	HookTarget(H_FINISH, vt[VT_Context_FinishCommandList]);
	HookTarget(H_EXEC, vt[VT_Context_ExecuteCommandList]);
	// issue 4752: the unit-draw filter (every draw entry, S6; the shader binds it tracks)
	HookTarget(H_VSSET, vt[VT_Context_VSSetShader]);
	HookTarget(H_PSSET, vt[VT_Context_PSSetShader]);
	HookTarget(H_DRAW_II, vt[VT_Context_DrawIndexedInstanced]);
	HookTarget(H_DRAW_I, vt[VT_Context_DrawIndexed]);
	HookTarget(H_DRAW, vt[VT_Context_Draw]);
	HookTarget(H_DRAW_INST, vt[VT_Context_DrawInstanced]);
	HookTarget(H_DRAW_IIIND, vt[VT_Context_DrawIndexedInstancedIndirect]);
	HookTarget(H_DRAW_INSTIND, vt[VT_Context_DrawInstancedIndirect]);
	HookTarget(H_DRAW_AUTO, vt[VT_Context_DrawAuto]);
	// ticket 4874: the model tap's Unmap copy
	HookTarget(H_MAP, vt[VT_Context_Map]);
	HookTarget(H_UNMAP, vt[VT_Context_Unmap]);
	HookTarget(H_UPDATESUB, vt[VT_Context_UpdateSubresource]);   // a later write clears a resource's identity
	HookTarget(H_COPYRES, vt[VT_Context_CopyResource]);
	HookTarget(H_COPYSUB, vt[VT_Context_CopySubresourceRegion]);
}

static void DiscoverDevice(ID3D11Device* d)
{
	HookTarget(H_CREATEDEF, (*(void***)d)[VT_Device_CreateDeferredContext]);
	HookTarget(H_CREATE_VS, (*(void***)d)[VT_Device_CreateVertexShader]);   // issue 4752: path 1 of the unit-draw filter
	HookTarget(H_CREATE_PS, (*(void***)d)[VT_Device_CreatePixelShader]);
	HookTarget(H_CREATE_BUF, (*(void***)d)[VT_Device_CreateBuffer]);       // ticket 4874: the model tap's resource identity
	HookTarget(H_CREATE_TEX2D, (*(void***)d)[VT_Device_CreateTexture2D]);
	ID3D11Device1* d1 = nullptr; ID3D11Device2* d2 = nullptr; ID3D11Device3* d3 = nullptr;
	if (SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device1), (void**)&d1))) { HookTarget(H_CREATEDEF, (*(void***)d1)[VT_Device1_CreateDeferredContext1]); d1->Release(); }
	if (SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device2), (void**)&d2))) { HookTarget(H_CREATEDEF, (*(void***)d2)[VT_Device2_CreateDeferredContext2]); d2->Release(); }
	if (SUCCEEDED(d->QueryInterface(__uuidof(ID3D11Device3), (void**)&d3))) { HookTarget(H_CREATEDEF, (*(void***)d3)[VT_Device3_CreateDeferredContext3]); d3->Release(); }
	ID3D11DeviceContext* imm = nullptr;
	d->GetImmediateContext(&imm);
	if (imm) { DiscoverContext(imm); imm->Release(); }
	ID3D11DeviceContext* def = nullptr;
	int saved = t_inComposite; t_inComposite = 1;
	HRESULT hr = d->CreateDeferredContext(0, &def);
	t_inComposite = saved;
	if (SUCCEEDED(hr) && def) { DiscoverContext(def); def->Release(); }
	else Log("discovery: CreateDeferredContext on device %p failed 0x%08X (single-threaded device?)", d, hr);
}

static void DiscoverSwapChain(IDXGISwapChain* sc)
{
	void** vt = *(void***)sc;
	HookTarget(H_PRESENT, vt[VT_SwapChain_Present]);
	HookTarget(H_RESIZE, vt[VT_SwapChain_ResizeBuffers]);
	IDXGISwapChain1* s1 = nullptr;
	if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&s1))) { HookTarget(H_PRESENT1, (*(void***)s1)[VT_SwapChain1_Present1]); s1->Release(); }
}

static void DiscoverFactory(IDXGIFactory* f)
{
	HookTarget(H_CREATESC, (*(void***)f)[VT_Factory_CreateSwapChain]);
	IDXGIFactory2* f2 = nullptr;
	if (SUCCEEDED(f->QueryInterface(__uuidof(IDXGIFactory2), (void**)&f2))) { HookTarget(H_CREATESC_HWND, (*(void***)f2)[VT_Factory2_CreateSwapChainForHwnd]); f2->Release(); }
}

// ===========================================================================
// Command-list tags
// ===========================================================================
struct Tags { uint32_t bits; uint8_t nClears, nRt, nDs; int16_t rt[4]; int16_t ds[4]; int ctxIdx; uint8_t nBinds; uint16_t rtW, rtH, dsW, dsH;
	ID3D11DepthStencilView* clearDsv;      // issue 3304: the view of the list's last depth clear (no reference held: the list
	                                       // holds it while it executes; used only inside that ExecuteCommandList)
	ID3D11RenderTargetView* clearScene;    // ticket 0804: the list's clear of the EXE's offscreen scene target (IsMsScene, or
	                                       // SsSceneAtFinish), the insertion's target then (held as clearDsv is)
	ID3D11RenderTargetView* sceneCand; };  // ticket 0804: the list's clear of a single-sampled render target of the back
	                                       // buffer's size (decided at FinishCommandList: SsSceneAtFinish)

// ---- ticket 0804: the EXE's multisampled scene target ---------------------------------------------------------------
// With MSAASamples > 1 and AllowLeaderAA = 1 (GraphicsSettingsDX11.ini) the EXE keeps a single-sampled swap chain and
// draws its scene into a multisampled FP16 target of the back buffer's size (with its multisampled depth), which it
// resolves into the back buffer before the interface: no list clears the back buffer, the clear_bb rule never matched and
// the server's world was never drawn (the beta 4 report: a grey map, then the fog's 10 s wait for an overlay pair ended
// the game). With AllowLeaderAA = 0 the swap chain itself is multisampled instead
// (research/05-graphics-composition/shared-texture-and-compositor.md 16). Such a
// target stands for the back buffer in the tags (its clear is clear_bb, its binds bind_bb / bind_bb_dsv), and the
// insertion draws into it (and its depth) rather than into the back buffer, which the resolve overwrites. Only a view of
// dimension TEXTURE2DMS is looked at further, so a single-sampled game pays one GetDesc per view, as before.
static volatile LONG g_msSceneSeen;        // 1 after the first one (logged once)
static bool IsMsScene(ID3D11RenderTargetView* rtv, const D3D11_RENDER_TARGET_VIEW_DESC& d)
{
	if (!rtv || d.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2DMS || g_bbSamples != 1 || !g_bbW || !g_bbH) return false;
	ID3D11Resource* r = nullptr; rtv->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	D3D11_TEXTURE2D_DESC td = {};
	if (r && SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { t->GetDesc(&td); t->Release(); }
	SafeRelease(r);
	const bool is = td.SampleDesc.Count > 1 && td.Width == g_bbW && td.Height == g_bbH && td.ArraySize == 1;
	if (is && !InterlockedExchange(&g_msSceneSeen, 1))
		Log("scene target: the EXE draws its scene into a %u-sample %ux%u texture (format %d) and the back buffer is single-sampled: "
			"its clear stands for the back buffer's and the server's world is drawn into it (ticket 0804)", td.SampleDesc.Count, td.Width, td.Height, td.Format);
	return is;
}

// The same for a single-sampled offscreen scene target that the EXE resolves or copies into the back buffer (the
// Architect's second case, ticket 0804; not seen in a game yet): a TEXTURE2D render-target view of a texture of the back
// buffer's size (not the back buffer) is a candidate when a list clears it (OnDefClear: sceneCand); it is the scene
// target only when the same list, which does not clear the back buffer, also clears a depth view of the back buffer's
// size (decided when the list is finished, so the order of the two clears does not matter). Every frame the game draws
// into the back buffer has its scene clear in a clear_bb list, and the back-buffer-sized single-sampled targets it clears
// elsewhere (an FP16 target, research 05 shared-texture-and-compositor.md 16.3's trace) are cleared without a depth, so
// such frames never take this route. The recognised texture is remembered (no reference; compared only) so its binds
// tag as the back buffer's; RefreshBackBuffer forgets it.
static ID3D11Resource* volatile g_ssSceneRes;
static volatile LONG g_ssSceneSeen;
static bool TexBbSized(ID3D11View* v, UINT wantSamples, D3D11_TEXTURE2D_DESC* out = nullptr)
{
	if (!v || !g_bbW || !g_bbH) return false;
	ID3D11Resource* r = nullptr; v->GetResource(&r);
	ID3D11Texture2D* t = nullptr;
	D3D11_TEXTURE2D_DESC td = {};
	if (r && SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { t->GetDesc(&td); t->Release(); }
	SafeRelease(r);
	if (out) *out = td;
	return td.Width == g_bbW && td.Height == g_bbH && td.ArraySize == 1 && (!wantSamples || td.SampleDesc.Count == wantSamples);
}

// At FinishCommandList (the list's tags complete): a candidate cleared with a back-buffer-sized depth becomes the scene.
static void SsSceneAtFinish(Tags& t)
{
	if ((t.bits & TAG_CLEAR_BB) || !t.sceneCand || !t.clearDsv) return;
	if (!TexBbSized(t.clearDsv, 0)) return;
	D3D11_TEXTURE2D_DESC td = {};
	if (!TexBbSized(t.sceneCand, 1, &td)) return;
	t.bits |= TAG_CLEAR_BB;
	t.clearScene = t.sceneCand;
	ID3D11Resource* r = nullptr; t.sceneCand->GetResource(&r);
	g_ssSceneRes = r;   // compared only
	SafeRelease(r);
	if (!InterlockedExchange(&g_ssSceneSeen, 1))
		Log("scene target: the EXE clears a single-sampled %ux%u texture (format %d) and a depth of the back buffer's size in one list "
			"that does not clear the back buffer: that texture stands for the back buffer and the server's world is drawn into it (ticket 0804)",
			td.Width, td.Height, td.Format);
}

static bool IsSsScene(ID3D11RenderTargetView* rtv)
{
	ID3D11Resource* want = g_ssSceneRes;
	if (!want || !rtv) return false;
	ID3D11Resource* r = nullptr; rtv->GetResource(&r);
	const bool is = r == want;
	SafeRelease(r);
	return is;
}

// issue 3005: width and height of the 2D texture behind a view (0 x 0 if not a 2D texture)
static void ViewSize(ID3D11View* v, uint16_t& w, uint16_t& h)
{
	ID3D11Resource* r = nullptr;
	v->GetResource(&r);
	if (!r) return;
	ID3D11Texture2D* t = nullptr;
	if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t)
	{
		D3D11_TEXTURE2D_DESC d; t->GetDesc(&d);
		w = (uint16_t)d.Width; h = (uint16_t)d.Height;
		t->Release();
	}
	r->Release();
}
struct CtxRec { ID3D11DeviceContext* volatile ctx; int index; bool deferred; Tags cur; };
static CtxRec g_ctxs[128];
static volatile LONG g_nCtx;
static CRITICAL_SECTION g_ctxLock;
struct ClRec { ID3D11CommandList* cl; Tags t; int64_t frame; };
static ClRec g_cls[1024];
static CRITICAL_SECTION g_clLock;

static CtxRec* FindCtx(ID3D11DeviceContext* c)
{
	LONG n = g_nCtx;
	for (LONG i = 0; i < n; ++i) if (g_ctxs[i].ctx == c) return g_ctxs[i].deferred ? &g_ctxs[i] : nullptr;
	EnterCriticalSection(&g_ctxLock);
	CtxRec* r = nullptr;
	n = g_nCtx;
	for (LONG i = 0; i < n && !r; ++i) if (g_ctxs[i].ctx == c) r = &g_ctxs[i];
	if (!r && n < (LONG)_countof(g_ctxs))
	{
		r = &g_ctxs[n];
		r->deferred = c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
		r->index = n;
		memset(&r->cur, 0, sizeof(r->cur));
		r->ctx = c;
		MemoryBarrier();
		InterlockedIncrement(&g_nCtx);
		if (r->deferred) Log("deferred context #%d %p seen (thread %u)", r->index, c, GetCurrentThreadId());
	}
	LeaveCriticalSection(&g_ctxLock);
	return (r && r->deferred) ? r : nullptr;
}

static void AddFmt(int16_t* a, uint8_t& n, int f) { for (int i = 0; i < n; ++i) if (a[i] == f) return; if (n < 4) a[n++] = (int16_t)f; }

static bool Matches(const Cond& c, const Tags& t)
{
	if (c.empty) return true;
	if (c.impossible) return false;
	if ((t.bits & c.req) != c.req || (t.bits & c.forbid)) return false;
	if (c.ctxIdx >= 0 && t.ctxIdx != c.ctxIdx) return false;
	if (c.minClears > 0 && t.nClears < c.minClears) return false;
	if (c.dsvFmt >= 0) { bool f = false; for (int i = 0; i < t.nDs; ++i) f |= t.ds[i] == c.dsvFmt; if (!f) return false; }
	if (c.rtFmt >= 0) { bool f = false; for (int i = 0; i < t.nRt; ++i) f |= t.rt[i] == c.rtFmt; if (!f) return false; }
	return true;
}

static void StoreTags(ID3D11CommandList* cl, const Tags& t)
{
	EnterCriticalSection(&g_clLock);
	unsigned h = (unsigned)(((uintptr_t)cl >> 4) & 1023);
	int slot = -1, oldest = -1; int64_t oldestFrame = INT64_MAX;
	for (int k = 0; k < 16; ++k)
	{
		int i = (h + k) & 1023;
		if (g_cls[i].cl == cl || !g_cls[i].cl) { slot = i; break; }
		if (g_cls[i].frame < oldestFrame) { oldestFrame = g_cls[i].frame; oldest = i; }
	}
	if (slot < 0) slot = oldest;
	g_cls[slot].cl = cl; g_cls[slot].t = t; g_cls[slot].frame = g_frameIndex;
	LeaveCriticalSection(&g_clLock);
}

static bool LookupTags(ID3D11CommandList* cl, Tags& out, bool erase)
{
	bool found = false;
	EnterCriticalSection(&g_clLock);
	unsigned h = (unsigned)(((uintptr_t)cl >> 4) & 1023);
	for (int k = 0; k < 16; ++k)
	{
		int i = (h + k) & 1023;
		if (g_cls[i].cl == cl)
		{
			out = g_cls[i].t; found = true;
			if (erase) { g_cls[i].cl = nullptr; g_cls[i].frame = 0; }
			break;
		}
	}
	LeaveCriticalSection(&g_clLock);
	if (!found) memset(&out, 0, sizeof(out));
	return found;
}

// inlist: record our draw into the deferred context right after its back-buffer clear (recording thread)
static int DeferredDraw(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv)
{
	if (g_cfg.mode != MODE_COMPOSITE || g_disabled) return TAG_WOULD;
	if (g_stale && g_cfg.hideWhenStale) return TAG_WOULD;
	int r = TAG_NOTREADY;
	int64_t t0 = Qpc();
	AcquireSRWLockShared(&g_resLock);
	if (g_resourcesOk && g_srvUnorm)
	{
		if (!g_haveContent) r = TAG_WOULD;
		else
		{
			if (!t_saved) t_saved = (SavedState*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SavedState));
			if (t_saved) { DrawQuadOn(c, rtv, *t_saved); r = TAG_INSERTED; }
		}
	}
	ReleaseSRWLockShared(&g_resLock);
	double ms = QpcToMs(Qpc() - t0);
	EnterCriticalSection(&g_recLock); Push(g_rec, g_nRec, ms); LeaveCriticalSection(&g_recLock);
	return r;
}

static void OnDefClear(ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv)
{
	LsOnDeferred();
	CtxRec* r = FindCtx(c);
	if (!r) return;
	bool bb = IsBackBuffer(rtv);
	if (rtv && !bb)
	{
		D3D11_RENDER_TARGET_VIEW_DESC d; rtv->GetDesc(&d);
		if (IsMsScene(rtv, d)) { bb = true; r->cur.clearScene = rtv; }   // ticket 0804
		else if (d.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D && d.Texture2D.MipSlice == 0 && TexBbSized(rtv, 1))
			r->cur.sceneCand = rtv;   // ticket 0804: decided at FinishCommandList (SsSceneAtFinish)
	}
	r->cur.bits |= bb ? TAG_CLEAR_BB : TAG_CLEAR_RT;
	if (r->cur.nClears < 255) r->cur.nClears++;
	if (rtv) { D3D11_RENDER_TARGET_VIEW_DESC d; rtv->GetDesc(&d); AddFmt(r->cur.rt, r->cur.nRt, d.Format); }
	if (bb && g_cfg.insert == INS_TAGGED && g_cfg.tagRule == RULE_INLIST && !(r->cur.bits & (TAG_INSERTED | TAG_WOULD | TAG_NOTREADY)))
	{
		Tags probe = r->cur; probe.ctxIdx = r->index;
		if (!Matches(g_cfg.match, probe)) return;
		t_inComposite = 1;
		r->cur.bits |= DeferredDraw(c, rtv);
		t_inComposite = 0;
	}
}

static void OnDefClearDsv(ID3D11DeviceContext* c, ID3D11DepthStencilView* dsv, UINT flags)
{
	CtxRec* r = FindCtx(c);
	if (!r) return;
	r->cur.bits |= TAG_CLEAR_DSV;
	if (dsv && (flags & D3D11_CLEAR_DEPTH)) r->cur.clearDsv = dsv;   // issue 3304: depth_write writes into this view
	if (dsv) { D3D11_DEPTH_STENCIL_VIEW_DESC d; dsv->GetDesc(&d); AddFmt(r->cur.ds, r->cur.nDs, d.Format);
		if (g_traceWanted && !r->cur.dsW) ViewSize(dsv, r->cur.dsW, r->cur.dsH); }
}

static void OnDefOMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
	LsOnDeferred();
	CtxRec* r = FindCtx(c);
	if (!r) return;
	if (r->cur.nBinds < 255) r->cur.nBinds++;
	for (UINT i = 0; i < n && rtvs; ++i)
	{
		if (!rtvs[i]) continue;
		bool bb = IsBackBuffer(rtvs[i]);
		D3D11_RENDER_TARGET_VIEW_DESC d; rtvs[i]->GetDesc(&d);
		if (!bb && (IsMsScene(rtvs[i], d) || IsSsScene(rtvs[i]))) bb = true;   // ticket 0804: the scene target binds as the back buffer
		r->cur.bits |= bb ? (dsv ? TAG_BIND_BB_DSV : TAG_BIND_BB) : (dsv ? TAG_BIND_RT_DSV : TAG_BIND_RT);
		AddFmt(r->cur.rt, r->cur.nRt, d.Format);
		if (g_traceWanted && !bb && !r->cur.rtW) ViewSize(rtvs[i], r->cur.rtW, r->cur.rtH);
	}
	if (dsv) { D3D11_DEPTH_STENCIL_VIEW_DESC d; dsv->GetDesc(&d); AddFmt(r->cur.ds, r->cur.nDs, d.Format);
		if (g_traceWanted && !r->cur.dsW) ViewSize(dsv, r->cur.dsW, r->cur.dsH); }
}

static void OnFinish(ID3D11DeviceContext* c, ID3D11CommandList* cl)
{
	CtxRec* r = FindCtx(c);
	if (!r) return;
	Tags t = r->cur; t.ctxIdx = r->index;
	SsSceneAtFinish(t);   // ticket 0804
	StoreTags(cl, t);
	memset(&r->cur, 0, sizeof(r->cur));
}

static void OnDefExec(ID3D11DeviceContext* c, ID3D11CommandList* cl)
{
	CtxRec* r = FindCtx(c);
	if (!r) return;
	Tags t; LookupTags(cl, t, false);
	r->cur.bits |= t.bits | TAG_NESTED;
	for (int i = 0; i < t.nRt; ++i) AddFmt(r->cur.rt, r->cur.nRt, t.rt[i]);
	for (int i = 0; i < t.nDs; ++i) AddFmt(r->cur.ds, r->cur.nDs, t.ds[i]);
}

// ===========================================================================
// Depth probe (epic 3200, issue 3208; ini depth_probe=1, off by default)
// ===========================================================================
// Premise P8: the EXE's depth at the insertion. Read from the existing hooks only, nothing changed in the EXE's state:
//   * every ClearDepthStencilView on a deferred context: flags, depth, stencil, the view's format and size;
//   * at every FinishCommandList of a deferred context that has a depth view bound, before the original call (the
//     deferred context's state is reset by it): the bound depth-stencil state (OMGetDepthStencilState + GetDesc), the
//     stencil ref, viewport 0's MinDepth / MaxDepth, the view's format and size, the back buffer bound or not - i.e.
//     the state of the list's last draw;
//   * the FinishCommandList and ExecuteCommandList restore flags;
//   * at the insertion on the immediate context: its bound depth view and depth-stencil state.
// Distinct combinations are counted and logged every 5 s while something changed ("depth: ..." lines).
struct DepthClearRec { int fmt; uint16_t w, h; UINT flags; float depth; UINT stencil; uint32_t n; };
struct DepthStateRec
{
	int kind;                       // 0 = a list with the back buffer + DSV, 1 = another target + DSV, 2 = the immediate context at the insertion
	int fmt; uint16_t w, h; bool dssNull;
	D3D11_DEPTH_STENCIL_DESC d; UINT ref; float vpMin, vpMax; uint32_t n;
};
static CRITICAL_SECTION g_depthLock;
static DepthClearRec g_depthClears[32]; static int g_nDepthClears;
static DepthStateRec g_depthStates[64]; static int g_nDepthStates;
static volatile LONG g_depthFinishRestore[2], g_depthExecRestore[2], g_depthChanged, g_depthOverflow;
static int64_t g_depthLastLog;

static void DepthViewInfo(ID3D11DepthStencilView* dsv, int& fmt, uint16_t& w, uint16_t& h)
{
	fmt = -1; w = h = 0;
	if (!dsv) return;
	D3D11_DEPTH_STENCIL_VIEW_DESC d; dsv->GetDesc(&d); fmt = d.Format;
	ViewSize(dsv, w, h);
}

static void DepthProbeClear(ID3D11DepthStencilView* dsv, UINT fl, FLOAT depth, UINT8 stencil)
{
	DepthClearRec k = {}; DepthViewInfo(dsv, k.fmt, k.w, k.h); k.flags = fl; k.depth = depth; k.stencil = stencil;
	EnterCriticalSection(&g_depthLock);
	int i = 0;
	for (; i < g_nDepthClears; ++i)
	{
		const DepthClearRec& e = g_depthClears[i];
		if (e.fmt == k.fmt && e.w == k.w && e.h == k.h && e.flags == k.flags && e.depth == k.depth && e.stencil == k.stencil) break;
	}
	if (i == g_nDepthClears) { if (i < (int)_countof(g_depthClears)) { g_depthClears[i] = k; g_nDepthClears++; InterlockedExchange(&g_depthChanged, 1); } else { i = -1; InterlockedIncrement(&g_depthOverflow); } }
	if (i >= 0) g_depthClears[i].n++;
	LeaveCriticalSection(&g_depthLock);
}

static void DepthProbeState(ID3D11DeviceContext* c, int kindIfBb, bool immediate)
{
	ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;
	c->OMGetRenderTargets(1, &rtv, &dsv);
	if (!dsv && !immediate) { SafeRelease(rtv); return; }
	DepthStateRec k = {};
	k.kind = immediate ? 2 : (rtv && IsBackBuffer(rtv)) ? 0 : 1;
	(void)kindIfBb;
	DepthViewInfo(dsv, k.fmt, k.w, k.h);
	ID3D11DepthStencilState* dss = nullptr;
	c->OMGetDepthStencilState(&dss, &k.ref);
	k.dssNull = dss == nullptr;
	if (dss) dss->GetDesc(&k.d);
	else   // the null state = the D3D11 defaults
	{
		k.d.DepthEnable = TRUE; k.d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; k.d.DepthFunc = D3D11_COMPARISON_LESS; k.d.StencilEnable = FALSE;
		k.d.StencilReadMask = k.d.StencilWriteMask = 0xFF;
		k.d.FrontFace.StencilFailOp = k.d.FrontFace.StencilDepthFailOp = k.d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP; k.d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		k.d.BackFace = k.d.FrontFace;
	}
	UINT nvp = 1; D3D11_VIEWPORT vp = {}; c->RSGetViewports(&nvp, &vp);
	k.vpMin = nvp ? vp.MinDepth : -1.0f; k.vpMax = nvp ? vp.MaxDepth : -1.0f;
	SafeRelease(dss); SafeRelease(rtv); SafeRelease(dsv);
	EnterCriticalSection(&g_depthLock);
	int i = 0;
	for (; i < g_nDepthStates; ++i)
	{
		const DepthStateRec& e = g_depthStates[i];
		if (e.kind == k.kind && e.fmt == k.fmt && e.w == k.w && e.h == k.h && e.dssNull == k.dssNull && e.ref == k.ref &&
			e.vpMin == k.vpMin && e.vpMax == k.vpMax && memcmp(&e.d, &k.d, sizeof(k.d)) == 0) break;
	}
	if (i == g_nDepthStates) { if (i < (int)_countof(g_depthStates)) { g_depthStates[i] = k; g_nDepthStates++; InterlockedExchange(&g_depthChanged, 1); } else { i = -1; InterlockedIncrement(&g_depthOverflow); } }
	if (i >= 0) g_depthStates[i].n++;
	LeaveCriticalSection(&g_depthLock);
}

static const char* kCmpNames[] = { "?", "NEVER", "LESS", "EQUAL", "LESS_EQUAL", "GREATER", "NOT_EQUAL", "GREATER_EQUAL", "ALWAYS" };
static const char* kSopNames[] = { "?", "KEEP", "ZERO", "REPLACE", "INCR_SAT", "DECR_SAT", "INVERT", "INCR", "DECR" };
static const char* CmpName(int f) { return f >= 0 && f <= 8 ? kCmpNames[f] : "?"; }
static const char* SopName(int f) { return f >= 0 && f <= 8 ? kSopNames[f] : "?"; }

// Render thread (stats tick): the table, every 5 s while it changed.
static void DepthProbeLog(int64_t now)
{
	if (!g_cfg.depthProbe) return;
	if (g_depthLastLog && QpcToMs(now - g_depthLastLog) < 5000.0) return;
	g_depthLastLog = now;
	bool changed = InterlockedExchange(&g_depthChanged, 0) != 0;
	EnterCriticalSection(&g_depthLock);
	Log("depth: table (%s): %d clear kinds, %d state kinds, overflow %ld; FinishCommandList restore 0/1: %ld/%ld; ExecuteCommandList restore 0/1: %ld/%ld",
		changed ? "changed" : "unchanged", g_nDepthClears, g_nDepthStates, g_depthOverflow, g_depthFinishRestore[0], g_depthFinishRestore[1], g_depthExecRestore[0], g_depthExecRestore[1]);
	for (int i = 0; i < g_nDepthClears; ++i)
	{
		const DepthClearRec& e = g_depthClears[i];
		Log("depth:   clear #%d: fmt %d %ux%u flags %u%s%s depth %.9g stencil %u - %u times", i, e.fmt, e.w, e.h, e.flags,
			(e.flags & D3D11_CLEAR_DEPTH) ? " DEPTH" : "", (e.flags & D3D11_CLEAR_STENCIL) ? " STENCIL" : "", e.depth, e.stencil, e.n);
	}
	static const char* kinds[] = { "list bb+dsv", "list rt+dsv", "immediate at the insertion" };
	for (int i = 0; i < g_nDepthStates; ++i)
	{
		const DepthStateRec& e = g_depthStates[i];
		Log("depth:   state #%d (%s, last draw of the list): dsv fmt %d %ux%u, dss %s: depth %s func %s write %s, stencil %s rmask 0x%02X wmask 0x%02X ref %u "
			"front fail/dfail/pass %s/%s/%s func %s, back %s/%s/%s func %s; viewport depth %.3f..%.3f - %u times", i, kinds[e.kind], e.fmt, e.w, e.h,
			e.dssNull ? "null (defaults)" : "set", e.d.DepthEnable ? "on" : "off", CmpName(e.d.DepthFunc), e.d.DepthWriteMask ? "all" : "zero",
			e.d.StencilEnable ? "on" : "off", e.d.StencilReadMask, e.d.StencilWriteMask, e.ref,
			SopName(e.d.FrontFace.StencilFailOp), SopName(e.d.FrontFace.StencilDepthFailOp), SopName(e.d.FrontFace.StencilPassOp), CmpName(e.d.FrontFace.StencilFunc),
			SopName(e.d.BackFace.StencilFailOp), SopName(e.d.BackFace.StencilDepthFailOp), SopName(e.d.BackFace.StencilPassOp), CmpName(e.d.BackFace.StencilFunc),
			e.vpMin, e.vpMax, e.n);
	}
	LeaveCriticalSection(&g_depthLock);
}

// ===========================================================================
// Target swap chain handling
// ===========================================================================
static void RefreshBackBuffer(IDXGISwapChain* sc)
{
	g_bbRes = nullptr;
	g_ssSceneRes = nullptr;   // ticket 0804: recognised again on the new target's first scene clear
	ID3D11Resource* r = nullptr;
	if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Resource), (void**)&r)))
	{
		g_bbRes = r;
		ID3D11Texture2D* t = nullptr;
		if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)))
		{
			D3D11_TEXTURE2D_DESC td; t->GetDesc(&td);
			g_bbW = td.Width; g_bbH = td.Height; g_bbFormat = td.Format; g_bbSamples = td.SampleDesc.Count;
			t->Release();
		}
		r->Release();
	}
}

static ID3D11RenderTargetView* GetBbRtv()
{
	if (g_bbRtv || !g_target || !g_dev) return g_bbRtv;
	ID3D11Texture2D* bb = nullptr;
	if (SUCCEEDED(g_target->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)))
	{
		D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
		D3D11_RENDER_TARGET_VIEW_DESC rd = {};
		rd.Format = td.Format;
		if (rd.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		if (rd.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS) rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		rd.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
		HRESULT hr = g_dev->CreateRenderTargetView(bb, &rd, &g_bbRtv);
		if (FAILED(hr)) Log("CreateRenderTargetView(back buffer) failed 0x%08X", hr);
		bb->Release();
	}
	return g_bbRtv;
}

static bool AdoptTarget(IDXGISwapChain* sc, const char* how, LONG seq = 0)
{
	DXGI_SWAP_CHAIN_DESC d = {};
	sc->GetDesc(&d);
	ID3D11Device* dev = nullptr;
	if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)))
	{
		Log("swap chain %p has no ID3D11Device (D3D10/12?) - ignored", sc);
		return false;
	}
	g_dev = dev;
	ID3D11DeviceContext* ctx = nullptr;
	g_dev->GetImmediateContext(&ctx);
	g_ctx = ctx;
	if (FAILED(g_dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&g_dev1)))
		Log("WARNING: device has no ID3D11Device1 (pre-Win8 runtime?) - cannot open shared textures by name");
	IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr;
	if (SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && SUCCEEDED(xd->GetAdapter(&ad)))
	{
		DXGI_ADAPTER_DESC a; ad->GetDesc(&a);
		g_clientLuid = ((uint64_t)(uint32_t)a.AdapterLuid.HighPart << 32) | a.AdapterLuid.LowPart;
		Log("host adapter '%ls' LUID %016llX", a.Description, g_clientLuid);
	}
	SafeRelease(ad); SafeRelease(xd);
	g_target = sc;
	g_targetSeq = seq;
	g_targetHwnd = d.OutputWindow;
	RefreshBackBuffer(sc);
	char wtitle[128] = ""; GetWindowTextA(d.OutputWindow, wtitle, sizeof(wtitle));
	Log("adopted swap chain %p creation #%ld (%s): %ux%u fmt %u buffers %u effect %u samples %u windowed %d flags 0x%X hwnd %p '%s'; device %p FL %x ctx %p",
		sc, seq, how, d.BufferDesc.Width, d.BufferDesc.Height, d.BufferDesc.Format, d.BufferCount, d.SwapEffect, d.SampleDesc.Count, d.Windowed,
		d.Flags, d.OutputWindow, wtitle, g_dev, g_dev->GetFeatureLevel(), g_ctx);
	Log("back buffer resource %p %ux%u fmt %u samples %u", g_bbRes, g_bbW, g_bbH, g_bbFormat, g_bbSamples);
	// The host's own implementations (they depend on the device: feature level, flags) - hook any not seen yet.
	t_inComposite = 1;
	DiscoverSwapChain(sc);
	DiscoverDevice(g_dev);
	CommitHooks("adoption");
	t_inComposite = 0;
	g_traceLeft = g_cfg.traceFrames;
	return true;
}

static void DropTarget(const char* why)
{
	ReleaseAll(why);
	LkDeleteSource(why);   // issue 3304: a source belongs to one device
	ID3D11DeviceContext* c = g_ctx; g_ctx = nullptr; SafeRelease(c);
	SafeRelease(g_dev1); SafeRelease(g_dev);
	g_target = nullptr;
	g_targetSeq = 0;
}

static void ResetIntervalStats()
{
	g_nFt = g_nCc = g_nLat = 0; g_nAge = 0; g_ageSum = 0; g_nAcq = 0; g_noNewFrame = 0; g_nGpu = 0; g_nGpuF = 0; g_nGpuW = 0;
	g_srvNew = g_srvDrop = g_dups = g_timeouts = g_missedIns = g_clearsTotal = g_framesInt = 0;
	g_missNoMatch = g_missNoAnchor = g_missNoExec = g_missNotReady = g_insWrong = g_insChecked = g_listsTotal = g_untagged = g_noDraw = 0;
	g_camOkI = g_camBadI = g_camRetryI = g_camLogI = 0; g_nCamT = 0;
	g_waitGaveUpCount = 0;
	memset(&g_lkI, 0, sizeof(g_lkI)); g_nLkWait = 0; g_nLkUv = 0; g_lkLate = lklate::Counts();   // issue 3304
	EnterCriticalSection(&g_recLock); g_nRec = 0; LeaveCriticalSection(&g_recLock);
}

static void ApplyConfigIfChanged()
{
	LONG v = g_cfgVersion;
	if (v == g_cfgApplied) return;
	EnterCriticalSection(&g_cfgLock);
	Config old = g_cfg;
	g_cfg = g_cfgShared;
	LeaveCriticalSection(&g_cfgLock);
	g_cfgApplied = v;
	if (g_cfgApplied > 0)
		Log("config applied on render thread: mode=%s insert=%s nth=%d rule=%s match='%s'%s anchor='%s'%s tag_nth=%d timeout=%u blend=%s hide_when_stale=%d",
			kModeNames[g_cfg.mode], kInsNames[g_cfg.insert], g_cfg.nth, kRuleNames[g_cfg.tagRule], g_cfg.match.text, g_cfg.match.impossible ? " (UNKNOWN TOKEN: matches nothing)" : "",
			g_cfg.anchor.text, g_cfg.anchor.impossible ? " (UNKNOWN TOKEN: matches nothing)" : "", g_cfg.tagNth, g_cfg.timeoutMs, g_cfg.alphaBlend ? "alpha" : "opaque", g_cfg.hideWhenStale);
	if (wcscmp(old.section, g_cfg.section))
	{
		LkDeleteSource("section changed");   // issue 3304
		CloseFrameSection();
		if (g_hdr)
		{
			CloseShared();
			CloseDepthShared();
			UnmapViewOfFile(g_hdr); g_hdr = nullptr; CloseHandle(g_section); g_section = nullptr;
		}
	}
	g_timeoutsInRow = 0; g_waitGaveUp = false; g_waitGaveUpLogged = false;   // issue 3014: a new config waits again
	g_lkGaveUp = false; g_lkMissRow = 0; g_lkxHung = false; g_lkHeldRunQpc = 0;   // issue 3304, ticket 0691: likewise
	ResetIntervalStats();
	g_intervalStart = Qpc();
}

static void StatsTick(int64_t now)
{
	if (!g_intervalStart) { g_intervalStart = now; return; }
	DepthProbeLog(now);   // issue 3208 (only with depth_probe=1)
	double el = QpcToMs(now - g_intervalStart);
	if (el < g_cfg.statsMs) return;
	double ftM, ftP, ftX, ccM, ccP, ccX, lM, lP, lX, aM, aP, aX, gM, gP, gX, fM, fP, fX, wM, wP, wX, rM, rP, rX;
	int nCc = g_nCc, nLat = g_nLat, nAcq = g_nAcq, nGpu = g_nGpu, nGpuF = g_nGpuF, nGpuW = g_nGpuW;
	MeanP99(g_gpuF, g_nGpuF, fM, fP, fX);
	MeanP99(g_gpuW, g_nGpuW, wM, wP, wX);
	MeanP99(g_acq, g_nAcq, aM, aP, aX);
	MeanP99(g_gpu, g_nGpu, gM, gP, gX);
	MeanP99(g_ft, g_nFt, ftM, ftP, ftX);
	MeanP99(g_cc, g_nCc, ccM, ccP, ccX);
	MeanP99(g_lat, g_nLat, lM, lP, lX);
	double cmM, cmP, cmX;
	MeanP99(g_camT, g_nCamT, cmM, cmP, cmX);
	EnterCriticalSection(&g_recLock);
	int nRec = g_nRec;
	MeanP99(g_rec, g_nRec, rM, rP, rX);
	g_nRec = 0;
	LeaveCriticalSection(&g_recLock);
	Log("stats el=%.0fms mode=%s ins=%s rule=%s frames=%d ft_mean=%.3f ft_p99=%.3f ft_max=%.3f comp_n=%d comp_mean=%.4f comp_p99=%.4f comp_max=%.4f rec_n=%d rec_mean=%.4f rec_p99=%.4f "
		"acq_n=%d acq_mean=%.4f acq_p99=%.4f no_new=%d gpu_draw_n=%d gpu_draw_mean=%.4f gpu_draw_p99=%.4f gpu_copy_n=%d gpu_copy_mean=%.4f gpu_copy_p99=%.4f gpu_syncwait_n=%d gpu_syncwait_mean=%.4f gpu_syncwait_p99=%.4f "
		"lat_n=%d lat_mean=%.2f lat_p99=%.2f age_mean=%.2f srv_new=%d srv_drop=%d dup=%d nodraw=%d timeouts=%d wait_off=%d missed_ins=%d miss_nomatch=%d miss_noanchor=%d miss_noexec=%d miss_notready=%d "
		"ins_checked=%d ins_wrong=%d lists_pf=%.1f untagged=%d clears_pf=%.1f stale=%d srv=%u/%u tex=%ux%u bb=%ux%u "
		"cam=%s cam_ok=%d cam_bad=%d cam_retry=%d cam_views=%u cam_mode=%u cam_grid=%u cam_log_suppressed=%d cam_us_mean=%.2f cam_us_p99=%.2f "
		"tap=%s tap_fail=%s tap_copies=%ld%s",
		el, kModeNames[g_cfg.mode], kInsNames[g_cfg.insert], kRuleNames[g_cfg.tagRule], g_framesInt, ftM, ftP, ftX, nCc, ccM, ccP, ccX, nRec, rM, rP,
		nAcq, aM, aP, g_noNewFrame, nGpu, gM, gP, nGpuF, fM, fP, nGpuW, wM, wP,
		nLat, lM, lP, g_nAge ? g_ageSum / g_nAge : 0.0, g_srvNew, g_srvDrop, g_dups, g_noDraw, g_timeouts, g_waitGaveUpCount, g_missedIns, g_missNoMatch, g_missNoAnchor, g_missNoExec, g_missNotReady,
		g_insChecked, g_insWrong, g_framesInt ? (double)g_listsTotal / g_framesInt : 0.0, g_untagged,
		g_framesInt ? (double)g_clearsTotal / g_framesInt : 0.0, (int)g_stale, g_openPid, g_openGen, g_localW, g_localH, g_bbW, g_bbH,
		g_camLastStatus == 0xFFFFFFFFu ? "-" : VPCamStatusName(g_camLastStatus), g_camOkI, g_camBadI, g_camRetryI, g_camBlk.numViews, g_camBlk.viewMode,
		g_camBlk.gridSeq, g_camLogSuppressed, cmM * 1000.0, cmP * 1000.0,
		TapStateName((uint32_t)g_tapLastState), TapFailName((uint32_t)g_tapLastFail), (long)g_tapCopies, g_disabled ? " DISABLED" : "");
	UnitFilter_StatsLine(Log, el);   // issue 4752: "unitfilter 1s:" and "unitfilter totals:" (not "stats": shellrun.stats() parses those)
	if (LsRows() || g_nLsD)
	{
		double dM, dP, dX, dMin = 0;
		if (g_nLsD) { dMin = *std::min_element(g_lsD, g_lsD + g_nLsD); }
		MeanP99(g_lsD, g_nLsD, dM, dP, dX);
		Log("lsprobe 1s: on=%ld n=%d uvsc_mean=%.3f uvsc_min=%.3f uvsc_max=%.3f no_sc=%d no_uv=%d uv_multi=%d uv_total=%ld rows=%ld dropped=%ld",
			g_lsOn, g_nLsD, dM, dMin, dX, g_lsNoSc, g_lsNoUv, g_lsUvMulti, g_lsUvCount, g_lsHead, g_lsDropped);
	}
	g_nLsD = 0; g_lsNoSc = g_lsNoUv = g_lsUvMulti = 0;
	if (SourcePath() || g_lkI.pubUv || g_lkI.pubPresent)
	{
		// issue 3304: one line per interval (not "stats": shellrun.stats() parses those)
		double wM, wP, wX, uM, uP, uX;
		MeanP99(g_lkWaitUs, g_nLkWait, wM, wP, wX);
		MeanP99(g_lkUvUs, g_nLkUv, uM, uP, uX);
		char ring[256] = "";
		if (g_ring)
		{
			const vpframe::RingSourceStats& rs = g_ring->stats();
			_snprintf_s(ring, _TRUNCATE, " ring_gen=%u ring_sync=%u ring_exact=%lld ring_older=%lld ring_none=%lld ring_released=%lld ring_fence_waits=%lld ring_claim_retries=%lld ring_pending=%d",
				g_ring->generation(), g_ring->syncInForce(), rs.exact, rs.older, rs.none, rs.released, rs.fenceWaits, rs.claimRetries, g_ring->pending());
		}
		Log("lockstep 1s: lockstep=%d src=%s open=%d bound_us=%u giveup=%d hook=%s pub_uv=%d pub_present=%d exact=%d older=%d none=%d nopub=%d noins=%d "
			"waits=%d wait_mean_us=%.1f wait_p99_us=%.1f wait_max_us=%.1f late_start=%d overrun=%d overrun_max_us=%.1f wait_off=%d giveups=%d recoveries=%d gave_up=%d "
			"depth=%d depth_nodsv=%d depth_refused=%d depth_nosrc=%d cmp_n=%d cmp_diff=%d uv_us_mean=%.2f uv_us_p99=%.2f uv_us_max=%.2f "
			"wrong_thread=%d src_fail=%d last_f=%lld rows=%ld dropped=%ld unbaked_n=%d gray_frames=%d gray_tiles=%lld mode=%s hung_ms=%d hung_ends=%d dead_ends=%d hung=%d "
			"held_skip=%d held_faults=%d%s",
			g_cfg.lockstep, g_src ? g_src->name() : "-", (int)g_srcOpen, g_cfg.boundUs, g_cfg.giveup, g_lsState == 1 ? (g_lsOn ? "on" : "off") : g_lsState < 0 ? "refused" : "none",
			g_lkI.pubUv, g_lkI.pubPresent, g_lkI.exact, g_lkI.older, g_lkI.none, g_lkI.nopub, g_lkI.noins,
			g_lkI.waits, wM, wP, wX, g_lkLate.lateStart, g_lkLate.overrun, g_lkLate.overrunMaxUs, g_lkI.waitOff, g_lkI.giveups, g_lkI.recoveries, (int)g_lkGaveUp,
			g_lkI.depthN, g_lkI.depthNoDsv, g_lkI.depthRefused, g_lkI.depthNoSrc, g_lkI.cmpN, g_lkI.cmpDiff, uM, uP, uX,
			g_lkI.wrongThread, g_lkI.srcFail, g_lkLastF, (long)g_lkHead, (long)g_lkDropped, g_lkI.unbakedN, g_lkI.grayFrames,
			(long long)g_lkI.grayTiles, g_cfg.lockstepExact ? "exact" : "bounded", g_cfg.hungMs, g_lkI.hungEnds, g_lkI.deadEnds, (int)g_lkxHung,
			g_lkI.heldSkips, g_lkI.heldFaults, ring);   // 0691
	}
	PcSecond(now);   // ticket 0688: the second's pacing numbers for the control thread's "pacing 1s:" line
	ResetIntervalStats();
	g_intervalStart = now;
}

static void TraceFrameLists()
{
	Log("trace: ---- Present #%lld (clears %d, omsets %d, of which bind the back buffer %d, ExecuteCommandLists %d this frame)", g_frameIndex, g_clearCount, g_omsetCount, g_omsetBbCount, g_execCount);
	for (int i = 0; i < g_nFl; ++i)
	{
		const FrameList& f = g_fl[i];
		char tags[160]; TagsText(f.bits, tags, sizeof(tags));
		const char* ev = f.ev == 'A' ? "  <== INSERT after (tagged)" : f.ev == 'B' ? "  <== INSERT before (tagged)" : f.ev == 'I' ? "  <== INSERT inside (inlist)" :
			f.ev == 'E' ? "  <== INSERT after (exec nth)" : "";
		char fm[96]; int k = 0; fm[0] = 0;
		for (int j = 0; j < f.nRt && k < 80; ++j) k += sprintf_s(fm + k, sizeof(fm) - k, "%s%d", j ? "," : "", f.rt[j]);
		k += sprintf_s(fm + k, sizeof(fm) - k, " ds ");
		for (int j = 0; j < f.nDs && k < 88; ++j) k += sprintf_s(fm + k, sizeof(fm) - k, "%s%d", j ? "," : "", f.ds[j]);
		Log("trace:   list #%d cl %p ctx#%d %s rt0 %d ds0 %d fmts rt %s nclears %u nbinds %u rtsz %ux%u dssz %ux%u%s%s", i + 1, f.cl, f.ctxIdx, f.known ? tags : "untagged", f.rt0, f.ds0,
			fm, f.nClears, f.nBinds, f.rtW, f.rtH, f.dsW, f.dsH, ev, f.wrong ? " (OUTSIDE the clear..3D window)" : "");
	}
}

// Issue 3304, at every Present of the target (after its insertion): finish the frame's row, reset the per-frame publish,
// and decide whether the next UpdateViews publishes (the hook reads g_lkWant).
static void LkOnPresent(int64_t now)
{
	if (SourcePath())
	{
		LkRow& r = g_lkCur;
		r.frame = g_frameIndex;
		if (g_lkUvF) { r.pubF = g_lkUvF; r.pubSrc = VPPUB_UPDATEVIEWS; r.pubQpc = g_lkUvQpc; r.deadline = g_lkUvDeadline; }
		if (!g_lkCurIns) { r.kind = LK_NOINS; r.unbaked = -1; g_lkI.noins++; }
		r.presentQpc = now;
		r.cx = g_camBlk.camWorld[12]; r.cy = g_camBlk.camWorld[13]; r.cz = g_camBlk.camWorld[14];
		PcWorld(r);   // ticket 0688: world update, world age, freezes (wu, world_age_ms)
		if (g_cfg.lockstepCsv)
		{
			LONG h = g_lkHead;
			if (h - g_lkTail >= kLkRing) InterlockedIncrement(&g_lkDropped);   // the control thread fell behind
			g_lkRing[h % kLkRing] = r;
			MemoryBarrier();
			InterlockedExchange(&g_lkHead, h + 1);
		}
	}
	memset(&g_lkCur, 0, sizeof(g_lkCur));
	g_lkCur.cmp = -1;   // not compared (lockstep_check=0 or no UpdateViews publish)
	g_lkCur.unbaked = -1;
	g_lkCurIns = false;
	g_lkUvF = 0; g_lkUvWait = false;
	InterlockedExchange(&g_lkWant, (g_cfg.lockstep && SourcePath() && g_cfg.mode == MODE_COMPOSITE && !g_disabled && !g_lkUvDead) ? 1 : 0);
}

static void OnPresent(IDXGISwapChain* sc)
{
	InterlockedExchange(&g_presentSeen, 1);
	ApplyConfigIfChanged();
	if (g_cfg.mode == MODE_OFF) return;
	int64_t now = Qpc();
	// Issue 0141: read the pointer, then its creation number (the creating thread writes them in the other order). A
	// pending chain at the target's own address is a new chain that got the released target's address: it is a
	// different chain when its creation number differs, and goes through the same rule as any new chain.
	LONG pendSeq = 0;
	bool pending = g_pendingAdopt == sc;
	if (pending) { MemoryBarrier(); pendSeq = g_pendingSeq; }
	if (pending && (sc != g_target || pendSeq != g_targetSeq))
	{
		const bool reused = sc == g_target;   // the old target's address, a new chain (0141)
		// Issue 3017 (verification 9031, V4): the rule is "the target is the swap chain of the game's main window". A
		// chain created through the factory replaces the target only when it presents to the target's window (the
		// game's resolution change: new chain, same hwnd, hooks-overlays-and-ceg.md section 5) or when the target's
		// window no longer exists (a host that recreates its window). Any other chain (an overlay, a second window)
		// is only listed as another chain; the 2 s silence rule below still hands over when the target stops.
		g_pendingAdopt = nullptr;
		bool take = !g_target;
		const char* how = "at its first Present after creation";
		if (g_target)
		{
			DXGI_SWAP_CHAIN_DESC d = {}; sc->GetDesc(&d);
			if (d.OutputWindow && d.OutputWindow == g_targetHwnd) { take = true; how = "at its first Present after creation, same window as the old target"; }
			else if (!g_targetHwnd || !IsWindow(g_targetHwnd)) { take = true; how = "at its first Present after creation, the old target's window is gone"; }
			// an address is reused only after its chain was freed: the old target no longer exists
			else if (reused) { take = true; how = "at its first Present after creation, the old target was released (its address reused)"; }
			else
				Log("swap chain %p created through the factory presents to window %p, not the target's window %p (which still exists): NOT adopted, the target stays %p",
					sc, d.OutputWindow, g_targetHwnd, g_target);
		}
		if (take)
		{
			if (reused)
				Log("swap chain %p creation #%ld has the address of the target (creation #%ld), which was released: a new chain, adopted as one (issue 0141)",
					sc, pendSeq, g_targetSeq);
			if (g_target) DropTarget(reused ? "a new swap chain at the old target's address was created through the factory"
			                                : "a new swap chain was created through the factory");
			if (!AdoptTarget(sc, how, pendSeq)) return;
		}
	}
	if (g_target && sc != g_target)
	{
		bool known = false;
		for (auto& o : g_otherChains) { if (o == sc) { known = true; break; } }
		if (!known)
		{
			bool listed = false;
			for (auto& o : g_otherChains) if (!o) { o = sc; listed = true; break; }
			if (listed)
			{
				DXGI_SWAP_CHAIN_DESC d = {}; sc->GetDesc(&d);
				Log("another swap chain presents: %p %ux%u hwnd %p (target stays %p)", sc, d.BufferDesc.Width, d.BufferDesc.Height, d.OutputWindow, g_target);
			}
			else
			{
				// issue 3017: more than 8 other chains used to log at every Present (2.17 MB in 6 s); now counted, and the
				// count is logged at most once per 10 s
				g_otherUnlisted++;
				if (!g_otherUnlistedLogQpc || QpcToMs(now - g_otherUnlistedLogQpc) >= 10000.0)
				{
					Log("other swap chains beyond the %d listed presented %lld times so far (one line per 10 s at most; target stays %p)",
						(int)_countof(g_otherChains), g_otherUnlisted, g_target);
					g_otherUnlistedLogQpc = now; g_otherUnlistedLogged = g_otherUnlisted;
				}
			}
		}
		if (QpcToMs(now - g_lastTargetPresent) < 2000.0) return;
		Log("target %p silent for 2 s - switching to %p", g_target, sc);
		DropTarget("retarget");
	}
	if (!g_target && !AdoptTarget(sc, g_frameIndex ? "after the old target fell silent" : "first presenting swap chain")) return;
	g_lastTargetPresent = now;
	g_presentTid = GetCurrentThreadId();   // issue 3304: the UpdateViews publish must run on this thread
	MaybeFault(2);
	SafeCamOnPresent();
	{   // ticket 0527: the farm builder guard (farm_guard.cpp checks the EXE by patterns and guards its own reads and writes)
		FarmGuardEnv fge = { g_exeBase, (int32_t)g_camValidated, g_camBlk.gridValid, g_camBlk.gridSeq, g_cfg.farmGuard, g_outDir, Log,
		                     g_cfg.farmBuilder ? 0 : 1 };   // ticket 4853: skipBuilder 1 = the builder is never called (the server's fields)
		FarmGuard_OnPresent(fge);
	}
	LsOnPresent(now);    // issue 9039: the lockstep probe's row of this frame (nothing unless lockstep_probe=1)
	SafeTapOnPresent();   // epic 3200, issue 3203: the height-field tap (the copy runs on its own worker thread)
	if (g_cfg.bakeTap)  // epic 3200, issue 3204: the tile-cache tap (bake_tap.cpp guards its own EXE reads)
	{
		BakeTapEnv be = { g_dev, g_ctx, g_hdr, g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.bakeTap, Log };
		BakeTap_OnPresent(be);
	}
	{   // epic 3600, issue 3603: the water mesh tap (water_tap.cpp guards its own EXE reads; water_tap=0 only publishes OFF)
		WaterTapEnv we = { g_dev, g_ctx, g_hdr, g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.waterTap, Log,
			g_camLastStatus == VPCAM_OK ? 1 : 0, g_sceneThisFrame ? 1 : 0,
			!g_tapDead && TapGateFirstOpen() ? 1 : 0 };   // ticket 0408: the height-field tap's first-load rule
		WaterTap_OnPresent(we);
	}
	{   // epic 0167, issue 3703: the Terrain Overlay target tap (overlay_tap.cpp guards its own EXE reads; overlay_tap=0 only publishes OFF)
		OverlayTapEnv oe = { g_dev, g_ctx, g_hdr, g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.overlayTap, Log,
			g_camLastStatus == VPCAM_OK ? 1 : 0, g_sceneThisFrame ? 1 : 0 };
		OverlayTap_OnPresent(oe);
	}
	{   // epic 0166, issue 4102: the decal tap (decal_tap.cpp guards its own EXE reads; decal_tap=0 only publishes OFF)
		DecalTapEnv de = { g_hdr, g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.decalTap, Log };
		DecalTap_OnPresent(de);
	}
	{   // epic 0169, issue 4304: the model shadow map tap (shadow_tap.cpp guards its own EXE reads; shadow_tap=0 only publishes OFF)
		ShadowTapEnv se = { g_dev, g_ctx, g_hdr, g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.shadowTap, Log,
			g_camLastStatus == VPCAM_OK ? 1 : 0, g_sceneThisFrame ? 1 : 0, (uint32_t)g_bbW, (uint32_t)g_bbH };
		ShadowTap_OnPresent(se);
	}
	{   // epic 0169, issue 4307: the ice tap (ice_tap.cpp guards its own EXE reads; ice_tap=0 only publishes OFF)
		IceTapEnv ie = { g_dev, g_ctx, g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.iceTap, Log,
			g_camLastStatus == VPCAM_OK ? 1 : 0, g_hdr };
		IceTap_OnPresent(ie);
	}
	{   // ticket 4553: the cut-mask tap (cut_tap.cpp: its worker guards its own EXE reads; cut_tap=0 only publishes OFF)
		CutTapEnv cte = { g_exeBase, g_camValidated > 0 ? 1u : 0u, g_camBlk.gridValid, g_camBlk.gridSeq, g_cfg.cutTap, Log };
		CutTap_OnPresent(cte);
	}
	if (g_cfg.forestTap)   // ticket 0420: the forest tap (forest_tap.cpp guards its own EXE reads; reads only, a file per load)
	{
		ForestTapEnv fte = { g_exeBase, g_camValidated > 0 ? 1u : 0u, &g_camBlk, g_cfg.forestTap, g_outDir, Log };
		ForestTap_OnPresent(fte);
	}
	{   // ticket 0339: the server's fog mask for the EXE's model draws (fow_models.cpp guards its own EXE reads and writes)
		// the section's camera block: its presentCount is the one the overlay tap stamps its pairs with
		FowModelsEnv fe = { g_dev, g_ctx, g_exeBase, g_camValidated > 0 ? 1u : 0u, g_hdr ? VPSHELL_CAMERA(g_hdr) : &g_camBlk, Log,
			g_camLastStatus == VPCAM_OK ? 1 : 0, g_sceneThisFrame ? 1 : 0 };
		FowModels_OnPresent(fe);
	}
	{   // epic 0040, issue 4752: the unit-draw filter latches the mode word for the draws of the next frame (unit_filter.cpp)
		UnitFilterEnv ue = { g_dev, g_exeBase, (int32_t)g_camValidated, g_hdr, g_cfg.staleMs, g_freq.QuadPart, g_camPresents,
			g_cfg.mode == MODE_COMPOSITE && !g_disabled ? 1 : 0, g_cfg.unitFilter, Log,
			g_cfg.handover == HO_RING ? 1 : 0, g_fs ? VPFRAME_SERVER(g_fs) : nullptr,   // issue 4761: the ring's liveness
			g_cfg.modelFilter, g_cfg.modelTapPassed, g_cfg.modelTapSlotKb, g_swOn[SW_MODELS] ? 1 : 0, g_swOn[SW_UNITS] ? 1 : 0 };   // ticket 4874
		UnitFilter_OnPresent(ue);
	}
	LkTapsDone();   // ticket 0713: the last tap of this Present is written (<base>.Tap)

	if (g_traceLeft > 0) { TraceFrameLists(); g_traceLeft--; }
	if (g_traceRequest) { g_traceLeft = g_traceRequest; InterlockedExchange(&g_traceRequest, 0); }
	InterlockedExchange(&g_traceWanted, (g_traceLeft > 0 || g_traceRequest) ? 1 : 0);

	if (g_prevPresentQpc) Push(g_ft, g_nFt, QpcToMs(now - g_prevPresentQpc));
	PcPresent(now);   // ticket 0688 (reads g_prevPresentQpc)
	g_prevPresentQpc = now;

	if (g_cfg.insert == INS_PRESENT && !g_inserted)
	{
		if (g_cfg.mode == MODE_COMPOSITE && !g_disabled) SafeComposite(GetBbRtv());
		else g_inserted = true;
	}
	if (!g_inserted)
	{
		g_missedIns++;
		if (g_cfg.insert == INS_TAGGED)
		{
			if (g_nFl == 0) g_missNoExec++;
			else if (g_cfg.tagRule == RULE_INLIST) { if (g_sawNotReady) g_missNotReady++; else g_missNoMatch++; }
			else if (!g_cfg.anchor.empty && !g_anchorSeen) g_missNoAnchor++;
			else g_missNoMatch++;
		}
		else g_missNoMatch++;
	}
	// inlist: keep the texture open and the local copy current even in frames without an inserted list, so the next
	// frame's recording threads find content
	if (g_cfg.insert == INS_TAGGED && g_cfg.tagRule == RULE_INLIST && g_cfg.mode == MODE_COMPOSITE && !g_disabled && !g_acquiredThisFrame)
		SafeAcquireOnly();
	if (g_cfg.mode == MODE_COMPOSITE && !g_disabled)
	{
		if (g_inserted && !g_freshThisFrame) g_dups++;
		if (g_inserted && g_haveContent) { g_ageSum += QpcToMs(now - g_contentQpc); g_nAge++; }
		if (g_freshThisFrame) Push(g_lat, g_nLat, QpcToMs(now - g_contentQpc));
	}

	for (auto& q : g_gt)
	{
		if (!q.pending) continue;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 a, b, mid;
		if (g_ctx->GetData(q.dj, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
		if (g_ctx->GetData(q.t0, &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
			g_ctx->GetData(q.t1, &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint && dj.Frequency)
		{
			double k = 1000.0 / (double)dj.Frequency;
			if (q.fresh && g_ctx->GetData(q.tm, &mid, sizeof(mid), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
			{
				Push(g_gpuW, g_nGpuW, (double)(mid - a) * k);
				Push(g_gpuF, g_nGpuF, (double)(b - mid) * k);
			}
			else if (!q.fresh) Push(g_gpu, g_nGpu, (double)(b - a) * k);
		}
		q.pending = false;
	}
	if (g_shotRequest) { InterlockedExchange(&g_shotRequest, 0); t_inComposite = 1; DumpBackBuffer(sc); t_inComposite = 0; }
	// issue 3005: listshot (one dump per frame, after list #k, k = from..to over successive frames)
	if (g_listShotK > 0)
	{
		if (!g_listShotDone)
		{
			Log("listshot: frame %lld has %d lists, fewer than k=%d: final back buffer dumped as k=%d, listshot ends", g_frameIndex, g_execCount, g_listShotK, g_listShotK);
			t_inComposite = 1; DumpBackBuffer(sc, g_listShotK); t_inComposite = 0;
			g_listShotK = 0;
		}
		else if (++g_listShotK > g_listShotTo) { Log("listshot: done (k up to %d)", g_listShotTo); g_listShotK = 0; }
		g_listShotDone = false;
	}
	if (g_listShotRequest) { g_listShotK = g_listShotRequest; InterlockedExchange(&g_listShotRequest, 0); g_listShotDone = false; Log("listshot: k from %d to %d", g_listShotK, g_listShotTo); }

	LkOnPresent(now);   // issue 3304: this frame's lockstep row; the UpdateViews publish of the next frame armed or not
	g_framesInt++;
	g_clearsTotal += g_clearCount;
	g_listsTotal += g_nFl;
	g_clearCount = 0; g_omsetCount = 0; g_omsetBbCount = 0; g_execCount = 0;
	g_inserted = false; g_freshThisFrame = false; g_acquiredThisFrame = false; g_sceneThisFrame = false;
	g_anchorSeen = g_clearSeen = g_3dStarted = g_sawNotReady = false; g_matchCount = 0; g_nFl = 0;
	g_frameIndex++;
	StatsTick(now);
}

static void SafeOnPresent(IDXGISwapChain* sc)
{
	__try { OnPresent(sc); }
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		AsanAfterSeh();
		InterlockedExchange(&g_disabled, 1);
		Log("EXCEPTION 0x%08X in Present hook - composite disabled", GetExceptionCode());
	}
}

// ===========================================================================
// Detour bodies
// ===========================================================================
static HRESULT Do_Present(PFN_Present tr, IDXGISwapChain* sc, UINT sync, UINT flags)
{
	if (t_inPresent || (flags & DXGI_PRESENT_TEST)) return tr(sc, sync, flags);
	t_inPresent = 1;
	SafeOnPresent(sc);
	HRESULT hr = tr(sc, sync, flags);
	if (LsRows() && sc == g_target) g_lsPrevRet = Qpc();
	t_inPresent = 0;
	return hr;
}

static HRESULT Do_Present1(PFN_Present1 tr, IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* pp)
{
	if (t_inPresent || (flags & DXGI_PRESENT_TEST)) return tr(sc, sync, flags, pp);
	t_inPresent = 1;
	SafeOnPresent((IDXGISwapChain*)sc);
	HRESULT hr = tr(sc, sync, flags, pp);
	if (LsRows() && (IDXGISwapChain*)sc == g_target) g_lsPrevRet = Qpc();
	t_inPresent = 0;
	return hr;
}

static void OnResizeBefore(IDXGISwapChain* sc, UINT w, UINT h)
{
	if (sc != g_target) return;
	Log("ResizeBuffers(%u x %u) on target: releasing everything first", w, h);
	ReleaseAll("ResizeBuffers");
}

static void OnResizeAfter(IDXGISwapChain* sc, HRESULT hr)
{
	if (sc != g_target) return;
	RefreshBackBuffer(sc);
	Log("ResizeBuffers returned 0x%08X; back buffer now %ux%u fmt %u samples %u", hr, g_bbW, g_bbH, g_bbFormat, g_bbSamples);
	g_traceLeft = std::max(g_traceLeft, 1);
}

static HRESULT Do_Resize(PFN_ResizeBuffers tr, IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl)
{
	__try { OnResizeBefore(sc, w, h); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); }
	HRESULT hr = tr(sc, n, w, h, f, fl);
	__try { OnResizeAfter(sc, hr); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); }
	return hr;
}

// A swap chain created through the factory becomes the target at its own first Present (decided on the render
// thread, so no state is touched from the creating thread). This replaces the prototype's 2 s silence rule for the
// game's resolution change, which creates a new swap chain and window.
static void OnSwapChainCreated(IDXGISwapChain* sc, const char* how, UINT w, UINT h, HWND hwnd)
{
	LONG seq = InterlockedIncrement(&g_scSeq);   // issue 0141: the chain's creation number (1, 2, ...)
	Log("%s %ux%u hwnd %p -> sc %p creation #%ld%s", how, w, h, hwnd, sc, seq, g_cfg.adoptOnCreate ? " (adopted at its first Present if it is the target's window or none; issue 3017)" : " (adopt_on_create=0: 2 s silence rule)");
	if (g_cfg.adoptOnCreate)
	{
		InterlockedExchange(&g_pendingSeq, seq);   // the number first, then the pointer (OnPresent reads them the other way)
		InterlockedExchangePointer((PVOID volatile*)&g_pendingAdopt, sc);
	}
	if (!g_scCreatedTick) InterlockedExchange(&g_scCreatedTick, (LONG)std::max<DWORD>(1, GetTickCount()));   // issue 3018 V9
}

static HRESULT Do_CreateSc(PFN_CreateSwapChain tr, IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* d, IDXGISwapChain** out)
{
	t_inCreateSc++;
	HRESULT hr = tr(f, dev, d, out);
	t_inCreateSc--;
	if (SUCCEEDED(hr) && t_inCreateSc == 0 && out && *out && !t_inComposite)
	{
		__try { OnSwapChainCreated(*out, "CreateSwapChain", d ? d->BufferDesc.Width : 0, d ? d->BufferDesc.Height : 0, d ? d->OutputWindow : nullptr); }
		__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); }
	}
	return hr;
}

static HRESULT Do_CreateScHwnd(PFN_CreateSwapChainForHwnd tr, IDXGIFactory2* f, IUnknown* dev, HWND w, const DXGI_SWAP_CHAIN_DESC1* d,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* o, IDXGISwapChain1** out)
{
	t_inCreateSc++;
	HRESULT hr = tr(f, dev, w, d, fs, o, out);
	t_inCreateSc--;
	if (SUCCEEDED(hr) && t_inCreateSc == 0 && out && *out && !t_inComposite)
	{
		__try { OnSwapChainCreated((IDXGISwapChain*)*out, "CreateSwapChainForHwnd", d ? d->Width : 0, d ? d->Height : 0, w); }
		__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); }
	}
	return hr;
}

static void OnDeferredCreated(ID3D11DeviceContext* c)
{
	CtxRec* r = FindCtx(c);
	if (r) memset(&r->cur, 0, sizeof(r->cur)); // a reused address: start clean
	UnitFilter_OnStateReset(c);                // issue 4752: likewise the unit filter's tracked shaders
	t_inComposite = 1;
	DiscoverContext(c);
	CommitHooks("CreateDeferredContext");
	t_inComposite = 0;
}

static HRESULT Do_CreateDeferred(PFN_CreateDeferred tr, ID3D11Device* dev, UINT flags, ID3D11DeviceContext** out)
{
	HRESULT hr = tr(dev, flags, out);
	if (SUCCEEDED(hr) && out && *out && !t_inComposite)
	{
		__try { OnDeferredCreated(*out); }
		__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION in CreateDeferredContext hook"); }
	}
	return hr;
}

// ---- immediate-context handlers (the prototype's insertion modes) ----
static void TraceRtv(const char* what, int idx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv)
{
	UINT w = 0, h = 0, s = 0; DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
	if (rtv) RtvSize(rtv, w, h, f, s);
	ID3D11Resource* r = nullptr; if (rtv) rtv->GetResource(&r);
	Log("trace:   %s #%d rtv %p res %p %ux%u viewfmt %u samples %u dsv %p%s", what, idx, rtv, r, w, h, f, s, dsv, (r && r == g_bbRes) ? "  <== BACK BUFFER" : "");
	SafeRelease(r);
}

static void OnClear(ID3D11RenderTargetView* rtv)
{
	g_clearCount++;
	if (g_traceLeft > 0) TraceRtv("clear", g_clearCount, rtv, nullptr);
	if (g_inserted) return;
	bool hit = false;
	if (g_cfg.insert == INS_CLEAR) hit = IsBackBuffer(rtv);
	else if (g_cfg.insert == INS_NTHCLEAR) hit = g_clearCount == g_cfg.nth;
	if (!hit) return;
	if (g_cfg.mode == MODE_COMPOSITE) SafeComposite(rtv);
	else g_inserted = true;
}

static void OnOMSet(UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
	g_omsetCount++;
	ID3D11RenderTargetView* r0 = (n && rtvs) ? rtvs[0] : nullptr;
	if (g_traceLeft > 0) TraceRtv("omset", g_omsetCount, r0, dsv);
	bool bb = (g_traceLeft > 0 || g_cfg.insert == INS_OMSET) && IsBackBuffer(r0);
	if (bb) g_omsetBbCount++;
	if (g_cfg.insert != INS_OMSET || !bb) return;
	if (g_inserted || g_omsetBbCount != g_cfg.nth) return;
	if (g_cfg.mode == MODE_COMPOSITE) SafeComposite(r0);
	else g_inserted = true;
}

// scene: the multisampled scene target the list cleared (ticket 0804), else the back buffer
static void InsertImmediate(ID3D11RenderTargetView* scene = nullptr)
{
	if (g_cfg.depthProbe && g_ctx) DepthProbeState(g_ctx, 0, true);   // issue 3208: the immediate context at the insertion
	if (g_cfg.mode == MODE_COMPOSITE) SafeComposite(scene ? scene : GetBbRtv());
	else g_inserted = true;
}

static bool InWindow() { return g_clearSeen && !g_3dStarted; }

// Before the original ExecuteCommandList on the immediate context. Returns the frame-list entry.
static FrameList* OnExecBefore(ID3D11CommandList* cl, const Tags& t, bool known)
{
	FrameList* f = g_nFl < (int)_countof(g_fl) ? &g_fl[g_nFl++] : nullptr;
	if (f) { f->cl = cl; f->bits = t.bits; f->ctxIdx = known ? t.ctxIdx : -1; f->rt0 = t.nRt ? t.rt[0] : -1; f->ds0 = t.nDs ? t.ds[0] : -1; f->known = known; f->ev = 0; f->wrong = false;
		f->nClears = t.nClears; f->nBinds = t.nBinds; f->nRt = t.nRt; f->nDs = t.nDs; memcpy(f->rt, t.rt, sizeof(f->rt)); memcpy(f->ds, t.ds, sizeof(f->ds));
		f->rtW = t.rtW; f->rtH = t.rtH; f->dsW = t.dsW; f->dsH = t.dsH; }
	if (!known) g_untagged++;
	if (g_cfg.insert != INS_TAGGED || g_inserted) return f;
	if (g_cfg.tagRule == RULE_BEFORE)
	{
		if ((g_cfg.anchor.empty || g_anchorSeen) && Matches(g_cfg.match, t) && ++g_matchCount == g_cfg.tagNth)
		{
			bool ok = InWindow();
			g_insChecked++; if (!ok) g_insWrong++;
			if (f) { f->ev = 'B'; f->wrong = !ok; }
			InsertImmediate();
		}
	}
	else if (g_cfg.tagRule == RULE_INLIST)
	{
		if (t.bits & TAG_NOTREADY) g_sawNotReady = true;
		if (t.bits & (TAG_INSERTED | TAG_WOULD))
		{
			if (g_cfg.mode == MODE_COMPOSITE && !g_disabled && !g_acquiredThisFrame) SafeAcquireOnly();
			g_inserted = true;
			g_insChecked++;
			if (f) f->ev = 'I';
		}
	}
	return f;
}

// After the original ExecuteCommandList on the immediate context.
static void OnExecAfter(FrameList* f, const Tags& t)
{
	g_execCount++;
	MaybeFault(3);
	if (g_listShotK > 0 && g_execCount == g_listShotK && !g_listShotDone && g_target)
	{
		t_inComposite = 1; DumpBackBuffer(g_target, g_listShotK); t_inComposite = 0;
		g_listShotDone = true;
	}
	if (g_traceLeft > 0 && !f) Log("trace:   ExecuteCommandList #%d (frame list full)", g_execCount);
	// issue 3203: a frame of the 3D map (the scene clear: back buffer and depth cleared in one list); the load screen has none
	if ((t.bits & (TAG_CLEAR_BB | TAG_CLEAR_DSV)) == (TAG_CLEAR_BB | TAG_CLEAR_DSV)) g_sceneThisFrame = true;
	// the clear..3D window, from this list's tags
	if (t.bits & TAG_CLEAR_BB) { g_clearSeen = true; g_3dStarted = false; }
	else if (g_clearSeen && (t.bits & (TAG_BIND_BB | TAG_BIND_BB_DSV))) g_3dStarted = true;
	if (!g_inserted)
	{
		if (g_cfg.insert == INS_TAGGED && g_cfg.tagRule == RULE_AFTER)
		{
			if ((g_cfg.anchor.empty || g_anchorSeen) && Matches(g_cfg.match, t) && ++g_matchCount == g_cfg.tagNth)
			{
				bool ok = InWindow();
				g_insChecked++; if (!ok) g_insWrong++;
				if (f) { f->ev = 'A'; f->wrong = !ok; }
				g_insDsv = t.clearDsv;   // issue 3304: this list's depth clear, alive while the list executes
				InsertImmediate(t.clearScene);   // ticket 0804: into the multisampled scene target when the list cleared one
				g_insDsv = nullptr;
			}
		}
		else if (g_cfg.insert == INS_EXEC && g_execCount == g_cfg.nth)
		{
			bool ok = InWindow();
			g_insChecked++; if (!ok) g_insWrong++;
			if (f) { f->ev = 'E'; f->wrong = !ok; }
			InsertImmediate();
		}
	}
	if (!g_cfg.anchor.empty && Matches(g_cfg.anchor, t)) g_anchorSeen = true;
}

// ---- context detours ----
static void Do_ClearRTV(PFN_ClearRTV tr, ID3D11DeviceContext* c, ID3D11RenderTargetView* rtv, const FLOAT col[4])
{
	tr(c, rtv, col);
	if (t_inComposite) return;
	__try { if (c == g_ctx) OnClear(rtv); else OnDefClear(c, rtv); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in ClearRenderTargetView hook - composite disabled", GetExceptionCode()); }
}

static void Do_ClearDSV(PFN_ClearDSV tr, ID3D11DeviceContext* c, ID3D11DepthStencilView* dsv, UINT fl, FLOAT d, UINT8 s)
{
	tr(c, dsv, fl, d, s);
	if (t_inComposite || c == g_ctx) return;
	__try { OnDefClearDsv(c, dsv, fl); if (g_cfg.depthProbe) DepthProbeClear(dsv, fl, d, s); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in ClearDepthStencilView hook - composite disabled", GetExceptionCode()); }
}

static void Do_OMSet(PFN_OMSetRT tr, ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv)
{
	tr(c, n, rtvs, dsv);
	if (t_inComposite) return;
	__try
	{
		if (c == g_ctx) OnOMSet(n, rtvs, dsv); else OnDefOMSet(c, n, rtvs, dsv);
		if (g_cfg.bakeTap && c == g_ctx) BakeTap_OnOMSet(c, n, rtvs, dsv);   // epic 3200, issue 3204
		if (g_cfg.overlayTap) OverlayTap_OnOMSet(c, n, rtvs);   // epic 0167, issue 3703 (verify mode only; one flag test otherwise)
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in OMSetRenderTargets hook - composite disabled", GetExceptionCode()); }
}

static HRESULT Do_Finish(PFN_Finish tr, ID3D11DeviceContext* c, BOOL restore, ID3D11CommandList** out)
{
	if (g_cfg.depthProbe && !t_inComposite)   // issue 3208: the list's last depth state, before Finish resets it
	{
		__try { InterlockedIncrement(&g_depthFinishRestore[restore ? 1 : 0]); DepthProbeState(c, 0, false); }
		__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in the depth probe (FinishCommandList) - composite disabled", GetExceptionCode()); }
	}
	HRESULT hr = tr(c, restore, out);
	if (!restore) UnitFilter_OnStateReset(c);   // issue 4752: the deferred context's state is now the default (no shaders)
	if (SUCCEEDED(hr) && out && *out) UnitFilter_OnFinishList(c, *out);   // ticket 4874: the context's model records go with the list
	if (t_inComposite || FAILED(hr) || !out || !*out) return hr;
	__try { OnFinish(c, *out); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in FinishCommandList hook - composite disabled", GetExceptionCode()); }
	return hr;
}

static void SafeFowFrameStart(ID3D11DeviceContext* c)
{
	__try { t_inComposite = 1; FowModels_OnFrameStart(c); t_inComposite = 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { t_inComposite = 0; AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in the model fog's frame start - composite disabled", GetExceptionCode()); }
}

static FrameList* SafeExecBefore(ID3D11CommandList* cl, Tags& t)
{
	FrameList* f = nullptr;
	__try { bool known = LookupTags(cl, t, !g_cfg.tagKeep); f = OnExecBefore(cl, t, known); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in ExecuteCommandList hook (before) - composite disabled", GetExceptionCode()); }
	return f;
}

static void Do_ExecInner(PFN_ExecCL tr, ID3D11DeviceContext* c, ID3D11CommandList* cl, BOOL restore);
static void Do_Exec(PFN_ExecCL tr, ID3D11DeviceContext* c, ID3D11CommandList* cl, BOOL restore)
{
	UnitFilter_OnExecuteList(c, cl);            // ticket 4874: the list's model records take their place in the frame (before it runs)
	Do_ExecInner(tr, c, cl, restore);
	if (!restore) UnitFilter_OnStateReset(c);   // issue 4752: the context's state is now the default (no shaders)
}

static void Do_ExecInner(PFN_ExecCL tr, ID3D11DeviceContext* c, ID3D11CommandList* cl, BOOL restore)
{
	if (t_inComposite) { tr(c, cl, restore); return; }
	if (c != g_ctx)
	{
		tr(c, cl, restore);
		__try { OnDefExec(c, cl); }
		__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); }
		return;
	}
	Tags t; memset(&t, 0, sizeof(t));
	if (g_cfg.depthProbe) InterlockedIncrement(&g_depthExecRestore[restore ? 1 : 0]);
	if (g_execCount == 0 && !g_disabled) SafeFowFrameStart(c);   // ticket 0394: the model fog's mask moved to this frame's window
	FrameList* f = SafeExecBefore(cl, t);
	int64_t ls0 = LsRows() ? Qpc() : 0;
	tr(c, cl, restore);
	int64_t ls1 = LsRows() ? Qpc() : 0;
	__try { OnExecAfter(f, t); }
	__except (EXCEPTION_EXECUTE_HANDLER) { AsanAfterSeh(); InterlockedExchange(&g_disabled, 1); Log("EXCEPTION 0x%08X in ExecuteCommandList hook (after) - composite disabled", GetExceptionCode()); }
	if (ls0) LsOnExec(t.bits, ls0, ls1, Qpc());
}

// ===========================================================================
// Discovery with a dummy device on hidden windows (control thread)
// ===========================================================================
static LRESULT CALLBACK DummyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static bool DiscoverWithDummy()
{
	WNDCLASSW wc = {}; wc.lpfnWndProc = DummyWndProc; wc.hInstance = g_module; wc.lpszClassName = L"VPShellCompositeDummy";
	RegisterClassW(&wc);
	HWND w1 = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 16, 16, nullptr, nullptr, g_module, nullptr);
	HWND w2 = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 16, 16, nullptr, nullptr, g_module, nullptr);
	bool ok = false;
	t_inComposite = 1; // our own calls pass straight through any hook enabled meanwhile
	if (w1 && w2)
	{
		DXGI_SWAP_CHAIN_DESC scd = {};
		scd.BufferDesc.Width = 16; scd.BufferDesc.Height = 16; scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.SampleDesc.Count = 1; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.BufferCount = 1;
		scd.OutputWindow = w1; scd.Windowed = TRUE; scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
		HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, nullptr, &ctx);
		if (FAILED(hr))
			hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, nullptr, &ctx);
		if (SUCCEEDED(hr))
		{
			ok = true;
			DiscoverSwapChain(sc);
			IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f2 = nullptr;
			if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && SUCCEEDED(xd->GetAdapter(&ad)) &&
				SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2)))
			{
				DiscoverFactory(f2);
				DXGI_SWAP_CHAIN_DESC1 d1 = {};
				d1.Width = 16; d1.Height = 16; d1.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d1.SampleDesc.Count = 1;
				d1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d1.BufferCount = 2; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				IDXGISwapChain1* sc1 = nullptr;
				if (SUCCEEDED(f2->CreateSwapChainForHwnd(dev, w2, &d1, nullptr, nullptr, &sc1))) { DiscoverSwapChain(sc1); sc1->Release(); }
			}
			IDXGIFactory* f0 = nullptr;
			if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&f0))) { DiscoverFactory(f0); f0->Release(); }
			SafeRelease(f2); SafeRelease(ad); SafeRelease(xd);
			DiscoverDevice(dev);
			CommitHooks("dummy device");
			sc->Release(); ctx->Release(); dev->Release();
		}
		else Log("dummy D3D11CreateDeviceAndSwapChain failed 0x%08X", hr);
	}
	t_inComposite = 0;
	if (w1) DestroyWindow(w1);
	if (w2) DestroyWindow(w2);
	UnregisterClassW(wc.lpszClassName, g_module);
	Log("dummy-device discovery %s: %d hook targets", ok ? "done" : "FAILED", g_nTg);
	return ok;
}

// ===========================================================================
// Control thread
// ===========================================================================
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID);   // hook_test=foreign tries to hook it

static FILETIME IniTime()
{
	WIN32_FILE_ATTRIBUTE_DATA a = {};
	GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &a);
	return a.ftLastWriteTime;
}

static bool TakeTrigger(const wchar_t* name, int* value)
{
	wchar_t p[MAX_PATH]; _snwprintf_s(p, _TRUNCATE, L"%s%s", g_dir, name);
	if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return false;
	if (value) *value = GetPrivateProfileIntW(L"trace", L"frames", 2, p);
	DeleteFileW(p);
	return true;
}

static void LogConfig(const char* what, const Config& c)
{
	char sw[200] = "";
	for (int s = 0; s < SW_COUNT; ++s) { char one[40]; _snprintf_s(one, _TRUNCATE, "%shook_%s=%d", s ? " " : "", kSwNames[s], c.hookSw[s]); strcat_s(sw, one); }
	Log("%s: mode=%s insert=%s nth=%d rule=%s match='%s'%s anchor='%s'%s tag_nth=%d tag_keep=%d adopt_on_create=%d timeout=%u blend=%s hide_when_stale=%d camera=%d camera_test=%s "
		"tap=%d tap_test=%s tap_test_load=%d tap_ack_timeout_ms=%u tap_delay_frames=%d tap_scene_frames=%d tap_first_load=%d tap_first_delay_frames=%d tap_byte_layer=%d tap_chunk_kb=%d depth_probe=%d lockstep_probe=%d section=%ls %s",
		what, kModeNames[c.mode], kInsNames[c.insert], c.nth, kRuleNames[c.tagRule], c.match.text, c.match.impossible ? " (UNKNOWN TOKEN: matches nothing)" : "",
		c.anchor.text, c.anchor.impossible ? " (UNKNOWN TOKEN: matches nothing)" : "", c.tagNth, c.tagKeep, c.adoptOnCreate, c.timeoutMs,
		c.alphaBlend ? "alpha" : "opaque", c.hideWhenStale, c.camera, c.cameraTest == 1 ? "stall" : c.cameraTest == 2 ? "corrupt" : c.cameraTest == 3 ? "late" : "-",
		c.tap, kTapTestNames[c.tapTest], c.tapTestLoad, c.tapAckTimeoutMs, c.tapDelayFrames, c.tapSceneFrames, c.tapFirstLoad, c.tapFirstDelayFrames, c.tapByteLayer, c.tapChunkKb, c.depthProbe, c.lockstepProbe, c.section, sw);
	// issue 3014: an ini timeout that was not used as given
	if (c.timeoutNote == 1) Log("%s: CONFIG ERROR timeout_ms='%ls' is not a number - 0 used (no wait)", what, c.timeoutText);
	if (c.timeoutNote == 2) Log("%s: CONFIG ERROR timeout_ms='%ls' is outside 0..%u ms - clamped to %u", what, c.timeoutText, kMaxTimeoutMs, c.timeoutMs);
	// issue 3016: a tag rule that is present but unusable matches nothing
	if (c.match.bad[0]) Log("%s: CONFIG ERROR tag_match: %s - the rule matches nothing", what, c.match.bad);
	if (c.anchor.bad[0]) Log("%s: CONFIG ERROR tag_anchor: %s - the anchor never comes (nothing inserted)", what, c.anchor.bad);
	// issue 4752: the unit-draw filter's switch
	if (c.unitFilterNote) Log("%s: CONFIG ERROR unit_filter='%ls' is none of auto, on, off - auto used (the mode word decides)", what, c.unitFilterText);
	if (c.unitFilter != UF_MODE_AUTO)
		Log("%s: unit_filter=%s - TEST ONLY (%s); hook_units=%d", what, kUnitFilterModeNames[c.unitFilter],
			c.unitFilter == UF_MODE_ON ? "the EXE's pass-0 unit draws are dropped whatever the mode word says" : "nothing is dropped whatever the mode word says",
			c.hookSw[SW_UNITS]);
	// ticket 4874: the model filter's keys
	if (c.modelFilterNote) Log("%s: CONFIG ERROR model_filter='%ls' is none of auto, on, off - auto used (the mode word decides)", what, c.modelFilterText);
	if (c.modelTapSlotNote)
		Log("%s: CONFIG ERROR model_tap_slot_kb='%ls' is not a multiple of 64 in %u..%u - %u used", what, c.modelTapSlotText,
		    VPSHELL_MODELTAP_SLOT_KB_MIN, VPSHELL_MODELTAP_SLOT_KB_MAX, VPSHELL_MODELTAP_SLOT_KB_DEFAULT);
	if (c.modelFilter != UF_MODE_AUTO)
		Log("%s: model_filter=%s - TEST ONLY (%s); hook_models=%d", what, kUnitFilterModeNames[c.modelFilter],
			c.modelFilter == UF_MODE_ON ? "the city and landmark colour draws in the identity set are dropped whatever the mode word says"
			                            : "no city or landmark draw is dropped whatever the mode word says", c.hookSw[SW_MODELS]);
	if (c.modelTapPassed) Log("%s: model_tap_passed=1 - TEST ONLY: the passed city and landmark draws are tapped as well", what);
	// issue 3304
	Log("%s: lockstep=%d lockstep_bound_us=%u lockstep_giveup=%d handover=%s depth_write=%d lockstep_csv=%d lockstep_check=%d ring_slots=%d ring_sync=%s",
		what, c.lockstep, c.boundUs, c.giveup, kHandoverNames[c.handover], c.depthWrite, c.lockstepCsv, c.lockstepCheck, c.ringSlots,
		c.ringSync == VPSYNC_CPU ? "cpu" : "fence");
	if (c.boundNote == 1) Log("%s: CONFIG ERROR lockstep_bound_us='%ls' is not a number - the default %u us used", what, c.boundText, c.boundUs);
	if (c.boundNote == 2) Log("%s: CONFIG ERROR lockstep_bound_us='%ls' is outside 0..%u us - clamped to %u", what, c.boundText, VPFRAME_BOUND_US_MAX, c.boundUs);
	if (c.giveupNote == 1) Log("%s: CONFIG ERROR lockstep_giveup='%ls' is not a number - the default %d used", what, c.giveupText, c.giveup);
	if (c.giveupNote == 2) Log("%s: CONFIG ERROR lockstep_giveup='%ls' is outside 1..1000 - clamped to %d", what, c.giveupText, c.giveup);
	Log("%s: lockstep_mode=%s lockstep_hung_ms=%d hung_sink_test=%d (ticket 0691: %s)", what, c.lockstepExact ? "exact" : "bounded", c.hungMs, c.hungSinkTest,
		c.lockstepExact ? "the insertion waits for its own frame with no bound; only a dead or hung server ends the wait" : "TEST / A-B ONLY: lockstep_bound_us and lockstep_giveup in force");
	if (c.modeNote) Log("%s: CONFIG ERROR lockstep_mode='%ls' is neither exact nor bounded - exact used", what, c.modeText);
	if (c.hungNote == 1) Log("%s: CONFIG ERROR lockstep_hung_ms='%ls' is not a number - the default %d used", what, c.hungText, c.hungMs);
	if (c.hungNote == 2) Log("%s: CONFIG ERROR lockstep_hung_ms='%ls' is outside %u..%u - clamped to %d", what, c.hungText, VPFRAME_HUNG_MS_MIN, VPFRAME_HUNG_MS_MAX, c.hungMs);
	if (c.handoverNote) Log("%s: CONFIG ERROR handover='%ls' is neither v3 nor ring - v3 used", what, c.handoverText);
	if (c.ringSlotsNote == 1) Log("%s: CONFIG ERROR ring_slots='%ls' is not a number - the default %d used", what, c.ringSlotsText, c.ringSlots);
	if (c.ringSlotsNote == 2) Log("%s: CONFIG ERROR ring_slots='%ls' is outside %u..%u - clamped to %d", what, c.ringSlotsText, VPFRAME_MIN_SLOTS, VPFRAME_MAX_SLOTS, c.ringSlots);
	if (c.ringSyncNote) Log("%s: CONFIG ERROR ring_sync='%ls' is neither fence nor cpu - fence used", what, c.ringSyncText);
	if ((c.lockstep || c.handover == HO_RING || c.depthWrite) && c.insert == INS_TAGGED && c.tagRule == RULE_INLIST)
		Log("%s: CONFIG ERROR lockstep, handover=ring and depth_write need the insertion on the immediate context; tag_rule=inlist keeps the "
			"version-3 path without them", what);
}

// ===========================================================================
// Ticket 0490: the values used, the ini against the built-in defaults, a missing or unreadable ini
// ===========================================================================
struct CfgKV { char key[40]; char val[200]; };
static const int kMaxCfgKV = 96;

static void CfgAdd(CfgKV* kv, int& n, const char* key, const char* fmt, ...)
{
	if (n >= kMaxCfgKV) return;
	strcpy_s(kv[n].key, key);
	char tmp[200];
	va_list ap; va_start(ap, fmt); _vsnprintf_s(tmp, _TRUNCATE, fmt, ap); va_end(ap);
	if (strchr(tmp, ' ')) _snprintf_s(kv[n].val, _TRUNCATE, "\"%s\"", tmp); else strcpy_s(kv[n].val, tmp);
	++n;
}

// Every key ReadConfig reads from [composite], in the shipped ini's order, with the
// value used, spelled as the ini spells it. A value holding a space is quoted ("...").
static int ConfigValues(const Config& c, CfgKV* kv)
{
	int n = 0;
#define add(...) CfgAdd(kv, n, __VA_ARGS__)
	static const char* kCamTest[] = { "", "stall", "corrupt", "late" };
	static const char* kFault[] = { "", "composite", "present", "exec", "uv" };
	add("mode", "%s", kModeNames[c.mode]);
	add("insert", "%s", kInsNames[c.insert]);
	add("nth", "%d", c.nth);
	add("tag_rule", "%s", kRuleNames[c.tagRule]);
	add("tag_match", "%s", c.match.text);
	add("tag_anchor", "%s", c.anchor.text);
	add("tag_nth", "%d", c.tagNth);
	add("tag_keep", "%d", c.tagKeep);
	add("adopt_on_create", "%d", c.adoptOnCreate);
	for (int s = 0; s < SW_COUNT; ++s) { char k[40]; _snprintf_s(k, _TRUNCATE, "hook_%s", kSwNames[s]); add(k, "%d", c.hookSw[s]); }
	add("timeout_ms", "%u", c.timeoutMs);
	add("blend", "%s", c.alphaBlend ? "alpha" : "opaque");
	add("hide_when_stale", "%d", c.hideWhenStale);
	add("stale_ms", "%d", c.staleMs);
	add("request_resize", "%d", c.requestResize);
	add("stats_ms", "%d", c.statsMs);
	add("trace_frames", "%d", c.traceFrames);
	add("camera", "%d", c.camera);
	add("camera_test", "%s", kCamTest[c.cameraTest >= 0 && c.cameraTest <= 3 ? c.cameraTest : 0]);
	add("camera_test_late", "%d", c.cameraTestLate);
	add("tap", "%d", c.tap);
	add("tap_ack_timeout_ms", "%u", c.tapAckTimeoutMs);
	add("tap_delay_frames", "%d", c.tapDelayFrames);
	add("tap_scene_frames", "%d", c.tapSceneFrames);
	add("tap_first_load", "%d", c.tapFirstLoad);
	add("tap_first_delay_frames", "%d", c.tapFirstDelayFrames);
	add("tap_byte_layer", "%d", c.tapByteLayer);
	add("tap_chunk_kb", "%d", c.tapChunkKb);
	add("tap_test", "%s", c.tapTest > TAPT_NONE && c.tapTest < TAPT_COUNT ? kTapTestNames[c.tapTest] : "");
	add("tap_test_load", "%d", c.tapTestLoad);
	add("depth_probe", "%d", c.depthProbe);
	add("lockstep_probe", "%d", c.lockstepProbe);
	add("lockstep", "%d", c.lockstep);
	add("lockstep_bound_us", "%u", c.boundUs);
	add("lockstep_giveup", "%d", c.giveup);
	add("lockstep_mode", "%s", c.lockstepExact ? "exact" : "bounded");   // ticket 0691
	add("lockstep_hung_ms", "%d", c.hungMs);
	add("hung_sink_test", "%d", c.hungSinkTest);
	add("handover", "%s", kHandoverNames[c.handover == HO_RING ? HO_RING : HO_V3]);
	add("depth_write", "%d", c.depthWrite);
	add("lockstep_csv", "%d", c.lockstepCsv);
	add("lockstep_check", "%d", c.lockstepCheck);
	add("pacing", "%d", c.pacing);   // ticket 0688
	add("ring_slots", "%d", c.ringSlots);
	add("ring_sync", "%s", c.ringSync == VPSYNC_CPU ? "cpu" : "fence");
	add("section", "%ls", c.section);
	add("shot_dir", "%ls", c.shotDir);
	add("overlay_tap", "%d", c.overlayTap);
	add("decal_tap", "%d", c.decalTap);
	add("shadow_tap", "%d", c.shadowTap);
	add("ice_tap", "%d", c.iceTap);
	add("forest_tap", "%d", c.forestTap);
	add("model_fow", "%d", c.modelFow);
	// at the end of the shipped ini since 0490 (MC: every key read is written there; tag_anchor alone stays out)
	add("bake_tap", "%d", c.bakeTap);
	add("water_tap", "%d", c.waterTap);
	add("cut_tap", "%d", c.cutTap);
	add("farm_guard", "%d", c.farmGuard);   // ticket 0527
	add("farm_builder", "%d", c.farmBuilder);   // ticket 4853
	add("fault_test", "%s", kFault[c.faultTest >= 0 && c.faultTest <= 4 ? c.faultTest : 0]);
	add("hook_test", "%s", c.hookTest ? "foreign" : "");
	add("unit_filter", "%s", kUnitFilterModeNames[c.unitFilter >= 0 && c.unitFilter <= 2 ? c.unitFilter : 0]);   // issue 4752
	add("model_filter", "%s", kUnitFilterModeNames[c.modelFilter >= 0 && c.modelFilter <= 2 ? c.modelFilter : 0]);   // ticket 4874
	add("model_tap_passed", "%d", c.modelTapPassed);
	add("model_tap_slot_kb", "%d", c.modelTapSlotKb);
#undef add
	return n;
}

// "<what>: values used i/N: key=value key=value ..." in lines of at most ~1500 characters (Log cuts at 2048).
static void LogValues(const char* what, const Config& c)
{
	static CfgKV kv[kMaxCfgKV];   // the control thread only
	const int n = ConfigValues(c, kv);
	static char chunks[8][1600];
	int nc = 0;
	chunks[0][0] = 0;
	for (int i = 0; i < n && nc < 8; ++i)
	{
		char one[260]; _snprintf_s(one, _TRUNCATE, "%s%s=%s", chunks[nc][0] ? " " : "", kv[i].key, kv[i].val);
		if (strlen(chunks[nc]) + strlen(one) >= 1500 && chunks[nc][0])
		{
			if (++nc >= 8) break;
			chunks[nc][0] = 0;
			_snprintf_s(one, _TRUNCATE, "%s=%s", kv[i].key, kv[i].val);
		}
		strcat_s(chunks[nc], one);
	}
	const int total = nc < 8 ? nc + 1 : 8;
	for (int i = 0; i < total; ++i) Log("%s: values used %d/%d (%d keys): %s", what, i + 1, total, n, chunks[i]);
}

enum { INI_OK = 0, INI_MISSING = 1, INI_UNREADABLE = 2, INI_NOSECTION = 3 };
static int g_iniStateLogged = -1;   // the last state named in the log (the control thread only)

static int IniState(DWORD* err)
{
	*err = 0;
	HANDLE h = CreateFileW(g_iniPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
	{
		*err = GetLastError();
		return *err == ERROR_FILE_NOT_FOUND || *err == ERROR_PATH_NOT_FOUND ? INI_MISSING : INI_UNREADABLE;
	}
	CloseHandle(h);
	static wchar_t names[8192];
	const DWORD got = GetPrivateProfileSectionNamesW(names, _countof(names), g_iniPath);
	for (const wchar_t* p = names; got && *p; p += wcslen(p) + 1)
		if (!_wcsicmp(p, L"composite")) return INI_OK;
	return INI_NOSECTION;
}

// After every read of the configuration (at load and at each change): a missing, unreadable or sectionless ini is named
// once per state (not again until the state changes), else the keys where the ini differs from the built-in defaults;
// then every value used.
static void LogConfigSource(const char* what, const Config& c, int iniState, DWORD iniErr)
{
	if (iniState != INI_OK)
	{
		if (iniState != g_iniStateLogged)
		{
			const char* why = iniState == INI_MISSING ? "is missing" : iniState == INI_UNREADABLE ? "cannot be read" : "has no [composite] section";
			Log("%s: CONFIG ERROR %ls %s (error %lu): every key not set by a VPSHELL_<KEY> variable takes the built-in default "
				"(ticket 0490: the defaults are the shipped ini's); the values used follow", what, g_iniPath, why, iniErr);
		}
	}
	else
	{
		if (g_iniStateLogged > INI_OK) Log("%s: %ls is readable again", what, g_iniPath);
		static Config fileOnly, defs;   // the control thread only
		static CfgKV a[kMaxCfgKV], b[kMaxCfgKV];
		ReadConfig(fileOnly, CFG_INI);
		ReadConfig(defs, CFG_NONE);
		const int na = ConfigValues(fileOnly, a), nb = ConfigValues(defs, b);
		char diff[1700] = "";
		int nd = 0;
		for (int i = 0; i < na && i < nb; ++i)
			if (strcmp(a[i].val, b[i].val))
			{
				char one[460]; _snprintf_s(one, _TRUNCATE, "%s%s=%s (default %s)", nd ? " " : "", a[i].key, a[i].val, b[i].val);
				if (strlen(diff) + strlen(one) < sizeof(diff) - 8) strcat_s(diff, one); else if (!strstr(diff, " ...")) strcat_s(diff, " ...");
				++nd;
			}
		if (nd) Log("%s: the ini differs from the built-in defaults in %d of %d keys: %s", what, nd, na, diff);
		else Log("%s: the ini's values equal the built-in defaults (%d keys compared; ticket 0490)", what, na);
	}
	g_iniStateLogged = iniState;
	LogValues(what, c);
}

static DWORD WINAPI ControlThread(LPVOID)
{
	// The ini's time is taken before the first read (issue 3007 fix, 2026-11-11): taken after the hooks were installed
	// (as before), an ini change made during the ~200 ms of hook discovery was never applied.
	FILETIME last = IniTime();
	DWORD iniErr = 0;
	int iniState = IniState(&iniErr);   // ticket 0490
	Config c; ReadConfig(c);
	EnterCriticalSection(&g_cfgLock); g_cfgShared = c; LeaveCriticalSection(&g_cfgLock);
	g_cfg = c; g_cfgApplied = 0; g_cfgVersion = 0;
	wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
	Log("==== shell_composite.dll (inline hooks, MinHook %s) loaded into pid %u '%ls' (DLL dir %ls)", "1.3.4", GetCurrentProcessId(), exe, g_dir);
	Log("output folder %ls (%s)", g_outDir, g_outNote);
	if (g_logNote[0]) Log("log: %ls", g_logNote);
	if (g_keepNote[0]) Log("log: %ls", g_keepNote);   // ticket 0747
	LogConfig("config", c);
	LogConfigSource("config", c, iniState, iniErr);   // ticket 0490
	CamValidate();
	UnitFilter_ControlTick(Log);   // issue 4752: the filter's log before its first hook can fire
	MH_STATUS st = MH_Initialize();
	g_mhOk = st == MH_OK;
	if (!g_mhOk) Log("MH_Initialize failed: %s - staying inert", MH_StatusToString(st));
	for (int s = 0; s < SW_COUNT; ++s) g_swOn[s] = c.mode != MODE_OFF && c.hookSw[s];
	if (g_mhOk && c.mode != MODE_OFF) { DiscoverWithDummy(); InterlockedExchange(&g_discovered, 1); ApplySwitches(c); }
	LsApply(c, g_mhOk);   // issue 9039
	if (g_mhOk && c.hookTest)
	{
		// hook_test=foreign (issue 3013, tests only): the D4 guard of HookTarget must refuse every target outside
		// d3d11.dll / dxgi.dll. Tried: the host EXE's entry point, this DLL's DllMain, a function of kernel32.dll and a heap
		// block. Each address and its first bytes are logged first, so the test can read them back from the process.
		const unsigned char* exe = (const unsigned char*)GetModuleHandleW(nullptr);
		const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(exe + ((const IMAGE_DOS_HEADER*)exe)->e_lfanew);
		void* heap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64);
		void* tries[4] = { (void*)(exe + nt->OptionalHeader.AddressOfEntryPoint), (void*)&DllMain,
			(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"), heap };
		static const char* what[4] = { "exe-entry", "own-dllmain", "kernel32", "heap" };
		int before = g_nTg;
		for (int i = 0; i < 4; ++i)
		{
			const unsigned char* p = (const unsigned char*)tries[i];
			Log("hook_test: %s %p bytes %02X %02X %02X %02X %02X", what[i], p, p[0], p[1], p[2], p[3], p[4]);
			bool hooked = HookTarget(H_PRESENT, tries[i]);
			Log("hook_test: %s -> %s", what[i], hooked ? "HOOKED (guard failed)" : "refused");
		}
		CommitHooks("hook_test");
		Log("hook_test: done, hook targets %d -> %d", before, g_nTg);
	}

	DWORD installedAt = GetTickCount();
	bool warned = false, notYet = false;
	for (;;)
	{
		Sleep(500);
		FILETIME t = IniTime();
		// issue 3308: the proxy's second boot in this process hands out a section of its own through VPSHELL_SECTION; the
		// composite stays loaded, so it follows the variable as it follows an ini change (the section is re-opened)
		wchar_t envSec[128] = L"";
		const DWORD envN = GetEnvironmentVariableW(L"VPSHELL_SECTION", envSec, 128);
		const bool envChanged = envN > 0 && envN < 128 && wcscmp(envSec, c.section) != 0;
		if (CompareFileTime(&t, &last) != 0 || envChanged)
		{
			last = t;
			Sleep(50);
			iniState = IniState(&iniErr);   // ticket 0490
			ReadConfig(c);
			EnterCriticalSection(&g_cfgLock); g_cfgShared = c; LeaveCriticalSection(&g_cfgLock);
			InterlockedIncrement(&g_cfgVersion);
			const char* why = envChanged ? "VPSHELL_SECTION changed (a new boot of the proxy)" : "config file changed";
			LogConfig(why, c);
			LogConfigSource(why, c, iniState, iniErr);
			if (g_mhOk && c.mode != MODE_OFF && !g_discovered) { DiscoverWithDummy(); InterlockedExchange(&g_discovered, 1); installedAt = GetTickCount(); }
			ApplySwitches(c);
			LsApply(c, g_mhOk);
		}
		LsFlush();
		LkFlush();   // issue 3304
		LkxSinkPoll();   // ticket 0691
		PcFlush();   // ticket 0688: the pacing lines and vidmem (written here, never on the main thread)
		UnitFilter_ControlTick(Log);   // issue 4752: a latch without a Present for 1 s is cleared
		int frames = 0;
		if (TakeTrigger(L"shell_composite.shot", nullptr)) { InterlockedExchange(&g_shotRequest, 1); Log("shot requested"); }
		{
			// issue 3005: shell_composite.listshot, [listshot] from=A to=B: the back buffer right after ExecuteCommandList #k,
			// one k per frame (k = A..B over successive frames; keep the camera still). Nothing is inserted or drawn.
			wchar_t lp[MAX_PATH]; _snwprintf_s(lp, _TRUNCATE, L"%sshell_composite.listshot", g_dir);
			if (GetFileAttributesW(lp) != INVALID_FILE_ATTRIBUTES)
			{
				int from = GetPrivateProfileIntW(L"listshot", L"from", 1, lp), to = GetPrivateProfileIntW(L"listshot", L"to", 60, lp);
				DeleteFileW(lp);
				if (from < 1) from = 1;
				if (to < from) to = from;
				g_listShotTo = to;
				InterlockedExchange(&g_listShotRequest, from);
				Log("listshot requested: k %d..%d", from, to);
			}
		}
		if (TakeTrigger(L"shell_composite.trace", &frames)) { InterlockedExchange(&g_traceWanted, 1); InterlockedExchange(&g_traceRequest, std::max(1, frames)); Log("trace of %d frames requested", frames); }
		// Issue 3018 V9 (2026-11-13): the game loads this DLL at the front end 16-30 s before its first Present, so the old
		// WARNING at 10 s came at every launch. Now: a WARNING 10 s after a swap chain was created through the hooked
		// factory without a Present, or 120 s after the hooks were installed; before that one plain "not yet" line.
		if (!warned && g_discovered && !g_presentSeen)
		{
			DWORD now = GetTickCount(), since = now - installedAt;
			LONG created = g_scCreatedTick;
			if (created && now - (DWORD)created > 10000)
			{
				warned = true;
				Log("WARNING: a swap chain was created through the hooked factory 10 s ago but no Present seen: the host presents through an implementation that was not hooked (other DXGI version, wrapper or overlay)?");
			}
			else if (since > 120000)
			{
				warned = true;
				Log("WARNING: hooks installed 120 s ago but no Present seen: the host presents through an implementation that was not hooked (other DXGI version, wrapper or overlay)?");
			}
			else if (!notYet && since > 10000)
			{
				notYet = true;
				Log("no Present seen yet 10 s after the hooks were installed (not a fault while the host loads; a WARNING follows at 120 s, or 10 s after a swap chain is created, if none comes)");
			}
		}
	}
}

// Issue 0148: g_outDir from VPSHELL_OUT_DIR (the folder is created if missing, one level), else the DLL's folder. Runs in
// DllMain under the loader lock: kernel32 calls only.
static void ChooseOutDir()
{
	wcsncpy_s(g_outDir, g_dir, _TRUNCATE);
	wchar_t v[MAX_PATH];
	const DWORD n = GetEnvironmentVariableW(L"VPSHELL_OUT_DIR", v, MAX_PATH - 2);
	if (n == 0) return;
	if (n >= MAX_PATH - 2) { g_outNote = "VPSHELL_OUT_DIR too long: the DLL's folder used"; return; }
	const size_t l = wcslen(v);
	if (l && v[l - 1] != L'\\' && v[l - 1] != L'/') { v[l] = L'\\'; v[l + 1] = 0; }
	CreateDirectoryW(v, nullptr);   // ERROR_ALREADY_EXISTS is fine
	const DWORD a = GetFileAttributesW(v);
	if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
	{
		g_outNote = "VPSHELL_OUT_DIR is not a folder and could not be created: the DLL's folder used";
		return;
	}
	wcsncpy_s(g_outDir, v, _TRUNCATE);
	g_outNote = "VPSHELL_OUT_DIR";
}

// Ticket 0385: the default log starts per EXE process. Before this process opens <outDir>shell_composite.log, a log left by
// an earlier process is renamed to shell_composite.<its last write, local time yyyymmdd-hhmmss>[-n].log, so no line is lost
// and tools that read shell_composite.log see this process only. When the rename is refused (another process still has the
// file open: the CRT opens it without FILE_SHARE_DELETE) or no free name is found, this process logs to
// shell_composite.<pid>.log instead and says so in its first lines. VPSHELL_LOG (a path the caller chooses, the tests'
// per-run file) is opened as before, appending. Runs in DllMain under the loader lock: kernel32 and the CRT's string
// functions only, as ChooseOutDir.
static void PrepareDefaultLog(wchar_t* path, size_t cap)
{
	g_logNote[0] = 0;
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return;
	if (fa.nFileSizeHigh == 0 && fa.nFileSizeLow == 0) { DeleteFileW(path); return; }   // nothing to keep (a delete that fails: appended to)
	wchar_t stem[MAX_PATH];
	wcsncpy_s(stem, path, _TRUNCATE);
	wchar_t* dot = wcsrchr(stem, L'.');
	wchar_t* sep = wcsrchr(stem, L'\\');
	if (dot && (!sep || dot > sep)) *dot = 0;
	FILETIME lt; SYSTEMTIME st = {};
	FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt);
	FileTimeToSystemTime(&lt, &st);
	wchar_t to[MAX_PATH];
	for (int n = 1; n <= 99; ++n)
	{
		if (n == 1) _snwprintf_s(to, _TRUNCATE, L"%s.%04u%02u%02u-%02u%02u%02u.log", stem, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		else _snwprintf_s(to, _TRUNCATE, L"%s.%04u%02u%02u-%02u%02u%02u-%d.log", stem, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, n);
		if (MoveFileExW(path, to, 0)) { _snwprintf_s(g_logNote, _TRUNCATE, L"the previous log kept as %s", to); return; }
		const DWORD e = GetLastError();
		if (e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS)
		{
			wchar_t was[MAX_PATH]; wcsncpy_s(was, path, _TRUNCATE);
			_snwprintf_s(path, cap, _TRUNCATE, L"%s.%u.log", stem, GetCurrentProcessId());
			_snwprintf_s(g_logNote, _TRUNCATE, L"%s could not be renamed (error %lu: another process has it open): this process logs here", was, e);
			return;
		}
	}
	wchar_t was[MAX_PATH]; wcsncpy_s(was, path, _TRUNCATE);
	_snwprintf_s(path, cap, _TRUNCATE, L"%s.%u.log", stem, GetCurrentProcessId());
	_snwprintf_s(g_logNote, _TRUNCATE, L"%s kept (no free name for it): this process logs here", was);
}

// Ticket 0747 (D255 G5b, research/07 options-file.md 10.4 (c)): the kept logs PrepareDefaultLog leaves had no limit (about
// 25 MB an hour of play for good). After it, only the newest N of <outDir>shell_composite.<yyyymmdd-hhmmss>[-<n>].log stay;
// older ones are deleted. N is VPSHELL_KEEP_LOGS (1..100; the proxy sets it from [Logs] KeepRuns, the number of run
// folders it keeps), else 10. Only names of exactly that form (n 2..99 without a leading zero, as PrepareDefaultLog
// makes them) count or are ever deleted: never shell_composite.log, a shell_composite.<pid>.log, the CSVs or anything
// else. Order: by the stamp, then n. Runs in DllMain under the loader lock: kernel32 and the CRT's string functions only.
static const int kKeepCap = 4096; // keys held per pass (the oldest ones); more files: another pass
static unsigned long long g_keepKeys[kKeepCap];

// key = yyyymmddhhmmss * 100 + n (n = 1 for the name without "-<n>"); 0 when the name is not a kept log's
static unsigned long long KeptLogKey(const wchar_t* name)
{
	static const wchar_t kPre[] = L"shell_composite.";
	const size_t lp = _countof(kPre) - 1;
	if (_wcsnicmp(name, kPre, lp) != 0) return 0;
	const wchar_t* p = name + lp;
	unsigned long long stamp = 0;
	for (int i = 0; i < 15; ++i)
	{
		if (i == 8) { if (p[i] != L'-') return 0; continue; }
		if (p[i] < L'0' || p[i] > L'9') return 0;
		stamp = stamp * 10 + (unsigned)(p[i] - L'0');
	}
	p += 15;
	unsigned n = 1;
	if (*p == L'-')
	{
		++p;
		if (p[0] < L'1' || p[0] > L'9') return 0;
		n = (unsigned)(p[0] - L'0');
		++p;
		if (*p >= L'0' && *p <= L'9') { n = n * 10 + (unsigned)(*p - L'0'); ++p; }
		if (n < 2) return 0;
	}
	if (_wcsicmp(p, L".log") != 0) return 0;
	return stamp * 100 + n;
}

static void KeptLogName(unsigned long long key, wchar_t* out, size_t cap)
{
	const unsigned n = (unsigned)(key % 100);
	const unsigned long long s = key / 100;
	const unsigned day = (unsigned)(s / 1000000ULL), tod = (unsigned)(s % 1000000ULL);
	if (n == 1) _snwprintf_s(out, cap, _TRUNCATE, L"%sshell_composite.%08u-%06u.log", g_outDir, day, tod);
	else _snwprintf_s(out, cap, _TRUNCATE, L"%sshell_composite.%08u-%06u-%u.log", g_outDir, day, tod, n);
}

static void PruneKeptLogs()
{
	g_keepNote[0] = 0;
	unsigned keep = 10;
	wchar_t v[16];
	const DWORD vl = GetEnvironmentVariableW(L"VPSHELL_KEEP_LOGS", v, _countof(v));
	const wchar_t* from = L"the default";
	if (vl > 0 && vl < _countof(v))
	{
		unsigned x = 0;
		bool ok = v[0] != 0;
		for (const wchar_t* q = v; *q && ok; ++q)
		{
			if (*q < L'0' || *q > L'9') ok = false;
			else x = x * 10 + (unsigned)(*q - L'0');
			if (x > 100) ok = false;
		}
		if (ok && x >= 1) { keep = x; from = L"VPSHELL_KEEP_LOGS"; }
		else from = L"the default; VPSHELL_KEEP_LOGS is not a whole number from 1 to 100";
	}
	else if (vl >= _countof(v))
		from = L"the default; VPSHELL_KEEP_LOGS is not a whole number from 1 to 100";
	wchar_t pattern[MAX_PATH];
	_snwprintf_s(pattern, _TRUNCATE, L"%sshell_composite.*.log", g_outDir);
	unsigned deleted = 0, failed = 0, total = 0;
	for (int pass = 0; pass < 64; ++pass)
	{
		// the kKeepCap oldest keys, ascending (insertion; the newest dropped when full), and the count of all
		int held = 0;
		total = 0;
		WIN32_FIND_DATAW fd;
		HANDLE fh = FindFirstFileW(pattern, &fd);
		if (fh == INVALID_HANDLE_VALUE) break;
		do
		{
			if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
			const unsigned long long k = KeptLogKey(fd.cFileName);
			if (!k) continue;
			++total;
			if (held == kKeepCap && k >= g_keepKeys[held - 1]) continue;
			int j = held < kKeepCap ? held++ : held - 1;
			while (j > 0 && g_keepKeys[j - 1] > k) { g_keepKeys[j] = g_keepKeys[j - 1]; --j; }
			g_keepKeys[j] = k;
		} while (FindNextFileW(fh, &fd));
		FindClose(fh);
		if (total <= keep) break;
		unsigned over = total - keep;
		if (over > (unsigned)held) over = (unsigned)held;
		unsigned failedNow = 0;
		for (unsigned i = 0; i < over; ++i)
		{
			wchar_t p[MAX_PATH];
			KeptLogName(g_keepKeys[i], p, _countof(p));
			if (DeleteFileW(p)) ++deleted;
			else ++failedNow;
		}
		failed += failedNow;
		if (failedNow || over == total - keep) { if (!failedNow) total = keep; break; }
	}
	if (deleted || failed)
		_snwprintf_s(g_keepNote, _TRUNCATE, L"older logs: %u deleted, %u could not be deleted; the newest %u kept (%s; ticket 0747)",
		             deleted, failed, keep, from);
	else if (total)   // no line when there is no kept log at all (ticket 0385's empty-log case stays without a "log:" line)
		_snwprintf_s(g_keepNote, _TRUNCATE, L"older logs: %u kept, at most %u (%s; ticket 0747)", total, keep, from);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_module = h;
		DisableThreadLibraryCalls(h);
		// The hooks point into this module: never let it unload.
		HMODULE pinned = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&DllMain, &pinned);
		QueryPerformanceFrequency(&g_freq);
		InitializeCriticalSection(&g_cfgLock);
		InitializeCriticalSection(&g_logLock);
		InitializeCriticalSection(&g_hookLock);
		InitializeCriticalSection(&g_ctxLock);
		InitializeCriticalSection(&g_clLock);
		InitializeCriticalSection(&g_recLock);
		InitializeCriticalSection(&g_depthLock);
		GetModuleFileNameW(h, g_dir, MAX_PATH);
		wchar_t* slash = wcsrchr(g_dir, L'\\'); if (slash) slash[1] = 0;
		_snwprintf_s(g_iniPath, _TRUNCATE, L"%sshell_composite.ini", g_dir);
		ChooseOutDir();
		wchar_t logPath[MAX_PATH];
		if (!GetEnvironmentVariableW(L"VPSHELL_LOG", logPath, MAX_PATH))
		{
			_snwprintf_s(logPath, _TRUNCATE, L"%sshell_composite.log", g_outDir);
			PrepareDefaultLog(logPath, _countof(logPath));   // ticket 0385: one process per log
			PruneKeptLogs();                                 // ticket 0747: only the newest N kept logs stay
		}
		g_log = _wfsopen(logPath, L"a", _SH_DENYNO);
		HANDLE t = CreateThread(nullptr, 0, ControlThread, nullptr, 0, nullptr);
		if (t) CloseHandle(t);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		// ticket 0339: at the process's end the white view and FOWAnim go back (data writes only, no D3D call); the log only
		// when its lock is free (a thread ended inside it would hold it for ever)
		if (TryEnterCriticalSection(&g_logLock))
		{
			LeaveCriticalSection(&g_logLock);
			if (g_pcRun.presents && !g_pcFinal) PcTotalsLine(g_pcRun, "exit");   // ticket 0688: the run-long histogram at the end (0697: unless a fatal end wrote it)
			FowModels_ProcessExit(Log);
		}
		else FowModels_ProcessExit(nullptr);
	}
	return TRUE;
}
