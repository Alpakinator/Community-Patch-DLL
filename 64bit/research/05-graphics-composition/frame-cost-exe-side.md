# The EXE's side of a lockstep frame: head, E, wait, tail (ticket 0715, M1 of epic 0686)

Ticket 0715 (impl-0715, account B, started 2026-11-23 20:22 EST) answers the Architect's M1
(`research/07-architecture-decisions/architect-2026-11-23-frame-overlap.md` section 5): under exact lockstep (D242) a
product frame is **head + max(E, S) + tail**, and on the quiet workstation about 4.5 ms of the 7.6 ms gap to stock was
on the EXE's side, never split. This file splits it. Marks: `[C]` read in the code, `[L]` measured live, `[I]` inferred.

**Result (2026-11-23, workstation, one launch per arm: an indication) [L for the timed rows]:** against a stock-like
EXE frame measured with the same probe (6.96 ms), the product's EXE side is 9.18 ms (+2.22: E +1.30, head +0.62, tail
+0.30), plus the lockstep wait 2.56 ms. E does not depend on the server (S +7 ms leaves E unchanged). Named: the model
capture's Unmap copy on the main thread **1.82 ms** (an uncached read of dynamic vertex buffers, 170-250 MB/s on the
RTX 4080; ticket 0719), Place 0.29 ms, the Present hook's taps and publication 0.48 ms, the insertion's own work in
the tail 0.30 ms; the product's own savings in the EXE (about -0.8 ms) are a remainder. Section 3 has the table.

**Current figures on beta 4 and EXE 26577625 (M2, ticket 0754, 2026-11-25, section 6; 3 launches an arm):**
- Workstation, moving: the gap of +2.98 ms is the wait 2.05 + the EXE side +0.83.
- Workstation, at rest: +2.54 = wait 1.03 + EXE side +1.48.
- Laptop at rest: the whole gap is the wait. The EXE side is 0.65-1.46 ms below stock's frame.
- E's launch spread is a machine-wide speed state: stock's own frame spreads 0.87 ms.

The M1 figures above are kept as measured on EXE 4390913 before 0719.

**0757 (2026-11-25, section 7, design):** after 0719 most of the copy is the uncached tail read of the pages a map did
not write: 0.42 of 0.77 ms per present, and 0.68 ms at rest, at 129 MB/s. Streaming loads (`VMOVNTDQA` / `MOVNTDQA`)
read the same memory 5-30x faster with the same bytes (bench). Route A, that read done in place, is recommended to the
MC.

**0758 (2026-11-25, section 8): route A built.** Same bytes offline, in the stand-in and live (1.92 GB of tail compared on
the laptop, 0 differences). Laptop grade A, at rest: copy at Unmap 2.18 -> 0.64 ms per present, E -1.23 ms, interval
unchanged (server-bound). The workstation series is owed.

Definitions (per Present N of the target swap chain, all QPC on one clock; `shell/evidence/0715/split0715.py`) `[C:
shell/composite/shell_composite.cpp at e7dc4b12c]`:

| Stretch | = | What runs in it |
|---|---|---|
| head | `pub(N) - present(N-1)` | the previous Present hook's work (`OnPresent`: the farm guard, the probe row, twelve taps, the unit/model filter's latch and the model publication, 4595-4732), the real `Present` call, the message pump, up to `UpdateViews` |
| E | `ins(N) - pub(N)` | the EXE's own frame from the camera's publish (`LkPublishAtUv`, at `UpdateViews`) to the insertion after the scene-clear list (`CompositeSource` stamps `ins` first, before any wait, 3470-3475) |
| wait | `wait1 - wait0` | the lockstep wait for the server's frame (0 when the frame was ready; `wait0 - ins` is 0.01 ms p50) |
| tail | `present(N) - max(ins, wait1)` | the composite's take and draw of the server frame, the EXE's 3D lists and interface list, up to the Present hook's entry (`present_qpc` is taken at `OnPresent`'s start, 4600, before the taps: the taps are in the next frame's head) |

`interval = head + E + (wait0 - ins) + wait + tail` exactly; the script checks it (the means add to the interval's mean
to 0.01 ms).

## 1. Step 1: the quiet pair's product run, read again (no build, no game) [L: the run's files; I: marked]

Inputs: lead-0673's quiet pair (`pan-coherence.md` 5.15 on lead-0673's branch at `d60ba2d9a`): the workstation in a held
quiet hour, EXE 4390913, huge save C5H-VP8P-HUGE_0350 (turn 350), uncapped (`launch --timing`), hidden, product build
`acc-0691b` (exact lockstep, ring hand-over, the reporter's `[Renderer]` set: ServerRenderer, Units, Cities, Landmarks,
FarmFields, Combat = 1), drive `drive0691.py` 3 repeats (key and drag pans), 2026-11-23 19:50-19:54 EST. Files:
`C:/c5-64bit-work/0673/quietpair/product-shell_composite.lockstep.csv`, `product-shell_composite.log`,
`marks-product.tsv` (the analysis window = first mark - 2 s .. last mark + 1 s: 5,584 presents, every one `exact`).
Command and full output: `shell/evidence/0715/README.md`, `shell/evidence/0715/step1.txt`. **One launch: an indication,
not a grade-A figure.**

### 1.1 The stretches per present (ms)

| Moving camera (1,748 presents) | p50 | p90 | p99 | mean |
|---|---|---|---|---|
| head | 1.82 | 2.63 | 4.29 | 1.94 |
| **E** | **8.37** | 14.06 | 18.20 | **8.96** |
| wait, all presents (0 when ready) | 1.79 | 5.96 | 9.40 | 2.36 |
| wait, the 63.1 % that waited | 3.13 | 6.55 | 9.98 | 3.74 |
| tail | 0.46 | 0.73 | 1.00 | 0.49 |
| present interval | 13.45 | 17.45 | 22.28 | 13.75 |
| S (server publish -> ready, same frame) | 10.40 | 13.07 | 16.88 | 10.23 |

| Still camera (3,836 presents) | p50 | p90 | p99 | mean |
|---|---|---|---|---|
| head | 1.71 | 2.41 | 3.88 | 1.78 |
| E | 8.11 | 10.45 | 14.82 | 8.07 |
| wait, all presents | 1.84 | 5.40 | 7.93 | 2.31 |
| tail | 0.45 | 0.69 | 0.92 | 0.47 |
| present interval | 12.62 | 15.29 | 19.40 | 12.65 |
| S | 10.19 | 12.50 | 14.92 | 9.91 |

Over the whole window (5,584 presents) the means are **head 1.83 + E 8.35 + wait 2.34 + tail 0.48 = 13.00 ms**, against
stock's whole frame of **5.85 ms** (the game's own per-frame Lua hook, same drive, `pan-coherence.md` 5.15; the
product's hook gave 13.01, the CSV 13.00). So the gap is 7.15 ms at the mean: **2.34 ms the wait, 4.81 ms the EXE's
side** (head + E + tail = 10.66 against 5.85) `[L for both sums, one launch each]`.

Correction of the note's reading (section 1 of the Architect's note, `[I]` there): the 3.13 ms "composite wait" of
5.15 is the median over the frames that waited (63 % moving); over all presents the wait's median is 1.79 ms and its
mean 2.36 ms. The head + tail remainder the note estimated at "about 2 ms" is 2.3-2.4 ms (head 1.8-1.9, tail 0.5).

### 1.2 E does not move with S `[L]`

Within the run, frame by frame, E and S of the same frame are uncorrelated: Pearson r = -0.007 moving, 0.151 still
(Spearman -0.007 / 0.140); E against the S of the frame before: 0.044 / 0.082. E by quartile of S, moving: S p50 7.28 /
9.22 / 11.10 / 12.77 ms -> E p50 8.44 / 8.35 / 8.14 / 8.47 ms. So the server's longer passes do not lengthen the
EXE's work through cores, memory or GPU at this load; contention, the Architect's third suspect, is not visible at
the frame level `[L]`. (E is longer in frames that did not wait, 10.68 against 7.28 ms p50 moving: that is the
selection, not a cause: a long E leaves the server time to finish.) Step 3's arms with the server idle and slowed
test the same thing by intervention.

### 1.3 The model capture, per present, from the `modelfilter 1s:` lines `[L: the counters; I: what they mean for E]`

The composite's per-second counters divided by the CSV's presents in the same second (74 seconds in the window; the
log clock anchored to QPC by 157 `qpc_now` lines, residual at most 1.2 ms). They are thread time summed over the
threads that ran them, not wall time (`unit_filter.cpp` 1082-1100 for the copy).

| Per present | moving | still |
|---|---|---|
| draws tapped (cities + landmarks + kept) | 369 | 373 |
| copy at `Unmap` (ms / KB) | **2.58 / 464** | **3.10 / 524** |
| draw path on the recording threads (ms) | 1.13 | 1.17 |
| publication at Present (ms; in the head) | 0.41 | 0.43 |
| sum (ms) | **4.12** | **4.70** |
| unit filter: draws seen by the hooks / dropped | 1,833 / 62 | 2,005 / 72 |

- **The copy reads at 170-180 MB/s here, against 540 MB/s on oldpc** (`city-renderer.md` 12.8: 596 KB in 1.10 ms,
  camera 1, idle). The workstation is the faster machine. The copy is a `memcpy` out of the pointer `Map` returned for
  a dynamic buffer; a read at this rate is the rate of uncached (write-combined) memory `[I]`. That the RTX 4080's
  driver hands the EXE write-combined or device-local memory for these buffers, where oldpc's RTX 2080 does not or
  less so, is a hypothesis, not checked `[I]`. It is the Architect's first idea for after M1 (the `Map` hook hands the
  EXE ordinary memory and the `Unmap` hook copies it forward once).
- **The capture grows with E across the seconds** `[L, I]`: bucketed by tapped draws per present (14 seconds each),
  269 / 284 / 321 / 376 / 496 / 845 tapped -> copy + draw 3.48 / 3.50 / 3.70 / 3.85 / 4.35 / 7.47 ms -> E mean 7.96 /
  8.11 / 8.51 / 8.32 / 8.88 / 8.49 ms. From the first to the fifth bucket the capture adds 0.87 ms and E 0.92 ms,
  consistent with the capture on E's critical path about one to one; the views with more draws are also more work
  for the EXE itself, so this is not a split `[I]`. Per-second Pearson r(E, copy + draw) = 0.13 over 74 seconds.
- The composite's own `comp_mean` (the `stats` line, the insertion from its entry to its end) is 2.48 ms per frame:
  2.34 of it the wait, so the take and the draw are about 0.13 ms `[I: difference of means]`, in the tail.

### 1.4 First reading of the 4.81 ms `[I]` (SUPERSEDED 2026-11-23 23:1x by section 3)

*Corrected by the arms (2.1, 3): the copy runs mostly on the immediate context, i.e. the EXE's main thread (1.69-1.96 of
1.97-2.25 ms), not "on the recording threads"; the draw path stays on the workers; and stock's whole frame is not the
right reference: a stock-like frame with the same probe is 6.96 ms of EXE side, and the product's excess over it is
+2.22 ms, named in section 3. The step-1 reading below is kept as it was written.*

| Part of the EXE side's excess over stock | ms per present | Mark |
|---|---|---|
| the model capture's copy at `Unmap`, on the recording threads (E, if on the critical path) | 2.6-3.1 | L counter, I placement |
| the model capture's draw path, recording threads (E, same caveat) | 1.1-1.2 | L counter, I placement |
| the model publication in the Present hook (head) | 0.41 | L counter |
| the composite's take and draw (tail) | about 0.13 | I |
| sum of the timed composite parts | 4.3-4.8 | |
| still unexplained: the other eleven taps, the farm guard and the Present call (head), `Place`'s untimed constant scan (E and tail), the unit filter's 1,833 hooked draws, the proxy's round trips | the rest | open |

If the timed capture lies wholly on E's critical path, it alone names 4.1-4.7 ms of the 4.81 ms, more than two
thirds. That is the open question for step 2: stock's own head, E and tail on the same drive were never measured (9039
measured D33, not stock; its city view gave UpdateViews -> scene clear 3.0-3.4 ms and previous Present -> UpdateViews
0.36-0.42 ms), so the step-1 table compares the product's stretches with stock's whole frame only.

### 1.5 The laptop arm: 0691's acceptance run read the same way `[L: the run's files; I: marked]`

lead-0673's laptop run `20261123-112740-fd80` (`pan-coherence.md` 5.9: build `acc-0691b`, TB-HUGE_0350 t350, the
reporter's `[Renderer]` set, **the 30 fps cap with vsync**, the same key and drag pans plus the wide hold), files
`F:/c5-64bit-work/0673/cap/acc0691b-20261123-112740-fd80/out/` and `marks.tsv`; output `shell/evidence/0715/step1-laptop.txt`.
11,127 presents in the window.

| Laptop, means (ms) | head | E | wait | tail | interval | S |
|---|---|---|---|---|---|---|
| moving (1,724) | 6.12 | 15.47 (p50 14.24) | 6.48 | 5.94 | 34.03 | 20.58 |
| still (9,403) | 3.93 | 13.41 (p50 11.67) | 4.81 | 11.25 | 33.43 | 17.09 |

- **Under the cap the stretches are not the EXE's work alone**: the tail (p50 11-12 ms still) and part of the head hold
  the cap's and vsync's waits, so only E and S are comparable with the workstation `[I: where the cap sleeps was not read]`.
- **On the laptop E does move with S**: Pearson r(E, S) 0.43 moving, 0.58 still (Spearman 0.54 / 0.71); by quartile
  of S (moving) 11.6 / 18.4 / 22.4 / 28.2 ms -> E p50 8.1 / 11.2 / 14.8 / 18.3 ms. Two readings, not separated: contention
  for the laptop's cores, memory or GPU with the server's pass (the Architect's third suspect), or views that cost both
  sides alike `[I]`. On the workstation r is about 0 (1.2). Step 3's slow and light arms test it on the workstation only.
- The model capture per present: copy 2.10 / 2.47 ms for 463 / 653 KB (moving / still: 220-260 MB/s), draw 1.14-1.19
  ms, publication 0.63-0.67 ms (in the head); across seconds E follows the capture (r 0.64, 366 s; the bucket of 753
  tapped draws a present has copy + draw 6.8 ms and E 17.8 ms, the others 2.4-3.0 ms and 11.3-14.0 ms) `[L counters, I
  causation]`. The log clock's anchors fit QPC only to 149 ms on this run (laptop log), so the per-second matching is
  approximate.

### 1.6 Why the copy is slow: reading back mapped dynamic vertex buffers (bench `mapread`, 2026-11-23 21:02-21:04) `[L: bench; I: that the EXE's buffers are of this kind]`

`shell/evidence/0715/mapread/mapread.cpp` (x86, VS 2022, `build.cmd`; output outside git): a D3D11 device on the
default adapter, a `D3D11_USAGE_DYNAMIC` buffer with `CPU_ACCESS_WRITE`, each of 300 rounds `Map(WRITE_DISCARD)`,
write the buffer, then `memcpy` it out (what the composite's `OnUnmap` does), `Unmap`; median MB/s. Workstation through
`quietlock.py run` (not quiet: other teams' builds running; a bandwidth figure, little moved by CPU load `[I]`);
oldpc by testbed job `20261123-210337-0eb3` (quiet, alone, headed). Outputs `shell/evidence/0715/mapread/*.txt`.

| Read out of the mapped pointer (MB/s) | workstation, RTX 4080 | oldpc, RTX 2080 |
|---|---|---|
| heap -> heap (control, 256 KB) | 18,461 | 40,960 |
| constant buffer 4 KB / 64 KB, immediate or deferred | 13,653-17,712 | 36,409-40,960 |
| **vertex buffer 256 KB, immediate / deferred** | **249 / 238** | **501 / 501** |
| vertex buffer 4 MB, immediate | 118 | 485 |
| (write into the mapped vertex buffer, 256 KB) | 3,158 / 1,720 | 478 / 483 |

- **The driver gives dynamic constant buffers cached memory and dynamic vertex buffers uncached (write-combined or
  device) memory**; both drivers report driver command lists. Reading a vertex buffer back runs at 118-249 MB/s on the
  workstation and about 500 MB/s on oldpc `[L]`.
- These rates are the live copy's own: the workstation's live `copy_ms` reads 170-180 MB/s (1.3), oldpc's 540 MB/s
  (`city-renderer.md` 12.8: 596 KB in 1.10 ms). So the tapped buffers the copy reads are, by their rate, of the vertex
  (instance) kind and the copy's time is the uncached read, not the amount of data `[I: rate match; which buffers the
  interest table holds was not listed]`. The faster card is the slower reader: 0.98 ms on oldpc, about 2 ms on the
  workstation for 480 KB.
- Consequence for the Architect's first idea `[I]`: if the `Map` hook handed the EXE ordinary (cached) memory and the
  `Unmap` hook wrote it forward to the driver's pointer once, the read would cost about 0.03 ms instead of 2.6-3.1 ms
  per present on the workstation, at the price of one more write of the same bytes (480 KB at 1.7-3.2 GB/s: about 0.15-
  0.3 ms) and the EXE writing cached memory instead of write-combined. A fix ticket, not this one.

## 2. Steps 2-3: the arms of the quiet hour

Plan written before the run (kept as written). Run 2026-11-23 22:01-22:59 EST in the quiet hour MC-B granted (B QUIET
21:58, #12316; D247 moved it to 22:00). Commands:
`shell/evidence/0715/README.md`. Build of this branch at `70fe07d3d` with `shell/evidence/0715/m1-diag.diff` applied
(diagnostic timers and test switches only, never merged). One launch per arm on c5h-a (product) and c5h-b (stock), the
quiet pair's save, drive and hook. Arms and what each answers:

| Arm | Change from `base` | Answers |
|---|---|---|
| base | the reporter's set, `lockstep_csv=1`, `lockstep_probe=1` | the product's head / E / wait / tail again, now split by the probe (ret -> UV, UV -> first record, -> first execute, -> scene clear, scene clear -> Present, the hook against the call) and with Place's time |
| stockp | stock `config.ini`, every model key 0, composite `mode=passthrough camera=0`, all taps and the unit/model hooks off, the Terrain bit kept (`crashlogs\m1_keep_terrain`) | **stock's own stretches** with the same probe; its frame time checked against `stock` |
| stock | the stock instance | the reference frame time (5.15 repeated) |
| nolandmarks, nocities | one model key 0 | the capture's share of E by its two families (the EXE draws them itself again) |
| nounits, nofarm, nocombat | one key 0 | the unit filter's and the other keys' share |
| slow / spin | server pass + 6 ms (sleep / spin) | does E move when S grows (wait, or contention) |
| light | server forest, water, shadow, decals off | does E move when S shrinks |

Expected `[I]`: stockp's UV -> scene clear about 4-5 ms (stock's frame 5.85 minus a 0.4 ms head and a short tail);
base minus nolandmarks minus nocities near the capture's 4 ms if it lies on E's critical path; slow raising the wait
by about 6 ms and E by nothing (the workstation's r of 1.2).

### 2.1 Results `[L, workstation, 2026-11-23 22:01-22:59 EST; one launch per arm, base twice]`

Inputs: EXE 4390913, C5H-VP8P-HUGE_0350 t350, uncapped, hidden, drive0691 3 repeats; product c5h-a (server.exe
`1e0887eeb55f2376`, proxy `1245b1dfb284017b`, composite `0b8cb1b6d2cd3cfb`, x64 DLL of pin 1b87dd185), stock c5h-b.
**Machine:** the quiet hour held every account-B job, but one `CivilizationV_DX11.exe` of the owner's own logon (pid
44488, not ours, untouched) ran through the whole hour (MC-B #12316); stock ran at 128.5 fps here against 171.0 in 5.15,
so the hour was not as quiet as 5.15's. Output `shell/evidence/0715/arms-an.txt` / `arms-an.json` (`an0715.py`).
Spread between the two identical `base` launches (first and last arm): E mean 7.36 / 6.73 ms, interval 11.99 / 11.49
ms: **single-arm differences under about 0.7 ms are not resolved by this series** `[L]`.

| Arm | hook frame mean (ms) / fps | head | E | wait | tail | interval | S |
|---|---|---|---|---|---|---|---|
| 1 base | 12.00 / 83.3 | 1.68 | 7.36 | 2.45 | 0.49 | 11.99 | 9.29 |
| 2 stockp (stock-like, probe) | 6.95 / 143.9 | (probe below) | | | | | |
| 3 stock | 7.78 / 128.5 | | | | | | |
| 4 slow (server pass +6 ms sleep) | 19.24 / 52.0 | 1.70 | 7.34 | 9.61 | 0.55 | 19.22 | 16.91 |
| 5 light (server forest, water, shadow, decals off) | 14.64 / 68.3 | 2.66 | 9.42 | 2.05 | 0.51 | 14.65 | 10.20 |
| 6 nounits | 10.06 / 99.4 | 1.70 | 7.79 | 0.14 | 0.42 | 10.05 | 5.27 |
| 7 nocities | 15.43 / 64.8 | 2.08 | 10.70 | 2.05 | 0.46 | 15.30 | 11.10 |
| 8 nolandmarks | 12.15 / 82.3 | 1.75 | 7.66 | 2.38 | 0.39 | 12.19 | 9.37 |
| 9 nofarm | 13.26 / 75.4 | 2.08 | 8.77 | 1.90 | 0.51 | 13.27 | 9.68 |
| 10 nocombat | 11.31 / 88.4 | 1.72 | 7.25 | 2.01 | 0.42 | 11.41 | 8.57 |
| 11 spin (server pass +6 ms spin, one core) | 17.75 / 56.3 | 1.52 | 6.54 | 9.24 | 0.47 | 17.79 | 15.75 |
| 12 base (repeat) | 11.46 / 87.2 | 1.63 | 6.73 | 2.66 | 0.46 | 11.49 | 9.00 |

The probe (lockstep-probe.md 1.2), means, ms: `ret->uv` = previous Present returned -> UpdateViews; `uv->rec` -> the
first deferred record; `uv->exec` -> the first ExecuteCommandList; `uv->sc` = UpdateViews -> scene-clear list executed
(**E** in the product); `sc->pres` -> the Present hook's entry (the product's wait and tail); `pres->ret` = the Present
hook and call.

| Arm | ret->uv | uv->rec | uv->exec | uv->sc (E) | sc->pres | pres->ret | Present hook / call (`m1diag`) |
|---|---|---|---|---|---|---|---|
| base (1 / 12) | 1.02 / 0.93 | 5.99 / 5.42 | 7.17 / 6.54 | **7.36 / 6.73** | 2.96 / 3.13 | 0.65 / 0.69 | 0.53 + 0.13 / 0.50 + 0.19 |
| **stockp** | **0.87** | **4.86** | **5.58** | **5.75** | **0.18** | **0.16** | **0.03 + 0.13** |

The composite's counters per present, ms (`modelfilter 1s:` with the diagnostic timers):

| Arm | Unmap copy (all / on the immediate context) | KB | draw path | publication (head) | OnExecuteList (Place) | tapped draws |
|---|---|---|---|---|---|---|
| base 1 / 12 | **2.25 / 1.96** ; **1.97 / 1.69** | 471 / 472 | 1.16 / 1.14 | 0.43 / 0.42 | 0.29 / 0.28 | 412 / 425 |
| nolandmarks | 2.24 / 1.93 | 454 | 0.84 | 0.24 | 0.16 | 119 |
| nocities | 3.00 / 3.00 | 236 | 0.87 | 0.36 | 0.25 | 258 |
| slow / spin | 2.02 / 1.74 ; 1.79 / 1.52 | 466 / 465 | 0.99 / 0.87 | 0.36 / 0.33 | 0.28 / 0.24 | 343 / 325 |

What the arms say:
- **E does not depend on the server** `[L]`: with the server's pass 6-7 ms longer (slow: S 16.9, spin: S 15.8 with one
  core spinning) E stays 7.34 / 6.54 ms, inside the two base launches (7.36 / 6.73); only the wait grows (9.6 / 9.2 ms).
  With the units off (S 5.3 ms, below E) the wait falls to 0.14 ms and the interval to 10.05 ms. On the workstation
  the frame is head + max(E, S) + tail with E fixed: the Architect's model, confirmed by intervention.
- **The Unmap copy runs mostly on the main thread** `[L]`: 1.69-1.96 of its 1.97-2.25 ms per present is on the immediate
  context (the EXE maps the instance buffers on its own thread), so it lies in the main thread's frame, not on the
  recording workers. The copy's KB do not follow the tapped draws: without the landmarks 119 draws remain and still
  454 KB are copied in 2.24 ms (the instance buffers are the cities' and the shared ones) `[L; I for the reason]`.
- **The arms that turn a model key off make the EXE draw that family itself**, so their E is not "the capture's
  share": nocities E 10.70 ms (the EXE draws its cities again), nofarm 8.77; nolandmarks and nocombat stay inside the
  base spread. Read as one launch each with the owner's game beside them `[L numbers; I reading]`.
- **light did not lighten the server** (S 10.20) and E rose to 9.42 ms: not explained; one launch, not used `[L]`.
- **stockp stands in for stock**: its frame (6.95 ms) is shorter than the stock instance's in the same hour (7.78 ms):
  the stock-like arm runs the DLL in the server, stock runs the 32-bit DLL on the EXE's threads `[I for the reason]`.
  So stockp's stretches are a stock-like EXE frame with the DLL's per-frame work out of the EXE, not stock's exactly.

## 3. Step 4: the table (E minus stock's same stretch, and head and tail) `[L rows; I where marked]`

Reference for "stock's same stretch": the stockp arm's probe (section 2.1), the only stock-like EXE frame with
UpdateViews, the scene clear and the Present timed. Product: the mean of the two base launches.

| Stretch | product (base, mean of 2) | stock-like (stockp) | excess |
|---|---|---|---|
| head (ret -> UV + the Present hook and call) | 0.98 + 0.67 = 1.65 | 0.87 + 0.16 = 1.03 | **+0.62** |
| E (UV -> scene clear executed) | 7.05 | 5.75 | **+1.30** |
| tail (scene clear -> Present, without the wait) | 0.48 | 0.18 | **+0.30** |
| EXE side | 9.18 | 6.96 | **+2.22** |
| wait (all presents) | 2.56 | - | +2.56 |

| Named cause, per present | ms | Where | Mark | Share of its stretch's excess |
|---|---|---|---|---|
| Unmap copy of the tapped instance buffers on the main thread (uncached read, 1.6) | **1.82** (1.96 / 1.69) | E | L timer; I that all of it falls between UV and the scene clear | 140 % of E's +1.30 |
| OnExecuteList (Place: the tap's constants scanned per list) | 0.29 | E (lists before the scene clear) and tail | L timer | 22 % |
| what the product takes out of the EXE's frame (terrain, water waves, decals, forest, fog: the patch's `[DisableSystems]` and the view mask; the dropped model and unit draws) | about -0.8 or more | E | I: remainder (E excess minus the two timed rows) | - |
| **E named** | 2.11 timed against +1.30 | | | **> 100 % (with an unmeasured saving)** |
| the Present hook's own work (twelve taps, the unit/model latch, the model publication 0.42) | **0.48** (0.53 - 0.03 / 0.50 - 0.03) | head | L timer | 77 % of head's +0.62 |
| ret -> UpdateViews (0.98 against 0.87) | +0.11 | head | L probe | 17 % |
| the composite's take and draw at the insertion, Place on the 3D lists | +0.30 | tail | I: the stretch itself | 100 % of tail's +0.30 (unsplit) |
| the model draw path on the recording threads | 1.15 thread time | in parallel with the main thread; E only if the recording waits on it | L timer, I placement | not counted |
| the proxy's round trips | at most 0.27 (turn-350 proxy table over the whole run, the load included) | E | I: upper bound | not counted |

**Result: the EXE side's +2.22 ms over the stock-like frame is named to more than two thirds** `[L for the timed rows]`:
the main-thread copy alone (1.82 ms) exceeds E's +1.30 ms, so E also holds savings of at least 0.8 ms from what the
product switches off in the EXE `[I]`; the Present hook's taps and the model publication give 0.48 of the head's +0.62;
the tail's +0.30 is the insertion's own work. The largest single item is the uncached copy: ticket 0719's before is
**1.97-2.25 ms per present (1.69-1.96 on the main thread), 471 KB**, on this build and machine.

Against the quiet pair (5.15): there the product's EXE side was 10.66 ms against stock's whole frame of 5.85 (+4.81). In
this hour the product's EXE side is 9.18 ms and the stock-like frame 6.96 ms. The two differ because the build is newer
(base 12.0 / 11.5 ms against 13.0) and stock's whole frame also holds the 32-bit DLL's work, which the stock-like
arm's server takes off the EXE; the quiet pair's +4.81 therefore over-states the EXE side's excess by stock's DLL work
`[I: stock 7.78 against stockp 6.95 in the same hour]`.

## 4. Not verified

- One launch per arm (base twice: 0.6 ms apart in E); the owner's own game ran beside the hour; grade A needs
  interleaved runs on a quiet machine (PERFORMANCE.md). The direction of each named row rests on its own timer, not
  on arm differences.
- That the immediate-context copy falls between UpdateViews and the scene clear rather than in the head is inferred
  (the head's other parts are timed and small).
- The saving the product makes in the EXE (about -0.8 ms) is a remainder, not measured; the D33 arms of 3207 (stock
  5.38 against D33 + terrain 4.32 ms) are the closest measurement.
- stockp is stock-like, not stock: the DLL runs in the server; its frame is 0.8 ms shorter than stock's in the same hour.
- The light arm's rise and the nocities / nofarm arms' E are not explained beyond one launch each.
- The laptop: only 0691's capped run (1.5); no uncapped laptop arm.

## 5. Ticket 0719: the cached map (the copy reads cached memory) (impl-0719, account B, 2026-11-23/24)

The fix for section 1.6 and section 3's largest row (the Architect's idea 1). Branch `task-0719-cached-map`; evidence and
commands `shell/evidence/0719/README.md`.

### 5.1 What changed `[C: shell/composite/unit_filter.cpp at ff9c8413d]`

The composite's own `Map`/`Unmap` hooks (import-slot hooks, a data path; D228 confirmed by the MC, #12293). With the
optional key `model_map_cache=1` (the default; `[composite]` of `shell_composite.ini` or `VPSHELL_MODEL_MAP_CACHE`, not
in the shipped ini):

- A `Map(WRITE_DISCARD)` of a buffer the copy wants (the interest table: 1.3) that is `D3D11_USAGE_DYNAMIC` and not a
  constant buffer hands the EXE a staging of ordinary memory instead of the driver's pointer: one per (buffer, context),
  `VirtualAlloc(MEM_WRITE_WATCH)`, 64 KB granular, every page touched once and the watch reset at allocation; all
  stagings together at most 64 MB (past it: the direct path, `stage_fail`). Live huge 350: 1.1-1.2 MB of stagings.
- Its `Unmap`, before the real one: (1) forwards to the driver's pointer every page the EXE wrote since the `Map`
  (`GetWriteWatch` with `WRITE_WATCH_FLAG_RESET`), whole 4 KB pages clipped to `ByteWidth`. **What is forwarded:** the
  pages the EXE wrote in this map, whole; a forwarded page's bytes the EXE did not write carry the staging's older bytes
  (after `WRITE_DISCARD` they are undefined in stock too); pages the EXE did not write are not touched, so the driver's
  memory there is what stock has. (2) The copy reads `[0, want)` from the staging (cached), **except the pages the map
  did not write below the draws' high-water mark `need`, which it reads from the driver's memory as before** (`tail_kb`;
  5.3 says why). (3) The record path is unchanged.
- Every other map is the direct path, unchanged: `WRITE_NO_OVERWRITE` and `WRITE` maps (the EXE writes the driver's
  memory itself, so no staging byte is ever written over data the GPU may be using), read maps (never hooked), constant
  buffers (the driver gives them cached memory: 1.6), a full open-map table.
- A cached map's `Unmap` forwards even when the model filter went off between the two calls (`g_cachedOpen`).
- `model_map_cache=0`: every map direct (the path before 0719). `model_map_cache=verify` (TEST ONLY): cached, and every
  forwarded page read back from the driver's pointer and compared (`verify_bad`); pages written into a staging between
  two maps counted (`late_pages`); a direct map whose pointer lies in its staging logged; and every read of a copy into
  a record compared with the direct path's copy of the same Unmap (the driver's memory read after the forward: `rec_kb`,
  `rec_diff`, `rec_noold`, the first 24 differences logged with their page and path).
- Counters on the `modelfilter` lines (before ` progs`): `map_cache`, `map_cached`, `map_direct`, `fwd_kb`, `fwd_ms`
  (inside `copy_ms`, which since 0719 is the hook's whole work: forward, copy and tail), `unwritten_kb` (bytes of
  `[0, want)` in pages the map did not write), `tail_kb`, `stage_kb`, `stage_fail`, `ww_fail`, `verify_kb`,
  `verify_bad`, `late_pages`, `rec_kb`, `rec_diff`, `rec_noold`.

### 5.2 Stand-in and bench `[L]`

- `shell/tests/test_model_filter.py` (unit_filter_host `--map-check`: after each frame's command list ran, the instance,
  ids and palette buffers are copied to a staging buffer and compared with what the host wrote): five map patterns
  (whole; split = `WRITE_DISCARD` of the first half then `WRITE_NO_OVERWRITE` of the rest; noover = initial data and only
  `WRITE_NO_OVERWRITE`; pad = 64 KB written past the copy's mark; def = the deferred context's own `WRITE_DISCARD` maps)
  x the three key values, plus the constant-buffer case and the bad key value. **Final code (ff9c8413d), 2026-11-24
  01:1x: plain 101 passed, ASan 101 passed with no ASan report** (`test_model_filter.py` + `test_unit_filter.py`). Before
  (cd7137c8b) one ASan timeout: def with key 0, the old path, the host logged `exit 0` but its process did not end within
  90 s; passed when rerun alone: an exit-time flake under load.
- **Negative result** `[L, stand-in, 2026-11-23 23:2x]`: a never-touched page of a `MEM_WRITE_WATCH` region counts as
  written when it is first touched, **even by a read** (the demand-zero fault): the copy's read of `[0, want)` past a
  partial map made two pages "written outside a map" once (split case, `late_pages=2`). Harmless after `WRITE_DISCARD`;
  removed by touching every page and resetting the watch at allocation.
- Bench `shell/evidence/0719/cachedmap` (x86, a DYNAMIC vertex buffer, `Map(WRITE_DISCARD)`, the EXE's write plus the
  hook's work, medians of 300; workstation RTX 4080, 2026-11-24 00:0x, under `quietlock.py run` without a hold:
  an indication; oldpc RTX 2080: testbed job 20261124-020710-5909, quiet place, disturbed by foreign load of 1.98 cores):
  `GetWriteWatch` on 64 KB with nothing written 5.2 us / 3.1 us. Outputs `shell/evidence/0719/cachedmap/*-run1.txt`. On
  oldpc the forward of 64-128 KB costs about as much as the write it replaces (the write-combined write is the slow part
  there), so the saving is the read alone; on both machines the saving grows with the buffer `[L]`.

| Buffer, written | workstation direct: write + read (ms) | workstation cached: write + watch + forward + read (ms) | oldpc direct | oldpc cached |
|---|---|---|---|---|
| 64 KB, all | 0.032 + 0.515 = 0.547 | 0.027 + 0.020 + 0.031 + 0.004 = 0.082 | 0.292 | 0.185 |
| 128 KB, all | 0.053 + 0.654 = 0.707 | 0.055 + 0.022 + 0.055 + 0.007 = 0.139 | 0.566 | 0.353 |
| 256 KB, all | 0.019 + 1.158 = 1.178 | 0.105 + 0.023 + 0.020 + 0.011 = 0.159 | 0.553 | 0.119 |
| 512 KB, all | 0.064 + 3.264 = 3.328 | 0.298 + 0.043 + 0.050 + 0.045 = 0.437 | 1.130 | 0.240 |
| 512 KB, 1/8 written | 0.009 + 6.385 = 6.395 | 0.042 + 0.048 + 0.005 + 0.126 = 0.221 | 1.124 | 0.059 |

### 5.3 Live: the verify launches on c5h-c `[L, workstation, 2026-11-24, EXE 4390913, C5H-VP8P-HUGE_0350, uncapped, hidden, keys + drags + 30 s wide; shell/evidence/0719/ab0719_ws.sh ARMS=verify]`

- verify2 (00:11, b6a664148's build): 1,046,476 KB forwarded in 13,195 cached maps (4.4 per present), **verify_bad 0**,
  late_pages 0; 3.86 M direct maps (the constant buffers, about 1,330 per present).
- verify3/4 (00:2x-00:37, the record check added): **rec_diff 247 / 465** reads out of about 0.8 GB of record reads.
  Every logged one: one deferred context (slot 8), two buffers in turn, a read of `[175104, 176640)` where the last
  map wrote 43 pages (`[0, 176128)`): **the EXE's draws read up to 512 bytes past what the map wrote**; the GPU reads
  the driver's older bytes there (in stock too: those pages are neither written by the EXE nor forwarded), the
  staging held the context's previous map's bytes `[L]`. Whether those instances are visible in the EXE's picture is
  not known `[I: older floats on both sides, e.g. BEA2F222 against BF74D458]`.
- Fix (ff9c8413d): pages the map did not write below `need` are read from the driver as before. verify5 (00:43):
  **850,312 KB forwarded, verify_bad 0; 870,159 KB of record reads, rec_diff 0**, late_pages 0; tail 166,629 KB against
  1.67 GB copied (about 8 % of the copy's bytes still read uncached; unwritten bytes in `[0, want)` 284,354 KB).

### 5.4 Live: oldpc arm pair `[L, oldpc RTX 2080, 2026-11-24 00:25-00:47, testbed quiet place, TB-HUGE_0350, 30 fps cap with vsync, keys + drag, one launch per arm; jobs 20261124-002519-0889 (key 0) and 20261124-003148-0e88 (key 1)]`

The laptop was down (MC #12394). Composite of 6a2576896 (before the tail read of 5.3; the tail adds about 8 % of the
copy's bytes at oldpc's 560 MB/s, about 0.06 ms `[I]`). Windows = the recordings' spans (frames.csv).

| Per present | key 0 (direct) | key 1 (cached) |
|---|---|---|
| copy at Unmap (ms) / KB | 0.773 / 435 | 0.189 (forward 0.036) / 436 |
| E (UpdateViews -> insertion), mean (ms) | 5.78 | 5.22 |
| interval (ms) | 33.40 (cap) | 33.40 (cap) |
| cached / direct maps | 0 / 821 | 3.05 / 817 |

One launch per arm under the cap: an indication (grade B at best), not a grade-A figure. E fell by 0.56 ms, the copy by
0.58 ms.

### 5.5 The grade-A series on the laptop `[L, laptop, 2026-11-24 12:45-13:51 EST; impl-0719s]`

*(Rewritten 2026-11-24 14:0x by impl-0719s; it was "5.5 The grade-A series: not run (follow-up)": the workstation
series never started, the oldpc series of 02:07-02:16 ended rc 3 at launch on oldpc's commit floor, all 12 jobs. Those
two attempts stand as history below.)*

**Result.** The copy at Unmap falls by 1.04 ms per present (-43 %), confirmed. E does not fall by a detectable amount,
and neither does the frame interval. The 1 ms saved on the EXE's side goes into the composite's wait for the server's
frame (+0.90 ms): on the laptop an exact-lockstep frame is bound by the server, so a faster EXE only waits longer.
**Owed bar:** the copy's ms per present before and after is met at grade A on the laptop. E before and after was
measured, but the improvement is not shown there. The acceptance's quiet-workstation interleaved arm is still not
measured (5.6).

**Set-up.** One interleaved set of 12 launches, ABBA x 3 (`0 1 1 0 0 1 1 0 0 1 1 0`, the key
`VPSHELL_MODEL_MAP_CACHE`), on the laptop's quiet place: each launch is its own `--quiet --headed` capture_job.py
product job (`--cases keys,drag --repeats 1`: 4 key and 4 drag recordings). Save TB-HUGE_0350 t350; the composite's
lockstep set of the job (`lockstep_mode=exact`, `lockstep_csv=1`, `pacing=1`); the reporter's `[Renderer]` set; no
frame cap (interval about 42 ms). Machine: i7-7700HQ, the server on the Quadro P3000 (render.log `using adapter`), the
display on Intel HD 630 (dupcap); which adapter the EXE draws on was not read. Builds of main `0f7465dd2` (0719
included), made by this session: server.exe `bce5160b...`, x64 DLL `b84aa3e9...`, proxy `5728f805...`, composite
`0d3704bd...`; the EXE `ba0c491b...` (the laptop's tb1, build 4390913). The probe `20261124-124354-61b4` passed before
the set. Jobs (in arm order): `20261124-124507-38fb` (cache0-1), `-124558-8d5b` (1-2), `-124636-43f0` (1-3),
`-124717-e2fa` (0-4), `-124759-409f` (0-5), `-124845-49aa` (1-6), `-125017-f633` (1-7), `-125100-bb69` (0-8),
`-125142-e7a5` (0-9), `-125225-f600` (1-10), `-125306-55bf` (1-11), `-125508-62b6` (0-12). All 12 ended rc 0, and none
was refused or retried. Windows: the recordings' spans (frames.csv) -2 s / +1 s, 1,605-1,684 presents per launch.
Counters in every launch: `verify_bad`, `stage_fail`, `ww_fail` and `late_pages` all 0; the cached launches 3.02-3.03
cached maps and about 746 direct maps per present.

**Load.** Every job's quiet proof says `alone=True undisturbed=False`. Foreign load was 0.24-0.52 cores against the
0.1 limit (cache0 mean 0.305, cache1 0.373), and Defender (`MsMpEng.exe`, 29-72 CPU s per job) was the top foreign
process every time. Next came `sshd`/`python` from the submissions and fetches (the first 3 jobs overlapped the
remaining submissions until 12:56), then `audiodg` and `SearchIndexer`. The machine's own CPU averaged 40-50 %, and its
lowest available memory was 2.5-4.0 GB. Defender's CPU was higher in the cache1 launches of blocks 1-2 (70 / 65 s
against 51 / 41 s) and equal in block 3 (33 / 32 s); whether the arm causes it is not known `[I]`. E rises about 11 ms
per foreign core across launches, so the table also gives the arm effect with the foreign load taken out (OLS).

| Per present (ms), launch means | cache0 (n=6) | cache1 (n=6) | cache1 - cache0, Welch 95 % | detectable | load-adjusted arm effect, 95 % | per ABBA block |
|---|---|---|---|---|---|---|
| copy at Unmap (forward included) | 2.444 (sd 0.101) | 1.399 (sd 0.077; forward 0.132) | **-1.044 [-1.162, -0.927]** | 0.23 | -1.074 [-1.196, -0.952] | -1.00 / -1.03 / -1.10 |
| E (UpdateViews -> insertion), mean | 17.397 (sd 0.711) | 17.937 (sd 1.402) | +0.540 [-0.978, +2.058] | 3.04 | -0.230 [-1.024, +0.565] | +1.38 / +1.15 / -0.91 |
| E, p50 | 14.334 | 14.103 | -0.231 [-1.121, +0.658] | 1.78 | | |
| head | 13.714 | 13.353 | -0.361 [-1.197, +0.475] | 1.67 | -0.063 [-0.745, +0.619] | -0.37 / -1.17 / +0.46 |
| composite wait (wait1 - wait0) | 7.637 | 8.533 | **+0.897 [+0.446, +1.347]** | | +0.720 [+0.340, +1.101] | +0.90 / +1.15 / +0.65 |
| tail (present - insertion - wait) | 3.165 | 3.033 | -0.132 [-0.567, +0.304] | | -0.065 [-0.548, +0.419] | |
| interval | 41.912 (sd 0.560) | 42.856 (sd 0.943) | +0.944 [-0.089, +1.976] | 2.07 | +0.363 [+0.002, +0.724] | +1.50 / +1.06 / +0.27 |
| server ready - insertion | 5.639 | 6.154 | **+0.515 [+0.160, +0.870]** | | +0.546 [+0.175, +0.918] | +0.39 / +0.63 / +0.52 |
| server pass (ready - wake) | 17.537 | 18.945 | +1.409 [-0.587, +3.405] | | +0.602 [-0.961, +2.165] | +2.12 / +2.79 / -0.69 |
| server busy per drawn frame (render.log `pass 1s`) | 27.629 | 28.386 | +0.757 [-1.087, +2.601] | | | +1.34 / +1.83 / -0.90 |
| model capture draw / publish | 1.249 / 0.936 | 1.263 / 0.952 | +0.014 / +0.016 (within +-0.11) | | | |
| copied / forwarded KB | 411.3 / 0 | 411.5 / 115.7 | | | | |

**Why the numbers moved.**
- **The copy** `[L]`. In the cached arm, about 3 maps per present (the instance, ids and palette buffers) are read from
  the staging. The rest of `[0, want)` is still read from the driver's memory: about 110 KB of unwritten pages per
  present (`unwritten_kb`, the 5.3 tail read). That is why the copy keeps 1.27 ms of reads plus a forward of 0.13 ms
  for 116 KB, against oldpc's 0.19 ms (5.4, the build before the tail read). On the laptop's mapped memory the leftover
  uncached read is the larger part `[I: the tail's share of copy_ms was not timed separately]`.
- **E and the wait** `[L numbers, I reading]`. In exact lockstep the EXE waits at the insertion for the server's frame
  of its camera. On the laptop that frame is ready later than the EXE's own work. Per launch (split0689),
  ready - publish has a p50 of 19.7-28.3 ms against insertion - publish at 12.6-17.4 ms (all 12 launches, moving and
  still). So a
  shorter Unmap moves the insertion earlier and lengthens the wait by the same amount, and the interval stays bound by
  the server. Block 3 shows it with Defender's load equal in both arms: E -0.91, wait +0.65, interval +0.27. In blocks
  1-2 the cache1 launches carried more foreign load, and the server's pass rose there in every stage (units +0.5-0.7,
  predraw +0.4, draw +0.1-0.2 ms per frame) with E: a machine-wide slowdown, not a composite cost `[I]`. The
  load-adjusted E effect (-0.23, interval [-1.02, +0.57]) does not exclude the full -1.0 ms reaching E, and does not
  show it either.
- **The server ready - insertion +0.5 ms** and the wait +0.7-0.9 ms are significant even after the load adjustment.
  The arm makes the EXE wait longer for the server; that is the lockstep reading above. A cost the arm puts on the
  server is not excluded: srv_cpu +0.05 ms per frame [-0.00, +0.10] `[L]`, and the stage table shows no stage that
  rises in all three blocks.

**On the laptop's product path, the cached map does not make frames faster** at this set's power: interval detectable
about 2 ms, the load-adjusted interval +0.36 [+0.00, +0.72]. Its saving is on the EXE's side and would show where the
EXE, not the server, sets the frame (the workstation, 1.2: E does not move with S there) `[I]`.

Commands (Git Bash, a checkout whose HEAD has `oldpc_series0719.sh` with `HOST`; builds of 1.2/1.5 of the runbook,
`tools/harness/native/build.bat` and `tools/capture/build.cmd` first; a SHIP folder as below):
```
HOST=laptop AFTER_NAME=now BENCH_JOB=0 SHIP=<ship: bin/ (no PDBs), proxy/CvGameCore_Expansion2.dll, composite/shell_composite.dll> OUT=<fetch root> bash shell/evidence/0719/oldpc_series0719.sh   # background; 12 jobs, about 5-6 min each, 67 min in all
python -I -X utf8 shell/evidence/0719/ab0719.py --run cache<a>-<n>=<OUT>/<job>/out/shell_composite.lockstep.csv=<OUT>/<job>/out/shell_composite.log=<OUT>/<job>/out/cap ... --json <f>
python -I -X utf8 shell/evidence/0719/split0719.py --run <the same arguments> --json <OUT>/split0719.json   # wait, tail, server ready / pass per launch
tb.py --host laptop fetch <job> --out <OUT>/<job> --max-mb 0 --include out/render.log                      # per job, for the stage table
python -I -X utf8 server/evidence/0689/split0689.py --log <OUT>/<job>/out/render.log --csv <OUT>/<job>/out/shell_composite.lockstep.csv --marks <OUT>/marks.tsv --tag <label>. --json <OUT>/s0689-<label>.json
python -I -X utf8 shell/evidence/0719/series0719.py <OUT> --json <f>                                         # load-adjusted arm effect, blocks, stages
```
`marks.tsv` columns are `tag series t0_qpc t1_qpc`, one row per recording, with tag `<label>.`; split0689 matches the
tag as a prefix, so without the dot `cache0-1` would also match `cache0-12`. The outputs of this set are in
`shell/evidence/0719/laptop-series-20261124.txt`. Fetched files were kept until the hand-back in
`C:/c5-64bit-work/0719-series/laptop-series` (deleted at the hand-back).

History (the earlier attempts):
- Workstation (c5h-c, 2026-11-24 00:50-01:10): never started: the rule A run_alls of 0704, then 0707 and 0712 kept the
  machine busy (nine heavy jobs registered at 01:10), so no quiet hold fitted before c5h-c's end (02:30).
- oldpc (MC-B's choice, #12505): `shell/evidence/0719/oldpc_series0719.sh` submitted the bench and 12 product jobs (ABBA
  x 3) right after 0712's series (02:07-02:16); **all 12 ended rc 3 at launch**: `launch gate: free commit 5.7 GB (floor
  6 GB) for game instance tb1` (job 20261124-021252-7d5e). No game ran; each job restored by hash. oldpc's free commit
  stayed below the game floor from about 02:07 (reported to the MC by MC-B; the machine is the owner's, nothing changed).

### 5.6 Not verified

- **The workstation's grade-A before/after is not measured.** The laptop set (5.5, 2026-11-24) is grade A for the copy
  (-1.04 ms per present). It shows no E or interval gain there, because the laptop's frames are server-bound. Every job's
  quiet proof says disturbed (Defender, 0.24-0.52 foreign cores). Whether E falls where the EXE sets the frame needs the
  acceptance's quiet-workstation interleaved arm, in a window the MC schedules. *(Corrected 2026-11-24 by impl-0719s;
  was "No grade-A timing (5.5): ... rest on one launch per arm on oldpc ...")*
- Why Defender's load was higher in the cache1 launches of blocks 1-2, and how much of the copy's remaining 1.27 ms is
  the tail read of unwritten pages on the laptop (5.5).
- Ledger run records (`c5-run/1`) were not written for the laptop set.
- Rule A (offline equals live) on this branch: not run before the hand-back (MC-B's rule A queue); the offline bundles do
  not run the composite's capture path `[I]`.

- Whether the 512-byte over-read of 5.3 shows in the EXE's picture.
- The tail read bounds the records to the direct path's bytes only below `need`; a draw reading between `need` and
  `want` on the frame it first does so reads the staging's older bytes there (it raises `need` for the next frame).

## 6. M2 (ticket 0754): M1's table again on beta 4 and EXE 26577625, and the laptop's split, uncapped (impl-0754, account B, 2026-11-25)

This is the Architect's second ticket beside J3 (`research/07-architecture-decisions/architect-2026-11-25-frame-gap-route.md`
section 6). It is a measurement only, with no product code. Commands and scripts: `shell/evidence/0754/README.md`. Outputs:
`shell/evidence/0754/ws-an.txt` (the M1 table, per launch), `ws-an0748.txt` (0748's drag windows on this series),
`laptop-an.txt` (the laptop's split). Marks: `[L]` measured, `[I]` inferred.

**Result [L for the rows; I where marked].**
- **Workstation, moving** (3 launches per arm, launch means, Welch 95 %): the product's frame is 9.36 ms (107 fps)
  against the stock-like frame's 6.38 ms with the same probe (stock's own is 6.00 ms). The gap is **+2.98 [+-0.85] ms =
  the lockstep wait 2.05 [+-0.61] + the EXE side +0.83 [+-0.59]** (head +0.48 [+-0.16], E +0.10 [+-0.46], tail +0.25
  [+-0.04]). So about 70 % of the moving gap is the wait for the server, and 28 % is the EXE side.
- **Workstation, at rest** (30 s still, no input): 8.33 against 5.79 ms, **+2.54 [+-0.21] = wait 1.03 [+-0.50] + EXE
  side +1.48 [+-0.37]** (head +0.52 [+-0.13], E +0.77 [+-0.41], tail +0.19 [+-0.02]). At rest the EXE side is the
  larger half.
- **Against M1** (EXE 4390913, before 0719): the EXE side's excess fell from +2.22 ms to +0.83 ms (moving) and +1.48 ms
  (rest), and the wait fell from 2.56 ms to 2.05 / 1.03 ms. The copy at Unmap fell from 1.97-2.25 ms to 0.69 (moving) /
  1.04 (rest) ms per present: that is 0719's cached map (5.5).
- **The EXE side's excess is named to more than two thirds** `[L timers; I placement]`.
  - Moving: the publication in the Present hook is 0.37 ms. It is the whole of the hook's own work: the probe's
    `pres->ret` is 0.59 against 0.23 ms. Add return to UpdateViews +0.12 and the composite's take and draw in the tail
    +0.25: 0.74 of +0.83 ms (89 %). The copy (0.69 ms) is inside E. There it is offset by what the product switches off
    in the EXE, so E is only +0.10.
  - At rest: publication 0.31 + return to UpdateViews +0.18 + tail +0.19 + copy 1.04 = 1.72 against +1.48 ms. That is
    over 100 %, with the same savings.
- **E does not depend on the server's load** `[L]`. Per launch, E and S, the head, the copy's ms and the server's units
  job all move together (Pearson over 6 product launches, with 0748's p2/p4/p6: r(E, head) 1.00, r(E, copy_ms) 0.98,
  r(E, job_ms) 0.96, r(E, S) 0.88). The copied KB stay at 461 in every launch (r -0.11), and the tapped draws vary by
  7 % (r 0.58). Within a launch, E against the tapped draws per second is r -0.34 to 0.03 while moving.
- **E's launch-to-launch spread (1.7 ms in 0748) is a machine-wide speed state, not the EXE's load** `[L numbers; I
  cause]`.
  - In this held hour the product's E spread is 0.30 ms (5.11 / 5.21 / 5.41 moving; sd 0.15) and the stock-like arm's
    is 0.38 ms. **Stock's own frame spread is 0.87 ms** (s1 6.46, s5 5.59, s9 5.94 ms moving).
  - 0748's three product launches spread 1.40 ms in E (4.38 to 5.78) under disturbed holds (`--overlap`).
  - In those launches the work of both processes (E, head, copy, the server's job) scaled together at the same copied
    KB, and the stock arm moves as much. So the spread comes from how fast the machine runs that launch: other load,
    clocks and P/E-core placement on the i9-13900KS.
  - Which of those it is was not separated. Per-thread core placement is not readable without ETW, and the sampler gives
    only the busy cores per kind (`research/08-performance/cpu-time-and-affinity.md` 1-2: an E-core runs the same work
    about 1.9x slower).
  - Bound: 0.30 ms (product) to 0.87 ms (stock) in a held hour, 1.4 ms in a disturbed one.
- **Laptop at rest, uncapped, vsync off, with the pack's purity.tsv: the gap is all wait, and the EXE side is below
  stock's whole frame.** Product: 3 launches per save. Stock TB-HUGE_0350: 3 launches in this series. Stock
  TS0438-H-T300: 5043's 6 launches of the same morning (`research/03-dll-engine-boundary/proxy-design.md` 37.5).
  - TB-HUGE_0350: stock 12.71 ms, product 15.63 ms, gap +2.93 [+-0.35] = wait 3.58 [+-0.42] + EXE side -0.65 [+-0.33].
  - TS0438-H-T300 (5043's view): stock 16.45 ms, product 22.04 ms, gap +5.60 [+-0.76] = wait 7.05 [+-0.53] + EXE side
    -1.46 [+-0.21].
  - The product waits in 98 to 100 % of presents. S is 11.53 / 18.15 ms against E 8.05 / 11.10 ms.
  - This confirms the Architect's reading of 2026-11-25 (section 3 of that note): on the laptop a saving on the EXE's
    side goes into the wait.
  - The server per drawn frame: units 3.95 / 7.17 ms is the largest stage, then draw 1.92 / 1.98 and predraw
    1.59 / 2.06 ms.

### 6.1 Method `[C: the scripts; L: the runs]`

- **Workstation** (`ws0754.sh`):
  - Instance and run conditions: c5h-new-b, EXE 26577625, mods548, hidden, `launch --timing` (uncapped), and
    `GraphicsSettingsDX11.ini WaitForVSync = 0` (backed up and restored by SHA-256).
  - Save: a copy of C5H-VP8P-HUGE_0350 (T350, the load's view at the capital) under its own name.
  - Binaries: beta 4's cand6 layout as 0748 laid it (server 6c75d7fa, x64 DLL 4c5da20e, proxy 48524398, composite
    fc01cafa), the owner's VP64Options.ini copy, and the pack's `purity.tsv` beside the x64 DLL. Every product and
    stock-like launch logs `purity: loaded (pack=0) ... 140 method keys, 29 Lua names; bound to the x64 DLL 4c5da20e`.
  - Arm `stock`: the instance's own 32-bit VP DLL.
  - Arm `stockp` (stock-like with the probe):
    - The product layout, the stock config.ini (prep's backup) and `[Renderer] ServerRenderer = 0`.
    - Beta 4's composite is loaded by the test switch `crashlogs\shell.helper` from a copy outside the MODS tree. Its ini
      is at `mode=passthrough camera=0`, with every tap and the unit/model hooks off and `lockstep_probe=1`.
    - With ServerRenderer = 0 the proxy writes no view mask, so the EXE draws everything, terrain included
      (`proxy_shell.cpp` OnBoot at dc259f0: the Terrain bit is cleared only with option 1). The server runs the DLL and
      renders nothing (0.08 cores).
    - **This replaces M1's arm, which needed a diagnostic proxy** (`m1-diag.diff`, `crashlogs\m1_keep_terrain`) and kept
      ServerRenderer = 1.
    - Every stockp launch logs `ServerRenderer = 0: no composite, no view-mask write, no render keys (the test switch
      crashlogs\shell.helper is honoured at the boot)`.
    - The proxy also writes `crashlogs\shell.log` (one line per load). It was moved out after the series.
  - Arm `product`: the composite ini with `lockstep_csv=1 lockstep_probe=1`, and the TEST ONLY server.cfg
    `render_pass_long_ms=8`.
- **Per launch**:
  - The game's own per-frame Lua hook (`ft_hook.lua`) runs over `ws0748.py`'s set-up and 10 fast drags (with the camera
    sampler `cam0748.py`), then over 30 s still.
  - `cpu0754.py` runs beside it: busy cores per core kind, the EXE's and the server's CPU and their hottest threads, and
    from p4 on also the five busiest other processes.
  - Windows: the drags run from the first posted move - 2 s to the last release + 1 s; the still window is `still.tsv`.
  - `an0754.py` reads the lockstep CSV (head / E / wait / tail, as defined at the top of this file), the probe
    (`ret->uv`, `uv->sc` = E, `sc->pres`, `pres->ret`), the composite's `modelfilter 1s:` counters and render.log's
    `units 1s` / `pass long:` lines.
- **Order and machine** `[L]`:
  - Latin square s1 k2 p3 p4 s5 k6 k7 p8 s9, each launch under its own quietlock take, in MC-B's quiet hour (#14592).
  - s1, k2 and p3 (09:51-10:09) had account A's 0729 timing series takes around them, never during a take.
  - p4 to s9 (10:18-10:52) ran back to back after 0729 paused (MC #14649).
  - No heavy job was registered in any take.
  - Other load on the machine in every launch, by the sampler (p4 on): python.exe 1.9-2.4 cores (in all arms the camera
    sampler reads every 0.5 ms, plus the harness and other sessions' pollers), dwm 0.7-0.9, MedalEncoder 0.4-0.5,
    SearchIndexer 0.8-1.1 (p4, p8), Defender 1.1-1.2 (p8).
  - With 3 launches an arm this is a workstation indication, not grade A (PERFORMANCE.md).
- **Laptop** (`job0754.py`, `series0754.sh`):
  - `job0754.py` is 5043's `job5043.py` with three changes: the save is chosen, `lockstep_csv=1 lockstep_probe=1` are
    set, and the composite's files are copied out.
  - 9 quiet headed jobs on tb1, 09:14-10:09, interleaved: the product (arm `idle`: no input) and stock at TB-HUGE_0350's
    load view, and the product at TS0438-H-T300's dense view. Each job has a 60 s idle window.
  - The pack's [Renderer] and [Performance] sets. EXE 4390913.
  - All jobs ended rc 0 and were restored by hash. Every quiet proof says `alone=True undisturbed=False` (Defender,
    MemCompression; 0.16-0.19 foreign cores).

### 6.2 The workstation table `[L: launch means, n = 3 an arm; Welch 95 %]`

| Moving (the drags) | product | stock-like (probe) | product - stock-like |
|---|---|---|---|
| frame (the hook) | 9.36 | 6.38 | **+2.98 [+-0.85]** |
| head (`ret->uv` + `pres->ret`) | 0.85 + 0.59 = 1.44 | 0.73 + 0.23 = 0.96 | +0.48 [+-0.16] |
| E (UpdateViews -> scene clear executed; the CSV's E is 5.24) | 5.25 | 5.15 | +0.10 [+-0.46] |
| tail, the wait out (CSV / `sc->pres`) | 0.41 | 0.16 | +0.25 [+-0.04] |
| EXE side | 7.10 | 6.27 | **+0.83 [+-0.59]** |
| lockstep wait (all presents; 80 % waited) | 2.05 | - | **+2.05 [+-0.61]** |
| S (the server's frame taken) | 7.05 | - | |
| stock's own frame | 6.00 (stock - stock-like -0.38 [+-0.98]) | | |

| At rest (30 s still) | product | stock-like | difference |
|---|---|---|---|
| frame | 8.33 | 5.79 | **+2.54 [+-0.21]** |
| head | 0.80 + 0.54 = 1.34 | 0.62 + 0.20 = 0.82 | +0.52 [+-0.13] |
| E | 5.57 | 4.80 | +0.77 [+-0.41] |
| tail | 0.34 | 0.15 | +0.19 [+-0.02] |
| EXE side | 7.26 | 5.77 | **+1.48 [+-0.37]** |
| wait (62 % waited) | 1.03 | - | +1.03 [+-0.50] |
| S | 6.24 | | |

The composite's counters per present after 0719 (moving / rest):
- copy at Unmap 0.69 / 1.04 ms (the forward's 0.05 ms included);
- 461 / 461 KB copied, and a tail read of unwritten pages of 41 / 86 KB;
- the model draw path on the recording threads 0.99 / 0.75 ms;
- the publication (head) 0.37 / 0.31 ms;
- tapped draws 362 / 266.

Not timed on this build: `Place` (OnExecuteList) and the copy's share on the immediate context. M1's diagnostic timers
are not in beta 4. M1 had 0.29 ms and 87 % for them `[L, M1]`.

0748's drag windows on this series (`ws-an0748.txt`, later drags, n = 27 an arm):
- new pictures a second in W1: stock 161.6 against product 109.6, **-52.0 [-59.7, -44.3]**; in W3 -49.7
  [-56.7, -42.7];
- longest gap +4.85 [+3.78, +5.91] ms;
- mean gap 6.29 against 9.41 ms.

This repeats 0748's result (-53.6 there) in a held hour.

**0753's "before" (J3, record ahead)** `[L, product arm p3 p4 p8]`, moving / rest:
- `units 1s` job_ms 2.85 / 2.75;
- mean join wait 2.17 / 2.10 ms (max 5.8-9.4 / 4.7);
- the share of presents that wait for the server 80 % (75-84) / 62 % (54-70);
- `pass long` lines (8 ms) 14.2 / 10.1 a second, top stage units (472-548 of 750-830 moving; 266-343 of 275-360 at
  rest), then idle;
- new pictures a second 109.6 (W1).

J3's step-0 bar (a mean join wait of at least 1.0 ms on the workstation) is met. The launch folders (lockstep CSV,
render.log, composite log, probe CSV) are kept in `C:/c5-64bit-work/keep/0754-before/` for 0753.

### 6.3 Route (b) candidates: the EXE-side rows on this build `[L rows; I the reading]`

Two rows reach 0.3 ms, the copy and the publication. Two smaller rows follow for completeness.

| Row | ms per present (moving / rest) | Where | Note |
|---|---|---|---|
| the copy at Unmap, after 0719 | 0.69 / 1.04 | E (main thread, M1: 87 %) | mostly the 461 KB read and the uncached tail read of pages the map did not write (41 / 86 KB); larger at rest |
| the model publication in the Present hook | 0.37 / 0.31 | head | the whole of the hook's own work |
| the composite's take and draw at the insertion | 0.25 / 0.19 | tail | below 0.3 |
| return -> UpdateViews | 0.12 / 0.18 | head | below 0.3 |

On the laptop no EXE-side row shortens the frame while S is over E (6, 5.5). On the workstation, route (a) (J3) addresses
the larger half while moving: the wait, 2.05 ms. At rest the EXE side is the larger half (1.48 against 1.03 ms).

### 6.4 Not verified

- Workstation: 3 launches an arm in one hour, with up to about 5 cores of other load (the camera sampler included) in
  every arm. This is an indication, not grade A. s1, k2 and p3 had 0729's takes around them.
- The stock-like arm differs from M1's (ServerRenderer = 0, no server rendering). Its frame equals stock's within the
  interval (-0.38 [+-0.98] ms).
- `Place` and the copy's split by context were not timed on beta 4 (diagnostic timers only). Their placement in E uses
  M1's shares.
- What makes a launch fast or slow on the workstation (clocks, P/E placement, other load) was not separated. Per-thread
  core placement needs ETW or affinity runs.
- The laptop's TS0438-H-T300 stock reference is 5043's jobs (the same morning, the same machine and layout, a 30 s idle
  window), not this series.
- No screenshots were taken. The windows are hidden on the workstation, and the laptop jobs record no pictures.

## 7. Ticket 0757: taking the copy at Unmap off the critical path, same bytes (design, step 1) (impl-0757, account B, 2026-11-25)

Step 1 of 0757 is a design with no product code: the routes that keep the captured bytes identical while shortening or
hiding the copy, each with its cost, its risk to D242's exact lockstep and the measurement that proves it. Two things
were measured for it: the copy's parts, from 0754's own logs (no game), and a bench of the read. Evidence and commands:
`shell/evidence/0757/README.md`. Marks: `[C]` read in the code (`shell/composite/unit_filter.cpp` at 50357b16a), `[L]`
measured, `[I]` inferred.

**Result.**
- **The tail read is most of what is left of the copy** `[L]`. 0719's read of the pages a cached map did not write,
  below the draws' mark, still comes from the driver's write-combined memory. It costs about 7.9 us per KB (129 MB/s)
  in 0754's launches: **0.42 of the 0.77 ms mean, 0.68 of the 1.03 ms in the seconds with an 86 KB tail.**
- **A streaming-load read (SSE4.1 `MOVNTDQA`, AVX2 `VMOVNTDQA`) reads the same memory 5-30x faster than `memcpy`, with
  the same bytes** `[L, bench]`.
  - On the RTX 4080: 2.4-8.3 GB/s against memcpy's 0.15-0.63 GB/s.
  - On the laptop's Quadro P3000: 5.1-11.6 GB/s against 0.37-0.39 GB/s.
  - Every round of every method compared equal with memcpy's bytes of the same map.
  - An 84 KB tail takes 8-30 us instead of 150-225 us in the bench, and about 0.68 ms live.
- **Recommendation to the MC: route A**, the streaming-load read of the tail runs, done in place. It moves no work to
  another thread, changes no order and no byte, and stays inside `OnUnmap`.
  - Expected `[I]`: the copy falls from 0.69 / 1.04 ms to about 0.37 / 0.39 ms per present (moving / at rest) on the
    workstation.
  - Proof: a TEST ONLY compare of every tail run, then an ABBA series on the workstation.
- Route B (a worker reads the tail while the map is open) also takes the read off the main thread in the bench, but only
  if A is not enough. The other routes the ticket names are rejected (7.3).
- As 0719 showed on the laptop, a saving on the EXE's side shortens the frame only where the EXE sets it. On the
  workstation that is the 38 % of presents at rest that did not wait for the server, and the 20 % moving (0754's
  waited share) `[I]`. A change ticket follows only on the MC's word, after J3 (0753) step 0.

### 7.1 What the copy is made of after 0719 `[C; L: 0754's counters]`

`OnUnmap` (one call per `Unmap` of a buffer in the interest table) does, in order:
1. For a cached map: the forward of the written pages (`Forward`, `fwd_ms`).
2. `memcpy` of `[0, want)` into the shadow, from the staging (cached map) or from the driver's pointer (direct map).
3. For a cached map: the **tail**. It copies the pages of `[0, min(need, want))` that the map did not write, in page
   runs, from the driver's pointer (the map's `real`) into the shadow (`tail_kb`). These are the bytes the GPU reads
   there: the EXE's draws read up to 512 B past what a map wrote (5.3).
4. Counters. `copy_ms` times all of it.

`split0757.py` fits `copy_ms` per present over every second of 0754's three kept product launches (p3, p4, p8:
416 seconds, R² 0.90). Output: `shell/evidence/0757/split-0754.txt`.

| Part, per present | coefficient [95 %] | at the mean (ms) | seconds with tail < 60 KB | tail >= 60 KB |
|---|---|---|---|---|
| tail read (uncached) | 7.94 [+-0.28] us per KB (129 MB/s) | **0.415** (52 KB) | 7.7 KB | 85.7 KB |
| direct maps (881 a present: the constant buffers, cached; about 250 B each) | 0.22 [+-0.17] us per map | 0.19 | 872 | 888 |
| bytes read from cached memory (stagings, constant buffers) | 0.39 [+-0.33] us per KB | 0.15 | | |
| constant | 0.01 [+-0.08] ms | 0.01 | | |
| **copy_ms** | | **0.77** | **0.42** | **1.03** |

- The tail is bimodal. About 8 KB in the seconds with more tapped draws (343 a present: the drags and the wide view),
  about 86 KB in the quieter ones (267: at rest) `[L]`. The mark `need` is a high-water mark over the run (it only
  grows, `InterestNeed`). At rest the EXE writes fewer instances than the busiest view did, so more pages below `need`
  stay unwritten and are read uncached `[C, I]`. 0754's "moving 41 KB / rest 86 KB" are window means over this mix.
- The 129 MB/s live is the SSE2 16-byte-load rate of the bench (130-190 MB/s), not the bench's memcpy rate
  (150-630 MB/s, 64-byte-aligned destination). The shadow's `realloc`'d destination is 8-byte aligned on x86. That
  the CRT's memcpy then takes its 16-byte-load path is a hypothesis, not checked `[I]`.
- The forward (0.05 ms) and the constant buffers' direct maps are cached work. They are not the uncached problem.
  The direct maps' cost is mostly per call: two `QpcNow`, `ShadowMake`'s probe, two or three interlocked adds `[I]`.
  `OnMap`'s own work (`InterestCopyBytes`, and `CacheableBytes`' `GetType` / `GetDesc` for each `WRITE_DISCARD`) is
  outside `copy_ms` and was never timed `[C]`.

### 7.2 Bench `tailread` (2026-11-25) `[L]`

`shell/evidence/0757/tailread/tailread.cpp` is x86 and D3D11. A `DYNAMIC` vertex buffer is mapped `WRITE_DISCARD`, its
first half is written (the EXE's part), then `[half, half + tail)` is read: the pages the map did not write, as the tail
does. Median of 300 or 600 rounds. Every method's bytes are compared with memcpy's of the same map.
- Workstation: RTX 4080, 11:15-11:20, under `quietlock.py run` without a hold. Other teams' render suites were running,
  so this is an indication.
- Laptop: testbed jobs `20261125-111727-6a1a` (adapter 0, Intel HD 630) and `20261125-111739-9435` (adapter 1, Quadro
  P3000), quiet headed, `undisturbed=True`.
- The EXE on the laptop draws on the P3000: 0719's live copy there read 168 MB/s (5.5), which is uncached memory.

**A. The tail read by instruction (us for the tail / MB/s)**

| Tail | 4 KB | 16 KB | 40 KB | 84 KB | 128 KB | bytes |
|---|---|---|---|---|---|---|
| RTX 4080 memcpy (runs 1-2) | 8.7 / 470 | 27-33 / 500-610 | 65-259 / 160-630 | **155-156 / 550** | 218-240 / 550-600 | |
| RTX 4080 SSE2 16-byte loads | 23-32 / 130-180 | 101-109 / 150-160 | 287 / 140 | 528-631 / 140-160 | 852-984 / 130-150 | equal |
| RTX 4080 SSE4.1 `MOVNTDQA` | 1.5-1.7 / 2,400-2,700 | 4.9-5.3 / 3,100-3,300 | 11.5-12.9 / 3,200-3,600 | **22-30 / 2,900-3,900** | 33-42 / 3,100-4,000 | equal |
| RTX 4080 AVX2 `VMOVNTDQA` | 1.1-1.2 / 3,400-3,700 | 2.7-4.5 / 3,600-6,100 | 5.5-6.7 / 6,100-7,400 | **13.7-16.3 / 5,300-6,300** | 21.5-27.8 / 4,700-6,100 | equal |
| RTX 4080, 1 MB / 4 MB buffer, AVX2 | 0.8-1.0 | 2.1-3.5 | 5.3-6.4 | 12.0-13.5 | 15.8-19.6 | equal |
| P3000 memcpy | 11.2 / 366 | 42.7 / 384 | 105 / 390 | **224 / 384** | 352 / 372 | |
| P3000 SSE4.1 | 0.8 / 5,100 | 2.3 / 7,100 | 5.2 / 7,900 | **10.4 / 8,300** | 16.0 / 8,200 | equal |
| P3000 AVX2 | 0.8 / 5,100 | 1.8 / 9,100 | 3.8 / 10,800 | **7.6 / 11,300** | 11.3 / 11,600 | equal |
| HD 630 memcpy (cached memory) | 0.1 / 41,000 | 0.4 | 1.1 | 2.5 / 34,000 | 4.2 / 31,000 | |
| HD 630 SSE4.1 / AVX2 | 0.2 | 0.6-0.9 | 1.5-2.3 | 3.5-4.9 / 17,500-24,600 | 5.6-7.6 | equal |

- **On write-combined memory a streaming load fills a 64-byte streaming buffer per line, and an ordinary load does not.**
  This is the instruction's documented purpose `[I: documentation; L: the rates]`. The rate holds for 256 KB, 1 MB
  and 4 MB buffers on the RTX 4080, where mapread's memcpy fell from 249 to 118 MB/s with size (1.6).
- On cached memory (the HD 630's dynamic buffers, and the product's stagings and constant buffers) streaming loads are
  slower than memcpy: 17-25 against 31-41 GB/s. **So the route uses them only for the driver's pointer of a DYNAMIC
  non-constant buffer**, the memory 1.6 found uncached.

**B. A worker reads the 84 KB tail while the map is open** (the main thread "writes" for W us, then at Unmap reads the
pages the worker has not claimed from one atomic page cursor, and waits for the page in flight):

| Main thread's us at Unmap, median / p90 | W 0 | W 50 | W 100 | W 200 | W 800 |
|---|---|---|---|---|---|
| RTX 4080, memcpy worker | 105 / 196 | 101 / 319 | 43 / 137 | 0.1 / 252 | 0.1 / 0.2 |
| RTX 4080, SSE4.1 worker | 10.5 / 14.5 | 0.1 / 0.2 | 0.1 / 0.2 | 0.1 / 0.2 | 0.1 / 0.2 |
| P3000, memcpy worker | 140 / 161 | 111 / 131 | | | |

- The worker wakes 6-10 us after `SetEvent` on the workstation, and 5-37 us on the laptop. Bytes were equal in every
  round.

### 7.3 The routes `[C for what each changes; L bench; I the live gains]`

The bytes are identical when every byte a record or the server reads is the byte it reads today.

| Route | What changes | Main-thread gain per present (ws, moving / rest) | Same bytes | Risk to D242's exact lockstep | Verdict |
|---|---|---|---|---|---|
| **A. streaming-load tail read** | the tail's page runs (and a direct map's read of a DYNAMIC non-constant buffer) read with `VMOVNTDQA` (AVX2), else `MOVNTDQA` (SSE4.1), else memcpy; CPUID once at init; `_mm_mfence` before; the unaligned remainder by memcpy; unaligned stores into the shadow | about **0.32 / 0.65 ms** `[I: 7.9 us per KB down to 0.2-0.4 us per KB]` | yes: a load is a load; bench equal in every round | none: same thread, same order, same place in the frame | **recommended** |
| B. worker reads the tail while the map is open | the Map hook wakes a worker with the driver's pointer and the predicted unwritten pages (the last map's); the Unmap hook claims the rest, waits for the page in flight, then forwards | A's gain plus the rest of the read (about 0.02 ms more after A) | yes: the read is complete before the forward and the real `Unmap`, and pages that turn out written are overwritten by the staging copy | low to medium: a thread, a wake (5-37 us), more CPU. On the laptop the server's pass slows with load (1.5, 5.5). The Map-to-Unmap window per buffer was never measured | only if A leaves too much |
| C. lighter per-call work for the ~880 constant-buffer Unmaps | no `QpcNow` pair per call (time 1 call in N), per-context counters instead of interlocked ones, the shadow found once per open map | at most about 0.1 of the 0.19 ms `[I: the coefficient's interval is wide]` | yes | none | a later small ticket, after a split timer shows the share |
| D. zero-copy staging (the shadow reads the staging until the next `Map` of that buffer) | the cached memcpy of about 215 KB goes; the shadow must not be overwritten before the records that read it are placed (`ResolveRefs` at `ExecuteCommandList`) | 0.02-0.15 ms `[I]` | yes, if the next `Map` copies first or rotates stagings. A rotation changes the older bytes in written pages for both the GPU and the record (still equal to each other) | medium: lifetime across deferred lists | not worth it now |
| E. defer the copy to the publication or to `Place` | - | - | **no**: the driver's pointer is invalid after `Unmap`, the staging is handed out again at the next `Map`, and the renamed driver memory may be reused | - | rejected |
| F. a smaller captured set: a decaying mark | reads fewer tail pages | up to A's gain | **no**: a deferred draw's reference is resolved at `Place`, after its drop. A read past the copy would give `place_miss` zeros in a dropped draw's record. `sh->bytes` never shrinks, so the draw-time check cannot see it | high | rejected |
| G. forward the tail pages instead of reading them (write the staging's bytes into the driver's unwritten pages) | a 0.03 ms write instead of the read | about A's gain | **no**: the GPU and the record then hold the staging's older bytes in place of the driver's, a change in what the EXE's over-read draws see (undefined after `WRITE_DISCARD` in stock too) | low for lockstep, but a picture change | only on the MC's word; unnecessary with A |
| H. a GPU copy (`CopySubresourceRegion` to a staging buffer, read back) | - | - | yes | **high**: the read-back waits for the GPU inside the frame | rejected |

### 7.4 The measurement that proves route A (for the change ticket)

1. **Same bytes, offline**: `shell/tests/test_model_filter.py`, all map patterns x the three key values, plain and ASan.
   Add a stand-in case with a tail of unwritten pages below the mark (the split and pad patterns have one).
2. **Same bytes, live**, on the workstation, verify launches like 5.3:
   - A TEST ONLY key (`model_tail_stream=verify`) reads each tail run both ways and counts `tail_diff` (must be 0).
   - `model_map_cache=verify`'s `rec_diff` and `verify_bad` must also be 0.
   - Bar: over at least 50 MB of tail on huge T350 (moving, wide and at rest).
3. **Cost**:
   - Add a `tail_ms` counter (inside `copy_ms`).
   - On the workstation, an interleaved ABBA x 3 series, with the key switching streaming loads off (0) and on (1), in a
     held hour, n = 6 an arm, Welch 95 % (PERFORMANCE.md).
   - Primary metric: `copy_ms` per present, moving and at rest. Expected about -0.3 / -0.65 ms; detectable at 0754's
     launch sd (0.05-0.09 ms).
   - Secondary: E, the wait and the interval, at rest and moving, read as 5.5 reads them. At rest a share of the saving
     should reach the interval on the presents that did not wait. On the laptop it should become wait.
4. **CPUs without AVX2 or SSE4.1** (Civ V's minimum spec has them without):
   - The fallback is memcpy, today's path.
   - A unit test forces each level by a TEST ONLY key.

### 7.5 Not verified

- The live gain of A. The bench reads one 256 KB to 4 MB buffer alone. The live tail is read in page runs, out of the
  EXE's buffers, while the GPU and the EXE run. The live 129 MB/s is below the bench's memcpy (7.1).
- The tail coefficient comes from seconds that differ in other ways too (view, draws), not from an arm. Its interval is
  narrow (+-0.28 us per KB), but a cost correlated with the tail would be counted in it.
- The workstation bench ran beside other teams' render suites, so it is an indication. The laptop jobs were undisturbed.
- Whether the store-to-load ordering on write-combined memory needs the fence before the streaming loads. The tail
  pages are ones this CPU did not write in this map, but the forward wrote other pages of the same buffer just before.
  The fence is in the design because it costs well under 1 us.
- The Map-to-Unmap window of the EXE's three cached maps (route B's budget). It was never timed.
- Oldpc (RTX 2080, 500 MB/s memcpy) was not benched for 0757 (busy with 5041's smoke).

## 8. Ticket 0758: route A built: the tail read with streaming loads (impl-0758, account B, 2026-11-25)

The change: `shell/composite/tail_read.h` (new) and `OnUnmap`'s tail runs in `shell/composite/unit_filter.cpp`. Evidence
and commands: `shell/evidence/0758/README.md`. Marks: `[C]` read in the code, `[L]` measured, `[I]` inferred.

**Result.**
- **Same bytes** `[L]`: offline over 2358 cases (8.2), in the stand-in at every key value (8.3), and live on the laptop:
  1.92 GB of tail read both ways with `tail_diff` 0 (8.4).
- **Laptop, grade A** (12 interleaved jobs, 6 an arm, at rest, 235 KB of tail per present) `[L]`:
  - The copy at Unmap: 2.18 -> 0.64 ms per present, **-1.53 ms [-1.59, -1.47]**.
  - The tail read inside it: 1.65 -> 0.10 ms (142 MB/s -> 2.3 GB/s).
  - E: -1.23 ms [-1.59, -0.88]. The interval does not move (+0.41 [-0.18, +1.00]): those frames wait for the server.
- **Workstation: owed** (8.5). The window was cancelled for beta 5's tier saves. Offline on the RTX 4080, an 84 KB tail
  reads in 11-15 us (avx2) instead of 389-407 us (memcpy) `[L, indication]`.
- No VP64Options key (D255). The fallback is today's memcpy, chosen once at the init from CPUID.

### 8.1 What changed `[C: unit_filter.cpp and tail_read.h on branch impl-0758]`

- **The tail run's read only.** In `OnUnmap`, each page run of a cached map's tail (7.1 step 3) is read by
  `TailRead(g_tailLevel, shadow + runAt, driver + runAt, len)` instead of `memcpy`. Same thread, same order, same place
  in the Unmap hook. The cached copy of `[0, want)` from the staging, the forward and the direct maps keep `memcpy`
  (they read cached memory, where streaming loads are slower: 7.2 A).
- **The level, once at the init** (`InitOnce`, beside `model_map_cache`): `TailReadLevel()` reads CPUID leaf 1 (SSE4.1:
  ECX bit 19; OSXSAVE bit 27, AVX bit 28) and leaf 7 (AVX2: EBX bit 5) and `XGETBV(0)` (XMM and YMM state saved by the
  OS), only when OSXSAVE is set. AVX2 -> `VMOVNTDQA` 32-byte loads, else SSE4.1 -> `MOVNTDQA` 16-byte loads, else memcpy.
- **The copy:** `MFENCE`; the bytes up to the source's 16- (32-) byte alignment by memcpy; four 64-byte lines in flight
  per loop with the loads of a line one after the other (0757's bench order), unaligned stores into the shadow (it is
  `realloc`'d, 8-byte aligned on x86); the last bytes by memcpy; `VZEROUPPER` after the AVX2 loop, before the memcpy. No
  load reaches outside `[src, src + n)`. The tail's source is page aligned (`runAt` is a page multiple) and its runs are
  whole pages except the last, which ends at `need`.
- **The TEST ONLY key `model_tail_stream`** (`[composite]` or `VPSHELL_MODEL_TAIL_STREAM`): unset / `auto` / `1` = the
  CPU's best; `0` = memcpy (the read before 0758: the A/B's base arm); `sse41`, `avx2` = that level, at most the CPU's
  best (a capped request is noted); `verify` = the CPU's best, and every tail run read again with memcpy into a 4 KB
  stack buffer and compared (`tail_diff` counts the runs that differ, the first eight are logged). A bad value is a
  CONFIG ERROR line and the default. One line at the first Present names the level and the CPU's best:
  `model_filter: tail read: avx2 (this CPU: avx2)`.
- **Counters** in `modelfilter 1s:` / `totals:`: `tail_read` (the level's name), `tail_ms` (the tail reads' QPC time,
  inside `copy_ms`; two `QpcNow` per run), `tail_vkb`, `tail_diff`.
- **No VP64Options key** (D255): the output is the same bytes and nothing is added. The server's input does not change,
  so rule A does not apply. The fallback on a CPU without SSE4.1 is today's memcpy.

### 8.2 Same bytes, offline `[L, workstation RTX 4080, 2026-11-25 11:51, under other teams' builds]`

`unit_filter_host.exe --tailread 300` (`shell/tests/unit_filter_host.cpp TailReadBench`): a 256 KB DYNAMIC vertex buffer
mapped `WRITE_DISCARD`, the first half written, then `[half + soff, +n)` read at every level the CPU has into a guarded
destination at offset `doff`. Three sources: the map with its tail unwritten (the product's case), the map written whole
(the tail holds the host's bytes: also checked against them), and ordinary memory. The bench's five tails at `soff 0`,
`doff` 0 and 8, 300 rounds each; 252 edge cases once each (source offsets 0, 4, 16, 20, 36, 4084; lengths 1 to 86116
around 16, 32, 64 bytes and a page; `doff` 0, 1, 8). **2358 cases: every read equal to memcpy's of the same map, no byte
written outside the destination, the host's bytes where it wrote them.**

| Tail (median us, the unwritten half; doff 0 / 8) | 4 KB | 16 KB | 40 KB | 84 KB | 128 KB |
|---|---|---|---|---|---|
| memcpy | 26 / 29 | 112 / 112 | 309 / 272 | **389 / 407** | 489 / 549 |
| sse4.1 | 1.4 / 1.4 | 6.0 / 5.7 | 14.6 / 10.3 | **33 / 23** | 42 / 37 |
| avx2 | 1.2 / 1.2 | 4.6 / 3.7 | 7.1 / 8.4 | **15 / 11** | 22 / 26 |
| memcpy, ordinary memory | 0.2 / 0.3 | 0.8 / 0.5 | 2.0 / 2.2 | 4.3 / 4.3 | 6.7 / 7.3 |
| avx2, ordinary memory | 0.2 / 0.3 | 0.8 / 0.8 | 1.7 / 1.8 | 3.3 / 3.5 | 6.7 / 7.0 |

- The host's memcpy into a `std::vector` destination ran slower than the bench's (155 us for 84 KB into 64-byte aligned
  memory, 7.2): 210-230 MB/s, near the live 129 MB/s. The streaming reads do not care about the destination's alignment.
- Log: `shell/evidence/0758/tailread-ws1.txt`.

### 8.3 Same bytes in the composite `[L, stand-in]`

`shell/tests/test_unit_filter_tailread.py` (new, 9 cases): the offline cases above at 40 rounds, then the model stand-in
(test_model_filter.py's host) in its `split` map pattern, whose cached map leaves the instance buffer's second half
unwritten below the draws' mark (27 KB of tail in 8 frames), with each key value: auto (with `model_map_cache` 1 and
verify), 0, sse41, avx2, verify (with `model_map_cache` verify and 1), and a bad value. Each run: the drops and records
of the direct path, the driver's buffers as written (`--map-check`), `rec_diff` 0 (every record read equal to the direct
path's copy), the level named as asked, and with verify `tail_diff` 0 over every tail byte. Checks run with the plain
build: test_unit_filter, test_model_filter, test_farm_guard and the new file, **121 passed** (300 s); ASan build: the new
file and test_model_filter, **63 passed** (162 s).

### 8.4 Laptop: the grade-A set `[L, laptop, Quadro P3000, EXE 4390913, 2026-11-25 12:18-13:33 EST]`

- **Method.** `series0758.sh`: 12 quiet headed testbed jobs, one at a time, ABBA x 3 (`0 auto auto 0 0 auto auto 0 0 auto
  auto 0`), `--env VPSHELL_MODEL_TAIL_STREAM=0|auto`; job `job0758.py --arm idle` on TB-HUGE_0350 at the load's view.
  Beta 4's cand6 layout (server, x64 DLL, proxy, cand6's composite ini) with **this ticket's composite (ffe95ab6)**, the
  pack's purity.tsv (`purity: loaded` in all 12 vpproxy.logs), uncapped, vsync off, every job restored equal by hash. The
  window is the job's 60 s `idle` window (no input); `an0758.py --jobs`. Every quiet proof `alone=True undisturbed=False`
  (0.17-0.30 foreign cores). Output: `shell/evidence/0758/laptop-an.txt`.
- **No tail without a camera move** `[L]`. 0754's idle job unchanged (jobs 20261125-120420-d615 key 0 and -121004-8cdf
  auto, kept out of the set) logged `tail_kb=0` for the whole run: the draws' mark `need` only grows, and a view that
  never changes writes every page below it each frame. `job0758.py` therefore zooms out 30 wheel ticks for 8 s and back
  before the windows (15.4-15.8 s). The tail at rest is then 235 KB per present. On the workstation the still window
  follows drags, and 0754 measured 86 KB there.

| At rest (60 s idle window), per present, 6 jobs an arm | key 0 (memcpy) | auto (avx2) | auto - 0, Welch 95 % | detectable |
|---|---|---|---|---|
| copy at Unmap `copy_ms` | 2.179 (sd 0.031) | 0.645 (sd 0.054) | **-1.534 [-1.595, -1.474]** | 0.12 |
| tail read `tail_ms` | 1.653 (sd 0.022) | 0.104 (sd 0.005) | **-1.550 [-1.573, -1.526]** | 0.05 |
| tail `tail_kb` | 235.4 | 235.3 | -0.1 [-0.3, +0.1] | |
| E (lockstep CSV) | 9.22 (sd 0.16) | 7.99 (sd 0.33) | **-1.23 [-1.59, -0.88]** | 0.72 |
| interval | 13.58 (sd 0.24) | 13.99 (sd 0.54) | +0.41 [-0.18, +1.00] | 1.19 |
| whole job (load, zoom, idle): `copy_ms` | 1.911 | 0.593 | -1.319 [-1.405, -1.232] | 0.17 |

- **Why it moved** `[L rates; I mechanism]`: the tail's 235 KB were read from the P3000's write-combined memory at 142 MB/s
  by memcpy (7.0 us per KB) and at 2.3 GB/s by `VMOVNTDQA` (0.44 us per KB). The bench's 8-11 GB/s (7.2) is not reached
  live: the read is in page runs between the EXE's other work. The copy's other parts (the staging copy, the forward,
  the direct maps) did not change (copy minus tail: 0.53 against 0.54 ms).
- **E falls by about the saving, the interval does not** `[L; I]`: the laptop's lockstep frames wait for the server
  (0719 5.5, 0754 6.4), so the EXE's 1.2-1.5 ms become wait. The gain reaches the frame only where the EXE sets it.
- **Live same bytes** `[L]`: job 20261125-133451-93c5 (13:35-13:40, `model_tail_stream=verify`, same layout): avx2,
  **1,920,361 KB of tail read both ways, `tail_diff` 0**; `rec_diff` 0, `verify_bad` 0, `place_miss` 0, restored equal.

### 8.5 Workstation: owed

The held window MC-B booked for 2026-11-25 16:00-17:15 was cancelled at 15:11 (#15162): beta 5's VP 5.5 tier saves took
the workstation's CPU. The series is ready to run in the next window: `shell/evidence/0758/ws0758.sh` (c5h-new-b, EXE
26577625, beta 4's cand6 layout with this composite, purity.tsv, the verify launch `v0` then five pairs
`z1 a2 a3 z4 z5 a6 a7 z8 z9 a10`), analysis `an0758ws.py` (dry-run on copies of 0754's launch folders). The 2026-11-25
11:51 offline figures (8.2) are its only workstation numbers, taken beside other teams' builds: an indication.

### 8.6 Not verified

- **The workstation's live gain** (8.5). Expected `[I]` from 0754's 41 / 86 KB tails and the laptop's live rate ratio:
  most of the 0.42 / 0.68 ms tail read (moving / at rest) goes; the interval moves only on the presents that did not wait.
- A CPU without SSE4.1 (the memcpy path) and one with SSE4.1 but not AVX2 were not run live. The SSE4.1 path ran offline
  (8.2) and in the stand-in (`model_tail_stream=sse41`, 8.3), both on the workstation's CPU.
- Whether the fence before the streaming loads is needed (7.5): it stays, and it is not timed apart.
- Oldpc was not run.

