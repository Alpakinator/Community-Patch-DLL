// srv_render_host.h - the renderer inside server.exe (epic 3300, issue 3301; decision D81 points 1-2).
//
// One render thread ("vp-render") owns the D3D11 device and its immediate context, the terrain scene (srv_render_scene.*,
// ported from shell_server.exe --scene terrain), the version-3 section inputs (camera, height-field tap, bake tap) and the
// frame hand-over (a vpframe::FrameSink: the version-3 keyed-mutex sink of srv_v3_sink.* for today's composite, or the
// ring sink of shell/frame/ring_sink.* with render_handover=ring). Design: research/05-graphics-composition/
// renderer-in-server-design.md sections 1, 5, 6 and the 3301 section.
//
// Rules (by construction):
//   * The render thread never calls into the DLL and never reads a DLL object: every file of server/src/render is compiled
//     without the fork's include path (server/build_server.py), and its only game input is the event feed
//     (feed/srv_feed.h, drained once per loop iteration).
//   * Failure inside the render thread never reaches RemoteMain's unhandled-exception filter (which would end the session
//     with exit 2): every C++ exception and every SEH exception is caught on the render thread; the renderer then goes to
//     VPFS_FAILED and the thread waits for the stop. A lost device is rebuilt in the process (back-off 1, 2, 4 ... 30 s);
//     after 3 lost devices in 5 minutes VPFS_FAILED (VPFSF_REBUILDS). In play (after the first READY) the game goes on.
//   * Ticket 0600 (D118, D125): a failure BEFORE the first READY (the first device, the sink, the scene's init, an
//     exception there) and every refusal of HostStart (except "already started") take D125's route once: HostConfig::fatal
//     with exit code 5, so the player gets the report and the popup, never a grey map. server.exe's StartRenderer does the
//     same for a refused render_* configuration (research/05-graphics-composition/renderer-failure-modes.md).
//   * Wake-up by <base>.Cam (vp_frame.h) once the composite has written the publish block; before that (today's composite
//     publishes nothing) the camera block is polled on a 0.5 ms high-resolution timer, counted as legacy pacing.
// This header has no DLL type and no D3D type: srv_remote_main.cpp (which sees the DLL's headers) includes it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <functional>
#include <map>
#include <string>

namespace srvrender {

typedef void (*HostLogFn)(const char* line);

const uint32_t kTestInitFailException = 100;   // ticket 0600: HostConfig::testInitFail's "exception" (not a VPFSF_ value)

struct HostConfig
{
	std::wstring section = L"Local\\VPShellSection";    // render_section: the version-3 section = <base> of vp_frame.h
	std::wstring textureBase = L"Local\\VPShellWorld";  // render_texture: the version-3 shared texture names
	std::wstring exe;                 // render_exe: CivilizationV_DX11.exe (TerrainShader programs, read-only)
	std::wstring logPath;             // render_log: render.log (every line; server.log gets the notes)
	std::string handover = "v3";      // render_handover: v3 | ring
	uint32_t ringSlots = 4;           // render_ring_slots: 2..4 (the composite's request wins)
	uint32_t ringSync = 1;            // render_sync: fence (1) | cpu (2)
	uint64_t luid = 0;                // render_luid: adapter by LUID (hex); 0 = the composite's (clientLuid), else the largest
	bool depth = true;                // render_depth: v3 depth block and texture
	bool gpuSync = true;              // render_gpu_sync: v3 waits for the GPU before ReleaseSync(1)
	uint32_t width = 1920, height = 1080;   // render_size: v3 start size until the composite asks
	int tilePlots = 0, tileD4 = 0, pass = -1;
	double tess = 0;
	bool water = true;
	// issue 3704 (epic 0167): render_overlay: 1 (default) = the EXE's Terrain Overlay target through the tap as terrain t1
	// and water t9 | 0 = no overlay (t1 / t9 transparent, the picture before 3704)
	int overlay = 1;
	// issue 3704, MC #701 (a) / #713: called at most once, on the render thread, when the overlay tap could not install at
	// game start (the reader's State::Failed: VPOVL_FAILED or a section layout it refuses), with exit code 5 and a text
	// naming the reason. server.exe: SrvRemote::ReportAndExit (its report, then the process ends; the proxy's next call
	// fails: the server-death report, whose log tails carry the text, and Fatal(3) in a D33 session: D125's route). A
	// stale or missed copy during play never calls it (D132 R6). nullptr (tests): the failure is LOUD, drawing goes on.
	// Ticket 0600: also called (code 5) when the renderer fails before its first READY, and by HostStart before it returns
	// false (except "already started"); nullptr: LOUD only, the process goes on.
	void (*fatal)(int code, const char* why) = nullptr;
	std::string color = "caches", fallback = "material";
	uint32_t constantRgba = 0xFF808080u;
	int staleFrames = 15;
	// issue 3308: the tile caches. render_tiles: bake (default: the server's own, epic 3400's Residency) | tap (the EXE's
	// caches through the bake tap: the test reference of D81 point 3) | none (fallback colour only)
	std::string tiles = "bake";
	std::string baker = "pipeline";   // render_baker: pipeline (the EXE's rules, issue 3404) | standin (tests)
	std::wstring bakeInstall;         // render_bake_install: the install the pipeline reads (default: server.cfg game_dir)
	int bakeRadius = -2;              // render_bake_radius -1..128 overrides [Renderer] BakedTileCameraRadius; -2 = the option
	double bakeBudgetMs = 4.0;        // render_bake_budget_ms: CPU per beforeDraw / idle call (shell_server --bake-pump-ms)
	double bakeRate = 400;            // render_bake_rate: background bakes per second
	int bakeBurst = 16;               // render_bake_burst: visible tiles baked per frame
	double bakeGpuMsPerS = 0;         // render_bake_gpu_ms_per_s: background GPU budget (0 = off)
	int testBakeFailX = -1, testBakeFailY = -1;   // render_test_bake_fail=X,Y: the stand-in fails that tile (test switch)
	// issue 3308: pacing before the composite's first publish. The 0.5 ms camera poll of the legacy composite runs only
	// while the scene has a camera to draw (state OK); otherwise (database phase, load, no composite) the render thread
	// wakes every render_idle_poll_ms, or at once on <base>.Cam. 0 = the old rule (0.5 ms poll throughout).
	double idlePollMs = 20;
	// ticket 0688 (epic 0686 P1; research/08-performance/frame-pacing-instruments.md): render_pass_stats 1 (default) = each
	// pass of the loop timed by stage (shell/common/vp_pacing.h PassTimer), a "pass 1s:" line, a "pass long:" line for each
	// pass of render_pass_long_ms or more (busy time, the wait excluded; 0..10000, default 20), the wake delay, and the
	// video budget in a "vidmem 10s:" line | 0 = none of it (the cost arm). Written through RLog (ticket 5054's log thread).
	bool passStats = true;
	double passLongMs = 20;
	// ticket 0711 (J1; srv_frame_jobs.h): render_frame_jobs = auto | 0 | N (1..8): the workers of the scene's frame-jobs pool
	// (auto: 0 on a machine with 2 logical cores or fewer, else 2; 0: every job inline at its join), resolved here; and
	// render_test_frame_jobs_jitter = 0 | 1 (TEST ONLY: 0-5 ms sleeps at each fork and job start, owner checks fatal)
	int frameJobs = 0;
	bool frameJobsJitter = false;
	// ticket 0753 (J3; SceneOptions::recordAhead): render_record_ahead = auto (-1, absent) | 0 | 1; resolved by Scene::init
	// on the device (auto: frame-jobs workers and driver command lists)
	int recordAhead = -1;
	// ticket 0713 (the prepare phase; research/07-architecture-decisions/architect-2026-11-23-frame-overlap.md 6):
	// render_prepare = 1 (default) | 0: the feed's drain and the taps' polls run on the composite's <base>.Tap (after the
	// last tap of its Present hook), and a camera whose tap number was prepared goes straight to decide; 0 = every pass
	// drains and polls at its start, as before (the A/B arm). Off with a recorder (render_test_record).
	bool prepare = true;
	// test switches (never set in a product config)
	int testDeviceLostEvery = 0;      // render_test_device_lost=N: every N-th draw is treated as a lost device
	int testThrowAt = 0;              // render_test_throw=N: a C++ exception at draw N
	int testAvAt = 0;                 // render_test_av=N: an access violation at draw N
	uint32_t rebuildBackoffMs = 1000; // render_rebuild_backoff_ms: the first back-off (doubles up to 30 s)
	// ticket 0600: render_test_init_fail=adapter|device|renderer|section|exception: the first device creation fails with
	// that reason (VPFSF_*), or a C++ exception is thrown in its place (kTestInitFailException); 0 = off
	uint32_t testInitFail = 0;
	std::wstring testDump;            // render_test_dump=PATH: the v3 frame after render_test_dump_after publishes (+ .depth.f32)
	int testDumpAfter = 1;
	// epic 0301, issue 3904: the scene recorder's keys as given (render_test_record, _after, _frames, _dump; checked by
	// ParseRecorderKeys in srv_scene_recorder.h). Empty = no recorder: the live path is today's.
	std::map<std::string, std::string> testRecord;
	HostLogFn log = nullptr;          // server.log (notes, LOUD lines)
	// server.exe: binds the delay-loaded D3D imports up front (issue 9008's rule); nullptr in the test host
	std::function<bool(std::string& why)> bindImports;
};

// Reads the render_* keys through get(key) ("" = not set) into out. False (why = the key and the value) on any malformed
// or out-of-range value: the caller does not start the renderer (server.exe, ticket 0600: and ends by D125's route, exit
// 5; before 0600 the game went on without terrain). gameDir: render_exe's default folder;
// runDir: render_log's default folder.
bool ParseHostConfig(const std::function<std::string(const char* key)>& get, const std::string& gameDir, const std::string& runDir,
                     HostConfig& out, std::string& why);

// Ticket 0674 (research/05-graphics-composition/pan-coherence.md 3): render_fow_mask_window = camera (default: when camera
// N sees ground outside the overlay pair's window, the fog's mask is built in a window made from camera N and the overlay
// of frame N-1 is moved into it) | pair (the pair's window always: the picture before 0674, where that ground reads no mask
// for one frame). ParseHostConfig reads it through ParseRenderFowMaskWindow (srv_render_options.cpp; HostConfig is not
// 0674's to change: a file-scope value as render_fow's); "" = the default. False (why = the key and the value) on another
// value. Last parse wins; the recorder writes it into render_config, so the offline player replays it.
const int kFowMaskWindowPair = 0, kFowMaskWindowCamera = 1;
bool ParseRenderFowMaskWindow(const std::string& value, std::string& why);
int RenderFowMaskWindowOption();                 // kFowMaskWindowCamera | kFowMaskWindowPair
const char* RenderFowMaskWindowName(int option);   // "camera" | "pair"

// Starts the render thread. False when it was not started (why).
bool HostStart(const HostConfig& cfg, std::string& why);
// Orderly stop (kRt_Shutdown): stop event, join (5 s), state STOPPED, magic cleared. Idempotent; no-op when not started.
void HostStop(const char* why);
// Marks the frame section STOPPED without joining (process exit on an error path). Safe from any thread.
void HostAbandon();
// One line for the server's report (state, draws, rebuilds, exceptions, feed); "" when never started.
std::string HostSummary();
// VPFrameServerState of the renderer (0 = never started).
uint32_t HostState();

}  // namespace srvrender
