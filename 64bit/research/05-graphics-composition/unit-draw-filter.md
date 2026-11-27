# The compositor's unit-draw filter (epic 0040, state ii-d)

Epic 0040 (`tracker/issues/0040-i3a-unit-true-absence.md`, breakdown
`research/07-architecture-decisions/epic-0040-breakdown.md`): with the option `[Renderer] ServerUnits = 1` the EXE's
Unit system runs as shipped, the compositor drops the EXE's pass-0 unit draws while the server's mode word says the
server draws the units, and the server draws them at the EXE's positions. This file holds the filter's inputs and
premises (sections 0-2, issue 4751), how it is built (section 3, issue 4752), and its cost and acceptance (sections
4-6, issues 4756-4758).

Marks: `[C]` read in code, in a capture export or in this repository's records; `[L]` measured live; `[I]` inferred.

## 0. In short (issue 4751, 2026-11-17)

- **S1 holds on the stock catalogue [C].** In all 28 exports of 3907 (17,128 draws: 16 land and 4 sea unit frames, 4
  city frames, 4 frames of the p4 scene), every draw that binds one of the unit pass-0 pixel shaders `60d270e1`
  (material 90), `5def4f99` (89) or `0e7a3566` (87) binds that material's pass-0 vertex shader (`29a0b061` for 89/90,
  `82e59838` for 87) and draws into the scene target or the water's reflection target: 1,258 draws (1,250 / 2 / 6), 0
  with another vertex shader. No city, landmark, effect or interface draw binds a unit pass-0 PS in these frames.
- **S1b holds [C]:** 0 draws of materials 83-86 and 88 in the 28 exports.
- **S6 [C]:** every one of the 17,128 draws in the 28 exports is a `DrawIndexedInstanced`, the 1,258 unit pass-0 draws
  (material 87's six included) and the 628 shadow-mask draws among them. No `DrawIndexed`, `Draw`, `DrawInstanced` or
  indirect draw appears.
- **S7 partly [C], its creation-time part FALSE for the product [C] (corrected below, 2026-11-17):** each unit program a frame binds is one object, created by
  `ID3D11Device::CreateVertexShader` / `CreatePixelShader` with bytecode whose SHA-256 is the server's table's (full
  digests equal for all five pass-0 programs), and the objects carry the same RenderDoc resource ids in captures of six
  game sessions (VS 2312 `29a0b061`, PS 2324 `60d270e1`, PS 2322 `5def4f99`, VS 2277 `82e59838`, PS 2318 `0e7a3566`).
  Not shown: how many objects of a program exist that a frame does not bind, and what happens at a device re-creation.
  **Corrected 2026-11-17 09:22 (lead-0040, from 4752's #4080): "created after the composite's hooks" is FALSE for the
  product [C].** The EXE creates all 1,417 shaders at app start in its renderer init, before any helper DLL loads, and
  0 shaders are created during a save load (`research/sources/shell-gates/GATE2.md`, the Shaders row; the running game
  uses the shader-model-5 set); the composite is loaded at the front end, 16-30 s before the first Present
  (`shell-foundations-verification.md` V9). So the composite's CreateVertexShader / CreatePixelShader hooks never see
  the unit programs in the game. 4752 adds a second, read-only recognition path from the EXE's handle globals to the
  shader objects, validated by SHA-256 against this table (section 3). Replaced: "whether they are created after the
  composite's hooks are in place ... 4752's `created` / `marked` counters per program answer it first".
- **The shared inputs are committed:** the filter table `shell/common/unit_programs.h` (equal to the server's program
  table by `render/tests/units/test_unit_programs_table.cpp`), the mode word block `VPShellMode` in
  `shell/common/shell_shared.h` at offset 512, and the bridge kinds 6 / 7 in `proxy/src/proxy_ui_bridge.h`. Nothing
  reads them yet.
- **O5's bound is 2 s [C]:** the compositor holds the last server frame without a time limit (`hide_when_stale=0`, the
  default of `shell_composite.ini` and of the code), and calls the server stale after `stale_ms` (1000 ms) without a
  heartbeat; so the shorter of "the hold" and 2 s is 2 s (section 2.4).

## 1. Premises S1, S1b, S6 from the stock catalogue (issue 4751)

Inputs: 3907's exports `F:/c5-64bit-work/0301/3907/exports/` (`rdc-export --select all`, tool
`tools/harness/rdcexport`, issue 0302), read in place: `units_land/L01-L16`, `units_sea/S01-S04`, `cities/c_ancient`,
`c_industrial`, `c_modern`, `c_renaissance`, `p4_shown_all`, `p4_hidden_all`, `p4_shown2a_all`, `p4_shown2b_all`
(the 26 of `shell/evidence/0301/3909/frames.csv` and the two p4 re-exports). Each export's `draws.json` names per draw
the call, the render target, and the first eight hex digits of the bound VS and PS digests.

Command (2026-11-17 07:01 EST, worktree of lead-0040):

    python -I shell/evidence/0040/1/premises_4751.py F:/c5-64bit-work/0301/3907/exports shell/evidence/0040/1

Output (`shell/evidence/0040/1/per_export.csv`, `unit_ps_draws.csv`, `calls.csv`; numbers only, D83):

| What | Count |
|---|---|
| exports / draws | 28 / 17,128 |
| draws binding PS `60d270e1` (90) / `5def4f99` (89) / `0e7a3566` (87) | 1,250 / 2 / 6 |
| ... of them with a VS other than `29a0b061` / `82e59838` | 0 |
| ... into target 2419 (scene) / 13415 (water reflection, 1024 x 1024) | 630 / 628 (one per mesh per pass; S03 has 19 scene and 17 reflection draws of material 90, as 3909 found) |
| ... through an entry other than `DrawIndexedInstanced` | 0 |
| shadow-mask draws (PS `72c27aea`, `246527ed`, `9a5991d7`; kept) | 628 |
| draws of materials 83, 84, 85, 86, 88 | 0, 0, 0, 0, 0 |
| draws of the selection-effect pair (VS `da2382af` / PS `8d853988`; kept, O4) | 108, in 19 exports, 1-12 per frame, all `DrawIndexedInstanced` into the scene target |
| draw entries seen | `DrawIndexedInstanced` 17,128 |

The unit pass-0 draws per export agree with 3909's `frames.csv` (which counted material 90 only) except four exports:
L06 and L09 have 2 more (material 87), S04 has 4 more (two of 87, two of 89), and L04 has 66 material-90 draws where
3909 has 64 (and 33 mask draws where it has 32). The L04 difference is 3909's count, not a draw that binds a unit
program with a foreign shader: every one of L04's 66 binds VS `29a0b061` [C, `unit_ps_draws.csv`]; its cause was not
looked into.

**What these frames do not cover [I]:** leader scenes (a separate 3D scene in diplomacy), wonders' animated parts,
art of mods outside the catalogue, the strategic view (V1), and frames with more than the catalogue's units. The live
counters of 4752 (drops per program per frame) and the pictures of 4758 cover them.

## 2. The shared inputs (issue 4751, as built 2026-11-17)

### 2.1 The filter table: `shell/common/unit_programs.h`

Plain C, included by the 32-bit compositor without any render header.

- `kVPUnitDrop[5]`: the pass-0 programs the server draws, by full SHA-256 of the DXBC container, with stage, the
  materials that use them (bits `VPUNIT_MAT87/89/90`) and build 4390913's VA (informational): VS `29a0b061`
  0x6803140 (89, 90), VS `82e59838` 0x68005D0 (87), PS `60d270e1` 0x6813630 (90), PS `5def4f99` 0x6811108 (89), PS
  `0e7a3566` 0x680CC38 (87).
- `kVPUnitDropPairs[3]`: (material, VS, PS) = (90, `29a0b061`, `60d270e1`), (89, `29a0b061`, `5def4f99`), (87,
  `82e59838`, `0e7a3566`). The drop rule of the breakdown (1.2): the bound PS is a DROP PS, and as the second check the
  bound VS is its pair's VS. Material 88 shares VS `82e59838` but has its own PS, which is not in the table, so it is
  never dropped.
- `kVPUnitKeep[7]`: recognised and always passed, for the filter's counters: the shadow-mask programs (VS `8cb52b96`
  89/90, PS `72c27aea` 90, PS `246527ed` 89, VS `59af33ff` 87, PS `9a5991d7` 87; pass 1, 0375 decision 7 (c)) and the
  selection effect (VS `da2382af`, PS `8d853988`; pass 0xFF = not a unit material; O4 (a)). The selection digests come
  from the 3907 exports (`units_land/L04/shaders/`), the others from `render/src/units/programs.cpp`.
- **The rule that keeps it right:** `render/tests/units/test_unit_programs_table.cpp` (3 cases, 2026-11-17 07:06 EST:
  `3 run, 0 failed checks`) checks that DROP equals `unitProgramTable()`'s Scene rows (digests, stages, material
  bits, VAs, pairs), KEEP's mask programs equal its ShadowMask rows, no KEEP program is in DROP and the selection
  programs are not in the server's table; and that a mutated copy (one digest byte of each program, a pair index, a
  material bit, a VA, a mask program moved into DROP, a selection program moved to pass 0) is caught. The render
  tests' CMake globs `tests/units/test_*.cpp`, so the file needed no CMake change.

### 2.2 The mode word: `VPShellMode` at offset 512 of the main section

In unused, zero-filled bytes between `VPShellHeader` (376 B) and the camera block (1024); `VPSHELL_VERSION` stays 3 (a
zero block reads as "no mode", which is how every older producer and consumer behaves). 64 bytes, static_asserts on
every offset:

| Offset | Field | Writer |
|---|---|---|
| 0 | `word`: bit 0 `VPMODE_UNITS`; bits 1-3 reserved (`VPMODE_CITIES` 0037, `VPMODE_LANDMARKS` 0037, `VPMODE_PATHS` 0300); 4-31 zero | server (4753) |
| 4 | `writerPid`: the server's pid, written before `word` | server |
| 8 | `changes`: incremented after each change of `word` | server |
| 12 | `readyLossTotal`: unit readiness lost while bit 0 was set | server |
| 16 | `changedQpc` (int64): QPC of the last change | server |
| 24 | `latchedWord`: what the compositor latched at its last Present, after the liveness check | compositor (4752) |
| 28 | `latchedPresent`: low 32 bits of the camera block's `presentCount` at that latch | compositor |
| 32 | `droppedTotal`: pass-0 unit draws dropped since the DLL started | compositor |
| 36 | `passedSetTotal`: pass-0 unit draws passed while the latched bit 0 was set (acceptance: 0) | compositor |
| 40 | `reserved[6]` | - |

The compositor uses `word` only when the header's magic is set, `writerPid` equals the header's `serverPid` and the
heartbeat is fresh (`stale_ms`); otherwise its latched word is 0. A restarted server has a new pid, so a word a dead
server left set is never used (D118 ruling: pass-through only between the failure and the popup).

### 2.3 The bridge records: `proxy/src/proxy_ui_bridge.h`

`kKindUnitPosition = 6` and `kKindFrameMark = 7` (`kKindCount` is now 8), with the field comments of
`unit-feed-contract.md` 3.3, and the flag constants of kind 6: `kPosPlayerShift = 16`, `kPosPlayerMask = 0xFF << 16`,
`kPosArrival = 1 << 24`. Nothing makes these records yet (4754). Until 4754 names them, the bridge's Lua statistics
table (`KindName`, test switch only) names kinds 6 and 7 `?`.

### 2.4 The compositor's hold (input of O5)

Read 2026-11-17 in `shell/composite/shell_composite.cpp` (`PollServer`, `SetStale`, the draw at the insertion point)
and `shell/composite/shell_composite.ini` [C]:

- The compositor keeps drawing its local copy of the last server frame while no new frame comes (`g_haveContent`); it
  stops only when the server is stale **and** `hide_when_stale=1`. The shipped value is `hide_when_stale=0` (the ini and
  the code's default), so the hold has **no time limit**.
- The server is called stale when its heartbeat (`heartbeatQpc`, written every loop iteration) is older than
  `stale_ms`, default 1000 ms (`stale_ms=500` in the frame-structure tests,
  `insertion-point-and-frame-structure.md`).
- So O5's "the hold or 2 s, whichever is shorter" is **2 s**: 4753's `render_units_ready_hold_ms` default 2000.

## 3. How the filter is built (issue 4752, 2026-11-17)

Code: `shell/composite/unit_filter.h` / `.cpp` (the whole filter), wired from `shell_composite.cpp`'s hook table; stand-in
host `shell/tests/unit_filter_host.cpp`, tests `shell/tests/test_unit_filter.py`; evidence `shell/evidence/0040/2/`.

### 3.1 In short

- **S7 is false for the product [C]** (correction of 4751's `[I]`, found at 4752's start): the EXE creates all 1,417 of its
  shaders at app start in the renderer init `0x4e5c20` / `0x80cd10`, before any helper DLL is loaded, and GATE2's device
  hooks saw 0 shader creations during a load (`sources/shell-gates/GATE2.md` 2; decision log, Gate 2); the composite is
  loaded at the front end (`shell-foundations-verification.md` V9). Creation hooks alone would therefore mark nothing in
  the game. The filter has two recognition paths: **path 1**, the creation hooks (the stand-in, and any shader the EXE
  creates later, e.g. after a device loss), and **path 2**, the objects read from the EXE's renderer once, at the first
  Present after the composite's EXE validation (3.2). lead-0040's answer #4083 (2026-11-17 09:25) put path 2 into 4752.
- **Hooks** (MinHook inline hooks on d3d11.dll's implementation functions, the composite's rule; never a vtable slot,
  `insertion-point-and-frame-structure.md` 6), all under the kill switch `hook_units`: `ID3D11Device::CreateVertexShader`,
  `CreatePixelShader`; `ID3D11DeviceContext::VSSetShader`, `PSSetShader`; and every draw entry: `DrawIndexedInstanced`,
  `DrawIndexed`, `Draw`, `DrawInstanced`, `DrawIndexedInstancedIndirect`, `DrawInstancedIndirect`, `DrawAuto` (S6: the
  game uses only the first; the others are hooked and counted for safety). The targets are discovered as every composite
  hook is: from the dummy device, the adopted device's immediate context and a discovery deferred context
  (`DiscoverDevice` / `DiscoverContext`); immediate and deferred contexts have different implementation functions, so
  each method has up to two (four) hooked variants (S4).
- **Per-context shader tracking**: a table of up to 64 contexts keyed by the context pointer (the EXE has the immediate
  context and nine deferred ones, 9.1) holds the program index (0-11 or none) of the bound VS and PS, written by the
  `VSSetShader` / `PSSetShader` detours (a lookup of the shader pointer in an open-addressed table of marked objects).
  A thread-local cache of the last context makes the lookup at a draw one compare. The state is cleared where the
  runtime resets it: `FinishCommandList(FALSE)`, `ExecuteCommandList(FALSE)` (the composite's own detours call
  `UnitFilter_OnStateReset`), and at `CreateDeferredContext` (a reused address). `ClearState` and
  `SwapDeviceContextState` are not hooked; they cannot cause a wrong drop (next point).
- **The per-draw path**: `g_off` test, slot lookup (thread-local compare), a per-context entry counter increment, and
  `any` (a program of the table bound) test; only then the slow path (S3: 4756 measures it). The slow path counts per
  program and decides: drop when the bound PS is a DROP PS, the bound VS is its pair's VS (the second check), and the
  frame's latch says drop; **before a drop the bound shaders are read back** (`PSGetShader` / `VSGetShader`, only on a
  draw about to be dropped) and must equal the tracked pair, else the draw passes and `mismatch` counts it. A stale
  track can therefore only pass a draw, never drop one. A dropped draw returns without calling d3d11 (on a deferred
  context: it is never recorded).
- **The latch** at every Present of the target (`UnitFilter_OnPresent`, called from the composite's `OnPresent` after
  the taps): the word is used only when the section is mapped, the header's magic is set, `serverPid` is not 0,
  `writerPid == serverPid`, the heartbeat is younger than `stale_ms` and the composite shows the server's picture
  (`mode=composite`, not disabled); otherwise the latched word is 0 and a raw word with `VPMODE_UNITS` is counted per
  reason (`ignored_nosection/notinit/pid/stale/notcomposite`). **Correction (issue 4761, 2026-11-17; found live by 4757,
  #4248 [C]):** that liveness source holds for `handover=v3` only. Under the product's `handover=ring` (the default since
  0490) the server's ring sink (`shell/frame/ring_sink.cpp`, `server/src/render/srv_v3_sink.cpp`'s ring path) writes
  magic, serverPid, state and heartbeatQpc only into the `<base>.Frame` section's `VPFrameServer` block and the main
  header keeps magic 0, so 4752's filter was `ignored_notinit` at every Present in the product and never dropped. Since
  4761, with `handover=ring` the latch reads that block (`UnitFilterEnv::ring`, `frameSrv` from the composite's own
  mapping `g_fs`): magic `VPFRAME_MAGIC` and version, `serverPid == writerPid`, state not FAILED / STOPPED (new reason
  `ignored_srvstate`; REBUILDING stays live), heartbeat within `stale_ms`; the frame section not mapped yet counts as
  `nosection`. `handover=v3` reads the main header as before. Stand-in: `unit_filter_host --ring ok|stale|pidother|failed|
  stopped|nomagic|rebuilding` (the main header's magic and serverPid 0), tests `test_ring_*` and
  `test_v3_reads_the_main_header_not_the_ring_block`. Drop = not off and (`unit_filter=on`, or `auto` and the
  latched bit). The consumer fields of `VPShellMode` are written there with single 32-bit stores: `latchedWord`,
  `latchedPresent` (the camera block's presentCount), `droppedTotal`, `passedSetTotal`. The control thread clears a
  latch older than 1 s (no Present meanwhile: the Present hook switched off, the target gone).
- **Failures**: an exception in any detour body (draw, set, create, Present, state reset) turns the filter off for the
  process (every draw passes), one LOUD line; the composite goes on. Path 2's refusal turns it off too (3.2), logged at
  once as a plain line, and **LOUD at the first Present whose latch asks for drops** (the server's `VPMODE_UNITS`, or
  `unit_filter=on`) while the filter is off or recognises no DROP program: `unit_filter: LOUD: the latch asks for drops
  (...) but the unit filter cannot drop ...: <why>`, once per process, and `frames_unable` counts every such Present.
  So a process that never draws the server's units (ServerUnits = 0, every other suite's stand-in, whose fake EXE has
  none of path 2's patterns) has no LOUD line from the filter; one that does is LOUD exactly when units would be drawn
  twice. (Minor decision of 2026-11-17 10:40: the first build logged the refusal itself LOUD, which broke the no-LOUD
  assertions of other suites' stand-in runs, e.g. `test_fow_models.py`, for a state that harms nothing there.)
- **Keys**: `unit_filter=auto|on|off` (default auto; on and off TEST ONLY, logged at start; a bad value is a CONFIG
  ERROR and auto), `hook_units=1`; both in `shell_composite.ini` and the built-in defaults (ticket 0490's rule), rows in
  `test_config_defaults.py`. Test-only keys read by unit_filter.cpp: `unit_filter_fault=draw|set|create|present`,
  `unit_filter_table=<file>` (the twelve digests replaced; logged LOUD).
- **Counters** (`unitfilter 1s:` per interval and `unitfilter totals:` lines at the composite's stats tick): mode,
  latched word, off and why, path 2's state, Presents, frames with the bit set / with drops, the ignored counts, dropped,
  passed_set, mismatch, untracked, contexts, creations hashed, draws per entry (`dii di d dinst diiind dinstind dauto`),
  and per program `<digest8>=c<created>/m<marked>/r<resolved>/f<refused>/s<seen>/d<dropped>/p<passed>`.

### 3.2 Path 2: where the EXE keeps its shader objects [C]

Read 2026-11-17 in the llvm-objdump listing of build 4390913 (`D:/c5-64bit-work/2900/2903/dx11.asm`, SHA-256 of the EXE
b2ce96ae...) and by hashing every blob of `render/src/shaders/create_sites_table.inc` in the EXE file against the
twelve digests of `shell/common/unit_programs.h` (helper scripts kept outside the repository,
`C:/c5-64bit-work/0040/4752/find_vas.py`, `sites.py`; numbers only, D83). Facts:

- The renderer's create functions (`thiscall`, ecx = the renderer `[0x3010570]`):
  `0x8bbbf0` PS (blob, size): `mov eax, [0x301057c]` (the ID3D11Device*), calls the device's vtable +0x3C, stores the
  object at `0x300df68 + 4 * [renderer+0x45c]`, increments `renderer+0x45c` and returns the old count as the handle;
  `0x8bbc40` VS (blob, size, mask): the same with vtable +0x30, objects at `0x300ef68 + 4 * [renderer+0x458]`, and it
  keeps the bytecode pointer at `0x3006f68 + 4 h` and the size at `0x3005f68 + 4 h`. Each object array is 0x1000 bytes
  (1,024 handles). `0x8bbcc0` HS keeps its objects at `0x300bf68`.
- The shader level `[0x2e49edc]` (read by the init at `0x4e6099`): `<= 8` set 4_0, `9` set 4_1, `10` set 5_0; the table's
  digests are of the 5_0 blobs, so path 2 requires level 10.
- Each of the twelve programs has exactly one create site, all in the 5_0 part of `0x4e5c20`, each `push <blob VA>` ...
  `call <create>` then `mov word ptr [<handle global>], ax` (the handle): twelve programs,
  five VS and seven PS: the unit materials 87, 89 and 90 (plain and with the mask) and the selection effect. The
  handles the filter reads are the twelve 16-bit handle globals these sites store: ten in 0x2de1d50..0x2de1d90 and
  the selection effect's 0x2de1a94 (VS) and 0x2de1aac (PS). The blob VAs and the push, call and store sites of each
  are in the table path 2 checks (`shell/composite/unit_filter.cpp`) and in the generated create-site table
  (`render/src/shaders/create_sites_table.inc`). (A 12-row table from the create-site tool was replaced by this
  description, 2026-11-18, D83 audit.)

  The selection effect's two VAs (unknown to 4751's table, `exeVa` 0) are new; the other ten equal `unit_programs.h`.
- **What path 2 checks, in order** (any failure: one line `unit_filter: path 2 REFUSED (...)` naming the program and the
  reason, nothing marked, the filter off for the process, LOUD when a latch asks for drops (3.1); read only, SEH-guarded): it runs only once
  the composite's camera validation passed (TimeDateStamp and the patterns of `common/exe_camera.h`); the ten
  instruction patterns of the create functions and the init (the device, count and array operands above, with their
  relocated addresses rebased); per program the push of its blob VA, the call to the right create function, the store
  to its handle global; the shader level 10 (else the line names the 4_0 / 4_1 set it created); the device global
  equals the target's device; per program: `DXBC` at the VA, its size (header +24) within 32..65,536, its SHA-256 equal
  to the table's digest, the handle below the renderer's count of that stage and 1,024, a non-null object, for a VS the
  recorded bytecode pointer and size equal to the blob, and the object a d3d11 object (its vtable's first entry in
  d3d11.dll) that answers `QueryInterface` for that stage with itself and whose `GetDevice` is the target's device.
  Only when all twelve pass are they marked (no partial mark); a conflict with a path-1 mark is refused too.
- `[I]`: that the code bytes compared here read in the running game as on disk (Steam's CEG); the camera's patterns do,
  these are in other functions. 4757's first live run shows it (path 2 resolved or a named refusal).

### 3.3 When the filter cannot drop: what happens, and the D118 route (lead-0040 #4173, 2026-11-17)

- **What the composite does** [C, code; L stand-in]: at the first Present whose latch asks for drops (the server's
  `VPMODE_UNITS` latched, or `unit_filter=on`) while the filter is off (path 2 refused, an exception, a refused test table)
  or recognises no DROP program, it logs once `unit_filter: LOUD: the latch asks for drops (...) but the unit filter cannot
  drop the EXE's pass-0 unit draws: <why>; the EXE's units are drawn as well`, counts `frames_unable` at every such
  Present, and every draw passes. In the mode block the server then reads `latchedWord` with `VPMODE_UNITS` and
  `droppedTotal` (and `passedSetTotal`: the draws are not recognised) not growing (`test_path2_refusals` checks the LOUD
  line, `frames_unable` and that block for all eleven refusals).
- **Popup and report: the server's D125 step, through `filterState` (as built, issue 4761, 2026-11-17; MC #4221 (a):
  "D118 forbids the drawn-twice fallback") [C, code and hermetic tests; not run live].** The composite has no route of its
  own to D118's popup: the popup and report come from the server's D125 step (`HostConfig::fatal`, exit 5, the proxy's
  server-death report, `vp64helper failed`; `unit-server-scene.md` 10), and every composite tap that fails in play
  reaches it by publishing a state that the server reads. `droppedTotal` not growing is not a usable signal by itself
  (a frame with no unit in view drops nothing either). *(Replaces 4752's "not built" proposal of the same route.)*
  - **The word.** `VPShellMode::filterState` at offset 40 (was `reserved[0]`; `reserved[5]` follows at 44; `sizeof` 64;
    static_asserts in `shell_shared.h`). Bits 0-7 the state: `VPFILTER_NOT_REPORTED` 0 (no compositor, an older one,
    before its first Present, or the filter has not decided yet), `VPFILTER_CAN_DROP` 1, `VPFILTER_CANNOT_DROP` 2;
    bits 8-15 the reason when 2 (`VPFILTER_R_*`): 1 path 2 refused (or conflicted with a path-1 mark), 2 path 2 not run
    (the composite refused the EXE image) and no creation matched, 3 an exception in a detour, 4 no DROP program
    recognised (path 2 pending), 5 `unit_filter=off` (TEST ONLY), 6 the test table refused, 7 the mark table full,
    8 no draw or shader bind reached the filter's hooks (`hook_units=0`, or the hooks not in place); bits 16-31 zero.
    One word, so state and reason are always consistent (one store).
  - **The writer** (`unit_filter.cpp` `FilterState`, at every Present of the target, one 32-bit store **before**
    `latchedWord`, so a reader that sees the latched bit sees a state at least as new). Order: `unit_filter=off`; the
    filter off for the process (its first reason, stored before `g_off`); no hooked call since the DLL started after the
    filter's first two Presents (reason 8); no DROP program marked or resolved (reason 2 or 4); not reported while a
    DROP program is known but no hooked call has come yet (the first Presents, the hooks going in); else can drop. A
    change is logged once (`unit_filter: filterState 0x.. (cannot drop: <reason>[; the server's VPMODE_UNITS is
    latched: the server takes the D125 step (D118)]) at present N`); the 1 s and totals lines carry `filter_state=`
    and `filter_reason=`. The LOUD "cannot drop" line and `frames_unable` now follow the same state (so reason 8 is LOUD
    too; before 4761 `hook_units=0` with path 2 resolved drew the units twice without a word).
  - **The reader** (`srv_units_mode.cpp` `ModeWriter::checkFilter`, in every `onPoll` and at the top of every `onFrame`):
    while the writer holds the bit (phase live) and `latchedWord` has `VPMODE_UNITS`, state 2 is fatal **at once** (the
    ticket's open choice: a filter that cannot drop does not recover, so the hold bound is not waited for; it is well
    within `render_units_ready_hold_ms`): the bit is cleared, `m_failed`, `fatal` and `filterFatal` + 1, phase failed,
    LOUD `units: the compositor's unit filter cannot drop the EXE's pass-0 unit draws (<reason>; filterState 0x..) while
    it has latched the unit bit: units drawn twice (D118)`, and the host's `UnitsFatalStep` takes it once
    (`HostConfig::fatal(5, why)`). States 0 and 1, any other value, and any state while the bit is 0 (starting, loading,
    lost, source own, `render_units=0`) or not latched never act; a server without a compositor (headless) or with an
    older one reads 0. The mode line ends `filter=0x.. filter_fatal=..`.
  - **Tests.** Composite: `test_unit_filter.py` checks the word (host frame lines `fstate=`, the totals, the change line)
    on the good paths (path 1, path 2, `unit_filter=on`, every server state) and for each way the stand-in can stage:
    the eleven path-2 refusals (0x102), path 2 not run (0x202), an exception in each detour (0x302), `unit_filter=off`
    (0x502), a refused test table (0x602), `hook_units=0` with path 1 and with path 2 (0x802,
    `test_kill_switch_with_path2_reports_no_hooks`). Not staged on the stand-in: reasons 4 (path 2 pending needs a
    target without a device) and 7 (512 marks). Server: `test_units_mode` cases `filter_no_fatal`, `filter_fatal` (at
    a poll and at a frame, hold 600000 ms: at once), `filter_reasons` (every reason and an unknown one named),
    `filter_not_held`, `filter_headless`, `scene_filter_fatal` (through `Scene` and `UnitsFatalStep`: one fatal call, code 5).
    Results: in `shell/evidence/0040/4761/results.txt`. 4758's D118 item gets one negative arm (named there, not run
    here): `unit_filter=off` on a live run with ServerUnits = 1 must end the server with exit 5 and the report.

### 3.4 ServerUnits = 0 end to end: the existing checks (MC #4116 (b); run 2026-11-17 11:15-11:25 on branch task-4752-unit-draw-filter)

The product default is `[Renderer] ServerUnits = 0` with the shipped `unit_filter=auto`. Each link has a check, all passing
on this branch (`shell/evidence/0040/2/results.txt`, `checks_b.sh`):

1. The proxy writes no unit keys into server.cfg (4759): `python -I proxy/tools/test_proxy_rt.py --only-units --asan
   --units-baseline 372823cad`: `test_proxy_rt: 90/90 checks passed`, among them
   `ok   units accepted R1 W1: ServerUnits = 0: no request, no ServerUnits line, render lines written` and
   `ok   units baseline accepted R1 W1: ServerUnits = 0 gives the server.cfg lines of 372823cad byte for byte`.
2. The server never sets the bit without unit drawing at the EXE's positions (4753): `test_units_mode: 16 of 16 cases
   passed` (`bit_rule`, `scene_defaults`: the product's defaults keep the bit 0, `scene_units_off`: render_units=0 keeps
   the word 0).
3. The compositor drops nothing without the bit (4752): `test_shipped_default_never_drops_without_the_bit` for a zero mode
   block, no section, a live server with word 0, another writer, a stale heartbeat: every draw passes and every counter
   stays 0, no LOUD line.

### 3.5 The stand-in tests and the existing suites (2026-11-17) [L stand-in]

`shell/tests/unit_filter_host.cpp` is a D3D11 program that plays the server's part of the main section (header, heartbeat,
`VPShellMode`), loads `shell_composite.dll`, waits until the composite's hooks are in place (the first bytes of the
implementations changed), and draws per frame twelve cells on the immediate context and twelve on a deferred context
(recorded, finished, executed), plus one `DrawAuto`; each cell is one draw entry with one (VS, PS) pair of its own test
shaders, read back after Present (P drawn, D dropped). Cells 0-5 are the three DROP pairs through the six indexed and
non-indexed entries; 6 unmarked, 7 a mask pair, 8 and 9 a DROP PS with a foreign VS (the second check), 10 the selection
pair, 11 material 88's case (VS 82e59838 with a PS outside the table). Its shaders are its own (D3DCompile at run time),
so the composite gets their digests through the test table (`unit_filter_table`, built by the test from `--dump`).
`--fake-exe` lays out a fake EXE image (`VPSHELL_EXE_BASE`) with the camera validation's patterns and every piece of 3.2,
with the program shaders created before the composite is loaded; `--fake-bad` breaks one piece.

`test_unit_filter.py` (35 cases with MC #4116 (a)'s five) covers the acceptance: the word written in mid-frame 4 and
cleared in mid-frame 8 drops frames 5-8 exactly, on both contexts and through all six entries, and nothing else (a, b, c);
`latchedWord` / `latchedPresent` / `droppedTotal` / `passedSetTotal` in the mode block (13 dropped and 4 passed with the
bit set per frame); the shipped default (auto) with a zero mode block, no section, a live server with word 0, another
writer or a stale heartbeat: nothing dropped, every counter 0 (MC #4116 (a)); the bit with pid 0, another writer, a stale
heartbeat, no magic, `mode=passthrough`: nothing dropped, `ignored_*` counted (d); stale until frame 3 then alive: drops
from frame 4; an exception in each of the draw, set, create and Present detours: one LOUD line, the filter off, every draw
passes, the composite not DISABLED (e); the 1 s lines add up to the totals, per program created / marked / resolved /
seen / dropped / passed (f); `unit_filter=off` (passes, counts passed_set), `on` (drops without the word), a bad value
(CONFIG ERROR, auto), `hook_units=0` (no filter hook) (g); a refused test table and no test table (nothing marked);
path 2 resolving 12 of 12 on the fake image and its eleven refusals (level 9 naming the 4_1 set, a changed blob byte, the
push, the call, the store, an instruction pattern, a handle at the count, a null object, a VS object in the PS array, a
VS created from another pointer, another device), each with nothing marked and every draw passing.

**S7's counters on the stand-in** (path 1, `test_word_schedule`): each of the twelve programs `created=1 marked=1`
(the host creates each once, after the hooks); path 2 (`test_path2_resolves`): each `resolved=1 created=0` (created
before the composite was loaded, as in the game). **The live check of S7 comes with 4757**: in the game path 1 should
count 0 creations (GATE2) and path 2 should resolve 12 of 12 at the first Present after the EXE validation; that
line and the counters of the first map frames answer how many objects each program has (the EXE's handle arrays hold
one per program per set).

**Results** (`shell/evidence/0040/2/results.txt`): `test_unit_filter.py` 35/35 plain; with `test_config_defaults.py`
44/44 under the composite's ASan build, 0 ASan reports. The full shell stand-in suite on the branch (61 min, other teams'
heavy jobs running): 113 failed, 631 passed, 14 errors, of which every failure in seven files ends in a build input the run
was not given (`test_fow_models`, `test_ice_tap`, `test_overlay_tap`, `test_shadow_tap`, `test_water_tap`, and the setup
errors of `test_lockstep` and `test_join_ring`; 0490's run excluded the same kind), and four others:
`test_tagged::test_kill_switches_and_mode_off` (4752's new switch, fixed in the test's `SWITCHES`, MC #4176, rerun
passed), `test_ice_tap::test_shipped_ini_holds_the_kill_switch_off` (a base failure from 0490, fixed on main 26305a54b),
`test_lockstep::test_publish_at_updateviews_exact` (timing; passed on the rerun) and
`test_residency::test_gray_frames_counted_when_budget_too_small` (failed again; it runs shell_server.exe alone, no
composite and no file of 4752).

## 4. Cost and identity (issue 4756): the plan, ready for the numbers

Breakdown 6 items 6 and 7, premises S3 (the detour is cheap) and F10 (the bridge's listener changes no game state).
Written 2026-11-17 by w-4756 (account B, branch `task-4756-cost-parity`); the drivers are in `shell/evidence/0040/6/`
and checked offline (`selftest_4756.sh`: FAIL=0, `selftest-out.txt`); **no number is measured yet**. Part (1) waits for
4752's merge (the series runs once, on the epic's build with the filter); part (2) waits for D177 (account B's auto-mode
entry for `frontend.py dismiss` / `turns.py`, MC #4097).

### 4.1 Part (1): the cost series on oldpc (S3; the server and the bridge with positions)

**Where and how.** The quiet testbed `oldpc` through `tools/testbed/tb.py --host oldpc submit --headed --quiet` (COMMON 23,
D106; a `quiet@oldpc` and a `game-instance@oldpc` place, the job takes both runner slots), one job, instance `tb1` or
`tb2` (the machine's own EXE), the D33 product path of `tools/testbed/jobs/product_path.py` (its prep and restore by
SHA-256, unchanged), the huge save copy `TBPP-HUGE_0350` of `TB-HUGE_0350` (c499d3c6), AI logging off, `WaitForVSync`
0, a timing launch (no frame cap), the window on screen (COMMON 24). Driver `shell/evidence/0040/6/cost_4756.py`,
extended from `server/evidence/0375/4466/cost_4466.py` (whose pure parts it imports; 4466's `gpu_sample.ps1` is used in
place). The build: `build_4756.sh` on the team branch with 4752 merged (server.exe and proxy of this branch, the pin
layout `x64-04b1211b-pfdee656`'s x64 DLL 29F58E17... and libraries, the composite of the same commit).

**Arms** (only the options file and the composite ini change between them; no `server_cfg_extra` in any arm):

| Kind | `VP64Options.ini` `[Renderer]` | `shell_composite.ini` | What it is |
|---|---|---|---|
| a | `ServerRenderer = 1`, `ServerUnits = 0` | `unit_filter=auto` | the product today: the EXE draws its units |
| b | `ServerRenderer = 1`, `ServerUnits = 1` | `unit_filter=auto` | the option on: the server draws the units at the EXE's positions (4759's three server.cfg keys, the bridge's two listeners), the compositor drops the EXE's pass-0 unit draws |
| c | `ServerRenderer = 1`, `ServerUnits = 1` | `unit_filter=off` (TEST ONLY, 4752 (g)) | b without the drop: the server's units and the bridge, the EXE still drawing its own |

The job refuses kind c (exit 2, before anything is changed) while the composite ini it ships has no `unit_filter=`
line, so kind c cannot silently run as kind b on a build without 4752 (4752 merged at f2f194e40 with `unit_filter=auto`
in the shipped ini, so the dry run accepts kind c on that commit: `test_cost_4756.py`, 2026-11-17 11:42). Each arm checks its mode from its own logs
and is not counted otherwise:
- b and c need the proxy's `shell: [Renderer] ServerUnits = 1: server.cfg gets ...` line, `render_units=1` in
  server.cfg, `units 1s` lines, and the unit scene ready (pre-check ended, `ready=1`) before the window. Kind a needs
  none of them.
- Over the idle window, the filter's `unitfilter 1s:` lines (3.1) must show what the kind asks for (`filter_check`):
  - a: no frame with the bit set, nothing dropped;
  - b: mode auto, `off=0`, `frames_unable` 0, and drops in the window;
  - c: mode off, nothing dropped.

  A b arm whose path 2 refused (3.3) is therefore an error, not a cheap filter.
- **Kind c on builds from 3e1754fa5 on (lead-0040, 2026-11-17 12:48): no longer possible as written.** 4761's
  `filterState` reports `unit_filter=off` with ServerUnits = 1 as "cannot drop" (reason 5), and the server then takes the
  D125 fatal step (D118). A re-run on such a build needs another arm c. Two options, neither built:
  - a test switch that keeps `filterState` "can drop" while it passes every draw;
  - b with the per-draw path's cost isolated another way (for example `hook_units=0`, which removes the hooks
    altogether and so measures the hooks with the drop, not the drop alone).

  Attempt 2 (job `20261117-122621-ef38`) is on b0adb1b85, before that change, so its kind c ran as described above.

**Order and size.** Warm-up b (measured, never counted), then six rounds with the order turned each round:
`a,b,c / b,c,a / c,a,b / a,b,c / b,c,a / c,a,b` (six runs per arm, PERFORMANCE.md 2.3); about 6 min per arm, 19 arms,
`--expect-min 125` (4468's set B took 5.3 min per arm with 2 turns and no pan).

**Per arm:** a fresh game; the settle (60 s; b and c until the unit scene is ready, at most 180 s more); the camera on
0375's DoD E camera 1, plot (85, 31) (`UI.LookAt`, the camera of 4468's set B); memory after the load; the **idle window**
(45 s, the Lua frame counter in 3 blocks, `fps_idle.py --measure`); the camera put back; one **pan** (20 s: the arrow
keys RIGHT and LEFT held 1.5 s each in turn through `cli.py key`, 3706's pan; the counter read before and after, and its
longest frame by `os.clock`); **2 AI turns** (`headed_probe.play_turns`; the ticket names no turns, but the bridge's
records come from moving units, which move only in the AI turns: a minor decision of this ticket); memory at the end;
the harness's safe stop. Over each window: server.exe's CPU time (psutil), its frames drawn (render.log `terrain 1s:
drawn=`), the 3D engines' utilization of server.exe and the EXE (idle window only), the bridge's records (the growth of
the server's `ui records (issue 0178): N received ... unit-position K (positions P, arrivals A) ...` summary), and the
compositor's unit filter lines. Those are kept raw, with the sums of their `unitfilter 1s:` intervals: presents,
frames set, dropping and unable, dropped, passed_set, mismatch. They give `filt_dropped_per_frame_idle/_pan`, the draws
the filter drops per EXE frame, which is the work that b - c pays for. The parse is checked against the line format of
`unit_filter.cpp` in `test_cost_4756.py`, not yet against a live line. One `c5-run/1` ledger record per arm in the job's `out/ledger/runs` (PERFORMANCE.md 2.7: kept in
this ticket's folder, not ingested).

**What each number means** (all per arm with mean, sd, median, min, max and the 95 % interval; the pairs b - a, c - a and
b - c paired by repetition with the 95 % t interval and the smallest detectable difference at 80 % power):

| Metric (`cost-summary.txt` key) | Meaning | Expected / reference |
|---|---|---|
| `exe_ft_ms` | the EXE's mean frame time over the idle window at camera 1 | **b - c is S3, the filter's own cost** (a compare and a branch per draw on the 47-54 command lists); 4468 set B's EXE idle 3.80-3.99 ms resolved 0.06 ms (det. ~0.02 ms, ~0.5 %) |
| `exe_pan_ft_ms`, `exe_pan_max_frame_ms` | the same over the pan, and its longest frame | b - c the filter while the drawn set changes; a stall shows in the maximum |
| `srv_cpu_ms_per_frame`, `srv_fps` (and `srv_pan_*`) | server.exe CPU ms per drawn frame and frames per second | **c - a is the server's unit scene in position mode exe**: 4468 set B (u1, the controller on) 10.22 ms / 115 fps against 4466's units-off 3.32 ms / 271 fps on oldpc [L, unit-server-scene.md 3.4]; b - c should be 0 (the filter is in the EXE) |
| `srv_gpu_ms_per_frame`, `srv_gpu_pct`, `exe_gpu_pct` | GPU time per drawn frame (server), 3D utilization (both) | c - a the units' draws (4468: +0.05 ms); b - c the EXE's dropped draws (lower EXE GPU in b) |
| `rec_per_s_idle`, `rec_per_s_pan`, `rec_per_s_turns` | the bridge's records (kinds 6 and 7) per second | a: 0 (no listener); idle and pan about 0 (nothing moves); the turns: the visible AI moves |
| `turn_wall_s`, `turn_srv_cpu_s` | the AI turn's wall time (end turn to the next human turn) and server CPU per turn | c - a and b - a: the feed and the render thread during the turn (4468: +5.4 s CPU per turn, wall not resolved) |
| `server_private_mb_load/_end`, `exe_private_mb_load/_end`, `server_peak_private_mb_end`, `video_local_mb_*` | private bytes after the load and at the end (server, EXE), the server's peak, its video memory | c - a the server's unit scene (4468: +40 MB at the end); b - a and c - a for the EXE: the bridge (expected small) |
| `precheck_ms` | the unit scene's pre-check (b, c) | 4468 set B 14.1 s |

**The main thread's time in the listeners** is not measured: the bridge has no timer around its two listeners
(`proxy_ui_bridge.cpp`; its stall heartbeat counts frames, not time), and the EXE's frame time is not read during the AI
turns, when the listeners fire. Its upper bound in idle and panning is c - a of `exe_ft_ms` / `exe_pan_ft_ms` (which
also holds the GPU contention of the server's unit draws). A number for the turns needs a timer in the bridge or a
sampled EXE: named for the lead, not built here.

**Trust grade expected:** A by PERFORMANCE.md 2.6-2.7 for the per-arm pairs (one interleaved series, one machine, the
same scenario, six runs per arm, the quiet job alone), as 4466's and 4468's sets, which were A with their quiet proofs
saying `undisturbed false` (foreign load ~0.2 cores); the proof's verdict is quoted with the numbers. If b - c is not
within the noise, the ticket reports the per-draw cost and a `perf/sampler` profile before any change (ticket "Open
decisions").

**Commands** (worktree root, after 4752 has merged into the team branch and this branch has merged it):

    sh shell/evidence/0040/6/build_4756.sh all            # rebuild: shell (the filter), server, native, layout
    sh shell/evidence/0040/6/selftest_4756.sh             # offline: the dry run then accepts kind c (rc 0)
    git commit ...                                        # the job runs HEAD
    sh shell/evidence/0040/6/submit_4756.sh               # tb.py --host oldpc submit --headed --quiet --expect-min 125
    python -I -X utf8 tools/testbed/tb.py --host oldpc wait JOB --timeout 170          (re-armed)
    python -I -X utf8 tools/testbed/tb.py --host oldpc fetch JOB --out F:/c5-64bit-work/0040/4756/oldpc-JOB --include "ledger/*"

Results go to 4.3 in PERFORMANCE.md section 6's form, with `cost-summary.txt`, the run records' path and the job id.

### 4.2 Part (2): the parity pair (D164, F10)

**Does not need 4752.** The filter is in the compositor: it decides only whether a draw call reaches d3d11, and the EXE
reads nothing back from its pass-0 unit draws (S2), so it changes no game state. The pair measures F10, which is the
option's server.cfg keys and the bridge's listeners; it can run on the build at hand (`build_4756.sh` at 1d4ac518f:
server.exe `417b0240...`, proxy `a40a150c...`, composite `be8c0751...`, x64 DLL 29F58E17..., 2026-11-17 09:54) as soon
as D177 is in.

**Method** (0178's, `proxy-design.md` 24.5; 4461's `parity_4461.sh`): one workstation pool instance (a game-instance
place, a short run), the product path of 4460's driver (`live_4460.py` prep / restore over `run_3308.py`: ServerRenderer
= 1, the composite and its manifest, D33 with Overlay kept on), `server_cfg_extra` unset right after the prep (the
switch is the product option alone), the save copy `C5H-0040-4756-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C, turn
18; never the owner's saves). Two arms of `proxy/tools/live/arm.py`, windowed against windowed (both through the same
`launch.py` route), the same build and instance, 30 AI turns each with the checklist: **u1** `ServerUnits = 1`, then
**u0** `ServerUnits = 0`. Each arm's mode is checked from its logs (u1: the proxy's ServerUnits line, `render_units=1`
in server.cfg, `units 1s` lines, the bridge's `unit positions 2 of 2` install; u0: none of them).

**Keys and pass rule:** `proxy/tools/live/eq_stats.py` dumps of every player's statistics after the load
(`stats-load.txt`) and at the last turn (`stats-end.txt`) of each arm, compared u1 against u0 with no key ignored. Pass:
both comparisons `VERDICT IDENTICAL: 0 differing keys` and no key on one side only, both arms rc 0 in their mode with
their 30 turns played. Any difference stops the ticket: it goes to the MC on thread 0040 and to lead-0040 (`compare`
prints both post commands); an arm that failed or ran in the wrong mode gives no verdict and is rerun.

**Commands** (worktree root; the claim first, released by hand after):

    python -I tools/harness/pool.py claim --owner W-4756 --ticket 4756 --length short
    D177=in C5_POOL_OWNER=W-4756 sh shell/evidence/0040/6/run_pair_4756.sh <instance> r1
        # = pair_4756.py prep -> arm u1 --d177-in --turns 30 -> arm u0 --d177-in --turns 30 -> compare -> restore
    python -I tools/harness/pool.py release <instance> --owner W-4756

`pair_4756.py arm` refuses without `--d177-in` (exit 3) and the wrapper without `D177=in`; nothing that passes the load
screen or plays turns runs before lead-0040 confirms D177 (COMMON 22). Work and evidence: `C:/c5-64bit-work/0040/4756/
pair-<tag>/` (`pair-prep.json`, `arms/u1|u0/` with `arm.json`, `pair-arm.json`, the stats dumps and logs,
`eq-load.txt`, `eq-end.txt`, `pair-result.json`); the result lines go to 4.3 and the small files to
`shell/evidence/0040/6/`.

### 4.3 Results

**Part (2), the parity pair r1: IDENTICAL; F10 holds for this game [L] (2026-11-17 10:50-11:04 EST, pool
instance c5h-c, workstation, Goldberg, Vibrant Textures present; claim W-4756, released 11:06).** Run as
`D177=in C5_POOL_OWNER=W-4756 sh shell/evidence/0040/6/run_pair_4756.sh c5h-c r1`, once D177 was in (lead-0040, 10:47).
- **Build:** `build_4756.sh` at 1d4ac518f (the team branch before 4752: the compositor has no filter yet, which the pair
  does not need, 4.2): server.exe `417B0240...`, proxy `A40A150C...`, composite `BE8C0751...`, x64 DLL `29F58E17...`
  (pin 04b1211b8, layout `x64-04b1211b-pfdee656`). Release configuration.
- **Game:** save copy `C5H-0040-4756-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C), Tiny, turn 18. Each arm loaded it
  and played 30 AI turns to turn 48 through `arm.py`'s checklist, windowed through the same `launch.py` route: u1
  `result pass`, 178.0 s; u0 `result pass`, 168.7 s. No crash, no dialog box (marker clear at every step and after the
  run), no LOUD line in server.log, render.log or vpproxy.log in either arm.
- **Mode of each arm, from its logs:**
  - u1: `options: [Renderer] ServerUnits = 1 (from the file)`; `shell: [Renderer] ServerUnits = 1: server.cfg gets
    render_units=1 render_units_passes=1 render_units_positions=exe`; server.cfg holds those three lines (no
    `server_cfg_extra`); `ui bridge: gen 1 installed into the InGame context ... unit positions 2 of 2 (positions
    only)` at 49.1 s. The server received 1,609 bridge records (1,166 positions, 443 frame marks, 0 bad, 0 sequence
    gaps). render.log has 247 `units 1s` lines; the last says `ready=1 ... refused=0 out_of_step=0`.
  - u0: `ServerUnits = 0 (from the file)`, `ui bridge: off`, no unit keys in server.cfg, no `units 1s` line.
- **Keys:** `eq_stats.py` u1 against u0, no key ignored:
  - after the load (turn 18): 376 / 376 keys, 0 only on one side, **0 differing** (`VERDICT IDENTICAL`);
  - at the last turn (turn 48): 455 / 455 keys, 0 only on one side, **0 differing** (`VERDICT IDENTICAL`).
- **Restore:** by hash (`backup_restore_ok` true), `provision.py verify` rc 0.
- **Pictures:** the checklist's 24 UI-state and AI-turn shots are in Visual Checks under issue 4756, described as
  DIAGNOSTIC `info`. None of them is compared with another picture.
- **Records:** `shell/evidence/0040/6/r1/` holds `chain.log`, `pair-prep.json`, `pair-result.json`, `eq-load.txt`,
  `eq-end.txt`, and per arm `arm.json`, `pair-arm.json`, `checklist-summary.json`, `lines.txt` (the log lines above) and
  both stats dumps. Full run data: `C:/c5-64bit-work/0040/4756/pair-r1/`.
- **What it does not show:**
  - One game, one Tiny save, 30 turns.
  - The filter's own effect on the game (4752 not in this build; S2 says it reads nothing back, and 4757 counts the
    events with it).
  - A human player's moves: the checklist plays AI turns only, so the listeners fired for AI moves the human could see.

**Part (1), attempt 1: stopped at its warm-up, the filter never latched [L] (oldpc job `20261117-114846-a150`,
2026-11-17 11:56-12:01 EST, exit 4; restored, `provision.py verify` rc 0).**
- Build: b9a3b5031 (the team branch with 4752 at f2f194e40), clean (`repo_dirty=0`). server.exe `dfa5015f...`, proxy
  `8b6e3faa...`, composite `1cac68ff...`, x64 DLL 29F58E17... Inputs in `shell/evidence/0040/6/cost-inputs.sha256`.
- What happened: the warm-up arm b reached the map (unit scene ready, camera 1 set, EXE idle 195.4 fps). Its
  `filter_check` then failed: `b: nothing dropped (frames_set 0, frames_drop 0)`. The job stopped before any counted arm
  and restored the instance by hash.
- This is lead-0040's finding of the same minute, from 4757's Tiny smoke (#4248), now seen on oldpc's huge game. Under
  `render_handover=ring` the server writes magic, serverPid and heartbeat only in the `.Frame` section's `VPFrameServer`
  block. `unit_filter.cpp` checks liveness on the main section's `VPShellHeader` (magic 0), so every Present counts
  `ignored_notinit` and nothing latches. Over the game's 37,244 Presents: `ignored_notinit=37180`, `frames_set=0`,
  `dropped=0`. The fix is 4761's (`unit_filter.cpp`).
- **Path 2 resolves in the product [L]**, which answers 3.2's `[I]` on CEG for this EXE and machine. The composite log
  at the first Present: `unit_filter: path 2: 12 of 12 programs resolved from the EXE's renderer (image 0x00D70000,
  shader level 10, device ...)`. Path 1 hashed 5 creations; none matched (`c0` on every program).
- **Draws per frame [L, the same game, every Present from the load to the stop, 37,244 Presents]:**

  | What | Count | Per Present |
  |---|---|---|
  | `DrawIndexedInstanced` (every program) | 63,770,550 | ~1,712 |
  | `Draw` | 36,449 | |
  | PS `60d270e1` with VS `29a0b061` (material 90, DROP) | 3,575,976 seen and passed | ~96 |
  | mask pair `8cb52b96` / `72c27aea` | 1,796,827 | ~48 |
  | selection VS `da2382af` | 909,499 | |
  | selection PS `8d853988` | 13,493 | |
  | materials 89 and 87 | 0 | |

  These per-Present figures are what the filter would drop per frame with the latch working, and what b − c pays for.
- Records: `shell/evidence/0040/6/cost-20261117-114846-a150.json`, `runs-20261117-114846-a150/` (the warm-up's ledger
  record). Job data, with the arm's `shell_composite.log`: `F:/c5-64bit-work/0040/4756/oldpc-20261117-114846-a150/`.
- After this attempt the job's b check also fails on `frames_set` 0 and on any `ignored_*` count other than
  `ignored_stale` that grows (`test_cost_4756.py`, with this job's live line as a case).
- The series is resubmitted on the team branch once 4761 has merged.

**Part (1), attempt 2: the series, grade A [L] (oldpc job `20261117-122621-ef38`, 2026-11-17 12:26-14:21 EST, exit 0;
18 counted arms and the warm-up; restored by hash, `provision.py verify` rc 0).** S3 holds: the filter's own cost (b - c)
is not resolved in the EXE's frame time, idle or panning, with a smallest detectable difference of 0.036 ms (0.70 %)
idle and 0.052 ms (0.94 %) panning, while it drops 133 draws per frame. In PERFORMANCE.md section 6's form:

    Metric / tool:        the EXE's mean frame time over a 45 s idle window and a 20 s pan (the Lua frame counter), the
                          longest pan frame; server CPU ms and GPU ms per drawn frame, server frames per s; the bridge's
                          records per s; AI turn wall time and server CPU per turn (2 turns); private bytes after the
                          load and at the end (server, EXE); the filter's dropped draws per EXE frame;
                          shell/evidence/0040/6/cost_4756.py (4466's pure parts), section 4.1
    Builds:               ea97c19b8 = team head b0adb1b85 (4752 + 4761's liveness fix) + this ticket's driver; clean
                          (repo_dirty=0): server.exe 539723b5..., proxy faf13e49..., composite 5d086419..., x64 DLL
                          29F58E17... (pin 04b1211b8), release; the same files in every arm (cost-inputs.sha256)
    Arms:                 a = ServerUnits 0 (the product today); b = ServerUnits 1 (unit_filter=auto, the drop on);
                          c = ServerUnits 1 + unit_filter=off (TEST ONLY: no drop); each checked from its logs and its
                          unitfilter 1s lines (a: no bit, 0 dropped; b: auto, off 0, 0 unable, drops; c: off, 0 dropped)
    Game / config:        TBPP-HUGE_0350 (c499d3c6), product path (D33, ServerRenderer = 1), timing launch (no cap),
                          WaitForVSync 0, AI logging off, camera 1 (85, 31), instance tb1 (oldpc's own EXE)
    Series:               warm-up b + six rounds, the order turned each round (a,b,c / b,c,a / c,a,b x 2), one game at a
                          time, 6.3 min per arm; quiet job: alone true, undisturbed false (foreign 0.64 cores mean, the
                          top two tvnserver.exe, as 0470's set: 0.636); window CPU 72-75 % per arm (mean 73.3 %); free
                          commit 7,433 MB at the lowest start, 2,038 MB at the lowest end
    Result (paired by repetition, n 6, 95 % t interval; smallest detectable at 80 % power):
      EXE frame time idle    a 5.106 ms / b 5.121 / c 5.146
                             b - c -0.025 [-0.051, +0.002], det. 0.036 (0.70 %): S3, the filter's own cost: not resolved
                             b - a +0.015 [-0.031, +0.060], det. 0.062: the option as a whole, idle: not resolved
                             c - a +0.039 [-0.004, +0.082], det. 0.059: not resolved
      EXE frame time pan     a 5.473 ms / b 5.602 / c 5.588
                             b - c +0.014 [-0.024, +0.052], det. 0.052 (0.94 %): not resolved
                             b - a +0.129 [+0.086, +0.173] (+2.4 %); c - a +0.115 [+0.046, +0.185] (+2.1 %): the server's
                             units (and the bridge), not the filter
      longest pan frame      a 16.8 ms / b 19.2 / c 20.3: no pair resolved (det. 5-9 ms)
      draws dropped / frame  b 132.9 idle (sd 0.2), 122.3 panning; a and c 0
      EXE 3D utilization     a 24.46 % / b 21.28 / c 23.36: b - c -2.09 points [-2.20, -1.97] (-8.9 %): the dropped draws'
                             GPU work; c - a -1.10 [-1.33, -0.87]
      server CPU per frame   a 7.86 ms / b 11.29 / c 11.32: b - a +3.43 [+3.21, +3.65] (+43.6 %); b - c -0.03, not
                             resolved; panning a 7.65 / b 12.37 / c 12.17: b - a +4.72 [+4.23, +5.22]
      server frames per s    a 196.5 / b 130.0 / c 129.1: b - a -66.5 [-69.3, -63.8] (-33.9 %)
      server GPU per frame   a 1.115 ms / b 1.168 / c 1.171: no pair resolved (det. 0.12)
      records per s          turns: b 60.1 / c 60.1 (kinds 6 and 7; a: no listener); idle and pan: no record before
                             the turns (nothing moves)
      AI turn wall (2)       a 28.21 s / b 29.42 / c 29.43: b - a +1.20 [+0.27, +2.13] (+4.3 %, det. 1.26, at the limit);
                             b - c -0.01, not resolved
      server CPU per turn    a 45.26 s / b 52.91 / c 52.78: b - a +7.65 [+6.30, +9.01] (+17.0 %)
      server private end     a 1,913.4 MB / b 1,986.3 / c 1,998.5: b - a +72.9 [+61.8, +84.0]; peak +46.3 [+35.2, +57.5]
      EXE private            load a 1,417.8 / b 1,418.5 / c 1,417.0 MB, end a 1,514.7 / b 1,509.2 / c 1,509.6: no pair
                             resolved (det. 2.2 MB at the load, 9.4 at the end)
      server video local     at the load b - a -103.2 MB [-138.9, -67.6] (-10 %), at the end -23.6 [-37.4, -9.8]
      pre-check              b 12.92 s / c 12.81
    Machine load:         window CPU mean 73.3 % (72.1-75.3) per arm; quiet proof above; LOUD lines other than counted:
                          2 in 18 arms, neither from the filter or the units: R1-b `cut_tap: LOUD: load 3: a check failed
                          4 polls in a row ... (a torn read)`, R3-c `overlay_tap: LOUD: missed copy: no free slot (missed 17
                          ... copies 40349)`
    Profile:              none taken: the ticket asks for one only when b - c is not within the noise
    Why it moved:         (b - c) The filter adds per draw a thread-local context compare, a counter and one test on
                          ~1,712 draws a frame (3.1). On the ~133 it drops it adds the read-back of the bound pair
                          (PSGetShader / VSGetShader) and saves the d3d11 call and its GPU work, which shows as the EXE's
                          3D utilization 2.1 points lower. The two do not move the frame time at 0.036 ms resolution. The
                          frame time stays 5.1 ms although the GPU does less [I: the EXE's frame is bound by its CPU
                          side at this view, the same with and without the drop].
                          (b - a, c - a) The server's unit scene in position mode exe: the controller and the unit draws
                          at camera 1 cost the server +3.4 ms of CPU per frame and a third of its frames per second, and
                          +7.6 s of CPU per AI turn, where the render thread keeps drawing (4468: +5.4 s). The EXE pays
                          for it only while panning (+0.12-0.13 ms, c - a about equal to b - a), when both processes draw
                          new views at once [I: contention for the GPU and the cores]. The AI turn's wall +1.2 s is at the
                          detectable limit. The bridge's records are 60 per s in the AI turns; its listeners' time on the
                          main thread is not separable here: the EXE's frame time is not read during the turns (4.1).
                          [I] The lower video memory at the load in b and c is the server's terrain bake at two thirds of
                          the frames, as 4466's starved arm; not a saving.
    Correctness:          every arm in its mode by the logs and the filter's lines. The composite's totals per game: b
                          dropped 4.22-4.32 M draws with frames_unable 0, passed_set 0, mismatch 0; c passed 4.19-4.31 M
                          with the bit set and dropped 0; a 0 and 0. Game identity: the parity pair r1 above (F10)
    Trust grade:          A by PERFORMANCE.md 7 for the per-arm pairs: one interleaved series, one machine, six runs per
                          arm, the quiet job alone; its proof says undisturbed false (foreign 0.64 cores, over the 0.1
                          limit), as the earlier sets graded A (4466, 4468, 0470)
    Run records:          19 sealed c5-run/1 records in shell/evidence/0040/6/runs-20261117-122621-ef38/; summary
                          cost-summary-20261117-122621-ef38.txt, result cost-20261117-122621-ef38.json; job data
                          F:/c5-64bit-work/0040/4756/oldpc-20261117-122621-ef38/

The listeners' cost on the EXE's main thread is not measured; an upper bound needs the EXE's frame time during the AI
turns or a timer in the bridge (4.1). A re-run on a build from 3e1754fa5 on needs another arm c (4.1).

### 4.4 Not verified

- The pan through `cli.py key` on oldpc's game window (3706 used it on the workstation); the camera move per press at
  the huge save's zoom; whether the pan ends where it began (it alternates RIGHT and LEFT).
- The compositor's filter lines and their keys (4752's format); kind c as a whole.
- That 2 AI turns give enough moving units at camera 1 for a resolved records-per-second number.
- The job on oldpc's Python 3.8 (checked by reading and by the local import; no 3.8 on the workstation).


## 5. The switch's live acceptance (issue 4757)

Breakdown 6 item 2 and part of 4 (O6), premises S2 and F4. Written 2026-11-17 11:41 EST by w-4757 (account B, branch
`task-4757-switch-acceptance`); the drivers are in `shell/evidence/0040/7a/` and checked offline (`run_4757.py selftest`:
35 of 35, `selftest-out.txt`; `dry-run`: every import and harness file present). Results go to 5.3.

### 5.1 Method

**Where.** One Goldberg pool instance on the workstation (Vibrant Textures present, D94), claimed by `W-4757` (ticket
0040, address lead-0040, `--length long`), the import-time wrapper installed (0473) after the native build; the product
path of 3308's prep (the composite and its manifest beside the proxy, `[Renderer] ServerRenderer = 1`, config.ini =
`darkred.py`'s D33 list with Overlay kept on, no viewmask marker, no `server_config=`); the composite ini is the
branch's (`unit_filter=auto`, `hook_units=1`, the shipped defaults). Build: `build_4757.sh all` on this branch with 4752
merged (team branch f2f194e40): server.exe, proxy and vp64helper of this branch (release), the composite of this branch,
the pin layout `x64-04b1211b-pfdee656`'s x64 DLL 29F58E17... and libraries.

**Saves.** A copy `C5H-4757-HUGE_0350` of the instance's `C5H-VP8P-HUGE_0350` (C499D3C6, turn 350, quick movement and
quick combat on in the save) for the arms of record; a copy `C5H-4757-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C) for
the smoke run of the driver (base and filter, the two combat turns only), so that a driver fault costs a Tiny launch,
not a huge one (a minor decision of this ticket). Never the owner's saves; both copies are removed by `restore`.

**Arms** (one launch each; only `VP64Options.ini` changes between them): **base** `ServerUnits = 0` (the product today),
**filter** `ServerUnits = 1`, and **filter again** (the repeat). Each arm's mode is read from its own logs (4756's rule):
filter needs the proxy's `shell: [Renderer] ServerUnits = 1: server.cfg gets ...` line, `render_units=1` in server.cfg,
`units 1s:` lines and the bridge's `unit positions 2 of 2`; base none of them. An arm in the wrong mode exits 7 and is
not counted.

**Per arm** (`run_4757.py run`, after `prepare.py --load`, `launch.py` through the harness, `wait_game.py`,
`frontend.ensure_in_game`, 0444's diplomacy handler armed, a 20 s settle): 0317's probes unchanged (40 listeners in the
InGame context; music and ambience 0; every unit of the player asleep; the audio meter; create a warrior, select, a
quick 2-plot move, quick movement off, the walk back, damage, fortify, select, the move-mode path display with the
cursor three plots away and its picture (popups closed first, D114), selection mode, kill); then combat with Quick
Combat **on** and then **off** (`Network.SendGameOptions` on the main thread, read back): a human Warrior and Archer and a
barbarian Warrior next to each (0316's spawn), the melee (`MISSION_MOVE_TO`) and the ranged attack
(`MISSION_RANGE_ATTACK`) ordered as WorldView.lua does (`UI.SelectUnit` + `Game.SelectionListGameNetMessage`), each
sampled every 0.5 s until the defender is damaged or gone and the attacker is not busy (at most 25 s), one picture of the
Quick-Combat-off melee 1.5 s after the order, then the turn ended (`turns.py end` / `wait --answer 8`); then AI turns up
to 30 in all (the two combat turns count), each with the 40 counts, the EXE's and server.exe's memory, the D155 marker
and the stop file `C:/c5-64bit-work/0040/4757/STOP-GAMES` checked first; the counts again; the stop of 0444 (a leader
screen answered in game, the harness's safe stop with the human-turn hook; a box leaves the game for its owner, exit 8).
Every log of the run folder, the composite's log and the instance's own logs are copied into `out-<arm>-<tag>/`.

**What each item reads** (`run_4757.py analyze --base r1 --filter r1,r2`, each filter run against base):

| Item | Read from | Holds when |
|---|---|---|
| (1) the filter | the composite's `unitfilter 1s:` lines and the last `unitfilter totals:` (3.1), its `path 2` line, render.log's `units 1s:` (listed - culled = the server's units in view) | filter: path 2 `12 of 12 programs resolved`; `off=0` throughout; `frames_unable` 0 and no "cannot drop" line (3.3); in every 1 s interval with `frames_set` > 0, `frames_drop` = `frames_set` or the server had no unit in view in that second; `passed_set` 0; `mismatch` 0; every mask program (`8cb52b96`, `72c27aea`, `246527ed`, `59af33ff`, `9a5991d7`) dropped 0 with some passed > 0; the selection pair dropped 0. base: `frames_set` 0, `dropped` 0 |
| (2) events | each probe step's count deltas (0317's table, `unit-state-ii.md` 3) | every event equal per stimulus; the walk's per-frame events (`LocalMachineUnitPositionChanged`, `UnitHexHighlight` in the walk) within 25 % and both non-zero (they count frames, and the frame rate differs between arms); the five that stopped with the Unit key listed apart |
| (3) sounds | the EXE's audio session peak per step (`tools/harness/audio.py`) | each step within 25 % (or 0.02) of base; the walk's peak >= 0.1 (0317: 0.158-0.160, 0.011 with the Unit key) |
| (4) path display | the DIAGNOSTIC picture pair (base, filter) in Visual Checks, issue 4757 | discs and chevrons visible in both (judged on the pictures) |
| (5) 30 turns | the turn records, the run folder's logs, new dumps, Goldberg's `crashes.txt` | 30 turns played; no dump, no crash line, no `server-death` / `NO PLAY` / `boot FAILED` / `vp64helper failed`, no server-report; LOUD lines by signature, none that base lacks; `units 1s:` `refused=0` or each refusal line named |
| (6) combat | the attack records and the combat turns | Quick Combat on and off: the melee and the ranged attack resolved, the turn after them ended; the hidden fight under the filter recorded (O1 (a)) |
| (7) O6 | render.log's `units mode: start-up transition N ended: unit bit 1 after X ms since the load's kLoadBegin (Y ms and Z not-ready frames since the first drawn frame)` (4753) | measured on each huge load: Y is the transition from the first drawn map frame to the first ready frame |

### 5.2 Commands

From the worktree root:

    sh shell/evidence/0040/7a/build_4757.sh all                 # into C:/c5-64bit-work/0040/4757/layout
    sh shell/evidence/0040/7a/setup_4757.sh claim huge          # pool.py claim --owner W-4757 --ticket 0040 --address lead-0040 --length long
    sh shell/evidence/0040/7a/setup_4757.sh wrapper <instance>  # wrapper.py install <instance>/install (0473)
    C5_POOL_OWNER=W-4757 sh shell/evidence/0040/7a/chain_4757.sh <instance> tiny smoke   # base t1, filter t1 (2 turns)
    C5_POOL_OWNER=W-4757 sh shell/evidence/0040/7a/chain_4757.sh <instance> huge r       # base r1, filter r1, filter r2 (30 turns)
    python -I tools/visualcheck/visualcheck.py list --issue 4757 --result undescribed    # lists nothing
    python -I tools/harness/pool.py release <instance> --owner W-4757

`chain_4757.sh` = `run_4757.py prep` -> per arm `arm_4757.sh` (`quietlock.py check`, the D155 marker, `arm`, `run`) ->
`analyze` -> `restore` (by hash; `provision.py verify`; the save copy removed); on a box it leaves the game for its
owner and skips the restore (exit 8). Work and evidence: `C:/c5-64bit-work/0040/4757/live-<name>/` (`out-<arm>-<tag>/`
with `run.json`, `arm-analysis.json` and the logs; `acceptance-4757.txt`); pictures under
`C:/c5-64bit-work/0040/4757/pics/`; dumps to `F:/c5-64bit-work/0040/4757/dumps/`.

### 5.3 Results

**In short (2026-11-17 13:47).** With 4761's liveness fix, the switch holds on the huge save in two filter launches
against one base launch [L]: the EXE's pass-0 unit draws dropped in every frame with the bit set (165,000 frames, 13.5 M
draws, `passed_set` 0, the mask pass passed), every 0317 event per stimulus and every unit event's 30-turn total as in
base (S2 and F4 hold for these games), the walk's sound at base's level (0.146 / 0.207 against 0.156), the path display
present, 30 AI turns each with 0 crashes, combat with Quick Combat on and off resolved; O6's transition is 0 ms on the
huge load (the scene is ready at the first drawn frame) and 18-19 s on the Tiny one. Named exceptions: one stale-heartbeat
Present at r2's load end (the EXE's units for one frame), one `cut_tap` LOUD line at r2's load. Before the fix the filter
never latched in the product (first bullet below).

**The Tiny smoke run (2026-11-17 11:46-11:57 EST, pool instance c5h-a, workstation, Goldberg, Vibrant Textures present;
claim W-4757).** Build `build_4757.sh all` at the merge of 4752 (team branch f2f194e40; server source 0cbaef875 clean):
server.exe `d6b6c308...`, proxy `ae1b8b9f...`, composite `0d187c96...`, x64 DLL `29F58E17...` (pin 04b1211b8), release.
Save copy `C5H-4757-TINY_0018` (E337244C, turn 18); base t1 and filter t1, the probes and the two combat turns each.
Records: `shell/evidence/0040/7a/smoke/` (`acceptance-4757.txt`, per arm `lines.txt`, `run-summary.json`,
`arm-analysis.json`).

- **The filter never latched in the product: item (1) cannot hold on this build [L, C].** Filter arm: the composite's
  `unitfilter totals:` at the end: `presents=7336 frames_set=0 frames_drop=0 frames_unable=0 ignored_notinit=6714
  dropped=0 passed_set=0`; its first latch line `unit_filter: latched word 0x0 (raw 0x0, server notinit)`; every Present
  after the server's start-up transition saw the raw word with `VPMODE_UNITS` and refused it as `notinit`. Cause, read in
  the code: under `render_handover=ring` (the product's) the server's sink is the ring sink (`shell/frame/ring_sink.cpp`,
  render.log `ring sink: open on Local\VP64Shell_<pid>`), which writes `magic`, `serverPid` and `heartbeatQpc` only into the
  `<base>.Frame` section's `VPFrameServer` block; the main section's `VPShellHeader` keeps `magic` 0 (only
  `srv_v3_sink.cpp` sets `VPSHELL_MAGIC`), and its `serverPid` and `heartbeatQpc` are not written either. `unit_filter.cpp`
  (the latch, 3.1) takes liveness from the main header, so in the product it is always `notinit` and nothing is dropped.
  The stand-in host (3.5) initialises the main header itself, so 4752's tests could not see it. The composite's own legacy
  counters agree (`stats ... srv=0/0 tex=0x0`: no frame through the main header's texture block). Proposed fix (4752's
  files): in ring handover, take the liveness from `VPFRAME_SERVER` of the frame section (magic `VPFRAME_MAGIC`,
  `serverPid == writerPid`, its heartbeat younger than `stale_ms`, state not failed or stopped), with a stand-in case
  whose main header is left unset. Sent to lead-0040 (queue #4248), 2026-11-17 12:00.
- **S7 holds live by path 2 [L]:** `unit_filter: path 2: 12 of 12 programs resolved from the EXE's renderer (image
  0x007A0000, shader level 10, ...)` at the first Present after the EXE validation (11:53:31.9, 25 s before the load);
  path 1 counted 0 creations for all twelve (`c0` in every line), as GATE2 predicted. One object per program.
- **The rest of the switch, with nothing dropped** (so S2 is not tested by this run): the filter arm ran in its mode
  (the proxy's ServerUnits line, `render_units=1` in server.cfg, the bridge's `unit positions 2 of 2`); every event count
  per stimulus equal to base (the walk: `LocalMachineUnitPositionChanged` 52, `UnitHexHighlight` 52,
  `UnitMoveQueueChanged` 1 in both); the walk's sound peak base 0.127, filter 0.146; combat with Quick Combat on and off:
  the melee and the ranged attack resolved within 0.8 s in both arms (the DLL resolves at once; `RunCombatSim` 1 per
  attack with Quick Combat off), each turn ended; 0 LOUD lines; no crash; restore by hash ok, `provision.py verify` rc 0.
- **O6 on the Tiny load [L]:** `units mode: start-up transition 1 ended: unit bit 1 after 23110 ms since the load's
  kLoadBegin (19149 ms and 646 not-ready frames since the first drawn frame)`. (The huge loads are item (7)'s.)
- **The fix (4761, team branch 622a42eef: in ring handover the latch reads the `.Frame` section's `VPFrameServer`) holds
  live [L]: the Tiny filter smoke t2 (2026-11-17 12:34-12:41, c5h-a, composite `3d4f534d...`, server and proxy as above).**
  `unit_filter: latched word 0x1 (raw 0x1, server live) at present 595: the units' pass-0 draws are dropped`; 172 of 195
  1 s intervals with the bit set, `frames_set` 7,037 = `frames_drop` 7,037, `dropped` 211,338 (all PS `60d270e1`, material
  90; the Tiny copy shows no 87 / 89 units), `passed_set` 0, `mismatch` 0, `frames_unable` 0, every `ignored_*` 0
  (`ignored_srvstate` included); the mask programs `8cb52b96` / `72c27aea` passed 107,953 each and dropped 0. With the
  drops on: the walk 52 / 52 / 1 (`LocalMachineUnitPositionChanged` / `UnitHexHighlight` / `UnitMoveQueueChanged`),
  `SerialEventUnitSetDamage` 1, `SerialEventUnitDestroyed` 1, as base; the walk's peak 0.170 (Tiny base 0.127); combat with
  Quick Combat on and off resolved and each turn ended; O6 18,308 ms and 588 not-ready frames. The only count differences
  against the Tiny base are in the Quick-Combat-off steps, which the base t1 sampled with the first probe (it stopped at
  0.72 s, before `EndCombatSim` and the damage events; the probe now samples to the picture at 1.5 s, and the huge base
  has them). Records `shell/evidence/0040/7a/smoke2/`.
- **The huge base r1 (2026-11-17 12:03-12:30, c5h-a, the first build: composite `0d187c96...`, which differs from the
  filter arms' only by 4761's liveness path; with `ServerUnits = 0` the latch has no bit to read either way).** Save copy
  `C5H-4757-HUGE_0350`, turn 350 -> 380: rc 0, 30 AI turns (the two combat turns included; 30-53 s each, the first combat
  turn 125 s), mode base (no unit keys, `ui bridge: off`), 0 LOUD lines, no dump, no crash line; launch to in game 221 s;
  EXE private 1,446 MB after the load, 1,661 MB at the end; server.exe 1,699 / 1,972 MB. Key events: the quick move 2 / 2,
  the walk 52 / 52 / 1, damage 1, destroyed 1; with Quick Combat off the melee gave `RunCombatSim` 1, `EndCombatSim` 1. The
  walk's peak 0.156 (0317: 0.158-0.160). The Quick-Combat-off picture: Visual Checks `vc-20261117-121025-workstation-e46a`
  (the EXE's `TechAwardPopup` and `NewEraPopup` closed first, D114). Restore by hash ok, verify rc 0. Records
  `shell/evidence/0040/7a/hbase/`.
- **The huge filter arms r1 (12:43-13:08) and r2 (13:09-13:36), 2026-11-17, c5h-a, composite `3d4f534d...`, against
  the huge base r1 [L].** Save copy `C5H-4757-HUGE_0350`, turn 350 -> 380 in each; `run_4757.py analyze --base-work
  live-hbase --base r1 --filter r1,r2` (`shell/evidence/0040/7a/hfilter/acceptance-4757.txt`):

  | Item | r1 | r2 |
  |---|---|---|
  | (1) the filter | holds: 1,267 of 1,277 intervals with the bit; `frames_set` 82,492 = `frames_drop`; `dropped` 6,892,156 (PS `60d270e1`; no 87 / 89 draws in view); `passed_set` 0, `mismatch` 0, `frames_unable` 0, every `ignored_*` 0; mask `8cb52b96` / `72c27aea` passed 3,463,505 each, dropped 0; path 2 12 of 12 | holds: 1,384 of 1,394; 82,444 = 82,444; 6,653,940 dropped; `passed_set` 0, `mismatch` 0; one Present at the load's end (present 66, 13:11:29.313: `latched word 0x0 (raw 0x1, server stale)`) passed the EXE's units with the server's heartbeat older than `stale_ms`, the next Present 71 ms later dropped (inside O6's window: listed as `ignored_at_start`, not judged) |
  | (2) events | every 0317 stimulus equal to base; the whole run's totals equal for every unit event (the five included); UI-driven totals differ by 1-6 (`DisplayMovementIndicator` 16/15, `UIPathFinderUpdate` 14/13, `SerialEventCityInfoDirty` 798/802, `SerialEventGameDataDirty` 790/796, `SerialEventUnitInfoDirty` 914/917: cursor and panel events, listed) | holds the same way |
  | (3) sounds | the walk 0.146 (base 0.156; 0317 0.158-0.160); create 0.309, select 0.167 as base | the walk 0.207; the rest as base |
  | (4) path display | the PrintWindow picture came out UI-less (no interface, no EXE model, only the server's picture: run-and-drive 4.1's artifact), so not judged; retaken (p1 below) | the same |
  | (5) 30 turns | 30 played, 0 dumps, 0 crash lines, 0 LOUD, `refused=0` | 30 played, 0 crashes; one LOUD line not in base: `cut_tap: LOUD: load 3: a check failed 4 polls in a row (read again at every poll, nothing published): the grid's header changed during the copy (a torn read)` at 13:11:10, the composite's cut tap during the load (not a unit path; named, counted) |
  | (6) combat | Quick Combat on and off: melee and ranged resolved (< 1 s; with it off `RunCombatSim` 1 per attack, `EndCombatSim` 1 for the melee), each turn ended (126 s for the first, which carries the AI's turn 350) | the same |
  | (7) O6 | `unit bit 1 after 42047 ms since the load's kLoadBegin (0 ms and 0 not-ready frames since the first drawn frame)`: on the huge load the unit scene is ready at the first drawn frame, so there is no transition window | 47,642 ms since kLoadBegin, 0 ms / 0 frames since the first drawn frame |

  **Item (4) on the huge save, retaken (p1, 13:39-13:47, the same build and save, 1 AI turn, no combat; three shots 2 s
  apart):** the first shot shows the interface, Thebes and the path display: discs `1`, `<2`, `2`, `3` and the chevrons
  between them from the selected warrior at (100, 25) to (97, 25), with the filter dropping (Visual Checks
  `vc-20261117-134441-workstation-0e70`, retakes `...-134447-...-a631`, `...-134453-...-27ea`); the base's picture
  (`vc-20261117-120905-workstation-0e94`) shows the same at the same camera. The pair holds [L, judged on the pictures].
  p1's own filter verdict holds (O6 again 0 ms from the first drawn frame, 43.5 s after kLoadBegin; walk 0.148); its event
  totals and combat differ from base by construction (1 turn, no combat). Records `shell/evidence/0040/7a/hpics/`.
  The UI-less PrintWindow frames of r1 and r2 at this step were 2 of the 9 shots on the huge save in this ticket (the
  other 7 complete); run-and-drive 4.1's artifact, so a driver takes two or three.

  Memory (one launch each, a busy workstation: an indication): EXE private after the load 1,446 / 1,459 / 1,449 MB (base /
  r1 / r2), at the end 1,661 / 1,650 / 1,635 MB; server.exe 1,699 / 1,574 / 1,881 MB after the load and 1,972 / 2,174 /
  2,145 MB at the end (the server's unit scene in the filter arms). Launch to in game 221 / 220 / 221 s; AI turns 34-39 s
  on average.
- Rule changed after the smoke (a minor decision, 12:41): the walk's sound holds when its peak is >= 0.1 and >= half of
  base's (the peak varies 0.13-0.17 between launches of the same arm; the key state gave 0.011); the other steps keep
  25 % or 0.02.
- Side note: render.log's first mode line reads `the unit bit's writer is on (... render_units_positions=own)` at
  11:53:06 although server.cfg says `exe`; the bit was set at 11:54:09, which the writer does only with the source
  `exe`, so the start line names the default before the options were applied [I].

## 6. The picture's live acceptance (issue 4758)

Breakdown 6 items 1, 3, 4 and 5 with premises P1 and V1; written 2026-11-17 13:35 EST by w-4758 (account B, branch
`task-4758-picture-acceptance`). Every picture is **DIAGNOSTIC** (D112, O1 (a): `[Renderer] ServerUnits = 1` is not the
release's default) and is in Visual Checks under issue 4758 with the asset set (D94: Vibrant Textures on c5h-c); popups
closed first (D114). Results go to 6.3.

### 6.1 Method

**Build** (`shell/evidence/0040/7b/build_4758.sh`, 4757's recipe on a `git archive` of the branch's commit; release):
team branch 9e93111c1 merged at a43d163fd; server.exe `77a0cac3...`, proxy `5e373b0b...`, vp64helper `09799e78...`,
composite `93f6bee8...` (built from 432006e10; `shell/` is the same at a43d163fd), the pin layout
`x64-04b1211b-pfdee656`'s x64 DLL `29F58E17...` and libraries. The composite ini is the repository's
(`unit_filter=auto`, `hook_units=1`).

**Item (1), units drawn once and by the server, at 0375's DoD E cameras** (`cams_4758.py`, over 4309's camera driver
`shell/evidence/0169/4309/run_4309.py`: its prep / run / restore of the product path and its stock arm unchanged; this
file sets the owner W-4758, the data root `F:/c5-64bit-work/0040/4758/`, the save copies, the plans and the camera
function). Pool instance c5h-c (workstation, Goldberg). Saves: `C5H-4758-TINY_0008` (copy of c5h-c's
`C5H-3600-TINY_0008`, B2CEB663: 0375's catalogue save `REL548-TINY_0008` / CAT_0009) and `C5H-4758-HUGE_0350` (copy of
`C5H-VP8P-HUGE_0350`, C499D3C6). Cameras: Tiny 4458's load camera (51, 14) (the capital, the P1 scene's place), 4465's
water camera (51, 11) and the capital 4 wheel ticks in; huge 4461 / 0442's (85, 31), (110, 28), (92, 35). The Tiny seam
camera (0, 14) is left out: in this save it is under clouds (28 plots revealed; a minor decision). Arms, one launch
each, in this order per map: **stock1** and **stock2** (the stock EXE with the 32-bit pin `i0ship-26234371-ia`, stock
config.ini, no proxy or server; PrintWindow shots checked for the interface; two sessions give stock's ceiling), **u1**
(the product path with `VP64Options.ini [Renderer] ServerRenderer = 1, ServerUnits = 1`) and **u0** (the same with
`ServerUnits = 0`: the EXE draws the units over the server's terrain; the reference frame of item (1) after 4761,
since `unit_filter=off` with `ServerUnits = 1` is now D118). At each camera (E9: the active player's units asleep, the
auto unit cycle off): `UI.LookAt`, wheel ticks, popups closed, the cursor parked over the minimap, 8 s, the camera
block, the picture (joined arms: the composite's back buffer, the presented frame, and one passthrough frame for
compare_4309's model mask and drawn-whole check), the camera read again (a move fails the arm). Then the extra steps:
**sel** (the first land unit of the active player outside a city selected by `UI.SelectUnit`, the camera on its plot:
O4's selection effect), **strat** (`ToggleStrategicView()` on the main thread from MiniMapPanel, 0035's call: V1), on the
huge **leader** (one met AI's `DoBeginDiploWithHuman()` as the diplomacy list calls it, the picture at 7 s, closed by
`tools/harness/diplo.py`'s handler armed with a 14 s delay: S1). Tiny u1 also carries the server's TEST ONLY unit table
and dump keys (`server_cfg_extra`) for the drawn set against 4458's capture of the load camera (`drawn_rerun.py --state
load`); the huge u1 and u0 end with 0442's per-unit kill sequence at the three cameras (every unit within 7 plots that
the active team sees, killed one by one with a back-buffer dump after each; `render/evidence/0442/perunit_compare.py`
with T0 = u0, T1 = u1). The joined arms' logs (composite, render, server, vpproxy) are copied into `<pics>/logs`.

What each part reads:

| Part | Read from | Holds when |
|---|---|---|
| no EXE pass-0 unit pixel (the filter) | the composite's `unitfilter 1s:` lines inside each picture's window (`analyze_4758.py filter`), render.log's `units 1s:` | u1: at every picture frames_set > 0 and frames_drop = frames_set, passed_set 0, frames_unable 0, mismatch 0, every ignored_* 0 (the passthrough frame's own interval apart: there the latch ignores the bit as `notcomposite` by design), the selection pair and the mask programs never dropped; u0: frames_set 0 and dropped 0 |
| the drawn set | Tiny: the server's dump at the load camera against 4458's capture (`server/evidence/0375/4465/drawn_rerun.py`); huge: per-unit masks u0 against u1 | Tiny: no plot whose listed set differs from the EXE's drawn set; huge: no unit only one arm draws, or each named |
| COMMON 25 | `render/evidence/0169/4309/compare_4309.py --root F:/c5-64bit-work/0040/4758 --arm u1` (and `--arm u0`), its model checks (drawn whole, the `models` class) | the figures per class beside stock2 against stock1 (the ceiling), the difference images; drawn whole reported |
| selection effect (O4) | the sel pictures, u1 against stock | the streaks over the selected unit in both |
| shadows | the pictures; the mask programs' passes in u1 | the units' shadows present as in stock |
| strategic view, leader screen (item 4) | the strat and leader pictures u1 against stock; the filter's counters there | nothing that stock shows missing |

**Item (2), motion** (`cams_4758.py` map `tiny_motion`, arms stock1 and u1, the same save): a warrior created on a free
plot A within 2 of the capital, B two plots from A and within 2 of the capital (so the city's sight keeps the fog
still); quick movement off (`Network.SendGameOptions` on the main thread); at three cameras (the pair's middle at the
load zoom, 3 and 6 wheel ticks in) four walks A <-> B with PrintWindow bursts in this process for 4 s each (the same
capture in both arms) and a rest picture after each, then quick movement on and two quick moves; per camera one rest
picture with every flag hidden (UnitFlagManager) for the flag's template, and the background after the unit is
killed. `motion_4758.py`: the flag found by normalised cross-correlation of its template, the model as the change
against the background below it; offset = flag centre - model centroid; the deviation from the rest offset per moving
frame, median and p95, u1 beside stock. Cases (step `cases`): a warrior moved into the capital (garrison), a warrior
created on a water plot by the capital (embarked), 0442's `air_cargo.lua` (a carrier with two fighters rebased onto it,
two based in the city): one picture each, u1 against stock.

**Item (3), D118** (`neg_4758_job.py` on oldpc's shipped pack layout, tb2, through `submit_neg_4758.sh`; 4460's
`neg_units_job.py` pattern over `tools/testbed/jobs/release_d125.py` unchanged; the pack by `tools/release/pack.py` with
Release-5.4.8's x86 DLL 262aee88...; save `TB-TINY_0008` = B2CEB663): both arms with `ServerUnits = 1` in
VP64Options.ini. (a) `--fault units`: `render_test_units_db=` a file that is not a database (the unit scene's pre-check
fails); (b) `--fault filteroff`: `shell_composite.ini unit_filter=off` (the filter publishes `filterState` 0x502, the
server takes D125's step at once once its bit is latched). Checks: release_d125's (the EXE ends by itself, the report
folder, report.txt, no .dmp, config.ini restored by the helper to the player's SHA-256, no side file, failed=1, the
popup, no Game Crash box) plus the proxy's ServerUnits line, the fault's LOUD line, `exit code 5` in server.log and
report.txt, the reason in the report folder ("units: pre-check" for (a), "units drawn twice (D118)" for (b)), for (b) the
composite's filterState line. The oldpc runner held a quiet job (4756's cost series) until 14:31; the jobs waited there
because the workstation chain of item (1) held c5h-c meanwhile (one heavy job of mine at a time; a minor decision).

### 6.2 Commands

From the worktree root:

    sh shell/evidence/0040/7b/build_4758.sh all                                          # C:/c5-64bit-work/0040/4758/layout
    python -I tools/harness/pool.py claim --owner W-4758 --name c5h-c --hours 6 --ticket 0040 --address lead-0040
    python -I tools/harness/native/wrapper.py install <instance folder>/install
    python -I shell/evidence/0040/7b/cams_4758.py plans
    sh shell/evidence/0040/7b/chain_cams_4758.sh c5h-c tiny_0008 huge tiny_motion:stock1,u1
    python -I shell/evidence/0040/7b/analyze_4758.py filter --pics F:/c5-64bit-work/0040/4758/captures/<map>/u1
    python -I render/evidence/0169/4309/compare_4309.py --root F:/c5-64bit-work/0040/4758 --maps tiny_0008,huge --arm u1
    python -I shell/evidence/0040/7b/motion_4758.py --arm u1=<pics u1> --arm stock1=<pics stock1>
    sh shell/evidence/0040/7b/submit_neg_4758.sh units ; sh shell/evidence/0040/7b/submit_neg_4758.sh filteroff

### 6.3 Results

**Item (1) on the Tiny copy [L; DIAGNOSTIC] (2026-11-17 13:18-13:43 EST, c5h-c, workstation, Goldberg, Vibrant Textures
present; claim W-4758).** `chain_cams_4758.sh c5h-c tiny_0008`: stock1 13:18-13:24, stock2 13:24-13:29, u1 13:29-13:37, u0
13:37-13:43, every arm exit 0, restored by SHA-256 (`backup_restore_ok` true, every entry equal); no box, no camera move.
Pictures and logs `F:/c5-64bit-work/0040/4758/captures/tiny_0008/<arm>/`, compare `F:/c5-64bit-work/0040/4758/compare/`.

- **The filter at every picture, u1** (`analyze_4758.py filter`, `filter.json`): path 2 `12 of 12 programs resolved`,
  `filterState 0x1 (can drop)`; at e1 / e2 / e3 / sel / strat: frames_set 259 / 288 / 225 / 293 / 135, frames_drop equal
  in every one, dropped 2072 / 2304 / 1800 / 2344 / 0 (all PS `60d270e1`), passed_set 0, frames_unable 0, mismatch 0,
  every ignored_* 0; the selection pair `da2382af` / `8d853988` dropped 0 and passed in every window (259-293), the mask
  VS `8cb52b96` dropped 0 and passed 900-1172. Over the run: 168 of 211 intervals with the bit set, none short;
  totals frames_set 4969 = frames_drop 4969, dropped 36,736, passed_set 0, `ignored_notcomposite` 488 (only in the
  three passthrough frames' intervals, 69-79 each, by design), 0 LOUD lines in render.log, server.log and the
  composite's log. The server's units in view at e1-e3: 2 (`units 1s:` listed 16, culled 14). In the strategic view the
  bit stays latched and nothing is dropped (no unit model is drawn there). **u0**: frames_set 0 and dropped 0 throughout
  (197 intervals), the selection pair passed. Holds.
- **The drawn set at the load camera** (`drawn_rerun.py --state load`, the server's TEST ONLY dump at e1 against 4458's
  capture of the same save and camera): the server lists (0, 1002) warrior at (50, 13) and (0, 1003) pathfinder at
  (51, 15), the EXE's two drawn units (12 and 6 instances) in 4458's capture; changed plots none. **Equal.** (4458's
  four single instances at (24, 18), (27, 45), (28, 46), (51, 13) on plots without a unit are the header rows that are not
  a team colour, left out as in 12.1 of unit-server-scene.md.)
- **COMMON 25** (`compare_4309.py --arm u1`, pooled over e1-e3, plots at least 2 from unrevealed ones; median / p95 /
  max, share within 16): u1 vs stock1 shadowed_land 2/15/183 95.4 %, shadowed_water 5/23/213 89.7 %, models 6/50/226
  79.0 %, all_kept 3/21/213 92.7 %; **u0** (the EXE's units) vs stock1 2/15/165 95.8 %, 5/23/211 89.8 %, 6/45/234
  80.8 %, 3/20/211 93.0 %; stock's ceiling (stock2 vs stock1) 0/8/189 97.2 %, 4/22/214 90.7 %, 2/38/231 86.9 %,
  0/18/214 94.3 %. u1 and u0 are within a point or two of each other in every class: the gap to the ceiling is the
  product path's, not the units'. **Drawn whole: PASS** (pooled 0/2/155, 98.8 %; e1 99.6 %, e2 97.8 %, e3 98.7 %). The
  units (not gated by the check) against the same session's passthrough frame, i.e. the server's units against the
  EXE's own at the same camera: e1 15,455 px 0/4/37 99.7 %, e2 15,985 px 1/7/85 97.5 %, e3 40,841 px 1/6/126 99.2 %
  (u0, the EXE against itself: 99.9 / 98.8 / 99.7 %). Difference images and tables `compare/u1/`, `compare/u0/`.
- **Selection effect (O4)**: the column of light over the selected warrior 1002 in u1 as in stock (crop
  `vc-20261117-134933-workstation-f978`). **Shadows**: the units' shadows in u1 as in stock and u0 (crop
  `vc-20261117-134931-workstation-0e18`; the mask programs pass). **Strategic view (V1)**: the same hexes and icons in
  u1 and stock, no unit model in either (`vc-20261117-134934-workstation-b6ba`).
- Visual Checks (all DIAGNOSTIC): the arms' pictures (info, at capture) and the difference images per camera
  `vc-20261117-134925-workstation-f084`, `vc-20261117-134927-workstation-d5ee`, `vc-20261117-134930-workstation-9ba4`
  (pass).

**Item (1) on the huge copy [L; DIAGNOSTIC] (2026-11-17 13:43-14:25 EST, c5h-c, the same build and claim).**
`chain_rest_4758.sh` part cams: stock1 13:43-13:50, stock2 13:50-13:57, u1 13:57-14:10 (with 0442's kills), u0
14:10-14:25 (with the kills), every arm exit 0 and restored by SHA-256; no box, no camera move.

- **The filter at every picture, u1**: at h1 / h2 / h3 / sel / strat / leader frames_set 253 / 266 / 230 / 228 / 287 /
  340, frames_drop equal in every one, dropped 36,408 / 14,358 / 33,648 / 24,934 / 0 / 0, passed_set 0, frames_unable 0,
  mismatch 0, every ignored_* 0; the selection pair and the mask pass (`8cb52b96` 7,179-18,217 passed) never dropped. Over
  the run 515 of 537 intervals with the bit set, none short; totals frames_set 13,673 = frames_drop, dropped 837,044,
  passed_set 0, `ignored_notcomposite` 484 (the passthrough frames only); 0 LOUD lines. The server's units in view at
  h1 / h2 / h3: 32 / 31 / 24. **u0**: nothing set or dropped in 568 intervals. Holds.
- **O6 on this build [L]** (render.log): the huge load's start-up transition ended `0 ms and 0 not-ready frames since
  the first drawn frame` (46,665 ms after kLoadBegin: the server was ready before the first map frame); the Tiny load's
  24,394 ms and 803 not-ready frames (4757's Tiny smoke: 18-19 s).
- **COMMON 25** (`compare_4309.py`, pooled over h1-h3): u1 vs stock1 shadowed_land 2/24/174 92.8 %, shadowed_water
  9/43/239 74.8 %, farm_route 12/79/224 54.4 %, models 7/57/252 71.2 %, all_kept 6/61/239 73.9 %; u0 2/26/172 92.2 %,
  9/43/233 75.0 %, 12/78/227 54.6 %, 6/52/252 72.3 %, 6/61/233 73.9 %; ceiling 1/24/196 92.8 %, 9/43/234 75.0 %,
  11/78/228 55.4 %, 6/51/252 74.0 %, 6/60/235 74.3 %. Every class of u1 is at the ceiling or within 3 points of it, as u0.
  **Drawn whole: u1 FAIL at h1 and h3, pass at h2; u0 passes all three** (u1 h1 95.4 % within 16 < 97 %, p95 15; h3 one
  failing blob of 228 px at plot (97, 38), 4.8 %; u0 99.4 / 100.0 / 99.6 %). The units, not gated, against the same
  session's passthrough frame: u1 95.9 / 95.0 / 96.7 % (u0 99.0 / 99.0 / 99.7 %).
- **A difference of the server's unit drawing found: the caravans [L, picture].** At h3 the caravan (0, 17460) at (97, 38)
  is one truck facing left-down in stock1, u0 and u1's own passthrough frame (the EXE in the same session); the server
  draws it facing right-up, with a second truck below it (crop `vc-20261117-143841-workstation-c13a`, **fail**). This is
  the h3 blob, and the likely cause of h1's shortfall [I: not separated per blob at h1]. Cause [I]: a trade unit's facing
  and figures in the EXE come from its trade route (it is not moved by a mission, so no position or facing record reaches
  the server), the server takes the record's facing and its own formation. Not the filter's; for the owner of the unit
  scene's facing and formation (0042's C1 / feed rule (g)), named to lead-0040.
- **The drawn set per unit** (0442's kills in this launch, `perunit_compare.py --map-arms T0,T1`, T0 = u0, T1 = u1; both
  arms now have the EXE's unit shadows): camera h3: 18 units drawn by both, **1 only by the EXE** (the caravan (0, 17459)
  at (97, 36), at the picture's right edge, x 2508: its server mask 0 px, consistent with the server drawing that caravan
  turned and placed elsewhere, above), **0 only by the server**, 11 by neither; IoU median 0.62, centroid offset median
  6.5 px, area ratio 1.03 (0442's state (ii) arm: 0.63, the shadows missing there); inside both masks median 3 / p95 46
  against the EXE's own two pictures' 0 / 5. Cameras h1 and h2: **not separable** by this method this time (the plot ->
  picture map needs clear single-component kills: 5 and 2, below its minimum), so their drawn sets rest on the filter's
  counters (no EXE unit draw passed) and the server's listed counts.
- **Leader screen (S1)**: in u1 Isabella's scene is drawn whole and the filter dropped nothing in its window (dropped 0,
  frames_set 340 = frames_drop) (`x_leader_u1.png`). Stock's pictures of the screen came too early (7 s: both stock
  sessions shot the map); retaken in arm stock3 (below). **Strategic view (V1)** on the huge: dropped 0, frames_set 287
  = frames_drop.
- Visual Checks: the difference images `vc-20261117-143835-workstation-5801`, `-143837-workstation-db76`,
  `-143840-workstation-4478` (info), the caravan crop (fail, above), the cam-3 mask overlay
  `vc-20261117-143842-workstation-b0b3`.

**Item (3) (a), the server's units made to fail, on oldpc's pack layout [L; DIAGNOSTIC] (job 20261117-144243-b562,
14:46-14:49 EST, tb2, the machine's own EXE; results `C:/c5-64bit-work/0040/4758/neg-units/fetch-b562/`).** 16 of 16
checks hold: the proxy's `shell: [Renderer] ServerUnits = 1: server.cfg gets render_units=1 render_units_passes=1
render_units_positions=exe`; the TEST ONLY `render_test_units_db=` line; render.log +3.187 s `LOUD: units:
PrecheckFailed: pre-check: the unit database ...: file is not a database` and `units: the 64-bit path cannot go on: ...
the server ends (exit code 5)`, +3.192 s `remote: exit code 5`; the EXE ended by itself 47.9 s after its window
(55.8 s after the launch), no Game Crash box, one report folder with report.txt (`server pid ...: exit code 5`, the last
FAILED line naming `units: pre-check`) and no .dmp; `vp64helper failed` restored config.ini to the player's SHA-256,
failed=1, no side file, the popup on screen (Visual Checks `vc-20261117-144627-oldpc-5942` on oldpc's store); the
instance restored by hash, provision verify 0, the save copy removed. As in 4460's DoD G run the failure comes before
the map (the pre-check runs when the database is ready): the bit was never set, so no EXE unit was drawn before the
popup either.
- Two failed submissions on the way, both of the testbed job's inputs, not of the patch (fixed in
  `submit_neg_4758.sh` / `neg_4758_job.py`): job 132159-5eae stopped at its start because `tools/testbed/jobs/
  product_path.py` `setup_env` copies every entry of the shipped native bin with `shutil.copy` and the native build now
  makes a `wrappers` folder (PermissionError); job 143248-44d4 booted into the proxy's pack check, `stage: pack,
  file-missing: the x64 DLL CvGameCore_Expansion2_x64.dll` (a pack manifest names the x64 DLL by its 2606 name, the
  job's set-proxy lays the development name; and the pack listed only the tag's x86 DLL while tb2 holds the pin -ia):
  the job now copies the x64 DLL under the pack's name (listed in `added`, removed at the restore) and the pack carries
  tb2's -ia DLL as a test entry. 44d4's own checks read a run folder of the day before (`rf.boot_lines` takes the newest
  run, and that boot made none): its popup picture on oldpc's store describes a server exit that did not happen
  (amended in Visual Checks: `vc-20261117-143759-oldpc-2bb4`, info).

**Item (3) (b), `unit_filter=off` with ServerUnits = 1, on oldpc's pack layout [L; DIAGNOSTIC] (job
20261117-145124-9d6b, 14:55-14:57 EST, tb2; results `C:/c5-64bit-work/0040/4758/neg-filteroff/fetch/`).** 16 of 16 checks
hold. The composite: `config: unit_filter=off - TEST ONLY`, at its first Present `unit_filter: filterState 0x502 (cannot
drop: unit_filter=off)`. render.log: `units mode: load 1 begins` 14:55:37.055, `start-up transition 1 ended: unit bit 1
after 15585 ms since the load's kLoadBegin (12542 ms and 409 not-ready frames since the first drawn frame)` 14:55:52.640,
and in the same frame (server +46.048 s) `LOUD: units: the compositor's unit filter cannot drop the EXE's pass-0 unit
draws (unit_filter=off; filterState 0x502) while it has latched the unit bit: units drawn twice (D118)`, `the 64-bit
path cannot go on: ... the server ends (exit code 5)`, +46.056 s `remote: exit code 5 (...)`. So the EXE's units were
drawn only in O6's start-up window (12.5 s from the first map frame, the bit clear: drawn once, by the EXE) and the
server ended at the moment its bit would have made them drawn twice; the popup and the report followed: the EXE ended
by itself 90.4 s after its window, report.txt `server pid ...: exit code 5` with the last FAILED line naming `units drawn
twice (D118)`, no .dmp, config.ini restored to the player's SHA-256, failed=1, no side file, no Game Crash box; restore
by hash ok, verify 0. Popup `vc-20261117-145557-oldpc-c452` (pass). **4761's negative arm holds live.**

**Item (2)'s cases [L; DIAGNOSTIC] (map tiny_motion, stock1 14:25-14:40, u1 14:40-14:53, c5h-c):** a warrior moved into
Honolulu (garrison), a warrior created on the water plot (51, 13) (embarked), 0442's air_cargo.lua (a carrier at (51, 12)
with two fighters rebased onto it, two based in the city): in u1 as in stock, the garrison has no model and its flag in
the city banner, the embarked warrior is the boat with its flag, the carrier is drawn with its flag and cargo count 2,
no aircraft model in either (`vc-20261117-145349-workstation-d937`, pass). The motion bursts of this first try are not
used: the pair's path crossed the bay (the warrior embarked at (51, 12) each walk), the walking unit's sight changed the
fog around it, and the one background per camera was taken after the zoom had drifted; reworked (`262c1ee2f`: a pair
joined by a land plot, the area within 5 made visible by `Plot:ChangeVisibilityCount` in every arm, a fresh warrior and
its own background per camera) and run again.

**Item (5), the HI release journeys (D162) with ServerUnits = 1 [L] (`hi_4758.py`, lead-0040's minor decision #4329:
hi_journey's own tools on the epic's build, not the stage, which needs a release take; c5h-c, 1920 x 1080 (D158)).**
Product route (3308's product path with `[Renderer] ServerRenderer = 1, ServerUnits = 1`, a development layout, mode=on),
work `C:/c5-64bit-work/0040/4758/hi-145356/product/`: **release-basic** 14:54-15:07, `hi.py replay` exit 0, 63 steps,
666 s, checkpoints founded, research, turn5, turn10, loaded, menu all ok, the game gone 0.8 s after the journey's EXIT
and Yes, no leftovers; **release-large** 15:07-15:18, exit 0, 21 steps, 455 s, checkpoints moved, attacked, city, camera,
menu all ok (P1: a unit selected and moved by click and an attack ordered by click on the server's units). Both passed on
the product route.
- **Harness finding [L]:** after release-large (`ends: menu`) the game sits at the main menu, and the harness's stop
  (`cli.py stop --human-turn-hook tools.harness.turns:stop_readiness`) refused with exit 6 after 309 s: `needs a
  decision: Lua channel silent ...; main menu after a game, but Logs\ written 0.83 s ago (< 30 s): a load may be
  starting`. The product composite writes `shell_composite.log` into `<user>\Logs` every second (its `unitfilter 1s:`
  and `lockstep 1s:` lines go on at the menu: `latched=0x1` with the server still up), so on the product path that
  front-end condition can never hold at the main menu; hi_journey's own release-large on a product build meets the same
  stop. Not routed around (COMMON 22): a coordinator's decision asked (queue #4454); the MC allowed one Lua-free stop
  (#4458), run 15:39 with exactly that override (exit 0, no orphans), then the route's restores by hash (`hi_4758.py
  finish`: settings, front end, layout; verify 0). Fixed in the harness the same afternoon by 0506 (stop readiness
  ignores the composite's periodic log); its first live use at the product main menu is below.
- **Stock route** (the pin -ia in the CP slot, stock config.ini, no proxy; `hi-145356/stock`, `stock2`): release-basic
  15:41-15:53, exit 0, 63 steps, 594 s, 6 of 6 checkpoints; at its last step (EXIT and Yes) the EXE ended with
  0xC0000005 and no box (`exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION (no proxy crash record)`, 15:53:31): the -ia
  DLL's known MemoryImports exit crash (scene-catalogue.md 12, 0484), after every checkpoint had passed (accepted by
  lead-0040, #4511). My port of `play()` had left out the stage's `ended_crash` rule (a marker of kind crash with no game
  running is not a box) and stopped before release-large; the rule was added and release-large played in its own folder:
  15:55-16:05, exit 0, 21 steps, 414 s, 5 of 5 checkpoints; restored by hash.
- **`tools/hi/stockctl.py compare`, product against stock: release-basic 6 of 6 checkpoints `same`, release-large 5 of 5
  `same`, no findings: both journeys PASSED on both routes with ServerUnits = 1** (D162's pair, played by hi_journey's
  tools on the epic's build, not by the stage on a release take).

**Item (4), the leader screen [L; DIAGNOSTIC] (huge arm stock3, 16:38-16:43, the leader step only, the picture at 14
s):** stock3's Isabella and u1's are the same scene, leader and interface (`vc-20261117-164306-workstation-7816`, pass);
with the strategic views above: **nothing that stock shows is dropped (V1, S1).**

**Item (2), motion: the offset in pixels is NOT measured [L for the pictures; the method did not hold].** The retake
(16:13-16:38, map tiny_motion, stock1 and u1, pair (50, 14) -> (50, 12) past the fortified warrior at (50, 13), quick
movement off; four walks and two quick moves per camera, each camera with a fresh warrior and its own background)
recorded the bursts, but `motion_4758.py` found no usable model at rest at cameras 1 and 2 and nonsense offsets at
camera 3 (median 165 px, against a rest floor of 11 px): (a) the selected warrior 1002's light column stands over the
path and moves in every frame, (b) the walking group (12 figures) overlaps 1002's group on the middle plot, (c) in u1 most
PrintWindow frames of a burst come without the interface (E9: PrintWindow on the product path; at camera 3 the frames
with the interface end 1.1 s into the 4 s walk), so the flag is missing exactly where the comparison needs it. What the
pictures show (contact strips, stock above u1, `vc-20261117-164307-workstation-3ec6` camera 1 walk, `-164308-...-3a62`
camera 3 walk, `-164309-...-4db5` camera 3 quick move; info): in both arms the flag goes along with the group, at the
same place relative to it within what a 0.1-0.2 s frame interval shows; no lag of the model behind its flag is visible
[I, by eye]. **What an exact measure needs:** a capture of the presented frame at frame rate (the composite's
`shell_composite.shot` dump takes 0.3-1 s a frame; a burst trigger, or `listshot` over successive frames, would do), a
walk on open ground with no selected unit (`UI.ClearSelectionList` before the walk, E9's pan trap handled by the held
units) and the flag's screen position from the UI (the flag's world anchor) instead of a template; named for 0042 /
hand-over designs 6.3, not done here.

### 6.4 In short, and not verified

- **Holds [L]:** the filter at every picture of both maps (no EXE pass-0 unit draw passed while the bit was set,
  frames_set = frames_drop, ignored 0, selection effect and mask pass kept); the drawn set at the Tiny load camera; the
  units' pictures within stock's band (Tiny drawn whole PASS; huge classes at the ceiling as with the EXE's units); the
  selection effect, the shadows, the strategic view and a leader screen as stock; both D118 arms on the pack layout
  (16/16 each); both HI release journeys on both routes (stockctl all `same`); the garrison, embark and carrier cases.
- **Found [L]:** the server draws the caravan at (97, 38) of the huge save turned the other way with a second truck
  (drawn whole FAIL at h1 / h3 of the huge in u1; u0 passes): the unit scene's facing / figures of trade units, not the
  filter; owner open (lead-0040). Harness: `cli.py stop`'s main-menu condition never held on the product path (fixed by
  0506 the same day); `tools/testbed/jobs/product_path.py` cannot ship the native bin's `wrappers` folder and lays the
  x64 DLL under its development name while a pack's manifest names `CvGameCore_Expansion2_x64.dll` (worked around in
  `neg_4758_job.py`).
- **Not verified:** the flag-model offset in pixels in motion (6.3, item 2); the per-unit drawn set at the huge's
  cameras 1 and 2 (not separable by 0442's kill method this run); the Tiny seam camera (under clouds in this save); the
  journeys through the stage itself on a release take (hi_journey's tools on the epic's build instead, #4329); every
  picture is DIAGNOSTIC (O1 (a)); one build, one machine, one session per arm.

### 6.5 The live check on main after 0040 and 0487 (ticket 0512)

Written 2026-11-17 by the implementer of ticket 0512 (account B, branch `task-0512-serverunits-live-check`). Why: 0487
changed the UI bridge's install (since then it installs for three reasons: the whole bridge with `ui_bridge=1`, the unit
positions that `ServerUnits = 1` asks for, and 0487's frame hook alone), and no game had run with `ServerUnits = 1` on a
build that has both.

**Build** (`shell/evidence/0512/build_0512.sh all`, release): server.exe, proxy, vp64helper and composite of main
8577644f2 (after 0040's merge 8041d526b and 0487's), against a `git archive` of the fork at the batch-3 pin 4f4daac70
(full build_all through `server/build_server.py`: no batch-3 deps folder was left on disk); layout = the pin layout
`x64-4f4daac7-pf8e9f42`'s bin (x64 DLL `2D737BD5...`) with that commit's server.exe, proxy and composite. The
composite ini is the repository's (`unit_filter=auto`, `hook_units=1`); vpproxy.ini has no `ui_bridge` key (0).

**Method** (`shell/evidence/0512/run_0512.py`, 4757's prep / arm / launch / stop / restore with its own sequence;
`chain_0512.sh`): c5h-a (workstation, Goldberg, Vibrant Textures present, D94), the product path of 3308's prep, a copy
`C5H-0512-TINY_0018` of `C5H-3000-TINY-T0018` (E337244C, turn 18). Two launches: **u1** `VP64Options.ini [Renderer]
ServerRenderer = 1, ServerUnits = 1, UiDrawListGrowth = 1`, then **u0** the same with `ServerUnits = 0`. In each: the
load, a 15 s settle, the units asleep, popups closed (D114), a DIAGNOSTIC picture at the load camera, a warrior created by
the capital, selected and moved two plots, a DIAGNOSTIC picture, the warrior killed, 5 AI turns (`turns.py end` / `wait`,
the D155 marker and a stop file checked before each), a last picture, the harness's safe stop. Checks (`run_0512.py
check`) from the arm's own logs: vpproxy.log's `ui bridge: gen N installed ...` lines (u1: `unit positions 2 of 2
(positions only)`, no drain-only install; u0: `installed (drain only, for the frame hook)`), the `[Renderer]
UiDrawListGrowth = 1` line, `uidraw: EXE checks passed`, `uidraw: grown:`, the bridge's stats line's `frame hook calls`
growing and the `uidraw: grown; ...; frames N` status; render.log's `units 1s:` `positions=` / `arrivals=` and seconds
with units in view; the composite's `filterState` line (u1: `can drop`, none `cannot drop`) and 4757's filter verdict;
the turns, crash evidence, LOUD lines of every log (none allowed), no new folder under `<user>\VP64\reports` (D118 /
D119).

**Commands** (from the worktree root):

    sh shell/evidence/0512/build_0512.sh all
    python -I tools/harness/pool.py claim --owner impl-B-0512 --ticket 0512 --address dispatcher-B --length short
    python -I tools/harness/native/wrapper.py install <instance folder>/install
    C5_POOL_OWNER=impl-B-0512 sh shell/evidence/0512/chain_0512.sh c5h-a <name>
    python -I tools/visualcheck/visualcheck.py list --issue 0512 --result undescribed     # lists nothing

**Result (2026-11-17 17:47-18:26 EST, c5h-a; build server `4b3cee37...`, proxy `90f5a444...`, composite
`26ce2959...`, x64 DLL `2D737BD5...`; the build's identity line says `repo 68511ee22 dirty=1`: the uncommitted files
were this file and the evidence scripts, no product source) [L; every picture DIAGNOSTIC]: ServerUnits = 1 holds on main
after 0040 and 0487.** Two chains, each u1 then u0: `a` (r1, no hold, 17:47-17:56) and `b` (r2, with a 600 s rest after
the turns, 17:57-18:26; `run_0512.py check` rc 0 on both arms). Restored by SHA-256 after each (`backup_restore_ok` true, provision verify 0); no box, no
dump, no crash line, no new report folder, 0 LOUD lines in vpproxy.log, server.log, render.log and the composite's log
in all four launches; 5 AI turns played in each (turns 18 -> 23, 3-7 s each).

- **u1, the bridge installs for both consumers [L].** vpproxy.log: `shell: [Renderer] ServerUnits = 1: server.cfg gets
  render_units=1 render_units_passes=1 render_units_positions=exe; the Lua bridge's position listeners are asked for`,
  `uidraw: [Renderer] UiDrawListGrowth = 1: ...`, `ui bridge: the unit option asks for the unit positions (issue 4754)`,
  then one install, `ui bridge: gen 1 installed into the InGame context (...; a new environment) ...: Events found,
  listeners 0 of 0, unit positions 2 of 2 (positions only), drain (LocalMachineAppUpdate) added, global UnitMoving kept`
  (no drain-only install beside it: the positions install carries the drain that 0487's frame hook runs from), `uidraw:
  EXE checks passed (time stamp, 7 code patterns, 22 cross-checks; ...)` and `uidraw: grown: stream ..., 4194304 B, 16384
  commands, after 2 frames (growth 1 this process)` 2.4 s after the install.
- **0487's frame hook counts [L].** The bridge's stats line and uidraw's status line come every 3,600 Updates of the
  proxy's `ICvGame1::Update` hook, which on this Tiny game at rest took about 450 s (the first Update at +55.6 s, the
  line at +508.1 s: about 8 Updates a second); chain `a`'s launches (about 95 s in game) had none, so chain `b` held the
  game at rest after the turns. u1 r2 at +508 s: `ui bridge: gen 1, installs 1 (failed 0), ..., posted 215, drains
  15254, ..., stalls 0, frame hook calls 15254; unit positions: records unit-position 150 frame-mark 65, positions 150,
  arrivals 0 (no position 0), ..., units known 19 (clears 0); post failures 0 (after 3600 Updates)` and `uidraw: grown;
  grown 1 (regrown 0); frames 15254; data high-water 52096 B of 4194304 B (1 %)`: the hook ran once per drain and
  uidraw counted every one of them.
- **Unit position records arrive and the server draws the units [L].** render.log `units 1s:` (u1 r2, last line)
  `ready=1 listed=40 culled=37 drawn_frames=22664 refused=0 pos=exe positions=150 arrivals=0`; the start-up transition
  ended `unit bit 1 after 23266 ms since the load's kLoadBegin (19764 ms and 644 not-ready frames since the first drawn
  frame)` (r1: 20870 / 18173 ms, 591 frames). `arrivals` stayed 0: Quick Movement is on in this save, so the created
  warrior's two-plot move came as position records with no arrival (`queue changed with moves left 0`) [I]. The
  composite: `unit_filter: path 2: 12 of 12 programs resolved`, `unit_filter: filterState 0x1 (can drop)` (4761's name;
  no other filterState line), totals `frames_set=22873 frames_drop=22873 frames_unable=0`, every `ignored_*` 0; 4757's
  verdict holds (r1: 104 of 126 intervals with the bit set, frames_set 5155 = frames_drop, dropped 52,182, passed_set 0,
  mismatch 0). No D118 report (no new folder under `<user>\VP64\reports`, no `exit code 5`). The pictures show each unit
  once, the moved warrior at its new plot with its flag (`vc-20261117-175046-workstation-6f64`, r1).
- **u0, the bridge installs for uidraw alone [L].** No ServerUnits line, no position request; `ui bridge: gen 1 installed
  (drain only, for the frame hook) into the InGame context (...): Events found, drain (LocalMachineAppUpdate) added`;
  the same `EXE checks passed` and `grown ... after 2 frames` lines; no `units 1s:` line; the composite `filterState 0x1
  (can drop)`, frames_set 0 and dropped 0 throughout (`latched=0x0`). r2 at +497.8 s: `... drains 16687, ..., stalls 0,
  frame hook calls 16687; post failures 0 (after 3600 Updates)` (no positions part: the positions are not on) and
  `uidraw: grown; grown 1 (regrown 0); frames 16687`. So the frame hook alone keeps the bridge's drain and is counted.
- Visual Checks (issue 0512, info, DIAGNOSTIC): r1 u1 `vc-20261117-175024-workstation-445f` (load),
  `-175046-...-6f64` (moved), `-175126-...-9da4` (after the turns); r1 u0 `-175419-...-aa31`, `-175443-...-0e7c`,
  `-175526-...-0dac`; r2 u1 `-175952-...-5548`, `-180015-...-38fb`, `-180055-...-5a6b`; r2 u0 `-181425-...-c956`,
  `-181450-...-310a`, `-181528-...-8651`.
- **For whoever reads the stats lines next [L]:** at rest the proxy's Update hook runs about 8 times a second on this
  Tiny game, so the bridge's and uidraw's status lines (every 3,600 Updates) come about every 7.5 minutes; a run of a
  few minutes in game has none. Read the install and `grown:` lines instead, or keep the game in play long enough.
- **Not verified:** the arrival record (Quick Movement on in the save); a second stats line in one launch (the count
  growing between two lines); a huge save; the ACCEPTANCE form of the pictures (ServerUnits = 1 is not the release's
  default, D112).
