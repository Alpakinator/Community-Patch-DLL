# server.exe's private bytes across reloads in one EXE process (bug 0121)

Question (bug 0121): when the player loads a save again in the same EXE process, does the 64-bit server's private memory
grow per load, and if it steps up once, why. A growth per load would eat the address-space headroom the patch exists
for (the server is 64-bit, so the cost is the machine's commit, not the EXE's 4 GB; the EXE's own memory is measured
alongside).

## 1. Earlier measurements (history, each on its own build) [L]

| when, who | build | save | private after each load (MB) | shape |
|---|---|---|---|---|
| 2026-11-13 08:24, 4000-0121 run 1 | shell_server (i2-shell-test-bbfacca1), D33 + server terrain | huge t359 | 1,144.8 / 1,517.1 / 1,519.6 / 1,569.2 / 1,576.9 | steps over 2-4 reloads, then 0-28 MB |
| 2026-11-13 08:46, 4000-0121 run 2 | same | same | 1,180.3 / 1,358.9 / 1,339.2 / 1,513.4 / 1,525.4 / 1,553.7 / 1,564.4 / 1,588.1 / 1,586.1 | same |
| 2026-11-13, 3306 r2 | server.exe, old pin | huge t350 | +289, +4.6, -0.4 (steps) | one step |
| 2026-11-13, 3308 r1 | product path, old pin | huge t350 after 10 turns | +323.5, +30.1, -86.7, +14.4, -3.1 (steps) | one step |
| 2026-11-13 15:27, 3311 p1 | product path b1 of bd00d2b, D96 pins | huge t350 after 30 AI turns | 2,089.7 -> 2,435.9 / 2,499.3 / 2,410.5 / 2,414.8 / 2,411.3 | one step (+346), then flat |

Sources: `research/05-graphics-composition/terrain-surface-verification.md` section 4 and V2 (4000-0121, data
`research/05-graphics-composition/evidence/4000-0121/`); `research/05-graphics-composition/renderer-in-server-design.md`
4.11 (3311, with 3306 r2 and 3308 r1). The 4000-0121 reading then: the growth is in 4-64 MB private allocations,
"allocator or driver pools sized by the rebuild peak [I]", not in the field's own blocks; video memory flat.

## 2. Method of 0121 (2026-11-15) [C]

- **Build**: the pinned release layout `<pins_dir>/dlls/x64-04b1211b-pfdee656` (COMMON 18, `deps/dll.lock`
  `release`: DLL 29F58E17..., server.exe 5B254430..., proxy 75C40CEC...), on the product path ([Renderer] ServerRenderer = 1,
  the composite beside the proxy with its manifest, no server_config=), composite built by `shell/build.cmd` from
  fdee65668's sources (the layout's tools commit; `shell_composite.dll` BA87A75E..., the bytes differ from 0360's
  04B9815E... only by build path and stamps [I]); D33 by darkred.py with Overlay kept on (D93). The shipped first playable
  build (0324) runs with ServerRenderer = 0 by default: the render=0 control arm below is that configuration's server.
- **Instance**: c5h-e (Goldberg, `F:/c5-instances`), workstation, RTX 4080, window as the harness sets it.
- **Saves** (copies, removed at restore): `C5H-0121-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (SHA-256
  C499D3C6..., turn 350, 128x80) and `C5H-0121-TINY_0018` of `C5H-3000-TINY-T0018` (SHA-256 E337244C..., turn 18).
- **Driver**: `evidence/0121/run_0121.py` (3311's prep / restore; first load, then 6 reloads of the same save by
  `shell/tests/tap_live.py reload`, i.e. `Events.PlayerChoseToLoadGame` from the InGame Lua state; no AI turns). A
  reload is complete when the InGame state answers again without a marker set in it before the reload. After each load:
  samples at 40 s (a) and 80 s (b), server.exe private, working set and peak commit (psutil), the EXE's private bytes, the
  render.log memory line (video local), and an owner walk of server.exe (`perf/ledger/owners/owners.py`: every region by
  owner, each heap's committed bytes and free list). A thread samples server.exe's private bytes every second; the peak of
  a load is the maximum between the reload command and the (a) sample. Summary by `evidence/0121/summarize_0121.py`.
- Functional run on the shared workstation (other teams' games and builds ran at the same time): memory only, no timing
  claim (COMMON 23).

## 3. Results

### 3.1 Huge, product path, no AI turns, first load + 6 reloads (run huge-r2, 2026-11-15 21:36-21:58 EST) [L]

server.exe private bytes 80 s after each load (MB), the peak private bytes seen between the reload command and the
settled sample, the process's peak commit so far, the EXE's private bytes, video local (render.log):

| load | load s | server private | step | peak in the load | peak commit | EXE private | video local |
|---|---|---|---|---|---|---|---|
| 0 (first) | 172 | 1,599.7 | | 1,599.8 | 1,599.8 | 1,439.8 | 1,143.5 |
| 1 | 82 | 1,683.6 | +83.9 | 1,943.2 | 1,965.3 | 1,472.1 | 824.2 |
| 2 | 76 | 1,608.4 | -75.2 | 1,863.7 | 1,965.3 | 1,481.1 | 777.8 |
| 3 | 81 | 1,681.3 | +72.9 | 1,932.8 | 1,965.3 | 1,492.6 | 825.3 |
| 4 | 81 | 1,720.7 | +39.4 | 1,979.3 | 2,021.5 | 1,497.7 | 829.7 |
| 5 | 76 | 1,741.8 | +21.1 | 1,996.6 | 2,023.7 | 1,502.4 | 834.7 |
| 6 | 81 | 1,726.0 | -15.8 | 1,984.7 | 2,031.2 | 1,516.8 | 842.5 |

Each reload's rebuild peaks 260-280 MB above its settled value (old and new scene side by side for a moment [I]). The
Residency keeps its arrays across reloads of a same-size field (render.log: "same size: compared", 415.9 MB of arrays and
352.7 MB of caches at every load, 16-28 tiles changed and re-baked per reload): the scene's own GPU set does not grow.
Guard checks 0 / 0 / 0, start-up watch ok, harness stop 0, leftovers 0, 0 LOUD lines in render.log, server.log,
vpproxy.log and shell_composite.log.

**Owner walk of server.exe, committed MB against the first load** (`summarize_0121.py`, regions.csv; images left out):

| load | driver write-combined private (0x404) | process heap committed | process heap free list | other private RW |
|---|---|---|---|---|
| 1 | +2.9 | +35.3 | +41.8 | +44.9 |
| 2 | +1.4 | +16.5 | +43.0 | -9.9 |
| 3 | +50.5 | +39.4 | +42.7 | -9.1 |
| 4 | +104.8 | +19.8 | +40.6 | -4.5 |
| 5 | +104.5 | +42.3 | +40.5 | -5.8 |
| 6 | +104.6 | +20.7 | +45.7 | 0.0 |

(At the first load: driver write-combined 352 MB committed, process heap 444.5 MB committed with 2.0 MB on its free list,
other private RW 884 MB, most of it 32 MB read-write blocks.) Reading [L for the numbers, I for the owners]:
- **The process heap** (the CRT's heap, which the DLL and the server share) keeps about 42 MB of freed blocks on its free
  list from the first reload on (2.0 -> 41.8 MB, then 40.5-45.7 at every later load): the rebuild's peak leaves free
  blocks the heap does not give back. Its committed size moves +16 to +42 MB from load to load around that, without a
  trend.
- **The video driver's write-combined system memory** grows in 32 MB steps: +3 / +1 / +50 / +105 / +105 / +105 MB, flat
  from the fourth reload. It is the driver's upload memory (write-combined pages are what a D3D11 driver hands out for
  CPU-written GPU data [I]), sized by the most it was asked for at once; the rebuild re-uploads the scene.
- Everything else is flat to within 10 MB after the first reload.
- Net: +126 MB after six reloads, with the last three loads at 1,720.7 / 1,741.8 / 1,726.0 (within 22 MB): a plateau, not
  a growth per load.

**Allocations, not only totals** (`evidence/0121/alloc_diff.py`, committed bytes per allocation base). First load ->
reload 1: the field's two CPU blocks (84.00 and 42.00 MB, in the process heap's big-block list) are freed and allocated
anew at another address on every load (gone / new pairs of the same size at each reload): no field copy is kept. The
driver-shaped allocations change: five new 31.62 MB read-write blocks and three new 31.97 MB write-combined blocks appear,
while existing 32 MB blocks of both kinds are partly decommitted (for example 31.98 -> 1.73 MB) or filled (3.92 ->
32.00 MB). Reload 4 -> reload 6: **no allocation of 4 MB or more appeared or disappeared**; only the committed part of the
existing 32 MB reservations moved (+4.5 MB net in read-write, -0.2 MB write-combined). So from reload 4 on the server
reuses the address-space blocks the first reloads created; what moves is commit inside them.

### 3.2 Tiny, product path, no AI turns, first load + 10 reloads (run tiny-r1, 2026-11-15 21:58-22:19 EST) [L]

| load | load s | server private | step | peak in the load | peak commit | EXE private | video local |
|---|---|---|---|---|---|---|---|
| 0 (first) | 106 | 704.5 | | 704.5 | 704.5 | 1,268.5 | 555.2 |
| 1 | 17 | 784.4 | +79.9 | 784.4 | 786.5 | 1,279.3 | 379.9 |
| 2 | 17 | 787.3 | +2.9 | 788.1 | 810.8 | 1,283.0 | 376.5 |
| 3 | 16 | 787.8 | +0.5 | 787.8 | 810.8 | 1,287.8 | 381.8 |
| 4 | 16 | 787.7 | -0.1 | 787.8 | 810.8 | 1,289.6 | 390.5 |
| 5 | 16 | 788.5 | +0.8 | 790.2 | 814.9 | 1,289.6 | 354.4 |
| 6 | 19 | 788.5 | 0.0 | 788.5 | 814.9 | 1,292.8 | 375.3 |
| 7 | 18 | 789.3 | +0.8 | 790.4 | 815.7 | 1,292.5 | 389.7 |
| 8 | 16 | 789.3 | 0.0 | 789.3 | 815.7 | 1,294.4 | 376.8 |
| 9 | 17 | 789.0 | -0.3 | 800.9 | 815.7 | 1,294.2 | 403.6 |
| 10 | 18 | 789.9 | +0.9 | 790.9 | 816.2 | 1,294.9 | 378.9 |

One step of +79.9 MB at the first reload, then +5.5 MB over the nine reloads after it (+0.6 MB per reload; the EXE +15.6
MB over the same nine, 1.7 MB per reload). The Tiny rebuild is too short for the 1-s sampler to see much of a peak (the
process's peak commit rises 22-30 MB above the settled value). Guard 0 / 0 / 0, start-up watch ok, stop 0, leftovers 0, 0
LOUD lines in the five logs. Arrays 128.0 MB and caches 114.3 MB at every load (render.log).

Owner walk, committed MB against the first load: at reload 1 driver write-combined +27.1, process heap +15.2 (free list
0.4 -> 7.7 MB), other private RW +37.5; at reload 10 +28.2 / +19.4 / +37.5. The first reload's new allocations of 2 MB
or more: a 19.36 MB and an 18.02 MB read-write block (sampled pages all zero) and an 18.73 MB write-combined block, all
kept to the end; the field's two heap blocks (17.50 and 8.75 MB) are freed and allocated again at every load. Reload 1 ->
reload 10: no allocation of 2 MB or more appeared for good; the process heap +4.3 MB, the driver's write-combined +1.1 MB.

### 3.2b Huge, product path, 10 AI turns, then 6 reloads (run huge-t10, 2026-11-15 22:19-22:48 EST) [L]

The shape of 3308 r1 (10 turns) and 3311 p1 (30 turns), where the first reload stepped +323.5 and +346.2 MB. Server
private 80 s after the first load 1,524.0 MB; over the 10 AI turns (350 -> 359, every `end` and `wait` rc 0) 1,558.5 ->
1,585.1 MB. Then:

| load | load s | server private | step | peak in the load | peak commit | EXE private | video local |
|---|---|---|---|---|---|---|---|
| 0 (after 10 turns) | | 1,585.1 | | | 1,625.9 | 1,551.7 | 592.7 |
| 1 | 89 | 1,644.4 | +59.3 | 1,894.6 | 1,925.2 | 1,537.5 | 1,040.1 |
| 2 | 97 | 1,591.7 | -52.7 | 1,836.9 | 1,925.2 | 1,541.6 | 760.3 |
| 3 | 118 | 1,583.4 | -8.3 | 1,869.4 | 1,925.2 | 1,552.2 | 806.0 |
| 4 | 89 | 1,584.9 | +1.5 | 1,843.6 | 1,925.2 | 1,557.9 | 811.1 |
| 5 | 81 | 1,581.4 | -3.5 | 1,850.7 | 1,925.2 | 1,563.1 | 817.0 |
| 6 | 89 | 1,585.1 | +3.7 | 1,835.5 | 1,925.2 | 1,577.8 | 784.8 |

**Flat**: after reload 2 the server is back at its pre-reload size (1,581-1,592 MB against 1,585.1), the last five loads
within 10.3 MB; each rebuild peaks 250-310 MB above the settled value and gives it back. Owner walk against the
after-turns state: driver write-combined +3 to +4 MB, process heap +1.7 to +9.7, other private +45 at reload 1, then -4
to -13. The EXE grows 26 MB over the six reloads. Guard 0 / 0 / 0, start-up watch ok, stop 0, leftovers 0, 0 LOUD lines.
**The +289 to +346 MB first-reload step of 3306 r2, 3308 r1 and 3311 p1 (old pins, server builds of 2026-11-13) does not
appear on the current release layout** (one run each; the turn count differs from 3311's 30).

### 3.2c Huge, render=0 control (the shipped default's server), first load + 6 reloads (run ctl-huge, 2026-11-15 22:48-23:12 EST) [L]

The same layout and instance with `vpproxy.ini server_config=` naming a server.cfg with `render=0` (3311's control): no
render thread, no D3D11 device. Server private after each load: **219.1 / 224.2 / 224.4 / 225.0 / 227.0 / 227.4 / 228.0
MB** (+5.1 at the first reload, then +0.2 to +2.0, +8.9 MB over six reloads, about 0.6 MB per reload after the first);
the peak in a load never rises more than 1.2 MB above the settled value; working set = private (218-226 MB). Owner walk:
the only change is the process heap, committed +5.0 -> +8.6 MB, its free list 1.2 -> 4.2 -> 8.3 MB. The EXE 1,455.8 ->
1,516.7 MB over the seven loads (+10 MB per reload, the same as in the product runs). Guard 0 / 0 / 0, start-up watch ok,
stop 0, leftovers 0, 0 LOUD lines. So the DLL and the server's game side keep nothing per reload: **the first-reload step
of the product path belongs to the renderer's D3D11 side**.

### 3.3 What the server's private bytes are made of (first load, huge and Tiny) [L for the walk, I for the owner]

- The process heap (CRT heap of the server and the x64 DLL): 444.5 MB huge, 221 MB Tiny.
- The video driver's write-combined system memory (protection 0x404, `nvwgf2umx.dll`, driver 610.88, RTX 4080): 352 MB huge.
- Other committed read-write private memory without a heap header: 884 MB huge, mostly in blocks of 31.6-32.0 MB, and
  317 MB Tiny in 18 blocks (32, 19.4, 18.0, 15, 11.0 x 4, 8, 5.4 x 4 MB). Their sampled pages are all zero
  (`evidence/0121/peek_allocs.py`): committed and never written. The working set confirms it: server.exe's working set is
  619 MB against 1,600 MB private on huge, 323 against 705 MB on Tiny. Four blocks of exactly 11.00 MB match one Residency
  array chunk (256 slices of BC1 at 256 with mips, `render/include/c5r/bake/residency.h`, "one chunk = 11 MB at 256"): the
  blocks are the D3D11 driver's system-memory reservation for the GPU resources [I: the driver allocates them; no caller
  stack was taken]. They cost commit charge, not RAM.

So most of server.exe's private bytes on the product path are the D3D11 driver's, and the step at the first reload is
mostly the driver's pools and the heap's free list growing to the rebuild's high-water mark (old scene resources released
but not yet destroyed while the new ones are created: D3D11 destroys a released resource later, when the GPU is done with
it [I]), then reused.

## 4. Answer to bug 0121 (2026-11-15) [L for the numbers, I where marked]

- **Flat after the first reload, on the pinned release layout `x64-04b1211b-pfdee656`.** Huge: +84 MB at the first
  reload, then a plateau (the last three loads within 22 MB); huge after 10 AI turns: +59 MB, then back at the pre-reload
  size; Tiny: +80 MB, then +0.6 MB per reload over nine more; the render=0 control: +5 MB, then about +0.6 MB per reload.
  No allocation is kept per reload: from reload 4 on (huge) and reload 2 on (Tiny) no allocation of 2-4 MB or more appears
  or disappears, only commit inside the existing blocks moves.
- **Why the first reload rises**: the rebuild of the renderer's scene (the field's CPU blocks freed and allocated again, the
  GPU resources released and created again) lifts the D3D11 driver's system-memory pools (write-combined blocks and
  committed, never-written read-write blocks of up to 32 MB in `nvwgf2umx.dll`'s pattern) and the process heap's free
  list (10-45 MB of freed blocks) to the rebuild's high-water mark; both are reused by every later rebuild [I: the
  owner of the driver-shaped blocks is inferred from their protection, their all-zero content and their sizes; no
  allocation stack was taken]. The game side keeps nothing (render=0 control).
- **The earlier +289 to +432 MB step does not reproduce** (9038 one sample in shell_server; 4000-0121 runs 1-2 in
  shell_server; 3306 r2, 3308 r1, 3311 p1 on the server builds of 2026-11-13). Those builds are history; the old
  statements in `05-graphics-composition/terrain-surface-verification.md` section 4 and V2 and
  `05-graphics-composition/renderer-in-server-design.md` 4.11 carry a dated pointer here.
- **The rebuild peak**: 250-310 MB above the settled value on huge, for the seconds of the rebuild; the process's peak
  commit stayed at or below 2,031 MB in every run.
- **The EXE** grows 6-10 MB per reload in every arm, product and control alike (huge 1,439.8 -> 1,516.8 over six reloads;
  Tiny 1,268.5 -> 1,294.9 over ten): not the server's, not measured further here.
- No code change (MC answer #2081 on the ticket thread, 2026-11-15: no HeapCompact trial). Not measured: server.exe built
  from main (the renderer of epic 0169 with decals, fog, water and overlays beyond the release layout's); 30 AI turns
  before the reloads (3311's count); a second run of each arm; another GPU or driver.

## 5. Data and tools

- Driver and summaries: `evidence/0121/run_0121.py` (prep / run / restore; `--turns`, `--control`), `summarize_0121.py`
  (per-load table and committed bytes per owner item against the first load), `alloc_diff.py` (allocations that appeared,
  disappeared or resized between two owner walks), `peek_allocs.py` (read-only content sample of large private
  allocations); `evidence/0121/summary-<run>.json` (each run's numbers).
- Raw data on `F:/c5-64bit-work/0121/` (D141): `w-product/out-huge-r2`, `out-tiny-r1`, `out-huge-t10`,
  `w-control/out-ctl-huge` (run.json, 1-s sampler CSV, render / server / proxy / composite logs, an owner walk per load
  under `owners/L<n>-b`), and `out-huge-r1` (the first try, aborted at reload 1 by a marker bug of the driver: the Lua
  channel's chunk environment has no `_G`; its first-load numbers are 1,716.0 MB server, 1,450.0 MB EXE).
