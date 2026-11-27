# DLL pin batch 5: the path finder's not-refreshed node-cache read fixed (4813), and the x64 interface version (4719)

Ticket 0551 (account B, 2026-11-18), branch `task-0551-pin-batch-5`. Batch 5 sits on pin batch 4 (ticket 0517,
`upstream-fix-batch-4.md` section 9: `i0-ship-batch4` e96457fd5, `x64-batch4` c29442e8c) and adds:

1. **The finder fix** (ticket 4813, epic 0504; `cnip-not-refreshed-neighbour-read.md` on epic 0504's branch): the unit path
   finder refreshes the node cache of a ring-2 destination's common neighbour (`CommonNeighborIsPassable`) and of the unit
   finder's extra children (`CvAStar::CreateChildren`) before reading it, so a search's answer no longer depends on which
   searches ran before it. Fork `fix-cnip-i0ship` 02749f1b9 / `fix-cnip-x64` 909f73bd1, one commit each, patch-id
   `3e180c82`. **Changes game results on purpose** (4813: D10 tiny from turn 43, standard 50, huge 355; windows M 157, H 302,
   E 49).
2. **The out-of-process interface version** (ticket 4719, `upstream-export-e4-dll-commit.md` sections 8-9), x64 only: fork
   `x64-ifver` 344bdff8e, `CvOutOfProcessInterface.h` (`VP_OUT_OF_PROCESS_INTERFACE_VERSION 1`) and the export
   `DllGetOutOfProcessInterfaceVersion` behind `VP_OUT_OF_PROCESS`. From this pin a server built against the pinned source
   refuses an x64 DLL without the export (8.5 there); the 32-bit preprocessed text is unchanged (9.4 there).

Nothing goes upstream (D95, D130): the finder fix's upstream draft waits for the owner's yes. Epic 0504's package (4807) is not
part of this batch; it rebases onto batch 5 in 0504/0562.

Fork clone for this work: `C:/c5-64bit-work/0551/fork` (`git clone --shared --no-checkout` of the owner's fork clone, remote
`origin` = our GitHub fork). Scripts: `server/evidence/0551/` (`s01_fork.sh` ...).

## 1. Branches [C, 2026-11-18 21:30-21:37 EST]

`s02_branches.sh` (log in the script's output, kept as `server/evidence/0551/s02-branches.txt`):

| Branch | Head | Made as | describe | tree | source fingerprint |
|---|---|---|---|---|---|
| **`i0-ship-batch5`** | `5d18d3d6cbe4075d66271ec55a2e6cfe3e466687` | `i0-ship-batch4` e96457fd5 + `cherry-pick -x` 02749f1b9 | `Release-5.4.8-50-g5d18d3d6c` | `656a1eff` | `v1 53d85121df7a8e67b90696cd9e738ac45e4f02a64f1a4d8e31fe89066c4eec21` 473 files |
| **`x64-batch5`** | `1fd51353e7640ec97f9657c2380f2268c7be9a81` | `x64-batch4` c29442e8c + `cherry-pick -x` 909f73bd1 (-> `fca1f5b56`) + `cherry-pick -x` 344bdff8e (-> `1fd51353e`) | `Release-5.4.8-65-g1fd51353e` | `fd043a50` | `v1 fe2fd907dacf6e16bdfa313376aae24661f92e009310948581ae3824f782af16` 474 files (the new header) |

- One commit per fix, no conflict, no hand edit.
- **Patch-ids** (`git patch-id --stable`): the finder fix `3e180c82` on both pins (the picks and both fix branches); the
  ifver pick `94f26572` = `x64-ifver`'s; batch 4 -> 5 whole: i0 `3e180c82`, x64 `d20a72f4`, x64 without the ifver pick
  `3e180c82` (so the x64 batch is exactly the i0 batch plus the ifver commit). Diffstat: i0 2 files +19 -1; x64 4 files
  +49 -1.
- **Cross-check (the carry's route)**: `x64-batch4` + the ifver pick + a `--no-ff` merge of `i0-ship-batch5` gives tree
  `fd043a50`, the tree of `x64-batch5`; `git show --remerge-diff` empty.
- None of `6f2c2ee3`, `e29032ad`, `90f1c4c0` is an ancestor of either head (`merge-base --is-ancestor`).
- Commit messages: the two fix commits' own (made by 4813 and 4719, checked by the MC for internal ids, 4719's log) plus
  git's `(cherry picked from commit ...)` line, as 0517's picks.
- **Pushed** to our GitHub fork as new branches 2026-11-18 21:36 EST (`s03_push.sh`: refuses a branch not at the expected
  commit, a forbidden ancestor or a name that exists on origin; never forced). `i0-ship`, `master` (e96457fd5), `x64`
  (c29442e8c) and the batch-4 branches untouched.

## 2. Carry series [C, 2026-11-18 21:37-21:41 EST]

`tools/release/series/carry.tsv`: a `port` row on `x64` (`1fd51353e`, the ifver pick: from this pin a server built against
the source refuses a DLL without the export, so the x64 build needs it) and a `fix` row on `i0` (`5d18d3d6c`, the finder
fix), both naming the batch-5 commits on the pushed branches. `branches.py --base Release-5.4.8 --series
tools/release/series/carry.tsv --name check0551 --no-fetch --compare-i0 i0-ship-batch5 --compare-x64 x64-batch5`
(`s04_carry.sh`; local branches `*-release-check0551`, not pushed): **57 applied, 0 dropped, 0 skipped, both trees equal**
(`server/evidence/0551/branches-check0551.json`). The clone fetched `i0-fix-13566 i0-fix-13482 i0-fix-13395 i0-fix-0421`
first (0497's finding).

## 3. Locks and descriptor (before the take)

`deps/i0.lock`, `deps/dll.lock`: `branch`, `commit`, `describe`, `fingerprint` (x64), `commits` (i0), `source` and a
re-pin paragraph each with a "Replaced" trace. The release keys (`release`, `instance`, `verified`; dll.lock `tools`,
`epoch`) follow the take (section 5); batch 4's are kept as `*-b4`. `tools/release/releases/Release-5.4.8.json`
`reference_counts` set before the take (the candidate descriptor copies them at testdata) to 4813's x86 = x64 counts of
batch 4 + the finder fix alone, **30,569 / 359,261 / 685,488** (was 31,226 / 349,074 / 683,261), since `x64-ifver` sits
behind `VP_OUT_OF_PROCESS` and the save test's x86 arm cannot see it; `tools/release/tests/test_descriptor.py` follows
(32/32).

## 4. Proxy model: no change [C, 2026-11-18 21:45-21:52 EST]

Run before the take so that its model stage (CL 10) does not stop after the builds (0463 3.4 item 2):
`proxy/gen/regen_model.py --src <git archive of 1fd51353e> --lock deps/dll.lock` under `quietlock.py run`
(`s06_model_check.sh`, 380 s): **exit 0, EQUAL**: `interfaces.json`, `slot-proof.json`, `lua.json`, `model.json`,
`policy-table.tsv` and the generated runtime equal the committed model; the only differences are informational
(`source.commit` c29442e8c -> 1fd51353e, and 12 `counts.ast.tus` values that depend on the source path). So neither the
finder fix (a `CvAStar.h` declaration made `const CvAStar*`, no record field) nor the ifver export (a new `extern "C"`
function, not a DLL -> engine call) reaches the model; nothing regenerated.

*Correction 2026-11-20 (MC, from ticket 0602):* "nothing regenerated" left a fault. The model files record the x64
commit they were generated from, and without `--write` they stayed at c29442e8c while `deps/dll.lock` named 1fd51353e,
so `tools/harness/tests/test_hi_coverage.py::test_the_repository_model_is_matched_whole` failed on main from this batch
until 0602 wrote the model at 1fd51353e (0 of 1,615 methods changed; main b2d5815f7). An EQUAL report does not show the
recorded commit: at every pin move run `regen_model.py ... --write` even when it reports EQUAL
(`research/06-tooling-and-workflows/new-vp-release-checklist.md` step 10).

## 5. The take: validated tier, D162 journeys

`s10_take.sh`: `take_release.py Release-5.4.8 --tier validated --carry pins --for-art --fork C:/c5-64bit-work/0551/fork
--work C:/c5-64bit-work/0551/run --scratch F:/c5-64bit-work/0551/scratch --who 0551 --gate-wait 60` with
`C5_RELEASE_INSTANCE`/`C5_RELEASE_HI_INSTANCE=c5h-a,c5h-c` (C: instances only: no game launch on the F: instances, MC
#5993), `C5_RELEASE_TESTBEDS=local` (whole games on the workstation), started 2026-11-18 21:52 EST at tools ec2edbd2e
(clean).

**First pass FAILED at the end of the build stage** (22:38): "this repository moved during the build stage (ec2edbd2e
dirty=0 -> ec2edbd2e dirty=1)": I had edited this file (tracked) while the stage ran. The rule is 0463 3.4 item 1: no
tracked change in the worktree until "stage build ok". Committed (1e09fc7c2) and rerun `--from build` 22:40: the i0
builds were reused, x64 rebuilt.

**Build stage ok 22:40:15-23:02:25 [L]** (x64 1,306 s on a busy machine: 0517's took 730 s; the i0 builds of the first
pass, i0 539 s and i0ia 550 s, link 359 s; the `-ia` patch chosen by base: 0463's `ia-39f17f754-440878983.patch`, applied
without conflict). x64: build_all steps stubs, cvgamedatabase, cvlocalization, dll, host, smoke rc 0 ("Supporting 22 major
civilizations", version "Release-5.4.8-65-g1fd51353e Copy"); unresolved 0; narrowing 794 against 798 (0 new, 4 gone).
Layout `C:/c5-64bit-work/0551/run/layout/x64-1fd51353-p1e09fc7` (server and proxy from repository 1e09fc7c2, repo_dirty 0):

| File | SHA-256 | Size (B) |
|---|---|---|
| `bin/CvGameCore_Expansion2.dll` (x64-batch5 1fd51353e) | `78355A759E8A77E367E2B4162DD8F6779848D382FF08722CEB3E4F15E2AEAFE6` | 23,681,536 |
| `bin/server.exe` (repository 1e09fc7c2) | `A6026D538FDB83F38992190165FDBB6C97ED951C98F5BE870CEFB90398FA92F7` | 8,269,824 |
| `proxy/bin/CvGameCore_Expansion2.dll` | `5BAB1B6C16822042F75D10C93AC1634ACFAE99933029642E791FE5043CA02C1E` | 1,306,624 |
| x86 plain `i0ship-5d18d3d6` (i0-ship-batch5 5d18d3d6c) | `00D35A6E9E06CA24A3AFC3DA7739B10F676C522DB250AA16D045EADE9DA5C1CC` | 17,153,536 |
| x86 `-ia` `i0ship-5d18d3d6-ia` | `E72F08809A6B6F9C81C650D0B9E10FD4C044059D6705531F2AA5AD8CB38BE29E` | 17,482,240 |

`lua51_x64.dll`, `CvGameDatabase_x64.dll`, `CvLocalization_x64.dll`, `civ5save.exe` = the batch-4 layout's (30724D6D,
9C37B39E, 35863EDE, 409422EF). Sizes against batch 4's pinned builds: x64 DLL +512 B (23,681,024; 4813's fix-only build
was 23,681,536 too), x86 plain +512 B (17,153,024), `-ia` +512 B (17,481,728). server.exe +560,640 B and the proxy +111,104 B
against 58724e072's are `main`'s own growth since 2026-11-17, not the batch.

census (x86 = x64, 173 TUs each, 0 x64-only records; 621 s), model (equal; 571 s), purity, pack ok (23:02-23:25).

**Offline stage, first try STOP at hutest [L, 23:30-23:32]**: rt exit 0 (272 s); hutest exit 1: "boot FAILED: kRt_Boot: ok
0, message 'interface version missing: the DLL has no DllGetOutOfProcessInterfaceVersion export, this server expects
interface version 1'". The batch-5 server is built against a source that has `CvOutOfProcessInterface.h`, so it refuses
any x64 DLL without the export (the rule of `upstream-export-e4-dll-commit.md` 8.5, working as designed), and hutest's
fake game DLL (`proxy/tools/hutest/hutest_dll.cpp`, built against the same source) had none. **Fix** (c954a2a09): the fake
DLL exports `DllGetOutOfProcessInterfaceVersion` returning `VP_OUT_OF_PROCESS_INTERFACE_VERSION` when
`__has_include("CvOutOfProcessInterface.h")` (so against a batch-4 or older source it still has no export, as the real
DLL of that source). Checked on the take's build (`s07_hutest.sh`): hutest PASS 55 checks 0 failed, the server's log
"boot: interface version: the DLL's is 1, as this server expects"; `--scenario all` PASS (19 scenarios as expected).
Other stand-in x64 DLLs loaded by a server.exe: none found (`server/tests/remote/dll_fatal_standin.cpp` is loaded by a unit,
not by server.exe; the proxy and pack tests' stand-ins are 32-bit) [I: a grep of `server/tests`, `proxy/tools`,
`tools/release/stages`, not a run of every suite against a batch-5 server].

**Offline ok after the fix [L, rerun `--from offline` 23:41]**: hutest 123 s, scenarios 164 s, verify leg 48 s (rt kept
from the first try).

**Save test (run_d10, D24) PASS [L, 23:47:34-00:22:52 EST, 2,118 s]**: tiny, standard, huge each parts 1, 2, 3; x86
direct = x64 direct **30,569 / 359,261 / 685,488 rows** (15 / 15 / 14 tables) = the descriptor's new reference counts,
i.e. exactly the counts 4813 measured for batch 4 + the finder fix alone; every host run reached its turn with 0 Lua
errors. Report `server/evidence/0551/d10-take-report.txt` (user paths as `%USERPROFILE%`); run folder
`F:/c5-64bit-work/0551/scratch/savetest-1`.

drift (Tiny 0-100 x86 = x64, 30,569 rows each, no differing turn) ok 00:22-00:24; fixchecks 0096 PASS (both arms'
`0096-RESULT PASS` lines; the tool's own exit 1 is not the verdict, as in 0517), 0084 PASS, 0103 PASS, oldsave exit 0
(00:24-00:28); refsave P10 ok (the descriptor's huge save loaded at 350 and played to 352, 0 Lua errors);
**instance** (c5h-c; c5h-a was refused at the claim, "a process runs from it", someone else's; 00:29-00:49): tiny arm
**208,403** answers verified, huge arm **370,660**, both 0 mismatches, 0 lock errors, no FALLBACK; the purity table bound
to the x64 DLL `78355a75...`; front end restored, proxy and DLL set back, `verify` rc 0, claim released by the stage.

**wholegames** (host `local`, the workstation, 00:50-02:54, 7,491 s; not a timing): **4 of 4 x64 = x86, IDENTICAL**
(16 tables each). These are the new whole-games references of the batch-5 pins; every ending changed against batch 4
(the finder fix changes games on purpose), so no result of batch 4's whole games is comparable:

| Game (seeds) | Batch 5 ending, rows (x64 = x86) | Batch 4 (0517 9.5) |
|---|---|---|
| Tiny Continents (tc, 5051/5253) | Diplomatic, turn 445, 1,381,434 | Cultural 425, 1,258,102 |
| Standard Fractal (sf, 5757/6868) | Science, turn 401, 2,098,323 | Diplomatic 345, 1,375,306 |
| Standard Pangaea (sp, 3333/4444) | Cultural, turn 363, 1,569,886 | Science 396, 1,863,156 |
| Small Pangaea (mp, 1819/2021) | Diplomatic, turn 325, 945,978 | Cultural 367, 1,172,594 |

Evidence: `server/evidence/0551/wholegames-{tc,sf,sp,mp}-compare.txt`, `wholegames-run.log`.

**hi_journey: verdict passed, D162 for the owner: yes [L, c5h-c, 2026-11-19 02:55-03:37 EST]** (the stage claimed c5h-c
itself with `--ticket 0551`; c5h-a was held by w5-4907; `AutoUnitCycle = 1` set by the stage since 0530):
release-basic **PASSED** (product 63 steps 547 s, stock 63 steps 485 s; 6/6 checkpoints the same) and release-large
**PASSED** (product 21 steps 425 s, stock 21 steps 410 s; 5/5 the same), on x64 DLL `78355a75...`, server `a6026d53...`,
proxy `5bab1b6c...`, i0ia `e72f0880...`; `take_release.py --art-check`: "for the owner: yes". Restore ok, `verify` rc 0, claim
released 03:37:47. One crash without a box during the stage: **pid 7672, 03:17:07, 0xC0000005**, the stock replay of
release-basic on the `-ia` DLL after its 63 steps had passed (replay exit 0); the journey ends by the game's own EXIT +
Yes, and the EXE then faults at the unload of the `-ia` DLL: ticket 0483's known exit crash
(`../06-tooling-and-workflows/scene-catalogue.md` section 12; test builds only), not 0576's server-side `CvLuaUnit` use
after free (no server and no proxy ran in that game). It kept the generic signature "exe-crash ... (no proxy crash
record)" because the exit came from the UI, not a Lua `UI.ExitGame` request, which `dialogwatch.known_exit_cause` needs.
0517 met it at the same step three times. Evidence `F:/c5-64bit-work/0551/crash-7672/`. Visual Checks: the HI lane's
checkpoint pictures are in the feed as DIAGNOSTIC under issue 0551; `visualcheck.py list --issue 0551 --result
undescribed` lists none.

**Tier validated reached 03:37:47.**

## 6. References remade and the crossed save test

The references remade on the batch-5 pins by the take (section 5): the D10 save test (tiny 0-100, standard 0-200, huge
350-400; 30,569 / 359,261 / 685,488 rows, the descriptor's `reference_counts`), the whole-games stage (four games, new
endings and rows), drift, the instance arms. The window references (M 150-160, H 300-305, E 0-50 of `perf/speedkit`) on
the batch-5 x64 DLL are 4813's (`cnip-not-refreshed-neighbour-read.md` section 5: its release fix build played exactly
the trace arm's M, H and E games): they were played on the batch-4 layout with only the DLL swapped, so they hold for
batch 5 by the b5 = cnip identity below [I: the windows were not replayed on the batch-5 layout here]. Results taken on
batch 4 or earlier pins are not comparable.

**Which change moves which game [L, 2026-11-19 03:40-03:45 EST]** (`s12_statscmp.sh`: `tools/d10/statscmp.py`, whole
`stats.db`, D24's skips only, both arms' direct plays of the D10 save test; first turns `s17_first_turns.py`; outputs
`server/evidence/0551/statscmp/`, `summary.txt`). "b4" is 0517's take savetest (batch 4), "cnip" is 4813's parity
run_d10 of batch 4 + the finder fix alone (its x64 DLL in the batch-4 layout bin, server 58724e072):

| Pair | tiny x86 / x64 | standard x86 / x64 | huge x86 / x64 |
|---|---|---|---|
| **batch 5 (this take) vs cnip** | **IDENTICAL / IDENTICAL** (30,569) | **IDENTICAL / IDENTICAL** (359,261) | **IDENTICAL / IDENTICAL** (685,488) |
| batch 5 vs b4 | DIFFERENT / DIFFERENT, 14 of 15 tables, from turn **43** (`MapUnitsState`) | DIFFERENT / DIFFERENT, 14 of 15, from **50** (`MapUnitsState`) | DIFFERENT / DIFFERENT, 13 of 14, from **355** (`BuildingInstantYields`, `HandicapYields`, `InstantYields`, `MapUnitsState`) |

So in the three D10 games **batch 5's only change of a game result against batch 4 is the finder fix's**, with exactly
4813's first turns and tables; **`x64-ifver` changes none** (and neither does the newer server.exe of 1e09fc7c2 against
58724e072): batch 5 equals batch 4 + the finder fix row for row in both arms.

**The crossed save test against batch 4 [L, 2026-11-19 03:45-04:46 EST]** (0517 9.6's method, `s13_crossed.sh` via
`s14_refs.sh`; `run_d10.py --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, AI logging off, the huge game
from the durable reference save `F:/c5-64bit-work/reference-saves/Release-5.4.8-HUGE_0350.Civ5Save`). Unlike 0517, each
x64 side plays in **its own pin's layout**: a batch-5 server refuses the batch-4 DLL (no interface-version export, section
5), so arm A's batch-4 x64 DLL runs in the batch-4 layout `x64-c29442e8-p58724e0` (server 58724e072, built against an
unversioned source):

| Run | x86 | x64 (layout) | part 1 writer | part 2 x86 direct vs x64 direct | part 3 other DLL vs owner |
|---|---|---|---|---|---|
| A (03:45-04:18) | batch 5 `00D35A6E` | batch 4 `2B56A1CD` (`x64-c29442e8-p58724e0`, server FC73E60E) | pass, all three | FAIL: tiny 14 of 15 tables from turn **43**, standard 14 of 15 from **50**, huge 13 of 14 from **355** | FAIL: tiny from 33, standard from 51, huge from 379 / 382 |
| B (04:18-04:46) | batch 4 `3737AE10` (`i0ship-e96457fd`) | batch 5 `78355A75` (this take's layout, server A6026D53) | pass, all three | FAIL: the same, 43 / 50 / 355 | FAIL: tiny from 33, standard from 51, huge from 381 / 381 |

The writer identity (part 1) holds across the batches; behaviour (part 2) differs exactly from the finder fix's first
turns of the direct plays (section 6 table, 4813's 43 / 50 / 355). **Part 3's continuations differ at their own turns,
and those are the fix's too [L for tiny and standard]**: each DLL continuing its own early save, batch 5 against batch 4
(`s18_cont_statscmp.sh`, `server/evidence/0551/statscmp/cont-*.txt`) differs first at tiny turn **33** and standard **51**
in both arms, the same turns as the crossed part 3 (a game continued from a save starts with an empty node cache, so
the stale reads, and with them the fix's first effect, fall on other turns than in the direct play). For huge the own
continuations start from T375 saves that already differ (after 355), so they differ from 375 and cannot separate the
cause; the crossed part 3's 379-382 (both sides continuing the same save) is then the fix's first effect after that load
[I]. No other difference: in the direct plays batch 5 = batch 4 + the finder fix row for row (above), so `x64-ifver`
contributes none. 0517's crossed runs against batch 3 showed the same pattern for A9 (part 3 first turns 57/63, 61/56,
376/378 against part 2's 57/50/354). Reports `server/evidence/0551/d10-crossed-{A,B}-report.txt`; run folders
`F:/c5-64bit-work/0551/d10/crossed-{A,B}`.

## 7. What 0551 leaves for the MC

- **Lay out the three layouts** from `F:/c5-64bit-work/0551/layouts/` under the pins folder (`%USERPROFILE%/c5-i0/dlls/`:
  `i0ship-5d18d3d6`, `i0ship-5d18d3d6-ia`, `x64-1fd51353-p1e09fc7`). The locks on the branch already name them: until
  then `provision.py pinned_dll` and run_d10's and run_pair's defaults find no build.
- **The locks** (`deps/i0.lock`, `deps/dll.lock`): commit, describe, branch, fingerprint, the re-pin paragraphs and the
  release keys moved (`s16_locks.py`); batch 4's kept as `release-b4`, `instance-b4`, `verified-b4`, `tools-b4`,
  `epoch-b4`. The 43-civ keys still describe batch 2. Layouts kept by `s15_keep_layouts.py`: every file equal to its
  source and to its `SHA256.txt` (x64 20 files, i0 4, i0-ia 5); the take's state, reports, logs and pack (manifest
  `D601D0CC...0514`) in `F:/c5-64bit-work/0551/take/`. Evidence in the repository: `server/evidence/0551/validated-report.md`,
  `take-state.json`, `hi_journey-report.md` (user paths as `%USERPROFILE%`).
- **Fork fast-forwards** (the MC's step, needs the owner's yes per the ticket): `i0-ship` and `master` e96457fd5 -> 5d18d3d6c,
  `x64` c29442e8c -> 1fd51353e (both fast-forwards: the batch-4 heads are their parents' ancestors).
- **From this pin every server built against the pinned source refuses an x64 DLL without
  `DllGetOutOfProcessInterfaceVersion`**, by design (4719). A test or tool that loads a **stand-in** x64 DLL into such a
  server needs the same export as hutest's fake DLL got here (section 5: `#if __has_include("CvOutOfProcessInterface.h")`
  -> export `VP_OUT_OF_PROCESS_INTERFACE_VERSION`). Only hutest was found [I]; a stand-in that fails with "interface
  version missing" in its server report is this case. The LOUD line of the transition ("accepted because this server was
  built against a DLL source without one") no longer appears on the pinned layout.
- **A pinned batch-4 x64 DLL does not run in a batch-5 server** (refused, "interface version missing"); a comparison arm
  with the batch-4 DLL uses the batch-4 layout `x64-c29442e8-p58724e0` whole (its server accepts both DLLs).
- **COMMON rule 18, proposed text** (the MC's file): see section 7.1.
- **Runs started on batch 4 finish on their pins and say so.** Epic 0504's package (4807) rebases onto batch 5 in
  0504/0562; the PGO profile (ticket 0513) is retrained on the new x64 pin.
- **README row** for this file in `research/03-dll-engine-boundary/README.md` (the MC's at the merge, MC ruling): "
  [upstream-fix-batch-5.md](upstream-fix-batch-5.md) | DLL pin batch 5 (ticket 0551, 2026-11-18/19): 4813's path finder
  fix (the not-refreshed node-cache read at CNIP and the unit finder's extra children; changes every game on purpose) on
  both pins and 4719's x64 interface version; branches i0-ship-batch5 5d18d3d6c / x64-batch5 1fd51353e, carry rows, the
  take (validated tier, D162 journeys), D10 references 30,569 / 359,261 / 685,488, the crossed save test against batch 4,
  the new whole-games endings; from this pin a versioned server refuses a stand-in x64 DLL without the export (hutest
  fixed) |".
- **Upstream draft** for the finder fix: `upstream-drafts/cnip-not-refreshed-neighbour-read.md` on epic 0504's branch;
  nothing sent (D95, D130).

### 7.1 COMMON rule 18, proposed text

"**Since 2026-11-19 (ticket 0551, DLL pin batch 5, D195): 32-bit `i0-ship-batch5` 5d18d3d6c (`i0ship-5d18d3d6`,
instance-aware `i0ship-5d18d3d6-ia`), x64 `x64-batch5` 1fd51353e with server.exe and proxy from repository 1e09fc7c2 in the
layout `x64-1fd51353-p1e09fc7` (DLL 78355A75..., server A6026D53..., proxy 5BAB1B6C...; `deps/dll.lock` `release`). The
batch carries ticket 4813's path finder fix (the node cache of a ring-2 destination's common neighbour and of the unit
finder's extra children refreshed before it is read) on both pins and, on x64 only, ticket 4719's interface version
(`DllGetOutOfProcessInterfaceVersion`). **The finder fix changes game results on purpose** (every D10 game: 30,569 /
359,261 / 685,488 rows, was 31,226 / 349,074 / 683,261; the four whole games end differently): never compare a result with
one taken on batch 4 or older. The interface version changes none (upstream-fix-batch-5.md section 6). **From this pin a
server built against the pinned source refuses an x64 DLL without the interface-version export**: a batch-4 or older x64
DLL runs only in its own pin's layout, and a stand-in x64 DLL loaded by such a server exports the number (hutest's fake
DLL does). The 43-civ keys still describe batch 2. Runs started on batch 4 finish on their pins and say so. Replaced:
`i0ship-e96457fd` (-ia `3eaa3290`), `x64-c29442e8-p58724e0`.**" (the batch-4 paragraph then becomes "Superseded: since
2026-11-18 04:35 EST (ticket 0517, ...)" as the earlier ones did).

## 8. Not verified

- The 43-civ variant of batch 5 (not built).
- The window games (M, H, E) were not replayed on the batch-5 layout; they rest on 4813's runs of the same x64 fix in
  the batch-4 layout and on the b5 = cnip identity of the D10 games (section 6) [I].
- No timing of batch 5 (4813's oldpc pair found no cost of the fix; the batch adds only the ifver export, which runs
  once per boot).
- Other stand-in x64 DLLs that a batch-5 server would refuse: only hutest's was found and fixed (section 5) [I: a grep,
  not a run of every suite against a batch-5 server].
- The proxy's popup and report route for a refused DLL in a real game (`dll-boot` -> D119; 4719 9.5: existing code, not
  run here either).
- The crash without a box after the stock replay of release-basic (pid 7672, 2026-11-19 03:17:07, c5h-c): 0483's known
  `-ia` exit crash at the game's own exit (the journey's EXIT + Yes), not 0576's server-side use after free: [L] by the
  same point and signature as 0517's three; not re-checked under cdb here (0483's cdb run is the [C]). Evidence
  `F:/c5-64bit-work/0551/crash-7672/`.
