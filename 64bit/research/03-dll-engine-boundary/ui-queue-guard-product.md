# The EXE's 4 MB UI message queue on the product route: exposure, guard options, #13397 (issue 0459)

Design step for the follow-up proposed in `upstream-fix-batch-2.md` 2.8 item 1 (one-off 9075). Ticket
`tracker/issues/0459-proxy-ui-queue-overrun-guard.md`, account B, 2026-11-17. No product code: `proxy/` is held by epic 2600
until its merge; the implementation is a later ticket the MC opens from section 6. Marks: `[C]` source, dump or log,
`[L]` live, `[I]` inferred. Addresses are DX11 EXE VAs at ImageBase 0x400000 unless marked; the queue's layout is
`research/02-exe-internals/message-queue-and-fixed-buffers.md` section 1 (RVAs there).

## 1. Result in short

- **Yes, the product route overruns the EXE's 4 MB UI queue** [L, 2026-11-17, workstation instance c5h-h, product path
  D33 development layout, 43-civ x64 DLL m43]: 9074's Marathon saves T650 (once) and T1100 (twice) each overran it in
  the load, with the stock signature (one 128-byte record past the end rewrote the flip counter to 128); T600 peaked at
  48 % in the load and 71.5 % in the AI turn. Stock's `-ia` guard drops records from the same save on (T650). The writers
  are the stock ones (LandmarkSystem's farm and route builders, fed by the server's `GameplayPlotStateChange` calls
  through the proxy); the proxy itself posts nothing. Measured with a read-only watch (`server/evidence/0459/qwatch.py`).
- **Recommendation: an EngineQueueGuard-style import-slot guard in the proxy** (option (a), section 4.1): it covers every
  writer, changes no game result (D164), costs well under a millisecond per load and 64 KB, and is what every stock
  `-ia` game has run with since 2026-09-21; its cost is dropped farm fields and route curves on such loads. Batching by
  the swap counter (b) is not recommended now: it still needs (a) as backstop and its DLL form lost route art (9075).
  The implementation ticket's text is in section 6.
- **#13397's full form** (load-screen hold) lays the deferred plots out through the same function that lost the art in
  9075's test, does not touch the T1100 failures (not caused by the overflow, 9074), and needs #13379 and mod Lua that
  the patch does not ship; its 9074-save load was not run (not cheap: section 5).
- **Found on the way**: both T1100 product loads ended 10 s after the map's first frame by the fog install check
  ("no overlay pair yet (overlay WAITING)", EXE exit 3, no popup); T650 got its overlay pair at 9.4 s and survived, so
  this is a load-size bug of the fog/overlay side, not the queue's [L/I]. Needs its own ticket.

## 2. Who writes the queue on the product route [C unless marked]

- **The writers are the stock ones.** The records that overrun the queue on late loads are terrain decals: farm-field
  polygons (types 3-6) and route curves (13-15), posted by LandmarkSystem's GameView listeners (farm builder VA
  0x748FC0, route builder 0x5EC250 -> 0x583CE0), which listen on the "B" event manager (types 59 and 45) and post into
  the 4 MB queue, swapped only after B's whole batch has been dispatched (`message-queue-and-fixed-buffers.md` 1, the
  2026-10-01 capture). D33 leaves LandmarkSystem on; its key list (`proxy/src/proxy_session_keys.h`
  `VP64_D33_KEYS_A2`: OverlayDebug, Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons, ColorKeyEdit; darkred.py
  also sets River and Arrows, Overlay kept on, D93) turns `Decal` off, but that does not stop these records: on the huge
  t350 save the EXE's TerrainDecalSystem holds 4,737 farm-field decals under D33 against the same farms on stock
  (`research/05-graphics-composition/terrain-decals.md` 3.2, 6.2) [L there]. So the decal system still consumes them,
  and the server's decal tap copies whatever the EXE holds into the server's terrain picture (same file, 3.2).
- **What makes them on the product route.** The EXE's per-plot load pass calls `ICvPlot1::UpdateLayout(bool)` and the
  map's `ICvMap1::UpdateLayout(bool)`; on the product route both are generated proxy methods forwarded synchronously to
  the server (`proxy/model/policy-table.tsv` rows `engine_to_dll ICvPlot1 UpdateLayout ... sync` and `ICvMap1
  UpdateLayout ... sync`). In the server the DLL's `CvPlot::updateLayout` calls `gDLL->GameplayPlotStateChange`, which
  the server's remote engine utility notes for the feed and forwards to the EXE over IPC
  (`server/src/remote/srv_remote_objects.cpp` lines 161-167, `SrvRemoteFeed::OnPlotStateChange`). The EXE then posts
  the same B-manager events and LandmarkSystem the same decal records as on stock. The same holds for an active-player
  change under autoplay, #13397's second trigger [I: the same DLL code path, not traced on the product route].
- **The proxy's own posts.** The proxy writes no queue record itself (no queue address and no `InterlockedExchangeAdd`
  slot in `proxy/src`, grep 2026-11-16 and 2026-11-17); the composite draws into the EXE's frame and posts nothing.
  Whatever the proxy calls in the EXE on the server's behalf can post, as above.
- **What decides an overrun** is the longest main-thread stretch without a swap while those records arrive
  (`message-queue-and-fixed-buffers.md` 1: about 80-120 s at 35-55 KB/s passes 4 MB on stock). On the product route
  every plot's layout is an IPC round trip with a nested callback, so the load pass's pace differs from stock; whether
  the stretch is longer or shorter is what section 3 measures.

## 3. Measurement: the queue's fill on a product-route load of T1100

### 3.1 Method

- **Read-only watch** `server/evidence/0459/qwatch.py` (Python, ctypes; own code): opens the game's EXE with
  `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ` only, checks the PE time stamp (0x546CD0A8) and the channel
  index (0 or 1), then samples with `ReadProcessMemory` the two buffers' size fields (channel + 0 and + 0x400080), the
  flip counter (+0x800100) and the high-water mark (+0x800104), every 2 ms requested (about 170 samples/s measured on the
  workstation, 2026-11-17 05:15). Nothing is written, nothing suspended, no EXE code byte is read. It reports per
  process the largest size seen in each buffer, samples above 90 % and above 100 % of 4,194,304 B (above 100 % is an
  overrun: the writers' reserve has no capacity check), the longest time between two flips, and flip-counter jumps (an
  overrun of buffer 1 writes onto it). Self-test: 10 s on a running workstation game (05:15) and, by chance, 13 s on
  0461's stock game on oldpc tb1 (05:19:56-05:20:09, job 20261117-051920-956d): stamp and fields read, flips advancing
  (37-75 per second), the high-water marks 2,948,992 and 3,014,784 B (70-72 %), i.e. plausible values [L].
- **The stock reference** is the `-ia` guard's own counters (EngineQueueGuard in the instruments patch): 9074's T1100
  loads on the 43-civ guard build dropped 3,182 KB / 3,174 KB at the load's end and 1,863 KB in the first autoplay turns,
  and every load from T650 on dropped (`research/08-performance/marathon-stress-t2000.md` 4.1 on 9074's branch, commit
  95d4aa824; table copied in `upstream-fix-batch-2.md` 2.8 item 1 by its numbers). The watch cannot see the guard's drops
  (the guard keeps the size at or below capacity), so on the stock route it sees at most 100 %.
- **The product run**: 9074's save `TS1.20-T1100` (180 x 112, 42 majors + 20 city states, Marathon; SHA-256 968d7ccd...),
  loaded on the D33 product path in a development layout (`research/03-dll-engine-boundary/evidence/0334/4255/pp_4255.py
  prep`: provision set-proxy, pack=0, `[Renderer] ServerRenderer = 1`, D33 with Overlay on, the composite with its
  manifest); x64 DLL = 0334's m43 build (cf981126..., not a pin, COMMON 18: the 43-civ variant of the x64 pin 04b1211b8);
  `server.exe` (d5dcf808...) and the proxy (d9b86aa8...) built at main e6908c57 against a copy of that build's deps
  (`server/evidence/0459/s01_build.sh`), the composite (29c88f58...) at the same commit. Driver
  `server/evidence/0459/s02_ws_t1100.sh` (perf/profilekit/acceptance.py run, config inc2, 2 AI turns, no ledger).
- **Where**: workstation instance c5h-h (pool claim impl-0459), not oldpc: the oldpc job 20261117-051920-956d was
  refused at the launch gate (free commit 4.7 GB below the 6 GB floor while 0461's game ran on tb1), and a resubmit would
  have waited behind a running job of unknown end, a quiet series and a two-slot job (COMMON 23, H2). Not a timing
  result; the queue's fill depends on the main thread's stall, which a busy workstation can lengthen (noted with the
  numbers).

### 3.2 Results

**T1100, run 1 (2026-11-17 05:26:41-05:33:17 EST, c5h-h, EXE pid 56572)** [L; C for the logs]. Work folder
`C:/c5-64bit-work/0459/ws-t1100-run1/` (`qwatch/qwatch-56572.csv` per second, `.json` summary), the server's run folder
`<c5h-h Documents>/Logs/VP64/run/20261117-052759-56572-1/` (copied to `F:/c5-64bit-work/0459/`).

| time (EST) | flips | buffer 0 size | buffer 1 size | hwm | what |
|---|---|---|---|---|---|
| 05:26:51 | 0 | 0 | 0 | 0 | watch attached (front end) |
| 05:28:26-05:30:21 | 3 -> 4 | | up to 544,640 | 544,640 | the longest stretch without a flip: 115.1 s |
| 05:29:33 | | | | | the server reads the save (server.log 93.7 s: "43 major civilizations ... loads") |
| 05:30:45 | 9 | 4,224 | 675,456 | 675,456 | last ordinary flip before the overrun |
| 05:31:42-05:31:44 | 9 | | 4,044,032 -> 4,153,088 | | buffer 1 fills at about 65 KB/s without a flip |
| **05:31:45** | **128** | 16,640 | **4,194,432** | **32** | **overrun**: one 128-byte record past the 4,194,304-byte capacity |
| 05:31:46-05:33:16 | 128 -> 163 | up to 3,241,856 | | 3,241,856 | writers go on in buffer 0; flips resume at 05:33 |
| 05:33:15 | | | | | server: "fog of war: no overlay pair yet (overlay WAITING) 10003 ms ... the 64-bit path cannot go on", exit 5 |
| 05:33:17 | | | | | proxy: "FATAL: the server failed a call (on_server_death=exit)", EXE exit code 3, no box |

- **The product route overran the queue** on this load [L, n = 1]: buffer 1's size field reached 4,194,432, i.e. one
  128-byte record was reserved past the end. Its header landed exactly on the channel's flip counter and high-water mark
  (buffer 1 ends at channel + 0x800100): the counter read 128 (the record's length) and the mark 32 (its type, the
  redraw rectangle of the 2026-10-01 capture) from that sample on. This is the overrun signature of
  `message-queue-and-fixed-buffers.md` 1 ("flips -> 128, hwm -> 13"), here with type 32. With the counter rewritten to
  an even value the writers' `flip & 1` selected buffer 0 without a swap, so every later record went into buffer 0 on top
  of whatever the dispatcher had not yet read (3.24 MB by 05:33:16); what the dispatcher then walked is not known.
  73,837 samples, 0 read failures, 13,468 samples above capacity (the field stayed at 4,194,432 until the next real
  swap).
- **When**: 132 s after the server read the save and about 80 s before the map's first frame (server.log 305.9 s:
  "terrain: state NO_FIELD -> OK"), i.e. in the EXE's load, as on stock. The watch saw 4 MB arrive in about 60 s
  (05:30:45-05:31:45) with no flip.
- **The run then ended** by the server's fog-of-war install check (D118, `research/05-graphics-composition/fog-of-war.md`
  7.5): the overlay tap stayed WAITING ("source state ABSENT -> WAITING" at 26.8 s and never a pair) for 10 s of the fog's
  own wait after the first frame, no render-thread gap. Whether the corrupted queue starved the overlay is **not shown**
  [I]: the overlay tap may not depend on these records at all, and this is also the first fog-era product load of a
  180 x 112 map with 43 civilisations seen here. The T650 load below separates the two: it overran and still installed
  the fog.

**T1100, run 2 (05:44:11-05:50:37 EST, same layout and instance, EXE pid 20216)** [L]: the same course. Flips 0 -> 9 by
05:48:05 (the longest stretch without a flip 105.8 s, 05:45:55-05:47:41); buffer 1 then filled from 675,456 to
4,188,928 B in 61 s (about 57 KB/s) and **overran at 05:49:07** (size 4,194,432, flip counter 128, mark 32), again one
128-byte record past the end. The writers went on in buffer 0 to 3,221,376 B; at 05:50:13 the counter moved on (129), the
mark took 3,221,376, buffer 1's size field fell back to 1,544,320 and then 256-1,024 B: the EXE dispatched and swapped
on and did not crash. At server time 295.6 s the map's first frame, at 305.6 s the same fog install failure ("no overlay
pair yet (overlay WAITING)", exit 5), the proxy's FATAL exit 3 at 05:50:37, no box. 75,767 samples, 0 read failures.
Work folder `C:/c5-64bit-work/0459/ws-t1100-run2/`, server run `20261117-054528-20216-1` (copied to F: as run 1).

**Two of two T1100 loads on the product route overran the queue**, at the same point of the load, with the stock
signature; neither came to a played turn, both ended by the fog install check 80-100 s later.

**T600 (05:51:55-06:02:08 EST, EXE pid 13592; stock: no drop at T600)** [L]: no overrun. The load's peak was 2,024,960
B (48 %) at 05:55:53 after the longest stretch without a flip of 75.9 s; the fog installed (`fog models: consumer
ACTIVE` 170.8 s), the game reached the map in 229 s and played turn 600 -> 601 (acceptance rc 0, autoplay handed back
at 601). The AI turn's own peak was 3,000,192 B (71.5 %) at 06:00:54, the "second burst" of 9074's table (an
active-player change under autoplay) [I: by its timing]. 7,194 flips, 0 read failures. So the product route at T600
comes near the limit but stays under it, as stock does (stock first drops at T650).

**T650 (06:02:10-06:11:19 EST, EXE pid 65504; stock: the first save that drops, 794 KB at the load's end)** [L]:
**overran** at 06:06:59, the same way: buffer 1 from 675,456 B (flip 9, 06:05:59) to 4,161,408 B at 06:06:58, then
4,194,432 B with the flip counter at 128 and the mark at **14** (a route-curve record's type). The game survived it: the
overlay's first pair came 9.4 s after the map's first frame (server.log 217.9 s against 208.5 s), the fog installed, the
map was reached in 276 s and turn 650 -> 651 played (acceptance rc 0). The AI turn's peak was only 547,200 B. Work
folder `C:/c5-64bit-work/0459/ws-t650-run1/`, server run `20261117-060345-65504-1`.

**Summary of the product loads** (all on c5h-h, the same build, 2026-11-17):

| save | stock (9074, `-ia` guard) | product: largest size seen | overrun | longest stretch without a flip | end |
|---|---|---|---|---|---|
| T600 | no drop | 2,024,960 B at the load, 3,000,192 B in the AI turn | no | 75.9 s | map reached in 229 s, turn played |
| T650 | 794 KB dropped at the load | 4,194,432 B | **yes**, 06:06:59 (mark 14) | 82.2 s | map reached in 276 s, turn played |
| T1100 run 1 | 3,182 KB dropped (two loads: 3,174) | 4,194,432 B | **yes**, 05:31:45 (mark 32) | 115.1 s | fog install failed, EXE exit 3 |
| T1100 run 2 | | 4,194,432 B | **yes**, 05:49:07 (mark 32) | 105.8 s | fog install failed, EXE exit 3 |

The product route overruns where stock's guard drops, from the same save on (T650) [L, n = 1-2 per save]. In each
overrun exactly one 128-byte record crossed the end and rewrote the flip counter; what follows (the writers moved to the
other buffer without a swap) is the corruption the guard exists to prevent. Without a guard the product is exposed on
every late load of a large map.

**The fog install failure is not the overrun's** [L]: T650 overran and still installed the fog (its overlay pair came at
9.4 s of the 10-s wait), while both T1100 loads waited the full 10 s with the overlay never active. What the two T1100
loads share is the map's size and turn, so the late overlay pair looks like a load-size matter of its own, close to the
limit already at T650 [I]. Section 6, open point 3.

**The watch on the stock route, incidentally** [L]: during run 2 the watch, not yet limited to its own instance, also
read 9074's game on c5h-i (the 43-civ `-ia` guard build, stock route, read only). There buffer 1's largest size was
**exactly 4,194,304**, with a 226-s stretch without a flip: the guard capped the buffer at its capacity, as it is meant
to. That confirms the watch reads the same field the guard protects. The watch has since a `--path-filter` and is run
with the claimed instance's name.

- The workstation was busy (4 registered heavy jobs at the start; the MC's notice #3795 names a server.exe that grew to
  about 131 GB from 05:34 to 05:38, after this run's overrun and end). A busier machine lengthens the main thread's
  stretch without a swap; this is a functional result, not a rate.

## 4. Options for a product guard

Both run in the EXE's process, so on the product route they belong in the proxy (the server's DLL cannot read or
write the EXE's memory). Common facts [C]: the queue is static data of the EXE (index RVA 0x160E400, channels RVA
0x160F080, stride 0x800180, size fields at + 0 and + 0x400080, capacity 0x400000, flip counter + 0x800100); the writers
reserve through the EXE's `KERNEL32!InterlockedExchangeAdd` import slot (RVA 0x5D647C, VA 0x9D647C) and return
`buffer + 0x80 + old` with no capacity check; the proxy is the EXE's game DLL, loaded at mod activation in the front
end, before any game load, and boots in `DllGetGameContext` (`proxy/src/proxy_rt.cpp`: `ProxyShell::OnBoot()` after a
successful mode=on boot, `ProxyShell::OnUnload()` at detach).

### 4.1 (a) An EngineQueueGuard-style import-slot guard in the proxy

- **What it does**: the fork's `EngineQueueGuard` (`CvGameCoreDLL_Expansion2/EngineQueueGuard.{h,cpp}`, branch
  `engine-queue-guard` and the instruments patch of the `-ia` builds; GPL, may be ported under D22 with its origin
  named) moved into the proxy. The import slot is pointed at a thunk; for a call whose addend is one of the four size
  fields (two channels x two buffers) it reserves with a compare-and-swap and, when the record would not fit, returns a
  pointer into a 64 KB scratch area instead (the size field is left alone, so the dispatcher never reaches the dropped
  record); every other caller is forwarded to the slot's previous value. Behaviour on stock: "drop what does not fit",
  the owner's choice of 2026-09-21; it is what every `-ia` harness game has run with since [L, 9074's and 9075's runs].
- **Hook point**: the import slot above, found by walking the EXE's import directory by name (`KERNEL32.dll`,
  `InterlockedExchangeAdd`) and checked equal to EXE base + 0x5D647C; installed from the proxy's mode=on boot next to
  `ProxyShell::OnBoot()`, i.e. before the front end loads any save. A data write to an import slot (AGENTS.md: import
  slots are an allowed tool; no code byte, D4). Not installed in mode=off (the original 32-bit DLL answers then and an
  `-ia` test DLL brings its own guard; two chained guards would count twice).
- **Checks before the write**: the PE time stamp 0x546CD0A8 (the proxy's `ProxySession::IsKnownExe`), the slot found
  by name at the expected RVA, the channel index 0 or 1, both size fields readable and at most the capacity, the flip
  counter readable. EngineQueueGuard also matches four EXE code byte patterns (writer, reserve, swap, dispatcher);
  whether new byte patterns may be committed under D83 is an open question for the MC (section 6): `proxy_shell.cpp`
  already carries ported patterns (`Mask::CODE_PATTERNS`, from the fork's `ViewMaskSync.cpp`), while 9075 read D83 as
  barring #13379's signature table. The pattern-free check above is enough for the one EXE build the product supports.
- **Unload**: the slot must never point into an unloaded image. **Corrected 2026-11-17 (ticket 0492, accepted by the MC):
  no pin at install.** A pin would keep the proxy's image across the EXE's `FreeLibrary` of the game DLL, so the next
  load would get the old image and its globals instead of a fresh boot, which breaks the proxy's second boot in one EXE
  process (issue 3308). The proxy gives the slot its previous value back at the unload by compare-and-swap (only while it
  still holds the thunk) and pins its image only when it sees another hook on top of the thunk (`LOUD`); section 7.1.
  (Replaced: "the proxy should pin itself (`GetModuleHandleEx` with `GET_MODULE_HANDLE_EX_FLAG_PIN`) once the guard is
  installed".)
- **Game identity (D164)**: unchanged. The queue carries engine-to-UI view records (decals, redraw rectangles); the
  game state lives in the server, and the DLL never reads these records. On stock the guard has been in every `-ia`
  build that the harness's games run [L]; the save tests run headless without the EXE and are untouched [C].
- **What it costs**: per EXE call of `InterlockedExchangeAdd`, one subtraction and compare for every caller that is not a
  size field; a compare-and-swap loop instead of one locked add for the queue's writers (about 75,000-160,000 records
  per late load, `message-queue-and-fixed-buffers.md` 1: well under a millisecond in total) [I, not timed]; 64 KB of
  scratch. What a drop costs is art: farm fields and route curves of the plots laid out past the 4 MB mark are missing
  (on stock: 793 of 1,316 farms without fields on the 2026-10-01 save), and on the product route the server's decal tap
  copies the EXE's decals, so the server's terrain picture misses them as well until the plot is laid out again [I:
  the tap "copies whatever the EXE holds", `terrain-decals.md` 3.2]. That is a D66 loss, bounded, against memory
  corruption, a hang or a crash without the guard.
- **How it fails loud (D118)**: install refused on the known EXE (a check above fails) = a `LOUD` line in `vpproxy.log`
  and a line in the error report (`proxy/src/proxy_report.cpp`), no popup: the game itself is not broken, only
  unprotected. A record larger than the scratch area (never seen: records are 128-2,304 B) = `LOUD`, forwarded
  unguarded as EngineQueueGuard does. Drops = one ordinary line per burst in `vpproxy.log` (records, bytes, largest,
  when) and a counter in the session report; whether the player should see a notice when farm fields were dropped is
  for the MC to decide (section 6).

### 4.2 (b) Batching fed by the EXE's swap counter (lossless in intent)

- **What it would do**: what #13397 does in the DLL (plot layout in batches of an estimated 256 KB of stream bytes, the
  next batch after one swap, plots past the budget left layout-dirty; `upstream-fix-batch-2.md` 2.3), with the swap
  counter coming from the EXE's process. Two placements:
  - (b1) **in the server's DLL** (the re-implementation `x64-fix-13397` with its EXE reader replaced by a value from the
    proxy): the proxy reads the flip counter (in-process, one load) and attaches it to every `ICvPlot1::UpdateLayout`
    and `ICvMap1::UpdateLayout` it forwards, or the server asks for it by a new call. Changes the DLL (behind a define,
    D104) and the IPC schema; the held `CvDllMap::UpdateLayout` and the deferred plots live in the server, and the
    re-arm in `CvGame::update` runs there.
  - (b2) **in the proxy alone**: the proxy forwards the server's `GameplayPlotStateChange` callbacks to the EXE; it can
    count their estimated stream bytes per frame (farm, route, other improvement, resource; the improvement's art from
    the database the proxy already reads) and, past the budget, keep the rest (last state per plot wins) and forward
    them after the next swap, from a per-frame point on the EXE's main thread. No DLL or schema change; the EXE sees
    each plot's state later than the server sent it.
- **Exact hook points**: (b1) the generated proxy methods `ICvPlot1::UpdateLayout(bool)` / `ICvMap1::UpdateLayout(bool)`
  (sync engine-to-DLL) and a new field or call; (b2) the proxy's handler of the server's
  `ICvEngineUtility4::GameplayPlotStateChange` (and the other layout callbacks the builders react to, which would have
  to be listed first) plus a per-frame callback on the main thread. Both read the flip counter (EXE base + 0x160F080 +
  index x 0x800180 + 0x800100), data only.
- **Game identity (D164)**: unchanged in both: only when the EXE draws a plot changes, not the game.
- **Fails loud**: the swap counter not found = no batching and a `LOUD` line (and then (a) is still needed as the
  backstop: the batching cannot cover writers it does not know).
- **Cost**: (b1) a DLL change, a schema change and a server-side state machine; (b2) a proxy-side queue and the list
  of callbacks that feed LandmarkSystem; both lengthen the time until the whole map is drawn (#13397: a 46-64 s hold on
  its dense save). **The art risk is measured against it**: 9075 found that plots first laid out after the EXE's load
  pass do not get the art the load pass gives them (a railway loop drawn in 2 of 2 pin arms, in 0 of 3 deferred arms,
  even with every deferred plot laid out in one pass) [L, `upstream-fix-batch-2.md` 2.3a]. (b1) is that same deferral;
  (b2) defers the EXE's side of it and is untested. So (b) is not lossless until a live check shows the deferred plots
  drawn as the pin draws them.

### 4.3 Comparison

| | (a) import-slot guard | (b) batching |
|---|---|---|
| prevents the overrun | yes, for every writer of the queue | only for the plot layout it batches |
| art | drops what does not fit (farm fields, route curves of the plots past 4 MB) | loses some route art in the DLL form [L]; proxy form untested |
| code | a port of 680 lines of fork code into the proxy, one data write | DLL + schema (b1) or a proxy-side queue (b2), plus (a) as backstop |
| identity | unchanged | unchanged |
| known on stock | every `-ia` game since 2026-09-21 [L] | 9075's re-implementation, rejected for art loss |

## 5. #13397 in upstream's full form (with the load-screen hold)

Read 2026-11-17 at its head 6d0c0f6d3 (open, "draft until #13379 is merged"; `gh pr view/diff 13397`), 21 files:
`CvMap.cpp` +399 (the batching, the hold, the warnings), `Exe/` (+#13379's address table), `CvLuaGame.cpp`
(`Game.ContinueDeferredLayout`, `Game.GetDeferredLayoutProgress`), and mod files: `LoadScreen.lua`/`.xml` of VP and of
the EUI compatibility pack, `DiploCorner.lua` (opens the chat panel outside network games) and two text files. What
the full form adds to 9075's re-implementation [C, the diff]:
- From `SequenceGameInitComplete`, `LoadScreen.lua` pauses the active player (single player) and calls
  `Game.ContinueDeferredLayout()` every frame from the load screen's update until it returns false, showing "Preparing
  map: N%". `ContinueDeferredLayout` runs `GC.getMap().updateLayout()` from that UI update when the game core is not
  executing and the last batch has been dispatched (one swap seen), and gives up after `MAX_LOAD_SCREEN_WAIT_MS` =
  600,000 ms.
- Chat warnings at 75 % of the stream and on overflow (the latter says a reload will not help).

What it changes in the picture on the stock route [I, not run]:
- **The owner**: the deferred plots are still laid out by `CvMap::updateLayout` after the EXE's load pass, only while the
  load screen is held instead of in game. 9075's separation test laid every deferred plot out in one in-game pass and the
  route loop was still missing; nothing in the hold changes which function lays the plots out, so the same loss is
  likely; upstream reports "no missing farms" on its dense save, which says nothing about route curves. A live check is
  needed before any take.
- **The T1100 failures**: 9074 showed them not caused by the overflow (the record-list break appears with no overflow:
  fix run 3; the guard build crashes at the same 0x55D9E2 site at T1150 and 0x55D947 at T1300), so neither form of
  #13397 is expected to cure them; the hold only moves the layout before the first frame.
- **Shipping it**: needs #13379 (not takeable: it removes `Game.SetExeWantForceResyncValue`, which 5.4.8's
  `DiploCorner.lua` calls each turn, and conflicts in `CvLuaSupport.cpp`; `upstream-fix-batch-2.md` 2.3) and new mod Lua
  that the patch does not ship (installer D26/D27). On the product route the DLL is in the server, so the hold would
  also need the swap counter over IPC (4.2 b1) and `Game.ContinueDeferredLayout` through the proxy's Lua bridge.
- **The one load of a 9074 save "if cheap"**: not done. It needs a 32-bit build of upstream master with #13379 and
  #13397, the 43-civ define and the instruments patch (the `-ia` conflict of 9075's `s06_ia_resolve.py` again), plus the
  changed `LoadScreen.lua` in a test instance's mods: hours of build and set-up, not cheap within this 4 h box. The
  check worth running, if the MC wants (b), is the art one on `C5H-9075-HUGE_T428` (the railway loop on the eastern
  peninsula at the start camera), where 9075's scripts already exist.

## 6. Recommendation and the implementation ticket

**Recommendation: (a), the import-slot guard in the proxy, as the product's protection; (b) not now.** The product
route overruns the queue on the loads where stock needs its guard (section 3.2), the overrun rewrites the flip counter
(memory corruption with the crash/hang/survive outcomes of 2026-09), and (a) is the one design that covers every writer,
is known on stock since 2026-09-21 and changes no game result. (b) costs a DLL or proxy state machine, still needs (a)
as its backstop, and its DLL form loses route art [L]; it is worth a ticket only if the dropped farm fields matter to
players once (a) is in, and only after an art check of the deferred layout (section 5).

**The implementation ticket (for the MC to open after epic 2600's merge frees `proxy/`)**, proposed text:
- *Scope*: port EngineQueueGuard into the proxy as `proxy/src/proxy_queue_guard.{h,cpp}` (origin named, D22); install
  from the mode=on boot next to `ProxyShell::OnBoot()`; never in mode=off; checks of 4.1 (pattern-free unless the MC
  rules otherwise on D83); pin the proxy once installed (corrected 2026-11-17: no pin at install, see 4.1); scratch 64 KB; drops counted (records, bytes, largest, last
  tick) and logged one line per burst after two quiet seconds (as EngineQueueGuard's `ReportDrops`, driven from the
  proxy's per-update call); install refusal and too-large records `LOUD`; counters in `vpproxy.log`'s end lines and the
  error report.
- *Tests*: offline, the thunk against a fake channel block (two buffers, a counter) and a fake slot: fits, exactly full,
  one byte over (diverted, size unchanged), concurrent writers (threads), non-queue addends forwarded, a value larger
  than the scratch (forwarded, `LOUD`), negative tests of each install check with the outputs and exit code checked
  (COMMON 9); one ASan run.
- *Live check*: this ticket's T1100 load (`server/evidence/0459/s02_ws_t1100.sh` with the new proxy) with the watch:
  buffer sizes never above 4,194,304, no flip-counter rewrite, a drop line in `vpproxy.log`; and one load of a save that
  does not overflow (the guard's status line, no drop).
- *Estimate*: 6-10 h (the port is 680 lines of known code; the tests and the live check are most of it).

**Open points for the MC**:
1. D83: may the proxy carry EXE code byte patterns for the guard's EXE check (as `proxy_shell.cpp` does for the view
   mask), or must it be pattern-free (4.1)? Recommendation: pattern-free; the stamp, the import slot found by name at its
   RVA and the data checks are enough for the one supported EXE build.
2. D118 for drops: a dropped farm field is a visible but bounded loss (D66). Recommendation: no popup; log and report
   only, as on stock.
3. **The fog install failure at the T1100 load** (section 3.2: 2 of 2 product loads ended by "no overlay pair yet
   (overlay WAITING)" 10 s after the map's first frame, the overlay tap never active) is a product bug of its own: T650
   overran and still got its overlay pair at 9.4 s of the 10-s wait (section 3.2). It needs a ticket (an MC-level bug
   id) owned by the fog/overlay side: the overlay's first pair on a 180 x 112, 43-civ late load comes later than the
   fog's 10-s install wait allows. Evidence: the server runs `20261117-052759-56572-1` and `20261117-054528-20216-1` in
   `F:/c5-64bit-work/0459/`.
4. The live runs were on the workstation (COMMON 23 move from oldpc); a check on oldpc would show whether a quieter
   machine keeps T650 under the limit (the stall length depends on the machine) [not run].

**MC decisions (2026-11-17, #3839)**: open point 1 pattern-free (no EXE code byte patterns in the proxy for the guard);
open point 2 no popup, drops logged and reported. Open point 3 became ticket 0491, the implementation ticket 0492
(section 7).

## 7. The implementation (ticket 0492, 2026-11-17)

Branch `task-0492-proxy-ui-queue-guard` (account B). Code `proxy/src/proxy_queue_guard.{h,cpp}` (ported from the fork's
`EngineQueueGuard.{h,cpp}`, branch `engine-queue-guard`, commit ac7afabc2, origin named in the file, D22).

### 7.1 What the proxy does [C]

- **Install**: `ProxyShell::OnBoot()` (proxy_shell.cpp) calls `ProxyQueueGuard::Install()` first, i.e. from the mode=on
  boot in `DllGetGameContext` after a successful boot, before the composite is loaded and before the front end can load
  a save. mode=off never calls `OnBoot`, so the guard is never installed there (an `-ia` test DLL brings its own).
- **Checks, pattern-free (MC decision)**: the image at `GetModuleHandle(NULL)` has a DOS and NT header, time stamp
  0x546CD0A8, `SizeOfImage` at least channels RVA 0x160F080 + 2 x 0x800180; the import directory has a `KERNEL32.dll`
  descriptor with `InterlockedExchangeAdd` imported by name, and that slot is at EXE base + 0x5D647C; the slot is bound
  (non-zero) and not already the guard; the channel index (RVA 0x160E400) is readable and 0 or 1; all four size fields
  (two channels x two buffers) are readable and at most 4,194,304; both flip counters (channel + 0x800100) are readable.
  Every structure is checked with `VirtualQuery` before it is read; no EXE code byte is read. The slot's value is compared
  with `GetProcAddress(kernel32, "InterlockedExchangeAdd")`; a different value (another module hooked it first) is a
  `LOUD` line and the guard forwards to whatever was there.
- **The thunk**: the origin's, unchanged in behaviour: for an addend that is one of the four size fields and a positive
  value, reserve with compare-and-swap if `old <= capacity && value <= capacity - old`, else, if `value <= 64 KB`,
  return `scratch - (size field + 0x80)` (the record lands in a 64 KB `VirtualAlloc` scratch area, the size field
  unchanged, so the dispatcher never reaches it) and count records, bytes, the largest and the tick; a larger value is
  forwarded to the previous slot value (the EXE's own overrun) and counted as "too large". Every other call is forwarded
  to the previous slot value. The thunk never logs (it runs on the EXE's writer threads).
- **Logging**: one line at install (`queue guard: UI message queue guard active (DX11 build; slot 0x..., channels 0x...,
  index N; scratch 64 KB)...`) or the refusal (`LOUD: queue guard: UI message queue guard off: <reason>; the EXE's queue
  is not protected (ticket 0492)`, not LOUD for another EXE's time stamp). `ProxyShell::OnUpdate()` (top of each
  `ICvGame1::Update`) calls `ProxyQueueGuard::OnUpdate()`: one line per burst once two seconds have passed without a
  drop (`queue guard: dropped N records (K KB) that did not fit the EXE's UI message queue; since the boot ... in B
  burst(s), largest L bytes, T too large to divert`), and a `LOUD` line for every new too-large record. The end lines of
  vpproxy.log (`WriteStats`, also on `Fatal`) and the error report's session block (`ReportFailure`) carry
  `UI queue guard: <status>; dropped ... records (... KB) in ... reported burst(s), largest ..., ... too large to divert`
  (two calls in `proxy_rt.cpp`). Drops raise no popup (MC decision, D118 not raised).
- **Unload (correction of 4.1's "pin once installed", MC accepted 2026-11-17)**: the design proposed pinning the proxy at install. That would
  keep the image across the EXE's `FreeLibrary` of the game DLL, so the next `LoadLibrary` would get the old image with
  its globals (boot state 1, the dead server's context) instead of a fresh boot, which issue 3308 supports (the proxy can
  boot more than once in one EXE process). So `ProxyShell::OnUnload()` gives the slot its previous value back with a
  compare-and-swap (only if it still holds the thunk), and the image is pinned (`GetModuleHandleEx` with
  `GET_MODULE_HANDLE_EX_FLAG_PIN`) only when `OnUpdate` sees another value on top of the thunk (another module hooked the
  slot after the proxy and may forward to it; `LOUD`, said once). The scratch area is never freed (a writer may hold a
  pointer into it; 64 KB per boot). Left open [I]: a writer that read the slot just before the restore and calls the
  thunk after the image is unmapped (a window of a few instructions under the loader lock at a mod-set change).

### 7.2 Offline tests [C, 2026-11-17]

`proxy/tools/test_proxy_rt.py` section 7 (`--only-queue`, `--asan`): `proxy/tools/rt_tests/queue_guard_unit.cpp` linked
with `proxy_queue_guard.cpp`, the proxy's toolchain (clang-cl -m32, VC9 headers), against a fake EXE image (38 MB
`VirtualAlloc`, a PE header with an import directory whose first descriptor is a decoy `USER32.dll` that also names
`InterlockedExchangeAdd`). 114 checks: install and status line, a second call does nothing, a slot bound elsewhere
(installed, LOUD, forwards to it), the product `Install()` in the test program (refused by the time stamp, not LOUD);
fits, exactly full, one byte over (diverted, size unchanged, counted), one byte under, all four size fields, a 64 KB
record written in full into the scratch area, a size field already above capacity; foreign addends forwarded (a
variable, the flip counter, a header field, just below the channels, a third channel's place, a negative and a zero
value on a size field); a record above 64 KB on a full buffer forwarded and LOUD once; the report (no line under two
seconds, one per burst, none without new drops, the totals); 8 writer threads, 32,000 records of 128-2,304 B (38 MB) on
one buffer: size never above capacity, the dispatcher-style walk ends exactly at the size, accepted + dropped =
attempted (3,433 accepted = 4,194,304 B, 28,567 dropped); the unload restore, a hook on top (LOUD, pinned, left alone at
the unload, still reaches the guard); and 17 refusals (each check, the reason, the slot unchanged, LOUD or not). Then the
program's own failure path (`--expect-fail`: exit code 1, one FAIL line). Result: 114/114, the negative case passes; the
same under MSVC `/fsanitize=address` (x86) 114/114 with the canary's heap-buffer-overflow caught (2026-11-17 06:35).

### 7.3 Live check [L, 2026-11-17, workstation instance c5h-h]

Layout and build: the same as section 3.1 (product path, D33 development layout via `pp_4255.py prep`, pack=0,
ServerRenderer = 1, Overlay on; x64 DLL 0334's m43 build cf981126..., the 43-civ variant of the x64 pin), with server.exe
(a293ff49...), the proxy (1de72650...) and the composite (20638623...) built at this branch
(`server/evidence/0492/s01_build.sh`, against a private copy of the m43 source and deps). Driver
`server/evidence/0492/s02_live.sh` (0459's `s02_ws_t1100.sh` with this build; the read-only watch
`server/evidence/0459/qwatch.py` beside it). Evidence: `server/evidence/0492/t1100/` (watch log, JSON summary, per-second
CSV, the vpproxy.log and server.log excerpts); work folder `C:/c5-64bit-work/0492/live-t1100-run1/`.

**T1100 (06:42:35-06:50:41 EST, EXE pid 72500, EXE base 0x00530000)**:
- Install, 1.13 s after the proxy's boot: `queue guard: UI message queue guard active (DX11 build; slot 0x00B0647C,
  channels 0x01B3F080, index 0; scratch 64 KB)`: the slot found by name at base + 0x5D647C on a relocated EXE.
- The watch: buffer 1 filled from 675,456 B (flip 9) to **exactly 4,194,304 B at 06:47:15 and stayed there for 71 s**
  (to 06:48:26) while the main thread did not swap (the longest stretch without a flip 123.8 s, ending 06:48:21);
  **never above 4,194,304** (samples above capacity 0 of 84,216; 18,259 at or above 90 %); the flip counter went
  0, 2, 3, 4, 7, 8, 9, 10, 11, 20, 41-44, i.e. **no rewrite** (0459's unguarded runs: 9 -> 128); the high-water mark
  read 4,194,304 (the guard's cap, not a record's type). Buffer 0's peak 2,694,400 B (64 %). 90 read failures, all as
  the process ended.
- vpproxy.log, from the game-core thread at 298.1 s: `queue guard: dropped 19139 records (3182 KB) that did not fit the
  EXE's UI message queue; since the boot 19139 records (3182 KB) in 1 burst(s), largest 256 bytes, 0 too large to
  divert`. **3,182 KB is the same amount stock's `-ia` guard dropped from this save's load** (9074, section 3.1) [L].
  The end line (301.3 s, written by `Fatal`) carries the same counters: `UI queue guard: active (...); dropped 19139
  records (3182 KB) in 1 reported burst(s), largest 256 bytes, 0 too large to divert`.
- The run then ended as 0459's did, by the fog install check (server.log 300.0 s `fog of war: no overlay pair yet
  (overlay WAITING) 10002 ms ...`, exit 5; the proxy's FATAL exit 3; map's first frame 290.0 s), ticket 0491's bug, not
  this one's; no box (`dialogwatch.py marker`: none). So the guard removes the overrun but not the T1100 fog exit,
  as section 3.2 predicted (T650 overran and still got its overlay pair) [L].

**T600, the load that does not overflow (06:51:16-07:01:40 EST, EXE pid 7704)**: the install line as above (1.55 s);
the watch's peaks 2,415,488 B (57.6 %) and 2,965,632 B (70.7 %, the AI turn), none at 90 %, flips 0 -> 12,374 with no
jump, 0 read failures (117,359 samples); turns 600 -> 601 -> 602 played (acceptance rc 0; 190.8 s and 88.8 s per turn);
**no drop line**, no LOUD line of the guard, no box. vpproxy.log has no end lines in this run: acceptance.py ends the
EXE by its stop (a terminate), and the proxy writes its end lines (`WriteStats`) only at an orderly unload or a `Fatal`,
not at a process exit (`DllMain` skips it when `reserved != NULL`, unchanged by this ticket) [C]; so on a load without
drops the status line is the install line. Evidence `server/evidence/0492/t600/`. Two acceptance.py pictures of this
run are in the Visual Checks feed, amended as diagnostics (development layout, not D112).

**Not covered live** [I]: the unload restore and a second boot in one EXE process (a mod-set change; covered offline
only, 7.2), the LOUD paths (none occurred), and T650 (not rerun; it overran without a guard in 3.2 and lies between
these two).

## 8. Which 32-bit DLLs carry a queue guard, and what a player with the patch off runs (ticket 0635, 2026-11-21)

Ticket 0635 assumed that the shipped 32-bit DLL guards the EXE's UI message queue on build 4390913 and loses that guard
on build 26577625. That assumption is wrong [C, impl-0635, 2026-11-21 01:58-02:05 EST]:

- **The 32-bit pin has no queue guard.** `git grep` of the fork at i0-0.1b `f33fc46a4` (`deps/i0.lock`) finds no
  `EngineQueueGuard` file and no guard text. The release builds agree: the plain DLLs `i0ship-e96457fd`
  (the pin folder that `deps/i0.lock` names) and `i0ship-5d18d3d6` (`F:/c5-64bit-work/0551/layouts/`) hold 0 occurrences of the
  string `UI message queue guard`. Their `-ia` variants (`i0ship-e96457fd-ia`, `i0ship-5d18d3d6-ia`,
  `i0ship-5d18d3d6-ia4968`) hold 2 each.
- **The guard is part of the instruments patch only.** `EngineQueueGuard` (section 4.1) is in the `-ia` patch rows of
  `tools/release/stages/build.py` `IA_PATCHES` (now `tools/d10/evidence/4968/ia-5d18d3d6c-4968.patch`). That patch
  makes the test-instance DLL. It is never shipped: a release ships no `-ia` DLL (ticket 0484). The patch's guard knows
  one EXE by its PE time stamp and also checks committed EXE code-byte patterns and address checks of build 4390913.
  On build 26577625 it therefore writes `off: not the EXE build this was written for (timestamp)` to
  `crashlogs\queueguard.log`. Ticket 0626 saw that line on c5h-new-a's stock route, with the `-ia` DLL installed.
- **A player never runs our 32-bit pin.** With the off switch (`vpproxy.ini` `mode=off`, `vp64helper off`) or a pack
  fallback, the proxy loads `fallback_dll`. That file is the player's own Vox Populi DLL, which the installer copies to
  `CvGameCore_Expansion2_x86.dll` after checking its hash (`research/06-tooling-and-workflows/installer-notes.md`
  section 3, item 2, and the layout block). The proxy installs its own guard only from `ProxyShell::OnBoot()`, and
  `proxy_rt.cpp` calls `OnBoot` only when not in off mode (`if (!g_offMode) ProxyShell::OnBoot();`, line 2910 at
  main 553a0c3ae).
- **Consequence:** a player with the patch off, or in a fallback, has no guard of the queue on either EXE build. That
  is stock Vox Populi behaviour. Moving to build 26577625 removes no protection on a player path. Only the `-ia` test
  DLL's stock arms lose their guard there, which matters for test comparisons, not for players.
- **The mitigation that exists:** the proxy's guard (section 7). It is already carried to build 26577625 through the
  local tables (`exe-build-table.md` section 11; live in 4967 part 1, 2026-11-20). If `BootOff` installed it too, then
  mode=off and the fallback would be guarded on both builds, with no fork change, no new 32-bit pin and no new address
  in the repository. The open point is an `-ia` fallback DLL on test instances: on build 4390913 its own guard would
  hook on top of the proxy's.
- **Decision:** asked on queue thread 0635 (#8995) and forwarded to the MC as desk 0136 #8997, with three options:
  (A) close 0635 because its premise is wrong; (B, recommended) install the proxy's guard in mode=off and the
  fallback; (C) the ticket as written, which can change only the `-ia` test DLL. **Ruled (B)** by the MC (#8999), and
  decision D224 approved it ("Yes, guard it", 2026-11-21 about 02:12 EST). The fork and `deps/i0.lock` stay as they
  are.
- **The before** [L, a vpproxy.log on c5h-new-a, build 26577625, 2026-11-21 02:03 EST, proxy of repository a4c1c8428]:
  a mode=off start writes four lines: the build line, the boot line (`mode=off`), the ini line and `boot: mode=off: the
  original 32-bit DLL ... loaded ...; no server is started`. There is no `queue guard:` line and no farm guard line, so
  no guard of the proxy runs there. The known-crash list (`research/06-tooling-and-workflows/known-crash-causes.md`,
  MC 2026-11-21) records the same for the farm builder guard and the UI draw-list growth: both are installed only by
  `ProxyShell::OnBoot`. Each of them in off mode needed its own decision (D225, 8.2); this section covers the queue guard.

### 8.1 The queue guard with the patch switched off (ticket 0635, D224) [C: code and offline tests, 2026-11-21]

- **Where it is installed:** `BootOff` in `proxy/src/proxy_rt.cpp` covers mode=off, `failed=1` and the pack fallback.
  It installs the guard right after the 32-bit DLL's first `DllGetGameContext` has returned a context, through
  `ProxyQueueGuard::InstallWhenOff()`. A D33 session never reaches `BootOff` (no play, D125), and an unknown EXE build
  keeps its fallback route.
- **The switch:** `VP64Options.ini` `[Proxy] GuardsWhenOff` (options id 29, default 1; MC #9004 and #9011; id 28 is
  reserved for epic 0042 stage 5's `[Renderer] ServerCombat`). `BootOff` reads the file with the options parser and
  never creates or rewrites it, because `OptionsBoot` does not run when the 64-bit side does not start. The key is
  one switch for every guard of off mode (the MC's ruling #9011); today it covers only the queue guard
  (`docs/OPTIONS.md`). The boot line names the value and where it came from, for example
  `boot: mode=off: [Proxy] GuardsWhenOff = 1 (default; options file <path>: read): the proxy installs its UI message
  queue guard (ticket 0635)`. With 0 the line is `LOUD: boot: ...: the UI message queue guard is LEFT OUT; ...`.
- **No hook on top of another hook (acceptance point 3):** `InstallWhenOff` runs every check of `Install` (section
  7.1). It also refuses when the import slot no longer holds `KERNEL32!InterlockedExchangeAdd`, as returned by
  `GetProcAddress`. The `-ia` test DLL's own `EngineQueueGuard` installs in that DLL's `DllGetGameContext` on build
  4390913, before the proxy tries, so the proxy then logs `LOUD: queue guard: UI message queue guard off: the import
  slot is already hooked (it holds 0x... in <module>, not KERNEL32's own function): with the patch switched off the
  proxy does not hook on top ...` and leaves the slot alone. The mode=on `Install` keeps its old rule: it chains to a
  foreign value, with a LOUD line. Both outcomes are LOUD in mode=off (MC #9004: "log which of the two happened,
  loudly"). An installed guard's line reads `LOUD: queue guard: UI message queue guard active (DX11 build; ...; the
  patch is switched off: the 32-bit DLL answers) ...`. An unknown EXE build stays quiet, as before.
- **Reports in mode=off:** mode=off has no `ICvGame1::Update` hook. So the drop report (one line per burst) and the
  check for a hook placed on top later run from the forwarded `DllGetGameContext` calls of the boot thread. How
  often the EXE calls it in a game is not measured [I]. At an orderly unload (`FreeLibrary`) the proxy writes
  `detach: mode=off: UI queue guard: <status>; dropped ...` before the slot is given back. A process exit writes no
  end line, as in mode=on.
- **Unload:** unchanged. `DetachOnUnload` calls `ProxyShell::OnUnload`, which restores the slot by compare-and-swap,
  before it frees the fallback DLL.
- **Offline tests** [C, 2026-11-21]: `queue_guard_unit.cpp` `TestWhenOff`, run on both builds' layouts (see the
  counts in ticket 0635's log). It covers: installed when the slot holds the bound function (LOUD, the status names
  it) and guarding (a divert and a forward); one try per image; a slot held by another module refused, LOUD,
  untouched, with the module named, while `Install` still chains on top; no bound function refused; an unknown build
  quiet; a hook on top afterwards pinned. `test_proxy_rt.py` loader cases `off` and `off-qg0`: the option line comes
  after the fallback's load; the loader is no known EXE, so the guard says so quietly and the detach line repeats it;
  with `GuardsWhenOff = 0` the guard is LEFT OUT, nothing is tried and the options file is unchanged. The options
  tests check row 29: default, range, warnings, the `[Proxy]` block before `[Extras]`, and the reserved 28.

### 8.2 The other two guards in off mode (decision D225, 2026-11-21: "Approve all three") [C: code, 2026-11-21]

- **The farm builder guard with rule 2 (known-crash rows K1, K2): in.** It works on the EXE's own farm builder (the
  listener node of queue B's type 59) and the layout graph, never on the DLL (`proxy/src/proxy_farm_guard.h`,
  `research/02-exe-internals/farm-builder-guard.md`). So the player's own VP DLL changes nothing for it.
  `ProxyShell::OnBootOff()` runs the same install as `OnBoot` (`InstallFarmGuard`: the shared page, the keys of
  `shell_composite.ini`, the watcher) and then `SetComposite(false)`: no composite loads in off mode, so
  `farm_builder = 0` does not act and the builder is always called through the guard. `BootOff` calls it right after
  `InstallWhenOff`, under the same key; `ProxyShell::OnUnload` stops it at an unload, as after `OnBoot`. Log:
  `shell: the patch is switched off: the farm builder guard is installed as on a 64-bit start (ticket 0635)`, then the
  guard's own `farm_guard:` lines.
- **The UI draw-list growth (K4): not possible with a player's own VP DLL, left out.** `ProxyUiDraw::OnFrame` must
  run on the EXE's main thread once per in-game frame, at the point where a half of the stream is empty
  (`proxy_uidraw.h`). Its only caller is the proxy's Lua bridge, `ProxyCombatHold::Init(ProxyUiDraw::OnFrame)` in
  `proxy_ui_bridge_rt.cpp`, driven by the bridge's listener of `Events.LocalMachineAppUpdate`. The bridge is installed
  by `ProxyUiBridgeRt::OnStateRegistered`, which the proxy's `ICvScriptSystemUtility1::RegisterScriptLibraries` hook
  calls (`proxy_lua.cpp` line 1272 at this branch). That hook exists only while the proxy answers the EXE's DLL
  interfaces, which is the mode=on path. In off mode `DllGetGameContext` returns the player's DLL's own context, the
  proxy is on no interface path, and nothing of it runs on the main thread per frame. Upstream's own form (#13406)
  needs a Lua line VP 5.4.8 lacks (known-crash list, K4). A route that would work is to wrap the fallback DLL's context
  object so that the proxy sees `RegisterScriptLibraries`. That puts the proxy on every interface call of the stock
  game, a far larger change than a guard, and it is not proposed for the first beta [I]. The MC confirmed it is left
  out and not built (#9041).

### 8.3 Live smoke (ticket 0635) [L, 2026-11-21, workstation]

Setup for each arm: `provision.py set-proxy` lays the proxy layout with `vpproxy.ini mode=off` and, as
`fallback_dll`, a plain 43-civ VP DLL with no guard and no Lua channel. That DLL is `i0ship-26234371-m43`
(6E2A7BE1..., `deps/i0.lock` `release-43`), a stand-in for a player's own DLL. The save is 9074's
`TS1.20-T0800.Civ5Save` (180 x 112, 42 majors, Marathon), loaded as a copy under its own name; stock's `-ia` guard
dropped 1,448 KB from it at the load's end (`marathon-stress-t2000.md` 4). Driver `server/evidence/0635/smoke_off.py`:
the harness front end loads the save; the read-only watch `server/evidence/0635/qwatch.py` (0459's, with the
addresses from `tools/exebuild/resolve.py`) runs beside it; after 660 s comes the harness stop (stop_nolua, saves
settled 60 s). Proxy and server built at this branch (proxy 74f5031b...). Excerpts with private paths and the new
build's addresses replaced are in `server/evidence/0635/<arm>/`.

| arm | build | GuardsWhenOff | vpproxy.log | watch: largest size of buffer 1 | flip counter | result |
|---|---|---|---|---|---|---|
| c-on (02:55-03:08) | 4390913 (c5h-c) | 1 | both guards installed after the fallback's load; `queue guard: dropped 8917 records (1451 KB) ...` at 177.7 s, from the boot thread (the forwarded `DllGetGameContext` path); farm guard: 3,599 builder calls, 9 skipped by rule 2 (grid 80,640 cells, above 65,535), every check sound | exactly 4,194,304 (capped), 0 samples above | 0 -> 13,817, no jump | survived, stopped cleanly |
| c-off (03:10-...) | 4390913 (c5h-c) | 0 | `LOUD: boot: mode=off: [Proxy] GuardsWhenOff = 0 (from the file ...): the guards are LEFT OUT ...`, no `queue guard:` or `farm_guard:` line | **4,194,432** at 03:14:10 (one 128-byte record past the end) | rewritten to **128**, high-water mark 13 | the overrun reproduced (the negative) |

The 1,451 KB dropped in mode=off match the 1,448 KB that stock's `-ia` guard dropped from the same save [L]. So the
proxy's guard in off mode does what the test DLL's guard does, and with the key at 0 the overrun of
`message-queue-and-fixed-buffers.md` 1 comes back with its known signature.

Two more starts on c5h-c [L, 2026-11-21]:
- **c-d33 (03:31-03:33): the off switch with config.ini in the D33 form (section 8.4).** `d33_off.py setup` backed up
  config.ini, vpproxy.ini and VP64Options.ini by SHA-256, ran `vp64helper on` (config.ini got the keys Decal, Forest,
  FOW, FluidFOW, DistanceFog and YieldIcons) and wrote `mode=off` into vpproxy.ini by hand. The guard ran at mod
  activation: `boot: NO PLAY: the patch is switched off (vpproxy.ini mode=off), but config.ini is in the patch's form:
  D33 session: config mask 0xFFEC3F67 ...`. The report went to `VP64\reports\20261121-033213`, with stage off-switch
  and code config-d33. `vp64helper failed --pid` started, and the EXE ended with exit code 3 0.18 s after the proxy's
  boot. No map was drawn: the EXE ended in the front end. The helper then restored config.ini; its SHA-256 equals the
  backup's (ACBA04B4...), so the next start is the normal game. Its popup was closed with `cli.py dismiss --patch-popup`
  after the game had ended. That build still showed the generic words ("The 64-bit part of the patch failed ..."),
  which the off-switch wording of 8.4 replaces. The rerun with the new words is on c5h-new-b.
- **c-mode-on (03:36-03:40): one normal 64-bit start, unchanged.** vpproxy.ini at mode=on, the same build, stopped at
  the menu. The log reads `options: [Proxy] GuardsWhenOff = 1 (default)` and `queue guard: UI message queue guard
  active (DX11 build; ...); ... (ticket 0492)`, without LOUD and without the off-mode words, followed by the farm
  guard's owner lines, as before this ticket.

Each run's files were put back by hash, the layout was removed (`set-proxy --original`), `provision.py verify c5h-c`
passed, and the instance was released at 03:42.

**Build 26577625 (c5h-new-b, F:), 2026-11-21 from 10:5x** (the merge condition; account B's limit stopped the 05:25 slot).
The same layout, built from the final tree (`build4`, proxy af713c3f...). The watch resolved the queue through the
local table (build 26577625). The excerpts in `server/evidence/0635/n-*/` have every address replaced by `<addr>`.

| arm | GuardsWhenOff | vpproxy.log | watch: largest size of buffer 1 | flip counter | result |
|---|---|---|---|---|---|
| n-on (10:5x-11:07) | 1 | both guards installed after the fallback's load (`LOUD: queue guard: ... active (DX11 build; slot <addr>, ...; the patch is switched off ...)`); the farm guard's 13 patterns match build 26577625; `queue guard: dropped 8962 records (1457 KB)` at 228.6 s; farm guard: 3,599 calls, 9 skipped by rule 2, all sound | exactly 4,194,304 (capped), 0 samples above | 0 -> 13,215, no jump | survived, stopped cleanly |
| n-off (11:08-11:21) | 0 | `LOUD: boot: mode=off: [Proxy] GuardsWhenOff = 0 (from the file ...): the guards are LEFT OUT ...`, no queue or farm guard line | **4,199,040** at 11:11:27, then up to **5,645,568** (1.45 MB past the end; 9,623 samples above the capacity) | 9 at the first overrun; no jump seen this time (the first record crossing the end did not start exactly at it, so no record header landed on the counter) [I] | the overrun reproduced (the negative); the game survived to the stop |

- **n-d33 (11:22-11:27), the off switch with config.ini in the D33 form, with the new words:** `boot: NO PLAY: the
  patch is switched off (vpproxy.ini mode=off), but config.ini is in the patch's form ...`, the report
  (`VP64\reports\20261121-112616`, stage off-switch, code config-d33, the intro line of 8.4), `vp64helper failed --pid`
  started, and `FATAL ... exit code 3` 0.22 s after the boot. No map was drawn. The popup showed the title "Vox Populi
  64-bit patch: error report", MC #9080's two paragraphs word for word, then the Details line and the report path,
  with the buttons Open folder and Close (no "Try again next start"). config.ini was restored by the helper (SHA-256
  equal to the backup, 1521591B...). The popup was closed with `cli.py dismiss --patch-popup` after the game ended;
  `server/evidence/0635/n-d33/popup.txt` holds its text.

### 8.4 The off switch with config.ini still in the patch's form: no play (MC #9064, #9072, #9080) [C: code and offline tests]

Found by 0626's rehearsal (#9063): its installed stock route had `mode=off` set by hand while config.ini stayed in the
D33 form that `vp64helper on` writes. The 32-bit game then ran with the stock systems off and drew a deep red map at
turn 0. That is a degraded world, which D118 rules out.
- **Where it is caught:** `DllGetGameContext`, before `BootOff`, on the off-switch path: mode=off, a missing ini, or
  `failed=1`. The pack fallback in a D33 session was already no play (issue 3523: `SessionKind` decides before
  `BootOff`), and so was the exe-build gate's A'' form (0588) [C]. So the red map could only come from the off switch.
- **The rule:** when the masks were actually READ (the EXE's own, config.ini's keys, or the test masks) and give a D33
  session, the proxy takes the D125 route through `OffD33NoPlay`: `boot: NO PLAY: ...`, the D118 report with stage
  `off-switch` and code `config-d33`, `vp64helper failed --pid`, then `Fatal(3)`; no 32-bit DLL is loaded. `vp64helper
  failed` gives config.ini its player values back after the EXE's end, from `vp64-config-side.ini`. **The exception
  (agreed by the MC, #9072):** a session only ASSUMED to be D33, because no mask was readable, keeps the 32-bit game,
  with a LOUD line. The off switch is the player's way out and is not refused on a guess.
- **The words (MC #9080):** the popup's title is unchanged. For this stage the first paragraph of `vp64helper.cpp`
  `CmdFailed` reads "The patch is switched off, but the game's settings file (config.ini) was still in the form the
  patch uses. The game would have drawn a broken map, so it was ended before the map appeared. Your config.ini is back
  to your own settings now: start the game again and it plays as the normal game, with the patch off." The second
  reads "To switch the patch back on, run vp64helper.exe on. To switch it off in future, run vp64helper.exe off
  instead of editing vpproxy.ini by hand; it puts config.ini back for you." There is no "Try again next start" button.
  Without a usable side file the first paragraph instead says that config.ini was left as it is, and how to put it
  back. report.txt's intro is one line of its own for the stage. The proxy's line follows as "Details: ..." and is
  popup-summary.txt. Every other stage keeps the generic words. The report's session line now gives vpproxy.ini's
  mode as it is (it always said mode=on).
- **Offline tests:** `test_proxy_session.py` cases off-d33 and failed1-d33 (no play, exit 3, the report's stage,
  code, intro and details, the helper's command line), off-stock and off-nomask (the 32-bit game as before). The
  earlier failed1 case now uses a stock config.ini, as the helper leaves it after the game's end. `test_vp64helper.py`
  covers the off-switch popup with and without a side file, and the generic words for another stage. Counts are in
  ticket 0635's log.


