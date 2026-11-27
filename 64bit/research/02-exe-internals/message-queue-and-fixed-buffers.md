# Fixed-size EXE buffers: the 4 MB game-to-UI queue and the 1 MB UI draw list

Sources: `memory-notes/civ5-exe-message-queue-overflow.md`, `memory-notes/units-myth-plan.md`,
`memory-notes/civ5-memory-investigation-log.md` (items 27, 30, 31), `memory-notes/civ5-64bit-architecture-options.md`,
`shell-gates/stubs/{landmark,terrain,unit,coupling}.md`, `shell-gates/STUBS.md`.

The memory notes write EXE addresses as `EXE+0x...` RVAs (read from a live process with ReadProcessMemory or from
the EXE file); VA = RVA + 0x400000. Fault addresses copied from `crashes.log` "Location (in file)" are file offsets
(VA = offset + 0x400C00); where the source does not say which kind a number is, it is marked. DX11 EXE
(TimeDateStamp 0x546CD0A8) unless stated. Both buffers are written with **no bounds check**.

## 1. The 4 MB message queue

### Layout [C, confirmed live with ReadProcessMemory]

| Item | RVA (`EXE+`) | VA |
|---|---|---|
| index global | 0x160E400 | 0x1A0E400 |
| channel c | 0x160F080 + c*0x800180 | 0x1A0F080 + c*0x800180 |
| per channel | two buffers of 0x400080 (128 B header + 4 MB of records); flip counter +0x800100; high-water mark +0x800104 | |
| swap (sets the hwm) | 0x90DF0 | 0x490DF0 |
| a record allocator (~325 inlined writer sites) | 0x1CA5E0 | 0x5CA5E0 |
| `InterlockedExchangeAdd` IAT slot used by the writers | 0x5D647C | 0x9D647C |
| dispatcher (`while (p != end)`, handler table this + c*0x600 + 8) | 0x2A5970 | 0x6A5970 |
| crash return address in crashes.log | 0x2A59A5 | 0x6A59A5 |
| dispatcher spin site on a zero-length record (`add esi,[esi]`) | 0x2A59AA | 0x6A59AA |
| 4 MB queue pump | 0x59D20 | 0x459D20 (pump frame 0x459D8A in the Landmark dump) |

Writers do `InterlockedExchangeAdd(&size, recsize)` and return `buffer + 0x80 + old` with no capacity check. Records
are 128 or 256 B (header `[len][type][0][0]`), so ~32k fit per buffer. Overrunning buffer 1 writes the next record's
`[len][type]` onto the flip counter / hwm (flips -> 128, hwm -> 13).

**Frame-level pumping** [C]: the frame function (~EXE+0xB3F50, VA ~0x4B3F50) pumps four queues in order:
EXE+0x260FF80 (VA 0x2A0FF80; pump EXE+0x1AD360), EXE+0x2610200 (VA 0x2A10200, the Lua-visible "B" event manager;
pump EXE+0x385AF0; growable vector buffers, records with a 0x44 header, dispatcher EXE+0x23ABA0 = VA 0x63ABA0),
EXE+0x260F480 (VA 0x2A0F480, the "C" manager that carries the `LocalMachine*` Lua events; pump EXE+0x3052B0 = VA
0x7052B0), then the 4 MB queue (VA 0x1A0E400, the internal "A" manager, whose engine symbol per PR #13379 is
`g_EventSystemLocalMachine`; pump EXE+0x59D20). *Names reconciled 2026-11-10 (librarian, see `addresses.md`
Conventions): "C" was called the "LocalMachine" manager here, which is an analyst label, not the engine's name.*
Every type's handler slot is one trampoline
(EXE+0x23E530 -> EXE+0x4668E0, VA 0x63E530 -> 0x8668E0; the Landmark dump shows the trampoline frame 0x866919) that
walks a listener list (count +0x14, head +0x18, 20-byte nodes "at [+8]", object +0xC, function +0x10). *Completed
2026-11-18 by ticket 0527 [C] (replaced: "wording of the source, node link field not spelled out"): the node's next
index is at +0x00; the list starts with a free-list array header (free head, live, data, size, capacity), +0x1C is the
tail; each listener is called with ecx = its object and one stack argument (the record + 0x44); registration and
unregistration of the "B" manager's lists take the spin lock 0xCBFC70, the walk takes none; the type table is
`mgr + 8 + [mgr] * 0x600`, 8 bytes {handler, list} per type. Details and the farm builder's node (type 59):
[farm-builder-guard.md](farm-builder-guard.md) section 1.* 41 of the
4 MB queue's 169 registered types had no listener. The pump has a re-entry latch at object+0x1001000.

### The crash (found 2026-09-21, GameId 88, 180x113, 20 majors, observer mode)

Signature: `Access Violation`, `???+0xfffff400` / live 0x00000000 (or a garbage EIP like 0x0020802a), the only stack
frame `CivilizationV_DX11+0x2a59a5`, `esi=0228f180` with the EXE at 0x480000 (= EXE+0x1E0F180), ~1 GB free; the
DLL's Game Crash box shows it. Seen on loads of turns 260, 265, 272 (2 of 2), crashed or hung 7 of 9 late loads of
that game before the guard. Not OOM and not the DLL.

Three outcomes of one overrun: a null/garbage handler call (crash); a zero-length record that pins the dispatcher at
EXE+0x2A59AA (endless loop, 100% of one core, no dialog; the turn-240 load overflowed to 178% and hung); or survival
when the stray record lands harmlessly (the turn-250 load peaked at 99.6 / 99.9 / 99.1% and survived).

What decides it is the **longest main-thread stall between two swaps**, not the save size: a load posts 9-20 MB of
records in total (75k-160k records, several times the 4.2-4.8 MB save); arrival ~35-55 KB/s, so a stall over ~80-120 s
passes 4 MB (240: 216 s gap -> 7.13 MB, hung; 250: 121 s -> 4.00 MB, survived; 272: 168-206 s -> 5.4-7.6 MB, failed).
Normal turns never pass ~4% (about 105 swaps/s): the overflow is load-only. The main thread swaps ~9 times in the
first 2 minutes of a load and then not at all for ~5 minutes while ~10 MB arrive (turn 272). A seat switch (observer
mode left, AI hand-off, switch to another player) re-shows the view and can fill a buffer on its own (timing
correlation, not traced).

### What floods it (capture of 2026-10-01, n = 1 load; counts within 1% of four earlier loads)

Turn-280 load, 39,237 records (7,020,544 B) dropped over 196 s of a 282 s no-swap window, 9 flips:
- Every record written by the **main thread**; stack words 634,598 EXE, 87 others. Not UI Lua and not the DLL.
  (Corrects the 09-21 working guess "UI Lua".)
- Types (names from the capture): **4** farm begin (no payload, no listener), **3** farm field polygon (256 B: cube hex
  x,y,z; field id; fields on that plot; chunk i/n; corner count; up to 16 float pairs in map units from the map
  centre), **6** field withdrawn, **5** farm end; **13/15** route begin/end, **14** route curve (6 points, or 3 on a
  city plot), **32** redraw rectangle; one **111**. Packed plot = `hexX | y << 16`; grid x = hexX + (y - (y & 1)) / 2;
  hex spacing 64 units.
- The type-3 handler EXE+0x3AD6C0 (VA 0x7AD6C0) and the route builder's inner function EXE+0x183CE0 (VA 0x583CE0)
  reference `.\App\Graphics\TerrainDecalSystem.cpp`; the farm builder EXE+0x348FC0 (VA 0x748FC0) references
  `ART_DEF_IMPROVEMENT_FARM`. So the records are **terrain decals**: farm-field polygons and route curves.
- They are produced by **LandmarkSystem's GameView listeners** (`stubs/landmark.md`): the farm-field builder 0x748FC0
  (registered by 0x465600) and the route builder 0x5EC250 -> 0x583CE0 (registered by 0x661D10 -> 0x758440). The
  builders are listeners of the SECOND queue (types 59 and 45) and post into the fourth (4 MB), which is not swapped
  until the second's whole batch is done. With LandmarkSystem = 1 they are never registered: on 2026-11-10 the
  B4 + Landmark load had **no queueguard drop line** and loaded in 2:08 instead of ~2:47 (STUBS.md; corrected 2026-11-10: the same-night A/B is -30.8 s, 148.2 -> 117.4 s,
  and the 18-20 s load gap is this farm-field layout, `load-sequence.md` section 1.1).
- Geography: records arrive in column order; the buffer filled at x = 86. Farms 520 west all delivered, 796 east
  of which 793 dropped; routes 1,381 of 1,384 east plots dropped. On screen: east farms draw as barns on bare ground
  without field patchwork; railroads still draw (route curves are presumably the painted surface, not the track
  models; not confirmed). 793 of 855 farm-end plots have IMPROVEMENT_FARM (62 unexplained); all 1,381 route plots
  have a route.
- Costs: farm group = 22.3 records, 5,191 B, 181 ms mean (121 ms early, 235 late); route plot = 14.6 records,
  1,870 B, 19 ms. Model 10.9 MB vs 10.7 MB measured. About 800 farms, or 500 farms + 850 route plots, fill 4 MB.
  169 of the 196 s were farm records: the long freeze at the end of a late load is mostly farm-field layout.
  Writers filled 32.5% of the reserved bytes (67.5% padding).

### EngineQueueGuard (the DLL-side fix, 2026-09-21)

`CvGameCoreDLL_Expansion2/EngineQueueGuard.{h,cpp}`, the owner's choice [reworded for privacy] (drop the items that exceed the buffer):
- Verifies the EXE (TimeDateStamp + byte patterns of a writer, the reserve function, the swap and the dispatcher;
  relocated addresses cross-checked against the import table), then points the EXE's
  `kernel32!InterlockedExchangeAdd` **IAT slot** at a thunk. No `.text` byte is changed.
- For the four buffer size fields it reserves with CAS and diverts a record that would not fit to a 64 KB scratch
  area (returns `scratch - (size field + 0x80)`); every other caller is forwarded. Other EXE builds are left alone;
  `VP_QUEUEGUARD=0` disables it.
- Reports to `crashlogs\queueguard.log` (status at launch, one line per load that dropped) and a line in
  `crashes.log`, e.g. `UI message queue guard: active, 106323 records (17656 KB) dropped, 0 too large`.
- First test: `AutoSave_Post_0280` loaded, buffers capped at exactly 4,194,304, 39,128 records (6.8 MB) dropped,
  screen normal, AI turn running. Reload in the same process: 39,052 dropped, back in game. That process later died
  of address-space starvation, not the queue.
- All three EXEs (2026-09-22, branch `engine-queue-guard`, gated on BIN_HOOKS): DX9 (0x546CCB59) and Tablet
  (0x546CD5F8) have byte-identical queue code (110 writer / 97 reserve hits per EXE, one swap, one dispatcher).
  DX9: index 0x15FE200, channels 0x15FEE80, IAT 0x5C9488, swap 0x385420, dispatch 0x20F40. Tablet: index 0x160FE80,
  channels 0x1610B00, IAT 0x5D847C, swap 0xACE00, dispatch 0x3980E0 (RVAs). On that branch `Install(MOD_BIN_HOOKS)`
  runs from `CvDllDatabaseUtility::CacheGameDatabaseData` right after `gCustomMods.preloadCache()`; the
  memory-diagnostics tree installs from `DllGetGameContext`. Verified on `AutoSave_Post_0280` in each (DX9 38,928
  dropped, Tablet 39,290). Branch commit 2f954ff99 off LoneGazebo master dcb33a654; PR not opened at the time.
- Capture facility (memory-diagnostics tree only): `crashlogs\queueguard.capture` marker; each diverted record in a
  32 MB never-reused area with thread, buffer, fill, flip counter and up to 24 EXE/DLL/lua51/database stack words;
  `ReportDrops` writes `crashlogs\queueguard-dropped-<date>-<time>.txt`.
- Not done: replay the dropped records after the swap (they hold only ints and floats), or swap+dispatch the 4 MB
  queue from inside the thunk when full (lossless, but runs consumers in the middle of a producer).
- The guard thunk sees reservations, not dispatch, so it cannot filter by listener (terrain.md).

## 2. The 1 MB UI draw-list buffer (units-on-screen crash)

Found 2026-09-29/30 ("Myth 10"): every load crashed at **400-450 units on screen** (placing 391-400, 421-430,
441-450 x3) on the huge save with EUI. Not memory: committed cost ~84-105 KB per land unit in view, ~1 KB below 2 GB,
VRAM 0, and 818-936 MB was free above 2 GB at every crash.

Mechanism [C from dumps, L from live reads]:
- The per-frame render job on a worker thread (RVA 0x209192, `fstp [esi+60h]`) takes records from a **bump
  allocator** (base + offset, `offset += size`, no bounds check). The note gives the allocator as "EXE 0xc7fdc0", an
  absolute address in that run (EXE base not stated) [kind unclear].
- Buffer object = `[[EXE+0x29D1D1C]+0x6F0]` (VA 0x2DD1D1C, the same global whose +0x644/+0x648 hold the backbuffer size
  in `CAMERA.md`): capacity +0x34 = 0x100000 (**1 MB**), base +0x24, offset +0x40 (0x101000 at the crash), last record
  +0x30, last size +0x4C. Double-buffered: two 1 MB halves of a 2 MB engine-pool block (0x201000 committed /
  0x208000 reserved), alternating per frame. Reset between frames.
- Contents (decoded 2026-10-06 from full stall dumps): the **2D UI's per-frame draw list**. Index at obj+0x14 (32-byte
  entries: +0 sort key, +0x10 kind, +0x14 record pointer, +0x18 size u16), count at obj+0x0C. Kinds: 1 = sprite
  240 B; 2 = text 144 B (one per label); 0x882 = nine-patch panel 352 B; 3 = line 48 B (seen in InfoAddict). Records
  hold only floats.
- Per unit in view: exactly 8 sprites = 4 x 64x64 flag layers, 2 x 32x32 unit icon (+ shadow), 2 x 16x16 promotion
  icons (1,920 B); measured 1,902 B = 13.2 records per unit. **The records are the EUI unit flags**: with the flag
  containers hidden the slope is 11 B/unit and 529 units fill 20% with no crash. Full at ~443-449 units in view; the
  crash at 440 had 4,556 records (4,067 sprites + 460 texts + 29 panels), flags 844,800 B = 80%.
- EUI promotion flags: 240 B per promotion icon per unit in view; 13 icons (EUI's cap) make a flag ~4,650 B, full at
  ~185 units in view (crashed at 200 x 13 icons, fill frozen at 1,052,864 B).
- Other fill: empty screen ~176-195 KB/frame; engine-drawn yield icons ~250 KB zoomed out (the 10-06 line; the 10-07
  YieldIconManager A/B later showed VP's Lua manager draws them, see `allocations-and-memory-map.md`); tech tree
  +280,800 B on top of the world UI (77% total); InfoAddict Historical Data graph +498,880 B (by arithmetic a 20-civ
  game would pass 1 MB on its own, untested); Single Unit Graphics does not change the fill (3D models never use this
  buffer).
- Unlike the 4 MB queue this is a heap object refilled every frame; the overflow is deterministic in play.
- Fault addresses as logged (kind noted where derivable): `EXE+0x208592` for the overrun (units loads 3 and 5, the
  arena run) - this is 0xC00 below the job's RVA 0x209192, so it is the crashes.log file offset of the same
  instruction, VA 0x609192 [I from the 0xC00 difference]. A second, rarer crash at ~400 units / 90% fill: the
  units-myth note gives `EXE+0x160cd9`, the investigation log `EXE+0x1618d9`, reading `[[EXE global]+0x9C]` =
  0xFFFFFFFF, then a sound-thread fault at `+0x51f5bc` on the same field; the two values differ by exactly 0xC00, so
  they are the file offset and the RVA of one site, VA 0x5618D9 [I]. Promotion-flag crash: `EXE+0x1e0129` [kind not
  stated]. Exit code of the first crash: 0xC000041D (STATUS_FATAL_USER_CALLBACK_EXCEPTION).
- No DLL-side guard is possible (the buffer is the EXE's); an import-slot or heap-level bound like EngineQueueGuard
  would be needed (suggested, not built).

## 3. Upstream PRs that touch fixed EXE buffers or EXE interop

The sources name these and nothing more than the one-line attributions below
(`civ5-64bit-architecture-options.md`: "Buffer-overflow crashes are a separate, handled class: upstream PRs
13397/13406/13379 (vaderkos), 13539 (Alpakinator), the owner's closed 13372").

| PR | Author | What the sources say |
|---|---|---|
| #13379 | vaderkos | "Make EXE interop easier to add and change" (open at the time; head `vaderkos/Community-Patch-DLL@ae17ecad6`); file `CvGameCoreDLL_Expansion2/Exe/CvExeAddresses.h`, 12 entries (10 addresses: 3 functions, 3 code locations, 2 vtables, 2 data; 2 struct offsets) with DX11/DX9/Tablet VAs and byte signatures, names from the Linux build's symbols. All 6 signatures match the DX11 EXE copy. Its EXE-interop notes are the source of the CEG rule (~80% of `.text`, two thirds of `.rdata` hashed). Removed an old commented-out NOP patch at 0x51E031 from `CvDllGame.cpp`. Its `LoadCvGameCoreDLL_SetEngineUserInterface` signature (0x47BA70 / 0x72B7F0 / 0x4CB690) is the anchor for `g_Civ5App` |
| #13397 | vaderkos | **The 4 MB queue** (checked on GitHub 2026-11-16, one-off 9074; open, not merged): batches the whole-map plot layout (on load and when the active player changes, hotseat or autoplay) into 256 KB of estimated stream bytes per buffer swap, holds the load screen until done, warns in chat at 75 % and on overflow. Names the queue `g_EventSystemLocalMachine` containers (DX11 0x1A0F080, = channel 0 here) and the max published size (DX11 0x220F184, = channel 0's hwm at +0x800104); its DX11 code signatures match the EXE. Its reproduction: a dense Tectonic save reached 4,194,432 B. See `08-performance/marathon-stress-t2000.md` 4.1 for a stress game where the guard diverted 3.8 MB |
| #13406 | vaderkos | buffer-overflow fix (no further detail in the sources) |
| #13539 | Alpakinator | buffer-overflow fix (no further detail in the sources) |
| #13372 | The owner (closed) | buffer-overflow fix, closed (no further detail in the sources) |
| #13387 | - | **not mentioned in any source file** of this knowledge base |

Related, not a PR number: vaderkos' `exe-api-normalization` branch provides `Exe.TryDisableEngineYieldIconManager()`
(used in the 2026-10-08 A/B). EngineQueueGuard itself lived on the owner's branch `engine-queue-guard` (the
project's DLL fork), not opened as a PR at the time of the note.

Which of #13397 / #13406 / #13539 / #13372 addresses the 4 MB queue or the 1 MB draw list is not stated in the
sources; check the PRs themselves before relying on this table.
