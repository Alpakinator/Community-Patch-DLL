# The composite's log per process, the taps' reader gates, the ice image off the render thread (ticket 0385, with 0384)

Ticket 0385 (seam G follow-ups of epic 0169, 0384 folded in), account B, branch `task-0385-seam-g-followups`,
2026-11-16. Code: `shell/composite/shell_composite.cpp` (the log), `shell/composite/shadow_tap.{h,cpp}`,
`shell/composite/ice_tap.{h,cpp}`. Tests: `shell/tests/test_composite_log.py` (new), `shell/tests/test_shadow_tap.py`,
`shell/tests/test_ice_tap.py`. Evidence and the oldpc A/B: `shell/evidence/0385/`. Earlier statements this file
corrects: `polar-ice.md` section 9 "Cost" (the copy's one-frame hitch, "not done") and `terrain-surface.md` section
14.4's driver note (the log appends across processes). `terrain-surface.md` carries a dated correction line (MC's
answer on the ticket's thread, 2026-11-16); `polar-ice.md` is held by ticket 0382, so the MC adds its line at the merge.

## 1. The composite's log starts per EXE process [C, tests]

Before (2026-11-15, `terrain-surface.md` 14.4): `shell_composite.dll` opened `<out>\shell_composite.log` with
`_wfsopen(.., "a", _SH_DENYNO)` in `DllMain`, so every EXE process appended to the lines of the ones before (drivers had
to cut at the last `==== shell_composite.dll ... loaded` line). `<out>` is `VPSHELL_OUT_DIR` (the proxy sets it to the
instance's `Logs` folder, issue 0148/0170) or the DLL's folder.

Now (`PrepareDefaultLog`, called in `DllMain` before the open; kernel32 only, under the loader lock):

| Case | What happens | The process's first lines say |
|---|---|---|
| `shell_composite.log` exists with content | renamed to `shell_composite.<its last write, local time yyyymmdd-hhmmss>.log`; if that name is taken, `-2`, `-3` ... `-99`; the new process opens a fresh `shell_composite.log` | `log: the previous log kept as <path>` |
| the rename is refused (another process has the file open: the CRT opens it without `FILE_SHARE_DELETE`, so a second EXE on the same folder gets error 32) | the old file is not touched; this process logs to `shell_composite.<pid>.log` | `log: <path> could not be renamed (error 32: another process has it open): this process logs here` |
| no free name among the 99 | as above, `shell_composite.<pid>.log` | `log: <path> kept (no free name for it): this process logs here` |
| the old file is empty | deleted (nothing to keep) | nothing |
| `VPSHELL_LOG` set (a path the caller owns: the shell tests' per-run log) | appended to as before, never renamed | nothing |

Nothing is ever deleted that holds a line. Old logs accumulate in `<out>` (one file per EXE process); no retention limit
was set (a limit would delete lines; a decision for the MC if the folder's size matters). Tools that read
`shell_composite.log` now see the current (or last) process only; a driver that recorded the file's size before a launch
and reads from that offset after it (`run_9040.py`, `run_3306.py` / `run_3308.py` / `run_3311.py` / `run_3313.py` per-step
slices, `pp_0349.py`) reads from 0 of a new, shorter file after a restart: their per-step slices inside one process are
unaffected, a slice taken across a relaunch now starts at a fresh file [I: the drivers are history; none was re-run].

Tests (`test_composite_log.py`, test_host on the hidden desktop, `VPSHELL_OUT_DIR` = a temporary folder, `VPSHELL_LOG`
removed): the rename with its time stamp and a second process (two files kept, each process's file holds its own banner
only); a taken name gets `-3` while `-1` and `-2` exist (both untouched); the negative case (the old log held open by
another process: left byte-equal, this process logs to `shell_composite.<pid>.log` with the error-32 line); an empty old
log removed; `VPSHELL_LOG` still appends.

## 2. The shadow tap pauses while nothing reads it [C, tests]

The server's shadow reader (`server/src/render/srv_shadow_tap.cpp`) acks every generation it binds (`ackGeneration`,
`ackServerPid` of `VPShellShadowTap`); it binds only a new generation. Before 0385 the producer copied ShadowBlur every
map frame whether or not anything read it: 4312's `tapsonly` arm (server.cfg `render_shadow=0`, `render_ice=0`, the taps
on) counted 32,000 unread copies in a game, CPU mean 0.0009 ms each (`terrain-surface.md` 13, the table of done D).

Now (`shadow_tap.cpp`, `TrackReader` and `CopyNow`, every map frame after the in-flight copy is polled):

- a generation published and not acked starts a clock; an ack that moves resets it (and restarts it at once when
  generations newer than the ack exist); all generations acked: no clock;
- after `shadow_idle_ms` (default 2000) on the clock the copies pause: the frame's values are still read and the
  texture resolved and checked (faults and description changes are found as before), but no slot is written, except
  one probe copy every `shadow_probe_ms` (default 250). A reader that comes (or comes back) binds the newest frame (at
  most one probe period old), acks it, and from the next map frame every frame is copied again;
- a skipped copy is neither a copy (`copiesTotal`) nor a missed one (`missedTotal`), and a frame in which a probe is
  still in flight is not counted as a lag: the reader's map-frame count (copies + missed) stays, so a reader that is
  there but does not ack (it refuses every slot) sees a gap without map frames, which it notes as "not a lag";
- lines: `shadow_tap: no reader: no copy acked for N ms (generation G, ack A): copies paused, one probe copy every P ms
  (episode E; ...)` and `shadow_tap: reader back: generation G acked by pid P after N ms unread (S copies skipped, Q
  probe copies): a copy every map frame again`, at most 20 of them per process, then counted only; the 1000-copy line
  adds `unread: E episodes, S skipped, Q probes`;
- `shadow_idle_ms=0`: never pause (the behaviour before 0385). The kill switch resets the gate.

No layout change: nothing new in `shell_shadow_tap.h` (the counters are in the log only). What "nothing reads" covers:
no server, a server without the shadow reader (`render_shadow=0`), a server whose render loop stalls for more than 2 s
(then one probe frame is bound when it returns; the reader can show that frame for one draw), and a reader that refuses
every slot (it gets a new candidate every 250 ms instead of every frame).

Tests (`test_shadow_tap.py`; the older tests set `shadow_idle_ms=0` themselves, since they start their reader seconds
after the copies began or count copies without a reader): pause after 500 ms with probes every 200 ms (2 to 8
generations a second against hundreds), missed and faults unchanged, then the reader's 30 triples exact and consecutive,
the "reader back" line with more than 50 skipped copies, and a second episode after the reader has gone; the defaults
(the pause after 2000-4000 ms, probes every 250 ms); `shadow_idle_ms=0` never pauses (more than 50 copies a second
after 3 s, no "no reader" line).

Live [L, 2026-11-16 05:52-06:09 EST, oldpc tb1, the product path of section 4.1 with this branch's server.exe, which
reads the shadow]: 21,000 copies in 16 minutes of a Tiny game, `unread: 0 episodes, 0 skipped, 0 probes`: with a reading
server the gate never closed.

## 3. The ice tap copies only when something can read it [C, tests]

The ice tap copies once per load (and per `Ice_Reload`); the copy is the costly part (section 4). What "nothing reads"
means for it, and what the tap does:

- **No server at all.** Nothing to add: without the main section (the server creates it, the composite only opens it)
  `CamOnPresent` returns before it reads the camera, so `camOk` stays 0, no Present is a map frame, and the ice watch
  never asks for a copy [C, `shell_composite.cpp` CamOnPresent; test below]. This held before 0385 too (and for the
  shadow tap, whose copies also need a map frame).
- **A server without the ice reader** (`render_ice=0`), the reader gate (`ice_tap.cpp` `GateWhy`, evaluated when a
  settled object state wants a copy, then each map Present while it waits): the newest image is still published, the
  section names the same server as when it was offered (`hdr->serverPid`; 0 = the section names none, as on the product
  path), `ackGeneration` is below that image's generation, and `ice_unread_ms` (default 10000) have passed since it was
  published. The copy waits, logged once per object state (`ice_tap: copy of load L deferred (the reader gate): the
  server has not acked generation G in N ms ...`); the unread image stays published. The gate opens when that image is
  acked (a slow reader) or the section names another server (only where a section names one: the product's server is
  never replaced in a session, D118): `ice_tap: deferred copy of load L starts: the reader gate is open`. An image
  closed for any reason (a failure, the kill switch) ends its offer. `ice_unread_ms=0` turns the gate off. The control
  block gains nothing: a waiting tap shows the older READY image.

*Corrected 2026-11-16 06:15-06:45 (same ticket): the first build also waited while the section's magic was not
`VPSHELL_MAGIC`; on the product path that deferred every copy for good [L, oldpc job 20261116-050355-c4ab: "copy of load 3
deferred (the reader gate): the server section is not initialised"], because with the ring hand-over (`handover=ring`,
the shipped ini) server.exe never writes the section's texture block: magic and `serverPid` stay 0 (only
`srv_v3_sink.cpp` writes them) [C]. The second build waited for the section itself; that part was then removed, since
without a section no copy is ever asked for (above). The oldpc series of section 4.1 ran the second build (the removed
part never waits on the product path: the section exists before the map).*

Tests (`test_ice_tap.py`; the older tests set `ice_unread_ms=0` themselves, they offer images nobody reads): no main
section -> no copy, state NONE, no gate line for 4 s; a section without magic created -> READY, the reader takes it; a
server named 4242 (and one not named, pid 0) with `ice_unread_ms=1000`, generation 1 unread for 1.5 s, an Ice_Reload ->
deferred with the reason, generation 1 still READY and its section still there; opened by the reader's ack (4242 and
0), or by pid 5151 in the section -> generation 2 READY and read; `ice_unread_ms=0` never defers; before
`ice_unread_ms` has passed an unread image does not hold back the next copy.

## 4. The ice image built off the render thread (0384) [C; L below]

Before (`polar-ice.md` 9): in the Present whose `Map(DO_NOT_WAIT)` of both staging buffers succeeded, the render thread
checked every index against the VB (Tiny 290,355 indices, huge 1,283,322), re-read the object, built the image (Tiny
1,070,390 B, huge 4,765,732 B), hashed it with FNV-1a 64 byte by byte, created the section and wrote it through views:
one Present of 7 ms (Tiny) to 18-21 ms (huge) on the workstation (indications).

Now: that Present maps, re-reads the object (CHANGED as before), and starts a worker thread (below-normal priority) that
gets the mapped staging memory and the copy's snapshots; it checks the indices, builds, hashes and writes the section,
and touches no D3D object and no EXE memory. A later Present sees it ended (`WaitForSingleObject(.., 0)`), unmaps, and
publishes READY (or the worker's LAYOUT / TOO_BIG / SECTION failure, as before). A device release during the build
(`IceTap_Shutdown`) or the kill switch waits for the worker first, then unmaps (at most the build's time on the render
thread, once). `ice_worker=0` (or a failed `CreateThread`) builds in the map Present as before: the A/B's base arm.
The READY line now reads `... composite CPU X ms (most in one Present Y ms), image built in Z ms by the worker|on the
render thread`; `copyCpuMs` in the control block stays the render thread's total.

### 4.1 Before and after on oldpc [L, 2026-11-16, grade A by loads]

Job `20261116-061819-e54f` on oldpc (quiet, headed, instance tb1), 06:55-07:29 EST, driver `shell/evidence/0385/ab_0385.py`
(one game session per subprocess `pp_0385.py`: `tools/testbed/jobs/product_path.py` unchanged, patched to load the save
and reload it in the same EXE process with `shell/tests/tap_live.py reload`; every load makes one ice copy). Product
path: server.exe `0746e09c...` and proxy `f17a7b07...` built from this branch (`build_0385.sh`, 0385 changes neither),
x64 DLL `29F58E17...` of the pin layout `x64-04b1211b-pfdee656`, composite `8050631a...` (commit c1a35c0a; its gate
differs from the final one only in a part that never waits on the product path), D33 + Overlay, the shipped ini, VT asset
set of the host. Arms: the composite key `ice_worker`, set before each load: 0 (base: the image built on the render
thread, the cost before 0385) and 1 (worker), alternating per load, the first arm rotated per session. Saves:
`TB-TINY_0009V` (SHA-256 27420a41..., 1 chunk, 96,785 triangles, image 1,070,390 B), 3 sessions x 8 loads;
`TB-HUGE_0350` (c499d3c6..., 8 chunks, 427,774 triangles, image 4,765,732 B), 2 sessions x 6 loads. Statistics:
`process_0385.py`, every load in `shell/evidence/0385/results_e54f.json`.

| Map | Metric (ms) | base: n, mean, sd, median, min-max, 95 % CI | worker: the same | worker - base, 95 % CI (Welch), detectable |
|---|---|---|---|---|
| Tiny | most in one Present | 12, 4.525, 0.410, 4.445, 4.144-5.723, [4.265, 4.785] | 12, 1.465, 0.122, 1.416, 1.298-1.667, [1.387, 1.542] | -3.061 [-3.330, -2.792] (-68 %), 0.35 |
| Tiny | composite CPU (the copy's sum) | 12, 5.984, 0.387 | 12, 1.637, 0.130 | -4.347 [-4.601, -4.092] |
| huge | most in one Present | 6, 19.052, 0.212, 19.041, 18.771-19.414, [18.830, 19.274] | 6, 1.406, 0.081, 1.363, 1.341-1.512, [1.321, 1.491] | -17.646 [-17.872, -17.420] (-93 %), 0.26 |
| huge | composite CPU | 6, 20.418, 0.244 | 6, 1.834, 0.218 | -18.584 [-18.887, -18.282] |

Reloads only (load 1 left out) give the same within 0.1 ms. Why it moved: the build (index check, image, FNV-1a 64,
section) left the render thread; its own time is unchanged (Tiny 4.44 / 4.37 ms, huge 18.73 / 20.19 ms on the worker,
whose one 25.6 ms build shows the below-normal priority). What stays on the render thread, about 1.4 ms in both maps, is
the gather Present (two staging buffers created, two CopyResource, the object, chunk and record reads, the ini keys read
twice) [I: not split; it was in the base arm's sum too: Tiny 5.98 - 4.53 = 1.46 ms].

READY's wall time grows by one Present: the worker's image is published at the Present after it ends (3-4 Presents, base
2-3), and after a load the EXE's next Present comes after a frame of about 1.9 s (huge; `ft_max` 1,909 and 1,991 ms in the
stats lines of a base and a worker load) or about 200 ms (Tiny): huge 88 ms -> 1.97 s, Tiny 40 -> 250 ms. The server acked
both arms' images after that same long frame (huge, copy start to ack: base 1.98 s, worker 2.10 s), so no frame that the
base arm showed with ice shows without it [L, the composite logs of session 1].

Quiet proof: alone, no other job, foreign cores 0.132 (limit 0.10, so "undisturbed false", as the other headed jobs
there, 0.10-0.14), CPU mean 21.6 %, max 55.2 %, lowest available memory 6,563 MB, 944 samples. Session exits: Tiny 0,
0, 0; huge 5, 5 (LOUD lines, all `fow_models: LOUD: the server's mask is stale (its heartbeat is still)`, one per huge
reload: the model fog of 0339, not this ticket's code); every session stopped, restored and verified. The pictures (an
end shot per session, product path, DIAGNOSTIC) are in the Visual Checks feed under issue 0385, oldpc.

The first attempt, job `20261116-050355-c4ab` (05:52-06:09 EST), stopped at its first load: the first form of the ice gate
waited for the section's magic (section 3's correction); the same run gave the live shadow-gate numbers of section 2.

## 5. Suites [L, workstation]

- **Plain** (`shell\build.cmd`, C5_SHELL_BUILD `C:\c5-64bit-work\0385\shell-build`; readers `ice_tap_test.exe` and
  `shadow_reader_tool.exe` built from this branch, their sources unchanged): `test_composite_log.py` + `test_ice_tap.py` +
  `test_shadow_tap.py` **96 passed, 0 skipped**, 2026-11-16 05:04-05:33 EST; after the gate's final form
  `test_ice_tap.py` **53 passed**, 06:39-06:55. Command: `C:/c5-64bit-work/0385/suite.sh plain <files>` (pytest through
  `quietlock.py run`, with `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`: a user-site `dash` plugin otherwise stops pytest at import
  on this machine).
- **ASan** (`shell\build.cmd asan`, the final code): **94 of 96**, 06:58-07:31, 0 ASan reports; the two failures were test
  hosts that did not end within the 40 s wait while the machine ran four games (`test_unread_image_defers_the_next_copy[ack-0]`,
  `test_real_path_refuses_the_fake_exe`), both passed on a rerun (07:32-07:35; the first now waits 80 s). An earlier ASan
  run (05:35-06:06) had two of the same kind (`test_section_failure_is_named`; `test_slot_in_use_is_never_written`, whose
  slot can be claimed between the test's read of `newestSlot` and its write of `inUseSlot`: a race of the test, with
  `shadow_idle_ms=0` the producer runs the code before 0385), both passed on a rerun.

## 6. Not verified

- A device release while the worker runs (the wait in `IceTap_Shutdown`) live: only the code path; the stand-in's tests
  do not resize during a copy.
- The shadow gate's pause and resume live (a server without the shadow reader, or one that stalls 2 s): only the
  stand-in; live only that it stays open with a reading server.
- The ice gate's deferral live (a server with `render_ice=0`): only the stand-in.
- The log rename on the product path with two EXE processes on one folder: only the stand-in's held-open case.
- Where the remaining 1.4 ms of the gather Present goes (section 4.1).

## 7. The cut-mask tap (ticket 4553, epic 0431, 2026-11-16)

Implementer of 4553 (account B), branch `task-4553-cut-tap` (from `epic-0431-trees` 56a5823d). What the grid is, who owns
and stamps it and why the tap reads it: `trees.md` section 14 (written by lead-0431 from the ticket's log) and
`cities-and-landmarks.md` 2.7. Code: `shell/composite/cut_tap.{h,cpp}` (producer and the shared layout),
`server/src/render/srv_cut_tap.{h,cpp}` (the reader; joins server.exe in 4554), tests `shell/tests/test_cut_tap.py` on
`cut_tap_host.exe` and `server/tests/render/cut_tap/` (`cut_tap_test.exe`), evidence `render/evidence/4553/`.

### 7.1 Design [C]

- **What it copies**: the plot mask grid `0xBA392C` (the Terrain system's static object) whole: its header
  (+0x00..+0x2F), the directory (tilesX x tilesY entries of 8 bytes) and the pool (blocks x 256 bytes, free blocks
  included), plus the GridHeightmap's sample counts and the cell rule's origin (`[0xD90FC4]` +0x0C / +0x10 / +0xB4 /
  +0xB8). The server's `c5r::forest::DenseCut::setFromBlocks` takes the directory and the pool as the EXE holds them.
- **No event, a watch**: the EXE announces a stamp on its 4 MB queue (event 0x20); reading that would mean EXE code. The
  tap polls instead (`cut_poll_ms` 500) and publishes when the bytes (FNV-1a 64 over sizes, origin, directory, pool and
  the load) differ from the published image and stayed the same for `cut_settle_polls` (2) polls, or the load changed.
- **On a worker thread**, not the render thread: a huge grid's directory is 344 KB and its pool several hundred KB to
  MBs [I until 7.3]; the copy, checks and hash run on a below-normal thread started by the first `CutTap_OnPresent`; the
  render thread only stores the camera block's gridValid / gridSeq, the EXE base and the switch (test host: 0.002 ms
  per call). A below-normal worker can be starved for seconds on a loaded machine (once 5.6 s in a test on the
  workstation, 2026-11-16 15:50): images then come late, never wrong.
- **Torn reads**: the header is read before and after the copy (byte-equal or refused), every state 0/1, every pooled
  index below the blocks, the pooled tiles = the grid's in-use count (+0x20). A refused read is counted
  (`refusedReads`) and read again at the next poll; a plain log line the first time; LOUD only when the same reason
  repeats `cut_loud_polls` (4) polls in a row (a layout the tap does not know). Corrected 2026-11-17 (ticket 0500; was:
  the torn read under the same 4-poll rule, which went LOUD once during a huge load's growth): a torn read (header not
  byte-equal) has its own run, LOUD only after `cut_torn_loud_s` (120 s) of torn reads at every poll (trees.md 14.8).
- **Sections** (as the ice tap's): control block `Local\VPShellCutTap.<pid>` (504 bytes, seqlock, magic 'C5CT',
  version 1, state NONE / READY / FAILED / OFF / NO_GRID, failReason PATTERN / EXCEPTION / SECTION / TOO_BIG, counters, the
  server's ack), one image section per generation `Local\VPShellCut.<pid>.<generation>` (160-byte header, directory and
  pool 16-byte aligned); the newest and the one before it stay open.
- **Refusals**: 13 byte patterns (the call sites of the forest's and the shadows' lookups, the lookup's cell rule, the
  tile lookup, the size rounding, the cell write and pool, the pool allocator, the grid's creation in CreateTerrain and
  its release in the Terrain shutdown) checked before the first read; a mismatch or an EXE the composite did not
  validate: FAILED PATTERN for good. SEH on every read: FAILED EXCEPTION, off for the process, except a fault while
  copying the directory or the pool under a header that moved meanwhile, which is a torn read (corrected 2026-11-22,
  ticket 0690, K28; was: every fault FAILED EXCEPTION; section 7.6). No EXE function called, nothing written into the EXE.
- **The switch**: `cut_tap` in `[composite]`, default **0** in code until lead-0431 binds the reader (4554); 0 publishes
  OFF from the render thread and starts no worker. The server's reader is LOUD once on OFF and places the trees with an
  empty mask.
- **The reader** (`CutTapReader`, a `c5r::forest::CutSource`): copies the image out of the section, checks it (21
  refusal kinds: block, staleness, open, truncation, magic, version, hash, block/image agreement, sizes, offsets, the
  directory), fills a `DenseCut`, acks; a refused image keeps the last good grid (LOUD: the first 20, then every 100th);
  the generation moves on every applied or dropped grid; `installFailure()` = the producer FAILED (pattern, fault, or
  no image could be made) or the first image refused (the scene's exit 5, D118 / D125); OFF drops the grid; NO_GRID
  (between loads) keeps it.

### 7.2 Tests [C, tests]

- `shell/tests/test_cut_tap.py` (26 cases) on `cut_tap_host.exe`: every byte of the image and every field of the control
  block against a synthetic grid recomputed in Python; one image while nothing changes; a new generation for a changed
  cell (exactly one cell differs, by bit 0), for a new load, after the grid was freed and made again; the settle rule;
  negatives: 6 refused layouts (no image, LOUD after 4 polls, the first a plain line), a short streak not LOUD, the grid
  not made / no GridHeightmap (NO_GRID), a fault (FAILED EXCEPTION, 1 poll), the real path on an EXE without the
  patterns, an unvalidated EXE, 5 broken patterns, all patterns in an image, the switch off from the start and during the
  run, no map, an image above `cut_max_kb`; the 13 patterns against the installed EXE's file. 2026-11-16: plain 26 of 26
  (15:56; `shell\build.cmd`, C5_SHELL_BUILD `C:\c5-64bit-work\4553\shell-build`), MSVC `/fsanitize=address` 26 of 26
  (15:59, `shell\build.cmd asan`), 0 ASan reports, 0 skipped. An earlier plain run lost one refusal case to the
  worker's starvation under load (above); the refusal cases now run their host 4 s.
- `server/tests/render/cut_tap/test_cut_tap_reader.py` on `cut_tap_test.exe` (31 cases, a fake producer in the test's
  process): every cell through `bitsAt` against the synthetic grid, the ack, a new generation, 21 refusals with their
  ackResult and the last good grid kept, install failures (first image refused, FAILED pattern, FAILED too big), OFF,
  NO_GRID, a torn block, the file form. 2026-11-16: plain (clang-cl /W3 /WX) 15:49, 31 of 31; MSVC `/fsanitize=address`
  16:00, 31 of 31, 0 ASan reports (`build_cut_tap_tests.py --out C:/c5-64bit-work/4553/reader [--asan]`). One refusal
  kind, CREF_GRID (DenseCut refusing an image the reader's own checks passed), has no test: those checks cover every
  refusal of `setFromBlocks`, so it cannot be reached with a consistent image.
- The proof tool `render/evidence/4553/cut_proof.exe` (4550's proof mode with a `cut IMAGE` input, built against c5render
  by `build_proof.py`) run with an empty cut on 0420's job c78e Tiny census after the load gives 4550's numbers
  exactly (46 exact, 49 more of 95; trees.md 10.5), 2026-11-16 16:02.

### 7.3 Live [L]

oldpc, 2026-11-16, instance `tb1` (the host's EXE), product path of `product_path.py` (proxy, `server.exe` built from this
branch, x64 DLL `29f58e17` of the pin layout `x64-04b1211b-pfdee656`, composite `bf815589`), driver
`render/evidence/4553/cut_t4.py` on 0420's `forest_t0.py`; arms `ct` (D33 with Forest kept on, `forest_tap=1`,
`cut_tap=1`) and `cd` (the product's D33, `cut_tap=1`); saves `TB-TINY_0009V` and `TB-HUGE_0350`. Jobs:
`20261116-170924-f98e` (Tiny ct, cd; huge ct, cd; 20 s settle, camera tour without pictures, 2 AI turns; 17:09-17:28) and
`20261116-172903-79b4` (Tiny ct, cd, ct, cd; huge cd, ct; 60 s settle; no tour, no turns; 17:29-17:55, adopted by the
runner after 0426's restart of it, finished cleanly). The first submission `20261116-155441-7166` stopped in every arm on
a RecursionError of the driver (fixed), after the loads. Every arm: exit 0, 0 LOUD lines, stopped, restored, `provision
verify` rc 0; no crash box.

- **On the real EXE**: the 13 patterns match; the worker publishes from the load on; the watcher copied every generation
  (0 missed). Huge: 4,096 x 2,688 cells (256 x 168 tiles), samples 8,192 x 5,376, origin (4096.0, 2327.8081); Tiny:
  2,048 x 1,120 cells (128 x 70 tiles) [`cutimg_summary.out.txt`].
- **The grid grows for about 40 s after the load**: huge generation 1 (4,094 pooled tiles: bits 0 and 3 only, the river
  mesh's 0x09) at the load, then 7,9xx, 8,7xx and about 10,160 pooled tiles (generation 4, image 2.9 MB) as the landmark,
  farm and city stamps arrive; Tiny 1,059 -> 1,359-1,374 tiles (image about 0.42 MB). Each AI turn adds a few generations
  (routes, improvements: huge 4 -> 14-16 over 2 turns).
- **Torn reads** happen while the grid grows: 13 polls in job f98e failed the header-before-and-after check, each passed
  at a later poll; none reached the LOUD streak.
- **Cost of a poll on the worker** (read, checks, FNV-1a 64 over the copy; huge): 9-10 ms, at most 14.6 ms, every 500 ms on
  the below-normal thread, off the render thread [L, the READY lines; an indication, not a timing series]. A cheaper
  change test (comparing the directory first, hashing only on a difference) would cut it; not needed for the stand-in.
- **The grid is random per session in the bits of the randomised placers**: two sessions of the same arm differ in bits
  0x01, 0x02 and 0x10 (huge 281,438 and 286,968 cells; Tiny 6,963 and 7,084 cells, bit 0x02 only), bits 0x04 and 0x08
  never; Forest-on against D33 differs by the same amount and in the same bits (huge 282,198 and 285,732; Tiny 4,981 to
  7,839) [`control_diff.out.txt`]. So a recorded grid is not the session's: the server must read the grid of its own
  session, as this tap does.

### 7.4 What the live run settles

- The proof of `trees.md` 10.5 again with the tapped grid of the same session (`proof_4553.py`, `cut_proof.exe`): every
  built plot exact on both saves at every census (Tiny 94 / 166 / 166, huge 97 / 324 / 275 after the load / the tour / the
  2 turns); with an empty cut the same sets give 41-60 exact (4550's numbers) [L]. Details in `trees.md` section 14
  (lead-0431).

### 7.5 Not verified, open

- The reader inside server.exe and the scene's use of it (4554); the switch's default 1 (lead-0431's). The reader was
  tested against a fake producer only; live, nothing acked the tap's images (no reader in this server.exe).
- The tap's effect on the EXE's frame time or memory: not measured (a worker thread, about 3 MB of copies on huge).
- A device release, a second load in one EXE process and a return to the main menu live: only the tests' load change and
  the grid freed and made again.

### 7.6 A fault during the pool's growth is a torn read (ticket 0690, K28, 2026-11-22)

**The failure** [L, ticket 5030's oldpc job `20261122-222342-00f4`, run r03-p1, 2026-11-22 22:59]: on the third huge load
(TS0438-H-T300) of one job, a torn read at 22:59:49.554 and one poll later `cut_tap: LOUD: FAILED (2): a read of EXE memory
faulted (the pool at 0x6DFA0024 (3297280 bytes) is not readable)`. The pool's capacity doubles when it is full and its
data pointer moves (trees.md 14.2 [C]); the old block (3.3 MB: a heap block that large is its own virtual allocation, so
its release unmaps it [I]) was released between the tap's first header read and its pool copy. The tap took that for a
broken layout (FAILED EXCEPTION, off for the process); with `render_forest` 1 the server's forest scene ended the 64-bit
path at the load (exit 5, K28 in `../06-tooling-and-workflows/known-crash-causes.md`).

**The rule since 0690** [C, `shell/composite/cut_tap.cpp` `HeaderMovedAfterFault`]: a fault while copying the directory or
the pool reads the header again (SafeRead); while it still equals the first read, up to 3 more times 2 ms apart (an EXE
that releases the old block before it stores the new pointer, as a vector's growth does, leaves the header unchanged for a
moment [I: the order in the EXE is not traced]). A header that moved = a torn read (result 4: counted in `refusedReads`
and "(N torn)", read again at the next poll, LOUD only after `cut_torn_loud_s`, section 7.1); unchanged after the re-reads,
or a header that itself faults = FAILED EXCEPTION as before. Log: the run's plain line names the fault ("a torn read; the
pool at 0x... (N bytes) faulted"); each further fault-torn read a plain line `torn read N of this run after a fault (M in
this process)` (the first 20, then every 100th); the worker's last line adds "M torn reads after a fault (0690)". The
cost: nothing on a good poll; a fault under an unchanged header waits at most 3 x 2 ms (Sleep granularity: up to about
3 x 16 ms) before FAILED.

**Tests** [L, workstation 2026-11-22 23:40]: test mode `cut_test=torn` with `cut_test_torn_free=1|2|3` (the pool in its own
virtual allocation; 1: the "EXE" moves the pool and stores the new pointer before the copy, the old block released; 2: the
old block released first, the pointer stored at the tap's first re-check; 3: released, the header keeps the pointer). New
cases in `shell/tests/test_cut_tap.py`: 1 and 2 give 30 torn reads, no LOUD, then generation 1 byte-equal to the test grid
(refusedReads 30, 19 per-fault lines, "30 torn reads after a fault"); 3 gives FAILED (2) "the pool ... is not readable" at
poll 1. Suite 41 cases: 40 passed, `test_image_above_the_limit_is_refused` failed under load (the worker started 5.2 s
late) and passed alone with the new cases (4 of 4) (`shell/evidence/0690/`). Negative control
(`shell/evidence/0690/old_control.py`: the same file with the fix taken out): cases 1 and 2 FAIL with exactly K28's line
(`FAILED (2): a read of EXE memory faulted (the pool at 0x... (2048 bytes) is not readable)`), case 3 passes.

**Live** [L, workstation instance c5h-a, EXE build 4390913, 2026-11-22 23:56 to 2026-11-23 00:19 EST]: the product
layout of `lay.py` (set-proxy + `vp64helper on`, `pack=0`) with server.exe built from main f940726d9 (pin x64-0.1b
1b87dd185) and this branch's composite (`cut_tap=1` from the shipped ini, `render_forest=1`); a copy of TS0438-H-T300 (the
save of K28's run) loaded 3 times, each in a new EXE process (`shell/evidence/0690/live_0690.sh`), the load screen closed
after the first image, stopped about 45 s after the last cut-tap line. Each load: 0 FAILED, 0 LOUD; the grid grew to
14,895-14,906 pooled tiles (blocks = in-use: the pool grew with the load, 8,243 at generation 1 of load 1); torn reads
counted: refusedReads 35 / 17 / 8 in 145 / 122 / 101 polls (20 / 9 / 5 runs of torn reads), every one a header change
after the copy; **no fault came up in these 3 loads** (K28 came in 1 of 6 H loads on oldpc), so the fault branch is
proven by the tests only. The server's forest scene drew with the tap's reader each time (`render_forest=1 ... ready`,
`the cut tap's reader for EXE pid ...`); the EXE ended by the harness's stop (exit code 15) each time. Evidence
`shell/evidence/0690/live-c5h-a-2026-11-23.txt`.

**The missing popup** [C, L]: run r03-p1 had no vp64helper popup because the turn-study runner lays the development layout
(`provision.py set-proxy` + `vp64helper on`): its `vpproxy.ini` has `pack=0` (the proxy's log: `... counters=1 pack=0`),
and the proxy writes the D119 report and starts the helper's popup only with `pack=1` (`proxy/src/proxy_rt.cpp`, the
server-death path: `if (g_cfg.pack && ...) ... ReportFailure`), which the installer writes (`tools/installer/build_installer.py`:
`pack=1`). Not a product fault; the same finding as `../02-exe-internals/crashes-huge-load.md` (no report and no popup with
`pack=0`). Runs that must see the popup use the installer-laid layout or the release stages (`hi_journey.py`, `instance.py`:
`pack=1`).
