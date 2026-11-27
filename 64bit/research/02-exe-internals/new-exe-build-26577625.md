# The 2026-11-19 Steam update (build 26577625): what in this project depends on the old EXE, and how to move it

Ticket 0582 (decision D200, the owner 2026-11-19). Firaxis shipped a Civ V update on 2026-11-19 (Steam build 26577625). The
build every address in this knowledge base was taken from is Steam build 4390913: DX11 EXE `CivilizationV_DX11.exe`,
16,982,584 B, PE TimeDateStamp 0x546CD0A8, FileVersion "1, 0, 3, 279, (403694) (11/19/2014)", PDB name
`...\Civ5Win32Final Release Steam CEG D3D11.pdb`. The pre-update install is kept as a verified copy at
`F:/c5-64bit-work/golden/civ5-steam-build4390913-ws` (manifest `civ5-steam-build4390913-ws.sha256.json`, 13,379 files;
ticket 0583).

This file has two parts. Part A (phase 1, 2026-11-19, before the new files were on any project machine) is the
inventory of everything in this repository tied to build 4390913 and the method and scripts for the comparison.
Part B (phase 2, 2026-11-19) is the comparison of the two installs and the answers to the owner's five questions; B.0 has them in one line each.

Scripts: `evidence/0582/` (all read-only, no third-party packages; Python 3.11, run with `python -I`).

| Script | What it does |
|---|---|
| `pe32.py` | A small PE32 reader: headers, sections with entropy and relocation counts, imports with IAT slot RVAs, delay imports, exports, relocations, TLS callbacks, load config, debug directory (PDB name), Authenticode signer names, Rich header, version strings. `python -I pe32.py FILE [--json out]` |
| `tree_diff.py` | Compares two SHA-256 manifests: same / changed / added / removed per folder and per extension, player/mod files listed apart, and for every changed `.exe`/`.dll` the header facts that moved (stamp, sections, imports added/removed per DLL, exports, signer, PDB) |
| `exe_scan.py` | DRM and integrity indicators of one EXE: the Valve `VLV` block in the DOS stub, high-entropy runs (the per-user CEG blob), STEAM_DRM_IPC names, `.bind` (SteamStub), TLS callbacks, anti-debug / signature / hashing APIs imported or named in strings, code references to section starts; the Lua global names (`loadstring`, `io`, `os`, `dofile` ...) with the code that references each |
| `inventory.py` | The repository inventory of section A.1 (patterns per file class; VA literals in code files outside comments and strings, with the old section of each) |
| `addr_match.py` | Carries addresses from the old EXE to the new by bytes, not position (A.3). Validated on two cross-build pairs of build 4390913; a vtable fallback by entries (B.2) |
| `pattern_check.py` | Rebuilds a product pattern table (`exe_camera.h` syntax) with carried operands and checks it on the new EXE (B.2) |

## A.1 Inventory of the repository's ties to build 4390913 (2026-11-19, commit 19958b92b) [C]

Counted by `inventory.py` over the working tree (files, not occurrences; `.git`, `third_party` and `.claude` skipped).
"product" = `proxy/src`, `shell/{composite,common,frame,server,standin}`, `server/src`, `render/{include,src}`;
"tests" = any `tests/`, `rt_tests/`, `test_*`; "tools" = the rest of the code (`tools/`, `perf/`, `shell/tools`,
`proxy/tools`, build scripts); "evidence" = any `evidence/` folder. Full table and per-file lists:
`evidence/0582/inventory-2026-11-19.md`; every VA literal with file and line: `evidence/0582/inventory-vas-2026-11-19.csv`.

| Tie | product | tests | tools | research notes | research sources | evidence | tracker/docs/ops |
|---|---|---|---|---|---|---|---|
| DX11 stamp 0x546CD0A8 | 25 | 18 | 11 | 40 | 12 | 84 | 9 |
| DX9 / Tablet stamps 0x546CCB59 / 0x546CD5F8 | 0 | 1 | 3 | 9 | 2 | 0 | 0 |
| EXE SHA-256 workstation `b2ce96ae...` | 1 | 0 | 2 | 29 | 1 | 7 | 9 |
| EXE SHA-256 Goldberg `pure_vp` `04cfdc71...` | 0 | 1 | 4 | 20 | 0 | 130 | 6 |
| EXE SHA-256 laptop `ba0c491b...` (oldpc's is `d2ccb491...`, not counted) | 0 | 2 | 0 | 2 | 0 | 35 | 2 |
| EXE size 16,982,584 | 0 | 0 | 0 | 3 | 1 | 15 | 6 |
| Steam build id 4390913 | 20 | 3 | 3 | 26 | 2 | 14 | 7 |
| version "1.0.3.279" | 2 | 3 | 0 | 3 | 1 | 5 | 0 |
| `d3dx9_42` / `d3dx11_42` / `D3DCompiler_42` | 3 | 4 | 4 | 8 | 5 | 21 | 4 |
| `lua51_Win32` | 3 | 6 | 19 | 13 | 11 | 79 | 11 |
| `steam_api` | 0 | 8 | 22 | 11 | 2 | 164 | 15 |

The three EXE hashes are one build: the copies differ only in per-user CEG data, the Valve header block and the
signature (`06-tooling-and-workflows/headless-and-parallel-testing.md` 3.2). After the update every machine's EXE
has a new hash; whether per-copy differences remain without CEG is a phase-2 check (B.1).

**EXE addresses in code.** Hex literals in the old DX11 image range (0x401000-0x6519000) in code files: 2,552
occurrences; unique values 859 in product code, 334 in tests, 128 in tools. Outside comments and string literals
(the ones the code uses; a few are other constants that fall in the range): **491 unique values in product code**
(by old section: `.text` 279, `.data`/`.bss` 187, `.rdata` 13, `.shader` 12), 270 in tests, 106 in tools. Addresses
written as RVAs (`proxy_pacing.h`, the 4 MB queue) and structure offsets are not in that count. The product files
with the most: `render/src/terrainsets/sets.cpp` 120 (mostly provenance in data tables), `shell/composite/unit_filter.cpp`
117, `decal_tap.cpp` 57, `water_tap.cpp` 39, `ice_tap.cpp` 29, `shadow_tap.cpp` 27, `shell/common/exe_camera.h` 25,
`farm_guard.cpp` 24, `proxy/src/proxy_shell.cpp` 21, `cut_tap.cpp` 17, `forest_tap.cpp` 15, `fow_models.cpp` 13,
`overlay_tap.cpp` 12, `render/src/liveterrain/producer.cpp` 11, `proxy_uidraw.cpp` 10, `shell_model_scene.h` 10.

**Address-keyed research notes.** 116 notes in sections 02-08 carry old-image VAs: 8,864 occurrences, about 3,072
distinct values (02: 17 notes / 3,804; 05: 47 / 4,039; 03: 15 / 448; 06: 15 / 280; 08: 7 / 213; 07: 12 / 77; 04: 3 / 3).
`addresses.md` is the single table (~245 rows). These stay valid as statements about build 4390913; they need a
build label, not a rewrite (A.4).

**There is no single build table in this repository.** Each feature carries its own stamp check (the 25 product
files above). Section A.2 lists the mechanisms per file and what each does on another EXE.

**The VP DLL has its own table.** Upstream Vox Populi (`CvGameCoreDLL_Expansion2/Exe/CvExeBuild.cpp` and
`CvExeAddresses.h`, PR #13379, merged 2026-11-11; upstream/master at 1fb49429f, 2026-11-16) keys 30 EXE symbols and
23 checks by PE stamp (DX11 0x546CD0A8, DX9 0x546CCB59, Tablet 0x546CD5F8), moves each to the loaded base, compares a
byte signature, and logs "ExeBuild: unknown EXE (timestamp ...)" and uses none of them on any other EXE. Its uses are
fixes in the EXE's own behaviour (the yield-icon manager unregistration at exit, `Civ5NetMessage::s_wantForceResync`,
the city building simulation and wonder removal). On the new EXE those fixes switch off; nothing crashes from that
side [C, source; I for the effect]. Its header records the CEG rule the project has followed: "Steam CEG hashes about
80% of .text and two thirds of .rdata in the loaded image".

**File names the update changes.** The old DX11 EXE imports D3DX9 and D3DX11 version 42 statically (maths functions
and one texture save), besides Direct3D 9 and 11, DXGI, Lua, Steam, the game's two database and localisation libraries
and the C++ runtime; it imports no shader compiler (its shaders are compiled into the EXE's `.shader` section). The
update moves the two D3DX libraries from version 42 to 43. (D83 edit 2026-11-25, 0772: the old EXE's static imports
per library with their function counts were replaced by this description.) The workstation's SysWOW64 already holds `D3DX9_43.dll`, `d3dx11_43.dll`, `D3DCompiler_43.dll`
(2026-11-19 listing) [C]. So do `oldpc` and the `laptop` (ssh, read only, 2026-11-19 21:30): `D3DX9_43.dll` 1,998,168 B,
`d3dx11_43.dll` 248,672 B, `D3DCompiler_43.dll` 2,106,216 B, the same sizes as the workstation's, next to the `_42`
files [C]. Goldberg instance copies use the host's SysWOW64, so nothing is missing for `_43` on any project machine.

## A.2 Mechanisms by which the product touches the EXE

Read from the code at 19958b92b, 2026-11-19 [C, source read; behaviour on another EXE inferred from the code, not
run]. Letters: (a) IAT slot write, (b) vtable or function-word swap, (c) data write (.data/.bss or EXE heap),
(d) read-only read, (e) call into an EXE function, (f) hook in another module, (g) `.text` byte patterns used as
validation (never written), (h) a section read by name from the EXE file, (i) Lua C API through `lua51_Win32.dll`.
VAs at ImageBase 0x400000; every module rebases them on the loaded base.

| Component | Mechanisms | Old-build values | Check, and what it does on another EXE |
|---|---|---|---|
| `proxy_session.cpp/.h` (session kind, reports) | d | g_Civ5App RVA 0x8C8E74, +0x940 config mask, GVS vtable RVA 0x620908, +0x1B4 view mask | `IsKnownExe`: stamp + SizeOfImage. Unknown: masks not read, session "stock (assumed)" |
| `proxy_shell.cpp` view mask | c (heap view mask), d, g | ~6 RVAs, 6 patterns | stamp, size, patterns; mismatch: off, quiet |
| `proxy_shell.cpp` white mask / model fog (D136) | c (texture handles 0xDA12AC/0xDA12B0, draw-object pool 0x313DE88), e (texture loader 0x8C6540), g | ~18 RVAs, ~16 patterns + 9 operand checks | mismatch: nothing loaded, quiet |
| `proxy_queue_guard.cpp` | a (KERNEL32!InterlockedExchangeAdd) | slot RVA 0x5D647C (found by name, then compared with the fixed RVA), queue 0x160E400/0x160F080 | unknown stamp: off, not loud |
| `proxy_uidraw.cpp` | c (UI manager stream fields), e (4 allocators 0x82C090, 0x813A80, 0x813D10, 0x813CF0) | 10 VAs, 13 offsets, 7 patterns | unknown stamp: off, not loud |
| `proxy_pacing.cpp/.h` (D165 options 2-4) | b (engine-utility vptr to a copied 264-entry vftable 0x5EA718), c (GVS+0x8CC, queue-B byte), d | ~15 RVAs | stamp, size, section flags; **configured on and mismatched: refuse, D118 report and popup** |
| `proxy_leaderscene.cpp/.h` | d (scene map RVA 0x9A27A4), e through the engine interface (not by VA) | 1 RVA + map offsets | off, one log line |
| `proxy_farm_guard.cpp` + `shell/composite/farm_guard.cpp` | b (listener node function word under the EXE's lock), c, e (builder 0x748FC0), d, g | ~14 VAs, 13 patterns | stamp then patterns; mismatch: writes nothing |
| `proxy_hooks_human.cpp` | e by export name (`FStringA_GetStringInitData`) | none | abort if the export is missing |
| `proxy_lua*.cpp`, `proxy_ui_bridge*.cpp` | i (import by name), engine interfaces | none | none. The Lua channel's runner (`proxy_luaexec.cpp` LUAEXEC_RUNNER, test tooling) calls `G.loadstring` from the engine's `_G` |
| `shell/common/exe_camera.h`, `shell_composite.cpp` camera | d, g | 14 VAs + 10 offsets, 11 patterns | `CamValidate` once: stamp + patterns; mismatch: REFUSED, and **every tap inherits it** |
| `shell_composite.cpp` lockstep probe (`lockstep=1` default) | f (MinHook on `d3dx9_42!D3DXMatrixInverse`), g | IAT slot 0x9D6B74, thunk 0x8A86B6, 0x51E1BC/0x51E1C8 | slot must equal `GetProcAddress("d3dx9_42.dll", ...)`: **the only product dependency on a `_42` file name** |
| `shell_composite.cpp` D3D hooks | f (d3d11, dxgi) | none | none needed |
| composite taps: bake, water, ice, overlay, shadow, decal, cut, forest, fow_models | d (reads only), g | 4-12 VAs each (0xD165E0, 0x414C348, 0x1A0C694, 0xDA12A4, 0xDA12B8 ...), 7-52 patterns each | camera validation + own patterns; mismatch: REFUSED/FAILED; the server's fatal steps (overlay, fog, decal, shadow, ice, units, models, forest) end the server once (`srv_render_host.cpp`, D125) |
| `unit_filter.cpp` | f, d, g | path 2: 0x8BBC40, 0x8BBBF0, 0x3010570 ... | path 1 matches shaders **by SHA-256** at creation (build-independent if the shaders are unchanged); path 2 fixed VAs: loud, off |
| `server/src/render/srv_liveterrain_exefield.*` (Extras gate 4902) | d, c **cross-process** (ReadProcessMemory / NtWriteVirtualMemory into the EXE's height-field pools) | 0xD91270, 0xD90FC4, constants 0xD90FBC | stamp + pointer check; mismatch: refused, loud |
| `render/` shader section (`shadersection.*`, `terrainsets`, water/ice/fow/decal/forest/arc/bake) | h (`.shader` by name from the player's EXE file) + fixed blob VAs (`findByVa`, ~50) | create-site table 1,417 rows | create sites applied only on the stamp, else silently not; a blob not at its VA: NotFound. Units, landmarks, path models find programs **by SHA-256** |
| `srv_exe_crash.cpp`, scene recorder | report text: the EXE's stamp and SHA-256 | literal stamp | text only |

Summary [C, source]: no component writes `.text` or `.rdata`; nothing depends on CEG being present (0x8000DEAD is
documented in research, never handled in code); no game DLL is identified by hash; `steam_api.dll` is never named in
product code; `lua51_Win32.dll` is an import by name. The stamp 0x546CD0A8 is duplicated in 10 places
(`proxy_shell.cpp`, `proxy_queue_guard.cpp`, `proxy_uidraw.cpp`, `proxy_pacing.h`, `proxy_leaderscene.h`,
`proxy_farm_guard.cpp`, `exe_camera.h`, `srv_liveterrain_exefield.h`, `srv_exe_crash.cpp`, `shadersection.h`) plus
`proxy_session.h`; several addresses are duplicated across files (g_Civ5App in 4, GVS vtable in 3, 0x414C348 in 5).

*Corrected 2026-11-19 (ticket 0588): the paragraph below describes the code at 19958b92b. Since 0588 the proxy decides
from the build registry before the server starts; see B.4, "0588 behaviour as built".*
**What the merged product did on an EXE it does not know (before 0588)** [I, from the code, not run]: nothing gates the
boot; the server starts; the camera validation refuses, so every composite tap refuses and the first fatal step ends
the server (D125); pacing options configured on refuse with the D118 popup; `SessionKind` returns "stock (assumed)"
because the masks are not read on an unknown EXE, so `proxy_rt.cpp` takes the **32-bit fallback** (`BootOff`) rather
than the D33 no-play path, even when `config.ini` is in the D33 form. The harm depends on `config.ini`: with
a stock `config.ini` the fallback is an ordinary 32-bit game, with the EXE drawing everything. With `config.ini` in
the A'' form, which `vp64helper on` writes (`proxy/helper/proxy_cfgini.cpp`) as the installer's set-up does, the
fallback is a 32-bit game without terrain (`release-driver.md` W3b.6), which is the case D125 was written to prevent.
Ticket 0583 (the update guard) is where this belongs. Checked by the MC on 2026-11-19 by reading files only: the owner's
main install has the patch in the mods form with `mode=on`, but its `config.ini` is the recorded stock file. On that
install the update would give a stock-drawn 32-bit game, not a broken one [I, not run].

## A.2b Who calls the Lua functions the update is said to remove (2026-11-19) [C, file read]

Lua files that call `loadstring`, `load`, `dofile`, `loadfile`, `io.*` or `os.execute/exit/getenv/remove/rename/setlocale/sleep/tmpname`.
Read-only scans of the workstation's golden copy, the owner's `My Games/.../MODS` (VP 5.4.8 in the mods form), and the Steam
installs on `oldpc` (old VP 5.2.3 modpack in DLC, EUI) and the `laptop` (no VP in its Steam install: no hits):

| Caller | Call | When it runs | Without the function |
|---|---|---|---|
| EUI `Assets/DLC/UI_bc1/ToolTips/InfoTooltipInclude.lua:3` | `loadstring(k.."=EUI."..k)()` in a loop at file load | when any of the 4 EUI files that include it is loaded (UI contexts) | error at include, so the including screen's script stops; EUI tooltips broken [I] |
| CPK `Kit/UI/Control/Instance/CPK.UI.Control.Instance.{Make,Manager,Error}.lua` | `local lua_loadstring = loadstring` captured at load; called as `lua_loadstring('return ContextPtr')()` / `'return StateName'` | Make.lua:71 and Manager.lua:262 only when no context is passed; Error/Manager for error text | "attempt to call a nil value" on those paths only; the load itself succeeds [I] |
| CPK `Kit/CPK.lua:46-91` | `os.getenv`, `io.popen`, `dofile` | only on the "not inside Civ5 Lua" branch (include emulation outside the game) | none in game |
| Communitu map script (`Assets/Maps`, VP `Mapscripts`, `DLC_SP_Maps`) `FloatMap:Save` | `assert(io.open(name, "w+"))` | a debug save of a height map; no caller in normal generation [I, not traced] | error only if called |
| IGE (`InGame Editor+`) `LoadFile(` | a UI function of IGE's own, not Lua's `loadfile` | - | none |
| this project: `proxy/src/proxy_luaexec.cpp` LUAEXEC_RUNNER (the harness's Lua channel; also the fork's `CvLuaSupport.cpp`) | `G.loadstring(code, '=luaexec')` from the engine's `_G` | every Lua-channel request (test tooling, not the product) | the channel returns an error for every request if the engine's `_G` loses `loadstring` |

Which Lua states lose which names, and whether `load(reader)` stays, is a phase-2 question (B.3). The workaround for
every caller above is the same: `load` with a reader function (Lua 5.1: `load(function() local s = src; src = nil; return s end, name)`), or, in C, `luaL_loadbuffer`, which a DLL or the proxy can still call.

## A.3 Method for the comparison, and how well it works [C]

1. **Install diff** (`tree_diff.py`): manifests of the golden old copy and the new copy; per-folder and per-extension
   counts; for each changed PE: stamp, sections, imports added/removed, exports, signer, PDB.
2. **EXE headers** (`pe32.py`, `exe_scan.py`): section table (is there a CEG blob, a `.bind`, a new section?),
   DllCharacteristics (FORCE_INTEGRITY, GUARD_CF), load config (CFG tables), TLS callbacks, entry section, Rich header
   (same compiler as 2014 or a new one), PDB name (the "Steam CEG" build configuration gone?), signer.
3. **Integrity and signature checks at run time**: imports and dynamically named APIs (WinVerifyTrust, Crypt*,
   BCrypt*, IsDebuggerPresent, NtQueryInformationProcess, GetThreadContext), code references to section starts;
   any new site found is read with `llvm-objdump -d` over that range (output stays in scratch, D83).
4. **Lua state set-up**: the code that references `loadstring`, `io`, `os.*` names in the new EXE that has no
   counterpart in the old one (addresses mapped back with `addr_match.py`) is the new sandbox; it is described as
   behaviour.
5. **Address carry-over** (`addr_match.py`): for a code address, the bytes from the address on with every relocated
   dword and every call/jmp/jcc rel32 masked are searched in the new `.text` (windows 16-160 B forward, then windows
   ending just after the address, first unique hit); failing that, each direct caller's context votes for the new
   target. For a data address, every instruction whose relocated operand is that address is found by its masked
   context in the new `.text`, and the new operand read there (majority vote); addresses inside a structure use the
   nearest referenced address below (+offset, low confidence).

   **Validation on build 4390913's three EXEs** (separate compilations and links of the same source; 2026-11-19):
   DX11 -> Tablet and DX11 -> DX9 with the 19 non-offset symbols that VP's PR #13379 table maps in all three builds,
   and the 16 rows of `addresses.md` "Other builds": **34 of 35 right, 0 wrong, 1 unmatched** in each pair (the miss is
   a mid-function code address, 0x525BD3, `ResetNetMessageStatics_ClearWantForceResync`). Truth files:
   `evidence/0582/truth_*.csv`. Over the 491 product-code VA literals (DX11 -> Tablet, no truth for most):
   `.text` 266 of 279 matched with high confidence (95 %), `.data` 162 of 187 (134 high, 28 medium), `.rdata` 11 of 13,
   `.shader` 7 of 12 (`evidence/0582/dryrun-product-vas-dx11-to-tablet.csv`). Run time 8 s for all 491.

   Limits: a compiler change (new MSVC instead of VS2008) changes instruction selection and would lower the byte-match
   rate sharply; a mid-function address whose surroundings are not unique needs a person. Every carried address still
   needs its existing byte-pattern check (most taps have one) and a live smoke before use (`merge-check-live-smoke`).

## A.4 What a migration would touch (phase-1 estimate, before seeing the new EXE; confirmed and sized in B.4) [I]

If the new EXE keeps the same compiler and the patch's shape holds (part B checks both), a migration touches:

- **Product code**: ~25 files; 491 VA literals (279 code, 187 data, 13 `.rdata`, 12 `.shader` blob VAs) plus RVAs
  and structure offsets in `proxy_pacing.h` and the queue guard; ~200 byte patterns in the taps, camera, farm guard,
  white mask, uidraw; the 1,417-row shader create-site table; 10 stamp constants. The matcher carries about 90 % of
  the addresses automatically, going by the Tablet dry run (`.text` 95 %, `.data` 87 %). Structure offsets change
  only if the classes changed, which a rebuild from the same source does not do. The patterns need regenerating from
  the new bytes.
- **Tests**: 18 test files carry the stamp; 270 VA literals in tests (fake images, expected values).
- **Tools**: 11 tool files check the stamp (`tools/harness/instance.py`, `shell/tools/view_mask.py`,
  `shell/tools/gt_reader.py`, `tools/i0b/read_masks.py`, `perf/...`), `tools/instances/provision.py`
  (stamp/size refusal from 0583), the Goldberg instance provisioning (CEG gone may simplify it), `render/build.py`
  (EXE hash).
- **Research**: 116 notes keep their addresses as facts about build 4390913. One line per section README and a
  build column in `addresses.md` are enough; a rewrite is not.
- **Upstream VP**: `CvExeAddresses.h` needs a fourth column (30 symbols, 23 checks), which is VP's own maintainers'
  work or an upstream PR.

## Part B: the comparison (phase 2, 2026-11-19 21:30-23:00)

Inputs: `F:/c5-64bit-work/golden/civ5-steam-build4390913-ws` (old) and `F:/c5-64bit-work/golden/civ5-steam-build26577625-ws`
(new; the workstation's Steam install after the owner's update at 21:16, copied by the MC, 13,379 files, manifest verified).
Static analysis only: no game run, nothing written to either copy. Disassembly of single ranges with `llvm-objdump`
(LLVM 19) stayed in scratch (D83); this file describes behaviour and addresses.

### B.0 Answers in one line each

1. **Files outside the EXE**: 11 of 13,379 files changed. No Lua, XML, SQL, map, model, sound or shader file
   changed. `lua51_Win32.dll` and `Launcher.exe` are byte-identical apart from an appended signature. The other DLLs
   were rebuilt from the same code (game-core DLLs: same layout, a few hundred code bytes moved). The DX11 EXE imports
   `d3dx9_43`/`d3dx11_43` instead of `_42`, which every project machine already has. For Vox Populi this is
   compatible except for the Lua sandbox (EUI's tooltip include breaks) and VP's own EXE table (its EXE fixes
   switch off until a column is added) [C].
2. **The patch's shape holds** [C, static]: same compiler (VS2008 SP1, MSVCR90), same classes and structure
   offsets, same 264-entry engine-utility vftable, same imports by name (no import the product uses was removed),
   same exports, `.shader` byte-identical, `.data` within 1,344 B of the old size. Functions were re-linked in another
   order, so every address moved.
3. **Writing `.text`/`.rdata` is now possible as far as static analysis shows** [I, strong]: CEG is gone entirely, and
   nothing replaced it (no self-hash, no signature check at run time, no anti-debug beyond the old CRT's
   `IsDebuggerPresent`). The 2K Authenticode signature covers the file on disk, not the process's memory. A live check
   is described in B.3 and needs the MC's go.
4. **Update size**: 4-6 agent-days, about one calendar week with 2-3 agents for code, tables, tests and live smokes,
   mostly by tooling (about 95 % of the product's addresses carry over automatically; 19 by hand). Another hotfix of the
   same kind costs the same again today, or about half a day to a day with a generated per-build table (B.4).
5. **Showstoppers: none found.** No new anti-tamper, nothing that ties the patch to the old EXE.

### B.1 What changed outside the EXE (question 1) [C]

`tree_diff.py` over the two manifests (`evidence/0582/install-diff-4390913-to-26577625.md`): same 13,368, changed 11,
added 0, removed 0. Every changed file:

| File | Old -> new | What changed |
|---|---|---|
| `CivilizationV_DX11.exe` | 16,982,584 -> 15,466,264 B, stamp 0x546CD0A8 -> **0x6AF3837D** | rebuilt without CEG (B.2, B.3) |
| `CivilizationV.exe` (DX9) | 10,602,040 -> 9,091,864 B, 0x546CCB59 -> 0x6AF3818F | the same; plus a new 4 KB section `.2k` (read-only, discardable, holding a 2K build marker `2K_<hash>.20261110_221827_...`; no code reference, no relocation) |
| `CivilizationV_Tablet.exe` | 16,990,776 -> 15,475,992 B, 0x546CD5F8 -> 0x6AF385BA | as DX11 |
| `lua51_Win32.dll` | 151,040 -> 166,680 B, stamp 0x514886F2 unchanged | **byte-identical** body, a 2K signature appended (15,640 B) |
| `Launcher.exe` | 1,730,048 -> 1,745,696 B | byte-identical body, signature appended |
| `CvGameDatabaseWin32Final Release.dll`, `CvLocalizationWin32Final Release.dll` | +15,640 B each | rebuilt: `.text` identical, 40 B of `.rdata` (debug directory) differ, signed |
| `CvGameCoreDLLFinal Release.dll`, `Expansion/CvGameCore_Expansion1.dll`, `Expansion2/CvGameCore_Expansion2.dll` (the stock game-core DLLs; VP replaces Expansion2's with its own) | +15,640-15,648 B each | rebuilt from the same code: identical section sizes and RVAs; 446-1,367 bytes of `.text`, 29-80 KB of `.rdata` and the relocations differ (data addresses shifted) |
| `Resource/DX9/ConquestDeluxeTextures.fpk` | 60,822,224 B both | 192,186 bytes differ between file offsets 0x10507C and 0x163FCF7 (content not examined) |

Every new binary is signed by "2K Games, Inc." (Microsoft ID Verified CS AOC CA 03, Microsoft RSA timestamp); checked
with `Get-AuthenticodeSignature`: Valid. The old EXEs were signed by Valve Corp. (CEG re-signs per user); the old DLLs
were unsigned. The build machine path changed (`g:\BuildAgent\work\a44d3f775c9d8d84\...`, old
`c:\BuildAgent\work\66e2ae1702439a00\...`); the PDB name still says "Steam CEG D3D11". The version resource is
unchanged ("1, 0, 3, 279, (403694) (11/19/2014)"), so the game-version string in saves (and
`server/src/srv_civ5save.cpp`'s `kCiv5GameVersion`) stays the same.

**The Lua sandbox** (the only behaviour change found) [C, disassembly of the new EXE compared with the old]. The EXE has
exactly one `lua_newstate` (VA 0x7779D3; the state is kept at `this+0x81900`) and one `luaL_openlibs`, so every Lua
context the EXE runs (front end, in-game UI contexts, mods' UI, map scripts, the DLL's Lua) lives in this one VM and
shares its `_G` (`06-tooling-and-workflows/installer-notes.md`: 154 Lua states, all threads of it). Its set-up function
(new VA 0x776FC0, run with `lua_cpcall` from 0x777A08; old VA 0x7A5B30 from 0x44B9C8) opens the standard libraries,
registers the memory-usage globals (`GetLuaMemoryUsage` ... `CollectDllContextMemoryUsage`), and then, **new in this
build**, sets the globals `loadstring` and `io` to nil, reads `os` and, if it is a table, sets `os.execute`, `os.exit`,
`os.getenv`, `os.remove`, `os.rename`, `os.setlocale` and `os.sleep` to nil, and goes on as before (`math.clamp`,
`table` helpers). The old build goes straight from `CollectDllContextMemoryUsage` to `math`. **Kept**: `load`,
`loadfile`, `dofile`, `require`/`package`/`module`, `debug`, `os.time`/`clock`/`date`/`difftime`/`tmpname`. The C API
(`luaL_loadbuffer`, `luaL_loadstring`) is untouched, so a DLL or the proxy can still compile chunks. The third party's
summary that the owner relayed is confirmed on these points, except that `load` and `dofile` stay.

Consequences (callers from A.2b):
- **EUI** (`UI_bc1/ToolTips/InfoTooltipInclude.lua:3`) fails when it is included: an error at include in the 4 EUI
  files that include it [I, the include runs in the shared VM]. The equivalent without `loadstring` is a plain table
  assignment into the file's environment. That is a fix in EUI or VP's EUI compatibility files, not in this patch.
- **CPK** `CPK.UI.Control.Instance.{Make,Manager,Error}.lua` fail only on the paths that call `lua_loadstring` (no
  context given, error text); `load` with a reader or a direct `ContextPtr` reference replaces them. VP's code.
- **Our Lua channel** (`proxy/src/proxy_luaexec.cpp` LUAEXEC_RUNNER, and the fork's `CvLuaSupport.cpp`) takes
  `loadstring` from `_G`, so every harness Lua request would fail on the new build. A small change (Lua 5.1's `load`
  takes a reader function, so wrap the string in one; or a C closure over `luaL_loadbuffer`). Test tooling, not the
  product.
- The patch's product code calls no removed function.

**What the file changes mean for compatibility** [C for the files, I for the effects]: the patch's own binaries load
`lua51_Win32.dll` by name (identical code), never hash a game DLL, and the x64 ports of the database and localization
libraries were made from code that has not changed. VP's DLL replaces the stock Expansion2 DLL as before; the
interfaces the EXE offers it are unchanged (the same 264-entry engine-utility vftable). VP's EXE interop
(`CvExeBuild`, 30 symbols) sees an unknown stamp and turns its EXE fixes off with a log line; all 12 of its
signature-carrying symbols still match their byte signatures at the addresses carried to the new build (B.2), so a
fourth column is all it needs.

### B.2 Does the patch's shape hold (question 2) [C, static]

| Mechanism | Old build | New build |
|---|---|---|
| Compiler / runtime | VS2008 SP1 (MSVCR90/MSVCP90) | the same: Rich header tool build 30729, the same MSVCR90 (165) / MSVCP90 (96) imports |
| Import slots | 31 DLLs | the same DLLs (d3dx `_42` -> `_43`), **no import added**; 25 removed, all used by CEG or the start-up (file mapping, events, security descriptors, registry reads, the command line, process exit); none is hooked by the product (D83 edit 2026-11-25, 0772: a list of 12 of the removed import names was replaced by these kinds). The slots the product uses exist by name at new RVAs: `KERNEL32!InterlockedExchangeAdd` 0x9D647C -> 0x939414, `d3dx9_4x!D3DXMatrixInverse` 0x9D6B74 -> 0x939B00, `MSVCR90!malloc` 0x9D67E0 -> 0x93967C, `free` 0x9D67E4 -> 0x939678 |
| vtables of heap objects | engine-utility vftable 0x9EA718 (264 entries), GVS vftable 0xA20908, FCamera 0xA9A1B0, model instance 0xA9A748 | 0x9854A0 (264), 0x94E850 (6 = 6), 0x9CA3A0 (3 = 3), 0x9CA938 (13 = 13) |
| `.data` / `.bss` reads and writes | `.data` VA 0xB94000, vsize 0x5606020 | `.data` VA 0xA25000, vsize 0x5605AE0 (1,344 B smaller); globals moved, found by their references (163 of 187 product data addresses carried); structure offsets unchanged (camera patterns below) |
| Hooks in other modules | d3d11, dxgi, user32, d3dx9_42 | unchanged; the lockstep probe's `GetProcAddress("d3dx9_42.dll", ...)` becomes `d3dx9_43.dll` |
| Calls into EXE functions | texture loader 0x8C6540, allocators 0x82C090/0x813A80/..., farm builder 0x748FC0, `NotifySpecificAILeaderInGame` through the interface | carried by bytes (allocators, builder: high confidence); through the interface: unchanged |
| `.shader` section (read by name from the file) | VA 0x619B000, 6,996,740 B | **byte-identical** at VA 0x602B000: every blob VA shifts by exactly -0x170000; the SHA-256-matched programs (units, landmarks, path models) need nothing |
| Exports | 22 (`FSBA*`, `GetFileSystemMgr`, `FStringA_GetStringInitData` ...) | the same 22 |
| ASLR | DYNAMIC_BASE, 143,327 relocations | DYNAMIC_BASE, 133,709 relocations |

The code was re-linked in a different order, so every function moved, often by megabytes:
`LoadCvGameCoreDLL_SetEngineUserInterface` 0x47BA70 -> 0x7B35F0, `UpdateViews` 0x51DF70 -> 0x4C66F0,
`FCamera::GetViewProj` 0x8E4A80 -> 0x84AA20. Function bodies are the same bytes apart from relocated operands and call
displacements.

**Carry-over measured** (`addr_match.py`, 2026-11-19): of the 491 VA literals in product code, `.text` 263 of 279 with
high confidence, `.data` 163 of 187 (131 high), `.rdata` 11 of 13, `.shader` all 12 by the fixed shift. A second pass
that reads the misses as RVAs (`proxy_pacing.h`, the queue guard, `proxy_shell.cpp`) or as import slots carries 25
more; 7 are not addresses (0x1000000, 0xFFFFFF ...). **19 remain for a person**: 3 mid-function code addresses in
`decal_tap`/`farm_guard`, 7 entries of one `decal_tap` `.rdata` table, and 9 data addresses in `farm_guard`,
`water_tap`, `unit_filter`, `sets.cpp` and `producer.cpp`. Map: the local per-build table (D202: stored locally only, never in git; see `tools/exebuild/` once epic 0590 lands; removed from git 2026-11-19).
**Independent checks**: VP's own byte signatures (PR #13379) hold at all 12 carried addresses that have one;
`exe_camera.h`'s 11 patterns, rebuilt with carried operands, hold 11 of 11 (`pattern_check.py`). The engine-utility
vftable was found by its entries (`addr_match.py` `match_vtable`, added for this).

### B.3 Can `.text` or `.rdata` now be written (question 3) [I, strong, static only]

What is gone [C]: the `VLV` block in the DOS stub; the 576 KB high-entropy CEG blob in `.rdata` (old VA 0xAB4D60); the
`STEAM_DRM_IPC`/`STEAM_DIPC_CONSUME` names and the file-mapping and event imports the Steam DRM IPC used; the one code
reference to the start of `.text` (old 0x45A87F, the base of the hash loop); the `0x8000DEAD` exit code (old
0x495F16, the DRM's exit on debug registers); the per-user Valve re-signing.

What is not there [C]: no TLS callback; no new import; no `WinVerifyTrust`, `Crypt*`, `BCrypt*`,
`CertGetCertificateChain` or `MapFileAndCheckSum` import or name string (`CryptAcquireContextW` is a string in both
builds; `IsDebuggerPresent`, `OutputDebugStringA` and `SetUnhandledExceptionFilter` are imported in both, by the CRT
and the crash handler); no `NtQueryInformationProcess` or `GetThreadContext`; no `.bind` (SteamStub); FORCE_INTEGRITY
and GUARD_CF are not set. The two code references to the start of `.rdata` are calls through the first import slot.
The 2K Authenticode signature is checked by Windows on the file (SmartScreen, WDAC policy) and by Steam's file
verification; it says nothing about the process's pages, and writing them does not change the file.

So, by static analysis: an in-memory write to `.text` or `.rdata` (VirtualProtect, write, restore) would not be noticed
by anything in the new EXE. Caveats [I]: a future update can add a check again; Steam itself (overlay, client) is not
known to inspect game memory for Civ V (no VAC listing found); the write exists only in memory, so every launch must
make it again. Also follows [I]: hardware breakpoints and debuggers no longer end the process (the 0x8000DEAD path is
gone), and a Goldberg copy no longer needs the Steam client as the DRM responder (`headless-and-parallel-testing.md`
on `STEAM_DRM_IPC`).

Live check that would confirm it (not run; it needs the MC's go and runs on a Goldberg copy of the new build, never on
the owner's install): a test DLL in the instance changes one `int3` padding byte that no code reaches (for example new VA
0x49240E, between two functions) and one `.rdata` byte that no code reads, verifies both writes, then plays 50 AI
turns and a save and load. Pass: no exit, no crash, the same game as an unpatched control run.
The live checks of the mechanisms that were run since (ticket 0592, D205) are in
[new-exe-live-mechanism-check.md](new-exe-live-mechanism-check.md) (link added 2026-11-20 by ticket 4967).

### B.3a Upstream's own EXE table on the new build, and the DRM markers of all six EXEs (2026-11-20, one-off under D218) [C, static]

The owner ordered a one-off (decision D218) that updated upstream Vox Populi's EXE interop for build 26577625: draft pull
request `LoneGazebo/Community-Patch-DLL` 13614 (branch `exe-addresses-steam-hotfix` on the fork, one commit, base
upstream master c434d45da; not compiled, no game run of a DLL built from it). What it established:

- All 30 symbols of upstream's `CvGameCoreDLL_Expansion2/Exe/CvExeAddresses.h` resolve on build 26577625 in the DX11,
  the DX9 and the Tablet EXE, and every signature of that header matches unchanged. Each value came from a signature
  that matches once, or from following the header's own checks from such a match; none from offset arithmetic. The
  values themselves are in that pull request (the owner's one exception to D202) and are not repeated here.
- 22 of the header's 23 `EXE_CHECK` relations hold unchanged. The 23rd, the check inside
  `cvCityBuildingSim_CreateNewCity` that names `CityData_Hex`, sits 0x17 bytes earlier in all three new EXEs: the update
  removed a CEG check stub (a `(x + 3) % 10` test and a call) from the function's body. It is the same kind of stub
  removal as the `fg_builder_layout` hand case of [exe-build-table.md](exe-build-table.md).
- Upstream picks its column by the PE TimeDateStamp: the new EXEs' stamps are DX11 `0x6AF3837D`, DX9 `0x6AF3818F`,
  Tablet `0x6AF385BA` (the 2014 ones `0x546CD0A8`, `0x546CCB59`, `0x546CD5F8`). The pull request replaces the old
  stamps, so upstream's DLL would no longer recognise the 2014 EXEs; keeping both builds needs three more columns and a
  per-build offset for the one moved check. That choice is left to upstream's maintainer.
- Static DRM markers on all six EXEs (extends B.3, which covered DX11 only): the 2014 DX11, DX9 and Tablet EXEs carry
  the `VLV` block in the DOS stub, the string `STEAM_DRM_IPC` and an immediate `0x8000DEAD`; none of the three
  26577625 EXEs carries any of them. Live proof exists for the new DX11 EXE only (ticket 0592's runs, B.3 and
  [new-exe-live-mechanism-check.md](new-exe-live-mechanism-check.md)); the DX9 and Tablet EXEs are static only.
- Cross-check: this project's own table for build 26577625 agrees with the pull request on the two symbols both hold
  (`cam_fn_setengineui`, `qg_channel_index`).

B.0 item 1's "VP's own EXE table (its EXE fixes switch off until a column is added)" is what that pull request
addresses; until upstream merges it, upstream's DLL still logs `ExeBuild: unknown EXE` on the new build.

### B.4 What needs updating, and the cost of the next hotfix (question 4) [I, estimate from the counts above]

Work for a migration to build 26577625 (not started; D200 needs the owner's OK):

| Item | Size | How |
|---|---|---|
| Product addresses | 491 literals in ~25 files, plus RVAs and offsets in `proxy_pacing.h` and the queue guard | `addr_match.py` carries ~95 %; 19 by hand; structure offsets unchanged |
| Byte patterns | ~200 (taps, camera, farm guard, white mask, uidraw) | the same code bytes with new operands: regenerate the tables with `pattern_check.py`'s method |
| Shader create-site table | 1,417 rows (`.text` call sites) + ~50 blob VAs | sites by the matcher; blobs -0x170000 |
| Stamp checks | 10 duplicated constants + `proxy_session.h` | one build table |
| `d3dx9_42` name | lockstep probe, test host | `_43` (or whatever the EXE imports) |
| Tests | 18 files with the stamp, 270 VA literals, fake images | regenerate with the tables |
| Tools | 11 tool files (stamp, size, hash), `provision.py`, Goldberg provisioning, `render/build.py` | the stamp table; provisioning may get simpler without CEG |
| Lua channel | the `proxy_luaexec.cpp` runner (`loadstring`) and the fork's `CvLuaSupport.cpp` | `load` with a reader, or a C closure |
| Research | 116 notes with old addresses | a build label (and a new-build column in `addresses.md`), no rewrite |
| Upstream VP | a fourth column in `CvExeAddresses.h` (19 addresses carried and checked here) | VP's maintainers, or a PR on the owner's word |
| Live smokes | ~15 features (camera, taps, unit filter, farm guard, queue guard, uidraw, pacing, leader scenes, white mask, live terrain) | one product-path run per feature group |

Estimate: **4-6 agent-days** (2-3 for code and tables, 1-2 for tests and tools, 1-2 for live smokes), about one
calendar week with 2-3 agents. The risky part is the live smokes; the address work is mechanical.

**A second hotfix**: if Firaxis ships another build made the same way (same compiler, relinked), it costs the same
week again with today's per-feature constants. With one change made during the migration, a **per-build table
generated by `addr_match.py` and `pattern_check.py`** (one file keyed by the PE stamp that every feature reads, with the
existing patterns kept as the run-time check), a hotfix of that kind costs **about half a day to a day**: run the
matcher, review the few misses, regenerate, smoke. Full run-time resolution by signature (scanning the loaded EXE at
boot, as VP's PR #13379 does for its 30 symbols) would make such a hotfix free of code changes. It costs a boot-time
scan (tens of ms with anchors) and the risk of a wrong unique match, which needs cross-checks (operand agreement, in
the style of VP's EXE_CHECK) and a loud refusal on any ambiguity. Recommended: the generated table first, with the
run-time patterns as the guard; run-time scanning only as the fallback for an unknown build. A build made with another
compiler would defeat both, and is the one case that costs weeks.

**Player-facing behaviour on an unknown EXE (for ticket 0588)** [recommendation]: decide at boot, before the server
starts, from the one build table. An unknown stamp means the 64-bit path is not started. If `config.ini` is in the A''
form it is restored first (the helper's `off` path), so the game is an ordinary stock-drawn 32-bit VP game, never one
without terrain. One popup per new EXE build says that Civilization V was updated (stamp shown), that the 64-bit patch
is off until it supports this build, and where to get an update; the D119 report records it. Today the proxy instead
starts the server, lets the taps refuse and the server end, and falls back without a word (A.2).

#### 0588 behaviour as built (2026-11-19, ticket 0588 = epic 0590 child 4961, branch `task-0590-c2-0588-fail-loud`) [C, unit tests; not run live]

What the proxy and the installer now do on an EXE build they do not know. Code: `proxy/src/proxy_session.cpp/.h`
(`ReadExeBuild`, `DecideBoot`, the notice record, config.ini as a mask), `proxy/src/proxy_rt.cpp` (`DllGetGameContext`,
before the pack check and the server), `proxy/src/proxy_report.cpp` (the [exe] build line), `tools/installer/patch64.iss`
(`ExeBuildCheck`).

- **Identity.** The build is read from the one registry `exebuild/include/vp_exe_build.h` (PE TimeDateStamp and
  SizeOfImage together; a stamp alone is not enough), on `GetModuleHandleW(NULL)`, with VirtualQuery before every read.
  `VP64_EXE_TIMESTAMP` is gone from `proxy_session.h`. "Supported" (corrected 2026-11-20; was a shim, every known
  build supported, until C1) = `VpExeBuildProductReady(id) && VpExeBuildSupported(id)`: the build is **product
  ready** (`vp_exe_build.h`; mirrored in `tools/exebuild/builds.py` `PRODUCT_READY` and the installer's
  `ProductReadyExeBuild`, `tools/exebuild/tests` keeps header and Python equal) and this binary's table for it is
  complete (`vp_exe_symbols.h`). Today only 4390913 is product ready: a real build-26577625 EXE takes the path below,
  named ("this game's EXE is Steam build 26577625, which this version of the 64-bit patch does not support yet", code
  `unsupported-build`, the report's [exe] line "NOT supported by this version of the patch yet"). The lead of epic 0590
  flips 26577625 in those three places when stage 2 (4963-4966) and 4967's live smokes have passed.
- **The developer switch `VP64_DEV_READY_BUILD`** (2026-11-20, for 4963-4967 before the flip): the environment
  variable `VP64_DEV_READY_BUILD=26577625` in the EXE's environment makes that known build count as product ready
  (still only with a complete table). Exact Steam build id only (`1`, `yes`, a trailing space or another build's id
  do nothing); no effect on a build already ready or on an unknown build. Every boot that uses it logs a LOUD line
  "known and supported: the 64-bit path starts (DEVELOPER switch VP64_DEV_READY_BUILD names this build ...)" and the
  report's [exe] line says "supported only through the DEVELOPER switch". Why it cannot reach a release: nothing of the
  patch sets it (not the installer, not vpproxy.ini or VP64Options.ini, not the helper); a Steam start of the game
  has no such variable; only a launcher that sets it on purpose does. **For the live smokes on c5h-new-a/b:** start the
  instance with the variable in the environment its launcher gives the EXE (how a given launcher passes extra
  variables was not checked by this ticket), and check vpproxy.log for the DEVELOPER line; without it the instance
  shows the 0588 popup and plays the 32-bit game. Tests: `session_unit.cpp` (the gate with and without the switch, its
  wrong values), `evidence/0588/gate_loader.py` D (not ready: 32-bit DLL and the named report), D2 (the switch: the
  64-bit boot), D3 (a wrong value: no effect).
- **Game EXE or test host.** The gate applies to a process whose file is `CivilizationV*.exe` (DX11, DX9, Tablet).
  Test and development hosts (the rt_tests `loader.exe`, stand-ins) keep the old behaviour, so the existing proxy
  suites are unchanged. The test switch `VPPROXY_TEST_EXE_BUILD=<stamp>,<SizeOfImage>` makes any process a game EXE with
  those values (logged in the report); it is the only way to simulate an unknown build (no EXE is ever edited).
- **The decision (`DecideBoot`, pure, unit-tested):** mode=off or failed=1: not asked. Known and supported: unchanged.
  Unknown, or known without a table: **the server is not started**, and by config.ini's form:
  - stock (every rule key on): the 32-bit DLL (`BootOff`, an ordinary stock-drawn VP game, the window-title notice),
    and on the first start of this build the D119 report (stage `exe-build`, code `unknown-build` or
    `unsupported-build`) and `vp64helper popup`. The build's line `0x<stamp> 0x<SizeOfImage>` is then appended to
    `<config.ini folder>\VP64\exe-build-notices.txt` (only when the helper started), and later starts on that build
    log the line and keep the title notice but show no popup;
  - A'' (a rule key off) or unknown (config.ini unreadable): **no play** (D125: the EXE read config.ini at its start,
    W3c.2 in `research/06-tooling-and-workflows/release-driver.md`, so restoring the file cannot give this process
    terrain): the report, `vp64helper failed --pid` (it waits for the EXE's end, restores config.ini through
    `cfgini::Off`, writes failed=1, shows its popup), then `Fatal(3)`. Every time, notice shown or not: the restore
    needs the helper. The next start is the normal 32-bit game (failed=1); "Try again next start" re-arms it.
- **Popup text.** The helper's own first line ("could not run ... normal (32-bit) game", or for `failed` "failed, so
  the game was ended ... config.ini is back"), then the proxy's summary line (`popup-summary.txt`): Civilization V
  was updated, the build found (Steam build or "a new build", stamp, SizeOfImage), the patch is off until a version
  that supports this build is installed, the game runs as the normal 32-bit VP game (A'': this start was ended because
  config.ini would have left the game without terrain), and where to look for an update,
  `https://github.com/LoneGazebo/Community-Patch-DLL/releases` (D260, ticket 0761, 2026-11-25; was this repository's
  own page, the installer's AppPublisherURL), then the helper's hand-in line. No helper file was changed.
- **Masks on such a build.** `ReadMasks` reads the EXE's own masks only on the real image of build 4390913 (its
  addresses). Otherwise: the test masks (tests), else in a game EXE config.ini's `[DisableSystems]` keys as a mask
  (`ConfigMaskOfIni`: non-zero value = bit clear; the EXE's defaults when the file or section is missing), source
  `config.ini`. The session line then names the build, e.g. "D33 session: config mask 0xFFEC3F67 ... (unsupported EXE
  build 0x12345678 (...): the config mask from config.ini's [DisableSystems])". A game EXE with no readable mask and no
  readable config.ini is "D33 session (assumed) ... fail loud", never "stock session (assumed)". (Since ticket 4963 the
  EXE's own masks are read on every supported build through the table; see exe-build-table.md "proxy carry".)
- **Installer, a known build not product ready** (2026-11-20): a warning naming the build (the patch switches itself
  off at the start until a version supports it), Yes installs, No stops (exit 7 with the link); a silent install
  installs and logs the warning.
- **Installer.** `patch64.iss` refuses an install (exit 7, the build's stamp and SizeOfImage named, the patch's page as
  the link) when the game folder's `CivilizationV_DX11.exe` is not one of the registry's builds. Its list is a copy of
  `VPEXE_KNOWN_BUILDS`; since ticket 4963 `tools/harness/tests/test_installer.py` `KnownBuildListTests` fails when it,
  `tools/exebuild/builds.py` and the header differ. A removal does not check. In test mode (/P64TEST=1) a fixture that is not
  a 32-bit PE is accepted with a log line (the hermetic suites' fixture EXE is a text file).
- **Tests (2026-11-19):** `proxy/tools/rt_tests/session_unit.cpp` 130 checks (registry reads of fake images incl. a
  header in a NOACCESS page, the switch, config.ini parsing, the notice record and its write failures, every
  `DecideBoot` branch with exit codes, also under ASan); `test_proxy_session.py` 154/154 and `test_proxy_rt.py` 199/199
  (the loader cases unchanged); the built proxy in `loader.exe` with the test switch, `evidence/0588/gate_loader.py`
  10/10 (unknown + stock: 32-bit DLL, one report, popup, no second one on the next start; unknown + A'': exit 3,
  `vp64helper failed`; 26577625 by the switch: the 64-bit boot); the installer: the six 0588 cases 6/6 (since 4963 in
  `tools/harness/tests/test_installer.py`, `test_0588_*`) and `test_installer.py` 25/25. Exit lines and the build used: `evidence/0588/acceptance.txt`.
- **Not verified:** no live run on a Goldberg instance (the switch on build 4390913); the windowed popups were not
  looked at (the helper's `--no-ui` form is what the suites see).

### B.5 Showstoppers (question 5)

None found [C for the absence of the mechanisms listed in B.3; I for "none at all"]. All of the patch's mechanisms
exist in the new build; there is no new protection; nothing ties the patch to the pre-update EXE. The losses are ones
VP shares: the Lua sandbox for EUI and CPK, and VP's own EXE table.

### Not verified

- No live run of the new build: all of part B is static. The EUI include failing, the Lua channel failing and the
  patch's behaviour on the unknown EXE are read from code.
  *Partly verified 2026-11-19 23:04 EST (epic 0590 C3, `06-tooling-and-workflows/headless-and-parallel-testing.md`
  3.8b) [L]:* the new EXE runs as a Goldberg copy (instance c5h-new-a: gbe's `steam_api.dll`, started directly by the
  harness, one process from launch to stop, no exit, no Steam relaunch) and played 8 AI turns of a VP Duel game on
  `i0ship-e96457fd-ia`; the Lua channel fails exactly as read (`=VP_LUAEXEC:127: attempt to call upvalue 'loadstring'
  (a nil value)`); EUI's `loadstring` include did not run in the mods form (CP's own `InfoTooltipInclude.lua` replaces
  it), so no EUI error appeared there. Still not verified: that it starts **without** the Steam client (Steam was
  running, as always on the workstation; the project does not close it), and the patch on this EXE.
- The carried addresses are checked by bytes, not in a live process; each needs its feature's live smoke.
- oldpc's and the laptop's own Steam installs were still on build 4390913 at 21:30 (they update only on launch); the
  DX9 texture pack's change was not examined.
- No 32-bit DLL or proxy was loaded into the new EXE.

## Part C: live smokes (ticket 4967, epic 0590; part 1 2026-11-20, W4-0590) [L where marked]

Part 1 runs on team branch `epic-0590-new-exe-migration` at d012115ec (stage 1: the build registry, 0588 and the
product-ready gate; the proxy carry 4963; the tools 0593; the `-ia` channel 4968), before the shell and render carries
(4964-4966). So on build 26577625 only the 0588 gate is smoked here; the 64-bit path itself is part 2's.

### C.1 Builds (step A)

Branch `task-0590-4967-live-smokes` (d012115ec merged). DLL source: `git archive` of the `deps/dll.lock` pin
1fd51353e into `C:/c5-64bit-work/4967/src`. Commands (each through `quietlock.py run --who 0590`, driver
`C:/c5-64bit-work/4967/a_build.sh`):

    python -I server/build_server.py --config release --src C:/c5-64bit-work/4967/src --out C:/c5-64bit-work/4967/server-build-rel --offline
    C5_SHELL_BUILD=C:\c5-64bit-work\4967\shell-build cmd.exe /c shell\build.cmd

Results [C, logs; 2026-11-20 02:44-03:09 EST]: server-release exit 0 (`build_server: OK in 1123 s`; build_all `dll`
588 s), proxy `CvGameCore_Expansion2.dll` SHA-256 `06bbdbd5...d1d2` (1,361,408 B, version line `repo fe205e8cb dirty=0
dll_pin 1fd51353e src copy config release`), `vp64helper.exe` `2f3d7c90...bb83`, server.exe `8299feb7...78ee`; shell
build exit 0 (`BUILD OK`), `shell_composite.dll` `0696cefd...4a92`. The emitter took the shared `exe_tables` with no
refusal (the `gen_table.py` commands were not needed). Logs `C:/c5-64bit-work/4967/logs/a-*.log`.

### C.2 The 0588 gate and popup in a real window, build 26577625 (step B)

Instance c5h-new-b (F:, build 26577625, test DLL `+ia4968` `ff0f6e86...` as the fallback 32-bit DLL), the layout of
C.1 by `provision.py set-proxy`, harness launch (hidden desktop, cloak), no developer switch unless named.

Expected per run (B.4 "0588 behaviour as built"): stock config.ini: the 32-bit DLL, title notice, one popup naming
build 26577625, the report folder (stage `exe-build`, code `unsupported-build`), the game plays; A'' config.ini: no
play, report, `vp64helper failed`, exit 3; a second stock start: no popup; with `VP64_DEV_READY_BUILD=26577625`: the
DEVELOPER LOUD line, the 64-bit path starts, the composite's camera validation refuses the build (until 4964/4966).

How [L, 2026-11-20 03:08-03:46 EST, workstation, c5h-new-b, cloak on the hidden desktop, Steam client running]:
`research/02-exe-internals/evidence/4967/lay.py prep c5h-new-b --build C:/c5-64bit-work/4967/server-build-rel --work
C:/c5-64bit-work/4967/b` (SHA-256 backup of the standard files, VP64Options.ini and the helper's side files; `set-proxy`
with the default vpproxy.ini, `mode=on`, no pack; `vp64helper.exe` beside the proxy); a new Tiny game each launch
(`tools/d10/evidence/4968/s03_prepare.py`); `cli.py -i c5h-new-b launch [--env VP64_DEV_READY_BUILD=26577625]`. The
patch's config.ini form: `lay.py helper ... on` = `vp64helper on --config --options --vp64-options`, as the installer
runs it. The composite was added (`lay.py composite`) only for the developer-switch run.

| Run | Result |
|---|---|
| B1: stock config.ini (hash `1521591b...`), first start on this build | **pass.** vpproxy.log: `boot: FALLBACK to the 32-bit DLL ... exe build: build 26577625 (stamp 0x6AF3837D, SizeOfImage 0x063A2000): a build this version of the 64-bit patch does not support yet; config.ini stock: the server is not started ...; the first start on this build: the report and vp64helper popup`; the window-title notice ` - VP64 64-bit patch OFF: ...`; `report: ... stage exe-build, code unsupported-build`. Report folder `<user>\VP64\reports\20261120-031016\`: `report.txt` (stage `exe-build`, code `unsupported-build`, [exe] "NOT supported by this version of the patch yet"), `popup-summary.txt`, `vpproxy.ini`, `vpproxy.log.tail.txt`; `<user>\VP64\exe-build-notices.txt` = `0x6AF3837D 0x063A2000`. Popup (dialog watcher, `run\dialog_open.json`, kind `patch`, cloaked, not seen by the owner): "The 64-bit part of the patch could not run. This game goes on as the normal (32-bit) game. Civilization V was updated: this game's EXE is Steam build 26577625 ... does not support yet ...", buttons Open folder / Close; picture `vc-20261120-031154-workstation-4f69`. Game on the 32-bit DLL `+ia4968`: 3 turns 0 -> 3 (9.7, 5.1, 9.8 s) through `lua.py` only (`evidence/4967/lua_turns.py`, exit 0), autosaves each turn. `cli.py stop` exit 7 (the helper an orphan, as in 0473's run 1); then `cli.py dismiss --button Close --patch-popup` exit 0 (cloaked, not seen by the owner), marker cleared. |
| Harness waits with the popup open | `frontend.py dismiss` exit 1 and the harness waits end at once (exit 8) on the open patch popup: the watcher as designed (D155). The game itself is not blocked: the popup belongs to `vp64helper.exe`, and `lua.py` drives the game. **A part-2 driver on a refused build must drive by `lua.py` or close the popup first.** |
| B3: second stock start (03:19) | **pass.** `boot: FALLBACK ... the notice for this build was shown before: no report, no popup`; no new report folder; no box in 90 s; the title notice kept. Stopped exit 0. |
| B2: A'' config.ini (`vp64helper on`: Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons = 1; hash `4a40b8bb...`) | **pass.** `boot: NO PLAY: exe build: build 26577625 ...; config.ini A'': no play (D125 ...)`, `report: written to ...\VP64\reports\20261120-032529`, `helper: started "...vp64helper.exe" failed --pid ...`, `FATAL: the EXE build is not supported and config.ini is in the A'' form (no play, D125, ticket 0588): the process ends with exit code 3`. EXE exit code 3 (dialog watcher tail). config.ini back to `1521591b...` (its hash before `on`), vpproxy.ini `failed=1`. Report: stage `exe-build`, code `unsupported-build`, files `report.txt`, `popup-summary.txt`, `helper.log`, `VP64Options.ini`, `vpproxy.ini`, `vpproxy.log.tail.txt`. Popup: "The 64-bit part of the patch failed, so the game was ended. Your config.ini is back to your own settings ... This start was ended because the patch's settings in config.ini would have left the game without terrain ...", buttons Open folder / Try again next start / Close; picture `vc-20261120-032647-workstation-371d`; closed by the owner's path, exit 0. The popup shows on every A'' start (the notice record does not suppress it), as B.4 says. |
| Developer switch: `--env VP64_DEV_READY_BUILD=26577625`, A'' config.ini (`on` again), composite added (03:29) | **Not as expected: a loud refusal but no end.** vpproxy.log: `boot: exe build: build 26577625 ...: known and supported: the 64-bit path starts (DEVELOPER switch VP64_DEV_READY_BUILD names this build ...)`; the server started; queue guard active, view mask and white mask active, uidraw "EXE checks passed (Steam build 26577625, 7 code patterns, 22 cross-checks)" (the proxy carry works live on the new build); farm guard off ("not an EXE build the guard's patterns describe": `GuardKnowsBuild`, 4964/4965). Server: `render: LOUD: renderer FAILED (RENDERER): terrain renderer: NotFound: TerrainShader blob <old-build VA> not in the .shader section; the game goes on without the server's terrain` (the shader blob addresses are 4966's). Composite: `camera: REFUSED: ... TimeDateStamp 0x6AF3837D is not 0x546CD0A8`, then every tap REFUSED (water, overlay, decal, shadow, ice, cut; overlay and shadow say "the server ends the game through D125's route"), unit and model filters path 2 skipped. **But the game did not end:** it reached the map, played turn 0 -> 1, and showed a grey map with units and no terrain (D33 hides the EXE's terrain; picture `vc-20261120-033641-workstation-a04f`, diagnostic). No box, no crash; stopped exit 0. Cause [C, code read]: the server's renderer had already gone to `VPFS_FAILED` at init (`srv_render_host.cpp` `Fail`: "the game goes on without the server's terrain"), so the scene's pre-check that turns a tap's `VPOVL_FAILED` into D125's end never runs. Reported to lead-0590 (D118: no degraded fallback); after 4964-4966 the init failure should not happen on this build, but a renderer init failure on any build still leaves a terrain-less game [I]. |

#### C.2a A renderer init failure leaves a grey, terrain-less game (ticket 0600; developer-switch run of C.2)

Evidence for ticket 0600 (MC, p8: a renderer init failure must end in the popup and the report, never a grey map).

**Setup [L, 2026-11-20 03:29-03:44 EST].** Workstation, instance c5h-new-b (F:, Goldberg copy of Steam build
26577625, stamp 0x6AF3837D, SizeOfImage 0x063A2000). 32-bit fallback DLL `+ia4968` `ff0f6e86...`; x64 DLL of the pin
1fd51353e (`6a040b70...`), server.exe `8299feb7...`, proxy `06bbdbd5...`, composite `0696cefd...` (team branch
d012115ec, release, C.1; before 4964-4966). config.ini in the A'' form written by `vp64helper on` (Decal, Forest, FOW,
FluidFOW, DistanceFog, YieldIcons = 1); VP64Options.ini `[Renderer] ServerRenderer = 1`; vpproxy.ini `mode=on`, no
pack. A new Tiny game (`tools/d10/evidence/4968/s03_prepare.py`), then
`python -I -X utf8 tools/harness/cli.py -i c5h-new-b launch --env VP64_DEV_READY_BUILD=26577625` (cloak, hidden desktop).
Run folder `Logs\VP64\run\20261120-033106-60308-1\`; copies in `C:/c5-64bit-work/4967/b/bdev*`.

**Log lines in order [L]** (proxy = vpproxy.log, seconds since the proxy's start; server = server.log; render =
render.log, clock times; composite = `Logs\shell_composite.log`):
1. proxy 2.16: `boot: exe build: build 26577625 (...): known and supported: the 64-bit path starts (DEVELOPER switch
   VP64_DEV_READY_BUILD names this build, which the product does not support yet: treated as ready; never in a
   player's install)`; the server is started.
2. composite 03:31:08.650: `camera: REFUSED: image at 0x008C0000: TimeDateStamp 0x6AF3837D is not 0x546CD0A8 (not
   the Steam DX11 EXE this reader knows) - the camera is never read in this process`.
3. render 03:31:08.628 `state NONE -> STARTING`; device created 03:31:09.156; then 03:31:10.036-10.148 (server.log
   2.586-2.698): `LOUD: terrain: no distance fog and no spec ... NotFound: TerrainShader blob 0x067d6490 not in the
   .shader section` (an address of build 4390913's table; this build's blob addresses come with 4966), `LOUD: terrain:
   renderer init with '...CivilizationV_DX11.exe' failed: ...`, **`LOUD: renderer FAILED (RENDERER): terrain renderer:
   NotFound: TerrainShader blob 0x067d6490 not in the .shader section; the game goes on without the server's
   terrain`**, `state STARTING -> FAILED reason RENDERER`.
4. composite 03:31:33.15-33.17 (after the load): every tap refuses, e.g. `overlay_tap: LOUD: FAILED PATTERN at the
   install: EXE not validated - no overlay for this process; the server ends the game through D125's route (decision
   5)`, `shadow_tap: LOUD: FAILED PATTERN at the install: ... the server ends the game through D125's route`,
   `decal_tap: LOUD: FAILED PATTERN ...`, `cut_tap: LOUD: FAILED (1): refused ...`, water and ice `FAILED ... DISABLED for
   the life of the process`; `unit_filter: path 2 not run`.
5. **No line of the scene's pre-check**: render.log ends at line 3's `state STARTING -> FAILED`; neither render.log nor
   server.log has `overlay: source state ...` or `LOUD: overlay tap FAILED (VPOVL_FAILED ...)` (the scene's
   `overlayPoll`), nor any fatal-route line, until the stop: server.log 724.444 `remote: the EXE (pid 60308) ended with
   exit code 0x0000000F (15)` (the harness's terminate) and `result: FAILED: the engine is gone: channel 2 serve()
   returned PeerDied`.

**What the player saw [L].** The game reached the map (load screen closed by `frontend.py dismiss`, `IN GAME`), a grey
map with units, unit flags and the interface and no terrain (D33 has the EXE's terrain off; the server draws none):
Visual Checks `vc-20261120-033641-workstation-a04f` (DIAGNOSTIC, harness PrintWindow shot at turn 0). Turn 0 -> 1
played (`turns.py end`, then `Game.GetGameTurn()` = 1, human turn active). No box, no popup, no report folder for this
start; stopped by `cli.py stop` (exit 0) at 03:43.

**Code reading [C, this branch, file read 2026-11-20].**
- `server/src/render/srv_render_host.cpp` `Fail(Host&, reason, text)` (line 306) writes the LOUD line "...; the game
  goes on without the server's terrain" and `SetState(h, VPFS_FAILED, reason)`.
- `ThreadProc` (from line 1430): after `RunSeh(h)` returns with `h.state == VPFS_FAILED` it releases the device,
  finishes the recorder and `WaitForSingleObject(h.stop, INFINITE)` with the comment "FAILED: no heartbeat, the game
  goes on; wait for the orderly stop". Nothing ends the server or the game.
- The D125 ends of the taps are steps of the render loop: `OverlayFatalStep`, `FowFatalStep`, `DecalFatalStep` and the
  shadow/ice step (lines 1223-1230, after `h.scene.poll()`), which read the scene's `overlayPoll` result
  (`srv_render_scene.cpp` `Scene::overlayPoll`, the `VPOVL_FAILED` -> "overlay tap FAILED" text at line 1547) and call
  `HostConfig::fatal`. With the renderer failed at init the loop never runs, so these steps never run either, whatever
  the composite publishes.
- So any renderer init failure (here a build whose shader blobs are not in the table) ends in a grey map in a D33
  session: D118 is not met on this path [C for the path; L for this one case]. [I] The same holds on build 4390913 for
  any other init failure (no device, an install whose shader section does not match); not tried.

#### Not verified in B

Not verified in B: the windowed popups seen by a person (they are cloaked on the hidden desktop; text read by the
watcher); "Try again next start"; the modpack form.

### C.3 The old-build regression smoke of the proxy carry (step C)

Instance c5h-a (build 4390913), the C.1 layout and the composite, a new Standard game, 10 turns, pacing options as 0.1b
ships them (D197: `[Performance] AiSkipVisualDrain = 1`, `AiDrainUnordered = 1`, `AiUpdateTickMs = 0`), compared with
a run of main's pinned layout (`deps/dll.lock` `release`) on the same instance and save: no new refusal is the pass.

How [L, 2026-11-20 03:51-04:30 EST, workstation, c5h-a (C:, Goldberg, build 4390913), cloak on the hidden desktop]:
each arm `lay.py prep c5h-a --build B --composite-dll D [--composite-ini I] --work W`, `lay.py helper c5h-a on`
(config.ini A'', VP64Options.ini `ServerRenderer = 1`), `evidence/4967/c_settings.py c5h-a` ([Performance] as above;
vpproxy.ini `leader_scenes=1`, **not** the shipped default, which has no key = off, so that the leader-scene code runs
its EXE checks), `cli.py -i c5h-a launch`, `evidence/4967/c_drive.py` (map reached, a Lua `Team:Meet` with player 1,
whose first-contact greeting opens the leader screen, shot, Esc from the harness, then 10 turns by `turns.py end` +
`wait`), end shot with no popup open (D114), `cli.py stop` with the human-turn hook, logs copied, `lay.py restore`.
No pack in either arm (the default `set-proxy` ini; the shipped installer has `pack=1` and a manifest). The 32-bit DLL
of c5h-a (fallback only) is its own; the x64 DLL is the pin 1fd51353e in both arms.

| Arm | Layout | Game | Result |
|---|---|---|---|
| team (03:53-04:04) | C.1: proxy `06bbdbd5`, server `8299feb7`, composite `0696cefd` | a new Standard game (Continents, 7 AIs, Prince; `evidence/4967/c_prepare.py new`), its turn-0 autosave kept as `C5H-4967-STD_0000` (`30f3d821...`) | in game 122 s after launch; leader screen of Bismarck up (`vc-20261120-035909-workstation-cee5`) and closed (`UI.GetLeaderHeadRootUp()` false); turns 0 -> 10 (5.4-8.8 s each); end shot `vc-20261120-040333-workstation-0ed6`; stop exit 0, no orphan, no box; restore ok |
| main (04:14-04:21) | `F:/c5-64bit-work/0551/layouts/x64-1fd51353-p1e09fc7` (proxy `5bab1b6c`, server `a6026d53`, the `release` row) + the composite of 1e09fc7 built from a `git archive` (`48caf877`) with its own shell_composite.ini | the team arm's `C5H-4967-STD_0000` loaded | the same: leader screen (`vc-20261120-041759-workstation-0abe`), turns 0 -> 10, end shot `vc-20261120-042046-workstation-4e4d` (the same map state), stop exit 0; restore ok, the save copy removed |

One line per feature group, team arm (vpproxy.log `[s]`, composite log clock time) [L]; the main arm logs the same
line with the same addresses unless said:

| Feature | Evidence on the table-driven code (team arm) |
|---|---|
| session masks (`ReadExeMasks`) | no own line on a successful boot (the session line is written only into a failure report); the session was taken as D33 (the server ran, no fail-loud) and the view mask read config `0xffec3f67` [I for ReadExeMasks itself] |
| view mask | `[2.30] shell: view mask: active: exe=Steam build 4390913 (2014-11-19 EXE, with CEG) base=0x001c0000 ... Terrain (0x1) cleared` (main: `exe=DX11 timestamp=0x546cd0a8`) |
| white mask, model fog | `[2.30] shell: white mask: checks passed (EXE base 0x001c0000, loader 0x00686540, mask global 0x00b612ac, config 0xffec3f67 ...)`; `model fog on (ticket 0339): the cloud base layer is loaded too`; composite `fow_models: INSTALLED` |
| queue guard | `[2.29] queue guard: UI message queue guard active (DX11 build; slot 0x0079647C, channels 0x017CF080 ...)` (main: the same slot and channels) |
| uidraw | `[55.64] uidraw: EXE checks passed (Steam build 4390913 ..., 7 code patterns, 22 cross-checks; #13406's DX11 layout)`, then `grown ... 4194304 B, 16384 commands` (main: "time stamp, 7 code patterns, 22 cross-checks") |
| pacing options 2, 3, 4 | `[58.31] pacing: on: [Performance] AiUpdateTickMs = 0: ... a copy of the EXE's vftable 0x007aa718 (264 entries and the RTTI slot, checked) ... slot 102 (getSecsPerTurn 0x004c4b60) answers 0.000 s`; `pacing: option 2 on: [Performance] AiSkipVisualDrain = 1 ...`; `pacing: option 4 on: [Performance] AiDrainUnordered = 1 ...` |
| leader scenes | `[58.31] leaderscene: on: vpproxy.ini leader_scenes = 1 (on demand); the EXE's scene cache at 0x00b627a4` (main: the same address); a leader screen opened and closed as above (a major below slot 22: the EXE's own scene, nothing for the proxy to add) |
| farm guard | `[2.29] farm_guard: 13 byte patterns match; the guard watches type 59's listener list (builder 0x00508FC0, object 0x009620D0, graph 0x00962228)`, `installed by the proxy` at 48.6 s, in-game checks "sound" |
| camera validation | `03:55:15.866 camera: EXE image at 0x001C0000: TimeDateStamp 0x546CD0A8 and 11 byte patterns match - camera read enabled` |
| taps | bake (`tap: READY load 3 copy 1: map 80x56 plots ...`, `RELEASED ... ack`), water (29 patterns), overlay (8), decal (52), shadow (20), ice (21) `EXE patterns match - tap enabled`; cut `READY generation 1..10`; forest (render.log `forest ... state=ACTIVE`); fow models `INSTALLED`; unit filter `path 2: 12 of 12 programs resolved from the EXE's renderer` |

Comparison (`evidence/4967/c_features.py <team> <main>`, refusal-like lines normalised): LOUD lines 0 in vpproxy.log,
server.log, render.log and shell_composite.log of both arms. Lines only in the team arm: `render-events: the DLL has no
DllGetRenderEventsVersion export ... extras that need them stay off (ticket 4932)` (a server check newer than 1e09fc7,
same DLL in both arms), `ui bridge: frame Lua hook set (the bridge is off ...)` and the model filter's periodic status
lines (composite features newer than 1e09fc7); the rest on either side are periodic status lines (terrain, forest,
shadow states) whose values differ with timing. **No new refusal: pass** [L].

Not verified in C: the installer-installed layout (pack, manifest, the shipped vpproxy.ini) and the modpack form; the
leader-scene proxy path proper (slot 22 and up, a 43-civ game); the farm guard's composite side.

### C.4 Part 2: the smoke set on both builds with every carry merged (ticket 4967 part 2, W4-0590, 2026-11-20)

Team branch `epic-0590-new-exe-migration` at 9ac15091e (every carry: proxy 4963, shell A and B 4964/4965, render and
server 4966, tools 0593, `-ia` channel 4968; main 0e26e22de), merged into `task-0590-4967-live-smokes`.

**C.4.1 Builds.** As C.1 into `C:/c5-64bit-work/4967/server-build-rel2` and `shell-build2` (driver `a_build.sh`, the same
DLL source copy of 1fd51353e). Local tables: the shared `C:/c5-64bit-work/exe-builds` (392 symbols, complete on both
builds), not regenerated. Results [C, logs, 2026-11-20]: server-release exit 0 (04:41-04:57, `build_server: OK in 942 s`;
x64 DLL `ee45ec65...`, server.exe `da240792...`, the full build's proxy `e9f3e3c2...`); shell exit 0 (04:57-05:01,
`BUILD OK`, `shell_composite.dll` `a36e8507...`). Then the team branch took c9b4046fd ("the proxy's farm guard owner
accepts every known EXE build": `GuardKnowsBuild` was still `build == 4390913` on 9ac15091e, the TODO of 4964/4965;
read in the code before the run and reported to the lead), merged here, and **the proxy alone** was rebuilt:
`python -I proxy/build_proxy.py --out .../server-build-rel2/proxy-c9b4 --src .../src --gen .../server-build-rel2/gen
--config release` through `quietlock.py run` (05:02-05:03, `build_proxy: OK in 61 s`), proxy `608f5918...`
(1,395,712 B), helper `72d4d9e1...`. **Every row of C.4.2-C.4.4 below is taken with proxy `608f5918`**, server
`da240792` and composite `a36e8507` (the layout folder `C:/c5-64bit-work/4967/layout2`), except the main arm.

**C.4.2 Build 26577625 with the developer switch (c5h-new-a).** Layout of C.4.1 with the composite, `vp64helper on`,
[Performance] as 0.1b (D197), `leader_scenes=1`, `cli.py launch --env VP64_DEV_READY_BUILD=26577625`, a new Standard
game, leader screen, 10 turns, the turn-10 autosave loaded again and 3 more turns. Its turn-0 save is the save of the
build-4390913 arms (C.4.4), so the world pictures compare the same game.

Run [L, 2026-11-20 05:04-05:17 EST, workstation, c5h-new-a (F:, Goldberg, build 26577625), 32-bit fallback DLL
`+ia4968` `ff0f6e86...`, x64 DLL `ee45ec65...` of the pin 1fd51353e; cloak on the hidden desktop; Steam client
running; the lead's stage-2 suite and W3's shell suites ran beside it]:
`lay.py prep c5h-new-a --build C:/c5-64bit-work/4967/layout2 --composite-dll .../shell-build2/bin/shell_composite.dll`,
`lay.py helper ... on`, `c_settings.py`, `c_prepare.py c5h-new-a new`,
`cli.py -i c5h-new-a launch --env VP64_DEV_READY_BUILD=26577625` (exit 0), `c_drive.py c5h-new-a --turns 10` (exit 0:
in game 99 s after the drive started, leader screen, turns 0 -> 10 at 5.2-7.8 s each). End shot, `cli.py stop` exit 0
(no orphan, no box). Saves kept: the turn-0 autosave as `C5H-4967-P2-STD_0000` (`6d33a6da...`) and the turn-10
autosave as `C5H-4967-P2-STD_0009` (`0ac0d668...`). Then the save and load: `c_prepare.py c5h-new-a load
C5H-4967-P2-STD_0009`, the same launch, `c_drive.py --turns 3 --no-leader` (exit 0, turns 10 -> 13), stop exit 0.
LOUD lines: **0** in vpproxy.log, server.log, render.log and shell_composite.log of both launches. No box, no crash.
Restore: `lay.py restore` ok (DLL `ff0f6e86...` again, verify --quick ok).

| Feature group | Build 26577625, the proof (log; the new build's addresses left out, D202) |
|---|---|
| boot | proxy `boot: exe build: build 26577625 (...): known and supported: the 64-bit path starts (DEVELOPER switch VP64_DEV_READY_BUILD names this build ...)` |
| camera validation (exe_camera.h) | composite `camera: EXE image at <base>: Steam build 26577625 (Firaxis' 2026-11-19 update, no CEG), 11 byte patterns match - camera read enabled`; status `INCONSISTENT -> OK` at the map (05:06:37), `first OK read ... viewport 2554x1411` |
| server renderer init (shader section of the running build) | render.log `terrain scene: EXE '...CivilizationV_DX11.exe' (shaders read in 394.4 ms ...) ... distance fog 1, spec map 1`, `state STARTING -> READY` 3.0 s after start; no LOUD. (The renderer logs no count of create sites; the 1,417-row array is the table's, and READY with fog ramp and spec map found means the blobs it names were found in this EXE's `.shader` section) [L for READY, I for the count]. Part 1's `TerrainShader blob ... not in the .shader section` is gone. |
| terrain | render.log `terrain: state ... NO_FIELD -> OK (camera status OK ...)` at 05:06:38; drawn: pictures below |
| bake tap | composite `tap: READY load 3 copy 1: map 80x56 plots (samples 5120x3584 ...)`, then `RELEASED ... ack` |
| water tap | `water_tap: EXE patterns match (29) - tap enabled`, `READY load 3 copy 1: 25 records ...` |
| ice tap | `ice_tap: EXE patterns match (21) - tap enabled`; render.log `ice: generation 1 (load 3) applied ... 190285 triangles` |
| overlay tap | `overlay_tap: EXE patterns match (8) - tap enabled`; render.log `overlay: source state WAITING -> ACTIVE` |
| shadow tap | `shadow_tap: EXE patterns match (20) - tap enabled` |
| decal tap | `decal_tap: EXE patterns match (45) on Steam build 26577625 ... - tap enabled`, `READY generation 1 (load 3, first copy): 108 decals ...` (the head checks 45 patterns on both builds, C.4.4; main's composite of 1e09fc7 checks 52: the row set changed with the carry, not by build) |
| cut tap | `cut_tap: READY generation 1 (load 3): 2560 x 1792 cells (160 x 112 tiles)` and later generations |
| forest | render.log `forest 1s: state=ACTIVE drawn=10 visible=14 built=14 ...` |
| fog models | `fow_models: EXE patterns match (7) on Steam build 26577625 ...`, `INSTALLED` |
| unit filter, model filter (cities, landmarks) | `unit_filter: path 2: 12 of 12 programs resolved from the EXE's renderer`; `model_filter: path 2: 10 of 10 programs resolved`, `filterState2 0x10001 (cities: can drop; landmarks: can drop)`. With `ServerCities`/`ServerLandmarks` at their default 0 the EXE draws cities and landmarks (nothing dropped, `drop_bits=0x0`); the server's own cities and landmarks were not switched on in this smoke |
| farm guard, proxy side | `farm_guard: 13 byte patterns match (Steam build 26577625 (...)); the guard watches type 59's listener list ...`, `installed by the proxy: the builder's listener node 4 ... now calls the guard`, in game `the check at this Present: sound` |
| farm guard, composite side | `farm_guard: the proxy owns the guard in this process (ticket 0535 ...): this composite installs nothing and hands its Presents to it` (the composite's own patterns are not run while the proxy owns the guard) |
| queue guard | `queue guard: UI message queue guard active (DX11 build; slot ..., channels ..., index 0; scratch 64 KB)` |
| uidraw | `uidraw: EXE checks passed (Steam build 26577625 (...), 7 code patterns, 22 cross-checks; #13406's DX11 layout)`, `grown: ... 4194304 B, 16384 commands` |
| pacing options 2, 3, 4 (D197) | `pacing: on: [Performance] AiUpdateTickMs = 0: the engine utility ... uses a copy of the EXE's vftable (264 entries and the RTTI slot, checked) ...`; `pacing: option 2 on: [Performance] AiSkipVisualDrain = 1 ...`; `pacing: option 4 on: [Performance] AiDrainUnordered = 1 ...` |
| leader scenes | `leaderscene: on: vpproxy.ini leader_scenes = 1 (on demand); the EXE's scene cache at ...`; a leader screen opened through the channel (first contact after `Team:Meet`) and closed with Esc (`UI.GetLeaderHeadRootUp()` false): `vc-20261120-050731-workstation-e7af` |
| view mask, white mask, model fog | `shell: view mask: active: exe=Steam build 26577625 ... Terrain (0x1) cleared (the composite is loaded)`; `shell: white mask: checks passed (...)`; `model fog on (ticket 0339): the cloud base layer is loaded too` |
| live terrain (0465) | not on the head: no option of 0465 in `options/src/vp64_options.cpp` at 9ac15091e (`[Extras]` has only the two switches); not tested |
| save, load, 3 turns | the turn-10 autosave loaded, all of the above logged again (camera, `state STARTING -> READY`, farm guard 13 patterns), turns 10 -> 13, 0 LOUD |

Pictures (DIAGNOSTIC: developer switch, no pack, `leader_scenes=1`; not the shipped installer layout), popups closed:
turn 10 `vc-20261120-051002-workstation-5386` (the server's terrain, water, rivers, forest, ice and fog under the EXE's
units and UI, the same look as build 4390913's arms); turn 13 after the load `vc-20261120-051559-workstation-e4cb`.
The same save on build 4390913: C.4.4.

**C.4.3 The 0588 path on the head (c5h-new-b, no switch, stock config.ini).** [L, 05:17-05:24 EST] The same layout
(proxy `608f5918`, composite), stock config.ini, a new Tiny game, `cli.py -i c5h-new-b launch` (no `--env`).
vpproxy.log: `boot: FALLBACK to the 32-bit DLL ...: exe build: build 26577625 (...): a build this version of the 64-bit
patch does not support yet; config.ini stock: the server is not started ...`, `report: the 64-bit path failed: stage
exe-build, code unsupported-build; stock session ...`, `report: written to ...\VP64\reports\20261120-051953`. The
popup (first start of this build on the instance: part 1's notice record had been moved out at its restore) with the
same text as C.2 B1 (`vc-20261120-052034-workstation-a771`). The 32-bit game played turns 0 -> 3 through `lua.py`
(`lua_turns.py`); at turn 2 an AI's first-contact greeting held the channel (lua timeout, "the channel is dead ...
while a leader screen holds CvGame::update"), closed with Esc from the harness, turn 3 reached. Stop exit 7 (the
helper orphan, as in part 1), popup closed by the owner's path (exit 0, cloaked, not seen by the owner), marker cleared,
restore ok. **Pass: still the popup and the 32-bit game.**

**C.4.4 Build 4390913 (c5h-a): the head's layout against main's pinned layout on the same save.** [L, 2026-11-20
05:25-05:40 EST, c5h-a (C:, Goldberg, build 4390913), its own 32-bit DLL as fallback, x64 DLL as the arm's layout;
cloak on the hidden desktop.] Driver `C:/c5-64bit-work/4967/c2_arm.sh ARM BUILD COMPOSITE [INI]` and `c2_end.sh ARM`
(the part-1 C steps: `lay.py prep`/`helper on`/`c_settings.py`, the save copy, `c_prepare.py load`, launch, `c_drive.py
--turns 10`, end shot, stop with the human-turn hook, logs, save copy removed, `lay.py restore`). Both arms load
`C5H-4967-P2-STD_0000` (`6d33a6da...`), the turn-0 save made on build 26577625 in C.4.2, so all three turn-10 pictures
show the same game and camera.

| Arm | Layout | Result |
|---|---|---|
| head (05:25-05:32) | `layout2`: proxy `608f5918`, server `da240792`, composite `a36e8507` | launch exit 0, in game, leader screen opened and closed, turns 0 -> 10, end shot `vc-20261120-053102-workstation-8e68`, stop exit 0 (no orphan), restore ok |
| main (05:32-05:40) | `x64-1fd51353-p1e09fc7` (proxy `5bab1b6c`, server `a6026d53`) + composite of 1e09fc7 (`48caf877`, its own ini) | the same; end shot `vc-20261120-053845-workstation-d9f6` |

LOUD lines: **0** in the four logs of both arms. Every group alive on the head's table-driven code, each line as on
build 26577625 (C.4.2) with this build's name: boot `exe build: build 4390913 ...: known and supported: the 64-bit path
starts` (no switch); camera `11 byte patterns match - camera read enabled`; renderer `state STARTING -> READY`; water
(29), overlay (8), decal (45), shadow (20), ice (21) `tap enabled`; fow models `EXE patterns match (7)`; unit filter
12/12 and model filter 10/10 programs; forest `state=ACTIVE`; farm guard `13 byte patterns match (Steam build
4390913 ...)` (proxy) and "the proxy owns the guard" (composite); queue guard; uidraw `22 cross-checks`; pacing `on`
(option 3), `option 2 on`, `option 4 on`; leader scenes `on`; white mask `checks passed`. The head's addresses of
build 4390913 equal main's constants where both log one (queue guard slot, white-mask globals, scene cache, pacing
vftable), as in part 1.

Comparison (`c_features.py <head> <main>`): lines only in the head's arm are the same five as part 1 C (the server's
`render-events` note of ticket 4932, `ui bridge: frame Lua hook set`, the model filter's status lines, one periodic
terrain status line); **none only in main's arm. No new refusal: pass** [L].

Pictures of the same game at turn 10 (DIAGNOSTIC; the same asset set, the instances' Vibrant Textures, D94): build
26577625 head `vc-20261120-051002-workstation-5386`, build 4390913 head `vc-20261120-053102-workstation-8e68`, build
4390913 main `vc-20261120-053845-workstation-d9f6`. Difference of the two builds' head shots
(`evidence/4967/pic_diff.py`, half-size PrintWindow shots, max channel difference): median 8, p95 39, max 207, 74.1 %
of pixels within 16 (`vc-20261120-053330-workstation-e5e7`, x4): the terrain and the static ground are black (equal);
what differs is what animates between two shots (the fog clouds, the water, the trees' sway) [L for the numbers; I
for the attribution to animation: two shots of one build were not compared; that pair is now taken, C.4.4.1].

**C.4.4.1 The noise floor: shots of ONE build at the same turn (ticket 0606 item 7, fourth lead, 2026-11-20)** [L].
Build 26577625 on c5h-new-a, the same layout as C.4.2 (`layout2`, proxy `608f5918`, server `da240792`, composite
`a36e8507`, `VP64_DEV_READY_BUILD=26577625`), `C5H-4967-P2-STD_0000` loaded, `c_drive.py --turns 10` with the leader
screen (09:13-09:18 EST), then three half-size `cli.py shot --scale 0.5` shots at turn 10 with no popup open (D114):
a, b 15 s later, c 64 s after b. `pic_diff.py`, 1277 x 705:

| pair | median | p95 | max | within 16 |
|---|---|---|---|---|
| a - b (one session, 15 s) | 6 | 27 | 183 | 83.3 % |
| a - c (one session, 79 s) | 7 | 34 | 197 | 75.5 % |
| b - c (one session, 64 s) | 8 | 33 | 197 | 74.9 % |
| a - arm B's turn-10 shot (same build, another run of the same save) | 9 | 34 | 194 | 74.1 % |
| build 26577625 vs build 4390913 (C.4.4, for reference) | 8 | 39 | 207 | 74.1 % |

So the two builds differ by about what one build differs from itself a minute later (median and share within 16 the
same; p95 39 against 27-34): the cross-build difference is animation noise, not a rendering difference [L for the
numbers, I for "no rendering difference" beyond what pixel statistics can show]. Stop exit 0, no orphan, 0 LOUD in
vpproxy.log, server.log, render.log and shell_composite.log, `lay.py restore` ok. Shots kept locally only.

**C.4.5 Verdict of part 2.** Every feature group alive on both builds, no new refusal on 4390913, no crash, no box
(part 2's only boxes were the 0588 popups of C.4.3, as designed). Not verified: the installer-installed layout (pack,
manifest, the shipped vpproxy.ini), the modpack form; the server's own cities and landmarks (`ServerCities`,
`ServerLandmarks` off by default and not switched on); live terrain (not on the head); the leader-scene proxy path for
slot 22 and up; a create-site count (no log line); the farm guard's composite-owned path (the proxy owns it in the
product).

### C.5 The product-ready flip of build 26577625 (ticket 0606, the owner's decision D214; fifth lead of 0590, 2026-11-20)

**The change [C].** `VpExeBuildProductReady` (`exebuild/include/vp_exe_build.h`), `PRODUCT_READY`
(`tools/exebuild/builds.py`) and the installer's `ProductReadyExeBuild` (`tools/installer/patch64.iss`, one line) name
build 26577625 beside 4390913. `proxy/tools/rt_tests/session_unit.cpp` asserted that 26577625 was not product ready;
its checks now assert the rule for every known build (supported exactly when product ready without the developer
switch; `VP64_DEV_READY_BUILD` naming a build makes `DevReadyBuild` true only for a build that is not product ready)
and name both builds as ready. With every known build ready, the proxy's not-ready path (0588's `unsupported-build`
FALLBACK for a known build) is reached by no real EXE; `DecideBoot`'s unit checks keep it tested through their inputs
(`in.supported = false`). A made-up known-but-not-ready build cannot be injected into the gate without a test hook in
product code (the known-build list is compile-time), so none was added. The developer switch stays in the code and is
now a no-op for both builds.

**The flipped release (a TEST build, D27).** Taken through the release driver's candidate stages
survey, branches (skipped, `--carry pins`), testdata, lock, build, model, purity, pack (`take_release.py Release-5.4.8
--carry pins --tier candidate --only <stage>`, descriptor = the repository's `Release-5.4.8.json` + `shell_config i2`,
pins x64 `1fd51353e` / i0 `5d18d3d6c`, work `C:/c5-64bit-work/0590/lead5/run`, 13:04-14:04 EST; driver
`C:/c5-64bit-work/0590/lead5_take.sh`), at this repository's `4c0104623` (the flip, `tools_dirty` false), then
`build_installer.py --test-build`. [C, logs] build: x64 1264 s, shell 440 s; layout `x64-1fd51353-p4c01046`: proxy
`799033d4...`, server.exe `33bfca13...`, x64 DLL `3f1c1112...`, compositor `d2c9fc20...`, vp64helper `a53fde12...`;
pack manifest `2f54391a...`; installer `VP64-Patch-x64-1fd51353-p4c01046-for-VP5.4.8-TEST.exe` 9,439,944 B, SHA-256
`91a9579ccdf84072f70fb3f51af9113ab100da1663ba4fd1fc3a7aa809911831` (a TEST build: Goldberg copies only, never the owner or
GitHub, D27; kept in that work folder). **The test data came from the pre-update core database:** the default
`core_db` (the workstation's live game cache) was rebuilt by build 26577625 and breaks `c5h snapshot`
(release-driver.md, testdata; ticket 0612), so `C5_CORE_DB` named a copy of 0574's pre-update database
(`dec46218...`). Offline on `4c0104623` [C, 2026-11-20]: tools/exebuild tests + test_safety_static 107 passed;
test_installer 35 OK; test_proxy_session 154/154 and test_proxy_rt 203/203 (session_unit included) against the take's
x64 build; shell test_exe_tables_shell_b 25 passed, shell static signature tests 6 passed.

**Run (a): the product path without any developer switch [L].** [L, 2026-11-20 13:53-14:04 EST, c5h-new-a (F:, Goldberg,
build 26577625, mods form), the instance's own 32-bit DLL `ff0f6e86...` as fallback, `VP64_DEV_READY_BUILD` unset]:
driver `C:/c5-64bit-work/0590/lead5_a.sh` (4967's `lay.py prep` with the take's layout and compositor,
`vp64helper on`, `c_settings.py` ([Performance] as 0.1b, `leader_scenes=1`), `c_prepare.py new`, `cli.py launch`
without `--env`, `c_drive.py --turns 10`). vpproxy.log: `boot: exe build: build 26577625 (stamp 0x6AF3837D,
SizeOfImage 0x063A2000): known and supported: the 64-bit path starts` (no DEVELOPER line). New Standard game, leader
screen, turns 0 -> 10, stop exit 0, no orphan. The turn-10 autosave (`AutoSave_0009`, kept as
`C5H-0606-A-STD_0009` in `C:/c5-64bit-work/0590/lead5/saves/`) loaded, turns 10 -> 13, stop exit 0. LOUD 0 and
FALLBACK 0 in vpproxy.log, server.log, render.log and shell_composite.log of both launches; no box. Every feature group
of arm B alive (`c_features.py` against C.4.2's arm B logs): boot, view and white mask, model fog, queue guard, uidraw,
pacing options 2/3/4, leader scenes, farm guard (8 lines, 13 patterns), camera (11 patterns, OK), render `STARTING ->
READY`, water (29), ice (21; generation applied), overlay (8), shadow, decal, cut, fow models (7), unit filter,
forest drawn (render.log `forest ... state=ACTIVE`). Lines only in this run against arm B: the newer `[Extras]` status
lines (both off by default) and one shadow status variant; none is a refusal. `lay.py restore` ok.

**Run (b): the installer-laid layout on a Goldberg copy [L].** [L, 2026-11-20 14:06-14:18 EST, c5h-new-b (F:, Goldberg,
build 26577625), VP 5.4.8 in the **mods form** (no modpack instance of this build exists; the modpack form is not
verified on it)]: driver `C:/c5-64bit-work/0590/lead5_b.sh` (0574's install check: the player's state by
`provision.py set-dll --dll` the tag's 32-bit DLL `262aee88...`, tree listing, the installer silent through
`evidence/2600/run_installer.py` in test mode with the instance's `/DIR`, `/GAMEDIR` and `--procroot`, other teams'
games running on the workstation). Install exit 0; the log's ready check: `Game EXE: CivilizationV_DX11.exe is Steam
build 26577625 (PE stamp 0x6AF3837D, SizeOfImage 0x063A2000).` and **no** `does not support yet` warning (0 lines);
`Vox Populi (mods form) ... 262aee88...`, standard variant, `install x64-1fd51353-p4c01046 (std, i2, mods form)`; the
My Games tree differs from before by exactly the installed files (30 lines). The game on the installer-laid layout
(no switch, the installed options as shipped: pacing options 2 and 4 off, leader scenes off, the defaults of an i2
install): vpproxy.log `boot: pack ...\VP64 ok: release Release-5.4.8, 32-bit DLL 262aee88... matches x86_dlls, x64 DLL
CvGameCore_Expansion2_x64.dll 3f1c1112..., purity ...` and `known and supported: the 64-bit path starts`; new Standard
game, leader screen, turns 0 -> 10, stop exit 0, no orphan; LOUD 0, FALLBACK 0 in all four logs; the same feature
groups alive as run (a) (options 2/4 and leader scenes off as configured). Removal exit 0; **the My Games tree is
identical by SHA-256 to the tree before the install outside the game's run outputs** (Logs, cache, Saves, ModdedSaves,
ModUserData: 73 entries, the game's own). The instance's DLL put back (`set-dll --previous`, `ff0f6e86...`), verify
--quick ok. Note: `set-dll --original` puts back the instance's FIRST DLL, not the one before the run; use
`--previous` (seen here, corrected the same minute).

**Regression on build 4390913 [L].** [L, 2026-11-20 14:25-14:32 EST, c5h-d (F:, Goldberg, build
4390913, mods form), the same flipped layout, the same driver and settings as run (a)]: `known and supported: the
64-bit path starts` for build 4390913, new Standard game 0 -> 10, stop exit 0, no orphan, LOUD 0 and FALLBACK 0 in all
four logs, no box. Against 4967's build-4390913 arm (C.4.4, team layout): no refusal only in this run (the lines only
here are the `[Extras]` status lines, off by default, and status variants of shadow, terrain and cut). The flip changes
nothing on 4390913 [L for this run; C: `VpExeBuildProductReady` already named it]. `lay.py restore` ok. (A first try of
this run on c5h-a, 14:07, wrote to an instance another worker held: the driver's pool claim was refused but a pipe hid
the refusal; the harness refused the launch and the instance was restored by hash at 14:15; the driver now runs with
`pipefail`.) Evidence: `evidence/0606/runs.txt`.

**Still open after the flip (written as open, D214).** Items 3 to 8 of 0606's not-verified list: (3) ~~live terrain on
build 26577625 (`lt_ginv` stays [I])~~ done 2026-11-20 by ticket 0619: the 0586 ice/waves gate was not carried (fixed
stamp and addresses; now on the build table) and live terrain ran on c5h-new-b: ice reloads ok, 4905's field writes ok,
`lt_ginv` [L] by an identical field hash (`research/05-graphics-composition/live-terrain.md` 10.8); (4) the four medium-risk carries of shell A beyond "pattern matched, feature up":
not needed for the first beta (ticket 0620, C.7.5); (5) the unit-filter path-2 tests' fake EXE lays out build 4390913
only: not needed for the first beta (C.7.5); (6) ~~ServerCities/ServerLandmarks not switched on, leader scenes for slots
22 and up not seen, no create-site count line~~ done 2026-11-20 by ticket 0620 (C.7): cities, landmarks, units and
hidden embarks switched on and live on build 26577625 in both forms; leader scenes are not in the installer's
configuration (no `leader_scenes` key is written, so they are off) and no save with a slot of 22 or above exists for
the standard variant; the renderer has no create-site count line (READY without a refusal stays the proof) (replaced:
"ServerCities/ServerLandmarks not switched on, leader scenes for slots 22 and up not seen, no create-site count
line"); (7) done (C.4.4.1); (8) the two engine-utility slots 195 and 256 not named (NEW-0590-2): not needed for the
first beta (C.7.5); named 2026-11-24 by ticket 0604 (C.8). Further: (9) DONE 2026-11-20 by ticket 0618 (C.6): no
layout difference on 26577625; the hold and the sound executor ran live there, their rows go to epic 0042 as patches;
only 4727's effect player keeps a live start/stop item. Replaced text: "two executors of epic 0042 refuse on build
26577625 until their rows are read there (lead-0042, thread 0590 #8255; W19 #8281): 4773, the proxy's combat hold
(on refusal the EXE's own combat timing stays, as today) and 4723, the shell's sound executor (refuses any build but
4390913 until FSound's layout at +0x18/+0x28/+0xA4 is read on 26577625; on refusal no server-owned fight sound is
played: while route S is off by default (its switch 4783 not merged) the player hears the EXE's stock sounds; once
route S is on, that build would have no combat sound at all, W19 #8281); ticket 0618 (started 14:11, worker W5 of 0590) carries these layouts. Neither is armed by default
at the flip." (10) ~~The modpack form of VP on build 26577625 (no modpack instance of that build exists)~~ done
2026-11-20 by ticket 0620 (C.7.3): instance c5h-new-mpk, installer-laid i3 layout, start, load, 10 turns, exit by the
Lua channel; two modpack-form faults of the product found (C.7.4 A: the composite's log goes into the game folder and
survives the removal). (13) New, ticket 0620 (C.7.4): the first beta's installer must carry 0606's
`ProductReadyExeBuild` line (int-0616's `patch64.iss` still names only 4390913), and the first beta's x64 DLL must be
the 0.1b pin (1b87dd185) for `HiddenEmbarks = 1` (0606's take, 1fd51353e, has no export). (11) The take's
test data were built from the pre-update core database (`dec46218...`, release-driver.md, ticket 0612); test data from
the new build's own core database are not verified. (Answered 2026-11-20 by ticket 0612 [C]: build 26577625's base-only
core cache has the same rows as `dec46218...` (content digest over every table), so one pinned file serves both builds;
the workstation's live cache `e5666c30...` is the fully loaded database (DLC and VP rows), not a core database, and is
refused; the release tooling now pins the core database by SHA-256: release-driver.md W1a.3.) (12) The HI release journeys on the new build (0601 step 2, not a
precondition, D214).

### C.6 Route S layouts (ticket 0618, w5-0590, 2026-11-20)

Route S's three shell parts read or write EXE structures whose offsets were read on build 4390913 only, so each
refused every other build (D118): the combat hold of 4773 (`proxy_combat_hold.*`), the sound executor of 4723
(`proxy_sound_exec.*`) and the effect player of 4727 (W26 of epic 0042, not written yet). Lists from lead-0042 (thread
0590 #8385) and W26 (thread 0042 #8408). Result: **no layout difference on build 26577625**; the two existing parts list
26577625 in their `kLayoutVerified` (patches to epic 0042, below); no per-build table is needed.

**C.6.1 Method [C, golden copies, 2026-11-20 14:15-14:45 EST].** For every EXE function a part relies on (the ones that
read or write the fields, by the lists' research references), the old-build function is carried to the new build with
`tools/exebuild/addr_match.match_code` (the build table's matcher), and both bodies are listed with llvm-objdump 19
from the entry to the int3 fill. Each instruction is normalised: branch and call targets and every operand value of
0x10000 or more (absolute addresses) become one token; every smaller value (struct displacements, immediates, `ret N`)
is kept. A function is "identical" when the two normalised sequences are equal, which means every displacement and
immediate in it is the same on both builds. Look-alike bodies the matcher refuses as ambiguous are checked instead by
their whole masked bytes (start to the last `ret`): the number of copies in the old `.text` and in the new one. The
scripts and listings are local only (`C:/c5-64bit-work/0618/`, D83/D202: they hold new-build addresses and disassembly);
no new-build address is written here. The comparison is not blind to change: the listing of 0x4037E0 runs on past its
`ret 4` into a neighbour that the relink changed, and that tail shows as 70 changed lines.

**C.6.2 The combat hold (4773) [C].** 16 functions, all identical: find-by-id 0x797E70, the move 0x4AC910, the
collection 0x457060, the pump 0x785AF0, the dispatch cursor 0x6DE5D0, the header copy 0x490310, the posts 0x87E3D0 and
0x87E4A0, `GameplayUnitCombat` 0x67DD90 (673 instructions), the simulation's take 0x4BA9A0, the base destructor
0x4067C0, the 0x59 listener 0x488B30, the simulation's start 0x731FF0 (650 instructions; the visualise byte at payload
+0x500), the block test 0x60EB10, its predicate 0x727A30 and the section leave 0x813D60 (combat-choreography.md 3.1-3.5).
Uses compared, old/new per field across these functions: stride 0x4C 16/16, flip +0x20 30/30, half +4 116/116 and +8
101/101 (all uses of the small offsets, not only the queue's), section +0x34 18/18, dispatched +0x2C 26/26, counter +0x3C
7/7, flags +0x40 5/5, event id +0x44 8/8, visualise +0x500 1/1, RunCombatSim 0x59 and size 0x560 in the posts 1/1 and
4/4. So the layout of `proxy_combat_hold.h` (stride, flip, halves, begin/end, critical section, record size, type,
header words, counter, id, visualise byte, 0x59/0x560) holds on build 26577625; `pac_qb_index` and `pac_qb_buf` are the
build table's (carried since stage 2).

**C.6.3 The sound executor (4723) [C].** 33 functions, all identical: the 3D play 0x8DC7F0 (both of its returns
`ret 0x2C`, so the declared 11 stack words hold), `FSound::Stop` 0x90EDE0 (its tests of +0x28 and +0x18), the
functions of both caller rows (the Sound trigger's type-0x15 handler 0x74C620 holding `audio_play3d_call`, the unit-loop
record function 0x8DB910 holding `audio_stop_call`, which also tests +0xA4) and the 29 functions of exe-audio.md's two
FSound method ranges (0x90E640-0x90F0A9, 0x919A40-0x919DB5; 0x919BE0 writes +0xA4). Uses of +0x18 49/49, +0x28 16/16,
+0xA4 2/2. The rows and the four patterns were already carried and OK (unit-combat-server.md 10.3). The script index at
script data +4 is not read by any of these functions statically; it is checked by the live smoke (C.6.5).

**C.6.4 The effect player (4727) [C].** W26's list #8408: the allocators 0x846030, 0x866AB0 and 0x866A50 (types
0x28, 0x29, 0x2A, size 0x80 each: identical, so each look-alike's type immediate is where it was), the functions of the
three caller rows (0x4CE510, 0x7454E0, 0x754330: the payload stores at +0x30, +0x40, +0x50, +0x54 identical), the key
map 0x5DB2B0, the stop 0x426700, and the name hash: the CRC routines 0x46E1F0 (C string), 0x830890 (with a length),
0x712320 (the 16-byte key) and the table fill 0x9C4BC0, all identical. The particle system's listeners: 0x4037E0 (type
0x2A) identical up to its final `ret 4`; 0x77DC60 and 0x56A2D0 (types 0x28 and 0x29) are a byte-identical look-alike
pair (2 copies of the 205-byte body in each build), so both are unchanged whichever copy is which; the job functions
0x64D390, 0x52DF30 and 0x4CFEF0: each whole masked body is unique in the old `.text` and found once in the new one. The
effect player's code does not exist yet, so nothing of it is armed or run here.

**C.6.5 Live [L, 2026-11-20 15:11-15:47 EST].** Layout: 0606's flipped release layout (`x64-1fd51353-p4c01046`:
server `33bfca13`, x64 DLL `3f1c1112`, composite `d2c9fc20`) with the proxy replaced by a local build (`0b2b78d6`) of
the 0590 team branch at 4f9b0108d merged with `w19-4723` @03b56133a and `epic-0042-combat-movement` @6de22c4f5 plus the
two `kLayoutVerified` rows (the patches below). No developer switch (the release layout is product-ready for 26577625,
0606). The drivers are 4773's and 4723's (`proxy/evidence/0042/4773/run_4773.py`, `.../4723/run_4723.py`) with this
ticket's owner, folders and save names; save `C5H-3000-TINY-T0018` copied under its own name; arm h5 (ServerRenderer 1,
ServerUnits 0, Combat and Projectile off), Quick Combat off and read back; restore by SHA-256 after every run
(backup_restore_ok true, provision verify 0).

| run | instance, build | result |
|---|---|---|
| hold (`VP64_TEST_COMBAT_HOLD=5000`) | c5h-new-a, 26577625 | "combat hold: on (Steam build 26577625 ...; layout verified: ticket 0618 ...)"; takes 4, drops 4, all by OwnerEnd, none by the watchdog; RunCombatSim -> EndCombatSim 5.000, 5.001, 1.735, 4.769 s (the blitz's second fight starts at the first's end, the AI-turn fight under the barrier) |
| sound (`VP64_TEST_SOUND_EXEC=1`) | c5h-new-a, 26577625 | "sound exec: armed on Steam build 26577625"; attached; records 5, played 2 (the two loops), refused 1 (the one-shot, as on 4390913), stopped 2 (clip 1, owner end 1), executedTotal 3, 4, 5, droppedStale 0, lost 0 |
| hold | c5h-b, 4390913 | layout line of 4796; takes 4, drops 4 by OwnerEnd; 4.999, 5.000, 1.733, 4.651 s (4773's own run: 5.003, 4.999, 1.701, 4.820) |
| sound | c5h-b, 4390913 | armed on 4390913; the same counts as on 26577625 and as 4723's run (unit-combat-server.md 10.6) |

Every run: 0 LOUD in server.log, render.log and shell_composite.log, one in vpproxy.log (the TEST ONLY switch's own
line), no crash, no box, stop and restore clean. The two stops on 26577625 passed the liveness check, so FSound +0xA4's
script data names the script index at its +4 there too. Not covered here: the drivers' scenes only (no fight with an
owner reaching the hold from the sound executor, no ring-out or watchdog live, as on 4390913); the Miles-log timings of
the stops were not extracted on either build.

**C.6.6 What changes elsewhere.** unit-combat-server.md 10.5's build-26577625 entry and combat-choreography.md 3.5.9
(epic 0042's files, through the patches) say the same. On build 26577625 the player now gets route S's hold and server
sound exactly as on 4390913 once route S is switched on. The effect player of 4727 has its static part done (C.6.4);
its code must list 26577625 in its own verified list when written (no layout difference to carry) and get its own live
start and stop. 0606's not-verified list (its C.5 draft) drops "route S's parts refuse 26577625" for the hold and the
sound executor.

**Patches** (thread 0042, lead-0042): `proxy_combat_hold.{h,cpp}` (row and refusal text), `combat_hold_test.cpp` (the
known-but-unverified case now uses the next build id, since both real builds are verified; a case that arms on 26577625;
the binding case for a 26577625 header passes the layout check) and combat-choreography.md 3.5.9: `test_combat_hold.py`
15/15 (core 144/144, binding 163/163, ASan); `proxy_sound_exec.cpp` (row and refusal text) and unit-combat-server.md
10.5: `test_sound_exec.py` 12/12 [C, 2026-11-20].

### C.7 What the installer's configuration switches on, and the modpack form, on build 26577625 (ticket 0620, w7-0590, 2026-11-20)

**C.7.1 The configuration and where each key ran live on build 26577625.** The installer of the first beta is the
i3 configuration of ticket 0574 (`research/06-tooling-and-workflows/release-builds.md` 1, 3): `vp64helper on` writes
`ServerRenderer = 1` and the config.ini form (D33's `[DisableSystems]` keys), and before it `P64WriteOptions` writes
the option set of the pack manifest into `VP64Options.ini`. The set is a take setting the MC fixes at ticket 0601 (MC
#8392 Q3); the table takes the last one named, rc1's (rc0c's descriptor, `release-builds.md` 4.6: D197, 0561's
cities and landmarks, 0573's hidden embarks; `GameEventsOneCrossing` stays 0, MC #8392 Q4). Defaults are
`options/src/vp64_options.cpp` on the team branch (ac68b5bd8). Runs: C.4.2 (developer switch), C.5 (a) and (b) (no
switch; (b) installer-laid, i2), C.6.5 (route S), 0619 (`live-terrain.md` 10.8), and this ticket's three runs (C.7.2,
C.7.3).

| Key | Default | Installer (rc1 set) | Live on build 26577625 before 0620 | 0620 |
|---|---|---|---|---|
| `[Renderer] ServerRenderer` | 0 | 1 (`vp64helper on`) | C.4.2, C.5 (a), (b) [L] | M, P [L] |
| config.ini `[DisableSystems]` (Decal, Forest, FOW, FluidFOW, DistanceFog, YieldIcons) | stock | written by `vp64helper on` | C.4.2, C.5 (a), (b) [L] | M, P [L] |
| `[Renderer] ServerUnits` | 0 | 1 | **no** (C.6.5's arm had 0) | M: `units totals ... drawn_units=310691`, composite `unitfilter ... dropped=835004`; P: `drawn_units=12882`, `dropped=120644` [L] |
| `[Renderer] ServerCities` | 0 | 1 | **no** | M: `modelfilter totals ... dropped_cities=623872`, render `totals models ... keys=5542 (cities 636, landmarks 4906) ... drawn=11740 ... draw_errors=0`; P: set of 633 city keys published and acked (`bits_acked=0x6`), `dropped_cities=0` because no city was in view (the driver founds none) [L] |
| `[Renderer] ServerLandmarks` | 0 | 1 | **no** | M: `dropped_landmarks=3226162`; P: `dropped_landmarks=80208`, 4,906 landmark keys [L] |
| `[Renderer] ServerArc`, `ServerPath` | 0 | 0 (written) | (= default) | - |
| `[Renderer] ServerFarmFields` | 0 | not written | (= default) | - |
| `[Performance] AiUpdateTickMs` | 0 | 0 (written) | C.4.2, C.5 (a) `pacing: on` [L] | M, P [L] |
| `[Performance] AiSkipVisualDrain` | 0 | 1 | C.4.2, C.5 (a) `pacing: option 2 on` [L] | M, P [L] |
| `[Performance] AiDrainUnordered` | 0 | 1 | C.4.2, C.5 (a) `pacing: option 4 on` [L] | M, P [L] |
| `[Performance] HiddenEmbarks` | 0 | 1 | **no** | M only: server `hidden-embark: [Performance] HiddenEmbarks = 1 (from the file); the DLL's feature version 1; told the DLL on`, end `on (source: the server), calls 275: posted watched 275 ... held back 0 ... missed reveals 0` [L; DIAGNOSTIC DLL swap, C.7.2]. P left it out: 0606's x64 DLL (1fd51353e) logs `it has no export VP64HiddenEmbarkControl (built without VP64_HIDDEN_EMBARK)` |
| `[Performance] GameEventsOneCrossing` | 0 | not written (MC Q4) | - | - |
| live terrain (no key; on with ServerRenderer) | on | on | 0619, `live-terrain.md` 10.8 [L] | M: `live terrain: on`, producer made in 1005 ms, the ice/waves section opened; no field change in the 10 turns [L] |
| `vpproxy.ini leader_scenes` | off (no key) | not written | C.4.2 (=1, slots below 22) [L] | not in the configuration |
| `[Extras]` | file defaults | not written | C.5 (a) status lines | - |

M = C.7.2 (mods form, c5h-new-a), P = C.7.3 (modpack form, c5h-new-mpk). What still has no live run on build
26577625 in the shipped form: `HiddenEmbarks = 1` with an installer-laid pack (it needs a pack built from the 0.1b pin;
the first beta's take 0601 makes one). A player whose pack lacks the export sees every embark as in the stock game
and one D118 report (`srv_hidden_embark.cpp` `Decide`) [C].

**C.7.2 Run M: the whole set in the mods form (DIAGNOSTIC layout)** [L, 2026-11-20 16:58-17:15 EST, workstation,
c5h-new-a (F:, Goldberg, build 26577625, mods548, Vibrant Textures), its 32-bit DLL `ff0f6e86...` as fallback, no
developer switch]. Layout: 0606's flipped release layout (server `33bfca13`, composite `d2c9fc20`) with this ticket's
proxy (`486b933d...`, built from the team branch at ac68b5bd8 with 0619's gate, `build_proxy.py` 70 s) and **0616's
x64 DLL of the 0.1b pin 1b87dd185** (`1719a788...`, `C:/c5-64bit-work/0616/x64-release`, built with int-0616's marker
defines) in place of 0606's: the only DLL with hidden embarks that runs beside a server built for build 26577625
(proxy/model regenerated EQUAL at 1b87dd185, impl-0616). `lay.py prep` + `helper on`, then the set written as
`P64WriteOptions` does (`evidence/0620/w7_settings.py`; after `on`, not before: only `off` differs). Save
`C5H-VP8P-HUGE_0350` (Huge, 8 players, turn 350), `c_drive.py --turns 10 --no-leader`: turns 350 -> 360, AI turns
14.3-22.2 s; exit by the Lua channel (`evidence/0620/w7_game.py exit`: `stop_readiness`, then `UI.ExitGame()`; the EXE
ended by itself after 8.3 s, exit code 0); `lay.py restore` ok. 0 FALLBACK; vpproxy.log and render.log 0 LOUD;
server.log 200 LOUD = the cap of 0576's stale-unit guard (`Unit.GetTransportUnit`/`GetPlot` on deleted units, channel 0:
the same lines on the same save on the old build, `research/03-dll-engine-boundary/lua-stale-instance.md` 6.2, not a
fault of this build); shell_composite.log 1 LOUD at the exit (C.7.4 B). No box, no crash.

**C.7.3 Run P: the modpack form, installer-laid** [L, 2026-11-20 16:20-17:08 EST, workstation].
- **The instance** c5h-new-mpk (new; authorized by ticket 0620): `provision.py --root F:\c5-instances create
  c5h-new-mpk --exe-build 26577625 --like c5h-mpk --gbe-dir <1112's gbe-release-x32> --dll <i0ship-5d18d3d6-ia4968>`
  through `quietlock.py`: pure_vp plus the golden copy's changed files of build 26577625 plus the 5.4.8 modpack
  (`F:\c5-64bit-work\2600\modpack548\VP_MODPACK`, profile modpack548), 16,034 + 5 files, 8.24 GB, 119.2 s; `verify
  --full` ok, no error, no warning; port 47597. No Steam install read or written.
- **The installer** (a TEST build, D27): int-0616's `build_installer.py` (i3) on a take-shaped work folder made from
  0606's take (`evidence/0620/make_work.py`: its pack copied, the manifest relabelled `shell_config i3` with
  rc1's set minus `HiddenEmbarks`; the build's proxy replaced by this ticket's), `--test-build`:
  `VP64-Patch-0620-i3-test-for-VP5.4.8-TEST.exe`, 9,444,574 B, SHA-256 `e884076e...`. Player's state first:
  `set-dll` the tag's DLL `262aee88...`. Install (`run_installer.py`, test mode): exit 0, `Vox Populi (modpack form):
  ...\VP_MODPACK\Mods\(1) Community Patch\CvGameCore_Expansion2.dll`, `install 0620-i3-test (std, i3, modpack form)`,
  the set's lines `P64: options: [Performance] AiDrainUnordered = 1 ...`. The DLC tree gained exactly the 18 product
  rows (proxy in place of VP's DLL, VP's DLL as `_x86`, compositor + ini + manifest, `VP64\` with the pack and
  `release-options.txt`, helper, notices, `vpproxy.ini`); My Games: config.ini changed, the two side files, `VP64\install`
  (record, two backups) and a new `VP64Options.ini`.
- **Game 1** (new Standard game, no mods, Continents, 7 AIs): proxy `boot: pack ...\VP_MODPACK\Mods\(1) Community
  Patch\VP64 ok`, `boot: exe build: build 26577625 (stamp 0x6AF3837D, SizeOfImage 0x063A2000): known and supported: the
  64-bit path starts`; the options read from the file; leader screen opened and closed; turns 0 -> 10, AI turns 2.0-5.5
  s; exit by the Lua channel (EXE exit code 0 after 6.1 s). 0 LOUD and 0 FALLBACK in all logs. **But the world was
  grey**: the composite never saw a Present (`hook IDXGISwapChain::Present: target ... is in steam_api.dll ... NOT
  hooked`): the harness's frame cap took the slot first because `framecap.composite_of` looks only under
  `MODS\*\shell_composite.dll` (`run-and-drive-the-game.md` 15; a harness fault, not the product's).
- **Game 2** (`C5_FRAME_CAP_AFTER=shell_composite.dll`, the turn-10 autosave `AutoSave_0009 BC-3640` loaded): the
  composite hooked Present in dxgi; the world drawn by the server (terrain, rivers, fog, forests; a diagnostic shot,
  local); units (`latched=0x7`, 120,644 draws dropped, 12,882 drawn by the server), landmarks (80,208 dropped), the city
  set published and acked; unit filter path 2 12 of 12, model filter 10 of 10; cut, decal, water taps READY; turns 10 ->
  20, AI turns 2.6-5.5 s; exit by the Lua channel (exit code 0 after 6.2 s). vpproxy, server and render logs 0 LOUD,
  0 FALLBACK; shell_composite.log 1 LOUD at the exit (C.7.4 B). No box, no crash in either game (dialog watcher: exit
  code 0, boxes 0).
- **What the first log lines name**: the proxy's names the EXE build (above) and the pack's path, which is the form
  (`...\Assets\DLC\VP_MODPACK\Mods\...` against `...\My Games\...\MODS\...`); no line says "modpack form" in words.
  The server's first line names its own build and image path (the same folder); it does not name the EXE build.
- **Removal** (exit 0): VP's DLL `262aee88...` back in place, config.ini back, `VP64Options.ini` (made by the install)
  gone; My Games identical by SHA-256 outside the game's run outputs (only the five empty folders the game makes:
  Maps, MODS, Replays, ScreenShots, TransferredMaps). **The DLC tree is not identical**: `VP_MODPACK\Logs\` with
  `shell_composite.log` and one rotated log stays (C.7.4 A); moved to the evidence folder by hand, then the tree equals
  the listing before the install except the DLL that `set-dll --previous` put back (`ff0f6e86...`); `verify --quick` ok.

**C.7.4 Findings** [L, C].
- **A. The composite writes into the game folder in the modpack form** (any EXE build). `proxy_shell.cpp` `OutDirFor`
  takes the last `\MODS\` of the composite's path, case-insensitive; a modpack's path has `...\Assets\DLC\VP_MODPACK\Mods\`,
  so the proxy sets `VPSHELL_OUT_DIR=<game>\Assets\DLC\VP_MODPACK\Logs` (vpproxy.log `shell: VPSHELL_OUT_DIR=...
  set`), and the farm guard's records follow the same rule (line 2058). The installer's removal does not know that folder,
  so it stays. The mods form is unaffected (its `\MODS\` is the user folder's). ~~Fix (proposed to lead-0590, #8567): when
  the composite is not under the user folder's `MODS`, use the user folder of `run_root` + `\Logs`.~~ **Fixed 2026-11-20 by
  ticket 0623** [C code and unit checks]: `OutDirFor` checks an `Assets\DLC\` folder pair (whole components, any case,
  something after `DLC`) before the MODS rule and then gives `<folder of the game's config.ini>\Logs`
  (`ProxySession::ConfigIniPath`: `VPPROXY_TEST_CONFIG_INI`, else `C5_DOCUMENTS\My Games\Sid Meier's Civilization 5`,
  else My Documents' one). Why that folder: in the modpack form the proxy's run folders and its off-switch log already go
  to `<the same folder>\Logs\VP64` (`ProxyLogic::UserLogsDir`, issue 2606) and its error report to that folder too, and
  in the mods form the composite's log is `<user>\Logs\shell_composite.log`, which is the same folder in an installed
  game: both forms now write the composite's log and records to `<My Games>\Sid Meier's Civilization 5\Logs`, a folder the
  game itself owns and the removal does not have to clean (it is outside the game install, the removal's record does not
  name it, and the game's own logs live beside it). `false` (the composite writes beside itself, the old behaviour) only
  when config.ini's path has no folder or the result does not fit. The farm guard's record folder (OnBoot) follows the
  same function. Unit checks: `proxy/tools/rt_tests/shell_unit.cpp` (both forms, any case, forward slashes, the near
  misses `AssetsX\DLC`, `DLC\Assets`, a file named `DLC`, the size limit, a config.ini without a folder). Live checks:
  C.7.6.
- **B. One LOUD line at every in-game exit, on both builds.** `fow_models: LOUD: the entry of handle 0x.. holds
  0x00000000, not our view ... (the process ends): not written` (`shell/composite/fow_models.cpp` `PutBack`, called with
  "the process ends" at the DLL's detach): at `UI.ExitGame` the EXE's own teardown has already cleared its
  texture-table entry, so the compare-and-swap finds 0 and correctly writes nothing. Seen on build 26577625 (runs M and
  P) and on build 4390913 (c5h-d, the same layout, HUGE_0350, one turn, `UI.ExitGame`, 17:19-17:23 EST) [L]; never
  seen before because the harness's `cli.py stop` ends the process without a detach. Harmless (no write, exit code 0);
  a plain line for an entry of 0 at the process's end would remove the noise. **Fixed 2026-11-20 by ticket 0623** [C code;
  test]: the cause in the code is `FowModels_ProcessExit` (the composite's `DLL_PROCESS_DETACH`) -> `PutBack("the process
  ends", d3d = false)`, whose compare-and-swap expects our view in the white handle's view-table entry; the EXE's own
  teardown at `UI.ExitGame` has already zeroed that entry, so the swap finds 0. `PutBack` now logs `fow_models: the entry of
  handle 0x.. is already 0 (the process ends: the game's teardown cleared its texture table): nothing to put back`
  (plain) for exactly that case: at the process's end (`d3d` false), no fault, value 0. Any other value at the end, a
  fault, and every value that is not ours while the game runs (`CheckSwap`, the stop / load / failure put-backs) stay
  LOUD. Tests (`shell/tests/test_fow_models.py`): mode `exitclear` (the test world zeroes the entry just before the detach:
  the plain line, no RESTORED white view, FOWAnim still put back, no LOUD line at all), mode `runclear` (the entry zeroed
  150 frames after the swap while running: `LOUD: FAILED MISMATCH ... changed under us to 0x00000000`, reported in the
  section), and the plain `ok` exit now asserts no LOUD line.
  **Other LOUD or FALLBACK lines of a clean exit today** (grep of 0620's three runs' logs, `F:/c5-64bit-work/0590/w7/`,
  run M mods form 26577625, run P modpack form 26577625 games 1 and 2, the c5h-d check on 4390913; 2026-11-20): none.
  vpproxy.log, render.log: 0 LOUD, 0 FALLBACK in every run; server.log: run M 200 and c5h-d 81 LOUD lines, all
  `remote: LOUD: stale Unit instance ...` of 0576's stale-unit guard, written in play (run M at 167-200 s of a 396-s
  session that ends with `remote: shutdown complete`), not at the exit and not new (`lua-stale-instance.md` 6.2);
  shell_composite.log: the fow_models line above only. So after this fix a clean exit logs no LOUD or FALLBACK line [L for
  the old logs; the fixed build's exit: C.7.6].
- **C. The first beta's installer must carry 0606's flip**: int-0616's `patch64.iss` `ProductReadyExeBuild` returns
  true only for 4390913, so its installer logs `Warning: ... build 26577625, which this version of the 64-bit patch does
  not support yet` (a silent install goes on; an interactive one asks) although the proxy runs the 64-bit path [L,
  install log of C.7.3; C]. MC #8452 asked 0616 to merge main for this.
- **Harness**: `framecap.composite_of` misses a modpack's composite (C.7.3 game 1); `treehash.py diff --apart` takes
  its prefixes as one list (`nargs='*'`): repeating `--apart` keeps only the last one, and a prefix ending in `/` never matches (the listing's separator is the backslash), so give them bare after one `--apart` (`--apart Logs cache Saves ModdedSaves ModUserData`).

**C.7.5 Items 4, 5 and 8 for the first beta** (one line each).
- (4) The four medium carries of shell A (`ice_ice`, `ovl_proj1`, `shadow_sm0_1`, `shadow_blur_name`): not needed:
  `ice_ice` now has its second method (0619 wrote the ice grid there and its W x H and flip count checks held,
  `live-terrain.md` 10.8), and a wrong overlay projection or shadow matrix would show in the picture, which C.4.4.1
  found within one build's own noise; a wrong value draws wrong, it does not crash.
- (5) The unit-filter path-2 tests' fake EXE: not needed: it is offline test coverage; path 2 resolved 12 of 12 and
  10 of 10 programs live on build 26577625 in C.4.2, C.5 and both 0620 runs, with units, cities and landmarks dropped
  and drawn by the server.
- (8) Engine-utility slots 195 and 256: not needed: `proxy_pacing.cpp` copies all 264 entries of the running EXE as they
  are and replaces only slot 102 (checked against `pac_secs_per_turn`), so what those two slots are named does not change
  what runs; pacing options 2-4 ran live (C.7.1). Named since by ticket 0604 (C.8, 2026-11-24).

**C.7.6 Ticket 0623: the fixes of C.7.4 A and B, and two build-neutral log texts** (impl-0623, account B, 2026-11-20,
branch `task-0623-modpack-log-exit-loud`).
- A and B: in C.7.4 (fixed in place).
- **E, the two log texts that printed build-4390913 addresses** [C]: `proxy_shell.cpp`'s base-layer line now says
  `pxw_fow_base of <build label> 0x00000000 -> 0x..` (`VpExeBuildLabel` of the build the white mask resolved; was the
  literal `0xDA12B0`), and `srv_liveterrain_exefield.cpp`'s `ReadObject` refusal says `[cam_gridptr] (<build label>) holds
  ...` (was `[0xD90FC4]`), as its resolve refusal already did. `shell_unit.cpp` checks the new base-layer text and that no
  `0xDA12B0` is logged.
- **Offline** [C, 2026-11-20]: `shell/tests/test_fow_models.py` whole, 32 passed (the two new cases among them);
  `proxy/tools/test_proxy_rt.py --only-units --asan`: the units and arc programs link with `proxy_session.cpp` and every
  byte-equal check against the base commit passed; `--only-shell --asan`: the OutDir checks of both forms pass (the
  results of the last run are in `evidence/0623/runs.txt`).
- **Live, modpack form, installer-laid** [L, 2026-11-20 19:47-20:03 EST, workstation, c5h-new-mpk (Goldberg, modpack548,
  build 26577625), Vibrant Textures; int-0616's i3 TEST installer on 0606's flipped layout with this branch's proxy and
  composite, rc1's option set without HiddenEmbarks]: the proxy sets `VPSHELL_OUT_DIR=<My Games>\Sid Meier's
  Civilization 5\Logs`, the composite's second line names that folder, `<game>\Assets\DLC\VP_MODPACK\Logs` is never made;
  new Standard game, turns 0 -> 5, `UI.ExitGame` (the EXE ended by itself, exit 0): at the exit `fow_models: the entry of
  handle 0x5 is already 0 (the process ends: ...): nothing to put back`, 0 LOUD in the composite's log, vpproxy.log and
  render.log, server.log 2 (0576's stale-unit guard in play), 0 FALLBACK; removal: **the DLC tree is identical** to the
  listing before the install (11,623 rows; 0620: `VP_MODPACK\Logs` left), My Games identical outside the run outputs.
  What the product leaves in the run outputs: `Logs\VP64\run\<boot>\` (kept by design unless the player ticks RemoveLogs;
  with RemoveLogs, a second install + removal, `Logs\VP64` was deleted file by file) and **`Logs\shell_composite.log`,
  which no removal deletes, with or without RemoveLogs** (and its rotated `shell_composite.<stamp>.log` after a later
  start). MC #8718: that is part of this ticket. **Fixed after 0616's merge** [C, hermetic test]: with RemoveLogs the
  installer now also deletes the compositor's files in `Logs` (`P64DelCompositeLogs`; the rule and its test in
  `research/06-tooling-and-workflows/installer-notes.md` 13.4 item 5). Not re-run live (the hermetic removal test
  covers it with real Inno Setup on synthetic trees).
- **Does the removal delete a player's own files in Logs?** [C: `tools/installer/install.iss` `P64RemoveLogs` of
  int-0616 at the archive this ticket used; L: the RemoveLogs removal above] No: with RemoveLogs it deletes
  `VP64Options.ini`, the trees `Logs\VP64` and `VP64\reports` (files and empty folders, never through a reparse point) and
  the empty `VP64`; the game's own logs beside them (`APP.log`, `Lua.log`, `Database.log`, ...) stayed byte for byte in
  the run. Everything under `Logs\VP64` is the product's own folder. The new RemoveLogs step names the composite's files in
  `Logs` by their whole name (`shell_composite.` + anything + `.log` or `.csv`), never `Logs\*`; the player's look-alikes
  stay (installer-notes 13.4 item 5). The box's caption now names `VP64Options.ini` (MC #8776).
- **Live, item D (ticket 0619's acceptance 4) on build 4390913** [L, 2026-11-20 20:05-20:24 EST, c5h-d (F:, Goldberg,
  mods548), Vibrant Textures; `lay.py prep` + `helper on` with the same layout; save copy `C5H-0301-4906-IWPL_0008` of
  `C5H-0167-TINY_0008`; `lt.py` through the live-reload gate]: `boot: exe build: build 4390913 ... known and supported`;
  `ice_remove` (45,4): request 1, 271 ice plots, **ice ok (0 set, 1 cleared, Ice_Reload 226.8 ms)**; `ice_add`: request 2,
  272 ice plots, **ice ok (1 set, 0 cleared, 140.2 ms)**; `fh_hills` (50,16): `exefield: Written: 1 rect(s), 112640 samples,
  47163 heights and 1831 materials changed`, field generation 1, box 3008,1024-3360,1344, **sha256 d51d186d...**, the same
  as build 26577625's and 10.6's runs (`live-terrain.md` 10.8). 0 LOUD and 0 FALLBACK in vpproxy.log, server.log,
  render.log and the composite's log; `UI.ExitGame` (the EXE ended by itself after 5.2 s): the exit's fow_models line plain
  on this build too. No box. The 5 turns after the smoke did not run: the Tiny save's turn 9 stays
  `ENDTURN_BLOCKING_PRODUCTION` after a `PushOrder` from Lua and after `CONTROL_FORCEENDTURN` (a harness gap on this save,
  not the product's). `lay.py restore` ok, My Games identical outside the run outputs; in them `Logs\shell_composite.log`
  and a rotated one (the same gap as above).
- **Live, mods form, installer-laid** [L, 2026-11-20 20:27-20:34 EST, c5h-d, build 4390913, the same TEST installer]:
  install "std, i3, mods form"; new Tiny game (CP + VP + EUI), turns 0 -> 5, `UI.ExitGame` (exit 0); the exit line plain,
  LOUD only 0576's guard in play, 0 FALLBACK; removal with RemoveLogs: **the MODS tree identical**, My Games identical
  outside the run outputs except `VP64Options.ini`, which predated the session and which RemoveLogs deletes by design
  (put back by hash). In `Logs`: `shell_composite.log` and 37 more composite files of earlier sessions on the instance
  (34 rotated `shell_composite.<stamp>.log`): the composite's rotated logs pile up across sessions and nothing removes
  them; the RemoveLogs step added after 0616's merge takes `shell_composite.log`, `shell_composite.*.log` and
  `shell_composite.*.csv` in `Logs` (MC #8718).

Evidence: `evidence/0620/` (runs.txt, `w7_game.py`, `w7_settings.py`, `make_work.py`); the shell drivers and the logs local (`C:/c5-64bit-work/0590/w7run/`,
`F:/c5-64bit-work/0590/w7/`; shots local, D112 diagnostic).

### C.8 The two engine-utility slots whose bodies differ: named (ticket 0604, 2026-11-24) [C by source; I where marked]

Open since 4967's cross-check (`exe-build-table.md` 16; item (8) of C.5/C.7.5, NEW-0590-2). Counted from the DLL's own
interface headers (no EXE bytes, D83); details, line numbers and the cross-checks in `exe-build-table.md` 16, "Slots 195
and 256 named".
- **Slot 195 = `ICvEngineUtility1::GameplayOpenOptionsScreen()`** (`ICvDLLUtility.h:291`, `void`, no parameters). The
  DLL calls it once, from `CvGame::doControl` for `CONTROL_OPTIONS_SCREEN` (`CvGame.cpp:3850` at the x64 pin c29442e8c),
  i.e. when the EXE hands it the human player's Options action (stock hotkey Ctrl+O, `CIV5Controls.xml:397`) [C source;
  the key-to-action step I]. The changed body cannot change what the DLL receives (nothing is returned) [C]; it may
  change what the EXE shows [I]. Reachable in the shipped game: yes, by a key press.
- **Slot 256 = `ICvEngineUtility3::GetNumAICivsProcessed() const`** (`ICvDLLUtility.h:391`). The DLL never calls it at
  either pin [C, `git grep`]; the change cannot reach the DLL [C].
- Why these two bodies differ is unknown [I]; a guess (CEG checks inside the old bodies, gone in 26577625) is in
  `exe-build-table.md` 16. Nothing in the product depends on it: `proxy_pacing.cpp` still copies the running build's
  entries as they are.
- **Follow-up** (acceptance (3) of 0604): a live check that Ctrl+O opens and closes the options screen on build
  26577625 with the patch on, drafted in ticket 0604's text for the MC to number. Not run.

Sources: this repository at 19958b92b; the two golden copies on F:; Vox Populi upstream `CvGameCoreDLL_Expansion2/Exe/`
at 1fb49429f (the local Community-Patch-DLL fork clone); scripts and outputs in `evidence/0582/`.
