# The patch side on the Vox Populi 5.5 pins (ticket 0765)

impl-0765, 2026-11-25, account A, epic 0763 (D259). Follows [vp55-dll-branches.md](vp55-dll-branches.md) (ticket 0764:
the fork branches x64-vp55 a2627c8ac and i0-vp55 dc6b8ad48 on upstream tag Release-5.5 b0bce8c2f). Branch
`task-0765-vp55-patch-side`. Marks: `[C]` read in git or the source (this agent), `[L]` a build or a run on the
workstation, `[I]` inferred.

## 1. The locks

`deps/dll.lock` and `deps/i0.lock` name the 5.5 branches `[C, 2026-11-25 18:1x]`:

| Lock | branch | commit | describe | fingerprint (`src_fingerprint.py --git <clone> --commit`) |
|---|---|---|---|---|
| dll.lock | x64-vp55 | a2627c8ac15610499a45614d2c5a3242ddbb4ee1 | Release-5.5-28-ga2627c8ac1 | v1 94579acb...5562, 488 files (= 0764's take lock of bea5974f6: the same tree) |
| i0.lock | i0-vp55 | dc6b8ad48885ab882c60085903e22a05632a5a56 | Release-5.5-25-gdc6b8ad488 | v1 ee12b593...44d4, 484 files |

`base = Release-5.5 (b0bce8c2f)` in both. The release configuration stays plain `release`: no `release-config` line;
the `profile`/`profile-file` lines are still 1b87dd185's and only a `release-pgo` build reads them (the PGO retrain on
5.5 is ticket 0774). The replaced 0.1b keys are kept with the suffix `-v548` (a key with a dot, such as `-0.1b`, is not
read by `tools/release/stages/_common.py`'s `LOCK_KEY_RE` `[a-z][a-z0-9-]*`: its continuation lines would be folded
into the key above it) `[C]`.

The lock is exported (MC #15669): the pins folder is written `<pins_dir>` (the `pins_dir` setting of
`tools/release/settings.py`) instead of the user's profile path; `tools/d10/run_d10.py`, `tools/drift/run_pair.py` and
`perf/speedkit/defineoff.py` resolve it and `%VAR%` placeholders (tests in `tools/d10/tests/test_pin_defaults.py`, which
also checks that no lock names a profile folder, and `tools/drift/tests/test_run_pair.py`). The owner's name in two
older comment lines was replaced by "the owner".

With the pins on 5.5 the release journeys follow (MC #16301): `tools/hi/journeys/journeys.json` has `default_vp` "5.5",
and its four 5.4.8 journeys (release-basic, release-large and their m43 forms) carry `"vp": "5.4.8"` (without the tag
they would count as 5.5 and collide with the 5.5 set: "journey 'release-basic' is listed twice for VP 5.5");
`tools/hi/journeys/tests` 38 passed, `tools/release/tests/test_hi_journey.py -k "index or vp or committed"` 3 passed (the whole stage suite, 136 tests, was stopped after 60 passed: about a minute a test on the loaded machine) `[L, 2026-11-25 23:5x - 2026-11-26 00:25]`.

## 2. VP 5.5's `Exe` table on the product route

### 2.1 What 5.5's Lua calls `[C, git grep of b0bce8c2f, LuaCATS excluded]`

| File (5.5 mod files) | Line | Call | When |
|---|---|---|---|
| `(1) Community Patch/Core Files/Overrides/InGame.lua` | 1078 | `Exe.TryGrowUICommandStream()` | `OnUpdate`, every frame while `g_IsGrowingUICommandStream`: stops on ok or any refusal other than `"pending"` |
| `(1) Community Patch/Core Files/Overrides/YieldIconManager.lua` | 601 | `Exe.TryDisableEngineYieldIconManager()` | once at load (result unused) |
| `(1) Community Patch/LUA/DiploCorner.lua` | 549, 558, 572 | `TryScheduleResync`, `CanScheduleResync`, `TryEnableTunerInMultiplayer` | the resync button's click; `PlayerDoTurn` and `MultiplayerGamePlayerUpdated` (shows the button when ok); a key, only when `Game.IsGameMultiPlayer()` |
| `(3a) VP - EUI Compatibility Files/LUA/DiploCorner.lua` | 547, 556, 570 | the same three | the same |

The DLL's table (`Lua/CvLuaExe.cpp` at a2627c8ac) has 11 functions: `GetBuildName`, `Can/TryScheduleResync`,
`Can/TryDisableEngineYieldIconManager`, `Can/TryEnableTunerInMultiplayer`, `Can/TryGrowUICommandStream`,
`Can/TryGetUICommandStreamUsage`; each returns `ok, reason` (`TryGetUICommandStreamUsage`: `true, {used, capacity}`),
and every one but `GetBuildName` refuses `"not_ui_thread"` on the game-core thread. In the out-of-process build
(`VP_OUT_OF_PROCESS`, the x64 layer 6f5f1cf55) every `Exe::` feature answers as for an unknown EXE:
`"unsupported_exe"` (`"bin_hooks_off"` with `MOD_BIN_HOOKS` off), `GetBuildName` `"Unknown"` `[C, Exe/CvExe.cpp
lines 287-362]`.

### 2.2 What the proxy answers (proxy/src/proxy_lua.cpp, `ReplaceExeTable`)

After the DLL's globals are set in a registration state (`ProxyHook_ICvScriptSystemUtility1_RegisterScriptLibraries`;
the EXE copies them into every UI context, `proxy-design.md` 24.2 B1), each of the 11 functions of the registered
`Exe` table is replaced by a C function of the proxy: no call crosses to the server. A registration without an `Exe`
table (VP 5.4.8's DLL) is left as it is. The main thread is the thread of the ui channel's registrations; off it every
function answers `"not_ui_thread"`.

| Function | Answer on the product route |
|---|---|
| `GetBuildName` | `"DX11"` on an EXE build of the registry (`ProxySession::IsKnownExe`), else `"Unknown"` |
| `CanGrowUICommandStream` | from `ProxyUiDraw` (proxy_uidraw.h `LuaCanGrow`): ok unless the growth is switched off (`"bin_hooks_off"`, tests only), the EXE is not a known build or its checks failed (`"unsupported_exe"`), or the growth failed after the checks (`"unavailable"`) |
| `TryGrowUICommandStream` | the same refusals; else ok once every stream is grown, `"pending"` before (the frame hook grows them; the Lua call grows nothing, so no stream is grown twice, upstream-13406-product.md 5.6) |
| `Can/TryGetUICommandStreamUsage` | the in-use stream's last-frame data use (stream field `+0x2074`) and data capacity (`+0x34`), read after the EXE checks passed, behind `VirtualQuery`; `"unavailable"` before |
| `Can/TryScheduleResync`, `Can/TryDisableEngineYieldIconManager`, `Can/TryEnableTunerInMultiplayer` | `false, "not_implemented"`: each acts on EXE objects (the resync flag, the engine YieldIconManager's event subscriptions, the tuner listener); no port, which would need the MC's word as an EXE call or data write (D136 class). In single player, the product's game, stock's in-process DLL answers `"not_network_game"` for the resync (the button is hidden either way) and the tuner key is not asked outside multiplayer. The engine's own YieldIconManager keeps raising `Events.ShowHexYield` as on 5.4.8; VP 5.5's `YieldIconManager.lua` does not listen to it, so the cost is the engine's own work, no second set of icons `[I: not measured]` |

The first answer of each function is logged once (`exe table: first call of Exe.<name>: ...`) and each replaced table
(`exe table: the DLL's Exe table in state ...`), so a product run's `vpproxy.log` shows what VP's Lua got.

Tests `[L, 2026-11-25 18:18]`: `python -I proxy/tools/test_proxy_rt.py --build <any> --scratch <dir> --only-uidraw
--asan`: `uidraw_unit` 304/304 on the proxy's toolchain and under MSVC `/fsanitize=address` (the accessors: pending
before and during the growth, ok after it with the 4 MB capacity, `"unavailable"` after a layout failure,
`"unsupported_exe"` for every EXE check that fails, loud or not, `"bin_hooks_off"` when switched off).

## 3. The stale-instance guard (0576) against upstream's auto-invalidating pointers

`[C, a2627c8ac]` On 5.5 a Lua instance is a full userdata `LuaTaggedAutoPointer` (`Lua/!CvLuaApiInstanceTemplates.h`)
holding an `AutoInvalidatedPointer` linked into the object's `m_PointerList` (`CvGameCoreDLLUtil/include/CvStructs.h`
247-272; `CvUnit`, `CvCity`, `CvPlot`, `CvPlayer`, `CvTeam`, `CvArea`, `CvDeal`, `CvLeague`, the tech class carry one).
The object's destructor nulls every pointer in its list, so `GetInstance` on a deleted unit or city returns NULL and
raises `"Invalid instance!"` instead of reading freed memory. 88400faf4 (#13555) keeps a copied object's list empty
(`CvLeague` copies). On the proxy route (0314) such instances cross as `kLInstance`, and the server's `DecodeMaster`
hands the DLL the DLL's own cached userdata, or its invalidated one for a deleted object (the DLL's error), never a raw
pointer (`server/src/remote/srv_remote_lua.cpp`). So on 5.5 the case 0576 guards (a table `{__instance = light
userdata}` of a freed `CvUnit`/`CvCity`) cannot arise.

The guard stays: it serves the 5.4.8 instance form, and on 5.5 it costs nothing. `SrvLuaStale::Learn` finds no
`LuaTypes.Unit`/`City` tables in a 5.5 state (the types are registry metatables), its mask is 0 and `CheckArgs`
returns at its first test `[C, server/src/remote/srv_lua_stale.cpp 133-185]`. Redundant on the 5.5 path, kept for
5.4.8; no code change.

## 4. Per-player object ids (upstream 0848d52cc, #13643)

`[C]` Cities, units and armies take ids `(player + 1) * 2^17 + n` (below 2^24, the part of a unit id the EXE's
strategic view keeps; the commit message). The server's and the proxy's readers of object ids keep them as 32-bit
`int`s: every key built from an id is `owner << 32 | (uint32_t)id` (`srv_units_core.h` 461, `srv_units_queue.h`
107/161, `srv_feed.cpp` 90, the four `server/src/extras/units/*` `KeyOf`); no id is packed into 16 or 24 bits
(`grep` of `server/src`, `proxy/src`, `render` for shifts and masks on ids, 2026-11-25). The proxy model marshals ids
as `int`. No change needed.

## 5. The server's database readers on 5.5's schema

`[L, 2026-11-25 18:3x, C:/c5-64bit-work/0765/schemacheck.py]` Every `.rows("Table", {columns})` read of `server/src`
and `render` (28) and the six fixed SQL statements of `srv_main.cpp`, `srv_ifaces.cpp` and `srv_remote_db.cpp`,
checked table and column against the 5.4.8 test database (`civ5-headless/pinned/vp-modpack-5.4.8-cdrift`) and 5.5's
(0764's testdata stage, sha256 d27ffe6b...): **34 reads, every table and column present in both**. The rows differ
(5.5 changes `ArtDefine_UnitMemberCombats` in 291 of 364 rows, `ArtDefine_Landmarks` in 143 of 627, `Units` 215 ->
214, `UnitPromotions` 419 -> 429; `MAX_MAJOR_CIVS` 22, `MAX_MINOR_CIVS` 41, `MAX_CIV_PLAYERS` 63 in both): data the
readers take as it is.

## 6. The localization database from 5.5's texts

`[L, 2026-11-25 18:43-18:49]` The release driver's testdata stage builds the game database with civ5-headless's
`dbbuild`, which attaches an EMPTY copy of the localization schema as `Localization` while the mods' update files run
(`civ5headless/dbbuild.py` `TEXT_SCHEMA`), so the mods' text lands in a throw-away `text-schema.db` (8,839 en_US rows
on 5.5) and an `<Update>` of base text finds no row; the candidate descriptor kept 5.4.8's merged database
(release-driver.md W1, "not rebuilt per release yet"). The product route is not affected: there the server starts its
localization from the instance's own `cache/Localization-Merged.db`, which the EXE writes (bug 0123).

Built here: `C:/c5-64bit-work/0765/locbuild.py` (kept with its output) runs the same testdata plan (0764's
`Civ5DebugDatabase.db.build/plan.txt`) through the same host (`civ5headless.exe builddb`, the release's
`out/Release/20261116-0452-landmasses` build, used as a tool) with only two changes: the plan's `attach ... Localization`
line names a copy of the base game's `Localization-Merged.db` instead of the empty schema, and the post-processing
lines are left out (the game database of this run is scratch). Base: instance c5h-55-a's cache file (EXE build
26577625, base game and DLC text, no mod: 24,571 en_US rows, sha256 86ec7674...; 5.4.8's pinned merged database has
33,662, the base plus 5.4.8's mod text). Result: host exit 0, every step ok, 360 s on the loaded F: drive,
**33,410 en_US rows, 147,143 rows in all, sha256 1f40f274a0d2abb77d83aaf6b1489c0e0b239053abe9f042f7b56e54b79c3e5b**;
5.5's new keys are there (`TXT_KEY_MP_FIRETUNER_REFUSED`, `..._ALREADY_ENABLED`, absent from 5.4.8's database). Kept
with the test database in `C:/c5-64bit-work/keep/release-5.5-db/` (README there); the Release-5.5 descriptor of the
hand-back names both. To redo for a later release: `python -I locbuild.py --plan <testdata db>/*.build/plan.txt
--game-dir <testdata game folder> --exe <civ5headless.exe> --base <a base Localization-Merged.db> --work <dir>`
(about 1-6 min). Not done: the testdata stage itself does not build it yet (tools/release/ is ticket 0766's; a
follow-up for the release driver's owner).

## 7. The purity lists re-audited at a2627c8ac

`[C, 2026-11-25 18:50-19:07]` `derive_purity.py` at the new pin kept 134 of the 169 audited rows: 8 method and 27 of
the 29 Lua rows dropped. Every drop was read by hand against the 5.5 source (`C:/c5-64bit-work/0765/src-x64`) and
the 0.1b pin's (`keep/pins-0.1b/src-x64-1b87dd185`):

| Cause (callee or text) | Rows | Why the row stays pure |
|---|---|---|
| `BasicLuaMethod`: 22 definitions -> 1 (`Lua/!CvLuaApiExec.h`: `return luaEx_exec(L, func)`, #13489) | the Lua getters through `BasicLuaMethod` (City.GetOwner, Game.GetActivePlayer, Game.GetGameTurn, Player.IsAlive/GetTeam/IsEverAlive, Plot.GetIndex/GetNumUnits, City.IsCapital) | the wrapper's text is unchanged; `luaEx_exec` reads the arguments, calls the same member and pushes its result: Lua plumbing |
| `GetInstance` changed (the instance templates of #13489: `LuaTaggedAutoPointer::CheckFromStack`, a read of the userdata, NULL for an invalidated object) | Unit.IsHasPromotion/IsEmbarked/GetDomainType/GetDamage/GetOwner/GetBaseCombatStrength, City.IsHasBuilding/IsFranchised/GetMaxHitPoints, Player.IsMinorCivDisplayedQuestForPlayer, Plot.IsRevealed | a read; no DLL state written |
| `luaL_optbool` moved into `Lua/!CvLuaApi.h` (`lua_isnone` instead of `lua_isnoneornil`) | Plot.IsRevealed | argument reading |
| `Destroy` +1 definition (`LuaTaggedAutoPointer::Destroy`, a userdata finalizer) | ICvUnknown::Destroy, ICvGameContext1::GetPreGame/GetScriptSystemUtility | the name check only: the new definition belongs to another class |
| `GetType` +1 (`ExeBuild::GetType`), `Push` 4 -> 2 (the instance templates) | Player.IsMinorCivDisplayedQuestForPlayer, the instance results | name check only / Lua bookkeeping |
| `initStatics` (`s_players.size()` -> `CvEnumMap<...>::size()`) | ICvGameContext1::GetTeam | the same loop bound |
| `GetAreaEffectPromotionUnits` +1 (a non-const overload), `plotDistance` (x and z now wrap together on a wrapping map, a fix) | ICvUnit1::IsInvisible | both read only |
| `TContainer::Get` text changed (#13643: a 64-slot id cache per container, read and written only on the game-core thread, `TContainerUseCache`, and only with the item `m_items` holds) | ICvPlayer1::GetUnit, Plot.GetPlotCity, Player.GetCapitalCity | the result is still the map's item; the cache is memoisation, not game state |
| `CvCombatInfo::getMaxDamageMemberCount` became static (the same constant) | ICvCombatInfo1::GetMaxDamageMemberCount, GetDamageMember | unchanged values |
| `Map.GetPlot`: pushes nil for an off-map plot (was no result) and `luaEx_pushPlot` | Map.GetPlot | the push writes the calling state's object cache or links a new userdata into the plot's `m_PointerList`: Lua bookkeeping |

The six rows whose cited text changed (ICvPlayer1::GetUnit, the two CombatInfo rows, Map.GetPlot, Plot.GetPlotCity,
Player.GetCapitalCity) carry `[cited at a2627c8ac]` with the 5.5 line numbers; a citation of a file whose name starts
with `!` (`Lua/!CvLuaApiInstanceTemplates.h`) is not read by `derive_purity.citations` (it resolves to
`CvLuaApiInstanceTemplates.h`, "file not in the DLL source"), so the push is named in prose there. `AUDITED_AT` =
a2627c8ac; `derive_purity.py --make-baseline --fork <clone>`: **169 entries (140 method, 29 lua), 425 citations, 408
callee names, 0 unresolved**; at the pin **169 kept, 0 dropped**; `gen_pure_list.py --check` holds;
`proxy/tests/test_purity_drops.py` 21 passed, 1 skipped (`test_build_objects` without `C5_PROXY_BUILD_OUT`): its
0628 rows now name their own citation commit (`ROW_CITED_AT`). A proxy build at the new pin without the re-baseline
fails at its step 7 ("the pin drops 35 audited purity row(s) not in the allow-list") `[L, 19:00]`.

## 8. Builds and suites on the new pins

### 8.1 Upstream's interface headers on 5.5, and our code built against them `[C read, L builds]`

- `FirePlace/include/FireWorks/FLua/include/FLua.h` starts with `#error` ("Use this no longer!"): the closed-library
  stubs (`tools/x64-dll/closedlib-stubs/build_stubs.py`, group FLua) build without it since 3511, and the proxy
  includes `<lua.h>`/`<lauxlib.h>` itself since 3518: the stubs step (22 s) and the proxy (41 s) built at a2627c8ac.
- `CvGameCoreDLLUtil/include/ICvDLLScriptSystem.h`: `struct lua_State;` became `typedef struct lua_State lua_State;`
  plus `typedef int(*lua_CFunction)(lua_State*);`. The two lines shift the interfaces of that header by one line
  (`ICvEngineScriptSystem1`, `ICvEngineScriptSystemArgs1` in `interfaces.json`); no method, signature or slot changed
  (`slot-proof.json` equal; the generated runtime and policy table equal).
- `CvGameCoreDLLUtil/include/CvStructs.h`: the auto-invalidating pointers (section 3).
- The proxy model at a2627c8ac (`proxy/gen/regen_model.py --src <copy> --lock deps/dll.lock --out <dir> --write`, 184 s,
  `[L, 19:06-19:09]`): DIFFERENT (123 lines of the report) in `interfaces.json` (the line shifts above;
  `records.CvAStar.x64`: #13423 rewrote the open list, `m_bHeapDirty` gone, 39 -> 38 fields, the fields after it moved),
  `lua.json`/`model.json` (3,473 compiled registrations in each configuration, was 3,463: the 10 worker-plan methods of
  `Player`; the `Exe` library's 11 functions); `slot-proof.json`, `policy-table.tsv` and the generated runtime equal.
  Written to `proxy/model/` (checklist step 10, "write the model even when it comes out EQUAL").

### 8.2 Builds `[L, 2026-11-25, workstation]`

| What | Command | Result |
|---|---|---|
| x64 DLL + server + proxy from scratch | `C5_RELEASE_TEST_DB=<5.5 test db> python -I -X utf8 server/build_server.py --config release --src C:/c5-64bit-work/0765/src-x64 --out C:/c5-64bit-work/0765/x64 --offline --jobs 8` (18:49-18:58) | build_all OK in 429 s (stubs 22, cvgamedatabase 30, cvlocalization 16, dll 332 with the LTO link, host 26, smoke 4 s); narrowing 799 sites, baseline 808, 0 new, 9 gone (0764's union baseline); smoke "Release-5.5-28-ga2627c8ac1 Copy", "Supporting 22 major civilizations"; render messages absent (as 0764's); server OK in 585 s; the proxy step then failed on the purity lists (section 7), built again alone after the re-baseline |
| proxy | `python -I -X utf8 proxy/build_proxy.py --out <x64>/proxy --src <copy> --gen <x64>/gen --config release` (19:18-19:21) | OK in 147 s with the full purity test (66/66 in 99 s) at e6edadd7c (repo_dirty 0) |
| final server + proxy | `server/build_server.py --config release --src <copy> --deps <the build_all above> --out C:/c5-64bit-work/0765/x64final --offline` at 957531e59 (20:03-20:09, `evidence/0765/p1_builds.sh`) | OK in 369 s (proxy step 7 test_purity 66/66): server.exe 8CBA59D6..., proxy 966A85B3...; laid out with the DLL as `F:/c5-64bit-work/0765/layouts/x64-a2627c8a-p957531e` (SHA256.txt, README.txt) |
| 32-bit i0 | `git -C <clone> worktree add --detach <dir> dc6b8ad48`, then from `<dir>` with LLVM and `<dir>` on PATH `python build_vp_clang.py --config release` (20:09-20:17) | OK, link 334 s; 13,474,816 B, 974AEDA7...; "Release-5.5-25-gdc6b8ad488 Clean"; laid out as `F:/c5-64bit-work/0765/layouts/i0ship-dc6b8ad4` |
| shell (composite) | `C5_SHELL_BUILD=<dir> quietlock.py run ... cmd //c shell\build.cmd` (20:17-20:21) | BUILD OK; the composite copied beside the layout: `F:/c5-64bit-work/0765/layouts/shell-957531e/` (77D7450F...) |
| source copy | `git archive <commit> CvGameCoreDLL_Expansion2 CvGameCoreDLLUtil CvGameDatabase CvLocalization CvWorldBuilderMap FirePlace ThirdPartyLibs build_vp_clang.py ...` (`C:/c5-64bit-work/0765/srccopy.sh`) | 40 MB instead of the whole tree's 355 MB (the mods' art); the fingerprint matches the lock (it covers only those folders); every builder accepted it |

Where: on C: because F: was saturated by other teams' builds and a huge autoplay at the time (`Get-Counter`: 945 %
disk time, 0.45 MB/s on F:; the stubs step compiled one object a minute there, a 355 MB tar took 30 minutes).

### 8.3 Suites

| Suite | Result |
|---|---|
| `python -I proxy/tools/test_proxy_rt.py --build C:/c5-64bit-work/0765/x64 --scratch <dir>` with `C5_RELEASE=<Release-5.5 descriptor>` | **240/240** (209 s) `[L, 19:14-19:18]` |
| `test_proxy_rt.py --only-uidraw --asan` | 304/304, also under MSVC ASan `[L, 18:18]` |
| `proxy/tools/test_purity.py` (inside build_proxy step 7) | 66/66 `[L, 19:20]`, with the 'derive b' fallback (a header definition when no uncited .cpp one is left: at 5.5 `IDInfo` of `CvStructs.h`; at 1b87dd185 it was `luaL_optbool` of `Lua/CvLuaSupport.cpp`, moved into `Lua/!CvLuaApi.h` by #13489) |
| `proxy/tests/test_purity_drops.py`, `test_build_settings.py` | 21 passed, 1 skipped (`C5_PROXY_BUILD_OUT` unset) `[L]` |
| The following on the final builds (x64final: server and proxy at 957531e59), `research/03-dll-engine-boundary/evidence/0765/p2_suites.sh` with `C5_RELEASE=<Release-5.5 descriptor>` `[L, 2026-11-25 20:22-20:39]` | |
| options (`ipc/build.py --arch both --target test_options`, both trees) | 1,121 passed, 0 failed in each tree |
| farm guard (`proxy/tests/test_farm_guard_proxy.py`) | 11/11 (the ASan unit 62 checks) |
| dbfp (`proxy/tools/test_dbfp.py --src <5.5 copy> --db <5.5 test db>`) | PASS (the 5.5 database's fingerprint, replay of 2,559 post-processing writes) |
| pytest: `tools/d10/tests/test_pin_defaults.py`, `tools/drift/tests/test_run_pair.py`, `perf/speedkit/tests/test_speedkit.py`, `proxy/tests/test_purity_drops.py`, `test_build_settings.py` (with `C5_PROXY_BUILD_OUT`), `tools/harness/tests/test_hi_coverage.py`, `proxy/gen/tests`, `tools/release/tests/test_descriptor.py` | 158 passed, 13 skipped (all 13 in test_hi_coverage: `lupa` is not installed on the workstation, as on main); the model's `source.commit` = the lock's commit checked by hand |
| `tools/harness/tests/test_safety_static.py` | 55 passed |
| units (`server/tests/units/build_units_tests.py` + `test_units_runner.py`) | 7 passed (214 s) |
| the verify leg (checklist step 12: `proxy/tools/run_proxy_game.py`, Tiny 0-50, `--mode proxy --fail-on-unexpected`, strict, local answers and verify, the UI-Lua mirror, `VP_PROXY_PURITY` = the derived purity file of a2627c8ac) | run 2 **PASS** in 92 s: server `result: passed`, host Lua errors 0, the mirror loaded 140 method keys and 29 Lua names, **1,200,288 UI-Lua hits verified, 0 mismatches**, the proxy replaced the DLL's `Exe` table in each of the three registrations ("11 of 11 functions"). Run 1 played the same 50 turns (1,200,030 hits verified, 0 mismatches, server passed, the proxy detached in order) but the civ5-headless host then hung for 120 s after printing its call summary, between `newgame-report.json` and `host-report.json`, and the harness ended it (exit 57005): not reproduced in run 2; cause not established (section 9). Evidence of run 1 kept: `F:/c5-64bit-work/0765/evidence/verify-run1/` |
| render ctest (`render/build.py --test --build-dir <dir> --game-dir <a 4390913 install> --exe <its EXE>`) | 109/109 passed, 0 skipped (1,083 s) `[L, 22:52-23:15]` |
| rule A (`server/tests/render/run_all.py --only offline-equals-live --only scenebundle-py`, `C5_SCENE_LIBRARY` = the C: copy of 17 bundles, MC-B's slot #16199) | offline-equals-live 18 passed, 0 failed (907 s); scenebundle-py 34 passed `[L, 23:18-23:45]`. render-host and scenebundle-programs not run: server.exe draws nothing new (MC-B's reading) |

### 8.4 The product-path game on a VP 5.5 install `[L, 2026-11-25 20:43-20:58, c5h-55-c]`

Instance c5h-55-c (profile mods55: VP 5.5 as mods, EXE build 26577625), MC-B's grant #15643 after the owner's play
window. Driver `research/03-dll-engine-boundary/evidence/0765/p3_game.sh <inst> <step>`: `lay.py prep` of the build
`x64final` (x64 DLL 05F9B101..., server.exe 8CBA59D6..., proxy 966A85B3...) with the composite of this branch's shell
build, `helper on` (ServerRenderer = 1); the save REL55-TINY_0008 (0766's, sha256 6d1f3a10...) put in as
R0765-TINY_0008 by `saves_snap.py put`; `frontend.py prepare --mods cp,vp,eui --load R0765-TINY_0008`; `cli.py launch`
(start-up watch ok, guard log clean); the harness checklist `python -X utf8 -m tools.harness.checklist --instance
c5h-55-c --turns 10`; one picture; `cli.py stop`; `lay.py collect`, `saves_snap.py restore`, `lay.py restore` (ok,
`provision.py verify --quick` ok), `cli.py leftovers` clean, the claim released 20:58.

Result: the load reached the map; turns 8 -> 18 (ten AI turns, 42 s), the checklist's status, UI states, city screen,
tech tree, diplomacy overview, the save (`UI.SaveGame` on the main thread: c5h-c5h-55-c-t0018-204741.Civ5Save,
268,255 B, settled in 5.4 s; kept with the other saves of the run in `F:/c5-64bit-work/0765/evidence/pp-c5h-55-c/saves_moved/`),
unit selection and hex picking all passed; the leader screen was skipped (no AI major met by turn 18 on this map), so
the checklist's verdict reads "incomplete". Lua.log: 0 runtime errors. The pictures are in the Visual Checks feed
(issue 0765, DIAGNOSTIC). The server's last line says "the engine is gone ... PeerDied": the harness's stop ends the
EXE by a terminate, as it always does.

What VP 5.5's Lua got from the proxy's `Exe` table (vpproxy.log of run 20261125-204556-37764-1):
- the DLL's table replaced in each registration state ("11 of 11 functions registered by the DLL");
- `Exe.TryDisableEngineYieldIconManager`: `false, "not_implemented"` (YieldIconManager.lua at load, 46.6 s);
- uidraw: the EXE checks passed on build 26577625 and the stream grew to 4 MB / 16,384 commands at 48.86 s, after 2
  frames; InGame.lua's first `Exe.TryGrowUICommandStream()` came at 104.8 s (after the load screen) and got `true`:
  its loop ended at once, nothing was grown twice;
- `Exe.CanScheduleResync`: `false, "not_ui_thread"`, called from `GameEvents.PlayerDoTurn` on the game-core thread
  (thread 45152, not the ui channel's): the answer upstream's own function gives there.

### 8.5 The 43-civ variants on the new pins `[L, 2026-11-25 21:00-22:51]`

`bash research/03-dll-engine-boundary/evidence/0765/p4_m43.sh take` (runbook 1.10: `take_release.py Release-5.5
--base-commit b0bce8c2f... --descriptor <the Release-5.5 descriptor> --tier candidate --carry pins --skip-ia
--major-civs 43`, stages survey, branches (skipped: pins), testdata, lock, build; work on F:, at repository commit
00ba21d8c, repo_dirty 0):

| Build | Result |
|---|---|
| x64 `build_all --major-civs 43` (the take's x64-dll-m43, then build_server --deps) | build_all every step rc 0, unresolved 0, narrowing 799 (0 new, 9 gone); smoke "Release-5.5-28-ga2627c8ac1+m43 Copy", 43 majors; DLL **48C508F6...630E**, server.exe 7FED9120...CB5D, proxy 83CE7C84...3765; 1,753 s + 1,274 s on the loaded F: drive |
| 32-bit m43 (MOD_GLOBAL_MAX_MAJOR_CIVS=43 in the copy's build_vp_clang.py predefines; no fork commit, MC G2) | exit 0 in 554 s; DLL **F42AEE30...7EF9**, "Release-5.5-25-gdc6b8ad488+m43 Copy" |
| layouts | `F:/c5-64bit-work/0765/take43/layout/x64-a2627c8a-m43-p00ba21d`, `.../i0ship-dc6b8ad4-m43`; the take's candidate locks in `F:/c5-64bit-work/0765/take43/deps/` |

One headless m43 game (`p4_m43.sh game`: `server/run_server.py server/tests/specs/continents-large-30majors-fixed-seed.json
--turns 10 --bin <the m43 layout>/bin --fail-on-unserved` with the Release-5.5 descriptor): **exit 0, result passed,
turns 0 -> 10, Lua errors 0**, 70 engine methods left to stubs, none outside the allow-list; "player limits:
MAX_MAJOR_CIVS 43, MAX_MINOR_CIVS 20 (the DLL's, read from Defines)"; the final summary seats the 30 majors in slots
0-29 and the city states from slot 43, as 0334's run on 5.4.8 did (player-limits.md 8.2). Run folder
`F:/c5-64bit-work/0765/m43game/`. The -ia m43 DLL is 0766's (`C:/c5-64bit-work/keep/vp-release/dlls/vp55-ia-m43/`).

## 9. Not verified

- The candidate tier's later stages on the new pins: save test (run_d10), drift (run_pair) and fix checks need 5.5's
  reference saves and counts (tickets 0766/0771); the descriptor of the hand-back has empty `reference_counts`. 0764's
  identity pass (Tiny 0-100, x64 = x86, 32,901 rows) ran on 0764's own builds of the same trees, not on the builds of
  this ticket (LTO builds of one tree differ in bytes: 0764's DLL F6AC13DC..., this one 05F9B101...).
- The instance-aware (-ia) build of i0-vp55 (ticket 0766 made 5.5's instruments patch and its own -ia DLLs); the lock's
  `instance` key says so.
- The verify leg's first run: after a passing 50-turn game the civ5-headless host hung at its exit, between
  `newgame-report.json` and `host-report.json`, and was ended after 120 s (exit 57005); the rerun passed. The host is
  the test tool, not the product; the cause is not established `[I: something at the host's process exit after the
  game DLL (here the proxy) was unloaded]`. Evidence `F:/c5-64bit-work/0765/evidence/verify-run1/` (host stdout, vpproxy.log,
  server log). A repeat with a dump of the hung host would settle it.
- `Exe.TryScheduleResync`, `Exe.TryEnableTunerInMultiplayer` and their `Can` forms were not called in a multiplayer
  game (the product route is single player); `Exe.TryDisableEngineYieldIconManager` is refused, so the engine's own
  YieldIconManager keeps raising `Events.ShowHexYield` (its cost on a large map not measured).
- The product game: one Tiny save, ten AI turns, no human move; the leader screen not opened (no AI met).
- The localization database built here is not used by the release driver's stages yet (testdata still names the base
  release's); not compared string by string with what a game with VP 5.5 activated would merge.
- The purity rows re-audited here: by reading; the UI-Lua mirror then verified 1,200,288 hits with 0 mismatches in the
  verify leg (Tiny 0-50), the only live check of the re-audit.
