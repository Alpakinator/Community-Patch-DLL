# The EXE build table: one registry of known builds, per-build address tables stored locally (epic 0590, D202)

Epic 0590 (decision D202, the owner 2026-11-19 [reworded for privacy]: build the address table, stored locally only). Design by lead-0590,
2026-11-19; the parts marked "W1" are filled in by the generator's worker (child C1). Background and the measurements
that make this possible: `new-exe-build-26577625.md` (ticket 0582) B.2 and B.4.

## 1. What is committed and what is local [design, 2026-11-19]

| Piece | Where | In git? |
|---|---|---|
| Known-build list: Steam build id, DX11 PE TimeDateStamp, SizeOfImage, label (no addresses) | `exebuild/include/vp_exe_build.h` (C++), `tools/exebuild/builds.py` (Python mirror) | yes |
| Symbol catalogue: name, kind, the anchor VA in build 4390913, consumer | `exebuild/symbols/<area>.tsv` | yes (anchor values are the old-build values the code and research already carried) |
| Pattern templates (run-time guard), operands written as symbol names | in the source, `X(...)` syntax of `shell/common/exe_camera.h` | yes |
| Generator, emitter, Python reader | `tools/exebuild/` | yes |
| Per-build address table (name -> VA, method, confidence; shader create sites; blob VAs) | `<exe_tables>/<steam build>/table.json` | **never** |
| Hand-review answers for the cases the matcher cannot carry | `<exe_tables>/<steam build>/hand-review.tsv` | **never** (the reasoning goes to research without the new VAs) |
| The header the builds compile | `<build output>/gen/vp_exe_tables.gen.h` | **never** (build output) |

`exe_tables` is a key in `deps/local-settings.ini` (environment variable `C5_EXE_TABLES` overrides it). On the
workstation it is `C:/c5-64bit-work/exe-builds` (C:, so no build depends on the USB drive F:, COMMON rule 15), with a
dated backup copy under `F:/c5-64bit-work/exe-builds/`. A build without the setting, or without a table for a known
build, fails and names the setting and the generator command.

## 2. Identity of a build [C, header read 2026-11-19 with `tools/exebuild/builds.py` on the two golden copies]

| Steam build | PE TimeDateStamp | SizeOfImage | ImageBase | DX11 file size |
|---|---|---|---|---|
| 4390913 | 0x546CD0A8 | 0x06519000 | 0x400000 | 16,982,584 (oldpc 16,982,840) |
| 26577625 | 0x6AF3837D | 0x063A2000 | 0x400000 | 15,466,264 |

The key is stamp and SizeOfImage together; both are in the mapped image, so an in-process module and the server
(reading the remote module's header) get the same answer. The file size is not part of the key (Steam's signature
differs per copy on build 4390913).

## 3. The symbol catalogue [W1, ticket 4960, 2026-11-19]

`exebuild/symbols/<area>.tsv`, one file per area (`proxy`, `shell`, `render`, `server`), tab-separated, `#` comments,
first non-comment row the header `name kind anchor_va consumer note`. `tools/exebuild/table.py` (`symbols()`) reads
and validates all files together.

- **name**: `<area prefix>_<rest>` in `[a-z0-9_]`, with at least one underscore, unique across all files (`cam_app`,
  `qg_iat_interlockedexchangeadd`). The underscore is what tells a symbol name from a hex VA in a pattern operand.
  C++ sees it as `VPX_<name>` (an enum value).
- **anchor_va**: the symbol's VA in build 4390913 at ImageBase 0x400000 (the value code and research already carry).
  Never another build's VA (D202). Unique per kind.
- **consumer**: the file that uses it. **note**: free text, except for `iat` and `array` (below).

| kind | what | carried to a new build by | anchor checked on build 4390913 |
|---|---|---|---|
| `code` | `.text` address (function or mid-function) | `addr_match.match_code`: masked bytes forward (16-160 B windows), then windows ending after the address, then the direct callers' vote; failing that, the entry of a carried vtable that holds it (`vtable-entry`) | in `.text` |
| `data` | `.data`/`.bss` | `match_data`: the instructions whose relocated operand is the address, matched by masked context, majority vote (`+off` from the nearest referenced address is low confidence: not taken) | in `.data`/`.bss` |
| `rdata` | `.rdata`, not a vtable | as `data`, with addr_match's vtable fallback | in `.rdata` |
| `vtable` | a vtable in `.rdata` | the references (xrefs) and `match_vtable` (entries carried as code and lined up), see 7.2 | in `.rdata` |
| `shader` | a blob in `.shader` | the section's shift (new `.shader` VA - old), confirmed by comparing the blob's bytes (the whole DXBC container, else 32 B) | in `.shader`, DXBC at the anchor |
| `iat` | an import slot; note = `DLL!Function` | the slot of the same function by name in the new imports; `d3dx9_4x`, `d3dx10_4x`, `d3dx11_4x` and `d3dcompiler_4x` count as one DLL (the 2026-11-19 update moved `_42` to `_43`) | the anchor is that import's slot |
| `array` | a bulk list; anchor_va `-`; note `file=<repository path>;cols=<kind>,<kind>,...` | each address cell by its column's kind (`code`, `data`, `rdata`, `shader`; `keep` = not an address, copied) | each cell in its column's section |

**Look-alike code (ticket 4960c, 2026-11-20) [C, generator runs on the golden copies].** W2 (4963) found two 0x20-byte
wrappers carried swapped with high confidence: `uid_delete_array` (0x813D10) and `uid_free_aligned` (0x813CF0). They
differ only in their call's rel32, which the mask hides, so the first 16, 24 and 32 masked bytes of each stand twice
in the old `.text` (2 hits each). On build 26577625 the 48-byte forward window, or the backward one, ran into the
wrappers' neighbours. The relink changed the neighbours' order, so the unique hit was the other wrapper. 0582's map has
the same swap, and no pattern of that time could see it.

Since then `addr_match.match_code` first asks `body_ambiguous`: do the 16 masked bytes from the address on also stand
elsewhere in the old `.text`? If so, the byte windows are not used. Such an address is carried only by something the
look-alike does not share: its callers' vote, or the routes of section 17. Without any of them the symbol is
`ambiguous`: not taken, listed in `review.tsv`, answered by hand. On the full catalogue the gate drops 19 symbols, and
section 17 carries all 19 to the same values as before.

The pattern token `C(name)` (section 4) makes such a swap visible at run time too. The destructor's calls
`E8 C(uid_delete_array)` at +0x13 and +0x2A and `E8 C(uid_free_aligned)` at +0x42 are BAD when the two values are
swapped. Tests: `test_look_alike_wrappers_are_not_carried_by_their_bytes` and `test_call_target_pattern_refuses_a_swap`.

The array file is the existing committed table (for example `render/src/shaders/create_sites_table.inc`, 1,417 rows
of `{v, v, ...},`: one row per line, `//` comments skipped). The render carrier adds it as
`render_shader_create_sites array - render/src/shaders/shadersection.cpp file=render/src/shaders/create_sites_table.inc;cols=shader,keep,keep,keep,code,code,data,keep`
(blob VA, size, stage, set, init function, call site, handle global, vertex mask). Taken: high and medium confidence;
low goes to `review.tsv` and the symbol stays unresolved.

The seed (ticket 4960): `shell.tsv` holds the 27 symbols of `shell/common/exe_camera.h` (14 data, 12 code, the FCamera
vtable; including the pattern operands `cam_world_identity` 0x2DD1FD0, `cam_fcamera_dtor` 0x8E5320 and
`cam_fcamera_update` 0x8E4D70, which have no `VPCAM_VA_` define); `proxy.tsv` one import slot
(`qg_iat_interlockedexchangeadd`, 0x9D647C, `KERNEL32.dll!InterlockedExchangeAdd`) and one vtable (`ses_gvs_vtbl`,
GameViewState 0xA20908); `render.tsv` and `server.tsv` only the header. The carrier workers fill the rest.

## 4. Pattern templates (the run-time guard) [W1, 2026-11-19]

Rows `X(addr, "name", "bytes")` in any source file listed in `exebuild/patterns.txt` (today `shell/common/exe_camera.h`);
`gen_table.py --patterns FILE` adds more for one run. `addr` is a hex VA of build 4390913 (`0x51DF70`, today's form), a
symbol name, or `VPX_<name>`. The bytes:

| token | meaning |
|---|---|
| two hex digits | a literal byte (pairs may be written without spaces: `8B0D`) |
| `??` | any byte |
| `R(name)` / `R(VPX_name)` | 4 bytes holding the symbol's VA in the running build, rebased on the module base |
| `R(hexVA)` | the same for the symbol whose anchor is that VA (no `0x`: `R(CC8E74)`, today's form); a VA that is no catalogue anchor makes the pattern BAD |
| `C(name)` / `C(hexVA)` | right after a literal `E8` or `E9` only: its rel32 must lead to that symbol's VA in the running build, so the call's target is checked rather than freed. Added 2026-11-20, ticket 4960c, after look-alike wrappers were carried swapped (section 3, "Look-alike code"). Generator: `expected_bytes(toks, resolve, at_va)`. Run time: `VpExeExpandPatternAt` and `VpExeCheckPatternVa`/`Sym`/`Anchor`; `VpExeExpandPattern` without the address refuses a C token. |

A literal `E8` or `E9` followed by four literal or `??` tokens frees those four bytes: a call's or jump's rel32
changes with every link, as every function moved in the 2026-11-19 relink. An `E8` followed by `R(...)` frees
nothing. The same rules are in the generator (`gen_table.py` `parse_template`, `expected_bytes`) and the run-time
checker (`exebuild/include/vp_exe_patterns.h` `VpExeExpandPattern`), so a pattern that passes generation passes at run
time on the same build. (The pre-0590 `VPCamExpandSig` of `exe_camera.h` compares the rel32 too; it is replaced when the
camera carrier moves `exe_camera.h` to the table.)

## 5. The local files [W1, 2026-11-19]

All under `<exe_tables>/<steam build>/`, the setting `exe_tables` (`deps/local-settings.ini`, workstation value
`C:/c5-64bit-work/exe-builds`; environment variable `C5_EXE_TABLES`). Stored locally only (D202); `.gitignore` refuses
`table.json`, `hand-review.tsv`, `review.tsv`, `vp_exe_tables.gen.h`, any `exe-builds/` folder and
`map-product-vas-*.csv` anywhere in the tree, and `tools/exebuild/tests` checks that `git ls-files` holds none of them
and that `git add` refuses a `table.json`. Dated backup copies: `F:/c5-64bit-work/exe-builds/<date>/`.

- **`table.json`** (format 1): `steam_build`, `stamp`, `size_of_image`, `build_known`, `anchor_build`, `generated`
  (local time), `mode` (`identity` or `carried`), the SHA-256 of both EXEs and of the catalogue (`catalogue_sha256`;
  a table made from another catalogue is stale and the emitter refuses it), `symbols` (name -> `va`, `kind`,
  `method`, `votes`, `confidence`: `identity`, `high`, `medium` or `hand`), `unresolved` (names), `arrays` (name ->
  `file`, `cols`, `rows` with hex address cells and 0 for a cell not carried, `unresolved_cells`), `patterns` (file,
  name, the symbol it sits at, `OK`/`BAD`, reason; no bytes) and `complete` (no unresolved symbol or cell, no BAD pattern).
- **`hand-review.tsv`**: `name<TAB>va<TAB>research reference`, a header row `name va reference` allowed; `name[row][col]`
  for an array cell. It overrides the matcher (method `hand: <reference>`, confidence `hand`); a name not in the
  catalogue is refused. The reasoning goes to research with the anchor VA only; the answer's VA stays in this file.
- **`review.tsv`**: written by every run: the symbols and cells not carried with high or medium confidence, with the
  matcher's candidate, method and votes. The person's worklist for `hand-review.tsv`.

## 6. Commands and build integration [W1, 2026-11-19]

    python -I tools/exebuild/gen_table.py --identity --old <DX11 EXE of build 4390913>
    python -I tools/exebuild/gen_table.py --old <DX11 EXE of 4390913> --new <DX11 EXE of N> --steam-build N
    python -I tools/exebuild/emit_header.py --out <gen folder>

**Making a checkout's tables in one step (ticket 4960b, 2026-11-20).** On the workstation, run these two commands
from the checkout whose build refuses. They read the golden copies, write
`<exe_tables>/<build>/table-<catalogue hash16>.json` and `table.json`, and print no address:

    python -I tools/exebuild/gen_table.py --identity --old F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe
    python -I tools/exebuild/gen_table.py --old F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe --new F:/c5-64bit-work/golden/civ5-steam-build26577625-ws/CivilizationV_DX11.exe --steam-build 26577625

The emitter's refusal prints these same commands (`table.gen_commands()`).

**One exe_tables folder, several catalogues (ticket 4960b) [C, 2026-11-20].** Every checkout and branch on the machine
shares `<exe_tables>`, but the carriers add catalogue rows on their own branches. Before this change the emitter
refused a `table.json` made from another catalogue as stale, so whoever generated last broke everyone else's build.
How it works now:
- `gen_table.py` writes `table.json` and also `table-<first 16 hex of the catalogue hash>.json`.
- `emit_header.py` (and `table.load(..., chash=)`, `table.va`) takes the copy for its own catalogue first.
- `table.json` stays "the last one generated", for checkouts older than this change.
- `hand-review.tsv` answers for a symbol the running catalogue lacks are skipped with a note. A malformed name is
  still exit 2.
- The catalogue hash also covers the rows of every array file.
- When neither file fits, the emitter refuses with exit 2, names the catalogue hash and the missing file, and prints
  the two commands above.

`.gitignore` covers `table-*.json`, and `table.tracked_forbidden()` reports any such tracked file. Tests:
`test_two_catalogues_share_one_folder_and_each_emitter_picks_its_own` and
`test_only_another_catalogues_table_is_refused_with_the_commands`.

`gen_table.py` exits 0 (written, complete), 4 (written, incomplete: answer `review.tsv` in `hand-review.tsv` and run
again), 1 (refused: a BAD pattern not named by `--allow-bad NAME`; nothing written but `review.tsv`), 2 (input,
catalogue or setting error). It prints names, counts and OK/BAD only: no EXE bytes, no VA of the new build. `--old`
must be the anchor build (stamp and SizeOfImage); a `--new` whose header is not in `VPEXE_KNOWN_BUILDS` is carried
with a note to add it to `vp_exe_build.h` and `builds.py` (the emitter needs it there). Golden copies on the
workstation: `F:/c5-64bit-work/golden/civ5-steam-build4390913-ws` and `.../civ5-steam-build26577625-ws` (read only).

`emit_header.py` writes `vp_exe_tables.gen.h` (enum `VpExeSym` `VPX_<name>`, `kVpExeSymNames`, `kVpExeSymKinds`,
`kVpExeSymAnchor`, `kVpExeSymVa[build][sym]` with 0 = not carried, `kVpExeBuildComplete[build]`, enum `VpExeArr`
`VPXA_<name>` and `kVpExeArrays`) from the catalogue and the table of every build in `VPEXE_KNOWN_BUILDS`, only when
its text changes. It fails (exit 2) naming `exe_tables`, `C5_EXE_TABLES` and the generator command when the setting is
missing, a known build has no table, a table is stale, or the anchor build's table disagrees with the catalogue.

Committed headers in `exebuild/include/` (C++03 for the proxy's VC9 toolchain, x64 for the server and render/):
`vp_exe_build.h` (the build list, `VpExeBuildOfImage`), `vp_exe_symbols.h` (`VpExeVa(build, sym)`, `VpExeRva`,
`VpExeRebase`, `VpExeAddr`, `VpExeBuildSupported(build)` = known and complete, `VpExeSymName`, `VpExeBuildLabel`,
`VpExeSymByName`, `VpExeSymByAnchor`, `VpExeArray`), `vp_exe_patterns.h` (`VpExeCheckPatternSym`, `...Anchor`,
`...Va`: returns 1, or 0 with a one-line reason and the first mismatching offset; reads through a caller's reader, or
in process through `VirtualQuery` first, so an unreadable page is a mismatch, never a fault).

| Build | Emitter output folder | Include path added |
|---|---|---|
| `proxy/build_proxy.py` (step 2b) | `<out>/gen-exebuild` | the proxy's compile flags |
| `server/build_server.py` | `<out>/gen-exebuild` | the server's units and the render units (`render_compile_args`) |
| `render/build.py` | `<build tree>/gen-exebuild`, passed to CMake as `C5R_EXEBUILD_GEN` | `c5render` PUBLIC (CMake fails without it) |
| `shell/build.cmd` (step 0a) | `%C5_SHELL_BUILD%\gen` | every `cl` line but MinHook's (`%EXEINC%`) |

**Every consumer rebases on the running module's base** [C, code read by the third lead-0590 on the merged state
f21029f6a, 2026-11-20 05:55 EST; prompted by epic 0042's spike 4798, which saw build 4390913's EXE loaded at base
0x00970000 on a Goldberg instance and crashed a prototype that used a 0x400000-based VA unrebased]. The table holds VAs
at ImageBase 0x400000; the EXE is relocated live, so no table value may be used as a pointer. Checked: `VpExeRebase` /
`VpExeAddr` (`va - VPEXE_IMAGE_BASE + moduleBase`), `ProxyExe::Addr`, `ShellExeAddr` and `ShellExeLay`, exe_camera.h's
`VPExeCheckSigs` / `VPExeLaySigs` (own constant `VPCAM_IMAGE_BASE`; corrected 2026-11-24, ticket 0603: that constant and
those helpers are gone, every consumer uses `VPEXE_IMAGE_BASE` and the helpers of `exebuild/include/vp_exe_sigs.h`,
section 18), the server's EXE-field reader
(`srv_liveterrain_exefield.cpp`: the remote main module's base from `EnumProcessModules`, then `VpExeAddr`), and
`resolve.py`'s `Resolver.at(base, name)`. Every product caller passes a base taken at run time
(`GetModuleHandle(NULL)` in the proxy, the composite's `g_exeBase`, the remote module base in the server); no product
source casts a constant VA to a pointer (grep of `exebuild/include`, `proxy/src`, `proxy/include`, `shell/composite`,
`shell/common`, `server/src` for pointer casts of 6-8 digit hex constants: 0 hits). Tests that fail if a consumer did not
rebase: `tools/exebuild/tests/native/exe_tables_test.cpp` ("rebase": `VpExeAddr` at base 0x10000000, and a pattern
check on a fake image in a static buffer), every shell suite run on `test_host --fake-exe` (the fake image is a buffer the host
allocates and names through `VPSHELL_EXE_BASE`, not at 0x400000: test_camera, test_unit_filter, test_model_filter, the tap tests),
`proxy/tools/rt_tests/session_unit.cpp` (fake image, `VpExeRebase`), `shell/tests/farm_guard_test.cpp` and
`tools/exebuild/tests` for `Resolver.at`. Live: 4967 part 2's arms B and C ran every feature group on both builds [L]
(the load base of those runs was not logged).

## 7. Results on the two builds, pilot catalogue [C, generator runs 2026-11-19 between 22:05 and 22:41 EST (clock reads), golden copies]

### 7.1 Counts

| | build 4390913 (`--identity`) | build 26577625 |
|---|---|---|
| symbols | 29 of 29, identity | 29 of 29: 28 high, 1 medium (`cam_viewport`, one reference) |
| methods | - | 11 code by bytes (`bytes16`-`bytes32`, one `back16`), 1 code by vtable entry (`cam_fcamera_dtor`), 14 data by xrefs (3 to 59 agreeing references each), 2 vtables by xrefs + entries, 1 import by name |
| `exe_camera.h` patterns | 11 of 11 OK | 11 of 11 OK |
| run time | 1.3 s | 2.9 s |

Cross-check with ticket 0582's map (`map-product-vas-from-0582.csv`, the lead's local copy beside the table): 24 of the
29 symbols are in it, and all 24 agree. The 5 not in it are the import slot, the GVS vtable (0582 carried both in its
second pass, not in the map), `cam_world_identity`, `cam_fcamera_dtor` and `cam_fcamera_update` (pattern operands
without a define, so not among 0582's 491 literals). `tools/exebuild/tests/test_exebuild.py::test_pilot_values_agree_with_0582_map`
repeats this check.

### 7.2 Vtables: a derived class ties the entry vote [C, 2026-11-19]

First run: `cam_fcamera_vtbl` (0xA9A1B0) and `cam_fcamera_dtor` (0x8E5320) unresolved, so the FCamera vtable pattern
was BAD and the run was refused (exit 1). Cause: the FCamera destructor is not unique by its bytes, so `match_code`
carries only entries [1] (Update) and [2] (GetViewProj); `match_vtable` then finds two `.rdata` places where both
carried entries line up with 2 votes each (a second vtable, a class that keeps FCamera's Update and GetViewProj and
has its own destructor) and returns the first; the references (`xrefs`, the store in the constructor) name the other
one with medium confidence. Rule since then (`gen_table.py` `carry_vtable`): a candidate (xrefs first, then the entry
vote) is taken only with high or medium confidence AND every carried entry equal at its index AND both vtables of the
same length (the run of consecutive `.text` pointers; MSVC puts the next vtable's RTTI locator between two vtables);
then a `code` symbol the matcher could not carry is read from the carried vtable's entry at the same index
(`vtable-entry`). Result: both vtables high, the destructor by entry [0], 11 of 11 patterns OK, and the xrefs answer
is the one 0582 found (B.2: "FCamera ... 3 = 3").

## 8. Procedure for the next EXE build (a Firaxis hotfix) [W1, 2026-11-19]

1. Copy the new install to a golden folder on F: (read only from then on) and read its DX11 header with
   `tools/exebuild/builds.py` (`of_file`, `read_header`).
2. Add the build to `VPEXE_KNOWN_BUILDS` in `exebuild/include/vp_exe_build.h` and to `KNOWN` in
   `tools/exebuild/builds.py` (same id, next number; `tools/exebuild/tests` keeps them equal).
3. `python -I tools/exebuild/gen_table.py --old <4390913 golden EXE> --new <new golden EXE> --steam-build N`.
   Exit 0: go to 5. Exit 1: a pattern is BAD (a changed function, or a wrong carry): read `review.tsv` and the BAD
   names; exit 4: unresolved symbols or cells.
4. For each name in `review.tsv`: find the address by hand (disassembly in scratch only, D83), write
   `name<TAB>va<TAB>research reference` into `hand-review.tsv`, and the reasoning (anchor VA, method, no new VA) into
   research. Run step 3 again until exit 0. `--allow-bad NAME` only after a person decided the pattern itself must
   change (then change it in the source and regenerate).
   **Rule: a `high` carry of a short or look-alike function is not proof (ticket 4963, 2026-11-20; section 11.2,
   cases 5 and 6).** The matcher carried two near-identical 0x20-byte wrappers to build 26577625 swapped, with high
   confidence, and 0582's map has the same swap: the two differ only in a call's rel32, which the byte vote masks, and
   the relink changed their order in `.text`. So before a carry is handed on, every `code` symbol that is short (under
   about 0x30 bytes to its return), a thunk or wrapper, or one of several functions with the same masked bytes, is
   cross-checked by what the vote does not use: its direct call targets in order (each old target carried and compared
   with the new one), its operands, or a caller that names it at a known offset. A short function without callees is
   checked by its callers or by the vtable slot that holds it. Agreement with 0582's map is not such a check (same
   matcher). Where a wrong address would be called or written through, the product keeps a run-time cross-check too
   (uidraw's rel32 targets are the model). The carrier's research subsection lists which symbols were checked and
   how; the merge request of a carry says so for every short function in it.
5. Every build regenerates the header from the tables (the four build scripts call `emit_header.py`); build all four.
6. Copy `<exe_tables>/N` to `F:/c5-64bit-work/exe-builds/<date>/` (backup), and run the live smokes of the features
   on the new build. Estimate 0582 B.4: half a day to a day for a relink with the same compiler.
7. **Rule for carriers: the acceptance lists every test that compiles a converted file (ticket 0605, 2026-11-24; the
   MC numbers it in COMMON).** A carry (or any change to a product source) runs
   `python -I tools/exebuild/compiled_by.py <every touched source and header>` and lists in its acceptance each test
   target and script it prints (tests that compile or name the file, and the partner tests); the hand-back shows each
   one built and passing, or says why one cannot run there and who runs it. Why: carry 4965 (section 15) converted
   `shell/composite/farm_guard.cpp`, which `shell/tests/farm_guard_test.cpp` (built by `shell/build.cmd`, run by
   `shell/tests/test_farm_guard.py`) and `proxy/tests/test_farm_guard_proxy.py` compile; neither was in 4965's
   acceptance, so the offline farm guard test did not compile from 4965 until lead-0590's fix c9b4046fd, and nothing
   failed loudly because no suite the carrier ran built it. The tool's output for `proxy/src/proxy_farm_guard.cpp`
   names both (the first directly, the second as a partner through `farm_guard.cpp`); for `farm_guard.cpp` both
   directly `[C]`, run 2026-11-24 on main 9bda43beb. It also catches a test that reads the C++ as text (0627's
   `test_model_programs_table.py` for `unit_filter.cpp`, section 15). What it does not see: its docstring (sources
   passed on another script's command line, f-strings, generated sources); the runbook's 2.10 table stays a cross-check.

## 9. Not verified [2026-11-19]

- No product code reads the table yet (the carrier workers convert the consumers on top of this); the run-time pattern
  checker is tested on a fake image only (`tools/exebuild/tests/native/exe_tables_test.cpp`, x86 C++03 and x64), not
  in the game.
- Only the pilot catalogue (29 symbols) is committed and carried; the other ~460 product addresses were not run through
  this generator. The create-site array was, as a scratch experiment (section 10); its 17 values left are not reviewed.
- The generated header compiles with the proxy's x86 toolchain (clang-cl -m32, VS 2008 headers, `-std=c++03`) and an
  x64 cl in the tests; the four builds' results are in section 10.

## 10. The four builds with the emitter, and the create-site array [C, workstation runs 2026-11-19 22:42-23:05 EST]

Branch `task-0590-c1-build-table`, DLL source = `git archive` of the pin 1fd51353e (`deps/dll.lock`), every build through
`quietlock.py run` and its own gate, build trees under `C:/c5-64bit-work/4960/`:

| Build | Result | Time |
|---|---|---|
| `render/build.py --build-dir ...` (all targets) | built; emitter into `<tree>/gen-exebuild` | 3 min (22:42:18-22:45:21) |
| `server/build_server.py --src ... --out ...` (debug; build_all, c5render, server.exe, then the proxy) | `build_server: OK`; the proxy `build_proxy: OK` with step 2b `[exebuild]` | 662 s (build_all 387 s) |
| `shell/build.cmd` (C5_SHELL_BUILD set) | `BUILD OK`, no compiler warning or error in the log | under 5 min (not timed) |

A first shell run failed at `lk_late_test.cpp`: the Edit tool had dropped the space after `%EXEINC%`, so the
following `/I` or `/DUNICODE` ran into the quoted folder name (the lines with `/DUNICODE` compiled without it).
Fixed in 2d3e7d3c4; a check that every `cl` line has `%EXEINC% /` is the quick test.

**The create-site array** (an experiment in a scratch catalogue and scratch tables, not the committed catalogue: the
render carrier owns the row; method as in section 3, `cols=shader,keep,keep,keep,code,code,data,keep`): build 4390913
identity in 0.7 s; build 26577625 in 22 s, exit 4 (incomplete), 41 of 5,668 address cells not carried:

| column | cells | distinct values | not carried |
|---|---|---|---|
| blob VA (`shader`) | 1,417 | 1,417 | 0 (the -0x170000 shift, bytes equal) |
| init function (`code`) | 1,417 | 2 | 0 |
| call site (`code`, mid-function) | 1,417 | 1,413 | 2 cells (2 values) |
| handle global (`data`, 16-bit globals) | 1,417 | 493 | 39 cells (15 values) |

The 17 values left are for `hand-review.tsv` (or the handle column is read from the carried call site's push, a
choice for the render carrier); the 11 camera patterns stayed OK in the same run.

The native unit test (`tools/exebuild/tests/native/exe_tables_test.cpp`, 48 checks) also ran once under MSVC
`/fsanitize=address` (x64, 2026-11-19, finished before 23:12 EST): 48 of 48, no report. Its first ASan run found one
check that assumed the fake image's low address byte (ASan aligns globals differently); the check, not the header,
was corrected (988539361).

## 11. The proxy carry (ticket 4963) [C, generator and disassembly-free byte reads of both builds' golden copies, 2026-11-19/20]

Branch `task-0590-c4-carry-proxy` (W2-0590). Every EXE address of `proxy/src` is a catalogue symbol
(`exebuild/symbols/proxy.tsv`, 53 rows: the two seed rows and 51 new ones; g_Civ5App is shell.tsv's `cam_app`, used,
not duplicated). No stamp constant and no EXE VA/RVA literal is left in the code of `proxy_session.cpp`,
`proxy_shell.cpp` (view mask, white mask, model fog), `proxy_queue_guard.cpp`, `proxy_uidraw.cpp`, `proxy_pacing.h/.cpp`
(the RVA block only: `Rvas()` / `SelectLayout`), `proxy_leaderscene.h/.cpp` and `proxy_farm_guard.cpp` (comments keep
old-build VAs, labelled). The tables for this work were generated into a private folder
(`C5_EXE_TABLES=C:/c5-64bit-work/4963/exe-builds`) so the shared one stayed the lead's.

### 11.1 How the proxy reads the table

`proxy/src/proxy_exe.h` (header only, C++03): `ProxyExe::BuildOfImage` (the build from the image's own header behind
VirtualQuery), `Va` / `Rva` / `Addr` (with a test-only hook `Test::DropSymbol` that makes one symbol "not carried" in
every build, so each feature's refusal is tested with complete tables), `UnknownBuild` / `NotCarried` (the one-line
reasons) and `CheckPattern` (vp_exe_patterns.h with the in-process reader). Each feature finds the build from the
image it is given (a fake image of either build in the unit tests), never from a global:

| Feature | Unknown build | Symbol not carried |
|---|---|---|
| session masks (`ReadExeMasks`) | config.ini's keys (0588) | no mask, D33 fail loud |
| view mask, white mask, model fog | off, one line (as before) | off, the symbol named |
| queue guard | off, quiet (as before) | off, LOUD, named |
| uidraw | off, quiet (as before) | refused, loud (report), named |
| pacing options 2-4 | refused (the existing report) | refused, named |
| leader scenes | off, one line | off, LOUD, named |
| farm guard (proxy side) | attaches, reads nothing | n/a: `shell/composite/farm_guard.cpp` holds the addresses (4964/4965); the proxy accepts build 4390913 only until then (`GuardKnowsBuild`, TODO) |

Patterns: the proxy's 29 pattern rows are X rows (`X(symbol, "name", "bytes")`) in `proxy_shell.cpp` (view mask 6,
white mask 12, cloud base 4) and `proxy_uidraw.cpp` (7); both files are in `exebuild/patterns.txt`. The shell's former
operand tables are `R(symbol)` bytes inside the rows (an operand that does not name the build's address is reported
as "its operand R(name) at +N does not name the build's address"; g_Civ5App keeps "g_Civ5App is not where the mask
code reads it"). uidraw keeps #13406's 22 cross-checks (abs32, rel32 call targets, val8/val32), now by symbol.

### 11.2 Carry results and the hand cases (build 26577625)

| | count |
|---|---|
| proxy symbols (incl. cam_app) | 54 |
| carried automatically | 48 (47 high, 1 medium: `pac_hold`, xrefs) |
| by hand (`hand-review.tsv`, local) | 6 |
| methods | bytes16-96: 30, xrefs: 16 (+1 vtable by xrefs and entries), import by name: 1 |
| patterns, both builds (`gen_table.py`) | 40 of 40 OK (11 camera, 22 shell, 7 uidraw) |
| run-time checks beyond the patterns, both builds (scratch byte reads) | all OK after the hand cases: uidraw 22/22 cross-checks; pacing's sections, the vftable's 264 code entries, slot -1 = 0, slot 102 = `pac_secs_per_turn`; queue B 0xC08 after its index; the queue guard's slot by name; the scene cache and the white-mask globals in writable data; the White.dds string |

The hand cases (VAs of build 4390913; the answers' new VAs are in the local `hand-review.tsv`):

1. `pxw_white_path` (0xA3D66C, `.rdata` string `assets\terrain\editor\White.dds`): the matcher found no reference by
   context. The carried `pxw_editor_site` pushes it (`68 imm32` at +6, operand at +7) in both builds; the new operand
   points at the same string. [C, byte reads of both EXEs, 2026-11-19]
2. `pac_util_vftable` (0x9EA718): low confidence ("no candidate with all carried entries agreeing", 0 xrefs found).
   The candidate has 264 consecutive `.text` entries as the old one, the entry after them is not code, slot -1 is 0,
   all 23 carried entries agree, two relocated references in each build, and it is the only `.rdata` table holding the
   new slot-102 function. [C, 2026-11-19]
3. `pac_secs_per_turn` (0x704B60, `fld dword [const]; ret`, not unique by bytes): the carried vftable's entry 102;
   the same two instructions, the constant 0.1 in both builds. [C, 2026-11-19]
4. `lsc_scene_map` (0xDA27A4 = the leader-head object 0xDA2780 + 0x24): the matcher's "+off" vote (51/51) is low
   confidence by rule. The object is referenced 101 times in each build, its +0x24 never directly; the answer is the
   carried object + 0x24. [C, 2026-11-19]
5. and 6. `uid_delete_array` (0x813D10) and `uid_free_aligned` (0x813CF0): **the matcher carried both with high
   confidence and swapped them.** The two are 0x20-byte wrappers that differ only in their call's rel32 (both call
   the same function in each build); in build 26577625 their order in `.text` is the other way round, and the
   masked-bytes vote cannot tell them apart. uidraw's run-time cross-check (the CCommandStream destructor's calls at
   +0x13 and +0x2A must go to operator delete[], the one at +0x42 to FireFreeAlignedNoTracking) would have refused the
   growth on the new build. The answers are the destructor's own call targets. [C, byte reads of both EXEs, 2026-11-20]

Lesson for the generator [I]: a `high` carry is not proof for small near-identical functions; `gen_table.py` checks
pattern bytes, not rel32 call targets (`E8 R(name)` cannot express one), so a cross-check of that kind only runs in
the product. A rel32-target token in the pattern syntax (for example `C(name)`: "a call to name") would let the
generator catch such a swap; proposed to the 4960 owner, not built.

0582's map (`map-product-vas-from-0582.csv`) agrees on 18 of the 20 proxy symbols it holds by VA; the two others are
the swapped wrappers of cases 5 and 6, which the map carries the same wrong way (the same matcher). **Carriers who
cross-check against that map: an agreement there is not independent evidence for near-identical small functions.**
Its rows for
the proxy's RVA literals (0x620908, 0x9A12B4, ...) are carries of those numbers read as VAs, not of the addresses
they stand for: not comparable, and not a check of anything.

### 11.3 Tests [2026-11-19/20, private tables, both builds' layouts]

The unit tests build their fake images from the table (the generated header; a missing or stale table fails naming
`exe_tables`, never skips) and run on every known build: `session_unit` (masks on both builds; 137), `queue_guard_unit`
(234, each symbol dropped), `leaderscene_unit` (1570), `pacing_unit` (650; the fake's sections are laid out around each
build's addresses; queue B's index and buffers lie on two pages in 26577625), `uidraw_unit` (274), `shell_unit` (560;
the fake's code is written from the product's own rows, expanded per build). `test_proxy_rt.py` gained
`--only-offline` and runs the leader-scene and pacing units (before: only their evidence scripts). The proxy builds
(`build_proxy.py`, step 2b). Results of the full suites: ticket 4963's log.

## 12. The Python tools that read a running game: `tools/exebuild/resolve.py` [ticket 0593, 2026-11-20]

Nine tools used to check `TimeDateStamp == 0x546CD0A8` and carry their own VA literals: `perf/quietlock/quietlock.py`,
`perf/turnstudy/pacing/poller.py`, `perf/turnstudy/budget/stock/stockthreads.py` and `exeprofile.py`,
`shell/tools/view_mask.py` and `gt_reader.py`, `tools/i0b/read_masks.py`, `tools/testbed/jobs/release_d33_runtime.py`,
`server/tests/units/live/art_records.py`. Since ticket 0593 they name every EXE address by a catalogue symbol and get the
build and the VA from `tools/exebuild/resolve.py` (standard library, Python 3.8 grammar; each tool loads it by path as
the module `c5_exebuild_resolve`, so no `sys.path` change is needed).

| Call | Result |
|---|---|
| `resolve.for_header(stamp, size_of_image)` | a `Resolver`; `UnknownBuild` (names both values and the known builds) when the pair is not in `builds.KNOWN` |
| `for_file(path)`, `for_build(build)` | the same from an EXE file's header, or a `Build` / steam build / id |
| `header_of_image(read, base)` | (stamp, SizeOfImage, machine) of a mapped image through the tool's own reader |
| `Resolver.va(name)`, `.rva(name)`, `.at(base, name)` | the symbol's VA (ImageBase 0x400000), RVA, or address rebased on the module base |
| `Resolver.of_anchor(va)` | the running build's VA of the symbol whose anchor is `va` (gt_reader's `Reader.va()` and `.rva()` keep taking anchor VAs, so the evidence scripts that call them keep working on build 4390913 and fail loud elsewhere) |

Rules [design, ticket 0593]:
- **Build 4390913 needs no local table**: it resolves from the committed anchors, so the tools keep working where no
  `exe_tables` setting exists (oldpc, the laptop, every testbed job). Any other known build loads
  `<exe_tables>/<steam build>/table.json` once; a missing setting or table raises `table.TableError` naming `exe_tables`,
  `C5_EXE_TABLES` and the generator command, and a symbol the table does not carry raises `NotCarried` naming it. Each
  tool turns these into its own refusal (poller exit 4, stockthreads snapshot exit 4, gt_reader `Refused`, read_masks
  `ok: false` with the reason, art_records exit 3, release_d33_runtime `OSError`, view_mask "refused: ...").
- **The tools' own symbols are in `exebuild/symbols/tools.tsv`** (21 rows at the hand-back; 16 since the merge into the
  team branch, 12.1: `pace_*` for the poller, `prof_*` for the
  stock instruments, `gtr_*` for gt_reader, `ar_unitsim`); symbols other areas already catalogue are used by name
  (`cam_app`, `cam_proj`, `cam_view`, `cam_numviews`, `cam_viewport`, `cam_wrapx`, `cam_gridptr`, `cam_aabb`,
  `cam_period`, `cam_worldh`, `cam_w2h`, `cam_h2w`, `cam_grid`, `ses_gvs_vtbl`). `gen_table.py --identity` with the
  50-row catalogue on the 4390913 golden copy: 50 of 50, every anchor in its section, 11 of 11 patterns OK [C,
  2026-11-19 23:37, scratch tables under `C:/c5-64bit-work/0593/exe-builds`].
- **Overlap with the carriers**: one row per anchor and kind is allowed (`table.symbols()`). These anchors of
  `tools.tsv` are also used by product code and may be added by the carrier tickets: 0x2A10200, 0x2A10E08, 0x9EA718,
  0x2DE0447, 0x2DE044C (`proxy/src/proxy_pacing.h`; **done at the merge, 2026-11-20: the proxy carry had them, so the
  five `tools.tsv` rows are gone and the poller uses `pac_qb_index`, `pac_qb_buf`, `pac_util_vftable`,
  `pac_ai_turn_byte` and `pac_hold`; they were `pace_qb_index`, `pace_qb_buf`, `pace_eu_vtbl`, `pace_ai_turn`,
  `pace_hold`**), `gtr_invview` 0xDA1410 (**done at the merge of shell carry A, 2026-11-20: 0xDA1410 is now
  `ls_invview0` of `shell.tsv` (the lockstep probe's &g_InvView[0]); the `tools.tsv` row is gone and `gt_reader.py`'s
  `SYM` names `ls_invview0`**), `gtr_bb_h` 0x3010568, `gtr_bb_w` 0x301056C
  (`render/src/terrainsets/sets.cpp`, `server/src/render/srv_render_scene.cpp`), `gtr_height_factor` 0x9F9E34
  (`render/src/forest/placement.cpp`). When both land, one row stays and the other side uses its name (each tool names
  its symbols in one place: `SYM` / `SYM_*` at the top).
- **The tools' symbols count for a build's completeness**: the generator treats them like any other row, so a
  `tools.tsv` symbol that does not carry makes the build's table incomplete (exit 4, `kVpExeBuildComplete` 0) until it is
  answered in `hand-review.tsv`. Adding rows also changes `catalogue_sha256`: every local table must be generated again
  before `emit_header.py` accepts it.
- **Structure offsets are not in the table**: the tools keep their field offsets (`app+0xC0`, `GVS+0x1B4`, `+0x8CC`,
  `app+0x1460`, the queue stride 0x4C, art-record offsets, GridHeightmap fields). 0582 found offsets unchanged between the
  two builds [I for these tools: not read live on build 26577625].

Per tool [C: code and hermetic tests, 2026-11-20; no live run]:
- **poller.py**: `check_target` resolves its 12 symbols into the layout's `_va` (never written to `pacing-run.json`:
  `hexd` leaves `_` keys out, D202) and checks SizeOfImage against them; the run record names `build` (was
  `kb_timestamp`). turn_a, the AI-turn byte and the hold are one 16-byte read when the build keeps them at +0, +7 and
  +0xC (build 4390913 does), else three reads.
- **stockthreads.py**: the main thread's start is the EXE file's `AddressOfEntryPoint` (RVA 0x527035 in build 4390913,
  equal to the start the proxy logged [C, header of the 4390913 golden copy read 2026-11-19 between 23:25 and 23:37 EST]), so it needs no symbol; the
  game-core thread's start is `prof_core_thread_proc`. The rules are read from the EXE file a thread's module path names,
  cached per path; the thread map records `exe_build`.
- **exeprofile.py**: the knowledge base's rows (`addresses.md`) are build 4390913's VAs, so on any other build (known or
  not) it names the build and leaves the subsystem names out, as it did for a foreign stamp. Mapping another build's
  frames to subsystems would need `addresses.md`'s rows carried (not done). Its `KB_CODE_RANGE` (build 4390913's
  `.text`, 0x401000-0x9D0000) is a parse filter for that file, not an address it reads.
- **gt_reader.py**: `grid()` reads the block in one piece when every field moved by the same amount (always on 4390913),
  else field by field (`block_hex` is then null); `camera()` and `grid()` add `build`.
- **quietlock.py**: a renamed game is recognised by the DX11 stamps of every known build plus the DX9 stamp of build
  4390913 (`DX9_PE_TIMESTAMPS`; the DX9 EXE of 26577625 was not read, D202's guard; it is still found by its image name).

Tests: `perf/tests/test_exebuild_tools_0593.py` (resolver, read_masks, view_mask, gt_reader on fake images of both builds
and fake tables with fictional VAs, the refusals, no stamp or anchor literal in the nine tools),
`perf/tests/turnstudy/test_pacing.py` (a second build's table, the split frame block, the refusals naming the
setting), `perf/turnstudy/budget/stock/tests/test_stock.py` (thread roles per build), `test_threads4671.py`.
Not verified: no tool has run against a live game of either build since the change. (The carry of the tool symbols to
build 26577625, open at the hand-back, is 12.1.)

### 12.1 The merge into the team branch: one row per anchor, and the carry to build 26577625 [C, lead-0590, generator and byte reads of both golden copies, 2026-11-20 02:05-02:25 EST]

- **One row per anchor.** `table.symbols()` refuses a second row for an anchor ("0x2A10200 is already catalogued as
  pac_qb_index"), so with the proxy carry (section 11) on the team branch the five pacing rows left `tools.tsv` and the
  poller and its tests use the proxy rows' names (the list is in the overlap rule above). Catalogue after the merge: 96
  symbols (shell 27, proxy 53, tools 16).
- **Carry.** `gen_table.py` on the two golden copies with that catalogue: build 4390913 identity 96 of 96, patterns 40
  of 40 OK; build 26577625 95 of 96 automatically (87 high, 2 medium, 6 by the proxy's hand answers), patterns 40 of
  40 OK. Of the 16 tool symbols 15 carried with high confidence (9 data or rdata by references, 6 code by bytes); the
  sixteenth is the vtable below.
- **Cross-check of the 15, independent of the matcher's vote** (scratch script, not committed: for a code symbol the
  length of the masked-equal run from the address, its direct call targets in order, each old target carried and
  compared with the new one, and the number of direct callers and of relocated references; for data the number of
  relocated references in each build and where each old referencing site lands):

  | symbol | evidence, build 4390913 / build 26577625 |
  |---|---|
  | `pace_core_thread_ptr` | 15 / 15 references; 12 old sites land on it, 0 elsewhere |
  | `pace_tick_acc` | 4 / 4; 4 land on it |
  | `pace_turn_a` | 16 / 16; 6 land on it, 0 elsewhere (10 sites not unique by context) |
  | `pace_turn_b` | 4 / 4; 3 land on it |
  | `prof_core_thread_proc` | 256 B equal, its 1 callee agrees, referenced once in each build (the thread start operand) |
  | `prof_world_update` | 87 B equal, 3 of 3 callees agree, 2 / 2 callers |
  | `prof_gvs_update` | 80 B equal, no callee and no direct caller (a virtual): it is entry [3] of `ses_gvs_vtbl` (6 entries) in both builds |
  | `prof_queue_pumps` | 256 B equal, 9 of 9 callees agree |
  | `prof_core_thread_step` | 28 B equal, 2 of 2 callees agree; it is also entry [1] of the game-core thread vtable below in both builds |
  | `prof_update_request` | 256 B equal, 7 of 7 callees agree, 1 / 1 caller |
  | `gtr_invview` (now `ls_invview0`) | 18 / 18 references; 14 land on it |
  | `gtr_bb_h`, `gtr_bb_w` | 57 / 57 and 56 / 56; 56 and 47 land on them |
  | `gtr_height_factor` | 86 / 86 references; 53 land on it; the float's 4 bytes equal |
  | `ar_unitsim` | 38 / 38; 36 land on it |

  No old site lands on another address for any of them. None of the six code symbols is a short look-alike (the
  shortest equal run is 28 B with two agreeing callees).
- **The hand case: `pace_core_thread_vtbl` (0xA55BA0, the game-core thread object's vftable, 6 entries).** The
  generator left it unresolved ("no candidate with all carried entries agreeing", low). Two causes, both in the rules
  and not in the EXE: (a) the reference vote found nothing because the one referencing instruction's 52-byte context
  window differs in its last byte (the 50-byte window is unique); (b) the entry vote's candidate (4 of 4 carried
  entries at their index) fails the equal-length rule, because in build 26577625 the dword after the sixth entry is
  another code pointer (run of 7), where build 4390913 has data (run of 6). Reasoning for the answer: the vtable has
  exactly one reference from `.text` in each build, the store `C7 06 imm32` at +0x15 of the constructor; the old
  constructor is carried by its bytes (high) to the function whose +0x15 store names the candidate; the store's
  fixed context (48 B in a 60-byte window) occurs once in the new `.text`; all six entries are equal in their first 16
  or more masked bytes at the same index (entries [2] and [3] are one shared function in both builds); and the seventh
  code pointer of the new run is, in build 4390913, a function held at one `.rdata` place 0x4BF18 bytes away between
  non-code data, so it is a neighbouring object the relink put next to the vtable, not a seventh virtual [C for the
  bytes read; I that it is no added virtual: the poller only compares the object's vftable pointer with this address].
  Answer in the local `hand-review.tsv`. After it: 96 of 96, complete, and `emit_header.py` writes 96 symbols for both
  builds.
- **For the generator (not built):** a vtable whose entry vote is unanimous and whose single reference sits in a
  carried function at the same offset could be taken without the equal-length rule; the length rule was made for the
  FCamera tie (7.2), where two candidates had the same votes.

## 13. Render and server carry (ticket 4966) [W1, 2026-11-20]

### 13.1 What is carried, and what each literal is [C, source read 2026-11-19/20]

The 0582 inventory names these code-use literals in render/ and the server (`evidence/0582/inventory-vas-2026-11-19.csv`,
column `where` = code). Decision per literal:

| File | Literals | What they are | Done |
|---|---|---|---|
| `render/src/shaders/create_sites_table.inc` | 1,417 rows | The create-site list of build 4390913 | Array `render_shader_create_sites` (render.tsv). The file stays the committed anchor list. |
| `render/src/terrainsets/sets.cpp` | 120 (.data) plus blob VAs | Handle globals and blob VAs in the shader-set tables (`PassDesc`). The hex numbers in strings are provenance. | Kept as build-4390913 names. Blobs are resolved by `ShaderSection::findByVa`, which maps the name to the build's blob through the array. Handles are compared with each blob's create-site handle in build-4390913 terms. |
| `render/include/c5r/arc/arc.h` and the other `findByVa` users (water, ice, fow, decal, forest, bake, bakepost, terrain, units) | about 50 blob VAs | Blobs of `.shader` | Unchanged: build-4390913 names through `findByVa`. Every blob is a cell of column 0 of the array, so every one is carried (by the section shift, with the blob bytes compared). None needs a catalogue row of its own. |
| `render/src/liveterrain/producer.cpp` | 11 (.data) | Offsets inside the grid constants block (VA - 0xD90FBC) | Unchanged: they are relative to the block. The server checks per build that the block's fields keep their offsets (13.4). |
| `render/src/gamefs/dds.cpp` (5), `heightfield/heightfield.cpp` (4), `bakegrid/grid.cpp` (2), `shaders/pe.cpp` (2), `shaders/dxbc.cpp` (2) | 15 | DDS channel masks, feature bits, a float mantissa mask, a 16 MB read chunk, the DXBC MD5 table | Not addresses (they only fall inside the old image range). Left alone. |
| `server/src/render/srv_liveterrain_exefield.h` | 3 (grid, grid pointer, stamp) | Live-terrain field addresses | The `lt_*` rows of server.tsv, through `ExeFieldAddressesFor` (13.4). |
| `server/src/render/srv_liveterrain.h` `kConstantsBlockVa` | 1 | The block start | Kept as the build-4390913 name. Its one user, `srv_liveterrain_scene.cpp` `ReadConstantsBlock`, now reads `ExeFieldTarget::constantsAddr` (a one-line change forced in that file). |
| `server/src/remote/srv_exe_crash.cpp` | the stamp | The report's `[exe]` section | Names the build from the registry: either known build's label, any other "unknown build", with stamp and SizeOfImage. |
| `server/src/remote/srv_remote_lua.cpp` (4) | 0x00FFFFFF masks | Not addresses | Left alone. |
| `server/src/render/srv_render_scene.cpp` | none | It takes only exe_camera.h's tolerances and status names, no VA | Nothing to change (exe_camera.h is 4964's). |

### 13.2 The create-site array on build 26577625 [C, gen_table runs 2026-11-19 between 23:20 and 23:45 EST (clock reads); disassembly of both builds in scratch]

All 5,668 address cells are carried, with none answered by hand:

| Column | Cells | Distinct values | Carried by |
|---|---|---|---|
| Blob VA | 1,417 | 1,417 | The section shift (-0x170000), whole DXBC bytes equal |
| Init function | 1,417 | 2 | Bytes |
| Call site | 1,417 | 1,413 | Bytes for 1,411 values; row sibling for 2 (rows 805 and 806, build-4390913 call sites 0x4ED9C8 and 0x4E7F3F) |
| Handle global | 1,417 | 493 | References for 478 values; row sibling for 15 (39 cells), for example 0x2DE167C, 0x2DE16C8, 0x2DE1850, 0x2DE1A6C, 0x61969A0 |

The **row sibling** pass (`gen_table.py` `carry_by_row_siblings`, added for this) runs after the per-value carry has
left a cell. It finds the cell at the same distance from a carried cell of its own row, inside the same masked bytes:
- A call site is placed from the reference to the row's carried blob VA (the `push imm32` four bytes before the
  `call`).
- A handle global is read from the instruction at the same distance from the row's carried call site (the
  `mov word [handle], ax` after the call).

Two rules keep it safe:
- A sibling that gives more than one candidate is not used. A handle shared by the set-50 and set-41 calls has one
  look-alike store after each call. That is why the first version of the pass missed the two call sites.
- A value appearing in several rows is taken only when every row agrees.

Checks:
- The per-value matcher had failed on these cells because the create calls are long runs of look-alike code (push size,
  push blob, call, store handle), so no masked window is unique.
- Each of the 1,906 (column, value) pairs of the call-site and handle columns was checked: 0 odd. Every carried handle
  has exactly as many `.text` references in build 26577625 as in build 4390913, and every carried call site is an `E8`
  in both builds.
- `llvm-objdump` of row 805 in both builds (scratch only, D83) shows the same sequence in each: `movw %ax, <previous
  handle>`, `pushl <blob>`, `calll <CreateVertexShader>`, `movl <device>, %ecx`, `pushl $0x7`, `pushl $0x2cc`,
  `pushl <next blob>`, then `movw %ax, <this row's handle>`. Only the addresses differ.

**Code cells checked against swaps [C, 2026-11-20 about 01:30; lead's request after W2's finding].** W2 found that
`addr_match` carried two near-identical 0x20-byte wrappers swapped, with high confidence (proxy, 4963).

Each code cell of the array was checked independently:
- **Init functions** (2 values, 0x4E5C20 and 0x80CD10): large and unique by bytes.
- **Call sites, 1,413 of 1,417 rows** (`verify_rows.py`, scratch): within 48 bytes of the old call site there are a
  relocated reference to the row's own blob VA and a store to the row's own handle. On build 26577625 the same
  offsets from the carried call site hold the row's carried blob VA (carried independently, by the section shift) and
  the row's carried handle, and the carried call site is an `E8`. These are three independent carries agreeing, and a
  swapped call site would fail it.
- **The other 4 rows**: 2 call sites (0x4EEBF0 and 0x80DC1C) each shared by two rows of the anchor list. Checked by
  `llvm-objdump` of both builds (scratch, D83): the same sequence on both builds (`push 0xCDC` / `push <blob>`, which
  moved by -0x170000, then `call <CreatePixelShader>`, then the handle stores).

0 rows disagree.

### 13.3 The one hand answer: `lt_ginv` [C for the evidence, I for the value; 2026-11-19]

`lt_ginv` (0xD91210, block +0x254, the 16 ints the producer reads as Ginv) has no reference in `.text`: 0 relocated
operands equal it. The code reaches it through 0xD911D0 and an index; nearby referenced addresses are 0xD911D0 (8),
0xD911E0, 0xD911F0, 0xD91200 (1 each) and 0xD91250 (2). The matcher therefore has nothing to vote with.

The other 10 block fields carried by references with no dissent: `cam_wrapx` 16/16, `cam_gridptr` 47/47,
`cam_h2w` 59/59, `lt_corners` 3/3, `lt_plots_w` 47/47, `lt_plots_h` 49/49, `lt_g` 7/7, `lt_grid_samples_w` 1/1,
`lt_grid_samples_h` 1/1 and `cam_grid` 28/28. All of them moved by the same delta, -0x17AA50, as did the camera's
.data globals from 0xD90FA4 to 0xDA1310 (section 7).

The answer in the local `hand-review.tsv` is 0xD91210 + that delta. [I] It assumes `.data` kept this block's internal
layout, which every referenced field in it confirms. It is not confirmed by a reference of its own.

**Live, 2026-11-20 (ticket 0619) [L]:** confirmed by output. On build 26577625 (c5h-new-b, the flip's release layout) the
live terrain producer, which maps samples to plots through `Ginv` read from this block, wrote the `fh_hills` field window
with the same SHA-256 (d51d186d...) as four build-4390913 runs of the same save
(`research/05-graphics-composition/live-terrain.md` 10.8). The hand answer stands; `lt_ginv` is [L] now.

**The proxy's live reload gate (0586) was missed by the stage-2 carries** (found 2026-11-20 by the sixth lead, ticket
0619): `proxy_livereload.cpp` compared the stamp and three fixed 4390913 addresses and had no rows. It now uses the
shell rows `ice_reload`, `ice_ice`, `cam_grid` (no new row). The sweep of 0619 for other misses (hex constants in the
image's range in `proxy/`, `shell/`, `server/src`, `render/`, `options/`, `exebuild/include`, outside comments and tests;
every stamp and SizeOfImage compare) found no other functional one: the rest are build-4390913 names in log texts
(`proxy_shell.cpp` "0xDA12B0 ... ->" in the base-layer line; `srv_liveterrain_exefield.cpp` "[0xD90FC4] holds ..." in a
refusal text), descriptive strings (`render/src/terrainsets/sets.cpp`, `render/src/bake/pipeline.cpp`), the block-relative
offsets of `render/src/liveterrain/producer.cpp` `MapFrame::fromConstantsBlock` (anchor values as offsets into the block,
right on both builds by 13.3) and `srv_liveterrain.h` `kConstantsBlockVa` (kept as the 4390913 name, see 12). Stamp
compares: only the registries (`vp_exe_build.h`, `builds.py`, the installer's `KnownExeBuild`, `instance.py`) and tools.

### 13.4 The server's checks per build [C, source; offline test]

`ExeFieldAddressesFor(stamp, SizeOfImage, base)` (`srv_liveterrain_exefield.cpp`) refuses in three cases, each with a
one-line reason:
- an unknown build;
- a known build whose table does not carry one of the 11 block symbols. These are shell.tsv's `cam_wrapx` (the
  block's first field), `cam_gridptr`, `cam_h2w` and `cam_grid` (one row per anchor across all files), and server.tsv's
  7 `lt_*` rows. Corrected 2026-11-20: they were 11 `lt_*` rows, 4 of which repeated an anchor;
- a field whose offset in the block differs from build 4390913's. The producer parses the 880-byte block at fixed
  offsets, so a layout change must refuse rather than read wrong fields.

About a dozen test builders compile every file of `server/src/render` (`build_render_tests.py`, the fow, forest,
overlay, shadow and scene-bundle builders, sceneshot, units). They have no generated header and never open a live EXE.
`srv_liveterrain_exefield.cpp` therefore checks for the header with `__has_include("vp_exe_tables.gen.h")`:
- without it, `ExeFieldAddressesFor` refuses every EXE ("built without the EXE build table");
- the product (`build_server.py`) and `test_exefield_build.py` define `VP_REQUIRE_EXE_TABLES`, which turns a missing
  header into a compile error.

`srv_exe_crash.cpp` includes `vp_exe_build.h` by relative path, which needs no table. Found 2026-11-20 at about 01:20 (the run started after 01:17), when
`run_all.py`'s `build_render_tests.py` failed with "'vp_exe_symbols.h' file not found".

`OpenExeFieldTarget` keeps its existing pointer check per build: `[cam_gridptr]` must equal the rebased `cam_grid`.
`test_exefield.cpp` case `build_addresses` covers unknown, build 4390913 and build 26577625 (offsets +0x8 and +0x2B4).

### 13.5 ShaderSection per build: the policy [C, source; unit tests]

| EXE | What happens |
|---|---|
| Known build, array present and complete, every row's blob at its carried VA, every blob named by a row | Create sites applied. `va` = the build-4390913 VA, `exeVa` = this EXE's; create-site fields in both terms (`createSite`, `exeCreateSite`, ...). |
| Known build, no array for it in this binary | Parse fails, NotFound: "no create-site array ... setting exe_tables" |
| Known build, a cell not carried | Parse fails, NotFound: "row R column C ... is not carried" |
| Known build, a blob not at its carried VA, or a blob no row names | Parse fails, BadLayout |
| Unknown build (including the stamp of 4390913 with another SizeOfImage, and the tests' synthetic EXEs) | Split as before, `va == exeVa`, no create sites, `build() == -1`. A caller fails at its first `findByVa` (NotFound naming the VA). In the product, 0588 stops the 64-bit path before the server starts. |

Before 4966, build 4390913 with a mismatching blob applied nothing and returned Ok. That case is now a failure
(`negative_known_build_with_other_blobs_fails_loud`).

### 13.6 One exe_tables folder, several catalogues [C, 2026-11-19/20]

This was found here and is described in section 6. It was split out as ticket 4960b (branch
`task-0590-c1b-per-catalogue-tables`, 226a864cc) for the stage-1 merge.

Check, 2026-11-20, finished before 01:06 EST (clock read). Both builds' folders hold `table.json` and three `table-<hash16>.json` files:
- `2fb31ccaa0d2a560`, the stage-1 catalogue with 29 symbols;
- `19e1a4173b120db7`, this branch's catalogue with 36 symbols and the array;
- `28b2a4bc1c630e9a`, an earlier state of this branch.

The emitter of this checkout and the emitter run with the stage-1 catalogue each found their own table (exit 0, 36 and
29 symbols). `table.json` was then made the stage-1 catalogue's again, for checkouts without 4960b.

### 13.7 Tests on both builds [C, workstation runs 2026-11-20 00:09-00:34 EST]

`render/build.py --build-dir C:/c5-64bit-work/4966/render-build --test` ran through `quietlock.py run`, with the game
folder of the setting `render_game_dir` both times:

| C5R_EXE | Result |
|---|---|
| the setting `render_exe` (build 4390913) | 87 of 87 passed, 0 skipped, 567 s |
| golden copy of build 26577625 (`F:/c5-64bit-work/golden/civ5-steam-build26577625-ws`) | 87 of 87 passed, 0 skipped, 457 s |

`c5r_shaders_test` reports "EXE build id 1" on the new build; there all 1,417 blobs are found by their build-4390913
VA and by their own. The terrain-set, water, ice, fow, decal, forest, arc and bake tests find their blobs through the
array on the new build without any change of their own.
New unit tests in `render/tests/shaders/section_test.cpp`:
- `section_of_an_unknown_build_applies_no_sites`
- `compiled_array_anchor_is_the_committed_table` (the compiled build-4390913 array equals `create_sites_table.inc`,
  1,417 rows)
- `negative_known_build_with_other_blobs_fails_loud`
- `known_build_maps_build_4390913_vas_to_its_own` (a fake array)
- `negative_known_build_without_its_array_fails_loud` (no array, or a cell not carried)
- `negative_blob_not_at_its_carried_va_fails_loud` (also: a blob no row names)

**Server suites** (`server/tests/render/run_all.py`, branch at d7d4d871f, each step through `quietlock.py run`):

| Run | Result |
|---|---|
| Build 4390913's EXE (setting `render_exe`), all 14 suites but `unchanged-vs-main` (01:32-02:40 EST) | 13 PASS. `render-host` FAIL with 145 passed and 2 failed. |
| Build 26577625's golden EXE, `--no-build`: render-host, ice-reader, fow-scene, water-scene, forest-scene (02:40-02:58 EST) | 4 PASS. `render-host` FAIL with the same 2 tests. |

The 2 failures are `test_gameinstall.py::test_exe_build_refusals_and_override` and `::test_require_install`. Neither
comes from this ticket. 0589's test expects the old refusal text "a different build", and 4962 changed the message of
`tools/harness/exe_build.py` (both on the team head). Fixed on this branch in e3efd72ae: the test now expects the
new text, "a build outside the known list". `test_gameinstall.py` then passes 4 of 4 (2026-11-20 03:33).

Exe-field: `test_exefield.py` passed 5 of 5, case `build_addresses` included. The server built (`build_server.py`, with
the proxy). Team head d012115ec was merged after these runs; the merged branch's tables (catalogue a5a30da11a67d240,
104 symbols) are complete on both builds, with 40 of 40 patterns OK (2026-11-20 03:05).

## 14. Shell carry A: exe_camera.h, the bake, water, ice, overlay and shadow taps, the lockstep probe [W3, ticket 4964, 2026-11-19/20]

Branch `task-0590-c5a-carry-shell-a`. Every EXE address these files use is a named catalogue entry
(`exebuild/symbols/shell.tsv`); the code reads the running build's value through the table and never carries a VA
literal of any build (the comments keep build 4390913's VAs, labelled). Until the merge of the team head (proxy carry
4963, tools 0593) the tables were generated into a folder of this ticket's own (`C5_EXE_TABLES=C:/c5-64bit-work/4964/
exe-builds`), because the shared folder's `hand-review.tsv` then named symbols of other branches' catalogues, which
`gen_table.py` refused (section 5); since the per-catalogue tables (`table-<hash16>.json`) the shared folder serves all.

**Catalogue** [C, after the merge of the team head, 2026-11-20 02:35 EST]: 104 new symbols (78 code, 24 data, 1 rdata,
1 iat), prefixes `bake_` 9, `water_` 40, `ice_` 13, `ovl_` 10, `shadow_` 27, `ls_` 5; shell.tsv now 131. An address
several files use has one name, also across areas: the proxy carry's `pxw_tex_views` (the texture view table: bake,
overlay, shadow), `pxw_drawobj_pool` (the draw records: water, ice) and `pxw_renderer` (ice's operand) replace this
carry's first names `bake_viewtab`, `water_rectab`, `ice_renderer`, dropped at the merge; this carry's `ls_invview0`
(0xDA1410) replaces 0593's `gtr_invview` in `tools.tsv` (section 12). Within the carry: `bake_viewtab_view2` and the site
`bake_texcreate_viewtab` (bake, overlay, shadow); the renderer tables `water_vbtab` / `vbdesc` / `ibtab` / `ibdesc` /
`stride` and their 9 create/pool sites (water, ice); `cam_numviews` (water, overlay, shadow operands) and `cam_grid`
(ice's operand, water's water height at +4). Every pattern row sits at a `code` symbol of its own (the row's address),
every `R(...)` operand is a symbol; the operands at an offset of another symbol are their own entries
(`bake_viewtab_view2` = table + 4, `ovl_proj1` = OverlayProj[1], `shadow_sm0_1` = ShadowMatrix0[1]).

**Carry to build 26577625** [C, gen_table.py, golden copies; 2026-11-20 00:34 EST on this carry's catalogue, again
02:35 EST in the shared folder on the merged one (199 symbols, 127 of 127 patterns OK on both builds, complete)]: all
104, 102 by the matcher (75 code by bytes, 1 by the window ending after it, 1 by its callers, 24 data by references, 1
import by name; 98 high, 4 medium: `ice_ice`, `ovl_proj1`, `shadow_sm0_1`, `shadow_blur_name`, each with one reference,
which a pattern of a carried site then checks), 2 by hand:

- **`bake_ts_site`** (anchor 0x4033FE): a 5-byte load of the TerrainSystem address into the `this` register, whose only
  operand is relocated; the relink changed the code before it (the two builds pass a flag through a different stack
  slot and register). Answered as the only place in each build where, after the camera-state test and the UpdateViews
  call, a flag is stored, reloaded and pushed and then the TerrainSystem address is loaded, with `cam_fn_updateviews`
  and `bake_ts` of the build (exactly one hit in each build; in 4390913 at the anchor). Scratch script, llvm-objdump in
  scratch only (D83). (D83 edit 2026-11-25, 0772: both builds' code bytes around the site and the instruction sequence
  the search matched were replaced by this description.)
- **`water_water`** (anchor 0xD3DD0C, the Water object = TerrainSystem + 0x2772C): a member of the TerrainSystem object
  that no instruction addresses absolutely in either build; the code reaches it at offset 0x2772C from the
  TerrainSystem pointer, which the pattern `water_terrain_render` (an address computation at that offset followed by a
  call; `exebuild/symbols/shell.tsv`, `shell/composite/water_tap.cpp`) checks on both builds. Answered as `bake_ts` +
  0x2772C. (D83 edit 2026-11-25, 0772: the pattern's bytes written out were replaced by this description.)

Both answers are in the folder's `hand-review.tsv` (method `hand`), the new VAs nowhere else.

**Patterns** [C, gen_table.py and `shell/tests/exe_golden.py` on both golden EXEs, 2026-11-20]: 98 of 98 OK on both
builds: exe_camera.h 11, bake_tap 7, water_tap 29, ice_tap 21, overlay_tap 8, shadow_tap 20, shell_composite.cpp 2
(the lockstep probe: `ls_loop`, where the loop computes the address of an inverse-view matrix, pushes it and makes a call, and `ls_thunk`, the jump
through the D3DXMatrixInverse import slot; patterns in `shell/composite/shell_composite.cpp`, symbols in
`exebuild/symbols/shell.tsv`; D83 edit 2026-11-25, 0772: the two patterns written out were replaced by this description;
the call's target and return are checked in code against `ls_thunk` / `ls_ret`). Every fully compared pattern of 16 bytes or more
occurs once in each image. None was weakened to pass; what the generator's rule changes: a literal `E8`/`E9` frees the
four rel32 bytes after it, which the pre-4964 checks compared (valid for one link only). Rows affected: `ice_createterrain`,
`ice_step_built`, `ice_builder_vb`, `ice_builder_ib`, `ice_builder_record`, `ovl_startup`, `shadow_setup_blur` (the rest
of each row is still compared), and `shadow_pass_tail` (`E9 rel32`) and `shadow_render_call` (`E8 rel32`), which would be
a lone opcode: for these two the tap checks the jump's / call's target in code (`shadow_resample`, and the new symbol
`shadow_pass`, the shadow pass's head 0x523CA0 in build 4390913), and `test_shadow_tap.py` checks the same on both golden
EXEs.

**The lockstep probe's DLL** [C, import tables of both golden EXEs]: the slot `ls_iat_matinv` is
`d3dx9_42.dll!D3DXMatrixInverse` in build 4390913 and `d3dx9_43.dll!D3DXMatrixInverse` in 26577625. The probe no longer
names a DLL: it takes the module the slot points into (`GetModuleHandleExW` from the slot's value), checks that its name is
`d3dx9_4?.dll` and that its `D3DXMatrixInverse` is the slot's value, and hooks that. test_host's `--fake-uv` loads
`d3dx9_43.dll` for a fake image of build 26577625 (both are in SysWOW64 on the workstation).

**How a consumer obtains an address now** (for ticket 4965's taps and render/server's 4966):
- The running build: the composite's camera validation reads the EXE's header once (`VpExeBuildOfImage`: stamp and
  SizeOfImage), refuses an unknown build or an incomplete compiled-in table, checks exe_camera.h's 11 patterns for that
  build, and sets `VPCamSetBuild(build)`; every tap reads it with `VPCamBuild()` (one value per module; test hosts set
  their own for the image they lay out). The validation stays the single gate: a tap starts only after it.
- An address: `VpExeAddr(VPCamBuild(), VPX_<name>, exeBase)` (0 when not carried), or `VpExeVa(build, VPX_<name>)` for
  the ImageBase-0x400000 VA. The camera's `VPCAM_VA_APP` ... `VPCAM_VA_FCAM_VTBL` are still there as expressions for the
  running build (`VpExeVa(VPCamBuild(), VPX_cam_*)`), and `VPCAM_VA_OF(build, cam_app)` for a given build.
- A pattern table: `#define XX_SIGNATURES(X) X(symbol, "description", "bytes with R(symbol) operands") ...`,
  `static const VPExeSig kXxSigs[] = { XX_SIGNATURES(VPEXE_SIG_ENTRY) };`, checked with `VPExeCheckSigs(build, base, sigs,
  n, reader, ctx, why, len)` (returns the first failing row or -1; `why` names description, symbol, the build's VA and
  the reason) and laid into a fake image with `VPExeLaySigs`; list the file in `exebuild/patterns.txt`. (Since ticket
  0603, 2026-11-24: `VPExeSig` and `VPExeCheckSigs` are thin names over `vp_exe_sigs.h`; a fake image is laid with
  `VpExeFakeHeader` and `VpExeLaySigs`, section 18.)
- The pre-4964 form (`VPCamSig { va, name, text }` rows with `R(hexVA)` and `VPCamExpandSig`) still compiles for the
  files 4965 converts; it resolves a catalogued anchor through the running build's table and an uncatalogued `R(hex)`
  only on build 4390913, so those taps refuse (safely) on build 26577625 until converted. (Removed by ticket 0603,
  2026-11-24: 4965 converted every such file and nothing used it any more.)
- exe_camera.h's table part is compiled only when `vp_exe_tables.gen.h` is on the include path (`__has_include`); a unit
  that uses only its matrix helpers and thresholds (the server's render scene) compiles without it.

**Swap review of the code symbols** [C, scratch script on both golden EXEs, 2026-11-20 01:10 EST; after W2's finding
that addr_match carried two near-identical 0x20-byte wrappers to build 26577625 swapped with high confidence, which a
pattern with a free call rel32 cannot see]. Every code symbol of this carry and of exe_camera.h (90) was checked three
ways, names and counts only:
- *Uniqueness*: the 48 bytes at the anchor, relocated operands masked, occur exactly once in build 4390913's `.text`, and
  the 48 bytes at the carried address exactly once in 26577625's, for all of them except `ls_thunk` (19 identical import
  thunks `FF 25 <slot>` in a row in both builds; its identity is the slot it jumps through, `ls_iat_matinv`, carried by
  import name, and its pattern `FF 25 R(ls_iat_matinv)` checks exactly that). A symbol with a unique masked window has no
  twin to be swapped with.
- *Neighbours*: 139 pairs of code symbols within 0x400 of each other keep their distance in 26577625, but one:
  `cam_fcamera_getviewproj` .. `cam_fcamera_update`, two different FCamera methods the relink placed apart; both are
  tied to the FCamera vtable's entries [1] and [2] (section 7.2), so not a swap.
- *Callers of function heads* (E8 rel32 to the head), build 4390913 / 26577625: `bake_tr_site` 1 / 1, `shadow_pass`
  2 / 2, `ls_thunk` 29 / 29, `cam_fn_updateviews` 2 / 2, `cam_fn_hextoworld` 37 / 37, `shadow_resample` 0 / 0 (reached
  by the shadow pass's tail jump, which the tap checks), `cam_fn_setengineui` and `cam_fcamera_getviewproj` 0 / 0
  (reached through the interface / the vtable).
Nothing of this carry writes EXE memory or calls EXE code; the only code addresses used for control are compared, not
called: the probe's return address `ls_ret` (checked = `ls_loop` + 12) and its call target `ls_thunk` (checked in code
against the call's rel32), and the shadow tap's two jump/call targets (checked in code). So no further run-time
cross-check is needed here; W1's coming `C(name)` call-target token can replace the two in-code target checks.

Per code symbol of this carry (78), how it was cross-checked beyond the matcher's byte vote (the stage-2 merge request cites this list; the camera's are in section 7 and the review above):

| Code symbol | Carried by | Cross-checked by |
|---|---|---|
| `bake_ts_site` | hand | own R(data) operand; 48-byte window unique 1/1; hand: the unique UpdateViews-call run (see above) |
| `shadow_tframe_views` | bytes24 | own R(data) operand; same distance to shadow_tframe_sm (R(data)); 48-byte window unique 1/1 |
| `shadow_tframe_sm` | bytes24 | own R(data) operand; same distance to shadow_tframe_views (R(data)); 48-byte window unique 1/1 |
| `water_river_render_pass` | bytes24 | own R(data) operand; 48-byte window unique 1/1 |
| `ice_builder_vb` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_vbhandle` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_ib` | bytes24 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_chunksize` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_chunks` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_record` | bytes16 | own R(data) operand; 48-byte window unique 1/1 |
| `ice_builder_rec_ibvb` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_rec_tris` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `ice_builder_rec_start` | bytes16 | same distance to ice_builder_record (R(data)); 48-byte window unique 1/1 |
| `bake_prebake_tiles` | bytes16 | 48-byte window unique 1/1 |
| `bake_prebake_flag` | bytes16 | 48-byte window unique 1/1 |
| `ovl_ortho_proj0` | bytes24 | own R(data) operand; same distance to ovl_ortho_projv (R(data)); 48-byte window unique 1/1 |
| `ovl_ortho_projv` | bytes32 | own R(data) operand; same distance to ovl_ortho_proj0 (R(data)); 48-byte window unique 1/1 |
| `ovl_ortho_bound` | bytes16 | own R(data) operand; same distance to ovl_ortho_proj0 (R(data)); 48-byte window unique 1/1 |
| `ls_loop` | bytes24 | own R(data) operand; same distance to cam_updateviews_getviewproj (R(data)); 48-byte window unique 1/1; operand ls_invview0; its call's target ls_thunk and return ls_ret checked in code |
| `ls_ret` | bytes24 | same distance to cam_updateviews_getviewproj (R(data)); 48-byte window unique 1/1; = ls_loop + 12, checked in code |
| `shadow_pass` | bytes16 | same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1; callers 2 / 2; the call target of shadow_render_call (checked) |
| `shadow_pass_raw` | bytes24 | own R(data) operand; same distance to shadow_pass_quality (R(data)); 48-byte window unique 1/1 |
| `shadow_pass_quality` | bytes24 | own R(data) operand; same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1 |
| `shadow_pass_factor` | bytes16 | own R(data) operand; same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1 |
| `shadow_pass_blurclear` | bytes24 | own R(data) operand; same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1 |
| `shadow_pass_rawopen` | bytes24 | own R(data) operand; same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1 |
| `shadow_pass_unitmask` | bytes16 | same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1 |
| `shadow_pass_tail` | back16 | same distance to shadow_pass_raw (R(data)); 48-byte window unique 1/1; its jump target = shadow_resample, checked in code and in test_shadow_tap.py |
| `shadow_setup_blur` | bytes24 | own R(data) operand; same distance to shadow_setup_q1 (R(data)); 48-byte window unique 1/1 |
| `shadow_setup_q1` | bytes24 | own R(data) operand; same distance to shadow_setup_blur (R(data)); 48-byte window unique 1/1 |
| `shadow_resample` | bytes24 | own R(data) operand; same distance to shadow_resample_blur (R(data)); 48-byte window unique 1/1; callers 0 / 0 |
| `shadow_resample_blur` | bytes16 | own R(data) operand; same distance to shadow_resample (R(data)); 48-byte window unique 1/1 |
| `ovl_startup` | bytes24 | own R(data) operand; 48-byte window unique 1/1 |
| `shadow_render_call` | bytes16 | same distance to water_passa_wave (R(data)); 48-byte window unique 1/1; its call target = shadow_pass, checked in code and in test_shadow_tap.py |
| `water_passa_wave` | bytes24 | own R(data) operand; 48-byte window unique 1/1 |
| `ice_step_built` | bytes16 | 48-byte window unique 1/1 |
| `shadow_writer_sm0` | bytes24 | own R(data) operand; same distance to shadow_writer_smv (R(data)); 48-byte window unique 1/1 |
| `shadow_writer_smv` | bytes32 | own R(data) operand; same distance to shadow_writer_sm0 (R(data)); 48-byte window unique 1/1 |
| `shadow_writer_bound` | bytes16 | own R(data) operand; same distance to shadow_writer_sm0 (R(data)); 48-byte window unique 1/1 |
| `water_wave_render_count` | bytes16 | own R(data) operand; 48-byte window unique 1/1 |
| `water_wave_render_waves` | bytes16 | same distance to water_wave_render_count (R(data)); 48-byte window unique 1/1 |
| `water_terrain_render` | bytes24 | 48-byte window unique 1/1 |
| `bake_tr_site` | bytes16 | 48-byte window unique 1/1; callers 1 / 1 |
| `bake_copy_198` | bytes16 | 48-byte window unique 1/1 |
| `bake_copy_19c` | bytes16 | 48-byte window unique 1/1 |
| `water_wavechunk_array` | bytes16 | 48-byte window unique 1/1 |
| `water_wavechunk_verts` | bytes16 | 48-byte window unique 1/1 |
| `water_wavechunk_fmt` | bytes16 | 48-byte window unique 1/1 |
| `water_wavechunk_idx` | bytes16 | 48-byte window unique 1/1 |
| `water_wave_update_count` | bytes16 | 48-byte window unique 1/1 |
| `water_wave_update_waves` | bytes16 | 48-byte window unique 1/1 |
| `water_river_upload_vb` | bytes32 | own R(data) operand; same distance to water_river_upload_ib (R(data)); 48-byte window unique 1/1 |
| `water_river_upload_ib` | bytes24 | own R(data) operand; same distance to water_river_upload_vb (R(data)); 48-byte window unique 1/1 |
| `water_river_upload_rec` | bytes16 | same distance to water_river_upload_vb (R(data)); 48-byte window unique 1/1 |
| `water_river_upload_mat` | bytes16 | same distance to water_river_upload_vb (R(data)); 48-byte window unique 1/1 |
| `water_river_upload_tris` | bytes16 | same distance to water_river_upload_vb (R(data)); 48-byte window unique 1/1 |
| `water_river_build` | bytes24 | own R(data) operand; 48-byte window unique 1/1 |
| `ovl_render_bind` | bytes24 | own R(data) operand; same distance to ovl_render_views (R(data)); 48-byte window unique 1/1 |
| `ovl_render_views` | bytes24 | own R(data) operand; same distance to ovl_render_bind (R(data)); 48-byte window unique 1/1 |
| `ovl_render_proj` | bytes24 | own R(data) operand; same distance to ovl_render_bind (R(data)); 48-byte window unique 1/1 |
| `ice_createterrain` | bytes24 | 48-byte window unique 1/1 |
| `water_ocean_cap` | bytes24 | 48-byte window unique 1/1 |
| `water_ocean_vb` | bytes16 | 48-byte window unique 1/1 |
| `water_ocean_ib` | bytes16 | 48-byte window unique 1/1 |
| `shadow_wframe_sm` | bytes24 | own R(data) operand; same distance to shadow_wframe_factor (R(data)); 48-byte window unique 1/1 |
| `shadow_wframe_factor` | bytes16 | own R(data) operand; same distance to shadow_wframe_sm (R(data)); 48-byte window unique 1/1 |
| `ice_reload` | bytes32 | own R(data) operand; 48-byte window unique 1/1 |
| `ls_thunk` | callers | own R(data) operand; 48-byte window unique 19/19; callers 29 / 29; its operand is the import slot ls_iat_matinv (carried by import name); the probe also checks ls_loop's call reaches it |
| `bake_texcreate_viewtab` | bytes24 | own R(data) operand; 48-byte window unique 1/1 |
| `water_pool_cap` | bytes16 | 48-byte window unique 1/1 |
| `water_recpool_cap` | bytes16 | 48-byte window unique 1/1 |
| `water_vbcreate_desc` | bytes16 | own R(data) operand; same distance to water_vbcreate_stride (R(data)); 48-byte window unique 1/1 |
| `water_vbcreate_stride` | bytes16 | own R(data) operand; same distance to water_vbcreate_desc (R(data)); 48-byte window unique 1/1 |
| `water_vbcreate_buf` | bytes16 | own R(data) operand; same distance to water_vbcreate_desc (R(data)); 48-byte window unique 1/1 |
| `water_ibcreate_desc` | bytes16 | own R(data) operand; same distance to water_vbcreate_desc (R(data)); 48-byte window unique 1/1 |
| `water_ibcreate_buf` | bytes16 | own R(data) operand; same distance to water_vbcreate_desc (R(data)); 48-byte window unique 1/1 |
| `water_ibcreate_fmt` | bytes16 | same distance to water_vbcreate_desc (R(data)); 48-byte window unique 1/1 |
| `water_reccreate` | bytes16 | own R(data) operand; 48-byte window unique 1/1 |

**Tests** [L, stand-in on the workstation's hidden desktop, 2026-11-20 02:42-03:56 EST, branch at a37b5623f, the shared
exe_tables folder]: `shell/build.cmd` (C5_SHELL_BUILD under `C:/c5-64bit-work/4964/`) BUILD OK, no warning; the four tap
readers built from `server/tests/render/*` and `shell/frame/build.cmd` for the lockstep ring tests;
`test_safety_static.py`, `tools/exebuild/tests`, `perf/tests/test_exebuild_tools_0593.py`: 122 passed; shell suites
`test_camera`, `test_bake_tap`, `test_water_tap`, `test_ice_tap`, `test_overlay_tap`, `test_shadow_tap`, `test_lsprobe`,
`test_lockstep`, `test_errors`: 317 passed, 2 failed in 65 min. The two failures are `test_overlay_tap.py`'s window
tests `test_leader_screen_frames_are_not_stale` and `test_late_query_is_counted_stale`: the reader, started after the
3 s test window, sees the copies already resumed (accepted 28 and 9, want 1); the same leader test fails the same way on
a build of the team head before this carry (b34cb5e35, built from a `git archive` with its own tables, 02:40 EST;
accepted 31), so they are timing-dependent on this machine, not caused by the carry [L]. A third window-counting test,
`test_target_lost_in_play_is_counted_and_the_game_goes_on` (LOUD lines at most 45), failed twice before (47 and 50
lines) and passed in the final run and on the pre-carry build. Both builds' fake images ran the camera read path,
every tap's refusal of an image without its patterns, the ice tap's pattern-by-pattern refusals, the lockstep publish at
the fake UpdateViews (the hook on `d3dx9_42.dll` for 4390913, `d3dx9_43.dll` for 26577625) and the refusals of a stamp of
no known build and of a known stamp with another SizeOfImage.

**Not verified**: no live run on either build (it needs the full 64-bit path on build 26577625: the proxy carry 4963 and
the render/server carry 4966; ticket 4967 runs the live smokes); the four medium carries are confirmed by their patterns,
not by a second method; exe_camera.h's `VPExe*` helpers and 4965's `shell/composite/shell_exe.h` (`ShellExe*`) do the
same work twice (a follow-up may make them one). (Done by ticket 0603, 2026-11-24: one header, section 18.)

## 15. Shell carry B (ticket 4965): decal, cut, forest, fog-model, unit-filter and farm-guard taps [C, 2026-11-20]

Ticket 4965 (account B, branch `task-4965-carry-shell-b` from the team branch at 97112d641). The six composite files
`shell/composite/{decal_tap,cut_tap,forest_tap,fow_models,unit_filter,farm_guard}.cpp` (and their headers) hold no EXE VA
and no stamp any more: every address is a catalogue symbol (`exebuild/symbols/shell.tsv`, prefixes `decal_`, `cut_`,
`forest_`, `fow_`, `uf_`, `fg_`), read for the running build through `shell/composite/shell_exe.h`, and every byte pattern is
an `X(VPX_name, "name", "bytes")` row (all six files are in `exebuild/patterns.txt`).

### What the code does now

- **`shell_exe.h`** (new, header only, C++03 because `farm_guard.cpp` is also compiled into the proxy): `ShellExeBuildOf`
  reads the image's own PE header behind VirtualQuery (`VpExeBuildOfImage`), `ShellExeResolve` returns the build or a
  one-line reason ("not an EXE build the patch knows (PE timestamp 0x..., SizeOfImage 0x...)", "symbol X is not carried to
  <build>"), `ShellExeCheck` runs a table through `VpExeCheckPatternVa` (message names the pattern, its symbol, the build and
  the first mismatching byte), `ShellExeAddr` rebases. Tests: `ShellExeLay` writes a fake image of any known build (a PE
  header naming it, every pattern's expanded bytes at that build's addresses), `ShellExeSpan` its size,
  `ShellExeTestForgetBuild` (a header no build has), `ShellExeTestDrop(name)` (one symbol "not carried"). (Since ticket
  0603, 2026-11-24, every `ShellExe*` is a thin name over `exebuild/include/vp_exe_sigs.h`, section 18.)
- **Each tap** resolves the build once, before its first read, and refuses for good (its existing FAILED PATTERN / REFUSED
  path, LOUD) on an unknown build, a symbol not carried, or a pattern that fails. The composite's own EXE validation
  (`exeValidated`, 4964's `exe_camera.h`) is still required first, as before.
- **Test modes on fake images of both builds**: `cut_test=patterns`, `forest_test=patterns`, and new `decal_test=patterns`,
  `fow_models_test=patterns`, each with `<tap>_test_build=<steam build>` (`0` = a header no build has),
  `<tap>_test_pattern=<k>` (one pattern broken) and `<tap>_test_drop=<symbol>`; `farm_guard_test.cpp` lays out both builds
  through `FgImageBytes` / `FgWritePatterns(build, ...)`.
- **decal_tap**: the 8 farm category strings (32 bytes apart from `decal_farm_cats`, the one the farm reader references)
  are one pattern of 237 bytes (`??` padding) instead of 8 rows; the 7 unreferenced entries are no symbols at all, so the
  7 hand cases of 0582 B.2 ("7 entries of one decal_tap .rdata table") no longer exist. The pattern matches once in
  `.rdata` of each golden EXE (scratch search, 2026-11-20).
- **unit_filter path 2**: per program three symbols (`uf_<stage>_<hash>_blob` shader, `_push` code, `_handle` data); the
  create call is at push + 0x0B and the handle store at push + a per-site offset (the old table's `callAt - pushAt` is 0x0B
  for all 22 sites; the store offsets 0x0A to 0x25 are kept in the site table). The site patterns
  `68 R(blob) ?? x6 E8 ?? x4 ... 66 A3 R(handle)` check both on every build; `Path2` checks the 10 common patterns and the
  group's own site patterns (units 0..11, models 12..21), so a model site that fails does not refuse the units.
  `unit_programs.h` / `shell_model_scene.h` still name each blob by its build-4390913 VA; that is compared with the blob
  symbol's anchor (`kVpExeSymAnchor`), a build-time consistency check.
- **farm_guard**: `FgWorldFromBase(build, base, w)` (false when a symbol is not carried), `FgCheckPatterns(base, why, n,
  &build)`; the layout graph is `fg_builder_obj + 0x158` (no own symbol). The proxy compiles this file too
  (`proxy/build_proxy.py` FARM_GUARD_SRCS): its include path must carry `exebuild/include` and the emitted header (step 2b
  does).
- **Tests** (`shell/tests`): `test_exe_tables_shell_b.py` (new) checks that the six files hold no stamp and no VA literal
  outside their rows, that every row names catalogue symbols, that every row is OK in this catalogue's local table of
  each build and again against each build's golden EXE file (`tools/harness/exe_build.golden_install`, read only; one
  row with a changed byte is BAD there), and that a missing table fails naming `exe_tables` in the reader and in the
  emitter (exit 2). The tap suites run their pattern modes on fake images of both builds with the negatives above;
  the old "patterns match the installed EXE" tests read the owner's Steam install, now the new build, and are gone.

### Catalogue and shared names

199 rows were written for these files: 130 code (pattern sites and functions), 64 data, 8 rdata, 22 shader, 2 iat (one of
them 4960's). One name per anchor and kind (the catalogue reader refuses a second row): the camera names (`cam_gridptr`,
`cam_numviews`, `cam_h2w`, `cam_aabb`) and, after the team branch brought 4963's `proxy.tsv` (2026-11-20 02:30), the
proxy carry's names (`pxw_fow_mask`, `pxw_fow_base`, `pxw_tex_views`, `pxw_builder_read`, `pxw_base_building_read`,
`pxw_renderer`, `pxw_device`, `pac_qb_index`) are used as they are; this ticket's copies of those rows were deleted then.
Five rows repeat rows of 4964 (`shell.tsv` on `task-0590-c5a-carry-shell-a`, not merged; marked "copy of 4964"):
`bake_ts` 0xD165E0 (= TerrainDecalSystem), `bake_tr_site`, `bake_viewtab_view2`, `bake_texcreate_viewtab`, `ovl_proj`;
at 4964's merge the copies go. 4964 names 0x414C348 `bake_viewtab` and 0x3010570 `ice_renderer`, 4963 `pxw_tex_views` and
`pxw_renderer`: these files now use 4963's (merged first); the lead resolves 4964's at its merge. No overlap with 4966's
`render.tsv` / `server.tsv`.

### Generator results (golden copies, workstation, 2026-11-19 23:58 to 2026-11-20 00:40 EST, ticket-local table folder)

| | build 4390913 (`--identity`) | build 26577625 |
|---|---|---|
| symbols | 228 of 228 identity | 215 by the matcher (206 high, 9 medium) + 13 hand = 228, complete |
| patterns (11 camera + 122 of these files) | 133 of 133 OK | 133 of 133 OK |

With the team branch's head merged (4963's proxy carry and 0593, 2026-11-20 02:30) the whole catalogue: build 4390913
287 of 287 identity, build 26577625 287 of 287 (257 high, 10 medium, 20 hand), patterns 162 of 162 OK on both.

Tables first generated into a ticket-local folder (`C:/c5-64bit-work/4965/exe-builds`, before 4960b's per-catalogue files
reached the team branch), then, after merging the team branch (2026-11-20 01:15), into the shared folder `exe_tables`
(`C:/c5-64bit-work/exe-builds`): this catalogue's copy is `table-<catalogue hash16>.json`, the same counts. The 13 hand
answers were appended (append only, no other row touched) to the shared `26577625/hand-review.tsv`, where the other
catalogues' runs skip them as names they lack. Local only, D202.

### The 13 hand cases [C: each derivation run on build 4390913 gives the anchor; on build 26577625 each symbol's pattern is OK]

Derived from carried symbols by a call target or an instruction operand (scratch script, never committed; the steps are
here so the next build can redo them). Anchor VAs are build 4390913.

| Symbol (anchor) | Why the matcher missed it | Derivation |
|---|---|---|
| `decal_s_blank_spec` (0xA272D4, `.rdata` string) | one reference only | operand of the push at `decal_startup_blank` + 1 |
| `decal_farmsys_at24` (0x47347B, mid-function) | 8 bytes, 4 significant | the only call of the farm init (`decal_farminit` - 0xC6, head of that function), minus 3 (`lea ecx, [esi+24h]`) |
| `decal_sort` (0x6488A3) | the sort is one of several identical template sorts | call target at `decal_draw_next` + 0x6E (the draw's call of its sort), + 0x13 |
| `cut_write_state`, `cut_write_pool`, `cut_write_block` (0x49F2E9, 0x49F32D, 0x49F355) | the cell write exists as 3 byte-identical copies (template instances) in both builds; nothing tells them apart by bytes or distance | the cell write is the call target of its two callers (old 0x52C291 and 0x628C44, carried by bytes, high; both agree on build 26577625); + 0x19, + 0x5D, + 0x85 |
| `cut_pool_block` (0x4CD584) | 3 byte-identical copies | call target at the cell write + 0x60, + 4 |
| `fg_listener_reg` (0x46560B, mid-function) | short, call-heavy | call target at `fg_startup_group` + 5 (the listener group), + 0x0B |
| `fg_register` (0x8478A0) | its pattern is mostly relocated operands | call target at `fg_listener_reg` + 0x0C |
| `fg_lockinit`, `fg_lock` (0xCBC56C, 0xCBFC70, `.data`) | referenced only by `fg_register` | operands at `fg_register` + 2 (`test byte ptr [..], 1`) and + 18 (`mov dword ptr [..], 0`) |
| `fg_type_handler` (0x63E530) | a 5-byte `jmp` | the only `E9` to the listener walk's head (`fg_listener_walk` - 5) in each build |
| `uf_ps_8d853988_handle` (0x2DE1AAC, 16-bit global) | (as 4960's create-site experiment: 16-bit handle globals) | operand of `66 A3` at `uf_ps_8d853988_push` + 0x20 + 2 |

Against 0582 B.2's list [I, by the file names]: its "3 mid-function code addresses in decal_tap/farm_guard" are
`decal_farmsys_at24`, `decal_sort` and `fg_listener_reg`; "data addresses in farm_guard/unit_filter" are `fg_lockinit`,
`fg_lock`, `uf_ps_8d853988_handle`; the 7 `.rdata` entries are gone (above). The cut-tap template copies, `fg_register`,
`fg_type_handler` and `decal_s_blank_spec` are misses of this generator that 0582's map did not need (not literals there,
or another matcher).

**For the next build**: the derivations are mechanical (call target, operand, unique jump); a generator method "derived
from a carried symbol" (a catalogue note such as `via=call:decal_draw_next+0x6E,+0x13`) would carry all 13 without a
person. Not done in this ticket (`tools/exebuild/` is 4960's); proposed to lead-0590.

### Swap review of the code symbols (lead-0590 #7513, after 4963's finding of two swapped twin wrappers) [C, 2026-11-20 01:40-02:00]

4963 found that the matcher can carry two near-identical functions to each other's place with high confidence (bodies that
differ only in a call's rel32, which every pattern frees). All 118 code symbols of these files were reviewed on both
golden EXEs with scratch scripts (local, never committed; no new-build address printed). Per symbol, over its pattern's
own span (the same function in both builds): (1) every `E8`/`E9` there: the callee's masked bytes up to its first `ret`
are equal in both builds, or the callee is a catalogue symbol and the carried one; (2) every relocated operand there: a
catalogue anchor maps to its carried value, an import slot names the same DLL and function, another `.rdata` value holds
the same 4 bytes; (3) whether the masked span is unique in the new `.text`. Results:

| Result | Symbols |
|---|---|
| OK by (1)-(3) | 105 (all 22 unit-filter sites, the create functions and their stores, the forest, fog-model and most decal, cut and farm-guard sites) |
| OK by a sibling in the same function at the same distance in both builds | `cut_write_state`, `cut_write_block` (`cut_write_pool`), `decal_draw_flags` (`decal_draw_query`), `decal_farmread_variants` (`decal_farmread_cats`), `decal_query_end` (`decal_query_take`), `fg_builder_ret` (`fg_builder`), `forest_layout_idx` (`forest_trees`) |
| OK through the function's callers (a call site next to a carried symbol, its target + offset = the carried symbol) | `cut_size` (1 of 1 call sites, via `cut_made` + 0x50), `decal_mapfind` (2 call sites via `decal_resmaps`; a `std::map` find with 40 callers), `decal_sort` (via `decal_draw_next` + 0x6E), `fg_list_add` (12 call sites via `fg_register`) |
| Flagged, explained by hand | `fg_startup_group`: its callee (the listener group) is byte-equal in both builds apart from relocated operands and rel32s; the flag was the masking heuristic (a literal `0F 8x` pair taken for a jump). `fg_builder_layout`: its callee (old 0x6016F0, the layout) lacks, in build 26577625, the first call of the old prologue (old +9, to 0x9D58D0: a stub at the end of `.text` that sets up an SEH frame and has this one caller); the rest of the body is the old one 4 bytes earlier. [I, strong: a CEG check stub, which the new build no longer has (0582 B.1: CEG gone).] |

Per symbol, how it was cross-checked beyond the byte vote (callees: calls in its pattern span whose callee is equal in both builds or the carried symbol; operands: relocated operands there that map to their carried symbol, the same import or the same constant; unique: the masked span occurs once in the new `.text`, which a twin differing only in a rel32 would break; sibling / caller: as in the table; scratch scripts, 2026-11-20 02:40 run on the merged catalogue):

- bake: `bake_texcreate_viewtab` (2 operands); `bake_tr_site` (unique).
- cut: `cut_cell` (unique); `cut_create_call` (1 callee, 2 operands); `cut_forest_lookup` (1 callee, 1 operand, unique); `cut_freed` (2 callees, unique); `cut_lookup` (2 operands, unique); `cut_made` (1 operand, unique); `cut_pool_block` (1 callee); `cut_shadow_lookup` (1 callee, 1 operand, unique); `cut_shutdown_call` (2 callees, 1 operand); `cut_size` (caller via cut_made+0x50); `cut_write_block` (sibling cut_write_pool); `cut_write_pool` (1 callee); `cut_write_state` (sibling cut_write_pool).
- decal: `decal_add_batches` (unique); `decal_add_batchvec` (unique); `decal_add_blank` (unique); `decal_add_chunk` (unique); `decal_add_index` (unique); `decal_add_item` (2 callees, unique); `decal_add_large` (1 operand, unique); `decal_area_grid` (unique); `decal_area_set` (1 callee, unique); `decal_area_tx` (unique); `decal_bake_rect` (unique); `decal_cell` (1 operand, unique); `decal_draw_flags` (sibling decal_draw_query); `decal_draw_grow` (1 operand, unique); `decal_draw_large` (unique); `decal_draw_next` (unique); `decal_draw_query` (unique); `decal_draw_rect` (unique); `decal_draw_tex` (unique); `decal_draw_tex2` (unique); `decal_draw_verts` (unique); `decal_farm_owner_ctor` (2 callees, 1 operand); `decal_farminit` (2 callees, 2 operands, unique); `decal_farmread_cats` (1 operand, unique); `decal_farmread_variants` (sibling decal_farmread_cats); `decal_farmsys_at24` (1 callee); `decal_ins_head` (unique); `decal_ins_items` (unique); `decal_mapfind` (2 callers via decal_resmaps); `decal_pool_items` (1 callee, unique); `decal_query_end` (sibling decal_query_take); `decal_query_take` (unique); `decal_resmaps` (1 callee, unique); `decal_rm_records` (unique); `decal_rm_tds_site` (1 callee, 1 operand, unique); `decal_sort` (caller via decal_draw_next+0x6E); `decal_startup_blank` (1 callee, 1 operand, unique); `decal_texunload_mgr` (1 callee, 1 operand, unique); `decal_texunload_node` (unique); `decal_walk` (unique).
- fg: `fg_builder` (unique); `fg_builder_layout` (callee = old body without its CEG-stub call (see the table above)); `fg_builder_ret` (sibling fg_builder); `fg_cityinit_flag` (2 operands, unique); `fg_freelist_free` (unique); `fg_list_add` (12 callers via fg_register); `fg_listener_reg` (1 callee, 1 operand); `fg_listener_walk` (unique); `fg_node_delete` (1 callee, unique); `fg_register` (1 callee, 8 operands); `fg_shutdown_flag` (1 callee, 1 operand, unique); `fg_startup_group` (callee equal but for relocations and rel32 (masking artefact)); `fg_type_handler` (1 callee).
- forest: `forest_built` (1 callee, unique); `forest_first` (unique); `forest_kind` (unique); `forest_layout` (unique); `forest_layout_count` (unique); `forest_layout_idx` (sibling forest_trees); `forest_next` (unique); `forest_ready` (unique); `forest_records` (unique); `forest_tree` (1 operand, unique); `forest_trees` (unique); `forest_update_call` (1 callee, 1 operand, unique).
- fow: `fow_pass1_anim` (4 operands); `fow_pass1_proj` (1 operand); `fow_pass2_anim` (4 operands); `fow_pass2_proj` (1 operand).
- pxw: `pxw_base_building_read` (1 operand, unique); `pxw_builder_read` (1 operand, unique).
- uf: `uf_create_ps` (1 operand, unique); `uf_create_vs` (1 operand, unique); `uf_ps_0e7a3566_push` (1 callee, 4 operands); `uf_ps_246527ed_push` (1 callee, 5 operands, unique); `uf_ps_5def4f99_push` (1 callee, 4 operands, unique); `uf_ps_60d270e1_push` (1 callee, 5 operands, unique); `uf_ps_72c27aea_push` (1 callee, 4 operands, unique); `uf_ps_73daa800_push` (1 callee, 4 operands); `uf_ps_8d853988_push` (1 callee, 3 operands); `uf_ps_9a5991d7_push` (1 callee, 4 operands, unique); `uf_ps_c704c80b_push` (1 callee, 5 operands); `uf_ps_count` (1 operand, unique); `uf_ps_e12e9eaf_push` (1 callee, 5 operands, unique); `uf_ps_f0e1f0a6_push` (1 callee, 3 operands); `uf_ps_store` (1 operand, unique); `uf_shader_init` (1 operand, unique); `uf_shader_level_read` (1 operand); `uf_vs_0a3f2230_push` (1 callee, 3 operands); `uf_vs_29a0b061_push` (1 callee, 5 operands, unique); `uf_vs_34b98e63_push` (1 callee, 4 operands, unique); `uf_vs_59af33ff_push` (1 callee, 4 operands, unique); `uf_vs_7a9e17f6_push` (1 callee, 5 operands, unique); `uf_vs_7ec4e9a5_push` (1 callee, 4 operands); `uf_vs_82e59838_push` (1 callee, 2 operands, unique); `uf_vs_88c2b7d4_push` (1 callee, 5 operands, unique); `uf_vs_8cb52b96_push` (1 callee, 4 operands); `uf_vs_bc_store` (1 operand, unique); `uf_vs_c4bb25da_push` (1 callee, 3 operands); `uf_vs_count` (1 operand, unique); `uf_vs_da2382af_push` (1 callee, 3 operands); `uf_vs_size_store` (1 operand, unique); `uf_vs_store` (1 operand, unique).

No symbol of these files is swapped. Run-time cross-checks that would catch a wrong address where something is called or
written through, all present: the farm guard swaps only a listener node whose function word equals `fg_builder`
(compare-and-swap) and its pattern `fg_listener_reg` checks that the EXE's own registration stores `R(fg_builder)` as the
listener's function, so the builder the guard calls is the one the EXE registered; `fg_type_handler` is compared with the
type's handler slot before the node is touched. The unit filter calls nothing in the EXE; path 2 checks that each create
call at push + 0x0B targets `uf_create_vs` / `uf_create_ps` and the stores name the carried handle and object arrays.
The fog-model swap writes FOWAnim (`fow_anim`) and one view-table entry (`bake_viewtab`): the model passes' patterns read
exactly `fow_anim`..`fow_anim_w`, the white handle in `pxw_fow_mask` must equal the proxy's, and the entry must hold a
D3D11 view before the swap (unchanged checks). The `C(name)` call-target token for patterns had not landed on the team
branch at 02:40 (not used).

### Not verified (2026-11-20)

- No live run on either build: the taps ran only on fake images and the stand-in hosts (the end smoke on a Goldberg
  instance is optional and MC-B's).
- `shell/tests/unit_filter_host.cpp` (`--fake-exe`) still lays out a build-4390913 image of its own; making it table-driven
  needs 4964's camera conversion (the composite validates the fake image with `exe_camera.h`'s patterns first).
- The proxy build with the new `farm_guard.cpp` was not run (4963 owns the proxy side); the file alone compiles with the
  proxy's toolchain and flags (clang-cl -m32 in the VS 2008 environment, `-Wno-everything`, the emitted header; 2026-11-20
  00:50, rc 0).

### A test that read the moved code as text (ticket 0627, 2026-11-20) [C, test runs at both commits]

`shell/tests/test_model_programs_table.py` (ticket 4874) cross-checks `shell_model_scene.h` and `unit_filter.cpp`'s
site table rows 12..21 against `render/evidence/0561/4874/p5_sites.txt` by reading the C++ as text. 4965 (e118529e6)
replaced the rows' five hex literals `{ va, push, call, store, handle }` by `{ VPX_uf_<stage>_<sha8>_blob, ..._push,
..._handle, storeOff }` (the call at push + `UF_CALL_OFF` 0x0B). The test's regex then found no row and
`test_the_table_equals_the_evidence` failed with "model site rows are not in shell_model_scene.h order: []". It was red
on main from 7cb3e57f8 (the stage 2 merge, 2026-11-20 09:00 EST) and green at its first parent 1d207d3d8. 4965's step 6
ran the shell test files named after its six source files. This one carries another ticket's name, and no merge check
runs `shell/tests` (`research/06-tooling-and-workflows/test-health-on-main.md`). Since 0627 the test reads the table's
committed home: the `VPX_` names in `kSites`, their anchors (kind and build-4390913 VA) in
`exebuild/symbols/shell.tsv`, `storeOff`, `UF_CALL_OFF` and each row's `X(..._push, ...)` pattern (the E8 and the
`66 A3 R(handle)` offsets). It has 14 mutation cases, and a parse that finds no rows is an error in its own right. For a
later carrier: before converting a file, grep the tests for the file's name (`grep -rl unit_filter.cpp */tests`). A
test that parses C++ text moves with the conversion. Since ticket 0605 (2026-11-24) the grep is a tool:
`python -I tools/exebuild/compiled_by.py <files>` lists the tests that compile, include or name the files (section 8
step 7, the carriers' rule). The farm guard tests were the second case of this carry: see section 8 step 7.

## 16. The merged catalogue of stage 2: the lead's cross-check of every symbol [C, lead-0590, scratch script on both golden copies, 2026-11-20 03:37-03:45 EST]

After 4963, 0593, 4965, 4966 and 4964 were merged into the team branch (c3d211829; the five rows both shell carries
held are one row each, `gtr_invview` is 4964's `ls_invview0`): 392 symbols and the create-site array, no anchor twice.
`gen_table.py`: build 4390913 identity 392 of 392, build 26577625 392 of 392 (353 high, 16 medium, 23 by hand), patterns
249 of 249 OK on both builds, both tables complete.

**The check** (independent of each carrier's own review, and of the matcher's vote wherever the table has a hand
answer): for each `code` symbol the masked-equal run from the address in both builds, its direct call targets in order
(each old target compared with the new one through the table when it is a catalogue symbol, else through
`match_code`), direct callers and relocated references; for each `data`/`rdata` symbol the number of relocated
references in each build and where each old referencing site lands; for each vtable the run length, the entries that
carry and agree, the references. Counts:

| kind | symbols | agree | left for a reason below |
|---|---|---|---|
| code | 243 | 222 | 21 |
| data | 109 | 107 | 2 |
| rdata | 11 | 10 | 1 |
| vtable | 4 | 2 | 2 |
| shader, iat, array | 26 | checked by the generator itself (blob bytes, import name, 13.2 for the array) | |

No call target of any carried function disagrees with the table, and no old referencing site of any data symbol lands
on another address.

**The two swapped wrappers are confirmed by their callers.** The byte vote carries `uid_delete_array` and
`uid_free_aligned` swapped (11.2, cases 5 and 6). With the hand answers in the table, 11 call sites in 9 other carried
functions agree: `cam_fcamera_dtor` +0x11, `decal_add_batches` +0xE5, `decal_add_chunk` +0xA4, `decal_texunload_mgr`
+0xE3, `decal_texunload_node` +0xC0, `decal_farmread_variants` +0x96, `cut_freed` +0xC, `forest_layout_count` +0x4E and
+0xB5, `fg_shutdown_flag` +0x1F (offsets equal in both builds). With the matcher's own vote every one of them
disagrees. So the hand answers rest on the CCommandStream destructor (W2's reasoning) and on nine more functions from
three other carries.

**What the check left, and why each is accepted:**
- 18 mid-function sites and short stubs whose equal run from the address is short and that have no call inside it
  (`water_river_build`, `water_river_upload_tris`, `ice_createterrain`, `ice_step_built`, `ovl_ortho_bound`,
  `shadow_pass_tail`, `decal_farmread_cats`, `decal_add_large`, `cut_shutdown_call`, `forest_next`, `fg_builder_ret`,
  `fg_cityinit_flag`, `uf_ps_count`, `uf_ps_store`, `uf_vs_count`, `uf_vs_store`, `uf_vs_bc_store`,
  `uf_vs_size_store`): each has its own pattern row, OK on both builds (read from the local table's pattern records,
  2026-11-20 03:44), and is in its carrier's per-symbol list with the method (14 and 15). In 10 of the 18 rows the
  bytes hold an `R(data)` operand, which the byte vote did not use; the other 8 (`water_river_upload_tris`,
  `ice_createterrain`, `ice_step_built`, `shadow_pass_tail`, `forest_next`, `fg_builder_ret`, `uf_ps_count`,
  `uf_vs_count`) have none in their own row and rest on what their carrier lists: the distance to a carried sibling
  site, a 48-byte window unique in the new `.text`, or a jump target checked in code (not re-derived by the lead).
- 3 by hand with their reasoning: `decal_farmsys_at24` (15), `pac_secs_per_turn` (11.2, case 3),
  `uf_vs_29a0b061_push` (15; its one call agrees).
- `lsc_scene_map` (11.2, case 4) and `lt_ginv` (13.3): data that no instruction references directly; hand answers by
  their carried neighbours.
- `forest_k_one` (the float 1.0 in `.rdata`): 1,317 references in build 4390913 and 1,316 in build 26577625; 67 old
  sites land on it, none elsewhere; the four bytes are equal (the bytes after it differ: a pooled constant).
- `pace_core_thread_vtbl`: 12.1.
- `pac_util_vftable` (0x9EA718, 264 entries, by hand, 11.2 case 2): W2's check used the first 40 entries (23 carried,
  all agree). Over all 264: 165 entries carry by `match_code`, 161 agree at their index, 251 of 264 are equal in their
  first 16 masked bytes at the same index, lengths 264 and 264, two references in each build. The four that disagree:
  entries [54] and [82] are look-alikes (each equals the new entry at its own index for 112 masked bytes; their first
  32 masked bytes stand at 9 places in the old `.text`; the matcher gives each `back16`, high, at the other's place:
  the swap class again, in a vtable). Entries [195] and [256] are **two virtual functions whose bodies differ between
  the builds**: the old and the new function at the index share only their first 16 masked bytes, the new one does not
  carry back to build 4390913, and the matcher's `bytes24`, high, carry of the old one is a place that equals it for
  only 26 and 28 masked bytes (another function with the same prologue). Neighbouring entries are equal, so the table
  is the same table [C]; that Firaxis changed these two methods of the engine utility object in the 2026-11-19 update
  is [I] (not disassembled; the product reads this vtable's address and slot 102 only). **Named 2026-11-24 (ticket
  0604): [195] is `ICvEngineUtility1::GameplayOpenOptionsScreen()`, [256] is `ICvEngineUtility3::GetNumAICivsProcessed()
  const`** [C]; what each is for, and the evidence, is in "Slots 195 and 256 named" at the end of this section.
  (Replaced: "what they do is not known".)

**Two more things the matcher gets wrong with `high`** (for the generator's owner, with the swap rule of section 8):
(1) a look-alike inside a vtable is carried to its twin (entries [54], [82]); (2) a function that changed after its
first 16 to 24 bytes is "carried" to any other function with the same prologue (entries [195], [256]). Neither hit a
catalogue symbol in this carry. For a future build: a `code` symbol carried by `bytes16`/`bytes24` whose masked-equal
run from the address ends before its first call or operand deserves a look; the scratch check prints that run.

**What the product relies on in that vtable, and the guard** [C, code read 2026-11-20 03:57 EST]. `proxy_pacing.cpp`
(options 2 to 4) reads the vtable's address, checks that all 264 entries point into the EXE's `.text`, that the entry
after them does not and that the one before is 0 or data, compares slot 102 with `pac_secs_per_turn`
(`ICvEngineUtility1::getSecsPerTurn`, its own catalogue symbol), copies the running EXE's 264 entries as they are and
replaces slot 102 in the copy. No other slot is read by index, so the two changed bodies are copied as the running
build has them. The poller (a tool) compares the object's vptr with the address only. The DLL calls this interface by
slot as it always did; the slot count is the same in both builds. Slots 195 and 256 are named below (2026-11-24,
ticket 0604; replaced: "which methods slots 195 and 256 are was not looked up (open: count the virtuals of the DLL's
engine-utility interface header to name them; a question for a follow-up of 0582, not for the carry)").
`tools/exebuild/tests/test_reviewed_vtable_slots.py` (MC's order, #7677) fails
when a product source other than `proxy_pacing.*` names `pac_util_vftable`, when a second `kSlot...` constant appears,
when the copied table is indexed with anything but the reviewed forms, or when `kEntries` changes; its docstring says
what a review of a further slot needs.

**Slots 195 and 256 named** [ticket 0604, 2026-11-24 16:50-17:00 EST; source reads only, no EXE bytes, D83].
- **How they were counted** [C]. The DLL's view of the vtable is the class chain `ICvUnknown` ->
  `ICvEngineUtility1` -> `2` -> `3` -> `4` in the fork's `CvGameCoreDLLUtil/include/CvDllInterfaces.h` (`ICvUnknown`,
  lines 303-321: `QueryInterface` and `Destroy`, 2 slots; the template `QueryInterface<T>` is not virtual) and
  `CvGameCoreDLLUtil/include/ICvDLLUtility.h` (`ICvEngineUtility1` lines 49-349, 2 lines 351-358, 3 lines 360-396,
  4 lines 398-404), both identical at the x64 pin c29442e8c and the 32-bit pin f33fc46a4 (`git diff` empty). Counted
  with MSVC's x86 rule: base first, each class in declaration order, the overloads of one name grouped at the first
  one's place in reverse order (only `IsModActivated` 10/11 and `endTurnTimerLength` 187/188 are such groups; neither
  is near 195 or 256). Total **260** interface slots (0-259, the last `ICvEngineUtility4::GetEvaluatedMapScriptPath`);
  the EXE's 264 entries are those 260 and 4 more of the EXE's own class, which no DLL header names (as `addresses.md`
  0x9EA718 already says).
  - **Slot 195** = `ICvEngineUtility1::GameplayOpenOptionsScreen()`, `ICvDLLUtility.h:291` (`void`, no parameters).
  - **Slot 256** = `ICvEngineUtility3::GetNumAICivsProcessed() const`, `ICvDLLUtility.h:391` (`int`, no parameters).
  - **Checks of the count** [C]: the same count gives every slot this knowledge base already ties to an EXE address:
    102 `getSecsPerTurn` (`ICvDLLUtility.h:164`; `pac_secs_per_turn`), 127 `GetAudioTagIndex`, 184
    `GameplayDiplomacyAILeaderMessage`, 233 `IsGameCoreExecuting`, 10/11 and 187/188 the two overload pairs
    (`addresses.md`, `exe-audio.md`, `proxy-design.md` 3). And the proxy's compiler-proven model gives the same two
    names: `proxy/model/interfaces.json` (`"slot": {"x86": 195}` on `ICvEngineUtility1::GameplayOpenOptionsScreen()`,
    `256` on `ICvEngineUtility3::GetNumAICivsProcessed() const`), whose x86 slots `proxy/gen/verify_slots.py` checked
    against MSVC's vcall thunks (`proxy/model/slot-proof.json`, 3,460 of 3,460 agree, 2026-11-20); also
    `proxy/model/policy-table.tsv` rows 403 and 464 and `tools/x64-dll/evidence/2026-11-10-smoke/run3/interface-slots.tsv`
    rows 197 and 258. Counting script: scratch only (it reads the two headers with `git show` and prints the slots).
- **Slot 195, `GameplayOpenOptionsScreen`: what the DLL calls it for.** One call site in each pin:
  `CvGameCoreDLL_Expansion2/CvGame.cpp:3850` (c29442e8c; `:3852` at f33fc46a4), `case CONTROL_OPTIONS_SCREEN:` of
  `CvGame::doControl` (`:3584`) [C]. `doControl` is reached from `CvGame::handleAction` (`CvGame.cpp:3267-3288`, "an action
  initiated by the local human player"), which the EXE calls through `ICvGame1::HandleAction(int)` (x86 slot 36,
  `policy-table.tsv` row 765), and from Lua's `Game.DoControl` [C, source]. No Lua file of the stock install or of the
  installed VP mods (`MODS`, read 2026-11-24) passes `CONTROL_OPTIONS_SCREEN` to `Game.DoControl`; the stock Controls
  table binds it to **Ctrl+O** (`Assets/Gameplay/XML/Units/CIV5Controls.xml:397`, the same row in `DLC/Expansion` and
  `DLC/Expansion2`; VP's `CoreNewControls.xml` adds rows and changes none) [C, file read]. That a Ctrl+O key press makes
  the EXE call `HandleAction` with that control's action is [I] (the EXE's hotkey dispatch was not traced). The
  in-game menu's Options button opens `OptionsMenu` from Lua and does not pass the DLL [I, not traced].
  **Can the changed body alter what the DLL receives?** No for the return path [C]: the method returns `void` and takes
  no argument, so the DLL receives nothing from it. What could change is the EXE-side effect (the options screen it
  opens) and any call the EXE makes back into the DLL while it runs [I, not seen]; the options the screen sets reach the
  DLL through `OptionsMenu.lua`, which the update did not change (no Lua file changed, `new-exe-build-26577625.md` B.0
  item 1). On the product path the call goes server -> proxy -> the EXE's object synchronously (`policy-table.tsv`
  row 403: `sync`, target `exe`), and with pacing options 2-4 on the EXE's object points at the copied table, whose
  entry 195 is the running build's own (above).
- **Slot 256, `GetNumAICivsProcessed`: what the DLL calls it for.** Nothing: no call site in the DLL at either pin
  (`git grep` over `*.cpp`, `*.h`, `*.inl`: only the declaration) [C]; `policy-table.tsv` row 464 says `unused` (the server
  answers a default). Its neighbours `allAICivsProcessedThisTurn`, `SendAICivsProcessed`, `HasReceivedAICivsProcessed`
  belong to the multiplayer AI-turn handshake [I, by name]. **Can the changed body alter what the DLL receives?** No [C]:
  the DLL never calls this slot; any caller is in the EXE itself.
- **Why exactly these two bodies differ** is not known [I]. One guess to check with a disassembly kept in scratch
  (D83): build 4390913 carried Steam CEG, which places its checks inside some function bodies, and build 26577625 has no
  CEG (`new-exe-build-26577625.md` B.0 item 3); two bodies that differ only where a CEG check sat would fit. Nothing in
  this project depends on the answer.
- **Reachable by a call the shipped game makes:** slot 195, when a human player presses Ctrl+O in game (the DLL's own
  call, from the action the EXE hands it) [I for the key-to-action step]. Slot 256: no. A draft follow-up for a live
  check of Ctrl+O on build 26577625 is in ticket 0604's text ("Draft follow-up"), for the MC to number.

## 17. 4960c: look-alike bodies [C, generator runs on the golden copies, 2026-11-20 03:45-04:20 EST, W1]

(Numbered 16 on its branch; renumbered 17 at the merge into the team branch by the third lead-0590, 2026-11-20, because the lead's cross-check took 16.)

Branch `task-0590-c1c-call-targets-ambiguity` (team head 698e4dc4a merged). Catalogue 647c1913e78ba2af: 392 symbols plus
the array. Tables written into a private folder with a copy of the shared `hand-review.tsv`.

### 17.1 The rule

The swap behind this rule is in section 3, "Look-alike code". A code address whose first 16 masked bytes also stand
elsewhere in the old `.text` (`addr_match.body_ambiguous`) is never carried by its own byte windows. Each of the
following routes uses something the look-alike does not share, and they are tried in this order:

1. **Its own callers** (`callers (body ambiguous)`): each caller's 64-byte context, found once in the new `.text`,
   votes for its call target.
2. **The function it lies in, through that function's callers** (`in-function F+off (callers n/m)`): F is a direct
   call target at or below the address. Every masked byte from F to 16 bytes past the address must be equal at F'.
   - A single caller whose context is unique is taken with medium confidence.
   - If F is not the enclosing function's start, the span crosses into other code. The relink reordered functions, so
     such a span does not match and nothing is taken.
3. **The function start found by its padding** (`in-function F+off (bytesN)`): the 16-byte boundary after int3 fill.
   F is carried by its own bytes, which must be unique in the old `.text`; then the same span check.
4. **The whole prefix [F, address + 16) as one window** (`prefix window`): unique in the old `.text` and in the new.
   It never crosses F, unlike the byte windows that swapped the wrappers.
5. **The calls inside the prefix** (`callees`): for template copies whose prefixes are equal byte for byte, take the
   call targets that differ between the old copies. Each is carried on its own, at most two levels deep, and exactly
   one new copy must call all of them at the same offsets.
6. **F's call site** (`its call at C`): a caller site C of F carried by `match_code`, at most two levels deep, whose new
   call goes to F'; then the span check.

If none of these applies the symbol stays `ambiguous`, goes to `review.tsv`, and needs a hand answer.

Two further rules were written and dropped, because the full catalogue never needs them and they would ship untested:
- an R(data) operand of the symbol's own pattern that the look-alike lacks;
- a same-function catalogue sibling.

### 17.2 The 19 symbols the gate drops, and their evidence

All 19 are `code` and mid-function with no direct callers. Before 4960c every one was carried `high` by byte windows,
and every one is now carried to the same value.

| Symbol | Before | Now (route of 17.1) |
|---|---|---|
| `pxw_drawobj_usecount` | bytes64 | in-function 0x8C5C00+0x9B (callers 2/2) |
| `pxw_city_read` | bytes48 | in-function 0x5A24A0+0x134 (start by padding, bytes16) |
| `pxw_city_ramp` | bytes96 | in-function 0x5A24A0+0x10F (bytes16) |
| `pxw_base_city_read` | bytes32 | in-function 0x5A24A0+0x145 (bytes16) |
| `ovl_ortho_projv` | bytes32 | in-function 0x51B450+0x2E8 (callers 2/2) |
| `shadow_writer_smv` | bytes32 | in-function 0x615950+0x292 (callers 3/3) |
| `decal_draw_flags` | bytes32 | in-function 0x742EF0+0xC0 (callers 2/2) |
| `decal_mapfind` | bytes24 | in-function 0x426BB0+0x15 (callers 36/36) |
| `decal_farmread_variants` | bytes64 | in-function 0x797200+0x21E (prefix window) |
| `decal_query_end` | bytes128 | in-function 0x724CA0+0x130 (its call at 0x6D7EE7) |
| `cut_cell` | bytes32 | in-function 0x4A0A50+0xC (callers 1/1, medium) |
| `cut_size` | bytes48 | in-function 0x60DA90+0x4C (its call at 0x6F73F3) |
| `forest_layout_idx` | bytes32 | in-function 0x439E00+0x1FF (callers 1/1, medium) |
| `forest_tree` | bytes32 | in-function 0x5A3BB0+0x2DD (callers 1/1, medium) |
| `fow_pass1_proj` | bytes160 | in-function 0x4FB320+0x30D (callers 2/2) |
| `fow_pass1_anim` | bytes128 | in-function 0x4FB320+0x320 (callers 2/2) |
| `fow_pass2_proj` | bytes160 | in-function 0x674AD0+0x30E (callers 1/1, medium) |
| `fow_pass2_anim` | bytes128 | in-function 0x674AD0+0x321 (callers 1/1, medium) |
| `fg_list_add` | back16 | in-function 0x861760+0x25 (callers 1/1, medium) |

The four proxy sites:
- The R() operands of `pxw_city_read`, `pxw_city_ramp` and `pxw_base_city_read` do not tell them from their look-alikes:
  the look-alike copies at 0x555921/0x5558FC/0x555932 read the same globals.
- What separates them is the function: F = 0x5A24A0, whose start is unique and carried by its own bytes, with every
  masked byte to each site equal.
- `pxw_drawobj_usecount` has no operand at all; its function 0x8C5C00 is carried by 2 agreeing callers.

`cut_size` lies in one of three template copies (0x5F851C, 0x60DADC, 0x798CFC, equal prefixes). They differ in the call
at +0x3A (0x605750, 0x6CFE10, 0x5D73A0), but those callees are look-alikes too. The copy is fixed by its function's one
call site, 0x6F73F3, which itself carries.

### 17.3 Result on the full catalogue

- 392 of 392 carried (23 hand, 347 high, 22 medium), 0 unresolved, 249 of 249 patterns OK.
- Every value equals the shared table made without the gate, and all 5,668 create-site cells are equal.
- No hand answer is added.
- The two wrappers are carried by their callers to the hand answers, which they keep.
- lead-0590's check is a test: at 10 call sites in 9 other carried functions the old call goes to a wrapper and the new
  call to that wrapper's answer (`test_wrapper_answers_agree_with_carried_call_sites`).
- `pac_util_vftable` entries 54 and 82 count as ambiguous. They now carry to the new vtable's entry at the same index or
  not at all, never elsewhere (`test_lookalike_vtable_entries_not_carried_elsewhere`).
- `C(name)`: `test_call_target_pattern_refuses_a_swap` keeps every other hand answer and swaps only the wrappers' two.
  The run is refused with exactly the destructor's 3 C() rows BAD.

### 17.4 For a future build

A short function, or a repeated piece of mid-function code, is no longer carried by bytes that also match its
look-alike. On a relink like 26577625's, which reorders functions, such a symbol is now carried by a route that fails
rather than lands on the look-alike. When no route applies the symbol goes to hand review. It is never carried
silently.

Cost: about 2 s more per table. The callers and span checks run only for ambiguous addresses: 19 of 243 code symbols,
plus array call sites (19 values carried by the row sibling, not 17).

Not covered: a look-alike that also shares its function's callers and every route above. That is a full copy of a
function with the same caller context, and it stays for a person.

## 18. One header for the pattern-table helpers and the image base: `exebuild/include/vp_exe_sigs.h` [C, ticket 0603, 2026-11-24]

Until ticket 0603 two headers did the same work: `shell/common/exe_camera.h` (`VPExeSig`, `VPExeCheckSigs`,
`VPExeLaySigs`, `VPExeSigsEnd`, `VPExeFakeHeader`, the dead `VPCamExpandSig`, its own `VPCAM_IMAGE_BASE`) and
`shell/composite/shell_exe.h` (`ShellExePat`, `ShellExeResolve`, `ShellExeCheck`, `ShellExeLay`, `ShellExeSpan`, ... over
`vp_exe_symbols.h`'s `VPEXE_IMAGE_BASE`): two image-base constants and two fake-image writers. Now:

- **`exebuild/include/vp_exe_sigs.h`** is the only file that defines an image-base constant
  (`git grep -nE 'define [A-Z_]*IMAGE_BASE'` finds only it) and holds every helper of a pattern table: the row
  `VpExeSig { sym, name, text }` with `VPEXE_SIG_ENTRY`, the test hook `VpExeSigTestDrop` / `VpExeSigVa` (one symbol "not
  carried", per module), `VpExeBuildOfLoaded` and `VpExeResolve` (the build from the loaded image's PE header behind
  VirtualQuery; the refusal lines), `VpExeBuildBySteam`, the check `VpExeSigsFirstFail` with its two message forms
  (`VpExeCheckSigs`: "pattern '<description>' at <symbol> (VA 0x...): <reason>[ at byte k]", any reader, the camera's and
  the bake, water, ice, overlay and shadow taps'; `VpExeCheckSigsOn`: "pattern '<description>' at <symbol> does not match on
  <build label> (byte +k)" and "pattern '<description>': symbol <name> is not carried to <build label>", the in-process
  reader, the 4965 taps'), and for test images `VpExeSigsEnd` (the VA past the furthest row's expanded bytes),
  `VpExeFakeHeader` (MZ, e_lfanew 0x80, PE, i386, one section, the build's TimeDateStamp and SizeOfImage; clears 0x200
  bytes first) and `VpExeLaySigs` (each row's compared bytes at its build's VA, bounded by the image's size, row `broken`'s
  last compared byte flipped), `VpExeTestForgetBuild`. No printf in it (the messages are built by hand), so it compiles in
  every unit: the proxy's C++03 units with VC9 headers, clang-cl, MSVC, x64.
- **Include structure.** `vp_exe_symbols.h` needs the image base before its own rebasing functions, and the helpers need
  `vp_exe_patterns.h`, which includes `vp_exe_symbols.h`. So `vp_exe_sigs.h` has two guarded parts and no `#pragma once`:
  part 1 defines `VPEXE_IMAGE_BASE`; `vp_exe_symbols.h` includes the file with `VPEXE_SIGS_BASE_ONLY_` defined and gets
  only that part; any other include gets part 2 too. The three headers can be included in any order.
- **Thin names.** `exe_camera.h` keeps `VPExeSig` (a typedef) and `VPExeCheckSigs` (an inline call) for its seven
  callers; the three callers of its old lay/end/header helpers (`shell/standin/test_host.cpp`, `shell/tests/unit_filter_host.cpp`,
  `shell/composite/ice_tap.cpp`) call `VpExeFakeHeader` / `VpExeSigsEnd` / `VpExeLaySigs` directly, and every
  `VPCAM_IMAGE_BASE` became `VPEXE_IMAGE_BASE`. `shell_exe.h` keeps every `ShellExe*` name as a one-line call
  (`ShellExeSpan` = `VpExeSigsEnd` rounded up to a page, at least one page; `ShellExeLay` = `VpExeFakeHeader` +
  `VpExeLaySigs`). `proxy/src/proxy_exe.h` is unchanged: its `ProxyExe::Test::DropSymbol` hook must stay apart from the
  composite's, because `shell/composite/farm_guard.cpp` is compiled into the proxy too and its checks would see the proxy
  tests' drops.
- **Behaviour changes of the camera's fake image (tests only)** [C, source]: the old `VPExeLaySigs` copied every expanded
  byte, free ones too (a call's rel32 as the template's literal bytes, ?? as 0), unbounded; the shared lay writes only the
  compared bytes (as `ShellExeLay` did), so a row's free bytes no longer overwrite an earlier row's, and it refuses a row past
  the image's end or in the header's first 0x200 bytes. The old `VPExeSigsEnd` was the largest row VA + 96; the shared end is
  the VA past the furthest row's expanded bytes (test_host adds a page, the ice tap rounds to 64 KB, so both images still
  cover every row). The camera's fake header now also has NumberOfSections = 1 and its first 0x200 bytes cleared (no reader
  of the fake image walks its sections). The camera's check now honours the test drop hook (no test drops a `cam_` symbol).
- **Tests** [C, workstation runs 2026-11-24 20:37-22:30 EST, branch `task-0603-shared-exe-sig-helpers` from main
  6c0a4cc33; record: `research/02-exe-internals/evidence/0603/acceptance.txt`]: shell build OK; shell suites on fake images
  of both builds (17 files, the camera, unit and model filters, every tap, the farm guard, the lockstep probe, the stand-in)
  520 passed, and the 5 failures all in `test_exe_tables_shell_b.py`, which also fail on main: its row count for
  `farm_guard.cpp` (21) predates ticket 0657's 13 edge rows; set to 34, that file passes 25 of 25 (the golden-EXE check of
  all 143 rows on both builds included). `tools/exebuild/tests` 62 passed (the native `exe_tables_test`, x86 and x64);
  server and proxy build OK; `test_proxy_session` 196/196, `test_proxy_rt` 235/235, `test_farm_guard_proxy` 11/11 (with
  its ASan unit), `test_build_settings` 4/4 on this proxy build; `render/build.py --test` 109 of 109, 0 skipped, with
  each build's golden EXE as `C5R_EXE`; under ASan (`shell/build.cmd asan`) test_camera, test_farm_guard, test_cut_tap,
  test_unit_filter and test_ice_tap 179 passed with no ASan report.
