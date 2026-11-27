# The model store: the server keeps every model instance it has drawn (ticket 0789, D269, 2026-11-26)

Ticket 0789 (impl-0789, account B; merged with main by lead-0673 after 0784's option 39), step B of the Architect's
retained world (`research/07-architecture-decisions/architect-2026-11-25-retained-world.md` 5 R1-R5, 6.1, 6.3), a beta 5
gate with 0788 (the owner's late-game model flash, `lategame-model-flash.md`). Written here instead of as a section of
`pan-coherence.md` (held by 0753 during this work); the evidence and scripts are in `server/evidence/0789/`. Marks:
[C] code or data read, [L] measured live, [I] inferred.

In short: the store bridges what 0788's one-frame hold in the compositor cannot see (an empty or cut tap, a flip of a draw
that was never recorded, a missing or refused tap, a re-entry at any pan speed, the reflection pass); with the hold, its R5
hand-over adds only a one-frame double and no visible gain (defence in depth). Live on the owner's T305 copy without the
hold it left no instance it could have filled undrawn (section 4); memory about 1.5 MB live, a ceiling of about 150 MB;
CPU an indication only (about +0.7 ms at p90 of the models stage on a loaded machine), to be measured.

## 1. What was built [C]

Server only; nothing in `shell/`. Files: `server/src/render/landmarks/srv_models_scene.{h,cpp}`,
`srv_model_layer_c5r.cpp`, `server/src/render/srv_render_scene.cpp` (the new log line), `srv_render_host.cpp` (the key),
`server/tools/sceneshot/sceneshot.cpp` (the key in a bundle), the options files (id 40).

- **The store** is 0709's `CulledKeep` with `KeepOptions::store`: no age bound (an instance seen once is known until R3
  removes it, a load or a step back of more than 16 Presents clears all, or the bound of 262,144 entries per pass evicts
  the oldest, counted and LOUD); each entry holds its own copy (`StoredInst`: the record, its four rows, VS / PS cb1 as
  blocks pooled by content, a CPU-built record shared by the instances of one draw), so no tap copy stays pinned (0709's
  `KeepRef` pinned the whole tap scene); one store per pass (scene, reflection), each selected with that pass's cameras.
- **Draw at frame P from tap C** (C = P - 1 under exact lockstep): C's DROPPED records, C's R5 records, then every stored
  instance not held by C (any flag) whose class bit the compositor drops at P and that lies inside camera P's box + 64
  and is (a) a re-entry (0709's rule without the 120 Presents), or (f) ABSENT: inside C's box and lacked by fewer than 2
  complete taps (drawn at a still camera too: counted as bridged), or (g) any, when C is cut (header truncated > 0).
- **R3**: an entry goes when 2 complete taps that tapped its class (header wantBits | latchedBits | the classes they
  dropped) lack it while its origin lies inside their rebuilt box (5.14's box); a sighting resets the count; a cut tap is
  no evidence. A real removal is drawn one frame late.
- **R5**: a PASSED record of tap C (not UNHASHED) is drawn when its key is in the identity set the drops of frame P use
  and its class bit is in the server's mode bits. Code read [C, `shell/composite/unit_filter.cpp` ModelOnPresent]: the
  compositor applies a new set at the Present that ends frame C BEFORE it publishes tap C, so tap C's header
  `setGeneration` is P's set; the server keeps its last 8 published sets (`ModelOwnership`). Offline (no mode block) the
  keys seen DROPPED since the load stand in for the sets and the tap's latch for the mode bits: R5 is then nearly never
  taken (a key's first drop is its hand-over), so offline differs from live only in hand-over frames (gap).
- **A cut tap** (header truncated > 0: the compositor published the frame empty, `unit_filter.cpp` Publish) is not
  paired while an older complete tap exists (the history, or the last complete tap kept); the store bridges otherwise.
- **The key**: VP64Options.ini `[Renderer] ModelStore` (id 40; id 39 is 0784's HideTreadEffect, merged first),
  default 1; 0 = 0709's keep exactly. server.cfg `render_models_store=0|1` overrides it (test switch),
  `render_models_store_refute` (1..16) and `render_models_store_max_entries` are test keys; `C5_RENDER_MODELS_KEEP=0`
  still turns everything off. A bundle records `render_models_store`; a bundle without it (before 0789) replays with the
  store off (0709's keep, what its live side drew).
- **Counters** (R3; the Architect's section 9 for 0788): render.log gets a line of its own each second and at the totals
  (the models line is near the log's 1,536-character body, `srv_render_common.cpp` Emit):
  `models store: on= entries= refl_entries= kb= pool= reentry= absent= cut= refl_kept= bridged= still_bridged= removed=
  evicted= handover frames= recs= inst= taps cut= skipped_cut= gen_changes= latch_changes= passed_not_in_set=
  passed_bit_off= missed checked= inst= known= frames=`. `still_bridged` > 0 is LOUD (at most every 10 s: a fault in the
  tap hidden from the eye, not from the log). `missed`: when tap P arrives after frame P was drawn, its DROPPED instances
  whose origin lay on screen at frame P's camera and that frame P did not draw (drawn by nobody: the flash, measured in
  the server); `known` = of those, held by the store (a rule refused them).
- **Not in the store**: skinned parts (material 6; their origin is not in their rows): R5 and the cut-tap pairing cover
  them; a re-entry of a skinned part is not covered.

## 2. Offline replay of 0709's taps [C]

`store_sim.py` (this folder) replays the rule in Python over the 150 `modeltap/` members of 0709's keep0 bundle
(`F:/c5-64bit-work/0673/rec0709/keep0`, 5.14.1: keep off live, fast drags, 6 hole pairs) in order, each pair = tap A
drawn at tap B's camera; holes = B's DROPPED instances on screen at camera B the frame lacks; ghosts = kept instances on
screen that B lacks (a wrong building). Faults put in: `--cut 7` every 7th tap A empty and cut; `--flip 5` every 5th
tap A without 30 % of its dropped draws (passed and not tapped: the byte budget, a buffer past its copy, 0788's cause).
Results 2026-11-26 01:0x (outputs `F:/c5-64bit-work/0789/sim/`):

| run | holes: none (no keep) | 0709's keep | the store | ghosts (all) |
|---|---|---|---|---|
| clean, 149 pairs | 103 | 0 | 0 | 0 |
| every 7th tap cut (21 pairs) | 7,942 | 7,839 | 41 | 0 |
| every 5th tap without 30 % of its draws (29 pairs) | 3,603 | 3,495 | 69 | 0 |

The store's 41 are one pair at the span's 7th frame (instances not seen yet in the replay: first entries); the 69 lie
in 6 flip pairs (first entries and instances in the 0-77 unit band outside the rebuilt box, where neither the absent rule
nor a re-entry applies while the camera approaches). 0 wrong buildings on screen in every run: the instance identity
(part + origin bits) is stable from tap to tap during these pans [C].

## 3. Tests [C]

`server/tests/render/cities/models_test.cpp`: `store_rule` (the options; no age; R3 at 2 and 3 taps; a sighting resets;
held passed records; cut taps and untapped classes no evidence; (g); the class check; the approach rule kept; evictions;
the step back), `scene_store_switch` (the options reach the layer; off = 0709; the counters, the store line, totals that
never fall), `scene_store_ownership` (the ownership handed to the layer: live, the mode bits, the generations' keys,
found by key), `scene_cut_tap` (a cut tap skipped for the newest complete one, beyond the history too; off: drawn).
`test_models.py` 35/35 (2026-11-26 00:5x); 36/36 on the head with 0788 merged (ace349321, its `server_draws_held` added;
08:4x). Options: `TestModelStore`, test_options x64 / x86 1155 passed; the installer schema test 26 OK.
Rule A on ace349321 (main with 0788 and the 5.5 pins; MC-B's go #17170; 2026-11-26 08:58-, out
`F:/c5-64bit-work/0789/rulea`): render-host 237 passed, scenebundle-py 35 passed, offline-equals-live 18 passed (the 17
library bundles: the store changes no still frame, and the library's bundles replay with the store off, as recorded), the
four scenebundle programs passed with a 7,200 s limit (bundle_format_test 1,260, recorder_test 268, sceneshot_test 328 with
the warp and the hardware adapter, sources_test 24; 09:53). server.exe on the pins-5.5 (`build_server.py` release) OK in
793 s on 7c9a2a0bb, again (103 s, incremental) on the merged head 9b92fdb6e with every other builder of the changed
sources: units, unit extras, unitfeed, farmfields, sceneshot `--smoke` OK; `test_units_runner.py` 7 passed; ASan (MSVC
`/fsanitize=address`, 10:0x-10:45, after two fixes MSVC's warnings-as-errors found: an unused local in a test, C4700, and
an int64-to-int store, C4244): models 36/36, run_all render-host 237 passed, fow-scene 3 passed, scenebundle-py 32 passed
and 3 failed with "1 dumped frame(s) of 2" in `test_recorder_host.py` (the recorder's frame count under ASan on a busy
machine, the same three-way failure 0709 recorded; those cases run without models; plain 35/35 above) [I]. Voided: the scenebundle runs of 06:1x
(timeouts under load) and of 07:18-08:06 (three runners of mine at once: two background waiters survived their stop).

## 4. The live pass on the owner's T305 save copy [L, 2026-11-26 03:2x-04:2x EST]

**Setup.** c5h-c (EXE 4390913, hidden, guard on; MC-B #16547), the owner's seven extra mods laid into c5h-c's own MODS
by hash (`mods0789.sh`, the two that differ kept as the instance's: impl-0788's rule), the save copy
`F:/c5-64bit-work/0788/in/OWNER-0788_T305.Civ5Save` as `C5H-0789-T305`, the owner's VP64Options copy, cand7's 5.4.8
layout (`x64-1b87dd18-p482ce16`: the composite, the proxy and the DLL the owner had) with 0789's `server.exe`
(sha256 `2cbe6831...`), built from beta 4 hotfix 1's repository commit 482ce16 with 0789's four model files over it (the
bases are identical; `src548c.sh`, `host548.py` adds `render_models_store` there, `mklayout0789.sh`). No hold (0788) in
this composite. Drive: impl-0788's `drive0788.py` (here `drive0789.py`): small left-button drags at 55 % height between
47 % and 53 % of the width, 300 ms each way, from end turn until the human turn or 150 s. Measure: the server's own
counter (section 1, `missed`): instances the compositor DROPPED in tap P whose origin lay on screen at frame P's camera
and that frame P drew from nobody, i.e. the owner's flash counted in the server; `known` = of those, the store (or 0709's
keep) held them. `storelines.py` reads the lines. Outputs: `F:/c5-64bit-work/0789/ws/<arm>/` (render.log, server.log,
composite.log, lockstep.csv, marks.tsv).

| arm | server.cfg | frames checked | frames with missed | missed instances | of them known to the store / keep |
|---|---|---|---|---|---|
| a3 (store on; two drive scripts ran at once by a harness mishap: more, faster drags) | - | 2,989 | 11 | 202 | 1 |
| b (store off: 0709's keep, 120 Presents) | `render_models_store=0` | 3,300 | 22 | 595 | 413 |
| a4 (store on, one drive script: the clean A/B against b) | - | 3,126 | 6 | 400 | 0 |

**Verdict** [L]: with the store, no frame missed an instance the store could have filled (known 1 in a3, 0 in a4,
against 413 with 0709's keep in b): acceptance 4's condition holds on this save. What is left (a4: 400 instances in 6
frames, 379 of them in two frames at 04:06:57 next to a new identity set generation and a pan into new ground: reentry
+401 in that second) are instances the server had never drawn: first entries and keys new to the set whose draws passed
without a record, the class 0788's hold fixes in the compositor. The two are complementary: 0788's hold alone measured 0
missing instances in 17,066 frames on the same save (lead-0673 #16822, #16638: the hold fixes first entries and every
passed-to-dropped flip it sees); the store bridges what the hold cannot see (an empty or cut tap, a flip of a draw that
was never recorded, a missing or refused tap, a re-entry at any pan speed) and the reflection pass. No run here shows a
case the hold alone misses. a4 also logged one LOUD still-camera bridge (1 absent instance drawn at a still camera, R3's
counter working as designed).

**Memory** [C: `sizeof` with clang-cl x64, 2026-11-26 04:2x, `server/evidence/0789/sizes.cpp` (sizes.sh)]: an entry is 240 bytes
(`StoredInst` 184, of it `DrawItem` 64 and four rows 64), about 280 with the map's node and bucket [I]; the constant pool
is shared by content. Live on the T305 copy: 3,840 + 80 entries, about 1.5 MB with the pool (a4's line, `kb=`). At the
ceiling of 262,144 entries per pass: about 75 MB per pass [I] plus the pool, so about 150 MB for both passes in the
worst case, in the server's 64-bit process only; an eviction past it is counted and LOUD.

a4's store counters at the end: entries 3,840 (reflection 80), reentry 4,620, absent 1,800, removed 89, evicted 0, R5 16
frames / 930 records / 1,448 instances, still_bridged 1. Arm a3's store counters at the end: entries 4,226 (reflection 142), reentry 3,891, absent 1,811, removed 96, evicted 0,
R5 19 frames / 318 records / 431 instances, still_bridged 0, about 1.5 MB. Its 202 missed instances fall in four seconds,
191 of them in 03:53:13-14 next to identity set generations 15-16 (+11 keys) and a pan into new ground (reentry +181 in
those seconds); 201 of the 202 were never drawn by the server before: first entries and new keys, which 0788's hold
covers. Arm b's 413 known are what the store adds over 0709's keep on this save.

**CPU, an indication only** [L, the same runs; a shared workstation with 5-12 heavy jobs, one run per arm: not a timing
claim, `docs/briefs/PERFORMANCE.md`]: render.log's `pass 1s` models stage (`modelsstage.py`), the per-second mean ms over
the run's seconds: b (keep) p50 0.022, p90 0.45, per-second max p50 0.20 / max 2.44; a4 (store) p50 0.082, p90 1.13, max
p50 0.32 / max 4.60. The store adds up to about 0.7 ms at p90 to the render thread's models stage on this save (its loops
over every entry per tap and pass, the R5 and kept draws); to be measured by the performance team's tools before any
speed claim.

## 5. Not verified

- That a model drawn by both the EXE and the server (R5's double, an absent instance at a real removal) shows no
  difference in a picture (the Architect's open point).
- The store's CPU cost per frame as a timing (above: an indication on a loaded machine).
- A skinned part's re-entry (skinned parts are not in the store).
- The live pass ran on the 5.4.8 layout (the owner's save's VP version) with 0789's server built from 482ce16; the
  product server of this branch (main, 5.5 pins) was not run live with a 5.5 save.
