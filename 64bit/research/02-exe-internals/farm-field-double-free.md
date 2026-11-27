# The farm-field layout's double free, measured (decision D186, tickets 0526 and later)

The EXE's farm-field layout (LandmarkSystem, layout graph at VA 0xBA2228) and the double free of a closed loop's
endpoint in its node delete 0x727420: what a debugger sees in live huge loads, stock and product. This file continues
`crashes-huge-load.md` section 8 (the code path, 0515's static reading) and the Architect's plan
(`../07-architecture-decisions/architect-2026-11-18-farm-field-layout-ticket.md`, pieces F0-F5). Addresses are DX11
VAs at ImageBase 0x400000 (EXE `04cfdc71...` on the Goldberg instances, `.text` as `crashes-huge-load.md` 1). Nothing
from the EXE is in this file but addresses, offsets and behaviour (D83).

**Section 2 (ticket 0538, 2026-11-18) is a second, separate fault of the same graph**: on maps of 16,384 plots or more the
grid has more than 65,535 cells, the 16-bit cell id overflows, and the cell lists break (9074's T1100 hang and its
0x5B1FB7 crashes). It is not the double free of section 1 and needs no double free.

**Section 3 (ticket 0542, 2026-11-18) is the terrain bake's share of that graph**: the bake inserts the coastlines, welds
each piece once, refiles a filed piece only when another polyline crosses it (never seen: coastlines do not meet), and its
one delete call removes a piece that was never filed. With ServerFarmFields on, nothing the bake does breaks a cell list
on the four oversize dumps read; the Architect's rule 2 (radius 2 around cell 65,535) holds on those maps, with one
condition for other maps (3.5).

## 1. F0: counting the delete under a debugger (ticket 0526, 2026-11-18)

### 1.1 Method

**The functions** [C, read 2026-11-18 on 0515's scratch copy of the EXE with the SDK's `cdb -z`, `u`; not committed]:

- **Node delete 0x727420**: thiscall (`ecx` = the layout graph), one stack argument (the node index), `ret 4`. The node
  record is `[this+0xC4] + index * 56`. If the node's kind byte +0x36 is 0 it returns at once. Otherwise it takes the
  node out of its grid cell (+0x34, 0xFFFF none), then calls the remove-by-index 0x536540 on the endpoint named by
  +0x16 and then on the one named by +0x14 (entries at `[this+0x9C] + id * 20`), reads the +0x14 entry's count, frees
  the +0x16 entry if its count is 0 and then the +0x14 entry if the count read before was 0 (the free 0x5855A0 on the
  header `this+0x94`; the two calls return to 0x727508 and 0x72751D), sets both ids to 0xFFFF, marks the node freed
  (bit 31 of +0x2C) and links it into the node free list (+0xBC head, +0xC0 live count). As `crashes-huge-load.md` 8.3
  says: the two ids are never compared, so a closed loop whose endpoint holds nothing but that node's handles gets its
  endpoint freed twice.
- **Endpoint free 0x5855A0**: thiscall on a free-list header (free head, live count, data, size, capacity), one
  argument (the index), 20-byte entries with the count word at +0x10, `ret 4`. It sets bit 31 without looking at it
  first, so the count word's bit 31 **before** the call tells a second free of the same entry.

**The breakpoint method planned for F0 is impossible** (section 1.2). What was measured instead, as amended by the MC
(ticket thread #4980, 2026-11-18): the double free's **footprint**, read only, in every load; and the route builder's
reach to the delete, statically (section 1.4).

**The footprint is a complete per-load detector** [C, from the free's behaviour above and `crashes-huge-load.md` 8.3]:
a second free of an entry always links the entry to itself (if it is the head, the else branch writes
`head.next = entry` on itself; otherwise its index is above the head's and `entry.next = head.next = entry`), and the
live count drops twice for one entry. The allocator then keeps handing out that entry and afterwards live entries, so the
walk from the head no longer ends at 0x0FFFFFFF over freed entries only. 0515's `layout_peek.py` (`endpoint_free_list`:
the walk, `chain_ends_at_nil`, `live_on_chain`, `cycle_at`, live + freed - size) reports any such state as
`sound = false`. A load in which no second free of an endpoint happened shows a sound list; a load in which one
happened shows an unsound list from that moment on [I that nothing in the layout repairs it later: a repair needs the
live entries on the chain to be freed again in a fitting order; 0515 saw none]. What it cannot give: the number of
deletes, their callers, and how many lone closed loops a load deletes (those need a breakpoint).

**The debugger** [L]: `shell/evidence/0526/run_0526.py` runs each load as 0410's driver does (`run_0410.py` imported:
prep, launch by the no-focus launcher on a hidden desktop, guard check, invasive cdb attached about 2 s after the
launch, layout graph read every 3 s, the load screen closed at its first Continue, 20 s in game, the harness's stop,
restore by hash), with four **hardware execute breakpoints** (`ba e1`, x86 debug registers DR0-DR3: no byte of the EXE
is written, not even in memory; only with `--hwbp`, see 1.2) and no software breakpoint at all: on the delete, on the free, on the farm builder
0x748FC0 and on the route builder 0x5EC250. At each hit cdb prints one line and continues: the delete's `this`,
return address, node index, kind byte, both endpoint ids and `esp`, and for a closed loop (both ids equal) the
endpoint entry's four slots and count word as they are before the delete; the free's header, return address, index,
the entry's count word before the free and the free head; the builders' return address and `esp`. cdb stays attached
until the harness's stop has ended the EXE: ending cdb earlier (0410 did) would leave the debug registers set in a
process without a debugger, and the next hit would end it. `shell/evidence/0526/summary_0526.py` reads the log:
deletes by graph, caller, kind byte and builder; closed loops; **alone** = a closed loop whose endpoint holds only
handles of that node (count 1 to 4, every handle's index = the node: the double-free case); frees, and **second
frees** (bit 31 already set), each tied to its delete; the graph check after the load (0515's `layout_peek.py`, with
the free-list walk). **The series ran without `--hwbp`**: cdb attached with no breakpoint (only to write a full dump
if a load crashes, 0164's exception settings, without 0164's ntdll breakpoint), the graph and its free list read
every 2 s from the attach to the stop (0410's `LayoutWatch`, 0515's peek) and once after the load.

### 1.2 The hardware breakpoint ends the EXE (0x8000DEAD) [L, 2026-11-18]

- Pilot s00: c5h-e (Goldberg, F:, EXE `04cfdc71...`), stock arm (below), cdb attached 01:31:47 EST with the four
  `ba e1` of 1.1 (`bl` showed all four set on every thread, `0:****`, EXE base 0x005E0000). The EXE ended by itself at
  01:32:41, 79 s after its start and before the load reached the layout (no delete, free or builder hit), with every
  thread's exit code **0x8000DEAD**; cdb logged `SetContext failed, 0x80070005` 798 times (thread contexts the
  debugger could not write).
- This is the EXE's DRM tamper exit that `../03-dll-engine-boundary/proxy-design.md` 23.4 had already recorded for
  `ba e1` on 2026-11-14 (`ExitProcess(0x8000DEAD)` from `CivilizationV_DX11+0x2ed408`, 2 of 2 runs, also for a
  breakpoint in a DLL): **any debug register set in the EXE's process ends it**. Not a crash of the game: the cause is
  the tool [C by the earlier stack, L here]. (Time lost: the knowledge base had it; search for "ba e1" / "hardware
  breakpoint" before planning a debugger method.)
- Consequence: no hardware breakpoint, and no software breakpoint (a code byte, excluded by D4 and by the ticket),
  can count the delete live. **Unanswered without one: the deletes by caller, the deletes per load, and the number of
  lone closed loops (a closed loop alone on its endpoint) each load deletes.** A counting means that writes no code
  byte and sets no debug register is not known [I: page-guarding the endpoint array's pages would trap every access
  of the layout and was not tried].

### 1.3 The series: 12 stock and 12 product huge loads [L, 2026-11-18]

**Setup.** Workstation, instance c5h-e (Goldberg, F:, EXE `04cfdc71...`), functional runs under other teams' load
(no quiet lock: nothing here is a timing). Save `C5H-VP8P-HUGE_0350` (c499d3c6..., turn 350, 0515's save) copied per
arm as `C5H-0526S-HUGE_0350` / `C5H-0526P-HUGE_0350` and removed at the restore. **Stock arm**: the instance as
provisioned: 32-bit DLL `3c2f47da...` (batch 2's instance-aware build, the DLL of 0515's stock arm; a labelled
comparison arm under COMMON 18, the DLL does not touch the EXE's farm layout), stock config.ini, no proxy.
**Product arm**: 0515's builds: the pin layout `x64-4f4daac7-pf8e9f42` (server.exe `ed829bda...`, x64 DLL
`2d737bd5...`, proxy `4815ed81...`) set by `provision.py set-proxy`, 0515's composite `ff72e6e6...` (main 4cb853d65,
copied unchanged), D33 with Overlay kept on, `ServerRenderer = 1` (0410's / 3308's prep). Series by
`shell/evidence/0526/series.sh`; per-load table by `summary_0526.py --table` over `C:/c5-64bit-work/0526/runs/<tag>`.

**Samples taken while the layout runs can be torn** [L, C for why]: the peek reads the header (head, live count) first
and the 9,000-entry array (about 180 KB) after it, while the main thread allocates. In s01 1 of 36 samples, in s03 4 of
39, a sample showed the head on a **live** entry whose count word (1 to 3) led to entries 1-3 and a cycle at 2, with
live + freed - size = -1 or -2. That is what one or two allocations between the two reads give (the allocator moved
the head and filled the entry after the header was read), and it looks like a double free's state; but a double free's
state cannot heal (once the head is on a live entry, every later allocation sets it to a live entry's count, 1 to 4,
never back to 0x0FFFFFFF; frees only link in front of or behind that head), and each such sample was followed 2 s
later by a sound one. So a load counts as **double free seen** only when the list is unsound in every sample from
some point to the end and after the load; single unsound samples are counted as torn reads.

**Results** (tables: `shell/evidence/0526/stock-table.txt`, `product-table.txt`; raw `C:/c5-64bit-work/0526/runs/`):

| Arm | Loads (EST) | Reached the map | Crashes | Double free seen (persistent unsound list) | After the load | Torn samples |
|---|---|---|---|---|---|---|
| stock | s01-s12 (01:35-02:25) | 12 | 0 | **0 of 12** | all sound: 8,753-9,093 live endpoints, 58-88 freed, live + freed = size; 13,354-13,943 nodes | 0-4 per load, 19 in 436 samples |
| product | p01-p12 (02:26-03:24) | 12 | 0 | **0 of 12** | all sound: 8,806-9,061 live endpoints, 58-81 freed, live + freed = size; 13,488-13,923 nodes | 0-4 per load, 22 in 448 samples |

Every load: EXE base 0x005E0000, the load screen's button 174-239 s (stock) and 202-283 s (product) after the start of the run (product
the slower), the harness's stop rc 0, no gbe crash line. The pilot s00 (1.2) is not counted. Both arms restored by
hash at 02:25 and 03:24 (`provision.py verify --quick` ok).

**What this answers** [L for the counts; C for the detector]:
- **Does the double free happen?** Not in any of these 24 loads. With 0515's 29 after-load checks of the same kind
  (oldpc, `crashes-huge-load.md` 8.5: 10 product, 19 stock, all sound) that is **0 in 53 huge loads**: the double free
  happens in at most **5.5 % of loads** (one-sided 95 %, 0 of 53); per arm at most 12.7 % (product, 0 of 22) and
  9.2 % (stock, 0 of 31); this series alone at most 22.1 % per arm (0 of 12).
- **How often per load?** When it does not happen, 0 times; no load showed one, so no load showed two.
- **Do the arms differ?** Not shown: 0 of 12 against 0 of 12 (Fisher's exact test p = 1).
- **Against the crash rate** (product 3 crashes in 57 loads, 5.3 %, `crashes-huge-load.md` 8.5): if every crash came
  from a double free and stock had it as often, 0 in 53 would have had probability 0.056 (0 in 22 product loads: 0.30).
  So the data are just compatible with the double free as the crashes' cause at that rate, and would begin to argue
  against it (or for a rate below the crashes' estimate) with more clean loads [I]. A double free that does happen is
  expected to leave the persistent state first: the crash needs it, and 0515's chain from the double free to the three
  crash sites takes later operations on the handed-out entries [C link by link].
- **Not answered** (needs a breakpoint, 1.2): the deletes per load and by caller, and how many lone closed loops a load
  deletes. The product's address space does not change the trigger here: the layout's inputs are the same save and
  the EXE base was the same in both arms; the composite and proxy only read the graph [C, `crashes-huge-load.md` 3].

### 1.4 Does the route builder meet the same delete? (draft section 6) [C for direct calls, 2026-11-18]

`shell/evidence/0526/callreach.py` follows direct calls breadth first with cdb's `uf /c` on 0515's scratch copy of the
EXE (`04cfdc71...`), from a function until nothing new is reached (indirect calls through registers or memory, such as
vtables and listener lists, are not followed). Output kept in `C:/c5-64bit-work/0526/reach*.out` (addresses only).

| From | Functions reached | Reaches the delete 0x727420 | the free 0x5855A0 | remove 0x536540 | weld 0x77C1C0 | 0x5B86D0 |
|---|---|---|---|---|---|---|
| farm builder 0x748FC0 | (stopped at the target) | yes: 0x748FC0 -> 0x6016F0 -> 0x493CF0 -> 0x727420 | yes: 0x6016F0 calls it directly | yes, via the merge 0x46F8B0 | yes, via 0x46F8B0 | yes, via 0x5362F0 |
| route builder 0x5EC250 (with its inner function 0x583CE0) | 211, the closure ends at depth 14 | **no** | no | no | no | no |

So **the route builder does not reach the layout graph's node functions by direct calls**: it has no graph of this
kind of its own, and roads and rails do not run this delete unless an indirect call leads there [I that none does: not
read]. The farm builder reaches the delete by a shorter path than 0515's (0x5B86D0's call at 0x5B8EA4): through
0x493CF0 (the caller site 0x493D6C of `crashes-huge-load.md` 8.3), called by the field layout 0x6016F0 itself. That
0xBA2228 is the object named in "RoadLayout::GameView::Startup()" (`world-systems-and-stubs.md`, the 0x4AB360 call)
does not make the route curves use it: the route builder's code never calls into it directly.

**The withdraw path of epic 0528** (MC note #5010, 2026-11-18, from `../05-graphics-composition/farm-fields-layout.md`
10.4 on branch epic-0528-farm-fields: the face walk 0x7A3BC0 has two early exits that leave the face-use count raised,
so an isolated closed loop survives the prune and 0x5C86D0, and the polygon withdraw 0x606030 deletes it with
0x727420 at 0x6061AF when its centroid is not on a farm plot) [C for direct calls, read 2026-11-18 by `callreach.py`
and `uf /c`]: the farm builder 0x748FC0 reaches every link of it: 0x748FC0 -> 0x4A8990 -> 0x7A3BC0 (the face walk),
0x748FC0 -> 0x4A8990 -> 0x5D3F50 -> 0x606030 (the withdraw), 0x748FC0 -> 0x61FD40 -> 0x5C86D0. The delete's direct
callers by function: 0x5B86D0 (site 0x5B8EA4), 0x493CF0 (0x493D6C), 0x5C86D0 (0x5C8740), 0x606030 (0x6061AF); all
four are reached from the farm builder, and **none** from the route builder (the same 211-function closure).
Which of the four fires on a lone closed loop in a live load needs a breakpoint (1.2) and stays [I].

### 1.5 Not verified (2026-11-18)

- Deletes per load, by caller, and lone closed loops deleted per load: no means without a breakpoint (1.2).
- That nothing repairs a double free's free-list state before the next 2-s sample: argued from the allocator and the
  free (1.1, 1.3) [C for the two functions, I for the whole layout]; the after-load check would still see it.
- Indirect calls from the route builder into the graph's functions (1.4: direct calls only).
- Other EXE bases: every load here had the EXE at 0x005E0000 (c5h-e); run5nd's was 0x00BA0000. Other hosts: these
  loads are the workstation only; 0515's 29 were oldpc.
- Which delete caller (0x5B86D0, 0x493CF0, 0x5C86D0, 0x606030) meets a lone closed loop in a live load (MC note #5010
  names 0x606030 [I]).

## 2. 9074's T1100 hang: the 16-bit cell id overflows on maps of 16,384 plots or more (ticket 0538, 2026-11-18)

The question of `../06-tooling-and-workflows/crash-ledger-audit.md` section 3 (row 4): 9074's T1100 run-1 hang walks
this graph's node array in a ring while both free lists are sound. **Answer: the ring is in the grid's cell lists, and it
comes from the grid's 16-bit cell id, not from the double free of section 1.** On 9074's 180 x 112-plot map the grid has
360 x 224 = 80,640 cells; cell 65,535 exists, and its id 0xFFFF is the value that means "in no cell". A node filed in
that cell therefore looks unfiled, its next refile does not take it out of that cell's list, and the lists break.

### 2.1 Method

- **Read only, no game run.** The full dump of the hung stock EXE that 9074 wrote
  (`F:/c5-64bit-work/9074/stock/t1100-run1/hang/x86-59172-20261116-181917.dmp`, 2026-11-16 18:19 EST, c5h-i, 43-civ
  `-ia` DLL of the batch-2 pin, save turn 1100; EXE at 0x008F0000, so the graph 0xBA2228 is at 0x01092228) opened with
  `cdb -z` (no process touched, no debug register, D65). Scripts and outputs in `tools/harness/evidence/0538/`:
  `hang_headers.sh` (graph header, registers, stack, the list object the loop works on), `dump_arrays.sh` (copies the
  node, endpoint, cell and point arrays and the header to `F:/c5-64bit-work/0538/`, scratch), `analyse_graph.py` ->
  `graph-analysis.txt` (the ring, every 16-bit id, every cell list), `list_direction.py` (which link runs which way),
  `cell_mapping.py` (cell index against position), `grid_dims.sh` -> `grid-dims.txt` (grid size in every other full
  32-bit dump on F:), `crash_dumps.sh` -> `crash-dumps.txt` (registers of the DLL's minidumps of the row-4 crashes).
- **The code** was read from the EXE image inside that dump (cdb `u` / `uf`; disassembly kept in scratch, not
  committed, D83); `weld_calls.py` -> `static-sites.txt` lists only call sites and the sites that write a node's
  +0x28, +0x2C or +0x34, as addresses. Everything below about the code is behaviour in our own words [C].

### 2.2 The node and cell layout, made exact [C, data of 80,640 cells and 55,683 nodes]

- **A cell** (24 bytes, array at graph+0x8C) is a list object: +0x00 the node pool (graph+0xBC), +0x04 the count,
  +0x08 the **first** node, +0x0C the **last** node, +0x14 the dirty byte. **A node's +0x28 is its next link and +0x2C
  its previous link** (bit 31 of +0x2C a flag): walking from +0x08 along +0x28 ends at +0x0C after exactly the count in
  80,638 of 80,640 cells, and from +0x0C along +0x2C back to +0x08 (`list_direction.py`). (`crashes-huge-load.md` 2 says
  only "the grid cell's list links".)
- **The cell index is column x rows + row** (column-major: column = (x - origin x) x 360 / extent x, row likewise with
  224), from the node's position: every one of the 55,669 nodes on a list fits it, **43,256 exactly and 12,413 only after
  keeping the low 16 bits** (`cell_mapping.py`). The 15,104 cells from 0x10000 up (from column 292, row 128 to the last) are all empty;
  their nodes sit in the cell 65,536 lower, and their +0x34 holds that same wrapped index. So the index is cut to 16 bits
  before it is used for the list and for the id alike.
- A node's +0x00..+0x0F is its own list object on graph+0xA8 (the third free-list array: 60-byte records, 158,212 in
  this state), +0x04 its count; +0x30 the tag the weld is given, +0x34 the cell id (16 bits, 0xFFFF none), +0x36 the kind.

### 2.3 The ring and the ids [C, data]

**The ring has six nodes, not four** (0534 saw four in its register samples): walking +0x2C from any of 0xD8DE, 0xD904,
0xD96D, 0xD973 gives 0xD8DE -> 0xD973 -> 0xD971 -> 0xD968 -> 0xD96D -> 0xD904 -> 0xD8DE.

| Node | Kind (+0x36) | Cell id (+0x34) | Endpoint +0x14 | Endpoint +0x16 | Point store: list on graph+0xA8, count, first = last | +0x10 | +0x2C | Tag +0x30 | Position (box minimum) |
|---|---|---|---|---|---|---|---|---|---|
| 0xD8DE | 2 | 0xFFFF | 0x91CD | 0x91B6 | 1 record, 0x269E0 | 3 | 0xD973 | 0x58CE | 3595.5, 429.7 |
| 0xD973 | 2 | 0xFFFF | 0x91A4 | 0x91C4 | 1, 0x269F4 | 2 | 0xD971 | 0x58CA | 3604.0, 456.4 |
| 0xD971 | 2 | 0xFFFF | 0x91B6 | 0x91C3 | 1, 0x269F2 | 2 | 0xD968 | 0x58CE | 3583.1, 441.2 |
| 0xD968 | 2 | 0xFFFF | 0x91C0 | 0x91C2 | 1, 0x269E9 | 2 | 0xD96D | 0x58D0 | 3577.8, 436.8 |
| 0xD96D | 1 | 0xFFFF | 0x91B6 | 0x91C0 | 1, 0x269EE | 2 | 0xD904 | 0x58CD | 3591.4, 436.8 |
| 0xD904 | 1 | 0xFFFF | 0x91C0 | 0x9156 | 1, 0x269DC | 2 | 0xD8DE | 0x58CD | 3579.7, 424.5 |

Every member's +0x28 reads 0x0FFFFFFF; those are the hung loop's own writes (it rewrites each visited node's neighbour
links), not the state that started it. All six lie in or beside cell 65,535 = column 292, row 127
(x 3583.7..3615.7, y 430.5..459.2; about plot (146, 63) [I: two cells per plot]).

**The 16-bit fields against the array sizes:** endpoint ids at +0x14 / +0x16: none at or above 37,328 (all 55,683
nodes); no live endpoint with a count above 4; node links: none at or above 55,683; endpoint and node free lists sound
(head 0x0FFFFFFF, live = size: 37,328 and 55,683, no node with bit 31 of +0x2C). **The cell id is the field that does
not fit**: 80,640 cells need 17 bits. Cell 0xFFFF holds count 10, first 0xD904, last 0xD8DE; walking back from its last
node is the ring. Cell 0xFFFE (the one below it, row 126) has the count word 0x880710B0 (the loop decrements it every
step: at least 2.0 billion steps in the 25 minutes) and first node 0xD904 as well. Only 8 nodes carry the id 0xFFFF; 0xD904
is the one node on two lists.

**The hung call** [C, registers and stack; I that 0xD929 is the weld's node: it is the index saved on the stack, and its
links fit]: the weld 0x77C1C0 (return address 0x77C254) refiles node 0xD929 (kind 0, tag 0x27, id 0xFFFE, previous link
none, next 0xD904: it was the first node of cell 0xFFFE, which now starts at 0xD904) and calls the erase 0x5B1F10 on cell 0xFFFE to take it
out (`ebp` = cell 0xFFFE: 0x3E1E3FF4 = cell array 0x3E064024 + 0xFFFE x 24). The erase walks backwards from the stop
node's previous link; 0xD904's +0x2C is 0xD8DE, a node of cell 0xFFFF's ring, not 0xD929, so it never meets the end
it looks for.

### 2.4 The code path [C: behaviour read from the image in the dump; I: the order of events that built this ring]

- **The weld 0x77C1C0 refiles a node** before welding its ends (`crashes-huge-load.md` 2): it first stores the 8 bytes
  it is given into the node's +0x30..+0x37 (tag, cell id, kind; site 0x77C1F3). **If that cell id is not 0xFFFF** it takes
  the node out of that cell's list (the erase 0x5B1F10, call at 0x77C24F) and sets the id to 0xFFFF (0x77C259); **if it
  is 0xFFFF it touches no list**. Then it takes the cell from the node's point number (point count >> 1), its middle
  point, walked with the point iterator 0x84D250 (column x rows + row, cut to 16 bits; *corrected 2026-11-18 by 0542,
  section 3.2, from the code and from 35,259 of the 35,261 bake nodes of four dumps; was "from the node's first point"*),
  stores that id (16-bit store at 0x77C369) and puts the node at the front of that cell's list with the list insert
  0x751BC0 (call at 0x77C39C; the only callee of the weld that writes a node's +0x28 / +0x2C).
- **The erase 0x5B1F10** (on a cell list; arguments: the first node to remove and the node to stop before): when the
  first node is the list's first, the list's first becomes the stop node; it then walks from the stop node's previous
  node (the list's last when the stop is none) backwards along +0x2C, unlinks each node (its neighbours' links rewritten,
  a node with bit 31 of +0x2C set returned to the pool), decrements the count, and stops when it reaches the first
  node's previous node. Nothing bounds the walk or checks for "none" before the next record is read: **a ring makes it
  loop for ever (the hang, 0x5B1F80-0x5B202A); a chain that reaches "none" first makes it read record 0x0FFFFFFF, i.e.
  node array + 0x7FFFFFC8: the fault at 0x5B1FB7**.
- **How a list breaks**: a node whose filing (middle) point lies in cell 65,535 gets the id 0xFFFF there and stays linked in that
  cell's list. When the weld runs on it again (each split, merge or weld of that node), it reads the id as "not filed",
  skips the erase and inserts the node at the front of its cell's list while its old neighbours, and that cell's first
  and last words, still name it. Inserted again into cell 65,535 (the node did not move), the front node's next leads
  on through the old list back to itself: a ring. Inserted into another cell (0xFFFE here), two lists share nodes, and the
  next erase or insert in either rewrites the other's links. Either way a later erase walks into a ring or off the end
  [C for each step; I for which sequence built this particular ring: the dump shows the result only].
- **This needs no double free and leaves the free lists sound**, which is what 0534 saw. It is a second fault of the same
  graph, independent of section 1's [C].

### 2.5 The other row-4 failures [C registers, I cause]

From the DLL's minidumps (`crash-dumps.txt`; they hold no heap, so the cell cannot be read):
- **0x5B1FB7, twice**: #13397 fix run 3 (c5h-i, 2026-11-16 22:12, pid 31676, EXE base 0x00A70000) and T1450 (2026-11-17
  07:05, pid 70120, base 0x00DF0000): both with the walk's index 0x0FFFFFFF and the offset 0x7FFFFFC8, the erase's
  "off the end" outcome of 2.4 [C registers; I that cell 65,535 broke the list]. pid 70120's list object 0x3E1A9FF4 ends
  in ...FF4 like the hang's cell 0xFFFE (page + 0x24 + 0xFFFE x 24), which fits the same cell [I].
- **0x77C6DA, T1100 run 3** (2026-11-16 19:44, pid 54376, base 0x008F0000): the weld's handle walk took the value
  0x1FFFFFFF, a never-filled endpoint slot (`crashes-huge-load.md` 2, way 2). The cell id does not explain it: no link
  from a broken cell list to an endpoint's count was found [I]. **Its root cause stays open** (D185).

### 2.6 Which maps it hits [C grid sizes, I map sizes]

Grids are two cells per plot each way (`grid-dims.txt`, the full 32-bit dumps on F:; plots = extent / 64 [I]):

| Dump | Grid | Cells | Map (plots) |
|---|---|---|---|
| 0317 (huge save class) | 256 x 160 | 40,960 | 128 x 80 |
| 0339 | 112 x 72 | 8,064 | 56 x 36 |
| 4203 crash-box dumps (2026-11-15) | 360 x 188 | **67,680** | 180 x 94 |
| 9074 hang; 0491 (c5h-h, 2026-11-17) | 360 x 224 | **80,640** | 180 x 112 |

More than 65,535 cells means **16,384 plots or more** (4 x plots > 65,535). VP's largest standard size, Huge
128 x 80 = 10,240 plots, is far below, so **the huge save's crashes (section 1, `crashes-huge-load.md` 8) are not this
fault**, and no standard map size can meet it [C]. It needs an oversize map such as 9073's stress map (20,160 plots).
There it fires only when a node in cell 65,535 is welded twice: one cell of the map, near plot (146, 63) on 9074's map.
Whether farm lines cross that cell changes with the turn and with the layout's `rand()` (P6, `terrain-decals.md` 3.1),
which fits 9074's pattern of some saves clean and T1100 failing 2 of 3 loads [I]. The cells from 65,536 up wrap
consistently (list and id agree): they only mix far-apart nodes in one list [I: harmless]. The product runs the same
EXE code, so the product on such a map has the same fault [C].

### 2.7 Would 0527's guard see it? [C against `farm-builder-guard.md` 4]

**No.** F2 checks the endpoint free list and counts above 4; in this state the endpoint list is sound (head none,
37,328 live = size, no count above 4). A node free-list walk would pass too (head none, 55,683 live = size, no bit 31).
What sees it is a check of the cell lists, and only cell 65,535 needs one (it is the only cell whose id cannot be told from
"none"). **Proposals for the MC (not code here; D185, the fix lies in EXE code, so the routes are the Architect's):**
1. **Skip the builder on oversize grids**: the guard reads the grid's two words +0x84 and +0x88 (data reads, at each
   builder call) and does not call the builder when their product is above 65,535. Such maps then get no farm fields,
   deterministically, and the farm chain never welds a node in cell 65,535. Cheapest and preventive; a visual loss only on
   maps of 16,384 plots or more [I that nothing else needs the fields there, as 0528 10.3 says for skipping the builder].
2. **Check cell 65,535's list after each builder call** (walk +0x28 from +0x08 for at most count + 1 steps, end at
   +0x0C, +0x2C mirrored, no member on another list), tripping as F2 does. Detects only after a call; the ring and the
   fault can come inside the call that breaks the list [I], as F2's limit (a) says for the double free.

### 2.8 Does 0528's ServerFarmFields option remove the path?

**Mostly, not entirely** [C for the call chain, I for the result]. With the option on the builder is never called, so
the farm chain's welds (the ones that built this ring: every node at cell 65,535 here is kind 1 or 2, the farm-line
kinds [I]) do not run. But the terrain bake still inserts its own lines through 0x70FE80 -> 0x49CED0 -> the weld
(`../05-graphics-composition/farm-fields-layout.md` 10.2 on branch `epic-0528-farm-fields`), and the weld's refile is the
faulty path. On this save no kind-0 node (12,240 of them, the bake's lines [I: the kind byte's meaning was not read])
touches cell 65,535, so with the option on that cell would stay empty [I]; on another oversize map a bake line through
cell 65,535 that is welded twice would break it the same way [I]. *(2026-11-18, 0542: answered in section 3. Kind 0 is
the bake's [C, 0x70FE80 passes kind 0]; 5,578-5,579 of the 12,109-12,240 kind-0 nodes are the bake's own pieces, the rest
are farm splits of them; a bake piece is welded a second time only by a split, which with the option on needs another
coastline crossing it, never seen [C code, data of four dumps].)* **To confirm**: 0528's acceptance on a save of 16,384
plots or more (9074's T1100 or T1650) with the option on, then cell 65,535's list read after the load (a dump through
`dump_arrays.sh` and `analyse_graph.py`, or a live peek).

### 2.9 Not verified (2026-11-18)

- The order of weld calls that built this ring (2.4): the dump shows only the result. A breakpoint is impossible (1.2).
- That the two 0x5B1FB7 crashes broke on cell 65,535: their minidumps hold no heap (2.5).
- The cause of the 0x77C6DA crash of T1100 run 3 (2.5): open.
- Map sizes from the grid extent (2.6): one plot = 64 units in x was inferred, not read.
- Whether the terrain bake's lines are ever welded twice in cell 65,535 (2.8). *Answered 2026-11-18 by 0542 (section 3).*

## 3. The terrain bake's inserts into the graph (ticket 0542, the Architect's M4 and section 6.2, 2026-11-18)

The question of `../07-architecture-decisions/architect-2026-11-18-farm-graph-routes.md` 2 (a), 4 (M4) and 6.2, with the
MC's additions (i) to (iii): what the terrain bake does to the layout graph 0xBA2228, which stays in the EXE with
ServerFarmFields on, and whether it can break a cell list on an oversize map.

### 3.1 Method

- **Static, no game run.** `cdb -z` on a scratch copy of the instances' EXE (`04cfdc71...`, ImageBase 0x400000; copy on
  `F:/c5-64bit-work/0542/`), read with `tools/harness/evidence/0542/static_read.sh` and `compact.sh`; the listings stay in
  scratch (D83), only addresses and behaviour below. Direct-call reach with 0526's `shell/evidence/0526/callreach.py`.
  Functions read: CreateTerrain 0x709B50 around the bake, 0x59CB60, 0x70FE80, 0x49CED0, the hit test 0x4072C0, the split
  0x5A9C90, the creator 0x554DB0, the node allocator 0x546970 and its copy 0x75D940, the weld 0x77C1C0, the point
  iterator 0x84D250, the point appends 0x524730 / 0x72FF90, 0x436C20, the re-lay 0x6E27D0, the A33 listener 0x79D040, the
  strip 0x55D0C0 (its list walk), the tracer set-up 0x5B5640 and step 0x49F790 with its test 0x585490, and the seed start
  in the layout 0x6016F0.
- **Dumps, read only** (`dump_arrays.sh`, `bake_cells.py` -> `bake-cells.txt`): the graph arrays of four full dumps of
  the stock EXE on oversize maps: 4203's two crash-box dumps (c5h-a pid 71660, seed 2, about turn 347; c5h-b pid 28900,
  seed 1, turn 506; 2026-11-15; 180 x 94 plots, 67,680 cells), 9074's T1100 run-1 hang (0538's copy) and 0491's c5h-h
  hang (pid 70244, 2026-11-17 10:34; both 180 x 112, 80,640 cells). Geometry of rule 2 by `cell_reach.py` ->
  `cell-reach.txt`.

### 3.2 What the bake inserts, and how [C]

- **The lines are the coastlines.** CreateTerrain builds the coastline curves (0x59BBC0, `asset-pipeline.md` 4.1 step 3),
  resamples them into polylines (0x566AA0: a point list and, per polyline, its start, length and a "closed" byte), moves
  them into layout units (x minus the terrain origin, y times 0.866 minus the origin) and hands each to 0x59CB60 on
  0xBA20D0 (call at 0x709FBF). Then it calls the re-lay 0x6E27D0 once (0x709FD3).
- **0x59CB60, one polyline**: takes the next number of the layout's line counter (0xBA2224 +0) as the polyline's **tag**,
  cuts it into chunks of about n / (n >> 4) points (at least 16; a closed polyline at least 2 chunks; a remainder under
  24 points joins the last chunk), and inserts each chunk with 0x70FE80. Consecutive chunks share their joint point: a
  chunk starts at the junction the previous one ended on, and the last chunk of a closed polyline ends on the first
  chunk's start junction.
- **0x70FE80** wraps the chunk's x into the graph's range (a whole number k of world widths) and calls 0x49CED0 with a
  record **{tag, cell id 0xFFFF, kind 0}**: every bake piece is **kind 0** (the kind byte of the record is zeroed in
  0x70FE80). It returns the junctions at the chunk's two ends.
- **0x49CED0, one chunk**: walks the chunk's segments. For each segment it calls the hit test 0x4072C0, sorts the hits
  along the segment and drops every hit on an edge of the **same tag** (so a coastline never cuts its own pieces), every
  hit repeating the previous hit's tag, and every hit with the tag 0xFFFF. It handles the first remaining hit, then tests
  the rest of the segment again from that point:
  - **a crossing (type 1)**: the split 0x5A9C90 on the edge it hit gives a junction J. If the hit lies within 1e-5
    (squared 1e-10) of that edge's first or last point, J is that end's junction and **nothing is split**; otherwise the
    edge is cut in two at the hit (the edge keeps its first part, a new edge of the same tag and kind gets the rest) and
    **both are welded**, the old edge with its own record (tag and current cell id). Then: J full (4 handles) while the
    current start junction is full too: the chunk ends, nothing more made. J holding exactly **two kind-0 handles** (a
    kind-0 edge was split): **the chunk ends there and the piece up to J is not made**. Otherwise the piece from the last
    junction to J is made (creator 0x554DB0), welded with the bake's record, and the chunk goes on; it stops after that
    piece when J holds 3 handles or exactly one kind-0 handle (it met another polyline's end).
  - **the wrap seam (types 2 and 4)**: a new junction at the seam, the piece up to it made and welded, k raised or
    lowered by one, and the chunk goes on from the seam.
  - **at the chunk's end** the last piece is made, from the last junction to the chunk's end junction (new when none was
    given), with the previous hit point put in front (0x72FF90). If that piece holds **exactly one point** and its kind
    byte is not 0, it is deleted at once (**0x727420 at 0x49D61D**, the one delete call of the bake); otherwise it is
    welded with the bake's record.
- **The hit test 0x4072C0** loops over every live edge whose box meets the segment's box grown by 20, and reports a
  crossing for any edge, of any kind, whose segment the bake's segment crosses (tolerance 1e-4), ends on (1e-5), or ends
  within 0.01 of (a snap). Its distance stops (2.4, 9.6) and its refusal apply only when the line being inserted and the
  edge have the same non-zero kind; **the bake's kind is 0, so the bake is never distance-tested and the hit test never
  refuses it** (`farm-fields-layout.md` 3.4 describes these stops for the farm lines).
- **The weld files a node by its middle point** (point number count >> 1, through the iterator 0x84D250), not its first
  point; section 2.4 is corrected. The data agree: 35,259 of the 35,261 kind-0 nodes in the four dumps carry the id of
  the cell their middle point lies in (the two others are a split's halves caught in mid-weld by 9074's hang: 0xD929 and
  its new half 0xD982, below). Farm nodes fit in 93 % (their ends move later through the weld's end writes without a
  refile).

### 3.3 Answers to the ticket's three questions

**(1) Which inputs make the bake reach the weld's refile and the delete** [C for the code; data for the dumps]:
- **The weld** runs once on every piece the bake makes (a first filing: the record's cell id is 0xFFFF, so no list is
  touched before the insert). It **refiles an already filed node** only inside the split, and the split runs only when a
  bake segment crosses or touches the inside of a live edge of **another tag**: another coastline, or a farm line if the
  bake ever ran after farms (it does not: CreateTerrain runs the bake once per load, before the queue-B farm events).
  The split refiles that edge, makes its second half, and the bake's chunk then ends at once (two kind-0 handles at J).
- **In the four dumps no coastline ever met another**: no junction holds kind-0 handles of two tags; every bake
  junction holds exactly two kind-0 handles, and there are exactly as many bake junctions as bake nodes, so every bake
  polyline is a closed ring of pieces (175 polylines on the 180 x 94 map, 327 on the 180 x 112 map). So **the bake's
  refile path did not run on these maps** [C, data]. Whether it can run on another map is a question of whether two
  jittered coastlines cross (a one-plot strait, a lake beside the sea) [I: not seen].
- **The delete 0x727420 at 0x49D61D** needs (a) a chunk's last piece of exactly one point: the piece's polyline points
  (at least two) and the hit point put in front must all be exactly equal, since both point appends drop a point only
  when it equals the single point already held (0x524730, 0x72FF90); and (b) a non-zero kind byte in the new record.
  **The creator does not write the kind**: the allocator copies a 56-byte template whose last word is an uninitialised
  stack slot of the creator (0x75D940 copies all of it; the creator then sets only the cell id to 0xFFFF and +0x37 to
  0), so the byte read at 0x49D610 is stack residue, typically from the last hit test's frame [C for the code; I for its
  value]. No kind-0 node of fewer than two points is in the four dumps.
- **If it fires, it cannot break a cell list** [C]: the piece was never filed (its id is 0xFFFF, so the delete unlinks
  nothing), it is not in the bake's result list, and the delete removes its two handles and frees a junction left empty.
  Its two junction ids differ (the previous junction and the chunk's end junction), so the closed-loop double free of
  section 1 cannot follow. Direct calls from every other callee of 0x49CED0 do not reach the delete (`callreach.py`,
  12 starts, depth 15).

**(2) A bake line through cell 65,535 on a map of 16,384 plots or more** [C for the path; data for the counts]:
- **Cell 65,535 holds no bake node in any of the four dumps**: no kind-0 node is filed there, none has a point in it,
  none has a box meeting it (`bake-cells.txt`). The 4203 map's cell 65,535 is column 348, row 111 (x 5375.5..5407.5,
  y 491.1..520.0): empty, count 0, no node of any kind near it. 9074's map (column 292, row 127): 14 kind-0 nodes filed
  within two cells (9 in 0491's dump), none in it.
- **Exposure, corrected**: the bake's own pieces are 2,468 on the 180 x 94 map (the same count in both 4203 dumps) and
  5,578-5,579 on the 180 x 112 map; they lie in **2,463 of 67,680 cells (3.6 %)** and **5,505-5,506 of 80,640 cells
  (6.8 %)**. A random oversize map therefore has a bake piece filed in cell 65,535 with a chance of the order of 4-7 %
  [I: coasts placed at random against that one cell], not the 14 % of the Architect's 6.2 (12,240 counted every kind-0 node, including 6,661 farm splits of bake pieces).
- **Such a piece is welded a second time only by a split.** With ServerFarmFields on, only by another coastline
  crossing it during the same bake, never seen (above). With the option off, by **any farm line that crosses it**: farm
  lines stop on coastlines by splitting them, 2,431-6,661 times per dump (the kind-0 nodes above the bake's own range),
  and **9074's hang was caught in exactly such a refile**: the hung weld's node 0xD929 is a coastline piece (kind 0,
  tag 0x27) that a farm line had just split (its new half 0xD982, tag 0x27, still unfiled with id 0xFFFF); its erase in
  cell 0xFFFE ran into the ring that farm nodes had made in cell 0xFFFF (2.3). The second weld of a piece filed in cell
  65,535 then breaks that list exactly as 2.4 describes (the old record says "none", so no erase, then a second insert).
- So with the option on the bake's own welds are one per piece, and the cell breaks only if two coastlines cross inside
  it [C path, I that it never happens]; with the option off a bake piece in that cell is at the mercy of the first farm
  line that ends on it.

**(3) The track re-lay 0x6E27D0 with ServerFarmFields on** [C]:
- **Still reached, from both callers.** CreateTerrain calls it once per load after the bake (0x709FD3); every cell the
  bake's welds filed into is dirty then (2,463 / 5,506 cells on these maps). The A33 listener 0x79D040 marks the cells
  under the event's rectangle dirty (0x504B10) and calls it; A33 comes from the farm manager's record removal 0x421280,
  which the option does not touch (`farm-fields-layout.md` 1.1, 1.2: every later B59 on a plot that already holds a farm
  record, and farm removal). 0x6E27D0 walks every cell by a 32-bit index and calls the strip 0x55D0C0 on each dirty one.
- **With which data**: the dirty cells' lists hold only bake pieces (two points or more, filed once, never deleted), and
  the field-margin mask bit 0x10 that the strip requires on every point is written only for fields the farm manager
  receives as type-3 records, none with the option on [I: the server does not post fields to the EXE]. So every piece
  fails the mask test and the strip adds nothing.
- **What could still go wrong there** [C]: the strip's first pass walks a cell's list by the next links until "none", with
  no bound: a ring in a list makes it loop for ever (a hang in the load's re-lay or in an A33). A node with no points
  passes the mask test and starts the runaway write of `crashes-huge-load.md` 9.1. With the option on, a 0-point node
  needs a delete of a filed node (the bake never deletes one) and a ring needs a second weld in cell 65,535 (only a
  coastline crossing), so neither arises from the bake on the maps read.

### 3.4 The MC's additions

**(i) Does a bake insert re-weld an already filed bake node (a crossing, a shared end)?** [C]
- **A crossing: yes, by the code**, when a coastline crosses or touches the inside of a piece of another coastline: the
  split refiles that piece and makes its second half, and the crossing chunk ends there. **Not seen on any of the four
  maps**: coastlines are disjoint closed rings (3.3 (1)).
- **A shared end: no.** A hit within 1e-5 of another edge's end returns that end's junction without a split; the new
  piece is made and welded up to it, and the weld's end walk writes the junction's point into the other pieces' ends
  without refiling them. The joint between consecutive chunks of one coastline is a start point, which the hit test
  skips, and same-tag hits are dropped anyway.

**(ii) Can a step of the farm chain create a point outside the calling plot's rectangle?** [C for each step]
- **Points traced: no.** The tracer's set-up 0x5B5640 stores the rectangle; each step's point is kept only if the test
  0x585490 finds it strictly inside the rectangle and the field bounds and off mask bit 0x08; a step that fails ends the
  line. Hit and split points lie on such a step's segment, which lies inside the (convex) rectangle.
- **Three small exceptions**: (a) an **offset line (seed kind 1) starts 0.65 units beside its parent's segment** (seed
  point + 0.65 x normal in 0x6016F0) with no rectangle test, so its first point can lie up to 0.65 outside; (b) a **snap**
  puts a hit up to 0.01 from the step's end, on the other edge; (c) on a wrapping map a line crossing the seam continues
  with its x shifted by one world width (the type 2 / 4 hits), so its points lie at the map's other x edge (the same
  ground modulo the width). A continuation (kind 2) starts at an existing junction, which may lie outside, but creates no
  point there. Merges, reversals, the weld's end writes and the prune create no new position.
- **Rule 2's radius holds for farm nodes** (`cell-reach.txt`): with the rectangle grown by 0.66, the plots whose call
  can put a point into a 32 x 28.7 (or 28.89) cell all lie within **hex distance 2** of the plot nearest the cell's centre,
  for every placement of the plot lattice against the cell grid. A node is filed by one of its own points (3.2), and a
  farm node's points all come from calls whose rectangle met them, so with those 19 plots skipped no farm node can have
  a point in cell 65,535, and none can be filed there.
- **The condition rule 2 needs, beyond the Architect's text**: a split refiles the **halves of an existing node by their
  middle points**, and those points may have been made by another call. For farm nodes that call is a neighbour inside
  the radius, so the bound stands. For **coastline pieces it does not**: a piece of up to 72.6 units (the largest bake
  piece in the four dumps; median 40-44, a plot is 64 wide) with a point in the cell can be split by a farm call up to
  **hex distance 4** away (61 plots), whose halves are then filed into the cell, and a second split in the same call
  breaks the list before the next count read. On the two maps read no bake piece has a point in cell 65,535 or a box
  meeting it, so radius 2 is exact there.
- **Proposal for the MC (data reads only, D4)**: at load, after the bake and before the first builder call, the hook
  reads the kind-0 nodes once and checks whether any has a point in cell 65,535 (or a box meeting it). None: rule 2 with
  radius 2 (19 plots). Some: radius 4 (61 plots), or 0538's proposal 1 for that map. On a wrapping map the distance wraps
  in x, and a cell within one rectangle (65 units) of the seam also counts the plots on the far edge.

**(iii) The 4203 dumps' cell 65,535 by node kind** [C, data]: **empty in both** (c5h-a about turn 347, c5h-b turn 506;
180 x 94 map, 67,680 cells): count 0, first and last none, no node of any kind with id 0xFFFF, a point in the cell or a
box meeting it. Nodes on the map: c5h-a 4,899 kind 0 / 14,321 kind 1 / 14,378 kind 2; c5h-b 6,013 / 22,251 / 22,311.
For comparison, **0491's hang dump shows the second shape of 2.4**: cell 65,535's list (count 11) walks 12 nodes whose
ids are 0xFF1F-0xFF21 (rows 127-129 of column 291, beside it), with previous links that do not mirror: its nodes were refiled into
other cells while cell 65,535 still names them [C, data].

### 3.5 For the MC

- **The A33 listener swap (Architect 2 (a)) is not needed for the bake** [I from C]: with the option on, the re-lay walks
  lists of coastline pieces that all fail the mask test, and only a list already broken (a ring) would make it hang; the
  swap would also leave CreateTerrain's own call. Its only use would be as a guard against a ring made elsewhere, which
  the per-load graph line (Architect 2 (b), 0538's proposal 2) detects instead. Proposal, not code.
- **Rule 2 (Architect 6.1) can be built with the load-time check of 3.4 (ii)**; without it, rule 2 is exact only on
  maps where no coastline passes through cell 65,535 (true for the 180 x 94 and 180 x 112 maps read).
- Not corrected here (other owners' files): `crashes-huge-load.md` 2 ("it (re)files node N in the grid (the cell from
  the node's first point") and the Architect's 6.1 / 6.2 text name the first point; the weld uses the middle point
  (3.2). The bound of 6.1 is unchanged by it for farm nodes.

### 3.6 Not verified (2026-11-18)

- That two coastlines never cross on any map (only four dumps of two maps read); a crossing would refile a bake piece
  once per crossing.
- The value of the stale kind byte at 0x49D610 and whether a one-point last piece ever occurs (no breakpoint can count
  it: 1.2); harmless to the lists either way (3.3 (1)).
- That the server's fields never reach the farm manager as type-3 records with the option on (the mask bit 0x10 of 3.3
  (3)); from `farm-fields-layout.md` 10.3 and 14.1, not read here.
- The bake's own pieces were told apart by node index (below the first farm node: the bake runs on an empty graph with
  no free records) [I]; the same count in both 4203 dumps (2,468) supports it.
- Rule 2's geometry assumes the plot lattice (64 x 55.43, odd rows shifted half a plot) and two cells per plot; the
  sweep covers every offset between the lattice and the cell grid but not a different cell size.

## 4. `ServerFarmFields = 1` for the first beta: both EXE builds on the release layout (ticket 0637, D223 K1, 2026-11-21)

Ticket 0637 measures the known fix of row K1 of `../06-tooling-and-workflows/known-crash-causes.md` (the server lays out
the farm fields, the EXE's builder 0x748FC0 is never called; epic 0528, `../05-graphics-composition/farm-fields-layout.md`
14) on the first beta's layout and on both EXE builds. It flips no default: the default and the release option set are
the owner's decision (the MC asks with this result). No address of build 26577625 is written here (D202).

### 4.0 Results in one screen (2026-11-21)

- **The fix works on both EXE builds** on the first beta's layout and option set: load, 5 AI turns, the game's own save
  reloaded, then 1 turn; 0 crashes, 0 boxes, 0 NO PLAY; the EXE's farm builder is never called; the graph holds only the
  terrain bake's 754 nodes (4.2, 4.3). With the option on: 9 huge loads on 26577625 and 2 on 4390913, all clean (4.10).
- **Update 2026-11-21 (ticket 0655, D230/D231)**: with the farm-end post of `farm-builder-guard.md` 13 the option keeps
  the farm buildings: the EXE makes them itself, at the centre of each farm plot (both EXE builds, [L]). The line below
  holds for builds without the post (before 0655, or with `farm_end_post=0`).
- **Its cost is larger than the docs say: the farm buildings are not drawn at all** with the option on (not "at the
  centre of the plot"); the option alone does it (4.4). The fields have the server's own layout, cover more of each farm,
  and use the same textures. This is what argues against shipping it on as it stands (4.5).
- **Frame rate: no difference** in three quiet on/off pairs (on 197.5, off 196.2 fps; +1.3, standard error 7.0). EXE
  private memory is 14 MB lower with the option, the server's 113 MB higher (4.9).
- **Without the option**, the guard's effect on the K1 rate is unmeasured: 22 guarded huge loads, 0 crashes, 0 trips (4.8).
- **The server could draw the farm buildings** in about 8-14 h [I] (4.7).
- Found on the way: a NO PLAY without popup or report on the server's cities hold (4.6, ticket 0641).

### 4.1 Set-up

- **Layout**: the release driver's stage layout of ticket 0626's rehearsal take, `x64-1b87dd18-pa4c1c84` (pin x64-0.1b
  1b87dd185 of `deps/dll.lock`; server.exe `0b5f9d95...`, proxy `59061751...`, compositor `f8f44aa2...`, all built from
  this repository at a4c1c84, the 0626 branch's merge of main after 0616; hashes checked against the layout's
  SHA256.txt), copied to `C:/c5-64bit-work/0637/layout/`. Main at 7c78efb9e differs from a4c1c84 in no farm-field or farm
  guard file (only 0624's options boot, the fog's model programs and tests) [C, `git diff --stat`]. 0626's TEST
  installer was not built at 02:30, so the layout was laid as the take's own stage lays it [C, 0606's run (a) route]:
  `research/02-exe-internals/evidence/4967/lay.py prep` (SHA-256 backup of the standard files and VP64Options.ini,
  `provision.py set-proxy` = the D28 layout, the compositor with its shipped ini and manifest), `lay.py helper on`
  (`vp64helper on`: config.ini's D33 keys, `ServerRenderer = 1`), then the release's option set written into
  VP64Options.ini as the installer's i3 step does (0626's rehearsal descriptor, the last 0.1b candidate's set):
  `[Renderer]` ServerUnits 1, ServerCities 1, ServerLandmarks 1, ServerArc 0, ServerPath 0; `[Performance]`
  AiUpdateTickMs 0, AiSkipVisualDrain 1, AiDrainUnordered 1, HiddenEmbarks 1, GameEventsOneCrossing 0; plus the arm's
  `ServerFarmFields`. Drivers (scratch, not committed): `C:/c5-64bit-work/0637/sh/run_arm.sh` (prep, play, reload,
  restore), `set_options.py`, `look.py` (pictures from the compositor's back buffer, memory, the frame window).
- **Save**: copies of `C5H-VP8P-HUGE_0350` (`c499d3c6...`, turn 350, 373 farm plots: 0528's huge save) under the name
  `C5H-0637-<label>`, removed at the restore; the reload loads a copy of the newest autosave after the turns.
- **Mods form** (cp, vp, eui), Goldberg instances only, the no-focus launcher, frame-capped (30 fps) unless marked.

### 4.2 EXE build 4390913: load, 5 turns, the game's save, reload (item 1) [L, 2026-11-21 02:40-02:50 EST]

c5h-f (Goldberg, F:, PE stamp 0x546CD0A8 checked), the layout and option set of 4.1 with `ServerFarmFields = 1`,
frame-capped, MC-B's grant 02:34. Raw: `C:/c5-64bit-work/0637/run-c5h-f/` (logs per launch, lay.json, drive json),
summaries by `C:/c5-64bit-work/0637/sh/summary.py`.

| Launch | What | Map | Turns | Stop | Box | Builder listener calls (skipped-server / built) | Graph lines, clean (nodes = endpoints) | Server layout |
|---|---|---|---|---|---|---|---|---|
| F-ON 02:40 | copy of the huge save, turn 350 | in game 02:42:47 | 5 AI turns, 350 -> 355 (turn waits 14-19 s) | rc 0, no orphan | none | 1,263 (1,200 in the load burst) / 0 | 11, 11 (754) | 373 farm plots -> 142 groups, 5,077 fields, 2,885 tracks, 810 ms on the worker |
| F-ON-RL 02:45 | copy of the game's own autosave of F-ON (turn 355) | in game 02:48:28 | 1, 355 -> 356 | rc 0, no orphan | none | 1,227 / 0 | 5, 5 (754) | 374 farm plots -> 142 groups, 5,056 fields, 2,890 tracks, 904 ms |

- The proxy boot: `boot: exe build: build 4390913 (...): known and supported: the 64-bit path starts`; `options:
  [Renderer] ServerFarmFields = 1 (from the file)`; `server.cfg gets render_farmfields=1 render_farmfields_keep_tap=0`;
  `VPSHELL_FARM_BUILDER=0 set in the EXE's environment`.
- **The builder-not-called line**: since ticket 0535 the farm guard's owner is the proxy, so the lines are in
  vpproxy.log, not shell_composite.log (that log says "the proxy owns the guard in this process"): `farm_guard: proxy
  keys: farm_guard=1 (ini), farm_builder=0 (env), composite loaded: the builder is never called (the server lays out the
  farm fields)` and `farm_guard: farm_builder=0: the EXE's farm builder is not called from now on`; every burst line
  `builder calls N (skipped N), checks 0: none run ... not called for farm_builder=0 N`; the in-game line `builder calls
  through the guard 0`. The node swap ran at 0 nodes / 0 endpoints live, in time.
- Graph: 256 x 160 = 40,960 cells, 754 nodes and 754 endpoints live, none freed, every cell list sound, in every line of
  both launches: the terrain bake's lines only, as on oldpc (0528 14.7). The double free's path (section 1) cannot run.
- LOUD: 0 in vpproxy.log, render.log and shell_composite.log; server.log 200 (F-ON) and 79 (F-ON-RL) lines, all the
  stale Lua instance guard of ticket 0576 (K8 of known-crash-causes.md: nil + LOUD, the DLL not called), unrelated to the
  option. FALLBACK 0. NO PLAY 0.
- Restore by hash 02:50 (`lay.py restore` ok, `provision.py verify --quick` ok); the instance's earlier `VP64` run-output
  folder, moved aside for the prep, put back; save copies removed; claim released 02:50.

### 4.3 EXE build 26577625: load, 5 turns, the game's save, reload (item 1) [L, 2026-11-21 10:56-11:11 EST]

c5h-new-a (Goldberg, F:, build 26577625), the same layout and option set (4.1), MC-B's slot of 10:54 (the planned 05:25
window did not run: the account's usage limit and the instance held by another ticket). Launch N-ON1 was a timing launch
(no frame cap) because it also carried the pictures and the frame window of 4.4; the reload was frame-capped. Raw:
`C:/c5-64bit-work/0637/run-c5h-new-a/`.

| Launch | What | Turns | Stop | Box | Builder listener calls (skipped-server / built) | Graph lines, clean (nodes) | Server layout |
|---|---|---|---|---|---|---|---|
| N-ON1 10:59 | copy of the huge save, turn 350 | 5, 350 -> 355 | rc 0, no orphan | none | 1,263 / 0 | 10, 10 (754) | 373 farm plots -> 142 groups, 5,077 fields, 2,885 tracks, 922 ms on the worker |
| N-ON1-RL 11:07 | copy of the game's own autosave of N-ON1 (turn 355) | 1, 355 -> 356 | rc 0, no orphan | none | 1,227 / 0 | 5, 5 (754) | 374 farm plots -> 142 groups, 5,056 fields, 2,890 tracks, 798 ms |

- The proxy's boot names build 26577625 "known and supported: the 64-bit path starts" without a developer switch; the
  farm guard's 13 byte patterns match ("Steam build 26577625 (Firaxis' 2026-11-19 update, no CEG)"); the option's three
  lines and `farm_guard: farm_builder=0: the EXE's farm builder is not called from now on` as on 4390913 (4.2). So the
  skip works on the new build: the same counts as on 4390913 (the same save gives the same 1,200-call load burst and
  the same 754-node bake graph on both builds).
- LOUD lines, none from the farm path: server.log's 0576 stale-instance guard (200 and 89); one per launch from the
  server's unit and model renderers (N-ON1: `units: UnitRefused: unit (1, 14692) UNIT_GREAT_WAR_INFANTRY: BadInput: no
  pose program for unit ... figure 6: not drawn`; N-ON1-RL: `models: tap frame refused (torn, ackResult -3 ...)`),
  reported to MC-B as findings outside this ticket.

### 4.4 What the player sees: the picture pairs, option 0 against 1 (item 3) [L, 2026-11-21 11:02-11:31 EST, build 26577625]

Pictures (never in git, COMMON 27) in `F:/c5-64bit-work/0637/pictures/`, recorded in the Visual Checks feed (issue 0637;
the ON entries carry the OFF picture as their reference, same save and camera, and the side-by-side figure as their
comparison). Taken by `look.py` from the compositor's presented back buffer (the whole frame with the interface, E9), on
the full system as 4.1 lays it, ACCEPTANCE-labelled (D112); popups were checked first (0 rounds needed, D114); the
active player's units asleep; camera by `UI.LookAt` and posted wheel ticks, the same sequence in both arms; 2554 x 1411.
Views from the save itself: the farm region = the visible farm plot with the most farms around it outside the city
(plot 108,22, 6 farms within 1 plot), close (+3 wheel ticks) and far (-8 ticks); the city =
the active player's city with the most farms within 2 plots (Xochicalco, plot 93,32, 10 farms), +5 ticks (the clamped
closest zoom). A unit info panel and a plot tooltip (the cursor at the centre after the wheel) show in both arms alike.

| View | Figure (off left, on right) | Pixels differing by more than 16 | What differs |
|---|---|---|---|
| farm region close | `N-ON1-26577625-pair-region-close.png` | 28.4 % | **The farm buildings are gone with the option on**: off shows five or more barn and silo models on the farm plots; on shows none. The field patchwork differs in layout (the server's port, larger fields, every farm plot covered); colours and textures match. |
| farm region far | `N-ON1-26577625-pair-region-far.png` | 9.6 % | At this distance the two patchworks read alike; the field pattern and the missing buildings are hard to tell apart by eye. |
| city with farms | `N-ON1-26577625-pair-city.png` | 30.3 % | The fields around the city differ in layout, and with the option on more plots carry fields (stock leaves some farms bare, 0528 2.1); the city, its buildings, the units and the railroads near the city are alike. |

- **Correction (2026-11-21, 0637): with `ServerFarmFields = 1` the farm buildings are not drawn at all; they do not
  "stay at the centre of their plot"** (was: OPTIONS.md, `../05-graphics-composition/farm-fields-layout.md` 10.1, 10.3,
  14.1, the Architect's switch note and section 4.5 item 1 here, all from the code reading, never looked at in a picture:
  0528's A/B pictures named the point for the owner without measuring it, 14.7 stage 4). A DIAGNOSTIC third arm,
  `ServerFarmFields = 1` with `ServerLandmarks = 0` (the EXE draws its own landmarks; `N-ON3-Xon-LM0-26577625-on-*.png`,
  labelled DIAGNOSTIC in the feed), shows no farm buildings either, so the option alone removes them, not the server's
  landmark renderer [L]. Mechanism [I, from `../05-graphics-composition/cities-and-landmarks.md` 2.6]: the farm manager
  creates its building instances (placer 0x4BE160) only from its own 0x90-byte records, and those come from the
  builder's posts; with the builder skipped no record and no building exists. To confirm [C]: the writers of the farm
  manager's records (whether anything but the builder's type 3-6 posts creates one).
- **The railroads differ between any two sessions**: the three sessions of this save (on, off, on with ServerLandmarks 0)
  show three different rail curves in the farm region, so the route curves are random per load (the route builder's
  own `rand()`), not changed by the option [L, three sessions; I that the route builder is the cause].
- Not taken: the same pairs on build 4390913 (one start there, used for item 1). The mechanism above is the EXE's farm
  manager, which is the same code path on both builds [I].

### 4.5 What else changes with the option on, and whether anything argues against shipping it (item 5) [C from the sources cited; L where they are]

Read only: `docs/OPTIONS.md` (`[Renderer] ServerFarmFields`), `../05-graphics-composition/farm-fields-layout.md` 0, 10.3,
13, 14 (epic 0528), this file 2.8 and 3, `farm-builder-guard.md`, `../05-graphics-composition/city-renderer.md` 3.5,
`../07-architecture-decisions/architect-2026-11-18-cities-landmarks-switch.md` 1-2, epic 0528's closing review.

**Known differences a player can see**
1. **The farm buildings (barn and silo models) are not drawn at all** [L, 4.4; corrected 2026-11-21 by 0637 after the
   pictures; was "stay at the centre of their plot instead of moving onto a field's edge", from the code reading of
   `farm-fields-layout.md` 10.1, 10.3 (1), 14.1 and OPTIONS.md, never looked at]. Every farm of the map loses its
   building. A server-side placement of the farm buildings is possible and not built (4.7).
2. **The field patterns differ from stock's** by design: the server's layout is a port written from the knowledge
   base's description (D83), seeded per plot by the plots' static seed (mapSeed reads 0 live, 14.7 item 7), so the
   fields are the same on every load of a save where stock's change per session (about 3 % between sessions, 14.2) and
   its own `rand()` stream (section 8 there) is not reproduced; it leaves no farm bare (stock leaves some farms bare,
   2.1 there) and its fields are about 15 % larger at the median (13.1, 14.7 stage 4). Style and tint come from the same
   `farm_types.xml` textures. Not pixel-faithful (D66): a look the owner accepts or not (the pairs of 4.4).
3. **Trees under fields** are cut by the server's own field footprints (14.2) by the low-corner rule, the best of four
   tried against one stock session, not read from the EXE's rasteriser [I]; edge cells differ from stock's.
4. **Farm tracks** are the server's own strips (13.2); the EXE's track records are 0 with the option on (14.8) [L].

**Known differences a player does not see**
5. Load time and memory: on oldpc the huge load's builder burst is 10.16 s shorter [-10.63, -9.69] and the EXE's
   private memory 13.53 MB lower [-16.15, -10.91] with the option on (14.7 stage 2, grade B) [L]; the server's layout
   costs 1.2-1.3 s on its worker, off the render thread.
6. Game state: identical with and without the option (D164 identity pair, 362 and 434 keys, 14.7 stage 3) [L]: the
   layout is a picture only; saves are unchanged and exchangeable.
7. In play: a farm built, pillaged, repaired or removed is laid out again within the same turn (6 s steps, 14.7 item 5)
   [L], also for AI farms during autoplay (stage 3).

**Conditions and open items**
8. **It acts only with `ServerRenderer = 1` and an accepted compositor.** With `ServerRenderer = 0` or a refused
   compositor the option does nothing and the EXE's builder runs with the stock crash exposure (14.3); the first beta
   ships `ServerRenderer = 1` through `vp64helper on`, and a refused compositor ends in D125's NO PLAY report, not in play
   (K16, K17 of known-crash-causes.md), so a player on the 64-bit path always has it when the option is 1 [C].
9. **On an EXE build whose patterns the compositor refuses** the guard is off, the builder runs, and the server still
   draws its own fields (14.1 last item): the crash risk is stock's. On 26577625 the guard's patterns match
   (`new-exe-build-26577625.md` C.4.4) [L]; the skip itself on that build is this ticket's 4.2.
10. **Patch off** (`mode=off`): the proxy installs neither the guard nor the skip (known-crash-causes.md fact 2); the option
    changes nothing there.
11. **Not excluded by 0528** (14.8): a call of the track strip builder 0x55D0C0 that makes nothing, through CreateTerrain's
    re-lay or the A33 listener on the terrain bake's own cells [I]; 0542 (section 3.3 (3) here) read that the strip adds
    nothing there because the mask bit 0x10 is never written with the option on [C], and only a ring made elsewhere
    could make the re-lay loop. Oversize maps (16,384 plots or more): the bake's pieces are welded once; a ring in cell
    65,535 then needs two coastlines crossing in that cell, never seen (3.3) [I].
12. **The weld walk crash 0x77C6DA of 9074's T1100 run 3** (2.5) stays without a root cause (D185); with the option on
    the weld runs only for the bake's coastline pieces, once each.
13. Not tried with the option on: multiplayer and hot seat (never tried at all, `docs/INSTALL.md`); the 43-civ variant
    on the batch-5 / 0.1b pins (0528's M2 ran 0334's m43 build of batch 2, a COMMON 18 exception); the modpack form.

**Does anything argue against shipping it on?** **Yes, item 1** (corrected 2026-11-21 11:40 after the pictures of 4.4;
the draft of 02:40 said "nothing found"): with the option on, every farm loses its building, a visible loss the docs did
not name. The crash side holds on both builds (4.2, 4.3). The rest is either the designed picture difference (2-4), a
gain (5), or a condition the first beta's configuration already meets (8, 9). So the choice for the owner is between (a) the
fix on, with farms without buildings (and other field patterns, D66) until the server draws the farm buildings (4.7),
and (b) the option off, with the guard's unmeasured mitigation (4.8) and the K1 crash. D186 left the default and the look
to the owner; the Architect recommended 1 where `ServerRenderer = 1` (0528's closing review), not knowing about the buildings.
The frame rate does not argue either way: three quiet pairs show no difference (4.9: +1.3 fps, standard error 7.0).

### 4.6 A NO PLAY without popup or report, seen in a diagnostic arm (not the farm path) [L, 2026-11-21 11:33-11:39 EST]

Launch N-ON4-Xoff-LM0 (c5h-new-a, build 26577625; DIAGNOSTIC arm: `ServerFarmFields = 0`, `ServerLandmarks = 0`, the
rest of the first beta's set, `ServerCities = 1` among it; the workstation under five other games, the load took 4 min)
loaded the huge save. The builder ran through the guard (1,200 calls, every check sound). At 191.7 s the server ended
itself in play:
- render.log: `models: cities: no accepted tap frame while the bit was set (at a drawn frame) ...` (189.3 s), then
  `models: cities: the readiness or the tap frames were lost in play for 2371 ms, beyond render_models_hold_ms=2000: the
  server cannot draw them (D118)`; server-report.txt ends `result: FAILED: models: cities: ...`.
- vpproxy.log: `call ICvGame1::Update() ... failed: PeerDied (server pid ...)`, then `FATAL: the server failed a call
  (on_server_death=exit): the process ends with exit code 3`.
- **No popup and no report reached the player**: the dialog watcher saw no box (exit code 3, `boxes_seen 0`), no
  vp64helper process was left, and no `VP64/reports` folder was written in the user folder. D118 asks for an error popup
  and a diagnostic report on every such end (as ticket 0600 gave the renderer's init failure). The cause of the end is
  the cities hold timeout (`render_models_hold_ms`, `server/src/render/cities/srv_models_mode.cpp`) on a heavily loaded
  machine; why the D118 popup path was not taken was not read [I: the server's mode writer ends the process without the
  popup step, so the proxy sees only a dead peer]. Filed by the MC as ticket 0641 (2026-11-21). Raw:
  `C:/c5-64bit-work/0637/run-c5h-new-a/N-ON4-Xoff-LM0/` and the run folder's server-report.txt (moved by
  `lay.py restore` into `C:/c5-64bit-work/0637/run-c5h-new-a/vp64-runs/`).

### 4.7 What it would take for the server to draw the farm buildings (reading only, MC #9198 (c)) [C for the sources cited, I for the plan and the sizes]

**Where the buildings come from today** [C, `../05-graphics-composition/cities-and-landmarks.md` 2.6 and its table row
FARM, `../05-graphics-composition/farm-fields-layout.md` 4.2]: the farm manager of the landmark sim keeps one 0x90-byte
farm record per farm plot, made by its own B59 listener 0x7040D0 (continent, wheat flag, state, owner, and the building's
row slot = `rand() % rows` of the farm's era); the era listener 0x5307A0 writes the era level (1-4); the placer
0x4BE160 puts the building at the plot centre, moves it to the nearest point of the record's field curves, turns it by
the direction field 0x44DA00 plus a random quarter turn, and sets the terrain height; 0x603000 re-creates a plot's
instance. The rows are the FARM rows of `ArtDefine_Landmarks`: 6 generic farm buildings (Medieval_Farm1-3 for the early
eras, Modern_Farm1-3 Industrial), up to 3 per era level. **Why none is drawn with the option on** [I]: the record exists
without the builder (it is the B59 listener's), so the building instance must be created on the field path, which only
the builder's type-3 records start (the curves are filled only from them, 4.2 there); with no type-3 record no instance
is made. To confirm [C]: the callers of 0x603000 and 0x4BE160 (a direct-call read as 0526's `callreach.py`), or the
landmark instance count of a FARM row in one load with the option on and off.
*[Superseded 2026-11-21 by `../05-graphics-composition/cities-and-landmarks.md` 2.6.1 (ticket 9078, [C], build 4390913):
the building is made only by the farm-end listener 0x603000, which only the builder's type-5 post reaches; the record
waits in the manager's pending list and the field records play no part. Ticket 0653's step 0 (stock pictures: pillaged,
fog, the rows the huge save's eras use) is in `../05-graphics-composition/farm-fields-layout.md` 16.1.]*

**Route S-F (the server draws them)**, at home in the farm-field producer plus the landmark draw layer of epic 0561:
1. Per farm plot (the producer's plot table already lists them: 373 on the huge save): the era level from the owner's
   era (the feed), the row slot by the server's own seeded choice among the era's rows (D194: plausible variants are
   enough), the pillaged state from the plot record.
2. Placement: the plot centre moved to the nearest point of the server's own field outlines for that plot (the producer
   has the polygons), the facing from the direction field already ported in the producer
   (`farm-fields-layout.md` 13.1), a seeded quarter turn, the terrain height from the server's height field. This is
   0338's D2f package (2-4 h in its table, `cities-and-landmarks.md`).
3. Drawing: the model library of 4872 reads the `ArtDefine_Landmarks` rows from the install [I: that the FARM rows load,
   since no manager's list names them]; the draw layer (4873, material 4) draws instances from a scene source; 4875 built
   the `exe` source and an `own` source left empty "until 4880/4881" (`../05-graphics-composition/city-renderer.md`). The
   farm buildings would be the first `own` instances, drawn beside the `exe` ones in the same frame with the model fog
   and shadows the landmark layer applies. With `ServerLandmarks = 0` (the EXE draws the landmarks) the server would
   still have to draw these, so this path must not depend on the landmarks mode bit.
4. Pillaged farms: the EXE's `ART_DEF_VEFFECT_FARM_PILLAGED_SMOKE_01` effect, started by the farm manager on state 8,
   would need route S's effects stage A (the EXE's particles at the server's command, D211) or stay missing.
5. Tests: the scene bundle (rule A: a new default drawing needs a bundle member or a render_config key), offline pictures
   against a stock capture of farm buildings, one live pair.

Size [I]: 8-14 h (placement 2-4, the `own` source and its mixing with `exe` 3-5, era and row 1-2, tests and pictures
2-3); the pillaged smoke not included. **An alternative to weigh (route E-F)** [I, not read]: hand the EXE's farm manager
the server's fields as type-3 records (data written into the 4 MB queue, a means D136 allows) so that the EXE makes, snaps
and draws the buildings and stamps the tree and margin bits itself; the builder and its layout graph (the crash) would
still never run. Risks: the margin bit 0x10 brings the track strip builder 0x55D0C0 back to work (4.5 item 11, 0537's
fault), and the EXE would make field decals that the server drops (`render_farmfields_keep_tap=0`). A question for the
Architect if the MC wants a second route.

### 4.8 What the candidate has WITHOUT the option: the guard's measured effect (MC #9198 (d)) [L for the counts, I for the reading]

- **No number exists for the guard's effect on the crash rate.** Before the guard (to 2026-11-17): product 3 crashes in
  57 huge loads (5.3 %, 95 % interval 1.1-14.6 %, `crashes-huge-load.md` 8.5). Since the guard merged (0527,
  2026-11-18 05:06; owned by the proxy since 0535), the huge loads with the guard on and the option off written down are
  0527's 13 (`farm-builder-guard.md` 6), 0528's 7 off-arm loads on oldpc (`farm-fields-layout.md` 14.7: stage 1 one, stage 2 five, stage 4 one) and this ticket's
  2 (N-OFF1, N-ON4-Xoff-LM0): **22 loads, 0 crashes**, a rate below 12.7 % (one-sided 95 %); at the old 5.3 % a run of 0 in
  22 has probability 0.30. So the data cannot tell the guarded rate from the unguarded one.
- **The guard has never had anything to stop**: every check of those loads was sound (0527: 15,600 checks; 0528's off
  arm and this ticket's: 1,200 per load, `checks 1200: all sound`), and no trip is written down anywhere in the
  knowledge base. It checks after each builder call, and the damage and the fault can fall inside one call
  (`farm-builder-guard.md` 8), so even a trip would not prove a crash avoided.
- Rule 2 (oversize grids, 4855) is measured: 14 oversize 43-civ loads, 0 crashes, against stock's 9 of 14 (K2); it does
  not touch the K1 double free on standard map sizes.
- So without the option the first beta carries the K1 crash at the pre-guard estimate (1.1-14.6 % of huge loads, point
  5.3 %) reduced by an unknown factor; with the option, no crash in any load so far (0528: 21 huge on oldpc, 14 oversize;
  this ticket: 4.10).

### 4.9 Frame rate and memory, option on against off (item 4) [L, 2026-11-21: one busy pair, DIAGNOSTIC and void; three quiet pairs]

**Busy workstation, one pair** (2026-11-21 11:03 and 11:15 EST, c5h-new-a, build 26577625, timing launches (no frame
cap), the load's camera after the three picture views, a 45 s window of `tools/testbed/jobs/fps_idle.py --measure` in
three 15 s blocks; `C:/c5-64bit-work/0637/run-c5h-new-a/look-N-*/look.json`; per-second lines compared by
`C:/c5-64bit-work/0637/sh/winstats.py`):

| | on (N-ON1) | off (N-OFF1) |
|---|---|---|
| fps (blocks) | 21.84 (24.0, 21.5, 20.0) | 38.44 (36.9, 38.9, 39.6) |
| the EXE's frame time, compositor stats (mean of per-second means; p99) | 46.4 ms (61.7) | 26.9 ms (35.2) |
| compositor waits (acq, gpu syncwait) | 0, 0 | 0, 0 |
| server render thread busy per second (host `rt_cpu_ms`) | 987 ms | 786 ms |
| server submit per frame (host `submit_ms`) | 28.5 ms | 16.0 ms |
| server terrain / forest CPU per frame | 2.0 / 2.4 ms | 1.3 / 1.3 ms |
| model copy per frame | 5.8 ms | 3.2 ms |
| EXE private / server private, 10 s after the map (MB) | 1,470 / 2,094 | 1,496 / 1,965 |

Reading [I]: every part of the server ran 1.5-2x slower in the on window, the farm-unrelated ones (terrain, forest,
models, units) as much as the rest, and the compositor waited for nothing in either arm, so the EXE's longer frames are
not the server's farm draw holding it; the two windows were 12 minutes apart on a machine whose other games changed
load (impl-0635's on c5h-new-b and three more). One pair on a busy machine decides nothing.

**The machine in the two windows** (diskwatch `%USERPROFILE%/c5-ops/diskwatch/diskwatch-20261121.jsonl`, 5 s records,
read by `C:/c5-64bit-work/0637/sh/disklat.py`; the rule of `../06-tooling-and-workflows/d-drive-load.md` 13: a measured
window with a record above 50 ms does not count) [L]:

| Window | F: (the instance's drive) read / write latency, max | F: records above 50 ms | C: max | Machine CPU |
|---|---|---|---|---|
| on, 11:03:13-11:03:58 | 15.5 / 3.3 ms | 0 | 0.5 ms | 75-88 % |
| off, 11:15:40-11:16:25 | 125 / 146 ms | 3 (does not count) | 1.6 ms | 41-55 % |

So the off window does not count under the F: rule, and the pair is void. It also shows that the slow arm was not the one
on the stalled drive: during the on window F: answered within 16 ms while the machine's CPU stood at 75-88 %, against
41-55 % in the off window. The lower frame rate of the on arm matches the busier CPU, not the drive [I: one pair; the CPU
load came from the other games, which this window cannot separate from the arm]. (Written 2026-11-21 13:0x; MC #9266
named F: as the cause of the pair, which holds for the off window only.)

**The quiet pairs: no frame-rate difference** [L, 2026-11-21 17:18-17:45 EST, MC-B's quiet window (no other game, build,
link or test on the workstation), c5h-new-a's copy on **C:** (ticket 0646), build 26577625, the stage layout and option set
of 4.1, timing launches (no frame cap), the huge save copy at the load's own camera (no picture views), a 45 s window of
`fps_idle.py --measure` 10 s after the map; each arm in its own quietlock hold (`quiet_session.sh`); table by
`C:/c5-64bit-work/0637/sh/quiet_report.py`; raw `C:/c5-64bit-work/0637/run-c5h-new-a-q/`; the window 2554 x 1411, RTX of
the workstation as on the day]:

| Arm | Window (EST) | fps (15 s blocks) | EXE frame ms (compositor) | compositor wait ms | server render thread busy ms/s | server submit ms | EXE / server private MB | C: read / write max ms (records > 50) | CPU mean / max % |
|---|---|---|---|---|---|---|---|---|---|
| Q1 off | 17:21:52-17:22:37 | 192.08 (201.3, 195.2, 179.7) | 5.4 | 0.00 | 982 | 7.0 | 1,496 / 1,911 | 0.2 / 0.1 (0) | 33 / 44 |
| Q2 on | 17:25:59-17:26:45 | 199.79 (199.6, 193.9, 205.9) | 5.0 | 0.00 | 981 | 7.3 | 1,487 / 2,079 | 0.4 / 0.3 (0) | 26 / 29 |
| Q3 on | 17:30:23-17:31:08 | 206.94 (207.3, 204.6, 208.8) | 4.9 | 0.00 | 984 | 7.4 | 1,476 / 2,029 | 0.4 / 0.1 (0) | 25 / 28 |
| Q4 off | 17:35:10-17:35:56 | 194.17 (195.4, 194.1, 192.9) | 5.2 | 0.00 | 985 | 6.9 | 1,495 / 1,978 | 0.3 / 0.1 (0) | 26 / 31 |
| Q5 off | 17:39:41-17:40:26 | 202.20 (202.4, 202.2, 202.0) | 5.0 | 0.00 | 983 | 6.9 | 1,497 / 1,948 | 0.2 / 0.1 (0) | 27 / 35 |
| Q6 on | 17:44:03-17:44:48 | 185.71 (187.5, 184.1, 185.6) | 5.4 | 0.00 | 982 | 7.6 | 1,482 / 2,068 | 0.5 / 0.3 (0) | 28 / 33 |

- **Frame rate**: on 197.48 fps (n 3), off 196.15 fps (n 3); on - off **+1.33 fps, standard error 6.96** (Welch): no
  difference; with three pairs the test could see about 20 fps (10 %) at 95 % [I: 2.8 standard errors, t with 4 degrees of
  freedom]. Every window is valid by d-drive-load.md 13 (C: under 1 ms, no record above 50 ms); quietlock's disturbance
  record per hold: total CPU mean 20-23 %, max 31-45 % (the game and its server themselves).
- **Where the time goes**: the EXE's frame is 4.9-5.4 ms in every arm, and the compositor waited 0 ms for the server in
  every arm (acq and GPU sync wait 0), so the frame rate is the EXE's own. The server's submit per frame is 7.43 ms with the
  option on against 6.93 off (+0.50 ms: its own 7,962 farm decals against the EXE's farm decals passed through the tap);
  its render thread is busy about 982 ms of each second in both arms. That cost stays on the server's side and does not
  reach the EXE's frame [L for the numbers, I for the reading].
- **Memory** (10 s after the map, n 3 each): EXE private 1,482 MB on against 1,496 off (**-14 MB**, as oldpc's -13.5 MB in
  `farm-fields-layout.md` 14.7); server private 2,059 MB on against 1,946 off (**+113 MB**, the server's own fields,
  footprint and producer [I]; the server's memory still grows after the map, so a later sample would differ).
- The busy pair above (21.8 against 38.4 fps) was the machine's load, not the option.
- Grade by `docs/briefs/PERFORMANCE.md`: three arms each on the workstation in a scheduled quiet window (COMMON 23), not
  the testbed; enough to say "no difference above about 10 %", not to put a number on a smaller one.

Memory with the option on in the busy series (frame-capped, N-ON2 and N-S5..N-S9, 10 s after the map): EXE private
1,444-1,451 MB, server private 2,353-2,414 MB (higher than the quiet arms: another procedure and a busy machine).

### 4.10 Huge loads with the option on (item 2) [L, 2026-11-21]

| Build | Loads with `ServerFarmFields = 1` | Crashes / boxes / NO PLAY | Builder listener calls (all skipped-server) | Graph lines, clean |
|---|---|---|---|---|
| 26577625 (c5h-new-a, 10:56-12:15) | **9**: N-ON1 (+5 turns), N-ON1-RL (the game's save, +1 turn), N-ON2, N-ON3-Xon-LM0 (DIAGNOSTIC: ServerLandmarks 0), N-S5..N-S9 | 0 / 0 / 0 | 10,890, 0 built | 29 of 29, 754 nodes each |
| 4390913 (c5h-f, 02:40-02:50) | 2: F-ON (+5 turns), F-ON-RL (+1 turn) | 0 / 0 / 0 | 2,490, 0 built | 16 of 16 |

The planned two hours shrank to 1 h 20 min (the account's usage limit took the 05:25 window; the slot of 10:55 was shared
with five other games, and loads took 4-9 min). 9 loads in 9 with no crash give a rate below 28 % (one-sided 95 %): the
count adds little by itself; what removes the fault is the mechanism, the same in every load (no builder call, no
node delete, the graph the terrain bake's 754 nodes) [L], as 0528 found on oldpc (21 huge + 14 oversize loads).
