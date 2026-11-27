# World systems, why five of them cannot be switched off, and the stubs

Sources: `shell-gates/STUBS.md`, `shell-gates/stubs/terrain.md`, `stubs/unit.md`, `stubs/landmark.md`,
`stubs/river_arrows.md`, `stubs/coupling.md` (all 2026-11-10, static Ghidra 12.1.4 analysis of a copy of the
analysed project + cdb on the probe minidumps), `exe-world-subsystems/PROBES.md`, `shell-gates/GATES.md`,
`memory-notes/civ5-shell-gates.md`, `memory-notes/civ5-exe-message-queue-overflow.md`.

DX11 VAs at ImageBase 0x400000 (TimeDateStamp 0x546CD0A8). Tags kept from the sources: **[C]** confirmed by
decompile and/or dump, **[I]** inferred, **[L]** verified live (STUBS.md usage). In the per-system notes, [L]
sometimes means "needs a live run"; where a claim was not run it says so. Effort figures are Opus-session-hours.

## 1. The shape of the problem

- **Every world system is a static `.data`/`.bss` object, not a heap object** [C]. A disabled system is a
  zero-filled object that is always there, so the crashes are NULL *members* that Startup/StartupGame would have
  created (write 0xC, write 0x4028, read NULL), never a NULL system pointer. A "null-object vtable swap" is the
  wrong stub shape: there is no system pointer to swap. The fixes are mask writes and calls to the EXE's own
  initialisers.
- **Two masks** (details in `disable-systems-and-masks.md`): config `[app+0x940]` gates startups;
  per-view `[GVS+0x1B4]` gates per-frame update and render, starts as 0xFFFFFFFF and is only partially cleared.
- **Mod activation ignores the mask** [C]: `SetActiveDLCandMods` 0x6B8E50 (block 0x6B9A17-0x6B9A95) and
  `PostProcessModActions` 0x629910 (0x629BA1-0x629C1F, 0x629BE3) read the modinfo flags `ReloadLandmarkSystem`
  (string 0xA7AEA8), `ReloadStrategicViewSystem` (0xA7AEC4), `ReloadUnitSystem` (0xA7AEE4), `ReloadAudioSystem`
  (0xA7AE90) and restart those systems with no test of `[app+0x940]`. CP, VP and the EUI compat modinfos set all
  four to 1. There is no Terrain, River or Arrows reload flag.
- **Event bus** [C]: systems subscribe to a multicast bus in their Startup; 359 registrars and 673
  subscriptions decoded (`coupling.md`). A system that never starts never subscribes, so dropping record types is
  mostly free. No crash found was in an event handler; the crash paths are the bake, the never-cleared mask bits,
  the pose pool and the city-field manager.
- **Address trap**: the PROBES.md "EXE+0x..." values are crashes.log file offsets; VA = offset + 0xC00 + 0x400000
  for `.text` (see `addresses.md`).

### Static objects [C]

| Bit | System | Object(s) | Startup | StartupGame | Update / Render |
|---|---|---|---|---|---|
| 0 | TerrainSystem | **0xD165E0** (~0x75FE8 bytes, no vtable; sub-objects +0x4F4, +0x500, +0x50C river-edge grid 0xD16AEC, +0x276D8 Marsh, +0x27818, +0x529BC) | 0x6B8370 | 0x4B2050 (and 0x71CAF0 at 0x46949B, unconditional) | U 0x4A8DD0 / 0x51B070; R 0x657070 / 0x6C3860; reflection 0x751F30 |
| 1 | Landmark family | 0x2D70310 (cvLandmarkSimSystem), 0xDB13D8 (cvLandmarkVisSystem), 0x2B735E8 (RoadLayout), 0xBA20D0 / 0xBA2224 / 0xBA2D40 + 0xB9DDA0 (farm/city-field/RoadLayout group), 0xC8F908 (DynamicGeometryVisSystem), 0x2D90BE8 | 0x636590, 0x62AE40; GVS Startup 0x661D10 / 0x465600 / 0x432220 / 0x4AB360 / 0x429F60 | 0x505400 | U 0x5AB0C0, 0x498B60; R 0x7A6680 / 0x602190 / 0x4FB320; shadow 0x559DF0 / 0x674AD0; reflection 0x69E5B0 |
| 2 | Overlay | 0xCF0AC0 | 0x6FFEE0 | 0x404260 | U 0x703B10; R 0x6EAD50 |
| 4 | Decal | 0xBA37D4 | 0x40CF10 | - | event-driven (SerialEventTerrainOverlayMod) |
| 5 | City | **0x2A11010**, 0xCD0D98 | 0x6DEF50, 0x5640A0 | 0x662C50 | U 0x537BF0 / 0x4572C0; R 0x472540 / 0x4A2960; shadow 0x63BBE0; reflection 0x6EC630 |
| 6 | Unit | **0x2D6F5F8** (UnitSim), **0xD02200** (Unit_System) | 0x6B2BC0 (events 0x6BCA20), 0x405C70 (events 0x710060) | 0x44D8C0, 0x75CE30 | U 0x42FA80 / 0x423D70; R 0x7058B0 / 0x40D590 / 0x445580; shadow 0x6D6830; reflection 0x609730 |
| 7 | Forest | 0xDA91C8, 0xBA395C | 0x726050, 0x469980 | 0x440230, 0x6178D0 | U 0x439E00; R 0x6BFE90 |
| 8 | River | **0x1A0C694** (.bss, flag +0x2C) | 0x638880 | none | U 0x553BE0 (empty function); R 0x463640, called from the Terrain render 0x657070 |
| 11 | Combat | 0x2D701A8 | 0x4C7AD0 | - | U 0x774FE0 (bit forced on) |
| 12/13 | Particle / Projectile | 0xDA9398, 0xC8EB50 / 0xC8EFB8 | 0x542100, 0x67B360 / 0x744970 | 0x503420 | U job 0x61FEC0; R 0x6239C0 / 0x6642F0 |
| 14 | FOW | 0xDAAA20 | 0x5BB6E0 | 0x6926B0 | R 0x55B8B0 |
| 15 | FluidFOW | 0xDA94D8 | 0x44A570 | 0x5B6AF0 (called at 0x468EFD) | U 0x4090F0; R 0x5B57F0 / 0x49C940 / 0x602B10; FOW CULL 0x6EA7A0 |
| 17 | DistanceFog | 0xD90EC4 | 0x69F240, unconditional at 0x47602E | - | - |
| 18 | StrategicView | 0x1A0ADA0 | 0x73FDB0 | 0x674710 | mode-2 path |
| 19 | Minimap | 0x1A0C670 | 0x692D80 | 0x549DF0 | U 0x4BEAB0; R 0x72C310 |
| 21 | Waves | 0x1A0C6C8 | 0x549C80 | 0x54BE70 | U 0x6AB850; R 0x643B60 |
| 22 | Arrows | **0xB9C418** (.data) | 0x526850 (events 0x671150) | none | U 0x643A60; R 0x4E5590 |
| 23 | MovementPath | 0xDAB490, 0xC8EEFC | 0x595690, 0x5B67E0 | (0x78F0B0 on 0xDAB490) | U 0x42C510; R 0x70BE30 / 0x519B90 |

Not gated, always present [C]: GridHeightmap 0xD91270 (pointer 0xD90FC4), GameSplineDatabase 0xBA3BC8 (app
Startup 0x7B1E80, StartupGame 0x78F0B0 at 0x468BC7), ColorKey 0xD90DE8, hex/world matrices 0xD90FF0 / 0xD91030
(MapSetup 0x535BF0), integer hex transforms 0xD91130 / 0xD911D0, map extents 0xD90FCC...

Global coordinators [C]: per-frame world update 0x6FBA80 (from GVS::Update 0x7253E0 and load stage 10); 3D render
0x7492E0 (also 0x5893F0); shadow pass 0x523CA0 and reflection pass 0x5A0F80, called every frame and gated
internally; app Startup 0x475F40; GameView Startup 0x623170; GVS::StartupGame 0x4686C0; bake 0x60ABB0 (not
gated); load stage 4 0x42FCC0 (posts messages only); game shutdown 0x580C50 (callers 0x77E580, 0x6E3230,
0x774950, 0x7B15E0; the RoadLayout write at 0x580F14 is unconditional); app shutdown 0x7A2200; mod activation
0x6B8E50 (not gated); debug menu 0x4421B0 (debug builds only, `[app+0x938]` bit 8).

## 2. Summary of the five (STUBS.md)

| System | Why disabling crashes | Cheapest stub | Effort | Confidence / state |
|---|---|---|---|---|
| LandmarkSystem | the **City** update writes into the city-field manager 0xB9DDA0, which only 0x432220 initialises, called only from the Landmark branch of GameView Startup (0x623280) | **none: disable City too** (B4 does). City on: the DLL calls `0x432220(this=0xBA2224)` once | 0 (config); 3-5 with City on | high; config route live-verified |
| River | `[GVS+0x1B4]` bit 0x100 never cleared, so the Terrain render 0x657070 calls River render 0x463640, whose first call 0x8CD490 resets a NULL batch (+0xC) | one heap write: clear bit 0x100 after app init | 1-2 | high, live (loads + 3 AI turns) |
| Arrows | same bug, bit 0x400000; render pass B 0x7492E0 -> 0x4E5590 hits the same reset | same write, bit 0x400000 | 1-2 (+1-2 to harden pass A) | high, live (arc not exercised) |
| Terrain | `Terrain=1` skips TerrainSystem::Startup but NOT the per-load bake 0x60ABB0 -> CreateTerrain 0x709B50 -> marsh replay -> 0x6B5A50, which reads a marsh stamp buffer (0xD3DCCC) only Marsh::Startup fills. Any map with marsh crashes | c1: keep on, clear bit 0 of `[GVS+0x1B4]` (0 MB); c2: Terrain=1 + DLL calls the EXE's sub-startups (a few MB); c3: skip the bake (~22 MB+, heights vanish) | c1 1-3; c2 8-14; c3 15-30 | c1 high; c2/c3 medium-low |
| Unit | a shared Granny pose pool 0x5F63958 is built by whichever of Unit/City/Landmark starts first; MovementPath (and the landmark renderer) use it before anyone built it. Mod activation restarts Unit anyway | "rendering off, sim on": config (B4 + Unit with City kept on) or a DLL pool fill. True absence: + `ReloadUnitSystem=0`, MovementPath=1, forced quick combat | ~5 (render off); 14-21 (absent) | render-off high, live; absence medium |

Stub total for Split 1 ~10-20 h; see section 8 for integration.

## 3. LandmarkSystem

**Crash** [C] (dump `CvMiniDump_20261109_180442_*.dmp`, EXE at 0x240000, EIP 0x5DC33D): AV writing 0x4028,
`mov [edi+2008h],ecx` at **VA 0x79C33D** (crashes.log file offset 0x39b73d), edi = 0x2020. Stack: 0x459D8A (4 MB
queue pump) -> 0x6A59A5 (dispatcher) -> 0x866919 (listener trampoline) -> 0x4034DC (per-frame GameView-update
record handler) -> 0x6FBA80 (per-frame update of every world system, gated on `[view+0x1B4]`) -> `if (mask & 0x20)
0x537BF0` (City update) -> **0x79C2D0**.

What 0x79C2D0 does (`this` = the city system, argument = a city index): it finds the city's record in an array of
0x6F4-byte records at `this+0x23C`, picks one of two 0x2020-byte stamps by whether the record's type is above 6 (the
second stamp is 0x2020 bytes after the first), adds that to the stamp vector's data pointer at 0xB9DDA4, and writes a
float at +0x2008 of the stamp. With the data pointer NULL and the second stamp chosen the write goes to 0x4028: the
AV. (Decompiler excerpt replaced by this description, 2026-11-18, D83 audit.)

The NULL is the data pointer of a vector inside the **city-field manager** singleton 0xB9DDA0 (allocator tag
`cvCityFieldManager.h`, name inferred): +0 init flag, +4/+8/+C vector of 0x2020-byte stamps (`TF_Small_City.dds`,
`TF_Large_City.dds`), +0x1C vector of 0x48-byte entries, +0x28 a 0x4020-byte 128x128 grid, +0x3C blend entries
(`TF_BLEND.dds`), floats to +0x4080 (0xBA1E20). Created only by **0x432220** (this = 0xBA2224; calls 0x663FC0
blend textures and 0x7812D0 city stamps), whose only caller is GameView Startup 0x623170 at 0x623280, inside the
LandmarkSystem (bit 0x2) branch. Freed only at exit (0x9D40E0). All four users sit in the City update 0x537BF0:
0x79C2D0, 0x569720, 0x738780 -> 0x451DF0 -> 0x7A04F0 / 0x60D3D0, 0x5BAC00 -> 0x7A04F0 / 0x60D3D0.
(Note 2026-11-15, 0319: the two stamps are 128 x 128 fields of unit 2D vectors and the manager is a direction field
for laying out farm fields and city objects, not a height change; the farm-field layout 0x6016F0 also reads it.
`research/05-graphics-composition/cities-and-landmarks.md` 1.9.)

**What the bit covers** [C]: five objects. 15 code sites test the bit (7 lifecycle, 1 per-frame block of 4
calls, 7 render-pass guards: 0x523E84, 0x589475, 0x589646, 0x5A1025, 0x749458, 0x749601, 0x5CA677).
- 0x2D70310 cvLandmarkSimSystem: app Startup via CEG thunk 0x75CC70 (0x7A2316); StartupGame 0x505400 (0x468C6E);
  per-frame 0x5524B0 (0x6FBB93); shutdown 0x433CD0 (0x580F30). Sub-startups 0x74CDC0 (farm types, listener group
  0x582970), 0x4D8FD0 (group 0x592670), 0x4F8D00 (group 0x52D800); unregister 0x614B50. ~23 queue listeners.
- 0xDB13D8 cvLandmarkVisSystem: app Startup 0x6711A0 (0x7A2329); art reload 0x636590 -> 0x560340
  (ArtDefine_Landmarks; `cvLandmarkLibrary.cpp:475` 3.9 MB); per-frame 0x498B60 (0x6FBC32); render 0x559DF0
  "LandmarksShadow", 0x7A6680. The art library at +0xB58 = 0xDB1F30 (0x1800-B entries, count +0xC08); instances
  +0xB20/+0xB24 (0x4B0-B entries).
- 0x2B735E8 layout listener object: GameView Startup 0x661D10 -> 0x758440 registers 8 listeners (0x5DE910 plot
  route bits, **0x5EC250 route builder -> 0x583CE0 TerrainDecal route curves**, 0x5FD600, 0x49E290, 0x710ED0,
  0x450A70, 0x4A3BE0, 0x521B70); per-frame 0x5AB0C0; shutdown 0x43AF60 (0x6208C1).
- 0xBA20D0 / 0xBA2224 / 0xBA2D40 + 0xB9DDA0: GameView Startup 0x465600 registers **0x748FC0 farm-field builder**
  (ART_DEF_IMPROVEMENT_FARM -> 0x6016F0), 0x45BDB0, 0x4C2CA0, 0x79D040; 0x432220 city-field init; 0x4AB360
  (this 0xBA2228) "RoadLayout::GameView::Startup()"; shutdown 0x490A10.
- 0xC8F908 DynamicGeometryVisSystem: GameView Startup 0x429F60; StartupGame 0x78F0B0; per-frame 0x553BE0 (an
  empty function); shutdown 0x68FB90. Handle table +0x158/+0x15C is bounds-checked by every user.

So "LandmarkSystem" = landmarks + farm fields + route decals + city-field stamps + dynamic geometry. With
`ReloadLandmarkSystem = 1` (CP/VP/EUI), mod activation restarts 0x2D70310.Startup (0x75CC70), 0xDB13D8.0x6711A0,
0xDB13D8.0x636590 and 0x2D70310.0x62AE40(0) regardless of the bit, so the app-level sim, renderer, art library
and ~23 sim listeners exist anyway; only per-game, GameView, per-frame and render parts are skipped.

**Entry points with Landmark off** [C unless noted]: 15 gated sites; ~14 ungated entries, of which one path (the
City update, 4 sites) crashes. Safe: City -> DG 0xC8F908 (bounds-checked); Terrain bake -> 0xBA20D0.0x760920 /
0x59CB60, 0xBA2224.0x6E27D0 (empirically passed in the probe); bake -> 0x2B735E8.0x5DE910 x6 + memcpy into
0x2B735FC; Terrain reinit 0x6684D0 -> 0xBA2228.0x435870; render frame -> 0xDB13D8.0x447930 -> 0x4F8B00 (count 0);
0x6BC770 (checks 0xDB14AC & 1). Sim listener groups 0x582970 / 0x592670 / 0x52D800 are active because of the
reload and were only spot-checked (handlers incl. 0x5307A0, 0x603000, 0x7AD6C0, 0x587170, 0x540640, 0x555120,
0x4349F0) - the main residual risk. Save/load, UI/Lua/picking/minimap/strategic view: no reference found.
Resolution change / device reset: not found, not exercised. Exit: static dtors 0x9D40E0, 0x9D40B0, 0x9D44D0,
0x9D3C30, 0x9D4130/40 run always (assumed safe). DLL producers: `GameplayPlotStateChange` (CvPlot.cpp:14044 via
`updateLayout`), `GameplayCityCreated` (CvCity.cpp:1845), `GameplayWonderCreated` (1883, 1939),
`GameplayCityCaptured/Destroyed`.

**Stub** (landmark.md): (c) config only, City off: predicted and then **verified live 2026-11-10** (B4 +
LandmarkSystem loads with no stub, 3 AI turns, 1,623 MB, -53 vs B4). (b') City on: from the DLL call
`thiscall 0x432220(this = 0xBA2224, int* dummy)` (`ret 4`, idempotent through the flag at 0xB9DDA0) once before
the first in-game frame, after verifying 16 prologue bytes at 0x432220 and the call at 0x623280; a hand-built
zeroed block is not enough (four vectors with engine-allocated memory). Fallback (a): drop or suppress the
DLL-side producers (`CvPlot::updateLayout` -> `GameplayPlotStateChange`, `GameplayWonderCreated`) if a sim
listener faults; never suppress `GameplayCityCreated/Captured/Destroyed`. Effort: (c) 1.5-2.5 h, (b') 3-5 h,
fallback +2-4 h, stopping the reload for the full saving +2-4 h. Memory payoff of the switch itself was
unmeasured statically; live it was -53 MB plus a faster load (2:47 -> 2:08; corrected 2026-11-10 to -30.8 s,
148.2 -> 117.4 s same-night A/B, `load-sequence.md` section 4).

## 4. River and Arrows

**Crash** [C] (dumps River 181104, Arrows 184604, A2 173954, B2 174555): AV writing 0xC at **VA 0x8CD492**
(file offset 0x4cc892), ECX = 0, render thread, called from the frame function (0x4B40CD -> 0x628286 -> world
pass). 0x8CD490 is a 30-byte `__fastcall` helper that zeroes a render batch's counters (+0xC, +4, +0x40, +0x44,
+0x48, +0x30, +0x54, +0x207C), inferred **RenderBatch::Reset**; the batch is a 0x2080-byte object from
`..\dx11\D3D11Renderer.cpp:0xf82` made by 0x8BD0C0 (inferred Renderer::CreateBatch); 12 call sites.

| Dump | EBX -> VA | Caller | NULL field |
|---|---|---|---|
| River | 0x1A0C694 RiverVisSystem | 0x463640 +0x15 (ret 0x46365A), "River Visualization" | `[this+0x10]` |
| Arrows | 0xB9C418 Arrow_System | 0x4E5590 +0xC (ret 0x4E55A1), "Arrow Visualization" | `[this+0x74]` |
| A2, B2 | 0x1A0C694 | River render (River draws before Arrows) | `[this+0x10]` |

Root cause: one EXE bug class hit twice. App Startup gates Startup correctly (0x4761A1 `test eax,0x100` ->
0x638880 RiverVisSystem::Startup(0x1A0C694); 0x476359 `test eax,0x400000` -> 0x526850 ArrowVisSystem::Startup
(0xB9C418)), so River +0x10 batch, +0x14 0x30-byte "WaterShader" material (ctor 0x60F0B0), +0x24 32x32 texture and
Arrow +0x74 batch, +0x78 "ArrowShader" material (0x705440) stay NULL. But no per-game startup clears 0x100 or
0x400000 in `[GVS+0x1B4]`, so the world pass calls both renders, each of which opens with RenderBatch::Reset:
River via 0x657070 (only if mask & 1) -> `0x6570D1 test [GameView+0x1b4],0x100` -> 0x463640 (passes its
`[this+0x2C]` check because the Terrain bake built the river mesh anyway); Arrows via pass B 0x7492E0
`0x7495D6 test [esi+0x1b4],0x400000` -> 0x4E5590 (dump return 0x7495ED). Names from profiler labels 0xA10424
"RiverVisSystem::Startup()" and 0xA10474 "Arrow_System::Startup()"; sources `.\App\Graphics\RiverVisSystem.cpp`,
`.\App\Graphics\ArrowVisSystem.cpp`.

River entry points (9, 1 crashing) [C]: app Startup (skipped); Terrain bake 0x709B50 at 0x70AE88 -> 0x6D51D0
(Terrain+0x50C) -> 0x560B40 (this = River) -> 0x738D10 mesh + 0x6B1580 (VB +0x1C, IB +0x20, draw record +0x18,
tri count +0x28, **+0x2C = 1**) - runs, safe; Terrain Startup 0x6B8579 -> 0x687630 -> 0x8F3650 (empty); Terrain
Shutdown 0x6CCA90 -> 0x43F3F0 -> 0x4A1300 (clears); minimap render 0x46E550 -> 0x51C820 -> 0x61B050 (minimap
rivers keep drawing); **world render** pass A 0x5895E5 / pass B 0x74959C -> 0x657070 -> 0x463640 (CRASH);
per-frame update 0x6FBCDF -> 0x553BE0 (`ret` stub shared by ~90 callers); ShutdownGame 0x580E1A -> 0x6A1C20
(re-derives bit 0x100); app Shutdown 0x7A2370 -> 0x6A5910 (skipped). No queue records, no events (the engine
event `RiverTileAdded`, 0x6BD7C6, handler 0x6542D0, is subscribed by Terrain 0x687630 and RoadLayout 0x758440,
not by River).

Arrows entry points (8, 1 crashing in pass B, 1 latent in pass A) [C]: app Startup 0x526850 (batch, material,
0x400x0x28 and 0x400x4 vectors, `assets/UI/Overlays/ArrowSettings.xml` via 0x681470, 3 event subscriptions,
1 hidden default arrow) - skipped; events via 0x671150: 0x79AF20 spawn, 0x69A840 remove all, 0x553BE0 no-op;
Lua `Events.SpawnArrowEvent` / `RemoveAllArrowsEvent` registered unconditionally (0x4D2966 / 0x4D2989); per-frame
0x6FBCA3 -> 0x643A60 (safe: loop count 0); **pass B** 0x7495D6 -> 0x4E5590 (CRASH); **pass A** 0x58961F (gated on
the Overlay bit) -> 0x4E5590 (crash if pass A and Overlay on); app Shutdown 0x7A2274 -> 0x5D0EA0; static init
0x9C5BB0 / atexit 0x9D3BE0; mesh builder 0x4D9D30 (unreachable).

**Stub**: the view-mask write (`disable-systems-and-masks.md` section 4). **Verified live 2026-11-10**: B4 +
Landmark + River + Arrows with bits 0x400100 cleared loads and plays 3 AI turns (1,625 MB). Payoff about zero
(< 1 MB inferred: a 0x2080-B batch, a 0x30-B material, a 32x32 texture; Arrows also ~44 KB of vectors and an XML
parse); leaving both on costs nothing, and with Terrain not drawn the River render is unreachable anyway. The
patch matters so that a "world off" key list cannot crash. Effort ~3-4.5 h for both verified (+1-2 h Arrows pass A,
+1-2 h Combat bit 0x800).

*2026-11-14 (task 3802): the arrow's spawn, mesh builder, render and ArrowShader, and MovementPath's markers, are now
described as behaviour in `arrows-and-movement-path.md`.*

What the player loses: River=1 removes only the animated water surface (banks and channels are terrain pieces
from the bake; minimap and strategic-view rivers stay). Arrows=1 removes only the ranged-attack arc spawned by
`Bombardment.lua` (5 call sites in CP); trade-route arrows (`TradeArrowShader`, 0x45D280), yield popups (0x6B3A80)
and the move preview (MovementPath) are separate.

## 5. Terrain

**Crash** [C] (dumps `CvMiniDump_20261109_170831`, `_172708`, `_173306`; EXE at 0x240000, EIP 0x4F5B3C =
**VA 0x6B5B3C**, `cmp byte ptr [edx+ecx],0` with edx = ecx = 0). It is **not** in the message-queue dispatch; it is a
direct call chain from the load state machine:

| Return | In | Role |
|---|---|---|
| 0x6A7523 | 0x6A74D0 | builds `{grid=*(0xD90FC4), player byte}`, calls `0x6B5A50(this=0xD3DCF8, mask, &grid)` |
| 0x630C1F | 0x630B70 | Marsh::OnMarshPlot(rec): defer if not ready, else stamp 128x128 around the hex |
| 0x63B10F | 0x63B0F0 | Marsh::FlushDeferred: ready=1, replay deferred 0x18-byte records |
| 0x70AD34 | 0x709B50 | TerrainSystem::CreateTerrain (this 0xD165E0; call at 0x70AD2F with ecx = this+0x276D8) |
| 0x60BBD6 | 0x60ABB0 | LoadGameState::StartInitTerrainFromGameplayMap (vtbl 0xA20EBC slot 7) |
| 0x40325F | 0x403170 | LoadGameState::Update (slot 8), state 2 -> 3, logs "StartInitTerrainFromGameplayMap()" |
| 0x5FAC07 | 0x5FAC00 | slot 3 -> vtbl[8] |

The NULL is the marsh stamp array `Marsh+0x14` = `*(DWORD*)0xD3DCCC` (count +0x18, capacity +0x1C, 0x4008-byte
entries), filled only by `Marsh::Startup` 0x517880 (ecx = 0xD3DCB8; loads `marsh_stamp.dds`, 128x128 L8; calls
`exit(0)` if missing), which `TerrainSystem::Startup` 0x6B8370 calls at 0x6B8584. The records come from
StartInitTerrainFromGameplayMap itself (one per plot whose `plot->vtbl[0x74]() == 2`, at 0x60AFCB; 2 = FEATURE_MARSH
[I]), so no queue is involved and **any map with a marsh crashes**. The 0x866919 / 0x6A59A5 / 0x459D8A values that
PROBES.md read as a dispatcher stack are stale words inside `LoadGameState::Update`'s locals, not live frames.
Under Terrain=1 the Marsh add/remove listeners (0x630B70 / 0x7620B0, registered via 0x861F70 / 0x861FD0, spinlock
0xCBFC70) are never registered, which is harmless.

**TerrainSystem 0xD165E0 members** [C unless tagged]: +0x0..+0x4F0 TerrainDecalSystem (`TerrainDecalSystem.cpp`;
the TerrainDecalShader object 0x45EE20 at +0x18, batch vectors +0x90/+0x9C/+0xB4/+0xB8 with 0x7FFF8-byte vertex chunks,
**+0xC0 the variant table** (16-byte variants {priority, albedo, heightmap, spec}, count +0xC4, capacity +0xC8) from
Startup 0x714B80 / `decal_filenames.xml`, the per-call draw list +0x4D0, the spatial index embedded at +0x4DC, the
default spec handle +0x4F0); +0x4F4..+0x50C [I: not decal members]; +0x50C sub-object
*(corrected 2026-11-15, epic 0166, terrain-decals.md 1.1 and 1.3 [C, L]; replaced: "+0x0..+0xC0 TerrainDecalSystem
(... decal-name manager 0x45EE20 ... **+0xC0 decal type table** ...); +0x4F4..+0x50C plot/decal grids [I]". 0x45EE20
builds shader 78 `TerrainDecalShader` (constant buffer 0x70), it is no name manager; the decal system's own members end
at +0x4F0 and no decal code reads +0x4F4..+0x50C; `decal_filenames.xml` lives in `Resource/Common/MiscData.fpk` and
holds one variant for each of ten categories, so the variant table names only the river, flood-plain and tree-shadow
textures and carries the priorities of `Detail_Road`, `Main_Road` and `Crops`; route curves, farm fields and model
grounds bring their own texture handles)*
(0x687630); +0x276D8 Marsh; +0x27718..+0x27728 grid shortcuts (CreateTerrain); +0x2772C Water (Startup 0x5F06A0;
reflection texture 0xDA12C0); +0x27818 sub-object (0x6E3C50); +0x629DC 0x30-byte object; +0x629F4.. texture pool
counts/handles (config 0xD90F38..0xD90F48); +0x75708/+0x7570C/+0x75710 three dynamic buffers via 0x8BD0C0
(0x80000, **0x400000**, 0x80000); +0x75714 job object; +0x7572C..+0x7574C light directions; +0x75750 TerrainDef.csv
table; +0x75F74..+0x75F98 lighting textures; **+0x75FA4 (0xD8C584) TerrainRenderer** (0x29D1C bytes,
`TerrainSystem.cpp:0x3c2`, built by 0x5BCEF0); flags +0x75FD9/+0x75FDA/+0x75FDC. Static ctor 0x7339B0 (from
0x9C56B0), atexit dtor 0x53E980 (from 0x9D3A40).

**Entry points under Terrain=1** (terrain.md section 3): 7 gated (Startup 0x6B8370 via 0x47696C; StartupGame
0x4B2050 via 0x468BF3; ShutdownGame 0x6684D0 via 0x58100A - skipped, so CreateTerrain's state is never freed and
0xD90FC4 never cleared and a second load in-process stacks on top [needs live check]; Shutdown 0x6CCA90 via 0x7A23BB;
per-frame update 0x51B070; draw 0x657070 / 0x6C3860; Forest draw 0x6BFE90 -> 0x63A7A0 reads TS light dirs);
9 harmless or null-guarded (0x6BB810 timers, 0x71CAF0, shadow pass 0x523CA0 guarded by 0xDA12B8, reflections
0x5A0F80 guarded by 0xDA12C0, shadow view matrix 0x615950, debug stats 0x6A5CC0, Marsh listeners); **1 confirmed
crash** (the bake); **3 likely crashes [I]**: the bake's tail (decal adds 0x6E27D0 -> 0x55D0C0 -> 0x60F180 ->
0x49B5A0 index the never-loaded type table +0xC0; *corrected 2026-11-15, epic 0166, terrain-decals.md 2.2 [C]: this
tail lays **farm tracks** (strips along polylines, category 0 `Detail_Road`; the same 0x6E27D0 runs from the farm
builder 0x748FC0 -> 0x61FD40); the river pieces (Riverbank, Floodplains and their end caps, categories 4-9) come from
0x559FF0 (called from 0x7857F0) through 0x60F180; both index the variant table +0xC0, so the crash reasoning stands
[I]*), the load-screen terrain draw 0x70CBC0 -> 0x68EFE0
(`0x4AE6F0(TS+0x7570C = NULL)`), and the runtime decal producers -> AddDecal 0x49B5A0 / RemoveDecal 0x68F1B0 (route
builder 0x5EC250 -> 0x583CE0 -> 0x49E660 -> 0x5EC600, farm builder 0x748FC0, 4 MB-queue field polygons 0x7AD6C0 ->
0x5865B0 / 0x543D10, 0x6DEC60 / 0x4349F0 -> 0x51D5A0, landmark visualizer 0x64E930 -> 0x85E7F0 / 0x85E900 ->
0x700030, cvWonderVis 0x44CF70 -> 0x62C390 / 0x76EBA0 -> 0x65A150, tree cut 0x598380 -> 0x614350 -> 0x69F250,
0x75D7D0 -> 0x52A360); 3 unknown (0x439E00 per-frame decal list, minimap 0x46E550 -> 0x578E80 / 0x62CF00, the
static dtor) plus debug Lua hooks (`Water_SetScale` 0x5A8770, `Water_SetAutoReload` 0x5F7980, `Water_SetSpec`
0x605320, `Water_SetSpeed` 0x74D7A0, `Ice_Reload` 0x77CC30). The route and farm listeners are registered under
the LandmarkSystem bit, so Landmark=1 removes the decal-producer crash path.

`coupling.md` writes that the Terrain shutdown 0x6684D0 "sits behind bit 1 at 0x58100A"; `terrain.md` says bit 0
at the same address. The terrain-specific note is the more detailed reading; not live-checked.

**Stub options** [terrain.md]:

| Option | What | Saves | Implement / verify | Main uncertainty |
|---|---|---|---|---|
| c1 | Terrain stays on; clear bit 0 of `[GVS+0x1B4]` after **each** StartupGame (0x468BCC re-derives it) | 0 MB; terrain/water/overlay/reflection draw stops; bake and heights stay | 0.5-1 h / 1-2 h | whether anything else reads bit 0 as "terrain exists" (scan: only the two renderers) |
| c2 | Terrain=1 + before the first load the DLL calls the EXE's own sub-startups: Marsh 0x517880 (ecx 0xD3DCB8), decal table 0x714B80 (ecx 0xD165E0), probably Water 0x5F06A0 (ecx 0xD3DD0C), and one 4 MB buffer for +0x7570C or clear +0x75FDC after the bake; undo ShutdownGame's work at exit (call 0x6684D0 or clear 0xD90FC4) | a few MB [I] (texture pools and render targets are VRAM) | 3-5 h / 5-9 h (3-6 crash iterations) | how many more never-started members the bake tail and decal producers touch |
| c3 | skip the bake: heap vptr swap of LoadGameState slot 7 to a DLL function that runs only GridHeightmap::Init 0x47E350 (+ route/feature records) | CreateTerrain blocks >= 1 MB = 21.6 MB on the huge save (`Terrain.h:79` 9.1, `D3D11Renderer.cpp:1982` 5.6, FFastVector 4.1, FDataField 2.8) + decal chunks + render caches | 6-12 h / 10-18 h | everything that reads baked data (rivers, coast, water, decals, unit heights) |

A null-object at field level (DLL writes dummies into the static object, e.g. a zeroed 0x4008-byte stamp with
count 1 at 0xD3DCCC) is possible and CEG-safe but needs crash iteration per member. Recommended before c2:
measure the payoff at the main menu with Terrain=0 vs 1 (the crash only happens at the end of a load). Not done.
Live plan in terrain.md section 6 (picking, units on hills vs baseline, roads/farms/wonder/chop in AI turns, a
marsh on screen, save + reload in-process, resolution change, strategic view).

## 6. Unit

Two crashes, one defect [C]: the engine's **shared Granny pose pool** `g_PosePool` = **0x5F63958** (.bss) is created
by whichever world system starts first. Per worker thread `t` a 0x180-byte block at `0x5F63958 + t*0x180`: +0x00
32 local poses (0x9BC8A0(0x100)), +0x80 32 world poses (0x9BC7D0(0x100)), +0x100 32 Granny DAG pose caches
(0x9A07B0, `granny_dag_pose_cache.cpp`). Thread count `[0x6196D54]` (read by 0x81D870). Slot creator **0x647F60**
(`__thiscall`, ecx = thread block, arg = slot count; idempotent).

| Creator | Caller | Bit | Boot order |
|---|---|---|---|
| 0x7A2470 | 0x405C70 Unit_System::Startup (ecx 0xD02200) | 6 | 1st (gate 0x476222) |
| 0x77CCD0, 0x5AFE60 | 0x5640A0 City startup | 5 | gate 0x4765E9 |
| 0x5D2840 | 0x636590 LandmarkSystem::Startup (ecx 0xDB13D8) | 1 | gate 0x476675 |

Pool users that assume it exists: 0x4B5580 (MovementPath startup, "cvMovementPathVisSystem"), 0x70BE30, 0x77DE00
(cvLandmarkLibraryEntry::BindAsset), 0x630A00, 0x688750, 0x6E8980, 0x769B60, 0x7948B0, 0x795050 (unit vis
instantiate), landmark render tasks.

- **Crash A, Unit=1 alone** (3 of 3, Steam .mdmp in the game root): main thread, AV read 0 at **0x9A3CF7**
  (0x9A3CE0, `granny_world_pose.cpp`), stack 0x8E57A2 (0x8E56E0) <- 0x5B1A5D <- **0x4B59D2** (0x4B5580) <-
  **0x595690 MovementPath startup** (gate 0x4763DA, bit 23). MovementPath starts after Unit and before
  City/Landmark. Fix: MovementPath = 1 too (A4/B4 have it).
- **Crash B, A3/B3**: worker thread, AV read 0 at **0x9A0814** (0x9A0800, `granny_dag_pose_cache.cpp`,
  `cmp edi,[ebx]` with ebx = NULL); stack 0x470BB7 / 0x9BCA8E <- 0x5414FB (bone-attachment callback) <- 0x5A48B8
  <- 0x69E6B0 <- **0x67421D** (0x6741A0, a landmark render task) <- 0x81D8EF / 0x7BF97B (TBB-style worker loop).
  With Unit and City off, LandmarkSystem::Startup's library load (0x560340 -> 0x5CC9E0 -> 0x77DE00) calls
  `0x647F60(ecx = 0x5F63958, 1)` for an animated landmark, creating thread 0 slot 0 only; then 0x5D2840 sees
  `pool[0x20]` non-zero and skips building the pool for all threads, but wires every worker context
  (`[0xDB13D0]` = landmark + 0xC2C, written by 0x6187D0 "Render Landmarks"). A latent EXE ordering bug, not a
  Unit-system access.

**What Unit=1 removes with VP mods** [C]: `SetActiveDLCandMods` restarts Unit regardless of the bit:
0x405C70(2) (UnitVisConfig.ini, "LoadUnitLibraries via Database", the 14 listener registrations of 0x710060, pose
pool creator 0x7A2470) and 0x6B2BC0(&0xD02200) (UnitSim, the 24 registrations of 0x6BCA20); `landmark.md` also lists
0x2D6F5F8.0x6E8140 and 0xD02200.0x61F4F0 in that block. So Unit=1 + VP mods = **unit rendering off, sim on**. A
second ungated start exists in 0x40B410 (no static caller). Gated by bit 6: app Startup 0x476222, StartupGame
0x4686C0, shutdown 0x580C50 / 0x7A2200, per-frame/render passes testing `[view+0x1B4] & 0x40` (0x6FBA80, 0x5893F0,
0x7492E0, 0x523CA0, 0x5A0F80).

**Entry points** [C]: 38 queue listeners (registered only by the two startups; absent-safe by construction):
Q1 0x2A0F480 (stride 0x180, pump 0x7052B0) types 6, 7, 8, 13, 14, 16, 19 (type 16 -> 0x5723F0 library reload +
re-instantiate) and 21, 28, 29, 30, 32; Q2 0x2A10200 (stride 0x600, dispatcher 0x63ABA0) types 2, 86, 87, 88 and
72, 73, 76, 77, 79, 80, 81, 89 (-> 0x488B30 -> 0x731FF0 `cvCombatSimSystem.cpp`), 91, 92, 114, 131, 138 + 3
runtime-numbered via 0x86C020 (numbers from 0x528C80, 0x656510, 0x57B4C0); Q4 4 MB queue 0x1A0E400 types 12, 34,
36, 37 (twice), 128. Type numbering derived from displacements, not cross-checked with a live record. ~35 functions
reference 0xD02200 (+0xC20 inferred unit-vis registry with 12 users: 0x42D1E0, 0x470BD0, 0x4981B0, 0x553790,
0x56C4A0, 0x571980, 0x5ACA80, 0x6AE910 (combat sim, 6 calls), 0x6C67C0, 0x700A60, 0x70DE60, 0x781790 - NULL
handling unverified; 0x47F4A0, 0x4D5030 Lua ArtInfo query, 0x6E4630 (+0xC40, Combat-gated), 0x644420 benchmark).
DLL -> EXE: 84 call sites of 20 `ICvEngineUtility::GameplayUnit*` methods (Visibility 21, Activate 15,
DestroyedInCombat 7, UnitCombat 6, ...; `Created` at CvUnit.cpp:1777) + UI selection calls.

**State (ii) in the product (issue 0317, 2026-11-15, `research/05-graphics-composition/unit-state-ii.md`)** [L]:
the product's D33 list + Unit=1 dies at boot with crash A (VA 0x9A3CF7), because MovementPath is on in the product
since 3803; with Unit=1 and MovementPath=1 the huge save loads and the human's turn plays, but the AI turn of turn 350
stalled (the EXE stopped calling `ICvGame1::Update` and polled `ICvUnit1::IsBusy` each frame for over 10 minutes),
and `LocalMachineUnitPositionChanged`, `UnitHexHighlight`, `UnitMoveQueueChanged`, `SerialEventUnitSetDamage` and
`SerialEventUnitDestroyed` no longer reach Lua. The 2026-11-10 run below had MovementPath off (B4) and 3 AI turns.

**Stub** [unit.md]: (1) config check B4 + Unit=1 with City=0 - **verified live 2026-11-10** (loads, 3 AI turns,
AI combat resolved, 1,688 MB); (2) DLL pool fill, CEG-safe, ~40 lines: for `t` in `[0, *(int*)0x6196D54)` call
0x647F60 with `ecx = 0x5F63958 + t*0x180`, arg 0x20, next to `EngineQueueGuard::Install`; (3) true absence: bit 6
off **and** `ReloadUnitSystem = 0` in every active modinfo, MovementPath = 1, pool fill, DLL forces
quickCombat/quickMovement; (4) if UI Lua unit events come from the Unit listeners, the server or DLL must supply
positions/creation events. Effort: ~5 h render-off; 14-21 h true absence. Main uncertainties: who fires the UI
unit events, how combat completes without the combat-sim listener (RunCombatSim / AbortCombatSim are subscribed
only by the Unit system [C]; turns may hang [I]), and the +0xC20 consumers. *(2026-11-15: the turn does not hang
with Vox Populi's DLL, which resolves at once (0316, `research/03-dll-engine-boundary/combat-on-the-product-path.md`).
RunCombatSim is type 0x59 and EndCombatSim 0x5A. The Unit system's listener 0x488B30 starts the simulation
0x731FF0, which runs only with Combat bit 0x800 set and holds the event while it plays
(`research/05-graphics-composition/combat-choreography.md` 3-4, 0321).)*

## 7. Coupling (coupling.md, [C] unless noted)

- **Heights live in GridHeightmap 0xD91270** (pointer `[0xD90FC4]`), not in TerrainSystem. Created
  unconditionally at app Startup (0x476018, capacity from MAX_SUPPORTED_MAP via 0x64C620); MapSetup 0x535BF0 sets the
  pointer on every load (0x536096); filled only by `GridHeightmap::Init` 0x47E350 (called from CreateTerrain at
  0x709D6D, from the editor loader 0x74D1A0 and from 0x40B410) and the setter 0x5A3490 (BlendTerrainHeightmap
  0x46AEF0 writes through it). Init sets the ready byte +9 = 1 at 0x47E6EF. `terrain.md` says the map setup creates it
  and CreateTerrain fills it; `coupling.md` places the creation at app Startup - consistent if "create" means the
  static object and "MapSetup" the pointer.
- Fields: +0x09 ready; +0x0C/+0x10 sample counts; +0x10/+0x1C 16x16-tile directory and pool of ushort heights;
  +0x54..+0x68 byte-per-sample layer (setter 0x775130, readers unknown); +0x9C capacity; +0xA0/+0xA4 samples =
  mapW*64 / mapH*64; +0xA8/+0xAC scale sx = 1.0, sy = 0.866 (`[0xB9A6D4]`, `[0xB9A6D8]`); +0xB0 height scale 0.2
  (`[0xB9A6DC]`), height = ushort / 255 * 0.2; +0xB4/+0xB8 offsets W*64/2, H*64*0.866/2; +0xC0/+0xC4 world size
  (minimap reads 0xD91330/34).
- **If Init never runs, every world position collapses to the origin** and mouse picking 0x6668F0 returns at
  once while `[0xD91279] == 0` [C code, effect not run]. Consumers: HeightAtGrid 0x6D8B60, HeightAtWorld 0x530A10,
  HexToWorld/GridToWorld 0x6851C0; Lua GridToWorld 0x63C280 (thunk 0x680CA0), HexToWorld 0x50F370; EUI flags,
  city banners. Unit placement samples it every frame (0x42FA80 > 0x50EAC0 > 0x5024B0 > 0x530A10), as do City
  (0x537BF0 > 0x5BAC00 / 0x79C2D0 > 0x530A10), Landmark (0x5AB0C0), Forest (0x439E00), Overlay, FOW CULL 0x6EA7A0,
  Arrows (0x79AF20 > 0x6851C0). Because the bake ignores the Terrain bit, heights stayed valid in every tested
  config. 0x74D1A0 is a ready-made "load this ushort heightmap into the grid" template for a server heightmap feed. *Corrected 2026-11-18 by epic 0465 (4900, `live-terrain.md` 4.1) [C static, I for reachability]: 0x40B410's Init call targets its own 64 x 64 grid on the stack, not 0xD91270; 0x74D1A0 is the Nexus `TerrainEditorSystem` heightmap load, which builds its own renderer and is not reached in a game [I]. So neither writes the game's field.*
- **Picking and camera do not need Terrain**: 3D picking intersects a flat plane at z = `[grid+0xB0] * -60`;
  only Strategic View picking calls HexToWorld; the camera clamps to the map bounds box GVS+0x154..0x160.
- **The bake writes into other systems**: RoadLayout 0x2B735E8 (0x5EEE50, 0x5DE910, not guarded), City 0x2A11010
  (0x472C70, 0x686BB0, guarded by `[City+0x7F678]`), 0x2D90BE8 (0x6C7CD0), Terrain sub-grid 0xD16AEC (0x44B5C0),
  0xBA20D0.0x760920 from CreateTerrain (0x709DC7), and registers Terrain's own event handlers (0x5BCEF0 > 0x678290:
  HexFOWStateChanged B114 > 0x701BB0, internal A106 > 0x43CE30). `coupling.md` flags a possible Landmark-off bake
  crash in RoadLayout 0x5DE910 [I]; `landmark.md` calls that path safe (`this` unused) and the live B4+Landmark run
  passed the bake - the live result wins.
- **Landmark <-> City: hard** (the city-field manager). **Landmark <-> decals**: Landmark's view listeners are the
  road/railroad/farm-field decal builders that flood the 4 MB queue on late loads (`message-queue-and-fixed-buffers.md`).
- **River <-> Terrain**: river mesh and banks are part of the bake; River=1 removes only the water surface.
- **Unit <-> combat**: RunCombatSim / AbortCombatSim subscribed only by Unit. **EUI unit flags** follow
  `LocalMachineUnitPositionChanged` (C-manager event, name 0xA58814), fired by the unit model while walking [I].
- **Shadows and reflections** are drawn by Unit, City, Landmark (and Particle for shadows) into the shadow map
  0xDA12B8 ("ShadowRaw") and reflection target 0xDA12C0 as part of the Terrain render [I]; created after
  TerrainSystem::Startup in app Startup (0x4769EF).
- **Event bus** [C]: managers B Lua-visible 156 named events at 0x2A10200 (slot = 0x2A1020C + 8*type; binder
  `push type; call 0x4A2210`; names from 0x6BD641), A internal 0x1A0E400 (engine symbol `g_EventSystemLocalMachine`
  per PR #13379), C 0x2A0F480 carrying the `LocalMachine*` Lua events (slots 0x2A0F4BC..0x2A0F58C; "LocalMachine" is
  an analyst label here, engine name unknown; reconciled 2026-11-10 by the librarian, see `addresses.md`
  Conventions). Registration thunks `lock; mov ecx,[mgr+idx*0x600+slot]; push fn; call 0x861760`. Key
  subscribers: HexFOWStateChanged B114 (Terrain, FluidFOW, Unit handler 0x501CD0, City, Minimap, GVS), RiverTileAdded
  B67, SerialEventRoadCreated/Destroyed B45/46, Improvement B59/60, ToggleBridgeState B47, SerialEventCityCreated B34,
  Wonder B37-39, SerialEventHexCultureChanged B29, SerialEventTerrainOverlayMod B31 (Decal), Unit* B69-B92, internal
  A34/A36/A37/A128.

## 8. Split 1 and Split 2

**Split 1 - server renders terrain, rivers, landmarks, FoW; EXE keeps units, cities, UI.**
Config: B4 + LandmarkSystem + River + Arrows (+ the view-mask write), Terrain via c1 (kept on, not drawn) or c2.
City must stay on, so Landmark needs the `0x432220` call (3-5 h) instead of the free config route. Heights come
free from the bake under c1/c2; a correct picture needs a depth hand-off so server terrain occludes EXE units
(the compositor currently draws under everything). Losses: unit/city shadows and water reflections, minimap
(black). **Stubs ~10-20 h + integration ~30-60 h** (coupling.md breakdown: grid feed 8-15, Landmark-off
survivability 5-10, depth hand-off 10-25, shadows/reflections 2-6, live verification 5-8).

**Split 2 - server renders everything world-side; EXE keeps only UI.**
Adds Unit true absence (14-21 h) with the combat hand-shake and unit positions for EUI flags; the UI needs only the
heightmap and the matrices. **Integration ~45-100 h**, more than Split 1, not in addition to it (Split-1 items
except depth 20-35, Unit-off survivability 6-12, combat/move hand-shake 8-25, server-to-UI flag positions 4-10,
minimap and city-view replacements 4-12, live verification 6-10). STUBS.md phrases it as "Split 2 integration
~45-100 h on top" of the stubs.

Open questions with cost estimates are listed in `coupling.md` section 8 and terrain/unit/landmark section 6-7:
the producer of `LocalMachineUnitPositionChanged` (accessor 0x5DFAD0 > 0x4AB410 / 0x7B9670), readers of the
heightmap byte layer and of Terrain+0x4F4 (0x69F250), the DLL's `ResolveCombat` path after a visible combat, and a
live dump of grid +0xA0..+0xC4.
