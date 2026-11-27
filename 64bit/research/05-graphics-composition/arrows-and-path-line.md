# The ranged-attack arrow and the move-order path line: stock, D33, and the route (epic 0179)

Opened 2026-11-14 19:36 EST by task 3801 (epic 0179 C1a, the live half of C1; brief
`docs/briefs/epic-0179-arrows-and-path-line.md`; issue `tracker/issues/3801-arrows-path-live-check-and-design.md`;
branch `task-3801-arrows-live`). The static EXE side (the arc builder's behaviour, `ArrowShader`, `ArrowSettings.xml`,
MovementPath's code, pass A, the server's path-call handler) is task 3802's
`research/02-exe-internals/arrows-and-movement-path.md`. Marks: [C] confirmed by decompile or dump (here: the RenderDoc
capture's own records), [L] verified live, [I] inferred. EXE behaviour is described, never quoted (D83); no vertex,
texture or shader bytes are in this file, only counts, formats, states and the arc's own world positions. Addresses are
DX11 VAs at ImageBase 0x400000.

**Status:** written 2026-11-14 19:36-20:06 EST (done, for review); the MC ruled the route on its evidence while it was written (MC #1009,
19:35: the path takes route (b) until increment 3 on condition of the diagnostic arm of section 7; MC #1030, 19:56: the
arrows take route (b) in increment 2; follow-ups 0199 = the server arc, 0300 = the path models drawn by the server, MC
#1038). Re-plan: the brief's section "Re-plan after the rulings".

## 1. Summary and the route per item

**The result in six lines.** (1) Stock draws a red 3D arc for an aimed ranged attack: two world-pass draws of a generated
468-vertex mesh into the back buffer with depth, no texture (P1, P2 hold). (2) Stock's move-order path is 54 world-pass
draws of **rigid Granny model meshes** (discs, chevrons, destination ring) with `UIShader` and three textures (P4 holds,
P5 fails). (3) **Under the shipped A'' form the EXE already draws the arc** over the joined picture: Arrows is on in
config.ini and its view bit is kept (P3 fails). (4) With MovementPath also on (an instance-local change), the EXE draws the
path over the joined picture as stock does, no crash, about +5 MB in the EXE (section 7). (5) The path's node calls cross
to the server with MovementPath off (P6), the bridge's `DisplayMovementIndicator` records carry the bool (P8), and a wrapper
of `InGame`'s `Events.SpawnArrowEvent` gets the arc's four integers while the EXE keeps drawing it (P9). (6) So both items
take **route (b)** in increment 2 (the EXE draws them; the MC's rulings), and the server's versions are later work (0199,
0300).

| Item | Stock | Under A'' today | Route (MC) | What remains in 0179 |
|---|---|---|---|---|
| Ranged-attack arc | 2 draws, generated mesh, ArrowShader, back buffer + depth | **drawn by the EXE** (view bit kept) | **(b)** in increment 2 (#1030); server arc = 0199 | nothing to build; the evidence of section 5 and 7 (depth, colour, on/off) |
| Move-order path (discs, chevrons, ring) | 54 draws, rigid `.gr2` meshes, `UIShader`, back buffer + depth | not drawn (MovementPath off) | **(b)** until increment 3 (#1009): MovementPath leaves the D33 key list (a product change, with 0192) | the key-list change and its checks; the server's path = 0300 (after 0041) |
| Path turn labels "<1", "1", ... | UI (`PathHelpManager.lua`) | drawn | stays in the EXE's UI | - |

### 1.1 Premises P1-P11 (the brief's table; the lead copies it into the epic issue)

| # | Premise | Result | Mark | Evidence |
|---|---|---|---|---|
| P1 | Stock draws a 3D arc for an aimed ranged attack | **holds** | L, C | stock picture 19:38 (`stock_arrow_on`), capture frame2846 draws 475-476; section 3 |
| P2 | The arrow draws into the back buffer in the world pass with depth | **holds** | C (capture) | back buffer RTV + 2554x1411 D24S8, `LESS_EQUAL`, write on; section 3; 3802 1.7 agrees from code |
| P3 | Under A'' the EXE draws no arrow | **fails** | L | the EXE draws the arc over the joined picture (`vc-20261114-195012-workstation-d8a4`); Arrows view bit kept (config 0xff640767, view 0xff440766; `proxy_shell.cpp:796`); section 5 |
| P4 | The disc and chevrons are MovementPath's render, not UI Lua | **holds** | C, L | 54 world-pass model draws in frame6496; the labels are UI; section 4 |
| P5 | The disc and chevrons are flat geometry, not Granny models | **fails** (risk 1), softened: rigid meshes | C | IMMUTABLE stride-32 VBs without skin input, per-object world matrices; 3802 2.1 [C] names the `.fxsxml` / `.gr2` files; section 4 |
| P6 | Under D33 the EXE still calls the path functions on a preview | **holds** | L | proxy per-method counters: 3 `TEMPCalculatePathFinderUpdates` and 18 of each getter per preview, MovementPath off and on; section 5 |
| P7 | The node answers are served where a copy reads no DLL object | holds (3802) | C (3802) | `research/02-exe-internals/arrows-and-movement-path.md` 4 |
| P8 | The bridge's `DisplayMovementIndicator` records carry the bool | **holds** | L | kind-4 record #1000 `a 1 0 0 0` for a `true` raise; Lua listeners get the bool too; section 5, 6.2 |
| P9 | A wrapper of `InGame`'s `Events.SpawnArrowEvent` records the four integers and keeps the EXE's arc | **holds** | L | wrapper recorded `50,14,49,16`, arc still drawn (`vc-20261114-193850-workstation-6cfc`); section 6.1 |
| P10 | `ArrowSettings.xml`, the arrow art and the path art resolve from the install | answered by 3802 | C (3802) | loose files in `Assets/UI/Overlays/`; the arrow has no texture; 3802 0 |
| P11 | Every install compared has VT | holds for c5h-c (asset-pipeline.md 1.7) | L (earlier) | not re-hashed here; no colour figure is claimed beyond the by-eye judgements |

## 2. Runs, pins and tools

| Run | When (2026-11-14, EST) | Configuration | Save | Evidence (never committed, D83) |
|---|---|---|---|---|
| stock | 19:34-19:44 | c5h-c; 32-bit pin `i0ship-26234371-ia` (SHA-256 3c2f47da...); `darkred.py --arm stock` (the instance's config.ini, `OverlayDebug` and `ColorKeyEdit` off); no proxy, server or composite; RenderDoc 1.47 preloaded (D57) | `C5H-0179-TINY_0009V` = copy of c5h-c's `C5H-3600-TINY_0009V` (SHA-256 27420a41..., turn 9, every plot revealed and visible; CP/VP/EUI); removed at the end | captures `c5h-c_20261114-193435_frame2846.rdc` (arrow, 110.2 MB) and `..._frame6496.rdc` (path, 101.5 MB), on `C:/c5-64bit-work/0179/3801/captures`, archived to `D:/c5-64bit-work/0179/3801/` |
| joined | 19:46- | see section 5 | same copy | section 5 |

Asset set: c5h-c's install, VT present (asset-pipeline.md 1.7 lists c5h-c's folder hash; not re-hashed here; no colour
comparison is made in this file). Instance restored by SHA-256 after each arm (`run_3801.py restore-*`; verify --quick
rc 0 at 19:45).

Stimuli (diagnostic, named in every record; `shell/evidence/0179/3801/drive_3801.py`): every unit of the active player put
to sleep (Lua `PushMission(MISSION_SLEEP, ...)`, 2 units); an **Archer** (unit 1039) created by Lua `InitUnit` at (50, 14)
next to the capital Honolulu (51, 14) and a **barbarian Warrior** (unit 1040, player 63) at (49, 16), distance 2, for which
`CanRangeStrikeAt` is true. Arrow: `UI.SelectUnit(archer)`, `UI.SetInterfaceMode(INTERFACEMODE_RANGE_ATTACK)`,
`UI.LookAt` the midpoint plot, then **the harness moves the cursor** onto the target plot's pixel (player input; pixel from
the EXE's own camera, grid and heights through `gt_reader`, as 3702's Geo). Path: the Warrior 1002 at (50, 13) selected,
`INTERFACEMODE_MOVE_TO`, LookAt (51, 13), cursor onto (53, 13). Off: `INTERFACEMODE_SELECTION`, `UI.ClearSelectionList()`,
cursor parked (2404, 1310). Lua listeners in the `InGame` context record every `SpawnArrowEvent`, `RemoveAllArrowsEvent`,
`DisplayMovementIndicator` and `UIPathFinderUpdate` call with `select('#', ...)` and the values.

Analysis: `cli.py rdc-summary` (zip.xml conversion, hidden desktop, `quietlock.py run`), 3701's `overlay_census.py --all`
(materials by shader hash against the player's EXE), `shell/evidence/0179/3801/export_3801.py` (3601's `xml_export.walk`
on chosen draws) and `summ_3801.py` (one block per draw). Outputs were written under `C:/c5-64bit-work/0179/3801/analysis`; the summaries, the cited captures, all pictures and the run logs are archived in `D:/c5-64bit-work/0179/3801/` (`archive_3801.sh`, 2026-11-14 20:04, 374 MB); the C: scratch (builds, conversions) is removed.

## 3. Stock: the ranged-attack arc, as the capture shows it [C capture, L picture]

**P1 holds [L, C]:** with the archer selected, range mode on and the cursor on the barbarian, stock draws a **red 3D arc**
from the archer's plot up and over to the target, with an arrow head over the target (Visual Checks: the picture of
19:38, `stock_arrow_on`; the P9 picture `vc-20261114-193850-workstation-6cfc`). It is drawn together with the fire-range
ring (Terrain Overlay target), the red target marker over the barbarian and the attack-preview panel. 3702's huge-save
`x_arrow` shot named no arc: at its camera and zoom the arc was not told apart from the beam (not re-checked here).

The arc in frame 2846 (1212 draws in the frame, 136 command lists, none missing) [C]:

| Item | Value |
|---|---|
| Draws | **2**, frame draws 475 and 476, both `DrawIndexedInstanced`, 1260 indices (420 triangles), the same VB and IB, on a deferred context, between the world's model draws and the UI |
| Target | the **back buffer** (RTV R8G8B8A8_UNORM) with the scene's **2554x1411 D24_UNORM_S8_UINT** depth buffer: the world pass, not an offscreen target (**P2 holds**) |
| Depth | test on, `LESS_EQUAL`, **write ALL** in both draws; no stencil |
| Raster | solid, **cull FRONT**, `FrontCounterClockwise` true, depth clip on, no bias |
| Draw 475 | blend off, write mask **ALPHA only** (an alpha / depth pre-pass) |
| Draw 476 | blend `SRC_ALPHA` / `INV_SRC_ALPHA` add (alpha `ONE` / `ONE`), write mask ALL |
| Shaders | VS SHA-256 `9c5bf893...`, PS `2525c4ee...` (not in the material table's SM5 rows that `known_blobs` reads; 3802 maps them to `ArrowShader`) |
| Constants | one buffer, slot 1 of VS and PS, 96 B: rows 0-3 the view-projection matrix, row 4.x **0.5**, row 5.x **0.7** (3802: alpha and ambient); 4.y/4.z hold non-float values |
| Textures, samplers | **none** bound |
| Input layout | stride 40: `POSITION` R32G32B32_FLOAT @0, `NORMAL` R32G32B32_FLOAT @12, `COLOR0` R8G8B8A8_UNORM @24, `COLOR1` R8G8B8A8_UNORM @28, `TEXCOORD0` R32G32_FLOAT @32 (the texcoords hold uninitialised values: not read, or read and meaningless) |
| VB / IB | dynamic, 160,000 B (4000 vertices) / dynamic R32_UINT 12,000 B (3000 indices); **468 vertices used** (indices 0-467) |
| Mesh | **25 rings of 16 vertices** (v0-v399) along the curve, then a **head of 68 vertices** (v400-v467). Per ring: 8 vertices COLOR0 (255, 0, 0, 255), COLOR1 (255, 255, 255, 255) with normals (the red body), then 8 vertices COLOR0 white, COLOR1 (0, 0, 0, 0), normal 0 (a second band; [I] the edge or outline). Indices start `0,16,1, 1,16,17, ...` (quads between ring k and k+1). |
| End points | ring 0 starts at world (1438.51, -224.92, 18.86); successive rings climb about 11 units each at first ((1435.87, -220.52, 30.53), (1433.24, -216.11, 41.26), (1430.60, -211.69, 51.04)); the head sits near (1373-1390, -107 to -120, 15.9-33.6) over the target plot |

The arrow is **not a model**: flat-shaded generated geometry with vertex colours and no texture, drawn by its own shader
into the world pass with depth. Route (a) for the arrow is not touched by risk 1. The captured VB (468 vertices) with the
arc's inputs (archer (50, 14) to target (49, 16), heights from the height field) is C3's bit-level reference: kept in the
capture (`frame2846`, VB resource 5294, IB 5295), never committed. 3802 describes the builder that makes it (rings per
0.04 step of the curve parameter: 25 rings for t = 0 .. 0.96, then the head).

## 4. Stock: the move-order path, as the capture shows it [C capture, L picture]

Picture (19:39, `vc-20261114-193945-workstation-fc52`): grey **turn discs** on every path node, with the UI's labels
"<1", "1", "<2", "2" on them, grey **chevrons** between the nodes, and a larger **ring** on the destination.

Frame 6496 (1159 draws) [C]: **54 draws**, frame draws 775-828, all with VS `7ce2cf44...` / PS `7d85ec9f...`:

| Item | Value |
|---|---|
| Target, depth | back buffer + the scene's D24S8 depth; test `LESS_EQUAL`, **write ALL**; cull BACK; blend `SRC_ALPHA` / `INV_SRC_ALPHA` |
| Meshes | each draw its own **IMMUTABLE** VB (stride 32: `POSITION`, `NORMAL`, `TEXCOORD0`; **no bone indices or weights**) and R16 IB: **rigid model meshes** loaded once (3802: the `.fxsxml` / `.gr2` files in `assets/UI/Overlays/`), placed by a per-object world matrix |
| Constants | cb0 (256 B): rows 0-3 the object's world matrix (rotation about z, translation in world units, e.g. (1471.9, -277.06, 19.52) for the start node), more per-object values after; cb1 (128 B): a second matrix and the view-projection |
| Textures | t0 128x128, t1 128x128 (per mesh), t2 32x32 (shared by all); three linear-wrap samplers |
| Draws for the 5-node preview (50,13)->(51,14)->(52,14)->(52,13)->(53,13) | node discs **1056 indices x 5** (one of them, the start node, with another t0) and an inner part **384 x 5**; connectors **6 + 90 indices x 21** (two meshes each; 6 + 6 + 6 + 3 over the four steps); destination **576 + 1392** once |

**P4 holds [C, L]:** the discs and chevrons are drawn by the EXE's world pass (MovementPath's models), not by UI Lua; the
turn labels on them ("<1", "1", ...) are UI (PathHelpManager.lua) and stay in the EXE under D33 (overlay-system.md 12.4).
**P5 fails as written [C]:** the disc, the chevrons and the destination ring are **model meshes** from the game's art
(3802 [C]: Granny files loaded at MovementPath's start-up), not generated flat geometry. In the frame they are rigid and
static (IMMUTABLE buffers, no skinning input), so drawing them needs the mesh, its textures and the world matrices, not a
pose or animation, except for 3802's animated `SelectedAni*` variant [I: not seen in this frame].

## 5. Under D33 today, the shipped A'' form [L, 2026-11-14 19:46-19:52, c5h-c]

Arm: the D33 product path as 3702/3706 run it (`run_3801.py prep-joined`, which reuses 3702's chain to 3308: SHA-256
backup, `provision.py set-proxy`, composite + manifest beside the proxy, `VP64Options.ini ServerRenderer = 1`, the shipped
composite ini, D33 in the A'' form: the 13 keys of `proxy/src/proxy_session_keys.h` off in config.ini, River, Arrows and
Overlay on); server.exe 71a0c296..., proxy 5651efed..., composite 78ce2fc6... (release, built from this branch at a2cb8b59
= main 6afb5419 + the epic's ops commits; `build_3801.sh`), x64 DLL 74588135 (pin `x64-04b1211b`), `WhiteFogMask = 1`
(default, 0185), plus `vpproxy.ini ui_bridge=1 ui_bridge_test=1`. Run folder `.../Logs/VP64/run/20261114-194726-29928-1/`
(copies archived in `D:/c5-64bit-work/0179/3801/logs/joined/out-hold-A2/`). Same save copy, same Lua-created pair (archer 1039 at
(50, 14), barbarian 1040 at (49, 16): `InitUnit` gave the same ids and plots), same stimuli and cameras as stock. Guard
checks 0 at start and end, stop rc 0, 0 LOUD lines in `server.log`, no crash.

**P3 fails [L]: the EXE draws the arc under the shipped A'' form.** Picture `vc-20261114-195012-workstation-d8a4`
(reference: stock at the same camera, the target pixel equal to 0.03 px): the red arc from the archer to the barbarian
over the joined picture, the same shape as stock's. Range mode ended (`j_arrow_off`,
`vc-20261114-195020-workstation-7ed9`): no arc left. Strongly red pixels (R > 170, G < 110, B < 90) in the arc's box
(1000-1600 x 330-1000): stock on 18,475 / off 5,258; joined on 25,247 / off 5,712 (the arc is drawn at alpha 0.5, so the
lighter D33 ground under it, no forest, shows through more red).

Why [L, `vpproxy.log` of the run; C, code]: the proxy's view rule is "view &= config | 0xFF000000" plus the rule bits
(`proxy/src/proxy_shell.cpp:796`, `dwAllowed = (dwConfig | ~KNOWN_BITS | g_dwKeep) & ~dwClearBits`; `ClearBits()` at
`:781` = Terrain 0x1 and Waves 0x200000 and the marker's `clear=`). Logged at boot: `config 0xff640767 keep 0x00000000
clear 0x00200001: view 0xffffffff -> 0xff440766`. Arrows is **on** in config.ini under A'' (config bit 0x400000 set), so
its view bit stays set (view 0xff440766) and the EXE's world pass B calls the Arrows render. `NEVER_RESTORE`
(`proxy_shell.cpp:509`, River and Arrows) only stops the proxy from **setting back** bits it cleared itself; nothing
clears the Arrows bit in this form.

**Proposed corrections (dated 2026-11-14 by 3801; the edits of the other files wait for the MC's grant):**
- The MC's brief, "What exists today", row "Arrows system under D33 (A'' form)": replace "its view bit 0x400000 cleared by
  the proxy (`NEVER_RESTORE` ...), so its render is not called in pass B [C]; the arc is not drawn [L, 12.4]" by "its view
  bit 0x400000 stays set (config 0xff640767, view 0xff440766; `proxy_shell.cpp:796`); the EXE draws the arc over the
  joined picture [L, 3801, Tiny, 2026-11-14]".
- `overlay-system.md` 12.4, row "Ranged-attack arrow": "the arrow proper is Arrows (on in config.ini under A'', its view
  bit cleared by the proxy)" -> "the arrow proper is Arrows, on in config.ini under A'' with its view bit kept, and the EXE
  draws it under D33 (3801 [L], Tiny); 3706's huge-save shot did not show it because ..." [I: the aim did not raise
  `SpawnArrowEvent` there, or the arc was taken for the beam; not re-checked on huge].
- `world-systems-and-stubs.md` 4 and `river_arrows.md` stay true for the view-bit-cleared case (no pass-B call); the
  question of pass A (risk 5) is moot for A'' while the bit is set.

What reaches the server [L]:

| Item | Under A'' | Evidence |
|---|---|---|
| Bridge install | gen 1 into the `InGame` context at 37.3 s, 4 of 4 listeners, the drain, `UnitMoving` replaced | `vpproxy.log` |
| Kind 1 (arrow) records | one per Lua raise, **`a 0 0 0 0`** (also for a raise of `SpawnArrowEvent(11, 12, 13, 14)` from `WorldView`'s context) | `server.log` record #4; bridge stats `arrow` 2 = the Lua listener's count |
| Kind 2 (remove) | one per raise (7 = the Lua listener's 7) | stats |
| Kind 4 (movement) | **carries the bool**: record #1000, a raise of `DisplayMovementIndicator(true)`, logged `a 1 0 0 0` (the first record, a `false`, `a 0 0 0 0`); 1013 records = the Lua listener's 1011 + 2 before it was armed | `server.log` (only the first record of a kind and every 1000th are logged, so record #1000 was steered onto a `true` raise: 991 raises from a `WorldView` main-thread handler at seq 9) |
| Kind 3 (minimap) | `0 320 190 0` at the load | `server.log` #1 |
| Sequence, threads | seq 1023 at the end, 0 gaps reported, 0 wrong-thread, 0 stale | stats, `server.log` |

**P6 holds [L]:** with MovementPath off, the path preview still makes the EXE call the DLL, served by the server (the
proxy's per-method counters, `proxy/tools/proxy_snapshot.py` between the steps, `vpproxy-snapshot-methods.tsv`):

| Step | `TEMPCalculatePathFinderUpdates` | `GetX` / `GetY` / `GetTurnNumber` | `ResetPathFinder` |
|---|---|---|---|
| load to idle (3234 frames) | 0 | 0 | 4 |
| arrow on / arrow off | 0 / 0 | 0 / 0 | 3 / 5 |
| path on (unit selected, move mode, cursor 3 plots east; 459 frames) | **3** | **18 each** | 3 |
| path off (mode ended) | 0 | 0 | 5 |

All "out" (made by the proxy, served by the server), none answered locally. The Lua side saw 7 `UIPathFinderUpdate`
events during the path step (5-node tables and empty ones) for the 3 path computations [I: the EXE raises the event also
for updates that reuse its last result]. Which server thread serves them and what a copy may read is 3802's (P7).

**P8 holds [L]**, for the bridge's C listener (kind 4 above) and for Lua listeners (section 6.2). The rule of
`proxy-design.md` 24.4 is per event, not per raise: tested from both contexts, `SpawnArrowEvent` reaches Lua and C
listeners with **0** arguments, `DisplayMovementIndicator` with its **bool**, whether raised from `InGame` or `WorldView`
[L, 19:49]. Who raises `DisplayMovementIndicator`: Lua only, as far as the fork's files show (VP `WorldView.lua`
`UpdatePathFromSelectedUnitToMouse` on every `UIPathFinderUpdate` and mouse move, `highlightAlternativeMove`; CP
`InGame.lua` `ShowMovementRangeIndicator` / `HideMovementRangeIndicator`; desk check 2026-11-14) [C for the Lua, I that the
EXE never raises it]. **Proposed note for `proxy-design.md` 24.4 (dated 2026-11-14, 3801; the edit waits for the MC's
grant):** "Corrected 2026-11-14 (0179, task 3801 [L]): whether Lua and C listeners receive the arguments of a Lua raise
depends on the event, not on the raising context: `SpawnArrowEvent` and `SerialEventGameDataDirty` arrive with none,
`DisplayMovementIndicator(bool)` arrives with its bool from `InGame` and `WorldView` alike [I: the EXE declares an
argument list per event and passes Lua's arguments on only where one is declared]; the bridge's kind-4 records carry the
bool (record #1000 `a 1`)."

## 6. Hand-over mechanisms [L, stock]

### 6.1 P9: the wrapper of `Events.SpawnArrowEvent` in the `InGame` context

Probe (19:38, Lua channel, `drive_3801.py wrap`): the `InGame` context's `Events.SpawnArrowEvent` (a **table** with a
metatable; `Add` is a function; `:Count()` = this context's listeners) was replaced, by plain assignment into that
context's `Events` table, with a table whose `__call` records its arguments and calls the original, and whose `__index`
passes every other field to the original. Then the arrow stimulus (cursor onto the barbarian in range mode):

- the wrapper recorded **`50,14,49,16`**: the attacker's x, y and the target's x, y, exactly the four integers
  `Bombardment.lua`'s `DisplayBombardArrow` passes;
- **the EXE still drew the arc** (picture `vc-20261114-193850-workstation-6cfc`), and the Lua listener on the same event
  was still called (with 0 arguments, as 24.4 found);
- assignment worked (`now.same` true, `raw.isproxy` true); no Lua file was changed.

**P9 holds [L].** Not covered: a raise of `SpawnArrowEvent` from another context's `Events` table (the wrapper sits in
`InGame` only; CP raises it only from `Bombardment.lua`, which `InGame.lua` includes; the fork has no other sender,
desk check of CP/VP/EUI Lua 2026-11-14); a context reload (the wrapper goes with the environment, as the bridge's
listeners do, proxy-design.md 24.2 B4).

### 6.2 P8: what Lua listeners receive from `DisplayMovementIndicator` and `UIPathFinderUpdate` [L, stock]

| Event (raised by) | Listener in `InGame` got | Values seen |
|---|---|---|
| `SpawnArrowEvent` (Lua, `Bombardment.lua` in `InGame`) | **0 arguments** (as proxy-design.md 24.4) | - |
| `RemoveAllArrowsEvent` (Lua) | 0 (it has none) | - |
| `DisplayMovementIndicator` (Lua: VP `WorldView.lua` on every path update and mouse move; CP `InGame.lua`) | **1 argument** | `boolean:true` while the move preview is up, `boolean:false` after the mode ends |
| `UIPathFinderUpdate` (the EXE, after `UI.SendPathfinderUpdate` / the path to the hex under the cursor) | 1 argument, a table of nodes | `x/y/turn`: `50/13/22, 51/14/21, 52/14/120, 52/13/121, 53/13/220`; an **empty table** when the mode ends |

So the 24.4 rule "a Lua raise reaches Lua listeners without arguments" does **not** hold for every event: the plain
Lua raise of `DisplayMovementIndicator(true|false)` arrived with its bool [L, 2026-11-14 19:38-19:41, 23 calls]. Why the
two events differ: it is the event, not the raising context (section 5: raised from `InGame` and from `WorldView`, each
event behaves the same) [L, 19:49]; the bridge's C listener records the same (section 5).
The turn field is VP's encoding (turns x 100 + a remainder; 3802 reads it as the EXE's turn number).

## 7. The route (b) diagnostic arms: the EXE's arc and path over the joined picture [L, 2026-11-14 19:50-20:0x, c5h-c]

MC #1009 made the path's route (b) conditional on this arm; MC #1030 accepted the arc on the same checks. Arms: the A''
arm of section 5 (arc), and the same layout with **one instance-local change**, `MovementPath = 0` in c5h-c's config.ini
(config 0xffe40767, view 0xffc40766: the MovementPath bit 0x800000 kept; the session stays a D33 session), backed up by
hash with the joined prep and put back before the restore; no change to `proxy_shell.cpp` or `proxy_session_keys.h`. Run
folder `.../run/20261114-195507-40348-1/`; stimuli and cameras as in sections 2 and 5; the stock references are the stock
arm's pictures at the same camera (the target pixel equal within 0.03 px in every arm).

| MC condition | Arc (EXE, A'' as shipped) | Path (EXE, MovementPath kept on) |
|---|---|---|
| (1) depth against the server's terrain | as stock by eye: the arc rises from among the archer's models (the models hide its foot as in stock) and comes down onto the barbarian's plot; no part under the ground or floating beyond stock's own (side-by-side crop of the arc box against stock) | as stock by eye: the five discs lie on the ground, the chevrons follow the ground across the river bank, the destination ring is cut by the ground at its lower left exactly where stock's is (side-by-side crop) |
| (2) colour, blend | the same red arc and white rim, half-transparent; no dark red. More of the ground shows through (D33 has no forest under it): strongly red pixels in the arc box 25,247 against stock's 18,475 | the same grey discs, chevrons and ring; no dark red (seam G does not touch them: `UIShader`, their own textures). The white fog mask (on by default) leaves them as they are: no fog in either picture on this fully revealed save |
| (3) on / off follows the game | on when aimed; gone after the mode ends (`RemoveAllArrowsEvent`): strongly red pixels 5,712 off against stock's 5,258 | on with the move preview; gone when the mode ends (`b_path_off`: no disc or chevron; the game panned to the next unit there) |
| crash, LOUD | none; 0 LOUD lines; guard checks 0; stop rc 0 | none; 0 LOUD lines; guard checks 0; stop rc 0 |
| pictures | `vc-20261114-195012-workstation-d8a4` (on), `...-195020-...-7ed9` (off), `...-195625-...-ae76` (on, path arm) | `vc-20261114-195658-workstation-66f3` (on, **pass**), `...-195706-...-3d0e` (off) |

A load from inside the game with an arc or a path up was **not** tried (the MC's "after a load" for the arc): the remove
event is raised by `EndRangedAttack` when the mode ends, and a load ends the mode [I].

Whole-picture figures are not used: in these states the stock "off" pictures also lose the range ring, the selection and
(path) the camera position, so an on/off mask is not the item alone (`compare_3801.py` reports 2.1-2.4 M px masks; kept
in the analysis folder only). The judgements above are by eye on full-resolution crops, as 3702's item-5 pairs were.

Memory, EXE private bytes (`tools/harness/memory.py`, indications only, COMMON 23):

| Point | A'' (MovementPath off) | MovementPath on | Difference |
|---|---|---|---|
| in game, right after the load | 1258.5 MB (largest free block 2009.0 MB; a second A'' run, 20:00, `logs/mem-a2.log`) | 1264.7 MB (largest free block 1988.8 MB) | +6.2 MB private; largest free block -20.2 MB |
| after the pair, the arc and the path were shown | 1234.1 MB (2009.0 MB) (first A'' run: 1234.6 MB) | 1239.4 MB (1988.8 MB) | +5.3 MB private; largest free block -20.2 MB |

`disable-systems-and-masks.md` row 23 has +5 MB at boot for the key on.

## 8. What the server arc would need (follow-up 0199)

For the backlog; nothing of it is built in 0179 (MC #1030). (2026-11-16: built offline as `c5r::arc` by 0199 part 1,
checked against the capture: section 12.)
- **Input:** the four integers per spawn and the remove event. The wrapper of `InGame`'s `Events.SpawnArrowEvent` gets
  them exactly (P9 [L]); a listener does not (0 arguments, section 5). `RemoveAllArrowsEvent` needs no arguments. A load
  clears the list [I].
- **Geometry:** generated, 468 vertices per arc: 25 rings of 16 vertices (8 red vertices with normals, COLOR0 (255, 0, 0,
  255), COLOR1 white; 8 rim vertices COLOR0 white, COLOR1 0, normal 0) and a 68-vertex head; 1260 R32 indices (24 joins x
  48 + 108 head, 3802). Stride 40: POSITION, NORMAL, COLOR0, COLOR1 (R8G8B8A8_UNORM), TEXCOORD0 (unused). The builder's
  behaviour, its settings (`ArrowSettings.xml`, loose in `Assets/UI/Overlays/`) and the end heights:
  `research/02-exe-internals/arrows-and-movement-path.md` 1.2-1.8.
- **Drawing:** ArrowShader (VS / PS SHA-256 `9c5bf893...` / `2525c4ee...` = the EXE's SM5 blobs at 0x0619B000 / 0x0619B7F8,
  3802), one 96-byte constant buffer (view-projection, alpha 0.5, ambient 0.7), **no texture**; two draws into the frame
  with the scene's depth: an alpha-only pre-pass (blend off, write mask alpha), then `SRC_ALPHA` / `INV_SRC_ALPHA`; depth
  `LESS_EQUAL` with write in both; cull front, front counter-clockwise.
- **Reference:** the captured VB of frame2846 (archer (50, 14) to target (49, 16), turn 9 Tiny copy): bit-level comparison
  as the brief's C3 described. Two head vertices for 3802's open head normals (M1 / M2): v420 position (1387.0979,
  -111.1648, 30.3536) normal (-0.6542, -0.6151, 0.4401); v426 position (1369.6333, -118.7979, 31.3448) normal (0.8598,
  0.2590, 0.4401); both COLOR0 (255, 0, 0, 255), COLOR1 white. The head's lowest vertices sit at z 15.89-16.04 (v428, v454),
  the rest of the head at z 29-34.
- **One arc, not two:** 3802 found world pass A is not picked in normal play (`arrows-and-movement-path.md` 0, P3 and
  risk 5), and the live A'' frame shows one arc; the arc the player sees under A'' is pass B's. And the EXE's arc must then stop: the Arrows view bit cleared (a proxy change, the MC's),
  or the player sees two arcs.

## 9. What 0041 needs for the path models (follow-up 0300)

- **Meshes:** rigid, static, per asset one immutable VB (stride 32: POSITION, NORMAL, TEXCOORD0) and an R16 IB, from the
  `.gr2` files of `Assets/UI/Overlays/` (loose files, 3802 P10); index counts seen: node disc 1056 + inner 384, connector
  6 + 90, destination (SelectedAni) 576 + 1392. Which file is which, and the placement rule (CenterOneTurn on the first
  node, CenterThreeTurns after it under VP's turn values, six connectors per step and three on the last, SelectedAni on the
  last node): 3802 2.1-2.4.
- **Placement:** a world matrix per instance (rotation about z, translation to the plot's world position and ground
  height); a second matrix and the view-projection in a second buffer.
- **Shader and textures:** `UIShader` (material 81; VS `7ce2cf44...` at 0x067FC9E8, PS `7d85ec9f...` at 0x067FEBA0, 3802),
  t0 and t1 128x128 per mesh, t2 32x32 shared (3802: `Resource/DX9/UITextures.fpk`), linear-wrap samplers; blend
  `SRC_ALPHA` / `INV_SRC_ALPHA`, depth `LESS_EQUAL` with write, cull back, back buffer with the scene's depth.
- **The exception:** the `SelectedAni*` assets carry a Granny animation (3802); the frame here showed the destination as
  two rigid draws, so whether the animation moves vertices or only the matrix was not seen [I].
- **Data:** the nodes are served by the server already (P6, P7); the show and hide signal is in the bridge's kind-4
  records (P8). Reference: capture frame6496.

## 10. Decisions 2, 3, 5 (evidence only; they pass to the follow-ups) and what is not verified

- **Decision 2 (the arrow's end points):** (i) the wrapper of `InGame`'s `Events.SpawnArrowEvent` works live (P9); (ii) a
  wrapper of the global `DisplayBombardArrow` would be fragile: `BeginRangedAttack` adds the global as a listener of
  `SerialEventMouseOverHex` when range mode starts and `EndRangedAttack` removes it by the same global, so a wrapper put
  in while the mode is on leaves the original registered (desk, CP `Bombardment.lua`). The only sender in the fork's Lua is
  `Bombardment.lua` (and `DisplayNukeArrow` beside it).
- **Decision 3 (path show and hide):** (i) not needed: the bridge's kind-4 records carry the bool (P8). (ii) also works in
  timing: the counters show `TEMPCalculatePathFinderUpdates` only while the preview is up and `ResetPathFinder` at each
  mode end (section 5), and `UIPathFinderUpdate` arrives with an empty table at the mode end.
- **Decision 5 (failure list):** for route (b) items nothing new can fail at install in the server; the EXE-side risk is
  the old crash key (an Arrows view bit set while the system is off, `NEVER_RESTORE`), which A'' avoids by keeping the
  system on. For MovementPath kept on: no crash in one Tiny run.
- **Not verified** (*2026-11-14 23:16, task 3804: the state of each item now is in 11.6; the huge arc, the hill and
  coast and the parity are verified there*)**:** the huge save under A'' with an arc (3706's huge shot showed none; why not is open); an arc or path
  across a load; a hill or coast under the arc (the arc's plots are grassland and forest); the 3702 cameras; RenderDoc of
  the joined arm (the depth judgement is by eye); the parity of MovementPath on (no AI turns were played); `SelectedAni`'s
  animation; the trade-route arrows (not this epic).

## 11. Acceptance (task 3804, with 3803 and 3805) [L, workstation, 2026-11-14 20:29-23:16 EST]

Written by task 3804 (issue `tracker/issues/3804-arrows-path-live-acceptance.md`; branch `task-3804-arrows-acceptance`).
Route (b) for both items (MC #1009, #1030): the EXE draws the arc and, since 3803 took MovementPath out of the D33 key list,
the path's models; the server draws neither. Marks as in the rest of this file.

### 11.1 Builds, places, drivers

| | |
|---|---|
| Joined builds | from `a25892d5` (3803 + 3805; `shell/evidence/0179/3804/build_3804.sh`): server.exe `9a7726b5`, proxy `01dd2de7`, composite `72972595`, x64 DLL `74588135` (pin x64-04b1211b); the parity pair ran on the build from `2eb96de5` (3803 only: server.exe `3f4ec2f4`, proxy `98693cc9`, composite `77a2917f`); 3805 changes only the config.ini routine in `vp64helper.exe`, which the dev proxy layout of these runs does not even contain, so it cannot change the game's state |
| Joined layout | run_3702's chain (prep by SHA-256 backup, `provision.py set-proxy`, composite + manifest, `VP64Options.ini` `ServerRenderer = 1` and nothing else, so `WhiteFogMask` at its default on); config.ini by darkred's list after 3803 (A'' form: the 11 keys off; River, Arrows, Overlay, Minimap, **MovementPath on**); the shipped composite ini unchanged for the pictures (`lockstep_csv=1` added for the turn runs, logging only); `ui_bridge` off |
| Stock | 32-bit pin `i0ship-26234371-ia` in the CP slot, `darkred --arm stock`, no proxy or server, same instance, save and cameras |
| Saves | Tiny `C5H-0179-TINY_0009V` (copy of `C5H-3600-TINY_0009V`, SHA-256 27420A41, turn 9, every plot revealed and visible); huge `C5H-0179-HUGE_0350` (copy of `C5H-VP8P-HUGE_0350`, C499D3C6, turn 350); both removed at the end |
| Instances | Tiny on c5h-c (C:), huge on c5h-d (F:), each claimed under 0179; restored by hash, `provision.py verify` ok |
| Asset set | the instances' installs, Vibrant Textures present (not re-hashed here, D94); stock and joined on the same install |
| Drivers | `shell/evidence/0179/3804/run_3804.py` (pictures, turns, stock), `chain_tiny.sh`, `chain_huge.sh`, `compare_3804.py` (0167's compare_3702 method), `turns_summary.py`; the pictures and camera blocks are on `D:/c5-64bit-work/0179/3804/` (D83, never committed) |

Stimuli (pictures): after the load every unit of the active player is put to sleep and the auto unit cycle is off (3702's
means, diagnostic); Lua `InitUnit` creates the pairs (diagnostic, the same in every arm): **a1** = 3801's pair (Archer
1039 at (50, 14), barbarian Warrior at (49, 16); the plot between them, (49, 15), is **hills**), **a2** = Archer 1042 at
(52, 14) and a Warrior at (52, 12) **across water** (51, 13). Path: the Warrior 1002 at (50, 13) to (53, 13) (3801's).
Cameras: p1 = LookAt (51, 13), 0 wheel ticks (3801's); p2 = the same, 3 ticks in; a1 = LookAt the pair's midpoint
(49, 15), 0 ticks (3801's); a2 = (52, 13), 0 ticks. **Every stimulus in every arm was player input**: a left click on
the unit's plot, key M (move) or key B (range attack), the cursor moved onto the destination or target pixel (from the
EXE's own camera, grid and heights, `gt_reader`); no Lua fallback was needed (the records say so per camera). Off = Lua
selection mode and `ClearSelectionList`, the cursor parked over the minimap, the clicked unit put back to sleep, the
camera put back by LookAt (the off camera equals the on camera within 0.018 world units). The Warrior 1002 stays the
head selected unit in the off shots (the game re-selects it), so on and off differ by the item, its mode's overlays
(movement or fire range, target marker) and the plot tooltip. Stock: on twice, 5 s apart (the shot-to-shot ceiling), off
once; joined: on once (the first of up to 10 PrintWindow shots whose top bar matches stock), off once.

### 11.2 Done C on Tiny: the frame against stock [L, c5h-c, stock 21:30-21:34, joined 21:37-21:42]

Method: `compare_3804.py` = compare_3702's (unproject the stock state camera onto the height field, plots the team sees,
minus the interface rectangles and a 16 px frame), with item masks: **item** = stock on differs from stock off by more
than 12 (minus the plot tooltip's box beside the cursor, the selected unit's panel and the ranged-attack preview, all UI); **item_cut** = item
minus pixels that move by themselves between stock's two on shots, dilated 16 px (animated figures, water, smoke: 16 px
around models); **item_strong** = item_cut where stock on and off differ by more than 48 (the discs, chevrons, ring, the
arc's body). Columns: median / p95 / max of the largest channel difference, and the share above 12. "Joined off vs
stock off" is the same pixels with no item drawn: the D33 picture's own difference there (no forest, no decals, other
unit figures) and so the floor the item is judged against. Camera difference: max |view| between the stock and joined
state camera blocks.

| Camera | Mask | Item px | Stock vs stock | Joined vs stock | Joined off vs stock off | Joined on vs joined off | Camera diff |
|---|---|---|---|---|---|---|---|
| p1 (path) | item | 493,413 | 12 / 135 / 224 (49.1 %) | 8 / 77 / 246 (41.4 %) | 17 / 137 / 240 (57.6 %) | 20 / 143 / 255 (65.9 %) | 0.001343 |
| p1 (path) | item_cut | 59,818 | 0 / 7 / 12 (0.0 %) | 7 / 81 / 246 (40.9 %) | 5 / 86 / 211 (38.2 %) | 27 / 138 / 255 (81.6 %) | 0.001343 |
| p1 (path) | item_strong | 22,904 | 0 / 1 / 9 (0.0 %) | 2 / 52 / 194 (16.6 %) | 3 / 79 / 211 (25.4 %) | 92 / 159 / 255 (97.2 %) | 0.001343 |
| p2 (path) | item | 1,615,939 | 0 / 29 / 230 (12.1 %) | 8 / 85 / 236 (40.4 %) | 8 / 87 / 244 (41.4 %) | 22 / 103 / 255 (76.6 %) | 0.002197 |
| p2 (path) | item_cut | 985,448 | 0 / 3 / 12 (0.0 %) | 5 / 83 / 221 (34.0 %) | 6 / 86 / 244 (34.8 %) | 20 / 88 / 255 (78.3 %) | 0.002197 |
| p2 (path) | item_strong | 181,745 | 0 / 2 / 12 (0.0 %) | 8 / 103 / 221 (43.5 %) | 9 / 106 / 244 (46.1 %) | 54 / 140 / 255 (83.3 %) | 0.002197 |
| a1 (arc) | item | 1,489,447 | 0 / 32 / 234 (14.3 %) | 21 / 101 / 239 (62.0 %) | 22 / 104 / 237 (61.8 %) | 37 / 99 / 244 (95.3 %) | 0.001129 |
| a1 (arc) | item_cut | 977,401 | 0 / 8 / 12 (0.0 %) | 20 / 105 / 239 (59.3 %) | 23 / 106 / 230 (62.7 %) | 38 / 99 / 238 (99.0 %) | 0.001129 |
| a1 (arc) | item_strong | 286,970 | 0 / 8 / 12 (0.0 %) | 12 / 81 / 217 (49.3 %) | 17 / 87 / 230 (56.4 %) | 62 / 133 / 238 (99.9 %) | 0.001129 |
| a2 (arc) | item | 1,367,065 | 1 / 71 / 234 (21.5 %) | 13 / 105 / 235 (51.1 %) | 15 / 106 / 237 (54.6 %) | 22 / 120 / 247 (69.4 %) | 0.000854 |
| a2 (arc) | item_cut | 659,561 | 0 / 2 / 12 (0.0 %) | 9 / 91 / 235 (46.2 %) | 13 / 93 / 237 (50.7 %) | 18 / 101 / 247 (65.2 %) | 0.000854 |
| a2 (arc) | item_strong | 178,561 | 0 / 3 / 12 (0.0 %) | 3 / 80 / 235 (30.4 %) | 7 / 89 / 237 (39.9 %) | 71 / 138 / 247 (89.4 %) | 0.000854 |

- **The joined picture draws both items at every camera** (joined on vs off: 83-100 % of the strong pixels change) and
  **on their pixels the joined picture is no further from stock than the D33 ground under them is with no item drawn**
  (joined vs stock median / p95 at or below joined off vs stock off on the same mask, all four cameras). Stock's own
  ceiling on these masks is 0 / 1-8 / 9-12 by construction of the motion cut; without the cut (mask "item") it is
  0-12 / 29-135 / 224-234 (p1's 12 / 135 is the animated water inside its range).
- **Classes of the remaining difference, named:** (1) the D33 ground: no Forest, no Decal (fields, roads' decals) under
  the half-transparent path discs, the ring and the arc (alpha 0.5), so the same item over a lighter ground: intended;
  (2) unit figures stand and animate differently across launches (3310: not comparable across launches): the archer's
  and the barbarians' figures inside the arc box and the ring; (3) water animation (removed by the cut); (4) the
  movement-range and fire-range overlays and the selection, drawn by the server since 0167 in the joined arm, part of
  the on state in both; (5) the plot tooltip and the unit panel: UI, masked. No other class was seen on the difference
  images (`diff/<cam>__item_diff.png`). The item's own contribution, |(joined on - off) - (stock on - off)|, is
  4-23 / 49-114 on the strong masks: classes (1) and (2) again (a half-transparent item over a different ground
  contributes a different delta).
- By eye on full-resolution crops (as 3801's section 7): the discs, the "<1"/"2"/"<3" labels, the chevrons and the
  destination ring at the same place and size; the ring is cut by the ground at the same place; the arc rises from the
  archer and comes down onto the barbarian with its head over the target, over the hills (a1) and over the water (a2)
  as in stock; no dark red, nothing under the ground or floating. **3801's "a hill or coast under the arc": checked,
  as stock (a1 hills, a2 water).**
- Visual Checks (Tiny, round 3): path ACCEPTANCE `vc-20261114-213756-workstation-878d` (p1, pass),
  `vc-20261114-213919-workstation-e3ea` (p2, pass); arc DIAGNOSTIC `vc-20261114-214054-workstation-674c` (a1, pass),
  `vc-20261114-214148-workstation-86cd` (a2, pass); off shots `-213825-...-145a`, `-213958-...-1e59`, `-214120-...-8027`,
  `-214220-...-bc4a`; stock references `vc-20261114-2130..2134-*` (named in each entry). Rounds 1 (20:29-20:40) and 2
  (21:03-21:25, stock only) are kept in the feed as superseded: in round 1 the off shot's camera had moved (the game
  selected a created archer that was not asleep and panned 62-126 world units), in round 2 the woken Warrior was
  re-selected; the on pictures of round 1 are valid (`vc-20261114-203728-workstation-7a5c` is 3803's DIAGNOSTIC).
- Run facts: joined pictures run rc 0, guard checks 0, stop rc 0; LOUD lines: render.log, server.log, vpproxy.log 0, the
  composite's log 1 (the overlay tap's rate-limited "missed copy", D132's counted episode, the same class as 0167 and
  0192); EXE private 1,241.4 MB, largest free block 2,008.3 MB after the load (stock: 1,731.7 / 1,514.2 MB).

### 11.3 Done D: the parity pair on Tiny [L, c5h-c, 20:40-21:01]

30 AI turns each (turn 9 to 39), the same build (`2eb96de5`), the same save copy; MovementPath **on** (config.ini
`MovementPath = 0`, the product since 3803) against **off** (`= 1`, the D33 of before 3803), an instance-local edit inside
the prep's backup (`run_3804.py mp`). Before every AI turn a path preview: the melee unit nearest the capital selected
(Lua), move mode (Lua), the cursor onto a destination 3 plots east by the harness (30 of 30 turns in move mode in both
arms; diagnostic means, which is what parity needs). `eq_stats.py` after the load (before the first preview) and after the
last turn: **362 keys and 434 keys, identical, 0 differing** (`runs/parity-load.txt`, `parity-end.txt` on D:). Both arms
rc 0, `max turns 30`, stop rc 0, guard checks 0, 0 LOUD lines in render.log, server.log, vpproxy.log; the composite's
overlay tap logged its rate-limited "missed copy" line 3 times (on) and 2 times (off) (D132, counted). Turn wall time
median 14.8 s (on) and 14.6 s (off); EXE private after the load 1,239.7 / 1,242.1 MB, after the last turn 1,283.3 /
1,284.6 MB; server private after the load 778.4 / 794.4 MB (indications, COMMON 23).

### 11.4 Done C on the huge save [L, c5h-d (F:), joined 22:57-23:06, stock 23:07-23:16]

The same method and stimuli as 11.2 on `C5H-0179-HUGE_0350` (turn 350). Setup by the same Lua: 72 units put to sleep; the
path's unit is the Mamluk 17540 at (105, 26), alone on its plot, to (102, 26) (3 plots west; the first stock arm,
21:44-21:52, had picked an Archaeologist stacked in the capital, so its click selected another unit and the record names
a Lua fallback: that arm is kept as `stock-r1` and was retaken after the joined arm with the fixed choice); pair a1 =
Archer 17542 at (100, 25) on a barbarian Warrior at (98, 25) (desert, no hill or water between), pair a2 = Archer 17545
at (101, 24) on a Warrior at (99, 25) **over hills** (100, 24). Cameras: p1 LookAt (103, 26) 0 ticks, p2 the same 3
ticks in, a1 (99, 25), a2 (100, 24), 0 ticks. Every stimulus of both arms was player input (no fallback).

| Camera | Mask | Item px | Stock vs stock | Joined vs stock | Joined off vs stock off | Joined on vs joined off | Camera diff |
|---|---|---|---|---|---|---|---|
| p1 (path) | item | 452,831 | 9 / 62 / 233 (40.4 %) | 8 / 94 / 244 (39.7 %) | 14 / 109 / 242 (53.1 %) | 23 / 92 / 254 (68.6 %) | 0.004639 |
| p1 (path) | item_cut | 66,960 | 0 / 8 / 12 (0.0 %) | 6 / 90 / 226 (30.9 %) | 9 / 110 / 214 (44.1 %) | 31 / 120 / 251 (80.0 %) | 0.004639 |
| p1 (path) | item_strong | 21,773 | 0 / 5 / 10 (0.0 %) | 4 / 83 / 226 (26.3 %) | 7 / 114 / 214 (39.0 %) | 64 / 154 / 251 (88.4 %) | 0.004639 |
| p2 (path) | item | 1,948,294 | 0 / 25 / 255 (10.4 %) | 8 / 106 / 248 (40.9 %) | 8 / 110 / 248 (42.1 %) | 25 / 119 / 255 (80.5 %) | 0.000977 |
| p2 (path) | item_cut | 1,388,546 | 0 / 2 / 12 (0.0 %) | 7 / 103 / 248 (33.9 %) | 7 / 104 / 248 (34.1 %) | 24 / 117 / 255 (81.8 %) | 0.000977 |
| p2 (path) | item_strong | 398,603 | 0 / 2 / 12 (0.0 %) | 5 / 114 / 248 (39.8 %) | 5 / 116 / 248 (39.5 %) | 65 / 152 / 255 (88.3 %) | 0.000977 |
| a1 (arc) | item | 561,357 | 15 / 148 / 251 (52.6 %) | 32 / 152 / 251 (62.8 %) | 8 / 104 / 237 (39.0 %) | 28 / 115 / 254 (60.7 %) | 0.000488 |
| a1 (arc) | item_cut | 176,639 | 0 / 2 / 12 (0.0 %) | 4 / 60 / 204 (19.3 %) | 8 / 107 / 206 (29.2 %) | 77 / 119 / 237 (99.0 %) | 0.000488 |
| a1 (arc) | item_strong | 141,469 | 0 / 0 / 12 (0.0 %) | 3 / 51 / 183 (16.6 %) | 8 / 108 / 206 (28.4 %) | 82 / 124 / 237 (99.8 %) | 0.000488 |
| a2 (arc) | item | 519,710 | 5 / 57 / 227 (27.7 %) | 24 / 100 / 231 (69.3 %) | 18 / 109 / 235 (55.0 %) | 32 / 112 / 254 (83.7 %) | 0.000732 |
| a2 (arc) | item_cut | 212,694 | 1 / 7 / 12 (0.0 %) | 20 / 87 / 231 (61.5 %) | 17 / 111 / 229 (52.9 %) | 41 / 120 / 254 (96.4 %) | 0.000732 |
| a2 (arc) | item_strong | 101,617 | 0 / 5 / 10 (0.0 %) | 19 / 65 / 182 (57.3 %) | 35 / 115 / 200 (60.4 %) | 72 / 144 / 254 (99.9 %) | 0.000732 |

- **Both items drawn by the joined picture at all four huge cameras** (joined on vs off: 88-100 % of the strong pixels
  change), and **on the item's pixels the joined picture is no further from stock than the D33 ground with no item
  drawn** (strong masks, joined vs stock against joined off vs stock off, median / p95: p1 4 / 83 against 7 / 114, p2
  5 / 114 against 5 / 116, a1 3 / 51 against 8 / 108, a2 19 / 65 against 35 / 115). Stock's own ceiling on the cut
  masks 0-1 / 2-8 / 12; without the cut 0-15 / 25-148 / 227-255 (a1's figures: the barbarians' figures animate).
  The item's own contribution |(joined on - off) - (stock on - off)| on the strong masks: 4-29 / 76-118.
- **Classes of the remaining difference** (difference images `diff/<cam>__item_diff.png`): the ones named for Tiny
  (D33 ground without Forest and Decal under the half-transparent items, here also the farm fields' decals; unit
  figures; water; the movement and fire ranges and the selection; UI), and on this save also **the pillage smoke
  columns** (Particle is off under D33, so stock's smoke over Thebes is missing in the joined picture) and **the plot
  tooltip's growth** (stock's second on shot shows one more tooltip line, "Repair an Improvement", which reaches past the
  masked box). No other class seen.
- **3801's open "the huge save under A'' with an arc": verified [L]**: the EXE draws the red arc over the joined huge
  picture at a1 and a2 (over hills), with the ranged-attack preview panel and the target marker, as stock does.
- By eye: the Mamluk's chevrons, the "<1" discs and the destination ring at (102, 26) next to Thebes as in stock, the
  ring at the same size and place; the white fog mask on (default) and the path's plots all visible (no fog over them).
- Visual Checks (huge): path ACCEPTANCE `vc-20261114-230126-workstation-3b98` (p1, pass), `-230322-workstation-6589`
  (p2, pass); arc DIAGNOSTIC `-230417-workstation-1bd2` (a1, pass), `-230554-workstation-868c` (a2, pass); off shots
  `-230244-...-f2c2`, `-230348-...-41cc`, `-230451-...-bc5b`, `-230622-...-2f5c`; stock references 23:07-23:16 (named in
  each entry; the first stock arm's 21:44-21:52 entries are superseded).
- Run facts: joined pictures run rc 0, guards 0, stop rc 0, 0 LOUD lines in render.log, server.log, vpproxy.log and none
  in the composite's log during the run; EXE private 1,393.1 MB, largest free 1,920.1 MB after the load (stock arm:
  2,002.7 / 1,230.4 MB). c5h-d restored by hash, `provision.py verify` (full) ok, claim released 23:16.

### 11.5 Done E: 30 AI turns on the huge save, MovementPath on against off [L, c5h-d (F:), ON 21:52-22:20, OFF 22:20-22:56]

The same instance, prep, save copy and build (`a25892d5`); ON = the product (config.ini `MovementPath = 0`), OFF =
today's D33 before 3803 (`= 1`), the arc drawn by the EXE in both (Arrows on under A''). Before every AI turn: a path
preview (the melee unit nearest the capital, Lua select and move mode, the cursor onto a destination 3 plots east by the
harness) and an **aimed ranged attack** (a real ranged unit of the player, at (84, 31), on an enemy at (84, 33), found by
3702's Lua pair search every turn; Lua select and range mode, the cursor onto the target; no unit was created): 30 of 30
turns in move mode and 30 of 30 in range mode in both arms. Means diagnostic (Lua select and mode), as 0167's done E
used Lua highlights; figures of time and memory are indications (COMMON 23: the workstation, other teams' load present).

| | ON (MovementPath on) | OFF (today's D33) | ON - OFF |
|---|---|---|---|
| turns played, end, rc, stop rc, guard checks (start, in game, end) | 30 (350 -> 379), max turns, 0, 0, 0/0/0 | 30 (350 -> 379), max turns, 0, 0, 0/0/0 | |
| EXE crashes, server crashes | **0, 0** (no crash box, dump or `Game Crash` mark) | 0, 0 | |
| LOUD lines: render.log, server.log, vpproxy.log | **0, 0, 0** | 0, 0, 0 | |
| the composite's overlay tap, rate-limited "missed copy" (D132, counted) | 4 lines; missed 3,001+ of 107,976 copies, faults 0 | 11 lines; missed 10,001+ of 103,547, faults 0 | |
| gray frames after the bake-ahead (render.log, 0139) | **0** (20 before it) | 0 (23 before it) | |
| lockstep over the turns: exact / older / none / nopub / late | 121,053 / 2,278 / 0 / 0 / 0 | 111,386 / 2,010 / 0 / 0 / 0 | |
| EXE private / largest free block after the load (MB) | 1,399.4 / 1,903.4 | 1,390.4 / 1,920.0 | **+9.0 / -16.6** |
| the same after the last turn | 1,567.5 / 1,689.9 | 1,557.4 / 1,706.3 | **+10.1 / -16.3** |
| server private after the load / after the last turn (MB) | 2,130.9 / 2,233.1 | 2,126.8 / 2,226.1 | +4.1 / +7.0 |
| server video after the load (render.log) | 1,535.3 | 1,707.2 | (read before the bake-ahead ended; not comparable) |
| turn wall time median / mean / max (s) | 47.6 / 48.5 / 58.8 | 61.3 / 63.0 / 101.2 | (OFF ran while the machine was busier: indications only) |

- **Done E: met** on its own terms: 0 EXE and 0 server crashes, 0 LOUD lines other than the overlay tap's counted D132
  episodes (MC #891's amended text), 0 gray frames after the bake-ahead. **Memory: MovementPath on costs about +9 to
  +10 MB of EXE private bytes and about 16.5 MB of the largest free block** (one run each; noise about 20 MB on this save),
  under MC #1011's +20 MB line; 3801 measured +5-6 MB on Tiny, the 2026-11-09 probe +5 MB at boot (row 23). The server
  is unchanged within noise (+4 to +7 MB; it draws neither item). Turn time: no claim (the OFF arm's turns were slower
  while other work ran on the workstation; COMMON 23).
- Runs: `runs/huge-e-on/out`, `runs/huge-e-off/out` (run.json per turn, the logs, stimuli.jsonl; on D:), summary
  `shell/evidence/0179/3804/turns_summary.py`.

### 11.6 3801's "not verified" list, after 3804

| Item | State after 3804 |
|---|---|
| The huge save under A'' with an arc (3706's huge shot showed none) | **verified [L]**: the EXE draws the arc over the joined huge picture at a1 and a2 (11.4), and was aimed in 30 of 30 turns of done E's ON arm without a crash |
| An arc over a hill or a coast | **verified [L]**: Tiny a1 over hills (49, 15), Tiny a2 over water (51, 13), huge a2 over hills (100, 24); the arc rises and falls as stock's at the same camera |
| An arc or path across a load | **not verified**: no load was made with an arc or path up. What settles it: a reload of the save in the same EXE process (3308's `--reloads 1`) right after an aim and a preview, then a picture; 3801's [I] that a load ends both modes stands |
| Whether the white fog mask covers the path models | **not verified**: every path destination in these runs was a visible plot (Tiny is fully revealed; on the huge save the Mamluk's path stays inside the visible area). `WhiteFogMask` was at its default (on) in every joined run and the path models were drawn unchanged there. What settles it: a preview onto a revealed but not visible plot on the huge save, stock against joined, WhiteFogMask on and off |
| The 3702 cameras | not used: this task's own cameras (3801's on Tiny, the same procedure on huge) |
| RenderDoc of the joined arm (depth judged by eye) | not done; the depth judgement stays by eye (the ring cut by the ground at the same place as stock, the arc's foot hidden by the archer's figures as in stock) |
| The parity of MovementPath on | **verified [L]**: 11.3 (identical, 0 differing) |
| `SelectedAni`'s animation | not checked frame by frame; the destination ring looked the same in stock's two on shots taken 5 s apart (inside the motion cut) |

### 11.7 Result

- **Done C**: met for both items on both maps at two cameras each, against stock at the same camera with stock's
  shot-to-shot figures beside them; on the item's own pixels the joined picture is no further from stock than the D33
  ground under them; every remaining class named (11.2, 11.4). Path pictures ACCEPTANCE (player input, popups closed,
  the build and options as shipped from the branch), arc pictures DIAGNOSTIC (#1030).
- **Done D**: met (11.3).
- **Done E**: met (11.5); MovementPath on costs about +10 MB EXE private and about -16.5 MB of the largest free block on
  the huge save (one run each, indications), under #1011's +20 MB.
- 3803's product change (MovementPath out of the key list) and 3805's config routine: `research/02-exe-internals/disable-systems-and-masks.md`
  5, "MovementPath on in the product".

## 12. The arc built by c5r, offline (ticket 0199 part 1, 2026-11-16)

Written by the implementer of 0199 part 1 (account B, branch `task-0199-ranged-arc`; increment 3 groundwork, D143). Part
2 (the server's wiring, clearing the Arrows view bit, the live comparison) waits for epic 0431's merge. Code:
`render/include/c5r/arc/arc.h`, `render/src/arc/` (`mesh.cpp`: settings, mesh, the arrow record, the end points;
`draw.cpp`: the two draws), tests `render/tests/arc/` (`c5r_arc_arc`, `c5r_arc_picture`, reference maker
`mkref_arc.py`), pictures `render/evidence/0199/picture_0199.py`. Written from sections 3 and 8 above and
`research/02-exe-internals/arrows-and-movement-path.md` 1.1-1.7 (D83: behaviour only). Marks as in the rest of this file;
[C] here = read from 0179's capture records.

### 12.1 Inputs and the reference

| Item | Value |
|---|---|
| Capture data | 3801's `frame2846.rdc` (archived `D:/c5-64bit-work/0179/3801/captures/`), exported 2026-11-16 19:27 with `python -I tools/harness/cli.py rdc-export CAPTURE C:/c5-64bit-work/0199/export --xml-dir C:/c5-64bit-work/0199/xml --select draws:475-476` (8.8 s); the VB, IB and constant buffer have the SHA-256 prefixes 3801 printed (`8c3da238`, `82b7106e`, `907142ec`) |
| Map data | 3804's Tiny stock arm (`D:/c5-64bit-work/0179/3804/captures/tiny/stock/`: `grid.json`, `heights/heightmap_u16le_3584x2560.raw`, the same save copy `C5H-0179-TINY_0009V` as the capture) |
| Pictures | 3804's Tiny stock cameras a1 (pair (50, 14) -> (49, 16)) and a2 ((52, 14) -> (52, 12), across water): on twice, off with the camera put back; stock's view-projection of each on shot = `views[0] x proj` of `<cam>_camera_on.json` |
| Reference set | `F:/c5-64bit-work/0199/ref` (made by `python -I render/tests/arc/mkref_arc.py C:/c5-64bit-work/0199/export F:/c5-64bit-work/0199/ref --stock3804 D:/c5-64bit-work/0179/3804/captures/tiny/stock`; game data, never committed; `manifest.txt` holds the SHA-256 of every file). Tests read it from `C5R_ARC_REF` (default that folder) |
| Builds | clang-cl Release `render/build.py --test -R c5r_arc` (tree `C:/c5-64bit-work/render-build/1bfe6db4c8`), C5R_EXE = the Ghidra project's copy of the DX11 EXE (build 4390913), C5R_GAME_DIR = `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`; RTX 4080 |

**Not usable:** 3801's own off shot `s_arrow_off.png` is at another camera (the game panned when the mode ended,
section 7), and 3801's capture camera (frame2846's constant buffer) is zoomed out against 3804's a1 (the view's z
translation -707.6 against -475.8; x and y equal), so the capture's camera has no stock off shot. The pictures therefore
use 3804's cameras; the capture is the reference for the mesh, states and constants.

**A replay of the capture for the colour and depth before and after the arc's draws was tried and dropped [L,
negative, 2026-11-16 19:37-19:47]:** qrenderdoc 1.47 on a hidden desktop (a copy of 3601's `rd_hidden.py` route with a
private settings folder) logged "Importing debugpy" and "Monkey-patched debugpy" and then nothing until the 600 s
timeout; it read `%USERPROFILE%/AppData/Roaming/qrenderdoc`, not the private folder. This is the hold already written in
`renderdoc-capture.md` 8 and `water-system.md` 6.2 (the owner's `Python_DebugEnabled`; qrenderdoc ignores APPDATA): read
those before trying a replay.

### 12.2 What the description gave, checked against the capture [C, L 2026-11-16]

- **The mesh, vertex for vertex.** From the two plots through the map (12.3) and the install's ArrowSettings.xml,
  `buildMesh` at progress 0.9999 gives **468 vertices and 1260 indices; the index list equals the capture's (all 1260);
  every COLOR0 / COLOR1 equals; positions differ by at most 0.000244 world units (v144; two float32 steps at |x| about
  1420), mean 1.9e-5; 166 of 468 positions and 246 of 468 normals are bit-equal; normals differ by at most 1.7e-5**.
  25 rings (s = 0 .. 0.96 in steps of 0.04, early stop at 0.96) and the 68-vertex head, as section 3. The remaining
  differences are float rounding of the operation order [I: the EXE's own order of the products was not reproduced].
- **The head's slanted normals (M1, M2; open in arrows-and-movement-path.md 1.6) [C, fitted]:** M1 =
  normalise(N x (L T - 2w S)) and M2 = normalise((L T + 2w S) x N), the normals of the plate's two slanted faces (the
  faces through B(+2), A and B(-2), A, each spanned with N; L = 2 sqrt(3) w). They match v420 and v426 to 1e-5 (a
  Python prototype first, then c5r). In the frame both have z 0.4401 and a T component of -0.486: the 1.6 decode that
  gave "no T component" had lost it.
- **Block 2's back plate** (vertices 18-25) has normal -T as 1.6 says; block 3 is block 2's layout at the outline's
  points with every triangle's last two indices swapped; the collar's indices are (a, a + 8, b), (b, a + 8, b + 8).
  All as 1.6, now checked index for index.
- **The two passes' states equal the capture's**, field by field (19 fields each: blend enable and factors, write mask
  alpha then all, depth on / write all / LESS_EQUAL, no stencil, solid, cull front, front counter-clockwise, depth clip
  on, no scissor, no multisample, triangle list).
- **The constant block:** rows 0-3 (the frame's View x Projection), 4.x = alpha 0.5 and 5.x = ambient 0.7 are bit-equal
  to the capture's when c5r packs the frame's matrix with the install's settings; 4.y and 4.z (non-float values in
  stock's block) are written 0.
- **The shaders:** the blobs at 0x0619B000 (VS) and 0x0619B7F8 (PS) of the player's EXE have the SHA-256 of the blobs
  bound in the capture (`9c5bf893...`, `2525c4ee...`); c5r reads them at run time with `c5r::shaders::ShaderSection`.
- **The install's ArrowSettings.xml** parses to the values of arrows-and-movement-path.md 1.5 (origin
  `Assets/UI/Overlays/ArrowSettings.xml`, the loose file).

### 12.3 The end points from the plots [C, L 2026-11-16]

The spawn handler's inputs come out of the map exactly: with 3804's grid (`M_h2w`, sx 1, sy 0.866, ox 1792, oy
1108.48) the hex coordinate of plot (x, y) is (x - (y >> 1), y), sample = hex x M_h2w, world = (sample.x sx - ox,
sample.y sy - oy). Plot (50, 14) gives (1439.94043, -221.63531), the capture's ring-0 centre to 1e-5; plot (49, 16) gives
(1375.94336, -110.78571), the end fitted from the capture's ring centres to 2e-5. **The start's z is HeightAtGrid of the
sample under the centre with the coordinates truncated:** sample (3231.94, 1024.07) -> (3231, 1024) gives 17.95294, the
capture's 17.95294; the rounded sample (3232, 1024) would give 17.57. **The end's z is the absolute 20** (target_height):
the fit's own z is 19.99999. This settles 1.2's "[I: the triple is the plot's hex coordinate]" and the end height.
`c5r::arc::spawnPoints` does this; the wrap choice (1.2 steps 2-3, `chooseWrap`) is implemented from the description
with the frustum planes from the view-projection [I: the EXE's own planes at 0xDA1510 were not compared] and is tested
only synthetically (no capture has an arc across the wrap seam).

### 12.4 The picture against stock (DIAGNOSTIC, D112) [L 2026-11-16 19:5x]

`c5r_arc_picture` draws the arc as the server will make it (spawnPoints, `Arrow` grown to the end, `Renderer` with the
EXE's ArrowShader) at 2554 x 1411 with a cleared depth, and `picture_0199.py` compares. Largest RGB channel difference,
median / p95 / max (share above 12):

| Comparison | a1 (50,14 -> 49,16) | a2 (52,14 -> 52,12, over water) |
|---|---|---|
| frame2846's camera: c5r's arc against stock's captured mesh, both through c5r's draws, every pixel | 0 / 0 / 0, 0 pixels differ of 33,222 covered (same pixels covered) | - |
| silhouette: stock's arc (pixels within 8 px of c5r's that stock's on shot changes by > 30) against c5r's covered pixels, motion and UI removed | IoU 0.961 (both 71,105; only stock 2,665; only c5r 230) | IoU 0.969 (both 64,156; only stock 703; only c5r 1,319) |
| c5r over stock's off shot vs stock on, arc pixels minus motion (`arc_cut`) | 19 / 53 / 127 (71.1 %), 71,335 px | 11 / 61 / 233 (46.5 %), 65,475 px |
| stock on vs stock on (5 s apart), same pixels | 0 / 10 / 12 (0 %) | 0 / 6 / 12 (0 %) |
| stock off vs stock on, same pixels (no arc drawn) | 105 / 143 / 227 (99.7 %) | 78 / 133 / 220 (98.6 %) |
| the ground beside the arc (4-16 px outside), stock off vs on | 9 / 53 / 232 (44 %) | 9 / 45 / 96 (41 %) |

- **The arc's shape, place and size are stock's**: the pixel-identical result at frame2846's camera (c5r's mesh against
  stock's mesh through the same pipeline) and the silhouettes at a1 and a2. The silhouette mismatches are the UI icons
  drawn over stock's arc (the city strike and unit icons: "only c5r") and the range-mode overlays next to the arc ("only
  stock").
- **The colour difference is the ground, not the arc** [I, from the pixels]: c5r's arc is drawn at alpha 0.5 over stock's
  off shot, whose ground lacks the range mode's overlays (the red fire-range tint and the yellow plot highlight, both
  in stock's on shot under the arc); for example at a1 (1086, 339) stock on is (158, 23, 10) over an off ground of (43,
  52, 16): no 0.5 blend of an arc colour can give that from this ground (green 23 < 52 / 2), so the on shot's ground under
  the arc is a different, redder one. Second class: stock's unit figures in front of the arc's foot and head (the depth
  is cleared in this picture, so c5r's arc is drawn over them). The part-2 comparison inside the server's frame (scene
  depth, the overlays drawn) is the measure of colour; this picture is not.
- Visual Checks: `vc-20261116-195651-workstation-f976` (a1), `vc-20261116-195653-workstation-7f93` (a2), DIAGNOSTIC,
  reference the stock on shot, comparison the side-by-side (stock on | c5r over off | difference x 4).

### 12.5 For part 2

- The input: `spawnPoints(grid, x0, y0, x1, y1, planes, start, endXY)` from the four integers of the wrapper of
  `InGame`'s `Events.SpawnArrowEvent` (section 6.1, P9), `Arrow::spawn`, `Arrow::removeAll` on `RemoveAllArrowsEvent`,
  `Arrow::update(dt)` once per frame, `Renderer::draw` after the world's models with the scene's colour and depth.
- The Arrows view bit must be cleared in the same change, or the player sees two arcs (section 8).
- Not done here: an arc across the wrap seam against stock, an arc during its 0.5 s growth against stock (phase), the
  live comparison inside the server's frame.

## 13. The arc drawn by the server (ticket 0199 part 2): design, 2026-11-17 21:20 EST

Written before the code (the ticket's acceptance), by the part-2 implementer (account B, branch
`task-0199-ranged-arc-p2`; `task-0199-ranged-arc` is part 1's, still checked out elsewhere). Marks as above; [D] =
design, not yet built or measured.

### 13.1 The chain [D]

| Step | Where | What |
|---|---|---|
| 1. The end points | proxy, `proxy_ui_bridge.cpp` | A **wrapper of the `InGame` context's `Events.SpawnArrowEvent`** (P9, section 6.1, done in C): the field is replaced by a table whose metatable's `__call` is a C closure (generation, the original) and whose `__index` is the original. The closure posts one **kind-1 record with a[0..3] = x0, y0, x1, y1** (the four integers `Bombardment.lua` passes), then calls the original with the same arguments (the EXE's own handler and every Lua listener still run: nothing the game sees changes). A listener of `RemoveAllArrowsEvent` posts kind 2. A re-install unwraps an older generation's wrapper (a marker key in the wrapper table), as UnitMoving's replacement does, so wrappers never nest. With the wrapper installed, the whole bridge's generic kind-1 listener (which gets 0 arguments, section 5) is not added, so every kind-1 record of such a session carries the end points. Switch: `UiBridge::ConfigureArrows(true)` ("arrows only" install: the wrapper, the remove listener and the drain; or with the whole bridge). No new record kind, so `srv_remote.h`'s decode is unchanged. |
| 2. The transport | proxy -> server | the ui channel's `kRt_UiRecord` as today; the server's `UiRecord` pushes `SrvFeed::kUiMirror` (flags = kind, a[0..3] kept) |
| 3. The read | server, render thread | `Scene::arcFeed(batch)` in the host's loop after `forestFeed` (and in `sceneshot`'s replay loop at the same place): kind 1 = a pending spawn (the last one of the batch wins, as the EXE's one record), kind 2 = remove; only with `render_arc=1` |
| 4. The record | `Scene::draw` | the pending spawn becomes `c5r::arc::spawnPoints(grid, x0, y0, x1, y1, planes of this frame's view 0 x projection)` then `Arrow::spawn`; `Arrow::update(dt)` with dt = the frame clock's difference (`SceneFrame::nowMs`, the recorded clock in the player), clamped to 0.25 s; while the arc grows the scene stays dirty, so frames are drawn |
| 5. The draw | `Scene::draw` | `c5r::arc::Renderer::draw` into the scene's colour and depth **after the water and the ice and before the forest's stage and the units** (stock: pass B after Terrain, Waves 0x200000 and the 0x20 system, before Forest 0x80, City and Unit; `arrows-and-movement-path.md` 1.7), view-projection = view 0 x projection (one arc, the record's wrap choice, as stock) |
| 6. The EXE's arc off | proxy, `proxy_shell.cpp` | the view rule's clear bits get **Arrows 0x400000** when this boot writes the arc's line into server.cfg; the system stays started (config.ini Arrows on, as A''), so the crash key of `NEVER_RESTORE` (bit set while the system is off) cannot occur; `NEVER_RESTORE` already keeps the bit from being set back |

### 13.2 The option [D]

`VP64Options.ini [Renderer] ServerArc` = 0 or 1, read by the proxy. With 1, and only when this boot writes the server's
render lines (`ServerRenderer = 1`, the composite accepted or the test switch, no `server_config=`), server.cfg gets
`render_arc=1`, the bridge is asked for its arrow install, and the Arrows view bit is cleared; otherwise nothing is
written and one line says why (the pattern of `ServerUnits`, 4759). The server's key `render_arc` (default 0) is parsed
in `srv_render_host.cpp`. **The default is 0 until every live check of the acceptance passes** (the ticket's rule); the
result and the default are in 13.4 when measured.

### 13.3 The bundle (rule A) [D]

The arc's inputs are all in members a bundle already has: the feed's `kUiMirror` kind-1 and kind-2 events (`feed.bin`
holds every drained event), the clock, the camera, the height field; the player reads `ArrowSettings.xml` and the
EXE's ArrowShader from the install it is given, as it does the water's art. What is new is a key: `render_config`
`render_arc` (written by the host's config map, read by `sceneshot`, which then calls `arcFeed` in its replay loop as
the host does). A bundle without the key plays with the arc off (what such a recording's server drew). With the
default 0 the library is not recorded again; a default of 1 would raise the format to 9 and record the library again
(rule A, section 25 of `offline-scene-harness.md`).

### 13.4 Files [D]

Inside the touches: `proxy/src/proxy_ui_bridge.{h,cpp}`, `proxy_ui_bridge_rt.{h,cpp}`, `proxy_shell.cpp`,
`proxy/tests/ui_bridge/`, `proxy/tools/test_proxy_rt.py` (granted #4767), `server/src/render/srv_render_scene.{h,cpp}`,
`srv_render_host.cpp`, `server/tools/sceneshot/`, `server/evidence/0199/`, this file and
`offline-scene-harness.md`. Outside: the option's three files `options/include/vp64_options.h`,
`options/src/vp64_options.cpp`, `options/tests/test_options.cpp` (asked, #4766; held by epic 0504, kept on a commit of
their own; granted #4775). Not needed after all: `srv_remote.h` (no new kind). *As built (2026-11-17 22:4x): also
`docs/OPTIONS.md` (the option's player text, which `test_options` requires for every key), committed with the options
files, and `server/tests/render/scenebundle/sceneshot_test.cpp` (`CaseArc`, inside the touches' scenebundle folder).*

### 13.5 As built (2026-11-17 21:2x-21:4x EST) [C code, tests]

- **The wrapper (`proxy_ui_bridge.cpp`, `WrapArrowEvent`, `ArrowWrapperCall`):** as 13.1. Two details found while writing
  the tests: (1) the original's methods are handed out through a forwarding `__index` (`ArrowIndex` / `ArrowMethod`),
  because a colon call `Events.SpawnArrowEvent:Count()` would otherwise pass the wrapper as `self`, which the EXE's own
  `Count` refuses ("Not a valid instance" in the test engine) [C test]; a dot call (`Add(f)`, as the UI files make it)
  passes its arguments unchanged. (2) An install whose InGame context has no `SpawnArrowEvent` is an install failure,
  logged `NOT wrapped`, and is not retried in that environment (no 250 ms install loop) [C test].
  A call without four whole numbers (two arguments, a fraction, a string, five arguments, a number beyond int32) posts
  nothing and is counted (`badArgs`); the original is called in every case; an error of the original propagates through
  the wrapper as it would without it. A raise on another thread is counted (`wrongThread`) and still calls the original.
- **Tests:** `proxy/tests/ui_bridge/ui_bridge_test.cpp` 120/120 checks under the proxy's toolchain and under MSVC ASan
  (the canary caught), of them the arrows-only, whole-bridge-with-arrows and missing-event blocks of 0199;
  `proxy/tools/test_proxy_rt.py --only-units --asan` 140/140 (the 68 `arc` checks: every ServerRenderer x ServerArc x
  server_config= case in the accepted / refused / marker layouts, the build without the glue, and the same under ASan);
  `options` 221/221 on x86, x64 and ASan. Logs `C:/c5-64bit-work/0199/{uib,rt,options}.log`.
- **Build:** `server/evidence/0199/build_0199.sh` (0516's script on this ticket's inputs: the fork at the pin 4f4daac70 by
  git archive, a copy of 0516's release `build_all` deps of that pin, the repository by git archive of HEAD): server.exe
  `70cf0b51`, proxy `c4094611`, composite `02b67a7b` (repository 9df06503), in the layout `C:/c5-64bit-work/0199/layout`
  with the pin layout `x64-4f4daac7-pf8e9f42`'s x64 DLL `2D737BD5`.
- **First live check [L, c5h-c, Tiny `C5H-0199-TINY_0009V` = copy of `C5H-3600-TINY_0009V` (27420A41), 21:37-21:44]:**
  `vpproxy.log`: `options: [Renderer] ServerArc = 1 (from the file)`, the shell's `server.cfg gets render_arc=1` line, the
  bridge's `gen 1: arrows: Events.SpawnArrowEvent wrapped, RemoveAllArrowsEvent listener added (arrows only)` at 45.7 s,
  the view mask `config 0xffec3f67 ... clear 0x00600001: view ... -> 0xff8c3f66` (Arrows 0x400000 cleared, Waves
  0x200000 and Terrain 0x1 as before). `server.log`: `ui records: #1 kind 1 ... a 50 14 49 16`, `#2 kind 2`.
  `render.log`: `arc: ArrowSettings.xml from Assets/UI/Overlays/ArrowSettings.xml (target_height 20.00, anim_speed 0.50 s,
  alpha 0.50, ambient 0.70)`, `render_arc=1 ... (29.3 ms)`, `arc: spawn 1: plots (50, 14) -> (49, 16), start (1439.94,
  -221.64, 17.95)` (the capture's ring-0 centre and height, 12.3), spawn 2 (52, 14) -> (52, 12). By eye on the pictures:
  one red arc at a1 (over hills), a2 (over water) and a3 (archer at (1, 14), target at (55, 15): **across the wrap
  seam**, drawn from the archer to the target's wrap copy beside it).

### 13.6 The server's arc against stock at 0179's cameras (DIAGNOSTIC, D112) [L, c5h-c, stock 21:44-21:50, joined 21:51-21:58]

Arms: **stock** = the stock EXE on the current 32-bit pin `i0ship-39f17f75-ia` (SHA-256 7DE39F71...; `darkred --arm stock`,
no proxy, no server; the EXE draws the arc); **joined** = the product path as 3804 ran it (3308's prep, D33 in the A''
form, the shipped composite ini, ServerRenderer = 1, WhiteFogMask default) with `ServerArc = 1` from this branch's layout
(13.5). Same instance, save copy (`C5H-0199-TINY_0009V`), Lua-made pairs (the same unit ids and plots in both arms:
a1 Archer 1039 (50, 14) on Warrior 1040 (49, 16) over hills (49, 15); a2 Archer 1042 (52, 14) on (52, 12) over water
(51, 13); a3 Archer 1044 (1, 14) on Warrior 1045 at (55, 15) **across the wrap seam** (56 x 36 map)), cameras (LookAt the
midpoint, a3 the archer's column; 0 wheel ticks) and stimuli (a left click on the archer, key B, the cursor onto the
target: all player input, no Lua fallback in either arm). Method: 3804's (`server/evidence/0199/compare_0199.py`, 11.2):
item = stock on differs from stock off by > 12 on ground the team sees, minus UI boxes; item_cut minus the pixels that
move by themselves between stock's two on shots (dilated 16 px); item_strong = item_cut where the arc changes stock by
> 48. Largest channel difference, median / p95 / max (share > 12):

| Camera | Mask | Item px | Stock vs stock | Joined vs stock | Joined off vs stock off (the floor) | Joined on vs joined off | Item delta diff | Camera diff |
|---|---|---|---|---|---|---|---|---|
| a1 (hills) | item_cut | 317,634 | 0 / 6 / 12 (0.0 %) | 10 / 59 / 215 (45.0 %) | 11 / 60 / 202 (47.2 %) | 33 / 110 / 253 (92.8 %) | 6 / 21 / 269 (16.8 %) | 0.00058 |
| a1 | item_strong | 86,740 | 0 / 6 / 11 (0.0 %) | 5 / 53 / 214 (33.8 %) | 7 / 56 / 192 (39.5 %) | 70 / 137 / 253 (99.6 %) | 7 / 36 / 269 (26.9 %) | 0.00058 |
| a2 (water) | item_cut | 364,048 | 0 / 4 / 12 (0.0 %) | 4 / 61 / 234 (35.2 %) | 6 / 63 / 226 (38.2 %) | 22 / 82 / 240 (80.5 %) | 6 / 33 / 331 (31.4 %) | 0.001343 |
| a2 | item_strong | 69,668 | 0 / 4 / 12 (0.0 %) | 3 / 60 / 219 (30.4 %) | 6 / 66 / 224 (36.7 %) | 67 / 135 / 240 (96.2 %) | 6 / 60 / 331 (35.0 %) | 0.001343 |
| a3 (seam) | item_cut | 47,189 | 0 / 2 / 11 (0.0 %) | 7 / 69 / 169 (43.0 %) | 16 / 84 / 186 (56.2 %) | 50 / 119 / 179 (91.5 %) | 8 / 53 / 303 (40.5 %) | 0.001709 |
| a3 | item_strong | 23,958 | 0 / 2 / 10 (0.0 %) | 5 / 67 / 151 (39.9 %) | 17 / 87 / 186 (56.5 %) | 77 / 133 / 179 (99.2 %) | 13 / 64 / 303 (50.2 %) | 0.001709 |

(The "item" rows, without the motion cut, are in `C:/c5-64bit-work/0199/cmp/joined.md`: stock's own ceiling there is
3-15 / 39-98 / 217-245.)

- **The server draws the arc at all three cameras** (joined on vs off: 96-100 % of the strong pixels change) and **on the
  arc's pixels the joined picture is no further from stock than the D33 ground with no arc drawn** (median / p95 at or
  below the floor at every camera and mask), the criterion 3804 accepted the EXE's arc on (11.2). The same class of
  remaining difference as there: the arc is half-transparent (alpha 0.5) over a ground that differs (D33: no Decal, and
  the server's own trees and units against the EXE's), and unit figures that animate differently across launches.
- **By eye** (side-by-side crops `C:/c5-64bit-work/0199/cmp/side_a1.png`, `side_a3.png`): the same red arc and white rim,
  rising from among the archer's figures (which hide its foot as in stock: the arc is drawn before the units, as stock's
  pass B draws it before the Unit system) and coming down with its head over the target; across the seam (a3) the arc
  reaches the target's wrap copy exactly where stock's does. One arc in every picture: the EXE's is not drawn (13.7).
- Difference images: `F:/c5-64bit-work/0199/captures/tiny/diff/<cam>__joined__item_diff.png` (red = |joined - stock| x
  4, green = the item mask). Pictures and camera blocks: `F:/c5-64bit-work/0199/captures/tiny/{stock,joined}` (game data,
  never committed, D83).
- Visual Checks: the joined shots of the run (each with its stock reference, `--ref-how same`) and the comparison entries
  `vc-20261117-220005-workstation-b843` (a1), `-220006-...-fdb2` (a2), `-220007-...-f27f` (a3), all DIAGNOSTIC.

### 13.7 The EXE's arc is off under ServerArc = 1 (TEST arm x) [L, c5h-c, 22:3x-22:40]

The ON arm's pictures show one arc; whether it is the server's alone is settled by a TEST arm: the same prep with
`ServerArc = 1` and the TEST ONLY `vpproxy.ini server_cfg_extra=` file `render_arc=0` (appended after the proxy's
`render_arc=1`; the server's last value wins: `render.log` `render_arc=0: no ranged-attack arc`). The proxy still cleared
the Arrows bit (`vpproxy.log`: `Arrows (0x400000) cleared`, view `0xffffffff -> 0xff8c3f66`).

- **No arc at a1, a2 or a3** (same stimuli, all player input, range mode on: the fire-range tint, the target marker and
  the preview panel are there): side by side with stock `F:/c5-64bit-work/0199/captures/tiny/diff/side_x_a1.png`;
  Visual Checks `vc-20261117-224056-workstation-e68d`. So the cleared bit stops the EXE's arc, and the ON arm's arc is
  the server's.
- The figures show the missing arc: on the strong mask the x arm is far from stock (p95 117 / 89 / 111 at a1 / a2 / a3)
  where the ON arm was at or below the ground floor (53 / 60 / 67; 13.6), with the floors at 63-94 in both runs
  (`C:/c5-64bit-work/0199/cmp/x.md`).

### 13.8 The parity pair and the 30 turns with ranged attacks [L, c5h-c, ON 21:58-22:15, OFF 22:16-22:3x]

The same instance, prep, save copy and build as 13.6; ON = `ServerArc = 1`, OFF = `ServerArc = 0` (the prep's
`VP64Options.ini`, `run_0199.py opt`), 30 AI turns each (turn 9 to 38). Before every turn a path preview and an **aimed
ranged attack** (3804's turn stimuli: Lua select and range mode, the cursor onto the target by the harness; a pair found by
Lua or made once): 30 of 30 turns in range mode in both arms. `server/evidence/0199/parity_0199.py`:

| | ON (ServerArc = 1) | OFF (ServerArc = 0) |
|---|---|---|
| `eq_stats` after the load / after the last turn | 362 / 438 keys | 362 / 438 keys, **identical: 0 differing in both** |
| turns, end, rc, crash, dialog box | 30 (9 -> 38), max turns, 0, none, none | 30, max turns, 0, none, none |
| LOUD lines: render.log, server.log, vpproxy.log, the composite's log | 0, 0, 0, 0 | 0, 0, 0, 0 |
| the bridge at the end | arrows: spawns 30, removes 122, bad arguments 0, stale wrapper calls 0, wraps 1; post failures 0 | no arrows install (records arrow 0) |
| EXE private, first / last turn (MB) | 1,336.0 / 1,371.2 | 1,336.7 / 1,375.4 |
| server private, first / last turn (MB) | 910.8 / 939.2 | 898.7 / 927.2 |
| turn wall time, median (s) | 26.5 | 27.8 (indications only: the workstation, COMMON 23) |

- **Parity: met** (the option changes nothing the game sees: the wrapper calls the original with the same arguments, and
  a cleared view bit only stops the EXE's own arc render).
- **30 turns with ranged attacks shown: met**, no crash, no dialog box, no D118 report (no failure was raised: the
  arc's failures are LOUD lines, none logged). The server is about 12 MB larger with the option on at both ends of the run
  (one run each; the arc itself holds a 160 KB vertex buffer, a 12 KB index buffer and the two programs [C code]; the
  rest is within run-to-run noise [I]).

### 13.9 The default, and what is not verified (2026-11-17 22:4x)

**`[Renderer] ServerArc` stays 0 by default.** Every live check of the ticket passed (13.6-13.8), but two parts of "every
check" for a draw the server makes by default are not done in this change: rule A's path for a default-on draw (format
9, the library recorded again with the option on and swapped by a script, `test_unitfeed`; the gap note is
`offline-scene-harness.md` 28) and D118's failure route for the arc (a renderer that cannot start or a draw that fails
is a LOUD line and the frame without the arc, while the EXE's arc is already off; with the option at 0 nothing
changes for the player). Flipping the default is the follow-up: those two, then the option's default, `OPTIONS.md` and
the option's test.

Not verified:
- **The growth phase against stock** (the 0.5 s in which the arc grows from the archer): the server's growth runs on
  its own frame clock (`SceneFrame::nowMs`, dt clamped to 0.25 s), the EXE's on its frame time; no frame-by-frame
  comparison was made (PrintWindow pictures cannot be timed to a frame). By eye in the live runs the arc appears at
  once at the next pictures (taken 3 s after the cursor moved).
- **An arc across a load** (0179's open item): `kLoadBegin` hides the server's arc and drops a pending spawn [C code];
  not run live.
- **The huge save**, a FOWLevel other than the shipped one, and the white fog mask over an arc whose plots are fogged
  (Tiny is fully revealed).
- **A bundle recorded live with `ServerArc = 1` and replayed by `sceneshot`** (the synthetic case `CaseArc` of
  `sceneshot_test` covers the replay path; 13.10).
- **Other raise paths:** only `Bombardment.lua`'s raise from the `InGame` context was exercised; a raise of
  `SpawnArrowEvent` through another context's `Events` table reaches the EXE but not the wrapper (no record, as
  6.1 said for P9) [C test].

### 13.10 The arc offline: `sceneshot_test`'s `CaseArc` (2026-11-17 23:xx) [C test]

A synthetic bundle (the 3905 fixture: a 4 x 4 field, the stand-in baker, the module's water, the overlay) whose live
side has `render_arc=1` and one kind-1 `kUiMirror` record (plots (1, 1) -> (2, 2)) in the drain of the span's first
frame; the arc grows over the six frames on the recorded clock (one dt above the 0.25 s clamp). The live side must have
spawned it once and drawn it in every frame, or the case refuses itself ("no teeth"). The player then replays it
byte-equal (3 dumped frames, colour and depth) with `render_config: arc 1`; the same live run with the key left out of
`render_config` replays without the arc and must differ (`colour DIFFERS` in at least two dumped frames). Both adapters
(WARP and the RTX 4080).
- **A trap, found on the first run:** the fixture's field had a zero hex-to-world matrix (`HeightGrid::hexToWorld` is
  "informational" for the terrain, and no earlier case read it), so `plotToWorldXY` put every plot at (-ox, -oy), outside
  the synthetic cameras: the arc was drawn but not seen, and only the negative caught it (the live checks passed). The
  case now builds its field with the EXE's layout of the matrix (64 samples per plot, odd rows half a plot right, centres
  at +32 / +32); the other cases' fields are unchanged. Any later test that places plots on a synthetic field needs the
  same.
- **Results:** MSVC ASan: `sceneshot_test` "RESULT: 203 passed, 0 failed, 0 skipped", no AddressSanitizer report
  (`C:/c5-64bit-work/0199/logs/sb-asan-1.log`); plain: `sceneshot_test` 203 passed, `recorder_test` 247 passed (with
  `C5R_EXE`), and `recorder_test` under ASan 247 passed; 0 failed, 0 skipped in each. `recorder_test` needs `C5R_EXE` (its P2
  case reads the EXE's programs): a run without it fails that one check by design (232 passed, 1 failed), not a change
  of this ticket; `server/evidence/0199/tests_0199.sh` sets it from the installed game.
