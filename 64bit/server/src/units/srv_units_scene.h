// srv_units_scene.h - the server's unit scene as the terrain scene sees it (epic 0375, issue 4454; design:
// research/05-graphics-composition/unit-server-scene.md sections 3, 7, 8, 9, 10). Written 2026-11-15 for this
// repository (GPL-3.0-or-later).
//
// Scene (srv_render_scene.h) owns one SceneUnits (Scene::m_units) while server.cfg render_units=1. Everything the unit
// scene does is behind this small interface, so that this header names nothing of 0041's module (c5r/units/*.h, which
// needs models/include): every render unit can include it, and the test programs of other teams that compile
// server/src/render/*.cpp without server/src/units/ still link (MakeSceneUnits then returns null, see below).
//
// The implementation (srv_units_scene.cpp, srv_units_core.cpp; srv_units_core.h for the tests): the renderer's copy of the
// unit and player tables (SrvFeed::CopyUnits / CopyPlayers after every batch that carries a unit event), the seed allocator
// (srvunits::SeedAllocator, issue 4453) fed per event in seq order and advanced per frame, the colour lookup (player colour
// index -> PlayerColors row -> Type; ColourUnknown, never a default), the art style and the era per player, the visibility
// fades on the frame clock, the view cull, the c5r::units::UnitScene of each drawn frame, the pre-check on a worker thread
// at each new unit database, the drawing renderer (prepare, draw) into the sink's colour and depth, the counters and the
// statuses of the note's section 10, and the readiness signal for 0040 (ready()). It writes no mode word.
// Threads: every call on the render thread, except the pre-check's own worker (joined in poll() or the destructor) and
// beginBody, which a frame job may run on a worker of the scene's pool while it owns the module (ticket 0711, R2).
#pragma once
#include <d3d11.h>
#include <stdint.h>
#include <map>        // issue 4986 (H1): UnitExtrasFrame::airInterceptRange
#include <memory>
#include <string>
#include <vector>

#include "feed/srv_feed.h"

namespace c5r { namespace heightfield { struct HeightGrid; } }
namespace c5r { namespace units { class PlacementSource; } }   // render/include/c5r/units/units.h (issue 4983 (d))
namespace srvrender { struct FeedSource; }   // srv_scene_sources.h (issue 4457 / 4456: the recording port)
namespace srvrender { class FrameJobOwner; struct FrameJobResult; }   // srv_frame_jobs.h (ticket 0711, J1)
namespace Vp64Opt { struct Options; }        // options/include/vp64_options.h (issue 4981: the unit extras' states)

namespace srvunits {

// What the host passes in (server.cfg, srv_render_host.cpp ParseHostConfig / SceneOpts).
struct UnitsOptions
{
	bool passes = false;                 // render_units_passes=1: the reflection pass too (issue 4455; decision 7 (c))
	std::wstring exe;                    // render_exe: CivilizationV_DX11.exe (the unit programs), read-only
	std::wstring install;                // the game install (art files), read-only
	bool modsAuto = true;                // the MODS folders as extra loose roots (srvrender::ModFolders()); false: `mods` only
	std::vector<std::wstring> mods;      // extra loose roots (tests; with modsAuto false)
	std::wstring dumpDir;                // render_test_units_dump: each drawn frame whose UnitScene changed -> .c5units
	int dumpMax = 1000;                  // render_test_units_dump_max: files at most (then LOUD once, no more)
	std::wstring dbOverride;             // render_test_units_db: a database file instead of SrvFeed::GetUnitDatabase (tests)
	bool anim = true;                    // render_units_anim (issue 4456, default 1): 0374's controller chooses every figure's clip
	                                     // and time (unit-server-scene.md 6); 0 = no controller, the module's default clip at t 0
	                                     // (TEST ONLY: decision 9's forced-pose comparison)
	std::wstring animFile;               // render_test_units_anim (issue 4456, TEST ONLY): the controller's input as 0374's
	                                     // .c5anim (every frame the scene applied), rewritten (tmp + rename) at the second line
	std::wstring tableFile;              // render_test_units_table (issue 4460): the scene's unit and player tables, its seeds
	                                     // and the feed's per-kind counts, rewritten (tmp + rename) at the second line when
	                                     // they changed: the live checks read it (TEST ONLY)
	bool animAll = false;                // issue 4468 (tests only, no configuration key): the controller builds programs for
	                                     // every held unit, not only the frame's drawn set (the behaviour before 4468)
	bool positionsExe = false;           // epic 0040, issue 4755: render_units_positions=exe: each unit's root at its newest EXE
	                                     // position record, move / arrive timed by the EXE's records (unit-server-scene.md 17);
	                                     // false = own (0375's). server.exe's factory sets it from the value ParseHostConfig read
	                                     // (srvrender::RenderUnitsPositionsOption(), the same value SceneOptions::unitsPositions
	                                     // and so the mode word's writer get); tests and unitfeed set it directly
	std::wstring triggerSection;         // epic 0042, ticket 4722: <base> of the frame section: the trigger stream on <base>.Trig
	std::string triggerLog;              // ticket 4722: the offline trigger log; both empty = no stream (the scene as before)
	bool triggerTierGate = true;         // ticket 4737: the emitter's tier gate (EmitterParams::tierGate; render_test_units_tier_gate)
	bool chargeAny = true;               // ticket 4737 (rule A; on again since 4743 W81): CombatParams::charge_any
	                                     // (AnimRunner::setChargeAny; render_test_units_charge_any; sceneshot: the
	                                     // bundle's key, absent = false)
	bool turnFirst = true;               // ticket 4735 (rule A): CombatParams::turn_first (AnimRunner::setTurnFirst;
	                                     // render_test_units_turn_first; sceneshot: the bundle's key, absent = false)
	bool fightDrawnFigures = true;       // ticket 4744 (rule A): CombatParams::fight_drawn_figures (AnimRunner::
	                                     // setFightDrawnFigures; render_units_fight_drawn_figures; sceneshot: the bundle's
	                                     // key, absent = false)
	bool reformFirst = true;             // tickets 4745 + 4747 (rule A): CombatParams::reform_first (AnimRunner::
	                                     // setReformFirst; render_test_units_reform_first; sceneshot: the bundle's key,
	                                     // absent = false)
	bool walkHome = true;                // ticket 4742 (rule A): CombatParams::walk_home (AnimRunner::setWalkHome;
	                                     // render_test_units_walk_home_in_fight; sceneshot: the bundle's key, absent = false)
	bool cityInPlace = true;             // ticket 4748 (rule A): CombatParams::city_in_place (AnimRunner::setCityInPlace;
	                                     // render_test_units_city_in_place; sceneshot: the bundle's key, absent = false)
	bool defendFromStance = true;        // ticket 4750 (rule A): CombatParams::defend_from_stance (AnimRunner::
	                                     // setDefendFromStance; render_test_units_defend_from_stance; sceneshot: the
	                                     // bundle's key, absent = false)
	bool reformEndsAtArrival = true;     // ticket 5050 (rule A; on since W74): CombatParams::reform_ends_at_arrival
	                                     // (AnimRunner::setReformEndsAtArrival; render_test_units_reform_ends_at_arrival;
	                                     // sceneshot: the bundle's key, absent = false)
	bool attackFromStance = true;        // ticket 5050 (rule A; on since W74): CombatParams::attack_from_stance (AnimRunner::
	                                     // setAttackFromStance; render_test_units_attack_from_stance; sceneshot: the
	                                     // bundle's key, absent = false)
	bool rushRideThrough = true;         // ticket 5057 (rule A): CombatParams::rush_ride_through (AnimRunner::
	                                     // setRushRideThrough; render_units_rush_ride_through; sceneshot: the
	                                     // bundle's key, absent = false)
	bool defenderWaitsFirstLanding = true;   // ticket 5057 (rule A): CombatParams::defender_waits_first_landing
	                                     // (AnimRunner::setDefenderWaitsFirstLanding;
	                                     // render_units_defender_waits_first_landing; sceneshot: absent = false)
	bool deathByVariant = true;          // ticket 5053 (rule A, W112; on, MC 2026-11-25): CombatParams::death_by_variant (AnimRunner::
	                                     // setDeathByVariant; render_test_units_death_by_variant; sceneshot: absent = false)
	bool endAfterDying = true;           // ticket 5053 (rule A, W112; on, MC 2026-11-25): CombatParams::end_after_dying (AnimRunner::
	                                     // setEndAfterDying; render_test_units_end_after_dying; sceneshot: absent = false)
	bool victoryNeedsWord = true;        // ticket 5059 (rule A): CombatParams::victory_needs_word (AnimRunner::
	                                     // setVictoryNeedsWord; render_units_victory_word; sceneshot: the bundle's
	                                     // key, absent = false)
	bool fightAfterWalk = true;          // ticket 5066 (rule A): the fighter's walks around its fight as stock, mode exe
	                                     // (AnimFeed::setFightAfterWalk; render_units_fight_after_walk; sceneshot: the
	                                     // bundle's key, absent = false)
	bool meleeReach = true;              // ticket 0743 (rule A): CombatParams::melee_reach (AnimRunner::setMeleeReach;
	                                     // render_units_melee_reach; sceneshot: absent = false)
	bool defenderStep = true;            // ticket 5061 (rule A): CombatParams::defender_step (AnimRunner::setDefenderStep;
	                                     // render_units_defender_step; sceneshot: absent = false)
	bool type2Phase2 = true;             // ticket 5067 S1 (rule A; on, MC #12907): CombatParams::type2_phase2
	                                     // (AnimRunner::setType2Phase2; render_units_type2_phase2; sceneshot: absent = false)
	bool ordersAfterWalk = true;         // ticket 5065 (rule A): in mode exe a walking unit's work and fortify wait for its
	                                     // walk's end (AnimFeed::setOrdersAfterWalk; render_units_orders_after_walk;
	                                     // sceneshot and unitfeed: the bundle's key, absent = false)
	bool readyAfterMove = true;          // ticket 4746 (rule A): CombatParams::ready_after_move (AnimRunner::
	                                     // setReadyAfterMove; render_test_units_ready_after_move; sceneshot: the bundle's
	                                     // key, absent = false)
	bool triggerStrikerWeapon = true;    // ticket 5052 (rule A, W): TriggerStreamConfig::strikerWeapon (a non-striking
	                                     // unit's $(WEAPON) from its opponent's figure 0; render_test_units_trigger_
	                                     // striker_weapon, default 1 since MC #10847; sceneshot: the bundle's key,
	                                     // absent = false: no drawn pixel depends on it)
	bool triggerCityEra = true;          // ticket 5058 (rule A): TriggerStreamConfig::cityEra (a Sound $(WEAPON) a city
	                                     // strikes: the city's weapon by its owner's era; render_units_trigger_city_era,
	                                     // default 1; sceneshot: the bundle's key, absent = false: no drawn pixel
	                                     // depends on it)
	bool triggerCityShot = true;         // ticket 5062 (rule A): AnimRunner::setCityShot (a city's own strike flies its
	                                     // owner era's Eras.CityBombardEffectTag: the shot and miss sounds, as stock;
	                                     // render_units_trigger_city_shot, default 1; sceneshot: the bundle's key,
	                                     // absent = false: the projectile is the EXE's effect, no pixel of the server's)
	bool triggerTerrain = true;          // ticket 5052 (rule A, T): TriggerStreamConfig::plotTerrain and the stream's
	                                     // UnitTerrainFn from setPlotTable's copy (a unit's $(TERRAIN) from its plot's
	                                     // feature, else terrain EffectTypeTag, else DIRT; render_test_units_trigger_
	                                     // terrain, default 1 since MC #11356; sceneshot: the bundle's key, absent =
	                                     // false: no drawn pixel depends on it)
	bool triggerStrikerMelee = true;     // ticket 5055 (rule A): TriggerStreamConfig::strikerMelee (every unit struck in
	                                     // its running fight named by its striker's weapon for a Sound; only with
	                                     // triggerStrikerWeapon on; render_units_trigger_striker_melee, default 1, MC
	                                     // #11607; sceneshot: the bundle's key, absent = false: no drawn pixel depends on it)
	bool triggerProjectilePose = true;   // ticket 5064 (rule A): RouteParams::projectilePose (a held projectile starts at
	                                     // its bone's transform and its member's scale; a launcher without a heading
	                                     // faces its target; render_units_projectile_pose, default 1; sceneshot: the
	                                     // bundle's key, absent = false: the projectiles are the EXE's effects, no pixel
	                                     // of the server's)
	bool triggerCityVolley = true;       // ticket 0742 (rule A): RouteParams::cityVolley (a city's volley flies from each
	                                     // shot's launch at the city volley's scale; render_units_city_volley, default 1;
	                                     // sceneshot: the bundle's key, absent = false: the EXE's effects, no server pixel)

	bool openAsync = false;              // ticket 0704: the drawing renderer opened, and the art rows of the table's units loaded
	                                     // and uploaded, on a worker thread after the pre-check passed (server.exe's factory
	                                     // sets it; no configuration key); the frames until it is taken are not drawn (not
	                                     // ready: the EXE draws its units, O6). false: opened on the render thread at the first
	                                     // frame that builds, as before (the offline tools and the tests, whose first built frame
	                                     // must be the recorded one's)

	float testOffset[2] = {0, 0};        // ticket 4785, render_test_units_offset=dx,dy (TEST ONLY): every server unit drawn (every
	                                     // pass) moved by (dx, dy) world units, to pose the server's figures away from the EXE's
	                                     // invisible ones for the live picture pair (unit-combat-server.md 13); LOUD once

	int quickCombat = 0;                 // ticket 0796 (Quick Combat Animations T1, D270): 0 off (every fight at real time, the
	                                     // rule before 0796) | 1 V2 (AnimRunner::setQuickCombat(QuickCombatV2()): the run-up
	                                     // faster, the first strike at 1x, the rest at 3x). render_test_units_quick_combat
	                                     // (TEST ONLY; sceneshot: the bundle's key, absent = 0); T2 0797 sets it from
	                                     // [Extras] QuickCombatAnimations; other values: off
};

// The camera of one draw (the terrain scene's SceneFrame, copied so that this header needs no shell header).
struct UnitsCamera
{
	float view[16] = {};                 // g_View[0]
	float proj[16] = {};                 // g_Proj
	float camWorld[16] = {};             // camera-to-world, row 3 = position (all 0: the inverse of view)
	float wrapPeriod = 0;                // world units between two wrap copies (<= 0 or not finite: no wrap)
	int64_t frameId = 0;                 // the composite's frame id (the camera block's frameStamp)
	double nowMs = -1;                   // the frame clock (QpcToMs scale); < 0: read in draw()
};

class SceneUnits
{
public:
	virtual ~SceneUnits() {}
	// The render device (null: device released). The drawing renderer is opened at the first draw after the device and
	// the pre-check's database are both there; releaseDevice() drops it and every GPU object (the tables stay).
	virtual void setDevice(ID3D11Device* dev) = 0;
	// One drained feed batch (every batch, in drain order) at the drain clock drainMs (QpcToMs scale; the recorder's value
	// when recording, so that feed.bin's drainMs is the scene's; < 0: read here). True when the unit list changed.
	virtual bool feed(const SrvFeed::Batch& b, double drainMs = -1.0) = 0;
	// Issue 4457 / 4456: the scene's table copies (CopyUnits, CopyPlayers) through this source from now on (the host's
	// recording feed with render_test_record, so that feed.bin holds the copies the scene took: records 3 and 6 of caller
	// 1); null = the port the scene was made with. The unit database is always asked of that port.
	virtual void useFeedSource(srvrender::FeedSource* src) = 0;
	// Every loop iteration: the unit database's generation (a new one starts the pre-check), the pre-check's end.
	virtual void poll() = 0;
	// Issue 4463 (unit-server-scene.md 12.1 [L]: the EXE does not draw other players' units on plots the active team does
	// not see): the active team's fog state per plot as the fog scene holds it (FowScene::states(), c5r::fow::PlotState:
	// 0 visible, 1 seen, 2 unexplored; n = width x height plots of the feed's grid) and its generation
	// (FowScene::fogGeneration(); the same generation and size are not copied again). The caller (the terrain scene,
	// before each beginFrame) keeps nothing alive: the states are copied here. From the next beginFrame a unit whose
	// owner's team is not the active team (PlayerRecord::team, activeTeam) is listed only when its plot's state is 0.
	// states null or n 0 (no fog scene: render_fow=0, the tests): no filter, said once in the log, counted per frame.
	virtual void setPlotStates(const uint8_t* states, size_t n, uint32_t width, uint32_t height, uint32_t gen) = 0;
	// Ticket 5052 (T): the plot table as the fog scene read it (FowScene::plotTable: its copy of SrvFeed::CopyPlots; n =
	// width x height plots of the feed's grid) and its key (the fog scene's read count: the same key, width and size are
	// not copied again): the terrain and feature of each plot, kept (PlotTerrainCopy below) for the trigger stream's
	// $(TERRAIN) while UnitsOptions::triggerTerrain is on (the scene copies nothing with it off). The caller keeps nothing
	// alive. plots null or n 0: no table (every unit DIRT, counted plotNoTable). Called by the terrain scene beside both
	// setPlotStates calls before beginFrame (srv_render_scene.cpp HandPlotTable); sceneshot opens no trigger stream and hands
	// none (unit-combat-server.md 18.25.1, 18.25.3).
	virtual void setPlotTable(const SrvFeed::PlotRecord* plots, size_t n, uint32_t width, uint32_t height, uint32_t gen)
	{
		(void)plots, (void)n, (void)width, (void)height, (void)gen;
	}
	// Issue 4455: the frame's lists and plans (build, prepare) once per drawn frame, before the water, so that the
	// reflection pass and the scene pass draw the same plans. drawScene without a beginFrame of its own calls it itself.
	virtual void beginFrame(const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid) = 0;
	// Ticket 0711 (J1; research/07-architecture-decisions/architect-2026-11-23-frame-jobs.md R1-R9): beginFrame in two
	// parts, so that its CPU work runs as a frame job (srv_frame_jobs.h) between the fork after Scene::decide and the join
	// before its first use. beginHead runs on the render thread: the frame stamp, the readiness checks, the drawing
	// renderer's open, route S's poll (the only IPC read of the begin, R6). True: beginBody must follow before any other
	// call into this module (it is the rest of beginFrame: the build, the fog filter, the controller, the three prepares, the
	// extras' transforms), on any one thread, with the camera, the grid and the clock beginHead stored by value (R1); false:
	// nothing more to do this frame (as beginFrame's early returns). beginFrame(cam, grid) == beginHead + beginBody(nullptr).
	// lines non-null: every log line of the body is appended there instead of written (R6; the caller writes them at the
	// join through beginJoined). The defaults: the whole begin in the head, no body (a scene without a job).
	virtual bool beginHead(const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid)
	{
		beginFrame(cam, grid);
		return false;
	}
	virtual void beginBody(std::vector<std::string>* lines) { (void)lines; }
	// After the join, on the render thread: the body's lines written in order, and the job's numbers for the `units 1s`
	// line (jobs, job_ms, join_wait_ms, inline).
	virtual void beginJoined(const srvrender::FrameJobResult& r) { (void)r; }
	// R2: the module's owner flag (null: no checks); a frame job that runs beginBody names it.
	virtual srvrender::FrameJobOwner* jobOwner() { return nullptr; }
	// Issue 4455 (decision 7 (c), render_units_passes=1): the reflection pass into the water's reflection target
	// (size x size, cleared by the water scene): the frame's plans with cb1 r4-r7 = the EXE's ShadowMatrix1 (0xDA1890; the
	// reflection pass's view-projection, unit-draws.md 1), moved by k W for wrap copy k as the scene pass; the other
	// registers as the scene pass's (equal in the three passes, 24 of 24 exports). shadowMatrix1 null: no draw,
	// ReflectionMatrixMissing (LOUD once, counted per frame). Nothing without a beginFrame of this frame.
	virtual void drawReflection(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t size,
	                            const float* shadowMatrix1) = 0;
	// The scene pass into the sink's colour and depth (after the water and the ice, before the cloud planes).
	// grid = the scene's kept height field (null: no units drawn, counted). Errors are counted and LOUD; never thrown.
	virtual void drawScene(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
	                       uint32_t h, const UnitsCamera& cam, const c5r::heightfield::HeightGrid* grid) = 0;
	// Ticket 4785 (unit-combat-server.md 9 and 13): the shadow-mask pass (the module's PassKind::ShadowMask: the EXE's pass-1
	// programs, no depth, blend off) of the frame's plans into rtv (w x h: the EXE's ShadowRaw size; cleared, or at quality 1
	// the tapped map's copy, by the caller) with cb1 r4-r7 = shadowMatrix0 (the tapped ShadowMatrix0 of view 0, row-major as
	// the EXE stores it), moved by k W for wrap copy k as the reflection pass. Returns true when every planned copy drew;
	// false without a beginFrame of this frame or with shadowMatrix0 null (nothing drawn). Errors are counted and LOUD.
	virtual bool drawMask(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h, const float* shadowMatrix0)
	{
		(void)ctx, (void)rtv, (void)w, (void)h, (void)shadowMatrix0;
		return false;
	}
	// The readiness for 0040's mode word (note section 8): the pre-check passed, the module open, the table taken after
	// the current load, the last prepare / draw a frame-level Ok, no D125 failure.
	virtual bool ready() const = 0;
	// True once a failure of D125's route happened (PrecheckFailed); why = the text for HostConfig::fatal.
	virtual bool failed(std::string& why) const = 0;
	// A fade is running: the next frame differs even without a feed change.
	virtual bool animating() const = 0;
	virtual void secondLine(char* out, size_t n) = 0;    // "units 1s: ..." (render.log once per second)
	virtual void totalsLine(char* out, size_t n) = 0;    // "units totals: ..." (render.log at the end)
};

// The factory: srv_units_scene.cpp registers it at static initialisation (server.exe and the unit tests link it). A
// program that does not link server/src/units/ gets null (srv_render_host.cpp then refuses render_units=1 with a named
// reason: never a silent game without units).
typedef std::unique_ptr<SceneUnits> (*SceneUnitsFactory)(const UnitsOptions& o);
void RegisterSceneUnitsFactory(SceneUnitsFactory f);     // defined in srv_render_scene.cpp (always linked)
bool SceneUnitsAvailable();
std::unique_ptr<SceneUnits> MakeSceneUnits(const UnitsOptions& o);
// Issue 4755: the factory srv_units_scene.cpp registers (server.exe's scene: the live feed, the install, RNote; the position
// source from render_units_positions as ParseHostConfig read it), callable by name for the tests (another case may have
// registered its own factory). It turns the DLL side's unit writers on (SrvFeed::SetUnitsEnabled(true)).
std::unique_ptr<SceneUnits> MakeSceneUnitsLive(const UnitsOptions& o);

// D125's route for a unit pre-check failure (note section 10, PrecheckFailed): beside ShadowFatalStep in the host's loop.
const int kUnitsFatalExitCode = 5;

// ---- The fog filter of the drawn list (issue 4463; one rule for the server and unitfeed since issue 4470) --------------
// unit-server-scene.md 12.1 [L]: the EXE does not draw another team's unit on a plot the active team does not see.
// Defined in srv_units_scene.cpp; UnitsHost::beginFrame (server.exe) and server/tools/unitfeed both call it after
// UnitsCore::build, so the replay of a bundle lists what the live server listed.
struct BuiltFrame;   // srv_units_core.h

struct FogFilterResult
{
	const char* state = "none";   // "on" (filtered), "none" (no states), "no-team" (no active team), "grid" (not the feed's grid)
	const char* why = nullptr;    // not "on": the line said once per reason ("units: ..."); the caller counts the frame
	size_t hidden = 0;            // "on": units removed (unique keys over the three wrap copies)
};
// A unit whose owner's team is not the active team stays in a wrap copy only when its plot's state is 0 (visible). The
// active team is the first player row's activeTeam (the same in every row); the owner's team its row's team (a unit with
// no row is filtered by its plot). states = the active team's c5r::fow::PlotState per plot (0 visible, 1 seen, 2
// unexplored), n = width x height, of the feed's grid (width == feedWidth); null or n 0: nothing filtered ("none"), as
// with no active team or another grid. A scene's player inputs with no listed unit left are dropped; `listed` loses the
// hidden units (units already culled stay counted as culled).
FogFilterResult FilterByFog(BuiltFrame& built, const std::vector<SrvFeed::PlayerRecord>& players, uint32_t feedWidth,
                            const uint8_t* states, size_t n, uint32_t width, uint32_t height);

// The fog scene's plot states from one of its table reads (srv_fow_scene.cpp FowScene::readTable's rule: each plot's
// c5r::fow::stateFromFowBits(PlotRecord::fow), bits above 1 ignored). False (states unchanged) for an empty table or one
// that is not width x height plots, as the fog scene keeps its states then. unitfeed's states from feed.bin's fog reads
// (PLOTS of caller kPlotsFog), the same values FowScene::states() held when the server gave them to setPlotStates.
bool FogStatesOfTable(const std::vector<SrvFeed::PlotRecord>& table, uint32_t width, uint32_t height, std::vector<uint8_t>& states);

// ---- Ticket 5052 (T): a unit's plot terrain and feature for $(TERRAIN) (one rule for the server and unitfeed) ----------
// Defined in srv_units_scene.cpp. UnitsHost::setPlotTable keeps the fog scene's table in one (server.exe); unitfeed keeps
// feed.bin's fog table reads (PLOTS of caller kPlotsFog: the records FowScene::readTable copied) in one, so the replay names
// a unit's plot as the live server did. unit-combat-server.md 18.25.1, 18.25.3.
class UnitsCore;   // srv_units_core.h
class PlotTerrainCopy
{
public:
	// The table (n = width x height records) under its key: each plot's terrain and feature copied (2 bytes a plot) only
	// when the key, the width or n changed; true when copied. plots null, n 0 or n not width x height: no table (false).
	bool set(const SrvFeed::PlotRecord* plots, size_t n, uint32_t width, uint32_t height, uint32_t key);
	// The unit's plot (its record in core) in the copy: false with no table, no record, a plot outside the table, or a core
	// on another grid's width (the caller's DIRT, counted plotNoTable).
	bool unitPlot(const UnitsCore& core, int32_t player, int32_t unit, int32_t& terrain, int32_t& feature) const;
	bool have() const { return m_have; }
	uint64_t copies() const { return m_copies; }

private:
	std::vector<int8_t> m_tf;   // 2 per plot: terrain, feature
	uint32_t m_w = 0, m_key = 0;
	bool m_have = false;
	uint64_t m_copies = 0;
};

// ---- The units seam (epic 0455, issue 4981; research/05-graphics-composition/extras-units.md section "4981") ----------
// The unit extras ([Extras] options 23-26, added by their own tickets) change what the unit scene draws through this one
// interface, implemented under server/src/extras/units/ and made through MakeUnitsExtras(): a program that does not link
// that folder gets null and draws exactly as before (the factory pattern of SceneUnits above). Every call on the render
// thread. The unit host (srv_units_scene.cpp) calls configure once at its start; UnitsCore calls onBatch after it took each
// batch, beginFrame and unit() in each build, before the cull and the fog filter.
struct UnitExtrasFrame
{
	const c5r::heightfield::HeightGrid* grid = nullptr;       // the scene's height field (null: no grid this frame)
	const std::vector<SrvFeed::UnitRecord>* units = nullptr;   // the scene's unit table, sorted by (owner, id)
	const std::vector<SrvFeed::PlayerRecord>* players = nullptr;   // by player index (player -1 = no record)
	uint32_t width = 0, height = 0;                           // the feed's plot grid
	double tMs = 0;                                           // the frame clock
	int64_t frameId = 0;                                      // the drawn frame's id
	// Issue 4986 (hunk H1): Units ID -> AirInterceptRange from the scene's database (Lookups::airInterceptRange; empty when
	// the column could not be read); null: none
	const std::map<int32_t, int32_t>* airInterceptRange = nullptr;
	// Issue 4986 (MC #11389): UnitPromotions.Type of the promotions that change the intercept range (a mod's; none in VP), not
	// included in airInterceptRange; null: not checked (the column could not be read)
	const std::vector<std::string>* airInterceptPromotions = nullptr;
};
// One unit of the table as the scene would draw it this frame. The hook runs after the scene set the unit's own root and
// heading (lead-0042's contract, extras-units.md 4981.1): they are inputs here.
struct UnitExtraUnit
{
	const SrvFeed::UnitRecord* rec = nullptr;
	int32_t plotX = -1, plotY = -1;   // the plot the scene draws it at (-1: outside the grid)
	bool listed = false;          // stock lists it this frame (visible, fading in or fading out)
	bool hiding = false;          // stock hides it or fades it out (the visible flag is clear; extras-units.md 4981.2)
	bool hasRoot = false;         // mode exe: the newest position record; false: the plot centre
	float rootX = 0, rootY = 0;
	int8_t facing = 0;            // the heading the scene gives it (DirectionTypes; c5r::units::facingYaw gives its yaw)
	bool heldInFight = false;     // epic 0042 (4774): held by a fight; the seam ignores every override of such a unit
};
// What the extras want for that unit (in and out: each extra sees the earlier ones' changes). Defaults: stock.
struct UnitOverride
{
	bool show = false;            // list a unit stock hides (hiding true), opaque; refused for a dead unit and for an invisible
	                              // unit of another team; the residency, seed and fog rules stay (extras-units.md 4981.3)
	bool root = false;            // the root at (rootX, rootY): the module's placement puts the slots around it and samples
	float rootX = 0, rootY = 0;   // the heights there (UnitInput::has_root)
	bool heading = false;         // the unit turned about its root to yaw (radians about +z, the placement's yaw:
	float yaw = 0;                // 150 - 60 facing degrees for the facings) after prepare
	float scale = 1;              // uniform, about the root, after prepare; (0, kMaxUnitExtraScale]
	uint8_t perimeterEdges = 0;   // issue 4983 (d): the living figures spread along these edges of the unit's plot (bits 0..5
	                              // E, NE, NW, W, SW, SE), facing out, by the module's placement decorator; 0 = as placed
	float lift = 0;               // issue 4986 (hunk H2): world units the whole unit is raised after prepare (after the turn
	                              // and the scale), [0, kMaxUnitExtraLift]; 0: as before
	float footRadius = 0;         // issue 4982 (FixUnitPlacement): > 0: each figure on land stands on the highest ground within
	                              // this radius of its origin (UnitInput::foot_radius, PlacementInput::footRadius); 0 = the
	                              // EXE's rule; [0, kMaxUnitExtraFootRadius]
};
const float kMaxUnitExtraScale = 4.0f;
const float kMaxUnitExtraLift = 200.0f;
const float kMaxUnitExtraFootRadius = 16.0f;   // = placement.cpp's own check
typedef void (*UnitsExtrasLogFn)(void* ctx, const char* line);

class UnitsExtras
{
public:
	virtual ~UnitsExtras() {}
	virtual void setLog(UnitsExtrasLogFn log, void* ctx) = 0;
	// Each extra's effective state (Vp64Opt::Extra) once; unitsAreOurs false (ServerUnits = 0): every unit extra off with
	// one line "off: ServerUnits = 0" (E5).
	virtual void configure(const Vp64Opt::Options& o, bool unitsAreOurs) = 0;
	virtual bool anyOn() const = 0;   // an extra in effect and not failed (the host drops the module otherwise)
	virtual size_t inEffect() const = 0;   // how many (the host's attach line)
	virtual void onBatch(const SrvFeed::Batch& b) = 0;
	virtual void beginFrame(const UnitExtrasFrame& f) = 0;
	// Never throws. An extra that fails (returns false, throws, or gives a value outside the override's ranges) is off for
	// the session with one line and the unit keeps what the extras before it gave (E6).
	virtual void unit(const UnitExtraUnit& u, UnitOverride& io) = 0;
	virtual bool animating() const = 0;   // an extra moves something without a feed change: the next frame differs
	// Counts per extra for the "units 1s" and "units totals" lines: " | extras <Name> on|off|failed ov=.. shown=.. scaled=..
	// failures=..". Empty without an extra.
	virtual void countsText(char* out, size_t n) const = 0;
	// Issue 4983 (d): true when an extra in effect places figures itself (UnitOverride::perimeterEdges); then the host
	// gives the drawing renderer placementOver(its source) instead of its source. Null or false: as before.
	virtual bool wantsPlacement() const { return false; }
	virtual c5r::units::PlacementSource* placementOver(c5r::units::PlacementSource* inner) { (void)inner; return nullptr; }
};
typedef std::unique_ptr<UnitsExtras> (*UnitsExtrasFactory)();
void RegisterUnitsExtrasFactory(UnitsExtrasFactory f);   // defined in srv_units_scene.cpp (the units module)
std::unique_ptr<UnitsExtras> MakeUnitsExtras();          // null without server/src/extras/units/

}  // namespace srvunits
