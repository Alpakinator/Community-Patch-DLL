# Upstream #13522 (shared zero block for empty 2D database tables): what it does for our routes, measured

Ticket 0461 (account B, 2026-11-17). Follows `upstream-fix-batch-1.md` section 3 (one-off 9020: the PR read, the
writer check, a measurement plan) and `upstream-fix-batch-2.md` 2.8 item 3 (one-off 9075: "worth a measurement ticket
for i0"). Scope: whether the PR applies to the stock route (the 32-bit `-ia` pin DLL in the 32-bit EXE) and to the
product path (the proxy in the EXE, the DLL in the 64-bit server), one A/B measurement on a test machine, and a
recommendation for the next pin batch.

## 1. What the PR changes [C, PR text and diff read 2026-11-17]

- **State**: open, head `85b2e7bc4` on base `ac3b46ec2` (after 5.4.8), last updated 2026-10-31; two review comments,
  no review decision. Fetched into the fork clone as `refs/pr/13522` (no branch, nothing pushed). The diff
  `ac3b46ec2..85b2e7bc4`: 11 files, +269 lines, SHA-256 `2B6E80B4...58E5` (19,816 B, kept as
  `F:/c5-64bit-work/0461/pr13522-ac3b46ec2-85b2e7bc4.patch`).
- **Mechanism**: `CvDatabaseUtility::Initialize2DArray` gives every database entry a dense `[rows][columns]` block
  for each of 92 yield tables (building, belief, policy, trait, corporation, event choice, improvement, feature,
  terrain, tech, unit). The PR calls a new `CvDatabaseUtility::Share2DArrayIfZero` at the end of the 12
  `CacheResults` functions that fill them: a table whose values are all 0 is freed and pointed at one shared,
  reference-counted all-zero block of the same shape. The shared block is made with `VirtualAlloc` (zeroed pages; only
  the row pointers are written) and then set `PAGE_READONLY` with `VirtualProtect`, so a later write through a shared
  table faults at once instead of changing every entry that shares it. `SafeDelete2DArray` releases one reference;
  the last one frees the block.
- **Values**: the shared block has `Initialize2DArray`'s layout (row pointers, then rows), so every read returns the
  same value as the dense zero block, row-pointer getters included. No save-format change, no game-result change
  expected (the PR's 64-bit check: 0 mismatches over 91.8 million values; reviewer JohnsterID: game state equal to
  master on the turns compared).
- **Upstream's numbers** (not ours): PR author, VP + EUI files: 44.7 MB dense, 41.5 MB freed; VP + 24 mods: 389 MB
  freed. Reviewer JohnsterID on the 32-bit DLL (VC9 Release, huge map, 12 AI, CP + VP only): private bytes about 48 MB
  lower at the first turn after loading a turn-100 save, about 55 MB lower 30 turns later; right after loading a
  turn-150 save free address space +46 to +48 MB, largest free block +53 to +56 MB, committed -42 MB; after 15-30
  turns of play the free address space was the same as master's while committed stayed 45-55 MB lower (the CRT heap
  keeps the freed blocks reserved and reuses them). Reviewer schnetziomi5 proposed a lazy variant (allocate a table only
  on its first non-zero write) and `const` members; not in the PR.

## 2. Does it apply to our routes [C unless marked]

- **Cherry-pick onto the pins**: `git merge-tree --write-tree --merge-base ac3b46ec2 <pin> 85b2e7bc4` (a simulated
  cherry-pick of the single commit) is clean on the i0 pin `26234371e` and on the x64 pin `04b1211b8` (2026-11-17).
  `git apply` of the PR diff onto `git archive 26234371e` + `ia-ee893bc18-440878983.patch` is clean (arm B below).
- **Writer check on the current i0 pin** (the 9020 script's method, on the pin + ia + PR tree): the 55 member names the
  PR shares are written 96 times, all inside a `CacheResults` function, as on 5.4.8 and on the PR's base. So the pin
  batches since 9020 (batch 1, batch 2, the -ia instruments) added no writer [C, regex over `CvGameCoreDLL_Expansion2/
  *.cpp`; writes through a returned row pointer are not covered by the regex; the PR's own check says the 22 row-pointer
  getters only read].
- **Stock route** (the 32-bit DLL in the 32-bit EXE): applies. These tables live in the DLL's heap inside the EXE's
  4 GB (LARGEADDRESSAWARE) address space, which is the stock route's main crash limit (`upstream-fix-batch-2.md` 2.8;
  9074's Marathon T1100 failures, `research/08-performance/marathon-stress-t2000.md` 4.1). Measured in section 3.
- **Product path**: the DLL runs in the 64-bit server, where address space is not short; the PR would still cut the
  server's commit by the freed tables (8-byte row pointers there, so somewhat more than in 32-bit) [I, not measured].
  The proxy in the EXE builds no database tables: `proxy/` has no `CvDatabaseUtility`, `Initialize2DArray` or
  `CacheResults` (grep, 2026-11-17), so the EXE's address space on the product path is unchanged by the PR [C].

## 3. Measurement

**Builds.** Arm A = the pin's instance-aware build `i0ship-26234371-ia` (SHA-256 `3C2F47DA...A06C`, 17,475,584 B,
`deps/i0.lock` `instance`; what the testbed instances carry). Arm B = the same recipe plus the PR: `git archive
26234371e`, `git apply` of `ia-ee893bc18-440878983.patch`, `git apply` of the PR diff, version stub
`Release-5.4.8-39-g26234371e+ia440878983+pr13522-85b2e7bc4 Copy`, `build_vp_clang.py --config release` (clang-cl
19.1.7, LTO; 2026-11-17 02:19-02:27 EST on the workstation under `quietlock.py run`): SHA-256 `692DD774...AA1F`,
17,478,656 B (+3,072 B), kept in `F:/c5-64bit-work/0461/armB/` (with its PDB) (build script
`server/evidence/0461/build_arm_b.py`; not repeatable byte for byte, as every 32-bit build). A was not rebuilt:
the two builds differ also in the embedded source path and build time (`headless-and-parallel-testing.md` 6.1b), which
does not move memory by megabytes.

**Job.** `tools/testbed/jobs/addrspace_ab.py` (new, this ticket): for each arm in an interleaved order, the arm's DLL,
the save `TB-HUGE_0350` (Huge Continents, turn 350, SHA-256 `C499D3C6...88C3`; testbed database `BD9B8AA1...DD6F`), and an address-space trace of the 32-bit
process every second with `shell/tools/addrspace.py` (private bytes, committed, reserved, free, free below 2 GB, the
largest free block). Two modes: in the game (`--headed`: set-dll in the job's instance, launch, in game, snapshots at
load and after each group of AI turns, stop, restore by hash) and `--headless` (the 32-bit headless host c5h through
`hostruns.py`, AI logging off, the save continued to `--to-turn`). Summary: `server/evidence/0461/summarize.py`.

### 3.1 Headless host (c5h), oldpc, 2026-11-17 [L]

oldpc had no logged-on session from at least 02:03 EST, so headed jobs were held; the DLL side was measured first in
the 32-bit headless host, which loads the same DLL, database and save without the EXE. Job `20261117-023038-bbe0`
(02:30-02:41 EST; GPU driver 32.0.16.1088 since 01:40, not relevant to a headless run), order A,B,B,A,A,B, turn 350 ->
360, one sample per second; evidence `server/evidence/0461/headless-20261117-023038-bbe0/`, summary
`server/evidence/0461/headless-summary.txt`. Mean of 3 runs per arm (sd, range):

| point | metric | A (pin -ia) | B (+ #13522) | B - A |
|---|---|---|---|---|
| after load (last sample before play) | private bytes | 271.0 MB (sd 23.9) | 228.1 MB (sd 24.6) | **-42.9 MB** |
| | free address space | 3635.7 MB | 3683.0 MB | **+47.3 MB** |
| turn 360 (10 AI turns later) | private bytes | 339.1 MB (sd 24.5, 322.8-367.3) | 269.1 MB (sd 3.3) | **-70.0 MB** |
| | free address space | 3565.7 MB | 3627.5 MB | **+61.7 MB** |
| whole run | peak private (tbmon, 0.5 s) | 368.0 MB (366.2-369.9) | 315.3 MB (312.9-319.3) | **-52.7 MB** |
| | min free address space | 3535.2 MB (3534.0-3536.2) | 3624.1 MB (3624.1-3624.2) | **+88.9 MB** |
| | seconds per AI turn | 7.3 | 7.3 | 0.0 |

- The saving at load matches upstream's count for CP + VP (41.5 MB) and JohnsterID's EXE numbers at load (+46 to
  +48 MB free) [L]. The "after load" point is noisy (sd 24 MB in both arms): it falls on whatever sample preceded the
  first turn; the ranges of A and B overlap there, the means differ by the expected amount. At turn 360 and over the
  whole run every B run is better than every A run (ranges apart).
- In the headless host the free-space gap did **not** close over 10 turns (+62 MB at turn 360, +89 MB at the run's
  worst point), unlike JohnsterID's EXE runs, where free address space was equal to master's after 15-30 turns. The
  host's heap is not the EXE's, so this does not settle the EXE question [I].
- **50 turns** (job `20261117-024419-e245`, 02:44-03:13 EST, order A,B,B,A, turn 350 -> 400, one sample every 2 s;
  evidence `server/evidence/0461/headless-20261117-024419-e245/`, summary `headless-50turns-summary.txt`) [L]. At each
  ten-turn checkpoint, mean of 2 runs per arm (every run of an arm within 1 MB of the other except B's free space, sd 11):

  | turn | private B - A | free address space B - A |
  |---|---|---|
  | 360 | -43.7 MB | +47.3 MB |
  | 370 | -54.3 MB | +39.4 MB |
  | 380 | -52.1 MB | +39.4 MB |
  | 390 | -51.9 MB | +39.4 MB |
  | 400 | -53.5 MB | +39.4 MB |

  So in the headless host the saving holds for 50 turns: about 52 MB less private memory and 39 MB more free address
  space; the heap did not hand the freed space back to later allocations within that time, contrary to what
  JohnsterID saw in the EXE. Whole-run extremes say little here: B's highest private and lowest free sample came after
  the turn-400 checkpoint, in the host's teardown (s 427 of 429, the moment of the exit crash below), and A's came during play
  (s 155 and 300 of 431); tbmon's 0.5 s peak private is -49.8 MB (A 387.4, B 337.6). The 2 s trace's "load" point
  is not usable in this job (one A sample fell inside the load). Turn time 8.3 s per AI turn in both arms.
- The largest free block in c5h is the untouched region above 2 GB (about 1.9 GB) in both arms: it says nothing here.
  In the EXE the largest block is the number that matters (the stock route's crashes end at 1-3 MB, upstream #13254;
  9074's T1100 probes 32-45 MB above 2 GB): only the game run (3.2) measures it.
- Turn time is unchanged (7.3 s per AI turn in both arms; not a timing claim, the job was not quiet).
- **Side finding [L]: c5h with the `-ia` pin DLL ends every run with host exit 0xC0000005 after the target turn's
  checkpoint** (11 of 11 runs over the three jobs, both arms: `"failed", "error": "host exit 3221225477"` 1-2 s after the turn-360
  checkpoint). All 10 turns were played and timed in every run, so the measurement stands; the testbed's headless series
  use the plain pin DLL, not the `-ia` one. Cause (ticket 0484, 2026-11-17, [C, dump]): MemoryImports of the `-ia` build
  leaves the allocator import slots pointing into the game DLL, and c5h's `FreeLibrary` at shutdown makes the next allocation
  call freed code; the reports written after it are lost, play outputs are not; fixed in `hostruns.py` with
  `--keep-dll-loaded` (`headless-host.md` section 5). *Corrected 2026-11-17; it read "Cause not investigated (the `-ia`
  instruments at the host's teardown are the first suspect [I])".* Each run also logged one assert, `CvGame.cpp:6003 "Failed to generate diplo response query"`, in
  both arms alike.

### 3.2 In the game (the EXE), oldpc

**Not measured yet.** oldpc was restarted by Windows Update at 02:04 EST (MC, 2026-11-17) and nobody was logged on, so
headed jobs were held. The job is queued as `20261117-031426-7498` (submitted 03:14 EST at commit 0b0c33105, `--headed`,
order A,B,B,A, the save loaded, 30 s rest, a snapshot, then 5 AI turns + snapshot twice; the trace every 5 s; the
instance's DLL restored by `set-dll` and the standard files by hash at the end). The first attempt
`20261117-024112-afc6` was cancelled before it started, to keep one job per queue place while the 50-turn headless job
ran. The MC runs it once the owner logs on to oldpc; read its `out/addrspace_ab.json` with
`python -I server/evidence/0461/summarize.py <file>` (labels `load`, `turns-1`, `turns-2`). It answers the two things
the headless host cannot: the largest free block of the EXE (the stock route's real limit) and whether the EXE's heap
gives the freed space to later allocations (JohnsterID: free equal after 15-30 turns, committed still 45-55 MB lower).

## 4. Recommendation for the next pin batch

MC reading, not a decision [I]:

- **Take #13522 into the next i0 pin batch, on both pins** (`i0-ship` and `x64`, one cherry-pick `-x` of `85b2e7bc4`,
  clean on both, section 2), provided the EXE job of 3.2 shows the same direction (no less free address space or largest
  free block with B at load or after 10 turns). It costs nothing in game results (values identical, writer check holds on
  the pin), nothing measurable in turn time, and gives the stock route about 43-54 MB less private memory and 39-47 MB
  more free address space in the DLL's host with CP + VP alone (3.1), more with mod sets (upstream: 389 MB with 25
  mods). The x64 pin takes it for one source (the x64 branch takes every i0 commit) and saves commit in the server; it
  changes nothing for the EXE on the product path.
- **Checks in the batch**: the save test (`run_d10.py`, x86 = x64; values are read identically, so it should pass
  unchanged); one stock-route game of a few turns on a late save in the EXE (the read-only pages fault at once on any
  write we missed); a grep of every new pin commit for writes to the 55 shared names (`writercheck.py`'s regex, kept in
  `server/evidence/0461/`).
- **Risks**: (a) a future commit of ours or upstream's that writes one of these tables crashes at once (by design; the
  writer check in each batch guards it); (b) upstream may merge a different form (schnetziomi5's lazy allocation and
  `const` members): then replace ours with upstream's at the following pin change, which conflicts only in these 11
  files; (c) the PR is open and unreviewed by the maintainers: carrying it is our choice, not upstream's.
- **Not a reason to wait**: the `-ia` DLL's exit crash in c5h (3.1) happens in both arms and is not caused by the PR.

## 5. Not verified

- The EXE (3.2): the largest free block and private bytes of `CivilizationV_DX11.exe` with each arm; queued, not run.
- Game results with B: no save test or game-result comparison was run (the values are read identically by construction
  and by upstream's 64-bit check; the turn times matched to 0.1 s).
- The headless numbers rest on 3 runs per arm (10 turns) and 2 runs per arm (50 turns), one save, CP + VP only, AI
  logging off; larger mod sets were not tried.
- ~~The cause of c5h's exit crash with the `-ia` DLL (3.1).~~ Found by ticket 0484 (`headless-host.md` section 5).
- The x64 server's commit with the PR (the product path's only effect) was not measured.

Files: `tools/testbed/jobs/addrspace_ab.py` (the job), `server/evidence/0461/` (results of both headless jobs,
`summarize.py`, the summaries, and the ticket's helper scripts `build_arm_b.py`, `mergecheck.py`, `writercheck.py` with
`members.txt`; they name the workstation's scratch paths of 2026-11-17).
