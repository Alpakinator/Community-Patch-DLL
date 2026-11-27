# The decal tap's walk check in play: why it misses, and the hold (ticket 0406)

Ticket 0406, 2026-11-16 05:32 EST on, account B implementer, branch `task-0406-decal-walk-misses` (from main 19dc4574).
Continues `terrain-decals.md` 18.4 (the walk block and its check) and 20.5 (the tie rule's cost in play). Code:
`shell/composite/decal_tap.cpp` (B_WALK, `WalkStep` / `WalkEnd`, the test modes), `shell/common/shell_decal_tap.h`
(`VPShellDecalTapControl::walkHolds`). Tests: `shell/tests/test_decal_tap.py`. Evidence scripts: `shell/evidence/0406/`.
Marks as elsewhere: [C] code or decompile, [L] live, [I] inferred.

## 1. Why the walk check misses (acceptance 1) [L, 0365's logs]

**Method.** `shell/evidence/0406/miss_causes.py` over the eight 30-turn product-path arms of 0365 on oldpc (huge t350,
`terrain-decals.md` 19.4; `F:/c5-64bit-work/0365/oldpc-{7cb3,f799}/out/{1-before,2-after,3-after,4-before}/`): every
`no walk block for this generation: <reason>` line of `shell_composite.log` (the tap writes the first disagreement it
finds), whether the build was a forced one (the line follows `a batch moved during the build after 3 restarts`), the batch
the reason names, the first `safety net: batch B changed with equal counters` line for that batch after it, the next two
READY lines (walk block or none); from `render.log` the generations applied with and without the walk, at the load and in
play (after `bake-ahead complete`). Output: `F:/c5-64bit-work/0406/miss_causes_0365.txt` (2026-11-16 05:40, rerun 06:31
with the forced test widened to 2 s and the 0406 log text; the same counts).

**Result** (24 misses in 8 arms, two classes, nothing else):

| Phase | Build | Count | The reason the tap logged | What followed |
|---|---|---|---|---|
| Load (generations 2 and 3 of every arm, 5-9 s after generation 1) | forced: 3 restarts in a row, then the snapshot published by design (`terrain-decals.md` 9.6, 4110) | 16 (2 per arm) | `cell C: item I names batch 10 (generation 2) or 11 (generation 3) slot S, not a record of the snapshot`: the index has items of decals the load added after the tap read that batch | published without the block; generation 4 has it again |
| Play (AI turns) | not forced (0 restarts) | 8 (in 7 of 8 arms) | `cell C: item I (batch B slot S) has the point P; the record's centre Q, small, its cell C`: the item's point (the new decal's centre) is not the centre of the record the snapshot holds in that slot, 0.1-41 samples apart, the same cell | the safety net's `batch B changed with equal counters`, same batch, **0.04-0.44 s later in all 8**; 5 published without the block (each followed 0.8-0.9 s later by a generation with it: 2 tie-rule changes each, the "4 of 8 arms" of `terrain-decals.md` 20.5), 3 abandoned before publication (the safety net's mark restarted the build) |

So, which records and when:
- **At the load** [L]: the EXE adds about 9,000 decals over 5-10 s after generation 1 (batches 10 and 11 last). Each
  build is overtaken by a batch change, the tap restarts it 3 times, then publishes the snapshot it has (4110's rule, for
  progress); by then the index holds the newer adds. Generations 2 and 3 go out without the block in every arm, 4 with it.
  This is the tap's design, not a fault of the check: the snapshot is older than the index by construction.
- **In play** [L counts, I mechanism]: during AI turns the EXE replaces a decal in a slot of a batch (a remove and an add
  that reuses the freed slot, or a rewrite of the record) so that the batch's vector entry, the 0x2C bytes the poll
  compares (`terrain-decals.md` 6.3), comes out equal: the poll does not mark that batch. A build that another batch's
  change started then copies the index (in one Present at B_WALK) with the new decal's item while its snapshot still holds
  the old record in that slot: the point differs. The safety net (one batch per Present after each 1 s tick, a hash of
  the live records) finds the batch soon after, and the build after it carries the walk. The cell in the logs is mostly
  250 near (6,760, 1,820) on this save (5 of 8), a place the AI rebuilds on every run [L]; which game object it is was not
  looked up. Generation 157 of arm 7cb3/2-after: `+64 -64` decals changed (other batches) with batch 5's slot 388 stale;
  generation 158 a moment later `+1 -1`, the stale record.
- **Not seen**: a miss from a torn index read, from a large-list disagreement or from a record missing from the index in
  play; no miss of a structural kind (out-of-range index, loop, free item).

## 2. The hold (acceptance 2) [C]

At B_WALK, when the walk check misses (`decal_tap.cpp`):
- **Forced build** (some batch is marked moved, or the batch count changed: a moved batch reaches B_WALK only after the
  restart limit) or **nothing published yet** or **the published generation has no walk block** (the server's tie rule
  does not change either way): the generation goes out without the block, as before 0406. The log line says which:
  `no walk block for this generation: <reason> (a batch moved after the build began and the restart limit was reached:
  the snapshot is older than the index | nothing published yet | the published generation has no walk block either |
  the walk check missed after the holds)`.
- **Otherwise** (the snapshot looks intact by every counter, the published generation has the walk): the build is
  **held**: the batch the miss names is marked moved (read again at the next read, as the safety net would mark it),
  the stage dropped, and a new build starts from the next read; the published generation, with its walk, stays in force.
  Log: `walk check missed: <reason> - the build is held: the batch it names is read again and the build repeated (hold h
  of 4; the published generation G keeps its walk block)`. A reason without a batch (a structural miss) reads the index
  again with the same snapshot. At most `decal_walk_holds` holds in a row (ini key / env `VPSHELL_DECAL_WALK_HOLDS`,
  0..16, **default 4**; 0 = the behaviour of 0366); the count restarts with every publication or end of a build. After the
  last hold the generation goes out without the block ("after the holds"): a disagreement that does not go away costs at
  most 4 extra builds (about 0.8 s each on huge) once, and then nothing, since the next misses publish at once (the
  published generation has no walk).
- **Every batch a miss can name**: an item's batch (not in the snapshot, listed twice, point or cell or size
  disagreeing), a large-list node's batch, and for "records in neither the index nor the large list" the batch of the
  first such record (the log line now names it: `(the first: record k, batch b slot s)`).
- **Counters** (control block): `walkMisses` counts every miss as before (each logged); new `walkHolds` (+260, from
  reserved; size still 336) counts the holds. The image layout does not change; the server is not changed.
- **Why a hold and not "keep the last good walk"**: a walk block is per record of one snapshot (cell, rank, point); the
  previous block does not describe the new records, and ranks in a cell depend on add order, which only the index has.
  A hold costs one more read of one batch and one more build (0.7-0.9 s wall on huge at `decal_budget_ms` 2, from 0365's
  READY lines) and delays that generation's content by as much; the safety net would have found the batch within 0.44 s
  anyway, so the delay is about what the flip-and-back cost before, without the flip.

## 3. Tests [C]

`shell/tests/test_decal_tap.py` on `decal_tap_host.exe` (the tap's own Present loop without D3D), four new cases and one
extended; the test modes are in `decal_tap.cpp`'s synthetic EXE (results: section 5):
- **`test_walk_stale_record_holds_and_publishes_with_the_walk`** (`decal_test=walkstale`, the in-play case): after
  generation 1 (with the walk) the change rewrites decal 2 of batch 0 in place (same slot and triangles, a new rectangle:
  the batch's vector entry is equal) and changes batch 1 (river bed out, road in); the safety net at 60 s so it cannot
  help. Asserted: the hold line (`... (batch 0 slot 1) has the point 125, 117; the record's centre 120, 116 ... hold 1 of
  4; the published generation 1 keeps its walk block`), generation 2 **with** the walk block, every record and walk
  entry exact against the world recomputed in Python (the rewritten decal included, its old and new rectangles in the
  changed block, `+2 -2`), walkMisses 1, walkHolds 1, no generation and no log line without the block, no LOUD line.
- **`test_walk_stale_record_without_holds_publishes_without_the_walk`**: the teeth. The same change with
  `VPSHELL_DECAL_WALK_HOLDS=0` (the tap of 0366): generation 2 without the block, with the stale decal 2 (`+1 -1`), the
  miss logged "after the holds", walkHolds 0.
- **`test_walk_miss_after_a_walk_holds_then_publishes_without`** (`decal_test=walkmisslate`, a miss that stays): 4 holds
  (each line asserted), then generation 2 without the block ("after the holds"), walkMisses 5, walkHolds 4; a further
  change misses and publishes at once ("the published generation has no walk block either"), walkMisses 6, walkHolds 4;
  not FAILED, no LOUD line.
- **`test_huge_storm_before_the_walk_publishes_without_the_block`** (the load's case, `decal_test=huge` with the trigger
  `shell_composite.decalstorm`: a change of batch 5, then one before the walk of each of the next 4 builds): 3 restarts,
  the 4th build goes on with its stale snapshot, the walk misses (`1 of 17800 records are in neither ... (the first:
  record k, batch 5 slot s) (a batch moved after the build began and the restart limit was reached ...)`), generation 2
  goes out without the block at once (walkHolds 0, no hold line), generation 3 (calm) with it, its 17,799 records and
  every walk entry exact.
- **`test_walk_miss_publishes_without_the_block`** (0366's, extended): with nothing published yet the miss publishes at
  once ("nothing published yet"), walkHolds 0.
- `test_header_layout_matches_this_reader` and the static_assert cover `walkHolds` at +260 (size 336 unchanged).

## 4. Live: 30 AI turns on huge, product path (acceptance 3)

**Method** (0365's recipe in its workstation form; counts only, timing an indication, COMMON 23): the D33 product path
of `shell/evidence/3308/run_3308.py` (imported unchanged by `shell/evidence/0406/run_0406.py`: owner 0406, save copy
C5H-0406-HUGE_0350 of c5h-g's C5H-VP8P-HUGE_0350, c499d3c6..., 30 turns, no reloads): proxy + server.exe with
`[Renderer] ServerRenderer = 1`, the composite beside the proxy with its manifest, D33 with Overlay on, ring + fence,
lockstep, depth; c5h-g (Goldberg on F:, VT present, D94), claimed by 0406, on the workstation loaded by about 11 other
heavy jobs (`quietlock.py check` 05:47). Builds (`shell/evidence/0406/build_0406.sh`, 2026-11-16 05:51-05:57): server.exe
release offline from this branch (main 19dc4574 + this ticket; its render code has 0393's tie-rule apply line)
**67f847c8...**, the pin layout `x64-04b1211b-pfdee656` (DLL **29f58e17...**, COMMON 18), proxy **e498596d...**, the
composite of this branch **a1659d8b...** (walk holds at the default 4). Counted by `miss_causes.py` (render.log's 0393
apply line `tie rule changed (A -> B): K of C tiles`, in play = after `bake-ahead complete`; the composite's miss and hold
lines). Logs kept in `F:/c5-64bit-work/0406/live1/` (not committed).

**Run 1, the product as configured** [L, 2026-11-16 05:58-06:30, rc 0, 30 turns (350 -> 380), 0 LOUD lines in
render.log, server.log and vpproxy.log; the composite's 14 LOUD lines are all `fow_models: the server's mask is stale (its
heartbeat is still)`, not the decal tap (see 6)]:

| Figure | Run 1 |
|---|---|
| Generations applied (in play) | 228 (212) |
| Generations without the walk block: load / play | 1 / **0** |
| **Tie-rule changes (0393's line): load / play** | 2 / **0** |
| The load's changes | generation 2 (the EXE's walk -> the 4111 rule): 2,867 of 2,990 tiles raised, **62** of them had a bake (re-baked); generation 3 (back): 2,237 of 3,003 raised, **50** with a bake |
| The load's miss | 1, forced (06:03:48, 4.8 s after generation 1: `cell 331: item 16021 names batch 11 slot 1292, not a record of the snapshot (a batch moved after the build began and the restart limit was reached ...)`) |
| Walk misses / holds in play | 0 / 0 |
| Safety-net finds in play (`batch B changed with equal counters`) | 1 (06:29:21, batch 8), with no build in progress: re-read before any walk |

So the run shows 0 tie-rule changes in play and names the load's two, but **no in-play miss happened in it** (none of the
replaced-record events overlapped a build: one safety-net find in 30 turns, against 0-6 per arm on oldpc in 0365). It does
not by itself show the hold working live; run 2 does (below).

**Run 2, the same with the safety net off** (DIAGNOSTIC arm: `decal_net_ms=600000` added to the instance's composite ini,
a test setting, so that a record replaced with equal counters can only be found by the next build's walk check) [L,
2026-11-16 06:31-06:58, the same instance, builds and save copy, rc 0, 30 turns (350 -> 380), 0 LOUD lines in render.log,
server.log, vpproxy.log and the composite's log]:

| Figure | Run 2 |
|---|---|
| Generations applied (in play) | 237 (233) |
| Generations without the walk block: load / play | 2 / **0** |
| **Tie-rule changes: load / play** | 2 / **0** |
| The load's changes | generation 2 (walk -> 4111 rule): 2,822 of 2,956 tiles raised (no re-bake line: none of them had a bake); generation 4 (back): 2,219 of 3,000 raised, **563** with a bake |
| The load's misses | 2, both forced (generations 2 and 3: `names batch 10 / 12 slot S, not a record of the snapshot (a batch moved after the build began and the restart limit was reached ...)`) |
| **Walk holds in play** | **3**, all `cell C: item I (batch B slot S) has the point P; the record's centre Q` (batches 3, 4, 8; cells 249, 250 and 493, the first two at the place of 5 of 0365's 8 in-play misses; the item's point 14-23 samples from
the snapshot record's centre), each followed by the next generation **with** the walk block 0.36, 0.71 and 0.90 s later |
| Walk misses published without the block in play | 0 |

Without 0406 each of those 3 holds would have been a generation without the block followed by one with it: 6 tie-rule
changes in play, about 6 x 2,200 = 13,000 re-bakes (`terrain-decals.md` 20.5's rate) [I]. With the safety net on (the
product, run 1) the same events are usually found by the net before a build reaches its walk; when a build gets there
first (0365's 8 of 24 misses), the hold now takes the net's place.

**The load's tie-rule changes remain** (by design, section 2): generation 2 (and 3) leave without the block, the next
with it. Their re-bakes of tiles that already had a bake: run 1 62 + 50; run 2 0 + 563 (its generation 4 came 5.5 s after
generation 2, when the bake-ahead had baked more); 0393's recording 38 + 48. At 1.92 ms per bake (oldpc) that is 0.2-1.1 s
of render-thread time once per load [I].

## 5. Suites (workstation) [C, 2026-11-16 05:47-07:10]

`shell/evidence/0406/suites.sh` (every step through the quiet lock's gate; logs `C:/c5-64bit-work/0406/out/`, copies on
`F:/c5-64bit-work/0406/`), the workstation loaded by 10-11 registered heavy jobs of other teams throughout:
- `shell/tests/test_decal_tap.py`, plain (shell build of this branch): **31 passed, 2 failed**; the four new cases and the
  extended one passed; the 2 failures are 4110's per-Present limits on the huge world
  (`test_huge_spread_equals_one_shot_and_stays_in_budget`: the tap's wall max 6.53 ms against 4.5;
  `test_huge_batch_changed_mid_build_restarts_the_build`: the host's CPU max 3.564 ms against 3.5). Rerun of the two
  alone: the second passed, the first failed again (wall max 5.41 ms against 4.5; host CPU 3.19 within its limit).
- The same suite under MSVC `/fsanitize=address`: **32 passed, 1 failed** (the same wall-time case: 9.26 ms against 7;
  host CPU 3.19), **0 ASan reports** (`log_path` folder empty).
- `server/tests/render/decal/test_decal_tap.py` (the server's reader compiled with the changed header): plain **13
  passed**.
- Base against this branch on the two timing cases, interleaved (`base_timing.sh`: main 19dc4574's `decal_tap.cpp` built
  with this header, then base, mine, base, mine; 2026-11-16 07:04-07:08): **every round of both builds failed one of
  the two limits**: base round 1 the spread case's tap wall max 20.3 ms, round 2 5.2 ms (limit 4.5); this branch round 1
  the mid-build case's host CPU max 7.11 ms, round 2 5.54 ms (limit 3.5); the other case passed in each round. So those
  limits fail on this loaded machine with or without 0406 [L, indication]; 0406 adds nothing to a Present that does not
  miss its walk (in test mode one more file-attribute check per Present, the `decalstorm` trigger). Not rerun quiet.

## 6. Not done, open, side findings

- **Not traced in the EXE** [I]: which call replaces a decal's record with the batch's vector entry equal (a remove and an
  add into the freed slot within one frame, or a rewrite of the record in place); the counts and the safety net's finds
  are [L], the mechanism is not decompiled. The tap does not need it: the hold re-reads whatever the miss names.
- **The load's two tie-rule changes stay** (forced builds, section 2). Holding them as well would delay generations 2-3
  of a load by several seconds while the bake-ahead bakes with generation 1's half set, which costs more re-bakes than the
  flips (section 4: 0-563 tiles with a bake per flip). `terrain-decals.md` 20.5's option (b) (the server keeps the walk's
  order through a walk-less generation) would remove them; not built.
- **The cost of a hold** was not measured on its own: one more read of one batch and one more build; in run 2 the next
  generation came 0.36-0.90 s after the hold line (workstation, loaded). No quiet A/B (none needed for a counts ticket).
- `shell/composite/shell_composite.ini`'s comment list of optional decal keys does not name `decal_walk_holds` (the file
  is not in this ticket's list); the key is documented in `decal_tap.cpp` and here.
- `terrain-decals.md` 18.4 still says "Any miss: this generation goes out without the block"; the MC adds the pointer to
  this file at the merge (ticket 0406's text).
- **Side finding, not decals**: run 1's composite log has 14 `fow_models: LOUD: the server's mask is stale (its heartbeat
  is still)` episodes (06:03-06:26, during AI turns, the white view put back each time); run 2, same builds and save, has
  none. Reported to the dispatcher; not investigated here.
