# What a new Vox Populi release costs the patch: the checklist

Written 2026-11-13 by ticket 0112 (account B), from the Architect's weak place C (issue 0109): no document said what moving
the patch to a new upstream Vox Populi release costs. This file is the procedure, step by step, with the tool, the owner
and the time of each step. It applies to a new release tag (`Release-5.x.y`) and, with fewer steps (marked), to a pin
batch of upstream fixes on the same release (the route of one-offs 9020/9029 and 9045).

Marks: `[C]` confirmed by source, `git` or a file; `[L]` measured in a run (date and issue named); `[I]` inferred or
estimated. Times are wall-clock on the owner's workstation (32 logical cores) unless a testbed is named. "Agent hours" are an
agent's working time; "machine" is the run's own length.

Pins this was written against (D96, COMMON rule 18): `deps/i0.lock` 26234371e (`Release-5.4.8-39-g26234371e`),
`deps/dll.lock` 04b1211b8 (`Release-5.4.8-51-g04b1211b8`), release layout `<pins_dir>/dlls/x64-04b1211b-pa803776`.
Upstream state read: the upstream clone `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL`, `upstream/master` 16596561f
(2026-11-11), the project's fork clone of one-off 9045 `D:/c5-64bit-work/9045/fork`.

## 0. The cost in one table

| # | Step | Owner | Agent time | Machine time | Source |
|---|---|---|---|---|---|
| 1 | Survey the release against what the patch reads | MC one-off (pin move) | 1-2 h | seconds | [I]; the survey commands ran in under 10 s [L, 0112] |
| 2 | Game data: mods, toolkit game folder, pinned database, saves | MC one-off; the owner for their own install | 1-3 h | minutes | [I]; database build ~3 s [C, c5h docs] |
| 3 | Fork branches: i0-ship and x64 on the new release | MC one-off | 0.1 h clean, 1-4 h with conflicts | minutes | [L 9045: 2 min clean]; [I] for conflicts |
| 4 | Port new upstream code to x64 | MC one-off (or bugfix team) | 0.5-2 h | 1 debug build | [I] |
| 5 | Lock files and source fingerprint | MC one-off | 0.25 h | ~1 s | [C 1508], [L 9045] |
| 6 | Builds: x64 (DLL, server, proxy) and 32-bit (plain and `-ia`) | MC one-off | 0.5 h | x64 572 s; 32-bit ~5 min each | [L 9045, 9029] |
| 7 | Narrowing gate | MC one-off; triage bugfix team | 0-1 h | in step 6 | [L 9045]; triage [tracker 1003] |
| 8 | Save-width (`size_t`) census | bugfix team | 0.25 h + pins | 465 s | [L, 0112 at 04b1211b8] |
| 9 | STL drift inventory over the diff | bugfix team | 0.5-1 h | seconds | [I]; full audit 0.4 h [tracker 1600] |
| 10 | Proxy model and generated runtime | owner of `proxy/gen` (team-3300 now; else bugfix team) | 0.5-2 h | ~3 min | [C proxy-design 1]; [I] review |
| 11 | Purity re-audit (D30 lists) | bugfix team | 0.1-3 h | seconds | [L 0111: 3 min for a 24-file diff]; [I] larger |
| 12 | Offline proxy checks (test_proxy_rt, hutest, verify leg) | MC one-off | 0.25 h | ~5 min | [L 9045] |
| 13 | Save test (D10/D24) | MC one-off | 0.1 h | 18-24 min | [L 9029, 9045] |
| 14 | Drift checks and whole-game re-validation x64 = x86 | MC one-off (testbeds) | 0.5 h | ~2.6 h on two testbeds | [L 9045] |
| 15 | Fix checks of carried fixes; old saves load | MC one-off | 0.25 h | minutes | [L 9045, 9029] |
| 16 | Live proxy route on an instance | MC one-off | 0.25 h | ~17 min | [L 9029] |
| 17 | Instances and testbed provisioning | MC one-off; epic owners for their test DLLs | 0.25 h | ~1-5 min per instance | [L 9014, 9029] |
| 18 | Tool defaults and documents | ticket (0133 pattern); MC for COMMON/PERFORMANCE | 0.5-1 h | none | [I] |
| 19 | Performance baselines | performance standards team | 2.2 h | ~28 min of quiet testbed + queue | [L 7010] |
| 20 | Merge, fork fast-forwards, announce | MC | 0.25 h | none | [I] |

**Total for a release:** about 10-25 agent hours and 4-5 hours of machine and testbed time, most of it unattended [I,
the sum of the rows]. A clean pin batch on the same release (9045: 8 fixes, no conflict) took **3.2 agent hours**
[tracker 9045 `actual_h`], batch 1 (17 upstream picks, with bisect builds) 5.2 h plus 1.4 h for the move [tracker 9020,
9029]. What makes a release dearer than a batch: the project's own commits must be carried onto new upstream code (step
3), new upstream code must be ported to x64 (step 4), every model and audit must be redone over a large diff (steps
8-11), the game data changes (step 2), and every baseline and reference game changes (steps 13-19).

## 0a. What `take_release` does of each step (epic 3500, issue 3507, 2026-11-14)

Since epic 3500 the candidate tier of the steps below runs as one command, `python -I tools/release/take_release.py
<tag> --fork <clone>` (`tools/release/README.md`, research `release-driver.md`). Each step is marked here as follows:
- **driver**: a stage does it and needs nobody when nothing has changed;
- **stop**: a stage does it, and on a known kind of change it ends with exit 10 and one line saying what to do;
- **by hand**: no stage does it yet.

"Seen on master" is the dry run on upstream master ba8aafdca (issue 3507, `release-driver.md` W7.2).

| # | Step | Mark | Stage (tier) | What stays by hand | Seen on master |
|---|---|---|---|---|---|
| 1 | Survey | driver | `survey` (candidate) | reading the flagged changes and the upstream PRs; the research file of step 1 | ok, 94 s; the same flags as section 3 |
| 2 | Game data | driver + by hand | `testdata` (candidate): game folder, database of the tag's CP + VP (+ EUI files), candidate descriptor; new reference saves: `validated` (3506) | the localization database (not rebuilt per release, W1a.3); the owner's own install (D26); today also the new release's descriptor, written before the run (issue 3512) | ok, 348 s; the built database differs from 5.4.8's in 148 of 829 tables, and no later stage uses it (3512) |
| 3 | Fork branches | stop | `branches` (candidate): 3505's `branches.py` on `tools/release/series/carry.tsv` | resolving a conflicting `port`/`identity` row and writing a release copy of the series; pushing the branches (MC, D46) | **stop** on 5201480f1 (`SqliteLogger.cpp`), as 3505 found; the release copy then gave 23 applied, 19 dropped, 3 skipped, trees = 3505's `-r2` branches. Issues 3509 (no `--series`) and 3510 (the copy named deleted commits) |
| 4 | Port new code to x64 | by hand (the `build` stage fails on a compile error) | `build` | the port itself (on master: 3505's five upstream-shape commits, already in the series), **including our own code compiled against the release's headers**: the closed-library stubs and the proxy | **failed** twice: the stubs include `FLua.h`, now `#error` (3511, fixed); the proxy relied on `CvLuaSupport.h` for `lua.h` (3518) |
| 5 | Lock files, fingerprint | driver | `lock` (candidate locks in `<work>/deps/`, never `deps/`) | moving `deps/*.lock` (the MC, COMMON 18) | ok, 16 s |
| 6 | Builds | driver | `build`: x64 release through `server/build_server.py`, 32-bit plain and `-ia`, layout under `<work>/layout/` | copying the layout to `<pins_dir>/dlls/` at the pin move. The link: since 0159 (2026-11-14) `build.py` waits for the machine-wide link place itself | x64 release DLL `7d074d9f...` linked (LTO 345 s, `build_all` 554 s); its smoke **failed** on build_all's hard-wired 5.4.8 database (3513), passes on master's |
| 6a | Instruments patch (`-ia`) | stop | `build` (the patch does not apply: stop CL 3) | a new patch base per release (step 3 "Also") | **stop**: 6 files do not apply, among them `Lua/CvLuaSupport.cpp/.h` (issue 3515); the run went on with `--skip-ia` |
| 7 | Narrowing gate | stop | `build` (stop CL 7), `narrowing` | the triage of each new site and the baseline update (`tools/x64-dll/README.md`) | **stop**: 10 new, 9 gone (benign `lua_tointeger` sites), before the link |
| 8 | Save-width census | stop | `census` (stop when `x64-only.txt` is not empty) | pinning a new site (`save-layout-audit.md`) | ok, 260 s: 0 x64-only sites |
| 9 | STL drift inventory | by hand | none | all of it; the drift and whole-game stages are its test | not run |
| 10 | Proxy model | stop | `model` (3503's `regen_model.py`; exit 1 = stop "the model differs") | reading the model diff, new `consumers.tsv` rows and server answers for new engine calls | **stop**: interfaces and slots unchanged, policy table and generated runtime equal, but `lua.json` comes out with 0 methods (issue 3519) |
| 11 | Purity re-audit | driver | `purity` (3503's `derive_purity.py`: drops every entry whose function or callee changed, counted in its report) | reading the dropped entries if they matter for speed (they only cost crossings) | ok, 10 s: 99 of 114 method rows and 1 of 29 Lua rows kept (callee checks drop the rest, the safe direction) |
| - | DLL pack | driver | `pack` (3504's `pack.py`; stop when the tag has no committed 32-bit DLL) | none for a candidate; epic 2600 ships it (`installer-notes.md` sections 6 and 7) | ok, 2 s; the manifest names 5.4.8's DLL `262aee88`, because master commits no new 32-bit DLL |
| 12 | Offline proxy checks | driver | `offline` | none | `test_proxy_rt` 79/79, hutest 46/46 after the `lua.h` fix (3518); the verify leg **fails**: the Lua globals are nil on the proxy route (3519) |
| 13 | Save test | driver + stop | `savetest` (stop when the rows differ from the descriptor's `reference_counts`) | recording a new release's counts in its descriptor | Tiny and Standard **pass** all three parts (35,034 and 350,774 rows; master adds a 16th table); huge **fails**: master does not load the 5.4.8 save (issue 3517, closed on MC #468: a release from master needs its own reference save) |
| 14 | Drift and whole games | driver | `drift` (Tiny pair, candidate); whole games on the testbeds: `validated` (3506) | reading a first differing turn | Tiny pair ok, no differing turn; the 32-bit whole-game proof of the pins on the testbeds is 3507 item 4b |
| 15 | Fix checks, old saves | driver | `fixchecks` | none | 0084 PASS, 0103 identical, 0096 the new build's PASS line; the old-save check **fails** like the huge save test (3517) |
| 16 | Live proxy route | driver | `validated` (3506) | none planned | not run (candidate tier only) |
| 17 | Instances and testbed | by hand (an instance with the tag's mods: `validated`, 3506) | `validated` | provisioning the instances and testbed bases at the pin move | not run |
| 18 | Tool defaults | by hand, smaller | none: since 3501 the tools read the release descriptor (`tools/release/releases/<tag>.json`), so a release changes one file; the pins' defaults follow `deps/*.lock` (0133) | writing the new descriptor; COMMON 18 and PERFORMANCE.md (MC, performance team) | the dry run used a stand-in descriptor |
| 19 | Performance baselines | by hand | none | all of it (performance standards team) | not run |
| 20 | Merge, fork fast-forwards | by hand | none | all of it (MC) | not run |

## 1. What the patch carries on top of Release-5.4.8 [C, `git` in the 9045 fork clone, 2026-11-13]

| Branch (pin) | Commits since Release-5.4.8 | Diff against Release-5.4.8 |
|---|---|---|
| i0-ship (26234371e) | 39 non-merge: 17 upstream fixes cherry-picked with `-x` (batch 1), 22 of the project's own (growth fixes, D25 determinism, D18 sorted writes, D35 statistics pragmas, the 0084/0096/0103/0116 fixes, the rollback-as-SQL commit) | 42 files, +895 -267 (one of them a VP text XML of upstream fix 13472) |
| x64 (04b1211b8) | 51 in all; 6 non-merge commits of its own (b25fd45c0 compile baseline, 8e26515e8 crash handler and pointer casts, 21f396027 and f3d2f5669 save-width pins, 47d7664e9 `size_t` allocation, e9d0c4837 D18) plus the merges of i0-ship | 48 files, +1,036 -334; against i0-ship 13 files, +141 -67 |

List them with `git -C <fork> log --no-merges --format="%h %s" Release-5.4.8..<pin>`. When a new release contains an
upstream fix the project cherry-picked, that commit drops out (check with `git cherry -v <new release> <i0 pin>` or by
patch id); the 22 own commits and the 6 port commits are carried.

Release cadence [C, tag dates in the upstream clone]: 5.4.2 2026-07-28, 5.4.3 08-14, 5.4.4 08-15, 5.4.5 08-21, 5.4.6
08-31, 5.4.7 10-09, 5.4.8 10-16: seven releases in 80 days. Size: 5.4.4 -> 5.4.5 13 commits (8 DLL files), 5.4.5 -> 5.4.6
21 (23 files, +892 -665), 5.4.6 -> 5.4.7 34 (38 files, +759 -307), 5.4.7 -> 5.4.8 3 (2 files, +6). Since 5.4.8,
`upstream/master` 16596561f has 151 commits, 77 of them in the DLL folders, **141 DLL files changed, +14,767 -7,552**:
the next release is much larger than any of the last four [C]. Section 3 lists what it changes in the parts the patch
reads.

## 2. The checklist

Every build, suite, game or binary of one's own starts through a gate or `quietlock.py run` without `--take` (COMMON 16,
EFFECTIVENESS E1); never two heavy jobs of one agent at once (a first attempt of 9020 ran four builds at once and ran out
of commit memory). Fork rules (AGENTS section 3): new branches only; never commit to the fork's `master` or the
upstream-tracking branches; fast-forwarding `i0-ship`, `x64` and `master` is the MC's step (D46).

### Step 1. Survey the release (also for a pin batch)

- **What**: which of the release's changes touch what the patch reads or carries. Upstream PR discussions explain intent
  (`gh` read only, as 9020 did).
- **Tool**: `git` over the upstream clone. The checks of section 3 are these commands (run 2026-11-13, each under a second):
  - boundary headers (proxy model input): `git diff --stat <old> <new> -- "CvGameCoreDLLUtil/include/CvDll*"
    "CvGameCoreDLLUtil/include/ICv*" "CvGameCoreDLL_Expansion2/CvDll*.h" "FirePlace/include/FireWorks/*"`;
  - Lua registrations: `git grep -h -o "Method(\w*)" <rev> -- "CvGameCoreDLL_Expansion2/Lua/*.cpp" | wc -l` at both revs;
  - engine calls: the `gDLL->X(` / `DLLUI->X(` / `GC.getDLLIFace()->X(` / `GC.GetEngineUserInterface()->X(` names at both
    revs, `comm -13` for new ones;
  - serialization headers: `CvSerialize.h`, `FStlContainerSerialization.h`, `CvInfosSerializationHelper.h`, `FDataStream.h`;
  - new and deleted sources: `git diff --name-status <old> <new> -- CvGameCoreDLL_Expansion2 CvGameCoreDLLUtil FirePlace`,
    and `build_vp_clang.py` (its `CPP = [...]` list is what `tools/x64-dll/build.py` compiles [C, build.py `cpp_list`],
    and part of the source fingerprint);
  - overlap with the patch: `git diff --name-only Release-5.4.8 <x64 pin>` (fork) against `git diff --name-only <old>
    <new>` (upstream), `comm -12`: these files can conflict in step 3.
- **Owner**: the one-off that moves the pin (9020, 9029, 9045 were MC one-offs).
- **Time**: 1-2 agent hours for a release this size [I]; minutes for a batch.
- **Done when**: a file in `research/03-dll-engine-boundary/` (as `upstream-fix-batch-1.md` section 2) lists every change
  that touches the boundary, saves, the Lua surface, the build, or a file the patch changes.

### Step 2. Game data: mods, the toolkit game folder, the pinned database, saves (release only)

- **What**: the DLL must match the installed mods and the save (`build-and-install-dll.md` section 3). Every test path
  is built for VP 5.4.8 today [C, `grep` 2026-11-13]: the toolkit game folder `VP_MODPACK_5_4_8_NEUTRALS` and
  `MODS_COMMIT = '7307aefdf'` in `tools/d10/run_d10.py`; the pinned database `civ5-headless/pinned/vp-modpack-5.4.8-cdrift/
  Civ5DebugDatabase.db` (`bd9b8aa1...`, `SQLITE_LOGGING = 1`) and its `Localization-Merged.db` in `run_d10.py`,
  `tools/drift/run_huge.py`, `run_pair.py`, `server/run_server.py` (`DEFAULT_LOC_DB`, `--db`), `perf/turnbench/turnbench.py`
  (`PINNED_DB`), `tools/x64-dll/build_all.py` (`testdb/vp548-cdrift-...`), `tools/i0/ab.py`, `tools/x64-dll/cvgamedatabase/ab/
  run_ab.py`, `cvlocalization/ab/run_ab.py`; `server/tools/explore_sets.py --strategies` default 37 (VP 5.4.8's strategy
  count); `tools/instances/provision.py` (mods form of VP 5.4.8 for the owner's reference save); `tools/testbed/jobs/longgames.py`
  (the toolkit folder's map scripts); the quiet testbed's bases `toolkit-game` and `civ5-headless` (`quiet-testbed.md`).
- **Tool**: the new database with civ5-headless's builder: `c5h dbbuild --mods-dir <MODS copy> --mod <GUID>:<version> ...
  --out <db>` (`~/Documents/civ5-headless/docs/database-builder.md`: about 3 s); then set `SQLITE_LOGGING = 1` as the cdrift
  copy has it. The mods registry for c5h runs: `--mod d1b6328c-ff44-4b0d-aad7-c657f83610cd:<CP version> --mod
  8411a7a8-dad3-4622-a18e-fcc18324c799:<VP version>` (5.4.8: CP v151, VP v17; `server-host.md` 12.3); the fork's source
  folders `(1) Community Patch` and `(2) Vox Populi` give the same registry as a MODS copy [C, 12.3].
- **The core database pin; re-pin after a game update** (ticket 0612, 2026-11-20): the `testdata` stage reads only a
  pinned base-game `Civ5CoreDatabase.db` (`settings.py` `core_db`, hash in `stages/testdata.py` `CORE_DB_PINS`; today
  `dec46218...`, for the build before 26577625 and for 26577625) and refuses any other. After a Firaxis update or a VP
  release that changes base-game XML: take the core cache of an instance of the new build whose cache was written
  before its mods database (base only: no `CustomModOptions` table, 365 tables in today's form, never a cache written
  by a mod-activated or modpack start), compare its content with the pinned file (every table's rows; the file hash
  differs at every rebuild even when the rows do not; script and method in `release-driver.md` W1a.3, 0612). Same
  rows: nothing to do. Different rows: copy it to a durable local file named by its hash, add the hash to
  `CORE_DB_PINS` with the build it serves, point `core_db` at it, and run one `testdata` stage (accepted failures:
  the `WB_Script` map scripts only).
- **Saves**: reference saves (`9020-HUGE_0350`, `TB-HUGE_0350`, the owner's `VP8P-HUGE_0350`) were written by 5.4.8. Whether
  the new release loads them is the release's own promise, not tested by us; a new huge reference save needs a long
  game (hours) [I]. Never move or overwrite the owner's saves; copies with their own names (AGENTS section 4).
  **Release-5.5 does not load any 5.4.8 save** (upstream 0848d52cc adds an unguarded field to `CvPlayer::Serialize`;
  impl-0766, 2026-11-25, `[C]`), so every save the tiers read was made again. The long ones (the huge T350 reference,
  the Large T200 bases of the HI large scenes, both scenes) took about 3 agent hours and **74 min of machine time for
  the longest** (headless in civ5-headless with the tag's 32-bit DLL, three games at once): recipe, traps and hashes in
  [human-input-testing.md](human-input-testing.md) 8.11 (ticket 0771) `[L]`. (Corrects "hours" above for the headless
  route, 2026-11-25.)
- **The committed `.modinfo` is not the release's** [C, L; queue ticket 3515, 2026-11-14, upstream master ba8aafdca]:
  master's committed CP `(1) Community Patch (v 151).modinfo` lacks four VFS imports that `Community Patch.civ5proj`
  has: `Gamecore/GamecoreInit.lua`, `Gamecore/GamecorePostProcess.lua` (#13489's Lua API bootstrap) and
  `Assets/Yields/YieldAtlas_128_Culture.dds`, `..._Faith.dds` (VP and EUI-compatibility modinfo match their projects;
  `tools/d10/evidence/3515/scripts/s16_modinfo.py`). ModBuddy rebuilds every modinfo from its project at a release
  (`NEW RELEASE INSTRUCTIONS.txt` line 167), so an installed release has them; a test install made from the repository
  folders does not. With the committed modinfo the master DLL logs `Error loading GamecoreInit.lua`, every Lua state has
  `Game`, `Map`, `Players`, `DB` nil, the map script fails, and the game starts with no living major civ: the DLL's
  assert `CvTeam.cpp:888` "Zero possible players?" (`DoBarbarianTech`) then blocks it behind a dialog [L, Goldberg c5h-c,
  new Tiny game, the `-ia` build of 8be73b6f5]. That assert is a consequence, not a master bug: with the four imports
  added (and the instance's `Civ5ModsDatabase.db` cache removed) the same DLL and mods play AI turns with no assert and the
  full Lua API [L, c5h-b; `headless-and-parallel-testing.md` 6.1d]. So a test path that installs a release cut from
  master from the fork's folders (instances, the testbed, `testdata`) must regenerate the modinfo from the `.civ5proj` or
  add the missing imports; a released tag's modinfo should be checked the same way. (Corrected 2026-11-15 from step
  10b, issue 0308: on the proxy route 3519's nil `Game`, `Map`, `Players` was the scratch registration, not this gap;
  replaces "Very likely also the cause of 3519's nil ... [I, not tested]".)
- **The driver does it** [C, queue ticket 0310, 2026-11-15; tests in `tools/release/tests/`]: `tools/release/stages/
  _modinfo.py` gives a mod folder's `.modinfo` every file its `.civ5proj` imports into the VFS (`<Content>` with
  `<ImportIntoVFS>True`) that the modinfo does not list, as `<File md5="<MD5>" import="1">path</File>` before
  `</Files>`, and sets `import="1"` on a listed file the project imports; nothing else changes (BOM, line ends,
  indentation and path separator kept; a project file missing from the folder is reported, never added; a matching
  modinfo is not rewritten). This is 3515's `s18_fixmodinfo.py` made general, not a full ModBuddy rebuild (Actions,
  EntryPoints and the md5 of existing lines stay as committed). Used by: the `testdata` stage on every mod folder it
  extracts from the tag (before the database build and the modpack copy; `testdata.json` step `mods` records
  `modinfo_regenerated` per folder), and the `instance` stage on the descriptor's mod folders of the claimed instance
  (project from the folder, an install from the fork's folders, else the `testdata` stage's copy of the tag's folder;
  a changed modinfo and the instance's `cache/Civ5ModsDatabase.db` are backed up, the cache removed, both restored by
  SHA-256 at the end). By hand: `python -I tools/release/stages/_modinfo.py check|fix FOLDER... [--project P]`.
  **A 5.4.8 release is unchanged** [C, 2026-11-15, `_modinfo.py check`]: the Release-5.4.8 (7307aefdf) folders
  `(1) Community Patch` and `(2) Vox Populi` (run_d10's copy `D:/c5-64bit-work/9045/d10/mods/`) and `(3a) VP - EUI
  Compatibility Files` (9045's `src-x64`) each match their project: nothing to add, nothing to set. Not run on a real
  master tree here (no master checkout on this machine's work drives; the 3515 finding and the hermetic tests cover the
  case) [I for master until the next dry run]. Evidence: `evidence/0310/acceptance.txt`.
- **Owner**: the one-off; the owner decides whether and when their Steam install and its MODS move to the new release (their saves
  and mods). Instances and testbed bases follow in step 17.
- **Time**: 1-3 agent hours [I]; new reference saves: hours of play, unattended [I].

### Step 3. Fork branches (release; a batch skips the re-cut)

- **What**: `i0-ship-<rel>` cut from the new release tag, the project's own i0 commits cherry-picked with `-x` in their
  original order; `x64-<rel>` = the 6 x64 commits applied to that base, then `i0-ship-<rel>` merged with `--no-ff` (every
  x64 pin took i0-ship by merge, `deps/dll.lock` comments). The batch route (9020, 9045): picks onto the i0 pin, merge into
  the x64 pin, `git show --remerge-diff` empty; check that the x64 tree equals the fix commits cherry-picked onto the x64
  pin (9045 8a).
- **Tool**: `git -C <fork clone> cherry-pick -x <commits>`; `git merge --no-ff`; `git show --remerge-diff <merge>`;
  push new branches to the fork (9045's `s02_branches.sh`, `s03_push.sh` in `tools/d10/evidence/9045/scripts/`).
- **Owner**: the one-off; the MC fast-forwards the fork's `i0-ship`, `x64` and `master` at the merge (D46).
- **Time**: clean picks and merges take minutes [L, 9045: both branches 10:26-10:28]. A release: 38 files that the
  patch changes were also changed by `upstream/master` since 5.4.8 [C, section 3], among them `CvGame.cpp/.h`,
  `CvPlayer.cpp/.h`, `CvTacticalAI.cpp`, `CvAStar.cpp`, `CvEspionageClasses.cpp`, `SqliteLogger.cpp`, `FFastVector.h`, so
  conflicts are likely: 1-4 agent hours [I].
- **Also**: the instance-aware instruments patch (`ia-<base>-440878983.patch`, `headless-and-parallel-testing.md` 6.1b) was
  made against Release-5.4.8 (7307aefdf); on batch 2 it needed three hand resolutions with `git apply -3` (9045 8b,
  `s06_ia_resolve.py`), and the order of the instruments' pollers and 0116's stamp in `CvGame::update()` matters. A release
  needs a new patch base. *Measured 2026-11-14 (queue ticket 3515; replaces "[I: likely more conflicts]"):* on upstream
  master (tree eb8ef8e73) six files conflict, two of them the Lua API refactor's `CvLuaSupport.cpp/.h`; resolved and the
  Lua hook moved to `NotifyDatabaseCached` in about 1 agent hour, `-ia` build 527 s, boot checked on an instance:
  `tools/d10/evidence/3515/ia-8be73b6f5-440878983.patch`, `headless-and-parallel-testing.md` 6.1d. Normalise the
  committed patch to LF before `git apply -3` (a Windows checkout gives CRLF and the three-way merge falls back).
  *The driver picks the patch by base* [C, queue ticket 0310, 2026-11-15; tests in `tools/release/tests/`]: with no
  `--ia-patch` (default `by-base`), the `build` stage's `-ia` step takes the row of `IA_PATCHES` in
  `tools/release/stages/build.py` whose upstream base is the newest one the i0 commit contains (`git merge-base
  --is-ancestor` in the fork): Release-5.4.8 7307aefdf gives 9045's `ia-ee893bc18-440878983.patch` (every 5.4.8-based
  pin), upstream master ba8aafdca gives 3515's `ia-8be73b6f5-440878983.patch` (an i0 branch cut from master at or after
  it). The choice is logged (`-ia instruments patch: ...`) and recorded (`build` outputs `ia_patch`, `ia_patch_choice`);
  an i0 commit that contains neither base is a stop of CL 3; `--ia-patch FILE` still wins; a work folder that recorded
  the old default (9045's patch) is read as `by-base`. A later master that 3515's patch no longer applies to stops at
  `ia-apply` as before: resolve it (3515's `s08_ia_resolve.py`), then add its row to `IA_PATCHES` or pass `--ia-patch`.
- **Done when**: both branches pushed; each own commit present once (`git log --no-merges`), each pick of an upstream fix
  that the release already contains dropped with a note.

### Step 4. Port new upstream code to x64 (release; a batch rarely)

- **What**: code that compiles for x86 but not for x64 (pointer truncation, inline assembly, x86-only APIs), and code that
  touches the EXE or the closed libraries, which the server does not have.
- **Tool**: `python -I tools/x64-dll/build.py --src <x64-<rel> worktree> --unlocked --config debug --out <dir>` (debug
  build 51 s [C x64-dll-port 0.1]); with `--census` the closed-library census: `unresolved.txt` lists any closed-library
  symbol the stubs and replacements do not cover (today 0; the release build fails on any, `/ERRORLIMIT:0`).
- **Known for the next release** [C source, I that it fails]: upstream PR 13379 (`CvGameCoreDLL_Expansion2/Exe/`, section 3)
  casts pointers to `DWORD` four times (`CvExe.cpp:115, 200, 267`, `CvExeBuild.cpp:28`), which clang-cl `-m64` rejects.
- **Owner**: the one-off; the bugfix team when the port is more than a few sites.
- **Time**: 0.5-2 agent hours [I]; the first x64 port (epic 1000, issues 1002-1004) was a team's day.

### Step 5. Lock files and the source fingerprint (also for a batch)

- **What**: `deps/i0.lock` and `deps/dll.lock`: `branch`, `commit`, `describe`, `base` (`Release-5.4.8 (7307aefdf)` today),
  `fingerprint`, then after step 6 `tools`, `epoch`, `verified`, `release`; the replaced pin kept in a dated comment.
- **Tool**: `python tools/x64-dll/src_fingerprint.py --git <fork clone> [--commit <c>]` (about 1 s; every script that takes
  `--src` refuses a copy that does not match, issue 1508 [C]). Source copies: `git -C <fork> archive -o <tmp>/src.tar <c>`
  then `tar -x`.
- **Owner**: the one-off; the MC merges. **Time**: 0.25 agent hours [I; 9045 moved them inside 10:28-11:27 with the builds].

### Step 6. Builds (also for a batch)

- **What and tool**:
  - x64 DLL, libraries, server and proxy in one command: `python -I server/build_server.py --src <git archive copy> --out
    <out> --config release --offline --jobs 6` (gated). `build_all.json` must show the steps stubs, cvgamedatabase,
    cvlocalization, dll, host, smoke all rc 0, 0 unresolved, the narrowing gate (step 7), smoke exit 0; then the server and
    proxy steps. **572 s** [L, 9045 2026-11-13 11:45-11:54], 570 s [L, 9029]; the full-LTO link alone 440-507 s.
  - 32-bit: the fork's `build_vp_clang.py --config release` in a detached worktree (version "... Clean"), and the `-ia`
    variant (git archive + `git apply` of the instruments patch + a version stub; `update_commit_id.bat` stubbed): about 5
    min each [C x64-dll-port 3.2, build-and-install-dll.md], run under `quietlock.py run` (9045's `build9045.py`).
  - Lay out `<pins_dir>/dlls/x64-<commit8>-p<tools8>/` (`bin/`, `proxy/bin/`, `SHA256.txt`, `README.txt`) and
    `i0ship-<commit8>` / `i0ship-<commit8>-ia` (the names `provision.pinned_dll()` derives from the lock) (9045's
    `s22_layout_final.py`).
- **Watch for**: the x64 DLL is not byte-repeatable across folders (9045 8c: the stub libraries' COFF timestamps or the
  environment); the hash in the lock names one build. A DLL that imports a new `CvGameDatabase` member is refused by the
  server's remote-mode write recording (`server/src/remote/srv_remote_db.cpp`, 9045 8d): step 12's verify leg shows it.
- **Owner**: the one-off. **Time**: 0.5 agent hours; about 20 min of machine time, one build at a time [L, 9045].

### Step 7. The narrowing gate (also for a batch)

- **What**: `-Wshorten-64-to-32` sites against `tools/x64-dll/narrowing-baseline.tsv` (798 sites at e9d0c4837; 794 at the
  pin, 0 new, 4 gone [L 9045]). A new site stops the build with exit 3 and its name.
- **Tool**: part of step 6 (`build.py --shorten`); after a triage of each new site (pattern: `tools/x64-dll/
  truncation-triage.md`), `build.py --shorten --write-narrowing-baseline` (`tools/x64-dll/README.md`).
- **Owner**: the one-off; the bugfix team for the triage. **Time**: 0 for a batch with no new site [L]; the first triage of
  794 sites took 0.45 agent hours [tracker 1003 `actual_h`]; a release with 141 changed DLL files: 0.25-1 h [I].

### Step 8. The save-width (`size_t`) census (release; a batch when it touches saved members)

- **What**: every place where the DLL writes or reads a 64-bit integer through `FDataStream`; a site that is 64-bit only on
  x64 changes the save bytes and breaks D10. Pass = `x64-only.txt` empty.
- **Tool**: `python -I tools/x64-dll/save-audit/save-width-census.py <source copy> <out> --arch both --jobs 16` (under
  `quietlock.py run`; set `VSCMD_SKIP_SENDTELEMETRY=1` until bug 0147 adds it to the script). Writes `x86-sites.txt`,
  `x64-sites.txt`, `x64-only.txt`. New sites are pinned as in `tools/x64-dll/save-layout-audit.md` (`FStlSerializedCount()`,
  32-bit members).
- **Measured** [L, 0112, 2026-11-13 20:24:14-20:31:59]: on the plain copy of the pin 04b1211b8 (`D:/c5-64bit-work/9045/src-x64f`,
  fingerprint-matched by 9045), `--jobs 16`, beside another team's x64 release build and two games (an upper bound):
  **465 s** (x86 about 3 min, x64 about 5 min), exit 0, no compile error in either log. **0 x64-only sites**; 9 sites on
  each build, the same 9 as the 2026-11-10 run after all pins (`tools/x64-dll/evidence/2026-11-10-save-census/
  allpins-x64-sites.txt`): `m_lJONSCultureEverGeneratedTimes100` (sync var, `CvPlayer.h:3888`, was 3854),
  `CvPlayerCulture::Serialize` and `CvPlayer::Serialize` (genuine 64-bit members, lines moved), `CvRandom::Serialize`
  (`m_ullRandomSeed`). So the save widths hold at the current pin [C]; the census had not been run since f3d2f5669.
  Output `D:/c5-64bit-work/0112/census/` (not committed).
- **Owner**: bugfix team (issue 1004 did the first one, 0.6 agent hours). **Time**: 0.25 agent hours plus the pins [I].

### Step 9. STL drift inventory over the diff (release)

- **What**: new unordered-container iteration, pointer-ordered containers, sorts with pointer comparisons or non-strict
  comparators, CRT maths in decisions (`stl-drift-sites.md`, the 51 sites and their risk classes).
- **Tool**: the `git grep -nP` patterns of `stl-drift-sites.md` section 10, run over the changed files only, then a read of
  each hit; the x86 = x64 games of step 14 are the test.
- **Owner**: bugfix team. **Time**: 0.5-1 agent hours [I]; the full first audit took 0.4 agent hours [tracker 1600].

### Step 10. Proxy model and generated runtime (release; a batch when a boundary header or an engine call changes)

- **What**: the EXE<->DLL interface model (`proxy/model/interfaces.json`, `lua.json`, `slot-proof.json`, `model.json`,
  `policy-table.tsv`) and the generated runtime. Any change of an interface method changes the schema hash
  (`kAppSchemaHash`): the proxy and the server must then come from one build (they do in step 6).
- **Tool** (`proxy-design.md` section 1, "all regenerable, ~3 minutes"):
  `python proxy/gen/extract_model.py`; `python proxy/gen/verify_slots.py` (clang's slots against MSVC's vcall thunks, both
  arches); `python proxy/gen/extract_lua.py`; `python proxy/gen/classify.py` (or `apply_consumers.py` while bug 0125 is
  open); `python proxy/gen/gen_runtime.py --out <dir> --src <source copy> --compile-check --objdir <dir>` (about 2 s plus
  the compile check). Diff the regenerated files against the committed ones and read every change.
- **Before running**: `proxy/gen/toolchain.py` names the source by constant, `FORK =
  %USERPROFILE%/Documents/GitHub/Community-Patch-DLL-x64` [path scrubbed 2026-11-17] and `PINNED_COMMIT = e9d0c4837` [C, 2026-11-13]: the committed model
  was made at e9d0c4837 and has not been regenerated at any later pin (9029 edited one row by hand). Point it at the new
  source; bug 0125 makes `classify.py` reproduce the committed table at the pin.
- **At every pin move, write the model even when it comes out EQUAL** (ticket 0602, 2026-11-20) [C]: `python -I
  proxy/gen/regen_model.py --src <source at the new pin> --lock deps/dll.lock --out <dir> --write` and commit
  `proxy/model/` with the new locks. EQUAL masks `source.commit`, `generated_at`, `fork_path` and the AST counts, so an
  equal model still has to be written: those fields name the commit the model was made from, and
  `tools/harness/tests/test_hi_coverage.py::test_the_repository_model_is_matched_whole` (and the HI coverage's
  boundary measure, which reports a pin mismatch) compare `interfaces.json`'s `source.commit` with `deps/dll.lock`'s
  `commit`. Pin batch 5 (0551, 2026-11-18) ran the check without `--write` ("nothing regenerated") and left the model at
  c29442e8c behind the 1fd51353e pin: that test failed on main from then until 0602 wrote 0551's own `regen_model` output
  at 1fd51353e (its model stage of the validated take, report EQUAL; only those masked fields changed). The same step is
  to go into the Re-pinning comment of `deps/dll.lock` (the MC adds it after the parked branches that change that file).
- **For each new DLL->engine call**: a row in `proxy/model/consumers.tsv` (exe, renderer or both; issue 3303) and a server
  answer (as 9029 did for `TryGameCoreLock`, `upstream-fix-batch-1.md` section 3). For each new interface method: a hook or
  handler where the generator cannot marshal it (`gen_runtime.py --list-loud`).
- **Owner**: the owner of `proxy/gen` and `proxy/model` (team-3300 while its epic runs; then the bugfix team).
- **Time**: about 3 min of machine time [C proxy-design 1]; 0.5-2 agent hours to read the diff [I]. The first model took
  0.6 + 0.37 + 0.5 agent hours (issues 2101, 2102, 2103).

#### 10a. The Lua model on upstream master's Lua API (issue 3519, 2026-11-14)

The dry run of 3507 (W7.2 row 14) found `lua.json` with 0 methods on master. Measured on the master port's x64 tree
`x64-release-master-ba8aafdc-r2` 76b3a5f70 (= the dry run's `x64-release-5.4.9-ce4e6aae0` tree, upstream master
ba8aafdca) and on the pin 04b1211b8, sources in `C:/c5-64bit-work/3519/` (deleted at hand-back), the extractor of branch
`task-3519-extract-lua-master`:

- **Cause [C, source and preprocessed text]**: upstream #13489 (`Lua/!CvLuaApi.h`, `!CvLuaApiExec.h`,
  `!CvLuaApiInstanceTemplates.h`) changed what `Method(Name)` expands to. Pin: `lua_pushcclosure(L, lName, 0);
  lua_setfield(L, t, "Name");` (instance libraries) and `RegisterMethod(L, lName, "Name");` (Game, Map). Master: every
  library fills its `CreateFunctable(L)` table at stack index 1, `lua_setfield(L, 1, "Name")`, Game and Map included, and
  `!CvLuaApi.h` has `#define lua_pushcclosure luaVp_pushcclosure`, so the preprocessed text reads `luaVp_pushcclosure(L,
  lName, 0)`. The extractor's pattern wanted `lua_pushcclosure` and the table `t`: 0 matches.
- **Second effect [C]**: master spells the instance helpers differently: `luaEx_toCity(L, 2)` for
  `CvLuaCity::GetInstance(L, 2)`, `luaEx_pushPlot(L, p)` for `CvLuaPlot::Push(L, p)`, `luaEx_exec<T>(L, &C::m)` for
  `BasicLuaMethod<R, T>(L, &C::m)`, `luaEx_to<T>(L, n)` for `toValue<T>(L, n)`. With only the pattern fixed, 1,133
  methods master did not change came out `access: unknown` (the free `luaEx_*` functions are unclassified callees) and
  lost their instance arguments and results. The extractor now reads the type names from master's own declarations
  (`LuaTypes_DeclareInstanced(tag, Name, CLASS)` in `!CvLuaApi.h`, `LuaTypes_DefineInstanced(CLASS, CvLua<Lib>)` in each
  `CvLua<Lib>.cpp`: Area, City, Deal, League, Player and PlayerBase -> CvLuaPlayer, Plot, Team, TeamTechs -> CvLuaTeamTech,
  Unit), rewrites the four spellings into the pin's before its textual parse (`normalise_body`), and counts `luaEx_*`,
  `luaVp_*`, the free `BasicLuaMethod` and `LuaSupport::TrackDllFinalizer` as Lua plumbing, the template classes
  `CvLuaDynamicInstance`, `CvLuaStaticInstance`, `LuaTaggedAutoPointer`, `TypeGlobals` as wrapper classes.
- **New on master, now in the model**: the 5 `Exe.*` functions of `CvLuaExe.cpp` (methods with `registration:
  exe_global`, library `Exe`); the 6 `vputils` functions streamed with `luafsq_func`/`luafsq_wrap` in `CvLuaSupport.cpp`
  (`counts.luafsq_registrations`: vpnewproxy, HelloWorld, hasGamecoreLock, SetFaith, SetFaithT100, GetProdTurnsForUnit);
  `counts.lua_api_form` (the type map). These two `counts` keys are written only for master's form, so a pin source
  regenerates the committed file unchanged.
- **Results [L, 2026-11-14 22:43-22:50 EST, `regen_model.py --src <tree> --out <dir>`, x86 and x64 configurations;
  `test_policy_table.py` at the pin 13/13, `test_purity.py --only regen` 3/3, `gen_pure_list.py --check` ok, the new
  `proxy/gen/tests/test_extract_lua.py` 15/15]**:

| Source | `Method()` tokens | Compiled registrations x86 / x64 | Methods in `lua.json` | `regen_model.py` |
|---|---|---|---|---|
| pin 04b1211b8 | 3,481 | 3,463 / 3,463 | 3,467 | **EQUAL** (exit 0, 246 s): every model file equals `proxy/model/` |
| master port 76b3a5f70 | 3,489 | **3,471 / 3,471** (0 before) | 3,480 | DIFFERENT (exit 1, 161 s): interfaces 15/16 line moves and `CvAStar` x64 (as the dry run); `slot-proof.json`, `policy-table.tsv`, the generated runtime equal; `lua.json`/`model.json` differ |

  By library on master: Area 25, City 616, Deal 61, Game 378, League 61, Map 38, Player 1,275, Plot 278, Team 181,
  TeamTech 21, Unit 537; reconciliation as at the pin (7 commented out, 11 macro definitions, 2 duplicate
  registrations). Methods: 2 gone (`Game.IsExeWantForceResyncAvailable`, `Game.SetExeWantForceResyncValue`), 15 new
  (10 worker-plan `Player.*`: `GetImprovementOptions`, `GetWorkerPlan`, `GetWorkerPlanOverview`, `GetWorkerPlanRevision`,
  `IsWorkerPlanSetting`, `SendAllPlannedRouteStates`, `SendClearWorkerPlans`, `SendRefreshWorkerPlans`, `SendWorkerPlan`,
  `SendWorkerPlanSetting`; the 5 `Exe.*`). Of the 3,465 common methods, compared by id with `line`, `registered_at`
  and `registration` left out (`registration` is `Method` for all 416 Game and Map methods on master, `RegisterMethod`
  at the pin: the source's form changed): **3,406 equal the pin's record**; **25 differ because master changed the
  method or a callee's signature** (for example `Map.GetPlot`/`GetArea`/`FindBiggestArea` and `Game.GetLeague` lost
  their nil branch, `Plot.SetPlannedRouteState` is no longer a `BasicLuaMethod`, `CvCity::GetFaithPurchaseCost` gained a
  parameter); 1 differs through a header (`Player.GetReplayData`: `CvReplayTurnData`'s container type); **33 differ
  with an unchanged body**: the pin's 40 unqualified `toValue<T>(L, n)` calls are not read by the textual parse (only
  `CvLuaArgs::toValue<T>` is), while master spells all 48 as `luaEx_to<T>`, which is read, so master's record has the
  argument the pin's lacks. *(Corrected 2026-11-16, issue 0309: the parse now reads the unqualified spelling and the
  pin model is regenerated with it (36 methods gained their arguments; `regen_model.py` EQUAL at the pin); on master
  3,439 of the 3,465 common methods equal the pin's record and 26 differ (the 25 above and `Player.GetReplayData`); the
  33 are equal. Details in `research/03-dll-engine-boundary/proxy-design.md` section 1b. Replaced: "a decision for the
  next model commit, not done here", MC's answer in 3519's thread, 2026-11-14.)* The 35 rows of `lua-pure-bindings.tsv`
  all exist on master; 34 have the pin's record, `Map.GetPlot` changed (its results).
- **What `regen_model.py`'s report does not show**: it compares lists by position and prints at most 40 paths per file,
  so on master the 15 added methods shift every later index and the report lists only counts and
  `other_registrations`. The method-by-method comparison above is a keyed comparison (scripts of 3519, not kept).
- **Purity (step 11) on master** [C, the dry run's row 20]: `derive_purity.py` dropped 13 Lua rows on `BasicLuaMethod`
  and 8 on `GetInstance`, master's refactor of those helpers (the callee check sees a changed body); not touched by 3519.
- **The proxy route on master** (W7.2 row 22: `Game`, `Map`, `Players` nil in the 32-bit host) [C each link; the chain
  confirmed live by 0308 on 2026-11-14, 10b, which replaced "I the chain, not reproduced live"]: master's `LuaSupport::NotifyDatabaseCached()` sets `skipGamecoreCreation` around
  `CreateLuaThread("!VPDLL")`, during which `RegisterScriptData` registers nothing (the engine registers the DLL's
  libraries in every new thread). On the proxy route that `CreateLuaThread` is forwarded to the engine, whose nested
  `RegisterScriptLibraries` reaches the server as `kRt_LuaRegister`; the server registers its per-thread scratch state
  once (`RegisteredCtx`, `srv_remote_lua.cpp`) and so keeps an empty one for every later state. Behind that, master's API
  no longer matches the bridge's model: every DLL function is a C closure with one upvalue (it crosses as `kLSRef`, not
  `kLCFunc`, and the UI-Lua mirror names none), Game, Map and instances are full userdata with registry metatables
  (`vpdllinternals`), not `__instance` tables with `LuaTypes`, and `GamecoreInit.lua` runs in the engine against a
  registry that is in the server. Proposal: `tracker/issues/NEW-proxy-lua-bridge-master-lua-api.md` (numbered 0308;
  its live results are in 10b). *(2026-11-25: the closures 0313, the instances 0314, GamecoreInit.lua's registry,
  `vputils` and `gc_post` 0315 (`proxy-design.md` 32, 35, 36); a release from master is checked live against a 32-bit
  master game as 0315 did: `research/03-dll-engine-boundary/evidence/0315/` and runbook 2.6.)*

#### 10b. The proxy Lua route on master, live (issue 0308, 2026-11-14)

Setup [C, 2026-11-14 23:05-]: scratch `C:/c5-64bit-work/0308/` (D111, deleted at hand-back): a `git clone --shared` of
the local fork clone `Community-Patch-DLL-x64` with `x64-release-master-ba8aafdc-r2` 76b3a5f70 fetched from the fork;
detached worktrees `src-pin` (04b1211b8) and `src-master` (76b3a5f70); a local lightweight tag `Release-5.4.9` =
ba8aafdca in that clone only (never pushed), as the 3507 dry run did. Builds, all `--config release --offline`, one at a
time: `build_all.py` of each tree once (master with `C5_RELEASE_TEST_DB` = master's test database from the release
driver's `testdata` stage, and `--no-gate` in place of the dry run's 10 extra narrowing-baseline rows), then
`build_server.py --deps <that build_all>` for (a) the pin with the fix, (b) master with the fix, (c) master with main
043b0f13's unfixed server (built from a `git archive` of main, `--repo-commit`). Runs: checklist step 12's commands on
each, and the verify leg on master with the 5.4.8 game folder (the dry run's default) and with the `testdata` stage's
game folder (`VP_MODPACK_5_4_9`, whose DLC scan finds `Gamecore/GamecoreInit.lua` whatever the `.modinfo` lists).
Evidence (the run logs, each run's `summary.json`, `server-report.txt` and host-report extract, the server's Lua lines,
both `proxy-lua-calls.tsv`, the hutest and rt logs, the scripts): `evidence/0308/`; the build and run folders were
deleted at the hand-back.

Builds [L, 2026-11-14 23:10-23:37 EST, workstation]: pin `build_all` every step rc 0 (DLL 622.8 s with the LTO link,
narrowing 0 new / 4 gone, smoke on the 5.4.8 database rc 0), DLL `444ccf51...`; master `testdata` ok in 126 s (database
**`1b9b01cb...`, identical to the dry run's**, game copy 99 s, snapshot 6 s, dbbuild 9 s); master `build_all` every step
rc 0 (DLL 456.7 s, smoke on master's database rc 0 in 2.9 s), DLL `2cf3f27a...` (not the dry run's `7d074d9f...`:
`--no-gate` drops `-Wshorten-64-to-32` and the folders differ; not compared further); each `build_server.py --deps` 70-97 s.

Results [L, 2026-11-14 23:37-23:43 EST; Tiny = `server/specs/continents-tiny-fixed-seed.json`, turns 0-50, step 12's
`--ini` set, `--fail-on-unexpected`; master runs with master's database, pin runs with the 5.4.8 descriptor's]:

| Build | Check | Result |
|---|---|---|
| master, main's server (unfixed) | verify, 5.4.8 game folder | **FAIL as the dry run**: 6 host Lua errors (`Map`, `Players`, `Game` nil in Map Script, base-demographics, the digest, T000 map, the checkpoint, arm-autoplay), 1 server scratch error (`CallFunction failed`), "SetAIAutoPlay did not arm autoplay"; 12 s |
| master, main's server | verify, testdata game folder (GamecoreInit.lua loads: `!VPDLL: GAMECOREINIT.LUA!` in Lua.log) | **the same FAIL**, the same 6 errors |
| master, main's server | hutest (the new check) | **FAIL** `lua_register_globals` (HtMarker 0) after `lua_register_skipped_first` ok; the host then stops (exit 1): the negative case of the regression check |
| master, fixed server | verify, testdata game folder | **PASS** in 28 s: host Lua errors 0, server scratch Lua errors 0, 3,121,473 kRt_LuaCall, 4 empty registrations repeated |
| master, fixed server | verify, 5.4.8 game folder (`LoadFile: GamecoreInit.lua not found`) | FAIL, 2 host Lua errors at turn 0 after map generation: "Headless T000 map:23: attempt to index local 'pl' (a function value)" (`Map.GetPlotByIndex(i)` gave a function) and "Headless checkpoint:1: attempt to index global 'Game' (a function value)"; see below |
| master, fixed server | test_proxy_rt / hutest / `--scenario all` | 79/79 / 47 checks 0 failed / 8 of 8 |
| pin, fixed server | test_proxy_rt / hutest / `--scenario all` / verify | **79/79 / 47 checks 0 failed / 8 of 8 / PASS** (29 s, host Lua errors 0): the pin as before (46 checks before; the 47th is the new one) |

**Which cause held** (the MC's question #1191, after 3515 #1189 found that master's committed CP `.modinfo` lacks the
two `Gamecore/*.lua` imports while `Community Patch.civ5proj` imports them) [L]: on the proxy route the scratch
registration. The server log shows the chain of 10a exactly: the first `kRt_LuaRegister` of channel 0 (5.03 s) comes
before `CreateLuaThread(!VPDLL)` returns (5.04 s) and has 0 globals, and so does every later one; making
GamecoreInit.lua loadable changes nothing on the unfixed server; with the fix every later `kRt_LuaRegister` has 72
globals. The `.modinfo` gap is real [C, `git show 76b3a5f70`: the `.modinfo` names no Gamecore file, the `.civ5proj`
has both with `ImportIntoVFS` True] and matters on a route whose VFS reads the `.modinfo` (a MODS-folder install, as
3515's c5h-b); the headless host's DLC scan indexes every Lua file under the package's `Mods` directory, so on this
route only the game folder decides (the dry run used the 5.4.8 folder, which has no GamecoreInit.lua at all).
Whether the `.modinfo` gap alone leaves `Game` nil in a 32-bit master game (3515's reading) is not checked here.

**The fix** (`srv_remote_lua.cpp` `RegisteredCtx`, branch `task-0308-proxy-lua-bridge-master`): the scratch state is
marked registered only when the DLL's `RegisterScriptLibraries` added a global (key count of the globals table before
and after); otherwise it stays unregistered (logged, at most 20 lines, and counted in server-report.txt as "Lua: N
RegisterScriptLibraries calls on a scratch state registered no global and were repeated (issue 0308)") and the next
request registers again. The nested request still answers 0 globals, as the EXE gives `!VPDLL` none. At the pin the
count is 0 (every call registers). The other candidate of 10a (register the scratch state before forwarding
`CreateLuaThread`) does not work: `skipGamecoreCreation` is already set when the DLL calls `CreateLuaThread`.
Regression check: hutest's fake DLL registers nothing on its first call; the host checks `lua_register_skipped_first`
(nil) and then `lua_register_globals` (HtMarker 2503), which fails on the unfixed server (table above).

**Repeat runs** [L, 2026-11-14 23:42-23:53 EST, the same builds]: master with the fix, verify leg, 5.4.8 game folder
**7 of 8 PASS**, testdata game folder **8 of 8 PASS** (28-33 s each, host Lua errors 0); the pin **4 of 4 PASS**; the
pin again on the branch after merging main 3e12c4f1 (23:56-23:59): test_proxy_rt 87/87 (main's 8 new checks), hutest
47/0, verify PASS. The one
failure (the first master run of the 5.4.8 folder, 2 host errors at turn 0) is intermittent and its cause is not
established [I: a stale stub in the proxy's sref cache for an id the server reused]; it is item 2 of
`tracker/issues/NEW-proxy-lua-master-userdata.md`. *(Corrected 2026-11-24, issue 0314 [C, L hutest]: the cause is
established and fixed. Lua 5.1 keeps a function stub in the proxy's weak sref cache for one more GC cycle when a
userdata finalized in that cycle reaches it (each stub's `__gc` userdata has the host thread's environment, which holds
the thread's function stubs), while the stub's own release goes to the server, whose `luaL_ref` reuses the freed slot
first; the proxy now reuses a cached stub only when it is alive and of the sent type. A hutest stress reproduces it on
main's proxy every run (33 userdata arrived as functions) and passes with the fix: `research/03-dll-engine-boundary/
proxy-design.md` 35.1. Replaced: "its cause is not established".)*

**What else the verify leg shows on master** [L, the passing runs]:
- every DLL function crosses on the sref path (C closures of `protectedWrapperAux`): `proxy-lua-calls.tsv` has 3 rows
  (`(kRt_LuaIndex)` 3,112,478 calls and two `(server-side closure or userdata)` rows) against 149 named functions at the
  pin, where `kRt_LuaIndex` is 15,175; `kRt_LuaCall` 3,121,473 (pin 3,113,308): about twice the pin's Lua crossings.
  `tracker/issues/NEW-proxy-lua-master-closures.md`. *(Corrected 2026-11-16, issue 0313 [L]: the server learns master's
  wrapper and sends its closures as kLCFunc; on master's builds of that branch the verify leg names every DLL function
  (156 rows; `proxy-lua-names.tsv` 3,598 names, the 35 bindings of `lua-pure-bindings.tsv` among them), 15,237 of
  3,123,179 `kRt_LuaCall` remain on the server-ref path, `kRt_LuaIndex` is 701,672 (a per-stub cache of method
  lookups in the proxy), and with a purity file the UI-Lua mirror verifies 3,305 hits with 0 mismatches; the pin's
  counts are unchanged. Details `research/03-dll-engine-boundary/proxy-design.md` section 32. Replaced: "every DLL
  function crosses on the sref path" for builds with 0313.)*
- `Game`, `Map`, `Players[i]` and every instance arrive as server-value stubs, never as instance tokens:
  `tracker/issues/NEW-proxy-lua-master-userdata.md`. *(Corrected 2026-11-24, issue 0314 [L]: on master's builds of
  that branch statics and instances cross as kLInstance and are engine-side userdata as on master (metatable hidden,
  methods DLL-function thunks); master's Tiny 0-50 verify leg has kRt_LuaIndex 15,175, the pin's number (0313: 701,672;
  0308: 3,112,478), and passes with both game folders; details `research/03-dll-engine-boundary/proxy-design.md` 35.
  Replaced: "arrive as server-value stubs" for builds with 0314.)*
- `GamecoreInit.lua` runs in the engine VM; its `gc_post` handler fails there ("attempt to index local 'core' (a nil
  value)", `_G.GameCore` is never set; printed by its own `pcall`, not a counted error), and its registry and
  `vputils` reads find the engine's, not the server's: `tracker/issues/NEW-proxy-lua-master-gamecoreinit.md`.
  *(Corrected 2026-11-25, issue 0315 [L, c5h-b]: "`_G.GameCore` is never set" is the headless host's: in a 32-bit
  master game the EXE sets `GameCore`, and `postinit` / `do_gamecore_post` run without error (`POSTPROCESS!` after each
  `gc_post`, `vputils` in 153 of 155 UI contexts, `Players.Majors()` iterates). Through main's proxy on a real EXE the
  handler failed at line 31 (`pairs` of the engine registry's missing `vpdllinternals`) and `vputils` was nil
  everywhere; with 0315's bridge the GamecoreInit lines of Lua.log equal the 32-bit game's line by line and the UI
  contexts get `vputils` and `Players.Majors` as there (`research/03-dll-engine-boundary/proxy-design.md` section 36).
  The headless verify leg still prints the line-30 error (its host sets no `GameCore`; not upstream's, not the bridge's).
  Replaced: "`_G.GameCore` is never set" as a statement about the EXE.)*
- The UI-Lua mirror counts 0 hits and 0 misses on both the pin and master on this route (`vpproxy.log` "UI-Lua mirror
  (on): hits 0 ... misses 0", "verified 0"), and the getter mirror 0 hits / 1,734,625 misses: step 12's PASS line above
  ("a non-zero count verified", 1,300,212 / 1,198,941 at 9029) does not hold for these builds, and `run_proxy_game.py`
  does not check it. Not investigated (the purity file is not loaded: "purity: not loaded (pack=0) ... every call
  crosses", which may be all of it) [L the counts, I the reason]. *(Answered 2026-11-16, issue 0313 [L]: it is all of
  it. With `VP_PROXY_PURITY=<derived purity.tsv>|<x64 DLL>` the same verify leg on the pin verifies 1,198,941 UI-Lua
  hits and 1,300,212 getter answers, 0 mismatches, as 3503's reference; on master `derive_purity.py` keeps only 1 of
  the 29 Lua rows (#13489 rewrote `BasicLuaMethod`/`GetInstance`), so master's mirror check needs a re-audit of those
  rows or a test file (proxy-design 32.2). A step 12 run that wants the mirror exercised sets `VP_PROXY_PURITY`.)*

### Step 11. Purity re-audit of the D30 lists (also for a batch)

- **What**: D30 condition 6: the pure lists keep source evidence. Check every citation of `proxy/model/pure-methods.tsv` and
  `proxy/model/lua-pure-bindings.tsv` against the new source and every callee against the changed functions; record the
  pin audited in `proxy-design.md` (17.5, 17.5a are the pattern).
- **Tool**: `research/03-dll-engine-boundary/evidence/4000/0111_cite_check.py` and `0111_callee_check.py` (issue 0111),
  `server/evidence/2800/w4/tools/cite_check.py`; `python proxy/tools/gen_pure_list.py --check` (build_proxy step 0 runs it).
  After the re-audit (TSVs edited, `--make-baseline` rerun): `python -I proxy/tools/test_purity.py --src <the audited
  source> --dll <x64 DLL> --scratch <dir>` in full, about 110 s; since issue 0480 (2026-11-17) the release driver's
  `purity` stage runs it after the derivation and `build_proxy.py` step 7 runs it whenever the purity inputs changed
  (proxy-design 16.9), so a list change that breaks it fails the stage and the next proxy build. `--only regen`
  (section 6) alone does not exercise the derivation (0478: its derive checks failed on main for ten hours unseen).
- **Owner**: bugfix team (0111). **Time**: 3 min for c43ea1b9f -> 76992d235, 24 files, 302 + 81 citations, 0 changed [L,
  tracker 0111 log 05:09-05:12]. A release with 141 changed DLL files and moved or changed citations: 1-3 agent hours [I].

### Step 12. Offline proxy checks (also for a batch)

- **Tool** (9045's `s13_offline.py`, each under `quietlock.py run`):
  `python -I proxy/tools/test_proxy_rt.py --build <out>` (**56/56, 46 s** [L 9045]);
  `python -I proxy/tools/hutest/build_hutest.py --b <out> --src <src> --deps <out>/deps --out <dir>` (45 checks, 0 failed)
  and the same with `--scenario all` (8/8);
  the verify leg: `python -I proxy/tools/run_proxy_game.py --bin <layout>/bin --proxy <layout>/proxy/bin --spec
  server/specs/continents-tiny-fixed-seed.json --turns 50 --run <dir> --mode proxy --fail-on-unexpected --ini strict=1
  --ini local_answers=1 --ini local_verify=1 --ini test_slot_poll=4 --ini lua_mirror=1`: PASS = 0 mismatches, 0 lock
  errors, a non-zero count verified (1,300,212 getter answers and 1,198,941 UI-Lua hits on Tiny 0-50 on the current pins;
  about 1 min [L 9029 14:40-14:41]). (Note 2026-11-14, 0308: on today's pin build with this command the proxy logs
  "purity: not loaded (pack=0)", mirror hits 0 and UI-Lua mirror hits 0, verified 0, and the tool still reports PASS:
  the non-zero count is not checked; 10b.)
- **Owner**: the one-off. **Time**: 0.25 agent hours; about 5 min of machine time [L 9045 12:08-12:14].

### Step 13. The save test (D10/D24) (also for a batch)

- **Tool**: `python -I tools/d10/run_d10.py --out <dir> --x86-dll <i0ship-.../CvGameCore_Expansion2.dll> --srv-bin
  <layout>/bin --huge-save <copy of the huge reference save> --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`
  (gated; name the builds, the defaults are old pins, step 18). PASS = parts 1-3 on tiny 0-100, standard 0-200, huge
  350-400, only the version-string exception.
- **Time**: **1,092 s** [L 9029, `--jobs 12`], **1,228 s and 1,397 s** [L 9045, `--jobs 6`].
- **Then**: record the new reference row counts in `d10-save-identity-test.md` (4d holds 9029's; 9045's standard 351,496 is
  only in `upstream-fix-batch-1.md` 8e [C]). A release changes the game, so every count changes.
- **Owner**: the one-off.

### Step 14. Drift checks and the whole-game re-validation (also for a batch)

- **Tool**: `python -I tools/drift/run_pair.py --spec tiny --turns 100 --out <dir> --x86-dll <...> --srv-bin <...>` (x86 =
  x64, first differing turn; 30,666 rows on 9029's pins [L]); `tools/drift/run_huge.py --save <copy> --turns N ...` for the
  huge save; on the testbeds whole games to the end with `tools/testbed/jobs/longgames.py` and `--compare x64:x86`
  (`tools/d10/statscmp.py`, every table), submitted with `python tools/testbed/tb.py submit` (not `--quiet`: these are not
  timings).
- **Time**: 17 whole games (the 9 of bug 0116 and 8 samples) on `oldpc` and `laptop`: **10:57-13:32, about 2.6 h** [L 9045
  8f], unattended.
- **Owner**: the one-off. Any game where x64 != x86 is a bug before the pin moves (0116 is the example: 3 of 12 games).

### Step 15. Fix checks of carried fixes; old saves (also for a batch)

- **Tool**: `research/03-dll-engine-boundary/evidence/4000/0096_run.py`, `0103_run.py`, `0084_lock_test.py` (the old
  layout against the new; the arguments as in 9045's `s13_offline.py`), minutes each [L 9045]. A save written by the old
  pin loads and plays two turns: `python server/run_server.py --load <copy> --turns <N> --run <dir> --bin <layout>/bin`
  (29 s [L 9029]).
- **Owner**: the one-off. **Time**: 0.25 agent hours.

### Step 16. The live proxy route on an instance (also for a batch)

- **Tool**: the harness on a claimed Goldberg instance (`tools/harness/`, the checklist of 9029), `vpproxy.ini
  local_answers=1 local_verify=1`, 10 AI turns each on a Tiny save and on the huge save; PASS = 0 mismatches, 0 lock
  errors, start-up watch ok (COMMON 14 c).
- **Time**: T-verify 7 min and H-verify 10 min [L 9029, 14:42-14:59]. **Owner**: the one-off.

### Step 17. Instances and the testbed (also for a batch)

- **Tool**: `python -I tools/instances/provision.py set-dll <name> --dll pin`, then the tool's full `verify` of the
  instance (48-300 s per instance [L, 9014, `headless-and-parallel-testing.md` 6.1b]; usage `tools/instances/README.md`); the testbed: `commission.py --host oldpc ship-bases --only dll-pin-ia` (6 s), `ship-tooling`,
  `restart-runner`, `set-dll tb1,tb2` (2 min) [L, `quiet-testbed.md` 13]. Instances running for another party are left
  alone and moved later (claims, COMMON 16).
- **Also**: test DLL branches of epics (`i2-shell-test`, `i2-white-mask` were left on an old base at the 9029 move) are
  rebased by their epic before their DLLs count as pin builds. For a release, the toolkit and civ5-headless bases change
  too (step 2).
- **Owner**: the one-off; the epic owners for their own test DLLs. **Time**: 0.25 agent hours.

### Step 18. Tool defaults and documents (also for a batch)

- **What** [C, 2026-11-13]: `tools/d10/run_d10.py` `DEFAULT_X86_DLL`, `DEFAULT_SRV_BIN`; `tools/drift/run_pair.py`
  `PIN_X86`, `PIN_SRV_BIN`; `perf/turnbench/turnbench.py` lines 604-606: all still name the pins of 2026-11-11
  (`i0ship-bbfacca1`, `x64-76992d23[-p1607d42]`). Ticket 0133 (queue draft 3.2) ties `run_d10`/`run_pair` to
  `deps/dll.lock`'s `release`; the same is open for turnbench and the x86 defaults. COMMON rule 18 and
  `docs/briefs/PERFORMANCE.md` name the pins (the MC's files). For a release also every `5.4.8` path of step 2.
- **Tool**: find them with `grep -rn "c5-i0/dlls/" tools server perf proxy --include=*.py` and, for a release, `grep -rn
  "5\.4\.8\|5_4_8" tools server perf proxy --include=*.py` (evidence folders excluded); edit with the file-editing tool;
  after 0133, `python -I -X utf8 -m unittest` in `tools/d10/tests` and `tools/drift/tests` checks the defaults against
  the lock.
- **Owner**: a queue ticket for the tools; the MC for COMMON; the performance team for PERFORMANCE.md.
- **Time**: 0.5-1 agent hours [I; 0133 is sized 1 h].

### Step 19. Performance baselines (also for a batch)

- **What**: seconds per AI turn, AI logging off and on, x64 and x86, on the quiet testbed (COMMON 23): huge 350 -> 355 and
  Standard 0 -> 50 (`turn-time-baselines.md` section 11). Every series of the old pins stops being a baseline (COMMON 18).
- **Tool**: `python tools/testbed/tb.py submit --quiet ... -- "%C5H_PYTHON% perf/tbjobs/series.py --game huge --to-turn 355
  --arm NAME=x64:<bin>:off ..."`, then `perf/tbjobs/analyze.py` (`docs/briefs/PERFORMANCE.md` section 3).
- **Time**: huge series 18 min (22:25-22:43), Standard series 10 min (22:43-22:53) of quiet testbed [L, perf 7010,
  2026-11-12]; 2.2 agent hours for the issue [tracker 7010]. Quiet jobs wait while anyone is logged on there.
- **State today** [C, 2026-11-13]: `PERFORMANCE.md` and section 11 are on bbfacca1a / 76992d235; nothing in
  `research/08-performance/` is measured on the D96 pins (04b1211b8 / 26234371e), which change game results (0116, 0103).
- **Owner**: the performance standards team (D40). The memory acceptance of increment 1 (100-turn slope, decision log D78)
  is re-run only when a claim depends on it.

### Step 20. Merge and announce (MC)

- Merge the lock change; fast-forward the fork's `i0-ship`, `x64`, `master` (D46, `merge-base --is-ancestor` before each
  push, as 9029 did); update COMMON rule 18 with the time of the change; tell every team which results stop being
  comparable (the list of `upstream-fix-batch-1.md` section 5: game baselines, turnbench and testbed baselines, verify and
  parity baselines, pin names in the briefs and tool defaults, policy rows, save sizes).
- **Tool**: `git -C <fork clone> merge-base --is-ancestor <old branch head> <new commit>` (must succeed), then a plain
  fast-forward push `git -C <fork clone> push origin <new commit>:refs/heads/<branch>` for `i0-ship`, `x64` and `master`
  (9029's route, `upstream-fix-batch-1.md` section 6); `git merge` of the team branch on `main`; COMMON rule 18 edited
  with the file-editing tool.
- **Owner**: the MC. **Time**: 0.25 agent hours [I].

## 3. The next release, as far as upstream master shows it [C, `git`, 2026-11-13, upstream/master 16596561f of 2026-11-11]

| Area | Release-5.4.8 -> upstream/master | Steps it costs |
|---|---|---|
| DLL folders | 77 commits, 141 files, +14,767 -7,552; 412 files in all (196 under `(2) Vox Populi`, 56 under `(1) Community Patch`) | all |
| Files the patch also changes | 38 (the x64 port's `CvAStar.cpp`, `CvDatabaseUtility.cpp` +319, `CvDllGame.cpp` -118, `CvEspionageClasses.cpp`, `CvGame.cpp/.h`, `CvGlobals.cpp` +359, `CvHomelandAI.cpp`, `CvTacticalAI.cpp`, `CvUnit.h`; i0's `CvPlayer`, `CvCity`, `CvTradeClasses`, `SqliteLogger.cpp`, `FFastVector.h`, ...) | 3, 4 |
| Boundary interfaces | `CvDllInterfaces.h` 958 and `CvDllInterfaces2.h` 38 `virtual` lines, unchanged; `ICvDLLScriptSystem.h` changes only typedefs (`lua_State`, `lua_CFunction`); `CvDllGame.h` drops `InitExeStuff()` (not a virtual of an interface) | 10 (regenerate to confirm the schema hash is unchanged) |
| Engine calls | 260 -> 261 distinct names; the new one is `TryGameCoreLock`, already served since 9029 | 10 |
| Lua surface | `Method(...)` tokens 3,481 -> 3,489; new `Lua/CvLuaExe.cpp` (Lua `Exe` functions); new `!CvLuaApi*.h`, three Lua instance headers deleted from `CvGameCoreDLLUtil/include`. Since 3519 (2026-11-14) `extract_lua.py` reads master's form: 3,471 compiled registrations, 3,480 methods (step 10a); the proxy's Lua bridge does not yet (step 10a, last item) | 10, 11 |
| **EXE interop (upstream PR 13379, 6c80fa023)** | New `CvGameCoreDLL_Expansion2/Exe/`: the DLL finds the running EXE by its PE timestamp (DX11, DX9, Tablet) through `GetModuleHandleA(NULL)`, checks byte signatures at 12 `EXE_SYMBOL` addresses and writes EXE `.data` and heap objects (resync scheduling, the engine yield-icon manager), behind the new mod option `BIN_HOOKS` (default 1 in `NewCustomModOptions.xml`) | 4, 10: in the server the running module is `server.exe`, so the build is "Unknown" and every `Exe::Try*` returns a refusal [I from the source]; on the proxy route these features would need the proxy (which runs in the EXE) to do the work. Four pointer-to-`DWORD` casts do not compile for x64 [C source, I compile]. A design question for the MC and the Architect before the release is taken |
| Network messages | `CvDllNetMessageExt.h`: five new worker-plan messages (`SetWorkerPlan`, `ClearWorkerPlans`, `RefreshWorkerPlans`, `SetPlannedRouteState`, `SetWorkerPlanSetting`) | 10 (how they cross: `NetMessageExt` rides the engine's net message path, which the server loops back, `server-host.md` 11) |
| Saves | `CvSerialize.h` -75 +17 (desync logging moved out of `CvSyncVar`); `uiVersion` lines touched in a loader | 8, 13 |
| Build list | `build_vp_clang.py` adds 5 TUs (`CvLuaExe.cpp`, four `Exe/*.cpp`): picked up by `tools/x64-dll/build.py` automatically; the fingerprint changes | 5, 6 |

## 4. Gaps found while writing this (2026-11-13)

- `proxy/gen/toolchain.py` still names the e9d0c4837 clone; the model has not been regenerated since [C]. Bug 0125.
- Tool defaults name the pins of 2026-11-11 [C]: ticket 0133 (run_d10, run_pair); turnbench has no ticket yet.
- No performance baseline on the D96 pins [C, absence in `research/08-performance/` and PERFORMANCE.md].
- `d10-save-identity-test.md` holds 9029's reference counts, not 9045's [C].
- The save-width census had not been run since the pins of issue 1004 (f3d2f5669, 2026-11-10); run here at 04b1211b8:
  still 0 x64-only sites (step 8) [L].
- The proxy model's counts in `proxy-design.md` section 2 are those of e9d0c4837 [C]; 9029 and 0111 checked the changes
  since by hand (one policy row; the purity citations), not by regeneration.

## 5. Not verified

- The time estimates marked [I], above all steps 3, 4 and 10 for a release of the size of section 3; no release has been
  taken since the project started (the installed mods and every pin are 5.4.8).
- That the x64 build of upstream master fails only on the four casts of step 4 (not compiled).
- That VP releases load each other's saves.

Sources: `deps/dll.lock`, `deps/i0.lock` (comments), `research/03-dll-engine-boundary/upstream-fix-batch-1.md` (sections 1,
5, 6, 8), `proxy-design.md` (1, 17.2, 17.5, 17.5a), `x64-dll-port.md` (0.1, 3.2, 4.1), `stl-drift-sites.md`,
`tools/x64-dll/save-layout-audit.md`, `research/06-tooling-and-workflows/d10-save-identity-test.md` 4d,
`headless-and-parallel-testing.md` 6.1a-c, `quiet-testbed.md` 13, `research/08-performance/turn-time-baselines.md` 9 and 11,
`docs/briefs/PERFORMANCE.md`, tracker issues 0111, 0125, 1003, 1004, 1600, 2101-2103, 7010, 9020, 9029, 9045 and queue
draft 3.2 (0133); `git` reads of the upstream clone and the 9045 fork clone (the commands are those of step 1; read only).
