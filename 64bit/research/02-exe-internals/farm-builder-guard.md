# The farm builder guard: the builder's listener word and the free-list check (ticket 0527, 2026-11-18, account B)

Pieces F1 and F2 of the Architect's draft (`research/07-architecture-decisions/architect-2026-11-18-farm-field-layout-ticket.md`,
decision D186): the interim protection against the farm-field layout's double free of `crashes-huge-load.md` section 8,
until the server's own layout (0528) can switch the EXE's builder off. Code: `shell/composite/farm_guard.{h,cpp}` in
`shell_composite.dll`; unit test `shell/tests/farm_guard_test.cpp` and `shell/tests/test_farm_guard.py`. Read on a copy of
the DX11 EXE (`b2ce96ae...`, TimeDateStamp 0x546CD0A8) with `llvm-objdump -d`; nothing of the EXE is in this file but
addresses, offsets and behaviour (D83). Marks: `[C]` read in the code, `[L]` seen live, `[I]` inferred. Addresses are DX11
VAs at ImageBase 0x400000. Times EST.

## 1. How the builder is registered and called [C]

- **Registration.** LoadGameState::Update 0x403170, state 1 -> 2 (`[this+0x10]`), calls 0x6436A0, which reaches GameView
  Startup 0x623170; its Landmark branch calls 0x661D10 on 0x2B735E8 (RoadLayout's eight listeners) and then **0x465600
  with ecx = 0xBA20D0**, which registers four listeners of that object: **0x748FC0 (the farm builder) through 0x8478A0**,
  0x45BDB0 through 0x847900, 0x4C2CA0 through 0x847840, 0x79D040 through 0x847960. Each register function takes a
  pointer to `{?, object, function}` (12 bytes; the object at +4, the function at +8).
- **0x8478A0 (type 59 of the "B" event manager 0x2A10200).** Initialises a spin-lock word 0xCBFC70 once (bit 0 of the
  byte 0xCBC56C says it was done), takes the lock (`InterlockedCompareExchange(0xCBFC70, 1, 0)` through the import slot
  0x9D646C, in a loop until it returns 0), reads the channel index `[0x2A10200]`, takes the listener list at
  `[0x2A103E4 + index * 0x600]`, adds the node with 0x861760, and writes 0 into the lock. 0x847900 uses the same lock and
  the slot +0x1EC (type 60); 0x847960 registers on the 4 MB "A" manager 0x1A0E400 (its own lock 0xCBA9F0, slot +0x114).
- **The handler table.** One channel's table starts at mgr + 8 and has 8-byte entries {handler, listener list} per type,
  0x600 bytes per channel (192 types); type 59's entry is at mgr + 0x1E0 + index * 0x600. The queue-B pump 0x785AF0
  passes `mgr + 8 + [mgr] * 0x600` to the dispatcher 0x63ABA0, which calls `entry.handler(record + 0x44, entry.list)`
  for each record. Every type's handler is 0x63E530, a jump to the **listener walk 0x8668E0**.
- **The listener list** (0x861760 and 0x8668E0): a free-list array header (free head, live count, data, size, capacity:
  the same five words as the layout graph's arrays), then +0x14 count, +0x18 head index, +0x1C tail index. A node is 20
  bytes: **+0x00 next index, +0x04 previous index [I: from the link call 0x870620's arguments], +0x0C object, +0x10
  function**. The add allocates a node (0x88EC00), copies object and function in, counts it, links it at the tail.
- **The walk 0x8668E0** (cdecl: the event payload, the list): for `count` steps from the head it calls
  `node.function` with **ecx = node.object** and **one stack argument, the payload** (and eax = &node.object), then reads
  the next index from the node array again (the array may move during the call). It takes no lock.
- **The unregister** (GameView shutdown 0x490A10 -> 0x6FA350 -> 0x846BE0 for type 59): under the same lock, walks the
  list and removes the **first node whose object and function both equal** the ones given; when none matches it removes
  nothing and says nothing. The shutdown then sets the word 0xB9DDA0 to 0 (when it was not 0) and `[0xBA20D0 + 0xCAC]`
  to 0.
- **0xB9DDA0** is set to 1 by the city-field init 0x432220 (0x432397), called by the same GameView startup, and back to
  0 by that shutdown: it says "the GameView group is up". The stock EXE never calls the builder while it is 0 (the
  builder's node is registered and removed inside the same startup and shutdown).

## 2. The builder's calling convention [C]

- **0x748FC0 is thiscall with one stack argument and `ret 4`** (its one return at 0x749118); this = 0xBA20D0, the
  argument = the event payload (it reads +0x08, the 16-bit plot x and y at +0x18 / +0x1A and a byte at +0x1C). It
  preserves ebx, ebp, esi, edi and reads neither edx nor eax at entry. A C function `void __fastcall f(void* obj, void*
  edx, void* ev)` has exactly that convention (ecx, a dummy edx, one argument popped by the callee), so the guard calls
  the builder through such a pointer with the same object and argument.
- What it does (behaviour): it tests the payload's flag byte, checks the plot through 0xD91130 (0x42B440) and the
  improvement's art name against `ART_DEF_IMPROVEMENT_FARM`; for a farm it looks the plot up in its map at this+0xCB0
  (0x758590): a new farm is laid out (the layout 0x6016F0 on this+0x154, then 0x4A8990 on this+0xC70 and the tracks
  0x61FD40), a known one updated (0x42B4F0 on this+0xC70); then it posts a record on the 4 MB queue (0x48E810). A
  payload that is not a farm returns at once. So the builder runs for every type-59 event, not only for farms.
- **Its three siblings** (registered beside it, draft 6): 0x45BDB0 (type 60) marks the plot's entry in the same map
  (this+0xCB0, +0x10 = -2: the farm is gone); 0x4C2CA0 copies 0x53 words of its payload into this+8; 0x79D040 (on the 4 MB
  "A" manager, type 33) calls 0x504B10 and 0x6E27D0 on this+0x154 with a rectangle from its payload (the farm tracks'
  pass, 0x6E27D0 = the strips of `terrain-decals.md`'s farm tracks). None of the four reads the node lists of the graph
  directly [C for the listeners' own bodies; their callees were not read].

## 3. What else needs the graph filled (draft 6) [C for the references, I for the use]

- Direct references to the graph object 0xBA2228 (or 0xBA2224 = 0xBA20D0 + 0x154): the GameView startup (0x62327B,
  0x623285: init), the terrain re-init at 0x6685A3 (0xBA2228.0x435870), the terrain bake 0x709FCE (0xBA2224.0x6E27D0, the
  farm tracks over an empty grid in the Landmark-off probe, `world-systems-and-stubs.md`), and the static constructor
  0x9C6FBA. References to 0xBA20D0: the startup 0x62326C, the shutdown 0x6208D6, CreateTerrain 0x709DBC / 0x709FBA
  (0x760920, 0x59CB60), the static destructor 0x9D4130.
- So, read statically: the farm tracks (0x6E27D0, from 0x79D040 and from the bake) walk the layout object, and the
  terrain re-init calls into it; nothing outside the farm chain was found that needs the graph to hold farm fields
  [I]. The other callers of the weld and the delete named in `crashes-huge-load.md` 8.3 (0x49CED0, 0x46F8B0, 0x493D6C,
  0x49D61D, 0x5C8740, 0x6061AF) were not read for this ticket: whether they are reached only from the farm chain stays
  open (F5's question, not F2's).
- What would confirm: with F5's option on (the builder never called), the graph at 0xBA2228 stays empty and the bake,
  the farm tracks' pass and a terrain re-init run without a fault (0528's acceptance names this).

## 4. The guard as built [C]

**F1, the switch point.** At every Present of the composite (the EXE's main thread, the same thread that pumps queue B:
`lockstep-probe.md`), after the composite validated the EXE and once 13 byte patterns match (the registration and its
call, the register function with its lock, list slot and add, the list header the add writes, the type handler's jump,
the walk's node offsets and call, the builder's entry and its `ret 4`, the layout call on this+0x154, the flag 0xB9DDA0's
set and clear, the node delete's free on this+0x94, the free's entry form), the guard:
1. takes the EXE's registration lock 0xCBFC70 itself with one compare-and-swap 0 -> 1 (held by the EXE: nothing this
   Present; bit 0 of 0xCBC56C clear: nothing registered yet);
2. reads type 59's list (channel index below 16, the handler equal to 0x63E530, count <= size <= capacity <= 4096, every
   index below size) and notes the nodes whose object is 0xBA20D0 and whose function is the builder, the guard's hook or
   its inert function;
3. writes: when a node still holds the builder and another holds the hook (a new registration while an earlier one
   could not be removed: section 1's unregister cannot match a swapped node), the hook's node gets the inert function
   (returns at once); each node holding the builder gets the hook; with `farm_guard=0` the hook's node gets the builder
   back. Each write is a compare-and-swap of one aligned word against its expected old value (D4: data only);
4. writes 0 into the lock.

**The hook** (the walk calls it in the builder's place): after a trip it returns; when 0xB9DDA0 reads 0 it returns (a
node left over from a shut-down GameView: the stock EXE has no node then); when the list still holds a node with the
builder itself it returns (this call came through a stale node; the EXE's own node calls the builder in the same walk);
else it calls the builder with the same object and argument, outside any SEH frame (a fault of the EXE's code stays the
EXE's: the proxy's crash filter, D118/D119), and then runs F2.

**F2, the check** (0515's rule, `crashes-huge-load.md` 8.3, on the endpoint array at graph+0x94): an empty graph (size
0) is sound; otherwise the header is plausible (data non-zero, size <= capacity <= 4,000,000), the walk from the head
meets only entries with bit 31 set and ends at 0x0FFFFFFF within as many steps as there are freed entries (more = a
cycle, the twice-freed head's self-link), it meets every freed entry, live + freed = size, and no live entry's count
(low 31 bits of +0x10) is above 4. The first failure: the line `farm_guard: LOUD: the EXE's farm-field layout graph
failed the free-list check after builder call N ...` in `shell_composite.log`, the same text appended to
`farm-guard.log` in the proxy's run folder of this process, and no further builder call in the process (the hook stays
in the node; `farm_guard=0` is ignored after it).

**The record's folder.** The D119 report takes the tail of every `*.log` in the run folder (`error-report-exe-crash.md`
1.5). The composite's log folder is `<player>\Logs` (VPSHELL_OUT_DIR, issue 0148) and the shipped run folder is
`<player>\Logs\VP64\run\<yyyymmdd>-<hhmmss>-<pid>-<n>`, so the guard takes the newest folder of that form whose pid is
its own process's (MC #4990, #4992: no proxy change). None found: `farm-guard.log` beside the composite's log, and a LOUD
line says a report would not carry it.

**Logs.** One line when the patterns match (and where the record would go); one line per swap with the graph's live
node and endpoint counts at that moment (0 = no layout had run yet: the swap was in time); one line per retirement and
restore; one line per burst of builder calls (a load, or farms changed in play), 2 s after its last call:
`farm_guard: load N (grid seq S, ...): builder calls C (skipped K), checks C: all sound | BAD ...; check time ... ms total,
max ... us; endpoints after the last check: live, size, freed, walked`; and for every load whose map came up (the camera
block's grid seq), 10 s after it and 2 s after the last builder call, `farm_guard: in game (grid seq S): builder calls
through the guard ... since the last in-game line ...; the check at this Present: sound | BAD: <rule>; endpoints ...`: the
check run once at that Present (the main thread between frames), so a load without any builder call (a map without
farms) logs its result too, and a bad graph found there trips the guard as after a call. Only a refusal, a bad list and
a trip are LOUD.

**The event log (MC #5073, added 2026-11-18 03:35 for 0528).** Every call of the hook appends one line to
`farm-builder-calls.log` in the same folder as `farm-guard.log` (the run folder): the call's number, the grid seq, what
happened (`built`, `skipped-tripped`, `skipped-noview`, `skipped-stale`), the payload's 16-bit x (+0x18) and y (+0x1A), its
dword +0x08 and byte +0x1C, read under SEH before the call (a data read, no debug register). The file is opened shared
(readable while the game runs), buffered by the CRT and flushed at each load line, in-game line and trip; the load line
counts its event lines. About 50 KB per huge load; the D119 report takes its last 64 KB as it takes every `*.log` there.

**Keys.** `farm_guard=1` (default) in `[composite]` of `shell_composite.ini`; `0` puts the builder's own word back. It acts
on every 64-bit start since ticket 0535 (section 12: the proxy owns it and reads the same keys from the same file); before
0535 it acted only when the composite was loaded (`[Renderer] ServerRenderer = 1`) (corrected 2026-11-18). *Added 2026-11-18 by ticket 4853 (epic 0528 D,
`research/05-graphics-composition/farm-fields-layout.md` 14.1):* `farm_builder=1` (default) | `0`, set to 0 by the proxy
through `VPSHELL_FARM_BUILDER=0` with `VP64Options.ini` `[Renderer] ServerFarmFields = 1` (the server lays out the farm
fields): the hook returns before the builder call (counted `skipServer`, event line `skipped-server`, burst and in-game
lines `not called for farm_builder=0 N`) and stays in the node even with `farm_guard=0`.

**What the check can and cannot catch.** The path to the double free, as epic 0528 read it (MC note #5011, from
`farm-fields-layout.md` 10.4 on branch `epic-0528-farm-fields`, [C for each path, I which fires]): the face walk 0x7A3BC0
has two early exits that leave the face-use count raised, so an isolated closed loop survives the prune and 0x5C86D0;
its polygon is withdrawn by 0x606030 when its centroid is not on a farm plot, which deletes it with the node delete
0x727420 at 0x6061AF: the double free. The delete's call sites are 0x5B8EA4, 0x493D6C, 0x5C8740 and 0x6061AF. All of
them run inside one builder call (the layout 0x6016F0 and its callees), so the check sees the state they leave **after**
the call: the twice-freed entry's self-link (a cycle), a live entry on the chain once the allocator has handed it out,
or live + freed != size. It cannot see (a) damage and fault inside the same call: the allocator hands the twice-freed
junction out again within the layout, the shift of 0x536540 or the weld walk 0x77C1C0 then runs past an entry and the
EXE faults before the builder returns (0515's three crash sites are all inside builder calls); (b) a double free whose
traces are gone by the end of the call (both hand-outs taken and the counts adding up again) [I: the live count stays
one low for the rest of the session, so live + freed = size fails at the next check unless a later free of a live
entry hides it]. So the guard stops the next call after a damaging one; it lowers the crash rate by the share of
double frees that do not fault in their own call, which no run has measured.

## 5. Tests [L offline, 2026-11-18, workstation]

- `farm_guard_test.exe` (fake manager, list, lock, builder and graph; the EXE's register, unregister and walk as section 1
  describes them; the free as `crashes-huge-load.md` 8.3 describes it): **96 checks passed** on the plain build of
  15b12000b's tree (`C:/c5-64bit-work/0527/shell-build`, 2026-11-18 01:53); **114 checks** with the in-game line and the
  event log (e4b532d9, 03:35), plain and under ASan (inside the suite below).
- Final suite run on e4b532d9 (03:37-03:42): plain 47 passed (test_farm_guard, test_config_defaults, test_decal_tap),
  ASan test_farm_guard 5 passed, 0 reports. Trap met on the way: `_wfopen_s` opens a file without sharing, so neither a
  reader nor the report could open the event log while the game holds it; it is opened with `_wfsopen(..., _SH_DENYNO)`.
- `shell/tests/test_farm_guard.py` (5 tests: the unit program and its runner's negative, the 13 patterns against the
  installed EXE on disk, the composite's wiring, the real path in the composite on the stand-in `test_host --fake-exe`:
  `farm_guard: LOUD: REFUSED: pattern 0 ... (fault)`, once, the host runs on, not disabled): **5 passed on the plain build
  and 5 passed under MSVC /fsanitize=address, 0 ASan reports** (`shell/evidence/0527/suites.sh`, 2026-11-18 02:02-02:03).
  With `test_config_defaults.py` (the shipped ini's `farm_guard=1` equals the built-in default) and `test_decal_tap.py`
  in the same plain run: 46 passed (01:57-02:02; the one failure of that run was this suite's own pattern regex, which
  did not allow the fake image's "(fault)", fixed before the rerun above).

**Identity (F1) and P6.** The farm layout calls `rand()` (`terrain-decals.md` 3.1, P6): two loads of one save lay out
different fields even in stock, and a session lays out each farm once, so "the farm records equal with and without the
swap" cannot hold record for record. The identity is shown instead by (a) construction and test: the guard calls the
builder with the same object and argument, and its convention is pattern-checked; (b) live, the farm-field and
farm-track counts and the set of farms with fields, guard on against guard off, judged against the spread between guard-off
sessions (MC #5013) and against 0166's census (huge: 381 of 425 farms with fields, 4,650 stock / 4,737 D33 field
decals). (Ticket wording corrected 2026-11-18 by MC #5013.)

## 6. Live runs [L, 2026-11-18, workstation, Goldberg instance c5h-f]

**Set-up.** `shell/evidence/0527/run_0527.py` (0410's load, 3308's prep and restore, imported unchanged) through
`series.sh`: D33 product path with Overlay on, `[Renderer] ServerRenderer = 1`, pins batch 3 (layout
`x64-4f4daac7-pf8e9f42`: server.exe `ed829bda...`, x64 DLL `2d737bd5...`, proxy `4815ed81...`), the instance's EXE
`04cfdc71...`; the composite of this branch with the shipped `shell_composite.ini` (`farm_guard=1`, or 0 for an off arm);
saves `C5H-0527-HUGE_0350` (a copy of `C5H-VP8P-HUGE_0350`, c499d3c6..., turn 350) and `C5H-0527-TINY_0008` (a copy of
`C5H-0366-TINY_0008`, b2ceb663...). One launch per load, the load screen closed by its own button, 20 s in game, then
`shell/tools/decal_probe.py`'s census, 4101's `plots.lua` and `p7.py`, the harness's stop. Raw: `F:/c5-64bit-work/0527/<tag>/`.

| Tag | Composite | Save, guard | Result | Guard lines |
|---|---|---|---|---|
| t01 (02:04-02:08) | `cc282e79...` (15b12000b) | Tiny, on | in game, no box, stop clean | patterns match; swap of node 4 of 5 (channel 0) with **0 nodes and 0 endpoints live at the swap** (before any layout); the record folder found (`<player>\Logs\VP64\run\20261118-020609-52700-1`); **no builder call**: the save has 0 farms (p7), so no burst line (the reason for the in-game line, below) |
| h01 (02:08-02:13) | `cc282e79...` | huge, on | in game, no box, stop clean | swap at 0 / 0 live; `load 1: builder calls 1200 (skipped 0), checks 1200: all sound; check time 22.71 ms total, max 96 us; endpoints live 9019, size 9087, freed 68, walked 68`; census: 425 farms, **4,724 field decals, 382 farms with fields (357 + 25 pillaged), 43 without (42 + 1)** |

- **The swap is in time** on both loads: the builder's node holds the guard while the graph is still empty, so every
  builder call of the load went through the guard (1,200 on the huge save) [L].
- **The builder runs for type-59 events of farms only on these saves** [L: 0 calls on Tiny with 0 farms; 1,200 on the
  huge save with 425 farms, about 2.8 per farm]. Each call's check cost 19 us on average and at most 96 us: 23 ms per
  huge load against the layout's 18-20 s.
- **Something other than the farm builder writes the graph** [L]: Tiny t0008, 0 farms and 0 builder calls, holds 238
  nodes and 238 endpoints at 0xBA2228 after the load (0410's `layout_peek.py`, none freed). Which writer was not read
  [I: the route layout; 0x4AB360 on 0xBA2228 is the startup that names itself "RoadLayout::GameView::Startup()"]. So the
  guard's check after the builder's calls does not see damage another writer makes until the next builder call; the
  in-game line (section 4, added after t01 in 8aca0ddd's successor) runs the check once per load at a Present as well.

**The series (02:26-03:27), composite `5aa96d1c...` (25a469b, with the in-game line)**, `summary_0527.py` and
`fieldless_0527.py` over `F:/c5-64bit-work/0527/`:

| Load | Guard | Builder calls / checks | Check time per load (max per call) | In-game check | Field decals | Tracks | Farms with / without fields |
|---|---|---|---|---|---|---|---|
| t02 Tiny | on | 0 / 0 (0 farms) | - | sound (238 live, 0 freed) | 0 | 0 | 0 / 0 |
| h01 huge (cc282e79) | on | 1200 / 1200 all sound | 22.7 ms (96 us) | (not in that build) | 4,724 | 2,479 | 382 / 43 |
| h02 | on | 1200 / 1200 all sound | 20.4 ms (131 us) | sound | 4,692 | 2,462 | 381 / 44 |
| h03 | **off** | - | - | sound | 4,654 | 2,429 | 379 / 46 |
| h04 | on | 1200 / 1200 all sound | 38.9 ms (3,446 us) | sound | 4,616 | 2,435 | 381 / 44 |
| h05 | on | 1200 / 1200 all sound | 27.4 ms (1,086 us) | sound | 4,522 | 2,442 | 381 / 44 |
| h06 | on | 1200 / 1200 all sound | 21.5 ms (304 us) | sound | 4,658 | 2,436 | 382 / 43 |
| h07 | on | 1200 / 1200 all sound | 18.9 ms (1,212 us) | sound | 4,745 | 2,445 | 382 / 43 |
| h08 | on | 1200 / 1200 all sound | 37.8 ms (904 us) | sound | 4,751 | 2,501 | 382 / 43 |
| h09 | **off** | - | - | sound | 4,681 | 2,457 | 381 / 44 |
| h10 | on | 1200 / 1200 all sound | 22.9 ms (194 us) | sound | 4,584 | 2,446 | 382 / 43 |
| h11 | on | 1200 / 1200 all sound | 20.3 ms (268 us) | sound | 4,745 | 2,456 | 380 / 45 |
| h12 | on | 1200 / 1200 all sound | 22.4 ms (210 us) | sound | 4,651 | 2,422 | 380 / 45 |
| h13 | on | 1200 / 1200 all sound | 22.7 ms (437 us) | sound | 4,650 | 2,448 | 379 / 46 |

- **14 loads, no crash, no box, every harness stop clean, no LOUD line** (0410's exit 0 each). All 11 guard-on huge loads:
  the swap at 0 nodes / 0 endpoints live, 1,200 builder calls, all 1,200 checks sound, 18.9-38.9 ms of check time per
  load (workstation, not a timing measurement: COMMON 23); endpoints after the load 8,837-9,107 live, 62-89 freed.
- **F1's identity (MC #5013)**: guard on (11 loads) 4,522-4,751 field decals, 2,422-2,501 tracks, 379-382 of 425 farms
  with fields; guard off (2 loads) 4,654-4,681, 2,429-2,457, 379-381; 0166's census of 2026-11-15 (D33, one load) 4,737
  fields and 381 farms with fields. The on range contains the off range and the census; the sets of fieldless farms:
  40 plots fieldless in all 11 guard-on loads (54 in some), 44 in both guard-off loads (46 in some), and 41 in every one
  of the first six loads of either arm: the same farms lack fields with and without the guard, and the plots that
  differ (28,45), (40,54), (42,29), (63,22), (63,23), ... differ between guard-off loads too. Equal within stock's own
  load-to-load spread [L].

**Closing loads (03:43-03:59), composite `e1570140...` (e4b532d9 + the event log of MC #5073)**: t03 Tiny on (in-game
check sound, 238 live), h14 huge **off** (4,573 fields, 2,444 tracks, 382 / 43), h15 on (1,200 calls all sound, 25.5 ms,
4,632 / 2,436, 377 / 48), h16 on (1,200 all sound, 22.3 ms, 4,727 / 2,439, 381 / 44); no crash, no box, no LOUD.
**All 18 loads of the ticket**: 13 huge guard-on (all 15,600 checks sound), 3 huge guard-off, 3 Tiny; guard on 4,522-4,751
field decals, 2,422-2,501 tracks, 377-382 farms with fields; guard off 4,573-4,681, 2,429-2,457, 379-382. The one
guard-on load below the off range (h15, 377 with fields, 48 without) is one load among 13 against 3 off loads; 40
farms are fieldless in all 13 guard-on loads, 42 in all 3 guard-off loads [L].

**The builder's events (MC #5073, for 0528; `farm-builder-calls.log` in the proxy's run folder of each load, copied to
`F:/c5-64bit-work/0527/h15/out/` and `h16/out/`; `events_0527.py`)** [L, huge save, h15 and h16 equal in these counts]:
1,200 calls per load, every one on a distinct plot; **373 with +0x1C = 1, all with +0x08 = 0x27**; 827 with +0x1C = 0 and
+0x08 one of many values (0x34 167, 0x32 114, 0x29 69, 0x41 69, 0x40 66, ...): the builder returns at once for those (its
first test, section 2). The plot is the event's hex x and y (grid x = x + (y - (y & 1)) / 2, wrapped by the map width):
354 of the 425 farms of `plots.lua` got a +0x1C = 1 event, 7 farms got only +0x1C = 0 events and 64 none; 19 of the 373
plots with a +0x1C = 1 event are not farms in `plots.lua` after the load (not read further). Of h15's 48 fieldless farms,
36 had no +0x1C = 1 event; 35 farms without one still show fields (a neighbour group's fields within 48 units, p7's rule).
So a farm without a +0x1C = 1 event is the main share of the bare farms [L for the counts, I for the cause: 0528's
reading, `farm-fields-layout.md` 2.1 on `epic-0528-farm-fields`].

**The D162 journeys' smoke (04:00-04:39)**, `shell/evidence/0527/journeys_0527.py` (hi_journey.py's helpers, product
route only, on this test layout; settings backed up and put back by hash): **release-basic exit 0** (63 steps, 596.5 s;
its two loads, the start save and the in-game reload of HI-4506-SAVED, had 0 builder calls: no farms yet; the reload
logged a second in-game line and no second swap, so that in-process load did not register the builder again [I: one
case, no farm to call it]). **release-large stopped at its step 12** ("plot [50, 50] not under the cursor at its pixel
now, nor after a search by moves", exit 6, 11 steps done) with the guard on (413 builder calls, all sound), and again
with `farm_guard=0` (04:22, the same step and words): the stop is not the guard's. (Corrected by the MC at the merge,
2026-11-18: the cause is c5h-f's `UserSettings.ini` `[InterfaceSettings] AutoUnitCycle = 0`, ticket 0530 [L from logs]:
without the auto-cycle the camera does not move after step 11's attack; not 3308's D33 set as first written here.) the release candidate's own layout passed it on
2026-11-17 (`tools/release/evidence/0509/hi_journey-report.md`). A third run with the guard on (04:31) is in
`F:/c5-64bit-work/0527/journeys-large-g1/`.

## 7. Option or always on (the MC's call at the merge; the implementer's recommendation)

Recommended: **always on in the composite**, with `farm_guard=0` in `shell_composite.ini` as the only switch (a kill switch
for tests and support, as the taps have), and no `VP64Options.ini` key. Reasons: it changes nothing a player sees while
the graph is sound (the builder gets the same calls; census within stock's spread, section 6); its cost is about 20 ms
of CPU per huge load against the layout's 18-20 s; it acts only after the patterns match on the one known EXE build;
and a player could not judge the option. What it did not cover until ticket 0535: a game without the composite
(`ServerRenderer = 0`, or the composite refused) had no guard, because the guard lived in the composite. Since 0535
(2026-11-18) the proxy owns it on every 64-bit start: section 12.

## 8. Not verified

- That the swap happens before the first type-59 dispatch of every load: registration is in LoadGameState state 1 and the
  queue-B flush in state 4, one state per frame [C], so at least two Presents lie between them if the load screen
  presents [I]; each swap logs the graph's counts to show it live.
- Whether F2 stops a crash in time: the damage and the fault can fall in one builder call (one farm group); it lowers the
  rate by an unknown factor (draft 6).

## 9. The graph line (ticket 4854, M3 of the Architect's routes file, 2026-11-18, account B) [C for the code, L where cited]

The Architect's measurement of `../07-architecture-decisions/architect-2026-11-18-farm-graph-routes.md` 2 (b) and 6.1
item 3: one read-only log line about the layout graph at 0xBA2228, so that every test load of every ticket reports the
graph's state (the footprint of `crashes-huge-load.md` 8 and 9 and of `farm-field-double-free.md` 2, without a debugger).
Code: `FgGraphScan`, `FgGraphScanSafe`, `FgGraphLineText` in `shell/composite/farm_guard.cpp`; it never writes and never
trips the guard.

**When.** At the Present (the EXE's main thread, between frames), after every in-game line (once per load, 10 s after the
map came up and 2 s after the last builder call) and after every burst line (2 s after a burst of the listener's calls:
the load's farms, or farms changed in play, which on a 43-civ late game means about once per turn). With `farm_guard` 0 or
1 and `farm_builder` 0 or 1 alike (the option `ServerFarmFields` at 1 or 0): the hook is in the node in every case but
`farm_guard=0` with the builder in use, where only the in-game line runs (no burst lines). The `where` field names it:
`in game, grid seq S` or `after load N, grid seq S`. Not when the guard refused the EXE (patterns) or the composite did
not validate it.

**What it reads** (offsets from the graph; the layout of `farm-field-double-free.md` 2.2 and `crashes-huge-load.md` 2):
the grid +0x84 / +0x88 (cells in x and y) and +0x8C (24-byte cells: +0x04 count, +0x08 first, +0x0C last node); the
node array +0xBC (head, live, data, size, capacity; 56-byte nodes: +0x10 point count, +0x14 / +0x16 endpoint ids, +0x28
next, +0x2C previous link in the cell with bit 31 the freed bit, +0x34 cell id, +0x36 kind); the endpoint array +0x94
(20 bytes: four handles, count word +0x10 with bit 31 freed); the farm-track strip's vertex vector 0xD16AD4 (+4 size,
+8 capacity, `crashes-huge-load.md` 9.1). Plausibility first (cells, node and endpoint sizes and capacities at most
4,000,000, data words non-zero); a fault of any read ends the scan with "NOT COMPLETE" and the counts so far (SEH). Scratch:
one byte and one word per node, grown as needed (55,683 nodes on 9074's T1100 state: 278 KB).

**The line** (one line, `farm_guard: graph (<where>): ...`; `farm_guard: LOUD: graph` when a cell list is not sound, a
listed node has no points or the freed bit, cell 65,535's list is not sound, or a handle of kind 0 names a never-filled
slot), in this order:
1. `grid NX x NY = C cells (within | ABOVE 65,535)`: above 65,535, cell 65,535 reads as "none" (`farm-field-double-free.md` 2).
2. `nodes live L, size S, capacity K, freed bit F (live + freed = | != size)`: whether bit 31 of +0x2C is the node's freed
   marker is checked by this sum on every line [I until a line with deletes shows "="].
3. `endpoints live, size, capacity, freed`; `ids past 65,535: nodes YES|no, endpoints YES|no` (the 16-bit ids at +0x14 /
   +0x16 and the cell id: the Architect's two further ceilings, routes file 1).
4. `cell lists walked W, nodes listed N, lists not sound B (first: cell X: why)`: every cell's list from +0x08 along +0x28,
   each node at most once per list (met again = "a ring"), at most `count` nodes ("more nodes than its count"), each
   node's +0x2C (low 31 bits) equal to the node before it ("links back to"), ending at none after exactly `count` nodes
   with +0x0C naming the last ("count C, walked W", "last word"); an index past the node array ends the walk. A step
   budget of 4 x nodes + 4,096 bounds a damaged graph.
5. `listed with no points P, with the freed bit F` (the strip's n = 0 of `crashes-huge-load.md` 9.1; a freed node left in
   a list, 9.3); `+0x34 not the holding cell's low 16 bits M`; `held by a cell above 65,535 H` (0538 found these cells
   empty: the index is cut before the list and the id alike); `on two lists D`; `live nodes on no list U (naming a cell V)`
   (0538: 14 such nodes in the hang, 7 naming a cell).
6. `cell id 0xFFFF on live nodes by kind 0/1/2/other` (the Architect's 6.1 item 3; 0538: 8 nodes in the hang, kinds 1 and
   2; kind 0 = the terrain bake's lines [I]).
7. `cell 65,535: none (the grid fits 16-bit ids)` or `count N, walked W, sound | NOT SOUND: why, also on another list K`
   (0538's proposal 2: the proof that rule 2 holds and the detector for the bake's lines there).
8. `handles: never-filled slot N (kind 0: K), past the node array P, not named back B`: the handles below each live
   endpoint's count (at most 4): index 0x0FFFFFFF = a never-filled slot (0x77C6DA's case, `farm-field-double-free.md` 2.5;
   kind 0 = the weld's walk takes it as a handle and faults); an index past the node array; a node whose endpoint id at
   that end (bit 28: +0x16, else +0x14) is not this endpoint (the stale handle of `crashes-huge-load.md` 9.2). Then
   `endpoints counting above 4 E` (F2's rule, for the record).
9. `vertex vector size S, capacity C` (0xD16AD8 / 0xD16ADC) and `scan T ms`.

**Tests** [L offline, workstation, 2026-11-18 08:25]: `farm_guard_test.exe` 221 checks (133 before this ticket), section 12
of the program: a sound 4 x 3 grid of 6 nodes and 3 endpoints with every count given exactly; damage that keeps the lists
sound (a listed node with no points, one with the freed bit, a wrong +0x34, never-filled slots of kind 0 and 3, a handle
past the array, a handle not named back); seven broken lists, one fault each, with the exact reason (ring, count high,
count low, index past the array, a back link, the last word, a node on two lists); an oversize grid of 300 x 219 = 65,700
cells with cell 65,535 sound, then 0538's ring (a node in cell 65,535 inserted at its front again) and 0538's second form
(the same node at the front of cell 65,534 too), nodes held by cells 65,537 and 65,600 (one with a wrong id); an empty,
an implausible (capacity, grid) and a faulting graph (the cell array in reserved memory: "NOT COMPLETE", partial counts
and the vertex vector kept); 65,536 nodes and 65,537 endpoints (ids past 65,535: YES); an endpoint past 65,535 whose node
cannot name it and a count above 4; and through the Present, with `farm_builder` 1 and 0: the graph line after the load's
burst line and after the in-game line, none more at the next Present, one after a burst in play, and LOUD (without a trip)
once a listed node loses its points. Suites (`shell/evidence/0528/suites_4854.sh`, 2026-11-18 08:26-08:31): plain
`test_farm_guard.py` + `test_config_defaults.py` 14 passed; under MSVC /fsanitize=address `test_farm_guard.py` 5 passed, 0
ASan report files.

**Live**: the M2 loads of ticket 4854 (9074's oversize saves on oldpc, `farm-fields-layout.md` 14) are the first product
runs that log it; their lines are summarised there.

## 10. Rule 2: oversize grids while the builder runs (ticket 4855, the Architect's 6.1 item 2, MC #5493; 2026-11-18) [C for the code, I where marked]

For games where the EXE's builder still runs (`farm_builder=1`: `ServerFarmFields = 0`, or a game without the composite
once ticket 0535 ports this to the proxy). Cause: `farm-field-double-free.md` 2 (cell 65,535's 16-bit id reads as "none",
the weld's next refile breaks that cell's list). Premise and bound: `../05-graphics-composition/farm-fields-layout.md` 15.
Code: `Rule2` and `FgRule2Geometry` in `shell/composite/farm_guard.cpp`. Data reads under SEH and a skip only (D4); nothing
is written into the EXE's objects.

**The rule, as a port must reproduce it** (in the builder's listener, before each call of the builder, after the guard's
own skips: tripped, no GameView, stale node, `farm_builder=0`):
1. Read the grid's cells in x and y (graph +0x84, +0x88). At most 65,535 cells: call the builder (the rule never acts).
2. (a) Read cell 65,535's count word (cell array graph +0x8C, 24-byte cells, +0x04). Not 0: **no further builder call in
   this process** (this call and every later one skipped, event line `skipped-rule2-stopped`), one LOUD line `farm_guard:
   LOUD: rule 2 (a): cell 65,535 of the farm-field layout grid (NX x NY = C cells, above 65,535) holds N nodes before
   builder call K ...` and the same text appended to `farm-guard.log` (the D119 record, section 4). It is the backstop:
   it fires when the bake filed a piece there, or when (b) failed.
3. Once per load (the first call after the builder's node was swapped, which every load's registration causes, or after
   the grid's size, extent or origin changed) find **the plot P holding cell 65,535**: the cell's centre (column =
   65,535 / NY, row = 65,535 mod NY, column-major; centre = origin + (index + 0.5) x extent / cells) against the plots'
   centres as the EXE's plot rectangle has them (`farm-fields-layout.md` 1.3: the cube coordinates through the hex -> world
   matrix 0xD91030 in row-vector form, x plus the world box's minimum x 0xD90FCC, y times 0.866 plus its minimum y
   0xD90FD0); the nearest centre (the lattice inverted, cube-rounded, then the nearest of the 3 x 3 neighbours). The map
   width W = NX / 2 (checked: |extent x - 64 W| <= 64). Checks: one hex x step must be (64, 0) and one hex y step (32,
   +-55.424) within 0.05; every plot within hex distance 4 of P whose rectangle (+-64, +-55.424) grown by 0.66 meets the
   cell must be within distance 2 of P. A failed check, or a faulting read: every builder call on that grid is skipped
   (`skipped-rule2-nogeo`, 0538's proposal 1), with one LOUD line per grid.
4. **The radius** (MC #5493 from 0542): read the live nodes (bit 31 of +0x2C clear) of kind 0 (+0x36), the bake's
   coastline pieces; if the box of any (+0x18 min x, min y, max x, max y) meets the cell or the cell shifted by +-1 world
   width (graph +0x6C), R = 4, else R = 2.
5. (b) Read the event's plot (hex x at +0x18, y at +0x1A, 16-bit). Its hex distance to P, the x wrapped by W columns
   (min over k of cube(dx + kW, dy)), at most R: skip the call (`skipped-rule2-near`; an unreadable payload is skipped too).
   Else call the builder.

**Logs**: one `farm_guard: rule 2: the grid has NX x NY = C cells (above 65,535): cell 65,535 = column, row, centre, size;
it lies in plot (x, y) (hex x, y; d from its centre), map width W; N plots within hex distance 4 can reach the cell, all
within distance 2; kind-0 nodes K, of them with a box meeting the cell T: radius R ...` per load; the burst line ends
`; rule 2 skipped N` (and `(rule 2 (a) fired ...)`); the in-game line counts `rule 2 skipped N in this process (near,
after (a), no geometry)`; the graph line (section 9) ends `; rule 2: plot (x, y), kind-0 nodes K, touching cell 65,535 T,
radius R; <state>` on every oversize grid with the option at 0 or 1 (state: in force with its radius, not acting
(farm_builder=0), not set up yet, no geometry, stopped by (a)). The graph line's kind-0 count is the live count at that
time: with the builder running it includes farm splits of coastline pieces (0542 3.3 (2)).

**Price**: the skipped plots (19, or 61) get no fields, like the farms stock leaves bare; nothing else changes. On a grid
of at most 65,535 cells (every standard VP size) nothing is read but the two cell counts.

**Tests** [L offline, workstation, 2026-11-18 08:49]: `farm_guard_test.exe` 334 checks (221 before), section 13: the
geometry of three hand-computed maps (9074's 180 x 112: cell 65,535 = column 292, row 127, centre (3599.69, 444.77),
plot (146, 64) = hex (114, 64), 16.34 from its centre, 9 plots reach the cell; a 164 x 100 map whose cell 65,535 is in
the last column beside the seam: plot (163, 68), 6 plots reach it; a 128 x 128 map whose cell 65,535 is the last cell, in
the margin above the map: plot (127, 130), off the map); radius 2 and 4 (a coastline box meeting the cell; on the seam map
a box at the left edge meeting the cell's image across the seam; a freed coastline node not counted); four refusals (no
cell 65,535 at 255 x 257 = 65,535 cells, not a hex lattice, a world width that does not fit, plots past distance 2 reaching
ten-times-high cells); the wrapped hex distance by hand; every plot of each map through the hook: exactly 19 calls skipped
on 9074's map and on the seam map (radius 2), 61 with radius 4, 0 on the edge map, with 25 hand-listed plots of 9074's map,
6 at radius 4 and 21 around the seam (grid x 0 and 1 across it) each skipped or called as computed; grids of 65,535 and
40,960 cells never skipped (no geometry read); (a) with its LOUD line, the record and every later call skipped even after
the cell is emptied; `farm_builder=0` with cell 65,535 not empty: nothing skipped by the rule, the graph line says "not
acting"; no geometry (not a lattice, a faulting matrix read): every call skipped, LOUD once; the radius chosen again at
the next registration (2 -> 4 after a coastline box was added). Suites on the merged branch (9127dc998, 2026-11-18
09:10-09:21): plain `test_farm_guard.py` + `test_config_defaults.py` 14 passed, MSVC ASan `test_farm_guard.py` 5 passed with
0 report files. Trap met on the way: MSVC's x86 ASan does not unpoison a frame that an SEH `__except` in a caller unwinds,
so a later CRT allocation (`_wfsopen`) over that stack area was reported as a stack-buffer-underflow (twice, in the test of
a faulting matrix read); fixed by reading through `SafeCopy` (the fault caught in a leaf frame without stack objects) and
by keeping the graph scan's reason buffer off its frame.

**Live** [L, oldpc job 20261118-090609-9a17, 2026-11-18 09:15-09:26 EST, instance tb1, RTX 2080 with NVIDIA 32.0.16.1088
(held, D175), composite `0c5a4fbd...` (e74a891ca)]: a copy of 9074's T1100 (`TB-4854-T1100`, 968d7ccd...) on the 43-civ
product layout of `../05-graphics-composition/farm-fields-layout.md` 14.9 with `ServerFarmFields` absent (the builder runs
through the guard), 1 AI turn: `rule 2: ... cell 65,535 = column 292, row 127, centre (3599.7, 444.8), 32.00 x 28.70; it lies
in plot (146, 64) (hex 114, 64; 16.7 from its centre), map width 180; 9 plots within hex distance 4 can reach the cell, all
within distance 2; kind-0 nodes 5578, of them with a box meeting the cell 0: radius 2` (the EXE's plot geometry gives the
hand-computed plot of the unit test; 0538's estimate was (146, 63) [I]); the load's 4,533 listener calls: **12 skipped by
rule 2** (hex (113, 65), (112, 66), (115, 62), (114, 64), (114, 65), (113, 66), (116, 62), (116, 63), (115, 64), (115, 65),
(114, 66), (116, 64): each within distance 2 of (114, 64) by hand; 3 of them farm events, art id 0x27: (115, 62), (116, 62),
(116, 63), which get no fields), 4,521 built, all 4,521 checks sound; in play 8 more calls built, none skipped. Every graph
line (5) clean: cell 65,535 count 0 and sound, 0 lists not sound, 0 listed nodes without points or with the freed bit,
0 never-filled handles; nodes 42,486 live of 42,580 with 94 freed bits (live + freed = size: **bit 31 of +0x2C is the
node's freed marker** [L]), endpoints 29,068 live of 29,138, 70 freed. In game, 1 turn played, no box, exit 0. Stock failed
this save 2 of 3 loads (`farm-field-double-free.md` 2.6); one clean load is not a rate (D185), but the mechanism is shown:
nothing was filed in cell 65,535.

## 11. ASan under MSVC x86 and SEH: read EXE memory through SafeCopy (ticket 4855, 2026-11-18) [C, from 4855's commit e74a891ca and log; recorded by the MC from auditor cycle 9, J7]

A fault caught by `__try/__except` (SEH) in a frame that has stack objects leaves that unwound frame's shadow memory poisoned under MSVC's `/fsanitize=address` (x86), and the next AddressSanitizer report in the process is false. 4855 first read rule 2's geometry from EXE memory inside such a frame; the fix reads through `SafeCopy` into a static buffer, so no SEH unwind crosses a frame with stack objects, and the ASan run is clean. Rule for the guard's code and any port of it (ticket 0535): every read of EXE memory that may fault goes through `SafeCopy` (or another frame with no stack objects), never a `__try` around code with locals.

## 12. One owner per process: the proxy (ticket 0535, 2026-11-18, account B) [C for the code, L offline where cited; live in 12.6]

Why: with `[Renderer] ServerRenderer = 0` (the options file's default) or a composite refused by its SHA-256 check, the
composite is not in the process, so sections 4-11 did not apply and such a game had the stock exposure to the farm-field
double free (`../05-graphics-composition/farm-fields-layout.md` 14.3; MC, D185). Code: `proxy/src/proxy_farm_guard.{h,cpp}`
(the owner's frame), `shell/composite/farm_guard.{h,cpp}` (the guard, one source compiled into both modules:
`proxy/build_proxy.py` `FARM_GUARD_SRCS`), `proxy/src/proxy_shell.cpp` `OnBoot` / `OnUnload` (the calls, through
`ProxyShell::g_farmGuardHooks`, which `proxy_farm_guard.cpp` sets at its static initialisation, so that the offline unit
programs that link `proxy_shell.cpp` alone, those of `proxy/tools/test_proxy_rt.py`, need no change). Design on the
thread (#5789), accepted by the MC with conditions (#5795).

### 12.1 Who installs

- **The proxy**, from `ProxyShell::OnBoot` (the mode=on boot, `DllGetGameContext` on the EXE's main thread at the front
  end, before any save loads), **before the composite's `LoadLibrary`**: it attaches the guard (12.2), reads the keys (12.4)
  and starts its watcher thread. The watcher runs every 2 ms on a high-resolution waitable timer: the EXE check once (PE
  time stamp 0x546CD0A8 here, then the 13 patterns of section 4), then section 4's F1 (the swap, retirement, restore under
  the EXE's registration lock), and the burst lines. Before it takes the EXE's lock it reads the list without the lock
  (bounded, SEH) and takes the lock only when a node needs a write. The hook runs where the EXE calls it, as before (the
  main thread's queue-B pump).
- **The composite's copy** (`FarmGuard_OnPresent`) first looks for the owner's page (12.2). Found: it installs nothing,
  reads and writes nothing in the EXE, logs once `farm_guard: the proxy owns the guard in this process (ticket 0535; shared
  page 0x...): this composite installs nothing and hands its Presents to it ...`, and at each Present calls the owner's
  present entry (`FgShared::presentFn`) with its camera block's grid valid and grid seq. Not found (a test host without the
  proxy): 0527's form, the composite installs its own (`owner: the composite (no proxy guard in this process)` in its ready
  line). A variable that names no valid page: LOUD, and the composite installs its own.
- So exactly one module writes the node in a process: the proxy whenever it runs (mode=on), whatever `ServerRenderer`
  says. The composite ships with the proxy it was packaged with (the manifest names both SHA-256s), so a new composite
  does not meet an old proxy on the product route.

### 12.2 The shared page and its stubs (the unload and reload of the proxy)

The proxy can be unloaded and loaded again in one EXE process (the EXE unloads the game DLL at a mod-set change; issue
3308), and a swapped node outlives the GameView (section 4: the EXE's unregister cannot match it). A node word pointing
into the proxy's image would make the next type-59 dispatch after an unload call unmapped memory. So the node word is
not the proxy's function but a stub in a page the owner allocates once per process and never frees:

- `VirtualAlloc` of 8 KB, read-write; the first 4 KB hold `FgShared` (magic 0x56504647, version 1, pid, its own address,
  `hookTarget`, `presentFn`, attach and detach counts, the forwarded Presents, the trip flags, the stubs' addresses, the
  owner's name); the second 4 KB the two stubs, then `VirtualProtect` to execute-read (never read-write-execute; MC
  #5795) and `FlushInstructionCache`. The address goes into the EXE process's environment as `VP64_FARM_GUARD=<8 hex
  digits>` (the address only).
- The hook stub (14 bytes at page + 0x1000): `mov eax, [hookTarget]; test eax, eax; jz +2; jmp eax; ret 4`. The walk
  calls it with ecx = the node's object and one stack argument (section 1); `jmp eax` hands both unchanged to the
  owner's hook (fastcall: ecx, a dummy edx, one argument popped by the callee, section 2); with `hookTarget` 0 it pops
  the argument and returns. The inert stub (page + 0x1020): `ret 4`. The stubs are this project's own code in this
  project's own allocation (as `server/src/srv_guard.cpp`'s trampoline); no EXE byte is read for them or written (D4).
- `hookTarget` and `presentFn` are written with interlocked 32-bit exchanges: set once the EXE check passed (so an old
  node never reaches a guard that is not ready), and set to 0 by `ProxyFarmGuard::OnUnload` (DLL_PROCESS_DETACH under the
  loader lock: the watcher is stopped outside its work first, as the view-mask watcher is, so it never holds the EXE's
  lock then). From the unload to the next boot every node of the guard returns at once.
- The next proxy boot finds the page by the variable, checks it (committed, the first page read-write and the second
  execute-read of the same allocation, magic, version, pid, self, both stubs byte for byte) and points `hookTarget` at its
  own hook; the nodes already hold the stub, so no new swap is needed, and a later registration is retired and swapped as
  in section 4. A trip (or rule 2 (a)) is kept in the page, so a later boot keeps the builder off for the rest of the
  process (`... the guard stopped the builder in an earlier boot of this process: it stays off`).
- Not verified [I]: that the EXE calls `FreeLibrary` on the game DLL from the main thread. If it did so on another
  thread while the main thread ran inside the hook, the image could go away under it.

### 12.3 What runs where, with and without the composite

- **With the composite** (`ServerRenderer = 1`): its Presents reach the owner's jobs on the main thread (the burst line
  and its graph line, the in-game line and check of sections 4 and 9, as before); the watcher only swaps. The lines are
  the owner's and go to `vpproxy.log` (in the run folder) instead of `shell_composite.log` (a change for readers of the
  composite's log: the evidence scripts of 0527 and 0528 read `shell_composite.log`).
- **Without the composite**: the proxy has no main-thread point between frames (its Update hook runs on the game-core
  thread, `game-core-thread.md`, beside the main thread; reading the graph there could see it half written and trip the
  guard falsely). So the graph is read only on the main thread, inside the hook: **at the first builder call of every
  burst** (2 s after the previous builder call) the hook runs the free-list check before the builder (line `farm_guard:
  before a burst (owner proxy, no composite: the in-game check at the burst's first builder call N): sound | BAD ...`) and
  the graph line (`farm_guard: graph (before builder call N, the first of a burst)`); a bad result trips the guard as the
  in-game check does, and that call is skipped. The burst line comes from the watcher thread, from the hook's counters
  only (`load N (grid seq 0, 2 s without a call; no composite: no graph line here)`); there is no camera block, so the grid
  seq is 0. A load without any builder call (no farms) gets no check: nothing is called.
- Once one forwarded Present has arrived in the owner's image, the owner counts as "with the composite" for good (no
  pre-check, no watcher burst line), as in 0527. (Corrected 2026-11-18 14:45 after load s1a: the first form, "a
  forwarded Present in the last second", fell back to the pre-check and the watcher's burst line during the load,
  because the EXE presents nothing while it lays out 1,200 farm events in about 20 s; the burst line then came without
  its graph line. s1a's numbers are kept in 12.6 as a run of that first form.)

### 12.4 Keys (names unchanged)

`farm_guard` and `farm_builder` in `[composite]` of `shell_composite.ini` beside the proxy, each overridden by
`VPSHELL_FARM_GUARD` / `VPSHELL_FARM_BUILDER` in the EXE's environment, as the composite reads them; the proxy reads them
again every second (line `farm_guard: proxy keys: farm_guard=1 (default|ini|env), farm_builder=... (...), composite loaded
| not loaded: the builder is ...` on each change). A missing ini (increment 1 ships none, `tools/installer/build_installer.py`)
gives the built-in defaults, 1 and 1. `farm_builder = 0` (set by `proxy_shell.cpp` as `VPSHELL_FARM_BUILDER=0` with
`ServerFarmFields = 1` and the render lines, 4853) acts only while the composite is loaded, as before 0535: without the
composite the server draws no fields, so the builder is called through the guard. No `VP64Options.ini` key (section 7
stands); docs/OPTIONS.md needs no line for 0535.

### 12.5 Tests [L offline, workstation, 2026-11-18]

- `proxy/tests/test_farm_guard_proxy.py` (`farm_guard_proxy_test.cpp` with `proxy_farm_guard.cpp` and `farm_guard.cpp`,
  built in the proxy's toolchain, clang-cl -m32 with VC9 headers, /W3, no warning in the two files; then under MSVC
  /fsanitize=address with a canary): **44 checks** on a fake world without any composite: the watcher swaps the node to
  the page's hook stub (stubs execute-read, data read-write), the builder gets the same object and argument through the
  stub and the stack is balanced (esp measured around each call), the pre-check and graph line once per burst, the burst
  line from the watcher without a graph line; the kill switch (`VPSHELL_FARM_GUARD=0` restores the builder's word, the
  default swaps again; `farm_guard=0` in the ini from the start installs none and the guard is never called; set to 1 in
  the ini it swaps within a second); `farm_builder=0` skips only with the composite; **the unload** (100 type-59 walks
  after it: the builder not called, the stack balanced, the watcher gone) **and the reload** (the page found by
  `VP64_FARM_GUARD`, attach 2, re-armed without a new swap; a later registration retires the old node to the inert stub,
  one builder call per event); a trip kept across boots. Plain 44 checks OK; ASan 44 checks OK, no report, the canary
  caught (peak 291 MB under memcap).
- `shell/tests/farm_guard_test.cpp` section 14, with the same file built a second time as `farm_guard_peer.dll`
  (`/DFARM_GUARD_PEER_DLL`, its own copy of `farm_guard.cpp`: the composite beside the owner in one process): **362
  checks** (334 before): owner and composite, exactly one swap (the owner's, to its stub; the composite's counts show 0
  swaps), the composite's hand-over line, its Presents forwarded (the owner's burst, graph and in-game lines at them, no
  pre-check); the kill switch with the composite present (the owner restores, the composite installs nothing); unload
  and reload with the composite present; no owner (the composite installs its own, 0527's form); an invalid page name
  (LOUD, the composite installs its own); the page's protections.
- Suites on the final code (`proxy/evidence/0535/suites_0535.sh all`, 15:23-15:28 EST): plain `test_farm_guard.py` (6
  tests, among them the new `test_proxy_owns_the_guard`) + `test_config_defaults.py` 15 passed; under MSVC ASan
  `test_farm_guard.py` 6 passed, 0 ASan report files; `test_farm_guard_proxy.py` 10/10 (plain and ASan, 44 checks each);
  `test_proxy_rt.py`'s shell unit through `proxy/evidence/0535/shell_unit_0535.py` (proxy_shell.cpp without the guard
  linked: the hooks stay NULL) 313/313 checks plain and under ASan, 9/9.
- Ticket 0658 (2026-11-21, account B) [C]: `test_proxy_owns_the_guard` follows ticket 0635's layout (the install in
  `InstallFarmGuard()`, called once at `OnBoot`'s top level before every `LoadLibraryW` and `Loader::LoadFromMarker`, and
  once from `OnBootOff`, which `proxy_rt.cpp`'s `BootOff` calls only under `[Proxy] GuardsWhenOff`; `OnBoot` only under
  `!g_offMode`, so one install per boot); the rules live in `_guard_install_problems`, and
  `test_proxy_guard_order_check_fails_on_a_broken_order` shows each rule caught on a text mutation of the sources. The
  product's order was read and is right on every path. Suite on 4472c77a9's shell build: 9 passed (21:38 EST).

### 12.6 Live smoke [L, 2026-11-18, workstation, Goldberg instance c5h-h]

**Set-up.** `proxy/evidence/0535/run_0535.py` (3308's prep, run and restore, imported unchanged) with the builds of
`proxy/evidence/0535/build_0535.sh`: pins batch 4 (layout `x64-c29442e8-p58724e0`: x64 DLL `2b56a1cd...`, server.exe
`fc73e60e...`), the proxy of this branch (`6f9bd946...`, built at da3cead3e against a private archive of the fork at
c29442e8c), the composite of this branch (`e31a6727...`) with its manifest and the shipped `shell_composite.ini`
(`farm_guard=1`, `farm_builder=1`), the instance's EXE `04cfdc71...`; `VP64Options.ini` holds only `[Renderer]
ServerRenderer = 0` or `1` (ServerFarmFields absent = 0); config.ini: stock with 0, the D33 set (Overlay on) with 1. Save
`C5H-0535-HUGE_0350` (our copy of `C5H-VP8P-HUGE_0350`, c499d3c6..., turn 350), 2 AI turns per load with turns.py, the
harness's stop. Raw: `F:/c5-64bit-work/0535/<tag>/out/` (vpproxy.log, shell_composite.log, guard.json, run.json,
farm-builder-calls.log).

**The first load crashed, and why (D185)** [C for the site, L for the case]. Load s0a (14:24-14:27, ServerRenderer = 0,
pid 47312): EXE access violation writing 0x0000000C at VA **0x8CD492** on the main thread 105 s after the start
(`exe-crash.txt` of run folder `20261118-142525-47312-1`; dialog watcher #5805, crash without a box). That is the known
River / Arrows crash: `RenderBatch::Reset` on a NULL batch when the River or Arrows system is switched off in config.ini
while its view bit stays set (`addresses.md` 0x8CD492; `disable-systems-and-masks.md` section 2 and 5 probes A2/B2).
3308's prep writes the D33 config.ini (River and Arrows off); the proxy clears those view bits only with ServerRenderer =
1 (`proxy_shell.cpp` Mask), so with 0 the River render drew with a NULL batch at the first world frame. The guard had
finished before it: the swap at 0 nodes / 0 endpoints live (57.3 s), the pre-check sound (754 endpoints, 70.9 s), 1,200
builder calls, all 1,200 checks sound, 19.5 ms of checks, the burst line at 91.8 s, no LOUD line. So the crash is the
test layout's (a D33 config.ini is not a player's ServerRenderer = 0 set-up), not the guard's and not a product defect;
the driver gives the ServerRenderer = 0 arm darkred.py's stock config.ini from then on. The shipped product cannot pair
the two (MC #5815's question, read 2026-11-18 [C]): the only product writer of config.ini is the config.ini routine of
issue 3520 (`proxy/helper/proxy_cfgini.cpp`, in vp64helper.exe; its "on" at install, its "off" at uninstall or the
switch back), and its keys are `VP64_D33_KEYS_A2` of `proxy/src/proxy_session_keys.h` = D33 **minus River and Arrows** (the
A'' form of D119, `../06-tooling-and-workflows/release-driver.md` W3b.5, chosen because those two keys crash any start
whose view bits are not cleared), and the proxy writes no config bit; so River and Arrows are off only if a player sets
them by hand, which crashes the stock game the same way. Evidence kept in
`F:/c5-64bit-work/0535/s0a-crash-d33/` and the instance's `run/crash/20261118_142712_pid47312`.

**Two loads of the first build** (proxy `6f9bd946...`, composite `e31a6727...`): s0b (14:28-14:35, ServerRenderer = 0,
stock config) passed every check, 1,252 builder calls; s1a (14:36-14:41, ServerRenderer = 1) showed the first form's
fall-back during the long layout (12.3's correction) and was stopped after 1 turn to rebuild. The instance was restored by
hash and prepared again for the final build.

**The six loads on the final build** (proxy `adebe8ae...` built at fbd3fe8ab, composite `940c6d84...`; 14:46-15:19 EST;
`proxy/evidence/0535/summary_0535.py`):

| Load | ServerRenderer | AI turns | exit | builder calls (all) | swap at nodes / endpoints | load 1 burst | check time (load 1) | composite hands over | pre-checks | in-game lines | exe-crash | LOUD |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| f0a | 0 | 2 | 0 | 1,252 | 0 / 0 | 1,200, checks 1,200: all sound | 21.97 ms (max 241 us) | no composite | 8 | 0 | none | 0 |
| f0b | 0 | 2 | 0 | 1,252 | 0 / 0 | 1,200, all sound | 20.46 ms (max 91 us) | no composite | 8 | 0 | none | 0 |
| f0c | 0 | 2 | 0 | 1,252 | 0 / 0 | 1,200, all sound | 23.14 ms (max 612 us) | no composite | 8 | 0 | none | 0 |
| f1a | 1 | 2 | 0 | 1,252 | 0 / 0 | 1,200, all sound | 20.34 ms (max 247 us) | yes | 0 | 1 | none | 0 |
| f1b | 1 | 2 | 0 | 1,252 | 0 / 0 | 1,200, all sound | 19.71 ms (max 127 us) | yes | 0 | 1 | none | 0 |
| f1c | 1 | 2 | 0 | 1,252 | 0 / 0 | 1,200, all sound | 19.03 ms (max 265 us) | yes | 0 | 1 | none | 0 |

- **One owner, the proxy, in all six** [L]: `farm_guard: owner proxy (ticket 0535): shared page 0x... created (attach 1
  ...)`, the ready line `... owner: the proxy (ticket 0535: one owner per process ...)` and `farm_guard: installed by the
  proxy: the builder's listener node 4 (list of 5, channel 0) ...` in each vpproxy.log; with ServerRenderer = 1 the
  composite's log has the hand-over line and no `installed` line; with 0 no composite log at all.
- **The swap is in time** from the proxy's watcher too: 0 nodes and 0 endpoints live at the swap in all six (the
  composite's Present gave the same in 0527's 13 loads), so all 1,200 load calls went through the guard [L].
- **ServerRenderer = 0**: 8 pre-checks per run, all sound: the load's burst and 7 bursts in the two AI turns (farms built
  in play; f0a: 1 + 12 + 33 + 3 + 1 + 1 + 1 calls); each ran the check and the graph line on the main thread before the builder.
  **ServerRenderer = 1**: no pre-check, the burst lines with their graph lines and one in-game line per load at the
  forwarded Presents, as 0527's composite gave them.
- 52 builder calls in the two AI turns of each load, the same in all six; no crash, no box, every stop clean.
- Not shown live: a trip (no bad graph met), the unload and reload of the proxy within one process (the harness starts a
  fresh process per load; offline in 12.5), rule 2 (a standard huge map is within 65,535 cells), and an oversize map with
  ServerRenderer = 0.

## 13. The farm-end post: the farm buildings with the builder skipped (ticket 0655, D230, D231, 2026-11-21, account B) [C for the code, L where cited]

Why (`../05-graphics-composition/cities-and-landmarks.md` 2.6.1, one-off 9078): with `[Renderer] ServerFarmFields = 1`
the builder 0x748FC0 is never called, so the farm manager's records wait for a farm-end record (type 5 of the 4 MB queue)
that only the builder posts, and no farm building exists (`farm-field-double-free.md` 4.4). Ticket 0655 makes the
builder's last step, and nothing else of it, from the hook's skipped-server path (section 12.3, ticket 4853). The owner made it
the first beta's route on 2026-11-21 (D231). Code: `shell/composite/farm_guard.{h,cpp}` (`PostFarmEnd`, compiled into the
proxy, the guard's owner, and into the composite); `proxy/src/proxy_farm_guard.cpp` (the keys). Branch
`task-0655-farm-end-post`.

### 13.1 What the hook does on the skipped-server path [C]

The builder's own three tests, then its last step, as its code at 0x748FD3..0x749111 does them (build 4390913):
1. the payload's flag byte +0x1C non-zero;
2. the plot on the map: the EXE's plot test 0x42B440 (thiscall on 0xD91130, one argument: a pointer to the cube
   coordinates x = payload +0x18, y = +0x1A, z = -x-y; the answer is `al` only, the rest of `eax` is not set);
3. the farm's art: the EXE's art id call 0x61AA50 (thiscall on 0xBA3458, argument the EXE's own string
   `ART_DEF_IMPROVEMENT_FARM` at 0xA390A4) equals the payload's +8 and is not -1. The call is made per event, as the
   builder makes it on every call in stock (it interns or looks up the name).
4. then the queue object `qg_channels + [qg_channel_index] * 0x800180` (0x1A0F080 + [0x1A0E400] * 0x800180; the index read
   under SEH and refused unless 0 or 1), the EXE's poster 0x48E810 on it (thiscall, no argument, plain `ret`; it reserves
   0x80 bytes in the double-buffered 4 MB queue through the import slot of `InterlockedExchangeAdd`, which the proxy's
   queue guard (0492) covers, writes type 5 and returns the payload with two zeroed 16-bit words), and the builder's two
   stores: hex x, hex y. A poster that returns 0 is refused (LOUD once); the builder itself writes without that test.

Nothing else runs: no type-4 post, no layout, no face pass, no graph write, no mask bit. The EXE's functions are called
outside any `__try` (their faults stay the EXE's, as the builder's are); only the hook's own reads are guarded. The other
skip paths (tripped, no view, stale, rule 2) post nothing; with `farm_builder = 1` the builder posts itself.

### 13.2 Where the addresses come from, and the per-build off switch (P5) [C, L]

Eight catalogue symbols in `exebuild/symbols/shell.tsv` (anchors of build 4390913): `fg_builder_tests` 0x748FD3,
`fg_builder_post` 0x7490F3, `fg_post_farm_end` 0x48E810, `fg_plot_on_map` 0x42B440, `fg_plot_grid` 0xD91130 (data),
`fg_art_id` 0x61AA50, `fg_art_db` 0xBA3458 (data), `fg_farm_art_name` 0xA390A4 (rdata); the queue's two words and the
import slot are the proxy's `qg_channel_index`, `qg_channels`, `qg_iat_interlockedexchangeadd`. Five byte patterns of their
own (`kFgPostSigs`): the builder's tests with every operand and both call targets (`C(fg_plot_on_map)`, `C(fg_art_id)`),
the builder's last step with its call target (`C(fg_post_farm_end)`), the stores and `ret 4`, the poster's body (type 5
at +4, the reservation through the import slot), the plot test's head (it reads three dwords of its argument), the art
name's bytes. So the post calls exactly the functions the builder's own code calls in the running build. (`ShellExeLay`
and `ShellExeSpan` of `shell_exe.h` now expand `C(...)` tokens, so the fake images of the tests carry them.)

They are checked after the guard's 13: a symbol not carried, a pattern that does not match or an unknown build turns
**only the post** off, with one line `farm_guard: LOUD: the farm-end post is OFF on <build>: <reason> - with
farm_builder=0 no farm building is made in this process (the guard and the skip run as before)`; the burst line then ends
`(the post is off for this build)`. When they match: `farm_guard: the farm-end post (ticket 0655): 5 byte patterns match
(<build>) ...`. Live, both builds match (13.6, 13.7): no build is off today; the testers' note keeps a "no farm buildings"
line only for a build that logs the OFF line.

Carried to build 26577625 by the generator on 2026-11-21 [C]: all eight with confidence high (bytes16, in-function, callers'
vote 22/22, data xrefs 57/57 and 20/20) except `fg_farm_art_name` (rdata xrefs 1/1, medium); all five patterns OK on both
golden EXEs (`gen_table.py`, `shell/tests/test_exe_tables_shell_b.py`); the addresses stay in the local table (D202).

### 13.3 The switch (P4), the counters and the lines [C, L]

- `farm_end_post = 1` (default) | 0 in `[composite]` of `shell_composite.ini` beside the proxy, overridden by
  `VPSHELL_FARM_END_POST` in the EXE's environment; read by the proxy (the guard's owner) with `farm_guard` and
  `farm_builder` every second; it acts only on the skipped-server path. It works on a shipped layout: the 0.1b TEST
  installer lays `shell_composite.ini` beside the proxy (`MODS/(1) Community Patch/`, seen on c5h-new-a 2026-11-21; 12.4's
  "increment 1 ships none" is older), so a tester bisecting a beta report adds the line `farm_end_post=0` right under its
  `[composite]` line (a layout without the file takes a new file with the two lines `[composite]` and `farm_end_post=0`),
  or sets the variable: the next start has no farm buildings and nothing else changes (the OFF arms below, both builds).
  Not a player option (no `VP64Options.ini` key).
- The proxy's key line gains a tail: `...: the builder is never called (the server lays out the farm fields);
  farm_end_post=1 (default): the farm-end post makes the farm buildings (ticket 0655)` (or `farm_end_post=0 (ini): no
  farm-end post, no farm building (the test switch of ticket 0655)`); the guard logs each change of the switch.
- Counters (`FgStats`): `posts`, `postFailFlag`, `postFailMap`, `postFailArt`, `postRefused` (post off for the build, an
  unreadable payload or queue index, a poster that returned nothing), `postSwitchedOff`. The burst line ends `farm-end
  posts N (failed the builder's tests F, refused R, switched off S)`; the in-game line `farm-end posts N in this process
  (failed the flag / plot / art test a / b / c, refused R, switched off S; post on|switched off|off for this build)`.
- `farm-builder-calls.log` words on that path: `skipped-server-posted`, `skipped-server-post-test-flag|map|art`,
  `skipped-server-post-refused-payload|queue|null`, `skipped-server-post-off-for-build`, `skipped-server-post-switched-off`
  (all `skipped-server` before 0655).

### 13.4 The area probe, a diagnostic for P1 [C, L]

The Architect's P1: each farm end makes the farm manager post one area event (A33) over the plot's rectangle, which the
builder object's area listener 0x79D040 turns into a dirty-cell re-lay (0x504B10, then 0x6E27D0 on this + 0x154); if the
dirty marks were not cleared the cost would grow with every post. The listener is NOT skipped (the Architect's ruling);
to measure it, key `farm_area_probe = 1` (default 0, `VPSHELL_FARM_AREA_PROBE` first; a DIAGNOSTIC, not for players)
swaps the listener's node word on the 4 MB A manager's type-33 list to a timing pass-through (`AreaProbe`: QPC around the
call of 0x79D040 with the same object and argument), under the A manager's own registration lock: lock-init byte
0xCC733C bit 0, lock word 0xCBA9F0, channel index 0x1A0E400, list `[0x1A0E514 + index * 0x600]`, registered by 0x847960
from the group 0x465600 (at 0x46564C); the same node form and walk as type 59 (section 1). A new registration retires the
old probe node to an inert word, the key at 0 puts the listener's address back, the owner's unload does the same. Lines:
`farm_guard: area probe: N calls ... total T ms, mean, median, max; the first 20 mean A us, the last 20 mean B us`, and
every value in `farm-area-calls.log` beside `farm-builder-calls.log`. Six symbols (`fg_area_listener`, `fg_area_reg`,
`fg_a_register`, `fg_a_lockinit`, `fg_a_lock`, `fg_a_lists`) and three patterns (`kFgAreaSigs`); on 26577625 the four of
the register are look-alikes of the type-59 register (only their operands differ), so the generator does not carry them
by bytes: answered in the local `hand-review.tsv` as the only register on the channel index whose list operand is the
index + 0x114, and confirmed by the group's pattern `E8 C(fg_a_register)` OK on that build [C].

### 13.5 Tests [L offline, workstation, 2026-11-21]

- `shell/tests/farm_guard_test.cpp`: 555 checks, plain and MSVC ASan x86. Section 1 per known build: the five post
  patterns and the three probe patterns on a fake image, each broken alone refused, a call target moved by one byte refused,
  a symbol not carried refused, the addresses rebased. Section 15 (stand-ins of the poster, the plot test with garbage in
  the upper bits of `eax`, the art id): one post per event that passes, the queue of channel 0 and 1, the two stores and
  nothing past them, each test failed alone (the later tests not called), an unreadable payload, a queue index of 2, a
  poster returning 0 (LOUD once), the graph unchanged, the burst and in-game lines, the event words, the switch, no post on
  the no-view, stale and tripped paths nor with `farm_builder = 1`, a world without the post (refused, the line says so).
  Section 16: the probe on a fake A list (off: nothing touched; on: swapped under the lock, one listener call per event,
  the burst line and file, a new registration retires the old node, off restores, a held lock writes nothing).
  `shell/tests/test_farm_guard.py`: 7 of 8 pass; `test_proxy_owns_the_guard` fails on main as well since ticket 0635 moved
  the proxy's guard install out of `OnBoot()` (not this ticket).
- `proxy/tests/test_farm_guard_proxy.py`: 62 checks in the proxy's toolchain and under ASan (11/11); section 6: the post
  through the shared page's hook stub with the stack balanced, the flag test, the switch by environment and by the ini.
- `tools/exebuild/tests` and `shell/tests/test_exe_tables_shell_b.py` (21 farm_guard rows; 0x800180 is an allowed literal).

### 13.6 Live on build 4390913 [L, 2026-11-21 18:37-19:19 EST, workstation, c5h-b]

Layout: this branch's server, proxy and composite built from the first beta's pins (`C:/c5-64bit-work/keep/pins-0.1b`),
laid by `evidence/4967/lay.py` (D28, the shipped `shell_composite.ini`), the first beta's option set of
`farm-field-double-free.md` 4.1 with `ServerFarmFields = 1`; copies of `C5H-VP8P-HUGE_0350` (turn 350, 373 farm plots);
restored by hash at the end (`lay.py restore` ok, `provision.py verify` ok). A read-only probe of the farm manager's
pending list (landmark sim 0x2D70310 + 0x24, count at +0x45C; build 4390913 only).

| Launch | Arm | Listener calls | Builder | Posts / flag-test fails | Pending after load | Graph (nodes = endpoints) | LOUD, box |
|---|---|---|---|---|---|---|---|
| ON1 | post on | 1,200 | 0 | 373 / 827 | 0 | 754 | 0, none |
| ON1, second load in the same process (`Events.PlayerChoseToLoadGame`) | post on | 1,200 | 0 | 373 / 827 | 0 | 754 | 0, none |
| OFF1 | `farm_end_post=0` | 1,200 | 0 | 0 (1,200 switched off) | **373** | 754 | 0, none |
| LM0 (`ServerLandmarks = 0`) + play + 3 AI turns | post on | 1,200 + 61 | 0 | 373 + 5 | 0 | 754 in all 8 graph lines | 0, none |
| ON2 | post on | 1,200 | 0 | 373 / 827 | 0 | 754 | 0, none |
| tiny T0018 + 2 farms + era change | post on | 2 | 0 | 2 | 0 | 269 | 0, none |

- **The buildings appear** (pictures not in git: `C:/c5-64bit-work/0655/run-c5h-b/`): barn and silo models on the server's
  fields, one per farm plot, at the plot centre, in the owner's era style, drawn by the EXE (`ServerLandmarks = 0`) and by
  the server from the model tap (`= 1`).
- **The mechanism is confirmed** [L]: the pending list is 0 after every load with the post and 373 (= the farm plots)
  without it; one post per farm plot (the other 827 events fail the builder's own flag test, as in stock).
- **P2**: 754 nodes and 754 endpoints, none freed, every cell list sound, after the load, the in-play farm changes and the
  turns (8 graph lines of LM0); and after the second load in the same process.
- **P3**: pillage (the pillaged smoke and the burnt model), repair (the building back), removal (fields and building gone;
  removal reaches the farm manager through B60, without a hook call), a new farm (its building), the removed plot farmed
  again (its building, a new quarter-turn facing), and pillage/repair toggled four times in one Lua chunk (one building at
  the end, none doubled) [L, pictures, `P1-*`, `P2LM0-*`]. The second load inside one process starts clean (373 posts,
  pending 0, graph 754). Exactly one instance per record holds by the code: 0x603000 frees an existing instance (flag
  0x100) before it makes one [C]; the pictures show no doubled model [L]; an instance count was not read.
- **Era**: on the tiny save, techs of later eras given by Lua (era 0 -> 5): the farm buildings and the city keep their
  style until the turn ends, then both take the modern style [L, `ERA-*.png`].
- **The EXE's own farm decals and mask**: 0 field and 0 track records in the newest decal tap image, 0 field and 0 margin
  (bit 0x10) cells in the EXE's cut grid (ON2) [L; readers of `shell/evidence/0528/tracks_4853.py` and `cutgrid_4853.py`].

### 13.7 Live on build 26577625 [L, 2026-11-21 19:22-20:45 EST, workstation, c5h-new-a]

The same layout and option set (proxy `fcf9c45d...`, composite `414139ab...`; for the probe runs the probe-carrying proxy
`2bc03ce2...` and composite `182c7e68...` of commit cc4fdab96).

| Launch | Arm | Listener calls | Builder | Posts | Graph | Decal tap EXE fields / tracks | Cut grid field / margin | LOUD, box |
|---|---|---|---|---|---|---|---|---|
| N-ON1 + play | post on | 1,200 | 0 | 373 (827 flag fails) | 754 | 0 / 0 | 0 / 0 | 0, none |
| N-OFF1 | `farm_end_post=0` | 1,200 | 0 | 0 (1,200 switched off) | 754 | | | 0, none |
| N-SFF0 | `ServerFarmFields = 0` (the builder path, stock's farm look) | 1,200 | 1,200, checks all sound | (the builder posts) | 9,060 live / 9,129 | | | 0, none |
| N-LM0 | post on, `ServerLandmarks = 0` | 1,200 | 0 | 373 | 754 | | | 0, none |
| N-ON2 | post on | 1,200 | 0 | 373 | 754 | | | 0, none |
| N-P1 + play | post on, area probe | 1,200 | 0 | 373 | 754 | | | 0, none |
| T-ON, T-OFF, T-OFF2, T-ON2 (ABBA, 20:00-20:14, DISTURBED) | post on / off | 1,200 each | 0 | 373 / 0 | 754 | | | 0, none |
| OV-P (oversize 43-civ, m43 TEST installer layout) + probe | post on | 3,438 | 0 | 737 (2,701 flag fails) | 5,578 (80,640 cells, sound) | | | 0 farm; 26 units LOUD (below) |
| OVT-ON, OVT-OFF, OVT-OFF2, OVT-ON2 (ABBA, 20:21-20:41, DISTURBED) | post on / off | 3,438 each | 0 | 737 / 0 | 5,578 | | | as OV-P |

- The five patterns match on 26577625 from the local table; the buildings appear as on 4390913. Picture pairs (the three
  views of `farm-field-double-free.md` 4.4, `C:/c5-64bit-work/0655/run-c5h-new-a/`, Visual Checks feed): post off / on:
  farm region close 30.0 %, far 9.0 %, city 19.9 % of pixels differing by more than 16 (the difference is the buildings;
  the server's fields are the same in both arms); stock look (`ServerFarmFields = 0`, the builder's fields and buildings
  on field edges) / product: 37.5 %, 11.7 %, 39.0 % (the field pattern and the building positions).
- In play (clear plot 88,18, the server drawing): pillage smoke, repair, removal, new farm, the fourfold toggle: as on
  4390913.
- **P1, the per-call cost of the area event, inside one run** (N-P1, huge, post on): 373 calls of the area listener at
  load (= the 373 posts: the listener's call count, now measured directly), total 16.7 ms, mean 44.8 us, median 43.0 us,
  max 431 us; decile means 53, 42, 42, 43, 44, 45, 45, 43, 44, 45 us; the first 20 mean 62.5 us, the last 20 44.9 us; a
  least-squares slope of +0.05 us per call: **flat** (no growth with the number of posts). In play, 7 more calls of 44-370 us.
- **P1 on the oversize 43-civ save** (9074's `TS1.20-T0750`, 180 x 112 plots, 80,640 grid cells; the m43 0.1b rehearsal
  TEST installer laid on c5h-new-a with this branch's proxy and composite swapped in, the proxy and server take no 43-civ
  define; removed afterwards, the DLC tree identical, the user tree back but for run outputs): 737 posts and 737 calls of
  the area listener at load, total 70.0 ms, mean 95.0 us, median 87.5 us, max 1,181 us; decile medians 87, 85, 85, 85, 94,
  93, 89, 93, 91, 86 us; the first 20 mean 228 us (the first calls of the load), the last 20 90 us; slope -0.02 us per call:
  **flat**. The mean per call is about twice the huge save's for about twice the cells (each re-lay walks the grid), as
  the Architect expected; it does not grow with the number of posts. Buildings drawn there as on the standard saves
  (`OV-P-farms.png`). Rule 2 stays out of the way (it acts only while the builder is called). 26 LOUD lines per oversize
  load in server.log, all `units: AnimFailed: issue 4722: the trigger stream refused frame ...`, in the post-off arms too:
  the units renderer's known issue 4722, not the farm path.
- **Load time (the cost statement, DISTURBED: other agents' games and builds ran in both holds; MC-B's notes of
  2026-11-21: W9 on c5h-a, W11 on c5h-c, W12 on c5h-new-b, 0652's builds, an ASan test 20:27:50-20:36)**: render.log's
  server start to the map's first drawn frame. Huge: post on 68.72, 68.89 s; off 67.58, 68.43 s (difference of the means
  +0.80 s, n = 2 + 2). Oversize: on 132.77, 145.91 s; off 134.34, 131.90 s. These pairs cannot resolve a difference below
  a few seconds; no claim is made (`docs/briefs/PERFORMANCE.md`). The main-thread time the route adds that is not in stock
  is measured directly above: 16.7 ms (huge) and 70.0 ms (oversize) of area-listener calls per load; the farm end itself
  (placer and instance) is the same work stock does per farm. A quiet pair is placed by MC-B for 2026-11-21 23:00-23:40
  (13.10).

### 13.8 What differs from stock, Rule A, scene bundles [L for the pictures, C for the rest]

- **For the testers' note** (superseded by ticket 0657 for builds with the edge placement, 14: the buildings stand at the
  centroid of one of the server's fields, by the stock rule; kept for builds without it): with the patch the farm buildings
  stand at the centre of their tile, among the patch's own field pattern; in the unpatched game they stand in the middle of one of the farm's fields, the one nearest the tile's
  centre (`../05-graphics-composition/cities-and-landmarks.md` 2.6.2 [C, L]; "on the edge of a field" was the pictures'
  look, corrected 2026-11-22 by 0657). Barns and silos keep their era style, smoke when
  pillaged, light, fog and shadow as before.
- The placement is the EXE's placer with no curves (2.6.1 (2)): the plot centre, the terrain height, the direction field
  plus a random quarter turn; a rebuilt farm can face another way than before (one CRT `rand()` per placement, as in stock).
- **Rule A**: the server draws nothing by new code: the buildings are the EXE's own landmark instances and reach the
  server through the existing model tap like every other landmark (`ServerLandmarks = 1`). A scene bundle with farms
  recorded with `ServerFarmFields = 1` before this change has no farm-building records; re-recording one with farms is
  advised so the offline library carries the buildings, not required for any existing check.
- Cost: 373 extra area events and their re-lay (16.7 ms of the main thread in total on the huge save), one farm-end
  record per farm event in play.

### 13.9 Not verified

- The quiet load-time pair: placed by MC-B for 2026-11-21 23:00-23:40 (13.10 when run).

- An instance count per farm (the pictures and the code, not a count, say "one").
- The effect on a save whose farms have no row for their era (the record stays pending, as in stock).

## 14. The edge placement: the farm buildings at the server's field centroids (ticket 0657, 2026-11-23, account B) [C for the code, L where cited]

With `[Renderer] ServerFarmFields = 1` the farm-end post of 13 makes each farm's building, but the EXE's farm manager has no
field curves, so its placer keeps the plot centre (13.8). Stock puts the building at the centroid of the plot's field
nearest the plot centre (`../05-graphics-composition/cities-and-landmarks.md` 2.6.2). Since 0657 the server computes that
rule on its own fields and publishes one point per farm plot, and the guard hands it to the EXE's placer as a transient
curve for the length of the EXE's own call. Design and the reads behind it: cities-and-landmarks.md 2.6.3; the server's
side: `../05-graphics-composition/farm-fields-layout.md` 16.3 and 16.6.

### 14.1 What the guard does [C]

- **Two more node swaps**, made by the owner's watcher thread exactly as F1's (a data write of a listener node's function
  word under that manager's own registration lock, compare-and-swap against the EXE's listener): the farm manager's
  **farm-end listener** `fg_farm_end_listener` in the A manager's type-5 list (slot offset 0x30) and its **era listener**
  `fg_farm_era_listener` in the B manager's type-48 list (slot 0x188). The node's object (the farm manager) is matched by
  any value (`FgWorld::builderObj` 0). Only while farm_builder=0 (the server lays out the fields), farm_edge=1 and the
  post's sites matched; otherwise the EXE's own words are put back. A swapped node beside a new registration of the same
  listener is retired to the inert word (F1's rule); a call through such a stale node does nothing.
- **The node words** are two more stubs in the shared page (version 2 since 0657, `FgShared::edgeTarget`, `edgeOrig`,
  `edgeStub` at +0x1040 / +0x1060): `mov eax, [target]; test eax, eax; jnz +5; mov eax, [orig]; jmp eax`. With no owner
  attached (target 0, after a proxy unload) a node calls the EXE's listener itself: the game goes on as before 0657, never
  into unmapped memory.
- **The pass-throughs** (`EdgeEnd`, `EdgeEra`; the EXE's main thread): cities-and-landmarks.md 2.6.3 (2). The farm end is
  held by posting the same farm end again with the EXE's poster (`fg_post_farm_end`, the queue object of the channel index,
  as 13.1) while this load's point is not there; the EXE's listener then runs at a later pump.
- **The server's points**: `<VPSHELL_SECTION>.Farm` (`shell/common/vp_farm_points.h`), opened read-only by the watcher once
  a second until it is there, read under its seqlock. A point is used only while the section is DONE and its loadSeq is the
  camera block's grid seq of the last composite Present (a second load in one process never takes the first one's points).
  The hold table (per plot, the first hold's time) is cleared at every new grid seq.

### 14.2 Sites, switch, counters [C]

- 13 patterns (`kFgEdgeSigs`, all with their call targets): the listener group (era listener to the B48 register, farm-end
  listener to the A5 register, the manager as the object), the two registers (lock, index, list), the two listeners' heads
  (the deferral bit re-queueing their own address, the pending list, the records) and their placer calls, the placer's head,
  its curve loop (record +5, manager +0x6F4, record +0x20, 0x2C per curve, +0x10, +8, 0xFC per segment, the centroid call)
  and its F call against the threshold `fg_placer_min_f`, and the heads of the three curve functions. Catalogue: 17 rows in
  `exebuild/symbols/shell.tsv`. A mismatch, an unknown build or a symbol not carried leaves the edge OFF with one LOUD line
  (`the edge placement (ticket 0657) is OFF on <build>: ...`); the guard, the skip and the farm-end post run as before.
- `gen_table.py` on the golden copies (2026-11-23 16:0x): 4390913 identity 436/436, 283/283 patterns OK; 26577625 after two
  hand answers 436/436, 283/283 OK. The two registers are look-alikes of other registers (the generator carried
  `fg_b48_register` to a wrong one with high confidence and could not carry `fg_a5_register`): they were found in the new EXE
  by their shape with every operand fixed (that build's lock, lockinit, IAT slot, channel index and list operand = index +
  0x34 / + 0x18C; one hit each), and `fg_farm_group`'s `C(fg_b48_register)` and `C(fg_a5_register)` confirm them. Script
  `shell/evidence/0657/find_regs.py` (prints no address; writes the LOCAL hand-review.tsv, D202).
- **The test switch** `farm_edge` = 1 (default) | 0: `VPSHELL_FARM_EDGE` in the EXE's environment first, then `[composite]`
  of `shell_composite.ini`, read by the proxy every second; `farm_edge=0` = no pass-through, every building at its plot
  centre (0655's behaviour). It works on a shipped layout too (P4's rule of 13.3).
- **Counters** (`FgStats`, the in-game line `edge (0657) on: farm ends N, placed N (read back N, more than 1 unit off N,
  worst D), centre N, held N + N deferred, run out N, re-post failed N, no section N, dropped N, stale N; era events N,
  curves N, deferred N; faults N`; the burst line `edge (0657): placed at the server's point N, at the plot centre N, held N,
  holds run out N, era curves N`). "read back": after each placing call the records' +0x14 / +0x18 are compared with the point.

### 14.3 Tests [L offline, workstation, 2026-11-23]

- `shell/tests/farm_guard_test.cpp` section 1: the 13 patterns on fake images of both builds, each broken alone, the group's
  call of the A5 register moved (a look-alike), a symbol not carried, the world's addresses; section 17: a fake farm manager
  with records, its two listeners and a placer that repeats the EXE's float loop of cities-and-landmarks.md 2.6.2 (1):
  farm_builder=1 touches nothing; the swap and farm_edge=0; a point placed (0.00024 off) with nothing of the curve left and a
  freed record of the same plot untouched; a CENTRE entry; held while PRODUCING, for another load's points, without a valid
  grid; a missing plot run out at its limit; the deferral held, then let through; a failed re-post; the view shut down; no
  section; the era listener with two curves; a stale node retired at the next registration; a held lock; edge off. 667
  checks, OK (`C:/c5-64bit-work/0657/shell-build`, 2026-11-23 16:1x).
- `proxy/tests/test_farm_guard_proxy.py` 11/11 (the guard compiled with the proxy's VC9 headers, its unit under MSVC x86
  ASan with the canary reported); `shell/tests/test_farm_guard.py` 11 passed (two new: the edge unit, the proxy's key).

### 14.4 Live on build 26577625 [L, 2026-11-23 18:03-18:14 EST, workstation, c5h-new-b]

Branch `task-0657-farm-building-field-edge` (server.exe and proxy by `build_server.py --config release` on the 0.1b pins,
the composite of the branch's `shell/build.cmd`), laid by `lay.py prep` + `helper on`, the first beta's option set with
`ServerFarmFields = 1` (`shell/evidence/0657/set_options.py`), a copy `C5H-0657-HUGE0350` of `C5H-VP8P-HUGE_0350` (turn 350,
373 farm plots), mods cp, vp, eui, hidden; driver `shell/evidence/0657/live_0657.sh n1 c5h-new-b C5H-0657-HUGE0350 1 play`.
- **Placement**: the edge probe (`shell/evidence/0657/edge_probe.py`, read only) 26 s after the load screen closed: 373
  live farm records, 373 with an instance, **373 of 373 within 0.05 world units of the server's point** (median 0.0001, max
  0.0020); 0 records holding a curve count. The farm manager was found through the guard's edge stub in the type-5 list.
- **Timeline** (proxy log, process time): the swaps at 27 s (a node of an earlier registration retired on both lists: the
  load registers the group twice); the 373 farm-end posts and 13,055 holds (373 per pump while the server's section said
  PRODUCING for grid seq 3) until the server's DONE (render.log 18:05:58, 6.0 s after its PRODUCING for the load); then 374
  placed at the server's points, 0 at the plot centre, 0 holds run out, 0 re-posts failed, 0 faults.
- **Play**: pillage of one farm, removal and rebuilding of another, the team's era 5 -> 6 by Lua, one turn (the AI played
  it: the human turn was blocked by a production choice), then the repair: after each, 373 of 373 records at their points
  (the record array grew to 472 with the era's re-creation; live 373). The era listener's pass-through gave 944 transient
  curves; the farm-end pass-through placed 124 + 25 + 1 more.
- **The K1 fix intact**: the decal tap through the load: 0 EXE farm-field and 0 farm-track records; the newest cut grid:
  0 field cells, 0 margin (0x10) cells (river 340,880, objects 924,063); the builder never called (1,200 + 31 calls, all
  skipped-server). 0 LOUD farm_guard lines, 0 boxes, 0 crashes.
- The cycle's picture was spoiled by the harness's Escape (it opened the game menu: no popup was open); the driver now puts
  the camera on a farm cluster instead (`look_farm.lua`).
- **A second load in one process** (cycle n2, 19:57-20:06, `RELOAD=1`: `shell/tools/mapchange.py load` from the InGame
  state): after the first load 373 of 373 at the points (max 0.002); the second load's grid seq was 5 (the first's 3), the
  server published PRODUCING for 5 at 20:05:16.7 and DONE at 20:05:18.0, the guard held the second load's farm ends
  13,801 times meanwhile and then placed them: 373 of 373 at the new load's points again (probe 4), 0 LOUD. The in-game
  line is written 10 s after the map comes up, before a held load is placed, so its `placed` lags; the burst lines and
  the probe are the measure.
- **The control** (cycle n3, 20:10-20:19, `VPSHELL_FARM_EDGE=0`, the same save and camera): every building at its plot
  centre: of 373, 0 within 0.05 of the server's point, 70 within 5, median 8.75, max 26.74 world units: the edge moves a
  building by that much. The pictures of n2 and n3 (DIAGNOSTIC, Visual Checks, issue 0657) show the same view with and
  without the move.

### 14.5 Live on build 4390913 [L, 2026-11-23 20:25-20:43 EST, workstation, c5h-c]

The same layout, option set, save copy and driver as 14.4, on c5h-c (mods548, EXE 4390913).
- **c1** (`live_0657.sh c1 c5h-c C5H-0657-HUGE0350 1 play`): after the load, after pillage + removal and rebuilding + era
  5 -> 6 + one AI-played turn, and after the repair: 373 of 373 records within 0.05 of the server's points each time (median
  0.0001, max 0.0020), 0 curves left; the guard held 13,055 + 746 farm ends, placed 496 + 23 + 1, the era pass-through gave
  944 curves; 0 EXE farm-field and track records, 0 field and margin cells; 0 LOUD.
- **c2** (`RELOAD=1`): the first load (grid seq 3, server DONE 20:38:08.1) and the second load in the same process (grid
  seq 5, PRODUCING 20:40:25.2, DONE 20:40:26.5): 373 of 373 at the points both times; 0 field and margin cells, 0 LOUD.
- **Totals, both builds**: 7 huge loads with the edge on (n1, n2 x 2, c1, c2 x 2, plus n2's first launch stopped on its
  load screen for the owner's hold), 1 with it off (n3); 0 crashes, 0 boxes; every probe of a load with the edge on: 373 of
  373 within 0.002 world units. The buildings appear about 1-6 s after the server's DONE for the load, which follows the
  load screen by up to a minute; with the edge off (0655) they appear at once at the plot centre.

### 14.6 Not verified

- A stock picture pair at the same camera (D112 ACCEPTANCE): the pictures are DIAGNOSTIC pairs of the product with the
  edge on and off; the stock positions were measured on stock by 2.6.2 (6) (357 of 373 within one unit of the rule), not
  pictured here.
- The 43-civ oversize save, a farm built in the fog, a farm whose plot has only small fields (the CENTRE kind: none on the
  huge save, 1 of 64 in the unit test), the era listener's deferral path live (the `eraDeferred` counter stayed 0).
- A proxy unload and reload in one process with the edge's stubs live (the unit test covers the page; 0535's live run
  predates the edge stubs).
- The hold's cost on the main thread: 373 re-posts per pump while a load waits (about 13,000 in all per load, the 4 MB
  queue never above a few percent [I, not measured]).

### 14.7 After the merge of main 01068b8a0: every builder and rule A [L offline, workstation, 2026-11-24, account B]

Branch `task-0657-finish` (954b5c277 + main 01068b8a0, merged without conflicts), one script for the whole pass:
`bash shell/evidence/0657/allb_0657.sh 1|2|3|4|5` from the worktree root (work folder `C5_0657_ALLB`, default
`C:/c5-64bit-work/0657f`; owner `C5_ALLB_OWNER`; every step through `quietlock.py run`; `GAME` = the 4390913 install outside
the pool). The builders follow runbook 2.10 for these sources: `render/src/farmfields/buildings.cpp` (c5render: every
linker of it), `srv_render_scene.h` (every server render unit: R13 + `build_trigger_scene.py` + `cities/build_models_tests.py`),
`srv_farmfields.cpp`, `shell/composite/farm_guard.cpp` and `shell/common/vp_farm_points.h` (`build_proxy.py`,
`test_farm_guard_proxy.py`, `shell/build.cmd`), `proxy/src/proxy_farm_guard.cpp`.
- Phase 1 (13:0x-13:24): render ctest whole 109 of 109; `build_server.py --config release` on the 0.1b pins OK in 358 s.
- Phase 2 (13:0x-13:26): render ASan `-R farmfields` 5 of 5, 0 ASan reports; `shell\build.cmd` and `shell\build.cmd asan` BUILD
  OK; `farm_guard_test.exe <scratch>` OK 667 checks plain and under ASan (no ASan log); `shell/tests/test_farm_guard.py` 11
  passed (on the rerun alone; the first run, beside the render ctest, timed out once in
  `test_composite_real_path_refuses_the_fake_exe`, a case the branch does not change: "no 'hooks committed (' within 18 s",
  bug 0120's load timing); `build_proxy.py` OK in 69 s (purity test cached); `test_farm_guard_proxy.py` 11/11.
  Trap: `farm_guard_test.exe` with no argument exits 2 ("usage: farm_guard_test <scratch folder>"); the script's first
  version gave none (both runs above were made by hand, through quietlock); the committed script passes one.
- Phase 3 (13:20-13:49): units build + `test_units_runner.py` 7 passed; units extras 13 of 13; unitfeed, trigger scene
  built; effect routes, trigger emitter, units fights / queue / records / seed runners OK (0 failing); farm producer plain
  and ASan built, `test_farmfields_producer.py` 3 passed each (every case of the program); unit shadow built; sceneshot
  `--smoke` OK; `cities/build_models_tests.py` + `test_models.py` 31 passed.
- Phase 4 (13:27-14:19): `run_all.py` 8 suites OK (plain): render-host 213 passed, fow-scene 3, overlay-scene 3,
  shadow-ice-scene 6, water-scene 29, forest-scene 3, scenebundle-programs 4 programs (268 + 24 checks), scenebundle-py 30.
- No step ended 0xC0000142 (grep of every log).
- Main moved twice more: 8c90233aa (merged 14:2x as 91c85fc63; tools/release, docs, research only) and 8af774a02 (merged
  15:2x as 8ba612216, no conflicts; 0711's frame jobs and 0680's options changed `srv_render_scene.{h,cpp}`,
  `srv_render_host.*`, `srv_units_scene.*`, `options/`, `proxy/src/`). The branch's three hunks in `srv_render_scene.cpp`
  and its member line in the header are unchanged by it. Phase 6 (15:26-15:44) relinked on 8ba612216: build_server OK in
  155 s; farm producer built, 3 passed; build_proxy OK in 64 s, `test_farm_guard_proxy.py` 11/11; `shell\build.cmd` BUILD OK,
  `farm_guard_test` OK 667 checks; units + runner 7 passed; units extras 13 of 13; unitfeed, trigger scene built;
  `test_models.py` 30 + 1 failed (`reader_torn`, the timing case runbook 2.10 names), 31 passed on the rerun alone.
  Phase 7 (15:27-16:29): `run_all.py` the same 8 suites OK again (render-host 213 ... scenebundle-py 30).
- **Rule A** (phase 5, MC-B's slot 17:21-17:35, head f22311fd3 = 8ba612216's sources): `run_all.py --no-build --only
  offline-equals-live` on phase 7's builds with a sceneshot built into them, the C: library copy (scenelib check OK against
  the F: master, 16 bundles): **17 passed, 0 failed** in 792 s. As 13.8 says, the farm buildings are the EXE's own
  landmark instances and reach the server through the model tap; no library bundle was recorded with the edge placement,
  so the check shows that nothing else moved, not the buildings' new positions (those are 14.4-14.5's live probes).
- Trap for a slot: with `--no-build`, run_all needs every build of the suite in `--out`; offline-equals-live needs
  `sceneshot` (build it into `<out>/sceneshot` with `--c5render <out>/rt/c5render --common-objs <out>/sb/obj/common`
  before the slot; the slot is then the suite alone, about 13 min).

## 15. The tread filter: the EXE's landship tread effect dropped at the Particle object's type-0x28 listener (ticket 0784, D268, 2026-11-26, account B) [C for the reads and the code, L where cited]

Why (`../05-graphics-composition/unit-server-scene.md` 19.14, 19.14.1): with `ServerUnits = 1` the landship's scrolling
tread shell is a timed-trigger effect the EXE starts at its own hidden figure, so it parts from the server's hull in a
turn. The owner chose fallback C for beta 5 (D268): drop the EXE's two tread effects, draw none (the hull keeps its
moulded tracks; the treads stop scrolling). Plan B (the server draws them, ticket 0789) replaces it after beta 5 and
reuses this pass-through. Code: `shell/composite/farm_guard.{h,cpp}` (`TreadPass`, `TreadMaintain`, `TreadJobs`,
`FgTreadCheckPatterns`, `FgEffectNameHash`), compiled into the proxy (the owner) and the composite as the rest of the
guard; `proxy/src/proxy_farm_guard.cpp` (key `tread_filter`), `proxy/src/proxy_shell.cpp` (the option to
`VPSHELL_TREAD_FILTER`). Branch `impl-0784b`. Read with `llvm-objdump` 19.1.7 on the old build's analysis copy (PE stamp
0x546CD0A8); the new build only by the shape finder below, its addresses in the local table (D202).

### 15.1 The reads (old build 4390913) [C]

- **The listener.** The Particle start-up 0x67B360 (`[esp+4]` = the effect system, stored at `this + 0x9C`) calls the
  listener group 0x6A1C50 with the same `this`; the group registers, after four or five B-manager listeners, the object on
  the 4 MB A manager's types 0x28 (0x77DC60), 0x29, 0x2A, 0x2B, 0x31 (`effects-formats.md` 8.1). The main world's
  Particle object is the static 0xC8EB50 (the engine start-up at 0x4767C3: `ecx = 0xC8EB50`, argument the effect system
  0xDA9398, then the Projectile flag test); two other callers of the start-up (0x40B52D, 0x624694) start Particle
  objects inside other objects (scenes) [I: which scenes]. The group's unregister twin is 0x4A4B10 (reached by a `jmp`
  from the Particle shutdown 0x625110, whose main-object caller 0x7A22EA sits in the engine teardown 0x7A2200 called from
  0x77E712: the end of the process [I]).
- **The register** 0x86DD60 is farm guard's type-33 register (13.4) with one operand changed: the same lock-init byte
  0xCC733C and lock word 0xCBA9F0, list `[0x1A0E54C + index * 0x600]` (channel index + 0x14C = type 0x28's slot + 4).
- **The listener 0x77DC60: thiscall, one stack argument, `ret 4`**; it reads neither `eax` nor `edx` at entry. Its
  argument is the record's **payload** (record + 0x10): it copies 16 dwords (the matrix) from +0x00, the key from
  +0x40..+0x4C, the name hash from +0x50, the scale from +0x54 (8.1's table holds). When the object's busy bit
  (`this + 0x98` bit 0) is set it copies the payload into its deferred list (`this + 0x88`) with **its own address** as the
  replay function, so a deferred record is replayed by the listener itself, never through the node: a pass-through in the
  node sees each record once, before any deferral.
- **The posts.** The timed-trigger poster 0x4CE510 (allocator 0x846030, type 0x28) writes the key as the model id
  sign-extended (+0x40, +0x44) and the trigger's index sign-extended (+0x48, **+0x4C**), so +0x4C is 0 or 0xFFFFFFFF in
  every trigger post; the shell's effect player writes 'VP64' (0x56503634) there (`effects-formats.md` 15.7.3). The
  hash is 0x830890 over the trigger's event name (CRC-32 of 8.1): `ART_DEF_VEFFECT_TANK_TREADS` 0xE6690C27,
  `ART_DEF_VEFFECT_TANK_TREADS_02` 0xD5A8C88D (computed from the names; the same rule gives 8.1's 0x26800651 for the
  paradrop).
- **A lasting bone effect is re-posted every frame.** The clip's trigger walk 0x5D65B0 (called by the animation step
  0x753960's function up to twice a frame: once, or twice when the clip loops inside the frame) goes over the clip's
  triggers (0x200 bytes each, sorted by time) and fires each one whose [time, time + duration] overlaps the frame's time
  window; for an Effect trigger it calls the poster, which posts a fresh 0x28 with the same key and name each time (8.1's
  B2 move: the instance follows the bone). So a walking landship member posts two tread records a frame (two members:
  four); dropping every one means no instance is ever made, and one made before the swap ends by its own 0.02 s base
  duration once the posts stop [C for the code; I: which of the object's two time words is "now"].

### 15.2 As built [C]

- **The node.** The owner's watcher (every 2 ms) scans the A manager's type-0x28 list (slot 0x148 of the channel) for the
  node whose object is the main Particle object (`tf_particle_obj`) and whose function is the listener, and swaps its
  function word to the tread stub of the shared page, under the A manager's registration lock (one try a tick, a
  compare-and-swap against the listener's address; D228 base product: data only). Other Particle objects' nodes are left
  alone (they live inside other objects [I: freed with them]; a swapped node could not be removed by the EXE's
  unregister, section 1, and would call into a freed object).
  A node of an earlier registration beside a new one is retired to the inert word (F1's rule). `tread_filter=0`: the
  listener's address back. Unload: the stub's target goes to 0 first (it then jumps to the EXE's listener: stock), then
  the node gets the listener's address back when the lock is free (a held lock leaves the stub, which behaves as stock).
- **The shared page, version 3.** `FgShared::treadTarget`, `treadOrig`, `treadStub` (self + 0x1080), the edge stubs' form
  (`mov eax, [target]; test eax, eax; jnz +5; mov eax, [orig]; jmp eax`). A page of version 2 is not taken (it can only come
  from another proxy build in the same process).
- **The pass-through** (`TreadPass`, the EXE's main thread, the A-queue pump): reads the payload's +0x50 and +0x4C under
  SEH; a hash of one of the two names with +0x4C not 'VP64' returns at once (the record is dropped); every other record,
  an unreadable one included, goes to the EXE's listener with the same `ecx` and argument, outside any `__try`. While the
  key is off and the node not yet restored, every record is passed.
- **Sites.** Seven catalogue rows in `exebuild/symbols/shell.tsv` (`tf_particle_start`, `tf_particle_startup`,
  `tf_particle_obj`, `tf_l28_reg`, `tf_l28_register`, `tf_l28_listener`, `tf_a28_lists`) and five patterns
  (`kFgTreadSigs`): the engine start-up's call of the Particle start-up with the object, the start-up's head, the group's
  type-0x28 registration with its call of the register, the register (lock, lock-init, IAT slot, channel index, list operand),
  the listener's head (busy bit, own address re-queued, `ret 4`, the payload reads). `FgTreadCheckPatterns` also requires
  `tf_a28_lists` = `qg_channel_index` + 0x14C. A symbol not carried, a pattern that does not match or an unknown build:
  `farm_guard: LOUD: the tread filter (ticket 0784) is OFF on <build>: <reason> - the landship's tread effect is drawn by
  the game ...`; match: `farm_guard: the tread filter (ticket 0784): 5 byte patterns match (<build>) ...`.
- **Build 26577625** [C, 2026-11-26 00:3x, `gen_table.py` and a shape finder outside git,
  `F:/c5-64bit-work/0784b/scripts/find28.py`]: the generator carries `tf_particle_start`, `tf_particle_startup` and
  `tf_particle_obj` (high); it carries `tf_l28_register` to a **look-alike register with high confidence, wrongly** (as
  `fg_b48_register` in 14.2), and cannot carry `tf_l28_reg`, `tf_l28_listener`, `tf_a28_lists`. The finder locates the
  register by its full shape with that build's lock, lock-init, IAT slot and channel index and the list operand index +
  0x14C (one hit), its one caller (the registration {object, listener}), the listener from that site (its head and its
  own address re-queued as on 4390913), and checks that the carried start-up calls a group holding that site and that the
  carried start site passes the carried object; it writes four answers into the local `hand-review.tsv` (appended to the
  shared `C:/c5-64bit-work/exe-builds/26577625/hand-review.tsv` too). Then both tables are complete: 447 of 447 symbols,
  288 of 288 patterns OK on both golden EXEs. The new build's group registers one B-manager listener fewer before type
  0x28, which is why the group's pattern is only its 0x28 registration (a head-to-site pattern failed there).
- **Key and option.** `tread_filter` (default 0) in `[composite]` of `shell_composite.ini`, `VPSHELL_TREAD_FILTER` in
  the EXE's environment first, read by the proxy every second; it acts only while the composite is loaded. The proxy's
  boot (`proxy_shell.cpp`) sets `VPSHELL_TREAD_FILTER` = `[Renderer] HideTreadEffect` (default 1; options id 39, D255)
  when `ServerUnits = 1`; with `ServerUnits = 0` nothing is set and the EXE draws its units and their treads together.
- **Lines and counters** (`FgStats::tread*`): the key line of the proxy gains `tread_filter=N (from)`; `farm_guard:
  treads (ticket 0784): on: ...` / `off: ...` at each change; the swap, retire and restore lines; at most every 30 s while
  the dropped count moves `farm_guard: treads (ticket 0784): N type-0x28 records through the filter in this process:
  dropped D (ART_DEF_VEFFECT_TANK_TREADS a, ART_DEF_VEFFECT_TANK_TREADS_02 b), passed P (...)`; the in-game line ends
  `treads (0784) on|off|off for this build: type-0x28 records N, dropped D (a + b), passed P (effect player's treads t,
  unreadable u)`.
- **Cost** [I]: one SEH-guarded read of two dwords and two compares per type-0x28 record (every model timed-trigger effect
  of every animated unit, a few hundred a frame at most), plus a list scan of a few nodes every 2 ms on the watcher.

### 15.3 Tests [L offline, workstation, 2026-11-26]

`shell/tests/farm_guard_test.cpp` section 1 (both builds): the five patterns on a fake image, each broken alone, the
registration's call of the register moved (a look-alike), `tf_particle_obj` not carried, the world (slot 0x148, the
object, the lock), the list operand rule; section 18: a fake A list with two Particle objects (the main one and a scene's):
the key off touches nothing; on: only the main object's node is swapped, under the lock; the two tread names dropped, a
tread record with the VP64 tag and any other name passed with the same object and argument; a busy object defers a passed
record and replays it itself, a dropped one never reaches its deferred list; the in-game and "treads" lines; an
unreadable payload passed; a second registration retires the old node; a held lock writes nothing; key off restores; the
owner with the shared page's stub, the unload with the lock held (the stub reaches the EXE's listener: stock), the next
boot re-armed without a new swap, the unload with the lock free (the listener's word back). Results: 15.4.

### 15.4 Results [L offline, workstation, 2026-11-26; L live, laptop]

- On the 0.1b pins (before the merge of main): `farm_guard_test` OK 745 checks; `test_farm_guard.py` + `test_exe_tables_shell_b.py`
  36 passed; `test_farm_guard_proxy.py` 11/11 (62 checks plain and ASan); `test_options_boot` 13/13, `test_minimap_fog`
  15/15, `test_combat_hold` 15/15, `test_route_s` 9/9; `test_build_settings` + `test_purity_drops` 22 passed; `test_options`
  1141 passed in both trees, options ASan OK; installer `test_options_schema` 27 OK; `test_safety_static.py` 55 passed.
- After the merge of main 38b221c7d, on `keep/pins-5.5`: listed in the ticket's log (phase 2 of the hand-back).
- Rerun of `test_proxy_rt` on the branch's build `F:/c5-64bit-work/0784b/p2/server` (keep/pins-5.5, source @0a51447d5,
  impl-0784r, 2026-11-26, each through `quietlock.py run`, nothing else of its owner running, 5 to 6 other owners'
  heavy jobs registered) `[L]`: `--only-guard` (the smallest group holding the late-veto negative; the script has no
  per-case switch; the loader copied from impl-0784b's full-run scratch) 19/19, 297 s, 08:25-08:30 EST; the whole
  script 238/240, 1594 s, 08:30-08:57 EST, late-veto ok. The two failures passed in impl-0784b's run on the same build
  and look like load races of the test, not of the tread filter `[I]`: `d28: abrupt unload` (the loader still saw the
  server 2 s after the proxy's terminate) and `shell-fatal-flushhang` (rc 1: the loader's own "no server child process
  after the boot" check ended the process during the proxy's 1.5-s flush wait, before its exit 3; the 1504.8-ms wait
  was in range). Logs `F:/c5-64bit-work/0784r/{guard,full}.log`.
  The two cases alone (impl-0784r2, 2026-11-26 09:03-09:07 EST, same build and source, three separate runs of each case
  through `quietlock.py run`, 5 other owners' heavy jobs registered) `[L]`: `d28: abrupt unload` 3/3 passed (with its
  three companion checks, keep_runs=2 as in the full script) and `shell-fatal-flushhang` 3/3 passed (waits 1505.7,
  1501.5, 1514.9 ms). The script has no per-case switch, so a scratch driver (not committed) called the script's own
  helpers (`layout`, `OnDesktop`, `build`, `shell_manifest.write`) with the case's set-up lines. So the two failures of
  the whole run do not reproduce alone: they are load races of the whole run `[I]`, not faults of the build. Logs
  `F:/c5-64bit-work/0784r2/{d28,flushhang}-{1,2,3}.log`.
- **Live** (laptop tb1, VP 5.4.8, build 4390913, 2026-11-26 02:38-02:47 EST, job 20261126-024105-3b4c): the five patterns
  matched, the main Particle object's node (the only node on type 0x28) was swapped, and while a landship walked 364 tread
  records were dropped (182 of each name) of 48,093 type-0x28 records; no tread shell in the frames
  (`../05-graphics-composition/unit-server-scene.md` 19.14.2). Not run live on build 26577625.
