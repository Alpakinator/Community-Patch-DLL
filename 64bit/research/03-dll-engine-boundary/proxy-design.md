# Proxy design: interface model, slot proof, marshalling, per-call policy, projected cost, UI-Lua path

Written 2026-11-10 by the team-2100 coordinator (epic 2100, package I1-C). Everything here was produced offline from
the DLL fork at the pinned commit e9d0c4837 (`%USERPROFILE%\Documents\GitHub\Community-Patch-DLL-x64`) and from the
2026-11-09 boundary benchmark (`research/sources/boundary-benchmark/results.json`); **no game was run and no call
crossed the transport**. Code and data: `proxy/` (README there). The model is `proxy/model/model.json`; one row per
method is in `proxy/model/policy-table.tsv`.

## 1. How the model is built (all regenerable, ~3 minutes)

| Step | Script | Method | Confidence |
|---|---|---|---|
| Interfaces, virtuals, types, const, calling convention | `proxy/gen/extract_model.py` | clang 19.1.7 `-ast-dump=json` (filtered by name family) over a probe TU that includes the DLL's PCH header, every `CvDll*.h` and the FireWorks interface headers, in the DLL's own x86 configuration (VS 2008 / VC9 headers, release defines) | [C] |
| vftable slots, x86 and x64 | same | `-Xclang -fdump-vtable-layouts` ("VFTable indices" blocks) on a probe that derives from every interface, compiled for x86 (VC9 env) and x64 (MSVC 14.4x STL, `/std:c++14`, `VP_OUT_OF_PROCESS`) | [C] |
| Record layouts of types that cross by value/reference | same | `-Xclang -fdump-record-layouts`, both arches | [C] |
| Slot cross-check | `proxy/gen/verify_slots.py` | Microsoft cl.exe (VS 2022, MSVC 14.4x) x86 and x64: a pointer to every virtual compiles to a vcall thunk `??_9Class@@$B<offset>A..`; the byte offset is read from the `/FAs` listing | [C] |
| Lua surface | `proxy/gen/extract_lua.py` (worker 2102) | clang `/E` of each `Lua/CvLua*.cpp` (x86 and x64) for the registrations that are really compiled; clang AST of the same TUs for the constness of every C++ member a Lua method calls; textual body parse for argument and result shapes [I] | [C]/[I] |
| Engine methods the DLL calls | `proxy/gen/classify.py` | source scan: `gDLL->X(` / `GC.getDLLIFace()->X(` for `ICvEngineUtility*`, `DLLUI->X(` / `GC.GetEngineUserInterface()->X(` for `ICvUserInterface*`, any `->X(` for script system, args, file system, logs | [C] text, [I] for the by-name families |
| Marshalling, policy, costs | `proxy/gen/classify.py`, `marshal_types.py` | rules in the code, each with its reason in the model (`policy.rule`) | [I] |

## 1a. Regenerating the policy table at the pin (issue 0125, 2026-11-13 21:05-21:30 EST)

Until this issue `classify.py` read the fork clone `toolchain.FORK` (at e9d0c4837) and one-off 9029 had hand-edited the
`TryGameCoreLock` row of `policy-table.tsv` (and its `model.json` entry, but not the counts), so a regeneration undid
that row and team 3300 wrote its consumer column with `apply_consumers.py` instead.

- [C] **What of the fork reaches `policy-table.tsv`**: only `scan_dll_usage` (which engine methods the DLL source
  calls). `guid_counts` reaches only `model.json`. Every other input is committed (`interfaces.json`, `lua.json`,
  `slot-proof.json`, `consumers.tsv`, the benchmark `results.json`).
- [C] **At e9d0c4837** (`classify.py --unlocked --check`, the clone at e9d0c4837): exactly one row differs,
  `dll_to_engine ICvEngineUtility1 TryGameCoreLock bool()`, columns 9-11 (`unused`/`unused`/"the DLL never calls it"
  against `server_local`). Cause: the DLL calls `gDLL->TryGameCoreLock()` only since upstream fix 6cc672569
  (`CvTradeClasses.cpp:4665`, `UpdateTradeStats`, at 04b1211b8).
- [C] **At the pin 04b1211b8** (plain copy: `git -C <fork clone> archive -o <tar> 04b1211b8`, extracted to
  `D:/c5-64bit-work/0125/src`, fingerprint equal to `deps/dll.lock`'s, 473 files): `policy-table.tsv` reproduced
  **byte for byte** (1,616 lines), the rule text of the 9029 row included, once that text moved into the generator
  (`classify.RULE_NOTES`, appended to the plumbing rule while the scan finds the method used). **No hand-kept rows
  remain**; the check skips nothing.
- [C] `model.json` had been stale since 9029 in three counts, now regenerated at the pin:
  `dll_to_engine_methods_used` 295 -> 296, `gDLL_distinct_names` 164 -> 165, `dll_to_engine_used_by_iface`
  `ICvEngineUtility1` 149 -> 150. New key `usage_scan_source: {commit}` (the fork commit the scan read; `source` stays
  `interfaces.json`'s e9d0c4837). Nothing else changed apart from `generated_at`.
- **Commands.** `python -I proxy/gen/classify.py --src <fork at the lock commit> [--out-dir DIR]` (refuses, exit 2, any
  source that `tools/x64-dll/build.py` `resolve_src` refuses; without `--src` that is the clone at e9d0c4837);
  `--check [--against-dir DIR]` generates into a temporary folder and compares: `policy-table.tsv` byte for byte after
  folding the committed file's CRLF to LF (this repository's checkouts use `core.autocrlf=true`, so the working copy
  has CRLF), `model.json` with its top-level `generated_at` masked; each differing row is printed with its key and
  column numbers; exit 0 equal, 1 different, 2 refused input (a bad `consumers.tsv` row now exits 2 too, was 1).
- [C] **Test** `python -I proxy/tools/test_policy_table.py --src D:/c5-64bit-work/0125/src --out
  D:/c5-64bit-work/0125/test -v`: 13/13 passed, 17 s (2026-11-13 21:15 EST): equal check, regeneration into a folder,
  a CRLF reference; negative: changed policy column, changed rule text, missing row, added row, two rows swapped, a
  changed `model.json` count (each exit 1 naming the row or file), a missing reference file, a missing source and a
  fork-shaped copy that is not the pin (each exit 2, nothing written); the committed files hashed unchanged.
  `apply_consumers.py --check` up to date; `server/tests/feed/test_consumers.py` 20/0.
- *Superseded 2026-11-13 by section 1b (issue 3503): all five files are now regenerated at 04b1211b8; the expectation
  below held for the generated runtime, not for `interfaces.json` (the CvAStar record grew a field).* [C] **Not
  regenerated, still from e9d0c4837**: `interfaces.json`, `slot-proof.json`, `lua.json`. Between e9d0c4837 and
  04b1211b8 nothing changed in `CvGameCoreDLLUtil/include` or `CvGameCoreDLL_Expansion2/CvDll*.h` (`git diff --stat`
  empty); `FirePlace/include/FireWorks/FFastVector.h` gained 7 lines in a function body (`deleteNode` bounds check), so
  `interfaces.json` is expected to be unchanged [I, extract_model.py not run]. `lua.json` may differ [I]:
  `CvLuaCity`/`CvLuaPlayer` `lGetRealPopulation` changed from `BasicLuaMethod` to an explicit `lua_pushnumber` body and
  `CvLuaGameInfo::pRegisterTypesInfo` sorts its types; `classify.py` copies `lua.json` only into `model.json` (`lua`,
  projection), never into `policy-table.tsv`.
- Not verified: `--src` as a git checkout at the pin (only the plain-copy form was run; the `resolve_src` path for git
  checkouts is the one every build uses).

## 1b. The whole model at a source path; the stage `model` (issue 3503, epic 3500, 2026-11-13 21:55-22:15 EST)

- **Every script of the chain takes `--src`** (`extract_model.py`, `verify_slots.py`, `extract_lua.py`, `classify.py`,
  `gen_runtime.py` had it). `proxy/gen/toolchain.py` `resolve_source(src, lock, unlocked, commit)` checks it: a git
  checkout at the lock's commit without tracked changes, or a plain copy whose source fingerprint (`tools/x64-dll/
  build.py` `fingerprint_dir`) equals the lock's; `--lock <file>` names another lock (a release candidate's,
  `<work>/deps/dll.lock`); `--unlocked --commit <label>` takes any fork-shaped tree (also a plain copy, unlike
  `resolve_src`). `use_source` sets `FORK`, `PINNED_COMMIT` and the environment variables `VP_PROXY_GEN_SRC`,
  `VP_PROXY_GEN_COMMIT`, `VP_PROXY_GEN_WORK`, which the worker processes of `extract_lua.py` read at import (they are
  spawned, so module globals set in `main` do not reach them). Without `--src` the scripts read the old fixed clone at
  e9d0c4837 as before. New options: `--out-dir`, `--model-dir` (verify_slots, gen_runtime), `--inputs-dir` (classify),
  `--work` (compiler scratch). [C]
- **`proxy/gen/regen_model.py --src <dir> --out <dir> [--lock F | --unlocked --commit C] [--compile-check] [--write]`**
  runs the five steps as processes into `<out>/model`, generates the runtime twice (from the regenerated model and from
  `proxy/model`) and compares: JSON value by value with `generated_at`, `source.fork_path`, `source.commit`,
  `usage_scan_source.commit` and the same three of model.json's embedded `lua` masked (the commits are printed as info),
  `policy-table.tsv` row by row, the generated files byte for byte. Exit 0 equal, 1 different, 2 refused, 4 a step
  failed, 5 `<out>/STOP`. Report `<out>/model-report.txt` and `.json`. [C]
- **At the pin 04b1211b8** (plain copy `D:/c5-64bit-work/3500/w2/src`, fingerprint equal to `deps/dll.lock`'s) against the
  model committed before 3503 [L, 2026-11-13 21:57-22:01 EST, 244 s with `--compile-check`]: `policy-table.tsv`,
  `slot-proof.json` (masked) and **the generated runtime equal** (so `kAppSchemaHash` and every generated file are
  unchanged: the regeneration does not touch the proxy/server pair); `interfaces.json` differs only in the record
  layout of `CvAStar` (x86 and x64: a field `std::vector<CvAStarNode*> m_childNodes` before `m_pBest`, size 840 -> 864
  on x64; an upstream change; the record crosses no interface by value); `lua.json` (and model.json's copy of it):
  line numbers of 1,500+ methods moved, `lGetRealPopulation` of City and Player now returns `long long` (was `long`;
  the body is now an explicit `lua_pushnumber`, so its shape rule moved from `wrapper_signature[C]` to
  `textual_body_parse[I]`), `Player.GetReplayData` resolves 5 callees (was 2), `summary.wrapper_methods` 1182 -> 1180,
  `callee_effects.read` 6240 -> 6243.
- **Two sources of run-to-run noise fixed** in `extract_lua.py`: `counts.other_registrations` came in the order the
  preprocessor threads finished (now sorted by file, line, name), and `counts.ast.tus` held each TU's AST seconds (now
  left out: `[TU, AST bytes, methods]`). Then `--write` committed the regenerated model (22:03-22:06, 185 s), and a third
  run compared EQUAL in every file once model.json's embedded `lua.generated_at` was masked (22:07-22:10, 170 s;
  `D:/c5-64bit-work/3500/w2/regen-pin3`). Step times: extract_model 34-50 s, verify_slots 61-78 s, extract_lua 69-85 s,
  classify 2 s, gen_runtime 1 s (42 s with the compile check). [L]
- The committed `interfaces.json`/`lua.json`/`model.json` `source` now names commit 04b1211b8 and the scratch path
  of the copy they were made from (provenance only; masked in every comparison).
- 2026-11-14 (3502, lead-3500's order after the Release-5.4.8 driver run): `counts.ast.tus` (AST bytes per TU) and
  `counts.preprocessed_mb_total` (and the same under `lua`) are out of the verdict and printed as information: they
  grew with the source folder's path (`C:/c5-64bit-work/3500/releases/Release-5.4.8/src/x64-99fb2c1c` against
  `D:/c5-64bit-work/3500/w2/src`: 26 values, nothing else differed; series commit 99fb2c1c, same tree as 04b1211b8).
  Tests: `proxy/tools/test_purity.py` section 6 (`--only regen`: other commit EQUAL, longer path EQUAL, a slot change
  DIFFERENT; 3/3) [C, L].
- **2026-11-16 (issue 0309): `extract_lua.py` reads the unqualified `toValue<T>(L, n)`.** The CvLua classes inherit
  `CvLuaArgs`, so the pin spells the scalar read two ways: 8 sites `CvLuaArgs::toValue<T>(L, n)`, which the textual parse
  (`RE_ACC` in `parse_shape`) read, and 40 sites (38 lines, 36 C functions, all in `CvLuaCity.cpp` and `CvLuaGame.cpp`)
  plain `toValue<T>(L, n)`, which it did not [C, grep of the pin's `Lua/`]. `RE_ACC` now takes
  `(?:CvLuaArgs::|(?<![.>:]))toValue\s*<[^>]*>` and records both as accessor `CvLuaArgs::toValue<T>` (kind from `T` as
  before); a member access (`x.toValue<`, `p->toValue<`), another class's qualification (`Other::toValue<`) or a longer
  name (`mytoValue<`) is not counted. Unit tests in `proxy/gen/tests/test_extract_lua.py` (18/18). Model regenerated at
  the pin (`regen_model.py --src` a plain copy of 04b1211b8, fingerprint equal to the lock) [L, 2026-11-16]: first run
  18:20-18:24 EST DIFFERENT only in `lua.json` and `model.json`'s copy of it, `interfaces.json`, `slot-proof.json`,
  `policy-table.tsv` and the generated runtime **equal**; by id exactly the 36 methods of those C functions changed, and
  only their `args` (one or two `CvLuaArgs::toValue<...>` arguments added; where a later optional argument was already
  read, for example `City.GetSpecialistRate`'s `luaL_optbool` at 3, it stays after the new one at 2). All five
  model files committed from that run (what `--write` does); a second run 18:25-18:29 EST compared **EQUAL** in every
  file (246 s). Trap [L]: commit all five files of one run, never only the ones that changed: `classify.py --check` (and
  so `test_policy_table.py`) masks only `generated_at` in `model.json`, not `source.fork_path`, which `model.json` takes
  from `interfaces.json`; with only `lua.json` and `model.json` copied, `test_policy_table.py` failed 7 of 13 cases on
  the path alone (`check-equal` and every negative case, since each also saw the `model.json` difference). With all
  five committed: `test_policy_table.py` 13/13, a third `regen_model.py` run at the pin 18:36-18:41 EST **EQUAL** in
  every file (326 s), `test_purity.py --only regen` 3/3, `test_gen_inout.py` 10/10.
  `pure-methods.tsv`, `lua-pure-bindings.tsv` and `purity-baseline.json` unchanged (none of the 36 is listed;
  `gen_pure_list.py --check` ok). On upstream master's port tree 76b3a5f70 (`--unlocked`, 196 s) a keyed comparison
  (by id; `line`, `registered_at`, `registration` left out, as in 3519) gives 3,465 common methods: **3,439 equal** (was
  3,406), **26 different** (3519's 25 changed by master plus `Player.GetReplayData`); the 33 methods of issue 0309 now
  all equal the pin's record. Of the 36 changed pin functions, `City.GetReligionGreatPersonRateModifier`,
  `City.GetSpecialistCityModifier` (master changed the body) and `City.GetSpecialistUpgradeThreshold` (callee signature)
  still differ, by `calls`/`access_rule` only. Checklist 10a (`research/06-tooling-and-workflows/new-vp-release-checklist.md`)
  corrected with these numbers.

## 2. Counts, reconciled with the research

| Quantity | Model (2026-11-10, e9d0c4837) | Research figure | Explanation |
|---|---|---|---|
| Interface classes on the boundary | **75**: 63 engine->DLL (implemented by the DLL's 53 `CvDll*` wrappers; 52 are direct bases, 11 are older versions in their chains such as `ICvGame1`, plus `ICvUnknown`), 12 DLL->engine (`ICvEngineUtility1-4`, `ICvUserInterface1-2`, `ICvEngineScriptSystem1`, `ICvEngineScriptSystemArgs1`, `CvDLLInterfaceIFaceBase`, `FIFileSystem`, `FILogFileMgr`, `FILogFile`) | 55 (2026-11-09) / 88 (2026-09-03) | 55 = lines containing `GUID guid` in `CvDllInterfaces.h` (54 GUID definitions + the `QueryInterface(GUID guidInterface)` parameter) [I: the only count that reproduces it]. 88 = `class ICv` occurrences in `CvDllInterfaces.h` (79, forward declarations included) + `CvDllInterfaces2.h` (9) [C: reproduced by grep]. Neither is an interface count; 75 is, by the compiler. `CvDLLFAStarIFaceBase` and `ICvSyncVar` are DLL-internal and excluded |
| Engine->DLL methods | **999** distinct virtuals; the 53 wrappers implement **1,106** methods counting inherited ones per wrapper | 1,106 instrumented (2026-11-09) | 1,106 reproduced exactly [C] |
| Virtuals | 1,615 distinct (999 + 616) | 958 (2026-09-03) | 958 = virtuals declared in `CvDllInterfaces.h` alone; reproduced exactly [C] |
| DLL->engine methods the DLL calls | 296 of 616: `ICvEngineUtility*` 162, `ICvUserInterface*` 97, script system 13 + args 9, `FIFileSystem` 6, `FILogFileMgr` 4, `FILogFile` 5; `CvDLLInterfaceIFaceBase` 0 | 164 used (2026-11-09) / 163 of 258 (2026-11-01) | 164/163 count `gDLL` (the engine utility) only: the model's 162 for `ICvEngineUtility*` (the plain `gDLL->` grep gives 163 names; one is not a call). The other 134 are the UI interface and services the earlier counts left out |
| Host exports | 22 (`server/src/srv_exports.cpp`) | 22 | Same list. All 22 are resolved by the x64 DLL from the server executable; **none crosses** |
| Lua methods | **3,463** compiled registrations in x86 and in x64 (none differs by arch, none `#if`-ed out); 3,462 distinct (`Player.GetExplorePercent` registered twice) + 5 `Fractal` functions = 3,467 in the model; plus 4 iterator closures (`Player.Cities/Units/UnitsInSquad`, `Map.Areas`) | 3,481 (2026-11-09) / 4,417 (2026-09-03) | 3,481 = `grep -o "Method(\w*)"` = 3,463 + 11 `#define Method` lines + 7 commented-out registrations, each listed with file:line in `lua.json` `reconciliation` [C]. 4,417 not reproduced; a `Method(` token count over `Lua/` today gives 4,452 (includes `BasicLuaMethod(`), so it was most likely a token count on an older checkout [I] |

Per library: Player 1,265 (1,264 distinct), City 616, Unit 537, Game 380, Plot 278, Team 181, Deal 61, League 61,
Map 38, Area 25, TeamTech 21.

## 3. Slot numbers: proven for both ABIs

- **Vtable order is not declaration order** (as epic 1000 found on x64): overloads are reversed
  (`ICvEngineUtility1::IsModActivated` slots 10/11, `endTurnTimerLength` 187/188, ...). Slot order differs from
  declaration order in 14 of the 75 interfaces, among the engine->DLL ones in `ICvPlot1` and `ICvPreGame1/2` [C].
- clang's dump and MSVC's vcall thunks agree for **all 3,460 pointers to member, x86 and x64** (2026-11-10,
  `proxy/model/slot-proof.json`); the 3 virtual destructors (`FILogFile`, `FILogFileMgr`, `ICvEngineScriptSystemArgs1`) have no
  pointer to member and are checked by the clang dump only [C].
- The x64 slots agree with all **458 rows** of the slot table the x64 smoke host measured at run time (issue 1007,
  `tools/x64-dll/evidence/2026-11-10-smoke/run3/interface-slots.tsv`) [L for those 6 interfaces].
- **x86 slots equal x64 slots for every method** (single inheritance, no virtual bases; the slot index does not
  depend on pointer size) [C].
- What is not proven: that the EXE's VC9 compiler lays out vftables the same way. VC9's own `cl.exe` (VS 2008, in
  `vsvars32.bat`) cannot parse the fork's C++11 headers (typed enums in `CvEnums.h`: error C2470), so the check used
  MSVC 14.4x. The Microsoft single-inheritance vftable rule has not changed since VC6 and the official x86 DLL works
  with these headers [I]. The proxy proof (section 8) adds a pointer-to-member table per interface so the first
  runtime test can compare against the EXE's live vftables.
- `CvString` is 28 bytes in x86/VC9 and 32 in x64; `FDataStream` 20/24, `FMemoryStream` 48/72, `FAutoArchive`
  32/48 (record dumps) [C]: none of these may cross by layout (PROTOCOL.md 5 already says so).

## 4. Marshalling classes and the unmarshallable list

Codecs (one table: `proxy/gen/marshal_types.py`; wire format PROTOCOL.md 5): `i32` (int, enums, uint, dword,
size_t narrowed), `bool`, `f32`, `u64`, `str`/`wstr` (incl. `CvString` and `std::string` by value or const
reference), `str_ret_interned` (a returned `const char*` must outlive the call: the proxy interns it), `guid`,
`handle` (any `ICv*` pointer: (type, id), proxy-owned wrapper objects), `out_scalar` / `out_str` (copy-out after
the call), `out_array_i32` (pointer + count pair, e.g. `ICvTeam1::GetTechs(TechTypes*, uint)`).

**90 distinct methods cannot be marshalled from their signature alone** (`model.json` `unmarshallable.items`,
each with its reasons). By category (a method can be in two):

| Category | Methods | Examples | Proposed treatment |
|---|---|---|---|
| Streams | 32 | `Read/Write(FDataStream&)` on Game, Player, Team, Map, Deal, Random, PreGame, NetInitInfo, NetLoadGameInfo; `ReadSupportingClassData`, `WriteReplay`; `GetSyncArchive()` returning `FAutoArchive&`; `send*SyncCheck(FMemoryStream&, ...)` | Write: server serialises into a buffer, proxy writes the bytes into the EXE's stream (chunked transfer, PROTOCOL.md 3.4). Read: see open decision MC-6 (the per-object "send the remainder" treatment of the proof is O(objects x remainder)). `FAutoArchive&`: the proxy owns one archive per object and refreshes it from the server when the EXE asks (multiplayer sync only) |
| Raw pointers | 20 | `GetDefineSTRING(char*, size_t, ...)`, `GetGreatWorkAudio(int, char*, int)`, `GetEvaluatedMapScriptPath(const char*, char*, uint)` (caller buffers with a size); `FirstUnit/NextUnit/FirstCity/NextCity(int*, bool)` (iteration cursor); `GetPlotDirectionX()` etc. returning `int*` to a static table; `TEMPGetRawCombatInfo()` `void*`; `TradeVisuals_NewRoute(..., int*, int*)`; `AddPopup(int, int, const int*, int, const bool*)` | caller buffer + size -> `out_buffer` codec (add to the table); cursor -> in/out `i32`; static tables -> proxy-side copies from the static mirror; the two pointer-array calls -> per-method codec using the count argument; `TEMPGetRawCombatInfo` -> answer NULL (debug only) [I] |
| Plain structs | 16 | `IDInfo` (by value, pointer, reference), `CvPopupInfo`, `CvColorA`, `UnitMoveRate`, `CvCombatMemberEntry*`, `CvMapInitData*`, `CvWorldBuilderMapLoaderMapInfo`; **`HeadUnitNode()/NextUnitNode(IDInfo*)` on plots and `headSelectedCitiesNode()/nextSelectedCitiesNode()` on the UI** | one field codec per struct (layouts in `model.json` `records`). The node walkers are the hard case (below) |
| Lua state | 16 | `ICvScriptSystemUtility1::RegisterScriptLibraries(lua_State*)`, `Push/GetCv{City,Deal,Plot,Unit}Instance(lua_State*, ...)`, `PushReplayFromStream`, six `ICvWorldBuilderMapLoader1` functions taking `lua_State*` | the proxy registers its own thunks in the EXE's VM (section 6) and builds instance tables from handles; the WorldBuilder functions are re-implemented in the proxy over server calls | *Updated 2026-11-15 (ticket 0343, `civ5map-format.md` section 7): LoadModData, GetMapPreview, GetMapPlayers and AddRandomItems are served: the proxy registers the engine's `lua_State` as an engine Lua thread for the call, the server runs the DLL's method on a scratch thread borrowed for it (globals fetched from the engine thread, LoadFile and fetched functions run in the engine VM), the results come back with the Lua codec; LoadModData's two fields of `MapModData` are set into the engine's table. ~~ScatterResources and ScatterGoodies (the world builder's) stay loud defaults for 0326; `test_proxy_rt.py`'s strict checks now use ScatterGoodies.~~* *Updated 2026-11-16 (ticket 0326, `civ5map-format.md` section 10): ScatterResources and ScatterGoodies are served the same way (`proxy_lua.cpp` `WbLuaMethod`, `srv_remote_lua.cpp` `WbServe`); all six functions of the row are done, and no loud default of an engine-to-DLL method is left reachable from a context object without a game (the strict checks' target: `civ5map-format.md` section 10).* |
| STL containers | 6 | `setVictories(const std::vector<bool>&)`, `GameplayUnitMoved(ICvUnit1*, const CvPlotIndexVector&)`, the four `send*SyncCheck` (vector of string pairs) | element-wise codecs; never by layout (VC9 vs MSVC 14 STL) |
| Engine objects | 5 | `SetGameDatabase(Database::Connection*)`, `SetDLLIFace(ICvEngineUtility1*)`, `SetEngineUserInterface(ICvUserInterface2*)`, `loadFromIni(FIGameIniParser&)`, `Debug_GetHeap()` | proxy-local: the proxy keeps the EXE's objects and the server has its own (DB copy, its own engine interfaces); `loadFromIni` -> the proxy reads the parser and forwards values |
| Variadic | 4 (`FILogFile::Msg/DebugMsg/WarningMsg/ErrorMsg`) | | server-local (logs), never cross |

**The three hardest cases**

1. **Pointer-identity walkers.** `ICvPlot1::HeadUnitNode()/NextUnitNode(IDInfo*)` return pointers into the DLL's
   per-plot unit list, and the EXE walks the list by feeding each pointer back; `ICvUserInterface1::
   headSelectedCitiesNode/nextSelectedCitiesNode` is the same in the other direction. A pointer cannot cross
   processes. Treatment: the proxy materialises a per-plot copy of the list (a mirror refreshed when the plot's
   units change) whose nodes have stable proxy addresses; the server keeps a copy of the EXE's selected-cities list.
   Correctness depends on refreshing at the right moments [I].
2. **Save/load and sync streams.** 32 methods; the EXE drives the order (`Write` per object, `ReadSupportingClassData`,
   replay), the DLL decides how many bytes each object takes, and a load hands the DLL one stream for many objects.
   `GetSyncArchive` returns a reference the EXE keeps. Needs the per-call protocol of MC-6 and a byte-exact
   round-trip test against the 32-bit DLL's saves.
3. **Lua state crossings.** `RegisterScriptLibraries(lua_State*)` is where the DLL installs its 3,463 methods into
   the EXE's VM; with the DLL out of process the proxy must install generic thunks instead, keep Lua-side instance
   identity (`Players[i]`, unit/city/plot tables) consistent with server handles, and carry `GameEvents` (MC-2).

## 5. Per-call policy

Policies: **local** (the proxy answers alone: lifetime, static handles, values carried by the handle), **mirror**
(answered from a proxy-side copy the server keeps current), **sync** (a synchronous call), **post** (batched,
fire-and-forget), **server_local** (never crosses), **unused** (the DLL never calls it). `now` is what the generator
should emit today; `target` is what it can emit once measurement 2106 (below) confirms it. Counts over the 1,615
distinct virtuals (`model.json` `policy_counts_now` / `_target`):

| Direction | local | mirror | sync | post | post candidate | server_local | unused |
|---|---|---|---|---|---|---|---|
| engine->DLL (999), now | 58 | 383 | 558 | 0 | - | - | - |
| engine->DLL, target | 58 | 383 | 253 | 0 | 305 | - | - |
| DLL->engine (616), now | - | 0 | 213 | 39 | - | 44 | 320 |
| DLL->engine, target | - | 7 | 80 | 39 | 126 | 44 | 320 |

Rules (each method carries its rule in the model):
- **local**: `QueryInterface`/`Destroy` (55% of today's per-frame inbound calls); static handles `GetGame`,
  `GetPreGame`, `GetMap`, `GetPlayer(i)`, `GetTeam(i)`, `Get*Info(i)`, `GetPlot(x, y)`, `GetPlotByIndex` (24%);
  `GetID`/`GetOwner`/`GetIDInfo` of units and cities and `GetPlotIndex`/`GetPosition` of plots, carried by the handle
  (MC-4). These remove the ~31k nested `GetOwner`/`GetID`/`GetPlotIndex` per AI turn.
- **mirror** (proxy side), with invalidation:

  | Mirror | What | Invalidation |
  |---|---|---|
  | `static_info` | every `ICv*Info1` getter | immutable after `CacheGameDatabaseData`; snapshot after boot and after each database reload |
  | `context_static` | `ICvGameContext1` counts, defines, direction tables | same |
  | `pregame` | every `ICvPreGame*` getter | write-through: every pregame setter goes through the proxy, which updates the mirror first; the server posts a snapshot after load/new game and whenever a top-level call changed pregame (record diff at the end of each call) |
  | `players` | `IsHuman`, `slotStatus`-side player fields, `IsAlive`, `IsEverAlive`, `IsTurnActive`, `GetTeam`, civ/leader/handicap/colour/era, names, `HasTurnTimerExpired` | per-player record, diffed by the server at the end of every top-level call, changes posted; the proxy drains posts before it returns to the EXE (PROTOCOL.md 4 rule 1). `HasTurnTimerExpired` is mirrored only while the end-turn timer option is off (it mutates state when on; research `interfaces-and-traffic.md` 5) |
  | `game` | `GetActivePlayer`, `GetActiveTeam`, `GetGameTurn`, `IsPaused`, `GetGameState`, `IsFinalInitialized`, options, ... | game record, same rule |
  | `map_dims` | grid size, wrap, plot count | fixed per map: snapshot at map init/load |
  | `plots` | render getters (terrain, feature, resource, improvement, route, river, revealed, FOW mode, owner, ...) | whole table pushed after load; then the changed plot records ride on the notifications the DLL already sends on visual change (`GameplayPlotStateChange`, `GameplayFOWChanged`, dirty bits). Coverage of every field by those notifications is unverified [I] |
  | selection (server side, target) | `GetHeadSelectedUnit`, `isCitySelection`, `IsUnitSelected`, `isCitySelected`, `getSelectionPlot`, `shiftKey`/`altKey`/`ctrlKey`, `IsSPAutoEndTurnEnabled` | the proxy pushes a snapshot at each selection change and at the start of each top-level call [I]; until then sync |
- **sync**: anything that returns a value or has out-parameters; `Update` (the core channel's top-level call);
  every DLL->engine call whose engine handler calls back into the DLL (`GameplayUnit*`/`GameplayCityCreated` ->
  `UpdateDeferredFog`, `GameplayEraChanged` -> `SetupGraphical`, `AutoSave` -> the `Write` sequence,
  `isCitySelected` -> `GetIDInfo`); and, **for now, every void command and notification** other than the ones below,
  because a post handler may not make a synchronous call (PROTOCOL.md 4 rule 3) and nothing yet says which
  handlers re-enter.
- **post**: flag-only `setDirty` and `SetSpecificCityInfoDirty` (research `ipc-design-and-measurements.md` 7.2) and the
  net sends (`send*`, answered later through `Response*`, engine contract 1.4/6.4).
- **post candidate** (target): the other void calls; they become posts per method once task 2106 records, for each
  inbound call, the outbound call it is nested in.
- **server_local**: lock and thread plumbing (`IsGameCoreThread`, `Has/Get/Release/TryGameCoreLock`,
  `GetScriptSystem`, ...: PROTOCOL.md 2.3), the script system and its args (with gameplay Lua in the server, MC-2),
  file system and logs, the 22 exports.

## 6. The UI-Lua path: decision proposal (MC-1)

Measured load (2026-11-09 huge save, research `interfaces-and-traffic.md` 3): 0.25-6 calls per frame at steady state,
bursts of ~600 in one 16.7 ms slot, 3-14k when a screen opens, 1.07-1.21 M per AI turn; the top 10 methods are 59-63%
of AI-turn calls (`Unit.IsHasPromotion` alone 36% in f1) and 71% of pan/zoom calls. Inventory (`lua.json`, worker 2102):
2,842 read, 575 write (no result: postable), 50 mixed; 2,757 mechanically cacheable (read, scalar/instance arguments
and result), 2,748 of them depend only on (instance, arguments) and game state; **3,456 of 3,467 can be carried by
one generic Lua-value codec** (numbers, booleans, strings, nil, instance handles, nested tables); 11 need hand-written
thunks (the 4 iterator closures, the 5 `Fractal` userdata functions, `Game.SpewTestEvents`, `Plot.SetCityPurchaseID`).
No method takes a Lua callback. All top-25 methods of every measured scenario are read and cacheable [C/I].

| Option | Per AI turn (f1 / f2), at 0.44 us per call | Worst slot | Engineering | Risk |
|---|---|---|---|---|
| A. UI Lua stays in the EXE's VM; every DLL method is a proxied call through generic thunks (+ a per-epoch read cache whose hit rate is unknown, so not counted) | 0.53 / 0.47 s of Lua calls; whole projection 0.60 / 0.53 s | 14.1k calls (diplomacy overview opening) = 6.2 ms in one slot; f1 max slot 53k = 23 ms | low: one codec, 11 hand thunks | stale reads none (always asks) |
| B. Mirror of read-mostly game state on the proxy, Lua answered from it | ~0 for mirrored reads; but 2,000+ of the cacheable methods compute (yields, `CanBuild`, `GetBestDefender`): they would need DLL logic on the proxy side | ~0 | very high: thousands of fields and invalidation hooks inside the DLL | stale or wrong reads are silent |
| **C. Hybrid (recommended)**: A, plus a proxy mirror of the few hot pure reads (`Unit.IsHasPromotion` as a per-unit promotion bitset, `City.IsHasBuilding`, `Player.IsMinorCivDisplayedQuestForPlayer`, `Game.GetCorporationFounder`, `City.IsFranchised`, `Plot.GetPlotCity`; `Map.GetPlot`, `City.GetOwner`, `Player.GetTeam` are already local or mirrored), invalidated by the posts the DLL already sends for those objects | 0.21-0.22 / 0.18-0.19 s of Lua calls (top 10 taken out); whole projection **0.27 / 0.24 s** | same as A minus the mirrored share | low-medium: A plus ~6 mirrored fields | limited to 6 fields, each with a test |

Recommendation: **C**. It keeps the UI and mod Lua where they run today, costs 1% or less of an AI turn's 25-27 s of
DLL compute (0.2-0.5% of its 58-127 s wall time), and its only stale-read exposure is six fields chosen from the
measurement. The per-epoch cache in A/C (results valid until the next server post) is an optimisation to measure
in the first live run, not a requirement.

## 7. Projected cost (`model.json` `projection`)

Assumptions: x86 client / x64 server through vpipc (research `ipc-design-and-measurements.md` 7.4): synchronous call
**0.44 us**, nested callback 0.40 us, post 0.06 us, one event wake **15 us** for the first call of a frame (the
server has slept past `idleSpinUs` 50 since the previous frame). Only the top 25 methods per scenario and thread were
recorded; the rest of each class takes the class default (getter = sync, command/notify = sync now / post
candidate target, lifetime and static handles = local, plumbing and script = server_local). Mirror maintenance
posts are not counted (expected far below the notification volume) [I]. Frame budget at ~155 fps: 6.4 ms.

| Scenario | Policy now | Target | Synchronous calls per frame (target) |
|---|---|---|---|
| idle frame | 24.4 us (0.38% of 6.4 ms) | 23.5 us | 19 (getters outside the top 25) + 0.1 Lua; the 15 us wake dominates |
| pan/zoom frame | 27.6-29.2 us (0.45%) | 26.8-28.3 us | 24 + 2.3 Lua |
| city view frame | 22.5 us | 22.3 us | |
| AI turn f1 (350->351), hybrid Lua | 0.30 s | **0.27 s** | main: 473k Lua + 75k sync + 44k nested; core: 14k sync + 20k nested + 17k posts + 9k post candidates |
| AI turn f2 (351->352), hybrid Lua | 0.27 s | **0.24 s** | main: 419k Lua + 54k sync + 39k nested; core: 18k sync + 18k nested + 29k posts + 9k post candidates |
| AI turn f1 / f2, all Lua proxied | 0.63 / 0.55 s | 0.60 / 0.53 s | |

Compared with the 2026-11-09 naive projection (0.27 ms per idle frame, 0.87-1.0 s per AI turn with the raw
microbenchmark latency): ~11x less per frame, 3-4x less per AI turn, at twice the per-call latency. Per frame the
remaining cost is the single wake; if the server's UI-channel thread spun across the whole 6.4 ms frame instead
(one core), it would fall to ~9 us.

## 8. Generator skeleton and compile-only proof (worker 2105)

`python proxy/proof/build_proof.py` (offline, ~65 s, 2026-11-10): `proxy/gen/gen_proxy.py` emits, from the model, the
proxy class (`Proxy_<I> : public <I>`, one `override` per virtual, so the compiler checks signature, `const` and
`__stdcall`), the server dispatch, method ids and an FNV-1a app schema hash over names and both slots; for
`ICvRandom1` (10 methods) and `ICvTeam1` (20). Results [C]:
- x86 proxy side: clang-cl `-m32` in the VS 2008 environment with the DLL's include dirs and defines (`_MSC_VER`
  1500, `__cplusplus` 201103, `_CPPLIB_VER` 505): compiles, links into a DLL importing only `MSVCR90` and `KERNEL32`;
  all 30 methods public virtual `__stdcall` in the mangled names; dropping `DLLCALL` or `const` in an override is a
  compile error (negative check).
- x64 server side: clang-cl `-m64`, MSVC 14.43 STL, `/std:c++14`, `VP_OUT_OF_PROCESS` (`_MSC_VER` 1943, `_CPPLIB_VER`
  650): compiles and links with the vpipc sources exported from the epic-1200 branch (5b9f06f).
- Not shown: any runtime behaviour; the wrapper table, server handle table, memory streams and string interning are
  link stubs; the stream hooks are hand-written sketches.

**Build rule for every proxy source and header: the fork's STL layout** (added 2026-11-13, issue 3503). The proxy is
compiled against the VC9 headers, where `_SECURE_SCL` and `_HAS_ITERATOR_DEBUGGING` change the size of `std::string`
and `std::vector`. The fork's `CvGameCoreDLLPCH.h` turns both off; every proxy TU must do the same **before its first
standard header** (`#ifndef` / `#define _SECURE_SCL 0`, `_HAS_ITERATOR_DEBUGGING 0`), and a proxy header that includes
a standard header must carry the same lines at its top (`proxy_rt_logic.h` since 2026-11-10, `proxy_purity.h` since
2026-11-13). A TU that misses them links one shared template instantiation (`std::vector<std::string>::insert`) of the
other layout and corrupts the stack at run time; unit tests built from that TU alone do not see it. Seen twice: the
`IsRunFolderName` crash of 2026-11-10 and the purity loader of 3503 (section 16.9).

**Build rule: a proxy TU that calls the Lua C API includes the Lua 5.1 headers itself** (added 2026-11-14, issue 3518).
`extern "C" { #include <lua.h> #include <lauxlib.h> }` (and `<lualib.h>` for `luaL_openlibs`), from the fork's
`ThirdPartyLibs/Lua51/include` (proxy/gen/toolchain.py `INCLUDES`). Never rely on the DLL's precompiled header for them.
Cause [C, read at both commits 2026-11-14]: `CvGameCoreDLLPCH.h` includes `Lua/CvLuaSupport.h`; at the pin 04b1211b8
(Release-5.4.8 base) that header includes `<lua.h>`, `<lualib.h>`, `<lauxlib.h>` and `FireWorks/FLua/include/FLua.h`; on
upstream master (fork branch `x64-release-master-ba8aafdc-r2` 76b3a5f70, after upstream #13489 and its Lua API rework)
it includes only `CvLuaArgsHandle.h` and declares `typedef struct lua_State lua_State;` and `lua_CFunction` itself
("Can be used to invoke lua functions without actually including lua headers"). Those typedefs are the same types as
`lua.h`'s, so including both compiles in either order (C++ allows a repeated typedef of the same type), and the forward
declaration `struct lua_State;` of `proxy_ui_bridge_rt.h` fits both. Files that use the Lua API: `proxy_lua.cpp`,
`proxy_luaexec.cpp`, `proxy_ui_bridge.cpp` (which always included the headers, so it built on master) and the hutest
host `proxy/tools/hutest/hutest_host.cpp`; the x64 side (`hutest_dll.cpp`, the server) gets them from
`server/src/srv_common.h`, which includes them itself. No proxy file uses `FLua.h` or `luaL_optbool`, the other two
things the 5.4.8 header gave.
Evidence [L, 2026-11-14, workstation, sources in `C:/c5-64bit-work/3518/`]: main d5ff12e3's `build_proxy.py --src
<master 76b3a5f70> --unlocked --config release` fails, 2 TUs (`proxy_lua.cpp`, `proxy_luaexec.cpp`), 40 errors, the
first `proxy_lua.cpp(106,54): use of undeclared identifier 'lua_gettop'` (as the 3507 dry run, `release-driver.md`
W7.2 rows 10 and 15); with the fix the same command is OK in 33 s (proxy `1e88a615...`, 1,090,048 B). Main's
`build_hutest.py` with `--src` master fails at `compile hutest_host.cpp` (20 errors, `luaL_newstate` first); with the fix
it passes, 46 checks, 0 failed. On the pin (git checkout 04b1211b8, `server/build_server.py --config release --offline
--jobs 6`, 948 s, every build_all step rc 0, narrowing 0 new / 4 gone, smoke exit 0; x64 DLL `96d80c88...`, proxy
`466aeaf7...`): `test_proxy_rt.py` 79/79 (56 s), `build_hutest.py` 46 checks 0 failed, `--scenario all` 8/8.
Not done: a full master `build_server.py` (its `build_all` needs master's test database for the smoke step, 5.4.8's traps
as in W7.2 row 9, and the 10 narrowing rows the dry run added to its copy only), so the master hutest ran with the pin's
`server.exe`, proxy and stubs (`--b`/`--deps`) and master's headers for the host and the fake DLL.

## 9. Open design decisions for the MC

## 9. Open design decisions for the MC

| # | Decision | Options | Recommendation |
|---|---|---|---|
| MC-1 | UI-Lua path | A all proxied / B mirror / C hybrid | **C** (section 6) |
| MC-2 | Where gameplay `GameEvents` Lua runs | (a) in the server's Lua state with the DLL (hooks never cross; mod scripts that also touch UI must be split) / (b) in the EXE's VM (30-38k synchronous hook calls per AI turn plus `CreateArgs`/`DestroyArgs`, each with its args marshalled; +0.03-0.05 s per turn) | **(a)**, after an inventory of which installed mods register `GameEvents` from UI contexts (not done) |
| MC-3 | Void commands and notifications | keep synchronous / post by default | **sync until task 2106** measures which handlers re-enter (cost difference ~5 ms per AI turn) |
| MC-4 | Handle encoding inside PROTOCOL.md's `(u32 type, i32 id)` | type = interface kind only / type = kind \| owner << 8 for units and cities | **kind \| owner**: makes `GetOwner`/`GetID`/`GetIDInfo` local (~31k nested calls per AI turn) without changing the protocol |
| MC-5 | Mirror updates | server diffs small records at the end of each top-level call (DLL unchanged) / dirty hooks in the DLL | **diff** for players, game, pregame; notification-attached records for plots |
| MC-6 | Save/load streams | per-object round trips (the proof's sketch: O(objects x remainder) bytes on load) / one transfer of the DLL's whole save section on the first `Read`/`Write` of a save, sliced per object | **one transfer**, after checking the EXE's call order on a save (headless traces have the `Write` sequence) |

## 10. Not verified

- No runtime: no proxied call, no mirror, no cache, no posted notification has been exercised.
- x86 slots are proven with MSVC 14.4x and clang, not with VC9 itself (section 3).
- Policy rules, mirror field lists and the re-entrancy list are [I] unless the rule cites a trace; task 2106 is the
  measurement that turns "post candidate" into post or sync.
- Costs rest on top-25 lists per scenario plus class defaults, on one save (huge, turn 350) and one machine, and on
  latencies measured under 25-60% background load.
- The DLL->engine usage scan is textual; a call through a stored pointer to an engine interface would be missed.
- The Lua access classes rely partly on getter/mutator names [I]; hidden-state detection is one call level deep.

## 11. Runtime design as built in epic 2300 (written 2026-11-10 10:58 EST, team-2300 coordinator)

The build specification of the first running proxy and the server's remote mode (tracker 2300). Correctness first:
every call synchronous, nothing mirrored, nothing batched, nothing answered locally except `QueryInterface` of a
wrapper's own interface chain. Results and measurements: section 12. Wire values: `proxy/include/vp_wire.h`; runtime
APIs the generated code calls: `proxy/src/proxy_rt.h` (x86) and `server/src/remote/srv_rt.h` (x64).

### 11.1 Processes and boot

- The proxy is `CvGameCoreDLL_Expansion2.dll` (x86, VC9 ABI, msvcr90, imports `lua51_Win32.dll` like the real DLL),
  exporting `DllGetGameContext` only (the fork's `CvGameCoreDLL.def` has no other export). Its settings come from
  `vpproxy.ini` next to the proxy DLL (or the file named by `VPPROXY_INI`): the server command line, the log file,
  the database path, `on_server_death`, `strict`, timeouts.
- `DllGetGameContext` (first call): create the vpipc client endpoint (`Local\VPIpc.<pid>.main`, app schema hash =
  the generated `kAppSchemaHash`), start the server (`server.exe --remote --section <name> --config <cfg>`), wait for
  it (`wait_for_server`), send `kRt_Boot`: the server loads the x64 DLL, calls its `DllGetGameContext`, registers the
  context and returns its handle; the proxy returns a wrapper. Any failure: logged, `DllGetGameContext` returns NULL
  (the engine reports a DLL that would not load).
- Shutdown: the engine's last call is `SetDLLIFace(NULL)` (engine-contract 8). The proxy forwards it, then sends
  `kRt_Shutdown`; the server writes its report and exits; the proxy waits for the process and closes the endpoint.
  Nothing crosses inside `DllMain` (loader lock).

### 11.2 Threads and channels

- Proxy: the thread that called `DllGetGameContext` is bound to the `ui` channel; the first other thread that calls
  the proxy gets `core`; every later thread the shared `aux` channel. The 32-bit headless host is single-threaded
  (it calls `Update` on its main thread and answers `IsGameCoreThread` by role; read in its source
  `host/engine/HHEngineServices.cpp`, 2026-11-10), so only `ui` carries traffic in the proof. For the real EXE the
  binding must be by role (the core thread is the one that calls `Update`): open item.
- Server: one service thread per channel (`serve()` loop); the DLL runs on the service thread of the channel the
  engine thread is bound to, so one engine thread always maps to one server thread and the DLL's game-core lock
  (taken by its wrapper constructors) stays per thread.
- Server-local in remote mode (never cross): the game-core lock (`Get/Release/Has/TryGameCoreLock`, a recursive lock
  per server thread, as in headless mode), `IsGameCoreThread` (true only inside the dispatch of `ICvGame1::Update`,
  the role the direct server and the 32-bit host answer), `IsGameCoreExecuting` (false), the 22 host exports (file
  system, log files, string init data, temp heap, FSBA), log files. Everything else of `ICvEngineUtility1-4`,
  `ICvUserInterface1-2`, `ICvEngineScriptSystem1` and `ICvEngineScriptSystemArgs1` crosses (D21 items 2 and 3), the
  net `send*` included: the engine loops them back, so the server's own loopback is off in remote mode (server-host
  11.2). The 11 methods the model misses (server-host 11.3) cross like the rest: every engine method is generated.

### 11.3 Handles and object lifetime

- `vpipc::Handle {type, id}`: type = kind (the most derived interface the object implements, `kT_<I>`) | owner
  byte (0 in this epic) | engine bit; id = slot in the owner's table (vp_wire.h).
- A DLL object returned to the engine (`GetGame`, `GetPlayer`, ...) is registered by the server and wrapped by one
  new `Proxy_<MD>` (MD = most derived interface of its kind) that the engine owns; `QueryInterface` answers the chain
  of MD locally and forwards any other GUID; `Destroy` forwards (the DLL's wrapper destructor releases the game-core
  lock on the right server thread), then the server removes the slot and the proxy deletes the wrapper.
- A DLL object passed as an argument of a DLL -> engine call (`GameplayUnitCreated(ICvUnit1*)`) is registered
  temporarily and wrapped temporarily for the duration of that call (the DLL frees its own wrapper afterwards).
- Engine objects (`SetDLLIFace(engine)`, `SetEngineUserInterface(ui)`, `GetScriptSystem()`, `CreateArgs()`) are
  registered in the proxy's engine table; the server stands for each by one `RemoteGen_<I>` subclass object per
  handle (cached, so pointer identity holds for the DLL).

### 11.4 Generated code (`proxy/gen/gen_runtime.py`)

From `proxy/model/interfaces.json` for all 75 interfaces, into a build directory (not committed):
- shared `vp_ids.gen.h`: `kT_<I>`, `kM_<I>_<method>` = (kT << 16) | index over the full chain, `kAppSchemaHash`,
  `vpk_most_derived[kind]`, `vpk_in_chain(have, want)`, the GUID -> kind table, a method-name table for reports.
- engine -> DLL interfaces: proxy `Proxy_<I>` classes (x86) and server `Dispatch_<I>` (x64); DLL -> engine
  interfaces: server `RemoteGen_<I>` classes forwarding every method (x64) and proxy `EngDispatch_<I>` calling the
  real engine object (x86).
- Codecs beyond 2100's: FDataStream& in/out by method name (vp_wire.h), `IDInfo` by value / const reference /
  pointer, caller buffers (`char*` + size), iteration cursors (`int*` in/out), `std::vector<bool>`,
  `CvPlotIndexVector`, CvString returned by reference (interned). A method left non-mechanical calls a hand-written
  hook: `ProxyHook_<I>_<M>` (proxy, engine -> DLL), `ServerHook_<I>_<M>` (server dispatch), `EngHook_<I>_<M>`
  (proxy dispatch of DLL -> engine); a hook with no hand-written definition in `proxy/src` or `server/src/remote`
  gets a generated loud default (`ProxyRt::Unimplemented` / `SrvRt::Unserved`: counted, listed, strict mode fails).
  On the server, non-mechanical DLL -> engine methods are virtuals of `RemoteGen_<I>` with a loud default that the
  hand-written subclass overrides.

### 11.5 Lua (D21 items 1 and 2)

- All Lua runs in the engine's VM. `RegisterScriptLibraries(L)` is answered by the proxy: it asks the server
  (`kRt_LuaRegister`), which runs the DLL's own `RegisterScriptLibraries` on a fresh server Lua state and returns the
  globals it created, encoded with the Lua value codec (vp_wire.h); the proxy rebuilds them in the engine state with
  one generic C thunk per DLL function (upvalue = function id). A thunk encodes its arguments and sends
  `kRt_LuaCall`; the server pushes them on its scratch state of the calling channel (one `lua_State` per service
  thread), calls the DLL's C function in protected mode and returns the results, or the error, which the thunk raises
  with `lua_error` in the engine VM, so the engine's error accounting sees it.
  The scratch state is registered once, by the first request that registers something (issue 0308, 2026-11-14: upstream
  master's DLL registers nothing in the `!VPDLL` thread of `NotifyDatabaseCached`, and that nested request came first,
  which left `Game`, `Map`, `Players` nil in every engine state; live evidence and master's other differences, C closures
  for every function and userdata instances, in `research/06-tooling-and-workflows/new-vp-release-checklist.md` 10a-10b).
- Instances (`Players[i]`, units, cities, plots, ...) cross as (type name, server pointer); in the engine VM they are
  instance tables `{__instance = lightuserdata token}` with metatable `LuaTypes[name]` built from
  `kRt_LuaDescribeType`; on the server a plain `{__instance = lightuserdata pointer}` is what the DLL's
  `GetInstance` reads (`CvLuaScopedInstance.h`: it checks only the `__instance` field). Closures and userdata the DLL
  returns (iterators, `Fractal` objects) stay on the server (`kLSRef`); Lua functions of the engine (map script
  functions) stay in the engine (`kLERef`). *(Added 2026-11-24, ticket 0314: on upstream master's DLL, whose instances
  and statics are userdata of the registry's `vpdllinternals` types, they cross as kLInstance too and are engine-side
  userdata with the type's function table; section 35.)*
- The DLL's own use of Lua states (map generation, `CvMapGenerator.cpp`: 81 direct `lua_*` calls, three
  `CallCFunction`s with DLL function pointers and `lua_gc`): `CreateLuaThread` creates the engine thread T (forwarded)
  and a server scratch thread S whose globals table has an `__index` that fetches T's globals (`kRtE_LuaGetGlobal`;
  functions become `kLERef` stubs whose call is `kRtE_LuaCall` through the engine's own `CallFunction`). The DLL gets
  S: `LoadFile(S)` and `FreeLuaThread(S)` are forwarded for T, `CallCFunction(S, f, ud)` runs `lua_cpcall` on S
  locally, `CallFunction(S, ...)` calls locally (a stub crosses), direct `lua_*` calls work on S. Values the codec
  cannot carry (Lua functions passed as arguments to DLL functions, threads, light userdata other than instances,
  tables over the limits) cross as `kLUnsupported`: nil on arrival, logged once, counted, listed.
- `ICvScriptSystemUtility1::Push*/Get*Instance(L, ...)` and `PushReplayFromStream`: the proxy reads or writes the
  engine stack, the server runs the DLL's own method on its scratch state.

### 11.6 Saves and streams (D21 item 6)

`Write*(FDataStream&)`: the server writes into its own memory stream and returns the bytes in the RETURN; the proxy
writes them into the engine's stream. `Read*(FDataStream&)`: on the first read of an engine stream at a position
the proxy reads the rest of that stream once (`GetSizeLeft`, `ReadIt`, back with `SetPosition`), sends it with
`kRt_StreamOpen` (one transfer per load), then each `Read` names the session and offset and returns the bytes
consumed, by which the proxy advances the engine stream. Sync archives (`GetSyncArchive`, `send*SyncCheck`) are
multiplayer-only: loud stubs.

### 11.7 Database: the server opens its own copy (decision, minor, 2026-11-10)

`SetGameDatabase(conn)` is answered by a hook that sends the database file's path (`vpproxy.ini` `db=`); the server
copies the file into its run folder, opens it and passes its own connection to the DLL (as in headless mode). Why
not proxied database calls: `Database::Connection` has no virtual methods (the DLL calls the closed library's code
directly with the connection pointer; its first member is the `sqlite3*`, `CvGameDatabase/include/Database.h`), the
boot alone issues hundreds of thousands of queries (`CacheGameDatabaseData` 10-11 s locally) and result objects would
have to cross by layout between two STLs. Consequence: the DLL's writes (`PerformDatabasePostProcessing` fills
`Defines` from `PostDefines`, `CvDllDatabaseUtility.cpp`) reach the server's copy, not the engine's; the engine's Lua
`GameInfo`/`GameDefines` see the engine's file. For the headless host's pinned database the post-processing is
idempotent (`INSERT OR REPLACE`). For the real EXE, how the proxy learns the database file (and whether the engine's
database is a file at that moment) is an open item.

### 11.8 Failure policy: what the engine sees

A call that does not return Ok (`PeerDied`, `PeerClosed`, `PeerHung` with failOnHung, `Timeout`, `ProtocolError`)
is logged with the method and status to the proxy log and to stderr. `on_server_death=exit` (default): the proxy ends
the host process with exit code 3 at once: the game state lives in the dead server, so continuing would only feed
zeros into the engine. `continue` (diagnostics): the call returns a value-initialised result and every later call
fails at once, never waits. Either way no call waits on a dead server longer than the transport's death detection
(2.5-3.1 ms measured, `ipc-design-and-measurements.md` 7.5).

### 11.9 Counters

Both sides count per method id: calls, payload bytes each way and wall time (the caller measures the round trip, the
callee the handler time; transit = difference). The server writes per-turn rows (game turn read after each
`Update`) by interface and direction to `proxy-calls-per-turn.tsv` and totals to `proxy-calls.tsv` in its run
folder; the proxy writes its totals to its log at shutdown.

### 11.10 Changes made in integration (2026-11-10 11:57-12:17)

- **Payload lifetime** [L]: a served CALL's payload and a received RETURN's payload live in the channel's data area,
  which the next nested call on that channel overwrites. Both runtimes now copy them first (`OnCall` copies the
  arguments; `End` keeps the RETURN in one of 64 per-thread buffers). Found as a host crash: the engine's
  `CreateLuaThread("VP_LUAAPI")` nests `RegisterScriptLibraries`, whose RETURN overwrote the thread name the proxy's
  hook still used. vpipc's debug validity check (pages decommitted when a payload expires) made it fault at once.
- **Local QueryInterface is a reference** [L]: the engine destroys both the net message handler and its
  `QueryInterface` result (the DLL's `Destroy` of that singleton is a no-op); a local QI answer now adds a wrapper
  reference and `Destroy` forwards only the last (`ProxyRt::WrapperAddRef/WrapperRelease`, added to `proxy_rt.h`).
- **Return statuses** moved to 16-20 (`vp_wire.h`): 2-6 collided with the transport's own (3 = protocol error).
- **Loads write into the engine's cache folder**: the DLL writes the save's embedded `Civ5SavedGameDatabase.db`
  through `FIFileSystem::Create` into `GetCacheFolderPath()`, which crosses and names the host's folder; the server's
  file system refused paths outside its run folder, `Game::Read` went on misaligned and `Map::Read` crashed. The
  engine's cache folder is now a second write root (`SrvSetExtraWriteRoot`).
- **`Player` and `Team` globals** alias `LuaTypes.Player/Team` in the engine VM, as the DLL's `RegisterScriptData`
  does (the server sends type tables empty).
- **vpipc in the proxy is built with `NDEBUG`** in release: without it every received message was copied into fresh
  pages and decommitted (the validity check above), about 5 us per message (measured below).

## 12. Results of epic 2300 (2026-11-10, measured 11:57-12:22 EST) [L]

Setup: the 32-bit headless host (`civ5headless.exe` build 20261027-120431, run as a tool) loads the proxy as its
DLL; the proxy starts the x64 server in remote mode; the x64 DLL is the locked build (fork x64 e9d0c4837, release,
SHA-256 `22f05bf5...`), proxy `1dbeaba6...`, server `ad182e3f...` (`server/build_server.py --config release`, which
also builds the proxy). The direct run is the same server binary playing the same spec headless. Database: the
pinned VP 5.4.8 cdrift database (`bd9b8aa1...`) for both; game folder: the toolkit copy; host `--no-mods`
`--no-dumps` `--no-save-final`, UI emulation profiles on. Harness: `proxy/tools/run_proxy_game.py`; evidence:
`server/evidence/2300/`. Machine: i9-13900KS, about 50% loaded by other teams' work during every run.

**Correction (issue 9008, 2026-11-10).** The harness that judged these runs could not fail where it should (section
13): `fail_on_unexpected` never reached the server, the pass verdict ignored the server's exit code, report and
counters, and two empty databases compared IDENTICAL. The stats.db identities below were real comparisons and stand;
"0 Lua errors, clean shutdown" missed one failed served call per run (`rtE:LuaGetGlobal`, a server bug, 13.3) and
`t50c`'s unserved `AddPopup`. The Tiny proofs were re-run strict in 9008 and pass (13.4). How every run was made,
command by command: `server/evidence/2300/README.md`.

| Proof | Result |
|---|---|
| Tiny Continents 30017/40017, new game, 50 turns (`t50c`) | **every `stats.db` table identical** to the direct run; host: 0 Lua errors, status passed; server: 0 scratch Lua errors; clean shutdown (server exit 0, endpoint closed) |
| Loaded save: the server's own Tiny turn-50 save, 50 -> 60 (`load50b`) | identical; the load went through one `kRt_StreamOpen` per stream (pregame 305,375 B, body 13,579,064 B) |
| Huge save (`VP8P-HUGE_0350` copy, Huge Continents 128x80, turn 350, CP + VP mods), 350 -> 360 (`huge360`), with epic 2000's THROWAWAY x64 DLL carrying the two crash fixes (`6a44abb7...`) on both sides | **identical** (all tables, 74,558 `BuildingYields` rows); host with `--mod` CP 151 and VP 17 from a scratch copy of MODS, `--no-addins` |
| Write path: the host's final save of a proxied 10-turn game (`save10`, 248,013 B) | the direct server loads it natively (pregame and body used up exactly) and plays 10 -> 12 |
| Server killed at turn 20 (`kill20`, TerminateProcess during `Update`) | the proxy's call returns `PeerDied`, it logs `FATAL ... exit code 3` and ends the host **27 ms** after the kill, exit code 3; no hang |

**Calls** (server counters, both directions, `calls_report.py`):

| Game | Calls per turn (mean) | DLL -> engine | engine -> DLL | of which Lua thunks | Boot + map generation |
|---|---|---|---|---|---|
| Tiny, turns 1-49 | 12,984 | 12,151 | 833 | 728 | 1.54 M (1.52 M Lua thunk calls of the map script) |
| Huge, turns 351-359 | 365,285 | 362,496 | 2,788 | 2,552 | 0.28 M |

Huge, per turn, top methods: `GetScriptSystem` 115 k, `ICvEngineScriptSystemArgs1::Push(int)` 96 k,
`DestroyArgs`/`CreateArgs` 36-37 k each, `CallTestAll` 34 k, `setDirty` 21 k, `getSelectionPlot` 5 k,
`IsUnitSelected` 5 k, `isCitySelected` 4 k, `GameplayFOWChanged` 3.5 k, `CallHook` 2.3 k. GameEvents in the engine VM
(D21 item 2) costs about 6 calls per hook (`GetScriptSystem`, `CreateArgs`, 2-3 `Push`, the call, `DestroyArgs`):
300 k of the 365 k calls per huge turn, against the 30-38 k hook calls per turn that section 9 (MC-2) counted.

**Per-call cost** (2026-11-10, after the NDEBUG fix; corrected 2026-11-10 by 9008, which replaced "0.45-0.5 us
transit" as a general figure): transit = the server's round trip per call minus the proxy's handler time per call
(both runtimes' counters, whole run; `proxy/tools/calls_report.py` prints it per method). It depends on the call type
[L, `t50c` and `huge360`]: **0.45-0.50 us** for calls with scalar arguments and no object (`Push(int)` 1.00-1.05 us
round trip vs 0.53-0.57 us handler, `CreateArgs`, `DestroyArgs`, `GameplayFOWChanged`, `IsUnitSelected`);
**0.57-0.63 us** `CallTestAll` (a string argument); **0.60-0.75 us** `GetScriptSystem` (returns an engine object: a
handle the server resolves to its cached remote object); **0.74-0.86 us** `setDirty`; **0.8-1.1 us** calls passing a
DLL object (`GameplayUnitShouldDimFlag`, `SetSpecificCityInfoDirty`: a temporary registration and a temporary
wrapper); **1.4-1.9 us** `getSelectionPlot` and `isCitySelected`. A Lua thunk call 1.25 us (Tiny) to 2.8 us (huge,
larger arguments) against 0.69-2.2 us in the server. The transport alone is the microbenchmark's 0.25-0.44 us; the
rest is the runtimes' staging, counters, payload copies and handle work. Before the fix: 6-7 us per call, and a spin
budget of 2000 us on both sides changed nothing (so it was not event wakes).

**Seconds per turn** (corrected 2026-11-10 by 9008: two figures were quoted without a definition). Two metrics existed:
(a) `calls_report.py` (the figures first quoted here): the mean of the host's per-turn seconds (`host-report.json`
`turns.secondsPerTurn`) and of the direct server's `turn-times.tsv` over turns 1-49 (Tiny) or, given by hand,
351-359 (huge): **Tiny 0.240 vs 0.212, huge 9.09 vs 8.27**; (b) the 2300 harness's `summary.json`: the mean over ALL
turns > 0 of each clock, which on the host side included the target turn's idle tail (Tiny turn 50: 0.012 s; huge
turn 360: 0.09 s) and for a load the first loaded turn (huge 350: 10.7 s), while the direct side excluded its idle row:
**Tiny 0.2355, huge 8.42 vs 8.45**. (b) mixes unlike turns, so it is not a comparison; (a) is. Since 9008 both tools
use one definition: the fully played turns strictly between the start turn and the target turn (new game 1..N-1, a save
of turn S: S+1..N-1), mean and median, host clock vs server clock. On (a): Tiny ratio 1.13; huge ratio 1.10 (+0.8 s
per turn, of which ~0.2-0.25 s is transit at the per-type figures above x 365 k calls; the rest is not attributed: the
host's own per-call work, the runtimes' counters, machine load). Map generation 4.15 s through the proxy vs 1.45 s
direct (1.52 M Lua thunk calls). Against section 7's projection (0.27-0.30 s of IPC per huge AI turn with hybrid Lua,
0.53-0.63 s with all Lua proxied): the measured transit is in that range, but the call mix is different: the
projection counted 0.5-0.6 M UI-Lua calls per AI turn (the real EXE's UI, not present in the host: here 2.5 k) and
took GameEvents and `GetScriptSystem` to be server-local, which D21 reversed; GameEvents alone is 0.3 M calls.

**Unmarshallable treatments that exist** (generated codecs or hand-written hooks, exercised by these runs unless
marked): FDataStream reads and writes (sessions, one transfer per load), IDInfo, caller buffers, cursors,
`std::vector<bool>`, CvString by value and interned references, engine and DLL object handles with temporary
wrappers, `SetGameDatabase` (server copy), `SetDLLIFace(NULL)` shutdown, `RegisterScriptLibraries` and the generic
Lua codec (thunks, instances, `kLSRef` closures and userdata, `kLERef` engine functions, `Fractal` userdata through
the map script), `CreateLuaThread`/`LoadFile`/`FreeLuaThread` with scratch threads, `CallFunction`/`CallCFunction`
local on the scratch thread, `Push/Get*Instance` and `PushReplayFromStream` (not exercised: the host never calls
them), `ICvEngineScriptSystemArgs1::GetString` (not exercised). **Loud stubs** (logged, counted, listed in the
reports, `strict` / `fail_on_unexpected` exit 4): `GetSyncArchive` (4), `send*SyncCheck` (4), node walkers
`HeadUnitNode`/`NextUnitNode`, `head/nextSelectedCitiesNode`, static tables `GetPlotDirectionX/Y`, `GetCityPlotX/Y`,
`GetHexDebugLayerNames`, `TEMPGetRawCombatInfo`, `GetDamageMember`, `loadFromIni`, `SetDLCAvailable` and
`GetSelectedUnits` (engine-implemented enumerators), `AddPopup` (2), `TradeVisuals_NewRoute`, `GameplayUnitMoved`,
`EnumerateFiles`, `GetModsDatabase`, `Debug_GetHeap`, the six WorldBuilder Lua methods. Hit in the proofs:
`AddPopup(const CvPopupInfo&)` once (Tiny), `TradeVisuals_NewRoute` 88 times (huge); neither changes game state.
Since 9008 (13.2): `ICvUserInterface1::AddPopup(const CvPopupInfo&)` is forwarded (the struct by value, field by
field) and reaches the engine; `TradeVisuals_NewRoute` is on the remote allow-list; the other `AddPopup` overload
(`int, int, const int*, int, const bool*`) is still a loud stub.

**Known gaps found** [L]: (1) the DLL's VPUI Lua framework (`LuaSupport::InitLuaFramework`, `CvLuaSupport.cpp`:
`luaL_dostring` on the "VP_LUAAPI" thread) runs on the server's scratch thread, not in the engine VM, and fails there;
in the real EXE VP's UI API would be missing. (Corrected 2026-11-10 by 9008: the "one `kRtE_LuaGetGlobal` for an unknown
engine thread in every run" was not this gap but a server bug, a corrupted thread id, 13.3. With it fixed, the chunk
gets one step further and stops at `G.Threads`, nil on the server, after the engine's `coroutine.running()` returned
an engine thread that cannot cross: one `kLUnsupported` of type thread per boot, on the remote allow-list.)
(2) `GetScriptSystem()` crosses 115 k times per huge turn for an object that never changes: the first local answer
to add. (3) Every engine Lua function the proxy encodes becomes an engine registry reference that is never released
(the map script's three functions in these runs).

**Open items for running under the real EXE** (named exactly; status 2026-11-10 after epic 2500: (a), (b), (c), (d),
(f) closed and (e) closed offline, sections 14-15; (g) observed only without a Steam launch; known gap 1 below closed
by issue 2504): (a) thread binding by role: the core thread must be
bound to the `core` channel by its first `ICvGame1::Update`, render workers to `aux` (today: first non-boot thread
= core); (b) the database: how the proxy learns the engine's database file and whether it is a file when
`SetGameDatabase` is called (today `vpproxy.ini db=`); (c) the VPUI framework of gap 1; (d) the msvcr90 / msvcp90
side-by-side activation of the proxy inside the EXE (embedded manifest; tested only in the host, which already has
the CRT loaded); (e) the human-game paths the host never calls: `Push*/Get*Instance`, node walkers, selection
enumerators, popups, `sendTurnComplete` family; (f) the server start: the EXE's working directory, the server's
console window (CREATE_NO_WINDOW) and the patch's install layout for `server_exe`; (g) Steam overlay or CEG timing of
a process started from inside the EXE: not tested.

**Not verified**: x86 vftable slots of the EXE's VC9 objects (the host's objects are clang-built: the host proves
the proxy's slot order against itself, not against the EXE); the `core` and `aux` channels (the host is single
threaded); `on_server_death=continue`; seconds per turn on a quiet machine (every run had ~50% background load).
(Proxied A/A, listed here until 2026-11-10: verified identical by the MC's post-merge verification and again in 9008,
13.4.)

## 13. The proof harness made strict (issue 9008, 2026-11-10 12:48-13:43 EST)

### 13.1 What the 2300 harness could not see [L, reproduced on main 3fc0943; `server/evidence/9008/before/`]

1. `run_proxy_game.py --fail-on-unexpected` passed the direct mode's `--fail-on-unserved` to `run_server.py
   --remote-config`, which ignored it: every proxied server ran with `fail_on_unexpected=0`, and a Tiny 0-50 run with
   an unserved `AddPopup` reported PASS, exit 0.
2. The proxied verdict read only the host (exit code, `host-report.json` status, orphans): not the server's exit
   code, its `result:` line, its Lua errors, unserved methods or failure counters, nor the proxy's totals.
3. `compare_stats.py` called two databases without tables (or without rows) IDENTICAL, exit 0; a table on one side
   only did not count in the total rows.
4. `run_proxy_game.py --run`, `build_proxy.py --src` and `gen_runtime.py --src/--objdir` defaulted into epic 2300's
   scratch folder.
5. vpipc reports the end of the server process as `Died` whatever its exit code; the harness listed the clean
   shutdown's lines as failure lines on passing runs (and the boot line echoing `on_server_death=` too).
6. `build_server.py` ended OK, exit 0, when `proxy/build_proxy.py` was missing.
7. A served call that failed in the proxy (`ProxyRt::DispatchFail`) was logged but not counted: "failed calls 0".

### 13.2 What changed (code)

Harness and tools: strictness per mode (`--fail-on-unserved` direct, `--fail-on-unexpected --remote-allow` remote;
`run_server.py` refuses the other mode's flag); the proxied verdict (`proxy_verdict`) requires everything in the
harness docstring (both sides' exit codes, reports, counters, the allow-list) and lists each reason for a FAIL;
`compare_stats.py` exits 2 on no tables / no rows and counts one-sided tables; no scratch defaults; `build_server.py`
fails without `build_proxy.py`; one definition of seconds per turn (12, corrected); `calls_report.py` prints transit
per method; the offline negative checks `proxy/tools/test_strictness.py` (43 checks, 3 s).

Remote allow-list `proxy/tools/remote-allowlist.txt` (one key and a written reason per line; printed in every run; the
server honours it under `fail_on_unexpected=1` through `unexpected_allow=`): `unsupported-lua:thread` (the VPUI gap,
13.3) and `ICvEngineUtility3::TradeVisuals_NewRoute` (renderer only; huge save; the real EXE needs it forwarded).

Runtime changes (small, for the real-EXE epic to know): proxy `OnCall` counts a failed served call per method
(`failed calls N (outbound X, served Y)` and one `failed calls of <method>: in|out N` line per method at shutdown);
unsupported Lua values counted per type (`unsupported Lua values of type T: N`); the server's exit after
`kRt_Shutdown` is logged as "the server process ended after kRt_Shutdown", not `Died`; the `LuaGetGlobal` failure
names the thread id and global. `ICvUserInterface1::AddPopup(const CvPopupInfo&)` is forwarded: the server's
`RemoteUserInterface::AddPopup` sends iData1-3, iFlags, bOption1-2, eButtonPopupType and szText (at most 511 bytes)
and the proxy's `EngHook_ICvUserInterface2_AddPopup_s45` rebuilds the struct and calls the engine [L: 1 call per
Tiny 0-50, served by the host]. The server reads `unexpected_allow` and binds its delay-loaded imports up front
(13.3).

### 13.3 The `LuaGetGlobal` failure was a server bug, not the VPUI gap [L]

The scratch thread the server makes for the DLL's `CreateLuaThread` reads the engine thread's globals through an
`__index` closure whose upvalue is the engine thread id, pushed with `lua_pushnumber`. That was the server's first
`lua_pushnumber` call, which went through LLD's lazy delay-load thunk; the thunk loses xmm1 (server-host.md section
13), so the upvalue held the bits of a pointer, `(uint32_t)` of a denormal = 0, and the proxy answered "unknown engine
Lua thread id 0". Diagnosed with temporary log lines (`server/evidence/9008/before/luagetglobal-diagnosis.txt`), fixed
by binding the imports at startup. The VPUI gap (12, gap 1) remains: the InitLuaFramework chunk now gets one step
further, its `coroutine.running()` (an engine function) returns an engine thread that cannot cross (one `kLUnsupported`
of type thread per boot, allow-listed), and it stops at `G.Threads` (nil on the server). The direct server has the same
latent delay-load bug (server-host.md 13, open for the owner of `srv_main.cpp`).

### 13.4 Strict re-runs (build b1 = the 9008 code on the pinned DLL; `server/evidence/9008/`)

Tiny 0-50 proxied = direct (15 tables, 9,257 rows) with the allow-list printed and every check of 13.2 passing; the
loaded Tiny turn-50 save 50-60 identical (14 tables, 3,030 rows); the kill test (host exit 3, 0.06 s after the kill);
a proxied A/A pair identical; a proxied game's save loaded and played 10 -> 12 by the direct server. Machine heavily
loaded by epic 2400's builds: no timing from these runs is a measurement. Not re-run: the huge save (waits for pinned
DLL builds from epic 2400; its mods come from the fork checkout, `server/evidence/2300/README.md`).

Also found: rows of the target turn are not comparable between runs of different length (a direct 10-turn run and a
50-turn run differ only in turn 10; an 11-turn run equals the 50-turn run on turns 0-10) [L]; see
`server/evidence/9008/README.md`, finding 8.

## 14. Epic 2500: the proxy made ready for the real EXE (2026-11-10, offline work of issues 2501-2505)

Team-2500 coordinator, written 2026-11-10. Detail, numbers and negative tests of each item are in the issue files
`tracker/issues/2501-*.md` to `2505-*.md` (section Findings); what follows is the design as it now stands. The live
results under the real EXE are section 15. Items (a)-(j) are the open items of section 12 and of one-off 9008.

- **Thread binding by role (item a; issue 2502)** [C, L]. Replaces 11.2's "first other thread = core". The thread
  that calls `DllGetGameContext` is `ui`; the first other thread whose FIRST call is `ICvGameContext1::GetGame()` or
  `ICvGame1::Update()` is `core`; every other thread is `aux`. Decided at a thread's first call, never changed. Each
  new thread is logged once with its Win32 start address; `Update` off the core channel, or a top-level `ui` call
  from another thread, is logged `BINDING:` and is fatal (exit 5) under `strict=1`. Guarantee: one EXE thread maps to
  one server thread, so a `CvDllGame`/`CvDllPlayer` wrapper's lock acquire, the `Update` and the matching `Destroy`
  run on one server thread, and the core thread never shares `aux`. ini key `host=exe|headless` (the headless host
  calls `Update` on its boot thread; the proof harness writes `headless`). Deadlock analysis: issue 2502 Finding 2.
  Risk not covered [I]: two locks exist where the game had one (the DLL's game-core lock is server-local).
- **Server start and the D28 layout (item f; 2502)** [L]. `MODS/(1) Community Patch/` holds the proxy as
  `CvGameCore_Expansion2.dll`, `CvGameCore_Expansion2_x86.dll` (the original 32-bit DLL, off switch),
  `vpproxy.ini`, and `VP64/` with `server.exe`, the x64 DLL, `lua51_x64.dll`, `CvGameDatabase_x64.dll`,
  `CvLocalization_x64.dll`. Relative paths resolve against the proxy's folder (GetModuleFileName), never the EXE's
  working directory. Each boot gets `VP64/run/<yyyymmdd-hhmmss>-<pid>-<n>/` (vpproxy.log, the generated server.cfg,
  the server's output, `db/`), pruned to `keep_runs` (10). **Corrected 2026-11-13 (issue 0148):** when the default run
  root lies under a folder named `MODS` (the shipped layout), it is `<user>\Logs\VP64\run\` instead, and the off switch's
  `vpproxy.log` goes to `<user>\Logs\VP64\`: the EXE watches `<user>\MODS` as a whole tree and stalls ~150 ms on every
  change under it (`research/05-graphics-composition/renderer-in-server-design.md` 4.14). A `run_root=` in vpproxy.ini is
  kept as written. (Was: always `VP64/run/` beside the proxy.) The server runs in `VP64/` with `CREATE_NO_WINDOW` and no
  inherited handles. Every ini key with its default: `proxy/README.md`. A missing ini means `mode=off`.
- **The x64 DLL's file name and the user folder in both VP forms (issue 2606, MC #3403 and #3454, 2026-11-17)** [C code
  and unit tests; L hermetic, `research/06-tooling-and-workflows/installer-notes.md` 15]. Why: in the modpack form (VP
  inside `<game>\Assets\DLC\VP_MODPACK\Mods\(1) Community Patch\`) the EXE's DLC scan loaded `VP64\CvGameCore_Expansion2.dll`
  into the 32-bit process (error 193, then 0xC0000005, installer-notes 14.9), and the 0148 rule took the DLC's own
  `Mods` folder for the user's, so the run folders and the off switch's log went to `<game>\Assets\DLC\VP_MODPACK\Logs\
  VP64\`. The rules (`proxy/src/proxy_rt_logic.h`, `X64DllName` and `UserLogsDir`):
  - **x64 DLL** (server.cfg's `dll=`, the error report's hash list, the pack=0 purity binding): with `pack=1` the checked
    manifest's `x64_dll.file` (the proxy hands `ProxyPack`'s checked path to server.cfg, so the bytes hashed are the
    bytes loaded); otherwise `VP64\CvGameCore_Expansion2_x64.dll` when that file exists, else the old
    `VP64\CvGameCore_Expansion2.dll`. Installer packs (`tools/release/pack.py` since 2606) carry the `_x64` name; every
    development and test layout with the old name works as before. The other x64 files keep their names (`_x64` already).
  - **Run root and off-switch log, MC #3454:** with no `run_root=` in vpproxy.ini, a proxy path under `Assets\DLC` (the
    modpack form) uses `<folder of the game's config.ini>\Logs\VP64\run` (and `...\Logs\VP64\vpproxy.log` for mode=off),
    the folder the error report already used (`ProxySession::ConfigIniPath`: VPPROXY_TEST_CONFIG_INI, C5_DOCUMENTS, else
    SHGetFolderPathW(CSIDL_PERSONAL)); checked before the MODS rule. A proxy path under a folder named `MODS` (the mods
    form) keeps 0148's `<MODS's parent>\Logs\VP64\run`, which in an installed game is the same folder as config.ini's;
    it stays so that a test layout under a temporary `<user>\MODS` never writes into the real player's My Games. Any
    other path: `<x64 dir>\run` as before. In the modpack form without a config.ini path the run root stays `<x64 dir>\run`.
  - The fork DLL at the pin 04b1211b8 looks up its own file name nowhere at load or run time (2606's check, the same
    result as the line below): only the crash dialog's wording compares the crashing module with
    `CvGameCore_Expansion2.dll`, so a crash inside the renamed x64 DLL is worded "unknown reason" there [C].
  - **`proxy/tools/test_proxy_session.py` red after 2606 (ticket 0510, 2026-11-17)** [C test code; the 21 failing checks are the ones 0487's run of 2026-11-17 listed, 126/147]. Cause: six loader
    cases damaged the pack's x64 DLL with a literal `open(VP64\CvGameCore_Expansion2.dll, 'ab')`. After 2606 the pack has
    no file of that name, so the call created a new 1-byte file beside the real DLL. The manifest's DLL was unchanged, the
    pack check passed, and pack-hash, pack-hash-d33, fluidfow-only (6 checks each), failed0, no-helper and blocked
    (1 each) failed: 21 of 147. failed1 passed by luck, because `failed=1` skips the pack check. The proxy was right. Fix
    (test only): the cases change the file the manifest names in `x64_dll.file` (the name the proxy uses), and a missing
    file stops the suite instead of creating one. Two new cases cover a pre-2606 pack (the DLL under the build's name, the
    manifest naming it): good-oldname boots (negative), and pack-hash-oldname reports `hash-mismatch`. Trap for any test
    that damages a file: `open(..., 'ab')` creates a missing file, so check that the file exists first. Result
    2026-11-17 [L hermetic]: `test_proxy_session.py --asan` **154/154** (96 s; 147 + 7 new) on a release build of the
    pin 4f4daac70 with this branch's server and proxy (`evidence/0510/acceptance.txt`).
- **Off switch (item j; 2502)** [L]. `mode=off`: `DllGetGameContext` loads `fallback_dll` by full path and forwards
  every call to its export; no server, no run folder; the proxy writes only `vpproxy.log` beside itself (since 0148: in
  `<user>\Logs\VP64\` when the proxy lies under MODS). The fork has
  no lookup that depends on the DLL's file name except the crash handler's message (`CvGlobals.cpp:3008`, cosmetic).
- **Strict allow-list (item i; 2502)**: `strict_allow=<file>`, the key forms of `proxy/tools/remote-allowlist.txt`;
  the default list `proxy/tools/proxy-strict-allowlist.txt` is empty on purpose.
- **Frames and snapshots (2502)**: a frame is the first `ICvPlayer2::HasTurnTimerExpired()` on `ui` after
  `ICvGame1::GetActivePlayer()` with no `ICvPlayer1::IsHuman()` in between (the slot poll calls it about 6.4 times
  per idle frame, so plain counting overcounts). The event `Local\VPProxySnapshot_<pid>` (tool
  `proxy/tools/proxy_snapshot.py`) appends a block to `vpproxy-snapshots.tsv`: frames, fps, frame time
  mean/p50/p99/max and calls per interface x direction x channel since the previous snapshot; a final block at an
  orderly shutdown.
- **The harness Lua channel is served by the proxy (2505)** [L]. Coordinator's decision (minor): the DLL's own
  channel would run in the server's scratch Lua state, which has no UI states. The proxy ports
  `LuaSupport::PollExternalLuaRequest` (fork `instance-aware-instruments` @ 440878983, GPL, attributed) and runs it
  against the engine VM at the top of every proxied `ICvGame1::Update`, with the same event names
  (`Local\VPLuaExec[Done]_<VP_INSTANCE>`), files, headers and result format, so `tools/harness/lua.py` works
  unchanged. Its persistent engine Lua thread `VP_LUAEXEC` is created once in the hook of `CacheGameDatabaseData` on
  the main thread (where the DLL creates its `VP_MEMPROBE` thread). Around a chunk, the server thread serving the
  core channel yields the DLL's game-core lock (`kRt_CoreLockYield` / `kRt_CoreLockResume`, added by the
  coordinator), as the DLL's channel releases it around `CallCFunction`. `turn=` comes from `Game.GetGameTurn()` run
  after the chunk. The x64 DLL needs no instrument branch for the harness: it stays at the pin.
- **The database (item b; 2504)** [L]. The proxy imports `CvGameDatabaseWin32Final Release.dll` (the fork's import
  library) and asks the engine's connection `PRAGMA database_list` and `sqlite_version()`. Order of use: `db=` in
  the ini; else the `main` file; else (in memory, as under the real EXE) a `Database::BinaryIO::Save` snapshot. A
  fingerprint (schema hash, per-table row count and row hash, `proxy/include/vp_dbfp.h`) is compared on both sides.
  Every write the DLL makes to its game database is recorded by import-slot hooks on the server's own x64 DLL image
  (22 of 45 `CvGameDatabase_x64.dll` imports; every other import must be on a read-only list or the boot fails) and
  replayed on the engine's connection (`kRtE_DbOps`; batched up to 512 per call inside the three
  `ICvDLLDatabaseUtility1` calls since the follow-up), with the result compared per write; after each of those calls
  the fingerprints are compared. Since the follow-up after live run P1 (section 15): tables that differ because the
  ENGINE wrote them are pulled from an engine snapshot into the server's copy (`kRtE_DbSnapshot`, up to 3 rounds); a
  differing table the DLL wrote, a schema difference, or no identity after the pull stays a loud failure.
- **VPUI in the engine VM (item c; 2504)** [L]. The server points the DLL's `luaL_loadstring` import at a hook: on a
  thread the DLL got from `CreateLuaThread` it pushes a stub whose call sends `kRtE_LuaRunChunk`; the proxy runs the
  same text in the engine thread with the thread's globals. `unsupported-lua:thread` left the remote allow-list.
- **Human-game paths (item e; 2503)** [L offline pair test `proxy/tools/hutest/`]. Node walkers (`HeadUnitNode`
  forwarded with the whole list, `NextUnitNode` answered locally), static tables cached once, `GetDamageMember`,
  `SetDLCAvailable`, `loadFromIni` with a remote parser, `GetSelectedUnits`, `headSelectedCitiesNode`, both
  `AddPopup` overloads and `TradeVisuals_NewRoute` (in-array codec), `GameplayUnitMoved` (FFastVector layout over the
  proxy's own buffer); local answers with reasons for `Debug_GetHeap` and `TEMPGetRawCombatInfo`. A generator bug was
  fixed: engine calls that return DLL objects (`GetHeadSelectedUnit`, `getSelectionPlot`, `getHeadSelectedCity`,
  `getLastSelectedUnit`, `GetScratchDeal`) gave the DLL a reference it then Destroyed under a live wrapper; the server
  now takes its own reference. Still loud: the six WorldBuilder Lua methods (a `.Civ5Map` map), multiplayer sync, and
  methods with no call site (issue 2503 F1). Open: temporary wrappers of DLL objects passed to engine calls are freed
  after the call; if the EXE keeps such a pointer (`selectUnit`, `InsertIntoSelectionList`, ...) it holds a freed
  wrapper [I]; not seen in the live runs of section 15. *Corrected 2026-11-11 (epic 2800, issue 2804): it does not; the
  selection stores IDInfo values and every handler seen live borrows its argument for the call only [C], section 17.4.*
  *Correction 2026-11-10 (issue 2704):* the pair test as committed did not run on main: the proxy imports
  `CvGameDatabaseWin32Final Release.dll` since 2504 and hutest did not supply it (LoadLibrary error 126, host exit
  101); with it supplied the run ended at the first `ICvGame1::Update` (thread binding, exit 5) because the generated
  `vpproxy.ini` lacked `host=headless` (the fake host is one thread); its proxy check also counted the binding line's
  "unimplemented calls allow-listed 0" as a failure; and `--break` only printed unexpected failures. Fixed in
  `build_hutest.py` [L offline 2026-11-10, build of 5414f5e + the 2704 changes, DLL pin 29f15ac38, release]: 37/37
  checks, server `result: passed`, 0 unserved, proxy totals `unimplemented calls 0`; `--break` 15 expected failures and
  no other, exit 0; `--break --selftest-unexpected dll_dlc` exit 1. Outputs: `server/evidence/2700/hutest/`.
- **Server window guard and two-process stop safety (2501)** [L, stand-ins]. With `CIVBG_GUARD=1` (inherited from
  the EXE) `server.exe` installs, before any DLL loads, an x64 port of the 1118 guard (win32u stub hooks, CBT
  activation veto, 100 ms watchdog, no ghosting) and publishes `Local\CivBgGuard_<pid>`; a failure exits
  0xC5F00030. The harness's status, watch, dialog state, turn driver and stop cover every descendant of the EXE;
  `stop` ends only the EXE, waits for each recorded child (60 s) and never terminates one; a child left alive is an
  orphan (exit 7); `cli.py leftovers` lists anything still running from the instance. Item (h): the direct server
  binds its delay-loaded imports up front too (Tiny 0-50 before/after identical).

## 15. The real EXE through the proxy (epic 2500, live on Goldberg instance c5h-a, 2026-11-10 14:43-15:46 EST) [L]

Setup: instance `c5h-a` (copy of pure_vp, source-built Goldberg emulator, CP 151 + VP 17 + EUI), harness guard route
`early` (see "Incidents"), proxy and server of this branch, release, built from the pin (`deps/dll.lock` e9d0c4837)
with `server/build_server.py --deps`, installed by `tools/instances/provision.py set-proxy` (D28 layout, section 14).
The off-switch fallback and every 32-bit baseline is the instance's own DLL `5fdf3432...`
(instance-aware-instruments 440878983, the harness's usual 32-bit DLL) loaded through the proxy's `mode=off`. Both
DLLs are 5.4.8 without the `AttackKey` fix (D25), so they are the same game logic. Machine: i9-13900KS shared with other
teams' builds and up to seven headless games during every run: no timing here is a quiet-machine number. Evidence:
`server/evidence/2500/` (README there lists every file).

### 15.1 Stages

| Stage | Result | Evidence |
|---|---|---|
| 1 Boot | **Pass** (second try). The EXE loaded the proxy from the CP slot once, after the mods were activated; the proxy started `server.exe` from `VP64/` beside itself (D28; a folder under `Documents\My Games` works); the server had no console window (`GetConsoleWindow()` 0), its guard was installed (`CIVBG_GUARD=1` inherited), parent = the EXE; the database snapshot was identical on both sides; the VPUI chunk ran in the engine VM; the main menu was reached (screenshot); EXIT + the game's own confirm popup ended the EXE cleanly: `SetDLLIFace(NULL)`, `kRt_Shutdown`, server exit code 0 59 ms later, `result: passed`. First try: the EXE crashed 1.6 s after boot (the context was Destroyed, 15.2) | `run1-boot/`, `run2-boot/`, `run2-menu.png` |
| 2 A played game | **Pass.** A Tiny Continents game (3 AIs, 4 city-states) made with the 32-bit DLL (B1, saved at turn 8) loaded through the proxy (P1) and through the off switch (B2); in each the harness checklist ran: 10 AI turns, city screen, tech tree, diplomacy overview, an AI leader screen opened and closed, a save, unit selection, three map picks: **all pass in both**. Lua contexts: 144 in both; the VPUI framework present in the engine VM in both (`Threads` metatable with `__newindex`); 15 notifications in both; Lua.log errors: only `VPUI_loader.lua` file errors, present in the baseline too (2 or 3 per run, instance content); no dialog, crash or hang in the final runs. Game state at turn 18 identical on the probe (score 42, gold 78, 1 city, 2 units, 2 techs) | `P1*`, `B1*`, `B2*`, `lua-logs/` |
| 3 The huge save | **Pass for 350 -> 353**, stopped there on purpose (the pinned x64 DLL lacks the turn-355 fix, tracker 0080). `C5H-VP8P-HUGE_0350` (the instance's copy) loaded through the proxy in 2 min 45 s from launch (32-bit: 2 min 18 s), 3 AI turns played with the leader screens answered by the harness's diplomacy handler, checklist steps pass in both. Probe at turn 353: identical except gold 14,263 (proxy, x64 DLL) against 14,262 (32-bit) | `P3*`, `B3*` |

### 15.2 What the real EXE did that the headless host never did (each fixed in this epic)

1. **The EXE Destroys the game context** after its first calls. The proxy forwarded `Destroy`, the server dropped
   the context handle, every later call failed "bad handle" and the EXE crashed (0xC0000005) 1.6 s after boot. The
   context wrapper is now pinned at boot (`ProxyRt::IsPinned`; generated `Destroy` returns early for it). [L]
2. **The engine's database is in memory** (`main=''`) and the EXE writes to it itself: the map-script tables grow
   when the load screen starts. 2504's snapshot path handled the first; the second made the identity check fail
   (`MapScripts` 33/57 rows and two more tables) until the 2504 follow-up pulled engine-written tables into the
   server's copy (`database: pull after CacheGameDatabaseData round 1: 3 tables ... IDENTICAL (0.29-0.41 s)`).
3. **The save body comes through a stream without a size** (`GetSizeLeft()` 0x7FFFFFFF). Reading "the rest" failed
   at once (a 2 GB reserve); reading in chunks until a short read never ended (`ReadIt` never comes up short), grew to
   the 512 MB cap and the EXE aborted with a CRT "Runtime Error!" box (cloaked; answered by the coordinator). Now such a
   stream is a **pull session**: the server's stream fetches exactly the bytes the DLL reads (`kRt_StreamOpenPull`,
   `kRtE_StreamRead`), so the engine stream ends where the in-process DLL would leave it.
4. **The replay of the DLL's database writes cost 12-100 s per boot** at one call per write (2,559 writes in
   `PerformDatabasePostProcessing`; 40 ms each when the machine was busy); batched (up to 512 per call) by the 2504
   follow-up; the boot's database phase now takes seconds.

### 15.3 Threads (item a) [L]

The boot thread (`CivilizationV_DX11.exe+0x527035`) is `ui`. The game-core thread (`+0x42A360`) first calls
`ICvGameContext1::GetGame()` and got `core` by the rule of section 14. On the huge save 3-4 render workers
(`MSVCR90.dll+0x2345E`, `_beginthreadex`) first call `ICvGameContext1::GetMap()` and got `aux` (386 calls in the
101 s AI-turn window). No `BINDING:` line in any run: every `Update` came on `core`, no other thread called on `ui`.
The Tiny game had no worker calls.

### 15.4 First measured traffic under the real EXE, beside the projection

Counters: the proxy's snapshots (section 14). A frame = the slot poll's marker; the same frames counted from Lua
(`Events.LocalMachineAppUpdate`) agree within 10%. Calls are all calls the proxy made or served, both directions.

| Window (proxy) | Wall | Frames, fps | Frame time mean / p50 / p99 | Calls | Per frame | Per AI turn |
|---|---|---|---|---|---|---|
| Tiny t18 idle human turn (P1) | 60.2 s | 20,359, 338 | 2.96 / 2.93 / 4.38 ms | 11.99 M (ui 11.97 M, core 15 k) | 589 | - |
| Huge t350 idle human turn (P3) | 60.3 s | 10,497, 174 | 5.74 / 5.68 / 9.34 ms | 6.64 M (ui 6.63 M, core 15 k) | 633 | - |
| Huge 350 -> 353, three AI turns incl. leader screens (P3) | 101.5 s | 5,310, 52 | 19.1 / 6.9 / 128 ms | 8.99 M: ui out 7.46 M, ui in 0.14 M, core in 1.03 M, core out 0.38 M, aux 386 | 1,694 | **about 3.0 M** (core 0.47 M) |

Per frame, idle (huge): `ICvGame2` 171, `ICvGameContext3` 149, `ICvPlayer3` 144, `ICvPreGame2` 120, `ICvPlot1` 28,
`ICvTerrainInfo1` 8.6, `ICvMap1` 4.4, `ICvUnit2` 2.8, `ICvGameAsynch1` 2.1 (the slot poll's GetGame / GetPlayer /
QueryInterface / Destroy and pregame reads: what section 5's mirror would answer locally). Per AI turn (huge): UI
Lua thunks `rt` 0.81 M on `ui`, `ICvGame2`/`ICvGameContext3`/`ICvPlayer3` 0.39/0.34/0.34 M on `ui` (the slot poll
keeps running at 52 fps), `ICvPlot1` 0.21 M, on `core` `ICvEngineUtility4` 0.12 M, `ICvEngineScriptSystem1` 0.10 M,
`ICvEngineScriptSystemArgs1` 0.09 M, `ICvUnit2` 0.04 M. Against section 7's projection and D21: the projection put
23.5 us of IPC and 19 synchronous calls in an idle frame, because it took lifetime calls and static handles
(`GetGame`, `GetPlayer`, `QueryInterface`, `Destroy`, pregame reads) to be answered by the proxy locally, and 0.42-0.47
M UI-Lua calls per AI turn; at that time (epic 2500; *since epic 2700 the local answers of section 16 exist: 31 crossings per idle Tiny frame, 58 huge*) nothing is answered locally except a wrapper's own `QueryInterface`, so an idle frame
makes 589-633 calls (about 0.3-0.6 ms per frame at the 0.45-1.0 us per call of section 12), and the huge AI turn
made 0.81 M UI-Lua thunk calls. The core channel's 0.47 M calls per AI turn match section 12's
headless 0.37 M (GameEvents in the engine VM, D21 item 2).

### 15.5 Memory, seconds per turn, frame rate: proxy against the 32-bit DLL (same instance, save and steps)

| | Proxy (EXE + server) | 32-bit DLL (off switch) |
|---|---|---|
| EXE private bytes, huge t350 idle after load | 1,812 MB | 2,078 MB |
| EXE private bytes, huge after 3 AI turns | 2,030 MB | 2,184 MB |
| EXE peak private bytes over the run (5 s samples) | 2,061 MB | 2,293 MB |
| EXE largest free address block, minimum over the run | 1,194 MB | 886 MB |
| Server private bytes, huge idle / after 3 turns / peak working set | 235 / 280 / 319 MB | - |
| Seconds per AI turn, huge 350->351, 351->352, 352->353 (leader screens answered 2 s after the message) | 33.4, 28.9, 26.7 (mean 29.7) | 37.2, 33.0, 30.9 (mean 33.7) |
| Frame rate, huge idle (Lua frame counter) | 171 fps | 183 fps |
| Frame rate, Tiny idle (Lua frame counter) | 311 fps | 395 fps |
| EXE private bytes, Tiny t18 idle | 1,676 MB (+158 MB server) | 1,800 MB |
| Launch to Lua channel answering, huge | 2 min 45 s | 2 min 18 s |

Correction 2026-11-10 16:58 EST (MC, from the independent verification, `server/evidence/2500/verification/`): three rows above have no saved output behind
them and are observations only until measured again: "Server private bytes" and "EXE private bytes, Tiny" (taken with a
different tool, output not kept) and both "Frame rate" rows (`fps_read.lua` and its output are not in the evidence). The
huge-save EXE memory rows are backed (same tool on both sides). "Game state matched" was 9 numbers of the idle human player.
The verifier's repeat on c5h-b (x64 pin 29f15ac38 against the instance's older 32-bit DLL) [L]: all 8 players' cities,
population, units, gold, score, techs, culture and faith identical at turn 18; unpacked save bodies differ in 2,316 bytes
in 12 clusters; a second 32-bit run differs from the first in 9 single bytes, so about 2.3 KB is a real difference whose
cause is not traced (the two DLLs are different sources: the D25 fixes are on one side only). The binaries of the epic's
runs cannot be tied to a commit (no build id in any log; builds are not bit-reproducible).
*Addition 2026-11-10 (issue 2704):* since 2704 the first line of `vpproxy.log` and of `server.log` is the build id:
`build: repo <commit> dirty=<0|1> dll_pin <commit> src <mode> config <c> built <time>; image <path> sha256 <hex>`
(`proxy/include/vp_buildid.h`; the commit, dirty flag and pin come from `vp_build_info.h`, generated by
`proxy/gen/build_info.py` in `build_proxy.py` and `server/build_server.py`; the SHA-256 is of the loaded image file,
computed at run time with bcrypt.dll loaded by name because a binary cannot contain its own hash and the proxy's import
list is checked). Both hashes equal `Get-FileHash` of the files [L offline 2026-11-10, hutest run; the line is
stamped 0.007 s in vpproxy.log and 0.008 s in server.log]: `server/evidence/2700/hutest/build-id-first-lines.txt`. Runs from then on can be tied to a
commit when the line says `dirty=0`.

Read with care: one run each, a shared and busy machine, turn times from the harness's 1-3 s polling, and the AI
turn includes leader screens. What the numbers do say: the EXE gives back 230-270 MB of private memory and gains about
300 MB of free address space with the game state in the server; AI turns were not slower through the proxy on this
save; idle frames are 7% (huge) to 21% (Tiny) slower, which fits 0.3-0.6 ms of per-frame call cost on a 2.5-5.5 ms
frame. Expected differences: the gold value at turn 353 (x64 against x86 DLL: the `AttackKey` cache and CRT maths
differ between the builds, x64-dll-port.md section 9.5, D25; the headless pair first differs at turn 354 [I: not
traced here]); the `rawget` global missing in the environment the Lua channel picks for "Main" under the proxy (the
runner chose another state's environment; `G.rawget` is present; not a game difference, not investigated).

### 15.6 Items (b), (d), (g) under the real EXE

- **(b)** answered: the engine's database is in memory; the proxy snapshots it with `BinaryIO::Save`, verifies it by
  fingerprint and keeps it identical (DLL writes replayed, engine writes pulled). `db=` is not needed.
- **(d)** answered [L]: the proxy (msvcr90/msvcp90 SxS manifest, resource id 2) loaded and ran inside the EXE in every
  run with no activation error; the proxy also imports `CvGameDatabaseWin32Final Release.dll` and `lua51_Win32.dll`,
  which the EXE had loaded. Which msvcr90 assembly version the loader bound was not read [not verified].
- **(g)** observed: the server started from inside the EXE ran with the source-built Goldberg emulator and the real
  Steam client answering the EXE's CEG handshake as before; the EXE started and ran normally every time; no Steam
  overlay module is injected on these instances (the EXE is not started by Steam), so overlay injection into a child
  process could not be observed here [not verified under a Steam launch].

### 15.7 Incidents (all on c5h-a; none visible to the owner, no focus taken)

Correction 2026-11-10 16:09 EST (MC) [C, guard logs]: the window guard's activation veto (SetWindowsHookExW WH_CBT with the guard module) failed with error 126 in all 9 real-EXE runs from 14:00 to 15:39 on 2026-11-10 (guard logs in %TEMP%/civbg_guard_<pid>.log, read by the MC at 16:07), on the early route; it was ok in every real-EXE run up to 08:00. Cloaking and the other 17 hooks were installed. Found by one-off 9009 (tracker 9009). The statements below about cloaking and
foreground state stand as observed; the statement that the guard was fully installed does not.

- 14:44 first boot: the EXE crashed (access violation, 15.2 item 1); the server saw its peer die and exited code 3
  11 ms later; no dialog, nothing left running.
- 15:01 P1: the EXE ended itself with `on_server_death=exit` after a failed stream call (15.2 item 3, first form);
  server exit 3; nothing left.
- 15:08-15:13 P1 retry: the CRT "Runtime Error!" box (abort, 15.2 item 3, second form) in the EXE: cloaked from
  creation, never foreground, `seen_by_art` false (status at 15:12); after a settled-save check (newest save 14:56:52,
  nothing written since) the coordinator pressed its only button by `WM_COMMAND` with the button's own id 2
  (`server/evidence/2500/press_ok.py`); the EXE ended, the server exited by itself; `leftovers` clean.
- The harness's `watch` ran in 120 s slices through every run: 0 hits in 25 slices. Every stop: `stop` with the
  human-turn hook; `server.exe` and its `conhost.exe` exited by themselves within 0.3 s; 0 orphans.
- Guard route: every launch used `route="early"`, because worker 2501 found that the import route's guard (the
  instances' default) lets a NULL-owner message box of the EXE take the foreground in the stand-in test
  (`tools/harness/native/tests/test_shim.py` import/dialogs fails; not changed here, reported to the MC).

## 16. Local answers (epic 2700, 2026-11-10 16:00-19:00 EST, team-2700 coordinator)

Epic 2500 ran every call across (section 15.4: 589-633 calls per idle frame against 19 projected). This epic answers
in the proxy (and for one engine object in the server) what does not need a crossing, with the rule: a local answer
is given only where the event that would change it is named and every such event is seen; otherwise the call keeps
crossing. Code: `proxy/src/proxy_local.h` (design comment), `proxy_local.cpp`, `proxy_hooks_local.cpp` (the hooks),
`server/src/remote/srv_remote_objects.cpp` (canonical objects, the lock-want sync, GetScriptSystem),
`proxy/model/pure-methods.tsv` (purity per method, issue 2701), `proxy/tools/gen_pure_list.py` (-> `proxy_pure.inc`).
Off switch: `vpproxy.ini` `local_answers=0` (every call crosses as in 2300-2500; the reference); `local_verify=1`
(every local answer is also asked of the server and compared; a mismatch is logged `VERIFY MISMATCH`, counted, and
fatal with exit 6 under `strict=1`; permanent, decision D30 condition 5); `local_test=stale` (negative test:
`ICvGame1::Update` no longer invalidates the mirror). The epoch mirror replaces D21 item 5 (server diffs) by decision
D30 (MC, 2026-11-10), with six conditions; how each is met is in 16.4.

### 16.1 What is local now, and what invalidates each answer

| Answer | Where | Who can change it, on which thread | Invalidating event (seen by the proxy) | Confidence |
|---|---|---|---|---|
| `GetGame`, `GetPreGame`, `GetMap`, `GetGameAsynch`, `GetTeam(i)`, `ICvGame1::GetPlayer(i)`, `GetTerrainInfo(i)`, `GetFeatureInfo(i)`: a new proxy wrapper (own reference count, as the DLL's fresh `CvDll*` wrapper) over the server's permanent object of that kind and index (`kRt_Canon`, fetched once per generation, made with the wrapper's lock calls suppressed) | proxy | `CvGame`/`CvMap` are made in `CvGlobals::init` and freed in `uninit` (CvGlobals.cpp:3218-3219, 3274-3275), players/teams in `initStatics`/`freeStatics` from the same two, reached only from `ICvGameContext1::Init`/`Uninit` (CvDllContext.cpp:799-807); info objects are deleted and rebuilt by `ICvDLLDatabaseUtility1::CacheGameDatabaseData` after `FlushGameDatabaseData` and freed at `Uninit` (CvDllDatabaseUtility.h:104-139); `CvDllPreGame` is stateless. Any engine thread | the start AND (since 2026-11-11, issue 2801) the end of `ICvGameContext1::Init`, `Uninit`, `ICvDLLDatabaseUtility1::CacheGameDatabaseData`, `FlushGameDatabaseData`: the generation increments at both and both sides drop their table (old server objects stay alive and registered, so a wrapper the engine still holds keeps working as a stale in-process wrapper would; handles are never reused); while one of them is in flight on any thread no canonical handle is used, fetched or stored and the server refuses `kRt_Canon` (section 17.1; replaced: "the proxy's `Begin` of ..." only) | [C] source, issue 2701 Findings; [L-offline] hutest `canon_after_init_*` |
| `Destroy` of such a wrapper | proxy | - | - (the server object is permanent) | [C] |
| The game-core lock that `CvDllGame`/`CvDllPlayer` take in their constructors and release in their destructors (CvDllGame.cpp:21-33, CvDllPlayer.cpp:21-33) | proxy + server | the EXE's ui and core threads (aux threads always cross for these two) | per EXE thread the proxy counts its live local lock wrappers C and writes C into the thread's channel word of `Local\VPProxyLock_<pid>`; the server sets its explicit lock depth for that channel to the word in the prologue of every call it serves there (so the DLL's `HasGameCoreLock` and its one-level releases around Lua hooks, CvLuaSupport.cpp:381-415, see the in-process depth); the 0 -> 1 transition crosses at once (`kRt_LockSync`: the server takes the lock, waiting where the in-process constructor would wait) and so does the release to 0 (the lock is free when it would be free in process) | [C] design; [L] 0 hangs, binding lines 0 |
| 24 getters of the slot poll and its neighbours (`IsHuman`, `slotStatus`, `GetGameTurn`, `GetActivePlayer`, `IsPaused`, `GetGameState`, `IsGraphicsInitialized`, `IsAlive`, `IsEverAlive`, `IsTurnActive`, `GetID`, `GetTeam`, `IsOptionKey`, `IsOption`, `civilization`, `playerColor`, `isNetworkMultiplayerGame`, `IsNetworkMultiPlayer`, `GetNumGameTurnActive`, `CountNumHumanGameTurnActive`, `IsFinalInitialized` x2, `GetActiveTeam` x2), on canonical handles and the context only | proxy (epoch mirror) | only the DLL, and only inside a served call (the server has no thread of its own that runs DLL code; every served call passes the proxy) | every crossing that is not `pure` or `neutral` in pure-methods.tsv increments the epoch at its start and its end and is in flight in between; a value read while nothing was in flight is kept with its epoch and answered while the epoch is unchanged and nothing is in flight (the check reads epoch, in-flight count, epoch). Values the getters read: CvPreGame statics, CvGame state/pause/turn flags, CvPlayer alive/turn-active/options, the graphics flag (issue 2701 evidence per getter) | [C] by construction; [L] verify runs below |
| `ICvPlayer2::HasTurnTimerExpired()` = false | proxy | - | answered locally only while the mirror holds `IsOption(GAMEOPTION_END_TURN_TIMER_ENABLED)` = false: CvPlayer.cpp:49274 (49368 at c43ea1b9f, same text) -> CvGame.cpp:2068-2070 returns false and touches nothing when the option is off; otherwise the call crosses as impure (it may reset the timer and write pregame) | [C] |
| `ICvEngineUtility1::GetScriptSystem()` | server (the DLL's calls never cross after the first) | nothing: the EXE returns one static object (0x2DD2034, made on first use, freed at process exit; issue 2702 [C], decompile) | none needed in the server's lifetime (a DLL reload is a new proxy boot and a new server) | [C] |

`ICvGame1::UpdateTestEndTurn()` (once per frame from the EXE's ui thread) is impure but classified `neutral`: its own
writes (`CvPlayer::SetEndTurnBlocking` fields, multiplayer-only `m_sentAutoMoves`/`m_endTurnTimer`, achievements off
in VP) reach no input of a mirrored getter, and everything else it causes (engine calls, the `TurnComplete` Lua hook)
re-enters the DLL only through crossings that are classified on their own [C, CvGame.cpp:2199-2340]. Without this the
mirror was invalidated every frame (first live run, 170 calls per idle frame, 17:21).

What still crosses and why: every call whose answer depends on state the proxy cannot see change without a crossing
(plot, unit and city getters: their objects come and go and their values change inside AI turns; the mirror would
need per-object invalidation and the counts are small at idle), every command and notification (D21 item 3), the UI
Lua thunks (D21 item 1; `kRt_LuaCall` is impure), the script-system args traffic of GameEvents (`CreateArgs`,
`Push`, `CallHook`, ...: each is a new engine object or an engine-side effect), and the lock syncs themselves.

### 16.2 Temporary wrappers the engine keeps (item 4, issue 2503 F6)

Built behind `keep_temp` (vpproxy.ini, default 0 = the behaviour of epics 2300-2500). With it on, a temporary wrapper
of a DLL object passed to an engine call that the engine QueryInterfaces is promoted (`ProxyRt::KeepIfTemp`: a
forwarded `QueryInterface(ICvUnknown)` gives the server a persistent registration and the DLL one more reference).
*2026-11-10 (issue 2708).* The first version crashed the EXE in its first live run (c5h-a 18:06, 0xC0000005 inside
`GameplayUnitCreated`, 0.45 s after the first `kept` line; the only record is gbe's `run/gbe/crashes.txt`, address
0x00A07F3B, not mappable: the EXE's load base is not recorded). Cause [C]: the EXE's `GameplayUnitCreated` (0x5579C0)
QueryInterfaces its argument for ICvUnit2 (0x557CF5), Destroys the result at once (0x557D1E) and goes on calling the
argument (0x557D20-0x557EA7); KeepIfTemp had handed the dispatch scope's reference to that QI, so the Destroy freed
the wrapper in the middle of the call. Rule: the engine borrows an argument wrapper for the whole call; the scope keeps
its own reference whether or not the engine QueryInterfaces it. Reproduced offline by `build_hutest.py --keep-temp`
(`temp_arg_live_after_qi_destroy` FAIL on the crashing build). Fixed (fc0f85d, verified offline only, check
`temp_arg_held_after_call` added): promoted wrappers sit in a per-scope kept list; a release to zero inside the call
demotes the wrapper back to its scope and forwards one Destroy; at the scope's end a still-held wrapper loses only the
scope's reference. Cost: two crossings per QueryInterface-then-Destroy of an argument (zero with keep_temp=0). The
MC suspended the adoption of 2503 F6 until the crash is understood (2026-11-10 18:27); `keep_temp` stays 0 and the
fix is not run live [not verified]. Without it, the epic-2500 behaviour (the wrapper is freed when the call returns)
stands: no live run of this epic showed a wrapper used after its call. *2026-11-11 (issue 2804, MC agreed): item 4 is
closed: the EXE keeps no DLL-object argument past the call for any method seen live (section 17.4, with the list and
the LOUD check for any other method); `keep_temp` stays 0 for good and fc0f85d is not run live.*

### 16.3 Per-call detector of engine-side database writes (item 5, issue 2504 follow-up)

Not built; reason [I]. A cheap detector would run `SELECT total_changes()` on the engine's connection before every
engine -> DLL call (issue 2504's suggestion). Against it: the connection belongs to the EXE and is used by the EXE's
own threads without a lock the proxy can take (a query from the proxy at an arbitrary engine -> DLL call could
interleave with an EXE query on another thread: not shown safe); the closed library offers no change hook; and the
statement is a prepare/step/finalize through the closed library on every crossing, of the order of a crossing itself
(not measured). Engine-written tables are pulled at the next `ICvDLLDatabaseUtility1` check (section 14), which is
where they were seen (the load phase). Open: measure the cost and the thread question before building it.

### 16.4 How the conditions of decision D30 are met

1. **Impure by default.** A crossing counts as pure only when its model key is a `pure` or `neutral` row of
   `proxy/model/pure-methods.tsv` (114 keys) *that the loaded purity file lists (since 2026-11-13, issue 3503, section
   16.9: the list is data bound to the x64 DLL, derived by `proxy/tools/derive_purity.py`; no file, every call crosses.
   Replaced: "`proxy/src/proxy_pure.inc`, checked by `gen_pure_list.py --check`")*;
   every other engine -> DLL method, every runtime method except `kRt_Canon` and `kRt_LockSync` (Lua thunk calls,
   Lua type and reference plumbing, the harness Lua channel's lock yield and resume), every save,
   load, `Read`/`Write`, net `Response*` and database-utility call bumps the epoch. *Corrected 2026-11-11 (issue 2801):
   the stream-session sends `kRt_StreamOpen`/`kRt_StreamOpenPull` do not pass `Begin`; they are made inside
   `StreamInArg` while the outer `Read*`/`Write*` call is staged and in flight, so that impure call has already
   invalidated the mirror (verification 2700 A1). Replaced: "stream sessions" in the list above.* *(e4a2e24: issue 2701's audit had
   called kRt_LuaDescribeType, kRt_LuaRelease, kRt_StreamOpen and kRt_StreamOpenPull pure; they are impure now. The
   live series 7 ran the build before that change (b7, 20542a2): those four calls invalidated nothing there, so b7
   could keep a value across one of them; none of them occurs at idle and the verify runs found 0 mismatches [I:
   the effect on the live verify counts is not re-measured with the stricter build].)*
2. **No value that may depend on engine state.** The server counts, per channel, every DLL -> engine call its
   serving thread makes, crossing (`SrvRt::Begin`) or answered in the server (lock, `IsGameCoreThread`,
   `IsGameCoreExecuting`, `GetScriptSystem`), in words 4-6 of `Local\VPProxyLock_<pid>`; the proxy reads the word
   before and after a read it wants to keep and keeps it only if unchanged (counter `not kept (the server called the
   engine)` in the `local:` line). Time, random numbers and the calling thread: no mirrored getter reads them (issue
   2701 evidence per getter); `IsGameCoreThread` would be caught by the counter.
3. **Bracketed.** `MirrorBegin` samples the epoch, the in-flight count and the engine word before the read crossing;
   `MirrorPut` keeps the value only if all three are as they were.
4. **No DLL code outside a served call.** The server counts DLL -> engine calls, and since 2026-11-11 (issue 2801) every
   engine method it answers itself (lock, `IsGameCoreThread`, `IsGameCoreExecuting`, `GetScriptSystem`,
   `nextSelectedCitiesNode`, the local enumerator, `CreateLuaThread`/`FreeLuaThread`/`CallFunction`/`CallCFunction`;
   `VP_SERVER_TEST_OUTSIDE=2`: counter 3), made on a thread that is not inside a
   served call (`local: DLL -> engine calls outside any served call: N` in `server-report.txt`, must be 0; the first
   one marks the run FAILED). In remote mode the server's loopback is off (section 11.2) and its other threads are the vpipc monitor, the guard's
   watchdog and the main thread waiting for the service threads (stacks of 2026-11-10 19:46, 16.6) [C for that
   process; the counter covers every DLL -> engine call]. Negative test: `VP_SERVER_TEST_OUTSIDE=1` runs `GetGame`/`Destroy` (the CvDllGame constructor
   and destructor call the engine's lock) on a thread of its own at the first `kRt_Canon`: the headless Tiny run ends
   `FAILED: DLL code ran outside a served call`, counter 2 [L, `server/evidence/2700/headless/outside/`].
5. **Verify mode is permanent** (`local_verify=1`), results below.
6. **Evidence per entry**: every row of pure-methods.tsv carries its file:line chain (issue 2701); a getter that writes
   a cache, a lazy value or a mutable member is impure there (e.g. `CvCity::HasGarrison` writes `m_hGarrison`; no
   mirrored getter reaches it).
   **Last audited at the DLL pin x64 76992d235 (= i0-ship bbfacca1a + port), 2026-11-13, issue 0111; no verdict changed
   (17.5a).** Earlier audits: e9d0c4837 (issue 2701), 29f15ac38, c43ea1b9f (17.5). Re-audit at every pin move.
   *Added 2026-11-24 (ticket 0628): at the pin x64-0.1b 1b87dd185 the lists were re-baselined mechanically (ticket 0616,
   `release-builds.md` 6.4), and the 4 rows the derivation dropped there were re-audited by hand and are `pure` again
   (17.5b).*

### 16.5 Measurements under the real EXE (c5h-a, 2026-11-10) [L]

Setup: instance c5h-a (Goldberg, CP + VP + EUI), launches on the early route, every arm by `proxy/tools/live/arm.py`
(recipe section 10), raw output in `server/evidence/2700/` (README there). Kinds: **on** = proxy with
`local_answers=1`; **off** = the same proxy build with `local_answers=0` (every call crosses: epics 2300-2500's
proxy); **B** = the off switch handing the EXE to the 32-bit i0-ship e6f904ad6 + instance-aware instruments (issue
2705; the same game logic as the x64 pin apart from D18, below). Frame rate: the Lua frame counter
(`Events.LocalMachineAppUpdate`) over a 45 s wall-clock window on the human's turn with nothing open, the same tool in
every kind. Machine: i9-13900KS shared with other agents' work during every run (epic 2400's servers were running;
see the arms' timing spread: the load phase took 92 s to 303 s for the same save); no number is a quiet-machine
number.

**Calls per idle frame** (the proxy's snapshots, crossings both directions; Tiny turn 8 save, build b7 20542a2):

| | epic 2500 (P1, 2026-11-10 15:2x) | off (T1-T3) | on (T1-T3) |
|---|---|---|---|
| Tiny idle frame, crossings | 589 | 590.1-591.1 | **30.7-33.7** |
| Tiny idle frame, answered locally | 0 | 0 | 575-593 |
| frame while the 10 AI turns and the checklist ran | - | 689-704 | **107-127** |

What still crosses in an idle Tiny frame (T2-on, 15 engine -> DLL calls + 16 runtime calls per frame, method by
method in `live7/T2-on/vpproxy-snapshot-methods.tsv`): `rt:LockSync` 16 (two per lock session: the slot poll and the
other engine call sites take the game-core lock about 8 times a frame; the 0 -> 1 and -> 0 transitions must cross,
16.1); plot reads 5.8 (`GetPlot`, `IsRevealed`, `GetActiveFogOfWarMode`, `IsRiver`, `GetFeatureType`,
`GetImprovementType`, `GetTerrainType`: plots are not canonical, 16.1); unit reads 2.9 (`GetUnit`,
`IsDelayedDeathExported`, `IsBusy`); `UpdateTestEndTurn` 1 (a command); mirror misses 3.6 (`IsHuman`, `slotStatus`
after each invalidation by the core thread's `Update`, which runs about every 36th frame at idle); the rest below 0.2
per frame (`GetTerrainInfo`/`GetFeatureInfo` getters on canonical infos, `GetWorldSoundscapeScriptId`). Huge idle frame
(H1-on, b7): 58.8 crossings (16 lock syncs, 22 plot and terrain reads of the larger visible area, 6.3 mirror misses)
and 587 local answers.

**Frame rate, Tiny idle** (interleaved T1-on, T1-off, T1-B, T2-..., T3-..., build b7, 18:22-19:20):

| | n | mean fps | min | max | stdev |
|---|---|---|---|---|---|
| on | 3 | 413.4 | 339.8 | 457.2 | 64.1 |
| off | 3 | 351.8 | 320.3 | 369.5 | 27.3 |
| B (32-bit DLL) | 3 | 441.4 | 434.0 | 451.5 | 9.1 |

T1 (18:22-18:46) ran while the machine was much busier than T2-T3 (its load phase took 303 s against 151-242 s; T1-off
and T1-on both low). The final reading, with six repetitions of each kind, is in 16.5b.

**Equality.** Every arm loaded the same save, played the same 10 AI turns with the same UI steps (checklist: AI turns
with leader screens answered, city screen, tech tree, diplomacy overview, leader screen, save, unit select, hex pick;
all pass in all arms) and saved at turn 18. Compared with `eq_save_body.py` (whole unpacked body) and `eq_stats.py`
(377 statistics keys of every player and team):
- same kind twice (control: on/on, off/off, B/B): body equal except 1-2 bytes at offsets 84 and 12,771,609 (the same
  offsets in every pair: run noise, also found by the 2500 verification); statistics identical;
- on against off (T1, T2, T3): the same 2 noise bytes, nothing else; statistics identical;
- on (x64) against B (32-bit): the version string (+12 bytes) and 4 regions, 2,249 differing bytes, after the
  embedded database; statistics identical. Issue 2707 [C]: the regions are `CvEconomicAI`'s three exploration-plot
  sets of players 0-3 (`m_vPlotsToExploreLand/Coast/Ocean`, `tr1::unordered_map`), the same records in a different
  order: decision D18 (fork e9d0c4837, x64 branch only) writes unordered containers in sorted key order and i0-ship
  does not have it. Not a game-state difference. Tool: `server/tools/explore_sets.py`.

**Verify mode** (local_verify=1: every local answer also asked of the server and compared):

| Run | Verified local answers | Mismatches |
|---|---|---|
| strict Tiny 0-50, headless host, build b9 e7ea828 (`headless/verify`) | 47 mirror answers + 203,454 `GetScriptSystem` answers | 0 |
| strict Tiny 0-50, headless host with `test_slot_poll=4`, epic 2800 build B3 741325b (`2800/final-offline`) | 1,300,212 mirror answers | 0 |
| live Tiny, 10 AI turns + checklist, build b7 (`live7/T-verify`) | 5,811,310 mirror answers + 46,296 `GetScriptSystem` answers | 0 |
| live huge, 350 -> 356 with leader screens, build b10 (`live10/H-verify`) | see 16.5b | |

**Negative tests.**
- `local_test=stale` (Update does not invalidate): headless Tiny ends at the first turn change, `VERIFY MISMATCH
  ICvGame1::GetGameTurn() const: mirror 0, server 1`, exit 6 [L, b9]. Live (b7, T-stale): **not caught** (0 mismatches
  in 305,499+ verified answers): in the real EXE other impure crossings (Lua thunk calls, `UpdateDeferredFog`, ...)
  surround every `Update`, so skipping `Update` alone leaves nothing stale. Hence `local_test=frozen` (no crossing
  invalidates): headless Tiny exit 6 at the first turn change [L, b9]; live: 16.5b.
- `VP_SERVER_TEST_OUTSIDE=1` (DLL code on a thread outside a served call): the headless run fails, counter 2 [L, b9].
- Headless strict Tiny 0-50 with local answers on, off and verify: all three `stats.db` identical to the direct run
  (15 tables, 9,257 rows) [L, b9].

### 16.5b The huge save and the final build (series live10, build b11 5fa3f5b, 2026-11-10 20:13-) [L]

Build b11 = the lock-want fix e7ea828, per-thread lock counters, the stricter D30 runtime purity and `local_test=frozen`.

- **The 19:36:55 autosave** written seconds before the 19:37 hang (copy `c5h-a-hangcopy-AutoSave_Post_0350-193655`,
  SHA-256 fc0b53ce...c9d9) loaded and played its AI turn 350 -> 351 with three leader screens answered
  (`turns.py wait --answer 8`): whole. Verify mode: 129,189 `GetScriptSystem` answers, 0 mismatches; 0 DLL calls
  outside a served call (`live10/H-autosave1936/`).
- **Huge save in verify mode** (H-verify, C5H-VP8P-HUGE_0350, 6 AI turns 350 -> 356, past turn 355, the checklist with
  leader screens answered, city screen, tech tree, diplomacy overview, leader screen, save, unit select, hex pick: all
  pass): **2,201,450 mirror answers and 637,954 `GetScriptSystem` answers verified, 0 mismatches**; 0 DLL calls
  outside a served call; no deadlock. Lock counts per thread (server-report.txt): ui: lock-want acquisitions 584,089
  (first-level 333,961) = releases, the DLL's own lock calls 242; core: the DLL's own lock calls 200,417 (first-level
  199,229) = releases, lock-want acquisitions 61,258 (first-level 1,216) = releases; aux 0.
- **Before and after, the same 6 AI turns** (H1-off = local answers off, H1-on = on; server per-turn counters, turns
  351-355; 2500's P3 for comparison):

  | Per huge AI turn | epic 2500 P3 (351-352) | off | on |
  |---|---|---|---|
  | all crossings | 2.29 M | 2.00 M | **1.41 M** |
  | `ICvGame2` + `ICvGameContext3` + `ICvPlayer3` + `ICvPreGame2` (the slot poll) | - | 0.65 M | 0.12 M |
  | runtime (mostly `kRt_LuaCall`, the UI Lua thunks; on: also `kRt_LockSync`) | - | 0.75 M | 0.82 M |
  | `ICvEngineUtility4` (`GetScriptSystem` now answered in the server) | - | 0.117 M | 0.016 M |
  | `ICvEngineScriptSystem1` + `Args1` (GameEvents: CreateArgs, Push, CallTestAll, DestroyArgs) | - | 0.177 M | 0.178 M |
  | `ICvPlot1` | - | 0.124 M | 0.120 M |

  Frames while the AI turns ran (the proxy's snapshots over the checklist): off 45.9 fps, 1,776 crossings per frame;
  on 54.1 fps, 963 crossings per frame. Idle huge frame: off 184.8 fps (630 crossings), on 221.0 fps (58 crossings).
  Lock counts, the whole run: off: the ui thread made 3,676,150 lock calls through the DLL's wrappers, 324,629 of them
  first-level; on: 242 such calls and 461,205 lock-want acquisitions, 353,461 first-level (a first-level acquisition is
  the one that can wait; the slot poll's sessions are first-level in both).
- **Equality, huge** (saves at turn 356 after the same 6 AI turns and UI steps; 3,577 statistics keys of every
  player and team): on against off: identical statistics; the bodies (37.7 MB) differ in 181 single bytes, at exactly
  the same 181 offsets as the control pair on/on (H-verify against H1-on, 183 bytes): run noise, nothing from the
  local answers (`live10/savebody-*.txt`). On (x64) against B (32-bit): identical statistics; the bodies differ by
  +696 bytes of length and about 319 KB in 298 regions. Most regions visible in the first 400 are re-ordered records
  of small unordered containers (the D18 pattern of issue 2707, e.g. pairs `(1, x)(11, y)` written as `(11, y)(1, x)`).
  **Not only that** [L, not traced]: some notification records hold a text in the 32-bit save ("A Unit was Killed!",
  "Public Declaration from Germany", "An Enemy is Near!", "Public Declaration from England") where the x64 save has an
  empty string; the cause is that the server's database copy has no `Localization` attached: `GetDiploResponse` joins
  `Language_en_US`, so the messages built from diplomacy responses were empty while the summaries (plain lookups) were right
  [C, issue 2802; fixed, section 17.3; replaced 2026-11-11: "the likely cause is the server's localization ... [I]"]. It is the same with local answers on and off, so it is not caused by this epic;
  it also emptied AI leader speech and trade replies under the proxy [C]; fixed by issue 2802 (section 17.3).
  `server/tools/explore_sets.py` found no exploration triples in the huge body (its record-shape assumptions are tuned
  to the Tiny save), so the huge difference is not attributed byte by byte.
- **Tiny, final build** (T-verify, 10 AI turns + checklist, all pass): 5,061,824 mirror answers and 45,798
  `GetScriptSystem` answers verified, 0 mismatches; 0 DLL calls outside a served call.
- **Live negative test** (T-frozen: `local_test=frozen`, nothing invalidates the mirror, `local_verify=1`, `strict=0`):
  the first mismatch 46.6 s after boot (`VERIFY MISMATCH ICvGame1::IsPaused(): mirror 1, server 0`), 99,590
  mismatches in 7,540,824 verified answers by the end; the checklist failed. The verifier catches a stale mirror in the
  real EXE.
- **Frame rate, Tiny idle, all six interleaved repetitions** (T1-T3 build b7 18:22-19:20; T4-T6 build b11, idle only,
  21:00-21:41; `server/evidence/2700/fps-tiny-all.txt`):

  | | n | mean fps | median | min | max | stdev |
  |---|---|---|---|---|---|---|
  | on (local answers) | 6 | 399.8 | 399.2 | 339.8 | 457.2 | 46.2 |
  | off (every call crosses) | 6 | 348.7 | 348.1 | 320.3 | 369.5 | 18.2 |
  | B (32-bit DLL) | 6 | 425.1 | 426.5 | 391.3 | 451.5 | 21.1 |

  Paired by repetition (on - B): -99, -8, +23, -8, -31, -29 fps; mean -25 (-6%), so on is below B in 5 of 6 pairs by
  less than the spread of on itself; off is 18% below B in every pair. Huge idle (one each, b11): on 221.0, B 207.0,
  off 184.8 fps. Reading: the local answers remove most of epic 2500's idle-frame deficit (Tiny 18% -> about 6%, huge
  -11% -> +7%); parity on Tiny is within the noise of this shared machine but not shown beyond doubt; what is left per
  idle Tiny frame is about 31 crossings (16 of them lock syncs) and the proxy's own work for about 575 local answers.

### 16.6 Defects of the local answers found only live (each fixed or switched off)

1. **The mirror invalidated every frame** (first arm, 17:21, 170 crossings per idle frame): `UpdateTestEndTurn`
   (once per frame from the EXE's ui thread) was impure. Classified `neutral` after reading it (16.1): 32 crossings.
2. **Item 4 crashed the EXE** (18:05, 0xC0000005): 16.2; `keep_temp=0`.
3. **Deadlock in a huge AI turn** (19:37, build b7): the server's core thread waited in `LuaSupport::CallTestAll` for the
   engine, the server's ui thread waited in the lock-want sync (`EnterCriticalSection`), stacks read with `cdb -pv`
   (non-invasive) on server.exe. Cause [C, by the stacks and the code]: inside a nested engine callback on the core
   thread the engine took and dropped a local lock wrapper (C 1 -> 2 -> 1) with no crossing afterwards, so the server
   kept the explicit depth 2; `CallTestAll`'s one-level release left the lock held (in process it would have been
   free) while the engine's Lua needed the ui thread. Fixed in e7ea828: the server also syncs the depth right after
   every DLL -> engine call returns (`SrvRt::End`), and the proxy crosses on every 0 <-> 1 transition. The game was
   stopped at 19:53 by the harness with the MC's override for that process (no save in progress; `live7/H1-on/stop.txt`).
   The Tiny runs never hit it (the headless host is single-threaded; Tiny AI turns took no nested lock wrapper at the
   wrong moment). Regression test (issue 2709, offline): hutest check `dll_lock_nested_release` (Update holds the
   game wrapper; the engine's `GameplayTurnChanged` handler takes a second local game wrapper, crosses once while
   holding it, drops it; the DLL then releases one level as `CallTestAll` does and must no longer hold the lock). Old
   code 64eba3b: FAIL (`1,1`, the stale explicit depth); e7ea828: PASS. Mixed builds show it covers the server half of
   the fix (`LockSync` after `end_call`); the proxy half (crossing on every 0/1 transition) is not exercised by it.

### 16.7 The projection for an AI turn, now measured

Section 7 projected 0.24-0.30 s of IPC per huge AI turn with hybrid UI Lua (0.42-0.47 M UI-Lua calls) and every
lifetime call local. Measured on the huge save (b11, turns 351-355): **1.41 M crossings per AI turn** with local
answers (2.00 M without; 2.29 M in epic 2500's P3), of which about 0.8 M are UI Lua thunk calls (`kRt_LuaCall`: the
real EXE's UI Lua during AI turns, D21 item 1; the hybrid mirror of hot pure Lua reads of section 6 is not built),
0.18 M GameEvents script-system calls (D21 item 2: CreateArgs, Push, CallTestAll, DestroyArgs), 0.12 M plot reads and
0.12 M slot-poll residue (mirror misses while `Update` is in flight, by design). At the 0.45-1.0 us per crossing of
section 12: **0.6-1.4 s of IPC per huge AI turn**, against AI turns of 27-33 s wall time on this save. The known limit
(D30): while a long impure call is in flight (an AI turn inside `Update`) the mirror answers nothing, so a frame during
an AI turn costs 963 crossings (huge; 107-127 Tiny) against 58 (huge; 31 Tiny) at idle; it still costs less than
without local answers (1,776 huge; 689-704 Tiny) because the canonical handles, `Destroy` and `GetScriptSystem` stay
local throughout. Next cuts, by size: the hybrid Lua mirror (section 6, option C), GameEvents args batching, a plot
mirror with invalidation by the DLL's plot notifications, and fewer lock syncs (one session per frame instead of ~8).
*Corrected 2026-11-11 (epic 2800, issue 2805; section 17.7): the UI-Lua thunk calls of AI turns do NOT run while
`Update` is in flight. Counted by the server per call (`proxy-lua-calls.tsv`; phase = the proxy's epoch state just before
the call: `open` = nothing impure in flight, `in_flight` = an impure call such as `Update` running) on the huge save,
10 AI turns 350 -> 360 with the checklist, build B3: 7,732,245 UI-Lua calls, **98.3% open**, 0.3% in flight (the rest on the
core channel, 1.4% open); Tiny 10 turns: 96.0% open. So an epoch mirror of pure Lua bindings can answer them; the
limit stated above holds for the engine -> DLL getters asked inside `Update`, not for the UI Lua.*

### 16.8 Not verified, open

- Issue 2701's purity audit read the DLL source at e9d0c4837; the builds ran pin 29f15ac38 (D25 and the turn-355 fix
  on top). The 24 mirrored getters were not re-read at 29f15ac38 [I: no change expected; the verify runs found 0
  mismatches on it].
- *Closed 2026-11-11 by section 17.1 item 2 (two-thread hutest checks; M1 and M2 fail them).* The proxy half of the
  lock-want fix (a crossing on every 0/1 transition) had no offline test of its own (2709 covers
  the server half); live: no deadlock in 7 huge AI turns with leader screens (H-autosave, H-verify, H1-on, H1-off) and
  15 Tiny arms on b11, b7 deadlocked once.
- Item 4 (keep_temp) is off and not needed (issue 2804, section 17.4); fc0f85d stays unused. Item 5 not built (16.3).
- Huge x64-vs-32-bit save difference not attributed byte by byte; empty notification texts in x64 saves (16.5b).
- One machine, shared and busy; no quiet-machine frame rate; Steam-launched EXE not tried.

### 16.9 The purity lists as data: purity.tsv and its derivation (issue 3503, epic 3500, 2026-11-13)

- **What changed** [C]. The proxy holds no compiled purity list: `proxy/src/proxy_pure.inc` (`kPureKeys`) and
  `proxy_lua.cpp`'s `kLuaPure` are gone. `proxy/src/proxy_purity.cpp` reads `purity.tsv` (format and every refusal:
  `tools/release/README.md` section 4) bound by `dll_sha256` to the x64 DLL; `ProxyLocal::InitTables` marks a method
  pure when `ProxyPurity::IsPureMethodKey` lists its model key, `NoteFuncName` a Lua function when `IsPureLuaName` lists
  one of its names. No file, a refused file or a file for another DLL: both lists empty, every engine -> DLL crossing
  and every Lua call is impure, so the epoch mirror never keeps a value and the UI-Lua mirror answers nothing (D30's
  safe default); the canonical handles and the lock-want word (16.1) do not depend on purity and stay local. Only
  `kRt_Canon` and `kRt_LockSync` stay compiled as pure (`kPureRt`, the proxy's own messages). The boot log line is now
  `local: ... N pure method ids (M of K listed keys in the model; L Lua names)`, preceded by the loader's `purity:` line.
  The boot's `ProxyPurity::Load` call is issue 3504's; until it lands `InitTables` loads
  `VP_PROXY_PURITY=<purity.tsv>|<x64 DLL>` (test only).
- **The audit frozen** [C]. `proxy/model/purity-baseline.json` (`derive_purity.py --make-baseline --fork <clone>`,
  13 s): for the 143 rows that can be listed (114 method keys: the 117 `pure` + 1 `neutral` rows less the four `kRt_*`;
  29 Lua `pure` rows), each of the 360 DLL-source citations of the evidence columns, read at the commit it was written at
  (pure-methods.tsv at e9d0c4837, issue 2701; lua-pure-bindings.tsv at c43ea1b9f, issue 2805) and located at the last
  audit 76992d235 (issue 0111): 309 at the same lines, 51 moved with identical text, 0 unresolved; plus the 350
  function names those lines call and, per name, the SHA-1 of every definition (function bodies found by a brace scan of
  the comment-stripped source, `#define`s with their continuation lines) at 76992d235. Text is compared after comments
  and whitespace are folded. 17 called names have no definition in the DLL source (library calls such as `c_str`,
  `lua_pushboolean`; member initializers).
- **The derivation** [C]. `derive_purity.py --src --dll --out` keeps a row only when every citation's text is found in
  the new source (same lines first, else any position) and every called name's definitions hash the same as at the
  audit; it reads the DLL source only (D83). This is issue 0111's citation and callee checks made mechanical, with
  the callee check stricter (whole bodies of every definition of the name, not the names in diff hunk headers) and
  without git at release time. [L 2026-11-13 22:0x] **At the pin 04b1211b8: 143 of 143 kept** (no cited text and no
  callee changed between 76992d235 and 04b1211b8), 9 s; the file is bound to the pin DLL 74588135...4570.
- **Seeded changes on a scratch copy of the pin's sources** (`proxy/tools/test_purity.py`) [L 2026-11-13 22:19 EST]:
  a statement added inside the cited `isAlive` lines of CvPlayer.h drops `ICvPlayer1::IsAlive() const` and every row
  calling `isAlive` (3 rows); a statement added to the body of `luaL_optbool` (called by one cited Lua binding, defined
  outside every cited file) drops exactly that row; `+ 0` added to the `GET_PLAYER` macro drops exactly its 9 callers; a
  comment added inside a cited range, or 5 lines inserted above it, drops nothing.
- **The test's row counts come from the lists (issue 0478, 2026-11-17)** [C, L]. Until 0478 `test_purity.py` section 3
  compared derive's output with the literals 114 method / 143 rows of the first baseline (3503). The lists grew on
  main with epic 0375's merge d7dbb94bd (2026-11-16 17:14 EST: 4452's rows and 4460's `GetMinorCivType`, baseline
  135 method + 29 Lua = 164) and 0439's merge 20d5985a6 (20:31 EST: 137 + 29 = 166), so from d7dbb94bd on 7 derive
  checks failed on main (the "every audited row kept" check, the report check and the "rest present" check of cases
  a-e); worker 2606 found it on 2026-11-17 02:24. **No suite caught it** because no runner calls the full test:
  `build_proxy.py` step 0 runs only `gen_pure_list.py --check` (which re-reads the lists, so it stayed green), the
  release driver's purity stage runs `derive_purity.py`, and the checklists run `test_purity.py --only regen` (section
  6, no derive). The full test ran only by hand (`tools/release/README.md` section 4 Tools) until issue 0480 (next
  bullet). Now the test reads the
  rows the purity file must carry from `proxy/model/pure-methods.tsv` and `lua-pure-bindings.tsv` itself (pure or
  neutral method rows less `kRt_*`, pure Lua rows: the rule of `tools/release/README.md` section 4 "What it lists",
  whose counts 118/114 there are those of 2026-11-13; at 0478 the lists give 141 pure+neutral method rows less 4 `kRt_`
  = 137, and 29 Lua) and compares row sets, not counts: baseline entries = listed rows, purity.tsv and report.tsv at
  the pin = listed rows, each seeded case's file = listed rows less the dropped ones. A negative control inside the
  test removes one kept row from a scratch copy of `pure-methods.tsv` and requires the same comparison to fail.
  [L 2026-11-17 03:00-03:02 EST] `test_purity.py --src F:/c5-64bit-work/0334/rebuild/src-x64-04b1211b --dll
  %USERPROFILE%/c5-i0/dlls/x64-04b1211b-pfdee656/bin/CvGameCore_Expansion2.dll` (2606's inputs, branch
  task-0478-test-purity-counts): **66/66** (was 56/63 on main), negative control fails as required.
- **The full test runs automatically (issue 0480, 2026-11-17)** [C, L]. Two runners now call
  `test_purity.py` in full (unit, ASan, derive, refusals, check, regen):
  (a) the release driver's `purity` stage after `derive_purity.py` (`research/06-tooling-and-workflows/release-driver.md`
  W2); (b) `proxy/build_proxy.py` step 7, after the output checks, with `--src` and the built proxy DLL (the test only
  hashes `--dll`), scratch `<out>/purity-test` removed after a pass. Step 7 runs only when the SHA-256 over its inputs
  changed since the last pass on the machine: `proxy/model/*`, `derive_purity.py`, `gen_pure_list.py`,
  `test_purity.py`, `rt_tests/purity_unit.cpp`, `proxy_purity.cpp/.h`, `proxy/gen/*.py` (26 files, CRLF folded) and the
  source's commit; a pass writes `C:/c5-64bit-work/cache/purity-test/<key>.json` (`$C5_PURITY_TEST_CACHE`). Not run, with
  a line, for an `--unlocked` source off the pin (rows drop there, and the test needs every row kept) and, LOUD, without
  VS 2022's vcvarsall or with `--no-purity-test`. A failed check fails the build. MC accepted this design on 2026-11-17
  (queue answer to 0480): every build paying the full test was too costly (builds 20-111 s).
  Measured [L 2026-11-17, pin source `src-x64-04b1211b` (0334's rebuild copy on F:), workstation with other heavy jobs registered]:
  | run | result | time |
  |---|---|---|
  | standalone `test_purity.py` (07:05) | 66/66 | 107 s |
  | stage `purity` alone (`take_release.py Release-5.4.8 --only purity`, 07:17) | ok: derive 9 s (137 method, 29 Lua, 0 dropped), test 66/66 | 82 s test |
  | `build_proxy.py`, first build (07:18) | step 7 ran, 66/66, stamp written | 77 s of 123 s |
  | `build_proxy.py`, second build (07:21) | step 7 "passed before for these inputs" | 0 s of 38 s |
  | **negative control**: scratch branch with `test_purity.py` from before 0478 (literals 114/143), not committed | stage `failed`, "7 check(s) failed" (the 7 derive checks of 0478); `build_proxy: FAILED` (56/63, 83 s, no stamp) | - |
  | same scratch branch, `--no-purity-test` | `build_proxy: OK`, LOUD line | 41 s |
- **The verify leg** (checklist step 12 command: headless Tiny 0-50, `strict=1 local_answers=1 local_verify=1
  test_slot_poll=4 lua_mirror=1`) on build b2 (repository 85097e0d, server and proxy built with `--deps` of 9045's
  `bF`, x64 DLL 74588135...: the pin's) [L 2026-11-13 22:41-22:43 EST, `D:/c5-64bit-work/3500/w2/v2-*`]:

  | purity file | proxy's load line | verified answers | mismatches |
  |---|---|---|---|
  | derived for the pin (VP_PROXY_PURITY) | 114 method keys, 29 Lua names; 348 pure method ids (114 of 114 keys in the model) | 1,300,212 getter + 1,198,941 UI-Lua | 0 + 0 |
  | the same file, bound to another DLL (the hash taken of server.exe) | refused: bound to another x64 DLL; 0 pure ids | 0 (1,734,625 mirror misses, every read crossed) | 0 |
  | none | no purity file; 0 pure ids | 0 (1,734,625 misses) | 0 |

  All three PASS. With the file the counts equal 9045's reference run on the compiled lists exactly (proxy-design 17.2,
  `deps/dll.lock` `verified`), so the data path gives the proxy the same purity as the compiled one did. Run times 70 s
  with the file, 40 s without (no local answer, so nothing to verify twice).
  The proxy's existing suites on b2 [L 2026-11-13 23:55 - 2026-11-14 00:00]: `test_proxy_rt.py` 79/79, hutest 46 checks
  0 failed (PASS), hutest `--scenario all` 8/8; `test_purity.py` 60/60 (the loader's unit test 63/63 with clang and again
  under MSVC `/fsanitize=address`, with the canary caught); `test_policy_table.py` 13/13 on the regenerated model;
  `apply_consumers.py --check` up to date. Scratch (`D:/c5-64bit-work/3500/w2`, `C:/c5-64bit-work/3500/w2`) deleted at
  the hand-back; the numbers here are the record.
- **Idle-frame crossings on Tiny with the derived file, measured 2026-11-14 (3514 item 5) [L]: 44.0 per frame, against
  42.8 with the pin's compiled lists in the same conditions. So the derived file changes nothing, but both are above
  the 31-34 of 2026-11-11.** Instance c5h-b (C:). Save `C5H-3514-TINY`, a copy of `D:/c5-64bit-work/3500/inputs/
  REL548-TINY_0008.Civ5Save` (`b2ceb663...`, 9029's live Tiny turn 8). `arm.py c5h-b idle-pack --mode on --local 1 --idle
  45 --ini pack=1`, the fallback DLL the pin's `i0ship-26234371-ia`, counts only (no lock hold, no speed claim; the
  workstation was shared). Script `C:/c5-64bit-work/3500/3514/item5.py` (scratch).
  | Arm | Layout | Purity | Idle window | Crossings / frame | Local answers / frame |
  |---|---|---|---|---|---|
  | derived (02:34) | the pin's x64 DLL 74588135 + server c2ca88d4 and proxy 101aa991 of 3514's branch (built at repo 2e33a9a4, dirty=0, on the pin's source 04b1211b8) in a `pack.py` pack | `derive_purity.py` for 74588135: 143 of 143 rows kept; `boot: purity: loaded (pack=1) ... 114 method keys, 29 Lua names; bound to the x64 DLL 74588135...` | 45.5 s, 9,062 frames, 199 fps | **44.02** | 569.95 |
  | control (02:40) | the pin's own layout `x64-04b1211b-pa803776` (proxy before 3503: compiled lists) | `348 pure method ids (114 keys)` (compiled) | 45.3 s, 10,569 frames, 233 fps | **42.79** | 569.09 |
  The crossing mix is the same method by method (derived / control): `rt:LockSync` 16.00 / 16.00; mirror misses
  `IsHuman` 4.35 / 4.69 and `slotStatus` 4.28 / 4.61; plot reads (`IsRevealed`, `GetActiveFogOfWarMode`,
  `ICvMap1::GetPlot`, `IsRiver`, `Destroy`) about 9.3 / 7.9; unit reads 2.9 / 2.9; `UpdateTestEndTurn` 0.95 / 0.96.
  Against 2026-11-11 (31: LockSync 16, plots 5.8, units 2.9, mirror misses 3.6), the excess is in the mirror misses
  (about 9 against 3.6) and the plot reads. Both arms ran at 199-233 fps against 437 then. The misses follow the core
  thread's `Update`, which invalidates the mirror on a clock, so they are spread over fewer frames at a lower frame rate
  [I]. The save and the camera also differ from `c5h-c5h-a-t0008` [I]. So the derived file is confirmed equivalent to
  the compiled lists in crossings, and the 31-34 range does not hold today for either. The range needs the conditions
  of 17.7 (that save, about 437 fps) before it can be used as a check. (Replaced 2026-11-14: "not measured", below.)
- **(History, superseded by the measurement above.) Idle-frame crossings on Tiny with the derived file: not measured** (done C of epic 3500 asks for 31-34, today's range,
  17.7 / 17.11). Two live arms on the workstation instance c5h-e (`proxy/tools/live/arm.py ... --mode on --local 1
  --idle 45`, save copy `c5h-3503-tiny-t0008` of `c5h-9029-tiny-t0008`, the derived file in the instance's `VP64\`
  loaded by the interim beside-the-DLL path, build b2) both ended in `wait_game`'s 900 s timeout before the save loaded,
  so the proxy never booted (no `Logs\VP64\run`): 22:53-23:12 during ticket 0158's capacity window (mod activation 255 s,
  then `LoadUnitLibraries` 99 s, then nothing), 23:15-23:37 after it (mod activation 170 s, then nothing for 15 min).
  Cause not found; c5h-e is the one instance on the D drive (run folders there are slow, EFFECTIVENESS planning numbers)
  and was otherwise only used for the huge save [I]. No testbed job runs a proxy-route Tiny idle window (0156's job is
  the huge D33 product path). Inference [I]: the derived file lists exactly the 114 + 29 keys the compiled lists had
  ([C]: set-compared with `proxy_pure.inc` and `kLuaPure` at caa32326, both equal), and the headless verify leg verified the same 1,300,212 + 1,198,941 answers as 9045's run on the compiled lists, so
  the idle-frame mix should be unchanged (about 31 crossings, 17.7); to be confirmed by a live arm once 3504's boot loads
  the pack's file (`arm.py` on a C-drive instance, counts only).
- **What it does not see** [I]. A change to a member's declaration or type (a header field) that the cited text reads
  but does not call; a change reached only through a virtual call or a function pointer; a callee of a callee (the
  evidence chains cite each level, so the second level is covered only where the audit cited it). The name-based callee
  check also over-drops: any change to any overload of a common name (`GetID` has 26 definitions) drops every caller.
  Dropping is the safe direction (impure); the report says why, for the CL step 11 re-audit.
- **A layout trap, found live** [C, L 2026-11-13 22:34-22:40]. The first proxy with the loader crashed the headless host
  0.6 s after the boot whenever a purity file was read (0xC0000005, execute at a stack address, `ebp` 0). Under cdb the
  stack held a return into `ProxyPurity::Load` from `std::vector<std::string>::insert`: `proxy_purity.cpp` included
  `proxy_purity.h` (`<string>`) without first turning the VC9 iterator checks off, so its `std::string` had the checked
  layout while every other proxy TU has the fork's unchecked one (`proxy_rt_logic.h`, rule of 2026-11-10), and the
  linker kept one instantiation of the shared template. The unit test passed under clang and MSVC ASan because both of
  its TUs had the same (wrong) layout. Fixed by `#define _SECURE_SCL 0` and `_HAS_ITERATOR_DEBUGGING 0` before the first
  include of the .cpp (and of the unit test); `test_purity.py` checks the order; proposed for `proxy_purity.h` itself
  (thread of epic 3500). Rule for any new proxy TU: those two lines first.
- `gen_pure_list.py --check` (build_proxy step 0) now checks that the audited TSVs are well-formed and equal to the
  baseline, that every listed key names a model method (`interfaces.json`) or Lua function (`lua.json`), and that
  `proxy/src` holds no compiled list.
- **Where it ships, and what a release does with it** (issue 3507, 2026-11-14). `purity.tsv` sits in the DLL pack beside
  the x64 DLL it is bound to (`VP64\`, `installer-notes.md` sections 6-7). With `pack=1` the boot hashes it with the
  manifest and calls `ProxyPurity::Load` while holding it (3504, `release-driver.md` W3). The release driver makes it in
  its `purity` stage after the `model` stage (CL steps 10-11). On upstream master (the dry run, `release-driver.md` W7.2,
  row 14) the `model` stage stopped before `purity` could run: `extract_lua.py` finds 0 compiled Lua registrations in
  master's refactored Lua API (issue 3519). With 0 registrations, the regenerated `lua.json` would make
  `gen_pure_list.py --check` refuse every one of the 29 Lua rows. So the extractor must be ported before the next
  release's purity file can be derived [I: not run past the stop].

## 16a. Independent verification of section 16 (2026-11-10 22:39 EST, MC, from `server/evidence/2700/verification/`)

Confirmed live on c5h-b at pin 29f15ac38 [L]: Tiny idle frame 590.2 crossings with local answers off, 30.9 and 31.5
with them on (575.8 answered locally per frame); during AI turns 694-696 off, 118-121 on; verify mode 0 mismatches
over 4,510,753 mirror answers and 47,168 `GetScriptSystem` answers; state on equal to off (whole inflated save bodies
and 376 statistics keys; every pair including on/on and off/off differs only at the same 9 noise offsets). The 24
pure getters were re-read at the pin [C]; caveat: `IsOption` with a hashed argument may query the DLL's own database
copy (read-only; not seen by the engine-call counter). No path was found where the mirror returns a stale value.

Open, from reading the code, none observed [C by reading, not L] (*all four closed 2026-11-11 by section 17.1, issue 2801*):
- Canonical handles are not covered by the epoch check: a new generation starts only at the start of Init, Uninit,
  CacheGameDatabaseData and FlushGameDatabaseData, so a handle fetched while one of them runs is kept
  (`proxy_local.cpp` lines 118-158 and 327-342 at 596747d); in the server that can be an info object being rebuilt.
- The proxy half of the lock-want rule has no offline test: two broken variants pass all hutest checks.
- Lock depth and wrapper count can disagree when a local lock wrapper is destroyed on another thread, in the server's
  release loop, and beyond 1000 wrappers.
- The outside-a-served-call counter counts DLL-to-engine calls only, not engine methods the server answers itself.
Why the plain stale variant was not caught live: it produced no stale answer, because other impure crossings move the
epoch around every turn change; verify mode compares every checked answer, so it did not miss one. Weak evidence in
section 16: the headless verify leg checked 47 mirror answers (closed 2026-11-11 by issue 2805: 1,300,212 with
`test_slot_poll=4`, section 17.2); the six frame-rate repetitions mix builds b7 and b11;
the huge-save control pair was verify-on against on. All of it is in the scope of epic 2800.

## 17. Epic 2800: the local answers made safe, bug 0086, AI-turn traffic, frame rate (2026-11-10 22:58 - 2026-11-11, team-2800 coordinator)

Branch `worktree-agent-a11ec86089c9ae67c`; pins x64 c43ea1b9f (`deps/dll.lock`) and i0-ship ccfd6dd4d (`deps/i0.lock`).
Child issues 2801-2805 (workers W1-W5, offline) and the coordinator's live runs on c5h-a. The live build is **B3 = 741325b**
(`server/build_server.py --config release --offline` from an archive of the fork at c43ea1b9f; proxy SHA-256 in
`server/evidence/2800/live/build-identity.txt`). Evidence: `server/evidence/2800/` (`w1`-`w5` per worker, `final-offline`
for the suite on B3, `live` for the live series; each has an `index.txt` or a README line below).

### 17.1 Safety of the local answers (issue 2801; closes findings 1-5 of the verification in 16a)

1. **Generation changes** [C, L-offline]. `ICvGameContext1::Init/Uninit` and `ICvDLLDatabaseUtility1::CacheGameDatabaseData/
   FlushGameDatabaseData` bump the canonical generation at their start AND at their end (proxy `BeforeCall` / `AfterCall`,
   the latter on every return path of `End`). While one of them is in flight on any thread the proxy neither uses, fetches nor
   stores a canonical handle: the getter crosses (a fresh DLL wrapper for that call, as in process). The server brackets the
   same four calls in `OnCall` (`GenChangeEnter/Leave`) and answers `kRt_Canon` with the refusal handle {type 0, id -1} while
   one is served on any thread; its entry waits for a Canon being built (`s_csCanon`). Test: hutest `canon_init_mid_fetch`,
   `canon_after_init_ui`, `canon_after_init_core` (the fake DLL's `Init` replaces the game object half-way through and a second
   engine thread asks meanwhile): **39fa514 FAIL** (tag 1000 kept on both threads), 2d021f3 PASS; each half alone passes
   (mutant PA: proxy half removed, the server refused 1 `kRt_Canon`). Evidence `server/evidence/2800/w1/hut-old-39fa514.txt`,
   `hut-pa.txt`.
2. **The lock-want rule has a two-thread offline test** [L-offline]. The hutest host has a second engine thread bound to the
   core channel (its first call is `GetGame`, as the EXE's). `lock_two_threads_try`: core holds a local `GetGame`, the fake DLL
   on the ui channel calls `TryGameCoreLock` and must get false: **M2 fails** ("ui TryGameCoreLock 1"). `lock_two_threads_release`:
   core takes and drops a local `GetGame` with no later crossing, ui must then get the lock within 3 s: **M1 fails by timeout**
   (3015 ms; the core thread then crosses once, so the suite never hangs). `w1/hut-m1.txt`, `hut-m2.txt`.
3. **Lock-count disagreements fail the run** [C, L-offline, hutest `--scenario`]. A local lock wrapper released on another
   thread, or a thread holding more than 1000: the proxy logs `LOCK ERROR`, exits 7 under `strict=1` and raises word 8 of
   `Local\VPProxyLock_<pid>`, so the server's next served call fails the run. A want word outside 0..1000: the server fails the
   run. A lock-want release that finds the depth already 0 (the `CallTestAll` pattern with the engine dropping the wrapper
   inside the hook): the server fails the run. None can be repaired in place (only the making thread can cross on its channel;
   in process the same patterns would unbalance the EXE's lock [I]). On 39fa514 all six scenarios ended "result: passed"
   silently (`w1/hut-old-39fa514-scenarios.txt`). Also: `CoreLockYield` no longer switches the sync off when it released
   nothing (it stayed off for the rest of the run: the proxy sends no Resume for depth 0); the proxy's `t_E` (write-only since
   e7ea828) is removed. Left on purpose: during a yielded harness Lua chunk a local `GetGame` on the core thread takes no lock
   until the resume (harness-only, one chunk) [I].
4. **D30 condition 4 counter** [C, L headless]. `NoteEngineCall` also runs in every engine method the server answers itself:
   `nextSelectedCitiesNode`, the local enumerator (`srv_remote_human.cpp`), the script system's `CreateLuaThread`,
   `FreeLuaThread`, `CallFunction`, `CallCFunction`. `VP_SERVER_TEST_OUTSIDE=2` (only those, on a thread of its own) fails the
   headless Tiny run with counter 3; mode 1 still fails with counter 2 (`w1/headless/outside1`, `outside2`).
5. **Minor** [C, L-offline]. The verify-mode force-pure flag is taken at the top of `Begin` (it could outlive a `Begin` that
   failed before `CallStart`). `build_proxy.py` step 0 runs `gen_pure_list.py --check` and fails the build when
   `proxy_pure.inc` does not match `pure-methods.tsv` (negative: a scratch copy with `ICvGame1::Update()` marked pure, rc 1,
   `w1/build-proxy-pure-negative.txt`). A build from a copy of this repository without `.git` logs `repo <commit> (archive)`
   (`--repo-commit` or a `.repo-commit` file) or a LOUD `repo unknown (archive: ...)` line (`w1/build-identity.txt`).
6. **Stream sessions** (16.4 item 1 corrected): `kRt_StreamOpen`/`kRt_StreamOpenPull` do not pass `Begin`; they are made
   inside `StreamInArg` while the outer `Read*`/`Write*` call is staged and in flight, so that impure call has already
   invalidated the mirror (verification 2700 A1) [C].

### 17.2 The headless verify leg with a slot poll (issue 2805; closes finding 6's headless leg)

`vpproxy.ini test_slot_poll=N` plays the EXE's slot poll N times at the top of every `ICvGame1::Update` through the proxy's
own wrappers (the 24 mirrored getters for all 64 player slots, 655 calls a pass; the player option through
`ICvPlayer2::IsOptionKey`). It is honoured only with `host=headless` and an EXE whose name does not start with
`CivilizationV`; otherwise it logs `REFUSED (ignored)` (`w5/headless-refused`). Strict headless Tiny 0-50,
`local_answers=1 local_verify=1 test_slot_poll=4`, on the live build B3: **1,300,212 mirror answers verified, 0 mismatches**
(`final-offline/headless-proxy-lines.txt`; the same count on W5's build dc161a9); without the poll 47 (what epic 2700's leg
checked); `stats.db` identical with and without the poll (9,257 rows, `w5/compare-verify-nopoll.txt`). Negative tests with the
poll, B3: `local_test=stale` exit 6 (`VERIFY MISMATCH ICvGame1::GetGameTurn() const: mirror 0, server 1`), `local_test=frozen`
exit 6 (`ICvPlayer1::IsTurnActive() const: mirror 1, server 0`) [L headless].

### 17.3 Bug 0086: empty notification texts (issue 2802)

- [C] The empty texts of 16.5b are notification MESSAGES, not summaries (record layout CvNotifications.cpp:25: the message is a
  u32 length 0 in the x64 save, the summary after it equal in both). They come from `CvGame::GetDiploResponse`
  (CvGame.cpp:5961), which runs `select Tag, Bias from Diplomacy_Responses, Language_en_US ...` on the game database.
  `Language_en_US` exists only in `Localization-Merged.db`, which the EXE attaches to its game database as `Localization`
  (live `PRAGMA database_list`). The server's copy (11.7) had nothing attached, so every diplomacy response was '':
  declaration notifications, AI leader speech (CvDiplomacyAI, 278 uses), trade replies (CvDealAI), culture-bomb complaints,
  `ICvDiplomacyAI1::GetDiploStringForMessage` called by the engine, Lua `Game.GetDiploResponse`. The server's dictionary was
  initialised all along; the other 973 `GetLocalizedText`/`Lookup` sites (names, tooltips, replay messages, summaries) were
  right. *(Corrects 16.5b "the likely cause is the server's localization".)*
- Fix: the proxy reads the engine connection's `PRAGMA database_list`; if a database named `Localization` is attached,
  `SetGameDatabase` sends its file (flag 4). The server attaches it to its copy before the DLL sees the connection
  (`server.cfg loc_attach = engine` (default) | 1 | 0) and, when no `loc_db` is configured, initialises the dictionary from
  that file (the EXE calls `SetGameDatabase` before `Init`). An attach failure fails the call (loud; exit 4 under strict).
- [C] The pick in GetDiploResponse is stateless (`randRangeExclusive` is const, CvGame.cpp:10166): with and without the
  attachment the save bodies differ only by the inserted message and `stats.db` is identical.
- [L offline] The headless hosts attach nothing (the 32-bit headless host's `database_list` has only `main`), so headless proof
  runs keep the empty message, like the 32-bit DLL in that host. With `VPPROXY_TEST_ATTACH_LOC=<file>` (the proxy attaches the
  file to the engine connection first: the EXE's path) a proxy run writes "The Maya announces to the world that it is now
  protecting Genoa. ..." where the old build wrote ''; it equals the direct server with `loc_attach=1` on 16/16 messages;
  `stats.db` identical (5,739 rows). Tool: `server/tools/notif_texts.py SAVE [OTHER]`. Evidence `server/evidence/2800/w2/`.
- Live: 17.7.

### 17.4 Temporary wrappers: item 4 of epic 2700 closed, not needed (issue 2804; MC agreed 2026-11-11)

In process the DLL passes a DLL-object argument of a DLL -> engine call as a fresh wrapper and frees it when the call returns
(`CvInterfacePtr<ICvUnit1> p(new CvDllUnit(..)); DLLUI->X(p.get())`, CvDllUnit.cpp:26-67); the interfaces have no AddRef, so the
EXE can keep the object only through a `QueryInterface` it does not `Destroy` before returning. With `keep_temp=0` the proxy
frees the temporary wrapper at the call's end, so it differs from the in-process DLL exactly when the EXE keeps such a QI.
Read in the EXE (analysis copy `%USERPROFILE%\tools\ghidra-projects\bin\CivilizationV_DX11.exe`, llvm-objdump and a read-only
Ghidra decompile of a scratch copy; addresses only, D22): engine utility vftable 0x9EA718 (x86 slots of `interfaces.json`); UI
as the DLL sees it 0xA04240, 152 adjustor thunks into the primary vftable 0xA3FE68. **Every handler of the 29 such methods
seen in the live traffic of epics 2500 and 2700 (373,550 calls) uses the argument only during the call** [C]:

| Method (vftable slot -> handler) | What the EXE does with the argument | Read |
|---|---|---|
| UI `isCitySelected` (33 -> 0x68D7C0 -> 0x494F10) | city GetIDInfo, compared with the selected-city list | by hand |
| `GameplayUnitShouldDimFlag` (158 -> 0x590250) | unit getters into a queued render message (0x87E980) | by hand |
| UI `SetSpecificCityInfoDirty` (110 -> 0x5D7600 -> 0x4D7390) | city getters into a queued message (0x871F50) | by hand |
| `GameplayPlotStateChange` (173 -> 0x7AAB90) | plot getters; values into messages | triage |
| `GameplayUnitTeleported` (148 -> 0x6FF3B0) | unit and plot getters | triage |
| `GameplayUnitVisibility` (160 -> 0x444040) | unit getters | triage + decompile |
| `GameplayYieldMightHaveChanged` (179 -> 0x406B20) | plot getters | by hand |
| `GameplayUnitFortify` (152 -> 0x619610) | unit getters | triage |
| `GameplayUnitCreated` (146 -> 0x5579C0) | getters; QI(ICvUnit2) at 0x557CF5, one call, Destroy at 0x557D1E, more getters (issue 2708) | by hand |
| `GameplayUnitSetDamage` (151 -> 0x43BEB0) | unit getters into a message (0x87E710) | by hand |
| `GameplayUnitWork` (153 -> 0x564960) | unit getters | triage |
| `GameplayWonderCreated` (189 -> 0x4BFDB0) | plot getters; the plot goes to 0x423DC0, which only calls GetPosition | by hand |
| `GameplayCitySetDamage` (165 -> 0x66F040) | city getters | triage |
| UI `UpdateCountryBorder` (106 -> 0x405380 -> 0x6F2460) | plot getters; values stored | triage |
| UI `RemoveFromSelectionList` (17 -> 0x788070 -> 0x50DAE0) | unit GetIDInfo erased from the IDInfo vector at this+0xA8 | by hand |
| `GameplayUnitDestroyed` (149 -> 0x68F260) | unit getters | triage |
| `GameplayUnitGarrison` (157 -> 0x497CA0) | unit getters | triage |
| `GameplayUnitEmbark` (156 -> 0x408310) | unit getters; values to 0x591930, 0x503880 | triage |
| `GameplayCityCreated` (162 -> 0x58E880) | city getters | triage |
| `GameplayDoFX` (177 -> 0x55CC80) | plot getters | triage + decompile |
| `GameplayWallCreated` (176 -> 0x484E70) | plot getters | triage |
| `GameplayCityPopulationChanged` (166 -> 0x632B10) | city getters | by hand |
| UI `AddDeferredWonderCommand` (133 -> 0x646710 -> 0x54FA00) | city getters | triage |
| `GameplayUnitDestroyedInCombat` (150 -> 0x58C9D0) | unit getters | by hand |
| UI `InsertIntoSelectionList` (25 -> 0x67D100 -> 0x6B1D70) | IsBusy; 0x43D190 appends the unit's IDInfo (8 bytes) to the vector at this+0xA8 | by hand |
| `GameplayFeatureChanged` (172 -> 0x426EF0) | plot getters | triage |
| `GameplayCityCaptured` (164 -> 0x4C13D0) | city getters | triage |
| UI `SetScratchDeal` (8 -> 0x6E7E10 -> 0x575FE0) | GetGame()->GetGameDeals()->GetDeal([this+0x98])->CopyFrom(arg): a DLL call inside the call | by hand |
| `GameplayUnitActivate` (155 -> 0x66D5D0) | unit getters | triage |
| *(added 2026-11-15, ticket 0329; not of the 29)* `GameplayUnitCombat` (161 -> 0x67DD90) | combat-info getters (slots 2, 4-16), values into a stack message queued at 0x87E3D0 / 0x87E4A0 + 0x813D60 (ticket 0316, met live) | by hand |
| *(added 2026-11-15, ticket 0329; not of the 29)* `GameplayCityCombat` (167 -> 0x48CCA0) | combat-info getters (slots 2-14), the same queue | by hand |

"triage" = the argument register is used only as the `this` of its own getters, not pushed to another call, not stored, no
slot-0 call (each flag checked by hand). Also read as borrowing, never seen live: UI `setLastSelectedUnit` (stores the IDInfo
at this+0x1B4), `selectUnit`/`selectGroup`/`selectAll` (pass the argument to `ICvGame1::SelectUnit/Group/All` inside the
call), `selectCity`, `addSelectedCity`, `centerCamera`, `VisuallyDeselectUnit`, `updateCityScreen`, `lookAt`,
`AddHexToUIRange`, `GameplayUnitMoved`, `GameplayCityDestroyed`, `GameplayUnitRebased`. **Not read** (never called live):
~~`sendNetDealAccepted`/`sendNetDemandAccepted` (multiplayer: the deal goes into a stack net message 0x444AF0)~~ *(read and listed 2026-11-15, ticket 0344, below)*, ~~the combat pair
`GameplayUnitCombat`/`GameplayCityCombat` (`ICvCombatInfo1`)~~ *(read and listed 2026-11-15, tickets 0316 and 0329, below)*, spaceship, wonder edit/remove, natural wonder (read and listed 2026-11-13, bug 0124, below), plot icon, unit
animation/mission/threat/paradrop, `DebugCityRoutePlots`, `sendRandomNumberGeneratorSyncCheck`.
*Corrected 2026-11-12 (one-off 9036, bug 0101): the question reopened live. The huge save through the proxy (9034, turns
350-420, both pins) handed DLL objects to four unlisted methods: `GameplayUnitMissionEnd` 14 times,
`GameplayUnitResetAnimationState` 3, `GameplaySpaceshipCreated` 1, `GameplaySpaceshipRemoved` 1 (two runs' vpproxy.log).
Read in the EXE (llvm-objdump of the analysis copy, by hand) together with `GameplaySpaceshipEdited`, all five borrow:
`GameplayUnitMissionEnd` (168 -> 0x633860): unit getters at slots 2, 3, 5-8, 10, 11, 17-19, 25 (vftable offsets 0x8-0x64)
and one plain call 0x65C910 that calls slots 30 and 3 and passes their values to 0x591930; `GameplayUnitResetAnimationState`
(170 -> 0x6F2E30): unit slots 2, 3, 17 and a getter on slot 17's result, values into a queued message (0x87E8B0);
`GameplaySpaceshipCreated` (192 -> 0x551E90), `GameplaySpaceshipEdited` (193 -> 0x5DDE50), `GameplaySpaceshipRemoved`
(194 -> 0x4D8A80): plot slots 15 and 53, values into queued messages (0x5872D0, 0x618650, 0x8802B0). None calls slot 0
(QueryInterface) or 1 (Destroy) on the argument or stores it [C]. The five are in `kBorrowedArgMethods` since 9036; the
hutest `unlisted` scenario now uses `GameplayUnitParadrop` (never seen live, not read). (Replaced: these five in the
"not read" list above.)*
*Live check of 9036's list, 2026-11-13 20:25-21:02 EST (queue ticket 0101, account B) [L]: **0 lines "not in the
borrowed-argument list" and 0 LOUD lines** in `vpproxy.log` (438 lines) over **55 AI turns (350 -> 404)** of the huge save
copy `C5H-0101-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, C499D3C6...), instance c5h-a, on the D33 product path (3308/3311's
set-up: `[Renderer] ServerRenderer = 1`, composite and manifest beside the proxy, D33 with Overlay on, `local_answers=1
local_verify=0 lua_mirror=0`) with the D96 DLL (`x64-04b1211b-pa803776/bin`, 74588135...) and server.exe B20ACEFC... /
proxy 25D273EC... / composite AF1583F4... built from main 7b8f45f7 (repo_dirty=0) on 9045's deps. Every turn step exit 0,
guard checks 0/0/0, start-up watch ok, harness stop 0, restore by hash ok. 9034's runs had 19 such lines in 61 and 70 turns
(old proxy); 9036's b1 had 0 in 30. Also 0 LOUD in server.log (246 lines), render.log (4,285) and shell_composite.log
(3,751). Driver `shell/evidence/0101/run_0101.py`, check `shell/evidence/0101/check_0101.py` (result
`shell/evidence/0101/acceptance.json`); logs `D:/c5-64bit-work/0101/live/out/`. Bug 0101 is done by its definition.*
*Corrected 2026-11-13 (issue 3309, team-3300 W9, bug 0124): `GameplayNaturalWonderRevealed(ICvPlot1*)` borrows its
argument and is in `kBorrowedArgMethods`. Found by team-3300 wave 1: the hutest fake DLL calls it for the feed check
(issue 3303) and the proxy logged it LOUD on every reveal. Read in the EXE (llvm-objdump 19 of the analysis copy, by
hand, Intel syntax): vftable 0x9EA718 slot 175 -> **0x741BB0** (neighbours checked against this table: 173 -> 0x7AAB90,
176 -> 0x484E70, 189 -> 0x4BFDB0, 192 -> 0x551E90). `__thiscall`, one stack argument (`ret 4`). The handler gets an engine
object from a helper (0x42C640) and calls its slot 4 for a team value; with a non-null plot it calls plot slot 40
`IsRevealed(team, false)` and returns unless revealed; then a second engine object from a helper (0x5D1DE0, which reuses
the argument's dead stack slot as its out-pointer; the plot stays in a register), plot slot 15 `GetPosition(&x, &y)`,
a coordinate conversion on a global (0x4E4E80 with ecx 0xD91130, values only), plot slot 53 `GetPlotIndex()`, a message
built on the stack from those values and queued (0x6F2C20, then 0x813D60, on per-context globals 0x2A10E08 / 0x2A10E3C
+ 0x4C * index); the two engine objects are Destroyed (slot 1) before `ret`. The plot is only the `this` of its own
getters (slots 40, 15, 53): no slot 0 (QueryInterface), no slot 1 (Destroy), never stored, never passed to another call
[C]. Test: `proxy/tools/rt_tests/check_borrowed_nwr.py --run <hutest out>/run` after a normal hutest run (the call
crossed the proxy, no NaturalWonderRevealed LOUD line, the shutdown count equals the LOUD lines). Results [L,
2026-11-13 11:54-11:55 EST, offline]: build `D:/c5-64bit-work/3300/w9/b1` (proxy SHA-256 9a3bccff...be3e): hutest PASS
(46 checks, 0 failed), checker PASS (1 call through the proxy, 0 NaturalWonderRevealed LOUD lines; the one LOUD line
left is `GameplayUnitCombat`, expected), `--scenario all` 8/8 as expected; the same hutest binaries with the unfixed
wave-2 proxy (`D:/c5-64bit-work/3300/w6/b-b1`, 1a4d44e5...2d2d): hutest PASS (it tolerates this line), checker FAIL,
exit 1 (`LOUD: engine method ICvEngineUtility1::GameplayNaturalWonderRevealed(ICvPlot1 *) received a DLL object`).
Log `D:/c5-64bit-work/3300/w9/hut.txt`. No live reveal was run. (Replaced: "natural wonder" in the "not read" list above.)*
*Corrected 2026-11-15 (ticket 0329, from 0316): the combat pair `GameplayUnitCombat(ICvCombatInfo1*)` (161 -> 0x67DD90) and
`GameplayCityCombat(ICvCombatInfo1*)` (167 -> 0x48CCA0) borrow their argument and are in `kBorrowedArgMethods` (commit
18371d8c). Ticket 0316 met `GameplayUnitCombat` live (every visualised combat, Quick Combat off, one LOUD line each) and read
its handler; 0329 read the city handler the same way: the argument is loaded once into a callee-saved register and is only
the `this` of its own getters (unit: slots 2, 4-16; city: slots 2-14), no slot 0 or 1, never stored or passed on; the values
go into a stack message queued at 0x87E3D0 / 0x87E4A0 + 0x813D60, the return value is the queue's event id [C]. Detail,
the DLL's callers and the test (`test_proxy_rt.py` section 6): `combat-on-the-product-path.md` 4 and 4.1; the live check of
both: same file, section 6. (Replaced: the combat pair in the "not read" list above.)*
*Corrected 2026-11-15 (ticket 0344, from 0322): the deal sends `sendNetDealAccepted(PlayerTypes, PlayerTypes, ICvDeal1*,
int, int, int)` (90 -> **0x476D90**) and `sendNetDemandAccepted(PlayerTypes, PlayerTypes, ICvDeal1*)` (91 -> **0x77DB50**)
borrow their deal argument and are in `kBorrowedArgMethods`. Ticket 0322 met `sendNetDealAccepted` live (26.3, 26.5: one LOUD
line per accepted deal). Read by hand with llvm-objdump 19 (Intel syntax) on the analysis copy, addresses only (D83); the
slots read from the vftable 0x9EA718 in the same file (89 -> 0x714010, 90 -> 0x476D90, 91 -> 0x77DB50) [C]:*
- *`0x476D90` (`ret 0x18`, 23 instructions): builds a 0x48-byte net message on its own stack with the constructor
  `0x444AF0(from, to, deal, value, mine, theirs)`, passes its address to the send `0x445E30`, then runs its destructor
  `0x42F7B0` and returns. The deal is touched nowhere else in the function.*
- *The constructor `0x444AF0` (`ret 0x18`): base `0x7DA6B0(0x75)` (message type 0x75), vtable 0xA9077C, the two players at
  +0x30/+0x34, the three ints at +0x38/+0x3C/+0x40, +0x44 = -1. Then the EXE's own copy of the deal:
  `0x42C640` (calls the function pointer at 0x2DD201C, the DLL's game context `[I: DllGetGameContext]`, then its slot 8
  `GetGame`, and Destroys the context) -> `ICvGame1` slot 20
  `GetGameDeals` -> `ICvGameDeals1` slot 13 `CreateDeal()`, the id stored at +0x44; slot 14 `GetDeal(id)` -> that deal's
  slot 8 `CopyFrom(argument)`; the returned deal and the deals object are Destroyed (slot 1). **The argument is only the
  parameter of `CopyFrom`, a DLL call inside the call: no slot 0 (QueryInterface), no slot 1 (Destroy) on it, never
  stored** (the message keeps the id of the copy, not the pointer).*
- *The send `0x445E30` (cdecl, the message's address): hands it to the net object at `[0x2DE0454]` (`0x5E88C0`); the
  message's serializer (vtable 0xA9077C slot 2, `0x41FE10`) writes the five values and then `GetDeal([+0x44])->Write(stream)`,
  i.e. the copy, not the argument. The destructor `0x42F7B0` calls `GetGameDeals()->DestroyDeal([+0x44])` (slot 15).*
- *`sendNetDemandAccepted` `0x77DB50` (`ret 0xC`) is the same pattern: constructor `0x788D20` (type 0x76, vtable 0xA90790,
  players at +0x30/+0x34, the copy's id at +0x38, `CreateDeal`, `GetDeal(id)->CopyFrom(argument)`), the same send
  `0x445E30`, destructor `0x6E6960` (`DestroyDeal([+0x38])`). Never called live.*
*So both sends borrow their argument for the call only: what the EXE keeps and sends is a DLL deal of its own, created and
destroyed through `ICvGameDeals1`. This is what 0322's run showed from the outside (26.5: `Write` 1, then `Read`,
`CreateDeal` and `DoAcceptedDeal` on the receiving side). Since ticket 0344 both are in `kBorrowedArgMethods`
(`"ICvEngineUtility1::sendNetDealAccepted("`, `"ICvEngineUtility1::sendNetDemandAccepted("`). Test:
`proxy/tools/test_proxy_rt.py` section 6 (`--only-borrowed`) checks both keys like 0329's; the live check is below.
(Replaced: the two sends in the "not read" list above.)*
*Live check of 0344's list, 2026-11-15 07:13-07:18 EST [L]: on the D33 product path (3308's set-up, as 0329), instance
c5h-d, Tiny copy `C5H-0344-TINY_0018` of `C5H-3000-TINY-T0018` (Spain, turn 18), build from 663819c8 (proxy D7C0F5A4...,
server.exe 64297779..., x64 DLL 74588135... = the pin): the human offered William a 50-gold gift through his trade screen
(an in-game handler armed in LeaderHeadRoot pressed Trade, filled the scratch deal and called `UI.DoProposeDeal()` as
DiploTrade's Propose button does, then closed the screens with their own functions; the Lua channel is silent while the
leader screen holds the game). The AI accepted (`CvDealAI::DoHumanOfferDealToThisAI`, gift), sent `sendNetDealAccepted`
once; the EXE answered with `DoAcceptedDeal` once; the gold moved 278 / 55 -> 228 / 105. proxy-calls: `CopyFrom` 2,
`Write` 1, `Read` 1, `CreateDeal` 3, `DestroyDeal` 2, as the reading predicts. vpproxy.log: **0 "not in the
borrowed-argument list"**, 0 "unknown handle", 0 "not a live wrapper", 0 LOUD; the turn ended. Driver
`shell/evidence/0344/run_0344.py`, check `check_0344.py` (PASS 9/9; with 0322's tiny150 vpproxy.log it fails on the
line, exit 1), evidence `shell/evidence/0344/acceptance.txt` and `data/`. Not run live: `sendNetDemandAccepted`.*
*Driving note for an accepted deal [L, 2026-11-15]: `UI.DoProposeDeal()` with the human as from-player works from an
`Events.LocalMachineAppUpdate` handler while DiploTrade is shown after the root's own `OnTrade()`; after the AI accepts,
DiploTrade hides itself within 3 s and the leader root stays up until its `OnClose()`. The harness's `try_lua` raises
`LuaTimeout` (it does not return an error) while the screen holds the game: a poll loop must catch it.*
Live logs: 94 vpproxy/server
logs of epics 2500/2700, 0 "not a live wrapper", 0 "unknown handle" [L, supporting]. Evidence
`server/evidence/2800/w4/temp-wrapper-verdicts.txt`. Decision: `keep_temp` stays 0 for good; fc0f85d is not run live.

**What reopens the question:** a DLL object handed to an engine method outside the borrowed list above. The proxy now logs that
LOUD (`LOUD: engine method X received a DLL object ... is not in the borrowed-argument list`, first 20, then counted) and states
the count at shutdown (`temporary wrappers: DLL objects handed to engine methods outside the borrowed-argument list N`);
`ProxyRt::WrapTemp` checks the innermost served engine method against `kBorrowedArgMethods` in `proxy/src/proxy_rt.cpp`.
Tests: hutest scenario `unlisted` (the fake DLL hands a unit to `GameplayUnitMissionEnd`; since 9036 to `GameplayUnitParadrop`): LOUD line present on B3, absent and
FAIL with the proxy of B2 (`final-offline/hutest-unlisted-on-B2-old-proxy.txt`); the normal hutest run tolerates only the
fake DLL's own `GameplayUnitCombat` line. **Open [I]:** the combat pair was never called in any live run (AI combat is not
animated in these runs); a human combat with animation will call `GameplayUnitCombat` and log LOUD until its handler is read.
*Corrected 2026-11-15 (queue ticket 0316): called live for the first time. Three human-visible combats with Quick Combat off on the D33 product path called `GameplayUnitCombat` and logged this LOUD line once each; the EXE read the combat info only inside the calls (0 "unknown handle" / "not a live wrapper"). Its handler (slot 161 -> 0x67DD90) was read by hand and borrows its argument [C]: `combat-on-the-product-path.md` section 4. Adding it to `kBorrowedArgMethods` is proposed to the MC; `GameplayCityCombat` (slot 167 -> 0x48CCA0) is still not called or read.* *(Done 2026-11-15, ticket 0329: `GameplayCityCombat` read (borrows) and both added to the list, so neither logs this line any more; the 17.4 correction of 0329 above. The normal hutest run's tolerance of the fake DLL's `GameplayUnitCombat` line is now stale, `build_hutest.py`, not changed by 0329.)* *(Removed 2026-11-15 by ticket 0344: the normal hutest run now fails on any "not in the borrowed-argument list" line; it passed on 0344's build, 47 checks, 0 such lines.)*
*Also 2026-11-15 (issue 0318) [L]: with the Combat, Particle and Projectile keys on (a test key list) the same line came
13 times in one Tiny run of 10 AI turns (2 human attacks, 11 animated AI combats) and 2 times in a second run, with no
crash or stall; with 0316's reading of the handler (borrows, nothing kept) the line is harmless there too.
`02-exe-internals/disable-systems-and-masks.md` 5, "The beta's gap alternative".*

### 17.5 Purity audit at c43ea1b9f; test_proxy_rt (issue 2804)

- [C] The only net source change 29f15ac38 -> c43ea1b9f is `SqliteLogger::EnsureOpen` (D35 pragmas); 40ce81aee (D18 by
  cherry-pick) brings no net change. All 292 DLL-source citations of the pure, neutral and conditional rows of
  `pure-methods.tsv` are in files unchanged since 29f15ac38; against e9d0c4837 (where the TSV was written) 22 line numbers moved
  with identical text (e.g. HasTurnTimerExpired CvPlayer.cpp:49274 -> 49368). The source of the 24 mirrored getters,
  HasTurnTimerExpired and UpdateTestEndTurn is byte-identical to the 29f15ac38 re-read. **No verdict changes.** Tool
  `server/evidence/2800/w4/tools/cite_check.py`; `w4/purity-audit-c43ea1b9f.txt`.
- [L offline] The two failing "strict" checks of `test_proxy_rt.py` were stale: they used `ICvGameContext1::GetCityPlotX()` as
  an unimplemented method, implemented since issue 2503. They now reach a loud default through
  `GetWorldBuilderMapLoader()->LoadModData`, and a new check reads `proxy_hook_defaults.gen.cpp` to prove that method is still a
  loud default, so the checks cannot go stale silently. The loader cases now run on a hidden desktop (COMMON rule 14). B3:
  55/55 (`final-offline/test_proxy_rt.txt`).
- [L offline, 2026-11-13, bug 0128, one-off 9045] **After bug 0123 the test failed 42/55** on main's server and proxy: the
  proxy's default `loc_db` is now the game's cache `Localization-Merged.db` beside the player's `config.ini`, so the server
  starts its localization from the game folder's `Locale/icudt44l.dat`; the test's fake game folder (the loader's) has none,
  `LocStartup` fails, and `SrvRemote::ReportAndExit` before `SrvRemote::Init` crashes (access violation under
  `WriteReport` -> `UnservedCount()`; stack in `tracker/issues/0128-*.md`; server side for team 3300). Every case that boots
  the real server then waited 60 s for the attach (472 s in all). Fix on the test side: every `mode=on` vpproxy.ini the test
  writes names `loc_db=none` (`ini_text()`), and a new check reads it back from server.cfg. Build `D:/c5-64bit-work/9045/bF`
  (server/proxy of repository a803776cf): **56/56, 46 s**. The test no longer depends on the player's cache or language.
  Note: the proxy still reads the player's real `config.ini` (language, logging) for the server.cfg it writes in the test.

### 17.5a Purity audit at the pin 76992d235 (issue 0111, bugfix team 4000, 2026-11-13)

- [C] Source c43ea1b9f -> 76992d235 (upstream fix batch 1 and the port; `upstream-fix-batch-1.md`): 24 files changed
  (298+/153-), 48 functions in the diff hunks (trade-route counts and trade stats, unit creation and purchase, espionage
  AI and its loader, tactical assignments, real population as `long long`, `CvPlot::updateWaterFlags`, script-data
  free, research turns left, `GetUnitNames`, deal history, timers, `updateMoves`, `LogMapState`, ...).
- [C] **`pure-methods.tsv`** (pure, neutral and conditional rows): 302 DLL-source citations; 232 in unchanged files, 53 in
  changed files with identical text at the same lines, 17 moved with identical text (e.g. `IsHuman` CvPlayer.cpp
  9776-9830 -> 9779-9833, `GetActiveFogOfWarMode` CvPlot.cpp 479-497 -> 480-498, `UpdateLayout` 13911-14052 ->
  13917-14058), 0 changed; the 10 others cite this repository's server files (`srv_*`, `vp_wire.h`), not the DLL.
- [C] **`lua-pure-bindings.tsv`** (the UI-Lua epoch mirror's pure rows, written at c43ea1b9f): 81 citations; 45 unchanged
  files, 19 identical, 17 moved with identical text (`CvLuaCity.cpp`/`CvLuaPlayer.cpp` +2 lines, `IsFranchised`'s
  CvCity.cpp chain +1/+11), 0 changed.
- [C] **No cited line calls a changed function** (name check of the call names in every cited range against the 48
  changed functions: 0 hits in both files). So no verdict changes and no new lazy cache is reached.
- [C] Worth knowing for later extensions: the trade-route count getters (`CvPlayerTrade::GetNumberOf*TradeRoutes`) now
  rebuild a cached `SPlayerTradeStats`, with a separate copy for the UI thread (`m_tradeStatsUi`, CvTradeClasses.h); a
  read from the UI is therefore a write and must stay impure if such a binding is ever listed. Neither list has one.
- Not done: a full re-read of every chain (the method is the 17.5 one: citations plus callees); the 24 mirrored getters,
  `HasTurnTimerExpired` and `UpdateTestEndTurn` were covered by their citations (all identical or moved).
- Tools: `server/evidence/2800/w4/tools/cite_check.py` (issue 2804, `pure-methods.tsv`), extended for any TSV and bare
  ranges as `evidence/4000/0111_cite_check.py`; `evidence/4000/0111_callee_check.py`. Outputs (not kept in the repository):
  `D:/c5-64bit-work/4000/0111/`.

### 17.5b The 4 rows given up at the first beta's pin, re-audited by hand at 1b87dd185 (ticket 0628, 2026-11-24)

Ticket 0616 (2026-11-20) marked 4 rows `not-audited` at the pin x64-0.1b `1b87dd185`, because the derivation dropped them
there (`release-builds.md` 5.4, 6.4). This section re-audits them against the fork at `1b87dd185` (read with `git show` from
a `--shared` clone of the fork, `C:/c5-64bit-work/0628/fork`). **Verdict: all 4 are `pure` again.** Each row's evidence
column now cites the pin's own lines and starts with `[cited at 1b87dd185]` (the derivation's new per-row marker, below).

- [C] **Why they dropped** (derive with the 76992d235 baseline, `3f661ca65^`, on the shared pin source
  `C:/c5-64bit-work/keep/pins-0.1b/src-x64-1b87dd185`, 2026-11-24 15:5x): `ICvPlayer1::GetUnit(int)`, `Plot.GetPlotCity`
  and `Player.GetCapitalCity` only for "callee Get: 5 definition(s) at the audit, 7 now". `ICvUnit1::GetFacingDirection(bool)
  const` for "CvDllUnit.cpp:154-157 the cited text changed" and its own name's body. Every other callee of the 4 rows has
  the same definitions at the pin as at 76992d235 (`getCity` 2, `getUnit` 3, `getCapitalCity` 1, `getPlotCity` 1,
  `getFacingDirection` 1, `Push` 4: all audit hashes present, none new). The 5 audit definitions of `Get` are all
  still there unchanged.
- [C] **The two new `Get` definitions cannot be reached** from the cited calls. They are `VP64Danger::(anonymous
  namespace)::Get()` (`VP64SpeedDanger.inl:165`, `inline State* Get() { return s_pState; }`; the `.inl` is included
  only by `CvDangerPlots.cpp:1039`, so it is internal to that translation unit) and `vp64speed::Log::Get()`
  (`VP64SpeedVerify.h:74`, a static member of class `Log`). Both take no argument. The cited calls are
  `m_units.Get(iID)` (`CvPlayer.cpp:42094`) and `m_cities.Get(iID)` (`CvPlayer.cpp:41709`), member calls on
  `TContainer<CvUnit> m_units` / `TContainer<CvCityAI> m_cities` (`CvPlayer.h:3723-3724`). C++ looks such a call up only in
  `TContainer<T>`, whose `Get(int) const` (`TContainer.h:103-112`, an `unordered_map::find`, const) is unchanged.
- [C] **`ICvPlayer1::GetUnit(int)`**: `CvDllPlayer.cpp:286-295` -> `CvPlayer::getUnit` (`CvPlayer.cpp:42091-42096`) ->
  `TContainer::Get` -> `new CvDllUnit(pkUnit)` (`CvDllUnit.cpp:15-24`: reference count 1, the pointer, an assert; no lock).
  The same text as at 76992d235. It allocates the wrapper, as before. `pure`.
- [C] **`ICvUnit1::GetFacingDirection(bool) const`** at the pin (`CvDllUnit.cpp:154-163`): with `VP64_HIDDEN_EMBARK`
  (given by `tools/x64-dll/build.py` whenever the source has `VP64HiddenEmbark.h`, so in every x64 build of the pin),
  `true` returns `CvUnit::getFacingDirection(true)` (`CvUnit.cpp:25362-25373`: always `NO_DIRECTION`), and `false` returns
  `VP64HiddenEmbark::VisualFacing(m_pUnit, m_pUnit->getFacingDirection(false))`. `VisualFacing`
  (`VP64HiddenEmbark.inl:404-418`):
  - reads `s_lFacingCount`, `s_bTrace`, `s_bDead` without the lock and returns the facing unchanged when no override
    exists (the common case);
  - otherwise it enters the tables' critical section (`Guard`, lines 40-44; a `CRITICAL_SECTION`, re-entrant on one
    thread), looks the unit's key (`Key`, line 70: owner and id) up in `s_facing` and returns the override if there is
    one. Through `getOwner`, `GetID` and `isEmbarked` (`CvUnit.h` 1848-1851, 1314-1317, 644-647: inline member reads);
  - writes only when the diagnostic trace is on (`VP64_HIDDEN_EMBARK_TRACE=1` with `VP64_HIDDEN_EMBARK_DIR` set in the
    server's environment; `Trace`, lines 155-162: the trace's own `s_traced` map and one CSV line). That is no game
    state and no input of a mirrored getter (16.1); so the row would be `neutral` at worst with the trace on. It is not
    on in any product or test configuration we ship.
  - It makes no DLL -> engine call. `s_facing` changes only inside the hidden-embark hooks (`Flush`, `OnMove`,
    `OnUnitGone`, `Clear`), which run inside impure game-state crossings (moves, embarks, combat, turn end, load).
  So the call reads only; `pure`. The answer for `false` can now differ from `m_eFacingDirection` (the facing of the
  unit's last held-back embark call, by design: `research/08-performance/hidden-embarks.md` 7.3). The proxy never keeps
  this method's answer (it is not one of the 24 mirrored getters), so the verdict only decides whether the call moves
  the epoch.
- [C] **The server's unit feed** (`server/src/remote/srv_remote_feed.cpp` `ReadUnit`, `r.facing =
  u->GetFacingDirection(false)`, on the DLL's thread inside a served call) **still makes a pure call**: it changes no DLL
  state and calls no engine method. When that read happens inside a hook that holds the hidden-embark lock (a flush
  posts `GameplayUnitEmbark`, the EXE's handler and the feed read the facing on the same thread, 7.3 of
  `hidden-embarks.md`), the re-entrant critical section does not block. What changed is the value: the feed now
  records the visual facing (the override while one exists), which is the facing the EXE is told and stock shows. Game
  identity does not depend on it: the DLL's own code never calls the wrapper, and `getFacingDirection(true)` (sight)
  is always `NO_DIRECTION` (`hidden-embarks.md` 7.3: off against on IDENTICAL, 39,257 rows).
- [C] **`Plot.GetPlotCity`**: `Lua/CvLuaPlot.cpp:1703-1709` -> `CvPlot::getPlotCity` (`CvPlot.cpp:9826-9832`) ->
  `CvPlayer::getCity` (`CvPlayer.cpp:41707-41710`) -> `TContainer::Get` -> `CvLuaCity::Push` (scratch Lua state only).
  **`Player.GetCapitalCity`**: `Lua/CvLuaPlayer.cpp:8680-8687` -> `CvPlayer::getCapitalCity` (`CvPlayer.cpp:31751-31754`)
  -> the same `getCity` chain. Same text as at the audit; `pure`.
- **The derivation** (`proxy/tools/derive_purity.py`, ticket 0628):
  - per-row citation commit: an evidence column may start with `[cited at <commit>]` (7 to 40 lower-case hex digits).
    `--make-baseline` then reads that row's citations at that commit instead of its list's `cited_at` (e9d0c4837 for
    `pure-methods.tsv`, c43ea1b9f for `lua-pure-bindings.tsv`), and records `cited_at` in the row's baseline entry. A
    malformed marker or two markers refuse (exit 2). Without it a re-audited row could not cite text that did not
    exist at its list's commit (the facing hook's lines);
  - file names in citations may now hold digits after the first character. Before, `VP64HiddenEmbark.inl:404-418` was
    read as `HiddenEmbark.inl` and went unresolved. The other 165 rows' baseline entries are byte-identical in
    citations, callees and definitions after the change (compared entry by entry, `C:/c5-64bit-work/0628/cmp_baselines.py`).
- [L] **Re-baseline**: `python -I -X utf8 proxy/tools/derive_purity.py --make-baseline --fork C:/c5-64bit-work/0628/fork`:
  169 entries (140 method, 29 lua), 425 citations, 406 callee names, 0 unresolved (was 165 entries). `derive_purity.py --src
  <shared pin source> --dll <shared deps DLL>`: 169 kept, 0 dropped. `gen_pure_list.py --check`: the lists hold.
  `proxy/tests/test_purity_drops.py`: 18 passed (the check "the given-up rows are not kept" became "the re-audited rows are
  kept"; new: the marker and its refusals, digits in file names).

#### The name-based callee check is coarse: a narrower check, proposed only (not changed; the MC decides)

The check binds a row to every definition of every name its cited lines call, anywhere in the DLL source. So the 3 `Get`
rows dropped because two unrelated functions named `Get` appeared in other translation units; with the re-baseline they
are now bound to all 7 `Get` definitions, and any new `Get` anywhere in the DLL drops them again at the next pin. Three
narrowings, from the cheapest:

1. **A callee whose definition the row cites is checked by that citation.** `TContainer.h:103-112` is cited, so its text
   is already hashed. Then bind the name only to the other definitions in the same file (a new overload in `TContainer`
   could change which one the call picks), not to every file. Needs a per-callee note (e.g. `Get=TContainer.h:103-112`)
   or a rule "a cited range that defines the name". Removes the cross-file false drops for every chain that cites its
   callee's body.
2. **Arity.** Record each call site's argument count. Bind only definitions whose parameter count, with defaults,
   admits it. `Get(iID)` would skip both new zero-parameter `Get()`s and `CvSparseIDInfoGrid::Get(int, int)`. Easy with
   the existing brace scan (the parameter list is already found). Not enough alone: same-arity names in other classes
   stay bound.
3. **Translation unit.** Do not bind a definition in an anonymous namespace (or `static` at file scope) to rows that do
   not cite a file of its translation unit. That would have excluded `VP64Danger::(anonymous)::Get()`, but not the static
   member `vp64speed::Log::Get()`.

Recommendation: (1) with (2). Both stay conservative: the drop is still the safe direction, and a human still re-audits a
dropped row.

#### Cost of the 4 rows while they were given up (2026-11-20 to this merge): not measured

Not measured, because it needs a live game (the EXE drives these calls) and this ticket had headless places only.
What the mechanism says [I]:
- **Lua rows: no cost in the product.** With `lua_mirror=0` (the product's default, `proxy_rt_logic.h`), every Lua call
  crosses and moves the epoch whether or not the function is pure-listed; purity changes only the shadow counters
  (17.6). With `lua_mirror=1` (the release offline verify leg) the two object-returning calls cross either way but move
  the epoch when not pure-listed.
- **`GetFacingDirection`: next to no cost.** The EXE reads it about once per `GameplayUnitEmbark` call, inside that call
  (`hidden-embarks.md` 7.3: 255 of 257 reads), so an impure call is already in flight and the mirror answers nothing at
  that moment anyway.
- **`ICvPlayer1::GetUnit(int)`: probably large at idle.** The EXE calls it about once per idle frame: 0.98 per frame on
  Tiny (`server/evidence/2700/live7/T2-on/vpproxy-snapshot-methods.tsv`, 20,033 frames) and 0.96 per frame on the huge save
  (`live10/H1-on`; both 2026-11-10, epic 2700 builds). Without its row each of those calls moves the epoch, so the slot
  poll's mirrored reads (`IsHuman`, `slotStatus` and the rest: about 125 local answers per idle Tiny frame there) would
  miss once per frame, as in the first live run of 16.1 when `UpdateTestEndTurn` moved the epoch every frame: 170
  crossings per idle frame against 31 after the fix. So the first beta's builds (purity.tsv without the 4 rows) may have
  lost most of the idle-frame mirror. **Not verified**: whether another impure per-frame call already moves the epoch at
  the pin, and the real numbers.
- **How to measure (about 30 min with a game place):** on the first beta's layout, two arms differing only in the
  `purity.tsv` beside the x64 DLL: the shipped one (162 rows) and one derived from this branch's lists for the same DLL
  (169 rows; `derive_purity.py --src <pin source> --dll <the layout's x64 DLL> --out <dir>`). Same save, idle 60 s plus 5
  AI turns; read the proxy's `local:` line (mirror hits and misses) and `vpproxy-snapshot-methods.tsv` per frame.

### 17.6 UI-Lua traffic per function, and the UI-Lua epoch mirror (issue 2805)

- The server counts every `kRt_LuaCall` and `kRt_LuaIndex` per DLL Lua function, channel and phase and writes
  `proxy-lua-calls.tsv` beside `proxy-calls.tsv` (also on peer death). Names are the Lua paths the DLL registered
  (`LuaTypes.<Type>.<key>` as `<Type>.<key>`, others `Game.X`). Phase = what the proxy saw before the call, sent in bits 30-31
  of the nargs word: `open` (no impure call in flight: an epoch-mirror answer would be possible), `in_flight` (an impure call
  ran, e.g. an AI turn inside `Update`), `unknown` (local answers off). Report: `proxy/tools/calls_report.py --lua <dir>`.
- *Corrected 2026-11-11 (the first design note of this section assumed AI-turn Lua runs inside `Update`; the live count
  says otherwise, 16.7 correction):* **the UI-Lua epoch mirror** (D21 option C under D30; issue 2805 part 2, W5, merged
  6288ee9). A `kRt_LuaCall` is a candidate when its callee is a DLL C function marked `pure` in
  `proxy/model/lua-pure-bindings.tsv` (29 pure rows with file:line evidence at c43ea1b9f; purity is per C function, so an
  alias of a pure name is pure), every argument is nil, boolean, number, string or an instance table, and the key is the
  function id + the encoded arguments (an instance encodes as type + server pointer; a pointer is reused only after a
  deletion, which moves the epoch). A reply is kept only when it is status 0 with all-scalar values; object results
  (`Map.GetPlot`, `Plot.GetPlotCity`, `Player.GetCapitalCity`) cross every time but, as pure calls, do not move the epoch
  (`ForcePureNext`). Fill bracketed (epoch, in-flight count, the server's engine word: D30 conditions 2 and 3); a hit needs an
  entry of the current epoch with nothing in flight; the table is emptied at every generation change and at 200,000 entries.
  `vpproxy.ini lua_mirror=0|1`, **default 0** (shadow: every call crosses as before and the would-hits are counted, column
  `would_hit` of `proxy-lua-calls.tsv`, proxy log `local: UI-Lua mirror (...)`); `local_verify=1` compares every Lua hit byte
  for byte (exit 6 under strict). Negative test `local_test=luastale` (Lua entries ignore the epoch). Headless, the host's own
  Lua is the map script and runs in flight, so `test_slot_poll` has a Lua leg (pure-listed reads only, through
  `ProxyLuaExec::RunTestChunk`). Strict headless Tiny 0-50 with `lua_mirror=1 local_verify=1 test_slot_poll=4`:
  **1,198,941 Lua hits verified, 0 mismatches** (getter mirror 1,300,212, 0); `luastale`, `stale`, `frozen` exit 6; stats.db
  identical to local answers off [L headless, `server/evidence/2800/w5/mirror/`]. Left crossing (exactness not shown):
  `Game.CanHandleAction` (engine state), `Plot.GetUnit` (an engine call in multiplayer), `Deal.IsPossibleToTradeItem`,
  `Player.GetNumResourceAvailable`, `Plot.GetBestDefender`, `Unit.CanEverRangeStrikeAt` (not read through), and every
  function not in the list. Live: 17.7.

### 17.7 Live results (c5h-a, 2026-11-11) [L]

Builds: **B3 741325b** (00:09-01:00: T-verify, H-verify, the four huge arms H-on-a, H-off, H-B, H-on-b), **B6 89084ac** (from
01:28: the Lua-mirror arms and every frame-rate repetition; B6 = B3 + the server's log-request line, the UI-Lua mirror
and the launch gates). B5 (6288ee9) is withdrawn: W5 had left the server half of the Lua mirror uncommitted, so the server
rejected every Lua call with bit 29 set as "bad arguments" (live T-luaverify on B5, 01:05; the verifying reads failed and
`verified 0` was logged; fixed in 57f9290). Every arm by `proxy/tools/live/arm.py` (recipe section 10); the guard veto was ok
on every thread and the start-up watch ok in every launch (`arm.json` `guard_ok`, `startup_watch_ok`). Saves:
`c5h-c5h-a-t0008` (Tiny turn 8) and `C5H-VP8P-HUGE_0350`. Logging: `config.ini LoggingEnabled = 1` in the B3 arms (the EXE
passes it to whichever DLL runs: one `SetLogging`/`SetAILogging`... crossing per run; the server wrote only its
always-written logs, `dll_logs=0`), `LoggingEnabled = 0` in every B6 arm (server report: the DLL asked for 7 log files,
none of them an AI log; 17.9).

**Verify mode** (`local_verify=1`; 0 lock errors, 0 DLL calls outside a served call, 0 canonical refusals in every run):

| Run | Getter-mirror answers verified | GetScriptSystem verified | Lua-mirror hits verified | Mismatches |
|---|---|---|---|---|
| T-verify, Tiny 10 AI turns + checklist, B3 | 4,761,498 | 48,796 | - | 0 |
| H-verify, huge 10 AI turns 350 -> 360 + checklist (pass), B3 | 2,433,107 | 1,049,059 | - | 0 |
| T-luaverify2, Tiny 10 AI turns, `lua_mirror=1`, B6 | 5,390,391 | 48,859 | 2,814 (of 106,375 candidate calls) | 0 |
| H-luaverify2, huge, `lua_mirror=1`, B6, 01:48 | crashed at load (17.10) | - | - | - |
| R1-H-luaverify2, huge 10 AI turns 350 -> 360 + checklist (pass), `lua_mirror=1`, B6, 05:43 | 2,727,371 | 1,048,555 | 257,021 (of 3,814,398 candidate calls, 6.7 %) | 0 |
| R2-H-luaverify2, the same, 05:53 | 2,842,581 | 1,048,021 | 256,151 (of 3,794,436, 6.8 %) | 0 |

**AI-turn traffic, huge save** (server per-turn counters, turns 351-355, mean crossings per AI turn, both directions;
`proxy/tools/live/turn_traffic.py`, `live/turn-traffic-*.txt`):

| | 2700 off (H1-off) | 2700 on (H1-on) | 2800 off (H-off, B3) | 2800 on (H-on-a, B3) |
|---|---|---|---|---|
| all crossings | 2.00 M | 1.41 M | 2.01 M | 1.39 M |
| runtime (UI-Lua thunks, lock syncs) | 0.75 M | 0.82 M | 0.75 M | 0.79 M |
| ICvPlot1 | 0.124 M | 0.120 M | 0.135 M | 0.133 M |
| ICvEngineScriptSystem1 + Args1 (GameEvents) | 0.177 M | 0.178 M | 0.178 M | 0.178 M |
| slot poll (ICvPlayer3 + ICvPreGame2 + ICvGame2 + ICvGameContext3) | 0.65 M | 0.12 M | 0.65 M | 0.12 M |

What remains per huge AI turn and why: ~0.8 M UI-Lua thunk calls (17.6: 98% run while the mirror is open, but only half of
them are pure-listed functions, 48.7% of calls, and the other half move the epoch between them, so the exact epoch mirror hits
few: Tiny 2,814 hits of 106,375 candidate calls, 2.6 %; the AI-turn traffic with `lua_mirror=1` on the huge save was not
measured, H-lua-on was dropped after the 01:52 crash); 0.18 M GameEvents argument calls (each is a new engine object or an engine-side effect: not cacheable
under D30); 0.13 M plot reads (plots are not canonical; a plot mirror needs per-object invalidation, not shown exact);
0.12 M slot-poll residue (mirror misses while `Update` is in flight, D30's limit). Idle huge frame (H-on-a, B3): 62.1
crossings per frame, 16 of them `rt:LockSync` (8 lock sessions), 586.8 answered locally; a frame during the AI turns: 926.7
crossings (539 `rt:LuaCall`, 47.7 `rt:LockSync`), 670 local. *(Note 2026-11-11, verification 9028: these two per-frame
figures count proxy -> server calls only; with server -> proxy calls, as the 1.39 M per AI turn counts them, H-on-a has
63.5 per idle frame and 1,104 per AI-turn frame. Recounted on B6 by the verifier: 57.6-61.6 per idle huge frame both
directions, 1.42 M per AI turn; `verification-2800.md` section 6.)*

**Lock sessions.** Not reduced in this epic: the 0 -> 1 and -> 0 transitions must cross so that the lock is held and free
exactly when the in-process DLL would hold it (16.1); fewer sessions need a lazy release (the ui server thread keeps the
lock after -> 0 and gives it up only when another channel asks for it), a change to the serve loop of the server and to the
two-thread lock design proven in 17.1. Design only [I]; not built.

**Equality** (`live/equality-huge.txt`): huge, 6 AI turns 350 -> 356, saves at 356. Control on/on: 183 bytes in 181 runs (run
noise), 3,577 statistics keys identical. On/off: the same 181 runs at exactly the same offsets, statistics identical. On
(x64) against the 32-bit DLL (i0ship-ccfd6dd4-ia): +12 bytes (version string) and 183 regions = the 181 noise offsets + the
version string + one 4-byte value at body offset 6981 (game section, before the save database; not attributed);
statistics identical. *(Replaces for the current pins the 16.5b result "about 319 KB in 298 regions": with D18 on both
sides, decision D32, the x64 and the 32-bit saves now differ only by noise, the version and one value.)* Notification texts:
17.3.

### 17.8 The 32-bit baseline at the current pin; tools/d10 (issue 2803)

- [C] Instance-aware off-switch baseline: `<pins_dir>/dlls/i0ship-ccfd6dd4-ia/CvGameCore_Expansion2.dll`, SHA-256
  A395CAD2206AA09E674855A9256DB02A4EBD483E6908FDDE93FC8AC6A9F0D712 (PDB D3D028F3...957D): i0-ship ccfd6dd4d + the instruments
  7307aefdf..440878983 (still the head of `instance-aware-instruments`), applied clean (one new overlap: SqliteLogger.cpp, the
  D35 pragmas and the RunId column merged by context), clang-cl 19.1.7 release, version `Release-5.4.8-13-gccfd6dd4d+ia440878983
  Copy`; Lua channel names instance-aware. It replaces i0ship-e6f904ad-ia for live comparisons at the current pins. Used live as
  the B arm (`--mode off --fallback <that path>`): 17.7. [I] Its game results were compared with the x64 build live (17.7
  equality), not with the plain i0ship-ccfd6dd4 (MemoryHooks changes the heap layout; RunId column).
- [C] `tools/d10/run_d10.py` now finds D18 by patch-id (`git cherry`), so the cherry-pick 40ce81aee (D32) and its descendants
  read as D18; defaults moved to `i0ship-ccfd6dd4` and `x64-c43ea1b9/bin` (hashes checked against the lock files); 45 tests
  pass; the new tests fail on the old code (`server/evidence/2800/w3/`).

### 17.9 The DLL's logging under the proxy (MC question, 2026-11-11)

- [C, L] The EXE sets the DLL's logging flags itself from the player's `config.ini` (`LoggingEnabled`): every live run's
  `proxy-calls.tsv` has one `ICvGameContext1::SetLogging(bool)`, `SetRandLogging`, `SetAIPerfLogging`, `SetBuilderAILogging`,
  `SetPlayerAndCityAILogSplit` and `SetTutorialLogging` crossing to the server's DLL. The DLL in the server follows the player's
  setting: with logging on it runs its logging code paths (AI logs, settle-plot dumps) exactly as the 32-bit DLL does.
- [C] What the server does NOT do is write those files: its `FILogFileMgr` (`server/src/srv_ifaces.cpp` `SrvLogFileMgr::Get`)
  writes only `cvassert.log`, `database.log`, `gamecore.log`, `custommods.log` and `xml.log` unless `server.cfg dll_logs=1`;
  every other log the DLL opens is accepted and discarded. `dll_logs` is a server.cfg key written by
  `tools/instances/provision.py set-proxy` (0); the shipped default is not decided. So today a player who turns logging on pays
  the CPU of the logging paths with the proxy and gets no AI log files (and they would land in the server's run folder
  `VP64/run/<stamp>/Logs`, not the player's `Logs` folder; since 0148 the run folder is `<user>\Logs\VP64\run\<stamp>`,
  outside the watched MODS tree). Not fixed in this epic: the fix is a decision about where the
  server writes the player's logs (proposal: follow the DLL's request, i.e. `dll_logs` default 1 in remote mode, into the
  player's `Logs` folder, which the EXE's own log manager would have used) [I].
- Evidence of the setting per run: since build B4 (438b5ca) `server-report.txt` has the line `dll logs: the DLL asked for N
  log files, M of them written (dll_logs=..): <names>`; with `LoggingEnabled = 0` the DLL asks for none of its AI logs. For the
  32-bit arm the instance's `Logs` folder shows whether AI logs were written during the run.

### 17.10 Incident: the EXE crashed loading the huge save (bug 0089, 2026-11-11 01:52 EST)

- Arm H-luaverify2, build B6 89084ac, `C5H-VP8P-HUGE_0350`, `lua_mirror=1 local_verify=1`, `LoggingEnabled = 0`, c5h-a, EXE
  pid 25196 launched 01:48:31 (guard veto ok, start-up watch ok, watch loop 0 hits, no dialog, leftovers clean, no save lost).
  At 01:52:01, 77 s into the proxy's boot while the save body streamed through pull sessions, the EXE ended with 0xC0000005
  in MSVCR90.dll 9.0.30729.9635 at offset 0x24651 (Application Error event 1000; gbe `crashes.txt` @0x70DE4651); the server
  exited by itself (code 3, peer died).
- [C] The faulting instruction (msvcr90 VA 0x78544651 at its ImageBase 0x78520000, between the exports `_freefls` 0x238DC and
  `memcmp` 0x24792) is `movdqa xmmword ptr [edi], xmm0`, the first store of the CRT's SSE2 aligned 128-byte block-copy loop
  (the fast path of memcpy/memmove); the four loads from `[esi]` before it succeeded, so the DESTINATION was not writable:
  a NULL/freed destination (as after a failed allocation) or a write past a buffer into an unmapped page. No dump (the WER
  archive holds no cab and is not readable without administrator rights), so which one is not known.
- [L] The machine was at its commit limit at that moment: the harness's own Python raised `MemoryError` allocating a 1 MB
  buffer at 01:52 (the same arm's `wait_game.py`) and again at 01:53 (arm F1-T-on, discarded); free commit 4.0 GB of 43.3 at
  01:55, 6.1 GB at 01:57 with no game of this team running (all processes 28.6 GB of commit; other teams' builds and verifiers).
- Attribution [I]: a failed allocation in the 32-bit EXE under system-wide commit exhaustion is the likely cause; a defect of
  the proxy code that runs in the EXE (the stream pull sessions or the Lua mirror) is not excluded. Evidence:
  `D:/c5-64bit-work/2800/incident-0152/` and `server/evidence/2800/live/H-luaverify2/incident.txt`. Rule from the MC: a
  rerun above 10 GB free commit, twice, before calling it out of memory. *Done 2026-11-11 05:43-06:04 (page file enlarged by the owner
  at 02:25): R1 and R2, the same build, save and settings, free commit never below 23.2 GB: both loaded and played 10 AI turns
  with the checklist, 0 mismatches (17.7). Attribution stays [I]: the crash happened only at the commit limit; bug 0089 stays
  open for the launch gate's memory floor.*
- Product note [I]: under the proxy the 32-bit EXE still streams the whole save body through its own heap (pull sessions); on a
  machine near its commit limit that is where a huge load fails. The EXE's peak private bytes during the load were not
  recorded in this run.

### 17.11 Frame rate on Tiny, settled on one build (B6 89084ac, 2026-11-11) [L]

Method (taken over by the performance team, PERFORMANCE.md section 2): instance c5h-a, save `c5h-c5h-a-t0008`, every arm
loads the save, closes the load screen and measures the Lua frame counter (`Events.LocalMachineAppUpdate`) over a 45 s
wall-clock window on the human's turn with nothing open (`proxy/tools/live/idle_window.py`), then stops. Arms: **on**
(`local_answers=1`, `lua_mirror=0`), **off** (`local_answers=0`), **B** (the off switch handing the EXE to the 32-bit
i0ship-ccfd6dd4-ia, 17.8), all three on the SAME proxy/server build B6; `config.ini LoggingEnabled = 0` in every arm (17.9).
Six repetitions, the order rotated per repetition (on-off-B, off-B-on, B-on-off, ...), run by the series runner
(`%USERPROFILE%/AppData/Local/Temp/claude/e2800/series.py`, copied to `server/evidence/2800/fps-tiny/series.py.txt`) under
the team's own quiet-machine holds (`perf/quietlock/quietlock.py take`/`release`, at most ~25 min per hold, 10 min gaps);
free commit recorded before and after every arm (`commit.tsv`), machine CPU every 5 s (`cpu-load.csv`), the lock tool's
disturbance summary per hold in `series.log`. Repetitions 1-3 were disturbed by other teams' work that had passed the
gate while the lock was free (MC notices: 02:21-02:36 9020's x86 hosts, 02:21-02:24 and 02:55-03:08 3100's builds and fuzz
run) and were rerun as complete triples (F1r-F3r). Table: `server/evidence/2800/fps-tiny/fps-table.txt`
(`python proxy/tools/live/fps_table.py server/evidence/2800/fps-tiny`).

| kind | n | mean fps | median | min | max | stdev |
|---|---|---|---|---|---|---|
| on (local answers) | 6 | 437.5 | 435.1 | 424.7 | 453.6 | 12.1 |
| off (every call crosses) | 6 | 359.6 | 363.3 | 328.7 | 379.7 | 17.5 |
| B (32-bit DLL through the off switch) | 6 | 450.3 | 451.6 | 421.2 | 468.7 | 17.0 |

Paired by repetition (same hold, consecutive arms): on - B = -44.0, -5.9, -10.4, +12.8, -19.0, -10.6 fps; mean **-12.9 fps
(-2.8 % of B)**, stdev 18.6, 95 % interval **-32.3 .. +6.6 fps** (-7.2 % .. +1.5 %). off - B = -89.0, -86.9, -99.5, -68.2,
-116.3, -84.7; mean -90.8 fps (-20.1 %), 95 % interval -107.6 .. -73.9. Repetitions used: F1r-F3r (reruns 04:54-05:43) and
F4-F6 (03:14-04:43); holds by the lock tool's record (`holds-history.jsonl`): every hold of these arms `disturbed: false`,
no other team's heavy process in them (new heavy processes: only this team's EXE and server); machine CPU over the 45 s
windows 25-45 % of all cores (mean; the measured EXE itself, other agents' sessions); free commit 30.9-39.6 GB at every arm
start and end. Discarded: F1-F3 of the first pass (02:21-03:10; other teams' builds and runs inside the holds, MC notices),
F1-T-on of 01:53 (harness MemoryError, commit exhaustion).

**Statement.** On Tiny idle, the proxy with local answers is **2.8 % below the 32-bit DLL (mean -12.9 fps), and the series cannot tell that from parity**: the 95 % interval of the paired difference runs from -7.2 % to +1.5 %, so a gap larger than about 7 % is excluded and the interval contains 0. Without local answers the gap is 20 % (16-24 %). [L, B6 89084ac, 2026-11-11]
*(Verification 9028, 2026-11-11 11:12-12:00, c5h-c, the same build and method, 3 interleaved repetitions per arm: on 439.5,
off 363.9, B 446.4 fps, each inside the team's interval; with a much smaller spread (sd about 2 fps) the paired on - B is
-1.5 % (95 % -2.8 % .. -0.3 %): a small real gap, not parity. Also: repetition 6 above pairs F6-T-off (04:41) with
F6-T-B (03:50), two different holds. `verification-2800.md` section 4.)*

Why the remaining gap (per idle Tiny frame, 17.7 and 16.5): about 31 crossings remain, 16 of them `rt:LockSync` (8 lock
sessions of the slot poll and the other engine call sites), each a round trip to the server; plus the proxy's own work for
about 576 local answers per frame. Reducing the lock sessions (17.7, lazy release) is the next lever for the idle frame.

**Huge-save frame rate: not done in this epic** (MC decision 2026-11-11 04:30: moved to the independent verification,
which runs it unchanged): spec `server/evidence/2800/fps-huge-spec.txt` (its first two lines are the crash reruns of 17.10, done here; then 18 arms F1-F6 x on/off/B, rotated, `--idle 45`,
save `C5H-VP8P-HUGE_0350`), build B6 89084ac (kept at `D:/c5-64bit-work/2800/B6`, hashes in
`server/evidence/2800/live/build-identity-B6.txt`), the same runner and rules (start at >= 8 GB free commit, 5 s commit
sampling, discard an arm whose minimum falls below 3 GB).
*Done 2026-11-11 07:57-11:09 by verification 9028 on c5h-c (18 arms, six holds, none disturbed, none rerun): on 214.0 fps,
off 194.5, B 231.6; on - B = -7.6 % (95 % interval -11.6 % .. -3.7 %): on the huge save the gap to the 32-bit DLL is real;
off - B -16.0 %. `verification-2800.md` section 3.*

### 17.12 Not verified, open (epic 2800)

- Huge-save frame rate (17.11, moved to the verifier; *measured by 9028: -7.6 % against the 32-bit DLL, 17.11 note*). AI-turn wall time per arm under a hold with logging off in both arms:
  not measured (the AI-turn arms of 17.7 ran with `LoggingEnabled = 1`, 32-bit arm writing its AI logs, the server not).
- The UI-Lua mirror hits few calls (17.6/17.7): extending `lua-pure-bindings.tsv` to the impure-by-default long tail (about
  650 functions, 51 % of the calls) is the work that would make it pay; until then `lua_mirror` stays 0.
- Fewer lock sessions per frame: design only (17.7). A plot mirror: not justified (0.13 M plot reads per huge AI turn; plots
  need per-object invalidation that cannot be shown exact today).
- Bug 0089 (17.10): attribution [I]; reruns in 17.7.
- The combat pair `GameplayUnitCombat`/`GameplayCityCombat` and 17 other engine methods taking DLL objects were never called *(4 of
  the 17 met live by 9034 on the huge save and, with `GameplaySpaceshipEdited`, read and listed by 9036: 17.4 correction)*
  live and not read; the LOUD check (17.4) will name them when a human combat with animation runs. *(2026-11-15, ticket 0316: `GameplayUnitCombat` met live and read, it borrows; 17.4 correction and `combat-on-the-product-path.md` section 4.)* *(2026-11-15, ticket 0329: `GameplayCityCombat` read, it borrows; both on the list; 17.4 correction.)*
- The player's DLL logs are not written under the proxy (17.9): a decision for the shipped `dll_logs` default.
- On-screen hover check of a notification text: not done (the notification list was read through the UI's own Lua calls).
  *(2026-11-11, verification 9028: hovers of notification texts shown through the harness, `server/evidence/9028/hover/`;
  a hover of a diplomacy-response text specifically not shown; `verification-2800.md` section 5.)*

## 18. The EXE ran out of address space through the proxy: bug 0102, issue 9036 (2026-11-11/12, one-off 9036)

One-off 9034's long proxy run (`p2-huge-new`, c5h-c, pin `x64-76992d23`, server.exe a1d7bdd6, `local_answers=1`,
`lua_mirror=0`, save `C5H-VP8P-HUGE_0350`) played AI turns 350-419 and at turn 420 (the turn the headless runs end in
a Diplomatic victory) showed "Microsoft Visual C++ Runtime Library: Runtime Error! ... This application has requested
the Runtime to terminate it in an unusual way." Evidence `D:/c5-64bit-work/9034/proxy/p2-huge-new/`; dumps and scratch
of 9036 in `D:/c5-64bit-work/9036/` (not committed).

### 18.1 What the abort was [C, dump `exe-40432.dmp`, SHA-256 A3574740...1E08]

- The thrown object is `std::bad_alloc` ("bad allocation") from msvcr90's `operator new`; nothing caught it, so
  `msvcr90!terminate` (called from EXE VA 0x42DEDB) -> `abort` -> the dialog. The victory, the harness's diplomacy
  answer and bug 0101 are not on the stack (the three candidates of the brief).
- Main thread (stack 0x0010xxxx; thread 0xE454): EXE `ICvGame2::Update` (called from VA 0x4D8C4A) ->
  `Proxy_ICvGame2::Update` -> `ProxyRt::End` (waiting for the server) -> the server calls back into the engine
  (`OnCall` -> `EngDispatchGenerated` -> `EngDispatch_ICvEngineScriptSystem1+0x29e`) -> `ProxyRt::EngineHandleOf` ->
  `g_eng.push_back` (proxy_rt.cpp 1797 at the pin) -> `operator new` of the vector's reallocation: **the allocation
  that failed was the proxy's own**, a 64 MB block for its engine-object table, requested under `g_lock`.
- `g_lock` stayed held (owner 0xE454, recursion 1; terminate runs before any unwinding); a job thread (thread 0) was
  blocked on it in `ProxyRt::WrapperRelease` from `Proxy_ICvGameAsynch1::Destroy` (called from EXE VA 0x7254F8).
- The address space was used up: committed 3,420 MB (private 2,954, image 432, mapped 34), reserved 514 MB, free
  159 MB, largest free block 12 MB, of 4,096 MB (dump's MemoryInfoList; the EXE is large-address-aware, flags 0x0122).
  `research/04-memory-investigation` puts the stock game's death at about 4,025 MB of address space.
- The server dump (`server-19184.dmp`, 384 MB, SHA-256 358AD901...8044) was not needed.

### 18.2 The EXE grows about 12 times faster through the proxy than on the stock route [L, 2026-11-11]

Same instance (c5h-a), same save (`C5H-VP8P-HUGE_0350`), same driver (`tools/validation/proxy_long.py`, AI turns, no
checklist; `--stock` for the instance's own layout), EXE memory sampled after every turn and every 5 s
(`tools/harness/memory.py`); runs in `D:/c5-64bit-work/9036/runs/stock-350` and `proxy-350`.

| | Stock route (32-bit pin DLL `i0ship-bbfacca1-ia`, no proxy) | Proxy route (pin `x64-76992d23`, server.exe 3A35330A) |
|---|---|---|
| EXE private right after load at turn 350 | 2,014 MB (largest free 1,245 MB) | 1,839 MB (largest free 1,518 MB) |
| after the first AI turn (351) | 2,112 MB | 1,965 MB |
| growth per AI turn | **2.41 MB** (least squares, turns 353-380) | **about 30 MB** (1,965 -> 2,291 MB, turns 351-361) |
| largest free block | 1,136 -> 969 MB (turns 351-380) | 1,363 -> 923 MB (turns 351-361) |
| seconds per AI turn (median) | 31.5 | 24-27 |

The proxy route starts 175 MB lower (the DLL's game state is in the server) and overtakes the stock route by turn 357.
9034's runs agree: about 2,609 MB at turn 375 and 3,000 MB at turn 400 on both pins (15 MB/turn, checklist samples).
The stock growth matches the memory investigation's 2.48 MB/turn (`04-memory-investigation/README.md` item 5).
The proxy run ended at turn 361 because 9036's own dump froze the EXE during a driven turn (run-and-drive section 14).

### 18.3 What grows [C, dumps of turn 361 (`series/c5h-a-x86-36836-20261111-172858.dmp`) and turn 420]

- **Save bodies.** Private allocations of 12.1 MB: 23 at turn 361, 70 at turn 420; of 18.5 MB: 0 and 8. About 715 of
  the 800 MB grown in those 59 turns. Their reserved sizes are 12,368-12,432 KB in 4 KB steps (each a little larger
  than the one before), the 18.5 MB ones exactly 1.5 times that. The content is the DLL's serialized game state
  (`REPLAYDATASET_*`, `TXT_KEY_SPY_NAME_*`, `MOVE_ROUTE`, plot byte arrays with a 128-plot row period = the huge map's
  width): uncompressed save bodies. *Corrected 2026-11-13 (9041, 18.7): they are NT-heap blocks of the proxy's own RETURN
  ring, not the EXE's; the allocation base is a reserved 8 KB prefix and the heap's `HEAP_VIRTUAL_ALLOC_ENTRY` header
  follows it (replaced: "No NT heap header at the allocation base: raw `VirtualAlloc` regions of the EXE's own
  allocator").* A separate group of 16,192 KB allocations went 39 -> 43 (NT heap segments, signature 0xFFEEFFEE [C, 9041]).
- **The proxy's engine-object table** `g_eng`: one 12-byte entry per engine object ever handed to the server, never
  reused. 3,543,306 entries at turn 420, all but 3 of them dead `ICvEngineScriptSystemArgs1` (kind 17; 1,021 distinct
  pointers, mostly 0x12010000): one per Lua hook call of the DLL (`CreateArgs`/`DestroyArgs`, dropped from the index
  by `EngineForget`). 42.5 MB, about 0.6 MB per turn, and each reallocation needs 1.5 times that contiguous.
- **Not growing:** the proxy's other tables at turn 420 (turn 361 in brackets): `g_canon` 154 (153), `g_canonHandles`
  288 (287), `g_local` 4,855 (1,226), `g_mirror` 501 (501), `g_luaMirror` 49,560 (39,798; emptied at 200,000),
  `g_wrappers` 8,874, `g_wrapperRefs` 1, `g_engIndex` 3, `g_intern` 368, `g_tokens` 0.45 MB, `g_erefs`, `g_erefFree`,
  `g_threads` and `g_release` empty. `g_pullStreams` has 131 entries, all the same dead stack address 0x06FEE170 (one
  pull session's engine stream per turn: 4 bytes and a dangling pointer, not memory). Vftable pointers of proxy, lua51
  and msvcr90 objects in private memory changed by at most a few thousand.
- How the proxy's VC9 globals were found without data symbols: the static initializer of `proxy_local.cpp` (file VA
  0x1005BB60 of the pin's proxy) stores each map's head and size: `g_canon` 0x100DEB8C/90, `g_canonHandles` 98/9C,
  `g_local` A0/A4, `g_mirror` A8/AC, `g_luaMirror` B0/B4 (rebase by the proxy's load address).

### 18.4 Why the save bodies stay [code C, mechanism I; REFUTED live 2026-11-13, see 18.6; the real cause is 18.7]

Every `Write(FDataStream&)` the EXE calls on a DLL object (the save) came back as ONE `FDataStream::WriteIt(len, p)` of
the whole body in `ProxyRt::StreamOutRet` (12 MB on this save). The in-process DLL writes the same body item by item.
Inferred [I], to be confirmed live (18.6): with one big write the EXE's stream grows to exactly the body's size (and to
1.5x on the next append), a new and slightly larger size every turn, which the EXE's allocator does not serve from the
previous turn's freed block; with many small writes the stream follows the same capacity ladder every turn and its
blocks are reused. The EXE's stream and allocator code was not read.

### 18.5 Changes (issue 9036)

- `ProxyRt::StreamOutRet` writes the body into the engine stream in 4 KB pieces. *(Did not change the growth, 18.6;
  harmless, kept so the EXE sees the same write pattern as in process.)*
- `g_eng` reuses dead slots (`ProxyLogic::SlotAllocator`, `proxy_rt_logic.h`): a freed slot is reused only after
  4,096 later frees (FIFO), so an id is never handed out again while the server may still hold it (`SrvRt::EngineForget`
  erases its entry right after the call that freed it) and a stale handle keeps being refused as dead for that long.
  Unit test `TestSlots` in `proxy/tools/rt_tests/unit.cpp`: 3,543,306 make/destroy pairs keep the table at <= 4,098
  slots, no slot is reused before 4,096 later frees, 1,000 live objects need <= 5,097 slots. Shutdown line `engine
  objects: table slots N (K KB), live L, freed slots waiting W; served calls ended by a C++ exception E`.
- `OnCall` catches C++ exceptions thrown inside a served call (`std::bad_alloc` first); the unwinding releases the
  locks taken in the call (the LOUD line says whether `g_lock` is still held by the thread: it must say `no`). MC
  decision 2026-11-12: the game does not go on after a call the EXE did not complete (the EXE and the server would
  differ silently). The proxy returns the new status `kStatusEngineException` (21, `vp_wire.h`); the server ends its
  session with `result: FAILED: exe_exception: ...` and exit code 6 (`srv_rt.cpp`); the EXE's thread that waits on the
  server then takes the existing `on_server_death` path (`Fatal`, exit 3 with `on_server_death=exit`). Test: hutest
  scenario `throw` (the fake engine throws `std::bad_alloc` from `GameplayUnitParadrop`; expects host exit 3, the LOUD
  line with `g_lock ... no`, and `exe_exception` in the server report). It does not reproduce a throw while `g_lock` is
  held (the turn-420 case); that release rests on the RAII `Lock` and the unwinding.
  *Corrected 2026-11-15 (ticket 0359, section 27): the catch did not cover every served call. `kRtE_StreamRead` (the
  server's pull of an engine save stream) was served in its own branch of `OnCall` before the try, so a throw from the
  EXE's stream (a read past its end) still went to terminate() and the CRT abort box (0334's 4254). Since 0359 it is
  served under the same catch (`ServeStreamRead`). Replaced: "OnCall catches C++ exceptions thrown inside a served call"
  read as every served call.*
- Bug 0101: five engine methods added to the borrowed-argument list (17.4, corrected there). A sixth since 2026-11-13:
  `GameplayNaturalWonderRevealed` (bug 0124, issue 3309; 17.4). Two more since 2026-11-15: `GameplayUnitCombat` and
  `GameplayCityCombat` (ticket 0329; 17.4).
- Offline results on build `b1` [L, 2026-11-13 00:10-00:25]: `test_proxy_rt.py` 55/55 (unit 58/58, `TestSlots`: 4,097
  slots after 3,543,306 make/destroy pairs; its never-reusing negative control 3,543,306 and fails the bound); hutest
  45 checks, 0 failed; hutest `--scenario all` 8/8 as expected (`throw`: host exit 3, server `result: FAILED:
  exe_exception: ... GameplayUnitParadrop ... threw std::bad_alloc`, proxy LOUD with `g_lock ... no`, then `FATAL ...
  on_server_death=exit`); the same `throw` scenario with the pin's proxy 96D4521E fails (host ends in `abort()`, exit 3,
  no window, neither line). Verify leg (17.2 command: headless Tiny 0-50, `local_answers=1 local_verify=1
  test_slot_poll=4`, strict): PASS, 1,300,212 mirror answers verified, 0 mismatches; shutdown line `engine objects: table
  slots 4100 (63 KB), live 2, freed slots waiting 4098; served calls ended by a C++ exception 0` (the slot reuse at work).
  Logs in `D:/c5-64bit-work/9036/` (`test_proxy_rt.txt`, `hutest*.txt`, `verify.txt`, `verify/proxy/vpproxy.log`).

### 18.6 Live results of the changes [L, 2026-11-12 23:32 - 2026-11-13 00:09, c5h-a]

Build `D:/c5-64bit-work/9036/b1` (repository c8a453926, dirty=0, pin 76992d235, plain source copy whose fingerprint
matches): proxy SHA-256 04A87E93...E5F7, server.exe D99F90EF...6C61 (not used live). The live run used the pin's
`bin` (server.exe 3A35330A, x64 DLL FA75F483) with this proxy, so only the proxy differs from run `proxy-350`.
Same save, driver and sampling as 18.2; region census every 30 s by a read-only `VirtualQueryEx` walk of the live EXE
(private allocations by committed size; scratch script, rows in `runs/<run>/regions.csv`).

| Run | turns | EXE private | slope (least squares 353-380) | largest free block | 11-13 MB / 17-19 MB private allocations |
|---|---|---|---|---|---|
| `stock-350` (stock route) | 351-380 | 2,112 -> 2,186 MB | 2.41 MB/turn | 1,136 -> 969 MB | - |
| `stock-regions` (stock route, census) | 351-360 | 2,106 -> 2,153 MB | (10 turns) | - | stays at 2-4 / 0-1 (each save's buffers come and go) |
| `proxy-350` (pin proxy) | 351-361 | 1,965 -> 2,291 MB | about 30 MB/turn | 1,363 -> 923 MB | (dumps: 23 at turn 361) |
| `fix-350` (9036 proxy) | 351-380 | 1,991 -> 2,731 MB | **24.3 MB/turn** | 1,366 -> 378 MB | 2 -> 44 / 1 -> 7 (about 650 MB in 30 turns) |

- **Writing the save body in 4 KB pieces did NOT stop the growth** (24.3 MB/turn against 2.41 on the stock route). The
  inference of 18.4 (one big `WriteIt` makes an exact-size buffer the allocator cannot reuse) is refuted [L].
- **What is established instead [L]:** body-sized buffers (11-13 MB, and 17-19 MB) appear on both routes, about 1.5
  per AI turn (VP autosaves twice per turn: `AutoSave_` and `AutoSave_Post_`); on the stock route they are released
  again (2-4 present at any time), through the proxy they are not (44 + 7 after 30 turns, about 22 MB/turn of the
  24.3). The remainder, about 2 MB/turn, is the stock route's own growth. *(Corrected 2026-11-13 by 18.7: the kept ones
  are not the EXE's save buffers but the proxy's copies of the save bodies in its RETURN ring; replaced: "something the
  EXE releases after a save in process is not released through the proxy".)*
- Not the cause (checked): per-turn worker threads (the EXE starts one or two per turn, `MSVCR90.dll+0x2345E`, first
  call `GetMap`, aux channel; thread count steady at 79-80 [L]; `ThreadState` is freed on `DLL_THREAD_DETACH` [C]);
  the proxy's tables (18.3); vpipc's reassembly buffer (`malloc`, released at the channel's next send above
  `kTrimBytes`, and a heap block would carry a heap header) [C].
- **Answered by 18.7 (9041):** the owner is the proxy's RETURN ring. The leads that follow were the open question of
  2026-11-13 00:09, kept for the record. *Open, for the successor (bug 0102 stays open):* which EXE object owns the
  body-sized buffers and what releases it in process. Leads: the order of calls around an autosave in `vpproxy-calls`/traffic tables of a stock (in-process,
  instrumented) and a proxy run; whether a `Destroy` or a release of an engine object (`FDataStream`, `ICvEngineScriptSystem
  Args1`, a pull session's stream) is missing on the proxy side; the 16 MB pull-session streams the load opens
  (`StreamInArg ... no size: pull session`, 131 of them at turn 420 in 9034's run, all the same stack address) are kept in
  `g_pullStreams` but not released by anyone. A read-only census like the one above, sampled around one autosave, would
  show which call frees them on the stock route.
- `g_eng`: no live count (the harness stop ends the EXE before `WriteStats`, so no shutdown line); the unit test covers it.
- Bug 0101: 0 borrowed-argument warnings in 30 AI turns of `fix-350` [L] (the 10 turns of `proxy-350` had 0 as well, so
  this run alone does not show the change; 9034's two runs had 19 over 61 and 70 turns).
- The end-game run from turn 418 was not made: the leak is not fixed, so the turn-420 end through the proxy depends on
  the address space left at load (1,846 MB private after load, about 24 MB/turn: turn 420 from a fresh load at 418 is
  within reach, but that would not show the leak gone).

### 18.7 The cause: the proxy's RETURN ring keeps every save body (issue 9041, 2026-11-13)

**Found [C]** by a read-only parse of 9036's turn-361 dump (`D:/c5-64bit-work/9036/dumps/series/c5h-a-x86-36836-20261111-172858.dmp`,
2.73 GB, the EXE through the pin proxy, save `C5H-VP8P-HUGE_0350`; scripts in the one-off's scratch, not committed: a
minidump reader of streams 4/9/16 and a numpy pass over all dumped memory for pointers):

- The 23 private allocations of 12.08-12.09 MB are NT-heap `VirtualAlloc` blocks: 0x2000 bytes reserved at the
  allocation base, then the committed part starting with `HEAP_VIRTUAL_ALLOC_ENTRY` (list links to the other big blocks,
  CommitSize 0xC14000, ReserveSize 0xC17000, the encoded busy entry); the user data starts 0x20 bytes into the committed
  part. 9036's "no NT heap header at the allocation base" looked at the reserved prefix.
- Each block's user data is pointed at by exactly one `std::vector<uint8_t>` (first, last, end) whose **size is 4-20
  bytes and capacity 12,661,184-12,663,676 bytes** (= the `ICvMap1::Write` body, 12,662,545 bytes per save in 9036's
  `proxy-calls.tsv`), plus one in-flight reference on the main thread's stack.
- All those vectors sit in **two arrays of 64 vectors of 16 bytes** (VC9 layout with `_SECURE_SCL 0`), each preceded by
  the `new[]` count cookie 64 and pointed at from one thread's TLS block, with a counter beside the pointer (13,094,761
  and 722,498 calls): the proxy's `KeepReturn` (`proxy_rt.cpp`), which copies every RETURN payload into the next of 64
  per-thread vectors (`t_next`, `t_bufs`).
- At turn 361 the two rings keep **188.0 MB and 213.9 MB**: 47 + 52 of the 128 slots hold 1 MB or more (12.4 MB map
  bodies and 0.3-2.9 MB other bodies), every slot's size is that of the last small RETURN.

**Mechanism [C, code]:** `KeepReturn` did `b.assign(payload)` on the slot's vector. `assign` never gives capacity back,
so a slot that once held a save body keeps that capacity for the life of the thread; the core thread makes thousands
of calls per turn, so each save's map body lands in a random slot and over the game more and more of the 64 slots of
each thread hold 12-19 MB (up to about 1.2 GB per thread). A slot whose old body is a little smaller than the new one
grows to 1.5 times its capacity (VC9 `vector` growth): the 18.5 MB group. The EXE's own save buffers are released on
both routes; the "save buffers kept through the proxy" of 18.6 are the proxy's copies. 9036's 4 KB pieces in
`StreamOutRet` could not help: the copy is made before `StreamOutRet` runs. The server's `srv_rt.cpp` has the same
ring (`KeepReturn`, 64 per thread) for the RETURNs it receives from the proxy (engine answers, `kRtE_StreamRead`
chunks): not part of the EXE's growth; noted for the server's memory (not changed by 9041). Corrected 2026-11-13:
fixed by issue 0119 with the same rule (`server-host.md` section 16; was "not changed").

**Fix (issue 9041):** `ProxyLogic::ReturnRing` (`proxy_rt_logic.h/.cpp`): a slot whose buffer is above 64 KB is
released before it is reused, and a payload above 64 KB gets a buffer of exactly its size (no 1.5x growth). A payload
stays valid for the next 63 calls of the same thread, as before. The ring is freed on `DLL_THREAD_DETACH` (the EXE's
per-turn worker threads each left a ring behind before). Unit test `TestReturnRing` (`proxy/tools/rt_tests/unit.cpp`,
2026-11-13 [L-offline]): the huge save's RETURN pattern (two saves per AI turn with the map 12.66 MB + 4 KB per turn,
64 players about 358 KB, 64 teams 20 KB, game 800 KB, six 1-2 MB other RETURNs and 2,000-5,000 small ones per turn)
over 70 turns keeps 1.2 MB at the end, peak 13.7 MB, every copy intact for the next 63 calls; the old behaviour
(negative control, the same class with no release) keeps 279.0 MB after 10 turns. Unit 64/64.

Not changed: `proxy_local.cpp`'s per-thread `ThreadLocal` (the local wrapper caches) is not freed on thread exit either;
small (maps of a few entries per worker thread), not part of this growth. Corrected 2026-11-13: freed on
`DLL_THREAD_DETACH` since issue 0119 (`ProxyLocal::FreeThread`, `server-host.md` section 16).

### 18.8 Live proof of the fix [L, 2026-11-13 00:59-01:21, c5h-b]

Run `ring-350`: c5h-b (same machine, same root `%USERPROFILE%\c5-instances` as 9036's c5h-a; save `C5H-VP8P-HUGE_0350`,
SHA-256 c499d3c6...88c3 on both instances), the pin's `bin` of `x64-76992d23` (server.exe 3A35330A, x64 DLL FA75F483)
with the 9041 proxy (`D:/c5-64bit-work/9041/b1/proxy/bin/CvGameCore_Expansion2.dll`, SHA-256
7dea904c56396e6d8a0e6ab4c2670fc2ceaed57fc50f92dd7c927a3dacbc728e, repo e4f87ae83 dirty=0, pin 76992d235; it also
carries 9036's changes and 9042's options-file loader), `local_answers=1`, `lua_mirror=0`; the same driver and
sampling as 18.2 (`proxy_long.py ... --max-turns 31 --checklist-every 0`), 31 AI turns 351-381, ended by max turns,
safe stop, `vpproxy.ini` restored, instance back in its own layout (DLL e589306e restored by hash). Evidence
`D:/c5-64bit-work/9041/runs/ring-350/` (run.json, memory.csv, vpproxy.log, server-report.txt) and
`D:/c5-64bit-work/9041/vp64-runs-c5h-b/`. Slope = least squares of `exe_mem.private_mb` after each turn, turns 353-380
(9036's window; the scratch script reproduces 9036's 2.41 and 24.34 from their run.json files):

| Run | route | EXE private 353 -> 380 | slope MB/AI turn (95 % CI) | largest free block, first -> last sample |
|---|---|---|---|---|
| `stock-350` (9036, c5h-a) | stock, 32-bit DLL | 2,116 -> 2,186 | 2.41 (2.01-2.81) | 1,136 -> 969 MB (turns 351-380) |
| `fix-350` (9036, c5h-a) | proxy 04A87E93 | 2,064 -> 2,731 | 24.34 (23.44-25.24) | 1,366 -> 378 MB |
| **`ring-350` (9041, c5h-b)** | **proxy 7dea904c** | **1,918 -> 1,987** | **2.01 (1.55-2.48)** | **1,424 -> 1,238 MB (turns 351-381)** |

- The proxy route now grows **2.01 MB per AI turn against 2.41 on the stock route** (difference -0.40, inside both
  intervals; DoD "within about 1 MB of stock" met). Over turns 352-381 (30 points) the slope is 1.79 (1.30-2.28).
- The largest free block shrinks 6.2 MB per turn (stock 5.6, the old proxy 33): no fragmentation from the exact-size
  buffers. The proxy route keeps its 175-200 MB head start from the server owning the game state (1,942 MB after the
  first AI turn against the stock route's 2,112).
- 0 LOUD lines, 0 borrowed-argument warnings, 0 exceptions in `vpproxy.log`; the server's report ends "the engine is
  gone ... PeerDied" as in every harness-stopped run (the harness stop ends the EXE; 9036's runs read the same).
- The hold was taken with `--overlap` (one heavy job of team 4000 running) and is marked DISTURBED: no timing is claimed.
- Not done in this run: a stock control on c5h-b itself (the stock figure is 9036's on c5h-a, same save and driver);
  the end-game run from turn 418 to the victory screen (9036's DoD item; now possible, address space is no longer the
  limit).
- Offline on the same proxy [L-offline, 2026-11-13 01:48-01:55], build folder `D:/c5-64bit-work/9041/b1` (`bin` and
  `gen` copied from 9036's b1, whose server source equals this branch's; `proxy` built here): `test_proxy_rt.py` 55/55
  (unit 64/64 with `TestReturnRing`); verify leg (17.2 command, headless Tiny 0-50, `local_answers=1 local_verify=1
  test_slot_poll=4`, strict) PASS, 1,300,212 mirror answers verified, 0 mismatches, 0 lock errors; hutest 45 checks,
  0 failed; hutest `--scenario all` 8/8 as expected. Logs `D:/c5-64bit-work/9041/{test_proxy_rt,verify,hutest,
  hutest-scenarios}.txt`. `test_proxy_rt.py --build` needs the build's `gen/` too (its loader stage reads vtable slots
  from `gen/proxy/proxy_ifaces_*.gen.cpp`): a build folder without it fails "vtable slots from the generated code".

### 18.9 The pin moved; the game to its end through the proxy (issue 9043, 2026-11-13)

**Pin [L-offline].** Decision D87's single move: server and proxy built from repository commit 1607d42b7 (dirty=0; it
carries 9036's g_eng slot reuse, in-order end on an exception and bug 0101's list, 9041's `ReturnRing` and 9042's
options file) with `server/build_server.py --deps` of 9036's full build of 76992d235; layout
`<pins_dir>/dlls/x64-76992d23-p1607d42` = the old pin's x64 DLL FA75F483 and libraries unchanged + server.exe
C71268B0...01B4 + proxy 21C6492A...418B (`deps/dll.lock` `release`; the old layout `x64-76992d23` stays as
`release-old`). Offline on this build: test_proxy_rt 55/55, hutest 45/0, hutest `--scenario all` 8/8, verify leg on the
layout itself (17.2 command with its `--bin` and `--proxy`) PASS, 1,300,212 answers verified, 0 mismatches, shutdown
line "table slots 4100, served calls ended by a C++ exception 0". The DLL rebuilt by 9036 at the same pin is 12B7F4FB,
not FA75F483 (bug 0115), so the layout copies the pinned bytes instead of the rebuild.

**Live, c5h-b, huge save [L].** Run `proxy-350` from turn 350: 86 AI turns, the game ended at turn 436 with the human
(Egypt) defeated, end-game screen shown, no dialog, 0 LOUD, 0 borrowed-argument, 0 exception lines; EXE private
+0.68 MB per AI turn (95 % CI 0.59-0.77), largest free block -1.52 (-1.78..-1.26), never below 1,157 MB
(`research/04-memory-investigation/increment-1-acceptance.md`); over the turns both routes played (351-426) +0.70
against the stock route's +1.63 MB per AI turn, largest free block -1.91 against -2.70 (the stock game ended at turn
425, Egypt defeated too: D78 met). From 9036's turn-418 save
(`9036-HUGE-T418`, cf5d9121, 9034's game): runs `endgame-418` (turns 419-428) and `endgame-428` (from that run's
AutoSave_0428, turns 429-437): the game ended at turn **437, the human defeated**, end screen
(`D:/c5-64bit-work/9043/runs/endgame-428/end.png`), no dialog, 0 LOUD, 0 borrowed-argument lines, EXE private about
1,880-1,940 MB. Neither game reached the Diplomatic victory at turn 420 that the headless hosts play (the EXE games
diverge from the headless ones: bug 0116's subject); the end of the game through the proxy is now shown with the
defeat screen, which takes the same `OpenEndGameMenu` path. The turn-420 abort of 9034 does not recur.
- A first attempt from T418 was held at turn 419 by an AI leader screen (Spain's demand for independence) because
  `turns.py wait` arms the leader-screen handler only after `end`, when the AI turn can already show one; the
  harness's click finder found no button with confidence. 9043 clicked "Understood. We grant you your independence."
  and stopped the game normally; `proxy_long.py` now arms the handler at load (`diplo arm`), and the rerun passed the
  same screen through the handler.
- The harness stop at a finished game (bug 0105's `stop_readiness` path, "game finished (game state 1 over)") ran
  live for the first time in both runs: rc 0, saves settled, `vpproxy.ini` restored by hash.
### 18.10 A hung but living server: the proxy's calls are bounded (bug 0113, 2026-11-13, bugfix team 4000)

**Confirmed [C].** `vpipc::WaitConfig` defaulted `callTimeoutMs` 0 and `failOnHung` false, and the proxy sets neither
(`proxy_rt.cpp` sets only `replySpinUs`/`idleSpinUs`, line ~1202). The heartbeat that `PeerHung` watches is written by
each side's monitor thread (`ipc/PROTOCOL.md` 7.3), so a server whose serving thread is stuck (a deadlock, an endless
loop in the DLL) keeps beating: even `failOnHung` would never fire, and the EXE waited for ever. A server suspended as a
whole (a debugger break, a dump taken with `cdb -pv`) stops beating; `failOnHung` stays off so that such a pause does
not end the game.

**The bound and why it tells a hung server from a slow one.** `callTimeoutMs` bounds each wait for the server's next
message inside a call (a nested call into the engine, a chunk, or the RETURN), not the whole call
(`ipc/src/wait.cpp`, `channel.cpp` `wait_return`). A slow AI turn keeps calling the engine (365 k calls per huge turn,
section 12), so it renews the wait thousands of times per second; only a server that sends nothing at all is cut. The
longest silence a legitimate call can contain is bounded by the longest whole operation measured through the proxy: one
huge-save AI turn, 26.7-33.4 s (15.5; median 31.5 s in 18.2), and the whole launch with the huge save's load, 2 min 45 s
(15.5). A UI call that waits behind the core thread's AI turn sees no message on its own channel for at most that turn.
Chosen: **600 s** (18x a huge AI turn, 3.6x the whole launch), as `WaitConfig::clientCallTimeoutMs`, which replaces a
`callTimeoutMs` of 0 on the client side only (`ipc/src/endpoint.cpp` `alloc_impl`). The server keeps no default bound: the
EXE may sit in a modal dialog for a coordinator's decision (rule D29), and a server that cut it would end the game.
[I] Not measured: the longest single silence inside a call (no per-message gap statistics exist); a machine 2-4x
slower than the workstation would still stay well inside the bound.

**The failure path.** A wait that runs out returns `Timeout`; the proxy logs `call <method> (0x...) on channel N
failed: Timeout (server pid P)` and, with the default `on_server_death=exit`, ends the EXE with exit code 3 (`Fatal`,
11.8), the path 9036 uses for an engine exception. The hung server is left as it is (the harness's `stop` never
terminates a child; `leftovers` lists it as an orphan). The channel stays broken, so no later call waits again.

**Tests [C, 2026-11-13].** `ipc/tests/test_lifecycle.cpp`: `hung_server_call_is_bounded_slow_calls_are_not` (a child
server whose handler never returns while its process and heartbeat live: `Timeout` after the 1.5 s test bound; a quiet
800 ms call and a 4 s call with nested calls every 300 ms: Ok; the next call fails at once), and
`hung_server_without_bound_waits_until_killed` (bound 0, the old behaviour: still waiting after 4 s, `PeerDied` once the
server is killed), `client_call_timeout_default` (600,000 by default, an explicit value wins). **Run 2026-11-13 06:22
EST** (bugfix lead, `python ipc/build.py --arch both --test` through `quietlock.py run`, branch at e72953b + 6f0c8aa):
x86 and x64 7 of 7 ctest, test_lifecycle 126 passed 0 failed each; hung call `Timeout after 1500 ms` (x86) / `1515 ms`
(x64) at bound 1500 ms; quiet call Ok 815 / 811 ms; slow call Ok 4041 / 4048 ms with 13 nested calls; unbounded call
still waiting after 4 s, `PeerDied` 2 / 1 ms after the kill [C]. Not run under ASan (`ipc/build.py` has no ASan
option). Not done: an end-to-end
test with the real proxy and a hung server (hutest has no hang scenario; its server side is not this fix's file), and
a proxy option to change the bound (`call_timeout_ms` would need `proxy_rt.cpp`, held by one-off 9040: proposed text in
issue 0113).

## 19. Mod activation on the proxy route: the server's database copy flushed every write (issue 0129, 2026-11-13)

**Symptom (9047 [L], 2026-11-13 11:48-12:02, c5h-e).** Every launch on the proxy route spent 14-15 min in the front
end's "activating 3 mods - this rebuilds the game database", the EXE "Not Responding": 2,563 DLL writes replayed in 6
`kRtE_DbOps` batches, one every 110-200 s, each batch's round trip only 0.009-0.045 s (server.log of
`D:/c5-64bit-work/9047/vp64-runs/run/20261113-114840-2760-1/`). The first 40 writes, logged one by one, are 0.10-0.57 s
apart (median ~0.12 s): they are `Results::Execute UPDATE AICityStrategies SET ID = ? WHERE ID = ?` and the same on
AIEconomicStrategies (the DLL's DatabaseRemapper in PerformDatabasePostProcessing), each made by the DLL on the server's
copy and only recorded for the next batch.

**Cause [L, headless, 2026-11-13 19:40-19:46 EST].** Not the round trip, not the proxy and not the EXE (the earlier
"[I: on the EXE side]" of `research/05-graphics-composition/terrain-bake-verification.md` section 10 is wrong). The
server copies the engine's database into `<run>/db/Civ5DebugDatabase.<n>.db` (SetGameDatabase hook,
`server/src/remote/srv_remote_main.cpp`) and opened it with SQLite's defaults: `synchronous` 2 (FULL), `journal_mode`
DELETE. Every autocommit write the DLL makes then creates the rollback journal, flushes it, writes and flushes the
database file and deletes the journal. c5h-e lives on the workstation's D:, a rotating SATA disk (ST8000DM004, 8 TB,
`Get-PhysicalDisk`); C: is an NVMe SSD (Samsung 970 EVO Plus). Under the real EXE the engine's database is in memory
(section 14 item b), so stock never pays this. Measured on copies of 9047's server copy (6.8 MB, 832 tables), one
`UPDATE AICityStrategies SET ID = ? WHERE ID = ?` per transaction:

| Disk | Settings | Per write (median) | 512 writes |
|---|---|---|---|
| D: (HDD), Python sqlite3, 64 writes | defaults (sync 2, journal delete) | 177 ms | ~91 s |
| D: | synchronous OFF only | 0.58 ms | 0.3 s |
| D: | journal_mode MEMORY only | 64 ms | ~33 s |
| D: | synchronous OFF + journal MEMORY | 0.03 ms | 0.015 s |
| C: (NVMe, `c5-instances`, Defender exclusion) | defaults | 3.9 ms | 2.0 s |
| C: (NVMe, Temp, no exclusion) | defaults | 4.3 ms | 2.2 s |
| D:, `CvGameDatabase_x64.dll` of the pin (the test below) | defaults | 136 ms | projected 69 s |
| D:, `CvGameDatabase_x64.dll` | synchronous OFF + journal MEMORY | 0.035 ms | 0.020 s |
| C: (Temp), `CvGameDatabase_x64.dll` | defaults / tuned | 4.3 ms / 0.083 ms | 2.5 s / 0.047 s |

The C: rows with and without the Defender exclusion agree, so the cost is the disk's flush, not Defender (this also
corrects the "[I: Defender scanning each write of the journal]" of run r1 in
`research/05-graphics-composition/renderer-in-server-design.md`, whose run folder was on D:). The 0.28 s a write seen
live is above the 0.14-0.18 s measured here: the live run shared the disk with other instances and the DLL reads
between its writes [I]. Script of the first rows: `D:/c5-64bit-work/0129/sqlite_write_cost.py` (scratch).

**Fix (`server/src/remote/srv_remote_db.cpp`, `vpsrvdb::TuneCopy`, called by `SrvDbSetGame` before the DLL's first
write).** `PRAGMA main.synchronous = OFF` and `PRAGMA main.journal_mode = MEMORY` on the server's copy, reported as
`database: the server's copy tuned for scratch use (issue 0129): synchronous 0, journal_mode memory` in server.log. The
copy is scratch (made again from the engine's database at every boot, never read after the process ends), so a crash
can lose nothing that matters; with the journal in memory `ROLLBACK`, savepoints and `PullTables`' `ROLLBACK TO` still
work (journal_mode OFF would not). `main.` only: the attached `Localization` database is the engine's own file and
keeps its settings. The server's own calls are not on the DLL's hooked import slots, so the pragmas are neither
recorded nor replayed on the engine's database. The proxy (`proxy_db.cpp`) is unchanged: its side of a batch is
0.009-0.045 s live.

**Test (`server/tests/db/run_db_write_test.py`, offline, under a minute).** Builds `test_db_write_cost.cpp` against the
pin's `CvGameDatabase_x64` (since 0152 an import library made from the layout bin's own DLL; before, the stub import
library of the build_all `--deps`) with the
`0129 COPY TUNING` block of `srv_remote_db.cpp` extracted as its header, so the tuning tested is the server's text.
It makes one batch of 512 writes in the DatabaseRemapper form on a copy (tuned as the server does), records them as
`vpdb::Op` and serialises them as `kRtE_DbOps` carries them, replays them with `vpdb::Apply` on a second copy that
stands in for the engine's in-memory database, checks every result and the fingerprints, and fails above **0.5 s for
the batch** (about 1 ms a write: 25x the tuned time on the HDD, 4x below the untuned time on the NVMe). A control arm
without the tuning must fail the same limit (it stops as soon as it passes the limit). Run 2026-11-13 19:44 EST (again at 20:20 for the hand-back: tuned PASS 0.105 s, control FAIL with its first write at 0.66 s; the tuned time varies 0.04-0.11 s between runs on the shared HDD, 5-13x under the limit), pin
`x64-04b1211b-pa803776`, input 9047's server copy:
`python -I server/tests/db/run_db_write_test.py --out D:/c5-64bit-work/0129/dbtest-D --bin
<pins_dir>/dlls/x64-04b1211b-pa803776/bin --deps D:/c5-64bit-work/9045/bF/deps --src
D:/c5-64bit-work/9045/src-x64f --db <9047 run>/db/Civ5DebugDatabase.1.db`: tuned PASS 0.039 s (server 0.020 s + engine
apply 0.019 s), 0 results differ, fingerprints identical; control FAIL (4 writes in 0.54 s, projected 69 s); on C:
(Temp) tuned 0.093 s PASS, control FAIL (105 writes in 0.50 s, projected 2.5 s).

**Stable inputs (issue 0152, 2026-11-13) [L].** The command above needs one-off scratch (9045's deps and source, a
9047 run's database). Since 0152 the test needs only `--out`; the 0129 command line above still works unchanged:

`python -I server/tests/db/run_db_write_test.py --out D:/c5-64bit-work/0152/dbtest-D`

- `--bin` defaults to `<release of deps/dll.lock>/bin` (the rule of `tools/d10/run_d10.py`, 0133), so a re-pin moves it.
- `--deps` is no longer needed: the test writes a `.def` from the export table of the bin's `CvGameDatabase_x64.dll`
  (104 exports, none data, at `x64-04b1211b-pa803776`) and makes the import library with `llvm-lib /def`, so it names
  exactly the DLL the test runs with. `--deps` still selects a build_all `stubs/` library.
- `--src` defaults to the fork's include folders (build.py `INCLUDE_DIRS` without `CvGameCoreDLL_Expansion2`, which the
  test does not include) at `deps/dll.lock`'s `commit`, taken by `git archive` from `--fork` (default: build.py's
  `DEFAULT_SRC`, the long-lived clone `Community-Patch-DLL-x64`; read only, its checkout may be anywhere: on 2026-11-13
  it was at e9d0c4837 and holds 04b1211b8) into `<out>/src`, kept there while `<out>/src/.commit` names the pin.
- `--db` defaults to a database the test makes (`make_db`): 16 tables of 40 rows in the form of the tables the
  DatabaseRemapper updates first (`'ID' integer primary key autoincrement`, `'Type' text not null unique`, five more
  columns), a filler table without ID, page size 1024, journal DELETE, 7.7 MB, nothing from the game's files (D83).
  Shape taken from 9047's server copy [C, 2026-11-13]: 830 tables, 183 indexes, page size 1024, journal DELETE,
  6,817,792 B; its first ID tables by name (AICityStrategies, AIEconomicStrategies, ...) have that key and 0-303 rows.
- Made against real database, alternating, 2026-11-13 21:36-21:37 EST, same pin [L]: on D: tuned 0.040-0.076 s (made)
  vs 0.038-0.099 s (real), control 3-4 writes before 0.5 s (0.13-0.19 s a write) in both; on C: (Temp) tuned 0.080-0.081
  vs 0.10-0.18 s, control 27-36 writes (14-19 ms a write) in both. Same verdicts; the made database is the default.
  Evidence: `server/evidence/0152/acceptance.txt`. Unit test of the defaults (no build):
  `python -I -m unittest discover -s server/tests/db -p test_run_db_write_test.py` (5 tests).

**Live (acceptance) [L, 2026-11-13 19:53-20:17 EST].** Instance c5h-e (Goldberg, on D:, the rotating disk), DX11 EXE,
CP/VP/EUI, front end only: `proxy/tools/live/prepare.py c5h-e --menu` (activate the three mods, load nothing), one
launch per sample through the harness (start-up watch ok and guard check ok on every launch), stop after the front end
was idle with the activation's end in Lua.log. The time is the front end's mod activation: Lua.log `vp-dll-dev:
activating 3 mods` to the next `requested mods are already active`; the engine's own stopwatch.log line
`SetActiveDLCandMods` of the same activation agrees to 0.1 s in every sample. Arms: **S** stock, the pin's
instance-aware 32-bit DLL `i0ship-26234371-ia` (3C2F47DA...) in the CP slot, no proxy; **B** the proxy route with
server and proxy built from main a83f7e3ca (without the fix; server 99A0A592..., proxy C1347B3E...); **A** the same with
this branch's commit 0001801b2 (server 3144E6EA..., proxy 27E830D4..., `dirty=0`); both proxy arms with the pinned x64
DLL 74588135... (built with `--deps` of 9045's build_all of 04b1211b8; `D:/c5-64bit-work/0129/build_arms.sh`). Order
S, A, B, S, A; machine not quiet (no lock held; the quiet lock free, no registered jobs at the start).

| Arm | Samples: activation (s) | Engine "Perform Gameplay Database Post Processing" (s) | "Cache Game Database Data" (s) | Server: 2,559 writes of PerformDatabasePostProcessing done at |
|---|---|---|---|---|
| S stock | 55.6, 52.7 (mean 54.2) | 0.07, 0.06 | 9.96, 10.16 | (no server) |
| B before (main a83f7e3ca) | 561.2 | 499.28 | 8.14 | 501.1 s after the server's start; batches every 77-117 s |
| A after (0001801b2) | 53.0, 53.8 (mean 53.4) | 0.58, 0.56 | 7.60, 7.33 | 2.07 s, 2.17 s; batches 0.02-0.06 s apart |

**After / stock = 53.4 / 54.2 = 0.99** (stated factor: within 1.1x of stock; met); before / after = 10.5x. The whole
difference of B is the DLL's post-processing writes (499 s of its 561 s: 0.195 s a write, the HDD's flush), as the
headless test predicts. In A the proxy route still spends 0.5 s more than stock in post-processing (the 2,559 writes
recorded, sent in 5 batches and replayed, plus two fingerprint checks) and about 1 s more in "Reload GameCore DLL"
(the server's start), and 2.5 s less in "Cache Game Database Data" [I: the x64 DLL's caching is faster than the 32-bit
DLL's]. The vpipc "peer hung" of 9047 did not occur in B here (the EXE's heartbeat continued). The server reports
`result: FAILED: the engine is gone` after each harness stop: the stop ends the EXE without the orderly shutdown, as in
every run stopped this way. Screenshots of the main menu after each activation are in the Visual Checks feed (issue
0129); the first S and A samples have none (the shot was taken while the EXE was still busy activating: `PrintWindow`
timed out, a symptom that the first idle check fired too early; the series then waited for the activation's end).
Evidence (scratch, not committed): `D:/c5-64bit-work/0129/live/{S1,A1,B1,S,A}/` (logs-final/Lua.log, stopwatch.log,
server.log, vpproxy.log), summary `D:/c5-64bit-work/0129/summarize.py`, the series `series.sh` and its logs
`series2.log`, `series3.log`; c5h-e restored by hash afterwards (`restore.log`; the CP DLL put back to the 2E433216...
found at the start).

**Not verified.** Only the front end's activation: a save loaded through the proxy (9047's launch loaded one, and its
server copy also takes the map script scan's pull) was not timed here; the same writes go through the same
`SrvDbSetGame` copy, so the load's database phase should lose the same flush cost [I]. One sample of B (the bug is a
factor of ten, 9047's two launches showed the same). Not measured on a machine whose user folder is on an SSD only (the
fix there saves about 2 s per activation: 4 ms a write).

## 20. Extra mods and the mod selection changed in one EXE session (queue ticket 0153, account B, 2026-11-13)

(Added 2026-11-18 by the MC from ticket 0541, `../06-tooling-and-workflows/crash-ledger-audit.md` 8.1: the Squads run's end ("the EXE is gone during the deactivation ... no dump") is an access violation at absolute 0x7C3DA2EA at about 22:41:14.9 EST, after the stopwatch's `LoadCoreDatabase - Detach Loc Database` and before `Load Database`, with the proxy still loaded and its server attached; root cause open under D185, ticket 0545.)

**Question (release epic 3500 draft, row P11).** Extra mods reach the 64-bit side by design: the EXE rebuilds the game
database at every activation and the server copies it (section 11.7, 19). No run had shown it with a gameplay mod beyond
CP, VP and EUI, nor with the mod set changed twice in one EXE session.

**Set-up.** Instance c5h-c (Goldberg, C: NVMe), DX11 EXE, D33 product path as 0101/3311 (`[Renderer] ServerRenderer = 1`,
composite and manifest beside the proxy, D33 with Overlay on, `local_answers=1 local_verify=0 lua_mirror=0`); x64 DLL of
the D96 pin `x64-04b1211b-pa803776/bin` (74588135...), server.exe 950F81EB..., proxy EE5D59FE..., composite C3C3F9C2...
built from main 490bbfc3 (build lines `repo 490bbfc38 dirty=0`) on 9045's deps (`D:/c5-64bit-work/0153/build.sh`). The
front end is the vp-dll-dev MainMenu.lua with an empty configuration (it acts as stock: `Modding.ActivateDLC` on show);
every mod choice was made by clicks in the game's own Mods menu (MODS, the mod's tick box, NEXT), every game was a new
Tiny game (Continental Drift, Emperor) started from the Mods single-player set-up. Extra mods, read-only copies of the owner's
own MODS folder (nothing downloaded): **VernsTweaks (v 1)** (7a1c6eb1-...; two InGameUIAddin Lua files; SQL that sets
`Defines.MINOR_MAX_XP_VALUE = 999`, `CustomModOptions.SQLITE_LOGGING = 1` and others, and updates one `Language_en_US`
text) and **(4a) Squads for VP (v 1)** (3645dbca-...; `CustomModOptions.SQUADS = 1`, 3 `InterfaceModes` rows, a UI
add-in with 5 Lua files). Stock arm: the same instance with its own layout and the D96 pin's instance-aware 32-bit DLL
(`i0ship-26234371-ia`, 3C2F47DA...) in the CP folder, no proxy, no server. Driver `shell/evidence/0153/run_0153.py`
(3311's prep and restore; `prep-stock` and `--stock` for the stock arm), acceptance `shell/evidence/0153/check_0153.py`
(offline test `test_check_0153.py`, 12 of 12 cases as expected); logs and the server run folders
`D:/c5-64bit-work/0153/` (`live`, `ctl-p1`, `stock`, `live2`, `stock2`, `vp64-runs`).

**What works [L].** One server start and one database copy per activation that loads the CP DLL (8 activations over
three proxy-route EXE sessions; a deactivation loads the base game's DLL and starts no server). Every copy matched the engine's
database (`IDENTICAL` in server.log) and carried the selection: with VernsTweaks 832 tables, 43,833 rows,
`MINOR_MAX_XP_VALUE` 999, `SQLITE_LOGGING` 1; CP+VP+EUI alone 43,600 rows, 70 and 0 (two activations of the same set gave
the same digest f0bf71c1...); with Squads 43,603 rows and the 3 squad interface modes. In game the Lua channel showed 4
activated mods and the mod's values (`GameDefines.MINOR_MAX_XP_VALUE` 999; `GameInfo.InterfaceModes.INTERFACEMODE_SQUAD_MOVEMENT`
present). New games with an extra mod: 3 AI turns each, every `turns.py end/wait` exit 0, 0 LOUD lines. CP+VP+EUI alone on
the proxy route in one EXE session (pid 3484): activate, back to the main menu, activate again, a game of 3 turns, exit
to the main menu, activate again, a game of 3 turns with a quick save, exit (the deactivation went through): no failure, no
"locked" line in Database.log. Front-end activation times on this NVMe instance: proxy 33.1-52.1 s, stock 44.5-51.3 s
(stopwatch.log `SetActiveDLCandMods`; not a timing comparison).

**What fails, on both routes [L].** Exiting to the main menu after a game played with an extra mod, and then changing the
mod set, ends the EXE:

| Route, mod | Session | What happened after Exit to Main Menu |
|---|---|---|
| proxy, VernsTweaks (quick save at turn 3) | pid 57020 | the deactivation's rebuild logs `database Localization is locked` (DETACH) and `ATTACH ? AS Localization: already in use`; the next activation (VernsTweaks unticked) logs 62 `database table is locked`; the engine's database is half built (836 tables, 31,120 rows; VP's work tables `Leaders_NEW`, `Calendars_FIX`, `Notifications_new`, `Building_ThemingBonuses_new` left); the x64 DLL stops on a PRECONDITION in `CvDatabaseUtility::PrepareResults` (from `CvBuildingEntry::CacheResults`, `PrefetchGameData`, `CacheGameDatabaseData`; dump `vp64-runs/20261113-213927-57020-1/crashlogs/`) and the proxy ends the EXE (`on_server_death=exit`, exit 3) |
| stock, VernsTweaks (quick save) | pid 55936 | the deactivation logs the same `Localization is locked` and `already in use` lines; the EXE is gone during that rebuild (last stopwatch line `Notify Localization Database Updated`); no dump in `install/crashlogs` |
| proxy, Squads (quick save) | pid 18880 | the EXE is gone during the deactivation (last stopwatch line `LoadCoreDatabase - Detach Loc Database`); no lock line; server.log `result: FAILED: the engine is gone`; no dump |
| stock, Squads (quick save) | pid 11348 | the EXE is gone during the deactivation (last stopwatch line `Notify Localization Database Updated`); no dump |

*Corrected 2026-11-14 (queue ticket 0164, section 21): the two stock rows are not the stock game. The stock arm ran the
pin's instance-aware build `i0ship-26234371-ia`, whose instrument MemoryImports rewrites CvGameDatabase's MSVCR90
allocator import slots and leaves them pointing into the game DLL after it is unloaded; the EXE's end in those rows is
that instrument [C, dump]. With the plain pin `i0ship-26234371` the stock route goes through the same rebuild and the
next activation [L]. The stock Squads run also logged the two `Localization` lines (its Database.log in
`D:/c5-64bit-work/0153/stock2/out`). The proxy rows stand; their cause is open (section 21.4). *2026-11-14 (ticket 0174,
section 23): the proxy VernsTweaks row is explained and is not proxy-specific: CP's Lua `CPK.Misc.CustomModOptionEnabled`
leaves 8 statements on the engine's connection stepped and never reset; a rebuild that starts while they live cannot
DETACH Localization or DROP VP's work tables, the database is half built and the DLL's PrepareResults ends the game, on
the proxy route through the server (exit 3) and on the stock route with the plain pin through VP's own crash dialog [C,
dumps at the lock; L]. The Squads proxy row (no lock line) did not recur in 3 proxy runs and stays open.* Replaced text: "the proxy
route fails where the stock route fails, once one step later. The defect is in the EXE's rebuild after a game with an
extra mod, not in the proxy's database handling [L, 2 of 2 mods, each on both routes]."*

So the ticket's flow (mod on, game, mod off, game, mod on, save loaded) could not be completed on either route at the D96
pins as run here (the stock arm with the instance-aware build; see the correction above). Without an extra mod
the proxy route went through the same steps (above); the stock route without an extra mod was not run through them.
Acceptance JSON: `shell/evidence/0153/results/acceptance-run1-verns.json`, `acceptance-run2-squads.json` (both fail:
phases 1 of 3, the EXE gone).

**A second, proxy-only crash [L, once].** In the control session, a new game with VernsTweaks (after the CP+VP+EUI games)
ended 123 s after the server started: access violation reading 0x24 in `CvGameDatabase_x64.dll+0xe056`, under
`Database::Connection::Count("Months")` in `CvGameTextMgr::setDateStr` from `CvDllGame::SetTimeStr`, served on
channel 1 (`Dispatch_ICvGame2` in server.exe; symbolized with llvm-symbolizer against the pin's PDB). Run folder
`vp64-runs/20261113-220753-3484-1`. The earlier game start with VernsTweaks (pid 57020) and the stock one did not crash.
*Corrected 2026-11-14 (ticket 0165, section 20a): channel 1 is the core channel, not the UI channel; the call was nested
in `CvGame::update` -> `AutoSave` on the core thread; the cause is an unlocked statement cache in our x64
`CvGameDatabase` library. (Replaced: "served on channel 1 (the UI channel) ... [I] Two server threads used the DLL's
database connection at once (the UI channel's `SetTimeStr` while the core channel worked at game start); the stock route
has one thread.")*

**Side observations.** (1) `turns.py frontend` stays BUSY after a game with `SQLITE_LOGGING = 1`: stats.db keeps its
journal file (`journal_mode = PERSIST`, decision D35), which the idle check reads as an open transaction. (2) The harness
shot sometimes returns a frame without the EXE's UI: all the D33 clear colour 84,84,84 on the product path, or terrain
only, and on the stock route the world without UI; the next shot seconds later is normal (the frame PrintWindow caught,
not a rendering state). *Seen again 2026-11-14 15:21-15:32 by task 3702 (joined D33 product path, huge save copy
`C5H-0167-HUGE_0350`, builds of bc05e6bb: server.exe 16f55bc7, proxy 2f723054, composite 5bdae04c; c5h-b): 7 of the
9 shots at one still camera (5 the world without the EXE's UI or models, 2 all 84,84,84; 0 of the other 14 shots), and
1 of 13 on Tiny (all 84,84,84); 3702's driver now takes three candidate shots and keeps the one whose interface regions match the stock
reference (`shell/evidence/0167/3702/run_3702.py` `shot`, `overlay-system.md` 9).* *Confirmed 2026-11-16 by ticket 0401
against the presented frame: on the huge save, 30 PrintWindow captures each paired with the composite's back-buffer dump
gave 8 flat 84,84,84 and 2 UI-less captures, and 0 flat or UI-less dumps; the harness's `shot` now retakes a one-colour
capture (`research/05-graphics-composition/terrain-surface-verification.md` 13).* (3) On the product path the load screen of a new game showed `TXT_KEY_VPUI_TITLE_TIP` and
`TXT_KEY_VPUI_TIP_4` untranslated and the world drawn through it (control session, after 6 activations); not checked on
stock.

**Not verified.** Why the EXE dies in the rebuild (no dump: the EXE wrote none, and WER LocalDumps is a system setting);
whether the 32-bit DLL would survive the half-built database; the stock route with CP+VP+EUI only through the same steps;
the cause of the proxy-only crash (since found: section 20a); the third phase (a save loaded after re-activation) on any
route. *(2026-11-14: the stock route's end is explained in section 21; the rest stands.)*

### 20.1 The Squads row on the batch-4 product route (ticket 0545, 2026-11-18, account B)

**Question (D185).** 0153 run 2's end (proxy row "Squads" above; `../06-tooling-and-workflows/crash-ledger-audit.md`
8.1.1: an access violation at absolute 0x7C3DA2EA about 1 s after `LoadCoreDatabase - Detach Loc Database`, the proxy
still attached, no dump, module not named) returned in none of 0174's 3 proxy runs. Does it return on the current pins,
and if so where?

**Set-up** [C, files; 2026-11-18]. Goldberg instance c5h-i (F:), DX11 EXE (04CFDC71...), windowed 2560 x 1440
(the instance's GraphicsSettingsDX11.ini, as 0153's 2554 x 1411 client area). Batch-4 pins (COMMON 18): server.exe FC73E60E..., x64 DLL 2B56A1CD..., proxy 3DFD7521... from the pin layout
`x64-c29442e8-p58724e0`; that layout carries no compositor (pack.py's "thin"), so the compositor was built from this
repository at the same commit 58724e072 (`tools/harness/evidence/0545/build_0545.sh`: shell_composite.dll CFA7D92E...,
the commit's shell_composite.ini C1E03D63...). The plain 32-bit pin `i0ship-e96457fd` (3737AE10...) set into the CP
folder by `provision.py set-dll` before `set-proxy`, so no instance-aware build is in the instance (0164's mechanism is
excluded). Product prep as 0153's (3308/3311's: the composite and its manifest beside the proxy, `VP64Options.ini
[Renderer] ServerRenderer = 1`, D33 by darkred.py with Overlay on, vpproxy.ini `mode=on local_answers=1 local_verify=0
lua_mirror=0`), every changed file and the instance's auto and quick saves backed up by SHA-256. (4a) Squads for VP is
in the instance from `provision.py align548`. The front end is the vp-dll-dev MainMenu with an empty configuration; every
mod choice by clicks in the game's Mods menu, the game a new Tiny game (Continental Drift, Emperor), 3 AI turns through
the Lua channel (`turns.py`), Esc, Quick Save, Esc, Exit to Main Menu, Yes. Driver `tools/harness/evidence/0545/run_0545.py`
(0153's driver imported) and `seq.sh`. Two watch forms: (a) 0164's `cdb_watch.py` attached to the EXE with its
breakpoint on `ntdll!NtTerminateProcess` removed and no hardware breakpoint (the ticket; 23.4: a debug register ends
the EXE through its DRM); with a debugger attached the proxy's chained filter (0419) and WER do not run, so cdb's
second-chance dump is the evidence; (b) no debugger: the proxy's `exe-crash.txt` and the harness's `crash.collect`
(0543's dump at WER, 0548's at a fatal box) through the dialog watcher. Work folders `C:/c5-64bit-work/0545/rN/`.

**Runs** (filled as they end):

| Run | Watch | Keys | Flow | End |
|---|---|---|---|---|
| r1 (pid 41128, launch 13:02:09) | cdb (a), attached 13:02:24 | product | Mods: Squads ticked (CP, VP, EUI already on), NEXT 13:05; Tiny game 13:09; turns 0-3 (5.0-5.9 s each); Quick Save 13:12:20 (255,164 B); Exit to Main Menu, Yes 13:13:23 | **through** [L]: the deactivation ran to `SetActiveDLCandMods` (29.5 s) and the main menu; vpproxy.log `shutdown: SetDLLIFace(NULL) forwarded`, server exit 0, `detach: after the orderly shutdown`; server.log `result: passed`. cdb saw no second-chance exception; it logged 149 first-chance C++ exceptions (e06d7363), 12 first-chance access violations and 1 break over the session, all handled (with `sxd` no context is kept, so where and when is not known) |
| r2 (pid 51428, launch 13:27:12) | cdb (a) + first-chance AV log (`run_0545.py` AV1), attached 13:27:30 | product | Squads still ticked (the menu keeps the last ticks); NEXT 13:29:05; Tiny game 13:30:39; turns 0-3 (4.9-7.7 s); Quick Save 13:32:46 (264,723 B); Exit to Main Menu, Yes 13:33:13 | **through** [L]: deactivation and main menu; `shutdown: SetDLLIFace(NULL) forwarded` at proxy 250.59 s (13 s after the Yes), `detach: after the orderly shutdown`; server `result: passed`. The 12 first-chance AVs of the session (11 + the one cdb counts at attach) are **all at one EXE instruction**, eip 0x00773DCB with the EXE at 0x00250000 (VA 0x923DCB at ImageBase 0x400000), on the main thread, each handled by the EXE [C, cdb register dumps]: not the fault of 0153. This deactivation also met
0174/0184's CPK lock (Database.log: `database Localization is locked` 2, `already in use` 2, no `table is locked`;
the rebuild went on with the old Localization, as 0174 r1); the other five runs logged none |
| r3 (pid 43556, launch 13:38:01) | none (b): proxy filter, dialog watcher, crash.collect | product | as r2; Yes 13:43:27 | **through** [L]: `shutdown: SetDLLIFace(NULL) forwarded` at proxy 241.36 s (10 s after the exit's view-mask line at 231.40), orderly detach; server `result: passed`; no `exe-crash.txt`, no box, no Goldberg record |

**0153 run 2 did not run the flow it is described with** [C, `D:/c5-64bit-work/0153/live2/session.log` and its shots
09-14, read 2026-11-18]. After the Quick Save (22:35:42) its driver pressed Esc (22:35:58), which closed the in-game
menu, so the next "Exit to Main Menu" click (0.4996, 0.5149, 22:35:59) and "Yes" click (0.4996, 0.5092, 22:36:06)
landed on the map, on the Russian Settler's plot (shots 09 and 10: still in game, the Settler selected, its plot
tooltip shown). The game then sat at the human turn for about 3 min; Esc at 22:39:27 opened the menu, Exit to Main
Menu at 22:40:20, and the confirmation stood 42 s until Yes at 22:41:02; the fault followed 12.6 s later. The real
tail therefore had two left clicks on the map with (4a) Squads' UI add-in active (Squads takes mouse input for its
group selection and movement modes) and about 5 min in game after the save. 0174's runs and r1-r3 here exited at once.
Timing also differs: in 0153 the deactivation's `Release Database Cache` came 11.6 s after the Yes (stopwatch anchored
on `Cache Game Database Data` = proxy 15.96 s), in r1 5.9 s. The fault fell 0.04-1 s after `Detach Loc Database`
(0541's anchoring confirmed). From r4 the runs replay that tail (`seq.sh tail53`).

| Run | Watch | Keys | Flow | End |
|---|---|---|---|---|
| r4 (pid 72156, launch 13:47:41) | none (b) | 0153 (3311's test keys: tap, lockstep CSV, camera, depth) | NEXT, Tiny game, turns 0-3, Quick Save; then 0153's tail: Esc 13:53:03, map clicks 13:53:05 and 13:53:14 (shot: the Settler selected, its plot tooltip, as 0153's shot 09), 175 s in game, Esc, Exit to Main Menu, confirmation 40 s, Yes about 13:57:40 | **through** [L]: deactivation to `Discovering Base Game Maps`, main menu; `shutdown: SetDLLIFace(NULL)` at proxy 520.25 s (11 s after the exit's view-mask line at 509.22), orderly detach; server `result: passed`; no `exe-crash.txt`, no box. The game's teardown was faster than 0153's: `EUI_context: Shutdown` 2.8 s after `InGame: Shutting down` (0153: 8.0 s) |
| r5 (pid 24100, launch 14:01:43) | cdb (a) + AV1, attached 14:01:49 | 0153 | as r4 (0153's tail); a non-invasive address probe in game during the 175 s wait (`addr_probe.sh`) | **through** [L]: no second-chance exception; again 12 first-chance AVs, all at eip 0x00773DCB (EXE at 0x00250000); `shutdown: SetDLLIFace(NULL)` at proxy 516.08 s, orderly detach; server `result: passed` |
| r6 (pid 67752, launch 14:16:00) | none (b) | 0153 | as r4 (0153's tail); stray clicks 14:24:40 and 14:24:51, Yes 14:29:27 | **through** [L]: deactivation and main menu; `shutdown: SetDLLIFace(NULL)` at proxy 523.83 s, `detach: exe-crash filter: removed`, orderly detach; server `result: passed`; no `exe-crash.txt`, no box |

Every run ended by the main menu's own EXIT and Yes (the harness stop refuses at the front end after a game: "Lua
channel silent ... not a front end", r1, 300 s), exit code 0, `cli.py leftovers` clean.

**The fault address belongs to no module [C for today's sessions; I for 0153's].** What the facts allow:
- The Goldberg printer's address is the exception address (EIP): its other records are `KERNELBASE!RaiseException`
  (0x75D4BAE4, 0x75E8BAE4 for the 0xE06D7363 records) and EXE+0x8E81C3 relocated (0x012881C3, EXE at 0x00DA0000).
- In 0153's session the EXE was at 0x00940000 (`vpproxy.log`, view-mask line) and the compositor at 0x7AC50000-0x7B29B000.
  In r5, the same flow, a non-invasive probe in game (`tools/harness/evidence/0545/addr_probe.sh`, 14:07) showed the
  compositor as the highest image (0x7A180000-0x7A83F000), the proxy at 0x776D0000-0x778F9000, the system DLLs at
  0x76000000-0x77660000, and **nothing executable above the compositor**; 0x7C3DA2EA itself lay in a private
  PAGE_READWRITE allocation (base 0x7B810000, 524 KB committed, heap-like); the only private executable memory in the
  process was one RWX page at 0x50020000 and a 64 KB execute-read region at 0x6D7A0000, both far below
  (`tools/harness/evidence/0545/r5-address-probe.txt`). The compositor is loaded by the proxy after the proxy itself, and nothing loads
  after it until the deactivation (r1's cdb module events), so in 0153's session too the region above 0x7B29B000 most
  likely held no image.
- So the EXE process most likely **executed a non-code address**: a call, jump or return through a stale or overwritten
  code pointer (a function pointer, a vtable slot or a return address) landed in heap or unmapped memory, which DEP
  faults at the target itself. No module "owns" 0x7C3DA2EA; the module that made the transfer is not known.
- Ruled out as the owner: the EXE image (0x00940000 + 0x06519000 ends 0x06E59000), the compositor (0541), the base
  game's DLL (it is mapped only after the proxy's `SetDLLIFace(NULL)` and unload, r1's cdb order, and 0153 never logged
  that call), the instance-aware 32-bit DLL (none in the product route), the CPK statement lock (no lock line; 23.3).
  The proxy (0541's nine candidates) stays unlikely: seven candidates are instruction boundaries, but none of them can
  fault by itself (register moves, pushes and pops, a stack store, a call; `proxy-candidates.txt`).
- What would confirm the [I] and name the caller: the exception's first parameter (8 = execute) and the stack at the
  fault. On a Goldberg instance neither reaches a file today: the proxy's filter (0419) records the code, "an address in
  no module (0x...)" and the access address only (no stack, D83/D119); and 0153's crash left no Application Error
  (1000) event (the workstation's Application log has none for CivilizationV_DX11.exe after 2026-11-11 01:52 EST,
  though Goldberg recorded game crashes on 11-13 to 11-18 [C, `wevtutil`, 2026-11-18]), so the process ended without WER
  and 0543's dump at WER would not have run [I: Goldberg's printer or the EXE's own filter ends it]; only a debugger
  attached beforehand (r1, r2, r5's form: no breakpoint, no debug register) keeps a dump.

**Status (D185, 2026-11-18).** The end did not return in 6 runs on the batch-4 product route, 3 of them with 0153's
real tail and its composite keys. Root cause not determined; the module is "none: an execute fault at a non-code
address" [I]. Differences left between 0153 run 2 and these runs: the build (0153: proxy, server and compositor of
490bbfc38 on the D96 pins; here 58724e072 on batch 4), the game's teardown (0153's `EUI_context: Shutdown` 8.0 s after
`InGame: Shutting down`, here 2.8 s; its deactivation began 11.6 s after the Yes, here about 6 s), the instance (c5h-c on
C:, here c5h-i on F:) and the boot. **Open question:** which code pointer the EXE process followed into 0x7C3DA2EA during
`LoadCoreDatabase - Load Database` after a game with Squads on 0153's build, and whether batch 4 still has it. Proposal
for the MC in ticket 0545's log.

*MC note 2026-11-18 15:02:* the proxy's filter already writes the access kind for an access violation (`access: read of / write to / execution at <address>`, `proxy/src/proxy_crash_filter.cpp`, ticket 0419), so the proposal's first form needs no change: a sighting on a current build where the filter runs names an execute fault directly. 0153 run 2's own record predates the line or did not reach it. Whether the filter runs at all when the process ends without WER on a Goldberg instance is not known [I].

## 20a. The server's crash in `Database::Connection::Count`: two threads on one kept statement (ticket 0165, 2026-11-14)

**The dump** [C, `cdb -z` on `CvMiniDump_20261113_220955_5.4.8-51-g04b1211b8_Release.dmp` of section 20 with the 0153
build's PDBs `D:/c5-64bit-work/0153/bin` (server.exe 950F81EB..., DLL 74588135..., CvGameDatabase_x64.dll 75045A87...);
scripts `server/evidence/0165/dump*.sh`, output `server/evidence/0165/dump-threads.txt`].

| Thread (server pid 8864) | Where at the dump |
|---|---|
| 4 (0xc7ec), the **core** channel's service thread | faulting: `sqlite3_column_int+0x86` <- `Database::Connection::Count+0x5cd` (Database.cpp:220) <- `CvGameTextMgr::setDateStr` (CvGameTextMgr.cpp:69, `DB.Count("Months")`) <- `CvDllGame::SetTimeStr` <- `Dispatch_ICvGame2` <- `SrvRemote::OnCall` <- `vpipc ... wait_return` <- `SrvRt::End` <- `RemoteGen_ICvEngineUtility4::AutoSave` <- `CvGame::update` (CvGame.cpp:1678) <- `Dispatch_ICvGame2` (Update) <- `ServiceThread`. So `SetTimeStr` was the EXE's nested call back into the DLL from inside the engine's `AutoSave`, which the DLL called from `Update` on the core thread (the initial autosave of the new game, engine-contract 1.8/1.10) |
| 3 (0x1744), the **ui** channel's service thread | `EnterCriticalSection` in `SrvRemote::LockSync` (srv_remote_objects.cpp:426): waiting for the game-core lock that the core thread held (the proxy log: the ui thread's last call was `rt:LockSync` on channel 0, the core thread's `ICvGame1::SetTimeStr` on channel 1) |
| 5 (0xb220), the aux channel's service thread | idle in `next_message` |
| 0, 1, 2, 6, 7-62 | main (`RemoteMain`), guard watchdog, vpipc monitor, `vp-render`, driver and thread-pool threads: no DLL or database frame |

The proxy log names the channels: thread 59188 (the EXE's boot thread) = `ui` = channel 0, thread 42032 (the game-core
thread, `+0x42A360`) = `core` = channel 1. Section 20 called channel 1 the UI channel; that was wrong.

**What the registers prove** [C, disassembly of `sqlite3_column_int` in CvGameDatabase_x64.dll 75045A87... against
SQLite 3.7.17's `columnMem`]: the function loads `pVm->pResultSet` (offset 0x28), finds it non-NULL and `i <
nResColumn`, enters `db->mutex`, then **reloads** `pResultSet` and computes `&pResultSet[0]`: the reload gave 0 (rax = rdi =
0, fault reading `[0 + 0x24]` = `Mem.flags`). The thread's own `sqlite3_step` had just returned `SQLITE_ROW` (Count's
`cmp eax,64h` before the call). In 3.7.17 `pResultSet` is set to 0 only by `sqlite3VdbeExec` (a step), `sqlite3VdbeList`
and `Cleanup` (a reset or finalize), all under `db->mutex` (sqlite3.c 61090, 61611, 65900). So **another thread stepped,
reset or finalized the same `sqlite3_stmt` while thread 4 waited for the connection mutex** between its step and its
column read. No second thread was in the database at the moment of the dump (the dump is written after the fault).

**The cause** [C code, I which DLL call was the second user]. `Database::Connection::Count(table, bCache)` (our
`tools/x64-dll/cvgamedatabase/src/Database.cpp`, written to the closed library's behaviour) keeps one prepared `select
count(*) from <table>` per table in `m_hshCountStatements` and the last value in `m_hshCountValues`, with no lock; the
value is reused only while `sqlite3_changes()` is 0, which is false in a game after the DLL's post-processing writes, so
every `Count` resets and steps the kept statement. SQLite's serialized mode locks each API call, not the sequence
reset/step/column/reset, and not the two `std::tr1::unordered_map`s. Two threads that `Count` the same table at once
therefore share one statement: one resets or steps it while the other reads its row. `Count("Months")` is reached from
`setDateStr`/`getTurnMonthForGame`/`getTurnYearForGame`, i.e. from `SetTimeStr` and from the Lua functions
`Game.GetTurnString`, `Game.GetDateString`, `Game.GetTimeString`, `Game.GetGameTurnYear` (CvLuaGame.cpp 871-893, 2732,
4224; CvGameCoreUtils.cpp 1577, 1595) that the UI's Lua calls. The server runs the DLL on one service thread per channel,
and the EXE's UI thread calls the DLL while the core thread is inside `Update` (engine-contract 2: "Update runs on the core
thread while the UI thread keeps calling the DLL"); a UI-Lua call crosses as `kRt_LuaCall` on the ui channel and runs
there with the lock depth of the EXE thread's live wrappers (section 16.1), which can be 0. The second user in the dump
was most likely such a UI call on the ui thread just before it asked for the lock [I: it is not on any stack at the dump;
the ui thread is the only other thread that runs DLL code, and the aux thread was idle]. Why VernsTweaks and only once in
the 0153 starts: a race of a few microseconds at the turn-0 autosave; VernsTweaks' part (if any) is not shown [I].

**Fix** (2026-11-14, `Database.cpp`): one process-wide recursive lock (`CountLock`, a `CRITICAL_SECTION` made when the DLL
loads) around every use of the kept statements and the two maps: `Count`, `ClearCountCache` and `Close` (whose
finalize loop also finalizes the kept statements). No member is added (the fork's `Database.h` fixes
`Connection`'s layout, and the game DLL builds `Connection` objects itself, CvGame.cpp:1317). Lock order: this lock, then
SQLite's connection mutex; SQLite never calls into the library under its mutex, so no inversion. The game DLL is unchanged.
Whether the closed 32-bit library has the same race was not checked (its decompile notes in
`tools/x64-dll/cvgamedatabase/README.md` name no lock) [I]; the stock route has not been seen to crash here.

**Headless reproduction** (`tools/x64-dll/cvgamedatabase/tests/run_count_threads.py`, program `count_threads.cpp`): one
`Connection` on a scratch file with a 12-row `Months` table and one row-changing `UPDATE` (so every `Count` steps), N
threads calling `Count("Months")` for M ms. [L, 2026-11-14 02:38, x64 release builds of the library on the D96 fork
pin's headers, 4 threads x 2000 ms x 5 rounds] **before the fix** (HEAD's `Database.cpp`, DLL a5719e79...): 5 of 5 rounds
died with 0xC0000005 within 0.2-2.9 s; under cdb (2 threads) the fault is `sqlite3_column_int+0x86` <- `Count+0x5cd`
(Database.cpp:220), **the same instruction as the game's dump**. **After the fix** (DLL 9c37b39e...): 5 of 5 rounds
passed, 17,316-26,419 calls per round, every one 12. Evidence `server/evidence/0165/test-two-threads.txt`,
`test-old-under-cdb.txt`.

**Save identity with the fixed library** [L, 2026-11-14 02:40-02:49, `tools/d10/run_d10.py --games tiny,standard
--allow-subset --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, x86 `i0ship-26234371` f92881a3..., x64 = the
D96 pin bin `x64-04b1211b-pa803776` with only `CvGameDatabase_x64.dll` replaced by the fixed 9c37b39e...]: **PASS**, tiny
227 s and standard 547 s, every part; the statistics rows of the identical pairs equal the pin's own reference (9045's
`d10-final-report.txt`: tiny 30,666 / 27,008 / 26,644, standard 351,496 / 327,041 / 341,206). Huge not run.
Report `server/evidence/0165/d10-fixed-db-report.txt`.

**Ten new-game starts with VernsTweaks on the product path, fixed library** [L, 2026-11-14 02:50-03:18 EST, c5h-c (C:
NVMe), driver `shell/evidence/0165/run_0165.py` on 0153's prep/stop/restore]: the crash's own build (server.exe 950F81EB...,
proxy EE5D59FE..., composite C3C3F9C2..., all from main 490bbfc3 by 0153; x64 DLL 74588135... of the D96 pin) with only
`CvGameDatabase_x64.dll` = 9c37b39e...; D33 product path as 0153. Each start a fresh EXE whose front end (frontend.prepare
`newGameSetup`) activated CP, VP, EUI and VernsTweaks and started a new Tiny Continental Drift game at Emperor; in game
(Lua: 4 activated mods, `GameDefines.MINOR_MAX_XP_VALUE` 999), 2 AI turns (turns 0 and 1), harness stop. **10 of 10 ok**:
no dump in any server run's `crashlogs`, no `PeerDied`/`FATAL` in any `vpproxy.log`, the database copy IDENTICAL at all 6
checks of each start. Per start the crash's paths ran: `AutoSave` 3 and `SetTimeStr` 5 times (core), the UI's
`Game.GetTurnString` 46-53 and `Game.GetGameTurnYear` 2 times on the ui channel; in **5 of the 10 starts one
`Game.GetTurnString` ran on the ui channel while an impure call (`Update`) was in flight** (`proxy-lua-calls.tsv` phase
`in_flight`), i.e. the UI does `Count("Months")` during `Update`, the overlap the dump requires [L]. Evidence
`shell/evidence/0165/results/` (`summary.out`, `series-verdicts.json`, `session.log`, `restore.json`: every backed-up
file equal, `provision.py verify c5h-c` rc 0).

What ten clean starts do and do not show: the crash was seen once in 0153's starts, so ten clean starts alone would not
separate the fix from luck; the proof of the mechanism is the dump plus the two-thread test (same instruction, 5 of 5 before,
0 of 5 after). No product-path control with the old library was run.

**Not verified**: which UI call was the second user in the dump (no stack; `Game.GetTurnString` during `Update` is shown live above); the stock route under the same load; any other shared
state of the DLL that two server threads can reach at once (the DLL's own caches are outside this fix).

## 21. The stock route's end in the mod rebuild is the instance-aware build's MemoryImports (queue ticket 0164, 2026-11-14)

**21.1 Set-up.** Instance c5h-a (Goldberg, C: NVMe), DX11 EXE, stock route: the instance's own layout (no proxy, no
server), the pin's 32-bit DLL in the CP folder (backed up and restored by SHA-256), VernsTweaks (v 1) copied read-only
from the owner's MODS folder, the vp-dll-dev front end with an empty configuration, every choice made by clicks as in 0153
(section 20): Mods, tick, NEXT, Single Player, Set Up Game, Tiny, Start Game; Esc, Quick Save, Esc, Exit to Main Menu,
Yes. Before the exit an invasive cdb (Windows SDK x86) was attached by `shell/evidence/0164/cdb_watch.py`: a full dump
and all stacks on any second-chance exception and on the first `ntdll!NtTerminateProcess`, first-chance exceptions only
logged, `-pd` so that ending cdb detaches (tested live on throwaway processes: an unhandled AV, an `_exit`, a detach
that leaves the target running). Driver `shell/evidence/0164/run_0164.py` (0153's driver with `dll install|restore
--pin ia|plain` and `cdb`). Evidence (scratch, deleted at the hand-back except the notes below):
`C:/c5-64bit-work/0164/{s1,s2,s3}`.

**21.2 Run s1: the pin's instance-aware build `i0ship-26234371-ia` (3C2F47DA...) [C, dump, 2026-11-14 03:24].** Three AI
turns, quick save, Exit to Main Menu: the deactivation's rebuild logged `database Localization is locked` (twice) and
`ATTACH ? AS Localization: already in use`, as in 0153, then the EXE took a second-chance access violation 13 s after
the click (dump 1.95 GB). Order of events in the debugger log: `Unload module ...MODS\(1) Community Patch\
CvGameCore_Expansion2.dll at 7fff0000`, `ModLoad ...Assets\DLC\Expansion2\CvGameCore_Expansion2.dll` at 7fff0000-806f9000
(unloaded and loaded again), then the fault:

- `eip = 0x80da13c0`, in no loaded module; the unloaded-module list has the CP DLL at 7fff0000-81513000, so the address
  is code of the game DLL that was just unloaded.
- Stack: `LoadLibraryW` from the EXE (0x571782 at base 0x400000) -> loader -> the base game DLL's `_DllMainCRTStartup`
  -> `_initterm` (its C++ static initialisers) -> a CvGameDatabase function at 0x78da7a60 (`push 28h; call 78e16ad6`, an
  allocation of 40 bytes) -> `78e16ad6: jmp [78e182b4]`, CvGameDatabase's import slot for MSVCR90 `operator new`
  (`??2@YAPAXI@Z`) -> 0x80da13c0.
- CvGameDatabase's MSVCR90 import slots at the fault (`dps 78e18240`, names from `!dh -i`): `_malloc_crt`, `realloc`,
  `operator delete`, `malloc`, `free`, `operator new`, `operator delete[]` all point into 0x80da0af0-0x80da13c0 (the
  unloaded DLL); every other slot points into msvcr90.

Who wrote them: `MemoryImports::Install()` (`MemoryImports.cpp` of `ia-ee893bc18-440878983.patch`, the instance-aware
instruments of the `-ia` builds), called from `DllGetGameContext`, rewrites every loaded module's MSVCR90 allocator
import slots (malloc, calloc, realloc, free, aligned, operator new/delete and the array forms) to thunks inside the game
DLL. It is on unless `VP_MEMIMPORTS=0`. The DLL's `DLL_PROCESS_DETACH` undoes only `EngineAllocTrace` (its own slots),
not MemoryImports, so when the EXE unloads the game DLL at a mod change, every module's allocator slots point at freed
code, and the first allocation through one ends the EXE. The EXE wrote no dump because the fault is a plain AV that the
EXE's filter did not record (none in `install/crashlogs`, none in 0153 either). This confirms the hazard the knowledge
base listed as possible (`../02-exe-internals/allocations-and-memory-map.md` section 3, "Limits").

**21.3 Run s2: the plain pin `i0ship-26234371` (F92881A3..., no instruments) [L, 2026-11-14 03:41-03:53].** Same flow
(turn 0: the plain build has no Lua channel, so `turns.py` cannot drive AI turns; the game was started, restarted once
by a mis-click on Restart Game, quick-saved and left by Exit to Main Menu under cdb): the deactivation went through
(`SetActiveDLCandMods` 27.7 s; Detach Loc 0.05 s, GenerateMergedLocalizationDatabase 2.6 s, Attach Loc 3.7 s, Reload
GameCore DLL 3.6 s), no exception, no `Localization` line in Database.log; then CP+VP+EUI without VernsTweaks was
activated in the same session (34.1 s), and the EXE was ended by its own main-menu Exit.

**21.4 Run s3: the `-ia` pin with `VP_MEMIMPORTS=0` [C, dump, 2026-11-14 03:55-03:59].** MemoryImports off (checked
live: CvGameDatabase's allocator slots read with `cdb -pv` pointed into MSVCR90), three AI turns, quick save, Exit to
Main Menu: `database Localization is locked` (twice) and `already in use` again, the rebuild then went further than s1
(the base DLL loaded, `Notify Localization Database Updated` 1.2 s, `Reload GameCore DLL` 4.7 s), and 15 s after the click
the EXE took a second-chance access violation at 0x80d7fc50, again inside the unloaded CP DLL: stack `lua_cpcall` from the
EXE (0x9f4e52) -> `lua_pcall` -> a Lua allocation -> 0x80d7fc50, which the `-ia` PDB names
`MemoryDiagnostics::LuaAllocHook` (`MemoryDiagnostics.cpp`:1537). `MemoryDiagnostics::InstallLuaAllocHook()`, called from
`LuaSupport::InitLuaFramework` (from `CvDllDatabaseUtility::CacheGameDatabaseData`, i.e. at every activation that loads
the DLL), puts `lua_setallocf(L, LuaAllocHook, ud)` on the engine's Lua universe, shared by every
Lua state of the EXE, with no switch and no uninstall, so the front end's next Lua allocation after the unload ends the
EXE. The s1 targets, named with the same PDB: `ThunkOperatorNew<2>`, `ThunkMalloc<2>`, `ThunkRealloc<2>`, `ThunkFree<0>`,
`ThunkOperatorDelete<0>`, `ThunkOperatorDeleteArray<0>` (`MemoryImports.cpp`) [C]. So an `-ia` game DLL cannot be
unloaded once it has cached the game database, whatever `VP_MEMIMPORTS` says [I for the case without a game: every
run here had played one].

**21.4a Where the `Localization is locked` lines come from [L, 5 runs, 2026-11-14].** The lines are SQLite refusing
`DETACH Localization` while that database is still in a read transaction on the engine's connection (an unfinished
statement); the rebuild then keeps the old Localization attached (`ATTACH ...: already in use`) and goes on. They appeared
in exactly the runs whose game was driven through the Lua channel (`turns.py end`/`wait`, the driver's state and mod
queries, three AI turns), and in none of the runs without it:

| Run | DLL | Lua channel and AI turns | Lock lines | End |
|---|---|---|---|---|
| s1 | `-ia` | yes, 3 turns | yes | AV in `ThunkOperatorNew<2>` (MemoryImports) |
| s2 | plain | no (no channel), turn 0 | no | went through; next activation went through |
| s3 | `-ia`, `VP_MEMIMPORTS=0` | yes, 3 turns | yes | AV in `LuaAllocHook` |
| s4 | as s3, plus a full Lua GC (`collectgarbage("collect")` twice) through the channel before the exit | yes, 3 turns | yes | AV in `LuaAllocHook` |
| s5 | `-ia`, `VP_MEMIMPORTS=0` | no channel use, turn 0 (started by clicks) | no | AV in `LuaAllocHook` |

So the lock is not caused by the extra mod alone nor by the `-ia` instruments; it follows the driver's Lua.
*Corrected 2026-11-14 (ticket 0174, section 23): the lock lines do not follow the driver's Lua. They appear on both
routes with no Lua channel use (proxy r1, r2; stock with the plain pin s1), and their cause is CP's Lua helper
`CPK.Misc.CustomModOptionEnabled`, whose 8 never-reset statements keep Localization in a read transaction (dump at the
lock, 23.3a). The `turns.py` candidate below is replaced. The correlation in this table was chance in which UI path
each run took.* A candidate
[I, not tested]: `tools/harness/turns.py`'s `END_TURN_CHUNK` leaves the loop `for t in GameInfo.Technologies() do ... break
end` early when no research is set (turn 0), which can leave that statement unfinished; any Localization read made while
it is open then keeps Localization in the read transaction. A full Lua GC did not clear it (s4). Not separated: the
channel's own runner versus the AI turns versus that loop. Whether the extra mod is needed at all: 0153's control session
(CP+VP+EUI only, same driver, proxy route) logged no lock line; not repeated here.

**21.4b Evidence.** Text excerpts (debugger logs around each fault, the import slots in s1, the live slots in s3, the
lock lines and stopwatch steps of every run): `shell/evidence/0164/excerpts/`. Dumps and run folders were scratch
(`C:/c5-64bit-work/0164/`, deleted at the hand-back). Screenshots: Visual Checks feed, issue 0164 (diagnostics).

**21.5 What this means.**
- The stock route at the D96 pins does not end in this rebuild; the end that 0153 and s1 saw on the stock route is a
  test-build artefact of the `-ia` DLL. The owner's own install and the shipped patch do not carry MemoryImports [C: deps/i0.lock
  says the i0-ship builds are upstream-clean, and s2 ran the plain build].
- Every test instance that runs a `-ia` build of the 32-bit DLL and changes the mod set after a game (or otherwise makes
  the EXE unload the game DLL) dies the same way. The headless host's crash at shutdown with MemoryImports on
  (`headless-host.md`, "cause not investigated") is the same stale-slot fault at the DLL's unload (confirmed by dump, ticket 0484, 2026-11-17: `ThunkOperatorNew<2>` after c5h's `FreeLibrary`, `headless-host.md` section 5; it read "very likely ... [I]").
  The windowed EXE's own exit does it too [C, cdb, ticket 0483, 2026-11-17 04:29, c5h-f, no RenderDoc]: after
  `UI.ExitGame()` the EXE unloads the CP DLL, then its main thread allocates a `std::string` (MSVCP90 `operator new`
  from `CivilizationV_DX11+0x125c2c`) and faults in `ThunkOperatorNew<5>` of the unloaded DLL; without a debugger the
  process ends 0xC0000005 about 1 s after the request with no box and no crash log. So every UI.ExitGame or Exit to
  Windows of a stock `-ia` game ends this way (`../06-tooling-and-workflows/scene-catalogue.md` 12).
  `VP_MEMIMPORTS=0` is not enough (s3: the Lua allocator hook has no switch). Ways out: run any test that changes the
  mod set after an activation with the plain build (`i0ship-<pin>`), or make MemoryImports and the Lua allocator hook
  undo themselves at `DLL_PROCESS_DETACH` (as EngineAllocTrace does; a change on the fork's instruments, outside this
  ticket).
- The proxy route's end in 0153 (section 20) is not explained by this: the proxy DLL rewrites no import slots (no
  `FirstThunk`/`VirtualProtect` on another module in `proxy/src`, 2026-11-14 [C, by grep]). Its VernsTweaks failure
  started from the same `Localization is locked` lines, which here follow the test driver's Lua (21.4a), so it may be a
  test artefact as well [I]; the Squads proxy run ended at `Detach Loc Database` with no lock line and no dump, cause
  unknown. Next step for it: 0153's proxy flow with `cdb_watch.py` on the EXE (and the server), once without the Lua
  channel (clicks only), once with it.
- For the patch: nothing on the shipped stock route needs guarding against this end (the plain build goes through);
  an EXE restart on every mod change is not indicated by these runs. The decision is the MC's.

## 22. The error report and the popup helper on every failure of the 64-bit path (issue 3521, 2026-11-14, account B)

Decisions D119 (report, popup), D120 (a file the tester sends in: no upload, no network code), D122 (handed in as an issue
on the patch's GitHub repository) and D124 (design V: the EXE always boots stock, config.ini is never touched, a failure
shows the popup and the report and the session goes on as the stock game). Issue `tracker/issues/3521-*.md`; the
Architect's texts `research/07-architecture-decisions/architect-2026-11-14-failure-popup-and-report.md` and
`architect-2026-11-14-always-boot-stock.md`. The helper itself is issue 3520's (`proxy/helper/`, `vp64helper.exe` beside
the proxy). 3521 was first planned under D119 with a session rule (config mask, D33 session = no play, end the game,
`failed=1`); D124 replaced that at 03:31 before any of it was tested, and none of it is in the code.
**Correction 2026-11-14 05:05 (issue 3523, the owner's D125 reversed D124): the session rule is back; 22.1-22.2 describe the
stock-session branch, and 22.8 the D33-session branch, `failed=1` and the dump kept out of the report (MC #577).**
**Addition 2026-11-16 (ticket 0418): when the EXE itself dies with an exception exit code (0xC0000005 and kin), the
proxy dies with it, so the server writes this report (stage `exe-crash`, the same folder form and root) and starts
`vp64helper popup`: [error-report-exe-crash.md](error-report-exe-crash.md).**

### 22.1 What the proxy does (as built) [C]

Only with `vpproxy.ini pack=1` (the shipped layout; development and test layouts with `pack=0` are unchanged):
- **At the boot** (`DllGetGameContext`): every failure that 3504 already turned into the 32-bit fallback (the pack check;
  the server's start; the handshake) first takes the fallback exactly as before (`BootOff`: one `boot: FALLBACK` line, the
  title notice), then `ReportFailure()` writes the report and starts the helper. The session goes on on the 32-bit DLL.
- **In a game** (`CallFailed`, a call that fails outside the boot and the shutdown): the first such failure writes the
  report and starts the helper (stage `server-death`), then `on_server_death` decides as before (`exit`: exit code 3; a
  32-bit DLL cannot take over a game in progress).
- Once per load of the proxy (`g_reported`, claimed with `InterlockedCompareExchange`; a second failing thread does not
  write a second report).
- The failure's stage and code are set where Boot() fails (`SetFail`): `pack` + the pack status name (`hash-mismatch`,
  `other-dll`, `no-pack`, ...); `server-start` + `run-folder`, `server-missing`, `strict-allow`, `server-cfg`, `endpoint`,
  `create-process` (with the Win32 error and the system's English text), `server-exit` (exit code), `attach-timeout`;
  `handshake` + `SchemaMismatch` / `VersionMismatch` / `LayoutMismatch` (the wait's status) or `attach-refused` (the
  client's `PeerEvent::Refused` during the boot; the refusal text and the proxy's schema hash in the details),
  `boot-call`, `dll-boot` (kRt_Boot's message), `context-kind`; `server-death` + the failed call's vpipc status, the
  server's exit code or "ended by exception code 0x...".
- The helper: `"<proxy dir>\vp64helper.exe" popup --report "<report folder>" --options "<vpproxy.ini>"` (3520's interface;
  `handin=` in vpproxy.ini replaces the default hand-in line; since ticket 0361, 2026-11-15, its first line follows the
  report's stage: the boot fallback's "goes on as the normal (32-bit) game", or after `server-death` "the game had to
  end", section 28), `CreateProcessW` with `DETACHED_PROCESS |
  CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB` (retried without the breakaway on `ERROR_ACCESS_DENIED`), no
  inherited handles, not waited for. A missing helper is one log line ("does not exist: not started"); the report and
  the fallback stay. A report folder that cannot be made is one `report: FAILED` line; the helper is still started with
  the intended folder.
- The proxy's import list is unchanged (`build-proxy.log` "check imports ok: cvgamedatabasewin32final release.dll,
  kernel32.dll, lua51_win32.dll, msvcp90.dll, msvcr90.dll"): user32, advapi32, shell32 and ntdll are looked up by name.
- Code: `proxy/src/proxy_report.{h,cpp}` (the folder, the scrub, the system text), `proxy_report_mods.cpp` (the mods
  database through the engine's database library), `proxy_session.{h,cpp}` (the masks for the report, the config.ini
  folder, the helper's start), `proxy_rt.cpp` (`SetFail`, `ReportFailure`, the two call sites), `proxy_rt_logic.cpp`
  (`handin=` accepted and ignored).

### 22.2 The report folder [C, L offline]

`<folder of config.ini>\VP64\reports\<yyyymmdd-hhmmss>[-n]\` = `Documents\My Games\Sid Meier's Civilization 5\VP64\
reports\...` (`SHGetFolderPathW(CSIDL_PERSONAL)`; tests: `VPPROXY_TEST_CONFIG_INI` names the config.ini whose folder is
used; config.ini itself is never read or written by this code). Files:
- `popup-summary.txt`: one line, "The 64-bit part of the patch failed (<stage>: <code>); the game goes on as the stock
  game." (or "the game had to end." for `server-death`); 3520's helper shows its first line in place of its default.
- `report.txt` (CRLF): `[failure]` stage, code, details, and (since ticket 0186, 2026-11-14; 22.9) the line
  `server.log, last FAILED or LOUD line: ...` when a `server.log` is among the tails; `[patch]` the proxy's build line, the pack's release; `[hashes]`
  SHA-256 found and expected (from the manifest; `[match]` / `[MISMATCH]`) of the 32-bit DLL (expected: the manifest's
  x86_dlls with their origins), the x64 DLL, the purity file, the proxy and server.exe; `[session]` the config and view
  masks with the keys they have off, and the ini's `pack` / `on_server_death`; `[exe]` the module name, its PE
  TimeDateStamp (0x546CD0A8 = the build the patch was written for) and size; `[mods]` the activated mods
  (`cache\Civ5ModsDatabase.db` beside config.ini, `Mods.Activated = 1` in RowID order, the name from `ModProperties`
  Name; opened read-only with `Database::Connection`); `[system]` Windows version (RtlGetVersion), processors, physical
  and virtual memory, the largest free block of the 32-bit address space, the process's private bytes, every attached
  display with its adapter, driver version and date (registry key from `EnumDisplayDevicesA`) and mode; `[attachments]`;
  `[left out]`.
- `vpproxy.log.tail.txt` and `<other>.log.tail.txt` (the last 64 KB of the proxy's log and every `*.log` in the run
  folder, cut at a line start); copies of `manifest.json`, `vpproxy.ini` and `VP64Options.ini` (at most their last
  256 KB). A `*.dmp` the server wrote into its run folder is **named, not copied** (section `[server minidump]` of
  report.txt: path, size, time, and that it may hold the user's name; corrected 2026-11-14 05:05, issue 3523, MC #577;
  was: "copied as it is").
- **Privacy:** every text file is scrubbed: the profile folder (`USERPROFILE`, in UTF-8 and the ANSI code page, with `\`,
  `/` and JSON's `\\`) becomes `%USERPROFILE%`; `USERNAME` and `COMPUTERNAME` / `USERDOMAIN` as whole words (case-
  insensitive) become `<user>` / `<computer>`; a SteamID64 (17 digits from 7656119) becomes `<steamid>`. No saves, no dump
  of the EXE, no server minidump (issue 3523; was: "a copied server minidump is binary and not scrubbed"). Reports are
  written only under the player's Documents (in tests under `--scratch`), never into the repository.

### 22.3 The proxy cannot write a minidump of server.exe [L, 2026-11-14 03:25]

A 32-bit process calling `MiniDumpWriteDump` (dbghelp, loaded by name) on a 64-bit process fails: return FALSE,
`GetLastError` 0x8007012B (`ERROR_PARTIAL_COPY` as HRESULT), file 0 bytes (experiment `dumpx.cpp` built with the proxy's
toolchain, target a 64-bit `cmd.exe`, Windows 11 26100). So D119's "minidump of server.exe from the proxy" is not
possible from the proxy (nor from an x86 helper). A crashed server's exception code is in the report (its exit code); the
exception address and a dump need the server to write one of itself (an unhandled-exception filter in `server.exe`
writing `<run folder>\*.dmp`), which the report names (since issue 3523; 3522 copied it). Built by issue 3522: section
22.6. (Replaced 2026-11-14 03:58: "Not built (server code is outside 3521): handed to the lead as a follow-up.")

### 22.4 Tests [L, 2026-11-14 03:40, build `D:/c5-64bit-work/3500/3521/b1` (branch task-3521-session-rule-report: server
and generated tree built at 03:17 from 191b824a + 3521's first commit, `--deps` 9045's bF, release; the proxy rebuilt by
`build_proxy.py --gen <b1>/gen` at f751dea7, sha256 ddfbd38d...)]

- `python -I proxy/tools/test_proxy_session.py --build <b1> --scratch <dir> --asan`: **66/66** (67 s). Unit
  (`rt_tests/session_unit.cpp`, clang-cl -m32) **38/38** and **38/38 under MSVC `/fsanitize=address`** x86 (canary
  caught). Loader cases with the built proxy, a seeded user `zqxuser3521` (in every path the proxy logs) and computer
  `ZQXHOST3521`, the fake helper `rt_tests/fake_helper.cpp`: pack-hash, pack-other, srv-missing, srv-start, srv-exit,
  handshake (an x64 `guard_fake` with another schema hash) each give the 32-bit fallback, one report with every section,
  the popup summary and the helper's exact command line; death (the real server ended from outside, then a call) gives the
  report and exit code 3; no-helper; blocked; negatives (good pack, `pack=0` missing server, `pack=0` death: no report,
  no helper); no file of the 8 report folders holds the seeded names.
- Observed: `CreateProcess` of a text file named `.exe` gives Win32 error **216** (`ERROR_EXE_MACHINE_TYPE_MISMATCH`), not
  193, on this Windows 11 26100 workstation [L]; the test accepts either.
- Unchanged suites on the same build: `test_proxy_rt.py` **79/79** (61 s), `test_proxy_pack.py --asan` **70/70** (38 s)
  and with `--fork D:/c5-64bit-work/9045/fork --tag Release-5.4.8 --tag-sha 262aee88...` **72/72** (37 s). Baseline before
  3521 on the same build: 79/79 and 64/64 (no `--asan`, no `--fork`).

### 22.5 Not verified

The real EXE: no game was run (the issue needs none). The popup itself (3520's helper) in a real session; the report
under a non-ASCII Windows user name (the scrub handles the UTF-8 and ANSI forms; not tested); the report's `[session]`
masks read from the real EXE (only the test masks were read); `Database::Connection` on the EXE's own, open
`Civ5ModsDatabase.db` (tested on a closed test file); whether the EXE's process is in a job that refuses the breakaway.

### 22.6 server.exe writes its own minidump (issue 3522, 2026-11-14) [C, L offline]

`server/src/srv_crashdump.{h,cpp}`; `srv_main.cpp` calls `SrvLog("server: %s", SrvCrashDumpInstall(s_szRunDir))` once,
right after server.log is opened (run_dir from server.cfg is known; no DLL loaded yet). server.log then has the line
`server: crash dump filter installed: <run folder>\server-<pid>-<yyyymmdd-hhmmss>.dmp (...)`.
- Install: dbghelp.dll from System32 only (`LOAD_LIBRARY_SEARCH_SYSTEM32`), `MiniDumpWriteDump` looked up then (no import
  change), the path prefix `<run>\server-<pid>-` prepared, `SetUnhandledExceptionFilter` once. A second install only
  replaces the folder and never chains the filter to itself.
- On an unhandled exception: only the first one is dumped (`InterlockedExchange` guard: a fault inside the filter or a
  second thread passes on). `MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules` with the exception
  record and the faulting thread, into `<name>.dmp.tmp`, renamed to `.dmp` at the end; a failed write deletes the .tmp,
  an unwritable folder creates nothing. No heap allocation in the filter (stack buffers, prepared prefix). Then the
  filter it replaced is called (for an escaping C++ throw the runtime's, which ends in terminate/abort), else
  `EXCEPTION_CONTINUE_SEARCH`: the exit code is the one without the filter, and the proxy sees the death as before.
- Size [L]: 44,728 bytes (access violation) and 46,248 bytes (C++ throw) for the 4-thread, 18-module test program; the
  server's will be larger (more threads and modules), still no full memory. The stacks are the tester's game state; the
  dump also holds strings from the stacks, which may contain paths with the Windows user name. So the report does not copy
  it, only names it (22.8; corrected 2026-11-14 05:05, issue 3523, MC #577; was: "the report copies dumps unscrubbed and
  says so; 3520's popup says a dump holds the game state").
- Tests [L, 2026-11-14 03:53]: `python -I server/tests/crashdump/run_crashdump_test.py --out <dir> --asan` (through
  `quietlock.py run`): **28/28**. clang-cl x64 (/W3 /WX, `SRV_CRASHDUMP_TEST`): access violation and an escaping C++ throw
  each against a control without the filter: the same exit code (0xC0000005; 3 for the throw: the runtime's abort with
  `_CALL_REPORTFAULT` off, as server.exe), exactly one `server-<pid>-<stamp>.dmp` and no .tmp, opened by dbghelp
  (`MiniDumpReadDumpStream`): exception record 0xC0000005 at an address in the program / 0xE06D7363 in KernelBase.dll,
  the faulting thread = the thread the program printed and listed in the thread list; installed twice: one dump; a folder
  under a file: nothing written, exit 0xC0000005; the write made to fail (test build only): no .dmp and no .tmp; no fault:
  no dump; an empty folder: "NOT installed". MSVC `/fsanitize=address` x64 (canary caught): throw, ok, ok-twice and the
  checker on the clang run's dumps, no AddressSanitizer report; the access violation is not run under ASan (ASan's own
  handler reports it first).
- With the proxy: see 22.7.

### 22.7 The dump in the report, and a race found on the way [L, 2026-11-14 03:56, build `D:/c5-64bit-work/3500/3522/b1`]

Build: `server/build_server.py --deps` 9045's bF, release, branch task-3522-server-crashdump (server.exe with the filter,
the proxy with 3521). `python -I proxy/tools/test_proxy_session.py --build <b1> --scratch <dir> --asan` (through
`quietlock.py run`): **74/76**, of which the 9 new 3522 checks pass:
- `death`: the real server.exe's server.log has `server: crash dump filter installed: <run folder>\server-<pid>-
  <yyyymmdd-hhmmss>.dmp`.
- `srv-crash` (new case): `server_exe` = the crashdump test program, which installs the filter on server.cfg's run_dir and
  faults before it attaches. The proxy falls back to the 32-bit DLL; the report has stage server-start, code server-exit and
  "(0xC0000005) before it attached"; the run folder's `server-<pid>-<stamp>.dmp` is in the report folder byte for byte,
  listed under `[attachments]` as the server's own minidump, and dbghelp opens the copy (0xC0000005 in the program).
  **Superseded 2026-11-14 05:05 (issue 3523, MC #577):** the dump is no longer copied; the case now checks that the
  report folder holds no .dmp and that report.txt names it (22.8).
- The privacy check skips `*.dmp`: a dump is copied unscrubbed by design (22.2). (Superseded by 3523: the check covers
  every file again and a second check finds no .dmp in any report folder.)

The 2 failures are 3521's `handshake` case, a race in 3521's code [L]: the boot thread saw the schema-mismatch fake server
exit (`[0.248841]`) 9 microseconds before the vpipc monitor thread logged the refusal and raised `PeerEvent::Refused`
(`[0.248850]`, `[0.248939]`), so the report classified the failure as server-start / server-exit, not handshake. It passed
in 3521's two runs (1 failure in 3 runs). **Fixed (lead-3500's word, commit c2dc769c "3521 fix"):** in Boot()'s
"exited before it attached" branch, when the exit code is non-zero, the boot thread polls `g_attachRefused` every 5 ms for
up to 250 ms before `SetFail`, so the monitor's refusal is seen; the 250 ms is only this wait for that one event (a
refusal arrives within microseconds; a server that exits for another reason costs the fallback 250 ms more).
`test_proxy_session.py --handshake-runs N` repeats the case.
`test_proxy_rt.py` on the same build: **79/79**.

After the fix [L, 2026-11-14 04:00, the same b1 with the proxy rebuilt at c2dc769c, sha256 17cf2d13..., imports unchanged]:
`test_proxy_session.py --asan --handshake-runs 12` **142/142** (81 s; the handshake case 12 times in a row, each stage
handshake with every check, 0 failures; unit 38/38 and 38/38 under ASan), `test_proxy_rt.py` **79/79**,
`test_proxy_pack.py --asan --fork ... --tag Release-5.4.8` **72/72**.

### 22.8 The D33-session rule, failed=1, and the dump kept out of the report (issue 3523, D125, MC #577) [C, L offline]

The owner's D125 reversed D124 ([reworded for privacy] D124 had chosen to save the memory and fail loudly): config.ini keeps D33 in the A'' form, and a failure in such a
session must not play on the 32-bit DLL (no terrain, release-driver.md W3b.6). As built (`proxy_session.*`,
`proxy_session_keys.h`, `proxy_rt.cpp`, `proxy_rt_logic.*`, `proxy_report.cpp`):
- **Our keys, one list:** `proxy/src/proxy_session_keys.h` `VP64_D33_KEYS_A2` = darkred.py `D33_OFF` minus River and
  Arrows, in its order (11 keys, bits 0x0013F898); `VP64_STOCK_OFF_KEYS` = OverlayDebug, ColorKeyEdit (darkred.py
  `STOCK_OFF`, off in the stock config.ini, mask 0xFFEFFFF7). Rule bits = our keys minus the stock-off pair = 0x0003F890
  (9 keys); the A'' config mask is 0xFFEC0767. *Corrected 2026-11-15 (issue 0368, the owner's decision D147: the beta draws the
  EXE's own combat): Particle, Combat and Projectile left the list (darkred.py `D33_ON`): **8 keys** (OverlayDebug, Decal,
  Forest, FOW, FluidFOW, DistanceFog, YieldIcons, ColorKeyEdit), bits **0x0013C098**, rule bits **0x0003C090** (6 keys),
  A'' config mask **0xFFEC3F67**. Replaced: the 11 keys, 0x0013F898, 0x0003F890 (9), 0xFFEC0767 above; that mask (a build
  before 0368) is still a D33 session, and a config.ini with only the three = 1 is a stock session (session_unit). The
  config.ini routine's former keys gain the three (`research/02-exe-internals/disable-systems-and-masks.md` 5, "Combat,
  Particle and Projectile on in the product").* *Corrected 2026-11-14 20:23 (issue 3803, epic 0179, MC #1009: route (b) for
  the move-order path until increment 3): MovementPath left the list (darkred.py `D33_ON`; the EXE draws the path's models).
  Replaced: 12 keys, bits 0x0093F898, rule bits 0x0083F890 (10 keys), A'' mask 0xFF6C0767; masks written by builds before
  3803 or 0192 are still D33 sessions. A config.ini left `on` by an older build keeps the former key at 1 after the new
  `off` (its side file is refused whole): `research/02-exe-internals/disable-systems-and-masks.md` 5, "MovementPath on in
  the product", with the fix proposed.* *Corrected 2026-11-14 (issue 0192, the MC's decision under the hand-over
  design's 5.2 after 0177): Minimap left the list (darkred.py `D33_ON`; the EXE's own minimap panel draws, equal to stock).
  Replaced: 13 keys, bits 0x009BF898, rule bits 0x008BF890 (11 keys), A'' mask 0xFF640767; a config.ini written by a
  build before 0192 (Minimap = 1 too) is still a D33 session under the new rule.* `test_proxy_session.py` and `test_cfgini.py` compare the list with
  darkred.py. Issue 3520's config.ini routine (`proxy/helper/proxy_cfgini.cpp`) takes its `kKeys` from the same header
  (lead-3500, 2026-11-14 05:10). *Corrected 2026-11-14 05:20 (lead-3500): Waves left the list when main's epic 3600
  (1f36a838) moved it from darkred.py `D33_OFF` to `D33_ON` (Waves stays on in config.ini; `proxy_shell.cpp`'s ClearBits
  clears its view bit 0x200000). Replaced: 14 keys, bits 0x00BBF898, rule bits 0x00ABF890 (12 keys), A'' mask
  0xFF440767. The test figures below were taken with the old list; the lead re-ran session_unit 51/51 (also under ASan),
  test_cfgini 12/12 (ASan) and test_vp64helper 81/81 with the new one; the loader part reruns on 3520's S1 build.*
- **The session's kind** (`ProxySession::SessionKind`), read at the failure: the config mask `[g_Civ5App+0x940]` by
  `proxy_session.cpp`'s reader (the RVA and offsets of `proxy_shell.cpp`, which has no public accessor; that file is
  untouched). All rule bits set = stock; any one clear = D33. With no readable mask: D33 in the known EXE build (fail
  loud), stock in any other process (development and test processes, which never ran a D33 config; the existing pack
  suites stay as they were).
- **A D33 session never plays:** at the boot (every failure that 3504 turned into the fallback) the proxy logs `boot: NO
  PLAY: <reason>; D33 session: ...`, loads no 32-bit DLL and shows no notice, writes the report, starts `vp64helper failed
  --pid <EXE pid> --config "<config.ini>" --report <dir> --options <vpproxy.ini>` (3520's final CLI; `--config` = the
  config.ini the proxy resolved inside the EXE: `VPPROXY_TEST_CONFIG_INI`, else `C5_DOCUMENTS` (a harness instance),
  else `SHGetFolderPathW(CSIDL_PERSONAL)`), then `Fatal(3, "the 64-bit path failed in a D33 session (no
  play, D125)")` (TerminateProcess, as `on_server_death=exit`). In a game (`CallFailed`): the report, `vp64helper failed`,
  then `Fatal(3)` whatever `on_server_death` says. A stock session keeps 3521's behaviour (report, `vp64helper popup`,
  the fallback; `on_server_death` in a game). The helper restores config.ini after the EXE has ended (3520; S0 found no
  other write of config.ini than at the EXE's start).
- **`failed=1`** in vpproxy.ini (the helper writes it; since D240, 2026-11-22, only for the stages off-switch and exe-build,
  release-driver.md W3c.6) = the off switch: the boot is `mode=off`'s (the 32-bit DLL, no
  server, no pack check, no report), logged `mode=on but failed=1 ... until vp64helper on clears it`. `failed=0` or no key:
  the rule as above. `handin=` stays accepted and ignored.
- **The server's minidump is not copied** (MC #577: reports are public, D122): report.txt's `[server minidump]` names
  each `<run>\*.dmp` with its size and write time and says it may hold paths with the Windows user name and should not be
  attached to a public issue; the dump stays in the run folder. 3520's helper adds its "game state" line only for a .dmp
  in the report folder, so that line no longer shows (told to the lead).
- `popup-summary.txt` in a D33 session: "... (<stage>: <code>); the game was ended." *Corrected 2026-11-22 (ticket 0682,
  D240): was "...; the game was ended; the next start is the stock game until the patch is switched on again." Since D240
  the helper leaves the patch on after a failure (release-driver.md W3c.6), so the summary promises nothing about the
  next start.*

Tests [L, 2026-11-14 05:06, build `D:/c5-64bit-work/3500/3523/b1` (branch task-3523-d33-session: server.exe and the
generated tree from 5b70826c's sources, `--deps` 9045's bF, release; the proxy rebuilt with `--config`, sha256
c3abac0e..., imports unchanged)], all through `quietlock.py run`:
- `test_proxy_session.py --asan`: **130/130** (85 s). Keys = darkred.py (2 checks). Unit `session_unit` **51/51** and
  **51/51 under MSVC `/fsanitize=address`** x86 (canary caught): the list and its bits, rule bits 0x00ABF890, the stock and
  A'' masks, each of the 12 rule bits alone makes a D33 session, no other bit does, the kinds without a mask, the helper's
  `failed --pid` command line recorded by the fake helper, `--config`, `C5_DOCUMENTS`, the dump named and not copied. Loader (the built proxy, a
  seeded user and computer name): the six boot faults in a stock session as before; the same six in a D33 session
  (mask 0xFF440767) and one with only FluidFOW off (0xFFEF7FF7): DllGetGameContext never returns, exit code 3, `boot: NO
  PLAY`, no FALLBACK line, no 32-bit DLL, one report with every section, `vp64helper failed --pid <the loader's pid>
  --config "<the test config.ini>" --report <dir> --options <ini>`; a server death in a D33 session with `on_server_death=continue`: exit code 3 (and the
  stock one with `continue` goes on: the negative); `failed=1` with a bad pack in a D33 session: the 32-bit DLL, no server,
  no report, no helper; `failed=0`: the rule; the crashing server: no .dmp in the report folder, report.txt names it with
  its size and time; no file of any report folder holds the seeded names; no report folder holds a .dmp.
- `test_proxy_rt.py` **79/79** (37 s), `test_proxy_pack.py --asan --fork ... --tag Release-5.4.8` **72/72** (25 s), on the
  same build.

Not verified: the real EXE (no game run; 3520's S1 does the live checks), the mask read from the real EXE at the failure,
the helper's `failed` command itself (a fake helper recorded the command line).

### 22.9 report.txt quotes the server's failure line (queue ticket 0186, account B, 2026-11-14) [C, L offline]

Why: 3706 item 5 (b) on oldpc (job 20261114-154008-b4d4, desk 821): after the overlay tap's install failure report.txt
said only `stage: server-death`, `code: PeerDied` and the exit code 5; the reason, the server's `LOUD: overlay tap FAILED
(VPOVL_FAILED reason 2 HANDLE)`, was only in the attached `server.log.tail.txt`.

As built (`proxy/src/proxy_report.{h,cpp}`; `proxy_rt.cpp` unchanged: it already passes `<run>\*.log` as tails):
- `ProxyReport::Write` finds the tail whose file name is `server.log` (case-insensitive), scrubs its text (the same
  scrub as the attachment), and `ProxyReport::LastFailureLine` takes the **last line holding the word `FAILED` or
  `LOUD`**: upper case, not inside a longer word of ASCII letters and digits (an underscore separates, so `VPOVL_FAILED`
  counts; `failed`, `CLOUD`, `LOUDER`, `FAILEDX`, `2FAILED` do not). The line loses its line end, control characters
  (a tab) become spaces, trailing blanks go, and it is clipped to `kFailLineMax` = 400 bytes, never inside a UTF-8
  sequence, with ` [...]` after a cut. Scrubbing comes before the clip, so a clip cannot leave half a user name behind.
- `[failure]` gets one line after the details: `server.log, last FAILED or LOUD line: <line>`; with no such line in the
  64 KB tail `(none in the last 64 KB of server.log)`; with an unreadable server.log `(server.log cannot be read (error
  N))`; with no server.log among the tails (no run folder) no line. Every stage, not only `server-death`: the run folder
  is the boot's own, so a `server-start` or `handshake` failure quotes the server's reason too. report.txt as a whole is
  scrubbed as before (D119's privacy rules unchanged); `popup-summary.txt` is unchanged.
- The last line of either word, not the last `FAILED` line: the server's overlay failure is a `LOUD: ... FAILED` line;
  its `result: FAILED: ...` summary line (srv_remote_main) comes last when the server ends orderly, which is the right
  quote then too.

Tests [L, 2026-11-14 18:07-18:46 EST, build `C:/c5-64bit-work/0186/b2` (branch task-0186-report-failure-line at
98da7d62 on main 3eab122e; `server/build_server.py --src` a plain copy of the pin 04b1211b8 `--deps` 0167's release
deps, release, OK in 446 s; deleted at hand-back)], all through `quietlock.py run`:
- `test_proxy_session.py --only unit --asan`: **11/11**; `session_unit` **60/60** and **60/60 under MSVC
  `/fsanitize=address`** x86 (canary caught). New checks: report.txt's `[failure]` holds, right after the details, the
  line `server.log, last FAILED or LOUD line: [   66.700] composite: LOUD: overlay tap FAILED (VPOVL_FAILED reason 2
  HANDLE) in %USERPROFILE%\x by <user> on <computer>` from a synthetic server.log whose later lines carry `failed`,
  `CLOUDY`, `LOUDLY`, `FAILEDX` (the tab made a space, CRLF dropped, the user, computer and profile folder scrubbed); a
  server.log with no such line says none; an unreadable one says why; no server.log, no line; `LastFailureLine` alone:
  the underscore rule, a first line and a last line without newline, the 400-byte clip, no cut inside a UTF-8
  sequence, the negatives. The existing check that no report file holds the seeded names covers the new line.
- Full `test_proxy_session.py --asan` on b2: **128/131** (689 s), rerun **129/131** (727 s). The new loader check
  (`death`: the line is in report.txt and equals the real server.log's last FAILED or LOUD line, or says none) passed
  both times; the real server ended from outside logs no such line, so the line said `(none in the last 64 KB of
  server.log)`. Every failure, 3 then 2 (pack-hash, srv-missing, srv-crash; then srv-exit, blocked), was the loader's
  `loader: FAIL: mode=off started a child process` in a **stock fallback**: the proxy starts `vp64helper.exe popup` as
  a child of the loader just before the fallback, and `loader.cpp`'s `off` scenario fails when any child is still
  alive at its check; the fake helper exits at once, so it is a race lost when process starts are slow (the machine was
  loaded: see the vcpkg-init PowerShell stall in research/06-tooling-and-workflows/worker-account.md, bug 0108 entry).
  Not caused by 0186 [I: 0186 changes only report.txt's text; the failing cases differ between runs; `loader.cpp` and
  the helper start are unchanged since 3523, whose run passed 130/130]. Posted to the queue for its own ticket (fix in
  `proxy/tools/rt_tests/loader.cpp`: the `off` check ignores a child whose image is the helper, or waits for it).
  **Fixed by ticket 0196 (22.10, 2026-11-14): both, ignored and waited for.**
- Unchanged suites on b2: `test_proxy_rt.py` **79/79** (386 s), `test_proxy_pack.py --asan` **70/70** (271 s).

Not verified: a live server failure (a game with the overlay tap's fault, as 3706 item 5 (b) on oldpc) writing the
line into a real report; the loader route quoting a non-empty line (only the unit test's synthetic log has one).

### 22.10 The loader's `off` check and the popup helper's child (queue ticket 0196, account B, 2026-11-14) [C, L offline]

Why: 22.9's runs failed 3/131 and 2/131 with `loader: FAIL: mode=off started a child process` in stock-fallback cases
(the proxy starts `vp64helper.exe popup` as a child of the loader, then falls back; the fake helper exits at once, but
under load it was still alive when `loader.cpp`'s `off` scenario took its child snapshot).

As built (`proxy/tools/rt_tests/loader.cpp`):
- `Children(skipImage, skipped)`: Toolhelp's `szExeFile` (wide, `Process32FirstW`) equal to `skipImage`
  (case-insensitive) is not returned; its pid goes to `skipped`. The bug-0097 id-reuse filter is unchanged.
- `off` (now `OffChecks`): a `vp64helper.exe` child is not counted and is waited for up to 10 s (`WaitGone`), logged as
  `loader: child pid N is vp64helper.exe (the fallback's popup helper, not a server): not counted; it has exited` (or
  `still running after 10 s`, which does not fail: the session test checks the helper's command line itself). Any other
  child still fails the case with the same text as before, now after one `loader: child pid N` line per child. The
  case's meaning is kept: mode=off and the fallback must not leave a server (which is `server.exe` or `server_exe`,
  never `vp64helper.exe`).
- New scenario `off-spawn <dll> <slot> <lua dir> <program>`: `off` with `<program>` started by the loader itself
  `CREATE_SUSPENDED` just before the child check (terminated before the scenario returns), so the verdict on a live
  child is tested without a race. `test_proxy_session.py` runs it twice in a `mode=off` layout: a copy of the fake helper
  named `vp64helper.exe` (must PASS with `not counted; still running after 10 s`) and the same program named
  `notahelper.exe` (negative: exit 1, `loader: FAIL: mode=off started a child process`). The suite has 133 checks (131 +
  these 2). `test_proxy_rt.py` drives `off` and its `null`/boot scenarios as before (no helper there).

Tests [L, 2026-11-14 19:57-20:08 EST, build `C:/c5-64bit-work/0196/b` (branch task-0196-loader-off-helper-race on main
596dcd2b; `server/build_server.py --src` a plain copy of the pin 04b1211b8, `--deps` 9045's release deps (DLL
74588135...), release, `--offline`, OK in 100 s; proxy 6e0c2998...; deleted at hand-back)], all through
`quietlock.py run`; output in `evidence/0196/acceptance.txt`:
- `test_proxy_session.py --asan`: **133/133** four times (101, 84, 90, 106 s): runs 1-3 in a row with three games and
  two servers of other tickets registered; run 4 while a `server/build_server.py` of the same tree (78 s, 32 parallel
  compiles) ran beside it for its first 67 s. The race was hit in every run: a `vp64helper.exe` child was in the
  snapshot at the off check in 3, 5, 5 and 7 stock-fallback cases (pack-hash, pack-other, srv-missing, srv-start,
  srv-exit, handshake, srv-crash, blocked; it had exited within the 10 s each time) besides the off-spawn case.
- Control: the same build with the old `loader.cpp` (main 596dcd2b) put back for one `--only loader` run: **118/123**,
  `mode=off started a child process` in pack-hash, srv-exit and srv-crash (the 22.9 failure, reproduced), plus the two
  off-spawn checks (`unknown scenario`, expected); the file was restored after the run.
- `test_proxy_rt.py`: **79/79** (48 s): its `off`, `no-ini`, `off-missing`, `null` and boot scenarios with the new loader.

Why so many hits against 22.9's 2-3 of 131 is not established [I: the old check counted any child it could still open,
the new log line shows only that the child was in the snapshot; the helper's exit time under three running games was not
measured].

## 23. 0153's flow on the proxy route under cdb, plain 32-bit pin (queue ticket 0174, 2026-11-14, account B)

**Result.** The proxy route's half-built-database end of section 20 is **not proxy-specific and not a test artefact**: it
is VP's own CP Lua helper `CPK.Misc.CustomModOptionEnabled`, whose never-finished `CustomModOptions` statements keep the
engine's SQLite connection busy, so a mod rebuild that starts while they live can neither DETACH Localization nor DROP
VP's work tables. The stock route with the plain pin ends the same way (run s1, 23.6): the same lock lines, the same
missing columns, VP's own "the game will now crash" dialog instead of the proxy's exit 3 [C, dumps at the moment of the
lock; L]. The Squads row of section 20 (EXE gone at `Detach Loc Database` with no lock line) did not recur in 3 proxy
runs and stays open; VernsTweaks arms were not rerun (the mechanism does not depend on the extra mod).
(Status line replaced 2026-11-14 15:20: was "partial, paused at 14:10 EST for the owner's reboot".)
**Fix (ticket 0184, 23.7):** upstream already has it (`14d16fa3d`, #13467, after 5.4.8; also `CPK.Util.Color`, which
holds the connection after a game); fork branch `fix-cpk-query-step-to-end`; live on the laptop it removes every stepped
statement and the proxy-route end does not happen.

**23.1 Set-up.** Instance c5h-c (Goldberg, C:), DX11 EXE, D33 product path exactly as 0153 (section 20): 0153's build
(server.exe 950F81EB..., proxy EE5D59FE..., composite C3C3F9C2..., x64 DLL 74588135... of the D96 pin) with only
`CvGameDatabase_x64.dll` = 9c37b39e... (0165's `CountLock` fix, rebuilt from this branch, byte-identical to 0165's), staged
by `shell/evidence/0174/build.sh`. The CP folder's 32-bit DLL was set to the **plain** pin `i0ship-26234371` (F92881A3...)
with `provision.py set-dll` before `set-proxy`, so the proxy's `fallback_dll` is the plain build and no `-ia` build is in
the instance; at the end `set-dll` put the instance's own `i0ship-bbfacca1-ia` (e589306e...) back, `provision.py verify
c5h-c` ok. Extra mod: (4a) Squads for VP. Every mod choice and game by clicks (`shell/evidence/0174/seq.sh`, driver
`run_0174.py` = 0153's + 0164's `dll`/`cdb`); an invasive cdb (0164's `cdb_watch.py`) on the EXE. Evidence:
`shell/evidence/0174/excerpts/`. Kept dumps (the rest of the scratch was deleted at the hand-back): `C:/c5-64bit-work/0174/keep/`
`r2-proxy-fatal-exit.dmp` (EXE at the proxy's TerminateProcess), `s1-at-detach-locked.dmp` (at the DETACH refusal),
`s1-dialog-state.dmp` (at VP's dialog), `s1-dll-minidump.dmp` (the DLL's own minidump).

**23.2 Runs** [L, 2026-11-14]:

| Run | Lua channel | Flow | End |
|---|---|---|---|
| r1 (pid 55000) | none | act Squads; game (turn 0), quick save, exit; act CP+VP+EUI; game, exit; act Squads; quick save **loaded**; exit; main-menu Exit | all phases through; no second-chance exception; `Localization is locked` (2) + `already in use` (2) at the deactivation after the LOADED game, rebuild went on |
| r2 (pid 62656) | none | act (Squads unticked by a mis-click: CP+VP+EUI); Back; act Squads (NEXT clicked ~9 s after Back, while the Back's deactivation rebuild ran) | `Localization is locked`, ~60 `database table is locked`, engine db **836 tables 31,205 rows** (half built), x64 DLL PRECONDITION in the server (`CvGameCore_Expansion2.dll+0x785eea`, `crashes.log`), the proxy's `Fatal` -> `TerminateProcess(3)` from `ProxyFwd_ICvDLLDatabaseUtility1_CacheGameDatabaseData` [C, cdb stack `excerpts/r2-proxy-fatal-stack.txt`] |
| r3 (pid 11684) | none | live dumps after boot and after act Squads; then a second cdb attach | tool artefact, see 23.4 |
| r4 (pid 61704) | none | cdb with a hardware breakpoint from the start | tool artefact, see 23.4 |
| r5 (pid 62320) | **yes** (3 AI turns `turns.py`, Lua queries) | act Squads, Back, act; game, 3 AI turns, quick save, exit; main-menu Exit | through; no lock line in the whole session |

So **the proxy VernsTweaks row of section 20 (lock lines -> half-built database -> PRECONDITION -> exit 3) happens without
the Lua channel and without any `-ia` build** [L, r2]; it is not a test-driver artefact. It is also not tied to the extra
mod's game: r2 had played no game. The Lua channel arm (r5) went through once. The Squads proxy row of section 20 (EXE gone
at `Detach Loc Database`, no lock line, no FATAL line in vpproxy.log) did not recur in r1 or r5; its cause stays open.

**23.3 What holds the engine's database** [C, dumps scanned with `shell/evidence/0174/vdbe_scan.py` / `vdbe_read.py`:
every SQLite 3.7.17 `Vdbe` in the EXE's memory by its magic `0xbdf20da3`, `db` at magic-0x34, `pc` at +0x24, `zSql` at
+0x6c (layout confirmed: the SQL text is at +0x6c for all hits, all engine statements share one `db`)].
- After boot (r3, front end, before any activation): 22 statements, **0 active**.
- Right after the first activation with mods (r3, front end, no game): 263 statements on the engine's connection, **8
  active** (`pc` 17), all `SELECT NULL FROM CustomModOptions WHERE Name = ? AND Value = 1 LIMIT 1`.
- At r2's end (after the failed activation): the same 8 active statements, nothing else active.

That SQL is CP's Lua helper `Kit/Misc/CPK.Misc.CustomModOptionEnabled.lua` (CP v151): `DB.CreateQuery(...)` once per Lua
state, then `query(name)()` reads ONE row and never finishes the iterator, so a LIMIT 1 query that found an enabled option
stays stepped (never reset) until its next call or its garbage collection. The helper uses this SQL only when `Game` is
nil or `Game.IsCustomModOption` is not a function; included at file load by `InfoTooltipInclude.lua`,
`TechButtonInclude.lua`, `PathHelpManager.lua`, VP `MilitaryOverview.lua`, EUI `UnitPanel.lua`. In game on the proxy
route `Game.IsCustomModOption` is a function and answers (`BALANCE_VP` true) [L, r5, Lua channel, engine state]; the 8
statements are made in the front end after an activation, where `Game` is presumably nil [I].
Why this ends the rebuild [C, SQLite 3.7.17 amalgamation `sqlite3.c`, the one build_db.py uses]:
- `btreeEndTransaction` (line 52827): when a statement finishes while **other statements of the same connection are
  active** (`activeVdbeCnt > 1`), each btree it used is left in `TRANS_READ` instead of being released. With 8
  statements stepped and never reset, every attached database that any later statement reads (Localization, read by
  every text lookup) stays in a read transaction.
- `detachFunc` (line 81652): `DETACH` fails "database %s is locked" while that database's btree is in a read
  transaction: the `Localization is locked` lines; the rebuild then keeps the old Localization (`already in use`).
- `OP_Destroy` (line 70159): `DROP TABLE` fails SQLITE_LOCKED "database table is locked" while another statement of the
  connection is active: VP's work tables (`Leaders_NEW`, `Calendars_FIX`, `Building_ThemingBonuses_new`, ...) are not
  dropped and renamed, so the engine database lacks VP's columns (836 tables, ~31,100-31,200 rows instead of 43,6xx).
- The DLL's `PrepareResults` then fails on the first missing column (`Building_ThemingBonuses.ConsecutiveEras`, s1's
  dialog text) and ends the game: on the proxy route inside the server (x64 PRECONDITION, server death, the proxy's exit
  3); on the stock route as VP's error dialog "the game will now crash".

**23.3a The dump at the lock** [C, run s1, stock route, plain pin]: cdb's software breakpoint on the DLL's `database %s is
locked` formatter fired (15:03:40); its full dump, scanned, shows exactly 9 active statements on the engine's connection:
`DETACH Localization` itself (`pc` 0) and the same 8 `CustomModOptions` statements (same Vdbe addresses as in the live
dump taken right after the activation). Nothing else is active, so these 8 are what keeps Localization in a read
transaction. Live dump after the first activation on the stock route: the same 8 active statements (264 in all) as on
the proxy route (r3): **not proxy-specific** [C].

**When it bites** [L]: the 8 statements exist after every activation with VP (both routes), yet most rebuilds go through
(r1's first two deactivations, r5, 0164 s2/s5): the lock needs a rebuild that starts while the front-end Lua states that
own the statements are still alive (they are finalized when those states are torn down or collected). Seen: an activation
started from the Mods list right after Back from the mods' single-player screen (r2 proxy, s1 stock: Back, then the list's
NEXT within ~10 s), and the deactivation after a game loaded from a save in the same session (r1, which went on with the
old Localization because the deactivation drops no VP table). 0153's VernsTweaks proxy row (lock at the deactivation
after a game, half-built database at the next activation) fits the same mechanism; 0164's stock `-ia` runs too (their
lock lines are these statements, not the Lua channel; section 21.4a corrected). Which UI transition keeps the states alive
is not pinned down [I].

**For the patch**: nothing in the proxy or server causes or can avoid this; the fix belongs in CP's Lua (finish or reset
the query, for example `for row in query(name) do return true end return false` stepping to the end, or
`Game.IsCustomModOption` only), i.e. upstream. The proxy's handling (server death -> exit 3; with the 3500 error route a
report and popup) is the 64-bit equivalent of VP's own crash dialog. Upstream report not filed here (D95 rules apply).

**23.4 Tool findings (negative results)** [L, 2026-11-14]:
- A **hardware breakpoint** in the EXE process (`ba e1`, even on an address in `CvGameDatabaseWin32Final Release.dll`)
  makes the EXE end itself seconds later with `ExitProcess(0x8000DEAD)` from `CivilizationV_DX11+0x2ed408` (2 of 2: r3
  4 s after the attach, r4 69 s into boot; `excerpts/r4-hwbp-8000dead-stack.txt`): the DRM's tamper exit, the code
  `quiet-testbed.md` section 15 saw for a foreign EXE copy. Software breakpoints (r5: `bp` on the DLL's `database %s is
  locked` formatter, `CvGameDatabaseWin32Final_Release+0x55d04` = the `push offset` of that format string, found by `s -a`
  / `s -b` in a dump) and 0164's `bp ntdll!NtTerminateProcess` do not trigger it. The r5 breakpoint never fired (no lock
  in r5).
- Ending cdb while it is broken in (stuck printing stacks with symbol downloads) leaves every thread with an extra
  suspend count and the int3 of its software breakpoint in place; the EXE then hangs and the harness stop refuses
  (NeedsDecision). Recovered by attaching again (`-pd`), `~*m` once, restoring the original byte (`eb 774b8500 b8`), setting
  `eip` back and `g`, after which the EXE finished its own ExitProcess. Rule: one invasive attach per process; end cdb only
  while it runs. `cdb_watch.py stop` while cdb runs (not broken in) detaches cleanly (r5).
- In the Mods menu, Back on the single-player mods screen returns to the Mods list and starts a deactivation rebuild;
  the menu remembers the last ticks, so a fixed "tick" click can untick (r2's first activation).
- CP's in-game menu at turn 0 has `Restart Game` first, so Quick Save is at 0.3801 (turn 0) or 0.3489 (later turns) of
  the window height; 0164's s2 restart mis-click came from this.

**23.6 Run s1: the stock control** [L, 2026-11-14 15:00-15:20, c5h-c, stock route: the instance's own layout with the
plain pin `i0ship-26234371` (F92881A3...) set by `provision.py set-dll`, no proxy, no server; Squads copied in; cdb with
the software breakpoint of 23.4 from the first attach]: boot; Mods, NEXT (CP+VP+EUI+Squads) -> live dump: 8 active
`CustomModOptions` statements; then r2's clicks (Back, +5 s a click on the list, +4 s the Squads box, +2 s NEXT):
the activation's `Detach Loc Database` hit the breakpoint (dump of 23.3a), then 2 `Localization is locked` and 60 `table
is locked` lines, `Validate FK Constraints` passed, and `CacheGameDatabaseData` stopped on VP's error dialog: "Failed to
prepare database query 'Building_ThemingBonuses' [...]: no such column: ConsecutiveEras -- the game database is
corrupted or a mod is broken" (CvDatabaseUtility.cpp:80; text in `excerpts/s1-dialog.txt`). Excerpts:
`excerpts/s1-*.txt`.
**The dialog's cause**: it is the DLL's own (plain build, VP's `VPRELEASE_ERRORMSG` path) reaction to the half-built
database of 23.3: VP's `Building_ThemingBonuses` swap (a `_new` work table renamed over the old one) did not happen
because its `DROP TABLE` failed "table is locked" while the 8 CPK statements were active, so the first query that
names a VP column fails to prepare. Ending: under MC #771 the process was dumped first (`cli.py dump`, 1.82 GB, SHA-256
592856d5..., kept as `C:/c5-64bit-work/0174/keep/s1-dialog-state.dmp`), then OK was pressed (`cli.py dismiss --button OK ... --coordinator-override`,
15:20:49): the game stopped itself deliberately, `crashes.log` "Exception 0xc000001d (Illegal Instruction), Deliberate
stop: a failed ASSERT/PRECONDITION", `CvGameCore_Expansion2.dll+0x005ab1a6` (file offset, plain pin), its own minidump
`CvMiniDump_20261114_152049_5.4.8-39-g26234371e_Release.dmp`. The same stop as the proxy route's server
(`+0x785eea` in the x64 DLL, r2): one cause, two routes.

**23.5 Not verified / open.** The Squads row of section 20 (no lock line, EXE gone at `Detach Loc Database`, no dump
then): not reproduced in r1, r3, r5 (nor in ticket 0545's six batch-4 runs, section 20.1, 2026-11-18). Which UI transition keeps the CPK-owning Lua states alive into the next rebuild.
VernsTweaks arms (not rerun; the mechanism does not involve the extra mod, which only needs to make an activation with
VP). ~~A fixed CP helper was not tried.~~ Tried in 23.7 (ticket 0184, 2026-11-14): the upstream fix removes every
stepped statement and the flow goes through on both routes.

**23.7 The fix: upstream's "Step CPK database queries to the end", live on both routes (queue ticket 0184, 2026-11-14,
account B)** [C, L].

*The line.* `(1) Community Patch/Kit/Misc/CPK.Misc.CustomModOptionEnabled.lua` (CP v151 = Release-5.4.8), line 26:
`return query(name)() ~= nil`. It calls the iterator of a `DB.CreateQuery` statement once: the statement steps to its first
row (VDBE pc 17 of the LIMIT 1 program) and is never stepped to its end, so it is never reset. `DB.CreateQuery` is the
EXE's database binding, not the DLL's: the fork has no C++ definition of it (`git grep CreateQuery -- '*.cpp' '*.h'` at
x64-batch2: none; only Lua callers) [C]. The statement is reset only by its next call (the query is rebound) or freed with
its Lua state. The same pattern is in `CPK.Util.Color.lua` line 134 (`return query(type)()`, GetColorByType), which this
ticket found holding the engine's connection in the save-load flow below (`SELECT Type as type, Red as x, ... FROM Colors
... LIMIT 1`, pc 32, the same Vdbe from the load screen through the exit to the main menu and the next deactivation) [C,
live scans].

*Upstream has fixed it.* LoneGazebo/Community-Patch-DLL `14d16fa3d` "Step CPK database queries to the end (#13467)"
(JohnsterID, 2026-11-10, after Release-5.4.8) replaces the one-row reads with loops that step to the end in 7 files:
CustomModOptionEnabled, CPK.Util.Color, VP PlotMouseoverInclude, VP AssignStartingPlots (ASP:IsModActive), the VP EUI
compatibility PopulateUniques, InfoTooltipInclude's eight `GameInfo.Defines{...}()` reads, VPUI_core's
`BALANCE_CORE_JFD` read. Its message describes the same mechanism (a save load with another DLC list as its trigger).
Fork branch **`fix-cpk-query-step-to-end`** = Release-5.4.8 (`7307aefdf`) + that commit cherry-picked with `-x`
(`a9adfe3d7`), pushed to `origin` (the project's DLL fork). It merges cleanly into `origin/i0-ship-batch2` and
`origin/x64-batch2` (`git merge-tree --write-tree`: no conflict; the mod Lua of both equals 5.4.8 for these files). It
changes no C/C++ file, so neither DLL build (32-bit `i0ship-*`, x64) changes: the "fixed DLL" of the ticket is the same
DLL with these 7 Lua files. **The patch does not ship VP's Lua** (installer-notes.md section 3: an add-on to VP 5.4.8,
D28), so players get the fix with VP's next release, unless the installer replaces these 7 files: a decision for the MC.

*Method.* Laptop `tb1` (its own Steam EXE), testbed jobs of `shell/evidence/0184/job_0184.py` (a `live_session.py`
job with prep and restore by SHA-256 of the 7 Lua files, the 32-bit DLL or the proxy layout, the front end), driven with
`tools/testbed/live.py`, pictures by RenderDoc's thumbnail (the laptop's grab is black, quiet-testbed.md 21.3). Stock
route: the plain pin `i0ship-26234371` (F92881A3...) in the CP folder; proxy route: `provision.py set-proxy` with 0178's
build b5 (server 10E63BA7..., proxy BBAB3A02..., x64 DLL 74588135 = the D96 pin, fallback the plain pin),
`ServerRenderer = 0`, the instance's own config.ini. Front end: the vp-dll-dev MainMenu (the testbed base's own
MainMenu.lua loads the newest save on every ReloadUI, i.e. right after an activation: job `20261114-180516-bb73`; not
0174's flow). The instance's 7 files were 5.4.8's before every run (SHA-256 with CR dropped = `git show 7307aefdf:<path>`).
Statement scans: the job's `vdbe` command, a non-invasive x86 cdb (`-pv`) that lists every SQLite 3.7.17 Vdbe in the RUN
state (magic 0xbdf20da3; pc at +0x24, SQL at +0x6c, as 0174's `vdbe_read.py`), 7-11 s per scan.

*Results* [L, 2026-11-14 18:05-19:40 EST; excerpts `shell/evidence/0184/excerpts/`]:

| Job | Route, Lua | Flow | Active statements | Lock lines | End |
|---|---|---|---|---|---|
| `20261114-182654-649c` | proxy, 5.4.8 | Mods, NEXT; Back; NEXT 1-4 s after the deactivation's end (3 rounds) | after each activation **8 CustomModOptions, pc 17** (263 statements); the deactivation frees them (new addresses next time) | 0 | went through: the activation must start before the old front-end states are freed, which clicks on the laptop did not hit (clicks during a rebuild are dropped) |
| `20261114-181500-a6bf` | stock, fixed | the same, 3 activations | **0** (9 CustomModOptions statements, all pc -1) | 0 | went through |
| `20261114-184651-261d` | proxy, 5.4.8 | activate + load Tiny save `C5H-3000-TINY-T0018` (copy), Continue, Exit to Main Menu (the mods stay active), Mods, untick EUI, NEXT, Back | load screen and in game: 1 (Colors, pc 32); at the main menu after the exit: **9 (8 CustomModOptions + Colors)**; after the Back's deactivation: Colors still active | **deactivation: `Localization is locked` 2, `already in use` 2; the activation right after it: `Localization is locked` 2, `already in use` 2, `table is locked` 60** | engine database **836 tables 31,202 rows** (half built), server died in `CacheGameDatabaseData`, proxy `FATAL ... exit code 3` (as 0174 r2) |
| `20261114-191129-ecc7` | proxy, fixed | the same | **0 at every scan** (load screen, in game, main menu, after the rebuilds) | 0 | went through: engine and server **832 tables 43,682 rows**, IDENTICAL digest (`server.log`) |

Stock route, the same load flow (plain pin `i0ship-26234371`):

| Job | Lua | Active statements | Lock lines | End |
|---|---|---|---|---|
| `20261114-193609-6ad6` | 5.4.8 | load screen: Colors (pc 32); in game: Colors + one `Leader_Traits` read (pc 16); main menu after the exit: **8 CustomModOptions + Colors**; after the rebuilds: 8 new CustomModOptions | **deactivation (Back): `Localization is locked` 2, `already in use` 2**; the activation right after it: none; a second round (Mods, NEXT, Back): none | went through (no `table is locked`, no missing column, no dialog; exited by the main menu) |
| `20261114-192537-5665` | fixed | **0 at the load screen and the main menu**; in game the `Leader_Traits` read (pc 16) | 0 | went through |

So on the stock route this flow shows the lock (the deactivation keeps the old Localization, 0174 r1's row) but not the
half-built database: there the Colors statement was freed before the activation's `DETACH`, on the proxy route it was
not (the same Vdbe 249ed7cc before and after the Back in 261d) [L; why the timing differs is not known, I: the proxy
route's front end is slower to tear down]. The stock end with VP's dialog (0174 s1, workstation) was **not reproduced
on the laptop** (clicks during a rebuild are dropped there); for the stock route the acceptance rests on the statement
counts (MC #976 allowed this fallback): 5.4.8 leaves 8-9 statements stepped at the main menu and locks the
deactivation, the fixed Lua leaves none and logs no lock line. The `Leader_Traits` read in game is not one of the
fixed files: base EUI's `UI_bc1` PopulateUniques has the same `g_traitsQuery(...)()` line, which upstream's commit
leaves as it is (its message says so); it was freed before the main menu in both runs and was not seen blocking [L].

So the fix removes every stepped statement of these helpers on both routes and the flow that ends 5.4.8 on the proxy
route goes through with it.

*The lock needs the statements alive when the rebuild starts.* After an activation from the Mods menu the 8
CustomModOptions statements live until the next deactivation frees their front-end states, so an activation started
from the Mods list later does not lock (649c); 0174's r2/s1 locked because NEXT was queued during the deactivation and
ran before the old states were freed [I: the workstation queued those clicks; the laptop drops clicks during a rebuild].
After a game, the main menu holds the 8 CustomModOptions statements and the Colors statement of the game's load screen
(both made where `Game` is nil), and the deactivation locks on both routes; on the proxy route the next activation
locked too and the game ended (261d): the reproduction this ticket found, which fits 0153's VernsTweaks row and 0174
r1's "lock at the deactivation after a loaded game".

*Not verified.* The workstation (0174's machine); the 0174 s1 dialog end with the fix (not reproduced without it on the
laptop either); a save load from inside a game with another DLC list (upstream's trigger); the base EUI
`Leader_Traits` read as a blocker; VernsTweaks/Squads arms. Jobs, all on laptop `tb1`, each restored by hash and
`provision.py verify` 0: `20261114-180516-bb73` (exploration), `-181500-a6bf`, `-182654-649c`, `-184651-261d` (+
`-191051-07b3`, a read-only copy of its proxy run logs), `-191129-ecc7` (its stop was refused: the Lua channel is
silent in that front end; ended by the game's own main-menu EXIT and restored by job `-192420-ce28`),
`-192537-5665`, `-193609-6ad6`; `-184308-2bef` failed in prep (save not shipped; restored).

## 24. The Lua bridge in the proxy: listen, raise, replace a global (issue 0178, 2026-11-14, account B)

Design: the Architect's hand-over designs 3.2 (`research/07-architecture-decisions/architect-2026-11-14-hand-over-designs.md`),
rules R1-R3 (D132). Code: `proxy/src/proxy_ui_bridge.h/.cpp` (the bridge, no proxy-runtime dependency),
`proxy/src/proxy_ui_bridge_rt.h/.cpp` (the glue), one call each in `proxy_lua.cpp` (end of the RegisterScriptLibraries
hook) and `proxy_update.cpp` (`BeforeUpdate`), `vp_wire.h` `kRt_UiRecord`, the server's post handler
(`srv_rt.cpp` `OnPost` -> `srv_remote_main.cpp` `UiRecord`) and `SrvFeed::kUiMirror` (`server/src/feed/srv_feed.h`).
Switch: `vpproxy.ini ui_bridge=1` (default 0), `ui_bridge_test=1` adds the test table `VP64UiBridgeTest`. Tests:
`python -I proxy/tests/ui_bridge/test_ui_bridge.py --scratch DIR` (55 checks, the proxy's toolchain and MSVC ASan).

### 24.1 Facts found before the premise checks [C unless marked]

- **`ICvGame1::Update` runs on the EXE's game-core thread, not the main thread** (15.3 [L]). The design's "raise at the
  top of the per-frame Update" and "main thread only" cannot both hold. Built: raises are queued from any thread
  (`UiBridge::QueueRaise`) and run on the main thread by the bridge's own C listener of `Events.LocalMachineAppUpdate`
  (the per-frame main-thread event the harness already uses for window-touching calls,
  `research/05-graphics-composition/hooks-overlays-and-ceg.md` section 4). The Update hook only checks a heartbeat of
  that listener (a STALL line after 600 Updates without a frame). Asked of the MC as a non-blocking question (queue
  entry 830, 2026-11-14 16:0x).
- **When the proxy sees `RegisterScriptLibraries`.** In earlier proxy-route runs (c5h-c,
  `...\Logs\VP64\run\20261114-153035-36104-1\vpproxy.log`, 0172's arms) the engine called it on the boot thread
  (`[10040]`, bound to `ui`): once at the boot (18.36 s, state `28BE8AB0`, inside `CacheGameDatabaseData`, just before
  `CreateLuaThread(VP_LUAAPI)`), **not** at the game load (37-114 s), and once more at 262 s (state `6A854E50`) at the
  end of the session. [I] the EXE makes a new Lua state when it leaves a game, and the in-game UI contexts are threads
  of the state the front end already had; checked live in 24.2 (B1, B4).
- **EUI captures `UnitMoving` when its file loads**: `UI_bc1/UnitFlagManager/UnitFlagManager.lua` line 48 and the VP
  EUI-compatibility copy (`MODS\(3a) VP - EUI Compatibility Files\LUA\UnitFlagManager.lua` line 48) say
  `local UnitMoving = UnitMoving`, then call it in the `LocalMachineUnitPositionChanged` handler (lines 650 / 904).
  A replacement of the global therefore reaches EUI's flags only if it is in place **before** that context loads (or
  before a UI reload re-runs the file); later replacements are invisible to it. This is a constraint on H3 (design
  6.3).
- The ranged-attack arrow is raised by `DisplayBombardArrow(hexX, hexY)` in CP's `Overrides\Includes\Bombardment.lua`
  (included by `InGame.lua`, so a global of the `InGame` environment): `Events.SpawnArrowEvent(attacker:GetX(),
  attacker:GetY(), hexX, hexY)` when `attacker:CanRangeStrikeAt(hexX, hexY)`, else `Events.RemoveAllArrowsEvent()`.
- The minimap: EUI's `UI_bc1/MiniMap/MiniMapPanel.lua` lines 45-64 add an anonymous listener to
  `Events.MinimapTextureBroadcastEvent` (`uiHandle, width, height`; it calls `Controls.Minimap:SetTextureHandle`)
  and then call `UI:RequestMinimapBroadcast()`; CP's `Overrides\MiniMapPanel.lua` lines 87-92 do the same with a
  global `OnMinimapInfo(uiHandle, width, height, paddingX)`.

### 24.2 Premises B1-B4 [L, c5h-b, 2026-11-14]

Runs (all on c5h-b, proxy route, `VP64Options.ini ServerRenderer = 0`, the instance's own config, x64 DLL = the pin
74588135; driver `shell/evidence/0178/run_0178.py`, probes `probe_0178.py`): **r1** build b1 (proxy d8184b17, the bridge
installed from `RegisterScriptLibraries`), huge save copy `C5H-0178-HUGE_0350` (of `C5H-VP8P-HUGE_0350`, turn 350),
16:19-16:27; **hold** the same build, Tiny copy `C5H-0178-TINY_0018` (of `C5H-3000-TINY-T0018`), hand probes over the
Lua channel 16:28-16:41; **r2** build b2 (proxy cef56245, the install of 24.3), huge copy, 16:51-16:58. Order note: the
bridge's record, queue and post code was written before the checks because no instance was free before 16:18 (0158's
hold); the install point, the only part that depended on B1, was changed after r1.

- **B1 holds, but not through `RegisterScriptLibraries`.** [L r1, hold] The `lua_State` the engine passes to
  `RegisterScriptLibraries` is a short-lived registration thread: it is freed right after (its address `248866D8` was
  reused by the proxy's own `VP_LUAEXEC` thread at 16.99 s in the hold run), and the EXE **copies the DLL's globals from
  it into every UI context's environment** when the context is made (the bridge's test table set there at boot was
  found in all 152 in-game environments, never in `_LOADED._G`). It is called once at the boot and once per new front
  end (after a game is left, also on a load from inside a game: r2 186.4 s), never per game. Listeners added there never
  fired (r1: 0 records, also after the load). The long-lived in-game state is the **`InGame` context**:
  `_LOADED._G.Threads` maps each of the 154 context threads to its environment (`StateName`), and the bridge reaches it
  from any Lua call of a DLL function on the main thread (the thunks, `FuncThunk`), with the Lua C API only (R2).
- **Every UI context has its own `Events` table** (154 environments, 154 distinct `Events` objects; `__index` is a
  function that makes the event objects on demand) and its own `UnitMoving` (a C function, `debug.getinfo` what = "C";
  `_G.UnitMoving` is another one). An event object has `Add, Remove, RemoveAll, Count, Call, CallImmediate, Dispatch`;
  `Add` is called with a dot (`Events.X.Add(f)`), `Count` needs the colon (`Events.X:Count()`; a dot call raises "Not a
  valid instance ... '.' instead of ':'") and counts that context's own listeners only. A raise through any context's
  object reaches the listeners of all contexts (B3 below).
- **B2 holds.** [L r2] A C listener added to the `InGame` context's event objects is called on the main thread
  (`wrongThread` 0) when the EXE raises `MinimapTextureBroadcastEvent` (record `1954 320 190 0`: handle, width,
  height, paddingX; the EXE raises it once at the load and on `UI:RequestMinimapBroadcast()`) and when Lua raises
  `SpawnArrowEvent` / `RemoveAllArrowsEvent` (`DisplayBombardArrow(84, 33)` of CP's `Bombardment.lua` with the active
  player's unit at (84, 31) selected, run on the main thread from an `InGame` handler). The server received every
  record on the ui channel (`server.log` "ui records", no sequence gap). Open: the arrow records carried `0 0 0 0` (see
  24.4).
- **B3 holds.** [L r2] Every context's `Controls` table was wrapped by a counting proxy (152 contexts) and
  `SerialEventGameDataDirty` raised (a) by Lua from an `InGame` handler on the main thread and (b) by the bridge
  (`QueueRaise` from the Lua channel on the core thread, run by the bridge's drain on the main thread at the next frame):
  the same contexts reacted with the same counts, `DiploCorner:3, TechPanel:34`, before and after a load from inside
  the game; no touch in two idle windows of 1.5 s. `MinimapTextureBroadcastEvent` with the EXE's own arguments: Lua
  `MiniMapPanel:4`, the bridge `MiniMapPanel:4`.
- **B4, load from inside the game: holds.** [L r2] `Events.PlayerChoseToLoadGame` from an `InGame` handler: a new
  front-end state (registration at 186.4 s), a new `InGame` environment (`72053A90`, old `23338098`); the bridge
  installed into it at 195.2 s and records, raises and the B3 comparison worked after the load. The old context's
  listeners went with it (no stale call after the old environment was gone). **The EXE does not drop listeners of a
  context that stays**: every reinstall into the same environment kept the older closures registered (r2: 12 drains per
  frame after 12 installs, 45,729 stale calls in 8 minutes), hence 24.3's marker.

### 24.3 The bridge as built (build b4: proxy bbab3a02, server of b3; repo 620877de)

- **Install** (`UiBridge::OnLuaEntry`, from `FuncThunk`, main thread only, at most every 250 ms): walk
  `_LOADED._G.Threads` (or its `__index` backing) for the environment whose `StateName` is `InGame`; install when that
  environment does not carry the current generation's marker (a light-userdata key, value = generation) or when the
  current generation's drain ran and then stayed silent for 10 s while Lua calls went on. The `Add` calls run on the
  `InGame` context's own thread (`lua_cpcall` on that thread), into its own `Events` table: `SpawnArrowEvent`,
  `RemoveAllArrowsEvent`, `MinimapTextureBroadcastEvent`, `DisplayMovementIndicator` (records) and
  `LocalMachineAppUpdate` (the drain). `UnitMoving` of the `InGame` environment is replaced by a C closure that counts
  and calls the original. [L r3, b4] one install per `InGame` environment (gen 1 at 52.6 s, during the load, 9 contexts
  existing; gen 2 after the load from inside the game), 0 stale calls, 0 wrong-thread calls.
  *Superseded (b2, r2):* a 2 s silence rule reinstalled every 2 s during the load screen (the EXE raises no
  `LocalMachineAppUpdate` for in-game contexts until the game is shown: 59-82 s on the huge save), 12 generations, all
  kept registered by the EXE.
- **Records**: 32 bytes (`UiBridge::Record`: version 1, kind, flags, four int32, sequence, generation), posted on the
  ui channel (`kRt_UiRecord`, vpipc post + flush), counted by the server (`server.log` "ui records", the report line
  "ui records (issue 0178)") and pushed into the renderer's feed as `SrvFeed::kUiMirror` (no-op with render=0). The
  post handler runs on the server's ui service thread and touches no DLL object (R1).
- **Raise**: `QueueRaise` (any thread, 64 slots) -> the drain (main thread, next frame) calls `Events.<name>(args)`
  through the `Events` table of the installed context (kept as the drain's upvalue), under `lua_pcall`.
- **Switch**: `vpproxy.ini ui_bridge=1` (default 0); `ui_bridge_test=1` adds the test table `VP64UiBridgeTest`
  (`QueueRaise`, `Stats`) through the registration thread, so a copy is in every context, and a record per
  `UnitMoving` call. Off: `FuncThunk` pays one load of a flag.

### 24.4 Event arguments: what Lua listeners receive [L, hold run on b4, Tiny copy, 2026-11-14 17:23-17:25]

`shell/evidence/0178/args_probe_0178.py` and `dispatch_probe_0178.py`, a Lua listener in the `InGame` (and
`MiniMapPanel`) context keeping `select('#', ...)` and the values:

| Raised by | `SpawnArrowEvent(11,12,13,14)` | `SerialEventGameDataDirty(21,22)` |
|---|---|---|
| Lua, `Events.X(...)` (as `Bombardment.lua` does) | listener called, **0 arguments** | called, 0 arguments, in every context (`InGame` and `MiniMapPanel`) |
| `Events.X.Call(...)`, `Events.X:Call(...)`, `Events.X:CallImmediate(...)` | called, 0 arguments | called, 0 arguments |
| `Events.X:Dispatch(...)` / `Events.X.Dispatch(31, 32)` | called **with** the arguments (the colon form passes the event table first) | **only the raising context's own listeners** (`InGame` got `31, 32`, `MiniMapPanel` nothing) |
| the bridge (`QueueRaise` -> drain -> `Events.X(...)`) | called, 0 arguments | called, 0 arguments |
| the EXE (`MinimapTextureBroadcastEvent` at the load, on `UI:RequestMinimapBroadcast()`) | - | (minimap) `1954, 320, 190, 0` |

Consequences: (1) the bridge's raise behaves exactly as a Lua file's raise (B3), and a raise from Lua carries its
arguments to the EXE's own consumer (the Arrow system draws `Bombardment.lua`'s arrows) but **not** to Lua listeners;
(2) **listening to `SpawnArrowEvent` cannot give H1's arrow end points** (design 4.2): the records carry `0 0 0 0`
(r2, r3). Options for 0179: replace the `InGame` context's `Events.SpawnArrowEvent` object (or the global
`DisplayBombardArrow`) with a wrapper that records the arguments and calls the original, or read the end points where
Bombardment.lua takes them (the head selected unit or city and the hex under the mouse); not tried here;
(3) a raise that must give Lua listeners arguments (H3's `LocalMachineUnitPositionChanged` for EUI's flags) has to use
`Dispatch` on the **target** context's own `Events` table (`UnitFlagManager`'s), not a plain raise; not tried here;
(4) EUI's `UnitFlagManager` keeps the `UnitMoving` it copied into a local at load (`local UnitMoving = UnitMoving`,
24.1): replacing the `InGame` global does not reach it, and every context has its own `UnitMoving`, so H3 (design 6.3)
needs the replacement in `UnitFlagManager`'s environment before its file runs; the DLL-globals copy at context creation
(24.2 B1) is a possible route (the bridge's test table arrives that way), not tried.

**Correction, 2026-11-14 19:49-20:0x EST (epic 0179, task 3801 [L]; granted by the MC through lead-0179):** whether
listeners receive a Lua raise's arguments depends on **the event, not on the raise or its context**. On c5h-c (stock 32-bit
pin `i0ship-26234371-ia`, and the joined D33 product path with this bridge, proxy 5651efed / server.exe 71a0c296 from
`task-3801-arrows-live` a2cb8b59), Tiny copy `C5H-0179-TINY_0009V`: `SpawnArrowEvent` reaches a Lua listener in `InGame`
with **0** arguments whether raised from `InGame` (`Bombardment.lua`) or from `WorldView`'s context, and the bridge's kind-1
records carry `0 0 0 0`; **`DisplayMovementIndicator(bool)` arrives with its bool** from both contexts (VP `WorldView.lua`
raises it on every path update; `true` while a move preview is up, `false` when the mode ends; 23 calls in stock, 1011 in
the joined arm), and the **bridge's kind-4 records carry it** (record #1000, a `true` raise, logged `a 1 0 0 0` in
`server.log`). `UIPathFinderUpdate` (raised by the EXE) carries its node table (x, y, turn). [I: the EXE declares an
argument list per event and passes Lua's arguments on only where one is declared.] So option (2) above still holds for
the arrow; the wrapper of `InGame`'s `Events.SpawnArrowEvent` was tried live and records the four integers while the
EXE keeps drawing the arc (`research/05-graphics-composition/arrows-and-path-line.md` 6.1). MC #1030 keeps the EXE's arc
in increment 2, so 0179 consumes no arrow record; the wrapper is the input for follow-up 0199.

### 24.5 B4 exit and restart, parity, tests [L, c5h-b, 2026-11-14 17:25-17:46]

- **Exit to the menu and start again** (b4, Tiny copy): `Events.ExitToMainMenu()` from an `InGame` handler
  (`shell/tools/mapchange.py exit`), then the save loaded by posted clicks (SINGLE PLAYER (0.500, 0.407), LOAD GAME
  (0.498, 0.471), the save row, Load Game (0.622, 0.705) of the 2554x1410 client; each checked on a picture first,
  pictures in the Visual Checks feed as DIAGNOSTIC). The front end got a new registration thread (103.6 s), the next
  game's `InGame` a new install (gen 2 at 105.9 s, 9 contexts present), and in that game: drains, records, the B3
  comparison identical (`DiploCorner:2, TechPanel:34` for both raises), `UnitMoving` replaced and counted, 0 stale,
  0 wrong-thread calls (`shell/evidence/0178/exit-restart/`). The proxy's Lua channel also answered in the restarted
  game. **The harness's front-end auto-load does not fire again after an exit** (MainMenu.lua's load token in the mods
  database, `Modding.SetSystemProperty`): a restart needs the menu clicks above.
- **UI reload: not verified** (no way to trigger one was found); the unit test covers listeners that vanish while the
  environment stays (reinstall after 10 s of a silent drain).
- **Parity (R1)** with `proxy/tools/live/arm.py`, Tiny copy `C5H-0178-TINY_0018`, 30 AI turns with the checklist's UI
  steps (18 -> 48), bridge on (`ui_bridge=1 ui_bridge_test=1`) against off (`ui_bridge=0`), same build b4:
  `eq_stats.py` 376 keys after the load and 455 at turn 48, **identical, 0 differing keys**
  (`shell/evidence/0178/parity/`; arms 17:25-17:31 and 17:31-17:38, both rc 0).
- **Tests**: `proxy/tests/ui_bridge/test_ui_bridge.py` 57/57 checks on the proxy toolchain and under MSVC ASan (a fake
  engine with per-context `Events`, `_G.Threads`, the registration copy, context destruction, a new state); 
  `proxy/tools/test_proxy_rt.py` 79/79 on the b4 layout (unit test with the `ui_bridge` keys).
- Not measured: the cost of the install check in `FuncThunk` with the bridge on (one `GetTickCount` and thread-id
  compare per DLL Lua call, a walk of `_G.Threads`, 154 entries, at most every 250 ms); with it off one flag load.
  No performance claim is made.
- Not done: the server-side summary line `ui records (issue 0178)` is written at `kRt_Shutdown`, which the harness's
  stop (a process end) never reaches; `server.log` has the first record of each kind and every 1,000th.
  *(2026-11-17, issue 4754: the server also writes the line, with "(so far, at frame mark N)", at a frame mark at most
  once a second; 24.6.)*

### 24.6 The unit positions: kinds 6 and 7, the positions-only install (issue 4754, epic 0040, 2026-11-17, account B)

Contract: `research/05-graphics-composition/unit-feed-contract.md` 3.3; breakdown
`research/07-architecture-decisions/epic-0040-breakdown.md` 1.4. Code: `proxy/src/proxy_ui_bridge.h/.cpp` (block "issue
4754"), the glue `proxy_ui_bridge_rt.h/.cpp`, the server `server/src/remote/srv_remote.h` (`SrvRemote::UiRec`) and
`srv_remote_main.cpp` (`UiRecord`, `UiRecordSummary`), `server/src/feed/srv_feed.h` (the `kUiMirror` comment). Branch
`task-4754-position-records`.

**What the bridge does [C].**
- Two C listeners in the `InGame` context, each with its generation as upvalue 1 (24.3's rules):
  `LocalMachineUnitPositionChanged(playerID, unitID, {x, y, z, w})` makes one kind-6 record: `a[0]` = unit id,
  `a[1..3]` = the bits of `(float)x`, `(float)y`, `(float)z` (the table's fields read by `lua_getfield`; `w` is not
  carried), `flags` = player << 16. The player must be a whole number 0..255 and the unit a whole number in int32;
  else, or with a non-table or a table without a number in x, y or z, the call is counted (`badArgs`) and makes no
  record. `UnitMoveQueueChanged(playerID, unitID, bRemainingMoves)` makes the arrival record (kind 6, `flags` |=
  `kPosArrival` = 1 << 24, `a[1..3]` = the unit's last position) only when `bRemainingMoves` is false or nil.
- **F6, the arrival rule.** The arguments are `(playerID, unitID, bRemainingMoves)` [C, the stock
  `Assets/UI/InGame/UnitFlagManager.lua` 1006 and `CityBannerManager.lua` 1174, which act only `if not
  bRemainingMoves`; UnitFlagManager.lua 998 calls its own handler with `false` for a teleport]. So the bridge reads it
  as those files do: true = the queue changed with moves left (counted as `queueRemaining`, no record). Live arguments:
  24.6.1. The last positions sit in a fixed table of 8,192 slots (open addressing, keyed by player and unit), cleared
  at a new `InGame` environment and when three quarters full (`tableClears`); an arrival of a unit with no known
  position carries x = y = z = 0 (`arrivalsNoPosition`).
- **The frame mark.** The kind-6 records of a frame are held on the main thread (256 slots) and posted by the drain
  (the bridge's `LocalMachineAppUpdate` listener) after one kind-7 record: `a[0]` = the frame number, `a[1]` = the
  number of kind-6 records that follow, both under the same generation; a frame without positions makes no mark. A
  frame with more than 256 posts them early, after a mark of their own with the same frame number (`earlyFlushes`).
  Held records of an older generation leave before a new install (under their own generation).
- **The frame number is the drain's number in the current generation** (`Stats::drains`, which a new install sets to
  0; the generation is in every record). Reason: the EXE's Present count lives in the shell's shared section (the camera
  block's `presentCount`, `shell/common/shell_shared.h`), which the proxy does not map, and the bridge's `Hooks` give no
  frame source; the drain runs once per EXE frame on the main thread (24.1), so its count is the EXE's frame count from
  the install on. A Present count would need the glue to open the section (`VPSHELL_SECTION` in the EXE's environment)
  [I]; not done.
- **The positions-only install.** `UiBridge::ConfigurePositions(true)` without `Configure`'s `enabled` installs only the
  two listeners and the drain: no test table, no `UnitMoving` replacement, no generic listener, `QueueRaise` refused;
  the install log names it (`listeners 0 of 0, unit positions 2 of 2 (positions only)`, `global UnitMoving kept`). With
  `ui_bridge=1` the whole bridge gets the two listeners as well; without `ConfigurePositions(true)` `ui_bridge=1` is
  unchanged (the unit test checks no listener and no kind-6/7 record). Turned on after an install, the next check
  installs a new generation into the same environment (`the positions were asked for`). The glue's entry for the unit
  option is `ProxyUiBridgeRt::RequestUnitPositions()` (4759 calls it; any thread; it takes effect at once when the ui
  thread is known from a registration, else at the next one). TEST ONLY: the environment variable
  `VPPROXY_TEST_UI_POSITIONS=1` in the EXE's process does the same at the glue's first use (a `LOUD: TEST ONLY` line in
  vpproxy.log); live runs before 4759 use it through the harness launch's inherited environment.
- **The server [C].** `UiRec::ToEvent` maps a record to `SrvFeed::kUiMirror` as before (flags = the record's kind,
  `a[0..3]` copied, plot = turn = -1), except kind 6: **`Event.plot` = the player (flags bits 16-23), `Event.turn` = 1
  for an arrival, 0 for a position** (srv_feed.h's comment). The report line now reads `ui records (issue 0178): N
  received (arrow .., remove-arrows .., minimap .., movement .., unit-moving .., unit-position K (positions P, arrivals
  A), frame-mark M (announcing K'), other ..), bad .., sequence gaps .., last seq .. gen ..`; the same line is written
  to server.log with `(so far, at frame mark N)` at a frame mark when the last one is a second old or more, so a run
  stopped by the harness still has counts (24.5's gap).

**Tests [L offline, 2026-11-17].** `python -I proxy/tests/ui_bridge/test_ui_bridge.py --scratch DIR`: 94/94 checks (57
before) on the proxy toolchain and under MSVC ASan (x86, canary caught): one position (the float bits exact, player
bits, the mark before it with the drain's number), a walk's frame (one mark counting 3, the arrival with the last
position), `bRemainingMoves` nil / true, an unknown unit, ten unreadable calls (non-table, short table, a string field,
no position, a string or out-of-range player, a fractional unit, no unit, no player) counted without a record, a call
on another thread, 300 positions in one frame (an early mark of 256 and one of 44, same frame number, no sequence gap),
a new environment (held positions leave under the old generation; last positions forgotten), positions asked for after
a full install, `ui_bridge=1` alone unchanged. `python -I server/tests/feed/run_ui_record_test.py --out DIR --asan`:
34/34 (decode and its refusals, the event layout for kinds 1, 4, 5, 6, 7 and players 0, 5, 63, 255, stray flag bits,
Push/Drain with the feed on and off, the counters and the report line), the injected failure caught, ASan clean.
Product build (`server/evidence/0040/4/build_4754.sh`, release, pin 04b1211b8): server.exe `e0590839...`, proxy
`822f978d...`, composite `cb256097...` from d7918284b.

#### 24.6.1 Live run (acceptance 3) [L, c5h-a, 2026-11-17]

Driver `server/evidence/0040/4/live_4754.py` (run by `run_4754.sh`): 0375's product-path prep (4465 / 4460 / 3308:
ServerRenderer = 1, the composite and its manifest, D33 with Overlay kept on, `ui_bridge` not set, no server key), the
Tiny copy `C5H-0040-4754-TINY_0018` (of `C5H-3000-TINY-T0018`, E337244C), `VPPROXY_TEST_UI_POSITIONS=1` at the launch;
probe_0317's listeners plus one on `UnitMoveQueueChanged` split by `bRemainingMoves`; snapshots of the Lua counts and
of server.log's size in quiet windows (3 s without a position event) before each stimulus: a warrior created beside
the capital, quick movement off, a two-plot walk; quick movement on and the move back (F9's question: a quick move
without `UnitMoveQueueChanged`); quick movement off and two AI turns; a final quick move whose first mark writes the last
"so far" line. Each window's server counts are the difference of the first "so far" lines after its two snapshots.

Run r1, 2026-11-17 07:42-07:48 EST, pool instance c5h-a (claim W-4754), the build above in the pin layout
x64-04b1211b-pfdee656 (x64 DLL 29F58E17...), turn 18 -> 20, player 0. Evidence `server/evidence/0040/4/r1/`
(acceptance.txt, plan.json, the "ui records" lines of server.log, the "ui bridge" lines of vpproxy.log).
- The switch: `LOUD: TEST ONLY: VPPROXY_TEST_UI_POSITIONS=1` at 9.7 s (the harness launch passes the shell's
  environment through to the EXE); gen 1 installed at 43.6 s, `listeners 0 of 0, unit positions 2 of 2 (positions only),
  ... global UnitMoving kept`; one install, no reinstall.
- **Counts, server against Lua, per window: equal in every one.**

| Window | Server positions / arrivals | Lua `LocalMachineUnitPositionChanged` / `UnitMoveQueueChanged` (bRemainingMoves false or nil, true) |
|---|---|---|
| create, select, quick movement off | 0 / 0 | 0 / 0 (0, 0) |
| the two-plot walk (quick movement off) | 52 / 1 | 52 / 1 (1, 0) |
| the move back with quick movement on | 2 / 0 | 2 / 0 (0, 0) |
| two AI turns (quick movement off) | 600 / 3 | 600 / 3 (3, 0) |
| whole run | 654 / 4 | 654 / 4 |

  Frame marks 623 for those 658 kind-6 records; bad 0, sequence gaps 0 (last seq 1282, gen 1). The kRt_Shutdown line
  was not reached (harness stop); the counts are the "so far" lines.
- **F6 [L]:** every call had 3 arguments, `(number player, number unit, boolean false)`: `0, 1052, false` (the walk's
  end) and `2, 1009, false` three times (an AI unit). No call with `true` came in this run, so the `queueRemaining` path
  is covered only by the unit test.
- **F9 for 4755 [L]:** the quick move (2 plots, quick movement on) made 2 position records and **no**
  `UnitMoveQueueChanged`, so **no arrival record**: with quick movement the arrival has to come from elsewhere (F9's
  rule, 4755).
- `LocalMachineUnitPositionChanged`'s table as the bridge read it: `{x -1183.995, y 332.5835, z 11.99686, w 0}` (world
  units, w 0), as 0317 logged.
- The frame number at the last mark was 4936 (drains since the install at 43.6 s, about 173 s of play: roughly 38 per
  second, the instance's frame cap) [L].
- Not verified live: a frame of more than 256 positions (early flush), a reinstall with positions held, `ui_bridge=1`
  with the positions, and 4759's `RequestUnitPositions` (only the TEST ONLY env path ran).

### 24.7 The frame hook and the drain-only install (ticket 0487, 2026-11-17)

(Numbered 24.6 on ticket 0487's branch, as epic 0040's unit positions were on its own; renumbered 24.7 at epic 0040's
merge of main, 2026-11-17 17:10, lead-0040. Ticket 0487's "proxy-design 24.6" means this section.)

**With both on (epic 0040's merge, 2026-11-17) [C, code; offline tests below]:** the install has three reasons, the
whole bridge (`ui_bridge=1`), the unit positions (24.6, `ServerUnits = 1`) and the frame hook. The glue arms when
`Active()` or `PositionsOn()`; an install with the positions and the hook but not the whole bridge is the positions-only
install of 24.6 (its drain carries the hook), and only with neither the whole bridge nor the positions is it the
drain-only install with 0487's log line. Offline on 17175d444: `test_ui_bridge.py` 9/9, `test_proxy_rt.py --only-units
--asan` 72/72, `--only-uidraw --asan` 9/9 (`research/07-architecture-decisions/options-file.md`, the 17:10 entry). Not run
live with both on (a game with `ServerUnits = 1` on a build after this merge).

`UiBridge::SetFrameHook(fn)`: the drain calls `fn` once per frame on the main thread, after the queued raises
(`Stats::frameHookCalls`). With a hook set and `ui_bridge=0` (the product) the bridge installs the drain alone into the
`InGame` context (log `ui bridge: gen N installed (drain only, for the frame hook) ...`): no event listener, no
`UnitMoving` replacement, no test table; the generation marker, the 10 s silence rule and the heartbeat work as in 24.3.
`UiBridge::Active()` (on, or a hook set) replaces `Enabled()` in the glue's checks, so `FuncThunk` makes the install
check whenever a hook is set. The first user is the growth of the EXE's UI draw buffers (`proxy/src/proxy_uidraw.cpp`,
`research/03-dll-engine-boundary/upstream-13406-product.md` section 9); the bridge itself stays EXE-free (R2).
Live [L, c5h-a, 2026-11-17 15:30]: the drain-only install came at 74.6 s into the huge save's boot (the first DLL Lua
call of the shown game), the first drain 27 s later. The bridge's own unit test (`proxy/tests/ui_bridge/`) was not
extended for the drain-only mode (outside 0487's files); it is covered live and by the unchanged suite.

## 25. server_cfg_extra=: test-only server.cfg lines on the product path (queue ticket 0307, account B, 2026-11-14)

Asked by epic 0168's worker 3954 (queue #1168, MC #1171): `render_fow=1` is the server's default, so a live arm with
`render_fow=0`, `render_overlay=0` or `overlay_tap=0` needs a server.cfg key, and on the product path the proxy writes
server.cfg itself (`server_config=` takes the boot off the product path).

**The key [C].** vpproxy.ini `server_cfg_extra=<file>` (the implementer's choice of the two forms the ticket allowed: one
file, not repeated keys; relative paths resolve against the proxy's folder like every other path key). TEST ONLY: the
shipped ini, `provision.py`'s `DEFAULT_PROXY_INI` and the release tools never set it (grep of the repository,
2026-11-14). Code: `proxy/src/proxy_rt_logic.cpp` `ParseServerCfgExtra`, `LoadServerCfgExtra`, `ServerConfigFile`;
`proxy/src/proxy_rt.cpp` `Boot`.
- The file: one `key=value` per line; blank lines and lines starting with `#` or `;` skipped; key and value trimmed and
  written back as `key=value` (the server's reader, `srv_main.cpp` `LoadConfig`, trims nothing: `render_fow = 0` would be
  the key `render_fow `); `=` inside a value is kept. A line without `=` or with an empty key makes the whole file
  invalid.
- server.cfg is now `ServerConfigFile(ServerConfigText(...), options_file, the shell's lines, path, lines)`: the text of
  before (base, `options_file=`, `render=1 render_section= render_handover= render_texture=`), then, only with the key, one
  comment line `# TEST ONLY: server_cfg_extra <path> (issue 0307; a later key wins)` and the lines in file order. The
  server's reader keeps the last value of a key (`s_cfg[line] = ...`, a map), so an appended key overrides one the proxy
  wrote; `lua_probe` is the one key the reader collects as a list. Without the key the text is byte for byte the old one
  (unit test).
- vpproxy.log: `LOUD: TEST ONLY: vpproxy.ini server_cfg_extra=<path>: N line(s) appended to server.cfg (issue 0307;
  never in a shipped ini)` and `LOUD: TEST ONLY: server.cfg += <line>` for each line.
- Failures are boot failures (the D125 route: `SetFail("server-start", "server-cfg-extra", ...)`, `boot FAILED:
  server_cfg_extra (TEST ONLY) <why>; DllGetGameContext returns NULL`; with `pack=1` the fallback or the D33 no-play
  rule and the report, as every other boot failure), never a silent skip: a file that cannot be read, a malformed line
  (`line N: no '='`, `line N: an empty key`), and the key beside `server_config=` (the proxy writes no server.cfg then).
  The check runs before server.cfg is written and before the server starts.
- How to set it in a live run: `tools/harness/README.md` "server.cfg lines on the product path" (`set_ini.py` sets and
  unsets it; the harness's launch never writes vpproxy.ini, so it gets no option of its own); a worked product-path run:
  `shell/evidence/0307/run_0307.py` (3308's prep / run / restore with a Tiny save copy and the key).

**Tests [L offline, 2026-11-14 23:05-23:10].** Build `server/build_server.py --config release --deps <a copy of 9045's
pin deps> --offline` (server.exe `a3ade1d4...`, proxy `7ad15d8f...`, x64 DLL `74588135...` = the pin's release DLL) from
branch task-0307-proxy-cfg-extra. `proxy/tools/test_proxy_rt.py` 87/87 (79 before): unit 94/94 with 15 new checks
(default empty, path resolution, absent key byte for byte, order, trimming, comments, duplicates in order, empty file,
no `=`, empty key, missing file, a file on disk, a bad file); loader cases `cfg-extra` (the real server boots, server.cfg
ends with the comment and both lines, both LOUD lines in vpproxy.log, d28's server.cfg has no TEST ONLY line),
`cfgextra-missing` and `cfgextra-bad` (NULL, `boot FAILED: server_cfg_extra (TEST ONLY)` with the reason, no server.cfg,
no server started). `unit.cpp` + `proxy_rt_logic.cpp` once under MSVC `/fsanitize=address` (x86): 94/94, no report, the
canary caught.

**Live product-path boot [L, 2026-11-14 23:12-23:17 EST, Goldberg instance c5h-e on F:].** `shell/evidence/0307/run_0307.py`
(3308's flow: ServerRenderer = 1, the composite with its manifest, D33 with Overlay kept on, no `server_config=`; a copy
of the Tiny save C5H-3000-TINY-T0018, one AI turn, the harness's safe stop, restore by hash: every backed-up file equal
or absent again, vpproxy.ini back to the hash recorded after set-proxy, `provision.py verify` rc 0). Build as above;
composite `5cc03a39...`. `extra.cfg` = `render_fow=0`, `render_overlay=0`. The run folder's server.cfg ends with the
comment and both lines after `render_handover=ring`; vpproxy.log has the three TEST ONLY lines and no other LOUD line;
server.log (and render.log) says `render: terrain scene: render_overlay=0: no overlay (terrain t1 and water t9
transparent)` at 3.76 s, so an appended key reaches the server's renderer. `render_fow=0` reaches the server only as a
server.cfg line: main's server.exe does not read `render_fow` (epic 0168 adds it; not on main at 85b7bc66). Evidence:
`shell/evidence/0307/acceptance.txt`. The run's end shot (Visual Checks `vc-20261114-231521-workstation-478e`) is
uniform gray and validates nothing; why was not examined.

## 26. Human play through the proxy: the human-only sends in a long game (ticket 0322, 2026-11-15, account B)

Every live run of increment 1 before this was AI play (autoplay, 86 AI turns of the huge save, epic 2500's checklist).
Ticket 0322 plays the human seat with a scripted player on the thin client (increment 1's configuration, MC ruling
2026-11-15 00:29: `ServerRenderer = 0`, the instance's config.ini untouched, no `vp64helper on`), so the DLL's human-only
`send*` calls (`server-host.md`, the table of `send*` methods) are hit and the EXE's net layer answers them through the
proxy.

### 26.1 Set-up [L]

- Instance **c5h-h** (Goldberg copy under `F:/c5-instances`, workstation), claimed through `tools/harness/pool.py`; its
  standard files backed up by SHA-256 (`C:/c5-64bit-work/0322/std-backup`) before `provision.py set-proxy`.
- Build: server.exe and the proxy from this repository at **2d59c2326** (`repo_dirty=0`), the x64 DLL of the pin
  **04b1211b8** (9045's release deps reused, `--deps`; DLL `74588135...` = `deps/dll.lock`'s), `server/build_server.py
  --config release --offline` (93 s): server.exe `7de634bc...`, proxy `e8345b78...`; folder `C:/c5-64bit-work/0322/b`.
  The 32-bit fallback in the layout is the instance's `i0ship-26234371-ia` (`3c2f47da...`). vpproxy.ini: `mode=on
  local_answers=1 local_verify=0 lua_mirror=0 counters=1`, `pack=0` (a test layout: the D125 report and popup are not
  armed; `<user>/VP64/reports` stayed empty in every run).
- Driver `tools/humanplay/` (its README): every order goes the UI's own way (`lua_chunks.py` docstring table: unit
  selection + `Game.SelectionListGameNetMessage`, `Game.CityPushOrder`, `Game.CityPurchaseUnit/Building`,
  `Network.SendResearch / SendUpdatePolicies / SendFoundPantheon / SendChangeWar`, `CONTROL_FORCEENDTURN` with units
  left ready; an AI offer accepted the way DiploTrade's Accept button does, `UI.DoProposeDeal()`), the harness's
  diplomacy handler refuses the other AI screens, and every 10 turns a map picture, the notification list, the city
  screen, the tech tree, an AI's leader screen and a save (the harness checklist's steps). Human mode through the Lua
  channel: `Game.GetAIAutoPlay()` read 0 in every run, so nothing was set.

### 26.2 Ending a turn with units unready: the DLL passes one idle unit [C, L]

A forced end of turn (`CONTROL_FORCEENDTURN`, Shift+Enter) does not end the turn while two or more units are awake, idle
and able to move: `CvGame::updateMoves` (`CvGame.cpp` 9150-9165 at the pin) lets the first such unit through by
clearing `IsForceEndingTurn`, and the second one calls `sendTurnUnready` and `setEndTurn(false)`; a unit whose move
mission completes at the end of the turn with moves left does the same (9118-9131) [C]. Live on c5h-h through the proxy
2026-11-15 01:05: two awake units, `Game.DoControl(CONTROL_FORCEENDTURN)` twice, the turn stayed active; one unit given
"skip turn" (`MISSION_SKIP` through the selection), one left awake, the next forced end ended the turn [L]. This is the
DLL's own logic (the same source in the 32-bit DLL), not the proxy. So the driver gives every ready unit but one "skip
turn" before Shift+Enter, and repeats once when a unit wakes (`sendTurnUnready`). The harness's `turns.end_turn` never
met this because it sets every unit's moves to 0 first (not a human path).

### 26.3 Tiny game 1 (`tiny150`): 150 human turns, new game [L, 2026-11-15 01:16-01:51 EST]

New Vox Populi game (`prepare.py --new-game "map=Continents;size=WORLDSIZE_TINY;ais=3;minors=4"`, Prince, CP+VP+EUI).
**Fixed seed:** the front end's new-game hook (`vp-dll-dev` MainMenu.lua `vpDevStartNewGame`) has no seed key, so the
map and sync seeds are the EXE's; the game's fixed point is its turn-0 save (`c5h-c5h-h-t0000-011922`, kept in `F:/c5-64bit-work/0322/saves`, written by the
driver's first round), from which it can be replayed. Turns 0-149 played by the driver, then the last save
(`c5h-c5h-h-t0150-014847.Civ5Save`) and the harness's safe stop (rc 0); vpproxy.ini restored by hash.

| | result |
|---|---|
| crashes, restarts, D125 reports | none: EXE and server ran the whole game in one process each; server-report.txt ends with the usual "the engine is gone: PeerDied" of a harness stop (18.8); no report folder |
| turns | 150 of 150 ended by `CONTROL_FORCEENDTURN` from the driver (0 harness fallbacks); 142 with ready units left at the end; 12 needed a second round (a unit woke, `sendTurnUnready`, 26.2); every AI turn waited out with code 0 |
| time per turn | human part median 3.4 s (max 45.4 s, a 10-turn round with its screens), AI part median 4.2 s (max 7.5 s); 1,733 s in all |
| actions sent (done / turns with one) | unit moves 156 / 96 turns, found 5 / 5, research 18, production 29 / 27, gold purchase 17 / 17, melee attacks 30 / 28, ranged attacks 9 / 7, promotions 13, policies 6, pantheon 1, worker automation 1 (29 more refused in Lua: `CanAutomate` false), war on city-state player 24 at turn 44, **trade route 1** (turn 147) |
| deals | **one AI offer accepted** (turn 132, player 3, 2 items, `UI.DoProposeDeal` on the offer: `sendNetDealAccepted` 1, `ICvDealAI1::DoAcceptedDeal` 1); **no AI offer refused**: the AIs made no other offer in 150 turns (the harness's diplomacy handler saw one screen, turn 6); one met major until late, scouts explored by hand |
| city capture | **none**: at war with one city-state from turn 44; 39 attacks on its units and city, its city never taken (the scripted army stayed at 2-7 units); the human lost a city twice (4 -> 3 at turns 108 and 135) |
| screens | 15 rounds (turns 0, 10, ..., 140) + the last save: map, notification list, city screen, tech tree, leader screen, save all pass except the turn-0 city screen (no city yet) |
| LOUD | vpproxy.log 1 (`sendNetDealAccepted` received a DLL object not on the borrowed-argument list: placeholder bug `tracker/issues/NEW-sendnetdealaccepted-not-borrowed.md`, numbered 0344; *fixed 2026-11-15 by ticket 0344: the send borrows its argument and is on the list, 17.4*), server.log 0 |

### 26.4 Tiny game 2 (`tiny150b`): the improved driver, to the human's defeat at turn 149 [L, 2026-11-15 01:51-02:34 EST]

The same set-up and new-game string, a second map (seeds the EXE's; fixed point `c5h-c5h-h-t0000-015405`, kept in `F:/c5-64bit-work/0322/saves`). Driver
changes after game 1: scouts explore on automation (`COMMAND_AUTOMATE` with `AUTOMATE_EXPLORE`, `sendDoCommand`), the
caravan's tech first (`TECH_HORSEBACK_RIDING` is `UNIT_CARAVAN.PrereqTech` in VP 5.4.8, read live), war from turn 30,
two AI offers to accept. Turns 1-2 were ended by the harness's `turns.end_turn` fallback (a driver fault: turn 0 ended
by itself after its last blocker was resolved and the fallback then ended turn 1; fixed in `humanplay.py`, "auto");
human turns 0 and 3-149 played (148).

| | result |
|---|---|
| end | **the human was defeated in the AI part of turn 149** (one city; at war with city-state 24 from turn 30 and with major player 2 later): the end-game screen "You have been defeated" came up, the turn wait timed out after 600 s (wait code 3), the driver read game state 1 / not alive and stopped safely (rc 0). No crash, no restart, no D125 report. The last save through the proxy is turn 140 (`c5h-c5h-h-t0140-022037`); a save after the defeat failed (the checklist's status step: no living human) |
| turns | 147 forced ends + 1 fallback; 144 with ready units left; 10 second rounds (`sendTurnUnready`); human part median 3.4 s, AI part median 4.0 s |
| actions | moves 143, attacks 19 + ranged 6, purchases 9, production 17, research 15, promotions 13, policies 4, pantheon 1, explore/build automation 43 (92 more refused in Lua, `CanAutomate` false), war 1; no trade route (no caravan before the defeat), no AI offer (none made; one AI screen at turn 3, answered by the harness) |
| LOUD | 0 in vpproxy.log and server.log |

### 26.5 The huge save (`huge80b`): 80 human turns from turn 350 [L, 2026-11-15 02:49-03:51 EST]

`C5H-VP8P-HUGE_0350` (the instance's own copy, SHA-256 `c499d3c6...88c3`, the D78 save; Egypt, 13 cities, at war with
nine players), turns 350-429 played by the driver (`--max-turns 80 --every 10 --turn-timeout 900 --accept-offers 1
--max-unit-orders 12`), then the last save `c5h-c5h-h-t0430-035002.Civ5Save` and the safe stop (rc 0). A first attempt
(`huge80`, 02:36-02:47, 9 turns) ended every turn through the harness's fallback (`ENDTURN_BLOCKING_CITY_RANGE_ATTACK`
had no answer in the driver); it was stopped by the stop file, the driver got the city bombard and the dismissal of
other blockers, and the run started again from the same save. Its numbers are not used.

| | result |
|---|---|
| crashes, restarts, D125 reports | none (one EXE process, one server process for the 80 turns; no report folder) |
| turns | 80 of 80 ended by `CONTROL_FORCEENDTURN` (0 fallbacks), all with units left ready (51-73 ready, all but one given "skip turn" first); 4 second rounds (`sendTurnUnready`); every AI turn code 0; human part median 10.9 s (max 55.1 s), AI part median 27.6 s (max 38.6 s); 3,502 s in all |
| blockers met and answered | production, promotion, city range attack (169 bombard / dismiss steps: `TASK_RANGED_ATTACK` through `Game.SelectedCitiesGameNetMessage`), event choice and league call for votes (notification dismissed) |
| actions | moves 539 (+ the skips), production 208 / 74 turns, gold purchases 64 / 64 turns, ranged attacks 79, melee attacks 20, promotions 22, research 8, policies 4, automation 15 (284 refused in Lua: `CanAutomate` false for units the snapshot calls workers), **trade route 1** (turn 423) |
| deals | **accepted** the AI offer of turn 350 (player 1, 11 items: `sendNetDealAccepted` 1, `ICvDeal1::Write` 1, `ICvDeal1::Read` 1, `ICvDealAI1::DoAcceptedDeal` 1); **refused 28 AI offers** (`DIPLO_UI_STATE_TRADE_AI_MAKES_OFFER`, the harness's handler: DiploTrade `OnBack()`, Lua.log `c5h-diplo: done ... actions=trade`: Ashurbanipal 15, Pacal 5, Elizabeth 3, Suleiman 2, Montezuma 2, Isabella 1); 164 discussion screens closed (Bismarck's and Montezuma's "stop conversions", Isabella's vassalage revocation, ...) |
| city capture | **none**: Egypt lost cities (13 -> 11 by turn 361) and gained none; the driver attacks units and cities in reach but marches on enemy cities only with the units it has left after the skips, and Egypt's front never reached an enemy city with a weakened garrison |
| screens | 8 rounds (turns 350, 360, ..., 420) + the last save: every step pass (map, notification list, city screen, tech tree, leader screen, save) |
| LOUD | vpproxy.log 1 (the same `sendNetDealAccepted` line as 26.3, at the accepted offer), server.log 0 |

### 26.6 The human-only sends and what the EXE did with them [L]

Counts from `proxy-calls.tsv` (counters=1) of the three runs, by `tools/humanplay/sends.py` (Tiny 1 / Tiny 2 / huge):

| send (server-host.md: stub in the headless server) | calls | the EXE's answer through the proxy |
|---|---|---|
| `sendTurnComplete` (not allow-listed) | 162 / 160 / 84 | no Response*: the EXE's turn machinery ended the turn every time (the AI turn followed, `HasReceivedTurnComplete`); more calls than turns: the second rounds of 26.2 |
| `sendTurnUnready` (not allow-listed) | 12 / 10 / 4 | the turn stayed open (DLL `setEndTurn(false)`); the driver gave the woken unit "skip turn" and forced again, which ended it |
| `sendAutoMoves` (not allow-listed) | 2 / 0 / 2 | no Response*; the turns ended normally |
| `sendNetDealAccepted` ("the riskiest stub") | 1 / 0 / 1 | **not** `ResponsePlayerDealFinalized` (0 calls): the EXE serialized the DLL's deal (`ICvDeal1::Write`), read it into a deal of its own (`CreateDeal`, `ICvDeal1::Read`) and called `ICvDealAI1::DoAcceptedDeal(PlayerTypes, ICvDeal1 *, int, int, int)` on the AI once; one LOUD line each time (the deal argument is not on the borrowed-argument list: `NEW-sendnetdealaccepted-not-borrowed`, numbered 0344; *since ticket 0344, 2026-11-15, on the list: the EXE's handler copies the deal into one of its own inside the call, 17.4*) |
| `sendNetDemandAccepted` | 0 / 0 / 0 | not hit (the human made no demand) |
| `sendPurchase` (stub in the server) | 17 / 9 / 64 | `ResponseCityPurchase` once per send; gold spent, unit or building in the city |
| `sendPushMission` | 336 / 382 / 8,152 | `ResponsePushMission` once per send (moves, attacks, founding, trade route, skips) |
| `sendDoCommand` | 14 / 56 / 93 | `ResponseDoCommand` once per send (automation, promotions) |
| `sendPushOrder` | 29 / 18 / 223 | `ResponseCityPushOrder` once per send |
| `sendDoTask` | 0 / 0 / 221 | `ResponseCityDoTask` once per send (city bombard `TASK_RANGED_ATTACK`) |
| `sendFromUIDiploEvent` | 0 / 0 / 1 | `ResponseDiplomacyFromUI` 1 |
| `sendSwapUnits`, `sendAutoMission`, `sendPopOrder`, `sendSwapOrder`, `sendMoveGreatWorks`, `sendMinorGiftGold` | 0 | not hit by this driver (automation goes through `sendDoCommand`; no queue edits, no great works, no gifts) |

Also hit and answered one to one: `sendMinorPledgeProtection` / `ResponsePledgeMinorProtection` (6 / 0 / 38, the AI's),
`sendMinorBullyGold` (1 / 0 / 1), `sendPlayerOption` 12 per launch; the UI's own `Network.Send*` calls arrived as
`ResponseResearch`, `ResponseUpdatePolicies`, `ResponseChangeWar`, `ResponseFoundPantheon`, `ResponseUpdateCityCitizens`.
So every human-only send this driver reaches is answered by the EXE's net layer through the proxy, as 9007's loopback
does in the headless server, except that the deal send is answered with `DoAcceptedDeal`, not with
`ResponsePlayerDealFinalized` as server-host.md's table guessed [L]. The server reported 0 methods without remote
treatment in all three runs.

### 26.7 Save identity of the last saves (D10, D24 part 1) [L, 2026-11-15]

`tools/humanplay/resave.py` (run_d10.py's part 1 for one file: c5h with the 32-bit pin DLL `i0ship-26234371`
`CvGameCore_Expansion2.dll`, the x64 server of 26.1, both loading the save and saving before any turn; `savecmp.compare`
without the order exception, both builds carry D18):

| save (written through the proxy) | x86 resave | x64 resave | verdict |
|---|---|---|---|
| Tiny 1 turn 150 `c5h-c5h-h-t0150-014847` (SHA-256 `fca1a3f7...`) | 423,231 B | 423,232 B | **IDENTICAL UNDER EXCEPTIONS**: only (a) `dll-version-string` (`Release-5.4.8-39-g26234371e Clean` / `Release-5.4.8-51-g04b1211b8 Copy`) |
| huge turn 430 `c5h-c5h-h-t0430-035002` (SHA-256 `2f95400d...`) | 2,448,537 B | 2,448,571 B | **IDENTICAL UNDER EXCEPTIONS**: only (a), the same strings |

### 26.8 The last saves on the stock route (the D28 off switch) [L, 2026-11-15 03:55-04:17 EST]

`humanplay.py --stock` (vpproxy.ini `mode=off`: the instance's 32-bit `i0ship-26234371-ia`, no server) with
`--max-turns 1 --every 0`:

- **Tiny 1 turn 150 loads on the stock route** (three loads, every one in game). With the full driver the turn did not
  end, twice (`stock-tiny150`, `stock-tiny150b`): the settler founded Gdansk, Gdansk got `UNIT_SPEARMAN` (probe: queue 1),
  and `ENDTURN_BLOCKING_PRODUCTION` stayed with the notification "Gdansk is ready for a new construction project" through
  its dismissal and the harness's 40 tries; both runs stopped safely (rc 5). **The same save with the same orders through
  the proxy ended the turn** (`proxy-tiny150-t150`, 04:05: the block cleared to `ENDTURN_BLOCKING_UNITS`, forced end,
  turn 151, rc 0). Placeholder bug `tracker/issues/NEW-stock-route-production-notification-stuck.md` [L, cause not known].
  *Cause found 2026-11-15 by ticket 0345 (section 29): VP-EUI's CityView.lua opens a city's screen when it is founded,
  the EXE does not re-read the end-turn blocking while a city screen is up, and the driver's bombard closed that screen
  only when it came after it (a race); not the stock route, not the DLL. The driver now leaves the screen.*
- Without unit orders (`--max-unit-orders 0`, `stock-tiny150-noorders`, 04:09) the turn ended on the stock route (city
  bombard, forced end with units unready), and then the 32-bit DLL showed its **"Assertion Failed"** box: `Expression:
  GetEndTurnBlockingType() == NO_ENDTURN_BLOCKING_TYPE`, `CvPlayer.cpp` line 34134 of the -ia build, "Expecting the
  end-turn blocking to be NO_ENDTURN_BLOCKING_TYPE, got 3" (3 = `ENDTURN_BLOCKING_UNITS`): the forced end of a turn with
  units unready trips this DLL assert. **On the proxy route the same assert fires too** and is only written to the
  server's `CvAssert.log` (once per session: `CvPlayer.cpp:34132` at 01:19:37 in `tiny150`, 02:52:12 in `huge80b`, 04:07:01
  in `proxy-tiny150-t150`) [L]. So a player who presses Shift+Enter with units unready gets this box on the 32-bit
  route once per session (VP's release asserts show a box) and nothing through the proxy. The box was left open for the
  MC (D29; queue question of 2026-11-15 04:17). The MC's word (#1483, #1486): one OK with D60's
  `--coordinator-override` (the box holds the game's Lua lock, so the dismissal's settled-save check could never pass);
  closed at 04:24, the game went on to **turn 151 on the stock route**, safe stop rc 0. The box is the DLL's own assert,
  not a fault of the patch: the instance-aware 32-bit build shows VP's release assert box, the x64 server logs it.
- **Huge turn 430 on the stock route** (`stock-huge430`, 04:26-04:32, `--keep-unready 0`: every ready unit skipped, so
  no forced end with units ready and no assert box): loaded, the human turn played (production 4, moves 7, ranged 2,
  city bombard, league vote notification dismissed), forced end, AI turn, **turn 431**, rc 0.
- c5h-h restored afterwards: vpproxy.ini to its set-proxy hash, `set-proxy --original` (run folders to
  `F:/c5-64bit-work/0322/proxy-runs`), the standard files equal to the SHA-256 backup, `provision.py verify` ok.

### 26.9 Not verified, open

- No city capture in any of the three games (26.3-26.5): the `CHOOSE_CITY_FATE` / puppet path of the driver ran
  offline only, and `sendDoTask` for a captured city's fate is not covered.
- No AI offer was refused in a Tiny game (none came after the accepted one); the huge game refused 28.
- The pictures are DIAGNOSTIC (a pack=0 test layout driven by the harness on a hidden desktop with the frame cap);
  no ACCEPTANCE picture was taken: the first playable build's package (0324) did not exist.
- Not run: a stock-route human game for a like-for-like memory comparison (D78's stock numbers are under AI play).
- ~~Why the stock route keeps the production block after founding (26.8; placeholder bug).~~ Answered by ticket 0345,
  section 29 (2026-11-15).

## 27. A read past the end of an engine save stream takes D118's path, not the CRT abort (queue ticket 0359, 2026-11-15, account B)

**Cause** [C, 0334's 4254 dump at a698bca4, `evidence/0334/4254/live/` on branch task-4254-43civ-parity]: on the
43-civilisation build's cross-variant load the DLL's `readSaveGameDB` read past the end of the save body; the server's
pull stream (`srv_rt.cpp` `PullStream::Ensure`) asked for the bytes with `kRtE_StreamRead`; the proxy served that
method in its own branch of `OnCall`, **before** 9036's try/catch (18.5), so the C++ throw of the EXE's stream
(`msvcr90!CxxThrowException` under `OnCall` on the dump's thread 0) escaped to `terminate()` and `abort()`: the CRT
"Runtime Error!" box, the server ended with `the engine is gone ... PeerDied` waiting in `kRtE_StreamRead`, no report.

**Fix** (`proxy/src/proxy_rt.cpp`, commit 8de1f805): the branch is `ServeStreamRead(args, ret, ch)`, called inside the
same try as `LuaServe` and `EngDispatchGenerated`. A throw now ends the served call with `kStatusEngineException`
(LOUD line `served call rtE:StreamRead (0xFFF10010) threw ...; g_lock held by this thread after the catch: no`), the
server ends with `result: FAILED: exe_exception: the engine threw a C++ exception in kRtE_StreamRead` (exit 6), the
proxy's waiting call sees the server gone and `CallFailed` takes D118's route (pack=1: the report and the helper, 22.1
and 22.8), then `on_server_death`. Bad arguments are refused as before (`kStatusBadArguments`, 64 MB cap).

*(Corrected 2026-11-16, ticket 0451 [C, L]: the catch kept `what = e.what()`, a pointer into the exception object,
and formatted it after the handler had destroyed the object (a use after free since 9036). The LOUD line and the
`exe_exception` text then printed freed heap: on 0313's server and proxy built on master's DLL, hutest
`variant-nodb-on43` and `variant-short` failed 2 of 2 with `threw ���b�read past the end of the save body` (the first
8 bytes, `hutest: `, overwritten), while main's build passed by its heap layout. Now the text is copied into a
256-byte buffer inside the handler (`proxy_rt.cpp`, `whatCopy`); the two constant texts are unchanged. Test
`proxy/tests/test_served_exception_text.py` (the three throwing scenarios, the texts byte for byte in vpproxy.log and
server-report.txt, printable ASCII) with a negative control on 0313's master build; results in the 0451 log and
`evidence/0451/`. No other catch in `proxy/` keeps `e.what()`. Replaced: "LOUD line ... threw ..." taken as reliable
text.)*

### 27.1 Offline tests [L, 2026-11-15 09:13-09:18 EST]

Build `C:/c5-64bit-work/0359/b`: `server/build_server.py --src` a plain copy of the pin 04b1211b8 `--deps` 0351's
release deps (copied), release, `--offline`, OK in 97 s; proxy `3a638bff...`, server.exe `cec50c8b...`, x64 DLL
`29f58e17...` (not the pinned bytes 74588135: another link of the same pin, see 0358). All through `quietlock.py run`.
- hutest scenario **`streamread`** (new): the fake engine hands `ICvGame1::Read` a 32-byte stream that reports no size
  (a pull session, as the EXE's save-load streams), the fake DLL reads 16 bytes (they arrive byte for byte) then 64
  more; the fake stream throws `std::runtime_error` on the read past its end. Expected and seen: host exit 3, proxy
  LOUD `... threw hutest: read past the end of the engine stream ...; g_lock ... no`, `FATAL ... on_server_death=exit`,
  server `exe_exception ... kRtE_StreamRead`, dll.log `stream_first=16 same=1` and no `stream_past_end_returned`.
  **`streamread-unknown`**: the same with a throw of no std type (`... threw a C++ exception of unknown type`).
  `build_hutest.py` checks a tuple of log lines per scenario and the fake DLL's log (`SCENARIO_DLL_LOG`).
- **Negative control**: the same two scenarios with main's proxy (1c62dede, git archive, `build_proxy.py --gen` of the
  same build; `16eefb29...`) fail as they must: host exit 3 through `abort()` (the host switches the CRT box off), no
  LOUD or FATAL line, server `the engine is gone: channel N serve() returned PeerDied`: 4254's picture.
- hutest normal run 47 checks 0 failed; `--scenario all` 10/10 as expected; `test_proxy_rt.py` 106/106.
- Not run under MSVC `/fsanitize=address`: no ASan harness exists for `proxy_rt.cpp` (unit.cpp links only
  `proxy_rt_logic.cpp`); the change moves code into a function and under an existing catch.
- Evidence `evidence/0359/offline/`.

### 27.2 Live: a truncated save on the pack=1 layout [L, 2026-11-15 09:24:53-09:27:24 EST, workstation, c5h-c]

Driver `evidence/0359/live_0359.py` (prep / run / restore). Instance c5h-c (Goldberg, C:), its own config.ini (stock:
config mask 0xFFEFFFF7, every rule key on, so a **stock session**, 22.1), VP64Options.ini unchanged (ServerRenderer 0).
- Layout: a test pack by `tools/release/pack.py --layout C:/c5-64bit-work/0359/b` with the instance's own 32-bit DLL
  (`e589306e...`, the bbfacca1a instance-aware build, the fallback; not used in the run) as its test-only x86 entry;
  `provision.py set-proxy` with the pack's x64 files and the proxy `3a638bff...`; manifest.json and purity.tsv (empty)
  in VP64; vpproxy.ini `mode=on local_answers=1 pack=1`. Boot: `boot: pack ... ok: release 0359-test` and the purity
  line; no FALLBACK.
- The helper beside the proxy was a **fake** `vp64helper.exe` (`proxy/tools/rt_tests/fake_helper.cpp`: records its
  command line, shows nothing): the real helper would put a topmost window on the owner's desktop, which COMMON rule 14
  forbids on the workstation (the game is cloaked by the in-process guard; a child process is not). The real helper's
  text was printed afterwards with `popup --report <the run's folder> --no-ui` (its own workstation form).
- Save: our copy `C5H-0359-TRUNC50` of `C5H-3600-TINY_0008` (c5h-c's, read only; `b2ceb663...`): `civ5save.exe
  unpack`, the inflated body (12,708,174 bytes) cut to its first half (6,354,087), `civ5save.exe pack`: a well-formed
  container of 112,487 bytes (`a5d1410a...`) whose body ends early, so the DLL reads past the end of the body stream.
- Result, every check as expected (`facts.json`):
  - the load reached the body: stream sessions 1-2 of known size (260 bytes; 111,594 at 893), then **83 pull
    sessions** on the same engine stream (one per engine -> DLL `Read` of the body: ICvGame1 1, ICvMap1 1, ICvTeam1 64,
    ICvPlayer1 16, ...); the 16th `ICvPlayer1::Read` pulled past the end;
  - **the EXE's stream threw a C++ exception of no std type** on that read (the proxy's `catch (...)` branch: `LOUD:
    served call rtE:StreamRead (0xFFF10010) threw a C++ exception of unknown type ...; g_lock held by this thread
    after the catch: no`, 35.37 s after the boot);
  - server: `result: FAILED: exe_exception: the engine threw a C++ exception in kRtE_StreamRead (...)`, exit code 6;
  - proxy: `call ICvPlayer1::Read(FDataStream &) ... failed: PeerDied`, `report: the 64-bit path failed: stage
    server-death, code PeerDied; stock session ...`, the report folder `<instance Documents>\My Games\Sid Meier's
    Civilization 5\VP64\reports\20261115-092653` written in 140 ms (report.txt with `[failure]` naming the failed
    call, the server's exit code 6 and server.log's `exe_exception` line (22.9); 8 attachments; no .dmp), the helper
    started as `vp64helper.exe popup --report "<that folder>" --options "<vpproxy.ini>"` (the fake's record), then
    `FATAL: the server failed a call (on_server_death=exit): the process ends with exit code 3`;
  - **the abort path was absent**: no CRT box. The guard log of the EXE (`guard-48032.log`) lists every window the
    process created: the game window and two ActiveMovie windows at the start, none after the load (a CRT abort box is
    a `MessageBoxA` window the guard cloaks and logs at creation); the watch loop: 0 hits; the harness saw the EXE gone
    90.9 s after the launch (`wait_game` "gone"), safe stop rc 0.
  - the popup text (`--no-ui`): title "Vox Populi 64-bit patch: error report"; "The 64-bit part of the patch could not
    run. This game goes on as the normal (32-bit) game."; "The 64-bit part of the patch failed (server-death:
    PeerDied); the game had to end."; the report's path; the hand-in line; buttons Open folder | Close.
- Restore: the added files and the report folder removed (the report kept only in the scratch folder, never committed:
  D119), `set-proxy --original` (run folders to `C:/c5-64bit-work/0359/live/vp64-runs`), `frontend.py restore`, the
  standard files restored by SHA-256 (`backup.py restore`: OK), `provision.py verify c5h-c --quick` rc 0, claim released.
- Evidence `evidence/0359/live/` (facts.json, vpproxy.log, server.log, server-report.txt, the report's `[failure]`
  section and file list, helper-calls.txt, the `--no-ui` popup, the guard log, arm and watch logs, prep/run/restore logs).

### 27.3 Findings beside the fix

- **The popup contradicts itself after a death in a game in a stock session** [L, C]: `vp64helper popup` always begins
  with "This game goes on as the normal (32-bit) game." (`vp64helper.cpp` line 711, written for the boot fallback),
  but the proxy also starts `popup` for a stock session's `server-death` in a game (22.1), where the summary line says
  "the game had to end" and the EXE is ended. Placeholder bug `tracker/issues/NEW-popup-stock-server-death-text.md`.
  **Fixed 2026-11-15 by ticket 0361 (section 28): the first line now follows the report's stage; after a server death
  it reads "The 64-bit part of the patch stopped during the game, so the game had to end."**
- **Every engine -> DLL `Read` of a body stream opens a new pull session** [L: 83 in one Tiny load; C: `StreamInArg`
  pushes the stream into `g_pullStreams` each time and nothing removes an entry]: 4 bytes per session for the life of
  the EXE, about 83 per load of a Tiny save (more for larger games). Not a fault; noted for whoever touches 2500's pull
  sessions.
- 15.2 item 3 says the EXE's size-less save stream "never came up short" on 1 MB reads; here the body stream threw on
  a read past the end of the inflated body. The two are not reconciled [I: 2500's chunked read may have been of another
  stream or in a build that read differently].

### 27.4 Not verified

- A **D33 session** (the shipped config.ini after `vp64helper on`): the proxy takes 22.8's branch (`vp64helper failed
  --pid ...`, then Fatal(3)) instead of `popup`; that branch is 3523's and was not re-run with this failure.
- The real popup window and its buttons (only the `--no-ui` text; a window may not be shown on the workstation).
- A file cut at the byte level (a killed autosave write: the container itself truncated, the EXE's inflater runs out of
  input): not run. Only a well-formed container with a short body was. Whether the EXE throws (caught now) or faults
  (an SEH fault no C++ catch sees) on input that ends inside a deflate block is open.
- The 43-build cross-variant load of 4254 itself with the fixed proxy (ticket 0354, backlog on 2026-11-15, is to refuse
  that load before the body reaches the DLL; the two are separate lines of defence).
- Exceptions from the engine stream outside a served call (`StreamInArg`'s known-size `ReadIt`, `StreamOutRet`'s
  `WriteIt`) run on the EXE's own call into the proxy and propagate back into the EXE's frames as they would from the
  in-process DLL; a throw there would leave the channel's begun call unfinished [I, not tested].

## 28. The stock-session popup after a server death in a game says the game had to end (queue ticket 0361, 2026-11-15, account B)

Fixes 27.3's first finding. Until this change `vp64helper popup` (the verb the proxy starts for every failure in a stock
session, 22.1) always began with "The 64-bit part of the patch could not run. This game goes on as the normal (32-bit)
game.", a line written for the boot fallback. For a `server-death` in a game the proxy then ends the EXE
(`on_server_death=exit`, the default), and the next line (popup-summary.txt) says "the game had to end" [L, 0359].

### 28.1 What the helper does now [C, `proxy/helper/vp64helper.cpp` `ReportStage`, the `popup` verb]

- The `popup` verb reads the failure's stage from the report folder: the `stage: ` line of `report.txt`'s `[failure]`
  section (written by `proxy_report.cpp`, 22.2); if `report.txt` is missing or has no such line, the `(<stage>: ` part of
  `popup-summary.txt`'s first line when that line begins "The 64-bit part of the patch failed (". `report.txt` wins over
  the summary. A stage line outside `[failure]` and a longer stage name (`server-deathly`) do not count.
- Stage `server-death`: first line "The 64-bit part of the patch stopped during the game, so the game had to end."
- Every other stage (`pack`, `server-start`, `handshake` at the boot), no stage, or no report folder: the boot fallback's
  line unchanged, "The 64-bit part of the patch could not run. This game goes on as the normal (32-bit) game."
- The rest of the popup is unchanged: the summary line, the dump line, `Report: <path>`, the hand-in line, the public
  warning, buttons Open folder | Close. The `failed` verb (D33 sessions, 22.8) is unchanged: its three first lines
  (ended and restored; did not close within the deadline; config.ini not restorable) already say what happened and the
  suite checks each of them.
- The D118 wording of the two stock-session popups, as of this change:
  - at the boot: "could not run. This game goes on as the normal (32-bit) game." + "... failed (<stage>: <code>); the
    game goes on as the stock game.";
  - after a server death in a game: "stopped during the game, so the game had to end." + "... failed (server-death:
    <code>); the game had to end."

### 28.2 Tests [L, 2026-11-15 09:37-09:41 EST, workstation, branch `task-0361-popup-death-text`]

- `python -I proxy/tools/test_vp64helper.py --scratch C:/c5-64bit-work/0361/t1`: 98/98 (case `stage`: 11 report
  folders, server-death from report.txt + summary, report.txt only and summary only; pack, server-start, handshake;
  report.txt over the summary; a stage line outside `[failure]`; `server-deathly`; a summary of another form; neither
  file; and that the summary line and the report's path follow). Evidence `evidence/0361/suite-clang.log`.
- Negative control: the same suite against the old helper source (`61f0ca90`): 92/98, exactly the 6 server-death checks
  fail with the old first line (`evidence/0361/suite-old-source-negative.log`).
- `--asan` (MSVC `/fsanitize=address`): 194/194, no AddressSanitizer report (`evidence/0361/suite-asan.log`).
- Replay of 0359's live report: a folder made from `evidence/0359/live/report-failure-section.txt` and
  `popup-summary.txt` (the proxy's bytes of 2026-11-15 09:26), the built helper (SHA-256 627e7456..., `evidence/0361/
  helper-sha256.txt`) with `--no-ui` on a hidden desktop: first line "stopped during the game, so the game had to end",
  no "goes on" line, exit 0 (`evidence/0361/replay.py`, `replay-0359-report-popup-noui.txt`).

### 28.3 Not verified, open

- No live game run: the helper's input is the report folder alone, and 0359's live folder was replayed; the proxy is
  unchanged.
- The real popup window (only `--no-ui`; COMMON 14).
- `on_server_death=continue` (not the default; 22.1): the proxy's summary line already says "the game had to end" for
  every `server-death` and the EXE then goes on with every call returning its default; the helper's new first line says
  the same. Neither text fits that setting. The proxy's text is `proxy_rt.cpp` `ReportFailure` (not in this ticket's
  files); if `continue` is ever offered to players, both lines need a third wording [I].

## 29. The stock route's stuck production block after a founding is VP-EUI's city screen (ticket 0345, 2026-11-15, account B)

Ticket 0345 (placeholder of 26.8): on the stock route, after the driver of `tools/humanplay` founded Gdansk and gave it
`UNIT_SPEARMAN` through `Game.CityPushOrder`, `GetEndTurnBlockingType()` stayed `ENDTURN_BLOCKING_PRODUCTION` with
notification 381 and no forced end could pass it; the same orders through the proxy ended the turn.

### 29.1 Cause [L, C]

1. **VP-EUI opens the new city's screen by itself.** `(3a) VP - EUI Compatibility Files/LUA/CityView.lua` (the
   instances' copy, lines 2831-2847) has an `Events.NotificationAdded` handler: for `NOTIFICATION_PRODUCTION` of the
   active player whose city was founded this turn (`city:GetGameTurnFounded() == Game.GetGameTurn()`) and with no city
   screen up, it calls `UI.DoSelectCityAtPlot(city:Plot())`, which opens that city's screen [C]. The DLL adds that
   notification inside the founding (`CvPlayer::found` -> `CvCity::chooseProduction`, `CvCity.cpp` 7800-7833 at
   26234371e) and broadcasts it at once (`CvNotifications::Add`), so the screen opens on every human founding [L: head
   city 2174 Gdansk, `UI.IsCityScreenUp()` true in the stuck state below]. This path does not set the screen's
   auto-close (`g_isButtonPopupChooseProduction` is set only in the `BUTTONPOPUP_CHOOSEPRODUCTION` path, line 2825), so the
   screen stays open after a production choice too, until the player leaves it (Return button, Esc or Enter: line 407,
   `ExitCityScreen` -> `Events.SerialEventExitCityScreen`).
2. **While a city screen is up the end-turn blocking type is not recomputed.** The blocking type the Lua and the forced
   end read is a cached member (`CvPlayer::m_eEndTurnBlockingType`), written only by `CvGame::updateTestEndTurn`
   (`CvGame.cpp` 2201-2317 at 26234371e) through `SetEndTurnBlocking`; the EXE calls `ICvGame1::UpdateTestEndTurn` once
   per frame in a human turn (`research/sources/civ5-headless/engine-contract.md` 6.6). Live, with a city screen up: the
   notification itself was expired (`CvNotifications::Update` ran: notification 381 gone from the undismissed list, the
   turn slice advancing about 8 per second, no pause, `IsWaitingForBlockingInput` path not taken), yet the cached type
   stayed PRODUCTION / 381; Esc closed the screen and within 2 s the type became `ENDTURN_BLOCKING_UNITS` [L]. A second
   probe: Warsaw's screen opened with `UI.DoSelectCityAtPlot`, every ready unit's moves set to 0 (Lua test call): the
   type stayed UNITS for 9 s with 0 ready units; after Esc it read NO_ENDTURN_BLOCKING_TYPE and the turn ended by
   itself [L]. So the EXE does not call `UpdateTestEndTurn` while a city screen is up [I from L: no call count was
   taken on the stock route]. `CONTROL_FORCEENDTURN` passes only with the cached type NO or UNITS (`CvGame.cpp`
   3784-3818), so the forced ends and the harness's `turns.end_turn` all failed.
3. **Why the proxy run passed and some stock runs pass: a race in the driver.** The driver's city bombard chunk
   (`lua_chunks.CITY_BOMBARD`) selects a city and ends with `UI.ClearSelectedCities()`, which also leaves an open city
   screen. When the bombard ran after the founding's screen was up, the screen was closed by it and the turn went on;
   when the bombard ran within about a second of the founding, before the screen came up, the screen opened after it
   and stayed. Live on the -ia build (below): found -> 4 s -> bombard -> produce cleared the block; the driver's own
   timing (found -> about 1 s -> bombard) stuck it. The 0322 proxy run (`proxy-tiny150-t150`) had found at 04:06:55 and
   bombard at 04:06:56; the timing through the proxy differs and the block cleared there [I: not re-run through the
   proxy here]. No DLL fault: the DLL expired the notification in every run.

So this is the game's own behaviour with VP-EUI (a player sees the new city's screen and leaves it before ending the
turn [I: the end-turn button was not looked at while the screen was up]), met by a driver that gave the order outside the screen and never
left it. **Fix (tools/humanplay):** `lua_chunks.EXIT_CITY_SCREEN` (if `UI.IsCityScreenUp()`, the screen's own exit,
`Events.SerialEventExitCityScreen()`) and `humanplay.exit_city_screen`, run before every end-of-turn read in
`resolve_and_end`; the closes are recorded per turn in `run.json` (`city_screens_closed`). Also: `--stock` runs an
instance without `vpproxy.ini` as it is, and `--issue` names the Visual Checks issue (was fixed to 0322).

### 29.2 Live runs on the -ia build [L, 2026-11-15 14:23-14:40 EST, workstation, c5h-h]

Instance c5h-h (F:, Goldberg, claimed for 0345), its own 32-bit `i0ship-26234371-ia` (`3c2f47da...`, no proxy layout,
no vpproxy.ini), save `c5h-c5h-h-t0150-014847` (0322's Tiny game 1, turn 150), Lua channel probes
(`C:/c5-64bit-work/0345/probe.lua`: turn slice, pause, blocking type and index, the undismissed notifications, every
city's queue and the city at its plot; `uistate.lua`: interface mode, popup, head city / unit, `UI.IsCityScreenUp()`).

| run | steps | result |
|---|---|---|
| 1 (14:26) | found 2169 -> 3 probes over 4 s -> produce `UNIT_SPEARMAN` -> bombard | notification 381 added at the founding, expired within 2 s of the order; block CITY_RANGE_ATTACK, then UNITS after the bombard |
| 2 (14:30) | 0322's order, a probe 2-3 s after each step: snapshot, close popups, move, ranged x2, found, bombard, produce | block PRODUCTION after the bombard, UNITS 3 s after the order |
| 3 (14:34) | 0322's driver steps and timing (`C:/c5-64bit-work/0345/turn150.py`: humanplay's `snapshot`/`do_actions`/resolve loop, no forced end) | **reproduced**: after the order the block stayed PRODUCTION / 381 for 17 s (6 probes) and through `DISMISS_BLOCK`, with 381 already expired and the slice advancing; `UI.IsCityScreenUp()` true, head city Gdansk. Esc (harness key) -> UNITS within 2 s |
| 3, cont. (14:39) | Warsaw's screen opened by `UI.DoSelectCityAtPlot`, ready units' moves set to 0 | UNITS kept 9 s with 0 ready units; Esc -> NO, the turn ended by itself, turn 151 |

Picture: Visual Checks, issue 0345, DIAGNOSTIC (the stuck state: Gdansk's city view, the map camera on its tiles; the
PrintWindow shot carries no interface panels).

**The fixed driver** [L, 2026-11-15 14:43-14:51 EST, c5h-h, -ia build, run `fix-ia-t150`:
`humanplay.py c5h-h fix-ia-t150 --load c5h-c5h-h-t0150-014847 --max-turns 1 --every 0 --stock --issue 0345`, the
instance without vpproxy.ini]: the same actions and timing as 0322's stuck runs (found 14:46:04, bombard 14:46:05,
produce 14:46:08); `exit_city_screen` found Gdansk's screen up 1 s after the order and closed it; the next read was
`ENDTURN_BLOCKING_UNITS`, the forced end was sent (14:46:11) and the turn ended. **Then the DLL's assert box of 26.8**
("Expecting the end-turn blocking to be NO_ENDTURN_BLOCKING_TYPE, got 3", `CvPlayer.cpp` 34134 of the -ia build) came
up: this run kept one unit unready (`--keep-unready` default 1; on the stock route `--keep-unready 0` avoids it, 26.8),
so the run ended rc 5 with the box left open for the MC (D29; queue question of 2026-11-15 14:53). MC #1866 allowed one
OK with `--coordinator-override` (14:58); the AI turn finished (turn 151) and the game was stopped safely.

### 29.3 The same on a founding at turn 0, and through the interface only [L, 2026-11-15 14:52-15:20 EST, c5h-g]

Instance c5h-g (F:, claimed for 0345), save `C5H-0345-T0000` (a copy, own name, of 0322's turn-0 save
`c5h-c5h-h-t0000-011922`, SHA-256 `b176198c...`: Poland, settler 1001 not yet founded). Steps with keys only, as a player
(harness posted messages): the game selects the settler by itself at turn 0 (`CvGame::updateSelectionList` ->
`SelectSettler`); **B** founds the city (needs the WM_CHAR too: `cli.py key B --char b`; the key alone did nothing [L]);
Esc. Pictures in Visual Checks, issue 0345, DIAGNOSTIC.

- **-ia build** (`3c2f47da...`, with Lua probes): after B, Warsaw (city 1021) founded and its city view up by itself
  (`UI.IsCityScreenUp()` true, head city 1021; picture: the city view with its RETURN button and no end-turn button).
  **With the screen up the cached blocking type read `ENDTURN_BLOCKING_UNITS` while Warsaw's production notification
  (index 0) was live and its queue empty** (slice 744 -> 810 advancing): the founding's new block was never read in.
  After Esc: `ENDTURN_BLOCKING_PRODUCTION` / 0, and the end-turn button reads CHOOSE PRODUCTION (picture). So the stale
  value is not only a block that stays: it can also be a block not yet seen [L].
- The -ia patch does not touch the code involved [C]: its files (`ia-ee893bc18-440878983.patch` beside the -ia DLL)
  do not include `CvNotifications.cpp`; its `CvGame.cpp` hunks are includes, `ExternalPause`'s mutex name, the top of
  `CvGame::update` (the pollers) and `Serialize`; its `CvPlayer.cpp` hunks are an include, `getArtStyleType` and
  `Serialize`. Release-5.4.8 (7307aefdf) against the pin 26234371e [C, `git diff`]: `CvNotifications.cpp/.h` and
  `CvDllGame.cpp` identical; no hunk in `updateTestEndTurn`, `UpdatePlayers`, `SetEndTurnBlocking`, `CvPlayer::found`
  or `CvCity::chooseProduction`. The tag's `(3a) VP - EUI Compatibility Files/LUA/CityView.lua` equals the instances' and the owner's installed copy
  (`29a8668f...`, CRLF) once carriage returns are dropped (`bcf16c1a...` both).
- **Plain 32-bit pin** `i0ship-26234371` (`f92881a3...`, no Lua channel; `provision.py set-dll`, the -ia DLL kept and
  checked by hash), 15:05-15:10 EST: the same save, keys only. The first B came while the load screen ("Continue Your
  Journey") was still up and did nothing; Esc closed the load screen; on the map the settler was selected with the
  end-turn button reading A UNIT NEEDS ORDERS; **B founded Warsaw and its city view came up by itself** ("Warsaw has
  been founded.", RETURN button, no end-turn button, Production "No Production"); after Esc the map with the end-turn
  button reading CHOOSE PRODUCTION. Same pictures as the -ia leg **[L, reproduced]**. Without a Lua channel no order can
  be given outside the screen, so the stale block itself cannot be read there; the parts it is made of are the same
  (VP-EUI's screen, the EXE, DLL code identical, [C] above).
- **Stock VP 5.4.8 DLL** (the tag's `262aee88...`, a copy of the owner's installed file, no Lua channel), 15:06-15:20 EST: it
  loads this save (written by the -ia build), three trials, keys only, the load screen closed with Esc first. **Trial 2:
  the city view came up by itself after B** (the same picture as the plain pin: RETURN button, "Warsaw has been
  founded.") [L, reproduced]. Trials 1 and 3: 6 s after B the map, Warsaw founded, the Warrior selected and the end-turn
  button reading CHOOSE PRODUCTION, no city view; the following Esc then opened the game menu. Whether the screen did
  not open or was opened and closed at once by the selection cycle is not visible without Lua [I: a race, as the
  driver's]. So VP-EUI's opening of the screen on a founding is the stock game's own behaviour on 5.4.8 as on our pins;
  how often it is seen depends on timing (plain pin 1 of 1, -ia 1 of 1, stock 1 of 3; too few trials for a rate).

**Verdict.** Not a fault of the stock route, the -ia instruments, the x64 pin or the order of net messages: the stuck
block is the game's own UI behaviour (VP-EUI opens a founded city's screen; the EXE leaves the end-turn blocking
unrecomputed while a city screen is up) met by a scripted order given outside the screen. A player is not stuck: the
screen offers RETURN, and on return the end-turn button is right again (pictures above). Fixed in the driver (29.1).
c5h-g restored: `set-dll --original` (`3c2f47da...` by hash), front end restored, the save copy removed,
`provision.py verify --quick` ok.

### 29.4 Not verified, open

- No count of `UpdateTestEndTurn` calls was taken while a city screen was up: that the EXE skips it there is inferred
  from the stale cached value and its update within 2 s of Esc [I]. A proxy-route run with `counters=1` and a city screen
  held open would count it.
- The 0322 proxy run's pass is explained by timing (the bombard after the screen came up) [I]; the proxy route was not
  re-run with the driver's stuck timing.
- On the plain pin and stock 5.4.8 the stale value itself was not read (no Lua channel); what was seen there is the
  screen opening by itself on a founding and the end-turn button reading the right state after RETURN/Esc.
- The fixed driver ran one turn (`fix-ia-t150`); no long game with it yet. On the stock route it needs
  `--keep-unready 0` (26.8's assert box).

## 30. A save of the other DLL variant (22 / 43 majors) is refused before the DLL reads it (ticket 0354, 2026-11-15, account B)

Why [C, L: player-limits.md 5.1, 5.3, 8.3 items 4-5]: the 22- and 43-major builds' saves are incompatible both ways
(fixed-length `MAX_MAJOR_CIVS` arrays written without a count), nothing in a save records the count, and the DLL's
`CvGame::Read` only logs "Potential savefile format mismatch". Through the proxy a cross load ended in the EXE's CRT abort
(4254), since 0359 in D118's report, but only after the misread had run (the 43 save in the 22 DLL misreads the whole game
section silently first). MC ruling (queue block; thread #1943): refuse, never load wrong; a marked save that mismatches is
always refused; an unmarked save is decided by a structural check if one exists (upstream's own 43 saves carry no mark and
must load in our 43 build), else it is "unknown" and not refused. Answer: **a structural check exists** (30.2): option (1).

### 30.1 Where and how [C, `server/src/remote/srv_remote_savevariant.cpp`]

- **Where.** A server hook `ServerHook_ICvGame2_Read`. `ICvGame1::Read` is the first engine -> DLL read of the save body
  (0359's live load: one pull session per `Read`, `ICvGame1` first, 27.3); the server dispatches it as `ICvGame2_Read`
  (the proxy hands out `ICvGame2` objects; `kM_ICvGame1_Read` has no dispatch case). The generator finds the hook in
  `server/src/remote` (server side: hand-written hooks 27). Server side, not proxy side: the server holds the running count
  (`SrvMajorCivs()`, player-limits.md 8.2) and the D118 exit (`SrvRemote::ReportAndExit`).
- **What it reads.** Only bytes `CvGame::Read` reads anyway, from the session stream the generated body would hand the
  DLL; then `SetPosition` back to the start. A pull session keeps every pulled byte (`PullStream`, 27), so the DLL reads
  them again from the server's buffer and nothing is pulled twice; the engine stream ends where the DLL's reads end (the
  proxy does not reposition a pull session's engine stream, `StreamInRet`).
  1. u32 save version, the 16-byte data hash, `CURRENT_GAMECORE_VERSION` (u32 length + bytes at body offset 0x14).
  2. **The mark**: a `+m<N>` token (1-2 digits, 2..62) of the describe part (the text before the first space, tokens split
     at `+`). Our builds stamp it for another count than 22 (`tools/x64-dll/build.py` `version_string`). A marked save
     costs no further read.
  3. **The layout**, for a save without the mark: the body on, in 1,024-byte pieces, to the embedded save database's magic
     `SQLite format 3\0` (the database is the last thing `CvGame::Read` reads, `CvGame.cpp:11110`). Then every position p
     and count N in 2..62 for which this chain ends exactly at the database's u32 size field:
     `CvGameTrade::m_routesPerPlayer` (u32 64, then 64 x (u32 k, k x i32)), `m_aaiTechDifference` and
     `m_aaiPolicyDifference` (2 x N x N x i32, no count), `m_iNextID`, `CvGameCorporations` (u32 c, c x 20 B),
     `CvGameContracts` (two lists, u32 + 20 B each), `m_strGameId` (u32 L, L B) (`CvTradeClasses.cpp:2401-2408`,
     `CvCorporationClasses.cpp:449-456, 1950`, `CvContractClasses.cpp:245-252, 837`, `CvGame.cpp:10980-10983` at
     04b1211b8). Exactly one N: the save's count; none or several: unknown. No member of `CvGame::Serialize` before
     `CvGameTrade` is sized by the major count (player-limits.md 5.1), so there is no earlier anchor; the 19 counted
     `CvCityEspionage` lists and the run-time vectors are read much later (cities, players).
- **Decision.** Count unknown, or the running count unknown (`SrvMajorCivs()` 0: the 4253 guard has not run; a LOUD
  line): no refusal, the DLL reads as before. Count = running: a `server-report.txt` line `save variant: save "<v>" has N
  major civilizations (by its version mark|its layout: <details>), the running DLL N: loads (ticket 0354)`. Count !=
  running: the same line ending `REFUSED`, then `ReportAndExit(0x2056SSRR, "save-variant: the save was made with a
  SS-civilization build of Vox Populi (by ...; gamecore version "<v>"), the running DLL has RR major civilizations;
  refused before the DLL read the game section (ticket 0354)")`. The exit code carries both counts (application bit 29,
  0x56, SS, RR).
- **Proxy** (`proxy_rt.cpp` `SaveVariantExit`, `CallFailed`, `ReportFailure`): the failed call reads the server's exit
  code. 0x2056SSRR (both 2..62, different) logs `save variant: the server refused the save: it was made with a
  SS-civilization build, the running DLL has RR major civilizations (server exit code 0x...; ticket 0354)` whatever `pack`
  says; with `pack=1` the report's stage is `save-variant`, its code `mSS-save-mRR-dll`, a detail line names both counts,
  and the summary (popup-summary.txt) reads "This save was made with the SS-civilization version of Vox Populi, but the
  installed patch is the RR-civilization version. The save was not loaded, so the game had to end." Then as for any
  server death: the helper and `on_server_death` (a D33 session: `failed` and Fatal; since D240, 2026-11-22, the patch
  stays on unless the player presses "Remove 64bit patch", and the summary ends "...was not loaded; the game was ended.",
  release-driver.md W3c.6; was: the next start is the stock game, and the summary said so).
- **Helper** (`vp64helper.cpp` `popup`): stage `save-variant` has its own first line, "This save cannot be loaded with the
  installed version of the 64-bit patch, so the game had to end."; the summary line under it names both versions.
- **Test switch.** `VP_SERVER_TEST_MAJOR_CIVS=<n>` stands in for the running count only while `SrvMajorCivs()` is 0
  (hutest's fake DLL has no database); it never overrides a count that was read.

### 30.2 The structural check on real saves [L, 2026-11-15 16:20-16:29 EST]

- Prototype `evidence/0354/proto.py` (Python, the same chain) over 20 saves of 10 builds. 22: `g04b1211b8`,
  `g26234371e+ia...`, `gbbfacca1a+ia...`, `g76992d235`, `gc43ea1b9f`, `g29f15ac38` (all `Copy`) and
  `Release-5.4.6-3-g434f30f80 Dirty`; 43: `g04b1211b8+m43`, `g26234371e+m43`, `g26234371e+ia...+m43`. Tiny to Huge, turns
  0-401, game sections 12,851-896,482 B. **Exactly one (p, N) fit in every save, the right N**, and no database magic
  before the real one (`offline/proto-20-saves.txt`). Database sizes seen: 3,072 B (turn 0 up to the 43 game's turn 387)
  and 569,344 B (the huge games); so the 1,024-byte read pieces never pass the game section's end.
- The C++ block itself: `evidence/0354/variant_test.py` extracts `BEGIN/END 0354 SAVE VARIANT LOGIC`, builds
  `test_variant.cpp` with clang-cl and with MSVC `/fsanitize=address` (after a canary) and feeds it the 20 bodies:
  **PASS 321/321** (`offline/variant-test-asan.log`). Cases: 20 mark strings (`+m43` after `+ia<n>`, `+m22`, `+m62`,
  `+m2`; out of range `+m63`, `+m1`, three digits, `+m`, `+m4x`, `+mm43`, `+x43`, a token after the space,
  `...gabcm43`); per save the body as written, the mark removed (5 upstream-style unmarked 43 bodies: **43 by the
  layout**), `+m43` added to the 15 unmarked bodies (the mark decides), cut 100 B before the database, inside the version
  string, inside the header, a version length of 600, a control byte in the string (each unknown, no refusal). Every
  body was read no further than its game section's end; a marked one no further than its version string.
- **Negative control** (`--control`: `LayoutMajors` made to answer 0): FAIL, exactly the 20 layout cases
  (`offline/variant-test-control.log`).

### 30.3 hutest scenarios `variant-*` and the suites [L, 2026-11-15 16:33-16:40 EST, build `C:/c5-64bit-work/0354/b`, kept after the hand-back as `F:/c5-64bit-work/0354/b-bin` and `b-proxy`]

Build: `server/build_server.py --src F:/c5-64bit-work/0334/rebuild/src-x64-04b1211b --deps
F:/c5-64bit-work/0334/rebuild/m43/deps --config release --offline` (release-43's deps: bin DLL `CF981126...`), 75 s;
proxy `49ac48af...`. `build_hutest.py --scenario all`: **19/19 as expected** (`offline/hutest-scenarios-0354-build.log`).
The fake engine hands `ICvGame1::Read` a body that reports no size (a pull session) and throws on a read past its end:
- refused (host exit 3 through Fatal, the proxy's `save variant` line, report `REFUSED` and the `save-variant:` result,
  the fake DLL never logs `game_read`): `variant-mark43-on22` (by the mark), `variant-unmarked22-on43` and
  `variant-unmarked43-on22` (by the layout);
- loads (host exit 0, report `: loads`, `game_read` with the version string as the engine holds it, the engine stream not
  read past the game section): `variant-mark43-on43`, `variant-unmarked43-on43` (upstream-style), `variant-unmarked22-on22`;
- never a refusal: `variant-nofit-on43` (no count fits: `count unknown`, the DLL reads), `variant-short` (10 bytes) and
  `variant-nodb-on43` (no database magic, the read runs to the body's end): the engine stream throws, `exe_exception`
  (0359's path).
- **Control** on main's server and proxy (820fdc6f, `git archive`, the same source and deps; scratch build, deleted):
  the three mismatch cases fail as they must (host exit 0, `dll.log` has `game_read`: the DLL reads the other variant's
  body), and every other `variant-*` case misses the `(ticket 0354)` report line (`offline/hutest-scenarios-main-control.log`).
- Suites on build b, plain (`offline/suite-*.log`): `test_vp64helper.py` 109/109 (new case group `stage` / save-variant:
  report.txt and the proxy's summary, report.txt only, the summary's `(stage: code)` form only, `save-variantx`,
  report.txt over a server-death summary; the summary line naming both versions follows), with `--asan` 216/216;
  `test_proxy_rt.py` 106/106; hutest 47 checks 0 failed; `run_preinit_report_test.py` 5/5; `run_power_test.py` pass;
  `run_rt_ring_test.py` 10/10; `run_vfs_test.py` 17/17.

### 30.4 What is not covered [C, I]

- A save whose embedded database is empty (u32 0, no magic; never seen) or missing: an unmarked one is read on to the
  engine stream's end and ends in `exe_exception` (0359's path) instead of loading. A database of fewer than 1,039 bytes
  could let the last read piece pass the game section's end; the SQLite files seen are 3 pages of 1,024 B or more [I, 20
  saves].
- A save whose layout fits no count, or several (never seen), is not refused (left to 0359's line).
- Other major counts (2..62) are handled the same way; only 22 and 43 builds exist.
- The stock route (no proxy, the 32-bit DLL alone) is upstream behaviour and unchanged.

### 30.5 Live on the product path [L, 2026-11-15 20:16-20:45 EST, workstation, Goldberg instance c5h-c]

Driver `evidence/0354/live_0354.py` (0359's prep / run / restore, with several saves per prep, `--strip` and `--recheck`),
its prep, run and restore phases called from scratch shell scripts (deleted at the hand-back; the commands are in
`evidence/0354/live/live43-state.json`, `live22-state.json` and the prep/restore logs); evidence `evidence/0354/live/<arm>/` (facts.json,
server-report.txt, server.log, vpproxy.log, the report's `[failure]` section and file list, popup-summary.txt, the real
helper's `popup --no-ui` text, the windows the guard cloaked, the DLL's `CvGame::Read` log lines; the report folders
themselves stayed in the scratch folder, D119). Set-up as 0359 (27.2): a test pack of the layout
(`tools/release/pack.py`, the instance's own 32-bit DLL `e589306e...` as its test-only x86 entry), `provision.py
set-proxy`, vpproxy.ini `mode=on local_answers=1 pack=1`, a **fake** `vp64helper.exe` (records its command line; COMMON
rule 14), the saves copied under our own names, the load from the load menu by the harness (`proxy/tools/live/arm.py`).
c5h-c's config.ini is stock (a stock session, 22.1). Layouts, both with this branch's server.exe and proxy `49ac48af...`:
**43** = `C:/c5-64bit-work/0354/b` (now `F:/c5-64bit-work/0354/b-bin`; bin DLL release-43's `CF981126...`, version `...+m43 Copy`); **22** = a copy with
the release pin's DLL `29F58E17...` (`Release-5.4.8-51-g04b1211b8 Copy`). Saves (copies): `X0354-22-TINY8` of c5h-c's
`C5H-3600-TINY_0008` (Tiny, turn 8, version `Release-5.4.8-18-g29f15ac38 Copy`: an unmarked save of an older,
non-variant build); `X0354-22-TINY9V` of `C5H-3600-TINY_0009V` (`...g26234371e+ia440878983 Copy`, the 32-bit pin's
`-ia` build); `X0354-43-SMALL` of 4255's `4255-SMALL43-PP-T000` (43 build, turn 0, `...+m43 Copy`, `9c2d9b7c...`);
`X0354-43U-SMALL`, the same with `+m43` taken out of the body's version string (civ5save unpack / pack, `b768e908...`):
an upstream-style unmarked 43 save.

| Arm (layout, save) | Result | Server's line | What else |
|---|---|---|---|
| refuse-22on43b (43, TINY8) | **refused**, all 14 checks | `has 22 major civilizations (by its layout: trade arrays of 22 x 22 ... database at 10574), the running DLL 43: REFUSED` | report stage `save-variant`, code `m22-save-m43-dll`; popup lines below |
| refuse-43on22 (22, 43-SMALL) | **refused**, all 14 | `has 43 ... (by its version mark), the running DLL 22: REFUSED` | the proxy's line 29.67 s after its start, report written, `FATAL ... exit code 3` at 29.81 s |
| refuse-43Uon22 (22, 43U-SMALL) | **refused**, all 14 | `has 43 ... (by its layout: trade arrays of 43 x 43 after m_routesPerPlayer at body offset 5575, database at 20687), the running DLL 22: REFUSED` | |
| load-43on43 (43, 43-SMALL) | **loads**, one AI turn, all 10 | `(by its version mark), the running DLL 43: loads` | |
| load-43Uon43 (43, 43U-SMALL) | **loads**, one AI turn, all 10 | `(by its layout ...), the running DLL 43: loads` | the DLL logs "Potential savefile format mismatch" (the strings differ; harmless, the layout is right) |
| load-22on22 (22, TINY9V) | **loads**, one AI turn, all 10 | `has 22 ... (by its layout ... database at 10696), the running DLL 22: loads` | |
| load-oldbuild22on22 (22, TINY8) | **loads**, one AI turn, all 10 | `(by its layout ... database at 10574), the running DLL 22: loads` | the unmarked save of a non-variant build |

- Every refusal: the EXE ended by itself (`wait_game` gone) through the proxy's `FATAL` (on_server_death=exit); the
  server's report has the `REFUSED` line and `result: FAILED: save-variant: ...`; the proxy logged `save variant: the
  server refused the save: ...` with the exit code (0x20562B16 for 43 on 22, 0x2056162B for 22 on 43); the report folder
  holds report.txt with `stage: save-variant`, `code: m43-save-m22-dll`, the failed call `ICvGame1::Read(FDataStream &)`,
  the detail line, the exit code and server.log's result line (22.9), no `.dmp`; the proxy started `vp64helper.exe popup
  --report <folder>`; **no dialog window** in the EXE's guard log (only the frame-cap dummy, GDI+, the game window and two
  ActiveMovie windows: no `#32770`, so no CRT box); and **no `CvGame::Read` line** in any log of the run folder (neither
  "Savefile was generated from gamecore version" nor "Potential savefile format mismatch": the DLL's `CvGame::Read`
  never ran), while every load arm's `custommods.log` has the first line (the positive control of that check).
- The real helper's `--no-ui` text for the refused 43 save on the 22 layout: "This save cannot be loaded with the
  installed version of the 64-bit patch, so the game had to end." / "This save was made with the 43-civilization version
  of Vox Populi, but the installed patch is the 22-civilization version. The save was not loaded, so the game had to
  end." / Report: <folder> / the hand-in line / the public-issue line / Open folder | Close. The reverse names 22 and 43
  the other way round.
- Load arms: in game, `tools/harness/checklist.py --turns 1`: the AI-turn step passed in all four; its city-screen step
  fails on the turn-0 43 save (no city yet; not this ticket's), so the driver's `turns_played` reads the AI-turn step
  (`--recheck` re-evaluated the two 43 load arms after that fix; the first refusal arm `refuse-22on43`, run 16:41-16:44 before the account stop, is kept with its
  one false check, the driver read the popup's title line as its first line, and was run again as `refuse-22on43b`).
  The checklist's step pictures are in the Visual Checks feed as DIAGNOSTIC, result info (D112).
- Restore after each layout: the added files and report folders removed, `set-proxy --original`, `frontend.py restore`,
  the standard files by SHA-256 (`backup.py restore` rc 0), `provision.py verify c5h-c --quick` rc 0; claim released.

### 30.6 Not verified, open

- The real popup window (only `--no-ui`; COMMON 14) and a D33 session (the `failed` verb's path with this stage; the
  summary said the next start is the stock game, which was 3523's behaviour; since D240, 2026-11-22, the patch stays on and
  the summary says only that the game was ended, release-driver.md W3c.6) [C only].
- A save of upstream's own 43 build was not at hand: the upstream-style case is our 43 save with the mark removed (the
  layout is the same source, `MOD_GLOBAL_MAX_MAJOR_CIVS` on the compile line) [I].
- The 43 games at hand have 30 majors at most and start at turn 0 or 25 (offline also turn 387); a 43 save with majors in
  every slot was not loaded live.
- The helper suite's new cases were not run against the old helper as a control (the hutest mismatch cases were run
  against main).

## 31. Blank map-option values in Advanced Setup on the proxy route (the owner's report, 2026-11-15 ~20:50 EST, first playable build)

The owner, playing the first playable build (0324, layout `x64-04b1211b-pfdee656`, thin client) in their own Steam install
on the workstation: in **Advanced Setup** with map type **Continental Drift - v3.1** (VP's
`(2) Vox Populi\Mapscripts/Continental_Drift.lua`), all 15 map options showed their names (Map Ratio, Temperature,
Rainfall, ... Start Placement) but an **empty value box**. Session `Logs\VP64\run\20261115-204448-46108-1` (EXE pid
46108, 20:44:48-20:49:15; mods CP, VP, EUI files and the owner's others; no game started).

### 31.1 Cause [C code, L logs; corrected 2026-11-15 by ticket 0380: fault 1 is not on the product route]

*Correction (2026-11-15 22:27 EST, ticket 0380, MC answer #2039): fault 1 below describes `proxy/gen/gen_proxy.py`, the
generator of the compile-only proof (`proxy/proof`, ICvRandom1 and ICvTeam1), which the product does not use. The
product's proxy and server are generated by `proxy/gen/gen_runtime.py` (`proxy/build_proxy.py`, `server/build_server.py`),
whose `int&` / `bool&` / `float&` codec (`PScalarRef`) has been in/out since d345f35c (2026-11-10, task 2301): the
generated `Proxy_ICvPreGame2::GetMapOption(const char*, int&)` sends `iValue` and the server starts from it [C,
generated code of the pin's generator, 0380]. So the owner's blank values came from fault 2 alone, together with an EXE
initial value that the screen cannot show (31.6). Was: "two faults together", fault 1 the cause of the 0.*

1. **(Proof generator only, see the correction) `out_scalar` is copy-out only, so an untouched `int&` comes back as 0.**
   The server stub declares the out value as
   `T n = T();` (`proxy/gen/gen_proxy.py` 345-347), the proxy sends no input for it (189-190), and after the call the
   server always encodes it (375-378) and the proxy always assigns it to the caller's reference (214-219). A DLL method
   that returns `false` without writing its `int&` therefore hands the EXE a 0 where the EXE's own value would have
   stayed. `ICvPreGame1::GetMapOption(const char*, int&)` is such a method: `CvPreGame::GetMapOption` (fork
   `CvPreGame.cpp` 1490-1515) writes `iValue` only when the option is set, or when it finds
   `MapScriptOptions.DefaultValue` for `CvPreGame::mapScriptName()` in the DLL's database; otherwise it returns false
   untouched. The stock setup screen (`Assets/UI/FrontEnd/GameSetup/AdvancedSetup.lua` 127-133) reads
   `PreGame.GetMapOption(option.ID)`, uses the default when it is `-1` and otherwise `option.Values[savedValue]`;
   `Values` is a 1-based Lua array, so `Values[0]` is nil and the button text is never set: the blank box.
   [I: the EXE's binding starts from -1 for an unset option; inferred from the stock Lua's `~= -1` test.]
2. **The server's database lacks the mods' map scripts until the next `CacheGameDatabaseData`.** The EXE scans the
   active mods' map scripts into `MapScripts` / `MapScriptOptions` / `MapScriptOptionPossibleValues` after
   `SetGameDatabase` (2504 follow-up, `srv_remote_db.cpp` `SrvDbCheckEngine`); the server pulls those tables only at
   its check after a `CacheGameDatabaseData`. In the owner's session 2 the server's copy (`db/Civ5DebugDatabase.1.db`) held
   38 map scripts and none of VP's `Mapscripts/` (only `My Games\Maps\Continental_Drift.lua` v1, 15 options, 79 values);
   no later `CacheGameDatabaseData` ran (no game was started), so the DLL's default lookup for the v3.1 file found no
   row and returned false, and the EXE got its own initial value back (31.6; was "fault 1 then turned that into 0",
   corrected 2026-11-15 by 0380). In the owner's session 1 (`20261115-201642-40512-1`, a game was
   started) the second `CacheGameDatabaseData` at 92 s pulled them (44,701 -> 45,331 rows; the copy holds 62 scripts,
   VP's v3.1 with 15 options and 79 values). Proxy counters: 45 `GetMapOption(const char*, int&)` calls and one
   `SetMapOption` in session 2.

On the stock route the same lookup runs on the EXE's database, which has the rows, so the values show.

### 31.2 What it affects [I]

- Display: every unset map option of a map script that the server's copy does not yet hold shows blank (the mods' map
  scripts; the base game's and `My Games\Maps` scripts are in the copy at the boot). Choosing a value from the list
  sets it (`SetMapOption` is by value) and shows it.
- The game: no wrong value is stored by the screen (it only reads). At game start the pull runs before map generation,
  so `Map.GetCustomOption` finds the database default for an untouched option [I: from the code and session 1's pull
  order; the generated map's options were not checked].
- Other `out_scalar` methods may differ from stock in the same way wherever the DLL returns without writing the out
  value and the EXE relies on its initial value [I: not surveyed; `policy-table.tsv` lists the `out_scalar` rows].

### 31.3 Fix directions (as written before ticket 0380; what was done is 31.5-31.7)

- Make `out_scalar` in/out: the proxy sends the caller's current value, the server stub starts from it (one generator
  change in `gen_proxy.py`, regenerate, parity tests). This alone restores the stock display (the screen then gets -1 and
  uses `option.DefaultValue`).
- Optionally pull the three map-script tables before pregame lookups on the front end (for example on the first
  `GetMapOption` / `GetMapOptionCount` after a `SetGameDatabase`), so the DLL's own default lookup matches stock.

### 31.4 Not verified, open (as of the report; answered by 0380 in 31.5-31.7)

- Not reproduced on a test instance and not run with the patch off on the owner's install (the stock-route claim is from the
  code path).
- The EXE's initial value (-1) is inferred, not read.

### 31.5 Census of the generated parameters that return a value (ticket 0380, 2026-11-15) [C]

`python -I proxy/gen/tests/out_param_census.py --out evidence/0380/out-param-census.tsv` lists every mechanical
parameter of both generators whose codec has an out half (product: `gen_runtime.py`, every interface, both
directions; proof: `gen_proxy.py`), with its class and the reason. Product, before 0380:

| Codec (gen_runtime.py) | Rows | Was | Class |
|---|---|---|---|
| `PScalarRef` (`int&`, `bool&`, `float&`) | 23 | in/out since d345f35c | not affected (`GetMapOption(const char*, int&)` among them) |
| `PScalarPtr` (`int*` cursors, `GetCurrentResearchTech`, `GetSteamStat`) | 7 | in/out with presence | not affected |
| `PCvStringRef` (`stripSpecialCharacters`) | 1 | in/out | not affected |
| `PStream` (`FDataStream&` of Read* / Write*) | 23 | a stream object | not affected: read streams are consumed, write streams appended to, as in-process |
| `PCallerBuf` (`char*` + capacity) | 7 | **copy-out**: the callee's zeroed buffer went back | fixed by in/out for `GetDefineSTRING` (false without writing when the define is missing), by-index `GetGameOption` / `GetMapOption` (false without writing), `GetHexDebugLayerString` (true without writing on a water plot or an unknown layer), `GetGreatWorkAudio` (false without writing); no difference before for `SetTimeStr` (always writes) and `GetEvaluatedMapScriptPath` (DLL -> engine; the DLL's one caller passes a zeroed buffer and uses it whatever the result) |
| `POutArray` (`ICvTeam1::GetTechs`) | 1 | **copy-out**: every element past the techs found came back 0 | fixed by in/out (the caller reads the returned count [I]) |

Since 0380 the caller buffer sends its current string (up to the first NUL, at most min(capacity, 1 MiB) bytes) after
the capacity and the callee's copy starts from it; the caller writes back at most `capacity` bytes and a NUL only when
there is room (a buffer that came without a NUL inside its capacity goes back as it came). The out array sends its
capacity clamped to 65,536 and that many current elements (the callee refuses a larger count with
`kStatusBadArguments`). Proof generator: `out_scalar` (25 rows over the model's interfaces: scalar references and the `int32*` of `GetSteamStat`,
which keeps its presence: NULL stays NULL) and `out_str` (`CvString&`, 4 rows) are in/out since 0380; its `out_array_i32`
(`GetTechs`) stays copy-out there (compile-only proof, not the product). The committed proof output
(`proxy/proof/generated/`) is unchanged byte for byte: ICvRandom1 and ICvTeam1 have none of these codecs.
Hand-written hooks are not generated and not in the census. Guard: `proxy/gen/tests/test_gen_inout.py` (10 tests,
offline, under 2 s) fails on any product row without an in half, with a negative case that drops the caller buffer's
in half and expects the census to report 7 `COPY-OUT` rows and exit 1.

### 31.6 The EXE's answer for an unset option, and the server fix (ticket 0380, 2026-11-15)

- **`PreGame.GetMapOption(name)` returns `nil` when the DLL's `GetMapOption(const char*, int&)` returns false** [L,
  2026-11-15 22:24 EST, c5h-c, stock route with the pinned 32-bit `i0ship-26234371-ia`, and both proxy layouts]: the
  test front end asked for an option no map script has (`C5H_0380_NO_SUCH_OPTION`) and got `nil` (type nil) on all
  three arms. The EXE's binding does not hand back its initial value; it returns nothing. The setup screen's
  `savedValue ~= -1` is then true and `option.Values[nil]` is nil: the blank box. So the in/out question of 31.1 never
  mattered for this screen; only the DLL's `true` / `false` does, and that depends on the rows of
  `MapScriptOptions` in the database the DLL reads (31.1 fault 2). Replaces 31.1's "[I: the EXE's binding starts from
  -1]".
- **Fix (server, `server/src/remote/srv_remote_db.cpp` `ServerHook_ICvPreGame2_GetMapOption_s37`)**: the method is now a
  server hook with the generated wire (CALL self, str name, i32 value; RETURN b, i32 value). When the DLL returns
  false, the hook runs `SrvDbCheckEngine` (the identity check of 2504's follow-up: the engine's fingerprint against the
  server's copy, and a pull of the tables the EXE changed itself) and, if tables were pulled, asks the DLL again. At
  most one such check per map script between two checks of any kind (`s_mapMissChecked`: script name -> the check
  counter after its own check), so a setup screen that asks for 15 options of a script costs at most one check, and a
  script that has no defaults at all costs one check, not one per option. A miss the pull does not fill goes back as
  the DLL's own answer (false, value unchanged) with a LOUD line naming the script (MC answer #2039: nothing invented).
  The DLL then stores the default it found in its pregame option list, as it does in-process. `server-report.txt` has a
  line `map option lookups without a default in the server's copy (issue 0380): N engine checks (T s), P of them
  pulled tables; F lookups filled by the pull, U left without a default`.
- The census (31.5) found and fixed the same kind of difference in the product's caller buffers and the out array;
  the native round trip `proxy/tests/inout/test_inout.py` runs them (31.7).

### 31.7 Live results and tests (ticket 0380, 2026-11-15 22:22-22:41 EST, workstation, c5h-c, CP + VP + EUI) [L]

Driver `evidence/0380/live_0380.py` (prep / arm / restore); the test front end is the vp-dll-dev skill's patched
MainMenu.lua with `evidence/0380/c5h0380_probe.lua` inserted (the patched game file itself is not committed, D83):
once the mods are active it prints, for every map script that has options, `PreGame.GetMapOption` of each option and
the value the stock screen would show; the setup screen is then reached by clicks (Single Player, Set Up Game,
Advanced Setup; the instance's own settings had Continental Drift - v3.1 selected, as the owner's). Arms, all on the
instance's Goldberg install, its 32-bit DLL set to the pin `i0ship-26234371-ia` (3c2f47da...):

| Arm | Route | Options of 46 scripts (249) blank | Continental Drift - v3.1 on the screen |
|---|---|---|---|
| stock | 32-bit DLL, no proxy | 0 | every value shown (reference picture) |
| proxy-old | the pin's layout `x64-04b1211b-pfdee656` (server 5B254430..., proxy 75C40CEC...) | **112** (every option of VP's 20 map scripts in `Mapscripts/`) | **all 15 boxes empty**: the owner's report reproduced |
| proxy-new | the pin's DLL and libraries with this ticket's server.exe 000efbaa... and proxy 167fa063... (`C:/c5-64bit-work/0380/build`, commit 1eb53dde) | 0; all 249 `get=` values equal the stock arm's | every value shown; the options panel (crop 1236,360-1704,1036 of 2554x1411) pixel-identical to stock (median / p95 / max 0 / 0 / 0) |
| proxy-new-screen | as proxy-new, the skill's front end without the probe (the setup screen's own lookups come first) | (no probe) | every value shown; options panel pixel-identical to stock (0 / 0 / 0) |

- proxy-new, server.log: the first lookup that missed (the probe's unknown option on Continental Drift, 33.1 s after
  the boot) ran one check: engine 44,674 rows against the copy's 44,044, DIFFERENT; one pull round of 3 tables
  (MapScriptOptionPossibleValues, MapScriptOptions, MapScripts), IDENTICAL after it; 0.695 s in all (fingerprints
  and pull); then the LOUD line for the unknown option (no default anywhere, the DLL's false goes back). No further
  check: every other lookup found its row.
- proxy-new-screen, the cost on the real path: the setup screen's first lookup that missed (option 11 of Continental
  Drift, 65.8 s after the boot, while the screen was opened by the clicks) ran the one check: 0.526 s (fingerprints
  0.128 + 0.100 s, the pull of the 3 tables 0.278 s), filled; no other check. server-report.txt: `1 engine checks
  (0.526 s), 1 of them pulled tables; 1 lookups filled by the pull, 0 left without a default`. Workstation numbers,
  not a timing claim (COMMON 23): an indication of the size, once per front-end session after the mods' scan.
- Pictures in the Visual Checks feed (issue 0380, all DIAGNOSTIC: harness instance, test front end): stock
  reference, proxy-old (result fail, comparison image), proxy-new and proxy-new-screen (result pass, reference and
  comparison).
- Tests (offline, evidence `evidence/0380/offline/`): `proxy/gen/tests/test_gen_inout.py` 10/10;
  `proxy/tests/inout/test_inout.py --scratch DIR` 20/20: the codec lines of gen_runtime.py for GetDefineSTRING,
  GetTechs and GetMapOption(const char*, int&) composed into caller and callee over the real vpipc marshal, 11/11 cases
  each on MSVC x86 and x64, plain and `/fsanitize=address` (no AddressSanitizer report; an ASan canary caught), and
  the negative control (the pre-0380 copy-out halves) fails exactly the four untouched cases on all four builds.
- Not checked: the generated map of a game started from that screen (the DLL's lookups now match stock before the
  game starts, and the pull at a game start's `CacheGameDatabaseData` was there before); the owner's own install (D26).

## 32. Upstream master's wrapped Lua functions cross as DLL functions (queue ticket 0313, 2026-11-16, account B)

Upstream master (#13489; the master port `x64-release-master-ba8aafdc-r2` 76b3a5f70) pushes every DLL C function as a C
closure of `protectedWrapperAux` whose one upvalue is the function itself (`Lua/!CvLuaApi.h` `#define lua_pushcclosure
luaVp_pushcclosure`, `Lua/CvLuaSupport.cpp`; the wrapper calls the upvalue under `lua_pcall` inside a C++ `try` and
re-raises an error) [C]. The bridge sent such a closure as `kLSRef` (a registry reference per crossing), so on master no
DLL function had an id or a name, every call took the server-ref path and the UI-Lua mirror (17.6) could answer nothing
(checklist 10b, issue 0308) [L]. Branch `task-0313-proxy-lua-master-closures`; scratch `C:/c5-64bit-work/0313/`
(deleted at the hand-back); scripts and outputs in `evidence/0313/`.

### 32.1 What changed [C]

- **Server, `srv_remote_lua.cpp`.** A kLCFunc id now stands for a pair (C function, wrapper or none). `LearnWrapper`
  runs after each channel's first full `RegisterScriptLibraries` until one finds a wrapper: it counts the C closures of
  the wrapper's shape (exactly one upvalue, a C function without upvalues; both functions inside the DLL's image, read
  from its PE header in `SrvLuaInstall`) among the global functions, the global tables, the `__index` table of a global
  userdata's metatable (master's `Game`, `Map`) and `vpdllinternals[<tag>].__meta.__index`, and takes the C function
  most of them share when at least 8 do (master: 3,893 closures, all of one function, at DLL+0x1271d0 in this build;
  the pin: none, nothing learnt). `CFuncIdAt` gives such a closure the id of (function, wrapper); decoding a kLCFunc of
  a wrapped id pushes the closure again (`lua_pushcfunction`, then `lua_pushcclosure(wrapper, 1)`), so every call
  still runs through master's wrapper. A closure of any other shape (iterators such as `Player:Cities()`, Fractal's
  methods, `luaVp_pushcclosure` with its debug helper) stays `kLSRef`. `NameFuncs` also names the functions of
  `vpdllinternals` by the entry's `__hint` (`TypeGlobals<T>::TypeName()`; `PlayerBase`, CvPlayer's type name, shares
  the tag `PLYR` with `Player` and is registered last, so it is named `Player` as at the pin) and of a userdata global's
  `__index` table. The first non-empty `kRt_LuaRegister` reply of the process carries one extra entry,
  `__vpproxy_function_names` = {name = kLCFunc id, ...} with every recorded name, only when a wrapper was learnt (the
  pin's reply is unchanged). At the exit the server writes `proxy-lua-names.tsv` (name, id, wrapped) beside
  `proxy-lua-calls.tsv`; server-report.txt says how many names there are and whether a wrapper was learnt.
- **Proxy, `proxy_lua.cpp`.** The register hook takes the names from that entry (`NoteThunkName`, so the purity file's
  Lua names mark the ids pure) and never sets it as a global. `SRefIndex` keeps, per server-side userdata stub (a
  weak-keyed registry table), the DLL function (FuncThunk) a string key gave: master's function tables are filled at
  registration and no type overrides `CustomizeMTT` at 76b3a5f70, so a key names the same function for the object's
  life; every other value crosses each time [C; I that no DLL code changes such a metatable later]. A pure-listed call
  whose arguments include server-side userdata stubs (master's instances, until 0314) cannot be a mirror key but no
  longer moves the epoch (`ForcePureNext`, as the pin's object-returning pure calls); a Lua function or a plain table
  among the arguments still moves it. `ProxyLuaMirrorReport` logs the three counts (`local: Lua bridge (issue 0313)`).
- **hutest.** The fake DLL registers `HtWrapped` (ten closures of `HtWrapperAux` and `Iter`, one around an inner closure
  with an upvalue) and `HtGame` (a zero-size userdata whose metatable `__index` holds one wrapped function); 8 new host
  checks: `lua_wrapped_run`, `lua_wrapped_call`, `lua_wrapped_error` (the message survives the wrapper),
  `lua_wrapped_iter_shape`, `lua_wrapped_is_dll_function` (the engine-side thunk has one upvalue, not the server-value
  thunk's two), `lua_wrapped_iter_is_server_value`, `lua_wrapped_meta_index_is_dll_function`, `lua_userdata_method_cached`
  (`HtGame.GetMarker == HtGame.GetMarker`).

### 32.2 Results [L, 2026-11-16 21:37-22:15 EST, workstation]

Builds (`--config release --offline`): the pin's DLL 74588135... from a copy of 9045's bF `build_all` (0442's copy);
master's `testdata` stage and master's `build_all` (`--unlocked --no-gate`, a git worktree of a shared clone at
76b3a5f70: `build_all` refuses a plain copy of any commit but the pin's); server and proxy by `build_server.py --deps`
from this branch (9ff4456ef) and, as the negative arm, from main c2c73496c. Checks: checklist step 12's commands
(`evidence/0313/offline.sh`); `pverify` is the verify leg with `VP_PROXY_PURITY` set: for the pin the derived
`purity.tsv` (137 method keys, 29 Lua names); for master a TEST-ONLY file made of master's derivation (122 method rows
and 1 Lua row: `derive_purity.py` drops 28 of the 29 Lua rows on master because #13489 rewrote `BasicLuaMethod` and
`GetInstance`, not because the functions changed) and the pin audit's 28 other Lua names (`evidence/0313/make_test_purity.py`).

| | pin, main's server (0308) | pin, this branch | master, main's server (0308) | master, this branch |
|---|---|---|---|---|
| verify leg, Tiny 0-50 | PASS | **PASS**, host Lua errors 0 | PASS (testdata game folder) | **PASS** with the 5.4.8 and the testdata game folder, host Lua errors 0 |
| kRt_LuaCall | 3,113,308 | **3,113,308** | 3,121,473 | 3,123,179, of which **15,237 on the server-ref path** (closures of another shape, Fractal's [I]) |
| kRt_LuaIndex | 15,175 | **15,175** | 3,112,478 | **701,672** (2,410,806 lookups answered by the per-stub cache, 686,496 fills) |
| `proxy-lua-calls.tsv` rows | 149, named | **149**, named | 3, none named | **156**, every DLL function named |
| names recorded (`proxy-lua-names.tsv`) | - | 3,585, wrapper none | - | **3,598** (3,477 wrapped closures), all sent to the proxy |
| `lua-pure-bindings.tsv`'s 35 bindings among the names | - | **35 of 35** | 0 | **35 of 35** (all wrapped) |
| UI-Lua mirror, verify leg with a purity file | (not loaded) | **1,198,941 hits verified, 0 mismatches** (= 3503's reference) | - | **3,305 hits verified, 0 mismatches** (Game.GetGameTurn 1,983, Game.GetActivePlayer 1,322), the same with both game folders |
| getter mirror, same runs | - | 1,300,212 verified, 0 mismatches | - | 47 verified, 0 mismatches |
| test_proxy_rt / hutest | 87/87 / 47 checks | **107/107 / 55 checks, 0 failed** | - | 107/107 / 55 checks, 0 failed |

- Negative arm, main's server and proxy (c2c73496c) with this branch's hutest: **55 checks, 3 failed**, exactly
  `lua_wrapped_is_dll_function` (the closure arrives as a server-value thunk), `lua_wrapped_meta_index_is_dll_function`
  and `lua_userdata_method_cached`; the other new checks pass there (calls through kLSRef worked before).
- In master's purity run 1,577,853 pure-listed calls with stub arguments crossed without moving the epoch. Still the
  mirror hits little on master: instance methods take server-side userdata stubs, which are no key (`ArgsSimple`), and
  the first lookups of each new stub cross (Map.GetPlot, GetCapitalCity give new stubs every Update). Game's two pure
  scalar functions are what hits. Instances as tokens are 0314's. *(Done 2026-11-24, section 35: master's UI-Lua
  mirror then verifies 1,198,311 hits, 0 mismatches.)*
- hutest `--scenario all` stops at its second scenario, `foreign-strict`, with a Python `PermissionError` reading
  `server-report.txt` while server.exe still has it open, **with main's server and proxy too** (pin-old arm); run one by
  one (`evidence/0313/scen_each.py`), 17 of 19 pass on this branch for the pin and for master, and the two strict lock
  scenarios (`foreign-strict`, `leak-strict`) fail that way; on master's build of this branch also `variant-nodb-on43`
  and `variant-short` (2 of 2 runs each; main's build on the same DLL 2 of 2 pass): the proxy logged `threw ���b�read
  past the end ...`. Cause [C, L]: `proxy_rt.cpp`'s served-call catch keeps `what = e.what()`, a pointer into the
  exception object, and formats it after the catch block has destroyed the object (a use-after-free; this branch's
  proxy only moved the heap layout). This branch's build with the text copied inside the catch (scratch copy, not
  committed) passes both, the LOUD line reading `threw hutest: read past the end of the save body` (`evidence/0313/r4-*`,
  `r5-*`). Fixed separately as ticket 0451 (MC #3215). The strict-scenario failures are not caused by this change; proposal
  `tracker/issues/NEW-hutest-strict-scenario-report-open.md`.
- Workstation run times are indications only (COMMON 23); none is claimed.
- *Added 2026-11-23 (ticket 0693):* this section's hutest change left `hutest --break` red. The fake DLL's
  `lWAnswer` adds 1 under `HUTEST_BREAK=1` (`HtWrapped.Answer(4,2)` gives 43), so `lua_wrapped_call` fails in break
  mode, but `BREAK_FAILS` in `build_hutest.py` did not list it: from 0313 (a0679b7dd) to 0693 every `--break` run ended
  `hutest --break: FAIL; ... unexpected failures: ['lua_wrapped_call']`, exit 1 (55 checks, 17 failed) [L offline
  2026-11-23, main f9891ef72 server+proxy release, DLL pin src-x64-1b87dd185]. The perturbation is meant (break mode
  perturbs every answer), so 0693 listed the check in `BREAK_FAILS` (17 expected failures now); the plain run is
  unchanged. Outputs: `evidence/0693/acceptance.txt`.

### 32.3 Not verified, open

- No 32-bit EXE game on master through the proxy (the headless verify leg only); no MSVC `/fsanitize=address` run of
  the server (COMMON rule 9: the change is in the server's Lua codec, which has no unit harness of its own; hutest
  exercises it in the real server.exe).
- The per-stub method cache assumes that no DLL code replaces a userdata's metatable or edits a type's function table
  after registration (read in `!CvLuaApiInstanceTemplates.h` and the `CustomizeMTT`/`CustomRegister` users at 76b3a5f70,
  not by a search of every `lua_setmetatable`).
- The wrapper is learnt by a count (at least 8 closures of one C function in the DLL's image); a DLL with fewer wrapped
  functions keeps them as `kLSRef` (correct, only slower).

## 33. The popup names the server's own reason first (queue ticket 0511, 2026-11-18, account B)

Why: ticket 0342's live fog install-fault run (2026-11-17, `fog-of-war.md` 7.3 Live (e)) failed loud as D118/D125
require, but the D33 popup's summary line read "The 64-bit part of the patch failed (server-death: PeerDied); ...": the
proxy's view (the pipe closed), while the server had written why it stopped (the missing `fogcloudtile_dif_01.dds`).
Only report.txt (22.9's quoted line) and the server.log tail named it. A beta tester reads the popup first.

### 33.1 As built [C] (`proxy/src/proxy_report.{h,cpp}`, `proxy/helper/vp64helper.cpp`; `proxy_rt.cpp` unchanged)

- `ProxyReport::ServerReason(text, maxLen)` reads the server's **verdict**, the line `WriteReport` in
  `server/src/remote/srv_remote_main.cpp` writes at every orderly exit: `result: FAILED: <why>; remote mode, section
  ...` (at the line's start or after its `[ time] ` stamp). The **last** verdict line of server.log's tail decides: if it
  is not `result: FAILED: <why>` (a `result: passed`, srv_main's `result: FAILED; turns ...`, an empty reason) there is
  no reason; a log without a verdict line (a crash, a cut tail, a server killed from outside) has none either. Returned:
  `<why>` without the `; remote mode, section ...` tail, control characters made spaces, outer blanks dropped, clipped to
  `kReasonMax` = 600 bytes never inside a UTF-8 sequence, with ` [...]` after a cut. It reads the scrubbed tail, as
  22.9's quote does. Why the verdict and not 22.9's last FAILED/LOUD line: a LOUD or FAILED line can come from a part
  that went on (an earlier non-fatal LOUD), and the 400-byte quote is clipped mid-sentence (0342's was); the verdict is
  the server's own statement of why it ended (`s_strFail`, the first `Fail` reason).
- `ProxyReport::Write` adds under `[failure]`, after 22.9's quoted line, `server's reason: <why>` when there is one;
  nothing otherwise. Every stage (the run folder is the boot's own); popup-summary.txt is unchanged.
- `vp64helper` (both verbs, `popup` and `failed`, through `ReportLines`): when report.txt's `[failure]` section holds a
  `server's reason: ` line with a non-empty text, the popup's **first line** is "The game's 64-bit server stopped with
  this error: <why>" (clipped to 700 characters + ` [...]`), the helper's own line for the stage or the D33 outcome
  follows, and popup-summary.txt's first line follows as "Details: <that line>" (so `server-death: PeerDied` moves to
  the details); the dump line, the report path, the hand-in and public lines as before. Without such a line (no reason,
  an empty one, the key outside `[failure]` or in a later section, another key, an older proxy's folder) the popup is
  unchanged line for line. The dialog watcher's signature for the box (its first line) therefore names the server's
  reason after this change.

### 33.2 Tests [L, 2026-11-18 00:16-00:20 EST, workstation, branch `task-0511-popup-first-line`]

- `python -I proxy/tools/test_proxy_session.py --build <unused> --scratch C:/c5-64bit-work/0511/scratch/session --only
  unit`: session_unit 77/77 (9 new: `ServerReason` on 0342's server.log lines, a verdict without stamp or final
  newline, without the remote tail, the 600-byte and UTF-8 clip; negatives: the last verdict passed after an earlier
  FAILED one, no verdict line, srv_main's form, an empty reason, `result:` inside a line; `Write` puts the scrubbed reason
  under `[failure]` and writes no such line for a server.log without a verdict); suite 9/9. `--asan` (MSVC
  `/fsanitize=address`): 77/77, no AddressSanitizer report, the canary caught; suite 15/15.
- `python -I proxy/tools/test_vp64helper.py --scratch C:/c5-64bit-work/0511/scratch/helper`: 165/165 (11 new, case
  `reason`: 0342's fog reason first, the stage's line, "Details: " with `server-death: PeerDied`, the path; without
  popup-summary.txt the default summary undecorated; the 700-character clip; five negatives with the popup line for line
  as before; the `failed` verb (D33) with and without a reason). `--asan`: 328/328, no AddressSanitizer report.

### 33.3 Live: 0342's fog install-fault run repeated [L, 2026-11-18 00:24:15-00:26:31 EST, Goldberg instance c5h-b]

- Layout as 0342's (`fog-of-war.md` 7.3 Live (e)): 0463's take-2 pack (manifest.json SHA-256 88c83c85...), the proxy
  (26df4c94...) and vp64helper.exe (a70a461c...) built from f8e9f424b with this ticket's diff of the three files applied
  (schema hash 0x765d76dcdc4e55b2, the take's server.exe ed829bda...), the compositor (6d9d8096...) from the same tree;
  `fogcloudtile_dif_01.dds` renamed in the instance's EffectTextures.fpk (backup by SHA-256). Driver: 0342's
  `server/evidence/0342/s03_fault.py` with the inputs, owner and four facts changed (kept outside the repository with
  the run in `F:/c5-64bit-work/0511/run1`: `facts.json`, the report folder, the run folder's logs).
- 15 of 15 facts: 0342's 11 again (one fog LOUD line, server exit 5, nothing drawn or published, the EXE ended by the
  proxy after 122.6 s, a report folder without .dmp, config.ini back by hash, no side file, failed=1, the popup shown and
  closed by the claim's owner), and: report.txt's `[failure]` has `server's reason: fog of war: the cloud planes
  (c5r::fow::CloudRenderer: FOWBaseLayer, fogcloudtile_dif_01..03.dds) could not start: NotFound: fogcloudtile_dif_01.dds
  not readable from the install: NotFound: gamefiles: no file named 'fogcloudtile_dif_01.dds'; unrevealed plots cannot be
  hidden, no frame is drawn (design 5.5, D118, D125)`; the popup reads, line by line, "The game's 64-bit server stopped
  with this error: fog of war: ... no file named 'fogcloudtile_dif_01.dds'; ...", then "The 64-bit part of the patch
  failed, so the game was ended. Your config.ini is back ...", then "Details: The 64-bit part of the patch failed
  (server-death: PeerDied); the game was ended; ...", the report path, the hand-in and public lines; the dialog
  watcher's signature names the fog. Restored by SHA-256 (EffectTextures.fpk db76b3df... again), set-proxy
  `--original` rc 0, provision verify rc 0.

### 33.4 Not verified, open

- The popup window as drawn (the dialog watcher read its text; no picture). The `popup` verb (stock sessions) with a
  server's reason only offline (suite case `reason`), not live.
- The reason is the server's own text: it is plain where the server's text is ("no file named
  'fogcloudtile_dif_01.dds'"), and it carries the server's internal names (`c5r::fow::CloudRenderer`, `design 5.5, D118,
  D125`). Wording the server's reasons for players is the server's `Fail` texts' matter, not the helper's [I].
- A server that ends without a verdict (a crash: the server's minidump path, 22.6; killed from outside) gives no
  reason line; the popup is then as before. `exe-crash` and `dll-fatal` reports (written by the server before its own
  verdict) most likely have none either [I: not read in srv_exe_crash.cpp / srv_dll_fatal.cpp line by line].

## 34. No NULL game context after a failed boot: the report route on every path (ticket 0721, K30, 2026-11-24, account B)

Ticket 0721 (D118, D185, D223), from 0720's set-up crash (MC-B #12403, 2026-11-23 23:39 EST, c5h-new-b, build 26577625,
a `pack=0` layout laid without the composite while `vp64helper on` had set ServerRenderer = 1): the composite guard of
ticket 0624 refused the boot ("shell_composite.dll is missing"), `DllGetGameContext` returned NULL, and the EXE raised
0xC0000005 (a read of address 0) 11 ms later on the same boot thread.

### 34.1 Where the EXE reads through the NULL [C, disassembly by hand, addresses only (D83), 2026-11-24]

On build 4390913 (`<work folder>/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe`, llvm-objdump 19):
- At `0x4817A6`-`0x4817B2` the EXE looks up `DllGetGameContext` in the DLL's module (the name string at 0xA552F4 in
  `.rdata`, file offset 0x6544F4) through GetProcAddress's import slot 0x9D6410 and stores the result at 0x2DD201C.
  This is the pointer that section 17.4's `0x42C640` calls ("the DLL's game context [I: DllGetGameContext]"): now `[C]`.
- `0x66A390`, the EXE's context getter, reads that pointer; when it is NULL it stores 0, else it calls it and stores the
  result in the caller's out slot with **no check of the result**.
- The boot's caller calls the getter at 0x47B910 (out = a local), keeps the returned context in a stack slot from
  0x47B915, reloads it at 0x47B95A and at **0x47B95E reads the context's vtable** (the faulting read when the context is
  NULL), then calls context slots 123 (+0x1EC) and 122 (+0x1E8).
  (D83 edit 2026-11-25, 0772: the instruction sequences of these three places were replaced by this description.)
- Build 26577625 has the same instructions: `tools/exebuild/addr_match.py <26577625 copy> <4390913 copy>` maps 0720's
  crash address to `0x47B95E` (method bytes16, confidence high) and the reverse run maps `0x2DD201C` to that build's
  pointer (`.data`, xrefs 7/7, high); the disassembly there reads the same (the caller, and the getter followed from its
  call, load that pointer and store the result unchecked). Their 26577625 values are not
  written here (D202: that build's addresses stay out of git); recompute them with
  `python -I tools/exebuild/addr_match.py <4390913 copy> <26577625 copy> <file holding 0x47B95E>`.
  **Correction (2026-11-24):** the ticket's EXE offset "on 4390913" (redacted, D202) was 0720's address on build 26577625; on 4390913
  the site is 0x47B95E.

Stock behaviour is the same: any DLL whose `DllGetGameContext` returns NULL crashes the EXE there; the EXE has no
error path for it. No code write is needed or made (D228).

### 34.2 Every path that left the proxy with no context (before 0721) [C, proxy/src/proxy_rt.cpp at 6291274f1]

| Path | Layout / session | Before 0721 | Since 0721 (stage, code) |
|---|---|---|---|
| Boot(): run folder, server_exe missing, strict_allow unreadable, ServerRenderer = 0 in a D33 session (0624), the composite refused in a D33 session (0624; 0720's case), server_cfg_extra missing or bad, server.cfg unwritable, endpoint, CreateProcess, server exit before attach, attach timeout, kRt_Boot no return, the DLL's boot failed, wrong context kind, no wrapper | `pack=0` (development and test layouts), any session | NULL to the EXE, no report | the report route: `server-start` (`run-folder`, `server-missing`, `strict-allow`, `options-renderer-off`, `composite-refused`, `server-cfg-extra`, `server-cfg`, `endpoint`, `create-process`, `server-exit`, `attach-timeout`) or `handshake` (`boot-call`, `dll-boot`, the refusal codes, `context-kind`, `context-wrap`: the last two had no stage before) |
| the server's window guard refused (CIVBG_GUARD=1, a test switch) | `pack=1` or `0` | NULL (no fallback by design, 3504) | `server-start`, `guard` (no stage before) |
| the 32-bit DLL (`fallback_dll`) missing, LoadLibrary failed, or no `DllGetGameContext` export: `mode=off` / no ini / `failed=1` in a stock session; the pack check's fallback; a failed 64-bit boot's fallback; the EXE-build gate's 32-bit route | the product (`pack=1`) included | NULL; after the pack's fallback also the report and a `popup` saying "goes on as the normal (32-bit) game", then the crash | `fallback-dll` (`missing`, `load-failed`, `no-export`); a 64-bit failure before it is kept in `[failure]` as "before it, the 64-bit path failed: stage S, code C" |
| any of the above in a D33 session with `pack=1` | product | already no play (3523, 0624, 0635, 0588) | unchanged |
| the 32-bit DLL loaded and its own export returned NULL | any | NULL | **unchanged**: handed over as that DLL gave it (stock behaviour), one `boot: LOUD:` line |

In the shipped layout (`pack=1`) the composite case of K30 was already safe: a D33 session goes to NO PLAY (0624) and a
stock session never runs the composite guard. The product paths that still crashed were the 32-bit DLL ones (a partial
install or an antivirus quarantine of `CvGameCore_Expansion2_x86.dll`, also with the patch switched off).

### 34.3 The route (as built) [C]

- `DllGetGameContext` ends with `if (!ctx && !g_fallbackGet) BootNullNoPlay();`: no NULL of the proxy's own leaves it.
  `BootNullNoPlay` opens the log if needed, sets `server-start` / `boot` when no failure point set a stage, logs
  `boot: NO PLAY: the boot failed (stage S, code C) and there is no game context to return ... (ticket 0721); <session>`,
  claims `g_reported` and calls `ReportFailure(kind, sessionWhy, bootEnded=true)`, then `Fatal(3, "the boot failed and
  there is no game context to return (no play, D118, ticket 0721)")`.
- `bootEnded` makes the helper's verb `failed --pid <EXE pid> --config <config.ini>` in a stock session too (the D240
  route: the helper waits for the EXE's end; nothing restored or set; the popup offers "Remove 64bit patch"); the
  report's `d33` field stays the session's kind. report.txt's opening paragraph says the game was ended at the start;
  for `fallback-dll` it asks for a new installation.
- `BootOff`'s three failures call `FallbackFail` (stage `fallback-dll`; the earlier failure kept; the popup summary
  "Details: the game's own 32-bit DLL ... (<file name> beside the patch's DLL), could not be loaded (fallback-dll:
  <code>)": the file's name only, no folder). The two callers that reported after `BootOff` (the pack fallback and the
  EXE-build gate) report only when the DLL loaded (`g_fallbackGet`); otherwise `BootNullNoPlay` reports once.
- `vp64helper failed` with stage `fallback-dll`: its own first line ("The game's own 32-bit DLL ... could not be loaded,
  so the game was ended before it could crash. The installation is partial or damaged ...: install the patch again.
  Nothing was changed."), nothing restored or set, no Remove button (the next start needs that DLL with the patch on or
  off; the remove choice is ignored), helper.log "stage fallback-dll: nothing restored, nothing set, no Remove button".
- `g_keepBootErr` is now set for `pack=0` boots too, so a failure point without a stage still gives its log line as the
  report's detail.

### 34.4 Tests [C]

- `proxy/tools/test_proxy_rt.py` (loader and guard sections): every former `null` case now drives the loader's
  `ended` scenario and asserts `check_report_route`: exit code 3, no `loader: PASS`, no "returned NULL", the NO PLAY line
  with its stage and code, exactly one report folder under the case's own `VPPROXY_TEST_CONFIG_INI` folder in
  `--scratch` whose report.txt names them, the helper's start tried (no `vp64helper.exe` in these layouts: "does not
  exist"), FATAL: no-server, cfgextra-missing, cfgextra-bad, off-missing (`fallback-dll`, `missing`), strict-missing,
  guard-fake-pass (`handshake`, `boot-call`), guard-veto (`server-exit`), guard-report (`guard`), shell-missing-d33
  (0720's set-up, `composite-refused`). New negative `off-nullctx`: a stand-in 32-bit DLL built with
  `/DSTANDIN_NULL_CONTEXT` (`rt_tests/fallback.cpp`) returns NULL: handed over (`loader: PASS` in the `null` scenario),
  the LOUD line, no report.
- `proxy/tools/test_proxy_pack.py`: `pack=1` no pack and no 32-bit DLL (`fallback-dll`, `missing`, the pack's `no-pack`
  kept, the summary names the file only) and `pack=0` server missing (`server-start`, `server-missing`).
- `proxy/tools/test_proxy_session.py` (fake helper beside the proxy): `pack=0` server missing in a stock session (the
  full report check, helper verb `failed --pid ... --config ...`) and `fb-missing` (`pack=1`, stock session, changed x64
  DLL and no 32-bit DLL: one FALLBACK try, then `fallback-dll`).
- `proxy/tools/test_vp64helper.py`: stage `fallback-dll` with the choices close and remove (its words, nothing changed,
  no Remove button); `fallback-dllx` among the look-alike stages that keep the generic words.
- **Found on the way (2026-11-24):** `test_proxy_pack.py`'s loader cases ran without `VPPROXY_TEST_CONFIG_INI` (only the
  modpack case set it, then removed it), so every fallback case since issue 3521 wrote its error report into the real
  `Documents\My Games\Sid Meier's Civilization 5\VP64\reports` of whoever ran the suite. The workstation's folder held
  197 report folders on 2026-11-24 00:25 (stages seen in three: `pacing`, `server-death`) [L, folder listing; the
  writers were traced on 2026-11-25 by ticket 0737, next bullet; this line said "not traced" before]. The suite now sets a config.ini under `--scratch` for its whole loader
  section; `test_proxy_rt.py`'s cases that report set one per case. Other suites that end on a report without it (the
  shell section's `shell-fatal*` cases of 0697, `death` cases elsewhere) were not traced [I].
- **Writers traced (ticket 0737, 2026-11-25 00:35, workstation, read-only listing of each `report.txt`'s `[exe] module:`,
  `stage:` and the proxy image path of `[patch]`):** the real folder held 262 report folders. 129 `loader.exe` (stages
  `pack` 88, `server-start` 33, in sets of 8 + 3 or 16 + 6 per suite run; 2026-11-14 03:42 to 2026-11-17 16:39; images under
  the `test_proxy_pack.py` / `test_proxy_rt.py` scratch folders of 3521, 3522, 3523, 0186, 2600 W2606, 0492, 0487): the
  loader cases fixed by 0721 above. 72 `hutest_host.exe` and 66 `civ5headless.exe`, all stage `pacing` code
  `install-exe` (2026-11-20 22:39 to 2026-11-25 00:20; images under the hutest `--out` and `run_proxy_game.py --run`
  folders of the tier suites cand1/cand2/cand4/cand5/cand6, 0626, 0630, 0639, 5015, 0673, 0681, 0691, 0693, 0697 and
  0314, which alone wrote 9 + 50 on 2026-11-24 17:16-18:03): the AI-pacing check (`proxy_pacing.cpp` GlueReport) fails
  at the install on any host that is not a known EXE build and writes its D118 report through `ProxyReport::Root`, and
  neither harness redirected `ConfigIniPath`. 3 `CivilizationV_DX11.exe` `server-death` (2026-11-22 18:23 to
  2026-11-23 23:27, image in the real MODS folder): the owner's own games, not a test [L].
  Fixed by 0737: `build_hutest.py` gives each run folder its own Documents (`C5_DOCUMENTS=<run>/Documents`,
  `VPPROXY_TEST_CONFIG_INI=<run>/Documents/My Games/Sid Meier's Civilization 5/config.ini`, not written), and lists the
  real player folder (VP64/reports, Logs/VP64/run, the folder, VP64Options.ini's size and time) before and after, read
  only: exit 1 when a new entry there names `--out`, a warning when one does not (`test_documents_redirect.py`, 9
  offline checks). `run_proxy_game.py` sets only `VPPROXY_TEST_CONFIG_INI=<run>/proxy/Documents/...` (its server keeps
  reading `C5_DOCUMENTS` or the real Documents for the mods database, `MODS` and `GraphicsSettingsDX11.ini`, which a
  change of `C5_DOCUMENTS` would alter: reads, not writes, left as they were [I]). Verified [L, 2026-11-25 00:45-00:48,
  the pins-0.1b source 1b87dd185 and this branch's proxy]: hutest PASS (65 checks), `--break` PASS (20 expected), `--scenario all`
  PASS (19), a 3-turn `run_proxy_game.py --mode proxy` PASS; their pacing reports were written under the run folders
  and the real folder still held 262. A hutest run without the fix needs no rerun to show the defect: the 72 folders
  above are it. The pacing check reporting on every non-EXE host is by design (one report per proxy load) and stays.

### 34.5 Results

- **Offline [C, 2026-11-24 00:52-01:00, workstation, `proxy/evidence/0721/suites_0721.sh`; build
  `C:/c5-64bit-work/0721/server-base` = `server/build_server.py --config release` from the pins-0.1b source 1b87dd185
  and this branch's proxy]:** test_vp64helper 228/228, test_proxy_pack 73/73, test_proxy_session 196/196,
  test_proxy_rt 232/232 (every section). A first run (00:40-00:48) failed only on the test's own wording check (it
  looked for "so the game was ended at once", which the `fallback-dll` paragraph does not say); fixed in the check.
  On 6291274f1's proxy the route checks cannot pass (it returns NULL there, so the loader's `ended` scenario prints
  "DllGetGameContext returned" and exits 1, not 3) [I: that negative control was not run; the live check's before is
  0720's crash].
- **Live [L, 2026-11-24 00:57-01:05, c5h-new-b, build 26577625, hidden, MC-B's grant #12463,
  `proxy/evidence/0721/live_0721.sh`]:** 0720's set-up exactly (`server/evidence/0600/lay.py prep` without
  `--composite-dll`, `helper on`: config mask 0xFFEC3F67, ServerRenderer = 1; `frontend.py prepare --mods cp,vp,eui`,
  no save; the proxy built from this branch, `repo 096a77e50 dirty=1`, sha256 0711ca62...). The proxy's log, 0.037 s
  after its start: the composite REFUSED ("shell_composite.dll is missing"), `boot FAILED: [Renderer] ServerRenderer = 1
  in a D33 session ...`, `boot: NO PLAY: the boot failed (stage server-start, code composite-refused) and there is no
  game context to return ...`; `report: written to ...\VP64\reports\20261124-005900` at 0.197 s; `helper: started
  "...vp64helper.exe" failed --pid 40020 --config ...` at 0.369 s; `FATAL: ... exit code 3` at 0.388 s. The EXE (pid
  40020) ended with **exit code 3, no crash marker, no crash folder** (the dialog watcher's tail; 0720's pid 54012 on the
  same instance and set-up ended 0xC0000005). The helper's log: "stage server-start: the patch stays on (D240); the popup
  offers Remove 64bit patch", "the EXE ended" 12 s after its start (00:59:00 -> 00:59:12; the EXE's own end after
  `TerminateProcess` took that long, cause not traced [I]), nothing restored or set, "popup shown". The popup
  (`VP64HelperPopup`, cloaked, `seen_by_art` false): "The 64-bit part of the patch failed, so the game was ended. The patch
  stays on ...", then the composite reason (0624's summary), the report's path, the hand-in lines; buttons Open folder |
  Remove 64bit patch | Close; closed by `cli.py dismiss --button Close --patch-popup` (exit 0). No picture taken (the
  watcher read the text). report.txt: the new paragraph ("failed at the start of the game ... the game was ended at
  once. The patch stays on ..."), `stage: server-start`, `code: composite-refused`. Restore: `lay.py restore` run before
  `frontend.py restore` ended with exit 7 (its closing `provision.py verify` fails while the front end is prepared, as
  runbook 3.7's trap says); after the front end's restore it ran again: `restore ok`; `verify --quick` 0, `leftovers`
  clean. The driver now restores the front end first.

### 34.6 Not verified, open

- The product layout's own new path (`pack=1`, the 32-bit DLL missing: stage `fallback-dll`) live: offline only
  (test_proxy_pack `nofallback`, test_proxy_session `fb-missing`, test_vp64helper's popup case); the live check ran the
  `pack=0` route of 0720's set-up (`server-start` / `composite-refused`).
- The `vp64helper failed` popup text for `fallback-dll` as drawn: `--no-ui` only.
- `load-failed` and `no-export` of `fallback-dll`: set in the code, no test makes LoadLibrary fail or a DLL without the
  export (`missing` is tested).
- Why the EXE took about 12 s to end after the proxy's `TerminateProcess` on the boot thread [I: not traced].
- Build 26577625's addresses of 34.1 are recomputable but not recorded (D202); the ticket text and 0720's note on its
  branch name that build's RVA as written by them (redacted on main, D202).

## 35. Upstream master's instances cross as instances; the stale server-value stub (ticket 0314, 2026-11-24, account A)

Branch `task-0314-proxy-lua-userdata`; scratch `C:/c5-64bit-work/0314/`; scripts and copied outputs in
`evidence/0314/`. Builds (all `--config release`): master = the master port `x64-release-master-ba8aafdc-r2` 76b3a5f70
(`build_all --unlocked --no-gate` on master's test database, DLL `63493cd3...`, as 0308/0313); pin = deps/dll.lock's
1b87dd185 (`C:/c5-64bit-work/keep/pins-0.1b`); server and proxy from this branch, and as the before arm main 43fa30aaf's
on the pin (`git archive`, `--repo-commit`).

### 35.1 The stale server-value stub: cause and fix [C Lua 5.1 lgc.c and the bridge's code; L hutest]

0308's intermittent failure on master (checklist 10b: "Game" and `Map.GetPlotByIndex(i)` arrived as functions in a new
host thread, 1 of 8 runs) is the proxy's sref cache (`PushSRef`: one engine value per (channel, sref id), a weak-valued
registry table) handing back a function stub for an id the server had released and reused:

1. Every stub the proxy makes is a full userdata or a C closure around one (`SRefFuncThunk`, upvalue 2 = the userdata
   whose `__gc` queues the id's `kRt_LuaRelease`). A userdata's environment is the environment current when it was
   made: on the headless host's threads (`HHScriptSystem::CreateLuaThread`: globals `{StateName = ...}` with `__index =
   _G`, the DLL's libraries registered inside `lua_cpcall`) that is the thread's own globals table, which holds the
   thread's globals, function stubs among them.
2. When such a thread is dropped, Lua 5.1's `atomic` (lgc.c) separates every unreachable finalizable userdata and marks
   them and all they reach (`marktmu`, then `propagateall`) BEFORE `cleartable` clears the weak tables. Through the
   userdata's environment the function stubs are reached, so they stay in the weak cache for that cycle, while their own
   `__gc` userdata are finalized in the same cycle (release queued). (Finalized userdata themselves are cleared from weak
   values in 5.1, `iscleared`; closures are not.)
3. The next `kRt_LuaCall` flushes the releases; the server's `luaL_unref` puts the slots on the registry's free list,
   and `luaL_ref` hands the last freed slot out first. The server's next values reuse those ids, and the proxy's cache
   returned the old, finalized function stub for them, whatever Lua type the server sent.

On 0308's builds every DLL function was such a function stub (before 0313), so each host thread's environment held
hundreds, and the next thread's `kRt_LuaRegister` (Game first) or the next plot met them. Since 0313 most functions cross
as kLCFunc and since this ticket instances and statics as kLInstance, but iterators (`Player:Units()`) and Fractal's
values remain server values: the defect is fixed on its own, not by making it rarer.

Fix (`proxy_lua.cpp` `PushSRef`, `SRefStubLive`): a cache hit is used only when the stub is alive (its userdata's
magic, which `SRefGc` clears at the finalization) and of the Lua type the server sent; a finalized stub is replaced by a
new one (counted, the first 20 logged: "a finalized server-value stub for id ... replaced by a new stub (issue 0314)");
a LIVE stub of another type (the server reusing an id the proxy never released) is counted and logged LOUD and replaced
too. Both counts are in vpproxy.log's `local: Lua bridge (issue 0314)` line (with `lua_mirror` on).

The stress check (hutest `lua_sref_stress_ran`, `lua_sref_no_stale_stub`; `hutest_host.cpp` `StaleStubRound`): 40
rounds of: a thread made as the headless host makes them; its chunk sets 32 globals to server function stubs (the fake
DLL's `HtSRef.Fn()`, a C closure of two upvalues) and calls two; the thread is dropped; ONE `lua_gc(LUA_GCCOLLECT)`;
then `HtSRef.Uds(256)` returns 256 new server userdata, each checked to arrive as a userdata.
- main 43fa30aaf's server and proxy with this hutest: **33 values arrived as another type (functions), the first in
  round 1** [L 2026-11-24 17:20, `evidence/0314/mainpin.hutest.log`]: the failure reproduced on purpose, every run.
- this branch: 0 wrong over 40 rounds, on the pin's and on master's build; the proxy replaced 66 finalized stubs (22
  logged lines, "a function, the server sent a userdata") and met 0 live mismatches [L 17:16-17:30].

### 35.2 Master's instance form crosses as instances [C]

Master (#13489, `Lua/!CvLuaApiInstanceTemplates.h`, `Lua/CvLuaSupport.cpp` at 76b3a5f70): the registry's
`vpdllinternals[<tag light userdata>]` = {`__hint` = `TypeGlobals<T>::TypeName()`, `__meta` = the metatable (`__index`
= the function table, `__metatable` = false, dynamic types `__gc`), `[light userdata object]` = its userdata}. Statics
(Game, Map) are one zero-size userdata global each; a dynamic instance is a 32-byte `LuaTaggedAutoPointer` {DWORD tag;
AutoInvalidatedPointerBase {obj, prev, next}} (x64: obj at offset 8), one per object and Lua state, kept in that table
(strong values) and invalidated (obj NULL) by the object's destructor through `m_PointerList`. The DLL's `GetInstance`
accepts only such a userdata with its tag (`CheckFromStack`), so the pin's `{__instance = p}` table would not do.

- **Server (`srv_remote_lua.cpp`).** `LearnMasterTypes` (after the scratch state's registration, before any encode)
  reads every `vpdllinternals` entry with a `__hint` and a `__meta` this bridge rebuilds (`__index` a table, no
  metamethod but `__index`, `__gc`, `__metatable`; any other form stays a server value, logged) into
  `masterByMt`/`masterByName` (name = `PinTypeName(hint)`: PlayerBase -> Player), and the statics (global zero-size
  userdata with such a metatable; registry refs). `EncodeMaster`: a static -> `kLInstance(name, 1)`; a dynamic instance
  -> `kLInstance(name, obj)` only when its size, tag and obj are right AND `vpdllinternals[tag][obj]` is this very
  userdata (so the layout is never trusted alone; else, and for an invalidated one, a server value as before).
  `DecodeMaster`: a static -> this state's own global; a dynamic one -> the DLL's cached userdata when it is live for
  that object; else (the engine got it on another channel, whose scratch state is another Lua state) `ResolveFrame`
  identifies the object through a temporary `LuaTaggedAutoPointer` that is not linked into the object's pointer list
  (self-linked, no metatable, so no finalizer) with the DLL's own getters under a structured-exception guard (Unit and
  City: GetID, GetOwner, then `Players[owner]:GetUnitByID / GetCityByID(id)`; Plot: GetX, GetY, then
  `Map.GetPlot(x, y)`, as ticket 0576's guard does at the pin), which pushes and caches the DLL's linked userdata; failing
  that, the DLL's invalidated userdata for that address (the call gets the DLL's own "Invalid instance!"), else nil with
  a LOUD line. Area, Deal, League and TeamTechs have no resolver. `kRt_LuaDescribeType(name)` of a master type answers
  the `__meta.__index` members after a first member `__vpproxy_form` = "userdata". server-report.txt: "Lua:
  master-form instances (issue 0314): sent N (statics N), not confirmed by the DLL's cache N; received N from the cache,
  N not live in it: N found again by identity, N passed invalidated, N nil".
- **Proxy (`proxy_lua.cpp`).** `MasterForm(name)` (the description's first member, kept per type once it has members),
  `PushMasterInstance`: an engine userdata `InstUd` {magic, token} per token and global state (weak-valued registry
  cache: identity as master's), metatable per type and state (`PushMasterMeta`: `__index` = the function table of
  DLL-function thunks from the description, named `Type.Key` for the mirror; `__metatable` = false; a marker key).
  `Enc` sends such a userdata as `kLInstance(name, server pointer)`; `ArgsSimple` accepts it as a mirror key (the pin's
  instance tables' rule). The aliases `Team` and `Player` (LuaTypes tables, CvLuaSupport.cpp:69-74 at the pin) are not
  set for master-form types (master's DLL sets neither), and not after a registration that brought no global (master's
  `!VPDLL` thread, 0308: the server knows no type then). A type description without members is no longer kept
  (`TypeDescription`): the first round of this ticket kept the empty answer the alias step got during that empty
  registration and made every Player instance a table without methods ("attempt to call method 'IsEverAlive'"), 1
  host scratch error per verify leg [L 17:21, `runs/master/verify` of round 2].
- **hutest.** The fake DLL registers City (GetID, GetOwner), Player (hint PlayerBase; GetCityByID), the static `HtMap`
  and `Players` in master's form, `HtMaster.City()`, `HtMaster.Forget(inst)` (drops the server state's cache entry, as
  if that state had never pushed the object), and master-form `Push/GetCvCityInstance`. Checks: `lua_master_run`,
  `_static`, `_instance`, `_identity`, `_method_is_dll_function`, `_resolve` (after Forget: the server's log "a City
  instance ... this state had not seen ... found again by its identity (505, 6)"), `_get_instance`, `_push_instance` (the
  engine passes the token to `GetCvCityInstance`, and `PushCvCityInstance` of the returned city gives the same engine
  userdata). `--break` (GetID + 1) adds `lua_master_instance`, `lua_master_resolve`, `lua_master_get_instance` to
  `BREAK_FAILS` (20 now).

### 35.3 Results [L, 2026-11-24 17:16-18:04 EST, workstation; `evidence/0314/verify-legs.tsv`, `pverify-legs.tsv`]

Checklist step 12's verify leg (Tiny `continents-tiny-fixed-seed`, turns 0-50, `strict=1 local_answers=1
local_verify=1 test_slot_poll=4 lua_mirror=1`, `--fail-on-unexpected`); master with master's test database, the
5.4.8 game folder (the default) or master's `testdata` game folder (`--game-dir`); the pin with the 5.4.8 cdrift
database.

| | pin, main 43fa30aaf | pin, this branch | master, 0313 (section 32) | master, this branch |
|---|---|---|---|---|
| verify leg | PASS | **PASS**, host Lua errors 0 | PASS | **PASS 21 of 21 with each game folder** (20 in a row each, interleaved, plus round 3's), host Lua errors 0, scratch errors 0 |
| kRt_LuaCall | 3,113,308 | **3,113,308** | 3,123,179 | **3,128,059** |
| kRt_LuaIndex | 15,175 | **15,175** | 701,672 | **15,175** (= the pin's; 0308: 3,112,478) |
| calls on the server-ref path | 15,237 | 15,237 | 15,237 | **15,237** (iterators, Fractal: the same rows as the pin) |
| `proxy-lua-calls.tsv` rows / names | 149 / 3,585 | 149 / 3,585 | 156 / 3,598 | 156 / 3,598 |
| master-form instances sent / statics / received from the DLL's cache / not live | - | 0 | - | 267,851 / 30 / 2,342,379 / **0** (no resolve was needed: no instance crossed channels in this leg) |
| stale stubs replaced / live mismatches | - | 0 / 0 | - | **0 / 0 in each of the 42 legs** |
| hutest / `--break` | (this hutest: 65 checks, **5 failed**, the negative arm, 35.1) | **65 / 0**; break 20 expected | 55 / 0 | **65 / 0**; break 20 expected |
| test_proxy_rt | 235/235 | **235/235** | 107/107 | **235/235** |
| UI-Lua mirror with a purity file: hits verified / mismatches | 1,198,941 / 0 | **1,198,941 / 0** | 3,305 / 0 | **1,198,311 / 0**, both game folders |
| getter mirror, same runs | 1,300,212 / 0 | **1,300,212 / 0** | 47 / 0 | 48 / 0 |

- Wall times per leg (indication only, COMMON 23): master 25.2-39.1 s (median 28.1) with the 5.4.8 folder, 24.6-46.1 s
  (median 28.0) with master's; the pin 31.2 s, main's pin build 32.6 s. No speed is claimed.
- `kRt_LuaCall` on master is 14,751 above the pin's (3,128,059 against 3,113,308): master's API calls more functions
  (0313 measured 3,123,179 with stubs for instances) [I: not broken down].
- `Push*Instance` / `Get*Instance` (contract D) do not occur in the headless verify leg (`vpproxy-calls.tsv` has no such
  row): the token the engine passes back is shown by hutest's master-form City only (`lua_master_get_instance`,
  `lua_master_push_instance`) through the real proxy and server.
- The mirror rows are the verify leg with `VP_PROXY_PURITY` (`evidence/0314/pverify.sh`, as 0313's pverify): the pin with
  its derived file (1b87dd185: 169 rows kept, 0 dropped), master with a TEST-ONLY file (master's derivation, 125 rows
  kept and 44 dropped because #13489 rewrote helpers the audit cites, plus the pin audit's 28 Lua names; 0313's
  `make_test_purity.py`). On master the UI-Lua mirror now answers about as often as at the pin (1,198,311 against 0313's
  3,305): instance arguments are a key again (`ArgsSimple`), every hit checked against the server (`local_verify=1`),
  0 mismatches. Not a release purity derivation for master (checklist step 11 is its own work).

### 35.4 Not verified, open

- No 32-bit EXE game on master through the proxy (the headless leg only, as 0313); `Push*Instance` live (above).
- The resolve by identity (an instance this state never pushed: the engine got it on another channel) ran only in
  hutest (`lua_master_resolve`, City); no verify leg needed it (0 misses). Unit and Plot resolvers are the same code
  path with other getters, not exercised. Area, Deal, League and TeamTechs have no resolver: such a miss gives nil and
  a LOUD line (counted as nil in server-report.txt).
- The `LuaTaggedAutoPointer` layout (x64, obj at 8) is read from master's header at 76b3a5f70; every send is confirmed by
  the DLL's own cache (0 unconfirmed in 42 legs), so a changed layout falls back to server values, not to wrong objects.
- A type whose `__meta` carries other metamethods (a future `CustomizeMTT`) stays a server value (logged).
- No MSVC `/fsanitize=address` run of the server or the proxy (COMMON 9): the changed code is the Lua codec, which has
  no unit harness of its own; hutest runs it in the real server.exe and proxy.
- GamecoreInit.lua in the engine VM (checklist 10b) is untouched (its own proposal). *(Done 2026-11-25, ticket 0315:
  section 36.)*

## 36. Upstream master's GamecoreInit.lua on the proxy route: GameCore, vpdllinternals, vputils, gc_post (ticket 0315, 2026-11-25, account A)

Branch `task-0315-proxy-lua-master`; scratch `C:/c5-64bit-work/0315/`; scripts and copied outputs (user paths scrubbed)
in `evidence/0315/`. Builds (`--config release`): master = the master port 76b3a5f70 with 0314's `build_all` (DLL
`63493cd3...`, `C:/c5-64bit-work/0314/master-deps`, read only); server and proxy from this branch, and as the negative
arm main 717d58964's (`git archive`, `build_main.sh`); the pin from `C:/c5-64bit-work/keep/pins-0.1b`.

### 36.1 What a 32-bit master game does [L, c5h-b, EXE 4390913, 2026-11-25 00:54-01:00 EST]

Arm 1: the 32-bit master `-ia` DLL of 3515 (`D:/c5-64bit-work/3515/i0ship-8be73b6f-ia`, `388A2D2B...`) by
`provision.py set-dll`, master's CP, VP and EUI-compatibility folders from the release driver's `testdata` stage of
Release-5.4.9 (`C:/c5-64bit-work/0314/testdata/mods`; its CP `.modinfo` imports `Gamecore/GamecoreInit.lua` and
`GamecorePostProcess.lua`) laid out as 3515's s13 did (`evidence/0315/mods.py`), a new Tiny Continents game (3 AIs, 4
city-states, `prep.py`), one turn, `probe.lua` in the `InGame` context (`evidence/0315/arm1.probe-ingame.txt`):

- **The EXE sets `_G.GameCore`** (`REGISTRY._LOADED._G.GameCore` is a table, and `GameCore.Players` is the very
  `Players` table of the UI contexts). 0308's "`_G.GameCore` is never set ... [I] the same line would fail in the EXE"
  came from the headless host (civ5-headless sets no `GameCore`), not from the EXE: upstream's code works in the EXE.
  The EXE's strings `_GameCore`, `GameCore`, `__index` next to "Failed to register game data." (file offsets
  0x2664E3-0x26654C of the DX11 EXE copy the `render_exe` setting names, dated 2026-11-09, so build 4390913 [I]) belong
  to that registration [I: strings only; the function was not decompiled].
- Lua.log has exactly these GamecoreInit lines (timestamps left out): `!VPDLL: GAMECOREINIT.LUA!`, `!VPDLL: Received
  event: gc_reset`, `!VPDLL: Received event: gc_post`, `!VPDLL: POSTPROCESS!` (the mod activation: NotifyDatabaseCached,
  then the front end's registration), then at the game start `Resetting lua gamecore api before LoadScreen context!`
  (the `Threads` listener), `!VPDLL: Received event: gc_post`, `!VPDLL: POSTPROCESS!`. No error: `postinit` and
  `do_gamecore_post` run through. One `gc_post` per engine `RegisterScriptLibraries`; there is none at
  `CreateLuaThread("Map Script")` (map generation lies between the two) [L, also the proxy arm's vpproxy.log: no
  `RegisterScriptLibraries` between `CreateLuaThread(Map Script)` and the LoadScreen registration; why the EXE
  registers in `!VPDLL`'s new thread but not in Map Script's was not looked into].
- In `InGame`: `vputils` a table of 6 functions (`GetProdTurnsForUnit`, `HelloWorld`, `SetFaith`, `SetFaithT100`,
  `hasGamecoreLock`, `vpnewproxy`), `vputils.HelloWorld()` = "Hello World!"; `vputils` in 153 of the 155 environments of
  `_G.Threads`; `GameCore.LuaTypes` with the 11 types (Area, City, Deal, Game, League, Map, Player, Plot, Team,
  TeamTechs, Unit); `REGISTRY.vpdllinternals` keyed by the 11 light-userdata tags and the 11 hints `postinit` adds;
  `Players.Majors()` = 0,1,2,3, `Players.Minors()` = 22,23,24,25, `Players(true)` = both, `tostring(Players[0])` =
  "Player#0(leader name)", `#Players[0]` = 0 (GamecorePostProcess's `__tostring` and `__len`). The PLYR entry's hint is
  `Player` (CvLuaPlayer's implementer is `CvLuaDynamicInstance<CvLuaPlayer, CvPlayerAI>`), so `vpreg.Player` exists.

### 36.2 What main's bridge did with it [L, arm 3, main 717d58964 on master, the same instance and setup, 01:23-01:50]

`evidence/0315/compare-arm1-arm3.txt`, `arm3.*`: both `gc_post` lines are followed by `!VPDLL: GamecoreInit.lua:31: bad
argument #1 to 'pairs' (table expected, got nil)` instead of `POSTPROCESS!`, and in `InGame` `vputils` is nil (0 of 155
environments), `GameCore.LuaTypes` empty, `GameCore.Player` nil, `vpdllinternals` nil, `Players.Majors` nil, `Players`
without `__call`. Why [C, `srv_remote_lua.cpp` at main]:

1. `vpdllinternals` is in the server's scratch registry; the engine registry has none (line 31's `pairs(nil)`).
2. `RegisterSupportStuff(supportState)` sets `vputils` on the scratch thread S (its own globals table), and
   `RunScript` forwards only `LoadFile` to the engine thread T, so `pushUtils` copies T's nil.
3. `executeSupportEvent("gc_post")` runs inside the server's scratch registration (`RegisteredCtx`, once per channel),
   before the engine state the EXE asked for has a single global. In this flow the two channels' registrations fell on
   the EXE's two registrations, so the count matched by chance; a third engine registration (another game, a load from
   inside a game) gets no `gc_post` on main [I].

Also seen on main's build only: the harness's `frontend.py ingame` probe read `Game.IsPaused()` as unreadable (in the
probe state, not in `InGame`, where the same call answered) and `turns.py end` hung at turn 0; the game was ended in
game with `UI.ExitGame()` through the channel. Cause not looked into (not this branch's: arm 2 passed the same probe).

### 36.3 The change [C]

- **Server, `srv_remote_lua.cpp`.**
  - `RegisteredCtx` marks the service thread (`t_collect`) while the DLL's `RegisterScriptLibraries` runs on the
    scratch state. `GlobalIndex` (the `__index` of a DLL thread's globals) hands out an engine function fetched then as
    a `DeferredCall` closure (engine thread id, global name, the stub): a call with scalar arguments only is kept in
    `LuaCtx::postCalls` and returns nothing (master's `lua_pcall(supportState, 1, 0, 0)` asks for none); any other call,
    or one after the registration, goes to the engine at once as before (counted, logged).
  - Every non-empty `kRt_LuaRegister` reply carries two more reserved entries (the 0313 pattern): `__vpproxy_dll_types`
    = {[i] = {tag, hint, name}} for master's types (none at the pin) and `__vpproxy_post_calls` = {[i] = {thread, name,
    n, args}} for the kept calls (none at the pin). So the calls go with each engine registration, not with the scratch
    state's one.
  - `LoadFile(S, file)`: S's own globals (master: `vputils`) are set on T first, by `kRtE_LuaRunChunk` with an empty
    chunk (as a chunk the DLL loads on S, 2504), because in the EXE S is T. While they are encoded, `t_trustWrapShape`
    lets a closure of master's wrapper shape (one upvalue, a C function without upvalues, both in the DLL image) cross
    as kLCFunc before the wrapper is learnt: NotifyDatabaseCached runs before the first registration, and a server value
    works on its own channel only. `FuncId` keeps one id per (function, closure function) and gives none for the same
    function in another closure.
  - server-report.txt: "Lua: engine calls the DLL made during RegisterScriptLibraries (issue 0315): kept N, sent to the
    proxy with replies N, made at once N".
- **Proxy, `proxy_lua.cpp`.** The register hook keeps the two entries (never globals). After the globals and the
  Team/Player aliases are set: `MirrorDllTypes` makes the engine registry's `vpdllinternals` when `gc_reset` has removed
  it and sets one new entry per type, `[tag light userdata] = {__hint, __meta = the type's engine-side metatable}`
  (`PushMasterMeta`, 0314: the metatable every engine-side instance of that type has, so GamecorePostProcess's
  `__tostring`/`__len` reach them); then `MakePostCalls` fetches each global on its engine thread and calls it there with
  `lua_pcall` (no results; an error is logged, not raised, as the DLL ignores it). vpproxy.log: "N DLL types mirrored
  into the engine registry's vpdllinternals (issue 0315)" at each registration (first 8); with `lua_mirror=1` the report
  line "local: Lua bridge (issue 0315): vpdllinternals mirrors N; the DLL's registration calls made ... N (failed N)".

### 36.4 Results [L, c5h-b, 2026-11-25; `evidence/0315/`]

| | arm 1: 32-bit master, stock route | arm 2 r1: this branch, first build | arm 2 r2: this branch | arm 3: main 717d58964 |
|---|---|---|---|---|
| GamecoreInit lines against arm 1 (`compare_lualog.py`) | - | 7 of 7 equal | **7 of 7 equal** | 5 of 7 (`:31: bad argument #1 to 'pairs'` twice) |
| `vputils` in `InGame` / in `Threads` environments | table / 153 of 155 | table / 153 of 155 | **table / 153 of 155** | nil / 0 |
| `vputils.HelloWorld()` | "Hello World!" | "attempt to call a nil value" | **"Hello World!"** | - |
| `GameCore.LuaTypes` / `vpdllinternals` hints | 11 / 11 tags + 11 names | the same | **the same** | empty / nil |
| `Players.Majors()` / `Minors()` / `Players(true)` | 0-3 / 22-25 / both | the same | **the same** | nil |
| `tostring(Players[0])`, `#Players[0]` | "Player#0(...)", 0 | the same | **the same** | - |
| server: scratch errors, bad references | - | 2, 2 (`server value 0x01000005 of channel 0 used on channel 1`) | **0, 0** | 0, 0 |

- r1's failure: `vputils`' functions are master's wrapper closures, sent by `LoadFile` before the wrapper was learnt, so
  they crossed as channel 0's server values and the UI channel's calls found nil; r2 is the `t_trustWrapShape` fix.
  r2 also called `vputils.SetFaith(Players[0], n)` from `InGame` (a master-form instance argument): `GetFaith` read
  0 -> 7 -> 0 (put back).
- The only other differences between arm 1 and arm 2 r2 (`compare-arm1-arm2.txt`): the `Map Script` line count (603
  against 484: each arm is a new random game, so map and leader differ; ours by setup, not by the bridge) and the same
  VP Lua error in both (`(2) Vox Populi\Core Files\Overrides\CultureOverview.lua` fails to load: upstream's, as in
  3515). Against arm 3 also `TurnProcessing` (arm 3's turn was never ended, 36.2).
- `RegisterScriptLibraries` in the proxy took 93.7 ms and 19.9 ms (r2's two registrations; main 40.2 and 13.6 ms): 11
  type descriptions once per process, the mirror and GamecorePostProcess. Indication only (COMMON 23), not a claim.
- The offline checks (checklist step 12) on the pin and on master: 36.5.

### 36.5 Offline checks [L, 2026-11-25 01:57-02:05 EST, workstation; `evidence/0315/offline.sh`, with `C5_DOCUMENTS` in the work folder]

| | pin (1b87dd185), this branch | master, this branch |
|---|---|---|
| test_proxy_rt | **235/235** | **235/235** |
| hutest / `--break` | **65 checks, 0 failed** / 20 expected, PASS | **65 / 0** / 20 expected, PASS |
| verify leg Tiny 0-50 | **PASS**, host Lua errors 0 | **PASS** with the 5.4.8 and with master's testdata game folder, host Lua errors 0 |
| kRt_LuaCall / kRt_LuaIndex / `proxy-lua-calls.tsv` rows | **3,113,308 / 15,175 / 149** (= 0314's) | **3,128,059 / 15,175 / 156** (= 0314's) |
| engine calls kept / sent with replies | 0 / 0 | 5.4.8 folder 0 / 0 (no GamecoreInit.lua, so no `_eventfunc`); testdata folder 1 / 15 |

- In the headless host the testdata leg now prints `Received event: gc_post` 15 times, once per non-empty engine
  registration (the host registers the DLL in every `CreateLuaThread`; 16 registrations, the first the empty one of
  `!VPDLL`), each followed by line 30's "attempt to index local 'core' (a nil value)": the headless host sets no
  `GameCore` (36.1). Not upstream's and not the bridge's; a host that should run GamecoreInit.lua through would need a
  `GameCore` table as the EXE's.
- The pin is unchanged by construction (its DLL fetches no engine function during its registration, has no
  `vpdllinternals` and loads no file on a thread with globals of its own) and by the counts above.

### 36.6 Not verified, open

- hutest has no check of this path: its host does not model the engine's `CreateLuaThread` / `LoadFile` for a DLL
  thread, so a fake DLL cannot raise an event on its own engine thread during registration. The live arms are the test
  (`evidence/0315/`, runbook 2.6); a hutest check needs that host part first (a follow-up if the MC wants one).
- A flow with a third engine registration (exit to the main menu and a second game, or a load from inside a game) was
  not run live; the headless leg's 15 registrations each got their `gc_post`.
- `executeSupportEvent` reads `_eventfunc` from `supportState`, a scratch thread of the channel that ran
  NotifyDatabaseCached, also from the other channels' service threads (their first registration): that `lua_getglobal`
  touches another service thread's Lua state (pre-existing since 0308; now only the lookup, the call is kept).
- `t_trustWrapShape` rebuilds a closure of the wrapper's shape from its two C functions; a closure that changes its own
  upvalue would lose that state. Only the DLL thread's own globals before `LoadFile` take this path (master: `vputils`'
  six functions).
- No MSVC `/fsanitize=address` run (COMMON 9): the Lua codec has no unit harness of its own; hutest and the live arms run
  it in the real server.exe and proxy.

## 37. The UI's Lua calls into the DLL on the human's own turn: hover and select, product against stock (ticket 5043, 2026-11-25, account B)

Ticket 5043 (the Architect's draft T13, section 9 of the third speedup list,
`research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md`): every call of interface Lua into
the DLL crosses to the server; on the human's own turn, while the player hovers over plots and selects units, that was
never measured. Measurement only: calls per second, the main thread's share and the frame time, product against stock
with the same replayed input, and a go or no-go on answering the pure hot getters from the proxy's UI-Lua mirror
(section 17.6). Nothing is built; nothing in the product changes.

### 37.1 Method `[C, perf/evidence/5043/job5043.py, analyze5043.py, series5043.sh]`

- **Machine and save.** The laptop (the quiet test machine of D106; i7-7700HQ, Quadro P3000, quiet-testbed.md 12.1 and
  21.4), one quiet headed testbed job per run (`tb.py submit --quiet --headed`), each submitted only after the one before
  ended. Save `TS0438-H-T300` (Huge Continental Drift, turn 300, 0438's scenario save, SHA-256 `308a96bb...`), copied in
  by hash; CP + VP + EUI; the human seat as 5030 checks it (player 0 human, not an observer, its turn active, autoplay 0).
  The camera stays where the load puts it in both arms (no LookAt).
- **Arms.** `stock`: the EXE with the laptop instance `tb1`'s own 32-bit DLL, `3c2f47da...` = `i0ship-26234371-ia`
  (the batch-2 32-bit pin with the instance-aware instruments: the harness's Lua channel needs an `-ia` build, so the
  player's plain 5.4.8 DLL cannot be driven; exe-audio.md names the hash). The EXE is Steam build 4390913 on the laptop
  (`vpproxy.log`: `exe build: build 4390913 ... known and supported`). `product`: beta 4's candidate as the take laid it (`C:/c5-64bit-work/cand6/tier/layout/x64-1b87dd18-pdc259f0`:
  x64 DLL `4C5DA20E...` at fork 1b87dd185, server.exe `6c75d7fa...` and proxy from dc259f0; the composite `FC01CAFA...`
  from `build/shell-dc259f0`; the pack's `purity.tsv` `a6e3ee5f...`), laid by `tools/testbed/jobs/product_path.py`'s prep
  (proxy shell, vpproxy.ini `mode=on local_answers=1 local_verify=0 lua_mirror=0`, D33 with Overlay on) plus two things
  the prep does not lay and the installer does: `purity.tsv` beside the x64 DLL in `VP64\` and the shipped
  `shell_composite.ini` in place of the repository's with product_path's test keys (37.4); VP64Options.ini with the
  pack's `[Renderer]` and `[Performance]` sets. `idle`: the product arm with the same timeline and no input (the control
  for the server's per-function totals, which are written only at the process end).
- **Both arms.** `config.ini LoggingEnabled = 0` and `GraphicsSettingsDX11.ini WaitForVSync = 0` (fps_idle.py's reason:
  with 1 an on-screen window runs at the 60 Hz refresh) after the prep's backup, restored with it by SHA-256; `cli.py
  launch --timing` (no harness frame cap).
- **Timeline after the load** (20 s settle, the setup, 10 s still): window `idle` 30 s (nothing), `hover` 60 s (posted
  WM_MOUSEMOVE through `tools/hi/actions.move` with the focus guard's virtual cursor, along a serpentine of 6 rows x 40
  points over the client's middle, x 0.30-0.70, y 0.30-0.66 of the client, a move every 50 ms and a 600 ms rest every 10
  moves so plot tooltips open), `select` 60 s (a left click every 2 s through `tools/hi/actions.click` on the player's own
  units outside cities, up to 12, found by the EXE's camera read `tools/hi/uimap.camera_pixel`, read only; fixed client
  points if none is found), 2 s between windows. The same input in both arms: the same save, camera and client size.
- **Frame time, both arms.** A Lua handler on `Events.LocalMachineAppUpdate` (once per frame on the main thread, as
  `proxy/tools/live/frame_counter.lua`) keeps each frame's `os.clock()` difference in 1 ms bins while a window is open;
  read at the window's end through the Lua channel. Mean = sum / frames (exact to about 1 ms over the window), p50 / p95 /
  p99 from the bins (1 ms resolution). The same chunk runs in both arms and makes no DLL call.
- **Calls, product.** The shipped proxy's counter snapshot (counters=1 is the default; the named event
  `Local\VPProxySnapshot_<pid>` with a label file, `proxy/tools/proxy_snapshot.py`'s way), taken at each window's start
  and end at the proxy's next frame marker: `vpproxy-snapshot-methods.tsv` gives `rt:LuaCall` and every other method per
  window, `vpproxy-snapshots.tsv` the crossings by interface, direction and channel and the proxy's own frame times, and
  `vpproxy.log` after each snapshot the UI-Lua shadow lines (the 20 pure-listed functions with the most candidate calls:
  candidates and would-hits, cumulative; the window's are the difference).
- **Main thread's share** = the window's `rt:LuaCall` per second x the call's round trip on the EXE side
  (`vpproxy-calls.tsv`, seconds / calls; the session's mean, and product minus idle sessions for the input's own calls).
  `[I]`: a product of two measured numbers; the proxy keeps no time per window.
- **Statistics** (PERFORMANCE.md 2): mean, sd, median, min, max, 95 % interval per arm and window; product - stock with
  Welch's interval and the smallest difference the series could detect (80 % power); machine CPU and the busiest other
  processes every 2 s (`out/load5043.csv`). Grade A needs six interleaved runs per arm on the quiet machine.

### 37.2 Smokes `[L, laptop, 2026-11-25, jobs 20261125-035309-bc71 and the second smoke below; functional, one run each]`

- **bc71** (product, 03:53-04:01 EST, rc 0, every file equal to before after the restore): the load took about 3 min;
  the seat check passed (`ok|0|true|false|0|true|300`); purity loaded (`140 method keys, 29 Lua names; bound to the x64
  DLL 4c5da20e5a1e...`); a snapshot at each window's start and end. One run's readings: 53.1 / 54.8 / 55.5 fps (18.9 /
  18.3 / 18.1 ms) in idle / hover / select; `rt:LuaCall` 2 in the 30 s idle window, **1,155 in the 60 s hover window (19
  a second)**, 77,978 in the 60 s select window (1,283 a second; 30 clicks, about 2,600 calls a click); every crossing out
  on the ui channel 6.2k / 6.5k / 10.5k a second (mostly the EXE's own per-frame engine calls: `ICvPlayer1::IsHuman`,
  `ICvPreGame1::slotStatus`, `ICvPlot1::IsRevealed`, ...) and 30k local answers a second. Two faults of the job found and
  fixed: the harness's stop terminates the EXE (`terminate_reason: no exit hook registered`), so the proxy never ran its
  shutdown and wrote no `vpproxy-calls.tsv` (the EXE-side round trips): the product arms now exit in game by
  `UI.ExitGame()`; and at the load's camera only one of the player's 82 units outside cities was on screen (the selected
  one, so the 30 clicks all hit it): the camera now goes first to the player's densest group of units (`UI.LookAt` of
  the plot with the most such units within 3 plots), the same in both arms. Laptop load in the job: 52-58 % CPU of all
  cores, the EXE 159-190 % of one core, server.exe 134-145 %; Defender (`MsMpEng.exe`) at 5-27 % of a core in every
  window and `MemCompression` 10-28 %: a disturbance this series cannot remove (D41: no system setting).
- **1b7d** (product, 04:07-04:14 EST, rc 0, restored, `provision.py verify` 0): both fixes hold. The camera went to
  (37,38), 18 of the player's units within 3 plots; 9 click targets by the camera read. `UI.ExitGame()` ended the EXE and
  server.exe in 7.9 s through the proxy's orderly shutdown (`SetDLLIFace(NULL) forwarded`, server exit code 0), and
  `vpproxy-calls.tsv` was written: `rt:LuaCall` 504,655 calls, 2.449 s round trip on the EXE side over the session =
  **4.85 us a call** on the laptop (the workstation's 1.25-2.8 us of 17.7 were another machine and pin). One run's
  readings at that camera: 45.6 / 45.9 / 48.7 fps (22.0 / 21.8 / 20.6 ms); `rt:LuaCall` 2 / 5,687 / 237,129 in the
  idle / hover / select windows = 0 / **94** / **3,933 a second** (30 clicks on 9 units, about 7,900 calls a click), so
  about 0.05 % and 1.9 % of the main thread by calls x round trip.

### 37.3 The decision rule, written at 04:25 EST before the series' first result

Go on answering the pure hot getters from the mirror only if all three hold: (1) the product's mean frame time in the
hover or the select window is longer than stock's, with the 95 % interval of the difference above zero; (2) the pure
getters' crossing time in that window (their calls a second x the round trip) is at least a quarter of that difference
or at least 0.5 ms a frame; (3) the epoch mirror could answer a material part of those calls (would-hits / candidates
of at least 0.5 in that window, since a mirror that would hit 10 % saves 10 % of their time). Otherwise no-go, with the
numbers.

### 37.4 Found on the way: product_path.py lays no purity.tsv `[C, L, 2026-11-25]`

`tools/testbed/jobs/product_path.py` lays the product by `provision.py set-proxy --bin <bin> --proxy <dll>`, which copies
only `PROXY_VP64_FILES` (server.exe, the x64 DLL, lua51_x64, CvGameDatabase_x64, CvLocalization_x64) into `VP64\`. A
pack=0 proxy (the D28 layout these jobs use) loads its purity lists only from `purity.tsv` beside the x64 DLL
(`proxy_purity.cpp LoadBesideX64`), so every product_path job since the purity lists became data (issue 3503) has run
with **no local answer and no UI-Lua mirror candidate**: 0738's laptop product arms of 2026-11-25 log
`boot: purity: not loaded (pack=0): ... both purity lists empty, every call crosses (methods 0, Lua names 0)` and
`0 pure method ids` (`F:/c5-64bit-work/0738/laptop/20261125-021037-6730/out/vpproxy.log` lines 70-72). The player's
installer layout (pack=1) loads the pack's `purity.tsv`. So any frame-rate or traffic figure taken through
product_path.py measures a slower proxy than the one players run (how much slower on the human's turn: bc71 counted
about 30,000 local answers a second against 6,000 crossings, all of which would have crossed). This ticket's job lays
the pack's `purity.tsv` (`a6e3ee5f...`, bound to the x64 DLL `4c5da20e...`) after the prep and removes it before the
restore, and puts the shipped `shell_composite.ini` back over product_path's test keys (`tag_match=clear_bb`,
`trace_frames=0`, `lockstep_csv=1`; the shipped ini has `tag_match=` empty, `trace_frames=2`, `lockstep_csv=0`). The fix
of product_path.py itself (copy `purity.tsv` from `--bin` when present, or take a pack) is the MC's to ticket; not
changed here.

### 37.5 Results `[L, laptop, 2026-11-25 04:18-06:45 EST, 14 jobs, all exit 0; grade A for the frame times (6 interleaved runs per arm, quiet jobs, load recorded); analysed by impl-5043]`

Series `5043-laptop`: jobs 20261125-041813-bf07 (stock), -042605-3c63, -043349-fc07 (product), -044134-2c22 (stock),
-044920-77f6 (idle), -051604-a15d (stock), -054316-d918, -055133-e392 (product), -055903-3ed9 (stock), -060651-7a47
(idle), -061434-ed2a (stock), -062227-b412, -063007-ebee (product), -063740-ba74 (stock); paused 04:57-05:16 and
05:24-05:43 for 0685's laptop pass (one job at a time throughout, no other job beside one of ours). Every job restored
its instance by hash (`restored_equal_before` all true), human seat every time, 9 click targets every time, 30 clicks,
550 hover moves. Laptop: EXE build 4390913, client 3825 x 2087 physical px (the 1536 x 864 logical screen at 250 %),
`WaitForVSync = 0`, `LoggingEnabled = 0`; asset set: the `tb` instances' full Vibrant Textures (quiet-testbed.md 21.4,
D94). Load: 45-47 % CPU of all cores in the stock arm, 51-53 % in the product arms; busiest other processes
`MemCompression` up to 38 % and Defender `MsMpEng.exe` up to 32 % of a core, `System` 21 %, `RuntimeBroker` 17 %
(the runner's quiet check: `undisturbed=False`, foreign 0.17-0.21 cores in every job). Numbers: `perf/evidence/5043/`
(`analysis-laptop.{txt,json}`, `runs/<job>.json`, `layout-SHA256.txt`).

**Frame time** (the Lua clock; mean of 6 runs, sd in brackets; the proxy's own frame tracker agrees to 0.03 ms):

| window | stock | product (beta 4) | product - stock (95 %) | idle arm (product, no input, 2 runs) |
|---|---|---|---|---|
| idle 30 s | 16.45 ms (0.12), 61.5 fps | 21.97 ms (0.23), 46.0 fps | **+5.53 ms (+5.28..+5.78), +33.6 %** | 22.47 ms |
| hover 60 s | 16.04 ms (0.08), 62.5 fps | 21.60 ms (0.51), 46.4 fps | **+5.56 ms (+5.02..+6.10)**, p95 +7.5 ms | 21.72 ms |
| select 60 s | 15.29 ms (0.12), 66.0 fps | 20.70 ms (0.28), 48.5 fps | **+5.42 ms (+5.12..+5.71)**, p99 +29.7 ms, max 101 against 36.5 ms | 20.10 ms |
| window minus the same run's idle window, product - stock | | | hover **+0.03 ms (-0.37..+0.44)**, select **-0.11 ms (-0.46..+0.24)**; detectable 0.44 / 0.38 ms | |

- **The gap is there at rest.** Product is 5.5 ms a frame (34 %) slower than stock in the idle window, with 0 Lua calls
  into the DLL (2 in 30 s). Hovering and selecting add nothing to it that this series resolves (the
  difference-in-differences is zero within 0.4 ms). Frames get shorter from window to window in every arm, the idle
  arm included (22.47 -> 21.72 -> 20.10 ms with no input at all): a drift after the load, not the input. Against that
  no-input control the product's select window is 0.60 ms longer (2 control runs, 95 % -2.5..+3.7: an indication
  `[I]`), the hover window 0.13 ms shorter.
- **The click is a hitch.** Each click in the product costs one long frame: p99 54.8 ms against stock's 25.2, the
  window's longest frame 101 ms against 36.5 ms. A click makes about 7,800 `rt:LuaCall` and about 12,700 other crossings
  on the ui channel (16,424 crossings a second in the window against 6,131 at rest), all inside the frame that handles it.

**Calls and the main thread** (product, mean of 6 runs; per call: the `rt:LuaCall` round trip on the EXE side of the
input's own calls, product minus idle sessions: 249,428 calls, 1.200 s = **4.81 us**; the session mean 4.67 us):

| window | rt:LuaCall a second | main thread inside them | per frame | pure-listed candidates a second (mirror would-hits) |
|---|---|---|---|---|
| idle | 0.07 | 0.00 % | 0.000 ms | 0 |
| hover | **94** | 0.05 % | 0.010 ms | 50-73 (0) |
| select | **3,910** | **1.88 %** | 0.39 ms | 354 (2.0 % would hit) |

Every crossing out on the ui channel: 6,131 / 6,411 / 16,424 a second, and 25,750 / 26,051 / 28,325 local answers a
second (idle / hover / select): the human's turn is dominated by the EXE's own per-frame engine calls, not by UI Lua.
The Architect's inference (112,000 hover calls a second, about 0.3 s of main thread a second, from another project) is
not what VP's interface does here: hovering crosses 94 times a second (a plot tooltip handler of a few calls per rest,
plus unit reads near units), selecting 3,910 a second (the unit panel and EUI's unit lists on each click).

**The hot functions** (session totals of the server's `proxy-lua-calls.tsv`, product mean minus idle-arm mean = what
the 60 s hover and 60 s select windows added, 249,480 calls; PURE = in the pack's purity list, a mirror candidate):

| function | added calls | pure | server us a call |
|---|---|---|---|
| Unit.GetX / Unit.GetY | 51,150 each | no | 2.35 / 2.22 |
| Player.GetUnitByID | 37,702 | no | 2.36 |
| Map.PlotDistance | 34,067 | no | 0.99 |
| **Unit.IsHasPromotion** | **17,230** | yes (would-hits 422) | 2.35 |
| Game.CanHandleAction | 16,533 | no (engine state) | **15.08** |
| Unit.MovesLeft, Unit.MaxMoves, Unit.GetBuildType, Unit.GetMaxHitPoints, Unit.IsAutomated, Unit.GetActivityType | 3,060-3,580 each | no | 2.8-3.4 |
| **Unit.IsEmbarked**, **Unit.GetDamage** | 3,525 / 3,510 | yes (0 / 0) | 2.47 / 2.66 |
| **Map.GetPlot** | 1,104 | yes (object result: crosses always) | 3.89 |
| **Game.GetCorporationFounder**, Unit.GetDomainType, Player.IsMinorCivDisplayedQuestForPlayer, City.IsHasBuilding, City.IsFranchised, Game.GetActivePlayer | 771, 549, 532, 516, 360, 336 | yes | 0.9-3.4 |

Pure-listed functions are 30,003 of the 249,480 added calls (12.0 %); in the select window 354 of 3,910 a second
(9.1 %), and the shadow mirror would have answered 2.0 % of them.

### 37.6 Go or no-go on answering the pure hot getters from the mirror: **NO-GO** `[L for the numbers, I for the per-frame products]`

By the rule of 37.3, written before the results: (1) holds (the product is 5.4-5.6 ms a frame slower than stock in the
hover and select windows, intervals above zero), but (2) fails: the pure getters' crossing time is 0.005 ms a frame in
the hover window and 0.035 ms in the select window (354 a second x 4.81 us / 48.5 fps), under 1 % of the 5.4 ms gap and
far from 0.5 ms; and (3) fails: the epoch mirror would hit 2.0 % of them (the epoch moves between the panel's calls, as
17.7 found for AI turns). The whole of UI Lua's crossing on the human's turn is 0.39 ms a frame while selecting and
0.01 ms while hovering; the gap of 5.5 ms is already there at rest with no Lua call. Even a perfect mirror of every
pure-listed getter would save about 3.4 ms of a click's 101 ms longest frame (about 710 pure calls a click x 4.81 us),
and one at its measured 2 % hit rate about 0.07 ms. Leads for the MC (not measured here, `[I]`): the frame-time gap at
rest belongs to the frame-pacing work, not to UI Lua; the click hitch is mostly the non-pure part of a click
(Unit.GetX/GetY, Player.GetUnitByID, Map.PlotDistance: a Lua loop over the player's units, about 5,800 calls a click,
and Game.CanHandleAction at 15 us a call, about 8 ms a click of server time), which only a batched or position-fed
answer could shorten.

### 37.7 Not verified, open

- The stock arm has no no-input control: its window-to-window drift is assumed to equal the product's in the
  difference-in-differences (the product's idle arm drifts more, -2.4 ms by the select window against -1.3 with input:
  2 runs only).
- Stock's own time inside these calls (the in-process DLL) was not measured; the crossing's net cost per call is the
  4.81 us round trip minus that.
- The per-window pure-getter lines come from the proxy's top-20 list by cumulative candidates: a function hot only in a
  window but outside that list is missing there (the per-function table above, from the server, has all of them).
- One machine (the laptop), one save, one camera (the densest group of the player's units), the posted-input lane with
  the focus guard's virtual cursor (not real SendInput), the owner's [Renderer] set and the pack's [Performance] set.
- The stock arm's DLL is `i0ship-26234371-ia` (batch 2 with instruments), the product's x64 DLL the 0.1b pin 1b87dd185;
  the interface Lua (VP, EUI) is the same in both.

## 38. Mods reloaded after a game in one EXE session: the beta 4 hang at "Reload GameCore DLL" (ticket 0805, 2026-11-26, account B)

**The report** (a beta 4 player, relayed by the owner 2026-11-26): after a game, back to the main menu and the mods
reloaded in the same EXE session, the game hangs at `Reload GameCore DLL` in stopwatch.log; no new `Logs\VP64\run`
folder, so the proxy never boots a second time; a restart of the game works.

**What the EXE does at that step** [C, `../02-exe-internals/addresses.md` 0x47B1D0, LoadCvGameCoreDLL]: the same DLL
path as the loaded one (and the byte 0x2DD2A05 == 0) means no reload: `SetDLLIFace([app+0x1460])` and
`SetGameDatabase(app+0x944)` again on the context it holds, no `DllGetGameContext`, so no boot and no run folder by
design (path A). Another path means: the old context's `SetDLLIFace(0)` (the proxy's `Shutdown()`: the server ends, the
endpoint closes), `SetEngineUserInterface(0)`, the old engine utility object destroyed and freed, `FreeLibrary` of the
old DLL and `LoadLibrary` of the new one, a new utility object, `SetDLLIFace(new)`, `SetGameDatabase` (path B). A
stopwatch line is written when its step ENDS (`[time] , <step>, <seconds>`, `../sources/probe-logs/*/stopwatch.log`):
a hang inside `Reload GameCore DLL` leaves `Notify Localization Database Updated` as the last line.

**The two menu flows.** The stock MainMenu.lua calls `Modding.ActivateDLC()` whenever it is shown (not on a hot load),
which deactivates every mod (a reload to the base game's DLL: path B for the proxy's unload); the mods browser's NEXT
calls `Modding.ActivateEnabledMods()` (path B again: base DLL -> the proxy). The ModsMenu's BACK calls
`Modding.DeactivateMods()`. 0153 (section 20) ran this flow on 2026-11-13 (proxy 490bbfc3) without a failure. The
harness's MainMenu.lua (`vp-dll-dev` skill) skips `ActivateDLC` while the wanted mods are active, so after Exit to Main
Menu under the harness the mods stay active and NEXT with the same set is path A.

**38.1 Set-up.** 2026-11-26 13:08-14:36 EST, workstation, instance c5h-c (Goldberg, C:, profile mods548, EXE build
4390913), the beta 4 hotfix installer `VP64-Patch-0.1b4h1-std-only-for-VP5.4.8.exe` (SHA-256 99a406bd...; proxy
5cea6db8..., built from 482ce16be on DLL pin 1b87dd185) laid by its own installer over the player's VP 5.4.8 DLL
(262aee88...) and removed after (tree diff: no difference but the instance's own DLL put back; `provision.py verify`
ok). Driver `proxy/tests/reload_after_game/drv_0805.sh` (install, launch, menu, turns, shot, click, runs, dump, stop,
remove); image bases read with `proxy/tests/reload_after_game/modlist.cpp` (x86, read only); every menu step by clicks
on the game's own screens. Excerpts: `proxy/tests/reload_after_game/excerpts-2026-11-26.txt`; full logs, the two D119
reports and a cdb dump on `F:/c5-64bit-work/0805/` (s1-s4, reports, cdb4). A beta 4h1 layout was also laid on
c5h-new-a (26577625) at 12:37 and removed at 12:44 without a launch (instance granted to another ticket), then laid
there again for session 5 (14:44-15:14; removed: tree diff 0 differences outside the run outputs, verify ok). Session 5
ended by the main menu's EXIT (the harness's stop refuses a front end it cannot prove safe after a game was loaded:
"needs a decision ... quit it in-game").

**38.2 The hang did not reproduce [L].** Five EXE sessions, 13 proxy boots (8 of them re-activations in a running EXE), each with its own run folder:

| Session | Flow | Result |
|---|---|---|
| 1 (pid 30824) | harness menu, load a Tiny t18 save, 3 turns, Exit to Main Menu, MODS, NEXT | path A: NEXT with the set already active runs no activation at all (no `SetActiveDLCandMods` in stopwatch.log) |
| 1 | then BACK on the ModsMenu (`DeactivateMods`), the harness menu re-activates | the proxy unloaded (`detach: after the orderly shutdown`), second boot, new run folder, `SetActiveDLCandMods` 36.85 s, `Reload GameCore DLL` 1.94 s |
| 2 (pid 3408) | stock-acting menu (`frontend.py prepare --mods ""`): MODS, NEXT; a NEW Tiny game; 5 turns; Exit to Main Menu (the menu's `ActivateDLC` unloads the proxy: base DLL mapped); MODS, NEXT | second boot, new run folder, 35.93 s: the player's exact flow works |
| 3, 4 | the same with a loaded save; session 4 four boots in one EXE | every boot ok |
| 5 (c5h-new-a, EXE build 26577625, pid 58872, 14:45-15:05) | stock-acting menu; MODS, NEXT; LOAD the Tiny t18 save; turns by clicks; Quick Save; Exit to Main Menu; MODS, NEXT (after a save); BACK; tick (4a) Squads for VP, NEXT (a changed set) | every boot ok: three run folders, images at 0x67EF0000, 0x67E50000, 0x6F780000 |

So neither build, nor a save before the reload, nor a changed set (adding a mod) reproduced it here. Session 5 used no
Lua-channel request at all (every step by clicks and keys), so it is also the product-like control for 38.3.

**38.3 A crash after the reload, root-caused [C].** A LOAD GAME after the second boot ended the EXE in sessions 2 and 3
(and in 4 when repeated as below): exe-crash 0xC0000005, `access: execution at` an address in no module, on the
game-core thread, 10-25 s after the load began. The address is the FIRST proxy image's base + 0x750C0 (session 3: base
0x68960000, crash 0x689D50C0; session 2: 0x67AA0000 + 0x750C0 = 0x67B150C0; session 4: image 2 at 0x67A30000, crash
0x67AA50C0), and RVA 0x750C0 is `FuncThunk(lua_State*)` of `proxy_lua.cpp` (the C closure of every DLL function the
proxy puts into the engine's Lua; the 4h1 proxy's PDB, `cdb -z` on the DLL file). Session 1 did not crash because its
second image was mapped at the same base, so the stale pointer hit the new image's own FuncThunk. Session 4, with cdb
attached (`shell/evidence/0164/cdb_watch.py`): with no Lua-channel request during the load, no crash; with the
harness's `frontend.py in_game` probe polling during the load, the second-chance AV, stack (faulting thread):
`<Unloaded_CvGameCore_Expansion2.dll>+0x50c0` <- lua51_Win32 (three nested Lua calls) <- `lua_pcall` <-
`RunExternalLua` (proxy_luaexec.cpp, the current image) <- `lua_cpcall` <- the EXE <- `ProxyRtInt::BeforeUpdate`
<- `ProxyRt::Begin` <- `Proxy_ICvGame2::Update`. The mechanism:

- `ProxyLuaExec::OnGameDatabaseCached` creates one engine Lua thread `VP_LUAEXEC` per proxy load
  (`CreateLuaThread`) and never frees it, so the engine's thread table (`G.Threads`) keeps one per earlier image, each
  with the DLL's registration globals (`Game`, `Players`, ... whose functions are FuncThunk closures of that image).
- The Lua channel's runner (harness only: the channel answers requests only under `VP_LUAEXEC=1`) resolves the state
  "Main" as this thread's environment if it sees `Game`, else `_G`, else `InGame`, else the FIRST environment in
  `G.Threads` that holds a `Game` table. During a load the new thread's environment and InGame do not have `Game` yet
  [I: the crash came only from probes sent during the load, never after the game was up], so it can pick an old
  `VP_LUAEXEC` environment; the probe's `Game.IsPaused()` then calls the old image's FuncThunk.
- What runs is whatever is mapped at that address now: nothing (the AV above), the base game's DLL (session 3 mapped it
  at 0x684F0000-0x68BF9000 over the old proxy range during the deactivation), or the new proxy image (session 4's
  image 2 covered image 1's address). Executing the middle of another function is a crash or a hang, depending on the
  bytes [I].

The trigger here is the harness's Lua channel (not in a player's game). The same class of fault is open in the
product
[I, not shown live]: C functions of an unloaded proxy image that stay reachable in the engine's one Lua VM: the leaked
`VP_LUAEXEC` thread environments (a mod or tool that walks `Threads`), the server-value stubs' metatables (`__gc =
SRefGc`, `__index`, `__call`: `PushMeta`; a `__gc` of an old stub runs whenever the Lua collector finalizes it, at any
allocation, also inside the next activation), and listeners the UI bridge adds to EXE events. Any of them landing in
the base DLL or the next proxy image is a candidate for the reported hang inside `Reload GameCore DLL`; a call landing in
unmapped memory is this crash. What would confirm it: the player's stopwatch.log tail and a dump of the hung EXE
(`cli.py dump`-style full dump: the main thread's stack).

**38.4 A second way to the reported symptom, from the code [C, code reading; not seen live].** The UI queue guard
(ticket 0492, `proxy_queue_guard.cpp` `UpdateOn`) pins the proxy image (`GetModuleHandleExW(... FLAG_PIN ...)`) when it
sees another module's hook on top of its import slot (`KERNEL32!InterlockedExchangeAdd` of the EXE). A pinned image
survives the EXE's `FreeLibrary`; the next `LoadLibrary` returns it without `DLL_PROCESS_ATTACH`, so `g_bootState`
is still 1 and `DllGetGameContext` returns the context whose server was shut down at `SetDLLIFace(NULL)`: no boot, no
run folder, exactly the report's words. `ui-queue-guard-product.md` 7.1 records this trade-off. No session here logged
the guard's `LOUD ... another module hooked it on top` line.

**38.5 Ruled out [L].** A `WH_KEYBOARD_LL` hook installed with a DLL's own module handle (ticket 0720's helper) does
not keep the DLL mapped after `FreeLibrary` (a scratch x86 test, with and without the unhook, 2026-11-26). The pacing
restore at the unload found the engine utility object already freed by the EXE (`the engine utility's vptr was not
this copy (nothing written)`), as the EXE's order (0x47B1D0: the old utility object is freed before the DLL is
unloaded) predicts.

**38.6 The worker threads ended by TerminateThread (the Architect's suspect, 2026-11-26) [C code, I risk].** A thread
killed while it holds a lock (the process heap's, the log's) leaves that lock held, and the next user of it (the next
DLL load, any allocation) waits for ever: the report's words. The proxy ends five threads this way, each in proxy code
outside this ticket's files: the title keeper (`proxy_pack.cpp` `StopTitleKeeper`, under the loader lock), the view-mask
watcher (`proxy_shell.cpp` `StopWatcher`, loader lock), the farm guard's watcher (`proxy_farm_guard.cpp` `StopWatcher`,
loader lock), the keyboard-hook helper (`proxy_kbhook.cpp` `StopHelper`, loader lock) and, only on an unload without
`SetDLLIFace(NULL)`, the vpipc monitor (`proxy_rt.cpp` `DetachOnUnload`). Each of the first four sets a stop flag and
waits for the thread to be outside its work before the kill: the keeper polls its busy flag up to 1.5 s (a tick: an
`EnumWindows`, one `SendMessageTimeout` of at most 250 ms to the game window, a log line), the two watchers 0.5 s (their
work: microseconds, a log line a few ms), the helper waits up to 0.5 s for its "stopped" event and then sits in
`Sleep(INFINITE)`. A kill inside work needs a tick longer than its wait; nothing in this ticket's 9 orderly unloads (of
13 boots in 5 EXE sessions, 38.2; counted from the run folders' `detach: after the orderly shutdown` lines) showed one:
each logged `detach: title: keeper stopped`, no session hung, and the hang did not reproduce in any of the 5 sessions.
(The hand-back post of 15:4x said "16 unloads"; 9 is the count.) Not excluded on a slow machine [I]. The orderly form
(the thread returns by itself, joined before the loader lock, e.g. from `Shutdown`) changes those four files: the MC
made it follow-up ticket 0812 (beta 6), outside 0805.

**38.7 The fix (ticket 0805, branch task-0805-reload-hang).** At the orderly shutdown (`proxy_rt.cpp` `Shutdown`, from
`SetDLLIFace(NULL)` on the engine's main thread, after the server has ended, the image still mapped),
`ProxyLuaExec::OnShutdown` (`proxy_luaexec.cpp`):

1. runs `ProxyLua_InertImage` (`proxy_lua.cpp`) on the persistent thread through the engine's `CallCFunction`: the
   server-value metatables (registry `kKeySRefMt`: `__gc`, `__index`, `__call`; `kKeySRefGcMt`: `__gc`) lose their
   metamethods, each master-form type's function table (registry `kKeyMasterTypes`, upstream master and VP 5.5 instances)
   is emptied and its `__index` removed, and every registry entry under this image's light-userdata keys goes (the two
   metatables, the types table, the stub and method caches, the master-instance cache, the ERef anchor thread). Stubs
   and instances that Lua still holds keep their (now empty) metatables: a later collection finalizes nothing of this
   image, a later index or call raises a Lua error. The core, `ProxyLuaExec::InertImageValues`, is plain Lua 5.1 C API
   (raw accesses, no Lua error) and is tested offline: `proxy/tests/reload_after_game/test_inert.py` (stock Lua 5.1.4,
   the proxy's toolchain and MSVC ASan; a control without the step shows every stub finalized through the old `__gc`).
2. frees the persistent `VP_LUAEXEC` thread with the script system it was made with (`FreeLuaThread`): `G.Threads`
   no longer lists its environment, so the Lua channel's "Main" resolution cannot pick it in a later load (38.3).

Only on the main thread (the engine's thread table may change only there, `OnGameDatabaseCached`'s note); elsewhere
nothing is done and one LOUD line is logged. One `shutdown: Lua: ...` line with the counts. Not changed: the queue
guard's pin (38.4) and the worker threads (38.6).

**38.8 Checks of the fix (2026-11-26, workstation, the fix at 421237f87 on the VP 5.5 pins a2627c8ac).**
- Offline [L]: `proxy/tests/reload_after_game/test_inert.py --also-luaexec`: 9/9 (`inert_test` 19/19 under the proxy's
  clang-cl -m32 and under MSVC x86 ASan with the canary caught; the control without the step finalizes all 16 stubs
  through the old `__gc` and runs the old `__index` and function; with it nothing of the "image" runs at collection, index,
  call or `lua_close`, and the counts are exact), `proxy/tests/luaexec/test_luaexec.py` 9/9 with the changed file.
- `build_proxy.py` and `build_server.py` (release) OK; the built proxy (1d3e6f18...) carries the new log line.
- `proxy/tools/test_proxy_rt.py --build <the fix's build_server output>`: 239/240 at 15:50-16:10; the failure is
  `d28: abrupt unload: the server is gone, the process lives on` ("the server is still running 2 s after the unload"),
  the unload WITHOUT `SetDLLIFace(NULL)`, which the change does not reach (`Shutdown` is not called there); every earlier
  recorded run of the suite passed it (evidence of 0313, 0326, 0334, 0343). Whole rerun 16:10-16:24: 239/240 again, but
  now that case passed and `d28: reload in one process` failed (the second boot's log stops at `sending kRt_Shutdown`, the
  loader's 120-s wait ran out; the Lua step comes after that point and was never reached). The two d28 cases alone
  (`proxy/tests/reload_after_game/d28_alone.py`, the script's own helpers and its built loader, three runs through
  `quietlock.py run`, 17:01-17:02): 3/3, each run the reload and the abrupt unload passed, and every shutdown logged `shutdown:
  Lua: no persistent engine Lua thread of this image (none was made)` (the stand-in has no script system). So the two
  whole-run failures are load races of the whole run, as 0784 found for the same case the same morning
  (`../02-exe-internals/farm-builder-guard.md`) [I], not faults of the change.
- `tools/harness/tests/test_safety_static.py`: 55 passed.
- Live [L], session 6 (c5h-55-a, Goldberg, F:, mods55, VP 5.5, EXE build 26577625, the fix's build_server output laid by
  `lay.py` with the composite of main, D33 helper on; 2026-11-26 16:18-16:59, pid 45324, one EXE, every step by clicks
  and keys, the stock-acting menu): MODS/NEXT (image 1 at 0x6C480000), a new Tiny game started while the harness's
  in_game probe polled the Lua channel through the load, 2 turns through the channel (the harness's diplo watcher armed),
  Exit to Main Menu (the menu's `ActivateDLC` unloaded the proxy), MODS/NEXT (image 2 at 0x6C4B0000: overlapping image 1,
  so before the fix image 1's FuncThunk address lay inside image 2), the same new-game start with probes during the load
  (the 38.3 crash shape): in game, no box, 2 turns; Exit to Main Menu, MODS/NEXT (image 3), a third game, then Exit to
  Windows with the proxy loaded. Three run folders (`162656`, `163944`, `165218`), three orderly shutdowns, each with
  `shutdown: Lua: this image's values made inert on the persistent thread (ran): 4 metamethods removed from the
  server-value metatables, 11 master-form function tables emptied (3472 functions), 6 registry entries removed; the
  persistent thread VP_LUAEXEC = ... freed` (22.6, 28.4 and 14.1 ms). The EXE also calls `SetDLLIFace(NULL)` at its own exit
  (the third shutdown came from Exit to Windows) and the step there is safe: exit code 0, no box, no crash record.
  The F: disk was saturated (an activation 2-4 min, `LoadUnitLibraries` up to 90 s): not a timing measurement.