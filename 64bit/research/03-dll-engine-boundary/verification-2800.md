# Independent verification of epic 2800 (one-off 9028, 2026-11-11)

Verifier: one-off 9028 (Opus 5.5), not a member of team-2800. Branch `worktree-agent-af80da25359928557`, instance
c5h-c. Subject: `proxy-design.md` section 17 (17.1-17.12) as merged into main at 6337717. Evidence:
`server/evidence/9028/`. Scratch (not committed): `D:/c5-64bit-work/9028/`.

## 0. Verdicts

| # | Claim (proxy-design 17) | Verdict | Section |
|---|---|---|---|
| 1 | The seven findings of the 2700 check are closed; the new tests fail on the old code | confirmed by own measurement (findings 1, 2, 3, 5, 6); 4 and 7 by reading | 2 |
| 2 | Verify mode 0 mismatches live, Tiny and huge, 10 AI turns | confirmed by own measurement | 7 |
| 3 | Bug 0086 fixed: notification texts equal to the 32-bit DLL's | confirmed by own measurement; hover shown for notification texts, not for a diplomacy-response text | 5 |
| 4 | Tiny idle on 437.5 / off 359.6 / B 450.3 fps | confirmed by own measurement (all my means inside the team's intervals); my tighter series finds a 1.5 % gap, not parity | 4 |
| 5 | Crossings: 1.39 M per huge AI turn, 62.1 per idle huge frame | confirmed by own measurement (1.42 M; 57.6-61.6), with a definitional note on 62.1 | 6 |
| 6 | UI-Lua mirror exact, 2.6 % / 6.7 % of candidates, ships off | confirmed by own measurement (default off; 6.6 % huge AI turns) | 6 |
| A | Huge-save frame rate (not run by the team) | on 214.0, off 194.5, B 231.6 fps; on - B -7.6 % (95 % -11.6 .. -3.7) | 3 |
| B | D10 save test, AI logging off in both arms (D49 acceptance) | PASS, all three parts of all three games, wall 20.1 min | 8 |

Defects: D1 (low) flaky `test_proxy_rt` check (section 2); D2 (low, upstream DLL) empty texts of a
`NOTIFICATION_UNIT_DIED` when a civilian is killed instead of captured (section 7); D3 (low) the D10 report does not
state the AI-logging setting (section 8). Documentation corrections made in
place in `proxy-design.md` 17.7, 17.11, 17.12 (dated notes).

## 1. Premises checked first

- [C] Build B6 (`D:/c5-64bit-work/2800/B6`): SHA-256 of `bin/server.exe` 451dce4c..., `bin/CvGameCore_Expansion2.dll`
  7eabd970..., `proxy/bin/CvGameCore_Expansion2.dll` 79627c92... equal `server/evidence/2800/live/build-identity-B6.txt`;
  `build-proxy.json` says `VP_BUILD_REPO_COMMIT 89084ac54`, dirty 0, DLL pin c43ea1b9f, source a plain copy whose
  fingerprint matches the pin. `git diff --stat 89084ac 6337717 -- proxy server ipc deps ':!server/evidence'` lists
  only `proxy/tools/live/fps_table.py` (new) and `server/run_server.py` (+2 lines, a tool): no change to the code that
  is built. B6 is therefore what main builds for every measured part, and all live work below used B6.
- [C] An own build of main (`server/build_server.py --src <archive of the fork at c43ea1b9f> --config release
  --offline`, 576 s, `D:/c5-64bit-work/9028/Bnew`) was used for the offline suite. Its build id says `dirty=1`: the only
  tracked change in the worktree at that moment was this issue's tracker file (no code).
- [C] c5h-c carries `i0ship-ccfd6dd4-ia` (SHA-256 a395cad2..., the instance-aware build of the pin; `instance.json`
  `dll.source`, `provision.py set-proxy` reports `dll.names = instance-aware`); `provision.py verify c5h-c` ok before
  the work.

## 1a. The team's stop by terminate at 2026-11-11 00:16 EST (05:16 UTC), asked by the MC

[L, from the team's logs] It is the stop at the end of arm T-verify (c5h-a, EXE pid 51920, arm 00:09:34-00:15:54):
`python tools/harness/cli.py -i c5h-a stop --human-turn-hook tools.harness.turns:stop_readiness`, rc 0 in 23.7 s
(`server/evidence/2800/live/T-verify/arm.log`, excerpt in `server/evidence/9028/stop-0016/`). The harness's settled
check ran before the terminate: `human_turn: true`, mode `ingame`, "turn 19 active, newest save settled", newest save
`c5h-c5h-a-t0019-001232.Civ5Save` (the checklist's save, mtime 00:13:35.19, 234,238 bytes) stable for 11.42 s; then
`method: terminate`, the server and conhost children exited by themselves (0.29 s, 0.04 s). The last save write was
about two minutes before the stop (00:13:35 against about 00:15:30). The Tiny autosaves of that run (turns 9-19) were
overwritten at 01:32 by arm T-luaverify2, so their state at 00:16 cannot be read any more. [C] Every save of c5h-a now
(`ModdedSaves/single` with `auto` and `auto/prev`, 115 files, the turn-19 save of T-verify among them) unpacks and
repacks byte-identical with `civ5save.exe corpus` (B6): none is truncated or unreadable
(`server/evidence/9028/stop-0016/c5h-a-save-corpus.txt`, 2026-11-11 06:5x). Verdict: the stop followed the harness's
rule (settled check passed, terminate only after it); no damaged save.

## 2. Claim 1: the seven findings of the 2700 check are closed, with tests that fail on the old code

Offline, 2026-11-11 06:28-06:39 EST, all builds release from the same fork archive (c43ea1b9f) and the DLL deps of
Bnew; the hutest host and fake DLL are always main's (built per output folder). Output:
`server/evidence/9028/offline/`.

| Build | What it is | hutest (45 checks) | `--scenario all` |
|---|---|---|---|
| Bnew | main 6337717 | PASS 45/45 | PASS, 7 scenarios as expected (foreign, foreign-strict, leak, leak-strict, outside2, underflow, unlisted) |
| Bnew `--break` | fake DLL perturbs every answer | PASS (15 expected failures, none unexpected) | - |
| Bold | archive of main 39fa514 (before the fixes), its own build_server.py | **FAIL 3**: `canon_after_init_ui` (tag 1000: a canonical handle fetched during Init and kept), `canon_after_init_core` (tag 1000), `dll_no_use_after_free` | **FAIL**: all 7 not as expected (the old code ends every one "passed"; `unlisted`: no LOUD line) |
| Bm1 | main with my own mutation M1 in `proxy/src/proxy_local.cpp` `LockExit`: no crossing on -> 0 | **FAIL 3**: `lock_two_threads_try` (TIMEOUT), `lock_two_threads_release` (ui waited 3000 ms), `canon_after_init_core` (TIMEOUT) | - |
| Bm2 | main with mutation M2 in `LockEnter`: no crossing on 0 -> 1 | **FAIL 1**: `lock_two_threads_try` (ui TryGameCoreLock 1 while core held a local wrapper) | - |

Also on Bnew: `test_strictness.py` 43/43; `test_proxy_rt.py` **54/55 on the first run, 55/55 on the rerun** one minute
later (see defect D1); `build_proxy.py` with `ICvGame1::Update()` marked pure in a scratch copy of
`proxy/model/pure-methods.tsv`: `build_proxy: FAILED: proxy/src/proxy_pure.inc does not match ...`, rc 1 (finding 5);
an archive build with `--repo-commit` logs `repo <x> (archive)` (finding 5). Headless (finding 6), Bnew, strict Tiny
0-50, `local_answers=1 local_verify=1 test_slot_poll=4 lua_mirror=1`: PASS, **1,300,212 getter answers and 1,198,941
Lua hits verified, 0 mismatches** (the same counts as the team's B6 run: the run is deterministic); `local_test=stale`,
`frozen` and `luastale` each end the host with exit code 6 (`stale`: getter mismatch; `frozen` and `luastale`: a
UI-Lua mismatch, which with `lua_mirror=1` comes first). Not repeated by me: finding 4's headless `outside1/2` runs
(the hutest scenario `outside2` passed), finding 7 (recipe text; read, `arm.py` reads the start-up watch and the guard
log and stops on a hit, confirmed by reading).

**Verdict: confirmed by own measurement** for findings 1, 2, 3, 5 and 6 (headless), confirmed by reading for 4 and 7.

**Defect D1 (low): `test_proxy_rt.py` check "strict (negative): strict_allow naming a missing file fails the boot" is
flaky.** First run on Bnew 06:33: FAIL with `loader: FAIL: a child process runs after a failed boot` although the boot
failed at 0.0046 s, before any server start; rerun 06:34 PASS. `proxy/tools/rt_tests/loader.cpp` `Children()` takes
every process whose `th32ParentProcessID` equals the loader's pid; Windows reuses pids, so a process whose (dead)
parent had the same pid counts as a child [I: the likely cause, not reproduced]. Fix: also require a creation time
after the loader's (`GetProcessTimes`), or track children by job object.
*Bug 0097, 2026-11-13 (bugfix 4000):* cause confirmed by reading [C] with the condition measured [L] (40 of 484 live
processes named a parent id no live process had); fix in `loader.cpp` `Children()`: a candidate created before the
loader is not its child. 20 runs in a row 07:48-07:58 EST: 20 of 20 passed, 55/55 checks each, 27-31 s per run
(`--build D:/c5-64bit-work/9043/b1`, the build of the p1607d42 layout; the pin folders lack `gen/` and cannot be used).
No run hit the reuse case (no "ignored" line), so the fixed branch is compiled and runs but was not exercised [I].

## 3. The huge-save frame rate (series A, the team's specification unchanged) [L]

**Method.** Instance c5h-c, build B6 (section 1), save `C5H-VP8P-HUGE_0350`, `config.ini LoggingEnabled = 0` (set
06:19 after a SHA-256 backup; restored at the end), every arm by `proxy/tools/live/arm.py` exactly as
`server/evidence/2800/fps-huge-spec.txt` lines 3-20 (`--idle 45`: load, close the load screen, statistics dump, 45 s
wall-clock window of the Lua frame counter on the human's turn with nothing open, stop). Arms: **on** (`local_answers=1`,
`lua_mirror=0`), **off** (`local_answers=0`), **B** (`mode=off`, the 32-bit `i0ship-ccfd6dd4-ia` through the off switch),
order rotated per repetition as in the spec. Runner: my own (`series9028.py`, adapted from the team's, scratch copy
in `server/evidence/9028/fps/`), every arm inside a hold of mine taken with `perf/quietlock/quietlock.py take
--job-wait 10` (no `--overlap`), at most 28 min per hold, 10.5 min gaps, a check for any other game EXE before each
take, free commit (start >= 8 GB, 5 s minimum sampling, discard below 3 GB) and machine CPU every 5 s recorded.
Statistics: my own script (`fps_stats.py`: fps recomputed from the raw frame counts and wall seconds of `idle.json`,
t intervals) and, as a cross-check, the team's `proxy/tools/live/fps_table.py`; both give the same numbers to 0.1 fps.

**Holds** (lock history `D:/c5-64bit-work/locks/quiet-machine.history.jsonl`, `server/evidence/9028/fps/holds.txt`):

| Hold | Arms | disturbed | machine CPU mean / max (lock monitor) | other new processes |
|---|---|---|---|---|
| 07:46:54-07:46:56 | none (the first arm failed to launch: harness helpers not built in my worktree; no game started) | false | - | - |
| 07:57:11-08:20:43 | F1-H-on, F1-H-off, F1-H-B, F2-H-off, F2-H-B | false | 13.4 % / 28.7 % | the game's audiodg, a Chrome renderer (5.6 %), python helpers of mine |
| 08:31:14-08:52:46 | F2-H-on, F3-H-B, F3-H-on, F3-H-off, F4-H-on | false | 13.5 % / 29.0 % | audiodg, Chrome renderers (5.6 %, 17.3 % once) |
| 09:03:16-09:11:53 | F4-H-off, F4-H-B | false | 13.7 % / 38.1 % | audiodg |
| 10:30:11-10:55:47 | F5-H-off, F5-H-B, F5-H-on, F6-H-B, F6-H-on | false | 15.6 % / 40.2 % | a `git fetch` 10:33:49-10:34:14 (index-pack 7.9 %, then 59.8 % of one CPU at 10:34:14, inside the last second of F5-H-off's window 10:33:24-10:34:09), `pool.py list` 10:35:19 (100 % of one CPU, 5 s, between windows), MoUsoCoreWorker |
| 11:06:18-11:32:12 | F6-H-off, F1-T-on, F1-T-off, F1-T-B, F2-T-off, F2-T-B | false | 15.5 % / 35.8 % | svchost 11:16:47, msedge prefetch 11:23-11:24 (between windows) |
| 11:42:43-11:59:57 | F2-T-on, F3-T-B, F3-T-on, F3-T-off | false | 15.0 % / 32.3 % | audiodg |

No other team's game or server ran in any hold; no take had to wait more than 2 min. Free commit at every arm start
35.8-40.1 GB, minimum during any arm 32.0 GB (F5-H-B): no arm near the 3 GB discard line. No arm was rerun.

**Table** (`server/evidence/9028/fps/huge-own-stats.txt`; CPU = mean machine CPU, all cores, over the 45 s window):

| Arm | fps | CPU % | | Arm | fps | CPU % | | Arm | fps | CPU % |
|---|---|---|---|---|---|---|---|---|---|---|
| F1-H-on 08:00 | 203.9 | 25.5 | | F1-H-off 08:06 | 197.1 | 22.6 | | F1-H-B 08:10 | 235.7 | 24.0 |
| F2-H-on 08:34 | 223.8 | 23.5 | | F2-H-off 08:15 | 194.1 | 21.5 | | F2-H-B 08:19 | 234.2 | 23.3 |
| F3-H-on 08:42 | 224.5 | 23.3 | | F3-H-off 08:47 | 199.8 | 22.4 | | F3-H-B 08:38 | 243.3 | 23.2 |
| F4-H-on 08:51 | 224.7 | 23.4 | | F4-H-off 09:06 | 197.6 | 22.4 | | F4-H-B 09:10 | 231.9 | 23.7 |
| F5-H-on 10:44 | 205.9 | 27.6 | | F5-H-off 10:33 | 197.5 | 27.3 | | F5-H-B 10:39 | 227.6 | 26.4 |
| F6-H-on 10:54 | 200.9 | 25.7 | | F6-H-off 11:09 | 181.2 | 26.9 | | F6-H-B 10:49 | 216.8 | 26.0 |

| kind | n | mean fps | sd | median | min | max | 95 % interval of the mean |
|---|---|---|---|---|---|---|---|
| on (local answers) | 6 | 214.0 | 11.5 | 214.8 | 200.9 | 224.7 | 201.9 .. 226.0 |
| off (every call crosses) | 6 | 194.5 | 6.8 | 197.3 | 181.2 | 199.8 | 187.4 .. 201.7 |
| B (32-bit DLL) | 6 | 231.6 | 8.9 | 233.1 | 216.8 | 243.3 | 222.3 .. 241.0 |

Paired by repetition: **on - B** = -31.8, -10.4, -18.9, -7.2, -21.8, -15.8 fps; mean **-17.6 fps (-7.6 % of B)**, sd 8.7,
95 % interval **-26.8 .. -8.5 fps (-11.6 % .. -3.7 %)**. **off - B** = -38.7, -40.1, -43.5, -34.3, -30.1, -35.6; mean -37.1
fps (-16.0 %), 95 % -42.0 .. -32.1 (-18.1 % .. -13.9 %). **on - off** = +19.4 fps (+10.0 % of off), 95 % +9.2 .. +29.6.
The detectable difference (half-width of the 95 % interval) is 9.2 fps (4 %) for on - B.

**Statement.** On the huge save at turn 350, idle, the proxy with local answers runs **7.6 % below the 32-bit DLL (95 %
interval 3.7 % to 11.6 %)**: unlike Tiny, the gap is real at this resolution, the interval excludes parity. Without local
answers the gap is 16 % (14-18 %). Local answers gain 10 % on this save (Tiny: 21 %). [L, B6 89084ac, 2026-11-11]

*Corrected 2026-11-15 by issue 0095: the 7.6 % above is build B6 on the workstation (cloaked window, cv about 5 %) and is
history. On the D96 pins (x64 `x64-04b1211b-pfdee656`, 32-bit `i0ship-26234371-ia`) on `oldpc`, quiet, six ABBA pairs,
the thin client is **-0.40 %** of the 32-bit pin (95 % -0.76 % .. -0.04 %, grade A, within 3 %):
`research/08-performance/idle-frame-rate.md` section 3. The B6 numbers stay as the record of that build.*

Observations, not explained [I]: the on arms fall in two groups (203.9, 205.9, 200.9 against 223.8-224.7); the low three
are the first arm after the guard wrapper was reinstalled (F1, cold file cache, 6.3 min against 4.3 for every later
arm) and the two arms of the later block, whose windows ran at 25.5-27.6 % machine CPU against 23.3-23.5 % for the high
group; B and off also fell in the later block (F5, F6), so the paired differences hold. The machine was busier after
10:30 (other teams' work outside my holds' registered jobs is not visible to the lock; the lock monitor's mean rose from
13.5 % to 15.6 %). Idle crossings per huge frame: section 6.

## 4. Claim 4: Tiny idle frame rate, three interleaved repetitions per arm [L]

Same method and build, save `c5h-9028-tiny-t0008` (a copy, made 06:21, of c5h-a's `c5h-c5h-a-t0008-205410.Civ5Save`, the
file the team's `--load c5h-c5h-a-t0008` picked as the newest match), holds 11:06-11:32 and 11:42-12:00 above.

| kind | n | mine: mean (sd), 95 % interval | the team (17.11): mean (sd), 95 % interval | mine inside the team's interval |
|---|---|---|---|---|
| on | 3 | 439.5 (1.8), 435.1 .. 443.8 | 437.5 (12.1), 424.8 .. 450.2 | yes |
| off | 3 | 363.9 (2.3), 358.2 .. 369.7 | 359.6 (17.5), 341.2 .. 378.0 | yes |
| B | 3 | 446.4 (2.6), 439.8 .. 453.0 | 450.3 (17.0), 432.4 .. 468.2 | yes |
| on - B, paired | 3 | -6.9 fps (-1.5 %), -12.5 .. -1.3 (-2.8 % .. -0.3 %) | -12.9 (-2.8 %), -32.3 .. +6.6 (-7.2 % .. +1.5 %) | yes |
| off - B, paired | 3 | -82.4 (-18.5 %), -94.0 .. -70.9 | -90.8 (-20.1 %), -107.6 .. -73.9 | yes |

Arms: F1-T-on 437.8, F1-T-off 366.5, F1-T-B 443.6, F2-T-off 362.0, F2-T-B 446.7, F2-T-on 441.3, F3-T-B 448.8, F3-T-on 439.3,
F3-T-off 363.4 fps; machine CPU 26.9-30.6 %. **Verdict: confirmed by own measurement**: every mean of mine lies inside
the team's interval. My spread is far smaller (sd 1.8-2.6 against 12-17), so my small series resolves what theirs could
not: on Tiny the proxy with local answers is **1.5 % below the 32-bit DLL (0.3 % to 2.8 %)**, a small real gap, not
parity. Note on the team's table: its repetition 6 pairs F6-T-off (04:41, another hold) with F6-T-B (03:50); the text
"same hold, consecutive arms" does not hold for that pair (their numbers are unaffected otherwise; reproduced exactly from
their `idle.json` files with my script).

## 5. Claim 3: bug 0086, notification texts through the server [L]

Arm H-traffic (12:00-12:12, B6, huge, `local_answers=1`, verify off, 6 AI turns 350 -> 356 by the checklist, every step
pass, save `c5h-c5h-c-t0356-120340.Civ5Save` on c5h-c): the run folder's `vpproxy.log` names the attachment of the
engine's `Localization-Merged.db`. `server/tools/notif_texts.py` against the team's 32-bit save of the same six turns
(`c5h-a/.../c5h-c5h-a-t0356-004659.Civ5Save`, arm H-B, i0ship-ccfd6dd4-ia): **894 notification records in each, 0 empty
messages, 894 equal, 0 different**. The UI's own list (`proxy/tools/live/notifications.lua`, 149 notifications, turn
356) is byte-identical to the team's H-B list, 0 empty messages; it includes the diplomacy-response texts that were
empty before the fix ("Germany announces to the world that it is now protecting Kyzyl. ...", indices 43-44, 95-96).
Evidence `server/evidence/9028/func/`.

**Hover screenshot (the part the team did not do).** Taken through the harness on the hidden desktop
(`cli.py move` = a posted `WM_MOUSEMOVE`, `cli.py shot` = PrintWindow; no focus): `server/evidence/9028/hover/` shows the
notification panel's tooltips "Empire-Wide Bonus Yields / Expanding Borders: Heliopolis +60 Food ...", "Suleiman has made
peace with Edinburgh! ... The allies or vassals of Suleiman have also made peace." and the stacked city-state ally texts,
each the message text drawn by the EXE's UI from the server's answers. Not achieved in the 10 minutes I gave it: a hover
of one of the "Public Declaration" notifications (the texts that came from `GetDiploResponse`): the EUI panel stacks
notifications by type and its scroll list showed only the newest groups; the declarations' texts are shown equal through
the UI's own calls above. Practical note for the next screenshot: after the checklist the game shows a queue of popups
(wonder, technology, new era, two great persons, golden age); each had to be closed with a posted click on its own button
before the panel took hovers (`UI.IsPopupUp()` false), ESC posted as a key did not close them.

**Verdict: confirmed by own measurement** (save records and UI list equal to the 32-bit DLL's; a hover of a notification
text through the harness shown; a hover of a diplomacy-response text specifically not shown).

## 6. Claims 5 and 6: crossings and the UI-Lua mirror [L]

Own counting (`crossings.py`): from the proxy's own per-method counters (`vpproxy-snapshot-methods.tsv`, out + in, the
snapshot window around the idle measurement) and, for AI turns, the server's per-turn counters summed by my own code.

| Quantity | Mine | The team's (17.7) |
|---|---|---|
| Idle huge frame, local answers on, crossings (proxy -> server + server -> proxy) | 57.6-61.6 per frame over six arms (proxy -> server alone 56.3-60.2); 586.8-587.0 answered locally; 16.0 `rt:LockSync` | 62.1 |
| Idle huge frame, local answers off | 627.1-631.1 | - |
| Idle Tiny frame, on / off | 34.7-35.3 / 590.1-590.3 | about 31 / 590.2 (16a) |
| Huge AI turn, local answers on, server counters, turns 351-355 | **1,420,032** mean (1.67, 1.38, 1.32, 1.33, 1.40 M) | 1.39 M (1,393,317 recounted from their H-on-a with my code) |
| Huge AI turns window, proxy counters (6 AI turns plus the checklist's UI steps and save) | 9,460,460 crossings, 1.58 M per AI turn, 1,009.7 per frame (out alone 849.4) | 926.7 per frame |

The team's "62.1 per idle frame" and "926.7 per frame during the AI turns" count proxy -> server calls only (their H-on-a:
539,760 / 8,695 = 62.08; 7,826,178 / 8,445 = 926.7), while "1.39 M per AI turn" counts both directions (server counters):
the two numbers of 17.7 use different definitions; both directions on their own H-on-a give 63.5 per idle frame. My idle
frames that ran faster had fewer crossings per frame (57.6 at 224 fps against 61.5 at 201-206 fps): part of the idle
traffic is per unit of time, not per frame [I]. **Verdict claim 5: confirmed by own measurement** (AI turn within 2 %; idle
frame within the definitional difference above).

UI-Lua mirror (claim 6): `lua_mirror` defaults to 0 in the code (`proxy/src/proxy_rt_logic.cpp` `luaMirror(false)`),
`tools/instances/provision.py set-proxy` writes no `lua_mirror` key (the `vpproxy.ini` it wrote on c5h-c, 06:20, has none)
and `arm.py` writes `lua_mirror=0` unless told otherwise; every shipped path keeps it off [C]. In shadow mode my arms count
would-hits: huge idle 2,119 of 85,788 candidate calls (2.47 %) in every on arm; huge 6 AI turns (H-traffic) 156,806 of
2,383,397 (**6.58 %**); Tiny idle 1,076 of 10,167 (10.6 %). The team's 2.6 % (Tiny, 10 AI turns) and 6.7 % (huge, 10 AI turns)
recomputed from their logs as hits / (hits + misses): 2,814 / 106,375 = 2.65 %, 257,021 / 3,814,398 = 6.74 %. Exactness:
1,198,941 Lua hits verified with 0 mismatches headless (section 2); live exactness was not rerun with `lua_mirror=1` (it
ships off). **Verdict claim 6: confirmed by own measurement** (default off; huge AI-turn share 6.6 % own against 6.7 %).

## 7. Claim 2: verify mode live, 10 AI turns, Tiny and huge [L]

B6, c5h-c, `local_answers=1 local_verify=1`, `lua_mirror=0`, `config.ini LoggingEnabled = 0`, checklist with 10 AI turns
(every step pass), guard and start-up watch ok (`server/evidence/9028/func/`):

| Run | Getter answers verified | GetScriptSystem verified | Mismatches | Lock errors / outside a served call / canonical refusals |
|---|---|---|---|---|
| T-verify, `c5h-9028-tiny-t0008`, turns 8 -> 19, 12:12-12:18 | 5,363,129 | 48,874 | 0 | 0 / 0 / 0 |
| H-verify, `C5H-VP8P-HUGE_0350`, turns 350 -> 360, 12:18-12:29 | 2,173,969 | 1,048,303 | 0 | 0 / 0 / 0 |

(Team: Tiny 4,761,498 / 48,796; huge 2,433,107 / 1,049,059; build B3, `LoggingEnabled = 1`.) Each server report ends
`result: FAILED: the engine is gone: ... PeerDied`: the harness stops the EXE by terminate after its settled-save check,
and the team's reports end the same way; it is not a failure of the run. **Verdict: confirmed by own measurement.**

**Finding D2 (low, upstream DLL, not the proxy): a notification with empty summary and message.** H-verify's UI list at
turn 360 has one entry (index 27, turn 356) with both texts empty; the save record (`c5h-c5h-c-t0360-122239`, body
offset 15,002,922 + 33) is type ff07247a (`NOTIFICATION_UNIT_DIED`), player 0, plot (93,35), gameData 7, following "A
Civilian was captured!" [C]. Cause [C, by reading the DLL source]: `CvUnit.cpp` 20410-20442 at c43ea1b9f (20435 at the
32-bit ship pin ccfd6dd4d): in the branch where a civilian is killed instead of captured, `Localization::String
strMessage = Localization::Lookup("TXT_KEY_UNIT_LOST"); Localization::String strSummary = strMessage;` declares NEW
variables that shadow the outer ones, so `pNotification->Add(NOTIFICATION_UNIT_DIED, strMessage.toUTF8(),
strSummary.toUTF8(), ...)` sends the outer, empty strings. The same code is in the 32-bit DLL, so the 32-bit game shows
the same empty entry [I: not run live with the 32-bit DLL to turn 360]. Not bug 0086. Fix (one line each): assign instead
of declare. For the MC's upstream-fix list.

## 8. Series B: the full three-game save test with the new default (D49 acceptance) [L headless]

`python perf/quietlock/quietlock.py run --who 9028 ... -- python tools/d10/run_d10.py --out D:/c5-64bit-work/9028/d10
--x86-dll <pins_dir>/dlls/i0ship-ccfd6dd4/CvGameCore_Expansion2.dll --srv-bin <pins_dir>/dlls/x64-c43ea1b9/bin`
from main 6337717 (+ docs-only merge of main 3a87070), 2026-11-11 12:29:01-12:49:08, **wall 1207 s (20.1 min)**, one
registered job, no hold, no other job of mine at the same time. `--ai-logging` left at its default `off`: every x86 host
command has `--host-arg=--no-game-logging`, every x64 command `--dll-log-flags 0` (the run's `*command.json` files).
Builds: the pins (x86 i0ship-ccfd6dd4 3e1a7fc7..., x64 c43ea1b9 DLL 7ea44fc3..., server.exe 9bbce03e...), D18 on both.

| game | part 1 writer | part 2 behaviour | part 3 cross-load | wall |
|---|---|---|---|---|
| tiny | PASS | PASS | PASS | 216 s |
| standard | PASS | PASS | PASS | 577 s |
| huge | PASS | PASS | PASS | 1192 s |

**Result: PASS** (exit 0), every part of every game. Report: `server/evidence/9028/d10/report.txt`, `report.json`. This is
the acceptance run of decision D49 (AI logging off in both arms, behaviour identical).

Defect D3 (low): `report.txt` does not state the AI-logging setting; PERFORMANCE.md section 2 wants every number to
say logging on or off. It can be read only from the host command files. Fix: one line in `report_text`.

## 9. Not verified, deviations

- Not run: finding 4's headless `outside1/2` runs (the hutest scenario `outside2` passed); a live run with `lua_mirror=1`
  (it ships off; headless exactness was repeated); a 32-bit live run to turn 360 for defect D2 (cause read in the source).
- A hover of a diplomacy-response notification text (section 5).
- Deviation: the series started 07:57, not 07:45 (the first take was spent on a launch that failed because my worktree
  had no built `tools/harness/native/bin`); block A then ran 12 arms instead of 9 because arms took 4.3 min, so block B
  started with F5. Arm budgets in my runner were 8 min (block A) and 5 / 4 min (block B); hold lengths stayed below 26 min.
- Deviation: three function runs and the D10 ran after the series without a hold (as planned); the hover pause kept the
  H-traffic game at the human's turn for about 6 min.
- The guard wrapper of c5h-c was reinstalled from current sources at 07:47 (`wrapper.py install`) and stays.
