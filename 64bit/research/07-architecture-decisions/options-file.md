# The patch's options file (VP64Options.ini)

Written 2026-11-13 by one-off 9042 (tracker `9042-patch-options-file.md`), on decisions D89 (one global options file,
plain INI, text-editor editable; the owner) and D88 (its first option, the baked-tile camera radius, default -1 = whole map;
the owner). Player-facing description: `docs/OPTIONS.md`. Code: `options/` (loader and tests), `proxy/src/proxy_options.cpp`,
`server/src/srv_options.{h,cpp}`.

## 1. Decisions and reasons

| Question | Decision | Why |
|---|---|---|
| Format | Plain INI: `[Section]`, `Key = value`, `;` or `#` comments | The owner's preference (D89 [reworded for privacy]: a plain INI file, or something equally simple); the game's own `config.ini` uses the same shape, so players know it |
| File name | `VP64Options.ini` | `VP64` is the patch's folder name in the D28 layout (`VP64/` beside the proxy); CamelCase like the game's `GraphicsSettingsDX11.ini`, `UserSettings.ini` |
| Location | `<Documents>\My Games\Sid Meier's Civilization 5\VP64Options.ini`, beside `config.ini` | Where players already edit the game's settings; per user; survives a patch update, a reinstall and Steam's "verify files" (which would not touch our files in the install anyway, but an installer replacing `MODS\(1) Community Patch\` would); writable without administrator rights (the install can be under `Program Files`); each test instance has its own Documents (the harness shim redirects `SHGetFolderPathW`, `tools/harness/native/README.md`), so instances never share options. Rejected: next to the patch's DLL (lost on reinstall, shared by every user, may need admin rights) |
| Override | Environment variable `VP64_OPTIONS` = a full path | Tests and players who keep files elsewhere; no second settings file needed. Not a `vpproxy.ini` key: that file is test configuration, and an environment variable also reaches the proxy before the ini is understood |
| First run | The proxy creates the file (Win32 `CREATE_NEW`, never replaces a file) with a header and every option commented out at its default. **Corrected 2026-11-20 (ticket 0624, section 7):** that holds for a stock session only; in a D33 session (config.ini in the patch's configuration) a missing file is created with the release's option set (`<x64_dir>\release-options.txt`) and `ServerRenderer = 1` uncommented, or not at all when the set is missing or unusable, and the renderer guard then refuses the boot. (Replaced: "creates the file ... at its default" for every session.) | A file nobody edited follows the defaults of later versions (if the default of an option changes, an untouched file does not pin the old value). The header says how to uncomment. The game folder must already exist (the EXE creates it); the loader creates no folders |
| Who reads it | The proxy (in the EXE, the only part of the patch that sees the player's Documents) finds, creates and reads it, logs every warning and every value to `vpproxy.log`, and writes `options_file=<UTF-8 path>` into the boot's `server.cfg`. The server reads the same file with the same code at start-up (`server.log`) and keeps the values for the process (`SrvOptions()`); the renderer runs inside the server and reads them there | One loader, one set of rules; the server never looks up Documents itself, so a direct-mode test server on the workstation never reads the owner's real `My Games` folder. Passing the path, not the values, keeps server.cfg free of a second encoding of every option |
| When read | Once per boot (the EXE's `DllGetGameContext` with `mode=on`); no reload while the game runs | The only option today (bake radius) sizes a cache at start-up; a live reload would need every consumer to handle a change. Players are told: edit while the game is closed |
| Errors | Never fatal. Unknown key: warning, ignored. Bad or out-of-range value: warning, default. Duplicate key: warning, last line counts. Known key in the wrong section or before any section: warning, honoured. Unknown section: warning, its keys still read. Unparsable line: warning, ignored. File over 64 KB: not read, warning, defaults. UTF-16 BE: refused with a warning; UTF-16 LE with BOM: converted | A typo must not stop a game from starting; every problem is visible in the log with its line number |
| Encoding | UTF-8 or ANSI text (UTF-8 BOM skipped); UTF-16 LE with BOM (older Notepad "Unicode") converted; LF, CRLF or CR line ends | Whatever Notepad writes |
| Inline comments | `;` or `#` after a space or tab, or at the start of the value, ends the value | Players write `= 5 ; less memory`; a `;` inside a value without a space before it stays (no option is affected today: all values are numbers) |
| Types | Whole numbers with a range, written as `-1` or `5` (sign optional, 1 to 9 digits, no hex, no decimals); one pair of double quotes allowed | The only type an option needs today; an on/off type is added with the first option that needs one (YAGNI) |
| Shared code | `options/src/vp64_options.cpp` + `options/include/vp64_options.h`: no STL type in the API, no C++11 library header, no exceptions, Win32 only; compiled into the proxy by `proxy/build_proxy.py` and into the server by `server/build_server.py` (`obj/options`, flags /W4 /WX) | The proxy's TUs must match the fork's VC9 STL layout (`_SECURE_SCL 0`, `proxy_rt_logic.h`); an API of plain ints, buffers and a callback cannot get that wrong. `shell32.dll` is loaded by name for `SHGetFolderPathW`, so the proxy's import check (KERNEL32, MSVCR90, ...) is unchanged |

## 2. The options

| Section | Key | Default | Range | Meaning | Consumer |
|---|---|---|---|---|---|
| `Renderer` | `BakedTileCameraRadius` | -1 | -1 to 128 | -1 = keep every land tile's baked terrain resident (D88; about 413 MB of video memory on the huge map, issue 3023's figure); N = only tiles within N rings of the camera view (5 suggested when memory is short; 0 = the view only) | epic 3400 (bake stage 1): `SrvOptions().BakedTileCameraRadius()` in the server. Nothing reads it yet [C 2026-11-13] |
| `Renderer` | `ServerRenderer` | 0 | 0 to 1 | 1 = the server draws the terrain: the proxy loads `shell_composite.dll` from its own folder after the SHA-256 check against `shell_composite.sha256`, writes `render=1` / `render_section=Local\VP64Shell_<pid>` into server.cfg, sets `VPSHELL_SECTION` for the composite and clears the EXE's Terrain view bit (D7 c1); 0 = none of it (issue 3305) | the proxy: `ProxyRtInt::ProxyOptions().ServerRenderer()` in `proxy/src/proxy_shell.cpp` (`ProxyShell::Configure`); the server gets it as `render=1` in server.cfg, not from the file [C 2026-11-13] |
| `Renderer` | `WhiteFogMask` | 1 (since D136, issue 0185, 2026-11-14; was 0 under 0172) | 0 to 1 | 1 = with FluidFOW off in config.ini, the proxy loads the map editor's `White.dds` once with the EXE's own texture loader (VA 0x8C6540), writes its handle into the FOW mask global (VA 0xDA12AC) and into the mask slot of the EXE's draw objects built before it loaded, so that buildings, wonders, improvements, resources and trees keep their textures (issue 0172, the product form of 3011; the owner allowed it and made it the product default, 0098 answered by D136); 0 = nothing. A file that does not set the key (every file the patch wrote before 0185 has it only as the comment `; WhiteFogMask = 0`) gets 1 | the proxy: `ProxyRtInt::ProxyOptions().WhiteFogMask()` in `proxy/src/proxy_shell.cpp` (`ProxyShell::OnBoot`, namespace `White`; details `research/02-exe-internals/disable-systems-and-masks.md`, "The white mask on the product path"). The server does not read it [C 2026-11-14] |
| `Renderer` | ~~`UiDrawListGrowth`~~ **removed 2026-11-22** (the owner, in person, one-off 9081: no longer an option; id 3 is a reserved row and the proxy always installs the growth; installer-notes 16.9). Was: | 1 (ticket 0487, MC decision 3 of 2026-11-17: a crash fix of what the stock EXE does wrong, not an `[Extras]` flag) | 0 to 1 | 1 = at the first in-game frames the proxy grows the EXE's per-frame UI draw buffers four times (4 MB, 16,384 commands per half; upstream #13406's effect), with the EXE's own allocators called by address after the EXE checks; a failure is non-fatal and loud (D118: `uidraw: FAILED`, the D119 report with stage `uidraw`, the popup once); 0 = the stock buffers (kept for comparisons) | the proxy: `ProxyRtInt::ProxyOptions().UiDrawListGrowth()` in `proxy/src/proxy_ui_bridge_rt.cpp` (`Init`), the growth in `proxy/src/proxy_uidraw.cpp`; design and results `research/03-dll-engine-boundary/upstream-13406-product.md` section 9. Id 3 on branch `task-0487-ui-draw-buffers`; epic 0437 (ids 3-5) and epic 0040 (`ServerUnits`) also took 3 on their branches, so the later merge renumbers. The server does not read it [C 2026-11-17] |
| `Renderer` | `ServerUnits` | 0 (MC's O1 (a), 2026-11-17: off until epic 0042's combat lands; switching it on is the owner's, D169) | 0 to 1 | 1 = the server draws the units (epic 0040, state (ii-d)): only when the boot writes the render lines (`ServerRenderer = 1`, the composite accepted or the test switch, no `server_config=`), the proxy adds `render_units=1`, `render_units_passes=1`, `render_units_positions=exe` to server.cfg and calls `ProxyUiBridgeRt::RequestUnitPositions()` (the Lua bridge's positions-only install, `research/03-dll-engine-boundary/proxy-design.md` 24.6); otherwise it writes nothing and logs one `shell: [Renderer] ServerUnits = 1: no unit keys ...` line naming why; 0 = nothing, server.cfg as before (issue 4759) | the proxy: `ProxyRtInt::ProxyOptions().ServerUnits()` in `proxy/src/proxy_shell.cpp` (`UnitLines`, called by `ProxyShell::Configure`); the server gets the three keys in server.cfg, not from the file; its own `render_units` default stays 0 for tests (breakdown O2 (a)). Id 4 (`kServerUnits`): 3 on epic 0040's branch, renumbered at 0040's merge after 0487 took 3 on main (MC #4045, #4204, #4579; 2026-11-17) [C 2026-11-17, code; tests: section 3] |
| `Extras` | `EnableBugfixes` | 1 (proposal 4.1, D169: fixes on by default) | 0 to 1 | The category switch of every bugfix extra (ticket 4931, epic 0454): an extra of category `kCatBugfix` is in effect only when this is 1 AND its own flag is non-zero AND the extra it needs (`Def::needsKey`) is in effect; 0 switches every fix off whatever its own line says. Id 16 (MC #7273, K4). `[Extras]` is written last in the default file, as one block (switches, bugfixes, visual additions); ids 18-21 are reserved empty-key rows for `RoadLayout` (id 18, renamed from `FixRoadLayout` by ticket 4940), `FixRoadBridges`, `FixRoadClearance`, `ExperimentSharperTerrain`, each filled by its feature's ticket with default 0 (E4). Naming and table rules (unique keys across the file, `Enable`/`Fix`/`Experiment` prefixes, 0 in every extra's range, needs without cycles, `kApprovedDefaults`) are a unit test (`options/tests/test_options.cpp` `TestExtrasTable`) | no consumer reads a switch directly: consumers ask `Vp64Opt::Extra(o, id)` (`ExtraState {on, value, why, decidedBy}`), server `SrvOptions()`, proxy `ProxyRtInt::ProxyOptions()`. Both processes log `options: extras: ...` at start (`LogExtras` in `server/src/srv_options.cpp` and `proxy/src/proxy_options.cpp`): the switches' line, then one line per extra with its state and why. No extra exists yet (X1 has the switches only) [C 2026-11-19, unit tests] |
| `Extras` | `EnableVisualAdditions` | 0 | 0 to 1 | The category switch of every visual addition (`kCatVisual`, experiments of something shown included, Q3): same rule as `EnableBugfixes`. Written only by `vp64helper visual-additions on|off` (`cfgini::SetOptionLine`, no side file: the player's choice, which `vp64helper off` never touches); the installer's prompt that runs it is a follow-up ticket of the installer's owner (Q4). Id 17 (MC #7273) | as `EnableBugfixes` [C 2026-11-19, unit tests] |
| `Extras` | `RoadLayout` | 0 (D212: stock for now; the maintainer may revisit) | 0 to 2 | The road and railroad layout (ticket 4934 as `FixRoadLayout` 0-1; ticket 4940, D215: renamed and widened by MC #8258/#8261, never shipped under the old name). A category per value (`Def::valueCategory`, MC #8261): 1 = the tidy layout, variant (e) of `docs/extras/roads.md` (straight centre to centre, curves of radius 22, branches merging, the ring rule), a bugfix under `EnableBugfixes`; 2 = all connections (every pair of neighbouring route plots joined, laid out by (e)'s rules; shaped by `RoadJunction` and `RoadJitter`), a visual addition under `EnableVisualAdditions` (that switch off gives 0, never 1). The server lays the roads out itself from the feed's revealed route table, in place of the EXE's route curves in the decal set; the forest's cut mask follows its roads. Needs `ServerRenderer = 1`. Id 18 (MC #7273) | `ParseHostConfig` (`server/src/render/srv_render_host.cpp`) puts the effective value and the shape into `srvextras::RoadLayoutChoiceSlot()`; the producer `server/src/extras/srv_extra_roads.*` (`RoadStyleFor`) [C 2026-11-22, unit tests: the row, its per-value states, its log lines, the installer page's radio] |
| `Extras` | `VisibleAreaOutline` | 0 (E4: not approved; the contribution guide's worked example, ticket 4938) | 0 to 1 | A visual addition (`kCatVisual`, no needed extra): a thin line on the ground along the hex edges between the plots the active team sees now and the plots out of sight, read from the fog scene's per-plot states (feed kind 3). In effect only with `EnableVisualAdditions = 1`. Id 22 (MC #7345) | the server's extras module `server/src/extras/` (being built on w5-4938, 2026-11-19) [C 2026-11-19, unit tests: the row, its states and its log line] |
| `Extras` | `FixUnitPlacement` | 0 (E4: not approved) | 0 to 1 | A bugfix (`kCatBugfix`, no needed extra): unit figures kept out of mountain and hill rock: a land unit at rest moves its formation to the flattest ground near its plot centre (at most 10 units) and each figure stands on the highest ground under its feet (terrain only by MC #8099, 2026-11-20; the wonder half and "lifted or moved" of the first text dropped). Needs `ServerUnits = 1` (checked by its consumer, one line; `needsKey` names `[Extras]` rows only). Id 23 (MC #7491, epic 0455) | `server/src/extras/units/srv_unit_placement_fix.*` (ticket 4982; registered first in `UnitExtraMakers()` on 2026-11-23, was "not yet registered" until then) [C 2026-11-20, unit tests: the row and its states; C 2026-11-23, the registration test] |
| `Extras` | `GarrisonVisible` | 0 (E4; **a test value since D243**, 2026-11-23: `Def::testOnly` = 1, so it is not in the default file and not on the installer's page, ticket 4983, MC #11877; still read and logged) | 0 to 1 | A visual addition (`kCatVisual`, no needed extra): a city's military land garrison (unit table flag garrisoned, land domain) shown at the city through the units seam, placed by look (a) (the maker's default; looks b-d by the maker's argument in tests). While `PerimeterGarrisons` is in effect it steps aside (one note line, no override). Needs `ServerUnits = 1` (its consumer's E5 line). Id 24 (MC #7491) | `server/src/extras/units/srv_unit_garrison.*` (ticket 4983; registered in `UnitExtraMakers()` by the 0455 landing, 2026-11-20) [C 2026-11-20, unit tests: the row, its states and its log lines] |
| `Extras` | `ShipsMoored` | 0 (D207: off by default) | 0 to 2 (D250: 1 look (d), 2 look (b)) | A visual addition (`kCatVisual`, no needed extra): ships in a city (or on a canal fort) drawn moored on the plot's water side, bow to the city, several side by side (the default look `MooredLook::BowToLand` until the owner picks). Needs `ServerUnits = 1` (its consumer's E5 line). Id 25 (MC #7491) | `server/src/extras/units/srv_unit_moored.*` (ticket 4984; registered by the 0455 landing, 2026-11-20) [C 2026-11-20, unit tests] |
| `Extras` | `FixAirBases` | 1 (D235 (2), after its live smoke: one-off 9081, main ea8878286, 2026-11-22; corrected 2026-11-25 by ticket 0739; it read 0 until accepted, MC #7901) | 0 to 1 | A bugfix (`kCatBugfix`, no needed extra; named `AirBases`, a visual addition, until MC #7901 of 2026-11-20): stock draws a based or carried aircraft after a load but not after a live rebase, a carrier move or a kill on its plot (the DLL's `CvPlot::updateCenterUnit` hides every non-centre unit of a plot when its units change, and an aircraft is never the centre); with this flag the server draws the active team's aircraft in every case as stock draws them after a load, at the same place. Display only, the DLL is untouched. Needs `ServerUnits = 1` (its consumer's E5 line). Id 26 (MC #7491, #7901). (Corrected 2026-11-20 by the 0455 landing; replaced "parked beside their city or on their carrier, aircraft on intercept in a holding loop", which moved to id 27 and research 4985.5.) | `server/src/extras/units/srv_unit_airbases.*` (ticket 4985; registered by the 0455 landing, 2026-11-20) [C 2026-11-20, unit tests] |
| `Extras` | `AirInterceptPatrol` | 0 (E4: not approved) | 0 to 1 | A visual addition (`kCatVisual`, no needed extra): own and team aircraft on intercept fly a holding loop in their range, with take-off and landing arcs (range from `Units.AirInterceptRange`, research extras-units.md 4986.0). Needs `ServerUnits = 1`. Id 27 (MC #8114) | the unit extras module (ticket 4986; not yet registered) [C 2026-11-20, unit tests: the row and its states] |
| `Renderer` | `ServerCombat` | 1 (MC #8627, 2026-11-20: route S on in the beta; a troubleshooting switch a tester can be asked to set to 0) | 0 to 1 | Route S's switch (ticket 4783): with `ServerUnits = 1` and the unit lines written, 1 adds `render_units_triggers=1` to server.cfg (one `shell: [Renderer] ServerCombat = 1: ...` line): the server opens its trigger stream and asks the proxy, which arms the combat hold, the sound executor and the effect player as one (`research/05-graphics-composition/unit-combat-server.md` 16); 0 = the key is not written, the question is never asked, nothing arms and the fights run on the EXE's own timing, sounds and particles, with one `shell: [Renderer] ServerCombat = 0: route S is off ...` line and no report (the player chose it). Nothing while `ServerUnits` is 0. Id 28 (MC #8627) | the proxy: `ProxyRtInt::ProxyOptions().ServerCombat()` in `proxy/src/proxy_shell.cpp` (`UnitLines`); the server gets `render_units_triggers=1` in server.cfg, not from the file [C 2026-11-20, unit tests: test_options `TestServerCombat`, test_proxy_rt's units cases] |
| `Extras` | `RoadJunction` | 0 (E4: through until the maintainer picks from the sheets; MC #10673) | 0 to 3 | A visual addition (`kCatVisual`, no needed extra: the installer's page puts an extra under another only when that one is a checkbox): the centre of a plot with four to six links under `RoadLayout = 2`: 0 through (one piece passes, the rest merge), 1 cross, 2 ring, 3 rosette (`c5r::extras::roads::Junction`). Read only while `RoadLayout = 2` is in effect. Id 30 (MC #10673) | `ParseHostConfig` -> `RoadLayoutChoiceSlot()` -> `RoadStyleFor` [C 2026-11-22, unit tests] |
| `Extras` | `RoadJitter` | 0 (E4: none until the maintainer picks from the sheets; MC #10673) | 0 to 7 | A visual addition (`kCatVisual`, no needed extra): under `RoadLayout = 2`, each plot's pass point moved by a fixed per-plot offset of up to N iso samples (`LayoutOptions::lineJitter`, roads.md 4f), so runs wobble from plot to plot. Read only while `RoadLayout = 2` is in effect. Id 31 (MC #10673) | `ParseHostConfig` -> `RoadLayoutChoiceSlot()` -> `RoadStyleFor` [C 2026-11-22, unit tests] |
| `Extras` | `PerimeterGarrisons` | 0 (D243: the owner's "Perimeter Garrisons", default off, offered by the installer's visual-additions page) | 0 to 1 | A visual addition (`kCatVisual`, no needed extra): GarrisonVisible's look (d) as the players' option: a city's military land garrison shown at the city, its living figures spread evenly in a ring along the city plot's land edges (no figure on an edge facing water), facing out; a one-plot island city: at the plot centre. Needs `ServerUnits = 1` (its consumer's E5 line). Id 32 (MC #11877) | `server/src/extras/units/srv_unit_garrison.*` (`MakePerimeterGarrisons`, registered last in `UnitExtraMakers()`) and the module's placement decorator `render/src/extras/units/garrison_perimeter.*` (ticket 4983) [C 2026-11-23, unit tests: the row, its states, the key off frame equal to the module before it, the key on frame equal to look (d)] |
| `Proxy` | `KeyboardHookThread` | 1 (ticket 0745, D255 G2: ticket 0720's behaviour, unchanged) | 0 to 1 | 1 = at a mode=on boot `ProxyKbHook::Install` (from `ProxyShell::OnBoot`) moves the EXE's `WH_KEYBOARD_LL` hook to the proxy's helper thread (import-slot thunks, Alt+Enter swallowed only with the game in front) and calls `DisableProcessWindowsGhosting` (`research/02-exe-internals/keyboard-hook-and-hangs.md` 4); 0 = Install does nothing at all: no slot written, no take-over, ghosting on, the EXE's own hook on its main thread as stock, one `kbhook: [Proxy] KeyboardHookThread = 0 (...)` line. Nothing in mode=off (Install never runs there). Id 34 | the proxy: `proxy_rt.cpp` passes the value to `ProxyKbHook::SetThreadOption` (declared locally in `proxy_rt.cpp` and `rt_tests/kbhook_unit.cpp`, defined in `proxy_kbhook.cpp`, so that the offline unit test needs no options code) right before `ProxyShell::OnBoot` [C 2026-11-25, kbhook unit `TestThreadOption`] |
| `Proxy` | `LeaderScenes` | 0 (ticket 0745, D255 G4: off, the stock behaviour, as `vpproxy.ini leader_scenes` defaulted) | 0 to 2 | 3D leader scenes for majors 22-42 (ticket 0381): 0 off, 1 on demand (the leader message hook), 2 all at load; about 32 MB of EXE address space per scene (`research/02-exe-internals/leader-scene-player-limit.md` 7.8). Precedence: `VPPROXY_LEADER_SCENES` (environment), then `vpproxy.ini leader_scenes` (both test switches, kept), then this key; the log's `leaderscene:` line names which decided. With the 22-major DLL nothing happens. Id 35 | the proxy: `proxy_leaderscene.cpp` `DecideOnce` (reads `ProxyRtInt::ProxyOptions()` when `IniValue` finds no `leader_scenes` line and the environment variable is unset) [C 2026-11-25; the offline unit covers DecideMode/IniValue, the options branch is product code, not unit-tested] |
| `Performance` | `RenderFrameJobs` | -1 (ticket 0745, D255 G3: auto, the server's own default since 0711) | -1 to 8 | The frame-jobs pool of the server's renderer (ticket 0711, `server/src/render/srv_frame_jobs.*`): -1 = auto (2 workers on more than 2 logical cores, else 0); 0 = no worker (every job inline); 1-8 workers. The proxy adds `render_frame_jobs=N` to server.cfg for 0..8 only when the boot writes the render lines (`render=1`); for -1 it writes nothing, so server.cfg is byte-equal to the one before the key and the server's `ParseFrameJobsKey("auto")` decides. A test's `server_cfg_extra` comes later in server.cfg and wins. Id 36 | the proxy: `RenderFrameJobsLine` in `proxy_rt.cpp` (around `ProxyShell::Configure`), one `options: [Performance] RenderFrameJobs = ...` line; the server reads only the server.cfg key (it does not use `SrvOptions()` for it) [C 2026-11-25] |
| `Extras` | `FixSmoothWheel` | 1 (ticket 0755, D255, D169: a bugfix; proposed by the ticket, **the owner confirms at the merge**, `kApprovedDefaults`) | 0 to 1 | A bugfix (`kCatBugfix`, no needed extra): the proxy's `PeekMessageW` import-slot thunk sums `WM_MOUSEWHEEL` deltas into whole notches and hands a merged multi-notch message out one notch per main-loop call (research/02 camera-and-picking.md 11.4, 11.6). 0 or `EnableBugfixes = 0` = the slot is never written (stock). Id 37 | the proxy: `ProxyWheel::Install` from `proxy_rt.cpp` right after `ProxyShell::OnBoot` (mode=on only), one `wheel: ...` line; the server does not read it [C 2026-11-25] |

Adding an option: an id in `vp64_options.h`, a row in the table in `vp64_options.cpp` (its help text becomes the
comment in new files), a section in `docs/OPTIONS.md` (the unit test fails when the document lacks `Key = default` or
`[Section]`), and its consumer reads `SrvOptions()` (server) or `ProxyRtInt::ProxyOptions()` (proxy).

## 3. Verification (2026-11-13)

- Unit tests `options/tests/test_options.cpp`, 126 checks, every validation and error path with its negative case
  (values and warning texts checked): pass in the x86 tree (clang-cl -m32, VC9 headers, msvcr90; `python ipc/build.py
  --target test_options`) and the x64 tree (MSVC 14.4x), and under MSVC `/fsanitize=address` (`options/test_asan.py`:
  canary caught, 126 pass, peak 64 MB) [C]. The suite fails when it should: run against `README.md` instead of
  `docs/OPTIONS.md` it reports 2 failures and exits 1; without arguments it exits 2 [C].
- Proxy and server build and link with the wiring (section 5) [C]. The real `server.exe` (direct mode, no game folder,
  so it stops at its boot phase right after the options lines) read an `options_file=` in a folder with a Cyrillic name:
  `BakedTileCameraRadius = 5 ; less memory` gave 5 "from the file", `NoSuchOption = 1` one warning; without
  `options_file` and with a missing file it logged the default -1 [C, headless, 2026-11-13 00:48].
- Not run: a proxy boot. The file's creation in a Documents folder, the proxy's `options:` lines and the server.cfg
  line written by the proxy are covered by the unit tests of the loader and by reading the code, not by a run [I]. The
  first live proxy run on an instance after the merge shows them in `vpproxy.log` and `server.log` (`options:` lines),
  and `VP64Options.ini` appears in the instance's redirected `Documents\My Games\Sid Meier's Civilization 5\`.
- 2026-11-14 (issue 0185, D136): `WhiteFogMask` default 0 -> 1. `test_options` 164 checks (160 before: the default,
  an empty file and a file with only `ServerRenderer` give 1, `= 0` gives 0, `off` added to the bad values, the new
  file's comment `; WhiteFogMask = 1` and its help text "1 = on (default)") pass in the x86 and x64 trees and under
  ASan (canary caught, peak 65 MB) [C].
- 2026-11-17 (issue 4759, epic 0040): `[Renderer] ServerUnits` added, default 0. The proxy's glue call
  (`ProxyUiBridgeRt::RequestUnitPositions`, in `proxy_ui_bridge_rt.cpp`) is reached from `proxy_shell.cpp` through
  `/alternatename` with a default in `ProxyShellDefault` that only logs, so programs linking `proxy_shell.cpp` without the
  glue (the offline `shell_unit` test) still link; the proxy links the real one. With ServerUnits = 1 and no render lines
  (ServerRenderer = 0, `server_config=`, the composite refused) the glue is not asked either: a tester with
  `server_config=` uses `VPPROXY_TEST_UI_POSITIONS=1` (24.6). Results, 2026-11-17 08:14-08:3x, run script
  `C:/c5-64bit-work/0040/4759/run_4759.sh` (not committed) [C]:
  - `test_options` 183 checks (164 before; `TestServerUnits`: the default 0, 0 and 1 accepted, `2 -1 on off yes 1.0 ""`
    refused with "not a whole number from 0 to 1; the default 0 is used", the new file's `; ServerUnits = 0` and its help
    text, docs) pass in the x86 tree and the x64 tree (`python ipc/build.py --arch both --target test_options`); against
    `README.md` it fails with 5 failures, exit 1 (the docs check names ServerUnits); `options/test_asan.py`: canary
    caught, 183 pass, peak 66 MB.
  - `proxy/tools/test_proxy_rt.py --only-units --asan --units-baseline 372823cad`: 90/90. A program generated by the
    test (no source file in the repository) calls `Configure` for ServerRenderer 0/1 x ServerUnits 0/1 x server_config= no/yes
    beside an accepted composite, a missing one and the test switch `crashlogs\shell.helper`: unit keys and one glue
    request only with the render lines, otherwise the lines of ServerUnits = 0 and one line naming the reason; built
    without the glue, `/alternatename`'s default logs it; the `proxy_shell.cpp` of 372823cad (before 4759) gives
    byte-equal lines with ServerUnits = 0 in all 12 cases and none with 1 (negative); once under MSVC ASan (x86).
  - The same with `--only-shell --asan --baseline-build` (real proxy built from this branch, proxy SHA-256 c7e3cc05...,
    and from 372823cad, 891d4161..., both release on the fork at 04b1211b8; loader.exe on a hidden desktop, the real
    server): `units-on` server.cfg has the three keys once each, vpproxy.log has the glue's own line `ui bridge: the unit
    option asks for the unit positions (issue 4754)` once and the server accepted the keys; `units-norender` none, one
    `shell:` line naming ServerRenderer = 0; `units-off` server.cfg equals a file without the key and the baseline
    build's for the same file, byte for byte after the layout path, pid and run folder (negative: the baseline build
    writes no unit key with ServerUnits = 1). Whole section 5 with the units section and ASan (2026-11-17 08:28-08:30):
    `test_proxy_rt: 132/132 checks passed (111 s)`.
  - Lesson: vpproxy.log names every option once in an `options: [Renderer] Key = value (from the file)` line, so a test
    that counts a key's mentions must count only its consumer's `shell:` lines (the first run failed 2 of 133 on this).
- 2026-11-17 17:10 (epic 0040's merge of main after ticket 0487; lead-0040, MC #4579): `ServerUnits` renumbered from
  id 3 to **id 4** (`kServerUnits = 4`), after `UiDrawListGrowth` = 3; its `kDefs` row follows 0487's, and
  `docs/OPTIONS.md` and the table above list the two in id order. The default file text now holds five options, so the
  tests' `DefaultFileText` buffers are 8192 bytes (4096 was too small once both options were in). Results on the merged
  head 17175d444 (git archive; script `C:/c5-64bit-work/0040/lead4/tests_head.sh`, not committed) [C]: `test_options`
  202 passed, 0 failed in the x86 and x64 trees; `options/test_asan.py` OK (canary caught);
  `test_proxy_rt.py --only-units --asan` 72/72 and `--only-uidraw --asan` 9/9; `proxy/tests/ui_bridge/test_ui_bridge.py`
  9/9; release server.exe and proxy on the DLL pin 4f4daac70 (server.exe 3ba49ffe..., proxy 6c0d4683...). The bridge
  installs now for three reasons: the whole bridge (`ui_bridge=1`), the unit positions (4754, `ServerUnits = 1`) or 0487's
  frame hook alone ("drain only"); with the positions and the hook both on (the product with `ServerUnits = 1`) it is the
  positions-only install, which carries the drain and so the hook.
- Test hygiene: `run_proxy_game.py`, `test_proxy_rt.py` (loader and guard cases) and `hutest` set `VP64_OPTIONS` into
  their run folder, so an offline proxy test never writes into the real Documents (they run without the shim).

## 4. Census of the existing settings (2026-11-13), with proposals

Proposals only; the MC decides. "Keep" = stays a test switch where it is, not in `VP64Options.ini`.

### 4.1 `vpproxy.ini` (next to the proxy DLL; `proxy/README.md`)

| Key | Proposal | Reason |
|---|---|---|
| `mode` on/off | Keep in `vpproxy.ini` | Decides whether the patch runs at all; install-level (an installer or uninstaller flips it next to the DLL it concerns). A player-facing "turn the 64-bit route off" switch could later be an option, but it must be read before the server starts, which the proxy can do either way |
| `fallback_dll`, `x64_dir`, `server_exe`, `server_config`, `run_root`, `log`, `game_dir`, `loc_db`, `db` | Keep | The D28 layout and test overrides; a player never needs them |
| `keep_runs` (10) | Keep as a test switch; *done 2026-11-25 (ticket 0747, D255 G5): `[Logs] KeepRuns` (section 10.5); set in `VP64Options.ini` it wins over `keep_runs`* | Was "Keep for now; move to `[Logs] KeepRuns` only if players ask" until 0746 measured the disk use (10.4) |
| `dll_logs` | Keep | Test switch. The stock game's logging follows `config.ini LoggingEnabled`; the server should follow the player's `config.ini` rather than a new option. *Done 2026-11-13 (issue 0117): the proxy's `ServerConfigText` writes `dll_logs=1` when the game's `config.ini [Debug] LoggingEnabled = 1` (or `dll_logs=1` here)* |
| `server_allow`, `server_args`, `on_server_death`, `strict`, `strict_allow`, `host`, `test_slot_poll` | Keep | Harness and strictness switches |
| `connect_timeout_ms` (60000) | Keep | Only a very slow disk would need more; revisit if a player report shows it |
| `reply_spin_us`, `idle_spin_us` | Keep until measured | CPU use against latency (laptops); a candidate for a player option only with the performance team's numbers (D40) |
| `counters` (1) | Keep | Diagnostics; whether the shipped default should be 0 is a performance question, not an options question |
| `local_answers`, `local_verify`, `local_test`, `keep_temp`, `lua_mirror` | Keep | Correctness switches of the local-answer mirror; reference and negative-test modes |

### 4.2 Marker files under `<install>\crashlogs\` (DLL lineage instruments; `research/06-tooling-and-workflows/instrumentation-in-the-dll.md`)

| Marker | Proposal | Reason |
|---|---|---|
| `allocTrace.enabled`, `queueguard.capture`, `uilockwaits.enabled`, `nodumps.please`, `nopopups.please` | Keep | Diagnostics of the 32-bit lineage; not for players |
| `shell.helper` | Keep as a test switch; one-off 9040 is moving the loading of the shell helper into the proxy (in flight on 2026-11-13, not on `main`). When the compositor ships it loads unconditionally from the patch's own folder, with no marker | A path to a DLL is not a player setting |
| `viewmask.enabled` (and `whitemask=1` inside it) | Keep as a test switch until the server renderer ships (9040 writes the view mask from the proxy). Then, if a "server terrain off" fallback is wanted for troubleshooting, it becomes an option `[Renderer] ServerTerrain = 1`, not a marker | Turning the server's terrain off is a troubleshooting choice a player could make; the mask bits themselves are not |

### 4.3 `server.cfg` (written by the proxy or `run_server.py`; `server/README.md`)

All keys stay internal: the file is regenerated at every boot. Two are worth a note:
- `language` (default `en_US`): the proxy does not pass the player's game language, so the server's localization runs in
  English whatever the game's language. It should follow the EXE's language automatically, not an option (follow-up
  for the MC: one-line fix in `ServerConfigText` once the language source in the EXE is known). *Done 2026-11-13 (issue
  0117): the source is the game's own `Documents\My Games\Sid Meier's Civilization 5\config.ini`, `[User Settings]
  Language = en_US` ("The currently selected language"; `SteamLanguage` and `AudioLanguage` are other keys) [C file; I
  that the EXE uses it as its current language: comment of the key, not traced in the EXE]. `ServerConfigText` reads it
  through `SHGetFolderPathW(CSIDL_PERSONAL)` (the call the EXE makes; a test instance's shim redirects it) and writes
  `language=ll_CC` when the value has that form, plus a comment line naming the file. Caveat: the server uses `language`
  only when `loc_db` is set; the proxy's default `loc_db` was "" (no server localization at all), so a shipped layout
  needed `loc_db` as well before the language shows (not part of 0117). Unit test `proxy/tools/rt_tests/unit.cpp`
  `TestGameConfig` (7 checks, test seam `VPPROXY_TEST_CONFIG_INI`); 4 fail on the old code.*
  *Bug 0123 (2026-11-13, replaces the caveat's "default ''"): without a `loc_db` key the proxy now writes
  `loc_db=<config.ini's folder>\cache\Localization-Merged.db` when that file exists (the database the EXE builds for the
  player's language and active mods), else `loc_db=` with a comment naming the path tried; `loc_db=none` (or an empty
  value) is an explicit "no server localization". Unit test `TestLocDbDefault` (5 checks; 3 fail on the old code) [C].
  Live 2026-11-13 08:10-08:17 [L] (c5h-a, proxy route, the team branch's build, huge t350, the `loc_db` line removed
  from vpproxy.ini): server.cfg got the instance's own cache database and `language=en_US`; the server attached it as
  `Localization` and the DLL's lookups returned English text (`research/03-dll-engine-boundary/evidence/4000/0122-0123/`).
  Not shown: a game language other than en_US. The provisioned instances set `loc_db` to the same file, so they behave
  as before.*
- `options_file` (new, 9042): the path of `VP64Options.ini`; `run_server.py --options INI` sets it for direct and
  remote test runs.

### 4.4 Environment variables

`VP64_OPTIONS` (new, 9042: the options file's path) is the only player-facing one. All others stay test-only:
`VPPROXY_INI`, `CIVBG_GUARD`, `VP_INSTANCE`, `VP_LUAEXEC`, `VP_ALLOCTRACE*`, `VP_QUEUEGUARD*`, `VPSHELL_LOG`,
`VPSHELL_EXE_BASE`, `VPSHELL_BAKE_TEST`, `VPSHELL_BAKE_MANIFEST`, `C5R_*` (render tests), `C5_*` (harness shim),
`HUTEST_*`, `VPPROXY_TEST_*`, `VP_SERVER_TEST_*`, `C5_GUARD_TEST_FAIL`, `CV64_STUBLOG`.

## 5. Build of the wired proxy and server

2026-11-13 00:47: `python server/build_server.py --src D:/c5-64bit-work/9036/src-76992d23 --deps
D:/c5-64bit-work/9036/b1/deps --out D:/c5-64bit-work/9042/b1 --config release --offline --jobs 4` (fork at the pin
76992d235): `build_proxy: OK` (proxy SHA-256 411f303a3494df1cc4e7900cf84c0d57c556e04bbf059dd13f1781ef29cc72df, 879,616
bytes; imports unchanged: KERNEL32, MSVCR90, MSVCP90, lua51_Win32, CvGameDatabaseWin32Final Release), `build_server: OK`
in 67 s [C]. `proxy/tools/test_strictness.py` 45/45 after the harness edits.

## 6. Lessons (time lost, 2026-11-13)

- clang-cl 19 `/W4 /WX` rejects `(Fn)GetProcAddress(...)` with `-Wcast-function-type-mismatch`; cast through `void*`.
- `quietlock.py run -- <exe>` needs an absolute Windows path for a binary (a relative path with forward slashes gives
  `FileNotFoundError` from CreateProcess). In Git Bash, `$(pwd -W)/build/...` works.
- A Python script that runs `cl` with a vcvars environment passed as `env=` must resolve `cl` with
  `shutil.which('cl', path=env['PATH'])`: CreateProcess searches the caller's PATH, not the child's.

Ticket 4931 (2026-11-19, W3 of team-0454):

- `Def` gained two trailing fields (`category`, `needsKey`). clang-cl `/W4` (= `-Wall -Wextra`, used by the CMake test
  trees and by `server/build_server.py` for `options/src`) rejects the older rows that leave them out
  (`-Wmissing-field-initializers`, 20 errors); a `#pragma clang diagnostic` push/ignored/pop around `kDefs` keeps
  every older row unchanged [C, build log 2026-11-19].
- The default file text was 7931 bytes with the two switches, and 8019 bytes once 0566's `AiUpdateTickMs` help was
  merged in [C: `test_options` prints it, 2026-11-19]: past the old test bound `n < 8000`, with 12 buffers of 8192.
  All are 32768 now, the bound 30000.
- **A bug in `vp64helper on` found and fixed [C: `cfgini_unit` 300/305 on branch w3-4931 before the fix, 308/308
  after; the 4931 changes before the fix did not touch that scan, so it was red before 4931 too, I]:** the
  commented-default scan of `proxy/helper/proxy_cfgini.cpp` took any comment line that began `ServerRenderer =`, the
  last one winning. The default file's `ServerPath` help (ticket 0300 part 2) has the line `; ServerRenderer = 1).`
  after `; ServerRenderer = 0`, so `on` replaced that help line, not the option's commented line. The file still
  parsed as 1 and `off` gave the bytes back (no player harm), but `cfgini_unit`'s default-file cases failed (5 of 305)
  whenever they were run against the current default text. Since 4931 only `; Key = <whole number>` with nothing after
  it counts. When `test_cfgini.py` last passed before this is not known [I: the help text came with 0300 part 2].

## 7. A deleted file in a D33 session, the release's option set, the renderer guard (ticket 0624, 2026-11-20)

Found by W31 of epic 0042 (read only, 2026-11-20); reproduced and fixed by ticket 0624 (branch
`task-0624-options-default-guard`).

### 7.1 The gap, reproduced offline [C: the old code run, 2026-11-20 19:3x EST]

`proxy/tests/options_boot/test_options_boot.py` builds main's `proxy_options.cpp` of 7682d4511 (before the fix) into
the test program's `VP_OPTIONS_BOOT_OLD` form and runs it. Its output, verbatim:

```
old: created: yes, bytes equal to DefaultFileText: yes
old: ServerRenderer=0 ServerUnits=0 ServerCities=0 ServerLandmarks=0 HiddenEmbarks=0 AiSkipVisualDrain=0 AiDrainUnordered=0 AiUpdateTickMs=0
old: LOUD lines: 0; lines naming the release set: 0
old: log| options: ...\VP64Options.ini (named by VP64_OPTIONS) did not exist: created with every option at its default
old: older file: ServerRenderer=1 ServerUnits=0 HiddenEmbarks=0 AiSkipVisualDrain=0; lines naming a missing key: 0
```

- **A deleted file** (what `docs/OPTIONS.md` told players to do to reset): the proxy re-creates it with every option
  commented at the table's default, so `ServerRenderer = 0` and every server feature of the release (`ServerUnits`,
  `ServerCities`, `ServerLandmarks`, `HiddenEmbarks`, `AiSkipVisualDrain`, `AiDrainUnordered`; the release's
  `AiUpdateTickMs = 0` equals the table's default 0) at 0, with one plain log line and no LOUD line. The old proxy has no
  notion of the session there: config.ini keeps `vp64helper on`'s `[DisableSystems]` (D33) form, so the game's own
  D33 systems stay off and `ProxyShell::Configure` writes no render lines. The code path is [C]; what the player sees
  is 7.4's live run: not a world without terrain, as the ticket and installer-notes 9 expected, but a degraded one.
- **An existing file that lacks keys** (an older release's file after an update by hand, or lines commented out): each
  missing key takes the table's default without a word. The installer writes the release's set into the file before
  `vp64helper on` (ticket 0574), so an installer update does not leave this state; a hand update does.

### 7.2 The fix [C: code; L: the offline suite]

- `ProxyRtInt::OptionsBoot(x64Dir, d33, sessionWhy)` (`proxy/src/proxy_options.cpp`). `Boot` (`proxy_rt.cpp`) passes
  the session's kind from `ProxySession::SessionKind(ReadMasks())`.
  - Stock session: unchanged (the commented defaults).
  - D33 session, the file missing, `<x64_dir>\release-options.txt` usable: the file is created (`CREATE_NEW`) as the
    default text with each set key's commented default line `; Key = d` replaced by `Key = v`, and `ServerRenderer = 1`
    the same way (`ProxyOptions::TextWithSet`; only the commented default line, which is followed by an empty line or the
    end, never a help line such as ServerPath's `; ServerRenderer = 1).`). One line, LOUD because a missing file in a D33
    session is not normal: `options: LOUD: <path> (...) did not exist in a D33 session (...): re-created with the
    release's option set <set path> (AiDrainUnordered = 1, ...) and ServerRenderer = 1, the other options at their
    defaults (ticket 0624)`.
  - D33 session, the file missing, the set missing or unusable: the file is NOT created (a later start with the set
    in place, for example after a reinstall, re-creates it properly), one LOUD line says why, and the guard refuses.
  - D33 session, the file exists, the set usable: the file is never rewritten; one `options: note:` line names each set
    key the file does not set whose default differs from the release's value.
- The set's format is 0574's (`Section<TAB>Key<TAB>Value`, `#` comments, BOM, LF or CRLF), parsed by
  `ProxyOptions::ParseSet` with install.iss's refusals (ServerRenderer, a key twice, empty, a bad row) and, beyond
  install.iss (W31's point), a key not in this build's table, a key outside its home section and a value out of range.
  A file over 64 KB or unreadable is unusable.
- **The renderer guard, its one place:** `Boot()` in `proxy/src/proxy_rt.cpp`, right after `OptionsBoot`, calls
  `ProxyRtInt::OptionsGuard(d33, why, writesServerCfg)`. A D33 session with `ServerRenderer = 0` in force (set in the
  file by hand, the table's default of a file that does not set it, or the file not created) is a boot failure:
  `SetFail("server-start", "options-renderer-off", ...)`, a player summary in `g_failSummary`, `boot FAILED: ...`,
  `return NULL`. With `pack=1` (every shipped layout) the existing D33 route follows: `boot: NO PLAY`, the D118 report,
  `vp64helper failed --pid` (config.ini back to the player's values, the options file as `off` leaves it, `failed=1`,
  the popup with "Try again next start"), `Fatal(3)`. No new popup code. With `pack=0` (development layouts) the boot
  returns NULL as every other boot failure there does.
- **Test hooks that skip the guard**, each with one LOUD line `options: LOUD: TEST HOOK: the renderer guard was skipped
  (ServerRenderer = 0 in a D33 session): <hook> (ticket 0624)` (MC #8701): `vpproxy.ini server_config=` (the test's own
  server config decides the render keys) and the file `crashlogs\shell.helper` under the game's working folder (the
  composite's test switch, honoured at the boot with ServerRenderer = 0). Can a player set either by accident? Not by
  any shipped tool [I, from the code and the installer's file list]: the installer writes neither, `vpproxy.ini` is the
  installer's file beside the DLL and `server_config=` is a test key that no document for players names; `crashlogs\` is
  the game install's own folder and `shell.helper` is a file only the harness writes (`research/06-tooling-and-workflows/
  instrumentation-in-the-dll.md`). A player would have to create one of them by hand; the LOUD line then names it in the
  report's log tail.
- **The composite guard (ticket 0624 item 5, MC #8762, 2026-11-20 20:4x; replaces the first hand-back's "not covered by
  this guard (a finding for the MC, not changed)").** A D33 session with `ServerRenderer = 1` whose product composite is
  refused (`shell: ... composite ... REFUSED`, for example a partial or damaged install) gives the same degraded world:
  `ConfigureRender` writes no render lines, the view-mask write keeps only the config rule, and before item 5 the game
  went on [C: `proxy_shell.cpp` `ConfigureRender`; the picture not checked live, I]. Now `ConfigureRender` keeps the
  refusal's text (`ProxyShell::RefusedComposite()`, "composite <path> REFUSED: <reason> (<detail>)"; `CheckComposite`
  itself unchanged), and `Boot()` calls `ProxyRtInt::CompositeGuard(d33, why, writesServerCfg, RefusedComposite())`
  right after `ProxyShell::Configure`: a refusal is `SetFail("server-start", "composite-refused", ...)` with a player
  summary that names the fix ("The patch's installation is partial or damaged: run the patch's installer again"), then
  the same D125 route. A stock session with a refused composite goes on as before (the game draws its own world); the
  test switch `crashlogs\shell.helper` never refuses (no check), and `server_config=` skips the guard with one LOUD line
  `options: LOUD: TEST HOOK: the composite guard was skipped (composite ... REFUSED: ... in a D33 session): ...`.
  So the guards sit in two places of `Boot()`, one after the other: the renderer guard after `OptionsBoot`, the
  composite guard after `Configure` (lead-0042's yes #8779 for the hunks).
- **Modpack form, live [L, 2026-11-21 12:31-13:26 EST, ticket 0645, c5h-new-mpk, build 26577625]:** cases (i)-(iii)
  pass, details in 7.6b. The TEST installer was built from main fca9c481d at the pins 1b87dd185, std, i3 with 0626's
  set, SHA-256 `cea9415e...`.

### 7.3 Tests [L, 2026-11-20, branch task-0624-options-default-guard]

- `python -I proxy/tests/options_boot/test_options_boot.py --scratch C:/c5-64bit-work/0624/off2` through
  `quietlock.py run` (19:3x EST): **13/13**: the old code's reproduction (2/2, 7.1); the new code with the proxy's
  toolchain **53/53** (set parser and 16 refusals, the created file line for line against the default text with only
  the 8 commented default lines changed, the guard's rule over all 16 combinations, OptionsBoot + OptionsGuard end to
  end: created-with-set, created-without-set and a bad set (refused, file not created), a stock session byte-equal to
  the default text, an existing file that lacks keys (bytes kept, one note naming HiddenEmbarks, AiSkipVisualDrain,
  AiDrainUnordered), a hand-edited `ServerRenderer = 0`, a file without the key, both hooks with exactly one LOUD line
  and the marker's negative, a stock session with 0); `--expect-fail` exit 1 with one more FAIL line; the same 53/53
  under MSVC x86 `/fsanitize=address` (canary caught).
- `proxy/tools/test_proxy_session.py`: its D33 loader cases now get `[Renderer] ServerRenderer = 1` in the options file
  (what `vp64helper on` gives the product; otherwise the guard refuses them first), and two new cases: a D33 session
  with a fresh file and no `release-options.txt` (refused: NO PLAY, exit 3, the report with stage server-start, code
  options-renderer-off) and the same with the set (the file re-created with it, the 64-bit boot goes on). Run with this
  branch's proxy (`build_proxy.py` at the pin 1fd51353e, SHA-256 `2270b905...`, 19:43) and the server and x64 DLL of
  the layout `x64-1fd51353-p1e09fc7` (same schema hash `0x765d76dcdc4e55b2`), through `quietlock.py run`:
  **`test_proxy_session: 155/155 checks passed`** (19:4x) and **`test_proxy_rt: 203/203 checks passed`** (19:5x).
  Again on the branch head after the wording change and a merge of main (proxy `d201bd70...`, 20:10): offline 13/13,
  `test_proxy_session: 155/155` (20:15), `test_proxy_rt: 203/203` (20:19).
- Item 5 (the composite guard), on the head with it (proxy `9e1657fb...`, 20:41), each through `quietlock.py run`:
  `test_options_boot` 13/13 with **62/62** checks plain and under ASan (the rule over 32 combinations, its reason text,
  `CompositeGuard` with a refusal, an accepted composite, NULL, a stock session, both hooks with one LOUD line each, and
  ServerRenderer = 0 left to the renderer guard); **`test_proxy_session: 165/165`** (20:43): `composite-refused-d33`
  (NO PLAY, exit 3, the report with code composite-refused, the helper's `failed`, the summary "run the patch's
  installer again"), `composite-ok-d33` and `composite-refused-stock` (negatives: boot, no report); every D33 loader
  case now carries an accepted stand-in composite (`rt_tests/composite_standin.cpp` with a `shell_manifest.py` manifest
  naming the proxy), as the installer lays one; **`test_proxy_rt: 205/205`** (20:45): `shell-missing-d33` (pack=0:
  boot FAILED naming the refusal, DllGetGameContext NULL, no server) and `shell-missing-d33-hook` (server_config=: one
  LOUD TEST HOOK line).

### 7.4 The gap live, on an installer-laid layout [L, 2026-11-20 19:52-20:06 EST, c5h-f]

MC-B's grant (window to 21:30); c5h-f (F:, profile mods548, EXE stamp 0x546CD0A8, `provision.py verify --quick` ok);
script `C:/c5-64bit-work/0624/live/repro.sh` (not committed), evidence in `C:/c5-64bit-work/0624/live/ev/`.

1. The player's state as 0574's install check made it (`set-dll` to rc0b's player 32-bit DLL); VP64Options.ini
   (`9497f85b...`), config.ini (`b494c28f...`) and the MODS DLL (`3c2f47da...`) backed up by SHA-256.
2. rc0b's TEST installer (`VP64-Patch-0.1b-rc0b-for-VP5.4.8-TEST.exe`, configuration i3) installed, exit 0: the
   payload's `MODS\(1) Community Patch\VP64\release-options.txt` (the seven rows of 4.3's set and one `#` comment line;
   so the proxy's `x64_dir`, `<proxy dir>\VP64`, is where the set lies) written into VP64Options.ini, then
   `ServerRenderer = 1` by `vp64helper on`; config.ini in the D33 form.
3. VP64Options.ini deleted (19:56:54); the save `C5H-0366-TINY_0008` loaded through the patched front end; in game at
   20:00:38.
4. rc0b's proxy (the old code) wrote: `options: ...\VP64Options.ini (in the Documents folder) did not exist: created
   with every option at its default`, `ServerRenderer = 0 (default)`, `ServerUnits = 0`, `HiddenEmbarks = 0`,
   `AiSkipVisualDrain = 0`, `AiDrainUnordered = 0`, `AiUpdateTickMs = -1` (rc0b's table default; main's is 0), then
   `shell: [Renderer] ServerRenderer = 0: no composite, no view-mask write, no render keys`; no LOUD line; server.cfg
   without any render key; config mask `0xffec3f67` (D33). The game played on.
5. **What the player sees** (Visual Checks, `--issue 0624`, DIAGNOSTIC): **the game draws its own terrain, water,
   rivers, hills and mountains** (Terrain is no D33 key and, with ServerRenderer = 0, the proxy writes no view mask),
   but the D33 systems are off: the unexplored tiles show as flat gray (no fog-of-war clouds), no forests, no decals or
   yield icons, and none of the server's features (units by the server, hidden embarks, the AI pacing options).
   **Correction (2026-11-20, this run):** the expectation "a world without terrain" (ticket 0624's title, W31's read,
   installer-notes 9 and `docs/OPTIONS.md`'s "nobody would draw the terrain") is wrong for the 64-bit path; the dark
   red map without terrain is the 32-bit DLL in a D33 config.ini (installer-notes 8). The state is still a broken,
   unsupported look, so the guard stays.
6. Stopped 20:04 (launcher stop with `tools.harness.turns:stop_readiness`, no box). rc0b removed, exit 0; the removal
   kept its record and the backup of VP64Options.ini because the file had changed ("NOTE: the install record and the
   kept backups stay"), as designed for a file the player changed: that leftover `VP64\install` (not there before) was
   copied to the evidence folder and removed. VP64Options.ini, config.ini and the MODS DLL back by SHA-256, `set-dll
   --original`, `verify --quick` ok, front end restored, claim released 20:06. My Games against the listing before:
   only run outputs differ (logs, caches, the save's autosaves).

The fix's own live check (acceptance 4: the world drawn and the release's features on after a delete, on build 26577625,
both forms) needs a proxy that matches a 26577625 server, so it follows ticket 0616's merge.

### 7.4b The item-5 state live: a refused composite in a D33 session, without the guard [L, 2026-11-20 22:11-22:32 EST, c5h-f]

MC-B's grant (P1, after lead-0615's rule A slot); c5h-f (old build 0x546CD0A8); script
`C:/c5-64bit-work/0624/live2/p1.sh` (not committed). rc0b's TEST installer laid as in 7.4 (`ServerRenderer = 1` by
`vp64helper on`, config.ini D33), then `MODS\(1) Community Patch\shell_composite.dll` moved out (copied and checked by
SHA-256 `2990e3f7...` first); `C5H-0366-TINY_0008` loaded with rc0b's proxy, which has no composite guard. The log:
`ServerRenderer = 1 (from the file)`, `composite ... REFUSED: shell_composite.dll is missing; the game draws its own
terrain`, `ServerUnits = 1: no unit keys ... the composite was refused`, the view mask written with config.ini's D33
mask (`view 0xffffffff -> 0xffec3f67`), no render key in server.cfg. **The picture is the same as 7.4's**: the EXE's
terrain, water and relief drawn, the unexplored tiles flat gray (no fog-of-war clouds), no forests, none of the
server's drawing (Visual Checks, DIAGNOSTIC, issue 0624). This confirms 7.2's [I] for item 5. The game was stopped at
22:27 with no box. The compositor was put back by SHA-256 and rc0b removed (exit 0, no leftover). VP64Options.ini,
config.ini and the MODS DLL were restored by SHA-256, `verify --quick` ok, and the claim released at 22:32.

### 7.6 The fix live on build 26577625, mods form, installer-laid [L, 2026-11-20 23:29 - 2026-11-21 00:16 EST, c5h-new-a]

MC #8839's route. The TEST installer was built from this branch: main a7162f560 plus 0624, the shared pins 1b87dd185,
proxy `41d610b4...`, the compositor from `shell/build.cmd`, and purity derived for the DLL (162/162 kept). It came
from a take-shaped work folder, as in 0620 (`C:/c5-64bit-work/0624/inst/make_work.py`, not committed). The installer is
`VP64-Patch-0624-guards-test-for-VP5.4.8-TEST.exe` (`4e165812...`, i3, 0626's option set, std only). Instance
c5h-new-a: F:, EXE build 26577625 (stamp <stamp>, MC-B 2026-11-21: not written), mods548, `verify --quick` ok, the tag's 32-bit DLL set first. Script
`C:/c5-64bit-work/0624/newa/cases.sh` (not committed). The installed state was snapshotted by SHA-256 and restored
after each case. F: read latency was at or below 0.5 ms at every launch. One DIAGNOSTIC Visual Check per playing case.

| Case | Launch | What was done | Result |
|---|---|---|---|
| (i) positive | 23:39-23:46 | the untouched installed session, `C5H-4773-TINY_0018`, 3 turns (18 -> 21) | **PASS**: `ServerRenderer = 1 (from the file)`, composite accepted, 8 render keys in server.cfg, **0 LOUD** lines in vpproxy.log and server.log; neither guard refused a good session |
| (ii) file deleted | 23:49-23:56 | VP64Options.ini deleted, 1 turn played | **PASS**: one LOUD line `... did not exist in a D33 session (...): re-created with the release's option set <...>\VP64\release-options.txt (AiDrainUnordered = 1, ..., ServerUnits = 1) and ServerRenderer = 1 ...`; read with 0 warnings; the same 8 render keys; no other LOUD line |
| (iii) ServerRenderer = 0 by hand | 23:58-23:59 | `ServerRenderer = 1` changed to `0` | **PASS**: `boot FAILED: VP64Options.ini gives [Renderer] ServerRenderer = 0 (set in the file) in a D33 session ...`, `boot: NO PLAY`, report with stage server-start and code options-renderer-off, then `vp64helper failed` and the popup (text below) |
| (iv) composite missing | 00:03-00:05 | `shell_composite.dll` renamed | **PASS**: `composite ... REFUSED: shell_composite.dll is missing`, then `boot FAILED: [Renderer] ServerRenderer = 1 in a D33 session ...`, NO PLAY, report code composite-refused, the popup |
| (iv b) composite of another version | 00:10-00:11 | rc0b's compositor (`2990e3f7...`) in its place | **PASS**: `REFUSED: the SHA-256 of shell_composite.dll differs from the manifest`, the same NO PLAY, report and popup |

The popup as the player sees it, case (iii) (class `VP64HelperPopup`, title "Vox Populi 64-bit patch: error report",
buttons Open folder / Try again next start / Close): "The 64-bit part of the patch failed, so the game was ended. Your
config.ini is back to your own settings and the patch is switched off: the next start is the normal game. "Try again
next start" switches the patch on again. // The 64-bit part of the patch failed (server-start: options-renderer-off):
the patch is switched on, but its options file VP64Options.ini says ServerRenderer = 0 (or was deleted and could not be
re-created with the release's options), so the game would show a degraded world (no fog of war, no forests). The game
was ended; the next start is the stock game until the patch is switched on again. // Report: <folder> // Hand it in as
a new issue ...". Case (iv)'s second paragraph: "... (server-start: composite-refused): the patch's compositor
(shell_composite.dll beside the patch's DLL) is missing, damaged or from another version, so the game would show a
degraded world. The patch's installation is partial or damaged: run the patch's installer again. ...". Each popup was
closed by the claim's owner after its game had ended (`dismiss --button Close --patch-popup`, ticket 0479).

Clean-up (00:13-00:16): the TEST installer was removed (exit 0). VP64Options.ini (`072b5b70...`), config.ini
(`7cf73442...`) and the MODS DLL (`ff0f6e86...`) were put back by SHA-256. The `VP64\reports` folder that the refused
cases created was removed after it had been copied to the evidence: the installer's removal leaves it, as it leaves
any report. `verify --quick` ok, leftovers clean, claim released 00:16.

Still open: the modpack form, cases (i)-(iii), after 0626 releases c5h-new-mpk (MC #8839 (4), post-merge). The 43
variant stays at 7.5's [C] argument; the MC adds it to 0601's acceptance (MC #8839 (5)).

### 7.6b The same in the modpack form [L, 2026-11-21 12:31-13:26 EST, c5h-new-mpk, ticket 0645]

The run was under ticket 0645 because the pool refuses a claim for a done ticket, MC-B granting until 14:30. The
instance: c5h-new-mpk (F:, build 26577625, modpack548, `verify --quick` ok, the tag's 32-bit DLL set first). The
installer: `VP64-Patch-0624-guards-test-mpk-for-VP5.4.8-TEST.exe`, SHA-256
`cea9415e17ec0c493305226957962bb3cffb2d4a9f8b31db6c9d298288b582e6`, built from main fca9c481d (0624 in) at the
shared pins 1b87dd185 (proxy `fd4e95e0...`, compositor and purity of that build), i3 with 0626's set (ServerCombat not
written: main's default 1 applies, so route S arms, server.cfg `render_units_triggers=1`). It installs into
`<game>\Assets\DLC\VP_MODPACK\Mods\(1) Community Patch` with `VP64\release-options.txt` beside the proxy (7.5 confirmed
live for this form). Script `C:/c5-64bit-work/0624/mpk/mpk.sh` (not committed). The installed state was restored by
SHA-256 after each case. F: was busy: other agents' games ran on it.

| Case | Launch (F: read latency) | Result |
|---|---|---|
| (i) untouched installed session | 12:37 (12 ms at launch, after a 348 ms spike) | **PASS**: composite accepted (the DLC path), 9 render keys, 3 turns (29 -> 32), **0 LOUD** in vpproxy.log; server.log 2 LOUD lines = 0576's known stale-unit guard, not 0624's |
| (ii) VP64Options.ini deleted, first try | 12:54 (1.7 ms at launch; 353-543 ms, idle 0 %, queue 6-12 from 13:01:27 to 13:02:12) | the proxy's part as wanted (re-created with the set and ServerRenderer = 1, one LOUD line, composite accepted), but at 13:02:07 the server ended: `models: landmarks: the readiness or the tap frames were lost in play for 2233 ms, beyond render_models_hold_ms=2000` -> PeerDied -> the D33 route (report server-death/PeerDied, popup). Ticket 0641's class (the 2 s models hold on a stall), not 0624's; its run folder and report kept for impl-0641 in `C:/c5-64bit-work/0624/mpk/ev/case2-stall/` |
| (ii) repeat (MC-B 13:05) | 13:06 (1.7 ms at launch; over the window median 44 ms, max 714 ms, 28 of 58 records over 50 ms) | **PASS**: re-created with the set and ServerRenderer = 1, exactly one LOUD line in vpproxy.log, 1 turn (29 -> 30); server.log 4 LOUD lines of landmarks tap-frame losses recovered in 24 and 35 ms (0641's class, inside the bound) and 2 of 0576's stale-unit guard |
| (iii) ServerRenderer = 0 by hand | 13:15 | **PASS**: `boot FAILED: VP64Options.ini gives [Renderer] ServerRenderer = 0 (set in the file) in a D33 session ...`, `boot: NO PLAY`, report server-start/options-renderer-off, popup "Vox Populi 64-bit patch: error report" with the same summary as in 7.6; closed by the owner after the game ended |

Clean-up (13:20-13:26): the TEST installer was removed. The DLC tree is identical to the listing before (11,623 rows).
config.ini (`6d45ca3e...`) and the Community Patch DLL (`ff0f6e86...`) were put back by SHA-256; VP64Options.ini is
absent again, as it was before. The guard's report folders were copied to the evidence and removed. My Games differs
only in run outputs; front end restored; `verify --quick` ok; leftovers clean; claim released 13:26.

### 7.5 The 43-civ variant and the modpack form find the same set [C: build_installer.py and install.iss on 0616's branch `int-0616-release-path`, read 2026-11-20]

`build_installer.py` `stage_variant` stages each variant folder (std and 43) the same way: the proxy, `vpproxy.ini`
and `VP64\` beside it, and for i3 `VP64\release-options.txt` from the work folder's option set; the build refuses two
work folders whose sets differ. The shipped `vpproxy.ini` names no `x64_dir`, so the proxy's `x64_dir` is `<proxy
dir>\VP64` in either form (mods: `MODS\(1) Community Patch\VP64`, seen live in 7.4; modpack: the modpack's Community
Patch folder, where the installer puts the same variant folder) and `OptionsBoot` reads the variant's own set. Not
yet checked live on the modpack form or the 43 variant [I for those two]: that is part of the live check after 0616.

## 8. Options that also follow the game's own settings (ticket 0718, D246, 2026-11-23)

A key in the file can be on and still not act, because a setting of the game decides it in play. `[Performance]
AiSkipVisualDrain` (1 or 2) and `AiDrainUnordered` (1) act only while the game's Quick Combat and Quick Movement are
both on (the owner's rule D246). The proxy reads the two settings from the DLL after each AI `Update`, at the next AI
player's start and never during a drain. Ticking or unticking them in the in-game options therefore switches both
options from the next AI player on, without a restart. The file is not rewritten and nothing is written back: the
key keeps its value, and `vpproxy.log` gets one `pacing: D246:` line per switch. The options file's own help text for
`AiSkipVisualDrain` (`options/src/vp64_options.cpp`) does not mention this yet. A later ticket may add it (proposed to
the MC, #12330). `docs/OPTIONS.md` states it for both keys. Details:
`research/08-performance/turn-time-study.md` "Options 2 and 4 follow Quick Combat and Quick Movement".

## 9. Text options: `[Shell] WindowTitlePrefix` (ticket 0680, 2026-11-22)

The first option whose value is text, not a whole number: the prefix the proxy puts in front of the game window's
title while the patch runs (design and keeper: `research/06-tooling-and-workflows/window-title.md`). The `Types` row of
section 1 ("whole numbers ... the only type") holds for the table `Def`/`Id`/`kCount`; text options have a side table of
their own [C: `options/include/vp64_options.h`, `options/src/vp64_options.cpp`, branch `task-0680-window-title`]:

- **Why a side table, not a new `Type` in `Def`.** Every loop over `kCount` reads `Def::defaultValue`, `minValue`,
  `maxValue` and `Options::value[]` as integers: the installer's schema (`server.exe --write-options-schema`, 9081: an
  int per column), the logs of the proxy and the server, the release's option set (`ProxyOptions::ParseSet`, rows
  `Section<TAB>Key<TAB>whole number`), `TextWithSet`'s `; Key = <default>` lines and the extras rules. A text row in
  that table would have to be skipped in each of them. The side table (`TextId`, `kTextCount`, `TextDef {section, key,
  defaultValue, help, title}`, `TextDefinition`, `FindTextKey`) leaves all of them as they were; `Options` gained
  `char text[kTextCount][kMaxTextBytes + 1]` and `textFromFile[]` (no heap, no STL: the API rule of section 1 holds).
- **Rules** (`Parse`): a text key is found after the whole-number keys miss; section and repeat warnings as for the
  others. Value in double quotes: exactly what stands between them (spaces kept, `;` and `#` part of it), after the
  closing quote only blanks or a comment. Without quotes: trimmed and cut at an inline comment, as every other value.
  An empty value is valid (`""` or nothing after `=`): for `WindowTitlePrefix` it means "the EXE's title unchanged".
  Refused with a warning and the default: no closing quote, text after it, more than `kMaxTextBytes` = 64 bytes,
  invalid UTF-8 (overlong, surrogate, above U+10FFFF, cut short), a control character (C0, DEL, C1) [C: `TextValueValid`].
- **Default file**: the text sections come after the whole-number sections and before `[Extras]`, each line as
  `; WindowTitlePrefix = "[VP64] "` (quoted: the default ends in a space).
- **On the installer's options page since ticket 0745** (D255 G1, 2026-11-25; until then: "not on the installer's
  options page (it shows the schema's whole-number rows only)"): one checkbox, section 10.5. Not in the release's option
  set (not needed: the table's default is the product's; `options_schema.py` refuses a text key there). `docs/OPTIONS.md` gets an entry (the MC writes it at merge;
  text in `server/evidence/0680/OPTIONS-md-entry.txt`); `TestDocs` checks `WindowTitlePrefix = "[VP64] "` and `[Shell]`.
- **The server** reads the same file with the same code, so the key is known there too (no "unknown option" warning);
  it does not use or log the value (`server/src/srv_options.cpp` was not changed).
- **Tests** [L 2026-11-22 20:2x EST, branch `task-0680-window-title`]: `TestTextOptions` (10 good forms, 11 refusals each
  with its warning and the "earlier valid line not kept" rule, section warnings, the default file's line and its round
  trip, `TextValueValid`), the text titles in `TestTitles`, the docs check: x86 and x64 trees 879 passed, 0 failed on a
  copy of `docs/OPTIONS.md` with the entry; 878 passed, 1 failed (the docs check, expected until the MC's merge) on
  main's text; under MSVC ASan (`options/test_asan.py`, canary caught) 879/0 on the copy, no report
  (`server/evidence/0680/options_tests.py`, `asan_doc_copy.py`). The finishing pass ran the same script after main
  01068b8a0 was merged in [L 2026-11-24, branch `task-0680-finish`]: 943/0 on the copy and 942/1 on main's text, in
  both trees (main's tests grew). The ASan run was not repeated. All 18 builders of `vp64_options.cpp` linked
  (`research/06-tooling-and-workflows/window-title.md` section 4).

## 10. D255 audit (ticket 0739, 2026-11-25)

The owner's rule D255 (decision log, 2026-11-25): every feature that uses more than the base cost (running the server
and its IPC channels) or changes the stock game's behaviour or graphics has a key in `VP64Options.ini`, the installer's
options page offers it, and `docs/OPTIONS.md` describes it. This section lists what main ships against that rule.

**Method.** Read only, 2026-11-25 00:57-01:15 EST, on c71ffcf38. Main at 2ffd65fc4 differs from it only in tracker
files. No game run, no build, nothing launched. Sources
read:
- the options table `options/src/vp64_options.cpp` (`kDefs`, `kTextDefs`);
- what the page is made from: `server/src/srv_write_options.cpp` (`SchemaText`, which skips reserved rows and
  `testOnly` rows) and `tools/installer/options_schema.py` (`build_page`: every schema row is a page row;
  `ServerRenderer` is locked);
- `docs/OPTIONS.md`;
- the boot and frame paths of the proxy (`proxy/src/proxy_shell.cpp` `OnBoot`, `Configure`, `OnUpdate`;
  `proxy_rt.cpp`; the headers of every `proxy/src/*.h`);
- the server's render configuration (`server/src/render/srv_render_host.cpp` `ParseHostConfig` and its defaults,
  `srv_frame_jobs.*`);
- the shipped compositor configuration `shell/composite/shell_composite.ini`, and the shipped `vpproxy.ini` text
  (`tools/installer/build_installer.py` `VPPROXY_INI`);
- `research/06-tooling-and-workflows/known-crash-causes.md` and the tickets named in the rows.

[C] in a row means the code or document was read for this audit. [I] means inferred.

Columns:
- **Key**: its section and key in `VP64Options.ini`, or none.
- **Docs**: whether `docs/OPTIONS.md` has an entry for it.
- **Page**: whether the installer's options page shows it.
- **Default**: the table's default. "Release" is the value of the release's option set (`VP64\release-options.txt`).
  The release values given are those of the first beta's set: ticket 0626's descriptor
  (`research/06-tooling-and-workflows/release-builds.md` 7.1) plus `ServerFarmFields = 1` (`docs/RELEASE-0.1b.md` 5.2,
  K1). Beta 4's descriptor is beta 3's with only its notes changed (`validation-tiers.md`, cand6), and its values are not
  in the repository [I].

### 10.1 Features with a key (the options table)

| Feature | Resource or stock change | Key | Docs | Page | Default | Decision | Verdict |
|---|---|---|---|---|---|---|---|
| Baked terrain kept resident | Video memory: about 413 MB on huge at -1 (issue 3023) | `[Renderer] BakedTileCameraRadius` | yes | yes (radio and number) | -1; release not set | D88 | OK |
| The server's world renderer as a whole | GPU, memory and threads in both processes (10.2 row R1); the EXE's own terrain, water, fog, forest, decal and yield drawing switched off by D33 | `[Renderer] ServerRenderer` | yes | shown, **locked**: ticked in i2 and i3, set by `vp64helper on` | 0; 1 by `vp64helper on` | D80, D33, D125; locked by ticket 0624's renderer guard (D118) | Deliberately not optional on its own: the way to have it off is `vp64helper off` (the patch off). A D33 session with 0 is NO PLAY, because the world would be degraded (D118) |
| White fog mask for the EXE's models | Loads the EXE's `White.dds` once and makes data writes to the fog-mask global | `[Renderer] WhiteFogMask` | yes | yes | 1 | D136 | OK |
| Units drawn by the server | GPU; a copy of the game database for the render side (`db/units-*.db` in the run folder [C: `srv_remote_main.cpp` `UnitDatabaseAtLoad`]) | `[Renderer] ServerUnits` | yes | yes | 0; release 1 | MC O1 (a) 2026-11-17, D169; D197 set | OK |
| Fights played by the server (route S), with the stock fight faults fixed (0599) | Sounds and effects timed by the server | `[Renderer] ServerCombat` | yes | yes | 1; release not set (the table's 1 applies) | D210, MC #8627 | OK |
| Ranged-attack arc by the server | GPU | `[Renderer] ServerArc` | yes | yes | 0; release 0 | 0199 | OK |
| Planned-move markers by the server | GPU | `[Renderer] ServerPath` | yes | yes | 0; release 0 | 0300 | OK |
| Farm fields by the server | Field pattern differs from stock; farm buildings stand at the tile's centre (0655) | `[Renderer] ServerFarmFields` | yes | yes | 0; release 1 | D230, D231 (K1) | OK |
| Cities by the server | GPU; the model tap copy in the EXE (0719's stagings, at most 64 MB, 1.1-1.2 MB live on huge, `research/05-graphics-composition/frame-cost-exe-side.md` 5.1) | `[Renderer] ServerCities` | yes | yes | 0; release 1 | 0561 | OK |
| Landmarks by the server | As ServerCities | `[Renderer] ServerLandmarks` | yes | yes | 0; release 1 | 0561 | OK |
| The server's model store (ticket 0789, 2026-11-26): every city and landmark instance kept until evidence removes it | Server memory only: 240 bytes per entry [C, sizeof], about 280 with the map's node [I]; about 1.5 MB on the owner's T305 copy [L]; at most 262,144 entries per pass (about 75 MB each, two passes) plus the constant pool; the EXE's side unchanged | `[Renderer] ModelStore` (id 40; id 39 reserved for 0784) | yes | yes (under ServerRenderer) | 1 | D269, D255 | OK; 0 = ticket 0709's keep. server.cfg `render_models_store` overrides it (test switch); `server/evidence/0789/README.md` |
| AI update tick | Stock pacing changed in AI turns | `[Performance] AiUpdateTickMs` | yes | yes (radio) | 0; release 0 | D165, D191 | OK |
| AI skip visual drain | Stock order of showing changed; acts only with both quick modes on | `[Performance] AiSkipVisualDrain` | yes | yes (radio) | 0; release 1 | D193, D197, D246 | OK |
| AI drain unordered | As AiSkipVisualDrain | `[Performance] AiDrainUnordered` | yes | yes | 0; release 1 | D197, D246 | OK |
| Game events cross once | Speed-up; games identical | `[Performance] GameEventsOneCrossing` | yes | yes | 0; release 0 | 0504 P2 | OK |
| Hidden AI embarks | What is shown out of sight | `[Performance] HiddenEmbarks` | yes | yes | 0; release 1 | D193 | OK |
| Guards while the patch is off | Queue guard and farm guard in off mode | `[Proxy] GuardsWhenOff` | yes | yes | 1 | D224, D225 (1) | OK |
| Window title prefix | Changes the EXE's window title | `[Shell] WindowTitlePrefix` (text) | yes | yes since ticket 0745 (a checkbox "Show [VP64] in the window title", 10.5); was **no** (the page showed only the whole-number schema rows) | `"[VP64] "` | ticket 0680 (the owner's request, 2026-11-22); no decision keeps it off the page | **GAP G1, closed by 0745 (2026-11-25)** |
| Bugfix category switch | - | `[Extras] EnableBugfixes` | yes | yes | 1 | D169 | OK |
| Visual-additions switch | - | `[Extras] EnableVisualAdditions` | yes | yes (the installer's prompt) | 0 | D169, D170 | OK |
| Road layout, junction, jitter | Road picture differs from stock | `[Extras] RoadLayout`, `RoadJunction`, `RoadJitter` | yes | yes (radio) | 0 each | D212, D215, D249, MC #10673 | OK |
| Visible-area outline | Visual addition | `[Extras] VisibleAreaOutline` | yes | yes | 0 | E4 | OK |
| Unit placement fix | Display only | `[Extras] FixUnitPlacement` | yes | yes | 0 | E4 | OK |
| Aircraft at their base | Display only | `[Extras] FixAirBases` | yes | yes | 1 | D235 (2) | OK |
| Ships moored | Visual addition | `[Extras] ShipsMoored` | yes | yes (radio) | 0 | D207, D250 | OK |
| Intercept patrol | Visual addition; no effect yet (the DLL sends no render messages) | `[Extras] AirInterceptPatrol` | yes | yes | 0 | MC #8114 | OK |
| Perimeter garrisons | Visual addition | `[Extras] PerimeterGarrisons` | yes | yes | 0 | D243 | OK |
| Garrison looks (a)-(c) | Visual addition | `[Extras] GarrisonVisible` (test value) | yes, marked as a test value | no (`testOnly`) | 0 | D243 | Deliberately not offered (D243); not a gap |

Every key in the table has its `docs/OPTIONS.md` entry, and `TestDocs` in `options/tests/test_options.cpp` checks
that [C]. The reserved rows (ids 3, 14, 19-21; 39 on ticket 0789's branch until 0784 fills it) ship nothing. No `[Extras] FullExeOptimizations` key or extra is on
main (D228 has none built yet) [C: no such key in `kDefs`].

### 10.2 Features without a key

| # | Feature (code) | Resource or stock change | Key | Docs | Page | Default | Decision | Verdict |
|---|---|---|---|---|---|---|---|---|
| R1 | The renderer's parts. The compositor's taps (`shell_composite.ini`): the height field, 126 MiB of commit on huge while a copy exists, plus 42 MiB for the byte layer; the overlay target, about 67 MB of video memory; shadow, ice, water, decal and cut. A ring of 4 shared frame textures. `model_fow` (0339). Live terrain by re-bake, with the EXE's `Ice_Reload` and `Waves_Reload` (0465, 0586). The decal loader thread (0642: 243 textures, 19.9 MB read off the render thread on huge). | GPU, commit and threads; makes the server's picture follow the stock picture | none of their own: under `ServerRenderer` | `ServerRenderer` (not itemised) | locked | on with the renderer | D80, D66, D216 (live terrain IN; "on, no option", `RELEASE-0.1b.md` log of 2026-11-19), D223 for 0642 (a game-ending stall) | Deliberately part of ServerRenderer. [I]: no decision names each part; they are what drawing the stock picture needs. The D118 rule ends a game without decals, so a key would only turn a part into a failure |
| R2 | The UI draw-list growth (K4; `proxy_uidraw.cpp`, the Lua bridge's frame hook) | The EXE's UI buffers 4x (4 MB, 16,384 commands per half) | none (id 3 reserved) | yes: the "Format rules" paragraph | no | always while the patch is on | D235 | Deliberately not optional (D235) |
| R3 | The UI message queue guard while the patch is on (K3; `proxy_queue_guard.cpp`, installed by `ProxyShell::OnBoot`) | Import-slot thunk; drops a record that would overrun the queue | none for patch on (`GuardsWhenOff` covers off mode only) | yes (`GuardsWhenOff`) | - | always | D223; D224 made only off mode optional | Deliberately not optional with the patch on (D223, D224) |
| R4 | The farm builder guard, rule 2, while the patch is on (K1, K2; `farm_guard` in the compositor, `proxy_farm_guard.cpp` without it) | Can stop or skip the EXE's farm layout, leaving a farm without its field pattern | none for patch on | yes (`GuardsWhenOff`) | - | always | D185, D186, D223, D225 (1) | Deliberately not optional with the patch on |
| R5 | The server's stale Lua instance guard (K8, 0576) | Lua gets nil and one LOUD line, instead of a use after free | none | no | - | always | D223, D185 | Deliberately not optional (a crash fix, D223) |
| R6 | The failure route: the renderer and composite guards (0624), the error popup and report (D118, D119, D240), the crash filter's `exe-crash.txt` (0419), NO PLAY | Ends a degraded or failed game; report folders on disk | none | yes (`ServerRenderer`) | - | always | D118, D119, D124, D125, D213, D240 | Deliberately not optional (D118: no degraded fallback) |
| R7 | The lockstep exact wait (0691) | The EXE's frame waits for the server's picture of the same camera | none (`lockstep_mode` in `shell_composite.ini` is for tests) | no | - | exact | D242 | Deliberately not optional (D242) |
| R8 | The D33 configuration: `vp64helper on` writes 8 `[DisableSystems]` keys into config.ini (2 of them already off in stock), and the proxy writes the view-mask bits | The player's config.ini changed while the patch is on | none (`ServerRenderer`, locked) | yes (`ServerRenderer`) | locked | on with the patch | D33, D125 | Deliberately not optional: it is the patch being on |
| R9 | **The frame-jobs pool** (0711; `srv_frame_jobs.cpp` `ParseFrameJobsKey`, called with "auto" when server.cfg has no `render_frame_jobs`) | **2 worker threads** in the server on a machine with more than 2 logical cores (`kFrameJobsAutoWorkers`), 0 to 8 by the server.cfg key the proxy never writes [C] | none | no | no | auto (2) | none: epic 0686. D241 says "the same performance on similar memory use" first, modern resources "as an aside" | **GAP G3, closed by 0745 (2026-11-25)**: `[Performance] RenderFrameJobs` (10.5) |
| R10 | **The EXE's keyboard hook moved to a helper thread, and window ghosting off** (0720; `proxy_kbhook.cpp` `Install`, called from `ProxyShell::OnBoot` with no switch [C]) | One helper thread. Changes the EXE's behaviour: Alt+Enter is swallowed only while the game's window is in front (stock swallowed it in every program), and a stalled main thread gets no DWM ghost window | none | no | no | always while the patch is on | none: MC #12349 | **GAP G2, closed by 0745 (2026-11-25)**: `[Proxy] KeyboardHookThread` (10.5) |
| R11 | 3D leader scenes for majors 22-42 (0381; `proxy_leaderscene.cpp`) | About 32 MB of EXE address space per scene; scenes the stock EXE never loads | none: `vpproxy.ini leader_scenes` only (the shipped `vpproxy.ini` does not set it) | no | no | 0 (off, the stock behaviour) | the MC's return of 0381, 2026-11-16 | **GAP G4, closed by 0745 (2026-11-25)**: `[Proxy] LeaderScenes` (10.5); was: low, off by default, so it costs nothing, but a player could not turn it on |
| R12 | Product diagnostics and their disk use | Run folders: `keep_runs` = 10 in `<user>\Logs\VP64\run\`, each with the logs, server.cfg and the server's copies of the game database (`db/Civ5DebugDatabase.<n>.db`, and with units or models on `db/units-*.db`) [C: `proxy_rt.cpp`, `srv_remote_main.cpp`; measured by ticket 0746, 10.4: 13.7 MB of copies per run folder plus about 33 MB of logs an hour; was "their size is not measured, I" until 2026-11-25]. Instruments that are on: the compositor's `pacing=1` (0688), the server's pass stats (`passStats = true`), the proxy's `counters=1` | none: `vpproxy.ini`, `shell_composite.ini` and server.cfg only | no (only where the log is) | no | on | none (the census in 4.1, 2026-11-13: "keep for now") | **GAP G5** |
| R13 | Vox Populi's own Lua crash fixes (K5-K7, 0638) | Replaces up to 3 of VP 5.4.8's Lua files | none: a box on the installer's checks page, ticked | `docs/INSTALL.md` 2a | the checks page (not the options page) | ticked | D225 (2) | Meets D255 ("configurable in the installer"). A key could not act: the files are placed at install time [I: the MC may confirm] |
| R14 | The DLL's crash fixes in the pins (K19, K20) and its speed-ups (0504 wave 1, A9/A10, PGO) | Fixes change a crashing or freezing game. The speed-ups play identical games | none | no | - | always on the 64-bit path | D176, D181, D184, D223, D225 (3) | Deliberately not optional: crash fixes under D223, and the speed-ups change no behaviour |
| R15 | The 43-civ variant | 43 players | none: the installer's variant choice | `docs/INSTALL.md` | variant choice | std | D146, D166 | Meets D255 (an installer choice) |

Not counted as features: the server process and its IPC (the base), including the proxy's local answers
(`local_answers`) and the Lua bridge's channel; the server's process name (0740); test-only switches (`server.cfg
render_test_*`, environment variables, marker files; 4.2-4.4).

### 10.3 Gaps, as proposed to MC-B (no code in 0739)

| Gap | Proposed ticket | Priority (proposal) | Size |
|---|---|---|---|
| G1 | **Show `[Shell] WindowTitlePrefix` on the installer's options page.** Either a text field (the schema gets the text rows, `options_schema.py` and the page code accept them), or one checkbox "Show [VP64] in the window title" that writes `""` when unticked. `docs/OPTIONS.md` then drops "not on the installer's options page" | 3 | 2-4 h. **Closed by 0745** (checkbox route, 10.5) |
| G2 | **A key for 0720's keyboard-hook move**: `[Proxy] KeyboardHookThread`, default 1 (0 = the EXE's own hook on its main thread, as stock, for diagnosis only), with an OPTIONS.md entry and a page row. The alternative is for the MC to record it as deliberately not optional, because the lag it removes is caused by the patch's own lockstep wait (D242), and to name it in OPTIONS.md as the UI buffers are named. Recommendation: the key, since the Alt+Enter change alters stock behaviour | 4 | 1-2 h. **Closed by 0745** (the key, 10.5) |
| G3 | **A key for the frame-jobs pool**: `[Performance] RenderFrameJobs`, -1 auto (default, 2 workers on more than 2 logical cores) / 0 off / 1-8. The proxy writes `render_frame_jobs` into server.cfg. Add an OPTIONS.md entry and a page row (radio) | 4 | 2-3 h. **Closed by 0745** (10.5) |
| G4 | **Leader scenes for majors 22-42 as a key**: `[Proxy] LeaderScenes` 0/1/2, default 0, the 43-civ variant's choice, with the address-space cost in its help. The alternative is for the MC to rule that it stays a test switch (off costs nothing) | 2 | 1-2 h. **Closed by 0745** (the key, 10.5) |
| G5 | **Measure the run folders and rule on the diagnostics.** On a huge late load: the size of one run folder, and the database copies in it. Then either delete the copies at the server's exit, or add a `[Logs] KeepRuns` key. The MC also rules whether the instruments that are on (pacing=1, pass stats, counters) are part of the base: they feed the D119 report and D185's root causes. **Measured and proposed: 10.4** (ticket 0746), which adds gap G5b (the kept composite logs) | 3 | 2 h measurement + 1 h key |

D255 part (2), 8 GB late-game systems: this audit measured no memory. The largest items of known size are the bake at
-1 (about 413 MB of video memory on huge, with its key), the overlay tap (about 67 MB of video memory), the height-field
tap (126 + 42 MiB of commit while a copy exists), the model tap's stagings (at most 64 MB in the EXE) and the leader
scenes (32 MB of EXE address space each, off).

**Correction made by this audit:** in the table of section 2, the default of `FixAirBases` is 1 since one-off 9081 (main
ea8878286, 2026-11-22, D235 (2)). The table read 0 until 2026-11-25.

Sources: `options/`, `docs/OPTIONS.md`, decision log D88 and D89, `proxy/README.md`, `server/README.md`,
`research/06-tooling-and-workflows/instrumentation-in-the-dll.md`, `tools/harness/native/README.md`.

### 10.5 G1-G4 closed (ticket 0745, 2026-11-25)

Branch `impl-0745` (account B, MC-B's dispatcher). Every default is the behaviour of main before the key, so a file
that does not set the key plays as before [C]. Rows in section 2; player text in `docs/OPTIONS.md`.

| Gap | Key | Default | Route |
|---|---|---|---|
| G1 | `[Shell] WindowTitlePrefix` (text, ticket 0680) on the page | ticked | The checkbox of 10.3, not a text field. **Schema**: `server.exe --write-options-schema` (`srv_write_options.cpp`) adds one row per text option after the whole-number rows: `t<n>`, section, key, the default text, `-` four times, title, help (10 fields, as every row). **Page data** (`options_schema.py`): a `t` row is an `Opt` with `text` set, min 0, max 1, default 1 (ticked); its default file block ends `; WindowTitlePrefix = "[VP64] "` (`TEXT_LINE_RE`), checked against the schema like the others; a text key in the release's option set is refused; Pascal kind `T` with an eleventh field, the default text. **Page code** (`patch64.iss`): ticked = the text in force, unticked writes `WindowTitlePrefix = ""`; the player's own non-empty text ticks the box and a ticked box then writes no choice (the player's line stays as it is, so a non-ASCII text is never re-encoded); the player's `""` unticks it; silent `/P64OPT=Shell.WindowTitlePrefix=0` writes `""`, `=1` writes nothing. **Writer** (`install.iss` `P64MergeChoices`): a choice's value and default may be a quoted text (`P64OptQuoted`: 2-66 characters, quotes at both ends, none inside, no control character), both of the same kind; "differs from the default" compares texts as texts (`P64OptDiffers`). The test driver `p64test.iss` spells a quoted text `q:<text>` (a Windows command line cannot carry the quotes). The title of the text option is now "Show [VP64] in the window title" (was "Window title prefix"; `TextDef::title` is used only by the page) |
| G2 | `[Proxy] KeyboardHookThread` | 1 | 0 = `ProxyKbHook::Install` returns at once: the whole of 0720's change is off (the hook move and `DisableProcessWindowsGhosting`), since both change stock behaviour and "as stock" means neither. The value reaches `proxy_kbhook.cpp` through `SetThreadOption`, set in `proxy_rt.cpp` before `ProxyShell::OnBoot` (the shell file is not touched) |
| G3 | `[Performance] RenderFrameJobs` | -1 | -1..8 as a radio (-1, 0, "1 to 8" with its number box). The proxy appends `render_frame_jobs=N` to the render lines only for 0..8; -1 writes nothing (byte-equal server.cfg, the server's auto). In `[Performance]`, not under `[Renderer] ServerRenderer` on the page (the page nests only under a box of the same section); its help says it needs ServerRenderer = 1 |
| G4 | `[Proxy] LeaderScenes` | 0 | 0/1/2 radio; help names "about 32 MB of its 4 GB of address space" per scene. The environment variable and `vpproxy.ini leader_scenes` stay as test switches and win over the key (the 0381 evidence scripts use the ini). With the standard 22-major DLL every slot from 22 is a city state and nothing is asked |

Layout of the default file after 0745: `[Proxy]` holds GuardsWhenOff, KeyboardHookThread, LeaderScenes; a second
`[Performance]` block (RenderFrameJobs, id 36) follows it, before `[Shell]`; the section order of first appearance is
unchanged (Renderer, Performance, Proxy, Shell, Extras), so the page's sections are too. Ids 34-36 taken; the next free
id is 37.

**Tests** [L 2026-11-25, branch `impl-0745`, main 51fec50f0 merged]: see the hand-back in the ticket (`tracker/issues/0745-*.md`)
for the counts of each suite: `test_options` x86 and x64 (`TestD255Options`, `kCount` 37), ASan (`options/test_asan.py`),
`test_options_schema.py` (`test_d255_rows`, `test_text_rows_refused`), `test_install_removal.py` (four text-choice
cases: unticked writes `""` and the removal gives the file back; ticked at the default writes nothing; ticked over the
player's `""` writes the default; five malformed choices refused), `tools/harness/tests/test_installer.py`
(`test_options_page_d255_rows`, `test_silent_text_choice`: the built installer's page self-test on a hidden desktop,
0728), the kbhook unit plain and ASan (`TestThreadOption`), the leader-scene offline unit.

Non-default values through the built proxy and server [L 2026-11-25 08:17, a temporary shell-on case of
`proxy/tools/test_proxy_rt.py --only-shell`, not committed]: `RenderFrameJobs = 3` and `KeyboardHookThread = 0` in the
options file gave `render_frame_jobs=3` after `render_handover=ring` in server.cfg, the proxy's `options:
[Performance] RenderFrameJobs = 3 (from the file): server.cfg gets render_frame_jobs=3` line and `kbhook: [Proxy]
KeyboardHookThread = 0 (from the file) ...` without the ghosting line; the default run's server.cfg has no such key.

**Not verified live**: no game was run for 0745. The three proxy reads are code paths with one log line each
(`kbhook: [Proxy] KeyboardHookThread = 0`, `options: [Performance] RenderFrameJobs = ...`, `leaderscene: off|on:
[Proxy] LeaderScenes = ...`); the first live run with a non-default value should find them in `vpproxy.log`.

### 10.4 G5 measured: run folders, the kept composite logs, the instruments (ticket 0746, 2026-11-25)

**Method.** Read only, 2026-11-25 01:23-02:10 EST, main 550e34166. No build, no launch: MC-B granted c5h-c for a live
run at 01:30 and called a full hold on the workstation (0734's barrier) before anything was laid, so the instance was
never claimed or changed. Every figure below comes from run folders and logs that earlier runs left on the disk, copied
to a scratch folder before reading:
- **121 run folders** in `<user>\Logs\VP64\run\` of 13 test instances (c5h-a to c5h-i, c5h-new-a and -b, the two mpk
  instances), 2026-11-17 to 2026-11-25. Among them **38 loads of the huge late save** (copies of `C5H-VP8P-HUGE_0350`,
  Huge Continents 128 x 80, turn 350, one at 355; server.log `feed: snapshot 128 x 80 at turn 350`) on lay.py product
  layouts, 3-7 min each: 29 with units, cities or landmarks on (two database copies: c5h-a 9, c5h-c 8, c5h-new-a 8,
  c5h-d, c5h-e, c5h-f 2) and 9 with them off (one copy: c5h-new-b 4, c5h-f 2, c5h-h 3) [L];
- **the owner's own install's 10 run folders and composite logs** (beta 4 installed, 2026-11-22 to 2026-11-25, 3.9 h
  of play in 10 sessions; the last one 89.7 min on a new Huge map, turns 0-13, ended by an orderly exit) [L]. Read for
  sizes and line kinds only; nothing of it is in the repository.
- the code that writes and reads them [C]: `proxy/src/proxy_rt.cpp` (`MakeRunFolder`, `Count`, the traffic table),
  `server/src/remote/srv_remote_main.cpp` (`SetGameDatabase`, `UnitDatabaseAtLoad`), `server/src/render/srv_render_host.cpp`
  (`passStats`), `shell/composite/shell_composite.cpp` (`PrepareDefaultLog`), `proxy/src/proxy_report.cpp`.

**1. One run folder** [L]. Each boot of the proxy (one per load of the game DLL) makes one run folder. Its content:

| Part | Size | Depends on |
|---|---|---|
| `db/Civ5DebugDatabase.<n>.db` (the server's working copy, made at `SetGameDatabase`) | **6.82 MB** (VP 5.4.8, mods form; 6.85-6.88 MB in the owner's install) | the mod set only: the same on Tiny turn 0 and Huge turn 350 |
| `db/units-<n>.<g>.db` (the render side's copy; with `render_units=1` or the model library wanted, that is ServerUnits, ServerCities or ServerLandmarks on: all three are on in the release set) | **6.82 MB**, the same file again | as above; a new generation only when the database changed (`data_version`); none seen with g > 1 |
| `render.log` | **26-35 MB an hour** in the steady part: huge t350 runs 7.4-9.6 KB/s (4 runs), the owner's 90-min huge session 8.4 KB/s (45.1 MB) | session length; hardly the map (early and late huge within the spread) |
| `server.log` | about 2 MB an hour (owner: 3.0 MB in 90 min; a 4-min huge t350 load 0.09-0.2 MB) | session length, turns |
| `vpproxy.log` | about 0.4 MB an hour (owner: 0.59 MB in 90 min) | session length |
| tables (`proxy-calls*.tsv`, `proxy-lua-*.tsv`, `vpproxy-*.tsv`, `feed-per-turn.tsv`, `gameevents-raises.tsv`), `farm-builder-calls.log`, `server-report.txt`, `server.cfg` | 0.3-0.6 MB a session | turns |
| `Logs\` (the DLL's own logs) | only with the player's config.ini LoggingEnabled = 1 (`dll_logs`); 0.03-0.6 MB seen | the player's choice |

So **one run folder = 13.7 MB of database copies + about 33 MB of logs per hour of play** (6.9 MB of copies with units,
cities and landmarks all off). A huge late load stopped after 3-7 min: 15.0-17.6 MB with two copies (13.6 MB of it),
7.5-8.4 MB with one (median of all 121 folders 14.3 MB). No folder held more than one pair of copies: a second load in the same EXE process goes
through the EXE's unload of the game DLL at the mod change (`research/03-dll-engine-boundary/error-report-exe-crash.md`
item 5) and a new boot, so it gets its own run folder [C: `MakeRunFolder` at each boot; the offline test `d28: reload in
one process`; not run live from the in-game load menu in this ticket, I]. One EXE session with three loads therefore
takes three of the ten places.

**2. The total at keep_runs = 10** [L for the owner's state, I for the projection]. `MakeRunFolder` prunes to `keep_runs`
folders, the new one included, oldest first; the value comes only from `vpproxy.ini`, which the installer ships without
it, so 10.
- The owner's install now: **249.7 MB** in 10 folders for 3.9 h of play, of which **130.3 MB (52 %) are the database
  copies**. Largest folder 63.9 MB (the 90-min session).
- 10 short sessions (the test instances' pattern): 77-166 MB found.
- 10 sessions of 4 h: about 10 x (13.7 + 4 x 33) = **1.5 GB**. The bound grows with session length, not map size.

**3. The kept composite logs: a second store with no limit (gap G5b, new)** [L, C]. Since ticket 0385 (2026-11-16,
b7b5a2c66) `PrepareDefaultLog` renames the previous `shell_composite.log` to `shell_composite.<last write>.log` at every
EXE start, in `<user>\Logs\` (VPSHELL_OUT_DIR), and **nothing deletes them**: no count, age or size limit. Rate:
**24-26 MB an hour** (owner 38.3 MB in 89 min; huge t350 runs 6.8 KB/s). On disk: the owner's install 15 files, 95.4 MB
after 3 days; c5h-new-a 109 files, 218.8 MB; c5h-new-b 89 files, 229.1 MB; c5h-a 119 files, 140.6 MB. A player keeps
about 25 MB per hour of play for good, for example 2.5 GB after 100 hours: after a few weeks this is larger than the run
folders, and it is the largest disk cost of the product's diagnostics.

**4. What fills the logs** [L, line kinds counted in the owner's 90-min session]:
- render.log (45.1 MB): the render host's 1 s status lines (`units 1s` 8.0 MB, `terrain 1s` 8.1, `models` 6.8,
  `host 1s` 2.9; `forest`, `shadow`, `near`, `fog`, `unitshadow`, `prepare`, `water refl` 9.0 together), about 35 MB;
  **the pass stats 6.0 MB** (`pass 1s` 3.3, `pass long` 2.6, `vidmem 10s` 0.1), 4.0 MB an hour, 13 % of the file;
  decal, road, farm and cut lines about 2.5 MB.
- shell_composite.log (38.3 MB): the filters' two lines a second (`modelfilter` 16.0 MB, `unitfilter` 9.3), `stats` 4.5,
  `lockstep` 3.8, `modelgeom` 1.4; **the pacing lines 1.9 MB** (`pacing` 1.8, `vidmem` 0.1), 1.3 MB an hour, 5 %.
- The instruments are a small part. The bulk is the 1 s status lines, which predate them and have no switch.

**5. The instruments that are on, with their cost.**

| Instrument | Switch (where) | Disk | CPU |
|---|---|---|---|
| Frame pacing in the compositor (0688) | `pacing=1` (`shell_composite.ini`) | 1.3 MB an hour of the composite log [L, 4.] | 21 ns per Present plus 46 ns for its own two QPC reads on oldpc (0.0004 % of a 16.7 ms frame); every 10 s 1.7 us on the main thread and 63 us on the control thread [L, grade A: `research/08-performance/frame-pacing-instruments.md` 4.2]; on/off frame-time arms show no difference [L, grade C, same file 4.3] |
| The server's pass stats (0688) | `render_pass_stats=1` (server.cfg default; the proxy never writes the key) | 4.0 MB an hour of render.log [L, 4.] | 0.25 us per pass (18 QPC reads) on oldpc, 0.0015 % of 16.7 ms [L, grade A, same file 4.2]; lines formatted and written on the server's log thread (5054) |
| The proxy's traffic table, frame tracker and snapshots | `counters=1` (`vpproxy.ini` default) | the `vpproxy-*.tsv` files, 0.13 MB a 90-min session [L] | one table add per crossing and a frame check on ui calls [C]; not measured. The per-method counts and their QPC times (`Count`) are **not** behind this switch: they run in every call whatever `counters` says [C] and feed proxy-calls.tsv and server-report.txt. Order of size [I]: a few ns per call, about 1-2 ms of a huge turn's 365 k calls (`research/03-dll-engine-boundary/proxy-design.md`, "Calls"), under 0.02 % of a 10 s turn |

**6. Who reads them** [C]. The D119 error report (`proxy_report.cpp`) takes the last 64 KB of `vpproxy.log` and of every
`*.log` in the **current** run folder, and names a `*.dmp` there; it never reads the database copies, older run folders
or the kept composite logs. The copies are only the server's working files: their content is fixed by the mod set, and
server.log keeps their table count, row count and digest (`database: ... copied to ... digest`). Older run folders and
composite logs serve a developer's root cause (D185) when a player sends them on request.

**7. Proposal to MC-B (no code in this ticket).**
- **(a) Delete the database copies at the server's clean exit: yes.** At the orderly shutdown (kRt_Shutdown, exit 0, no
  failure reported) the server closes its connections and deletes `db\*.db` of its own run folder; after any failure (a
  report written, a non-zero exit, a dll-fatal) they stay, for the root cause. Saves 13.7 MB per kept folder, **half the
  run folders' disk** in the owner's real state (130 of 250 MB). Nothing reads them after the exit (6.). No option
  needed. About 1 h with a test.
- **(b) A `[Logs] KeepRuns` key: yes, with (a).** Whole number 1..100, default 10, in `VP64Options.ini`, with an
  OPTIONS.md entry and a page row (number field). The proxy reads it at boot and it wins over `vpproxy.ini keep_runs`
  (which stays as the test override). It meets D255 because the folders are disk use beyond the base, and it lets a
  player with long sessions (1.5 GB at 10 x 4 h) choose. About 1 h (options table, the proxy's read, docs, page row, the
  options test).
- **(c) G5b, a limit for the kept composite logs: yes, the most needed of the three.** `PrepareDefaultLog` keeps only the
  newest N stamped logs and deletes the oldest `shell_composite.2*.log` beyond them, with N the same KeepRuns value (the
  proxy hands it over beside VPSHELL_OUT_DIR, or the composite reads the options file). With N = 10 the store is bounded
  like the run folders (owner today 95 MB, then about 60 MB). Moving the composite's log into the run folder would give
  one place and one limit, but the composite loads before the proxy makes the folder [I]; the count rule is the small
  change. About 1 h. *(Corrected 2026-11-25, ticket 0747: the order is the other way round [C: `proxy_rt.cpp`
  `DllGetGameContext` calls `Boot()`, which makes the run folder, then `ProxyShell::OnBoot()`, which sets
  `VPSHELL_OUT_DIR` and only then calls `LoadLibraryW` on the composite]. The count rule was built anyway (10.5): the
  log stays where tools and testers look for it.)*
- **(d) The instruments as D255 base cost: yes, recommended.** CPU: under 0.01 % of a frame for pacing and pass stats
  (grade A, oldpc), the counters switch under 0.02 % of a turn [I]. Disk: 5.3 MB an hour together, 9 % of the about
  58 MB an hour the product logs. They are what the D119 report's tails and D185's root causes are made of (the freeze
  and long-pass lines name the stage of a stall), and D241's frame-pacing parity is judged by them. A key to turn them
  off would save no measurable CPU and little disk; the disk is handled by (a)-(c). They stay without a key, named in
  OPTIONS.md as the UI buffers are (10.2 R2), with their disk figures.
- For the MC's judgement, not a D255 key: the 1 s status lines (about 46 MB an hour, 80 % of the logs) could be written
  every 10 s while the camera is still, which would cut both stores by most of their size.
- Priority (proposal): (c) 4, (a) 3, (b) 3; one ticket for (a) + (b) (server and proxy), one for (c) (the composite).

**8. Not measured here.** A live second load from the in-game menu in one EXE session (1.: from the code and the offline
test); a session longer than 90 min on a late huge game (the rate was the same on early and late huge, so the projection
uses it); the counters switch's CPU by an on/off arm; the DLL logs with LoggingEnabled = 1 on a long late game (the
player's own setting; kept ten times because they live in the run folders, where the stock game overwrites one set in
`<user>\Logs\`). The live check takes about 30 min on one hidden instance after MC-B's HOLD OFF: a copy of the huge save
loaded twice from the in-game menu, then the run folders listed.

### 10.5 G5/G5b built: `[Logs] KeepRuns`, the database copies, the kept composite logs (ticket 0747, 2026-11-25)

Branch `impl-0747` from main 2a0677cc2. The MC's ruling (d) of 10.4 item 7 is recorded in `docs/OPTIONS.md`
(`[Logs] KeepRuns`, last paragraphs): pacing lines, pass stats and the proxy's counters are D255 base cost, without a key.

**(b) `[Logs] KeepRuns`** [C: code]. Options table id 38 (`options/src/vp64_options.cpp`), whole number 1..100, default
10, category none, title "How many diagnostic logs are kept"; its own `[Logs]` block in the default file after the
second `[Performance]` block and before `[Shell]` (the page's section order becomes Renderer, Performance, Proxy, Logs,
Shell, Extras). The help's one value line `1 to 100 = ...` makes it one radio button with the page's number box
(`patch64.iss`, the "a to b" button), started at 10 or the release's set. The proxy cannot wait for `OptionsBoot`
(it logs, so it runs after `MakeRunFolder`): `ResolveKeepRuns` (`proxy/src/proxy_rt.cpp`, at the top of `Boot`, D28
layout only) reads the file read-only as `GuardsWhenOffOption` does, and `ProxyLogic::EffectiveKeepRuns`
(`proxy_rt_logic.cpp`) decides: KeepRuns set in the file wins; else `vpproxy.ini keep_runs` when the ini names it (test
switch; `Settings::keepRunsSet`); else the table's default; bounded to 1..100. `vpproxy.log` says it once:
`boot: run folders kept: 3 ([Logs] KeepRuns = 3 from the options file (it wins over vpproxy.ini keep_runs)); VPSHELL_KEEP_LOGS=3 set ...`.

**(c) the kept composite logs** [C: code]. The proxy puts the same number into the EXE's environment as
`VPSHELL_KEEP_LOGS` (marked `VPSHELL_KEEP_LOGS_BY=vpproxy`, so that the boot after a mod change in the same process sets
it again; a value a test put there first is kept and logged). `shell_composite.cpp` `PruneKeptLogs`, in `DllMain` right
after `PrepareDefaultLog` and only for the default log (never with `VPSHELL_LOG`), keeps the newest N of
`shell_composite.<yyyymmdd-hhmmss>[-<n>].log` in the output folder, ordered by stamp then n, and deletes the rest. Only
that exact form counts (n 2..99 without a leading zero, as `PrepareDefaultLog` writes it): `shell_composite.log`, a
`shell_composite.<pid>.log`, the CSVs and malformed stamps are never touched. N = `VPSHELL_KEEP_LOGS` 1..100, else 10
(a bad value: 10 and a note). kernel32 only, under the loader lock; at most 4096 keys a pass (the oldest), more files
take another pass. The composite's log says `log: older logs: 14 deleted, 0 could not be deleted; the newest 3 kept
(VPSHELL_KEEP_LOGS; ticket 0747)`. The composite is loaded once per EXE process (pinned), so it prunes once per game start.

**(a) the database copies** [C: code]. Deleted by the proxy, not the server: at kRt_Shutdown the server's SQLite
connections are open (its own `s_pDb`, which the DLL holds, and the render side's copy), SQLite opens without
`FILE_SHARE_DELETE`, and closing the DLL's database under it during the shutdown is a new risk for nothing. The proxy's
`Shutdown` already waits up to 30 s for the server's exit and reads its code; after it, `DeleteRunDatabaseCopies`
deletes `<run>\db` files whose names `ProxyLogic::IsRunDatabaseName` accepts (`Civ5DebugDatabase.<n>.db`,
`units-<n>.<g>.db`, each also with `-journal`, `-wal`, `-shm`) only when `ProxyLogic::DeleteRunDatabases` says so: the
server ended by itself (the wait returned), exit code 0 (the server exits 2 when it recorded a failure, `ServiceThread`)
and no error report this load (`g_reported`). Otherwise one line says why they are kept. An abrupt unload (no
SetDLLIFace(NULL)), a crash of the EXE or a dead server never reaches this path, so the copies stay there. Nothing else
in `db\` and nothing outside it is deleted. `vpproxy.log` (offline test, two planted 4 KB files and a foreign one [L]):
`shutdown: the server exited cleanly: its 2 database copies in ...\db deleted (0.0 MB; 1 other files there kept) (ticket
0747)`; in a real game the two copies are 13.7 MB (10.4 item 1) [I: not run live].

**Files changed** (proxy, server, shell): `options/include/vp64_options.h`, `options/src/vp64_options.cpp`,
`options/tests/test_options.cpp`, `proxy/src/proxy_rt.cpp`, `proxy/src/proxy_rt_logic.{h,cpp}`,
`proxy/tools/rt_tests/unit.cpp`, `proxy/tools/rt_tests/loader.cpp` (`LOADER_PLANT_DB`), `proxy/tools/test_proxy_rt.py`,
`shell/composite/shell_composite.cpp`, `shell/tests/test_composite_log.py`, `tools/installer/tests/test_options_schema.py`,
`docs/OPTIONS.md`, `proxy/README.md`, `shell/README.md`. No server source changed. `tools/installer/options_schema.py`
needed no change (a new section and a range button are generic).

**Tests** [L, 2026-11-25 15:35-16:25 EST, workstation, through quietlock, branch `impl-0747`]: `test_options_schema.py`
26/26 (new `test_keep_runs_row`: the page row, its Pascal rows, a release set's 25 taken and 0 refused); `test_options`
x86 1121/1121, x64 1121/1121, ASan (`options/test_asan.py`, canary caught) 1121/0 (new `TestKeepRuns`: 100 good values,
six bad ones each one warning and the default, the `[Logs]` block's place, the file parses back); `test_options_boot.py`
62/62 plain and ASan; `build_server.py` (0.1b pins, release) OK; `test_proxy_rt.py` 240/240 (unit 128/128 with
`EffectiveKeepRuns`, `IsRunDatabaseName` 8 yes / 17 no names, `DeleteRunDatabases` and its three negatives; d28: the
KeepRuns line, planted copies deleted after a clean exit with the foreign file kept, KeepRuns = 3 over keep_runs=1
leaves 3 run folders, the copies kept after an abrupt unload); `shell/build.cmd` and `build.cmd asan` OK, then
`shell/tests/test_composite_log.py` 8/8 on each (new: VPSHELL_KEEP_LOGS=3 keeps the newest 3 of 17 and deletes 14, the
seven foreign names untouched; the default 10; 0, 101 and "x" give 10 with a note; at the limit nothing deleted). The
first composite run failed ticket 0385's `test_empty_old_log_is_removed` (a "log:" line when there is nothing to keep):
the note is now written only when a kept log exists. `test_safety_static.py` 55 passed.
**Not verified**: no live game (a real run folder's two 6.8 MB copies deleted at a real exit, and the composite's
prune in an installed game); the installer's page itself was not run (its data is tested; the "a to b" number box is
the RenderFrameJobs mechanism of 0745); `run_extras_test.py` and `run_hidden_embark_test.py`, which also link
`vp64_options.cpp`, were not run (the change adds a table row only, no symbol).
