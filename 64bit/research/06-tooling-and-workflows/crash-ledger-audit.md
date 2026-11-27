# Crash and fatal-box ledger audit against D185 (ticket 0534, 2026-11-18)

Decision D185 (the owner, 2026-11-18, COMMON rule 30): a crash seen with the 64-bit patch is never closed on a rate, "not
reproduced" or a retry; its root cause is determined and written into the knowledge base with its confidence. This file
checks every signature the harness has recorded against that rule: where its root cause is written, and which ones have
none. Read only: no game instance was run, no ticket opened, no other file changed. Times EST. Addresses are DX11 VAs at
ImageBase 0x400000; a VP crash box's "Location (in file)" is a file offset (VA = offset + 0xC00 + 0x400000).

**Sources read (2026-11-18 06:35-07:10)** [C, files]:
- The dialog watcher's signature ledger on the workstation (`dialogwatch.ledger_dir()` = the pool's
  `dialog_signatures` folder, `%USERPROFILE%/c5-instances/_pool/dialog_signatures`): **15 signatures**, first sighting
  2026-11-16 02:47, last 2026-11-18 03:57.
- The queue's dialog-watcher entries (author `dialogwatch`, read from the store opened read only): **45 entries**, one
  per sighting; every one maps to a ledger signature. Each names the instance, the game pid, the issue and the evidence
  folder (`<instance>/run/crash/<time>_pid<pid>`), which were read for the crash details (VP crash box text,
  `crashes.log`, minidump present or not, the DLL's `instruments.log` line that says whether the proxy ran).
- The Goldberg crash printer's records on the workstation's instances (`<instance>/run/gbe/crashes.txt`, 22 records
  2026-11-10..18): they cover crashes from before the ledger counted boxless crashes (2026-11-17 01:16).
- The knowledge base: `../02-exe-internals/crashes-huge-load.md`, `../02-exe-internals/farm-field-double-free.md`,
  `../02-exe-internals/allocations-and-memory-map.md`, `../02-exe-internals/disable-systems-and-masks.md`,
  `../03-dll-engine-boundary/` (`upstream-fix-batch-2.md`, `upstream-fix-batch-4.md`, `upstream-13406-product.md`),
  `scene-catalogue.md` 12, `installer-notes.md` 14, `human-input-testing.md`, `../05-graphics-composition/fog-of-war.md`
  7.6, `../08-performance/marathon-stress-t2000.md`, `../08-performance/metrics-ledger.md`, and the issues' logs.
- One read-only debugger read of an existing dump (section 3): 9074's T1100 hang dump, `cdb -z`, no process touched.

Not read: the ledgers of the test machines (oldpc, laptop): each machine keeps its own pool folder and their watchers
cannot post to the queue (`dialogwatch.post`), so their sightings are in neither source (section 4.3).

Scripts and raw output (scrubbed of local user paths): `tools/harness/evidence/0534/` (`scan_ledger.py` ->
`scan-raw.json`, `scan-summary.txt`; `crash_context.py` -> `crash-context.txt`; `box_details.py` -> `box-details.txt`;
`gbe_crashes.py` -> `gbe-crashes.txt`; `t1100_hang_graph.sh` -> `t1100-hang-graph.txt`; `show_entries.py` reads queue
entries by id).

## 1. Result in short

- *Added 2026-11-18 (ticket 0541, section 8.1): a fourth class without a root cause, on the product route: queue
  ticket 0153 run 2's EXE fault at absolute 0x7C3DA2EA in the mod deactivation after a game with (4a) Squads for VP
  (2026-11-13 22:41:14 EST, c5h-c); `../03-dll-engine-boundary/proxy-design.md` 20 and 23.5 already list it as open.
  Bug proposed: T6 in section 5.*
- **3 crash classes have no determined root cause**, 1 of them seen on the product route: the EXE's write fault at VA
  0x55D91A-0x55D9E2 under the B event manager's dispatcher (product 1, stock 4) together with the read at 0x7A3F0A
  (stock 2); the product-route crash at VA 0x8E81C3 of 2026-11-16 08:37 (before the ledger); and how the farm-field
  graph's node list becomes a cycle in 9074's late-game loads (stock: the T1100 hang, 0x5B1FB7 x2, 0x77C6DA x1).
- **Section 3 is new [C, dump]**: the T1100 hang walks the **farm-field layout graph's node array** (the object of
  `crashes-huge-load.md` 2), and in that state the endpoint free list shows **no double-free footprint**. So 9074's
  stock failures and the product's huge-load crashes are in one EXE structure, which epic 0528's server layout retires,
  but the 9074 state is not (or not yet shown to be) 0515's double free.
- **All other sightings have a written cause**: the `-ia` DLL's exit crash (12 sightings + 1 named), the deliberate
  reproductions (UI draw list, #13482, the DLL's improvement precondition), the forced faults of the patch's error
  route (7 popups), the stock-only memory and map-size limits, and the D33 config.ini crash class.
- **The ledger cannot tell causes apart**: 4 of its 15 signatures each hold sightings of different causes (the VP
  "Game Crash" box: 14 sightings at 9 different sites; the boxless crash without a proxy record: 12 `-ia` exits and one
  unrelated start-up crash; two patch-popup signatures). Its `bug` field is set on 1 of 15. A harness ticket is
  proposed (section 5, T4).
- **Corrections owed in four other files** (outside this ticket's touches; for the MC, section 6).

## 2. The table

One row per ledger signature (id = the ledger file's name); a signature whose sightings have different causes is split
into one row per cause (the sub-rows carry the same id with a letter). Rows without a written root cause first. Arm:
**product** = the 64-bit path ran (the proxy's Lua channel in `instruments.log`, or a proxy run folder); **stock** = the
32-bit DLL in the stock EXE, no proxy (stock-only rows are marked); **deliberate** = a test provoked it on purpose.

| # | Id | Kind | Signature / site | First - last seen | Count | Arm | Root cause written (file, section) | Conf. |
|---|---|---|---|---|---|---|---|---|
| 1 | `07a62bf44a75` | crash | EXE AV write at **VA 0x55D91A** (RVA 0x15D91A), main thread, no box | 2026-11-17 11:10 | 1 (c5h-f, 0491 run b1, 9074's T1100 save, first AI turn) | **product** (dev layout) | **none**: `fog-of-war.md` 7.6 names only the site and "the known late-game EXE failure class of these saves" | C site; no cause |
| 2 | `5e071aaea5b2`-b | fatal (VP "Game Crash") | EXE AV write at **VA 0x55D9E2** (x2), **0x55D947**, **0x55D9AE**: the B event manager's dispatcher (stack RVAs 0x2E282E, 0x3490D9, 0x466919, 0x23ABD4) | 2026-11-16 21:27 - 2026-11-17 11:47 | 4 (c5h-i: #13397 fix run 1, T1150, T1300, T1600) | stock-only, 43-civ `-ia` DLL | **none**: `marathon-stress-t2000.md` 4.1-4.2 name the sites, rule out the 4 MB queue and allocation failure | C site; no cause |
| 3 | `5e071aaea5b2`-c | fatal | EXE AV read at **VA 0x7A3F0A** under the 4 MB queue's / B manager's dispatcher | 2026-11-16 21:55 - 2026-11-17 10:13 | 2 (c5h-i: #13397 fix run 2, T1550) | stock-only | **none** (same file, same sections) | C site; no cause |
| 4 | `5e071aaea5b2`-d | fatal | EXE AV read at **VA 0x5B1FB7** (x2) and **0x77C6DA** (x1): the farm-field graph's node walk; plus the T1100 hang in the same loop | 2026-11-16 19:44 - 2026-11-17 07:05 | 3 (c5h-i: T1100 run 3, #13397 fix run 3, T1450) | stock-only | **partly**: structure [C, dump] in section 3 here; how the cycle forms is open; `marathon-stress-t2000.md` 4.1 called the list "[I, what the list is]" | C structure; cause open |
| 5 | (not in the ledger) | crash, no box | EXE AV at **VA 0x8E81C3** (Goldberg `crashes.txt` 0x012881C3, EXE base 0x00DA0000 from `vpproxy.log`) during a load | 2026-11-16 08:36:53 | 1 (c5h-c, 0392 task 4510, second launch) | **product** | **none**: `human-input-testing.md` 12.5 ("silent EXE death during a load") records it, no cause; the site is new (section 4.1) | none |
| 6 | `4c7da30500db` | crash | EXE AV read at **VA 0x77C6DA** (weld loop of 0x77C1C0), proxy boot thread, no box | 2026-11-18 01:18 | 1 (c5h-b, 0517 hi_journey release-large, Large scene `HI-4506-LARGE-SCENE`, in the load) | **product** | `../02-exe-internals/crashes-huge-load.md` 2 and 8; `farm-field-double-free.md`; open until epic 0528 (D186). This sighting is only in `../03-dll-engine-boundary/upstream-fix-batch-4.md` (hi_journey first pass): a **Large** map, which the rate tables (huge loads only) do not count | I (double free; 0526 saw no footprint in 77 loads) |
| 7 | `d392eb4ffeca`-a | crash, no box | EXE AV, no proxy record, at start-up (Goldberg 0x010F9612) | 2026-11-17 01:16 | 1 (c5h-mpk, 2604 modpack form) | product install, 64-bit path never started | `installer-notes.md` 14.9 item 4: the EXE loaded the x64 `VP64\CvGameCore_Expansion2.dll` from the DLC folder (`GameCore.log`: LoadLibrary error 193); fixed by 2606's rename (14.12) | L trigger; I the EXE's file choice |
| 8 | `d392eb4ffeca`-b | crash, no box | EXE AV at the exit of a stock game with the `-ia` DLL (after `UI.ExitGame()` or the journey's own EXIT/Yes) | 2026-11-17 03:09 - 2026-11-18 03:57 | 12 (pids 47460, 70296, 49680, 39336, 24156, 54748, 67516, 72604, 51380, 9228, 37936, 53896) | stock route, test DLL only | `scene-catalogue.md` 12 and `../03-dll-engine-boundary/headless-host.md` 5 (0483, 0484): MemoryImports' allocator slots point into the unloaded DLL | C (cdb dump), L |
| 9 | `cfc8ac3165a7` | crash | the same exit crash, named (bug `0483`) | 2026-11-17 04:42 | 1 | stock route | as row 8 | C, L |
| 10 | `1676d60fbd1a`, `687149c46513` | crash | EXE AV at **VA 0x9082C2** and **0x90F32A**, main thread | 2026-11-17 15:46, 16:08 | 1 each (c5h-a, 0487 off arms) | product, **deliberate** | `../03-dll-engine-boundary/upstream-13406-product.md` 9.3: the stock EXE's 1 MB UI draw list overruns at 433-454 unit flags in view; 0487's growth fixes it on the product route | L; I which buffer overran first |
| 11 | `5e071aaea5b2`-a | fatal | **Stack Overflow** at file offset 0x525d37 (VA 0x926937) in the load | 2026-11-16 02:47 - 03:24 | 3 (c5h-i x2, c5h-h) | stock-only (9073 map-size calibration) | `../02-exe-internals/allocations-and-memory-map.md` 6.1: maps above about 20,000-21,800 plots overflow a thread stack | L limit; I mechanism (`_chkstk`) |
| 12 | `5e071aaea5b2`-e | fatal | EXE AV write to 0xB421E000 at file offset 0x1252c8 (VA 0x525EC8) | 2026-11-16 11:41 | 1 (c5h-h, 9073 stock T200 run, turn 225) | stock-only | `../08-performance/metrics-ledger.md` (9073 stock row): out of address space (largest free 1.15 MB, 38 MB free) | L; I mechanism |
| 13 | `ef93300efeb8` | fatal ("Render Error") | `FGXRenderer11::Present failed hr=DXGI_ERROR_DEVICE_REMOVED` | 2026-11-16 12:21 - 13:34 | 2 (c5h-h, 9073 stock turns 202 and 209) | stock-only | `../08-performance/metrics-ledger.md` (9073): address-space pressure, GPU load a possible contributor | I |
| 14 | `5e071aaea5b2`-f | fatal | EXE AV write to 0xC at file offset 0x4cc892 (**VA 0x8CD492**) in the load | 2026-11-16 05:15 | 1 (c5h-f, 0392 task 4505, stock arm) | stock route with D33's config.ini and `mode=off` | the River/Arrows render on a NULL batch when River or Arrows is off in `[DisableSystems]` with its view bit still set: `../02-exe-internals/world-systems-and-stubs.md` (crash, VA 0x8CD492), `disable-systems-and-masks.md` 2, D116 (3516). `human-input-testing.md` 7.6 still calls it unknown, "[I] address-space exhaustion" (correction owed, section 6) | C site and class; L for this sighting by its configuration |
| 15 | `737de4f02d23` | fatal (DLL "Error") | `CvGlobals.cpp:4244 eImprovementNum > -1` | 2026-11-16 08:53 - 09:45 | 3 (c5h-c 0392/4510 finding; c5h-d 0423 probe; c5h-b 0421 negative control) | stock route (2 deliberate) | `../03-dll-engine-boundary/upstream-fix-batch-2.md` 1.1: EUI without VP's compatibility mod calls `Plot:IsResourceConnectedByImprovement(-1)`; PRECONDITION; guarded by 0421, in pin batch 3 | C |
| 16 | `092dca94646e` | patch popup | "The game stopped on an internal check in the game's DLL" (`dll-fatal: eImprovementNum > -1 at CvGlobals.cpp:4279`) | 2026-11-17 05:02 | 1 (c5h-a, 0475) | product, **deliberate** (0422's probe) | as row 15; the route: `run-and-drive-the-game.md` 7.3.1 | C |
| 17 | `d919bedd3779` | fatal (VP "Game Crash", DLL) | DLL AV read of 0x9D0 at `CvGameCore_Expansion2.dll+0x33F779` | 2026-11-16 23:25 | 1 (c5h-b, 9075) | stock-only, **deliberate** | `../03-dll-engine-boundary/upstream-fix-batch-2.md` 2.6a: upstream #13482's bug (`CvCity::ChangeImprovementCount` through a missing owning city), reproduced on the pin; the fix is in pin batch 3 | C, L |
| 18 | `3592d6cd16cc` | patch popup | "The 64-bit part of the patch could not run" | 2026-11-17 03:04, 16:30 | 2 | product layout, **deliberate** | 03:04 (c5h-f, 0473): pack=1 with no `manifest.json` (`no-pack`), `run-and-drive-the-game.md` "Live run" of 0473; 16:30 (c5h-a, 0487): `VP64_UIDRAW_TEST_FAIL`, `upstream-13406-product.md` 9.6 | L |
| 19 | `1546aa563014` | patch popup | "The 64-bit part of the patch failed ... config.ini was left as it is" | 2026-11-17 11:36, 11:45 | 2 (c5h-f, 0491) | product, **deliberate** | `../05-graphics-composition/fog-of-war.md` 7.6: fault1 (a layout error of the driver: `file-missing`), fault2 (`render_overlay=0`, server exit 5) | L |
| 20 | `62fbaca452d6` | patch popup | "The 64-bit part of the patch failed ... config.ini is back" (`server-death: PeerDied`) | 2026-11-17 17:02 | 1 (c5h-b, 0342) | product, **deliberate** | 0342: `fogcloudtile_dif_01.dds` removed on purpose; `fog-of-war.md` 7 and 0342's log | L |
| 21 | `41ddd6a00c0f` | patch popup | "The game's 64-bit server stopped with this error: fog of war: ... fogcloudtile_dif_01.dds" | 2026-11-18 00:26 | 1 (c5h-b, 0511) | product, **deliberate** | 0511's live repeat of 0342 (its log, 00:24-00:27) | L |

Counts: 45 sightings; 12 on the product route or a product layout (9 of them deliberate faults or reproductions), 33 on
the stock route (all of rows 2-4 and 11-13 from the 9073/9074 stress series on the 43-civ `-ia` DLL).

(Row 5 updated 2026-11-18 by the MC: ticket 0539 read the site, the getter 0x8E81B0 through the Granny timed-trigger path, `../02-exe-internals/crash-load-8e81c3.md`; root cause NOT determined, leading candidate [I, low] a trigger reaching a destroyed or reused model context; open under D185 with tickets 0543 (a dump while WER holds the process) and 0544 (the writers of +0x88).)
(0543, 2026-11-18: `crash.collect` dumps a WER-held game read-only (`cdb -pv`) when it runs during the hold. The dialog watcher runs it during a hold with no box, as row 5's was. Not yet seen on a real crash. See `../02-exe-internals/crash-load-8e81c3.md` 6.1.)
(2026-11-18, 0544: candidate turned into a mechanism, medium confidence: a unit model destroyed by 0x734DE0 while a queued trigger copy still points at it; `crash-load-8e81c3.md` 9; root cause still open until 0543's dump names the thread and the object.)

## 3. 9074's T1100 hang is in the farm-field layout graph [C, dump, 2026-11-18]

(Updated 2026-11-18 by the MC: ticket 0538 found the cause [C]: the graph's 16-bit cell id overflows on maps of 16,384 plots or more (`../02-exe-internals/farm-field-double-free.md` section 2); rows 1-3 (ticket 0537, `crashes-huge-load.md` 9) come from the same oversize saves; 0x77C6DA of T1100 run 3 stays open.)

`tools/harness/evidence/0534/t1100_hang_graph.sh` read 9074's full dump of the hung stock EXE
(`F:/c5-64bit-work/9074/stock/t1100-run1/hang/x86-59172-20261116-181917.dmp`, EXE loaded at 0x008F0000, so the graph
object 0xBA2228 of `crashes-huge-load.md` 2 is at 0x01092228) with `cdb -z`; output `t1100-hang-graph.txt`.

- **The loop's registers name the graph**: thread 0 at VA 0x5B2024 (inside the loop 0x5B1F80-0x5B202A) has
  `ebx = 0x010922E4` = graph + 0xBC, the **node free-list header**, and `edi` = that header's data pointer
  (`edi - [0x010922EC] = 0`). So the "index-linked list of 56-byte records" of `marathon-stress-t2000.md` 4.1 is the
  farm-field graph's node array (56-byte nodes, links at +0x28 / +0x2C, as `crashes-huge-load.md` 2 describes), and the
  loop's caller at RVA 0x37C254 is the weld function 0x77C1C0 [C].
- **Both free lists are sound in that state**: node header head 0x0FFFFFFF, live 0xD983 (55,683) = size, capacity
  0x10000; endpoint header (+0x94) head 0x0FFFFFFF, live 0x91D0 (37,328) = size, capacity 0x10000. After 0515's double
  free the endpoint head never returns to 0x0FFFFFFF (`crashes-huge-load.md` 8.3), so **the double free had not
  happened in this session** [C for the state; I that no other path restores the head].
- **The cycle**: the four nodes the loop cycles through (0xD8DE, 0xD904, 0xD96D, 0xD973) each hold +0x28 = 0x0FFFFFFF
  and a +0x2C link to another of them or a neighbour (0xD904 -> 0xD8DE -> 0xD973 -> 0xD971; 0xD96D -> 0xD904), a grid
  cell id of 0xFFFF (+0x34, none), kind byte 1 or 2 (+0x36), the +0x10 word 2 or 3, and +0x00 pointing at the graph's
  third free-list array (graph + 0xA8). So the walk follows +0x2C links that never return to the start.
- **For the open question** [I]: both arrays sit at capacity 0x10000 with ids held in 16 bits (endpoint ids at
  +0x14 / +0x16, the grid cell id at +0x34); a late-game marathon map has about 4 times the huge save's 13,950 nodes.
  Whether a 16-bit field wraps, or another delete path than 0515's breaks the links, is the first thing to read.

What it means: 9074's stock failures (rows 2-4) and the product's farm-field crashes (row 6 and
`crashes-huge-load.md` 1, 7, 8) are failures of one EXE structure. 0528's server layout (D186), which switches the
EXE's farm-field layout off, would remove both if row 4's cycle and rows 2-3's dispatcher writes come from this graph;
rows 2-3 are not yet tied to it.

## 4. Crashes outside the ledger

### 4.1 The product-route crash at VA 0x8E81C3 (row 5)

2026-11-16, c5h-c, 0392 task 4510's second launch (`human-input-testing.md` 12.5; evidence
`F:/c5-64bit-work/0392/4510/crash-083739/`: server.log, vpproxy.log, proxy-calls.tsv, server-report.txt). Goldberg's
record `[2026/11/16 - 13:36:53]` (UTC) `code 0xc0000005 @address = 0x012881C3`; `vpproxy.log` gives the EXE base
0x00DA0000 (`shell: white mask: checks passed (EXE base 0x00da0000 ...)`), so **VA 0x8E81C3** [C, arithmetic]. The
server saw the peer hung at 82.3 s (no heartbeat for 5,027 ms; 08:36:59 by its start) and the EXE exit 0xC0000005 at
122.6 s (08:37:39), so the process stayed alive about 46 s after the fault [L]. No address in the knowledge base is
within that function; no dump was taken. The fault came before 0419's chained filter, so no `exe-crash.txt`.

### 4.2 Goldberg crash printer records (`tools/harness/evidence/0534/gbe-crashes.txt`)

22 records on 7 instances. The EXE is loaded at a 64 KB boundary, so the low 16 bits of an absolute address are the
VA's low 16 bits; matched against the knowledge base by those bits and the date [I unless the run's base is known]:

| Records | Attributed to |
|---|---|
| c5h-a 2026-11-14 05:09, 05:19 UTC; c5h-b 2026-11-14 06:19 UTC (`...D492`) | VA 0x8CD492, the D33 config.ini class (row 14; 3516) [I] |
| c5h-b 2026-11-13 13:50, 15:45, 16:03 UTC (`...4B03`) | VA 0x574B03, 0127's Overlay-off crash (`disable-systems-and-masks.md`, bases from the composite's log) [C] |
| c5h-a 2026-11-10 23:06 UTC (0x00A07F3B) | `proxy-design.md` (GameplayUnitCreated, EIP in data) [C] |
| c5h-a 2026-11-11 06:52 UTC (0x70DE4651) | MSVCR90 at the commit limit, bug 0089, `proxy-design.md` [C] |
| c5h-e 2026-11-15 05:56 UTC (`...3CF7`) | VA 0x9A3CF7, the Unit-alone crash (`../05-graphics-composition/unit-state-ii.md`) [C] |
| c5h-a 2026-11-17 20:46, 21:08 UTC; c5h-b 2026-11-18 06:18 UTC; c5h-f 2026-11-17 16:10 UTC; c5h-g 2026-11-16 10:20 UTC; c5h-mpk 2026-11-17 06:16 UTC | rows 10, 6, 1, `crashes-huge-load.md` 1 (run5nd), row 7 [C] |
| c5h-c 2026-11-16 13:36 UTC (0x012881C3) | row 5: **no cause** |
| c5h-a 2026-11-10 19:44 UTC (0x00560A98) | epic 2500's first real-EXE boot: the EXE crashed 1.6 s after boot (the context Destroyed); `../03-dll-engine-boundary/proxy-design.md` 15.2 item 1, 15.7; fixed in 2500 (section 8.1) |
| c5h-c 2026-11-14 03:41 UTC (0x7C3DA2EA, a DLL's range) | queue ticket 0153 run 2 (Squads, product route): the EXE died in the mod deactivation; **cause open**, bug proposed (section 8.1, T6) |
| c5h-a 2026-11-10 20:10, c5h-c 2026-11-11 21:30, c5h-b 2026-11-15 01:13, c5h-c 2026-11-15 13:49 UTC (0xE06D7363 at `KERNELBASE+...BAE4`) | the CRT abort box after an unhandled C++ exception, each with a written and fixed cause: epic 2500's unsized save stream (15.2 item 3), 0102's out-of-address-space `bad_alloc` (9036, 9041), 0175's header without the mod list, 4254's cross-variant load (0359) (section 8.1) |

*Corrected 2026-11-18 (ticket 0541): the four records were "not attributed" and whether the patch ran in them was not
determined. All six records that were open here are product-route runs (the harness saw `server.exe` start as the
EXE's child in each); five have a written and fixed cause, one (0x7C3DA2EA) is open. Section 8.1 has the table.*

### 4.3 The test machines

oldpc's and the laptop's watchers keep their own ledgers and cannot reach the queue. Product-route crashes known from
the knowledge base there: oldpc 2026-11-17 18:12, VA 0x77C70D (`crashes-huge-load.md` 7) and laptop 2026-11-17 20:31,
VA 0x536580 (8.1), both the farm-field class [I, row 6's cause]; laptop 2026-11-14, 3516's two load crashes (the D33
config.ini class [C], D116). Their ledgers were not read here. *2026-11-18: oldpc's read by ticket 0541 (section 8.2);
the laptop's by ticket 0546 (section 8.3): every sighting has a written cause.*

## 5. Proposed bug tickets (for the MC; none opened here)

**T1 (rows 1-3): "EXE write fault at VA 0x55D91A-0x55D9E2 under the B event manager's dispatcher, and the read at
0x7A3F0A, in late-game loads (product 0491 run b1; stock 9074 T1150/T1300/T1600 and #13397 fix runs 1-2): root cause".**
Acceptance: from the minidumps already collected (c5h-i `run/crash/*_pid28828`, `*_pid5608`, `*_pid3312`,
`*_pid70412`, `*_pid41176`, `*_pid63660`; read only, `cdb -z`) and a static read of the function around 0x55D91A
(D83: addresses and behaviour only): what the dispatcher's handler writes, which object or record the faulting address
belongs to, and whether it is the farm-field graph of section 3 or another structure; the cause written into
`../02-exe-internals/crashes-huge-load.md` (new section) with its confidence and what would confirm an [I]; if the fix
lies in EXE code, the MC asks the Architect for routes (D185). No game run unless the dumps leave the structure unnamed.

**T2 (row 4, section 3): "The farm-field graph's node links form a cycle in 9074's late-game stock loads with sound free
lists: how, and whether epic 0528 retires it".** Acceptance: from 9074's T1100 hang dump (read only): walk the cycle
through +0x2C from each of the four nodes and list every member's kind, cell, endpoint ids and point store; check every
16-bit id field against the array sizes (55,683 nodes, 37,328 endpoints); name the code path that writes +0x2C without
a grid cell (static read); state whether 0527's guard check (`farm-builder-guard.md` 4) would see this state and whether
0528's switch-off removes the path; written into `../02-exe-internals/farm-field-double-free.md`.

**T3 (row 5): "Product-route EXE crash at VA 0x8E81C3 during a load (2026-11-16 08:36, c5h-c, 0392/4510), no box:
root cause".** Acceptance: a static read of the function holding VA 0x8E81C3 (what it reads at that instruction, its
callers, which system), the evidence of section 4.1 and the run's game logs if still on c5h-c; the cause or the
leading candidate with its confidence written into `../02-exe-internals/crashes-huge-load.md` or a new file;
a reproduction only if the static read names a trigger to provoke.

**T4 (harness, section 1): "The dialog watcher's ledger tells causes apart and names known ones".** Acceptance: a VP
crash box's signature carries its `Location (in file)` and exception code (rows 2-4, 11, 12, 14 then become separate
signatures), a patch popup's signature carries its stage line (`pack: no-pack`, `uidraw: test`, `server-death: ...`),
and a stock game with an `-ia` DLL that ends after a journey's own EXIT/Yes (no `lua_exit_request` line) gets 0483's
known signature, as `UI.ExitGame()` does since 0483 (row 8: 9 of the 12 exits came after 0483's merge of 2026-11-17
04:42 and all 9 still got the lumped signature, one of them with the hint); the `bug` field of known signatures filled (`dialogwatch` tests, offline).

**T5 (section 4.2, 4.3): "Attribute the crash records outside the workstation ledger".** Acceptance: the four
unattributed Goldberg records (0x00560A98, 0x7C3DA2EA, four 0xE06D7363) each tied to its run by time in the instance's
`run/harness.log` (patch or stock, which ticket), with a root cause or a bug; oldpc's and the laptop's
`dialog_signatures` folders read through a testbed job (read only) and added to this file's table.
*Status 2026-11-18: opened as ticket 0541; the Goldberg part and oldpc are in section 8; the laptop waits for a
window that the MC names (D163: the laptop is epic 0437's quiet space).*

**T6 (section 8.1): "Product-route EXE fault at absolute 0x7C3DA2EA in the mod deactivation after a game with (4a)
Squads for VP (0153 run 2, 2026-11-13 22:41:14 EST, c5h-c, pid 18880): root cause".** Acceptance: 0153's flow (CP + VP
+ EUI + Squads activated in the Mods menu, a Tiny game, 3 AI turns, a quick save, Exit to Main Menu, the exit's Yes)
on the product route of the current pins, with the proxy's chained exception filter (0419: `exe-crash.txt` names the
module and offset) and `shell/evidence/0164/cdb_watch.py` on the EXE, run until it ends the same way or for the number
of runs the MC sets (0174 ran it 3 times without the end, on 0153's build); a second-chance fault read for its module,
offset, thread and stack (D83: addresses and behaviour only); the cause written into
`../03-dll-engine-boundary/proxy-design.md` 20 with its confidence; if the end does not return, the module named from
the facts of section 8.1 by the next reading that can name it, and the open question stated, never closed on "not
reproduced" (D185).

## 6. Corrections owed in other files (not made here: outside 0534's touches)

- `scene-catalogue.md` 12: "c5h-mpk's unrelated crash of 01:16 (pid 35632 ...); cause not investigated" -> its cause is
  in `installer-notes.md` 14.9 item 4 (row 7).
- `human-input-testing.md` 7.6 (the stock load crash of 2026-11-16 05:15:54): "whether the D33 arm or the composite keys
  under `mode=off` cause the crash ... is not known" and "[I] address-space exhaustion" -> the site VA 0x8CD492 is the
  River/Arrows NULL-batch crash of D33's config.ini without the proxy's view-mask clear (row 14; 3516, D116).
- `../08-performance/marathon-stress-t2000.md` 4.1-4.2: "[I, what the list is]" -> the farm-field layout graph's node
  array [C, dump] (section 3).
- `../02-exe-internals/crashes-huge-load.md` 8 and `farm-field-double-free.md`: the product's farm-field crash count
  leaves out 0517's crash at VA 0x77C6DA in release-large's load of a Large scene (row 6), and the stock side has shown
  the weld loop's read too (9074 T1100 run 3, 0x77C6DA, 43-civ late-game save; row 4).

- (2026-11-18, ticket 0541) `../03-dll-engine-boundary/proxy-design.md` 20, the Squads proxy row ("the EXE is gone
  during the deactivation ... no dump"), and 23.5: the end is an access violation at absolute 0x7C3DA2EA at about
  22:41:14.9 EST, after the stopwatch's `LoadCoreDatabase - Detach Loc Database` and before `Load Database`, with the
  proxy still loaded and its server attached (no `SetDLLIFace(NULL)`, no detach line); section 8.1 here.
- (2026-11-18, ticket 0541) `release-driver.md`, "Why the EXE aborted" (0175 run 2): the "[I for the last step]" (the
  EXE turned the mods off) has a direct sign: the harness's `lua_channel` read at the abort (20:13:16) found only the
  base game's DLL mapped, not the proxy (section 8.1).

## 7. Not verified (2026-11-18)

- Row 8's twelve pids are classed by the pattern (stock route, `-ia` DLL's `instruments.log` line without the proxy's
  channel, the crash seconds after a journey's or Lua's exit, the owners' D155 reports); no dump was read for them.
- Section 4.2's attributions by low address bits and date are [I] where the run's EXE base is not recorded.
- Section 3 read one dump of one hang; the other 9074 failures were not read for the graph.

## 8. The records outside the workstation ledger (ticket 0541, 2026-11-18)

Read only; no game instance was started and no instance file changed. Scripts and output in
`tools/harness/evidence/0541/`: `gbe_runs.py` -> `gbe-runs.txt` (each Goldberg record's run from the instance's
`run/harness.log`, whose times are EST; the Goldberg printer's are UTC, EST = UTC - 5 in November 2026),
`pe_ranges.py` (preferred image ranges from PE headers), `read_ledger_job.py` (the testbed job of 8.2) ->
`oldpc-ledger.txt`, `proxy-candidates.txt` (8.1.1), and `acceptance.txt`.

### 8.1 The six open Goldberg records: every one a product-route run

| Record (UTC) | Run (EST, from `harness.log`) | What it was | Cause (where written) | Conf. |
|---|---|---|---|---|
| c5h-a 2026-11-10 19:44:10, AV at 0x00560A98 | launch 14:43:00, pid 30004, `server.exe` child 14:44:09 | epic 2500's first real-EXE boot | the EXE Destroyed the game context, the proxy forwarded it, the next calls failed "bad handle", the EXE crashed 1.6 s after boot; fixed in 2500 by pinning the context: `../03-dll-engine-boundary/proxy-design.md` 15.2 item 1, 15.7 ("14:44 first boot") | L (2500), C (time match) |
| c5h-a 2026-11-10 20:10:00, 0xE06D7363 | launch 15:05:51, pid 52000, child 15:07:52 | epic 2500's P1 retry | the unsized save stream read in chunks to the 512 MB cap, a C++ exception, the CRT abort box (OK pressed by the coordinator about 15:12); fixed by the pull session (`kRt_StreamOpenPull`): proxy-design.md 15.2 item 3, 15.7 ("15:08-15:13") | L (2500), C (time match) |
| c5h-c 2026-11-11 21:30:51, 0xE06D7363 | launch 15:45:31, pid 40432, child 15:46:45; the harness met the CRT box at 16:32:41 | one-off 9034's `p2-huge-new` long run, turn 420 (bug 0102) | `std::bad_alloc` on the EXE's main thread with the 4 GB address space used up (the proxy's `g_eng` table under `g_lock`; the proxy route grew 24.3 MB per AI turn); fixed by 9036 and 9041, closed 2026-11-13 after the end-game run to turn 437: `tracker/issues/0102-*.md`, proxy-design.md 18.6 | C (9036's dump) |
| c5h-b 2026-11-15 01:13:14, 0xE06D7363 | launch 20:11:10, pid 1608, child 20:12:17; CRT box met 20:13:16 | ticket 0175's second fresh run, the huge arm of the release driver's instance stage | the server-written reference save had an empty mod list; the EXE turned CP and VP off for it and loaded a Vox Populi save without Vox Populi; fixed by `refsave.py` `with_release_mods`: `release-driver.md`, "Why the EXE aborted". New sign here: the harness's `lua_channel` read at 20:13:16 found **only the base game's DLL mapped** (the proxy gone), as that explanation needs | C header + proxy log; L the DLL switch |
| c5h-c 2026-11-15 13:49:10, 0xE06D7363 | launch 08:46:55, pid 28800, child 08:48:33; CRT box met 08:59:15 | ticket 4254's cross-load arm (a 22 save in the 43 layout; epic 0334) | the EXE stream's read-past-end C++ exception unwound through the proxy's `kRtE_StreamRead` branch, which had no catch, into `terminate()`; fixed by 0359 (that branch inside 9036's catch, then D118's popup): `../03-dll-engine-boundary/player-limits.md` (0354's verdict, item 5) | C (dump + source) |
| c5h-c 2026-11-14 03:41:14, AV at **0x7C3DA2EA** | launch 22:31:10 (2026-11-13), pid 18880, child 22:34:47; `stop` at 22:44:19 found the EXE gone | queue ticket 0153's run 2 on the product route with the extra mod (4a) Squads for VP: a Tiny game, 3 AI turns, a quick save, Exit to Main Menu, the exit's Yes (22:41:02), then the EXE's mod deactivation | **open**: proxy-design.md 20 (the Squads proxy row), 20.1 (ticket 0545: not returned in 6 batch-4 runs; the address is most likely in no module, an execute fault through a bad code pointer) and 23.5; 8.1.1 adds what the records show | C run and time; I module none; cause none |

So the four 0xE06D7363 records are the EXE's CRT abort box ("Runtime Error! ... terminate it in an unusual way"):
the Goldberg printer records the C++ exception when it is raised, and the box follows from `terminate()`. None of the
six is on the stock route.

#### 8.1.1 What the records show of 0153 run 2's end (2026-11-13 22:41 EST, c5h-c, pid 18880)

Sources [C, files]: 0153's kept scratch `D:/c5-64bit-work/0153/` (`vp64-runs/20261113-223313-18880-1/`:
`vpproxy.log`, `server.log`, `render.log`; `live2/out/stopwatch.log`; `live2/session.log`), the instance's
`run/harness.log` and `run/shim.log`, and the proxy source at the build's commit (`490bbfc38`; proxy EE5D59FE...,
composite C3C3F9C2...).

- **The time.** The server's run folder starts 22:33:13 and the server logged the client's exit with code
  0xC0000005 at 481.063 s ("client pid 18880 exited (code 3221225477)"), that is 22:41:14-15; the Goldberg record is
  22:41:14. One crash, on the product route [C].
- **Where in the EXE's work [I, to about a second].** The EXE's stopwatch for that session ends with
  `BeforeDeactivateMods - Release Database Cache` (0.083 s), `Unload DLL`, `AfterDeactivateMods - Reset filesystem`,
  `LoadCoreDatabase - Detach Loc Database` (0.053 s). Anchored on the `Cache Game Database Data` step (11.7 s in both
  the stopwatch and the proxy log), the last line falls at the proxy's 481.1 s: the fault came within about a second
  after the Localization database was detached, in the rebuild's next steps (`Load Database` and the merged
  localization database, 0.02 s and about 2 s in 0153's other sessions). The EXE's logs may lose buffered lines at a
  crash.
- **The proxy was still loaded and connected [C, log + source].** The proxy writes each log line with `WriteFile`
  (`proxy_rt.cpp` at `490bbfc38`), so no written line is lost at a crash. Its log has no `shutdown: SetDLLIFace(NULL)
  forwarded` line (the orderly shutdown, logged at every other deactivation in 0153's sessions, about 8 s after the
  same stopwatch steps) and no `detach:` line (`DetachOnUnload` logs "the proxy is unloaded without SetDLLIFace(NULL)"
  and terminates the server when the EXE frees the proxy without the shutdown). The server was not terminated: it
  served until the peer died. So the EXE faulted with the proxy mapped, before it released the DLL interface. The 0164
  mechanism (allocator slots pointing into an unloaded DLL) does not fit: the product route maps no `-ia` DLL, and the
  proxy had not been unloaded.
- **The server's renderer** drew normally to the end (`render.log` 22:41:13.977: ring READY, 117,093 frames
  published, 0 errors). At 22:41:14.982 (server 480.101 s) its ring sink read the shared fence as 0xFFFFFFFFFFFFFFFF
  ("its consumer ended") and made a new generation; 0.96 s later it saw the client's exit. D3D11 reports a fence's
  completed value as UINT64_MAX once a device is gone; whether that came before the fault or from it cannot be told
  at this resolution [I].
- **The faulting module is not named by these records.** No dump and no module list (the shim log records no module
  loads; the run predates 0419's chained filter, which now writes `exe-crash.txt` with module and offset). Every
  32-bit image in the instance, the proxy and the composite are ASLR images (`pe_ranges.py`: `DYNAMIC_BASE`; none of the
  seven without it has a preferred range that holds the address). The composite was loaded at 0x7AC50000
  (`vpproxy.log`; SizeOfImage 0x64B000), so it ends at 0x7B29B000: **not the composite** [C]. Were the address in the
  proxy (SizeOfImage 0x1B5000, `.text` RVA 0x1000-0x8FB3A, loaded at a 64 KB boundary), its RVA would be
  0xA2EA + k x 0x10000, k = 0..8; `proxy-candidates.txt` disassembles the nine candidates with the proxy's PDB
  (`cdb -z` on the image file): none is a return address after a call (where a thread waiting in the proxy would
  resume) and two are not instruction boundaries; a proxy thread is not indicated [C the disassembly; I the
  conclusion]. Not checked: the game's own DLLs and the display driver's user-mode DLL, at unknown bases.
- **Status (D185).** Root cause not determined. The facts narrow it to a fault in the EXE process during the mod
  rebuild after `Detach Loc Database`, with the proxy, the server and the composite still running. Bug proposal T6
  (section 5) is for the MC; `proxy-design.md` 20 and 23.5 need these facts (section 6).
  *Update 2026-11-18 (ticket 0545, `../03-dll-engine-boundary/proxy-design.md` 20.1):* six runs of the flow on the
  batch-4 product route (c5h-i), three with 0153 run 2's real tail, did not end this way. 0153 run 2's own session.log
  shows that tail: its Esc after the Quick Save closed the menu, two clicks landed on the Settler's plot, and the game
  stood about 5 min before the exit. The address is the exception address (EIP), and in the same flow today
  nothing executable is mapped above the compositor, which was the highest image in 0153's session too
  (0x7AC50000-0x7B29B000); 0x7C3DA2EA lay in private read-write memory in r5. So the module is most likely **none**:
  an execute fault after a transfer through a stale or overwritten code pointer [I]. The module that made the transfer
  is unknown, and the question stays open.

### 8.2 oldpc's ledger: one per testbed job (jobs 20261118-075745-4f0f and 20261118-081733-4896)

**There is no machine-wide ledger on a testbed host [C, job + source].** The first job (`read_ledger_job.py`, 07:57-
08:14 EST, `tb.py --host oldpc submit --slots 1 --timeout 600`, not quiet, no window, no game) found no
`dialog_signatures` folder in `%USERPROFILE%/c5-instances/_pool` (where `pool.pool_dir()` points on a testbed host: the
runner sets only `C5_INSTANCES_ROOT`) or under `E:/c5-testbed/instances/_pool`. The testbed's game jobs set
`C5_POOL_DIR=<job>/work/pool` (`tools/testbed/jobs/product_path.py`, `live_session.py`, `headed_probe.py`,
`addrspace_ab.py`, `perf/profilekit/tbjob.py`), so each job keeps its own ledger in `jobs/<id>/work/pool/dialog_signatures`,
and `tb.py prune` deletes a job's `work/` past its retention: **a test machine's sightings live only as long as the job's
work folder** and none reaches the queue. The second job (08:17-08:20, the same script with the per-job paths added,
`oldpc-ledger.txt`) read 6 per-job ledgers still on the host (jobs from 2026-11-16 19:42 on) and the instances' Goldberg
files and `run/crash/` manifests (tb1 5 folders, tb2 1; tb3-tb6 do not exist on oldpc).

Same columns as section 2 (Arm as there; times EST):

| # | Id | Kind | Signature / site | First - last seen | Count | Arm | Root cause written (file, section) | Conf. |
|---|---|---|---|---|---|---|---|---|
| O1 | `4c7da30500db` | crash, no box | EXE AV read at **VA 0x77C6DA** (weld loop of 0x77C1C0), the proxy's boot thread, in a huge load; Goldberg tb1 2026-11-17 02:21:54 UTC | 2026-11-16 21:21:57 | 1 (tb1, job 20261116-194257-1251: 0431/4555 trees T5 set 1, launch 16, arm n) | **product** | as row 6: `../02-exe-internals/crashes-huge-load.md` 2 and 8, `farm-field-double-free.md`; this sighting in `../05-graphics-composition/trees.md` 15.10 (1 of 37 huge product loads there); open until epic 0528 (D186) | I (as row 6) |
| O2 | `5188729e1a4c` | crash, no box | EXE AV at **VA 0x77C70D** (same function), the proxy's boot thread, in a huge load; Goldberg tb1 2026-11-17 23:13:00 UTC | 2026-11-17 18:13:03 | 1 (tb1, job 20261117-170336-9fdb: 0435 device-flag series, launch 14, arm R3-v0) | **product** | `../02-exe-internals/crashes-huge-load.md` 7 (the farm-field class); open until epic 0528 | I (as row 6) |
| O3 | `ef93300efeb8` | fatal ("Render Error") | `FGXRenderer11::Present failed hr=DXGI_ERROR_DEVICE_REMOVED` | 2026-11-17 01:40:04 | 1 (tb1, job 20261117-012523-0375: 0456's live stock session 2; three box evidence folders of pid 22080, 01:40:06-01:44:38, `found_crash` false) | stock (the pin's `-ia` DLL, no proxy) | `quiet-testbed.md` 25: Windows Update replaced oldpc's NVIDIA driver 01:38:51-01:40:03; the box was the driver swap; D175 holds drivers since | L |
| O4 | `62fbaca452d6` | patch popup | "The 64-bit part of the patch failed ... config.ini is back" | 2026-11-17 14:37:58 | 1 (tb2, job 20261117-143248-44d4: 4758 item (3) a, first try) | product layout, **test input** | the job's inputs, not the patch: the pack manifest named the x64 DLL by its 2606 name while the job laid the development name, so the proxy's pack check stopped (`stage: pack, file-missing`): `../05-graphics-composition/unit-draw-filter.md` (item (3) (a), "Two failed submissions"); the job was fixed | L |
| O5 | `62fbaca452d6` | patch popup | the same popup (`units: pre-check` failed, server exit 5) | 2026-11-17 14:46:24 | 1 (tb2, job 20261117-144243-b562: 4758 item (3) a) | product, **deliberate** | the server's units made to fail on purpose (`render_test_units_db=`): `unit-draw-filter.md` item (3) (a) | L |
| O6 | `62fbaca452d6` | patch popup | the same popup (`units drawn twice (D118)`, server exit 5) | 2026-11-17 14:55:55 | 1 (tb2, job 20261117-145124-9d6b: 4758 item (3) b; box evidence folder `20261117_145555_pid5980`) | product, **deliberate** | `unit_filter=off` with ServerUnits = 1, D118's negative arm: `unit-draw-filter.md` item (3) (b) | L |

So every oldpc sighting still on the host has a written cause; the two product-route crashes are the farm-field class
of row 6 (open until epic 0528). Older jobs' ledgers (before 2026-11-16 19:42) were pruned and cannot be read; the
crashes from them that the knowledge base records are those of section 4.3. Proposal for the MC (an addition to T4 or
its own harness ticket): testbed jobs count their sightings into a machine-wide ledger (or `fetch` brings the per-job
ledger back and it is counted on the workstation), so that a test machine's crash classes outlive `tb.py prune`.
(2026-11-18, ticket 0546: done in the narrower form of 8.4, a per-job archive that the prune cannot remove; a
machine-wide count is not made.)

### 8.3 The laptop's ledger: read by ticket 0546 (laptop job 20261118-095601-b2f4, 2026-11-18)

*(Was, ticket 0541: "not read"; the laptop is epic 0437's quiet space, D163.)* In the MC's window (MC #5552, directly
after lead-0398's item-36 job `20261118-095459-f29c`, in the laptop queue from 09:54:59) one read-only job ran:
`python -I tools/testbed/tb.py --host laptop submit --slots 1 --timeout 600 --name "0546 laptop ledger read (read only,
no game)" -- "%TESTBED_PYTHON%" tools/harness/evidence/0541/read_ledger_job.py`, submitted 09:56:01, ran 09:58:46-47
(1.0 s, exit 0; no game, no instance file written). Output (user paths replaced): `tools/harness/evidence/0546/laptop-ledger.txt`
[C, files].

**What the laptop holds [C].** As on oldpc, no machine-wide ledger (`%USERPROFILE%/c5-instances/_pool` and
`C:/c5-testbed/instances/_pool` hold none). **Two per-job ledgers** are left, of the jobs whose `work/` the 72 h
retention (or an exit-code hold) has kept: `20261117-202351-7fdf` (ticket 0428) and `20261118-051814-ea8b` (ticket
0519). A job that met no box writes no ledger folder (`dialogwatch.ledger_count` creates it at the first count), so the
other unpruned jobs (the turn-study series, 0398's HI jobs) met none [I: their pool may sit outside the five patterns the
script searches; the script also searched one and two levels under the root]. The instances: tb1 has 4 crash evidence
folders and 4 Goldberg crash printer records; tb2 none.

Same columns as section 2 (times EST):

| # | Id | Kind | Signature / site | First - last seen | Count | Arm | Root cause written (file, section) | Conf. |
|---|---|---|---|---|---|---|---|---|
| L1 | `fa8b89fc8333` | WER box ("unknown"; bug 0531's class) | `WerFault.exe: Sid Meier's Civilization V`, after the EXE's AV read at **VA 0x536580** (EXE+0x136580, image at 0xE40000; Goldberg tb1 2026-11-18 01:31:02 UTC `@ 0x00F76580`; evidence folder `20261117_203112_pid12200`) in a huge product load | 2026-11-17 20:31:12 | 1 (tb1, job 7fdf: 0428 (1) load 2 of 5) | **product** | the box: 0531 (WER's box of a crash; counted here before 0531 counted the crash under the proxy record's signature). The crash: `../02-exe-internals/crashes-huge-load.md` 8 (remove-by-index's shift after a double free in the farm-field layout, the class of row 6 and O1/O2), `farm-field-double-free.md`; open until epic 0528 (D186) | I (as row 6) |
| L2 | `fa8b89fc8333` | WER box | the same box, after the job's **injected** fault (a remote thread at address 0, AV at 0x00000000) | 2026-11-18 06:06:59 | 1 (tb1, job ea8b: 0519 live rerun) | product, **deliberate** | ticket 0519's own test crash (TEST ONLY, no EXE byte changed): `../03-dll-engine-boundary/server-host.md` 19.4-19.5 | L |
| L3 | `c4dcd8fa7534` | crash (proxy record) | `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at an address in no module (0x00000000)`; Goldberg tb1 2026-11-18 11:06:43 UTC `@ 0x00000000` | 2026-11-18 06:07:13 | 1 (tb1, job ea8b) | product, **deliberate** | the same injected fault (thread 16188, process 1772): `server-host.md` 19.5 | L |
| L4 | `50a8c3a6980a` | patch popup | "The game itself crashed (the game's own process, not the 64-bit server), so the game had to end." (D118, stage `exe-crash`) | 2026-11-18 06:07:15 | 1 (tb1, job ea8b) | product, **deliberate** | the D118/D119 route of L3, the check's expected end: `server-host.md` 19.5; route bug 0418 (known_bug's `exe-crash` stage) | L |

**Crashes only in the Goldberg file (before the ledger counted boxless crashes, 2026-11-17 01:16) [C, file].** tb1's
`run/gbe/crashes.txt` also holds two records `0xc0000005 @ 0x00A9D80D` at 2026-11-14 07:38:46 and 07:46:51 UTC (02:38
and 02:46 EST), with the EXE at 0x00890000: **VA 0x60D80D**, 3516's two load crashes (jobs `20261114-022913-eb2a` arm a
and `20261114-024408-cd76` arm a1). Cause written: the run-time config-mask write of D116's option A (and A') after app
Startup, 2 of 2, in the world-to-grid transform 0x60D790 reading a dead object (`release-driver.md` W3b.3, W3b.5;
`../02-exe-internals/disable-systems-and-masks.md`) [L the trigger, C the faulting read; which skipped system's object
it is was not bisected, on the Architect's advice: `../07-architecture-decisions/architect-2026-11-14-failure-popup-and-report.md`
item 5]. That route was dropped (D118/D119): no shipped configuration writes the mask at run time. These are the
"D33 load crashes" of section 4.3; their jobs' ledgers (if any) were pruned.

So every laptop sighting still on the host has a written cause: one product-route crash of the farm-field class (open
until epic 0528, as on oldpc and the workstation), three sightings of 0519's deliberate fault, and, from the Goldberg
file, the dropped option A's two load crashes. **No new bug is proposed.** Not readable: ledgers of laptop jobs pruned
before 2026-11-15 about 10:00 (72 h before the read), apart from what their Goldberg records show. From the laptop's
next runner restart on 0546.1 (section 8.4) a pruned job's records stay in `ledger-archive/`.

### 8.4 Where a test machine's sightings are kept after `tb.py prune` (ticket 0546, 2026-11-18)

**The kept place: `<testbed root>/ledger-archive/<job id>/`** on each test machine (oldpc and laptop: the root of
`tools/testbed/hosts/<host>.json`). From runner version 0546.1 the runner's retention pass (`prune_jobs` in
`tools/testbed/remote/runner.py`, every 30 min and `tb.py prune`) first copies every signature ledger record of the
job, `jobs/<id>/work/**/dialog_signatures/*.json`, to `ledger-archive/<id>/<the same path under the job folder>`
(for the usual game job: `ledger-archive/<id>/work/pool/dialog_signatures/<signature id>.json`), with
`ledger-archive/<id>/index.json` naming the job (id, name, submission time, state, exit code, the records copied, any
skipped as larger than 1 MB). Only the records go: no `.lock` files, no dumps, no evidence folders (those stay under
`out/` when a job wrote them there, or go with `work/`). If a record cannot be copied, that job keeps its whole bulk
for that pass and the pass reports an error (`tb.py prune` prints `error <job>: ledger archive failed, bulk kept` and
exits 1); the next pass tries again. `jobs/<id>/pruned.json` gains `ledger_archive` and `ledger_records`; `tb.py prune`
prints `ledger <job>: N record(s) kept in ledger-archive/<job>` [C, `tools/testbed/tests/test_prune_keeps_ledger.py`,
10 offline cases, 2026-11-18].

What an audit reads: the jobs still unpruned (`jobs/*/work/**/dialog_signatures`, as 0541's `read_ledger_job.py`
does) plus `ledger-archive/*/**/dialog_signatures`; `index.json` gives the job's name where `job.json` is no longer
next to the records (it is: `job.json` is a kept record of 0187, but the archive does not depend on it).
`read_ledger_job.py` (0541's evidence, not changed here) does not yet search `ledger-archive/`; a later audit adds the
pattern `ledger-archive/*/work/**/dialog_signatures`.

Not covered [I]: the jobs pruned before a host's runner reached 0546.1 (their ledgers are gone: oldpc's before
2026-11-16 19:42, section 8.2); a job that writes its ledger outside `work/` (none of the game jobs listed in 8.2
does); a machine-wide count (the archive is per job; an audit sums the records). **Deployment:** the hosts run the
new runner only after `python tools/testbed/commission.py --host <h> ship-tooling` and `restart-runner` from `main`
(jobs keep running); not done by 0546 (the laptop is 0437's quiet space, D163; oldpc's runner change is the MC's
call). Until then oldpc and the laptop prune as before.
