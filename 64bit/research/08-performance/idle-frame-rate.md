# Idle frame rate on the huge save: the thin client against the 32-bit DLL (issue 0095)

Issue 0095 (account B, 2026-11-15). The question: one-off 9028 measured on 2026-11-11, on the workstation and build B6,
that the proxy with local answers runs the huge save idle at 7.6 % below the 32-bit DLL (95 % interval 3.7 % to 11.6 %;
`research/03-dll-engine-boundary/verification-2800.md` section 3). Is that still so on the current pins (COMMON 18,
D96), measured off the workstation (COMMON 23)? This file holds the method, the job, the numbers and the
recommendation. Every time is EST (host clocks on `oldpc` agree with the workstation's to the second [I]).
Section 0 (issue 0350) is about the Lua frame counter every frame-rate tool here shares.

## 0. The Lua frame counter: `Events.LocalMachineAppUpdate`'s arguments are the uptime in ms and the frame delta in s (issue 0350, 2026-11-15)

**The question.** `proxy/tools/live/frame_counter.lua` (issue 2704) counted frames with a handler on
`Events.LocalMachineAppUpdate` and kept the largest handler argument as "the largest frame time" (`idle_window.py`'s
`max_frame_s`). lead-0330 read one argument on 2026-11-15 as 52,384,101 at log time 52,384 s
(`metrics-ledger.md` M4) and 0157 had measured its change per wall second as 999.6-1000.0
(`research/06-tooling-and-workflows/run-and-drive-the-game.md`, "Animation follows the clock"). Issue 0350 checks
live what the argument is, fixes the two tools and lists every recorded result that used the wrong reading.

**Method** (`research/08-performance/evidence/0350/live_0350.py`, own code; three runs on 2026-11-15, 21:15-21:22,
21:23-21:31 and 21:35-21:42 EST, on c5h-h (F:), the
stock 32-bit route with the pin `i0ship-26234371-ia` (`3C2F47DA...`), the save `C5H-VP8P-HUGE_0350` (turn 350),
frame-capped launch, never a timing launch; workstation, machine busy with other teams' work, so no number here is a
frame-rate result): a recorder handler keeps 240 frames' arguments (all of `...`; the second from run 2) and
`os.clock()` per frame; Python reads
`GetTickCount64`, `QueryPerformanceCounter` and `timeGetTime` around each read; then the fixed counter over 20 s at
the guard's rates 30, 10 and 0 fps (`framecap.set_fps` at run time); then `idle_window.py`'s own `main()` at 10 fps
with the capped-instance refusal bypassed (a functional check of the reading, not a frame rate), and unbypassed, which
must refuse (exit 4). Run 3 ran the final code (five values); runs 1 and 2 the first draft (three values). Evidence:
`research/08-performance/evidence/0350/run1..run3/` (`run.json`, `driver.log`, `idle_window_patched.json`).

**Result** [L, 2026-11-15, three runs alike].
- **The handler gets two numbers.** `select('#', ...)` = 2, both `number`. The first is the machine's uptime in
  milliseconds: an integer (109,679,265 at 21:19 = 30.5 h, the same clock as lead-0330's 52,384,101 at 05:23 plus the
  15.9 h between), equal to `GetTickCount64` read around the chunk (the last argument lay 0-125 ms after the tick read
  before the chunk was sent and 0-47 ms before the one after it answered), and its change over Python's wall time was
  0.9995-1.0037 in all nine 20-s windows. **It is not a frame delta.** The second argument is the frame delta in
  seconds, a single-precision float: over 239 frames it equals the first argument's difference / 1000 within
  2.1e-6 ms (`second_vs_delta`, run 2).
- **Both have the 15.6 ms tick of `GetTickCount`.** Frame-to-frame steps of the first argument: 31, 32 and 47 ms at the
  guard's 30 fps (mean 33.34), 93, 94, 109 and 110 ms at 10 fps (mean 99.96), 0, 15 and 16 ms uncapped (mean 8.0-9.4 at
  94-101 fps). A frame time from the arguments reads up to one tick (15.6 ms) high or low; uncapped above 64 fps
  most frames read 0 or 16 ms.
- **`os.clock()` in the game is wall time, not processor time** (corrects 2704's `[I]` in
  `research/06-tooling-and-workflows/run-and-drive-the-game.md` section "Frames from Lua"): over the recorder's 240
  frames it advanced 7.966 s against 7.969 s of the argument at 30 fps, 23.899 against 23.890 s at 10 fps (where the
  EXE uses well under one processor, 0.55-0.69 at 30 fps by 0157) and 1.907 against 1.907 s uncapped (where it uses
  about 3.5); over the fixed counter's 20-s windows its change over wall time was 0.999-1.001 (run 3). Its steps are
  1 ms (0.032-0.035 s per frame at 30 fps), so it gives the finer frame time [I: the MSVC C runtime's `clock()` is the
  wall time since the process started; the same EXE runs in both routes, so the proxy route is the same, not run].
- **The fixed tools, live (run 3):** largest frame time per 20-s window by `os.clock()` / by the arguments: 0.082 /
  0.078 s at 30 fps, 0.105 / 0.110 s at 10 fps, 0.032 / 0.032 s uncapped. `idle_window.py`'s own `main()` at 10 fps
  (refusal bypassed): rc 0, fps 10.0, `max_frame_s` 0.104, `max_frame_engine_s` 0.110, `arg_clock_ratio` 0.9996,
  `os_clock_ratio` 1.0001; unbypassed it refused the capped instance with exit 4 and wrote no JSON. 24 of 24 checks of
  the driver passed (run 2: 14 of 14 with the draft's checks; run 1 failed only the driver's own type check, written for
  one argument). Each run: stopped by the harness on the human's turn with the newest autosave settled, the front end
  and the standard files restored by hash (all equal), `provision.py verify c5h-h --quick` ok after run 3.

**The fix** [C, code; hermetic test; L run 3]. `frame_counter.lua` (new key `vp.live_fc2`: a game that had armed the
old chunk keeps its old handler, which counts into `vp.live_fc` and is no longer read) returns five values: the frame
count, the largest engine frame time (difference of two consecutive first arguments, s), the last first argument (ms),
the largest `os.clock()` difference between two frames (s) and the last `os.clock()`; it resets both maxima at every
read. `idle_window.py` reads five values (fewer are refused: "expected 5 values") and writes `max_frame_s` (the
`os.clock()` one, 1 ms steps), `max_frame_engine_s` (the arguments', 15.6 ms tick), both None when no frame fell inside
the window, and `arg_clock_s` / `arg_clock_ratio`, `os_clock_s` / `os_clock_ratio` (each clock's change between the
two reads, and over `wall_s`: about 1.0, a check of both clocks on every run). `fps` is unchanged in definition. The
added handler work is one `os.clock()` call, two subtractions and two comparisons per frame [I: not measured; frame
times are 2.5 ms and up]. `tools/testbed/jobs/fps_idle.py` reads only the first value (the count) and needs no change
(its 40 tests pass). Test: `proxy/tools/live/test_frame_counter.py` (14 tests: the chunk in a Lua 5.1 VM through lupa
with a fake `Events` that passes (uptime, delta) and a fake `os.clock`, the old chunk as the negative control,
`idle_window.window()` and `frames()`, the old two- and three-value shapes refused; run without `python -I` and with
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`, since lupa is in the user site).

**Recorded results that used the wrong reading** (searched 2026-11-15 over the whole repository for `max_frame_s`,
`maxdt`, `max_dt`, "largest frame", `frame_counter`, `idle_window`, `LocalMachineAppUpdate`). Every `max_frame_s` in
an `idle.json` written by `idle_window.py` before this fix is the machine's uptime in ms at the window's largest
argument, not a frame time (values 1,048,939,703 to 1,126,278,828: 12.1 to 13.0 days of uptime); the `fps`, frame
counts, wall seconds and memory in the same files are not affected:

| Where | Files | Taken |
|---|---|---|
| `server/evidence/2700/` (`live/`, `live7/`, `live10/`, `verification/live/`) | 39 `idle.json`, 39 `arm.log` | 2026-11-10 |
| `server/evidence/2800/` (`fps-tiny/`, `live/`, `huge-crash-reruns/`) | 36 `idle.json`, 36 `arm.log` | 2026-11-11 |
| `server/evidence/9028/` (`fps/huge`, `fps/tiny`, `func/`) | 30 `idle.json` | 2026-11-11 |
| `server/evidence/9029/live/` | 2 `idle.json` | 2026-11-11 |

No research text and no table quotes any of those `max_frame_s` values (search above), and `proxy/tools/live/
fps_table.py`, `summarize_arms.py` and the ledger's adapters (`perf/ledger/adapters/`) do not read the field, so no
statement in the knowledge base changes; the evidence files stay as they were (originals) and this section is their
correction. Other readers of the argument: `server/evidence/2500/fps_arm.lua` (the same misreading; its output was
never kept, `proxy-design.md` 15.5); `tools/harness/evidence/0157/run_0157.py` records `max_dt` and `dt_sum` with the
same misreading (both meaningless), but 0157's research cites only the argument's change per wall second, which is
right; `perf/profilekit/stocklive.py` (4203) reads it correctly as uptime. `metrics-ledger.md` M4's bullet on the
argument is left to issue 9071 (its owner); its lines are listed on 0350's thread.

## 1. The frame-rate job on a test machine (step 0) [C, code; L where marked]

Before this issue there was no frame-rate tooling on the test machines (`research/06-tooling-and-workflows/
quiet-testbed.md` 21.4: `frame-rate series` "no" on both). Issue 0349 had shown that a `--headed --quiet` product-path
job runs on `oldpc` (one job holding both runner slots, with the quiet proof).

`tools/testbed/jobs/fps_idle.py` (Python 3.8; tests `tools/testbed/tests/test_fps_idle.py`, 27 offline tests: the
statistics against hand values, the argument checks, prep failing before and after its backup, an arm that does not
reach the map, a refused stop, a dialog, a capped launch, an arm in the wrong mode, a failed restore, a cancel):

- **Shipped with the job:** the five VP64 files of the x64 pin `x64-04b1211b-pfdee656` (server.exe `5B254430...`, DLL
  `29F58E17...`), its proxy (`75C40CEC...`), the 32-bit pin `i0ship-26234371-ia` (`3C2F47DA...`), the harness
  natives built in the worktree and the vp-dll-dev skill's front-end assets. 6.2 MB compressed (20.5 MB raw) for the
  first job on 2026-11-15.
- **Prep, once per job:** the standard files and `VP64Options.ini` backed up by SHA-256 into the job's work folder;
  `provision.py set-proxy` with the shipped files and the 32-bit pin as `--fallback-dll` (it lands beside the proxy as
  `CvGameCore_Expansion2_x86.dll`, the default ini's `fallback_dll`); `VP64Options.ini` `[Renderer] ServerRenderer = 0`
  (the shipped default, `docs/OPTIONS.md`); `config.ini` `LoggingEnabled = 0` (the value before is recorded), no D33
  `[DisableSystems]` change (stock config.ini otherwise); the save copy `TBFPS-HUGE_0350` of `TB-HUGE_0350` (SHA-256
  `c499d3c6...`, the workstation's `C5H-VP8P-HUGE_0350`, turn 350).
- **Each arm, a fresh game:** the `vpproxy.ini` keys of its kind; `frontend.py prepare --load`; `cli.py launch --timing
  --mode offscreen --park none` (a timing launch: no frame cap, issue 0157; the window on screen, COMMON 24); in game
  with the load screen closed (`frontend.py ingame` / `dismiss`); 20 s settle; `frontend.py ingame` again (rc 0 = in
  game on the human's turn, nothing modal); the EXE's child processes read (`server.exe` must run in `on` and `off`,
  must not in `B`: an arm in the wrong mode fails the job, exit 4); `proxy/tools/live/idle_window.py` with 45 s; the
  harness stop with the human-turn hook. Kinds: `on` = thin client (`mode=on local_answers=1 local_verify=0
  lua_mirror=0`), `off` = no local answers, `B` = `mode=off` (the proxy boots the 32-bit pin).
- **The number:** `idle_window.py`'s fps = frames counted by `frame_counter.lua` (a handler on
  `Events.LocalMachineAppUpdate`, once per frame on the main thread, the same chunk in both modes) between two reads
  through the Lua channel, divided by the wall seconds between the two reads. It refuses an instance without a timing
  launch on record (exit 4), and the job refuses an `idle.json` without `frame_cap.timing = true`. Its `max_frame_s`
  is the largest frame time since issue 0350 (section 0; *corrected 2026-11-15 by 0350, was: "is **not** a frame
  time ... meaningless and is not used", true of every `idle.json` written before the fix*); this issue's job does
  not use it either way.
- **Load:** machine CPU (all cores, psutil) and available memory every 5 s into `out/cpu-load.csv`; the mean over each
  window is in the arm's record. The quiet proof of the runner covers the whole job.
- **Restore:** `vpproxy.ini` back to its recorded hash, `set-proxy --original`, `frontend.py restore`, the backup
  restored by hash (created files removed), the save copy removed, `provision.py verify`.
- **Statistics** (`out/fps-summary.txt`, `out/fps_idle.json`): per kind n, mean, sd, cv, median, min, max, the 95 %
  t interval; paired differences per repetition (`on - B`, `off - B`, `on - off`) with mean, sd, 95 % interval and the
  half-width in % of B (the smallest difference the set resolves); for a one-kind set (A/A) the resolution a paired set
  of six pairs would have, `t(5) * sd * sqrt(2) / sqrt(6)`, and the consecutive pseudo-pairs as a check of that
  assumption. Arm folders are named `F<rep>-H-<kind>`, so `proxy/tools/live/fps_table.py` reads them too.

Differences from 9028's method (verification-2800.md 3): no proxy counter snapshot inside the window and no statistics
dump before it (both kinds are measured the same way); a 20 s settle instead of 9028's guard check + 5 s + dump; the
test machine's EXE and GPU (RTX 2080, 1920 x 1080 desktop), not the workstation's.

### 1.1 Changes after the performance team's review (7000, thread entry #1793, 2026-11-15) [C, code]

The first version (commit f00bc5dc) used `idle_window.py`. 7000 confirmed the method with changes, all in `fps_idle.py`
since 8dcffbde: the window is the job's own process (`fps_idle.py --measure`), which reads the same counter at the start
and after each of three 15 s blocks (fps per block and over the whole window, the foreground window's process at each
read); one warm-up arm (`--warmup on`, `W0-H-on`) loaded and measured first and never counted (a cold file cache after
the prep); one metrics-ledger record (`c5-run/1`) per arm under `out/ledger/` (series_id = the job id, arm = the kind,
repetition = the rep; configuration `inc1-thin` for `on`, `stock-32` for `B` with the label "32-bit pin booted by the
proxy, mode=off" (B is not proxy-free: the proxy forwards every call to the pin); the four binaries hashed; free commit
at the arm's start and end; grade A only with the quiet job, else B), with two new registry ids that 7000 approved:
`frame.idle_ft_mean_ms@1` (1000 x wall s / frames) and `frame.idle_frames@1` (pins added in
`perf/ledger/tests/test_registry_pins.py`); the smallest detectable difference at 80 % power, (t(0.975, df) + t(0.80,
df)) x SE (3.49 x SE for 6 pairs), as `perf/ledger/page/stats.py`; the A/A stop rules (a paired set of 6 pairs resolves
2.02 x sd: stop above 3 % of the mean, i.e. an A/A cv above 1.49 %; stop when the pseudo-pair sd is above 1.5 x sd x
sqrt(2)); the block-drift rule (first block's fps minus the last above the A/A sd: a 60 s settle in step 2); the closing
rule (section 4); arms with window CPU more than 10 points above the set's median named and kept.

### 1.2 Two things found on the way [L, 2026-11-15]

- **No frame-rate tool with the 0157/0159 check ran on `oldpc`.** `framecap.require_uncapped` calls
  `timing_refusal`, which imports `tools/x64-dll/build.py`; its annotations `str | None` raise `TypeError` at import on
  Python 3.8, the runner's Python there. The first A/A job (`20261115-124944-c84e`, 12:49-12:53) failed in its first
  window (`idle_window.py` rc 1, job exit 4), the instance restored by hash, `provision.py verify` rc 0. Fixed on main by
  the MC at d3a9603d (`from __future__ import annotations` in `build.py`); `test_fps_idle.py` checks that `build.py`
  keeps it.
- **On `oldpc` the game's window on screen is vsync-bound: 60.0 fps** in the warm-up and every arm of the A/A job
  `20261115-130206-8961` (each 15 s block 60.0). Every instance copy has `GraphicsSettingsDX11.ini WaitForVSync = 1`
  (`tb1` on `oldpc`; `c5h-a` and `c5h-c` on the workstation, read 2026-11-15); the workstation's harness windows are
  cloaked or parked and never synced (`research/06-tooling-and-workflows/run-and-drive-the-game.md`, 113-338 fps since
  2026-11-10), which is why 9028 saw 231 fps there. So the job sets `WaitForVSync = 0` for its run (in the standard
  backup, restored by hash; `--vsync keep` leaves it), records the display's refresh rate (`GetDeviceCaps VREFRESH`) and
  refuses an arm whose fps is within 1 % of the refresh or of half of it (exit 4).

## 2. Step 1: the A/A set on `oldpc` [L, 2026-11-15]

Job `20261115-132845-3614` (`--quiet --headed`, both runner slots, commit 985821dd), `tb1` with `oldpc`'s own EXE, the pins
of COMMON 18 (x64 `x64-04b1211b-pfdee656`: server.exe `5B254430...`, DLL `29F58E17...`, proxy `75C40CEC...`; 32-bit
`i0ship-26234371-ia` `3C2F47DA...`), save copy of `TB-HUGE_0350` (turn 350, `c499d3c6...`), `LoggingEnabled = 0`,
`WaitForVSync = 0`, `ServerRenderer = 0`, stock config.ini otherwise, display 60 Hz, 20 s settle. One warm-up and six
arms of `on` (thin client), 13:29-13:56 EST. Fetched to `C:/c5-64bit-work/0095/oldpc-aa3/`; evidence in
`perf/evidence/0095/aa/`.

| arm | idle start | fps | blocks (15 s each) | window CPU % (machine) |
|---|---|---|---|---|
| W0-H-on (warm-up, not counted) | 13:31:47 | 211.9 | 202.8 / 216.8 / 216.0 | |
| F1-H-on | 13:35:31 | 212.1 | 203.7 / 216.9 / 215.6 | 48.4 |
| F2-H-on | 13:39:15 | 211.3 | 204.4 / 213.9 / 215.7 | 48.5 |
| F3-H-on | 13:42:59 | 211.8 | 205.1 / 215.4 / 214.9 | 49.3 |
| F4-H-on | 13:46:42 | 211.8 | 204.3 / 215.1 / 216.0 | 49.2 |
| F5-H-on | 13:50:26 | 211.7 | 201.7 / 216.1 / 217.3 | 49.6 |
| F6-H-on | 13:54:09 | 212.7 | 204.7 / 217.2 / 216.3 | 49.0 |

- **Spread:** n 6, mean 211.9 fps, sd 0.47, cv 0.22 %, 95 % interval 211.4 .. 212.4. A paired set of six pairs
  resolves 2.02 x sd = 0.95 fps = **0.45 %** at 80 % power: far below the ticket's 3 %, so step 2 goes ahead (7000's
  rule 6). Pseudo-pairs +0.7, -0.0, -1.1 fps, sd 0.90 against the predicted 0.67 (1.34 x, below the 1.5 x limit).
  The game is 0.22 % noisy here against 9028's 5 % on the workstation (sd 11.5 at 214 fps).
- **The first 15 s are slower:** block 1 is 12.0 fps below block 3 on average (9.8 to 15.6), far above the A/A sd; so
  step 2 settles 60 s instead of 20 s (7000's rule 5). [I: the game is still streaming or building something after
  the load; not investigated.]
- Machine: window CPU 48-50 % of all logical processors (psutil); the runner's
  quiet proof: `alone` true, `undisturbed` false, foreign cores mean 0.133 (limit 0.1; svchost, MsMpEng, the game's
  audiodg; the same 0.11-0.13 in every job of the day, 0349's included). Free commit 31.4 GB at each arm's start, 27.9
  GB at its end. No arm more than 10 points above the median CPU.
- **Foreground:** the game's window held the foreground in no read of any arm. The foreground window belonged to
  `SoftwareUpdate.exe` (Apple Software Update, `C:\Program Files (x86)\Apple Software Update\`, in the owner's session 3) in
  every read of every job of 2026-11-15. `ThrottleOnLossOfFocus = 0` (prep.json), so the EXE does not throttle in the
  background; both kinds of step 2 are measured the same way. Nothing on `oldpc` was touched (D41).
- Restore: `vpproxy.ini` back to its recorded hash, `set-proxy --original` rc 0, backup restored by hash, save copy
  removed, `provision.py verify` rc 0.
- Ledger: the seven records (warm-up included, `identity.arm` `warmup-on`) validated and ingested into
  `F:/c5-64bit-work/ledger/index.duckdb` on 2026-11-15 at about 13:58 (series_id `20261115-132845-3614`).

The vsync-bound A/A (`20261115-130206-8961`, 13:02-13:29, `WaitForVSync = 1`): 60.0 fps in every block of every arm,
sd 0: the display's refresh, not the game. Its ledger records are kept in `C:/c5-64bit-work/0095/oldpc-aa2/` and not
ingested (a refresh-bound frame time is not the metric's definition, which requires no frame cap).

## 3. Step 2: thin client against the 32-bit pin [L, 2026-11-15]

Job `20261115-135603-235c` (`--quiet --headed`, commit ce5f44ab, the same machine, instance, pins, save and settings as
section 2), `--warmup on --arms on,B,B,on,on,B,B,on,on,B,B,on --settle 60` (60 s by 7000's rule 5 after the A/A's
block drift), 13:56-14:53 EST. Repetition r of `on` is paired with repetition r of `B` (adjacent under ABBA, each kind
first in three of the six pairs). Fetched to `C:/c5-64bit-work/0095/oldpc-s2/`; evidence in `perf/evidence/0095/step2/`.

| rep | on: idle start, fps | B: idle start, fps | on - B fps | % of B |
|---|---|---|---|---|
| 1 | 14:04:00, 215.3 | 14:08:24, 215.8 | -0.4 | -0.19 |
| 2 | 14:17:12, 213.9 | 14:12:48, 216.1 | -2.1 | -0.99 |
| 3 | 14:21:35, 215.6 | 14:25:43, 216.6 | -1.0 | -0.48 |
| 4 | 14:34:30, 215.6 | 14:30:06, 216.4 | -0.8 | -0.36 |
| 5 | 14:38:53, 215.9 | 14:43:17, 216.8 | -0.9 | -0.42 |
| 6 | 14:51:48, 216.4 | 14:47:25, 216.3 | +0.1 | +0.04 |

Warm-up `W0-H-on` 216.3 fps, not counted.

| kind | n | mean fps | sd | cv | median | min | max | 95 % interval |
|---|---|---|---|---|---|---|---|---|
| on (thin client) | 6 | 215.5 | 0.84 | 0.39 % | 215.6 | 213.9 | 216.4 | 214.6 .. 216.4 |
| B (32-bit pin through the proxy's off switch) | 6 | 216.3 | 0.38 | 0.18 % | 216.4 | 215.8 | 216.8 | 215.9 .. 216.7 |

**on - B: -0.9 fps = -0.40 % of B, sd 0.74 fps, 95 % interval -1.6 .. -0.1 fps (-0.76 % .. -0.04 %), smallest
detectable difference 0.49 % (80 % power, 6 pairs). Grade A** (6 interleaved pairs in one quiet job, load recorded).
By 7000's closing rule the whole interval lies above -3 %: **within 3 %**. The thin client is measurably slower than
the 32-bit DLL on this save, by about 0.4 %, not 7.6 %.

- Mode check: every `on` arm had `server.exe` as a child of the EXE, no `B` arm had one.
- Block drift with the 60 s settle: mean -0.57 fps (first minus last block), largest F2-H-on -7.2 (its first block 208.5;
  the pair's -2.1 fps is the set's largest difference, kept). Window CPU 46.4-50.8 % (on 48.7-50.8, B 46.4-48.2: the
  thin client runs `server.exe` beside the EXE); no arm more than 10 points above the median. Free commit 31.3-31.4 GB
  at each start, 27.8-27.9 GB at each end. Quiet proof: `alone` true, `undisturbed` false, foreign cores 0.143 (svchost
  228 CPU s, MsMpEng 109, audiodg 22, WTabletServicePro 22 over 57 min). The game's window was never foreground (the
  Apple Software Update window was, section 2); `ThrottleOnLossOfFocus = 0`.
- Restore: all by hash, `provision.py verify` rc 0. Ledger: 13 records (warm-up included) ingested 2026-11-15 at about 14:53;
  the 12 counted: on `...190446Z-oldpc-a22394`, `...191757Z-oldpc-a7643f`, `...192221Z-oldpc-1f9911`,
  `...193515Z-oldpc-2f221b`, `...193939Z-oldpc-ebde18`, `...195234Z-oldpc-7667cd`; B `...190911Z-oldpc-c35d43`,
  `...191334Z-oldpc-dfb136`, `...192628Z-oldpc-ae7302`, `...193052Z-oldpc-6e885f`, `...194402Z-oldpc-3b9609`,
  `...194810Z-oldpc-075973` (all `20261115T`).
- The `on` level here (215.5) is above the A/A's (211.9) because of the settle: the A/A's 20 s settle left its first
  block about 12 fps low; its later blocks (214-217) match this set.

**Why the gap is not 7.6 % any more: not attributed** [I]. What changed between 9028 and this set: the builds (B6 89084ac
against the D96 pins with 13 days of proxy and server work), the machine (the workstation against `oldpc` with
an RTX 2080), the window (cloaked, never vsync-bound, against on screen with vsync off) and the spread (cv
5 % against 0.2-0.4 %; 9028's interval was 3.7-11.6 %). No profile was taken: the ticket's step 3 asks for one only if
the gap stays above 3 %. Not excluded either: that at ~216 fps on `oldpc` part of the frame is bound by something the
proxy does not touch (GPU, present), which would hide part of a CPU-side cost of the proxy; the workstation's number on
the same pins was not measured (COMMON 23: only in an MC window).

## 4. Recommendation for the MC (step 3)

**Close 0095.** On the current pins the thin client's idle frame rate on the huge save is within 3 % of the 32-bit DLL
on the quiet testbed, at grade A: -0.40 % (95 % -0.76 % .. -0.04 %). The remaining gap is real but small (0.9 fps at
216) and no follow-up is proposed for increment 1; a profile would be worth taking only if a workstation window (the
product's GPU) showed a larger gap. What is not verified: the workstation's own number; a window with the foreground;
the frame rate during AI turns (only the idle human turn was measured).

Lines for other holders (posted on the 0095 thread, 2026-11-15): `research/06-tooling-and-workflows/quiet-testbed.md`
21.4 `frame-rate series` is now "yes" on `oldpc` through `tools/testbed/jobs/fps_idle.py` (with `WaitForVSync = 0`);
`research/03-dll-engine-boundary/verification-2800.md` section 3's 7.6 % is history of build B6 on the workstation, the
current number is section 3 here.
