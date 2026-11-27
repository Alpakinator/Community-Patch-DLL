# Hidden embarks as product (ticket 0573, D193): the design

Ticket 0573, step 1. Written 2026-11-18 by the implementer of 0573 (account B). This is the design for the MC. Step 2, the
build, is a separate ticket that the MC opens when it answers yes to this design. Nothing here is built. Marks: `[C]` read in
the DLL's source at the current pins (x64-batch4 `c29442e8c`, i0-ship-batch4 `e96457fd5`) or in this repository's code;
`[L]` measured live (the runs named); `[I]` inferred.

The prototype, its measurements and its pictures are in `turn-time-study.md`, section "Embarks the human cannot see"
(ticket 4676). This file does not repeat them. Evidence for this file is in `perf/evidence/0573/`.

## 1. What the product does

The behaviour is the prototype's (fork `x64-proto-0437-embark`, build 2 `3fd5255a1`), with the changes in section 4:

- `CvUnit::embark` and `CvUnit::disembark` do not post `GameplayUnitEmbark` when two things hold: no watching human can see the
  plot (`CvPlot::isVisibleToWatchingHuman`, the rule `setXY` already uses to choose animated moves), and the unit's team
  has no human player.
- For each unit whose call was held back, a transient side table records the model state the EXE still has. This table is
  never serialised.
- When the unit comes into view, the DLL posts `GameplayUnitEmbark(unit, isEmbarked())`. This is the flush. A flush always
  posts, even when the EXE already has the right state: the EXE then rebuilds the model with the unit's current art, as
  stock does at every call. Four points trigger a flush:
  - `setXY`: before the move is shown, when a watching human sees the new plot;
  - `CvPlot::changeVisibilityCount`: when a plot turns visible, for the units on it;
  - `CvCombatInfo::setVisualizeCombat(true)`: for the attacker, the defender and the interceptor;
  - a safety sweep at `CvPlayer::setTurnActive(false)`. A unit that the sweep finds still held back on a watched plot is
    a **missed reveal**: the sweep counts it and posts it.
- The entry is dropped when the unit is killed.

What the player sees: nothing changes out of sight. A unit that comes into view shows its true model at its true plot. The
known difference from stock (gap (a)) is the facing of its formation.

## 2. Product form on both pins

### 2.1 Fork branches and the define

- **Branches** (new branches only; never `master`, `i0-ship`, `x64` or an upstream-tracking branch):
  - `hidden-embark-x64` from `x64-batch4` `c29442e8c`;
  - `hidden-embark-i0` from `i0-ship-batch4` `e96457fd5`.

  Each branch gets one commit. The two commits have the same patch-id (the batch-4 practice, `deps/dll.lock`).
- **The prototype applies to both pins as it is** `[C, 2026-11-18]`. Its diff applies cleanly to both pins' files, with
  line offsets only (+7 in `CvPlayer.cpp`, +5 in `CvPlot.cpp`). The check: `perf/evidence/0573/applycheck.sh`, output
  `applycheck.txt`; it reads the fork with `git show` only.
- **Define `VP64_HIDDEN_EMBARK`** (D104) replaces `VP64_PROTO_HIDDEN_EMBARK`:
  - New files `VP64HiddenEmbark.h` and `VP64HiddenEmbark.inl`. As in the prototype, the `.inl` is included at the end of
    `CvUnit.cpp`. Neither pin's build file list then changes, in the x64 `build.py` or in the 32-bit `build_vp_clang.py`.
  - Every hook and include sits inside `#if defined(VP64_HIDDEN_EMBARK)`.
  - The checks:
    - 4674's `guardcheck.py --define VP64_HIDDEN_EMBARK`;
    - the speed kit's define-off byte check (`research/08-performance/speedup-test-kit.md`): the define-off DLL equals the
      pin's in every byte except the build identity; `#line` after each hook block keeps `__LINE__`.
      *Corrected 2026-11-18 23:50 (build step, section 7.1): the product branch carries no `#line`. With it, the
      narrowing gate of `tools/x64-dll/build.py` fails the release build: 17 "new" warning sites that are the same
      sites, shifted (the speed kit's known gap). The MC accepted this (#6589). The define-off check is then stated as
      "byte-identical except the build id and `__LINE__`-dependent constants", with every differing site listed, plus
      statscmp IDENTICAL define-off against the pin on M.*
- **Where the define is set**:
  - x64: one entry in `tools/x64-dll/build.py` `PREDEFS['release']` (and in `debug`). *Corrected 2026-11-19 (7.10):
    built as a conditional in `cl_args`, only for a source with the feature's header.*
  - 32-bit: the release driver's x86 define injection (`tools/release/stages/build.py` `inject_x86_define`, as issue 4252
    does for the 43-civ build).

  The build ticket's touches name both files. The fork's own build scripts stay unchanged, so an upstream build without
  the define is the stock DLL.

### 2.2 What a re-pin to batch 5 (0551) would change `[C]`

Batch 5 changes three things:

- the path finder fix: `fix-cnip-x64` `909f73bd1` and `fix-cnip-i0ship` `02749f1b9`, which touch `CvAStar.cpp/.h` only;
- the x64 interface version: `x64-ifver` `344bdff8e`, which touches `CvGameCoreDLL.cpp` and adds
  `CvOutOfProcessInterface.h`.

None of these files is a hook file, so the 0573 commit cherry-picks onto batch 5 with offsets at most.

The path finder fix changes game results. Identity runs and timing series must therefore all sit on one pin: a grade A pair
taken on batch 4 is not compared with a run on batch 5.

The switch route in 2.3 does not use `CvOutOfProcessInterface.h`, so 0573 does not depend on 0551 and neither raises the
other's interface version.

### 2.3 The runtime switch and its default

The proposal: an options key, read by the server and passed through a named export. There is one fallback for hosts that do
not pass it.

1. **`VP64Options.ini [Performance] HiddenEmbarks`**: 1 = on (the intended default, D193), 0 = stock behaviour.
   - The id is reserved by the MC (K4). The key name is proposed here.
   - The row goes into `options/src/vp64_options.cpp`, with tests in `options/tests/test_options.cpp` (the
     GameEventsOneCrossing pattern, epic 0504).
   - It gets an entry in `docs/OPTIONS.md` and in the default file text.
2. **The server reads the key** (`SrvOptions()`, once at boot, as for `GameEventsOneCrossing` and
   `BakedTileCameraRadius`; `server/src/srv_options.cpp`).
   - Right after `DllGetGameContext` (`server/src/srv_main.cpp`, beside `dll_log_flags`), it calls the DLL's named export
     `VP64HiddenEmbarkControl(int set, VP64HiddenEmbarkStats* out)`.
   - `set` = 0 or 1. `set` = -1 only queries.
   - The export returns the feature version (1).
   - It exists only under the define, as `__declspec(dllexport)`. It is not an `ICvGameContext` method, so no interface or
     version change is needed `[I: an export beside the .def is linked as usual; checked at the build]`.
   - `server-report.txt` gets a line with the value the server set, its source (the file or the default), and the DLL's
     totals at the end (section 4.4).
3. **Hosts that call no export** fall back to the environment variable `VP64_HIDDEN_EMBARK` (`0` / `1`). These hosts are the
   stock 32-bit EXE with the i0-ship DLL, the c5h headless host and test harnesses. The DLL reads the variable once, at its
   first hook. With no variable, the compiled default `VP64_HIDDEN_EMBARK_DEFAULT` (1) applies.
   *Corrected 2026-11-18 by the MC's answer #6536: the compiled default is **0** (off), so upstream and the stock 32-bit
   game are unchanged unless switched on. The options table's default is also 0, like options 2-4; the 0.1b release
   configuration (0574) writes `HiddenEmbarks = 1`. The option id is **15**.*
   - Precedence: the export call, then the environment, then the compiled default.
   - The x64 server always calls the export, so in the product only `VP64Options.ini` decides.
   - Tests on the 64-bit product set the key in `VP64Options.ini` (the runner already writes `[Performance]` keys). Tests on
     c5h or the 32-bit EXE use the variable.
4. **A missing export while the key is 1.** The export can only be missing if the DLL lacks the feature: the pack's manifest
   binds the shipped DLL by hash, so this means a broken or wrong install. The server writes one LOUD line and runs with
   stock behaviour. **Question to the MC:** should this instead be a D118 boot refusal with the error report?
   *Answered by the MC (#6536): treat it as options 2-4 do. In remote mode the server writes a LOUD line and the D118
   report (`SrvExeCrash::ReportServerFailure`, stage `hidden-embark`; vp64helper's popup, which does not block). The game
   goes on and shows every embark.*

Routes considered and not proposed:

- **A game option** (`GAMEOPTION_...`): it is part of the game's set-up, is saved in every save, shows in the set-up screens
  and is synchronised in multiplayer. A visual-only switch does not belong in game state.
- **A `CustomModOptions` row**: this would change the game database. The x64 server and the EXE each hold one, and both must
  stay equal (the database fingerprint). It is upstream's usual mechanism, so the upstream report draft (step 2) says that
  upstream may want the switch in that form.
- **The environment variable alone**: possible (the server sets it before `LoadLibrary`), but the server could then not
  report what the DLL actually did.

## 3. How each open gap is closed

### (a) Facing after embark changes out of sight, with Quick Movement on

**What is known.**

- The model's position and state are right `[L, 4676]`. With Quick Movement on, a unit that comes into view can face
  another way than in stock (cataphract 2/3596, `turn-time-study.md`).
- `setXY` sets `m_eFacingDirection` to the step's direction before the hook's graphics block (`CvUnit.cpp` 20523 against
  20987 at `c29442e8c`) `[C]`.
- `move()` calls `embark()`/`disembark()` before `setXY` (5444-5476). So at the stock call the facing is still that of the
  step before `[C]`.
- **Facing is not game state for sight.** `getFacingDirection(true)` always returns `NO_DIRECTION` (line-of-sight by facing
  was removed; `CvUnit.cpp` 25338-25348). Changing `m_eFacingDirection` directly therefore moves no fog and changes no
  game result `[C]`. `setFacingDirection` would also set a UI dirty bit, so the design does not use it.
- **Which facing the EXE uses is not known.** The EXE's `GameplayUnitEmbark` handler posts six records (destroy, create,
  damage, dim and two more; 4672). It reads the facing through `ICvUnit1::GetFacingDirection(false)` (`CvDllUnit.cpp` 154)
  either at the post or when the record is dispatched frames later. The fresh load and the off arm agreed in 4676's
  pictures, which fits a read at dispatch, but one unit does not decide it `[I]`.

**Step 2a: trace first** (about 1 h, workstation, product path, one run). In the 64-bit product every EXE read of
`ICvUnit1::GetFacingDirection` crosses the proxy. The proxy's per-call trace, keyed by the unit id, records each read with
the frame and with the post of each `GameplayUnitEmbark` for the units 4676 pictured (M 151-162, the observer following
player 0, off and on arms). The trace shows whether the EXE reads facing at the post or at the dispatch, and which value
stock's last rebuild used.

**Step 2b: the fix, by the trace's answer.** Each held-back call records the unit's `m_eFacingDirection` at that call in
the side table, as "the facing of the last held-back call".

- **If the EXE reads at the post:** the flush swaps `m_eFacingDirection` to the recorded facing for the duration of the
  `gDLL->GameplayUnitEmbark` call and swaps it back after. This is a direct field write, not `setFacingDirection`, and it
  is neutral for game state as shown above `[C]`.
- **If the EXE reads at the dispatch:** a per-unit visual facing override in `CvDllUnit::GetFacingDirection(false)`, the
  EXE's wrapper, which the DLL's own code never calls. The override answers the recorded facing until the unit's next
  `setXY` or `setFacingDirection`, the next points at which stock would also show a new facing. Game state is untouched
  either way.
- **If the trace shows that stock's model takes the facing at the dispatch of the last held-back call** (end of phase under
  option 2, about a frame later at stock pacing), the side table records the facing that the unit has when its AI player's
  turn ends (hook at the sweep, which already runs at `setTurnActive(false)`), not at the call.

**The check:** DIAGNOSTIC pictures (D112) of the three units 4676 pictured, plus every unit in the window that comes into
view after a held-back change, beside stock, with Quick Movement on and off. Pass: the same model, plot and facing.

**Fallback,** if neither variant gives stock's facing within the build's time: the named difference stays (D66). The
pictures then show the unit at play distance (the default camera height, unit flags on, D114). The difference is written
in `docs/OPTIONS.md` beside the key.

### (b) The memory growth seen with it on (0.31 -> 0.58 MB per turn, n = 3)

**Re-read of the four laptop pairs** `[L, 2026-11-17 runs; analysed 2026-11-18]`.

- Method: `perf/evidence/0573/memcount.py` over `F:/c5-64bit-work/0437/4676-M-he-*`, output `memcount.tsv`. It repeats the
  runner's slope (`runrecord.py`, least squares over the boundaries 150-165) and adds a Theil-Sen slope.
- The metric `growth_mb_per_turn` is the EXE's private bytes (`exe.private_slope@3`; `runrecord.py`) `[C]`. The
  prototype's state lives in `server.exe`, not in the EXE, so it cannot add to that number directly.

EXE slope, on minus off, MB per turn, per Williams round (OLS):

| Pair | round 1 | round 2 | round 3 | mean |
|---|---|---|---|---|
| Quick Movement on, options 2+3 (`56d0`, the 0.31 -> 0.58) | +0.32 | +0.48 | -0.01 | +0.27 |
| Quick Movement on, stock pacing (`5950`) | -0.71 | +0.43 | +1.41 | +0.37 |
| Quick Movement off, options 2+3 (`ab01`) | -0.08 | -0.31 | -0.20 | -0.20 |
| Quick Movement off, stock pacing (`bf69`) | -1.30 | -0.29 | -1.32 | -0.97 |

What the re-read shows:

- **The server's slope shows no difference** in any pair. It is 0.2-0.8 MB per turn in both arms, with mixed signs.
- **The sign of the EXE difference is not stable across pairs.** Two pairs lean up, two lean down. The QM-off stock pair
  leans down by more than the QM-on pair leans up.
- **The EXE's slope is much higher at stock pacing** (1.1-2.9 MB per turn) than with options 2+3 (0.15-0.67) in both arms.
  The EXE's memory at a turn boundary therefore follows the pacing, meaning what the EXE's queues hold at the moment of the
  sample, more than the game `[I]`.
- **Single boundary samples carry 30-40 MB spikes.** Example: off arm `56d0` r01, turn 158: 1694.6 MB against 1654 MB on
  either side. Over 15 points, such spikes move a slope by tenths of a MB.
- **The side table is small.** In the on runs it held 19 units at turn 150 and 88 at turn 165 (`pending_max`; one entry is
  a 64-bit key and a flag in a `std::map` node, under 100 bytes). That is under 10 KB per run, in the server.

**Reading** `[I]`: within the noise of 15-turn slopes, with no mechanism in the EXE. The one pair that excluded 0 did so by
0.02 MB per turn, and it is one of four pairs tested.

**Closing it in step 2** (the same runs as (f)):

- On/off pairs of **50 turns** (M 151-200, options 2+3, Quick Movement on: the pair where it showed), 3 runs per arm.
- Memory from `perf/memsampler` on the EXE and the server at its fixed tick. The slope is taken from the per-turn minimum,
  not one boundary sample, plus the runner's slope for comparison.
- **Pass:** the on-off difference of the EXE's 50-turn slope has a 95 % interval that includes 0, or is under 0.1 MB per
  turn. The side table's size per turn is logged (`pending_end`), and it levels off below the AI unit count.

**The side table's growth limit by design:** one entry per living unit that changed embark state out of sight and has not
come into view since. Entries go when the unit dies or comes into view; the whole table is cleared at a game load or a new
game (section 4.1).

### (c) Quick Combat off (combat flushes, 0 seen live)

Why 0 combat flushes were seen `[C]`: every visualised combat calls `setVisualizeCombat` only in the `!quickCombat` branch.
The places are melee, ranged, city, air, nuke and the city attack (`CvUnitCombat.cpp` 3657-3667, 3753-3759, 3788-3795,
3870-3876, 3922-3929, 3985-3991, 4093-4105, 4207-4231; `CvCity.cpp` 33150-33161). The study ran with Quick Combat on.

With Quick Combat off:

- The call comes before `GameplayUnitCombat` / `GameplayCityCombat` is posted. A flush there therefore precedes the combat
  record in the EXE's queue, which is the right order `[C]`.
- `setVisualizeCombat(isActiveVisible)` uses the active team, which is a watching human. So the defender is never held back
  at a visualised combat: its plot is visible, and the reveal hook has already flushed it.
- The attacker can be held back, for example a ranged attack from a hidden plot. The combat flush handles it.

Test in step 2:

1. A **constructed situation** on a copy of M (the situation kit, Lua channel; Quick Combat off and on):
   - an AI unit disembarks out of player 0's sight;
   - it then attacks a player-0 unit in view (by mission push);
   - and a second unit is attacked while held back.

   Pass: `flushed_combat >= 1`, IDENTICAL against the off arm (statscmp), and a DIAGNOSTIC picture of the combat beside
   stock.
2. The **counter from the natural game**: the 50-turn runs of (f), repeated with Quick Combat off (one run per arm). Report
   the count, which may be 0.

### (d) Hotseat and multiplayer (the union of the humans' views)

- **The rule is the union by construction** `[C]`. `isVisibleToWatchingHuman` asks whether any living human team, or an
  assigned observer slot, sees the plot. A unit is held back only when no human sees it. A unit of a team with a human is
  never held back.
- **Hotseat:** every human's sight counts at all times, whoever is active. Test in step 2: a hotseat game of 2 humans and
  AIs (set-up through the front end on a test instance, HI or Lua-channel set-up), 10 turns. Pass:
  - counters with deferrals > 0 and missed reveals 0;
  - the same game as the off arm;
  - a picture of a unit held back from both humans that comes into view of the second human.
- **Multiplayer:** every machine runs the same DLL code in lockstep. The rule's input (all humans' visibility) is the same on
  every machine, so every machine holds back the same calls. The side table is never read by game logic and never written to
  a save or a sync checksum. So it cannot desync `[C for the code paths; I for a live session]`.
  - **No multiplayer test set-up is known in the project** (searched `research/` on 2026-11-18: none, and multiplayer between
    patched and unpatched clients is a non-goal: decision log, "Correction 2026-11-10 04:17"). Step 2 says "multiplayer not tested" unless the MC
    names a set-up. The Goldberg instances might host a LAN game; that is the MC's call (a separate ticket).
- **Autoplay** (tests only): the autoplay observer sees every plot, so nothing is held back unless it follows player 0
  (`observer_follow 0`, 4676). With the observer override, the copy of the visibility state does not pass
  `changeVisibilityCount` `[I]`; the sweep catches it, as a missed reveal at the turn end. This matters only in test arms.

### (e) The 32-bit build and run

- **Build:** the i0-ship branch (`hidden-embark-i0`) with the define, by the release driver's 32-bit build
  (`build_vp_clang.py --config release` with the injected define, under quietlock). The define-off build equals the
  i0-ship-batch4 DLL except the build identity (the same byte check).
- **Run 1, c5h headless, M 151-160:** no graphics, so nothing is held back (`posted_no_graphics`). Pass: statscmp IDENTICAL
  against the i0-ship-batch4 DLL.
- **Run 2, the stock EXE windowed with the 32-bit DLL (S2i), M 151-165**, the observer following player 0, on and off by
  `VP64_HIDDEN_EMBARK`. Pass:
  - IDENTICAL (D164, windowed against windowed);
  - deferrals about the 64-bit figure (316 per 15 turns, `[L, 4676 on arms]`);
  - missed reveals 0;
  - one DIAGNOSTIC picture.
- **32-bit timing:** an indication only (one pair, grade B). The x64 grade A pair is (g).

### (f) Every reveal path covered: missed reveals 0 over at least 50 turns on M and over a huge save

**Reveal paths** `[C, c29442e8c]`:

- A plot turns visible only through `CvPlot::changeVisibilityCount`. It is the only writer that raises
  `m_aiVisibilityCount` and `m_aiVisibilityCountThisTurnMax`. `flipVisibility` only lowers the latter or copies the count.
  With `MOD_CORE_DELAYED_VISIBILITY` the hook's condition `!bOldVisibility && isVisible` can fire for a plot that is
  already visible; that is a harmless extra check.
- A unit enters a watched plot only through `setXY`.
- At a load, `setupGraphical` posts every unit's true state.
- The watching set changes without a visibility change only at a player's death (it shrinks: no reveal), or when an observer
  slot is claimed or a player turns human (autoplay, a hotseat takeover). The sweep catches those at the turn end.

**Already measured** `[L, 4676's laptop pairs, 2026-11-17]`: 12 on runs × 15 turns (M 151-165, Quick Movement on and off,
both pacings). Each run had 389 calls and 316 held back; flushes: 16 by move, 4 by reveal, 0 by combat; **missed reveals
0**; 22 dropped with their unit; at most 88 held back (`perf/evidence/0573/memcount.tsv`).

**Step 2:**

- **M:** 151-200 (50 turns), the product build, options 2+3, Quick Movement on, 3 runs on and 3 off. These are the same runs
  as (b).
- **H:** 300-325, one run on and one off, Quick Movement on, stock pacing.
- Pass: `flushed_sweep` = 0 in every on run (`hecount.py` exits 3 otherwise) and IDENTICAL against off.
- Any missed reveal is root-caused before the hand-back.

### (g) A grade A laptop pair by PERFORMANCE.md (stock pacing and options 2+3)

- **Set-up:** laptop, one build (the product x64 DLL), one setting differs (`HiddenEmbarks` 1 / 0): grade A under
  PERFORMANCE.md 2.7's treatment exception.
  - M 151-165, Quick Movement on, Quick Combat on, the observer following player 0.
  - 6 runs per arm in Williams order, quiet, `--expect-min` set.
  - GPU drivers named (D175: Intel 27.20.100.9320, NVIDIA 32.0.15.7322 as of 2026-11-17).
- **Two pairs:**
  - stock pacing: about 6 min per run × 12, about 72 min;
  - options 2+3: about 4.3 min per run × 12, about 52 min.

  About 2.1 h of laptop time in all, by 4676's turn times plus a 2.5 min boot `[I]`.
- **Report:** PERFORMANCE.md 6's template, with statscmp IDENTICAL within each pair and the counters per run.
- **Optional, for the owner's default decision:** a third pair with options 2+3+4, grade B. 4675's model gives about 0 s there,
  since option 4 already removes the same drain frames. If options 2+3+4 become the default (D178, 0563), hidden embarks
  pay mainly with Quick Movement off (option 4 is refused there), at stock pacing and in the 32-bit game.

## 4. Changes from the prototype for the product

### 4.1 State

- One side table `(owner, id) -> {the EXE's model state, the facing of the last held-back call}`.
- Cleared at `CvGame::uninit` (a load or a new game in the same process). **The prototype does not clear it** `[C]`: a second
  game in the same session could flush, or count as missed, a stale key. Step 2 finds the exact clear point and tests it with
  two loads in one session.

### 4.2 Threads

All hook sites are game-state writers that run on the game-core thread under the DLL's game lock `[I]`. A cheap entry guard
(thread id and a busy flag) catches a hook entered from a second thread while another hook runs. That is a guard failure:
D118, the error report and popup, through the server's existing fail path in x64. In the 32-bit game it is a LOUD
`FILogFile` line and the feature turns itself off for the session.
*Changed in the build (2026-11-18, section 7.1). The tables are read from a second thread by design: the EXE asks for a
unit's facing through `CvDllUnit::GetFacingDirection`, the facing override's reader, and in the 64-bit server that call may
be served on another thread. So one critical section guards the tables, and a hook entered from a second thread is safe.
Such entries are counted (`other_thread`), and they are not a guard failure. The override's reader takes the lock only
while an override exists; it checks an interlocked count first. The export reads the counters with `TryEnter`, so a report
written on a crash path never waits.*

### 4.3 Missed reveals

The sweep posts the true state (the unit shows its right model at the turn end at the latest) and counts it. That is a LOUD
line in `server-report.txt`, not a popup (proposed; **question to the MC**).

### 4.4 Counters

- Per-turn CSV only when `VP64_HIDDEN_EMBARK_DIR` is set (tests). The columns are as `hecount.py` reads them today.
- Totals through the export for the server report.
- No file I/O in the product otherwise.

### 4.5 Upstream form (D104, D95, D130)

- The define, the side table and the hooks go upstream as they are.
- The switch: the environment variable or the compiled default. The draft names `CustomModOptions` as upstream's likely
  preference.
- The text is scrubbed, and goes only with the owner's yes to the exact text.

## 5. Step 2, the build ticket (MC's estimate 12-20 h), in order

1. The two fork branches with the commit (section 2.1, 4).
2. The options key, the server's read and export call, and the report line, with tests: options, the server's unit test of
   the export probe with the export missing, wrong-version and present cases.
3. The x64 build (release driver) and guardcheck; the define-off byte check on both pins; a headless S3 identity run.
4. The (a) trace, then the facing fix, then the (a) pictures.
5. The (c) constructed situation and the (d) hotseat run.
6. The (e) 32-bit build and runs.
7. The (b)+(f) M 50-turn runs and the (f) H run, on the laptop or oldpc.
8. The (g) grade A pairs on the laptop.
9. The upstream report draft for the owner.
10. The default confirmed with the owner (intended on).

Touches for the MC to grant:

- the fork branches;
- `options/`, `server/src/srv_main.cpp`, `server/src/srv_options.*`, `docs/OPTIONS.md`;
- `tools/x64-dll/build.py` (PREDEFS) and `tools/release/stages/build.py` (the x86 define);
- `perf/turnstudy/runner/` (the arm: the product build with the key instead of the S4e layout);
- `perf/evidence/0573/` and this file.

## 6. Questions for the MC (posted on thread 0573)

1. The switch route of 2.3, and the option id for `[Performance] HiddenEmbarks` (K4).
2. A missing export while the key is 1: a LOUD line (proposed) or a D118 refusal?
3. Missed reveals: flush plus a LOUD line (proposed), not a popup?
4. Multiplayer: say "not tested" (no set-up known), or name a set-up?
5. The optional 2+3+4 pair in (g): wanted?
6. Fork branch names `hidden-embark-x64` and `hidden-embark-i0`, and how they reach 0.1b (D195): a pin batch with them, or
   the release driver building from the branches.

## 7. The build (step 2, in this ticket from 2026-11-18 22:25)

The MC accepted the design (#6533 to #6536) and the build continues in this ticket (D195: merged by the freeze, Thu
2026-11-19 20:00 EST). Priorities if time runs short: (a), (c), (e), (f) and the identity runs first; (b) and (g) may
finish after the freeze.

### 7.1 What was built `[C, 2026-11-18/19, workstation]`

- **Fork branches** (in a clone of the local fork clone, `C:/c5-64bit-work/0573/fork`; not pushed yet):
  - `hidden-embark-x64` = `c29442e8c` + `e9ccfcee7`;
  - `hidden-embark-i0` = `e96457fd5` + `71cb0bf57`.

  The two commits have the same patch-id, `fe298698ef56...` (`git patch-id --stable`). Hook files are equal on both
  pins, except `CvGame.cpp` (one line shorter on i0 above the hook, so its `#line` value differed until `#line` was
  dropped). The commit message is scrubbed for upstream (D95): no ticket or decision ids. It is in
  `perf/evidence/0573/msg-fork.txt`.
- **The hooks** are put in by `perf/evidence/0573/hooks.py`, by anchor text. Each anchor must match once, or the script
  refuses. The hooks are the prototype's, plus:
  - `CvGame::uninit` → `Clear()`: a load or a new game empties the tables;
  - `CvDllUnit::GetFacingDirection` → `VisualFacing()`: the facing override, gap (a);
  - `embark()`/`disembark()` call `VP64HiddenEmbark::Embark(...)`, which posts or holds back; the original
    `gDLL->GameplayUnitEmbark` line stays in the `#else` branch.
- **The `#line` lines were removed** (`strip_line.py`). Build 1 had them, and the narrowing gate failed: "17 new, 21
  gone" sites, the same sites shifted (`C:/c5-64bit-work/0573/build-x64/dll/narrowing-new.txt` of that build). Without
  them the gate passes: 794 sites against a baseline of 798, 0 new, 4 gone. Section 2.1 has the correction.
- **The module**, `VP64HiddenEmbark.h` and `.inl` (about 450 lines):
  - The side table is `(owner, id) -> {model state the EXE has, facing at the last held-back call}`.
  - A facing override table, plus an interlocked count of its entries.
  - One critical section guards both tables (section 4.2's correction).
  - Totals and per-turn counters. The per-turn CSV is written only under `VP64_HIDDEN_EMBARK_DIR`; its columns are the
    prototype's plus `cleared`, `other_thread` and `facing_overrides`.
  - The facing trace `HiddenEmbarkFacing.csv`, only with `VP64_HIDDEN_EMBARK_TRACE=1`. It has a row for every post,
    hold-back and flush, and for every `GetFacingDirection(false)` read of a unit that had an embark event. Each row
    records the sequence number, the turn, whether a `GameplayUnitEmbark` call was in progress, and the thread.
  - The export `VP64HiddenEmbarkControl(set, stats)`: `set` = 0 or 1 switches the feature; -1 only reads. It returns
    version 1. Reads use `TryEnterCriticalSection`, so a report on a crash path never waits.
  - The run-time switch, first match wins: the export, then the `VP64_HIDDEN_EMBARK` environment variable, then the
    compiled default 0. The facing mode: `VP64_HIDDEN_EMBARK_FACING` = `call` (default) or `none`.
- **x64 builds** (`perf/evidence/0573/build_x64.sh`: `build_all.py --config release --offline --unlocked` with
  `CL=-DVP64_HIDDEN_EMBARK`):
  - b1, superseded: DLL `940AB174...`.
  - **b2**, 23:13-23:40: DLL **`BCE6677EF8189AFECE729EB60E4CB5058BB3B7EC0493474903CC32D03F58FC8D`**, 23,699,968 B,
    "Release-5.4.8-64-ge9ccfcee7 Clean". Every step returned 0. The smoke host ran with the DLL saying "Supporting 22
    major civilizations".
  - The export is in the table: `llvm-objdump -p` lists `VP64HiddenEmbarkControl` beside `DllGetGameContext`.
- **The server**:
  - `server/src/srv_hidden_embark.cpp`/`.h`: `Decide()` (pure), `Apply()` and `Summary()`.
  - `Apply()` is called right after `DllGetGameContext`, in direct mode (`srv_main.cpp`) and in remote mode
    (`srv_remote_main.cpp`, kRt_Boot). It probes the export, queries the version and calls it with `SrvOptions()`'s
    `HiddenEmbarks`. If the export is missing (or has another version) and the key is 1, it writes a LOUD line, and in
    remote mode the D118 report (stage `hidden-embark`); the game goes on.
  - The counters line `hidden-embark: ...` goes into both reports. It carries `LOUD:` when missed reveals > 0.
  - The host test `server/tests/hidden_embark/run_hidden_embark_test.py`: 27 checks, PASS with clang-cl x64 and with MSVC
    x64 `/fsanitize=address` (the canary was caught). It covers the export present, missing or at another version × key
    0 or 1 × direct or remote mode, the report switched off, the counters line, and the layout size (176 B).
- **The option**: `[Performance] HiddenEmbarks`, id 15, default 0. Ids 11-14 are reserved rows with empty keys, using
  0561's `FindKey` / `DefaultFileText` / docs-test lines verbatim, so the merges meet the same text. `docs/OPTIONS.md` has
  the player's text. `test_options`: 395/395 passed in both trees.
- **The layout** (`perf/evidence/0573/build_pack.sh`; `C:/c5-64bit-work/0573/b2/layout`):
  - server.exe `10E837E6`, proxy `1F8FBC69`, vp64helper `F6F2B622`, built at repository `da8cbc17d` against the fork
    checkout (`--unlocked`, `--deps` = the DLL's build);
  - `pack.py` VP64 with manifest `03EC7CCE` and purity `F0F496D5`.
- **Runner** (`perf/turnstudy/runner/`):
  - arm **S4h** (S4b4's form with this layout, `hidden_embark_build: "x64"`). A variant's `hidden_embark` makes the run
    write `VP64Options.ini [Performance] HiddenEmbarks` and set the product's environment names.
  - `hidden_embark_trace`.
  - M variants `he_{off,on}_trace_{skip1_tick0,stock}` and `he_{off,on}_qcoff_stock`; H variants `he_{off,on}_stock`.
  - `perf/turnstudy/pacing/embarkvis.py --issue`.
  - Tests: `perf/tests/turnstudy/test_hiddenembark_product.py` plus the prototype's, 16 passed.

### 7.2 Finding: the facing hook takes `GetFacingDirection` off the proxy's purity list `[C, 2026-11-18 23:55]`

`proxy/tools/derive_purity.py` on the b2 DLL keeps 165 of the 166 audited rows. It drops
`ICvUnit1::GetFacingDirection(bool) const`, because the cited text `CvDllUnit.cpp:154-157` changed
(`C:/c5-64bit-work/0573/b2/purity/report.tsv`). In that layout the proxy no longer keeps the EXE's facing reads in its
epoch mirror, so every read crosses to the server. Two consequences:

- the facing trace sees every read the EXE makes (good for gap (a));
- a product that keeps the override pays one crossing per facing read.

How many reads that is per turn, and whether the row can be re-audited as pure (the override changes only inside hooks,
which run in game-state writes), is decided with the trace below.
*Corrected 2026-11-24 (ticket 0628): the proxy never answers `GetFacingDirection` itself (it is not one of the 24
mirrored getters), so every read crosses with or without the row; without it each read moves the epoch, so mirrored
getters read afterwards cross again. The row was re-audited by hand at `1b87dd185` and is `pure` again
(`research/03-dll-engine-boundary/proxy-design.md` 17.5b).*

### 7.3 Gap (a): the EXE reads the facing inside the call, and the flush gives it stock's value `[L, 2026-11-19 00:04-00:35 EST, workstation]`

**Set-up.** Shared workstation: 7 heavy jobs of other owners were registered at the start (builds, a D10 test, a game, a
render suite). Instance c5h-b, window cloaked (the harness's no-focus launch). Arm S4h (the b2 layout), variants
`he_off_trace_skip1_tick0` and `he_on_trace_skip1_tick0`: options 2+3, Quick Movement and Quick Combat on, the observer
following player 0, the DLL's facing trace on. M from TS0438-M-T150 by hash, window 151-160, statistics logging on,
driven by `perf/turnstudy/pacing/embarkvis.py` through `perf/evidence/0573/runset.sh`. One run per arm: functional, an
indication (D106). Raw runs are in `F:/c5-64bit-work/0573/ws/runs/`; summaries in `perf/evidence/0573/ws/`.

- **Game identity: IDENTICAL**, off against on: 15 tables, 39,257 rows (`statscmp-trace-skip1_tick0.txt`).
- **The server's lines** (`server-lines-*.txt`):
  - off: `HiddenEmbarks = 0 (from the file) ... told the DLL off`; calls 289, of which 58 posted watched and 231 would be
    held back;
  - on: `= 1 ... told the DLL on`; held back 231; flushed by move 8, by reveal 4, by combat 0; **missed reveals 0**;
    dropped 19; pending 73 at the end; facing overrides 8; other-thread entries 0.
- **When the EXE reads the facing** (`facetrace-off-trace-skip1_tick0.txt`, stock posting): over 10 turns, 255 posts and 257
  reads of those units' facing. **255 of the reads came inside a `GameplayUnitEmbark` call, one per call**, all on the
  game-core thread. Only 2 reads came outside a call. So the EXE takes the facing at the post, not at the dispatch.
  The model stock builds faces the way the unit faced at the unit's last embark call. That call comes in `CvUnit::move`
  before `setXY`, so it is the facing of the step before.
- **The flushes against stock** (`facecmp-trace-skip1_tick0.txt`, `perf/evidence/0573/facecmp.py`): the game is the same in
  both arms, so a unit's k-th embark call is the same call in both. **12 of 12 flushes gave the EXE the facing and the
  embarked state of stock's matching call.** In 8 of the 12, the unit's current facing differed from it (the override
  acted). Without the override, the facing would have differed in those 8: that is the named difference of 4676 (the
  cataphract 2/3596 here: last held-back facing 0, current facing 4; the EXE read 0, as in stock).
- **Purity** (7.2): the EXE reads a unit's facing about once per embark call, not per frame. Dropping the purity row
  therefore costs about one crossing per embark call, about 26 per M turn, inside a call that crosses anyway `[I: from
  the counts]`.
- **Pictures**: the three shots per arm (`vc-20261119-0011*`, `-0033*`, refiled under 0573) are not usable. The game's
  "You Have Researched A New Technology!" popup for turn 162 covers the screen's middle, where `UI.LookAt` puts the unit
  (D114). They are retaken with the camera 6 plots east of the unit (`embarkvis.py --shot-offset`), so that the unit stands
  left of the popup.

**Verdict for gap (a):** the product's flush gives the EXE stock's facing. It is closed by the trace; the pictures are a
confirmation, to come.

### 7.4 Stock pacing: the same result `[L, 2026-11-19 00:46-01:01 EST, workstation, the same set-up as 7.3]`

Variants `he_{off,on}_trace_stock` are 7.3's pair with stock pacing (no proxy option: the 32-bit game's case).

- IDENTICAL: 15 tables, 39,257 rows.
- The server's counters are the same as with options 2+3: 289 calls, 231 held back, 8 + 4 flushes, missed reveals 0, 19
  dropped, 73 pending at the end, 8 facing overrides, 0 other-thread entries.
- Stock again read the facing inside the call for 255 of 257 reads.
- **12 of 12 flushes gave stock's facing and state.**

Files: `perf/evidence/0573/ws/*-trace-stock*`.

**The pictures (PrintWindow) do not help here.**

- The first pair's shots show the game's tech popup over the unit.
- With the camera 6 plots east, the stock arm's shot still has the popup's modal dimming over the map.
- The on arm's shot of 2/3596 is a UI-less frame with no units: PrintWindow's known failure on the product path
  (EFFECTIVENESS E9, proxy-design.md 20).

These are DIAGNOSTIC entries under 0573 with result info. The next pair presses ESC once before the shots (the runs
are known to end on the Theology popup of turn 162; `embarkvis.py --shot-esc`). The decisive evidence for gap (a) stays
the trace, which reads the value the EXE took, not a picture of it.

### 7.5 A server crash on the laptop: a UI Lua call reading a freed unit `[C: the DLL's minidump read with cdb, 2026-11-19 01:55; L: the run]`

**What happened** `[L, laptop job 20261119-010137-e711]`.

- The job: S4h b2, M 151-178, options 2+3, Quick Movement on, the observer following player 0.
- Runs r01-p1 (off) and r01-p2 (on) passed all 28 turns.
- In r02-p1 (on), server.exe (pid 4728) exited with 0xC0000005 at about turn 172, 206 s into the run.
  - The proxy wrote the D119 report (stage server-death, PeerDied on `rt:LuaCall` on **channel 0**: the EXE's main
    thread, the interface Lua) and started vp64helper's popup.
  - The runner's restore failed: vp64helper.exe was still in use (PermissionError). So tb1 kept the S4h layout, and
    every run of the next job (`9bc6`) refused to start. I cancelled the two queued jobs.
- Recovery: job `20261119-014209-45a7` (`perf/evidence/0573/recover_tb1.py`, on the MC's word #6698) ran these steps,
  in order:
  1. copied the evidence first;
  2. closed the popup with `cli.py dismiss --button Close --coordinator-override`;
  3. ran r02-p1's own `ClientRun.restore`: ok;
  4. ran `leftovers`: empty.
- The server's own crash-dump filter wrote no dump. That is expected: `CvGlobals` replaces it with the DLL's
  `CustomFilter`, as `research/03-dll-engine-boundary/error-report-exe-crash.md` 9.1 says. Windows logged no
  Application Error event either (`crash_events.py`, job `5530`).
- The DLL's own filter wrote `crashlogs\crashes.log` and a minidump into the run folder. Read-only job `0db2`
  (`crash_folder.py`) copied them out; they are in `perf/evidence/0573/crash-e711/`. The minidump itself is kept on F:
  only: `F:/c5-64bit-work/0573/tb/0db2/`.

**The stack** (cdb on the minidump with the b2 PDBs; `perf/evidence/0573/crash-e711/cdb-stack.txt`):

```
CvUnit::getX [inlined in CvLuaUnit::lGetPlot+0x15]: mov r9d,[rax+48h]   read of 0x000002e2ff7f6c18 (unit pointer rax = 0x2e2ff7f6bd0)
CvLuaUnit::lGetPlot+0x15
lua51_x64!luaD_precall / luaD_call / luaD_rawrunprotected / luaD_pcall / lua_pcall
server!LuaPCall+0x45
server!SrvRemote::HandleLuaRt+0x5a3
server!SrvRemote::OnCall+0x1a4
server!vpipc::ChannelImpl::serve+0xdd3
server!ServiceThread+0x8d
```

`CvLuaScopedInstance<CvLuaUnit,CvUnit>::GetInstance` returned the CvUnit pointer stored in the instance table's
`__instance` field. It checks nothing else (`CvLuaScopedInstance.h` 185-207 at the pin; proxy-design.md: instances cross
as server pointers). The first member read faulted, so that unit's memory was no longer mapped: **a use after free of a
CvUnit, through a unit instance that interface Lua on the EXE's main thread still held and passed to `Unit:GetPlot()`.**

**Is it the hidden-embark change?**

- **The faulting code is not in it.** No hidden-embark function is on the stack. The fault is on the server's service
  thread for channel 0, serving one Lua call from the EXE, not on the game-core thread where the hooks run.
- The hidden-embark side table holds `(owner, id)` keys and two small integers, never a CvUnit pointer. It only decides
  whether and when `GameplayUnitEmbark` goes to the EXE. The override answers `GetFacingDirection` for live units through
  `CvDllUnit`.
- **Whether the change makes the race likelier is not known** `[I]`. With the feature on, the EXE gets a different
  sequence of unit events: fewer embark sets out of sight, and flushes when a unit comes into view. So interface Lua that
  reacts to unit events runs at other moments. The same use after free can happen in stock: a script takes a unit object,
  and between two of its calls the AI's thread frees the unit. A 32-bit game in one process would read freed heap memory
  without a fault in most cases, while the 64-bit server faults when the page is gone `[I]`.
- **The counts so far:**
  - With the product b2: 1 crash in 2 laptop on-runs of 28 turns and 4 workstation on-runs of 10 turns; 0 in the off-runs
    (1 laptop run of 28 turns, 3 workstation runs).
  - With the 4676 prototype on batch 2: 0 in 12 on-runs and 12 off-runs of 15 turns.
  - With S4b4 on batch 4: 0 in 0563's and 0570's runs.

  One crash proves nothing either way.
- **Which interface script made the call is not determined.** The server only runs the one C function. The script and
  its event are in the EXE's Lua; 0518's scratch measuring build would name them.

**Consequence (the MC, #6698):** hidden embarks is not on by default in 0.1b until this crash has a root cause and a
fix, or a proof that it is unrelated. Proposed next steps, for the MC:

1. A crash-rate pair on the laptop or oldpc: S4h on/off, more runs of 28 turns. A crash in an off-run also proves that
   the crash is not the change's.
2. A guard against stale unit instances on the product path, as its own ticket: for example, the server resolves a unit
   `__instance` against the owner's unit list before calling, and answers nil and a LOUD line on a miss. It also
   protects the default-off build.

### 7.6 Gap (c): Quick Combat off `[L, 2026-11-19 01:09-02:00 EST, workstation; C: the code]`

**The options call.** Variants that turn Quick Combat off alone were refused by the runner's read-back:
`Network.SendGameOptions` with Quick Combat false and Quick Movement true left the game option for Quick Movement false
(read back `[False, False, False, True]`: game options QC and QM, then PreGame QC and QM). So the variants
`he_{off,on}_quickoff_stock` turn both off, as the study's `quick_off` does. Stock pacing.

- off arm: c5h-b, 01:09-01:26;
- on arm: c5h-e (F:, read latency 0 ms at the start), claimed in pool.py, 01:42-02:00.

**Results.**

- IDENTICAL: 15 tables, 39,257 rows.
- The counters equal 7.3's, and **flushed by combat 0**. No visualised combat in M 151-160 involved a held-back unit.
- **Why a natural run rarely meets a combat flush** `[C]`:
  - `setVisualizeCombat(isActiveVisible)` comes only on the `!quickCombat` branches, and its target plot is visible to
    the active team, a watching human. So a defender on it was already flushed by the reveal hook.
  - Only an attacker on a hidden plot can still be held back at a visualised combat: a ranged attack from fog after an
    embark change out of sight. The combat hook flushes it before `GameplayUnitCombat`/`GameplayCityCombat` is posted.
- **A constructed situation was not made** in this ticket. It needs an AI unit that disembarks out of sight and then
  attacks a visible target from fog, by mission push in the AI's own turn. Gap (c) therefore rests on the code path
  `[C]` and on 0 combat flushes and 0 missed reveals in the natural Quick Combat off pair `[L]`.

**Pictures** (one ESC closes the turn-162 tech popup, `--shot-esc 1`, D114 met; entries `vc-20261119-0125*` (off) and
`-0157*`/`-0158*` (on), described):

- by eye, the same units, with the same land models, on the same plots, for the three units;
- the formation members' poses differ in the cataphract 2/3596's stack. That is idle animation or facing: this pair had
  no facing trace. With Quick Movement off, 4676 saw the facing equal. With Quick Movement on, the trace settles it (7.3,
  7.4).

### 7.7 Gap (e): the 32-bit build and run `[C: build; L, 2026-11-19 02:17-02:31 EST, workstation]`

**The build.** `perf/evidence/0573/build32.py` builds fork branch `hidden-embark-i0` `71cb0bf57` the way the release
driver builds an `-ia` DLL, in these steps:

1. a git archive of the commit;
2. the instruments patch of pin batch 3 (`tools/d10/evidence/0463/ia-39f17f754-440878983.patch`) applied;
3. the version stub written;
4. `VP64_HIDDEN_EMBARK` added to `build_vp_clang.py`'s RELEASE/DEBUG_PREDEFS in the copy (as `inject_x86_define`; clang-cl
   ignores `CL` for this script).

The result: cpps 109 s, link 304 s, DLL **`0DA0A2CC426D888148438093B79878C81444D963AD5BD8F79FC492FD0F151491`**,
"Release-5.4.8-50-g71cb0bf579+VP64_HIDDEN_EMBARK+ia0463 Copy". `llvm-objdump -p` lists `VP64HiddenEmbarkControl` beside
`DllGetGameContext`, so the export is in the 32-bit build too.

**The run.** Arm **S2h**: the stock EXE, no proxy, with this DLL, set into c5h-e by `provision.py set-dll` for the set and
put back by `--previous` at its end. `provision.py verify` rc 0; the instance's DLL is back to `3C2F47DA`. The variants
are `he_{off,on}_trace_stock`; the DLL takes the switch from `VP64_HIDDEN_EMBARK` (source 2). c5h-e is an F: instance:
its read latency was 4.75 ms at the start, and it was claimed in pool.py.

- **IDENTICAL**, 15 tables, 39,257 rows (`statscmp-s2h-trace-stock.txt`).
- In-process too, the EXE reads the facing **inside the call**: 255 of 257 reads.
- **12 of 12 flushes gave stock's facing and state** (`facecmp-s2h-trace-stock.txt`). Missed reveals 0, other-thread
  entries 0.
- **Pictures** (one ESC closes the popup; D114): the cataphract 2/3596's formation faces the same way in both arms, with
  the same units, models and plots (entries `vc-20261119-0223*` off, `-0230*` on, the on arm marked pass against the
  off arm's picture).

**Gap (e) is closed:** the 32-bit game builds, runs, plays the same game and gets stock's facing. What it saves in time
was not measured (no 32-bit timing pair).

### 7.8 The define-off check without `#line` `[C, 2026-11-19 02:33-02:50 EST, workstation]`

The MC's condition (#6589) for a product branch without `#line`: a build without the define is byte-identical except the
build id and `__LINE__`-dependent constants, every differing site is listed, and statscmp IDENTICAL against the pin on M.

**The build.** `perf/speedkit/defineoff.py --fork <clone> --branch hidden-embark-x64 --deps <b2's build_all>`, with no
define and with the pin's version string and epoch. The DLL is
`C5056521BF72CA7450C9C48CA7CA81F20825CC15A8CC87C30DCC5DD6351A7B2F`: 23,681,024 B, the pin's size. The tool's verdict is
DIFFERENT, as expected: 1,430 differing bytes, all in `.text` (`perf/evidence/0573/defineoff.txt`).

**Every differing site.** `perf/evidence/0573/defineoff_sites.py` finds 1,411 sites: 1,392 of 1 byte and 19 of 2 bytes.
Each is listed with its function and source line from the define-off PDB in `perf/evidence/0573/defineoff-sites.txt`.
Each site holds a number. In every site, the new value minus the pin's value is the number of lines the hooks insert
above that point, the signature of a shifted `__LINE__`:

| new - pin | sites | the hook lines above |
|---|---|---|
| +3 | 1,131 | the 3-line blocks: the PCH header's include, and the hooks in CvPlayer, CvPlot, CvGame and CvUnit's kill. Sites the symbolizer puts in other files (CvLua*.cpp, CvDiplomacyAI.cpp, CvWeightedVector.h, ...) are code inlined there whose line constant comes from below one of these blocks `[I]` |
| +4 | 4 | `CvGameCoreStructs.cpp` (the 4-line `setVisualizeCombat` hook; also its inline copies in CvUnitCombat) |
| +6 | 5 | `CvDllUnit.cpp` (the facing wrapper's block) |
| +11 | 116 | `CvUnit.cpp` after `embark()`/`disembark()` (3 + 4 + 4) |
| +14 | 155 | `CvUnit.cpp` after `setXY`'s hook (+3 more) |

Two examples:

- `CvCombatInfo::setDamageMemberCount`: pin 0x20E (line 526), new 0x212 (530);
- `CvUnit::SetMissionTimer`: pin 0x79F5, new 0x7A03 (+14).

No other byte differs, so without the define the branch compiles to the pin's code except for those line numbers
`[C: the byte comparison; the reading of each delta as inserted lines I: from the counts]`. statscmp against the pin is
in 7.9.

### 7.9 Headless identity, the second crash, gaps (f) and (b), and H `[L, 2026-11-19]`

**Headless identity** (`perf/evidence/0573/run_s3.py`, the b2 server.exe in direct mode, M 151-160, workstation,
02:52-02:53, 18-20 s a run; `s3-server-lines.txt`, `statscmp-s3-*.txt`):

- the pin DLL `2B56A1CD` against b2 with `HiddenEmbarks = 1`: **IDENTICAL**, 15 tables, 38,090 rows. Headless every call
  is watched: posted watched 289, 0 held back.
- the pin DLL against the define-off DLL `C5056521` (7.8): **IDENTICAL**, 38,090 rows. That completes the MC's
  define-off condition.
- **The negative**: key 1 with the pin DLL, which has no export. The server writes `hidden-embark: LOUD: ... HiddenEmbarks
  = 1 (from the file) but the DLL cannot do it: it has no export VP64HiddenEmbarkControl ...; every embark is shown as in
  the stock game`, and the game goes on. With key 0 and no export it writes the plain line.

**The second crash: in an off-run, the same class** (job `20261119-020216-9a2f`, the crash-rate pair the MC asked for,
#6717).

- Runs r01 and r02 (off and on) passed 28 turns. r03-p1, with **HiddenEmbarks = 0**, lost its server (pid 1876,
  0xC0000005) 277 s into the run.
- Recovery `20261119-025755-40ab` ran on the MC's word #6777, the same steps as before (the script now takes the
  incident by arguments and also copies `crashlogs\`).
- cdb: `GetPlayerUnit` inlined in `CvUnit::getTransportUnit`, inlined in `CvLuaUnit::lGetTransportUnit+0x16`. It is the
  first member read of `GetInstance`'s CvUnit pointer, `[rax+514h]`, with the same server stack and channel 0 as 7.5
  (`perf/evidence/0573/crash-9a2f/`).
- **With the feature off, the crash is not caused by hidden embarks** (the MC, #6778: no further crash-rate pair is
  needed). The guard is ticket 0576, which got both signatures.
- A repro hint: both crashed runs died in turn 172 of the same game. The last counters row is turn 171 in both; 2 of the
  5 runs that reached turn 172 crashed there.

**Gap (f): missed reveals 0.**

| Where | Runs | Turns | Held back | Flushed (move / reveal / combat) | Missed |
|---|---|---|---|---|---|
| M 151-178, options 2+3, laptop (`e711`, `9a2f`) | 3 complete on-runs | 28 each | 620 a run | 27 / 7 / 0 | **0** |
| H 301-325, stock pacing, oldpc (`d50b`) | 1 on-run | 25 | 2,609 of 3,057 calls | 192 / 32 / 0 | **0** |
| M 151-160, workstation (7.3-7.7, 32-bit included) | 6 on-runs | 10 each | 231 / 209 | 8 / 4 / 0 (32-bit 7 / 4 / 0) | **0** |

`perf/evidence/0573/memcount-28turn-and-H.tsv`.

- The runner caps a run at 28 turns ("D87's 30"), so one M run cannot cover 50 turns. M 151-178 (28 turns) and H
  301-325 (25 turns) together are 53 turns with 0 missed reveals. A 50-turn span of M alone would need a continuation
  scenario from the turn-178 autosave; not made.
- **Identity**: every pair is IDENTICAL: M 28 turns within and across arms (51,566 rows of BuildingYields among them,
  analyze.py), H 635,072 rows (`statscmp-H-oldpc.txt`).

**Gap (b): memory, settled over 28 turns.** The EXE's private slope, OLS and Theil-Sen, over the complete 28-turn runs
(laptop, options 2+3; `memcount-28turn-and-H.tsv`):

| Arm | runs | OLS, MB/turn | Theil-Sen, MB/turn |
|---|---|---|---|
| off | 0.342, 0.130, 0.379 | mean 0.284 | mean 0.434 |
| on | 0.293, 0.055, 0.287 | mean 0.212 | mean 0.420 |

- analyze.py over 9a2f's two complete rounds: on minus off -0.08 MB/turn, 95 % CI [-1.95, +1.78].
- **The 15-turn +0.27 of 4676's pair does not return: over 28 turns the on arm grows no faster** (slightly slower in the
  means).
- The server's slope is equal (0.33-0.79 in both arms).
- The side table levels off: 93-102 entries at the end of M, at most 284 on H.

**Time on H, an indication only** (oldpc, 1 run per arm, stock pacing, grade C):

| Measure | off | on |
|---|---|---|
| seconds per H turn | 54.27 | 44.22 |
| EXE CPU, s per turn | 95.2 | 78.6 |
| growth, MB per turn | 1.07 | 1.05 |

The on arm is **-10.0 s per turn (-18.5 %)**, identical game.

**Time on M over 28 turns** (laptop, options 2+3, 9a2f's 2 rounds, grade B): 9.40 against 6.46 s per turn,
**-2.94 s (-31.3 %)**, 95 % CI [-3.95, -1.93].

The grade A pairs (gap g) are queued on the laptop: jobs `d7bd` (stock pacing) and `1ad2` (options 2+3). The use-after-free
crash may stop them (about 2 in 10 runs of 28 turns so far) until 0576's guard is in.

### 7.10 Where the define is set, and what is still open `[C, 2026-11-19 03:20]`

**The define.** *Corrected 2026-11-19 (MC #6794): replaced section 2.1's "one entry in `PREDEFS['release']`".*

- On the PREDEFS line the define would change every x64 build's command line, the pins' included. The command line goes
  into the PDB's build info, and the reproducible link derives the build id from the PDB `[I]`. So locked hashes would
  move, although the code does not.
- Instead `tools/x64-dll/build.py` `cl_args` appends `/DVP64_HIDDEN_EMBARK` only when the source has
  `CvGameCoreDLL_Expansion2/VP64HiddenEmbark.h`. It goes after the major-count define and before the include folders.
- The test `tools/x64-dll/tests/test_hidden_embark_define.py` checks:
  - a source without the header gets the line unchanged;
  - with the header, the one define is the only difference, for release, debug and fast, with and without the 43-civ
    define;
  - a header elsewhere does not count;
  - PREDEFS never holds the define.

  13 tests, with test_major_civs, pass; `tools/harness/tests/test_safety_static.py` passes 55.
- The 32-bit build gets the define in the build script's copy (`build32.py`, as `inject_x86_define`). 0574 is told.

**The fork branches** (pushed to the project's fork; new branches only):

| Branch | Base | Commit |
|---|---|---|
| `hidden-embark-x64` | `c29442e8c` | `e9ccfcee7` |
| `hidden-embark-i0` | `e96457fd5` | `71cb0bf57` |
| `hidden-embark-x64-b5` | x64-batch5 `1fd51353e` | `a49d8b2e7` |
| `hidden-embark-i0-b5` | i0-ship-batch5 `5d18d3d6c` | `f33fc46a4` |

All four commits have patch-id `fe298698`. The cherry-picks onto batch 5 were clean: batch 5 touches no hook file
(2.2).

**Open at the hand-back:**

- **(g)**: the grade A pairs run on the laptop: `20261119-030044-d7bd` (stock pacing, 6 + 6 × 15 turns) and
  `20261119-030220-1ad2` (options 2+3). Their analysis is `tb.py fetch` then `perf/turnstudy/runner/analyze.py`. The
  use-after-free crash (0576) can stop them; a stopped job leaves tb1 for a recovery as in 7.5.
- **(d)**: hotseat, not run live (Not verified).
- **(c)**: a constructed combat flush, not made (7.6).
- **The default for 0.1b** is the MC's (#6778): on once (a), (c), (f) are green and 0576's guard is merged. (a), (e) and
  (f) are closed here. (c) rests on the code path and on the natural Quick Combat off pair.

## 8. The upstream report draft (D104, D95, D130: for the owner's yes to the exact text; nothing is sent)

This is written for upstream's issue tracker. It is scrubbed: no internal ids or project names, no cherry-pick lines.
D130 puts upstream drafts after the beta, so it is only kept here until then.

> **Title:** Embark/disembark calls for units no human can see cost frames in the AI turn
>
> Every `CvUnit::embark` and `CvUnit::disembark` calls `gDLL->GameplayUnitEmbark`. The engine answers each call by
> queuing six records for the unit's model (destroy, create, damage, dim flag and two more), and the visual queue
> passes about one record per frame. In a mid-game standard map, most embarks of an AI turn happen where no human can
> see them: in one save, 78 % of the AI turn's embark records. These records still have to be drained one per frame
> before the next human turn, so they account for a large share of the "waiting for the AI" time. When the queue is
> drained at the end of the AI phase, about 2-3 seconds per turn on a laptop.
>
> A change that keeps the game identical: post the call only when a watching human can see the plot
> (`CvPlot::isVisibleToWatchingHuman`, the rule `setXY` already uses for animated moves) or the unit's team has a human.
> Otherwise, remember the model state the engine still has. When the unit comes into view, post its true state. That
> happens in `setXY` before the move is shown, when a plot turns visible in `changeVisibilityCount`, for a combat's
> units in `setVisualizeCombat(true)`, and in a sweep at the end of each player's turn. The engine reads the unit's
> facing inside the call, so the reveal answers `GetFacingDirection` with the facing the unit had at its last held-back
> call, and the model faces as it would have.
>
> Nothing in the game state changes. The side table is not saved, is cleared on load, and is never read by game logic,
> so saves, multiplayer sync and AI decisions are untouched. In our tests the statistics database of whole runs was
> identical with the change on and off. The change is behind a define and a runtime switch, off by default. A
> `CustomModOptions` row may suit you better as the switch.

## Not verified

- Hotseat (gap (d)): not run live in this ticket. No hotseat set-up exists in the project; the headless tools read
  single-player saves only (`research/sources/civ5-headless/saves.md`). `[I]`: by construction, the rule is the union
  of the humans' views.
- *Done 2026-11-19 (7.3, 7.4): which facing the EXE uses.* The EXE reads it inside the call.
- *Checked 2026-11-18: the export is in the x64 DLL's export table (7.1).* Still open: that the named
  `__declspec(dllexport)` exports as expected in the 32-bit build.
- *Measured 2026-11-19 (7.3, 7.4): 0 other-thread entries in 4 runs.* That every hook site runs on the game-core thread
  only. The EXE's facing reads came on the game-core thread too, inside the posts.
- That the side table's clear point is `CvGame::uninit` (4.1): a load within one session is not tested yet.
- The memory reading of (b): an inference from 15-turn data; the 28-turn runs decide (7.5).
