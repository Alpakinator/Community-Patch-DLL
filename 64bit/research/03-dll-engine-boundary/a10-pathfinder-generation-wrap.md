# A10: the path finder's 16-bit generation stamp wraps without clearing the node stamps (ticket 4806, epic 0504)

impl-4806, 2026-11-17, workstation (builds, counting runs) and `oldpc` (timing), account A. The owner's D181 (2026-11-17
15:25): A10 is a defect fix in the fork on both pins, measured in two forms, one recommended. Marks: `[C]` read in the
source or a dump, `[L]` seen in a run, `[I]` inferred. P0.2's counts (ticket 4800) are in
`../08-performance/speedup-p0-measurements.md` ("P0.2"); this file does not repeat them.

**Headlines:**

- Both exact fixes, (a) clear the stamps at the wrap and (b) a 32-bit stamp, read **0 stale cache data** on M 151-160,
  M 151-200 (two wraps), H 301-305 (a wrap) and the early game 1-50, and play **the same games as the pin** there (the
  one early-game difference of (a) is bug 0508's uninitialised read, gone when 0508 is fixed in every arm) [L].
- **No time cost** on `oldpc`, H 300-315: both within +0.03 % of the pin, nothing detectable above about 2 %; memory
  unchanged [L, D178 development series].
- **Recommended: (b)**, a 4-line diff that keeps the node cache at 24 bytes on x86 and x64 [C]. It is on fork branches
  `fix-a10-x64` 2915322be and `fix-a10-i0ship` bae2ec653 (one commit each, the same text); **x64 = x86 parity: the D24
  save test passes** on the two fixed DLLs [L].
- Upstream still has the defect and no report of it; draft for the owner: `upstream-drafts/a10-pathfinder-generation-wrap.md`
  (nothing sent).

## 1. The defect `[C]`

At both batch-3 pins (x64 `4f4daac70`, i0-ship `39f17f754`; `CvAStar.cpp`, `CvAStar.h` and `CvAStarNode.h` differ
between the two pins only in `CvPathNodeArray`'s save widths, not in the path finder) and on upstream `master`
`c722e8ea24` (2026-11-18 00:06 UTC, read through the GitHub API 2026-11-17 19:05 EST):

- `CvAStar::m_iCurrentGenerationID` is an `unsigned short` (`CvAStar.h`), 0 after construction, never saved.
  `CvAStar::FindPathWithCurrentConfiguration` (`CvAStar.cpp` line 355 at the pins) adds 1 before every search and sets
  it to 1 when it reaches `0xFFFF`: the IDs run 1..0xFFFE, a cycle of 65,534 searches per finder instance.
- Each node carries `CvPathNodeCacheData::iGenerationID` (`unsigned short`, `CvAStarNode.h`). Nothing clears the
  nodes' stamps at the reset to 1.
- **Writes of a stamp:** `UpdateNodeCacheData` (`kToNodeCacheData.iGenerationID = finder->GetCurrentGenerationID()`,
  the `//done!` line, 1057 at the pins); `CvAStarNode::clear()` (whole cache memset, stamp 0), called by `CvAStar::Reset`
  for the previous search's open and closed nodes only; the whole-struct copy to a stop node in
  `CvTwoLayerPathFinder::AddStopNodeIfRequired` (`pStopNode->m_kCostCacheData = current->m_kCostCacheData`).
- **The one compare** (every site named): `UpdateNodeCacheData`, `if (kToNodeCacheData.iGenerationID ==
  finder->GetCurrentGenerationID()) return;` (line 946 at the pins). An equal stamp skips the refresh of the node's
  cache (revealed/visible flags, terrain and territory entry, enemy and neutral units, stacking, cities, routes, the
  move-cost multipliers, airlift/sealift/port flags). No other code compares a stamp.
- **Other reads of the ID** (no compare): the path log line and log file name in `FindPathWithCurrentConfiguration`
  (`%d`, `PathfindingRun%06d`, debug logging only) and a `MOD_CORE_DEBUGGING` tactical log line in
  `CvTacticalAI.cpp` (`GC.GetPathFinder().GetCurrentGenerationID()`).
- **The alias:** a node stamped by search N of one cycle that is not stamped again and not cleared by a `Reset` before
  search N of the next cycle has a stamp equal to the current ID there; `UpdateNodeCacheData` then keeps cache data
  65,534 searches old. Nodes whose cache was filled as a neighbour (or an extra child) and that never entered the open
  or closed list are the ones `Reset` does not clear.
- P0.2 (4800, batch-2 pin, identical path-finder code) counted 0 such aliased compares over 5 wraps of the unit path
  finder and 4 of the step finder (about 243 million compares) [L]; the step finder never calls `UpdateNodeCacheData`.

## 2. The two fixes `[C]`

Scratch branches of the fork, each one commit on the x64 pin `4f4daac70` (clone `C:/c5-64bit-work/4806/fork`):

| Fix | Branch, commit | Diff | Layout |
|---|---|---|---|
| (a) clear the stamps at the wrap | `fix-a10-x64-clear` 226d14a7a | `CvAStar.cpp` +25, `CvAStar.h` +2: a virtual `ClearNodeGenerations()` (base layer in `CvAStar`, both layers in `CvTwoLayerPathFinder`) called where the ID is set back to 1; it sets every node's `iGenerationID` to 0 (the cleared-node stamp no ID uses) and leaves the cached data itself as it is | none (one vtable slot more in `CvAStar`; the class is not an engine interface) |
| (b) a wider stamp | `fix-a10-x64-wide` 2915322be | 4 lines: `m_iCurrentGenerationID`, `GetCurrentGenerationID()` and `iGenerationID` become `unsigned int`; the reset is `if (m_iCurrentGenerationID==0) m_iCurrentGenerationID = 1;` (0 stays the cleared stamp) | **none**: the stamp takes the two padding bytes after it; `CvPathNodeCacheData` stays 24 bytes, `iEndTurnDanger` stays at offset 16, x86 and x64 (clang-cl 19.1.7 `-fdump-record-layouts`, `C:/c5-64bit-work/4806/layout/`, `perf/evidence/0504/4806/layout.txt`); `CvAStar`'s member is followed by an `int`, so its 2 more bytes take padding too [I] |

Why both are exact `[C]`: the stamp decides only whether `UpdateNodeCacheData` refreshes a node. With (a) a stamp
equal to the current ID can only have been written by the current search (IDs are unique inside a cycle, and every stamp
of the old cycle is 0 after the wrap); with (b) the same holds until 2^32 - 1 searches of one finder (about 200,000 H
turns at 21,000 searches per turn). Where the pin does not alias, both fixes refresh exactly the nodes the pin
refreshes, so a game that met no alias plays identically; where the pin aliases, the fixes refresh the node (the
intended behaviour). Neither touches the cached data that P0.2's "not refreshed by the current search" reads see
(`CommonNeighborIsPassable`, `PathValid`, `CanEndTurnAtNode`, ...): fix (a) zeroes only the stamp, not the data.

Counting builds: 4800's counter stack (`x64-count-0504`, 6 commits) cherry-picked (`-x`) onto each fix:
`fix-a10-count-clear` a914beb9f (one conflict at the generation increment: the counter's `AStarGeneration` call placed
after the new block), `fix-a10-count-wide` 82c51e09c (clean; plus one commit making the counter's generation types and
cycle 32-bit). Built `--config fast` with `CL=-DVP64_COUNT_ASTAR` only (D107: functional counting).

## 3. Counts with the fixes `[L, 2026-11-17 19:37-19:53 EST, workstation]`

Runs: `perf/evidence/0504/4806/run_counts.sh` (4800's `run_count.py` copied, work folder `C:/c5-64bit-work/4806/runs/`):
headless `server.exe` direct mode of the batch-3 pin layout `x64-4f4daac7-pf8e9f42` (server `ED829BDA...`, which
carries bug 0452's load fix) with the arm's DLL swapped in, 0438's statistics-on database (`9335186b...`), the toolkit
game folder, `dll_log_flags 0`, each run under `quietlock.py run` (owner 0504, load before and after in
`load-<run>.txt`). M = `TS0438-M-T150` to 160, H = `TS0438-H-T300` to 305, E = a new game from
`server/specs/continents-standard-fixed-seed.json` (map seed 1001, sync seed 2002) to 50; each played 2 turns past the
window (the counter closes a turn at the next turn's start). Counting DLLs (`--config fast`, `CL=-DVP64_COUNT_ASTAR`):
**count-clear** `023c3cd1c71be8991d9d9ace4eaafac494be8a364d75253733244ce04851fd17` (23,511,552 B, fix-a10-count-clear
a914beb9f), **count-wide** `6bbccbcddd2c236b273709f32b5df9eabca4603e03a3c8bfbaaf1c02a34a574f` (23,511,040 B,
fix-a10-count-wide 82c51e09c); reference **ref** = the pin layout's own DLL `2D737BD5...`. Totals by
`perf/evidence/0504/4806/summarize_counts.py` (`counts.txt` in the evidence folder).

| Window | arm | unit path finder searches / its wraps in the window | step finder searches / wraps | compares fresh / **aliased** / unequal | **aliased reads** (every site) | generation mismatches, nodes without a slot |
|---|---|---|---|---|---|---|
| M 151-160 | clear | 31,323 / none (ID 34,588 at 160) | 24,536 / none | 8,814,434 / **0** / 3,165,382 | **0** | 0, 0 |
| M 151-160 | wide | the same | the same | the same | **0** | 0, 0 |
| H 301-305 | clear | 104,964 / 1 (turn 303; ID 60,963 at 305) | 76,374 / 1 (304) | 56,903,422 / **0** / 20,752,830 | **0** | 0, 0 |
| H 301-305 | wide | 104,964 / **0** (ID 126,497 since the load) | 76,374 / 0 (ID 90,490) | the same | **0** | 0, 0 |
| E 1-50 | clear | 31,761 / none (ID 31,935 at 50) | 15,022 / none (ID 57,084) | 4,972,605 / **0** / 1,939,286 | **0** | 0, 0 |
| E 1-50 | wide | the same | the same | the same | **0** | 0, 0 |

Long window, added to see fix (a)'s clear run more than once (`run_counts_long.sh`, M to 200, 20:20-20:29 EST):

| Window | arm | unit path finder searches / wraps | step finder searches / wraps | compares fresh / **aliased** / unequal | **aliased reads** | mismatches, no slot |
|---|---|---|---|---|---|---|
| M 151-200 | clear | 190,219 / 2 (turns 169, 186) | 169,402 / 2 (173, 191) | 72,583,382 / **0** / 23,802,774 | **0** | 0, 0 |
| M 151-200 | wide | 190,219 / 0 (ID 193,484 at 200) | 169,402 / 0 (ID 171,713) | the same | **0** | 0, 0 |

(4800 counted 81,932,019 / 26,017,389 compares on M 151-200 with the batch-2 layout: the batch-3 layout's server, with bug
0452's load fix, plays another M game after the load; compare like with like only.)

So **both fixes read 0 stale (aliased) cache data in all three windows and in M 151-200** [L]. The compare totals equal 4800's counts on
the batch-2 pin for H and E exactly (56,903,422 / 20,752,830; 4,972,605 / 1,939,286): the same searches in the same
games. With fix (b) the IDs pass 65,535 without a wrap (126,497 and 90,490 at H 305), and the counter's own check (the ID
equals its unwrapped twin modulo the cycle) holds at every search. Only H has a wrap in these windows: the IDs start at 0
at the load, M 150-160 and E 0-50 end below 65,535 (P0.2). The read classes other than "aliased" (earlier generation
of the same cycle, cleared) are the same in both arms and are P0.2's not-refreshed reads, not A10. With fix (a) no read
in H 301-305 touched a node stamped in the previous cycle ("previous cycle" 0 in both arms), so its clear at turn 303
changed nothing the searches read.

## 4. Game identity against the pin `[L, 2026-11-17, idtrace.py + tools/d10/statscmp.py]`

`perf/evidence/0504/4806/identity.sh` (0438's `idtrace.py`, copied: both `stats.db` trimmed to the window, then
`statscmp.py --from-turn LO`); output `identity.txt` in the evidence folder.

| Window | ref = clear | ref = wide | rows compared |
|---|---|---|---|
| M 150-160 | IDENTICAL | IDENTICAL | 32,838, 15 tables |
| H 300-305 (a wrap of the unit path finder at 303) | IDENTICAL | IDENTICAL | 125,210, 14 tables |
| M 150-200 (wraps at 169 and 186; `identity-long.txt`) | IDENTICAL | IDENTICAL | 216,819, 15 tables |
| E 0-50 | **DIFFERENT** (only `ReligionBeliefYields.IsReligionOwner`, 66 rows from turn 26; every other table and column the same) | IDENTICAL | 20,626, 15 tables |

The E difference is **bug 0508**, not A10 [I, see below]: `IsReligionOwner` is the column the statistics logger fills
from a pantheon's uninitialised holy-city coordinates (`../08-performance/speedup-p1-pgo.md` section 6); a pantheon is
founded at turn 26 in this game, and which garbage the stack slot holds depends on the build's code layout (the
count-clear build adds a virtual function and code in `CvAStar`). No path-finder ID wrapped anywhere in E (section 3),
so fix (a)'s new code never ran there; only its presence in the binary differs. Check with bug 0508 fixed in all three
arms: section 4.1.

### 4.1 The early game with bug 0508 fixed in every arm `[L, 2026-11-17 20:29-20:38 EST]`

4805's fork branch `fix-0508-x64` (77d110cc4, "Initialise a pantheon's holy-city coordinates", on 4f4daac70) fetched into
this ticket's clone; `src-0508-pin` = that commit, `fix-a10-0508-clear` a4605017d and `fix-a10-0508-wide` 062ec3842 =
each A10 fix + a cherry-pick `-x` of it (scratch). Built `--config fast` (z-pin `66237c57...`, z-clear `eb4893ac...`,
z-wide, all 23,496,192 B), E 0-50 as above (`chain4.sh`, `identity-0508.txt`):

| E 0-50, 20,626 rows, 15 tables | result |
|---|---|
| pin+0508 = clear+0508 = wide+0508 | **IDENTICAL** (all three pairs) |
| pin release (ref, no 0508 fix) = pin+0508 (fast) | **IDENTICAL** |

So with the uninitialised read gone, fix (a) plays the early game exactly as the pin does, and the 0508 fix itself
does not change the pin's game here (as 4801 found on batch 2). **The E difference of section 4 is bug 0508 [L], not
A10.**

So, **by design and as measured, neither fix changes a game that met no alias**: the first turn and table where a
window's `stats.db` differs from the pin is "none" in M, H and (for fix (b)) E; fix (a)'s E difference is bug 0508's
build-dependent garbage. (M 150-160 has 32,838 rows on the batch-3 layout against 4800's 32,631 on the batch-2 layout:
a different server build, compared here only like with like.)

## 5. Timing, D178 development series on `oldpc`

Testbed job `20261117-193640-2433` (submitted 19:36 EST, `--quiet`, planned 50 min; `submit_tb.sh`, `tbjob.py` ->
`tools/x64-dll/pgo/tbseries.py` -> `perf/tbjobs/series.py`): three arms, each the batch-3 pin layout's bin (server
`ED829BDA...`) with its release DLL built with the same tools and stubs: **pin** `31b7b7c0...` (4f4daac70), **clear**
`b13ccc17...`, **wide** `26c4f9c9...`; `TS0438-H-T300` 300 -> 315 (turns 301-315 timed), 3 interleaved runs per arm,
AI logging off, 0438's statistics-on database; `--compare pin,clear` and `pin,wide` on round 1's `stats.db`; the GPU
driver read into `gpu.txt` (D175).

**Result: neither fix costs or gains measurable time** [L, D178 development signal, not grade A (3 runs per arm)]:

```
Metric / tool:        seconds per AI turn (server turn-times.tsv, boot and load excluded), perf/tbjobs/series.py through
                      perf/evidence/0504/4806/tbjob.py + tools/x64-dll/pgo/tbseries.py (start turn 300), branch task-4806-a10
Builds:               pin = batch-3 pin layout bin with release DLL 31b7b7c0... (4f4daac70); clear = b13ccc17...
                      (fix-a10-x64-clear 226d14a7a); wide = 26c4f9c9... (fix-a10-x64 2915322be); all release (LTO),
                      the same tools (this branch) and stubs; server.exe ED829BDA... in all three
Game / config:        TS0438-H-T300 (Huge Continental Drift) 300 -> 315; 0438's statistics-on database (D35 settings);
                      AI logging off (dll_log_flags 0)
Series:               oldpc, job 20261117-193640-2433, quiet (alone, undisturbed, foreign cores 0.071), 2026-11-17
                      20:40:39-21:15:19 EST, 3 interleaved runs per arm, order rotated per round; GPU NVIDIA GeForce
                      RTX 2080, driver 32.0.16.1088 (2026-07-21) (D175)
Result:               pin   15.117 s/turn (sd 0.112, cv 0.74 %, 15.036-15.245, 95 % CI 14.839-15.395)
                      clear 15.122 s/turn (sd 0.101, cv 0.67 %, 15.022-15.225, 95 % CI 14.870-15.374)
                      wide  15.120 s/turn (sd 0.145, cv 0.96 %, 15.002-15.283, 95 % CI 14.759-15.481)
                      clear - pin = +0.005 s/turn (+0.03 %, 95 % CI -0.272 .. +0.283), detectable ~0.244 s (1.6 %)
                      wide - pin  = +0.003 s/turn (+0.02 %, 95 % CI -0.334 .. +0.340), detectable ~0.297 s (2.0 %)
Boot + load:          pin 3.53, clear 3.46, wide 3.45 s
Memory (D151 metric): peak private bytes after the window 379.8-380.6 MB in every run of every arm; peak working set
                      306 MB mean in each arm: unchanged
Machine load:         total CPU mean 15.9 %, max 35.8 %; foreign CPU 148 s over 35 min (Defender 77 s, svchost 15 s,
                      tablet service 10 s, explorer 9 s)
Why (no) move:        (b) changes only the width of a compare and a store; (a) adds one pass over 2 x 10,240 nodes at each
                      wrap of a finder (5 wraps of the unit path finder in this window), microseconds against 15 s turns
Correctness:          round 1's stats.db on oldpc: pin = clear and pin = wide, 14 tables, 339,035 rows, IDENTICAL
                      (turns 300-315, which hold 5 wraps of the pin's unit path finder: 303, 306, 309 counted by 4800, and
                      about 312 and 315 [I]); D24 parity of the chosen fix: section 7
```

Evidence: `perf/evidence/0504/4806/oldpc-20261117-193640-2433/` (`out/H/series.json`, `turnbench.json`, `cmp-pin-*`,
the ledger run records, `gpu.txt`, `quiet/summary.json`, `job.log`, `analyze.txt` = `perf/tbjobs/analyze.py <out>/H --ref
pin`). A grade A claim would need 6+ runs per arm; none is made: A10 is a correctness fix, and the series shows only
that neither form has a cost above about 2 % on H.

## 6. Recommendation: fix (b), the 32-bit stamp

Both fixes are exact and both read 0 stale data (sections 2-4). Fix (b) is recommended:

- **The smaller upstream diff**: 4 changed lines in 3 files against 27 added lines and a new virtual method in 2 files
  for (a); a reviewer can check (b) at a glance.
- **No layout change**: the stamp fills existing padding (24-byte node cache on x86 and x64), so no memory and no save
  change (the cache is not serialized).
- **No new work**: (a) adds a pass over both node layers (about 2 x 10,240 nodes on a huge map) every 65,534 searches,
  and a virtual call; (b) adds nothing.
- **What (b) leaves**: a wrap after 2^32 - 1 searches of one finder in one session (about 200,000 H turns at 21,000
  searches per turn) [I]; (a) has no such limit. Not a practical difference.
- **Identity**: both are identical to the pin wherever bug 0508 is out of the picture (sections 4, 4.1, 5). Until
  0508's fix is in the pins, (a)'s binary moves 0508's garbage in a new game (section 4) and (b)'s does not in the
  game measured; that is 0508's defect and luck of code layout, not a property of either fix [I], and weighs little.
- Timing: no difference either way (section 5: both within +0.03 % of the pin, nothing detectable above about 2 %), so
  timing does not decide between them.

## 7. Fix branches, builds and parity

Fork branches (clone `C:/c5-64bit-work/4806/fork`; one commit each, the same message "Path finder: 32-bit generation
ID for the node cache", `git patch-id --stable` c1ef6bb8 for both; `final_branches.log`):

| Branch | Head | Base (pin) | describe |
|---|---|---|---|
| `fix-a10-x64` | `2915322be5928dbd305b5807083b27f69a04d0e1` (the scratch commit of `fix-a10-x64-wide` itself) | x64 batch 3 `4f4daac70` | Release-5.4.8-58-g2915322be5 |
| `fix-a10-i0ship` | `bae2ec6536ed98a815986df8040921861b655e2c` (cherry-pick of it, no `-x`, clean) | i0-ship batch 3 `39f17f754` | Release-5.4.8-45-gbae2ec6536 |

Builds (2026-11-17, this repository's tools at the branch `task-4806-a10`, stubs built once into
`C:/c5-64bit-work/4806/deps/stubs`):

| Build | Config | DLL SHA-256 | Size | Notes |
|---|---|---|---|---|
| x64 `fix-a10-x64` (rel-wide) | release (LTO), `build.py --shorten --unlocked` | `26c4f9c9164e8d14e86572f713ebfc553b806317bf3ce43d43d9539466627ec9` | 23,669,760 | 19:09:57-19:19:26 EST, link 431 s, narrowing 794 / baseline 798 (0 new), 0 unresolved; layout `C:/c5-64bit-work/4806/layout/x64-fix-a10-wide/bin` = the pin layout with only the DLL and PDB replaced |
| x86 `fix-a10-i0ship` | release, `build_vp_clang.py --config release` on a `git archive` copy, version "Release-5.4.8-45-gbae2ec653 Copy" | `43672a2f9ac7679989786d37a9bcfd82e8b6bbb13deedf1e0171267fa7e3e447` | 17,147,904 | 19:53:48-19:59:59 EST (link 294 s) |
| x64 fix (a) (rel-clear, scratch) | release | `b13ccc17d9ffc7e9e4543a2b9b93d8f60e2da3731d2eacb7c65fc382a5d310d0` | | timing arm only |
| x64 pin 4f4daac70 (rel-pin) | release, same tools and stubs | `31b7b7c0a47d9b081e3811ca45269946497ae58f7c455476bd6732317ff0236f` | 23,669,760 | the timing reference (the layout's own `2D737BD5...` was built with the 0463 tools) |

**Parity: x64 = x86, D24 save test PASS** [L, 2026-11-17 20:00:02-20:20:12 EST, workstation]:
`perf/evidence/0504/4806/parity.sh wide` = `tools/d10/run_d10.py --x86-dll <the x86 fix DLL 43672a2f...> --srv-bin
<the x64 fix layout, DLL 26c4f9c9...> --order-exception none --jobs 10` (AI logging off in both arms, D49): tiny 0-100,
standard 0-200 and huge 350-400 each pass part 1 (writer identity; only the version-string exception), part 2 (x86
direct = x64 direct: 30,666 / 351,496 / 689,879 rows) and part 3 (cross-load). Report
`perf/evidence/0504/4806/parity-wide-report.txt`. The row counts equal those of the batch-3 pin's own save test
(`deps/dll.lock` `verified`, 0463); the fixed games were not compared row by row with the pin's save-test games (that
run's folders are not kept), only in the windows of section 4. At H's rate of about 21,000 unit-path-finder searches per turn, the huge game's 50
turns from a load at 350 would make about a million searches, about 16 wraps of the 16-bit pin [I: that game's rate
was not counted]; with the fix neither build wraps there.

## 8. Upstream

Checked 2026-11-17 19:05 EST: upstream `master` `c722e8ea24` still has the 16-bit ID and the reset to 1 (code above);
`gh search issues/prs` in LoneGazebo/Community-Patch-DLL for "GenerationID", "pathfinder generation", "generation id",
"0xFFFF" and "pathfinder cache" finds nothing about it (one 2017 MP desync issue, #3511, matches "pathfinder cache" by
words only). The last commits to `CvAStar.cpp` (#13582 clang-tidy 2026-11-17, #13428, #13423, 472e1163d) do not touch
the stamp; the node struct is unchanged against the pin. Open pull requests touching the path finder (#13520, #13487,
#13518, #13483) do not touch it either. Draft: `upstream-drafts/a10-pathfinder-generation-wrap.md` (nothing sent,
D95, D130).

## 9. Not verified

- **The alias itself never occurred** in any run (sections 3-4, and P0.2's 5 wraps): the fixes' behaviour on an aliased
  node, a refresh where the pin skips it, rests on the code [C], not on a run. No synthetic test forces an alias.
- The 32-bit build's `CvAStar` object size with the wider member (padding after it inferred [I]; the node struct is
  [C]).
- The counts and identity of fix (b) on the i0-ship build alone (the x86 build ran only inside the parity save test).
- The fixed games against the pin's save-test games row by row (only equal row counts; section 7).
- A game with a window (the client path): headless only. The fix touches no engine interface or hook.
- Fix (a) on the i0-ship pin (built and measured on x64 only; not chosen).

## 10. Fork branches in the shared clone

Pushed 2026-11-17 to the shared local fork clone `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL` (not to GitHub):
`fix-a10-x64` 2915322be, `fix-a10-i0ship` bae2ec653, and the scratch branches `fix-a10-x64-clear` 226d14a7a,
`fix-a10-x64-wide` (= `fix-a10-x64`), `fix-a10-count-clear` a914beb9f, `fix-a10-count-wide` 82c51e09c
(`perf/evidence/0504/4806/push_fork.sh`). The 0508 scratch branches `fix-a10-0508-*` stay in
`C:/c5-64bit-work/4806/fork` only.
