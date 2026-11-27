# DLL pin batch 4: upstream #13522, the D176 air-strike fix and D181's three DLL fixes (bug 0508, A9, A10)

**Current state (2026-11-17, ticket 0517): the pins move to batch 4 with D181's fixes, section 9.** The fork branches
`i0-ship-batch4` and `x64-batch4` were fast-forwarded by 0517 from the heads 0497 made (2fbf08f9a / 66b4853cc, sections
1-8 below) to `e96457fd5` / `c29442e8c`, which add bug 0508's, A9's and A10's fixes. Sections 1-8 describe 0497's
preparation and stay true for those older heads; where they name the pin move (section 7), section 9 is what was done.

Ticket 0497 (account B, 2026-11-17), branch `task-0497-pin-batch-4`. Batch 4 sits on top of pin batch 3 (ticket 0463,
`upstream-fix-batch-2.md` section 3; its branches `i0-ship-batch3` 39f17f754 and `x64-batch3` 4f4daac70) and adds two
changes:

1. **Upstream #13522** (`85b2e7bc4`, "Share one read-only zero block among the empty 2D database tables", msw1age): the
   address-space saving measured by ticket 0461 (`upstream-13522-address-space.md`).
2. **The D176 fix** (ticket 0477, `research/05-graphics-composition/combat-choreography.md` section 13): in
   `CvUnitCombat::AttackAir` the EXE gets a copy of the combat information in which a `UNITCOMBAT_SUBMARINE` defender
   deals no damage and does not retaliate, so a bomber's strike on a submarine no longer waits 20 s for an anti-air
   answer the submarine models cannot play. Fork commits `cb85112f1` (on the i0 pin, branch
   `fix-air-strike-submarine-retaliation`) and `23bb8cbca` (on the x64 pin, `-x64`), local branches in the owner's fork clone.

Nothing here moves a pin: `deps/` belongs to 0463 until it merges. Nothing goes upstream (D95).

Fork clone for this work: `C:/c5-64bit-work/0497/fork` (`git clone --shared --no-checkout` of the owner's fork clone,
remote `origin` = the GitHub fork, remote `art` = the owner's clone for the two D176 branches). Scripts: `server/evidence/0497/`.

## 1. Branches [C, 2026-11-17 10:12 EST]

- **`i0-ship-batch4` 2fbf08f9a** (`Release-5.4.8-46-g2fbf08f9a`, tree `aa023f47`): `i0-ship-batch3` 39f17f754 +
  `git cherry-pick -x 85b2e7bc4` (-> 71381245e; auto-merge in CvDatabaseUtility.cpp/.h, CvInfos.cpp, CvTechClasses.cpp,
  CvTraitClasses.cpp, CvUnitClasses.cpp, no conflict) + `git cherry-pick -x cb85112f1` (-> 2fbf08f9a, clean). No hand edit.
- **`x64-batch4` 66b4853cc** (`Release-5.4.8-60-g66b4853cc`, tree `7f69152f`): `x64-batch3` 4f4daac70 + `--no-ff` merge of
  `i0-ship-batch4` (the route of every earlier x64 pin: x64 keeps i0-ship as an ancestor). Auto-merge in
  CvDatabaseUtility.cpp; `git show --remerge-diff` empty.
- **Cross-check**: `x64-batch3` + cherry-picks of `85b2e7bc4` and of the x64 fix `23bb8cbca` gives the same tree
  `7f69152f`. So the x64 branch carries exactly the x64 fix commit the ticket names.
- **Same change on both**: `git diff 39f17f754 i0-ship-batch4` and `git diff 4f4daac70 x64-batch4` have the same patch-id
  `72a01df6`; from the pins (`26234371e..i0-ship-batch4`, `04b1211b8..x64-batch4`) also equal (`4454238c`). The D176 part
  (`CvUnitCombat.cpp`) has the patch-id `5236b2dd` of both `cb85112f1` and `23bb8cbca` (the two fix commits are the same
  patch). 12 files, +285 -1.
- None of `6f2c2ee3`, `e29032ad`, `90f1c4c0` is an ancestor of either branch (`merge-base --is-ancestor`).
- New commits: 71381245e (msw1age, #13522), 2fbf08f9a (D176), 66b4853cc (the merge).

- **Pushed** to our GitHub fork of Community-Patch-DLL as new branches `i0-ship-batch4` and `x64-batch4`,
  2026-11-17 10:40 EST (`s08_push.sh`: refuses an existing name, a branch not at the expected commit or a forbidden
  ancestor; never forced). `i0-ship` and `master` (26234371e), `x64` (04b1211b8) and the batch-3 branches untouched.
  The D176 fix branches stay local in the owner's clone; the batch-4 commits make them unnecessary for the carry (section 7).
- **Carry series check** [C, 10:35 EST]: 0463's `carry.tsv` (from `task-0463-pin-batch-3`) + the two rows of section 7,
  `branches.py --base Release-5.4.8 --name check0497 --no-fetch --compare-i0 i0-ship-batch4 --compare-x64 x64-batch4` in
  the local clone: **52 applied, 0 dropped, 0 skipped, both trees equal** (local branches `*-release-check0497`, not
  pushed; `server/evidence/0497/branches-check0497.json`, script `s07_carry.sh`). The clone needed
  `git fetch origin i0-fix-13566 i0-fix-13482 i0-fix-13395 i0-fix-0421` first (branches.py stops at CL step 3 naming
  the four batch-3 row commits otherwise).
- **Source fingerprints** (`tools/x64-dll/src_fingerprint.py --git <clone> --commit`): x64-batch4 66b4853cc
  `v1 661a5e4ef6eb76cfd6435397e11b2a475e9239fc4775d4984f5cddc0a75629e3` 473 files; i0-ship-batch4 2fbf08f9a
  `v1 7caf25ec59078e1aeabe1b174ae6d697db36f5b5c6de9d197ee8b8764c81a00b` 473 files.

## 2. The instruments (`-ia`) patch: batch 3's applies unchanged [C]

0463's resolved patch `ia-39f17f754-440878983.patch` (the committed blob on `task-0463-pin-batch-3`,
`tools/d10/evidence/0463/`, SHA-256 `97887b35...767d`) passes `git apply --check` on a `git archive` of
`i0-ship-batch4`. The patch's 41 files and batch 4's 12 files have no file in common. So no new patch is needed: the
batch-4 `-ia` build is that patch on a git archive of 2fbf08f9a, and the pin move registers no new `IA_PATCHES` row as
long as 0463's row picks by a base that `i0-ship-batch4` contains (39f17f754 is its ancestor).

## 3. Builds

Workstation, 2026-11-17, one after the other (COMMON 16, D107), script `s04_build.sh` (started 10:16 EST, all done
10:44). Kept with PDBs, `SHA256.txt` and a README under `F:/c5-64bit-work/0497/builds/`.

| Build | Route | Version string | Size (B) | DLL SHA-256 | Time |
|---|---|---|---|---|---|
| **x64** `x64-66b4853c` | `tools/x64-dll/build_all.py --config release --offline --unlocked --jobs 6`, detached fork worktree of 66b4853cc | `Release-5.4.8-60-g66b4853ccc Clean` | 23,680,512 | `991f45a5dba5fd9c81f4305ef960055b30d3368b2b6541c8d62cda4b0dd19e36` | 718 s (DLL 571 s, link 440 s) |
| **x86 plain** `i0ship-2fbf08f9` | the fork's `build_vp_clang.py --config release` (clang-cl, LTO) in a detached worktree of 2fbf08f9a, under `quietlock.py run` (as `stages/build.py` `build_i0`) | `Release-5.4.8-46-g2fbf08f9ad Clean` | 17,150,976 | `2738c3290897a3d04017630bb21f23edda9b204e314f48632313f88dd8c4f02b` | compile 103 s, link 376 s |
| **x86 -ia** `i0ship-2fbf08f9-ia` | `git archive` of 2fbf08f9a + `git apply` of `ia-39f17f754-440878983.patch` + version stub (as `stages/build.py` `prepare_ia`), `build_vp_clang.py --config release` under `quietlock.py run` | `Release-5.4.8-46-g2fbf08f9a+ia440878983 Copy` | 17,478,656 | `c788f5b5a9034e0afbe67de749130b4594bec070c3e56419fc3478411f9ee3a9` | link 384 s |

- x64: all six `build_all` steps rc 0 (stubs, CvGameDatabase, CvLocalization, DLL, smoke host, smoke run: "Supporting 22
  major civilizations"); 0 unresolved; narrowing gate 794 sites, baseline 798, **0 new**, 4 gone (as 0463's batch-3 build);
  repeatability check OK (181 link inputs, none time-stamped), so a rebuild of the same source folder gives the same
  bytes. The release driver builds a pin whose fingerprint matches from a `git archive` (version "... Copy" when it
  stubs the version, see 0463's `Release-5.4.8-57-g4f4daac70 Copy`): its DLL hash will differ from this one by the
  version string and the source path.
- Sizes against batch 3 (0463's layouts): x86 plain +2,048 B (17,148,928), x86 -ia +1,024 B (17,477,632), x64 +10,752 B
  (23,669,760). 0461's #13522 arm on the i0 pin was +3,072 B on its `-ia` build.
- The 32-bit builds are not repeatable byte for byte (`__FILE__`, `__DATE__`, link time; `headless-and-parallel-testing.md`
  6.1b).

## 4. Writer check of #13522's shared tables on the new heads [C, 2026-11-17 10:16 EST]

`server/evidence/0497/writercheck.py` (0461's method and member list `server/evidence/0461/members.txt`, parameterised by
source folder; regex over `CvGameCoreDLL_Expansion2/*.cpp`, top level only, as 0461): every write (`=`, `+=`, `-=`, `++`,
`--` on `name[..][..]`) to the 55 shared member names, and whether it lies inside a `CacheResults` function.

| Tree | .cpp files | writes | outside CacheResults |
|---|---|---|---|
| `i0-ship-batch4` 2fbf08f9a | 157 | 96 | 0 |
| `i0-ship-batch4` + `ia-39f17f754-440878983.patch` (what the testbed instances run) | 163 | 96 | 0 |
| `x64-batch4` 66b4853cc | 157 | 96 | 0 |
| negative control: the i0 tree + one write to `m_ppaiPlotYieldChange` in a new function | 157 | 97 | 1 (found, exit 1) |

96 writes, all in `CacheResults`, as on 5.4.8, on the PR's base and on the i0 pin (0461 section 2): batch 3's three
upstream takes, bug 0421's guard and the D176 fix add no writer. Not covered: writes through a returned row pointer (the
PR's own check says the 22 row-pointer getters only read) and `.cpp` files below `CvGameCoreDLL_Expansion2/` (the regex's
scope since 9020). Output: `server/evidence/0497/writercheck.txt`; script `C:/c5-64bit-work/0497/s05_writercheck.sh`
(kept as `server/evidence/0497/s05_writercheck.sh`).

## 5. The EXE-side address-space job of 0461 (oldpc, job 20261117-031426-7498) [L, 2026-11-17]

0461's recommendation (`upstream-13522-address-space.md` section 4) took #13522 "provided the EXE job of 3.2 shows the same
direction (no less free address space or largest free block with B at load or after 10 turns)". The job ran on oldpc
04:45-05:21 EST (headed, `TB-HUGE_0350`, order A,B,B,A, load + 30 s rest, then 5 + 5 AI turns; A = the pin's `-ia` DLL
`3C2F47DA...`, B = A's recipe + #13522 `692DD774...`), exit 0, all four runs reached turn 360. Fetched by this ticket at
10:18 EST into `F:/c5-64bit-work/0497/job-7498`; result and summary kept as
`server/evidence/0497/oldpc-exe-20261117-031426-7498-addrspace_ab.json` and `...-summary.txt` (`summarize.py` of 0461).
`CivilizationV_DX11.exe`, mean of 2 runs per arm, MB:

| Point | private A / B (B-A) | free A / B (B-A) | largest free block A / B (B-A) |
|---|---|---|---|
| load (turn 350) | 1850.2 / 1804.2 (**-46.0**) | 1441.1 / 1507.4 (**+66.3**) | 1391.2 / 1451.5 (**+60.3**) |
| after 5 turns (355) | 1925.6 / 1881.6 (-44.0) | 1315.9 / 1351.8 (+35.9) | 1253.0 / 1231.8 (-21.2; A 1237-1269, B 1225-1239) |
| after 10 turns (360) | 1938.2 / 1895.3 (**-42.9**) | 1270.7 / 1318.8 (**+48.1**) | 1205.0 / 1231.8 (**+26.8**) |

So the condition holds at load and after 10 turns: less private memory (43-46 MB, as the headless host's 43-54 MB), more
free address space (+48 to +66 MB) and a larger largest free block (+27 to +60 MB). After 5 turns the largest free block
was 21 MB smaller with B, inside the spread of the two A runs (32 MB apart) [I: placement noise of the EXE heap, n=2].
Upstream's reviewer saw free space equal again after 15-30 turns while committed stayed 45-55 MB lower; this job's 10
turns do not reach that far. Section 3.2 and 5 of `upstream-13522-address-space.md` still say "not measured"; the MC
may point them here (outside this ticket's files).

## 6. Save test (D10) on the batch-4 builds

Command (`s06_d10.sh`, started 10:47:55 EST):
`python -I tools/d10/run_d10.py --out F:/c5-64bit-work/0497/d10/run --games tiny,standard,huge --x86-dll <x86 plain>
--srv-bin F:/c5-64bit-work/0497/layout-x64b4/bin`. The server bin is 0463's batch-3 layout bin
(`x64-4f4daac7-pf8e9f42`: server.exe, libraries and civ5save.exe built at repository commit f8e9f424b, which carries
0452's load fix) with only `CvGameCore_Expansion2.dll`/`.pdb` replaced by the x64 build above (0421's way). Expected:
PASS, x86 direct = x64 direct, and the row counts of batch 3 (tiny 30,666, standard 351,496, huge 689,879; 0463
section 3.5), since #13522 reads the same values and the D176 fix changes only what the EXE is told.

**Result [L, 2026-11-17 10:47-11:12 EST, wall 1472 s]: PASS**, all three games, all three parts (writer identity,
behaviour identity, cross-load); x86 direct = x64 direct with **tiny 30,666 rows (15 tables), standard 351,496 (15),
huge 689,879 (14)**, the batch-3 counts. DLL AI logging off in both arms (D49). Report:
`server/evidence/0497/d10/d10-report.txt` (user paths written as `%USERPROFILE%`); run folder
`F:/c5-64bit-work/0497/d10/run`.

**Batch 4 against batch 3** (`s10_vs_batch3.sh`, `tools/d10/statscmp.py`, whole `stats.db`, D24's skips only): each
batch-4 direct play against 0463's batch-3 direct play of the same game (x86 against 0463's crossed run A, whose x86 is
the batch-3 plain DLL `44d89d7c`; x64 against crossed run B, whose x64 is the batch-3 x64 DLL), all six
**IDENTICAL** (tiny 30,666, standard 351,496, huge 689,879 rows, each arm). So in these three games batch 4 changes no
game result. Whether any of them has a bomber strike a submarine was not checked [I: the D176 fix keeps the real combat
values for the game result by design, 0477]. Outputs `server/evidence/0497/d10/{tiny,standard,huge}-{x86,x64}.txt`.

## 7. What the MC's pin move must do

*(Superseded 2026-11-17 by ticket 0517, section 9: D181 added three fixes to this batch, so the commits, carry rows,
descriptor counts and lock values below are 0497's proposal for the two-change batch, not what was pinned.)*
Proposed, for after 0463's merge (the pins then are i0 39f17f754 and x64 4f4daac70; COMMON 18 says where pins live):

1. **Locks**: `deps/i0.lock` -> branch `i0-ship-batch4`, commit `2fbf08f9ad5900effe840e85c763e0812441db51`, describe
   `Release-5.4.8-46-g2fbf08f9a`, `commits` +2; `deps/dll.lock` -> branch `x64-batch4`, commit
   `66b4853cccf07c772e38d0e1f9b215d09201396b`, describe `Release-5.4.8-60-g66b4853cc`, fingerprint
   `v1 661a5e4e...29e3` (section 1). Each with a "Re-pinned by" paragraph and a "Replaced" trace of batch 3, as 0463
   wrote them. Release hashes: from the take that builds the move (section 3's are this ticket's builds, not the
   release driver's).
2. **Carry series** `tools/release/series/carry.tsv`: two `fix` rows on `i0` after 0463's `ae0efb7c6` row, in this order:

   ```
   fix	i0	71381245ea97051a0a5f0d493987ce626164957c	Share one read-only zero block among the empty 2D database tables	85b2e7bc465b384ef3272a4be882419dc7a9e2f3 #13522
   fix	i0	2fbf08f9ad5900effe840e85c763e0812441db51	Do not ask a submarine to fire back at an air strike in the combat animation	-
   ```

   The rows name the batch-4 commits themselves (on the pushed `i0-ship-batch4`), not `85b2e7bc4` (an upstream pull
   request head that a fetch of our fork does not bring) or `cb85112f1` (a local branch in the owner's clone). Their diffs
   equal those commits' (section 1). A base that contains upstream's `85b2e7bc4` or a commit naming `#13522` drops the
   first row; if upstream merges a different form (0461 section 4, risk b), replace the row at that pin change.
3. **Instruments patch**: none new. 0463's `IA_PATCHES` row (base 39f17f754) picks `ia-39f17f754-440878983.patch` for
   2fbf08f9a, and it applies unchanged (section 2).
4. **Fork**: fast-forward `i0-ship` (and `master`, as for earlier batches) to 2fbf08f9a and `x64` to 66b4853cc only
   after batch 3's own fast-forward (39f17f754 / 4f4daac70 are their ancestors, so both are fast-forwards).
5. **Checks of the take**: the save test as in section 6 (counts 30,666 / 351,496 / 689,879 expected); the D176 product
   check on the batch-4 release candidate (0477's open item: the full D33 product, the RC's HI phase); 0461's stock-route
   check "one stock-route game of a few turns on a late save in the EXE" (read-only pages fault at once on any missed
   writer; section 5's job is such a run with arm B, without a fault).
6. **Release descriptor**: `reference_counts` unchanged if section 6's counts hold.

## 8. Not verified

- No game ran with the batch-4 builds in the EXE (the ticket: offline only). The D176 fix on these heads, the stock-route
  late-save run of 0461 section 4 with the batch-4 `-ia` DLL, and the product path all wait for the take of the pin move
  (section 7, item 5). 0477 and 0485 proved the fix on the batch-2 pins; 0461's EXE job (section 5) measured #13522 on
  the i0 pin, not on batch 4.
- The release driver's own build of these commits (different hashes, section 3) and its suite: the MC's take.
- The writer check's limits (section 4): row-pointer writes and nested `.cpp` files are outside its regex.
- The EXE's largest free block after 5 turns (section 5) rests on two runs per arm.

## 9. The pin move with D181's fixes (ticket 0517, 2026-11-17)

Tracker `tracker/issues/0517-pin-batch-4-move-with-d181.md`, branch `task-0517-pin-batch-4` (account B). D181 (the owner,
2026-11-17): A10 and A9 as core DLL fixes on both pins in one pin batch with every reference remade; bug 0508 goes in the
same batch. Fork clone `C:/c5-64bit-work/0517/fork` (`--shared --no-checkout` of the owner's clone; remote `art` for the A10
branches). Scripts: `server/evidence/0517/`.

### 9.1 Branches [C, 2026-11-17 21:30-21:35 EST]

| Fix | i0 fix branch (from 39f17f754) | x64 fix branch (from 4f4daac70) | patch-id | on i0-ship-batch4 | on x64-batch4 |
|---|---|---|---|---|---|
| bug 0508: a pantheon's holy-city coordinates initialised to -1 (CvReligionClasses.cpp) | `fix-0508-i0ship` 36b4f0ee1 | `fix-0508-x64` 77d110cc4 | `125fa2ee` | 052b7e725 | ceadc7549 |
| A9: no already built wonder as the next desired wonder (CvWonderProductionAI.cpp) | `fix-a9-i0ship` 6b8379894 | `fix-a9-x64` caf579d9f | `c5345bb4` | 28a262221 | 15d5f1e7d |
| A10: 32-bit path-finder generation ID and node stamp (CvAStar.cpp/.h, CvAStarNode.h) | `fix-a10-i0ship` bae2ec653 | `fix-a10-x64` 2915322be | `c1ef6bb8` | e96457fd5 | c29442e8c |

- **`i0-ship-batch4` e96457fd5** (`Release-5.4.8-49-ge96457fd5`, tree `518a6cd6`, fingerprint `v1 18c6681c...763a` 473
  files) = 0497's 2fbf08f9a + `git cherry-pick -x` of the three i0 fix commits in the order of the table.
- **`x64-batch4` c29442e8c** (`Release-5.4.8-63-gc29442e8c`, tree `f4bc1593`, fingerprint `v1 77a417ec...6414` 473 files)
  = 0497's 66b4853cc + `git cherry-pick -x` of the three x64 fix commits in the same order. One commit per fix per pin.
- No conflict, no hand edit. Each pick has the patch-id of its fix branch, the same on both pins (table); the additions
  over 0497's heads have the same patch-id on both pins (`9883695b`), and so has the whole batch 3 -> batch 4 diff
  (`0a248751`; 17 files, +297 -5).
- **Cross-check (the carry's route)**: `x64-batch3` 4f4daac70 + a `--no-ff` merge of the new `i0-ship-batch4` gives tree
  `f4bc1593`, the tree of `x64-batch4`; `git show --remerge-diff` empty.
- None of `6f2c2ee3`, `e29032ad`, `90f1c4c0` is an ancestor of either head (`merge-base --is-ancestor`).
- **Pushed** to our GitHub fork 2026-11-17 21:35 EST (`s03_push.sh`, never forced): `fix-a10-i0ship` and `fix-a10-x64`
  as new branches (until then only in the owner's local clone; 4806); `i0-ship-batch4` 2fbf08f9a -> e96457fd5 and
  `x64-batch4` 66b4853cc -> c29442e8c as fast-forwards (the script refuses a remote head other than 0497's). `i0-ship`,
  `master` (39f17f754), `x64` (4f4daac70) and the batch-3 branches untouched; fast-forwarding them is the MC's step.

### 9.2 Carry series [C, 21:36 EST]

`tools/release/series/carry.tsv`: five `fix` rows on `i0` after 0463's `ae0efb7c6`: 0497's two (71381245e #13522,
2fbf08f9a D176) and 052b7e725, 28a262221, e96457fd5 (upstream `-`; their diffs equal the fix branches'). `branches.py
--base Release-5.4.8 --series tools/release/series/carry.tsv --name check0517 --no-fetch --compare-i0 i0-ship-batch4
--compare-x64 x64-batch4` (`s04_carry.sh`; local branches `*-release-check0517`, not pushed): **55 applied, 0 dropped,
0 skipped, both trees equal** (`server/evidence/0517/branches-check0517.json`). The clone needs `git fetch origin
i0-fix-13566 i0-fix-13482 i0-fix-13395 i0-fix-0421` first (0497's finding).

### 9.3 Locks and descriptor

`deps/i0.lock`, `deps/dll.lock`: `branch`, `commit`, `describe`, `fingerprint` (x64), `commits` (i0), `source` and a
re-pin paragraph each with a "Replaced" trace. The release keys (`release`, `instance`, `verified`; dll.lock `tools`,
`epoch`) follow the take (9.5); batch 3's are kept as `*-b3`. `tools/release/releases/Release-5.4.8.json`
`reference_counts` set before the take (0463 3.4 item 5: the candidate descriptor copies them at testdata) to ticket
4805's x86 = x64 counts of batch 3 + A9 alone, **31,226 / 349,074 / 683,261** (was 30,666 / 351,496 / 689,879), since
#13522, D176 (0497 section 6), 0508 (4805) and A10 (4806) each kept batch 3's counts; `test_descriptor.py` follows
(31/31).

### 9.4 Proxy model: A10 changes one field type [C, 21:40-21:49 EST]

Run before the take so that its model stage (CL 10) does not stop after a 40-minute build (0463 3.4 item 2):
`proxy/gen/regen_model.py --src <git archive of c29442e8c> --lock deps/dll.lock` (`s05_model_check.sh`, 207 s): exit 1,
**one difference, in `interfaces.json` and `model.json`: `records.CvAStar.{x86,x64}.fields[9]` `unsigned short
m_iCurrentGenerationID` -> `unsigned int m_iCurrentGenerationID`** (A10's widened ID). `slot-proof.json`, `lua.json`,
`policy-table.tsv` and the generated runtime equal (so `kAppSchemaHash` unchanged; no new DLL -> engine call). Neither
0508's nor A9's change reaches the model. Regenerated with `--write` (`s06_model_write.sh`) and committed (58724e072);
`proxy/tools/test_policy_table.py` 13/13, `gen_pure_list.py --check` holds (137 method keys, 29 Lua names). Trap:
Git Bash's `tar -x -f C:/...` reads `C:` as a remote host ("Cannot connect to C: resolve failed"): `tar --force-local`.

### 9.5 The take: validated tier, D162 journeys

`s10_take.sh`: `take_release.py Release-5.4.8 --tier validated --carry pins --for-art --fork C:/c5-64bit-work/0517/fork
--work C:/c5-64bit-work/0517/run --scratch F:/c5-64bit-work/0517/scratch --who 0517 --gate-wait 60` with
`C5_RELEASE_INSTANCE`/`C5_RELEASE_HI_INSTANCE=c5h-b,c5h-e`, `C5_RELEASE_TESTBEDS=local` (wholegames on the workstation),
started 2026-11-17 21:45 EST at tools 58724e072 (clean).

**Build stage ok 21:48:29-22:13:11 [L]** (x64 730 s, i0 357 s, i0ia 365 s; the `-ia` patch chosen by base: 0463's
`ia-39f17f754-440878983.patch`, applied without conflict). x64: build_all steps stubs, cvgamedatabase, cvlocalization,
dll, host, smoke rc 0; unresolved 0; narrowing 794 against 798 (0 new, 4 gone). Layout
`C:/c5-64bit-work/0517/run/layout/x64-c29442e8-p58724e0`:

| File | SHA-256 | Size (B) |
|---|---|---|
| `bin/CvGameCore_Expansion2.dll` (x64-batch4 c29442e8c) | `2B56A1CDCAF6380DD58E9F8984534123AC12379D213FE63CF78F558650F9038E` | 23,681,024 |
| `bin/server.exe` (repository 58724e072) | `FC73E60EE6843FB26451F3C97DB5397969321E1AD4FFF608615237BAF5BCB4CE` | 7,709,184 |
| `proxy/bin/CvGameCore_Expansion2.dll` | `3DFD7521C8E4444A301E8722B397939AEB01219D08FEF1AAF8D0CD946E22B2EA` | 1,195,520 |
| x86 plain `i0ship-e96457fd` (i0-ship-batch4 e96457fd5) | `3737AE10059CA770898C2AB4F6635C3E7F4CA0437E4A56F18003971CB7840A62` | 17,153,024 |
| x86 `-ia` `i0ship-e96457fd-ia` | `3EAA3290AA59B048B96AFA6B7B4AFBECF6A0944ACBB4E979F6C8CA0B8E053603` | 17,481,728 |

Sizes against batch 3's pinned builds: x64 DLL +11,264 B (23,669,760), x86 plain +4,096 B (17,148,928), `-ia` +4,096 B
(17,477,632).

census, model (**equal** to the committed model of 58724e072), purity, pack ok; offline ok (rt 52 s, hutest 25 s,
scenarios 32 s, verify leg 32 s; 22:24-22:25).

**Save test (run_d10, D24) PASS [L, 22:25:43-22:45:31 EST, 1,188 s]**: tiny, standard, huge each parts 1, 2, 3;
x86 direct = x64 direct **31,226 / 349,074 / 683,261 rows** (15 / 15 / 14 tables) = the descriptor's new reference
counts, i.e. exactly the counts 4805 measured for batch 3 + A9 alone. DLL AI logging off in both arms (D49). Report
`server/evidence/0517/d10-take-report.txt` (user paths as `%USERPROFILE%`); run folder
`F:/c5-64bit-work/0517/scratch/savetest-1`.

drift (Tiny 0-100 x86 = x64) ok 22:45-22:46; fixchecks 0096 PASS (both arms' `0096-RESULT PASS` lines; the tool's own
exit 1 is not the verdict, as the stage reads the line), 0084 PASS, 0103 PASS, oldsave exit 0 (22:46-22:49); refsave P10
ok; **instance** (c5h-b, 22:49-23:04): tiny arm **175,141** answers verified, huge arm **372,462**, both 0 mismatches,
0 lock errors, no FALLBACK.

**wholegames** (host `local`, the workstation, 23:04-00:54, 6,557 s; not a timing): **4 of 4 x64 = x86, IDENTICAL**
(16 tables each). These are the new whole-games references of the batch-4 pins; every ending changed against batch 3
(A9 changes games on purpose), so no result of batch 3's whole games is comparable:

| Game (seeds) | Batch 4 ending, rows (x64 = x86) | Batch 3 (0463 3.4) |
|---|---|---|
| Tiny Continents (tc, 5051/5253) | Cultural, turn 425, 1,258,102 | Cultural 361, 903,979 |
| Standard Fractal (sf, 5757/6868) | Diplomatic, turn 345, 1,375,306 | Science 414, 2,255,970 |
| Standard Pangaea (sp, 3333/4444) | Science, turn 396, 1,863,156 | Cultural 370, 1,622,664 |
| Small Pangaea (mp, 1819/2021) | Cultural, turn 367, 1,172,594 | Science 427, 1,708,434 |

Evidence: `server/evidence/0517/wholegames-{tc,sf,sp,mp}-compare.txt`, `wholegames-run.log`.

**hi_journey, first pass FAILED [L, c5h-b, 00:54-01:18]**: release-basic **PASSED** on product (63 steps, 530 s) and
stock (63 steps, 463 s; 6/6 checkpoints the same). Then two crashes on c5h-b, both known, neither the batch's:

1. 01:14:47, pid 51380, 0xC0000005 with no box right after the stock replay's own exit (its replay had passed, exit 0
   01:14:52): the `-ia` DLL's exit crash (ticket 0483, `research/06-tooling-and-workflows/scene-catalogue.md` section
   12; test builds only). The stage passed over it ("nothing runs; going on"), as 82e68bf81 intends.
2. 01:18:13, pid 30676, **0xC0000005 at `CivilizationV_DX11.exe+0x37C6DA` (VA 0x77C6DA)** on the proxy's boot thread
   in release-large's product-route load (the journey's save copy `HI-4506-LARGE-SCENE`, proxy run
   `20261118-011612-30676-1`), no box: the EXE's farm-field layout crash in the weld loop at 0x77C1C0
   (`research/02-exe-internals/crashes-huge-load.md` sections 1-2 and 8, bug 0515; the fix is D186's tickets 0526,
   0527 and epic 0528; MC #4961). The stage then counted release-large as not played (D162: "for the owner: NO").

**Trap (harness)**: on a crash without a box during a replay, `stages/hi_journey.py` raises `LeftRunning` ("the instance
keeps the release layout and the claim until a coordinator closes the game and restores it"), although the game is gone;
its undo list lives only in memory, so the instance stays laid with the release (as 0463 3.4 item 6). Restored by hand
(`server/evidence/0517/s16_restore_c5hb.sh`, 0463's s16 adapted: made saves removed, UserSettings.ini,
GraphicsSettingsDX11.ini and vpproxy.ini back by hash, pack files removed, `set-proxy --original`, `set-dll` back to the
kept `262aee88...`, front end original, `provision.py verify --quick` rc 0), claim released 01:21. A rerun from
hi_journey reads the old crash marker as a crash without a box and goes on (01:22). Proposal for the stage's owner:
treat a boxless crash (watcher kind `crash`, the process gone) in a replay as an ended game: restore and release.
(Done 2026-11-18 by ticket 0533: the replay is recorded as crashed, the instance restored by hash and released;
`research/06-tooling-and-workflows/release-driver.md` section 0533.)

**Second pass, c5h-b, 01:22-02:01 [L]: STOP (D162)**. release-basic PASSED again (product and stock, 6/6 checkpoints
the same; 0483's `-ia` exit crash again after the stock replay, pid 9228, 01:43:47, passed over). **release-large
failed identically on both routes** at step 11 (product 357 s, stock 344 s; checkpoints `moved` and `attacked` ok and
the same): "plot [50, 50] not under the cursor at its pixel now, nor after a search by moves" (the click on plot 50,50
after the attack, `human-input-testing.md` release-large). The same failure, at the same step and on both routes, is
what 0463's second take met on c5h-f with batch 3 (`upstream-fix-batch-2.md` 3.4), so it is neither batch 4 nor the
64-bit route [L: both routes fail alike; I: an instance-dependent camera position after the attack, c5h-f and c5h-b
fail, c5h-c and c5h-d have passed]. Restore ok (verify rc 0), claim released by the stage. Reports kept:
`server/evidence/0517/hi_journey-report-1.md`, `-2.md`.

**Why release-large fails on some instances [I, 2026-11-18 03:30; the test is the third pass below]:** the game option
`AutoUnitCycle` in each instance's `UserSettings.ini` (`s20_usersettings.sh`, read 03:30): **0 on c5h-b and c5h-f** (where
release-large failed, 0463 and here), **1 on c5h-c, c5h-d** (where it passed for 0463, 2604, 4758) **and c5h-g** (where it
was recorded); a, h: 0; e, i: 1. With the option on, the game selects the next unit after a move and re-centres the camera
on it; the recording's camera at step 12 (`F:/c5-64bit-work/0392/4506/rec-large3/shot_0012_101500.png`: Amsterdam at the
top, the swordsman with 1/2 moves) and the replay's on c5h-b (`stop_0012.png`: Amsterdam lower, the swordsman 0/2) sit
on different places, so the recorded pixel of plot 50,50 holds another plot. The stage's `settings_files` sets only
`SinglePlayerAutoEndTurnEnabled = 0` and the window size; it does not set `AutoUnitCycle`. (A third difference in the
pictures, the swordsman's moves left, is not explained by this.) Proposal for the stage's owner: set `AutoUnitCycle = 1`
there too (backed up and restored by hash like the others), or record it in the journey's settings.

**Third pass, c5h-b with `AutoUnitCycle = 1`: verdict `validated`, D162 for the owner: yes [L, 2026-11-18 03:35-04:17 EST].**
`s21_hi_c5hb_unitcycle.sh`: c5h-b's `UserSettings.ini` backed up (`5ffa161d...`), `AutoUnitCycle = 0` -> `1`, the take
rerun `--from hi_journey`, the file put back by hash afterwards (restored `5ffa161d...`, checked). **release-basic PASSED**
(product 63 steps 576 s, stock 63 steps 485 s, 6/6 checkpoints the same) and **release-large PASSED** (product 21 steps
430 s, stock 21 steps 410 s, 5/5 the same) on the instance where release-large had failed twice with the option off:
**the instance dependence of release-large is `AutoUnitCycle` [L: off fails 2 of 2 on c5h-b, on fails 0 of 1 on the same
instance and build; with 0463's c5h-f failure and the passes on c5h-c/c5h-d, 5 of 5 runs fit]**. 0483's `-ia` exit crash
came once more after the stock replay of release-basic (pid 53896, 03:57:56, dialog watcher #5098; passed over).
`take_release.py --art-check`: "for the owner: yes (both journeys passed on this build, product and stock)", played on x64 DLL
`2b56a1cd...`, server `fc73e60e...`, proxy `3dfd7521...`, i0ia `3eaa3290...`. Restore ok, verify rc 0, claim released.
Pack manifest SHA-256 `4AB0E525B1DB93937A9DECE9D8CA3F676FCC6E241B90DC3326C74AD0E802324E`.

Kept (D141): `F:/c5-64bit-work/0517/layouts/` (the three layouts, each file checked against its source and its
SHA256.txt, `s22_keep_layouts.py`) and `F:/c5-64bit-work/0517/take/` (state, validated report, HI report, driver logs,
pack). Evidence in the repository: `server/evidence/0517/validated-report.md`, `take-state.json`,
`hi_journey-report-{1,2,3}.md` (user paths as `%USERPROFILE%`). Visual Checks: the HI lane's checkpoint pictures are in
the feed as DIAGNOSTIC under issue 0517; `visualcheck.py list --issue 0517 --result undescribed` lists none.

### 9.6 Reference runs remade and the crossed save test

The references remade on the batch-4 pins by the take (9.5): the D10 save test (tiny 0-100, standard 0-200, huge
350-400; 31,226 / 349,074 / 683,261 rows, the descriptor's `reference_counts`), the whole-games stage (four games, new
endings and rows, 9.5 table), drift, the instance arms. Results taken on batch 3 or earlier pins are not comparable.

**Which fix changes which game [L, 2026-11-18 02:12-02:16 EST]** (`s12_statscmp.sh`: `tools/d10/statscmp.py`, whole
`stats.db`, D24's skips only, both arms' direct plays of the D10 save test). "b3" is 0497's run_d10 of batch 3 +
#13522 + D176, which 0497 showed IDENTICAL to 0463's batch-3 plays (section 6), so it stands for batch 3:

| Pair | tiny x86 / x64 | standard x86 / x64 | huge x86 / x64 |
|---|---|---|---|
| batch 3 + 0508 alone (4805's run_d10) vs b3 | IDENTICAL / IDENTICAL | IDENTICAL / IDENTICAL | IDENTICAL / IDENTICAL |
| batch 3 + A10 alone (4806's parity run, fix (b)) vs b3 | IDENTICAL / IDENTICAL | IDENTICAL / IDENTICAL | IDENTICAL / IDENTICAL |
| batch 3 + A9 alone (4805's run_d10) vs b3 | DIFFERENT / DIFFERENT | DIFFERENT / DIFFERENT | DIFFERENT / DIFFERENT |
| **batch 4 (this take's savetest) vs batch 3 + A9 alone** | **IDENTICAL / IDENTICAL** (31,226) | **IDENTICAL / IDENTICAL** (349,074) | **IDENTICAL / IDENTICAL** (683,261) |
| batch 4 vs b3 | DIFFERENT / DIFFERENT | DIFFERENT / DIFFERENT | DIFFERENT / DIFFERENT |

So in the three D10 games **batch 4's only change of a game result against batch 3 is A9's**: 0508 and A10 alone change
nothing, #13522 and D176 nothing (0497), and batch 4 equals batch 3 + A9 row for row. Where A9 first shows (batch 4 vs
b3, x86; x64 the same): tiny turn 57 (MilitarySummary, WorldStateLog; 14 of 15 tables differ by turn 100), standard turn
50 (BuildingYields, WorldStateLog), huge turn 354 (BuildingInstantYields, BuildingYields, HandicapYields). Outputs
`server/evidence/0517/statscmp/`.

**The crossed save test against batch 3 [L, 2026-11-18]** (0463 3.5's method, `s13_crossed.sh`; `run_d10.py
--order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, AI logging off): both x64 arms in this take's layout bin
(server.exe of 58724e072, 0452 in both hosts), arm A's x64 DLL replaced by the batch-3 pin's (`2D737BD5...`):

| Run | x86 | x64 DLL | tiny | standard | huge |
|---|---|---|---|---|---|
| A | batch 4 `3737AE10` | batch 3 `2D737BD5` | part 1 pass; parts 2, 3 FAIL: 14 of 15 tables differ from turn 57 | part 1 pass; parts 2, 3 FAIL: 14 of 15 from turn 50 | part 1 pass; parts 2, 3 FAIL: 13 of 14 from turn 354 |
| B | batch 3 `13CFB6D1` (`i0ship-39f17f75`) | batch 4 `2B56A1CD` | the same: from turn 57 | the same: from turn 50 | the same: from turn 354 |

A 02:12-02:24 (tiny, standard) and 02:40-03:09 (huge), B 02:24-02:38 and 03:09-03:32 EST. The writer identity (part 1) holds across the batches; behaviour (part 2) and the
cross-loads (part 3) differ exactly where A9 first changes the game (the same first turns as batch 4 vs b3 in the table
above: tiny 57, standard 50, huge 354). Trap: the first runs did not play the huge game ("huge save missing": the
descriptor's reference-save path, a temp folder of 2026-11-10, is gone from this machine; the take passes its own input
copy `C:/c5-64bit-work/0517/run/inputs/Release-5.4.8-HUGE_0350.Civ5Save`, same SHA-256 `c499d3c6...`, with
`--huge-save`/`--huge-sha256`); the huge game was run on its own with that copy (`s19_crossed_huge.sh`). Reports
`server/evidence/0517/d10-crossed-{A,B}-report.txt`, `d10-crossed-{A,B}-huge-report.txt`.

### 9.7 What 0517 leaves for the MC

- **Lay out the three layouts** from `F:/c5-64bit-work/0517/layouts/` under the pins folder (`%USERPROFILE%/c5-i0/dlls/`:
  `i0ship-e96457fd`, `i0ship-e96457fd-ia`, `x64-c29442e8-p58724e0`). The locks on the branch already name them: until
  then `provision.py pinned_dll` refuses ("the instance-aware build of the i0 pin e96457fd5 is missing") and run_d10's
  and run_pair's defaults find no build [L, checked 2026-11-18 04:20].
- **The locks** (`deps/i0.lock`, `deps/dll.lock`): commit, describe, branch, fingerprint, the re-pin paragraphs and the
  release keys moved; batch 3's kept as `release-b3`, `instance-b3`, `verified-b3`, `tools-b3`, `epoch-b3`. The 43-civ
  keys still describe batch 2 (no 43 build of batch 3 or 4).
- **Fork fast-forwards** (the MC's step, as for batch 3): `i0-ship` and `master` 39f17f754 -> e96457fd5, `x64`
  4f4daac70 -> c29442e8c (both are fast-forwards: the batch-3 heads are ancestors).
- **COMMON rule 18, proposed text** (the MC's file): "Since 2026-11-18 (ticket 0517, DLL pin batch 4, D181): 32-bit
  `i0-ship-batch4` e96457fd5 (`i0ship-e96457fd`, instance-aware `i0ship-e96457fd-ia`), x64 `x64-batch4` c29442e8c with
  server.exe and proxy from repository 58724e072 in the layout `x64-c29442e8-p58724e0` (DLL 2B56A1CD..., server
  FC73E60E..., proxy 3DFD7521...; `deps/dll.lock` `release`). The batch carries upstream #13522 (shared zero block), the
  D176 air-strike fix, bug 0508's fix, A9 and A10. **A9 changes game results on purpose** (every D10 game: 31,226 /
  349,074 / 683,261 rows, was 30,666 / 351,496 / 689,879; the four whole games end differently): never compare a result
  with one taken on batch 3 or older. The others change none (upstream-fix-batch-4.md 9.6). Replaced: `i0ship-39f17f75`
  (-ia `7de39f71`), `x64-4f4daac7-pf8e9f42`."
- **Runs started on batch 3 finish on their pins and say so** (COMMON 18; the increment 2 release candidate 0509 stays
  on batch 3). After this batch the PGO profile is retrained on the new x64 pin (ticket 0513).
- **Tools to fix (owners: the release-driver/HI teams)**: (1) `stages/hi_journey.py` `settings_files` should set
  `AutoUnitCycle = 1` (9.5, third pass); (2) a crash without a box during a replay is raised as `LeftRunning` and leaves
  the instance laid with the release (9.5, first pass); (3) the descriptor's `reference_saves.huge.path` names a temp
  folder that is gone, so a plain `run_d10.py` without `--huge-save` cannot play the huge game (9.6). (1) is ticket
  0530; (2) and (3) are fixed by ticket 0533 (2026-11-18; the save's durable copy is
  `F:/c5-64bit-work/reference-saves/Release-5.4.8-HUGE_0350.Civ5Save`; `release-driver.md` section 0533).
- **Upstream drafts** for 0508, A9 and A10 are in `research/03-dll-engine-boundary/upstream-drafts/` (A10's on epic
  0504's branch until it merges); nothing was sent (D95, D130).

### 9.8 Not verified

- The 43-civ variant of batch 4 (not built).
- The D176 fix's product check on the batch-4 release candidate (0477's open item; 0497 section 7 item 5) and 0461's
  stock-route late-save check with the batch-4 `-ia` DLL: not run here (the HI journeys and the instance arms played
  the batch, without a bomber strike on a submarine).
- Whether stock 5.4.8 shows the farm-field crash (0410's open point): no stock load crashed in this ticket's runs.
