# Interfaces and traffic

What crosses between the EXE and the DLL, and how often, in a live late game.

## 1. The boundary's shape

Three directions, two threads.

| Direction | Surface (2026-11-09 study) | Nature |
|---|---|---|
| Engine -> DLL (ICv* interfaces, implemented by the DLL's `CvDll*` wrapper classes) | 55 interfaces, 1,106 methods (all 1,106 instrumented) | Synchronous; mostly wrapper lifetime and tiny getters |
| UI Lua -> DLL (Lua libraries the DLL registers) | 3,481 methods | Synchronous reads; a script cannot proceed without the answer |
| DLL -> engine (`gDLL` = `ICvEngineUtility*`, `DLLUI` = `ICvUserInterface*`, script system) | 164 distinct methods used, 421 covered by generated forwarding proxies | Mostly fire-and-forget notifications, lock plumbing, a few selection getters and Lua hook results |

Other counts in the sources (different dates and counting bases; see "Contradictions" at the end):

- 2026-09-03 verdict (superseded in part): **88 ICv* interfaces / 958 virtuals / 4,417 Lua methods**.
- 2026-11-01 unit-test study: `ICvEngineUtility4` has **258 virtuals, 163 used** by the DLL. The contract a host must
  satisfy also includes `ICvUserInterface2`, the script system, a `Database::Connection*`, the three closed Firaxis
  DLLs, and **22 C exports** the DLL looks up in the EXE with `GetProcAddress(GetModuleHandle(NULL))` (5 of them crash
  in `DllMain` if missing: log manager x2, file system, FString init data x2).

Threads (headless `engine-contract.md`, measured in traces of the real game, 2026-10-25 runs):

| Thread | What it does at the boundary |
|---|---|
| Main / UI thread | Boot, front end, new game (map generation, `Init2`), shutdown; in the turn loop: rendering getters, `UpdateTestEndTurn` (human turn), `UnitIsMoving`, `Map::UpdateLayout`, Lua object pushes, net-message delivery (`Response*`), tuner/UI Lua |
| Game-core thread | Only `CvDllGame::Update` at depth 0 (plus wrapper `Destroy`); AI, autosave `Write`s, hooks and `send*` are nested inside it |
| Render workers | Only terrain/feature/plot getters (`GetEffectTypeTag`, `GetFeatureType`, ...) |

`Update` runs on the core thread **while the UI thread keeps calling the DLL**. The DLL picks its pathfinder instance
and trade-path caches by the answer to `IsGameCoreThread`, so a server must emulate the thread role. Each `Update` is
wrapped in `GetGame`/`Destroy`; the wrapper acquires and releases the game-core lock, which the EXE owns. One `Update`
processes one player; the real game waits a median **~105 ms between Updates** (2.6 s of a ~3 s early turn is idle
pacing).

## 2. The benchmark setup

Measured 2026-11-09 03:00-03:45 with an instrumented release DLL.

| Item | Value |
|---|---|
| Save | `ModdedSaves/single/VP8P-HUGE_0350`: Huge (128x80 = 10,240 plots, Continents), turn 350, Modern era |
| Game state | 8 majors, 28 players alive, 845 units, 124 cities; human Ramesses II (79 units, 13 cities) |
| Mods | CP + VP + (3a) EUI |
| Frame rate | uncapped, ~155 fps |
| Frame proxy | `CvDllGame::GetGameTurn`, called 9.47 times per frame (no Present count available) |
| "Slot" | a fixed 16.67 ms window |
| Machine | i9-13900KS |

## 3. UI Lua -> DLL (UI thread)

| Scenario | secs | fps | calls/s | per frame | per slot median / p95 / p99 / max | distinct methods | pushes/s (new objects) | returns a value |
|---|---|---|---|---|---|---|---|---|
| a idle | 30.4 | 156 | 38 | 0.25 | 0 / 3 / 3 / 393 | 56 | 18.5 (1) | 100% |
| b pan + zoom | 30.4 | 154 | 910 | 5.9 | 0 / 16 / 613 / 619 | 177 | 21.7 (143) | 100% |
| c city view (Thebes, pop 35) | 15.3 | 155 | 80 | 0.5 | 0 / 3 / 3 / 651 | 61 | 38.2 (5) | 100% |
| d tech tree | 15.4 | 141 | 0 | 0 | 0 / 0 / 0 / 0 | 0 | 0 | - |
| e1 tooltips (top panel, map, units) | 20.4 | 156 | 172 | 1.1 | 0 / 3 / 28 / 656 | 290 | 27.6 (18) | 98% |
| e2 diplomacy overview | 15.3 | 156 | 43 | 0.3 | 0 / 3 / 3 / 305 | 52 | 23.4 (1) | 100% |
| e3 economic overview | 15.3 | 155 | 0 | 0 | 0 | 0 | 0 | - |
| f1 AI turn 350->351 | 127.1 | ~5 | 9,519 (1.21 M per turn) | - | 0 / 31 / 4,303 / 53,062 | 408 | 649 (2,525) | 99.7% |
| f2 AI turn 351->352 | 58.3 | ~10 | 18,377 (1.07 M per turn) | - | 0 / 2,000 / 6,425 / 30,659 | 383 | 1,455 (2,398) | 99.6% |

"Returns a value" = the C function returned at least one result, counted in the thunk.

Screen-open bursts (each screen above was measured from 4 s after it opened; this table is the opening itself):

| Transition | Lua calls | Busiest slot |
|---|---|---|
| open city view | 26.8k | 3.4k |
| close city view + open tech tree | 35.1k | 7.3k |
| open diplomacy overview | 23.8k | 14.1k |
| open economic overview | 6.5k | 3.2k |

Steady state is 0.3-6 calls per frame; bursts of ~600 calls in one slot every few seconds.

Top methods:

- Idle: `Map.GetPlot` 553, `Plot.CalculateYield`, `Plot.GetYieldWithBuild`, `Plot.GetBuildTime`, `TeamTech.HasTech`, `Plot.CanBuild`.
- Pan/zoom: `Unit.IsHasPromotion` 17,179, `Plot.GetBestDefender`, `Unit.IsEmbarked`, `Unit.GetDomainType`, `Plot.GetX`/`GetY`, `Map.GetPlot`, `Unit.GetBaseCombatStrength`, `Plot.IsHills`, `Unit.CanEverRangeStrikeAt`.
- AI turn f2: `Unit.IsHasPromotion` 303,770; `Player.IsMinorCivDisplayedQuestForPlayer` 93,100; `City.IsHasBuilding` 69,375; `Game.GetCorporationFounder` 40,288; `Map.GetPlot` 27,222; `Plot.GetPlotCity` 26,870; `City.GetOwner` 25,403; `City.IsFranchised` 15,282; `Game.CanHandleAction` 15,204; `Player.IsMinorCiv` 12,128; `Deal.IsPossibleToTradeItem` 11,531; `Game.GetResourceUsageType` 11,340.
- Across both AI turns `Unit.IsHasPromotion` is 300-440k per turn.

Instance pushes per AI turn: Plot 37k, City 32k, Unit 11k, League 5k; only ~2.4k created new handle objects. Lua
running on the core thread (GameEvents listeners) made 1.4-5.3k calls per turn.

Not counted: GameInfo, Locale, UI, Events and ContextPtr live in the EXE and never cross; `ICvEngineScriptSystemArgs1::Push*`
calls; Lua-channel calls on the core thread. Only DLL-registered Lua functions (the `Method`/`RegisterMethod` tables and
the Cities/Units/Areas iterators) were counted.

## 4. Engine -> DLL (CvDll* wrappers)

Classes: **lifetime** = `Destroy`, `QueryInterface`; **static handle** = a wrapper the client could compute itself
(`GetGame`, `GetPreGame`, `GetPlayer`, `Map::GetPlot`, `Get*Info`); **lookup** = returns an object after a real lookup
(`GetUnit`, `GetPlotCity`); **getter / command** = returns a value / returns void.

UI thread, per frame:

| Scenario | calls/s | per frame | lifetime | static handle | getter | lookup + command | per slot median / p99 / max |
|---|---|---|---|---|---|---|---|
| idle | 150,878 | 968 | 531 | 233 | 202 | 2 | 2,586 / 10.6k / 11.4k |
| pan/zoom | 246,921 | 1,607 | 706 | 348 | 549 | 2 | 2,586 / 21.9k / 44.3k |
| city view | 146,390 | 943 | 523 | 228 | 192 | 0 | 2,544 / 9.4k / - |
| tech tree | 137,821 | 980 | 535 | 236 | 209 | 1 | 1,826 / 12.9k / - |

Top idle calls per frame:

| Class | Calls per frame |
|---|---|
| lifetime | `CvDllGameContext::Destroy` 157, `CvDllPlayer::Destroy` 142, `CvDllGame::Destroy` 87, `CvDllPlayer::QueryInterface` 70 |
| static handles | `GetGame` 87, `GetPlayer` 72, `GetPreGame` 64 |
| getters | `IsHuman` 69, `slotStatus` 63, `GetGameTurn` 9.5, `HasTurnTimerExpired` 6.4, `GetActivePlayer` 5.2, ~5 each for plot/terrain getters |

Panning adds ~60 terrain getters per frame. Render worker threads made only 214 calls in 30 s.

Core thread, per AI turn (f1 / f2): ~169k / 174k calls; lifetime 67k / 70k, static handles 34k / 36k, getters
58k / 59k, lookups 5k, commands 4.9k / 4.6k. These are engine callbacks made while the DLL is running, e.g.
`CvDllUnit::GetOwner`/`GetID` ~12k each, `Plot::GetPlotIndex` 7k.

## 5. The 63-player-slot poll and the game-core lock

- The EXE walks **all 63 player slots every frame** through wrapper objects: `GetGame`/`GetPreGame`/`GetPlayer`,
  `QueryInterface`, `IsHuman`/`slotStatus`, `Destroy`. REPORT.md: **about 830 of the 968** inbound calls per idle frame.
- Each `CvDllGame` and `CvDllPlayer` wrapper (only these two; corrected 2026-11-10) takes the game-core lock through an
  outbound `GetGameCoreLock`/`ReleaseGameCoreLock` pair: **317 DLL -> engine calls per frame** on the UI thread outside
  AI turns, all lock calls except ~1 per frame of `IsSPAutoEndTurnEnabled` - that is **~158 pairs** (86.5 `GetGame` +
  72 `GetPlayer` constructions per frame), not 317 pairs. Most of them are **recursive re-entries** by a thread that
  already holds the lock; only about **2 per frame** in the slot poll are first-level acquisitions that can wait
  (correction below).
- During an AI turn the core thread holds the lock and the UI falls to **5-10 fps** (2026-11-09 benchmark).
  **Corrected 2026-11-10 (measured live, section 9.1):** the UI's first-level lock waits in an AI turn total about
  0.1 s in 205 s (p99 0.10 ms per frame); the low fps comes from leader screens and UI work, not from lock waits.
  (Replaced: "the UI thread's first-level acquisitions wait on it ... not yet measured".)
- **Correction 2026-11-10 (epic 1300, task 1304; decompile of the DX11 EXE, [C]):** the 317 lock calls per frame are
  mostly **re-entries, not waits**. Only `CvDllGame` and `CvDllPlayer` lock, in their constructors
  (`CvDllGame.cpp:21-33`, `CvDllPlayer.cpp:21-33`, Release-5.4.8); `CvDllPreGame` and `CvDllGameContext` never do. The
  poll is `FUN_006A1DC0`: it creates the active player's `CvDllPlayer` wrapper before the 64-slot loop, keeps it alive
  through the loop and calls `HasTurnTimerExpired` on it every frame, so every per-slot `GetGame`/`GetPlayer`
  acquisition is a recursive re-entry by the lock owner. Under contention the poll waits about **twice per frame**
  (the `GetActivePlayer` wrapper and the active-player wrapper), not 128 times. Per-frame counts match the idle CSV
  (slotStatus 62.5, GetPreGame 63.5, QueryInterface 70, IsHuman 69). `CvGame::hasTurnTimerExpired` (`CvGame.cpp:2062`)
  is not read-only when `GAMEOPTION_END_TURN_TIMER_ENABLED` is on (resets the timer, walks all units), so it needs the
  lock. During an AI turn the UI thread also takes the lock per frame for `CvDllPlayer::GetUnit` (35/frame),
  `IsEverAlive` 21, `IsTurnActive` 14, `GetGameTurn` 9.5 and others [L, f2 CSV]; how many of all these are top-level
  (waiting) acquisitions was not recorded, so which calls actually stall the UI is **not known**. Detail and the
  decompile recipe: `tracker/issues/1304-i0-slot-poll-lock.md` Findings 1-10. (Replaced: "each wrapper takes the lock"
  read as 317 waits per frame.)
- Overall, 55% of inbound calls are `Destroy`/`QueryInterface` and 24% static handles.
- What the client needs instead (REPORT.md bottleneck 1): a mirror of `slotStatus`/`IsHuman`/`GetGameTurn`/
  `HasTurnTimerExpired`/`IsPaused` that the server pushes when something changes.

## 6. DLL -> engine (gDLL, DLLUI, script system)

Per AI turn:

| Class | f1 core | f2 core | f1 main | f2 main |
|---|---|---|---|---|
| plumbing: `IsGameCoreThread`, `Has`/`Get`/`ReleaseGameCoreLock`, `GetScriptSystem` | 1,164,917 | 1,063,792 | 516,764 | 353,806 |
| script: `CreateArgs`/`DestroyArgs`/`CallTestAll`/`CallHook`... | 113,271 | 91,986 | 768 | 21 |
| notifications (void) | 25,175 | 36,775 | 435 | 51 |
| getters whose return value the DLL uses | 16,576 | 20,241 | 50,248 | 45,616 |

Top core-thread calls, f2:

| Kind | Calls |
|---|---|
| plumbing | `IsGameCoreThread` 647,881; `HasGameCoreLock` 220,528; `GetScriptSystem` 97,199 |
| script system | `CreateArgs`/`DestroyArgs` 30,662 each; `CallTestAll` 28,379 |
| notifications | `setDirty` 23,727; `SetSpecificCityInfoDirty` 3,859; `GameplayUnitShouldDimFlag` 2,923; `GameplayUnitTeleported` 1,576; `GameplayFOWChanged` 1,369 |

Outbound calls whose return value the DLL uses: selection reads (`isCitySelected` 7.6k, `IsUnitSelected` 4.9k,
`getSelectionPlot` 4.7k, `GetHeadSelectedUnit`), `GetAdvisor*AttackInterrupt` 1.3k each, every
`CallTestAll`/`CallHook`/`CallAccumulator` (the hook result) and `CreateArgs`, and all the plumbing calls.

Lock and thread-check plumbing is **77%** of the core thread's crossings (`IsGameCoreThread` ~670k, `HasGameCoreLock`
~230k); about 1.1 M per turn; it would be server-local. Script hooks: **30-38k calls per turn return a value**
(projection.txt "script returning calls": 37,693 in f1, 30,662 in f2); GameEvents listeners live in the UI's Lua state.

## 7. Nested re-entrant calls

- `Game.CanHandleAction` (Lua -> DLL) calls `shiftKey`, `isCitySelection` and `GetHeadSelectedUnit` (DLL -> engine)
  about **15k times per turn** on the UI thread.
- The engine calls `GetOwner`/`GetID`/`GetPlotIndex` back into the DLL from inside notifications, **about 60k per turn**.
- Headless traces list further callbacks nested in outbound calls: `CvDllMap::UpdateDeferredFog()` inside
  `GameplayUnit*`/`GameplayCityCreated` notifications (4,999 in a 100-turn A/A run), `CvDllUnit::SetupGraphical()`
  inside `GameplayEraChanged`, the whole `Write` sequence inside `AutoSave`, `RegisterScriptLibraries(L)` inside
  `CreateLuaThread("VP_LUAAPI")`, `CvDllPreGame::autorun()` inside `GetAutorun`.
- REPORT.md: they "complicate the protocol more than they cost in time". The study: each side must service incoming
  requests while waiting for its own answer, and every bug there is a deadlock or a stale read.

## 8. AI-turn wall time and leader screens

| Turn | Wall | Stalled on AI leader screens | Core thread busy | `Update` calls |
|---|---|---|---|---|
| f1 350->351 | 127.12 s | 89.1 s | 25.15 s | 39 |
| f2 351->352 | 58.3 s | 19.9 s | 26.81 s | 36 |

Screens: a peace offer, a missionary demand, a denouncement, answered by hand (Refuse, Very well, Back). Why the
turn stops [C, source at `Release-5.5`, one-off 9086, 2026-11-25]: in single player an AI that opens a leader screen
sets `SetWaitingForBlockingInput` and the rest of its turn (`doTurnPostDiplomacy`: economy, military, cities) waits
for the answer; the deferral Extra that removes the wait is specified in
`research/07-architecture-decisions/deferred-ai-deals-spec.md` (tickets 0786-0787). Almost
nothing crossed the boundary while they were up. Per AI turn the core thread crosses the boundary **1.4-1.5 M times**
against 25-27 s of compute; what really has to cross is 25-37k fire-and-forget notifications plus 80-118k synchronous
calls (0.02 s per turn with spinning, 1.1-1.5 s with events).

## 9. Cheap EXE-side finds

1. **Slot-poll lock.** The per-frame slot poll takes the game-core lock through every `CvDllGame`/`CvDllPlayer` wrapper
   constructor (`CvDllGame.cpp:21-33`, `CvDllPlayer.cpp:21-33`), but nearly all of those are re-entries; it is **not
   established** that the poll is why the UI drops to 5-10 fps during AI turns (corrected 2026-11-10; the 2026-11-09
   statement read "which is why the UI drops to 5-10 fps"). Candidate DLL-side fix: skip the lock for pregame-only reads in the wrapper
   constructors. Needs a safety check before shipping; the study says the pattern fits "the EXE-interop framework
   already in review". REVIEW.md puts it in increment 0 (in place, shippable now). In the out-of-process design the
   contention disappears structurally because the proxy answers slot polls from a local mirror.
   **2026-11-10 (epic 1300, [C]):** skipping the lock in the slot poll alone removes about 2 lock waits per frame, not
   317, and the "pregame-only reads" do not cover the poll: it also calls `HasTurnTimerExpired`, which mutates state
   when the end-turn timer option is on (section 5 correction). A lock-free poll is possible in single-player (an
   option-off fast path plus a whitelist of `GetActivePlayer`/`GetPlayer`/`QueryInterface`/`IsHuman`), but the
   whitelist applies at every EXE call site (~174 callers of the GetGame helper `0x42C640`, not audited) and moves the
   acquisition later (lock-order risk). Expected fps gain during AI turns is small and unmeasured; the next step is to
   measure top-level lock waits per EXE call site in a live run (tracker 1304, options A-E). **Decision D17
   (MC, 2026-11-10): option A**, no lock change ships from epic 1300; a diagnostic build on throwaway fork branch
   `diag/ui-lock-waits` measures UI-thread first-level waits and core-thread hold durations in the live huge-save run
   (task 1305); the measured profile and a recommendation among B (lock-free poll), C (wider whitelist), D (more
   core-thread yields), E (server mirror) are added here, and any fix becomes its own epic.
   The diagnostic exists (2026-11-10, task 1304): fork branch `diag/ui-lock-waits` @ 070d476ed (throwaway), DLL SHA-256
   `BF9E7FB5...EB0F8D` in `<pins_dir>\dlls\diag-uilockwaits-070d476e\`; opt-in `crashlogs\uilockwaits.enabled`
   or `VP_UILOCKWAITS=1`; six `uilockwaits_*.csv` in the cache folder; off and on both play identically to base over
   200 headless turns. Headless only (no UI thread there) [I]: 77% of the core thread's lock-held time is in spans of
   16 ms or more (longest 1.17 s); a UI acquisition at a random moment would wait ~35 ms for the next release, and two
   such waits per frame give ~14 fps, in the range of the observed 5-10 fps. To be confirmed live (task 1305).
   **Measured live 2026-11-10 (task 1305 R2) [L]: the UI thread does not wait on the game-core lock in any
   meaningful amount; the headless inference above is superseded.** Setup: Goldberg instance c5h-b, copy of
   VP8P-HUGE_0350 (Huge, t350), DLL `cecf5495...` (instance-aware-instruments 440878983 + diag/ui-lock-waits),
   marker on, census off, harness cloaked; analyzer `1304-uilockwaits/analyze_uilockwaits.py` (team notes folder),
   output `<pins_dir>\evidence\1305\live\R2\analysis-*.txt`.

   | phase | wall | UI frames (slot polls) | fps | frame p50 / p99 ms | UI lock wait total | core held |
   |---|---|---|---|---|---|---|
   | idle human turn | 60 s | 11,549 | 192 | 5.2 / 7.9 | 0.0 s | 0.1 s |
   | pan (posted arrow keys) | 30 s | 5,025 | 165 | 6.0 / 10.8 | 0.0 s | 0.0 s |
   | city screen | 34 s | 5,209 | 154 | 6.5 / 9.4 | 0.0 s | 0.1 s |
   | AI turn 350->351 incl. ~3 min of leader screens | 205 s | 1,665 | 8.1 | 6.6 / 122 | **0.1 s** | 17.4 s |
   | idle after the AI turn | 30 s | 3,261 | 109 | 10.0 / 12.4 | 0.0 s | 0.0 s |

   - In the AI turn only 2 UI waits were >= 1 ms: 91.2 ms (the core let go in the `PlayerCanEverResearch` Lua hook)
     and 4.7 ms (`CityEventCanTake`), both at EXE call site 0x0040C165 (the GetGame+GetPlayer helper; callers
     0x0069E31A, 0x0045307C). 10 more waits of 53-202 us, mostly when the core did not hold the lock at all.
   - Core-thread holds in the AI turn: 38,156 spans, 17.4 s of 205 s; longest 2.92 s inside the `PlayerCanTrain` Lua
     hook; spans >= 1 ms dominated by Lua hooks (`CanStartMission` 4.4 s in 251 spans, `PlayerCanTrain` 3.5 s,
     `CanDeclareWar` 1.5 s, `UnitSetXY` 1.2 s) and tactical pauses.
   - The low average fps in an AI turn is the leader screens (the slot poll does not run while one is up; one frame
     lasted 90.9 s) and UI work, not lock waits (p99 lock wait per frame 0.10 ms).
   - **Recommendation (D17 deliverable):** do not pursue B (lock-free poll) or C (wider whitelist): they would save
     about 0.1 s per AI turn here. D (more core-thread yields) has no target while the UI is not waiting. E (the
     server's local mirror) stays the structural answer. Caveats: one AI turn on one save; the 2026-11-09
     benchmark's 5-10 fps counted leader-screen time and ran with per-call counters on every crossing.
2. **~105 ms pacing.** Headless traces show the real game waits ~105 ms (median) between `Update` calls; 2.6 s of a
   ~3 s early turn is idle pacing. The sources flag it but propose no fix.

## 10. The instrumentation

| Piece | What it did |
|---|---|
| `CvGameCoreDLL_Expansion2/BoundaryCounters.{h,cpp}` | Per-call counters: a thread-id read, two interlocked increments and a QPC read per counted call (~30-50 ns, **estimated, not measured**: ~0.05 ms per frame, ~0.2 s per AI turn) |
| `Boundary::HitIn(n)` in all 51 `CvDll*.cpp` | Engine -> DLL counts; method names in `BoundaryInNames.gen.h` |
| `BoundaryProxies.gen.h` | Generated forwarding proxies over 421 DLL -> engine methods |
| Thunks in `Lua/CvLua{Area,City,Deal,League,Player,Plot,Team,TeamTech,Unit,Game,Map}.cpp` and `Lua/CvLuaScopedInstance.h` | Lua -> DLL counts, pushes, "returns a value" |
| `BoundaryLuaChannel.cpp` | Ported Lua channel used to drive the live game |
| Hook-ups in `CvDllContext.cpp`, `CvGame.cpp`, `CvDllDatabaseUtility.cpp`, `CvGameCoreDLLPCH.h`; `build_vp_clang.py` | Wiring and build |
| `gen/gen_out_proxies.py`, `gen/insert_inbound.py` | Generators for the proxies and the inbound insertions |

Location: **uncommitted on branch `boundary-benchmark`** of the Community-Patch-DLL checkout, cut from
`instant-yield-triggers` plus the `engine-queue-guard` commits 2f954ff99 and ac7afabc2 (`CvGlobals.cpp` conflict
resolved). Data and tools: `experiments/boundary-benchmark/` in that checkout (`results/results.json`,
`results/methods_<scenario>.csv`, `results/projection.txt`, `run2_data/timeline.csv`, `ipc/`, `shots/`, `tools/`, `gen/`).
The generated proxies and name tables are a natural starting point for package C's "generated interface proxies"
(inference, not stated in the sources).

Caveats (REPORT.md): one save, one machine; no Standard/Large comparison (`Catherine_0062` hit
`Building_ThemingBonuses.ConsecutiveEras`, missing from the installed 5.4.8 DB; the Oda saves need the League Project
Alerts mod); the first run was discarded (the Lua channel created its thread before `GameInfoTypes` was filled); pan/zoom
was synthetic input; the end-turn script dismissed blocking notifications and zeroed unit moves; the original DLL was
restored afterwards (sha256 262aee88c5312c4b...).

## Contradictions and uncertainty

- Interface counts: 88 ICv* / 958 virtuals / 4,417 Lua methods (2026-09-03) vs 55 interfaces / 1,106 methods / 3,481 Lua
  methods (2026-11-09). The sources do not reconcile them; the 2026-11-09 numbers are what the instrumentation covered.
- Slot-poll size: REPORT.md says ~830 of 968 inbound calls per frame; the study says "about 1,300 crossings per frame are
  the EXE constructing and destroying player wrappers", which matches the **total** per-frame crossings (1,286 in
  projection.txt = inbound + lock pairs + Lua) rather than the slot poll alone.
- Lock count wording (2026-11-10): "317 lock pairs per frame" (the 1300 brief, the handoff section 2.4, the
  architecture memory note) is 317 lock **calls** = ~158 pairs, and nearly all are re-entries, not waits (section 5).
- Nested callbacks: the architecture memory note says "~170k engine callbacks nested in notifications" and the study
  table says "~170k GetOwner/GetID/GetPlotIndex from inside notifications"; REPORT.md says 169-174k is **all**
  core-thread engine -> DLL calls per turn and only ~60k are nested in notifications.
- All "per frame" figures inherit the 9.47-`GetGameTurn`-calls-per-frame proxy; there was no Present count.

Sources: `research/sources/boundary-benchmark/REPORT.md`, `projection.txt`, `results.json`;
`research/sources/thin-client-study.html` (sections 3, 5, 7); `research/sources/memory-notes/civ5-64bit-architecture-options.md`,
`dll-unit-test-study.md`; `research/sources/REVIEW.md` sections 2, 6; `~/Documents/civ5-headless/docs/engine-contract.md`
sections 1, 2, 6 (read in place, not copied into sources).
