# `[DisableSystems]` and the two system masks

Sources: `exe-world-subsystems/PROBES.md`, `memory-notes/civ5-exe-disable-systems.md`, `shell-gates/GATES.md`
(Gate 1), `shell-gates/STUBS.md`, `shell-gates/stubs/{terrain,unit,landmark,river_arrows,coupling}.md`,
`shell-gates/tools/{disable_systems,view_mask}.py`, `probe-logs/*/system_init.log` + `result.json`.

All addresses are VAs of `CivilizationV_DX11.exe` (PE TimeDateStamp 0x546CD0A8, Steam build 4390913, "App
Version 403694 FINAL_RELEASE" in `net_message_debug.log`) at ImageBase 0x400000, unless marked. Confidence:
[C] confirmed in decompile/disassembly/dump, [I] inferred, [L] verified live (the stub notes also use [L] for
"needs a live run"; where that is meant it says so). "EXE+0x..." values copied from `crashes.log` are FILE
OFFSETS, see `addresses.md`.

## 1. The switch

- File: `Documents\My Games\Sid Meier's Civilization 5\config.ini`, section `[DisableSystems]`, 24 keys. The owner's
  file has all of them 0 except `OverlayDebug = 1`, and that value was left in place in every probe, so every
  baseline already has OverlayDebug off.
- Also accepted on the command line (any argument containing the substring `DisableSystems`).
- Parser 0x49FF20, key-name table 0xB94078. [C]
- Each key set to 1 **clears** one bit of the config mask `[app+0x940]`, `app = g_Civ5App = *(void**)0xCC8E74`.
  Bit set = system enabled. [C]
- Gates sit in app Startup (0x475F40-0x476C00), per-game StartupGame (0x468B1E-0x469434), game shutdown/reinit
  (0x580D04-0x58112D) and GameView Startup (0x6231CE-). App shutdown 0x7A2200 is gated too. [C]
- Related engine switches, not part of the mask: `-NullDevice` sets `[0x301058C]` (NULL D3D driver; saves GPU
  work only); `MaxSimultaneousThreads = 8` in config.ini sets the job-thread count that sizes the FluidFOW
  per-thread blocks (see `allocations-and-memory-map.md`). `MaxSimultaneousThreads = 2` is untested.

## 2. The 24 keys

Bit order from the name table (`civ5-exe-disable-systems.md`); objects and profiler names from
`stubs/coupling.md` section 1; single-key results from PROBES.md (2026-11-09, huge save `VP8P-HUGE_0350` t350,
cp+vp+eui, Release 5.4.8 DLL, baseline 1,985 MB private / 1,715 MB WS, one run per key, noise ~20 MB).

| Bit | Mask | Key | Profiler name / static object | Known config gate (app Startup) | Single key, 2026-11-09 |
|---|---|---|---|---|---|
| 0 | 0x1 | Terrain | TerrainSystem, 0xD165E0 | call 0x47696C (local copy test 0x476963) | **crash** at end of load (~107 s), VA 0x6B5B3C |
| 1 | 0x2 | LandmarkSystem | LandmarkSystem / RoadLayout / DynamicGeometryVisSystem: 0x2D70310, 0xDB13D8, 0x2B735E8, 0xBA20D0, 0xBA2224, 0xBA2D40, 0xC8F908 | 0x476675; also 0x7A2316, 0x7A2329 | **crash** late in load (~200 s), VA 0x79C33D |
| 2 | 0x4 | Overlay | Overlay_System, 0xCF0AC0 | - | loads, -8 MB; terrain black, buildings untextured white, borders gone |
| 3 | 0x8 | OverlayDebug | - | - | =1 in every probe (the owner's setting); not probed alone |
| 4 | 0x10 | Decal | Decal_System, 0xBA37D4 | - | loads, -3; nothing visible changed (farm fields and rails still drawn) |
| 5 | 0x20 | City | City_System 0x2A11010, CityBuildingSystem 0xCD0D98 | 0x4765E9 | loads, -29; city meshes gone, banners work |
| 6 | 0x40 | Unit | Unit_System 0xD02200, UnitSimSystem 0x2D6F5F8 | 0x476222 (first system started) | **dies** 3 of 3 (~45-50 s), VA 0x9A3CF7 during app Startup |
| 7 | 0x80 | Forest | Forest_System 0xDA91C8, 0xBA395C | - | loads, -17; trees gone |
| 8 | 0x100 | River | RiverVisSystem 0x1A0C694 | `test eax,0x100` at 0x4761A1 | **crash** at end of load (~155 s), VA 0x8CD492 |
| 9 | 0x200 | LeaderHead | - | - | kept on, never probed |
| 10 | 0x400 | Audio | - | - | kept on, never probed |
| 11 | 0x800 | Combat | Combat_System 0x2D701A8 | - | loads, +12; not exercised. *Exercised 2026-11-15 (issue 0318) with Particle and Projectile on the product path: the combat plays over the server's picture as in stock; section 5 "The beta's gap alternative"*. **ON in the product since issue 0368 (2026-11-15, the owner's decision D147: the beta draws the EXE's own combat): out of `darkred.py` `D33_OFF` and `proxy_session_keys.h`; section 5 "Combat, Particle and Projectile on in the product"**. *2026-11-20 (ticket 4783, route S, MC #8543 / #8582): the key stays ON in config.ini (4783's planned "Combat and Projectile off again" is NOT done: no product default changes); while route S is armed the proxy clears this bit in the app's run-time mask `[cam_app]+0x940` (the combat simulation tests it per fight, `../05-graphics-composition/combat-choreography.md` 4.1) and sets it back as found at the disarm and the unload; `../05-graphics-composition/unit-combat-server.md` 16.3* |
| 12 | 0x1000 | Particle | Particle_System 0xDA9398 (+ 0xC8EB50 / 0xC8EFB8 for Particle/Projectile, split not decoded) | - | loads, -43; no particle effects (with Combat and Projectile on the product path, issue 0318: +15 to +51 MB for the three together on Tiny, section 5). **ON in the product since issue 0368 (2026-11-15, the owner's decision D147: the beta draws the EXE's own combat): out of `darkred.py` `D33_OFF` and `proxy_session_keys.h`; section 5 "Combat, Particle and Projectile on in the product"** |
| 13 | 0x2000 | Projectile | see Particle | - | loads, +1; not exercised. *Exercised 2026-11-15 (issue 0318): the archers' arrows drawn over the server's picture as in stock*. **ON in the product since issue 0368 (2026-11-15, the owner's decision D147: the beta draws the EXE's own combat): out of `darkred.py` `D33_OFF` and `proxy_session_keys.h`; section 5 "Combat, Particle and Projectile on in the product"** |
| 14 | 0x4000 | FOW | FOW_System / FogSystem 0xDAAA20 | - | loads, -6; nothing visible changed |
| 15 | 0x8000 | FluidFOW | FluidFOW_System 0xDA94D8 | per-game init call at 0x468EFD | loads, **-294**; map below the units flat deep red, unexplored grey; units, flags, banners, tooltip, minimap, top panel work |
| 16 | 0x10000 | YieldIcons | not gated in Startup/StartupGame | - | loads, +11; not exercised |
| 17 | 0x20000 | DistanceFog | DistanceFog_System 0xD90EC4 (Startup unconditional at 0x47602E) | none | loads, +7; nothing visible changed |
| 18 | 0x40000 | StrategicView | StrategicView 0x1A0ADA0 | - | loads, +9; not exercised |
| 19 | 0x80000 | Minimap | MiniMap_System 0x1A0C670 | - | loads, -9; minimap panel black. **Kept on under D33 (issue 0177, 2026-11-14, section 5 "The EXE's own minimap under D33"): the panel equals stock (99.1 % of pixels), +11 MB private, 30 AI turns clean. ON in the product since issue 0192 (2026-11-14): out of `darkred.py` `D33_OFF` and `proxy_session_keys.h`; on the workstation the panel equals stock pixel for pixel, +3.2 MB private at load, +0.2 MB after 30 turns, the ping blinks as in stock (section 5 "Minimap on in the product")** |
| 20 | 0x100000 | ColorKeyEdit | ColorKey 0xD90DE8 is ungated | - | only in the B-family combos |
| 21 | 0x200000 | Waves | Wave_System 0x1A0C6C8 | - | loads, +4; no clear difference |
| 22 | 0x400000 | Arrows | Arrow_System 0xB9C418 | `test eax,0x400000` at 0x476359 | **crash** at end of load (~155 s), VA 0x8CD492 (same as River) |
| 23 | 0x800000 | MovementPath | MovementPathSystem 0xDAB490, 0xC8EEFC | 0x4763DA | loads, +5; not exercised. **ON in the product since issue 3803 (2026-11-14, epic 0179, MC #1009: route (b) for the move-order path until increment 3): out of `darkred.py` `D33_OFF` and `proxy_session_keys.h`; the EXE draws the path's discs and chevrons (18 rigid overlay models, `arrows-and-movement-path.md` 2) over the joined picture; task 3801 measured +5-6 MB private in the EXE on Tiny (`research/05-graphics-composition/arrows-and-path-line.md` 7); section 5 "MovementPath on in the product"** |

Notes on the table:
- "All 24 keys probed" (memory note title) means 20 keys alone, OverlayDebug always on, ColorKeyEdit only in the
  B-family combinations, LeaderHead and Audio never switched off.
- Startup order at boot [C] (gate addresses): River 0x4761A1, Unit 0x476222, Arrows 0x476359, MovementPath
  0x4763DA, City 0x4765E9, Landmark 0x476675, Terrain 0x47696C. *Not the whole list (2026-11-14, issue 3516): 20 of the
  24 keys are gated in app Startup; full table in section 3, "Which keys app Startup gates".* `system_init.log` lists in order: DistanceFog,
  ColorKey, GameSplineDatabase, RiverVisSystem, Unit_System, Overlay_System, Arrow_System, FluidFOW_System,
  LandmarkSystem, MiniMap_System, TerrainSystem, LeaderHead_System, StrategicView, AudioSimSystem. With B4 the
  Overlay, FluidFOW, MiniMap and StrategicView lines are absent, and `DistanceFog_System::Startup` still runs (it
  is unconditional). City, Forest, Particle and MovementPath are not logged in `system_init.log` at all. [L]
- "(x at crash)" memory figures in PROBES.md are the frozen process on the DLL's crash dialog, not in-game numbers:
  Terrain 1,522, LandmarkSystem 1,868, River 1,974, Arrows 1,976, Unit ~445 at death.
- Unit "silent exit, no dump" in PROBES.md is corrected by `stubs/unit.md` (2026-11-10): all three runs left
  Steam minidumps in the game root (`civilizationv_dx11_403694_crash_2026_11_9T22_13_5C0.mdmp` etc., written by
  the EXE's own filter 0x42DEA0 via `SteamAPI_WriteMiniDump`). The process died ~43 s after spawn during app
  Startup, not at mod activation: the "Changing active DLC and Mods" line was the boot-time DLC activation.
- FluidFOW look: the 2026-11-09 morning note said "deep red tint"; the evening probe saw a flat deep red map with
  terrain not visible. The later full probe is the reference.

## 3. Two masks

| Mask | Where | Set by | Read by |
|---|---|---|---|
| Config mask | `[app+0x940]`, `app = *(0xCC8E74)` | the config/command-line parser; bit set = enabled | app Startup, per-game StartupGame, GameView Startup, shutdown/reinit, app shutdown gates |
| Per-view active-systems mask | `[GVS+0x1B4]`, `GVS = *(*(0xCC8E74)+0xC0)` (GameViewState) | GVS constructor 0x64A860 writes **0xFFFFFFFF** (0x64A8DC and 0x64AA81; `mov [esi+0x1b4],ebp` with ebp = -1); per-game startups then clear bits | per-frame world update 0x6FBA80, render passes 0x5893F0 (pass A) and 0x7492E0 (pass B), shadow pass 0x523CA0, reflection pass 0x5A0F80, terrain render 0x657070 (River sub-call) |

[C] for all of the above (`stubs/river_arrows.md`, `stubs/coupling.md`, `stubs/terrain.md`).

How GVS is found: `g_Civ5App` at 0xCC8E74 is the pointer PR #13379's verified signature
`LoadCvGameCoreDLL_SetEngineUserInterface` (DX11 0x47BA70: `8B 0D <&g_Civ5App>`) already resolves; GVS is the
0x8D0-byte object at `[g_Civ5App+0xC0]`, vtable 0xA20908 (`CAMERA.md`). `view_mask.py` rebases 0xCC8E74 on the live
module base and refuses any EXE whose TimeDateStamp is not 0x546CD0A8.

The per-bit pattern in the startups is `mask |= bit; if (!(cfg & bit)) mask &= ~bit; else Subsystem::StartupGame()`.
Terrain example at 0x468BCC: the code sets bit 1 in the view mask at GVS+0x1B4, tests bit 1 of the config mask at
`g_Civ5App+0x940`, and clears the view bit again when the config bit is clear. [C] (Instruction sequence replaced by
this description, 2026-11-18, D83 audit.)

### Which startup clears which bit [C]

| Function | Bits it gates (clears when disabled) |
|---|---|
| GVS::StartupGame 0x4686C0 (gates 0x468B1E-0x469434) | 0x80, 0x1, 0x2, 0x4, 0x800000, 0x4000, 0x8000, 0x40, 0x20, 0x1000, 0x2000, 0x40000, 0x80000, 0x200000, 0x200, 0x400 |
| GameView::Startup 0x623170 (gates from 0x6231CE) | 0x100000, 0x8000, 0x40, 0x2, 0x4, 0x8, 0x10, 0x20, 0x80, 0x200, 0x1800 (does `mask |= 0x1800` at 0x62344B but tests only 0x1000), 0x4000, 0x80000 |
| ShutdownGame 0x580C50 | re-derives 0x100 (River) from config at 0x580E1A; per-bit gates 0x580D04-0x58112D |

Bits never cleared by any per-game startup: **0x100 River, 0x400000 Arrows** (no gate anywhere for 0x400000;
the 45 `or [reg+0x1b4],...` sites were listed), **0x800 Combat** (forced on by the 0x1800 OR),
**0x10000 YieldIcons, 0x20000 DistanceFog** (nothing reads these two from `+0x1B4`). Unconditional calls in the
same functions: `0xD165E0.0x71CAF0` at 0x46949B (toggles a Terrain byte; harmless), DistanceFog Startup at
0x47602E, GridHeightmap creation at 0x476018.

### Which keys app Startup gates (issue 3516, 2026-11-14) [C]

Read from `llvm-objdump -d` of the DX11 EXE (TimeDateStamp 0x546CD0A8, the Ghidra input copy
`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`), VA 0x475F40-0x476C40; every gate tests the same local
copy of the config mask (`mov eax,[esp+0xa4]`, then `test`), and each branch calls the system's Startup with its static
object in ECX. **20 of the 24 keys are gated in app Startup, at boot, before any game DLL (or the proxy) is loaded**,
not the seven of the boot-order note in section 2 (which listed only the gates that matter for the crashes). So a
config-mask write made later (at the proxy's boot in the front end, decision D116 option A) cannot stop any of these 20
systems from starting; it can only skip their per-game start-up (GVS::StartupGame / GameView::Startup, table above).
That is why option A of D116 cannot keep FluidFOW's per-job-thread blocks (`allocations-and-memory-map.md` row
`FluidParticles.cpp` site 0x68F795 and section 5: 8 x 24 MB at app start-up, main menu -221 MB) [C for the gate; L for the
main-menu memory, 2026-11-09].

| Bit | Key | App Startup gate (test) | Startup called (object) | Per-game gate as well | Memory, single key in game (section 2, 2026-11-09) | Boot-time part known |
|---|---|---|---|---|---|---|
| 0x1 | Terrain | 0x476963 | 0x6B8370 (0xD165E0) | StartupGame | crash | - |
| 0x2 | LandmarkSystem | 0x476675 | 0x636590 / 0x62AE40 | both | crash | - |
| 0x4 | Overlay | 0x4762D7 | 0xCF0AC0's start-up (OverlayDebug bit passed in) | both | -8 | - |
| 0x8 | OverlayDebug | none (an argument of Overlay's start-up) | - | GameView::Startup | not probed alone | - |
| 0x10 | Decal | 0x476469 | 0x40CF10 (0xBA37D4) | GameView::Startup | -3 | - |
| 0x20 | City | 0x4765E9 | 0x5640A0 (0xCD0D98), then 0x2A11010 | both | -29 | - |
| 0x40 | Unit | 0x476222 | 0x405C70 (0xD02200), 0x6B2BC0 | both | dies in app Startup | - |
| 0x80 | Forest | 0x476720 (`test al,al; jns`) | 0x726050 (0xDA91C8), 0x469980 | both | -17 | - |
| 0x100 | River | 0x4761A1 | 0x638880 (0x1A0C694) | none | crash | - |
| 0x200 | LeaderHead | 0x476A11 | - | both | never probed | - |
| 0x400 | Audio | 0x476BC3 | 0x570100 (0xC957A0) | StartupGame | never probed | - |
| 0x800 | Combat | 0x476AA3 | - | forced on (0x1800) | +12 | - |
| 0x1000 | Particle | 0x4767AC | 0x542100 (0xDA9398), 0x67B360 | both | -43 | - |
| 0x2000 | Projectile | 0x4767DA (inside Particle's branch) | 0x744970 (0xC8EFB8) | StartupGame | +1 | - |
| 0x4000 | FOW | 0x476568 | 0x5BB6E0 (0xDAAA20) | both | -6 | - |
| 0x8000 | FluidFOW | 0x4764E7 | **0x44A570** (0xDA94D8): the 8 x 24 MB blocks | both (+ the grid at 0x468EFD) | -294 | **yes: -221 MB at the main menu** (192 MB blocks + overhead); the grid (~74 MB huge) is per game |
| 0x10000 | YieldIcons | none | - | none | +11 | - |
| 0x20000 | DistanceFog | none (Startup unconditional at 0x47602E) | - | none | +7 | - |
| 0x40000 | StrategicView | 0x476B42 | 0x73FDB0 (0x1A0ADA0) | StartupGame | +9 | - |
| 0x80000 | Minimap | 0x4768E2 | 0x692D80 (0x1A0C670) | both | -9 | - |
| 0x100000 | ColorKeyEdit | none | - | GameView::Startup | B-family only | - |
| 0x200000 | Waves | 0x476861 | 0x549C80 (0x1A0C6C8) | StartupGame | +4 | - |
| 0x400000 | Arrows | 0x476359 | 0x526850 (0xB9C418) | none | crash | - |
| 0x800000 | MovementPath | 0x4763DA | 0x595690 / 0x5B67E0 | StartupGame | +5 | - |

*FluidFOW's -294 was taken at FOWLevel 3 (the instances' setting); at FOWLevel 2 FluidFOW costs about 0 MB (3 MB, inside
the noise) and at FOWLevel 3 -283 MB private (two launches per arm, 32-bit pin, issue 3957, 2026-11-15:
`research/05-graphics-composition/fog-of-war.md` 8.3; added 2026-11-15).*
"Memory" is the single-key delta in game on the huge save (one run each, noise ~20 MB); only FluidFOW's split between
boot and game is known. The live comparison of D33 by config.ini against D33 at run time (A) and A' (config.ini with
FluidFOW only) is `research/06-tooling-and-workflows/release-driver.md` W3b.

**Clearing config bits after app Startup crashes the load [L, 2 of 2, laptop tb1, 2026-11-14].** The test started from a
stock config.ini (or one with only FluidFOW = 1) and cleared D33's keys other than River and Arrows (0x00BBF898) in
`[app+0x940]` at the proxy's boot in the front end. That boot is after app Startup, so those systems had started. The EXE
then died in the huge save's load with 0xC0000005 at **VA 0x60D80D**, inside 0x60D790: a world-to-grid transform
(thiscall) that reads `[ecx+0xA8]` with an ECX that was not a live object. So the config mask must not be changed for a
system that app Startup has already started. The culprit key is not known (not bisected; candidates Decal, Forest, FOW, Waves,
MovementPath, Particle, Projectile, Combat, Minimap). Details: release-driver.md W3b.3 and W3b.5.

Render pass selection [C]: `0x66CE00 test byte [ecx+0x1b8],0xf; jz pass B (0x7492E0) else pass A (0x5893F0)`;
`[GVS+0x1B8]` is copied from `[app+0x690]` at 0x4B011A when `[app+0x690] & 3`. Its meaning is unknown [I: an
alternate or stereo view mode]; every probe ran pass B. `coupling.md` calls 0x5893F0 "the benchmark variant" [I].

### Live-read values [L]

| Config (2026-11-10, huge save t350) | `[GVS+0x1B4]` |
|---|---|
| any config, right after the GVS ctor (app init) | 0xFFFFFFFF |
| B4 + LandmarkSystem, in game | **0xFF432F49**: still SET although disabled: Combat 0x800, Projectile 0x2000, YieldIcons 0x10000, DistanceFog 0x20000, OverlayDebug 0x8 |
| B4 + Landmark + River + Arrows, `view_mask.py clear 0x400100` right after the view exists | 0xFFFFFFFF -> **0xFFBFFEFF** at app init, **0xFF032E49** in game (the cleared River/Arrows bits held through the load) |
| 2026-11-10 (epic 1900, c5h-a): instance's own config (all systems on except the owner's OverlayDebug = 1 and ColorKeyEdit = 1), DLL 440878983 or 48d581df2 without marker; `tools/i0b/read_masks.py` by PID | config **0xFFEFFFF7**, view **0xFFEFFFF7** in game at t350-t355 (equal: those two bits are cleared by the startups) |
| 2026-11-10 (epic 1900): B4 + Landmark + River + Arrows, DLL `i0-view-mask` 48d581df2 with `crashlogs\viewmask.enabled` (`crashlogs\viewmask.log`) | config **0xFF000641** (on: Terrain, Unit, LeaderHead, Audio). At DLL load (front end, before the save loads) view 0xFFFFFFFF -> **0xFF000641**; after the load the startups had set **only Combat 0x800** again (view 0xFF000E41 at the first CvGame::update of t350), cleared again at once -> 0xFF000641, then unchanged at t351-t353 (instrument log and outside read agree) |

Discrepancy resolved 2026-11-10 (epic 1900) [L]: the static lists above put 0x8 (OverlayDebug) in GameView::Startup and
0x2000 (Projectile) in GVS::StartupGame, yet without the write the in-game mask had both set. With the bits cleared
before the load, the load sets back only 0x800 (the `mask |= 0x1800` at 0x62344B followed by a test of 0x1000 alone),
and Projectile, YieldIcons, DistanceFog and OverlayDebug stay clear: so the startups never set those four, they are
simply left at the constructor's 1 (*was: "the static gate lists may record an OR-only pattern"*). The same
GameViewState (one address) served the front end, the load and three turns: no re-creation by a load [L, one load
per process, four processes]. Q11 (do the disabled-but-set systems misbehave): see section 4, "Live result".

## 4. The one-write fix

STUBS.md: after app init, **AND `[GVS+0x1B4]` with the config mask `[app+0x940]`**. One heap data write, no
`.text` or IAT bytes, so it is CEG-safe. It covers River, Arrows and the still-set Combat / Projectile /
YieldIcons / DistanceFog / OverlayDebug bits at once. Verified live from outside the process for 0x400100
(`view_mask.py`); *corrected 2026-11-10: the in-DLL version is built and ran live, see "Live result" below (was: "the
in-DLL version is not built yet").*

### The in-DLL instrument `ViewMaskSync` (epic 1900, 2026-11-10)

Code [C]: Community-Patch-DLL fork branch `i0-view-mask`, commit **48d581df2** on `instance-aware-instruments`
440878983, files `CvGameCoreDLL_Expansion2/ViewMaskSync.{h,cpp}` plus one call in `DllGetGameContext`
(`ViewMaskSync::Install`) and one at the top of `CvGame::update` (`ViewMaskSync::OnGameUpdate`); self-contained, so the
hunk can move to a clean branch. Release DLL SHA-256 **9ee7ec66e46cdd951860c7b75874d3dfe4dfbdda19973a7842ad8152a7307cb8**
(17,457,664 B, "Release-5.4.8-23-g48d581df2 Clean"; PDB 14f89344...), pinned in `deps/i0-view-mask.lock`.
- Opt-in: marker `crashlogs\viewmask.enabled` in the install folder; log `crashlogs\viewmask.log`. Without the marker it
  does nothing and logs nothing. Marker content `keep=0x<hex>` (re-read every 2 s) exempts bits and sets back any it
  cleared; deleting the marker while running sets back everything it cleared except River/Arrows and stops (both
  paths not run live).
- Write: `view &= config | 0xFF000000 | keep` (the 24 known bits only; the config's top byte was 0xFF live anyway)
  with a compare-and-swap, at Install and whenever a bit the config clears is set again; logs before, after and a
  read-back, and one read-back line per new turn.
- Checks before any write: TimeDateStamp 0x546CD0A8; six code patterns (RVA 0x68BCC terrain gate with
  `test byte [ecx+940h],1`, 0x2570C5 river test, 0x761A1 / 0x76359 River / Arrows startup gates, 0x3495D6 Arrows
  render test, 0x24A8D0 the ctor's mask write) with the g_Civ5App address (RVA 0x8C8E74) checked inside two of them;
  the GameViewState vtable (RVA 0x620908); every pointer followed lies in committed readable (the GVS: writable)
  memory (VirtualQuery). Failures log one `off:` / `skip:` line and write nothing. [C] the patterns were read from
  the instance's EXE file; the code bytes on disk are plain (not CEG-encrypted) at these addresses.
- [L] The EXE ran at base **0x00F60000** in all four runs of 2026-11-10 (not 0x400000); everything is rebased on
  `GetModuleHandle(NULL)` (addresses.md says the same). *2026-11-11 (epic 3000, c5h-b): base **0x00740000** in all six
  runs; the base changes between boots, never hard-code it.*
- **`clear=0x<hex>` (added 2026-11-11, fork branch `i2-shell-test` b92373aeb, issue 3002) [C code, L run]:** bits
  cleared even though the config keeps them; it wins over the config and over `keep=` (`allowed = (config | 0xFF000000
  | keep) & ~clear`); re-read every 2 s like `keep=`; a bit taken out of `clear=` is set back if the config allows it
  (not run live). For D7 option c1 the marker holds `clear=0x1`: Terrain stays on in `config.ini` (the per-load bake
  fills the heightmap and TerrainSystem is started) but view bit 0 is cleared. GVS::StartupGame ORs bit 0 back
  (0x468BCC) on every load; the CvGame::update re-check clears it again. Live (2026-11-11, both games): the load set
  back **0x801** (Terrain + Combat); the first CvGame::update cleared both, logged at 00:39:20.6 for a load whose
  screen reported "ready" at 00:39:21 (huge) and at 00:59:43.4 for one ready at 00:59:43 (Tiny). **Not covered:** the
  frames between StartupGame (load stage 4) and that first update; they are load-screen frames, so the terrain pass
  may run under the load screen for those seconds [I: whether the world is rendered under the load screen was not
  checked]. Pinned in `deps/i2-shell-test.lock`. *Corrected 2026-11-13 (issue 3305): on the proxy route the window is
  closed by a watcher thread in the proxy; measured live on the huge save, the bit would stay set 34.6 s and was set
  at most 10.4 ms, and the translucent load screen shows the world drawn under it at its end
  (research/05-graphics-composition/proxy-route-compositor.md 4.3, 4.5).*
- **On the proxy route (2026-11-13, one-off 9040) [C, L]:** the x64 DLL runs in the server and cannot write the EXE's
  mask, so `ViewMaskSync` is ported into the x86 proxy (`proxy/src/proxy_shell.cpp`: boot = Install, the top of each
  `ICvGame1::Update` = the re-check; same marker, log and checks). Live on c5h-a with the huge save: the same masks as
  above (boot 0xFFFFFFFF -> 0xFF000662, load sets back 0x801, the first Update clears it), 30 AI turns, no crash
  (research/05-graphics-composition/proxy-route-compositor.md).

### Which 32-bit DLLs honour the marker (issue 0171, 2026-11-14) [C]

`ViewMaskSync` exists only on the test lineage `i0-view-mask` 48d581df2 -> `i2-shell-test` b92373aeb (adds `clear=`)
and its re-bases. It was **never** part of the instance-aware patch (`ia-7307aefdf-440878983.patch`, resolved for batch
2 as `ia-ee893bc18-440878983.patch`: the DLL-source part of 7307aefdf..440878983; 440878983 is the parent of
ViewMaskSync's commit) and never on
`i0-ship`. So **neither the plain pin `i0ship-26234371` nor the instance-aware pin `i0ship-26234371-ia` reads
`crashlogs\viewmask.enabled`** (0 occurrences of the string `viewmask` in either DLL and in the patch; 1 in the
i2-shell-test DLL); no build flag is involved. With D33 in `config.ini` on either pin and no proxy, nothing clears River
(0x100) or Arrows (0x400000) and the EXE crashes in the River render at VA 0x8CD492 at the first world frame (3601,
2026-11-13 23:43, c5h-c, view 0xFF472F67; earlier 9046 on `-ia` e589306e, section 5 "Side finding"). On the proxy route
the proxy's own port of ViewMaskSync writes the mask (bullet above), whatever DLL the EXE would have had.

| 32-bit DLL | Fork commit | ViewMaskSync | Use |
|---|---|---|---|
| `i0ship-26234371` (F92881A3...) | 26234371e | no | the shipped 32-bit pin |
| `i0ship-26234371-ia` (3C2F47DA...) | 26234371e + ia patch | no | test instances' default (`provision.py pinned_dll`) |
| `i2-shell-test` (638B5E21...) | b92373aeb on ccfd6dd4d | yes, `clear=` | old pins only |
| `i2-shell-test-bbfacca1` (FF648358...) | 5412d1ecf on bbfacca1a | yes, `clear=` | batch-1 pins (issue 3021) |
| **`i2-shell-test-26234371`** (see `deps/i2-shell-test.lock`) | **6f1172314** on 26234371e | yes, `clear=` | **D33 on the 32-bit in-process route at the current pins** |

`i2-shell-test-26234371` (fork branch, 2026-11-14, issue 0171): 26234371e + `--no-ff` merge of `i0-view-mask` 48d581df2
(64f55ee9b; three conflicts, SqliteLogger.cpp/.h against 0084 and `CvGame::update()` against 0116, resolved as 9045's ia
patch, `../03-dll-engine-boundary/upstream-fix-batch-1.md` 8b; ViewMaskSync's `OnGameUpdate` runs with the other
pollers, before 0116's `m_iCacheStamp++`; it touches no game state) + cherry-pick -x of b92373aeb (6f1172314). Its DLL
source equals `git archive 26234371e` + `ia-ee893bc18-440878983.patch` + the diff 440878983..48d581df2 + b92373aeb
[C, `diff -r` of the two trees: only the shell-gates lineage's non-DLL files differ (`.claude/`, `.mcp.json`,
`experiments/`, `.gitignore`, a comment in `CoreDefineChanges.sql` plus the define `MEMORY_DIAGNOSTICS_HEAP_WALK_INTERVAL`
of the mod database), as on every i2-shell-test branch]. So it is the `-ia` pin plus ViewMaskSync and ShellHelperLoader,
both opt-in by marker. MC decision (0171 thread #525, 2026-11-14 03:07): ViewMaskSync stays on this test branch; the
`-ia` pin and the instances' DLLs are not changed. A D33 run on the 32-bit in-process route installs this DLL; a run
without the marker behaves as `-ia` [I: same source apart from the two opt-in instruments]. Build: release, clang-cl
19.1.7, LTO, `<pins_dir>/dlls/i2-shell-test-26234371/`, SHA-256 **EB8D26C0...E932** (17,480,704 B,
"Release-5.4.8-64-g6f1172314 Clean"), pinned in `deps/i2-shell-test.lock`.

Live [L] (2026-11-14 03:14-03:20 EST, c5h-d, D33 by `darkred.py config --arm d33` + marker `clear=0x1`, save copy
`C5H-0171-TINY_0008` = `c5h-9028-tiny-t0008`, the save 3601 crashed on; no proxy, server or compositor; driver
`shell/evidence/0171/run_0171.sh`, outside reads `mask_watch.py`): GameViewState created with view 0xFFFFFFFF (cfg
0xFF040667); ViewMaskSync's install at DLL load cleared 0x00FBF999 -> **0xFF040666** (River and Arrows clear); the load
set back Combat (for 16.6 s) and Terrain (for 5.1 s); the first `CvGame::update` cleared 0x801 again; in game at turn 9
the view stayed 0xFF040666 for over a minute, world frames drawn, **no crash** (3601's crash at 0x8CD492 was on the same
save with `-ia`). Logs and acceptance: `shell/evidence/0171/`.

### Live result (epic 1900, 2026-11-10 07:53-08:09 EST, instance c5h-a) [L]

Save `C5H-VP8P-HUGE_0350` (huge, turn 350), CP/VP/EUI, import route with the 1118 guard, same DLL 48d581df2 in both
configurations, driver `tools/i0b/run_1900.py checklist` (in game, outside mask read, combat-sim listeners, camera
pan by `UI.LookAt` over four plots plus posted wheel in/out, then `tools/harness/checklist.py --turns 3`: 3 AI turns
350 -> 353 with the 1116 handler, city screen, tech tree, diplomacy overview, leader screen, save, unit select, hex
pick), stop by `stop_readiness` at turn 353. Evidence `tools/i0b/evidence/{allon,mask,mask2}/`, comparison
`tools/i0b/evidence/memory-compare.json`, instrument log `tools/i0b/evidence/viewmask.log`.

| | all on (no marker, instance config) | B4 + Landmark + River + Arrows + marker |
|---|---|---|
| masks in game | cfg = view = 0xFFEFFFF7 | cfg = view = 0xFF000641 (t350-t353) |
| launch -> in game | 2:44 | 1:56 |
| AI turns 350/351/352 (s) | 39.6 / 34.5 / 33.5 | 34.2 / 28.2 / 27.0 |
| private / WS / committed MB at t350 in game | 2,129 / 1,831 / 2,458 | **1,636 / 1,431 / 1,982 (-493 / -400 / -476)** |
| same at t353 before stop | 2,181 / 1,919 / 2,508 | **1,683 / 1,485 / 2,025 (-498 / -434 / -483)** |
| peak private MB | 2,290 | 1,761 (-529) |
| largest free block at t350 / lowest | 1,144 / 902 | 1,613 / 1,419 (+470 / +517) |
| units alive t350 -> t353 (all players) | 845 -> 834 | 845 -> 834 (same AI combat outcome) |
| checklist | pass, 11/11 | 10/11: `hex_pick` third point stayed on the second hex once; a second run of the same configuration (`mask2`, 1 AI turn) passed 11/11 and a 27-pick probe (3 points x 3 repeats x waits 0.7 / 1.5 / 3.0 s) gave the right three hexes every time: a one-off flake |

Answer to Q11: with the view bits of Combat (0x800), Projectile (0x2000), YieldIcons (0x10000) and DistanceFog
(0x20000) cleared to match the config, nothing crashed or stalled: loads, 3 AI turns with combat (11 units lost, the
same as all-on), city screen, camera pan and zoom, leader, tech and diplomacy screens, save and picking all worked;
River and Arrows disabled load with no crash. Not covered: this save runs **quick combat** (`GAMEOPTION_QUICK_COMBAT`
true; 0 `RunCombatSim` / `EndCombatSim` events in both configurations), so the combat visualiser and projectiles were
never asked to play; a game without quick combat, a human attack, nukes/air strikes and a resolution change (view
re-creation) are untested. *(2026-11-15, queue ticket 0316: a human melee and ranged attack with Quick Combat off and on ran on the D33 product path (Combat, Projectile, Particle off): each resolved within 0.13 s, no unit busy, the turn ended in 6 s [L]; `research/03-dll-engine-boundary/combat-on-the-product-path.md`. Nukes, air strikes and city combat are still untested.)* Memory: the instrumented-baseline ceiling of section 5 (-497 MB) is reproduced (-493 MB at
t350) by the config plus the write; how much of it the write itself adds was not measured (this config without the
write crashes on River/Arrows).

The River/Arrows-only form from `stubs/river_arrows.md` (load-bearing; signatures there must be verified first):

```c++
// base = (BYTE*)GetModuleHandle(NULL); verify TimeDateStamp 0x546CD0A8 and the signatures below first
BYTE*  app  = *(BYTE**)(base + (0xcc8e74 - 0x400000));
BYTE*  view = app ? *(BYTE**)(app + 0xc0) : NULL;
DWORD  cfg  = *(DWORD*)(app + 0x940);                       // DisableSystems mask (bit set = enabled)
if (view) *(DWORD*)(view + 0x1b4) &= cfg | ~0x00400100u;   // River 0x100, Arrows 0x400000 (+0x800 Combat?)
```

Sites to verify before the write (DX11 VA) [C]; the product checks each by a byte pattern, held in
`proxy/src/proxy_shell.cpp` (`CODE_PATTERNS`, as RVAs) and `shell/tools/view_mask.py` (signature bytes replaced by
these descriptions, 2026-11-18, D83 audit):
- 0x6570C5: terrain render reads `g_Civ5App` (0xCC8E74), takes the GVS at +0xC0 and tests the River bit 0x100 of the
  view mask at GVS+0x1B4.
- 0x7495D6: render pass B tests the Arrows bit 0x400000 of the view mask and then loads the Arrows system (0xB9C418).
- 0x4761A1: app Startup tests the River config bit 0x100 and, when set, loads the River system (0x1A0C694).
- 0x476359: app Startup tests the Arrows config bit 0x400000 and, when set, loads the Arrows system (0xB9C418).

DX9 and Tablet addresses for this patch were not derived.

Timing:
- Proposed apply points: where `EngineQueueGuard::Install` runs (`CvDllDatabaseUtility::CacheGameDatabaseData`)
  and again at each game load (`CvGame::init` / load path). Idempotent.
- River/Arrows bits are only set by the GVS ctor, so one write after app init holds for the process (`view_mask.py`
  docstring; confirmed for one load live). Bits that a per-game startup re-derives (e.g. Terrain bit 0 at 0x468BCC
  for the Terrain "c1" option) must be re-applied after **each** StartupGame.
- GVS lifetime, sources disagree: `stubs/river_arrows.md` and `view_mask.py` say the GVS is built once in app init
  (0x4AF630, called from 0x422EA5); `CAMERA.md` says "GVS is created per game (Civ5App.cpp line 0xF1F / 0xF3E)"
  and the allocation-site list has two 0x8D0-byte `Civ5App.cpp` sites (0x4AFAAC line 0xF1F, 0x4AFFA9 line 0xF3E).
  The live run supports "no re-creation during one load"; re-applying per load covers both readings.
- Arrows residual: pass A (0x5893F0, 0x58961F) gates the arrow render on the **Overlay** bit (`test byte
  [esi+0x1b4],bl`, bl = 4), so with Overlay on and pass A active, Arrows=1 can still crash. Fix: a zeroed 0x2080-byte
  block at `[0xB9C418+0x74]` (~1 h to confirm begin/end pass 0x8CD6A0 / 0x8CDB20 do not touch it). Irrelevant in
  B4 (Overlay off). [C] code, [I] when pass A runs.

## 5. Combined configurations

### 2026-11-09 probes (plain Release 5.4.8 DLL, PROBES.md)

| Probe | Keys set to 1 | Result | Private (WS) MB | Delta |
|---|---|---|---|---|
| A, world off, UI kept | Terrain LandmarkSystem Decal City Unit Forest River FOW FluidFOW DistanceFog Waves YieldIcons Arrows MovementPath Particle Combat Projectile | crash ~100 s, file offset 0x2b4f3c (Terrain) | (1,163 at crash) | n/a |
| B, all but Audio and LeaderHead | A + Overlay OverlayDebug StrategicView Minimap ColorKeyEdit | crash ~105 s, Terrain site | (1,160 at crash) | n/a |
| A2 = A minus Terrain | 16 keys | crash ~115 s, VA 0x8CD492 (River/Arrows) | (1,450 at crash) | n/a |
| B2 = B minus Terrain | 21 keys | crash ~120 s, VA 0x8CD492 | (1,441 at crash) | n/a |
| A3 = A minus Terrain, LandmarkSystem, River, Arrows | 13 keys | crash ~135 s, VA 0x9A0814 (file offset 0x59fc14), worker thread, Granny pose cache | (1,499 at crash) | n/a |
| B3 = B minus the same four | 18 keys | crash, VA 0x9A0814 | (1,508 at crash) | n/a |
| **A4** = A3 minus Unit | Decal City Forest FOW FluidFOW DistanceFog Waves YieldIcons MovementPath Particle Combat Projectile | **loads and runs** | **1,561 (1,364)** | **-424** |
| **B4** = B3 minus Unit | A4 + Overlay OverlayDebug StrategicView Minimap ColorKeyEdit | **loads and runs**; map black with dark red patches, minimap black, UI works | **1,545 (1,349)** | **-440** |
| A5 = A4 minus FluidFOW | 11 keys | loads and runs; map normal except missing cities and forests | 1,869 (1,641) | -116 |

### 2026-11-09 Gate 1 re-run (instrumented DLL, GATES.md)

The experiment DLL (5.4.8 + MemoryImports/MemoryHooks + EngineAllocTrace) costs ~140 MB private against the plain
DLL, so only deltas compare.

| Moment | All on, private | B4 private | Delta | All on, largest free | B4 largest free | Delta |
|---|---|---|---|---|---|---|
| t350 after load | 2,122 | 1,676 | -446 | 1,054 | 1,500 | +446 |
| t351 | 2,167 | 1,721 | -446 | 995 | 1,436 | +441 |
| t353 | 2,211 | 1,757 | -454 | 901 | 1,281 | +380 |

Working set at load 1,848 -> 1,476 MB. Free below 2 GB ~26-30 MB in both: the saving lands above 2 GB. Growth over
three AI turns +89 MB (all on) vs +81 MB (B4). B4 functional checklist (city screen, unit select + LookAt,
minimap/flag/banner contexts, hex picking by posted mouse moves, three AI turns with combat and diplomacy, wonder
popup): all pass. Main-menu memory with this DLL: 1,615 MB private.

### 2026-11-10 live configs (STUBS.md, no-focus mode, same instrumented baseline)

| Config | Loads | 3 AI turns + checklist | Private after load | Largest free | t353 private |
|---|---|---|---|---|---|
| B4 (reference, 11-09) | yes | pass | 1,676 | 1,500 | 1,757 |
| B4 + LandmarkSystem (City off) | yes, no stub | pass | 1,623 (-53) | 1,542 | 1,704 (-53) |
| B4 + Landmark + River + Arrows, view bits 0x400100 cleared from outside (`view_mask.py clear 0x400100 --wait 240`) | yes (both used to crash at VA 0x8CD492) | pass | 1,625 | 1,543 | 1,705 |
| B4 with City kept on + Unit (config-only "unit rendering off, sim on") | yes | pass (units 79 -> 75: AI combat resolved without the unit renderer) | 1,688 (+12 vs B4; City back on costs ~29) | 1,539 | 1,728 |

B4 + Landmark + River + Arrows is -497 MB against the instrumented all-on 2,122. B4 + Landmark loads in 2:08
vs ~2:47 (no farm/route decal layout; corrected 2026-11-10: same-night unsampled A/B is 148.2 -> 117.4 s, -30.8 s,
the ~2:47 reference had EngineAllocTrace on; `load-sequence.md` section 4), its dark red patches are gone (they were the Landmark decals) and
`queueguard.log` has no drop line for that load.

**Config-only ceiling: ~440-450 MB on the huge map.** FluidFOW is two thirds of it (-294 MB); Particle -43,
City -29, Forest -17; the rest is within noise.

### 2026-11-11 the D33 shell configuration live: the road question (epic 3000, issue 3003) [L]

*Corrected 2026-11-13 (bug 3010, option A): StrategicView is no longer in D33's off list (masks now 0xFF040663 /
0xFF040662); the run below used the old list.* Configuration (decision D33, D7 option c1): `[DisableSystems]` = 1 for Overlay, OverlayDebug, Decal, Forest, River,
FOW, FluidFOW, DistanceFog, Waves, YieldIcons, Arrows, MovementPath, Particle, Combat, Projectile, StrategicView,
Minimap, ColorKeyEdit; on: Terrain, LandmarkSystem, City, Unit, LeaderHead, Audio; marker `crashlogs\viewmask.enabled`
= `clear=0x1`. Masks in game: config **0xFF000663**, view **0xFF000662** (Terrain bit cleared; River and Arrows clear,
no crash). "Off" arm: the instance's own config (all on; OverlayDebug and ColorKeyEdit = 1), no marker, masks
0xFFEFFFF7 / 0xFFEFFFF7. Same DLL in both arms: `i2-shell-test` b92373aeb (SHA-256 638B5E21...C83B4). Instance
c5h-b (Goldberg), CP/VP/EUI 5.4.8, windowed 2554 x 1411, cloaked. Same camera in both arms by `UI.LookAt` and
posted wheel ticks; the view matrices of the two arms differ by at most 0.08 world units, projections identical
(`shell/evidence/3000/road/camera_same_both_arms.csv`). Games: huge save `C5H-VP8P-HUGE_0350` t350 (Memphis, 6 world
wonders, railroads, roads, farms, mines, pastures, a landmark); Tiny `C5H-3000-TINY-SETUP` t18 (a copy of an epic-run
Tiny save with routes, farms, mines, a plantation, resources and the Pyramids added by `shell/tools/i2_tiny_setup.lua`,
saved and loaded again). Three views per game: default zoom on the city, zoomed in on the city, zoomed in on a
railroad plot. Driver `shell/tools/i2_capture.py`; full-size shots `D:/c5-64bit-work/3000/road/`.

| Item | Huge t350 | Tiny t18 |
|---|---|---|
| Road decals | **no**, not drawn | **no** |
| Railroad decals | **no** | **no** |
| Farm fields (the ground patchwork) | **no** | **no** |
| Landmark ground pattern (the maze decal around a landmark) | no | (none on the map) |
| Improvement models (farm houses, mines, pasture fences, towns, forts, landmark obelisk) | **yes**, drawn, untextured dark red | yes, dark red |
| Resource models (horses, sheep, bison, whales) | yes, dark red | yes, dark red (whales) |
| World wonder models | yes, dark red (Memphis's wonders) | yes, dark red (Pyramids) |
| City buildings and walls | yes, dark red | yes, dark red |
| Units | yes, normal textures | yes, normal |
| Models on the ground | **yes**: a 50 % blend of the two arms at the same camera puts every model of the D33 arm exactly over its stock counterpart (`huge_v2_blend_models_on_ground.jpg`); heights come from the bake, which still runs | yes, same |
| Terrain, water, rivers, trees, fog clouds, borders | absent (grey clear colour) | absent |

**Answer: roads, railroads and farm fields vanish with the terrain pass off; the landmark system's models stay and sit
on the ground.** So in D33 the server must draw roads, railroads and farm fields in increment 2 (decision-log D33 note
(3): "if they vanish, roads and farm fields are drawn by the server"). [L, two games, one run per arm.] Why [I]: the
route and farm decals are drawn by TerrainDecalSystem (TerrainSystem+0) as part of the terrain pass, which the cleared
bit 0 skips (`stubs/terrain.md` section 2). The dark red: *corrected 2026-11-11 by issue 3009 (was: "the
landmark/city model shader without a fog-of-war input (FluidFOW and FOW off) [I]")*: the building shader's FOW Mask
input, which only FluidFOW creates; see "Why the models are dark red" below. Evidence (small copies):
`shell/evidence/3000/road/*_stock_top_d33_bottom.jpg`, `huge_v3_rail_crop_stock_top_d33_bottom.jpg`.

Memory and load, one launch per arm, same DLL, measured 2026-11-11 00:31-01:00 EST (`memory_load.csv`; sample right
after the load, in game, before any turn; load = launch -> the load screen's Continue button shown, then -> in game
after the scripted Continue):

| | huge all on | huge D33 | delta | Tiny all on | Tiny D33 | delta |
|---|---|---|---|---|---|---|
| private MB | 2,074 | 1,648 | **-426** | 1,790 | 1,432 | -359 |
| working set MB | 1,787 | 1,423 | -364 | 1,404 | 1,102 | -302 |
| committed MB | 2,402 | 1,994 | -409 | 2,120 | 1,778 | -342 |
| largest free block MB | 1,114 | 1,538 | +424 | 1,444 | 1,693 | +249 |
| launch -> load screen ready | 168.0 s | 154.6 s | -13.4 s | 102.8 s | 148.0 s | +45 s (one observation; see below) |
| launch -> in game | 191.2 s | 166.1 s | -25 s (*not reproduced by verifier 9031, 2026-11-11: -14.0 s loaded and -12.9 s under the quiet lock, the same as the -13 s to the ready screen; memory -428.5 / -457.9 MB confirmed; shell-foundations-verification.md section 5*) | n/a | n/a | |

Against section 4 (epic 1900, B4 + Landmark + River + Arrows with the write, City off): -493 MB private there,
-426 here; the D33 configuration keeps City on (about +29, section 2) and is otherwise the same set; one run each, noise
~20 MB [I]. Load: 1900 measured 2:44 -> 1:56 (-48 s) with LandmarkSystem off; with it on (D33) the saving is -13 s
to the ready screen, consistent with the farm-field and route layout being the load-time cost (section 5 above,
`load-sequence.md` section 4) [I]. The Tiny D33 load (+45 s) was one launch right after another team's 30-minute
quiet-machine hold ended; machine load was not recorded, so it is not a result. Not covered: AI turns, combat,
picking and a second load in the same process in this configuration (the 1900 checklist covered a City-off variant).

### Why the models are dark red in D33, and what fixes it (issue 3009, 2026-11-11)

**Answer: the building shader replaces the lit model colour by the fog-of-war base layer wherever the FOW Mask reads 0,
and in D33 the FOW Mask does not exist (FluidFOW off) and its projection is not set up (Overlay off). Keeping FluidFOW
and Overlay on gives normal textures [L]; either one alone does not [L].** Units are normal because the unit shaders
have no fog inputs [C].

Static [C] (shader blobs dumped with `render/tools/shaderdump --out` from c5h-b's EXE; slot names
`research/05-graphics-composition/evidence/2900/2903/slot_names.txt`; copies of the three blobs in
`shell/evidence/3000/darkred/*.asm`):
- `BuildingShader_CODE` / `_Instanced` / `_Skinned_CODE` pass 0 PS `0x061a56e8` and `_Instanced_Opaque` PS `0x061a7d00`
  (the materials whose draw-object builders `0x7b2150` and `0x75a220` read the handles) end with:
  `c = ColorCorrection(lit)`; `base = t8 FOWBaseLayer(v1.zw)`; `m = t7 FOWMask(v3.zw).xz`;
  **`c = lerp(base, c, m.x)`**; `c = lerp(c, t9 FOWColorKey(c), m.x * (1 - m.y))`; then distance fog (t5, CB1[17..19]).
  So with `m.x = 0` the output is the base layer alone: no diffuse, no lighting.
- The draw object copies the handles at build time: `0x7b267c` `movzx eax, word [0xDA12AC]` -> t7 (sampler 0),
  `0x7b2695` `[0xDA12B0]` -> t8 (sampler 0), `[0xDA12B4]` -> t5, `[0xD90E88]` -> t6, `[0xD90E60]` -> t9 (sampler 5).
  `0xDA12AC` (FOW Mask target) and `0xDA12B0` (cloud base layer) are written only by FluidFOW_System start-up
  `0x44a731` / `0x44ac35` / `0x44af3e` / `0x44b14b`, and by the Nexus terrain editor's set-up `0x5619c0` (called from
  the editor constructor `0x57DFA0`), which loads **`assets\terrain\editor\White.dds` into `0xDA12AC`** (`0x561fe2`; and
  `Blank.dds` into `0xDA12B8` / `0xDA12BC`): the engine's own "no fog" stand-in. `white.dds` is in
  `Resource/DX9/TerrainTextures.fpk` (16 x 16 BGRA, `evidence/2900/2901/terrain_side_files.csv`).
- Handle 0 is not a null view: `0x8bd160` turns handle h into the view at `0x414c348 + 12h`, so handle 0 binds whatever
  texture sits in slot 0; that texture is the dark red [I: slot 0's contents not dumped].
- The building VS (`0x0619be88`) computes the mask uv `v3.zw` from world xy through CB1 rows 8-11 of shared buffer
  id 16 (0x160 bytes); with Overlay off this uv evidently does not hit the mask's lit texels (R1 below) [I: the
  writer of rows 8-11 was not traced; the overlay projection `0xDA1690` of Overlay_System `0x51b450` is the candidate].
  *Settled 2026-11-14 (epic 0168, `fog-of-war.md` 2.8 [L] and 3.10 [C]; pointer by issue 3957): rows 8-11 **are
  OverlayProj[v]** (`0xDA1690 + 0x40 v`), written by the model-pass frame function `0x4FB320` / `0x674AD0` (rows 0-3 a
  matrix, 4-7 g_View, 8-11 OverlayProj, 12 FOWAnim, 13-16 the inverse view); the captured VS cb1 rows 8-11 equal
  TerrainFrame 0x040 exactly at two cameras. So the models sample the mask in the overlay window.*
- `ForestShader` PS `0x0676d388`: `m = t6 FOWMask.xz`; **`discard` if `m.x == 0`**; then the same lerp with t7 base layer
  and the t8 LUT. So trees are discarded outright wherever the mask reads 0.
- FluidFOW's draw calls in the frame function `0x5893f0` (`0x5b57f0` at `0x5894f6`, `0x49c940` at `0x5895d1`, `0x602b10`
  at `0x5896c0`) test only view bit 15 (0x8000), not the Terrain bit; FOW_System's render `0x55b8b0` tests bit 14.

Live [L] (2026-11-11 02:45-03:33 EST, c5h-b, DLL `i2-shell-test` b92373aeb SHA-256 638B5E21...C83B4, CP/VP/EUI, huge
`C5H-VP8P-HUGE_0350` t350, the 3003 camera plan plus a forest view at plot (98, 34); one launch per arm; driver
`shell/tools/darkred.py` + `shell/tools/i2_capture.py`; handles read by ReadProcessMemory; `runs.csv`):

| Run (D33 plus) | Masks config / view | `0xDA12AC` / `0xDA12B0` | Models | Private MB after load |
|---|---|---|---|---|
| R0 nothing (D33) | 0xFF000663 / 0xFF000662 | 0 / 0 | dark red, untextured (reproduces 3003) | 1,653 |
| R1 FluidFOW on | 0xFF008663 / 0xFF008662 | 0x247 / 0x248 | **untextured pale grey-white**: the cloud base layer, i.e. `m.x` still 0 | 1,971 (+318) |
| R3 Overlay on | 0xFF000667 / 0xFF000666 | 0 / 0 | dark red | 1,636 (-17, noise) |
| R2 FluidFOW + Overlay on | 0xFF008667 / 0xFF008666 | 0x258 / 0x259 | **normal textures** (`huge_v2_city_crop_r0_r1_r3_r2.jpg`) | 1,957 (+304) |

The colour change R0 -> R1 (red -> cloud white) with the mask still 0 is the shader's `lerp(base, c, m.x)` seen live.
Not run: FOW on, DistanceFog on, the Terrain view bit left on (the static result makes them unnecessary: FOW and the
Terrain bit gate no write of `0xDA12AC` and no FluidFOW draw; the 11-09 single-key probes saw "Overlay off: buildings
untextured white" and "FluidFOW off: deep red", the same two halves).

**Cost and the cheapest fix.** FluidFOW + Overlay costs about +304 MB private on the huge save (one run each; FluidFOW
is the 8 x 24 MB per-job-thread particle blocks plus the per-game grid, `allocations-and-memory-map.md` section 5;
Overlay ~8 MB), i.e. it gives back about three quarters of D33's -426 MB, so it is not the fix. The cheapest fix [I,
not built]: a white FOW Mask stand-in, as the terrain editor does. With a constant white t7, `m.x = 1` and
`m.y = 1` give `c` unchanged and no colour-key or base-layer term whatever the uv, so neither FluidFOW nor Overlay is
needed and the memory cost is one 16 x 16 texture. It needs: the 16-bit handle of a loaded white texture (load
`assets\terrain\editor\White.dds` with the EXE's own loader `0x8c6540`, as `0x561fe2` does, or find an already
loaded white texture) written into `0xDA12AC` before the building draw objects are built (the builders copy the handle,
`0x7b267c`), e.g. from the DLL's `ViewMaskSync::Install` (a heap/.data write, CEG-safe). Trees would then draw too
(the forest PS discards only on `m.x == 0`). *Corrected 2026-11-11 by the live half of issue 3011 (below): the stand-in
textures improvement, resource and other landmark-system models, but city buildings and world wonders stay dark red and
trees still do not draw; the "trees would draw" prediction was wrong for the global alone. Corrected again 2026-11-11
12:55 (3011 second half): with the pool pass (`whitemask=2`) cities, wonders and trees are right as well [L].* Side effect: models in revealed-but-not-visible plots lose the fog
tint (the server's fog would have to cover them). Estimate 2-4 h with a live check. Calling `0x8c6540` from the DLL is
calling EXE code, not patching it; whether that is acceptable is the coordinator's call.

**The white mask stand-in, static half (issue 3011, decision D56, 2026-11-11 04:45-04:56 EST, Ghidra 12.1.4 headless
read-only on a copy of the `civ5` project, `D:/c5-64bit-work/3000/w6/`; on-disk bytes checked in the Ghidra input copy
of the DX11 EXE, TimeDateStamp 0x546CD0A8).**
- **No already-loaded white texture to reuse [C for the named loads, I for the rest].** All 30 call sites of the
  loader `0x8C6540` (and its thunk `0x8C6640`) were listed; the only white file it loads is `White.dds` at `0x561fe2`.
  That function (`0x5619c0`, allocator file `.\App\Engine\TerrainEditorSystem.cpp`; it also starts the terrain decal
  system `0x714b80` and the ColorKey tables) has no direct caller in the project (indirect call), and the live read of
  run R0 (`0xDA12AC` = 0 in game) shows it does not run in a normal game. Other candidates: `blank_texture.dds` (4 x 4
  DXT5 in `DecalTextures.fpk`, loaded with the decal system, off in D33, colour not read), FluidFOW's own fallback base
  layer (32 x 32, `memset 0`, black), texture slot 0 (the dark red itself). Handles of model textures loaded by other
  paths are not in globals. So the stand-in needs one loader call; a data write alone is not enough.
- **The loader `0x8C6540` [C]:** `__thiscall`, ECX = `[0x03010570]` (every caller loads it from that global), one
  stack argument `const char*` path (relative, looked up through the EXE's file system `0x404790` -> `0x41d600` ->
  `0x817b00`, so packed `.fpk` files are found), `ret 4`, EAX = texture handle (index < 0x3000) or 0 on failure (file
  not found: returns 0 before any allocation; DDS not parsed: returns 0, no handle). It builds a ~66 KB file object on
  the stack, parses the DDS (`0x8c9500`), takes a **new** handle from the allocator `0x8c5cb0` on the pool object
  `0x413e2c0` (spin lock by `InterlockedCompareExchange`, so the handle allocation is thread-safe; no cache by name:
  every call makes a new texture) and creates the texture and its view through the D3D11 device `[0x0301057C]`
  (`0x8bbd60`: device vtable +0x14 CreateTexture2D, +0x1c CreateShaderResourceView), storing the view at
  `0x414c348 + 12h` (the table `0x8bd160` binds from). Ownership: the caller (the handle's use count is incremented
  once at allocation); nothing releases it unless the caller does. CEG: the function and its direct callees decompile
  to plain engine code with no call into Steam/CEG code; the on-disk bytes are plain at `0x8c6540`, `0x8c65fb`,
  `0x8c6628`, `0x561fcb` and `0x7b267c` [C]. The DLL re-checks those byte patterns in memory before the call.
- **Thread and moment [C + I]:** `DllGetGameContext` runs once per process, on the EXE's main thread, at the front end
  after the mods were activated (`load-sequence.md` section 6 [L]); the device and the file system exist then (the
  front end is drawn and its textures loaded) [I], and the main thread is not inside a frame while it runs the DLL
  call [I]. `CvGame::update` runs on the game-core thread (start VA `0x82A360`, RVA 0x42A360; corrected 2026-11-15, issue 0335: written `0x42A360` before, an RVA; `game-core-thread.md`), which must not call the loader (file system and
  texture creation race with the main thread) [I]; it only reads and, if needed, rewrites the cached handle.
- **Timing [C]:** the building draw objects copy the handle when they are built (`0x7b2150`, called from `0x604fd0` and
  `0x402c40`, model set-up during a game load), and the instanced ones at `0x75a612` (`0x75a220`); FluidFOW would have
  written `0xDA12AC` in its start-up `0x44a570` (`0x44a731`, a "FOW Mask" render target). A write at
  `DllGetGameContext` comes before the first game load, so every draw object built during a game load copies it.
  Hook point chosen: `ViewMaskSync::Install` (already called from `DllGetGameContext`). *Corrected 2026-11-11 (3011,
  second half): `0x44a570` is called at `0x4764f3` inside app Startup (0x475F40-0x476C00, gate bit 0x8000), i.e. at
  boot before the DLL is loaded, not per game (replaced: "per-game start-up ... i.e. inside the load"); and the draw
  objects built in app Startup (cities, wonders, trees) are exactly the ones a write at `DllGetGameContext` misses.*
- **Second load in one process [C, from cross-references; I: computed writes would not show]:** the only writers of
  `0xDA12AC` are `0x44a731` (FluidFOW start-up, not run with FluidFOW off) and `0x561fe2` (editor); nothing resets it
  to 0, so the handle stays for the process and the next game's draw objects copy it again. The texture is loaded once
  per process (the DLL is loaded once per process), so there is no leak per load. Not established: whether a
  texture-manager purge between games frees all handles (then the slot could be reused by another texture); the DLL
  logs the handle's view pointer every turn and flags a change. Live check pending.
- **The base layer `0xDA12B0` needs no value [C]:** `BuildingShader_CODE` PS line 93 lerps from the base
  colour to `c` by the mask's `m.x`, so it gives `c` exactly for `m.x = 1` (shader line replaced by this description,
  2026-11-18, D83 audit); line 94 `m.x * (1 - m.y)` = 0 for `m.y` (t7.z) = 1, so the colour key t9 is
  not applied either. Distance fog (t5 `0xDA12B4`, CB1) is unchanged from run R2, which had normal textures with
  DistanceFog off [L]. The forest PS: `discard` where t6.x == 0 (line 13-14 of the dump) cannot fire with white; its
  lerp then keeps the lit colour.
- **Side effects [I]:** every other material that binds `0xDA12AC` (13 more reading functions, among them FluidFOW's draw
  `0x49c940`; the others not identified one by one) sees a fully visible mask: no fog tint or discard anywhere, including plots never revealed; the
  server's fog has to cover models and trees there.
  *What epic 0168 did (pointer 2026-11-14 by issue 3957; `fog-of-war.md` 5.4, MC #1131 decision 3 (i)): the server
  draws the fog of the ground (mask, cloud base, TerrainFOW LUT, the cloud planes) and **does not touch the white mask**
  (nothing writes `0xDA12AC`, the white texture, the draw objects or the config mask; FluidFOW stays off). The models
  the EXE draws over the server's image keep the white mask: models on seen-not-visible plots stay fully lit and
  models near unexplored plots are not covered by cloud edges (stock draws its cloud planes after the models), intended
  differences of this epic; trees stay off in D33. The DLL reveals no unit, city or improvement on an unexplored plot,
  so no model stands under full cloud [L, 3951 at two cameras]. Handing the server's mask to the EXE's model draws
  (decision 3 (ii), sized 3-5 h) is a follow-up. The white-mask log lines in the fog-on and fog-off arms: 3957 phase 2.*
  *Pointer 2026-11-16 (ticket 0339, `fog-of-war.md` 9): the follow-up is built and on by default (`model_fow=1`): the
  composite puts the server's mask behind the white handle's view-table entry, the proxy loads the cloud base
  (`FogCloudTile_dif_02.dds`) into `0xDA12B0` and the base-layer slots, the composite writes FOWAnim; models on
  seen-not-visible plots are tinted as stock [L]. The white mask itself (steps 1-5 below) is unchanged and still needed.*
- **Code:** fork branch `i2-white-mask` commit **92393b750** (on `i2-shell-test` b92373aeb), `ViewMaskSync.{h,cpp}`;
  opt-in by `whitemask=1` in `crashlogs\viewmask.enabled`; log lines start `whitemask`. Not built, not run (2800 priority
  window). Test plan: tracker `3011-white-fow-mask.md`.

**The white mask stand-in, live half (issue 3011, 2026-11-11 09:14-09:30 EST) [L].** Instance c5h-b (Goldberg), DLL
`i2-white-mask` 92393b750 (SHA-256 B3DB8815...9CD51865, `deps/i2-white-mask.lock`), CP/VP/EUI, huge
`C5H-VP8P-HUGE_0350` t350, marker `clear=0x1` + `whitemask=1` (`darkred.py config --arm d33 --whitemask`), the 3009
camera plan (`shell/evidence/3000/darkred/plan_huge.json`) by `i2_capture.py --views-only`; one launch per run; all
guard self-tests and start-up watches ok; stopped by `cli.py stop` with the settled-autosave hook. Evidence
`shell/evidence/3000/whitemask/` (`runs.csv`, `viewmask_3011.log`, half-size shots, handles); full shots
`D:/c5-64bit-work/3000/whitemask/`.
- **The call works.** Log of each process: `whitemask: checks passed (loader 0x01196540, ...)` (EXE base 0x00CD0000 in
  all three), then `loaded assets\terrain\editor\White.dds in 0 ms: handle 0x2 view 0x4ed5628c; mask global 0x00000000
  -> 0x00000002`, on the main thread (tid = the guard self-test's thread); `0xDA12AC` reads 0x2 in game
  (`handles_w1.json`). No exception, no dialog. So at `DllGetGameContext` the device, the file system and the packs are
  ready [L] (corrects the [I] of the static half).
- **Second load in the same process [L]:** from W1 in game, `mapchange.py load --save C5H-3000-TINY-SETUP --state
  InGame` (Events.PlayerChoseToLoadGame on the main thread; the in-game path works, unlike the front-end one of 3008
  item 11) loaded Tiny at 09:18:45; no second `loaded` line, `whitemask turn 18: mask global 0x2, view 0x4ed5628c` (the
  same handle and view as for the huge game): the global and the texture survive a load; one texture per process.
- **What it fixes and what not (W1, `huge_v2_city_zoomin_w1_d33_whitemask.jpg`):** improvement models (farms, mines,
  pastures, lumber mills, forts, citadels, the Great Wall, a landmark obelisk), resource models (horses, sheep, bison,
  whales) and other landmark-system models are **textured**; **city buildings and world wonders stay dark red**
  (Memphis, Elephantine; on Tiny the Pyramids and Madrid). Revealed-but-not-visible plots: drawn fully lit, no fog tint
  (`huge_v5_fog_revealed_not_visible_w1.jpg`, citadel at (103, 37)), as predicted.
- **Trees: still not drawn [L]** with Forest on (W2, config 0xFF0006E3) and also with Forest + Overlay on (W3,
  0xFF0006E7, TerrainOverlay handles 0x247/0x248): the forest view v4 shows the same models as W1 (Forest off); a pixel
  diff W1-W2 shows only animated units, animals and the red city blobs (`diff_w1_w2_v4_forest_zoomin.png`). Overlay on
  does not change the cities either. So the mask projection (Overlay) is not the missing input. *(Both misses are
  fixed by `whitemask=2`, second half below: the draw objects were built at boot with mask 0.)*
- **Why cities, wonders and trees miss it: answered by the second half below [C].** *Corrected 2026-11-11 (3011,
  second half): the forest builder does store `0xDA12AC` at +0x24 (t6): it is `0x6c1cd0`, a function Ghidra's
  analysis of the `civ5` project never defined, so its reads were missing from the cross-references. Replaced: "None
  stores at +0x24 (t6) ... the forest draw object takes its mask from somewhere else" and "the city-building path was
  not found".* The other readers copy the handle into a texture slot at +0x18 + 2 x slot (e.g. `0x565bbf`, `0x57dbc5`,
  `0x5a25d4` store at +0x26 = t7; `0x495dc5` in the ice system at +0x28; `0x6cf962` +0x2a; `0x6b172c` / `0x73ad9c` +0x2c;
  `0x59fe52` +0x1a).
  `0x5e8df0` (thiscall on pool `0x413e2c0`) is the texture release (use count -1, frees at 0); `0x683c70` (FluidFOW
  shutdown) and `0x5c0f40` (editor shutdown) release `0xDA12AC`; neither runs with FluidFOW off [C].
  `0x49c940` (FluidFOW, bit 15) renders INTO `0xDA12AC` as a render target each frame; with FluidFOW off it does not run.
- **Memory [L, one run each]:** private MB after load W1 1,643 (R0 D33 1,653: -10, within noise), W2 (+Forest) 1,679
  (R4 1,678), W3 (+Forest +Overlay) 1,686; the stand-in costs nothing measurable. Tiny after the in-process second
  load: 1,542 (not comparable to a fresh Tiny launch). Frame rate: not measured (needs six interleaved launches per arm
  under the quiet lock; left for after block B).

**The white mask stand-in, second half: where cities, wonders and trees take their mask (issue 3011 continuation,
2026-11-11 10:10-10:35 EST, static part).** Method: byte scan of `.text` of the Ghidra input copy of the DX11 EXE
(`%USERPROFILE%/tools/ghidra-projects/bin/CivilizationV_DX11.exe`, TimeDateStamp 0x546CD0A8) for every 4-byte operand
in 0xDA1280-0xDA12E0, `llvm-objdump -d` of the whole `.text` and a direct-call graph built from it, Ghidra headless on
the `civ5` project copy for the functions it has; scripts in `D:/c5-64bit-work/3011/scripts/` (`scan.py`, `xref.py`,
`cg.py`). **Use the byte scan, not Ghidra's references, for "who reads this global": the `civ5` project leaves whole
functions undefined (here the forest builder and Forest_System::Startup's body), and their reads do not appear [C].**
- **Readers of `0xDA12AC` [C, byte scan]:** 21 sites: three in FluidFOW's internals, one in the ice system, two in
  FluidFOW's draw, five between 0x555924 and 0x5A25D7 (city templates), one in the editor's shutdown, one in FluidFOW's
  shutdown, **two in the forest builder (not in Ghidra)**, and six not attributed to a system; writers `0x44a732`,
  `0x561fe3`. (Reader address list replaced by the count per system, 2026-11-18, D83 audit; the scan is reproducible
  with the method above.)
- **Trees [C]:** `0x6c1cd0` (thiscall, ECX = Forest_System `0xDA91C8`) builds, for each of 5 entries, two draw objects
  with program 44 (`push 0x2c` at `0x6c210c`, ForestShader) through `0x8c69b0`, and copies the slots in ForestShader's
  order: +0x1c t2 = `[0xDA12A4]` (Terrain Overlay), +0x1e t3 = `[0xDA12A8]` (Screen Overlay), +0x20 t4 = `[0xDA12B4]` (fog
  ramp), +0x22 t5 = `[0xD90E4C + 4i]`, **+0x24 t6 = `[0xDA12AC]` (FOW mask)**, +0x26 t7 = `[0xDA12B0]`, +0x28 t8 =
  `[0xD90E60 + 4i]`, +0x7c = 9 textures. Its only caller is `0x7260a6` in `0x726050`, which is **Forest_System::Startup**
  (called at `0x47672d` in app Startup, gate `test al,al; jns` = bit 0x80; the log name "Forest_System::Startup()" is
  pushed after the call at `0x476755`, as for every system there). App Startup runs at boot, before any front-end Lua
  and before the mods are activated (`research/sources/shell-gates/LOADTIME.md` rows 5 and 7), so the forest draw objects
  copy `0xDA12AC` = 0 before the DLL exists.
- **Cities and wonders [C]:** `0x5a24a0` fills a city-building template (texture "Assets\Buildings\City\black.dds" as the
  diffuse fallback; cubelight by the era index at `this+0x15dc`): +0x22 t5 = `[0xDA12B4]`, +0x24 t6 = `[0xD90E88]`, **+0x26
  t7 = `[0xDA12AC]`** (`0x5a25d4`), +0x28 t8 = `[0xDA12B0]`, +0x2a t9 = `[0xD90E60]`, +0x7c = 10: the BuildingShader
  layout. Its chain is all direct calls from City startup at boot: `0x4765f6` (app Startup, gate bit 0x20, ECX =
  CityBuildingSystem `0xCD0D98`) -> `0x5640a0` -> `0x46eb90` -> `0x6cc530` -> `0x856dc0` -> `0x766f70` (loops over the
  building models) -> `0x47f920` -> `0x47a340` (once per model; flag +0x78 marks it built) -> `0x733630` (takes a 0x84-byte
  record from the array at `this+0x13ec` through `0x766970`) -> `0x5a24a0`. A second boot chain `0x5640a0` -> `0x625730`
  -> `0x504e20` -> `0x736d60` -> `0x465390` -> `0x6e0a80` copies 0x80 bytes of each template into a new pool draw object
  (`0x6e0b97` `call 0x8c69b0`, `rep movsd` 0x20 dwords). So city buildings and wonders are made at boot as well.
- **Why improvements and resources were fixed [C + L]:** their builders (`0x7b2150`, `0x75a220`) run during the game
  load (`0x6fba80` chains), after `DllGetGameContext`, so they copy the white handle (live half above).
- **The draw-object pool [C]:** `0x8c69b0(out ptr, out index, program)` takes an index from the allocator at VA
  `0x30BCE00` (`0x8c5c00`: +0 spin lock by InterlockedCompareExchange, +0x40 high-water index, +0x44 + 4 x index use
  count, at most 0x20000, free list from +0x80044) and returns the entry at **VA `0x313DE88` + 0x80 x index** (static,
  in the image, memset 0, word +0 = program index, the order of `evidence/2900/2903/slot_names.txt`: ForestShader 44 =
  the builder's 0x2c). `0x8c69f0` clones an entry; `0x8bcb60` copies one out; `0x8c3280` releases (use count -1, onto
  the free list at 0). Entry layout as the builders write it: +0x18 + 2 x slot texture handle, +0x4c + 2 x slot sampler
  state, +0x7c texture count.
- **Fix chosen (fewest writes, no extra call) [L, live part below]:** after the global is written at
  `DllGetGameContext`, one pass over the live pool entries (use count > 0, index < high-water) writes the white handle
  into the mask slot where it is 0: programs 1-6 (BuildingShader family) slot t7 (+0x26) when t5 (+0x22) equals the
  fog ramp `[0xDA12B4]`; program 44 (forest) slot t6 (+0x24) when t4 (+0x20) equals the fog ramp. The same pass runs
  once per turn from `CvGame::update` and logs the counts. A per-object path (Forest_System `+0xb0 + 0x50/0x64 + 4i`
  hold the forest draw-object indices; the city templates sit in the 0x84-byte array at `CityBuildingSystem+0x13ec`)
  would need more structure knowledge for the same writes. Not chosen: an earlier write (impossible: app Startup runs
  before the DLL is loaded) and a call to rebuild the objects (an extra EXE call). Code: fork branch `i2-white-mask`
  `66b0b2bdc`, marker `whitemask=2` (1 keeps the global-only behaviour).

**The white mask stand-in, second half, live part (2026-11-11 12:17-12:55 EST) [L].** Instance c5h-b (Goldberg), DLL
`i2-white-mask` 66b0b2bdc (SHA-256 24A64E57...59FBBAC5, `deps/i2-white-mask.lock`), CP/VP/EUI, marker `clear=0x1` +
`whitemask=2`, the 3009 camera plan (`shell/evidence/3000/darkred/plan_huge.json`) and the ground-truth Tiny plan
(`D:/c5-64bit-work/i2-ground-truth/tiny/plan.json`) by `i2_capture.py --views-only`; one launch per arm; guard hooks
17/17 and start-up watch ok; stopped by `cli.py stop` with the settled-autosave hook. Evidence `shell/evidence/3011/`
(`runs.csv`, `viewmask_3011_x1_x2.log`, six 800-px shots); full shots `D:/c5-64bit-work/3011/x1`, `x2b`.
- **Live read of the candidates (the pass's own counts):** at `DllGetGameContext` the pool held 37,702 live entries
  (high-water 37,905); **36,185 building-family entries (programs 1-6) had mask 0 and fog ramp 0x0005**, all patched
  (0 ms); with Forest on, also the **10 forest entries** (5 x 2, as the builder makes them). After the game load:
  45,094 building-family entries, all with the white handle, none with 0 (the load-time builders copied the global);
  forest 10 ours. No entry of these programs had another handle ("other" 0 in every pass). Log lines:
  `whitemask pool install turn -1: high-water 37905, live 37702; building family: ours 0, zero->ours 36185 ...`.
- **Result:** with D33 (X1, 0xFF000663) **city buildings and world wonders are textured** (Memphis, Elephantine;
  `huge_v2_city_zoomin_x1_d33_whitemask2.jpg` against the dark red of `shell/evidence/3000/whitemask/huge_v2_city_zoomin_w1_d33_whitemask.jpg`),
  improvements and resources stay textured. With D33 + Forest (X2, 0xFF0006E3) **trees are drawn**
  (`huge_v4_forest_x2_d33_forest_whitemask2.jpg`). Revealed-but-not-visible plots: drawn fully lit, no fog tint
  (`huge_v5_fog_revealed_not_visible_x1.jpg`, plot (103, 37) and the Mayan city there), the expected loss.
- **Second and third load in one process:** X1 huge -> Tiny (`mapchange.py load --state InGame`): Madrid and the
  Pyramids textured (`tiny_v2_city_zoomin_x1_second_load.jpg`), turn-18 pass: 45,212 ours, 0 zero, same handle 0x2 and
  view. X2 huge -> Tiny -> huge: trees and cities still right after the third load
  (`huge_v4_forest_x2_third_load.jpg`); every pass 0 zero, forest 10 ours; no second `whitemask: loaded` line: one
  texture per process, no stale handle. Not tested: the front-end route (back to the main menu, then load), which
  the harness cannot drive yet (3008 item 11).
- **Memory (one run each, private MB after load):** X1 1,641 (capture) / 1,643 (memory.py) against R0 D33 1,653 and W1
  1,643: the pass costs nothing; X2 1,671 / 1,681 against R4 D33 + Forest 1,678. So D33's saving (-426 MB against the
  stock configuration, section 4) is kept, as against +304 MB for FluidFOW + Overlay. After two more in-process loads
  1,698 (normal growth across reloads; not compared).
- **Not measured:** frame rate (not part of this task). Not checked: a city founded or a building built in play (new
  draw objects would come from the boot templates with mask 0: the per-turn pass patches them within one turn and logs
  the count; the city templates themselves stay 0).
- Operator note: a second launch needs `frontend.py prepare` again; without it the front end logs "this launch already
  issued its load" and stays at the main menu (cost 20 min here).

### The white mask on the product path (issue 0172, 2026-11-14)

**What and where [C].** 3011's fix (`whitemask=2`, fork `i2-white-mask` 66b0b2bdc, a 32-bit test DLL) is ported into the
x86 proxy, `proxy/src/proxy_shell.cpp` namespace `White`, behind `VP64Options.ini` `[Renderer] WhiteFogMask` (1 = on,
**the product default since D136** (the owner, 2026-11-14, answering 0098; issue 0185); 0 = off; `options/`, `docs/OPTIONS.md`). On the product path the 32-bit DLL is the proxy, so the fix had to
move; the proxy, not the composite DLL, because `ProxyShell::OnBoot` already runs from `DllGetGameContext` on the EXE's
thread that called it (3011's install moment, [L] the main thread at the front end), it already holds the EXE checks
(`Mask::VerifyExe`) and it has the per-Update hook on the game-core thread (`ProxyShell::OnUpdate`, top of each
`ICvGame1::Update`); the composite is loaded only with `ServerRenderer = 1` and has no Update hook. The option is
independent of `ServerRenderer` (it acts on any mode=on boot whose config has FluidFOW off).

**Product default since D136 [C] (issue 0185, 2026-11-14).** The owner answered 0098 (D136 [reworded for privacy]: approved on both
counts): the proxy may call the EXE's texture loader by address and write the white mask into the EXE's draw-object
data. `WhiteFogMask` therefore defaults to 1 (was 0 under 0172): in the options table (`options/src/vp64_options.cpp`),
in the file the patch writes (`; WhiteFogMask = 1`, help text "1 = on (default)") and in `docs/OPTIONS.md`; `= 0` still
turns it off. Consequences: a `VP64Options.ini` without the key, which includes every file the patch wrote before 0185
(they carry the key only as the comment `; WhiteFogMask = 0`), now gets the white mask; so does every test or job that
writes only `ServerRenderer` (`tools/testbed/jobs/product_path.py` and the release jobs, `proxy/tools/test_proxy_rt.py`'s
loader cases, where the EXE checks would refuse in `loader.exe` [I]). A run that needs the old picture (dark-red
models) writes `WhiteFogMask = 0`. Tests with the new default (2026-11-14, workstation): `test_options` 164/164 x86, x64
and ASan; `proxy/tools/test_proxy_rt.py --only-shell` 19/19 (`shell_unit` 267/267; loader cases with a server.exe
built for 0177, `C:/c5-64bit-work/0177/b`) [C]. No live run for 0185: 0172 proved both settings (below). Acceptance pictures
(D112) follow with the first increment-2 release candidate ("Pictures, how to take them again", below).

What it does, step by step (log lines in `vpproxy.log`, prefix `shell: white mask:`):
1. `[Renderer] WhiteFogMask = 0` (set in the file; the default is 1 since D136): one line "off (nothing loaded,
   nothing written)" and nothing else.
2. Guards, all on bytes that are equal in every installed copy (quiet-testbed.md 15.3: `.text` and the link timestamp):
   `Mask::VerifyExe` (timestamp 0x546CD0A8, the six view-mask patterns), then the twelve code patterns of the table
   below and nine absolute operands inside them, then the image size and VirtualQuery of the globals, the texture-view
   table and the pool. **Changed against the origin:** its check of the `White.dds` string bytes at VA 0xA3D66C is
   dropped, because that string is in `.rdata`, which differs per installed copy; only the editor call site's pushed
   address (a `.text` operand) is compared, and the proxy passes its own copy of the path to the loader.
3. g_Civ5App readable, config mask with FluidFOW (0x8000) off, renderer `[0x3010570]` and device `[0x301057C]` non-null.
4. The mask global 0xDA12AC: 0 -> one call of the loader 0x8C6540 (SEH-guarded) with `assets\terrain\editor\White.dds`,
   handle < 0x3000 with a non-null view, compare-and-swap into 0xDA12AC, the handle kept in the EXE's environment as
   `VP64SHELL_WHITE`. Non-zero: taken over only if it equals `VP64SHELL_WHITE` (a later boot of the proxy in the same EXE
   process, issue 3308: no second texture), else "left alone" and nothing more.
5. The pool pass of 3011 (building family programs 1-6 slot t7 where t5 is the fog ramp, forest 44 slot t6 where t4 is
   the fog ramp; only mask slots that are 0) at the boot, then from the Update hook at most once a second (3011: once a
   turn), which also re-writes the global if it went back to 0 and stops if it holds a handle that is not ours. A pass is
   logged when it wrote something or the count of foreign handles changed (at most 300 white-mask lines a process).

| RVA (VA - 0x400000) | Name in the log | What the pattern covers | Operand check |
|---|---|---|---|
| 0x4C6540 | texture loader entry | the loader's prologue: an aligned frame with a 0x10588-byte stack area, `this` kept, the first argument passed on | |
| 0x4C65FB | texture loader create | the call that creates the texture | |
| 0x4C6628 | texture loader return | the epilogue: returns the handle and pops one stack argument | |
| 0x161FCB | editor white load | the editor's load of `White.dds`: renderer read, path pushed, loader 0x8C6540 called, handle stored in the mask global | +2 = 0x3010570, +7 = 0xA3D66C, +24 = 0xDA12AC |
| 0x3B267C | builder mask copy | the 16-bit mask handle read from the global into the draw object's slot +0x26 | +3 = 0xDA12AC |
| 0x4C69B0 | draw-object alloc | a slot index taken from the pool and the 128-byte record addressed in the table | +3 = 0x30BCE00, +24 = 0x313DE88 |
| 0x4C5C84 | draw-object high-water | the high-water count at +0x40 compared with the limit 0x20000 | |
| 0x4C5C9B | draw-object use count | the new high-water stored and the slot's use count (+0x44 + 4 * index) incremented | |
| 0x2C210C | forest program | program 44 pushed and the draw-object builder 0x8C69B0 called | |
| 0x2C2231 | forest mask copy | the mask handle read from the global into slot +0x24 | +3 = 0xDA12AC |
| 0x1A25D4 | city mask copy | the mask handle read from the global into slot +0x26 | +3 = 0xDA12AC |
| 0x1A25AF | city fog ramp copy | the fog-ramp handle read from its global into slot +0x22 | +3 = 0xDA12B4 |

(Operands as VAs at ImageBase 0x400000; the code compares each with base + RVA, at the given offset into its pattern.
The patterns are the origin's, read from the DX11 EXE's on-disk `.text` by 3011 [C], which also passed live on c5h-b;
they are held in `proxy/src/proxy_shell.cpp` (`PATTERNS`). The table's bytes column was replaced by what each pattern
covers, 2026-11-18, D83 audit.)

**A clang-cl fact found on the way [L offline, 2026-11-14].** The origin's pool loop read the pool directly inside
`__try`. In the offline test (`proxy/tools/rt_tests/shell_unit.cpp`, a page of the fake pool made PAGE_NOACCESS) the
proxy-toolchain build (clang-cl -m32) did not catch the access violation: the test process ended with 0xC0000005. With
the loop in a `__declspec(noinline)` function called from the `__try` block (as `PollGuarded` calls `Apply`), the fault
is caught and the pass is switched off. Rule for the proxy: a `__try` meant to catch a hardware fault wraps a call, not
inline memory accesses (clang treats only calls as able to raise inside a `__try`, unless built with `/EHa`).

**Tests [C].** `proxy/tools/rt_tests/shell_unit.cpp` section "white" against a fake image (now 0x3E00000 bytes, for the
texture-view table up to RVA 0x3D73348) and a stand-in loader: option 0 does nothing; each of the 12 patterns and 9
operands changed refuses with its name, the loader is not called and nothing is written (the "guard refuses a modified
EXE" test); a wrong timestamp, a view-mask pattern and a too small image refuse; FluidFOW on, no renderer, no device, no
app, a global already set, the loader returning 0 / a non-handle / a handle without a view / raising: nothing written;
on: one call with the editor's path and the renderer as this, the global 0 -> 2, building t7 and forest t6 written, the
others (ramp not the fog ramp, free entry, program 7, a foreign handle, the forest's t7) untouched; the Update pass picks
up a later entry, its once-a-second limit holds, a zeroed global is re-written, a foreign global stops the pass; a second
boot takes the handle over without a load and refuses a handle `VP64SHELL_WHITE` does not name; a fault in a pass turns
the pass off. 2026-11-14: `shell_unit` 267/267 with the proxy's toolchain and under MSVC `/fsanitize=address` (canary
caught); `test_options` 160/160 in the x86 and x64 trees and under ASan; `proxy/tools/test_proxy_rt.py --build b1`
79/79 (65 s; unit, loader, guard, shell with the real server).

**Live on the product path [L] (2026-11-14 15:15-15:31, c5h-a, huge `C5H-0172-HUGE_0350` = copy of
`C5H-VP8P-HUGE_0350`, turn 350).** Built from source: proxy 15273ED8... (repo 02c0e5290), server.exe ACFA499B..., x64 DLL
74588135 (pin 04b1211b8, 9045's deps), composite 393F458E...; `ServerRenderer = 1`; config.ini D33 with Overlay on (D93;
config mask 0xFF240667). One launch per arm, the same prep, restored by hash after (`shell/evidence/0172/`):
- **on** (`WhiteFogMask = 1`): checks passed at the boot (EXE base 0x00D50000, thread of `DllGetGameContext`), `White.dds`
  loaded in 0.6 ms, handle 0x2, global 0 -> 0x2; the boot pass set 36,185 building-family entries (high-water 38,240,
  live 38,037; the same count as 3011's) in 0.92 ms; no later pass found a zero entry (the load-time builders copied the
  global). City buildings, wonders, improvements and resources textured at the load view, the capital (Thebes) and
  Akhetaten; dark-red pixel share of the whole window 0.87 / 0.57 / 0.50 % against stock's 0.95 / 0.66 / 0.60 % (the
  rest is red interface and units) and off's 4.41 / 14.2 / 4.56 %.
- **off** (`WhiteFogMask = 0`): the one "off" line; models dark red as before 0172.
- **stock** (`ServerRenderer = 0`, the instance's own config): the reference.
- Memory, EXE private MB after the load (one launch each, an indication): on 1,424.6, off 1,424.9, stock 1,853.0.
- Trees are not in these pictures: the product's D33 set has Forest off; 3011 showed them with Forest on.
- Pictures: Visual Checks feed, issue 0172, labelled DIAGNOSTIC (MC #706: no increment-2 release candidate exists yet);
  ids and the differences to stock in `shell/evidence/0172/acceptance.txt`.

**Pictures, how to take them again (on the first increment-2 release candidate).** Driver
`shell/evidence/0172/run_0172.py` (on top of 3311's product-path prep, launch, stop and restore by hash): one `prep` with the
candidate's x64 folder (`--bin`, server.exe and the x64 DLL with its libraries), proxy (`--proxy`) and composite folder
(`--composite`, holding `bin/shell_composite.dll`), then for each arm `arm --arm on|off|stock` and `run --arm ... --pics
C:/c5-64bit-work/<ticket>/pics --label "<the candidate's name>" [--acceptance]` (`--acceptance` only for a shipped release
candidate, D112), then `restore`; `analyze_0172.py --work ... --pics ... --out ...` gives the dark-red share per arm and
camera and the differences to stock. The instance is claimed by `C5_POOL_OWNER` (name it with `--who <owner>`;
the default is `impl-B-0172`).

**Trees (Forest system) with the terrain pass off [L]:** D33 with Forest on (R4, config 0xFF0006E3) draws **no trees**:
at the 3003 default view the stock shot (`huge_v1_trees_stock_3003.jpg`, same DLL, view matrix bytes identical) shows
forests top left and top centre, the R4 shot (`huge_v1_trees_d33_forest_on_r4.jpg`) none; a pixel diff R0 vs R4 at both
views shows only animated units, animals and UI. Cause [C]: the forest PS discards where the FOW Mask reads 0 (the
Forest render is gated by its own bit 0x80 at `0x589632`, not by the Terrain bit). Memory +25 MB for Forest on
(1,678 vs 1,653, one run; 11-09 probe: 17 MB). Not run: Forest on together with FluidFOW + Overlay (predicted to draw).

### The EXE's own minimap under D33: Minimap kept on (issue 0177, 2026-11-14) [L]

Step A of the Architect's minimap design (`research/07-architecture-decisions/architect-2026-11-14-hand-over-designs.md`
5.2): today's product `config.ini` list (D33 with Overlay kept on, `shell/tools/darkred.py`) with only `Minimap` taken
out, everything else as shipped. Job `tools/testbed/jobs/minimap_step_a.py` (`product_path.py --keep-on Minimap` is the
same switch for a plain product-path run). Laptop `tb1` (Quadro P3000, 1530 x 835 back buffer, Vibrant Textures present),
save copy of `TB-HUGE_0350` (SHA-256 `c499d3c6...`, = `C5H-VP8P-HUGE_0350`), cp+vp+eui, built from branch
task-0177-minimap-step-a (main 923de9ba): server.exe `617b7069`, x64 DLL `74588135` (pin 04b1211b8), proxy `d6388df0`,
composite `424c0838`. Pictures are the composite's back-buffer dumps (PrintWindow and ImageGrab are black on the laptop,
release-driver.md W3b.7), all DIAGNOSTIC (a test arm, D112).

**Result: the EXE's own minimap works under D33 when Minimap is kept on, and it equals stock's texel for texel at the
same turn and camera except for the camera frame [L, 1 run per arm].** Job `20261114-155109-347b` (2026-11-14
15:51-16:42 EST, exit 0), arms in one job on one instance, each its own launch, prep and restore by hash:

| | mm (D33, Minimap kept on) | d33 (today's product list) |
|---|---|---|
| config.ini keys = 1 | d33's 15 minus Minimap (14) | OverlayDebug, Decal, Forest, River, Combat, Particle, Projectile, FOW, FluidFOW, YieldIcons, DistanceFog, Minimap, ColorKeyEdit, Arrows, MovementPath |
| config / view mask, after the load and after the turns | 0xFF2C0667 / 0xFF0C0666 (bit 0x80000 set in both) | 0xFF240667 / 0xFF040666 |
| launch to in game | 164.1 s | 164.1 s |
| AI turns 350-380 | 30 of 30, mean 43.1 s | 30 of 30, mean 42.9 s |
| crash box, server.exe alive at the end, LOUD lines (server, render, vpproxy, composite logs) | none, yes, 0 | none, yes, 0 |
| EXE private MB / largest free block MB after the load | 1,431.0 / 2,002.8 | 1,420.0 / 1,990.2 |
| the same after the 30 turns | 1,630.9 / 1,774.1 | 1,596.9 / 1,822.9 |
| stop, restore by hash, `provision.py verify` | rc 0, all, 0 | rc 0, all, 0 |
| panel (Visual Checks, back buffer at load / at turn 380) | drawn: `vc-20261114-155442-laptop-cc4b` / `vc-20261114-161632-laptop-9224` | dark: `vc-20261114-162016-laptop-f533` / `vc-20261114-164158-laptop-1ce7` |

- **Memory:** Minimap on costs **+11.0 MB** private after the load and **+34.0 MB** after 30 turns, one run each (the
  2026-11-09 single-key probe said 9 MB; noise ~20 MB, and the AI turns of two launches are not the same game). The
  largest free block was 12.6 MB larger with Minimap on after the load and 48.9 MB smaller after the turns: inside the
  fragmentation noise of one run, and still about 400 MB above the stock game's (3516's fb-stock on the same host and
  save: 1,375.8 after the load, 1,202.9 at t353; release-driver.md W3b.7).
- **The picture** (`shell/evidence/0177/compare_panels.py`): the minimap is a **320 x 190 image at 1:1** in both the
  laptop's 1530 x 835 back buffer (at 1201,641) and the workstation's 2554 x 1411 stock picture (at 2225,1217) [L]
  (the renderdoc-capture.md 320x190 pass [I] is that size). Against issue 3608's stock picture of the same save at the
  same camera (`D:/c5-64bit-work/3600/3608/huge/stock/v1_city_default_stock.png`, workstation c5h-b, 32-bit pin
  i0ship-26234371-ia, Vibrant Textures as on the laptop's `tb1`): **99.1 % of the 60,800 pixels equal, 99.74 % within 8,
  99.99 % within 32**; the rest is the camera frame (a slightly different trapezoid: different screen size and aspect) and
  one unit dot. Terrain, ocean, ice, fog (black unexplored plots), owner colours, city and unit dots equal stock. So
  Minimap does not sample the EXE's tile caches the product does not bake (the design's worry): it draws from the
  terrain build and the events, which D33 keeps. After 30 AI turns the panel still draws and follows the game (borders
  changed); the turn-380 pictures of both arms have the Alcatraz wonder popup over the map (not closed: D114 does not
  apply to the panel check, and the panel is dimmed by it, not hidden).
- **Today's D33 panel is not plain black:** with no texture broadcast the Lua never sizes the panel
  (`MiniMapPanel.lua` sets the sizes in its `MinimapTextureBroadcastEvent` handler), so the panel stays at its XML size
  (taller than 320 x 190) and shows a dark, nearly black area with faint shapes; the comparison against stock is 0 %
  equal (`vc-20261114-162016-laptop-f533`).
- **The UI lookup for the panel's rectangle** (`ContextPtr:LookUpControl("/InGame/WorldView/MiniMapPanel/Minimap")` in
  the Lua channel's `InGame` state) returns an object without `GetScreenOffset`; the job takes `--panel-rect` instead.

**Click and ping (arm mm, 0 turns, jobs `20261114-164239-f06d`, `20261114-164909-1f52`, `20261114-165430-0828`,
2026-11-14 16:42-17:00 EST, each exit 0, restored by hash, 0 LOUD lines):**

- **A click on the panel moves the camera there [L, 2 of 2 runs, identical].** Posted messages (`cli.py click` with
  client fractions) at 0.25 and 0.75 of the panel's width, 0.5 of its height: the camera jumps to the western continent
  and to the Spanish/Mayan border, the panel's camera frame follows (to x ~77 and ~237 of 320), and the hex under the
  screen centre is **30,39 and 94,39** on the 128 x 80 map (expected about 32,40 and 96,40; the same -2,-1 offset for
  both is the centre hex against the look-at point). The two clicks are 160 panel pixels and 64 plots apart: 2.5 px per
  plot = 320 / 128, so **the click handler maps the panel linearly onto the whole map width** (the design's premise M3:
  from the map size) [L]. Visual Checks `vc-20261114-165246-laptop-4907`, `-165253-laptop-6064` (and `-165806-laptop-bb99`,
  `-165813-laptop-7cae`).
- **Harness note [L]:** on the laptop the game window's client is 3824 x 2088 physical pixels while the back buffer and
  the UI are 1530 x 835, and the game scales client coordinates to the UI. A click given in integer back-buffer pixels
  (`cli.py click 1281 736`) lands at 0.33 / 0.35 of the screen, on the map (job f06d's two clicks; entries amended).
  Give posted clicks as client fractions (`floats in [0, 1]`, tools/harness/input.py).
- *Corrected 2026-11-14 (issue 0192, next section): the ping works and is the same as stock's; it is a dot that blinks about
  every 0.4 s for about 5 s after the call, so pictures at 0.5 s, 3 s and 4-9 s mostly miss it. The bullet below is
  0177's reading, kept as its trace.*
- **The ping is not established [L, 1 of 3 runs].** `Player:AddNotification(NOTIFICATION_GENERIC, ..., 64, 42)` through
  the Lua channel adds the notification (it shows in the notification list); `CvNotifications::Add` calls
  `GameplayMinimapNotification` (policy-table row 408, consumer exe, posted). Job f06d: 3 s later a yellow (255,255,0)
  dot at panel pixel (162,88), where plot 64,42 maps, absent before (`vc-20261114-164638-laptop-5f91`). Jobs 1f52 and 0828:
  no dot there in 10 pictures between 0.5 s and 9 s after the call (only a blinking unit dot at 277,123 changes). Not
  known: whether stock's notification ping is a short blink that these samples miss, or whether the call is lost on
  some route (no log counts the proxy's posted engine calls by name). A stock control of the same call, or a dense frame
  capture, decides it.
- The UI lookup `ContextPtr:LookUpControl("/InGame/WorldView/MiniMapPanel/Minimap")` from the channel's `InGame` state
  returns a Lua table (job f06d's type check), not a control; `UIManager:GetScreenSizeVal()` = 1530, 835 and
  `Map.GetGridSize()` = 128, 80 there.

### Minimap on in the product (issue 0192, 2026-11-14) [L]

**Decision (MC, 2026-11-14 17:57, under the hand-over design's 5.2 after 0177):** Minimap is no longer one of the D33 keys.
Since branch task-0192-minimap-off-d33 `shell/tools/darkred.py` has it in `D33_ON` (was `D33_OFF`), and the product's A''
list `proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2` (what the helper's config.ini routine writes and the session rule
reads) has 12 keys: OverlayDebug, Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons, MovementPath, Particle, Combat,
Projectile, ColorKeyEdit; their bits 0x0093F898, rule bits 0x0083F890 (10), the A'' config mask **0xFF6C0767** (was
13 keys, 0x009BF898, 0x008BF890, 0xFF640767). A config.ini that an older build left with Minimap = 1 still classifies as
a D33 session (session_unit checks both masks), but the routine (`proxy_cfgini.cpp`, keys from the header) neither
writes nor restores Minimap any more: a config.ini that a pre-0192 build left in its `on` state keeps Minimap = 1 after
the new build's `off` (an older side file's Minimap line is not read) [C by reading `kKeys`; not run]. *Corrected
2026-11-14 (issue 3803, the same code path run offline for MovementPath, cfgini_unit case `pre3803`): the older side file
is not read in part, it is refused whole: `ReadSide` rejects a key that is not in `kKeys` ("unknown, repeated or empty
key"), so the new `on` refuses and changes nothing, and the new `off` takes the "side file unusable" path: it writes the
stock value 0 into every one of its own keys that holds 1 (the player's own values of those keys are lost), removes the
side file, and leaves the former key at 1. Section "MovementPath on in the product" has the fix proposed.* No build with the
routine has reached players (no increment-2 release candidate yet), so only such a leftover on a test machine is
concerned. Every other key and the config.ini backup and restore by hash are
unchanged. Unit tests: test_cfgini --asan 12/12 (cfgini_unit 85/85, also under ASan; `on` keeps Minimap = 0),
test_vp64helper 82/82, test_proxy_session --only unit --asan 11/11 (session_unit 62/62), pytest test_d33_config,
test_minimap_step_a, test_product_path 30/30 (2026-11-14, finished before 19:17 EST).

**Live run [L, workstation, c5h-b, 2026-11-14 19:22-20:03 EST, one run per arm].** Driver
`shell/evidence/0192/run_0192.py` (run_3311's prep, launch, guards, turns and restore; analysis `analyze_0192.py`,
`memory_0192.py`; results `shell/evidence/0192/acceptance.txt`). Product path built from 7f1c1a12 on the x64 pin's
source and release deps (copied to `C:/c5-64bit-work/0192`): server.exe `7cd38a54`, x64 DLL `74588135` (pin 04b1211b8),
proxy `865b71d9`, composite `5dc07c81`; `[Renderer] ServerRenderer = 1`; huge save copy C5H-0192-HUGE_0350 of
`C5H-VP8P-HUGE_0350` (SHA-256 C499D3C6, turn 350), camera UI.LookAt 100,30; cp+vp+eui; Vibrant Textures present.
Arms: **new** = config.ini by darkred's new list (keys = 1: the 12 above plus River and Arrows; Minimap = 0), **old** =
the list before 0192 (Minimap = 1 too), each one launch with 30 AI turns (350-380); **stock** = the 32-bit pin
i0ship-26234371-ia in the CP slot, `darkred --arm stock`, no proxy, no server, same instance, save and camera, 0 turns.

| | new (Minimap on) | old (Minimap off) | new - old |
|---|---|---|---|
| reached the map, 30 of 30 turns, end turn 380, harness stop rc | yes, 30, 380, 0 | yes, 30, 380, 0 | |
| guard checks (start, in game, end), start-up watch | 0, 0, 0, ok | 0, 0, 0, ok | |
| crash box, crash dumps | none | none | |
| LOUD lines: server.log, render.log, vpproxy.log, shell_composite.log | 0, 0, 0, **1** | 0, 0, 0, **1** | |
| seconds per AI turn (mean) | 26.6 | 27.6 | |
| EXE private MB / largest free block MB after the load (+20 s) | 1,390.7 / 1,924.3 | 1,387.5 / 1,937.3 | **+3.2 / -13.0** |
| the same after turn 30 | 1,549.8 / 1,707.9 | 1,549.6 / 1,720.2 | **+0.2 / -12.2** |
| mean over the 30 turns (private / largest free) | 1,528.2 / 1,763.0 | 1,524.7 / 1,758.4 | +3.5 / +4.6 |
| minimap panel after the load | drawn | dark | |

- The one LOUD line of each arm is the overlay tap's rate-limited `overlay_tap: LOUD: missed copy: the previous copy is
  still in flight (missed 1, ...)` (epic 0167's D132 counting, `research/05-graphics-composition/overlay-system.md`,
  "the one remaining difference is the LOUD lines"; MC #865), the same in both arms: not Minimap.
- Memory: Minimap on costs a few MB (+3.2 at the load, +0.2 after 30 turns, one run each; noise ~20 MB). 0177 measured
  +11.0 / +34.0 on the laptop with the same single runs; the 2026-11-09 probe 9 MB. The largest free block stays about
  700 MB above the stock game's on the same instance and save (stock arm: private 2,002.7 MB, largest free 1,209.5 MB
  after the load).
- **The panel equals stock pixel for pixel** at the same camera on the same machine: the new arm's 320 x 190 panel (at
  2225,1217 of the 2554 x 1411 client) is 100.0 % equal to this ticket's stock arm (max difference 0) and 99.98 % equal to
  issue 3608's stock picture of the same save (max 4). After 30 turns it is still drawn and follows the game.
- **The ping (NOTIFICATION_GENERIC at plot 64,42 through the Lua channel, 3 calls per arm; the panel cut out of a
  PrintWindow capture about every 0.2 s for 10 s):** stock shows a yellow dot at the plot's panel position (161,89) that
  blinks, on and off alternately about every 0.4 s, from the first sample (0.18 s) to about 5.1 s, never later: in 34
  of 67 valid samples within 0-5 s. The product arm shows the same dot in all three calls (5, 5 and 3 samples, 0.55-4.96
  s, none after 5 s): 13 of 36 valid samples within 0-5 s. A product sample is valid only when PrintWindow returned the
  EXE's layer: 87 of 150 product samples were glitch frames of the joined window (flat grey or server terrain only,
  as run_0172.py's `ui_present` notes), against 22 of 151 in stock, where they are frames between the EXE's own
  presents. So the ping is drawn as in stock; 0177's single-picture samples (0.5 s, 3 s, then 4-9 s) caught the blink
  once in three. Visual Checks: `vc-20261114-192629/-192630/-192631-workstation-*` (new, pass) against
  `vc-20261114-200253/-200254/-200255-workstation-*` (stock); zooms `vc-20261114-200426-workstation-5a51` (new) and
  `-200427-workstation-399e` (stock); panels `vc-20261114-192548-workstation-7fa1` (new, pass), `-194402-workstation-7c3f`
  (old, dark), `-200211-workstation-7229` (stock).
- Not measured here: a click on the panel (0177: 2 of 2 on the laptop), frame rate, more than one run per arm.
  *(2026-11-15: the click, the ping and the strategic view were checked on today's beta product path against stock in
  issue 0035, next section.)*

### Minimap and strategic view on the beta product path against stock (issue 0035, 2026-11-15) [L]

**Result: the definition of done of package I2-F ("minimap and strategic view match the original in a live test") is
met on the beta product path: the minimap panel, a click on it and the ping are as in stock, and the strategic view,
entered and left by F10 and by `ToggleStrategicView()` from the MiniMapPanel context, is stock's own view mode 2, as
close to stock as two stock launches are to each other; 10 huge AI turns clean [L, one product launch, two stock
launches].** Both parts are the EXE's own systems (Minimap on since 0192, StrategicView on since 3010); the server draws
neither. The Architect's step B (`research/07-architecture-decisions/architect-2026-11-14-hand-over-designs.md` 5.3: the
server draws the minimap picture, 20-35 h) is a D80 re-implementation, not needed for this definition of done.

Set-up [L, workstation, c5h-c (C:, Goldberg, Vibrant Textures present, D94), 2026-11-15]. Product path built from main
6ae726e1 (`shell/evidence/0035/build_0035.sh`): server.exe `0c5a9a1f`, proxy `5b522b05`, vp64helper `4db9ff59`,
composite `17a3fc83` (manifest `1000DB48`), x64 DLL `29F58E17` and CvGameDatabase_x64 `9C37B39E` = the pin layout
`x64-04b1211b-pfdee656` (deps/dll.lock since 0360); `[Renderer] ServerRenderer = 1`, the shipped composite ini;
config.ini by darkred.py's list since 0368 in the A'' form (keys = 1: OverlayDebug, Decal, Forest, FOW, FluidFOW,
YieldIcons, DistanceFog, ColorKeyEdit; Minimap, StrategicView, Combat, Particle, Projectile = 0; River and Arrows on in
config.ini, their view bits cleared by the proxy). Stock = the 32-bit pin i0ship-26234371-ia in the CP slot,
`darkred --arm stock`, no proxy, no server. Huge copy `C5H-0035-HUGE_0350` of `C5H-VP8P-HUGE_0350` (SHA-256 C499D3C6,
turn 350; Quick Combat as the save has it), cp+vp+eui. Driver `shell/evidence/0035/run_0035.py` (0318's chain, as 0368;
its step: auto unit cycle off, units asleep, selection cleared, `UI.LookAt` 100,30 (0177/0192's camera), popups closed
by Esc first (D114: none open), cursor parked at 0.5,0.4), chain `chain_0035.sh`, analysis `analyze_0035.py` (13
checks, all PASS, exit 0; negative control with the first stock launch as the reference: 2 FAIL, exit 1). Arms, each
its own launch, restore by SHA-256 after each (`backup_restore_ok` true, `provision.py verify --quick` rc 0): stock1
15:11-15:18 EST, product 15:19-15:33 (step 15:24-15:28, then turns 350-360), stock2 15:35-15:43. The product pictures
are compared with stock2; stock1 against stock2 is the stock-against-stock control. Every arm ran beside other teams'
registered jobs (5-9 heavy: game instances of 4203, 0169, 0345; builds and suites of 0041, 0324, 0169-4308;
`shell/evidence/0035/data/logs/check-*.txt`), so the memory numbers are indications (COMMON 23).

| item | product (beta path) | stock | how |
|---|---|---|---|
| minimap panel after the load (320 x 190 at 2225,1217 of 2554 x 1411) | **99.98 % equal** to stock2, median 0, p95 0 (stock1 against stock2: 99.99 %; against 3608's stock picture: 99.99 %) | drawn | whole-window PrintWindow, the panel cut out |
| click on the panel at 0.25 / 0.75 of its width, 0.5 of its height (posted, client fractions 0.9025 / 0.9652, 0.9298) | plot under the centre **30,39 / 94,39**; panel with the camera frame 100.0 % / 99.99 % equal to stock2's | 30,39 / 94,39 in both launches (0177: the same on the laptop) | `UI.GetMouseOverHex` with the cursor at 0.5,0.5; zoom unchanged (14.26) |
| ping (`AddNotification` NOTIFICATION_GENERIC at 64,42, 3 calls, the panel sampled every ~0.2 s for 10 s) | the dot in **10 of 27, 7 of 27, 7 of 31** samples, 0.28-5.30 s after the call | stock2 4/18, 9/23, 6/29; stock1 11/28, 8/29, 4/16; 0.34-5.39 s | 0192's method; the dot blinks ~0.4 s for ~5 s |
| F10 posted: in view mode 2 | `InStrategicView()` true 4 s after the key, false 4 s after the second F10; the Lua channel answered in 0.03-0.18 s (no hang) | the same | |
| F10: the picture in view mode 2 | **80.7 % equal, 98.1 % within 8**, median 0, p95 1 against stock2 | stock1 against stock2: **81.4 % / 98.0 %** | whole frame with the UI |
| `ToggleStrategicView()` on the main thread from the MiniMapPanel context (a one-shot `Events.LocalMachineAppUpdate` handler; ran ok in 0.10-0.13 s) | in and out as F10; picture 80.7 % / 97.9 % against stock2 | 81.3 % / 97.8 % | |
| the composite in view mode 2 (shell_composite.log) | `camera: status OK -> NOT_MAP (... view mode 2 ...)` on entering, `NOT_MAP -> OK (... view mode 1 ...)` on leaving, both routes | (no composite) | the server's picture steps aside; the EXE draws the view alone |
| the map back after leaving | the frame after leaving against the one before entering: 99.1 % within 32 (both routes) | stock's own: 99.1 % | the server's terrain is back |
| 10 AI turns 350-360 | 10 of 10, end/wait rc 0/0, harness stop rc 0, guard checks 0/0/0, start-up watch ok, no crash box | | turns 351-359 29.9 s mean (turn 350's 263 s includes the step) |
| LOUD lines: server.log, render.log, vpproxy.log, shell_composite.log | **0, 0, 0, 0** (none beyond 0329's list; not even 0192's overlay_tap missed-copy line) | | |
| EXE private MB / largest free block MB after the load (+20 s) | 1,450.8 / 1,811.5 | 2,025.6 / 1,191.1 (stock2) | `tools/harness/memory.py` |
| the same after turn 10 (turn 360) | 1,597.2 / 1,621.9; 10-turn mean 1,590.3 / 1,662.5 | | |

- **Against 0192's figures** (Minimap on, c5h-b, 2026-11-14, the list before 0368): after the load +60.1 MB private /
  -112.8 MB largest free block (0192: 1,390.7 / 1,924.3); after turn 10 +66.0 / -137.9 (0192: 1,531.2 / 1,759.8). The
  difference is the 0368 list (Combat, Particle, Projectile on: +42 to +53 MB private and 29-97 MB smaller largest free
  block, section "Combat, Particle and Projectile on in the product"); against 0368's own new arm on this instance and
  list (1,461.1 / 1,782.1 after the load) it is -10.3 / +29.4, inside a single load's 20-30 MB noise. The largest free
  block stays about 430-620 MB above the stock game's on the same instance and save.
- **Two stock launches differ by about 19 % of the pixels in view mode 2** (route splines drawn with a different
  curve each launch, blinking notification and button icons; `diff_sv_*_inside_control.png`), so the strategic view is
  held to that control, not to an absolute share. The analysis' first criterion (>= 95 % equal), set before the control
  existed, failed for the product and for stock alike and was replaced (2026-11-15 15:47, `analyze_0035.py` b2).
- **PrintWindow on the stock EXE can return a frame without the UI** (stock1's click1 picture: terrain only, no top
  bar; a capture between two presents, as 0192 counted 22 of 151 stock samples). The driver now keeps, of up to 8
  captures, the one whose top bar's opaque parts (rows 0-30, columns 0-700 and from 1830) are nearest a picture known to
  carry the UI; stock2 was taken with that check, and every stock2 and product picture passes it (distance 0.1-0.9,
  against 130.6 for the UI-less frame).
- Pictures (all DIAGNOSTIC, D112: built from a branch, driver-made set-up): Visual Checks issue 0035, product entries
  `vc-20261115-152430-workstation-0649` (panel, pass), `-152541-...-e26a` / `-152606-...-2d28` (clicks, pass),
  `-152442-...-0b56`, `-152457-...-cde9`, `-152512-...-73fe` (ping strips, pass), `-152631-...-cc18` / `-152708-...-4115`
  (view mode 2, pass), `-152645-...-9486` / `-152723-...-8b59` (after leaving, pass), each with stock2's picture as its
  reference; stock2 `vc-20261115-153949` to `-154223`, stock1 `vc-20261115-151519` to `-151744` (info). Pictures and
  difference images under `F:/c5-64bit-work/0035/captures/huge` (moved from C: at the hand-back, D141), never committed.
- Not checked: a real mouse click on the minimap's strategic-view button (the call it makes was run from its context),
  `Game.DoControl`, the multiplayer staging room, frame rate, Quick Combat off during the turns, more than one product
  launch. The minimap after many turns was checked by 0192 (30 turns).
- **Recommendation:** close 0035 (definition of done met), and file the Architect's step B (hand-over designs 5.3: the
  server draws the minimap picture and the compositor copies it into the EXE's texture, 20-35 h; it needs unit
  positions in the feed, which 0041 needs too) as an increment-3 ticket under D80, to be taken up when the EXE's
  Minimap system is to be switched off for its memory.

Evidence: `shell/evidence/0035/` (`acceptance.txt`, `build_0035.sh`, `run_0035.py`, `chain_0035.sh`,
`analyze_0035.py`, `record_0035.py`; `data/`: `analysis.json`, `check.txt`, `check-negative-control.txt`, `record.txt`,
each arm's step and run JSON, the chain and load logs).

### MovementPath on in the product (issue 3803, epic 0179, 2026-11-14) [C, L offline; live below]

**Decision (MC, 2026-11-14 19:35, #1009 on 0136, route (b) for the move-order path until increment 3):** MovementPath is
no longer one of the D33 keys. Since branch task-3804-arrows-acceptance (commit of 20:23) `shell/tools/darkred.py` has it
in `D33_ON` (was `D33_OFF`), and `proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2` has **11 keys**: OverlayDebug, Decal,
Forest, FOW, FluidFOW, DistanceFog, YieldIcons, Particle, Combat, Projectile, ColorKeyEdit; their bits **0x0013F898**,
rule bits **0x0003F890** (9), the A'' config mask **0xFFEC0767** (was 12 keys, 0x0093F898, 0x0083F890, 0xFF6C0767).
`NEVER_RESTORE` and the view-bit rule are unchanged (`proxy_shell.cpp`: the MovementPath view bit 0x800000 follows the
config bit, so it stays set). The masks written by builds before 3803 (0xFF6C0767) and before 0192 (0xFF640767) still
classify as D33 sessions; a config.ini with only MovementPath = 1 is a stock session (session_unit). Why: the EXE draws
the path's discs and chevrons, 18 rigid Granny overlay models (`arrows-and-movement-path.md` 2 [C]), over the joined
picture as stock does (task 3801's arm, `research/05-graphics-composition/arrows-and-path-line.md` 7 [L, Tiny]).

Tests (2026-11-14 20:17-20:19 EST, each through `quietlock.py run`, 0 skipped): test_cfgini 6/6 (cfgini_unit 95/95) and
under ASan 12/12; test_proxy_session `--only unit` 7/7 (session_unit 65/65) and under ASan 13/13; test_vp64helper 83/83
and under ASan 164/164; pytest test_d33_config, test_minimap_step_a, test_product_path 30/30. Negative: with MovementPath
put back into darkred.py's `D33_OFF` only, test_proxy_session fails 3 of 7 and test_cfgini 1 of 6 (the list comparisons);
test_proxy_session also checks that a header with MovementPath put back fails the comparison.

**A config.ini left in the `on` state by a build before 3803 (0192's open point, checked for MovementPath) [L offline,
cfgini_unit case `pre3803`].** Such a file holds MovementPath = 1 and a side file `vp64-config-side.ini` with a
`MovementPath=` line. The new routine refuses that side file whole (`ReadSide`: a key not in `kKeys`), so: `status` is
partial ("side file unusable"); `on` refuses and changes nothing ("run vp64helper off first"); `off` (and the D125
`failed` route, which restores through it) writes the stock value 0 into each of its 11 keys that holds 1, removes the
side file and **leaves MovementPath = 1**: the stock game the player gets back has no move-order path, and a later `on`
keeps it off, until the player edits config.ini. The same holds for Minimap after 0192 (a pre-0192 side file) and for both
together. Shipped builds: none affected as far as the tracker shows (no build with the config.ini routine has reached
players: 0192's note, no increment-2 release candidate yet) [I]; test machines with a leftover `on` state are.
*The behaviour above is that of the routine until issue 3805; fixed 2026-11-14 20:50 (issue 3805, MC #1093) [C, L
offline]:* `proxy/helper/proxy_cfgini.cpp` has a list of **former keys** (`Waves` since epic 3600, `Minimap` since 0192,
`MovementPath` since 3803; `cfgini::FormerKeys()`). `ReadSide` accepts an older side file's lines for them (optional; any
other key still refuses the whole file). Where config.ini still holds 1 in a former key and the side file recorded
another value, `off` (and with it D125's failed-start restore) and `on` put the recorded value back (a removed line where
it was `<absent>`) and report `former:<key>`; `on` never writes 1 into a former key; `status` calls that state partial
and names `former:<key>`. A former key the player changed after the old `on` is left alone. The side file keeps a former
key's record until `off` removes the file. Tests (cfgini_unit 126/126, plain and under MSVC ASan, 0 skipped; test_cfgini
6/6 and 12/12; test_vp64helper 83/83 and 164/164; 2026-11-14 20:48-20:50 EST): the `on` states of a pre-3803, a pre-0192
(Minimap and MovementPath) and a pre-3600 build (all three, LF file) and a pre-3803 state whose config.ini had no
MovementPath line, each going `off`, and `on` then `off`, back to the original bytes; a former key the player changed;
negative: a side file with `Terrain=` (neither ours nor former) is still refused whole (`on` refuses, `off` takes the stock
path). *Since issue 0378 (2026-11-15) that path leaves config.ini as it is and moves the side file aside; it no longer
writes stock values (`installer-notes.md` 11.4).*

**Live [L, workstation, c5h-c, 2026-11-14 20:34-20:40 EST, one run].** The product path built from 2eb96de5 (3803's
commit; `shell/evidence/0179/3804/build_3804.sh`): server.exe `3f4ec2f4`, proxy `98693cc9`, composite `77a2917f`, x64 DLL
`74588135` (pin x64-04b1211b); prep by run_3702's chain (`shell/evidence/0179/3804/run_3804.py`, config.ini by darkred's
new list: MovementPath = 0, River, Arrows, Overlay, Minimap on), the shipped composite ini, `ServerRenderer = 1`, Tiny
copy `C5H-0179-TINY_0009V` (of `C5H-3600-TINY_0009V`, SHA-256 27420A41, turn 9). With the Warrior 1002 clicked, key M and
the cursor on (53, 13) (player input, 3801's camera), the EXE draws the path's discs, chevrons and destination ring over
the joined picture as stock does at the same camera (Visual Checks `vc-20261114-203728-workstation-7a5c`, DIAGNOSTIC,
pass; 3804's ACCEPTANCE pictures and figures: `research/05-graphics-composition/arrows-and-path-line.md` 11). Run rc 0,
guard checks 0, stop rc 0, **0 LOUD lines** in render.log, server.log, vpproxy.log and the composite's log; EXE private
1,241.6 MB, largest free block 1,993.6 MB, server private 772.8 MB after the load.

### The beta's gap alternative: Combat, Particle and Projectile back on over the product path (issue 0318, 2026-11-15) [L]

The question (Architect's increment 3 decision sheet, `research/07-architecture-decisions/architect-2026-11-15-increment-3-decision-sheet.md`
section 8 item 6): with the product's switches there is no combat animation, no effect and no unit shadow until 0042; one
cheap alternative was unmeasured: the EXE's Combat, Particle and Projectile systems back on over the server's picture. This
note measures it once; it changes no default (the product's A'' list keeps the three keys = 1) and informs the owner's decision.
*The owner decided on 2026-11-15 (D147): the beta turns the three back on. Done by issue 0368: the product's list has them on
since then; the next section has its measurements on the product's default list.*

**Set-up.** Workstation, instance c5h-f (F:, Goldberg), 2026-11-15. Product path built from 0cc2d703 on the pin's release
deps (`shell/evidence/0318/build_0318.sh`): server.exe `8d966c3d`, proxy `e4e127a8`, composite `61feda47`, x64 DLL
`74588135` (pin x64-04b1211b); `[Renderer] ServerRenderer = 1`, the shipped composite ini. Tiny copy `C5H-0318-TINY_0009V`
of `C5H-3600-TINY_0009V` (SHA-256 27420A41, turn 9), cp+vp+eui, Vibrant Textures present. Driver
`shell/evidence/0318/run_0318.py` on run_3804's chain (prep and restore by SHA-256); chain `chain_0318.sh`. Arms, each its
own launch:

- **stock**: the 32-bit pin i0ship-26234371-ia in the CP slot, `darkred --arm stock`, no proxy, no server; 0 turns (the
  look reference).
- **product**: config.ini by darkred's list in the A'' form (keys = 1: OverlayDebug, Decal, Forest, Combat, Particle,
  Projectile, FOW, FluidFOW, YieldIcons, DistanceFog, ColorKeyEdit; River and Arrows stay 0, the proxy clears their view
  bits); Quick Combat ON (the product has no Combat system; a human attack with Quick Combat off there is issue 0316's run).
- **combat**: the same with a test copy of the key list: Combat = Particle = Projectile = 0; Quick Combat OFF. The proxy's
  session rule still says D33 (the other rule keys are off; `proxy_session.cpp` Classify) and its view-bit rule lets the
  three view bits follow the config (`proxy_shell.cpp` Apply).

In-game step of every arm (before the first AI turn): units asleep, Lua InitUnit makes an Archer 2 plots from a barbarian
Warrior (run_3804's pair search: Archer at 50,14, target 49,16 on this save) and a Warrior next to a second barbarian
Warrior; each attack is ordered through the UI's own route (Lua `UI.SelectUnit`, then `Game.SelectionListGameNetMessage`
PUSH_MISSION RANGE_ATTACK / MOVE_TO), camera LookAt the pair's midpoint with 3 wheel ticks in, popups closed first (D114),
then 10 harness shots (one every ~2.2 s from 0.5 s after the order). Then 10 AI turns (product and combat arms) with EXE
memory per turn; LOUD lines counted in server.log, render.log, vpproxy.log and the composite's log.

**Result in short [L, workstation, 2026-11-15 00:46-01:26 EST; pair 1 with 10 AI turns, pair 2 with 0 turns]: with the
three keys on, the EXE plays the combat over the server's picture as stock does (the combat camera moves to the fight, the
archers' arrows fly, the figures strike, are hit and fall, the melee figures mix), both attacks resolve with stock's numbers,
10 AI turns (9 to 19) run clean, and the EXE costs +15 to +51 MB private after the load (two pairs; +62 to +64 MB over
the turns of pair 1) with a largest free block 24-27 MB smaller after the load and 72 MB smaller after the turns. What
stays: the proxy logs a LOUD line on every visualised combat (`GameplayUnitCombat` is not yet in the borrowed-argument
list: 13 lines in pair 1, 2 in pair 2; harmless by issue 0316's reading of the handler, silenced by adding it to the list),
and what the server's picture lacks under the fight (trees, unit shadows) stays lacking.**

| | stock (0 turns) | product (A'', Quick Combat on) | combat (3 keys on, Quick Combat off) | combat - product |
|---|---|---|---|---|
| reached the map, turns, end turn, harness stop rc | yes, 0, 9, 0 | yes, 10, 19, 0 | yes, 10, 19, 0 | |
| guard checks (start, in game, end), start-up watch | 0, -, 0 | 0, 0, 0, ok | 0, 0, 0, ok | |
| crash box, Lua errors in the step | none, none | none, none | none, none | |
| LOUD lines: server.log, render.log, vpproxy.log, composite log | (no proxy) | 0, 0, 0, 0 | 0, 0, **13**, 0 | |
| ranged attack (Archer 50,14 at Warrior 49,16): target damage, attacker out of attacks | 27, yes | 27, yes | 27, yes | |
| melee attack (Warrior 52,15 onto Warrior 53,16): attacker / target damage | 22 / 41 | 22 / 41 | 22 / 41 | |
| EXE private MB / largest free block MB after the load (+20 s) | 1,724.7 / 1,470.1 | 1,234.7 / 1,984.4 | 1,285.4 / 1,960.1 | **+50.7 / -24.3** |
| the same after the two attacks | 1,710.3 / 1,470.1 | 1,237.3 / 1,984.4 | 1,287.8 / 1,960.1 | +50.5 / -24.3 |
| the same after turn 10 | | 1,262.6 / 1,984.4 | 1,326.5 / 1,912.3 | **+63.9 / -72.1** |
| mean over the 10 turns (private / largest free) | | 1,260.6 / 1,984.4 | 1,322.8 / 1,949.0 | +62.2 / -35.4 |
| server.exe private MB after the load / after turn 10 | | 640.6 / 657.5 | 643.6 / 658.0 | |
| seconds per AI turn, turns 10-18 (mean, max) | | 5.1, 6.0 | 11.7, 23.6 | |
| **pair 2** (combat launched first, 0 turns, burst from 2 s): EXE private / largest free after the load | | 1,264.3 / 1,976.5 | 1,279.8 / 1,949.3 | **+15.5 / -27.2** |
| pair 2: LOUD lines in vpproxy.log; attacks' results; guard checks; stop rc | | 0; as above; 0, 0, 0; 0 | **2**; as above; 0, 0, 0; 0 | |

- **Memory:** the 2026-11-09 single-key probes (huge save) gave Combat +12, Particle -43, Projectile +1, so "at least 43 MB"
  [L for those probes]. Measured together on Tiny through the product path, after the load: **+50.7 MB private (pair 1) and
  +15.5 MB (pair 2), mean +33 MB**; the largest free block **-24.3 and -27.2 MB**. Over pair 1's 10 turns +62 to +64 MB
  private and -72 MB of largest free block at turn 10. The product arm alone read 1,234.7 and 1,264.3 MB in its two loads,
  so a single load's noise is ~30 MB here (section 2: ~20 MB on the huge save) and the private difference is known only to
  that precision; the largest free block's loss is the steadier number. The product's EXE still has its largest free
  block about 450-510 MB above the stock game's on the same instance and save (stock 1,470-1,535 MB, two loads). The stock
  column is the second stock run (the first, kept as `stock-r0`, read 1,707.9 / 1,534.6 after the load). Not a claim of
  speed or of a fixed cost: two loads per arm, Tiny only (PERFORMANCE.md: an indication).
- **The picture** (DIAGNOSTIC, Visual Checks below; side by side `F:/c5-64bit-work/0318/captures/tiny/compare_*.png`):
  in the combat arm the EXE's combat camera moves to the fight and back exactly as in stock (same framing at 0.5 s and at
  3-4 s; the product arm's camera only centres on the selected unit), the target Warriors are struck and fall, the melee
  figures run into each other and fight; all drawn by the EXE over the server's terrain
  [L]. **The arrows (Projectile) are drawn too [L]:** stock shows the arrow trails 2.6 s after the order; pair 1's combat
  shots fell at 0.5 and 3.7 s and missed them (shots take 2.3 s in stock and about 3 s joined through the harness), so pair
  2 started its burst 2 s after the order and caught the trails at 2.6 s, in the same place and shape as stock's
  (`compare_ranged_arrows.png`, `vc-20261115-012619-workstation-9499`, pass). No particle effect was recognisable in any
  arm's samples (stock included), so the Particle system's picture is not established. What the combat does not bring back: the
  server's terrain under the fight has no trees (Forest stays off, increment 3 work) and the figures cast no shadow on it,
  as before; the gap of "unit shadows" in the sheet is not closed by these keys.
- **AI turns:** with Quick Combat off the AI's visible combats are animated too, so turns take longer (11.7 s mean against
  5.1 s, up to 23.6 s) [L]: a property of the option (stock behaves the same), not of the keys. The AI's 11 animated
  combats ran through the same `GameplayUnitCombat` route without a crash or a stall.
- **The LOUD lines:** every visualised combat, human or AI, calls `ICvEngineUtility1::GameplayUnitCombat(ICvCombatInfo1 *)`
  (pair 1: 13 calls, 2 of them the step's attacks, 11 in the AI turns; pair 2: the 2 attacks; the product arms, Quick
  Combat on: none), and the proxy hands the EXE a temporary wrapper of the DLL's
  combat info that it frees after the call. Pair 1's runs (00:59-01:08) and ticket 0316's (00:39-00:48, Combat key off) are
  the first live calls (`proxy-design.md` 17.4). 0316 read the EXE's handler (slot 161 -> 0x67DD90) by hand: it uses the
  argument only through its getters inside the call and queues a message of values, nothing kept [C]
  (`research/03-dll-engine-boundary/combat-on-the-product-path.md` 4). So the line is harmless here too (13 combats with the
  Combat system on, no crash, no stall, results as stock), and adding `GameplayUnitCombat` to `kBorrowedArgMethods`
  (`proxy/src/proxy_rt.cpp`, 0316's proposal to the MC) silences it for both. *Corrected 2026-11-15 at the merge of main into
  this branch: the first text of this bullet (written before 0316's note was on main) called the handler unread and a
  possible use after free.* *(Done 2026-11-15 by ticket 0329, noted by ticket 0344: `GameplayUnitCombat` and
  `GameplayCityCombat` are in `kBorrowedArgMethods` since commit 18371d8c, so neither logs this LOUD line any more; live
  check with Quick Combat off in `combat-on-the-product-path.md` 6. "0316's proposal" above is no longer open.)*
- Not measured: frame rate during a fight; huge maps; a city attack (`GameplayCityCombat`, the pair's second method, was not
  called: no city combat); air and naval combat; nukes; sound (audio stays in the EXE and was not listened to).

Visual Checks (all DIAGNOSTIC, `info`): stock ranged sheet `vc-20261115-005033-workstation-2650`, frames `-005034-...-3b73`,
`-005035-...-6f51`, `-005036-...-a8a1`, before `-005037-...-13a3`; stock melee sheet `vc-20261115-005144-workstation-619e`,
frames `-005145-...-e8bc`, `-005146-...-447f`, `-005146-...-c038`, before `-005147-...-f333`; product ranged sheet
`vc-20261115-005641-workstation-bbf2`, melee sheet `vc-20261115-005758-workstation-73e8`; combat ranged sheet
`vc-20261115-010351-workstation-f874`, frames `-010352-...-08cb`, `-010354-...-3e33`, `-010355-...-b654`; combat melee
sheet `vc-20261115-010512-workstation-f61d`, frames `-010513-...-8b98`, `-010515-...-4114`, `-010516-...-ae4d`. Pair 2:
combat ranged sheet `vc-20261115-011602-workstation-fdc3` (frame with the arrows `-011605-...-dba4`), melee sheet
`-011741-...-0b26`; product ranged sheet `vc-20261115-012309-workstation-6237`, melee sheet `-012439-...-bc4d`. Side by
side (stock | combat | product, frames 0-2 of each burst): `vc-20261115-012612/-012614/-012615-workstation-*` (ranged),
`-012616/-012617/-012618-workstation-*` (melee); arrows `vc-20261115-012619-workstation-9499`. The first stock run (its
melee step failed on a Lua argument, fixed) left `vc-20261115-004504/-004506/-004507/-004508-workstation-*`.

Evidence: `shell/evidence/0318/` (driver `run_0318.py`, `chain_0318.sh`, `build_0318.sh`, `compare_0318.py`,
`record_compare.sh`, `acceptance.txt`); run folders (run.json, memory.csv, the four logs) and pictures under
`F:/c5-64bit-work/0318/` (moved from C: at the hand-back, D141).

### Combat, Particle and Projectile on in the product (issue 0368, 2026-11-15) [C, L]

**Decision (the owner, 2026-11-15 12:11, D147):** the beta turns the EXE's own combat drawing back on. Since branch
task-0368-beta-exe-combat-on (commit f5419035) `shell/tools/darkred.py` has Particle, Combat and Projectile in `D33_ON`
(were in `D33_OFF`; dated trace comment as 0192 and 3803 left), and `proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2`
has **8 keys**: OverlayDebug, Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons, ColorKeyEdit; their bits
**0x0013C098**, rule bits **0x0003C090** (6: Decal, Forest, FOW, FluidFOW, YieldIcons, DistanceFog), the A'' config mask
**0xFFEC3F67** (were 11 keys, 0x0013F898, 0x0003F890 (9), 0xFFEC0767). The later goal (the ticket's scope) is to switch the
three off again when the server draws combat (0042) and the EXE's unit system is absent (0328), to save their memory.

- **The proxy's session rule [C, session_unit]:** `proxy_session.cpp` `Classify` calls a session D33 when any rule bit is
  clear. The new A'' mask, and the masks written by builds before 0368 (0xFFEC0767), 3803 and 0192 are D33 sessions; a
  config.ini with only Combat, Particle, Projectile = 1 (the player's own choice; the stock mask minus 0x3800) is a stock
  session, and so is one with only Combat = 1.
- **The view-bit rule [C, by reading; L below]:** unchanged. `proxy_shell.cpp` `Apply` keeps a view bit only where the
  config allows it (`dwAllowed = config | ~KNOWN_BITS | keep`) and clears its own bits on top (`ClearBits`: Terrain with
  Waves, and the marker's `clear=`); the three view bits 0x800, 0x1000, 0x2000 are none of those, so with the keys = 0
  they follow their config bits and stay set. River and Arrows (config on, view bits cleared by the proxy) are unchanged.
- **The config.ini routine** (`proxy/helper/proxy_cfgini.cpp`, outside the ticket's `touches`, issue 3805's pattern): the
  former keys are now Waves, Minimap, MovementPath, Particle, Combat, Projectile. A config.ini left `on` by a build before
  0368 (the three = 1, their player values in the side file) goes back to the player's values with `off` (and with D125's
  failed-start restore) and with the new `on`, which never writes 1 into them (cfgini_unit cases `pre0368`,
  `pre0368-3803`, `pre3600-all`, each `off` and `on` then `off`, back to the original bytes). A config.ini whose player
  set the three = 1 by hand: `on` writes only the 6 keys, the side file has no line for them, `off` gives every byte back
  (case `cbplayer`).
- **Quick Combat for the product [C, by reading; L below]:** the product sets nothing. `GAMEOPTION_QUICK_COMBAT` stays the
  player's own option (the setup screen and the in-game options menu, saved with the game; `combat-on-the-product-path.md`
  1); nothing in `shell/tools`, `proxy/helper`, the proxy or the shipped options file writes it (only the server's
  autoplay test harness `srv_main.cpp` and test drivers do). Until 0368 the option made no visible difference on the
  product path (no Combat system: no animation either way, 0316) and 0318 ran its product arm with it on; with the three
  systems on, Quick Combat off plays the EXE's combat as stock does. The beta therefore keeps the stock behaviour: the
  player's setting, off unless ticked.
- **Tests** (2026-11-15 12:17-12:21 EST, each through `quietlock.py run`, 0 skipped; `shell/evidence/0368/tests_0368.sh`):
  test_cfgini 6/6 and under ASan 12/12 (cfgini_unit 160/160, no ASan report); test_proxy_session `--only unit` 9/9 and
  under ASan 15/15 (session_unit 68/68, no ASan report); test_vp64helper 99/99 and under ASan 196/196; pytest
  test_d33_config (a new case: the three are 0 after `config --arm d33` from all-on and all-off), test_minimap_step_a,
  test_product_path 32/32. Negative control (`drift_0368.sh`, 2026-11-15 14:00): with the three put back into darkred.py's `D33_OFF` only (the header
  without them), test_proxy_session `--only unit` fails 4 of 9 (the header-vs-darkred comparison, the D33_ON checks) and
  test_cfgini 1 of 6 (the routine-vs-darkred comparison), both exit 1; darkred.py restored byte for byte (SHA-256 checked).

**Live run [L, workstation, c5h-c (C:, Goldberg), 2026-11-15 12:25-13:59 EST, one launch per arm].** Product path built
from f5419035 on the x64 pin's source and release deps (`shell/evidence/0368/build_0368.sh`, copies of 9045's on C:):
server.exe `d908960e`, proxy `18a5b2b8`, vp64helper `01cc7859`, composite `55ba30f5`, x64 DLL `74588135` (pin
x64-04b1211b); `[Renderer] ServerRenderer = 1`, the shipped composite ini. Huge copy `C5H-0368-HUGE_0350` of
`C5H-VP8P-HUGE_0350` (SHA-256 C499D3C6, turn 350; Quick Combat ON in the save), cp+vp+eui, Vibrant Textures present.
Driver `shell/evidence/0368/run_0368.py` (a wrapper of 0318's `run_0318.py`: prep and restore by SHA-256, the Lua-created
Archer/Warrior pairs near the capital, the attacks by the UI's own route, a burst of 10 harness shots from the order on),
chain `chain_0368.sh`, analysis `memory_0368.py`, figures `compare_0368.py`. Arms, each its own launch:
**stock** = the 32-bit pin i0ship-26234371-ia in the CP slot, `darkred --arm stock`, no proxy, no server, 0 turns (the
look reference); **new** = config.ini by darkred.py's list since 0368 (keys = 1: the 8 above; Combat, Particle,
Projectile = 0; prep's log line `keys_1` and the proxy's boot line `config 0xffec3f67 ... view 0xffffffff -> 0xffcc3f66`:
the three view bits stay set [L]); **old** = the list before 0368 (the three = 1: main's product until this ticket). In
every arm Quick Combat was set OFF by Lua `Game.SetOption` before the attacks (read back false) and stayed off for the 30
AI turns (350-380), so the AI's visible combats are animated in the new arm and memory compares like with like.

| | stock | new (three on) | old (three off) | new - old |
|---|---|---|---|---|
| reached the map, turns, end turn, harness stop rc | yes, 0, 350, 0 | yes, 30, 380, 0 | yes, 30, 380, 0 | |
| guard checks (start, in game, end), start-up watch | 0, -, 0 | 0, 0, 0, ok | 0, 0, 0, ok | |
| crash box, turn end/wait rcs | none | none, 0/0 | none, 0/0 | |
| LOUD lines: server.log, render.log, vpproxy.log, shell_composite.log | (no proxy) | 0, 0, 0, 0 | 0, 0, 0, 0 | |
| human ranged attack (Archer 100,25 at barbarian Warrior 98,25), Quick Combat off: target damage, attacker out of attacks | 36, yes | 36, yes | 36, yes | |
| human melee attack (Warrior 102,27 onto barbarian Warrior 103,26): attacker / target damage | 13 / 53 | 13 / 53 | 13 / 53 | |
| EXE private MB / largest free block MB after the load (+20 s) | 2,002.4 / 1,201.0 | 1,461.1 / 1,782.1 | 1,413.4 / 1,879.0 | **+47.7 / -96.9** |
| the same after the two attacks | 2,003.9 / 1,201.0 | 1,470.8 / 1,782.1 | 1,418.0 / 1,879.0 | +52.8 / -96.9 |
| the same after turn 30 | | 1,597.5 / 1,656.6 | 1,556.1 / 1,685.6 | **+41.4 / -28.9** |
| mean over the 30 turns (private / largest free) | | 1,587.6 / 1,684.7 | 1,544.2 / 1,729.7 | +43.4 / -45.0 |
| seconds per AI turn (mean, max) | | 120.8, 242.3 | 37.2, 165.2 | |
| `ICvCombatInfo1` calls engine -> DLL over the 30 turns (proxy-calls-per-turn.tsv) | | 40,475 | 40,475 | |

- **Memory:** with the three systems on the EXE holds about **+42 to +53 MB private** on the huge save (after the load,
  after the attacks, after 30 turns, and as the 30-turn mean), and its largest free block is **29 to 97 MB smaller** (the
  load's -97 MB narrows to -29 MB after 30 turns, where both arms have fragmented). One launch per arm, and the arms ran
  beside other teams' registered jobs (`quietlock.py check` before each arm: five to seven heavy jobs, game instances of
  4203, 4255 and 0169, 0358's LTO links, 0333's and 0369's suites; logs in `C:/c5-64bit-work/0368/logs`, kept on F:):
  numbers on the workstation outside a window are an indication (COMMON 23), and a single load's noise is about 20-30 MB
  (sections above). The 2026-11-09 single-key probes gave Combat +12, Particle -43, Projectile +1 on the huge save; 0318
  measured +15.5 / +50.7 MB on Tiny. The EXE's largest free block stays far above the stock game's on the same instance
  and save: 1,656.6 MB after 30 turns in the new arm against 1,201.0 MB for stock after the load.
- **The picture** (DIAGNOSTIC, Visual Checks below; figures `C:/c5-64bit-work/0368/captures/huge/compare_*_g0*.png`, kept
  on F:): in the new arm the EXE's combat camera moves to the pair at the order exactly as in stock (the ranged figure at
  0.4-0.5 s: the same framing, median difference of the crop to stock 8 of 255), the archers' firing poses and the melee
  scrum are drawn by the EXE over the server's terrain, the "-53" damage text shows, and the attack resolves as in stock
  [L]. The old arm's frames show neither the combat camera's framing nor the fight (and 6 of its 10 ranged shots were flat
  captures). What the three keys do not bring back: the
  figures cast no shadow on the server's terrain (visible in every crop against stock) and Forest stays off, as 0318 found.
  The arrows were not caught on the huge save (a shot takes about 2.5 s there and the frame at 3.0 s was a PrintWindow
  glitch frame of the joined window: server terrain only); the Tiny pair below catches them.
- **AI turns with Quick Combat off:** the new arm's turns took 120.8 s on the mean (max 242.3 s) against 37.2 s in the old
  arm, because the AI's visible combats are animated (both arms played the same game: the same 40,475 combat-info reads,
  the same attack results). This is the option's stock behaviour, not a cost of the keys, and not a timing claim (one run,
  workstation under load; PERFORMANCE.md); a player who ticks Quick Combat gets the old arm's turn length back [I].
- **No crash, no stall, no LOUD line** in 30 huge AI turns with the three systems on and Quick Combat off, and none in the
  comparison arm; restore by SHA-256: every backed-up file equal (`backup_restore_ok` true, both arms and the stock arm),
  `provision.py verify --quick` rc 0; the staged save copies removed.
- Not measured: frame rate during a fight; a city attack (`GameplayCityCombat`; 0329 ran it with the systems off); air and
  naval combat; nukes; sound; more than one launch per arm.

**The arrows, Tiny [L, c5h-c, 2026-11-15 14:00-14:12 EST].** The same chain on the Tiny copy `C5H-0368-TINY_0009V` of
`C5H-3600-TINY_0009V` (SHA-256 27420A41, turn 9), stock and new arms only, 0 turns, the bursts from 2 s after the order
(0318's pair 2): stock draws the archers' arrow volley 2.4 s after the order and the new arm 2.3 s after it, in the same
place and shape (crop median difference 12 of 255, the rest is the missing trees and shadows); the melee scrum is drawn at
2.3-2.4 s in both, with a white hit flash in the new arm's frame (an effect, by its look [I]). Attacks as stock (ranged 27;
melee 22 / 41), 0 LOUD lines, guards 0, restore by SHA-256 ok. New arm EXE 1,287.3 MB private / 1,961.4 MB largest free
before the attacks (stock 1,709.8 / 1,526.8). Particle effects as such were not identified separately (as in 0318).

Visual Checks (all DIAGNOSTIC; `info` unless marked): huge stock ranged sheet `vc-20261115-123011-workstation-f815`, melee
sheet `-123119-...-e743`, with their frames; new and old arms' sheets and frames recorded by the driver under issue 0368
(12:35-13:40); side by side (stock | new | old, non-flat frames 0-2) `vc-20261115-141244-workstation-0297`,
`-141244-...-5cfc`, `-141245-...-49be`, `-141246-...-9c74`, `-141247-...-5626`, `-141247-...-58a7`; Tiny arrows
`vc-20261115-141248-workstation-7db0` (pass), Tiny melee `-141249-...-11e4` (pass).

Evidence: `shell/evidence/0368/` (`acceptance.txt`, `run_0368.py`, `chain_0368.sh`, `build_0368.sh`, `tests_0368.sh`,
`drift_0368.sh`, `memory_0368.py`, `compare_0368.py`, `record_compare.sh`, `data/`: the analysis JSON, each arm's
combat.json, the combat-info call tallies); run folders, logs and pictures under `F:/c5-64bit-work/0368/` (moved from C:
at the hand-back, D141).

### The shell configuration from one switch: `vp64helper on` alone (issue 0030, 2026-11-16) [C, L]

**The switch [C].** MC ruling 2026-11-16 (one switch): `vp64helper on` writes the A'' keys above into config.ini AND
`VP64Options.ini [Renderer] ServerRenderer = 1`; `off` puts both back (the player's config.ini values; VP64Options.ini
byte for byte from the helper's record `vp64-options-side.ini`, or removed when `on` created it). So no player reaches
D33 with `ServerRenderer = 0`, the game without terrain of `installer-notes.md` 8. Code
`proxy/helper/proxy_cfgini.cpp` (RendererOn / RendererOff / RendererStatus), contract and tests:
`research/06-tooling-and-workflows/installer-notes.md` 9.

**The DoD run [L, workstation, Goldberg instance c5h-f (F:), 2026-11-16].** Release layout built from branch
task-0030-shell-configuration c0e6a28d at the DLL pin 04b1211b8 (`shell/evidence/0030/build_0030.sh`: server.exe
`86911fad`, proxy `4df506b6`, vp64helper `0bebdd24`, composite `02062f90`, x64 DLL `74588135` = 9045's release deps,
the deps every product-path run since 0368 used; not the `29f58e17` rebuild of layout `x64-04b1211b-pfdee656`, which
differs by 0343's .Civ5Map reader stubs and plays the same games, 0351), staged as a player gets it
(`shell/evidence/0030/run_0030.py stage`: `tools/release/pack.py` bound to the tag's own 32-bit DLL `262aee88`, purity
143 kept, the shipped `vpproxy.ini` `70f2c066` with mode=on and pack=1, plus `shell_composite.dll`, its shipped ini and
`shell_composite.sha256`). Steps, each its own command of the driver: `provision.py verify c5h-f` rc 0 (the instance
as its base); every changed or created file backed up by SHA-256 (34 files); (1) Community Patch's DLL = the tag's (the
player's state, as 0324), then the install of `docs/INSTALL-FIRST-PLAYABLE.md` 3 (rename to `_x86`, copy); the game
started once to the main menu (mods activated; the proxy booted the 64-bit path, `boot: pack ... ok`, created
VP64Options.ini with every option at its default, `ServerRenderer = 0 (default)`, `shell: ... no composite, no view-mask
write`; server attached after 1.4 s; harness stop rc 0); then only `vp64helper on` (with `C5_DOCUMENTS` naming the
instance's Documents, the helper's documented test-instance path): `on: ok: ... keys written Decal, Forest, FOW,
FluidFOW, DistanceFog, YieldIcons` (OverlayDebug and ColorKeyEdit were already 1), `VP64Options.ini ...: ServerRenderer =
1 written`, vpproxy.ini unchanged (mode=on already); `status` exit 3 (both files on). config.ini `b494c28f` ->
`1752afd8`, VP64Options.ini `9497f85b` -> `446ae0e1`.

**Result [L, 2026-11-16 12:04-12:24 EST]: the definition of done holds.**

| Step | Result |
|---|---|
| load of the huge save copy `C5H-0030-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, SHA-256 C499D3C6, turn 350; launch 12:04, in game 12:06:46) | `vpproxy.log`: `boot: pack ... ok`, `options: [Renderer] ServerRenderer = 1 (from the file)`, `shell: [Renderer] ServerRenderer = 1: composite ... sha256 02062f90... matches`, `shell: composite ... loaded`, `shell: view mask: active: ... Terrain (0x1) cleared (the composite is loaded)`, `boot (Updates 0): config 0xffec3f67 ... view 0xffffffff -> 0xffcc3f66` (the A'' mask of 0368, Terrain c1), `boot: server attached` (1.8 s); guard checks 0, start-up watch ok |
| the server draws the terrain | `render.log`: 21,810 frames drawn and published over the run, `gray_frames=0`, residency 4,922 land tiles (radius -1), `failed=0`; pictures below (no trees: Forest is off in D33) |
| 10 AI turns 350 -> 360 | every `turns.py end/wait` rc 0; 34.1-52.0 s per turn (one run on the workstation under other teams' load: not a timing result); EXE private 1,590.8-1,634.6 MB; no crash, no dialog (watcher marker clean at each turn), 0 LOUD lines in vpproxy.log, server.log, render.log, shell_composite.log; no D125 report folder; harness stop rc 0, leftovers 0 |
| `vp64helper off` | `off: ok: ... restored Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons; VP64Options.ini ...: put back ServerRenderer; ... mode=off`; `status` exit 0; config.ini `b494c28f` and VP64Options.ini `9497f85b` = their SHA-256 before `on`, both side files gone. The game session left config.ini as `on` wrote it (`1752afd8` before `off`, as after `on`) |
| one start after off (12:17-12:23) | `Logs\VP64\vpproxy.log`: `mode=off: the original 32-bit DLL ...CvGameCore_Expansion2_x86.dll loaded ...; no server is started`; the same save at its own camera, the load screen's button by a harness click (the tag's DLL has no Lua channel), the EXE draws the terrain with trees (the stock picture); stop through `stop_nolua` rc 0; config.ini after the start still `b494c28f` |
| restore | backup restored by hash (34 files, created ones removed), front end restored, this run's 16 autosaves and its two server run folders moved out (kept under F:), `provision.py verify c5h-f` ok (no error, no warning); claim released |

- Pictures (Visual Checks, all DIAGNOSTIC: built from a branch, not a release candidate, D112): first start at the menu
  `vc-20261116-120129-workstation-9d26`; on, at the load `vc-20261116-120713-workstation-13e8`; on, after 10 turns
  `vc-20261116-121551-workstation-7813` (the player's capital Asyut had been taken during the turns, its plots fogged,
  by the picture [I]); off, the stock picture at the same save and camera `vc-20261116-122255-workstation-c49c` (pass).
- Not shown here: a pixel comparison of the on and off pictures (the terrain's look is the renderer epics' work; this
  run checks who draws); a start of a new game in the shell configuration; the installer (epic 2600) calling the
  helper; the 29f58e17 DLL of the pinned layout (see the build line above).
- The run's popup check (`run_3804.close_popups`' rule: Esc until the top middle of the picture is bright) counts fog
  of war as a popup: after the turns it pressed Esc 7 times over a fogged area. Nothing was open at the shot, but a
  check by brightness is not one for fogged views.
- Evidence: `shell/evidence/0030/` (`acceptance.txt`, `build_0030.sh`, `run_0030.py`, `data/*.json`); the run's logs,
  pictures and the instance's moved logs under `F:/c5-64bit-work/0030/` (D141).

### Strategic View with the StrategicView system off hangs the EXE's main thread (issue 3008, 2026-11-11) [L + C]

**Product consequence: in the D33 shell configuration a player pressing the strategic view key (or any other route to
view mode 2) would hang the game for good. The shell must block or handle every route to view mode 2 while the
StrategicView system is off (bug 3010).**

- Reproduction (once, not repeated): instance c5h-b, D33 configuration (config.ini `[DisableSystems]` as in 3003,
  StrategicView = 1; marker `viewmask.enabled` `clear=0x1`), DLL `i2-shell-test` b92373aeb, CP/VP/EUI, huge save
  `C5H-VP8P-HUGE_0350` t350, shell_composite.dll loaded (composite mode, grid server). At 04:11:25 EST
  `ToggleStrategicView()` ran on the main thread (one-shot `Events.LocalMachineAppUpdate` handler armed from the Lua
  channel; camera at Memphis 100,30, zoom 11). The handler never reported back.
- Symptom [L]: no Present after 04:11:24.9 (the composite DLL's last stats line; no `camera: status` change was ever
  logged, so UpdateViews never returned to the Present), Lua channel silent ("request never picked up"), window not
  responding, the main thread (tid 0xE54C, the thread that created the window and presents) at 100 % of one core
  (5.2 s CPU in 5 s), the seven worker threads waiting. No dialog, no crash box.
- Where [C from three non-invasive `cdb -pv` stacks, 04:13-04:16, EXE base 0x00FE0000 rebased to 0x400000]: eip
  cycling 0x8044F4 / 0x8044F8 / 0x8044FB (`subss xmm1,xmm0` / `movss [eax],xmm1`, the stored value 0x7F800000 = +inf;
  +inf and -inf (0xFF800000) on the stack); return addresses 0x8043BA, 0x46B192 (inside 0x46B150, the StrategicView
  object's view-count function), **0x51E306 (inside `GameViewState::UpdateViews` 0x51DF70, the view-mode-2 path that
  sets `g_NumViews` from `0x1A0ADA0->0x46B150`, camera-and-picking.md section 4)**, then the frame loop. No
  shell_composite.dll frame on the stack. Reading [I]: with the system off, the strategic view's extents are never set
  and stay at +-inf, and a loop that steps from -inf to +inf never ends. Stacks:
  `D:/c5-64bit-work/3000/live/w5/hang/stack{1,2,3}.txt` (with the module list), copies in
  `shell/evidence/3000/live/hang/`.
- Ended at 04:16:56 by `cli.py stop --coordinator-override` on the MC's decision (the harness's own stop refused:
  channel silent). Hung and unusable 5.5 min. Leftovers clean.
- Not tested: the strategic view key (posted), the minimap button, StrategicView system on with Terrain kept on
  (whether D33 + StrategicView on is enough is open; costs per the B4 table), whether the DX9/Tablet EXEs behave alike.

#### Routes into view mode 2 and the options against the hang (bug 3010, 2026-11-13, by reading) [C source, I]

Every route a player or a mod has into view mode 2 [C, read 2026-11-13 in the toolkit game folder
`VP_MODPACK_5_4_8_NEUTRALS` and the DLL source at pin 76992d235]:

| Route | Path | Reachable from Lua? |
|---|---|---|
| F10 key | `CIV5Controls.xml` row `CONTROL_TOGGLE_STRATEGIC_VIEW`, `HotKey KB_F10`; the EXE's hotkey dispatch calls the DLL's `CvGame::doControl`, which calls `GC.GetEngineUserInterface()->ToggleStrategicView()` (`CvGame.cpp:3558-3559`) | no: C++ from the EXE through the DLL; only by removing the hotkey row from the database or by a DLL change |
| `Game.DoControl(CONTROL_TOGGLE_STRATEGIC_VIEW)` from any Lua | same DLL path | no (same) |
| Minimap button | `MiniMapPanel.lua` calls the global `ToggleStrategicView()`: base `Assets/UI/InGame/WorldView`, `DLC/Expansion2`, the CP override, `VP_MODPACK.../UI`, EUI `UI_bc1/MiniMap` (EUI and VP cache it at load: `local ToggleStrategicView = ToggleStrategicView`) | yes, per context |
| Esc while in the view; restoring a player's saved view on an active-player change | `WorldView.lua` (base, Expansion, Expansion2, CP override, VP override, EUI `UI_bc1/Improvements`), lines 113 and ~851-869 in the base file | yes, per context |
| Multiplayer staging room | `StagingRoom.lua:179` (base and CP override) | yes |
| A mod | any `ToggleStrategicView()` or `Game.DoControl` call | per mod |

Options [I unless marked]:

| | Option | Covers | Cost | Open |
|---|---|---|---|---|
| A | **Keep the StrategicView system on in D33** (drop `StrategicView` from the D33 off list, `shell/tools/darkred.py` `D33_OFF`) | every route, mods included: the system's start-up sets the extents the loop needs | memory: none measurable. Single key alone 2026-11-09 (section 2, row 18): **+9 MB** private with it off, noise ~20 MB, one run; it is not one of the systems that carry D33's saving (FluidFOW -294, Particle -43, City -29, Forest -17). Frame cost outside view mode 2: none expected (its object only works in mode 2) [I]. One config line, reversible | live: that D33 + StrategicView on loads, that F10 / the minimap button / `ToggleStrategicView()` enter and leave view mode 2 without a hang, what view mode 2 shows with Terrain, Overlay, FluidFOW and Minimap off, memory against D33. The compositor already passes view mode 2 through (`shell/README.md`: view mode 2 = `NOT_MAP`) |
| B | Block every route in Lua | the Lua routes only; F10 and `Game.DoControl` need the hotkey row removed from the database as well | no memory; 6+ file overrides across base, Expansion2, CP, VP and EUI contexts (each context has its own globals; EUI and VP cache the function at load, so a global override must load first); breaks on every mod update; removes a feature | coverage can never be shown complete (any mod) |
| C | DLL guard in `CvGame::doControl` | F10 and `Game.DoControl` only | a fork change and a pin move (MC); the DLL must know the system is off | still needs B for the Lua routes |
| D | A data write that gives the off StrategicView object finite extents, so that `0x46B150` ends | the hang only | decompile of the object at `0x1A0ADA0` and of `0x46B150` first | view mode 2 would draw from an object that never started: a blank or a further fault is likely; no code-byte change (D4) but unproven |
| E | The server draws the strategic view | all | D81's order lists "minimap and strategic view" after the join and bake stage 1 | the long-term answer; does not help increment 2 now |

Recommendation (bugfix team 4000, 2026-11-13): A, after its live check; E later per D81. A changes the D33 system list
(not its measured saving), so it is the MC's call; B, C and D are kept only as fallbacks if A fails live. A new negative
run reproduces a hang that the harness cannot stop without a coordinator's override, so it needs the MC's go; the 3008
reproduction above stands as the failure on the old configuration.

#### Option A implemented and checked live (bug 3010, 2026-11-13) [L]

**D33 changed (MC decision 2026-11-13 03:11): the StrategicView system stays ON.** `shell/tools/darkred.py`: `D33_OFF`
no longer holds `StrategicView` (it is in `D33_ON` and in `D33_NEVER_OFF`); `--on StrategicView` is still accepted and
changes nothing. Test `shell/tests/test_d33_config.py` (the tool on a config.ini in a temp folder, from all-on and
all-off: StrategicView = 0 after `config --arm d33`; against the old tool it fails with `assert 1 == 0`). D33's masks
are now config **0xFF040663**, view **0xFF040662** (was 0xFF000663 / 0xFF000662; 0x40000 = StrategicView).
`tools/i0b/set_disable.py` is unchanged: its `B4` preset is the 2026-11-09 B4 configuration of this section's tables,
not D33, and keeps its historical definition.

Live, c5h-c (Goldberg, cloaked, guard self-test ok, start-up watch ok), DLL `i2-shell-test` b92373aeb (SHA-256
638B5E21...C83B4), CP/VP/EUI, huge save `C5H-VP8P-HUGE_0350` t350, the new D33 (`darkred.py -i c5h-c config --arm
d33`, marker `clear=0x1`), no compositor, 2026-11-13 03:44-03:49 EST. Every route entered and left view mode 2 with
the Lua channel answering within 0.12 s after each step (`InStrategicView()` read on the channel):

| Route | Enter | Leave (Esc posted) |
|---|---|---|
| `ToggleStrategicView()` on the main thread (InGame state, one-shot `Events.LocalMachineAppUpdate` handler) | in view mode 2 after 3 s | back to the map |
| the same call from the `MiniMapPanel` context (what the minimap button's handler calls; the button itself was not clicked) | yes | yes |
| F10 posted to the window (`input.key`, the engine route through `CvGame::doControl`) | yes | yes |

What view mode 2 shows with Terrain kept on and Overlay, FluidFOW, Minimap off: the full stock strategic view (flat
terrain hexes, water, borders, city banners, unit and resource icons), shot `D:/c5-64bit-work/4000/3010/
strategic_lua.png` (not committed; 2554x1411). Memory (private MB, `memory.py`, one run): 1,619.5 after load,
1,627.1 inside view mode 2; D33 as it was (StrategicView off) 1,641-1,653 after the same load (runs R0, W1, X1 above):
no cost measurable (noise about 20 MB). Instance restored by hash afterwards (config.ini, DLL back to i0ship-bbfacca1-ia
e589306e, front end restored, marker and viewmask.log removed; `provision.py verify c5h-c` ok, `leftovers` clean).
No new hang run (the 3008 reproduction is the failure on the old configuration). Not checked: a real click on the
minimap button, `Game.DoControl`, the multiplayer staging room, DX9/Tablet EXEs. *(2026-11-15, issue 0035: on the beta
product path (server, proxy, composite, ServerRenderer = 1) F10 and the MiniMapPanel-context call enter and leave view
mode 2 the same way, and the picture there is stock's within stock's own launch-to-launch difference; section
"Minimap and strategic view on the beta product path against stock".)*

### Hex highlights with the Overlay system off can crash the EXE (bug 0127, one-off 9046, 2026-11-13) [C + L]

**Product consequence: in the D33 configuration (Overlay = 1) every named-style `Events.SerialEventHexHighlight` is a
lottery: the EXE looks the style up in a map that the Overlay system never filled, the lookup inserts a style record
copied from uninitialised stack, and the handler then `strcmp`s a pointer from that record. When the stack word is not
a readable address the EXE dies with 0xC0000005 at VA 0x574B03, with no dialog and no dump on the proxy route.** This
was the "EXE crash at the first pan" of 3408 (3 of 5 huge launches, team-3400's branch, `terrain-bake-residency.md`
6.6 there).

- **No hang [L, logs of 3408].** The x64 server's log puts each EXE death at the moment presenting stopped: pid 3208,
  server boot 10:43:01 + 178.158 s = 10:45:59.2, the composite's last stats line 10:45:58.9, shell_server's last consumed
  frame 10:45:59; pid 63048 11:00:56 + 168.86 s = 11:03:44.9; pid 5444 08:47:52 + 144.80 s = 08:50:16.8. The driver's
  next step already got `OpenProcess(3208) failed: 87`. The "Lua answered, then ~80 s later the crash" of the 3408 log
  is the driver's own timeouts (10 + 10 + 60 s) after the death. *Corrects* the 3408 reading "the render thread hangs
  first, then the process faults" (team-3400 owns that file and was told).
- **Where [L, gbe crash printer].** `c5h-b/run/gbe/crashes.txt`: 0xC0000005 @ 0x003F4B03 (13:50:16 UTC), 0x009B4B03
  (15:45:59), 0x00794B03 (16:03:44); the composite logged the EXE bases 0x00280000 / 0x00840000 / 0x00620000 for those
  pids: **EXE VA 0x574B03 all three times**. On the proxy route the DLL's crash handler is not in the process (the
  proxy is the game DLL), so the gbe crash printer is the only record (section 9 of `run-and-drive-the-game.md`).
- **The code [C, disassembly of the DX11 EXE, llvm-objdump; dump below].**
  - 0x574A00 (thiscall, one argument) is a handler on `g_EventSystemLocalMachine` 0x1A0E400 (subscribed by the stubs
    0x861B10 / 0x862EF0 from the static initialiser 0x9C6730): the `SerialEventHexHighlight` record = hex x/y/z at
    +0/+4/+8 (to grid by 0x56D480 on 0xD91130, bounds 0xD911C4 / 0xD911C8), colour floats +0xC..+0x14, on-flag byte +0x1C,
    style name chars from +0x1D. Per-plot entries of 0x188 bytes at this+0x4C (width this+0x44, height +0x48; a static
    entry 0xC40268 when out of range).
  - With the flag on it calls 0x618EB0(&name) on 0xCF0A48 = intern the name, then 0x755430 = the map's `operator[]`.
    On a miss 0x755430 builds the new pair with 0x459110 from its own local at [esp+0x10] that **nothing initialises**
    (0x9C bytes of whatever the stack held) and inserts it (0x84B460). The returned record's first word is stored at
    entry+0x1C and then compared with `"ValidFireTargetBorder"` (0xA34C94) and `"AMRBorder"` (0xA34CAC) by an inline
    `strcmp`: **0x574B03 `mov al,[ecx]` with ecx = that word**.
  - The map is filled only from `Highlights.xml` (style parser around 0x4FC6xx: `model`, `color`, `width`; errors
    "Style missing name", "Could not load highlight xml file") by 0x6DBE40, called from 0x6316C0
    (`HighlightSystem.cpp`), which only the two Overlay start-ups call (0x57D2D6, 0x6FFFAE; both also create the Terrain
    Overlay target 0xDA12A4). Overlay off = no start-up = empty map [C for the calls; I that the Overlay key is the gate,
    L below that the map is empty].
- **Live [L, 2026-11-13, c5h-b, huge save copy `C5H-9046-HUGE_0350` t350, CP/VP/EUI, D33 on the proxy route of 9040
  (proxy 8F10D815, server.exe 77877FB1, x64 DLL 12B7F4FB; no composite, no shell_server), EXE pid 3560 base 0x00C60000].**
  Style map read by PID (`ReadProcessMemory`): size word (0xCF0A48+0xC) **0 in game**. Ten Lua-channel calls
  `Events.SerialEventHexHighlight(ToHexFromGrid(Vector2(101,25)), true, Vector4(1,0,1,1), "<style>")` with Culture,
  OwnedFill, OwnedOutline, CityLimits, WorkedFill, WorkedOutline, EnemyFill, EnemyOutline, GroupBorder,
  MovementRangeBorder: size 0 -> 1 -> 10, one insert per new style; the EXE survived all ten. Non-invasive dump
  `D:/c5-64bit-work/9046/dumps/c5h-b-x86-3560-20261113-113832.dmp` (1,828,157,939 B, SHA-256 c39cf292...) [C]: the
  'Culture' record's first word is **0x774C0C30 = `ntdll!_except_handler4`**, a stale stack word that happened to be
  readable (code bytes), so `strcmp` did not fault. In 3408 the calls came from the UI context (below), with other
  stack contents, and the word was not readable.
- **What sends the highlights at a pan [C, mod Lua; I that it was the trigger in 3408].** EUI's city banners
  (`(3a) VP - EUI Compatibility Files/LUA/CityBannerManager.lua`): `Mouse.eMouseEnter` on a banner ->
  `RefreshCityBanner` -> `SerialEventHexHighlight(..., "Culture" / "WorkedFill" / "OwnedFill" / "CityLimits" / ...)`.
  A pan moves the banners under the (stationary) cursor position the game holds; the first pan from the capital (101,
  25) moves its banner. Whether a banner passes the cursor, and whether the stack word is readable, decide each launch:
  hence 3 of 5. Other senders with styles: EUI `PlotHelpManager.lua` (CityLimits / OwnedFill / OwnedOutline when the
  selected unit can found a city), VP `WorldView.lua` (GroupBorder), base `InGame.lua` / `Bombardment.lua`
  (MovementRangeBorder, FireRangeBorder, ValidFireTargetBorder, AMRBorder). The default style `""` goes the same way.
- **The game itself inserts during the 3408 camera sequence [L, arm D, same route and save, pid 54080, 11:47-11:48].**
  With no highlight sent by us: style map size 0 after the load, **1 after the first `UI.LookAt` (101,25) + pan
  (102..111, 25)** of the 3408 script (Lua channel, 0.15 s per step), cursor parked by posted moves (`cli move`) at
  client (0.50, 0.50); unchanged over five more passes with the cursor at (0.50..0.30, 0.45). The inserted record's
  first word was 0x0753F5A4, a stack address (readable), so the EXE lived. Which Lua sent it was not identified [I:
  the banner path above].
- **Fix check: D33 with the Overlay system kept on [L, arm C, same route and save, pid 62684, 11:43].** Config mask
  0xFF040667, view 0xFF040666 (the proxy's view-mask write as usual). The style map holds **43 styles after the load**
  (the VPUI `Highlights.xml` list) and stays at 43 through the same ten named-style calls: every lookup hits, nothing is
  inserted, no uninitialised record can exist. EXE alive throughout. Memory and picture of D33 + Overlay were not measured
  here; section 5's R3 row (Overlay on alone, dark red unchanged, 1,636 MB, -17 = noise) is the existing figure.
- Not the server, not the residency, not the composite, not the proxy: none of them is in the faulting code or its data;
  arm B above had neither composite nor shell_server [L for the insert, C for the fault address].
- **Owner and fix.** Owner: the D33 configuration's key list (`shell/tools/darkred.py` `D33_OFF`, decision D33), not a
  code defect of the server, the residency or the proxy. Fix (recommended, same pattern as bug 3010's StrategicView):
  **keep Overlay on in D33** (drop it from `D33_OFF`; arm C). Alternatives, not tried: leave Overlay off and fill the
  style map without its start-up (would mean calling EXE code from the proxy); unsubscribe 0x574A00 from the event
  (a data write into the event system's handler list; also drops every highlight); or swallow
  `SerialEventHexHighlight` in mod Lua (does not cover every context or the EXE's own callers). Answered 2026-11-13
  (team-3300, D93 applied; `research/05-graphics-composition/renderer-in-server-design.md` 4.11, 3311 s1 arm) [L]: with
  Overlay kept on, D33 no longer crashes (0 in 12 launches, six with pans), but the EXE still draws **no** hex
  highlights, selection or movement range under D33, while stock does; the server must draw them (bug 0138). *Corrected
  2026-11-14 (epic 0167, `research/05-graphics-composition/overlay-system.md` 1-3, [C+L]): the EXE draws them into the
  Terrain Overlay target under D33 (the Overlay render keeps its own view bit); they are invisible because the terrain
  draw that samples the target is off. Epic 0167 taps the target for the server.* (Replaced
  2026-11-13: "Not verified: whether the EXE then draws highlights / borders into its own layer under the composite".)
- Side finding [L]: D33 on the **stock 32-bit route** (no proxy, so nothing writes the view mask) crashes at the end
  of the load at VA 0x8CD492, the River single-key crash of section 2 (c5h-b 11:24:19, DLL i0-ship `-ia` e589306e;
  minidump kept in `D:/c5-64bit-work/9046/armA/`). D33 needs the view-mask writer of the proxy route (or of the
  `i2-shell-test` DLL; at the current pins `i2-shell-test-26234371`, section 4 "Which 32-bit DLLs honour the marker",
  issue 0171: no `-ia` build has the writer).

## 6. Tools

`shell-gates/tools/disable_systems.py` edits `config.ini` with a hash-verified backup in
`%USERPROFILE%\Documents\shell-gates-backup` (`config.ini.orig` + `SHA256.txt`):

```
python disable_systems.py backup                 # once; refuses to overwrite a differing backup
python disable_systems.py set B4                 # preset: A4, B4, A5, FluidFOW, none
python disable_systems.py set FluidFOW Particle  # explicit keys to 1, other keys unchanged
python disable_systems.py show                   # current disabled keys + whether config.ini equals the backup
python disable_systems.py restore                # copy back and verify SHA-256 (exit 1 on mismatch)
```

`set` refuses unless `config.ini` currently equals the backup, so presets never stack. Presets in the script:
`A4` = the 12 keys above, `B4` = A4 + Overlay OverlayDebug StrategicView Minimap ColorKeyEdit, `A5` = A4 minus
FluidFOW. The restored file hash quoted in the write-ups is `7b53b17c3935702d...`.

`shell-gates/tools/view_mask.py` reads or clears bits of `[[*(0xCC8E74)]+0xC0]+0x1B4` in the running DX11 game with
ReadProcessMemory / WriteProcessMemory (the out-of-process twin of the DLL stub):

```
python view_mask.py show
python view_mask.py clear 0x400100 [--wait 300]   # River (0x100) + Arrows (0x400000)
```

It finds the process by `tasklist`, the EXE module with `EnumProcessModulesEx`, checks TimeDateStamp 0x546CD0A8,
rebases 0xCC8E74, polls every 0.2 s until `app` and `view` are non-NULL (up to `--wait` seconds), prints
`mask@<addr> = <value>`, and for `clear` writes `cur & ~bits` and reads it back (`OK` / `FAILED`). Use `--wait` so
the write lands right after the GVS exists and before the first world frame.
