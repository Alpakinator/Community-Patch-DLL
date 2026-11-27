# EXE address table

Sources: every file of this section's sources that names an EXE address: `exe-world-subsystems/PROBES.md`,
`exe-world-subsystems/allocsites.txt`, `exe-world-subsystems/imports.txt`, `shell-gates/GATES.md`,
`shell-gates/STUBS.md`, `shell-gates/stubs/{terrain,unit,landmark,river_arrows,coupling}.md`,
`shell-gates/CAMERA.md`, `shell-gates/alloc-trace-README.md`, `shell-gates/tools/view_mask.py`,
`memory-notes/civ5-exe-disable-systems.md`, `memory-notes/civ5-exe-message-queue-overflow.md`,
`memory-notes/units-myth-plan.md`, `memory-notes/civ5-memory-investigation-log.md` (item 30),
`shell-gates/LOADTIME.md` (rows added 2026-11-10).

## Conventions

- **Build**: `CivilizationV_DX11.exe`, PE TimeDateStamp **0x546CD0A8**, Steam build id 4390913, "App Version 403694
  FINAL_RELEASE" (the project's "1.0.3.279"), SHA-256 `b2ce96aefcf27d1ed6d5d021c5ad1e4fdb0fd01628e115da880596f00670c88e` (TOOLING.md). Every address in the main table is for
  this build unless the row says otherwise. DX9 `CivilizationV.exe` is 0x546CCB59, Tablet `CivilizationV_Tablet.exe`
  is 0x546CD5F8.
- **ImageBase 0x400000.** All values below are VAs at that base. RVA = VA - 0x400000. The EXE is often relocated at
  run time (dumps show it at 0x240000, 0x480000, ...), so live tools rebase on the module base and check the
  TimeDateStamp first (`view_mask.py` does).
- **The file-offset trap.** `crashes.log` "Location (in file)" is a **file offset**, not an RVA. `.text` has raw data
  at file offset 0x400 and VA 0x1000, so for `.text`:

  **VA = file offset + 0xC00 + 0x400000 = file offset + 0x400C00.**

  The dump is the authority: EIP - module base = RVA. The stack RVAs that `crashes.log` prints (e.g.
  `CivilizationV_DX11+0x2a59a5`) are real RVAs. The memory notes' `EXE+0x...` values read with ReadProcessMemory or
  from the trace logs are RVAs (add 0x400000).
- **Confidence**: C = confirmed in decompile, disassembly or dump; I = inferred; L = verified live. A row inherits
  the strongest tag its sources give; "[I]" inside a description marks an inferred part of a confirmed row.
- **Source keys**: P = PROBES.md; DS = civ5-exe-disable-systems.md; G = GATES.md; S = STUBS.md; T = stubs/terrain.md;
  U = stubs/unit.md; L = stubs/landmark.md; RA = stubs/river_arrows.md; C = stubs/coupling.md; CAM = CAMERA.md;
  AT = alloc-trace-README.md; AS = allocsites.txt; IM = imports.txt; Q = civ5-exe-message-queue-overflow.md;
  UM = units-myth-plan.md + investigation-log item 30; GT = game-core-thread.md (issue 0335, 2026-11-15, read from 0317's stall dumps and the EXE file); PP = the client's update pacing, `game-core-thread.md` 6 (4656: static, 2026-11-16, Steam EXE `b2ce96ae...`, ImageBase 0x400000; 4661: live read-only poll, 2026-11-17, workstation, S2i on M); VM = tools/view_mask.py; LT = LOADTIME.md (2026-11-10;
  L rows are main-thread frames from live `cdb -pv` stack samples of the huge-save load, raw `dps` scan).
- **Build 26577625 (Firaxis' update of 2026-11-19, stamp 0x6AF3837D)**: every address in this file is for build
  4390913 (stamp 0x546CD0A8). In the new build every function and global moved (re-linked); structure offsets did not.
  Carry an address with `evidence/0582/addr_match.py` and check it with `pattern_check.py`; the carried product
  addresses are in the local per-build table (D202: stored locally only, never in git; see `tools/exebuild/` once epic 0590 lands; removed from git 2026-11-19), the facts in `new-exe-build-26577625.md` (ticket 0582).
- **Other copies of the EXE** (2026-11-10, issue 1101): every address here is for the Steam EXE `b2ce96aefcf27d1ed6d5d021c5ad1e4fdb0fd01628e115da880596f00670c88e`; before any address or byte-pattern work on a Goldberg (`pure_vp`) instance, diff its EXE against that one first - for pure_vp's `04cfdc71...` only per-user CEG data differs (`.text`, `.data`, `.reloc` and the import table are byte-identical; `.rdata` differs in a 580,221 B blob at VA 0xAB4D60-0xB4285D and 16 small values between VA 0x9EBF40 and 0xAB4654, plus 16 B of `.rsrc`, the DOS-stub `VLV` block, checksum and signature), details in `../06-tooling-and-workflows/headless-and-parallel-testing.md` section 3.2 [C].

## Crash sites: file offset -> VA

| Probe | crashes.log (file offset) | VA | Dump EIP (EXE at 0x240000) | What |
|---|---|---|---|---|
| Terrain | 0x2b4f3c | **0x6B5B3C** | 0x4F5B3C | NULL marsh stamp in 0x6B5A50 (bake) |
| LandmarkSystem | 0x39b73d | **0x79C33D** | 0x5DC33D | NULL city-field stamp vector in 0x79C2D0 (City update) |
| River, Arrows, A2, B2 | 0x4cc892 | **0x8CD492** | 0x70D492 | RenderBatch::Reset on a NULL batch |
| Unit in A3/B3 | 0x59fc14 | **0x9A0814** | 0x7E0814 | NULL Granny DAG pose cache on a worker thread |
| Unit alone | (Steam .mdmp, not crashes.log) | **0x9A3CF7** | - | NULL pose in MovementPath startup |
| 4 MB queue overflow | stack RVA 0x2a59a5 | 0x6A59A5 | EXE at 0x480000 | dispatcher return; EIP 0 or garbage |
| 1 MB UI draw list | 0x208592 (and RVA 0x209192) | 0x609192 [I] | - | bump-allocator overrun (units-on-screen) |
| units-on-screen, second crash | 0x160cd9 / 0x1618d9 | 0x5618D9 [I] | - | reads `[[global]+0x9C]` = 0xFFFFFFFF |

Contradiction: `stubs/coupling.md` converts the queue-dispatch value "EXE+0x2a59a5" as if it were a file offset
(VA 0x6A65A5, "inside 0x6A6490"). The queue-overflow note (ReadProcessMemory, dispatcher at EXE+0x2a5970) and the
Landmark/Terrain dump analyses (frames 0x6A59A5) show it is an RVA, so **VA 0x6A59A5** is right. Other crash values
whose kind the sources do not state: `EXE+0x51f5bc` (sound-thread fault after the second units crash), `EXE+0x1e0129`
(promotion-flag crash), `EXE+0x4ba4da` (`movntdq` into a NULL buffer after a failed allocation, 2026-09-21), and the
draw-list bump allocator given as "EXE 0xc7fdc0" (absolute in that run).

## Other builds (from the sources; RVAs unless the table says VA)

| Item | DX11 | DX9 | Tablet | Source |
|---|---|---|---|---|
| g_Civ5App (VA) | 0xCC8E74 | 0xCB8CF4 | 0xCCA8F0 | CAM |
| PR #13379 SetEngineUserInterface signature (VA) | 0x47BA70 | 0x72B7F0 | 0x4CB690 | CAM |
| FCamera::GetViewProj (VA) | 0x8E4A80 | 0x96BA80 | 0x8E6F70 | CAM |
| GameViewState::UpdateViews (VA) | 0x51DF70 | 0x453BE0 | 0x73EBA0 | CAM |
| g_Proj / g_View[0] (VA) | 0xDA12D0 / 0xDA1310 | 0xD91120 / 0xD91160 | 0xDA2D50 / 0xDA2D90 | CAM |
| g_NumViews (VA) | 0xD90FA4 | 0xD80DF4 | 0xD92A24 | CAM |
| viewport / world (VA) | 0x2DD1FB8 / 0x2DD1FD0 | 0x2DC1DB8 / 0x2DC1DD0 | 0x2DD3FA0 / 0x2DD3FB8 | CAM |
| WorldAnchor site (VA) | 0x522B70 | 0x57B630 | 0x558A60 | CAM |
| engine operator new / aligned new / aligned helper (VA) | 0x82C090 / 0x813A80 / 0x813AB0 | 0x81CAE0 / 0x81CB80 / 0x81CBB0 | 0x814CA0 / 0x814A10 / 0x814A40 | AT |
| 4 MB queue index (RVA) | 0x160E400 | 0x15FE200 | 0x160FE80 | Q |
| 4 MB queue channels (RVA) | 0x160F080 | 0x15FEE80 | 0x1610B00 | Q |
| InterlockedExchangeAdd IAT slot (RVA) | 0x5D647C | 0x5C9488 | 0x5D847C | Q |
| queue swap (RVA) | 0x90DF0 | 0x385420 | 0xACE00 | Q |
| queue dispatcher (RVA) | 0x2A5970 | 0x20F40 | 0x3980E0 | Q |

The world-system, mask and stub addresses (River/Arrows signatures, pose pool, city-field manager, etc.) were derived
for DX11 only.

## Import address tables (DX11, RVA, from `imports.txt`)

The EXE links 19 libraries statically: the C++ runtime, Windows, Direct3D 9 and 11 with D3DX, Steam, Lua, the game's
two database and localisation libraries, audio, zlib, networking, image and debug helpers. The import slots the product
and the instruments use are named in the rows of the tables where they are used. (D83 edit 2026-11-25, 0772: a list of
the 19 libraries' import tables by RVA was replaced by this description.)

## Main table (DX11, sorted by VA)

**Build label (ticket 4967, 2026-11-20): every VA in this table and the tables below is a VA of Steam build 4390913
(stamp 0x546CD0A8, SizeOfImage 0x06519000).** Build 26577625 (stamp 0x6AF3837D, SizeOfImage 0x063A2000) has no
column here: its addresses live only in the local per-build table (`<exe_tables>/26577625/table-*.json`, D202; how it
is made: `exe-build-table.md` sections 5-8), and a single new-build address appears in research only beside the
reasoning that needs it (e.g. `exe-build-table.md` 11.2's hand cases, `new-exe-build-26577625.md` B.3). Whether every
product symbol carried to 26577625 is alive in the running game: `new-exe-build-26577625.md` C.4 [L, 2026-11-20].
A symbol's name is the key between the two builds: `exebuild/symbols/*.tsv` names each row of this table that the
product uses (its `anchor_va` column is the build-4390913 VA, as in this table). (The brief for 4967 named a "26577625 column" here;
W4 read MC #7318's rule "single addresses beside their reasoning, never the table" as excluding a whole column and
added this label instead; the lead may ask for more.)

Source key PR = PR #13379 `CvExeAddresses.h` labels as applied to the Ghidra project
(`%USERPROFILE%\tools\ghidra-projects\logs\apply_pr13379_script.log`, 2026-11-09).

**Names.** PR #13379's names are the engine's own symbols (Linux build, per the PR text) and win over analyst
labels. The event managers "A", "B", "C" (and the queue's "Q1"/"Q2"/"Q4") are analyst labels from `stubs/coupling.md`
and `stubs/unit.md`; "LocalMachine" on manager C is likewise an analyst label taken from the `LocalMachine*` Lua
event names it carries. The engine symbol `g_EventSystemLocalMachine` is **0x1A0E400** (manager "A"), not 0x2A0F480
(reconciled by the librarian 2026-11-10; see the 0x1A0E400 and 0x2A0F480 rows).

| VA | What | Sources | Conf. |
|---|---|---|---|
| 0x403170 | LoadGameState::Update / LaunchNextStage (vtbl slot 8); state 2 calls slot 7 (the bake); return 0x40325F in the Terrain dump. The only writer of queue B's ordering byte (`q+0x30`, VA 0x2A10E38 + 0x4C x [0x2A10200]): := 0 in its first state at 0x4031F7, := 1 in a later state at 0x403492 (4675) | T,C,PP | C |
| 0x4034DC | per-frame GameView-update record handler in the Landmark dump; one-time UpdateWorldCamera call at load | L,CAM | C |
| 0x403684 | LoadGameState::Update state jump table on `[this+0x10]` (Ghidra project lacks its case bodies) | LT | C |
| 0x404260 | Overlay StartupGame | C | C |
| 0x405C70 | Unit_System::Startup (ecx 0xD02200; 14 listener registrations via 0x710060; pose-pool creator 0x7A2470); restarted by mod activation | U,C | C |
| 0x40B410 | second ungated Unit start with its own event channel; also calls GridHeightmap::Init; no static caller | U,C | I |
| 0x40CF10 | Decal_System Startup | C | C |
| 0x41BE50 | CameraController ctor (GVS+0x44) | CAM | C |
| 0x41E020 | camera controller Reset (target 0, speed 1, base 50, zoom limits 11 / 20.5) | CAM | C |
| 0x422EA5 | call into app-init 0x4AF630 that builds the GameView | RA | C |
| 0x424630 | SteamAPI_Init call path, seen in the pre-log window (1 of 11 samples) | LT | L |
| 0x429F60 | DynamicGeometryVisSystem GameView Startup | L | C |
| 0x42C510 | MovementPath per-frame update | C | C |
| 0x42DEA0 | EXE top-level exception filter; SteamAPI_WriteMiniDump -> .mdmp in the game root | U | C |
| 0x42F310 | Lua UI.LookAt | CAM | C |
| 0x42FA80 | Unit per-frame update; samples the heightmap via 0x50EAC0 > 0x5024B0 > 0x530A10 | C | C |
| 0x42FCC0 | LoadGameState::StartInitStuffOnMap (stage 4; posts per-plot messages, then msg 0x72 "map ready" 0x43CE80) | C | C |
| 0x432220 | city-field manager init (this 0xBA2224); only caller GameView Startup at 0x623280 (Landmark branch) | L,C | C |
| 0x4333D0 | Lua UI.CenterCamera | CAM | C |
| 0x4365E0 | farm-field layout chain frame (0x585490 -> 0x4365E0 -> 0x77C1C0) during the load gap | LT | L |
| 0x4387D0 | caller of TerrainDecalSystem geometry 0x49B5A0 in the load gap/tail (<- 0x81D790 <- 0x7BF260) | LT | L |
| 0x439E00 | Forest per-frame update / per-frame decal list (called from UpdateWorldCamera 0x6FBD0B) | C,T | C |
| 0x44A570 | FluidFOW_System Startup | C | C |
| 0x44D8C0 | Unit StartupGame | C | C |
| 0x459D20 | 4 MB queue pump (EXE+0x59D20); frame 0x459D8A in the Landmark dump | Q,L | C |
| 0x463640 | River render "River Visualization"; opens with RenderBatch::Reset([this+0x10]) | RA | C |
| 0x465600 | GameView Startup group registering the farm-field builder 0x748FC0 | L | C |
| 0x4686C0 | GVS::StartupGame: camera init + per-system StartupGame gates (0x468B1E-0x469434) | RA,C,CAM | C |
| 0x468BCC | Terrain view-mask bit re-derived: or [esi+0x1b4],1; test [app+0x940],1; jne; and ~1 | T | C |
| 0x468EFD | call to the FluidFOW per-game init 0x5B6AF0, only when bit 15 is set | AT,G | C |
| 0x46949B | unconditional TerrainSystem 0x71CAF0 call inside StartupGame | C,T | C |
| 0x46E550 | minimap render path reading Terrain/River fields | T,RA | C |
| 0x474120 | stop and free the game-core thread object (resume, wait, delete, clear 0x2DD202C); callers 0x449E5B, 0x47B842 (0335) | GT | C |
| 0x475F40 | app Startup (gated range 0x475F40-0x476C00) | DS,C | C |
| 0x476018 | app Startup: GridHeightmap created, capacity from MAX_SUPPORTED_MAP via 0x64C620 | C | C |
| 0x47602E | app Startup: DistanceFog Startup 0x69F240, unconditional | C | C |
| 0x4761A1 | app Startup: test eax,0x100 -> RiverVisSystem::Startup 0x638880 (the site is checked by a byte pattern in the product before use, `pxm_river_gate` in proxy/src/proxy_shell.cpp; D83 edit 2026-11-25, 0772: the written-out signature bytes were dropped) | RA | C |
| 0x476222 | app Startup gate: Unit (bit 6), the first system started | U | C |
| 0x476359 | app Startup: test eax,0x400000 -> Arrow Startup 0x526850 (the site is checked by a byte pattern in the product before use, `pxm_arrows_gate` in proxy/src/proxy_shell.cpp; D83 edit 2026-11-25, 0772: the written-out signature bytes were dropped) | RA | C |
| 0x4763DA | app Startup gate: MovementPath (bit 23) | U | C |
| 0x4765E9 | app Startup gate: City (bit 5) | U | C |
| 0x476675 | app Startup gate: LandmarkSystem (bit 1) | U | C |
| 0x47696C | app Startup: TerrainSystem::Startup call (local mask copy tested at 0x476963) | T | C |
| 0x4769EF | app Startup: shadow map 0xDA12B8 created after TerrainSystem::Startup | C | C |
| 0x47BA70 | PR #13379 signature LoadCvGameCoreDLL_SetEngineUserInterface: 8B 0D <&g_Civ5App>; anchor for 0xCC8E74 | CAM,G | C |
| 0x47E350 | GridHeightmap::Init (fills 0xD91270; ready byte set at 0x47E6EF) | C,T | C |
| 0x494160 | LoadGameState::Exit (vtbl 0xA20EBC +0x08); ends the `LoadGameState_TOTAL` timer | LT | C |
| 0x49B5A0 | TerrainDecalSystem AddDecal (indexes the type table TS+0xC0) | T | C |
| 0x49FF20 | [DisableSystems] parser | DS | C |
| 0x4AC910 | queue B's move (called by the pump 0x785AF0): walks the write half in posting order; per record it reads the ordering byte `q+0x30` (at 0x4AC9A2): 1 = the blocking check 0x60EB10 (predicate 0x727A30) against the dispatch half and the records before it, 0 = no check, every record moves in one pass (4672, 4675; `../08-performance/turn-time-study.md` "What the drain is made of") | PP | C |
| 0x4AF630 | app init: builds the GameViewState (once, per river_arrows.md) | RA | C |
| 0x4AFAAC | GameViewState allocation, Civ5App.cpp line 0xF1F, 0x8D0 bytes (second site 0x4AFFA9, line 0xF3E) | AS,CAM | C |
| 0x4B011A | [GVS+0x1B8] copied from [app+0x690] when [app+0x690] & 3 (render pass selector) | RA | C |
| 0x4B2050 | TerrainSystem::StartupGame | T | C |
| 0x4B3F50 | frame function that pumps the four queues (EXE+0xB3F50, "~"); 0x4B40CD is on the River/Arrows dump stack | Q,RA | I |
| 0x4B4029 | frame function's call to the queue-B pump 0x785AF0 (second of four pumps) | LT | C |
| 0x4B4067 | frame function's call to the 4 MB queue pump 0x459D20 (last of four pumps) | LT | C |
| 0x4B5580 | MovementPath startup "cvMovementPathVisSystem"; uses the pose pool (crash A frame 0x4B59D2) | U | C |
| 0x4D2966 | Lua Events.SpawnArrowEvent registration, unconditional (RemoveAllArrowsEvent at 0x4D2989) | RA | C |
| 0x4D8BF0 | game-core thread step (thread object vftable 0xA55BA0 slot 1): semaphore +0x3C, `ICvGame1::Update` (vtable +0x120, return 0x4D8C4A) unless the update hold is set, pending +0x40 decremented, self-suspend requested (0335) | GT | C |
| 0x4E5590 | Arrows render "Arrow Visualization"; opens with RenderBatch::Reset([this+0x74]) | RA | C |
| 0x4FB9F0 | queue-B empty check used by LoadGameState state 5 (arg buffer 0x2A10E08 + idx*0x4C) | LT | C |
| 0x4CFC40 | the AI-turn byte's drop with arg2 0: appends one 0x60-byte record (header words 0x60, 0x98) to queue B's write half under the queue's critical section (+0x34) through 0x71CB50, which also `InterlockedIncrement`s the queue's word +0x28 (4665) | PP | C |
| 0x50A170 | setter of the frame's AI-turn byte 0x2DE0447 (4656); (on, arg2): on = 1 with the byte 0: spins on `InterlockedCompareExchange(0x2A10F00, 1, 0)`, byte := 1, `InterlockedIncrement(0x2DE0448)`; on = 0 with the byte 1: 0x2A10F00 := 0, byte := 0, and with arg2 = 0 the record of 0x4CFC40. Callers: the frame 0x72579E (F, 0), 0x580C89 (0, 1; a GameViewState method), 0x6A36AD (0, 0), the thunk 0x582180 (arg, 0) (4665) | PP | C |
| 0x5426A0 | getter of the AI-turn byte 0x2DE0447 (used twice by `GameViewState::Update`) (4665) | PP | C |
| 0x50F370 | Lua HexToWorld C function -> 0x6851C0 | CAM,C | C |
| 0x517880 | Marsh::Startup (ecx 0xD3DCB8; marsh_stamp.dds; exit(0) if missing) | T | C |
| 0x5183F0 | 4 MB-queue handler for type-0x66 timer records: writes `system_init.log` lines at pump time | LT | C |
| 0x51AFD0 | reset of the leader/diplomacy globals: update hold 0x2DE044C = 0, 64 x 0x18 B entries at 0x2DDFE40 (0335) | GT | C |
| 0x51B070 | Terrain per-frame update | T | C |
| 0x51B1E0 | `ICvEngineUtility1::GameplayDiplomacyAILeaderMessage` handler (engine utility slot 184 -> thunk 0x605CB0; this = [[0xCC8E74]+0xD0], current leader at +0x1E4): on the leader-screen branch increments the update hold 0x2DE044C (0335) | GT | C |
| 0x51DF70 | GameViewState::UpdateViews: writes g_Proj / g_View / planes once per 3D frame (signature site 0x51DFBD) | CAM,G | C |
| 0x522910 | WorldAnchor projection (D3DXVec3Project with the view globals) | CAM | C |
| 0x522B70 | WorldAnchor signature site: 68 <world> 55 68 <proj> 68 <viewport> 57 | CAM | C |
| 0x523CA0 | shadow pass, every frame, gated internally (skips when 0xDA12B8 is 0) | C,T | C |
| 0x526850 | Arrow_System Startup (ArrowSettings.xml; events via 0x671150) | RA | C |
| 0x530A10 | HeightAtWorld | C | C |
| 0x535BF0 | MapSetup: hex/world matrices, extents, sets [0xD90FC4] = 0xD91270 (load stage 1; 0x536096) | C,CAM | C |
| 0x5362F0 | farm-field layout chain; only caller 0x6016F0; calls 0x5B86D0 | LT | C |
| 0x537BF0 | City per-frame update (City bit 0x20); all four users of the city-field manager | L,C | C |
| 0x53AF10 | update request from the main thread's frame (called at 0x7258E5): synchronous (0x53B021), job (job set 0x2DD2030, counter 0xC936EC) or thread mode; thread mode: return while +0x40 > 0, else +0x40 = 1 and resume (0x82A320), or create and start the thread (0335) | GT | C |
| 0x553BE0 | empty function (ret): River update, DG per-frame, arrow no-op handler; ~90 callers | RA,L | C |
| 0x560340 | ArtDefine_Landmarks library load (cvLandmarkLibrary.cpp:475) | L,U | C |
| 0x5618D9 | second units-on-screen crash site reading [[global]+0x9C] = 0xFFFFFFFF (RVA 0x1618D9; also logged as 0x160cd9) | UM | I |
| 0x574A00 | `SerialEventHexHighlight` handler on `g_EventSystemLocalMachine` (stubs 0x861B10/0x862EF0): style lookup 0x618EB0 -> map 0xCF0A48 `operator[]` 0x755430, then inline strcmp with "ValidFireTargetBorder"/"AMRBorder" (bug 0127, `disable-systems-and-masks.md`) | 0127 | C |
| 0x574B03 | 0127 CRASH: `mov al,[ecx]` in that strcmp, ecx = first word of a style record that 0x755430 inserted from uninitialised stack because the style map is empty (Overlay off) | 0127 | C |
| 0x580C50 | game ShutdownGame/reinit: per-bit gates 0x580D04-0x58112D; River bit at 0x580E1A; Terrain shutdown gate 0x58100A; RoadLayout write 0x580F14 | RA,C,T | C |
| 0x583CE0 | route builder inner function (TerrainDecalSystem.cpp; EXE+0x183CE0) | Q,T | C |
| 0x585490 | farm-field layout chain frame (0x5B86D0 -> 0x585490 -> 0x4365E0) | LT | L |
| 0x5893F0 | 3D render pass A (alternate/benchmark variant) | RA,C | C |
| 0x58961F | pass A arrow render, gated on the Overlay bit (bl = 4) | RA | C |
| 0x591200 | LoadGameState::Enter (vtbl 0xA20EBC +0x04): starts `LoadGameState_TOTAL`, loads FrontEnd/LoadScreen, MapSetup 0x535BF0 | LT,C | C |
| 0x595690 | MovementPath startup caller (Unit-alone crash stack) | U | C |
| 0x597590 | leader-head scene class (vftable 0xA1DAE0) slot 2: drops the update hold if set, resets the scene (0335) | GT | C |
| 0x5A0F80 | reflection pass "Clear Reflections", gated internally (0xDA12C0) | C,U,T | C |
| 0x5A3490 | GridHeightmap height setter | C | C |
| 0x5AD360 | queue-A pump (0x2A0FF80), first pump of the frame function | LT | C |
| 0x5B6AF0 | FluidFOW per-game init (StartupGame); calls the grid builder 0x729C40 | AT,C | C |
| 0x5B86D0 | farm-field layout chain; only caller 0x5362F0 | LT | C |
| 0x5BCEF0 | builds TerrainRenderer (last CreateTerrain step) and registers Terrain event handlers | T,C | C |
| 0x5BFD50 | asset file load frame (with 0x6B6550) in the all-on load-gap samples | LT | L |
| 0x5CA5E0 | a 4 MB-queue record allocator (EXE+0x1CA5E0); ~325 inlined writer sites | Q | C |
| 0x5DB290 | Lua `UI.RequestLeaveLeader` C function: calls 0x5DB0E0 on the diplomacy manager [[0xCC8E74]+0xD0] (0335) | GT | C |
| 0x5EC250 | route builder listener (Landmark GameView, registered by 0x758440) | L,T | C |
| 0x5ED240 | Lua ToHexFromGrid | CAM | C |
| 0x6016F0 | farm-field layout (only caller 0x748FC0; uses city-field manager 0xB9DDA0); 13 of 17 B4 load-gap samples | LT | C |
| 0x602451 | g_Civ5App set (cleared at 0x60248E) | CAM | C |
| 0x603580 | `ICvEngineUtility1::IsGameCoreExecuting` (slot 233): in the thread mode true while the thread object's `+0x40` > 0; the frame skips its update request and the queue-B pump while true (4656) | PP | C |
| 0x605CB0 | thunk of engine utility slot 184 (`GameplayDiplomacyAILeaderMessage`) -> 0x51B1E0 (0335) | GT | C |
| 0x609192 | 1 MB UI draw-list job, fstp [esi+60h] (RVA 0x209192; crashes.log 0x208592) | UM | I |
| 0x60ABB0 | LoadGameState::StartInitTerrainFromGameplayMap (vtbl slot 7); NOT gated; CreateTerrain call at 0x60BBD1 | T,C | C |
| 0x623170 | GameView::Startup: gates from 0x6231CE; Landmark branch 0x623280; or 0x1800 at 0x62344B | RA,C,L | C |
| 0x629910 | PostProcessModActions: restarts Unit/Landmark at 0x629BE3 regardless of the mask | L,C | C |
| 0x630B70 | Marsh::OnMarshPlot (on the Terrain crash stack) | T | C |
| 0x6316C0 | HighlightSystem start-up (`HighlightSystem.cpp`): 0x6DBE40 loads `Highlights.xml` into 0xCF0A48; called only from the Overlay start-ups at 0x57D2D6 / 0x6FFFAE | 0127 | C |
| 0x636590 | LandmarkSystem::Startup / art reload (ecx 0xDB13D8) | U,L | C |
| 0x638880 | RiverVisSystem::Startup (this 0x1A0C694) | RA | C |
| 0x63ABA0 | queue Q2 dispatcher (EXE+0x23ABA0) | Q,U | C |
| 0x63B0F0 | Marsh::FlushDeferred | T | C |
| 0x63C280 | Lua GridToWorld (thunk 0x680CA0) | C | C |
| 0x63E530 | queue handler-slot trampoline (EXE+0x23E530) -> 0x8668E0 | Q | C |
| 0x6436A0 | GameViewState::Startup (LoadGameState state 1) -> GameView Startup 0x623170 | LT,C | C |
| 0x643A60 | Arrows per-frame update | RA | C |
| 0x647F60 | pose-pool slot creator (thiscall; ecx = thread block, arg = slot count; idempotent) | U | C |
| 0x64A860 | GameViewState ctor; [GVS+0x1B4] = 0xFFFFFFFF at 0x64A8DC and 0x64AA81 | RA,C | C |
| 0x657070 | Terrain + water draw; River render under mask 0x100 at 0x6570D1 (signature at 0x6570C5) | RA,T | C |
| 0x6668F0 | GVS::UpdateMouseOverHex: picking on a flat plane; returns while [0xD91279] == 0 | CAM,C | C |
| 0x6684D0 | TerrainSystem::ShutdownGame (sets 0xD90FC4 = 0) | T,C | C |
| 0x66CE00 | render pass selection: test byte [ecx+0x1b8],0xf | RA | C |
| 0x679EA0 | system-info report ("gamver/winver/sysinf/cpufea/DXDIAG"; runs DxDiag, D3D12/D3D11 device creation); ~12-14 s of the pre-log window | LT | C |
| 0x6851C0 | HexToWorld / GridToWorld core (this = *(0xD90FC4)) | CAM,C | C |
| 0x6881D0 | writes the mouse-over plot to InterfaceBuddy+0x90 | CAM | C |
| 0x68F795 | FluidFOW per-thread block new(0x1800000), FluidParticles.cpp line 0x26F (memory note: site 0x68F790) | AS,DS | C |
| 0x694160 | `NotifyAILeadersInGame`: sends `NotifySpecificAILeaderInGame` for players 0-21 only (the loop end is 22), called at load from 0x4035CB; majors 22-42 of the 43-civ build get no 3D leader scene (0355, `leader-scene-player-limit.md`) | GT | C |
| 0x6A36A0 | leader-head scene class slot 3 (per frame): "Leaderheads are disabled!" when bit 9 of [this+0x718] is clear, "THERE IS NO SCENE LOADED FOR THIS LEADER!!" when 0xDA8F8C is 0 (0335) | GT | C |
| 0x6A5970 | 4 MB queue dispatcher (EXE+0x2A5970) | Q | C |
| 0x6A59A5 | dispatcher return address in crashes.log stacks (EXE+0x2a59a5 is an RVA); stale word in the Terrain dump | Q,L,T | C |
| 0x6A59AA | dispatcher add esi,[esi]: endless loop on a zero-length record | Q | C |
| 0x6A74D0 | builds the marsh mask grid and calls 0x6B5A50 | T | C |
| 0x6B2B30 | main-thread wait on a DirectShow (quartz.dll) graph after Lua init (with 0x6BB710), ~10 s | LT | L |
| 0x6B2BC0 | UnitSim Startup (24 listener registrations via 0x6BCA20) | U,C | C |
| 0x6B5A50 | marsh-mask stamp into the terrain grids (this 0xD3DCF8) | T | C |
| 0x6B5B3C | TERRAIN CRASH: cmp byte ptr [edx+ecx],0 on the NULL marsh stamp (crashes.log 0x2b4f3c) | T,S | C |
| 0x6B6550 | asset file load frame (with 0x5BFD50) in the all-on load-gap samples | LT | L |
| 0x6B8370 | TerrainSystem::Startup (Marsh::Startup call at 0x6B8584) | T | C |
| 0x6B8E50 | SetActiveDLCandMods: Reload*System restarts with the mask ignored (0x6B9A17-0x6B9A95) | U,L,C | C |
| 0x6BB710 | DirectShow graph frame after Lua init (with 0x6B2B30) | LT | L |
| 0x6BB810 | Terrain StartGPUInitWork (LoadGameState state 8; calls UpdateViews) | LT,C | C |
| 0x6CCA90 | TerrainSystem::Shutdown | T | C |
| 0x6CE440 | Lua allocator, seen only as a stale stack slot in the load-gap scans; module attribution [I] | LT | I |
| 0x6F3ED0 | per-frame FOV/aspect re-apply (called at 0x72595C) | CAM | C |
| 0x6F5940 | Lua UI.GetMouseOverHex (reads [[g_Civ5App+0xD0]+0x90]) | CAM | C |
| 0x6FBA80 | GameViewState::UpdateWorldCamera = per-frame world update of every system, gated on [GVS+0x1B4] | CAM,C,L | C |
| 0x704B60 | `ICvEngineUtility1::getSecsPerTurn` (slot 102, `+0x198` of vftable 0x9EA718): returns the float at 0x9E402C (0.1); called by `GameViewState::Update` (its 2-byte `call edx` at 0x7258BF, **return address 0x7258C1**; corrected 2026-11-17 by 4662, which saw every frame's call return to EXE+0x3258C1 live; 4656 had written 0x7258BF as the return address) as the update tick's threshold (4656) | PP | C,L |
| 0x7052B0 | queue-C pump (0x2A0F480), third pump of the frame function | LT | C |
| 0x709B50 | TerrainSystem::CreateTerrain (this 0xD165E0; GridHeightmap::Init at 0x709D6D; river mesh 0x6D51D0) | T,C | C |
| 0x714B80 | TerrainDecalSystem type-table Startup (decal_filenames.xml) | T | C |
| 0x7253E0 | GameViewState::Update (vtbl slot 3; "GameViewState frame stalled"); UpdateWorldCamera call 0x72591F; paces the update requests: tick accumulator 0xCCF91C, skips while an update is pending, and at an AI player change (heap field `GameViewState+0x8CC`, the accepted player) while queue B is not empty (`game-core-thread.md` 6, 4656 [C], 4661 [L]) | CAM | C |
| 0x729C40 | FluidFOW particle grid builder: FluidParticles.cpp lines 718/719/723/724/726 at 0x729E0C..0x729F0B; freed at 0x70F790 | AT,G | C |
| 0x741EB0 | camera controller update (glide, pan, yaw, wrap, clamp) | CAM | C |
| 0x748FC0 | farm-field builder listener (ART_DEF_IMPROVEMENT_FARM; EXE+0x348FC0) | Q,L | C |
| 0x7492E0 | 3D render pass B; arrows test at 0x7495D6 (a test of the view mask's Arrows bit followed by a short jump; checked by a byte pattern in the product, `pxm_arrows_test` in proxy/src/proxy_shell.cpp; D83 edit 2026-11-25, 0772: the written-out signature bytes were dropped) | RA,C | C |
| 0x74E510 | is the update held ([0x2DE044C] != 0); callers 0x4D8C36, 0x53B026, 0x5975A9, 0x6C8DD0, 0x72552D (0335) | GT | C |
| 0x758440 | RoadLayout listener registration (8 listeners incl. route builder 0x5EC250) | L,C | C |
| 0x769FB0 | update hold(bool): `InterlockedIncrement` / `InterlockedDecrement` of 0x2DE044C; callers 0x51B31B (true), 0x5975B4 (false) (0335) | GT | C |
| 0x77C1C0 | innermost EXE frame of the farm-field layout chain in the load gap | LT | L |
| 0x785AF0 | queue-B pump (0x2A10200); called by the frame function at 0x4B4029 and at the end of GVS::StartupGame 0x4686C0 | LT | C |
| 0x79C2D0 | City-update helper writing city-field stamps | L | C |
| 0x79C33D | LANDMARK CRASH: mov [edi+2008h],ecx through the NULL stamp vector (crashes.log 0x39b73d) | L,S | C |
| 0x7A2200 | app Shutdown, per-bit gated on [app+0x940] | RA,C | C |
| 0x7A2470 | pose-pool creator called by Unit_System::Startup | U | C |
| 0x7AD6C0 | 4 MB-queue type-3 farm-field polygon handler (TerrainDecalSystem.cpp; EXE+0x3AD6C0) | Q,T | C |
| 0x7BF260 | outer frame of the TerrainDecalSystem geometry chain (0x7BF260 -> 0x81D790 -> 0x4387D0 -> 0x49B5A0) in the load tail | LT | L |
| 0x813A80 | FFastVector FMALLOCALIGNED aligned new (749 callers); helper 0x813AB0 | AT,G | C |
| 0x81D790 | TerrainDecalSystem geometry chain frame (see 0x7BF260) | LT | L |
| 0x820750 | FFileIO ReadFile wrapper; 7-11 s before NetLoadGame (file not identified) | LT | L |
| 0x8208B0 | FFileIO SetFilePointer wrapper; same window as 0x820750 | LT | L |
| 0x82A250 | game-core thread: request suspend (byte +0x34 under the critical section +0x0C) (0335) | GT | C |
| 0x82A320 | game-core thread: resume (+0x34 = 0, `ResumeThread` on +0x38); callers 0x474165, 0x53B017 (0335) | GT | C |
| 0x82A360 | game-core thread procedure (Win32 start `CivilizationV_DX11.exe+0x42A360`): loop of self-suspend and step (0335) | GT | C |
| 0x82C090 | engine operator new(size, count, file, line, tag, tag2); 1,536 callers | AT,G | C |
| 0x82C0E0 | FSBADeallocate || free; no direct callers (inlined ~650 times) | AT | C |
| 0x8447F0 | thunk to the timer-record allocator 0x5CA5E0 on `g_EventSystemLocalMachine` 0x1A0E400 | LT | C |
| 0x861760 | event-bus registration call target | C | C |
| 0x8668E0 | listener-list walker behind the trampoline (EXE+0x4668E0); frame 0x866919 in dumps | Q,L | C |
| 0x8A86A4 | D3DX9 import thunks 0x8A86A4..0x8A8734 (PlaneIntersectLine .. Vec3Project) | CAM | C |
| 0x8BB6B1 | D3D11CreateDevice call (out-params 0x301057C / 0x3010584) | CAM | C |
| 0x8BD0C0 | Renderer::CreateBatch: 0x2080-byte batch (D3D11Renderer.cpp:0xf82) | RA | I |
| 0x8CD490 | RenderBatch::Reset, 30-byte fastcall helper | RA | I |
| 0x8CD492 | RIVER / ARROWS CRASH: write [ecx+0xC] with a NULL batch (crashes.log 0x4cc892) | RA,S | C |
| 0x8CFD50 | shader-constant CPU block staging (returns block+4, sets a dirty byte) | CAM | C |
| 0x8E4A80 | FCamera::GetViewProj (vtbl 0xA9A1B0 slot 2; thiscall ret 8) | CAM | C |
| 0x8E4D70 | FCamera::Update (slot 1) | CAM | C |
| 0x8E5030 | FCamera::GetFrustumPlanes | CAM | C |
| 0x9A0814 | UNIT COMBO CRASH (A3/B3): worker thread, NULL Granny DAG pose cache in 0x9A0800 (crashes.log 0x59fc14) | U,S | C |
| 0x9A3CF7 | UNIT-ALONE CRASH: NULL pose in 0x9A3CE0 (granny_world_pose.cpp), main thread, app Startup | U | C |
| 0x9C4EB0 | static initialiser calling the system-info report 0x679EA0 (first of two) | LT | C |
| 0x9C4EC0 | static initialiser calling 0x679EA0 (second); the next initialiser installs 0x42DEA0 | LT | C |
| 0x9D647C | KERNEL32 IAT slot InterlockedExchangeAdd (EngineQueueGuard thunk; EXE+0x5D647C) | Q,IM | C |
| 0x9D66D8 | MSVCR90 IAT slot _aligned_free | AT | C |
| 0x9D66DC | MSVCR90 IAT slot realloc | AT | C |
| 0x9D66E0 | MSVCR90 IAT slot _aligned_malloc | AT | C |
| 0x9D67E0 | MSVCR90 IAT slot malloc | AT | C |
| 0x9D67E4 | MSVCR90 IAT slot free | AT | C |
| 0x9E402C | `.rdata` float 0.1, compiler-pooled (50 `.text` instructions name it): the update tick's threshold through 0x704B60; never written (read-only section, and not "the" interval alone) (4656) | PP | C |
| 0xA0BB1C | near-plane constant 50.0 | CAM | C |
| 0xA10424 | profiler string "RiverVisSystem::Startup()" (0xA10474 "Arrow_System::Startup()") | RA | C |
| 0xA1DAE0 | leader-head scene class vftable (.rdata) (0335) | GT | C |
| 0xA20908 | GameViewState vtable; `[[0xCC8E74]+0xC0]` carries it in a game (the proxy checks it before every write of D165 option 2, 4665) | CAM,RA,PP | C |
| 0xA20EBC | LoadGameState vtable | T,C | C |
| 0xA55BA0 | game-core thread object vftable (.rdata) (0335) | GT | C |
| 0xA7AEA8 | modinfo string ReloadLandmarkSystem (0xA7AEC4 StrategicView, 0xA7AEE4 Unit, 0xA7AE90 Audio) | RA,C | C |
| 0xA9A1B0 | FCamera vtable (dtor 0x8E5320, Update 0x8E4D70, GetViewProj 0x8E4A80) | CAM | C |
| 0xB94078 | [DisableSystems] key-name table (start of .data region 0xB94000) | DS | C |
| 0xB9A6D4 | GridHeightmap constants sx 1.0 (0xB9A6D8 sy 0.866, 0xB9A6DC height scale 0.2) | C | C |
| 0xB9C418 | Arrow_System static object (.data) | RA,C | C |
| 0xB9DDA0 | city-field manager singleton (cvCityFieldManager.h); stamp-vector data pointer 0xB9DDA4 | L | C |
| 0xBA20D0 | Landmark-family farm/city-field/RoadLayout group (0xBA2224 = this of 0x432220; 0xBA2D40) | L,C | C |
| 0xC8F908 | DynamicGeometryVisSystem static object | L | C |
| 0xC98DE4 | a game-turn word the frame's start-of-turn step compares with [0x2DE0440] (4656) [C]; live (S2i after a load at turn 150): 150 through turn 150, -1 from that turn's change on, so not a plain game turn (4661) [L] | PP | C,L |
| 0xCBFC70 | engine listener-registry spinlock | T | C |
| 0xCC8E74 | g_Civ5App pointer: app+0x940 config mask, +0xC0 GVS, +0xD0 InterfaceBuddy | CAM,RA,VM | C |
| 0xCCF91C | update-tick accumulator (float, `.data`): += the frame's dt in `GameViewState::Update` 0x7253E0, set to 0 (not reduced) once above `getSecsPerTurn()` = 0.1, which is a tick; the update request 0x53AF10 is called only on a tick (4656) [C]; live, every request came on a reset, 75 of 75 (4661) [L] | PP | C,L |
| 0xCD0D98 | CityBuildingSystem static object | C | C |
| 0xCF0A48 | highlight style map (name -> 0x9C-byte style record; `operator[]` 0x755430 inserts uninitialised records on a miss); size word at +0xC; 0 while the Overlay system is off [L] | 0127 | C |
| 0xCF0AC0 | Overlay_System static object | C | C |
| 0xD02200 | Unit_System static object (+0xC20 unit-vis registry [I], +0xC24 art library, +0xC40) | U,C | C |
| 0xD165E0 | TerrainSystem static object (~0x75FE8 bytes, no vtable, 52 call sites) | T,C | C |
| 0xD3DCB8 | Marsh sub-object = TerrainSystem+0x276D8 | T | C |
| 0xD3DCCC | marsh stamp array pointer (NULL under Terrain=1) | T | C |
| 0xD8C584 | TerrainSystem+0x75FA4: TerrainRenderer pointer | T | C |
| 0xD90EC4 | DistanceFog_System static object | C | C |
| 0xD90FA4 | g_NumViews | CAM | C |
| 0xD90FBC | g_MapWrapsX | CAM | C |
| 0xD90FC4 | pointer to GridHeightmap (0 before a map loads) | CAM,C,T | C |
| 0xD90FCC | world AABB min x,y,z (max at 0xD90FD8) | CAM | C |
| 0xD90FE4 | wrap period = world width (0xD90FE8 world height) | CAM | C |
| 0xD90FF0 | matrix world -> hex (cube) | CAM,C | C |
| 0xD91030 | matrix hex -> world | CAM,C | C |
| 0xD91270 | GridHeightmap static object (ready byte 0xD91279) | C,T | C |
| 0xD91318 | grid scale/offset floats 0xD91318..0xD91328; 0xD91320 ground-height base | CAM | C |
| 0xDA12B8 | shadow map "ShadowRaw" | C,T | C |
| 0xDA12C0 | reflection texture (Water::Startup) | C,T | C |
| 0xDA12D0 | g_Proj | CAM | C |
| 0xDA1310 | g_View[i] (+0x40*i) | CAM | C |
| 0xDA1410 | g_InvView[i] | CAM | C |
| 0xDA1510 | frustum planes (+0x60*i) | CAM | C |
| 0xDA1AD0 | g_ViewOffset[i] | CAM | C |
| 0xDA8F8C | current leader scene pointer (0 = none loaded) (0335) | GT | C |
| 0xDA94D8 | FluidFOW_System static object | C | C |
| 0xDAAA20 | FOW_System static object | C | C |
| 0xDB13D8 | cvLandmarkVisSystem static object (art library +0xB58 = 0xDB1F30) | L,U | C |
| 0x1A0ADA0 | StrategicView static object (own views) | C,CAM | C |
| 0x1A0C670 | MiniMap_System static object | C | C |
| 0x1A0C694 | RiverVisSystem static object (.bss) | RA,C | C |
| 0x1A0C6C8 | Wave_System static object | C | C |
| 0x1A0E400 | `g_EventSystemLocalMachine` (engine symbol, PR #13379, EXE_DATA label; data entries carry no byte signature, so the name rests on the PR [I]) = the 4 MB queue index global = the stubs' internal event manager "A" (EXE+0x160E400). *Corrected 2026-11-10 (librarian): was "internal event manager A" with no engine name, while LOADTIME.md used the PR name; both describe the same global.* | Q,C,U,PR,LT | C |
| 0x1A0F080 | 4 MB queue channel 0; channel c at +c*0x800180, two 0x400080 buffers each | Q | C |
| 0x2A0F480 | queue / event manager "C", "Q1" (EXE+0x260F480); carries the `LocalMachine*` Lua events (e.g. LocalMachineUnitPositionChanged), hence the analysts' "LocalMachine" label. Not `g_EventSystemLocalMachine` (that is 0x1A0E400); engine name unknown. *Label clarified 2026-11-10 (librarian); was "LocalMachine event manager C".* | Q,C,U | C |
| 0x2A0FF80 | first queue pumped by the frame function (EXE+0x260FF80) | Q | C |
| 0x2A10200 | queue / Lua-visible event manager B, 156 named events, "Q2" (EXE+0x2610200) | Q,C,U | C |
| 0x2A10E08 | queue-B buffer array (buffer idx at +idx*0x4C), checked by 0x4FB9F0 | LT | C |
| 0x2A10E38 | queue B's ordering byte of buffer 0 (`q+0x30`; buffer i at 0x2A10E38 + 0x4C x i, i = [0x2A10200], 0..2 before the flag set at 0x2A10F00): 1 in play, 0 while a game loads; written only by LoadGameState::Update 0x403170 (two stores, both with that index), read by the move 0x4AC910; not rewritten per frame (4675 [C]; live: `../08-performance/turn-time-study.md` "Option 4 built"). D165 option 4 clears it for the AI turn's end-of-phase drain | PP | C |
| 0x2A10F00 | queue B's flag of a set of four (with 0x2A10080, 0x2A0FB80, 0x2A0F400): held (1) while the AI-turn byte is 1 (0x50A170); 0x6E2AA0 spins until it takes all four (its one caller 0x475E80 switches the queue index 0x2A10200 and clears them); 5 operands in `.text` (4665) | PP | C |
| 0x2A11010 | City_System static object | C | C |
| 0x2B735E8 | RoadLayout listener object (Landmark family) | L,C | C |
| 0x2D6F5F8 | UnitSimSystem static object | C | C |
| 0x2D701A8 | Combat_System static object | C | C |
| 0x2D70310 | cvLandmarkSimSystem static object | L | C |
| 0x2DD1D1C | renderer/UI object pointer: +0x644/+0x648 backbuffer size, +0x6F0 = 1 MB draw-list object (EXE+0x29D1D1C) | CAM,UM | C |
| 0x2DD1FB8 | g_Viewport (D3DVIEWPORT9) | CAM | C |
| 0x2DD1FD0 | identity world matrix for projection | CAM | C |
| 0x2DD202C | pointer to the game-core thread object (0x44 B) (0335) | GT | C |
| 0x2DD2F5E | byte (set through 0x6D7B20; meaning not read): while non-zero `GameViewState::Update` skips the whole update-tick block (4656) | PP | C |
| 0x2DE0440 | a game turn the frame remembers for its start-of-turn step (4656) [I]; live -1 after a load at turn 150, 150 from that turn's change (4661) [L] | PP | I,L |
| 0x2DE0447 | the frame's AI-turn byte (4656) [C]; live in autoplay: 1 from the frame's acceptance of the next player (`GameViewState+0x8CC`) to the end of that player's `Update`, 0 from the `Update`'s end to the next acceptance; 0 in the human's turn (4661) [L]. *Refined 2026-11-17 by 4665:* it drops in the first frame after the `Update` (median 8 ms later in 4662's samples, 299 of 300 Update ends [L]) because skip 2 clears the frame's flag there, not at the `Update`'s end; while an `Update` is pending the frame copies it into its flag and changes nothing [C] | PP | C,L |
| 0x2DE0448 | counter: +1 at each 0 -> 1 of the AI-turn byte (0x50A170), -1 in the callback 0x6D08C0 (at 0: 0x77A890 on queue B); zeroed by the reset 0x51AFD0; 0x59A060 returns "it is 0" (4665) | PP | C |
| 0x2DE044C | update hold counter: every `ICvGame1::Update` is skipped while non-zero; raised by an AI leader message, dropped when the leader screen is left (0335) | GT | C |
| 0x301057C | ID3D11Device* (0x3010584 = immediate context; 0x301056C/0x3010568 backbuffer W/H) | CAM | C |
| 0x301058C | -NullDevice flag (NULL D3D driver) | DS | C |
| 0x5F63958 | g_PosePool: shared Granny pose pool, 0x180 bytes per worker thread | U | C |
| 0x6196D54 | worker-thread count read for the pose pool (by 0x81D870) | U | C |

**Unit model load path (ticket 0379, 2026-11-16) [C]:** 0x8ECC50 / 0x8ECBA0 (basis conversion per model file and member Scale, through Granny's 0x9BC790 -> 0x9C0C60, inverse 0x99E800, transform 0x9BCDA0 -> 0x9C1840 -> 0x9C1130 -> 0x9BECE0; clips 0x8ECCC0), constant 0xA9B35C (39.37008), vertex formats 0xCA0108 (0x8E2EB0 / 0x8E32E0; choice 0x9BBD90), copy 0x9BCCA0 / 0x9A1580, float to half 0x999C10, weight renormaliser 0x5EC070, uploaders 0x59BF10 / 0x51CB30: what each does is in `../05-graphics-composition/unit-renderer.md` 10.9 (the module follows it since ticket 0447).

## Engine objects the EXE hands the DLL (issue 2702, 2026-11-10)

Read-only headless Ghidra 12.1.4 (`-readOnly -noanalysis`, a private copy of project `civ5`), DX11 build above.
Slots are the x86 slots of `proxy/model/interfaces.json`. The slot order was cross-checked: slot 5 `DoMapSetup(int)`
= 0x4ACFA0 allocates per-plot arrays from `.\App\DLLInterface\CvDLLUtility.cpp` [C]. Full reasoning in
`tracker/issues/2702-stable-engine-objects.md`.

| VA | What | Conf |
|---|---|---|
| 0x9EA718 | vftable of the engine utility object (`CvDLLUtility`, implements `ICvEngineUtility1-4`; 260 interface slots to 0x9EAB24, 4 more entries follow). The only vftable whose slot 0 is 0x5688B0. Ticket 4662 (2026-11-17, the Steam EXE file): slots 0-263 all in `.text`, the entry before slot 0 (0x9EA714) is 0 (no RTTI locator), the entry after slot 263 (0x9EAB38) is not a pointer: 264 entries | C |
| 0x5688B0 | slot 0 `QueryInterface`: compares the GUID with 0xA55B60 (ICvUnknown [I]), `guidICvEngineUtility1-3` (0xA5556C/7C/8C) and 4 (copied by 0x5C8000 from 0xA5559C); on a match `++[this+4]` and returns `this`, else 0 | C |
| 0x4C0FB0 | slot 1 `Destroy`: `--[this+4]` unless it is 1; never frees | C |
| 0x790010 | slot 2 `getInterfaceIFace`: returns `[g_Civ5App+0xD0]` (InterfaceBuddy; the DLL never calls this slot) | C |
| 0x402390 | slot 3 `GetScriptSystem`: returns the fixed address **0x2DD2034**, a function-local static (guard bit 0 of 0x2DD2044) built on the first call by 0x59A030 (vftable 0xA5502C, 14 entries), destroyed only by the CRT `atexit` thunk 0x9D4B50 -> 0x490DC0. No other writer of 0x2DD2034 | C |
| 0x6EDA30 | slot 9 `GetModsDatabase`: returns `g_Civ5App+0xAF8` (an embedded `Database::Connection`; the DLL never calls this slot) | C |
| 0x740840 | engine utility constructor (object 0x468 bytes, `[this+4]` refcount = 1); called only from 0x47B943 | C |
| 0x5579C0 | slot 146 `GameplayUnitCreated(ICvUnit1* u)` (`[0x9EA718+0x248]`; llvm-objdump, issue 2708, 2026-11-10): calls `u` slots 3, 2, 25, 18, 30, 3 ...; at 0x557CF5 `QueryInterface(u, guidICvUnit2)` through 0x5A7920, calls slot 45 on the result, **Destroys the result at 0x557D1E and goes on calling the argument `u`** (0x557D20 slot 17, 0x557D64.., 0x557DAF slot 2, 0x557DF9 slot 3, 0x557E05 slot 17, 0x557E71 slot 2, 0x557EA7 slot 16). The engine thus borrows its argument for the whole call and its QueryInterface result is a temporary reference. Ends `ret 4` at 0x557F64 | C |
| 0x5A7920 | `QueryInterface(p, guidICvUnit2)` helper: copies the 16-byte GUID 0xA55B90 to the stack and calls `[[p]+0]` | C |
| 0xA55B90 | `guidICvUnit2` {03C93CEC-650A-43DE-9E1B-FE15D75EBA92} (CvDllInterfaces2.h:63-65) | C |
| 0x449E50 | engine utility destructor body; called from 0x42E810 and the app shutdown 0x77E580 | C |
| 0x42E810 | `app->SetEngineUtility(p)`: destroys and frees the object at `[g_Civ5App+0x1460]`, stores `p` | C |
| 0x47B1D0 | LoadCvGameCoreDLL (contains PR #13379's 0x47BA70), only caller 0x6B95C2 in SetActiveDLCandMods 0x6B8E50. Same DLL path and byte 0x2DD2A05 == 0: re-calls `SetDLLIFace([app+0x1460])` and `SetGameDatabase(app+0x944)` on the loaded DLL's context, no new object. Otherwise: old context `SetDLLIFace(0)` path, `SetEngineUserInterface(0)`, 0x42E810(0) frees the old utility object, loads the DLL (0x481730), `new` utility object (0x740840), `SetDLLIFace(new)`, `SetGameDatabase(app+0x944)`, `SetEngineUserInterface([app+0xD0]+4)` | C |
| 0x77E580 | Civ5App shutdown ("*****Civ5App shutting down"): releases `[app+0xD0]`, `[app+0x1460]`, and the log manager 0x6196A54 | C |
| 0x838F80 | export `GetFileSystemMgr`: lazy singleton `[0x6196A68]` (4-byte object, vftable 0xB89930); no code clears it | C |
| 0x81CD40 | export `GetLogFileMgr`: lazy singleton `[0x6196A54]` (0x14 bytes, vftable 0xB88A00); `PeekLogFileMgr` 0x81CD50 reads it; deleted and zeroed only by 0x77E580 | C |
| 0x81CF00, 0x81D0C0 | `FILogFileMgr::GetLog` (slots 3, 4): find-or-create by name in the manager's vector `[this+8..+0xC]`; the same name returns the same `FILogFile*` (0x2C-byte object) | C |

## Player limits (issue 4257, 2026-11-15)

Rows added by worker A2 of epic 0334 (read-only headless Ghidra on a scratch copy of project `civ5`, a byte-pattern scan
of the Lua registrations, source reads of the fork at `04b1211b8`); the behaviour behind each is in
`../03-dll-engine-boundary/player-limits.md` section 3 (source key PL). PreGame offsets named from
`CvDllInterfaces.h` (`ICvPreGame1`).

| VA | What | Sources | Conf. |
|---|---|---|---|
| 0x418C30 | builds a per-major record for slots 0-21 (0x84 B each) plus a count over 64 teams and posts it as one 0x900-byte event (type 0xA8) on the game-to-UI queue; caller 0x6B2C70; consumer not traced | PL | C |
| 0x4223C0 | writes defaults 22 (`MaxPlayerCount`) and 41 (`MaxCityStateCount`) into a map/map-script description object (vtable method) | PL | C |
| 0x435140 | Lua `BroadcastPlayerInfo`: player argument checked < 63 | PL | C |
| 0x475C40 | game-launch pass over slots 0-62 (called from 0x770820 before 0x50B090) | PL | C |
| 0x4E5240 | Lua `PreGame.SetSlotStatus` -> `ICvPreGame1::setSlotStatus` (+0x2C4), no range check in the EXE | PL | C |
| 0x500350 | Lua `TestLeaderMessage`: both player arguments checked < 64 | PL | C |
| 0x50B090 | game-launch random fill ("Choosing Civ", "Choosing Leader", "Choosing Minor Civ"): civs and leaders over slots 0-62, minor-civ types for slots 22 .. 22+numMinorCivs-1 (start 0x16 set at 0x50B71D, compare 0x50B72A) | PL | C |
| 0x51A007-0x51AA78 | registration of the `PreGame` Lua table (push function, `setfield -2`); e.g. `GetSlotStatus` 0x7A6F60, `SetSlotStatus` 0x4E5240, `SetPlayerColor` 0x7A05B0, `SetNumMinorCivs` 0x738480, `ResetSlots` 0x76FAD0 | PL | C |
| 0x533DB0 | network session: number of players (optionally minus a pending list) | PL | C |
| 0x58CBC0 | network game launch (`GAMEOPTION_PITBOSS`, `GAMEOPTION_DYNAMIC_TURNS`); loops over 63 slots; calls 0x770820 and 0x50B090 | PL | C |
| 0x59B680 | `NetPlayerInfo` log formatter: id, slot status/claim, team, colour are 32-bit fields of the message object | PL | C |
| 0x5BA200 | Lua `GetPlayerList`: loops to 63 | PL | C |
| 0x5DC5D0 | slot adjust after a random world size: closed slots to computer/open up to the world's default player count, then slots up to 21 to observer; skipped if any slot is computer | PL | C |
| 0x6A1DC0 | per-frame slot poll (KB 1304): steps the 64 x 0x18 table 0x2DDFE40 and ends on its end address 0x2DE0440 | PL | C |
| 0x6C2960 | save-header writer (active player's civilisation, colour `PlayerColors` type, handicap, speed, world, mods affecting saves) | PL | C |
| 0x6C3D40 | network seat test for one slot; slots above 63 are never available | PL | C |
| 0x6CDEC0 | resolves random set-up choices ("Choosing world size at random", climate, sea level, era); calls 0x5DC5D0 | PL | C |
| 0x6DA0F0 | network player-drop handler; per-player flags at session+0x18C, 64 entries (`player < 64`) | PL | C |
| 0x6FE2D0 | network connection attempt: refuses at 63 players (0x533DB0), seats through 0x76B5D0; logs "could not find slot" | PL | C |
| 0x711C80 | registers the Lua `GameInfo` and `GameDefines` accessors (`GameDefines` from the `Defines` table, which the DLL fills with the `MAX_*` constants) | PL | C |
| 0x76B5D0 | network join seat search: scores slots 0-21 only (bounds 0x76B798, 0x76BA2C) | PL | C |
| 0x770820 | game launch (map file load, random choices 0x6CDEC0, 0x475C40, 0x50B090) | PL | C |
| 0x77AF90 | save-header reader (pair of 0x6C2960) | PL | C |
| 0x7A6F60 | Lua `PreGame.GetSlotStatus` -> `ICvPreGame1::slotStatus` (+0x2FC), no range check in the EXE | PL | C |
| 0x7AEB10 | in-world object key: `(id AND 0xFFFFFF) OR (player << 24)` after `player < 64` | PL | C |
| 0x2DDFE40 | per-slot table, 64 x 0x18 B (.data), stepped by the slot poll 0x6A1DC0, reset by 0x51AFD0, referenced by 14 other functions (indexed by slot, not by leader) | PL | C |
