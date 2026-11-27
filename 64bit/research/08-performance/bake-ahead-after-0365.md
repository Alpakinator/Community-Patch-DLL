# The load's bake-ahead after 0365: where the +0.5 s came from (ticket 0402)

Ticket 0402, 2026-11-16 04:07 EST on, branch `task-0402-bake-ahead-0p5s` (from main 17674f2c). The question: 0365's two
oldpc sets (research/05-graphics-composition/terrain-decals.md 19.4) showed the load's bake-ahead on huge about 0.5 s
(3 %) longer in all four after arms, with the same bakes and the same cost per bake. **Answer (section 5): not 0365's
code or builds; the bake-ahead's run level on oldpc moves by up to about 0.4 s and holds for tens of minutes, and it fell
on both after arms of each set (set 1 also had about 0.3 s of real extra bakes from `staleFirst`, now off).** Scripts and
evidence in `shell/evidence/0402/`: `window_0402.py`, `persec_0402.py`, `steady_0402.py`, `lockstep_0402.py` (0365's
logs), `loopprof.patch`, `build_0402.sh`, `build_plain_0402.sh` (the two jobs' builds), `analyze_0402.py` with
`results_probe.json` and `results_plain.json` (the two jobs' statistics), `machine_cpu_0402.py`, `pe_sections_0402.py`,
`placement_0402.py` (code placement). Fetched jobs: `F:/c5-64bit-work/0402/oldpc-8379`, `oldpc-5e04` (render.log and
lockstep CSV of every arm, quiet proofs). Marks: [C] read from the code or the binaries, [L] measured in a live run,
[I] inferred.

## 1. What the bake-ahead's length is made of [C]

The bake-ahead (residency `idle()`, `render/src/bakeresidency/residency.cpp`) runs on the render thread, once per pass
of the host loop (`server/src/render/srv_render_host.cpp` `Loop`): poll the taps, draw if a camera waits, call the source's
`idle()` with the budget (4.0 ms), then wait for `<base>.Cam` with a 2 ms timeout while bakes are pending. `idle()` bakes
while it has a token (rate 400/s, burst 16) and the budget is not spent (checked after each bake, so a slice of 1.9 ms
bakes holds 3), and ends early when a camera waits (a *yield*). At about 1.9 ms of CPU per bake the 400/s rate is not
reached (340-356 bakes/s): **the bake-ahead's speed is the loop's pass rate times the bakes per pass**, and anything that
lengthens a pass without baking (draw, wait, poll) lengthens the bake-ahead.

0365 changed nothing in that loop. Its code on the bake-ahead's path [C, diff 3710b71b..719e6fbc]: `idle()`'s pick has a
`staleFirst` branch (a bool test when off), the source's `idle()` tests `m_aheadHold` (false: no wait was taken in any arm,
no `waits up to` line in the eight render.logs), `decalTileBaked` keeps a second sorted list per pending generation (a
`lower_bound` per baked tile), and load generations 3 and 4 are applied by the delta (shorter). `srv_render_host.cpp` and
`srv_render_scene.cpp` are byte-identical in the two builds.

## 2. The two sets from their logs [L, 0365's jobs `20261115-231028-7cb3` and `20261116-004232-f799`, oldpc]

Fetched job folders `F:/c5-64bit-work/0365/oldpc-7cb3` and `oldpc-f799` (huge save copy `TB-HUGE_0350`, product path,
headed, quiet). Set 1's after build is `75cd7f30` (d0dbb17d, `staleFirst` **on**); set 2's after is `3ee01a0f`
(719e6fbc, `staleFirst` off); before is `41b51f57` (3710b71b) in both. Window: the `residency: field ... (new, load 3)`
line to `bake-ahead complete`; the cumulative counters of the 1 s lines interpolated to the two ends
(`window_0402.py`).

| Arm | ahead ms | bakes | idle slices | slices/s | bakes/slice | yields | loop passes | ms per pass |
|---|---|---|---|---|---|---|---|---|
| set 1 1-before | 15,003 | 5,010 | 1,830 | 121.9 | 2.69 | 211 | 1,843 | 8.14 |
| set 1 2-after | 15,472 | 5,113 | 1,871 | 120.9 | 2.72 | 170 | 1,878 | 8.24 |
| set 1 3-after | 15,450 | 5,122 | 1,910 | 123.6 | 2.67 | 187 | 1,917 | 8.06 |
| set 1 4-before | 15,031 | 5,078 | 1,853 | 123.3 | 2.70 | 199 | 1,880 | 8.00 |
| set 2 1-before | 15,266 | 5,118 | 1,874 | 122.7 | 2.71 | 191 | 1,905 | 8.01 |
| set 2 2-after | 15,609 | 5,113 | 1,845 | 118.2 | 2.74 | 151 | 1,855 | 8.41 |
| set 2 3-after | 15,886 | 5,129 | 1,848 | 116.3 | 2.73 | 165 | 1,868 | 8.50 |
| set 2 4-before | 15,102 | 5,126 | 1,874 | 124.1 | 2.70 | 198 | 1,892 | 7.98 |

(bakes = the `bake-ahead complete` line's count; the other columns over the window.) The steady part of the window
(from 5.5 s after the field, when the EXE's frame rate settles at 30 fps and the server draws about 58 frames/s, to 0.5 s
before the end; `steady_0402.py`), per pass: [L]

| Arm | ms/pass | bakes/pass | CPU per bake ms | bake CPU per pass ms | rest per pass ms | draws/pass | yields/pass | bakes/s |
|---|---|---|---|---|---|---|---|---|
| set 1 1-before | 7.653 | 2.725 | 1.802 | 4.912 | 2.741 | 0.442 | 0.125 | 356.1 |
| set 1 2-after | 7.950 | 2.767 | 1.824 | 5.048 | 2.902 | 0.460 | 0.105 | 348.0 |
| set 1 3-after | 7.828 | 2.729 | 1.835 | 5.009 | 2.819 | 0.449 | 0.118 | 348.6 |
| set 1 4-before | 7.721 | 2.725 | 1.814 | 4.942 | 2.779 | 0.445 | 0.125 | 352.9 |
| set 2 1-before | 7.765 | 2.758 | 1.804 | 4.975 | 2.790 | 0.452 | 0.119 | 355.1 |
| set 2 2-after | 8.310 | 2.845 | 1.815 | 5.164 | 3.146 | 0.483 | 0.081 | 342.3 |
| set 2 3-after | 8.277 | 2.795 | 1.845 | 5.156 | 3.120 | 0.476 | 0.096 | 337.7 |
| set 2 4-before | 7.695 | 2.739 | 1.804 | 4.940 | 2.755 | 0.445 | 0.126 | 355.9 |

("rest" = the pass minus the bakes' CPU: poll, draw, the pacing wait and the rest of the loop.)

What the logs settle [L]:
- **The two sets are not the same effect.** Set 1's after arms baked 103-112 tiles more than its before arms (5,113 /
  5,122 against 5,010 / 5,078: `staleFirst` on, each load generation's re-bakes no longer coalescing, as 0365 first
  wrote) **and** ran 2 % fewer bakes per second; at about 350 bakes/s the extra bakes are about 0.2-0.3 s of the +0.44 s.
  Set 2's after arms baked the same number of tiles (5,113 / 5,129 against 5,118 / 5,126) and the whole +0.56 s is a
  lower bake rate: 338-342 against 355-356 bakes/s in the steady part (-4 %).
- **Not 0365's bounded wait**: no arm took it (generation 1 came 7.0-8.9 s before the field).
- **Not the cost per bake**: the CPU per bake differs by +0.01 to +0.04 ms (+0.6 to +2.3 %), about 0.03-0.1 ms per pass.
- **The pass got longer outside the bakes**: the rest per pass is +0.36 ms in set 2 (3.12-3.15 against 2.76-2.79 ms) and
  about +0.1 ms in set 1. More bakes per pass in the after arms (2.80-2.85 against 2.73-2.76) follow from longer passes
  (more tokens accrue) and fewer yields; they do not make up for the fewer passes.
- **It appears only once the draw rate settles**: in seconds 1-5 after the field (the EXE at 14 fps, about 25 server
  draws/s) the passes take the same time in both builds (7.4-8.1 ms); from about 6 s on (30 fps, 58 draws/s) the after
  arms' passes are 8.1-8.5 ms against 7.5-8.0 (`persec_0402.py`).
- **The cameras waited less for the render thread in the after arms**: the composite's lockstep rows over the same
  steady part (`lockstep_0402.py`) show the server's wake latency (camera published to the render thread awake) at a mean
  of 0.31 / 0.71 ms against 0.88 / 0.99 ms in set 2 and more exact frames (234 / 224 of 267 / 274 against 196 / 189 of
  257 / 252); the render thread was more often in its pacing wait, not in a slice, when a camera came. The EXE's frame
  rate (34 ms frames), the server's CPU and GPU per frame and the composite's lockstep wait are the same in all arms.

So the logs place the time in the loop's non-bake part, mostly the pacing wait, but cannot say why its length changed
with a build whose loop code is identical. Section 3 measures that directly.

## 3. The measurement: the loop's time split (instrumented A/B)

(Method written 2026-11-16 04:30 EST, before the run.) Both builds of 0365's set 2 (before 3710b71b, after 719e6fbc)
rebuilt with one identical patch, `shell/evidence/0402/loopprof.patch` (MEASUREMENT BUILD ONLY, never merged), that adds
one `loopprof 1s:` line per second to render.log: the loop's passes, each pass split into poll (loop top to the frame
decision), draw (decide + `DrawFrame`), idle (the source's `idle()`), wait (the pacing `WaitForMultipleObjects`) and
other; the pacing wait by how it ended (timeout, `<base>.Cam`, already signalled), with 0.5 ms histograms; the render
thread's CPU time and cycles (`GetThreadTimes`, `QueryThreadCycleTime`), the process's CPU time and the timer resolution
in force (`NtQueryTimerResolution`). The two builds differ by 0365's code only. One quiet testbed job on oldpc, 0365's
product path and save (`TB-HUGE_0350`), 12 arms in the order before, after, after, before (x3), 1 AI turn each (the
bake-ahead is at the load), statistics per PERFORMANCE.md (grade A: 6 interleaved per arm).

Expected: if the pacing wait per pass is longer in the after arms with the same split of wait endings, the change is in
how the passes line up with the composite's cameras and the timer (a schedule effect, not 0365's code); if the slices
take longer at the same bakes, the bake path itself is slower (code placement); if `other` or `poll` grows, a per-pass
cost.

**Run** [L, job `20261116-043819-8379`, oldpc, 2026-11-16 05:04-05:50 EST, rc 0, every arm exit 0, 1 turn each]:
server.exe before `e5870489...` (3710b71b + patch), after `94557d8f...` (719e6fbc + patch); proxy `7463c869...` and
composite `1b75e5b5...` (both from 719e6fbc; the same files in both arms); x64 libraries of the pin layout
`x64-04b1211b-pfdee656` (DLL `29f58e17...`); build lines `shell/evidence/0402/build_0402.sh`. Quiet proof: alone, foreign
load 0.139 cores (limit 0.1: svchost 168 s, MsMpEng 91 s, audiodg 27 s over the job; 0365's sets had 0.103 and 0.113),
CPU mean 19.9 %, lowest available memory 6.2 GB. Statistics `shell/evidence/0402/analyze_0402.py`, per-arm values
and summaries `results_probe.json`. Grade **A** (6 interleaved per arm, quiet machine; foreign load slightly above the
limit, as in 0365's sets).

| Metric (bake-ahead / its steady part) | before (n 6): mean, sd | after (n 6): mean, sd | after - before, Welch 95 % | detectable |
|---|---|---|---|---|
| Bake-ahead after the field, ms | 15,362, 93 | 15,337, 180 | **-25 (-221 .. +171)** | 232 |
| Bakes in it | 5,115.7, 3.4 | 5,116.7, 4.3 | +1.0 (-4.1 .. +6.1) | 6.3 |
| Bakes per second (steady) | 348.5, 3.6 | 349.5, 5.0 | +1.0 (-4.7 .. +6.7) | 7.0 |
| ms per loop pass (steady) | 7.918, 0.159 | 7.821, 0.176 | -0.096 (-0.315 .. +0.123) | 0.271 |
| ... idle (the bakes) | 5.011, 0.059 | 4.974, 0.057 | -0.037 (-0.113 .. +0.039) | 0.094 |
| ... wait (pacing) | 1.881, 0.081 | 1.839, 0.096 | -0.042 (-0.158 .. +0.074) | 0.144 |
| ... draw | 0.963, 0.025 | 0.947, 0.024 | -0.016 (-0.047 .. +0.016) | 0.039 |
| ... poll; other | 0.051; 0.011 | 0.050; 0.011 | -0.001; -0.000 | 0.002; 0.001 |
| CPU per bake, ms | 1.810, 0.007 | 1.813, 0.013 | +0.004 (-0.011 .. +0.018) | 0.017 |
| Render thread Mcycles per pass | 22.90, 0.28 | 22.65, 0.28 | -0.25 (-0.62 .. +0.12) | 0.46 |
| Timeout waits: share of waits; mean ms | 0.772; 2.235 | 0.775; 2.179 | +0.003; -0.056 (-0.193 .. +0.081) | 0.008; 0.170 |
| Yields per pass | 0.107 | 0.117 | +0.010 (-0.007 .. +0.027) | 0.021 |

**With 0365's code and an identical probe in both builds, the after build's bake-ahead is not longer**: -25 ms, 95 %
interval -221 .. +171 ms, against the +563 ms (set 2) and +444 ms (set 1) of 0365's sets; every part of the pass is the
same within its interval. The probe itself costs nothing measurable (`other` 0.011 ms per pass, `poll` 0.05 ms).

**What the run-to-run spread is made of** [L, the 12 arms]: the bake-ahead ranged 15,054-15,531 ms (477 ms) across
arms of the same setup; per arm it follows the steady ms per pass (r 0.67), which is set by the pacing wait (r 0.98 with
ms per pass) and the timeout waits' mean (r 0.97: 2.09-2.42 ms by arm), not by the CPU per bake (r 0.17). The 2 ms
timeout of the pacing wait lasts 1.0-3.0 ms (10,589 of the 10,670 timeouts of the steady parts in the four 0.5 ms bins
between 1.0 and 3.0 ms, 80 at 3.0-3.5 ms, one longer; the timer resolution in force was 1.0 ms nearly every second,
0.5 ms in a few), so a wait ends on the 1 ms timer tick and its length depends on where in the tick the slice ended; slow arms have their timeout waits in 2.0-3.0 ms
(arm 12: 44, 68, 297, 428 per bin), fast arms more in 1.0-2.0 ms (arm 3: 179, 252, 188, 297). Slow arms also have fewer
yields per pass (r -0.89 with ms per pass) and a lower camera wake latency in the composite's lockstep rows (arms 12 and 9:
0.48 and 0.44 ms against 0.61-1.27 ms), the same signature as 0365's slow after arms (0.31 and 0.71 ms, section 2). That
is a per-run regime of how the render thread's passes line up with the 1 ms timer tick and the EXE's 30 fps camera
cadence, independent of the build. [I: the phase between the EXE's vsync-paced frames and the system timer tick is fixed
for seconds at a time, so a whole 15 s bake-ahead stays in one regime.]

## 4. The same code without the probe: do 0365's binaries reproduce the +0.5 s?

(Method written 2026-11-16 06:15 EST, before the run.) Section 3 changes the binaries: the probe adds code to the render
host, and every function after it moves (an instrumented build shares the begin and end RVA of only its first 2,199
(before) or 2,252 (after) of about 8,300 `.pdata` entries with the plain build of the same commit). So a build effect
tied to where the code lands (alignment of the bake path) would not show there. The second job runs 0365's set-2 code
without the probe: `srv_render_host.cpp` put back from git in the same source copies, built by `shell/evidence/0402/build_plain_0402.sh` (before `c4688fa8...`, after
`ec27262f...`; proxy, composite and x64 libraries as section 3).

**These builds place the code as 0365's did** [C, 2026-11-16 06:05]: the build is deterministic apart from the build-id
strings. A second build of the same before source without `--repo-commit` (as 0365's before build was made: `repo
unknown`) gives another file hash (`33a9ff49...`; the embedded strings differ) but **all 8,268 functions at the same
begin and end RVA** as `c4688fa8` (`pe_sections_0402.py`, which compares the function tables). 0365's binaries
(`41b51f57`, `3ee01a0f`) were built from the same two commits with the same deps, fork copy and toolchain on the same
day, so their function placement is this one [I: their files are on oldpc only and were not compared byte for byte].
Between the before and after builds 39 % of the bake-path functions (`Residency`, `PipelineBaker`, `BlendPass`,
`BakePost`, `srvrender`) and 30 % of all functions sit at another offset modulo 64 (`placement_0402.py`).

Job `20261116-055218-5e04`: 12 arms (before, after, after, before x3), 1 turn each, the same save and job script as
section 3. Reading: if the after arms are again about 0.5 s longer with a higher CPU per bake or a different wait
regime, the effect belongs to these two binaries (code placement), not to what 0365's code does; if not, 0365's +0.5 s
was the run regime of section 3 falling twice the same way.

**Run** [L, job `20261116-055218-5e04`, oldpc, 2026-11-16 06:09-06:56 EST, rc 0, every arm exit 0; quiet proof: alone,
foreign 0.136 cores (svchost 162 s, MsMpEng 95 s, audiodg 26 s), CPU mean 19.9 %, lowest available memory 6.2 GB;
`results_plain.json`; grade A]:

| Metric | before (n 6): mean, sd | after (n 6): mean, sd | after - before, Welch 95 % | detectable |
|---|---|---|---|---|
| Bake-ahead after the field, ms | 15,437, 226 | 15,523, 101 | **+86 (-161 .. +333)** | 282 |
| Bakes in it | 5,116.5, 11.2 | 5,119.0, 9.5 | +2.5 (-11.1 .. +16.1) | 16.8 |
| Bakes per second (steady) | 347.5, 6.5 | 345.2, 3.6 | -2.3 (-9.5 .. +4.9) | 8.5 |
| ms per loop pass (steady) | 7.867, 0.195 | 7.991, 0.167 | +0.124 (-0.114 .. +0.361) | 0.294 |
| CPU per bake, ms | 1.823, 0.018 | 1.832, 0.012 | +0.009 (-0.011 .. +0.030) | 0.025 |
| Bake CPU per pass; rest per pass, ms | 4.982; 2.885 | 5.053; 2.938 | +0.071 (-0.015 .. +0.158); +0.053 (-0.106 .. +0.211) | 0.107; 0.196 |
| Yields per pass | 0.112 | 0.104 | -0.007 (-0.023 .. +0.009) | 0.019 |

**0365's code with 0365's code placement does not take 0.5 s longer either**: +86 ms, 95 % interval -161 .. +333 ms;
0365's +563 ms (set 2) and +444 ms (set 1) are outside it. The machine's CPU during each bake-ahead (the quiet proof's
samples, `machine_cpu_0402.py`) was 22.6-26.4 % in every arm, no foreign burst.

**The level moves within a job and holds for several arms** [L]: the same two builds took 15,572-15,688 ms in arms 1-6
(06:09-06:32) and 15,210-15,538 ms in arms 7-12 (06:32-06:56); the before build alone 15,614 / 15,688 / 15,621 ms in
the first half and 15,251 / 15,237 / 15,210 ms in the second, a 408 ms step for one binary. It shows in the loop's pass
rate (idle slices 118.7-120.6 per second in arms 1-6, 124.2-125.1 in arms 7-10 and 12, 120.7 in arm 11), as in section 3.
The residuals of the arms from their build's mean are correlated from one arm to the next (lag-1 autocorrelation 0.57
in this job, 0.26 in section 3's; residual sd 167 and 137 ms). Across jobs the same before placement measured
15,003-15,266 ms in 0365's two jobs and 15,210-15,688 ms here.

## 5. The answer

[L, sections 2-4] **0365's +0.5 s was not caused by 0365's code or by its builds.** It was the bake-ahead's run level on
oldpc, which drifts by up to about 0.4 s and holds for tens of minutes, falling on both after arms of each 0365 set:
- **Set 2 (+563 ms, `staleFirst` off, the code that is merged):** the same code re-run 6 + 6 interleaved, with 0365's
  code placement (section 4: +86 ms, -161 .. +333) and with a probe in both builds (section 3: -25 ms, -221 .. +171),
  shows no lengthening; the bakes, the CPU per bake and every part of the loop pass are equal within their intervals.
- **Set 1 (+444 ms, `staleFirst` on, a build not merged):** about 0.3 s of it was real: 103-112 more bakes (each load
  generation's re-bakes no longer coalescing) at about 350 bakes/s; the rest is the same run level. `staleFirst` is
  off by default since set 2.
- **What the level is made of:** the bake-ahead is paced by the render loop (section 1): its length is the loop's
  pass rate times the bakes per pass. The pass rate moves with the pacing wait (r 0.98 with ms per pass), whose 2 ms
  timeout lasts 1.0-3.0 ms on the 1 ms timer (the timer resolution in force on oldpc, measured), and with how the
  passes meet the EXE's 30 fps cameras (fewer yields and a lower camera wake latency in slow arms, in 0365's arms and
  in these). Neither depends on 0365's code. [I: the camera cadence and the timer tick keep their phase for minutes at a
  time; not measured directly: a first look at the camera times modulo 1 ms showed no sharp alignment.]
- **Why the ABBA sets caught it:** with 21-minute arms (30 turns each) the two after arms of 0365's sets were the
  middle 42 minutes of each 85-minute job. A level that holds for several consecutive arms (autocorrelation 0.26-0.57
  between 4-minute arms here) and steps by 0.4 s puts the same offset on both after arms against both before arms; with
  two arms per build nothing separates that from a build effect. Turn times (30 per arm, medians 35.75-36.15 s) and the
  machine's CPU showed no such step, so the step is specific to the render loop's pacing, not a slower machine.

**For later A/Bs of the bake-ahead or the load on oldpc** [I]: compare at least 6 short arms per build (1 turn each,
about 4 minutes, as here), interleaved in rounds of ABBA, and read the difference against the within-job drift (the
residual autocorrelation); two long arms per build cannot tell a 0.5 s build effect from this level.

**A side finding for the MC** [I, from section 3's split, not measured as a change]: in the bake-ahead the pacing wait
takes 1.84-1.88 ms of each 7.8-7.9 ms loop pass (about 24 %) while bakes are pending (`srv_render_host.cpp`: 2 ms
`WaitForMultipleObjects` when `sourcePending()`), and the 2 ms lands on the 1 ms timer as 1-3 ms. A loop that does not
sleep while bakes are pending and no camera waits, or waits on a high-resolution timer, could shorten the huge
bake-ahead by up to about a fifth (about 3 s of 15). The wait is 3308's "wake again soon" between budgeted slices;
whether a shorter one costs the EXE's frame rate or the lockstep would have to be measured; a separate ticket if wanted.
(Done as ticket 0414: section 6.)

## 6. Ticket 0414: no pacing wait after a slice that baked

Ticket 0414, 2026-11-17 from 08:10 EST, account B, branch `task-0414-no-sleep-while-bakes-pending` (from main
0ece34f5). Scripts and evidence in `shell/evidence/0414/`.

### 6.1 The change [C]

`server/src/render/srv_render_host.cpp` `Loop`, new server.cfg key **`render_bake_no_sleep`** (0 | 1, default 1; any
other value refuses the start, exit 2): while the source has bakes pending (`sourcePending()`), a pass whose `idle()`
slice made at least one bake ends with a wait of 0 ms on `{stop, <base>.Cam}` (event pacing) or no wait (legacy and idle
pacing) instead of the 2 ms wait of 3308. A pass whose slice made no bake (no token, a yield at the slice's first check,
nothing left to bake, the 100 ms cache of `pending()` still saying pending) waits the 2 ms as before. So the bake rate is
set by the residency's token bucket (`render_bake_rate`, 400/s, burst 16) and the bake CPU, no longer by the loop's
sleeps; tokens are not lost while the thread bakes, because the bucket (cap 16 = 40 ms of tokens) is never full then.
- **How the loop knows that the slice baked:** through the residency's yield check (`ResidencyOptions::yield`, issue
  0140). `Residency::idle` calls it after picking a tile and before each bake, and once more at the end of a slice that
  baked (`render/src/bakeresidency/residency.cpp`); the host's callback `CameraWaiting` now gets the `Host` as its
  context (was `&h.scene`) and counts the checks that found no camera (`bake_checks`). The count moves during an
  `idle()` call exactly when that call baked. No file outside the host changes; with `render_bake_yield=0` there is no
  check, so the loop keeps the 2 ms wait (tested).
- **Cameras:** unchanged. The slice still ends before its next bake once a camera waits (the yield), the next pass
  draws it, and the 0 ms wait still takes a set `<base>.Cam` (auto-reset) as the 2 ms wait did. At most one bake
  (about 1.8 ms on oldpc) stands between a camera and its draw, as before.
- **Counters:** the `host 1s:` line gets `bake_no_sleep=<0|1> idle_no_sleep=<passes without the wait> bake_checks=<n>`
  after `idle_calls=`; the first pass without the wait logs `pacing: bakes pending: no pacing wait after a pass whose
  slice baked (render_bake_no_sleep=1, ticket 0414)` once.
- **Expected effect [I, from sections 1 and 3]:** in the huge bake-ahead the loop's pass was about 5.0 ms of bakes +
  1.9 ms of wait + 1.0 ms of draw + 0.06 ms of the rest; without the wait about 2.7 bakes per 6 ms pass would ask for
  about 450 bakes/s, above the 400/s token rate, so the bake-ahead becomes token-bound: about 5,116 / 400 = 12.8 s
  against 15.3-15.5 s, about -2.5 s. The render thread's CPU in the bake-ahead rises from about 65 % to about 75 % of a
  core (400 x 1.8 ms/s). A further gain would need a higher `render_bake_rate` (not this ticket).

### 6.2 The tests [C]

`server/tests/render/test_render_host.py`: `test_no_pacing_wait_while_the_slice_bakes` (the stand-in baker's whole-map
bake-ahead of a 16 x 16 field, one bake per slice, event pacing with no `<base>.Cam` after the first: with the default
every slice's pass skips the wait (`idle_no_sleep` within 2 of the slices that baked), the note is logged; with
`render_bake_no_sleep=0` in a second process no pass skips it and the bake-ahead takes at least 0.5 ms per bake longer,
a bound that a loaded or sanitized machine cannot break, as every such pass waits at least 1 ms);
`test_no_sleep_needs_the_yield_check` (negative: `render_bake_yield=0`, no check, no skip, `bake_checks=0`);
`test_config_refused` with `render_bake_no_sleep=2` and `=on` (exit 2, nothing written into the section).

Results [L, workstation, 2026-11-17, an indication only (COMMON 23)]: render-host suite plain 134 passed (08:21-08:35,
`run_all.py --only render-host`). The new test under ASan (09:05, machine loaded by five other teams' heavy jobs):
256 bakes in 2,175 ms with the default (`idle_no_sleep` 256 of 256 slices, `bake_checks` 512 = one check before and one
after each one-bake slice), 5,944 ms with `render_bake_no_sleep=0` and 5,923 ms with `render_bake_yield=0` (no skip,
`bake_checks` 0). The 2 ms wait lasted about 14.7 ms per pass there: the test host runs at the default 15.6 ms timer
resolution, while the game on oldpc runs at 1.0 ms (section 3), so **outside the game the old wait cost far more than
the 1-3 ms measured in it** [I: the test host does not raise the resolution; not read]. ASan suite: see 6.5.

### 6.3 The measurement: method (written 2026-11-17 08:30 EST, before the run)

One quiet headed testbed job on oldpc, the product path (`tools/testbed/jobs/product_path.py` through
`shell/evidence/0414/pp_0414.py` and `ab_0414.py`), the huge save `TB-HUGE_0350`, **one server.exe for both arms**
(built from this branch by `shell/evidence/0414/build_0414.sh`; proxy and composite from the same source copy; the four
x64 libraries of the pin layout `x64-04b1211b-pfdee656`, DLL `29f58e17...`); the arms differ only by
`render_bake_no_sleep` through `vpproxy.ini server_cfg_extra=` (TEST ONLY, as 0349), so no code-placement difference is
possible (section 4). A warm-up arm (`nosleep`, not analysed), then 12 arms of 1 AI turn each (about 4 min),
`sleep,nosleep,nosleep,sleep,nosleep,sleep,sleep,nosleep,sleep,nosleep,nosleep,sleep`: 6 per arm, in consecutive pairs
(1-2, 3-4, ...) that each hold one arm of each build, their order alternating (ABBA BAAB ABBA). The design against
0402's level drift (section 4: a level that steps by up to 0.4 s and holds for several 4-minute arms, lag-1
autocorrelation 0.26-0.57): the analysis (`shell/evidence/0414/analyze_0414.py`) takes the **paired** difference inside
each pair (6 pairs), which a level holding for at least two arms leaves out, besides Welch over the 6 + 6 arms, and
reports the residuals' lag-1 autocorrelation. Metrics: the bake-ahead (ms after the field, bakes, passes, passes without
the wait, the steady part per pass), the composite's lockstep rows in the bake-ahead (exact share, camera wake latency,
server CPU and GPU per frame, the composite's lockstep wait) and in play, over the AI turn (pp_0349's per-turn
`lockstep 1s:` sums: exact / older / none share; the residency's gray counters: `gray_frames`, `gray_after_ahead`,
`gray_tile_frames`, `stale_frames`), and the turn's wall time.

Expected: the bake-ahead about 2-3 s shorter in the `nosleep` arms with the same bakes; in play the lockstep shares and
the gray counters equal within their intervals (the turn has few re-bakes: 360-390 in 0402's arms).

Builds [C, 2026-11-17 08:35-08:44 EST, `shell/evidence/0414/build_0414.sh` from commit f1d775d0, release, deps of the
pin 04b1211b8]: server.exe `a2f1d031...`, proxy `d57f32fe...`, composite `316df30f...`; x64 libraries of the pin layout
(DLL `29f58e17...`, lua51_x64 `30724d6d...`, CvGameDatabase_x64 `9c37b39e...`, CvLocalization_x64 `35863ede...`).
Job `20261117-084445-3bce` submitted 08:44 EST, queued behind 4666's quiet job `20261117-083839-4101` (planned end
11:39; the MC's word on 0414's thread: wait in the queue, MC fetches). GPU: oldpc's RTX 2080, NVIDIA driver
32.0.16.1088 (installed by Windows Update 2026-11-17 01:40 and held since by D175, quiet-testbed.md 25.1; 0402's jobs of
2026-11-16 ran on 31.0.15.4592, so their levels are not compared with these) [I: the job does not read the driver].

### 6.4 The measurement: results [L, job 20261117-084445-3bce, oldpc; filled by the MC 2026-11-17 12:10]

Run 11:06:00-11:56:15 EST (queued from 08:44 behind 4666's and other quiet jobs), job rc 5 because one arm (7-sleep)
exited 5; its numbers are read and kept (ab_0414.py: exit 5 = an unexpected LOUD line or a flat picture, the arm
restored). Arm 7's LOUD line is not this ticket's: `load 3: a check failed 4 polls in a row ... the grid's header
changed during the copy (a torn read)`, from the cut tap (`shell/composite/cut_tap.cpp`, epic 0431) during the grid's
growth after the load, in a **sleep** arm (the old behaviour); the other 11 arms had none. Follow-up ticket 0500.
Arms' exits: warm-up nosleep 0, then 1-sleep 0, 2-nosleep 0, 3-nosleep 0, 4-sleep 0, 5-nosleep 0, 6-sleep 0, 7-sleep 5,
8-nosleep 0, 9-sleep 0, 10-nosleep 0, 11-nosleep 0, 12-sleep 0 (each 231-234 s). Quiet proof: alone true, undisturbed
false, foreign load 0.141 cores mean, machine CPU mean 21.0 % (max 56.2 %), lowest available memory 7,462 MB, not
interrupted. Arm check: `idle_no_sleep` > 0 only in the nosleep arms (sleep 0 in all six) [yes]. Grade A (6
interleaved per arm in ABBA/BAAB pairs, quiet machine). GPU RTX 2080, driver 32.0.16.1088 (D175). Fetched by the MC
into `F:/c5-64bit-work/0414/oldpc-3bce` (`post_0414.sh` with its wait check skipped, since `tb.py wait` returns 1 for a
job that ended rc 5); statistics `shell/evidence/0414/results_0414.json` (`analyze_0414.py`).

| Metric | sleep (n 6): mean, sd | nosleep (n 6): mean, sd | paired nosleep - sleep (6 pairs), 95 % | Welch 95 % | lag-1 |
|---|---|---|---|---|---|
| Bake-ahead after the field, ms | 25,868, 322 | 22,813, 107 | **-3,055 [-3,392, -2,718]** | [-3,393, -2,716] | +0.15 |
| Bakes in it | 7,878, 281 | 8,561, 274 | +683 [+152, +1,215] | [+321, +1,046] | -0.53 |
| Host idle calls in it; passes without the wait | 3,322, 176; 0 | 7,031, 187; 5,759, 340 | +3,709 [+3,530, +3,889]; +5,759 | | +0.07 |
| Loop: ms per iteration | 7.81, 0.45 | 3.25, 0.10 | -4.56 [-4.97, -4.14] | | +0.03 |
| Steady: ms per pass; bakes per pass; bakes per second | 8.35; 2.69; 321.5 | 3.20; 1.26; 392.2 | -5.15; -1.43; **+70.7 [+66.9, +74.5]** | | +0.05 |
| Steady: CPU per bake, ms | 1.883, 0.006 | 1.888, 0.018 | +0.005 [-0.020, +0.029] | | -0.47 |
| Bake-ahead: draws per second | 38.88, 0.37 | 37.33, 0.37 | **-1.56 [-2.04, -1.07]** | [-2.04, -1.07] | -0.11 |
| Bake-ahead: lockstep exact share; camera wake latency ms | 0.360; 6.55 | 0.435; 4.24 | +0.075 [-0.034, +0.185]; -2.31 [-4.32, -0.30] | | |
| Bake-ahead: server CPU / GPU per frame ms; composite's lockstep wait ms | 0.615 / 0.543; 0.655 | 0.625 / 0.508; 0.649 | +0.010 / -0.035; -0.007 [-0.041, +0.027] | | |
| Play (AI turn): lockstep exact / older / none share | 0.531 / 0.470 / 0 | 0.521 / 0.479 / 0 | -0.009 [-0.035, +0.016] / +0.009 / 0 | | +0.10 |
| Play: gray_frames; gray_after_ahead; gray_tile_frames | 0; 0; 0 | 0; 0; 0 | 0 | | |
| Play: turn wall time, s | 39.75, 0.15 | 40.77, 1.36 | +1.02 [-0.29, +2.32] | [-0.42, +2.45] | -0.08 |

Reading [L, I]: **the bake-ahead is 3.05 s shorter (-11.8 %) with more bakes**, as predicted (2-3 s): without the pacing
wait after a slice that baked, the loop runs 2.4 times as often (7.8 -> 3.2 ms per iteration) and the steady part bakes
392 per second, the token rate's ceiling (about 400/s), against 321 with the wait; the CPU per bake is unchanged, so the
gain is the removed waiting, not cheaper bakes. The cost is a frame rate during the bake-ahead 1.56 frames per second
lower (38.9 -> 37.3, -4.0 %; the loop takes more of the frames' time), for those 23 s only. In play nothing measurable
changes: the lockstep shares and every gray counter are equal within their intervals (gray 0 in both), and the AI
turn's wall time difference (+1.0 s) includes 0 in both intervals; one nosleep arm (11) had 3 stale frames, which is
why its sd is larger. Verdict: the default `render_bake_no_sleep=1` is accepted (PERFORMANCE.md: measured, interleaved,
explained).

### 6.5 The render-host suite under ASan [L, workstation, 2026-11-17]

`run_all.py --only render-host --asan` on this branch (08:46-09:05) and on main's files 0ece34f5 (09:09-09:25, a `git
archive` of main, the same machine load: five other teams' heavy jobs, games and ASan suites): **no ASan report in
either**. Main: 5 failed, 125 passed; this branch: 8 failed, 126 passed (the 4 new tests pass). The 5 that failed on
main failed here too, for one cause that is not this ticket's: fixed-length runs (`--seconds 3` to `6`) that end before
the bake-ahead does, because ASan's start-up took about 1.8 s and 0365's decal hold waits up to 2,000 ms for a decal set
the stand-in never gets (`test_bake_yield.py`'s two tests, also with `render_bake_yield=0`, where 0414's code changes
nothing; `test_residency_source.py`'s radius and feed-ignores tests). The other 3 of this branch
(`test_bake_standin_whole_map_resident_and_drawn`, `test_decal_wiring.py`'s `test_set_after_the_wait_bound_rebakes_its_tiles`
and `test_unresolvable_name_is_an_install_failure[no_attribute]`) passed when run again on the same ASan build (09:27,
5 passed with the other parameters; the first two also at 09:05). **Open for the owner of those tests** [I]: the
stand-in tests with a fixed `--seconds` do not leave room for 0365's 2 s hold under ASan on a loaded machine; a test
that waits for `bake-ahead complete` (as 0414's does) instead of a fixed run would not fail this way. **Done by ticket
0496 (6.6).**

(The reading of the results is at the end of 6.4.)

### 6.6 Ticket 0496: the stand-in tests wait for the event they check [C, L workstation 2026-11-17]

Every render-host test that ran `render_host_test.exe` for a fixed `--seconds` and then asserted on what the run had
reached (the bake-ahead, a feed round, a decal set, the fatal hook) now starts with `Rig.start_until` (a `--stop-event`
of issue 0333, `--seconds` only the bound: 120 s, 300 s under ASan), waits for that event with a bounded timeout
(`renderfx.WAIT_S`: 60 s, 120 s under ASan; the wait ends early with the host's output when the process ends first),
and ends the run with `Rig.stop`. The waits: `wait_residency(pred)` (a residency part of a `terrain 1s:` line that
meets pred, e.g. `fx.ahead_done`: ahead_done=1 and pending=0), `wait_next_residency` / `wait_next_second` (one more
per-second line: the renderer has taken what the test did, and a negative case sees anything that came late),
`wait_stdout` (the fatal hook's `result: fatal hook code` line), and `wait_log(..., p=)`. Converted: both tests of
`test_bake_yield.py`; in `test_residency_source.py` the near-key, whole-map, radius, options bad value, both feed tests,
failed tile, no-install pipeline, tiles none/tap, the real-pipeline bake (it ran its whole 45 s / 120 s; now it stops one
line after the bake-ahead), idle pacing's wake and both table tests (4555); in `test_decal_wiring.py` the `start()`
helper and every test using it (a `--seconds` of 4 to 13; the sleeps of 3.5 s and 3 s that stood for "the bound passed"
are a wait for the fatal line, or for the LOUD line and the bake-ahead plus 1.5 s); `test_decal_names_mods.py`'s
`run_set` (its 1.5 s sleep is `fatal_or_set`: the set in use or the fatal line, then one more host line). Kept as they
were: the config-refused cases (exit 2 at once), the 0333 / 0414 runs that already stop by event, and the quiet windows
(`time.sleep(1.0)` / `1.5` / `0.8` after the bake-ahead), which now cannot be cut short by the run's end. `wait_log`'s
default timeout is `WAIT_S` (was 20 s). No product code changed.

A second cause, found by the series [C, `server/src/feed/srv_feed.cpp` Drain; L, ASan attempt 1 run 5]:
`test_feed_changes_the_bake_ignores[owner-2]` waited its 120 s with `feed_plots=20`, not 40. `render_host_test.exe
--feed-rounds 2` started round 2 0.5 s after round 1's last patch, and `SrvFeed::Drain` hands over each changed plot once
per drain (`g_changedMark`), so when no drain came in those 0.5 s both rounds of a plot were one changed plot. Under ASan
beside the load the render thread's drains came more than 0.5 s apart; the fixed 5 s run of before would have failed the
same way (its assertion is `feed_plots == 40`). The test host now also waits, before each round after the first, until
`SrvFeed::GetCounters().drains` has moved past its value at the last round's last patch (`render_host_test.cpp`, test code
only); `test_feed_changed_plots_rebake_their_tiles` (2 rounds) had the same exposure.

A third, the render thread's teardown against `HostStop`'s 5 s [L, measured 2026-11-17 17:50 over the render logs of
ASan attempt 4 runs 8-10; C, `srv_render_host.cpp` ThreadProc / HostStop]: `test_tap_failed_layout_in_play_is_loud_not_fatal`
(plain attempt 2 run 4), `test_pipeline_without_an_install_draws_with_the_fallback` and
`test_pipeline_bakes_a_real_tap[1]` (ASan attempt 4 run 10) found no totals line. Each render.log has
`LOUD: the render thread did not stop within 5 s`. `HostStop` sets the stop event, waits 5 s for the thread and then
leaves it running. The thread writes the totals line (terrain, residency, decal totals) only after `ReleaseDevice`, and
`render_host_test.exe` exited before that. From `stop:` to the totals line in 104 stops: median 0.15 s, p90 1.5 s, the
longest that finished 4.7 s, and 2 over 5 s. The thread was running at the stop (a `host 1s:` line 60-150 ms before it
in both ASan cases). The same holds for server.exe's own shutdown beside heavy load; a ticket for the product's bound
is the MC's call. The fix is in the test host only: when `HostStop` logged that line, `render_host_test.exe` waits up to
60 s for the totals line from the thread it left running, and prints `result: HostStop left the render thread running;
its totals line came N ms after HostStop returned`. `Rig.finish` / `Rig.stop` wait 90 s (was 60). (Corrected
2026-11-17 17:54: this paragraph first read the plain case as a stall that began before the stop; that came from
reading the time of the last `1s:` line, and the stop came 0.37 s after it. A `Rig.stop` that first waited for the
next `host 1s:` line, written for that reading, was taken out again.)

A fourth, a wall-clock bound (plain attempt 3 run 4): `test_idle_slice_yields_to_a_waiting_camera` saw
`idle_slice_max_ms` 107 against its bound of 50, with 6 yields in 7 slices and `yield_wait_max_ms` 8.5: one slice ran its
whole 100 ms budget, which with the yield on means that no camera waited during it. The cameras come from a Python
thread at 100 Hz; beside the load it was starved for longer than 100 ms [I: the gap was not measured in that run]. The
test now measures the writer's longest gap between two writes and allows the slice that much beyond its 10 ms period
(`BUDGET_MS / 2 + max(0, gap - 10 ms)`); the yield's own measure (`yield_wait_max_ms < 50`) is unchanged. Other wall-clock
bounds kept as they were: `test_set_after_the_wait_bound_rebakes_its_tiles` (resume 1,990-4,000 ms; one ASan run here
logged a resume 4,942 ms after a 2,000 ms hold in another test, so this bound can fail when the render thread stalls),
`test_set_after_the_field_within_the_wait_bakes_once` (200-2,000 ms), `test_idle_pacing_wakes_at_once_on_the_first_publish`
(under 0.9 s).

Kept, checked by grep: `test_render_host.py`'s 16 runs with a fixed `--seconds` (2 to 14) use the EXE-cache tap source
(the rig's default `render_tiles=tap`): no residency, so no bake-ahead and no decal hold; none failed in 0414's loaded
ASan run (6.5) or in this ticket's full-suite runs (not run in the 10x series; they gain the teardown wait of the test host too).

Results [L, workstation, 2026-11-17; `server/evidence/0496/run_0496.sh`: `series_0496.py` runs the four changed modules
(77 tests) N times on one build, `load_0496.py` runs 6 load jobs of 2 busy cores and 256 MB each, registered through
`quietlock.py run`, beside the other teams' work (2-4 game instances, other suites)]. Final code (attempt 5, 17:55-19:41,
both builds of that code): **plain 10/10 runs passed (227-261 s a run), ASan 10/10 (368-411 s), no ASan report.** The
earlier attempts each found one of the causes above: attempt 1 (fixed waits only) plain 10/10, ASan run 5 the feed
rounds; attempt 2 plain run 4 the teardown; attempt 3 plain run 4 the slice bound; attempt 4 plain 10/10, ASan run 10 the
teardown again (the first mitigation did not hold, which led to the measured cause). Before the change, 0414's ASan run on
the same machine failed 5 of these tests on main (6.5).
