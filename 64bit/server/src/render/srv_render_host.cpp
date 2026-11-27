// srv_render_host.cpp - the render thread inside server.exe (issue 3301). Contract and rules: srv_render_host.h.
// The loop, the adapter pick and the memory line follow shell/server/shell_server.cpp's wmain (PickAdapter, LogMemory,
// the 0.5 ms legacy pacing) at commit b4f7748; that file is unchanged.
#include "srv_render_common.h"
#include "srv_render_host.h"
#include "srv_render_scene.h"
#include "srv_v3_sink.h"
#include "srv_residency_source.h"
#include "srv_scene_recorder.h"   // epic 0301, issue 3904
#include "ring_sink.h"
#include "feed/srv_feed.h"
#include "srv_power.h"
#include "srv_options.h"   // ticket 4934, 4940: RoadLayout, RoadJunction, RoadJitter (Vp64Opt::Extra)
#include "vp_pacing.h"     // ticket 0688: the pass's stage timer (shell/common)
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <stdlib.h>
#include <deque>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

namespace srvrender {

namespace c5t = c5r::terrain;
using vpframe::SinkStatus;

namespace {

// issue 3607: render_water's mode and render_water_freeze, parsed by ParseHostConfig beside HostConfig::water (the struct is
// in srv_render_host.h, which 3607 does not hold; these move into it when that file is next changed). Last parse wins.
int g_waterMode = WaterSceneOptions::kFull;
bool g_waterFreeze = false;
// issue 0140: render_bake_yield (1: the residency's idle slice ends before its next bake once a camera waits) and
// render_bake_stale_before_draw (0: stale visible tiles are re-baked in the idle slice, not before the draw), the same way
// (srv_render_host.h is not this issue's file). Last parse wins.
bool g_bakeYield = true;
bool g_bakeStaleBeforeDraw = false;
// ticket 0414: render_bake_no_sleep (1, the default: while the source has bakes pending, a pass whose idle() slice baked
// is followed by no pacing wait, only a look at <base>.Cam and the stop event; 0: the 2 ms wait of 3308 after every pass,
// the behaviour before 0414, kept as the A/B arm). The slice's bakes are seen through the yield check (CameraWaiting), so
// with render_bake_yield=0 the loop keeps the 2 ms wait. Parsed beside render_bake_yield, the same way. Last parse wins.
bool g_bakeNoSleep = true;
// ticket 0435: render_prevent_driver_threading (0 | 1): 1 creates the device with
// D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, so the driver runs no worker threads of its own for this
// device and the ocean draw is byte-stable from redraw to redraw (0336, water-system.md 15); 0 = flags 0, the device before
// 0435. Default 0 by the measurement (water-system.md 15.1.4-15.1.5: the flag costs the render thread about 0.45 ms per
// frame and the huge bake-ahead about 2.2 s at 60 Hz, and gives the player no picture change); 1 by default when recording
// (render_test_record, below). Parsed beside render_bake_no_sleep, the same way (HostConfig's header is not 0435's). Last
// parse wins.
bool g_preventDriverThreading = false;
// ticket 0753 (J3, acceptance): render_test_d3d_debug (TEST ONLY, 0 | 1): 1 creates the device with
// D3D11_CREATE_DEVICE_DEBUG (the Windows debug layer, d3d11_3SDKLayers.dll) and writes every corruption, error and warning
// message of its info queue to the log as a "LOUD: d3d debug:" line after each pass (counted; INFO and MESSAGE are not
// written); render_test_record_ahead_always (TEST ONLY, 0 | 1): record ahead records and executes the draw stage's end on
// every draw, also without a units' job in flight (the render-host suite has no units), so the suite runs its A3 path.
// Parsed beside render_prevent_driver_threading, the same way. Last parse wins.
bool g_testD3dDebug = false;
bool g_testRecordAheadAlways = false;
bool g_testRecordAheadEndOnly = false;   // ticket 0753 (TEST): render_test_record_ahead_end_only = 1: step 1 alone
// issue 0140, tests only: render_test_bake_cpu_ms = the stand-in baker's busy wait per bake (StandinFaults::cpuMs)
double g_testBakeCpuMs = 0;
// issue 0417: render_bake_warm (0 default: first uses inside the bakes, as before 0417) | 1 (the pipeline baker's warm-up at
// the residency's init: every layer texture, style and pass used once before a load; c5r::bake::WarmUp::AtInit). Off by
// default: the live series found the slow single bakes to be a wait in the driver's Flush under GPU memory pressure, not a
// first use (terrain-bake-slow-first-use.md 5), and the MC took no warm-up (2026-11-16); kept as a test arm. Parsed beside
// render_decals, as g_decals (HostConfig's header is not 0417's). Last parse wins.
int g_bakeWarm = 0;
// issue 0371: render_bake_near (1, the default since terrain-bake-passes.md 3.5.4's checks: the EXE's near-camera RGBA8 pairs,
// c5r::bake::ResidencyOptions::nearCaches, the state-5 bake and the draw from the pair in states 2 and 5 | 0: every tile
// drawn from its BC1 pair, the picture before 0371). Parsed beside render_bake_warm, as g_bakeWarm. Last parse wins.
int g_bakeNear = 1;
// issue 3954: render_fow and render_fow_level, parsed by ParseHostConfig beside render_overlay (HostConfig is in
// srv_render_host.h, which 3954 does not hold; these move into it when that file is next changed). Last parse wins.
int g_fow = 1;
int g_fowLevel = -1;
// issue 4105: render_decals (1 default: the decal tap's decals in the server's bake, an input that cannot install ends the
// game, decision 6 of epic 0166) | 0 (no reader, no decals: the picture before 0166); render_test_decal_wait_ms (the
// install bound, default 20000) and render_test_decal_trace (log the first N bakes with decals). Parsed by
// ParseHostConfig beside render_fow, as g_fow (HostConfig's header is not 4105's). Last parse wins.
int g_decals = 1;
double g_decalWaitMs = 20000;
int g_decalTrace = 0;
// issue 4308 (epic 0169): render_shadow (1 default: the shadow tap's triple as terrain t3 / Arg6 / ShadowMatrix0 and water
// t13 / 0x220; a tap that cannot install ends the game, D125) | 0 (none: the picture before 4308); render_ice (1 default: the
// ice tap's mesh drawn at stock's place; an ice input that cannot install ends the game) | 0 (none);
// render_test_shadow_wait_ms (no shadow tap section this long after the load = an install failure; default 20000) and
// render_test_ice_wait_ms (the same for the ice tap; default 0 = no bound). Parsed by ParseHostConfig beside render_decals,
// as g_decals (HostConfig's header is not 4308's). Last parse wins.
int g_shadow = 1;
int g_ice = 1;
double g_shadowWaitMs = 20000;
double g_iceWaitMs = 0;
// issue 4308, a test key: render_test_ice_file=PATH, an `ice_mesh` bundle member file (ice/gen<N>.ice, form C5IC) the scene
// draws as its ice source instead of the ice tap's reader (while task 4307's reader is not merged; never in a shipped
// server.cfg). A file that cannot be read or parsed is LOUD and the tap's reader stays.
std::string g_iceTestFile;
// epic 0375, issue 4454 (unit-server-scene.md 8, decision 8): render_units (0 default: no unit scene, no unit feed, the
// picture before) | 1 (the server's unit scene: SrvFeed::SetUnitsEnabled(true) at HostStart, the scene pass in
// Scene::draw, a pre-check failure ends the game by D125's route); render_units_passes (0 default | 1: the reflection pass
// too, issue 4455; needs render_units=1). Test keys: render_test_units_dump=DIR (each drawn frame whose UnitScene changed as
// DIR/frame_<frame id>.c5units), render_test_units_dump_max (default 1000), render_test_units_db=FILE (a unit database
// instead of the feed's copy), render_test_units_table=FILE (issue 4460: the scene's tables, seeds and the feed's counts per
// kind, rewritten once a second when changed; its folder must exist). Parsed by ParseHostConfig beside render_shadow, as
// g_shadow. Last parse wins.
int g_units = 0;
// epic 0431, ticket 4554: render_forest (1, the default where the program links server/src/forest/: the trees; an install
// that fails ends the game, D125) | 0 (none: the picture before 0431); render_test_forest_ids_wait_ms (no published feature
// ids this long after the first plot table = an install failure; default 10000); render_test_forest_seed (the session's
// random stream; 0, the default: random per session, as stock). Parsed beside render_units. Last parse wins.
int g_forest = 1;
// ticket 0199 part 2: render_arc (0, the default: no arc from the server | 1: the ranged-attack arc from the proxy's arrows
// records; research/05-graphics-composition/arrows-and-path-line.md 13). The proxy writes 1 with [Renderer] ServerArc = 1.
// Parsed beside render_forest. Last parse wins.
int g_arc = 0;
// ticket 0300 part 2: render_path (0, the default: no path markers from the server | 1: the move-order path's discs,
// chevrons and destination ring from the proxy's path records; research/05-graphics-composition/path-models.md 7). The
// proxy writes 1 with [Renderer] ServerPath = 1. Parsed beside render_arc. Last parse wins.
int g_path = 0;
// epic 0561, ticket 4875 (research/05-graphics-composition/city-renderer.md 11): render_cities / render_landmarks (0, the
// defaults: no model module, the picture before | 1: the server reads the compositor's model tap, publishes its identity
// set and draws the dropped cities / landmarks, landmarks/srv_models_scene.h; the proxy writes 1 with [Renderer]
// ServerCities / ServerLandmarks = 1 and ServerRenderer = 1); render_models_source (exe, the default | own: an empty scene,
// 4880 / 4881 parked); render_models_no_frame_ms, render_models_hold_ms, render_models_install_ms (the mode writer's
// bounds, srv_models_mode.h; ticket 0641: no_frame and hold count step time, the hold's default 30000; ticket 4885: the
// install bound counts from the later of the load's first drawn frame and the
// model layer's open), render_models_open_stall_ms (60000) and render_models_open_max_ms (600000) (ticket 4885: the open's
// own bounds, no progress and a ceiling; 0 = none); render_test_models_draw_all=1 (TEST ONLY: every resolved record drawn, not only the dropped
// ones). Parsed beside render_path. Last parse wins.
srvmodels::ModelsOptions g_models;
double g_forestIdsWaitMs = 10000;
uint32_t g_forestSeed = 0;
// epic 0528, ticket 4852: render_farmfields (0, the default until 0528 D's option: the decal tap's farm fields as they are |
// 1: the server lays out its own, srv_farmfields.h); render_farmfields_keep_tap (0, the default: the tap's farm records are
// dropped | 1: kept, and a tap set holding them is handed on without the server's); render_farmfields_ids_wait_ms (no farm
// id this long after the first plot table: one LOUD line, the tap's set used; default 20000). Last parse wins.
int g_farmfields = 0;
int g_farmKeepTap = 0;
double g_farmIdsWaitMs = 20000;
// ticket 4934 (epic 0454): render_roadlayout (RoadLayout's effective value from the options file unless server.cfg names
// it; 0 = the tap's roads as they are, the default; 1 the tidy layout; 2 all connections, ticket 4940); ticket 4940:
// render_roadlayout_junction (0..3) and render_roadlayout_jitter (0..7), RoadJunction's and RoadJitter's effective values
// while the layout is 2 (0 otherwise) unless server.cfg names them. The three go to srvextras::RoadLayoutChoiceSlot() (the
// scene passes the layout on as "on" only). Last parse wins.
int g_roadLayout = 0;
int g_roadJunction = 0;
int g_roadJitter = 0;
// ticket 4553 in the forest scene: render_test_cut_wait_ms (no cut grid this long after the field = an install failure;
// default 20000, 0: no bound) and render_test_cut_fail=1 (the cut reader's install failure on purpose: exit 5, the report).
double g_forestCutWaitMs = 20000;
int g_forestCutFail = 0;
int g_unitPasses = 0;
std::wstring g_unitsDump;
int g_unitsDumpMax = 1000;
std::wstring g_unitsDb;
std::wstring g_unitsTable;   // issue 4460
// issue 4456: render_units_anim (1 default: 0374's controller chooses the figures' clips and times; 0: the module's default
// clip at t 0, TEST ONLY for decision 9's forced-pose comparison); render_test_units_anim=FILE (TEST ONLY: the controller's
// input as .c5anim, rewritten once a second when frames were added; its folder must exist)
int g_unitsAnim = 1;
std::wstring g_unitsAnimFile;
// epic 0042, ticket 4722: render_units_triggers (0 default | 1: the server's trigger stream on <section>.Trig, route S; never
// on by default before 4783; needs render_units=1 and render_units_anim=1)
int g_unitsTriggers = 0;
// ticket 4737: render_test_units_tier_gate (TEST ONLY; 1 default | 0: the trigger stream sends every figure's Sound
// triggers whatever their tier, route S before 4737; research/02-exe-internals/exe-audio.md 13)
int g_unitsTierGate = 1;
// ticket 4737 (rule A, W43): render_test_units_charge_any (TEST ONLY; 1 default again since ticket 4743 W81, MC #11142;
// 0 from 4743 W46 to W81 | 1: every RunCharge attacker run-charges, 4737's rule; 0: the 4775 rule, only a rush formation
// member run-charges; CombatParams::charge_any, research/02-exe-internals/exe-audio.md 13.3, unit-combat-server.md 18.13,
// 18.28, 18.28.1). Both 4737 keys go into every bundle's render_config; sceneshot replays a bundle without
// render_test_units_charge_any (recorded before 4737) with 0, and one with "0" with 0.
int g_unitsChargeAny = 1;
// ticket 4735 (rule A, W45): render_test_units_turn_first (TEST ONLY; 1 default | 0: no figure turns before a fight whose
// figures stand, the rule before 4735; CombatParams::turn_first, research/05-graphics-composition/unit-combat-server.md
// 10.11). Into every bundle's render_config; sceneshot replays a bundle without it (recorded before 4735) with 0.
int g_unitsTurnFirst = 1;
// ticket 4744 (rule A, W49): render_units_fight_drawn_figures (1 default | 0: every held figure of a fighting unit
// stands, the rule before 4744; CombatParams::fight_drawn_figures, research/05-graphics-composition/unit-combat-server.md
// 18.16). Into every bundle's render_config; sceneshot replays a bundle without it (recorded before 4744) with 0.
int g_unitsFightDrawn = 1;
// tickets 4745 + 4747 (rule A, W53): render_test_units_reform_first (TEST ONLY; 1 default | 0: no unit reforms before its
// fight, the rule before 4745; CombatParams::reform_first, research/05-graphics-composition/unit-combat-server.md 18.17).
// Into every bundle's render_config; sceneshot replays a bundle without it (recorded before 4745) with 0.
int g_unitsReformFirst = 1;
// ticket 4742 (rule A, W54): render_test_units_walk_home_in_fight (TEST ONLY; 1 default | 0: the survivors walk home after
// the fight's end, the rule before 4742; CombatParams::walk_home, research/05-graphics-composition/unit-combat-server.md
// 18.18). Into every bundle's render_config; sceneshot replays a bundle without it (recorded before 4742) with 0.
int g_unitsWalkHome = 1;
// ticket 4748 (rule A, W59): render_test_units_city_in_place (TEST ONLY; 1 default | 0: a melee attacker on a city approaches
// its centre before its strike, the rule before 4748; CombatParams::city_in_place, research/05-graphics-composition/
// unit-combat-server.md 18.21). Into every bundle's render_config; sceneshot replays a bundle without it (recorded before
// 4748) with 0.
int g_unitsCityInPlace = 1;
// ticket 4746 (rule A, W57): render_test_units_ready_after_move (TEST ONLY; 1 default | 0: every waiting figure enters
// combat ready 1600, the rule before 4746; CombatParams::ready_after_move, research/05-graphics-composition/
// unit-combat-server.md 10.12). Into every bundle's render_config; sceneshot replays a bundle without it (recorded before
// 4746) with 0.
int g_unitsReadyAfterMove = 1;
// ticket 4750 (rule A, W61): render_test_units_defend_from_stance (TEST ONLY; 1 default | 0: a non-answering defender's
// figures request bombard defend 1280 at once, the rule before 4750; CombatParams::defend_from_stance,
// research/05-graphics-composition/unit-combat-server.md 10.14). Into every bundle's render_config; sceneshot replays a
// bundle without it (recorded before 4750) with 0.
int g_unitsDefendFromStance = 1;
// ticket 5050 (rule A, W65; 1 by default since W74, MC #10667 / #10726): render_test_units_reform_ends_at_arrival (TEST
// ONLY; 1 default: phase 1 ends at the last reforming figure's arrival, CombatParams::reform_ends_at_arrival | 0: after its
// run stop, the rule before 5050) and render_test_units_attack_from_stance (TEST ONLY; 1 default: stock's phase-2 stance
// 1450 first, CombatParams::attack_from_stance | 0: an attacker's Ready requests 1600 at once, the rule before 5050);
// research/05-graphics-composition/unit-combat-server.md 18.23, 18.23.2. Into every bundle's render_config; sceneshot
// replays a bundle without them (recorded before 5050) with 0.
int g_unitsReformEndsAtArrival = 1;
int g_unitsAttackFromStance = 1;
// ticket 5057 (rule A, W85; MC #11162): render_units_rush_ride_through (1 default: a rush rider lands its hit at its
// arrival and rides on in its 1120, stock's riders, CombatParams::rush_ride_through | 0: every rider stops, enters
// 1600 and strikes 1100, the rule before 5057) and render_units_defender_waits_first_landing (1 default: in fight type 2
// the defender strikes from the attacker's first landing on, CombatParams::defender_waits_first_landing | 0: at once,
// the rule before 5057); research/05-graphics-composition/unit-combat-server.md 18.29, 18.29.1. Into every bundle's
// render_config; sceneshot replays a bundle without them (recorded before 5057) with 0.
int g_unitsRushRideThrough = 1;
int g_unitsDefenderWaitsFirstLanding = 1;
// ticket 5053 (rule A, W112, design W72; default ON by the MC's word 2026-11-25, a parity fix back to stock's fight-end
// timing, no VP64Options key): render_test_units_death_by_variant (TEST ONLY, diagnosis; 1 default: a dying figure
// lasts the death clip its machine plays, A or B, CombatParams::death_by_variant | 0: every dying figure is removed
// after its 1200 clip, the rule before 5053) and render_test_units_end_after_dying (TEST ONLY, diagnosis; 1 default:
// the standing figures wait for the dying and a land defender leaves 1280 with a 1450 shuffle,
// CombatParams::end_after_dying | 0: they end at the last landing, the rule before 5053);
// research/05-graphics-composition/unit-combat-server.md 18.26. Into every bundle's render_config; sceneshot replays a
// bundle without them (recorded before 5053) with 0.
int g_unitsDeathByVariant = 1;
int g_unitsEndAfterDying = 1;
// ticket 5059 (rule A, W100; design W96, lead-0042 GO #11696 / #11703): render_units_victory_word (1 default: victory
// only for a member whose EnableActions carry the word Victory, as stock: none in VP, so no victory pose and no victory
// vox, CombatParams::victory_needs_word | 0: a survivor whose machine takes 1800 may take victory, the rule before 5059);
// research/05-graphics-composition/unit-combat-server.md 18.36. Into every bundle's render_config; sceneshot replays a
// bundle without it (recorded before 5059) with 0.
int g_unitsVictoryWord = 1;
// ticket 5067 S1 (rule A, W122; lead-0042 #12649 / #12652; default ON by the MC's word #12907, a parity fix):
// render_units_type2_phase2 (1 default: in fight type 2 nobody looks before stock's 0.665 s phase 2 ends, the defender's
// figures in the 1450 stance meanwhile, CombatParams::type2_phase2 | 0: no phase 2 in a charge fight, the rule before
// 5067); unit-combat-server.md 18.29.3. Into every bundle's render_config; sceneshot replays a bundle without it
// (recorded before 5067) with 0.
int g_unitsType2Phase2 = 1;
// ticket 0743 (rule A, W137; the owner's beta 4 report; a parity fix): render_units_melee_reach (1 default: an attacking
// land melee figure of a non-rush member stops within stock's reach, its and its target's AttackRadius, of the nearest
// standing opponent figure and aims at the nearest candidate, CombatParams::melee_reach | 0: every approach ends
// MoveRadius short of a random doomed figure, the rule before 0743); unit-combat-server.md 18.44. Into every bundle's
// render_config; sceneshot replays a bundle without it (recorded before 0743) with 0.
int g_unitsMeleeReach = 1;
// ticket 5061 (rule A, W139; a parity fix, D255: no options-file key): render_units_defender_step (1 default: in a type 2
// fight, from the attacker's first landing, the defender's land figures of non-rush members run to stock's reach of the
// nearest attacker figure, those holding a strike and the others alike, CombatParams::defender_step | 0: the defender
// strikes from where it stands, the rule before 5061); unit-combat-server.md 18.45. Into every bundle's render_config;
// sceneshot replays a bundle without it (recorded before 5061) with 0.
int g_unitsDefenderStep = 1;
// ticket 5065 (rule A, W118): render_units_orders_after_walk (1 default: in mode exe a walking unit's work and fortify
// wait for its walk's end, as the EXE plays them, AnimFeed::setOrdersAfterWalk | 0: taken at once, the rule before 5065);
// research/05-graphics-composition/unit-controller.md 20. Into every bundle's render_config; sceneshot and unitfeed
// replay a bundle without it (recorded before 5065) with 0.
int g_unitsOrdersAfterWalk = 1;
// ticket 5052 (rule A, W71): render_test_units_trigger_striker_weapon (TEST ONLY; 1 default since the live pair, MC #10847,
// unit-combat-server.md 18.25.2: a unit that does not strike in its running fight takes its opponent's figure-0 weapon,
// TriggerStreamConfig::strikerWeapon | 0: the rule before 5052, every figure's $(WEAPON) is its own member's weapon) and render_test_units_trigger_terrain (TEST ONLY; 1 default since its
// live pair, MC #11356, unit-combat-server.md 18.25.4 / 18.25.5: the unit's plot's feature, else terrain EffectTypeTag,
// else DIRT, TriggerStreamConfig::plotTerrain, from the plot table SceneUnits::setPlotTable gets | 0: $(TERRAIN) DIRT, the
// rule before 5052); research/05-graphics-composition/unit-combat-server.md 18.25. Into every bundle's render_config;
// sceneshot replays a bundle without them with 0 (no drawn pixel depends on either).
int g_unitsTriggerStrikerWeapon = 1;
int g_unitsTriggerTerrain = 1;
// ticket 5055 (rule A, W93; MC #11607): render_units_trigger_striker_melee (1 default: every unit struck in its running
// fight takes its striker's weapon for a Sound's $(WEAPON), both sides of a melee, impacts and battle beds, as stock,
// with its own weapon as the second try, TriggerStreamConfig::strikerMelee | 0: 5052's rule, only a unit that does not
// strike back is named by its opponent). It refines the striker-weapon naming and takes effect only when
// render_test_units_trigger_striker_weapon is 1 too: with that key 0 it changes nothing. unit-combat-server.md 18.30.
// Into every bundle's render_config; sceneshot replays a bundle without it with 0 (no drawn pixel depends on it).
int g_unitsTriggerStrikerMelee = 1;
// ticket 5064 (rule A): render_units_projectile_pose (1 default: a unit's projectile starts at its bone's world transform
// and its member's scale, as the EXE's own start, and a launcher without a heading faces its target,
// RouteParams::projectilePose | 0: the rule before 5064, scale 1 and the identity orientation, about seven times stock's
// size); unit-combat-server.md 18.39. Into every bundle's render_config; sceneshot replays a bundle without it with 0 (the
// projectiles are the EXE's effects, no pixel of the server's).
int g_unitsProjectilePose = 1;
// ticket 0742 (rule A): render_units_city_volley (1 default: a city's volley flies from each shot's launch, so the
// projectile travels its flight, at the city volley's scale, RouteParams::cityVolley | 0: the rule before 0742, the
// flight planned at its landing with its start 0.6 s in the past, so it never travels, at scale 1);
// unit-combat-server.md 18.43. Into every bundle's render_config; sceneshot replays a bundle without it with 0 (the
// projectiles are the EXE's effects, no pixel of the server's).
int g_unitsCityVolley = 1;
// ticket 5058 (rule A): render_units_trigger_city_era (1 default: a Sound $(WEAPON) that a city strikes takes the city's
// weapon by its owner's era, stock's CITY_EARLY / _MID / _LATE selector, TriggerStreamConfig::cityEra, with the striker
// weapon on | 0: the rule before 5058, the unit's own); unit-combat-server.md 18.34. Into every bundle's render_config;
// sceneshot replays a bundle without it with 0 (no drawn pixel depends on a sound name).
int g_unitsTriggerCityEra = 1;
// ticket 5066 (rule A, W117; lead-0042 #12565 / #12574): render_units_fight_after_walk (1 default: in mode exe the
// fighter's walks around its fight as stock, AnimFeed::setFightAfterWalk: (b) a kind 12 given back at a fight's end frees
// the kept walk-back view, so the winner walks from where it stands onto its plot | 0: the rules before 5066, byte for
// byte); research/05-graphics-composition/unit-combat-server.md 18.42. Into every bundle's render_config; sceneshot replays
// a bundle without it (recorded before 5066) with 0.
int g_unitsFightAfterWalk = 1;
// ticket 5062 (rule A): render_units_trigger_city_shot (1 default: a city's own strike flies its owner era's
// Eras.CityBombardEffectTag, so the EXE plays stock's shot and miss sounds, AnimRunner::setCityShot | 0: the rule before
// 5062, no name, nothing flown); unit-combat-server.md 18.37. Into every bundle's render_config; sceneshot replays a bundle
// without it with 0 (the projectile is the EXE's effect, no pixel of the server's).
int g_unitsTriggerCityShot = 1;
// epic 0040, issue 4753 (the epic's host keys, one block per place in this file; srv_units_mode.h, unit-server-scene.md 8.1):
// render_units_ready_hold_ms (1..600000, default 30000 of step time since ticket 0641, was 2000 of wall time, breakdown O5: a
// readiness lost while the mode word's unit bit was set may last this long before D125's fatal step; srv_units_mode.h) and render_units_positions (own, the default: the server's own root
// positions; exe: the EXE's position records, issue 4755; the unit bit is set only with exe). Neither needs render_units=1
// (both are read only by the unit scene). Last parse wins.
double g_unitsReadyHoldMs = srvunits::kUnitsReadyHoldMsDefault;   // ticket 0641: 30000 (was 2000)
int g_unitsPositions = 0;   // SceneOptions::UnitsPositions
// epic 0042, ticket 4785 (srv_unit_shadow.h, unit-combat-server.md 13): render_units_shadow (1, the default: with
// render_units=1 and render_shadow=1 the server's units cast stock's shadow into the tapped map and the mode word's bit 4 asks
// the compositor to drop the EXE's unit mask draws; 0: the EXE's figures cast, the picture before 4785). Last parse wins.
int g_unitsShadow = 1;
float g_unitsTestOffset[2] = {0, 0};   // ticket 4785: render_test_units_offset=dx,dy (TEST ONLY; UnitsOptions::testOffset)
// ticket 0796 (rule A; Quick Combat Animations T1, D270, quick-combat-extra.md 11): render_test_units_quick_combat (TEST
// ONLY; 0 default: every fight at real time | 1: V2, the run-up at 2x, the first strike at 1x, the rest at 3x;
// UnitsOptions::quickCombat). The player key is T2's ([Extras] QuickCombatAnimations, 0797). Written to render_config;
// sceneshot reads it (absent = 0, a bundle before 0796). Last parse wins.
int g_unitsQuickCombat = 0;

const char* kStateNames[] = {"NONE", "STARTING", "READY", "REBUILDING", "FAILED", "STOPPED"};
const char* kFailNames[] = {"NONE", "ADAPTER", "DEVICE", "RENDERER", "REBUILDS", "EXCEPTION", "SECTION"};
const uint32_t kMaxBackoffMs = 30000;
const int kMaxRebuildsInWindow = 3;          // the 3rd lost device inside the window: FAILED
const double kRebuildWindowMs = 5 * 60 * 1000.0;
const int kMaxCreateAttempts = 6;            // device re-creation attempts per loss (1+2+4+8+16+30 s with the default)
const int kRenderInitFatalExitCode = 5;      // ticket 0600: the overlay's code (0167): "the 64-bit path cannot start its picture"

struct Host
{
	HostConfig cfg;
	HANDLE thread = nullptr;
	HANDLE stop = nullptr;
	volatile LONG started = 0;
	volatile LONG stopped = 0;
	// the host's own view of <base>.Frame (state, failReason, deviceLostTotal: written before any sink exists too)
	HANDLE frameMap = nullptr;
	void* frame = nullptr;
	// device
	IDXGIAdapter1* adapter = nullptr;
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	uint64_t luid = 0, followedLuid = 0;
	bool overlayFatalDone = false;   // issue 3704: the fatal route taken (once per thread)
	bool fowFatalDone = false;       // issue 3954: the fog's fatal route taken (once per thread)
	bool decalFatalDone = false;     // issue 4105: the decals' fatal route taken (once per thread)
	bool shadowFatalDone = false;    // issue 4308
	bool iceFatalDone = false;
	bool unitsFatalDone = false;     // epic 0375, issue 4454
	bool modelsFatalDone = false;    // epic 0561, ticket 4875
	bool forestFatalDone = false;    // epic 0431, ticket 4554
	// ticket 0600: the first READY reached (after it a failure is "in play"); the failure came in play; D125's route taken
	bool initDone = false, failedInPlay = false, failFatalDone = false;
	std::unique_ptr<vpframe::FrameSink> sink;
	V3Sink* v3 = nullptr;   // the v3 sink's extras (test dump, overwritten / skipped counters); everything else via FrameSink
	Scene scene;
	// counters (read by HostSummary from another thread after the stop; plain values)
	volatile LONG state = 0;
	volatile LONG failReason = 0;
	int64_t draws = 0, published = 0, busy = 0, notReady = 0, sinkErrors = 0, drawErrors = 0, lost = 0, rebuilds = 0, exceptions = 0;
	int64_t camerasSkipped = 0, waitsEvent = 0, waitsLegacy = 0, waitTimeouts = 0, busyRetried = 0;
	int64_t waitsIdle = 0, idleCalls = 0;   // issue 3308: slow waits before the first publish; the source's idle() calls
	// ticket 0414: the residency's yield checks that found no camera waiting (each is followed by one bake, or ends a slice
	// that baked: Residency::idle), and the passes that skipped the pacing wait because their slice baked
	int64_t bakeChecks = 0, idleNoSleep = 0;
	// ticket 0713 (the prepare phase; research/07-architecture-decisions/architect-2026-11-23-frame-overlap.md 6): <base>.Tap
	// (opened by Loop), the publish block's tapPresent read before the newest drain and polls (-1: none since the start or the
	// composite's pid change), how the pass before ended its wait, and the counts (totals: "prepare 1s:" and the summary)
	HANDLE tapEv = nullptr;
	int64_t prepTap = -1;
	int wokeBy = 0;   // kWoke*
	bool prepEvent = false;   // the pass before was in event pacing (the tap is waited on and yielded to only then)
	int64_t prepPasses = 0;      // passes woken by <base>.Tap that drained and polled (the `prepare` stage)
	int64_t prepNothing = 0;     // ... woken by it with the taps already polled (a camera pass polled after the tap was written)
	int64_t camHits = 0;         // camera passes that went straight to decide (tapPresent equal to the prepared number)
	int64_t camPolls = 0;        // camera passes that drained and polled first (no prepare, or a tap written since)
	int64_t holdRepolls = 0;     // frames about to be held for EXE inputs that polled once more first (0691's rule)
	int64_t tapYields = 0;       // idle-bake yield checks that found a tap pending (no camera)
	// ticket 0435: the device's creation flags; per second, the CPU wall time of a frame's submission (Scene::draw to the
	// sink's end: every pass's D3D calls and the hand-over) and the render thread's own CPU time (GetThreadTimes)
	UINT deviceFlags = 0;
	ID3D11InfoQueue* infoQueue = nullptr;     // ticket 0753 (TEST ONLY, render_test_d3d_debug=1)
	int64_t d3dDebugMessages[3] = {0, 0, 0};  // by severity: corruption, error, warning (the whole run)
	int64_t submitQpcSec = 0, submitMaxQpcSec = 0, submitNSec = 0;
	uint64_t rtCpu100nsLast = 0;
	// ticket 0688: the pass's stage timer (vp_pacing.h; HostConfig::passStats), this second's maxima and sums, the long
	// passes logged this second, the run's totals for the exit line
	vppacing::PassTimer pass;
	vppacing::PassSecond passSec;
	int64_t passLongRun = 0, passesRun = 0;
	int64_t passFrameId = 0;   // the camera drawn in this pass (0 = none)
	double passWakeMs = -1.0;  // this pass's wake delay (woken by <base>.Cam; -1 = not)
	double wakeNextMs = -1.0;  // the wake delay measured at the wait's end, for the pass that begins next
	// ticket 0711: the `draw` stage of each drawn pass in three stretches (head, after the water, end; Scene::drawHeadTicks),
	// this second's maxima and sums (QPC ticks) for the "pass 1s:" line
	int64_t drawSplitMax[3] = {0, 0, 0}, drawSplitSum[3] = {0, 0, 0};
	// issue 3312: the composite stopped publishing while its Presents go on (a version-3 composite without lockstep, ring or
	// depth; a publish path that died): event pacing falls back to the camera-block poll until the publish seq moves again
	bool paceFallback = false;
	uint32_t paceSeq = 0;          // the publish block's seq when last seen moving
	int64_t pacePc = -1;           // the camera block's presentCount at that moment (-1: not yet seen)
	int64_t paceFallbacks = 0, paceResumes = 0, waitsFallback = 0;
	int64_t feedEvents = 0, feedChanged = 0, feedDrains = 0;
	uint32_t feedGen = 0;
	int64_t lastDrawnFrameId = 0;
	int64_t heldFrameId = 0, holdIds = 0;   // ticket 0691: the hold reported now (HoldReport), frame ids of its episode
	uint32_t heldBits = 0;
	uint32_t lastExePid = 0;
	bool dumped = false;
	std::deque<int64_t> lostAt;
	std::string failText;
	char exceptionText[256] = {};
	// ---- epic 0301, issue 3904: the scene recorder (render_test_record; all null without it) ----
	std::unique_ptr<SceneRecorder> rec;
	std::unique_ptr<FeedSource> recFeed;   // the recording feed over LiveFeed() (Loop's drain, the residency's tables)
	FeedSource* feed = &LiveFeed();        // what Loop drains: LiveFeed() = SrvFeed::Drain, today's call
	OverlaySource* recOvl = nullptr;       // the recording overlay wrapper the scene owns (its counters); null after a release
	RecorderCounters recLast;              // the readers' counters at the last step (the overlay's go with its device)
};

Host* g_host = nullptr;
SRWLOCK g_hostLock = SRWLOCK_INIT;

// ---- config parsing ------------------------------------------------------------------------------------------------

bool ParseInt(const std::string& v, long long lo, long long hi, long long& out)
{
	if(v.empty())
		return false;
	char* end = nullptr;
	const long long x = strtoll(v.c_str(), &end, 10);
	if(!end || *end || x < lo || x > hi)
		return false;
	out = x;
	return true;
}

bool ParseDouble(const std::string& v, double lo, double hi, double& out)
{
	if(v.empty())
		return false;
	char* end = nullptr;
	const double x = strtod(v.c_str(), &end);
	if(!end || *end || !(x >= lo && x <= hi))
		return false;
	out = x;
	return true;
}

std::wstring Wide(const std::string& s)
{
	wchar_t w[1024] = {};
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, 1023);
	return w;
}

bool ColorSourceFrom(const std::string& v, bool fallback, c5t::ColorSource& out)
{
	if(v == "caches" && !fallback)
		out = c5t::ColorSource::Caches;
	else if(v == "material")
		out = c5t::ColorSource::Material;
	else if(v == "constant")
		out = c5t::ColorSource::Constant;
	else if(v == "height")
		out = c5t::ColorSource::Height;
	else if(v == "normal")
		out = c5t::ColorSource::Normal;
	else
		return false;
	return true;
}

// ---- the server block of <base>.Frame (host's writes) -------------------------------------------------------------

VPFrameServer* FrameServer(Host& h) { return h.frame ? VPFRAME_SERVER(h.frame) : nullptr; }

// ticket 0691 (MC #11169): the frame the scene holds for EXE inputs (FowScene::holdFrameId / holdReasons, the only hold of
// Scene::decide), reported to the composite, which then does not wait for it (vp_frame.h VPFrameServer::heldFrameId /
// holdReasons and their writer order); frameId 0 clears (a frame admitted to the draw, a new composite, the loop's start).
#define C5_HOLD_EQ(a, b) ((uint32_t)FowScene::a == (uint32_t)b)
static_assert(C5_HOLD_EQ(kHoldNoTable, VPHOLD_FOG_NO_TABLE) && C5_HOLD_EQ(kHoldTableSize, VPHOLD_FOG_TABLE_SIZE) &&
                  C5_HOLD_EQ(kHoldNoRevealed, VPHOLD_FOG_NO_REVEALED) && C5_HOLD_EQ(kHoldNoPair, VPHOLD_FOG_NO_PAIR) &&
                  C5_HOLD_EQ(kHoldInstall, VPHOLD_FOG_INSTALL),
              "the fog's hold bits are vp_frame.h's VPHOLD_*");
#undef C5_HOLD_EQ
static_assert(Scene::kHoldNoField == (uint32_t)VPHOLD_NO_FIELD, "the scene's field hold bit is vp_frame.h's VPHOLD_NO_FIELD");
void HoldReport(Host& h, int64_t frameId, uint32_t bits, bool force = false)
{
	if(!frameId)
		bits = 0;
	VPFrameServer* s = FrameServer(h);
	if(!s || (!force && frameId == h.heldFrameId && bits == h.heldBits))
		return;
	InterlockedExchange64(&s->heldFrameId, 0);
	InterlockedExchange((volatile LONG*)&s->holdReasons, (LONG)bits);
	if(frameId)
	{
		InterlockedExchange64(&s->heldFrameId, frameId);
		if(frameId != h.heldFrameId)
		{
			InterlockedIncrement64(&s->heldFramesTotal);
			if(!h.heldFrameId)
				RNote("lockstep: frame %lld held for EXE inputs (reasons 0x%08X): reported to the composite, which does not wait for "
				      "it (ticket 0691)", (long long)frameId, bits);
			h.holdIds++;
		}
	}
	else if(h.heldFrameId)
	{
		RNote("lockstep: hold cleared after %lld held frame id(s), the last %lld (ticket 0691)", (long long)h.holdIds,
		      (long long)h.heldFrameId);
		h.holdIds = 0;
	}
	h.heldFrameId = frameId;
	h.heldBits = bits;
}

void SetState(Host& h, uint32_t st, uint32_t reason = VPFSF_NONE)
{
	const uint32_t old = (uint32_t)h.state;
	InterlockedExchange(&h.state, (LONG)st);
	InterlockedExchange(&h.failReason, (LONG)reason);
	// VPFSF_SECTION: the section may belong to another live server (its sink refused it): never write into its block
	if(VPFrameServer* s = reason == VPFSF_SECTION ? nullptr : FrameServer(h))
	{
		s->failReason = reason;
		MemoryBarrier();
		s->state = st;
		if(s->magic != VPFRAME_MAGIC && st != VPFS_STOPPED)
		{
			s->serverPid = GetCurrentProcessId();
			s->version = VPFRAME_VERSION;
			s->qpcFrequency = QpcFreq();
			MemoryBarrier();
			s->magic = VPFRAME_MAGIC;
		}
	}
	if(old != st)
		RNote("state %s -> %s%s%s", kStateNames[old < 6 ? old : 0], kStateNames[st < 6 ? st : 0], reason ? " reason " : "",
		      reason ? kFailNames[reason < 7 ? reason : 0] : "");
}

// Ticket 0600 (D118, D125; D213 for the in-play case): every Fail() is terminal (the thread leaves its loop and waits for
// the stop) and leaves the world without terrain (D33 has the EXE's off), so ThreadProc takes D125's route once
// (HostConfig::fatal, exit code kRenderInitFatalExitCode) after the device is released: the player gets the report and the
// patch's popup instead of a grey map. At init: the first device, the sink, the scene's init, an exception on the way
// there. In play (after the first READY): the rebuild limit, a failed re-create, an exception on the render thread. A lost
// device the renderer recovers from is no Fail() and ends nothing (research/05-graphics-composition/renderer-failure-modes.md).
void Fail(Host& h, uint32_t reason, const std::string& text)
{
	h.failText = text;
	h.failedInPlay = h.initDone;
	RNote("LOUD: renderer FAILED (%s): %s; %s", kFailNames[reason < 7 ? reason : 0], text.c_str(),
	      !h.cfg.fatal ? "no fatal hook (a test host): the process goes on"
	      : h.initDone ? "in play: the world has no terrain from here, the server ends (D118, D125, D213: report and popup)"
	                   : "at init: the 64-bit path cannot draw the world, the server ends (D118, D125: report and popup)");
	SetState(h, VPFS_FAILED, reason);
}

// Ticket 0600: D125's route for a FAILED renderer, once, on the render thread (server.exe: SrvRemote::ReportAndExit, which
// does not return; the test host's hook returns and the thread then waits for the stop as before). The text names the
// reason, when (init or play), the failure's own text (the HRESULT or the exception code) and the counts.
void FailFatalStep(Host& h)
{
	if(h.state != VPFS_FAILED || h.failFatalDone || !h.cfg.fatal)
		return;
	h.failFatalDone = true;
	const uint32_t r = (uint32_t)h.failReason;
	char b[1024];
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "the server's renderer FAILED %s (VPFSF_%s): %s; draws %lld, published %lld, lost devices %lld, rebuilds %lld, "
	            "exceptions %lld",
	            h.failedInPlay ? "in play" : "at init", kFailNames[r < 7 ? r : 0], h.failText.c_str(), (long long)h.draws,
	            (long long)h.published, (long long)h.lost, (long long)h.rebuilds, (long long)h.exceptions);
	RNote("LOUD: D125 route: %s", b);
	h.cfg.fatal(kRenderInitFatalExitCode, b);
}

// ---- device ------------------------------------------------------------------------------------------------------

bool PickAdapter(Host& h, uint64_t wantLuid)
{
	SafeRelease(h.adapter);
	IDXGIFactory1* f = nullptr;
	if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)))
	{
		RNote("LOUD: CreateDXGIFactory1 failed");
		return false;
	}
	IDXGIAdapter1* best = nullptr;
	SIZE_T bestVram = 0;
	uint64_t bestLuid = 0;
	for(UINT i = 0;; ++i)
	{
		IDXGIAdapter1* a = nullptr;
		if(f->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 d;
		a->GetDesc1(&d);
		const uint64_t l = ((uint64_t)(uint32_t)d.AdapterLuid.HighPart << 32) | d.AdapterLuid.LowPart;
		RLog("adapter %u: '%ls' LUID %016llX VRAM %llu MB%s", i, d.Description, (unsigned long long)l,
		     (unsigned long long)(d.DedicatedVideoMemory >> 20), (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (software)" : "");
		bool pick = false;
		if(wantLuid)
			pick = l == wantLuid;
		else
			pick = !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > bestVram;
		if(pick)
		{
			SafeRelease(best);
			best = a;
			bestVram = d.DedicatedVideoMemory;
			bestLuid = l;
		}
		else
			a->Release();
	}
	f->Release();
	if(!best)
	{
		RNote("LOUD: no adapter%s", wantLuid ? " with the wanted LUID" : "");
		return false;
	}
	DXGI_ADAPTER_DESC1 d;
	best->GetDesc1(&d);
	h.adapter = best;
	h.luid = bestLuid;
	RNote("using adapter '%ls' LUID %016llX%s", d.Description, (unsigned long long)bestLuid,
	      wantLuid ? " (asked for)" : " (largest dedicated video memory)");
	return true;
}

void LogMemory(Host& h, const char* tag, bool note)
{
	PROCESS_MEMORY_COUNTERS_EX pmc;
	ZeroMemory(&pmc, sizeof(pmc));
	pmc.cb = sizeof(pmc);
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	unsigned long long vid = 0;
	IDXGIAdapter3* a3 = nullptr;
	if(h.adapter && SUCCEEDED(h.adapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)))
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO vi = {};
		if(SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi)))
			vid = vi.CurrentUsage;
		a3->Release();
	}
	if(note)
		RNote("mem %s: private=%.2f MB working=%.2f MB video_local=%.2f MB", tag, pmc.PrivateUsage / 1048576.0, pmc.WorkingSetSize / 1048576.0,
		      vid / 1048576.0);
	else
		RLog("mem %s: private=%.2f MB working=%.2f MB video_local=%.2f MB", tag, pmc.PrivateUsage / 1048576.0, pmc.WorkingSetSize / 1048576.0,
		     vid / 1048576.0);
}

void RingLog(void*, const char* line) { RNote("%s", line); }

// issue 0140: the residency's yield check (ResidencyOptions::yield): a camera block newer than the last one drawn.
// Ticket 0414: the context is the Host; a check that finds no camera is counted (Host::bakeChecks). Residency::idle makes
// the check before each bake and, once at least one bake was made, once more at the slice's end; so the count moves during
// an idle() call exactly when that call baked (residency.cpp, issue 0140's comment at the check).
bool TapPending(Host& h);   // ticket 0713 (below, with Loop)

bool CameraWaiting(void* host)
{
	Host& h = *static_cast<Host*>(host);
	bool waiting = h.scene.cameraPending();
	// ticket 0713: an idle bake never starts while the prepare phase's tap is pending either (the frame-overlap note 6)
	if(!waiting && TapPending(h))
	{
		waiting = true;
		h.tapYields++;
	}
	if(!waiting)
		h.bakeChecks++;
	return waiting;
}

SceneOptions SceneOpts(const HostConfig& c)
{
	SceneOptions o;
	o.exe = c.exe;
	o.tilePlots = c.tilePlots;
	o.tileD4 = c.tileD4;
	ColorSourceFrom(c.color, false, o.color);
	ColorSourceFrom(c.fallback, true, o.fallback);
	o.constantRgba = c.constantRgba;
	o.pass = c.pass;
	o.tess = c.tess;
	o.water = c.water;
	o.waterMode = g_waterMode;   // issue 3607
	o.waterFreeze = g_waterFreeze;
	o.overlay = c.overlay;       // issue 3704
	o.fow = g_fow;               // issue 3954
	o.fowLevel = g_fowLevel;
	o.decals = g_decals;         // issue 4105
	o.decalWaitMs = g_decalWaitMs;
	o.decalTrace = g_decalTrace;
	o.shadow = g_shadow;         // issue 4308
	o.shadowWaitMs = g_shadowWaitMs;
	o.ice = g_ice;
	o.iceWaitMs = g_iceWaitMs;
	o.units = g_units;           // epic 0375, issue 4454
	o.forest = g_forest;         // epic 0431, ticket 4554
	o.arc = g_arc;               // ticket 0199 part 2
	o.path = g_path;             // ticket 0300 part 2
	o.models = g_models;         // epic 0561, ticket 4875 (the install, the EXE and the mods' folders below)
	o.models.install = c.bakeInstall;
	o.frameJobs = c.frameJobs;              // ticket 0711 (J1)
	o.frameJobsJitter = c.frameJobsJitter;
	o.recordAhead = c.recordAhead;          // ticket 0753 (J3)
	o.recordAheadAlways = g_testRecordAheadAlways;   // ticket 0753 (TEST ONLY)
	o.recordAheadEndOnly = g_testRecordAheadEndOnly;
	o.models.exe = c.exe;
	o.forestIdsWaitMs = g_forestIdsWaitMs;
	o.forestSeed = g_forestSeed;
	o.forestCutWaitMs = g_forestCutWaitMs;
	o.forestCutFail = g_forestCutFail != 0;
	o.farmfields = g_farmfields;   // epic 0528, ticket 4852
	o.farmKeepTap = g_farmKeepTap != 0;
	o.farmIdsWaitMs = g_farmIdsWaitMs;
	o.roadLayout = g_roadLayout;   // ticket 4934
	o.unitsOpt.passes = g_unitPasses != 0;
	o.unitsOpt.exe = c.exe;
	o.unitsOpt.install = c.bakeInstall;
	o.unitsOpt.dumpDir = g_unitsDump;
	o.unitsOpt.dumpMax = g_unitsDumpMax;
	o.unitsOpt.dbOverride = g_unitsDb;
	o.unitsOpt.tableFile = g_unitsTable;   // issue 4460
	o.unitsOpt.anim = g_unitsAnim != 0;     // issue 4456
	o.unitsOpt.animFile = g_unitsAnimFile;
	o.unitsOpt.triggerSection = g_unitsTriggers ? c.section : std::wstring();   // ticket 4722: <section>.Trig (route S)
	o.unitsOpt.triggerTierGate = g_unitsTierGate != 0;   // ticket 4737
	o.unitsOpt.chargeAny = g_unitsChargeAny != 0;        // ticket 4737 (rule A)
	o.unitsOpt.turnFirst = g_unitsTurnFirst != 0;        // ticket 4735 (rule A)
	o.unitsOpt.fightDrawnFigures = g_unitsFightDrawn != 0;   // ticket 4744 (rule A)
	o.unitsOpt.reformFirst = g_unitsReformFirst != 0;        // tickets 4745 + 4747 (rule A)
	o.unitsOpt.walkHome = g_unitsWalkHome != 0;              // ticket 4742 (rule A)
	o.unitsOpt.cityInPlace = g_unitsCityInPlace != 0;        // ticket 4748 (rule A)
	o.unitsOpt.readyAfterMove = g_unitsReadyAfterMove != 0;  // ticket 4746 (rule A)
	o.unitsOpt.defendFromStance = g_unitsDefendFromStance != 0;   // ticket 4750 (rule A)
	o.unitsOpt.reformEndsAtArrival = g_unitsReformEndsAtArrival != 0;   // ticket 5050 (rule A)
	o.unitsOpt.attackFromStance = g_unitsAttackFromStance != 0;         // ticket 5050 (rule A)
	o.unitsOpt.rushRideThrough = g_unitsRushRideThrough != 0;           // ticket 5057 (rule A)
	o.unitsOpt.defenderWaitsFirstLanding = g_unitsDefenderWaitsFirstLanding != 0;   // ticket 5057 (rule A)
	o.unitsOpt.deathByVariant = g_unitsDeathByVariant != 0;             // ticket 5053 (rule A)
	o.unitsOpt.endAfterDying = g_unitsEndAfterDying != 0;               // ticket 5053 (rule A)
	o.unitsOpt.victoryNeedsWord = g_unitsVictoryWord != 0;   // ticket 5059 (rule A)
	o.unitsOpt.type2Phase2 = g_unitsType2Phase2 != 0;                     // ticket 5067 S1 (rule A)
	o.unitsOpt.meleeReach = g_unitsMeleeReach != 0;                       // ticket 0743 (rule A)
	o.unitsOpt.defenderStep = g_unitsDefenderStep != 0;                   // ticket 5061 (rule A)
	o.unitsOpt.ordersAfterWalk = g_unitsOrdersAfterWalk != 0;           // ticket 5065 (rule A)
	o.unitsOpt.triggerStrikerWeapon = g_unitsTriggerStrikerWeapon != 0;   // ticket 5052 (rule A)
	o.unitsOpt.triggerTerrain = g_unitsTriggerTerrain != 0;               // ticket 5052 (rule A)
	o.unitsOpt.triggerProjectilePose = g_unitsProjectilePose != 0;        // ticket 5064 (rule A)
	o.unitsOpt.triggerCityVolley = g_unitsCityVolley != 0;               // ticket 0742 (rule A)
	o.unitsOpt.triggerStrikerMelee = g_unitsTriggerStrikerMelee != 0;     // ticket 5055 (rule A)
	o.unitsOpt.triggerCityEra = g_unitsTriggerCityEra != 0;               // ticket 5058 (rule A)
	o.unitsOpt.fightAfterWalk = g_unitsFightAfterWalk != 0;               // ticket 5066 (rule A)
	o.unitsOpt.triggerCityShot = g_unitsTriggerCityShot != 0;             // ticket 5062 (rule A)
	o.unitsReadyHoldMs = g_unitsReadyHoldMs;   // epic 0040, issue 4753
	o.unitsPositions = g_unitsPositions;
	o.unitsShadow = g_unitsShadow;   // ticket 4785
	o.unitsOpt.testOffset[0] = g_unitsTestOffset[0];
	o.unitsOpt.testOffset[1] = g_unitsTestOffset[1];
	o.unitsOpt.quickCombat = g_unitsQuickCombat;   // ticket 0796 (rule A; TEST ONLY)
	o.install = c.bakeInstall;   // the game install (water art)
	o.staleFrames = c.staleFrames;
	// issue 3308: the tile source
	o.tiles = c.tiles == "tap" ? SceneOptions::kTilesTap : c.tiles == "none" ? SceneOptions::kTilesNone : SceneOptions::kTilesBake;
	o.bake.pipeline = c.baker != "standin";
	o.bake.install = c.bakeInstall;
	o.bake.exe = c.exe;
	o.bake.res.radius = c.bakeRadius;
	o.bake.res.rate = c.bakeRate;
	o.bake.res.burst = c.bakeBurst;
	o.bake.res.gpuMsPerS = c.bakeGpuMsPerS;
	o.bake.budgetMs = c.bakeBudgetMs;
	o.bake.res.staleBeforeDraw = g_bakeStaleBeforeDraw;   // issue 0140; the yield is set by CreateDevice (it needs the Scene)
	o.bake.faults.failX = c.testBakeFailX;
	o.bake.faults.failY = c.testBakeFailY;
	o.bake.faults.cpuMs = g_testBakeCpuMs;
	o.bake.warm = g_bakeWarm != 0;   // issue 0417
	o.bake.res.nearCaches = g_bakeNear != 0;   // issue 0371
	o.bake.res.nearOptions.minClass = o.bake.res.sizeClass;   // [0xD90F30]: the near pairs' smallest size is the cache class
	return o;
}

// ---- epic 0301, issue 3904: the scene recorder (srv_scene_recorder.h; every function here is a no-op without one) -------
// scene.json's render_config: every resolved value the scene reads (strings, contract 1)
std::map<std::string, std::string> RecorderRenderConfig(const HostConfig& c)
{
	std::map<std::string, std::string> m;
	char b[64];
	m["render_handover"] = c.handover;
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%ux%u", c.width, c.height);
	m["render_size"] = b;
	m["render_depth"] = c.depth ? "1" : "0";
	m["render_pass"] = std::to_string(c.pass);
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%g", c.tess);
	m["render_tess"] = b;
	m["render_tile_plots"] = std::to_string(c.tilePlots);
	m["render_tile_d4"] = std::to_string(c.tileD4);
	m["render_water"] = !c.water ? "0" : g_waterMode == WaterSceneOptions::kPlaceholder ? "placeholder" : "1";
	m["render_water_freeze"] = g_waterFreeze ? "1" : "0";
	m["render_ice_reflection"] = WaterSceneReflectIce() ? "1" : "0";   // issue 0382
	m["render_overlay"] = std::to_string(c.overlay);
	m["render_color"] = c.color;
	m["render_fallback"] = c.fallback;
	_snprintf_s(b, sizeof(b), _TRUNCATE, "0x%08X", c.constantRgba);
	m["render_constant_rgba"] = b;
	m["render_stale_frames"] = std::to_string(c.staleFrames);
	m["render_tiles"] = c.tiles;
	m["render_baker"] = c.baker;
	m["render_bake_radius"] = std::to_string(c.bakeRadius);
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%g", c.bakeBudgetMs);
	m["render_bake_budget_ms"] = b;
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%g", c.bakeRate);
	m["render_bake_rate"] = b;
	m["render_bake_burst"] = std::to_string(c.bakeBurst);
	_snprintf_s(b, sizeof(b), _TRUNCATE, "%g", c.bakeGpuMsPerS);
	m["render_bake_gpu_ms_per_s"] = b;
	m["render_bake_install"] = Narrow(c.bakeInstall);
	m["render_bake_near"] = std::to_string(g_bakeNear);   // issue 0371
	m["render_exe"] = Narrow(c.exe);
	// issue 3911 (format version 2): the fog's two keys; render_fow_level is replaced after Scene::init by the level the fog
	// resolved (render_fow_level, else the player's GraphicsSettingsDX11.ini, else 3), so that the player reads no ini
	m["render_fow"] = std::to_string(g_fow);
	m["render_fow_level"] = std::to_string(g_fowLevel);
	m["render_fow_mask_window"] = RenderFowMaskWindowName(RenderFowMaskWindowOption());   // ticket 0674
	m["render_fow_overlay_history"] = RenderFowOverlayHistoryName(RenderFowOverlayHistoryOption());   // ticket 0793
	m["render_shadow"] = std::to_string(g_shadow);   // issue 4308 (format version 3)
	m["render_ice"] = std::to_string(g_ice);
	// issue 0364 (format version 3): the decals as the scene runs them (render_decals with the server's bake only) and the
	// mods' folder their names and textures are read from (ModFolders: C5_DOCUMENTS, else Documents), for the player
	const bool decals = g_decals && c.tiles == "bake";
	m["render_decals"] = decals ? "1" : "0";
	std::wstring modsRoot;
	if(decals)
		ModFolders(&modsRoot);
	m["render_decal_mods"] = Narrow(modsRoot);
	m["render_units"] = std::to_string(g_units);              // epic 0375, issue 4454
	m["render_units_passes"] = std::to_string(g_unitPasses);
	m["render_units_ready_hold_ms"] = std::to_string((long long)g_unitsReadyHoldMs);   // epic 0040, issue 4753
	m["render_units_positions"] = g_unitsPositions == SceneOptions::kUnitsPositionsExe ? "exe" : "own";
	// ticket 4785: the live side's units cast into the tapped map with 1 (the bundle's shadow member holds the merged map's
	// input, the tap's copy; the player does not draw the units' mask yet: rule A's key route)
	m["render_units_shadow"] = std::to_string(g_unitsShadow);
	// ticket 4737 (rule A): the fights' approach rule (the player replays it; "1" by default since 4743 W81; absent = 0, a
	// bundle recorded before 4737) and the trigger stream's tier gate (the player sends no triggers: a record of what the
	// live side sent)
	m["render_test_units_charge_any"] = std::to_string(g_unitsChargeAny);
	m["render_test_units_tier_gate"] = std::to_string(g_unitsTierGate);
	m["render_test_units_turn_first"] = std::to_string(g_unitsTurnFirst);   // ticket 4735 (rule A): absent = 0 in the player
	m["render_units_fight_drawn_figures"] = std::to_string(g_unitsFightDrawn);   // ticket 4744 (rule A): absent = 0 in the player
	m["render_test_units_reform_first"] = std::to_string(g_unitsReformFirst);   // tickets 4745 + 4747 (rule A): absent = 0 in the player
	m["render_test_units_walk_home_in_fight"] = std::to_string(g_unitsWalkHome);   // ticket 4742 (rule A): absent = 0 in the player
	m["render_test_units_quick_combat"] = std::to_string(g_unitsQuickCombat);   // ticket 0796 (rule A): "0" by default; absent = 0 in the player (a bundle before 0796)
	m["render_test_units_city_in_place"] = std::to_string(g_unitsCityInPlace);   // ticket 4748 (rule A): absent = 0 in the player
	m["render_test_units_ready_after_move"] = std::to_string(g_unitsReadyAfterMove);   // ticket 4746 (rule A): absent = 0 in the player
	m["render_test_units_defend_from_stance"] = std::to_string(g_unitsDefendFromStance);   // ticket 4750 (rule A): absent = 0 in the player
	m["render_test_units_reform_ends_at_arrival"] = std::to_string(g_unitsReformEndsAtArrival);   // ticket 5050 (rule A): "1" by default since W74; absent = 0 in the player (a bundle before 5050)
	m["render_units_rush_ride_through"] = std::to_string(g_unitsRushRideThrough);   // ticket 5057 (rule A): "1" by default; absent = 0 in the player (a bundle before 5057)
	m["render_units_defender_waits_first_landing"] = std::to_string(g_unitsDefenderWaitsFirstLanding);   // ticket 5057 (rule A): "1" by default; absent = 0 in the player
	m["render_test_units_death_by_variant"] = std::to_string(g_unitsDeathByVariant);   // ticket 5053 (rule A): "1" by default (MC, 2026-11-25); absent = 0 in the player (a bundle before 5053)
	m["render_test_units_end_after_dying"] = std::to_string(g_unitsEndAfterDying);   // ticket 5053 (rule A): "1" by default (MC, 2026-11-25); absent = 0 in the player
	m["render_units_melee_reach"] = std::to_string(g_unitsMeleeReach);   // ticket 0743 (rule A): "1" by default; absent = 0 in the player (a bundle before 0743)
	m["render_units_defender_step"] = std::to_string(g_unitsDefenderStep);   // ticket 5061 (rule A): "1" by default; absent = 0 in the player (a bundle before 5061)
	m["render_units_victory_word"] = std::to_string(g_unitsVictoryWord);   // ticket 5059 (rule A): "1" by default; absent = 0 in the player (a bundle before 5059)
	m["render_units_type2_phase2"] = std::to_string(g_unitsType2Phase2);   // ticket 5067 S1 (rule A): "1" by default (MC #12907); absent = 0 in the player (a bundle before 5067)
	m["render_units_orders_after_walk"] = std::to_string(g_unitsOrdersAfterWalk);   // ticket 5065 (rule A): "1" by default; absent = 0 in the player (a bundle before 5065)
	m["render_test_units_attack_from_stance"] = std::to_string(g_unitsAttackFromStance);   // ticket 5050 (rule A): "1" by default since W74; absent = 0 in the player (a bundle before 5050)
	m["render_test_units_trigger_striker_weapon"] = std::to_string(g_unitsTriggerStrikerWeapon);   // ticket 5052 (rule A): default 1; absent = 0 in the player (no pixel depends on it)
	m["render_test_units_trigger_terrain"] = std::to_string(g_unitsTriggerTerrain);   // ticket 5052 (rule A): default 1 (MC #11356); absent = 0 in the player (no pixel depends on it)
	m["render_units_projectile_pose"] = std::to_string(g_unitsProjectilePose);   // ticket 5064 (rule A): default 1; absent = 0 in the player (the projectiles are the EXE's effects)
	m["render_units_city_volley"] = std::to_string(g_unitsCityVolley);   // ticket 0742 (rule A): default 1; absent = 0 in the player (the projectiles are the EXE's effects)
	m["render_units_trigger_striker_melee"] = std::to_string(g_unitsTriggerStrikerMelee);   // ticket 5055 (rule A): default 1 (MC #11607); absent = 0 in the player (no pixel depends on it)
	m["render_units_trigger_city_era"] = std::to_string(g_unitsTriggerCityEra);   // ticket 5058 (rule A): default 1; absent = 0 in the player
	m["render_units_fight_after_walk"] = std::to_string(g_unitsFightAfterWalk);   // ticket 5066 (rule A): default 1; absent = 0 in the player (a bundle before 5066)
	m["render_units_trigger_city_shot"] = std::to_string(g_unitsTriggerCityShot);   // ticket 5062 (rule A): default 1; absent = 0 in the player
	m["render_arc"] = std::to_string(g_arc);   // ticket 0199 part 2: the arc's records are in feed.bin; the player draws them with 1
	// ticket 0300 part 2: the path's records (kind 4 and 8) are in feed.bin; the player does not draw the path yet (rule A's
	// key route: a bundle says what its live side drew)
	m["render_path"] = std::to_string(g_path);
	// ticket 4875 (format version 9): the model module's keys; the recorder writes modeltap/frame<NNNNNN>.mtap with them
	m["render_cities"] = g_models.cities ? "1" : "0";
	m["render_landmarks"] = g_models.landmarks ? "1" : "0";
	m["render_models_source"] = g_models.source == srvmodels::kModelsSourceOwn ? "own" : "exe";
	if(g_models.drawAll)
		m["render_test_models_draw_all"] = "1";
	// ticket 0789 (rule A): the model store, with the model module on; a bundle recorded before 0789 has no key and is
	// played with the store off (0709's keep, what its live side drew)
	if(g_models.any())
		m["render_models_store"] = g_models.store ? "1" : "0";
	m["render_forest"] = std::to_string(g_forest);   // ticket 0495 (format version 8); the recorder sets it again at the span
	// ticket 4852: the farm producer (the recorder sets it again at the span, with render_farmfields_state); keep_tap only
	// when not the default, so a bundle without farm fields carries one key
	m["render_farmfields"] = std::to_string(g_farmfields);
	if(g_farmKeepTap)
		m["render_farmfields_keep_tap"] = "1";
	// ticket 4934: only when on (the recorder adds render_roadlayout_state at the span), so a bundle without the road
	// layout keeps the keys it had (rule A)
	if(g_roadLayout)
		m["render_roadlayout"] = std::to_string(g_roadLayout);
	if(g_roadLayout == 2)
	{   // ticket 4940: the all-connections layout's shape, only with it (a bundle of layout 1 keeps the keys it had)
		m["render_roadlayout_junction"] = std::to_string(g_roadJunction);
		m["render_roadlayout_jitter"] = std::to_string(g_roadJitter);
	}
	// ticket 0435: the live device's flag (not a renderer input: the player always creates its device with it); "1" says
	// the bundle's dumps were drawn byte-stable, so P1 holds at 0:0 for it
	m["render_prevent_driver_threading"] = g_preventDriverThreading ? "1" : "0";
	// ticket 0711 (J1): the frame jobs in force (the resolved worker count, never "auto"); the player replays a bundle without
	// them with 0. Not a renderer input: the picture is byte-equal with any value (R8, rule A with 0, 1 and 1 + jitter)
	m["render_frame_jobs"] = std::to_string(c.frameJobs);
	m["render_test_frame_jobs_jitter"] = c.frameJobsJitter ? "1" : "0";
	// ticket 0753 (J3): record ahead as keyed (auto | 0 | 1; the player resolves auto on its own device). Not a renderer
	// input either: the same calls in the same order (rule A with off, on and on + jitter)
	m["render_record_ahead"] = c.recordAhead < 0 ? "auto" : c.recordAhead ? "1" : "0";
	return m;
}

// At the render thread's start, before the first device: the recorder, its feed wrapper and the readers' sinks. A folder
// it cannot use is LOUD and the renderer runs without it (the game goes on).
void StartRecorder(Host& h)
{
	if(h.cfg.testRecord.empty())
		return;
	const std::map<std::string, std::string>& kv = h.cfg.testRecord;
	RecorderKeys k;
	std::string why;
	if(!ParseRecorderKeys([&](const char* key) { auto i = kv.find(key); return i == kv.end() ? std::string() : i->second; }, k, why) || !k.on)
	{
		RNote("LOUD: recorder off: %s; the game goes on", why.c_str());
		return;
	}
	RecorderContext c;
	c.renderConfig = RecorderRenderConfig(h.cfg);
	c.install = h.cfg.bakeInstall;
	c.exe = h.cfg.exe;
	if(h.cfg.tiles == "bake")
		c.versionNotes.push_back("gap: bake-cache tap left out (render_tiles=bake)");
	h.rec = SceneRecorder::Open(k, c, why);
	if(!h.rec)
	{
		RNote("LOUD: recorder off: %s; the game goes on", why.c_str());
		return;
	}
	h.recFeed = MakeRecordingFeed(LiveFeed(), *h.rec);
	h.feed = h.recFeed.get();
	// epic 0375, issues 4457 / 4456: the unit scene's table copies through the recording feed (feed.bin records 3 UNITS and
	// 6 PLAYERS of caller 1), so that unitfeed replays the copies the scene took
	if(h.scene.units())
	{
		h.scene.units()->useFeedSource(h.recFeed.get());
		// issue 0440 (format version 5): the scene's ready() per frame and its database (units_history.bin, units/gen<...>.db)
		h.rec->setUnits({h.scene.units(), [](std::wstring& p, uint32_t& g) {
			                 return !g_unitsDb.empty() ? (p = g_unitsDb, g = 1, true) : SrvFeed::GetUnitDatabase(p, g); }});
		h.rec->setRenderConfig("render_units_anim", std::to_string(g_unitsAnim));
	}
	if(WaterTapReader* r = h.scene.water().reader())
		r->setImageSink(&SceneRecorder::WaterSink, h.rec.get());
	h.scene.setFieldSink(&SceneRecorder::FieldSink, h.rec.get());
	// issue 3911: the fog's plot-table reads through the recording feed (feed.bin PLOTS caller 3; before Scene::init, whose
	// fog start reads the table first) and its clock for fog_clock.bin
	h.scene.fow().setTable(MakeFeedSourcePlotTable(*h.recFeed));
	h.rec->setFog(&h.scene.fow());
	// issue 0364 (format version 3): the decal reader's set in force at the span's first frame (decals/load<NNNN>.dtap)
	if(g_decals && h.cfg.tiles == "bake")
	{
		Host* hp = &h;
		h.rec->setDecalReader([hp]() { return hp->scene.decalReader(); });
	}
	// ticket 4875 (format version 9): the model tap frames the module used (modeltap/frame<NNNNNN>.mtap, when they change)
	if(g_models.any())
	{
		Host* hp = &h;
		h.rec->setModels([hp]() -> const srvmodels::ModelsScene* { return hp->scene.models(); });
	}
	// ticket 0495 (format version 8): the forest's plot-table reads through the recording feed (PLOTS caller 4; set before
	// Scene::init, which makes the forest scene and its table), its state and the near pairs' at the span's first frame
	{
		Host* hp = &h;
		ForestPlotTableSlot() = [hp]() { return MakeFeedSourcePlotTable(*hp->recFeed, kPlotsForest); };
		h.rec->setForest([hp]() -> const SceneForest* { return hp->scene.forest(); });
		h.rec->setFarm([hp]() -> const FarmFieldsProducer* { return hp->scene.farm(); });   // ticket 4852
		h.rec->setRoads([hp]() -> const srvextras::SceneRoads* { return hp->scene.roads(); });   // ticket 4934
		h.rec->setLiveTerrain([hp]() -> const liveterrain::LiveTerrain* { return hp->scene.liveTerrain(); });   // ticket 4907
		h.rec->setNearState([hp](std::vector<uint8_t>& st, uint32_t& tx, uint32_t& ty, bool& on, std::vector<scenebundle::NearPairImage>& images) {
			on = false;
			images.clear();
			if(hp->cfg.tiles != "bake")
				return false;
			ResidencySource* rs = static_cast<ResidencySource*>(hp->scene.source());
			if(!rs || !rs->nearOn())
				return false;
			on = true;
			const c5r::bake::ResidencyStats* s = rs->stats();
			if(!s || !rs->nearState(st))
				return false;
			tx = s->tilesX;
			ty = s->tilesY;
			std::vector<c5r::bake::Residency::NearPairImage> imgs;   // the pairs whose content is behind (their texels)
			const c5r::Status is = rs->nearStaleImages(hp->ctx, imgs);
			if(!is.ok())
			{
				RNote("LOUD: recorder: the near pairs' images: %s (near.bin is not written)", is.str().c_str());
				return false;
			}
			for(auto& m : imgs)
			{
				scenebundle::NearPairImage o;
				o.tile = m.tile;
				o.size = m.size;
				o.second = m.second;
				o.texels = std::move(m.texels);
				images.push_back(std::move(o));
			}
			return tx && ty;
		});
	}
}

// After each Scene::init that succeeded: the recording overlay wrapper around a tap source of this device (the scene's
// own, made by init and not yet asked, is replaced) and the adapter for scene.json.
void RecorderAfterInit(Host& h)
{
	if(!h.rec)
		return;
	if(g_fow && h.scene.fow().level() >= 0)   // issue 3911: the level the fog resolved
		h.rec->setRenderConfig("render_fow_level", std::to_string(h.scene.fow().level()));
	if(h.cfg.overlay)
	{
		std::unique_ptr<OverlaySource> w = MakeRecordingOverlaySource(MakeTapOverlaySource(h.scene.header(), h.dev), *h.rec);
		h.recOvl = w.get();
		h.scene.setOverlaySource(std::move(w));
	}
	if(h.adapter)
	{
		DXGI_ADAPTER_DESC1 d;
		h.adapter->GetDesc1(&d);
		LARGE_INTEGER umd = {};
		char drv[64] = "";
		if(SUCCEEDED(h.adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
			_snprintf_s(drv, sizeof(drv), _TRUNCATE, "%u.%u.%u.%u", (unsigned)HIWORD(umd.HighPart), (unsigned)LOWORD(umd.HighPart),
			            (unsigned)HIWORD(umd.LowPart), (unsigned)LOWORD(umd.LowPart));
		h.rec->setAdapter(h.luid, Narrow(d.Description), drv);
	}
}

RecorderCounters RecCounters(Host& h)
{
	RecorderCounters c = h.recLast;
	if(const WaterTapReader* r = h.scene.water().reader())
		c.waterRefused = r->refused;
	if(h.recOvl)
	{
		c.overlayRefused = h.recOvl->refusedCount();
		c.overlayStale = h.recOvl->staleCount();
	}
	h.recLast = c;
	return c;
}

// The render thread's end, after the device (and with it the overlay wrapper and its pending texels) is released.
// Recorder exceptions never reach the thread's end: the recorder writes through c5r::Status and catches nothing itself.
void FinishRecorder(Host& h, const char* why)
{
	if(!h.rec)
		return;
	h.rec->finish(RecCounters(h), why);
	RNote("%s", h.rec->summary().c_str());
}

// The frame hook's part after the draw (DrawFrame): the frame's record, the chosen frames' targets staged.
void RecorderAfterDraw(Host& h, const SceneFrame& f, const vpframe::FrameTargets& t, bool drawOk, int64_t waterDraws0,
                       int64_t waveDraws0, const c5r::Status& ds)
{
	if(!drawOk)
	{
		h.rec->drawFailed(f.frameId, ds.str());
		return;
	}
	uint32_t flags = 0;
	if(h.scene.water().drawsTotal > waterDraws0)
		flags |= scenebundle::kFrameWaterModule;
	if(h.scene.water().waveDrawsTotal > waveDraws0)
		flags |= scenebundle::kFrameWaves;
	if(h.scene.fow().terrainInputs().on)   // issue 3911: the fog's mask was bound in this draw
		flags |= scenebundle::kFrameFog;
	h.rec->afterDraw(h.ctx, f, f.nowMs, t.width, t.height, t.colour, t.depth, h.scene.lastOverlay(), flags);
	// issue 4308: the frame's shadow triple and ice mesh (shadow.bin, shadow/, ice/)
	ShadowFrame sf;
	const bool sh = h.scene.lastShadowFrame(sf);
	const std::shared_ptr<const c5r::ice::Mesh> ice = h.scene.lastIceDrawn() ? h.scene.iceMesh() : nullptr;
	h.rec->afterDrawSeamG(h.ctx, sh ? &sf : nullptr, ice.get(), ice ? h.scene.iceGeneration() : 0, h.scene.iceLoadSeq());
}

void DrainD3dDebug(Host& h);   // ticket 0753 (below)

// Everything that belongs to one device. On failure: the reason (VPFSF_*) and the text; nothing is left half-created.
void ReleaseDevice(Host& h)
{
	h.recOvl = nullptr;   // epic 0301, issue 3904: the scene's overlay source goes with the device
	h.scene.releaseDevice();
	if(h.sink)
		h.sink->release();
	h.sink.reset();
	h.v3 = nullptr;
	if(h.ctx)
	{
		h.ctx->ClearState();
		h.ctx->Flush();
	}
	DrainD3dDebug(h);   // ticket 0753 (TEST ONLY): what the release said
	SafeRelease(h.infoQueue);
	SafeRelease(h.ctx);
	SafeRelease(h.dev);
}

// Ticket 0753 (TEST ONLY, render_test_d3d_debug=1): the debug layer's stored messages of severity corruption, error and
// warning as LOUD lines (at most 20 a call written; all counted), then the queue cleared.
void DrainD3dDebug(Host& h)
{
	ID3D11InfoQueue* q = h.infoQueue;
	if(!q)
		return;
	const UINT64 n = q->GetNumStoredMessages();
	int written = 0;
	for(UINT64 i = 0; i < n; ++i)
	{
		SIZE_T len = 0;
		if(FAILED(q->GetMessage(i, nullptr, &len)) || len == 0)
			continue;
		std::vector<char> buf(len);
		D3D11_MESSAGE* m = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
		if(FAILED(q->GetMessage(i, m, &len)))
			continue;
		if(m->Severity > D3D11_MESSAGE_SEVERITY_WARNING)
			continue;   // INFO and MESSAGE
		h.d3dDebugMessages[m->Severity]++;
		if(written++ < 20)
			RNote("LOUD: d3d debug: %s #%d: %.*s", m->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ? "CORRUPTION"
			                                       : m->Severity == D3D11_MESSAGE_SEVERITY_ERROR    ? "ERROR"
			                                                                                         : "WARNING",
			      (int)m->ID, (int)(m->DescriptionByteLength ? m->DescriptionByteLength - 1 : 0), m->pDescription ? m->pDescription : "");
	}
	q->ClearStoredMessages();
}

uint32_t CreateDevice(Host& h, std::string& why)
{
	uint64_t want = h.cfg.luid;
	if(!want && h.scene.header())
		want = h.scene.header()->clientLuid;   // the composite's adapter, when it has connected
	if(!PickAdapter(h, want) && !(want && !h.cfg.luid && PickAdapter(h, 0)))
	{
		why = "no adapter";
		return VPFSF_ADAPTER;
	}
	D3D_FEATURE_LEVEL fl;
	const D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	// ticket 0435: render_prevent_driver_threading
	UINT flags = g_preventDriverThreading ? (UINT)D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS : 0u;
	if(g_testD3dDebug)   // ticket 0753 (TEST ONLY): the debug layer
		flags |= (UINT)D3D11_CREATE_DEVICE_DEBUG;
	HRESULT hr = D3D11CreateDevice(h.adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, fls, 2, D3D11_SDK_VERSION, &h.dev, &fl, &h.ctx);
	if(FAILED(hr))
	{
		char b[96];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "D3D11CreateDevice failed 0x%08lX (flags 0x%X)", hr, flags);
		why = b;
		return VPFSF_DEVICE;
	}
	h.deviceFlags = flags;
	RNote("device created, feature level %x, flags 0x%X (render_prevent_driver_threading=%d)", (unsigned)fl, flags,
	      g_preventDriverThreading ? 1 : 0);
	if(g_testD3dDebug)   // ticket 0753 (TEST ONLY): the debug layer's queue, read after each pass (DrainD3dDebug)
	{
		SafeRelease(h.infoQueue);
		h.dev->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&h.infoQueue);
		if(h.infoQueue)
			h.infoQueue->SetMessageCountLimit(4096);
		RNote("LOUD: TEST KEY render_test_d3d_debug=1: the D3D debug layer is on (info queue %s); its corruption, error and warning "
		      "messages are written as 'LOUD: d3d debug:' lines (ticket 0753)", h.infoQueue ? "read" : "NOT available");
	}
	vpframe::SinkConfig sc;
	sc.baseName = h.cfg.section.c_str();
	sc.slots = h.cfg.ringSlots;
	sc.sync = h.cfg.ringSync;
	sc.adapterLuid = h.luid;
	if(h.cfg.handover == "ring")
	{
		vpframe::RingSink* r = new vpframe::RingSink();
		r->setLog(&RingLog, nullptr);
		h.sink.reset(r);
	}
	else
	{
		V3Options o;
		o.textureBase = h.cfg.textureBase;
		o.width = h.cfg.width;
		o.height = h.cfg.height;
		o.depth = h.cfg.depth;
		o.gpuSync = h.cfg.gpuSync;
		V3Sink* v = new V3Sink(o);
		h.sink.reset(v);
		h.v3 = v;
	}
	if(!h.sink->open(h.dev, h.ctx, sc))
	{
		why = std::string("the ") + h.sink->name() + " sink could not open its section (see render.log)";
		ReleaseDevice(h);
		return VPFSF_SECTION;
	}
	SceneOptions so = SceneOpts(h.cfg);
	if(g_bakeYield)   // issue 0140: h (and h.scene) lives as long as the residency, which calls this on the render thread
	{
		so.bake.res.yield = &CameraWaiting;
		so.bake.res.yieldCtx = &h;   // ticket 0414: the Host, for its bake count (was &h.scene)
	}
	so.bake.feed = h.recFeed.get();   // epic 0301, issue 3904: the recording feed; null (no recorder) = LiveFeed(), today's
	const c5r::Status st = h.scene.init(h.dev, h.ctx, so);
	if(!st.ok())
	{
		why = "terrain renderer: " + st.str();
		ReleaseDevice(h);
		return VPFSF_RENDERER;
	}
	if(g_ice && !g_iceTestFile.empty())   // issue 4308: render_test_ice_file
	{
		std::vector<uint8_t> b;
		FILE* fp = nullptr;
		std::string err;
		if(_wfopen_s(&fp, scenebundle::Widen(g_iceTestFile).c_str(), L"rb") == 0 && fp)
		{
			uint8_t buf[65536];
			size_t n;
			while((n = fread(buf, 1, sizeof(buf), fp)) > 0)
				b.insert(b.end(), buf, buf + n);
			fclose(fp);
		}
		else
			err = "cannot open the file";
		std::shared_ptr<c5r::ice::Mesh> mesh(new c5r::ice::Mesh());
		uint32_t gen = 0, load = 0;
		scenebundle::CheckResult cr;
		if(err.empty() && !scenebundle::ParseIceMesh(b, mesh.get(), &gen, &load, &cr).ok())
			err = cr.line();
		if(err.empty())
		{
			h.scene.setIceSource(MakeStaticIceSource(mesh, gen, load));
			RNote("LOUD: render_test_ice_file='%s' (a test key): the scene draws this ice mesh (generation %u, load %u, %zu chunks), not the ice "
			      "tap's",
			      g_iceTestFile.c_str(), gen, load, mesh->chunks.size());
		}
		else
			RNote("LOUD: render_test_ice_file='%s': %s; the ice tap's reader stays", g_iceTestFile.c_str(), err.c_str());
	}
	RecorderAfterInit(h);   // epic 0301, issue 3904
	return VPFSF_NONE;
}

bool StopRequested(Host& h, DWORD ms = 0) { return WaitForSingleObject(h.stop, ms) == WAIT_OBJECT_0; }

// A lost device: release everything, back off, create again on the same adapter, re-apply the field. False = FAILED or
// stop requested.
bool Rebuild(Host& h, const char* why)
{
	h.lost++;
	if(VPFrameServer* s = FrameServer(h))
		s->deviceLostTotal = h.lost;
	const int64_t now = Qpc();
	h.lostAt.push_back(now);
	while(!h.lostAt.empty() && QpcToMs(now - h.lostAt.front()) > kRebuildWindowMs)
		h.lostAt.pop_front();
	RNote("LOUD: device lost (%s): %d in the last 5 minutes", why, (int)h.lostAt.size());
	ReleaseDevice(h);
	if((int)h.lostAt.size() >= kMaxRebuildsInWindow)
	{
		Fail(h, VPFSF_REBUILDS, std::to_string(h.lostAt.size()) + " lost devices in 5 minutes");
		return false;
	}
	SetState(h, VPFS_REBUILDING);
	uint32_t backoff = h.cfg.rebuildBackoffMs;
	for(size_t k = 1; k < h.lostAt.size(); ++k)
		backoff = backoff * 2 > kMaxBackoffMs ? kMaxBackoffMs : backoff * 2;
	std::string text;
	uint32_t reason = VPFSF_DEVICE;
	for(int attempt = 1; attempt <= kMaxCreateAttempts; ++attempt)
	{
		RNote("rebuild: waiting %u ms before attempt %d", backoff, attempt);
		if(StopRequested(h, backoff))
			return false;
		reason = CreateDevice(h, text);
		if(reason == VPFSF_NONE)
		{
			h.rebuilds++;
			SetState(h, VPFS_READY);
			RNote("rebuild: device and renderer created again (rebuild %lld)", (long long)h.rebuilds);
			LogMemory(h, "after rebuild", true);
			return true;
		}
		RNote("rebuild: attempt %d failed: %s", attempt, text.c_str());
		backoff = backoff * 2 > kMaxBackoffMs ? kMaxBackoffMs : backoff * 2;
	}
	Fail(h, reason, "rebuild failed: " + text);
	return false;
}

// ---- ticket 0688 (epic 0686 P1): the pass's stage timer (shell/common/vp_pacing.h; research/08-performance/
// frame-pacing-instruments.md). A pass is one iteration of Loop: from the end of one pacing wait to the end of the next.
// Every stage switch is one QueryPerformanceCounter; the lines go through RLog (ticket 5054's log thread: no file write on
// the render thread). HostConfig::passStats = 0: no switch is timed and no line is written.
constexpr int kPassLongLinesPerSec = 20;   // "pass long:" lines a second at most (the rest counted: long_unlogged)
bool ReadPublish(Host& h, int64_t& frameId, int64_t& publishQpc, int64_t& deadlineQpc);

inline void PassTo(Host& h, int stage)
{
	if(h.cfg.passStats)
		h.pass.to(stage, Qpc());
}

// Loop's top: the pass that ended (its wait included) is counted, a long one gets its line; the next pass begins.
void PassRoll(Host& h, int64_t now)
{
	if(!h.cfg.passStats)
		return;
	if(h.pass.start)
	{
		h.pass.to(vppacing::kStOther, now);
		h.passSec.add(h.pass);
		h.passesRun++;
		const int64_t busy = h.pass.busy();
		if(QpcToMs(busy) >= h.cfg.passLongMs)
		{
			h.passSec.longPasses++;
			h.passLongRun++;
			if(h.passSec.longPasses <= kPassLongLinesPerSec)
			{
				char parts[512];
				int k = 0;
				parts[0] = 0;
				for(int i = 0; i < vppacing::kStageCount; ++i)
					if(h.pass.t[i])
						k += _snprintf_s(parts + k, sizeof(parts) - k, _TRUNCATE, " %s=%.2f", vppacing::kStageNames[i], QpcToMs(h.pass.t[i]));
				const int top = h.pass.top();
				RLog("pass long: busy_ms=%.1f top=%s top_ms=%.1f frame=%lld wake_ms=%.2f qpc0=%lld qpc1=%lld |%s", QpcToMs(busy),
				     vppacing::kStageNames[top], QpcToMs(h.pass.t[top]), (long long)h.passFrameId, h.passWakeMs, (long long)h.pass.start,
				     (long long)(h.pass.start + busy), parts);
			}
			else
				h.passSec.longUnlogged++;
		}
	}
	h.pass.begin(now);
	h.passFrameId = 0;
	h.passWakeMs = h.wakeNextMs;   // the wait that ended this moment (PassWoken)
	h.wakeNextMs = -1.0;
	vppacing::CurrentPass() = &h.pass;   // the scene's StageScope marks (the render thread only)
}

// Woken: the delay from the newest camera's publish (the composite's QPC) to this thread running again.
void PassWoken(Host& h, bool byCamera)
{
	if(!h.cfg.passStats || !byCamera)
		return;
	int64_t id = 0, pubQpc = 0, deadline = 0;
	const int64_t now = Qpc();
	if(!ReadPublish(h, id, pubQpc, deadline) || pubQpc <= 0 || pubQpc > now)
		return;
	h.wakeNextMs = QpcToMs(now - pubQpc);
	h.passSec.addWake(h.wakeNextMs);
}

// The 1 s line of the stages (SecondLine): per stage the second's maximum and mean per pass, in ms.
void PassSecondLine(Host& h)
{
	if(!h.cfg.passStats)
		return;
	const vppacing::PassSecond& s = h.passSec;
	char parts[1024];
	int k = 0;
	parts[0] = 0;
	for(int i = 0; i < vppacing::kStageCount; ++i)
		k += _snprintf_s(parts + k, sizeof(parts) - k, _TRUNCATE, " %s=%.2f/%.3f", vppacing::kStageNames[i], QpcToMs(s.maxT[i]),
		                 s.passes ? QpcToMs(s.sumT[i]) / (double)s.passes : 0.0);
	// ticket 0711: the `draw` stage in its three stretches, max/mean per pass as the stages (they add up to draw's): the head
	// before the units' mark, the stretch after the water (arc, extras, the path's depth), the end (cloud planes, path)
	static const char* const kDrawPart[3] = {"draw_head", "draw_mid", "draw_end"};
	if(k > 0 && k < (int)sizeof(parts))
		k += _snprintf_s(parts + k, sizeof(parts) - k, _TRUNCATE, " | draw split max/mean ms:");
	for(int i = 0; i < 3 && k > 0 && k < (int)sizeof(parts); ++i)
	{
		k += _snprintf_s(parts + k, sizeof(parts) - k, _TRUNCATE, " %s=%.2f/%.3f", kDrawPart[i], QpcToMs(h.drawSplitMax[i]),
		                 s.passes ? QpcToMs(h.drawSplitSum[i]) / (double)s.passes : 0.0);
		h.drawSplitMax[i] = h.drawSplitSum[i] = 0;
	}
	RLog("pass 1s: passes=%lld long=%lld long_unlogged=%lld long_ms=%.1f busy_max=%.2f busy_mean=%.3f wake_n=%lld wake_mean=%.2f "
	     "wake_max=%.2f passes_run=%lld long_run=%lld qpc_now=%lld | max/mean ms:%s",
	     (long long)s.passes, (long long)s.longPasses, (long long)s.longUnlogged, h.cfg.passLongMs, QpcToMs(s.maxBusy),
	     s.passes ? QpcToMs(s.sumBusy) / (double)s.passes : 0.0, (long long)s.wakeN, s.wakeN ? s.wakeSumMs / (double)s.wakeN : -1.0,
	     s.wakeN ? s.wakeMaxMs : -1.0, (long long)h.passesRun, (long long)h.passLongRun, (long long)Qpc(), parts);
	h.passSec.reset();
}

// Every 10 s: the server's video memory against the budget Windows gives it (IDXGIAdapter3::QueryVideoMemoryInfo), local
// and non-local, with the process's private bytes (research/07 architect-2026-11-22-frame-pacing.md M5).
void VidmemLine(Host& h)
{
	if(!h.cfg.passStats)
		return;
	PROCESS_MEMORY_COUNTERS_EX pmc;
	ZeroMemory(&pmc, sizeof(pmc));
	pmc.cb = sizeof(pmc);
	GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
	DXGI_QUERY_VIDEO_MEMORY_INFO lo = {}, nl = {};
	bool okL = false, okN = false;
	IDXGIAdapter3* a3 = nullptr;
	if(h.adapter && SUCCEEDED(h.adapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)))
	{
		okL = SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &lo));
		okN = SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nl));
		a3->Release();
	}
	const double mb = 1048576.0;
	RLog("vidmem 10s: process=server private_mb=%.1f working_mb=%.1f adapter=%d local_usage_mb=%.1f local_budget_mb=%.1f "
	     "local_reserved_mb=%.1f nonlocal_usage_mb=%.1f nonlocal_budget_mb=%.1f qpc_now=%lld",
	     pmc.PrivateUsage / mb, pmc.WorkingSetSize / mb, a3 ? 1 : 0, okL ? lo.CurrentUsage / mb : -1.0, okL ? lo.Budget / mb : -1.0,
	     okL ? lo.CurrentReservation / mb : -1.0, okN ? nl.CurrentUsage / mb : -1.0, okN ? nl.Budget / mb : -1.0, (long long)Qpc());
}

// Reads the publish block (seqlock). False when the composite never wrote it (legacy pacing) or it was being written.
bool ReadPublish(Host& h, int64_t& frameId, int64_t& publishQpc, int64_t& deadlineQpc)
{
	if(!h.frame)
		return false;
	const VPFramePublish* p = VPFRAME_PUBLISH(h.frame);
	for(int tries = 0; tries < 8; ++tries)
	{
		const uint32_t a = p->seq;
		if(a == 0)
			return false;
		MemoryBarrier();
		if(a & 1)
		{
			YieldProcessor();
			continue;
		}
		frameId = p->frameId;
		publishQpc = p->publishQpc;
		deadlineQpc = p->deadlineQpc;
		MemoryBarrier();
		if(p->seq == a)
			return true;
	}
	return false;
}

// One frame: begin, draw, end. False = the device was lost (the caller rebuilds).
bool DrawFrame(Host& h, const SceneFrame& fr, int64_t wakeQpc)
{
	vpframe::FrameInfo fi;
	fi.frameId = fr.frameId;
	fi.wakeQpc = wakeQpc;
	fi.proj22 = fr.cam.proj[10];   // D3DX row-major: [2][2], [3][2]
	fi.proj32 = fr.cam.proj[14];
	fi.nearZ = fr.cam.nearZ;
	fi.farZ = fr.cam.farZ;
	int64_t pubId = 0, pubQpc = 0, deadline = 0;
	// issue 3312: while the composite does not publish, the block's last frame says nothing about this camera
	const bool lockstep = !h.paceFallback && ReadPublish(h, pubId, pubQpc, deadline);
	if(lockstep && pubId == fr.frameId)
		fi.publishQpc = pubQpc;
	if(lockstep && h.lastDrawnFrameId && fr.frameId > h.lastDrawnFrameId + 1)
	{
		h.camerasSkipped += fr.frameId - h.lastDrawnFrameId - 1;
		if(VPFrameServer* s = FrameServer(h))
			s->camerasSkipped = h.camerasSkipped;
	}
	vpframe::FrameTargets t;
	PassTo(h, vppacing::kStSlot);   // ticket 0688
	h.passFrameId = fr.frameId;
	SinkStatus st = h.sink->begin(fi, t);
	if(st == SinkStatus::Busy && lockstep && deadline)
	{
		// 3302's rule: no free slot because the composite's GPU still reads the others: retry until the camera's deadline
		// (the composite gives slots back inside its wait) instead of skipping the camera at once
		while(st == SinkStatus::Busy && Qpc() < deadline && !StopRequested(h))
		{
			YieldProcessor();
			if(h.sink->freeSlots())
				st = h.sink->begin(fi, t);
		}
		if(st == SinkStatus::Ok)
			h.busyRetried++;
	}
	if(st == SinkStatus::DeviceLost)
		return false;
	if(st != SinkStatus::Ok)
	{
		if(st == SinkStatus::Busy)
			h.busy++;
		else if(st == SinkStatus::NotReady)
			h.notReady++;
		else
			h.sinkErrors++;
		return true;
	}
	h.draws++;
	// test switches (render_test_*): the failure paths of the design, forced
	if(h.cfg.testThrowAt && h.draws == h.cfg.testThrowAt)
	{
		RNote("test: render_test_throw at draw %lld", (long long)h.draws);
		throw std::runtime_error("render_test_throw");
	}
	if(h.cfg.testAvAt && h.draws == h.cfg.testAvAt)
	{
		RNote("test: render_test_av at draw %lld", (long long)h.draws);
		*(volatile int*)(uintptr_t)8 = 1;   // an access violation on the render thread
	}
	if(h.cfg.testDeviceLostEvery && h.draws % h.cfg.testDeviceLostEvery == 0)
	{
		RNote("test: render_test_device_lost at draw %lld", (long long)h.draws);
		h.sink->abandon();
		return false;
	}
	c5t::FrameStats fs;
	// epic 0301, issue 3904: the recorder's frame hook (nothing of it without render_test_record): a frame of the span is
	// drawn with the clock value the recorder reads and records (SceneFrame::nowMs; else the scene reads the clock itself)
	SceneFrame recFrame;
	bool recIn = false;
	int64_t recWater0 = 0, recWaves0 = 0;
	if(h.rec)
	{
		double now = -1;
		recIn = h.rec->beforeDraw(fr.frameId, h.published, h.scene.water(), &now, &fr);
		recFrame = fr;
		recFrame.nowMs = now;   // issue 0440: with units recorded, a frame before the span gets the recorder's clock too (else -1)
		if(recIn)
		{
			recWater0 = h.scene.water().drawsTotal;
			recWaves0 = h.scene.water().waveDrawsTotal;
		}
	}
	PassTo(h, vppacing::kStDraw);   // ticket 0688 (the scene's own marks claim its pre-draw bakes and copies)
	const int64_t submit0 = Qpc();   // ticket 0435: the frame's submission, Scene::draw to the sink's end
	FILETIME cpu0[4] = {};   // ticket 5054: the thread's CPU over the submission (a slow one names its parts)
	GetThreadTimes(GetCurrentThread(), &cpu0[0], &cpu0[1], &cpu0[2], &cpu0[3]);
	const c5r::Status ds = h.scene.draw(h.ctx, t.colour, t.depth, t.width, t.height, h.rec ? recFrame : fr, fs);
	const int64_t drawEnd = Qpc();
	h.ctx->OMSetRenderTargets(0, nullptr, nullptr);   // the sink copies from both targets (shell_server unbinds too)
	if(recIn)
		RecorderAfterDraw(h, recFrame, t, ds.ok(), recWater0, recWaves0, ds);   // epic 0301, issue 3904
	if(!ds.ok())
	{
		h.drawErrors++;
		h.sink->abandon();   // the last published frame stays (shell_server's rule)
		const HRESULT rr = h.dev->GetDeviceRemovedReason();
		return rr == S_OK;
	}
	vpframe::DrawReport rep;
	rep.result = VPDRAW_OK;
	rep.cpuMs = (float)fs.cpuMs;
	rep.gpuMs = (float)fs.gpuMs;
	rep.tilesDrawn = fs.tilesDrawn;
	// issue 0139: the gray tiles of this frame, counted by the renderer at the draw (independent of the residency's
	// counters); counted only when the frame draws from tile caches
	{
		c5t::ColorSource cs = c5t::ColorSource::Material;
		ColorSourceFrom(h.cfg.color, false, cs);
		rep.tilesUnbaked = cs == c5t::ColorSource::Caches ? (int32_t)fs.landTilesWithoutCache : -1;
	}
	PassTo(h, vppacing::kStHandover);   // ticket 0688
	if(h.cfg.passStats)   // ticket 0711: the draw stage's three stretches of this pass (the end is the remainder)
	{
		const int64_t head = h.scene.drawHeadTicks(), mid = h.scene.drawMidTicks();
		const int64_t part[3] = {head, mid, h.pass.t[vppacing::kStDraw] - head - mid};
		for(int i = 0; i < 3; ++i)
		{
			h.drawSplitSum[i] += part[i];
			h.drawSplitMax[i] = (std::max)(h.drawSplitMax[i], part[i]);
		}
	}
	const int64_t end0 = Qpc();
	st = h.sink->end(fi, rep);
	{   // ticket 0435 (a recorded frame's staging copies are inside, its readback after the end is not; timing records nothing)
		const int64_t endQ = Qpc();
		const int64_t q = endQ - submit0;
		h.submitQpcSec += q;
		h.submitMaxQpcSec = std::max(h.submitMaxQpcSec, q);
		h.submitNSec++;
		if(QpcToMs(q) >= 1000.0)   // ticket 5054: a submission of 1 s or more names its part (a wait, when the CPU is small)
		{
			FILETIME cpu1[4] = {};
			GetThreadTimes(GetCurrentThread(), &cpu1[0], &cpu1[1], &cpu1[2], &cpu1[3]);
			auto ticks = [](const FILETIME& a) { return ((uint64_t)a.dwHighDateTime << 32) | a.dwLowDateTime; };
			const double cpuMs = (double)(ticks(cpu1[2]) + ticks(cpu1[3]) - ticks(cpu0[2]) - ticks(cpu0[3])) / 10000.0;
			RNote("host: frame %lld's submission took %.1f ms: scene draw %.1f ms, between %.1f ms, sink end %.1f ms; thread CPU "
			      "%.1f ms (ticket 5054)",
			      (long long)fr.frameId, QpcToMs(q), QpcToMs(drawEnd - submit0), QpcToMs(end0 - drawEnd), QpcToMs(endQ - end0), cpuMs);
		}
	}
	PassTo(h, vppacing::kStOther);   // ticket 0688
	if(recIn)
		h.rec->afterEnd(h.ctx);   // epic 0301, issue 3904: the staged dumps mapped and written after the sink's end
	if(st == SinkStatus::DeviceLost)
		return false;
	if(st == SinkStatus::Ok)
	{
		h.published++;
		h.lastDrawnFrameId = fr.frameId;
		if(h.v3 && !h.dumped && !h.cfg.testDump.empty() && h.published >= h.cfg.testDumpAfter)
		{
			h.dumped = true;
			const bool ok = h.v3->dump(h.cfg.testDump.c_str());
			RNote("test: dump of frame %lld to '%ls' %s", (long long)fr.frameId, h.cfg.testDump.c_str(), ok ? "written" : "FAILED");
		}
	}
	else if(st == SinkStatus::Busy)
		h.busy++;
	else
		h.sinkErrors++;
	return true;
}

// ticket 0435: the calling thread's user + kernel CPU time in 100 ns units (0 when it cannot be read)
uint64_t ThreadCpu100ns()
{
	FILETIME c, e, k, u;
	if(!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
		return 0;
	return ((uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime);
}

// The render thread's 1 s lines (called on the render thread only: rt_cpu_ms reads the calling thread)
void SecondLine(Host& h)
{
	char line[4096];
	h.scene.secondLine(line, sizeof(line));
	RLog("%s", line);
	// ticket 0435: the frame submission's mean and largest CPU wall time over the frames of this second, and the render
	// thread's CPU time since the last line
	const uint64_t cpu = ThreadCpu100ns();
	const double rtCpuMs = cpu >= h.rtCpu100nsLast ? (double)(cpu - h.rtCpu100nsLast) / 10000.0 : -1.0;
	h.rtCpu100nsLast = cpu;
	const double submitMs = h.submitNSec ? QpcToMs(h.submitQpcSec) / (double)h.submitNSec : -1.0;
	const double submitMaxMs = h.submitNSec ? QpcToMs(h.submitMaxQpcSec) : -1.0;
	const long long submitN = (long long)h.submitNSec;
	h.submitQpcSec = h.submitMaxQpcSec = h.submitNSec = 0;
	RLog("host 1s: sink=%s state=%s draws=%lld published=%lld busy=%lld busy_retried=%lld notready=%lld sink_err=%lld draw_err=%lld "
	     "lost=%lld skipped_cams=%lld waits_event=%lld waits_legacy=%lld waits_idle=%lld wait_timeouts=%lld idle_calls=%lld "
	     "bake_no_sleep=%d idle_no_sleep=%lld bake_checks=%lld "
	     "dev_flags=%u submit_n=%lld submit_ms=%.3f submit_max_ms=%.3f rt_cpu_ms=%.1f "
	     "pace_fallback=%d pace_fallbacks=%lld pace_resumes=%lld waits_fallback=%lld "
	     "v3_overwritten=%lld v3_skipped=%lld acquire_max_ms=%.2f | feed drains=%lld events=%lld changed=%lld gen=%u",
	     h.sink ? h.sink->name() : "-", kStateNames[(uint32_t)h.state < 6 ? h.state : 0], (long long)h.draws, (long long)h.published,
	     (long long)h.busy, (long long)h.busyRetried, (long long)h.notReady, (long long)h.sinkErrors, (long long)h.drawErrors,
	     (long long)h.lost, (long long)h.camerasSkipped, (long long)h.waitsEvent, (long long)h.waitsLegacy, (long long)h.waitsIdle,
	     (long long)h.waitTimeouts, (long long)h.idleCalls, g_bakeNoSleep ? 1 : 0, (long long)h.idleNoSleep,
	     (long long)h.bakeChecks, (unsigned)h.deviceFlags, submitN, submitMs, submitMaxMs, rtCpuMs, h.paceFallback ? 1 : 0, (long long)h.paceFallbacks, (long long)h.paceResumes,
	     (long long)h.waitsFallback, h.v3 ? (long long)h.v3->overwritten() : 0LL, h.v3 ? (long long)h.v3->skipped() : 0LL, h.v3 ? h.v3->acquireMaxMsAndReset() : 0.0,
	     (long long)h.feedDrains, (long long)h.feedEvents, (long long)h.feedChanged, h.feedGen);
}

// issue 3312: Presents without a publish before event pacing gives up on the composite. A publishing composite writes the
// publish block once per frame before (UpdateViews) or at (Present) that frame's Present, so between two looks the
// presentCount runs ahead of the last seq change by at most 1-2; 3 leaves room for a read between the two writes.
constexpr int64_t kPaceStallPresents = 3;

bool PaceNoteDue(int64_t n)
{
	return n <= 10 || n % 1000 == 0;   // the first ten switches, then every thousandth (the per-second line counts them all)
}

// issue 3312: decides whether the event pacing is in force. Event pacing once the publish block's seq is non-zero, except
// while the camera block's presentCount has advanced by more than kPaceStallPresents since the seq last moved (the
// composite presents but publishes nothing, so <base>.Cam is never set); then the camera block is polled as before the
// first publish. The next seq change returns to the events.
bool EventPacing(Host& h)
{
	if(!h.frame)
		return false;
	VPShellHeader* hdr = h.scene.header();
	// presentCount first, then seq: a publish between the two reads can only make the Presents look fewer
	const int64_t pc = hdr ? InterlockedCompareExchange64(&VPSHELL_CAMERA(hdr)->presentCount, 0, 0) : -1;
	MemoryBarrier();
	const uint32_t seq = VPFRAME_PUBLISH(h.frame)->seq;
	if(seq == 0)
		return false;
	if(seq != h.paceSeq || h.pacePc < 0 || pc < h.pacePc)   // the composite published (or a new section / composite)
	{
		const bool moved = seq != h.paceSeq;
		h.paceSeq = seq;
		h.pacePc = pc;
		if(h.paceFallback && moved)
		{
			h.paceFallback = false;
			h.paceResumes++;
			if(PaceNoteDue(h.paceResumes))
				RNote("pacing: the composite publishes again (seq %u, presentCount %lld): woken by <base>.Cam (resume %lld)", seq,
				      (long long)pc, (long long)h.paceResumes);
		}
		return !h.paceFallback;
	}
	if(!h.paceFallback && pc >= 0 && pc - h.pacePc > kPaceStallPresents)
	{
		h.paceFallback = true;
		h.paceFallbacks++;
		if(PaceNoteDue(h.paceFallbacks))
			RNote("pacing: %lld Presents without a publish (presentCount %lld -> %lld, seq %u): the camera block is polled until the "
			      "composite publishes again (fallback %lld)",
			      (long long)(pc - h.pacePc), (long long)h.pacePc, (long long)pc, seq, (long long)h.paceFallbacks);
	}
	return !h.paceFallback;
}

// The legacy composite's camera poll: a high-resolution waitable timer of this period (100 ns units; 5000 = 0.5 ms, as
// shell_server). Issue 0333: the pacing note prints it from this constant, so a test reads the period the loop uses
// without timing the loop on a loaded machine.
constexpr LONGLONG kLegacyPollHns = 5000;

// ---- ticket 0713: the prepare phase (research/07-architecture-decisions/architect-2026-11-23-frame-overlap.md 6) ----------
// The composite sets <base>.Tap after the last tap of its Present hook, with that Present's number in the publish block's
// tapPresent (vp_frame.h). A pass woken by it (no camera pending) drains the feed and polls the taps (the `prepare` stage)
// and notes the number it read before them; the next camera goes straight to decide while tapPresent still equals it, and
// drains and polls first, as before 0713, when it does not (no prepare in time, or a newer tap). A frame about to be held
// for EXE inputs (0691) polls once more first. Off (every pass as before) with render_prepare=0, with a recorder (its
// iteration is drain + draw), or without the event.
enum
{
	kWokeOther = 0,
	kWokeCamera = 1,
	kWokeTap = 2
};

bool PrepareOn(const Host& h)
{
	return h.cfg.prepare && h.tapEv && !h.rec && h.frame;
}

int64_t TapPresent(Host& h)
{
	return h.frame ? InterlockedCompareExchange64(&VPFRAME_PUBLISH(h.frame)->tapPresent, 0, 0) : 0;
}

// A tap written since the newest drain and polls (the composite before 0713 never writes one: 0).
bool TapPending(Host& h)
{
	if(!PrepareOn(h) || !h.prepEvent)
		return false;
	const int64_t t = TapPresent(h);
	return t > 0 && t != h.prepTap;
}

// The event feed: the renderer's only game input (counted; the tap source counts changed plots). Epic 0301, issue 3904:
// through h.feed (LiveFeed() = SrvFeed::Drain; the recording feed with render_test_record)
void DrainFeed(Host& h, SrvFeed::Batch& batch)
{
	if(h.feed->drain(batch))
	{
		h.feedDrains++;
		h.feedEvents += (int64_t)batch.events.size();
		h.feedChanged += (int64_t)batch.changedPlots.size();
		h.feedGen = batch.snapshotGen;
		if(!batch.changedPlots.empty())
			h.scene.plotsChanged(batch.changedPlots.data(), batch.changedPlots.size());
		h.scene.fowFeed(batch);   // issue 3954: the fog's plot states, sequence and overflow checks
		// epic 0375, issue 4454: the unit tables, the allocator, the fades (nothing with render_units=0); issue 4456: at the
		// drain's clock, the recorder's own value when recording (feed.bin's drainMs), else the scene reads the clock
		h.scene.unitFeed(batch, h.rec ? h.rec->lastDrainMs() : -1.0);
		h.scene.forestFeed(batch);   // epic 0431, ticket 4554: the forest and jungle records
		h.scene.farmFeed(batch);     // epic 0528, ticket 4852: the farm plots (nothing with render_farmfields=0)
		h.scene.arcFeed(batch);      // ticket 0199 part 2: the arc's spawn and remove records (nothing with render_arc=0)
		h.scene.pathFeed(batch);     // ticket 0300 part 2: the path's nodes and show / hide (nothing with render_path=0)
		h.scene.modelsFeed(batch);   // ticket 4875: a load clears the model bits (nothing with both model keys 0)
	}
}

// The scene's taps and the fatal steps (the polls).
void PollScene(Host& h)
{
	h.scene.poll();
	// issue 3704, MC #701 (a) / #713, decision 5: a tap that could not install ends the server once (HostConfig::fatal)
	OverlayFatalStep(h.scene, h.cfg.fatal, h.overlayFatalDone);
	// issue 3954, decision 4: a fog that could not install ends the server once (the same hook, code 5)
	FowFatalStep(h.scene, h.cfg.fatal, h.fowFatalDone);
	// issue 4105, decision 6: a decal input that cannot install ends the server once (HostConfig::fatal, D125)
	DecalFatalStep(h.scene, h.cfg.fatal, h.decalFatalDone);
	// issue 4308: the shadow or the ice that cannot install ends the server once (HostConfig::fatal, D125)
	ShadowFatalStep(h.scene, h.cfg.fatal, h.shadowFatalDone);
	IceFatalStep(h.scene, h.cfg.fatal, h.iceFatalDone);
	// epic 0375, issue 4454: a unit pre-check that failed ends the server once (HostConfig::fatal, D125)
	UnitsFatalStep(h.scene, h.cfg.fatal, h.unitsFatalDone);
	// epic 0561, ticket 4875: the model module's failure (D118 per bit) ends the server once (HostConfig::fatal, D125)
	ModelsFatalStep(h.scene, h.cfg.fatal, h.modelsFatalDone);
	// epic 0431, ticket 4554: the trees that cannot install end the server once (HostConfig::fatal, D125)
	ForestFatalStep(h.scene.forest(), h.cfg.fatal, h.forestFatalDone);
}

// The drain and the polls, with the tap number read before them (stage: `prepare` in a tap pass, else feed and polls).
void TakeInputs(Host& h, SrvFeed::Batch& batch, bool prepare)
{
	if(PrepareOn(h))
		h.prepTap = TapPresent(h);
	PassTo(h, prepare ? vppacing::kStPrepare : vppacing::kStFeed);   // ticket 0688
	DrainFeed(h, batch);
	PassTo(h, prepare ? vppacing::kStPrepare : vppacing::kStPolls);   // ticket 0688
	PollScene(h);
}

void PrepareSecondLine(Host& h)
{
	if(!h.cfg.prepare)
		return;
	RLog("prepare 1s: on=%d tap_event=%d tap_present=%lld prepared=%lld prepare_passes=%lld prepare_nothing=%lld cam_hits=%lld "
	     "cam_polls=%lld hold_repolls=%lld tap_yields=%lld",
	     PrepareOn(h) ? 1 : 0, h.tapEv ? 1 : 0, (long long)TapPresent(h), (long long)h.prepTap, (long long)h.prepPasses,
	     (long long)h.prepNothing, (long long)h.camHits, (long long)h.camPolls, (long long)h.holdRepolls, (long long)h.tapYields);
}

void Loop(Host& h)
{
	SrvFeed::Batch batch;
	// ticket 0713: the prepare phase's event (created or opened; vp_frame.h <base>.Tap)
	if(h.cfg.prepare && !h.rec)
	{
		h.tapEv = vpframe::ring::OpenEvent_(h.cfg.section.c_str(), VPFRAME_TAP_EVENT_SUFFIX);
		if(h.tapEv)
			RNote("prepare: the feed's drain and the taps' polls run on <base>.Tap, before the camera; a camera whose tap number "
			      "was prepared goes straight to decide (render_prepare=1, ticket 0713)");
		else
			RNote("LOUD: prepare: <base>.Tap not created or opened (error %lu): the drain and the polls run at the camera (ticket 0713)",
			      GetLastError());
	}
	else
		RNote("prepare: off (%s): the drain and the polls run at the camera (ticket 0713)",
		      !h.cfg.prepare ? "render_prepare=0" : "a recorder runs");
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if(!timer)
		RNote("no high-resolution waitable timer (error %lu): legacy pacing sleeps 1 ms", GetLastError());
	int64_t secStart = Qpc(), lastMem = secStart;
	h.rtCpu100nsLast = ThreadCpu100ns();   // ticket 0435: the first 1 s line counts from the loop's start
	bool legacyNoted = false, eventNoted = false, idleNoted = false;
	HoldReport(h, 0, 0, true);   // ticket 0691: no hold of an earlier server in the section
	while(!StopRequested(h))
	{
		const int64_t now = Qpc();
		PassRoll(h, now);   // ticket 0688: the pass before counted, this one begins
		PassTo(h, vppacing::kStPolls);
		h.sink->tick(now);
		if(h.sink->takeResized())
			h.scene.markDirty();
		// the composite moved to another adapter: shared textures cannot cross adapters, so follow it (not a lost device)
		if(!h.cfg.luid && h.scene.header())
		{
			const uint64_t cl = h.scene.header()->clientLuid;
			if(cl && cl != h.luid && cl != h.followedLuid)
			{
				h.followedLuid = cl;   // once per LUID: an adapter we cannot open must not loop
				RNote("the composite's adapter LUID %016llX differs from ours %016llX: device re-created on its adapter", (unsigned long long)cl,
				      (unsigned long long)h.luid);
				ReleaseDevice(h);
				std::string why;
				const uint32_t r = CreateDevice(h, why);
				if(r != VPFSF_NONE)
				{
					Fail(h, r, why);
					break;
				}
				SetState(h, VPFS_READY);
				continue;
			}
		}
		// a new composite numbers its frames from 1 (3302's integration note); its Presents too (ticket 0713: nothing prepared)
		if(h.frame)
		{
			const uint32_t pid = VPFRAME_PUBLISH(h.frame)->exePid;
			if(pid != h.lastExePid)
			{
				if(h.lastExePid)
					RNote("composite pid %u -> %u: frame ids restart", h.lastExePid, pid);
				h.lastExePid = pid;
				h.lastDrawnFrameId = 0;
				h.scene.resetFrameIds();
				HoldReport(h, 0, 0);   // ticket 0691
				h.prepTap = -1;        // ticket 0713
			}
		}
		// ticket 0713: what this pass is. A tap pass (woken by <base>.Tap, no camera waiting) prepares and draws nothing; a
		// camera pass whose tap number was prepared skips the drain and the polls; every other pass takes them as before.
		const int woke = h.wokeBy;
		h.wokeBy = kWokeOther;
		const bool prepOn = PrepareOn(h);
		const bool tapPass = prepOn && woke == kWokeTap && !h.scene.cameraPending();
		const bool camHit = prepOn && !tapPass && woke == kWokeCamera && h.prepTap > 0 && TapPresent(h) == h.prepTap;
		if(h.rec)
			h.rec->beginIteration();   // epic 0301, issue 3904 (prepare is off with a recorder)
		if(tapPass)
		{
			if(TapPresent(h) != h.prepTap)
			{
				TakeInputs(h, batch, true);
				h.prepPasses++;
			}
			else
				h.prepNothing++;
		}
		else if(camHit)
			h.camHits++;
		else
		{
			TakeInputs(h, batch, false);
			if(prepOn && woke == kWokeCamera)
				h.camPolls++;
		}
		SceneFrame fr;
		PassTo(h, vppacing::kStDecide);   // ticket 0688 (DrawFrame marks its slot, draw and hand-over)
		bool drawIt = !tapPass && h.scene.decide(fr);
		// ticket 0713: a frame about to be held for EXE inputs (0691) when this pass took no inputs: the inputs once more, then decide
		if(!tapPass && !drawIt && camHit && h.scene.holdFrameId())
		{
			h.holdRepolls++;
			TakeInputs(h, batch, false);
			PassTo(h, vppacing::kStDecide);
			drawIt = h.scene.decide(fr);
		}
		if(drawIt)
		{
			HoldReport(h, 0, 0);   // ticket 0691: drawn, no hold
			// ticket 0711 (R9): the frame's clock stamped at the pass's start in live runs too, so that the units, the water and
			// the fog share one clock per frame and the units' job gets it by value (a recording host keeps the recorder's
			// value, set in DrawFrame, and forks there through Scene::draw)
			if(fr.nowMs < 0)
				fr.nowMs = QpcToMs(now);
			// ticket 0711 (J1): the units' begin forked once the camera is fixed; joined in the draw before the units' first
			// use, and below on every path DrawFrame takes (a busy slot, a lost device, an error: R3)
			if(!h.rec)
				h.scene.forkFrameJobs(fr);
			const bool drawn = DrawFrame(h, fr, now);
			h.scene.joinFrameJobs();
			DrainD3dDebug(h);   // ticket 0753 (TEST ONLY: nothing without render_test_d3d_debug=1)
			if(!drawn)
			{
				if(!Rebuild(h, "the sink or the draw reported it"))
					break;
				continue;
			}
		}
		else if(!tapPass)   // ticket 0691: a frame the scene holds for EXE inputs: the field's (cand4) or the fog's (0 / 0 when nothing is held)
			HoldReport(h, h.scene.holdFrameId(), h.scene.holdReasons());
		// issue 3308: the tile source's background work after the publish (its GPU work never delays a published frame);
		// none while a newer camera already waits
		bool idleBaked = false;   // ticket 0414: this pass's slice made at least one bake (CameraWaiting counted a check)
		PassTo(h, vppacing::kStIdle);   // ticket 0688
		if(const double b = h.scene.idleBudgetMs())
		{
			const int64_t checks0 = h.bakeChecks;
			h.scene.idle(h.ctx, h.scene.cameraPending() || TapPending(h) ? 0.0 : b);   // ticket 0713: the tap's prepare first
			h.idleCalls++;
			idleBaked = h.bakeChecks != checks0;
		}
		PassTo(h, vppacing::kStRecorder);   // ticket 0688
		if(h.rec)
			h.rec->step(RecCounters(h));   // epic 0301, issue 3904: the bundle finished once its span and overlays are in
		PassTo(h, vppacing::kStLog);   // ticket 0688
		const int64_t t1 = Qpc();
		if(QpcToMs(t1 - secStart) >= 1000.0)
		{
			SecondLine(h);
			PassSecondLine(h);   // ticket 0688: "pass 1s:"
			PrepareSecondLine(h);   // ticket 0713: "prepare 1s:"
			secStart = t1;
		}
		if(QpcToMs(t1 - lastMem) >= 10000.0)
		{
			LogMemory(h, "periodic", false);
			VidmemLine(h);   // ticket 0688: "vidmem 10s:" (the budget)
			lastMem = t1;
		}
		PassTo(h, vppacing::kStOther);   // ticket 0688
		// pacing: <base>.Cam once the composite publishes (vp_frame.h), else the 0.5 ms poll of shell_server; issue 3312: also
		// the poll while the composite presents without publishing (its <base>.Cam never comes)
		const bool eventMode = EventPacing(h);
		h.prepEvent = eventMode;   // ticket 0713
		if(VPFrameServer* s = FrameServer(h))   // issue 3306: the pacing in force, for the composite and the tests
			s->pacing = eventMode ? VPPACE_EVENT : VPPACE_LEGACY;
		// issue 3308: background bakes waiting: wake again soon (each idle() call is budgeted)
		const bool busySource = h.scene.sourcePending();
		// ticket 0414: and no wait at all after a slice that baked: the 2 ms wait lasted 1-3 ms on the 1 ms timer and was about
		// 24 % of each bake-ahead pass (research/08-performance/bake-ahead-after-0365.md 5). The pass still looks at <base>.Cam
		// and the stop event (a wait of 0 ms, so a set <base>.Cam is taken as before) and still yields to a camera: the
		// slice ends before its next bake once one waits, and the next pass draws it. A slice that did not bake (no token,
		// a yield at its first check, nothing left) waits as before, so the token bucket, not the loop, sets the pace.
		const bool noSleep = g_bakeNoSleep && busySource && idleBaked;
		if(noSleep)
		{
			h.idleNoSleep++;
			if(h.idleNoSleep == 1)
				RNote("pacing: bakes pending: no pacing wait after a pass whose slice baked (render_bake_no_sleep=1, ticket 0414)");
		}
		// issue 3308: before the first publish the 0.5 ms camera poll runs only while there is a camera to draw (an old
		// composite that writes camera blocks without the event); otherwise slow waits, ended at once by <base>.Cam
		const bool idlePace = !eventMode && h.cfg.idlePollMs > 0 && h.scene.state() != 1 /* TS_OK */;
		PassTo(h, vppacing::kStWait);   // ticket 0688: not part of the pass's busy time
		if(eventMode || idlePace)
		{
			if(eventMode && !eventNoted)
				RNote("pacing: the composite publishes: woken by <base>.Cam");
			eventNoted = eventNoted || eventMode;
			if(idlePace && !idleNoted && !h.paceFallback)
			{
				RNote("pacing: no camera to draw and no publish yet: waking every %.1f ms or on <base>.Cam", h.cfg.idlePollMs);
				idleNoted = true;
				legacyNoted = false;
			}
			HANDLE cam = (HANDLE)h.sink->camEvent();
			// ticket 0713: and <base>.Tap (after the camera: when both are set the camera's pass comes first and polls itself)
			HANDLE tap = eventMode && PrepareOn(h) ? h.tapEv : nullptr;
			HANDLE hs[3] = {h.stop, cam, tap};
			DWORD n = cam ? 2 : 1;
			if(cam && tap)
				n = 3;
			const DWORD ms = noSleep ? 0 : busySource ? 2 : eventMode ? 100 : (DWORD)(h.cfg.idlePollMs + 0.5);
			const DWORD w = WaitForMultipleObjects(n, hs, FALSE, ms || noSleep ? ms : 1);
			PassWoken(h, cam && w == WAIT_OBJECT_0 + 1);   // ticket 0688: the wake delay
			h.wokeBy = cam && w == WAIT_OBJECT_0 + 1 ? kWokeCamera : n == 3 && w == WAIT_OBJECT_0 + 2 ? kWokeTap : kWokeOther;
			if(eventMode)
			{
				if(w == WAIT_TIMEOUT && !busySource)
					h.waitTimeouts++;
				h.waitsEvent++;
			}
			else
			{
				h.waitsIdle++;
				if(h.paceFallback)
					h.waitsFallback++;
			}
		}
		else
		{
			if(!legacyNoted && !h.paceFallback)
			{
				RNote("pacing: legacy (the composite has never written the publish block): camera block polled every %.1f ms",
				      (double)kLegacyPollHns / 10000.0);
				legacyNoted = true;
				idleNoted = false;
			}
			h.waitsLegacy++;
			if(h.paceFallback)
				h.waitsFallback++;
			if(noSleep)
			{
				// ticket 0414: no poll wait; the next pass reads the camera block (and the loop's condition the stop)
			}
			else if(timer)
			{
				LARGE_INTEGER due;
				due.QuadPart = -kLegacyPollHns;   // relative
				SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
				HANDLE hs[2] = {h.stop, timer};
				WaitForMultipleObjects(2, hs, FALSE, 5);
			}
			else
				WaitForSingleObject(h.stop, 1);
		}
	}
	vppacing::CurrentPass() = nullptr;   // ticket 0688
	if(timer)
		CloseHandle(timer);
	if(h.tapEv)   // ticket 0713
	{
		CloseHandle(h.tapEv);
		h.tapEv = nullptr;
	}
}

void RunCpp(Host& h)
{
	try
	{
		StartRecorder(h);   // epic 0301, issue 3904 (render_test_record; nothing without it)
		std::string why;
		// ticket 0600: render_test_init_fail (TEST ONLY) forces one reason at the first device, as the live failures give it
		const uint32_t forced = h.cfg.testInitFail;
		if(forced == kTestInitFailException)
			throw std::runtime_error("render_test_init_fail=exception (TEST ONLY)");
		const uint32_t r = forced ? forced : CreateDevice(h, why);
		if(forced)
			why = std::string("render_test_init_fail=") + kFailNames[forced < 7 ? forced : 0] + " (TEST ONLY)";
		if(r != VPFSF_NONE)
		{
			Fail(h, r, why);
			return;
		}
		h.initDone = true;   // ticket 0600: from here a failure is "in play" (the game goes on, as before)
		SetState(h, VPFS_READY);
		LogMemory(h, "start", true);
		Loop(h);
	}
	catch(const std::exception& e)
	{
		h.exceptions++;
		_snprintf_s(h.exceptionText, sizeof(h.exceptionText), _TRUNCATE, "C++ exception: %s", e.what());
		Fail(h, VPFSF_EXCEPTION, h.exceptionText);
	}
	catch(...)
	{
		h.exceptions++;
		_snprintf_s(h.exceptionText, sizeof(h.exceptionText), _TRUNCATE, "C++ exception of unknown type");
		Fail(h, VPFSF_EXCEPTION, h.exceptionText);
	}
}

int SehFilter(Host& h, EXCEPTION_POINTERS* p)
{
	const EXCEPTION_RECORD* r = p ? p->ExceptionRecord : nullptr;
	HMODULE mod = nullptr;
	char modName[MAX_PATH] = "?";
	if(r && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                           (LPCSTR)r->ExceptionAddress, &mod) && mod)
		GetModuleFileNameA(mod, modName, MAX_PATH);
	_snprintf_s(h.exceptionText, sizeof(h.exceptionText), _TRUNCATE, "SEH exception 0x%08lX at %p (%s+0x%llX)%s",
	            r ? r->ExceptionCode : 0, r ? r->ExceptionAddress : nullptr, modName,
	            (unsigned long long)(r && mod ? (uintptr_t)r->ExceptionAddress - (uintptr_t)mod : 0),
	            r && r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 ? " (access violation)" : "");
	return EXCEPTION_EXECUTE_HANDLER;
}

void OnSehCaught(Host& h)
{
	h.exceptions++;
	Fail(h, VPFSF_EXCEPTION, h.exceptionText);
}

// No C++ object with a destructor in these two functions (SEH frames; MSVC C2712 otherwise).
void RunSeh(Host& h)
{
	__try
	{
		RunCpp(h);
	}
	__except(SehFilter(h, GetExceptionInformation()))
	{
		OnSehCaught(h);
	}
}

void CleanupSeh(Host& h)
{
	__try
	{
		ReleaseDevice(h);
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		RNote("LOUD: an exception while releasing the device after a failure: objects leaked");
	}
}

DWORD WINAPI ThreadProc(void* pv)
{
	Host& h = *(Host*)pv;
	typedef HRESULT(WINAPI * SetDesc)(HANDLE, PCWSTR);
	if(SetDesc f = (SetDesc)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))
		f(GetCurrentThread(), L"vp-render");
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	char power[320];
	SrvPowerOptOut(power, sizeof(power));   // bug 0118: process (again) and this thread
	RNote("render thread %lu: %s", GetCurrentThreadId(), power);
	RunSeh(h);
	if(h.state == VPFS_FAILED)
	{
		CleanupSeh(h);
		FinishRecorder(h, "the renderer FAILED");   // epic 0301, issue 3904
		SetState(h, VPFS_FAILED, (uint32_t)h.failReason);   // the sinks' release() wrote STOPPED / cleared magic
		FailFatalStep(h);   // ticket 0600: FAILED ends the server (D125, D213); server.exe does not come back
		WaitForSingleObject(h.stop, INFINITE);   // a test host's hook returned: no heartbeat; wait for the orderly stop
	}
	else
	{
		ReleaseDevice(h);
		FinishRecorder(h, "the render thread ends");   // epic 0301, issue 3904
	}
	char tot[2048];
	h.scene.totalsLine(tot, sizeof(tot));
	RNote("%s", tot);
	if(g_testD3dDebug)   // ticket 0753 (TEST ONLY): the debug layer's messages of the whole run (the suite's check reads it)
		RNote("d3d debug totals: corruption %lld error %lld warning %lld", (long long)h.d3dDebugMessages[0],
		      (long long)h.d3dDebugMessages[1], (long long)h.d3dDebugMessages[2]);
	LogMemory(h, "end", true);
	SafeRelease(h.adapter);
	h.scene.detach();
	return 0;
}

}  // namespace

// =====================================================================================================================

// issue 3954 (srv_fow_scene.h): what ParseHostConfig read last
int RenderFowOption() { return g_fow; }
int RenderFowLevelOption() { return g_fowLevel; }
// epic 0040, issue 4753 (srv_render_scene.h)
double RenderUnitsReadyHoldMsOption() { return g_unitsReadyHoldMs; }
int RenderUnitsPositionsOption() { return g_unitsPositions; }
int RenderUnitsShadowOption() { return g_unitsShadow; }   // ticket 4785

bool ParseHostConfig(const std::function<std::string(const char*)>& get, const std::string& gameDir, const std::string& runDir,
                     HostConfig& out, std::string& why)
{
	HostConfig c;
	auto bad = [&](const char* key, const std::string& v, const char* want) {
		why = std::string(key) + "=" + v + ": " + want;
		return false;
	};
	std::string v;
	long long n = 0;
	double d = 0;
	if(!(v = get("render_section")).empty())
	{
		if(v.size() > 180)
			return bad("render_section", v, "at most 180 characters");
		c.section = Wide(v);
	}
	if(!(v = get("render_texture")).empty())
	{
		if(v.size() > 120)
			return bad("render_texture", v, "at most 120 characters");
		c.textureBase = Wide(v);
	}
	v = get("render_exe");
	c.exe = Wide(v.empty() ? gameDir + "\\CivilizationV_DX11.exe" : v);
	v = get("render_log");
	c.logPath = Wide(v.empty() ? runDir + "\\render.log" : v);
	if(!(v = get("render_handover")).empty())
	{
		if(v != "v3" && v != "ring")
			return bad("render_handover", v, "v3 or ring");
		c.handover = v;
	}
	if(!(v = get("render_ring_slots")).empty())
	{
		if(!ParseInt(v, VPFRAME_MIN_SLOTS, VPFRAME_MAX_SLOTS, n))
			return bad("render_ring_slots", v, "2..4");
		c.ringSlots = (uint32_t)n;
	}
	if(!(v = get("render_sync")).empty())
	{
		if(v == "fence")
			c.ringSync = VPSYNC_FENCE;
		else if(v == "cpu")
			c.ringSync = VPSYNC_CPU;
		else
			return bad("render_sync", v, "fence or cpu");
	}
	if(!(v = get("render_luid")).empty())
	{
		char* end = nullptr;
		const unsigned long long l = _strtoui64(v.c_str(), &end, 16);
		if(!end || *end)
			return bad("render_luid", v, "a hex LUID (0 = the composite's)");
		c.luid = l;
	}
	auto flag = [&](const char* key, bool& dst) {
		const std::string s = get(key);
		if(s.empty())
			return true;
		if(s != "0" && s != "1")
			return bad(key, s, "0 or 1");
		dst = s == "1";
		return true;
	};
	if(!flag("render_depth", c.depth) || !flag("render_gpu_sync", c.gpuSync))
		return false;
	g_bakeYield = true;   // issue 0140
	g_bakeStaleBeforeDraw = false;
	if(!flag("render_bake_yield", g_bakeYield) || !flag("render_bake_stale_before_draw", g_bakeStaleBeforeDraw))
		return false;
	g_bakeNoSleep = true;   // ticket 0414
	if(!flag("render_bake_no_sleep", g_bakeNoSleep))
		return false;
	g_preventDriverThreading = false;   // ticket 0435 (the default: g_preventDriverThreading's comment)
	if(!flag("render_prevent_driver_threading", g_preventDriverThreading))
		return false;
	g_testD3dDebug = false;            // ticket 0753 (TEST ONLY)
	g_testRecordAheadAlways = false;
	g_testRecordAheadEndOnly = false;
	if(!flag("render_test_d3d_debug", g_testD3dDebug) || !flag("render_test_record_ahead_always", g_testRecordAheadAlways) ||
	   !flag("render_test_record_ahead_end_only", g_testRecordAheadEndOnly))
		return false;
	// issue 3607: render_water = 1 (default: the water module, the flat placeholder as the fallback when the tap or the
	// module is missing) | placeholder (the flat plane only) | 0 (no water); render_water_freeze = 0 | 1 (diagnostics: the
	// water clock held at the tapped values)
	g_waterMode = WaterSceneOptions::kFull;
	g_waterFreeze = false;
	if(!(v = get("render_water")).empty())
	{
		if(v == "1")
			c.water = true;
		else if(v == "0")
			c.water = false;
		else if(v == "placeholder")
		{
			c.water = true;
			g_waterMode = WaterSceneOptions::kPlaceholder;
		}
		else
			return bad("render_water", v, "0, 1 or placeholder");
	}
	if(!flag("render_water_freeze", g_waterFreeze))
		return false;
	// issue 0382: render_ice_reflection = 1 (default: the polar ice drawn into the water's reflection target, as stock) | 0
	// (the reflection stays (0,0,0,0): the picture before 0382; diagnostics)
	{
		bool reflIce = true;
		if(!flag("render_ice_reflection", reflIce))
			return false;
		WaterSceneSetReflectIce(reflIce);
	}
	// issue 3704: render_overlay = 1 (default: the EXE's Terrain Overlay target through the tap as terrain t1 / water t9; a
	// tap that cannot install ends the game, MC #701) | 0 (no overlay: t1 / t9 transparent, the picture before 3704)
	if(!(v = get("render_overlay")).empty())
	{
		if(v == "1" || v == "0")
			c.overlay = v == "1" ? 1 : 0;
		else
			return bad("render_overlay", v, "0 or 1");
	}
	// issue 3954: render_fow = 1 (default: the fog of war; one that cannot install ends the game, decision 4; it needs
	// render_overlay=1, decision 5) | 0 (no fog: the picture before 3954); render_fow_level = -1 (default: FOWLevel of
	// the player's GraphicsSettingsDX11.ini) | 0..3 (diagnostics)
	g_fow = 1;
	g_fowLevel = -1;
	if(!(v = get("render_fow")).empty())
	{
		if(v == "1" || v == "0")
			g_fow = v == "1" ? 1 : 0;
		else
			return bad("render_fow", v, "0 or 1");
	}
	if(!(v = get("render_fow_level")).empty())
	{
		if(!ParseInt(v, -1, 3, n))
			return bad("render_fow_level", v, "-1 (the player's GraphicsSettingsDX11.ini) or 0..3");
		g_fowLevel = (int)n;
	}
	if(!ParseRenderFowMaskWindow(get("render_fow_mask_window"), why))   // ticket 0674 (srv_render_host.h)
		return false;
	if(!ParseRenderFowOverlayHistory(get("render_fow_overlay_history"), why))   // ticket 0793 (srv_render_scene.h)
		return false;
	// issue 0371: render_bake_near (the block comment at g_bakeNear)
	g_bakeNear = 1;
	if(!(v = get("render_bake_near")).empty())
	{
		if(v == "1" || v == "0")
			g_bakeNear = v == "1" ? 1 : 0;
		else
			return bad("render_bake_near", v, "0 or 1");
	}
	// issue 0417: render_bake_warm (the block comment at g_bakeWarm)
	g_bakeWarm = 0;
	if(!(v = get("render_bake_warm")).empty())
	{
		if(v == "1" || v == "0")
			g_bakeWarm = v == "1" ? 1 : 0;
		else
			return bad("render_bake_warm", v, "0 or 1");
	}
	// issue 4105: render_decals, render_test_decal_wait_ms, render_test_decal_trace (the block comment at g_decals)
	g_decals = 1;
	g_decalWaitMs = 20000;
	g_decalTrace = 0;
	if(!(v = get("render_decals")).empty())
	{
		if(v == "1" || v == "0")
			g_decals = v == "1" ? 1 : 0;
		else
			return bad("render_decals", v, "0 or 1");
	}
	if(!(v = get("render_test_decal_wait_ms")).empty())
	{
		if(!ParseInt(v, 100, 600000, n))
			return bad("render_test_decal_wait_ms", v, "100..600000");
		g_decalWaitMs = (double)n;
	}
	if(!(v = get("render_test_decal_trace")).empty())
	{
		if(!ParseInt(v, 0, 100000, n))
			return bad("render_test_decal_trace", v, "0..100000");
		g_decalTrace = (int)n;
	}
	// issue 4308: render_shadow, render_ice, render_test_shadow_wait_ms, render_test_ice_wait_ms (the block comment at g_shadow)
	g_shadow = 1;
	g_ice = 1;
	g_shadowWaitMs = 20000;
	g_iceWaitMs = 0;
	if(!(v = get("render_shadow")).empty())
	{
		if(v == "1" || v == "0")
			g_shadow = v == "1" ? 1 : 0;
		else
			return bad("render_shadow", v, "0 or 1");
	}
	if(!(v = get("render_ice")).empty())
	{
		if(v == "1" || v == "0")
			g_ice = v == "1" ? 1 : 0;
		else
			return bad("render_ice", v, "0 or 1");
	}
	// ticket 0199 part 2: render_arc (the block comment at g_arc)
	g_arc = 0;
	if(!(v = get("render_arc")).empty())
	{
		if(v == "1" || v == "0")
			g_arc = v == "1" ? 1 : 0;
		else
			return bad("render_arc", v, "0 or 1");
	}
	// ticket 0300 part 2: render_path (the block comment at g_path)
	g_path = 0;
	if(!(v = get("render_path")).empty())
	{
		if(v == "1" || v == "0")
			g_path = v == "1" ? 1 : 0;
		else
			return bad("render_path", v, "0 or 1");
	}
	// epic 0561, ticket 4875: the model module's keys (the block comment at g_models)
	g_models = srvmodels::ModelsOptions();
	if(!(v = get("render_cities")).empty())
	{
		if(v != "1" && v != "0")
			return bad("render_cities", v, "0 or 1");
		g_models.cities = v == "1";
	}
	if(!(v = get("render_landmarks")).empty())
	{
		if(v != "1" && v != "0")
			return bad("render_landmarks", v, "0 or 1");
		g_models.landmarks = v == "1";
	}
	if(!(v = get("render_models_source")).empty())
	{
		if(v == "exe")
			g_models.source = srvmodels::kModelsSourceExe;
		else if(v == "own")
			g_models.source = srvmodels::kModelsSourceOwn;
		else
			return bad("render_models_source", v, "exe or own");
	}
	if(!(v = get("render_models_no_frame_ms")).empty())
	{
		if(!ParseInt(v, 16, 600000, n))
			return bad("render_models_no_frame_ms", v, "16..600000");
		g_models.noFrameMs = (double)n;
	}
	if(!(v = get("render_models_hold_ms")).empty())
	{
		if(!ParseInt(v, 1, 600000, n))
			return bad("render_models_hold_ms", v, "1..600000");
		g_models.holdMs = (double)n;
	}
	if(!(v = get("render_models_install_ms")).empty())
	{
		if(!ParseInt(v, 0, 3600000, n))
			return bad("render_models_install_ms", v, "0..3600000 (0: no bound)");
		g_models.installMs = (double)n;
	}
	// ticket 4885: the model layer's open: no progress this long, and its ceiling (srv_models_mode.h)
	if(!(v = get("render_models_open_stall_ms")).empty())
	{
		if(!ParseInt(v, 0, 3600000, n))
			return bad("render_models_open_stall_ms", v, "0..3600000 (0: no bound)");
		g_models.openStallMs = (double)n;
	}
	if(!(v = get("render_models_open_max_ms")).empty())
	{
		if(!ParseInt(v, 0, 3600000, n))
			return bad("render_models_open_max_ms", v, "0..3600000 (0: no bound)");
		g_models.openMaxMs = (double)n;
	}
	if(!(v = get("render_models_pair_wait_ms")).empty())   // R1: the wait for the tap of the frame's own Present
	{
		if(!ParseInt(v, 0, 100, n))
			return bad("render_models_pair_wait_ms", v, "0..100 (0: no wait)");
		g_models.pairWaitMs = (double)n;
	}
	if(!(v = get("render_test_models_draw_all")).empty())
	{
		if(v != "1" && v != "0")
			return bad("render_test_models_draw_all", v, "0 or 1");
		g_models.drawAll = v == "1";
	}
	// ticket 0789 (D269, D255): the model store, VP64Options.ini [Renderer] ModelStore (default 1); render_models_store
	// (0 | 1) overrides it (a test switch); render_models_store_refute (1..16, R3's complete taps) and
	// render_models_store_max_entries (1024..4194304 per pass) are test keys
	g_models.store = SrvOptions().ModelStore() != 0;
	if(!(v = get("render_models_store")).empty())
	{
		if(v != "1" && v != "0")
			return bad("render_models_store", v, "0 or 1");
		g_models.store = v == "1";
	}
	if(!(v = get("render_models_store_refute")).empty())
	{
		if(!ParseInt(v, 1, 16, n))
			return bad("render_models_store_refute", v, "1..16");
		g_models.storeRefuteTaps = (int)n;
	}
	if(!(v = get("render_models_store_max_entries")).empty())
	{
		if(!ParseInt(v, 1024, 4194304, n))
			return bad("render_models_store_max_entries", v, "1024..4194304");
		g_models.storeMaxEntries = (size_t)n;
	}
	if(g_models.any() && !srvmodels::MakeModelsScene())
		return bad(g_models.cities ? "render_cities" : "render_landmarks", "1",
		           "this program does not link the model module (server/src/render/landmarks/)");
	// epic 0528, ticket 4852: render_farmfields and its keys (the block comment at g_farmfields)
	g_farmfields = 0;
	g_farmKeepTap = 0;
	g_farmIdsWaitMs = 20000;
	if(!(v = get("render_farmfields")).empty())
	{
		if(v == "1" || v == "0")
			g_farmfields = v == "1" ? 1 : 0;
		else
			return bad("render_farmfields", v, "0 or 1");
	}
	if(!(v = get("render_farmfields_keep_tap")).empty())
	{
		if(v == "1" || v == "0")
			g_farmKeepTap = v == "1" ? 1 : 0;
		else
			return bad("render_farmfields_keep_tap", v, "0 or 1");
	}
	if(!(v = get("render_farmfields_ids_wait_ms")).empty())
	{
		if(!ParseInt(v, 0, 600000, n))
			return bad("render_farmfields_ids_wait_ms", v, "0..600000");
		g_farmIdsWaitMs = (double)n;
	}
	// ticket 4934, 4940: render_roadlayout and its shape (the block comment at g_roadLayout)
	{
		const Vp64Opt::ExtraState rl = Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadLayout);
		g_roadLayout = rl.on ? rl.value : 0;   // value 2 with EnableVisualAdditions = 0 is off: 0, never 1
		g_roadJunction = g_roadLayout == 2 ? Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadJunction).value : 0;
		g_roadJitter = g_roadLayout == 2 ? Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadJitter).value : 0;
	}
	if(!(v = get("render_roadlayout")).empty())
	{
		if(v == "0" || v == "1" || v == "2")
			g_roadLayout = v[0] - '0';
		else
			return bad("render_roadlayout", v, "0, 1 or 2");
	}
	if(!(v = get("render_roadlayout_junction")).empty())
	{
		if(!ParseInt(v, 0, 3, n))
			return bad("render_roadlayout_junction", v, "0..3");
		g_roadJunction = (int)n;
	}
	if(!(v = get("render_roadlayout_jitter")).empty())
	{
		if(!ParseInt(v, 0, 7, n))
			return bad("render_roadlayout_jitter", v, "0..7");
		g_roadJitter = (int)n;
	}
	if(g_roadLayout != 2)
		g_roadJunction = g_roadJitter = 0;   // the shape belongs to layout 2 only (a bundle of layout 1 never carries it)
	{
		srvextras::RoadLayoutChoice rc;
		rc.layout = g_roadLayout == 2 ? 2 : 1;
		rc.junction = g_roadJunction;
		rc.jitter = g_roadJitter;
		srvextras::RoadLayoutChoiceSlot() = rc;
	}
	// epic 0431, ticket 4554: render_forest and its test keys (the block comment at g_forest)
	g_forest = SceneForestFactorySlot() ? 1 : 0;   // a program without server/src/forest/ (the render test host): 0
	g_forestIdsWaitMs = 10000;
	g_forestSeed = 0;
	if(!(v = get("render_forest")).empty())
	{
		if(v == "1" || v == "0")
			g_forest = v == "1" ? 1 : 0;
		else
			return bad("render_forest", v, "0 or 1");
		if(g_forest && !SceneForestFactorySlot())
			return bad("render_forest", v, "this program does not link the forest scene (server/src/forest/)");
	}
	if(!(v = get("render_test_forest_ids_wait_ms")).empty())
	{
		if(!ParseInt(v, 0, 600000, n))
			return bad("render_test_forest_ids_wait_ms", v, "0..600000");
		g_forestIdsWaitMs = (double)n;
	}
	if(!(v = get("render_test_forest_seed")).empty())
	{
		if(!ParseInt(v, 0, 2147483647, n))
			return bad("render_test_forest_seed", v, "0..2147483647 (0: random per session)");
		g_forestSeed = (uint32_t)n;
	}
	g_forestCutWaitMs = 20000;
	g_forestCutFail = 0;
	if(!(v = get("render_test_cut_wait_ms")).empty())
	{
		if(!ParseInt(v, 0, 600000, n))
			return bad("render_test_cut_wait_ms", v, "0..600000 (0: no bound)");
		g_forestCutWaitMs = (double)n;
	}
	if(!(v = get("render_test_cut_fail")).empty())
	{
		if(v == "1" || v == "0")
			g_forestCutFail = v == "1" ? 1 : 0;
		else
			return bad("render_test_cut_fail", v, "0 or 1");
	}
	// epic 0375, issue 4454: render_units, render_units_passes and the test keys (the block comment at g_units)
	g_units = 0;
	g_unitPasses = 0;
	g_unitsDump.clear();
	g_unitsDumpMax = 1000;
	g_unitsDb.clear();
	g_unitsTable.clear();
	g_unitsAnim = 1;
	g_unitsAnimFile.clear();
	g_unitsTriggers = 0;   // ticket 4722
	g_unitsTierGate = 1;   // ticket 4737
	g_unitsChargeAny = 1;  // ticket 4737 (rule A); 1 by default again since ticket 4743 W81 (0 from W46 to W81)
	g_unitsTurnFirst = 1;  // ticket 4735 (rule A)
	g_unitsFightDrawn = 1; // ticket 4744 (rule A)
	g_unitsReformFirst = 1;   // tickets 4745 + 4747 (rule A)
	g_unitsWalkHome = 1;      // ticket 4742 (rule A)
	g_unitsCityInPlace = 1;   // ticket 4748 (rule A)
	g_unitsReadyAfterMove = 1;   // ticket 4746 (rule A)
	g_unitsDefendFromStance = 1;   // ticket 4750 (rule A)
	g_unitsReformEndsAtArrival = 1;   // ticket 5050 (rule A; 1 since W74)
	g_unitsAttackFromStance = 1;      // ticket 5050 (rule A; 1 since W74)
	g_unitsRushRideThrough = 1;       // ticket 5057 (rule A)
	g_unitsDefenderWaitsFirstLanding = 1;   // ticket 5057 (rule A)
	g_unitsDeathByVariant = 1;        // ticket 5053 (rule A): default 1 (MC, 2026-11-25)
	g_unitsEndAfterDying = 1;         // ticket 5053 (rule A): default 1 (MC, 2026-11-25)
	g_unitsVictoryWord = 1;           // ticket 5059 (rule A)
	g_unitsType2Phase2 = 1;           // ticket 5067 S1 (rule A): default 1, MC #12907
	g_unitsMeleeReach = 1;            // ticket 0743 (rule A): default 1
	g_unitsDefenderStep = 1;          // ticket 5061 (rule A): default 1
	g_unitsOrdersAfterWalk = 1;       // ticket 5065 (rule A)
	g_unitsTriggerStrikerWeapon = 1;  // ticket 5052 (rule A): default 1, MC #10847
	g_unitsTriggerTerrain = 1;        // ticket 5052 (rule A): default 1, MC #11356
	g_unitsProjectilePose = 1;        // ticket 5064 (rule A): default 1
	g_unitsCityVolley = 1;            // ticket 0742 (rule A): default 1
	g_unitsTriggerStrikerMelee = 1;   // ticket 5055 (rule A): default 1, MC #11607
	g_unitsTriggerCityEra = 1;        // ticket 5058 (rule A): default 1
	g_unitsFightAfterWalk = 1;        // ticket 5066 (rule A): default 1
	g_unitsTriggerCityShot = 1;       // ticket 5062 (rule A): default 1
	if(!(v = get("render_units")).empty())
	{
		if(v == "1" || v == "0")
			g_units = v == "1" ? 1 : 0;
		else
			return bad("render_units", v, "0 or 1");
		if(g_units && !srvunits::SceneUnitsAvailable())
			return bad("render_units", v, "this program does not link the unit scene (server/src/units/)");
	}
	if(!(v = get("render_units_passes")).empty())
	{
		if(v == "1" || v == "0")
			g_unitPasses = v == "1" ? 1 : 0;
		else
			return bad("render_units_passes", v, "0 or 1");
		if(g_unitPasses && !g_units)
			return bad("render_units_passes", v, "1 needs render_units=1");
	}
	if(!(v = get("render_test_units_dump")).empty())
	{
		const DWORD at = GetFileAttributesW(Wide(v).c_str());
		if(at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY))
			return bad("render_test_units_dump", v, "an existing folder");
		g_unitsDump = Wide(v);
	}
	if(!(v = get("render_test_units_dump_max")).empty())
	{
		if(!ParseInt(v, 1, 1000000, n))
			return bad("render_test_units_dump_max", v, "1..1000000");
		g_unitsDumpMax = (int)n;
	}
	if(!(v = get("render_test_units_db")).empty())
	{
		const DWORD at = GetFileAttributesW(Wide(v).c_str());
		if(at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY))
			return bad("render_test_units_db", v, "an existing file");
		g_unitsDb = Wide(v);
	}
	if(!(v = get("render_test_units_table")).empty())   // issue 4460: TEST ONLY
	{
		const std::wstring w = Wide(v);
		const size_t cut = w.find_last_of(L"\\/");
		const DWORD at = cut == std::wstring::npos ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(w.substr(0, cut).c_str());
		if(at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY))
			return bad("render_test_units_table", v, "a file in an existing folder");
		g_unitsTable = w;
	}
	if(!(v = get("render_units_triggers")).empty())   // ticket 4722 (checked against render_units_anim below)
	{
		if(v != "1" && v != "0")
			return bad("render_units_triggers", v, "0 or 1");
		g_unitsTriggers = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_tier_gate")).empty())   // ticket 4737: TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_tier_gate", v, "0 or 1");
		g_unitsTierGate = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_charge_any")).empty())   // ticket 4737 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_charge_any", v, "0 or 1");
		g_unitsChargeAny = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_turn_first")).empty())   // ticket 4735 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_turn_first", v, "0 or 1");
		g_unitsTurnFirst = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_fight_drawn_figures")).empty())   // ticket 4744 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_fight_drawn_figures", v, "0 or 1");
		g_unitsFightDrawn = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_reform_first")).empty())   // tickets 4745 + 4747 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_reform_first", v, "0 or 1");
		g_unitsReformFirst = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_walk_home_in_fight")).empty())   // ticket 4742 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_walk_home_in_fight", v, "0 or 1");
		g_unitsWalkHome = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_city_in_place")).empty())   // ticket 4748 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_city_in_place", v, "0 or 1");
		g_unitsCityInPlace = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_ready_after_move")).empty())   // ticket 4746 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_ready_after_move", v, "0 or 1");
		g_unitsReadyAfterMove = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_defend_from_stance")).empty())   // ticket 4750 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_defend_from_stance", v, "0 or 1");
		g_unitsDefendFromStance = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_reform_ends_at_arrival")).empty())   // ticket 5050 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_reform_ends_at_arrival", v, "0 or 1");
		g_unitsReformEndsAtArrival = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_attack_from_stance")).empty())   // ticket 5050 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_attack_from_stance", v, "0 or 1");
		g_unitsAttackFromStance = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_rush_ride_through")).empty())   // ticket 5057 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_rush_ride_through", v, "0 or 1");
		g_unitsRushRideThrough = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_defender_waits_first_landing")).empty())   // ticket 5057 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_defender_waits_first_landing", v, "0 or 1");
		g_unitsDefenderWaitsFirstLanding = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_death_by_variant")).empty())   // ticket 5053 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_death_by_variant", v, "0 or 1");
		g_unitsDeathByVariant = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_end_after_dying")).empty())   // ticket 5053 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_end_after_dying", v, "0 or 1");
		g_unitsEndAfterDying = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_victory_word")).empty())   // ticket 5059 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_victory_word", v, "0 or 1");
		g_unitsVictoryWord = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_type2_phase2")).empty())   // ticket 5067 S1 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_type2_phase2", v, "0 or 1");
		g_unitsType2Phase2 = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_melee_reach")).empty())   // ticket 0743 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_melee_reach", v, "0 or 1");
		g_unitsMeleeReach = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_defender_step")).empty())   // ticket 5061 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_defender_step", v, "0 or 1");
		g_unitsDefenderStep = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_orders_after_walk")).empty())   // ticket 5065 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_orders_after_walk", v, "0 or 1");
		g_unitsOrdersAfterWalk = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_trigger_striker_weapon")).empty())   // ticket 5052 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_trigger_striker_weapon", v, "0 or 1");
		g_unitsTriggerStrikerWeapon = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_units_trigger_terrain")).empty())   // ticket 5052 (rule A): TEST ONLY
	{
		if(v != "1" && v != "0")
			return bad("render_test_units_trigger_terrain", v, "0 or 1");
		g_unitsTriggerTerrain = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_projectile_pose")).empty())   // ticket 5064 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_projectile_pose", v, "0 or 1");
		g_unitsProjectilePose = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_city_volley")).empty())   // ticket 0742 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_city_volley", v, "0 or 1");
		g_unitsCityVolley = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_trigger_striker_melee")).empty())   // ticket 5055 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_trigger_striker_melee", v, "0 or 1");
		g_unitsTriggerStrikerMelee = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_trigger_city_era")).empty())   // ticket 5058 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_trigger_city_era", v, "0 or 1");
		g_unitsTriggerCityEra = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_fight_after_walk")).empty())   // ticket 5066 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_fight_after_walk", v, "0 or 1");
		g_unitsFightAfterWalk = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_trigger_city_shot")).empty())   // ticket 5062 (rule A)
	{
		if(v != "1" && v != "0")
			return bad("render_units_trigger_city_shot", v, "0 or 1");
		g_unitsTriggerCityShot = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_units_anim")).empty())   // issue 4456
	{
		if(v == "1" || v == "0")
			g_unitsAnim = v == "1" ? 1 : 0;
		else
			return bad("render_units_anim", v, "0 or 1");
		if(!g_units)
			return bad("render_units_anim", v, "needs render_units=1");
	}
	if(g_unitsTriggers && (!g_units || !g_unitsAnim))   // ticket 4722
		return bad("render_units_triggers", "1", "needs render_units=1 and render_units_anim=1");
	if(!(v = get("render_test_units_anim")).empty())   // issue 4456: TEST ONLY
	{
		const std::wstring w = Wide(v);
		const size_t cut = w.find_last_of(L"\\/");
		const DWORD at = cut == std::wstring::npos ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(w.substr(0, cut).c_str());
		if(at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY))
			return bad("render_test_units_anim", v, "a file in an existing folder");
		if(!g_units || !g_unitsAnim)
			return bad("render_test_units_anim", v, "needs render_units=1 and render_units_anim=1");
		g_unitsAnimFile = w;
	}
	// epic 0040, issue 4753: render_units_ready_hold_ms and render_units_positions (the block comment at g_unitsReadyHoldMs)
	g_unitsReadyHoldMs = srvunits::kUnitsReadyHoldMsDefault;
	g_unitsPositions = SceneOptions::kUnitsPositionsOwn;
	if(!(v = get("render_units_ready_hold_ms")).empty())
	{
		if(!ParseInt(v, 1, 600000, n))
			return bad("render_units_ready_hold_ms", v, "1..600000");
		g_unitsReadyHoldMs = (double)n;
	}
	if(!(v = get("render_units_positions")).empty())
	{
		if(v == "own" || v == "exe")
			g_unitsPositions = v == "exe" ? SceneOptions::kUnitsPositionsExe : SceneOptions::kUnitsPositionsOwn;
		else
			return bad("render_units_positions", v, "own or exe");
	}
	// epic 0042, ticket 4785: render_units_shadow (the block comment at g_unitsShadow)
	g_unitsShadow = 1;
	if(!(v = get("render_units_shadow")).empty())
	{
		if(v == "0" || v == "1")
			g_unitsShadow = v == "1" ? 1 : 0;
		else
			return bad("render_units_shadow", v, "0 or 1");
	}
	g_unitsTestOffset[0] = g_unitsTestOffset[1] = 0;   // ticket 4785, TEST ONLY
	if(!(v = get("render_test_units_offset")).empty())
	{
		float dx = 0, dy = 0;
		char tail = 0;
		if(sscanf_s(v.c_str(), "%f,%f%c", &dx, &dy, &tail, 1) != 2 || !(dx > -500 && dx < 500) || !(dy > -500 && dy < 500))
			return bad("render_test_units_offset", v, "dx,dy within +-500 world units");
		g_unitsTestOffset[0] = dx;
		g_unitsTestOffset[1] = dy;
	}
	g_unitsQuickCombat = 0;   // ticket 0796 (rule A; TEST ONLY): default 0
	if(!(v = get("render_test_units_quick_combat")).empty())
	{
		if(v != "0" && v != "1")
			return bad("render_test_units_quick_combat", v, "0 or 1");
		g_unitsQuickCombat = v == "1" ? 1 : 0;
	}
	if(!(v = get("render_test_shadow_wait_ms")).empty())
	{
		if(!ParseInt(v, 0, 600000, n))
			return bad("render_test_shadow_wait_ms", v, "0..600000 (0: no bound)");
		g_shadowWaitMs = (double)n;
	}
	g_iceTestFile = get("render_test_ice_file");   // issue 4308 (a test key; checked when the scene starts)
	if(!(v = get("render_test_ice_wait_ms")).empty())
	{
		if(!ParseInt(v, 0, 600000, n))
			return bad("render_test_ice_wait_ms", v, "0..600000 (0: no bound)");
		g_iceWaitMs = (double)n;
	}
	if(!(v = get("render_size")).empty())
	{
		unsigned w = 0, hh = 0;
		char extra = 0;
		if(sscanf_s(v.c_str(), "%ux%u%c", &w, &hh, &extra, 1) != 2 || w < 16 || hh < 16 || w > 16384 || hh > 16384)
			return bad("render_size", v, "WxH, 16..16384 each");
		c.width = w;
		c.height = hh;
	}
	auto integer = [&](const char* key, long long lo, long long hi, int& dst) {
		const std::string s = get(key);
		if(s.empty())
			return true;
		long long x = 0;
		if(!ParseInt(s, lo, hi, x))
			return bad(key, s, ("an integer " + std::to_string(lo) + ".." + std::to_string(hi)).c_str());
		dst = (int)x;
		return true;
	};
	if(!integer("render_tile_plots", 0, c5t::kMaxTilePlots, c.tilePlots) || !integer("render_tile_d4", 0, 1024, c.tileD4) ||
	   !integer("render_pass", -1, 2, c.pass) || !integer("render_stale_frames", 1, 1000000, c.staleFrames) ||
	   !integer("render_test_device_lost", 0, 1000000, c.testDeviceLostEvery) || !integer("render_test_throw", 0, 1000000, c.testThrowAt) ||
	   !integer("render_test_av", 0, 1000000, c.testAvAt) || !integer("render_test_dump_after", 1, 1000000, c.testDumpAfter))
		return false;
	int backoff = (int)c.rebuildBackoffMs;
	if(!integer("render_rebuild_backoff_ms", 10, 30000, backoff))
		return false;
	c.rebuildBackoffMs = (uint32_t)backoff;
	if(!(v = get("render_tess")).empty())
	{
		if(!ParseDouble(v, 0, 64, d))
			return bad("render_tess", v, "a number 0..64");
		c.tess = d;
	}
	if(!(v = get("render_color")).empty())
	{
		c5t::ColorSource cs;
		if(!ColorSourceFrom(v, false, cs))
			return bad("render_color", v, "caches, material, constant, height or normal");
		c.color = v;
	}
	if(!(v = get("render_fallback")).empty())
	{
		c5t::ColorSource cs;
		if(!ColorSourceFrom(v, true, cs))
			return bad("render_fallback", v, "material, constant, height or normal");
		c.fallback = v;
	}
	if(!(v = get("render_constant_rgba")).empty())
	{
		char* end = nullptr;
		const unsigned long long x = _strtoui64(v.c_str(), &end, 16);
		if(!end || *end || x > 0xFFFFFFFFull)
			return bad("render_constant_rgba", v, "hex 0xAABBGGRR");
		c.constantRgba = (uint32_t)x;
	}
	if(!(v = get("render_test_dump")).empty())
		c.testDump = Wide(v);
	// epic 0301, issue 3904: the scene recorder's four keys (refused here like every render_* key: the renderer does not start)
	{
		RecorderKeys rk;
		if(!ParseRecorderKeys(get, rk, why))
			return false;
		if(rk.on)
			c.testRecord = rk.given;
		// ticket 0435 (water-system.md 15.1.5): a recording draws with the flag unless the key says otherwise, so that the
		// bundle's dumps are byte-stable and the player (always flagged, 0336) equals them at 0:0
		if(rk.on && get("render_prevent_driver_threading").empty())
			g_preventDriverThreading = true;
	}
	// ---- issue 3308: the tile source, the server's bake, the pacing before the first publish
	if(!(v = get("render_tiles")).empty())
	{
		if(v != "bake" && v != "tap" && v != "none")
			return bad("render_tiles", v, "bake, tap or none");
		c.tiles = v;
	}
	if(!(v = get("render_baker")).empty())
	{
		if(v != "pipeline" && v != "standin")
			return bad("render_baker", v, "pipeline or standin");
		c.baker = v;
	}
	v = get("render_bake_install");
	c.bakeInstall = Wide(v.empty() ? gameDir : v);
	if(!integer("render_bake_radius", -1, 128, c.bakeRadius) || !integer("render_bake_burst", 0, 4096, c.bakeBurst))
		return false;
	auto real = [&](const char* key, double lo, double hi, bool loOpen, double& dst) {
		const std::string s = get(key);
		if(s.empty())
			return true;
		double x = 0;
		if(!ParseDouble(s, lo, hi, x) || (loOpen && x <= lo))
			return bad(key, s, ("a number " + std::string(loOpen ? "above " : "") + std::to_string(lo) + ".." + std::to_string(hi)).c_str());
		dst = x;
		return true;
	};
	if(!real("render_bake_budget_ms", 0, 1000, true, c.bakeBudgetMs) || !real("render_bake_rate", 0, 100000, true, c.bakeRate) ||
	   !real("render_bake_gpu_ms_per_s", 0, 1000, false, c.bakeGpuMsPerS) || !real("render_idle_poll_ms", 0, 1000, false, c.idlePollMs))
		return false;
	g_testBakeCpuMs = 0;   // issue 0140
	if(!real("render_test_bake_cpu_ms", 0, 1000, false, g_testBakeCpuMs))
		return false;
	// ticket 0688: the pass's stage timer and its lines (HostConfig::passStats, passLongMs)
	if(!real("render_pass_long_ms", 0, 10000, false, c.passLongMs))
		return false;
	if(!(v = get("render_pass_stats")).empty())
	{
		if(v != "0" && v != "1")
			return bad("render_pass_stats", v, "0 or 1");
		c.passStats = v == "1";
	}
	// ticket 0713: the prepare phase (HostConfig::prepare; absent = 1)
	if(!(v = get("render_prepare")).empty())
	{
		if(v != "0" && v != "1")
			return bad("render_prepare", v, "0 or 1");
		c.prepare = v == "1";
	}
	// ticket 0711 (J1): the frame-jobs pool (srv_frame_jobs.h ParseFrameJobsKey; absent = auto)
	{
		v = get("render_frame_jobs");
		std::string jwhy;
		if(!ParseFrameJobsKey(v.empty() ? std::string("auto") : v, FrameJobsLogicalCores(), c.frameJobs, jwhy))
			return bad("render_frame_jobs", v, jwhy.c_str());
		if(!(v = get("render_test_frame_jobs_jitter")).empty())
		{
			if(v != "0" && v != "1")
				return bad("render_test_frame_jobs_jitter", v, "0 or 1");
			c.frameJobsJitter = v == "1";
		}
	}
	// ticket 0753 (J3): record ahead (absent = auto)
	if(!(v = get("render_record_ahead")).empty())
	{
		if(v == "auto")
			c.recordAhead = -1;
		else if(v == "0" || v == "1")
			c.recordAhead = v == "1" ? 1 : 0;
		else
			return bad("render_record_ahead", v, "auto, 0 or 1");
	}
	if(!(v = get("render_test_bake_fail")).empty())
	{
		int x = -1, y = -1;
		char extra = 0;
		if(sscanf_s(v.c_str(), "%d,%d%c", &x, &y, &extra, 1) != 2 || x < 0 || y < 0 || x > 4096 || y > 4096)
			return bad("render_test_bake_fail", v, "X,Y (a tile, 0..4096 each)");
		c.testBakeFailX = x;
		c.testBakeFailY = y;
	}
	if(!(v = get("render_test_init_fail")).empty())   // ticket 0600 (TEST ONLY)
	{
		if(v == "adapter")
			c.testInitFail = VPFSF_ADAPTER;
		else if(v == "device")
			c.testInitFail = VPFSF_DEVICE;
		else if(v == "renderer")
			c.testInitFail = VPFSF_RENDERER;
		else if(v == "section")
			c.testInitFail = VPFSF_SECTION;
		else if(v == "exception")
			c.testInitFail = kTestInitFailException;
		else
			return bad("render_test_init_fail", v, "adapter, device, renderer, section or exception");
	}
	out = c;
	return true;
}

// Ticket 0600 (D118, D125): HostStart refused a renderer that server.cfg asked for: the 64-bit path cannot draw the world,
// so D125's route (HostConfig::fatal, exit 5), after the refusal's LOUD line. Not for "already started". Called with the
// host lock released (server.exe's fatal does not return).
bool StartRefused(const HostConfig& cfg, const std::string& why)
{
	if(cfg.fatal)
	{
		const std::string t = "the server's renderer could not start: " + why;
		RNote("LOUD: D125 route: %s", t.c_str());
		cfg.fatal(kRenderInitFatalExitCode, t.c_str());
	}
	return false;
}

bool HostStart(const HostConfig& cfg, std::string& why)
{
	AcquireSRWLockExclusive(&g_hostLock);
	if(g_host)
	{
		ReleaseSRWLockExclusive(&g_hostLock);
		why = "already started";
		return false;
	}
	Host* h = new Host();
	h->cfg = cfg;
	LogOpen(cfg.logPath.c_str(), cfg.log);
	// issue 3308: BakedTileCameraRadius from the options file the server read (SrvOptions()), unless server.cfg names one
	if(h->cfg.tiles == "bake")
	{
		const bool fromOption = h->cfg.bakeRadius < -1;
		if(fromOption)
			h->cfg.bakeRadius = RenderOptionBakedTileCameraRadius();
		RNote("render host: tiles from the server's bake (baker %s, install '%ls'), BakedTileCameraRadius %d (%s)", h->cfg.baker.c_str(),
		      h->cfg.bakeInstall.c_str(), h->cfg.bakeRadius, fromOption ? "[Renderer] option" : "server.cfg render_bake_radius");
	}
	else
		RNote("render host: tiles %s", h->cfg.tiles == "tap" ? "from the EXE's caches (bake tap: the test reference)" : "none (fallback colour)");
	RNote("render host: handover %s, section '%ls', textures '%ls', EXE '%ls', log '%ls', depth %d, gpu sync %d%s%s%s",
	      cfg.handover.c_str(), cfg.section.c_str(), cfg.textureBase.c_str(), cfg.exe.c_str(), cfg.logPath.c_str(), (int)cfg.depth,
	      (int)cfg.gpuSync, cfg.testDeviceLostEvery || cfg.testThrowAt || cfg.testAvAt ? " TEST SWITCHES:" : "",
	      cfg.testDeviceLostEvery ? " device_lost" : "", cfg.testThrowAt ? " throw" : cfg.testAvAt ? " av" : "");
	if(cfg.bindImports)
	{
		std::string bwhy;
		if(!cfg.bindImports(bwhy))
		{
			why = "the D3D imports could not be bound: " + bwhy;
			RNote("LOUD: not started: %s", why.c_str());
			delete h;
			ReleaseSRWLockExclusive(&g_hostLock);
			return StartRefused(cfg, why);   // ticket 0600
		}
	}
	if(!h->scene.attach(cfg.section))
	{
		why = "the version-3 section could not be mapped";
		RNote("LOUD: not started: %s", why.c_str());   // ticket 0600: this refusal had no LOUD line
		delete h;
		ReleaseSRWLockExclusive(&g_hostLock);
		return StartRefused(cfg, why);   // ticket 0600
	}
	h->frame = vpframe::ring::MapSection(cfg.section.c_str(), h->frameMap);
	if(!h->frame)
	{
		why = "the frame section " + Narrow(cfg.section) + ".Frame could not be mapped";
		RNote("LOUD: not started: %s", why.c_str());
		delete h;
		ReleaseSRWLockExclusive(&g_hostLock);
		return StartRefused(cfg, why);   // ticket 0600
	}
	// another live server on this section: refuse before writing anything into its blocks
	const VPFrameServer* fs = VPFRAME_SERVER(h->frame);
	uint32_t other = 0;
	if(fs->magic == VPFRAME_MAGIC && fs->serverPid != GetCurrentProcessId() && fs->state != VPFS_STOPPED)
		other = fs->serverPid;
	else if(h->scene.header()->magic == VPSHELL_MAGIC && h->scene.header()->serverPid != GetCurrentProcessId())
		other = h->scene.header()->serverPid;
	if(other)
	{
		HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, other);
		const bool alive = hp && WaitForSingleObject(hp, 0) == WAIT_TIMEOUT;
		if(hp)
			CloseHandle(hp);
		if(alive)
		{
			why = "the section is served by another live server, pid " + std::to_string(other);
			RNote("LOUD: not started: %s", why.c_str());
			UnmapViewOfFile(h->frame);
			CloseHandle(h->frameMap);
			delete h;
			ReleaseSRWLockExclusive(&g_hostLock);
			return StartRefused(cfg, why);   // ticket 0600
		}
	}
	// epic 0375, issue 4454: the unit scene before the thread's first drain, and with it the DLL side's unit writers
	// (decision 8: with render_units=0 neither happens and no unit code runs)
	if(g_units)
	{
		std::string uwhy;
		const SceneOptions so = SceneOpts(h->cfg);
		if(!h->scene.configureUnits(true, so.unitsOpt, uwhy))
		{
			why = uwhy;
			RNote("LOUD: not started: %s", why.c_str());
			UnmapViewOfFile(h->frame);
			CloseHandle(h->frameMap);
			delete h;
			ReleaseSRWLockExclusive(&g_hostLock);
			return StartRefused(cfg, why);   // ticket 0600
		}
		// the DLL side's unit writers (SrvFeed::SetUnitsEnabled(true)) are turned on by the live unit scene's factory
		// (srv_units_scene.cpp MakeLive), so that this file names no unit symbol of the feed
		RNote("render host: render_units=1 (the server's unit scene; render_units_passes=%d, render_units_anim=%d%s%ls%s%ls%s%ls)", g_unitPasses,
		      g_unitsAnim, g_unitsDump.empty() ? "" : ", TEST KEY render_test_units_dump=", g_unitsDump.c_str(),
		      g_unitsTable.empty() ? "" : ", TEST KEY render_test_units_table=", g_unitsTable.c_str(),
		      g_unitsAnimFile.empty() ? "" : ", TEST KEY render_test_units_anim=", g_unitsAnimFile.c_str());
	}
	// epic 0561, ticket 4875: the model module before the thread starts (it asks the feed for the database copy at the
	// first load: SrvFeed::SetModelDatabaseWanted); with both keys 0 nothing is made
	if(g_models.any())
	{
		std::string mwhy;
		const SceneOptions so = SceneOpts(h->cfg);
		if(!h->scene.configureModels(so.models, mwhy))
		{
			why = mwhy;
			RNote("LOUD: not started: %s", why.c_str());
			UnmapViewOfFile(h->frame);
			CloseHandle(h->frameMap);
			delete h;
			ReleaseSRWLockExclusive(&g_hostLock);
			return StartRefused(cfg, why);   // ticket 0600
		}
	}
	SetState(*h, VPFS_STARTING);
	h->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	h->thread = h->stop ? CreateThread(nullptr, 8 * 1024 * 1024, ThreadProc, h, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr) : nullptr;
	if(!h->thread)
	{
		why = "CreateThread failed (" + std::to_string(GetLastError()) + ")";
		RNote("LOUD: not started: %s", why.c_str());
		SetState(*h, VPFS_STOPPED);
		if(h->stop)
			CloseHandle(h->stop);
		delete h;
		ReleaseSRWLockExclusive(&g_hostLock);
		return StartRefused(cfg, why);   // ticket 0600
	}
	h->started = 1;
	g_host = h;
	ReleaseSRWLockExclusive(&g_hostLock);
	return true;
}

void HostStop(const char* why)
{
	AcquireSRWLockExclusive(&g_hostLock);
	Host* h = g_host;
	if(!h || InterlockedExchange(&h->stopped, 1))
	{
		ReleaseSRWLockExclusive(&g_hostLock);
		return;
	}
	RNote("stop: %s", why ? why : "-");
	SetEvent(h->stop);
	const DWORD w = WaitForSingleObject(h->thread, 5000);
	if(w != WAIT_OBJECT_0)
		RNote("LOUD: the render thread did not stop within 5 s (left running; the frame section is marked STOPPED)");
	if(h->state != VPFS_FAILED)
		SetState(*h, VPFS_STOPPED);
	if(VPFrameServer* s = FrameServer(*h))
	{
		if(s->serverPid == GetCurrentProcessId())
		{
			MemoryBarrier();
			s->magic = 0;
		}
	}
	RNote("%s", HostSummary().c_str());
	ReleaseSRWLockExclusive(&g_hostLock);
}

void HostAbandon()
{
	Host* h = g_host;
	if(!h || !h->frame)
		return;
	VPFrameServer* s = VPFRAME_SERVER(h->frame);
	if(s->serverPid == GetCurrentProcessId())
	{
		s->state = h->state == VPFS_FAILED ? VPFS_FAILED : VPFS_STOPPED;
		MemoryBarrier();
		s->magic = 0;
	}
	if(VPShellHeader* hdr = h->scene.header())
		if(hdr->serverPid == GetCurrentProcessId())
			hdr->magic = 0;
}

uint32_t HostState()
{
	Host* h = g_host;
	return h ? (uint32_t)h->state : 0;
}


std::string HostSummary()
{
	Host* h = g_host;
	if(!h)
		return "";
	char b[1024];
	const uint32_t st = (uint32_t)h->state;
	_snprintf_s(b, sizeof(b), _TRUNCATE,
	            "render: handover %s, state %s%s%s, draws %lld, published %lld, busy %lld, not ready %lld, sink errors %lld, draw errors %lld, "
	            "lost devices %lld, rebuilds %lld, exceptions %lld, cameras skipped %lld, waits event %lld legacy %lld idle %lld, "
	            "pace fallbacks %lld resumes %lld, feed drains %lld events %lld changed plots %lld, prepare passes %lld nothing %lld "
	            "camera hits %lld camera polls %lld hold repolls %lld tap yields %lld%s%s",
	            h->cfg.handover.c_str(), kStateNames[st < 6 ? st : 0], st == VPFS_FAILED ? " " : "",
	            st == VPFS_FAILED ? kFailNames[(uint32_t)h->failReason < 7 ? h->failReason : 0] : "", (long long)h->draws,
	            (long long)h->published, (long long)h->busy, (long long)h->notReady, (long long)h->sinkErrors, (long long)h->drawErrors,
	            (long long)h->lost, (long long)h->rebuilds, (long long)h->exceptions, (long long)h->camerasSkipped, (long long)h->waitsEvent,
	            (long long)h->waitsLegacy, (long long)h->waitsIdle, (long long)h->paceFallbacks, (long long)h->paceResumes, (long long)h->feedDrains, (long long)h->feedEvents, (long long)h->feedChanged,
	            (long long)h->prepPasses, (long long)h->prepNothing, (long long)h->camHits, (long long)h->camPolls, (long long)h->holdRepolls,
	            (long long)h->tapYields, h->failText.empty() ? "" : "; ", h->failText.c_str());
	return b;
}

}  // namespace srvrender
