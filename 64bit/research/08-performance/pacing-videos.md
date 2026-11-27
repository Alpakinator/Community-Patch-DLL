# Pacing videos: the end of an AI phase with the D165 options (ticket 0567, D191)

The owner (2026-11-18, D191) asked for a GIF or MP4 of some movement with option 4 (`[Performance] AiDrainUnordered`) on and
off, Quick Movement on, before they decide option 4's default. This file says how the videos were made and what they
show. The options and their measurements are in `turn-time-study.md` ("Option 2 built", "Option 3 built", "Option 4
built") and `d165-pacing-fix-design.md`; this file adds no timing claim (PERFORMANCE.md: one functional run per arm,
grade D). Marks: `[L]` seen live in the recorded runs, `[C]` read in our source or the DLL source at the pin, `[I]`
inferred.

## 1. Set-up `[2026-11-18]`

- **Build**: ticket 0563's packed batch-4 client build (arm S4b4), main's code with the D165 options 2, 3 and 4 (off by
  default) on DLL pin batch 4: server.exe 2BC94943, the proxy 631E4C3E, vp64helper.exe 2AF96926 (built by 0563 with
  `server/build_server.py --config release --offline` at ab6c50709, no product file differing from main a13b82271), the
  x64 DLL 2B56A1CD of the pin layout `x64-c29442e8-p58724e0`, packed by `tools/release/pack.py` (pack=1, the increment
  1 RC's vpproxy.ini text, the tag's DLL 262aee88 as the fallback, thin: no compositor), manifest.json B408DFFD
  (x64_dll 2B56A1CD, purity 10D7EB41). This ticket ran its own copy, `C:/c5-64bit-work/0567/layout`, checked file by
  file against 0563's `SHA256.txt` (`perf/evidence/0567/copy_layout.sh`). NOT a player's build: the options are set
  per run in the instance's `VP64Options.ini` and restored by hash.
- **Game**: scenario M (`TS0438-M-T150`, Standard Communitu_79a, save turn 150, cp,vp,eui, statistics logging on, AI
  logging off, 1920 x 1080 windowed), Quick Combat on, Quick Movement on or off per arm (off by the in-game options call
  before the arm, read back), autoplay `Game.SetAIAutoPlay(3, 0)` from the loaded turn 150 with the observer following
  player 0 (`Game.SetObserverUIOverridePlayer(0)` before the arm, read back 0: the observer sees what player 0's team
  sees, so the DLL teleports the moves player 0 cannot see, as for a human, `turn-time-study.md` 4678). Autoplay plays
  turns 150, 151 and 152 and hands the game back to player 0 at turn 153: the recorded AI phase is turn 152's (in
  autoplay player 0 is played by the AI inside that phase too), the human turn is 153. The camera is where the load puts
  it; nothing moves it by command.
- **Machine**: oldpc (headed test machine; D63: nothing hidden), its instance tb1 with the machine's own EXE, one
  headed functional job through `tools/testbed/tb.py` that took both slots (nothing else ran beside it).
- **Arms** (variants of M added in memory by `perf/evidence/0567/vidlib.py` from `arms_0567.json`; the runner's
  `arms.json` and `M.json` are unchanged):

| arm | [Performance] | Quick Movement |
|---|---|---|
| (c) stock pacing `v0567_stock` | AiUpdateTickMs -1, AiSkipVisualDrain 0, AiDrainUnordered 0 | on |
| (a) options 2 + 3 `v0567_drain0` | AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 0 | on |
| (b) options 2 + 3 + 4 `v0567_drain1` | AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1 | on |
| (a) QM off `v0567_qmoff_drain0` | AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 0 | off |
| (b) QM off `v0567_qmoff_drain1` | AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1 | off |

## 2. How the frames were taken `[C, perf/evidence/0567/]`

- `vidjob.py` runs the turn-study series job unchanged (`perf/turnstudy/runner/series_job.py`, `clientarm.py`: backup
  and restore by SHA-256, the dialog watcher, the stop with the human-turn hook, `provision.py verify`) with three
  additions made from outside its code: (1) the arm and variants above; (2) beside 4661's read-only pacing poller,
  `recorder.py` records the game window from the poller's start (the game is in, before the arm) to its stop; frames
  older than 4 s before the first sight of `AutoSave_Post_0152` (turn 152 begins) or newer than 10 s after the
  hand-back are deleted on the machine, so only the last AI phase and the human turn's start travel back; (3) after the
  window, autoplay's hand-back is awaited by a Lua channel poll every 0.5 s (`Game.GetGameTurn()`,
  `Game.GetAIAutoPlay()`, the active player's `IsTurnActive()`; the runner's own wait polls every 2 s); the hand-back
  time is the midpoint of the first answer showing turn 153, autoplay 0 and the turn active; then 8 s of the human turn
  are recorded before the runner goes on. So turn 152's AI phase is recorded with a channel call every 0.5 s in every
  arm alike [I: a small load on the main thread; not measured].
- **Autoplay writes no autosave at the human turn** `[L, job 20261118-191837-e917, oldpc, 2026-11-18 20:20-20:59]`:
  after `AutoSave_Post_0152` no autosave appeared in 600 s although the channel then showed turn 153, autoplay 0 and
  player 0's turn active at once (`AutoSave_Post_N` is written when the human ends turn N, `memory-sampler.md`). The
  first form of the job waited for one and kept 10 minutes of frames per run; that job was stopped after 3 runs and is
  not used.
- `recorder.py`: one frame every 1/12 s of the wall clock (a late tick is skipped, never caught up), the window's
  client area copied from the screen by GDI `BitBlt` (Windows' own API; the composed picture the player sees) while the
  window is on the screen and the window under its centre, otherwise the harness's PrintWindow
  (`tools/harness/winapi.py capture`); scaled to 960 px wide, JPEG quality 85; `frames.tsv` has each frame's wall time,
  capture time and source. On the workstation (2026-11-18, a loaded machine, a 1920 x 1032 window) one screen copy took
  52 ms (p50, 20 tries) and one PrintWindow 46 ms; a PrintWindow of a large desktop window took about 200 ms.
- `makevideo.py`: the panels start together 1 s before turn 152's AI phase began and play in real time (output frame k
  shows each panel's newest frame taken at or before start + k / 10 s), so the arm whose AI phase ends first reaches
  its human turn first; each panel's caption names the arm, the build and the turn, and the phase line counts the
  seconds since the AI turn began, then since the human turn began. Turn changes are taken from the autosaves' first
  sight (up to 0.1-0.5 s late). MP4: OpenCV's Media Foundation writer (`cv2.CAP_MSMF`, fourcc H264: Windows' own
  H.264 encoder; read back as `h264`, 10 fps; OpenCV 4.9.0 as already installed in the workstation's user
  site-packages, so the script runs without `-I`); GIF: Pillow 9.4.0 of the Anaconda install (128 colours per frame).

## 3. Results `[L, oldpc, job 20261118-205945-55fa, 2026-11-18 21:00-21:17 EST, instance tb1, screen 1920 x 1080, one run per arm in the order (a), (b), (c), (a) QM off, (b) QM off, 30 s idle before each; grade D: functional, one run each, no timing claim]`

Every run exit 0, restored (backup by hash, `provision.py verify` rc 0), no dialog box, the observer read back 0 in
every run, Quick Movement read back off in the two QM-off runs. Recorder: 12.0 frames a second achieved in every
run, every frame a screen copy (capture p50 34-35 ms), no late tick; 199-305 frames kept per run.
`perf/evidence/0567/runs-55fa.txt` (`summarize.py`), `series-55fa.json`.

**The game is the same in every arm**: `tools/d10/statscmp.py --from-turn 150` IDENTICAL for (a)/(b), (c)/(a),
(c)/(b), the QM-off pair and (a)/(a) QM off (14 tables, 9,874 rows, turns 150-153; `statscmp.txt`).

**The videos** (MP4 H.264, 10 frames a second, real time; each panel captioned with the arm, the build and the turn,
and a phase line counting the seconds since turn 152's AI phase began, then since the human turn began):

| file | panels | size | SHA-256 |
|---|---|---|---|
| `F:/c5-64bit-work/0567/videos/0567-qm-on-23-vs-234.mp4` | QM on: (a) options 2 + 3, (b) options 2 + 3 + 4; 1280 x 456, 13.3 s (133 frames) | 6.9 MB | 43cc0a80...6d33e |
| `F:/c5-64bit-work/0567/videos/0567-qm-on-stock-23-234.mp4` | QM on: (c) stock, (a), (b); 1440 x 356, 17.1 s (171 frames) | 8.4 MB | 42f95c98...1ef68d |
| `F:/c5-64bit-work/0567/videos/0567-qm-off-23-vs-234.mp4` | QM off: (a), (b); 1280 x 456, 18.5 s (185 frames) | 9.8 MB | 8c181620...bd8a675 |
| `F:/c5-64bit-work/0567/videos/0567-qm-on-23-vs-234.gif` | the QM-on pair as a GIF (panels 320 px wide) | 10.2 MB | 2c8f2e36...4c629f |
| `F:/c5-64bit-work/0567/videos/0567-qm-off-23-vs-234.gif` | the QM-off pair as a GIF | 13.3 MB | b75f2e0f...69fb06 |

(Pictures are not stored in git (COMMON 27, the owner); all five files are on F: under F:/c5-64bit-work/0567/videos/. The MP4s were committed by mistake in 448404bc9 and removed from the tree on 2026-11-18; they remain in that commit's history.)

**Turn 152, the recorded AI phase, per arm** (one run each on oldpc: an indication, not a result):

| | (c) stock | (a) 2 + 3 | (b) 2 + 3 + 4 | (a) QM off | (b) QM off |
|---|---|---|---|---|---|
| AI phase, `AutoSave_Post_0152` first seen to the hand-back seen by the channel (± half the poll's round trip) | 10.00 s (± 0.03) | 6.22 s (± 0.75) | **2.60 s** (± 0.53) | 11.33 s (± 0.76) | 11.41 s (± 0.81) |
| the turn by the poller (`pacing.py --turns 152-152`, request to request) | 9.12 s | 6.80 s | **3.10 s** | 12.08 s | 12.19 s |
| queue B after the last AI player's Update | 24 KB, drained in 0.32 s | 445 KB, drained in **3.94 s** | 445 KB, turn change requested **0.47 s** later | 530 KB, drained in 9.27 s | 530 KB, turn change 9.27 s later |
| frames with a change of the map area (`framediff.py`, mean grey difference >= 1) during the phase | 5, at 0.5-1.2 s | 7, at 1.9-2.6 s | 3, at 1.8-2.0 s | 26, at 1.6-6.3 s | 24, at 1.7-6.3 s |
| the camera's jump to player 0's unit (the first frame with a difference > 30) | 11.6 s | 7.0 s | **3.5 s** | 12.1 s | 12.2 s |

(`pacing-turn152.txt`, `framediff-55fa.txt`.) The channel answered with a round trip of about 1-1.6 s during the
options-2 AI phases (a main thread busy with the drain's frames) and at once in stock pacing.

**What the videos show** `[L, the recorded frames; I where marked]`:

- **Quick Movement on, (a) against (b).** Both panels show the same picture for the first 1.8 s (the "Please wait"
  button, the "Processing turn for ..." banner). In (a) the units in view change in a short burst at 1.9-2.6 s, then the
  picture stands still for about 4 s with "Processing turn for Barbarians" while queue B's 445 KB backlog drains (3.9 s
  by the poller) before the turn changes; in (b) the burst is at 1.8-2.0 s and the turn changes at about 2.6 s, so the
  still wait after the burst is about 0.6-1.5 s instead of about 4 s. Once both are in the human turn the map content
  is the same (the same units on the same plots at this resolution, the same "Choose production" and unit panel), and
  the game is identical (statscmp). No unit is seen in a wrong place or with a missing model in either arm [L, at
  960 px frames; finer checks: 4675's pictures].
- **(c) stock pacing.** The units in view move early in the phase (0.5-1.2 s), then about 10 s pass with "Please wait"
  while the AI players' turns go by one at a time (queue B is drained after each player, so only 24 KB are left at the
  end); the human turn comes at about 10-11.6 s.
- **The start of the human turn** is the same in every arm: the camera jumps to player 0's unit (Askia's Pikeman) and
  for about 1-2 s the terrain where it lands is grey and untextured until it loads, also in stock pacing; this is the
  EXE's own behaviour on a camera jump (4675's `grayscan.py` saw the same with option 4 off), not an effect of the
  options [L, every arm].
- **Quick Movement off, (a) against (b): no difference**, as expected. Both panels play the same animated moves at the
  same times (26 and 24 changing frames at 1.6-6.3 s), the same 530 KB backlog drains in 9.27 s and the human turn comes
  at 11.3 / 11.4 s. The proxy's lines say why: in (b) `pacing: option 4 on: ... clears queue B's ordering byte ... when
  Quick Combat and Quick Movement are on ...`, and the counters line `end-of-phase drains 1, cleared 0, set back 0, not
  cleared: Quick Combat off 0, Quick Movement off 1, ...`: the guard kept the byte set. In (b) with Quick Movement on the
  same line reads `end-of-phase drains 2, cleared 2, set back 2, not cleared: ... 0, ...; records walked 6802 (max
  3955)`. (Each run's log holds one counters line, written 60 s after the proxy's start, so it counts the drains of
  turns 150 and 151 up to then, not turn 152's; the harness ends the EXE by terminating it, so the unload line is not
  written: `pacing-lines.txt`.) Options 2 and 3 were on in every options run (`option 2 counters: state active ...`,
  `on: [Performance] AiUpdateTickMs = 0 ...`), and stock pacing installed nothing (`option 2 off ... nothing read or
  written`, `off ([Performance] AiUpdateTickMs = -1): nothing installed`).

Visual Checks feed (`--issue 0567`, `--result info`, every `--what` starting "DIAGNOSTIC (D112"): one still of each
video, vc-20261118-212856-oldpc-5274 (the three-panel QM-on video at 3.5 s), vc-20261118-212857-oldpc-0b92 (the QM-on
pair at 3.5 s), vc-20261118-212858-oldpc-3c57 (the QM-off pair at 11.0 s).

## 4. Not verified

- Every time above is one run on oldpc with a recorder and a channel poll running beside the game: an indication of
  what the player sees, not a timing result (PERFORMANCE.md; the laptop pairs of `turn-time-study.md` and 0563 are the
  timing results: on batch 4 stock 14.33 s, options 2 + 3 + 4 4.64 s per M turn).
- The recorder's own cost to the game's frames (a screen copy of 34 ms twelve times a second on another process) and
  the channel poll's cost to the AI phase were not measured; they are the same in every arm.
- The hand-back time is the channel's (± 0.5-0.8 s in the options arms); the camera jump in the frames comes 0.7-1.6 s
  after it, so the videos' "HUMAN TURN" label appears slightly before the picture changes.
- The recorded AI phase is turn 152's in autoplay, where player 0 is also played by the AI inside the phase; in a human
  game the AI phase follows the human's own moves (scenario H of 0437, not run here). One camera only (where the load
  puts it); a turn with an AI leader's message (the hold), Quick Combat off and other maps were not recorded.
- Whether the final map is the same in every pixel after the drain: compared by eye at 960 px and by statscmp (game
  state), not by a pixel compare of the two arms' frames (their cameras jump at different times).
