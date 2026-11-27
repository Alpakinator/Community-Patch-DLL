# proxy/ - the interface model and the proxy generator (epic 2100, increment 1 package I1-C)

The 32-bit proxy DLL sits in the game DLL's slot inside the EXE and forwards the EXE<->DLL interfaces to the x64
server over the vpipc transport (`ipc/PROTOCOL.md` 1.0, epic 1200). This folder holds the machine-readable model
everything is generated from, the per-call policy, and the generator skeleton with a compile-only proof. Findings,
numbers and the open decisions: `research/03-dll-engine-boundary/proxy-design.md`.

## Regenerate (offline, ~3 min, nothing is run)

```
python proxy/gen/extract_model.py   # clang AST + vtable/record dumps, x86 (VC9) and x64 -> model/interfaces.json  (~30 s)
python proxy/gen/verify_slots.py    # MSVC 14.4x cl.exe vcall-thunk check, both arches + x64 smoke table -> model/slot-proof.json (~60 s)
python proxy/gen/extract_lua.py     # clang /E + AST of Lua/CvLua*.cpp, x86 and x64 -> model/lua.json  (~60 s)
python proxy/gen/classify.py --src <fork at the deps/dll.lock commit>   # marshalling, policy, cost projection -> model/model.json, model/policy-table.tsv (~2 s)
python proxy/gen/classify.py --src <...> --check                       # issue 0125: regenerate into a temp folder, compare with the committed files
python proxy/proof/build_proof.py   # generator + compile/link of ICvRandom1 and ICvTeam1 on both toolchains (~65 s)
```

Inputs of these five scripts (epic 2100; corrected 2026-11-10, issue 9008): the DLL fork named by `FORK` in
`gen/toolchain.py` (the DLL source setting of `tools/x64-dll/dll_source.py`: `VP_DLL_SRC`, else
`deps/dll-source.ini`; corrected 2026-11-17, ticket 4713: it named a worktree of the fork's `x64` branch in a user folder;
it must be at the `deps/dll.lock` commit, `PINNED_COMMIT` e9d0c4837, when you regenerate: check it with `git -C <fork>
rev-parse HEAD` first, the branch moves), `research/sources/boundary-benchmark/results.json`, the x64 smoke host's
slot table (`tools/x64-dll/evidence/2026-11-10-smoke/run3/interface-slots.tsv`) and the vpipc transport in `ipc/` of
this repository (merged from epic 1200; the branch `claude/epic-1200-ipc-transport-32a9c4` it came from is history).
Their build outputs go to `BUILD_ROOT` in `gen/toolchain.py` (`%TEMP%/claude/team2100`). The runtime build
(`gen_runtime.py`, `build_proxy.py`, `server/build_server.py`) does not use `FORK`: it takes the fork copy as `--src`.
Since issue 0125 (2026-11-13) `classify.py` does the same: `--src` is required in effect (checked against
`deps/dll.lock` by `tools/x64-dll/build.py` `resolve_src`; `FORK` is refused while it is not at the lock's commit), and
the committed `model.json` and `policy-table.tsv` are its output at 04b1211b8. `extract_model.py`, `verify_slots.py` and
`extract_lua.py` still read `FORK` (`PINNED_COMMIT` e9d0c4837); their committed outputs are from that commit
(`research/03-dll-engine-boundary/proxy-design.md` 1a).
Toolchains: `gen/toolchain.py` (x86 = the DLL's `build_vp_clang.py` setup, x64 = `tools/x64-dll/build.py`'s).

## Files

| Path | What |
|---|---|
| `model/model.json` | **The model** (single file): counts, slot proof, every interface and virtual with types, const-ness, calling convention, both slots, marshalling codecs, policy (now / target) and rule; mirrors with invalidation rules; the unmarshallable list; exports; the Lua surface (all of `lua.json`); cost projections |
| `model/policy-table.tsv` | One row per distinct virtual: direction, interface, signature, slots, codecs, policy, rule |
| `model/interfaces.json`, `lua.json`, `slot-proof.json` | Intermediate outputs of the first three scripts (inputs of `classify.py` and `gen_proxy.py`) |
| `gen/marshal_types.py` | The one type -> codec table, shared by `classify.py` and `gen_proxy.py` |
| `gen/gen_proxy.py` | Generator skeleton: proxy stubs (x86), server dispatch (x64), ids and schema hash |
| `proof/` | Compile-only proof (see `proof/README.md`) |
| `gen/gen_runtime.py` | The runtime generator of epic 2300: every interface, both directions (next section) |

## gen_runtime.py: the generated half of the proxy and the server's remote mode (epic 2300, task 2301)

```
python proxy/gen/gen_runtime.py --out <dir> --src <DLL fork copy> [--hooks-proxy proxy/src] [--hooks-server server/src/remote]
python proxy/gen/gen_runtime.py --out <dir> --src <DLL fork copy> --compile-check --objdir <dir>
```

Offline, about 1 s to generate; `--compile-check` adds about 15 s. Input: `model/interfaces.json` (all 75
interfaces) and the `wrappers` table of `model/model.json`; the fork copy (`--src`, required since issue 9008: it had
a default inside epic 2300's scratch folder; the `deps/dll.lock` commit) gives the `guid<I>` constants and the
compile-check include paths. Output only under `--out`, never in the repository. Specification: the team-2300
contract sections A-C; design `research/03-dll-engine-boundary/proxy-design.md` section 11; wire values
`include/vp_wire.h`; runtime APIs `src/proxy_rt.h` and `server/src/remote/srv_rt.h`. The module docstring holds
the full rule per type.

| Output | Side | What |
|---|---|---|
| `common/vp_ids.gen.h`, `.cpp` | both (C++03) | `kT_<I>` (1-based model position), `kM_<I>_<ident>` = (kT << 16) \| index over the full chain, `kAppSchemaHash`, `kVpKindCount`, `vpk_most_derived/in_chain/is_engine_iface/iface_name/method_key/method_count` |
| `proxy/proxy_ifaces.gen.h`, `proxy_ifaces_1..6.gen.cpp` | x86 | `Proxy_<MD>` per engine -> DLL MD interface (53), every hook and forwarder declared |
| `proxy/proxy_factory.gen.cpp` | x86 | `VpProxyNewWrapper(handle)` |
| `proxy/proxy_engdispatch.gen.cpp` | x86 | `EngDispatch_<MD>` per DLL -> engine MD interface (8), `EngDispatchGenerated` |
| `proxy/proxy_hook_defaults.gen.cpp` | x86 | loud defaults (`ProxyRt::Unimplemented`) of the hooks nobody wrote |
| `server/server_hooks.gen.h` | x64 | hook and forwarder declarations, `RemoteGen_<MD>` classes (8) |
| `server/server_dispatch_1..6.gen.cpp` | x64 | `Dispatch_<MD>` (53), `DispatchGenerated` (last part) |
| `server/server_remote_1..2.gen.cpp` | x64 | `RemoteGen_<MD>` bodies, `VpGenInternS/W` (strings returned by pointer from engine calls) |
| `server/server_hook_defaults.gen.cpp` | x64 | loud defaults (`SrvRt::Unserved`) of the dispatch hooks nobody wrote |
| `proxy/vp_guids.gen.cpp`, `server/vp_guids.gen.cpp` | both | `vpk_kind_of_guid(GUID)` |

MD (most derived interface): engine -> DLL, the direct interface base of the DLL wrapper class whose chain contains
I (ICvGame1 -> ICvGame2, ICvPlayer1/2 -> ICvPlayer3, ...); ICvUnknown is its own MD (static type of
`ICvEnumerator::GetCurrent`). DLL -> engine, the most derived dll_to_engine interface containing I
(ICvEngineUtility1-3 -> 4, ICvUserInterface1 -> 2). 61 MD interfaces: 53 engine -> DLL, 8 DLL -> engine.

Codecs (wire: CALL = self handle, in-parameters in order; RETURN = return value, out-parameters in order):

| Type | Wire |
|---|---|
| ints, enums, `uint`, `DWORD`, `size_t` (narrowed); `bool`, `float`, `double`, `unsigned long long` | i32; b, f32, f64, u64 |
| `const char*` family / `CvString`, `std::string` (value or const ref) / `const wchar*` family | str / str / wstr; returned pointers interned by the caller (`ProxyRt::Intern`, `InternW`, `InternCv` for `const CvString&`; server `VpGenInternS/W`) |
| `GUID`, `const GUID&` | 16 bytes |
| `IDInfo` (value, const ref, returned), `UnitMoveRate`, `CvColorA&` / `CvWorldBuilderMapLoaderMapInfo&` returned, `CvMapInitData*` (presence flag) | fields in declaration order; a returned const reference is kept in a member of the wrapper |
| `const std::vector<bool>&` | u32 n + n b |
| `int&`, `bool&`, `float&`; cursors `int* pIterIdx`, `TechTypes*`/`int*` of GetCurrentResearchTech, `int32*` of GetSteamStat; `CvString&` | in/out (pointers with a presence flag) |
| `char*` + capacity (GetDefineSTRING, GetEvaluatedMapScriptPath, GetGameOption, GetMapOption, GetGreatWorkAudio, GetHexDebugLayerString), SetTimeStr (constant 256, `strcpy_s(256)` in CvDllGame.cpp) | in: u32 capacity (0 = no buffer); out: str, copied as at most capacity-1 bytes + NUL |
| `TechTypes*` + `uint` of GetTechs | in: u32 capacity; out: u32 n + n i32 |
| `FDataStream&` (engine -> DLL) | Read*/read* in, Write*/write* out (`vp_wire.h`) |
| interface pointers | handles per the contract's table (HandleOf / Register / RegisterTemp / Resolve / Wrap / WrapTemp; EngineHandleOf / EngineResolve / EngineWrap) |

Not mechanical (72 methods per side): Lua states and C functions, `void*`, `HANDLE`, `Database::Connection*`,
`FIGameIniParser&`, `FIFile*`, `FFileErrorHandler*`, `FFileTimestamp`, `FAutoArchive&`, `FMemoryStream&` sync
checks, `EnumeratedFilesList&`, `CvPopupInfo`, `CvCombatMemberEntry*`, `IDInfo*` node walkers, `int*`/`const char**`
static tables, `byte**`, `char*&`, `const int*`/`const bool*` arrays (AddPopup, TradeVisuals_NewRoute),
`CvPlotIndexVector` (FFastVector allocates through FireWorks, which the proxy does not link), variadic `FILogFile`
messages, engine-implemented `ICvEnumerator` (GetSelectedUnits, SetDLCAvailable). `--list-loud` prints them.
Special methods are generated, never hooks: `QueryInterface`, `Destroy`, `ICvGame1::Update` on the server,
`DestroyArgs`.

**Hook scanning rule.** Every `*.cpp` and `*.h` in the two hook directories (comments stripped; `*.gen.*` skipped) is
searched for a definition `ProxyHook_<MD>_<ident>(...) {`, `ServerHook_...` or `EngHook_...`. A method with a
hand-written hook calls it on that side, mechanical or not, and a mechanical one also gets its generated body as a
forwarder (`ProxyFwd_`, `ServerFwd_`, `EngFwd_`); a non-mechanical method without one calls a generated loud
default. Server-side DLL -> engine methods have no hooks: `RemoteGen_<MD>` methods are virtual, the non-mechanical
ones log `SrvRt::Unserved`, and a hand-written subclass overrides them. The summary lists hooks found, hooks that
match no method (stale) and hooks of special methods (ignored).

**--compile-check** compiles every generated .cpp without linking, objects under `--objdir` (required with it): x86 with clang-cl -m32 in the VS 2008 environment and the DLL's
flags (`toolchain.dll_args`, include paths under `--src`) plus `proxy/src`, `proxy/include`, `ipc/include`; x64 with
`server/build_server.py`'s flags for `server/src/*.cpp` (/W3 /WX, `/Od` instead of `/O2`) plus `server/src`,
`server/src/remote`, `proxy/include`, `ipc/include`. Then a symbol check (llvm-nm): every generated name an object
references (hooks, forwarders, dispatchers, vpk_*) is defined in the generated objects or by a scanned hand-written
hook. Result 2026-11-10: 22 TUs (x86 11, x64 11), 0 errors, symbol check clean. `RemoteGen_<MD>` classes carry a
`static_assert(!std::is_abstract<...>)`; `Proxy_<MD>` are instantiated by the factory, so both are complete.

## The running proxy (epic 2300)

Design and results: `research/03-dll-engine-boundary/proxy-design.md` sections 11-12.

| Path | What |
|---|---|
| `include/vp_wire.h` | wire values shared by both sides: handle layout, runtime method ids, Lua value codec, stream parameters, return statuses |
| `src/proxy_rt.h` | the runtime API the generated proxy code calls (`ProxyRt::`) |
| `src/proxy_rt.cpp`, `proxy_lua.cpp`, `proxy_hooks.cpp` | the x86 runtime: `DllGetGameContext`, `vpproxy.ini`, server start, endpoint, thread binding, wrapper and engine tables, failure policy, counters, streams; the engine side of Lua; the hand-written hooks (`SetGameDatabase`, `SetDLLIFace`, and since 9008 `ICvUserInterface1::AddPopup(const CvPopupInfo&)`, forwarded to the engine field by field) |
| `build_proxy.py` | `python proxy/build_proxy.py --out <dir> --src <fork source> [--gen <generated dir>] [--config release]`: generator, vpipc x86 (NDEBUG in release), compile, link, manifest, import/export check. `server/build_server.py` calls it, so one command builds both (and fails when this script is missing, since 9008, or exits 0 without its `build_proxy: OK` line, since 9023). Since 9023 the link also needs no error line in lld-link's output and a freshly written DLL, and the OK line carries the final DLL's SHA-256, size and timestamp (also in `build-proxy.json`) |
| `tools/run_proxy_game.py` | the proof harness: the 32-bit headless host with the proxy and the direct server on the same game, `stats.db` compared, `--kill-server-at-turn`, `--load`, `--mod` for saves made with mods; `--fail-on-unexpected` makes both sides strict; the verdict (module docstring) checks both sides' exit codes, reports and counters |
| `tools/remote-allowlist.txt` | the remote-mode allow-list: what a proxied run may hit with a written reason (server unserved methods, proxy unimplemented methods, `failed:<method>`, `unsupported-lua:<type>`); the harness prints it and the server honours it under `fail_on_unexpected=1` |
| `tools/test_policy_table.py` | `python -I proxy/tools/test_policy_table.py --src <fork at the deps/dll.lock commit> --out <dir> [-v]`: issue 0125 (about 17 s, nothing built or played): `classify.py` at the pin reproduces the committed `policy-table.tsv` byte for byte and `model.json` (generated_at masked); its `--check` fails on a changed, missing, added or reordered row and on a changed model.json, and refuses a source that is not the pin. Run it after any change to `classify.py`, `consumers.tsv`, the model inputs or `deps/dll.lock` |
| `tools/test_strictness.py` | `python proxy/tools/test_strictness.py [-v]`: offline negative checks of the harness (issue 9008; 3 s, nothing built or played): compare_stats on empty and one-sided databases, the verdict on canned run folders, strictness reaching server.cfg, no scratch defaults, build_server failing without build_proxy.py. Run it after any change to the harness, compare_stats.py, run_server.py or build_server.py |
| `src/proxy_guard.h/.cpp` | issue 2703: the check of the server's window guard after the server attaches (below) |
| `src/proxy_options.cpp` | one-off 9042: the player's options file `VP64Options.ini` (`docs/OPTIONS.md`; loader `options/`): at a mode=on boot finds it (`VP64_OPTIONS`, else `<Documents>\My Games\Sid Meier's Civilization 5\`), creates it with the commented defaults when missing, logs warnings and values (`options:` lines in vpproxy.log) and adds `options_file=<UTF-8 path>` to the server.cfg it writes (not with `server_config=`). The test tools (`run_proxy_game.py`, `test_proxy_rt.py`, `hutest`) set `VP64_OPTIONS` into their run folder so that no test writes into the real Documents |
| `src/proxy_report.h/.cpp`, `src/proxy_report_mods.cpp`, `src/proxy_session.h/.cpp` | issue 3521 (D119, D124): with `pack=1` every failure of the 64-bit path writes the local error report (`Documents\My Games\Sid Meier's Civilization 5\VP64\reports\<stamp>\`, scrubbed of the user and computer names) and starts `vp64helper.exe popup --report <dir> --options <vpproxy.ini>`; the session then goes on as before (research/03-dll-engine-boundary/proxy-design.md section 22). Tests: `tools/test_proxy_session.py` (`rt_tests/session_unit.cpp`, `rt_tests/fake_helper.cpp`, loader cases) |
| `src/proxy_rt_logic.h/.cpp` | issue 2502: the vpproxy.ini settings and D28 defaults, run folders, strict allow-list, thread-binding rule, frame marker and snapshot format; no engine needed, unit-tested (`tools/test_proxy_rt.py`) |
| `src/proxy_shell.cpp`, `.h` | the shell's two in-process jobs on the proxy route (one-off 9040; product form issue 3305). Switched by `VP64Options.ini` `[Renderer] ServerRenderer` (0 = nothing). With 1: `ProxyShell::Configure` (in `Boot`, before server.cfg) checks `<proxy folder>\shell_composite.dll` against `shell_composite.sha256` (the proxy's own SHA-256 and the composite's; written by `tools/shell_manifest.py`), holds the file without write sharing and adds `render=1`, `render_section=Local\VP64Shell_<pid>` to server.cfg; `OnBoot` sets `VPSHELL_SECTION=<the same>` and loads it; the view-mask write `view &= config \| 0xFF000000`, Terrain cleared while the composite is loaded, every EXE check kept, re-checked at each Update and by a watcher thread every 10 ms (the window after GVS::StartupGame); `OnUnload` stops the watcher. Test switches: the 9040 markers `crashlogs\shell.helper` (no hash check) and `crashlogs\viewmask.enabled` (`keep=`/`clear=`, their crashlogs logs). One `shell:` line in vpproxy.log per change. `research/05-graphics-composition/proxy-route-compositor.md` 4 |
| `tools/shell_manifest.py` | issue 3305: `write --proxy <dll> --composite <dll>` writes `shell_composite.sha256` beside the composite; `check <folder>` verifies a layout as the proxy will |
| `src/proxy_update.cpp` | issue 2502: the Update hook (polls the Lua channel before each `ICvGame1::Update`), `VpProxy_EngineUtility`, and (coordinator) `VpProxy_CoreLockYield/Resume` (`kRt_CoreLockYield/Resume`) |
| `src/proxy_luaexec.cpp`, `.h` | issue 2505: the harness's external Lua channel served against the engine VM (port of the fork's `PollExternalLuaRequest`, instance-aware-instruments @ 440878983: same events `Local\VPLuaExec[Done][_<VP_INSTANCE>]`, files in the engine's cache folder, headers, result format), so `tools/harness/lua.py` works unchanged; persistent engine Lua thread `VP_LUAEXEC` created in the hook of `CacheGameDatabaseData` |
| `src/proxy_db.cpp`, `include/vp_dbfp.h` | issue 2504: `SetGameDatabase` (finds the engine's database: ini `db=`, the main file, or a BinaryIO snapshot of the in-memory database), the database fingerprint, the replay of the DLL's writes (`kRtE_DbOps`), engine snapshots for the server's pull of engine-written tables (`kRtE_DbSnapshot`) |
| `src/proxy_hooks_human.cpp` | issue 2503: the human-game hooks (node walker, static tables, combat members, SetDLCAvailable, loadFromIni, selected cities and units, GameplayUnitMoved) |
| `src/proxy_budget.h/.cpp`, `src/proxy_budget_core.h`, `include/vp_budget.h` | ticket 4652 (epic 0437; off by default): with `VP64_TURN_BUDGET=1` in the EXE's environment the proxy creates `Local\VPBudget_<pid>` (server.exe publishes its turn boundaries there) and writes `budget-proxy.tsv` beside vpproxy.log: per turn the EXE's time with no call to the server in flight, and per call class the calls each way with the proxy's times. `vp_budget.h`: the call classes and the switch, shared with the server. Off: one bool test per call. Host test `server/tests/budget/run_budget_test.py`; research `research/08-performance/turn-time-study.md` "The turn budget (W4a)" |
| `tools/test_proxy_rt.py`, `tools/rt_tests/` | issue 2502: offline tests: unit tests, a loader that loads the proxy as the EXE does (D28 layout, reload, abrupt unload, off switch, strict allow-list); issue 3305: section 5 (`--only-shell`, `--asan`): `rt_tests/shell_unit.cpp` (proxy_shell.cpp against a fake EXE image and the stand-in composite `rt_tests/composite_standin.cpp`) and the shell loader cases |
| `tools/proxy_snapshot.py` | asks a running proxy for a counter snapshot (`Local\VPProxySnapshot_<pid>`; rows in `vpproxy-snapshots.tsv`) |
| `tools/proxy-strict-allowlist.txt` | the proxy's strict allow-list (empty) |
| `include/vp_buildid.h`, `src/proxy_buildid.cpp`, `gen/build_info.py` | issue 2704: the build id, the first line of `vpproxy.log` and (`server/src/srv_buildid.cpp`) of `server.log`: repo commit, dirty flag, DLL pin, src mode, config, build time (generated `vp_build_info.h`), and the image's own SHA-256 computed at run time (bcrypt.dll loaded by name, no import added) |
| `tools/hutest/` | issue 2503: the human-paths pair test (real proxy and server between a fake engine and a fake DLL); since issue 2704 it supplies the closed database library, writes `host=headless`, and `--break` fails on any unexpected failure (`--selftest-unexpected CHECK` proves it) |
| `tools/live/` | issue 2704: the live-run helpers of `tools/harness/RECIPE-proxy.md` (prepare, launch, guard check, watch loop, wait, idle window, probe, found city, `set_ini.py`); no absolute paths. Issue 2706: `eq_save_body.py`, `eq_stats.lua/.py` (equality that can differ). Epic 2700: `arm.py` (one arm of a live series), `summarize_arms.py` |
| `src/proxy_local.h/.cpp`, `src/proxy_hooks_local.cpp`, `src/proxy_purity.h/.cpp` | epic 2700 (issue 3503: the purity file): local answers (canonical handles, the lock-want word, the epoch mirror; decision D30); design and invalidation of each answer: `research/03-dll-engine-boundary/proxy-design.md` section 16 |
| `model/pure-methods.tsv`, `model/lua-pure-bindings.tsv`, `model/purity-baseline.json`, `tools/derive_purity.py`, `tools/gen_pure_list.py`, `tools/test_purity.py` | issue 2701/2805: the audited purity lists with source evidence; issue 3503: they reach the proxy as data, `purity.tsv` bound to the x64 DLL (`src/proxy_purity.cpp` loads it; `derive_purity.py` writes it from the audit frozen in the baseline; `gen_pure_list.py --check` is build step 0; proxy-design 16.9) |
| `gen/regen_model.py` | issue 3503: the whole model regenerated from a `--src` and compared with `model/` (the release driver's stage `model`; proxy-design 1b) |
| `tools/test_dbfp.py`, `tools/dbfp_unit.cpp`, `tools/db_dump_diff.py` | issue 2504: database fingerprint and replay tests; a full dump diff of two databases |
| `tools/hutest/` | issue 2503: the real proxy and server between an x86 fake engine and an x64 fake DLL (`build_hutest.py --b <build> --src <fork> --deps <deps> --out <dir> [--break]`) |
| `tools/calls_report.py` | calls per turn by direction and interface, seconds per turn of both runs (the same turns as the harness: start+1 .. target-1) and transit per call by method (server round trip minus proxy handler time) of a harness run |

`vpproxy.ini` (next to the proxy DLL, or `VPPROXY_INI`; relative paths against the proxy's own folder; no ini at all
= `mode=off`; issue 2502, 2026-11-10). Keys and defaults: `mode` on | off; `fallback_dll` `CvGameCore_Expansion2_x86.dll`
(the original 32-bit DLL the off switch loads); `x64_dir` `VP64`; `server_exe` `<x64_dir>/server.exe`; `server_config`
none (the proxy writes server.cfg into the run folder; the proof harness passes its own; its `dll=` is the x64 DLL by
issue 2606's rule: with `pack=1` the manifest's `x64_dll.file`, else `<x64_dir>/CvGameCore_Expansion2_x64.dll` when it
exists, else `<x64_dir>/CvGameCore_Expansion2.dll`); `run_root` `<x64_dir>/run` (in an installed game
`<user>\Logs\VP64\run`: the mods form from the MODS folder's parent, issue 0148; the modpack form, under `Assets\DLC`,
from the game's config.ini folder, issue 2606, MC #3454; research proxy-design 14);
`keep_runs` 10 (ticket 0747: a test switch; `[Logs] KeepRuns` set in `VP64Options.ini` wins over it, and the number in
force also goes to the compositor as `VPSHELL_KEEP_LOGS`; after a clean server exit the proxy deletes the server's
database copies in `<run>\db`); `log` `<run folder>/vpproxy.log`; `game_dir` the EXE's folder; `loc_db` the game's own
`Documents\My Games\Sid Meier's Civilization 5\cache\Localization-Merged.db` when it exists, else none (bug 0123; `none`
or an empty value = the server runs without localization); `server_allow` "" (the server's
`unexpected_allow`); `dll_logs` 0; `db` "" (explicit override; else the proxy finds the engine's database itself,
issue 2504); `server_args` ""; `on_server_death` exit | continue; `strict` 0; `strict_allow` ""; `connect_timeout_ms`
60000; `reply_spin_us`, `idle_spin_us` vpipc defaults; `host` exe | headless (the proof harness writes headless);
`counters` 1; epic 2700: `local_answers` 1 (0 = every call crosses, the reference), `local_verify` 0 (1 = every local
answer also asked of the server and compared; mismatch logged, exit 6 under strict), `local_test` "" (`stale`: the
negative test), `keep_temp` 0 (item 4, off after a live crash). Layout under the real EXE (D28) and what each boot writes: `research/03-dll-engine-boundary/proxy-design.md`
section 14; provisioning into an instance: `tools/instances/provision.py set-proxy`; the live recipe:
`tools/harness/RECIPE-proxy.md`.

The server's window guard (issue 2703, 2026-11-10). When `CIVBG_GUARD=1` is in the EXE's environment (the harness
sets it; the server inherits it), the boot, right after the server attaches and before `kRt_Boot`, reads the server's
`Local\CivBgGuard_<server pid>` (`src/proxy_guard.cpp`) and fails the boot like every other boot failure
(`DllGetGameContext` returns NULL, the server is closed) unless magic is `CBG1`, `enabled` is 1, `guard_state` (1276)
is 1 and `win_veto_failed` (1272) is 0. vpproxy.log lines: `boot: guard: server pid N: Local\CivBgGuard_N magic
0x31474243, enabled 1, guard_state 1, guard_missing 0x0 ...` on success; `boot FAILED: guard: ... refusing to
continue (issue 2703)` when incomplete; `boot FAILED: guard: ... has exited with code 0x...` naming the server's
guard exit codes; without the variable `boot: guard: CIVBG_GUARD is not 1: ... not checked`. A server that ends with
its guard code before it attaches (0xC5F00030 install, 0xC5F00032 self-test) is caught by the attach loop
(`boot FAILED: the server exited with code ... (0xC5F00032) before it attached`); one that ends later (0xC5F00033, a
veto failed on a thread about to create a window) is a server death (`on_server_death`). Server side and exit codes:
`server/README.md`, "The window guard". Offline test: `tools/test_proxy_rt.py` section 4.

Example (offline):
```
python server/build_server.py --src <fork at deps/dll.lock> --out <dir> --config release --offline
python proxy/tools/test_strictness.py
python proxy/tools/run_proxy_game.py --bin <dir>/bin --proxy <dir>/proxy/bin/CvGameCore_Expansion2.dll --spec server/specs/continents-tiny-fixed-seed.json --turns 50 --fail-on-unexpected --run <run dir>
```
Every proof of epic 2300 command by command (the direct baseline, the Tiny turn-50 save, the host save loaded by the
direct server, the kill test, the huge save and where its mods come from): `server/evidence/2300/README.md`; the
strict re-runs of issue 9008: `server/evidence/9008/README.md`.
