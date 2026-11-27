# The server's allocator: the heap's share of a turn and the Segment Heap trial (ticket 5037)

Ticket 5037 (epic 0647, the Architect's draft T7, row VX5 of
`research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md`). Written by impl-5037, 2026-11-24.
The server (`server.exe`) and the x64 DLL it hosts allocate through the CRT (`/MD`, ucrtbase), whose heap is the
process heap. Question: does another heap implementation make an AI turn faster or smaller? Step one is the Segment
Heap, switched on by one line in `server.exe`'s application manifest; step two (mimalloc from its own source) runs only
on the MC's word after a go. The value floor of the third speedup list's first review: 0.3 % of an H or M turn.

## 1. How the Segment Heap is switched on `[C, L 2026-11-24]`

- `server/build_server.py --heap-type segment` writes `<out>/server/server.heap.manifest` and links it into
  `server.exe` (`/MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:...`, lld-link 19.1.7). The manifest holds only
  `<windowsSettings><heapType xmlns="http://schemas.microsoft.com/SMI/2020/WindowsSettings">SegmentHeap</heapType>`:
  no UAC element, no compatibility section. `--heap-type default` (the default) links no manifest, as every server
  build before it: the pin layouts' `server.exe` has no manifest (no `<assembly` in `x64-c29442e8-p58724e0/bin/server.exe`) `[C]`.
- The setting is per process: it changes the process heap, so every `malloc`/`new` of `server.exe` and of the DLL (both
  `/MD`, ucrtbase's heap is `GetProcessHeap()`) goes to the Segment Heap. Windows 10 2004 (19041) or later reads it;
  oldpc runs 19045, the workstation 26100 `[I: documented behaviour; L: the check below]`.
- Which heap a running process uses is read from outside with `perf/evidence/5037/heapkind.py` (read only: PEB
  ProcessHeap +0x30, the signature at heap+0x10: 0xFFEEFFEE NT heap, 0xDDEEDDEE Segment Heap). Positive controls on the
  workstation, 2026-11-24 15:58: Edge, RuntimeBroker, SearchHost, StartMenuExperienceHost, TextInputHost = SEGMENT;
  python.exe = NT `[L]`.

## 2. The two arms `[C 2026-11-24 16:05]`

`bash perf/evidence/5037/build.sh both` (= `server/build_server.py --src C:/c5-64bit-work/keep/pins-0.1b/src-x64-1b87dd185
--deps C:/c5-64bit-work/keep/pins-0.1b/deps-x64-release --config release --no-proxy --heap-type default|segment`, runbook
1.2; 91 and 102 s), both from repository commit 1a1acf4ac with no local change (build id `repo_dirty=0`), the DLL pin
1b87dd185 of `deps/dll.lock` (x64-0.1b: batch 5 + the danger package + hidden embarks).

| arm | server.exe SHA-256 | x64 DLL | heap |
|---|---|---|---|
| base | `851b2e8e628da4da...` (11,771,904 B, no manifest) | `b84aa3e9...` | NT heap (`heapkind.py`: NT) `[L]` |
| seg | `c13162a5bc1951df...` (11,772,416 B, `.rsrc` with the manifest) | `b84aa3e9...` (the same file) | Segment Heap (`heapkind.py`: SEGMENT) `[L]` |

`perf/evidence/5037/pesections.py`: `.text`, `.data`, `.pdata`, `.tls` and `.reloc` byte-equal; `.rdata` differs only by
the link's build identity (the PDB GUID that `/Brepro` derives from the inputs); seg adds `.rsrc` (424 B) and its
`.reloc` moves by one page `[C]`. So the code is the same and only the heap differs.

Trap (cost one rebuild, 10 min): the build id (`vp_build_info.h`: repository commit and dirty flag) is compiled into
server.exe, so two arms built before and after a commit differ in `.text` too. Build both arms from one clean commit.

## 3. Base: the heap's share of a turn at the current pins, and the Segment Heap's `[L, 2026-11-24 16:11-16:20 EST, workstation, shared: 19-22 other owners' heavy jobs registered at every check; attribution, never timing]`

`bash perf/evidence/5037/run_profile.sh M|H base|seg <name>`: 4668's driver (`perf/evidence/0437/4668/profile_s3.py`,
through `perf/evidence/5037/profile_arm.py` with this ticket's work folder) unchanged: 0438's saves, 0438's database with
statistics logging on, AI logging off, sampler `perf/sampler` CPU mode at a 10 ms tick, M turns 151-160, H 301-305;
`heapkind.py` read the running server's heap in each run. Heap share: `perf/evidence/5037/heapshare.py`
("heap self" = the leaf-side run of ntdll/ucrtbase/KERNELBASE frames holds a heap entry point; this counts the Segment
Heap's internals, whose frames carry other nearest-export names, e.g. `RtlInitializeSid`, so 4668's leaf-name rule
undercounts that arm). Evidence `perf/evidence/5037/profiles/` (reports, windows, turn times, loads, `heapshare.txt`,
`families.txt`); raw `folded.txt` on `F:/c5-64bit-work/5037/profiles/`.

| profile | walks | cycles, window | heap self (this rule) | heap self (4668's rule) |
|---|---|---|---|---|
| M base (NT heap) | 2,084 | 59.3 G | **8.82 %** | 8.60 % |
| M seg (Segment Heap) | 2,899 | 43.7 G | **3.58 %** | 2.30 % |
| H base (NT heap) | 17,363 | 374.5 G | **5.14 %** | 4.68 % |
| H seg (Segment Heap) | 24,161 | 423.2 G | **4.51 %** | 2.44 % |
| for comparison: 4668 m1 (batch 2, 2026-11-17) | 2,445 | 68.7 G | 6.36 % | 5.96 % |
| for comparison: 4668 h1 (batch 2, 2026-11-17) | 21,011 | 590.4 G | 4.94 % | 4.56 % |

- **Base at the current pins: the heap is 8.8 % of M's cycles and 5.1 % of H's** (one sampled run each, loaded
  machine). 4668's 6.0-7.3 % (M) and 4.6 % (H) on batch 2 agree in size (VX5's row said 4.4-7.3 %).
- **The M base run's extra heap time sits in four callers**: frees in `CvUnit::ComputePath` 1.68 %,
  `CvHomelandAI::GetBestExploreTarget` 1.59 % and the server's `SrvArgs::Add` 1.44 % (`free_base` -> `RtlFreeHeap`
  leaves) and an allocation in a `std::set<BuilderDirective>` insert 1.31 % (`operator new` -> `RtlAllocateHeap`);
  none of them is above 0.5 % in the seg run or in 4668's m1.
  One run each: whether these are a steady NT-heap cost (the back end's free lists and decommit for blocks above the
  LFH's sizes) or a sampling cluster is not decided by these runs `[I]`.
- **With the Segment Heap the heap self share drops on M (8.8 -> 3.6 %) and a little on H (5.1 -> 4.5 %)**; the H
  callers (path-node vectors, `GetMultiplePaths`, the danger package's `std::map` inserts) stay, at about the same
  shares. The cycle totals cannot be compared (the load differed run to run: M seg 26 % fewer cycles, H seg 13 % more);
  the turn-level effect is the oldpc series' (section 4).

## 3a. Identity (D164): the Segment Heap plays the same games `[L, 2026-11-24 16:19:33-16:58 EST, workstation, headless]`

`python -I perf/quietlock/quietlock.py run --who impl-5037 --what ... -- python -I -X utf8 perf/evidence/5037/idjob_bins.py
--cand-bin C:/c5-64bit-work/5037/b-seg/bin --dll C:/c5-64bit-work/5037/b-base/bin/CvGameCore_Expansion2.dll --layout-bin
C:/c5-64bit-work/5037/b-base/bin --work F:/c5-64bit-work/5037/id-1` (the speed kit's `idjob.py` with the candidate's
own layout: reference server `851b2e8e`, candidate server `c13162a5`, the same DLL `b84aa3e9`). Evidence
`perf/evidence/5037/identity/`.

| window | result |
|---|---|
| M (0438's Standard save, 150..160) | IDENTICAL, 15 tables, 32,815 rows |
| H (0438's Huge save, 300..305) | IDENTICAL, 14 tables, 125,209 rows |
| E (new Standard fixed-seed game, 0..50) | IDENTICAL, 15 tables, 20,619 rows |
| D24 (the save test against `deps/i0.lock`'s i0ship-f33fc46a, the candidate's bin) | PASS: tiny, standard, huge parts 1-3 pass |

Expected: the heap changes where blocks lie, not what the DLL computes; a difference would have meant that some code
orders by pointer value (the Architect's open point for pools, turn-time-study-analysis.md 9.8 row 7). None of the
windows shows one `[L]`. The oldpc series also compared round 1 of each arm with statscmp: same (4.1, 4.2).

## 4. Grade A series on oldpc: seconds per turn, peak working set, memory after 10 turns

Both series: `python -I -X utf8 perf/evidence/5037/pair_bins.py --a base=C:/c5-64bit-work/5037/b-base/bin/CvGameCore_Expansion2.dll
--b seg=C:/c5-64bit-work/5037/b-seg/bin/CvGameCore_Expansion2.dll --scenario M|H --runs N --turns 15 --issue 5037`
(= the speed kit's `pair.py`, each arm staged from its own bin; the job is `perf/speedkit/tbpair.py` unchanged:
`perf/tbjobs/series.py`, interleaved rounds with the order rotated, one process at a time, 0438's save and
statistics-on database, AI logging off, the server's private bytes per turn). oldpc = Windows 10 19045, NVIDIA RTX
2080 driver 32.0.16.1088 (D175). Statistics: `perf/tbjobs/analyze.py <out> --ref base` and
`perf/evidence/5037/runs_table.py <out>` (per run, Welch interval, median, exact Mann-Whitney, paired by round).

### 4.1 M, turns 151-165, 12 runs per arm `[L, 2026-11-24 16:09:25-16:17:00 EST, oldpc, job 20261124-160818-8efd, quiet: alone, undisturbed, foreign cores 0.051]`

Evidence `perf/evidence/5037/oldpc-M-8efd/` (`analyze.txt`, `runs_table.txt`, `series.json`, quiet proof).

| arm | s/turn mean (sd) | median | min - max | peak working set | peak private | private after 10 timed turns (D151) | growth |
|---|---|---|---|---|---|---|---|
| base (NT heap) | 1.0051 (0.0758) | 0.9780 | 0.9661 - 1.2325 | 158.3 MB | 220.2 MB | 216.6 MB | 0.71 MB/turn |
| seg (Segment Heap) | 1.0155 (0.0152) | 1.0099 | 1.0016 - 1.0465 | 155.7 MB | 215.8 MB | 211.1 MB | 0.67 MB/turn |

- **The Segment Heap is slower on M: median +3.26 %**, Mann-Whitney exact two-sided p = 0.0045; paired by round seg was
  slower in 10 of 12 rounds, and in all 9 of rounds 4-12 (+1.7 to +3.8 %, median +3.5 %). The mean difference (+1.04 %,
  Welch 95 % CI [-3.8, +5.9] %) is not significant only because of one slow base run (round 3, 1.2325 s, machine CPU
  47 % in that run against 14-15 % in rounds 4-12; rounds 1-3 ran at 15-47 % CPU, Defender's MsMpEng the largest
  foreign process with 13 s). Read the median and the paired rounds: about **+3.3 % per M turn**.
- Memory: the Segment Heap saves little here: private after 10 turns -5.5 MB (-2.6 %), peak private -4.5 MB, peak
  working set -2.6 MB (-1.6 %).
- Same game: statscmp of round 1, every table same (`compare.txt`).

### 4.2 H, turns 301-315, 6 runs per arm `[L, 2026-11-24 16:17:04-17:04:04 EST, oldpc, job 20261124-160951-97a0, quiet: alone, undisturbed, foreign cores 0.038, machine CPU mean 14.7 %]`

Evidence `perf/evidence/5037/oldpc-H-97a0/`.

| arm | s/turn mean (sd) | median | min - max | peak working set | peak private | private after 10 timed turns (D151) | growth |
|---|---|---|---|---|---|---|---|
| base (NT heap) | 14.019 (0.053) | 14.012 | 13.969 - 14.119 | 268.5 MB | 360.1 MB | 332.3 MB | 1.92 MB/turn |
| seg (Segment Heap) | 14.399 (0.169) | 14.309 | 14.269 - 14.647 | 264.7 MB | 350.7 MB | 301.3 MB | 0.79 MB/turn |

- **The Segment Heap is slower on H: +0.380 s per turn (+2.71 %), Welch 95 % CI [+0.203, +0.557] s**, significant;
  every one of the 6 rounds slower (+1.8 to +4.5 %, paired mean +2.71 %, 95 % CI [+1.64, +3.78] %); Mann-Whitney exact
  p = 0.0022 (no overlap: the slowest base run 14.119 s is faster than the fastest seg run 14.269 s).
- Memory: **private after 10 turns -31.0 MB (-9.3 %)**, growth over the window 0.79 against 1.92 MB/turn, peak
  private -9.3 MB (-2.6 %), peak working set -3.8 MB (-1.4 %). The Segment Heap gives freed memory back sooner.
- Same game: statscmp of round 1, 14 tables, 362,705 rows, IDENTICAL.

### 4.3 The workstation, for Windows 11: an indication only `[L, 2026-11-24 16:57:53-17:03 EST, workstation (Windows 11 26100), shared, no quiet lock: NOT a result (COMMON 23)]`

`python -I -X utf8 perf/evidence/5037/ws_indication.py --scenario M --rounds 6 --out C:/c5-64bit-work/5037/ws-M` (through
`quietlock.py run`): both arms alternated ABBA on M 151-165, `procstats.py` reading each run's page faults and CPU times
from outside. Evidence `perf/evidence/5037/ws-M/`.

| arm | s/turn median (range), 6 runs | page faults per run, median | kernel s per run | user s per run | peak private |
|---|---|---|---|---|---|
| base | 1.547 (1.355 - 1.855) | 105,437 | 2.17 | 25.06 | 226.3 MB |
| seg | 1.512 (1.390 - 2.168) | 116,310 (+10 %) | 2.26 | 24.78 | 226.5 MB |

The spread (37 and 56 % between the fastest and slowest run of one arm) hides a 3 % effect: the workstation says nothing about Windows 11's sign. What
it does show, as counts that load does not change much: the Segment Heap takes about 10 % more page faults per M run
(the identity job's E window: 52,474 against 43,652, +20 %) and spends about the same CPU time in the kernel.

## 5. Go or no-go `[L for the numbers; I for the reading]`

**No-go for the Segment Heap.** On oldpc (Windows 10 19045), grade A, it makes the AI turn slower, not faster:
**+2.7 % on H** (Welch 95 % CI [+1.4, +4.0] %, 6 of 6 rounds) and **about +3.3 % on M** (median; Mann-Whitney p 0.0045;
9 of 9 clean rounds). The value floor is a gain of 0.3 %; this is a loss of ten times that. The game is the same
(identity M, H, E and D24, and both series' statscmp). Its one gain is memory: -31 MB private after 10 H turns (-9 %),
-5.5 MB on M, and a flatter growth; the peak working set moves by 1-2 % only. A memory saving of that size does not pay
for 3 % of every turn on the player's main wait, so `--heap-type segment` stays off; the build switch stays for later
trials (default `default`, nothing changes for any build).

Why the sampled heap share and the turn time disagree `[I]`: the profiles (section 3) charge the Segment Heap fewer
cycles inside the heap's own frames (M 8.8 -> 3.6 %), yet the turn is slower. The cost has moved out of those frames:
the Segment Heap returns freed pages to the system sooner (lower private bytes, flatter growth: section 4.2) and takes
10-20 % more page faults per run (4.3); a demand-zero fault on reuse is charged to the code that touches the new memory
(the DLL's own frames, a `memset`, a constructor), not to `RtlAllocateHeap`. That fault count alone (about 11,000 more
per M run, a few microseconds each: tens of milliseconds) explains only a small part of the 0.5 s per M run, so placement (cache and TLB locality of the
Segment Heap's blocks against the NT heap's LFH) is the other candidate. Neither is measured on oldpc: the
page-fault count job for oldpc (`perf/evidence/5037/tbfaults.py`, `submit_faults.sh`) was written and then cancelled
(it waited about 2 h behind other teams' quiet jobs).

**Trial two (mimalloc)** was to run only after a go, on the MC's word; there is no go, so it was not started. What this
trial says for it: the heap is still 5-9 % of a turn's cycles at the current pins (section 3), so an allocator that is
faster than the NT heap's LFH could still pay; the Segment Heap is not that allocator on Windows 10. mimalloc is a
separate decision (the MC's): it would replace `malloc`/`new` in the DLL and the server by linking or overriding, not by
a manifest, and needs its own identity runs (pointer-ordered code, section 3a) and its own series.

## 6. Not verified

- The sign on Windows 11 (the workstation's spread is too wide; a grade A pair needs a quiet Windows 11 machine or an
  MC window on the workstation). Players run both; Windows 10 19045 is what oldpc measures.
- The mechanism of the slowdown on oldpc (page faults against placement): no oldpc profile or fault count.
- Each sampled profile is one run on a loaded workstation; the M base run's four large heap callers (section 3) were not
  seen again.
- The product path (the server under the proxy with the renderer on): only headless direct mode was measured. The
  render thread allocates too; nothing here says how the Segment Heap treats it.

## 7. Files

`server/build_server.py` (`--heap-type`), `perf/evidence/5037/`: `build.sh` (both arms), `pair_bins.py` (speed kit
pair, a layout per arm), `idjob_bins.py` (identity job, a layout per arm), `heapkind.py` (NT or Segment heap of a running
process), `pesections.py` (PE sections compared), `run_profile.sh` + `profile_arm.py` (4668's sampled run), `heapshare.py`
(heap self share for either heap), `runs_table.py` (per run, Welch, median, Mann-Whitney, paired rounds),
`procstats.py` (page faults and CPU times of every server run), `ws_indication.py`, `tbfaults.py` + `submit_faults.sh`
(the oldpc count job, not run), `collect_profiles.sh`, `collect_series.sh`; evidence folders `profiles/`, `identity/`,
`oldpc-M-8efd/`, `oldpc-H-97a0/`, `ws-M/`. Raw data on `F:/c5-64bit-work/5037/` (fetched jobs, profiles, identity runs).
