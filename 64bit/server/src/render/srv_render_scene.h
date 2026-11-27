// srv_render_scene.h - the terrain scene of the renderer inside server.exe (epic 3300, issue 3301).
//
// Origin (COMMON 13, D22): the terrain scene of shell/server/shell_server.cpp (--scene terrain: TerrainState, TapPoll,
// WriteTapAck, ApplyField, WantTilePlots, the camera part of TerrainFrame, the draw, TerrainSecond and the totals line)
// at this repository's commit b4f7748 (file last changed in ff223fd); that file is unchanged (team 3400 owns it). The bake
// tap moved behind srvrender::TileCacheSource (srv_exe_cache_tap.*). Differences: the scene draws into the targets a
// vpframe::FrameSink hands it (the sink owns colour and depth), there is no rate cap, no --tap-file / --save-tap and no
// overlay grid (test-only options of shell_server.exe).
//
// Inputs: the version-3 section (camera block, tap block, bake-tap block), mapped by the scene itself so that both sinks
// (v3 and ring) see the same inputs. All calls on the render thread. No DLL access.
#pragma once
#include "srv_render_common.h"
#include "srv_exe_cache_tap.h"
#include "srv_residency_source.h"
#include "srv_water_scene.h"   // epic 3600, issue 3607
#include "srv_overlay_tap.h"   // epic 0167, issue 3704
#include "srv_fow_scene.h"     // epic 0168, issue 3954
#include "srv_shadow_tap.h"    // epic 0169, issue 4308
#include "srv_ice_tap.h"       // epic 0169, issue 4308
#include "srv_unit_shadow.h"   // epic 0042, ticket 4785
#include "srv_frame_jobs.h"    // ticket 0711 (J1): the frame-jobs pool
#include "units/srv_units_scene.h"   // epic 0375, issue 4454
#include "units/srv_units_mode.h"    // epic 0040, issue 4753
#include "forest/srv_forest_scene.h"   // epic 0431, ticket 4554 (abstract SceneForest; the units in server/src/forest/)
#include "srv_farmfields.h"   // epic 0528, ticket 4852: the server's own farm fields
#include "srv_liveterrain.h"   // epic 0465, ticket 4907: live terrain (Scene::liveTerrainStep)
#include "landmarks/srv_models_scene.h"   // epic 0561, ticket 4875 (abstract ModelsScene; the units in landmarks/, cities/)
#include "extras/srv_extras.h"   // epic 0454, ticket 4938 (abstract SceneExtras; the units in server/src/extras/)
#include "c5r/arc/arc.h"   // ticket 0199 part 2
#include "c5r/pathmodels/pathmodels.h"   // ticket 0300 part 2
#include "c5r/ice/ice.h"
#include "c5r/terrain/terrain.h"
#include "c5r/heightfield/heightfield.h"
#include <memory>

namespace srvrender {

// ---- The overlay (epic 0167, issue 3704; research/05-graphics-composition/overlay-system.md 4, 5 and 10) ----------------
// Every loop iteration the scene asks its OverlaySource for the newest pair (the EXE's Terrain Overlay target copied at
// Present N-1 and the OverlayProj array of that frame: decision 2 (i)); a new generation asks for a redraw. The draw binds
// the pair as terrain t1 and water t9 with each wrap copy's matrix (OverlayCopiesFromFrame). A source in State::Failed
// (the tap could not install: VPOVL_FAILED, or a section the reader refuses) is LOUD once and reported through
// overlayFailed(); the host then takes the fatal route of MC #701 / #713 (OverlayFatalStep, HostConfig::fatal). Any other
// state draws the transparent default (today's picture, bit-equal), the state logged once per change. render_overlay=0:
// no source at all.

// Where the pairs come from: the tap's reader in server.exe (MakeTapOverlaySource), a fake in the tests.
struct OverlaySource
{
	virtual ~OverlaySource() {}
	virtual bool acquire(OverlayFrame& out) = 0;          // as OverlayTapReader::acquire (out.srv owned by the source)
	virtual OverlayTapReader::State state() const = 0;
	virtual uint32_t failReason() const = 0;               // VPShellOverlayFail when state() == Failed
	virtual std::string why() const = 0;                   // the reader's lastWhy()
	virtual int64_t staleCount() const { return 0; }
	virtual int64_t staleLongestPresents() const { return 0; }
	virtual int64_t refusedCount() const { return 0; }
};
std::unique_ptr<OverlaySource> MakeTapOverlaySource(VPShellHeader* hdr, ID3D11Device* dev);
const char* OverlayStateName(OverlayTapReader::State s);
const char* OverlayFailName(uint32_t reason);

// The OverlayProj of each wrap copy k = -1, 0, +1 (proj[k + 1], c5r::terrain::Overlay's convention) from one pair:
// copy 0 = view 0; a view v > 0 whose matrix equals view 0's but for an x translation of k * wrapPeriod world units
// (within 0.5; the EXE's wrap views: wrapPeriod = 0xD90FE4 = 64 W - 1) -> copy k, taken as the EXE wrote it; a copy that
// no EXE view shows (the EXE does not draw it) takes view 0's matrix moved by k * wrapPeriod, marked derived; a view that
// fits no copy is counted in unmatchedViews. wrapPeriod not finite or <= 0 (no wrap): every copy is view 0's.
// False (why): numViews 0 or above VPSHELL_OVERLAY_MAX_VIEWS, a matrix not finite, view 0's x or y scale 0.
struct OverlayCopies
{
	float proj[3][16] = {};
	bool fromExe[3] = {false, true, false};
	uint32_t unmatchedViews = 0;
};
bool OverlayCopiesFromFrame(const OverlayFrame& f, float wrapPeriod, OverlayCopies& out, std::string& why);

// Ticket 0793 (research/05-graphics-composition/overlay-history.md): render_fow_overlay_history = 1 (default: every
// received overlay is written into the overlay's whole-map history, and a camera-window frame's leading edge is drawn
// from it where it holds the ground) | 0 (the picture before 0793: the edge transparent for a frame). The render_config
// key, parsed (srv_render_options.cpp) by ParseHostConfig and written into a bundle's render_config by the recorder
// (srv_render_host.cpp, as 0674's render_fow_mask_window), so an offline replay draws what was recorded; the environment
// variable C5_RENDER_FOW_OVERLAY_HISTORY ("0" / "1") overrides it at Scene::init (tests, A/B runs).
bool ParseRenderFowOverlayHistory(const std::string& value, std::string& why);
bool RenderFowOverlayHistoryOption();
const char* RenderFowOverlayHistoryName(bool on);   // "1" | "0"
// The overlay the water scene `w` binds at its next draw() (srv_water_scene.cpp): t9 = srv (null: the module's own
// transparent input) and WaterFrame.OverlayProj per view = proj[1] (the camera's view), proj[0] / proj[2] (the views at
// -width / +width). A free function because srv_water_scene.h is not 3704's; it moves into WaterScene when that header
// is next changed (3607's pattern for render_water's mode).
void WaterSceneOverlay(const WaterScene* w, ID3D11ShaderResourceView* srv, const float (*proj)[16]);

// MC #701 (a) / #713: one step of the fatal route, run by the host after every Scene::poll(). When the scene's overlay
// source is Failed (an install failure at game start) and `done` is false: done = true, a note (the LOUD line is the scene's, once), then fatal(5, text)
// (HostConfig::fatal; server.exe: SrvRemote::ReportAndExit, which does not return). Never for any other state: a stale or
// missed copy during play keeps the source Active (D132 R6). fatal null: LOUD only, the scene draws on without the overlay.
const int kOverlayFatalExitCode = 5;   // server.exe's exit code on this route (2 = an exception, 3 = the engine is gone)
class Scene;
void OverlayFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);

// ---- The models' shadow and the polar ice (epic 0169, issue 4308; terrain-surface.md 11.10, polar-ice.md 10) -------------
// Shadow: every loop iteration the scene asks its ShadowSource (the shadow tap's reader, ShadowTapReader, made once the
// EXE's pid is known) for the newest triple (the EXE's ShadowBlur copy, ShadowMatrix0 per view and the strength of one
// frame: never mixed) and binds it in the draw as the terrain's Options::shadow (t3, Arg6, ShadowMatrix0 per wrap copy by
// ShadowCopiesFromFrame) and the water's t13 / WaterFrame 0x220 / ShadowMatrix0 per view (WaterSceneShadow). Quality 0
// (srv null): the modules' no-shadow path with strength 0. render_shadow=0: no source (the picture before 4308).
// Ice: the scene owns an IceSource (IceTapReader) and a c5r::ice::Renderer; a new generation is setMesh'd (a mesh the
// module refuses is LOUD and counted, the last good mesh stays); each frame the ice is drawn after the ocean water and the
// rivers and before the coast waves and the cloud planes (stock's order, polar-ice.md 2; WaterSceneAfterOcean), depth
// write on, with the terrain's camera, OverlayProj, the fog's FOWAnim / mask / cloud base (or the terrain's fog-off
// constants when the terrain draws without the fog) and the terrain's fog ramp and Generic TerrainFOW LUT. Not into the
// reflection (decision 3: the server's reflection target is transparent black; a named difference). render_ice=0: none.
// Failures (D118, D125, D132): a source in State::Failed (installFailure(), lastWhy()), the ice module that cannot start,
// or (shadow only) no shadow tap section within shadowWaitMs after the first field with a valid camera grid -> LOUD once,
// shadowFailed() / iceFailed(), and the host's ShadowFatalStep / IceFatalStep: HostConfig::fatal once. The tap OFF (the
// composite's shadow_tap=0 / ice_tap=0, test switches) is LOUD once and draws none. In play: a refused triple or mesh is
// LOUD (the readers' own lines; the scene counts), the last good input stays, never fatal.
struct ShadowSource
{
	virtual ~ShadowSource() {}
	virtual bool acquire(ShadowFrame& out) = 0;            // as ShadowTapReader::acquire (out.srv owned by the source)
	virtual ShadowTapReader::State state() const = 0;
	virtual uint32_t failReason() const = 0;
	virtual std::string why() const = 0;                   // the reader's lastWhy()
	virtual int64_t refusedCount() const { return 0; }
	virtual int64_t staleCount() const { return 0; }
};
std::unique_ptr<ShadowSource> MakeTapShadowSource(uint32_t exePid, VPShellHeader* hdr, ID3D11Device* dev);
const char* ShadowStateName(ShadowTapReader::State s);
// The ShadowMatrix0 of each wrap copy k = -1, 0, +1 (matrix[k + 1], c5r::terrain::Shadow's convention) from one triple:
// copy 0 = view 0; a view v > 0 equal to view 0 but for row 3 moved by k * wrapPeriod times row 0 (terrain-surface.md 11
// S3: within 1e-5 of the matrix's largest element) -> copy k, as the EXE wrote it; a copy no EXE view shows takes view 0's
// matrix moved, marked derived; a view that fits no copy is counted. wrapPeriod not finite or <= 0: every copy is view 0's.
// False (why): numViews 0 or above VPSHELL_SHADOW_MAX_VIEWS, a matrix not finite.
struct ShadowCopies
{
	float matrix[3][16] = {};
	bool fromExe[3] = {false, true, false};
	uint32_t unmatchedViews = 0;
};
bool ShadowCopiesFromFrame(const ShadowFrame& f, float wrapPeriod, ShadowCopies& out, std::string& why);
// The shadow the water scene `w` binds at its next draw() (srv_water_scene.cpp): t13 = srv (null: the module's own zero),
// WaterFrame 0x220 = strength, ShadowMatrix0 per view = m[1] (the camera's view), m[0] / m[2] (the views at -width /
// +width). m null: nothing set (the module's defaults: the picture before 4308).
void WaterSceneShadow(const WaterScene* w, ID3D11ShaderResourceView* srv, float strength, const float (*m)[16]);
// The ice's place in the water scene's draw: after the ocean and the rivers, before the coast waves (fn null: none).
void WaterSceneAfterOcean(const WaterScene* w, void (*fn)(void* user, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                                                          ID3D11DepthStencilView* dsv),
                          void* user);

struct IceSource
{
	virtual ~IceSource() {}
	virtual bool poll(uint32_t haveGeneration) = 0;                     // as IceTapReader::poll
	virtual std::shared_ptr<const c5r::ice::Mesh> mesh() const = 0;
	virtual uint32_t generation() const = 0;
	virtual uint32_t loadSeq() const = 0;
	virtual IceTapReader::State state() const = 0;
	virtual bool installFailure() const = 0;
	virtual std::string why() const = 0;
	virtual int64_t refusedCount() const { return 0; }
};
std::unique_ptr<IceSource> MakeTapIceSource(uint32_t exePid, VPShellHeader* hdr);
// One fixed mesh as the ice source (server.cfg render_test_ice_file, a test key): poll() offers it once, state Active.
std::unique_ptr<IceSource> MakeStaticIceSource(std::shared_ptr<const c5r::ice::Mesh> mesh, uint32_t generation, uint32_t loadSeq);
const char* IceStateName(IceTapReader::State s);

const int kShadowFatalExitCode = 5;   // as the overlay's: the route of D125
const int kIceFatalExitCode = 5;
void ShadowFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);
void IceFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);
// epic 0375, issue 4454 (unit-server-scene.md 10, PrecheckFailed): the unit scene's pre-check failed (a pre-check entry not
// Ok, the module's open failing, the unit database unreadable) -> once: HostConfig::fatal(5, why) (D125's route). fatal null:
// LOUD only, the scene draws on without units. Epic 0040, issue 4753: also a unit readiness lost in play beyond
// render_units_ready_hold_ms while the mode word's unit bit was set (Scene::unitsFailed; srv_units_mode.h), why "units: ...".
void UnitsFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);
// epic 0561, ticket 4875 (D118, D125): the model module's failure (srv_models_scene.h, srv_models_mode.h) -> once:
// HostConfig::fatal(5, why). fatal null: LOUD only, the scene draws on (the module draws nothing more after a failure).
void ModelsFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);
struct SceneShadowCounters
{
	int64_t boundFrames = 0, q0Frames = 0, noneFrames = 0, triples = 0, derivedCopies = 0, unmatchedViews = 0, badTriples = 0;
	int64_t readers = 0;
	uint32_t generation = 0, quality = 0, numViews = 0, format = 0;
	float strength = 0;
};
struct SceneIceCounters
{
	int64_t meshes = 0, meshRefused = 0, drawnFrames = 0, drawErrors = 0, readers = 0;
	uint32_t generation = 0, chunks = 0, copies = 0, draws = 0;
	uint64_t indices = 0;
};

struct SceneOptions
{
	std::wstring exe;                 // CivilizationV_DX11.exe: the TerrainShader programs (read-only)
	int tilePlots = 0;                // 0 = the bake-tap block's when ACTIVE, else c5r::terrain::kDefaultTilePlots
	int tileD4 = 0;
	c5r::terrain::ColorSource color = c5r::terrain::ColorSource::Caches;
	c5r::terrain::ColorSource fallback = c5r::terrain::ColorSource::Material;
	uint32_t constantRgba = 0xFF808080u;
	int pass = -1;
	double tess = 0;
	bool water = true;                // false: no water at all (render_water=0)
	// issue 3607: the water (srv_water_scene.h): full (the module, the placeholder as the fallback) or the placeholder only
	int waterMode = WaterSceneOptions::kFull;
	bool waterFreeze = false;         // diagnostics: the water clock held (render_water_freeze=1)
	std::wstring install;             // the game install (water art), read-only
	int staleFrames = 15;             // presentCount - frameStamp above this = the publisher stopped (STALE)
	// issue 3308: where the tile caches come from (server.cfg render_tiles)
	enum Tiles { kTilesBake = 0, kTilesTap = 1, kTilesNone = 2 };
	int tiles = kTilesBake;           // bake = the server's own (c5r::bake::Residency); tap = the EXE's (test reference)
	ResidencySourceConfig bake;       // render_baker, render_bake_*
	// issue 3704: render_overlay: 1 (default) = the tap's overlay as terrain t1 / water t9 (a tap that cannot install
	// ends the game, MC #701); 0 = no overlay source (the transparent default)
	int overlay = 1;
	// issue 3954: the fog of war (srv_fow_scene.h). 0 here (tools and the scene tests that build a Scene directly keep
	// today's picture); server.exe takes render_fow from server.cfg, default 1 (srv_render_host.cpp SceneOpts).
	int fow = 0;
	int fowLevel = -1;                // render_fow_level: -1 = the player's GraphicsSettingsDX11.ini
	double fowWaitMs = 10000;         // the install's wait for the plot table, a revealed plot and the overlay pair
	// issue 4105: render_decals (1: the decal tap's reader, the decals in the server's bake, the install-failure route; 0:
	// no reader and no decals, the picture before 0166); the install bound (no decal set within it after the first field
	// and a valid camera grid = an install failure) and render_test_decal_trace (the first N bakes with decals logged)
	int decals = 0;                   // 0 here (tools and scene tests keep today's picture); server.exe: render_decals, default 1
	double decalWaitMs = 20000;
	int decalTrace = 0;
	// issue 4308: render_shadow (1: the shadow tap's triple as terrain t3 / Arg6 / ShadowMatrix0 and water t13 / 0x220;
	// 0: none, the picture before) and render_ice (1: the ice tap's mesh drawn by c5r::ice at stock's place; 0: none). 0
	// here (tools and scene tests); server.exe: default 1 both. shadowWaitMs: no shadow tap section (state Absent) this
	// long after the first field with a valid camera grid = an install failure (0: no bound); iceWaitMs the same for the
	// ice tap (default 0: no bound, the ice tap is not in every composite yet).
	int shadow = 0;
	double shadowWaitMs = 20000;
	int ice = 0;
	double iceWaitMs = 0;
	// epic 0375, issue 4454: render_units (1: the server's unit scene, srv_units_scene.h; 0: none, the picture before) and
	// its options (render_units_passes, render_test_units_dump, ...). 0 here (tools and scene tests); server.exe takes
	// render_units from server.cfg, default 0 (decision 8). Read by Scene::configureUnits, not by init().
	int units = 0;
	srvunits::UnitsOptions unitsOpt;
	// epic 0040, issue 4753 (srv_units_mode.h): render_units_ready_hold_ms (a readiness lost while the mode word's unit bit
	// was set may last this long before D125's fatal step; breakdown O5: 2 s) and render_units_positions (own: the server's
	// own root positions, 0375's; exe: the EXE's position records, built by issue 4755; the unit bit is set only with exe).
	// server.exe parses both in srv_render_host.cpp; read by Scene::init and Scene::configureUnits.
	enum UnitsPositions { kUnitsPositionsOwn = 0, kUnitsPositionsExe = 1 };
	double unitsReadyHoldMs = srvunits::kUnitsReadyHoldMsDefault;
	int unitsPositions = kUnitsPositionsOwn;
	// epic 0042, ticket 4785 (srv_unit_shadow.h; unit-combat-server.md 13): render_units_shadow (1: with render_units and
	// render_shadow, the server's units cast stock's shadow into the tapped map and the mode word's bit 4 asks the compositor
	// to drop the EXE's unit mask draws; 0: the EXE's figures cast, the picture before 4785). 1 here and in server.exe; it acts
	// only with a unit scene, the shadow tap and a compositor that flags the slots.
	int unitsShadow = 1;
	// epic 0431, ticket 4554: render_forest (1: the forest and jungle trees, forest/srv_forest_scene.h; 0: none, the picture
	// before 0431). 0 here (tools, the offline player and the scene tests that build a Scene directly); server.exe takes
	// render_forest from server.cfg, default 1. forestIdsWaitMs: render_test_forest_ids_wait_ms (no published feature ids
	// this long after the first plot table = an install failure). forestSeed: the session's random stream (0: random).
	int forest = 0;
	double forestIdsWaitMs = 10000;
	uint32_t forestSeed = 0;
	// ticket 4553's cut tap in the forest scene: render_test_cut_wait_ms (no grid this long after the field = an install
	// failure; 0: no bound) and render_test_cut_fail=1 (the reader's install failure on purpose, a negative launch)
	double forestCutWaitMs = 20000;
	bool forestCutFail = false;
	// epic 0528, ticket 4852: render_farmfields (1: the server lays out its own farm fields and tracks, srv_farmfields.h,
	// and joins them to the decal tap's set; 0: none, the tap's set as it is). 0 here and in server.exe until 0528 D's option.
	// farmKeepTap: render_farmfields_keep_tap=1 keeps the tap's own farm records (then a tap set that holds them is handed
	// on without the server's, as the forest does); 0 (default) drops them. farmIdsWaitMs: render_farmfields_ids_wait_ms.
	int farmfields = 0;
	bool farmKeepTap = false;
	double farmIdsWaitMs = 20000;
	bool farmThreaded = true;   // false: the offline player (every production inside poll(), before the frame needing it)
	// ticket 4934 (epic 0454): render_roadlayout (1: FixRoadLayout, the server lays out the roads itself, srv_extra_roads.h,
	// composed before the farm's; 0: the tap's route curves as they are). server.exe takes it from FixRoadLayout's effective
	// state unless server.cfg names it; the offline player from the bundle's render_config or --option. roadThreaded: as
	// farmThreaded.
	int roadLayout = 0;
	bool roadThreaded = true;
	// ticket 0199 part 2: render_arc (1: the ranged-attack arc, c5r::arc, from the feed's kUiMirror kind-1 / kind-2 records
	// of the proxy's arrows install; research/05-graphics-composition/arrows-and-path-line.md 13; 0: none). 0 here (tools,
	// scene tests); server.exe takes render_arc from server.cfg, default 0 (the proxy writes 1 with [Renderer] ServerArc = 1);
	// the offline player from the bundle's render_config.
	int arc = 0;
	// ticket 0300 part 2: render_path (1: the move-order path's discs, chevrons and destination ring, c5r::pathmodels, from
	// the feed's kUiMirror kind-8 (nodes and the EXE's tests) and kind-4 (show / hide) records of the proxy's path install;
	// research/05-graphics-composition/path-models.md 7; 0: none). 0 here; server.exe from server.cfg, default 0 (the
	// proxy writes 1 with [Renderer] ServerPath = 1). The offline player does not draw the path yet.
	int path = 0;
	// epic 0561, ticket 4875: render_cities / render_landmarks (the proxy writes 1 with [Renderer] ServerCities / ServerLandmarks
	// = 1 and ServerRenderer = 1), render_models_source and the mode writer's bounds (landmarks/srv_models_scene.h). Both 0
	// here and in server.exe by default; read by Scene::configureModels, not by init().
	srvmodels::ModelsOptions models;
	// ticket 0711 (J1; srv_frame_jobs.h): render_frame_jobs, the workers of the scene's frame-jobs pool (0: every job runs
	// inline at its join, on the same code path), and render_test_frame_jobs_jitter (TEST ONLY: 0-5 ms sleeps at each fork
	// and job start, R8's determinism proof; the owner checks are fatal with it). 0 here (tools, scene tests); server.exe:
	// render_frame_jobs=auto (srv_render_host.cpp); the offline player: the bundle's key, absent = 0. Read by init() once.
	int frameJobs = 0;
	bool frameJobsJitter = false;
	// ticket 0753 (J3; research/07 architect-2026-11-25-frame-gap-route.md 5): render_record_ahead = auto (-1) | 0 | 1. On,
	// the draw stage's end (FowScene::afterWorld, pathDraw) is recorded into a deferred context's command list while the
	// units' job is forked (before its join) and the list is executed at the stage's place; the same calls in the same
	// order. auto: on when the frame-jobs pool has workers and the driver reports command lists (D3D11_FEATURE_THREADING
	// DriverCommandLists); 1: on whatever the driver says (the runtime then replays the calls at the execute: the TEST arm of
	// rule A and machines without driver lists); 0: off. 0 here (tools, scene tests); server.exe: auto; the offline player:
	// the bundle's key, absent = 0, C5_SCENESHOT_RECORD_AHEAD replaces it. Read by init().
	int recordAhead = 0;
	// ticket 0753 (TEST ONLY, render_test_record_ahead_always): record ahead records and executes the end on every draw, also
	// without a units' job in flight (the render-host suite has no units)
	bool recordAheadAlways = false;
	// ticket 0753 (TEST, render_test_record_ahead_end_only): step 1 alone (the draw stage's end), not step 2's terrain and
	// middle lists (the A/B of the two steps)
	bool recordAheadEndOnly = false;
};

// ---- ticket 0753 (J3): record ahead's counters (Scene::recordAheadCounters) ----
struct SceneRecordAheadCounters
{
	int64_t lists = 0;           // lists recorded and executed at their place
	int64_t notForked = 0;       // draws with record ahead on but no units' job in flight (drawn inline, nothing to hide)
	int64_t finishFailed = 0;    // FinishCommandList failed: the stage drawn inline at its place (A5; LOUD)
	int64_t dropped = 0;         // recorded but the draw ended before the list's place (a failed terrain draw, an exception)
	int64_t timedLists = 0;      // lists whose particle GPU timing bracketed the executed list on the immediate context
	// step 2: the terrain list (the clear and the terrain draw) and the middle list (arc, extras, the path's depth copy,
	// forest) executed at their places; draws without a prediction of the units' merged shadow map (step 2 not recorded:
	// the step makes its UnitShadow later, or apply() made no map yet); predictions that missed (both lists dropped and
	// drawn inline, LOUD)
	int64_t terrainLists = 0, midLists = 0, noPrediction = 0, mispredicted = 0;
	double recordMsTotal = 0, execMsTotal = 0;
	// per second (secondLine resets them)
	int64_t listsSec = 0, notForkedSec = 0;
	double recordMsSec = 0, recordMsMaxSec = 0, execMsSec = 0, execMsMaxSec = 0;
};

// ---- ticket 0300 part 2: the path's counters (Scene::pathCounters) ----
struct ScenePathCounters
{
	int64_t nodeRecords = 0;   // kind-8 records taken
	int64_t paths = 0;         // whole paths assembled (every node of one update, or one empty-path record)
	int64_t broken = 0;        // records that did not continue the path being assembled (dropped; LOUD once each kind)
	int64_t shows = 0, hides = 0;   // kind-4 records with a[0] = 1 / 0
	int64_t loads = 0;         // load begins (the path and the shown flag cleared)
	int64_t rebuilds = 0;      // instance lists made (placePath)
	int64_t placeErrors = 0;   // nodePositions / placePath refused (LOUD)
	int64_t drawnFrames = 0;   // frames with at least one draw of the path
	int64_t draws = 0;         // mesh draws in all
	int64_t unplaced = 0;      // instances held by no view on screen (not drawn, as the EXE's two lists)
	int64_t drawErrors = 0;    // buildDraws / PathRenderer::draw failed (the frame stands without the path; LOUD)
	uint32_t lastInstances = 0;   // the last list's instances
	bool ready = false;        // the models and the renderer were made (render_path=1)
};

// ---- ticket 0199 part 2: the arc's counters (Scene::arcCounters; the totals line) ----
struct SceneArcCounters
{
	int64_t spawns = 0;        // kind-1 records taken (the last of a batch is applied, as the EXE's one record)
	int64_t removes = 0;       // kind-2 records and load begins taken
	int64_t applied = 0;       // spawns turned into the arrow record at a draw
	int64_t spawnErrors = 0;   // spawnPoints / Arrow::spawn refused (a plot outside the grid, no grid yet: LOUD once each)
	int64_t drawnFrames = 0;   // frames with the arc drawn (2 draws each)
	int64_t drawErrors = 0;    // Renderer::draw or Arrow::update failed (the frame stands without the arc; LOUD)
	int64_t uploads = 0;       // mesh uploads (one per rebuilt generation)
	bool ready = false;        // the renderer and the settings were made (render_arc=1)
};
// epic 0040, issue 4753: what ParseHostConfig read last for unitsReadyHoldMs / unitsPositions (srv_render_host.cpp; tests)
double RenderUnitsReadyHoldMsOption();
int RenderUnitsPositionsOption();
int RenderUnitsShadowOption();   // ticket 4785: render_units_shadow as ParseHostConfig read it last

// ---- Decals (epic 0166, issue 4105; research/05-graphics-composition/terrain-decals.md 12) --------------------------------
// The scene owns the decal tap's reader (srv_decal_tap.h), made once the tap block names the EXE's pid, polled every loop
// iteration. A set the reader applied goes to the residency (ResidencySource::setDecals: names resolved and checked), then
// the generation's changes (ResidencySource::decalChanges: the tiles re-baked), never in the other order (P13), and only
// once the residency has a field (until then they wait in the reader's queue). Failures (decision 6, D118, D125):
//   install: the control block FAILED with PATTERN or SEH (the tap is off for the life of the EXE), no decal set within
//   decalWaitMs after the first field with a valid camera grid, the decal pass not starting (TerrainDecalShader blobs), a
//   texture name the install cannot resolve -> LOUD once, decalFailed(), and the host's DecalFatalStep: HostConfig::fatal
//   once (D125's report and popup), never a picture without decals.
//   in play: an image the reader refuses (torn, hash, a bad record), a FAILED LAYOUT / NAMES / TOO_BIG / SECTION the tap
//   retries, a bake with decals that fails -> LOUD (the first 20, then every 100th), counted, the last good set stays.
//   The tap state OFF (the composite's decal_tap=0, a test switch) is LOUD once and draws no decals.
const int kDecalFatalExitCode = 5;   // as the overlay's: the route of D125
void DecalFatalStep(Scene& s, void (*fatal)(int code, const char* why), bool& done);
struct SceneDecalCounters
{
	int64_t sets = 0, refused = 0, inPlayFaults = 0, changesDrained = 0, readers = 0;
	uint32_t generation = 0, state = 0, failReason = 0;
	double lastReadyToAppliedMs = -1;   // the tap's readyQpc -> setDecals (indication)
};

// The camera chosen for one draw.
struct SceneFrame
{
	VPShellCamera cam;
	int64_t frameId = 0;              // the camera block's frameStamp (= the publish block's frameId under lockstep)
	// epic 0301, issue 3903: the water clock's time for this draw in ms (QpcToMs scale); negative (the default) = the wall
	// clock read in draw() where it was read before (today's behaviour). The offline player passes each recorded value.
	double nowMs = -1;
};

struct SettleResult;   // srv_scene_sources.h (epic 0301, issue 3903)

class Scene
{
public:
	Scene();
	~Scene();
	// Maps the version-3 section (created or opened). False: logged.
	bool attach(const std::wstring& section);
	VPShellHeader* header() { return m_hdr; }
	// The renderer on this device (shaders from the EXE) and the tile source (render_tiles); the kept field is re-applied
	// (device rebuild). A tile source that cannot start is LOUD once per device and the scene draws without caches.
	c5r::Status init(ID3D11Device* dev, ID3D11DeviceContext* ctx, const SceneOptions& o);
	// Device lost or shutdown: every D3D object (the renderer, the tap's textures); the height field copy is kept.
	void releaseDevice();
	void detach();
	// Every loop iteration: the tap block, the bake tap. Never blocks for long (a tap copy is one memcpy of the field).
	// Without a section (epic 0301, issue 3903: the offline player): only the overlay source and the residency's flag.
	void poll();
	// The camera block: true when a frame should be drawn now (new frameStamp, or the field / caches / target changed).
	bool decide(SceneFrame& out);
	void markDirty() { m_dirty = true; }
	void resetFrameIds() { m_lastDrawnStamp = -1; }
	// Draws the chosen camera into the sink's targets (colour cleared to transparent, depth cleared to 1).
	c5r::Status draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	                 const SceneFrame& f, c5r::terrain::FrameStats& fs);
	// The feed's changed plots (re-bakes in a baking source; the tap only counts them). Without a source (no device yet,
	// a rebuild) they are counted as lost: a new source starts from onField(), i.e. from the whole field (issue 3306).
	void plotsChanged(const uint32_t* idx, size_t n)
	{
		if(TileCacheSource* s = source())
			s->onPlotsChanged(idx, n);
		else
			m_plotsChangedNoSource += (int64_t)n;
	}
	// issue 3308: after the frame was published, the source's background work (bake-ahead, re-bakes) within budgetMs (0
	// when a newer camera already waits); a visible tile that got its cache asks for a redraw.
	void idle(ID3D11DeviceContext* ctx, double budgetMs);
	double idleBudgetMs() const { return m_res ? m_res->budgetMs() : 0; }
	// background work is waiting: the host waits briefly instead of up to 100 ms
	bool sourcePending() { return m_res && m_res->pending(); }
	// a camera block newer than the last one decide() read
	bool cameraPending() const;
	TileCacheSource* source() { return m_tap ? (TileCacheSource*)m_tap.get() : m_res ? (TileCacheSource*)m_res.get() : nullptr; }
	void secondLine(char* out, size_t n);
	void totalsLine(char* out, size_t n);
	bool haveField() const { return m_haveField; }
	WaterScene& water() { return m_water; }   // issue 3607
	// ---- epic 0301, issue 3904: the height-field tap's hand-off point for the scene recorder (contract 4.2) ----
	// Called by tapPoll() with the C5HF payload of a copy that passed every check (readTap, the tap block's and the camera
	// block's map size), cut to c5h::tapSize(), before the section is unmapped and acked. The sink must not keep the
	// pointer. Null (the default): no recorder; the live path then costs one pointer test per accepted copy.
	void setFieldSink(void (*f)(void* ctx, uint32_t loadSeq, const uint8_t* c5hf, size_t bytes), void* ctx)
	{
		m_fieldSink = f;
		m_fieldSinkCtx = ctx;
	}
	int state() const { return m_state; }
	// issue 3704: true once the overlay source said Failed; why = the error text naming the reason (for the fatal route)
	bool overlayFailed(std::string& why) const
	{
		if(m_ovlFailed)
			why = m_ovlFailWhy;
		return m_ovlFailed;
	}
	// tests: replaces the overlay source of this device (after init(); releaseDevice() drops it)
	void setOverlaySource(std::unique_ptr<OverlaySource> s) { m_ovl = std::move(s); m_ovlHave = false; }
	const c5r::terrain::Overlay& lastOverlay() const { return m_lastOverlay; }   // tests: what the last draw bound
	// ticket 0674: what the last draw bound for the terrain, water, trees, ice and models: lastOverlay() (the received pair,
	// which the recorder writes) on most frames; on a frame whose fog mask window was made from the camera (FowTerrainInputs::
	// cameraWindow) that window's copy matrices with the pair's overlay moved into it (or none when the move failed)
	const c5r::terrain::Overlay& boundOverlay() const { return m_boundOverlay; }
	// issue 3954: the fog of war. fow().terrainInputs() is what the terrain draw binds for its fog (task 3953: t4 mask,
	// t5 cloud base, FOWAnim), valid during draw() after the mask step; FowFatalStep reads fow().failed().
	FowScene& fow() { return m_fow; }
	// ticket 0691 (cand4's hang, 2026-11-23): the hold of the newest decide() for the host's report (vp_frame.h
	// VPFrameHoldReason): VPHOLD_NO_FIELD for the camera's frame while the terrain has no field or another load's (a map
	// reload: the composite copies the new field only after 30 Presents with the camera OK, so the frame must not be waited
	// for), else the fog's hold (fow().holdFrameId() / holdReasons()); 0 / 0 when nothing is held
	static constexpr uint32_t kHoldNoField = 1u << 5;   // = vp_frame.h VPHOLD_NO_FIELD (the host checks it)
	int64_t holdFrameId() const { return m_fieldHoldFrame ? m_fieldHoldFrame : m_fow.holdFrameId(); }
	uint32_t holdReasons() const { return m_fieldHoldFrame ? kHoldNoField : m_fow.holdReasons(); }
	// The host's drained feed batch (every one, also without changed plots): a fog change asks for a redraw.
	// Ticket 4555 (trees.md 15.4): first the residency's late table (a table generation the field has not taken: the
	// table came after the field and no plot changed), so the tiles' styles (and the trees' style entries) follow at
	// once. Here, in the drain's handling, because the host's loop and the offline player (sceneshot) both call
	// fowFeed for every drained batch and the player reads a PLOTS record only inside the drain before it.
	// Ticket 4938: then the extras module (after the fog took the batch; nothing without an extra in effect).
	void fowFeed(const SrvFeed::Batch& b)
	{
		if(m_res && m_haveField && m_res->onFeedGeneration(b.snapshotGen))
			m_dirty = true;
		if(m_fow.onBatch(b))
			m_dirty = true;
		if(m_extras)
			m_extras->onBatch(b);
	}
	// ---- epic 0454, ticket 4938: the extras module (extras/srv_extras.h) ----
	// The [Extras] options' effective state, taken once per Scene: init() calls it at the first device with SrvOptions()
	// (server.exe: the options file; sceneshot: its defaults and --option) and SceneOptions::fow. No extra in effect = no
	// module (no extras code runs: the picture as before). An extra in effect in a program that does not link
	// server/src/extras/ is LOUD once and not drawn. Tests may call it before init().
	void configureExtras(const Vp64Opt::Options& o, bool fogConfigured);
	srvextras::SceneExtras* extras() { return m_extras.get(); }
	// ---- epic 0375, issue 4454: the unit scene (unit-server-scene.md 3, 7, 8; srv_units_scene.h) ----
	// Before the host's loop drains its first batch (the allocator must see every unit event of the process): on = the
	// unit scene is made (once; it lives as long as the Scene, across device rebuilds); off = none: unitFeed and the draw
	// do nothing and no unit code runs. False (why) when on and this program does not link the unit scene.
	bool configureUnits(bool on, const srvunits::UnitsOptions& o, std::string& why);
	// The host's drained feed batch (every one, after fowFeed): the unit tables, the allocator, the fades.
	// issue 4456: drainMs = the drain's clock (the recorder's value when recording; < 0: the scene reads the clock)
	// issue 4753: a kLoadBegin in the batch clears the mode word's unit bit (unitsModeFeed, before the unit scene's feed)
	void unitFeed(const SrvFeed::Batch& b, double drainMs = -1.0)
	{
		if(!m_units)
			return;
		unitsModeFeed(b);
		if(m_units->feed(b, drainMs))
			m_dirty = true;
	}
	srvunits::SceneUnits* units() { return m_units.get(); }
	// 0040's readiness signal (note 8): the unit scene on, its pre-check passed, the module open, the table taken, the last
	// frame Ok, no D125 failure. Issue 4753: the mode word's writer (m_unitsMode) reads it after each drawn frame and in
	// every poll().
	bool unitsReady() const { return m_units && m_units->ready(); }
	// The unit scene's D125 failure (PrecheckFailed, AnimUnavailable) or, issue 4753, a readiness lost in play beyond
	// render_units_ready_hold_ms (the mode writer's); the host's UnitsFatalStep takes either once.
	bool unitsFailed(std::string& why) const
	{
		if(m_units && m_units->failed(why))
			return true;
		return m_units && m_unitsMode && m_unitsMode->failed(why);
	}
	// issue 4753: the mode word's writer (srv_units_mode.h), made (srvunits::MakeModeWriter) and turned on with the unit
	// scene (configureUnits / setUnits), off and kept without it; null before the first unit scene and in a program without
	// server/src/units/. Its block is the attached section's VPSHELL_MODE (tests may attach their own). unitsFrameDone() is
	// its step after each drawn frame (draw() calls it after the units' scene pass; public for the tests, which draw no frame).
	srvunits::ModeWriter* unitsMode() { return m_unitsMode.get(); }
	const srvunits::ModeWriter* unitsMode() const { return m_unitsMode.get(); }
	void unitsFrameDone();
	// tests: a unit scene of the test's own making (MakeSceneUnitsWith); replaces any
	void setUnits(std::unique_ptr<srvunits::SceneUnits> u);
	// ---- ticket 0711 (J1): frame jobs (srv_frame_jobs.h; the Architect's R1-R9) ----
	// forkFrameJobs(f) once the frame's camera is fixed (the host: right after decide() returned true, with SceneFrame::nowMs
	// stamped, R9; draw() forks itself when nobody did, so the players and a recording host take the same path): the
	// fog's plot states and plot table handed to the units, their beginHead on this thread, then their beginBody forked
	// as the units' job owning the module (R2). draw() joins it before the units' first use (the cast into the shadow map,
	// or the later begin place); joinFrameJobs() joins whatever is still forked (the host after DrawFrame on every path,
	// R3; releaseDevice, setUnits and the destructor too). Nothing to do without a unit scene.
	void forkFrameJobs(const SceneFrame& f);
	void joinFrameJobs();
	FrameJobs* frameJobs() { return m_jobs.get(); }
	// ticket 0711: the `draw` stage of the last draw() in its three stretches (QPC ticks; vppacing kStDraw): head = before
	// the units' mark (the host's part, the overlay, the fog's placement, the shadow and ice inputs), mid = after the water
	// (the arc, the extras, the path's depth); the end (the cloud planes, the path) is the host's remainder. 0 without a pass.
	int64_t drawHeadTicks() const { return m_drawHeadTicks; }
	int64_t drawMidTicks() const { return m_drawMidTicks; }
	// ticket 0753 (J3): record ahead resolved at init (SceneOptions::recordAhead) and its counters
	bool recordAheadOn() const { return m_raOn; }
	bool recordAheadDriverLists() const { return m_raDriverLists; }
	const SceneRecordAheadCounters& recordAheadCounters() const { return m_rac; }
	// ---- epic 0561, ticket 4875: cities and landmarks from the compositor's model tap (city-renderer.md 11) ----
	// on (o.any()) = the module is made (once; it lives as long as the Scene, across device rebuilds) and configured; off =
	// none. False (why) when on and this program does not link server/src/render/landmarks/ (MakeModelsScene null).
	bool configureModels(const srvmodels::ModelsOptions& o, std::string& why);
	srvmodels::ModelsScene* models() { return m_models.get(); }
	bool modelsFailed(std::string& why) const { return m_models && m_models->failed(why); }
	// The host's drained feed batch: a kLoadBegin clears the model bits (the module's mode writer)
	void modelsFeed(const SrvFeed::Batch& b);
	// ---- epic 0431, ticket 4554: the trees (research/05-graphics-composition/trees.md 13) ----
	// The host's drained feed batch (every one, after fowFeed): the forest and jungle records.
	void forestFeed(const SrvFeed::Batch& b)
	{
		if(m_forest && m_forest->onBatch(b))
			m_dirty = true;
	}
	// null: render_forest=0, or this program does not link server/src/forest/ (then render_forest=1 is LOUD at init)
	SceneForest* forest() { return m_forest.get(); }
	// ---- epic 0528, ticket 4852: the server's farm fields (farm-fields-layout.md 13) ----
	// The host's drained feed batch (every one, beside forestFeed): the farm plots.
	void farmFeed(const SrvFeed::Batch& b)
	{
		if(m_roads)   // ticket 4934: the route bands (FixRoadLayout), from the same batches
			m_roads->onBatch(b);
		if(m_farm)
			m_farm->onBatch(b);
	}
	// ticket 4934: null when render_roadlayout=0 (or server/src/extras/ is not linked)
	srvextras::SceneRoads* roads() { return m_roads.get(); }
	// null: render_farmfields=0
	FarmFieldsProducer* farm() { return m_farm.get(); }
	// tests: a producer of the test's own making (replaces any), and the decal set the residency would get for a tap set
	void setFarm(std::unique_ptr<FarmFieldsProducer> f) { m_farm = std::move(f); }
	std::shared_ptr<const c5r::decal::DecalSet> decalsFor(const std::shared_ptr<const c5r::decal::DecalSet>& tap)
	{
		return composedDecals(tap);
	}
	// ---- ticket 0199 part 2: the ranged-attack arc (render_arc=1; arrows-and-path-line.md 13) ----
	// The host's drained feed batch (every one, after forestFeed): kUiMirror kind 1 (SpawnArrowEvent with x0, y0, x1, y1
	// from the proxy's wrapper) = a pending spawn, the batch's last one wins; kind 2 (RemoveAllArrowsEvent) and kLoadBegin
	// hide the arc and drop a pending spawn. Nothing with render_arc=0.
	void arcFeed(const SrvFeed::Batch& b);
	const SceneArcCounters& arcCounters() const { return m_arcC; }
	const c5r::arc::Arrow& arcArrow() const { return m_arc; }   // tests
	// epic 0301, issue 3911: decide()'s fog step (FowScene::admit with the scene's field, overlay source and pair) for the
	// offline player, which draws without decide() and passes the recorded clock value. server.exe does not call it.
	bool fowAdmit(const VPShellCamera& cam, double nowMs);

	// ---- epic 0301, issue 3903: the height field's file entry and the settle call ----
	// The file source of the height-field tap: c5h::readTap over a recorded C5HF payload, the camera block's grid checks of
	// tapPoll() when `cam` is given (its gridSeq against loadSeq: stale; its samples against the field's), then the field
	// kept and applied as tapPoll() does after a good copy. No section, no ack. Errors (nothing changed): readTap's,
	// Stale-as-BadLayout, BadLayout for the map size, NotFound when applyField fails (renderer not initialised or
	// setHeightField refused; logged).
	c5r::Status loadField(const uint8_t* c5hf, size_t bytes, uint32_t loadSeq, const VPShellCamera* cam = nullptr);
	// Runs the tile source until no bake is pending for this camera at target size w x h (ResidencySource::settle), at
	// most maxMs. No source (render_tiles=none) or the tap: settled at once (calls 0). out must not be null.
	c5r::Status settle(ID3D11DeviceContext* ctx, const SceneFrame& f, uint32_t w, uint32_t h, double maxMs, SettleResult* out);

	int64_t drawnTotal = 0, drawErrTotal = 0, staleTotal = 0, hiddenTotal = 0, camBlocksTotal = 0;
	int64_t tapLoads = 0, tapErrors = 0, fieldErrors = 0, tapRequests = 0;
	// issue 3704: frames drawn with / without an overlay, new pairs taken, copies whose matrix was derived (no EXE view),
	// EXE views that fit no copy, pairs refused by OverlayCopiesFromFrame
	int64_t ovlBoundFrames = 0, ovlNoneFrames = 0, ovlPairs = 0, ovlDerivedCopies = 0, ovlUnmatchedViews = 0, ovlBadPairs = 0;
	// ticket 0674: frames whose overlay was moved into the fog's camera window (c5r::fow::OverlayResampler), frames where that
	// move failed (drawn without the overlay, the mask still placed), frames in a camera window with no overlay to move
	int64_t ovlResampled = 0, ovlResampleFails = 0, ovlCamWinNoOverlay = 0;
	// ticket 0793: overlays offered to the history (OverlayResampler::remember), refused offers (no world box, a bad
	// window, a device error); the history's own counters are OverlayResampler::historyStats() (ovlHistory())
	int64_t ovlHistOffers = 0, ovlHistFails = 0;
	c5r::fow::OverlayHistoryStats ovlHistory() const { return m_ovlResample.historyStats(); }
	bool ovlHistoryOn() const { return m_ovlHistOn; }

	// issue 4105: true once a decal input could not install (the block comment above DecalFatalStep); why = the text
	bool decalFailed(std::string& why) const
	{
		if(m_decalFailed)
			why = m_decalFailWhy;
		return m_decalFailed;
	}
	const SceneDecalCounters& decalCounters() const { return m_dcount; }
	// issue 0364 (the scene bundle's decal member, format version 3): the decal tap's reader (null before the EXE's pid is
	// known or with render_decals=0); the recorder writes its set in force at the span's first frame (writeCurrentImage).
	const DecalTapReader* decalReader() const { return m_decalReader.get(); }
	// issue 0364: the offline player's decal input. The bundle's decals/load<NNNN>.dtap through the scene's reader (one
	// without a tap, made here when there is none: DecalTapReader(0)) and its file entry (loadImage: loadFile's checks on
	// bytes the player already checked against the manifest), then applied as decalPoll applies a polled set: the
	// residency's setDecals, and the generation's changes drained to the residency once it has a field (before it they
	// are dropped: the field's first bakes read the set in force). Errors: the reader's Status (nothing applied), or
	// NotFound / BadLayout with *why when there is no residency or setDecals refuses (a name the install and the mods'
	// folders do not hold).
	c5r::Status loadDecalImage(const uint8_t* image, size_t n, std::string* why);

	// ---- issue 4308: the shadow and the ice (the block comment above ShadowSource) ----
	bool shadowFailed(std::string& why) const
	{
		if(m_shFailed)
			why = m_shFailWhy;
		return m_shFailed;
	}
	bool iceFailed(std::string& why) const
	{
		if(m_iceFailed)
			why = m_iceFailWhy;
		return m_iceFailed;
	}
	// tests and the offline player: replace the sources (after init(); releaseDevice() drops the shadow source, the ice
	// source is kept); a source set here is never replaced by a tap reader
	void setShadowSource(std::unique_ptr<ShadowSource> s)
	{
		m_shSrc = std::move(s);
		m_shHave = false;
		m_shExternal = m_shSrc != nullptr;
	}
	void setIceSource(std::unique_ptr<IceSource> s)
	{
		m_iceSrc = std::move(s);
		m_iceExternal = m_iceSrc != nullptr;
		m_iceSrcGen = 0;
	}
	const c5r::terrain::Shadow& lastShadow() const { return m_lastShadow; }   // tests: what the last terrain draw bound
	bool lastShadowFrame(ShadowFrame& out) const                           // the triple the last draw bound (recorder)
	{
		if(m_lastShHave)
			out = m_lastShFrame;
		return m_lastShHave;
	}
	bool lastIceDrawn() const { return m_iceDrawnFrame; }                    // the ice was drawn in the last draw
	const c5r::ice::FrameStats& lastIceStats() const { return m_iceStats; }
	std::shared_ptr<const c5r::ice::Mesh> iceMesh() const { return m_iceMesh; }   // the mesh in the module (or null)
	uint32_t iceGeneration() const { return m_iceGen; }
	uint32_t iceLoadSeq() const { return m_iceLoadSeq; }
	const SceneShadowCounters& shadowCounters() const { return m_shc; }
	const SceneIceCounters& iceCounters() const { return m_icec; }

private:
	// issue 4308
	uint32_t exePid() const;
	void shadowPoll();
	void shadowFail(const std::string& why);
	void icePoll();
	void iceFail(const std::string& why);
	void iceApply();
	static void IceHook(void* user, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv);
	std::unique_ptr<ShadowSource> m_shSrc;
	bool m_shExternal = false, m_shHave = false, m_shFailed = false, m_shOffLogged = false;
	ShadowFrame m_shFrame;
	uint32_t m_shPid = 0;
	int m_shLoggedState = -1;
	int64_t m_shWaitFrom = 0, m_shRefusedSeen = 0;
	std::string m_shFailWhy, m_shLastBad;
	c5r::terrain::Shadow m_lastShadow;
	ShadowFrame m_lastShFrame;
	bool m_lastShHave = false;
	int m_shBoundSec = 0;
	SceneShadowCounters m_shc;
	c5r::ice::Renderer* m_ice = nullptr;
	bool m_iceReady = false;
	std::unique_ptr<IceSource> m_iceSrc;
	bool m_iceExternal = false, m_iceFailed = false, m_iceOffLogged = false;
	uint32_t m_icePid = 0, m_iceSrcGen = 0, m_iceGen = 0, m_iceLoadSeq = 0;
	int m_iceLoggedState = -1;
	int64_t m_iceWaitFrom = 0;
	std::string m_iceFailWhy, m_iceLastErr;
	std::shared_ptr<const c5r::ice::Mesh> m_iceMesh;   // the last good mesh (re-applied on a new device)
	c5r::ice::FrameInputs m_iceIn;                     // this frame's inputs (IceHook)
	bool m_iceWanted = false, m_iceDrawnFrame = false;
	int m_iceDrawnSec = 0;
	c5r::ice::FrameStats m_iceStats;
	SceneIceCounters m_icec;
	ID3D11ShaderResourceView* m_iceWhite = nullptr;    // the terrain's fog-off FOW mask (visible) for the ice
	ID3D11ShaderResourceView* m_iceBlack = nullptr;    // and its fog-off cloud base

	// issue 4105: the decal tap's reader and its control block (state and fail reason, read under its seqlock)
	void decalPoll();
	void decalFail(const std::string& why);
	void decalCloseControl();
	std::unique_ptr<DecalTapReader> m_decalReader;
	uint32_t m_decalPid = 0;
	HANDLE m_decalCtlMap = nullptr;
	const VPShellDecalTapControl* m_decalCtl = nullptr;
	int64_t m_decalCtlNextOpen = 0, m_decalWaitFrom = 0;
	uint32_t m_decalLoggedState = 0xFFFFFFFFu, m_decalLoggedFail = 0xFFFFFFFFu;
	uint64_t m_decalRefusedSeen = 0;
	bool m_decalFailed = false, m_decalOffLogged = false, m_decalEverSet = false;
	std::string m_decalFailWhy;
	std::vector<DecalChange> m_decalChanges;
	SceneDecalCounters m_dcount;
	DecalBakeCounters m_decalPrev;   // the residency sources released so far (device rebuilds, the end)
	void overlayPoll();
	std::unique_ptr<OverlaySource> m_ovl;
	OverlayFrame m_ovlFrame;
	bool m_ovlHave = false;
	bool m_ovlFailed = false;
	std::string m_ovlFailWhy, m_ovlLastBad;
	int m_ovlLoggedState = -1;
	int m_ovlBoundSec = 0;
	c5r::terrain::Overlay m_lastOverlay;
	c5r::terrain::Overlay m_boundOverlay;          // ticket 0674 (boundOverlay())
	c5r::fow::OverlayResampler m_ovlResample;      // ticket 0674: the overlay moved into the fog's camera window
	std::string m_ovlResampleLastErr;
	int m_ovlResampleSec = 0;
	// ticket 0793: the overlay's history (render_fow_overlay_history, read at init), the last refusal's text (LOUD once per
	// reason), the strips of the second filled whole / with a hole (the fog 1s line's ovl_hist / ovl_hole)
	bool m_ovlHistOn = true;
	std::string m_ovlHistLastErr;
	uint64_t m_ovlHistStripsMark = 0, m_ovlHistFilledMark = 0, m_ovlHistHolesMark = 0;
	FowScene m_fow;   // issue 3954
	std::unique_ptr<srvunits::SceneUnits> m_units;   // epic 0375, issue 4454 (null: render_units=0)
	std::unique_ptr<srvunits::ModeWriter> m_unitsMode;   // epic 0040, issue 4753 (on only while m_units)
	// ticket 0711 (J1): the frame-jobs pool and the units' job (declared after m_units, so destroyed before it: the job's
	// destructor joins it, R3); the fork of this frame (m_unitsForked until joinFrameJobs) and its frame, for draw()'s check
	std::unique_ptr<FrameJobs> m_jobs;
	FrameJob m_unitsJob{"units begin"};
	bool m_unitsForked = false;
	int64_t m_unitsForkFrame = 0;
	double m_unitsForkNowMs = -1;
	bool m_unitsForkMismatchSaid = false;
	bool joinUnitsJob(bool rethrow);   // true: a fork was joined here
	int64_t m_drawHeadTicks = 0, m_drawMidTicks = 0;
	// ticket 0753 (J3): record ahead. m_raCtx: a deferred context of this device (made by init when on, released with the
	// device); m_raEndList: this draw's recorded end (null: none, drawn inline at its place); m_raEndInline: the recording
	// failed at FinishCommandList, so the place draws the stage inline (A5); m_raEndTimed: the recorded particle draw wants its
	// GPU timing around the list's execute (FowScene::listTiming, A5: no query read on a deferred context)
	ID3D11DeviceContext* m_raCtx = nullptr;
	bool m_raOn = false, m_raDriverLists = false;
	ID3D11CommandList* m_raEndList = nullptr;
	bool m_raEndInline = false, m_raEndTimed = false;
	std::string m_raLastErr;
	int64_t m_raRecordTicks = 0;   // this draw's recording (QPC ticks; kept out of drawMidTicks)
	double m_raExecFrameMs = 0;    // this draw's executes (every list), for exec_ms_max
	SceneRecordAheadCounters m_rac;
	void recordAhead(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h, const SceneFrame& f,
	                 const c5r::terrain::Camera& cam, const c5r::terrain::Options& op);
	void recordDrawEnd(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h, const SceneFrame& f);
	// step 2: the terrain list and the middle list, recorded before the end (their CPU order kept: the trees' Screen Overlay
	// reads the terrain draw's LOD of the frame, pan-coherence.md 5.20.3), with the Options the shadow step will produce
	// (m_raShadowPredicted); checked after the step (raCheckPrediction), else both dropped and drawn inline
	ID3D11CommandList* m_raTerrainList = nullptr;
	ID3D11CommandList* m_raMidList = nullptr;
	c5r::Status m_raTerrainDs = c5r::Status::Ok();
	c5r::terrain::FrameStats m_raTerrainFs;
	ID3D11ShaderResourceView* m_raShadowPredicted = nullptr;
	bool m_raMidPathDepth = false;   // the middle list holds this frame's path depth decision and copy
	bool raPredictShadow(const c5r::terrain::Options& op, ID3D11ShaderResourceView*& srv) const;
	bool recordStep2(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h, const SceneFrame& f,
	                 const c5r::terrain::Camera& cam, const c5r::terrain::Options& op);   // false: the terrain draw failed
	void raCheckPrediction(const c5r::terrain::Options& op, int64_t frameId);
	void raDropStep2();
	bool raFinish(ID3D11CommandList** out, const char* what, int64_t frameId);
	void drawEnd(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	             const SceneFrame& f);
	void dropRecorded();   // a recorded list whose place the draw did not reach (counted)
	void unitsModeFeed(const SrvFeed::Batch& b);
	void unitsModeOn();
	void unitsModeOff();
	// epic 0042, ticket 4785: the units' shadow (srv_unit_shadow.h), made at the first frame that wants it on this device
	std::unique_ptr<UnitShadow> m_unitShadow;
	bool m_unitShadowTried = false;
	bool unitShadowStep(ID3D11DeviceContext* ctx, const SceneFrame& f, c5r::terrain::Options& op);   // true: units begun
	void unitShadowReadiness();
	struct UnitShadowCounters
	{
		int64_t mergedFrames = 0, mergedQ1 = 0, flagFrames = 0, notMerged = 0, maskFails = 0;
		int64_t mergedSec = 0;
		std::string lastWhy;
	} m_usc;
	// epic 0561, ticket 4875: the model module (null: render_cities=0 and render_landmarks=0, or not linked)
	std::unique_ptr<srvmodels::ModelsScene> m_models;
	srvmodels::FrameTextures modelsFrameTextures() const;
	// ticket 4882: this frame's camera and fog placement for the model records (set in draw(), read by the passes)
	srvmodels::FrameTextures m_modelsCam;
	// epic 0431, ticket 4554: the trees (null: render_forest=0); made once, kept across device rebuilds (records, layouts)
	std::unique_ptr<SceneForest> m_forest;
	// epic 0528, ticket 4852: the farm fields (null: render_farmfields=0); made once, its worker kept across device rebuilds
	std::unique_ptr<FarmFieldsProducer> m_farm;
	// ticket 4853: the farm footprint version last handed to CutOverlaySlot() (srv_cut_tap.h), and how often
	uint64_t m_farmCutSeen = 0;   // the producer's version before its first footprint
	int64_t m_farmCutSets = 0;
	// the decal set the residency gets: the tap's with the server's farm fields (FarmFieldsProducer::composeDecals, the
	// tap's own farm records dropped) and the forest's Tree_Shadow decals (SceneForest::composeDecals)
	std::shared_ptr<const c5r::decal::DecalSet> composedDecals(const std::shared_ptr<const c5r::decal::DecalSet>& tap)
	{   // ticket 4934: roads -> farm -> forest (the roads' set drops the tap's route curves first)
		const std::shared_ptr<const c5r::decal::DecalSet> withRoads = m_roads && tap ? m_roads->composeDecals(tap) : tap;
		const std::shared_ptr<const c5r::decal::DecalSet> withFarms = m_farm && withRoads ? m_farm->composeDecals(withRoads) : withRoads;
		return m_forest && withFarms ? m_forest->composeDecals(withFarms) : withFarms;
	}
	void forestDecalPoll();   // the forest's decal changes: the composed set to the residency, then the change (P13)
	void farmDecalPoll();     // ticket 4852: the farm producer's finished production and changes, as forestDecalPoll
	// ticket 4934: FixRoadLayout's producer (null: render_roadlayout=0); made once, its worker kept across device rebuilds
	std::unique_ptr<srvextras::SceneRoads> m_roads;
	uint64_t m_roadsCutSeen = 0;
	void roadsDecalPoll();    // its finished production and changes, as farmDecalPoll
	// epic 0454, ticket 4938: the extras (null: no extra in effect, or server/src/extras/ not linked); made once by
	// configureExtras, kept across device rebuilds
	std::unique_ptr<srvextras::SceneExtras> m_extras;
	bool m_extrasConfigured = false;
	void extrasDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	                const SceneFrame& f);
	// ticket 0199 part 2: the arc (m_arcR null: render_arc=0 or its start failed)
	void arcInit(ID3D11Device* dev, const SceneOptions& o);
	void arcDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	             const SceneFrame& f);
	std::unique_ptr<c5r::arc::Renderer> m_arcR;
	c5r::arc::Arrow m_arc;
	bool m_arcSettingsRead = false;
	bool m_arcPending = false;
	int32_t m_arcPend[4] = {};
	double m_arcLastMs = -1;
	SceneArcCounters m_arcC;
	std::string m_arcLastErr;
	// ticket 0300 part 2: the move-order path (m_pathR null: render_path=0 or its start failed)
	void pathInit(ID3D11Device* dev, const SceneOptions& o, const c5r::gamefs::GameFiles* files);
	void pathDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
	              const SceneFrame& f);
	void pathFail(int64_t& counter, const char* what, const c5r::Status& st, int64_t frameId);
	// The scene's depth before the trees and the units (path-models.md 7.4: stock's path is not hidden by trees): copied
	// into m_pathDepth when a path is shown, and bound by pathDraw instead of the scene's depth. Released with the device.
	void pathSaveDepth(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);
	// ticket 0753 (J3): the copy's decision and its texture without the copy (pathSaveDepth = this + the copy); a recorded
	// pathDraw needs the decision before the copy's place, so record ahead takes it early (m_pathDepthAhead) and the copy
	// stays at its place on the immediate context
	bool pathDepthPrepare(ID3D11DepthStencilView* dsv);
	ID3D11Texture2D* m_pathDepthTex = nullptr;
	ID3D11DepthStencilView* m_pathDepth = nullptr;
	bool m_pathDepthSaved = false;
	bool m_pathDepthAhead = false;   // ticket 0753: m_pathDepthSaved was decided by record ahead this frame
	int m_pathPlacedLogs = 0;
	std::unique_ptr<c5r::pathmodels::PathModels> m_pathM;     // kept across a device rebuild (CPU data)
	std::unique_ptr<c5r::pathmodels::PathRenderer> m_pathR;
	struct PathNodeIn { int16_t x, y, turn; bool embark; };
	std::vector<PathNodeIn> m_pathAsm;      // the update being assembled
	uint32_t m_pathAsmUpdate = 0, m_pathAsmCount = 0;
	std::vector<PathNodeIn> m_pathNodes;    // the last whole path
	uint32_t m_pathBits = 0;                // its common bits (tested, attack of the last node; for an empty path the cursor's)
	int16_t m_pathMouseX = -1, m_pathMouseY = -1;
	bool m_pathShown = false, m_pathChanged = false;
	std::vector<c5r::pathmodels::Instance> m_pathInst;
	double m_pathClock = 0, m_pathLastMs = -1;
	ScenePathCounters m_pathC;
	std::string m_pathLastErr;
public:
	const ScenePathCounters& pathCounters() const { return m_pathC; }
	// The host's drained feed batch (every one, after arcFeed): kUiMirror kind 8 (the path's nodes with the EXE's tests),
	// kind 4 (DisplayMovementIndicator's show / hide), kLoadBegin (both cleared). Nothing with render_path=0.
	void pathFeed(const SrvFeed::Batch& b);
private:

	bool applyField(uint32_t tilePlots);
	uint32_t wantTilePlots() const;
	void tapPoll();
	void writeTapAck(uint32_t loadSeq, uint32_t copySeq, int32_t result);

	HANDLE m_section = nullptr;
	std::wstring m_sectionName;   // ticket 0657: attach's name (the farm producer makes <name>.Farm beside it)
	VPShellHeader* m_hdr = nullptr;
	ID3D11Device* m_dev = nullptr;
	SceneOptions m_opt;
	c5r::terrain::Renderer* m_r = nullptr;
	bool m_rInit = false;
	std::unique_ptr<ExeCacheTap> m_tap;
	std::unique_ptr<ResidencySource> m_res;   // issue 3308: render_tiles=bake
	int64_t m_tapPrev[5] = {};   // cache opened / missing / refused, manifest errors, plots changed of released sources
	// issue 3308: the residency's totals of released sources (device rebuilds) and the source failures
	int64_t m_resPrev[6] = {};   // bakes, rebakes, failures, gray frames, gray frames after the bake-ahead, plots changed
	int64_t m_sourceFailures = 0;
	void releaseSource();
	int64_t m_plotsChangedNoSource = 0;
	c5r::heightfield::HeightGrid m_grid;   // kept: a tile-size change and a device rebuild rebuild the tiles from it
	bool m_haveGrid = false, m_haveField = false;
	uint32_t m_fieldTilePlots = 0, m_fieldLoadSeq = 0;
	// camera
	VPShellCamera m_cam = {};
	bool m_haveCam = false;
	uint32_t m_lastCamSeq = 0;
	int64_t m_lastDrawnStamp = -1, m_lastPc = -1;
	int64_t m_fieldHoldFrame = 0;   // ticket 0691 (cand4): the camera frame decide() refused for the field (0: none)
	bool m_dirty = false;
	int m_state = -1;
	// tap
	bool m_tapAckedAny = false;
	uint32_t m_tapAcked = 0, m_tapAckedCopy = 0;
	bool m_tapRequested = false;
	uint32_t m_tapRequestLoad = 0, m_tapRequestCopy = 0;
	int32_t m_tapLastResult = 0;
	uint32_t m_tapLoggedState = 0xFFFFFFFFu;
	// per second
	int m_drawnSec = 0, m_drawErrSec = 0, m_staleSec = 0, m_hiddenSec = 0, m_blocksSec = 0;
	double m_gpuSumSec = 0, m_gpuMaxSec = 0, m_cpuSumSec = 0;
	int m_gpuNSec = 0;
	c5r::terrain::FrameStats m_lastStats;
	WaterScene m_water;   // issue 3607: the tap reader and the water module
	void (*m_fieldSink)(void*, uint32_t, const uint8_t*, size_t) = nullptr;   // epic 0301, issue 3904
	void* m_fieldSinkCtx = nullptr;
	std::string m_lastDrawError;
	// epic 0465, ticket 4907: live terrain (srv_liveterrain_scene.cpp; called by poll() in the residency's block)
	void liveTerrainStep();
	std::unique_ptr<liveterrain::LiveTerrain> m_live;
	bool m_liveTried = false, m_liveHaveLoad = false, m_liveTableWaitNoted = false;
	uint32_t m_liveLoad = 0, m_liveTableGen = 0;
	int64_t m_livePlotsSeen = 0;
	std::unique_ptr<liveterrain::Replay> m_liveReplay;   // the offline player's (render_live_terrain_journal)
	std::vector<liveterrain::FrameGen> m_liveReplayFrames;
	std::unique_ptr<liveterrain::ExeLink> m_liveExe;   // ticket 4905's EXE field target of the current load
	// ticket 4909 (srv_liveterrain_scene.cpp, live-terrain.md 8.14): the live apply's region path. A change is applied in
	// two steps: the EXE write and the window into m_grid on the render thread, the height pages the window reaches built
	// on a worker from m_grid (read only while the job runs: tapPoll joins it before a new load's field replaces m_grid),
	// then at a later poll the pages uploaded and the region updates of the terrain, the residency, the water and the
	// trees. applyFieldRegion is that last step (false: why; the caller re-applies the whole field, LOUD).
	std::unique_ptr<liveterrain::ApplyJob, liveterrain::ApplyJobDelete> m_liveJob;
	bool m_liveRegionTried = false, m_liveRegion = true, m_liveAsync = true;
	bool applyFieldRegion(const std::vector<c5r::terrain::SampleRect>& rects, const std::vector<c5r::bake::HeightPage>* built,
	                      std::string& why, std::string& line);
	void liveTerrainFinish(bool discard);   // the pending job's last step (discard: a new load; joins the worker)
	void liveTerrainJoin();                 // tapPoll, before m_grid is replaced
public:
	const liveterrain::LiveTerrain* liveTerrain() const { return m_live.get(); }   // null: off (tests, the totals)
	// The offline player: the bundle's journal (render_config render_live_terrain_journal), after the load's field is in;
	// then before each frame's poll() the field generation the live server drew at that frame (the journal's jobs run
	// through the producer, each written window's hash checked; a mismatch is an error, never a tolerance). Errors:
	// DecodeSpanText's (BadLayout), no field yet (NotFound), the Replay's.
	c5r::Status liveTerrainReplayInit(const std::string& journalText);
	c5r::Status liveTerrainReplayTo(int64_t frameId, uint32_t* generation = nullptr);
};

}  // namespace srvrender
