# Late-game models flash for one frame on a small pan (ticket 0788, beta 5 gate, D266)

Written 2026-11-26 by impl-0788 (account B, branch `task-0788-lategame-model-flash`, ticket 0788 under lead-0673).
Scripts: `server/evidence/0788/`. The owner's video and save are copies on F: (`F:/c5-64bit-work/0788/in`, SHA256.txt
beside them); no picture, video or save is in git (COMMON 27). Mark convention: `[C]` code read or the diagnostic's own
lines, `[L]` live run named, `[I]` inferred. Related: `pan-coherence.md` 5.11 (the one-Present-old model tap), 5.13 and
5.14 (0709: instances entering the EXE's cull, the keep), `city-renderer.md` 10.3-10.5 (the drop rule, the tap, the Unmap
copy), `frame-cost-exe-side.md` 5 (0719's cached maps).

## 1. The report and what the video shows [L: the owner's recording, read frame by frame]

The owner, 2026-11-25 23:2x, beta 4 hotfix 1 (VP 5.4.8, EXE build 26577625, the owner's options) [reworded for privacy]: in late-game situations, terrain elements that seem to come from drawn models flash; easily reproduced with the turn 305 save by panning the mouse camera a little. Video: 1920 x 1080, 30 fps container, 2.7 s, during an AI turn
("Processing Turn For Portugal").

Read with `vidframes.py extract` at full size (runbook 4.12) and crops of one star fort and one city cluster
(`server/evidence/0788/`'s scratch scripts on C:, not kept): in recorded frames 2, 5, 7, 10, 11 and 42 (t = 66, 199, 266,
399, 433, 1866 ms) the models are **missing**, and only their shadows lie on the ground; frames 6, 8, 9 between them hold
the models. A per-frame edge-energy measure of the fort crop drops from about 2,800 to about 2,000 in exactly those frames.
The shadows are the EXE's shadow-mask pass, which the compositor never drops (`city-renderer.md` 10.3 rule 1), so in the
flash frames the compositor dropped the EXE's model draws and the server drew none of them.

## 2. Reproduction on a test instance [L]

- **The save needs the owner's extra mods.** The turn-305 save copy (`OWNER-0788_T305.Civ5Save`, CP v151 / VP v17 =
  VP 5.4.8 in its header) did not load on c5h-a with CP + VP + EUI: server.log `save mods: the DLL reports a
  deserialization error (a sentinel of the save did not match) after the game data mismatch (ticket 0681)`, game exit 3
  (run r2, 2026-11-25 23:41). The cause is `../03-dll-engine-boundary/save-mod-mismatch.md`'s: mods with
  `AffectsSavedGames = 0` that the header does not list. The MC copied the owner's seven extra mods (#16307, #16328; names
  kept out of the knowledge base) to `F:/c5-64bit-work/0788/in/mods`; `server/evidence/0788/mods0788.sh add` lays the
  five that c5h-a lacks into its own MODS folder (two it has already, identical but for UI Lua files, stay the
  instance's), `remove` takes them out by hash. With them the save loads (runs t1 on, 2026-11-26 00:00).
- **Driver**: `server/evidence/0788/rep0788.sh` (claim, begin, prep, run, end; model: 0748's `ws0748.sh`) lays
  beta 4 hotfix 1's layout (`C:/c5-64bit-work/cand7/tier/layout/x64-1b87dd18-p482ce16`, its composite, the pack's
  `purity.tsv`, the owner's `VP64Options.ini` copy of 2026-11-23), launches hidden and uncapped, drags 20-30 s in the
  human turn, ends the turn and drags during the AI turn (`drive0788.py`: 300 ms drags over 6 % of the client width,
  0.3 s apart) until the human turn is back. c5h-a is EXE 4390913 (the owner plays 26577625); the AI turn lasts 25-45 s.
- **The scene recorder cannot span it** [L]: with `render_test_record` the load's members took over 30 s and the
  composite's hung route ended the game (t2, exit 3); with `lockstep_hung_ms=120000` (TEST ONLY) the span ended at the end
  turn because the server drew a frame id again (`frame 1098 drawn after frame 1098 (a redraw or a new composite): the
  span ends there`, t4). So the evidence comes from a diagnostic build instead.
- **The diagnostic** (`server/evidence/0788/diag0788.diff`, never merged; applied to the beta 4 hotfix 1 cut 482ce16be,
  built with runbook 1.2 against cand7's source and deps, `server.exe` a33bd588...; on with `C5_DIAG_0788=1` in the
  launch's environment): when the tap of Present P arrives, the instances that tap DROPPED (the EXE did not draw them in
  frame P) whose mesh key the tap the server drew frame P with did not hold as DROPPED; one `diag0788: frame P=...`
  line per frame with at least `C5_DIAG_0788_MIN` (10) such instances, split by what the drawn tap held for the key
  (absent, or passed as not_in_set / bit_off / unhashed / plain), and a `diag0788 1s` summary.

**Run d1** (c5h-a, 2026-11-26 00:38-00:42 EST, the owner's T305 copy and mods, 30 s of drags in the human turn, then the
AI turn) [L, C the lines]: 63 frames of 10,627 with 10 or more missing instances. The largest:

| frame P | drawn with tap | P dropped (instances / draws) | drawn tap (draws, dropped cities / landmarks) | missing instances (keys) | in the drawn tap |
|---|---|---|---|---|---|
| 6955 | 6954 | 1,104 / 384 | 131, 48 / 58 | **749 (297)** | absent |
| 7144 | 7143 | 1,810 / 638 | 573, 101 / 454 | 111 (44) | absent |
| 6945 | 6944 | 492 / 255 | 265, 36 / 185 | 78 (51) | absent |
| 6971 | 6970 | 1,489 / 521 | 493, 86 / 376 | 11 (11) | not_in_set (set 9 -> 10) |
| 6036, 6787 | the tap before | 14 / 14 | **0 draws** | 14 (14) | absent |

Frame 6955 is the owner's flash: 68 % of the models on screen missing for one frame. In the composite's second around it
(`modelfilter 1s` 00:41:03) `no_shadow=375` and `unhashed=234`: in EXE frame 6954 those draws passed without a record
(a draw reading past its buffer copy's high-water mark, a part hashed one frame late; `city-renderer.md` 10.5), so the
EXE drew them in 6954 and the server's frame 6954 (drawn from tap 6953) had them too; in frame 6955 the compositor
dropped them, and the server drew tap 6954, which lacked them. Most other lines are 10-80 instances at the leading edge of
a drag (0709's case: instances, or whole single-instance keys, entering the EXE's cull) and new identity-set generations
(the server publishes one at most every 500 ms, `srv_models_scene.cpp` `publishIfChanged`; in AI turns they come in
bursts, generations 8-12 within 5 s in run t1).

Not seen in any run [L, logs of h1, t1, d1]: a latch loss (`unit_filter: latched word` changed only at the load), a stale
heartbeat, a model bit loss (`LOUD: models:`), a budget or slot overflow (`budget=0`, `truncated=0`).

**The empty taps** (P = 6036 and 6787: the drawn tap held 0 draws, frame P dropped 14 draws) [C the lines; I the why]:
the compositor publishes a tap at every Present while a bit is tapped (`city-renderer.md` 10.5), so a tap with no draw is
an EXE frame in which no model draw of a DROP pair reached the tap with a complete record: either the EXE drew no
models in that frame, or every model draw passed without a record. Which of the two is not read here (the composite
counts per second, not per frame). Either way frame P was drawn by nobody for those 14 draws; with the hold each of them
is held at P (its key was not droppable at P-1) and the server draws it at P+1.

**An independent find in an older recording** (impl-0790, 2026-11-25/26, `[L]` the recording, `[C]` the scores): with
`server/evidence/0790/flash0790.py` (a tile score over every displayed frame, runbook 4.15) the beta 4 laptop recording
job 20261125-003902-b4db (TB-HUGE_0350, 0736's keys run), case `keys1200_r1` frame 48 (t1 18.8; every other frame of
the case 0.7-5) shows the obelisk landmarks, the Great Wall and the farm buildings missing for one frame, back in
frame 49, 13 ms after `models: the compositor applied identity set generation 12 (5548 keys)` in that run's render.log:
cause 2 of section 3 (and, through new kinds' buffers not yet copied, cause 1: on 0753's laptop smoke `no_shadow` came
in bursts in the seconds of set generations, 429 + 85 at generation 10 -> 12). Not every generation flashes (8-11
gave nothing there).

**The picture gate's calibration** (impl-0790 #16532, laptop recordings of 2026-11-25 on TB-HUGE_0350; `flash0790.py`):

| arm | t1 (one-frame tile score) | t2 (two-frame rule) |
|---|---|---|
| stock, fast drags | 1.6-3.2 | 0.9-2.1 |
| beta 4 product, drags | 0.2-3.1 (keys + drags up to 5.0) | - |
| the owner's video (fort missing in frames 5, 7) | 14.4 | 9.1 |
| beta 4 keys1200_r1 frame 48 (above) | 18.8 | - |

Gate: the product's top t1 / t2 / st above max(stock's top, floor 6.0) + margin fails. The plot tooltip at a drag's start
scores about 100 in both arms and is set aside as interface; flash0736's whole-frame score is only 0.36 on the owner's
flash.

## 3. The cause [C: code; L: d1]

Under exact lockstep the server draws frame P with the model tap of Present P-1 (`pan-coherence.md` 5.11): it draws that
tap's DROPPED records at frame P's camera. The compositor decides drops per frame. So **every draw that goes from passed
in frame P-1 to dropped in frame P is drawn by nobody in frame P**. Ways a draw goes that way, all in the drop rule of
`city-renderer.md` 10.3:

1. its record could not be complete in P-1 (`no_shadow`: the instance buffer was read past the copied range; the copy
   grows by the mark rule from the next frame on), or a part was not hashed yet (`unhashed`, read-back one frame later);
2. its key joined the identity set at the Present between (a new generation; 5.11's cause 2);
3. its latch bit was set at the Present between (a model bit back after a loss);
4. the EXE drew no models in P-1 at all (an empty tap).

Late game makes (1) and (2) frequent and large: more and bigger instance buffers grow past their marks as pans and AI
moves bring new instances into view, and new keys keep becoming drawable. 0709's keep (5.14) cannot help: it keeps
instances by key and origin from taps the server drew, and here the whole key was absent from the drawn tap.

**Regression or not** [I]: the mechanism exists since the server drew the paired older tap (4882, 2026-11-19) and is
exact every frame since 0691's exact lockstep (2026-11-23). Whether 0719's cached maps (in the owner's build) or 0758's
tail read (main) change how often `no_shadow` passes a draw is not measured; the cause does not need either.

## 4. The fix (ticket 0788): hold a newly droppable key one frame [C]

`shell/composite/unit_filter.cpp` (`HoldSeenBefore`, `g_mHold`): a draw the compositor would drop, whose mesh key was not
droppable in the frame before, is passed once more (the EXE draws it in this frame) and recorded with the new flag
`VPMTAP_DRAW_HELD` (0x20, `shell/common/shell_model_tap.h`); from the next frame on the key is dropped. The server
(`srv_model_layer_c5r.cpp` accept through `ServerDrawsRecord`, `srv_models_scene.h`) draws DROPPED and HELD records of
the tap it pairs, so in that next frame it draws the held draws from the previous tap. Every transition of section 3
becomes one frame later and seamless. Counter `held` in the `modelfilter` lines; a key table of 16,384 entries (probe 32,
an entry free once its key was not droppable for two frames; `hold full` LOUD once if a key finds none: that key is
dropped without the hold). Diagnostic switch: the environment variable `C5_SHELL_MODEL_HOLD=0` of the game's process (no ini key, MC #16446;
logged).

Limits [I]: keys, not instances: a key already dropped whose new instances arrive in a second draw that passed in P-1
(two draws of one key, one of them past its copy) is still missing for a frame, and an instance entering the EXE's cull
under a dropped key is 0709's keep. A HELD record drawn by the server in a frame where its draw is not dropped (it passed
again) is drawn twice (EXE and server) for that frame, the same geometry. Offline the bundles carry the flag, so a replay
draws what the live server drew (scene-bundle rule A); bundles recorded before the fix have no HELD record.

Beside it (ticket 0789, impl-0789, not merged on 2026-11-26): a server-side instance store that keeps drawing an instance
it has drawn before while a complete tap lacks it inside its box, and draws a passed record whose key is in the set
frame P uses (its R5). It covers instances seen before; the hold covers first entries and needs no history; with both, a
held record that 0789 also draws is a one-frame double, never a hole (impl-0789 #16458).

**Live A/B** [L, c5h-a, EXE 4390913, 2026-11-26 02:42-02:54 EST; the owner's T305 copy and mods, `rep0788.sh` with 30 s of
drags in the human turn and the whole AI turn; the beta 4 hotfix 1 cut 482ce16be + this fix + the diagnostic (which
counts a HELD record of the drawn tap as drawn): server.exe `0ab53931...`, composite `83956abc...`; one launch per arm]:

| run | build | frames read | frames with >= 10 missing | missing instances (all frames) | worst frame | `held` |
|---|---|---|---|---|---|---|
| d1 | beta 4 hotfix 1 + diagnostic, no fix | 10,627 | 63 | 3,244 | 749 of 1,104 | - |
| d2 | + the fix (hold on) | 17,066 | **0** | **0** | 0 | 2,258 |
| d3 | + the fix, hold off (`VPSHELL_MODEL_HOLD=0` on this test build, renamed `C5_SHELL_MODEL_HOLD` since) | 9,245 | 33 | 3,210 | 799 | 0 |

So the hold removes every frame the server lacked a dropped model in, the leading-edge first entries included (a key
entering the EXE's cull is newly droppable and held). Its cost, as an indication only (one launch per arm on a heavily
loaded workstation, COMMON 23; not a timing claim): the composite's time in the model draws (`draw_ms` of the totals,
which includes the hold's lookup) per frame read 0.475 ms with the hold and 0.645 ms without, per dropped draw 3.9 and
3.3 us; the lookup itself is one SRW lock and at most 32 probes per droppable draw [C], well under the spread between the
launches.

Tests: `shell/tests/test_model_filter.py` (four `test_hold_*` cases; the older cases pin the first drop frame and run
with `C5_SHELL_MODEL_HOLD=0`), `shell/tests/unit_filter_host.cpp` (an `H <f> nheld=<n>` line per frame),
`server/tests/render/cities/models_test.cpp` `server_draws_held`. Results [C, 2026-11-26, workstation, the branch with
main 93b915fd2 (VP 5.5 pins) merged]: shell build (composite `2b8bf4c5...`) + test_model_filter, test_cpu_meshes,
test_unit_filter_tailread, test_unit_filter, test_config_defaults, test_farm_guard 151/151 (test_model_filter 58/58 again on the ASan shell build); models tests 32/32 plain and
32/32 ASan; server.exe on the 5.5 pins (`1737e7f1...`) and the proxy (`07255e37...`, purity test cached) build OK.

**Rule A** [L, 2026-11-26 07:15-08:00 EST, workstation, impl-0788r, branch @074b89d95, `server/evidence/0788/rulea0788.sh`
with `OUT=F:/c5-64bit-work/0788r/rulea`, the pure_vp copy (EXE 4390913), the C: scene library copy (17 bundles, matches
the F: master) and asset root `mods548-vt`; no pool instance]: run_all plain, every build and suite through quietlock:
render-host 235 passed (843 s), scenebundle-py 34 passed (188 s), offline-equals-live 18 passed (866 s), 0 failed, 0
skipped, no timeout, no 0xC0000142; `run_all: OK: 3 suite(s) passed`, exit 0. Then the shell build of the same commit
(`F:/c5-64bit-work/0788r/shell-build`, about 13 min under load, composite `5a52231d...`) and `shell/tests/test_farm_guard.py`
11 passed (45.6 s), through quietlock.

## 4a. The picture pass on oldpc (in progress at 05:18 on 2026-11-26) [L]

`server/evidence/0788/pic0788.sh` (lap0790.sh's four jobs with the T305 copy and the seven mods shipped per job through
`capture_job.py --save-file / --mods-dir / --mods`, added by this ticket; MC #16693) on oldpc (its tb instances are VP
5.4.8: `probe0788.py`, CP v151 / VP v17), SHIP `C:/c5-64bit-work/0788/ship-b4fix` = beta 4 hotfix 1's bin + the PLAIN
fixed server `4a88acca...` + the pack's purity.tsv, proxy `5cea6db8...`, composite `83956abc...`:
- stock-rest 20261126-042338-913b done rc 0 (on tb2; the save loaded, 6 mods laid and all 6 removed by tree hash, 1
  kept as the instance's own, restore ok); fetched to `F:/c5-64bit-work/0788/pic2`.
- product-rest, stock-ai, product-ai of 04:34-04:38 were refused at the game start (`free commit 4.4 GB is below the
  floor of 6 GB`, right after the previous game closed); rerun 90 s apart: product-rest 20261126-044609-6985 done rc 0,
  fetching to `F:/c5-64bit-work/0788/pic3` (slow: about 6 MB/min); stock-ai and product-ai follow
  (`C:/c5-64bit-work/0788/scr/pic_rest.sh`).
- **Result (2026-11-26 06:55-07:1x):** all four jobs done rc 0 (stock-ai 20261126-060932-5a5f, product-ai
  20261126-063728-15ac in `pic3`). `flash0790.py gate` verdict FAIL, on the drag class only: product t1 15.33 / t2 15.84
  against stock 12.74 / 6.83 (drag0300l_r1 frames 72-76) and aidrag t1 12.35 against stock 11.57 (aidrag0300r_r1
  f287); keys, fastdrag, still, zoom and the AI-turn fastdrag / still / zoom pass (product t1 0.03-3.3). Counters:
  truncated, budget_fail, lost, losses, clears all 0; `no_shadow` 200 after the first minute (row 4, diagnostic by
  lead-0673 #16556). Read by eye (strips on F:, not in git): in every flagged frame the **Windows Defender Firewall
  alert** sits over the game window (MC-B #16823: such a capture is DIAGNOSTIC and is rerun after the owner answers),
  and the view is the save's capital (Wiesbaden on the coast, few models) during the pan; no frame shows models missing.
  So the picture pass is diagnostic until rerun without the alert, ideally with `--look-at` on a dense area.
- The gate when all four are fetched: `python -X utf8 server/evidence/0790/flash0790.py gate --stock <pic2>/<stock-rest>,<pic3>/<stock-ai>
  --product <pic3>/<product-rest>,<pic3>/<product-ai> --composite ... --render ...` (pic0788.sh prints it). MC-B #16823:
  a capture with the Windows Firewall alert over the game window is diagnostic and that case is rerun after the owner
  answers.

## 5. Not verified (2026-11-26)

- A picture pass of the fix (flash0790's scores on a visible window: laptop or oldpc, with the owner's mods there) and the
  owner's eye.
- EXE 26577625 (the owner's) and VP 5.5 (main): the live runs here were on 4390913 with VP 5.4.8.
- How often `no_shadow` fires with 0719 / 0758 against without them.

Sources: runs r2, h1, t1-t4, d1-d3 (`C:/c5-64bit-work/0788/ws/<run>/`: render.log, composite.log, server.log, vpproxy.log,
lockstep.csv), the owner's video copy, `server/evidence/0788/`.
