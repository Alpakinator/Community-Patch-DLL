# Upstream #13406 (the 1 MB UI draw list): where it must run on each route, and the proxy-side design

Queue ticket 0460, account B, 2026-11-17 05:10-05:35 EST. Reading and design only: no game run, no build, no product code.
Follows `upstream-fix-batch-2.md` section 2.7 (one-off 9075) and `../02-exe-internals/message-queue-and-fixed-buffers.md`
section 2 (the crash itself, 2026-09/10). Addresses are DX11 VAs at ImageBase 0x400000 unless marked. Marks: `[C]` read in
the pull request, in the DLL source at the pin or in this knowledge base's dumps; `[L]` seen live; `[I]` inferred.
This file describes behaviour; it quotes no EXE bytes and no decompiler output (D83).

## 0. In short

1. **What #13406 does** `[C]`: once, on the UI thread, before the first in-game frame, it replaces the EXE's per-frame UI
   draw-list buffers (1 MB of data and 4,096 32-byte commands per half) with blocks four times larger (4 MB, 16,384),
   allocated and later freed with the EXE's own allocators, by writing the stream objects' pointer and size fields. Its
   trigger is a line in VP's `InGame.lua` (`OnUpdate`) that calls a new Lua function `Exe.TryGrowUICommandStream()`.
   It acts in the process that runs the EXE's UI, and nowhere else.
2. **Stock route (the 32-bit pin DLL inside the EXE)** `[C]`: the DLL is in the right process, so the C++ part would work
   there, but it cannot be cherry-picked onto the i0 pin: it is built on upstream #13379 (the `Exe/` interop framework),
   which the pin does not have and which removes two `Game` Lua functions that VP 5.4.8's `DiploCorner.lua` calls every
   turn; and its trigger is a mod Lua line that VP 5.4.8 does not have and the patch does not ship. Section 3.
3. **Product route** `[C]`: the x64 DLL runs in `server.exe`, where there is no EXE image and no UI; upstream's code reads
   the running module (`GetModuleHandleA(NULL)`), finds `server.exe`, and refuses. Only the proxy is inside the EXE
   process on the product route, so the fix belongs in the proxy package. Section 4.
4. **The proxy-side change** (section 5): a per-frame main-thread callback from a C listener of the `InGame` context's
   `Events.LocalMachineAppUpdate` (the install machinery of the proxy's UI bridge, issue 0178), which grows the
   streams by the same two-step swap, with the EXE's allocators called by address and heap fields written. No EXE code
   byte, no import slot. The EXE calls need the MC's word first (D136 / D144 / D170 class).
5. **9074's failure sites are not the draw list** `[C, addresses; I, cause]`: none of 0x5B1F80-0x5B202A, 0x5B1FB7,
   0x77C235, 0x77C6DA, 0x55D947, 0x55D9E2, 0x7A3F0A is a draw-list site (0x609192, 0x5618D9), four of the eight failures
   happened during the load, before any in-game flag is drawn, and the stacks run through the event managers'
   dispatchers on the main thread. Section 6.
6. **Recommendation** (section 7): pin batch 4 takes no row for #13406. Product: a proxy ticket after 2600's merge,
   with a live reproduction first. The new finding that changes the design: **the command array overflows before the
   data buffer** (4,556 commands against a 4,096 cap at the 440-unit crash, section 2.2), and the grown buffers move
   the cliff (to about 780-1,550 units in view), they do not remove it.

## 1. Inputs read (2026-11-17)

- Pull request LoneGazebo/Community-Patch-DLL#13406 "Grow the engine's UI draw buffers (Prevent crashes)", vaderkos,
  state open, not draft, `mergeable_state` dirty (conflicts with master), head 6b3d305cf3ce, last updated
  2026-11-13 04:24 UTC; read with `gh pr view` / `gh pr diff` (public data, nothing sent). One commit, 9 files:
  `Exe/CvExe.cpp` +518, `Exe/CvExeAddresses.h` +407, `Exe/CvExeApi.cpp` +150, `Exe/CvExeApi.h` +27, `Exe/CvExe.h` +12,
  `Lua/CvLuaExe.cpp` +55 -1, `Lua/CvLuaExe.h` +4, `LuaCATS/Exe.d.lua` +26 -1, `(1) Community Patch/Core Files/
  Overrides/InGame.lua` +10. Its body: "Based on #13379"; it cites a public toolkit report, "InterfaceDrawListBuffer".
  **Update 2026-11-17 (ticket 0487):** the head moved to **8cfa46a83** (updated 2026-11-17 15:34 UTC, still open); read
  with `gh pr diff` at 15:0x EST. The DX11 symbols, patterns, cross-checks, allocator arguments (heap 9, tags 0xC / 0xE)
  and the two-step swap described below are unchanged at that head [C]; 0487 ports from it.
- Upstream #13379 "Make EXE interop easier to add and change", **merged 2026-11-11 02:00 UTC as 6c80fa023** (after
  Release-5.4.8, 2026-10-16; no VP release carries it yet: the latest release is 5.4.8 [C, `gh release list`]).
  Its files at 6c80fa023 (`Exe/CvExeSymbols.cpp`, `Exe/CvExeBuild.cpp`, `Lua/CvLuaExe.cpp`, `Exe/CvExe.cpp`).
- The i0 pin 26234371e (`deps/i0.lock`) and VP 5.4.8's `DiploCorner.lua` and `InGame.lua` at `Release-5.4.8`.
- 9074's stock probes (`research/08-performance/marathon-stress-t2000.md` 4.1 on branch
  `claude/oneoff-9074-marathon-t2000-041fc3`, read at 1a8961bec; not yet on `main` when this was written).
- The proxy: `proxy/src/proxy_ui_bridge.h`, `proxy/src/proxy_shell.h/.cpp`, `proxy/src/proxy_report.h`,
  `proxy/src/proxy_session.h`, `proxy-design.md` sections 11.2, 11.5, 24.

## 2. What #13406 changes, and in which process

### 2.1 The mechanism [C, pull request; agrees with our 2026-09/10 dumps]

The EXE's UI manager (`g_pUIManager`, DX11 VA **0x2DD1D1C**; our `addresses.md` row "renderer/UI object pointer") holds
up to three command-stream pointers: **+0x6F0** (the stream in use), **+0x6F4 / +0x6F8** (left and right, distinct only
with stereo 3D). The stream object's fields, as the pull request names them (DX11):

| Offset | Pull request name | Our earlier reading (`message-queue-and-fixed-buffers.md` 2) |
|---|---|---|
| +0x04 | SetCount | - |
| +0x08 | CommandCap (4,096) | - |
| +0x0C | CommandCount | "count at obj+0x0C" |
| +0x14 | Commands (array of 32-byte entries) | "index at obj+0x14 (32-byte entries)" |
| +0x24 | Data | "base +0x24" |
| +0x34 | DataSize (0x100000) | "capacity +0x34 = 0x100000" |
| +0x40 | DataUsed | "offset +0x40" |
| +0x2068 / +0x2070 | DataOther / CommandsOther (DX11's second half) | "double-buffered: two 1 MB halves of a 2 MB engine-pool block" |
| +0x2074 | DataUsedOther (the last frame's DataUsed, saved at the swap) | - |

Every offset that both sources name agrees `[C]`. The pull request names the helper at VA 0x8CD490
`CCommandStream::ClearStream` (it zeroes +0x0C, +0x04, +0x40, ...); `addresses.md` row 0x8CD490 calls it
"RenderBatch::Reset" `[I]`: same function, the pull request's name is the engine's symbol (from the Linux build, per
#13379's text). Proposed correction for `addresses.md` (not made here: outside this ticket's touches).

What it does `[C]`:

1. `CanGrowUICommandStream`: refuses unless the mod option `BIN_HOOKS` is on, the EXE is not the Tablet build (its frame
   end copies the data into a 1 MB GPU buffer), every address it uses resolved and passed its checks, and
   `[g_pUIManager]` is not NULL.
2. `TryGrowUICommandStream` (UI thread only: the Lua binding refuses on the game-core thread): for each distinct
   non-NULL stream (+0x6F4, +0x6F8, +0x6F0): done if DataSize >= 4 MB and CommandCap >= 16,384.
   - **DX11 (two halves)**: the first call checks that each pair (Data/DataOther, Commands/CommandsOther) is one block
     of two adjacent halves, allocates one 8 MB data block (`FireMallocAlignedNoTracking`, 16-aligned, the stream's heap
     and tag) and one 2 x 16,384 x 32 B = 1 MB command block (the engine `operator new[]`). On each call, only when the
     current half is empty (DataUsed, CommandCount and SetCount all 0, "between frames ... nothing reads it"), it
     points the current half's Data and Commands at the matching new half; it returns "pending" until the other half
     has also been replaced (the EXE swaps halves each frame, so this takes two calls a frame apart). Then it writes
     DataSize = 4 MB and CommandCap = 16,384 and frees the old blocks (the lower pointer of each old pair) with the
     matching EXE free.
   - DX9 (one buffer): one call, when the buffer is empty.
   - Between the two DX11 steps the EXE's destructor would free a wrong pointer; the pull request accepts that because
     the destructor runs only at exit.
3. `TryGetUICommandStreamUsage`: `{used = DataUsedOther, capacity = DataSize}` of +0x6F0 (DX11 and Tablet).
4. `InGame.lua`: `OnUpdate` (the `ContextPtr:SetUpdate` callback, run on the UI thread every frame) calls
   `Exe.TryGrowUICommandStream()` until it returns ok or a reason other than "pending".

EXE functions it calls by address (DX11, `CvExeAddresses.h` of the pull request; each verified at run time by a byte
signature or by cross-checks from the constructor/destructor) `[C]`: engine `operator new` 0x82C090 and
`FireMallocAlignedNoTracking` 0x813A80 (both in `addresses.md` / `allocations-and-memory-map.md` already, same
meaning), `operator delete[]` 0x813D10, `FireFreeAlignedNoTracking` 0x813CF0 (our "aligned free"). It writes only
heap fields of the stream objects; no EXE code byte (D4 holds). The process it acts in is whichever process the DLL
is loaded into: `ExeBuild` reads the PE timestamp of `GetModuleHandleA(NULL)` (`CvExeBuild.cpp` line 28 at 6c80fa023).

### 2.2 The command array overflows first [C, our dump numbers; I, the conclusion]

`message-queue-and-fixed-buffers.md` section 2 recorded the 440-unit crash with **4,556 records** at obj+0x0C (4,067
sprites, 460 texts, 29 panels) and data at 80 % (844,800 B of flags). Obj+0x0C is the pull request's CommandCount and its
cap is 4,096 (+0x08). So at that crash the 128 KB command array (4,096 x 32 B) had already been written 460 entries
(14,720 B) past its end, into whatever the engine heap had placed after it, while the data buffer still had about 20 %
left. The final fault (VA 0x609192, a data write at offset 0x101000) came later in the same frame or the next.
[I] This makes heap damage next to the command array a likely cause of the "second, rarer crash" at about 400 units /
90 % fill (VA 0x5618D9 reading 0xFFFFFFFF from `[[global]+0x9C]`), which the 2026-09 notes did not explain.

Consequences for any re-implementation:

- Growing only the data buffer would not help; the command cap matters at least as much (#13406 grows both, x4).
- **The grown buffers move the cliff; they do not remove it** (neither limit is ever checked by the writer, and no
  import-slot or heap-level bound exists for a bump allocator, section 2 of the queue file). By the 2026-09/10 per-unit
  figures (1,902 B and about 10.4 commands per plain EUI flag including the screen's base fill; 4,650 B and more
  commands per flag with 13 promotion icons), 16,384 commands hold roughly **1,500 plain flags** and 4 MB of data
  roughly **2,100**; with full promotion rows the data holds roughly **860** and the commands roughly **780** [I,
  arithmetic on n = 1 measurements per row]. Today's limits: 440-450 plain, about 185 with full promotion rows [L,
  2026-09/10].
- Cost: per stream +8 MB data and +1 MB commands allocated, 2 MB + 256 KB freed: **+6.75 MB of the EXE's address
  space** (x3 only with stereo 3D), one contiguous 8 MB block at the first in-game frame. On the stock route this comes
  out of the 32-bit address space whose exhaustion is the stock game's main crash (9074 section 4: the largest free block
  falls to 32-45 MB late in that game). The growth runs at the first in-game frame of a session, when the largest
  block is usually hundreds of MB, and stays for the process [I].

## 3. Stock route: the i0 pin and pin batch 4

- **The process is right** `[C]`: on the stock route the 32-bit pin DLL is the DLL the EXE loads, so code in it runs in
  the EXE's process and can find `g_pUIManager` and call the EXE's allocators, as upstream intends.
- **The framework is missing** `[C, git ls-tree 26234371e]`: the pin has no `CvGameCoreDLL_Expansion2/Exe/` and no
  `Lua/CvLuaExe.cpp`. #13406 cannot be cherry-picked without #13379 (6c80fa023).
- **#13379 does not fit a 5.4.8 pin used with 5.4.8 mod files** `[C]`: it removes `Game.SetExeWantForceResyncValue` and
  `Game.IsExeWantForceResyncAvailable` (and `CvDllGame::InitExeStuff`, `CvBinType`) and moves the resync onto
  `Exe.TryScheduleResync`. VP 5.4.8's `(1) Community Patch/LUA/DiploCorner.lua` calls `Game.SetExeWantForceResyncValue(1)`
  (line 515) and `Game.IsExeWantForceResyncAvailable()` (line 521, from a `GameEvents.PlayerDoTurn` listener, so every
  turn). A pin with #13379 as merged would raise a Lua error in DiploCorner every turn with VP 5.4.8's files [I, not
  run]. A port would have to keep the old `Game` functions.
- **The trigger is not there** `[C]`: VP 5.4.8's `InGame.lua` has `OnUpdate` (line 1073, `ContextPtr:SetUpdate` line
  1108) but no `Exe` call; the patch ships no mod Lua (the installer places binaries; mod-file changes are the D26/D27
  line of `upstream-fix-batch-2.md` 2.8 item 4). So the C++ part alone, even if built, would never be called.
- **A DLL-side trigger** would be needed for a stock-route fix before VP ships the Lua line. Candidates, none verified:
  (a) the DLL installs its own C listener of the `InGame` context's `Events.LocalMachineAppUpdate`, the way the proxy's
  UI bridge does (`proxy-design.md` 24.3; that is Lua C-API work inside the upstream-clean DLL); (b) a per-frame UI-thread
  DLL entry the EXE already makes: the 63-slot poll (`GetGame`, `GetPlayer`, about 830 calls per idle frame) or
  `ICvGame1::UpdateTestEndTurn` (once per frame, `proxy-design.md` 16). For (b) the order against the draw-list fill is
  unknown; #13406's "only when the current half is empty" test keeps it safe but may leave it pending for ever [I].
  Upstream's own trigger (a `SetUpdate` callback) runs in the UI update pass before the UI draws [I, from where it is].
- **Upstream state**: #13406 open, conflicting with master, no review yet; if it is merged and released, the stock
  route gets it with that VP release (the release checklist rebases the pin; `new-vp-release-checklist.md` section 3
  already lists #13379's x64 problems: four pointer-to-`DWORD` casts that clang-cl `-m64` rejects; #13406 adds more,
  e.g. `reinterpret_cast<DWORD>(pData)` in `StartGrowth`).
- **A hazard for the next VP release, both routes** `[C, source; I, effect]`: VP files that carry #13379 call `Exe.*`
  unguarded: `YieldIconManager.lua` at load (`Exe.TryDisableEngineYieldIconManager()`), both `DiploCorner.lua` copies
  (`Exe.CanScheduleResync()` / `Exe.TryScheduleResync()`), and with #13406 `InGame.lua` every frame. Under a DLL without
  the `Exe` table (both of today's pins) each is "attempt to index global 'Exe'": YieldIconManager's file stops at load,
  and InGame's `OnUpdate` stops at its first line every frame (its alert handling after that line would never run).
  So a pin rebased onto such a release must carry the `Exe` table on both builds, and on the product route the table's
  answers must come from the proxy (section 5.6). For the release checklist (`new-vp-release-checklist.md` 3, row
  "EXE interop"), noted here, not edited there.

## 4. Product route: why it must live in the proxy package

- The x64 DLL runs in `server.exe` (`proxy-design.md` 11.1). `GetModuleHandleA(NULL)` there is `server.exe`; its PE
  timestamp matches no column, the build is "Unknown", every `Exe::Try*` refuses `[I, from the source; the checklist
  says the same of #13379]`. Even with the right addresses the server could not reach the EXE's heap: it is another
  process (and 64-bit).
- The EXE's UI, its UI manager, the draw list and the unit flags (EUI's `UnitFlagManager` Lua, drawn by the EXE's 2D UI)
  stay in the EXE on the product route (D80: only the DLL and the world renderer move). So the product has the same
  overflow at the same unit counts [I: same EXE UI code; not reproduced on the product route].
- The proxy is the only module of the patch inside the EXE process (`proxy_shell.h` header), and it already does the
  same class of work there: EXE checks by PE timestamp and code patterns, heap/data writes (the view mask), and an EXE
  function called by its address (the white fog mask's texture loader 0x8C6540, D136).
- Lua calls of `Exe.*` from VP files reach the server through the proxy's thunks (`proxy-design.md` 11.5): the x64 DLL
  would answer them, wrongly (refusal), unless the proxy answers them itself.

## 5. The proxy-side change (design for the proxy ticket)

### 5.1 Where it runs: a per-frame callback on the main thread

- **Entry**: the proxy's Lua thunk (`FuncThunk`, every DLL Lua function the UI calls, main thread) already runs the UI
  bridge's install check (`UiBridge::OnLuaEntry`, at most every 250 ms: find the `InGame` environment through
  `_LOADED._G.Threads`, add C listeners to its own `Events` table, a generation marker so an environment gets one
  install; `proxy-design.md` 24.3). The bridge's drain is a C listener of `Events.LocalMachineAppUpdate`, which the EXE
  raises once per frame on the main thread for in-game contexts, from the first shown in-game frame on (24.3: none
  during the load screen) [L, 0178].
- **Change**: give the bridge one more listener kind, a frame hook `void (*onFrame)()` called from the drain, and install
  the drain when any consumer needs it (today only `ui_bridge=1` installs anything; the product runs `ui_bridge=0`,
  `server/evidence/0422/live/report/vpproxy.log.tail.txt` line 67). The bridge stays EXE-free (its rule R2); the EXE
  work is in a new file of the proxy (proposed `proxy/src/proxy_uidraw.cpp`, beside `proxy_shell.cpp`), which the
  frame hook calls.
- Why this point and not the alternatives: it is the same phase as upstream's `SetUpdate` callback (Lua update on the
  main thread, before that frame's UI draw [I]); `ICvGame1::Update` is on the game-core thread (wrong thread); the
  proxy_shell watcher thread is not the UI thread; a hook on a D3D or window import slot would run on the render path
  where the halves are in use. No import slot is needed.

### 5.2 What it does each frame

1. First frame of the process: the EXE check. `ProxySession::IsKnownExe` (PE timestamp 0x546CD0A8, the DX11 Steam build
   the proxy already requires); then read-only checks, with `VirtualQuery` before every read as `proxy_shell.cpp` does:
   the code patterns #13406 uses for the stream constructor (VA 0x8CE150), destructor (0x8CCD20), `ClearStream`
   (0x8CD490), the renderer start-up site that creates the streams (0x5C991B) and reads `g_pUIManager` (0x5C9A1F), and
   the allocators (0x82C090, 0x813A80), plus the cross-checks the pull request makes (the offsets +0x6F0/+0x6F4/+0x6F8,
   +0x08, +0x14, +0x24, +0x34, +0x2068, +0x2070, +0x2074 read out of those functions; the allocator call targets).
   Patterns are kept as in `proxy_shell.cpp` (wildcarded, the shell's precedent; the export scrub of D174 decides how
   they are carried). Any mismatch: off for the process, loud (5.4).
2. Every frame (cheap: three pointer reads and two compares per stream): for each distinct stream, `IsGrown`. A stream
   back at 1 MB / 4,096 (a renderer restart re-runs the start-up that creates the streams [I: graphics settings change,
   device loss; not verified]) is grown again. This differs on purpose from upstream's Lua, which stops after the first
   success.
3. Growth: #13406's DX11 two-step swap unchanged in behaviour: the layout check (each pair is one block of two adjacent
   halves), the two allocations with the EXE's allocators (`FireMallocAlignedNoTracking`(8 MB, 16, heap 9, tag 0xE) and
   `operator new`(1 MB, 1, heap 9, tag 0xC), the arguments the pull request passes "as CCommandStream calls them"),
   one half per frame when the current half is empty, then the sizes and the frees (0x813CF0 for data, 0x813D10 for
   commands). Re-implemented from this description (D83; the pull request is GPL-3.0 DLL code, so a port with an origin
   comment is also allowed, D22). DX9 and Tablet are out of scope: the product requires the DX11 build.
4. Usage: once a second, the high-water of `DataUsedOther` (+0x2074) and of the command count, logged when it crosses
   50 %, 75 % and 90 % of the (grown) capacity, and the high-water in the report's session lines.

### 5.3 Same game state as stock (D164)

- The draw list holds only per-frame 2D UI records (floats: sprites, text, panels, lines) built by the EXE and read by its
  renderer; nothing in it reaches the DLL, the server or a save `[C, 2026-10-06 decode]`. The growth changes no game
  input, no Lua value and no call across the boundary; it adds no call to the server.
- What it does change: the EXE heap's layout (+6.75 MB, one 8 MB block) and, past about 450 flags, a crash into no crash.
  Neither is game state; a stock game that would have crashed has no result to compare.
- Check for the ticket: the 0178 parity method (`proxy-design.md` 24.5: `proxy/tools/live/arm.py`, `eq_stats.py`, the
  same build with the growth on and off, 30 AI turns on a Tiny copy): identical keys expected; plus the windowed
  stock-versus-product identity runs of D164 with the growth on.

### 5.4 Loud failure (D118, D119)

- What can fail: the EXE check (5.2 step 1), the layout check, an allocation (NULL), the growth staying "pending" for
  more than, say, 600 frames (the current half never seen empty), or a stream going back to 1 MB more than a few times.
- None of these degrades the world (D118's concern): the game keeps the stock 1 MB buffers and the stock crash risk.
  But a silent skip is not allowed (MC #2058, `proxy_shell.cpp` ticket 0339). Proposal: one `uidraw: FAILED: <why>` line
  in `vpproxy.log`, and the D119 report with a new non-fatal stage `uidraw` and the helper's popup once per session
  ("the patch could not enlarge the UI's draw list; the game may crash with many unit flags on screen"), then the game
  goes on; the report's session lines carry the usage high-water. **Decided (MC, 2026-11-17 14:45):** non-fatal and
  loud (D118, D125): the line, the report with the non-fatal stage `uidraw` and the helper's popup once per session;
  "pending" is a failure after 600 frames. As built, section 9.
- Success: `uidraw: grown: stream 0x..., 4194304 B, 16384 commands, after N frames` in `vpproxy.log`.

### 5.5 Switch

**Superseded 2026-11-22 (the owner, in person, one-off 9081): no longer an option; the proxy always installs the growth, id 3 is a reserved row (installer-notes 16.9).** **Decided (MC, 2026-11-17 14:45, tracker 0487; replaces this section's proposal of `[Extras] UiDrawListGrowth=1`):**
`[Renderer] UiDrawListGrowth`, default 1, not under `[Extras]`: a crash fix of what the stock EXE does wrong, classed like
the bomber fix (0456) and live terrain (D173); 0 = the stock buffers, kept for comparisons as `WhiteFogMask` is
(`docs/OPTIONS.md`).

### 5.6 When VP ships #13406's Lua (later)

If a VP release carries #13379/#13406, its Lua calls `Exe.*`. On the product route the proxy should answer the `Exe`
table itself in every engine state (replace the x64 DLL's registrations in the copy the EXE makes into each context,
`proxy-design.md` 24.2 B1): `TryGrowUICommandStream` / `CanGrowUICommandStream` / `TryGetUICommandStreamUsage` from
5.2's code (the same state, so the Lua call and the frame hook agree), the others (`TryScheduleResync`,
`TryDisableEngineYieldIconManager`) as their own small ports or as refusals. Not needed for a 5.4.8-based product.

**2026-11-25 (lead-0763, epic 0763, D259): VP 5.5 ships it, so this section is now needed** `[C, git grep of tag
Release-5.5 = b0bce8c2f in the fork clone]`. The Lua callers of `Exe.*` in 5.5's mod files (LuaCATS excluded), 8 lines:
`(1) Community Patch/Core Files/Overrides/InGame.lua` 1078 `Exe.TryGrowUICommandStream()` (every frame until done);
`Core Files/Overrides/YieldIconManager.lua` 601 `Exe.TryDisableEngineYieldIconManager()` (at load); `(1) Community
Patch/LUA/DiploCorner.lua` 549, 558, 572 and `(3a) VP - EUI Compatibility Files/LUA/DiploCorner.lua` 547, 556, 570:
`Exe.TryScheduleResync()`, `Exe.CanScheduleResync()`, `Exe.TryEnableTunerInMultiplayer()` (5.5 adds the tuner one,
upstream 4d4373e8f). In the out-of-process DLL every `Exe::` feature answers as for an unknown EXE (fork branch
export-e5-dll-wip, ticket 0762: 9 functions); what each Lua line gets on the product route is ticket 0765's to settle. The
DLL-internal users (d1022016b budgeted map-graphics layout, 1fb49429f city-art repair) take their unknown-EXE fallback in
the server, which is 5.4.8's behaviour `[I]`.

### 5.7 The live reproduction the ticket needs first

The 2026-09/10 method (`message-queue-and-fixed-buffers.md` 2): the huge save with EUI, units placed in view in steps
(Lua spawn through the harness), the camera over them; the crash at 440-450 plain flags (exit 0xC000041D, fault VA
0x609192 or 0x5618D9). On the product route with the growth off it should crash at the same count; with it on, survive
1,000 plain flags and log the usage; the counts and the fault are the evidence. Pictures go into the Visual Checks feed
as diagnostics (D112); EXE popups closed first (D114). A test client must not sit on the crash box (D155).

## 6. 9074's failure sites against the draw list

| 9074 failure (stock, 43-civ `-ia` pin DLL, `marathon-stress-t2000.md` 4.1) | Site (VA) | When | Path |
|---|---|---|---|
| T1100 run 1, hang | loop 0x5B1F80-0x5B202A | first turn | main thread, index-linked record list (cycle) |
| T1100 run 3, crash | read at 0x77C6DA | **in the load** | same EXE path as run 1 (return RVAs 0x1AA0D8, 0x1B8A27), Lua frames |
| T1150, crash | write at 0x55D9E2 | **in the load** | B event manager's dispatcher (RVA 0x23ABD4), handler RVA 0x3490D9 |
| T1200, hang | 0x77C235 | **in the load** | same function as 0x77C6DA |
| T1300, crash | write at 0x55D947 | **in the load** | as T1150 |
| #13397 build runs 1-3 | 0x55D9E2, 0x7A3F0A, 0x5B1FB7 | first autoplay turn | the 4 MB queue's dispatcher / the B manager's dispatcher |

The draw-list overflow's sites are VA **0x609192** (the per-frame render job's data write, on a worker thread) and the
rarer **0x5618D9** (a read of `[[global]+0x9C]`), with exit code 0xC000041D in the first case
(`message-queue-and-fixed-buffers.md` 2). **No 9074 site matches** `[C, addresses]`. Four of the eight failures
happened while the save was still loading, before any in-game frame: the `InGame` contexts get no
`LocalMachineAppUpdate` until the game is shown (`proxy-design.md` 24.3), and EUI's unit flags are drawn by an in-game
context, so the draw list holds no flags then `[I]`. The failure stacks run through the EXE's event managers'
dispatchers and Lua frames on the main thread, not through the render job `[C, 9074's crash reports]`. So **9074's
failure sites do not point at the draw list** `[I]`. What 9074 did not record: the number of unit flags in view at the
first-turn failures (the autoplay camera's view); a 43-civ late game can have hundreds of units near the start camera,
so a draw-list crash there cannot be excluded for the first-turn runs by count, only by site.

## 7. Recommendations

### 7.1 Pin batch 4 (stock route): no row for #13406

No carry row, as for #13397 (`upstream-fix-batch-2.md` 2.8). Reasons: it needs #13379, which does not fit a 5.4.8 pin
with 5.4.8 mod files (section 3); its trigger is a Lua line nobody ships for 5.4.8; it is unmerged and conflicting
upstream; there is no live reproduction yet. Stock VP 5.4.8 has the same crash, so the stock route is not worse than
stock. It reaches the stock route with the first VP release that carries #13379 and #13406 (through the release
checklist, which must then also give both pins an `Exe` table, section 3 last item).
If the MC wants stock-route protection before that: a fork fix branch `i0-fix-13406` with a minimal port (#13379's
resolver for #13406's symbols only, the old `Game` resync functions kept, #13406's growth, `BIN_HOOKS`-gated) and a
DLL-side trigger from section 3 (option a, the `LocalMachineAppUpdate` listener, is the one whose timing matches
upstream's); about 4-6 h with the live reproduction of 5.7 [I, estimate]. Severity for the batching rule: medium (a
crash in normal play on large late games zoomed out with EUI, deterministic at the count, no workaround but zooming in).

### 7.2 Product route: a proxy ticket after 2600's merge

Scope: section 5 (frame hook in the UI bridge's install, `proxy_uidraw.cpp`, the option, loud failure, usage lines),
the reproduction of 5.7 on the product route with the growth off and on, the parity check of 5.3, the offline unit test
against a fake EXE image as `rt_tests/shell_unit.cpp` does for the shell (layout mismatch, NULL allocation, pending
for ever, a stream reset to 1 MB, two distinct stereo streams), ASan once. Decisions it needs from the MC before code:
(1) the EXE allocator calls by address (0x82C090, 0x813A80, 0x813D10, 0x813CF0) under D136 / D144 / D170 ("any EXE call
comes to the MC first"); (2) the failure form of 5.4 (non-fatal report with popup, or without); (3) the option's
category and default (D169). Section 5.6 waits for a VP release with #13406.
**Answered (MC, 2026-11-17 14:45, tracker 0487):** (1) allowed (D136, D144), only after the known-EXE check and every
read-only cross-check pass, no code byte (D4), the Steam check untouched (D65); (2) non-fatal and loud with the popup
once per session; (3) `[Renderer] UiDrawListGrowth`, default 1. Built as ticket 0487, section 9.

## 8. Not verified

- Nothing here was run. The product-route overflow is inferred from the shared EXE UI code; the order of
  `LocalMachineAppUpdate` against the draw-list fill (that the current half is empty when it fires) is inferred from
  upstream's choice of `SetUpdate`, not measured; that a renderer restart recreates the 1 MB streams is inferred.
  **Corrected 2026-11-17 (ticket 0487, section 9.3) [L]:** the product-route overflow was reproduced (433-454 units in
  view, growth off) and the current half is empty when the listener runs (command count 0 at every hook, growth done in
  2 frames). The renderer restart is still not verified.
- The capacity figures of 2.2 are arithmetic on single 2026-09/10 measurements; the command count per flag is the
  440-unit crash's average including the screen's base fill.
- Whether the command-array overrun explains the 0x5618D9 crash (2.2) is an inference.
- #13379 breaking VP 5.4.8's `DiploCorner.lua` (section 3) is read from the sources, not run.

## 9. As built: ticket 0487 (account B, branch `task-0487-ui-draw-buffers`, 2026-11-17)

### 9.1 Code [C]

- `proxy/src/proxy_uidraw.h/.cpp` (new): the checks and the growth. Ported from #13406 head 8cfa46a83 (GPL-3.0, origin in
  the file's header, D22); the check semantics (abs32 = an address in this process, rel32 = an `E8`/`E9` whose VA target
  equals, val8 / val32 = the operand's value) from upstream's resolver `CvExeSymbols.cpp` at 6c80fa023. Nothing taken
  from this project's dumps (D83). The table: 7 code patterns (start-up site 0x5C991B, `g_pUIManager` read 0x5C9A1F,
  constructor 0x8CE150, destructor 0x8CCD20, `ClearStream` 0x8CD490, `operator new[]` 0x82C090,
  `FireMallocAlignedNoTracking` 0x813A80) and the pull request's 22 DX11 cross-checks; the two frees (0x813D10,
  0x813CF0) have no pattern, the destructor's calls vouch for them, as upstream does. Every read is preceded by
  `VirtualQuery` (proxy_shell.cpp's rule); `OnFrame` also runs under `__try` (an access violation = failure `fault`).
- The allocators are called cdecl as `(size, align, file, line, heap 9, tag)`: data 2 x 4 MB, align 16, tag 0xE;
  commands 2 x 16,384 x 32 B, align 1, tag 0xC; frees cdecl `(pointer)` of the lower pointer of each old pair.
- Differences from upstream, on purpose: (a) a stream found at the stock sizes again after a growth (same object or a new
  one) is grown again, at most `kMaxRegrowths` = 4 times per process, then failure `regrowth` (upstream's Lua stops after
  the first success); (b) "pending" for 600 frames is a failure (`pending`), as is "no stream readable" for 600 frames;
  (c) at any failure a growth in flight that has not swapped a half yet gives both new blocks back (upstream keeps them);
  (d) a data block that is not 16-aligned counts as an allocation failure; (e) DX11 only (the product needs that EXE),
  three growth slots (upstream two).
- Time stamp other than 0x546CD0A8: one line `uidraw: off: not the DX11 EXE build ...`, no report, no popup (0492's
  precedent; the product refuses other builds elsewhere). Every other check: loud.
- `proxy/src/proxy_ui_bridge.h/.cpp`: `UiBridge::SetFrameHook(fn)`, called by the drain (the `LocalMachineAppUpdate`
  listener) after the queued raises; with a hook and `ui_bridge=0` (the product) the bridge installs the drain alone
  (log line `ui bridge: gen N installed (drain only, for the frame hook) ...`): no event listener, no `UnitMoving`
  replacement, no test table. `UiBridge::Active()`; `Stats::frameHookCalls`. The bridge stays EXE-free (R2): the EXE work
  is in the hook.
- `proxy/src/proxy_ui_bridge_rt.cpp`: reads `[Renderer] UiDrawListGrowth` (`Vp64Opt::kUiDrawListGrowth`, id 3 on this
  branch), sets the hook, and writes the failure's D119 report (stage `uidraw`, code = the failure's, the status line in
  the session lines) and starts `vp64helper popup` once, on the game-core thread at the next `ICvGame1::Update`, never
  on the main thread inside the Lua listener. Interim form (MC 2026-11-17): built from `ProxyIniSettings()` (proxy,
  x64 DLL and server hashes without expected values, the run folder found by `<date>-<time>-<pid>-<n>` under the run
  root, vpproxy.ini and the options file copied); a shared `ProxyRtInt::ReportNonFatal` in `proxy_rt.cpp` replaces it
  after epic 0437 merges (follow-up filed by the MC).
- Log lines (all start `uidraw:`): the option, `EXE checks passed (...)`, `stream 0x... growth started: ...`,
  `grown: stream 0x..., 4194304 B, 16384 commands, after N frames`, `usage: the UI draw data reached P % ...` at each 10 % step
  (LOUD from 90 %), `FAILED: <code>: <why>; ...`, and every 3,600 Updates the status line `uidraw: <state>; grown N
  (regrown N); frames N; data high-water B of B (P %)`.

### 9.2 Offline tests [C, 2026-11-17]

`python -I proxy/tools/test_proxy_rt.py --build <any> --scratch <dir> --only-uidraw --asan` (section 8 of the runner;
`proxy/tools/rt_tests/uidraw_unit.cpp`): **127/127** (130/130 since the TEST ONLY switch of 9.6 added its case) on the proxy's toolchain, the `--expect-fail` negative (exit 1, one
FAIL line), **127/127 (130/130) under MSVC `/fsanitize=address`** (x86, canary caught, peak 487 MB under memcap). The fake image
is built from the same table, so its control case also checks that the pull request's patterns and cross-checks agree
with each other (they do: no cross-check writes over a fixed pattern byte with another value). Cases: growth in both
parities with the allocator arguments, the old blocks freed exactly, 3 MB / 12,000 commands then written into the grown
halves (ASan sees the real sizes), usage levels; two stereo streams; layout mismatch (data, commands); NULL data, NULL
commands, misaligned data (the other block given back); pending 599 / 600 frames (the unused blocks freed); no UI
manager; streams arriving late; reset to 1 MB (same object, new objects) and the regrowth limit; each check negative
(time stamp not loud; a byte of each pattern, abs32, rel32 target and opcode, val8, val32, image too small, no image);
the product path in a program that is not the EXE (off, not loud); option 0.

### 9.3 Live reproduction of 5.7 on the product route [L, 2026-11-17, workstation c5h-a]

Setup: development layout of `proxy/evidence/0487/s01_build.sh` (server.exe and proxy built at branch commit 4a383d8e9,
`repo_dirty=1` from an uncommitted tracker line; proxy d9f595b8...; the x64 DLL is the pin's 29F58E17..., its libraries
equal the pin's), installed by `provision.py set-proxy` (`vpproxy.ini` mode=on, `ui_bridge` not set = 0), no compositor
(ServerRenderer = 0), `vp64helper.exe` beside the proxy; save `C5H-0487-HUGE_0350` (a copy of `C5H-VP8P-HUGE_0350`,
SHA-256 c499d3c6..., Huge, turn 350, CP + VP + EUI), client 2554 x 1410. Driver `proxy/evidence/0487/live_0487.py`:
the camera fully out over the capital (12 x `Events.SerialEventCameraOut`), VP's CameraView asked for the plots on
screen (**533**, as in 2026-09; 292 land and 173 water usable, 68 skipped: cities, mountains, other majors' land), then
20 units per step with 5 s after each: `UNIT_MACHINE_GUN` on the plots without a combat unit, land nearest the capital
first, then water (embarked), then `UNIT_WORKER` on the same plots; "in view" = units on those 533 plots visible to the
player's team. Pictures in the Visual Checks feed (issue 0487, DIAGNOSTIC: development layout, no compositor).

| Arm | Growth | Result | uidraw lines |
|---|---|---|---|
| on1 (15:30-15:39) | 1 | **survived 962 units in view** (411 MG + 457 workers placed, 46 steps; plots exhausted before 1,000); stop rc 0 | install (drain only) at 74.6 s; EXE checks passed at 101.8 s; grown after 2 frames (111.9 s); usage 10 % (440 KB) 150 s, 20 % (844 KB) 264 s, 30 % (1,270,624 B) 402 s |
| off1 (15:42-15:47) | 0 | **the EXE crashed** after step 18 (433 units in view at step 17, 454 at step 18; 4 s into the settle): exit 0xC0000005, access violation reading 0x00000017 at **VA 0x009082C2** (RVA 0x5082C2) on the main thread (the proxy's boot thread), no box; the proxy's crash filter (0419) wrote `exe-crash.txt` and its LOUD line at 278.8 s; the dialog watcher marked it (signature `exe-crash: 0xC0000005 ... +0x5082C2`) | `UiDrawListGrowth = 0: off` only |
| off2 (16:04-16:09) | 0 | **the EXE crashed** after step 18 again (433 -> 454 units in view): exit 0xC0000005, read of 0xFFFFFFFF at **VA 0x0090F32A** (RVA 0x50F32A), main thread, no box | `off` only |
| on2 (16:09-16:19) | 1 | **survived 962 units in view** (the same 46 steps); stop rc 0 | install 88.7 s; checks passed 120.3 s; grown after 2 frames (131.0 s); usage 10 % (440 KB), 20 % (840 KB), 30 % (1,270,624 B) |

Findings [L, n = 2 per arm: both off arms crashed in the same step, both on arms ran to the end]:
- **The product route overflows at the same count as stock** (433-454 units in view here against 440-450 in 2026-09 on
  stock, same save family, same 533-plot view): the inference of section 4 holds. **With the growth on, 962 units in
  view ran without a crash** (the plots in view were used up before 1,000), the data use reached at least 1,270,624 B,
  above the stock 1 MB capacity.
- The fault site differs from 2026-09's (VA 0x609192 on the render worker, 0x5618D9): here VA **0x9082C2** (off1, reading 0x17) and **0x90F32A** (off2, reading 0xFFFFFFFF), both on the main
  thread [L]; two sites in two runs, as 2026-09 also saw more than one. [I] Another symptom of the same overrun (the command array or the data block written past
  its end damages a neighbour on the EXE heap, section 2.2); which buffer overran first was not measured here.
- **The current half is empty when the hook runs** [L]: every usage line read the command count at 0 at the hook
  (`command count now 0 of 16384`), and the growth finished in 2 frames: `LocalMachineAppUpdate`'s listener runs between
  frames, as upstream's `SetUpdate` trigger does (section 8's first open point, answered). So the command count's
  high-water cannot be read at the hook; only the data use (+0x2074, the last frame's) is reported.
- The growth's own cost: install at 74.6 s (first Lua call into the InGame context after the load), EXE checks and
  allocations at 101.8 s (the first drain), grown at 111.9 s; the data block came from above 2 GB (0x8FECC030; the EXE
  is large-address aware).
- Pictures (Visual Checks, issue 0487): on1 start, 312, 454, 605, 805 units and the end (962); off1 start and 312.

### 9.4 Parity of 5.3 [L, 2026-11-17 15:48-16:01, c5h-a]

`proxy/evidence/0487/s03_parity.sh` (proxy-design 24.5's method): the same build and layout as 9.3, `proxy/tools/live/
arm.py` twice on `C5H-0487-TINY_0008` (a copy of `C5H-0167-TINY_0008`, SHA-256 b2ceb663...), `--turns 30` of the harness
checklist (AI turns with its UI steps), `[Renderer] UiDrawListGrowth` = 0 then 1 (the growth ran: grown after 2 frames at
48.0 s). `eq_stats.py`: **after the load 362 keys, identical; at the end (turn 39) 432 keys, identical, 0 differing
keys**. Both arms rc 0, checklist rc 0, stop rc 0. (`idle_window.py` returned 4 in both arms: its idle measurement,
not part of the parity; not looked into.) So the growth changes no game state (D164), as 5.3 expected. Not run: the
windowed stock-versus-product identity runs of D164 with the growth on.

### 9.5 Suites at the branch [C, 2026-11-17 16:20-16:24, `proxy/evidence/0487/s04_suites.sh`]

`test_proxy_rt.py --build <this build> --asan`: **131/131** (sections 1-8; section 8 = 9.2's uidraw unit and ASan runs); `proxy/tests/ui_bridge/test_ui_bridge.py`: **57/57** on the proxy toolchain and 57/57 under ASan (the bridge's own suite, unchanged, passes with the frame-hook changes); `options` `test_options` passed in the x86 and x64 trees (`ipc/build.py --arch both --test --target test_options`; ctest's six ipc tests are "Not Run" because only that target was built) and **183 passed, 0 failed** under ASan (`options/test_asan.py`, canary caught; 164 before 0487); `test_proxy_pack.py`: **70/70**.
Re-run 16:34-16:40 on the final build 24c3c313f (`repo_dirty=0`; proxy 8703f54a..., the report paragraph of 9.6): the same results (test_proxy_rt 131/131 with --asan, uidraw 130/130 plain and ASan, ui_bridge 57/57 + 57/57, options x86 and x64 passed and 183/0 under ASan, test_proxy_pack 70/70). `test_proxy_session.py --asan`: 126/147, the 21 failures all in the cases that corrupt the pack's x64 DLL (`pack-hash`, `pack-hash-d33`, `fluidfow-only`, `failed=0`, `no-helper`, `blocked`): the test appends to `VP64\CvGameCore_Expansion2.dll`, but since 2606 (4040b638e) `pack.py` writes the manifest's x64 DLL as `CvGameCore_Expansion2_x64.dll`, so the pack still matches and the boot succeeds [C, the test's line 335 and the scratch layout]. Not 0487's: neither file is in its change; reported to the MC.

### 9.6 The loud failure, live [L, 2026-11-17 16:29-16:32, c5h-a]

Build cce79864e (`repo_dirty=0`, proxy 6ea2f4af...), the same layout as 9.3, `C5H-0487-TINY_0008`, the TEST ONLY
environment variable `VP64_UIDRAW_TEST_FAIL` in the launch's environment (`live_0487.py failtest`; it makes the growth
fail at its first frame through the real failure path, after the EXE checks): `uidraw: FAILED: test: ...` at 42.55 s
(the first drain), at the next Update (on the game-core thread, 2.1 s later) `uidraw: report: written to <user
folder>\VP64\reports\20261117-163004 (report.txt and 7 attachment line(s))` and `uidraw: helper: started
"...\vp64helper.exe" popup --report "..."`; the dialog watcher saw the patch box "Vox Populi 64-bit patch: error report"
with the summary line "The patch could not enlarge the game's interface drawing buffers (uidraw: test); the game goes on,
but many unit flags on screen may crash it."; the game went on and was stopped by the harness (stop rc 7: the helper
left running, as designed); the popup was then closed by the claim's owner with `cli.py dismiss --button Close
--patch-popup` (cloaked, not seen, not foreground). report.txt: stage `uidraw`, code `test`, the two detail lines, the
proxy's build line, the proxy, x64 DLL and server hashes (no expected values: the interim form has no pack manifest),
the tails of vpproxy.log, server.log and stubcalls.log, vpproxy.ini and VP64Options.ini copied. Found here and fixed
after the run: report.txt's generic first paragraph ("At the start of a game the game went on as the stock game...")
does not fit a non-fatal stage; `ProxyReport::Input::nonFatal` now gives its own paragraph (proxy_report.cpp; not
re-run live, the suites of 9.5 were re-run on the final build).

### 9.7 Not verified (2026-11-17)

- A renderer restart (graphics settings change, device loss) re-creating the streams: the regrowth path is covered by
  the unit test only.
- Stereo 3D (two distinct streams): unit test only.
- The command array's use: not readable at the hook (always 0 between frames); its overflow before the data's (2.2)
  stays an inference from 2026-09/10 numbers.
- The windowed stock-versus-product identity runs of D164 with the growth on; the unload of the proxy while an InGame
  context holds the drain closure (the same premise as 24's bridge: the InGame context is gone before the front end can
  unload the DLL [I]).
- The interim report (no pack manifest values, the run folder found by name) is replaced by a shared
  `ProxyRtInt::ReportNonFatal` in proxy_rt.cpp after epic 0437 merges (MC follow-up).
