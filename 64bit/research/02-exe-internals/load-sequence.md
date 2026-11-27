# Launch-to-play load sequence

Sources: **`shell-gates/LOADTIME.md` (final, 2026-11-10; current, supersedes the load-time note where they differ)**
with its tools and data in `shell-gates/loadtime/` (`live/phase_table_20261110.txt`, `live/*_chains.txt`,
`live/*_meta.json`, `live/exe_addrs_named.tsv`); `memory-notes/civ5-load-time-breakdown.md` (2026-11-10), raw logs
`probe-logs/B4_all_off_safe_keep_unit/` and `probe-logs/A5_safe_keep_unit_fluidfow/` (`stopwatch.log`,
`system_init.log`, `net_message_debug.log`, `result.json`), `shell-gates/STUBS.md`,
`shell-gates/stubs/{terrain,coupling,unit,landmark}.md`, `memory-notes/civ5-exe-message-queue-overflow.md`,
`civ5-headless/engine-contract.md` (the `ShouldValidateGameDatabase` engine call).

Run conditions: huge save `VP8P-HUGE_0350` (t350, 2,227,934 bytes), mods cp+vp+eui. Two measurement sets:

- **2026-11-09 probes** (13 runs with Logs; B4, A5, Combat used below): `vp_modgame.py` automation (no human
  pauses), Release 5.4.8 DLL. The B4 log timestamps in section 2 are from this set.
- **2026-11-10 live phase** (LOADTIME.md, six launches 01:44-02:07, plus the `b4unit` run earlier that day):
  experiment DLL 5.4.8 + memory-diagnostics (`clang-output\Release`, 17,453,056 bytes, PDB matched), launched by
  `nofocus/civ_bg.py launch`; three unsampled runs for timings (B4, B4 + `ValidateGameDatabase = 0`, B4 +
  LandmarkSystem off) and three `cdb -pv` stack-sampled runs for attribution (B4, B4 + Landmark, stock all-on).
  One run per configuration: **n = 1, run-to-run noise 2-4 s per phase**. Sampling stretched each window ~1.6x, so
  sampled runs give owners, not timings.

"B4" is the `[DisableSystems]` combination of `disable-systems-and-masks.md` (City off, Unit on); "B4 + Landmark"
adds `LandmarkSystem = 1`. Process start is the CRT start (EUI's "Loading EUI plot help" time minus its
`os.clock()`); it precedes the first log line by 16-18 s.

## 1. Owner summary

> **Corrected 2026-11-10 (LOADTIME.md).** This section replaced: "Total ~128 s: EXE ~89, DLL ~16 (12%), unknown
> ~19, automation ~3", which was measured from the first stopwatch line (it missed the 16-18 s before the EXE
> writes APP.log) and left the 18-20 s gap unattributed.

**Process start -> `WaitForNetPlayers` is 141-157 s [L]** (11-09: B4 142.77, A5 146.10, Combat 156.89; 11-10:
b4unit 147.45, B4 148.24, B4 + `ValidateGameDatabase = 0` 141.34). From APP.log it is 126.8-139.5 s [L]; the
**pre-log window is 16.0-18.4 s [L]**. vp_modgame's launch -> in-game time was 152 s for the 11-10 B4 run [L].

Owner totals, B4 (11-09 probe, 142.8 s; attribution of the "live" rows from the 11-10 sampled B4 run):

| Owner | s | Of which | Conf. |
|---|---|---|---|
| EXE, attributed | 59.5 | DB + localization + map scripts + mod SQL 21.2; app system startups 10.7; Unit/Landmark restarts at mod activation 11.9; terrain bake 7.7; menu, front-end rebuild, load screen, GPU init, net 8.1 | [L] logs |
| EXE, before and around the load (formerly "unattributed EXE") | 37.0 | pre-log 16.0: a system-info report that **runs DxDiag twice** (static initialisers 0x9C4EB0 / 0x9C4EC0 -> 0x679EA0, strings "gamver/winver/sysinf/cpufea/DXDIAG"; D3D12/D3D11/DirectDraw device creation, driver signature checks) + SteamAPI_Init (0x424630); after Lua init 10.0: the main thread waits on a **DirectShow graph (quartz.dll)** via 0x6B2B30 / 0x6BB710 although `SkipIntroVideo = 1`; save read 11.0: **FFileIO ReadFile / SetFilePointer** (0x820750 / 0x8208B0), file not identified | [L] sampled (prelog 11, postlua 8, saveread 6 samples); timing [L] |
| EXE, LandmarkSystem view build (the former "unknown ~19") | 27.1 | gap 19.9 (farm-field layout), tail 7.2 (TerrainDecalSystem geometry); section 1.1 | [L] A/B + samples |
| DLL | 17.4 | **ValidateGameDatabase 10.1** (Validate FK 2.23 + 2.22 at boot, 5.65 at mod activation; a config switch, section 5), PrefetchGameData 3.5, `CvGame::Read` 3.3, rest 0.5 | [L] stopwatch |
| Lua | ~1.8 | in-game UI contexts, plus part of the front-end rebuild and the late EUI contexts | [L] logs |

None of the 37 s "around the load" is skippable by config [I: no config key reaches the DxDiag report, the
DirectShow graph or the FFileIO reads; only identification or an EXE patch would].

Phase table (seconds; LOADTIME.md section 1 and "Live results"; reproduce with
`python loadtime/loadtime_logs.py --table <Logs dirs>`). All values [L].

| Phase | B4 11-09 | A5 11-09 | Combat 11-09 | B4 11-10 | B4 no-validate 11-10 | B4 + Landmark 11-10 | Owner |
|---|---|---|---|---|---|---|---|
| process start -> APP.log | 16.00 | 16.78 | 17.42 | 16.90 | 16.94 | 17.29 | EXE (DxDiag x2, Steam) |
| APP.log -> CreateCoreDatabase | 9.95 | 9.89 | 9.67 | 10.56 | 10.21 | 10.56 | EXE (DirectShow wait) |
| -> boot activation | 5.23 | 5.49 | 5.76 | 5.16 | 4.60 | 5.18 | EXE (loc 2.66) |
| boot `SetActiveDLCandMods` (base + DLC) | 10.21 | 12.44 | 12.12 | 13.86 | **7.36** | 12.37 | EXE 4.9 / base DLL 5.3 |
| app system Startups | 10.67 | 10.39 | 14.78 | 11.75 | 11.84 | **6.02** | EXE (Unit 4.5, Landmark 5.6, City 2.0) |
| ActivateEnabledMods | 31.66 | 31.97 | 34.45 | 31.96 | **26.73** | 34.82 | EXE 22.9 / DLL 8.8 |
| front-end rebuild | 2.50 | 2.88 | 2.05 | 2.36 | 2.44 | 2.78 | EXE + Lua |
| Multiplayer Init -> NetLoadGame | 11.02 | 10.06 | 10.84 | 7.47 | 10.75 | 10.28 | EXE (FFileIO reads) |
| DLL read (NetLoadGame -> RegisterComplete) | 3.31 | 1.97 | 3.25 | 2.17 | 3.48 | 2.62 | DLL |
| -> LoadScreen | 1.16 | 0.77 | 1.08 | 1.08 | 0.88 | 1.08 | EXE (LoadGameState::Enter 0x591200) |
| terrain bake | 7.70 | 9.18 | 8.46 | 9.74 | 8.97 | 7.67 | EXE |
| in-game Lua UI contexts | 1.77 | 1.81 | 2.12 | 1.97 | 2.11 | 2.08 | EXE + Lua |
| **gap** | **19.86** | **20.77** | **22.22** | **19.92** | 22.47 | **1.06** | EXE, LandmarkSystem |
| **tail** | **7.23** | **7.27** | **7.41** | **8.02** | 7.84 | **0.03** | EXE, LandmarkSystem |
| GPU init -> IAmReady | 2.12 | 1.88 | 2.47 | 2.16 | 1.72 | 0.69 | EXE + Lua |
| -> WaitForNetPlayers | 1.50 | 1.72 | 1.77 | 2.05 | 1.92 | 2.05 | EXE |
| **process start -> WaitForNetPlayers** | **142.77** | **146.10** | **156.89** | **148.24** | **141.34** | **117.42** | |

Two activations happen per launch: the boot one (base + DLC) and the app system startups both run **before any
front-end Lua** [L] (first front-end print 36 s after APP.log; two "Changing active DLC and Mods" lines in
modding.log), so the front end cannot skip them. Localization is merged three times per launch (2.0-2.1 s each)
inside the EXE [L]. Inside ActivateEnabledMods, the Unit/Landmark restart block after the second
LoadUnitLibraries stays at 13-14 s with Landmark off [L], so it is not (only) Landmark's restart or the restart
ignores the config mask [I].

`LoadGameState_TOTAL` (39-44 s) is LoadGameState::Enter 0x591200 -> Exit 0x494160 [C], i.e. the LoadScreen context
to about IAmReady; its line is flushed with WaitForNetPlayers. `VP_LUAAPI ####` in Lua.log is not a reliable "DLL
loaded" marker.

### 1.1 The 18-20 s gap: LandmarkSystem's farm-field layout

> **Answered 2026-11-10 (LOADTIME.md live phase).** Replaced: "UNATTRIBUTED gap ... same with world systems on or
> off, 18-20 s, owner unknown" (the 11-09 probes never had LandmarkSystem off).

Mechanism [C] (Ghidra, static):

- `system_init.log` lines are queue records, not direct writes: each timer slot is a 128-byte record of type 0x66
  allocated by 0x5CA5E0 (thunk 0x8447F0 on the 4 MB queue's manager 0x1A0E400), written to the log by the record
  handler 0x5183F0 **when the 4 MB queue is pumped** (0x459D20). A line's timestamp is the next pump, so 20 s
  between two lines = 20 s without a completed pump.
- LoadGameState::Update 0x403170 is a state machine on `[this+0x10]` (jump table 0x403684), one step per frame.
  **State 4** runs `GameViewState::StartupGame` 0x4686C0 (its own timer 1.8 s; ends by pumping queue B 0x785AF0 and
  the 4 MB queue - the flush that writes the gap's start line), then **StartInitStuffOnMap 0x42FCC0** (own timer
  0.2 s), which walks every plot and posts **queue-B records** (0x44-byte header; types 0x32, 0x41, 0x83, ...) and
  sets state 5. **State 5** stays until queue B is empty (0x4FB9F0 on buffer 0x2A10E08 + idx*0x4C), then state 8.
- The frame function pumps queue A (0x2A0FF80 via 0x5AD360), **queue B (0x2A10200 via 0x785AF0, call at
  0x4B4029)**, queue C (0x2A0F480 via 0x7052B0) and only then the 4 MB queue (0x459D20, call at 0x4B4067).

So **the gap = the first frame after state 4 spending ~20 s in queue-B dispatch before it reaches its 4 MB pump; the
tail (7-8 s) = further queue-B work until state 5 sees the queue empty** [C mechanism, L timing].

Live attribution [L] (2026-11-10, n = 1 per configuration):

- **LandmarkSystem off: gap 19.92 -> 1.06 s, tail 8.02 -> 0.03 s, app startups -5.7 s, process start ->
  WaitForNetPlayers 148.24 -> 117.42 s (-30.8 s)** (unsampled B4 vs B4 + Landmark, same night).
- Gap samples: 13 of 17 (B4) and 11 of 19 (stock all-on) in one chain: queue-B pump 0x785AF0 -> dispatcher
  0x63ABA0 -> listener walk 0x8668E0 -> **farm decal builder 0x748FC0 (`ART_DEF_IMPROVEMENT_FARM`) -> field layout
  0x6016F0** (uses the city-field manager 0xB9DDA0 that only the Landmark startup initialises) -> 0x5362F0 ->
  0x5B86D0 -> 0x585490 -> 0x4365E0 -> 0x77C1C0. Ghidra's caller graph confirms the links 0x748FC0 -> 0x6016F0 ->
  0x5362F0 -> 0x5B86D0 (each callee has a single caller) [C].
- Most of the rest of the gap, and 9 of 11 tail samples, are **TerrainDecalSystem geometry** (0x49B5A0 <- 0x4387D0
  <- 0x81D790 <- 0x7BF260). The all-on run's other gap samples: GPU/d3dx11 2, asset file loads (0x6B6550 /
  0x5BFD50) 2, allocator 1.
- **Refuted:** Lua handlers (the `lua51_Win32` values in the raw stacks are stale slots of the Lua UI contexts that
  ran at the same stack depth just before), the DLL (one B4 sample was a queue-B listener inside a gameplay-DB
  query), City/Unit view systems (not seen in samples; n=1 log comparisons put Unit ~2.5 s and City ~2 s of the gap,
  noise ~2 s [L, weak]). Ruled out statically: DLL deserialisation (done before LoadGameState),
  GameViewState::StartupGame and StartInitStuffOnMap (own timers), `LoadScreenClose`.
- Farm fields, not route curves or improvement models, are the bulk. The layout is per farm and serial on the main
  thread (G88 capture: 121-235 ms per farm group); a lever, if Landmark stays in the EXE, is the farm art-field
  layout data, not the Lua UI [I].

Method: the EXE has no frame pointers, so chains come from a raw stack scan (`dps @esp`) kept to EXE code
addresses and checked against Ghidra's caller graph; `kb` is unreliable for EXE frames.

## 2. Log evidence, B4 run (2026-11-09; seconds are log timestamps)

| Log time | Source | Event | Duration | Owner |
|---|---|---|---|---|
| 970871.437 | stopwatch | first line: Discovering Base Game Map Scripts; CreateCoreDatabase 0.58 s | - | EXE |
| 970872.671 | stopwatch | `cvAudioSystem::Startup` | 0.21 | EXE |
| 970875.437 | stopwatch | New Localization Code (merged loc DB 2.10) | 2.66 | EXE |
| 970876.671 -> 970886.875 | stopwatch | boot `SetActiveDLCandMods` (no mods): Rollback Database 3.20, Reload GameCore DLL 2.67 (incl. Validate FK 2.23), Cache Game Database Data 2.63 | 10.21 | EXE / DLL |
| 970884.031 | net_message_debug | `App Version: 403694 FINAL_RELEASE, GameCore: {83F34FF0-3D60-43E7-AEDB-82C79867EE3F}` (base-game DLL) | - | - |
| 970891.406 | stopwatch | `LoadUnitLibraries via Database` (inside Unit_System::Startup) | 4.48 | EXE |
| 970897.546 | system_init | app Startup systems: DistanceFog 2 ms, ColorKey 46, GameSplineDatabase 6, RiverVisSystem 0, **Unit_System 4,494**, Arrow_System 10, **LandmarkSystem 5,567**, TerrainSystem 454, LeaderHead 3, AudioSimSystem 0 | ~10.6 | EXE |
| 970897.75 | stopwatch | Discovering Base Game Maps (main menu) | - | - |
| 970898.109 -> 970929.765 | stopwatch | `ActivateModsAndDLCForEnabledMods` / `SetActiveDLCandMods`: Rollback 3.63, Parse Map Scripts 1.03, Import Mod Files 0.33, OnModActivated actions (mod SQL/XML) 3.06, Notify Loc DB 1.25, Reload GameCore DLL 0.05, Validate FK 5.65, **Cache Game Database Data 8.71**, Parse Map Scripts 1.02, **LoadUnitLibraries 5.46** (the `ReloadUnitSystem` restart), audio reload | **31.66** | EXE 23 / DLL 9 |
| 970907.687 | net_message_debug | `GameCore: {BF9BF7F0-E078-4D4E-8A3E-84712F85AA2B}` (the VP DLL now loaded) | - | - |
| 970929.765 | stopwatch | `(Lua) CanLoadSavedGame` | - | - |
| 970932.281 | net_message_debug | `NetRegister` | - | - |
| 970943.296 | net_message_debug | `Net SEND NetLoadGame` size=2,228,226, chunks=1,537; RECV at 970943.390 | - | EXE |
| 970944.343 | net_message_debug | `changeNumGameTurnActive ... [loading save game]` | - | DLL |
| 970948.015 | system_init | `RoadLayout::GameView::Startup` 12 ms, `DynamicGeometryVisSystem::GameView::Startup` 0, `LoadGameState::GameViewState::Startup` 26 ms | - | EXE |
| 970955.734 | system_init | **`StartInitTerrainFromGameplayMap()` 7,703 ms** | 7.7 | EXE |
| 970957.531 | system_init | `GameViewStateLuaContextInit::StartupGame()` 1,717 ms, then per-system StartupGame lines (all 0-48 ms) | 1.7 | EXE |
| 970957.5 -> ~970975.6 | - | **gap**: LandmarkSystem farm-field layout in queue-B dispatch (section 1.1; was "unattributed" until 2026-11-10) | ~18 | EXE (Landmark) |
| 970977.390 | system_init | `LoadGameState::GameViewState::StartupGame()` 1,798 ms; `LoadGameState::StartInitStuffOnMap()` 195 ms (both run before the gap; their lines wait for the next 4 MB pump) | 2.0 | EXE |
| 970984.625 | system_init | `CreateTreeShadows()` 0 ms, `StartGPUInitWork()` 3 ms | - | EXE |
| 970986.734 | net_message_debug | `NetPlayerReady(Player=0, count=1 / 1)`; system_init `NetPlayer::IAmReady` 970986.750 | - | - |
| 970987.265 | Lua.log | `TopPanel: Finished loading EUI top panel` | - | - |
| 970988.250 | system_init | `WaitForNetPlayers()` 0 ms; **`LoadGameState_TOTAL()` 38,963 ms** | 39.0 | - |
| 971107.046 | net_message_debug | first `UpdateMoves()` (after the probe's "Continue" click, ~240 s after launch) | - | DLL |

The log span from the first stopwatch line to `LoadGameState_TOTAL` is 116.8 s. Adding the pre-log window (16.0 s)
and APP.log -> first stopwatch line (~10 s) gives LOADTIME.md's 142.77 s [L]. (Corrected 2026-11-10; replaced: "the
note's 128 s adds the EXE's start-up before the first stopwatch line and ~3 s of automation".)

A5 run (FluidFOW on, 11 keys), same phases: boot `SetActiveDLCandMods` 12.44 s, `LoadUnitLibraries` 4.56 s,
Unit_System::Startup 4,572 ms, LandmarkSystem::Startup 5,069 ms, TerrainSystem::Startup 438 ms, mod activation
31.97 s (Validate FK 6.90, Cache Game Database Data 8.68, LoadUnitLibraries 5.65), `StartInitTerrainFromGameplayMap`
9,180 ms, `GameViewStateLuaContextInit` 1,674 ms, `GameViewState::StartupGame` 1,838 ms, `LoadGameState_TOTAL`
41,221 ms. With FluidFOW on, `Overlay_System`, `FluidFOW_System` (76 ms), `MiniMap_System` and `StrategicView`
(67 ms) Startup lines also appear, plus `FluidFogSystem::StartupGame` 34 ms.

Memory during the load (`result.json`, private / WS MB sampled every ~6 s): B4 rises from 166 at 8 s to a peak of
1,601 at 151 s and settles at 1,537-1,540 on the load screen; A5 peaks at 1,917 at 150 s and settles at 1,865.

## 3. The load pipeline inside the EXE (`stubs/coupling.md` section 2.2, [C])

`LoadGameState` vtable 0xA20EBC (+0x04 Enter 0x591200, +0x08 Exit 0x494160, +0x18 StartInitStuffOnMap 0x42FCC0,
+0x1C StartInitTerrainFromGameplayMap 0x60ABB0, +0x20 Update 0x403170 per LOADTIME.md), `LaunchNextStage` /
`Update` 0x403170 (state jump table 0x403684):

| Stage | What runs | Gated? |
|---|---|---|
| 1 | 0x591200 > **MapSetup 0x535BF0**: hex/world matrices, extents, `[0xD90FC4] = &grid` | no |
| 1 | `GVS::Startup` 0x6436A0 > GameView Startup 0x623170 | per bit |
| 2 | **StartInitTerrainFromGameplayMap 0x60ABB0** (vtable +0x1C, slot 7), ends with `TerrainSystem::CreateTerrain` 0x709B50 at 0x60BBD1 | **no** |
| 4 | `GVS::StartupGame` 0x4686C0 (camera init, then per-system StartupGame gates; ends by pumping queue B and the 4 MB queue) | per bit |
| 4 | **StartInitStuffOnMap 0x42FCC0** (vtable +0x18): posts per-plot messages (improvements, resources, cities) to queue B, then message 0x72 with byte 9 = 1 ("map ready", 0x43CE80); sets state 5 | no |
| 5 | wait until queue B is empty (0x4FB9F0); this is where the Landmark farm-field layout runs (section 1.1) | no |
| 8 | CreateTreeShadows (0x47C610, 0x546110), StartGPUInitWork 0x6BB810 (calls UpdateViews) | no |
| 10 | first world update 0x6FBA80 (LOADTIME.md: state 10 = IAmReady, 11 = WaitForNetPlayers) | per bit |

`terrain.md` labels the dispatch differently: `LoadGameState::Update` slot 8 (0x403170), state 2 -> 3 calls
`vtbl[7]()` = StartInitTerrainFromGameplayMap and logs `"StartInitTerrainFromGameplayMap()"`; slot 4 = load-screen
terrain draw 0x70CBC0. CreateTerrain loads TerrainDef.csv and the lighting DDS files, initialises the GridHeightmap
(0x709D6D), runs HexPatternMatcher / BlendTerrainHeightmap (0x412680 > 0x5FC1C0 > 0x46AEF0, pieces in
`assets\terrain\pieces`), builds the river mesh (0x6D51D0) and registers Terrain's own event handlers (0x5BCEF0).
(LOADTIME.md's byte offsets +0x1C / +0x18 for the two vtable entries agree with slot 7 / slot 6 here.)

## 4. Two findings that matter for the shell

- **The terrain bake ignores the Terrain switch.** `StartInitTerrainFromGameplayMap` (and inside it CreateTerrain)
  runs whatever bit 0 is: it appears in B4 (7.7 s) as in A5 (9.2 s), and with `Terrain = 1` it runs on a never-started
  TerrainSystem and crashes on the first marsh (VA 0x6B5B3C). It also fills the heightmap that units, cities, flags,
  banners and picking need. Only Terrain option c3 (skip the bake, 15-30 h) would remove these 7.7-9.7 s, and then
  the heights must come from the server (`world-systems-and-stubs.md` section 5).
- **Landmark off saves 30.8 s of the launch** [L] (148.24 -> 117.42 s process start -> WaitForNetPlayers, unsampled
  B4 vs B4 + Landmark, same night 2026-11-10; startups -5.7, gap -18.9, tail -8.0): the farm-field and route decal
  layout is skipped (its builders are Landmark GameView listeners), the dark red patches of B4 vanish, and
  `queueguard.log` shows no drop line. (Corrected 2026-11-10; replaced: "cut the load from ~2:47 to 2:08", a 39 s
  difference from STUBS.md whose 2:47 reference was the 11-09 B4 live run with EngineAllocTrace on, LOADTIME.md
  section 1.) On late loads of a big game the same decal layout is what stalls the main thread: on the GameId 88
  turn-280 load, 169 of the 196 s of overflowing records were farm-field layout
  (`message-queue-and-fixed-buffers.md`).

## 5. What a shell could remove (LOADTIME.md section 3 + stub notes)

> **Corrected 2026-11-10 (LOADTIME.md).** The phase list now uses the measured owners. Replaced rows: "EXE start ->
> main menu 15, no", "Unattributed gap 18-20, unknown until attributed", "GameView StartupGame etc. 12, partly".

Without patching EXE code bytes (config, mod data, install layout):

| # | Change | Kind | Saves per launch (s) | Basis | Notes |
|---|---|---|---|---|---|
| 1 | **`ValidateGameDatabase = 0`** | config.ini key | **~11.7 measured** [L] (boot activation 13.86 -> 7.36, mod activation 31.96 -> 26.73; Validate FK timed at 10.1 / 11.6 / 13.4 s in B4 11-09 / B4 11-10 / B4 + Landmark 11-10, so **~10-14 s** per launch) | 11-10 unsampled A/B, n = 1 | Validation only logs (it reports "Failed Validation" on base `ArtDefine_Landmarks` every run). Whole launch fell only 6.9 s that run because two unrelated phases were slower (noise). Applies to every run the owner makes today, not only the shell. See "Where the key is read" below |
| 2 | **LandmarkSystem off** (with City off, as B4) | config `[DisableSystems]` | **30.8 measured** [L] | 11-10 unsampled A/B | no improvement models, farm fields or route decals: the server must draw them |
| 3 | Unit rendering off (`Unit=1`, City on) | config | 4.5 boot + ~2.5 of the gap [L, n=1] | b4unit Logs | the 5.5-6.4 s reload at activation stays while `ReloadUnitSystem = 1`; combining with row 2 needs a 3-5 h stub (STUBS.md) |
| 4 | `ReloadLandmarkSystem = 0` in the CP, VP, EUI-compat modinfos | mod data | unclear, probably small [I] | the 13-14 s restart block did not shrink with Landmark off | not run (needs a shell copy of the mods or the owner's OK); no longer a strong candidate |
| 5 | City off | config | 2.0 boot + ~2 view build [L, n=1] | B4 vs single-key runs | already in B4 |
| 6 | **VP as a DLC modpack** instead of mod activation | install layout | **~20-27 (estimate)** [I] | stopwatch breakdown: ActivateEnabledMods and the front-end rebuild go; the boot pass grows by VP's SQL/XML ~3, prefetch +2.4, localization ~1 | `ModdedSaves` do not load under the modpack: needs modpack saves or a header conversion (+4-10 h) |
| 7 | Fewer map scripts in the shell install | install data | 1.5-2.5 [L] (three parses 0.76 + 1.02 + 1.01) | stopwatch | a load-only shell needs none |

Config-only total: rows 1 + 2 (+ 5, already in B4) = **~42 s of ~148 s**; B4 + Landmark + `ValidateGameDatabase = 0`
should reach WaitForNetPlayers in ~105-106 s [I: 117.4 - 11.7, not run as one configuration]. Row 3 with its stub
adds ~7 s [I]; the modpack another ~20-27 s [I].

Not reachable without EXE code changes or further identification:

| Phase | s | Removable? |
|---|---|---|
| Pre-log DxDiag system-info report x2 (0x679EA0 from static initialisers 0x9C4EB0 / 0x9C4EC0) | ~12-14 of the 16-17 s pre-log [L, 9 of 11 samples] | needs an EXE patch of the two initialiser jumps (out of scope: CEG); the report feeds the crash handler (0x42DEA0 installed by the next initialiser), so the loss is crash-report detail only [I] |
| DirectShow graph after Lua init (0x6B2B30 / 0x6BB710 -> quartz.dll) | up to ~10 [L] | unknown: which media file it opens is unidentified (Process Monitor needs elevation). If it is a data file, removing or emptying it is a data change [I] |
| FFileIO reads before NetLoadGame (0x820750 / 0x8208B0) | 7-11 [L] | unknown until the file is identified |
| Boot activation (base + DLC) + app system startups | 21-27 | run before any front-end Lua [L]; the DLL's ~5 s (Validate FK, prefetch) only with row 1 / a warm server [I] |
| Mod activation DB build | 32 | the DLL's ~9 s with row 1 and a warm server; the EXE's ~23 s stays (closed CvGameDatabase lib applying mod SQL; the EXE's Lua UI needs that DB) unless row 6 |
| **Caching the built gameplay DB** | would save ~9-10 per activation (2026-11-24, ticket 5035, section 7.3: measured step by step on oldpc's product path, the cacheable steps are 7.70 s of a 26.47 s activation, about 7.3 s net [I]; this row's "~9-10" was an estimate from the 11-09 workstation run) | **needs an EXE hook in SetActiveDLCandMods 0x6B8E50** [I]: the DLL first runs at "Reload GameCore DLL", after the EXE has already rebuilt the DB, and its Database API (Execute, SelectAll, Analyze, ValidateFKConstraints, transactions) cannot replace a build the EXE already did. Out of scope for a config/data-only shell |
| DLL deserialization | 2-3.5 | stays (the server still deserializes) |
| Terrain bake | 7.7-9.7 | only with Terrain option c3 (15-30 h) |
| EUI Lua contexts | 1.8-2.1 | no |
| Faster DLL PrefetchGameData (Buildings 1.11, MinorCivilizations 0.36, Policies 0.26 ...) | 1-1.5 [I] | DLL code (ours), 4-8 h |

**Where `ValidateGameDatabase` is read.** It is a `config.ini` key [L: the 11-10 run with
`ValidateGameDatabase = 0` in config.ini logged no "Validate FK" line; the `vp-dll-dev` skill lists it among
config.ini's engine switches]. The EXE reads it and the DLL asks for it through the engine interface:
`CvDllDatabaseUtility::ValidateGameDatabase` returns at once when `gDLL->ShouldValidateGameDatabase()`
(`ICvEngineUtility3::ShouldValidateGameDatabase`) is false [C: DLL source per LOADTIME.md;
`civ5-headless/engine-contract.md` records the engine answering true in the real game]. The owner's config.ini has it on
today [L: every run logs Validate FK]. Where in the EXE the key is parsed is not recorded [I]; an xref to the string
"ValidateGameDatabase" in Ghidra would settle it, but nothing in the shell plan depends on that.

The load-time note's earlier estimate, **realistic shell floor ~60-80 s** against the pure port's claimed 10-20 s,
is consistent with the above only if the modpack (row 6), the Unit stub and at least one of the three EXE windows
(DxDiag, DirectShow, FFileIO; ~30 s together) are taken [I]; config alone reaches ~105 s.

## 6. The DLL boundary seen from a proxy in the DLL slot (epic 2500, live on c5h-a, 2026-11-10) [L]

Observed by the x86 proxy of epic 2500 (`proxy/`) loaded as the CP mod's DLL in the DX11 EXE (Goldberg instance
c5h-a, CP 151 + VP 17 + EUI, the proxy's `vpproxy.log` of each boot; runs listed in tracker 2500 and
`research/03-dll-engine-boundary/proxy-design.md` section 15). Nothing here was read from the EXE's code; it is what
the EXE did at the DLL's interface.

- **One DLL load per process.** In every run (main menu only, a new game, a save load) the EXE loaded the mod's DLL
  once, after the patched front end activated the mods (about 50-60 s after start), and called `DllGetGameContext`
  once, on its main thread (Win32 start address `CivilizationV_DX11.exe+0x527035`).
- **The EXE Destroys the game context** right after its first round of calls (`Init`, `GetPreGame`,
  `setVersionString`/`versionString`, `Destroy` of the pregame and then of the context). The DLL's
  `CvDllGameContext::Destroy` does nothing (a singleton), so this is harmless in process; a proxy that forwarded it
  lost the context and the EXE crashed (0xC0000005) on its next call (first live boot, 14:44).
- **Threads that call the DLL**: the main thread (everything at boot, front end, loading, the per-frame slot poll and
  UI Lua); the game-core thread, start address `CivilizationV_DX11.exe+0x42A360` (VA 0x82A360; how it is woken for each `Update` and held: `game-core-thread.md`, 2026-11-15), whose first call is
  `ICvGameContext1::GetGame()` (then `Update`); on the huge save, 3-4 more threads started through msvcr90's
  `_beginthreadex` (start `MSVCR90.dll+0x2345E`) whose first call is `ICvGameContext1::GetMap()` (render workers,
  appearing when the map is first drawn; 386 calls in a 101 s AI-turn window). On the Tiny game no worker thread
  called the DLL.
- **The game database is in memory.** At `SetGameDatabase` the engine's connection reports `sqlite_version()` 3.7.17
  and `PRAGMA database_list` `main=''`, `temp=''`, `Localization=<user>\cache\Localization-Merged.db` (attached).
  `cache\Civ5DebugDatabase.db` is therefore not the live database (it is a dump the EXE writes).
- **The EXE writes its own database after the DLL's boot**: when the load screen starts (after its second
  `CacheGameDatabaseData` call) `MapScripts` grows from 33 to 57 rows, `MapScriptOptions` 137 to 249,
  `MapScriptOptionPossibleValues` 587 to 1081 (the map scripts of the mods and DLC scanned for the setup screens).
- **Save loads read the game body through a stream without a size.** `GetSizeLeft()` returns 0x7FFFFFFF and `ReadIt`
  never returns fewer bytes than asked (a read of 1 MB chunks went on to 512 MB and the EXE aborted with the CRT
  "Runtime Error!" box); the pregame and the file header come through streams with real sizes (260 and 218,193 bytes
  for a Tiny turn-8 save of 219,086 bytes). The DLL reads the body item by item through many separate `Read*` calls
  on that one stream.

## 7. The mod activation's steps on the product path (ticket 5035, 2026-11-24)

The Architect's T5 (LD1, LD2; `research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md`):
the database of the active mod set is rebuilt at every launch. This section times that rebuild step by step, so that
the MC can decide whether a cache behind "Full EXE Optimizations" is worth building. Measurement only; nothing was
changed in the EXE.

### 7.1 Method

- **Where and what.** oldpc, the quiet testbed (one quiet headed job, `alone`, the runner's quiet proof), instance `tb1`
  with oldpc's own Steam EXE, **build 4390913** (oldpc has no 26577625; the MC agreed on 2026-11-24, #13221, that
  4390913 is enough here and that a 26577625 check belongs to a later build ticket). The product path of
  `tools/testbed/jobs/product_path.py` (the proxy in the CP mod's DLL slot, server.exe with `[Renderer] ServerRenderer
  = 1`, the composite; config.ini D33 with Overlay on), built from branch `task-5035-modset-activation-steps` on the
  `deps/dll.lock` pin 1b87dd185 (server.exe `ee4040c7...`, proxy `fed5cf65...`, composite `eab7e64d...`). Mods cp, vp,
  eui; config.ini's `ValidateGameDatabase` left as the instance has it (no validation switch set).
- **Launches.** 8 launches in one job, each a cold process start: `frontend.py prepare --mods cp,vp,eui --load` the Tiny
  save `TB-TINY_0008` (SHA-256 `b2ceb663...`; the activation runs in the front end before the load, so the save only
  shortens the run), launch, in game, stop. The first launch follows the instance's idle time (colder file cache).
- **Source of the times.** The EXE's own `stopwatch.log` (rewritten at each launch): each line is a timer that ended,
  with its seconds and its nesting by tabs. The activation is the front end's `ActivateEnabledMods` timer; its steps
  are the direct children of its `SetActiveDLCandMods` timer, the two children of `Activate Mods` (file import, the
  mods' actions) counted on their own. Time inside the activation that no timer covers is "untimed".
  `load.mods_activation_s@1` is the same `ActivateEnabledMods` timer (`perf/profilekit/loadphases.py`).
- **Tools.** Job `perf/evidence/5035/act_job.py` (submitted by `submit_5035.sh`; layout by `build.sh`), analysis
  `perf/evidence/5035/analyze_5035.py <fetched job folder> --json OUT`. The job also lists every `.modinfo` of the
  instance (files, imports, `UpdateDatabase` actions with sizes) and times a stat and a SHA-256 of every listed file
  (what a cache key would cost).

### 7.2 Results [L]

oldpc job `20261124-180130-1a64` (2026-11-24 18:01-18:24 EST, exit 0; 8 of 8 launches in game, every stop rc 0,
server.exe running in game in all 8, `vpproxy.log` "exe build: build 4390913 ... known and supported" in all 8; the
restore and `provision.py verify` 0). Quiet proof: alone, but **undisturbed = false**: 0.141 foreign cores on average
(limit 0.1; Defender 63 s and one svchost 59 s of CPU over the 22 min), so the run records are grade B. The spreads
below show that the disturbance did not move the steps by more than their own noise. Run records (`c5-run/1`,
`load.mods_activation_s@1`, one per launch): `perf/evidence/5035/records/runs/2026-11/20261124T230211Z-oldpc-d30fe4`
... `20261124T232051Z-oldpc-eb5596` (8 folders, `ledger.py validate`: 8 valid). Tables:
`perf/evidence/5035/oldpc-1a64-all.txt` (n = 8), `oldpc-1a64-warm.txt` (launches 2-8), per launch
`oldpc-1a64-analysis.json`, job summary and mods survey `oldpc-1a64-job.json`.

**`ActivateEnabledMods` = 26.47 s median** (launches 2-8: mean 26.62, min 26.35, max 27.71, stdev 0.48, CV 1.8 %);
launch 1 (after the instance's idle time) 28.38 s. All 8: median 26.48, mean 26.84, CV 2.9 %.

| Step (stopwatch timer) | Group | Median s (n = 7, launches 2-8) | min-max | Launch 1 |
|---|---|---|---|---|
| `AfterDeactivateMods - Rollback Database` (the core DB reloaded; of it `LoadCoreDatabase - Attach Loc Database` 2.05, which holds `GenerateMergedLocalizationDatabase` 1.41, the third loc merge of the launch) | rollback | 2.42 | 2.39-2.46 | 3.36 |
| `AfterDeactivateMods - Load DLC` | rollback | 0.115 | 0.113-0.129 | |
| `BeforeDeactivateMods - *`, `Reset filesystem` | rollback | 0.004 | | |
| two map-script parses (base game, then mods) | map_scripts | 1.19 | 1.17-1.26 | |
| `Import Mod Files into filesystem` (1,733 files with `import="1"` in the three .modinfo) | file_import | **0.098** | 0.096-0.101 | 0.17 |
| `Perform OnModActivated Actions`: 417 `UpdateDatabase` files (CP 172, VP 242, EUI compatibility 3; 5.24 MB); the largest single file 0.12 s | mods_actions | **3.15** | 3.00-3.50 | 3.13 |
| `Notify Localization Database Updated` | loc_notify | 0.79 | 0.77-0.91 | |
| `Reload GameCore DLL` (on the patch: the proxy's load, server.exe started, the in-memory DB sent to it) | dll_reload | 1.25 | 1.22-1.29 | |
| `Refresh include() file list` 0.015, `Refresh Gameplay DLL Data` 0.19, `Perform Gameplay Database Post Processing` 0.38 | post_proc | 0.59 | 0.57-0.61 | |
| `Cache Game Database Data` (the DLL's cache pass, run in server.exe on the patch; ticket 5036) | dll_cache | **6.25** | 6.16-6.31 | 6.25 |
| `LoadUnitLibraries via Database` | unit_libs | **4.93** | 4.89-5.25 | 5.51 |
| untimed span after `LoadUnitLibraries`, before `Reload Modded Audio` (the view-system restart of `ReloadUnitSystem` / `ReloadLandmarkSystem`, section 1; it stays with D33's `[DisableSystems]`) | rest | **5.33** | 5.21-5.71 | 5.49 |
| other untimed spans and wrappers (`Get Enabled Mods`, the remainder of `Activate Mods`, ~0.13 before the rollback, ~0.11 at the end) | rest | 0.35 | | |
| `Reload Modded Audio` | audio | 0.011 | | |
| **total** | | **26.47** | 26.35-27.71 | 28.38 |

Per group (n = 7 medians; the CV of every group with more than 0.5 s is at most 6 %): rollback 2.54, map scripts 1.22,
file import 0.098, mods' actions 3.15, loc notify 0.79, DLL reload 1.25, DLL cache 6.25, post processing 0.59, unit
libraries 4.93, rest 5.68.

Other facts of the same runs:
- **No `Validate FK Constraints` timer in the mod activation** although config.ini has `ValidateGameDatabase = 1`; the
  boot activation (the base DLL in the EXE) has two (1.86 + 1.87 s). On the patch `CacheGameDatabaseData`, which calls
  `ValidateGameDatabase` first (DLL source `CvDllDatabaseUtility.cpp`, pin 1b87dd185), runs in server.exe, so any
  validation is inside the 6.25 s and is timed only by the server [I: not split here]. The workstation sample of
  c5h-i (an older log of that instance, configuration not recorded, not for timing) has none in the mod activation
  either.
- **The patch already ships the built database as an image.** At `SetGameDatabase` the proxy saves the EXE's
  in-memory database (`BinaryIO::Save` snapshot, 832 tables, 43,600 rows) in 0.154 s and server.exe loads an
  identical copy (`vpproxy.log`, `server.log`, launch 3); after `CacheGameDatabaseData` both sides fingerprint the
  database in 0.075 s. A cached image would load at about that cost [I: the EXE's side would need its own load into
  its connection].
- The boot activation (base + DLC, before the menu) is 8.70 s median (8.55-8.91); it is not part of T5.
- The user cache folder holds `Localization-Merged.db` 53.1 MB (rewritten at each launch), `Civ5DebugDatabase.db`
  6.8 MB (the EXE's dump of the built DB), `Civ5ModsDatabase.db` 1.2 MB, `Civ5CoreDatabase.db` 2.3 MB.
- A cache key's cost, measured in Python 3.8 on oldpc after the launches: a stat of the 2,175 files the three
  .modinfo list 0.076 s; reading and SHA-256 of all of them (241.5 MB) 1.52 s the first time, 0.67 s warm. The 417
  action files are 5.24 MB, so their hash is about 0.02 s [I: scaled by bytes from the warm figure].
- On the workstation (c5h-i, an older log, not for timing) the same activation took 36.3 s with the same shape
  (actions 3.93, cache 8.64, unit libraries 6.66, untimed 7.53): oldpc needs 20-29 % less time on each large step (26.47 / 36.32 = 0.73 for the whole).

### 7.3 What a mod-set cache could keep (T5), and what keys it [I]

What a cache of the built database can replace is the work that only turns the same inputs into the same database
rows; what it cannot replace is work outside the database (the DLL's own memory, the unit art libraries, the view
systems, the file system registration the game reads files through later).

| Cacheable step | Median s | Keyed by |
|---|---|---|
| mods' actions | 3.15 | the ordered list of active mods (id and version: CP `d1b6328c...` v151, VP `8411a7a8...` v17, EUI compatibility `24923240...` v1), the bytes of every `UpdateDatabase` file in action order (417 files, 5.24 MB), and the database they run on (below) |
| rollback: core DB reload + the merged localization DB (`Attach Loc Database` 2.05, of it the merge 1.41) | 2.07 | `Civ5CoreDatabase.db` and the DLC set's databases and `Localization-*.db` (bytes or size + time), the EXE build, the language |
| Load DLC | 0.115 | the DLC set (ids and their database files) |
| Notify Localization Database Updated | 0.79 | the mods' text files (they are among the action files) + the merged loc DB above |
| two map-script parses | 1.19 | the map scripts of the base game, the DLC and the mods (file list and bytes) |
| Perform Gameplay Database Post Processing | 0.38 | the database after the actions (follows from the keys above) |
| **sum** | **7.70** | |

Costs that remain: loading the image (about 0.2-0.3 s by the proxy's snapshot figures above), the key (a stat-based
key of the action, loc and map-script files about 0.02-0.08 s; a full hash of the 417 action files about 0.02 s
warm), and on a miss the stock route plus the write of a new image. **Reachable: about 7.3 s of the 26.5 s
activation per launch on oldpc (27.6 %), about 10 s at the workstation's slower step times** [I: the oldpc sum is
measured, the replacement costs are inferred from the proxy's snapshot, the workstation figure is scaled by 1 / 0.73]. This agrees with the Architect's "7 to 10 s" (LD1).

Stays with any such cache: the DLL's cache pass 6.25 s (it fills the DLL's memory; ticket 5036 times it), unit
libraries 4.93 s, the untimed view-system restart 5.33 s, the DLL reload 1.25 s (the proxy and the server's start),
refresh and file import about 0.3 s: 18.1 s of 26.5 s.

**LD2, a stat per mod file: not worth a change [L].** The whole file import of 1,733 files is 0.098 s (about 57 µs a
file, launch 1 0.17 s), and a stat of all 2,175 listed files from Python is 0.076 s. Nothing larger than 0.1 s per
launch can be saved there.

A verify mode (T5) costs a full stock activation plus a table-by-table compare: the proxy's fingerprint of 832 tables
took 0.075 s, so the compare is cheap next to the rebuild [I].

### 7.4 Not measured

- Build 26577625: nothing here was run on it (oldpc has only 4390913). The MC decided on 2026-11-24 that 4390913 is
  enough for this measurement; the build ticket confirms on 26577625.
- Stock (no patch) on oldpc: the activation was timed only on the product path. Only `Reload GameCore DLL` and
  `Cache Game Database Data` can differ between the two by construction (the proxy, server.exe).
- Where in the untimed 5.33 s the time goes (which view systems restart): no system_init line falls inside the
  activation's window, so the logs do not split it.
- Other mod sets (more mods would make the actions step and the cache's gain larger: the Architect's "more mods, more
  gain" is not measured).
