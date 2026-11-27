# Known crash, hang and data-loss causes on the first beta's player paths (ticket 0636, D223, 2026-11-21)

Decision D223 (the owner, 2026-11-21): a crash cause on a player path for which a fix or a mitigation is KNOWN is fixed
before the build reaches human players; a known-issues line is not a substitute; no new test campaign. This file lists
what the project already knew on 2026-11-21 02:00-03:00 EST, read only (tracker, research, MC-STATE, decision log,
release and install documents, the crash ledger audit, the product source on main 553a0c3ae, the DLL fork's trees at the
pins). No game, build or test was run for it.

**The first beta** = main plus the pins x64-0.1b `1b87dd185` (`deps/dll.lock`) and i0-0.1b `f33fc46a4`
(`deps/i0.lock`), installed by the installer, on EXE builds 26577625 and 4390913. Player paths: the **64-bit path**
(proxy + server + compositor), the **patch switched off** (`vpproxy.ini mode=off` or `vp64helper off`), the installer
and its removal, the modpack form of Vox Populi, the 43-civ variant.

**Two facts that change how the rows read** [C]:

1. **With the patch switched off the player runs Vox Populi's own 32-bit DLL, not the i0 pin.** The installer ships no
   32-bit game DLL; it keeps the player's own VP DLL as `CvGameCore_Expansion2_x86.dll` and the pack's manifest lists it
   by hash (`installer-notes.md` 3 item 2 and 7, D28, D171). So the i0 pin's DLL fixes (bomber freeze, upstream crash
   fixes, path finder) reach players only on the 64-bit path, in the x64 DLL; the patch-off path is stock Vox Populi 5.4.8
   behind the proxy. The i0 pin serves the release take's tests (save test, parity, stock arms) [I: `tools/release`
   README and `release-builds.md` 5; no shipping step for it was found].
2. **In off mode the proxy installs none of its EXE-side guards.** `proxy/src/proxy_rt.cpp` boot: `if (!g_offMode)
   ProxyShell::OnBoot();`, and `OnBoot` (`proxy/src/proxy_shell.cpp`) is what installs the UI message queue guard (0492)
   and the farm builder guard (0535); the UI draw-list growth (0487) is the UI bridge's, configured with the 64-bit path
   [I for uidraw: not traced to its gate]. The proxy DLL is loaded in the EXE in off mode too, so these guards could run
   there; today they do not.

Marks: `[C]` source, dump, lock file or file read; `[L]` seen live in a run recorded in the knowledge base; `[I]`
inferred. Addresses are DX11 VAs of build 4390913 at ImageBase 0x400000; no address of build 26577625 is written here
(D202).

## The table

State "in" = in main or in the pins today; "NOT in" names what would bring it; "unknown" names the one read that settles it.

| Id | Cause (one sentence) | Player path | Source | Fix or mitigation KNOWN? | State in the first beta | Conf. |
|---|---|---|---|---|---|---|
| K1 | The EXE's farm-field layout graph frees an endpoint twice (closed loops through the reversal/split, then the clean-up's delete) and later reads or writes through it: crashes at VA 0x77C6DA / 0x77C70D (weld loop) and 0x536580 (remove-by-index) in huge loads, about 1 in 31 huge product loads, also seen on stock | 64-bit path, any map with farms (huge loads seen); patch off (stock EXE code) | `../02-exe-internals/crashes-huge-load.md` 2, 7, 8 (8.7, 8.9); `../02-exe-internals/farm-field-double-free.md` 1; tickets 0410, 0515, 0547, 0549; D186; D189 (i2-rc1 shipped it as a known issue) | **Yes, fix**: `[Renderer] ServerFarmFields = 1` never calls the EXE's farm builder (epic 0528: 0 crashes in 28 launches incl. 21 huge loads with the option on, 2026-11-18; ticket 0637, 2026-11-21: both EXE builds on the first beta's layout and option set, load + 5 turns + the game's save reloaded, 0 crashes, builder never called [L]; ticket 0655, 2026-11-21 (main 5f4a9b563): WITH the farm-end post: the builder is still never called, so the double free's path is gone; only the builder's last step, the farm-end post, is made from the guard's hook, so the EXE makes each farm's building again (373 of 373 on the huge save, 0 field and 0 track records, both builds [L], `../02-exe-internals/farm-builder-guard.md` 13); **cost: the field pattern is the server's, not stock's, and each farm building stands at the centre of its tile, not on a field's edge (ticket 0657, later); on a build whose sites do not match the post is off with one LOUD line and no farm building is drawn there** (without the post none is drawn at all, `../02-exe-internals/farm-field-double-free.md` 4.4)). **Partly, mitigation**: the farm builder guard F2 (0527, owned by the proxy since 0535) checks the endpoint free list after each builder call and stops feeding the builder on a bad result; it acts after the call, so it lowers the rate by an unknown factor (`farm-builder-guard.md` 8) | Guard **in** on the 64-bit path, active on both builds [L: `../02-exe-internals/new-exe-build-26577625.md` C.4.4, C.5]. Fix **in the first beta's option set** (D230, D231, the owner 2026-11-21: the candidate's option descriptor sets `ServerFarmFields = 1`; to be checked at the candidate take; the option's own default line stays 0, `docs/OPTIONS.md`); it read "NOT in" before: ServerFarmFields was not in the release option set (0626's acceptance names ServerRenderer, ServerUnits, ServerCities, ServerLandmarks, HiddenEmbarks, performance 2-4, GameEventsOneCrossing 0). Patch off: the farm builder guard with rule 2 is **in** since ticket 0635 (main f57eeb3f5, 2026-11-21, D225; `[Proxy] GuardsWhenOff`) [L, both builds]; the fix (ServerFarmFields) is not; it read "neither" before | C sites; I double free (strong, 8.9) |
| K2 | On maps of 16,384 plots or more the graph's 16-bit cell id makes cell 65,535 read as "none"; the weld's refile breaks that cell's list into a ring: hang (9074 T1100), reads at 0x5B1FB7, and the B-dispatcher writes at 0x55D91A-0x55D9E2 / read at 0x7A3F0A tied to the same graph | 64-bit path and patch off, only on oversize (custom or stress) maps, 43-civ games most likely | `../02-exe-internals/farm-field-double-free.md` 2; `crashes-huge-load.md` 9; `crash-ledger-audit.md` 1-3; tickets 0537, 0538, 4855 | **Yes**: rule 2 (4855) skips the builder near cell 65,535 / stops it when that cell holds a node; or ServerFarmFields = 1 | Rule 2 **in** on the 64-bit path (compiled into the proxy's owner, `proxy/src/proxy_farm_guard.h` [C]); 0528's acceptance: 14 oversize 43-civ loads, 0 crashes, stock 9 of 14 [L, build 4390913 only]. Patch off: rule 2 is **in** since ticket 0635 (main f57eeb3f5, 2026-11-21) [L with a 43-civ stand-in DLL; not run with a player's 22-civ DLL]; it read "NOT in" before | C cause (0538); I for the 0x55D9xx tie |
| K3 | The EXE's 4 MB UI message queue overruns on late huge loads (one 128-byte record past the end; 9074 T650/T1100) | 64-bit path; patch off; 43-civ late games most exposed | `../03-dll-engine-boundary/ui-queue-guard-product.md` 1; `../02-exe-internals/message-queue-and-fixed-buffers.md` 1; tickets 0459, 0492, 0635 | **Yes**: EngineQueueGuard (import-slot guard, drops records that do not fit; fork branch `engine-queue-guard`, in the `-ia` test DLL since 2026-09-21; ported into the proxy by 0492) | 64-bit path **in**, active on both builds [L: `new-exe-build-26577625.md` C.4.4 "queue guard: UI message queue guard active", C.5]. Patch off **in** since ticket 0635 (main f57eeb3f5, 2026-11-21, D224): the proxy's guard also runs in off mode, key `[Proxy] GuardsWhenOff` (default 1) [L, both builds: no overrun with it, the overrun reproduces with the key at 0]; it read "NOT in" before. Ticket 0635 as written targets the i0 pin's `-ia` DLL: the line "guard off: not the EXE build this was written for" exists only in `tools/d10/evidence/*/ia-*.patch` [C], and the fork tree at `f33fc46a4` holds no EngineQueueGuard file [C, `git ls-tree`], so 0635 changes no player's game | C, L |
| K4 | The EXE's per-frame UI draw list (1 MB, 4,096 commands) overruns at about 433-454 unit flags in view (#13406) | 64-bit path; patch off | `../03-dll-engine-boundary/upstream-13406-product.md` 0, 9; `docs/OPTIONS.md` UiDrawListGrowth; ticket 0487 | **Yes (mitigation)**: the proxy grows the buffers 4x (always since 2026-11-22: the owner, in person, one-off 9081 removed the option; was UiDrawListGrowth = 1, installer-notes 16.9) (limit moves to about 1,500 flags, does not vanish) | 64-bit path **in**, default on, active on both builds [L: C.4.4, C.5 "uidraw: EXE checks passed ... grown"]. Patch off **NOT in and not possible** without wrapping the fallback DLL's context object (ticket 0635, [C]: the growth is driven per frame by the proxy's Lua bridge, which exists only with the patch on); stock's limit of about 433-454 unit flags in view applies there, a crash cause without a fix in off mode. Upstream's form needs #13379 and a VP Lua line VP 5.4.8 lacks; the proxy's form is not run in off mode [I, fact 2] | L; I which buffer first |
| K5 | VP 5.4.8's UI Lua: loading a save from the game menu while a game runs can crash (UI callbacks released after their context is gone; upstream #13463) | both paths, any form | `../03-dll-engine-boundary/upstream-lua-fixes.md` 1, 3; `installer-notes.md` 8a; `docs/INSTALL.md` Known issues | **Yes**: upstream commit 475bd4997 (VP's own Lua, merged 2026-11-09) | **in, by the installer** (ticket 0638, main 285e98418, 2026-11-21, D225): VP's own fixed file is placed where the installed file holds 5.4.8's exact hash, one box ticked by default; with the box unticked as before; [C] the files in place, the crash itself not reproduced on demand; it read "NOT in" before | C fix; I crash on our path |
| K6 | VP 5.4.8's Lua leaves database statements open; changing mods after a game rebuilds the database half: on the 64-bit path the server fails (D125 popup, exit 3, no play), on stock the game may misbehave (#13467) | both paths | `upstream-lua-fixes.md` 5; `installer-notes.md` 8a; ticket 0184; `proxy-design.md` 23.7 | **Yes**: upstream 14d16fa3d (VP Lua); fork branch `fix-cpk-query-step-to-end` holds it for tests | **in, by the installer** (ticket 0638, main 285e98418, 2026-11-21, D225): VP's own fixed file is placed where the installed file holds 5.4.8's exact hash, one box ticked by default; with the box unticked as before; [L] fixed on the 64-bit path (box off: NO PLAY after a mod change; box on: back at the main menu); it read "NOT in" before | C, L |
| K7 | VP 5.4.8's `Events.ClearHexHighlights` on maps wider than 181 plots writes past the EXE's plot highlight grid (crash, e.g. leaving the city screen with EUI) (#13539) | both paths; custom maps 182-198 wide only | `upstream-lua-fixes.md` 4 | **Yes**: upstream aa7a29b38 (VP Lua) | **in, by the installer** (ticket 0638, main 285e98418, 2026-11-21, D225): VP's own fixed file is placed where the installed file holds 5.4.8's exact hash, one box ticked by default; with the box unticked as before; [C] the files in place, not reproduced on demand (needs a map 182-198 plots wide); it read "NOT in" before | C call sites; I write |
| K8 | Interface Lua calls a CvUnit method through an instance whose unit was freed (use after free in CvLuaUnit::lGetPlot / GetTransportUnit) | 64-bit path (faults: the page is gone in the server); patch off: same read of freed heap, usually no fault | `../03-dll-engine-boundary/lua-stale-instance.md` 1-2; tickets 0576, 0578 | **Yes**: the server's stale-instance guard (nil + one LOUD line) | 64-bit path **in** (merged 5e1a16e15, 2026-11-19); seen working on build 26577625 (C.7.4: 200 LOUD lines, no crash) [L]. Patch off: stock behaviour, no fix known in the DLL | C |
| K9 | The EXE's Granny bone getter reads a unit model's node table through a dead model during a load (VA 0x8E81C3; a timed-trigger copy keeps a raw pointer to a model that 0x734DE0 destroyed) | 64-bit path (seen once, 2026-11-16); never seen on stock | `../02-exe-internals/crash-load-8e81c3.md` 6, 9; `crash-ledger-audit.md` row 5; tickets 0539, 0543, 0544 | **No** (D185 open; object not named; mechanism medium confidence) | Dump-on-crash tooling in the harness only (0543); nothing in the product | C site; I mechanism |
| K10 | Execute fault through a stale code pointer during the mod deactivation after a game with (4a) Squads for VP (absolute 0x7C3DA2EA, proxy still attached) | 64-bit path with an extra mod; seen once 2026-11-13 | `crash-ledger-audit.md` 8.1.1; `../03-dll-engine-boundary/proxy-design.md` 20.1, 23.5; ticket 0545 | **No** (6 reruns on batch 4 without the end; module unknown) | Open under D185; the proxy's crash filter (0419) now names the access kind and module on a recurrence | C run; I mechanism |
| K11 | Loading a map above about 20,000-21,800 plots overflows a thread stack in the EXE's load | both paths; oversize custom maps only | `crash-ledger-audit.md` row 11; `../02-exe-internals/allocations-and-memory-map.md` 6.1 | **No** (EXE code) | Nothing in the product | L limit; I mechanism |
| K12 | The 32-bit process runs out of address space on very large late games (stress map T225: AV, or DXGI device removed) | patch off only (the 64-bit path moves the DLL's memory out of the EXE) | `crash-ledger-audit.md` rows 12-13; `../08-performance/metrics-ledger.md` (9073 stock row) | **Yes**: the 64-bit path itself | **in** for players on the 64-bit path; patch off stays stock | L; I mechanism |
| K13 | A save of the other variant (22 save in the 43-civ build or the reverse) is read past its end or misread | 64-bit path; patch off | `../03-dll-engine-boundary/player-limits.md` 0 item 4, 8.3 verdict; tickets 0354, 0359 | **Yes on the 64-bit path**: the server refuses it before the DLL reads (D118 report, popup), and the stream-read catch (0359) | 64-bit path **in** (since 2026-11-15). Patch off: upstream behaviour (crash early in `Game::Read`, or misread) [I]; no refusal exists for VP's own DLL | C, L (64-bit); I (stock) |
| K14 | `config.ini [DisableSystems]` keys beyond the product's set crash or hang the EXE: River/Arrows NULL batch (0x8CD492), Overlay off (0x574B03), StrategicView off (main-thread hang), Unit/MovementPath at boot (0x9A3CF7) | 64-bit path and patch off, only with a hand-edited config.ini | `disable-systems-and-masks.md` 2; `world-systems-and-stubs.md` 6; tickets 3516, 0127, 3010; D93, D116, D125 | **Yes**: the helper writes only the A'' key set (D33 minus River and Arrows, Overlay kept); StrategicView handled (3010 option A); a failed 64-bit start restores config.ini by hash (3516, D125) | **in** (`proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2`); a player who hand-edits config.ini crashes stock the same way | C, L |
| K15 | The patch's DLL is not loaded (VP reinstalled over the patch, mods not activated) while config.ini keeps the patch's memory savers: flat red map without terrain (degraded play, not a crash) | installer / any later VP install | `installer-notes.md` 8, 13.5, 13.7; D125 | **Yes, mitigation by design** (the owner, D125 [reworded for privacy]: keep the memory saving and accept a loud failure): the off switch, the beta text, the installer's stale-record clean-up running the helper's `off` first (12.7) | **in** as designed | C, L |
| K16 | VP64Options.ini deleted (as the options page suggests for a reset) re-created at defaults: ServerRenderer 0 in a D33 session gives a world without terrain; a refused compositor gives the same | 64-bit path | ticket 0624; `options-file.md` 7.2 | **Yes**: re-create from the release's option set with ServerRenderer = 1, else NO PLAY with a D118 report (server-start/options-renderer-off, server-start/composite-refused) | **in** main (2f395cc6d, 2026-11-21 00:27); the modpack-form cases passed live on 2026-11-21 (ticket 0645, main 00103001e) and ticket 0624 is closed; it read "ticket state review" before | C; L mods form and modpack form on 26577625 |
| K17 | The server's renderer fails at init or in play and the game goes on with a grey map, no popup | 64-bit path | ticket 0600; D213; `../05-graphics-composition/renderer-failure-modes.md` | **Yes**: D125 route (report, popup) | **in** (merged 2026-11-20 06:43) | C |
| K18 | The fog's install check ended huge late loads 10 s after the first map frame (exit 3, no popup) | 64-bit path, huge late loads | ticket 0491; `ui-queue-guard-product.md` 1 | **Yes**: the wait counts map frames | **in** (merged 2026-11-17) | L |
| K19 | AI bombers striking a submarine never end the strike; the game freezes (the owner's report) | 64-bit path; patch off | tickets 0456, 0477, 0485; D176 | **Yes**: DLL fix in pin batch 4 (2fbf08f9a) | 64-bit path **in** (x64-0.1b contains batch 4 [C, `deps/dll.lock`]). Patch off **NOT in** (VP's own DLL, fact 1) | C, L |
| K20 | DLL crash and precondition faults fixed in the pins: religion AI divide by zero (0078), counterspy log (0080), owned plots without a city (#13482), homeland move name (#13566), Lua hooks during capture (#13395), Lua plot improvement index -1 precondition with EUI (0421), pantheon coordinates (0508), path-finder generation wrap (A10), not-refreshed node cache (4813) | 64-bit path; patch off | `deps/i0.lock` and `deps/dll.lock` commit lists; `upstream-fix-batch-1.md`, `-2.md`, `-4.md`, `-5.md` | **Yes** | 64-bit path **in** (both pins carry them [C]). Patch off **NOT in** (VP's own DLL, fact 1) | C |
| K21 | Faults of the 64-bit path's own code found and fixed: CRT abort on a stream read past the end (0359), the proxy's served-call use after free (0451), the x64 database Count race between two channels (0165), the proxy route's address-space growth to a bad_alloc at turn 420 (0102), the first-boot context destroy (2500), a mixed STL checking setting (0609) | 64-bit path | the tickets; `proxy-design.md` 15, 18.6, 27; `crash-ledger-audit.md` 8.1 | **Yes** | **in** (all merged 2026-11-10..20) | C |
| K22 | In the modpack form the EXE's DLC scan loaded the x64 DLL from `VP64\` and crashed at start | installer, modpack form | `installer-notes.md` 14.9, 14.12, 15; ticket 2606 | **Yes**: the x64 DLL ships as `CvGameCore_Expansion2_x64.dll` | **in**; modpack form on build 26577625 played two games clean [L, C.7.3] | L |
| K23 | The installer named only build 4390913 as product-ready, so on 26577625 it warned or asked although the proxy runs the 64-bit path | installer | `new-exe-build-26577625.md` C.7.4 C | **Yes** | **in**: `tools/installer/patch64.iss` `ProductReadyExeBuild` names both builds [C, main 553a0c3ae] | C |
| K24 | The instance-aware (`-ia`) test DLL ends 0xC0000005 about 1 s after the game's exit. On the release journeys' stock route (proxy with `mode=off`, the `-ia` DLL as its fallback) the fault is inside the EXE's `FreeLibrary` of the PROXY: the proxy frees the `-ia` DLL in its `DllMain`, then its own CRT static destructors call `operator delete` through the proxy's MSVCR90 import slot, which the `-ia` DLL's MemoryImports had pointed at its `ThunkOperatorDelete` in the now-unloaded image (details: section "K24 in detail") | none (test DLL only; the installer ships no 32-bit DLL, and a player's own VP DLL rewrites no import slot) | tickets 0678 (dump), 0483, 0484, 0164; `scene-catalogue.md` 12 | Yes, in the `-ia` DLL's unload path (not built): undo MemoryImports and the Lua allocator hook at `DLL_PROCESS_DETACH` (section "K24 in detail") | not on a player path [C, fact 1]; fix NOT in the `-ia` patch | C (dump, 0678) |
| K25 | A stall of the server's own render thread (4.8 s of synchronous decal-texture reads in one terrain bake on a loaded disk, then more slow bakes) made the cities' model hold of 2000 ms of WALL time end a running game (server exit 5, EXE exit 3); the units' readiness hold had the same rule | 64-bit path with ServerCities / ServerLandmarks / ServerUnits on a busy PC | ticket 0641; `../05-graphics-composition/city-renderer.md` 16; 0637's run of 2026-11-21 11:39 | **Yes**: the holds count step time (a stall counts at most 250 ms), 30 s; the bakes' reads off the render thread are ticket 0642 | **in** with ticket 0641 (offline [C]; live on c5h-d 2026-11-21: stalls of 3 and 12 s survived, the forced end showed the popup with the reason and the report [L], `city-renderer.md` 16.5); a second case on c5h-new-mpk (landmarks, 2233 ms) has the same shape (16.6) | C, L |
| K26 | A game cannot load when one terrain decal lies wholly past the map's east (or west) edge: the EXE clamps each side of its rectangle on its own (min x > max x), the decal tap took that for an unreadable layout and refused the whole first set (FAILED LAYOUT), and the server's 20 s install bound ended the game with the D118 popup ('decals cannot install'); no crash. Seen twice on the large 43-civ journey save on build 26577625 (one road curve, batch 2 slot 22; the same record exists on build 4390913, where the load passed by a race of the first set); in play the same form was not fatal (0491, T1100) | 64-bit path, any map with such a record at the first decal set | ticket 0649; `../05-graphics-composition/terrain-decals.md` 21; found by 0643's large journey 2026-11-21 | **Yes**: the tap leaves that record out (the EXE's own draw never takes it), counted, one log line per set; every other inverted rectangle still refuses the set | **in** main (918a5837a, 2026-11-21 16:30): the save loads live on c5h-new-b, the seam's decals equal stock [L]; census: 1 record on the large 43-civ save, 0 on the large 22-civ, huge and Tiny saves | C, L |
| K27 | A land/water change within about 3 plot rows of the map's top or bottom edge sent the grid's margin cells into the live-terrain producer's clean check, which read the plot table at `py = gy - 2` with no bounds check (up to 2 x w plots before the table or past its end). server.exe crashed on its worker thread (access violation, huge h18 `ice_land` at (103,2), 2026-11-21 19:18, build 4390913; reproduced at 19:26); where the read landed in mapped memory it read garbage instead, which can give a false river-edge wait only with protectRivers off (never in the product, whose options keep every protect on). The case had never run before that evening: h18 was the series' first class edit in an edge row, and the acceptance series should have had a class edit in the first and the last row of every map size | 64-bit path with ServerRenderer and live terrain's land/water step: a plot changing land/water class near the north or south edge (polar ice to land, a mod or the world builder), any map size | epic 0615 (W9: the cause, W12: the fix); `../05-graphics-composition/live-terrain.md` 11.8 (the crash and the repro arms) and 11.9 (the fix, tests, sweep, audit) | **Yes**: the loop skips cells outside the map's rows and columns (commit 30aac843c on w12-h18, cherry-picked alone to main from branch h18-main); a margin cell has no plot, so every in-map decision is unchanged. Tested: an offline ASan repro on the recorded huge inputs fails before the fix; 5 unit tests (`lw_edge_row_*`) fail before and pass after (48 run, 0 failed, 0 ASan reports); a 48-case ASan sweep of edge rows and the seam on the huge and Tiny recordings gives 0 reports; an audit of every computed index found no other unguarded one | **in** with the h18-main merge: live h18 x 3 clean and the row-0/row-79 class edits drawn live on build 4390913 (c5h-a, W9) and on build 26577625 (c5h-new-b, W12), layout t12 | C, L |
| K28 | The composite's cut-mask tap takes a SEH fault while copying the tree grid's pool for a broken layout (FAILED EXCEPTION, off for the process), although the fault came during the grid's growth after a huge load, 1 s after a torn read, when the EXE had replaced the pool [I for the replacement]; with `render_forest` 1 the server ends the 64-bit path (exit 5) and the proxy ends the EXE at the load | 64-bit path with ServerRenderer, the composite's cut tap and the server's forest: a huge-map load (1 of 6 H loads of TS0438-H-T300 on oldpc, build 4390913, 2026-11-22) | ticket 5030's H series (oldpc job 20261122-222342-00f4, run r03-p1); `tracker/issues/0690-cut-tap-fault-during-grid-growth-ends-huge-load.md` (the MC numbers it); `../05-graphics-composition/shell-composite-taps.md` (torn reads), `../05-graphics-composition/trees.md` 14.8 | **Yes**: on a fault while copying the directory or the pool, the header is read again (and up to 3 more times 2 ms apart while unchanged); a moved header is a torn read (read again at the next poll), an unchanged one stays FAILED EXCEPTION (`shell/composite/cut_tap.cpp` `HeaderMovedAfterFault`; tests and negative control `shell/tests/test_cut_tap.py`, `shell/evidence/0690/`; `../05-graphics-composition/shell-composite-taps.md` 7.6). No popup in that run: the runner's development layout has `pack=0`, which writes no report and starts no popup (7.6) | **in** on branch `task-0690-cut-tap-fault` (ticket 0690, 2026-11-22; in the next beta once merged); live 2026-11-23 on c5h-a: 3 huge loads of K28's save, 0 FAILED, torn reads counted (35 / 17 / 8), no fault came up in them (the fault branch proven by the tests only; 7.6) | C, L |
| K29 | A save loaded with a different set of mods than it was made with, including AffectsSavedGames=0 mods that change rules tables (seen: VernsTweaks' added promotion), makes the DLL misread the save and read past the end in ICvPlayer1::Read: exit 6, engine-exception, kRtE_StreamRead; the DLL warns 'Save data hash mismatch' first | the player loads an older save after changing their mod list (the owner's report 2026-11-22 20:53, and its 18:23 run) | the DLL's save read (not the patch); stock VP crashes the same way (0681 [L], c5h-c) | **mitigation, known**: no load is refused before the DLL itself fails to read (since 0744, 2026-11-25: the server ends at the DLL's first Deserialization Error, K31); since 0681 (merged a2d2dead7) the server logs one LOUD line with the header's mods, the active mods and the difference, the report says code save-mod-mismatch, and the popup opens with a plain line telling the player to load the save with its mods (the patch stays on, D240) | IN after a2d2dead7 (the third candidate); a second signature with the generic popup: K31 (0741) | C, L |
| K30 | When the proxy's boot fails (seen: shell_composite.dll missing while ServerRenderer = 1), DllGetGameContext returns NULL and the EXE reads through it 11 ms later on its boot thread (0xC0000005, read of address 0; on build 4390913 at 0x47B95E, the boot's read of the context's vtable after the EXE's unchecked getter 0x66A390 [C, disassembly, `../03-dll-engine-boundary/proxy-design.md` 34.1]; 0720 saw it at the same code of build 26577625, whose address stays out of git, D202. Corrected 2026-11-24 by 0721: the row had named that build's offset as the site) | a broken or partial install (a file missing or quarantined). In the shipped layout (`pack=1`) the composite case was already no play in a D33 session (0624); what still crashed there was a missing or unloadable 32-bit DLL (`CvGameCore_Expansion2_x86.dll`), with the patch on (the fallback) or off (`mode=off`). `pack=0` development and test layouts crashed on every boot failure | the proxy: every boot path that had no context returned NULL (the list: `proxy-design.md` 34.2) | **Yes**: ticket 0721: `DllGetGameContext` never hands a NULL of the proxy's own to the EXE; every such path ends on the report route (the report, `vp64helper failed --pid`, exit code 3; stage `fallback-dll` for the 32-bit DLL with its own popup words, no Remove button); a NULL from the 32-bit DLL's own export is handed over unchanged (stock behaviour); no EXE write (`proxy-design.md` 34.3) | on branch `task-0721-null-context-boot-failure` (ticket 0721, 2026-11-24); target beta 4 | [L] 0720's run on c5h-new-b, 2026-11-23 23:4x (MC-B #12403); fix: [C] four offline suites green, [L] 0720's set-up again on c5h-new-b 2026-11-24 00:58 with 0721's proxy: NO PLAY, report, popup, EXE exit 3, no crash (`proxy-design.md` 34.5) |
| K31 | K29's cause, second signature (ticket 0741, 2026-11-25): after the DLL's 'Deserialization Error' in a save loaded with other mods, the misread damaged the server's memory before it ran off the stream's end; the idle render thread faulted first (0xC0000005 in `IceTapReader::poll`, `srv_ice_tap.cpp:434`/`:59`, the read of the ice control block through a pointer written once at start), then the device release and the process itself (exit 0xC0000005, no minidump). The player got the generic server-death popup, not K29's mods line: the save-mods override leaves `render*` codes alone and the proxy ignores server-fatal.txt when the exit code differs | the player loads a save made with other mods (the owner's beta 3 game, 2026-11-23 23:27, cand5 d56fbd94f, EXE build 26577625) | `../05-graphics-composition/renderer-failure-modes.md` 6; `../03-dll-engine-boundary/save-mod-mismatch.md` | **Yes** (ticket 0744, the MC's ruling (c) of 2026-11-25): (c) remote mode ends the server at the DLL's first 'Deserialization Error' line of a load, on the reading thread inside the DLL's log call (`SrvSaveMods::SetEndHook` = `SrvRemote::ReportAndExit`, exit 6), with the save-mod-mismatch record and the player's words, before the misread reads on; (a) after a deserialization error a `render*` end is recorded as save-mod-mismatch too, the render code kept as what ended the server; (b) server-fatal.txt carries the server's pid and the proxy takes a record of this pid when the server then dies by an exception code. Same-mod-list saves never log the line and load as before (D239); direct mode reads on (`../03-dll-engine-boundary/save-mod-mismatch.md` 6) | **fix in** on branch `worktree-agent-a73802740d77a0fd3` (ticket 0744, 2026-11-25; in the next beta once merged): offline [C] only (`server/tests/remote/test_savemods.py`, the proxy's `death-server-fatal-pid` cases), no game run; beta 4 (dc259f0b1) does NOT have it. (Was until 0744: 'mitigation known, not reaching the player; NOT in beta 4'.) | C (site, beta 4; the fix offline), I (cause: no dump) |
| K32 | Beta 4 player report (ticket 0804, 2026-11-26): with the game's MSAA on (`MSAASamples = 8`) the whole map is grey and the game ends about 10 s into the map; `MSAASamples = 1` works. Cause [L, reproduced on c5h-a 2026-11-26 14:33]: with MSAA **and `AllowLeaderAA = 1`** (the reporter's ini) the EXE keeps a single-sampled swap chain and draws its scene into its own 8-sampled FP16 target, which it resolves into the back buffer; no list clears the back buffer, the composite's `clear_bb` rule never matched, the server's world was never drawn, and the fog's 10 s wait for an overlay pair ended the game by the D125 route (`stage server-death`, `code render-fog`). With `AllowLeaderAA = 0` the swap chain itself is multisampled, the game plays, and only the depth write was refused (`TEXTURE2DMS` scene depth) | 64-bit path with MSAASamples > 1 (any EXE build seen); the end only with AllowLeaderAA = 1 | `../05-graphics-composition/shared-texture-and-compositor.md` 16; `shell/composite/shell_composite.cpp` `LkDsvUsable`, `IsMsScene` | **Yes**: fix 1 (merged 25141d337): the depth write accepts a multisampled scene depth; fix 2: a multisampled render target of the back buffer's size while the back buffer is single-sampled stands for the back buffer, and the insertion draws the server's colour and depth into it; fix 2b: also a single-sampled one, recognised by a back-buffer-sized depth clear in the same list. No option (parity, D255), no write to the player's ini. Player workaround until then: `AllowLeaderAA = 0` or `MSAASamples = 1` | fix 1 in main (beta 5); fix 2 on branch `task-0804-msaa`, target beta 5 | [C] the reporter's logs; [L] stand-in `shell/tests/test_msaa.py` 10/10 (fails-before: the scene cases); [L] c5h-a 2026-11-26: repro L8; fix 2 F8L (10 min of pans and wheel-ins), fix 2b F2L, F4L, F8L, F8Z (2x, 4x, 8x; F8Z with zoom-outs): world drawn, depth written at N samples, game alive; [L] laptop and oldpc 8x jobs with fix 1 |
| K33 | Beta 4 player report (ticket 0805, 2026-11-26): after a game, back to the main menu and the mods reloaded, the game hangs at `Reload GameCore DLL`. The hang did NOT reproduce (5 sessions, 13 boots, 8 of them re-activations in a running EXE, builds 4390913 and 26577625, after a save, a changed set). What was found [C, cdb stack and the proxy's PDB]: the engine's one Lua VM outlives each proxy image (the EXE unloads the game DLL at every mod change), and each image leaves C functions of its own in it: one `VP_LUAEXEC` engine Lua thread per boot, never freed, whose environment holds the DLL's registration globals (Game, Players, ... as FuncThunk closures), and the server-value stubs' metatables (`__gc`, `__index`, `__call`). After a re-activation at another base, the harness's Lua channel picked an old thread's environment during a load and called the unloaded FuncThunk: the EXE executed unmapped memory (0xC0000005, 3 of 3 times), or would run whatever image is mapped there now (the base DLL or the next proxy image, which session 3 and 4 showed at those addresses: a crash or a hang) | 64-bit path, a mod change after a game in one EXE session; the seen trigger is test-only (the Lua channel), an old stub's `__gc` (run by any later collection) is a product path [I] | `../03-dll-engine-boundary/proxy-design.md` 38; `proxy/src/proxy_luaexec.cpp` `OnGameDatabaseCached`, `proxy/src/proxy_lua.cpp` `PushMeta` | **Yes**: at the orderly shutdown (`SetDLLIFace(NULL)`, main thread, image still mapped) the image's values are made inert (metamethods removed, master-form function tables emptied, its registry keys removed) and the persistent thread is freed (`ProxyLuaExec::OnShutdown`). Not covered: the UI queue guard's pin (on another module's hook over its slot) keeps the image and gives no new boot, the report's exact words; and the proxy's five worker threads end by TerminateThread after a bounded wait for their busy flag (a kill inside a longer tick could leave a lock held [I], not seen) | fix on branch `task-0805-reload-hang`; beta 5 gate (D223) | [C] stack (cdb, 2026-11-26 14:28) and PDB; [L] 3 crashes, 8 clean re-activations |

## K24 in detail: the stock route's exit crash under a debugger (ticket 0678, 2026-11-22)

*Correction 2026-11-22 (0678): K24's row read "MemoryImports' allocator slots point into the unloaded DLL | ... | n/a",
taken from 0483's run without the proxy; the validation tiers' sightings (`validation-tiers.md` 16 and 19: c5h-new-a
2026-11-22 08:07, 11:29, 13:36, 14:17, 16:34) had no dump and were classed `[I]` by route, code, moment and DLL. The
dump below confirms the class and corrects where the first faulting call is on the release stock route.*

**Set-up** [L]. Instance c5h-a (Goldberg, EXE build 4390913, C:), the release stage's stock layout laid by hand as
`tools/release/stages/hi_journey.py` `_lay` does: `provision.py set-dll` with `i0ship-f33fc46a-ia` (6C8CD5F4..., the
first beta's i0 pin plus `ia-5d18d3d6c-4968.patch`, from take 0671's layout), `provision.py set-proxy --pdb` with that
take's `x64-1b87dd18-p06d9e82` (proxy DLL 9245A061...), `vpproxy.ini mode=off` (the proxy loads the `-ia` DLL as
`CvGameCore_Expansion2_x86.dll`, no server). No test pack (mode=off does not read it). `GuardsWhenOff` default (1). A
copy `C5H-0678-BASIC-T0` of release-basic's start save `HI-4506-BASIC-T0` (68ED61E6...), loaded by the front end
(`proxy/tools/live/prepare.py --load`), in game, 20 s, then 0164's invasive cdb (`shell/evidence/0164/cdb_watch.py`'s
command script: full dump on a second-chance exception, `-pd`) and `UI.ExitGame()` over the Lua channel (0483 run A's
call). Before and after: SHA-256 backup and restore of the standard set (14 files, all `equal` or `absent` after),
`set-proxy --original`, `set-dll` back to the instance's 3C2F47DA..., `verify --quick` exit 0, run outputs
(autosave `AutoSave_Initial_0000`) left as the game wrote them. The release journey itself exits by the menus (game
menu, main menu, EXIT, Yes); this run exited by the same Lua call from the game, see "Not verified".

**Run A, MemoryImports on (as the release stock route) [C, dump, 2026-11-22 20:59:57 EST].** The process ended 22.5 s
after the request (debugger and a 1.5 GB dump included). Event order in cdb's log: `Unload module ...\(1) Community
Patch\CvGameCore_Expansion2_x86.dll at 7c660000` (the `-ia` DLL), then the access violation, first and second chance, on
the main thread:

```
eip=7d411570  <Unloaded_CvGameCore_Expansion2_x86.dll>+0xdb1570
<Unloaded_CvGameCore_Expansion2_x86.dll>+0xdb1570
CvGameCore_Expansion2!std::vector<const char *>::~vector (inline: _Tidy, allocator::deallocate)   [the proxy]
CvGameCore_Expansion2!`dynamic atexit destructor for 'v'+0x12                                     [the proxy]
CvGameCore_Expansion2!_CRT_INIT+0x1be / __DllMainCRTStartup+0xb7 / _DllMainCRTStartup+0x1e         [the proxy]
ntdll!LdrUnloadDll+0x158 ; KERNELBASE!FreeLibrary+0x16
CivilizationV_DX11+0xb360                                                                         [the EXE]
```

`CvGameCore_Expansion2` here is the proxy (303f0000, image `...\(1) Community Patch\CvGameCore_Expansion2.dll`, symbols
from the take's proxy PDB). Read in the dump: the return site `304b759d call CvGameCore_Expansion2!operator delete`, and
the proxy's MSVCR90 import table (`!dh -i`) holds `??2@YAPAXI@Z` 7D4128F0, `??3@YAXPAX@Z` **7D411570**, `??_U@YAPAXI@Z`
7D412940, `??_V@YAXPAX@Z` 7D411620, `_malloc_crt` 7D412670, `calloc` 7D4126C0, `free` 7D4113A0, `malloc` 7D412670,
`realloc` 7D412710: every allocator slot points into the unloaded `-ia` image (7c660000-7db85000), every other MSVCR90
slot into MSVCR90 (6192xxxx). Named with the `-ia` PDB (the DLL opened as an image in cdb): +0xdb1570 =
`ThunkOperatorDelete` (one body for all 32 slots: the template does not use its slot number, so the linker folded
them), +0xdb28f0 `ThunkOperatorNew<6>`, +0xdb2670 `ThunkMalloc<6>`, +0xdb13a0 `ThunkFree` (folded),
+0xdb2710 `ThunkRealloc<6>`: the proxy is MemoryImports' module slot 6.

So the chain is [C]: the `-ia` DLL's `DllGetGameContext` (first called through the proxy's boot in off mode) runs
`MemoryImports::Install()`, whose `PatchLoadedModules()` rewrites the MSVCR90 allocator slots of every loaded module,
the proxy included; nothing puts them back (the `-ia` `DLL_PROCESS_DETACH` undoes only `EngineAllocTrace`). At the
game's exit the EXE calls `FreeLibrary` on the proxy; the proxy's `DllMain` (`DetachOnUnload`, `proxy/src/proxy_rt.cpp`,
mode=off) frees the `-ia` DLL ("detach: mode=off: freeing the original 32-bit DLL"); then the proxy's C runtime runs its
static destructors after `DllMain` returns, the first one that frees heap memory (a static `std::vector<const char*>`
named `v`, as in `proxy/src/proxy_session.cpp` 438 or `proxy/helper/proxy_cfgini.cpp` 440/446) calls `operator delete`
through the rewritten slot, and the CPU executes freed code. The EXE's filter and Goldberg record nothing and no box
appears (as in 0483), which is why the tiers see only the exit code.

Difference from 0483 run A (no proxy, 2026-11-17): there the first fault was the EXE's own `std::string` allocation
(MSVCP90's slot, `ThunkOperatorNew<5>`) after it had unloaded the `-ia` DLL; with the proxy in front, the proxy's
static destructors fault first, still inside the EXE's `FreeLibrary` of the proxy. Same cause, MemoryImports' slots.

**Run B, control with `VP_MEMIMPORTS=0` (launch `--env`) [C, dump, 2026-11-22 21:08:37 EST].** Same layout and save.
Now both modules unloaded cleanly (`Unload ... CvGameCore_Expansion2_x86.dll at 7d620000`, then `Unload
...CvGameCore_Expansion2.dll at 30880000`: the proxy's destructors passed), and the process faulted later, in the EXE's
`lua_close` (`CivilizationV_DX11+0x2217a7` -> `lua51_Win32!lua_close` -> the state's allocator) at
`<Unloaded_CvGameCore_Expansion2_x86.dll>+0xd90500` = `MemoryDiagnostics::LuaAllocHook` (named with the `-ia` PDB): the
Lua allocator hook of `MemoryDiagnostics::InstallLuaAllocHook()` (`lua_setallocf`, no switch, no undo), the second
hazard that `../03-dll-engine-boundary/proxy-design.md` 21.4 found at a mod change. So `VP_MEMIMPORTS=0` does not avoid
K24; a fix must undo both.

**Fix (proposed, not built; in the `-ia` DLL's unload path; no product change).** In `CvGameCoreDLL.cpp`'s
`DLL_PROCESS_DETACH`, only when `lpReserved == NULL` (a `FreeLibrary`, not the process's end), before the existing
`EngineAllocTrace::Uninstall()`:
1. `MemoryImports::Uninstall()`: for every module it patched, write each rewritten MSVCR90 slot back to the original
   function, only where the slot still holds this DLL's thunk, with the same `VirtualProtect` and module pin as
   `PatchModule`. Safe for blocks already handed out [C, source of `ia-5d18d3d6c-4968.patch`: each thunk records and
   then calls the original MSVCR90 function with the caller's arguments and returns its result unchanged, e.g.
   `ThunkOperatorDelete` -> `g_pfnOperatorDelete(pBlock)`].
2. The Lua allocator hook back: `lua_setallocf(L, g_pfnLuaPrevAlloc, ud)` with the state and user data recorded at
   install (the hook keeps `g_pfnLuaPrevAlloc` from `lua_getallocf` and returns the previous allocator's result
   untouched [C, same source], so the blocks stay valid).
3. `EngineQueueGuard` has no undo either (only `Install`); not seen faulting at the exit (on 4390913 its slot is
   the EXE's UI message queue, on 26577625 it stays off), but the same rule applies.
The `-ia` DLL's detach runs inside the proxy's `FreeLibrary(g_fallback)`, i.e. before the proxy's static destructors
and before the EXE's `lua_close`, so this order covers both run A's and run B's faults [I until a build with the change
exits 0 on this layout]. Alternative without touching the instruments: pin the `-ia` image (`GetModuleHandleEx` with
`GET_MODULE_HANDLE_EX_FLAG_PIN` in `MemoryImports::Install`), so the thunks stay mapped to the end of the process; it
cannot reload a fresh `-ia` DLL after a mod change, so not recommended. Or no fix: the crash is after the work is on disk
and the tiers already report it as "K24, after the replay passed" without changing a verdict (`validation-tiers.md` 16).
**Recommendation**: the fix (1)+(2) on the `-ia` patch at low priority, so that stock-route exit codes are clean and a
real stock crash at the exit is not hidden under K24; the MC decides.

**Players [C by mechanism].** Nothing of this is on a player path: with the patch switched off the proxy's fallback is the
player's own VP DLL, which has no MemoryImports and no Lua allocator hook (fact 1; 0164 s2 and 0484: the plain build has
no `VP_MEMIMPORTS` string). The proxy's own guards in off mode give their slots back in `ProxyShell::OnUnload` before the
fallback is freed (`DetachOnUnload` order, `proxy_rt.cpp`) [C, source; not seen faulting in run A or B].

**Not verified.** (a) EXE build 26577625 (the tiers' sightings were on c5h-new-a, 26577625; the chain does not depend on
the EXE's addresses, only on the proxy's import table and the `FreeLibrary` order) [I]. (b) The release journey's own
exit path (game menu, main menu, EXIT, Yes) under a debugger; this run used `UI.ExitGame()` from the game, the call the
menus end in [I]. (c) The fix itself: not built or run.

**Evidence.** Full dumps (1.5 GB each) and cdb logs: `F:/c5-64bit-work/0678/dumps/A/` (run A) and `.../B/` (run B), kept
on F: for the MC's decision (delete after it), with the driver beside them (`F:/c5-64bit-work/0678/k24_run.py`:
0483's `exit_run.py` with the stage's stock layout and `--env`; not committed: outside 0678's files), the run records
and the offline cdb readings (`an/`); the C: scratch was deleted at the hand-back. The commands are the runbook's
3.2, 3.5 and 3.7 plus `cdb -z <dump> -y <proxy bin> -cf <commands>` for the offline reading (`lmvm`, `ub`, `!dh -i`) and
`cdb -z <the -ia CvGameCore_Expansion2.dll> -y <its folder>` with `ln CvGameCore_Expansion2+<offset>` to name an
offset of the unloaded image.

## Build-keyed protections on EXE build 26577625 (as written, 2026-11-20/21)

Each feature checks the EXE by PE stamp and SizeOfImage through the build registry and takes its addresses from the
local per-build table (`../02-exe-internals/exe-build-table.md` 1, 11); an unknown build switches it off with a line.

- Proxy UI message queue guard (K3): **on** [L, `new-exe-build-26577625.md` C.4.4, C.5; 64-bit path only].
- Proxy UI draw-list growth (K4): **on** [L, same].
- Farm builder guard, proxy owner, with rule 2 (K1, K2): **on** [L, C.4.4 "13 byte patterns match (Steam build 26577625)"];
  the owner's build check accepts every known build since c9b4046fd [C, C.4.1].
- Pacing options 2-4 (configured on and mismatched: refuse with D118): **on** [L, C.4.4, C.7.1].
- View mask, white mask, model fog: **on** [L, C.4.4].
- Leader scenes for majors 22-42 (43-civ): **on when configured**; the shipped configuration writes no key, so off
  [L, C.7 table]. Without it majors 22-42 get no 3D leader scene, which does not stall the AI turn [L, `player-limits.md` 0 item 8].
- Combat hold and route S sound (epic 0042): layout verified on 26577625 (ticket 0618) [L, C.6]; route S is off by default.
- Live-terrain reload gate (ice, waves): **on**; `Waves_Reload` on 26577625 not yet verified live (MC-STATE 2026-11-20).
- The `-ia` test DLL's queue guard: **off** on 26577625 (0635) [L]; not on a player path (K3).
- Vox Populi's own EXE table (upstream #13379, its yield-icon unregistration at exit, force-resync and city-simulation
  fixes): **not in our pins at all** (the pins predate #13379, `upstream-13406-product.md` 0 item 2) and not in VP 5.4.8,
  so no change between builds; upstream's update for 26577625 is draft PR 13614 (D218).

## Rows the MC must rule on

Known fix or mitigation, not (or not provably) in the candidate; most serious first.

1. **K1, the farm-field double free on the 64-bit path.** The known fix (ServerFarmFields = 1) is built, measured
   (0 crashes, 28 launches) and off by default; the candidate relies on the after-the-fact guard, and D189 already
   shipped this crash once as a known issue. Smallest action: add `[Renderer] ServerFarmFields = 1` to the first beta's
   option set (the installer writes it, as for the other i3 keys), with one installer-laid smoke on each EXE build
   (the option is unmeasured on 26577625 as far as written). Cost to weigh: farm buildings stay at the plot's centre
   (`docs/OPTIONS.md`), a visible difference from stock (pixel-faithful goal).
   *Measured 2026-11-21 by ticket 0637 (`../02-exe-internals/farm-field-double-free.md` 4): the fix works on both EXE
   builds on the first beta's layout and option set (load, 5 turns, the game's save reloaded: 0 crashes, the builder
   never called, graph clean), but **the cost is larger than written: with the option on the farm buildings are not
   drawn at all** (4.4; OPTIONS.md's "stay at the centre" is wrong). So the choice is the owner's: the fix with farms without
   buildings until the server draws them (4.7: 8-14 h [I]), or the guard alone, whose effect on the rate is unmeasured
   (4.8). Frame rate: no difference in three quiet on/off pairs (on 197.5, off 196.2 fps, +1.3, standard error 7.0;
   the busy pair's 21.8 against 38.4 fps was the machine's load); memory EXE -14 MB, server +113 MB with the option (4.9).*
2. **K1, K2, K3, K4 on the patch-off path.** The proxy is loaded in off mode but installs none of its EXE-side guards
   (fact 2), so a player who switches the patch off gets stock exposure to the farm-field crashes, the 4 MB queue
   overrun and the draw-list overrun. Smallest action: decide whether "off" means "stock" (D28's fallback promise) or
   "stock plus the proxy's guards"; if the latter, call the queue guard and the farm guard (and the draw-list growth)
   from the off-mode boot. To confirm today's state first: one `vpproxy.log` of a `mode=off` start (no `queue guard:`
   or `farm_guard:` line expected).
3. **K3 / ticket 0635's scope.** 0635 would teach the `-ia` test DLL the new build; no player runs that DLL (fact 1).
   Smallest action: re-scope or drop 0635 for the first beta (it stays useful for the test instances' stock arms), and
   cover the player's patch-off exposure through item 2 instead.
4. **K5, K6, K7, VP 5.4.8 Lua fixes (#13463, #13467, #13539).** Upstream has the fixes; the patch does not ship VP files
   (0184 ruling, D171). D223 says a known-issues line is not a substitute. Smallest action: a ruling (with the owner, as it
   changes the shipping list approved under D171) on either shipping the three upstream Lua changes as override files,
   or recording these as stock VP defects outside the patch's reach, which D223 may not cover (the owner's [reworded for privacy] clause that invites correction).
5. **K19, K20 on the patch-off path.** The DLL fixes are in the x64 DLL only; with the patch off the player runs VP's
   own DLL. Smallest action: a ruling that the off switch is stock VP by design (no action), or shipping the i0 pin's
   32-bit DLL as the fallback (a D28 change; the installer then keeps VP's DLL only for removal).
6. **K13 on the patch-off path** (low: only a player who loads the other variant's save with the patch off). Smallest
   action: none, or a known-issues line naming it as upstream behaviour.
7. **K16 (0624)**: done. The modpack-form live cases (i)-(iii) passed on 2026-11-21 (ticket 0645); it read "merged but in review" before.

## Not covered

- Test-machine crash ledgers pruned before 2026-11-16 19:42 (one machine) and 2026-11-15 about 10:00 (the other)
  (`crash-ledger-audit.md` 8.2-8.4); the workstation ledger was audited to 2026-11-18 07:10 only; sightings after that
  are known only from tickets and MC-STATE.
- Whether rule 2 works on build 26577625 (measured on 4390913; the guard's patterns match on 26577625 [L]).
  ServerFarmFields = 1 on 26577625 is measured since 2026-11-21 (ticket 0637, `../02-exe-internals/farm-field-double-free.md`
  4.3) [L]; it read "Whether ServerFarmFields = 1 and rule 2 work on build 26577625".
- (Settled 2026-11-22 by K4's row: the UI draw-buffer growth, always on since D235 and no longer the option
  UiDrawListGrowth, runs only with the patch on, since the proxy's Lua bridge drives it; it read "Whether
  UiDrawListGrowth is gated by off mode the way the two guards are".)
- Multiplayer and hot seat: never tried (`docs/INSTALL.md`); HiddenEmbarks untested there (a desync risk, not a known
  crash).
- The 43-civ variant on build 26577625 through the installer: its live case is in 0601's chain, not yet on record.
- Firaxis' Lua sandbox on 26577625 removes `loadstring`, `io` and some `os` functions; EUI tooltips that use them break
  (`new-exe-build-26577625.md` B.1). Whether any of that raises a Lua error that stops play was not found written.
- Epic 0615 (live land/water terrain) and epic 0042 stage 5 (route S) are not merged; their crash history is in their
  own tickets (4720/4725 spike crashes: an ODR clash fixed by 0609; 4727 fixed) and was not audited here.
- Long paths (over 260 characters) in the installer: never tried (`installer-notes.md` 13.7).
