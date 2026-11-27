# Speedup epic proposal: the other port's exact speedups brought into our DLL fork, server and proxy (ticket 7018)

Written 2026-11-17 (14:21-14:35 EST) by the performance standards team's lead (ticket 7018, account A, branch
`perf-7018-speedup-epic`) on the owner's order of 2026-11-17 ([reworded for privacy]: once the performance team has used the review to write a proposal for an epic to bring those efficiency gains into our fork, review it with the Architect before adding it to the tracker and assigning a priority). Input: `external-port-speedups-review.md` (ticket 9077, merged 1f3bd8632), epic 0437's
`turn-time-study-analysis.md` section 9.8 and `d165-pacing-fix-design.md` section 10 (branch `epic-0437-turn-study` at
519cba51a). **Status: a proposal.** The MC reviews it with the Architect; nothing is in the tracker, no package has an
id (P0-P15 are placeholders), nothing was built or run for it. The two staging documents stay out of the repository
(D179); entries are cited by their IDs, in our words. Every gain below is **[I]**: scaled from the other port's quiet
measurements or from 0437's sampled shares (workstation indications), never a timing result (D106).

## 0. The result in brief

- **15 packages** (P0-P14) plus one deferred decision (P15) and four behaviour changes held for the owner. Quick exact items are
  grouped by DLL file so that no two packages edit the same file at the same time; where two must share files they run
  one after the other (section 4.3).
- **Expected gain [I]:** the item estimates add up to about 30 % of the H headless turn (20.9 s) before PGO; the
  path-finder items overlap and the other port found its own estimates too high (their R6), so the **planning figure is
  12-20 % of every H turn from the DLL packages plus 5-15 % from PGO**, and **about 1.0-1.3 s per client M turn**
  (of about 5.9 s with D165 options 2+3+4), of which the GameEvents hook path (P2) is the largest part.
- **Wave 1:** P0 (the open measurements and the test kit), P1 (PGO), P2 (GameEvents args in one crossing), then P3
  (tactical AI) and P5 (path finder, quick). Packages that keep values are timed again on PGO before they go on.
- **Proposed priority 6**, a lead and 3 workers, preferably on account B as a queue epic; about 160-255 worker hours
  plus 25-45 lead hours and 15-25 hours of quiet testbed time on `oldpc` [I].
- **Five questions for the owner** (section 6): A10 and A9 as core DLL defect fixes (recommended yes, A10 after P0's count);
  A1, A2, A4 not now (recommended).

## 1. Goal and scope

**Goal.** Bring every exact speedup of the review that is worth its cost for our architecture into the product: the x64
DLL (on the fork, behind defines, D104), the server's and proxy's GameEvents path, and the x64 DLL's build (PGO). Exact
means **the game stays identical** (D164: `stats.db` compare, headless against headless, windowed against windowed); a
change that alters the game is not in the epic's default scope and goes to the owner (section 6).

**Turns it acts on.** H = Huge Continental Drift turn 300 (about 175 cities, 1,600 units; headless S3 20.9 s per turn,
workstation indication); M = Standard Communitu_79a turn 150 (headless S3 1.424 s, laptop grade A); both from
`perf/turnstudy/scenarios/` (0438). In the client an AI turn is paced by the EXE's drain; with D165 options 2+3+4 the
served Updates become the critical path (M: DLL work 2.14 s + GameEvents Lua about 0.9 s + autosave 0.56 s;
`d165-pacing-fix-design.md` 10), so DLL and hook savings count fully on H, and on M once those options are on.

| Where | Today [L, indication unless marked] | Expected from the epic [I] |
|---|---|---|
| H headless (S3) | 20.9 s per turn | DLL packages 2.5-4.2 s (12-20 %) + PGO 1.0-3.1 s |
| M headless (S3) | 1.424 s per turn (grade A) | DLL packages 0.15-0.3 s + PGO 0.07-0.21 s |
| M client, options 2+3+4 | about 5.9 s per turn (one loaded workstation run) | 1.0-1.3 s: P2 0.6-0.75, DLL + PGO 0.3-0.5 (corrected 2026-11-17 by lead-0504: P2 measured about 0.025 s [L, 4804]; P15's stand-in -0.34 s [L, 4811]; see P2 below) |
| H client, options 2+3+4 | not measured (the H series was stopped by D178) | the DLL and PGO seconds of the H headless row, plus P2 on H's raises (P0 counts them) |
| Worst single turns | Congress session, era change, city-state friendship changes | P11 removes most of the yield passes on those turns (theirs 1.4 s -> 0.1 s per event) |

**Scope.** In: the review's MISSED items with an exact form (sections 2.1-2.8 of the review), A10 and A9 if the owner says yes,
PGO, the OPEN measurements that decide packages. Out: section 8.

## 2. Rules for every package

1. **One package = one ticket = one implementer.** Each lists its files; nobody else edits them while it runs.
2. **DLL code**: on a fork branch `perf/speed-<package>` cut from the x64 pin (`deps/dll.lock`: 04b1211b8 on
   `x64-batch2` today), every change inside `#if defined(VP64_SPEED_<PACKAGE>)`; kept-value items also get a verify define
   `VP64_SPEED_<PACKAGE>_VERIFY` that runs the original next to the fast path and reports any difference, with a seeded
   negative control (one kept value deliberately spoilt, which the verify run must catch: the other port's L7). Server and
   proxy code: a branch of this repository, behind a `[Performance]` patch option off by default (D165's pattern), failing
   loud (D118).
3. **Identity check (D164)**, every DLL package, before any timing:
   - define off: the x64 release DLL built from the branch is byte-identical to the pin's release DLL (reproducible
     build with the pin's `SOURCE_DATE_EPOCH`, as 0159 showed for 74588135) [I: 0159 did it for the release config];
   - define on: headless S3 runs on M 150-160 and H 300-305 give `stats.db` IDENTICAL to the pin's (`tools/d10/statscmp.py`,
     `perf/evidence/0438/idtrace.py`), plus a fresh Standard fixed-seed game 0-50 for early-game paths, plus the D24
     three-part save test (`tools/d10/run_d10.py`);
   - **coverage**: the package shows that its windows exercise each item (a counter in its counting build, or rows in
     `stats.db`); an item whose event never happens there (a Congress session, an era change, corporations) gets a save at
     such a turn, made headless from the scenario saves (copies under the package's own names; never the owner's saves);
   - verify runs: 0 differences, and the negative control caught.
   Server or proxy packages: windowed S4 against S4 with the option on, M 151-160, IDENTICAL; the proxy's offline tests
   and ASan (COMMON 9); a negative test for every error path.
4. **Timing (PERFORMANCE.md; D106; D178).** Off the workstation only: `oldpc` (quiet), the laptop once epic 0437 frees it
   (D163 reserves it). Development signal: a D178 pair, 2-3 trials per arm of 10-20 turns (M 151-165, H 301-315),
   `tools/testbed/tb.py submit --quiet` with per-arm DLLs (`perf/tbjobs/series.py`, or 0437's runner once it is on
   `main`). **Default-on acceptance** needs grade A: at least 6 interleaved runs per arm, 12 when the claimed change is
   under 1 % (logged-on `oldpc` resolves about 0.7 %), and a sampler profile before and after naming the function that
   moved. Proposed team ruling: packages each estimated under 1 % of H may be accepted together in one bundle series (arm
   = all of them on), each keeping its own identity check and profile delta. Bursty items (P11) are timed as paired
   per-turn differences on the event turns, which an identical game puts on the same turns in both arms.
5. **PGO and caches.** The other port's R3: a memo that won on a plain build lost on a quiet PGO build. So any package
   that keeps values (P6, P13, the kept-value items in P3 and P7) is timed again on PGO arms (each arm with its own
   trained profile) before it goes on by default. Pure removals of dead or repeated work need no PGO re-run; they are
   covered by the epic's final combined series.
6. **Into the product.** An accepted package's define is set in the x64 release configuration only by the MC, at a pin
   batch (as 9045 did); the 32-bit build never sets it, so `i0-ship` and the 32-bit DLL are unchanged and x64 = x86
   parity (D127) holds because the game is identical. The epic ends with one combined grade A series (pin + PGO against
   pin + PGO + all accepted defines) on H, M and the M client.

## 3. Packages

Value columns are seconds per turn [I]; "% H" against 20.9 s, M against 1.424 s headless. Estimates are worker hours
for build, identity, dev pair and hand-back (the grade A acceptance series runs on the testbed, not in these hours).

| Id | Package | Files it owns | Items (review IDs) | Exact | Estimate h | Value H / M [I] | Risk |
|---|---|---|---|---|---|---|---|
| P0 | Open measurements and the epic's test kit | a counting fork branch; `perf/evidence/<P0>/` | LU1 counts, A10 count, A5, DG8, OT5, OT8, MB6, MI5/A7 early profile, HL9 base, coverage of P8/P11 events | no product change | 8-12 | decides P2's H value, P15, A10's urgency, six items | low |
| P1 | PGO of the x64 DLL | `tools/x64-dll/` (a new step or configuration), training scripts | BS1 (+ an asserts-off sizing arm) | yes (identity decides) | 12-18 (+2 per pin move) | 1.0-3.1 / 0.07-0.21 (5-15 %) | low-medium |
| P2 | GameEvents args kept in the server, one crossing per raise | `server/src/remote/srv_remote_lua.cpp`, the IPC call table, `proxy/src/proxy_lua.cpp`, one option key | LU1 (a), LU12 | by construction | 14-22 | client M 0.6-0.75; client H after P0 | medium-low |
| P3 | Tactical AI | `CvTacticalAI.cpp/.h` | TA14, TA17, TA2, TA13, TA12, TA8, TA9, TA10, TA3, TA11, MB1, MB2, MB3, MB4 (tactical part), DG8 (if P0 says) | yes (TA3 with the counter detail) | 16-24 | 0.8-1.4 (4-7 %) / 0.05-0.09 | low-medium |
| P4 | Tactical analysis map | `CvTacticalAnalysisMap.cpp/.h` | TA4, TA6, TA5, TA7 | yes | 8-12 | 0.5-0.65 (2.5-3 %) / about 0.03 | low |
| P5 | Path finder and units, quick | `CvAStar.cpp/.h`, `CvAStarNode.h`, `CvUnitMovement.cpp/.h`, `CvUnit.cpp/.h`, `CvPromotionClasses.cpp` | PF2, PF3, PF4, PF8, PF9, PF11, PF12, PF13, PF14, PF15, TR5, MI7, MI9, MI10 | yes | 12-18 | 0.6-1.0 (3-5 %) / 0.04-0.07 | low |
| P6 | Path finder kept values (after P5) | as P5 | PF1 (six parts), PF6 | yes, by verify mode | 18-28 | 0.5-1.2 / 0.06-0.13 (not additive with P5) | medium |
| P7 | Homeland, builder and military | `CvHomelandAI.cpp`, `CvBuilderTaskingAI.cpp`, `CvMilitaryAI.cpp`, `CvAIOperation.cpp` | HL1, HL3, HL4, HL7, HL8, MB4 (homeland part), MI1, MI3, MI4; MI5 if P0's early profile says | yes | 8-14 | 0.3-0.5 / 0.02-0.035 | low |
| P8 | City and economy | `CvCity.cpp/.h`, `CvBuildingClasses.cpp/.h`, `CvCityStrategyAI.cpp`, `CvBuildingProductionAI.cpp`, `CvFlavorManager.cpp` | CB16, EC4, CB10, CB8, CB5, CB6, EC1, EC3, EC5-EC9, CB1-CB4, CB9, CB11, CB12, CB13, CB15 | yes | 14-22 | 0.5-1.0 late game / 0.01-0.03 | low |
| P9 | Trade | `CvTradeClasses.cpp/.h` | TR7, TR8, TR6, TR9 | yes | 5-8 | 0.3-0.6 / small | low |
| P10 | Player, plot and danger | `CvPlayer.cpp/.h`, `CvPlot.cpp/.h`, `CvDangerPlots.cpp/.h` | OT3, OT4, OT6, EC10, DG3, DG4, MB5; MB6 if P0 says | yes | 6-10 | 0.15-0.4 / small | low |
| P11 | Bursty yield passes and city-states | `CvMinorCivAI.cpp`, `CvVotingClasses.cpp`, `CvTeam.cpp` | DI1, DI2, PO3, DI5 | yes | 10-16 | event turns 0.5-2.5 each; mean 0.1-0.3 | low |
| P12 | Diplomacy, espionage, religion, policy | `CvDiplomacyAI.cpp`, `CvEspionageClasses.cpp`, `CvReligionClasses.cpp`, `CvPolicyAI.cpp` | DI3, DI4, RE1, RE2, RE3, PO1, PO2 | yes | 5-8 | under 0.15 / small | low |
| P13 | Kept values with writer lists (after P8, P10, P12) | the writer sites in those packages' files | CB7, EC2, DG1, DG2, CB14 | yes, by writer list and verify | 14-22 | 0.3-0.8 / small | medium |
| P14 | A10 and the PFHI items (only on the owner's yes; after P6, P3, P7) | `CvAStar.*` (A10 also on `i0-ship`), `CvHomelandAI.cpp`, `CvArmyAI.cpp` | A10, PF5, PF7, HL10, HL11, MI6; TA3 without the counter detail | PFHI items exact given A10 | 12-20 | 0.6-1.2 (3-6 %) / 0.03-0.06 | medium |
| P15 | GameEvents listener-presence mirror (decision after P0 and P2) | proxy + server; design first | LU1 (b) | exact only if the registration mirror is complete | 16-30 | client M 0.1-0.2 after P2 | medium-high |

Sums: P0-P14 about 160-255 worker hours; P15 16-30 more if taken.

### P0. Open measurements and the epic's test kit

What each measurement decides; all are counts or sampled shares, so the workstation is enough (indications, D106):

1. **GameEvents raises by name per turn**, M 151-160 and H 301-305 headless, with a counting define (`VP64_COUNT_HOOKS`)
   on a counting branch from the pin, as 4669 did; the run counts only if its `stats.db` is IDENTICAL. Plus the
   listeners present per name in the EXE's Lua state at a turn start of one client M run (read-only, through the harness's
   Lua channel). Decides P2's value on H and whether P15 is worth its risk (review 3.2 item 2, 5).
2. **A10 wraps and stale reads**: per finder per turn, how often the 16-bit generation wraps and how often a node from an
   earlier wrap is read as fresh (M, H, the early game). Decides how urgent the A10 question is and whether A10-on changes
   the reference windows.
3. **Shares from 4668's existing folded stacks** (`F:/c5-64bit-work/4668/runs/`, no new run; LTO inlines helpers, so
   attribute by caller): the no-embark harassment searches (A5), `GetSimulatedDamageFromAttackOnUnit` (DG8), the hot
   `getInfoTypeForString` sites by caller (OT5: each package then pins the hot literals in its own files),
   `UpdateYieldFromCorporationFranchises` (whether corporations exist at H 300: CB16's value), `ScorePlotBuild` (HL9's
   base).
4. **Early game**: a sampled profile of a Standard fixed-seed game, turns 0-75, headless (MI5, A7, and each package's
   early-game share; our saves start at 150 and 300).
5. **The autosave's 0.56 s per client M turn** split into DLL serialization, transfer and EXE write, from 0437's W4a
   timers where they split it (MB6; dropped if serialization is small). `PrefetchGameData`'s share of the server's load
   (OT8), from one sampled load.
6. **Event coverage** of the identity windows: Congress sessions, era changes, city-state friendship changes and
   corporations in M 150-160 and H 300-305; where absent, the extra saves rule 3 needs.
7. **The test kit**: one identity job and one D178 pair job, parameterised by DLL path and define, that every DLL package
   reuses (scenario saves, windows, statscmp, the define-off byte check).

Optional, only if a client place is free (UI Lua in VP's own files, upstream work after the beta, not this epic's
code): city-screen refresh time (LU11), civ-list rebuilds and tech-panel handler time per AI phase with options 2+3+4
(A13, A14).

### P1. PGO of the x64 DLL (BS1)

- clang-cl IR instrumentation (`-fprofile-generate` on the LTO build), training runs, `llvm-profdata merge`,
  `-fprofile-use` on the release LTO link [I: method; not tried here]. Training set (the other port's lesson): late and
  early turns: H 300-305, M 150-160, Standard 0-50, headless server; no path instrumentation (too slow on late turns).
- Identity: IDENTICAL on M, H and the early game; D24; x64 = x86 parity on 0351's validated tier (D127), because PGO
  changes the shipped build. Floating point must not change; the identity test decides.
- Timing: grade A on `oldpc`, release against release + PGO, H 300-305 and M 150-160, 6 runs per arm (the expected
  5-15 % is far above the resolution), sampler profiles. One extra arm built without `VPRELEASE_ERRORMSG` sizes the cost of
  the asserts compiled into release (F4, OT4): a measurement only; turning them off changes the error reports and is not
  this epic's decision.
- Upkeep: retrain at each pin move (the other port: when under 90 % of profiled code still matches); the compiler's
  out-of-date profile warnings are counted in `build-summary.json`.
- For the MC and the Architect (section 9): which configuration carries PGO (D107); where the profile file lives and how
  the one-command release build (3500) and the export's clean-build acceptance (D174) reproduce it; the instrumented
  link's memory (the release link peaks at 13.3 GB today; one whole-program link at a time, D107/D121).
- Value: 5-15 % of every server turn [I; 0437 9.8 rank 2; the other port's PGO part was about 15 % on top of
  whole-program optimisation]. Lowest identity risk of the large items; it also changes what every other package is
  worth, which is why it starts first.

### P2. GameEvents args kept in the server, one crossing per raise (LU1 part a; LU12)

- Today each raise is n + 3 crossings (`RemoteArgs`: the args object, each pushed argument, the call, the
  destructor; 5.56 on M, 6.32 on H) at about 10 us each; about 90,000 crossings per client M turn = 0.94 s (review F1)
  [L, one workstation run], which are 16,467 raises per M turn; H: 160,126 raises per turn, about 1.01 million crossings
  [L, headless counts of 4800, `speedup-p0-measurements.md` P0.1]. (Corrected 2026-11-17 by 4800: this line said
  "about 4-6 crossings" and "90,164 raises per client M turn"; the 90,164 was a crossing figure, not raises.)
- Change: the server builds the argument list locally and sends one message (call kind, event name, packed args); the
  proxy builds the EXE's args object, calls, returns the result. Needs a new IPC call (the protocol's version handshake),
  a `[Performance]` key off by default (the `options/` files belong to epic 0040 since 2026-11-17 08:09: ask the MC),
  fail loud (D118). LU12 (intern the instance-push key strings in `proxy_lua.cpp`) comes with it: same file.
- Identity: windowed S4 against S4 with the key on, M 151-160, IDENTICAL; raise counts and per-name results equal in
  both arms; offline tests with ASan; negative tests (malformed pack, unknown name).
- Timing: the client M turn with options 2+3+4, D178 dev pair on the product path, then grade A (6 per arm); the metric
  is U per turn from 0437's budget timers.
- Value [I]: most of the 0.94 s, about 0.6-0.75 s per client M turn (five of six crossings gone; the EXE's own dispatch
  stays). H depends on P0.1. Headless turns do not change. (Added 2026-11-17 by 4800, P0.1: at 0.94 s / 90,150 crossings =
  10.4 us per crossing, P2 removes 4.56 crossings per raise on M, about 75,000 per turn = **0.78 s per client M turn**, and
  5.32 per raise on H, about 852,000 per turn = **8.9 s per client H turn** [I: the per-crossing mean of one M run applied to
  headless counts; no client H turn was measured]; the one crossing left per raise is 0.17 s on M and 1.7 s on H, P15's
  ceiling. 52 % of H's raises are one name, `CanLoadAt` (`CvUnit::canLoad`, air units' city-load test per plot).)
- **Correction (lead-0504, 2026-11-17 22:30):** the estimates above are replaced by measurements. P2 saved about
  **0.025 s per client M turn** [L, 4804, windowed, workstation indication]: the removed crossings cost 0.8-2 us each;
  the `Call*` costs about 44 us per raise, of which about 36 us is waiting for the EXE's Lua critical section held by
  the EXE's main thread [L, 4811]. P15's ceiling is therefore not 0.17 s: a server-side stand-in that answers the names
  without a listener saved **0.34 s per client M turn** (3 pairs) [L, 4811]; H about 1.1 s for the wait-free part [I].
  Detail: `speedup-p2-gameevents.md` sections 9.3 and 11. The figures above stay as a trace.

### P3. Tactical AI (`CvTacticalAI.cpp/.h`)

- Dead or repeated work: TA14 (the citadel scan nobody reads; 1.6 % of M [L]), TA17 (`Sleep(1)` every 500 positions;
  keep a hand-over only when the server knows a waiter exists: the implementer checks how the server's game lock is
  waited on), TA12 (stop at the first safe plot), TA8, TA10.
- Tables instead of searches: TA2 (dense distance-to-target), TA13 (plot -> position index, stamped per position), TA9
  (open addressing for the danger cache), TA11 (candidate reuse on exact equality, verify mode).
- Allocation: MB2 (simulation vectors keep their buffers; allocation is 4.6 % of H, 6-7 % of M [L]), MB1, MB3, MB4's
  tactical part. Check first that no iteration orders by pointer value (0437 9.8 row 7).
- TA3 (the two step floods as plain breadth-first search; 4.6 % of H inclusive [L], est. 3 % H): exact only if the BFS
  still advances the step finder's generation counter; if that needs a `CvAStar.h` accessor, P5 adds it (P5 owns the
  file) and P3 takes TA3 last. DG8 joins only if P0.3 shows a share.
- Value [I]: H 0.8-1.4 s, M 0.05-0.09 s.

### P4. Tactical analysis map (`CvTacticalAnalysisMap.cpp/.h`)

TA4 (unit-major strength pass; integer sums: confirm every zone field is one), TA6 (rows by map distance; wrap
handling), TA5 (bitmaps for the dominance zones, same visit order), TA7 (skip the closest-city call when distance
decides). Runs in parallel with P3 (other file). Value [I]: H 0.5-0.65 s, M about 0.03 s.

### P5. Path finder and units, quick (`CvAStar`, `CvUnitMovement`, `CvUnit`, `CvPromotionClasses`)

PF2 (one `canEnterTerrain` evaluation for both answers), PF4 (obsolete child before `PathCost`, **keeping the guard that
the danger map is clean**, since `PathCost` can trigger the lazy rebuild), PF3, PF8, PF9, PF11 (and the river vector
copy), PF12, PF13 + PF14 (army searches), PF15 (army part; the trade-path variant measured, not built), TR5 (sort the
destinations once per list), MI9 (ready gate in `AI_promote`, after checking that `canPromote` is false whenever no
promotion is ready), MI10, MI7. Value [I]: H 0.6-1.0 s, M 0.04-0.07 s. `CvUnit.cpp` is also edited on 0437's prototype
branches (4676, 4677, 4678: embark and move paths); those functions are not touched here, and the MC's pin batch
resolves any text conflict.

### P6. Path finder kept values (PF1, PF6; after P5, same files)

PF1's six missing parts (territory and terrain in `PathValid`, unit and trait data in `GetCostsForMove`, end-turn costs;
the danger part is upstream since 472e1163d) and PF6 (skip bound or dead neighbours: the stop-node condition is subtle).
One verify define per part with the negative control; timed again on PGO (rule 5). Value [I]: H 0.5-1.2 s, M 0.06-0.13 s
(the largest M item), not additive with P5's PF2/PF4.

### P7. Homeland, builder and military

HL1 (same-plot directive lookup by reference), HL3 + HL4 (the build-time bound before the expensive tests, keeping the
lazy danger rebuild where it is), HL7 (adjacent build lists kept within a worker pass, verify mode; est. 1 % M), HL8,
MB4's homeland part, MI4 + MI3 (three approach walks fused; est. 0.7 % H), MI1. After it, HL9's question (a pool for
`ScorePlotBuild`) is answered by a re-profile, not built. MI5 only if P0.4's early profile shows a share. Value [I]: H
0.3-0.5 s, M 0.02-0.035 s.

### P8. City and economy

CB16 (corporation franchise yields walk only present buildings; medium-high late game, if corporations exist: P0.3),
EC4 (static indexes for `GetBuildingYieldValue`'s loops; 0.2-0.3 s H), CB10 (apply only changed flavors), CB8 (no
tooltip text without a sink; present at the pin), CB5 + CB6 (skip zero rows in `processBuilding`), and the small static
indexes and loop hoists EC1, EC3, EC5-EC9, CB1-CB4, CB9, CB11-CB13, CB15. If P11 needs a narrowed yield-update entry in
`CvCity`, P8 adds it. Value [I]: H 0.5-1.0 s late game, M 0.01-0.03 s.

### P9. Trade (`CvTradeClasses.cpp/.h`)

TR7 (furthest possible route from the cache row; present at the pin; 0.2-0.4 s H), TR8 (turn modifier walks only
relevant buildings), TR6, TR9 (with the listener fallback). Not the trade-path cache: that is 4673's (TR3) and stays
there. 4673's prototype edits the same file on its own branch; the MC orders the two if both reach the product.
Value [I]: H 0.3-0.6 s.

### P10. Player, plot and danger

OT3 (a visibility change skips the unit loop on quiet plots; 164,000 visibility changes per H turn [L, 4669]), OT4 (the
resource-count asserts in one map walk; asserts are compiled into release), OT6, EC10, DG4 (stamped array for ranged
tiles), DG3, MB5; MB6 if P0.5 says. `CvDangerPlots` is also 4674's prototype file: these items are outside the reach
cache it adds. Value [I]: H 0.15-0.4 s.

### P11. Bursty yield passes and city-states

DI1 (city-state bonus passes skipped when nothing changes; frequent with 20 city-states), DI2 (Congress yield passes
restricted; theirs 1,449 -> 91 ms per session), PO3 (era-change yield passes restricted; theirs 1,373 -> 63 ms), DI5.
After P8 if a narrowed `CvCity` entry is needed. Timing as rule 4's bursty method, on saves where the events happen
(P0.6). Value [I]: 0.5-2.5 s on each event turn, 0.1-0.3 s on an average H turn; it flattens the worst turns, which the
player notices more than the mean.

### P12. Diplomacy, espionage, religion, policy

DI3, DI4, RE1 (keeps the trade-path cache triggers in order: they are 4673's rebuild points), RE2, RE3 (the skipped call
raises CityCanTrain/PlayerCanTrain, which cross our IPC until P2), PO1, PO2. Low value; spare capacity. Value [I]: under
0.15 s H.

### P13. Kept values with writer lists (after P8, P10, P12)

CB7 (class-happiness update only for relevant buildings; VP has many dummy buildings), EC2 (total building count per
player), DG1 + DG2 (a city danger power table for `CvCity::IsInDangerFromPlayers` within one diplomacy re-evaluation;
diplomacy triggers danger rebuilds worth 5.1 points of the H turn [L, 0437]), CB14 (light refills in `SetWorkingPlot`,
exact only by a read census redone on 5.4.8). Each needs the complete list of writers of its inputs, a verify define and
the negative control, and PGO timing (rule 5). Value [I]: H 0.3-0.8 s.

### P14. A10 and the path-finder items it unlocks (only on the owner's yes)

A10 widens the node cache's generation stamp so that a node stamped exactly one wrap earlier can no longer read as fresh
(`CvAStar.h`, `CvAStarNode.h`). It changes the game from the first stale read after a wrap, so it goes into both pins
(`i0-ship` and x64, as bug 0116's fix did) to keep x64 = x86 parity (D127), and from then on identity pairs compare A10-on
against A10-on. Then, exact given A10: PF5 + PF7 (neighbour fills deferred to expansion, none for dead nodes; fills are
8 % of H [L]), HL10 + HL11 (garrison search stops at a 0-turn answer; check the readers of `SetMovesToTarget`), MI6, and
TA3 without the counter detail. Value [I]: H 0.6-1.2 s (3-6 %), plus a defect gone that is also a candidate cause of
windowed-vs-headless divergence (review section 4).

### P15. GameEvents listener-presence mirror (LU1 part b; deferred decision)

With P2 one crossing per raise remains (about 0.1-0.2 s per client M turn [I]). Removing it means the server must know
whether a name has any listener in the EXE, which needs a read of how the EXE registers `GameEvents` listeners (not in
the knowledge base), a generation the proxy bumps on every Add and Remove, fail loud, a verify mode and a windowed
identity pair; a missed registration would skip a real listener and change the game. Decide after P0.1 (how many raises
on H; which names have listeners) and P2's measured gain; the design goes to the Architect first.

## 4. Order and dependencies

### 4.1 Epic 0437 (the turn-time study, priority 8, still running)

- Not duplicated: TR3 and K1 (4673's trade-path reuse), DG5 and DG6 (4674's danger-reach cache), A11 and A12 (4677,
  parked by the MC), and the embark and move paths of 4676 and 4678. No package touches those functions.
- **Built on 0437 only after its pairs:** TR4 (a narrower search signature on top of 4673) and DG7 (the danger derive
  step shared across observers, on top of 4674) wait until 4673's and 4674's testbed pairs (laptop jobs `c8ac` and
  `b874` of 2026-11-17) show a gain and the MC decides who turns those prototypes into product code. If that falls to
  this epic, they become two more packages, timed on PGO (their R3 warning).
- PGO is rank 2 of 0437's 9.8 list; 0437 has no PGO ticket (4650-4678 at 519cba51a [C]), so P1 builds it here. Tell
  0437's lead, so its report quotes P1's result rather than a second estimate.
- The testbeds: the laptop is 0437's quiet space while it runs (D163); this epic times on `oldpc` beside 0437's oldpc
  jobs in the order the MC sets, and on the laptop after 0437. P2's client timing reuses 0437's runner and budget
  timers: from `main` once 0437 merges, or from the epic branch with the MC's leave.
- D165 options 2+3+4 (0437's 4662, 4665, 4675) must be on in every client arm of this epic: without them the drain, not
  the served Updates, sets an M turn and P2 and the DLL packages show little.

### 4.2 Where PGO comes

P1 starts in wave 1, in parallel with everything (it edits only the build tools). Packages develop and take their dev
pairs on the plain release build; once P1 is accepted, the kept-value packages (P6, P13, the kept items of P3 and P7)
are timed again on PGO arms before their defines go on (rule 5); the epic's final combined series runs on PGO.

### 4.3 File ownership and the order it forces

| Files | Packages, in this order |
|---|---|
| `CvAStar.*`, `CvAStarNode.h`, `CvUnit*`, `CvUnitMovement.*` | P5 -> P6 -> P14 (P3's TA3 uses an accessor that P5 adds) |
| `CvTacticalAI.*` | P3 -> P14's TA3 simplification |
| `CvHomelandAI.cpp` | P7 -> P14 (HL10, HL11) |
| `CvCity.*` and the building files | P8 -> P11 (only if a narrowed yield entry is needed) -> P13 |
| `CvPlayer.*`, `CvPlot.*`, `CvDangerPlots.*` | P10 -> P13 |
| `CvDiplomacyAI.cpp` | P12 -> P13 (DG2) |
| `srv_remote_lua.cpp`, the IPC call table, `proxy_lua.cpp` | P2 -> P15 |

### 4.4 Waves (3 workers; start order by value against risk)

1. **Wave 1:** P0 (worker 1), P1 (worker 2; its long links leave room for its own training scripts), P2 (worker 3).
2. **Wave 2:** P3, P5, P4, P8 as places free (P0's results feed P3's DG8 and P8's CB16).
3. **Wave 3:** P7, P9, P10, P6 (after P5), P11 (after P8 if needed).
4. **Wave 4:** P13 (after P8, P10, P12 and P1), P14 (after the owner's yes, P6, P3, P7), P12 when a place is idle, P15 if the
   MC and the Architect decide so.
5. **Close:** the combined grade A series, the pin batch request to the MC, the findings in `research/08-performance/`.

## 5. Where the code lives and what goes upstream

- **DLL:** the Community-Patch-DLL fork, one branch per package, `perf/speed-<package>`, cut from the current x64 pin,
  each change behind `VP64_SPEED_<PACKAGE>` (D104: one source builds both ways, the 32-bit DLL unchanged). Pushed to
  `origin` only as those branches; never a commit to the fork's `master`, `i0-ship`, `x64`, `x64-batch2` (the pin
  branch) or an upstream-tracking branch; no pull request. A10 and A9, if the owner says yes, go as fix branches for both pins
  the way bug 0116's did (9044, 9045), merged by the MC.
- **Server and proxy:** a branch of this repository per package; the IPC protocol change of P2 is asked of the MC with
  its paths (shared files).
- **Upstream:** the exact speedups and the two defect fixes are candidates for the `64bit` folder export and the DLL
  pull request behind defines (D168, D174) and for upstream bug reports (A10, A9, TA14's dead code). Nothing is sent
  before the beta (D130), every text is scrubbed (D95), and nothing goes without the owner's yes to the exact text.

## 6. Decisions for the owner

Each is a behaviour change: the game differs from the first affected decision. **Note:** D169's Extras are visuals only
(the owner, 2026-11-16 [reworded for privacy]: approved, visuals only; a third category for extras that change the game is reserved, not designed), so a
game-changing DLL fix does not fit the bugfix flag of increment 4. The route that exists is a core defect fix in the
fork on both pins, as bug 0116 was. The questions are framed that way.

1. **A10 (16-bit path-finder stamp).** Should the path finder's node-cache stamp be widened so that cached facts can no
   longer survive a counter wrap (a defect present at our pin; on H a finder wraps every few turns [I]), as a core fix on
   both pins with an upstream bug report after the beta? **Recommendation: yes, once P0 has counted the stale reads on
   M and H**; it removes a candidate cause of windowed-vs-headless divergence and unlocks P14 (3-6 % of an H turn [I]).
2. **A9 (wonder specialization picks built wonders).** Should the AI stop choosing wonders that are already built when
   it picks a city's wonder specialization (VP defect at the pin: `ChooseWonder` has no built test before the sanity
   check; in the other port 14 of 20 picks were built wonders), as a core fix on both pins with an upstream bug report
   after the beta? **Recommendation: yes**, after P8's implementer confirms the sanity check does not already reject
   them (if it does, the skip is exact and needs no decision).
3. **A1 (battle-simulation candidate lists reused).** Should we build the other port's largest AI speedup after PGO,
   which changes how the AI fights by design? **Recommendation: no, not now**: keep the epic exact; revisit only as an
   option of the reserved third Extras category if late-game turns are still too slow after this epic.
4. **A2 (one diplomacy re-evaluation per war cascade).** Should war-cascade turns re-evaluate diplomacy once instead of
   per declaration (bursty; small drift later)? **Recommendation: no, not now**; revisit with a measurement on the 42-major
   stress map (D157), where cascades are frequent.
5. **A4 (skip the single-unit opportunity attack with nothing near).** **Recommendation: no**: a small saving (about
   0.1 s of their turn) for a change in unit decisions.

## 7. Priority and team shape

- **Priority 6.** Below 0437 (8), which decides the order of the largest items (its prototypes, PGO's place) and holds
  the laptop; above the units epic 0040 (4) and the Extras (2), because the AI turn is the player's longest wait in a
  late game, every package is exact (no gameplay decision), the packages are independent and small, and the first wave
  (P1, P2) holds most of the value for little risk. If increment 3 needs the places, wave 1 alone (P0-P2) at 6 and the
  rest at 4 is the fallback.
- **Team:** one lead and 3 workers (Opus), on account B as a queue epic (D97, D139: account A runs it only if B's epic
  place is taken); the lead keeps the file-ownership order of 4.3 and the testbed queue. The performance standards team
  (7000) reviews each acceptance series and the combined series (D40), as it reviewed 0437 (7014-7017). The Architect is
  asked about P1's profile-file question and P15's design (section 9).

## 8. Excluded or deferred, and why

- **N/A for our mod set** (no listener in CP + VP + EUI, review F2; or the EXE's code): LU2-LU10, A8, B1, B3; BS2 (no
  run-time switches); OT9 (no pool); A15 (done in kind by D165); A16 (mod not in our set).
- **IN WORK in 0437:** TR3, K1, DG5, DG6, A11, A12; TR4 and DG7 wait for them (4.1).
- **Parallel batches deferred:** TR1 + TR2, HL12 + HL13, PF16, PF17, TA15, HL9's pool. Large cost, high thread-safety
  risk in a single-threaded DLL, 0437 counted little headroom across players (rank 6), the other port's working batches
  gave 2-6 % each (L4), and PGO and 4673 shrink the trade family first. Revisit after P1 and 4673's decision with a
  counting run of ordered-batch headroom.
- **OT1 + OT2 plot bitsets** (with HL6, HL14): medium-large cost for little measured value; only if another item needs
  them.
- **Negligible for us** (0.1 % of a turn or less [L]): TA1, TA16, HL2, HL5, HL6, HL14, HL15, HL16, MI2, MI8, MI11, MI12,
  MI13 (and PF11 except its vector copy, taken in P5).
- **OPEN until P0:** A5 (look for an exact landmass test only if the share is large), A7 (after P7's exact HL items),
  MI5, DG8, OT5's list, OT8, MB6, HL9.
- **UI Lua in VP's own files** (LU11, A14; A13's speed part): our product ships VP's Lua unmodified; any change is upstream
  work after the beta (D130), sized by P0's optional client measurements.
- **Not recommended:** A3, A6, B2, B4 (their own low ranking or stale answers; PO2 gives B4's saving exactly), and the
  other port's kept-off (K2-K22) and rejected (R1-R14) items, recorded as theirs in the review.

## 9. Not verified, and open points for the MC and the Architect

- Every value is [I]: the other port's figures scaled to our H, or 0437's sampled shares (workstation, one run for H).
  The planning figure halves the item sum on purpose.
- The define-off byte identity (rule 3) assumes the x64 release build reproduces the pin's bytes from the same source and
  epoch, which 0159 showed once (2026-11-13) [L]; P0's test kit confirms it for a branch with an empty define.
- P1: whether a PGO profile may be committed (it is derived from running our GPL DLL, not from game files, but D83's
  reading is the MC's), how the one-command release build (3500) and the export's clean-build acceptance (D174) reproduce
  a PGO build, the instrumented link's memory, and whether the server flushes the instrumented DLL's profile on exit.
- P2: the IPC protocol change touches files shared with other epics (the call table, possibly `proxy_rt.cpp`); paths to be
  asked of the MC. TA17's hand-over condition depends on how the server's game lock is waited on (not read here).
- Line numbers in the review are at 04b1211b8; the fork's working tree was used only to place functions in files
  (`IsInDangerFromPlayers` is in `CvCity.cpp`, `GetBuildingYieldValue` in `CvCityStrategyAI.cpp`,
  `GetSimulatedDamageFromAttackOnUnit` in `CvTacticalAI.cpp`) [C, 2026-11-17].
- The staging documents were not re-read for this proposal; the review's paraphrase was enough for every package.

Sources: `research/08-performance/external-port-speedups-review.md` (9077); on `epic-0437-turn-study` at 519cba51a:
`turn-time-study-analysis.md` 9.8, `d165-pacing-fix-design.md` 10, tickets 0437 and 4650-4678;
`research/06-tooling-and-workflows/x64-dll-build-configurations.md`;
`research/07-architecture-decisions/architect-2026-11-16-increment-4-extras-epic.md` (D169's categories);
`docs/briefs/PERFORMANCE.md`, `docs/briefs/performance-standards-team.md`; decisions D24, D40, D83, D95, D104, D106,
D107, D121, D127, D130, D139, D157, D163, D164, D165, D168, D169, D174, D178, D179.
