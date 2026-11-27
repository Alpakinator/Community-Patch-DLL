# The headless host (`civ5-headless` / `c5h`)

The existing program that runs the unmodified VP DLL without the game, and the model for the 64-bit server.

## 1. What it is

| Fact | Value | Source |
|---|---|---|
| Checkout | `~/Documents/civ5-headless` (ruskyandrei, GPL); run as `./c5h.cmd ...` from that folder | dll-unit-test-study.md, headless-dll-ab-test.md |
| Status | 0.3.0 (README) | README.md |
| Size | ~12,000 lines of C++ | thin-client study section 1 |
| Bitness | 32-bit (x86), built with MSVC 2008 (VC9) like the game DLL; links `/LARGEADDRESSAWARE`; `tools/build_host.py` uses `/MACHINE:X86`; one 32-bit asm file `host/engine/HHInitLocks.asm` (a `__thiscall` export) | why-32-bit.md |
| Engine contract | Implements the 22 C exports (`host/civ5headless.def`), `ICvEngineUtility4`, `ICvUserInterface2`, script system, `Database::Connection*`; loads the three closed Firaxis DLLs | dll-unit-test-study.md |
| Fidelity | Plays the unmodified DLL turn-for-turn identically to the real game, with real `.Civ5Save` files in both directions. **Except games loaded from a save** (corrected 2026-11-16, issues 4655 and 0452): its load never called `ICvMap1::RecalculateLandmasses()`, which the EXE calls after the player reads, so a loaded game played with the plots' water flags all false and diverged from the real game at once. Fixed on branch `fix-recalculate-landmasses-after-load` (60716ff, the fork only); the fixed exe is `out/Release/20261116-0452-landmasses` (SHA-256 `bb8d3542...65f7`), the in-use `out/latest.txt` build is not; with it a loaded M and H scenario play the client's game, statscmp-identical over 25 / 10 turns (`server-host.md` section 17) | thin-client study; server-host.md 17 |
| Speed | 7-22x faster than the real game | thin-client study; why-32-bit.md (from verification.md) |
| Memory peaks (VP) | tiny games ~350 MB; Standard to turn 281: 442 MB; for comparison the real game 1.5-1.7 GB | why-32-bit.md |
| Throughput | Standard map, 8 majors: 200 turns in ~10 min, ~370 MB | headless-dll-ab-test.md |
| UI | Does not run the game's screens; emulates UI side effects with small Lua **UI emulation profiles** (`profiles/<name>/profile.json`, `--profiles DIR`, `--no-profiles`) | thin-client study; profiles.md |
| Parallelism | Many host processes at once (`c5h queue`, verification suite), not one large process | why-32-bit.md |

### `engine-contract.md`

`~/Documents/civ5-headless/docs/engine-contract.md` (1,009 lines) records what `CivilizationV_DX11.exe` does at the
boundary, from trace builds of the real game (runs dated 2026-10-25), labelled [M] measured / [S] read in source /
[I] inferred. Facts relevant to the server:

- New game runs on the UI thread, the turn loop on a separate game-core thread; the DLL chooses its pathfinder and
  trade-path caches by `IsGameCoreThread`. The host answers `true` always; the contract says "emulate the role, not the
  thread".
- `Update` runs on the core thread while the UI thread keeps calling the DLL; no state-changing inbound call overlapped an
  `Update` in the logged lines.
- One `Update` per alive player per turn; median ~105 ms gap between Updates in the real game.
- `send*` calls with a `Response*` must be queued and delivered after the current `Update` returns and before the next.
- Autosaves write the game only at turn 0 and every 10th turn (`TurnsBetweenAutosave = 10`), synchronously on the core
  thread.
- Random civs and city-states were originally chosen from the EXE's clock seed before seed pinning; resolved in commit
  255e26ba2 (pregame seeds applied in `setGameStarted(true)`).
- At the time of the traces the host's four GameEvents dispatchers returned false without running listeners, unlike the
  engine (real semantics measured in section 10.1; whether the host has since changed is not checked here); hooks with
  listeners in a 100-turn run included EUI UI-script hooks (`PlayerDoTurn`, `SetPopulation`, `UnitPromoted`,
  `CityConstructed`, `CityBoughtPlot`).

## 2. How it is used for DLL A/B tests

From `headless-dll-ab-test.md` (worked 2026-10-30, extended 2026-11-07):

1. Game folder: the toolkit bridge's copy,
   `%USERPROFILE%\Documents\verns_civ5_vp_toolkit\verns_civ5_vp_toolkit\headless_data\game\VP_MODPACK_5_4_8_NEUTRALS\Sid Meier's Civilization V`
   (the toolkit folder name is doubled), passed as `--game-dir`. *Corrected 2026-11-10 by the librarian (path
   reported by epic 1300, checked on disk 2026-11-10 [L]); it read
   `verns_civ5_vp_toolkit/headless_data/game/...`, one level too shallow. The same short path is in
   `../06-tooling-and-workflows/headless-and-parallel-testing.md` section 1 (epic 1100's file), which this
   note supersedes until that file is corrected.*
   **Run instrumented headless builds with `VP_MEMIMPORTS=0` or `--host-arg=--keep-dll-loaded`:** with MemoryImports on, the host crashes at shutdown,
   right after it unloads the game DLL: the stale import slots MemoryImports leaves behind, section 5 [C, dump, ticket 0484, 2026-11-17]
   (*corrected 2026-11-17 by ticket 0484; it read "cause not investigated; very likely the stale import slots ... [I for the host]"*) [L, epic 1300 task 1301, 2026-11-10; `tools/i0/README.md` and
   `../04-memory-investigation/leaks-and-growth.md` on branch `claude/jolly-kare-987ea6`]. Always pass `--dll` (the toml's `dll` path is dead). The toml's pinned `db`/`loc_db` (5.4.8
   cdrift, `SQLITE_LOGGING=1`, events off) apply by default.
2. Spec map script must be the modpack path, e.g. `Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS/Mods/(2) Vox Populi/Mapscripts/Continental_Drift.lua`; add `--no-mods`.
3. **Games are deterministic for fixed `--seeds`.** Build the baseline from `git worktree add --detach <scratch> master`
   plus `build_vp_clang.py --config release` (LTO ~5 min; both builds can run at once), play both DLLs on the same seeds.
4. Each run gets its own `<run-dir>/cache/stats.db`; diff them with `ATTACH` (Windows-style `C:/...` paths). Lua probe
   prints land in `<run-dir>/Logs/Lua.log`; `--lua-probe` can force rare events (`Player:SetHasPolicy`,
   `Player:InitUnit` + `Unit:PushMission`).
5. DB compatibility: a pre-5.4.7 master DLL works with the pinned 5.4.8 DB. **Upstream master 59c50bc00 (post-5.4.8) does
   not** (2026-11-07: `no such table: UnitPromotions_TerrainHeal`). Build a matching DB from the checkout's mod files (~5 s):

   ```
   ./c5h.cmd dbcache --snapshot cache/db/snapshot/4DF730138BA1F0EE/snapshot.db --mods-dir <repo root> --mod d1b6328c-ff44-4b0d-aad7-c657f83610cd --mod 8411a7a8-dad3-4622-a18e-fcc18324c799 --dll <dll>
   ```

   The explicit `--snapshot` is required (the default Steam-folder snapshot reports benign WB_Script errors and dbcache
   refuses it; snapshot 04E7DFA2784E8518 is polluted with VP tables). Copy the DB, flip `CustomModOptions`, pass `--db`,
   and run with the same `--mods-dir/--mod` pair and a stock spec (`examples/specs/continents-standard-fixed-seed.json`).

The same database mismatch hit the live boundary benchmark (a fresh DLL against the installed 5.4.8 DB), which is why it
has no second-save comparison.

6. **c5h's own per-turn Lua is part of the game it plays (one-off 9044, 2026-11-13) [L].** `c5h run` executes its
   checkpoint Lua after every turn (`--checkpoint-every`, default 1; also at `--detail summary`, which only deletes the
   files afterwards). Its STRENGTH metrics call `CalculateGoldRate` and `GetScience`, which on DLLs without the bug 0116
   fix store the unit maintenance the AI reads later in the turn, so c5h plays a different game than a host without
   that Lua (the x64 server) in about 1 game in 4 of 300-400 turns. The pinned i0-ship DLL in c5h with
   `--checkpoint-every=1000` equals the x64 server game exactly. Comparing a DLL across hosts: give c5h
   `--checkpoint-every=1000`, or use DLLs with the fix (`x64-dll-port.md` 9.10). Comparing two DLLs inside c5h is
   unaffected (both arms run the same Lua). The c5h author fixed the same defect in their own fork (civ5-headless
   `docs/history/host-ledger.md`, "Second frame-count dependence").

## 3. The unit-test study (2026-11-01)

Artifact: https://claude.ai/artifact/Kd5bceBb8XhuAqw9G4TmgT. Conclusions:

- A standalone test exe of "pure" code is a poor fit: anything touching `gGlobals` or `CvAssertDlg` links 169 of 172
  objects; every test binary must be 32-bit VC9 ABI (`/MD` msvcr90); VC9 headers lack `<chrono>/<atomic>/<mutex>/<thread>`,
  so Catch2, doctest 2.x and gtest >= 1.10 are out (hand-rolled C++03 registry, or doctest 1.2.9 / gtest 1.7 / Catch 1.12,
  all unverified under VC9).
- `civ5-headless` already implements the whole engine contract, so **it is the test fixture**.
- Without the DB cached, `gCustomMods` flags are zero, so a DB-less fixture runs Community Patch rules, not VP.
- Recommended order: (0) `VP_SELFTEST`-gated in-DLL tests plus `Game.RunSelfTests(filter)`, run via
  `c5h run --turns 1 --final-extra`; (1) `build_vp_clang.py --tests` exe over Debug objects with a fake-exports file for
  the pure tier in GitHub CI; (2) fixture helpers (one city / two players / edge entities); (3) port `HHDigest` from
  ruskyandrei's fork and run invariants every autoplay turn.
- History survey (master 2024-11 -> 2026-10, 74 diffs): 53 pure-logic / 9 whole-game / 12 crash. Best-value tests include
  save -> load -> save byte identity and reset == fresh (directly relevant to the x64 `size_t` trap).

Three latent bugs found (not fixed, not reproduced in game):

| Bug | Location | Note |
|---|---|---|
| Enum sizing typos | `CvEnumsUtil.h:223-227` sizes `SpecialistTypes` by promotion count; `:73-77` sizes `CivilizationTypes` by feature count | On master since d698bf454 (2021); two oversized `CvCityCitizens` maps are serialized, so a fix changes save format |
| `plotDistance` under X-wrap | `CvGameCoreUtils.cpp:454-469` | Underestimates for distances > W/2 (own "fixme" comment; shown in a Python model only) |
| Branch mute-vector OOB | `CvPlayer.cpp:38829-38836` on branch `instant-yield-triggers` | `m_abInstantYieldNotificationsDisabled` length-prefixed on load but indexed unchecked; old saves + `NotificationLogPopup.lua` read/write past the end for types 64-73 |

The arena-API study (2026-11-01, https://claude.ai/artifact/FqsM8hhwFRMvZeHUvYE2Hz) relies on the same property: RNG is
mostly a stateless seeded hash, so **fixed-seed headless replay is the behaviour-preservation regression gate** for
refactors.

## 4. Why the 64-bit server is modelled on it

- It "already proves the runner half": enough of the engine contract to play the unmodified DLL identically, with real
  saves (study section 1).
- Package B of the thin-client plan is "port the headless host, serve two client channels, own the turn loop, database
  and saves" (50-100 agent-hours), gate "plays a game headless with the x64 DLL".
- The headless database builder already opens its own DB copy, which is what the server must do (study section 5).
- REVIEW.md: "The server is the headless host with a renderer; the same binary runs AI tests, replays and the DLL unit
  tests in CI."

What it does **not** do, and the server must add:

| Gap | Why it matters |
|---|---|
| Serve a live UI | It emulates UI side effects with profiles; the server must answer real UI-thread calls and UI Lua from the shell |
| Two threads | It answers `IsGameCoreThread` true always; the server must serve a UI channel and a core channel with the EXE's lock semantics |
| 64-bit | It is 32-bit by design; `why-32-bit.md` argues a 64-bit host loses its main property (matching the real game turn for turn) because of x87/SSE2 drift, and is "weeks of work, mostly the database layer". *The drift argument assumes a VC9 (x87) DLL; the shipped 5.4.8 DLL is clang-cl `-m32 -msse3` with 0 x87 arithmetic, so it does not hold as stated (librarian 2026-11-10; `x64-dll-port.md` section 5, decision-log "Correction 2026-11-10 04:17"). D19 has the server go straight to x64.* |
| Hook semantics | Real-game GameEvents semantics (engine-contract 10.1) must be reproduced if mod Lua runs in the server |

`why-32-bit.md` concludes a 64-bit build "would only make sense as a separate simulator for DLLs built from source, with no
claim of matching the real game". That is exactly the position the 64-bit server is in; the project accepts the drift
(all players on the same build).

## 5. The exit crash with an instance-aware (`-ia`) DLL (ticket 0484, 2026-11-17)

**What and where [C, dump; L].** c5h with the pin's `-ia` build (`i0ship-26234371-ia`, `3C2F47DA...`) ends every run with
exit 0xC0000005 about 1 s after the final save, with nothing in `Headless.log` after `Lua shutdown: N thread(s) never
freed` (0461: 11 of 11 runs on oldpc, `upstream-13522-address-space.md` 3.1). Reproduced on
the workstation under cdb (huge save copy, turn 350 -> 351, host `civ5headless.exe` `BB8D3542...` of the testbed base,
2026-11-17 03:59-04:00 EST): the debugger logs `Unload module ...CvGameCore_Expansion2.dll`, then the access violation at
`<Unloaded_CvGameCore_Expansion2.dll>+0xdb13c0` (execute of freed code). Stack with the host's PDB: `Boot`
(`hhmain.cpp` line 1453, the `HHLog("game DLL unloaded")` right after `FreeLibrary(hGame)`) -> `HHFormatV` ->
`std::string` in MSVCP90 -> `std::_Allocate` -> MSVCP90's `operator new` import slot -> the unloaded DLL. Offset
0xdb13c0 is `MemoryImports.cpp`'s `ThunkOperatorNew<2>` in this build (the same offset 0164 named with the `-ia` PDB,
`proxy-design.md` 21.4). MemoryImports (the `-ia` instruments, on unless `VP_MEMIMPORTS=0`) points the MSVCR90 allocator
slots of every loaded module (the host, MSVCP90, lua51_Win32, CvGameDatabase, CvLocalization) at thunks in the game DLL
and does not put them back at `DLL_PROCESS_DETACH`, so the first allocation after the host's `FreeLibrary` ends the host.
The host's own crash handlers allocate too (the vectored handler's `Report` -> `HHFormat`), fault again in the same
place (five nested first-chance AVs in the dump) and log nothing, which is why no `HHCRASH` line appears. Evidence:
`server/evidence/0484/` (`crash-stack.txt`, the records and run ends of both arms, `ws_runs.py`; the 137 MB dump stayed in
scratch on F:).

**Not with the plain pin [C].** `i0ship-26234371` (`F92881A3...`) has no MemoryImports (no `VP_MEMIMPORTS` string in its
bytes, which the `-ia` build has), so nothing points into it after the unload; its c5h runs end with exit 0 (0155's laptop
runs of 2026-11-13, 3507's x86 pairs on oldpc and the laptop [L]). No new plain-pin run was made.

**What is lost [L].** Everything the run writes during play is on disk before the crash: `turn-times.jsonl`, the
checkpoints and `final-state.json`, the final save (`saved final: ...`, logged before the shutdown phase), `CvAssert.log`,
replay data. What the host writes after the unload is lost: `newgame-report.json` and `host-report.json` (host status,
Lua error count, update counts, call summary, shutdown and total timings, host memory). c5h's wrapper then records
`state: failed` with `hostStatus`, `luaErrors` and `updates` null, and `tools/testbed/jobs/hostruns.py` marks the run
`ok: false` (its turn times are kept). A Lua error in such a run would go unreported by the counts. `stats.db` is not
affected (the `-ia` build writes none, `../08-performance/metrics-ledger.md`). The D10 save test (`tools/d10/run_d10.py`),
`perf/turnbench`, `tools/drift` set `VP_MEMIMPORTS=0` and are not affected.

**Dialog box (D155) [L/I].** None: under cdb the debugger took the second chance; on oldpc the wrapper saw the exit
1.1 s after the last checkpoint (07:51:47.7 -> 07:51:48.8 UTC, run-1-A of `20261117-024419-e245`), so no box held the
process. The host's unhandled-exception filter returns `EXCEPTION_EXECUTE_HANDLER` (no Windows Error Reporting box) [C].

**Fix [L].** `tools/testbed/jobs/hostruns.py` `keep_dll_args()` adds `--host-arg=--keep-dll-loaded` (c5h skips the
`FreeLibrary`) for a DLL whose bytes carry `VP_MEMIMPORTS`; `hostruns` (headless series, capacity, `addrspace_ab.py
--headless`) and `longgames.py` (and through it the release driver's whole games) use it. Live: the same run with the fix
exited 0 with `host-report.json` and `newgame-report.json` written, `ok: true` (2026-11-17 04:01-04:02 EST). `VP_MEMIMPORTS=0`
would also avoid it, but changes what an `-ia` run measures (MemoryImports' own memory and attribution), so it was not
chosen. The real fix would be on the fork's instruments (MemoryImports and the Lua allocator hook undoing themselves
at detach, `proxy-design.md` 21.5); not filed: the test builds only, the shipped DLLs carry no instruments.

## Contradictions and uncertainty

- The host's own docs argue against a 64-bit host (memory is not a limit for headless play: 442 MB at turn 281 Standard);
  the project's motivation is the real game's 32-bit wall, which headless play does not hit. Not a contradiction of fact,
  but a difference in goal worth keeping in mind when reusing `why-32-bit.md`'s conclusions.
- "12,000 lines" comes only from the study; not checked against the checkout.

Sources: `research/sources/thin-client-study.html` sections 1, 4, 5, 6; `research/sources/memory-notes/headless-dll-ab-test.md`,
`dll-unit-test-study.md`, `arena-api-scope-study.md`, `civ5-64bit-architecture-options.md`; `research/sources/REVIEW.md`
sections 3, 7; `~/Documents/civ5-headless/README.md`, `docs/why-32-bit.md`, `docs/engine-contract.md`, `docs/profiles.md`
(read in place).
