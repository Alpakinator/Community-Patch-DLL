# Upstream fix batch 1 (one-off 9020, 2026-11-11)

Decision D44 lets the MC take useful upstream fixes of the Vox Populi DLL into the project's DLL line; D46 named the
batch. This file records what was taken, what was left out and why, how each fix changes save bytes and game results
against the pins, and what is not verified. Tracker: `tracker/issues/9020-dll-upstream-fix-batch-1.md`. Survey the batch
came from: `research/sources/0081-upstream/0081-upstream-fixes-for-mc.md` (upstream master dde15030d).

## 1. Branches and builds

The project's fork of Community-Patch-DLL (clone `D:/c5-64bit-work/9020/fork`, a local clone of 0081's clone with remotes
`origin` and `upstream`):

- `i0-ship-next`: cut from the i0 pin ccfd6dd4d; 17 upstream commits cherry-picked with `-x` (origin hash and author kept),
  in the order of section 2. **No conflict, no hand edit** in any of them [C, `git cherry-pick` exit 0 each].
- `x64-next`: the x64 pin c43ea1b9f merged with `i0-ship-next` (`--no-ff`), the route every earlier x64 pin took
  (`deps/dll.lock`: x64 takes i0-ship by merge). Merge clean; `git show --remerge-diff` empty [C].

Builds (release, clang-cl 19.1.7, LTO; 32-bit by `build_vp_clang.py` in a detached worktree, the instance-aware
variant by the route of `../06-tooling-and-workflows/headless-and-parallel-testing.md` 6.1b: `git archive` of the
commit, `git apply` of `ia-7307aefdf-440878983.patch` (clean), `update_commit_id.bat` replaced by a stub):

| Build | Commit | DLL size | DLL SHA-256 | Version string |
|---|---|---|---|---|
| `<pins_dir>/dlls/i0next-bbfacca1` | i0-ship-next bbfacca1a | 17,144,320 B | `F7C64840A0E5BCE890E68D468ABF7CBA94E8B43CD43ABC1131960CB850FD01AC` | `Release-5.4.8-30-gbbfacca1a Clean` |
| `<pins_dir>/dlls/i0next-bbfacca1-ia` | bbfacca1a + instance-aware instruments | 17,476,608 B | `E589306EFF23821EF7C509D1EDB95FC7666D1680258950FFDBB3981FFB0C5C54` | `Release-5.4.8-30-gbbfacca1a+ia440878983 Copy` |
| `<pins_dir>/dlls/x64next-76992d23/bin` | x64-next 76992d235 | 23,617,536 B | `D6E4F88144094EADBBAAC65CE593A8C63156BDD7499256B76A3A1C54CF364BD0` (server.exe `2C52540670F59C086F10E44A5677D160606D36D25044B58211E88942B28D7D18`) | `Release-5.4.8-40-g76992d235 Clean` |

The x64 build: `server/build_server.py --src <x64-next worktree> --unlocked --offline --jobs 6`, release,
2026-11-11 03:57-04:06 EST: every `build_all.json` step (stubs, cvgamedatabase, cvlocalization, dll, host, smoke) rc 0,
0 unresolved, narrowing gate 794 (baseline 798, 0 new), then the server built; **the proxy step failed** only because
`proxy/build_proxy.py` checks `--src` against `deps/dll.lock` and `build_server.py` does not pass `--unlocked` on to it
[C, build log]. The proxy is not needed headless; a proxy for x64-next needs that pass-through (or the pin moved). *Both done by
one-off 9029, 2026-11-11: the pin moved, and `build_server.py` now passes `--unlocked` on to the proxy build
(`build_proxy_step(..., unlocked=)`, check 6 of `proxy/tools/test_strictness.py`, 45/45 with the two new checks).* The pin builds are in
section 6.
Hashes of every file: `SHA256.txt` beside `bin`. A first attempt at 01:50 (four builds at once with the 32-bit
ones) ran out of commit memory ("LLVM ERROR: out of memory", paging file too small) and was discarded.

Bisect builds (not for use by other teams, under `D:/c5-64bit-work/9020/dlls/`): `s01`-`s05` (group 1, one per commit),
`s09-051c5503` (group 2 end), `s12-dc5e2291` (group 3 end), the `-ia` variants of s09 and s12, `s06`, `s13`, `s15`, `s16`.

## 2. The batch, fix by fix

Bisect by group, then by single commit where a group changed something (cumulative builds `sNN` = the branch after
its NN-th commit; 2026-11-11 02:20-04:36 EST; method in 2a). "Game" = does a game played from the same start give
the same `stats.db` as with the pin; "save" = does a load-and-resave of the same input give the same bytes as the pin
(version string excepted). First differing turn in brackets.

| # | Upstream (pull request) | What | 5.4.8 has the bug? | Taken | Conflicts | Changes save bytes | Changes game results | Evidence |
|---|---|---|---|---|---|---|---|---|
| 1 | 5b7606f86 (13548) | units deleted by city capture read afterwards | yes (`kill(false)` deletes at once, CvUnit.cpp:2514 at ccfd6dd4d) | yes, b9414f9d6 | none | no (group 1: all three resaves identical) | group 1 changes the huge continuation (turn 372, `MilitarySummary.WarCount`, `InstantYields` 'Won Battle'); tiny, standard, both cont. games identical. Single-commit bisect: **commit 1 alone** (pin vs s01 differs from 372, s01 = s02 = s03 = s04 = s05). In the pin, the check after the attack read the deleted garrison's freed memory, which still looked alive, so the tactical AI took the kill as failed [I from the upstream report, L that this commit moves the game] | cmp1b vs cmp-s05 |
| 2 | cae30372d (13550) | log format strings; barbarian camp spacing read `vValidPlots` after clearing it; spaceship log read a scrapped unit; `_strdup` freed with `delete[]` | yes | yes, 3c835840e | none | no | no (s01 = s02 on every rerun game) | cmp-g1 |
| 3 | 7c9a724ff (13481) | city order queue corruption on a free building | yes | yes, ee0948b56 | none | no | no (single-step bisect: s02 = s03 = s04 = s05) | cmp-g1 |
| 4 | dcc703818 (13472) | counterspy missions blocked by the offensive-mission cooldown (game lock) | yes (condition from c4277d5, in 5.4.8) | yes, 36dbb08bc | none (also one VP text row, not part of the DLL) | no | no (single-step bisect: s02 = s03 = s04 = s05) | cmp-g1 |
| 5 | 4a5f7e841 (13466) | container sizes cast before subtracting | yes (UB only; every compiler gives -1) | yes, 2d0faba37 | none | no | no (s04 = s05) | cmp-g1 |
| 6 | 9a52f6975 (13530) | saved members without a starting value; unit name returned from a temporary | yes | yes, eac0390e9 | none | no (s06 resave of the huge input = s05's, testbed job 20261111-044518-5c3b) | no (group 2: every game identical to group 1) | resave, cmp-s09 |
| 7 | 30a2349ed (13527) | espionage AI per-civ vectors appended after the defaults on load | yes | yes, 5a36cfbf8 | none | **yes**: every resave of the pin is 39,424 B longer than the new build's (= 64 players x 7 vectors x 22 x 4 B, the stale copies); the new build writes each vector at its 22 entries. The few short differing runs before the insertion (huge body 14,970,476 on) also come with this commit (s06 = pin, s09 differs) [L] | no in these games (group 2 identical on every game, the continuations included: no spy history yet at 25/50, none read in 350-400) [L] | resave |
| 8 | 42b5034eb (13528) | research turns left in 64 bits | yes (int overflow above 214,748 beakers) | yes, 4782a6276 | none | no | no (group 2) | |
| 9 | eb13c66a9 (13529) | real population in 64 bits | yes (wraps from population 183) | yes, 051c5503d | none | no | no (group 2; demographics only) | |
| 10 | d49ce8fc7 (13447) | per-turn map state log kept until shutdown | yes (only with `MOD_LOG_MAP_STATE`) | yes, d0e9c3ccb | none | no | no (group 3) | |
| 11 | 536232ddb (13496) | tactical sim copies at parent capacity | yes | yes, 4ac6f1185 | none | no | **no** (group 3 identical on every game: "sim results unchanged" confirmed) | cmp-s12 |
| 12 | 71de947eb (13421) | free the scratch state of expanded tactical positions | yes | yes, dc5e22917 | none (combines with b9311e2ee, section 3) | no | **no** (group 3) | cmp-s12 |
| 13 | c0188334e (13462) | fresh water depended on plot update order | yes | yes, 80a074c6f | none | no (`m_bIsFreshwater` is not saved) | **yes, after a load only**: standard continuation from turn 50 differs from turn 51 (`MilitarySummary.RecNavyOffensive`); new games and the tiny continuation identical | cmp-s13 vs cmp-s12 |
| 14 | c094e12f0 (13465) | visibility player set on every machine | yes (MP only) | yes, c00498108 | none | no | no (s13 = s15 on every game; autoplay has no human with units) | cmp-s15 |
| 15 | ff578c1a3 (13465) | delayed visibility flipped the same way on every machine | yes (MP; SP changes only for a human's units removed outside its turn) | yes, 272e94ba3 | none | no | no (s15) | cmp-s15 |
| 16 | e862ebfa2 (13494) | air strike defence roll seeded with the attacker | yes (since upstream 11828, in 5.4.8) | yes, a5f53048d | none | no | **yes**: huge continuation from turn 369 (`MapUnitsState`, then `MilitarySummary` 370; s15 vs s16); standard 0-200 identical | cmp-s16 |
| 17 | 6cc672569 (13474) | cached trade route counts reset with the routes; own copy for the UI thread | yes | yes, bbfacca1a | none | no | **yes**: standard 0-200 from turn 143 (`MilitarySummary.Danger`, then plots 144, units 146; s16 = pin, head differs) and the huge continuation from turn 359. The counts are now reset when a route is cleared and before either player's connections are valued, so yields that read them change [C source] | cmp-s16, cmp1 |
| - | 88400faf4 (13555) | CvLeague copy with dead Lua links | **no** (code after 5.4.8) | no | (would conflict) | - | - | section 3 |
| - | 9b3da47bb (13488) | stale area-effect cache index | **no** (code after 5.4.8) | no | (would conflict) | - | - | section 3 |
| - | aa7a29b38 (13539) | ClearHexHighlights on maps wider than 181 | mod Lua, not the DLL | no | - | - | - | section 3 |

Summary against the pins (head bbfacca1a, 32-bit, logging on):

| Game | pin vs head | where it comes from |
|---|---|---|
| tiny 0-100 | identical (30,553 rows) | - |
| standard 0-200 | differs from turn 143 (`MilitarySummary.Danger`, then plots and units) | commit 17 (6cc672569) alone |
| huge 350-400 | differs from turn 359 (`MapUnitsState`) | commit 1 (from 372), commit 16 (from 369), commit 17 (from 359) |
| tiny continuation 25-100 | identical (27,044 rows) | - |
| standard continuation 50-200 | differs from turn 51 | commit 13 (fresh water) alone |
| resave tiny T25 / standard T50 / huge T350 | differ: 39,424 B shorter, plus a small region before it | commit 7 alone (30a2349ed), testbed bisect |

## 2b. Save test and determinism on the new builds [L]

- **Save test (D24), new x86 vs new x64**: `run_d10.py --x86-dll <pins_dir>/dlls/i0next-bbfacca1/... --srv-bin
  <pins_dir>/dlls/x64next-76992d23/bin --huge-save D:/c5-64bit-work/9020/inputs/9020-HUGE_0350.Civ5Save
  --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, DLL AI logging **off** (the tool's default since D49),
  2026-11-11 04:07:48-04:29:56 EST: **PASS** on tiny 0-100, standard 0-200, huge 350-400; part 1 identical on every
  input with only the version-string exception; every round trip byte-identical; part 2 identical (tiny 30,666 rows,
  standard 351,478); part 3 gates identical. Report: `tools/d10/evidence/9020/d10-new-report.txt`. (The detector of D18
  by ancestry looks in the owner's checkout, which lacks the new commits, hence `--d18-* yes`.)
- **Determinism, 32-bit** (logging on): the head build played tiny 0-100 and standard 0-200 three times, the third
  with `--heap-perturb 7` (DLL rebased, random heap spacers): all three identical per game (30,553 and 349,633 rows)
  [cmp1, 02:20-02:27 EST]. Bisect builds with different heap seeds also played identical games (huge 350-375,
  instrumented pin build, seeds 12/14/15).
- **Determinism, x64** (logging on): x64-next server played tiny 0-100 and standard 0-200 twice each: identical
  (30,553 and 349,633 rows, the same counts as the 32-bit head) [x64runs, 04:07-04:12 EST].
- **x64 narrowing gate**: 794 sites against the baseline 798, 0 new, 4 gone; 0 unresolved; smoke run exit 0
  [C, `build_all.json` of the x64-next build].

## 2c. Tactical-memory growth (commits 11-12, with b9311e2ee) [L]

Quiet testbed `oldpc` (job 20261111-045727-85e4, 2026-11-11 04:59-05:39 EST, 2 runs at once, not a quiet job: memory
counts, not times), c5h, huge save `TB-HUGE_0350` (= `c499d3c6...`), CP + VP, 350 -> 375, `VP_MEMIMPORTS=0`, logging on,
instance-aware (instrumented) builds, one `--heap-perturb` seed per run (11-14), arms interleaved and rotated per
seed; per-turn values from the instruments' `Mem*` tables at each turn's end, slopes by least squares over 351-375.
Seed-paired differences (the seed sets the heap spacers, so levels depend on it), mean, sd and 95 % CI (t, n = 4).
Script `tools/d10/evidence/9020/scripts/growthjob.py`, readout `growth_readout.py`, data and both readouts
`tools/d10/evidence/9020/growth/`.

**Isolating the two commits**: `s09-ia` (groups 1-2) against `s12-ia` (+ d49ce8fc7, 536232ddb, 71de947eb). Both play
the same game (group 3 changes no result, section 2), so every difference is memory handling:

| Metric (c5h process / DLL) | s09-ia | s12-ia | s12 - s09, paired (95 % CI) |
|---|---|---|---|
| committed MB at turn 375 | 383.5 | 370.6 | **-12.9 (+-8.9)** |
| committed MB, highest turn end | 395.9 | 379.1 | **-16.8 (+-8.1)** |
| heap free-list MB at 375 (freed, not returned) | 39.8 | 31.0 | **-8.8 (+-8.3)** |
| heap free-list MB, highest | 48.6 | 43.1 | -5.5 (+-3.9) |
| heap busy MB at 375 | 208.8 | 209.0 | +0.2 (+-0.2) |
| heap busy blocks per turn | 140.1 | 149.9 | +9.8 (+-1.5) |
| DLL live MB per turn | 0.090 | 0.053 | -0.037 (identical in every run) |
| DLL live MB, highest turn end | 142.9 | 147.6 | **+4.6** (identical in every run) |
| TacticalAI live MB, highest turn end | 4.86 | 9.12 | +4.26 (identical in every run) |
| largest free block MB | 1927.4 | 1924.6 | -2.8 (+-8.1), not resolved |

Why the numbers moved [I from source, L numbers]: 536232ddb makes every copy-on-write child reserve its parent's
capacity (255 plots for the plot table), so freed copies have one size and are reused instead of piling up on the
heap's free lists in many size classes; the free-list and committed reductions are that effect, on the Windows CRT
heap (upstream measured it under Wine/Proton, where it is far larger). 71de947eb frees the scratch state of expanded
positions during a search and keeps the buffers on spare lists until the next hard reset; that is the +4.3 MB of
TacticalAI live memory at some turn ends (the spare lists still hold their buffers when the turn ends) and the small
rise in blocks per turn. The DLL's live bytes per turn fall slightly. In this 25-turn window the net effect on the
32-bit process is about 13-17 MB less committed memory and about 6-9 MB less freed-but-held heap. Peak memory
*inside* a tactical search was not sampled (the instruments write once per turn).

**The whole batch against the pin** (`pinia` = `i0ship-ccfd6dd4-ia`, `headia` = `i0next-bbfacca1-ia`; different games
from turn 359 on, so this mixes game and memory changes): committed at 375 -9.7 MB (+-9.0), committed highest -5.6
(+-8.2, not resolved), heap busy per turn -0.048 MB, DLL live MB per turn 0.089 -> 0.047, DLL live highest +4.6 MB,
TacticalAI highest 4.9 -> 9.1 MB. Epic 1300's measure (DLL live blocks per turn) is not moved: 1,079 -> 1,089 blocks per
turn on this save (s09 -> s12). Readouts: `growth/s09ia-vs-s12ia.txt`, `growth/pinia-vs-headia.txt`.

## 2a. How the comparisons were run

All 32-bit runs: civ5-headless (c5h) as a tool through `tools/d10/run_d10.py`'s `Hosts` (same command line as the save
test: `--no-profiles`, `--detail summary`, toolkit game folder, `VP_MEMIMPORTS=0`), **statistics logging on and the
DLL's own AI logging on** (the hosts' default before decision D49; the same in every arm), driven by a one-off runner
(`tools/d10/evidence/9020/scripts/cmp_runs.py`). Games, each played by every build:

- tiny: Continents Tiny fixed seed, new game 0 -> 100, save at 25, no mods;
- standard: Continents Standard fixed seed, 0 -> 200, save at 50, no mods;
- huge: `9020-HUGE_0350.Civ5Save` (a copy of `E2000-HUGE_0350`, SHA-256 `c499d3c6...88c3`), 350 -> 400, CP + VP from
  `git archive 7307aefdf`;
- tinycont / stdcont: the **pin's** tiny turn-25 and standard turn-50 saves, continued to 100 / 200 (a game after a
  load, the same input for every build);
- resave: those two saves and the huge input, each loaded and saved at once (`tools/i0/save-now.lua`).

Behaviour: `tools/d10/statscmp.py` (every table and column, rowid order, plausibility checks). Save bytes:
`tools/d10/savecmp.py` with no order exception (both sides carry D18), only the version-string exception.

## 3. Facts found on the way

- **Left out because 5.4.8 does not have the code they repair [C, `git grep` at ccfd6dd4d, `git merge-base`]:**
  88400faf4 (upstream pull request 13555) adds a copy constructor to `AutoInvalidatedPointerBase` in `CvStructs.h`;
  that struct and the CvLeague Lua pointer list came with upstream dae4c612e (13489, 2026-11-09), after 5.4.8; at
  ccfd6dd4d `CvStructs.h` ends at line 238 and no file names `m_PointerList` or `AutoInvalidatedPointer` (the
  cherry-pick conflicts for that reason). 9b3da47bb (13488) resets `m_iAreaEffectCacheIndex` in
  `CvPlayer::UpdateAreaEffectUnits`; the cache index came with upstream 87e3d4ab0 ("Full Recon Rework", not an ancestor
  of Release-5.4.8); 5.4.8's `UpdateAreaEffectUnits` (CvPlayer.cpp:47230 at ccfd6dd4d) clears and refills four
  plain lists and keeps no per-unit index, so the bug cannot occur.
- **aa7a29b38 (13539) left out: it is mod Lua only** (seven `.lua` files of Community Patch, Vox Populi, EUI
  compatibility, Squads and UI_bc1); no DLL source changes, and the project ships the 5.4.8 release's mod files. The
  crash needs a map wider than 181 plots; no project test uses such a map. *Corrected 2026-11-17 (ticket 0462):*
  was "which the game reaches only with an EXE stack patch (the pull request says so)"; the stock EXE loads any shape
  up to about 20,000 plots, 198 x 101 included (`../02-exe-internals/allocations-and-memory-map.md` 6.1, 9073), so
  widths 182-198 are reachable unpatched; the stack patch is needed only past about 20,000 plots. Product path and
  recommendation: `upstream-lua-fixes.md` section 4. It belongs to the mod-file line if the project ever ships changed mod
  Lua (installer decisions D26/D27).
- **dcc703818 (13472)** also changes one English text row in the Vox Populi database XML; that file is carried on the
  branch with the commit but is not part of the DLL and is not installed by the project. The condition it changes
  (`GetNumTurnsSpyActiveMissionsBlocked`, CvCity.cpp:4618 and 5477 at ccfd6dd4d) came with upstream c4277d5, which is
  in Release-5.4.8 [C].
- **6cc672569 (13474) makes the DLL call `ICvEngineUtility1::TryGameCoreLock`, which 5.4.8 never calls** [C: no call
  site at ccfd6dd4d]. Both hosts serve it: the x64 server (`server/src/srv_ifaces.cpp`, `TryEnterCriticalSection`;
  remote mode `server/src/remote/srv_remote_objects.cpp`) and c5h (`HHEngineServices.cpp:355`, always true).
  *Done by one-off 9029, 2026-11-11:* the only call site at 76992d235 is `CvPlayerTrade::UpdateTradeStats`
  (`CvTradeClasses.cpp:4665`), on a thread that is not the game-core thread and does not hold the lock; it releases
  with `ReleaseGameCoreLock` when it took the lock, and keeps the old counts when the try fails [C source]. In remote
  mode that is a call from the UI channel (outside `ICvGame1::Update`), answered on the server by the same critical
  section as `GetGameCoreLock`: it never crosses to the proxy, so the proxy does nothing with it [C,
  `srv_remote_objects.cpp` 78-84]. Row 438 of `proxy/model/policy-table.tsv` and the four `TryGameCoreLock` entries of
  `proxy/model/model.json` now say `server_local` (plumbing), `used_by_dll` true; `policy_counts_*` 44 -> 45
  server_local, 309 -> 308 unused. Neither field feeds the generated runtime (`gen_runtime.py` reads neither; the
  schema hash covers interface signatures only), so no generated file changes [C]; the edit was made by hand because
  `classify.py` scans a fixed local fork checkout (`toolchain.FORK`, pinned e9d0c4837), not the pin. One asymmetry for
  the proxy team [C, I that it cannot happen today]: in remote mode `GetGameCoreLock`/`ReleaseGameCoreLock` do nothing
  while `t_bSuppressLock` is set (canonical object creation), `TryGameCoreLock` does not check it; a try inside that
  window would enter the critical section and the release would not leave it. The window only calls `GetGame`,
  `GetPlayer` and similar getters, none of which reaches `UpdateTradeStats`. (Replaced: "the proxy team must re-check
  that row".)
- **SAFE_FREE** used by cae30372d exists in 5.4.8 (`FirePlace/include/FireWorks/FFireTypes.h:332`, a template, not
  a macro) [C].
- **The tactical-memory pair and the project's b9311e2ee combine without conflict** [C, source read]: b9311e2ee frees
  each position's `childPositions` in `wipeDerived` on a hard reset (`CvTactPosStorage::reset(true)`, when no position
  is in use); 71de947eb's `releaseScratch` reads `childPositions` during a search (to see which fields a child still
  borrows) and its hard reset also clears the spare-buffer lists. 536232ddb only changes how a child copies its
  parent's vectors (capacity reserved first). Applied in upstream order (536232ddb, then 71de947eb), both
  cherry-picked cleanly onto ccfd6dd4d. Their effect on growth is measured in section 2 (growth row).
- **a946ceb2a (13562, clang Release DLL crashing on mod load): the project's builds are not affected** [C source, L
  runs]. The bug was in upstream's CI script `build_vp_clang_sdk.py`, which passed the VC9 and SDK headers with
  `-external:I` and so put VC9's `vadefs.h` ahead of clang's (VC9's `va_start` breaks inlined `CvString::format`).
  The project builds the 32-bit DLL with `build_vp_clang.py`, which takes the VC9 headers from `%INCLUDE%` (set by
  `vsvars32.bat`, `VS90COMNTOOLS`; only the project folders go through `/I`, build_vp_clang.py:52-61 and 316-317 at
  ccfd6dd4d), and clang-cl searches `%INCLUDE%` after its own headers: the reviewer on that pull request states the
  same for `build_vp_clang.py`. Every release build the project made loads the mods and plays (the d10 huge runs with
  CP + VP, the live instances of 1305 and 9014). The x64 build (`tools/x64-dll/build.py`) uses MSVC 14 headers, not
  VC9's. Nothing to take; the commit changes only upstream's CI files.
- **Open upstream pull request 13522 (one shared, read-only zero block for empty 2D database tables)**, head 85b2e7bc4
  on base ac3b46ec2 (after 5.4.8): applies to ccfd6dd4d without conflict (`git merge-tree`) [C]. Writer check on 5.4.8
  (script over every `.cpp` of `CvGameCoreDLL_Expansion2`, regex for `member[..][..] =`, `+=`, `-=`, `++`, `--` on the 55
  member names the pull request shares): 96 writes, all inside `CacheResults` functions, the same count as on the
  pull request's base [C, regex; writes through a returned row pointer are not covered by the regex, the pull
  request's own check says the 22 row-pointer getters only read]. So the premise holds on 5.4.8. Not taken (the MC's
  brief). What a measurement would take with the project's tools: build ccfd6dd4d + 85b2e7bc4 (plain and `-ia`),
  load the huge save in c5h with `VP_MEMIMPORTS=0` and read `MemDiagnostics`/`DLL live MB` at the first turn and
  after 50 turns with `tools/i0/memgrowth.py`, A/B against the pin `-ia` build; then one live load on a Goldberg
  instance with `addrspace.py` for free address space and the largest free block (what upstream's tester measured:
  +46-48 MB free and +53-56 MB largest block right after a load, the gap gone after 15-30 turns as the CRT heap
  reused the freed blocks); plus `run_d10.py` to show no behaviour change. Risk: low for game state (values are read
  identically; the writer check holds), real for crashes if any later code writes a shared table (the pages are
  read-only, so it faults at once instead of silently corrupting); none for the x64 server, where address space is
  not short. Its benefit is mostly for large mod sets (upstream: 41 MB with VP alone, 389 MB with 25 mods); with
  the project's VP-only set, about 45-55 MB less committed at load, by upstream's measurement [I for this project].

- **The long x64 runs "stall" at a game's end, not in a defect** [L, 2026-11-11]. x64-next in the server: standard fixed
  seed 0 -> 500 stopped at turn 310, the huge save 350 -> 450 at turn 408, both `FAILED: turn does not advance
  (max_updates_per_turn)` (2,000 updates without a new turn; at 408 within 57 ms of the turn start). Reproduced on the
  quiet testbed at the same turns (job 20261111-043759-0c90; standard 0 -> 320 and huge 350 -> 410), where the pin x64
  build c43ea1b9f passed both ranges. `stats.db` `GameResult` of the stopped runs: **Cultural victory** (standard: Japan,
  turn 310; huge: Egypt, turn 408). After a victory the game turn never advances, and `server/src/srv_main.cpp`
  `PhaseTurnLoop` counts that as a failure. The pin passes those ranges only because its games diverge earlier (the
  batch changes game results, section 2) and no victory falls inside them. Server-side gap: the turn loop should stop
  with "game over" when the game state is over. *Fixed by one-off 9029 (bug 0094), 2026-11-11, corrected the same day:*
  **what a real victory leaves [C, `CvGame.cpp` 7492-7504 at 76992d235]**: `CvGame::setWinner` (called when a team
  wins) switches AI autoplay off (`setAIAutoPlay(0, winner's leader)`) and sets **`GAMESTATE_EXTENDED`** when autoplay
  was on (or the engine's autorun is on), `GAMESTATE_OVER` only otherwise. Every headless run is in autoplay, so a
  victory there leaves state 2 (extended), `GetWinner()` the winning team, `GetVictory()` the victory type and
  `GetWinningTurn()` the elapsed turns; the turn never advances again (the winner's leader is now the active,
  human-controlled player), and further Updates play the stopped turn on (9034 saw partial statistics rows for the
  victory turn after it) [L 9034, r2-small-x64, Cultural victory at 367]. **How civ5-headless decides [C,
  `host/setup/HHNewGame.cpp` 1266-1270 and `python/civ5headless/run.py` 617-621]**: its autoplay loop stops after any
  Update that leaves `GetGameState() != GAMESTATE_ON` ("game state %d at turn %d"), and the run's `endReason` is
  "victory" when `Game.GetWinner() >= 0`. **The server now does the same**: after every Update of its turn loop it reads
  the game state; any state other than `GAMESTATE_ON` ends the loop at once with "GAME OVER at turn N: winner team W,
  victory type V, winning turn T (elapsed), game state S" and `result: game_over; turns S -> N`, exit 0, and no idle
  Updates follow. A turn that does not advance while the game is on is still the failure "turn does not advance
  (max_updates_per_turn)". (Replaced: the first fix of 14:30, which checked only `GAMESTATE_OVER` and only after
  `max_updates_per_turn`: it passed the probe test, which forces `GAMESTATE_OVER`, and missed every real autoplay
  victory.) `run_server.py` prints "game over at turn N"; `run_d10.py` reports such an x64 run as "result game_over;
  ended at turn N" (still a failure there: the save test needs its target turn). Test `server/tools/test_game_over.py`:
  section 6. Before this was established, the x64 runs to turn 500 / 450 were the
  "longer AI run" of the brief: 310 and 58 turns played without a crash, 0 Lua errors.

- **The save that grew on every load was the espionage AI** [L]. `../06-tooling-and-workflows/d10-save-identity-test.md`
  section 1 records that a second load-save of tiny T25 inserted 39,464 bytes at body 2.52 MB (both DLLs). 39,424 of
  them are commit 7's stale vector copies (64 x 7 x 22 x 4 B). With the new build, one hop against two hops of the
  same input (the save test's `resave` and `roundtrip/via-x86` files, `D:/c5-64bit-work/9020/d10-new`) differ in the
  body by **40 bytes** only (tiny T25, T100, standard T50, T200), plus the pregame's stored path. So a load-save is
  still not a fixed point, but the remainder is 40 bytes per hop; what they are was not looked at.


## 4. Not verified

- The pin DLL loading a save written by the new builds (the new builds load the pin's saves: the resaves of section 2).
- ~~Nothing live: no game instance, no EXE, no proxy~~ (2026-11-11, one-off 9029: the proxy built, verify mode
  headless and live on Tiny and huge with 0 mismatches, the policy row corrected; section 6). Still not exercised:
  whether the live game ever reaches `TryGameCoreLock` while the core holds the lock (no counter for it; not measured).
- The crash fixes were not shown to remove a crash in this project's games: none of these crashes was met in any
  run of either build (they are upstream reports: a strict allocator, a free building during `popOrder`, the counterspy
  focus lock, maps wider than 181).
- The single-commit bisect covered the games each group changed (standard, huge, the standard continuation); the
  tiny games and the tiny continuation were not rerun per commit (no group changed them).
- Commit 6's starting values only matter for a new map or a fresh player; no direct test showed a byte or result
  difference from it (its group shows none).
- The 40 bytes a second load-save still adds (section 3).
- Growth: 25 turns, one save, 4 seeds per arm, turn-end samples only; in-search peaks, longer games and the GameId 62
  map were not measured; the x64 server's memory was not measured.
- 32-bit comparisons ran with the DLL's AI logging on, the save test with it off (D49); A/A and the comparisons hold
  within each setting, not across them.
- Whether `TryGameCoreLock` behaves the same in the real EXE as in the hosts (the UI thread keeps its previous counts
  when the lock is busy): upstream measured it in the real game; not measured here.

## 5. Recommendation for the pin move

MC reading for the MC, not a decision [I]. *Carried out by one-off 9029 on 2026-11-11: section 6.*

- **When**: at the epic boundary D46 names (after epic 2800 has merged and been verified), both pins in one change:
  `deps/i0.lock` -> `i0-ship-next` bbfacca1a (a fast-forward of `i0-ship` from ccfd6dd4d; fork `master` follows by
  fast-forward, D46), `deps/dll.lock` -> `x64-next` 76992d235 (a fast-forward of `x64`; fingerprint from
  `tools/x64-dll/src_fingerprint.py`). The builds named in section 1 are the ones to pin; the save test between them
  passed (2b).
- **How**: fast-forward the two fork branches (no new commit), update both lock files with the new describe strings
  (`Release-5.4.8-30-gbbfacca1a`, `Release-5.4.8-40-g76992d235`), the hashes of section 1, and the `-ia` build
  `i0next-bbfacca1-ia`; then build the proxy against the new x64 pin (it failed here only on the lock check).
- **What every team must redo afterwards**: (1) every baseline of game results: the standard fixed-seed game differs
  from turn 143, the huge continuation from 359, a continued standard game from turn 51 (stats row counts in
  `d10-save-identity-test.md` and earlier evidence are for the old game); (2) the performance team's turnbench and
  testbed baselines (different code and different games; `dll-pin-ia` base on the testbed, instance DLLs of c5h-a..e
  and tb1/tb2 provisioned from the old `-ia`); (3) proxy verify-mode and live parity baselines (epic 2700/2800 runs);
  (4) the pin names in `docs/briefs/COMMON.md` rule 18, `docs/briefs/PERFORMANCE.md` and the tools' defaults;
  (5) `proxy/model/policy-table.tsv` row 438 (`TryGameCoreLock` is now called); (6) any comparison of save sizes
  (saves written after a load are 39,424 B smaller). Saves stay loadable both ways by design (the format is unchanged:
  the new loader reads the stored count) but **the old DLL loading a new-build save was not tested** (section 4).
- **Independent of the pin**: the server's turn loop should treat a finished game as the end of the run (section 3).
- **Not to take with it**: upstream pull request 13522 (section 3) until it has been measured as described.

## 6. The pin move (one-off 9029, 2026-11-11)

Done as section 5 recommends; tracker `tracker/issues/9029-move-the-dll-pins-to-upstream-fix-batch-1.md`.

- **Fork [C]** (14:04 EST, `git merge-base --is-ancestor` checked before each push, plain fast-forward pushes): `i0-ship`
  ccfd6dd4d -> bbfacca1a, `x64` c43ea1b9f -> 76992d235, `master` ccfd6dd4d -> bbfacca1a (D46). Fork pull request 5
  shows merged (its head reached its base). Untouched: `instant-yield-triggers` (based on an older upstream commit,
  not on the pin), the upstream-tracking branches, `deterministic-order` (fork pull request 4, the owner's review), and the
  test branches `i2-shell-test` b92373aeb and `i2-white-mask` 66b0b2bdc: **both are based on ccfd6dd4d and must be
  rebased (or merged) onto bbfacca1a by epic 3200** before their DLLs count as pin builds.
- **Builds [C]**: 32-bit `<pins_dir>/dlls/i0ship-bbfacca1` (`f7c64840...01ac`) and `i0ship-bbfacca1-ia`
  (`e589306e...5c54`), copies of section 1's builds under the folder names the tools derive from the lock; x64
  `<pins_dir>/dlls/x64-76992d23` with `bin/` (DLL `fa75f483...8cec`, server.exe `a1d7bdd6...803b`, replaced
  2026-11-11 16:0x by `3a35330a...4ccc` with the corrected bug 0094 fix; the save test and verify runs below used
  `a1d7bdd6`, whose turn loop differs only after a game has ended) and
  `proxy/bin/` (`96d4521e...1ff7`), built by `build_server.py` from a git archive at 76992d235, release, 570 s + a
  54-s server/proxy rebuild at repository commit 887e89dc2 (clean). The x64 DLL is not byte-identical to section 1's
  `x64next` build of the same source (23,618,048 against 23,617,536 B; built with newer tools of this repository);
  both play the same games (the save test's rows equal 9020's).
- **Save test [L]**: PASS, 14:22-14:40, 1,092 s, AI logging off in both arms; the new references are in
  `../06-tooling-and-workflows/d10-save-identity-test.md` section 4d (standard 351,478 rows, huge 683,123).
- **Proxy verify mode [L]**: headless Tiny 0-50 strict, `local_answers=1 local_verify=1 lua_mirror=1
  test_slot_poll=4`: PASS, **1,300,212 getter answers and 1,198,941 UI-Lua hits verified, 0 mismatches, 0 lock
  errors** (14:40-14:41, `server/evidence/9029/headless/`); the same counts as 9028 on the old pins (Tiny plays the
  same game on both). Live on c5h-a (proxy layout from `x64-76992d23`, fallback the new `-ia` DLL), `local_answers=1
  local_verify=1 lua_mirror=0`, checklist with 10 AI turns, every step pass, no step skipped, start-up watch ok:

  | Run | Save | Getter answers verified | GetScriptSystem verified | Mismatches | Lock errors |
  |---|---|---|---|---|---|
  | T-verify, 14:42-14:49 | `c5h-9029-tiny-t0008` (a copy of a c5h-a save of 2026-11-10 20:54, written before the move), turns 8 -> 19 | 4,628,556 | 48,931 | 0 | 0 |
  | H-verify, 14:49-14:59 | `C5H-VP8P-HUGE_0350`, turns 350 -> 360 | 2,658,310 | 1,049,483 | 0 | 0 |

  Evidence `server/evidence/9029/live/`. Each server report ends "the engine is gone ... PeerDied": the harness's stop
  by terminate after its settled-save check (as in 9028). This is also step 7's live load of the huge save through the
  proxy on the new pin.
- **Old-pin saves load in the new builds [L]**: T-verify above (live, through the proxy); a huge turn-360 save written
  by the old x64 pin through the proxy (9028's `c5h-c5h-c-t0360-122239`, copied as `9029-oldpin-H360`, `080fbe61...`)
  loads in the new server and plays 360 -> 362 (`run_server.py`, 29 s, `result: passed`); the 32-bit continuations of
  section 2 (pin saves continued by the new DLL). Still not tested: the old pin DLL loading a save written by the new
  builds (section 4).
- **Bug 0094, real victory [L, 2026-11-11 15:47-16:05]**: the Standard fixed-seed game (`server/specs`), new game,
  AI logging off, on the pin DLL with the corrected server: Cultural victory of team 6 (Japan) at turn **281** (`GameResult`
  row "Cultural", turn 281); the server ends "GAME OVER at turn 281: winner team 6, victory type 3, winning turn 281,
  game state 2", `result: game_over; turns 0 -> 281`, exit 0, 357 s; no statistics row after turn 281; no idle Updates.
  The victory came in the Update that started turn 281 (0 Updates of that turn played). (One-off 9020 saw this game end
  at turn 310 with AI logging on; why the two runs end at different turns was not looked at [I: other settings of that
  run].) `server/tools/test_game_over.py` now has a real-victory case from a save at turn 278 of that game
  (`server/evidence/9029/0094/9029-STD-T278.Civ5Save`, `feee3407...0f15`), continued to 330: **17/17 checks on the
  corrected server; 8/17 on the first fix** (`a1d7bdd6`: "turn does not advance", exit 2, 10 idle Updates, the same
  failure 9034 met at turn 367), the probe and control cases pass on both.
- **Drift tool [L]**: `tools/drift/run_pair.py --spec tiny --turns 100` with its new defaults (the pin builds):
  x86 = x64, 30,666 rows in 15 tables, no differing turn (15:0x).
- **Instances**: `../06-tooling-and-workflows/headless-and-parallel-testing.md` 6.1c (c5h-a, c5h-c on the new `-ia`; c5h-b,
  c5h-d, c5h-e not). Quiet testbed: `dll-pin-ia` now `base/dlls/i0ship-bbfacca1-ia` (old folder kept), tb1 and tb2 set
  to it and sealed (14:51-14:52); timing: `../08-performance/turn-time-baselines.md` section 9.

Sources: the upstream commits and their pull-request discussions (read 2026-11-11 with `gh`, read only); fork clone
`D:/c5-64bit-work/9020/fork`; evidence `tools/d10/evidence/9020/`.

## 7. Fix branches for the next pin move (bugfix team 4000, 2026-11-12)

Fixes of the project's own finding, made as ready fork branches for the batch after this one (charter rule 3, D44: they
reach the tests only when the MC moves the pins). Each bug has one commit on a branch cut from the i0 pin bbfacca1a,
cherry-picked with `-x` onto a branch cut from the x64 pin 76992d235 (no conflict, no hand edit) [C]. Tracker:
`tracker/issues/0096-*.md`, `tracker/issues/0084-*.md`. Tests and evidence: `evidence/4000/`.

### 7a. Bug 0096: empty notification for a civilian killed instead of captured (upstream bug)

- **Code [C]**: `CvUnit::setXY`, `CvUnit.cpp` 20435-20436 at both pins (20565-20566 at upstream master 16596561f of
  2026-11-11, read with `gh api`, so upstream still has it). In the branch where an enemy combat unit enters a plot with
  a civilian that is not captured (no capture class, e.g. a Great Person, or embarked without `CaptureWhileEmbarked`),
  `Localization::String strMessage = ...; Localization::String strSummary = strMessage;` declare new variables that
  hide the ones declared before `if (bDoCapture)`, so `pNotification->Add(NOTIFICATION_UNIT_DIED, ...)` sends two empty
  strings to the civilian's owner. Only human players record notifications (`CvNotifications::Add` returns at once for
  an AI), so it shows only in a human's notification list. Text key `TXT_KEY_UNIT_LOST` = "A Unit was Killed!" (base
  game `CIV5GameTextInfos.xml`).
- **Fix**: assign the outer variables (2 lines). Branches `i0-ship-fix-0096` 55d750754 (on bbfacca1a) and
  `x64-fix-0096` 511bf915d (on 76992d235).
- **Check [L, 2026-11-12 23:40 EST]**: `evidence/4000/0096_run.py` runs the probe `0096-civilian-killed.lua` in the x64
  server (`run_server.py`, Tiny fixed-seed new game, `--dll-log-flags 0`, one turn; the probe runs before autoplay is
  armed, while player 0 is still human). It creates a player-0 Great Scientist (no capture class) at (8,18) and a
  barbarian warrior at (8,19), orders the warrior onto it, and reads player 0's notifications. Pin build
  `x64-76992d23/bin`: `0096-RESULT FAIL empty notification: message [] summary []`; the same bin folder with the fix
  DLL (`D:/c5-64bit-work/4000/dll/bin-x64-0096`, DLL SHA-256 `9d888f37...990c`, release, `build_all.py`, every step
  OK): `0096-RESULT PASS message [A Unit was Killed!] summary [A Unit was Killed!]`; in both the scientist died and
  the warrior stood on (8,18). `MISSION_MOVE_TO` did not move the barbarian outside its own turn, so the probe moved
  it with `Unit:SetXY`, which is the same `CvUnit::setXY` a move ends in [C]. The 32-bit line is compiled only
  (section 7c); the source change is identical.

### 7b. Bug 0084: the statistics logger drops a row on a failed insert

- **Code [C]**: `SqliteLogger.cpp` (identical file at both pins): `Statement::execute` (line 509 at the pins; 503 at
  29f15ac38, where 9011 saw it) and `BatchWriter::flush` asserted "failed to execute insert" without the SQLite error and
  dropped the row; a batch began with `SAVEPOINT` (`Connection::BeginTransaction`) and ended with a `COMMIT` whose
  failure left the connection inside the transaction.
- **What the database layer does [C, `../../tools/x64-dll/cvgamedatabase/README.md`, same in the closed 32-bit
  library by its A/B]**: SQLite 3.7.17; every `Open` installs a busy handler that sleeps 2 ms per attempt and gives up
  after 30 attempts, so an insert that meets another connection's lock for longer than that fails with SQLITE_BUSY;
  `Connection::ErrorCode()` / `ErrorMessage()` are `sqlite3_errcode` / `sqlite3_errmsg`, exported by the closed 32-bit
  import library `CvGameDatabase/lib/CvGameDatabaseWin32.lib` (`?ErrorCode@Connection@Database@@QBEHXZ`,
  `?ErrorMessage@Connection@Database@@QBEPBDXZ`) and by the project's x64 library [C]. Statements are prepared with
  `sqlite3_prepare_v2`; since SQLite 3.7.0 a step after a failed step resets the statement itself and keeps its
  bindings (no `SQLITE_OMIT_AUTORESET` among the compile options), so stepping again repeats the whole insert [C, SQLite
  source and documentation; I that the closed library prepares the same way, by the A/B]. The DLL's release builds of
  both lines define `VPRELEASE_ERRORMSG`, so `ASSERT` writes to `CvAssert.log` in release too [C, `build_vp_clang.py`,
  `tools/x64-dll/build.py`].
- **Fix**: `SqliteLogger::ExecuteWithRetry` runs every insert and a batch's `BEGIN IMMEDIATE` and `COMMIT`. Success
  costs the same single step as before. On SQLITE_BUSY or SQLITE_LOCKED it sleeps 20 ms and steps again, up to 10
  times (after the busy handler's own waits); a statement that uses up its retries turns retries off for the rest of
  that game turn, so a long-held lock cannot stall every row. Every final failure goes to `CvAssert.log` as
  `SqliteLogger: <what>, row dropped (table T): SQLite error N: <message>` (the prefix `SqliteLogger:` that
  `run_d10.py` fails a run on is kept). Batches start with `BEGIN IMMEDIATE` instead of `SAVEPOINT`, so the write lock
  is waited for before the batch holds any lock (a deferred transaction holding a shared lock while it waits for the
  write lock can deadlock against another writer); a `COMMIT` that still fails is rolled back. Branches
  `i0-ship-fix-0084` 80b7495c5 (on bbfacca1a) and `x64-fix-0084` 7b56ef457 (on 76992d235).
- **First lock test: inserts alone are not enough [L, 2026-11-12 23:40-23:41 EST]** (`evidence/4000/0084_lock_test.py`,
  run `D:/c5-64bit-work/4000/runs/0084-a`, Tiny 0-30, 4,574 rows in the control arm; 6 lock windows of 700 ms per
  locked arm). Pin build with the lock: 20 rows missing in 5 tables (BuildingYields 12, HandicapYields 3,
  MilitarySummary 3, PolicyChoices 1, ReligionChoices 1); `CvAssert.log` "failed to execute insert" (509), "failed to
  execute batched insert" (601), "failed to create table" (228), "failed to prepare insert statement" (317). First fix
  build with the lock: every insert arrived, but **the whole BuildingYields table was missing** (599 rows): tables are
  registered lazily, once per session, on first use (`SqliteLoggerRegistrations.h`, a `static bool` per table), and
  that table's `CREATE TABLE` and insert prepare met the lock unretried. Also learned [C, `CvGameCoreUtils.cpp`
  `CvAssertDlg`]: with popups suppressed (headless, server) an assert site is logged once and then ignored for the
  session, so `CvAssert.log` shows only the first failure per site; a shared report function would show only the
  first failure of all. Without popups suppressed, a release build (`VPRELEASE_ERRORMSG`) shows the assert as a dialog
  in single player, as the old asserts already did.
- **Second commit** (i0 11de7fa61, x64 351780044): every statement of the logger retries (open pragmas,
  uuid_dictionary, `PRAGMA table_info`, `DROP`/`CREATE TABLE`, the insert prepare); a schema check that a lock kept
  from reading no longer counts as "table missing", so a locked database can never make the logger drop a table; the
  report is a macro, one assert per call site.
- **Lock test on the second commit: PASS [L, 2026-11-12 23:55-23:57 EST]** (run `D:/c5-64bit-work/4000/runs/0084-b`,
  Tiny fixed seed 0-60, x64 server, AI logging off; fix bin = the pin's `x64-76992d23/bin` with only the DLL replaced,
  `D:/c5-64bit-work/4000/dll/bin-x64-0084`, DLL SHA-256 `8d1d9df0...abea`, release `build_all.py`, every step OK, no
  warning in `SqliteLogger.cpp`). Lock 700 ms of every 1,000 ms from autoplay on, 15 windows (10.6 s locked) per locked
  arm:

  | Arm | Build | Lock | Rows | Wall | `SqliteLogger` asserts |
  |---|---|---|---|---|---|
  | control | pin | no | 12,429 | 20.1 s | 0 |
  | unfixed | pin | yes | 10,704 (**1,725 missing**: BuildingYields 1,690 = the whole table, BuildingsOverview 16, MilitarySummary 17, MapPlotsState 2) | 27.5 s | 4 sites: create table (228), prepare insert (317), insert (509), batched insert (601) |
  | fixed | fix | yes | 12,429 | 29.8 s | 0 |
  | fixed-nolock | fix | no | 12,429 | 16.7 s | 0 |

  `tools/d10/statscmp.py` control vs fixed: "15 tables, 12428 rows compared: IDENTICAL"; control vs unfixed: DIFFERENT,
  "IMPLAUSIBLE: BuildingYields: missing". The failure line on the unfixed code: `SqliteLogger: failed to execute insert`
  (`SqliteLogger.cpp:509`), without an error code.
- **The new report [L, 23:57 EST]**: the fix build under a lock longer than its retries (3,000 ms of every 3,300 ms,
  Tiny 0-30, run `0084-c-longlock`): 4,534 rows (40 fewer than an unlocked Tiny 0-30), 30.0 s wall (the retry cut-off
  per turn keeps the game moving), and `CvAssert.log`: `SqliteLogger: failed to execute insert, row dropped (table
  MilitarySummary): SQLite error 5: database is locked` and `SqliteLogger: failed to begin a batch, batch dropped (table
  BuildingYields): SQLite error 5: database is locked`. So the cause of 9011's lost rows can now be read from the log
  the next time it happens; 9011's original cause is still not established [I: a lock, by the shape of the loss].
- Not verified: the 32-bit line live (compiled only, section 7c); the cost of a retry on the game's turn time beyond
  these wall times (one arm each, the workstation, not a timing measurement); SQLITE_LOCKED (only SQLITE_BUSY was
  produced); a database shared by two DLLs at once.

### 7c. Builds and state of the fix branches (2026-11-13 00:10 EST)

| Fork branch | Head | Base (pin) | Build (release) | DLL SHA-256 | Run |
|---|---|---|---|---|---|
| `i0-ship-fix-0096` | 55d750754 | bbfacca1a | `build_vp_clang.py`, clang-cl 19.1.7, LTO link 250 s, 2026-11-13 00:09; `D:/c5-64bit-work/4000/dll/i0-0096/` | `e6f14e69...4e8e` | none |
| `x64-fix-0096` | 511bf915d | 76992d235 | `build_all.py --config release --unlocked`, every step OK, 2026-11-12 23:39; `D:/c5-64bit-work/4000/dll/bin-x64-0096/` | `9d888f37...990c` | 7a check |
| `i0-ship-fix-0084` | 11de7fa61 (2 commits) | bbfacca1a | `build_vp_clang.py`, LTO link 277 s, 2026-11-13 00:03; `D:/c5-64bit-work/4000/dll/i0-0084/` | `0e8e1a73...6dfa` | none |
| `x64-fix-0084` | 351780044 (2 commits) | 76992d235 | `build_all.py`, every step OK, 2026-11-12 23:55; `D:/c5-64bit-work/4000/dll/bin-x64-0084/` | `8d1d9df0...abea` | 7b lock test |

All four pushed to the project's DLL fork (2026-11-13 00:05 EST).

### 7d. Bug 0103: the DLL's own AI logging changes the game (upstream bug)

- **Symptom [L, one-off 9034, 2026-11-12]**: the same new game with AI logging on (`dll_log_flags` 15) and off (0)
  differs from turn 38-54 (6 of 6 games; tiny Continents 1001/2002 from turn 47, standard from 51), on the x64 server and
  the 32-bit DLL alike (`tracker/issues/0103-*.md`). So decision D49's premise (logging changes only the logs) did not
  hold.
- **Cause [C, read at both pins and at upstream master 16596561f]**: `CvPlayer::GetBestSettlePlot` (`CvPlayer.cpp:47669`
  at the x64 pin; 48088 at upstream master) sets `bLogging = (GC.getLogging() && GC.getAILogging()) || bForceLogging`
  and with it (1) scores every candidate plot with a fresh `CvSiteEvaluatorForSettler::PlotFoundValue(pPlot, this,
  ignorePlots, bNewContinent, &strDebug)` instead of the cached `pPlot->getFoundValue(eOwner)` (the cache,
  `updatePlotFoundValues`, is computed once per turn, ignores unrevealed plots too and passes no `bNewContinent`, so the
  values differ and the AI picks other sites); (2) calls `ComputeFlavorMultipliers(this)` on the shared settler
  evaluator, whose multipliers `PlotFertilityValue` reads later (exploration score `CvPlot.cpp:11425`); (3) reads
  `pUnit->GetDanger(pPlot)` for every plot (`CvPlayer::GetPlotDanger` runs `UpdateDanger()` when dirty). It is called
  for settlers and, with `pUnit` NULL, from `CvUnitProductionAI.cpp:1101` (city production choice) every turn. Lesser:
  the "City founded ... plot value, q" log lines (`CvHomelandAI.cpp:3108`, `CvAIOperation.cpp:1956`) call
  `getFoundValue` / `GetSettlePlotQualityMeasure`, which run the lazy per-turn `updatePlotFoundValues()`; the homeland one
  is a no-op (the same function filled the cache at 3089), the operation one is not. Correction: one-off 9034's candidate
  (the homeland log line) was not the main cause.
- Scan method (2026-11-13, for the next reader): every `if (...getLogging/getAILogging/...)` block of the DLL (577 at
  the x64 pin) listed with the calls it makes, plus every `bool x = ...getLogging()` local; the one that switches a
  decision is `GetBestSettlePlot`'s `bLogging`. Not every called getter was read for hidden lazy updates.
- **Fix**: the decision always uses the cached value; the CitySites log writes that value (`cached` instead of the
  evaluator's breakdown string), the evaluator is not touched, the danger column is read only when the danger plots
  are not dirty (-1 otherwise); the two "City founded" lines read the cache without the update
  (`CvPlayer::getPlotFoundValueNoUpdate`, `GetSettlePlotQualityMeasureNoUpdate`). Branches `x64-fix-0103` 563ae6736
  (on 76992d235) and `i0-ship-fix-0103` 565ba5ba3 (on bbfacca1a, cherry-pick `-x`, no conflict).
- **Check, first fix commit [L, 2026-11-13 00:55-00:58 EST]** (`evidence/4000/0103_run.py`, run folder
  `D:/c5-64bit-work/4000/runs2/0103-tiny`, x64 server, Tiny Continents 1001/2002 new game to turn 60, every table of
  stats.db by `statscmp.py` against the pin with logging off; fix bin = the pin's `x64-76992d23/bin` with the DLL
  replaced, `D:/c5-64bit-work/4000/dll2/bin-x64-0103`, DLL `1cdc48e4...7e2e`, release `build_all.py`, every step OK):

  | Arm | DLL | `dll_log_flags` | First differing turn against pin/off |
  |---|---|---|---|
  | on-pin | pin | 15 | **47** (MapUnitsState; the turn 9034 saw on `oldpc`) |
  | on-fix | fix | 15 | **58** (MilitarySummary.Danger 11 vs 10, then MapUnitsState 59, MapPlotsState 60) |
  | off-fix | fix | 0 | identical (13,615 rows): the fix does not change a logging-off game |
  | fix, flags 1 / 3 / 5 | fix | AI only / + perf / + builder | 58, 58, 58 |
  | fix, flags 8 | fix | SetLogging without AI logging | identical |

  So `GetBestSettlePlot` was the main side effect (47 -> 58) and a second one remains behind `getAILogging()` alone
  (not the builder, perf or split flags).
- **Second side effect, by reading [C for the code, I that it is the turn-58 one]**: three AI log lines read
  `CvPlayer::GetNumUnitsSupplied()` / `GetNumUnitsOutOfSupply()`, which fill a lazy cache (`m_iNumUnitsSuppliedCached`,
  reset in `doTurn`, `CalculateNetHappiness`, city add/delete, great person expended; inputs: techs, population, cities,
  war weariness) on first use: the PlayerHappinessStats line in `CvPlayer::doTurn` right after `CalculateNetHappiness()`
  reset it, `CvMilitaryAI::LogMilitaryStatus`, `CvDiplomacyAI::LogWarStatus`. A value cached at the log's moment is what
  the game's own later reads (unit production, disbanding, deals, 20+ sites) get until the next reset. Second commit
  x64 f455e6a3f / i0 2002eff2f: those lines use new `GetNumUnitsSuppliedForLog()` / `GetNumUnitsOutOfSupplyForLog()`
  (cached value if present, else computed without storing). **Not built or run yet** (2026-11-13 01:27: the lock was held
  by other teams from 00:59, with 9039 to 01:51 and 9038 (~47 min) queued before 4000). Build script
  `evidence/4000/0103_build.sh`, test `evidence/4000/0103_run.py`, 0104 run `evidence/4000/0104_run.py` (not run). The statistics logger's MilitarySummary row (`MOD_SQLITE_LOGGING`,
  `CvMilitaryAI.cpp` 3093-3095, upstream too) fills the same cache, so statistics logging on vs off may also change the
  game [I, not tested; every project test has statistics logging on, so comparisons between arms are not affected].
- **Correction 2026-11-13 03:05 [L]: the supply cache was not the turn-58 effect.** Built (x64 f455e6a3f, DLL bin
  `bin-x64-0103b`), Tiny 0-60 with logging on still differed from turn 58. The second commit stays (the log lines did
  fill the cache) but changed nothing measurable here.
- **Third side effect, found by bisection [L, C]**: the diagnostic build (`x64-diag-0103c`, `C5_AILOG_DENY`) with AI
  logging off at single call sites: denying `CvTacticalAI.cpp` alone, and then lines 1172-1300 alone, made the Tiny game
  identical to logging off (8 rounds of 1-4 Tiny runs, about 25 s each, 03:15-03:30). The site is
  `CvTacticalAI::PlotPillageMoves` (`CvTacticalAI.cpp:1209` at the x64 pin): **`iBaseDamage = 0` for citadels and
  improved resources is set only inside the `GC.getLogging() && GC.getAILogging()` block** that names the target, so
  with AI logging on, undamaged units are also sent to pillage. Third commit x64 4178ca095 / i0 15d061e9c: the block only
  names the target; the decision keeps the logging-off value (25), the behaviour players have. The old comment ("also
  undamaged units may pillage this") suggests 0 was meant everywhere: a gameplay choice left to upstream.
- **Check, three commits [L, 2026-11-13 03:31 EST]** (x64 4178ca095, bin `D:/c5-64bit-work/4000/dll2/bin-x64-0103c`,
  DLL `7aeb40b4...`, release `build_all.py`, every step OK): Tiny 1001/2002 0-100: pin logging on differs from turn 47;
  **fix logging on = pin logging off, identical**; fix logging off = pin logging off, identical (`runs2/0103c-tiny`).
  Standard 1001/2002 0-100 (`runs2/0103c-std`): pin logging on differs from turn 51; fix logging on and fix logging off
  both identical to pin logging off. Whole Standard game with the fix and logging on (`runs2/0103c-stdfull`,
  03:39-03:48, 503 s): Cultural victory at turn 281 like logging off (pin with logging on: 310-312); against 9034's
  logging-off x64 run (`9034/r2-pairs/r2-std-x64`) every table is identical through turn 280; at 281 only the reference
  has extra rows (its old server played on after the victory, bug 0094).
- **32-bit line [L, 2026-11-13 03:55-04:00 EST]** (`evidence/4000/0103_run_x86.py`, c5h, Tiny 1001/2002 0-100, i0 15d061e9c built with `build_vp_clang.py` release, `D:/c5-64bit-work/4000/dll2/i0-0103`, DLL SHA-256 in `SHA256.txt` there): pin `i0ship-bbfacca1` logging on differs from logging off from turn 47; fix logging on and fix logging off identical to pin logging off (`runs2/0103c-x86`).
- **Fix branches (final, pushed)**: `x64-fix-0103` 4178ca095 (3 commits on 76992d235), `i0-ship-fix-0103` 15d061e9c (3 cherry-picks `-x` on bbfacca1a, no conflict). The statistics logger's own MilitarySummary row still fills the unit-supply cache (not touched; a candidate for bug 0116) [I].
- How the second effect was searched (scripts not kept; method for the next reader): from the call names of every
  AI-logging block, a name-based call graph (depth 2-3) to functions that assign a `m_*Cache*`/`m_*Cached*` member, set a
  dirty/updated flag, or call `RefreshIfOutdated`/`UpdateDanger`/`updatePlotFoundValues`. Name collisions give noise
  (`GetType`, `GetEntry`, `reset`); the real hits were `getFoundValue` (fixed), `GetPlotDanger` (fallout log line only)
  and `GetNumUnitsSupplied`. Getters with other lazy patterns would be missed: if the second commit is not enough, the
  local fork branch `x64-diag-0103c` (fix + `C5_AILOG_DENY=File.cpp[:lo-hi],...`, which turns `getAILogging()` off per
  call site through a `__FILE__`/`__LINE__` macro) bisects by call site, about 20 s per Tiny run. The `bin-x64-*` folders are the pin's
`x64-76992d23/bin` with only `CvGameCore_Expansion2.dll`/`.pdb` replaced (server.exe `3a35330a...4ccc`, the pin's).
The two bugs touch different files (`CvUnit.cpp`; `SqliteLogger.cpp`/`.h`), so the branches should combine without
conflict at a pin move [I, not merged]. Nothing was opened upstream; the 0096 report text is in its issue for the owner.

### 7e. Bug 0104: with AI logging on, x64 and the 32-bit DLL differ on Standard from turn 257 (open)

- **Reproduced on the workstation [L, 2026-11-13 04:11-04:28 EST]** (`evidence/4000/0104_direct.py`, pins, Standard
  1001/2002 from turn 0, AI logging on): the x64 server (443 s, AI log files written with `--dll-logs`) equals one-off
  9034's `oldpc` x64 run through turn 257, so the logging-on game is the same on both machines; the x86 DLL in c5h
  (547 s) differs first at turn 257 by one extra row each in BuildingInstantYields and InstantYields (Japan buys a Bank
  in Kyoto), MapUnitsState from 258.
- **From one save both builds agree [L]** (`evidence/4000/0104_run.py`): x64 played 0-250 and saved; x64 and x86
  continued that save to 258 identically (14 tables, 44,481 rows from 251). Both continuations differ from the direct
  plays from 251 (a loaded Standard game does not continue the direct one, section 5). So the turn-257 difference comes
  from state the x86 game carries that a load does not reproduce (a saved value that differs earlier, unseen in the
  statistics, or an unsaved cache) [I].
- **Not done**: the x86 AI logs (c5h keeps the DLL's logs only with `--detail full`; `0104_direct.py` passes it now; the
  rerun was refused by a timing hold). Comparing the two Japan homeland logs up to turn 257 should name the differing
  input. The x64 logs are in `D:/c5-64bit-work/4000/runs2/0104-direct/x64/Logs/`.
- **First state difference [L, 2026-11-13 04:44-05:07]**: with the x86 AI logs (c5h `--detail full`) all 506 shared AI
  logs compared (`evidence/4000/0104_logdiff.py`, blank lines and milliseconds masked): every line equal until **Japan's
  gold at turn 232**, treasury 1,037 (x64) vs 1,038 (x86) before its first purchase that turn, production-log GPT 2269 vs
  2270; the one gold carries to 257. Side facts: the x64 server's log writer adds a blank line after a message ending in a
  newline, CitySites dumps differ in format, and `%.2f` text differs in the last digit (msvcr90 vs UCRT printf), none of
  it game state.
- **Candidate [I] with a measured basis [L]**: the unit-maintenance `pow` of `CvTreasury::CalculateUnitCost` differs by 1
  ulp between msvcr90 and UCRT for 2,130 of 2,020,101 swept inputs (tech progress 0-100, base cost 0-20,000;
  `evidence/4000/0104_treasury_sweep.py` with tools/drift's mathcheck programs), enough to move `int(dFinalCost)` by one
  at an integer boundary. Not shown for Japan's actual inputs. Next: tools/drift `maths` instrumentation, window turns
  231-232. Issue given back to the MC; with the 0103 fix this trajectory no longer occurs; may belong with bug 0116.

## 8. DLL pin batch 2 (one-off 9045, 2026-11-13)

Decision D91 (MC, 2026-11-13 10:25): the next pin batch takes the project's own fixes of bugs 0084, 0096, 0103 (sections
7b, 7a, 7d) and 0116 (`x64-dll-port.md` 9.10), and rebuilds the server and the proxy from `main` (bugs 0100, 0117, 0118,
0123) with the repeatable tools of bug 0115 (`../06-tooling-and-workflows/implementation-effectiveness.md` section 7).
Tracker `tracker/issues/9045-dll-pin-batch-2.md`. Fork clone `D:/c5-64bit-work/9045/fork` (a local clone of 9020's,
`origin` = the GitHub fork). Scripts: `tools/d10/evidence/9045/scripts/`.

### 8a. Branches [C]

- **`i0-ship-batch2` 26234371e** (`Release-5.4.8-39-g26234371e`, fingerprint `v1 99a6ba78...6dd4`): cut from the i0 pin
  bbfacca1a; the eight commits of the four fix branches cherry-picked with `-x` in issue order: 0084 80b7495c5 ->
  cef58bbc4, 11de7fa61 -> 5201480f1; 0096 55d750754 -> 1fed984cd; 0103 565ba5ba3 -> 6d2e0954a, 2002eff2f -> d96de0bbf,
  15d061e9c -> c7e1af934; 0116 c4f3c7b34 -> 84d4d4e9e, d0fb1fbe3 -> ee893bc18. **No conflict, no hand edit** (each
  cherry-pick exit 0). Then one commit of this batch, 26234371e (section 8d): the statistics logger rolls back a failed
  batch COMMIT with `Execute("ROLLBACK")` instead of `Connection::RollbackTransaction()`.
- **`x64-batch2` 04b1211b8** (`Release-5.4.8-51-g04b1211b8`, fingerprint `v1 3f171f20...02ad`): `i0-ship-batch2` merged into
  the x64 pin 76992d235 with `--no-ff` (batch 1's route; x64 keeps i0-ship as an ancestor): first feece282c (the eight
  fixes; clean, auto-merges in CvGame.cpp/.h, CvHomelandAI.cpp, CvTacticalAI.cpp, `git show --remerge-diff` empty), then
  04b1211b8 (the rollback commit; clean, empty remerge-diff). Check: the eight `x64-fix-*` commits cherry-picked onto
  76992d235 (local scratch branch `scratch-9045-x64cp`) give **the same tree as feece282c** (`5d7240218e7d`), so the merge
  carries exactly the code the x64 fix branches were tested with.
- Pushed to the fork as new branches (10:28, 11:27 EST). `i0-ship`, `x64`, `master` untouched (bbfacca1a, 76992d235,
  bbfacca1a); fast-forwarding them to 26234371e / 04b1211b8 is the MC's step. `deps/i0.lock` and `deps/dll.lock` name the
  batch branches.
- **The save format does not change** [C, diff read]: 0116 keeps `m_iExpensePerTurnUnitMaintenance` saved (now set once in
  `CvTreasury::DoGold`), the new `CvGame::m_iCacheStamp` is not saved; 0084, 0096, 0103 touch no saved member. The save
  test (8e) agrees.

### 8b. The instance-aware variant: three conflicts [C]

`ia-7307aefdf-440878983.patch` (the instruments, `../06-tooling-and-workflows/headless-and-parallel-testing.md` 6.1b) no
longer applies to the batch: three files conflict. Resolved with `git apply -3` in a scratch worktree, both sides kept
each time (`scripts/s06_ia_resolve.py`):
- `SqliteLogger.cpp` constructor and `SqliteLogger.h` members: 0084's `m_iNoRetryTurn` and the instruments' `m_iRunId`;
- `SqliteLogger.cpp` `BatchWriter::flush`: the instruments' RunId bind, then 0084's `ExecuteWithRetry` and report;
- `CvGame::update()`: the instruments' pollers (memory snapshot, **external Lua request**, queue guard, alloc trace)
  first, then 0116's `m_iCacheStamp++`. **The order matters**: the external-Lua poller runs a Lua chunk inside `update()`;
  with the increment after it, a cache that chunk fills keeps the old stamp and this update's own reads recompute (the
  point of the stamp). With the increment first, a tool's Lua request would again decide what the game reads (bug 0116's
  mechanism, through the instruments instead of c5h's checkpoint).
Resolved patch `ia-ee893bc18-440878983.patch` (SHA-256 `59c53a10...7180`, 41 files, +9179 -41), kept in the `-ia` build
folders; it applies unchanged to 26234371e. `tools/instances/provision.py pinned_dll()` accepts the new `-ia` build.

### 8c. Builds; the x64 DLL did not repeat across folders: stub time stamps, fixed by 0131 [C, L]

**Corrected 2026-11-15 (issue 4252, epic 0334; `player-limits.md` 8.1; `deps/dll.lock` `repeat`): repeatability is NOT
guaranteed after 0131.** Two release builds of 04b1211b8 at one epoch, in the output folders m22 and m22base, had
byte-equal link inputs: 173 objects, link.rsp, and the stub and import libraries, with 0 stamped inputs. They still
gave two different DLLs: `CvPlayer::doInstantGreatPersonProgress` differs (629 bytes of .text, 19 of .rdata), so
something outside the link inputs picks the code variant. The "fixed by 0131" below holds for the stub time stamps
only. A follow-up ticket for repeatability was proposed to the MC.

Release builds, clang-cl 19.1.7. x64: `python -I server/build_server.py --src <git archive, fingerprint matches the lock>
--out <out> --config release --offline --jobs 6`, every `build_all.json` step (stubs, cvgamedatabase, cvlocalization, dll,
host, smoke) rc 0, 0 unresolved, narrowing gate 794 against 798 (0 new, 4 gone). 32-bit: `build_vp_clang.py --config
release` (worktree for the plain build, version "... Clean"; git archive + patch + version stub for `-ia`).

| Build | Source | Repository (tools) | Folder | Main hashes (SHA-256) |
|---|---|---|---|---|
| x64 A (re-validation build) | feece282c | 6a9b54408 | `<pins_dir>/dlls/x64-feece282-p6a9b544` | DLL `d40bd178...cd11`, server.exe `70efdabc...2de8`, proxy `4e5e5bb8...7dc0` |
| x64 B (second copy of A's source, another folder) | feece282c | 6a9b54408 | `D:/c5-64bit-work/9045/build-B-second` | DLL **`7aa8671c...e0c0`**, server.exe, proxy, libraries: = A |
| x64 final (the pin) | 04b1211b8 | a803776cf | `<pins_dir>/dlls/x64-04b1211b-pa803776` | DLL `74588135...4570`, server.exe `b7a4d8cc...3224`, proxy `78108da9...b4cf` |
| 32-bit A | ee893bc18 | - | `i0ship-ee893bc1` (and `-ia`) | `b8c4a8ab...d331`; `-ia` `b5c1fa66...4716` |
| 32-bit final (the pin) | 26234371e | - | `i0ship-26234371` (and `-ia`) | `f92881a3...59d5`; `-ia` `3c2f47da...a06c` |

The three libraries and civ5save.exe equal issue 0115's double build of 76992d235 (`75045a87`, `35863ede`, `30724d6d`,
`409422ef`): their sources did not change.

**Repeatability (bug 0115's claim, re-tested; corrected 2026-11-13 by issue 0131).** Builds A and B (same source commit,
same tool commit, different source and output folders, 10:41 and 10:57) gave equal server.exe, proxy, libraries and
civ5save.exe, but **different x64 DLLs of the same size**. All 175 LTO bitcode objects of `deps/dll/obj` and `link.rsp`
are byte-equal between the two; the PCH differs (not a link input); the static stub libraries (`CvGameCoreDLLUtil`,
`CvWorldBuilderMap`, `FireWorks`, `FLua`, `lua51`) differ only in their members' COFF header TimeDateStamps (no code);
the three import libraries (`CvGameDatabase_x64`, `CvLocalization_x64`, `lua51_x64`) are equal [C for A against the pin
build F, `tools/x64-dll/evidence/2026-11-13-0131/cmp_inputs.py`; B was deleted by 9045's cleanup]. The linked DLLs differ in one function,
`CvPlayer::doInstantGreatPersonProgress` (753 bytes in 16 runs of `.text` and `.rdata`; different register and
instruction choices, llvm-symbolizer with the PDB), plus the `/Brepro` header stamp.

**Cause: the stub libraries' COFF time stamps [L, issue 0131, 2026-11-13 21:30-22:12 EST].** clang-cl writes the compile
time into the COFF header TimeDateStamp of every native object unless it is given `/Brepro` (its default is
`-mincremental-linker-compatible`); `SOURCE_DATE_EPOCH` does not reach that field [C: the stubs of 9045's builds A and F carry
their compile times 1794584509-11 and 1794588319-21 although both builds set the variable]. The stubs are such objects,
and the DLL's full-LTO link (lld-link 19.1.7) gave other code for one function when only those stamps changed. Relinks
of the pin build's own inputs (9045's `bF`: objects, `link.rsp`, `.def`, stubs, vcvars environment), one link at a time,
6-8 min each (`tools/x64-dll/evidence/2026-11-13-0131/relink.py`, `s01_relinks.sh`, `s02_relinks_env.sh`; log
`D:/c5-64bit-work/0131/relinks.log`):

| Relink | Changed against the pin build | DLL SHA-256 |
|---|---|---|
| r1, r7 (control, 21:37 and 22:05) | nothing (fresh folder) | `74588135...4570` = the pin |
| r2 | build A's stub libraries (the 21 static-stub members stamped 3,810 s earlier; import libraries equal) | `74588135...4570` = the pin |
| r3, r6 (21:50 and 21:57) | every COFF member of every stub library stamped +1000 s (the 21 static-stub members and the 9 import-descriptor members of the three import libraries, 0 -> 1000); nothing else | `09D23C25...18B5`, both times |
| r4 | build A's vcvars environment (differs in `OLDPWD`, `SHELL`, `SHLVL`, `C5_QUIETLOCK_JOB`) | `74588135...4570` = the pin |

r3's DLL differs from the pin in 16 runs, 754 bytes: the header stamp and `CvPlayer::doInstantGreatPersonProgress`
(`.text` 11 runs, `.rdata` 4; one changed instruction early in the function, then displacements +4 and less nop padding
at its end; `tools/d10/evidence/9045/scripts/s14_dlldiff.py`, `s15_sym.py`), the same function and size as 9045's A/B pair.
So: the link is deterministic for identical inputs (r1 = r7 = the build, r3 = r6), the environment did not change it, and
the stub libraries' time stamps do, deterministically. Not every stamp change does (r2 gave the pin's bytes): the stamps
select one of (at least) two code variants of that function by a mechanism inside lld/LLVM not found here [I: some
choice keyed on input bytes or their layout]; which members' stamps matter (static stubs or import descriptors) was not
bisected. *(Replaced: "First reading (11:10): lld-link's LTO code generation is not deterministic", corrected by 9045 at
12:48, and 9045's "the stub libraries ... or the environment (B's vcvars env)", which 0131 settles.)*

**Fix (issue 0131, tool commit 1d75adca).** `repro_cl_args` in `tools/x64-dll/build.py`, which every clang-cl compile of the
DLL, the stubs, the two libraries, the server, civ5save, vpipc, zlib, c5render and the proxy takes, now adds `/Brepro`: every native
object's stamp is 0, so the stub libraries repeat byte for byte (LTO bitcode objects have no such field). After every
link `build.py` prints a **repeatability check** line (`tools/x64-dll/repeatcheck.py`, also runnable on an existing
`link.rsp`): it reads the link's response file and lists every object or library member of the build's own folder whose
COFF stamp is not 0 (`repeatability check: OK: N link inputs of this build (M objects and library members), none carries
a time stamp; DLL sha256 ...` or `... NOT REPEATABLE: k of M ... carry a COFF time stamp (e.g. ...)`), and records it in
`build-summary.json` (`repeat_check`). On 9045's pin build it reports 21 of 530 stamped. Tests
`tools/x64-dll/tests/test_repeatcheck.py` (7, synthetic archives, including the stamped and the no-input cases).

**Double build with the fix [L, 2026-11-13 22:13-22:57 EST].** Two full release builds of 04b1211b8 at tool commit
bbae0edd (the fix 1d75adca with `main` merged), `SOURCE_DATE_EPOCH=1794587302` (the lock's `epoch`), each `python -I
server/build_server.py --src <dir> --config release --offline --jobs 6` from a git archive of the fork: `D:/c5-64bit-work/0131/srcA`
-> `outA` (22:13-22:28, 914 s) and `D:/c5-64bit-work/0131/src-copy-B-in-a-longer-folder` -> `out-build-B-longer`
(22:36-22:57, 1,280 s; its start waited 8 min for other agents' DLL links, D107). Every `build_all.json` step rc 0, 0
unresolved, narrowing gate 794 against 798. Both print `repeatability check: OK: 181 link inputs of this build (530 objects
and library members), none carries a time stamp`. **All seven binaries equal** (`tools/x64-dll/evidence/2026-11-13-0131/
double_build.py`, `acceptance.txt`): DLL `74588135...4570` in both, **the pin's own hash** (with stamp-0 stubs the link chose the
pin's variant); server.exe `24F0180D...A319` (build id of tool commit bbae0edd, so not the pin's B7A4D8CC); proxy
`8260DF06...AFEE` (likewise); lua51_x64.dll, CvGameDatabase_x64.dll, CvLocalization_x64.dll and civ5save.exe equal to the pin's
(`30724D6D`, `75045A87`, `35863EDE`, `409422EF`).

Consequence for the pin: the pin's DLL bytes were reproduced by both builds, in two new folders, with this fix and
`SOURCE_DATE_EPOCH=1794587302` [L]; deps/dll.lock's
`repeat` line ("NOT repeatable across folders") is out of date (the MC's file; named in 0131's hand-back). Issue 0115's double
build of 76992d235 gave equal DLLs although its stubs were stamped too: like r2, not every stamp change flips the variant.
The 32-bit build was never repeatable (absolute `__FILE__` paths, `__DATE__`/`__TIME__`, link time).

### 8d. The proxy route refused the first batch DLL: `RollbackTransaction` [L, C]

The verify leg (8e) on build A failed in 8 s: the server's remote-mode database write recording
(`server/src/remote/srv_remote_db.cpp`, `kHooks` and `kNeutral`) hooks every `CvGameDatabase_x64.dll` import of the DLL
and **fails loud on any it does not know**: "database: LOUD: the DLL imports
?RollbackTransaction@Connection@Database@@QEAAXXZ, which the write recording does not cover". The import came with bug
0084's fix (a batch whose COMMIT still fails is rolled back). Direct mode (run_server, the save test, the testbeds) does
not install the recording and was not affected. MC decision (option 1 of two, 11:2x): the DLL rolls back with
`m_kConnection.Execute("ROLLBACK")` (fork commit 26234371e and its merge 04b1211b8); `Execute(sql)` is hooked and passes
through for the statistics connection (not the game database). Behaviour is the same; the path runs only after a COMMIT
failed after its retries, never in an unlocked game. The final DLL has no `RollbackTransaction` import [C, byte scan].
The alternative (a hook for it in srv_remote_db.cpp) was not taken: server code, team-3300's area. Any future DLL change
that imports another database member hits the same gate: check the verify leg before pinning.

### 8e. Offline checks

On build A / the A layouts [L, 2026-11-13 11:12-11:45]:
- `test_proxy_rt.py --build bA`: **42/55**, the 13 failures every case that boots the real server: bug **0128** (since
  0123 the proxy's default loc_db is the game's cache, the test's fake game folder has no ICU data, and the server's
  `ReportAndExit` before `Init` then crashes; the stack is in `tracker/issues/0128-*.md`; the server side goes to team
  3300). Fixed on the test side (`loc_db=none` in every ini the test writes, one new check): see the final line below.
- hutest 45 checks, 0 failed; hutest `--scenario all` 8/8 as expected.
- verify leg (proxy-design.md 17.2 command, Tiny 0-50 strict through the proxy, `local_answers=1 local_verify=1
  test_slot_poll=4 lua_mirror=1`): FAIL at boot, section 8d.
- **Save test (D10/D24) PASS**: `run_d10.py --x86-dll i0ship-ee893bc1 --srv-bin x64-feece282-p6a9b544/bin --huge-save
  D:/c5-64bit-work/9020/inputs/9020-HUGE_0350.Civ5Save --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, AI
  logging off, 11:24:38-11:45:06 (1,228 s): parts 1-3 pass on tiny 0-100, standard 0-200, huge 350-400, only the
  version-string exception; x86 direct = x64 direct: tiny 30,666 rows, standard **351,496** (old pins 351,478: the
  standard game changed), huge 683,123 (unchanged). Report `D:/c5-64bit-work/9045/d10/report.txt` (copy in
  `tools/d10/evidence/9045/`).
- Fix checks (`evidence/4000/`) with the old pin layout `x64-76992d23-p1607d42/bin` against the batch:
  `0096_run.py` **PASS** (pin: empty notification; batch: "A Unit was Killed!"); `0103_run.py` Tiny 1001/2002 0-100: batch
  logging off = batch logging on = **pin logging off, identical** (34,614 rows), pin logging on differs from turn 47;
  `0084_lock_test.py` (Tiny 0-60, 700 ms locks): the batch kept every row under the lock (12,429 = control, 0 report
  lines), but **the negative control did not fail**: the pin also lost no row this time (its server is now 1607d42's,
  not 3a35330a's of 7b; why the lock no longer makes the pin's inserts fail was not looked into), so the run proves
  nothing about the fix (verdict FAIL by the script's rule). 0084's own evidence (7b) stands.
- **0084 under long locks [L, 2026-11-13 12:48-12:50]** (final layout, `0084_lock_test.py --turns 60 --hold-ms 2500
  --gap-ms 300`, 12 windows, 30.1 s locked per locked arm; run `D:/c5-64bit-work/9045/fix0084-final`): control 12,429
  rows; **unfixed (old pin) 12,417** (12 missing: BuildingYields 7, MilitarySummary 5; asserts "failed to execute insert",
  "failed to execute batched insert"); **fixed (batch) 12,314** (115 missing in 8 tables, BuildingYields 55, MilitarySummary
  10, ...), `CvAssert.log`: "failed to begin a batch, batch dropped (table InstantYields): SQLite error 5", "failed to
  execute insert, row dropped (table MilitarySummary)" and **"failed to drop table (table PolicyChoices): SQLite error 5:
  database is locked"**; fixed without lock 12,429. Two findings for 0084's owner [L, cause I]: (1) beyond the retry
  budget (about 0.3 s) the fix loses more rows than the old code, since a failed `BEGIN IMMEDIATE` drops a whole batch
  where the old SAVEPOINT batch lost single rows; (2) the logger tried to DROP a table during the game under a lock.
  *Corrected 2026-11-13 (bug 0132, 8h): the DROP was the `DROP TABLE IF EXISTS` that every first registration of a table
  missing from the fresh stats.db issued before its CREATE, not a lock-misread schema; PolicyChoices did not exist yet and
  lost no row. Replaced: "here only the DROP's own failure saved PolicyChoices".* Under 700 ms locks (7b, 8e above) the fix keeps every row. Not a blocker for the pin (no
  test or game holds stats.db locked); to the MC for an id.

On the final builds (04b1211b8 / 26234371e; layouts `x64-04b1211b-pa803776`, `i0ship-26234371`) [L, 2026-11-13
12:08-12:38]: `test_proxy_rt.py --build bF` **56/56** (46 s; with 0128's test fix); hutest 45 checks, 0 failed;
`--scenario all` 8/8; **verify leg PASS**: 1,300,212 getter answers and 1,198,941 UI-Lua hits verified, 0 mismatches,
0 lock errors (the same counts as 9029/9043: Tiny 0-50 plays the same game); the DLL has no `RollbackTransaction` import
[C]. **Save test PASS** (12:14:21-12:37:38, 1,397 s, the same command with the final builds): parts 1-3 on tiny,
standard, huge; x86 direct = x64 direct 30,666 / 351,496 / 683,123 rows (`tools/d10/evidence/9045/d10-final-report.txt`).
`0096_run.py` PASS; `0103_run.py` batch logging on = off = old pin logging off (identical), old pin logging on differs
from turn 47.

### 8f. Re-validation on the testbeds (x64 against x86, c5h's checkpoint as today)

Build A (x64 DLL `d40bd178`, server `70efdabc`; x86 `b8c4a8ab`), shipped without PDBs; `longgames.py` whole games to the
end, AI logging off, statistics on, the pinned VP database; per game `--compare x64:x86` (`tools/d10/statscmp.py`, every
table). Jobs `oldpc` `20261113-105627-ff18`, `-105646-d46d`; `laptop` `-105656-6d6e`, `-105714-8486` (submitted
10:56-10:57). The final DLLs differ from A only in the rollback line (8d), which runs only after a failed COMMIT, so the
games are the same [I]. **Result [L, 2026-11-13 10:57-13:32]: 17 of 17 whole games x64 = x86**, every table identical but
`ReligionBeliefYields.IsReligionOwner` (the known unset column, `../06-tooling-and-workflows/quiet-testbed.md` 14.6); every
x64 run a clean `game_over` rc 0, every x86 run c5h `victory` rc 0, no Lua errors, no lost rows. Fetched to
`D:/c5-64bit-work/9045/rv/<job>/`, summary script `scripts/s26_summ.sh`.

| Game | Set | Old pins x64 / x86 (9037) | Batch: x64 = x86 | Rows compared |
|---|---|---|---|---|
| huge Pangaea 1717/2828 | 0116 | Diplomatic 359 / 348 | Diplomatic 359 | 2,970,630 |
| standard Archipelago 5859/6061 | 0116 | Science 400 / 418 | Science 418 | 2,474,168 |
| tiny Oval 2121/3131 | 0116 | Domination 298 / Cultural 319 | Cultural 347 | 602,744 |
| large Oval 4243/4445 | 0116 | Cultural 357 / Science 383 | Science 380 | 2,406,000 |
| standard Continents 1212/3434 MARATHON | 0116 | Domination 931 / 926 | Domination 970 | 3,210,295 |
| large SmallContinents 2627/2829 | 0116 | Science 377 / Diplomatic 357 | Diplomatic 357 | 2,041,019 |
| standard Pangaea 6263/6465 EPIC | 0116 | Cultural 447 / 452 | Cultural 476 | 1,880,475 |
| small Archipelago 4141/5151 | 0116 | Science 410 / 405 | Science 412 | 1,572,858 |
| standard Fractal 5757/6868 | 0116 | Science 418 / 410 | Science 414 | 2,255,970 |
| tiny Fractal 1111/2222 | sample | Cultural 296 (both) | Cultural 296 | 491,344 |
| standard Pangaea 3333/4444 | sample, had 2 `WorldStateLog` rows differing | Cultural 370 | Cultural 370, **now fully identical** | 1,622,664 |
| tiny Archipelago 7475/7677 | sample, had approach counts differing | Cultural 386 | Cultural 387 | 1,010,727 |
| tiny Continents 5051/5253 | sample, had one opinion row differing | Cultural 361 | Cultural 361 | 903,979 |
| standard Continents 3839/4041 QUICK | sample, had a `Danger` value and opinions differing | Science 247 | Science 251 | 1,158,307 |
| large Continents 7777/8888 | sample | Science 359 | Science 359 | 2,190,458 |
| small Oval 3535/4646 | sample | Science 423 | Diplomatic 402 | 1,491,153 |
| small Pangaea 1819/2021 | sample | Science 399 | Science 427 | 1,708,434 |

The nine 0116 games end as in 9044's runs with fixes 1 + 2 (same outcomes and row counts where 9044 recorded them: tiny
Oval 602,744, small Archipelago 1,572,858, large Oval 2,406,000, standard Fractal 2,255,970, large SmallContinents 2,041,019,
standard Pangaea EPIC 1,880,475, marathon 3,210,295, standard Archipelago 2,474,168). **The four sample games that had
statistics-only differences on the old pins (opinion, approach and `Danger` figures) are now fully identical**: those
figures read the World Congress votes cache or the unit-maintenance value that c5h's checkpoint used to fill (0116) [I,
consistent with 9044's cause]. Several games end differently from the old pins (the batch changes results: 0116 on the
32-bit side, and the x64 side from turn ~200 in some games), as expected; never compare with old-pin outcomes (COMMON 18).

### 8g. Not verified

- The final builds in the testbed games (build A was used; 8f says why that stands).
- Whether the two code variants of `CvPlayer::doInstantGreatPersonProgress` (8c; the link is deterministic, the stub
  time stamps chose the variant) behave differently anywhere (no game was played on build B or r3's DLL).
- 0084's negative control on the current pin layout (8e). *(2026-11-13, 8h: under 2.5 s locks the pin layout does lose
  rows, 164 in bug 0132's run; under 700 ms locks still not shown.)*
- The old pin DLLs loading saves written by the batch (the format is unchanged by reading; not run).
- The server's crash in `ReportAndExit` before `Init` (0128; team-3300) is not fixed; the test no longer reaches it.
- *(Done 2026-11-13, issue 0131: the input is the stub libraries' COFF time stamps, fixed with `/Brepro`; 8c.)* Not
  known: the mechanism inside lld/LLVM by which input time stamps select the code variant, and which stub members matter.
- Tool defaults that still name the old pins: `perf/turnbench/turnbench.py` (issue 7012, team 7000); COMMON rule 18 (the
  MC's). Corrected 2026-11-13 (issue 0133): `tools/d10/run_d10.py` (DEFAULT_X86_DLL, DEFAULT_SRV_BIN) and
  `tools/drift/run_pair.py` (PIN_X86, PIN_SRV_BIN) now read their defaults from the `release` key of `deps/i0.lock` and
  `deps/dll.lock` (the first absolute path in the value), tested by `tools/d10/tests/test_pin_defaults.py` and
  `tools/drift/tests/test_run_pair.py` [C]. (Replaced: this line listed those two tools as well.)

### 8h. Bug 0132: the statistics logger under 2.5 s locks (account B, 2026-11-13)

Tracker `tracker/issues/0132-*.md`; the two findings of 8e (whole batches dropped, a DROP under a lock). Fork clone
`D:/c5-64bit-work/0132/fork` (a clone of 9045's), scripts `D:/c5-64bit-work/0132/scripts/`.

- **Causes [C, `SqliteLogger.cpp` at 26234371e]**: (1) `ExecuteWithRetry` gave up after 10 retries 20 ms apart plus
  the busy handler's waits (about 0.3 s in all, 8e), and then turned retries off for the rest of the turn; a 2.5 s lock
  outlasts that, so `BEGIN IMMEDIATE` failed and `flush` dropped the whole batch, and every later statement of that turn
  got only the busy handler's 60 ms. (2) The DROP was not a misread schema: `RegisterTable` sent every table missing
  from stats.db (each table's first registration in a fresh run) down the "missing or differs" path, `DROP TABLE IF
  EXISTS` then `CREATE TABLE`; the DROP met the lock. PolicyChoices kept all its rows in 9045's run (equal counts in all
  four arms), so the CREATE succeeded later. This corrects 8e's reading "the schema check ... here only the DROP's own
  failure saved PolicyChoices" (2026-11-13): no existing table was at risk. (3) Found on the way [L, Python 3.41.2 only,
  `scripts/s04_pragma_lock.py`]: under another connection's EXCLUSIVE lock even `PRAGMA table_info` fails ('database is
  locked') for an existing and a missing table alike; the DLL retried only its prepare, not its steps, so a table's
  first registration under a lock read "unreadable", was not created, and lost every row [I for SQLite 3.7.17].
- **Fix** (fork `i0-fix-0132` = 26234371e + 3630da685 + 0f1a2b955; `x64-fix-0132` = 04b1211b8 + two `--no-ff` merges
  of it, head 2c2c9d997, clean, empty remerge-diffs): retries end after 10 s per statement (`GetTickCount`), not after
  10 attempts; the per-turn cut-off after a statement used its time up stays, so a database held locked for good stalls
  the game by at most 10 s per turn plus the busy handler per statement. A batch whose `BEGIN IMMEDIATE` fails, or whose
  COMMIT fails and is rolled back, writes its rows one by one (each its own transaction, each retried) instead of being
  dropped; only a failed ROLLBACK drops the batch (still inside the transaction). A missing table is created with
  `CREATE TABLE IF NOT EXISTS`, no DROP; DROP only for a table whose differing schema was read, and no CREATE after a
  failed DROP. The schema read (`TableSchemaMatches` around a new `ReadTableSchema`) is retried as a whole within the
  same budget. No new database import (the x64 server's remote-mode write recording, 8d) [C: only `Execute`,
  `ErrorCode`, `ErrorMessage` and `Results` members are used, as before].
- **Acceptance command**: `0084_lock_test.py --pin-bin <pins_dir>/dlls/x64-04b1211b-pa803776/bin --fix-bin
  D:/c5-64bit-work/0132/bin-x64-0132 --out <run> --turns 60 --hold-ms 2500 --gap-ms 300` (pin = D96's; the fix bin is
  the pin's `bin` with only the DLL and its PDB replaced). Expected: control = fixed = fixed-nolock row counts, no
  `SqliteLogger` failure line in the fixed arms, the unfixed (pin) arm loses rows.
- **Builds [C]** (2026-11-13 20:24-20:47 EST): `python -I tools/x64-dll/build_all.py --src <worktree of the fork at the
  commit> --out D:/c5-64bit-work/0132/bx2 --config release --offline --unlocked --jobs 6`, every step (stubs,
  cvgamedatabase, cvlocalization, dll, host, smoke) exit 0, narrowing gate 794 against 798 (0 new), no warning in
  `SqliteLogger.cpp`; the first commit alone built the same way (`bx`, DLL `366a4cae...63c5`). Final DLL (2c2c9d997)
  SHA-256 `5B7DE48CC86484758FF73126DC4801D066FAC718416AC5A25E907DE9B194128C` (23,614,464 B); fix bin
  `D:/c5-64bit-work/0132/bin-x64-0132` (hashes in `bin-x64-0132-SHA256.txt` beside it; every other file = the pin's).
  Its 45 CvGameDatabase imports equal the pin DLL's (`scripts/s07_imports.py`, llvm-readobj). The 32-bit line was not
  built (the ticket: only if the MC asks).
- **Acceptance PASS [L, 2026-11-13 20:48:33-20:51:32 EST]** (run `D:/c5-64bit-work/0132/acc`, the command above,
  through `quietlock.py run` without `--take`, AI logging off; output `evidence/4000/0132-summary.txt`):

  | Arm | Build | Lock (2.5 s of every 2.8 s) | Rows | Wall | `SqliteLogger` lines |
  |---|---|---|---|---|---|
  | control | D96 pin | no | 12,429 | 30.6 s | 0 |
  | unfixed (negative control) | D96 pin | 18 windows, 45.2 s | 12,265 (**164 missing** in 7 tables: BuildingYields 103, BuildingsOverview 39, MilitarySummary 9, HandicapYields 6, ReligionBeliefYields 3, InstantYields 2, TechChoices 2) | 67.5 s | "failed to execute insert, row dropped (table MilitarySummary): SQLite error 5", "failed to begin a batch, batch dropped (table BuildingYields): SQLite error 5" |
  | fixed | fix | 16 windows, 40.1 s | **12,429** | 62.4 s | **0** |
  | fixed-nolock | fix | no | 12,429 | 18.5 s | 0 |

  `tools/d10/statscmp.py` control vs fixed: "15 tables, 12428 rows compared: IDENTICAL"; control vs unfixed: DIFFERENT.
  The pin's loss differs from 9045's 115 (8e) because the lock windows fall at other moments of the game; this run's
  pin arm issued no DROP (9045's did). Wall times are one arm each on a shared workstation, not a timing measurement:
  the fixed arm's game waits out each lock window instead of dropping rows, so a locked game runs about as long as the
  lock lets it.
- **Not verified**: the 32-bit line (`i0-fix-0132` not built); the per-turn cut-off path live (a lock longer than 10 s:
  rows are then written one by one with the busy handler only, about 60 ms each, the pre-0084 cost); SQLITE_LOCKED;
  `ResolveGameId`'s SELECT step is still not retried (a lock at game start could give GameId 0; the test's locks start
  at ARM). For the next pin batch: the instance-aware patch (`ia-ee893bc18-440878983.patch`, 8b) will conflict in
  `BatchWriter::flush`, whose row loop moved to the new `BatchWriter::InsertRows`; its RunId bind goes there, before the
  `ExecuteWithRetry` call [C, patch read].
## 9. Upstream pull-request branches for 0096 and 0103 on master (one-off 9048, 2026-11-13)

*(Numbered 8 when written; renumbered 9 at the merge with one-off 9045's section 8, 2026-11-13. Issues 0096, 0103 and 9048
cite it as "section 8": they mean this section.)*

The owner asked for two PRs against `LoneGazebo/Community-Patch-DLL` closing #13571 (0096) and #13572 (0103). Branches built and
checked; the PR texts are in the two issue files under "Upstream PR (draft for the owner's approval; not opened)". Nothing was
opened or posted upstream.

- **Base [C]**: upstream master **16596561f** ("Fix merge error", 2026-11-11 07:58 EST; `git ls-remote upstream` at
  12:28 EST, no newer commit). 151 commits after Release-5.4.8 (7307aefdf).
- **Branches (the project's DLL fork, pushed 12:46 EST)**:

  | Branch | Head | Commits on 16596561f |
  |---|---|---|
  | `upstream-fix-13571-civilian-killed-notification` | 1308df2f7 | `cherry-pick -x` of 55d750754, clean |
  | `upstream-fix-13572-logging-changes-game` | 40af3f82b | `cherry-pick -x` of 565ba5ba3 / 2002eff2f / 15d061e9c (8af702e7b, 6bd2f8fbd, b0a889d1c), all clean; 40af3f82b takes our bug numbers out of the code comments (the pillage comment now says only that the block names the target and why 0 was dropped) |

  No conflict: master had not touched the changed lines since 5.4.8, and master still has both bugs (0096 at `CvUnit.cpp`
  20565-20566; 0103's `GetBestSettlePlot` `bLogging` branch and the pillage block unchanged). The commit messages keep
  the fork's text ("Bug 0103: ...", "Tracker 0096 (c5-64bit-patch)").
- **Upstream's rules [C]**: no PR template and no CONTRIBUTING file (`.github/` holds `ISSUE_TEMPLATE/` and two
  workflows); `DEVELOPMENT.md` asks contributors to check that changes compile without new warnings in both the MSVC and
  the clang builds, and CI runs both (`build_vp.yml`: clang, and VC9 `msbuild VoxPopuli_vs2013.sln` Debug and Release,
  on every PR to master).
- **Builds [L, 12:28-12:34 EST]**: `build_vp_clang.py --config release` through `quietlock.py run`, three in parallel
  (pch 11 s, cpps 92 s, LTO link 263 s each); `evidence/9048/build.sh`. DLL SHA-256: master `6002da27...c644`, 13571
  `3a1862d8...888f`, 13572 `d816074d...6458` (`D:/c5-64bit-work/9048/dll-*/SHA256.txt`). Each `build.log` has 25 warnings,
  the same set as master's: no new warning. **Not built with VC9** (not installed here; upstream CI builds it).
- **How master was run [L]**: the master DLL does not start on the project's pinned 5.4.8 database (c5h, phase cache:
  `Database error: no such table: UnitPromotions_TerrainHeal`, then exception 0xC000001D in the DLL). So master runs on
  master's own mod data (`evidence/9048/setup-master-db.sh`, 50 s): a copy of the toolkit game folder (205 MB) without
  the `VP_MODPACK_5_4_8_NEUTRALS`, `VPUI` and `UI_bc1` DLC folders; a MODS folder = `git archive 16596561f` of
  `(1) Community Patch` (v151) and `(2) Vox Populi` (v17); and `c5h dbbuild --mod <CP>:151 --mod <VP>:17` with
  `--snapshot cache/db/snapshot/A4AC7A9F4EE95DDE/snapshot.db` (the clean BNW + DLC snapshot: 369 tables, no
  CustomModOptions). Without `--snapshot`, dbbuild refuses: the snapshot build logs 18 failed steps (WB_Script map scripts
  without `GetMapScriptInfo`) and counts them as errors (the older snapshot 4DF730138BA1F0EE had the same 18 failed steps
  and its report says "built"). After the build `CustomModOptions.SQLITE_LOGGING` is set to 1 (master's default is 0;
  the pinned 5.4.8 database has 1). Games: `c5h run ... --game-dir <copy> --mods-dir <archive> --mod ... --no-addins
  --db built.db --dll X` (`evidence/9048/run_master.py`, gated per start). The master database build logs two errors
  from master's own data: `(2) Vox Populi/.../BuildingChanges2.sql: no such table: Helper` (not looked into) and
  `PromotionDisplaySweeps.sql: near "<<": syntax error`, which is **an unresolved merge-conflict block in upstream
  master** (lines 304-308, `<<<<<<< HEAD` ... `>>>>>>> 891bd6ce7 ((11-039) Siege Rebalancing II : Parabolic Payload
  promo)`) [C]: a candidate upstream report, not filed.
- **0096 on master [L, 12:42 EST]** (`evidence/9048/m0096.sh`: the probe `evidence/4000/0096-civilian-killed.lua` as a
  c5h `--lua-probe`, Tiny Continents 1001/2002, 1 turn, AI logging off; player 0 is human when the probe runs): master
  `0096-RESULT FAIL empty notification: message [] summary []`; branch `0096-RESULT PASS message [A Unit was Killed!]
  summary [A Unit was Killed!]`. (c5h reports the run as "failed" because the probe ends with `error()` on purpose.)
- **0103 on master [L, 12:43-12:48 EST]** (`evidence/9048/m0103.sh TURNS SIZE`, Continents 1001/2002, every stats.db
  table by `tools/d10/statscmp.py` against master with logging off; runs in `D:/c5-64bit-work/9048/runs/`):

  | Game | master, logging on | branch, logging on | branch, logging off |
  |---|---|---|---|
  | Tiny 0-60 | from turn 43 (MapUnitsState) | identical | identical |
  | Tiny 0-100 (35,802 rows) | from turn 43 | identical | identical |
  | Standard 0-100 | from turn 32 (MapUnitsState) | only `ReligionBeliefYields.IsReligionOwner` differs, 196 rows from turn 34; the other 15 tables identical | identical |

  `IsReligionOwner` is the known memory-layout column (pantheon holy-city coordinates; `server-host.md` section 5,
  `x64-dll-port.md` 9.6), not game state. Master diverges earlier than 5.4.8 did (Tiny 43 vs 47, Standard 32 vs 51).
  Wall time per arm: Tiny 100 turns 19-33 s, Standard 100 turns 42-76 s (logging on is the slower). **Not run on
  master**: a whole game, other map sizes, the x64 line (the x64 fix branch is unchanged: section 7d).
- Scratch: `D:/c5-64bit-work/9048/` (worktrees `wt-13571`, `wt-13572`, `wt-base` of the fork checkout
  `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL`; DLLs; game copy, mods, `master-db/built.db`; runs). Kept until
  the PRs are opened (the branches may need rebuilds after review; the master database setup is reusable); remove with
  `git worktree remove` from the fork checkout.

2026-11-13 (MC, after the PRs opened): on the owner's word the PR branches' commit messages were rewritten without internal bug numbers, the project name or `(cherry picked from ...)` lines (trees unchanged): 13571 -> cf553a13f, 13572 -> 974b7f7b1 / 2d7089549 / 600fb873e / 0a3a1f2c1. Rule for future upstream branches: commit messages carry no tracker ids, project names or cherry-pick provenance; cherry-pick without `-x` or reword before pushing.
