// vp64_options.h - the options file of the 64-bit patch (one-off 9042, decisions D89 and D88).
//
// One plain INI file, VP64Options.ini, read by every part of the patch that has user-facing options. The same code is
// built into the x86 proxy (clang-cl -m32, VC9 headers, msvcr90) and the x64 server (MSVC 14.4x STL), so the API uses
// no STL type and no C++11 library header: plain integers, char/wchar_t buffers and a warning callback. Nothing here
// allocates on the heap except LoadFile's file buffer (freed before it returns).
//
// Where the file is and how a value reaches each process (design: research/07-architecture-decisions/options-file.md):
//   - the proxy (in the EXE) resolves the path (DefaultPath), creates the file with commented defaults when it is
//     missing (CreateDefaultFile), loads it, logs the warnings to vpproxy.log and writes the path into the server.cfg
//     of the boot as options_file=<UTF-8 path>;
//   - the server loads the same file with the same code at start-up (server/src/srv_options.cpp), logs to server.log,
//     and keeps the values for the process (SrvOptions()); the renderer runs inside the server and reads them there.
// The player-facing description of the format and of every option: docs/OPTIONS.md.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>

namespace Vp64Opt
{
// ================================================================================================ the options
// One entry per option. Adding one: append an id before kCount, add its row to the table in vp64_options.cpp, describe
// it in docs/OPTIONS.md (a unit test checks that the document names every key and its default).
enum Id
{
	kBakedTileCameraRadius = 0,  // [Renderer] BakedTileCameraRadius: -1 = keep the whole map's baked terrain resident
	                             // (default, D88); N = 0..128 = only the tiles within N rings of the camera view
	kServerRenderer = 1,         // [Renderer] ServerRenderer (issue 3305): 0 = off (default), 1 = the server draws the
	                             // terrain and the proxy loads the compositor (proxy/src/proxy_shell.cpp)
	kWhiteFogMask = 2,           // [Renderer] WhiteFogMask (issue 0172): 1 = on (default since D136, issue 0185), the proxy
	                             // gives the EXE's models a white fog-of-war mask when FluidFOW is off
	                             // (proxy/src/proxy_shell.cpp, White); 0 = off
	kReserved3 = 3,              // reserved (the owner, in person, one-off 9081, 2026-11-22): was [Renderer] UiDrawListGrowth;
	                             // the growth is always installed now (proxy/src/proxy_uidraw.cpp). The old comment:
	                             // [Renderer] UiDrawListGrowth (ticket 0487, MC 2026-11-17): 1 = on (default), the proxy
	                             // grows the EXE's per-frame UI draw buffers four times (proxy/src/proxy_uidraw.cpp);
	                             // 0 = the stock buffers. Id 3 on its branch; renumbered after 0437's and 0040's ids at
	                             // the merge that comes later
	kServerUnits = 4,            // [Renderer] ServerUnits (issue 4759): 0 = off (default); 1 = with ServerRenderer = 1 the
	                             // server draws the units (proxy/src/proxy_shell.cpp, UnitLines). Id 3 on epic 0040's
	                             // branch; 4 since 0040 merged after 0487 (MC #4045, #4204, #4579)
	kServerArc = 5,              // [Renderer] ServerArc (ticket 0199 part 2): 0 = off (default), the game draws the
	                             // ranged-attack arc; 1 = with ServerRenderer = 1 the server draws it and the game's own
	                             // is switched off (proxy/src/proxy_shell.cpp, ArcLines)
	kServerPath = 6,             // [Renderer] ServerPath (ticket 0300 part 2): 0 = off (default), the game draws the
	                             // move-order path's discs and chevrons; 1 = with ServerRenderer = 1 the server draws them
	                             // and the game's own are switched off (proxy/src/proxy_shell.cpp, PathLines)
	kServerFarmFields = 7,       // [Renderer] ServerFarmFields (ticket 4853, epic 0528 D): 0 = off (default), the game lays
	                             // out the farm fields; 1 = with ServerRenderer = 1 the server draws the farm fields and
	                             // tracks and the EXE's farm builder is no longer called (proxy/src/proxy_shell.cpp,
	                             // FarmLines). Id 8 once epic 0504's 7 has merged (MC #5171, #5192): 7 on this branch only
	kAiUpdateTickMs = 8,         // [Performance] AiUpdateTickMs (ticket 4662, D165 option 3): -1 = stock: the proxy
	                             // installs nothing; 0..100 = the EXE's update tick in AI turns, in milliseconds,
	                             // through a copy of the engine utility vftable (proxy/src/proxy_pacing.cpp). Default 0
	                             // since ticket 0566 (D191, 2026-11-19; was -1)
	kAiSkipVisualDrain = 9,      // [Performance] AiSkipVisualDrain (ticket 4665, D165 option 2): 0 = off (default): the
	                             // proxy installs nothing; 1 = after each AI player's Update the proxy marks the player
	                             // change as seen, so the EXE does not wait for queue B (proxy/src/proxy_pacing.cpp);
	                             // 2 = as 1, but at an AI gap with more than 100 undispatched records the proxy leaves the
	                             // EXE's own wait for queue B to act (ticket 0571, D191, 0568's variant F1, prototype)
	kAiDrainUnordered = 10,      // [Performance] AiDrainUnordered (ticket 4675, D165 option 4): 0 = off (default): the
	                             // proxy reads nothing more; 1 = (only with AiSkipVisualDrain = 1 or 2) the AI turn's end-
	                             // of-phase drain of queue B in one frame: the proxy clears its ordering byte there (and,
	                             // with 2, at each AI gap left to the EXE's wait; ticket 0571) (proxy/src/proxy_pacing.cpp)
	kGameEventsOneCrossing = 11, // [Performance] GameEventsOneCrossing (ticket 4804, speedup P2): 0 = off (default);
	                             // 1 = a GameEvents raise crosses once (kRtE_LuaRaise; server/src/remote/
	                             // srv_remote_lua.cpp). Id 11 since main's 0199, 0300, 0528 and 0437 took 5-10 (MC #4895,
	                             // #5688, #5980, 2026-11-18)
	kServerCities = 12,          // [Renderer] ServerCities (ticket 4875, epic 0561): 0 = off (default), the game draws the
	                             // cities (blocks, walls, wonders, display buildings, ruins); 1 = with ServerRenderer = 1 the
	                             // server draws them and the compositor drops the game's draws it has (proxy_shell.cpp,
	                             // ModelLines). Id reserved by the MC (#6034, K4): branches never renumber
	kServerLandmarks = 13,       // [Renderer] ServerLandmarks (ticket 4875): 0 = off (default); 1 = with ServerRenderer = 1
	                             // the server draws the improvements, resources, natural wonders and pontoon bridges
	                             // (proxy_shell.cpp, ModelLines). Id reserved by the MC (#6034, K4)
	kFixUnknownWonderBase = 14,  // [Extras] FixUnknownWonderBase (ticket 0613, K4: 0465's id; MC #13471): bugfix, 1 = on
	                             // (default, D169): a natural wonder the EXE does not know is drawn on its plot's own
	                             // ground, live and at every load, instead of on Mount Fuji's; 0 = the EXE's Fuji
	                             // (server/src/render/srv_liveterrain*.cpp, live-terrain.md 8.13.2). An [Extras] row
	                             // below the switches' ids: the id was reserved before the [Extras] block existed
	kHiddenEmbarks = 15,         // [Performance] HiddenEmbarks (ticket 0573, D193): 0 = off (default in the table; the
	                             // release configuration may write 1); 1 = the server tells the DLL not to post an
	                             // embark or disembark that no human can see (server/src/srv_hidden_embark.cpp)
	// ---- [Extras] (ticket 4931, epic 0454, D169/D170; docs/extras/flags-and-messages-design.md section 1). Ids reserved
	// by the MC (#7273, K4): never renumbered. An extra is in effect only when its category switch AND its own flag are
	// both on (and the extra it needs, if any): consumers ask Extra(), never the raw flag.
	kEnableBugfixes = 16,        // [Extras] EnableBugfixes: 1 = on (default): the fixes below that are on are used; 0 = every
	                             // fix is off, whatever its own line says
	kEnableVisualAdditions = 17, // [Extras] EnableVisualAdditions: 0 = off (default): no visual addition is used; 1 = the
	                             // additions below that are on are used. The installer's prompt writes it (vp64helper
	                             // visual-additions on); nothing else writes it
	kRoadLayout = 18,            // [Extras] RoadLayout (ticket 4940, D215; was FixRoadLayout 0-1 of ticket 4934, never shipped,
	                             // renamed by MC #8258/#8261): a visual addition (needs EnableVisualAdditions; that switch
	                             // off gives stock): 0 = stock (default, D212), 1 = the tidy layout, variant (e), 2 = all
	                             // connections. Until 2026-11-23 a category per value (1 a bugfix); the owner made every value a
	                             // visual addition (one-off 9081). The server lays the roads out itself
	                             // (server/src/extras/srv_extra_roads.*)
	kReserved19 = 19,            // reserved: [Extras] FixRoadBridges (bugfix, needs RoadLayout; X4)
	kReserved20 = 20,            // reserved: [Extras] FixRoadClearance (bugfix, needs RoadLayout; X4)
	kReserved21 = 21,            // reserved: [Extras] ExperimentSharperTerrain (visual; X9, never default 1 under this name)
	kVisibleAreaOutline = 22,    // [Extras] VisibleAreaOutline (ticket 4938, MC #7345: the contribution guide's worked
	                             // example): visual addition, 0 = off (default, not approved), 1 = a line on the ground
	                             // around the plots the active player sees now (server/src/extras/)
	// ids 23-27: epic 0455 (Extras B, the unit extras; MC #7491, 26 renamed FixAirBases by MC #7901, 27 by MC #8114).
	// Each needs ServerUnits = 1 as well (the consumer checks it, server/src/extras/units/). 28: ServerCombat (ticket
	// 4783), 29: GuardsWhenOff (ticket 0635, MC #9004); 30-31: RoadJunction, RoadJitter (ticket 4940, MC #10673); 32:
	// PerimeterGarrisons (ticket 4983, D243, MC #11877); 33: FixMinimapUnexplored (ticket 0727); 34-36: ticket 0745
	// (D255 gaps G2-G4: KeyboardHookThread, LeaderScenes, RenderFrameJobs); 37: FixSmoothWheel (ticket 0755, D255); 38:
	// KeepRuns (ticket 0747, D255 G5); 39: HideTreadEffect (ticket 0784, D268); 40: ModelStore (ticket 0789, D269); the
	// next free id is 41
	kFixUnitPlacement = 23,      // [Extras] FixUnitPlacement (ticket 4982, terrain only by MC #8099): bugfix, 0 = off
	                             // (default until the maintainer accepts), 1 = figures kept out of mountain and hill rock
	kGarrisonVisible = 24,       // [Extras] GarrisonVisible (ticket 4983): visual addition, 0 = off (default), 1 = a city's
	                             // military garrison drawn at the city (server/src/extras/units/srv_unit_garrison.*), look
	                             // (a). A test value since D243 (Def::testOnly): not in the default file, not on the
	                             // installer's page; PerimeterGarrisons (32) is the players' garrison option
	kShipsMoored = 25,           // [Extras] ShipsMoored (ticket 4984, D207: off by default): visual addition, 0 to 2
	                             // (D250): 0 = off (default); ships in a city drawn moored on the city plot's water side,
	                             // 1 = look (d) BowToLandSmall, 2 = look (b) AlongShore (server/src/extras/units/srv_unit_moored.*)
	kFixAirBases = 26,           // [Extras] FixAirBases (ticket 4985, MC #7901): bugfix, 0 = off, 1 = (the default
	                             // since D235, live smoke 2026-11-22 01:26) the active team's based and carried aircraft drawn in every case
	                             // as the game draws them after a load (server/src/extras/units/srv_unit_airbases.*)
	kAirInterceptPatrol = 27,    // [Extras] AirInterceptPatrol (ticket 4986, MC #8114): visual addition, 0 = off (default),
	                             // 1 = own and team aircraft on intercept fly a holding loop in their range
	kServerCombat = 28,          // [Renderer] ServerCombat (ticket 4783, MC #8627): 1 = on (default): with ServerUnits = 1 the
	                             // server asks the proxy and route S (the server's fight clock, sounds and effects) arms as
	                             // one (proxy/src/proxy_route_s.h); 0 = the question is never asked: fights run on the
	                             // game's own timing, sounds and particles (proxy/src/proxy_shell.cpp, UnitLines). A
	                             // troubleshooting switch for testers; no effect while ServerUnits is 0
	kGuardsWhenOff = 29,         // [Proxy] GuardsWhenOff (ticket 0635, MC #9004, #9011; D224, D225): 1 = on (default):
	                             // with the patch switched off (mode=off, the pack fallback) the proxy still installs its
	                             // guards against the EXE's own faults (the UI message queue guard, proxy_queue_guard.cpp
	                             // InstallWhenOff; the farm builder guard, ProxyShell::OnBootOff); 0 = it installs none
	                             // then, a stock game (for diagnosis). Read by the proxy's BootOff only
	kRoadJunction = 30,          // [Extras] RoadJunction (ticket 4940, MC #10673): visual addition, the centre of a plot with
	                             // four to six roads under RoadLayout = 2: 0 = through (default), 1 = cross, 2 = ring,
	                             // 3 = rosette (c5r::extras::roads::Junction's values); read only while RoadLayout = 2 is in effect
	kRoadJitter = 31,            // [Extras] RoadJitter (ticket 4940, MC #10673): visual addition, 0 = none (default), 1-7 =
	                             // each plot's pass point moved by a fixed per-plot offset of up to N iso samples
	                             // (LayoutOptions::lineJitter); read only while RoadLayout = 2 is in effect
	kPerimeterGarrisons = 32,    // [Extras] PerimeterGarrisons (ticket 4983, D243, MC #11877): visual addition, 0 = off
	                             // (default), 1 = a city's military garrison drawn in a ring along the city plot's land
	                             // edges, facing out (GarrisonVisible's look (d); server/src/extras/units/srv_unit_garrison.*)
	kFixMinimapUnexplored = 33,  // [Extras] FixMinimapUnexplored (ticket 0727, 0552's route R1): bugfix, 1 = on (default,
	                             // D169): a plot the game sets back to unexplored leaves the EXE's minimap at once (the
	                             // proxy marks its cell; proxy/src/proxy_minimap_fog.*); 0 = as the stock game, until a load
	// ---- ticket 0745 (D255, 0739's audit gaps G2-G4, research/07 options-file.md section 10): features that shipped
	// without a key
	kKeyboardHookThread = 34,    // [Proxy] KeyboardHookThread (ticket 0720's move, gap G2): 1 = on (default): the proxy runs
	                             // the EXE's low-level keyboard hook on a helper thread and switches window ghosting off
	                             // (proxy/src/proxy_kbhook.cpp); 0 = neither: the EXE's own hook on its main thread, as stock
	kLeaderScenes = 35,          // [Proxy] LeaderScenes (ticket 0381, gap G4): 3D leader scenes for majors 22-42 (the 43-civ
	                             // variant): 0 = off (default, as stock), 1 = on demand, 2 = all at load
	                             // (proxy/src/proxy_leaderscene.cpp; vpproxy.ini leader_scenes and VPPROXY_LEADER_SCENES,
	                             // test switches, override it)
	kRenderFrameJobs = 36,       // [Performance] RenderFrameJobs (ticket 0711's pool, gap G3): -1 = auto (default: 2 workers
	                             // on more than 2 logical cores, else 0), 0 = no worker, 1..8 workers; the proxy writes
	                             // render_frame_jobs=N into server.cfg for 0..8 and nothing for -1 (proxy/src/proxy_rt.cpp)
	kFixSmoothWheel = 37,        // [Extras] FixSmoothWheel (ticket 0755, D255; research/02 camera-and-picking.md 11.4): bugfix,
	                             // 1 = on (proposed default, the owner confirms at the merge): the proxy's PeekMessageW
	                             // import-slot thunk sums WM_MOUSEWHEEL deltas into whole notches and hands a merged
	                             // multi-notch message out one notch at a time (proxy/src/proxy_wheel.*); 0 = as stock: every
	                             // wheel message one step, whatever its delta
	kKeepRuns = 38,              // [Logs] KeepRuns (ticket 0747, D255 G5/G5b; research/07 options-file.md 10.4-10.5): 1..100,
	                             // default 10: the run folders kept under <user>\Logs\VP64\run (this start's included) and the
	                             // kept shell_composite.<stamp>.log files in <user>\Logs (proxy/src/proxy_rt.cpp hands it to
	                             // the compositor as VPSHELL_KEEP_LOGS). Set in the file it wins over vpproxy.ini keep_runs
	                             // (a test switch, which wins over the default)
	kHideTreadEffect = 39,       // [Renderer] HideTreadEffect (ticket 0784, D268, D255; research/02 farm-builder-guard.md 15):
	                             // 1 = on (default): with ServerUnits = 1 the proxy's farm guard owner drops the EXE's two
	                             // landship tread effects (ART_DEF_VEFFECT_TANK_TREADS, _02) at the Particle object's
	                             // type-0x28 listener (a node word, D228 base product), so no tread shell parts from the
	                             // server's hull; the hull's moulded tracks stay, the treads no longer scroll. 0 = as the
	                             // game draws them. Read by proxy/src/proxy_shell.cpp (VPSHELL_TREAD_FILTER); no effect
	                             // while ServerUnits is 0
	kModelStore = 40,            // [Renderer] ModelStore (ticket 0789, D269, D255): 1 = the server's model store (default),
	                             // 0 = ticket 0709's keep; read by the server (srv_render_host.cpp), server.cfg
	                             // render_models_store (a test switch) wins
	kCount
};

enum Type
{
	kTypeInt = 0  // a whole number from minValue to maxValue (the only type so far; an on/off type is added with the
	              // first option that needs one)
};

// ================================================================================================ text options
// Ticket 0680: options whose value is a line of text. They have their own small table (TextId, TextDef), outside the
// whole-number table above, so that every loop over Id and kCount (the installer's schema, the logs, the release's
// option set) stays as it is. A text option is read from the same file with the same rules as the others, except for
// its value: written in double quotes it is taken exactly as written between them (spaces kept; ';' and '#' are part of
// it); without quotes it is trimmed and ends at an inline comment, as every other value. An empty value is a valid
// value (not the default). At most kMaxTextBytes bytes of UTF-8, no control character; else a warning and the default.
enum TextId
{
	kWindowTitlePrefix = 0,  // [Shell] WindowTitlePrefix (ticket 0680): put in front of the game window's title while the
	                         // patch runs (proxy/src/proxy_pack.cpp, TitleKeeper); default "[VP64] "; "" = the EXE's title
	                         // unchanged
	kTextCount
};
enum { kMaxTextBytes = 64 };

struct TextDef
{
	const char* section;       // home section, without brackets ("Shell")
	const char* key;           // as written in the file; matched without regard to case
	const char* defaultValue;  // the value when the file does not set it
	const char* help;          // comment lines for the default file, '\n'-separated, without the leading "; "
	const char* title;         // a short title (1-45 characters, no trailing period), as Def::title
};
const TextDef& TextDefinition(TextId id);
// The id of a text option's key (case-insensitive), or kTextCount when no text option has that name.
TextId FindTextKey(const char* key, size_t len);
// The rule for a text option's value (Parse uses it): true when value (len bytes) is at most kMaxTextBytes bytes of valid
// UTF-8 without a control character (below 0x20, or 0x7F); else false and *why (may be NULL) one clause.
bool TextValueValid(const char* value, size_t len, const char** why);

// What governs an option (ticket 4931). Every option outside [Extras] is kCatNone. The third category,
// EnableGameplayChanges (rules or saved state), is reserved by name only (proposal 4.1): no value, no switch, no row.
enum Category
{
	kCatNone = 0,    // not an extra
	kCatSwitch = 1,  // a category switch itself (EnableBugfixes, EnableVisualAdditions)
	kCatBugfix = 2,  // governed by EnableBugfixes; key starts with Fix (or Experiment)
	kCatVisual = 3   // governed by EnableVisualAdditions (an experiment of something shown included)
};

struct Def
{
	const char* section;  // home section, without brackets ("Renderer")
	const char* key;      // as written in the file ("BakedTileCameraRadius"); matched without regard to case
	Type type;
	int defaultValue;
	int minValue, maxValue;  // inclusive
	const char* help;        // comment lines for the default file, '\n'-separated, without the leading "; "
	// Ticket 4931: at the end, so that every row without them stays as it is (missing members are zero: kCatNone, NULL).
	unsigned char category;  // Category; kCatNone for every option outside [Extras]
	const char* needsKey;    // an extra this one needs (rule E5), by key; NULL = none
	// One-off 9081 (the owner, 2026-11-21): a short title (1-45 characters, no trailing period) the installer's options page
	// shows as the row's label; NULL only for a reserved row (an empty key). Not written into the options file.
	const char* title;
	// Ticket 4940 (MC #8261: a category per value; RoadLayout had one until one-off 9081, 2026-11-23): NULL = every
	// value has `category`; else one entry per value from minValue to maxValue, value 0's kCatNone (stock) and each other
	// value's kCatBugfix or kCatVisual. `category` is then the row's place in the default file and on the installer's page
	// (the category of its first non-zero value). Extra() decides by the category of the value in force.
	const unsigned char* valueCategory;
	// Ticket 4983 (D243, MC #11877): 1 = a test value, not offered to players: read, logged and documented like any other
	// row, but left out of the default file (DefaultFileText) and of the installer's page (server.exe
	// --write-options-schema). 0 (every other row) = offered.
	unsigned char testOnly;
};

const Def& Definition(Id id);
// The category that governs value v of row d: valueCategory's entry when the row has one (d.category for a value
// outside the range or for 0, the stock value), else d.category.
unsigned char CategoryOf(const Def& d, int v);
// The id of a key (case-insensitive), or kCount when no option has that name.
Id FindKey(const char* key, size_t len);

// ================================================================================================ extras (ticket 4931)
enum ExtraWhy
{
	kExtraOn = 0,           // in effect
	kExtraFlagOff = 1,      // its own flag is 0
	kExtraCategoryOff = 2,  // its category switch is 0
	kExtraNeedsOff = 3,     // the extra it needs (Def::needsKey) is not in effect
	kExtraNotAnExtra = 4    // the id is no extra (kCatNone or kCatSwitch, a reserved row, or out of range)
};
struct ExtraState
{
	bool on;
	int value;      // the flag's value when on; 0 otherwise (0 is the stock value of every extra: a naming-rule test)
	ExtraWhy why;
	Id decidedBy;   // the option that decided: the switch (kExtraCategoryOff), the needed extra (kExtraNeedsOff), the
	                // extra itself (kExtraOn, kExtraFlagOff); kCount for kExtraNotAnExtra. ExtraOf, which has no id,
	                // gives kCount for "itself"
};
struct Options;
// The effective state of an extra: its category switch and its own flag both on, and its needed extra in effect.
// Rules in this order: not an extra; switch 0 (the switch of the value's category, CategoryOf); own value 0; needed
// extra off; else on. Consumers ask only this, never the raw flag, and keep the result (no per-frame cost):
// Vp64Opt::Extra(SrvOptions(), Vp64Opt::kRoadLayout).value (0 stock, 1 tidy, 2 all connections)
ExtraState Extra(const Options& o, Id id);
// The same decision for a row given directly (tests use it with rows that are not in the table). need = the state of the
// extra d needs, or NULL when d needs none; a row that names a needsKey but gets need NULL is off (kExtraNeedsOff), the
// safe side. decidedBy for kExtraNeedsOff is FindKey(d.needsKey) (kCount for a key not in the table).
ExtraState ExtraOf(const Def& d, int ownValue, const ExtraState* need, const Options& o);
// The start log's line for one extra (snprintf semantics: at most cap-1 bytes and a NUL; returns the full length):
//   "RoadLayout: in effect (RoadLayout = 1 from the file; EnableVisualAdditions = 1 from the file)"
//   "FixUnitPlacement: off: EnableBugfixes = 0 (from the file) switches off every bugfix"
//   "RoadLayout: off: EnableVisualAdditions = 0 (default) switches off every visual addition"
//   "FixRoadBridges: off: it needs RoadLayout, which is off"
//   "ExperimentSharperTerrain: off: ExperimentSharperTerrain = 0 (default)"
//   "<key>: not an extra" (or "option <n>: not an extra" for a reserved row or an id out of range)
size_t DescribeExtra(const Options& o, Id id, char* buf, size_t cap);
// The same line for a row given directly with its state (tests use it with rows that are not in the table; a row
// without a key says "(no key): not an extra").
size_t DescribeExtraOf(const Def& d, int ownValue, bool ownFromFile, const ExtraState& s, const Options& o, char* buf,
                       size_t cap);
// The start log's first extras line (snprintf semantics):
//   "EnableBugfixes = 1 (default), EnableVisualAdditions = 0 (default)"
size_t DescribeSwitches(const Options& o, char* buf, size_t cap);

// The values in force. fromFile[i] is 1 when the file set option i to a valid value, 0 when the default is used.
struct Options
{
	int value[kCount];
	unsigned char fromFile[kCount];
	char text[kTextCount][kMaxTextBytes + 1];  // ticket 0680: the text options, NUL-terminated UTF-8
	unsigned char textFromFile[kTextCount];
	Options() { SetDefaults(); }
	void SetDefaults();
	int Get(Id id) const { return value[id]; }
	int BakedTileCameraRadius() const { return value[kBakedTileCameraRadius]; }  // -1 = whole map
	int ServerRenderer() const { return value[kServerRenderer]; }                // 0 = off, 1 = on
	int WhiteFogMask() const { return value[kWhiteFogMask]; }                    // 0 = off, 1 = on
	int ServerUnits() const { return value[kServerUnits]; }                      // 0 = off, 1 = on
	int ServerArc() const { return value[kServerArc]; }                          // 0 = off, 1 = on
	int ServerPath() const { return value[kServerPath]; }                        // 0 = off, 1 = on
	int ServerFarmFields() const { return value[kServerFarmFields]; }            // 0 = off, 1 = on
	int AiUpdateTickMs() const { return value[kAiUpdateTickMs]; }                // -1 = stock, 0..100 = ms
	int AiSkipVisualDrain() const { return value[kAiSkipVisualDrain]; }          // 0 = off, 1 = on, 2 = on + gap hold
	int AiDrainUnordered() const { return value[kAiDrainUnordered]; }            // 0 = off, 1 = on (with option 2)
	int ServerCities() const { return value[kServerCities]; }                    // 0 = off, 1 = on
	int ServerLandmarks() const { return value[kServerLandmarks]; }              // 0 = off, 1 = on
	int GameEventsOneCrossing() const { return value[kGameEventsOneCrossing]; }  // 0 = off, 1 = on
	int HiddenEmbarks() const { return value[kHiddenEmbarks]; }                  // 0 = off, 1 = on
	int ServerCombat() const { return value[kServerCombat]; }                    // 0 = off, 1 = on (with ServerUnits 1)
	int EnableBugfixes() const { return value[kEnableBugfixes]; }                // 0 = off, 1 = on (a switch, not an extra)
	int EnableVisualAdditions() const { return value[kEnableVisualAdditions]; }  // 0 = off, 1 = on (a switch)
	int GuardsWhenOff() const { return value[kGuardsWhenOff]; }          // 0 = off, 1 = on (ticket 0635)
	int KeyboardHookThread() const { return value[kKeyboardHookThread]; }        // 0 = stock, 1 = helper thread (0745)
	int LeaderScenes() const { return value[kLeaderScenes]; }                    // 0 = off, 1 = on demand, 2 = all (0745)
	int RenderFrameJobs() const { return value[kRenderFrameJobs]; }              // -1 = auto, 0..8 workers (0745)
	int KeepRuns() const { return value[kKeepRuns]; }                            // 1..100 run folders and logs kept (0747)
	int HideTreadEffect() const { return value[kHideTreadEffect]; }              // 0 = as stock, 1 = dropped (0784)
	int ModelStore() const { return value[kModelStore]; }                        // 0 = 0709's keep, 1 = the store (0789)
	const char* Text(TextId id) const { return text[id]; }                       // ticket 0680
	const char* WindowTitlePrefix() const { return text[kWindowTitlePrefix]; }   // "" = the EXE's title unchanged
	ExtraState Extra(Id id) const;                                             // Vp64Opt::Extra(*this, id)
};
inline ExtraState Options::Extra(Id id) const { return Vp64Opt::Extra(*this, id); }

// ================================================================================================ parsing
// Warnings are complete lines without a trailing newline, for example
//   "line 7: BakedTileCameraRadius = abc: not a whole number from -1 to 128; the default -1 is used".
// The callback may be NULL. ctx is passed through.
typedef void (*WarnFn)(void* ctx, const char* line);

// Parses INI text (UTF-8 or ASCII; a UTF-8 byte order mark is skipped; LF, CRLF or CR line ends). Starts from the
// defaults: out.SetDefaults() is called first. Returns the number of warnings. Rules (docs/OPTIONS.md, "Format"):
//   - blank lines and lines starting with ';' or '#' are comments;
//   - "[Section]" starts a section; a section no option belongs to is warned about once and its keys are still read;
//   - "Key = value"; spaces around both are ignored; a ';' or '#' after a space ends the value (inline comment);
//     one pair of double quotes around the value is removed;
//   - keys are matched without regard to case; a known key outside its home section is honoured with a warning;
//   - an unknown key is warned about and ignored; a repeated key is warned about and the last valid one wins;
//   - an empty or invalid value, or one out of range, is warned about and the default is used (an earlier valid
//     value of the same key is not kept: the line the player wrote last decides, and it says "default");
//   - a line that is neither a comment, a section nor "key = value" is warned about and ignored.
unsigned Parse(const char* text, size_t len, Options& out, WarnFn warn, void* ctx);

// The text of a new options file: a header, then every section with each option's help as comments and the option
// itself commented out at its default ("; BakedTileCameraRadius = -1"), so that a file nobody edited follows the
// defaults of later versions. Writes at most cap-1 bytes and a NUL (when cap > 0); returns the full length (without
// the NUL), as snprintf does. CRLF line ends (Notepad-friendly).
size_t DefaultFileText(char* buf, size_t cap);

// ================================================================================================ files (Win32)
enum { kMaxFileBytes = 64 * 1024 };  // a larger file is not read (warning; defaults)

enum LoadStatus
{
	kLoaded = 0,      // read and parsed (warnings may have been reported)
	kMissing = 1,     // no such file: defaults
	kUnreadable = 2,  // exists but cannot be opened or read: defaults, one warning
	kTooLarge = 3     // over kMaxFileBytes: defaults, one warning
};
// Reads and parses the file. A UTF-16LE file with a byte order mark (Notepad's "Unicode") is converted to UTF-8 first.
// out holds the defaults for every status but kLoaded. *warnings (may be NULL) gets the number of warnings.
LoadStatus LoadFile(const wchar_t* path, Options& out, WarnFn warn, void* ctx, unsigned* warnings);

enum CreateStatus
{
	kCreated = 0,       // the file did not exist and was written with DefaultFileText
	kExists = 1,        // the file exists: nothing is written (an existing file is never overwritten)
	kCreateFailed = 2   // could not be created (folder missing, no permission, disk full): nothing left behind
};
// Creates the file with the default text (CREATE_NEW: never replaces a file). Does not create folders. *error (may be
// NULL) gets GetLastError() on kCreateFailed.
CreateStatus CreateDefaultFile(const wchar_t* path, unsigned long* error);

// The file name and where it lives.
#define VP64OPT_FILE_NAME L"VP64Options.ini"
#define VP64OPT_ENV_VAR L"VP64_OPTIONS"
enum PathSource
{
	kPathNone = 0,       // no path (buffer too small, no Documents folder)
	kPathEnv = 1,        // the environment variable VP64_OPTIONS (a full path; tests and players who want another place)
	kPathDocuments = 2   // <Documents>\My Games\Sid Meier's Civilization 5\VP64Options.ini, beside the game's config.ini
};
// Resolves the path. Documents is asked of SHGetFolderPathW(CSIDL_PERSONAL) through shell32.dll loaded by name (the
// proxy adds no import; the test instances' Documents redirection hooks that function, so every instance has its own
// file). Computes only: creates nothing.
PathSource DefaultPath(wchar_t* buf, size_t cap);

// UTF-8 <-> UTF-16 for the server.cfg line (options_file=<UTF-8>). Return false when the buffer is too small or the
// input is invalid; the output is NUL-terminated on success.
bool WideToUtf8(const wchar_t* in, char* out, size_t cap);
bool Utf8ToWide(const char* in, wchar_t* out, size_t cap);
}  // namespace Vp64Opt
