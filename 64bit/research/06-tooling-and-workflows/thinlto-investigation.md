# ThinLTO for the x64 DLL's measured and shipped builds: investigation for the owner's go or no-go (ticket 0160)

Ticket 0160, decision D107 (the owner's order on linker memory, 2026-11-13), section 4C of
`../07-architecture-decisions/architect-2026-11-13-linker-memory.md`. Written by impl-B-0160 (account B),
2026-11-13 22:37 to 2026-11-14 03:00 EST. Marks: `[C]` confirmed in a file or by a tool's own output, `[L]` measured
live, `[I]` inferred. Nothing here changed a pin, a measured build or a shipped build: until the owner's go they stay on
today's release build (full LTO).

## 0. The result for the owner

**The owner's answer, 2026-11-14 about 03:10 EST (decision D121): no-go** [reworded for privacy] (the owner confirmed the no-go). Full LTO stays.

| Question | Full LTO (today's release) | ThinLTO, job limit 2 / 4 / 8 | Section |
|---|---|---|---|
| Peak private memory of the link | **13.0-13.1 GiB** (here 13.13; 0159 13,340 MiB) | **5.3 / 5.7-5.8 / 6.3 GiB** cold; **0.5-0.7 GiB** warm re-link | 3 |
| Link time (workstation, indication) | 471 s (here and 0159, one thread) | 409 / 155-215 / 178-195 s cold; **2-4 s** warm re-link with nothing changed; 125-275 s after a change in `CvCity.cpp` (98 of 174 modules rebuilt) | 3 |
| Seconds per turn (quiet laptop, huge 350-355, 8 rounds) | 6.980 | **7.052: +1.0 %** (95 % CI +0.003..+0.140 s; slower in 8 of 8 rounds) | 6 |
| Same game results | - | **d10 PASS**, all 12 x64 `stats.db` identical to the release pin's run; testbed statscmp identical | 5 |
| Repeatable (issue 0115) | yes (0159 and here: a rebuild gives the pin's bytes) | yes for a fixed job count and a relative (or no) cache: cold, warm and a second build in another folder byte-equal; code identical at every job count, but the PDB records the job limit and the cache path, so the DLL's 20 PDB-identity bytes change with them | 4 |
| DLL / PDB size | 23,616,000 B / 141,705,216 B | 24,025,600 B (+1.7 %) / 146,669,568 B (+3.5 %) | 7 |
| What a go costs | - | about 4-5 agent hours and 3-4 machine hours once (new pin layout, save test, baselines again) | 8 |

**The question:** should the x64 DLL's measured and shipped builds move from full LTO to ThinLTO with a job limit
(the link needs 6 GiB instead of 13 GiB, and a re-link with a warm cache takes seconds), at the cost of about 1 % more
time per AI turn in the shipped game?

**Recommendation: no-go.** The memory problem that started D107 is already solved without touching the shipped
binary: 0159's `fast` configuration took every functional build off whole-program optimisation (0.5 GB, 2 s links),
one whole-program link runs at a time (`perf/quietlock/linkplace.py`), and the workstation gets 64 GB on 2026-11-14.
What remains are the few release links per day for pins, timing and shipping; for them 13 GiB once at a time fits. A
measured 1 % slower turn in every player's game is a permanent cost of a product whose purpose is speed, and a go
would also reset every timing baseline. Revisit if release links become frequent (for example per-commit timing
builds) or if the 1 % can be won back (ThinLTO's import limits, a profile), which was not tried here.

## 1. The switches (LLVM 19.1.7's own help text) [C]

Read 2026-11-13 22:29 EST with `clang-cl /?` and `lld-link -help` from `C:/Program Files/LLVM/bin` (clang 19.1.7,
LLD 19.1.7):

| Purpose | Switch | Help text, quoted |
|---|---|---|
| ThinLTO at compile | `-flto=thin` | "-flto=<value>  Set LTO mode"; plain `-flto` is "Enable LTO in 'full' mode" (today's release) |
| Linker's ThinLTO job limit | `/opt:lldltojobs=N` | only "/opt:<value>  Control optimizations" is listed; the value `lldltojobs=` is in lld-link.exe's own strings and the linker answers `/opt:lldltojobs: invalid job count: zz` for a bad count and `/opt: unknown option: lldltojobsx=2` for a bad name [L, toy DLL] |
| ThinLTO cache | `/lldltocache:<dir>` | "Path to ThinLTO cached object file directory" (pruning: `/lldltocachepolicy:<value>` "Pruning policy for the ThinLTO cache") |
| Linker threads in general | `/threads:N` | "Number of threads. '1' disables multi-threading. By default all available hardware threads are used" |
| Line tables only (option 4) | `-gline-tables-only` | "Emit debug line number tables only" |

A toy DLL (two C files, 2026-11-13 22:31) linked with `/Brepro` at `lldltojobs` 1, 2 and 8, with a cold cache, a
warm cache and none, gave four byte-equal DLLs [L]; the toy has no PDB, which is where the real DLL's differences come
from (section 4).

## 2. Method

- Source: `git archive` of the fork at the pin 04b1211b8 into `C:/c5-64bit-work/0160/src`; its fingerprint equals
  `deps/dll.lock`'s (`3f171f20...`, 473 files) [C]. `SOURCE_DATE_EPOCH` = 1794587302, the commit time of the pin's tool
  commit a803776cf (`deps/dll.lock` `epoch`), so that `__DATE__`/`__TIME__` equal the pinned build's.
- Tool: `perf/thinlto/thinlto_arm.py` (this ticket). It imports `tools/x64-dll/build.py` unchanged and uses its compile
  and link command lines (`cl_args`, `compile_one`, `link`); the arms differ from today's release build only in
  `-flto` -> `-flto=thin` at compile and `/opt:lldltojobs=N`, `/lldltocache:<dir>` at link. Link inputs (stubs and the
  two libraries) come from `perf/thinlto/libs.sh`, build_all.py's library steps alone. Drivers: `run_links.sh`,
  `rest_links.sh`, `repro_links.sh`; layout `layout.py`; the table `summarize.py`.
- Memory: the tool puts itself into a job object before the link and reads the job's `PeakProcessMemoryUsed` after
  it (peak private bytes, i.e. commit, of the largest process in the job: the linker), with the job's CPU time.
  0159's numbers come from `PeakPagefileUsage` of the linker's handle: the same quantity.
- One link at a time: before each link the tool waits until no other `lld-link.exe` or `link.exe` runs and free commit
  is at least the 8 GB floor plus the expected peak, and from 01:39 it also takes the place of
  `perf/quietlock/linkplace.py` (0159, merged at that time).
- Everything on C: (the SSD; decision D111). The first attempt on D: is in section 9.
- Reference: the pinned release layout `<pins_dir>/dlls/x64-04b1211b-pa803776` (DLL `74588135...`).

## 3. Links: memory, time [L]

2026-11-14, the workstation shared with other teams' links, games and servers (times are indications, COMMON 23;
memory peaks are a property of the link). CPU = the linker's user + kernel seconds. Every record:
`C:/c5-64bit-work/0160/<folder>/arms/<label>/link.json` (deleted at hand-back; the numbers are here). "Map" = the
first 8 hex digits of the `.map` file's SHA-256 (equal maps = equal code and data layout).

| Arm | Jobs | Cache before -> after (entries) | Started | Wall s | CPU s | Peak private GiB | DLL SHA-256 | Map |
|---|---|---|---|---|---|---|---|---|
| full LTO (0159, `x64-dll-build-configurations.md` 2) | 1 thread | - | 2026-11-13 23:55 | 470.7 | - | **13.03** (13,340 MiB) | `74588135` = the pin | - |
| full LTO (here) | 1 thread | - | 02:36:48 | 470.7 | 455.4 | **13.13** | `74588135` = the pin | `6D1BCA9D` |
| ThinLTO cold | 2 | 0 -> 174 (`cache-j2`) | 00:23:47 | 409.4 | 741.1 | **5.27** | `59108646` | `EA852500` |
| ThinLTO cold | 4 | 0 -> 174 (`cache-j4`, 253 MB) | 00:10:37 | 207.0 | 736.6 | **5.74** | `4E624E1F` | `EA852500` |
| ThinLTO cold | 8 | 0 -> 174 (`cache-j8`) | 00:31:01 | 194.8 | 822.2 | **6.25** | `B9E973F3` | `EA852500` |
| ThinLTO, no cache | 4 | - | 01:39:32 | 157.2 | 611.1 | 5.97 | `DC4D724F` | `EA852500` |
| ThinLTO warm, nothing changed | 4 | 174 -> 174 | 01:39:09 | **4.2** | 13.2 | **0.67** | `4E624E1F` = cold | `EA852500` |
| ThinLTO after a one-file change | 2 | 174 -> 272 | 01:47:21 | 274.7 | 565.2 | 5.08 | `90769591` | `83D02C4A` |
| ThinLTO after a one-file change | 4 | 174 -> 272 | 01:54:17 | 215.2 | 740.6 | 5.65 | `FE99C34A` | `83D02C4A` |
| ThinLTO after a one-file change | 8 | 174 -> 272 | 01:58:07 | 125.5 | 615.7 | 6.21 | `A39080D3` | `83D02C4A` |
| ThinLTO cold, relative cache | 2 | 0 -> 174 (`ltocache`) | 02:08:58 | 340.4 | 701.8 | 5.31 | `4C6DF6EE` | `EA852500` |
| ThinLTO cold, relative cache | 4 | 0 -> 174 | 02:28:42 | 154.7 | 599.0 | 5.77 | `B6722B93` | `EA852500` |
| ThinLTO cold, relative cache | 8 | 0 -> 174 | 02:18:20 | 178.2 | 814.8 | 6.32 | `95F21344` | `EA852500` |
| ThinLTO warm, relative cache | 4 | 174 -> 174 | 02:31:22 | **2.3** | 8.7 | **0.49** | `B6722B93` = cold | `EA852500` |
| ThinLTO, second compile in a second folder, relative cache | 4 | 0 -> 174 | 02:33:35 | 179.5 | 674.8 | 5.74 | `B6722B93` = first | `EA852500` |
| full LTO, line tables only (option 4, information) | 1 thread | - | 02:46:35 | 289.2 | 291.7 | **7.61** | `337135EA` (code differs) | `2EC6AAF2` |

Findings:

- **The full-LTO link here rebuilt the pin's DLL byte for byte** (`74588135...`, 13.13 GiB, 470.7 s, 455 CPU s on one
  thread) from this ticket's own source copy and freshly built stubs and libraries (`perf/thinlto/libs.sh`): the
  `repeat` note of `deps/dll.lock` (two feece282c builds differing, suspected stub libraries) does not hold for this
  pin's inputs [C, SHA-256].
- **ThinLTO needs 5.1-6.3 GiB at its peak, under half of full LTO's 13.0-13.1 GiB**; more jobs cost a little more memory
  (about +0.25 GiB per two jobs) and save wall time up to 4 jobs (409 s at 2, 155-215 s at 4, 178-195 s at 8 on a
  loaded machine). CPU seconds are about the same at every job count (600-820 s): the work is the same, spread wider.
- **A warm re-link with nothing changed takes 2-4 s and 0.5-0.7 GiB** and gives the same bytes as the cold link.
- **A one-file change does not make the re-link cheap when the file is central**: one function appended to
  `CvCity.cpp` invalidated 98 of 174 cached modules (the cache key covers the modules a module imports from), so the
  re-link took 125-275 s and 5.1-6.2 GiB, about a cold link. A leaf file would invalidate fewer [I, not measured].
  The change was taken back and the restored object compared byte-equal to the original [C].
- **Option 4 (information, not proposed): full LTO with `-gline-tables-only` instead of `/Zi`** links in 289 s at
  7.61 GiB (-42 % against full LTO) with a 57.8 MB PDB (-59 %), but its code is not the release build's: the DLL has
  the same size and other bytes, and `.text` is 16 bytes shorter (`0x1287FA6` against `0x1287FB6`) [C, map]. It would
  need the same identity and speed checks as ThinLTO before any use, and it loses local-variable debug information.
- The ThinLTO cache is 174 entries, 253 MB per cache folder; nothing prunes it unless `/lldltocachepolicy` is set.
- Compile: `-flto=thin` compiles as fast as `-flto` (173 TUs at 6 jobs: 122-150 s against 199 s for full on the same
  evening; one run each, a loaded machine) and the bitcode objects are the same size (233.5 MB in all) [L].

## 4. Repeatability (issue 0115's property) [C, `cmp` and SHA-256]

- **Fixed job count and relative cache: repeatable.** At 4 jobs with the cache given relative to the output folder
  (`/lldltocache:ltocache`), the cold link, the warm re-link and a second compile and link in a second folder give the
  same DLL `B6722B93...` and the same PDB `39BCBE79...` byte for byte. Without a cache the build is repeatable by the
  same reasoning [I]; with an absolute cache path, the cold and warm links into the same folder are equal
  (`4E624E1F`).
- **The code is the same at every job count and every cache folder**: every cold, warm and no-cache link of the
  unchanged objects has the same `.map` (`EA852500...`), and their DLLs differ in exactly 20 bytes: the PE
  `TimeDateStamp` (file offset 0x80), the debug directory's time stamps and the PDB GUID, which `/Brepro` derives from
  a hash of the PDB.
- **What moves the PDB**: (a) the linker's own command line, recorded in the PDB (`... /opt:lldltojobs=2
  /lldltocache:ltocache ...` against `=8`: 13 bytes); (b) with a cache, each ThinLTO module's name in the PDB is its
  cache file's path (`C:\c5-64bit-work\0160\cache-j4\llvmcache-0373258B...`; 187 bytes between two cache folders);
  given relatively, the name no longer depends on the folder. So a repeatable ThinLTO release fixes the job limit in
  `build.py` and gives the cache a relative path (or none). Two builds at different job limits are never byte-equal,
  only code-equal.
- The objects: 173 ThinLTO objects compiled from two source copies into two folders were byte-equal (on D:,
  2026-11-13 22:47) [L].

## 5. Same game results: d10 save identity [L]

`run_d10.py` on the ThinLTO layout (`C:/c5-64bit-work/0160/layout-thin`: the pinned layout with only
`CvGameCore_Expansion2.dll`/`.pdb` replaced by the 4-job link `4E624E1F...`), one-off 9045's command
(`--order-exception none --d18-x86 yes --d18-x64 yes`, AI logging off in both arms, `--jobs 6`), 2026-11-14
00:38:17-01:08:30 EST (1,813 s): **PASS**, every part of tiny, standard and huge, only the version-string exception (the
same string as the pin's, `Release-5.4.8-51-g04b1211b8 Copy`). Report: `perf/thinlto/evidence/d10-thin-report.txt`.
The rows of the identical pairs equal the release pin's run (9045) row for row: x86 direct = x64 direct 30,666 /
351,496 / 683,123; x64 continues x86's early save 27,008 / 327,041 / 357,454; x86 continues x64's early save 26,644 /
341,206 / 356,506.

Directly against the release pin's run (`perf/thinlto/cmp_release.py`, 2026-11-14 01:10-01:41, evidence
`perf/thinlto/evidence/cmp-release-summary.txt`): **all 12 x64 `stats.db` identical** (direct, early and both
continuations of tiny, standard, huge). The 16 x64-written saves of the two runs differ, as saves of two independent
plays do even A/A (`headless-save-comparison.md` section 2: game GUID, times; a resave also stores the absolute path
it loaded from); D24's writer identity is what `run_d10.py` checks inside each run. Its A/A self-test on 9045's
folder: 28 of 28 equal [L, 2026-11-13 22:50-23:06].

## 6. Speed: seconds per turn on the quiet testbed [L]

```
Metric / tool:   seconds per AI turn, perf/tbjobs/series.py + analyze.py (performance team's tools, PERFORMANCE.md)
Machine:         laptop (quiet testbed; oldpc was busy with 3507's job, MC #421/#429 moved the series), runner job
                 20261114-013219-52fd, quiet proof: alone, undisturbed, foreign 0.028 cores (Defender 12 s of 20.9)
Builds:          rel  = <pins_dir>/dlls/x64-04b1211b-pa803776/bin (the pin, full LTO), DLL 74588135..., server.exe B7A4D8CC...
                 thin = the same layout with the ThinLTO DLL 4E624E1F... (4-job link); same server.exe and libraries
Game / config:   huge save TB-HUGE_0350, 350 -> 355, AI logging off (D49), the pins' statistics settings (D35)
Series:          8 interleaved rounds per arm, order rotated, one process at a time, 2026-11-14 01:32:36-01:44:48 EST
Result:          rel  6.980 s/turn (sd 0.047, cv 0.67 %, median 6.967, 6.923-7.079, 95 % CI 6.941-7.020)
                 thin 7.052 s/turn (sd 0.073, cv 1.04 %, median 7.022, 7.010-7.227, 95 % CI 6.991-7.113)
                 thin - rel = +0.071 s/turn (+1.02 %), 95 % CI +0.003..+0.140; detectable ~0.086 s
Paired by round: thin slower in 8 of 8 rounds: +0.049, +0.043, +0.102, +0.069, +0.051, +0.148, +0.055, +0.054 s
                 (median +0.055 s, +0.8 %; sign test p = 2/256 = 0.008); slower on every turn 350-354 (+0.4 to +2.2 %)
Machine load:    machine CPU 13.5-13.8 % mean (one busy thread), free commit 9.1-9.2 GB throughout
Correctness:     statscmp of round 1, rel against thin: 14 tables, 62,453 rows IDENTICAL
Trust grade:     A for this comparison inside this set (8 interleaved per arm, quiet proof); laptop numbers are not
                 comparable with oldpc's baselines (another machine)
```

Evidence: `perf/thinlto/evidence/laptop-20261114-013219-52fd/` (`out/series.json`, `turnbench.json`, `analyze.txt`,
the statscmp, the quiet proof).

Why it moved [I]: full LTO optimises the whole program as one module (inlining and constant propagation across every
file); ThinLTO imports only the functions its summaries rate worth inlining, per module, so some of today's cross-file
inlining is lost. The ThinLTO DLL is 409,600 B (1.7 %) larger, which fits more out-of-line copies. Not profiled (a
sampled profile of both arms would name the functions; not needed for the decision).

## 7. Size [C]

DLL: full LTO 23,616,000 B (the pin), ThinLTO 24,025,600 B (+409,600, +1.7 %). PDB: 141,705,216 B against
146,669,568 B (+3.5 %; 146,653,184 B without a cache). Line tables only (option 4): DLL 23,616,000 B (the same size as full LTO, other bytes), PDB 57,765,888 B (-59 %).

## 8. What a go costs [C rows from `new-vp-release-checklist.md` and tracker 9045; I for the sum]

A go changes the bytes of the x64 DLL (not of `server.exe`, the proxy or the libraries), so the DLL is a new build of
the same pin and everything keyed to the DLL's hash is redone once:

| Step | Work | Cost (source) |
|---|---|---|
| `tools/x64-dll/build.py` | `-flto=thin` at compile, a fixed `/opt:lldltojobs=N` and a relative cache (or none) at link for the release configuration; the place's memory figure; tests; README | one small ticket [I] |
| Pin layout rebuilt | `server/build_server.py ... --config release` at the pin, new `deps/dll.lock` `release` hash and `verified` line | 572 s build (9045) [L], 0.5 h agent (checklist step 6) |
| Offline proxy checks | test_proxy_rt, hutest, verify leg on the new layout | ~5 min, 0.25 h (step 12) |
| Save test (D24) | `run_d10.py` on the new layout | 18-30 min, 0.1 h (step 13; 30 min here) |
| Validation games x64 = x86 | the testbed whole-game re-validation, if the MC wants it for a pure build change | ~2.6 h testbed, 0.5 h (step 14) |
| Instances and testbed | provision the new DLL into the instances and the testbeds' `base` | 1-5 min per instance (step 17) |
| Timing baselines | the performance team's baselines on the new DLL (`../08-performance/turn-time-baselines.md` section 11) | 2.2 h agent, ~28 min quiet testbed (step 19) |
| Tool defaults, COMMON 18 | the pin line, the configuration wording | 0.5-1 h (step 18) |

About 4-5 agent hours and 3-4 hours of machine time, most of it unattended; cheapest when folded into the next DLL pin
batch, which redoes every row anyway [I].

## 9. First attempt on D: (stopped) [L]

2026-11-13 23:20:28-23:51:31 EST, the 4-job cold link with inputs, outputs and cache on `D:/c5-64bit-work/0160`:
stopped by me at 31 min (the owner's order of 23:55 via the MC to move disk work off D:; the link had stalled). Peak private
memory 7.14 GiB (7,664,132,096 B), 724 CPU s (507 user, 216 kernel); all 174 cache entries written, the import
library written at 23:41, no DLL yet. The linker's threads sat idle for tens of seconds at a time, the last 10 minutes
on one thread at 0 CPU with its working set trimmed to 1.4 of 3.8 GB private; the machine had 4.0-4.3 GB of physical
memory available (87 % used, page file 28 of 46 GB in use), Defender used up to 0.85 of a core, and D: was saturated
(`d-drive-load.md`). The same link on C: an hour later: 207 s, 5.74 GiB. The higher peak on D: is not explained [I:
the trimmed working set and longer life of mapped inputs may count differently].

## 10. Other whole-program builds in the project [C]

`grep` over the repository's build scripts (2026-11-13): only `tools/x64-dll/build.py` uses `-flto` and `/LTCG`. The
fork's own `build_vp_clang.py` (the 32-bit DLL, i0-ship) also compiles its release with `-flto` (line 310) and links
with `/LTCG` (line 323); lld-link is a 64-bit process, so those links can be as heavy; they take no link place today
and are outside this ticket.

## 11. Not verified

- Speed on `oldpc` (the performance team's reference machine): the series ran on the laptop; a comparison holds only
  inside one machine's set.
- Whether ThinLTO's 1 % can be won back (import instruction limits, `-fwhole-program-vtables`, PGO): not tried.
- A one-file change in a leaf file (fewer invalidated modules); the cache size limit and pruning policy.
- Link times on a quiet workstation: every time here was taken beside other teams' work.
- The 7.14 GiB peak of the stalled D: link against 5.74 GiB on C: (section 9).
