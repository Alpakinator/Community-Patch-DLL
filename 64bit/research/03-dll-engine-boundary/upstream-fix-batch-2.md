# DLL fix batch 2: fixes waiting for the next pin change

This file collects DLL fixes made after DLL pin batch 2 (one-off 9045, 2026-11-13; `upstream-fix-batch-1.md` section 8)
that wait for the next pin change. Each fix is on its own fork branches. The pin moves only by the MC's pin procedure, and
low-severity fixes wait for the next pin change. Batch 1, pin batch 2 and the fix branches of bugs 0084, 0096, 0103,
0116 and 0132 are in `upstream-fix-batch-1.md`.

## 1. Bug 0421: `Plot:IsResourceConnectedByImprovement(-1)` stops the DLL (queue ticket 0421, 2026-11-16)

Tracker `tracker/issues/0421-dll-guard-resource-connected-by-improvement.md`. Found by 0392's first human-input finding
(4510, #2482): a 3 s hover over an unimproved plot (Tiny save `HI4510-TINY_0008`, plot 51,13) with EUI from DLC
`UI_bc1` and mods cp,vp **without** VP's EUI compatibility mod. Evidence of that finding:
`F:/c5-64bit-work/0392/4510/crash-082444`, `crash-084659`, `crash-085315-stock`. Ticket 0423 checks whether the fleet's
mod set (EUI without VP's compatibility files) is one a VP player has. The guard is right either way.

### 1.1 Cause [C, source at the pins i0 26234371e and x64 04b1211b8; `CvLuaPlot.cpp` is byte-identical in both and in Release-5.4.8 7307aefdf]

- EUI's `PlotHelpManager` calls `Plot:IsResourceConnectedByImprovement(-1)` for an unimproved plot.
- `CvLuaPlot::lIsResourceConnectedByImprovement` passes the Lua integer straight to `GC.getImprovementInfo()`.
- `CvGlobals::getImprovementInfo` (CvGlobals.cpp line 4279 at 04b1211b8; 4244 in the stock 5.4.8 DLL, as the stock
  box says) begins with `PRECONDITION(eImprovementNum > -1)` and `PRECONDITION(eImprovementNum < GC.getNumImprovementInfos())`.
  `PRECONDITION` (`CvGameCoreDLLPCH.h` line 83) calls `CvPreconditionFailedFormat`, declared `CV_PRECONDITION_NORETURN`
  (`CvAssert.h`). It is active in release builds and the game cannot go on. The `if` range check after the two
  preconditions, which would return NULL, is never reached.
- Seen by the player: on the stock 32-bit route, the DLL's fatal "Error" box (CvGlobals.cpp:4244). On the product route,
  the server's DLL stops and the game vanishes with no message (the missing report is ticket 0422).
- Upstream master has the same code: `1fb49429f` (2026-11-16 02:35 EST), `CvLuaPlot.cpp` lines 2185-2202, and the same
  `CvPlot`/`CvGame` paths for the other five methods of 1.2 [C].
- **Who calls it with -1** [C, grep of the Lua files, 2026-11-16]: EUI as shipped in the DLC folder `UI_bc1` calls it
  unguarded: `PlotHelp/PlotHelpManager.lua` line 624 (`plot:IsResourceConnectedByImprovement( revealedImprovementID )`,
  -1 on an unimproved plot with a revealed resource) and `CityBanners/CityBannerManager.lua` line 199
  (`plot:GetImprovementType()`). VP's "(3a) VP - EUI Compatibility Files" replaces both and checks first: its
  `LUA/PlotHelpManager.lua` line 695 `revealedImprovementID ~= -1 and plot:IsResourceConnectedByImprovement(...)`,
  `LUA/CityBannerManager.lua` line 193 and `ImprovedTopPanel/TopPanel.lua` lines 2177 and 2550 the same. So with VP's
  compatibility mod active the plot help does not reach the bug; without it (the fleet's `--mods cp,vp`, ticket 0423)
  it does. The base game's `UnitPanel.lua` (Expansion and Expansion2) passes a build's improvement, which is valid.

### 1.2 Every Plot Lua method that takes an improvement index [C, `CvLuaPlot.cpp` and `CvPlot.cpp`/`CvGame.cpp` at 04b1211b8]

"Out of range" means outside [0, `GC.getNumImprovementInfos()`).

| Lua method | Path for an out-of-range index | Reaches a PRECONDITION? | Fixed |
|---|---|---|---|
| `IsResourceConnectedByImprovement(i)` | `GC.getImprovementInfo(i)` in the Lua wrapper | yes, for -1 and every other out-of-range value | yes: false |
| `CanHaveImprovement(i, ...)` | `CvPlot::canHaveImprovement` -> `GC.getImprovementInfo(i)` first | yes, for -1 and every other | yes: false |
| `GetUpgradeTimeLeft(i, player)` | `CvPlot::getUpgradeTimeLeft` -> `CvGame::getImprovementUpgradeTime` -> `GC.getImprovementInfo(i)` | yes, for -1 and every other | yes: 0 |
| `CalculateImprovementYieldChange(i, ...)` | `CvPlot::calculateImprovementYield` returns 0 for -1 (NO_IMPROVEMENT), then `GC.getImprovementInfo(i)` | yes, for -2 and below and for >= count; -1 was already safe | yes: 0 |
| `GetNumResourcePostModifiers(player, i)` | `CvPlot::GetNumResourcePostModifiers` skips -1, otherwise `GC.getImprovementInfo(i)`; only on a plot with a resource | yes, for -2 and below and for >= count; -1 was already safe | yes: counted as NO_IMPROVEMENT |
| `SetImprovementType(i)` | `CvPlot::setImprovementType` returns for i < -1, then `GC.getImprovementInfo(i)` for i > -1 | yes, for >= count; -1 removes the improvement (valid) | yes: ignored, as `setImprovementType`'s own check intends; -1 unchanged |
| `HasImprovement(i)`, `IsAdjacentToImprovement(i)`, `IsWithinDistanceOfImprovement(i, d)` | compare `getImprovementType()` with i | no | no (unchanged) |

Not in the list: methods that return an improvement (`GetImprovementType`, `GetRevealedImprovementType`) and methods
that take a build, not an improvement. Arguments of other types in the same methods are not checked by this fix (for
example `CalculateImprovementYieldChange`'s player goes to `GET_PLAYER` unchecked) [I: the same class of fault, out of
the ticket's scope].

### 1.3 The fix

One helper in `CvLuaPlot.cpp`, `IsValidImprovementIndex(i)` (`i >= 0 && i < GC.getNumImprovementInfos()`), checked in
the six fixed wrappers before anything reaches `getImprovementInfo`. Behaviour for valid indices is unchanged. The DLL's
C++ callers are untouched.

- Fork branches (the project's DLL fork): **`i0-fix-0421`** = i0 pin 26234371e + **ae0efb7c6** ("Lua plot
  methods: do not pass an out-of-range improvement to getImprovementInfo (tracker 0421)"); **`x64-fix-0421`** = x64 pin
  04b1211b8 + a `--no-ff` merge of it, head **b2c8ed982** (clean; `git diff 04b1211b8 b2c8ed982` equals
  `git diff 26234371e ae0efb7c6`, the same MD5 of the patch text). Neither the fork's master, `i0-ship`, `x64` nor the
  batch or upstream-tracking branches were touched. Fork clone for this work: `C:/c5-64bit-work/0421/fork` (a local clone
  of the owner's fork clone, origin set to GitHub).
- Builds (2026-11-16):
  - x64, fast configuration (ticket 0159; functional work only, never timing):
    `python -I tools/x64-dll/build.py --src C:/c5-64bit-work/0421/fork --config fast --out C:/c5-64bit-work/0421/x64fast
    --stubs C:/c5-64bit-work/0421/stubs --unlocked`, the stubs copied from a release `build_all` of 04b1211b8 (one-off
    9045's, via `C:/c5-64bit-work/0418/deps-release/stubs`). 09:24-09:28 EST, 0 errors, 0 unresolved, link 1.8 s. DLL
    SHA-256 `97af9354dff3ce7744be1cdfb331500c496a118cd23d458618470c4521fed782` (23,450,624 B). Fix layout
    `C:/c5-64bit-work/0421/x64-b2c8ed98-pfdee656-fast/bin` = the pin layout `x64-04b1211b-pfdee656/bin` with only the
    DLL and its PDB replaced (`SHA256.txt` beside it).
  - 32-bit instance-aware (`-ia`, for the test instances), by the release driver's route (`tools/release/stages/build.py`
    `prepare_ia` + `build_i0`): `git archive` of i0-fix-0421, `git apply` of the pin's
    `ia-ee893bc18-440878983.patch` (clean), version stub `Release-5.4.8-40-gae0efb7c63+ia440878983 Copy`,
    `build_vp_clang.py --config release` (clang-cl, LTO) under `quietlock.py run`, 09:29-09:38 EST (compile 98 s, link
    446 s). DLL SHA-256 `bcfa50d6ba6763565e265bf3534e810c5e19061c3c25513348ffb9ed68663901` (17,475,584 B, the pin
    `-ia`'s size), kept in `C:/c5-64bit-work/0421/i0ia-fix/`. Script `C:/c5-64bit-work/0421/s06_i0ia.sh`. The plain
    (not `-ia`) 32-bit DLL was not built.
- Upstream master: the fix commit applies to `1fb49429f` without conflict [C: `git merge-tree --write-tree --merge-base
  ae0efb7c6~1 1fb49429f ae0efb7c6` exit 0, tree a4fbe48b0]. No upstream branch was made.

### 1.4 Live check: the Lua-level test on both routes

Method. Instance c5h-b (C:, Goldberg copy, claimed by impl-B-0421), save `C5H-0421-TINY_0008` (a copy of the Tiny save
`b2ceb663...` that 0392's `HI4510-TINY_0008` was copied from; turn 8), `frontend.py prepare --mods cp,vp --load
C5H-0421-TINY_0008`, `cli.py launch` (cloaked, frame cap 30), `frontend.ensure_in_game`, then one Lua chunk through the
Lua channel (state Main): `C:/c5-64bit-work/0421/guard_test.lua`, checked by `guard_test.py` (exit non-zero on any
failed check or Lua error). Plot 51,13 (0392's hover plot): no improvement, resource 32, 48 improvements in the
database. Every method of 1.2 is called with -1, -2, 48 and 1048, and the six guarded ones also with every valid index
0-47 (a digest per method, to compare builds and routes). Scripts: `C:/c5-64bit-work/0421/arm.sh`, `ingame.py`,
`guard_test.py`, `guard_test.lua`; outputs `C:/c5-64bit-work/0421/live/<arm>/guard.json`.

| Arm | Route and DLL | Result |
|---|---|---|
| stock-fix | 32-bit, `-ia` of i0-fix-0421 (`bcfa50d6...`), 2026-11-16 09:39-09:42 EST | **PASS** [L]: every out-of-range call answered, no box: `IsResourceConnectedByImprovement` false, `CanHaveImprovement` false, `GetUpgradeTimeLeft` 0, `CalculateImprovementYieldChange` 0, `GetNumResourcePostModifiers` 1 (= its value for -1), `SetImprovementType(48)` and `(-2)` left the plot unimproved; `HasImprovement`/`IsAdjacentToImprovement`/`IsWithinDistanceOfImprovement` true for -1 (the plot and its neighbours are unimproved), false otherwise. Valid digest: conn 13, can 2, upg 48, yld 24, post 49. Diagnostic shot in the Visual Checks feed (issue 0421). |
| stock-pin-neg (negative control) | 32-bit, the pin's `-ia` `i0ship-26234371-ia` (`3c2f47da...`), 09:43-09:46 EST | **teeth shown** [L]: the valid-index request first gave the **same digest** as the fixed build (conn 13, can 2, upg 48, yld 24, post 49, -1 value 1), so the fix leaves valid indices unchanged on the 32-bit DLL; then `IsResourceConnectedByImprovement(-1)` got no answer within 30 s and the dialog watcher saw the DLL's fatal "Error" box, "Expression: eImprovementNum > -1 ... CvGlobals.cpp Line: 4244" (signature `CvGlobals.cpp:4244 eImprovementNum > -1`, the same signature as 0392's hover on c5h-c and 0423's on c5h-d). Closed by the claim owner, `cli.py dismiss --button OK --fatal`, then `stop --after-fatal` (D155); crash evidence `%USERPROFILE%/c5-instances/c5h-b/run/crash/20261116_094537_pid68660` and `..._094619_pid68660`. |
| proxy-fix | proxy route: `provision.py set-proxy c5h-b --bin C:/c5-64bit-work/0421/x64-b2c8ed98-pfdee656-fast/bin --proxy <pin layout>/proxy/bin/CvGameCore_Expansion2.dll --pdb` (32-bit EXE + the pin's proxy `75c40cec...`, server.exe `5b254430...` with the fix DLL `97af9354...` in `VP64\`; `mode=on`, no composite), 09:48-09:50 EST | **PASS** [L]: the same answers as stock-fix for every out-of-range call, the **same valid-index digest** (conn 13, can 2, upg 48, yld 24, post 49), no box, server alive; `turn=9` against stock's 8 (the proxy-route load reports the next turn; not looked into). The Lua Plot methods run in the server's DLL on this route: 0392's unfixed product run died in `rt:LuaCall` with `PeerDied` on this call (`F:/c5-64bit-work/0392/4510/crash-084659/vpproxy.log` lines 386-390), so the product-route negative control was not repeated here. Run outputs `<c5h-b user>/Logs/VP64/run/20261116-094837-59860-1` (kept by `keep_runs`). Diagnostic shot in the feed. Afterwards `set-proxy --original`, DLL back to `3c2f47da...`, `provision.py verify c5h-b` ok. |

**Found on the way: the fast configuration imports one more database member** [C, 2026-11-16]. The fast x64 DLL
(`--config fast`, no LTO) imports 46 members of `CvGameDatabase_x64.dll`; the release pin DLL imports 45. The extra one is
`?Bind@Results@Database@@QEAA_NHM@Z` (`Results::Bind(int, float)`), which LTO inlines or drops in release. The server's
write recording does not cover it, so server.log says `database: LOUD: the DLL imports ?Bind@Results@Database@@QEAA_NHM@Z,
which the write recording does not cover` and `FAILED: database: the DLL imports database members the write recording
does not cover (see the report)` at boot (`llvm-readobj --coff-imports`, `C:/c5-64bit-work/0421/s14_imports.sh`). On this
non-pack instance the game booted and played on. Not checked: whether a `pack=1` layout refuses to boot with it, and
whether a float bind through that path would be missing from the write replay. This concerns every fast-config server
layout (ticket 0159), not this fix.

**Fixed by ticket 0752 (2026-11-25): the write recording hooks `Results::Bind(int, float)`** [C]. Where the import comes
from: `SqliteLogger::Statement::bind(float)` (`CvGameCoreDLL_Expansion2/SqliteLogger.cpp` at pin 1b87dd185) calls
`m_pkResults->Bind(idx, fValue)`; no call of `Statement::bind(float)` survives release's LTO link, which drops it and
its import [I: from the import's absence], while debug and fast keep both (the mangled name is in the debug DLL of a `build_server.py --config debug`
build of the pin and not in `pins-0.1b/deps-x64-release/dll/CvGameCore_Expansion2.dll`, checked by string search
2026-11-25). Every debug and fast server in remote mode therefore ended `FAILED: database` at boot: exit code 2 at an
orderly shutdown, exit code 4 at once with `fail_on_unexpected` (strict). That is all four failures of
`test_proxy_rt.py` on a debug build (d28's "exited with code 0", the three strict cases), which ticket 0752 first took for
a regression on main (231/235 on ee9695f16 with build_server's default `--config debug`; release 235/235; no commit in
a9ed984c6..ee9695f16 changed product code). `server/src/remote/srv_remote_db.cpp` now hooks
`?Bind@Results@Database@@QEAA_NHM@Z` (`H_BindF`) and records the bind as a double (type 2), replayed by the proxy through
`Bind(int, double)`: the database library binds a float as `sqlite3_bind_double` of the value widened to double
(`tools/x64-dll/cvgamedatabase/src/Database.cpp` `Results::Bind(int, float)` [C]; the EXE's 32-bit library is assumed to
do the same [I]), and the widening is exact, so the engine's row gets the same value. The statistics logger binds on its
own `stats.db` connection, so these binds are not tracked in practice (only statements on the game connection are);
the hook closes the boot refusal and any future float bind on the game connection. Runs: section on 0752 in
`tracker/issues/0752-main-red-proxy-rt-d28-strict.md`.

Not verified: a release (LTO) x64 build of x64-fix-0421 (only fast); the plain (not `-ia`) 32-bit DLL; the original
symptom (a 3 s hover with EUI from `UI_bc1`, without VP's compatibility mod) on the fixed builds, as opposed to the
direct Lua call; master. The pin is unchanged: the MC moves it with the next pin change (low severity waits).
Evidence copied into the repository: `evidence/0421/` (the test scripts and each arm's `guard.json`).
*Kept (2026-11-16 10:00 EST): the scratch folder `C:/c5-64bit-work/0421` (fork clone, sources, scripts) was deleted at
hand-back; the branches are on GitHub, and the two fix builds named above are kept in `F:/c5-64bit-work/0421/i0ia-fix/`
and `F:/c5-64bit-work/0421/x64-b2c8ed98-pfdee656-fast/` (same hashes).*

### 1.5 Upstream report draft (D95-scrubbed; for after the beta, D130; NOT sent)

Nothing below has been sent. It names no internal ids, decisions, project names or cherry-pick lines. If the owner wants it
sent, it goes to `LoneGazebo/Community-Patch-DLL` as an issue, with the pull request text below if a fork branch on
master is wanted (the commit applies to master `1fb49429f` as is).

**Issue title:** Plot:IsResourceConnectedByImprovement(-1) stops the game (getImprovementInfo precondition)

**Issue text:**

> `Plot:IsResourceConnectedByImprovement(improvementID)` passes its argument straight to
> `GC.getImprovementInfo()`. With `-1` (NO_IMPROVEMENT) or any other value outside the improvement table, the
> `PRECONDITION(eImprovementNum > -1)` at the top of `CvGlobals::getImprovementInfo` fires. Preconditions are active in
> release builds and do not return, so the game stops with the DLL's error box ("CvGlobals.cpp", the precondition line).
>
> How it is reached: EUI's original plot help (`UI_bc1/PlotHelp/PlotHelpManager.lua`) calls
> `plot:IsResourceConnectedByImprovement( revealedImprovementID )` for every plot with a visible resource, and
> `revealedImprovementID` is -1 on an unimproved plot. Hovering over an unimproved resource for a moment, with EUI but
> without the VP EUI compatibility files, stops the game. The compatibility files check for -1 first, so the usual VP
> setup does not hit it; any other mod or UI that passes -1 does.
>
> Reproduce from the Lua console in any game: `Map.GetPlot(x, y):IsResourceConnectedByImprovement(-1)`.
>
> The same holds for other Plot methods that take an improvement index: `CanHaveImprovement` and `GetUpgradeTimeLeft`
> (-1 and any out-of-range value), and `CalculateImprovementYieldChange`, `GetNumResourcePostModifiers` and
> `SetImprovementType` (out-of-range values other than -1).

**Pull request title:** Check the improvement index in the Plot Lua methods

**Pull request text:**

> Plot Lua methods that take an improvement index now check it against the improvement table before anything calls
> `GC.getImprovementInfo()`, whose precondition stops the game for an out-of-range index.
>
> - `IsResourceConnectedByImprovement`, `CanHaveImprovement`: false
> - `GetUpgradeTimeLeft`, `CalculateImprovementYieldChange`: 0
> - `GetNumResourcePostModifiers`: an out-of-range improvement counts as none
> - `SetImprovementType`: -1 still removes the improvement; other out-of-range values are ignored, as
>   `CvPlot::setImprovementType` already intends
>
> Valid indices behave as before. `HasImprovement`, `IsAdjacentToImprovement` and `IsWithinDistanceOfImprovement` only
> compare and are unchanged. Tested: each method called from Lua with -1, -2, the table size and a larger value on an
> unimproved plot (no stop, the values above), and every valid index gives the same results as before.

The last sentence of the pull request text rests on section 1.4; on master itself nothing was built or run.

## 2. Upstream crash-fix survey (one-off 9075, 2026-11-16)

The owner, 2026-11-16 20:33 EST [reworded for privacy]: asked for a review of upstream pull requests such as
https://github.com/LoneGazebo/Community-Patch-DLL/pull/13406 that carry potential crash fixes; where a fix addresses a
root cause the project does not already fix, either take that fix over or re-implement something similar. Tracker `tracker/issues/9075-upstream-crash-fix-survey.md`.

### 2.1 Method [C]

- Every pull request of `LoneGazebo/Community-Patch-DLL` merged since Release-5.4.8 (`gh pr list --search
  "merged:>=2026-10-16"`, 103) and every open one (35), read 2026-11-16 20:40 EST; upstream master at 1fb49429f. Plus
  the 162 non-merge commits of `Release-5.4.8..upstream/master`, filtered by subject for crash, freeze, hang, leak,
  corrupt, overflow, uninitialised, dangling, deleted, stale, bounds, lock, loop, divide, assert (commits pushed without
  a pull request included). Candidates were read in full; each was checked at the pins (i0 26234371e, x64 04b1211b8)
  with `git grep`/`git show` and against `upstream-fix-batch-1.md` (sections 2-3), `tools/release/series/carry.tsv` and
  the fork's fix branches.
- Fork clone for this work: `C:/c5-64bit-work/9075/fork` (local clone of the owner's fork clone, origin = GitHub fork,
  upstream = LoneGazebo, pull request heads fetched as `refs/pr/<n>`; the whole folder `C:/c5-64bit-work/9075` was
  deleted at hand-back, 2026-11-16 23:30 EST, after every fork branch was checked equal on GitHub). Nothing was sent
  upstream (D95, D130). Kept on F: (`F:/c5-64bit-work/9075/`): the two 13397 `-ia` builds given to 9074 and the test save
  `saves/C5H-9075-HUGE_T428.Civ5Save` (`ef3271dd...a4ba`).

### 2.2 Verdicts

"Pins" = i0 26234371e and x64 04b1211b8. "Not applicable" = the pins do not have the code or the failure path.

| Upstream | State | What it fixes (root cause) | Our pins | Verdict |
|---|---|---|---|---|
| #13397 (6d0c0f6d3, vaderkos) | open | 4 MB LocalMachine event stream overrun on a whole-map plot layout (load, active-player change): no writer checks capacity; corrupts memory after the buffer (crash, dispatcher spin, lost farm fields) | not fixed in what ships: the EngineQueueGuard (drop what does not fit) is only in the test instruments patch (`-ia`); the shipped 32-bit DLL and the product's proxy have no guard [C grep] | **re-implement, not ready** (2026-11-16 21:40, MC accepted): the re-implementation `i0-fix-13397` loses map art in live runs [L] (section 2.3a; the T1100 crashes are not shown to be its doing, corrected 22:25); not carried; the guard stays the only protection |
| #13406 (6b3d305cf, vaderkos) | open | 1 MB UI draw-list data buffer and 4096-command array, unchecked; ~450 unit flags in view overflow them (crash) [`02-exe-internals/message-queue-and-fixed-buffers.md` section 2] | not fixed anywhere | **re-implement, as its own ticket** (section 2.7): the fix runs in the EXE process on the UI thread, calls EXE allocators and needs a Lua trigger; for the product it belongs in the proxy |
| #13566 (745adf954) | merged | `homelandMoveNames` has no entry for `AI_HOMELAND_MOVE_OPPORTUNITY_ATTACK`: `Unit:GetMissionInfo`, the homeland-move log line and VPDEBUG stats read one past the end | has the bug [C: enum at CvEnums.h:2676, array ends at H_MOVE_SECONDARY_SETTLER, CvHomelandAI.cpp:7026] | **take** (`i0-fix-13566`), section 2.4 |
| #13482 (65936969b, ruskyandrei) | open | owned plot without an owning city: `setImprovementType`, `AcquireWaywardPlots`, `DoAlterWorkingPlot`, builder AI dereference the missing city (crash; reached from Lua `Plot:SetOwner(p, -1)`, `SetCityPurchaseID(-1)`, or a NewOwner improvement for a builder with no cities) | has the bug [C: cherry-picks onto the pin without conflict] | **take** (`i0-fix-13482`), section 2.4; low severity (mods or rare paths) |
| #13395 (6949d5787, ruskyandrei) | open | Lua hooks raised inside `acquireCity`/`CvCity::kill` release the game core lock while the city is half-built or deleted; a UI script reads freed city data (crash in `CvCity::GetStaticYield`, timing-dependent, long autoplay games) | has the bug [C: cherry-picks without conflict] | **take** (`i0-fix-13395`), section 2.4 |
| #13423 (34210e4d1) | merged | pathfinder nodes freed without destructors: each node's child vector leaks on every map init and load | **already covered** by our a4678f41b (children in one list per pathfinder; `CvAStarNode` keeps only `m_iNumChildren`) [C] | already covered |
| #13569 (163febdd7) | merged | counterspy crash in `LogSpyStatus` | already covered by our 040b063ea (tracker 0080) | already covered |
| #13573, #13574 | merged | our 0096 and 0103, sent upstream | in the pins | already covered |
| #13555 (88400faf4) | merged | `CvLeague` copy keeps Lua pointer links to a dead stack object | code after 5.4.8 (batch 1 section 3) | not applicable |
| #13488 (9b3da47bb) | merged | stale area-effect cache index | code after 5.4.8 (batch 1 section 3) | not applicable |
| #13565 (2d816b046) | merged | uninitialised `m_ppiGreatPersonExpendedYieldEraScaling` | member came with ed6a969f4, after 5.4.8 [C: 0 hits at the pin] | not applicable |
| #13570 (ba8aafdca) | merged | DLL `__gc` finalizers run after the DLL is unloaded (crash at quit, or at the main menu after a mod switch) | at the pins only fractals carry a DLL `__gc` (CvLuaFractal.cpp:133), made by map scripts whose Lua state ends with map generation; the game-object finalizers that cause the crash came with #13489 [C; I that no fractal outlives the DLL] | not applicable |
| #13539 (aa7a29b38) | merged | `Events.ClearHexHighlights` on maps wider than 181 | mod Lua only (batch 1 section 3) | not applicable (mod files) |
| #13463 (475bd4997) | merged | crash when loading a game from within a game | mod Lua only (DiploCorner, NotificationPanel, Squads) [C] | not applicable (mod files) |
| #13467 (14d16fa3d) | merged | Lua queries left on their row lock the game database; a rebuild then misses mod tables (the game stops) | mod Lua only [C] | not applicable (mod files); see 2.8 |
| #13562 (a946ceb2a) | merged | clang Release DLL crashed on mod load: `build_vp_clang_sdk.py` put VC9/SDK headers ahead of clang's `vadefs.h` | the CI script only; our builds use `build_vp_clang.py` and load mods [L, every pin run] | not applicable |
| #13392 (7e3f57576) | open | election-rigging failure without a spy trips an ASSERT | ASSERT is active in release (`VPRELEASE_ERRORMSG`) but the path needs `EVENTS_ESPIONAGE`, 0 in CP 5.4.8 and not set by VP [C] | not applicable |
| #13556 (77552d429) | merged | operator precedence (a NULL `pPlayer` path no caller takes), two missing `break`s (latent with the default data), constructor init order | no reachable crash | not a crash |
| #13582 | open | clang-tidy cleanup, "no intended change" | - | not a crash |
| #13522 | open | shares one read-only zero block among empty 2D database tables: ~41 MB of 32-bit address space with VP alone, ~389 MB with 25 mods | - | not a crash fix as such; address-space relief for the 32-bit DLL, see 2.8 |
| #13531 (50642e29a) | merged | EXE's 256-entry trade-route visual table filled by other players' routes; new routes not drawn | the allocator returns NULL and the route is dropped, no crash [C, from the message] | not a crash |
| #13464 (ac3b46ec2), #13476, #13577, #13579-#13581, #13583, #13448 | merged | observer-mode exit, MP desync, gameplay/UI fixes, crash-report wording | - | not a crash |
| #13434, #13435 | merged 2026-10-16 | crash on map init, game freeze | ancestors of Release-5.4.8 [C] | already in the base |

### 2.3 #13397 re-implemented: map layout in budgeted batches (i0-fix-13397) - NOT READY, see 2.3a

**Why not a take [C].** #13397 is one commit on upstream master 8caa2d5a1 and needs #13379 (6c80fa023, merged
2026-11-11, not in our pins), the Exe/ address-table framework. Taking #13379 is not possible as is: it removes
`Game.SetExeWantForceResyncValue` and `Game.IsExeWantForceResyncAvailable`, which the 5.4.8 `DiploCorner.lua` (CP and
EUI compatibility) calls from `GameEvents.PlayerDoTurn` every turn (lines 515-526), and the cherry-pick conflicts in
`Lua/CvLuaSupport.cpp`, which #13489 rewrote. #13379's table also carries EXE code-byte signatures, which D83 does not
allow us to commit.

**What the re-implementation does** (fork commit 28ca892ee, all in `CvMap.cpp` plus five call-site lines; GPL, with the
origin named in the code):
- The batching of #13397 unchanged: plot layout in batches of 256 KB of estimated stream bytes (farm 8,704 B with its
  stencils, route 2,048, other improvement 256, resource 128), whether the EXE's per-plot load pass
  (`CvDllPlot::UpdateLayout`) or `CvMap::updateLayout` runs it; the next batch waits for one buffer swap; plots past the
  budget stay layout-dirty and `CvGame::update` re-arms `PlotData_DIRTY_BIT` while any remain; `CvDllMap::UpdateLayout`
  is held until the last batch is dispatched.
- Its own EXE reader instead of #13379: the build by PE time stamp (DX11 0x546CD0A8, DX9 0x546CCB59, Tablet
  0x546CD5F8), then the current channel (DX11 VA 0x1A0E400) and the container's swap counter (containers 0x1A0F080 +
  channel x 0x800180, counter at +0x800100). Before use it checks that the EXE's own instructions name exactly these
  values: the relocated operands at the record reserver (DX11 VA 0x870760, +0x01 channel, +0x07 0x800180, +0x0D
  containers, +0x13 0x800100, +0x1C 0x400080, +0x31 0x80) and at the debug-info reader (VA 0x6A60E9, +0x02 channel, +0x08
  0x800180, +0x0E containers + 0x800104); the DX9 and Tablet values are #13397's. Only reads (VirtualQuery-checked); no
  EXE byte is committed. One line in `CustomMods.log` says what it found. Gated on `MOD_BIN_HOOKS` (default 1), as
  upstream.
- Left out of #13397: the load-screen hold with "Preparing map: N%" and the chat warnings at 75 % and on overflow
  (they need new mod Lua and text). Deferred plots are laid out in the next frames instead.
- In the 64-bit DLL (`_WIN64`) the reader is compiled out and layout runs as before: the server has no EXE in its
  process.
- Game state: unchanged by design; `m_bPlotLayoutDirty` is not saved (CvPlot.cpp, no serialization entry) [C].
- `-ia` build: the instruments patch `ia-ee893bc18-440878983.patch` conflicts with the commit's two lines at the top of
  `CvGame::update`; `C:/c5-64bit-work/9075/s06_ia_resolve.py` applies the patch to the pin first, then the fix without
  `CvGame.cpp`, then inserts `UpdateDeferredLayout();` as the first statement of `CvGame::update` (copied to
  `evidence/9075/`).

### 2.3a Live results of the re-implementation: art lost; crashes not attributed (2026-11-16)

Stock 32-bit route, `-ia` builds, all pictures diagnostic (not acceptance, D112).

- **Reader [L]**: on the DX11 EXE `CustomMods.log` says "Map layout batching: DX11 EXE, event stream found"
  (c5h-b 21:19 EST; 9074 on c5h-i 21:16 EST).
- **c5h-b, `C5H-9075-HUGE_T428`** (a copy of the test save `9043-HUGE-T428`, SHA-256 `ef3271dd...a4ba`, cp+vp, turn 429
  after the load), `evidence/9075/s09_live.sh` / `s14_live_long.sh`, outputs `C:/c5-64bit-work/9075/live/<arm>/`:
  - pin `-ia` (3c2f47da) 21:11-21:16: in game after 165 s; queue guard: no drop (this save does not overflow the queue on
    the pin) [L].
  - fix `-ia` (62b566f2) 21:17-21:22: in game after 123 s (one sample each, no timing claim); no drop [L]. **A railway
    loop on the eastern peninsula that the pin draws is missing** in the picture 60 s after the load [L].
  - debug build (fix + `CUSTOMLOG` lines, `s13_debug.py`, not committed) 21:33-21:39: the load pass deferred about 1,500
    plots, all at swap 9; in game `CvMap::updateLayout` then laid them out in 21 batches (20 of 262,144 B estimated, one of
    115,328 B) between swaps 69 and 147, then found nothing left [L]. **The loop is still missing 240 s after the load**
    [L]. So the DLL sends every deferred plot's state (`GameplayPlotStateChange`), and the EXE still does not draw it.
- **9074, Marathon T1100** (`F:/c5-64bit-work/9074/stock/`, 43-civ build `b596610b...`): load 228 s against 384-426 s
  with the guard build; no drop where the guard build dropped about 3.2 MB [L]. No dark farm rows around Rome in game,
  where the guard build shows them (`f13397-shots/run1-2125.png` against `t1100-run2/shots/turn-1100.png`) [L].
  **Run 1 crashed** 11 minutes after the load, still in turn 1100: a write at 0xAB2B3000 (page boundary), EXE VA
  0x55D9E2 (live 0x00BCD9E2, EXE at 0x00A70000), stack (RVAs) 0x2E282E, 0x39D074, 0x466919 (listener trampoline),
  0x2A59A5 (the 4 MB queue's dispatcher), 0x59D8A (its pump), 0xB406C (frame); free 316 MB, largest block 233 MB
  (below 2 GB: 41 MB, largest 8 MB); evidence `F:/c5-64bit-work/9074/stock/t1100-f13397-run1/` (minidump, crashes.log,
  CustomMods.log). None of the guard build's three T1100 runs failed there [L, n = 1 for the fix].
- **Reading [I]**: without upstream's load-screen hold, the re-implementation moves the bulk of the farm and route
  layout from the EXE's load pass into in-game frames. The EXE's in-game decal path draws a different result (art
  lost) and, in one run, wrote past a buffer under the queue's dispatcher. Whether upstream's full form (layout during
  the held load screen, `ContinueDeferredLayout` from `LoadScreen.lua`) avoids both is not known: upstream reports no
  missing farms on its dense save, but that was not checked here.
- **Separation test [L, 21:50-21:59 EST]**: debug build 2 (`s13_debug.py` plus `bBatch = false`: the load pass still
  defers, then the first in-game `CvMap::updateLayout` call lays out every deferred plot in one pass). **The loop is
  still missing at 240 s.** So splitting neighbouring plots into batches is not the cause: plots first laid out after
  the EXE's load pass do not get the art the load pass gives them (at least this route loop). Upstream's
  `ContinueDeferredLayout` lays out through the same `CvMap::updateLayout`, only earlier (while the load screen is
  held), so it may lose the same art; not tested [I].
- **Pin repeat [L, 22:00-22:05 EST]**: a second pin `-ia` arm draws the loop again. Tally on this save and camera: the
  loop is drawn in 2 of 2 pin arms and in 0 of 3 fix arms (fix, debug 1, debug 2; 60 s and 240 s pictures).
- **9074 run 2 [L]**: also crashed in the first autoplay turn of T1100, about 12 minutes after a 246 s load, at another
  place: a read at 0x0FB36020, EXE VA 0x7A3F0A, under the Lua-visible "B" event manager's dispatcher (RVAs 0xA8A19,
  0x3490D0, 0x466919, 0x23ABD4, 0x385BA7, 0xB402E); free 292 MB, largest block 196 MB (below 2 GB: 42 MB, largest 8 MB);
  minidump `F:/c5-64bit-work/9074/stock/side-crash/20261116_215549_pid41176/`. Tally on T1100 (all 43-civ `-ia`): guard
  build 3 loads = one hang in turn 1, one clean, one crash in the load (1,087 MB free); fix build 2 loads = 2 crashes in
  turn 1 (316 and 292 MB free).
- **9074 run 3 [L]**: crashed in the first autoplay turn too (load 229 s, crash 22:12 EST, 0 records dropped): a read at
  0x2B8A64F8, EXE VA 0x5B1FB7, inside the record-list loop VA 0x5B1F80-0x5B202A where the guard build's run 1 hung,
  reading a record outside the pool; under the "B" manager's dispatcher; free 314 MB, largest block 241 MB. Evidence
  `F:/c5-64bit-work/9074/stock/t1100-f13397-run3/`, `side-crash/20261116_221208_pid31676/`. **Fix build 3 of 3 loads
  crashed in turn 1100** (VA 0x55D9E2, 0x7A3F0A, 0x5B1FB7, no drop); guard build 2 of 3 failed (hang, crash in the load;
  3-7 MB dropped). 9074's conclusion (`research/08-performance/marathon-stress-t2000.md` 4.1, 9074 branch acd7aeb35): the
  T1100 failures are not caused by the queue overflow (the broken list shows up with no overflow), and the batching
  made the first turn worse.
- **Correction (MC, 2026-11-16 22:25; replaces "the batching made the first turn worse" as a cause)**: at 22:18:21
  9074's resumed probe run on c5h-i, with the **guard build without #13397** ("Release-5.4.8-39-g26234371e+ia440878983+m43
  Copy", 9,210 records / 1,585 KB dropped), crashed at the **same site as fix run 1**: a write to 0x92C87000 at
  `CivilizationV_DX11.exe+0x15CDE2` (file offset; VA 0x55D9E2), with 1,068 MB free in total (largest block 1,024 MB)
  but **34 MB free below 2 GB** (largest 5.5 MB); minidump
  `CvMiniDump_20261116_221821_5.4.8-39-g26234371e+ia440878983+m43_Release.dmp`, evidence
  `F:/c5-instances/c5h-i/run/crash/20261116_221825_pid5608`. So the crash at VA 0x55D9E2 is not the re-implementation's,
  and **the T1100 crashes are not shown to be caused by it** [L]. Low address space does not separate them either
  (corrected 2026-11-16 after 9074's probes, `research/08-performance/marathon-stress-t2000.md` 4.1, commit
  3bd0e36c2): the clean probes T050-T1100 had as little or less free below 2 GB (29.6-34.8 MB, largest block 0.5-1.9 MB)
  as the failing runs (34-42 MB, 5.5-8 MB), so it is constant on this map [L]. The fix runs' lower total free memory (292-316 MB against about 1 GB) is a difference not
  explained here (one reading: the EXE draws the farm fields the guard drops; not tested) [I].
- **Verdict (MC accepted 2026-11-16 21:40, kept 22:25 on the art loss alone)**: re-implement, not ready; `i0-fix-13397` / `x64-fix-13397` stay on the fork
  as a record, out of the carry. The EngineQueueGuard (drop what does not fit) remains the only protection, in the test
  builds only.

**The product is not covered [I, grep 2026-11-16].** The product's EXE still runs LandmarkSystem (D33 leaves it on),
so its farm-field and route records go through the same 4 MB queue; `proxy/` has no queue guard (no
`InterlockedExchangeAdd` slot, no queue address). The server's DLL cannot read the EXE's queue. A product fix belongs in
the proxy (an EngineQueueGuard-style import-slot guard, or batching that gets the EXE's swap counter over IPC): proposed
as a follow-up in 2.8.

### 2.4 Takes: #13566, #13482, #13395

Cherry-picks `-x` of the upstream commits onto the i0 pin, no conflict, no hand edit; x64 branch = x64 pin + `--no-ff`
merge of the i0 branch, same patch-id as the i0 diff.

| Fix | i0 branch (head) | x64 branch (head) | Notes |
|---|---|---|---|
| #13397 re-implementation (not ready, 2.3a; record only) | `i0-fix-13397` 28ca892ee | `x64-fix-13397` 45a95bac5 | patch-id c87c7650 both |
| #13566 | `i0-fix-13566` dc2779953 | `x64-fix-13566` 67899a65c | one string; touches only names (logging, `GetMissionInfo`) |
| #13482 | `i0-fix-13482` 54e8e5b8e (2 commits) | `x64-fix-13482` c8186c299 | branches only reached with a city-less owned plot; upstream: fingerprints identical on 151 Tiny and 201 Standard turns |
| #13395 | `i0-fix-13395` b2da2eaaf | `x64-fix-13395` e84e39feb | see below |
| all four (superseded) | `i0-fixes-9075` 8c7b0dc4e | `x64-fixes-9075` e6066ad1a | patch-id 3d680e9b both; made before 2.3a, kept unbuilt (no history rewrite of a pushed branch) |
| the three takes (test only) | `i0-takes-9075` c125463b1 | `x64-takes-9075` cd12e699a | patch-id 75eb974a both; for the builds and the parity runs (`s08b_takes.sh`) |

#13395 on the product route [C, `server/src/srv_lua.cpp` lines 360-400, `server/src/remote/srv_remote_lua.cpp`]: the
deferral reads each hook's arguments back through `ICvEngineScriptSystemArgs1::Count/GetType/GetInt/GetFloat/GetBool/
GetString`. The server's local args object implements them 0-based, and the remote route forwards them to the EXE's
object over IPC, so the deferral works in the server; on the remote route each deferred hook costs one IPC call per
argument (about 5 hooks per capture, upstream's count). Its index guess (`GetType(0) == ARGTYPE_NONE` means 1-based) is
upstream's and was not checked against the EXE [I].

### 2.5 Builds (2026-11-16)

Script `evidence/9075/s05_build.sh` (32-bit: `git archive` + version stub + `build_vp_clang.py --config release`
under `quietlock.py run`; x64: `tools/x64-dll/build.py --config fast --unlocked`, stubs copied from
`C:/c5-64bit-work/0442/deps-release/stubs`). Pitfall met on the way: a `C:/...` folder put into a POSIX `PATH` under
Git Bash is split at its colon and `build_vp_clang.py` dies at `update_commit_id.bat` with `FileNotFoundError`; use
`cygpath -u` (now in the script).

| Build | Source | Time (EST) | DLL SHA-256 | Size | Kept |
|---|---|---|---|---|---|
| 32-bit `-ia` | i0-fix-13397 + ia patch (`s06_ia_resolve.py`), "Release-5.4.8-40-g28ca892ee+ia440878983 Copy" | 20:48-20:54 | `62b566f2c2dd3974a7f51a42b4ee708289d4a69fcffd4ad3f3ce70e568c62eef` | 17,480,704 | `F:/c5-64bit-work/9075/i0ia-13397/` |
| 32-bit plain | i0-fix-13397, "Release-5.4.8-40-g28ca892ee Copy" | 20:54-21:01 | `dcbd2125565aa125df3e1c5bcba139a6ee00538e5e44521d552a75968ac3902d` | 17,148,416 | not kept |
| x64 fast | x64-fix-13397 45a95bac5 | 21:01-21:04 | `a04f3094fdee68e1ca48146483fb981dc5bff6c08ba4647a3edc9cd420d9e8b7` | 23,450,624 | not kept (0 errors, 0 unresolved; "not repeatable": the copied stubs carry COFF time stamps, issue 0131) |

| 32-bit `-ia` 43-civ, for 9074 | as `-ia` + `MOD_GLOBAL_MAX_MAJOR_CIVS=43` by `tools/release/stages/build.py` `inject_x86_define` (issue 4252's recipe; `s12_m43ia.sh`), "Release-5.4.8-40-g28ca892ee+ia440878983+m43 Copy" | 21:04-21:11 | `b596610b8ead0509a537d5d3e3de12d175d5111fa8569e924bd73939a5b78c5a` | 17,454,592 | `F:/c5-64bit-work/9075/i0ia-13397-m43/` |
| debug 1 and 2 (`CUSTOMLOG` lines; 2 also one in-game pass) | `s13_debug.py`, not committed | 21:25-21:33, 21:43-21:50 | - | - | `F:/c5-64bit-work/9075/i0ia-dbg*/` (deleted at hand-back) |

Pitfall: `tools/release/stages/build.py` imports `take_release`, so a caller puts `tools/release` on `sys.path` first.

PENDING: the takes branches (32-bit plain, x64 release).

### 2.6 Save test and parity: the three takes change no game result [L]

Builds of the takes branches (`s10_combined_build.sh`, 2026-11-16): 32-bit plain `i0-takes-9075` c125463b1, "Release-5.4.8-43-gc125463b1
Copy", 22:05-22:12 EST, SHA-256 `79e4b6280cc9b5d9df25dce0662baee60f86c110f2af3ec5c9f92e89202074ec`; x64 **release** (LTO, the
pins' configuration, D107) `x64-takes-9075` cd12e699a, 22:12-22:21 EST (link 432 s), 0 unresolved, DLL
`0d22ddec9ee3ea6c0e17508e1e99f7f349c36e4d1f169d24bc3bfabe3a55b027` (23,619,072 B), laid out as the pin layout
`x64-04b1211b-pfdee656/bin` with only the DLL and PDB replaced (server.exe `5b254430...`).

The save test crossed with the pins (`s11_parity.sh`; `tools/d10/run_d10.py --order-exception none --jobs 10`, all three
games, AI logging off in both arms, D49). The pins themselves pass x86 = x64 (`deps/i0.lock` `verified`), so:

| Run | x86 | x64 | Result | Wall |
|---|---|---|---|---|
| A | takes `79e4b628...` | pin `x64-04b1211b-pfdee656` (DLL `29f58e17...`) | **PASS**: tiny, standard, huge each pass writer, behaviour and cross-load | 22:21-22:42 EST |
| B | pin `i0ship-26234371` (`f92881a3...`) | takes layout (DLL `0d22ddec...`) | **PASS**: tiny, standard, huge each pass writer, behaviour and cross-load | 22:42-23:04 EST |

So on tiny 0-100, standard 0-200 and huge 350-400 the takes change no `stats.db` row and no save byte (version string
excepted), on both builds [L]. Reports: `evidence/9075/d10/A-report.txt`, `B-report.txt` (run folders
`C:/c5-64bit-work/9075/d10/`, deleted at hand-back). Limits: the hosts run without UI Lua (`--no-profiles`), so
#13395's deferral meets no UI script there; #13482's branches need a city-less owned plot, which these games never make
(upstream says the same); #13566 only names a move. None of the three was checked live in a city capture with EUI on
the stock route. The `D18` lines of both reports name the fork commits as unknown to the tool's clone ("git merge-base:
128"); with `--order-exception none` that does not matter.

### 2.6a #13482 reproduced before and after [L, 2026-11-16]

Upstream's probe through the Lua channel (`evidence/9075/test13482.lua`, `test13482.py`, `s15_arm13482.sh`): on c5h-b,
save `C5H-9075-HUGE_T428`, stock 32-bit route, the first flat, unimproved, featureless, resource-free land plot of the
active player is taken off its city (`SetOwner(-1,-1)`, `SetOwner(player,-1)`), gets a Pasture, and loses it again. (A
first try looked only next to the capital and found no such plot in either arm, 23:11-23:19; not a result.)

| Arm | DLL | Result |
|---|---|---|
| fix, 23:19-23:23 EST | `-ia` of `i0-fix-13482` (`s06_ia_resolve.py`, "Release-5.4.8-41-g54e8e5b8e+ia440878983 Copy", `2e359703...`) | answered: plot (79,21), Pasture placed (8), owner 0, no working city, removed afterwards; no box (`test13482-fix.json`) |
| pin, 23:23-23:27 EST | pin `-ia` `3c2f47da...` | no answer within 60 s; the DLL's "Game Crash" box, access violation reading 0x000009D0 at `CvGameCore_Expansion2.dll+0x33F779` (file offset): upstream's `[ebx+9D0h]` in `CvCity::ChangeImprovementCount` through the missing owning city. Closed by the claim's owner (`dismiss --fatal`, `stop --after-fatal`, D155); evidence `%USERPROFILE%/c5-instances/c5h-b/run/crash/20261116_232552_pid49444` (`test13482-pin.json`) |

The `-ia` build of all three takes was not made: the instruments patch and #13395 both change `Lua/CvLuaSupport.cpp`
and `Lua/CvLuaSupport.h`, and `s06_ia_resolve.py`'s plain `git apply` stops there; a pin batch that takes #13395 needs
that conflict resolved in its `-ia` patch, as 9045 did for 0084 and 0116 (`upstream-fix-batch-1.md` 8b). *(Corrected
2026-11-16 by 0463, section 3.2: with `git apply -3` the `.cpp` merges cleanly and the one conflict is in the `.h`;
resolved there.)*

### 2.7 #13406: proposal, not built

What it does [C, PR head 6b3d305cf, `Exe/CvExe.cpp` +518]: on the UI thread, before the first in-game frame (called
from `InGame.lua` `OnUpdate` until done), it finds the UI manager (`g_pUIManager`, DX11 VA 0x2DD1D1C) and its command
streams (+0x6F0, left/right +0x6F4/+0x6F8 with stereo 3D), allocates new blocks with the EXE's own allocators
(`FireMallocAlignedNoTracking` 16-aligned for the data, `operator new[]` for the commands), and swaps the stream's data
and command pointers and its sizes (+0x34 data size, +0x08 command capacity) to 4 MB and 16,384 commands while the
current half is empty; DX11 swaps one half per frame, so it takes two calls; the old blocks are freed with the matching
EXE free. Tablet is refused (its frame end copies into a 1 MB GPU buffer).

Why a ticket of its own and not this one-off: it writes EXE heap objects and calls EXE functions (D136-style interop,
allowed, but a design check for the shell); it needs a trigger on the UI thread between frames, which upstream gets
from mod Lua that we do not ship (installer D26/D27); on the product it must run in the EXE process, that is the proxy
(another team's package); and it needs a live reproduction at about 450 unit flags in view (the crash of
`message-queue-and-fixed-buffers.md` section 2). Proposed in 2.8.

### 2.8 Proposals for the MC

**Carry-series change for the next pin batch** (`tools/release/series/carry.tsv`; the pin moves only by the MC's pin
procedure). Three takes, four `fix` rows on branch `i0`, after the last row (26234371e), in this order (the order of
`i0-takes-9075`; each also applies alone on the pin):

```
fix	i0	dc2779953f33c7c9a8c7eedf645ed359dc1dba09	Add the missing homeland move name for opportunity attacks (#13566)	745adf954062520aee98f5daa95167e50e6bf9b3 #13566
fix	i0	34607be3459af2b0d75bccc0074dd79095e6c721	Fix crashes on owned plots that have no owning city	#13482
fix	i0	54e8e5b8e5524fd5d82ca39c80fdb0a35344cf75	DoAlterWorkingPlot: give a plot without an owning city to the city	#13482	34607be34
fix	i0	b2da2eaaf202f8296b6e163cd0cfe04930ab0874	Defer Lua hooks raised while a city is captured or destroyed	#13395
```

(Each hash is the commit on its own fix branch; the pin batch cherry-picks them, as 9045 did; a base containing the
named upstream commit or pull request drops the row.) No row for #13397 (section 2.3a). Severity, for the MC's batching
rule: #13395 medium
(timing-dependent use-after-free, long autoplay games); #13482 and #13566 low (mods or rare paths; a logging read).

**Follow-ups, each a ticket of its own:**

1. **The product's exposure to the 4 MB queue overrun** (proxy package). The product's EXE runs LandmarkSystem (D33),
   so a whole-map layout floods `g_EventSystemLocalMachine` exactly as on the stock route, and nothing guards it: the
   server's DLL cannot read the EXE, and `proxy/` has no guard. Files: `proxy/src/proxy_session.cpp` (the D33 keys,
   LandmarkSystem on), `proxy/src/proxy_shell.cpp` (the EXE-side data writes the proxy already makes); the queue layout
   and the guard design: `research/02-exe-internals/message-queue-and-fixed-buffers.md` section 1 (EngineQueueGuard:
   the `kernel32!InterlockedExchangeAdd` import slot of the EXE, no code bytes), the batching: section 2.3 above.
   Options: (a) port EngineQueueGuard into the proxy (drops what does not fit; the stock test builds' behaviour, loses
   farm fields on an overflow); (b) lossless batching. Section 2.3a shows that batching by deferring plots past the
   EXE's load pass loses art [L] (its T1100 crashes are not attributed: the guard build crashed at one of the same
   sites), so (b) must first answer:
   does upstream's full form of #13397 (#13379 + #13397 with the `LoadScreen.lua` hold, `ContinueDeferredLayout`) draw
   the same art as the pin and survive T1100 (test it as upstream ships it, on a test instance with upstream's Lua); and
   is the T1100 loss an address-space cost of drawing the fields the guard drops. Inputs: 9074's T1100 saves
   (`F:/c5-64bit-work/9071/stress/`, 43-civ `-ia` builds), the test save `C5H-9075-HUGE_T428` (c5h-b, copy on `F:/c5-64bit-work/9075/saves/`; the railway loop on the
   eastern peninsula at the start camera is the visible check), the fork branches `i0-fix-13397` / `x64-fix-13397` and
   the debug scripts `evidence/9075/s13_debug.py`, `s14_live_long.sh`. Then the product route.
   **What the T1100 failures look like (9074's data, `research/08-performance/marathon-stress-t2000.md` 4.1, commit
   3bd0e36c2; MC 2026-11-16)**: low address space is not the separator (the clean probes T050-T1100 had as little below
   2 GB: 29.6-34.8 MB free, largest block 0.5-1.9 MB). What grows towards T1100 is total private memory, the shrinking
   largest free block above 2 GB (669 MB at T050, 32-45 MB at T1100) and the queue overflow (none before T650, 3-7 MB per
   load at T1100) [L]. The failure sites (VA 0x5B1F80 list loop, 0x55D9E2, 0x7A3F0A, 0x77C6DA) look like corrupted EXE
   record-list and event-handler structures [I]. #13397's part in them: not shown.
2. **#13406, the 1 MB UI draw list** (section 2.7). Both routes crash at about 450 unit flags in view. Needs: an EXE-side
   trigger on the UI thread between frames (the proxy on the product; for the 32-bit DLL a DLL-side trigger, since we
   ship no mod Lua), the EXE allocator calls, DX11's two-step swap, and the live reproduction.
3. **#13522, address space in the 32-bit DLL** (not a crash fix as such): ~41 MB with VP alone, more with mod sets;
   worth a measurement ticket for i0 (largest free block after a late load, with and without), since 32-bit
   address-space exhaustion is the stock route's main crash. Not needed for x64.
4. **Mod-Lua crash fixes we cannot ship in the DLL**: #13463 (load from within a game), #13467 (statements left open
   lock the database rebuild), #13539 (maps wider than 181). They belong to the mod-file line (installer D26/D27) if the
   project ever ships changed mod files; until then players get them from the next VP release.

## 3. DLL pin batch 3 (ticket 0463, 2026-11-16/17)

Tracker `tracker/issues/0463-pin-batch-3-upstream-crash-takes.md`, branch `task-0463-pin-batch-3` (account B). The MC's
answers on the ticket's thread: #3293 (push the two batch branches as new fork branches; nothing upstream, no pull
request), #3296 (take bug 0421's guard too; the locks on the branch, COMMON 18 as proposed text; whole games on the
workstation, not oldpc or the laptop). Fork clone `C:/c5-64bit-work/0463/fork` (`--shared --no-checkout` of the owner's
clone, origin = the GitHub fork). Scripts: `evidence/0463/`.

### 3.1 Branches [C, 2026-11-16 23:40-23:46 EST]

- **`i0-ship-batch3` 39f17f754** (`Release-5.4.8-44-g39f17f754`, fingerprint `v1 bf10d8ed...c236`): `i0-takes-9075`
  c125463b1 (the pin 26234371e + 9075's four cherry-picks -x of upstream 745adf954, f2f4166b9, 65936969b, 6949d5787; its
  tree `ff91f621` equals fresh cherry-picks -x of the fix-branch commits dc2779953, 34607be34, 54e8e5b8e, b2da2eaaf onto
  the pin) + a cherry-pick -x of `i0-fix-0421` ae0efb7c6. No conflict, no hand edit.
- **`x64-batch3` 4f4daac70** (`Release-5.4.8-57-g4f4daac70`, fingerprint `v1 2b1571a9...45db`): the x64 pin 04b1211b8 +
  `--no-ff` merge of 39f17f754 (auto-merge in CvHomelandAI.cpp, `git show --remerge-diff` empty). `git diff 04b1211b8
  4f4daac70` and `git diff 26234371e 39f17f754` have the same patch-id (d2b30596...). `Lua/CvLuaPlot.cpp` equals
  `x64-fix-0421`'s.
- Neither branch contains 6f2c2ee3, e29032ad or 90f1c4c0 (`merge-base --is-ancestor`). Pushed to the fork as new
  branches 2026-11-16 23:46 EST (`s04_push.sh`, refuses an existing name; never forced). `i0-ship`, `master` (26234371e)
  and `x64` (04b1211b8) untouched; fast-forwarding them is the MC's step.
- **Carry series** (`tools/release/series/carry.tsv`): five `fix` rows on `i0` after 26234371e, 2.8's four plus
  `ae0efb7c6` (upstream `-`). `branches.py --base Release-5.4.8 --series carry.tsv --compare-i0 39f17f754 --compare-x64
  4f4daac70` (local branches `*-release-check0463`, not pushed): 50 applied, 0 dropped, 0 skipped, **both trees equal**.

### 3.2 The instruments (`-ia`) patch: one conflict, in `Lua/CvLuaSupport.h` [C]

`git apply -3` of 9045's `ia-ee893bc18-440878983.patch` on a worktree of 39f17f754: 40 files apply, `Lua/CvLuaSupport.cpp`
merges cleanly (#13395's deferral code and the instruments' external-Lua code touch different places), and **one
conflict in `Lua/CvLuaSupport.h`**: both add a declaration after `CallAccumulator` (#13395's `class DeferredHookScope`,
the instruments' `PollExternalLuaRequest()`). Resolved by keeping both, #13395's first (`s06_resolve.py`). *(Corrected
2026-11-16 23:44: 2.6a said the conflict is in `Lua/CvLuaSupport.cpp`; that was `s06_ia_resolve.py`'s plain `git apply`
stopping at the first file of the pair; with `-3` the `.cpp` merges and the `.h` conflicts.)* No ordering question as
in 9045's `CvGame::update()` case: the external-Lua poller runs from `CvGame::update`, the deferral scope only inside
`acquireCity` and `CvCity::kill`.
Resolved patch **`tools/d10/evidence/0463/ia-39f17f754-440878983.patch`** (SHA-256 `97887b35...767d`, 41 files, +9179 -41,
kept byte for byte by a `-text` attribute); `git apply --check` passes on a git archive of 39f17f754. Registered in
`tools/release/stages/build.py` `IA_PATCHES` as row `0463` (base = the pin commit itself, so every i0 commit that contains
the batch-3 pin picks it by base; test `test_ia_patch_default_rows_pick_by_real_bases` updated, the 7 `ia_patch` tests
pass). Pitfall: the 9045 patch file as checked out in a worktree under `core.autocrlf=true` (SHA-256 `8615e0f5...`, every
line CRLF) does not `git apply -3` in a fork worktree (every file "patch does not apply"); the committed blob
(`git show HEAD:<path>`, `59c53a10...`) does.

### 3.3 Locks [C]

`deps/i0.lock` and `deps/dll.lock` on the branch: `branch`, `commit`, `describe`, `fingerprint` (x64), `commits` (i0),
`source` and a re-pin paragraph each, with "Replaced" traces. The release hashes: section 3.4. The pins move on `main`
only by the MC's merge.

### 3.4 Release suite on the new pins (validated tier, D162 journeys)

`C5_JOB_OWNER=0463 python -I tools/release/take_release.py Release-5.4.8 --tier validated --carry pins --for-art --fork
C:/c5-64bit-work/0463/fork --work C:/c5-64bit-work/0463/run --scratch F:/c5-64bit-work/0463/scratch --who 0463
--gate-wait 60` with `C5_RELEASE_INSTANCE`/`C5_RELEASE_HI_INSTANCE=c5h-b,c5h-d,c5h-f`, `C5_RELEASE_HI_TICKET=0463`,
`C5_RELEASE_TESTBEDS=local` (`evidence/0463/s10_take.sh`), started 2026-11-16 23:49 EST from a clean worktree at
cd3b335e8 (the branch's first commit). Three stops on the way, each rerun with `--from`:

1. **Build stage FAILED, 00:15 [L]**: "this repository moved during the build stage (cd3b335e8 dirty=0 -> dirty=1)".
   The x64 step had ended (764 s, DLL `e158f0e6...`), and I edited tracked files (this section) while the 32-bit
   builds ran. The check is at the end of the whole stage, not of the x64 step: **keep the driver's worktree free of
   tracked changes until `stage build ok`**, including during the 32-bit builds (0351's rule, restated). Rerun at
   63a79cfc6: build ok 00:26 (x64 615 s; the 32-bit builds reused).
2. **Model stage STOP, 00:35 [L, C]** (CL 10): `proxy/model` differs from the regenerated model in 84 places:
   `lua.json`/`model.json` 41 each (CvLuaPlot.cpp line shifts of 0421's guard; the six guarded methods gain the call
   `IsValidImprovementIndex`, rule `free_function_by_name[I]`) and `policy-table.tsv` 2 rows:
   **`ICvEngineScriptSystemArgs1::PushNULL` and `GetString` go from `unused` ("the DLL never calls it") to
   `server_local`**: #13395's deferral copies each hook's arguments and pushes them again, so the DLL now calls both.
   The server implements both on its local args object (`server/src/srv_lua.cpp` lines 375, 393) and its remote args
   object forwards them to the EXE's (`server/src/remote/srv_remote_lua.cpp` `RemoteArgs`, GetString explicit, PushNULL
   generated). `interfaces.json` and `slot-proof.json` equal (provenance only); generated runtime equal (so
   `kAppSchemaHash` unchanged). Regenerated with `regen_model.py --src <the run's x64 source> --write` and committed
   (e6a717c29); `proxy/tools/test_policy_table.py` hard-coded `dll_to_engine_methods_used` 296, now reads the committed
   count (298) (13/13). `gen_pure_list.py --check`: the audited lists hold. Rerun from build (x64 and the 32-bit builds
   again at the new tools commit).
3. **Offline stage FAILED, 01:30 and 01:31 [L]**: `hutest --scenario all` stopped at `foreign-strict` with
   `PermissionError` on `server-report.txt` (bug 0453, on main's build too; reproduced on C: as well as on the F:
   scratch). Fixed on this branch with the MC's after-the-fact grant (#3416, MC 01:36): `build_hutest.py read_settled`
   retries a held run file for up to 30 s, then names it as the scenario's failure; 19/19 scenarios pass. Test
   `proxy/tools/hutest/test_read_settled.py` (a child holds the file with share mode 0): the old plain read raises
   PermissionError (teeth), `read_settled` waits and returns the text, names the file after a short limit, missing file
   unchanged; 4/4. Rerun `--from offline` at 738addb82 (the build stage not rerun: its layout says tools e6a717c29).

**The build (stage ok 2026-11-17 00:42:55-01:17:45 EST, tools e6a717c29, dirty 0)** [L]: x64 release (LTO), narrowing
794 against 798 (0 new, 4 gone), repeatability check OK (181 link inputs, 532 members, none stamped), 0 unresolved. Layout
`C:/c5-64bit-work/0463/run/layout/x64-4f4daac7-pe6a717c`: `bin/CvGameCore_Expansion2.dll`
**4CB366A2F4460699DC8009FEE347CFCBE1AA7AC42C749DC29948FC04F77F5626** (23,669,760 B), `bin/server.exe`
**8D3BCA48CC44404166EE924C3F6D8E11160ECDE2342CEFB509F44C66A5181CBA** (7,167,488 B), `proxy/bin/CvGameCore_Expansion2.dll`
**81140D32ED6D0FAB6B5680C7ECD11BDB00F7F410A0210AD83FB84CAEDBC7BB15** (1,153,024 B); `lua51_x64.dll` 30724D6D...,
`CvGameDatabase_x64.dll` 9C37B39E..., `CvLocalization_x64.dll` 35863EDE..., `civ5save.exe` 409422EF... (= the pin
layout's). 32-bit plain `i0ship-39f17f75` **11DBD076997F4AD826D23685E8BFE40B7BEFFD1C2DFE8A078F3B679253534202**, `-ia`
`i0ship-39f17f75-ia` **8A201A18CEA96B45F47786B5CE9612292589149B2457AF5ACBAA89D948F133EB** (the `-ia` patch chosen by base,
row 0463). The x64 DLL bytes differ between the three builds of the same source (`e158f0e6`, `1768d70c`, `4cb366a2`):
`__DATE__`/`__TIME__` come from the tools commit's time (`deps/dll.lock` `epoch`), which moved with each commit [I].
census ok (0 x64-only records), model ok, **purity 137 method keys + 29 Lua, 0 dropped**, pack ok; offline:
`test_proxy_rt` **107/107**, hutest **PASS** (55 checks, 0 failed, 0 unserved), scenarios 19/19, verify leg (Tiny 0-50
through the proxy, strict) **PASS** (its log says `0 pure method ids (0 of 0 listed keys)`: the leg runs without the
pack's purity file, so its "verified" counts are 0; the instance stage below runs with it).

4. **Savetest FAILED, 01:58 [L]: the hosts, not the batch.** Parts 1 and 3 failed on tiny, standard and huge (writer
   identity: 6 one-byte runs `00`/`04` per tiny save; the x64 round trip), huge part 2 from turn 350. Cause: at 00:20
   the workstation's civ5-headless `out/latest.txt` was switched to `20261116-0452-landmasses` (bug 0452: the x86 host
   now calls RecalculateLandmasses after a load), and the server of this branch (main of 23:34) did not yet carry 0452's
   server half (a91e0c3b3). **Control [L, 01:59-02:01]: run_d10 tiny on the OLD pins** (`i0ship-26234371` +
   `x64-04b1211b-pfdee656`, the layouts that passed for 0351) **fails the same way** (`tools/d10/evidence/0463/
   d10-control-oldpins-tiny-FAIL.txt`), so every D10 run against the pin layout with today's c5h fails until the pin
   layout carries 0452 (`server-host.md` 17.4-17.5; MC 02:11: the next pin, this one, brings it). The A/A "CONTROL"
   lines (continuation vs direct play, one DLL) differ in passing runs too (0351's report): they never gate.
   Merged main (3d0441e8b, with 0452), rebuilt from build.

**The build (stage ok 2026-11-17 02:03:58-02:31:16 EST, tools 3d0441e8b, dirty 0)** [L]: x64 release (LTO), narrowing
794 against 798 (0 new, 4 gone), repeatability check OK (181 link inputs, 532 members, none stamped), 0 unresolved. Layout
`C:/c5-64bit-work/0463/run/layout/x64-4f4daac7-p3d0441e`: `bin/CvGameCore_Expansion2.dll`
**D6D8723EE5D3DD09D7D2288E424C6F1AC9202C98E80F5A7836EF86C1145D88E7** (23,669,760 B), `bin/server.exe`
**546293C515625ADAC11C1BCC458F2FC2A42D0B13B803EFB2FDE82B4BC71420ED** (7,184,384 B), `proxy/bin/CvGameCore_Expansion2.dll`
**985D982294D35B0A1D827358C87C38D9E1941D8FFEACB3D96243B9F7DD9A909F** (1,153,024 B); the libraries and civ5save.exe =
the pin layout's. 32-bit plain `i0ship-39f17f75` **44D89D7C06DDFFFF3061517C706DCEB8062C659F91778989639B729A6F2DC302**
(version "Release-5.4.8-44-g39f17f7545 Clean": git describe in the fork clone needs 10 hex digits there), `-ia`
**B098491D3B7699782EDB4C204A2269FAC436CD897059DC6F36DA7BE6669B2789**. (Earlier builds of this run, replaced: x64
`e158f0e6`, `1768d70c`, `4cb366a2`; the x64 DLL's bytes move with the tools commit time, `deps/dll.lock` `epoch` [I].)
census, model, purity (137 + 29, 0 dropped), pack, offline (rt 107/107, hutest 55/0, scenarios 19/19, verify leg) ok again.

**Save test (run_d10, D24) PASS [L, 02:46:41-03:07:08, 1,227 s]**: tiny, standard, huge each parts 1, 2, 3, only the
version-string exception; x86 direct = x64 direct **30,666 / 351,496 / 689,879 rows**
(`tools/d10/evidence/0463/d10-take-report.txt`). Tiny and standard equal the descriptor's reference counts; **huge
689,879 against 683,123**: the driver stops ("x86 = x64 row counts differ from the descriptor's reference_counts").
The huge game loads the 5.4.8 reference save, which 0452 changes (17.4: it now plays with the water flags). Attribution
to 0452 rather than the batch: the crossed runs (3.5). With the MC's word (#3580) the descriptor's
`reference_counts.huge` is now 689879 (`tools/release/releases/Release-5.4.8.json`, `reference_counts_pins` says why;
`test_descriptor.py` 31/31), and the take reruns from savetest (49f4ce004).

5. **Savetest STOP again, 04:26 [L]: the same count stop.** The stages after `testdata` read **testdata's candidate
   descriptor** (`<work>/testdata/Release-5.4.8.json`, issue 3512), written from the repository's descriptor when
   testdata ran; editing `tools/release/releases/Release-5.4.8.json` does not reach a run whose testdata is done
   (27 min of save test spent again). Rerunning `--from testdata` would change testdata's outputs and with them the
   build stage's inputs (a rebuild), so the candidate's `reference_counts.huge` was edited by hand to 689879 (the
   README's 1a.3 allows it) and the take rerun `--from savetest`. Rule: **a count change for a re-take goes into the
   repository's descriptor and into the run's candidate**, or the take is started after the descriptor change.

**The rest of the tier [L, 2026-11-17]**: savetest **PASS** again 04:27-04:52 (30,666 / 351,496 / 689,879 = the
reference counts); drift Tiny 0-100 x86 = x64, no differing turn; fixchecks 0096 PASS, 0084 PASS, 0103 PASS, oldsave
(5.4.8 huge 350 -> 352) exit 0; refsave P10 of the descriptor's huge save (350 -> 352) ok; **instance** (c5h-b,
04:56-05:14): tiny arm **180,556** answers verified, huge arm **378,562**, both 0 mismatches, 0 lock errors, no
FALLBACK, `purity: loaded (pack=1)` bound to DLL `d6d8723e...` (137 method keys, 29 Lua names). **wholegames** (host
`local`, the workstation, 05:14-07:45; not a timing): **4 of 4 x64 = x86, identical** (16 tables), and the same endings
and row counts as 0351's run on the old pins: Tiny Continents 5051/5253 Cultural turn 361 (903,979 rows), Standard
Fractal 5757/6868 Science 414 (2,255,970), Standard Pangaea 3333/4444 Cultural 370 (1,622,664), Small Pangaea
1819/2021 Science 427 (1,708,434). So the batch changes none of the four whole games either (new games: 0452 does not
reach them).

6. **hi_journey FAILED, 08:07 [L]: release-basic passed on both routes, then the -ia exit crash.** release-basic
   PASSED on the product route (63 steps, 533 s) and on the stock route (63 steps, 473 s), stockctl 6 of 6 checkpoints
   the same. After the stock replay's own EXIT/Yes the game ended 0xC0000005 with no box (pid 39336; dialog watcher
   #4009): the known `-ia` MemoryImports exit crash (bugs 0483/0484; test builds only). The branch's hi_journey (main of
   02:00) treated the marker as a box before the next launch and failed, keeping the release layout and the claim
   (its undo list lives in memory); main's 82e68bf81 (04:40, 2604) goes on after such a crash. c5h-b restored by hand
   (`evidence/0463/s16_restore_c5hb.sh`: made saves and start copies removed, settings and vpproxy.ini back by hash,
   pack files removed, `set-proxy --original`, `set-dll` back to `3c2f47da`, front end original, `verify --quick` rc 0)
   and released.
7. **hi_journey FAILED again, 08:13 [L, C], after merging main**: `provision.py set-proxy: exit 2: the x64 DLL would be
   CvGameCore_Expansion2_x64.dll (--x64-name auto, the pack's manifest names it) but the proxy does not know that name (a
   proxy built before issue 2606)`. The stage makes its pack with the current `pack.py` (2606: the x64 DLL is
   `CvGameCore_Expansion2_x64.dll` in packs) while the layout's proxy was built at 3d0441e8b. **A take whose tools move
   past 2606 between its build and its hi_journey must be rebuilt.** The stage restored and released c5h-b itself.

**First take, kept as evidence (MC 2026-11-17 08:5x):** everything of the validated tier passed on the build at
3d0441e8b except D162 (release-large not played). **Second take**: the whole tier again from build at f8e9f424b
(main with 0452, 2606 and 82e68bf81), MC agreed. Build stage ok 08:15:09-08:54:22: layout
`C:/c5-64bit-work/0463/run/layout/x64-4f4daac7-pf8e9f42`: `bin/CvGameCore_Expansion2.dll`
**2D737BD58EF4FD31829793C5992497FC366CDFC6A522789DA29429C9A597438B** (23,669,760 B), `bin/server.exe`
**ED829BDA1F338B100703630FEB885D72FE36FE19084782AF483E743A05277FE8** (7,564,288 B), `proxy/bin/CvGameCore_Expansion2.dll`
**4815ED814CD2BC8CC727C5F0561EF7A80066490B2566C5DDCB4E482DC79B775F** (1,162,240 B); libraries and civ5save.exe = the pin
layout's; 32-bit `i0ship-39f17f75` **13CFB6D15E830031AECCD4747764A5A611D33CABBCC5AC06DBD649E1246E5053**, `-ia`
**7DE39F710C5D4169DC5101950F493488C357EFDACB003E34AA9E2780E96E52BE**; narrowing 794/798 (0 new), repeatability check OK,
0 unresolved.

**Second take: verdict `validated`, D162 for the owner: yes [L, 2026-11-17 08:15-16:28 EST].** census, model (equal to the
committed model of e6a717c29), purity 137 + 29 / 0 dropped, pack, offline (rt, hutest, scenarios 19/19, verify leg)
ok; **save test PASS 09:15-09:37** (30,666 / 351,496 / 689,879 = the reference counts); drift none; fixchecks 0096,
0084, 0103, oldsave ok; refsave ok; **instance** (c5h-b, 09:42-09:59): tiny **181,998** and huge **402,070** answers
verified, 0 mismatches, 0 lock errors, no FALLBACK. **wholegames** (local): the first run's Standard Pangaea and Small
Pangaea arms failed at 11:09:14 within 0 s with rc 3221225794 (0xC0000142, STATUS_DLL_INIT_FAILED: no process could
start at that moment; the agent session was paused by the account's usage limit at the same time) [L; cause I]; rerun
11:31-14:57: **4 of 4 x64 = x86, identical**, the same endings and rows as the first take (903,979 / 2,255,970 /
1,622,664 / 1,708,434). **hi_journey**: on c5h-f (14:57-15:41) release-basic passed on both routes but release-large
failed **identically on both routes** at step 11 ("plot [50, 50] not under the cursor at its pixel now, nor after a
search by moves"; moved and attacked ok): an instance or journey matter, not the build (the stock route fails the same
way; release-large passes on c5h-c and c5h-d for 2604 and 4758) [L; cause on c5h-f not looked into]. Rerun on c5h-d
(`C5_RELEASE_HI_INSTANCE=c5h-d,c5h-b`, 15:46-16:28): **release-basic PASSED** (product 63 steps 594 s, stock 63 steps
507 s, 6/6 checkpoints same) and **release-large PASSED** (product 21 steps 439 s, stock 21 steps 435 s, 5/5 same);
the stock game's known `-ia` exit crash after release-basic (16:07, pid 67516) was passed over as 82e68bf81 intends;
restore ok, `verify` rc 0, claim released. `take_release.py --art-check`: "for the owner: yes (both journeys passed on this
build, product and stock)", played on x64 DLL `2d737bd5...`, server `ed829bda...`, proxy `4815ed81...`, i0ia
`7de39f71...`. Pack manifest (`<work>/pack/vp64pack/manifest.json`) SHA-256
`88c83c85f36b36605ba2fed50d04eee8575cafb6ef057b1481e45ed58f686ca3`. Kept (D141): `F:/c5-64bit-work/0463/take2/` (state,
reports, driver log, pack, candidate locks and descriptor) and `F:/c5-64bit-work/0463/layouts/` (the three layouts,
SHA256.txt checked); the run folder on C: is deleted at hand-back. Evidence in the repository:
`research/06-tooling-and-workflows/evidence/0463/` (state.json, validated-report, hi_journey report).

### 3.7 What 0463 leaves for the MC

- **The pins** (`deps/i0.lock`, `deps/dll.lock` on the branch): `commit` 39f17f754 / 4f4daac70 and their keys; the new
  `release`/`instance`/`verified` (and `tools`/`epoch` in dll.lock) name `%USERPROFILE%/c5-i0/dlls/i0ship-39f17f75`,
  `i0ship-39f17f75-ia`, `x64-4f4daac7-pf8e9f42`, **which the MC lays out** from `F:/c5-64bit-work/0463/layouts/`
  (until then `provision.py pinned_dll` and run_d10's default find no build). The batch-2 values are kept as
  `release-b2`, `instance-b2`, `verified-b2`, `tools-b2`, `epoch-b2`. Fast-forwarding the fork's `i0-ship`/`master` to
  39f17f754 and `x64` to 4f4daac70 is the MC's step. The 43-civ keys (`release-43` etc.) still describe the batch-2
  builds; no 43 build of batch 3 was made.
- **COMMON rule 18, proposed text** (the MC's file): "Since 2026-11-17 (ticket 0463, DLL pin batch 3): 32-bit
  `i0-ship-batch3` 39f17f754 (`i0ship-39f17f75`, instance-aware `i0ship-39f17f75-ia`), x64 4f4daac70 with server.exe and
  proxy from repository f8e9f424b in the layout `x64-4f4daac7-pf8e9f42` (DLL 2D737BD5..., server ED829BDA..., proxy
  4815ED81...; `deps/dll.lock` `release`). The batch carries upstream #13566, #13482, #13395 and bug 0421's guard and
  changes no game result (crossed save tests and whole games identical, upstream-fix-batch-2.md 3.5); the layout carries
  bug 0452's load fix, so the D10 huge game now has 689,879 rows (was 683,123). Replaced: `i0ship-26234371`
  (-ia `3c2f47da`), `x64-04b1211b-pfdee656`."
- **Other fix branches waiting for a pin change**: this file names none besides 0421's (taken). `upstream-fix-batch-1.md`
  8h names bug 0132's statistics-logger fix (`i0-fix-0132` / `x64-fix-0132`), not in pin batch 2 or 3 [C: its
  commits are not ancestors of 39f17f754]; not taken (MC #3296).
- **Tools and traps found**: 0453's hutest fix (3.4 item 3, MC #3416: 0453 closes at the merge); the `-ia` row in
  `IA_PATCHES`; the regenerated proxy model; the candidate-descriptor trap (item 5); the build stage's
  repository-moved check covers the 32-bit builds too (item 1); a take must be rebuilt when main's tools move past a
  pack-format change (item 7).

### 3.5 The crossed save test: batch 3 changes no game result [L, 2026-11-17]

9075's method (2.6) with all four fixes, on today's hosts: both sides carry 0452 (the workstation's c5h
`20261116-0452-landmasses`; the x64 arm's server.exe from 3d0441e8b), so the "old x64" arm is the batch-3 layout's `bin`
with only the DLL and PDB replaced by the old pin's (`29F58E17...`, from `x64-04b1211b-pfdee656`):
`F:/c5-64bit-work/0463/layout-oldx64dll-p3d0441e` (`evidence/0463/s14_oldx64_layout.sh`). `run_d10.py --games
tiny,standard,huge --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, huge save
`Release-5.4.8-HUGE_0350` (`c499d3c6...`), AI logging off (`evidence/0463/s13_d10.sh`).

| Run | x86 | x64 DLL | Result | Rows tiny / standard / huge | Wall (EST) |
|---|---|---|---|---|---|
| A | batch 3 `44D89D7C...` | old pin `29F58E17...` (0452 server) | **PASS** all three games, parts 1-3 | 30,666 / 351,496 / 689,879 | 03:07:59-03:31:27 |
| B | old pin `i0ship-26234371` (`F92881A3...`) | batch 3 `D6D8723E...` | **PASS** all three games, parts 1-3 | 30,666 / 351,496 / 689,879 | 03:31:31-03:58:11 |

Reports `tools/d10/evidence/0463/d10-crossed-A-report.txt`, `d10-crossed-B-report.txt`. The old pins pass x86 = x64, so
the batch (the three takes and 0421's guard) changes no `stats.db` row and no save byte (version string excepted) on
tiny 0-100, standard 0-200 and huge 350-400 [L]; and the huge count 689,879 comes with the old DLL too, so its change
from 683,123 is 0452's. Limits as in 2.6: no UI Lua in the hosts, no city-less owned plot in these games; the live
probes (3.6) cover #13482, #13395 and 0421 in the real game.

### 3.6 Live probes on the new pin: #13482, #13395 with EUI, 0421 [L, 2026-11-17 11:14-11:29 EST]

c5h-b (C:, claimed `impl-0463`), stock 32-bit route (no proxy), `frontend.py prepare --mods cp,vp,eui` (EUI from the
instance's DLC `UI_bc1` plus VP's EUI compatibility files, as a player runs it), save `C5H-9075-HUGE_T428` (turn 429),
the batch-3 `-ia` DLL `7DE39F71...` (second take). One arm (`evidence/0463/s20_arm.sh`): `set-dll`, launch, in game,
0421's guard test (`guard_test.lua`, the first unimproved land plot with a resource standing in for 0421's plot 51,13),
then `probe.py`: upstream's #13482 probe (`test13482.lua`, 9075's) and the #13395 capture (`capture13395.lua`), one
described shot, `stop`, front end restored, `set-dll --previous`. Results in `evidence/0463/live/<arm>/`.

- **0421 guard: PASS** (arms batch3, batch3b): every out-of-range call answered (`IsResourceConnectedByImprovement`
  false, `CanHaveImprovement` false, `GetUpgradeTimeLeft` 0, `CalculateImprovementYieldChange` 0,
  `GetNumResourcePostModifiers` = its -1 value, `SetImprovementType(48)`/`(-2)` left the plot unimproved); no box.
- **#13482: answered** on the new pin: `result=ok;x=79;y=21;placed=8;owner=0;workingcity=false;after=-1` (the plot taken
  off its city, a Pasture placed and removed; 9075's pin arm stopped here with the "Game Crash" box at
  `CvCity::ChangeImprovementCount`, 2.6a).
- **#13395: one city capture with EUI, no crash, every deferred hook saw the complete city.** `Player:AcquireCity` of
  the AI's Teotihuacan (82,52; population 28) by the active player as a conquest, with a read-only listener on every
  fire-and-forget GameEvent (the 104 `GAMEEVENTINVOKE_HOOK` names that are never raised as a test or accumulator, plus
  Firaxis's `CallHook` names; `s11_hooknames.sh`) recording `owner/population` of the plot's city at each call. The
  chunk must run in a UI state (`--@state=InGame`): `GameEvents` is nil in the channel's Main state (first try 11:18).

| Arm | DLL | Hook log (call order, `name@owner/population`) |
|---|---|---|
| batch3b | batch 3 `7DE39F71` | UnitPrekill@1/28, UnitSetXY@1/28, **SetPopulation@0/26 x4**, CityConvertsReligion@0/26, UnitSetXY@0/26, CityCaptureComplete@0/26 |
| pin-control | batch 2 `3C2F47DA` (negative control) | UnitPrekill@1/28, UnitSetXY@1/28, **SetPopulation@1/0, SetPopulation@0/1, SetPopulation@0/1**, SetPopulation@0/26, CityConvertsReligion@0/26, UnitSetXY@0/26, CityCaptureComplete@0/26 |

  On the old pin three `SetPopulation` hooks run while the city is half-built (the old owner's city at population 0,
  then the new owner's at 1): the window in which a UI script read freed data (upstream's crash). With batch 3 the same
  hooks are queued and run once the city is complete. UnitPrekill and the first UnitSetXY come before the scope begins
  and see the old city intact. Both arms: game and EUI ran on, no box, the dialog watcher's marker clear; the
  capture took 772 ms (batch 3) and 1,087 ms (pin) inside the Lua call. Shots in the Visual Checks feed (issue 0463,
  diagnostic). Not checked: a capture by a unit in combat (the probe calls the capture function directly), the
  product route, and the UI-thread race itself (no crash was reproduced on the old pin; the hook log shows the window).
