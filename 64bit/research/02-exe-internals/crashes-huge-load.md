# EXE crashes during the huge save's load (ticket 0410, 2026-11-16)

The silent EXE crash that 0404 met in one of nine huge product-path loads: where it is, what it read, why it shows no
box, and what the patch's error report saw. Addresses are DX11 VAs at ImageBase 0x400000 (EXE SHA-256 `b2ce96ae...` /
the instances' `04cfdc71...`, `.text` byte-identical, `06-tooling-and-workflows/headless-and-parallel-testing.md` 3.2).
Nothing from the EXE is in this file but addresses, offsets and a description of behaviour (D83); the decompiles and
the disassembly were read on a copy of the Ghidra project and in scratch (`C:/c5-64bit-work/0410`), not committed.

## 1. The crash (0404's run5nd) [L]

- 2026-11-16, instance c5h-g, save copy `C5H-0404P-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, c499d3c6..., turn 350), D33
  product path (ServerRenderer = 1, the composite, Overlay kept on), builds of main e76257f7 (server.exe 9a65b2e4...,
  proxy b3381fff..., x64 DLL 74588135..., composite 917b8f70...), the control arm without back-buffer dumps.
- The EXE (pid 47048, launched 05:16:54 EST) ended at **05:20:16 EST** with 0xC0000005 at **0x00F1C6DA**: Goldberg's
  crash printer (`<instance>/run/gbe/crashes.txt`, "10:20:16" UTC) is the only record. The composite's log put the EXE
  image at 0x00BA0000, so the RVA is 0x37C6DA and the **VA 0x77C6DA**. (The ticket's title says "image 0x00BA0000 +
  0x1DC6DA"; that sum is 0xD7C6DA. The RVA is 0x37C6DA; the VA 0x77C6DA is right.)
- When: in the load, 6.4 s after the proxy's view-mask watcher saw `GameViewState::StartupGame` set the view bits again
  (`vpproxy.log` 102.65 s, "watch (Updates 0)"; the server's clock puts its own `the engine is gone` at 108.90 s =
  05:20:16), i.e. at the start of the load gap in which queue B's records run LandmarkSystem's farm-field layout
  (`load-sequence.md` 1.1). `Lua.log` ends 05:20:12, `system_init.log` 05:20:11 with the `StartupGame` timers, no
  later line. The load screen's button never showed.
- The server logged `FAILED: the engine is gone: channel 2 serve() returned PeerDied` and `client pid 47048 exited
  (code 3221225477)` (= 0xC0000005) and ended with exit code 3. The dialog watcher logged `dialog_watch_end ... why
  process ended` at 05:20:25; no box at any poll.
- The game's own logs of that launch were still on c5h-g on 2026-11-16 05:50 and are kept in
  `F:/c5-64bit-work/0410/run5nd_gamelogs/` (APP, Database, Localization, Lua, modding, net_message_debug, stopwatch,
  system_init, xml, xml-perf, the instance's `crashlogs/instruments.log` and `run/gbe/crashes.txt`).

## 2. Where: the weld loop of the farm-field layout graph [C]

(Added 2026-11-18 by the MC at 0538's merge: the grid's cell id is 16 bits with 0xFFFF meaning "none", so on maps of 16,384 plots or more (grid above 65,535 cells) cell 65,535 reads as unfiled and its list breaks into a ring through the weld's refile [C, 9074's T1100 dump]: `farm-field-double-free.md` section 2. No standard VP map size reaches it; the huge save's crashes of section 8 are a different fault. Section 9's writes came from the same oversize saves.)

**0x77C6DA is inside 0x77C1C0** (Ghidra: body 0x77C1C0-0x77C777, thiscall, two stack arguments, `ret 8`), the
innermost EXE frame of the farm-field layout chain that `load-sequence.md` 1.1 already named from live stack samples of
the load gap: queue-B pump 0x785AF0 -> dispatcher 0x63ABA0 -> listener walk 0x8668E0 -> farm builder 0x748FC0
(`ART_DEF_IMPROVEMENT_FARM`) -> field layout 0x6016F0 -> 0x5362F0 -> 0x5B86D0 -> 0x77C1C0. Its callers (Ghidra caller
graph): 0x5B86D0 (twice), 0x5A9C90 (twice), 0x49CED0 (three times), 0x46F8B0.

**The object.** The farm builder runs on the static object at **0xBA20D0** (0x748FC0's `this`); the layout 0x6016F0
works on 0xBA20D0 + 0x154 = **0xBA2224** (the "0xBA2224" of `world-systems-and-stubs.md`'s Landmark row), and the
chain below it on **0xBA2228** (+4). Confirmed live [L, 2026-11-16, ticket 0410 `shell/evidence/0410/layout_peek.py`]:
read at the rebased 0xBA2228 during and after a load, the headers below hold the counts of a planar graph that grows
from 0 to about 9,080 endpoints and 13,950 nodes in the 50 s of the load gap.

The graph (offsets from 0xBA2228) [C, L for the counts]:

| Offset | What |
|---|---|
| +0x6C, +0x70 | the grid's world extent (x, y), floats |
| +0x74, +0x78 | the grid's origin (x, y) |
| +0x84, +0x88 | grid cells in x and y; +0x8C the cell array (24-byte cells: a list of nodes, a count, a dirty byte at +0x14) |
| +0x94..+0xA4 | **endpoint free-list array**: free head, live count, data, size, capacity. Entry 20 bytes = **four handles and a count**; bit 31 of the count word marks a freed entry, whose low 31 bits then hold the free list's next index |
| +0xA8..+0xB8 | a third free-list array (43,600 live on the huge save mid-load; not traced) |
| +0xBC..+0xCC | **node free-list array**: free head, live count, data, size, capacity. Entry 56 bytes (a polyline): +0x00 the polyline's point store, +0x08 / +0x0C its first and last segment, +0x14 / +0x16 the ids of its two endpoints (**16-bit**; 0xFFFF none), +0x18..+0x24 its bounding box, +0x28 / +0x2C the grid cell's list links (bit 31 of +0x2C marks a freed node), +0x30 / +0x34 the caller's tag and the grid cell id (16-bit), +0x36 a kind byte |

A **handle** in an endpoint's list is the node index in bits 0-27, bit 28 = which end of the node (set: the end stored
at +0x16), bits 29-30 a kind, bit 31 a flag. A freshly made endpoint's four slots are filled with 0x1FFFFFFF OR'ed onto
whatever the caller's stack held (bits 29-31 are not cleared), and only the first `count` slots are meaningful.

**What 0x77C1C0 does** (behaviour): it (re)files node N in the grid (the cell from the node's middle point (point count >> 1; corrected 2026-11-18 from "first point" by ticket 0542, farm-field-double-free.md 3.2), the cell's
list and dirty byte), recomputes N's bounding box over all its points, and then **welds the ends**: it copies the two
endpoint entries of N (the one at +0x16, then the one at +0x14) into two local four-slot lists on its stack, and for
each list walks `count` handles: the first handle whose kind (bits 29-30) is 0 gives the reference point (the first or
last point of that node's polyline, by bit 28), and every later such handle's node gets that point written into its
own first or last point. The walk is bounded only by the copied count; nothing checks it against 4.

**The faulting instruction** is the walk's read of the node record for a later handle with bit 28 set (the record's
+0x08, its first segment), at `node array + (handle & 0x0FFFFFFF) * 56`. The fault means that handle's index lies far
past the node array: **the walk read a word that was not a handle of a live node**. A handle of a live node cannot
fault there (the node array is one block, 0.75 MB on the huge save). Two ways get such a word into the walk [C for each
step, I for which one happened in run5nd]:

1. **A count above 4.** The walk then reads past the four slots: first the count word itself, then the other list,
   then 0x77C1C0's saved registers, return address and its callers' stack. Any word there with bits 29-30 clear is taken
   as a handle; a stack or code address gives an index of millions, a heap pointer in 0x10000000-0x1FFFFFFF also has bit
   28 set and faults exactly at 0x77C6DA. How a count passes 4: the node-splitting function **0x5A9C90** (insert a
   point into node N: a new node, a new endpoint between them) removes N's handle from N's old start endpoint (0x536540,
   by index) and then **appends** the new node's handle to that endpoint **without a capacity check**; the merge
   function **0x46F8B0** does the same (remove by index, then an unchecked append). The only creator that checks is
   0x554DB0 (both endpoints below 4), and 0x5B86D0 refuses only when both ends are already full. If the removal finds
   nothing (N is not listed in the endpoint its +0x14 names) and the endpoint holds four handles, the append writes the
   fifth handle over the count word (slot 4 = +0x10) and the count becomes that handle + 1.
2. **A slot past the real handles.** If the count is right but the endpoint's list is shorter than it says, the walk
   meets a never-filled slot. Its value 0x1FFFFFFF (with bits 29-31 from the stack) is the nil index with bit 28 set:
   when bits 29-30 happen to be clear it is taken as a handle and faults at exactly **0x77C6DA**
   (`array + 0x0FFFFFFF * 56` wraps to array + 0x7FFFFFC8). How the list gets shorter than its count was not found in the
   functions read; 0x536540's removal shifts the list down and decrements the count consistently [C].

Also in the code [C]: 0x46F8B0 frees the endpoint it merges away (count set to 0, then freed) without touching other
nodes that still name it; a node left naming a freed (and later reused) endpoint is the state in which 0x5A9C90's or
0x46F8B0's removal finds nothing (way 1). `layout_peek.py` counts exactly these states.

## 3. Reproduction (2026-11-16) [L]

**Setup.** Instance c5h-e (Goldberg, F:, EXE `04cfdc71...`), workstation under other teams' load (a functional run,
no quiet lock). Save `C5H-VP8P-HUGE_0350` (c499d3c6...) copied as `C5H-0410P-HUGE_0350` (product) and
`C5H-0410S-HUGE_0350` (stock). Product arm = 0404's recipe (run_0404.py / run_3308.py prep and restore imported
unchanged): D33 with Overlay kept on, ServerRenderer = 1, the composite; builds of main 5b5af9fd by 0404's recipe
(`shell/evidence/0410/build_all.sh`; loads by `shell/evidence/0410/series.sh`): server.exe fb5fcb1b..., proxy 7e6e0f3e..., x64 DLL 74588135... (the
pin's release DLL), composite e0a59d33... (proxy and composite sources unchanged since 0404's e76257f7; the bytes
differ by build stamps). Stock arm = the instance as provisioned: the pin's 32-bit `i0ship-26234371-ia` (3C2F47DA...),
stock config.ini, no proxy. Driver `shell/evidence/0410/run_0410.py`: one launch per load, the load screen closed at
its first Continue (as 0404), 20 s more watched, the harness's stop; every load (but the `n` runs) under an invasive
cdb (0164's command script: dump on any second-chance exception); the layout graph read every 3 s and after the load
(`layout_peek.py`); EXE memory every 2 s. Raw data `F:/c5-64bit-work/0410/<tag>/`.

| Arm | Loads | Debugger | EXE crashes | Graph after the load (layout_peek) |
|---|---|---|---|---|
| product | p01-p12 (06:06-07:18) | cdb | **0 of 12** | 8,968-9,164 endpoints, 13,687-14,023 nodes; no count above 4, no handle to a freed node, no node on a freed endpoint, no node missing from its endpoint in any load; p10 ended with one endpoint listing one node twice (a closed loop: both ends of one polyline on one endpoint) |
| stock | s01-s05 (07:19-07:49) | cdb | **0 of 5** | 8,984-9,135 endpoints; all clean |
| product | n01-n06 (08:00-08:35) | none (as run5nd) | **0 of 6** | 9,012-9,106 endpoints; all clean |

p01-p03 ran before the after-load check was added (p03 was sampled by hand every 3 s through its load: clean at the
end). One unrelated stop fault: s05's EXE took 51 s to end after the harness's TerminateProcess (the harness waits 15 s,
so `stop` returned 1); the machine was loaded.

Endpoint counts after a load are 1-4 (about 2,900 at 2, 2,550 at 3, 3,500 at 4: a third of the junctions are full).
The samples taken while the graph is being built show short-lived inconsistencies (1-21 samples per load); they are
torn reads of a structure the main thread is changing, and none survived to the next sample but p10's loop.
Each load (both arms) also takes about 120 handled first-chance access violations on one thread (cdb log), so they
are the EXE's own and not the composite's guarded reads.

**What this says.** 0404's run5nd stays the only crash at this address: across 0401's 4, 0404's 9 and these loads,
**1 crash in 31 product loads** (0401's 4, 0404's 9, these 18; 12 of them under cdb) and **0 in 5 stock loads**; no
gbe crash line, no WER record and no inconsistent graph after any of 0410's 23 loads. No address in any crash record
of the instances or of the owner's Steam install ends in ...C6DA other than run5nd's (all `run/gbe/crashes.txt` and
`crashlogs/` searched 2026-11-16). The rate is too low for 5 stock loads to show whether stock does it too (at 1 in 31,
5 loads see no crash with probability 0.85); the code is the stock EXE's and nothing of the product path writes the
layout's data [C: the taps only read, `shell/composite/decal_tap.cpp` header; the view-mask watcher writes only
`GameViewState+0x1B4`], so the product path is not shown to cause it. What the product path changes is the address
space: the EXE was at 0x00BA0000 in run5nd and at 0x003F0000 in every load here, the composite's sections and the
proxy sit in it, and whether a walk past the four slots meets a word that faults (a crash, as in run5nd) or one that
points into mapped memory (a silent wrong write into the graph or the heap) depends on that layout [I].
A layout seen as random per session (terrain-decals.md P6: `rand()` in the field styles) makes the trigger a matter of
which layout a session draws.

## 4. Why no box, and what the error report saw

**In stock the box comes from the 32-bit DLL, which the product path does not load into the EXE.** The VP DLL's
`CvGlobals::init` installs the process's top-level exception filter (`SetUnhandledExceptionFilter(CustomFilter)`,
`CvGameCoreDLL_Expansion2/CvGlobals.cpp` of the fork): it writes `crashlogs/crashes.log` and a minidump and shows the
**"Game Crash"** box for a crash in any module, the EXE's included ("The game has crashed for an unknown reason") [C,
source]. On the product path the game DLL slot holds the proxy and the 32-bit VP DLL is not in the EXE's process: the
module list of a product load (0410's p01, cdb `lm`/ModLoad lines, 2026-11-16) has the proxy and the composite under
`MODS/(1) Community Patch`, and the base game's `Assets/DLC/Expansion2/CvGameCore_Expansion2.dll` only loaded and
unloaded twice during the start [L]. Neither the proxy nor the composite installs an exception filter (no
`SetUnhandledExceptionFilter` / vectored handler in `proxy/src`, `shell/composite`, `shell/common`; only the server and
the proxy's own test loader have one) [C, 2026-11-16]. **Corrected 2026-11-16 (ticket 0419):** the proxy now installs a
chained top-level filter at its boot (`proxy/src/proxy_crash_filter.cpp`): it writes `<run>\exe-crash.txt` (code,
module + RVA, the VA at the EXE's ImageBase, thread) and hands on to the filter it replaced, so the EXE's filter and
Goldberg's printer below still run and still show no box; the server's report (0418) quotes the record
(`03-dll-engine-boundary/error-report-exe-crash.md` 10). The composite still has none. Was: "Neither the proxy nor the
composite installs an exception filter".

What is left in the EXE [C]: the Goldberg emulator's crash printer (`crash_printer/win.cpp` of gbe_fork e5df46e:
`SetUnhandledExceptionFilter` at its settings parse; on a crash it appends `[UTC time] Unhandled exception: code,
@address` to `crashes.txt` and calls the filter it replaced), and under it the EXE's own filter 0x42DEA0 (installed by
the static initialiser at 0x9C4ED0: `SteamAPI_SetMiniDumpComment`, `SteamAPI_WriteMiniDump` - an empty function in gbe
- then the filter that was there before it, else `EXCEPTION_CONTINUE_SEARCH`). On a Steam install the same EXE filter
writes Steam's `.mdmp`; neither shows a box. Windows did not either: for run5nd there is **no** Application event 1000,
no folder in `C:/ProgramData/Microsoft/Windows/WER/ReportArchive` (the last `AppCrash_CivilizationV_DX_*` is 0089's of
2026-11-11 01:52) and no `%LOCALAPPDATA%/CrashDumps` file [L, read 2026-11-16]; why WER was not reached is not
established [I: the earlier gbe-instance crash of 2026-11-10 18:06 left no record either, run-and-drive-the-game.md 9].
So on the product path **any** EXE crash is silent: no box, no dump; the gbe line exists only on test instances.

**The D118/D119 error report did not catch it [L, C].** The report (`proxy/src/proxy_report.*`, issue 3521) is written
by the proxy, inside the EXE, at a failure the proxy detects (pack check, server start, handshake, a server that dies or
fails a call); it has no path for the EXE's own death, which takes the proxy with it. The server saw the death
(`PeerDied`, the client's exit code 0xC0000005 in `server.log`), wrote only its own run diagnostics
(`server-report.txt` in its run folder, `SrvRemote::ReportAndExit`) and ended with exit code 3: no D119 report, no
popup; no `Documents/My Games/Sid Meier's Civilization 5/VP64/reports` folder exists on c5h-g [L, 2026-11-16 05:55].
The dialog watcher (D155) watches for windows; a crash without one ends as `dialog_watch_end ... process ended`, the
same line as a normal stop (**corrected 2026-11-16, ticket 0419:** the watcher now reads the exit code and records a
crash code without a box like a fatal box, `06-tooling-and-workflows/run-and-drive-the-game.md` 17.3), and the driver of 0404 probed a dead channel for 900 s (fixed in run_0404.py by a pid check).

## 5. Fix proposals (to the MC on 0410's thread, not made here)

1. **D118 for the EXE's own death (the gap this crash shows; any EXE crash on the product path is silent).**
   a. The server: when the vpipc peer dies and the client's exit code is an NTSTATUS error (0xC0000000 and above), it
      writes the D119 report itself (stage `exe-crash`, the exit code, the tails of its own and the proxy's logs, which
      outlive the EXE) and starts `vp64helper popup`; today it writes only `server-report.txt` and exits 3. The server
      outlives the EXE, so this path cannot be taken down by the crash.
   b. The proxy: a top-level exception filter (`SetUnhandledExceptionFilter`, installed at the proxy's boot; data
      only, no EXE code bytes, D4) that records the code, the faulting address as module + RVA and the EXE's base into a
      small file or the shared section for (a), then chains to the filter it replaced and returns its answer (Goldberg's
      printer and the EXE's own filter keep working). It must not write a dump of the EXE into the report (D119:
      privacy) and must do nothing that allocates on the crashed heap beyond one file write.
   c. The harness: `dialogwatch.py` and the drivers treat "process ended with an NTSTATUS exit code and no box" as a
      crash (the watcher's `dialog_watch_end` line records the exit code and posts the owner as for a fatal box), so a
      silent crash is never read as a normal end.
   d. (Added 2026-11-17, ticket 0515, section 8.4.) The server: also take the proxy's `exe-crash.txt` in its run folder
      as proof of an EXE crash, whatever the exit code; after WER's box and "Close program" the EXE ends with 255, which
      today is "not a crash code", so a player on a machine where WER shows its box gets Windows' box and no patch
      report. The dialog watcher can do the same for its ledger (a WerFault box after an `exe-crash.txt` is a crash).
      **Done for the server 2026-11-17 by ticket 0519** (`research/03-dll-engine-boundary/server-host.md` section 19):
      any exit code but 0 that is not a crash code is an EXE crash when the record names the EXE's pid and was written
      after it started; offline tests 185/185; live on the laptop 2026-11-18 (19.5): WER's box, "Close program", exit 255, the D119
      report and the patch's popup 1.8 s after the EXE's end. The "peer hung" trigger was not
      taken (19.2); the watcher's ledger part waits for a follow-up after epic 0398.
2. **The EXE's weld-loop crash itself** cannot be fixed in the EXE's code (CEG, D4). It sits in LandmarkSystem's
   farm-field layout, which the product path keeps on because the decal tap reads its records (route (a) of epic 0166).
   Options, in order of cost: accept it as an EXE fault (the stock game has the same code) and rely on 1 so the player
   gets the box and a report and can load again (the layout is random per session, terrain-decals.md P6, so a reload
   takes another layout); or, once the server lays out farm fields itself (increment 3), switch the EXE's layout off.
   No data-only lever on the layout was found.
   **Re-ranked 2026-11-17 (ticket 0515, sections 8.3, 8.5, 8.6)**: the defect is now located (a double free in the
   node delete, section 8.3) but is still EXE code reached by direct calls; no reliable data-only lever exists (8.6).
   Order: (i) item 1 first, now with 1d, because the crash happens in stock code and the player must at least get the
   patch's report and can reload; (ii) the server-side farm-field layout with the EXE's layout off (increment 3) as the
   fix; (iii) a main-thread free-list repair by data writes only as an experiment, if the rate of 8.5 makes the wait
   for (ii) too costly; it cannot undo the junctions handed out twice before it runs.
3. **Tooling.** `shell/evidence/0410/layout_peek.py` (read only) checks the graph after a load; a count above 4 or a
   node left on a freed endpoint is the precondition of this crash without the crash, and is worth a line in long
   soaks' end checks if the MC wants the rate measured over many loads.

## 6. Not verified (2026-11-16)

- **Which of the two ways of section 2 happened in run5nd.** No crash came again, so there is no dump: the frame of
  0x77C1C0 at the fault (the handle pointer against the two copied lists at esp+0x38 / esp+0x4C, their count words)
  would tell. `shell/evidence/0410/crash_cmds.txt` is the prepared cdb command list (`.ecxr`, the frame, the
  object's headers); the next crash's dump (any run with 0164's `cdb_watch.py` or 0410's driver) can be read with it.
- **Which way, answered for the later crashes (2026-11-17, ticket 0515, section 8.2)** [C]: the laptop's 0x536580 at a
  page start needs a count above 4 (way 1), and 0x77C70D (section 7) cannot come from a never-filled slot (way 2);
  run5nd itself stays undecided without a dump. (2026-11-18, ticket 0547, section 8.7: the first dump of the weld crash,
  stock route, 0x77C6E6, is way 1 [C]: a count of 0x2219 from the merge 0x46F8B0's unchecked append.)
- **How an endpoint loses a handle of a node that still names it: narrowed 2026-11-17 (ticket 0515, section 8.3).** The
  node delete 0x727420 frees a closed loop's endpoint twice when it holds only the loop's two handles; the free list
  then hands out live junctions, whose handles are overwritten while their nodes still name them [C link by link; I
  that the farm layout does it]. The node reversal at 0x52F1xx can also drop one of a closed loop's two handles [C].
  Was: "Candidates [I]: a closed loop ... meeting 0x536540's remove-by-index ...; or 0x46F8B0 freeing a junction that
  a third node still names. Neither was traced to a fifth append end to end." The 0x46F8B0 candidate was not followed
  further.
- **Why Windows' error reporting saw nothing** (no event 1000, no ReportArchive folder), while the 2026-11-11 crash of
  bug 0089 (in MSVCR90) was reported. (2026-11-17, ticket 0515, section 8.1: on the laptop WER did show its box and
  archive a report for the same kind of crash; on oldpc it did not; the machine setting was not read.)
- **Whether stock crashes here** at a rate like the product path's: 5 stock loads cannot show it (section 3).
  (2026-11-17, ticket 0515, section 8.5: 24 stock loads, 0 crashes, stock below 11.7 % at 95 %; product 3 in 57; the
  difference is not significant, p = 0.55. Still open; about 56 crash-free stock loads would separate them.)
- **Other EXE bases.** Every 0410 load had the EXE at 0x003F0000 (c5h-e); run5nd's was 0x00BA0000 (c5h-g).

## 7. A second crash in 0x77C1C0, at VA 0x77C70D (ticket 0435, 2026-11-17) [L]

- oldpc testbed job `20261117-170336-9fdb` (ticket 0435's device-flag series), instance tb1 (oldpc's own Steam EXE,
  `pure_vp` base, VT present), save copy `TBPP-HUGE_0350` of `TB-HUGE_0350` (c499d3c6..., turn 350), D33 product path
  (ServerRenderer = 1, the composite), a timing launch (no frame cap), GraphicsSettingsDX11.ini WaitForVSync 1, server
  key `render_prevent_driver_threading=0` (flags 0, the device as before 0435), config.ini LoggingEnabled 0. Builds:
  server.exe `1ca0e3db...` (0435 branch at 6dd5af4c, release), x64 DLL `29f58e17...` (pin layout
  `x64-04b1211b-pfdee656`), proxy `ef23cb98...`, composite `92cb7af4...`. The 14th launch of the job (arm R3-v0; the
  13 before it reached the map).
- The EXE (pid 11864, launched 18:11:15 EST) ended in the load with **0xC0000005 at CivilizationV_DX11.exe+0x37C70D
  (VA 0x0077C70D), thread 2400 (the proxy's boot thread = the EXE's main thread)**, no box; the proxy's crash record
  `<instance>\Documents\My Games\Sid Meier's Civilization 5\Logs\VP64\run\20261117-181203-11864-1\exe-crash.txt`; the
  dialog watcher's marker (kind `crash`, signature `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at
  CivilizationV_DX11.exe+0x37C70D`, id `5188729e1a4c`, first seen 18:13:03) kept in
  `F:/c5-64bit-work/0435/oldpc-dbbb/out/collected/run__dialog_open.json`. The load screen had not been passed (the
  driver's `ingame` polls 18:11:32-18:12:48 said not in game).
- **The same function as run5nd** [C, by the address only]: 0x77C70D lies inside 0x77C1C0 (body 0x77C1C0-0x77C777,
  section 2), 0x33 bytes after run5nd's 0x77C6DA; which instruction of the weld loop it is was not read (no decompile
  was opened for this note). So the farm-field layout crash has now been seen twice on the product path: 1 in 31 loads
  before (section 3), 1 in 14 loads of this job, at two instructions of one function [L]; no dump (the job ran without
  cdb).
- The job's driver stopped there (exit 1: its own summary then failed on a division by a zero mean, fixed); the
  instance was restored by hash and verified by a second job (`20261117-181650-dbbb`, rc 0). 0435's device flag is not
  involved (the arm drew with flags 0, and the crash is on the EXE's main thread in the load). 0435's driver now runs
  an arm again after such a crash.
- **Which instruction (added 2026-11-17, ticket 0515, section 8.2)** [C]: 0x77C70D is the weld walk's other branch: the
  read of the node record's +0x0C (its last segment) for a later handle with bit 28 **clear**; run5nd's 0x77C6DA is
  the same read of +0x08 for bit 28 set.

## 8. The third site, 0x536580, and the root-cause candidate: a double free (ticket 0515, 2026-11-17)

**Since 2026-11-18 (MC, at 0519's merge):** the double free was counted in live huge loads (ticket 0526: 0 in 24, 0 in 53 with 0515's checks; at most 5.5 % of loads; still the [I] root cause of the product's 3 crashes in 57) in `farm-field-double-free.md`; the interim guard on the farm builder (ticket 0527, always on in the composite) is in `farm-builder-guard.md`; the server's own layout that retires the builder is epic 0528 (D186). The crash stays open (D185) until 0528's option is in.

Read on a scratch copy of the instances' EXE (`04cfdc71...`, `.text` as section 1) with the Windows SDK's cdb (`-z`,
`u`) and a direct-call scan of `.text`; nothing of the EXE is in this file but addresses and behaviour (D83). Times EST.

### 8.1 The crash (0428's laptop job) [L]

- Laptop testbed job `20261117-202351-7fdf` (ticket 0428), instance tb1 (the laptop's own EXE, WER report: version
  1.0.3.279, timestamp 546cd0a8), huge save copy `TBPP-HUGE_0350` (c499d3c6..., turn 350), D33 product path. Pins batch
  3: server.exe `ed829bda...`, proxy `4815ed81...`, x64 DLL `2d737bd5...`; composite of main 1d502008e. Load 2 of 5;
  load 1 of the same job reached the map.
- The EXE (pid 12200, launched 20:28:41) ended in the load gap after StartupGame, before the server had a field,
  with **0xC0000005 at CivilizationV_DX11.exe+0x136580 (VA 0x00536580), a read of 0x51B82000 (a page start), on the
  EXE's main thread** (the proxy's boot thread 13176), EXE image at 0x00E40000, at 20:31:02.039 (the proxy's
  `exe-crash.txt`; gbe's `crashes.txt` line `0x00F76580` = the same address). The server logged `peer pid 12200 hung:
  no heartbeat for 5020 ms` at 82.1 s and nothing more before the logs were copied.
- **Windows' error reporting showed its box on the laptop**: WerFault.exe's "Sid Meier's Civilization V" with the one
  button "Close program" (the dialog watcher, 20:31:12; WER's `Report.wer`: APPCRASH, exception offset 0x00136580,
  "Sid Meier's Civilization V has stopped working"), and WER made a reflected copy of the process (a second
  CivilizationV_DX11.exe, pid 11804, child of 12200, created with WerFault). No dump was written (WER's archive holds
  only the report). The box stayed open until 0428's recovery job pressed "Close program" on MC #4721 (20:36:16); the
  EXE then ended with **exit code 255** (0x000000FF, the watcher's `dialog_watch_end`), not 0xC0000005, and its
  server.exe ended with it. Evidence copy: `F:/c5-64bit-work/0428/laptop-8cfa/out/` (recovery job
  `20261117-203535-8cfa`: `crash/` with exe-crash.txt, crashes.txt, Report.wer, the marker, the box's picture, the
  game's and the server's logs; `recover.json`, `steps.jsonl`).
- **Where WER shows a box** [L]: on the laptop it did (this crash, and 3516's two load crashes of 2026-11-14 on the same
  instance, `dialog_dismiss.jsonl`); on the workstation (run5nd) and on oldpc (section 7) the same kind of crash
  ended with no box at all. Why the hosts differ was not read (a per-machine WER setting is the likely place [I]).

### 8.2 What the three sites read [C]

- **0x536540 (remove-by-index; `this` = one endpoint entry, argument = a node index).** It walks the entry's handles
  for i below the count word's low 31 bits (bit 31, the freed mark, is masked off and not checked). At the first handle
  whose bits 0-27 equal the node index it shifts every later handle down by one slot, up to the count, decrements the
  count (bit 31 kept), and then goes on at i + 1, so the handle that was shifted into slot i is not looked at. Nothing
  bounds the count by 4.
- **0x536580 is the shift's read of slot j** (`entry + 4 j`). The only reads before it are the count word (+0x10) and
  slots 0..i, all on the entry's 20 bytes; a fault at a **page start** therefore means j ran past the end of a mapped
  region: **the count word was above 4** (at least 6 even if the entry ended on that page) and a matching handle had
  been found below it. Every shift step also writes slot j - 1, so the walk moved the heap above the entry down by 4
  bytes until it faulted. This is way 1 of section 2 (a count above 4), seen here directly; way 2 (a never-filled
  0x1FFFFFFF slot) cannot fault in 0x536540.
- **0x77C70D** (section 7) is the weld walk's read of +0x0C for a handle with bit 28 clear. A never-filled slot reads
  0x1FFFFFFF OR'ed with stack bits, which always has bit 28 set, so way 2 cannot produce 0x77C70D either; it needs a
  word that is not a handle (read past the four slots, way 1) or a slot holding a stale or foreign handle.
- So none of the three crashes needs way 2, and two of them cannot be way 2.

### 8.3 Who calls the remove, and the double free [C for the code, I that it happens in the farm layout]

Direct calls of 0x536540 (scan of `.text`): 0x46F9B3 (in the merge 0x46F8B0), 0x52F1B0 and 0x52F203 (in a node reversal,
start not read), 0x5A9FFB (in the split 0x5A9C90), 0x7274D5 and 0x7274DD (in the **node delete 0x727420**).

- **The reversal (0x52F1xx)** swaps the node's +0x14 and +0x16 endpoint ids, then for each of the two endpoints removes
  the node by index and appends one handle with the new end bit. Its counts balance when each endpoint held one
  handle of the node. On a closed loop (both ends on one endpoint E, two handles of one index) the two removes and two
  appends on E can leave **one** handle of the node where there were two: in a behavioural model of the code (scratch,
  not committed) 3 of 6 slot orders lost one (E = [a, N, N', b], [N, a, N'], [a, N, b, N']); E = [N, N'] and
  [a, b, N, N'] kept both. The endpoint then lacks a handle of a node that still names it twice [C for the code, the
  model for the orders].
- **The node delete 0x727420** removes the node from its +0x16 endpoint and from its +0x14 endpoint (0x536540 each),
  reads both endpoints' counts, and frees each endpoint whose count is 0 (the free-list free 0x5855A0 on the array at
  +0x94), then frees the node. It reads the +0x14 endpoint's count **before** it frees the +0x16 one and does not
  compare the two ids. On a closed loop whose endpoint holds nothing but the loop's two handles, the first remove takes
  one handle (the second, shifted into its slot, is skipped), the second remove takes the other, both counts read 0,
  and **the same endpoint is freed twice**.
- **The free 0x5855A0** sets bit 31 of the entry's count word and links it in: before the head when its index is
  below the head's, else right after the head (entry.next = head.next; head.next = entry), and decrements the live
  count. It does not look at bit 31 first. A second free of an entry that is already the head or the head's next
  writes **entry.next = entry**: a one-entry cycle, and a live count one too low.
- **The allocator 0x599460** takes the head (0x0FFFFFFF = empty: then it grows the array), sets the head to the
  entry's low 31 bits, copies the 20-byte template into the entry and clears bit 31. After the cycle the twice-freed
  entry is handed out, the head stays on it, the next allocation hands it out again and moves the head to whatever
  that now-live entry's count word holds (1 to 4), and every later allocation hands out **live junctions** whose
  index is the previous one's count: their four slots and count are overwritten with the template while the nodes
  that named them keep their ids.
- That is the state section 6 asked for: **endpoints that lack the handles of nodes that still name them**, and
  endpoints that list handles of nodes that never named them. The next split (0x5A9C90) or merge (0x46F8B0) on such a
  node finds nothing to remove and appends without a capacity check; on a full entry the fifth handle lands on the
  count word (section 2, way 1) and the count becomes a handle value, 0x10000000 or more when the end bit is set.
  The next remove-by-index on that entry runs to the end of the mapping (0x536580, this crash) and the next weld walk
  over it reads past its four slots (0x77C6DA, 0x77C70D).
- **The delete is in the farm chain itself** [C]: 0x5B86D0 (the frame between 0x5362F0 and the weld 0x77C1C0 in the
  load gap's stacks, section 2) creates a node with the creator 0x554DB0 and, when the new node's +0x10 word is 1 and
  its kind byte +0x36 is not 0, deletes it at once (call 0x727420 at 0x5B8EA4); otherwise it welds it (0x77C1C0 at
  0x5B8ECF). The creator takes the two endpoint ids from its caller and allocates a new endpoint only for an id of
  0xFFFF; it refuses (returns -1) when either given endpoint already holds four handles. Other callers of the delete:
  0x493D6C, 0x49D61D, 0x5C8740, 0x6061AF; the field layout 0x6016F0 also calls the free directly (0x601B14). Whether
  0x5B86D0 can be handed one endpoint id for both ends of a node it then deletes was not read. (Answered 2026-11-18 by
  ticket 0549, 8.9: no for the delete at 0x5B8EA4, whose node's +0x14 end is always a fresh endpoint; the creator can
  be handed one id for both ends of a piece it keeps; and the cleanup 0x493CF0's delete of a loop that kept one handle
  frees its endpoint twice without 8.3's isolated loop.)
- The chain from the double free to a crash is [C] link by link; that the farm layout deletes an isolated closed loop
  in a load is [I]: no dump and no live trace has shown it. A closed loop was seen live once (0410's p10, section 3).
  The free-list state is checkable after the fact: once the cycle exists the head never returns to 0x0FFFFFFF, the
  walk from the head meets a live entry, and live + freed != size. `shell/evidence/0515/layout_peek.py` (0410's checks
  plus that walk, read only) reports it as `endpoint_free_list.sound = false`.

### 8.4 The D118/D119 route for this crash [L for the logs, C for the server's code]

- The proxy's chained filter (0419) wrote `exe-crash.txt` (wording: "exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION
  at CivilizationV_DX11.exe+0x136580 (VA 0x00536580), thread 13176 (the proxy's boot thread)", then code, where,
  module base, rva, the VA at ImageBase 0x400000, "access: read of 0x51B82000", thread, process, time, "written by:
  the proxy's chained unhandled-exception filter (ticket 0419); no code bytes, no stack and no memory of the process
  (D83)") and a `LOUD: exe-crash:` line in `vpproxy.log` at 77.09 s.
- **No D119 report and no patch popup**, for two independent reasons: the test layout has `pack=0` in vpproxy.ini, so
  the server's `exe_crash_report=auto` is off by design (`srv_exe_crash.cpp` ReportEnabled); and even on a shipped
  pack the server checks the EXE's exit code when its channel fails, which after WER's "Close program" was **255**,
  not an exception code: `IsCrashCode(255)` is false ("not a crash code; no exe-crash report"). The server's log after
  the box was closed was not copied (the run folder stayed on the laptop), so its last lines are [I] from the code.
- **The shipped layout (pack=1) would not have shown the popup either** [C, from the code of main before 0519]
  (**Corrected 2026-11-17 by ticket 0519:** with 0519's server it would: the record proves the crash and the report and
  popup follow "Close program", `research/03-dll-engine-boundary/server-host.md` section 19): ReportEnabled would say yes,
  but Work() returns before it on the exit code 255; and while WER's box is up the EXE is alive and frozen, which the
  server only logs ("peer ... hung") and does not act on. So on a machine where WER shows its box, a player sees
  **Windows' "has stopped working" box and nothing of the patch**: the gap of section 5 item 1 is still open there. Proposal (section 5, 1d): the server also takes the
  proxy's `exe-crash.txt` in its run folder as proof of an EXE crash, whatever the exit code; the record is written
  before WER runs.
- The dialog watcher recorded the WER box (kind `unknown`, signature `WerFault.exe: Sid Meier's Civilization V`) and
  ended with `crash: false` (exit code 255), so its ledger counts a box, not a crash. (Fixed 2026-11-18 by ticket 0531
  in the harness: a WerFault box followed by exit 255, with the proxy's `exe-crash.txt` naming the game's pid, is now
  ledgered as a crash under the record's signature, `crash: true` with its code and address;
  `06-tooling-and-workflows/run-and-drive-the-game.md` 17.5. Offline tests only; the server side is 0519.) Since
  ticket 0532 (2026-11-18) the claim's owner also closes that box without the coordinator's override when the record
  is there: `cli.py dismiss --button "Close program" --fatal`, then `stop --after-fatal` (17.1, 17.5; COMMON rule 29).

### 8.5 The rate (oldpc, 2026-11-17)

**First stock-route occurrence [L, 2026-11-18 08:24:55 EST, reported by impl-4608 of epic 0398, queue #5465/#5467]:** a VP "Game Crash" box on c5h-h, sweep era-atomic on the **stock** route (save HI-KIT-ATOMIC-HUGE-T350, a standard Huge map, DLL pin i0ship-39f17f75-ia, no proxy), on the sweep's 9th in-process reload of the save, 43 s into the load: 0xC0000005 read of 0x00007377 at VA 0x77C6E6 (file offset 0x37BAE6) inside the weld 0x77C1C0, with the same chain as the product crashes (0x46F8B0 -> 0x6016F0 -> 0x748FC0 -> 0x8668E0 -> 0x63ABA0) [C by address]. Minidump 6da3af5c... at F:/c5-64bit-work/0398/4608/crash-era-atomic-stock/. So this crash class is the EXE's own on a standard map, not the patch's (stock was 0 of 24 in 0515's series; now at least one sighting). The repeated in-process reload is a new condition to note. Root cause still open (D185); ServerFarmFields retires the builder when on. **Read 2026-11-18 by ticket 0547 (8.7):** the dump shows way 1 directly (an endpoint count of 0x2219 written by the merge 0x46F8B0's unchecked append, called from the field layout at 0x601AF9); the open link is how the full junction came to lack the merged node's handle; the 9th load is not shown to matter.

(Added 2026-11-18 by the MC from ticket 0534: the counts below are huge loads only; they leave out 0517's product crash at VA 0x77C6DA in release-large's load of a Large scene (2026-11-18 01:18, `../06-tooling-and-workflows/crash-ledger-audit.md` row 6), and the stock side has shown the weld loop's read too (9074 T1100 run 3, 0x77C6DA, a 43-civ late-game save, row 4). 9074's T1100 hang walks this same layout graph with a sound endpoint free list (audit section 3): ticket 0538.)

**Setup.** oldpc testbed (Windows 10, the host's own Steam EXE `d2ccb491...`, `pure_vp` base with VT), huge save
`TB-HUGE_0350` (c499d3c6..., turn 350) copied per arm, one job per arm, functional runs (not timing: no quiet slot,
frame cap on). Driver `shell/evidence/0515/loads_0515.py` (one launch per load, `frontend.py dismiss` at the load
screen's first button, 20 s in game, the harness's stop; crash records copied before any box is closed, MC #4754);
the graph read with `shell/evidence/0515/layout_peek.py` at the button and again after the 20 s. Product arm: pins batch
3 (server.exe `ed829bda...`, x64 DLL `2d737bd5...`, proxy `4815ed81...`; layout `x64-4f4daac7-pf8e9f42`), composite
`ff72e6e6...` built from main 4cb853d65, D33 with Overlay on, ServerRenderer = 1, instance tb2. Stock arm: instance tb1
as provisioned, its 32-bit DLL `3c2f47da...` (the batch-2 pin's instance-aware build: a comparison arm under COMMON
18; the DLL does not touch the EXE's farm layout), no proxy layout, stock config. Per-load table:
`shell/evidence/0515/summary_0515.py` over the fetched `loads_0515.json` (raw: `F:/c5-64bit-work/0515/oldpc-<job>/`).

| Arm | Job | Loads | Crashes | Graph at the button and after the load |
|---|---|---|---|---|
| product | `20261117-205346-e04a` (21:16-21:45) | 10 | **0** | all clean, free list sound (61-81 freed entries, chain = freed, live + freed = size); 8,879-9,052 endpoints, 13,582-13,888 nodes; no count above 4, no node missing from its endpoint; EXE base 0x00570000 every load; 124-168 s to the map |
| stock | `20261117-205140-defb` (21:16-21:19) | 1 (+1 refused) | 0 | clean, sound (71 freed); load 2 refused by the launch gate's memory floor (4.3 GB free, floor 6 GB) because the product job ran beside it; the driver now waits for memory |
| stock | `20261117-214601-c14f` (21:46-22:34) | 18 | **0** | all clean, free list sound (54-81 freed); 8,866-9,120 endpoints, 13,566-14,039 nodes; EXE base 0x00D70000 every load; 117-126 s to the map; L12 ended with one closed loop (node 3319 with both ends on endpoint 2408, which also holds two other handles: the state of 0410's p10, not alone on its endpoint) |

**The rate, all huge loads so far (2026-11-17)** [L; pooled over hosts, builds and drivers, so a rough figure]:

| Arm | Loads | Crashes | Rate | 95 % interval (Clopper-Pearson) |
|---|---|---|---|---|
| product | 57: 0401 4, 0404 9, 0410 18, 0435 14, 0428 2 (laptop 7fdf), 0515 10 | 3 (run5nd 0x77C6DA, 0435 0x77C70D, 0428 0x536580) | 5.3 % | 1.1-14.6 % |
| stock | 24: 0410 5, 0515 19 | 0 | 0 % | 0-14.2 % (one-sided upper 11.7 %) |

Fisher's exact test, product 3/57 against stock 0/24: p = 0.55 (two-sided): **the data do not show that stock crashes
less**; with 0 crashes, about 56 stock loads would be needed to put stock's one-sided 95 % bound below the product's
5.3 % (`shell/evidence/0515/stats_0515.py 3 57 0 24`). Stock's rate is below 11.7 % at 95 %. This series of 0515 (10
product + 19 stock on one host) saw no crash; at 5.3 % per load 29 loads see none with probability 0.21.

**The graph after every one of these 29 loads was clean and its endpoint free list sound**: no count above 4, no node
missing from its endpoint or on a freed one, the walk from the head visiting exactly the freed entries (54-81 of
them) and ending at 0x0FFFFFFF, live + freed = size. Endpoints are freed in every load, but no load left the state a
double free leaves (it persists to the end of the session, section 8.3), so the trigger is rare: at most about 1 in
10 loads at 95 % from these 29 alone, consistent with a crash rate of a few per cent if most double frees end in a
crash [I]. Closed loops do occur in normal loads (0410's p10, 0515's stock L12), each time sharing its endpoint with
other handles, which the delete does not free.

### 8.6 A data-only lever? [I]

No reliable one was found. The double free, the free and the allocator are EXE code reached by direct calls (no
import slot, vtable or callback in between), and the inputs of the delete (the node's two endpoint ids) are written by
the same code. What data writes could do:
- **Repair the free list from outside the layout code** (the composite or the proxy on the EXE's main thread, e.g. at
  the Present hook during the load screen): walk it, and when it is unsound rebuild it from the bit-31 marks and
  correct the live count. This repairs only the list: entries already handed out twice stay shared, and in the load gap
  the layout performs many operations between two frames, so the second hand-out (the first damage) most likely comes
  before any repair point [I]. A repair from another thread would race the main thread: excluded.
- **Keep the EXE's farm-field layout from running** (an art or game-data change so that the farm builder lays out no
  fields): a data change, not an EXE code change, but it changes what the stock picture shows (D66) and was not looked
  for here.
- The structural fix stays the one of section 5 item 2: the server lays out farm fields itself (increment 3) and the
  EXE's layout is switched off.

### 8.7 The first dump of the weld crash: the stock route's 0x77C6E6 (ticket 0547, 2026-11-18)

Read only: the VP DLL's minidump of 8.5's stock sighting (`CvMiniDump_20261118_082455_5.4.8-44-g39f17f7545+ia440878983_Release.dmp`,
SHA-256 `6da3af5c...354e`, 1,876,165 B, in `F:/c5-64bit-work/0398/4608/crash-era-atomic-stock/20261118_082624_pid50196/`
with the DLL's `crashes.log`, `queueguard.log` and `instruments.log`) with the SDK's x86 `cdb -z` on a scratch copy, and a
static read of the functions on a scratch copy of c5h-h's EXE (`04cfdc71...`, the instances' EXE of section 1). No
process touched, no game run. The dump holds registers, stacks and modules only (no heap, no `.data`): node and endpoint
records are not in it, so everything below comes from the registers, the stack frames and arithmetic on them. Scripts:
`tools/harness/evidence/0547/` (`dump_read.sh`, `static_read.sh`, `sessions_0547.py`); their output stays in scratch
(`C:/c5-64bit-work/0547`, D83). Addresses are VAs at ImageBase 0x400000; the EXE sat at 0x00AC0000 in this process (VA =
live - 0x6C0000). Times EST.

**The run [L].** c5h-h (Goldberg, F:), stock route (no proxy), DLL `Release-5.4.8-44-g39f17f7545+ia440878983` (pin batch
3's instance-aware build `i0ship-39f17f75-ia`, CP v151), mods cp, vp, eui, save `HI-KIT-ATOMIC-HUGE-T350` (turn 350, map
size not read from the save), epic 0398's HI sweep `era-atomic` (impl-4608; `F:/c5-64bit-work/0398/phase/i2-rc1/sweeps/stock/era-atomic/`).
EXE pid 50196, launched 07:40:40; **the crash was in the process's 9th load of this save**: the harness log's setups and
`queueguard.log`'s per-load drop lines give 8 completed loads in this process (07:44, 07:51, 07:55, 07:57, 08:09, 08:11,
08:18, 08:20, each a sweep reload to turn 350), and the 9th, started when session s12 ended at 08:24:12, faulted at
08:24:55, 43 s in. Main thread (thread 0x735C; its stack ends in the CRT start 0x9270D2 and `BaseThreadInitThunk`).

**The stack [C, return addresses in the dump's stack].** weld 0x77C1C0 (fault at 0x77C6E6) <- **the merge 0x46F8B0** (return
0x46FACF) <- the field layout 0x6016F0 (return 0x601AFE) <- farm builder 0x748FC0 (return 0x7490B3) <- listener walk
0x8668E0 (0x866919) <- dispatcher 0x63ABA0 (0x63ABD4) <- queue-B pump 0x785AF0 (0x785BA7) <- 0x4B402E <- 0x81DCB5 <-
0x602477 <- 0x926EB5. The same farm-builder chain as sections 2 and 8, but the weld is reached **from the merge, which
the field layout calls directly** (call at 0x601AF9), not through 0x5362F0 -> 0x5B86D0 as in the live stack samples of
section 2. (The ticket's "0x5A9FFB, 0x7274D5/DD" are the split's and the node delete's calls of the remove-by-index
0x536540, section 8.3, not crash sites; this crash passed through the merge's own remove call at 0x46F9B3.)

**The faulting instruction [C].** 0x77C6E6 is three instructions after run5nd's 0x77C6DA, in the same branch of the
weld's handle walk (a later handle, bit 28 set): 0x77C6DA reads the node record's +0x08 (its first segment), 0x77C6E4
reads the record's +0x00 (the pointer to its point store), and 0x77C6E6 reads that store's +0x08. At the fault eax =
0x4893B9C4 (the "node record"), edx = **0x0000736F** (that record's +0x00), so the read is 0x736F + 8 = **0x7377**.
0x736F is not a field of the graph: it is whatever word lies at 0x4893B9C4, an address the walk computed from a word
that is not a handle (below). Which instruction faults (0x77C6DA when the computed record is unmapped, 0x77C6E6 when it
is mapped but its first word is not a pointer) depends only on the address space; it is one fault.

**The weld's frame [C].** The weld was called as weld(node **0x2218** (8,728), {tag 0x00012745, cell word 0x0001FFFF}):
the cell id is **0xFFFF**, because the merge unfiles both nodes (+0x34 = 0xFFFF) before it calls the weld, so the weld
files node 0x2218 anew by its first point. Its two copied endpoint lists (the endpoint at node +0x16 at esp+0x38, the
one at +0x14 at esp+0x4C) are **byte for byte the same**:

| Slot | 0 | 1 | 2 | 3 | count word (slot 4) |
|---|---|---|---|---|---|
| esp+0x38 and esp+0x4C | 0x8000005D | 0x9000221D | 0x10002218 | 0x00002211 | **0x00002219** |

The walk was on the first list (edi = esp+0x48, its count word) at **step 10** (esi = 10, slot pointer esp+0x60): the
count 0x2219 = 8,729 let it read past the four slots, through the count word (step 4), the second list (steps 5-9) and
on into the frame. Step 10's word, at esp+0x60, is **0x9C389C2C**, a stale stack word: the address of node 0x2037's
record, left by the merge (the weld's saved esi holds the same value). Taken as a handle (bits 29-30 clear, bit 28 set)
it gave the index 0x0C389C2C, and node array + 0x0C389C2C x 56 wraps to **0x4893B9C4**, exactly the eax of the fault: the
node array starts at **0x9C319024** (node 0x2218's record 0x9C390564 and node 0x2037's 0x9C389C2C fit it to the byte).
Before the fault the walk wrote the reference point (node 0x5D's end, (-849.09, -91.31)) into the ends of nodes 0x221D,
0x2218 and 0x2211, and also into node **0x2219**, named only by the count word, twice: a silent wrong write into a node
that is not on this endpoint [C]. With step 10's address unmapped or its first word a pointer, the walk would have gone
on writing through the stack and wherever its words point [I].

**Where the count 0x2219 came from: the merge's unchecked append (section 2, way 1), now seen in a dump [C].** The merge
0x46F8B0 (A, B), from its code: it unfiles A and B from their cells, sets the count of A's +0x14 endpoint J to 0 and frees
J, sets A's +0x14 to B's +0x14 (call it S), removes B from S by index (0x536540 at 0x46F9B3), **appends A's handle (A's
index, bit 28 clear) at slot `count` of S with no capacity check** (0x46F9EA) and adds 1 to the count, joins the two
polylines into A, frees a node record (0x61AA00 on the node array: B's, traced 2026-11-18 by ticket 0549, 8.9; it keeps B's ids) and welds A. Here A = 0x2218 and B = 0x2037 (the merge's arguments in its
frame). The appended handle 0x00002218 sits in slot 4, which is the count word, and the count became 0x2218 + 1 =
0x2219: so S held **exactly four handles when the append ran, and none of them was B's**, though B named S. (S cannot
have held five with B among them: a removal from five would have shifted the count word's value into slot 3.) And since
both copies are the same entry [I: two entries cannot both hold this overflowed count], A's +0x16 endpoint is S as well: the merge made A **a closed loop on a full junction**,
whose slot 2 (0x10002218) is A's own other end. S's other handles are nodes 0x5D, 0x221D and 0x2211.

**What made S lack B [C for the call site, I for the mechanism].** The field layout's step at 0x601A2A-0x601B14 calls
0x5362F0 twice at one junction (edi), with the far end 0xFFFF, the first time with flag 0, the second with flag 1;
0x5362F0 has 0x5B86D0 fill the vector at layout + 0x1A0 with the nodes it makes, runs the node reversal 0x52F0E0 on each
of them when the flag is 1, and returns their number. The layout keeps the first node of the first call, and merges the
first node of the second call (A, reversed) with it (B), then frees the junction edi if its count is 0. So A and B are
the two nodes this step had just made, and **the state that crashed (S full, without B's handle, while B names it) was
made inside this one farm-builder step**, not left over from an earlier load [I: that the vector's first entries are the
nodes made by these calls is read from 0x5362F0; that they were new nodes, not existing nodes that 0x5B86D0 reused, is
not]. The two nodes running between the same two junctions (A: J to S, B: S to J) would be the two halves of one closed
field outline that this step joins into a loop. Candidates for how S ended up without B's handle [I, each]:
1. S already held four handles when B (or A) was attached to it, and the attaching path named S in the node without
   appending (or its append was undone): a junction where five field lines meet. 0x5B86D0 refuses only when both ends
   are full (section 2), so a node with one full end is made.
2. The double free of section 8.3: S's entry was handed out twice and overwritten while B still named it.
3. The node reversal 0x52F0E0 (run on A in this very step) or a split dropping a handle (section 8.3's reversal on a
   closed loop).

(Read 2026-11-18 by ticket 0549, 8.9: no path names a junction without appending (1 as worded is excluded; the
creator's own closed loop at count 3 is a separate way-1 route with another signature); A was not a loop when
reversed, but the reversal and the split do drop a handle of a closed loop (3) and that feeds the double free (2); S's
slots say its entry was handed out again by the allocator while B's handle was on it, which needs an unsound free list:
2 is the reading.)
What would decide: a full-memory dump of the next occurrence (WER LocalDumps with DumpType 2 on the test instance, or a
cdb `.dump /ma` from a non-invasive attach; the DLL writes only a minidump; since ticket 0548 the harness takes the
latter at VP's box, 8.8) to read nodes 0x2037 and 0x2218 (+0x14, +0x16),
S's index and its handles, every node naming S, and the endpoint free list (`layout_peek.py`'s soundness rule: an unsound
list means candidate 2); or a read-only trace of this call site (S's count and handles before 0x46F9B3) in a load series.

**Against the other sightings [C for the sites, the ways from this file and `farm-field-double-free.md` 2.5]:**

| Crash | Route, map, load | Site | What the walk or remove read | Dump |
|---|---|---|---|---|
| run5nd (section 1) | product, huge save, 1st load of the process | 0x77C6DA (weld, bit 28 set) | not known (way 1 or 2) | none |
| 0435 (section 7) | product, huge save, 1st load | 0x77C70D (weld, bit 28 clear) | not way 2; way 1 or a stale handle | none |
| 0428 (8.1) | product, huge save, 1st load | 0x536580 (remove-by-index's shift) | a count above 4 (way 1) | none |
| 9074 T1100 run 3 (audit row 4) | stock, 43-civ oversize map (180 x 112) | 0x77C6DA | a never-filled slot 0x1FFFFFFF (way 2) | mini |
| 0517 (audit row 6) | product, Large scene | 0x77C6DA | not known | none |
| **this one** | **stock, huge save, 9th load of the process** | **0x77C6E6** (weld, bit 28 set) | **a count of 0x2219 from the merge's append: way 1** | mini |

So the first crash of this class with a dump on a standard-size save shows **way 1 through the merge 0x46F8B0** called
from the field layout at 0x601AF9, the precondition being an endpoint that lacks a handle of a node that names it
(section 8.3's state). The cell-id overflow of `farm-field-double-free.md` 2 needs 16,384 plots or more and does not
apply to a Huge (128 x 80) save [C for the sizes; I that this kit save is the standard Huge size, as 8.5 says]. 9074's
way-2 crash on the oversize map stays a different read of the same walk. The fault is the stock EXE's on the stock route:
nothing of the patch was in this process.

**The condition: the 9th in-process load [C for the code, I for the order].** The graph does not carry over between
loads: `TerrainSystem::ShutdownGame` 0x6684D0, run by the game shutdown/reinit 0x580C50 when a game is left, calls 0x435870 on
the graph 0xBA2228, which unlinks and frees the grid, and sets the free heads of the node (+0xBC), endpoint (+0x94) and
third (+0xA8) arrays to 0x0FFFFFFF and their live counts and sizes to 0 [C]; that 0x580C50 runs before an in-game load is
[I], backed by `research/sources/shell-gates/stubs/terrain.md` row 3 (with that shutdown skipped, a second in-process load
stacks on top [L]). The node ids here (0x5D-0x221D, at most 8,733) lie within one huge load's range (13,566-14,039 nodes
at its end, 8.5) [C for the ids]. What does carry over [C, code read; effect I]: the arrays' memory (data pointers and
capacities are kept, entries are not zeroed; the allocator overwrites an endpoint with its template when it hands it
out, 8.3); and the table that 0x432220 (GameView startup, 0x623280) fills at 0xBA2224 + 0x1AC with 50 random entries of
48 bytes per call, with no reset in 0x432220, the shutdown 0x490A10 or its 0x6FA350 (which only unregisters the four
listeners), so it grows by 50 per load [C for the three functions read; I that nothing else resets it and what reading
it does to the layout]. The 0527 guard's swap line (0 nodes and 0 endpoints live at the swap) was seen on first loads
only; no count at the start of an in-process reload has been read [L gap]. The process was not short of memory for the
layout: 2,504,856 KB committed, 1,074,760 KB free, the largest free block 978,060 KB at 0xB999D000 (the DLL's
`crashes.log`; below 2 GB only 44,104 KB free); the DLL's
"near-NULL ... low address space may have failed an allocation" class and impl-4608's "32-bit address space exhausted"
come from the DLL's rule for reads below 64 KB, and this read's address came from a wild index, not a failed allocation
[C]. The 52,940 records `queueguard.log` dropped over the 8 earlier loads (6,346-7,023 per load) are the 4 MB queue's
decal records, the builders' output; the farm builder's input is the second queue (`message-queue-and-fixed-buffers.md`),
so the drops do not reach the graph [C for the queues, I for no effect]. So the reload is not shown to be needed: the
product's three huge-save crashes were each in the first load of a process. Whether repeated reloads raise the rate is
open; this is one sighting.

**For the MC (D185).** The cause is now [C] down to: the field layout merges two nodes at a junction that was full and
lacked one of them, and the merge's unchecked append overflows the four slots; the walk that trusts the count does the
rest. The open link is how the junction came to lack the handle (the three candidates above). The routes of 8.6 and
9.4 do not change: 0528's ServerFarmFields retires the builder on the product route (this merge call at 0x601AF9 is in its
field layout [C for this stack; other callers of 0x6016F0 not read]); 0527's guard checks counts after each builder call, which is too late for this
crash, because the overflow and the fault fall in one call. The stock route has no lever of ours: it is the stock EXE
with VP's farm density. Not added to 8.5's rate table (a different save, and HI-sweep reloads rather than fresh loads).

### 8.8 The full dump of the next occurrence: the harness takes it at VP's box (ticket 0548, 2026-11-18)

8.7's "what would decide" needs the heap (nodes 0x2037 and 0x2218, the junction S, every node naming S, the endpoint
free list), which the VP DLL's minidump does not hold. On the stock route the crash shows VP's **"Game Crash"** box
(section 4: the DLL's top-level filter writes the minidump and then shows it), and the process stays alive while the box
is open. Since 0548, `crash.collect` (`tools/harness/crash.py`) takes the full dump there:

- **When [C, code and tests].** No WER hold (0543's rule, `crash-load-8e81c3.md` 6.1, comes first), and
  `crash.dialog_state(pid)` (the EXE and the processes it started, so the server's DLL boxes too) shows a box that
  `dialogwatch.classify` calls **fatal** (COMMON rule 29's table: title "Game Crash"; "Error" with "the game will now
  crash"; "Render Error"; the C runtime's "Runtime Error!" box; each with the one button OK), and the box's process
  runs. Continuable asserts, the patch's popup, WER's own box and every box off the table are not dumped by this rule.
- **What [C].** The process that owns the box (the EXE on the stock route; server.exe for the server's DLL) is dumped by
  0543's `dump_wer_hold`: `cdb -pv -p PID -c '.dump /ma "<run>\crash\<stamp>_pid<game pid>\full.dmp"; qd'` (non-invasive,
  read only; never a breakpoint or debug register, D65), the same refusals (the owner's real installs, a folder in the
  repository, less than the dump's size + 25 GB free, no cdb), the same 40 s time box (cdb is left to finish on a
  timeout), once per process (a later collect, a second box of the same process included, records `already`). The
  record (`report.wer_dump`, `manifest.json`, `wer_dump.json` in the crash folder, and the marker's `crash.wer_dump`, which
  is the box's record) carries `reason: fatal_box` and the box (title, text, buttons, hwnd, pid, process); 0543's dumps
  carry `reason: wer_hold`.
- **Before any dismiss [C].** The dialog watcher's first sighting of a box and `launch.dismiss_dialog`'s evidence step
  both run collect before anything is pressed, so the dump is taken while the box holds the process, and the owner then
  closes the box by COMMON 29's path (`dismiss --button OK --fatal`, then `stop --after-fatal`) as before.
- **Live stand-in [L, 2026-11-18 09:16 EST, workstation, `tools/harness/evidence/0548/live_standin.py`].** A 64-bit python
  stand-in showing a real MessageBox "Game Crash" (OK) on a hidden desktop: the real reader saw it, `classify` called it
  fatal, collect wrote a 157.6 MB full dump with the x64 cdb in 5.5 s, the stand-in and its box were still there after
  the dump, a second collect said `already`, `cdb -z` opened the dump and named the stand-in's pid; a second stand-in with
  an "Assertion Failed" OK/Cancel box was read as continuable and not dumped. (0543's stand-in: 30 MB in 4.3 s.)
- **Not verified.** VP's real box in the 32-bit EXE: the x86 cdb on a process of about 2.5 GB committed (8.7's
  `crashes.log`), with the faulting thread inside the DLL's filter. The two stand-in points suggest a few seconds of attach
  plus roughly 100 MB/s, so a 2.5 GB dump would take about 25-30 s, inside the 40 s box but near it [I]; on a timeout the
  dump still completes (the record says `timeout`, `full.dmp.cdb.log` says whether cdb finished). A non-invasive dump has
  no exception record (`.ecxr` does not work); the faulting context is on the faulting thread's stack inside the filter
  (the EXCEPTION_POINTERS argument), and the DLL's own minidump still holds it [I].

### 8.9 Static read of 8.7's candidates 1 and 3 (ticket 0549, 2026-11-18)

Read only: `cdb -z` on a scratch copy of the instances' EXE (`04cfdc71...`, copied from c5h-h's `install/` into
`C:/c5-64bit-work/0549`), functions listed with `uf`; no game run, no process touched, no debug register. Scripts in
`tools/harness/evidence/0549/` (`static_read.sh`, `callees.sh`; their output stays in scratch, D83) and a behavioural
model of the endpoint lists written from this file's description, `model_0549.py` (`python -I model_0549.py`, exit 0).
Addresses are VAs at ImageBase 0x400000. "Graph" is 0xBA2228, "layout" 0xBA2224 (graph - 4; the layout's
`[ebp+0xA0]` is the graph's endpoint data +0x9C). Handle end bits: **S** = bit 28 set (the node's +0x16 end, its first
point), **C** = clear (+0x14, its last point).

**The functions, as far as the ticket needs them [C].**

- **Field layout step 0x6016F0, work items of kind 0 and 1 (0x601965-0x601B14).** It increments the layout's counter
  `[0xBA2224]` (that value is this step's **tag**, passed to both calls), allocates a **fresh seed endpoint** `edi`
  (0x6019B1, count 0), clears the output vector (layout + 0x1A4 = 0) before **each** call of 0x5362F0, and calls it twice
  from `edi` with end 0xFFFF: first with flag 0 (0x601A39), then with flag 1 (0x601AD8). B = the first node of the first
  call, A = the first node of the second call; merge(A, B) at 0x601AF9 only when both calls made a node; then frees
  `edi` when its count is 0 (0x601B07-0x601B14). Work items of kind 2 (0x6017C9-0x6018C3) extend from an existing
  endpoint that holds exactly one handle of the item's kind: one call of 0x5362F0, flag 1 when the item's byte is 8, no
  merge. After the work loop, 0x6016F0 calls the cleanup **0x493CF0** (0x601B36).
- **0x5362F0** (this = layout; polyline, tag, start junction, end junction, flag): calls 0x5B86D0, then for each node in
  the output vector, in order, the reversal 0x52F0E0 when the flag is 1 (0x536384), and returns the vector's count.
- **0x5B86D0** (this = graph; polyline, vector of touched nodes, output vector, {tag, ...}, start a5, end a6) lays the
  polyline into the planar graph piece by piece. Per segment it collects crossings with existing nodes (0x4072C0: record
  of 28 bytes, +0 kind, +4 crossed node, +8 its segment, +0xC **the crossed node's tag** (+0x30), +0x10.. the point),
  sorts them, drops crossings whose tag is **this step's own tag**, or equal to the previously used crossing's tag or to
  the previous list entry's tag (0x5B88C7-0x5B88E4), and takes the first one left:
  - kind 1: the split 0x5A9C90 at the crossing (0x5B8A22) gives the junction J; **then** the checks on J's count c:
    c = 4 and a5's count = 4: stop (0x5B8A51-0x5B8A60); c = 3: stop after this piece (0x5B8A6B); handles at J whose
    node has the step's kind byte: 2 = stop now, without a piece (0x5B8ACC), 1 = stop after this piece;
  - kind 2 or 4: a new endpoint (0x5B8B17), no split;
  - then the creator 0x554DB0(range, **a5, J**) (0x5B8B64); -1 ends the call. The piece goes into the output vector,
    the split's two nodes into the touched vector, and the piece is welded (0x5B8C8C) with the step's tag. Then
    **a5 := J** (0x5B8D0C) and the scan goes on from the crossing point.
  - The last piece: creator(range, a5, a6) at 0x5B8E34; in every layout call a6 = 0xFFFF, so its +0x14 end is a fresh
    endpoint. If that node's +0x10 word is 1 and its kind byte is not 0 it is deleted at once (0x727420 at 0x5B8EA4),
    else pushed and welded (0x5B8ECF).
- **The creator 0x554DB0** (begin, end, startJ, endJ, flags): allocates an endpoint for each id of 0xFFFF **first**, then
  refuses (-1) if startJ's count >= 4 or endJ's count >= 4 (0x554E90, 0x554EAB; an endpoint it allocated is then left
  live with count 0), else allocates the node and appends **S to startJ** (0x554F96) and **C to endJ** (0x554FD2), each
  at slot `count` with no further check, and stores +0x14 = endJ (0x554FEF), +0x16 = startJ (0x554FF3). It does not
  compare startJ with endJ.
- **The split 0x5A9C90** (node N, segment, point, out junction): if the point is within its epsilon of N's last point it
  returns N's **+0x14 endpoint** in `out` and 0x0FFFFFFF (no new node); of N's first point, N's **+0x16 endpoint**
  (0x5A9D21, 0x5A9D79). Otherwise a new node M (N's tag and kind) and a new endpoint E (count 0); P = N's old +0x14.
  It finds N's handle in P (0x7804C0, for its kind bits), **removes N from P by index** (0x5A9FFB), sets N.+0x14 = E
  and appends N:C to E (0x5AA00F-0x5AA03F), sets M.+0x16 = E and appends M:S to E (0x5AA042-0x5AA080), sets
  M.+0x14 = P and appends M:C to **P with no capacity check** (0x5AA087-0x5AA0CD), welds N and M, `out` = E, returns M.
  The flag bits 31 (and 28 on M:S) of these handles are taken from the address of M's record + 0x30 (0x5A9E5D), so on
  a heap above 0x90000000 the two handles of a fresh split junction read **0x8000nnnn (N) and 0x9000mmmm (M)**.
- **The reversal 0x52F0E0** (node R): reverses the point order, **swaps +0x14 and +0x16** (0x52F168-0x52F177), finds R's
  handle in each endpoint (0x7804C0, kind bits), then on the new +0x16 endpoint removes R by index and appends R:S
  (0x52F1B0-0x52F200), and on the new +0x14 endpoint removes R by index and appends R:C (0x52F203-0x52F241). So it
  rewrites both end bits, with one remove + one append per endpoint, and does the same when both ids are one endpoint.
- **The merge 0x46F8B0** (A, B), completing 8.7: unfiles A and B, sets the count of A's +0x14 endpoint J to 0 and frees
  it (0x46F96C, 0x46F97E) **without looking at its handles**, sets A.+0x14 = B.+0x14 = S (0x46F987), removes B from S by
  index (0x46F9B3) and appends A:C with B's handle's kind bits (0x46F9EA), joins B's points into A, clears B, frees
  **node B** (0x61AA00 at 0x46FAAB with B's index: the free sets bit 31 of +0x2C and links it; it does **not** clear B's
  +0x14/+0x16), welds A. It assumes J holds exactly A's +0x14 and B's +0x16 handles; it does not touch the handle of
  B's +0x16 end (J, freed).
- **The node delete 0x727420**: does nothing when the kind byte is 0; else removes the node from its +0x16 and +0x14
  endpoints by index, frees the +0x16 one when its count is 0 and then the +0x14 one when its count (read before the
  first free) is 0 (as 8.3), sets both ids to 0xFFFF, frees the node inline.
- **The cleanup 0x493CF0** (end of every field layout): over all live nodes, deletes a node whose +0x10 word is 1, or
  whose kind byte is not 0, byte +0x37 is 0 and **one of its endpoints has count 1** (a dangling line); repeats until a
  pass deletes nothing.
- **The allocator 0x599460** (as 8.3): takes the head, sets the head to the entry's count & 0x7FFFFFFF, copies the
  template (count 0); no check of bit 31. 0x5855A0 (as 8.3); since a freed entry 0 always becomes the head, a freed
  entry's next is never 0 in a sound list, so 0x601B07's "count 0" test does not free `edi` a second time after the merge.

**Answer (1), candidate 1 [C for the code, I where marked].** Every store of an endpoint id into a node's +0x14/+0x16
on the paths below the layout step is in 0x554DB0, 0x5A9C90, 0x52F0E0, 0x46F8B0 (and 0x727420, which writes 0xFFFF),
and every one is paired with an append to that endpoint in the same call. So **no path names an endpoint in a node
without appending its handle**. A full endpoint is guarded only by the creator's `count < 4` and 0x5B86D0's checks; the
split's append to P and the merge's append to S are not guarded, and are balanced only when the removal before them
finds the handle. The inputs:
- **Five lines at one point, ordinary case: refused.** A crossing at a junction that already holds four handles ends
  the call (both full: 0x5B8A60; one full: the creator's -1). The seed `edi` is fresh. No fifth handle, and no node
  naming the full junction, comes out of it.
- **Five lines at one point, through the creator's own loop: a direct overflow.** When the walk's next crossing is at
  the junction it stands on (a5): the split returns the crossed node's end junction when the point lies at that end,
  and that can be a5 itself, e.g. two crossings at one existing junction with nodes of two other tags (not filtered,
  since the tags differ). Then the creator gets **startJ = endJ = a5** and makes a closed loop: at count 2 it fills the
  junction to 4; at **count 3 it appends twice and the second append lands on the count word**: way 1 without any
  missing handle (count = the loop's C handle + 1, slot 3 = its S handle). 0x5B86D0's "count 3: stop after this piece"
  does not prevent it, because the piece is still made. [C for the code; I that the crossing finder returns a crossing
  at the walk's own start point.] **Not this dump's way:** 8.7's slot 3 is 0x00002211 and slot 2 the S handle of the
  merged node, which is the merge's append, not the creator's.
- **A closed outline whose two halves first meet one junction S: no overflow by itself.** Both calls end their first
  piece on S (call 1: B, `edi` to S; call 2: A', `edi` to S, reversed to A: S to `edi`); the merge then makes A a closed
  loop on S. Counts: with S at 2 before the step, B makes 3 (call 1 stops after it), A' makes 4, the merge's remove of B
  and append of A keep 4; with S at 3, call 2's creator refuses and no merge runs. The loop's two handles are **adjacent
  only when A:S was the last handle of S**; when call 2 went on from S (as in 8.7, the piece 0x2211), the merge appends
  A:C behind it and the loop is **non-adjacent** (model: [x, y, A:S, z, A:C]); a later removal of A by index then takes
  both handles (section 8.2's skip only spares a handle right behind a removed one).
- **Is B's handle on S undone inside the step? No path found.** In the step, the removals by index on S are of A' (the
  reversal), of call 2's later pieces (their reversal), of a stub of the last piece (its delete) and of nodes split at S;
  none has B's index, and the removal never takes another index. B is not a loop (`edi` is fresh) and is never reversed
  (the output vector is cleared before call 2) nor split by call 2 (same tag, filtered). No call in the step frees S
  while B is on it (the delete frees an endpoint only at count 0; the merge frees only `edi`).

**Answer (2), candidate 3 [C for the code, model for the orders].**
- **The reversal 0x52F0E0 swaps +0x14/+0x16 and rewrites both end bits** (one remove + one append per endpoint), also
  when both ids are one endpoint. On a **closed loop** R with handles R:S and R:C on one endpoint, the result keeps both
  only when the pair is **the last two handles** of the endpoint (or its whole list); in every other order it **loses one**
  (model: 24 of 32 orders with up to two other handles; 8.3's 3 of 6 is a subset). In the layout the reversal runs only on
  nodes that the same 0x5362F0 call has just made with flag 1 (call 2 of the step, kind-2 items with byte 8): a loop
  among them comes from the creator's own loop above, and loses a handle when the call made another piece from that
  junction after it (the pair is then no longer last). A' in 8.7's step was not a loop when it was reversed (`edi` != S).
- **The split 0x5A9C90 moves both ends correctly for an ordinary node** (N's handle leaves P by index, N:C and M:S go to
  E, M:C to P; counts balance). It **does not handle a loop**: for N with both ends on P it removes N from P (one
  handle when the two are adjacent, **both** when they are not) and appends only M. Model, 32 orders: 9 keep N:S
  (right), 9 keep N:C (count right, **wrong end bit**: the weld then moves N's other end), **14 lose N altogether**: P
  lacks a handle of N while N.+0x16 still names it, which is exactly 8.7's precondition, for N. The merge's
  non-adjacent loops (above) are this case as soon as another field crosses them.
- **Candidate 3 feeds candidate 2 [C, each step].** A loop that has kept only one of its handles shows the cleanup
  0x493CF0 an endpoint of count 1 (a dangling line), so it is deleted; the delete's two removes take that one handle,
  both counts read 0, and **the endpoint is freed twice** (model: 2 frees): 8.3's double free, reached here without the
  isolated two-handle loop that 8.3 needed. The same holds for an isolated loop (8.3). The 0x5B86D0 delete at 0x5B8EA4
  cannot do it in the layout: its node's +0x14 end is always a fresh endpoint (a6 = 0xFFFF), so the "one id for both
  ends" question of 8.3 is answered no for that call site (unless the allocator already hands out live entries).

**Answer (3): candidates 1 and 3 do not make 2 unnecessary for 8.7; the dump's slots point to 2 [I, strong].** 8.7's S
held, before the merge's append, [0x8000005D, 0x9000221D, 0x10002218, 0x00002211]. Read against the code:
- Slots 0 and 1 are **a fresh split junction's pair**: N = 0x5D (an early node) and M = 0x221D, in the split's append
  order, with the split's address-derived flag bits (0x8 on N, 0x9 on M). So S's entry was last **allocated by a split
  of node 0x5D** (count 0, then N and M).
- Slot 2 is A:S after the reversal and slot 3 is 0x2211:C after its reversal, in the order of 0x5362F0's reversal loop:
  0x2211 was the piece call 2 made from S after A'. Its creator required S's count below 4, so when it ran S held
  [0x5D, 0x221D, A'] and **not B** [C, given the creator's check].
- If that split had run in call 1, S = [0x5D, 0x221D] + B, and call 2's first crossing at S would have met count 3,
  made A' and stopped (no 0x2211), and the merge would have balanced. So the split ran in **call 2**, after call 1 had
  made B with B.+0x14 = the same index: **the endpoint allocator handed out an entry that was still live** (B's end),
  and its template wiped B's handle. With a sound free list that cannot happen, and no path in the step frees it first
  (above). In 8.3's broken free list it is what the allocator does: after the self-cycle the twice-freed entry is
  handed out twice in a row, then entries whose index is the previous hand-out's count (0-4), and an entry whose count
  equals its index is handed out again and again.
- So the chain for 8.7 reads: (3) a loop loses a handle (reversal or split) -> the cleanup's delete frees its endpoint
  twice -> (2) the allocator hands out live junctions -> B's junction is handed out again to a split in the next field
  step and wiped -> the merge finds no B on a full junction and appends over the count -> the weld walks past the slots.
  Candidate 1's creator loop is a second, independent route to way 1 with a different signature, not this crash's.
  This is [C] link by link and [I] as the sequence of this crash: no dump has shown the unsound list.

**What a full dump (8.8) would show for each [I, predictions].**
- **Candidate 2 (this file's reading of 8.7):** `layout_peek.py`'s `endpoint_free_list.sound = false` (the walk from
  the head meets a live entry, or live + freed != size); **S's index small (0-4) or equal to the twice-freed entry**;
  the freed record of node B (bit 31 of +0x2C set, +0x14 still = S, since 0x61AA00 does not clear it) naming S without a
  handle there; several recent nodes of different steps naming the few entries the allocator keeps handing out.
- **Candidate 1 (creator loop at count 3):** a node R with +0x14 = +0x16 = an endpoint whose slot 3 is R:S and whose
  count word is R's C handle + 1; the faulting stack from 0x5B86D0 (its weld at 0x5B8C8C or the next remove), not
  from the merge.
- **Candidate 3 alone (a loop lost a handle, free list sound):** a live node with +0x14 = +0x16 listed once (or not at
  all) on that endpoint; the crash then comes from a later split or merge on that loop node itself (N or A in the
  merge's frame), not from a fresh B.

**For the MC (D185).** The open link of 8.7 narrows to the endpoint free list being unsound before the step [I], and two
code defects that can make it so or that overflow on their own are now [C]: the reversal and the split mishandle closed
loops (0x52F0E0, 0x5A9C90), and the creator accepts one id for both ends (0x554DB0). Data-only levers are not in sight
(8.6); a mitigation would need either to keep closed loops out of the graph or to stop the cleanup's delete freeing an
endpoint twice, both EXE code. ServerFarmFields (0528) remains the product route's fix. A proposed lever is for the MC
and the Architect, not made here.

**Not verified (2026-11-18).** Whether 0x4072C0 returns a crossing at the walk's own start point (the creator-loop
route); what the node's +0x10 word counts; the order of hand-outs in the post-cycle regime for this step (needs the free
list at the crash); that 0x5D's kind differs from the step's (call 2 would otherwise have stopped at S's two same-kind
handles); no live run or trace.

## 9. The write fault at 0x55D91A-0x55D9E2 and the read at 0x7A3F0A: both in the farm builder's graph (ticket 0537, 2026-11-18)

Rows 1-3 of `../06-tooling-and-workflows/crash-ledger-audit.md` (T1). Read from the VP crash minidumps already
collected on c5h-i (`run/crash/*_pid28828`, `*_pid41176`, `*_pid5608`, `*_pid3312`, `*_pid63660`, `*_pid70412`; the
first folder of each pid holds the dump, the later ones are copies) with the SDK's x86 `cdb -z`, read only, and a
static read of the functions on a scratch copy of the instances' EXE (`04cfdc71...`, `cdb -z` on the image). No game
was run. The minidumps hold registers, stacks and modules only (no heap, no `.data`), so record contents below come
from the stack frame and the registers. Scripts: `tools/harness/evidence/0537/` (`dump_read.sh`, `strip_frame.sh`,
`static_read.sh`); their output stays in scratch (D83). Addresses are VAs at ImageBase 0x400000. Times EST.

### 9.1 The write: the farm-track strip on an edge with no points [C]

- **Where.** 0x55D947, 0x55D9AE, 0x55D9E2 and the product's 0x55D91A all lie in **0x55D0C0, the farm-track strip
  builder** (`../05-graphics-composition/farm-fields-layout.md` 5.2, on branch epic-0528-farm-fields). The chain is the
  queue B pump -> dispatcher 0x63ABA0 -> listener walk 0x8668E0 -> **farm builder 0x748FC0** (return 0x7490D9) -> track
  pass 0x61FD40, which calls the edge clean-up 0x5C86D0 and then *jumps* to 0x6E27D0 (so its frame is not on the stack)
  -> 0x6E27D0 (re-lays every dirty grid cell; return 0x6E282E) -> 0x55D0C0. All four stacks match it.
- **What the strip does** (behaviour). It works on one grid cell of the layout graph (object 0xBA2228, section 2), in
  two passes over the cell's edge list (next edge = node +0x28). Pass 1: for each edge whose points all lie on mask bit
  0x10, it adds 2 x n vertices, n - 1 segments and 2 x (n - 1) triangles, where **n is the edge's point count at node
  +0x10**. It then grows the static vertex vector at **0xD16AD4** (begin; size at 0xD16AD8, capacity at 0xD16ADC; the
  index vector at 0xD16AE0) to the total if needed and sets its size. Pass 2 writes the vertices: two 16-byte vertices
  (x, y, u, v) per point, the first point, then a loop **counted by n - 2** over the inner points, then the last point.
  It reads the points from the edge's point store (node +0x00 -> chunked store of 60-byte chunks: next chunk at +4,
  count at +8 with bit 31 a flag, six 8-byte points from +0xC; 0x0FFFFFFF ends the chain). Only that counter bounds the
  loop. Nothing checks the write index against the vector's size or capacity.
- **n = 0 makes the write unbounded.** n - 1 = 0xFFFFFFFF, so pass 1 adds 2 x 0xFFFFFFFF + 2 = **0** vertices (32-bit
  wrap) and reserves nothing for the edge. Pass 2's counter starts at 0xFFFFFFFE, and the loop writes 32 bytes a step
  past the vector, over whatever heap follows it, until it reaches the first unmapped page. Every fault address is a
  page start (0x92C87000, 0x860E8000, 0x98729000, 0xAB2B3000, 0xA25D7000). When the points run out, the loop goes on
  reading at chunk 0x0FFFFFFF, which wraps to mapped memory (T1600: eax 0xBFFFFFC4 = 0x0FFFFFFF x 60 mod 2^32, edi
  0x0FFFFFFF).
- **Measured in each dump** (frame words of 0x55D0C0 at the fault; n - 1 at [esp+0x5C], 2(n - 1) at [esp+0x4C], the
  loop counter at [esp+0x50], the vertex index at the edge's start at [esp+0x10], the edge index at [esp+0x34], with
  [esp+0x38] = index x 56 as a check) [C]:

  | Dump (pid) | Run | Fault VA | Edge index | n - 1 | Start vertex | Loop steps done | Bytes past the start |
  |---|---|---|---|---|---|---|---|
  | 28828 | #13397 fix run 1 (2026-11-16 21:27) | 0x55D9E2 | 0x9EEA | 0xFFFFFFFF | 72 | 14,760 | 473,504 |
  | 5608 | T1150 (2026-11-16 22:18) | 0x55D9E2 | 0x8FB1 | 0xFFFFFFFF | 0 | 2,474 | 79,200 |
  | 3312 | T1300 (2026-11-17 02:20) | 0x55D947 | 0xA9A8 | 0xFFFFFFFF | 0 | 54,069 | 1,730,240 |
  | 70412 | T1600 (2026-11-17 11:47) | 0x55D9AE | 0xB62B | 0xFFFFFFFF | 0 | 7,027 | 224,896 |

  In each row the counter, the start vertex and ecx agree exactly with n = 0 (counter = 0xFFFFFFFE - steps; ecx =
  (start + 2) x 16 + 32 x steps). **So every one of these crashes is a node of the farm-field layout graph with a point
  count of 0 that is still listed in a dirty grid cell's edge list.** Before the fault, between 79 KB and 1.7 MB of heap
  after the vertex vector were overwritten with vertex data. A run where that heap was mapped further out would corrupt
  memory silently instead of faulting here [I].
- **The product's 0x55D91A** (0491 run b1, c5h-f pid 25132, 9074's T1100 save, 2026-11-17 11:10; record
  `exe-crash.txt`: write to 0xA25D7000, EXE base 0x00180000; no dump) is the same loop's write of a vertex's y at a page
  start [C for the function and the instruction, I that n was 0 there: no dump]. The product path does not write this
  graph (section 3), so it is the same EXE fault on the product route.

### 9.2 The read at 0x7A3F0A: the face walk follows a handle to an edge with no endpoint [C]

- **Where.** 0x7A3F0A is in the **face walk 0x7A3BC0**, called from the face pass 0x4A8990 (return 0x4A8A19), which the
  farm builder calls (return 0x7490D0). The stacks of fix run 2 (pid 41176) and T1550 (pid 63660) match.
- **What it read.** The walk takes the edge named by a junction's handle and reads the count word (+0x10) of the
  junction at that edge's far end. The far end's id (node +0x14 or +0x16, 16 bits) was **0xFFFF, "none"**: eax =
  0x4FFFB = 0xFFFF x 5 in both dumps. The address is endpoint data + 0xFFFF x 20 + 0x10, past the end of the
  endpoint block (0x0FB36020 and 0xBAFA5020). A live edge never carries the id 0xFFFF there. The node delete 0x727420
  sets both ids to 0xFFFF when it frees a node. So **a junction still held a handle to a deleted edge** (a stale handle)
  [C for the read and the 0xFFFF; I that the edge was deleted rather than never finished].

### 9.3 The cause, and how it ties to the other sites [I, the leading candidate]

- **One structure, one call.** Every site of rows 1-3 sits inside **one farm-builder call** (B59,
  `ART_DEF_IMPROVEMENT_FARM`) on the layout graph at 0xBA2228: the face pass (0x7A3F0A), the edge clean-up's delete
  (0x5B1FB7, row 4, stack 0x7274A9 <- 0x5C8745 <- 0x61FD48 <- 0x7490D9) and the track re-lay (0x55D9xx). It is the
  structure of the audit's section 3 and of sections 2 and 8 here [C for the addresses and stacks].
- **The common state: an edge that is gone but still referenced.** The face walk meets a handle to a node whose
  endpoint ids were reset to 0xFFFF. The strip meets a node with no points in a cell's edge list. Both are what a node
  freed by 0x727420 looks like while something still points at it [I].
- **How a freed node can stay in a cell's list** (static read of the delete, behaviour): 0x727420 takes the node out of
  its grid cell by the **cell id stored in the node (+0x34)**. It walks that cell's list and unlinks the node through
  0x5B1F10, whose loop is 0x5B1F80-0x5B202A, **the loop of the T1100 / T1650 hangs and of the 0x5B1FB7 crashes**. If
  +0x34 is 0xFFFF, it skips the unlink altogether. If +0x34 names a cell other than the one whose list holds the node, it
  unlinks from the wrong list. Either way **the freed node stays linked in the cell that really holds it**. The next
  re-lay of that cell then strips a node with no points (9.1). A wrong list can also become a cycle, which is the hang
  [I: this needs a node whose +0x34 disagrees with the list holding it. Section 2 says the weld 0x77C1C0 refiles a node
  by its first point, so a refile that moves the node to another cell without updating one of the two would give
  exactly this; not read].
- **Ruled out** [C, L from `../08-performance/marathon-stress-t2000.md` 4.1-4.2]: the 4 MB queue (fix runs had 0
  drops), allocation failure (about 1 GB free above 2 GB at every crash) and the #13397 batching (the guard build shows
  all three sites). It is not the double free of section 8 either, as far as one dump shows: 9074's T1100 hang showed
  sound free lists (audit section 3). The two may still be separate routes into the same state [I].
- **What would confirm it** (no game run needed first): in 9074's full T1100 hang dump
  (`F:/c5-64bit-work/9074/stock/t1100-run1/hang/`) and the full 0491 hang dump of a 0x5B1FC5 loop
  (`F:/c5-64bit-work/0491/dumps/`, c5h-h, 2026-11-17 10:34; not listed in the audit), check every node in every cell's
  list: its +0x34 against the cell whose list holds it, freed nodes (bit 31 of +0x2C) still in a list, and nodes with
  +0x10 = 0. Then read statically where +0x34 is written (the weld's refile, the creator 0x554DB0, the split 0x5A9C90)
  for a path that moves a node without unlinking it. This is ticket 0538's work (audit T2).

### 9.4 For the MC (D185)

The fault is in EXE code: an unchecked count in the strip, and the cell bookkeeping of the graph. Routes, for the
Architect: (a) **0528's switch-off of the farm builder** (D186) removes every site of rows 1-4, because the strip, the
face walk and the delete only run on this graph from the builder (the bake's inserts of `farm-fields-layout.md` 10.2
aside) [I until 0528's option is measured on the 9074 saves]; (b) 0527's guard on the builder
(`farm-builder-guard.md`) could be widened to check the graph after each call, for 0-point nodes in cell lists and
handles to freed nodes, and skip the builder or repair the list, by data writes only [I, not designed]; (c) a data lever
on the vertex vector (0xD16AD4..0xD16ADC), for example pre-growing its capacity, does **not** help: the loop runs about
4 billion steps and writes past any capacity. Until (a) or (b) lands, late-game saves of this size crash, hang or
silently corrupt the heap in the farm layout on stock and product alike.

### 9.5 Not verified (2026-11-18)

- n = 0 at the product's 0x55D91A (no dump).
- That the 0-point edge was a freed node rather than a live node that lost its points (the heap is not in the dumps).
- The cell-id unlink route of 9.3 (no full dump was read for it; static read of the writers of +0x34 not done).
- Whether the double free of section 8 can lead into the same state.
