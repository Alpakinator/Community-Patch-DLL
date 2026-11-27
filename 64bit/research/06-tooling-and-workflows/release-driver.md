# The release driver (`take_release`): premises, design and results

Epic 3500 (team-3500, account B). Started 2026-11-13 by the epic's lead. The epic's description is
`research/07-architecture-decisions/architect-2026-11-13-release-epic-draft.md` (called "the draft" below); the
procedure the driver automates is `new-vp-release-checklist.md` (CL); the scope note behind both is
`research/07-architecture-decisions/architect-2026-11-13-release-automation-scope.md`. This file is the epic's
research file: premises checked, the contracts between the work packages, and every result. W7 completes it.

Marks: `[C]` read in source, `git` or a file; `[L]` measured in a run; `[I]` inferred.

Scratch folders (2026-11-14 12:31, lead-3500 #3, MC #608, at the epic's close): the epic's scratch on C: and D: is
deleted. The scratch files this file cites are kept in `evidence/3500-scratch/` under the same sub-paths:
`D:/c5-64bit-work/3500/lead/{p6p7,p7,p10}.sh` -> `evidence/3500-scratch/lead/`, `D:/c5-64bit-work/3500/w1a/p9_variant.py`
-> `evidence/3500-scratch/w1a/`, `D:/c5-64bit-work/3500/w5/scripts/*` -> `evidence/3500-scratch/w5/scripts/`, and
`D:/c5-64bit-work/3500/w5/rel548/{validated-report.json,validated-report.md,state.json}` -> `evidence/3500-scratch/w5/rel548/`.
Logs, run folders and build trees named below are gone. Still on disk: `D:/c5-64bit-work/3500/{evidence,inputs}` (long-lived)
and `C:/c5-64bit-work/3500/releases/Release-5.4.8` (3502's work folder for done A, cited in W1b).

## 1. The `[I]` premises of the draft, checked 2026-11-13 21:35-21:41 EST (lead, read-only `git` and file reads)

Sources read: the fork clone of one-off 9045 `D:/c5-64bit-work/9045/fork` (pins 04b1211b8 / 26234371e), the upstream
clone `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL` (`upstream/master` 16596561f as last fetched; a dry-run
fetch shows upstream master has moved on to ba8aafdca, not fetched here), the toolkit game folder and civ5-headless's
pinned databases. Scripts: `D:/c5-64bit-work/3500/lead/p6p7.sh`, `p7.sh`, `p10.sh` (scratch, not committed).

**P6 (five of six port commits leave the 32-bit build unchanged).** By reading every hunk of b25fd45c0, 8e26515e8,
21f396027, f3d2f5669, 47d7664e9 [C]: each change is either guarded (`#if !defined(_M_IX86)`, `_WIN64`, `_M_X64`,
`VP_OUT_OF_PROCESS`, `_MSC_VER >= 1900`) or replaces a type by one that is the same width on x86 (`size_t`,
`UINT_PTR`, `ULONG_PTR` for `unsigned int`/`DWORD`; `min<size_t>`/`max<size_t>` where both operands are 32-bit on
x86; `pair<...>(...)` for `make_pair<...>(...)`). The `_MSC_VER >= 1900` guard in `CvUnit.h` (`swap`) relies on the
32-bit build seeing an older `_MSC_VER`: `build_vp_clang.py` runs clang-cl in the VS 2008 environment
(`vsvars32.bat`) without `-fms-compatibility-version`, so clang-cl takes the version of that toolchain [I, strong;
not printed]. Result: **source-neutral on x86 [C]; binary and behaviour identity still [I]**; W4's evidence (32-bit
DLL built with and without the series plays the same games) is the proof. The 32-bit DLL is not byte-repeatable
(`deps/i0.lock`: `__FILE__`, `__DATE__`, link time), so the proof is game output, not DLL bytes.

Correction to the draft's W4 text: the crash handler's x64 path is **not a stub** at the pin. b25fd45c0 stubbed
`AppendStackScan` for non-x86; 8e26515e8 then ported the scan to x64 (`ULONG_PTR` slots from `Rsp`, x64 CALL
encodings, full-width formats) [C]. W4's crash-handler item is therefore "switch on the compiler's own macros and
review", not "write a 64-bit stack walk"; a real unwinder (`RtlVirtualUnwind`/`StackWalk64`) is optional.

**P7 (upstream master's only new x64 breaks are the four pointer-to-`DWORD` casts in `Exe/`).** **Not true as
stated.** A pattern scan of the 13,275 added lines of `git diff Release-5.4.8 16596561f` in the DLL folders [C]:
- the four `reinterpret_cast<DWORD>` in the new `Exe/` code (diff lines for `GC.GetEngineUserInterface()`, `pFlag`,
  `pManager`, `GetModuleHandleA(NULL)`), as the checklist said;
- **two more `make_pair<CvUnit*, BuilderDirective>(NULL, ...)` sites** in `CvHomelandAI.cpp` (master has seven, lines
  3255-3355; the port's b25fd45c0 fixed the five that 5.4.8 had). This is the exact pattern b25fd45c0 had to fix for
  the modern STL, so it is a compile break [C pattern, I compile]; and the context change makes b25fd45c0 itself
  conflict or apply partially on master [I];
- no inline assembly, no `_M_IX86`/register-name code, no new pointer casts to `int`/`DWORD` outside `Exe/`.
Whether anything else breaks is known only from a compile (W4's port of master).

**P9 (the tag's mod folders plus the headless database builder give a complete test game folder).** **Partly.**
[C]:
- The x86 test runs use the toolkit game folder `verns_civ5_vp_toolkit/.../VP_MODPACK_5_4_8_NEUTRALS/Sid Meier's
  Civilization V` (205 MB: the base game and DLC copied from the Steam install, plus `Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS`
  with nine mod folders: (1) Community Patch, (2) Vox Populi, (3a) EUI compatibility, (4a) Squads, Global Neutral
  Flavors, Quick Turns, autoplay, Hinin's Musashi, a Congress demo mod; plus `VPUI`, `VT`). The tag holds six mod
  folders: (1), (2), (3a), (3b) 43 Civs, (4a), (5) Modpack Maker. So the toolkit's extra mods are not in the tag.
- The pinned database `civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db` (`bd9b8aa1...`) is a
  **snapshot** of `pinned/vp-modpack-5.4.8/Civ5DebugDatabase.db` plus the Continental Drift map script and the
  localization attach (its `build/plan.txt`), not a build from the tag's folders. Its content before that is the
  game's database on the toolkit modpack's mods without Global Neutral Flavors: section W1a.2 (3501, 2026-11-13; this
  line said "not written down here" before).
- `run_d10.py` takes only `(1) Community Patch` and `(2) Vox Populi` from the fork at 7307aefdf (`MOD_FOLDERS`) as the
  mods registry and VFS; with the pinned database the folders only give the registry (`server-host.md` 12.3).
Consequence for W1: for a new release the base game folder stays (it is the Steam install's, release-independent),
the modpack folder and the test database must be made from the tag. Whether `c5h dbbuild` of the tag's (1)+(2) at
Release-5.4.8 reproduces `bd9b8aa1...` is the first check of the test-data stage (3501); if it does not (the extra
mods of the toolkit), the descriptor for Release-5.4.8 names the existing pinned database so that done A holds, and
a new release gets a built one (every reference count changes with a release anyway).

**P10 (a new release does not promise to load the 5.4.8 reference saves).** **Weaker than feared** [C]: the mod
versions every save names are the same in every tag from Release-5.4.5 to upstream master 16596561f (`(1) Community
Patch (v 151).modinfo`, `(2) Vox Populi (v 17).modinfo`), so the EXE's mod-version check does not refuse a 5.4.8 save;
the one changed loader in master's diff (`CvEspionageAI` vectors) was written to read older saves. Whether every
loader of the next release still reads a 5.4.8 save is [I] until a run tries. The driver tries the old reference
save first and makes a new one (W5) only when it fails or the comparison needs one.
**Correction 2026-11-14 02:14 (lead, from 3507's dry run, MC #468) [L]:** upstream master's builds do NOT load the 5.4.8
huge reference save E2000-HUGE_0350 (x64: `CvGame::Read` reads past the end at `CvGame.cpp:11047`; 32-bit: an access
violation in the load). The mod-version check passing (above) does not make the save loadable. A release cut from master
needs its own huge reference save through the validated tier's `refsave` path (W5); 5.4.8 stays the release for
increment 2. Whether upstream keeps old saves loadable is upstream's choice (MC #468). (Replaced: "weaker than feared".)

**MC rulings of 2026-11-13 22:20 (epic thread #203).** (1) The test database of a new release is what a player installs
from that tag: Community Patch + Vox Populi, plus the EUI compatibility files when the tag ships them; not the toolkit's
extra mods (an `--extra-mod` option stays, off by default). A comparison with a 5.4.8 baseline built on the toolkit
set names the mod-set difference and is not called like-for-like. (2) Corrected by the MC 22:22 (thread #206): the shipped pack manifest lists only the tag's committed 32-bit DLL
(`262aee88...` for 5.4.8); `4e91c58a...` is the local toolkit build (5.4.8 plus two SQLite pragmas), not a
published binary, and appears only in a test-only manifest entry for the test instances; any other hash falls
back to 32-bit with one LOUD line naming it (`tools/release/README.md` section 5). (Replaced: the 22:20 text that
called 4e91c58a the published installer's DLL.)

Side finding for W3 [C]: each release tag commits the 32-bit DLL that players install, at
`(1) Community Patch/CvGameCore_Expansion2.dll` (last changed by the commits "5.4.8 Release" 7307aefdf, "5.4.7
Release" 44c1e14ff, ...). The DLL pack's manifest therefore names the SHA-256 of **that** file (the player's DLL),
not of the project's i0-ship build. **But** (3501, 2026-11-13) [C]: the toolkit game folder's modpack DLL
`Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS/Mods/(1) Community Patch/CvGameCore_Expansion2.dll` (copied from the Steam install
on 2026-10-28, `headless-game-copy.json`) has SHA-256 `4e91c58a...3f82`, while the tag's committed DLL
(`git show Release-5.4.8:"(1) Community Patch/CvGameCore_Expansion2.dll"`) has `262aee88...f16e`. So "the player's
DLL" is not one file: a modpack-form install can carry a different 5.4.8 DLL from the tag's. Which one a player of the
mods form or of the installer has was not checked (the owner's install was not read). W3/3504: a manifest bound to one
SHA-256 boots the 32-bit fallback for the other.

## W1a (3501): the release descriptor and the test-data stage

Issue `tracker/issues/3501-release-descriptor-and-test-data.md` (worker W1a, 2026-11-13 21:45-). Scratch
`D:/c5-64bit-work/3500/w1a/` (scripts, logs; not committed); the stage output
`D:/c5-64bit-work/3500/releases/Release-5.4.8/testdata/`.

### W1a.1 The descriptor and the tools that read it [C, unit tests 2026-11-13]

- `tools/release/descriptor.py` reads `tools/release/releases/<tag>.json`; `releases/current.json` names the default
  release; `C5_RELEASE=<tag>` in the environment overrides `current.json` (for the driver's runs of other tools on a new
  release; since 2026-11-16, 3512, it may also name a descriptor file, section 3512). Fields (format 1): required `format`, `tag`, `upstream_commit` (40 hex), `mods` (`folder`, `guid`,
  `version`, activation order), `game_dir`, `test_db`, `test_db_sha256`, `loc_db`, `reference_saves`
  (`{name: {path, sha256, turn}}`), `reference_counts` (`{game: rows}`), `economic_strategies`; optional
  `loc_db_sha256`, `stock_x86_dll` (+`_sha256`), `player_dll_sha256`, `test_db_source`, `reference_counts_pins`,
  `notes`. A missing or unknown field, a wrong type, a tag that is not the file's, a missing file or bad JSON raises one
  `DescriptorError(file, field)`. `python -I tools/release/descriptor.py [--tag T] [--field F] [--check-files]` (exit 0,
  1 = a named file missing or its hash differs, 2 = bad descriptor).
- `Release-5.4.8.json` holds the values the tools hard-wired: `upstream_commit` 7307aefdf985..., CP
  `d1b6328c-...:151`, VP `8411a7a8-...:17`, the toolkit game folder, the stock DLL in it (`4e91c58a...`), the tag's
  DLL hash (`262aee88...`), `test_db` = `civ5-headless/pinned/vp-modpack-5.4.8-cdrift/Civ5DebugDatabase.db`
  (`bd9b8aa1...`), `loc_db` its `Localization-Merged.db` (`ff9a0997...`), the huge save `E2000-HUGE_0350` (turn 350,
  `c499d3c6...`; its path is the durable copy `F:/c5-64bit-work/reference-saves/Release-5.4.8-HUGE_0350.Civ5Save` since
  2026-11-18, ticket 0533, section 0533; was a temp folder, gone), the reference counts 30,666 / 351,496 / 683,123 (9045's pins), `economic_strategies` 37 (= rows of
  `AIEconomicStrategies` in the test database). `--check-files` passes on the workstation (2026-11-13 21:51).
- Converted to the descriptor (old value kept as the 5.4.8 descriptor's; the test `tools/release/tests/
  test_descriptor_tools.py` asserts each, and greps the code lines of these files for any 5.4.8 path, commit, hash,
  `GUID:version` pair or `default=37/350`; on the pre-3501 files the same grep finds 19 lines, now 0):
  `tools/d10/run_d10.py` (`TOOLKIT_GAMEDIR`, `MODS_DB`, `MODS_COMMIT` now the full hash, `MOD_FOLDERS`, `DEFAULT_HUGE`,
  `HUGE_SHA256`, the huge game's turns = save turn, +25, +50), `tools/drift/run_pair.py` (`GAMEDIR`, `OFFICIAL_X86`,
  `LOC_DB`), `tools/drift/run_huge.py` (`DB`, `MODS`, `--start` default), `server/run_server.py` (`GAME_DIR`,
  `DEFAULT_DB`, `DEFAULT_LOC_DB`), `server/tools/explore_sets.py` (`--strategies` default), `tools/i0/ab.py`
  (`GAME_DIR`), `proxy/tools/test_dbfp.py` (`CLOSED_DIR`, `DB`), `proxy/tools/test_proxy_rt.py` (`DEFAULT_LUA_DIR`),
  `tools/testbed/jobs/longgames.py` (`MODS`; granted after 0156, 22:20; its game folder, database and localization are
  the testbed bases' `TESTBED_*` paths and `HUGE_SAVE` the base's `TB-HUGE_0350`, which follow a release at CL step 17;
  the job ships `git archive HEAD`, so `tools/release/` is there; `descriptor.py` parses with the Python 3.8 grammar).
  `perf/turnbench/turnbench.py`: only the pin defaults (`--x64-bin`, `--x86-dll` = `run_d10.DEFAULT_SRV_BIN` /
  `DEFAULT_X86_DLL`, the `release` keys of `deps/*.lock`, as 0133 did for run_d10/run_pair); its database follows
  through `run_d10.MODS_DB`. `tools/d10/savecmp.py` has no 5.4.8 constant (its "37 in VP 5.4.8" is a comment; the
  parser finds N itself). Left as they are: `tools/instances/provision.py`'s profile name `mods548` (the owner's install's
  mods form, CL step 2: the owner decides when their install moves), version-string fixtures `Release-5.4.8-...` and the D18
  ancestry fact `7307aefdf` in `tools/d10/tests` and `tools/instances/tests` (parser test data, not defaults).
  Added by 3502 after 0131 handed them back (2026-11-13 23:07): `tools/x64-dll/build_all.py` and both
  `tools/x64-dll/*/ab/run_ab.py` (section W1b.5).
- Two hidden 5.4.8 defaults found and made explicit [C]: (1) the x86 runs of `run_d10` (tiny, standard: `--no-mods`)
  and `run_pair` passed no `--db` / `--loc-db`, so c5h took its own `civ5headless.toml` (`db = pinned/vp-modpack-
  5.4.8-cdrift/Civ5DebugDatabase.db`, `loc_db` likewise); they now pass the descriptor's (the same files for 5.4.8;
  c5h's `dbcache.database` uses `--db` exactly as the configured `db`). (2) `run_server.py`'s default database was a
  copy `AppData/Local/Temp/claude/cv64-deps/testdb/vp548-cdrift-Civ5DebugDatabase.db`; it has the same SHA-256
  `bd9b8aa1...` as the pinned file, so the default now names the pinned file (a Temp copy can vanish).
- Suites 2026-11-13 21:56-21:58 (through `quietlock.py run`): `tools/release/tests` 35 OK, `tools/d10/tests` 51 OK,
  `tools/drift/tests` 4 OK, `tools/instances/tests` 81 OK, none skipped.

### W1a.2 Premise P9: does `c5h dbbuild` of the tag reproduce the pinned database? No [C, L 2026-11-13 22:07-22:15]

The stage (`tools/release/stages/testdata.py`, W1a.3) built `(1) Community Patch` + `(2) Vox Populi` of Release-5.4.8
(git archive of 7307aefdf) on a DLC snapshot of the toolkit game folder without its modpack, post-processed with the
tag's DLL: `db/Civ5DebugDatabase.db` `5518cd5b...` (414 update files; c5h's known "missing CoreGameOptionTextChanges.xml"
note; `SQLITE_LOGGING` 0 as built, set to 1). Compared table by table with `bd9b8aa1...` (row multisets, columns by
name; `compare-Release-5.4.8.json`): 830 tables in both, **795 identical, 35 differ**, none only in one. The
differences are the toolkit modpack's extra mods, not the builder:

| Difference in the pinned database | Source |
|---|---|
| one more civilization (`Civilizations`, `Leaders`, `Traits`, 38 `Leader_Flavors`, 198 `Diplomacy_Responses`, 35 city names, 10 spy names, 10 icon atlases `HININ_MUSASHI_*`, sounds, a player colour) | Hinin's Musashi for VP |
| `CustomModOptions` `SQUADS` = 1, three `INTERFACEMODE_SQUAD_*` | (4a) Squads for VP |
| `GameOptions` defaults `NO_GOODY_HUTS`, `QUICK_COMBAT`, `QUICK_MOVEMENT` = 1; `MovementRates.TotalTime` 0.01; `AnimationPaths.MissionPath` 0 | Quick Turns |
| `Defines` `TEMPORARY_TOURISM_BOOST_MOD` 25 (built 50), `TOURISM_MODIFIER_OPEN_BORDERS` 10 (15), `..._DIFFERENT_IDEOLOGIES` -20 (-10), one more | Congress 11-002 General Tourism Reduction demo |

A second build with the toolkit modpack's own mod folders (all nine but Global Neutral Flavors: (1), (2), (3a), (4a),
Congress demo, Musashi, Quick Turns, autoplay; post-processed with the toolkit's DLL `4e91c58a`; script
`D:/c5-64bit-work/3500/w1a/p9_variant.py`) leaves **11 tables differing, every gameplay table identical** (819 of 830).
The 11 are the modpack form against the mods form [C by the rows; cause I]: `ArtDefine_*` (5 tables: NULL against ''
and text values, the Modpack Maker's export), `Colors` (17 rows: 14 significant digits against full doubles, e.g.
0.22352941176471 / 0.2235294117647059), `DownloadableContent` (the pinned game has `DLC_SP_Maps` active, the mods form
blocks it; the stage's folder adds the `VP Modpack` package row B5932AE4), `MapScripts` / `MapScriptOptions` /
`MapScriptOptionPossibleValues` (the pinned set has 42 scripts: the base ones, the owner's own `Maps/Communitu_79a.lua`, VP's
Skirmish/Ring as `Assets\Maps\...` overrides and Continental Drift; the build registers all 19 of VP's `Mapscripts`
under the modpack path, 58), `ScannedFiles` (file times and the `VP_MODPACK_5_4_8\Override` paths).

So: the pinned database is the toolkit modpack's mod set (CP, VP, EUI compatibility, Squads, Quick Turns, Musashi,
Congress demo, autoplay; original flavours) in modpack form, and every test on it plays with **no goody huts, quick
combat and movement, Squads on, Musashi in the civ pool and the Congress tourism values** (game options a spec sets
override the defaults [I]). The 5.4.8 descriptor keeps naming it (done A); the stage's candidate descriptor for
Release-5.4.8 also does (`test_db_source` says why). **MC ruling 2026-11-13 22:20 (epic thread #203):** the test
database of a new release is what a player installs from that tag: CP + VP, plus the EUI compatibility files when the
tag ships them, not the toolkit's extra mods. The stage therefore builds the descriptor's mods plus the tag's
`(3a) ... EUI ...` folder (in the database and the stage's modpack; not in the descriptor's mod set, which is what saves
name); `--extra-mod <folder>` stays as an option, off by default. **Any comparison of a new release with a 5.4.8
baseline taken on the pinned database names this mod-set difference (Squads, Quick Turns, Musashi, the Congress demo,
autoplay in the 5.4.8 set only) and is not called like-for-like.**

### W1a.3 The test-data stage [L 2026-11-13 22:07-22:08]

`python -I tools/release/stages/testdata.py --tag <tag> --out <work>/testdata [--fork <clone>] [--base <tag>] [--core
CORE.db] [--dll DLL] [--extra-mod DIR ...] [--compare|--no-compare]`; the driver calls `run(ctx)` (returns `status`
ok/stop/failed, `outputs`, `stop`). Exit 0 done, 1 a step failed, 2 bad arguments, 10 `<out>/STOP` found. Steps and
times on Release-5.4.8 (run 2, through `quietlock.py run`, 91 s): `commit` 0.1 s; `mods` (git archive of the base
descriptor's mod folders; guid and version from each `.modinfo`) 12.7 s; `game` (copy of the base game folder, 8,580
entries, without `VP_MODPACK_*`, plus `Assets/DLC/VP_MODPACK_5_4_8/` = the base modpack's `MPModsPack.Civ5Pkg` and
`Mods/<the tag's folders>`) 20.5 s (88 s on a cold cache, run 1); `config` (a `civ5headless.toml` under `<out>/c5h`,
`C5H_CONFIG`: the stage's game folder, an empty user folder, cache and runs under `<out>`; the core database copied
from `My Games/.../cache/Civ5CoreDatabase.db`, `39157b03...`) 0 s; `snapshot` (`c5h snapshot --out --core`) 6 s;
`dbbuild` 50 s (host 40.5 s; path-as `Assets\DLC\VP_MODPACK_5_4_8\Mods`); `compare` 1 s; `descriptor` (the candidate
`<out>/Release-5.4.8.json`, validated by `descriptor.parse`) 0.1 s. After the run no file in `civ5-headless` or the
toolkit's game folder was newer than a marker made before it (`find -newer`).

- Run 3 (22:21-22:29, after the MC ruling, with `(3a) VP - EUI Compatibility Files` from the tag): `dbbuild` "3 mods,
  417 update files" and the **same database bytes** `5518cd5b...` as run 2 [L]: the EUI compatibility files add no
  database row that survives (their 3 update files are text, which `dbbuild` drops). Times under the 0158 capacity
  window: `game` 371 s, `mods` 41 s, `snapshot` 38 s, `dbbuild` 39 s; 8 min in all. Output named in the issue log.
- `c5h snapshot` exits 1 on this game folder: 18 map scripts `DLC_SP_Maps_2/_3/Maps/WB_Script_*.lua` have no
  `GetMapScriptInfo` [L]. The game does not register them either (no `WB_Script` row in `bd9b8aa1...`) [C], so the
  stage accepts exactly these failures (listed in `testdata.json`) and refuses any other.
- **Since the Firaxis update (build 26577625) the default core database breaks `snapshot`** [C, L 2026-11-20, fifth lead
  of 0590, ticket 0606's take]: `c5h snapshot` exits 1 with 10 errors `columns StrategicViewType, TileType are not
  unique` in the DLC StrategicView ArtDefines (DLC_01 to DLC_06), not the 10 accepted `WB_Script` failures, so testdata
  fails. Cause: the default `core_db` (`tools/release/settings.py`, `C5_CORE_DB`) is the workstation's live
  `Documents/My Games/Sid Meier's Civilization 5/cache/Civ5CoreDatabase.db`, which the new EXE rebuilt on 2026-11-19
  19:56 EST (SHA-256 `e5666c30...`); 0574's rc0c take of 18:07 that day copied the pre-update one (`dec46218...`, kept in
  its work folder `testdata/c5h/core/`) and passed. Workaround: `C5_CORE_DB` naming a copy of that pre-update database.
  The live cache is not a pinned input; proposed to the MC (thread 0590) to pin a durable copy with its hash.
  (Corrected 2026-11-20 by ticket 0612: "rebuilt by the new EXE" names the moment, not the cause; the cause follows.)
- **The cause, ticket 0612** [C, 2026-11-20, worker w6-0590; both files read only on copies with Python's `sqlite3`]:
  the live file is not a core database at all but the game's **fully loaded database**. `dec46218...` (2,304,000 bytes,
  the pre-update cache, copied by 0574 on 2026-11-18) is the base game only: 365 tables, 20 civilizations, 170
  `ArtDefine_StrategicView` rows, no DLC or mod table. `e5666c30...` (5,485,568 bytes, written 2026-11-19 19:56:42 EST)
  holds 832 tables, 45 civilizations (base, expansions and DLC civs), 380 StrategicView rows and Vox Populi's tables and
  rows (`CustomModOptions` 295 rows, `Belief_YieldFromKills` ...). It has the same 832 tables with the same SQL and the
  same row counts as the game's `Civ5DebugDatabase.db` written 23 s later in that start, apart from
  `DownloadableContent` (0 vs 15) and the three map-script tables (empty), i.e. the database after the DLC and mod
  load, before map-script registration. `c5h snapshot` treats `--core` as layer 1 (base XML) and applies every DLC
  package on top, so the DLC rows go in a second time: `ArtDefine_StrategicView` keeps its `PRIMARY KEY
  (StrategicViewType, TileType)` and so gives the errors, while the other tables of the loaded database have lost
  their `unique` and foreign-key clauses (of the 365 base tables 327 differ in SQL: 282 only by dropped `unique`,
  foreign-key and `default NULL` clauses, 45 also by columns the expansions and VP add; all 832 tables have the same SQL
  as the pinned game-captured 5.4.8 database `bd9b8aa1...`) and take **doubled rows silently**: ignoring those errors would
  give a wrong snapshot. Reproduced [L 2026-11-20 16:56, `c5h snapshot` on a copy, the 0612 stage's configuration]:
  41 failed steps, 18 `WB_Script` map scripts plus 23 XML files (22 of them `not unique`; Expansion 6, Expansion2 2,
  Shared 2, DLC_01-06 10, the three SP_Maps 3), a snapshot of 859 objects and 47,805 rows. The "10 errors" of the
  first report are the console's first 10 lines only; the stage's accepted failures are 18 (`WB_Script_*`), not 10. The game's own `Logs/Database.log` of that start (last written 19:57:57) has the same
  `columns StrategicViewType, TileType are not unique` lines (from `Assets\DLC\Shared\StrategicView\...` among others).
  Answer to the ticket's question: **not a schema change by build 26577625 and not another load order; the cache file
  was written from the loaded database (DLC and mod rows in it)**. It is not tied to the new EXE: the old build did the
  same on instance `c5h-mpk` (VP in modpack form, cache of 2026-11-17 12:20:47: 5,482,496 bytes, the same 832 tables
  and the same row counts as `e5666c30...`), and build 26577625 writes a base-only cache on `c5h-new-a` and `c5h-new-b`
  (2026-11-20 15:36 and 15:51). In both full cases the cache was written 2-3 s after `Civ5ModsDatabase.db`, in the
  normal case before it (`c5h-new-a`, `c5h-d`: core first, mods and debug about 70 s later) [C, file times]. Why some
  starts write the loaded database is not established [I: a start whose cache is stale (the update changed the EXE)
  rebuilt it during a mod-activated load]; who started the game on the Steam install at 19:56 is not established
  from these files.
- **The core database is the same for both EXE builds** [C, 2026-11-20]: a content digest (every object's SQL and every
  table's rows as a sorted multiset, `sqlite_stat1` left out) of `dec46218...` equals that of the base-only caches of
  build 26577625 (`c5h-new-a` `91fe4575...`, `c5h-new-b` `91d8c9cf...`) and of `c5h-d` (`d9bd42a4...`): 480 objects,
  15,398 rows each. The file hash changes at every rebuild while the content does not (four files, four hashes, one
  digest; `39157b03...` of 2026-11-13 is presumably the same rows [I: that file is gone]), so the pin is one file, not
  one per rebuild. **A release that serves both builds makes its test data from `dec46218...`.** Build 26577625's own
  base-only core database (`c5h-new-a`'s copy) gives a snapshot with the same content digest (`759dd95e...`, 489
  objects, 22,099 rows) as the pinned file's and the same 18 accepted `WB_Script` failures, nothing else [L, 2026-11-20].
- **The 0612 stage run** [L 2026-11-20 16:52-16:54, worker w6-0590, `stages/testdata.py --tag Release-5.4.8` into a
  work folder of its own, no take; machine loaded]: ok in 115 s (`game` 78 s, `snapshot` 11 s, `dbbuild` 10 s); core
  `dec46218...`; snapshot `4980033c...` with 18 accepted `WB_Script` failures; database `5d7664a2...`; both
  byte-identical to 0574's rc0c take of 2026-11-19 18:07 (same core). `compare` against `bd9b8aa1...`: 795 of 830
  tables identical, as in rc0c.
- **The pin (ticket 0612)**: `settings.py` `core_db` has no default any more (was the live cache); the workstation's
  `deps/local-settings.ini` names the durable copy `C:/c5-64bit-work/coredb/Civ5CoreDatabase-dec46218.db` (read-only
  attribute; game data, never in git, D83). `stages/testdata.py` `CORE_DB_PINS` lists the accepted SHA-256 values
  (`dec462184517315c72753029bc3f84f86244528c31ba505eb7cba5cef45706f2`); `check_core` runs before the stage's first
  step and refuses with one line when the setting is missing (`no core database: set C5_CORE_DB ...`), the file is
  missing (`... is missing (pinned SHA-256: ...)`) or its hash is not pinned (`... has SHA-256 X, not a pinned one`);
  the `config` step checks the copy's hash again. Driver test hook: config `core_db_pins`. Scripts of the comparison
  (`compare.py`, `strategic.py`, `sqldiff.py`, `digest.py`, `report.py`: read only, standard library) in `evidence/0612/`. Re-pin after a game
  update: `new-vp-release-checklist.md` step 2.
- Not rebuilt per release yet: the localization database (the candidate descriptor names the base release's;
  `dbbuild` throws mod text away by design, `database-builder.md` step 4). x86 new games (`run_d10` tiny/standard,
  `run_pair`) run `--no-mods` with the game folder's modpack Lua; with the stage's game folder that is the tag's (1)+(2)
  under `VP_MODPACK_<version>` [I: not yet played on it].
- Disk: `<out>` holds the game copy (about 205 MB) and the databases; it stays for the driver's later stages.

## W4 (3505): the carry as a series, the DLL in upstream shape

Issue `tracker/issues/3505-dll-carry-series-and-upstream-shape.md`. Clone `D:/c5-64bit-work/3500/w4/fork` (`git clone
--shared` of 9045's clone; `origin` = the fork, `upstream` = LoneGazebo, fetch only; scripts in
`research/03-dll-engine-boundary/evidence/3505/scripts/`, not committed).
**Deleted at hand-back (2026-11-13 23:5x EST, D110/D111):** every W4 path under `D:/c5-64bit-work/3500/w4/` named
below (clone, worktrees, builds, game runs). The scripts are kept in
`research/03-dll-engine-boundary/evidence/3505/scripts/`, the comparison outputs in that folder, and the master
port's branches are on the fork (`i0-release-master-ba8aafdc-r2`, `x64-release-master-ba8aafdc-r2`). The 5.4.8
branches were local only: `branches.py` remakes them in about 20 s.

### W4.1 The series and `branches.py` [C, L 2026-11-13]

- `tools/release/series/carry.tsv`: 45 rows = the 39 commits of `Release-5.4.8..26234371e` (linear, no merges) and the
  6 own commits of the x64 pin (`26234371e..04b1211b8 --no-merges`). Classes: 6 port (b25fd45c0, 8e26515e8,
  21f396027, f3d2f5669, 47d7664e9; 26234371e, which only matters with 0084's cef58bbc4), 11 identity (D18 e9d0c4837 and
  its cherry-pick 40ce81aee, D25 50a389682/1512a94b4/5810b3c71, e6f904ad6, D35 ccfd6dd4d, 0084 cef58bbc4/5201480f1,
  0116 84d4d4e9e/ee893bc18), 28 fix (growth set, 0078, 0080, batch 1's 17 upstream picks, 0096, 0103).
- Upstream equivalents [C, `git cherry` and `merge-base --is-ancestor` against upstream/master ba8aafdca, 2026-11-14
  01:49 UTC]: the 17 batch-1 picks have the same patch id as their upstream originals (all in master, none in
  5.4.8); 50a389682 = upstream 2bc6d34a1 (same patch id); ccfd6dd4d = upstream 232ebaf72 (#13552) by content but **not
  by patch id** (so the row names it in `upstream`); the other 21 own rows and the 6 x64 rows have no upstream
  equivalent. So on master as base, 19 rows drop.
- Order: rows apply in file order, the pins' history order. A class-sorted order (port, identity, fix) was rejected:
  26234371e (port) edits code that cef58bbc4 (identity) adds, e6f904ad6 edits 4a8f99c79's code. Dependencies are the
  optional `needs` column (e6f904ad6 needs 4a8f99c79; 5201480f1 and 26234371e need cef58bbc4).
- Picks are `git merge-tree --write-tree --merge-base=<commit^> <head> <commit>` plus `git commit-tree` (git 2.43):
  no worktree, no checkout of the 1 GB tree; the author is kept, the committer is the clone's user, the message gets
  `-x`'s line. A pick whose tree equals the head's is a drop (the base had the change).
- **Acceptance on Release-5.4.8 [L, 2026-11-13 21:55:17-21:55:36 EST, 19 s]**: `python -I tools/release/branches.py
  --fork D:/c5-64bit-work/3500/w4/fork --base Release-5.4.8 --series tools/release/series/carry.tsv --name 5.4.8 --out
  D:/c5-64bit-work/3500/w4/accept548 --compare-i0 26234371e --compare-x64 04b1211b8`: 45 applied, 0 dropped, 0 skipped;
  `i0-release-5.4.8` ec94b32a5, tree 0c0e382a2 = 26234371e's tree; `x64-release-5.4.8` 91aeae115 (merge of ec94b32a5
  into ac719f5a3), tree d17732e35 = 04b1211b8's tree. The linear series gives the pins' trees although the x64 pin's
  history has six merges of i0-ship (done A's branch part). Local branches only, not pushed.

### W4.2 Upstream master ported once (item 3) [C, L 2026-11-13]

Details: `research/03-dll-engine-boundary/x64-dll-port.md` section 12. Summary: on upstream master ba8aafdca the
series as it is stops (exit 10) on 5201480f1 (`SqliteLogger.cpp`); with the release copy
`tools/release/series/carry-master-ba8aafdc.tsv` (five upstream-shape port commits, two resolved i0 rows)
`branches.py` gives 23 applied, 19 dropped, 3 skipped (exit 0, 53 s); `x64-release-master-ba8aafdc` ce47ef342
compiles for x64 (debug, 0 errors) and links with 0 unresolved symbols. The driver's real workflow for a release that
conflicts is therefore: run, read the stop, resolve the row by hand in a worktree, write a release copy of the series
with the resolved commit, rerun with `--series <copy> --replace` (about 2 minutes of tool time per conflict here,
plus the reading). **Release build of the master port: not run** (2026-11-13): deferred by D107 (one LTO link at a
time) and the 0158 window (no LTO link 22:30-23:20); from 23:21 to 23:41 the waiter (take the team link slot, then
require no other `lld-link.exe`) found another team's link (0160's thin-LTO measurements) every 150 s, and D110/D111
then moved large scratch off D:. The script is `research/03-dll-engine-boundary/evidence/3505/scripts/s12_build_master_rel.py`
(build_all.py `--no-gate`, then build_server.py `--deps`); for the next run its OUT goes under
`C:/c5-64bit-work/3500/` (D111). The 10 new narrowing sites need `--no-gate` or a baseline update by build.py's
owner.

### W4.3 The 32-bit proof (item 4) [L, 2026-11-13 22:30-22:43 EST]

Question: does the 32-bit DLL behave the same with and without the upstream-bound commits (the six x64 commits)?
- Arm A `i0pin`: the pinned 32-bit build `<pins_dir>/dlls/i0ship-26234371/CvGameCore_Expansion2.dll`
  (`deps/i0.lock` release, F92881A3...59D5; fork 26234371e).
- Arm B `x64src`: the x64 pin's source 04b1211b8 (= 26234371e + the six x64 commits, tree d17732e35 = the
  `x64-release-5.4.8` tree) built for 32-bit: worktree `D:/c5-64bit-work/3500/w4/wt-x86p`, `build_vp_clang.py
  --config release` under `quietlock.py run`, 2026-11-13 22:20:08-22:30:00 EST (592 s, beside other teams' x64 links);
  `D:/c5-64bit-work/3500/w4/x86-04b1211b/CvGameCore_Expansion2.dll` SHA-256 B44729A6...A147, 17,143,808 B.
- Games: the save test's Tiny (0-100, save at 25) and Standard (0-200, save at 50) fixed-seed games, AI logging off
  (`--host-arg=--no-game-logging`), `--no-profiles`, through `tools/i0/ab.py run` (c5h, both arms in parallel)
  under `quietlock.py run`; script `research/03-dll-engine-boundary/evidence/3505/scripts/s11_games.py`. Then `tools/d10/statscmp.py`
  (`--expect-turns 0 <final>`) on the two stats.db and `tools/d10/savecmp.py` on each save pair.
- Expected: statistics identical, saves identical apart from the version string (savecmp exception a). Both arms
  carry D18, so no order exception applies.

Results (evidence: `research/03-dll-engine-boundary/evidence/3505/`):

| Check | Tiny 0-100 | Standard 0-200 |
|---|---|---|
| Behaviour: `statscmp.py` A vs B, every table, turns 1..final present | **identical**, 15 tables, 30,666 rows (52 s) | **identical**, 15 tables, 351,496 rows (353 s) |
| Writer: each arm's two saves (4 inputs) loaded by both DLLs from one slot path and resaved, `savecmp.py` | 4/4 **IDENTICAL UNDER EXCEPTIONS** (only `dll-version-string`) | 4/4 **IDENTICAL UNDER EXCEPTIONS** |
| Cross-load | each DLL loaded both arms' saves (the 8 resaves above, c5h rc 0) | same |

The row counts equal the save test's reference counts for these games (`deps/i0.lock`: 30,666 / 351,496), so the
games are the save test's. Conclusion: the six x64 commits leave the 32-bit DLL's game behaviour and save bytes
unchanged on these games [L]; with P6's reading [C] this is the 32-bit evidence the draft asked for. It covers the
pins' port commits on 5.4.8; the master port's upstream-shape commits differ from them only in comments, the two
master-only sites (both keep the x86 expression) and the `Exe/` guards (no effect without the define) [C, not run].

**Direct-play saves are not a usable comparison** [L]: the two arms' own turn-25/100 saves differ in 21 byte runs
(Tiny) and 607-610 runs (Standard), but an A/A control (the pinned build twice, Tiny) differs in 622-624 runs: the
game GUID (ASCII and binary at body offsets 11146-11182 and 14288-14304) and other values vary from run to run of one
DLL. Writer identity therefore needs the resave method of the save test (one input, one slot path, both writers),
as `run_d10.py` part 1 does; `tools/i0/ab.py` alone compares statistics only.

**D18 (sorted writes) separately.** Both arms carry it (40ce81aee / e9d0c4837), so it is not under test above. What
it changes, stated plainly: saves written by a DLL with D18 store three hash-ordered containers
(`CvCity::m_miInstantYieldsTotal`, `CvExplorationPlotSet::m_aValues`, `CvGame::m_AdvisorMessagesViewed`) in key
order instead of hash-table order, so their bytes differ from stock 5.4.8's in the order of those records (same
count, same records). The readers insert each element, so old saves load [C, the readers in
`FStlContainerSerialization.h`] and each DLL loads the other's saves (D32's decision text); the save test's huge input
(`c499d3c6...`, a save of epic 2000, before D18 existed [I: from the dates]) loads and plays 350-400 in both pinned
builds [L 9045, `deps/i0.lock` verified]. Not run here: a stock 5.4.8 DLL loading a D18 save.

Not done: whole games on the testbeds (no testbed place was claimed for 3505; the Tiny and Standard games above are
the save test's). Run times were measured beside the 0158 capacity window and other teams' builds (not timing
claims).

## W2 (3503): the model at a source path, purity as data

Details in `research/03-dll-engine-boundary/proxy-design.md` sections 1b (model) and 16.9 (purity); contracts in
`tools/release/README.md` sections 2a and 4.

- **Stage `model` (CL 10)**: `python -I proxy/gen/regen_model.py --src <x64 source> --out <work>/model [--lock
  <candidate lock> | --unlocked --commit <label>]`, about 3-4 min [L 2026-11-13: 170-244 s]. At the pin 04b1211b8
  against the model committed before 3503: generated runtime, `policy-table.tsv` and `slot-proof.json` equal; differences
  only in `interfaces.json` (the CvAStar record) and `lua.json` (line moves, `lGetRealPopulation` long -> long long),
  none reaching generated code. The regenerated model is committed; a rerun compares EQUAL. So at a release, a
  difference that reaches the generated runtime or `policy-table.tsv` is the review signal; record-layout and line
  changes are expected. [L]
- **Stage `purity` (CL 11)**: `python -I proxy/tools/derive_purity.py --src <x64 source> --dll <x64 DLL> --out
  <work>/purity`, about 9 s [L]: `purity.tsv` bound to the DLL's SHA-256 and `report.tsv` (every audited row, kept or
  dropped with reasons). It needs no fork clone and no git at release time: the audit is frozen in
  `proxy/model/purity-baseline.json` (made from the fork at 76992d235 with `--make-baseline`; rerun it after each human
  re-audit, or `gen_pure_list.py --check` and the derivation refuse). At the pin: 143 of 143 rows kept.
- **The stage runs the full purity test (issue 0480, 2026-11-17)** [C, L]. After `derive_purity.py` the stage runs
  `proxy/tools/test_purity.py --src <src> --dll <x64 DLL> --scratch <scratch>/purity-test` (tool `purity_test`, under
  `quietlock.py run`): unit, ASan, derive with its seeded cases, refusals, check, regen. Until 0480 no runner called
  the full test and its derive checks failed on main for ten hours unseen (proxy-design 16.9, 0478). The test needs a
  source where every audited row is kept, so `<src>` is the release's x64 source when derive dropped none, else a git
  archive from the fork of the baseline's `audited_at` (`<scratch>/src/purity-audited-<c8>`; a fork without that
  commit fails the stage and says so). A failed check fails the stage (its `FAIL` lines in the message); outputs gain
  `test_checks` and `test_src` (`release` | `audited`). [L 2026-11-17 07:17-07:18 EST, `take_release.py
  Release-5.4.8 --only purity` on a state whose build entry names the pin source F:/.../src-x64-04b1211b and the pin
  DLL 74588135...] derive 9 s (137 method, 29 Lua, 0 dropped), test 82 s **66/66**, stage ok, test_src `release`. The
  standalone run before it took 107 s (07:05-07:07, five other heavy jobs registered). The audited-source path is
  covered only by the hermetic tests (`tools/release/tests/test_take_release_purity.py`, 6 tests: both sources, a
  failed check, exit 0 without a summary, the audited commit missing from the fork), not live: no fork clone of the
  ticket's own held 76992d235. `proxy/build_proxy.py` runs the same test on an input-hash cache (step 7, below in
  proxy-design 16.9 and `tools/release/README.md` section 4).
- **What a release costs here** [I]: rows whose cited text or a called name's definitions changed are dropped
  automatically (safe: those calls cross); the CL 11 re-audit reads the report, re-confirms or re-cites the rows, updates
  the TSVs and reruns `--make-baseline`. Name-based callee checking over-drops on common names (`GetID`: 26
  definitions), so a large release may drop many rows until that re-audit; that costs crossings, not correctness.

## W1b (3502): the driver `take_release.py`, its state and the candidate stages

Issue `tracker/issues/3502-take-release-driver.md`. Code `tools/release/take_release.py`, `tools/release/stages/*.py`
(all but `testdata.py`, 3501's), tests `tools/release/tests/test_take_release.py`. Fork clone of its own
`D:/c5-64bit-work/3500/w1b/fork` (`git clone --shared` of 9045's clone; nothing pushed). Live state of the 5.4.8 run:
`D:/c5-64bit-work/3500/releases/Release-5.4.8/state.json`.

### W1b.1 Design [C, the code]

- **Command**: `python -I tools/release/take_release.py <tag> [--work DIR] [--from STAGE | --only STAGE] [--tier
  candidate|validated] [--fork CLONE] [--carry series|pins] [--jobs N] [--ia-patch FILE|by-base] [--descriptor FILE]
  [--gate-wait MIN] [--who ID]`. `--fork`, `--carry`, `--jobs`, `--ia-patch`, `--descriptor`, `--gate-wait`, `--who`
  are recorded in the state's `config` on the first run and reused when a rerun leaves them out. `--ia-patch` defaults
  to `by-base` since 0310 (2026-11-15; was 9045's patch): the `build` stage picks the instruments patch by the i0
  commit's upstream base (`new-vp-release-checklist.md` step 3, "The driver picks the patch by base").
- **Exit codes**: 0 the tier is reached (with `--only`: that stage ran ok); 10 a stop, one line `take_release: STOP
  <stage> (CL step <n>): <what>; to unblock: <what>`; 1 a failure (`take_release: FAILED <stage>: <why>`, a Python
  exception's traceback in `<work>/logs/<stage>-traceback.log`); 3 a gate refusal passed through (`take_release:
  REFUSED <stage> ...`, the stage stays `pending`); 2 an argument error.
- **State** `<work>/state.json` (written atomically after every change): `format`, `tag`, `config`, `runs` (each
  run's argv and exit), `stages[]` with `name`, `cl_step`, `status`, `started`, `ended`, `outputs`, `stop`, `inputs`,
  `inputs_sha`. A stage's inputs are its own declared inputs (`inputs(ctx)`: descriptor fields, config values, file
  hashes) plus the SHA-256 of the outputs of each stage it `DEPENDS` on (and of `USES` stages when ok). A rerun skips
  an `ok`/`skipped` stage whose inputs hash is unchanged and runs it again otherwise; a changed output cascades only as
  far as outputs keep changing. `--from S` reruns S and every later stage; `--only S` reruns S alone and refuses (exit
  2) while a stage S depends on is not ok.
- **Parts**: a stage made of several heavy steps (build: x64, 32-bit, -ia; offline: four checks; fixchecks: four)
  records each finished step with `ctx.partial(...)`; a rerun with the same inputs gets them back from
  `ctx.previous_outputs()` and resumes at the step that did not finish (no second 10-minute x64 build after a 32-bit
  failure).
- **Heavy commands**: `ctx.run(cmd, name=...)` appends to `<work>/logs/<stage>-<name>.log`, sets `C5_JOB_OWNER`,
  `VSCMD_SKIP_SENDTELEMETRY=1` and `C5_RELEASE` (so tools that read 3501's descriptor read this release; corrected
  2026-11-16, 3512: it was `<tag>` only when `releases/<tag>.json` existed, so with `--descriptor` the tools read
  another release; now the tag of the repository's file, else the path of the stage's descriptor file), and
  starts the command through `perf/quietlock/quietlock.py run --who <who> --what ...` unless it is a gated launch point
  itself (`gated=True`: `server/build_server.py`, `tools/d10/run_d10.py`, `server/run_server.py`). A refusal is a
  non-zero exit with `launch gate: REFUSED` (gate.py) or `refused, command not started` (quietlock.py run, exit 2) in
  that run's output; exit 3 alone is not a refusal (build.py's narrowing gate also exits 3). With `--gate-wait MIN`
  the driver waits in `quietlock.py wait --timeout <=170` calls and starts the command again, else it exits 3.
- **Stop file** `<work>/STOP`: checked before every stage and before every command; exit 10, the stage stays
  `pending`. A second driver in the same work folder is refused (`<work>/driver.pid`, a live pid; never `os.kill(pid,
  0)`, which is CTRL_C_EVENT on Windows).
- **Safety checks**: `--work` not under the repository or the pins folder; `--fork` not under
  `%USERPROFILE%/Documents/GitHub` (the owner's clones); the descriptor's tag = the argument; the state's tag = the
  argument.
- **Other issues' stages** (README 2a): a missing stage module (`testdata.py`) or a missing entry-point script
  (`proxy/gen/regen_model.py`, `proxy/tools/derive_purity.py`, `tools/release/pack.py`) is a stop "not built yet
  (issue N)", so the stages before it can be run and `--only`/`--from` can run the ones after it.

### W1b.2 The stages [C, the code; CL = new-vp-release-checklist.md]

| Stage | CL | What it runs | ok when / stop when |
|---|---|---|---|
| survey | 1 | read-only `git` in the fork: old = the pins' base (`deps/dll.lock` `base`), new = the tag | ok: report `survey/survey.md`+`.json` with the flags of CL 1 (boundary headers, new engine calls, new/deleted sources, build list, serialization headers, Lua `Method()` count, overlap with the carry); stop: tag missing, tag != descriptor's `upstream_commit`, a pin commit missing |
| branches | 3 | 3505's `branches.py --base <tag commit> --name <version>-c<8 hex of SHA-256(carry.tsv + tag commit)> --replace --no-fetch --compare-i0/-x64 <pins>` (no `--push`); `--carry pins` skips it and takes the lock commits | ok: both branch heads; its exit 10 passed through |
| testdata | 2 | 3501's module | (3501) |
| lock | 5 | `src_fingerprint.py --git --commit`, `git describe`; writes `<work>/deps/dll.lock`, `i0.lock` (never `deps/`) | ok; outputs `fingerprint_matches_pin` |
| build | 6 | x64: `server/build_server.py --src <src> --out <work>/build/x64-<c8> --config release --offline --jobs N` (gated); src = `git archive` when the fingerprint equals `deps/dll.lock`'s, else a detached worktree with `--unlocked`. 32-bit: `build_vp_clang.py --config release` in a detached worktree (plain) and in an archive + the instruments patch + version stub (`-ia`), each under `quietlock.py run`. Layout `<work>/layout/x64-<c8>-p<tools7>/`, `i0ship-<c8>[-ia]/` with `SHA256.txt`, `README.txt`; the candidate locks get `tools`, `verified`, `release`, `instance` | stop (CL 7): the narrowing gate cut the x64 build (`narrowing-new.txt` NEW lines); stop (CL 3): the instruments patch does not apply |
| narrowing | 7 | reads `build_all.json` `dll.narrowing_gate` | stop: no gate result or new sites |
| census | 8 | `save-width-census.py <x64 src> <work>/census --arch both --jobs 16` (quietlock run) | stop: `x64-only.txt` not empty |
| model | 10 | 3503's `proxy/gen/regen_model.py --src --out --lock <candidate dll.lock>` | exit 1 = stop (review the model diff), 2 = failed |
| purity | 11 | 3503's `proxy/tools/derive_purity.py --src --dll --out` | ok with counts of methods, Lua entries, dropped |
| pack | - | 3504's `pack.py --layout --purity --x86-dll <the tag's (1) Community Patch DLL, git show> --release --out` | stop: the tag's DLL differs from `player_dll_sha256` |
| offline | 12 | `test_proxy_rt.py`, `build_hutest.py` (and `--scenario all`), the verify leg of `run_proxy_game.py` (Tiny 0-50, strict, local_answers/verify, `--db`/`--loc-db` of the descriptor) | every tool exit 0 |
| savetest | 13 | `run_d10.py --out <work>/savetest-<n> --x86-dll <layout i0ship> --srv-bin <layout bin> --huge-save <copy in <work>/inputs> --huge-sha256 --order-exception none --d18-* yes --jobs N` (gated) | ok: pass and part-2 rows = descriptor `reference_counts`; stop: rows differ (a new release records its own counts) |
| drift | 14 | `run_pair.py --spec tiny --turns 100` (quietlock run) | stop: a first differing turn |
| fixchecks | 15 | `0096_run.py` (new build's line `0096-RESULT PASS`), `0084_lock_test.py --arms control,fixed,fixed-nolock` (VERDICT PASS), `0103_run.py` new-off vs new-on (IDENTICAL), `run_server.py --load <huge copy> --turns <turn+2>` | every check passes |
| validated | 14-17 | wave 2 (W5) | stop "not built yet" |

Choices made here [I, reasons]: the fix checks compare the new build with itself (0096's own verdict wants an unfixed
"pin" and so fails when both builds carry the fix; 0084's `unfixed` arm needs an unfixed build); the 32-bit plain build
is a worktree so its version string is "... Clean" like the pins'; `build_vp_clang.py` runs without `-I` (as 9045's
`build9045.py`: the fork's script imports from its own folder).

### W1b.3 Tests [L 2026-11-13]

`python -I -X utf8 -m unittest discover -s tools/release/tests`: the driver's own tests use fake stage modules and a
fake `quietlock.py` (resume after a stop at each stage, rerun on changed inputs and the cascade, stop line and exit 10,
a failing tool exit 1, an exception exit 1 with traceback, gate refusal exit 3 for both refusal texts, `--gate-wait`
retry, the STOP file before a stage and inside a stage before a step, parts resumed, `--from`, `--only` and its
dependency check, missing module, a second driver refused and a stale pid taken over, and every argument check).
`tests/test_take_release_stages.py` runs the real stage modules against a fake fork (a git repository made in the
test, with a tag and two pin commits) and fake tools (`tests/fake_tool.py`) and a fake test-data module: the whole
candidate tier on the pins and on a series, rerun with nothing to do, a changed series, the branches stop passed
through, the narrowing cut (CL 7), no gate result, a failing x64 build, a 32-bit failure resumed without a second x64
build, `--skip-ia`, an instruments patch that does not apply, a census site, the model's difference and refusal, a
missing entry point, pack's exit 10, the player's DLL mismatch, offline failure and resume, the save test's row
mismatch, a bad reference save, the C: free-space floor, `--scratch`, a drift difference, a failing fix check and
old save, the survey's stops and flags. **2026-11-14 01:44-01:48, through `quietlock.py run`: `python -I -X utf8 -m
unittest discover -s tools/release/tests` 145 tests OK, 0 skipped** (with 3501's descriptor and testdata tests and
3505's, 3506's in the folder at that commit). `proxy/tools/test_purity.py --only regen` 3/3.

### W1b.4 The live candidate run on Release-5.4.8 [L]

State `D:/c5-64bit-work/3500/releases/Release-5.4.8/state.json`. 2026-11-13 22:06:56-22:07:38 EST: survey ok (old =
new = 7307aefdf, no flag), branches ok in 39 s (3505's tool: 45 applied, 0 dropped, 0 skipped; i0-release
a1da38fd0, x64-release 678978326, trees = the pins'), testdata stop "not built yet (issue 3501)" (exit 10). 22:07
`--from lock --gate-wait 60`: lock ok, `<work>/deps/dll.lock` fingerprint `v1 3f171f20...` = `deps/dll.lock`'s, so the
x64 build takes the `git archive` route without `--unlocked`.

- x64 (`server/build_server.py`, archive of x64-release 678978326 = the pin's tree): 22:08:31-22:25:48, **1037 s**
  (9045: 572 s on a quieter machine; this one ran beside other teams' LTO link and builds), every build_all step rc 0,
  0 unresolved, narrowing 794 sites, gate ok (0 new, 4 gone), config `release`; DLL `285C064F...B986` (23,616,000 B;
  not the pin's `74588135...`: the x64 DLL is not byte-repeatable across folders, `upstream-fix-batch-1.md` 8c), proxy
  `126efc87...`. The link ran before D107 (22:27) and so without the link slot.
- 32-bit plain (`build_vp_clang.py` in the detached worktree of i0-release a1da38fd0): 22:26:59-22:35:10, 491 s (link
  384 s).
- 22:30 a STOP file for ticket 0158's capacity window (22:30-23:20, no new heavy start): the driver stopped before the
  `-ia` build at 22:35:10 (exit 10, build `pending`, the x64 and plain parts kept in its outputs), the stop-inside-a-
  stage path of W1b.3 in a live run. 23:22 resumed with a plain rerun (`take_release.py Release-5.4.8 --gate-wait
  60`): survey and branches skipped (inputs unchanged), testdata (3501's stage, landed meanwhile) runs.
- The D: drive was saturated (ticket 9060): testdata's `mods` step took 383 s (12.7 s cold before), removing its old
  game copy about 8 min, and the copy itself was still running after 18 min. At 23:55, on the owner's order (MC desk #315:
  disk-heavy work off D: at once), the run was stopped (STOP file, the copy process ended; exit 1, testdata
  `failed`). The D: work folder holds the x64 and 32-bit builds of 22:08-22:35; it is no longer used.
- **Restarted on C: at 23:59** (D111: `C:/c5-64bit-work/3500/releases/Release-5.4.8`, the new default work folder; 88 GB
  free): `take_release.py Release-5.4.8 --fork D:/c5-64bit-work/3500/w1b/fork --skip-ia --gate-wait 60` (no -ia build,
  D110/MC #306). Results (state `C:/c5-64bit-work/3500/releases/Release-5.4.8/state.json`):
  - survey ok (no flag); branches ok in 272 s (39 s at 22:07: the fork clone's objects are on D:; branches.py
    `--replace` made new commits of the same trees: i0-release 7e32b574, x64-release 99fb2c1c);
  - testdata ok 00:04:20-00:10:02 (3501's stage: `mods` 152 s, game copy 120 s, `snapshot` exit 1 with the accepted
    WB_Script failures, `dbbuild` 12 s; `identical_to_base` false, as W1a.2 found);
  - lock ok (fingerprint = `deps/dll.lock`'s, plain-copy route);
  - build ok 00:10:06-00:40:36: link slot taken 00:10:23 (free; no other release link running), x64 `server/build_server.py`
    **1005 s**, slot released 00:27:13; config `release`, `repeat_check` ok (181 inputs, 530 members, 0 stamped: 0131's
    /Brepro), 0 unresolved, narrowing 794 (0 new, 4 gone); DLL `D9CBF3D5...`, server.exe `63D54CFF...`, proxy
    `3A227DB5...`, the four libraries and civ5save.exe equal to the pin's (`75045A87`, `35863EDE`, `30724D6D`,
    `409422EF`); detached worktree of i0 ~4.5 min (objects on D:), 32-bit plain build 523 s, DLL `BFFB0904...`; no -ia;
  - narrowing ok; census ok in 608 s: **0 x64-only sites**, 9 and 9 sites (as 0112 at the pin);
  - model **stop** (CL 10) at 00:56:16 after 207 s: 3503's `regen_model.py` exit 1, "DIFFERENT (26)": all metadata,
    `source.commit` / `usage_scan_source.commit` (the series commit 99fb2c1c against the committed 04b1211b8, same tree)
    and the per-TU byte counts of the AST and preprocessed text (they grow with the source folder's path length);
    `policy-table.tsv` and the generated runtime equal. Reported to the lead for 3503 (00:58): as written every release
    run stops here even when nothing changed.
  - 00:56 `--from offline` (offline, savetest, drift, fixchecks depend on build only): offline **failed** in 26 s:
    Windows refused to start `test_proxy_rt.py`'s freshly built `offline/rt/loader/loader.exe` under
    `C:/c5-64bit-work` (`CreateProcessW` error 225, `ERROR_VIRUS_INFECTED`: Defender) [L]; the same check ran on
    `D:/c5-64bit-work` in 9045. The smoke host of the x64 build ran under C: (the build passed), so the block is
    specific to that loader [I]. Asked on thread 3500 (offline scratch on D:, or an exclusion by the owner); agents do not
    change Defender settings.
  - `regen_model.py` changed on the lead's order (00:58-01:05): the AST and preprocessed sizes are information, not
    part of the verdict (`proxy-design.md` 1b); the model stage is to be rerun.
  - 00:57 `--only savetest`: **ok, 00:57:35-01:22:44** (run_d10 1,495 s, `--jobs 6`, AI logging off, `--order-exception
    none`, `--d18-* yes`): PASS on tiny 0-100, standard 0-200, huge 350-400, parts 1-3 each, only the version-string
    exception; **x86 direct = x64 direct 30,666 / 351,496 / 683,123 rows = the descriptor's `reference_counts`** (the
    pins' counts of 9045) on the builds of the series branches made by the driver (x86 `layout/i0ship-7e32b574`, DLL
    `BFFB0904...`; x64 `layout/x64-99fb2c1c-p0ea6fd7/bin`); huge save a copy of `E2000-HUGE_0350` in `<work>/inputs`.
    run_d10 deleted its 2,505 MB of database copies at the end. Report
    `C:/c5-64bit-work/3500/releases/Release-5.4.8/savetest-1/report.txt` [L]. This is done A's save test.

  - 01:23-01:42, one stage at a time with `--only`: model ok (01:22:53-01:25:43, `regen_model.py` EQUAL after the change
    above), purity ok (143 entries kept: 114 methods, 29 Lua; 0 dropped), pack ok (`<work>/pack/vp64pack`: x64 DLL
    `d9cbf3d5...`, the four libraries, server.exe, purity.tsv; manifest bound to the tag's 32-bit DLL `262aee88...`),
    drift ok (Tiny 0-100, first differing turn None, 30,666 = 30,666 rows, 40 s), fixchecks ok 01:27-01:31 (0096 the new
    build's "0096-RESULT PASS"; 0084 VERDICT PASS, 103 s; 0103 new-off = new-on IDENTICAL, 34,614 rows, 77 s; the huge
    reference save copy loads and plays to 352 on the new server, 36 s), offline ok 01:32-01:41 with its scratch on
    `D:/c5-64bit-work/3500/offline` (MC #405): test_proxy_rt 79/79 (403 s), hutest 46 checks 0 failed, `--scenario
    all` 8/8, the verify leg PASS (Tiny 0-50 strict, 58 s).
  - **Every stage of the candidate tier is `ok` in the state file** (`branches` ok through the series, not skipped).
    The "tier reached" line of a plain run was not printed: the remaining stages ran with `--only` (the order of
    events above), and a plain rerun now would build again, because MC #414 added `testdata` to the build stage's
    `DEPENDS` after this build, which changes its inputs (by design: the smoke database is part of what was built).

### W1b.7 Done A from a clean state: one plain run, every stage ok [L 2026-11-14 01:52-02:58 EST, lead-3500]

The run that W1b.4 could not give (it needed resumes and `--only`). Command, from an empty work folder, no hand step:
`python -I tools/release/take_release.py Release-5.4.8 --fork C:/c5-64bit-work/3500/lead/fork --work
C:/c5-64bit-work/3500/releases/Release-5.4.8-clean --skip-ia --scratch D:/c5-64bit-work/3500/scratch-clean --who 3500`
(team branch at the first lead's 02:00 state; the fork is a `--shared` clone of `D:/c5-64bit-work/9045/fork`; scratch on
D: because Defender quarantines `test_proxy_rt`'s loader.exe under C:, W1b.6). Ran 01:51:57-02:58:45, `rc=0`, one run in
`state.json`, the line "tier candidate reached for Release-5.4.8". All 14 stages `ok`. Step times: branches 21 s,
testdata 72 + 10 s, lock 2 s, build x64 906 s and i0 397 s, census 445 s, model 155 s, purity 9 s, pack 1 s, offline
48 + 28 + 35 + 44 s (rt, hutest, scenarios, verify), savetest 1,498 s (`run_d10`, PASS; rows tiny 30,666, standard
351,496, huge 683,123 = the descriptor's reference counts), drift 42 s, fixchecks 0084 72 s, 0103 57 s, oldsave 28 s,
0096 33 s (the new build's line "0096-RESULT PASS"; the tool's own exit 1 / VERDICT FAIL is its pin-vs-fix design, W1b.2). Wall time 67 min on a shared workstation (other teams' links and games ran beside it: not a timing
claim, PERFORMANCE.md). Copies kept: `evidence/3500-clean-run/` (`state.json`, `clean-run.log`, `savetest-report.txt`).
Done A of the draft is met by this run.

### W1b.6 Defender and the offline checks' test loader (MC #405, 2026-11-14) [L, read-only]

- **What was blocked**: `test_proxy_rt.py`'s test loader `proxy/tools/rt_tests/loader.exe` (a test harness that loads
  the built x86 proxy as the EXE would; not shipped), built at
  `C:/c5-64bit-work/3500/releases/Release-5.4.8/offline/rt/loader/loader.exe`, SHA-256 `5f1656c01e8cf1f6...0c531`
  (from Defender's upload event 2050, 00:57:06). Defender's operational log (read with `wevtutil qe
  "Microsoft-Windows-Windows Defender/Operational"`, nothing changed): events 1116/1117 at 00:57:12, 00:58:01 and 01:00:33
  UTC-stamped, **`Program:Win32/Wacapew.A!ml`** (a machine-learning verdict), severity High, category "Settings
  Modifier", source Real-Time Protection, action **Quarantine**; the process starting it got `CreateProcessW` error 225
  (`ERROR_VIRUS_INFECTED`). The same loader ran under `D:/c5-64bit-work` in 9045 and 9043 (whether it was scanned
  there differently is not known) [I].
- Other files of this run in the log: only upload events 2050 ("uploaded for further analysis", no verdict) for
  `offline/rt/d28/VP64/server.exe` (SHA-256 `63d54cff...` = the layout's server.exe) and `offline/rt/alt/a/alt_a.exe`.
- **Shipped binaries, read-only scan** (`MpCmdRun.exe -Scan -ScanType 3 -File <dir> -DisableRemediation`, 01:27):
  `<work>/pack/vp64pack` (the x64 DLL, CvGameDatabase_x64.dll, CvLocalization_x64.dll, lua51_x64.dll, server.exe,
  purity.tsv, manifest.json) and `<work>/layout` (the same plus the x86 proxy `proxy/bin/CvGameCore_Expansion2.dll`
  and the 32-bit test DLL): **"found no threats"** for both. So no shipped binary of this candidate gets the verdict;
  the loader is a test tool. Not verified: a player's machine with other Defender signatures or cloud verdicts.
- What the driver does about it: the offline stage runs with its scratch on `D:/c5-64bit-work/3500/offline/` (MC #405,
  granted: small and short); Defender settings are not changed and no exclusion is asked for (standing rule).

### W1b.5 Later changes on the lead's orders (2026-11-13 22:27-23:12) [C, tests]

- *Corrected 2026-11-14 01:15: 0159 landed; `tools/x64-dll/build.py` waits in place for the machine-wide
  whole-program link place (`perf/quietlock/linkplace.py`) and the team's link slot is retired, so the build stage no
  longer takes it (d32af6cd). The interim text follows for the record.*
- **D107 interim (MC 22:27: each full-LTO x64 link held 12.7 GB)**: the build stage takes the team's link slot
  (`tools/release/linkslot.py take --who <who>`) and checks that no `lld-link.exe` / `link.exe` runs (psutil) before
  `server/build_server.py`; if either fails it releases the slot and waits (with `--gate-wait`, a check every 150 s)
  or exits 3 (`REFUSED build ... (D107, one release link at a time)`). The slot is released in a `finally`, also after
  a failed build. Only the x64 build is guarded: a resumed build whose x64 part is done never takes the slot. To be
  replaced by 0159's rule (build.py waits for the one release link itself; a third configuration without whole-program
  optimisation for functional work).
- **Build outputs**: `x64_config` = the configuration `build.py` records (`build-summary.json` `config`; MC #226: it
  goes into the state, not the descriptor), `x64_repeat_check` = `build.py`'s repeatability check of the link (0131,
  `/Brepro`).
- **3501's `testdata.py` as written**: the driver accepts a stage result in the dict form `{status, outputs, stop:
  {what, cl_step, unblock}, error}`, sets `ctx.descriptor`, and `ctx.run(cmd, **subprocess kwargs)` without `name=` is
  a `subprocess.run`-compatible runner (stop file checked, driver environment). Since 3501's hand-back the stage is
  3502's: its heavy steps (the game-folder copy, now its own process `testdata.py --copy-game SRC DST PACKS...`, and
  `c5h snapshot`, `c5h dbbuild`) go through `ctx.run(cmd, name=...)`, i.e. `quietlock.py run` (the copy took 371 s
  under 0158's window against 20 s before); on the command line through `quietlock.py run` directly. Unit tests keep
  the in-process copy (no `heavy` runner).
- **Validated tier hook for 3506**: `take_release.tier_stages('validated')` = the candidate stages + `STAGES`
  `[(name, cl_step, owner)]` of `tools/release/stages/validated.py` when it exists; otherwise one placeholder stage
  `validated` that stops "not built yet (issue 3506)".
- **Stage modules are compiled from source at each load** (and registered in `sys.modules`): a byte-cached module of
  the same size and second hid an edit in a test (a `.pyc` reused although the source changed).
- **Tools converted to the descriptor (0131's files, given to 3502 at 23:00)**: `tools/x64-dll/build_all.py` (the smoke
  run's default `--db`: `default_db()` = `descriptor.load().test_db`; was the copy `<cache root>/testdb/vp548-cdrift-
  Civ5DebugDatabase.db`, the same bytes `bd9b8aa1...`; `run_smoke.py` copies the file it gets), `tools/x64-dll/
  cvgamedatabase/ab/run_ab.py` (`CLOSED_DIR` = `game_dir`, `DB_MASTER` = `test_db`, `DB_SHA` = `test_db_sha256`; was
  the same Temp copy), `tools/x64-dll/cvlocalization/ab/run_ab.py` (`CLOSED_DIR`, `LOCDB` = `loc_db`, `LOCDB_SHA`).
  `test_descriptor_tools.py` asserts the 5.4.8 values and greps the three files (0 constants). Not touched:
  `tools/x64-dll/build.py` (0159's).
- **MC condition #414 (2026-11-14 01:10; 3507 found the gap)**: the smoke run of the x64 build uses the database the
  DLL is built for. `build_all.default_db()` takes `$C5_RELEASE_TEST_DB` first, then the descriptor's `test_db`; the
  build stage `DEPENDS` on `testdata` and starts `server/build_server.py` with `C5_RELEASE_TEST_DB` = the test-data
  stage's `test_db` (the environment passes through `build_server.py` to `build_all.py`). For a new tag the
  descriptor's `test_db` is still 5.4.8's (3507's stop 3512), so the default alone would not do.
- **`--skip-ia` / `--with-ia`** (recorded): no instance-aware 32-bit build (D110: no new source copy on D: without the
  MC; the save test does not need it). **`--scratch DIR`** (recorded): the large run outputs of census, offline,
  savetest, drift and fixchecks go there; the save test stops when its drive has less than 3 + 25 GB free (D111).
  Default work folder `C:/c5-64bit-work/3500/releases/<tag>` (D111, lead 23:47).
- **Small fixes found by the live run**: purity's dropped count reads the report's `decision` column (it counted
  every row); drift reads `compare_stats.py`'s `total rows: A n, B n`; pack passes `--fork-commit` and
  `--tools-commit` (the manifest said "unknown"); model passes `--stop-file`, purity `--commit`.

## W3 (3504): the DLL pack and the run-time fallback

Issue `tracker/issues/3504-dll-pack-and-runtime-fallback.md`; contract and the pack's place `tools/release/README.md`
section 5 (the authoritative description of the format, the ini key and `pack.py`). Code: `proxy/src/proxy_pack.*`,
`proxy/src/proxy_rt.cpp` (`DllGetGameContext`, `BootOff`, `Boot`), `proxy_rt_logic.*` (`pack=`), `tools/release/
pack.py`; tests `proxy/tools/test_proxy_pack.py`, `proxy/tools/rt_tests/pack_unit.cpp`. Scratch
`D:/c5-64bit-work/3500/w3/` (not committed).

### W3.1 Decisions (W3's, minor; 2026-11-13)

- **The pack is the `x64_dir` folder** (`VP64\` of D28), not a second folder beside it: the hashed x64 DLL is the
  one `server.cfg dll=` names, held open without write sharing (`FILE_SHARE_READ` only; the server's `LoadLibrary`
  still maps it [L: the good-pack loader case boots]) until `kRt_Boot` returns. A pack folder is written by `pack.py`
  as `<out>/vp64pack/` and copied into `VP64\` by the installer.
- **`x86_dlls`, a list (MC rulings 22:20/22:22, threads #203/#206; replaces W3's first single `x86_dll_sha256`,
  2026-11-13 22:3x).** The proxy accepts `fallback_dll` when its SHA-256 is any listed entry (the log names the
  matching entry's origin) and otherwise falls back with the loud line naming the full unlisted hash and the listed
  ones. `pack.py` writes the tag's DLL first, then an optional published entry (`--published-x86-*`), then test-only
  entries (`--test-x86-dll`, origin prefixed `test only: `); it stops with exit 10 only when the tag's DLL itself is
  missing. The live test packs list the tag's 262aee88 plus a test-only entry for the instances' fallback
  `i0ship-26234371-ia` (3c2f47da...); the "other DLL" fault uses the shipped form (the tag's entry only).
- **`vpproxy.ini pack=1` turns the check and the fallback on; default 0.** Every development and test layout boots as
  before, so a test can never run on the 32-bit DLL silently. The shipped ini (epic 2600) must carry `pack=1`.
- **Fallback scope:** every `NULL` return of the mode=on boot except the window-guard refusal (issue 2703, test-only).
  The reason in the FALLBACK line is the boot's last `LogErr` line (kept while `pack=1`), without its
  "; DllGetGameContext returns NULL" tail.
- **The notice** (the issue asked for a form that never takes focus and still reaches the main menu): one log line
  `boot: FALLBACK to the 32-bit DLL <path>: <reason>; the 64-bit patch is off for this load of the DLL`, and a suffix
  ` - VP64 64-bit patch OFF: <short reason>` on the title of the process's largest visible unowned top-level window
  (IME windows excluded), set with `WM_SETTEXT` under `SendMessageTimeoutW` (1 s, `SMTO_ABORTIFHUNG`) from the boot
  thread; user32 and bcrypt are loaded by name, so the proxy's import list is unchanged [C: `build-proxy.log` "check
  imports ok"]. A second boot in one process replaces the suffix instead of adding one. Rejected: a message box (modal,
  takes focus), a tray balloon (shows on the owner's taskbar from a hidden-desktop test), a front-end Lua popup (once the
  32-bit DLL answers, the proxy sees no engine object or Lua state; that would be the installer's or a front-end mod's
  job, epic 2600). Weakness [I]: in fullscreen the title is visible only in the taskbar and Alt+Tab.

### W3.2 Offline results [L, 2026-11-13 22:0x EST, build `D:/c5-64bit-work/3500/w3/b1` of this branch at 4e03e804, pins 04b1211b8 / tools as built]

- `python -I proxy/tools/test_proxy_pack.py --build D:/c5-64bit-work/3500/w3/b1 --scratch D:/c5-64bit-work/3500/w3/tp3
  --asan --fork D:/c5-64bit-work/9045/fork --tag Release-5.4.8 --tag-sha 262aee88...`: **65/65** (76 s). Unit 52/52
  with clang-cl -m32 and 52/52 under MSVC `/fsanitize=address` x86 (canary caught, no report, peak 321 MB). `pack.py`
  on a fake layout and refusals; `--fork/--tag Release-5.4.8` gives the tag DLL 262aee88c531... Loader layouts
  with the built proxy: the good pack boots the real server; no pack, bad JSON, format 2, a missing field, another
  32-bit DLL, a changed x64 DLL, a changed purity file, server missing, server exits (code 3) before attaching, server
  never attaches (1.5 s timeout): each gives the stand-in 32-bit DLL's context, no child left, exactly one FALLBACK
  line naming the cause, one notice line; no pack and no 32-bit DLL: NULL; `pack=0`: unchanged.
- Cost of the check [L, loader, one run]: 27.5 ms for the manifest, a 23.6 MB x64 DLL, the purity file and a small
  stand-in 32-bit DLL (bcrypt SHA-256); the player's 16.8 MB 32-bit DLL adds about two thirds of that again [I].
  A server that never attaches costs `connect_timeout_ms` (default 60 s) plus 2 s before the fallback [L: 3.7 s with
  1.5 s].
- [C] The instances' `instance.json` `dll.original_sha256` (the DLL copied from the owner's install when they were made)
  is 262aee88c5312c4bd4e899da1d8b413c000bc996fa57abfe8d04b87fd37df16e = the tag's committed DLL: the side finding of
  section 1 holds for the owner's install.

### W3.3 Live runs on c5h-d (Goldberg, D26) [L, 2026-11-13]

Instance c5h-d (claim owner 3500-w3), `provision.py set-proxy` with the server and proxy of this branch and
`--fallback-dll` = deps/i0.lock's `i0ship-26234371-ia` (3c2f47da...); `pack=1` set with `set_ini.py`; `manifest.json`
and `purity.tsv` copied into `VP64\` per case and removed before `set-proxy --original`; save `C5H-3200-TINY` loaded
by the front-end autoload (`proxy/tools/live/prepare.py --load`). Driver `D:/c5-64bit-work/3500/w3/scripts/live.py`.

- **Attempt 1 (22:16-22:32, build b2 at 5871fd33e, single `x86_dll_sha256` manifest, the control case = a good pack).**
  The front end's mod activation (the patched front end activates CP, VP and EUI and rebuilds the database at every
  launch) took until 22:23:43 (`stopwatch.log` "SetActiveDLCandMods - Activate Mods, 149.5 s"), and the EXE's main
  thread called `DllGetGameContext` only at 22:27:37, 11.5 min after the launch (the window did not answer
  `PrintWindow` in between). The pack check passed (`boot: pack ... ok`, 32-bit DLL 3c2f47da... matches), the server
  started, and then **stopped after "delay-loaded imports of CvGameDatabase_x64.dll bound up front"**: it did not
  attach within `connect_timeout_ms` 60000, so the proxy fell back as designed: `boot: FALLBACK to the 32-bit DLL ...:
  the 64-bit boot failed: boot FAILED: the server did not attach within connect_timeout_ms 60000`, the fallback DLL
  loaded in 7.3 s, and `boot: notice: the title of window 017F20E8 (class WinClass_FXS, 1 candidate(s)) now ends with
  " - VP64 64-bit patch OFF: the 64-bit server did not start"` (the notice on the real EXE window). The next step in a
  normal server start is the localization start from `loc_db` (the instance's `cache\Localization-Merged.db`, which had
  a `-journal` file beside it after the rebuild): that the server waited on the database the EXE had just rebuilt is
  [I], the same family as task 0153's "database Localization is locked" after a mod re-selection. The driver's
  `wait_game` timeout (600 s) ended before the load, so this attempt is not a done-D result; it is one live instance of
  the "server does not connect" fallback.
- **Attempt 2, fault "no pack" (22:42-22:54, build b3 at 7ae6c2fe5; its proxy's build line says `dirty=1` because a
  research text was uncommitted at the link: the proxy sources equal the commit).** The front end activated the mods
  (DB rebuild), the proxy booted at 22:44:50 with `pack=1` and no `manifest.json`: `boot: FALLBACK to the 32-bit DLL
  ...: pack check no-pack: no DLL pack: ...\VP64\manifest.json does not exist`, the fallback loaded in 335 ms, `boot:
  notice: the title of window 03C80906 (class WinClass_FXS, 1 candidate(s)) now ends with " - VP64 64-bit patch OFF: no
  64-bit DLL pack is installed"`. The game reached the main menu on the 32-bit DLL (Visual Checks entry 22:52, PASS;
  the title read back with `winapi.window_text`: `Sid Meier's Civilization V (DX11) - VP64 64-bit patch OFF: no 64-bit
  DLL pack is installed`). The save was not loaded: the front end said "this launch already issued its load - not
  issuing a second one", because the autoload marker that `prepare.py --load` arms is used up by one launch and the
  driver had armed it once for several launches (attempt 1 used it). Arm it before every launch (fixed in the driver).
- **Machine load, 22:55-23:30:** with four game instances and 0158's capacity window on the workstation, one launch
  took 30 min to get from the start to the mods' activation (`stopwatch.log` "LoadUnitLibraries via Database, 309 s",
  normally seconds), so `wait_game` (1500 s) ended before the load: no result from that launch.
- **Done D on c5h-b (C:, D110/D111), 2026-11-13 23:45-00:00, build b3 (repo 7ae6c2fe5; the proxy's build line reads
  `dirty=1` because uncommitted research text, not code, was in the tree), fallback `i0ship-26234371-ia` 3c2f47da...,
  save `C5H-3000-TINY-T0018`, the autoload armed before each launch.** Each fault launched hidden through the harness
  (`startup_watch_ok` true, guard check exit 0 and every activation veto ok, i.e. COMMON 14 c), loaded the save on the
  32-bit DLL (`wait_game` exit 0: the Lua channel answered in the game), wrote exactly one FALLBACK line and one notice
  line, showed the notice in the window title, started no server child, and stopped cleanly (`stop` 0, `leftovers` clean):
  | fault | FALLBACK reason (log) | title suffix | launch to loaded |
  |---|---|---|---|
  | another 32-bit DLL (shipped-form manifest: only the tag DLL 262aee88) | `pack check other-dll: fallback_dll ... has SHA-256 3c2f47da6572d38a31558c6fd35294e433fb0dbd71c3063ec8c6e7ce909ea06c, which the pack (release Release-5.4.8) does not list: 262aee88... (tag Release-5.4.8 (1) Community Patch/CvGameCore_Expansion2.dll (git show, 16837632 bytes))` | `... for another Vox Populi version` | 149 s |
  | server missing (`server_exe=VP64/missing-server.exe`, good pack) | `the 64-bit boot failed: boot FAILED: server_exe ...: no such file` | `the 64-bit server did not start` | 141 s |
  | no pack (no `manifest.json`) | `pack check no-pack: no DLL pack: ...\VP64\manifest.json does not exist` | `no 64-bit DLL pack is installed` | 139 s; Lua: turn 18, player 0, 1 city |
  Visual Checks (all PASS, issue 3504): 23:48 otherdll and 23:52 nosrv (the save's leader screen "Continue Your
  Journey" over the loaded game), 23:59 nopack (the map at turn 18, after the load screen was dismissed), and the 22:52
  main-menu shot of the c5h-d attempt. Results: `D:/c5-64bit-work/3500/w3/live/c5h-b-*/result.json` (scratch, deleted at
  the hand-back; the numbers are here).

### W3.4 Do the shell parts load on a fallback? No [C, L] (2026-11-14, question from 3506's laptop run via the lead, MC #379)

3506's fallback job on laptop `tb2` (2026-11-14): with the tag DLL 5.4.8 as the 32-bit fallback (`pack=1`, a manifest
naming another DLL) on the huge save `TBPP-HUGE_0350`, the game crashed at 00:21:33 with an access violation writing
0xC at `CivilizationV_DX11.exe+0x4cc892`. Sub2G was 1,806,212 KB committed with a largest free block of 13,760 KB.
Minidump `CvMiniDump_20261114_002133_5.4.8-0-g3dc5bea6e_Release.dmp`, which 3506 copies with the log to
`D:/c5-64bit-work/3500/evidence/`. The product path's preparation had also installed the composite and set
`VP64Options.ini ServerRenderer=1`. The question was whether those shell parts load into the 32-bit EXE on a fallback
and take address space the stock game does not use.

- **Code [C]:** every shell part loads only in `ProxyShell::OnBoot` (`proxy/src/proxy_shell.cpp`): the composite's
  `LoadLibraryW`, the 9040 test helper (`Loader::LoadFromMarker`), the view-mask install with its watcher thread, and
  the names in the EXE's environment (`VPSHELL_SECTION`, `VPSHELL_OUT_DIR`). `DllGetGameContext` calls `OnBoot` only
  when `!g_offMode`, and every fallback goes through `BootOff`, which sets `g_offMode`. So on a fallback no composite
  and no view-mask writer load, and no frame ring exists (the frame ring is the server's and the composite's; no
  server runs). A pack-check fallback, 3506's case, never enters `Boot()` at all: the options file is not even read.
  Only a fallback after a failed 64-bit boot has run `ProxyShell::Configure`. That leaves at most an open handle on the
  checked `shell_composite.dll` (no mapping, no address space) and the `VP64SHELL_BOOT` environment variable, both
  released or harmless at the unload; the server it started is terminated and the vpipc section unmapped
  (`CloseAfterFailedBoot`).
- **Logs [L, c5h-b, 2026-11-13 23:45-00:01]:** the three fallback runs' `vpproxy.log` have no `shell:` or `options:`
  line, while the 64-bit control run has them (`options: ... ServerRenderer = 0`, `shell: ... no composite`).
- **What a fallback adds to a stock 32-bit game:** the proxy image itself (about 0.95 MB mapped) and the bcrypt and
  user32 calls of the check (bcrypt is freed again). Nothing else.
- **Conclusion:** no shell part loads on a fallback boot [C, L]. **Correction 2026-11-14 01:05 (lead):** the address-space
  limit is NOT the cause. 3506's second fallback job (oldpc tb2, job 20261114-005346-f5f8, Tiny save, server.exe absent
  as the fault) crashed at 00:59:09 with the same signature (write to 0xC at `CivilizationV_DX11.exe+0x4cc892`, same
  stack) with Sub2G free 299 MB and a largest free block of 138 MB [L]: the tag DLL 262aee88 on the fallback crashed in
  game 2 of 2. 3504's done-D fallbacks with the instance-aware i0 build (i0ship-26234371-ia) did not crash. Whether the
  fallback route or the tag DLL in that game folder (the toolkit modpack ships 4e91c58a, not the tag's DLL) is the cause
  is open; a control without the proxy is proposed to the MC (3500 thread). (Replaced: "the cause, the stock 32-bit game
  at its address-space limit, is [I]", 00:58.) Optional hardening (not done, no address-space effect): call
  `ProxyShell::OnUnload()` on the failed-boot fallback path to release the held composite file at once.

## W3b (3516): D33 at run time instead of in config.ini (decision D116, under review)

Issue `tracker/issues/3516-fallback-crash-under-d33-config.md`; the Architect's advice
`research/07-architecture-decisions/architect-2026-11-14-d33-at-run-time.md`. Scratch `C:/c5-64bit-work/3500/3516/`.

### W3b.1 The premise that does not hold [C + L, 2026-11-14 02:20]

D116's option A keeps `config.ini` stock and has the proxy clear the config-mask bits `[app+0x940]` at run time, in the
front end. The Architect's premise 2 was that only River, Arrows and MovementPath are app-Startup systems among D33's,
so A "costs nothing measurable" and keeps FluidFOW's 294 MB. **20 of the 24 keys are gated in app Startup**
(`disable-systems-and-masks.md` section 3, "Which keys app Startup gates"), FluidFOW among them: its Startup 0x44A570
(called at 0x4764F3, gate 0x8000) allocates the 8 x 24 MB per-job-thread blocks at boot, before any game DLL exists
(`allocations-and-memory-map.md` section 5: main menu, no mods, FluidFOW = 1 against 0: 1,048 against 1,269 MB, -221).
A run-time write can skip only the per-game part (the grid at 0x468EFD, ~74 MB on the huge save). Posted #474; the MC
(#475) stopped the build at the measurement below.

### W3b.2 The measurement (MC #475) [method C; results below when in]

Job `tools/testbed/jobs/release_d33_runtime.py` (this branch), on the product path of `product_path.py` (proxy,
server.exe, composite with its manifest, `ServerRenderer = 1`), builds of 0156 (main a780eecb: server.exe `6c609646`,
proxy `4ab0f9ba`, composite `6bb32a85`, x64 DLL `74588135` = the pin), huge save `TB-HUGE_0350` t350, 3 AI turns
(350 -> 353). Arms, one launch each, interleaved `d33,a,a1,a1,a,d33`, each with `product_path.py`'s prep and restore by hash:
- **d33**: `config.ini` = today's D33 with Overlay kept on (`darkred.py` `D33_OFF` at this branch, Waves still off).
- **a** (A from outside): `config.ini` = the instance's own file; when `vpproxy.log` says `shell: view mask: active`
  (the proxy's `OnBoot`: front end, after the server's start, before the load), the job writes (WriteProcessMemory,
  data only) `[app+0x940] &= ~0x00BBF898` (D33's keys without River and Arrows) and `[GVS+0x1B4] &= ~0x00400100`
  (River, Arrows: view bits only). The proxy's watcher then clears the view bits of the config's off keys as usual.
- **a1** (A', MC #475): `config.ini` = the instance's own file plus `FluidFOW = 1` (the app-Startup saver whose 32-bit
  start does not crash), the rest at run time as in a.
Per arm: memory (`tools/harness/memory.py`: private, working set, committed, largest free block) and both masks at the
write moment (front end, DLL booted, no game: the boot-time memory), after the load + 20 s, and after the 3 turns;
whether the write came before the load screen (`Lua.log` without `LoadScreen:`). Expected [I]: a above d33 by about
200 MB (FluidFOW's blocks) plus whatever the other boot-gated D33 keys allocate; a1 within noise of d33 unless another
key carries a boot-time saving.

The A' fallback control (MC #475): the same job, arm `fb-a1`, on oldpc `tb2`: the pin's instance-aware 32-bit DLL
`i0ship-26234371-ia` alone (no proxy: what a fallback leaves running) under `config.ini` own + `FluidFOW = 1`, the huge
save, in game, a whole-screen DIAGNOSTIC picture (ImageGrab, window parked on screen), My Games and the standard files
backed up by SHA-256 and restored by hash.

Jobs: laptop `tb1` `20261114-022913-eb2a` (arms), oldpc `tb2` `20261114-023343-5f56` (fb-a1).

### W3b.3 Option A from outside does not load: EXE crash at VA 0x60D80D [L, 1 of 1, laptop tb1, 2026-11-14]

Job `20261114-022913-eb2a`, arm 1 (`a`): `config.ini` the instance's own (OverlayDebug, ColorKeyEdit = 1). The write
landed 69.6 s after the launch, at the proxy's boot, before any `LoadScreen:` line in `Lua.log`: config `0xFFEFFFF7 ->
0xFF440767`, view `0xFFEFFFF6 -> 0xFFAFFEF6`. The proxy's watcher then cleared Combat (0x800, set again by GameView::Startup)
and Terrain during the load (`vpproxy.log` 56.8 s and 71.6 s). **The EXE crashed in the load at 02:38:46-47 EST**
(after the composite had read the grid constants): gbe crash printer `0xC0000005 @ 0x00A9D80D`, EXE base 0x00890000,
so **VA 0x60D80D** (WER APPCRASH, exception offset 0x20D80D; no minidump kept). 0x60D790 is a world-to-grid transform
(thiscall). It reads the world matrix 0xD91030.. and then `[ecx+0xA8]`, `[ecx+0xAC]` and `[ecx+0xB4]`, and the fault is on
the first of those reads, so ECX was not a live object [C]. It has 20+ callers; 0x6EA9DA in the FluidFOW cull 0x6EA7A0
passes the static GridHeightmap 0xD91270 [C]. Which caller faulted is not known. [I] A system started at boot whose
per-game start the run-time write skipped. Arm d33 of the same job loaded and played its 3 turns. The WerFault box was
closed by the harness on the MC's word (#498, job `20261114-024300-01d3`) and arm a was not run again (MC). Evidence:
`D:/c5-64bit-work/3500/evidence/3516/` (`runtime.json`, `inspect.json`).

### W3b.4 The A' fallback control: no crash, a deep red map [L, oldpc tb2, 2026-11-14 02:34-02:42]

Job `20261114-023343-5f56` (arm `fb-a1`): the pin's `i0ship-26234371-ia` 32-bit DLL (3c2f47da...) alone, no proxy,
`config.ini` = own + `FluidFOW = 1` (masks cfg = view = 0xFFEF7FF7), huge save copy. In game after 151 s, no crash box,
stopped by the harness, everything restored by hash (the standard files, the DLL back to e589306e, My Games 21,874 files
unchanged; `provision.py verify --quick` 0). In game: private 1,611.6 MB, largest free block 1,802.7 MB (32-bit DLL in
process; not comparable with the product path). Picture (DIAGNOSTIC, whole screen, Visual Checks
`vc-20261114-024024-oldpc-010e`): **the map under the units is flat deep red with grey unexplored areas, and no terrain,
water or rivers are drawn**. Units, ships, cities, banners, the top panel, the unit list and the minimap work. This is the 2026-11-09
single-key FluidFOW result again (section 2 of `disable-systems-and-masks.md`): A' leaves the fallback unusable as a map, not stock.

### W3b.5 A' crashes the same way; what the front-end memory says [L, laptop tb1, 2026-11-14]

Job `20261114-024408-cd76`, arm 0 (`a1`: own `config.ini` + `FluidFOW = 1`, the rest written at run time: config
`0xFFEF7FF7 -> 0xFF440767`, 68.9 s after the launch, before the load screen) crashed in the load with the **same fault**:
gbe `0xC0000005 @ 0x00A9D80D`, base 0x00890000, VA 0x60D80D, WER offset 0x20D80D (inspect job `20261114-024729-b3a7`).
So **the run-time config write crashes 2 of 2** (arms a and a1), and FluidFOW is not the cause: it was off at boot in a1.
[I] One of the systems that app Startup started (stock `config.ini`) and whose per-game start the write skipped leaves an
object that 0x60D790 reads; candidates are Decal, Forest, FOW, Waves, MovementPath, Particle, Projectile, Combat and Minimap
(not bisected).

Front-end private memory after the proxy's boot (one run each, laptop, the same moment in every arm): **d33 945.8 MB,
a1 1,000.0 MB (+54), a 1,120.9 MB (+175)**. So at boot FluidFOW alone accounts for ~121 MB on this host. The
other boot-started D33 systems take ~54 MB more, and their in-game cost could not be measured because neither run-time arm
loaded. The d33 arm: 1,352.0 MB after the load (largest free 1,996), 1,503.9 at t353 (largest free 1,913).

**A'' (proposed to the MC, #503):** `config.ini` = D33 minus River and Arrows, the only two D33 keys that crash a
32-bit start (VA 0x8CD492, never-cleared view bits). The proxy writes no config bit. It clears the River and Arrows view
bits (one constant in `ClearBits`, as `NEVER_RESTORE` names them). The product's memory is D33's: section 5 of
`disable-systems-and-masks.md` measured River + Arrows off at -2 MB. The fallback is not stock (deep red map, W3b.4), but
by the 2026-11-09 A4/B4 probes it should load. Its fallback control is job `20261114-024824-b4af` (oldpc tb2).

### W3b.6 The A'' fallback control, and where 3516 stopped [L, oldpc tb2, 2026-11-14 02:48-02:55]

Job `20261114-024824-b4af` (arm `fb-a1` with `--a1-keys` = D33 minus River and Arrows): the pin's `-ia` 32-bit DLL alone
under that `config.ini` (masks cfg 0xFF440767, view 0xFF472F67). In game after 134 s, **no crash box**, stopped by the
harness. In game: private 1,560.2 MB, largest free block 1,844.9 MB (A' under the same conditions: 1,611.6 / 1,802.7).
Everything was restored by hash: the standard files, the DLL back to e589306e, My Games unchanged, `verify --quick` 0. Picture
(DIAGNOSTIC, whole screen) `vc-20261114-025340-oldpc-43c2`: like A' (W3b.4), the map is flat deep red with grey unexplored
areas, with no terrain, water or rivers, and in addition the minimap is black. Units, cities, banners and the UI work.

**Stopped here (2026-11-14 02:51, D118, MC #505):** the owner does not want a fallback that keeps playing with no terrain
[reworded for privacy]: an error popup with automatic error reporting is wanted instead, carrying every detail needed so that beta
testers can submit it for diagnosis. The Architect is redesigning 3516. Not done: the bisect of the nine
candidates (cost estimated in #510: ~45 min for one culprit, up to ~2 h for several, every crash needs the MC's word),
the static map of 0x60D790's callers (~1-2 h), A or A'' in the proxy. The summary for the Architect is #510 on the 3500 thread.
Instances: laptop `tb1` restored twice by hash (jobs `20261114-024355-f6e5`, `20261114-025335-dde1`: 15 of 15 entries
equal or absent, `provision.py verify` 0); oldpc `tb2` restored after both controls.

### W3b.7 Design V's one-launch test: the load survives, +257/+289 MB, models pale [L, laptop tb1, 2026-11-14 03:31-03:53]

Design V (D124, `research/07-architecture-decisions/architect-2026-11-14-always-boot-stock.md`): the EXE always boots
stock and the proxy clears only view bits. Job `20261114-033052-7d00`, arms `v, fb-stock, d33`, huge save, 3 AI turns, one
launch per arm, the 0156 product-path builds, `--pictures` (camera on the capital Thebes by `UI.LookAt`, right after the
load). Arm `v`: `config.ini` the instance's own (cfg 0xFFEFFFF7, never written) plus the existing test switch
`crashlogs\viewmask.enabled` with `clear=0x00FBF998`. Those are the view bits of the 16 keys of `darkred.py` `D33_OFF` at
this branch, **Waves among them** (3600's removal is not merged). The proxy's product rule adds Terrain. `fb-stock`: the pin's
`-ia` 32-bit DLL alone with stock `config.ini` (the stock game).

- **(a) The load survives [L, 1 of 1].** In game after 198 s, 3 AI turns to t353, no crash. The view mask is 0xFF040666,
  the same as D33's product view mask. The test switch's log (`viewmask-n0.log` in the job's out folder) shows the
  startups set the bits again during the load (0x8000, then 0x85890 and others) and the watcher clearing them within one
  10 ms poll. So the 0x60D80D crash of W3b.3 came with the config write, not with clearing the view bits.
- **(b) Memory** (private MB / largest free block MB, `memory.py`, one run each):

| | front end after the proxy boot | after the load | t353 |
|---|---|---|---|
| v | 1,122.7 / 2,006.3 | 1,680.1 / 1,767.3 | 1,840.6 / 1,588.3 |
| d33 (today's product) | 942.6 / 2,006.4 | 1,423.6 / 2,006.4 | 1,551.9 / 1,858.1 |
| fb-stock (stock game, DLL in process) | - | 1,946.0 / 1,375.8 | 2,055.0 / 1,202.9 |

  V costs **+256.5 MB** private after the load and **+288.7 MB** at t353 against D33. Its largest free block stays
  **above the stock game's by +391.5 / +385.4 MB**, which is the Architect's condition for recommending yes.
- **(c) Pictures** (DIAGNOSTIC: V uses a test switch, D112; no popup over the map in either product picture):
  - V `vc-20261114-033500-laptop-8082` (marked fail): the server's terrain composes, but the EXE's building,
    improvement, wonder and resource models are drawn **untextured pale grey-white**, the look of run R1 of issue 3009
    (the cloud base layer).
  - D33 `vc-20261114-034729-laptop-ee27`, same camera: dark red models, as today.
  - The stock grab `vc-20261114-034112-laptop-5695` is black: ImageGrab gives a black client area on the laptop, so there
    is no stock picture.
  - [I] Under V the draw objects carry FluidFOW's mask handle (FluidFOW started at boot) and FluidFOW is not updated
    (view bit 15 clear), as the Architect predicted (his 4c). V needs a mask fix that covers that handle too. Only the
    fork branch `i2-white-mask` has a white-mask pass (section 5 of `disable-systems-and-masks.md`); the product proxy has none.
- Restores: all three arms by hash (v and d33: 15 of 15 entries equal or absent, `provision.py verify` 0; fb-stock:
  standard files equal, the DLL back, `verify --quick` 0). V's `viewmask.log` was moved into the job's out folder.

## W3c (3520): config.ini under D119, the config.ini routine and the helper `vp64helper.exe`

Issue `tracker/issues/3520-config-ini-routine-and-failure-helper.md`; decision D119
(`research/07-architecture-decisions/architect-2026-11-14-failure-popup-and-report.md`), D120. Scratch
`C:/c5-64bit-work/3500/3520/` (deleted at hand-back).

**Not needed for the release under D124 (2026-11-14 03:25, `architect-2026-11-14-always-boot-stock.md`):** under design V
the EXE always boots stock and config.ini is never touched, so the S0 question (does the EXE rewrite config.ini) and the
config.ini routine are on hold. S0 was stopped after 2 of its 5 launches on the lead's word (03:27). What it found is
kept below for whoever needs config.ini behaviour later (a run-time single-key write, D124 section 3).
*Corrected 2026-11-14 05:05: the owner's D125 reversed D124 (design V not taken; the config.ini route in the A'' form resumes).
S0's results are needed again: the restore after the EXE's end (W3c.4) rests on W3c.2's "no write but at the start".*

### W3c.1 S0 (a), static: the EXE has its own INI writer [C, 2026-11-14, DX11 EXE of instance c5h-a]

- The DX11 EXE imports **no** `GetPrivateProfile*` / `WritePrivateProfile*` function (its import table, read with a
  small PE reader): config.ini is read and written by the EXE's own INI class. File imports present: `CreateFileA/W`,
  `MoveFileW` (slot 0x9D62F0), `_wfopen` (0x9D689C), `fopen` (0x9D6890).
- The only code reference to the wide string `config.ini` (VA 0xA0F48C) is in the app's configuration load at
  **0x5CF6F0** (push at 0x5CF713): it builds the path from the user folder, loads the file into the INI object held at
  `[app+0x6E4]` (call 0x829580), then registers each key with its default and its comment (pairs of calls 0x81EB40 /
  0x81EE90, for example the `[DisableSystems]` names at 0xA0F43C..0xA0F47C referenced from 0x49FF37..0x4A006D).
  After each registration it calls the INI object's virtual function +0x14 when the object's bytes +0x38 and +0x4 are
  both set [C]; [I] a "save if changed" (a key missing from the file is added with its default and comment, which is how
  a stock config.ini gets its "; Set to 1 to disable ..." lines). Not followed further: the live run below answers when
  the file is written.
- `UserSettings.ini` and `GraphicsSettingsDX11.ini` sit beside `config.ini` in the user folder; which of the three an
  options commit writes is measured below.

### W3c.2 S0 live: when the EXE writes config.ini, and which exit ends it cleanly [L, 2026-11-14 03:20-03:28, 2 of 5 runs]

Driver `C:/c5-64bit-work/3500/3520/s0_live.py` (scratch; not kept), instance **c5h-e** (D:, MC #527), the pin's
`i0ship-26234371-ia` DLL (3c2f47da...) with CP, VP, EUI; the standard files and `UserSettings.ini` backed up by SHA-256
and restored by hash at the end. A sampler thread records every change of size or mtime of `config.ini`,
`UserSettings.ini` and `GraphicsSettingsDX11.ini` (every 0.2 s, with its SHA-256 and the phase). Five launches, each
ended through `launch.stop()` and its gates (recorded process, no dialog, saves settled or front end idle):
R1 a new duel game, an options change committed in game (`OptionsManager.SetAutoUnitCycle_Cached` +
`CommitGameOptions` + `CommitGraphicsOptions`, then back), exit by `UI.ExitGame()` over the Lua channel; R2 front end
(mods active, no game), exit by the menu's EXIT and the confirm popup's Yes (posted clicks; the channel is silent in a
front end without a game); R3 front end, `WM_CLOSE` posted to the main window (`WinClass_FXS`); R4 in game, `WM_CLOSE`;
R5 front end, process end by the harness (TerminateProcess after the gates). Per exit: the method, the exit code, WerFault
processes for the pid.

Results (R1 and R2 ran; R3-R5 were not run, D124):
- **The EXE rewrites config.ini at every start, early in the front end, before mod activation** [L, 2 of 2]: R1 at
  67 s after the launch (03:21:35, pid 16368), R2 at 48 s (03:26:06, pid 47272). Same size (8,501 B) and the same SHA-256
  (`e899caae...`) as before: it writes back what it read. The file it writes has CRLF line ends (359 lines, every one CRLF; c5h-a's
  the same). So a value written into config.ini before the start survives the start (the EXE writes back
  the values it loaded) [I: only tested with unchanged values].
- **No write anywhere else** [L, R1]: not during the new game's start or in game, not on an options commit, not at the
  exit through `UI.ExitGame()`. Nothing in R2's front end after the start write, nor at its exit.
- **An options commit writes `UserSettings.ini`, not config.ini** [L, R1]: `OptionsManager.SetAutoUnitCycle_Cached` +
  `CommitGameOptions()` + `CommitGraphicsOptions()` changed `UserSettings.ini` within 0.15 s (same size 1,580 B; SHA-256
  `3ad3abf9...` -> `ffcd5980...`, back to `3ad3abf9...` after the second commit). `GraphicsSettingsDX11.ini` was not
  written (no graphics option changed).
- **Both orderly exits end the EXE without a dialog, with exit code 0xC0000005** [L, 2 of 2]: R1 `UI.ExitGame()` over the
  Lua channel in game (process gone 5 s after the call); R2 the front end's EXIT + Yes (posted clicks; gone 1.6 s after the
  Yes). No WerFault process for the pid in the 30 s after either exit, no crash dialog. [I] The EXE faults during its own
  shutdown after the windows are gone and the crash is swallowed; a parent that reads the exit code sees an access
  violation from a clean exit. Not looked into.
- Config: c5h-e (D:), DX11 EXE, the pin's `i0ship-26234371-ia` 32-bit DLL (3c2f47da...) with CP, VP, EUI; a new duel
  Continents game (1 AI). Afterwards every standard file and `UserSettings.ini` back by hash (15 of 15 equal or absent);
  `cli.py leftovers` clean; claim released.
- Not measured: `WM_CLOSE` (R3, R4), the harness's terminate (R5), a config.ini with changed values across a start.

### W3c.3 The popup helper `vp64helper.exe` (D124 form) [C, tests 2026-11-14]

- Source `proxy/helper/vp64helper.cpp`. `proxy/build_proxy.py` step 5b (`build_helper`) builds it into
  `<out>/bin/vp64helper.exe`: x86, clang-cl in the VS 2008 environment, static CRT (VC9 libcmt), GUI subsystem with a
  narrow `main` (`/ENTRY:mainCRTStartup`), no manifest. The import check allows only kernel32, user32, gdi32 and
  shell32 (no network library, D120). It is about 99 KB.
- A wide entry (`wWinMain` or `wmain`) does not link with VC9 libcmt and lld-link: `crt0.obj` is loaded for
  `___app_type` before the wide start-up object, so `__aenvptr` and three other symbols are defined twice [L, 2026-11-14].
  So the entry is `main`, and the arguments come from `CommandLineToArgvW`.
- `vp64helper popup --report <dir> [--options <vpproxy.ini>] [--no-ui] [--no-ui-choice open|close]`. Under D124 the game
  goes on as the stock game. The helper does not wait for the EXE, changes no file and sets no flag. It shows these lines:
  the 64-bit part could not run and this game goes on as the normal (32-bit) game (corrected 2026-11-15, ticket 0361:
  that line is the boot fallback's; when the report's stage is `server-death` the first line says instead that the 64-bit
  part stopped during the game, so the game had to end; `research/03-dll-engine-boundary/proxy-design.md` section 28);
  what the report holds (the first line
  of `<report>/popup-summary.txt` if the proxy wrote one, else a fixed line that also says there are no saves, no user or
  computer names and no Steam id); one more line if the folder holds a `*.dmp` (the server's dump holds the game's
  state); `Report: <full path>`; the hand-in line; a warning that issues are public, so the tester looks the report over
  before attaching it (D122).
- The hand-in line is `handin=` of vpproxy.ini (`--options`, default the vpproxy.ini beside the EXE; flat key=value as
  the proxy parses it, quotes dropped, the first `handin=` counts). Its default is
  `https://github.com/LoneGazebo/Community-Patch-DLL/issues/new` (D260, ticket 0761, 2026-11-25; was this repository's
  own issue page from lead-3500, 2026-11-14, under D122, then the DLL fork's issue page from 0574).
- The window: a read-only edit control holding the text, so the path and the page can be copied, plus "Open folder"
  (Explorer, the popup stays) and "Close". It is top-most and centred. Exit codes: 0 shown and closed; 1 the report
  folder does not exist (the popup still says so, with "Open folder" disabled); 2 usage. `--no-ui` prints `popup: ...`
  lines and opens no window; it is the only form the workstation's tests use (COMMON 14). The real window has not been
  shown on any machine yet (S1, on the testbeds).
- Tests: `python -I proxy/tools/test_vp64helper.py --scratch <dir> [--asan]`: 76/76, 0 skipped (the clang build:
  usage, basic, dump, summary, handin, exedir, missing, open, unicode, imports and the build step with its negative; an
  MSVC `/fsanitize=address` build with a canary, every case clean). Every run is on a hidden desktop.
- (Replaced 2026-11-14 05:05 by W3c.4: the routine was on hold under D124, parked uncompiled; D125 resumed it.)
- The release testbed jobs' hermetic tests (MC #523): `tools/testbed/tests/test_release_jobs.py`, 24 tests, 0 skipped.
  They cover `release_fallback.py` main's preconditions, `--config stock`, run's `--park`, `--continue-key`, on-screen,
  crash-box and label checks, `control_restore`, and `release_tb_repair.py --phase control-restore`.
  `release_d33_runtime.py` is left to 3516 (its arm "v" for D124's test; lead-3500).

### W3c.4 D125: the config.ini routine and the helper's on, off, status and failed [C, hermetic tests 2026-11-14 05:01]

- Routine: `proxy/helper/proxy_cfgini.h` / `.cpp` (moved out of `proxy/src`: the proxy does not link it; only the helper
  does, `build_proxy.py` step 5b now compiles both files). Our keys: darkred.py `D33_OFF` minus River and Arrows (14
  keys, OverlayDebug and ColorKeyEdit included: stock config.ini has them at 1 already, so `on` writes nothing for them
  and `off` leaves them). Side file `vp64-config-side.ini` beside config.ini (`[PlayerValues]`, one `key=value` per key,
  `<absent>` for a key that was missing). Every write is temp file + `MoveFileExW`. Rules: `on` refuses a corrupt side
  file, a read-only config.ini, a key twice, UTF-16, no file or no `[DisableSystems]` (nothing changed); `off` restores
  only keys that still hold ours (others reported `kept:`), and without a usable side file it leaves config.ini as it
  is and says so (`Result.note`, keys at 1 named `left:`; an unusable side file moved to `vp64-config-side.ini.unusable`).
  *Corrected 2026-11-15 (issue 0378, installer-notes.md 11.4): was "it writes the stock values (reported `stock:`) so a
  failure always leaves a playable config.ini", which reset a player's own 1 to 0.* Line ends, a BOM and every other byte are kept;
  only a key that was absent gets an appended line, removed again by `off` [C, unit].
- `ConfigIniPath`: `VPPROXY_TEST_CONFIG_INI`, then `C5_DOCUMENTS` (a test instance's Documents: the helper inherits the
  EXE's environment but is not under the guard's shim, so without it the helper would reach the machine's real
  Documents), then `SHGetFolderPathW(CSIDL_PERSONAL)`.
- Helper verbs (contract also in `installer-notes.md` section 9): `on` (vpproxy.ini must exist and be writable first;
  config.ini on, then `mode=on` and `failed=` removed; if vpproxy.ini cannot be written afterwards config.ini is put back),
  `off` (config.ini off, then `mode=off`; a config.ini that cannot be restored leaves vpproxy.ini alone, because D33 with
  the 32-bit DLL draws no terrain), `status` (exit 0 off, 3 on, 4 partial, 5 no config.ini),
  `failed --pid --report [--options] [--config] [--deadline-s 180] [--no-ui] [--no-ui-choice open|remove|close]`: waits
  for the EXE's end; then the popup, and **the patch stays on** (D240, ticket 0682, W3c.6): nothing restored or set; the
  opt-in button "Remove 64bit patch" runs `off`. *Corrected 2026-11-22 (ticket 0682, D240): was "then off's config.ini
  part, `failed=1`, the popup with 'Try again next start' (= `on`)", which is now the route of the two stages off-switch
  and exe-build only; `--no-ui-choice retry` is gone.* Each step is
  logged in `<report>\helper.log`. Deadline passed: nothing restored or set, popup says to close the game and run
  `vp64helper off` (exit 3); config.ini not restorable (a switch-off stage): no `failed=` (exit 4); report folder
  missing: exit 1, the rest done. `popup` is unchanged (the stock-session fallback). vpproxy.ini is edited in place: the key's first line is replaced
  (its line end kept), later lines of the key removed, a missing key appended with the file's line end.
- Tests (0 skipped): `python -I proxy/tools/test_cfgini.py --scratch <dir> [--asan]`: unit 82/82 (CRLF/LF/BOM round trips,
  every negative case of the issue), the key list equal to darkred.py's with a negative control.
  `python -I proxy/tools/test_vp64helper.py --scratch <dir> [--asan]`: 81/81 with the clang build; the new cases cover on,
  off, status and failed, with byte compares and every refusal. Every helper run gets `VPPROXY_TEST_CONFIG_INI` at a path
  that does not exist and no `C5_DOCUMENTS`, so a case cannot reach a real config.ini. With `--asan` (MSVC
  `/fsanitize=address`, canary caught): test_cfgini 12/12 (the unit 82/82 clean), test_vp64helper 160/160 (73 checks
  under ASan, no report) [L 2026-11-14 05:0x]. Not yet live: S1.

### W3c.5 S1: D125's failure path live on the testbeds [L, 2026-11-14 05:23-06:1x]

Builds b2 of `task-3520-d125` at b7dc7778a (the team branch a546e663 merged: the 13-key A'' list without Waves),
`server/build_server.py --src D:/c5-64bit-work/9045/src-x64f --deps D:/c5-64bit-work/9045/bF/deps --config release
--offline` + `shell/build.cmd`: server.exe `a63213ac...`, proxy `f4f00e6a...`, vp64helper.exe `37e43b3b...` (step 5b
in a full build_proxy run: imports gdi32, kernel32, shell32, user32 only), composite `4e7becb7...`, x64 DLL `74588135...`
(the pin). Pack `3520-S1-b2` by `tools/release/pack.py --x86-dll` = the tag's own 32-bit DLL `262aee88...` (git show
Release-5.4.8), empty purity file. Save `TB-TINY_0008` (REL548-TINY_0008, turn 9). Job `tools/testbed/jobs/release_d125.py`
(hermetic tests `tools/testbed/tests/test_release_d125.py`): product_path's prep, the instance's own config.ini put back,
then `vp64helper on` (the installer's step: config.ini in the A'' form, `mode=on`), the pack with pack=1 and
`fallback_dll` = the tag DLL, one fault; `--next-start` and `--retry` continue in the same job. Stops: the harness with
the Lua channel on the 64-bit path, `stop_nolua.py` for the tag DLL (MC #586).
`test_proxy_session.py --build b2` (no ASan): 124/124.

| Fault (A'' config.ini) | Job | The fault start | Next start (failed=1) | "Try again next start" |
|---|---|---|---|---|
| server.exe missing | oldpc tb2 `20261114-053710-1bab` (fault part also `-053058-e39a`) | NO PLAY; EXE ended by the proxy 54.6 s after the launch; one report folder (stage server-start, code server-missing; no .dmp); helper.log: restored, failed=1 written, popup shown; config.ini = the player's bytes (SHA-256), no side file; popup with "Try again next start" (ACCEPTANCE vc-20261114-054120-oldpc-42d7) | the tag DLL in game, no server child, no notice in the title, map with terrain (ACCEPTANCE vc-20261114-054509-oldpc-ef02; the off-switch log line was not read by that run, a job bug fixed for the next ones); stop_nolua rc 0 | `vp64helper on` rc 0, config.ini = the first `on`'s bytes, failed= gone; the 64-bit path in game with server.exe, no FALLBACK (DIAGNOSTIC vc-20261114-054757-oldpc-4a31); cli stop rc 0 |
| pack hash mismatch (manifest names another 32-bit DLL) | laptop tb1 `20261114-053728-1e7a` | report stage pack, code other-dll, found 262aee88 / expected the induced zeros; the report says the game was ended; EXE ended 60 s after the launch; config.ini = the player's; failed=1; the popup with "Try again next start" (DIAGNOSTIC vc-20261114-053851-laptop-0a91, over a stale desktop image, below) | tag DLL in game, no server child; stop_nolua rc 0 | on rc 0, the 64-bit path in game, server child |
| server.exe killed in a game | oldpc tb2 `20261114-055051-340a` (exit 0, every check) | report stage server-death, code PeerDied (the failed call rt:LockSync, server exit code 15); EXE ended 5.5 s after the kill; config.ini = the player's; failed=1; popup (DIAGNOSTIC vc-20261114-055502-oldpc-d587) | `boot: ini ...: mode=on but failed=1` logged in `Logs/VP64/vpproxy.log`; tag DLL, map with terrain (DIAGNOSTIC vc-20261114-055852-oldpc-253f); stop_nolua rc 0 | on rc 0; 64-bit path in game (vc-20261114-060139-oldpc-1787); cli stop rc 0 |

- **A stock config.ini session** (no `on`, the player's own config.ini, server.exe missing), laptop tb1
  `20261114-052754-f4af`: FALLBACK to the tag DLL, the title notice "VP64 64-bit patch OFF: the 64-bit server did not
  start", one report folder without a .dmp, `vp64helper popup` shown 54 s after the launch over the game window
  (vc-20261114-052922-laptop-f05e), config.ini untouched, no failed=, in game on the tag DLL, stop_nolua rc 0. (Its exit
  5 came from a job bug: report lines counted as NO PLAY; fixed.) On oldpc tb2, job `20261114-060446-89b8`, exit 0:
  the same, popup 44 s after the launch (DIAGNOSTIC vc-20261114-060831-oldpc-eba2), then the stock game at turn 9 with
  terrain and the title notice, no popup over the map (ACCEPTANCE vc-20261114-061133-oldpc-ebb3, checked by eye).
- The report folders the runs left in the instances' Documents (VP64\reports) were removed afterwards by
  `release_d125.py --remove-reports` (oldpc `20261114-061619-3c9b`, laptop `20261114-061630-e6c8`; copies in the jobs'
  out folders); the job now removes its own at the end. Hermetic tests of the job: `test_release_d125.py` 18 (with
  `test_release_jobs.py`, 42 OK, 0 skipped).
- Every job restored its instance: product_path's restore ok (by hash, provision verify), config.ini at the end = the
  player's SHA-256, the host's My Games unchanged, the side file and every added file removed.
- **The 64-bit path's picture under A''** [L, two runs, oldpc]: after "Try again" the map draws with terrain, water and
  rivers, but the city, the resources and some units are dark red shapes and the minimap is dark (vc-...-4a31,
  vc-...-1787). Not examined here (not 3520's); passed to the lead.
- **Capture on the laptop** (MC #572) [L, jobs f4af and 1e7a, `--capture-probe`]: the whole-screen grab (PIL ImageGrab)
  shows the 32-bit and the 64-bit game window as black (mean 8.75 of 255); at two other moments it returned a Task
  Manager window with the taskbar clock at 03:03 while the grab ran at 05:29 and 05:38, so the laptop's desktop image
  behind new windows is stale (the session's screen is not being repainted); our popup, a new top-most window, is drawn
  correctly over it. PrintWindow with PW_RENDERFULLCONTENT (flag 2) of the game window
  gives only the title bar, the client area black (mean 7.4), on both routes. The helper's own popup window is captured by
  ImageGrab. So no tested capture path shows the game on the laptop; the D3D11 swap chain needs another path (DXGI
  desktop duplication or the composite's back buffer, not tried here). Game pictures stay on oldpc, where ImageGrab works.
- *Note 2026-11-22 (ticket 0682, D240):* the "Next start (failed=1)" and "Try again next start" columns describe the
  helper before D240. Since then a failure in a D33 session leaves the patch on (W3c.6). `release_d125.py` follows
  D240 since 6fe8c2a76 (merged with 0682): Close = the popup ended (patch on); `--next-start` answers Remove by
  `vp64helper off`; `--retry` runs `on` only after Remove.

### W3c.6 D240 (ticket 0682): after a 64-bit failure the patch stays on; "Remove 64bit patch" is opt-in [C, tests 2026-11-22]

The owner's decision D240 (2026-11-22): when the 64-bit path fails in a D33 session and `vp64helper failed` shows its
popup, the patch stays on, so the next start uses the 64-bit patch again; the popup offers "Remove 64bit patch", which
switches it off as the old default did. The MC approved the design (#10729, #10736, 2026-11-22 21:33).

- **What `failed` does now** (`proxy/helper/vp64helper.cpp` `CmdFailed`) [C]: waits for the EXE's end as before (W3c.2),
  then the popup. Config.ini, VP64Options.ini and vpproxy.ini are left as they are, and no `failed=` is written. helper.log
  says `stage <s>: the patch stays on (D240)` and `nothing restored, nothing set: the patch stays on`, and after the
  popup closes either `popup closed: the patch stays on` or `popup closed: the patch was removed (Remove 64bit patch)`.
  The popup's first paragraph: "The 64-bit part of the patch failed, so the game was ended. The patch stays on: the next
  start uses the 64-bit patch again. "Remove 64bit patch" switches the patch off and puts your own game settings back,
  so that the next start is the normal 32-bit game (vp64helper.exe on switches it on again later). "Close" keeps the
  patch on." Buttons: `Open folder | Remove 64bit patch | Close` (button id 103, the old "Try again" id).
- **"Remove 64bit patch"** = `off` (`DoOff`, shared with the `off` verb): config.ini back from the side file, then
  VP64Options.ini's ServerRenderer, then `mode=off`. It writes no `failed=`: `mode=off` is the off switch, and `on`
  undoes it. It is logged as `Remove 64bit patch pressed: off ok|FAILED: <off's message>`, and its result is added to the
  popup text. If it fails (for example a read-only config.ini) nothing is half-done: off's rule that config.ini comes first
  holds, the patch stays on, and the line says to run `vp64helper.exe off`. Without a usable side file (issue 0378)
  config.ini is left as it is, `mode=off` is written, and the line says how to put `[DisableSystems]` back by hand: Decal,
  Forest, FOW, FluidFOW, DistanceFog and YieldIcons to 0 (the game's own values), OverlayDebug and ColorKeyEdit stay 1
  (the stock-off pair, `proxy_session_keys.h`); stage off-switch's line without a side file says the same (`ConfigByHandText`,
  built from the key list) `[C]` 2026-11-26, ticket 0811. *Corrected 2026-11-26: until then both lines said "back to 1 by
  hand", which left six render systems off in the plain game.* The
  next start then takes 0635's off-switch route, as `failed=1` did before. --no-ui: `--no-ui-choice remove` prints
  `remove: off ok: ...` or `remove: off FAILED: ...`. The exit code is still `failed`'s own; a Remove does not change it.
- **Two stages keep the switch-off of before D240**, with no Remove button (`--no-ui-choice remove` does nothing there):
  `off-switch` (ticket 0635: the patch was already switched off) and `exe-build` (ticket 0588: an EXE build this
  version does not support; staying on would end every start with no play). Exe-build's popup now says why: "This
  version of the patch does not support this build of the game, so the patch could not stay on: every start would end
  the same way". Its "Try again next start" sentence became "vp64helper.exe on ... switches the patch on again once a
  version of the patch that supports this game build is installed".
- **Deadline passed** (the EXE did not end within `--deadline-s`): no Remove button (the EXE writes config.ini at its
  start, so a restore while it may still run is not safe), exit 3. Text: the patch stays on, and to remove it, close the
  game and run `vp64helper off`.
- **The `popup` verb** (stock sessions; the server's exe-crash and dll-fatal popups) has no Remove button: it switches
  nothing (MC #10736). A follow-up may add one.
- **The proxy's player text** (`proxy_rt.cpp`, `proxy_report.cpp`): no line promises "the next start is the stock game"
  any more. The D33 summary is now "The 64-bit part of the patch failed (<stage>: <code>); the game was ended." The
  save-variant summary in a D33 session ends "...was not loaded; the game was ended." The options-renderer-off summary ends
  "The game was ended. To play with the patch, run vp64helper.exe on (it sets ServerRenderer = 1)." The composite-refused
  summary ends "...run the patch's installer again. The game was ended." report.txt's D33 paragraph: "The patch stays
  on: the next start uses the 64-bit patch again, unless "Remove 64bit patch" was chosen in the popup (helper.log in this
  folder says which)". The exe-build stage keeps the old paragraph, which says the patch is switched off.
- **The owner's crash of 2026-11-22** went through server exit 6, which server-fatal.txt records as code
  `engine-exception` (`srv_exe_crash.cpp` `FatalWords`, ticket 0641). The proxy's stage was `server-death` with that
  code (`proxy_rt.cpp`: a death in a game takes the record's code), in a D33 session, so `failed`. With D240 that route
  stays on [C, offline case `failed (D240), server-death: engine-exception`].
- **Tests** [L 2026-11-22, workstation]: `python -I -X utf8 proxy/tools/test_vp64helper.py --scratch <dir>`: 215/215
  (21:33-21:36 EST). With `--asan`: 428/428, 207 checks under MSVC ASan with no report, the canary caught. The proxy's
  report code: `test_proxy_session.py --only unit --asan` 15/15 (session_unit 141/141 under ASan). The cases: Close
  (stays on; files byte for byte; helper.log), Remove (off; mode=off; no failed=; `on` again afterwards; read-only
  config.ini; no side file), every stage but two and look-alike stage names stay on, exe-build and off-switch switch
  off, deadline, no report folder, the engine-exception case, usage (`retry` refused on both verbs, `remove` refused on
  `popup`).
- **Live check** [L, 2026-11-22 21:54-22:03 EST, workstation, Goldberg instance c5h-new-a, EXE build 26577625]. Layout:
  this branch's release build (runbook 1.2, the pins-0.1b deps, repo 05338f1c0), its shell build, and a TEST pack with
  pack=1, laid by `server/evidence/0600/lay.py`. The failure was forced with vpproxy.ini `server_cfg_extra` (TEST ONLY)
  `render_test_init_fail=device`: the server's renderer failed at init, the proxy reported stage server-death, then the
  `failed` route. **Close: PASS.** The popup had the new text and the buttons Open folder | Remove 64bit patch | Close.
  config.ini and VP64Options.ini kept their SHA-256, vpproxy.ini stayed at mode=on with no failed=, and helper.log said
  "popup closed: the patch stays on". The next start with the key removed booted the 64-bit server (kRt_Boot ok, no
  FALLBACK and no NO PLAY) and was in game with the Lua channel. **Remove: PASS** [L, 2026-11-22 22:31-22:37 EST,
  c5h-new-b, build 26577625, with MC-B's override for this one press]. After the game had ended, `cli.py dismiss --button
  "Remove 64bit patch" --coordinator-override ...` brought config.ini back to its pre-`on` SHA-256, removed the
  VP64Options.ini that `on` had created, set vpproxy.ini to mode=off with no failed=, and removed both side files. helper.log
  said "Remove 64bit patch pressed: off ok". The popup stays open after the press (the harness reports "posted, still
  open"), so Close follows. Trap: a game that the failure ended was not stopped by `cli.py stop`, so its front-end backup
  is still in place and `provision.py verify` refuses until `frontend.py -i <inst> restore` has run.
  **The owner's exit-6 route is not verified live**: no
  test key forces an engine exception in a server call; it is the same server-death D33 route. Evidence:
  `server/evidence/0682/acceptance.txt`; the snapshot tool is `server/evidence/0682/snap.py`.

## W3d (3521): the error report and the popup helper on every 64-bit failure (D119, D124; 2026-11-14)

Issue `tracker/issues/3521-proxy-session-rule-and-error-report.md`; the whole description, the report's fields, the
privacy scrub and the tests are in `research/03-dll-engine-boundary/proxy-design.md` section 22 (21 until main's ticket 0164 took that number, 2026-11-14). In short [C, L offline]:
with `pack=1` every failure that 3504 turns into the 32-bit fallback, and the first failed call in a game, now also writes
`Documents\My Games\Sid Meier's Civilization 5\VP64\reports\<yyyymmdd-hhmmss>\` (report.txt, popup-summary.txt, log
tails, copies of manifest.json / vpproxy.ini / VP64Options.ini, a server-written `*.dmp`) and starts
`vp64helper.exe popup --report <dir> --options <vpproxy.ini>` (issue 3520's helper) detached; the session then goes on as
3504 built it (D124: no D33-session branch, no end of the game, config.ini never touched; D119's first plan for 3521 was
dropped at 03:31 before it was tested). Found on the way [L]: the 32-bit proxy cannot minidump the 64-bit server
(MiniDumpWriteDump 0x8007012B); a server crash dump needs the server's own filter (follow-up). Offline:
`test_proxy_session.py` 66/66 (unit 38/38 also under ASan), `test_proxy_rt.py` 79/79, `test_proxy_pack.py` 72/72. No
game was run.

**Correction 2026-11-14 05:10 (issue 3523, the owner's D125 reversed D124):** the above is now the stock-session branch only.
A failure in a D33 session (config.ini in the A'' form: any of the 12 rule keys off in the config mask) never plays: the
report, `vp64helper failed --pid <EXE pid> --config <config.ini> --report <dir> --options <vpproxy.ini>`, then the end of
the game (exit code 3, as `on_server_death=exit`); `failed=1` in vpproxy.ini is the off switch (since D240, 2026-11-22,
the helper writes it only for the stages off-switch and exe-build; otherwise the patch stays on, W3c.6); the server's `*.dmp` is
named in report.txt but no longer copied (MC #577; was: "a server-written `*.dmp`" among the copies). The one key list is
`proxy/src/proxy_session_keys.h`. Details and tests: proxy-design.md 22.8.

## W5 (3506): the validated tier (`refsave`, `instance`, `wholegames`, `report`)

Issue `tracker/issues/3506-validated-tier.md`. The stages are registered in `tools/release/stages/validated.py`
(`STAGES`), which 3502's hook `take_release.tier_stages('validated')` appends to the candidate stages (commit c4232b27:
no edit to `take_release.py`).

### W5.1 P10 on the pin: the 5.4.8 huge reference save loads and plays two turns [L 2026-11-13 22:58-23:01 EST]

A copy `D:/c5-64bit-work/3500/w5/p10/w5-p10-HUGE_0350.Civ5Save` (SHA-256 `c499d3c6...88c3` = the descriptor's
`reference_saves.huge`) through `python -I server/run_server.py --load <copy> --turns 352 --run
D:/c5-64bit-work/3500/w5/p10/run --bin <pins_dir>/dlls/x64-04b1211b-pa803776/bin --save-out w5-p10-HUGE_0352`
(the `deps/dll.lock` release layout): exit 0, `result: passed; turns 350 -> 352`, pregame 13,798 bytes and body
37,694,481 bytes used up exactly, Lua errors 0, 0 engine calls outside `unserved-allowlist.txt`; turns 12.1 s and
9.2 s (functional, not a timing: the workstation was busy); boot phase 107.5 s (database copies to D:, which was
saturated by an 8.2 GB instance copy at the time, `d-drive-load.md`). Wall 201.6 s. On the pin this is the expected
result (the save was written by 5.4.8); the check is the stage's first step on a new release, where it decides whether
a new reference save is needed.

### W5.2 The reference game: fixed-seed huge to turn 350 on the x64 server [L, running]

Started 2026-11-13 23:01 EST (`D:/c5-64bit-work/3500/w5/scripts/huge350.sh`):
`python -I server/run_server.py tools/i0/specs/continents-huge-fixed-seed.json --turns 350 --run
D:/c5-64bit-work/3500/w5/huge350/run --bin <pins_dir>/dlls/x64-04b1211b-pa803776/bin --dll-log-flags 0
--save-out W5-HUGE_0350 --timeout 21600` (Continents 128x80, 12 majors and 24 city states by the world's defaults,
standard speed, ancient era, Emperor, seeds map 1001 / sync 2002; the descriptor's test database; AI logging off as
`run_d10.py` passes it to the server, D49). **Result [L, 2026-11-13 23:01:28 - 2026-11-14 01:50:12 EST]: passed, turns 0 ->
350, Lua errors 0, 0 engine calls outside the allow-list, peak working set 343 MB; wall 10,123 s (autoplay 9,983 s, 28.5
s/turn on average; from turn 252 65-240 s a turn with the run folder on the saturated D:, so not a timing).** Save
`W5-HUGE_0350.Civ5Save`, 2,850,162 B, SHA-256 `cbbaab32bf47cd8ecc3483ccb7756511d4735a27d934fdf88b3a7347ba93c6f5`, kept as
`D:/c5-64bit-work/3500/inputs/W5-HUGE_0350.Civ5Save`; the run folder (database copies removed) stays at
`D:/c5-64bit-work/3500/w5/huge350/run` for `C5_RELEASE_REFSAVE_RUN`. (Replaced 2026-11-14: "Expected ... Result below
when it ends".)

**The adoption through refsave: refused, correctly; the save is a game one step from its end [L, 3514, 2026-11-14
01:51:55-01:52:38 EST].** `C5_RELEASE_REFSAVE=new C5_RELEASE_REFSAVE_RUN=D:/c5-64bit-work/3500/w5/huge350/run
take_release.py Release-5.4.8 --tier validated --only refsave --who 3514` in the work folder
`C:/c5-64bit-work/3500/3514/rel548` (seeded with the pins as in W5.4). The P10 of the descriptor's old save passed (28 s).
The adoption took the run's `W5-HUGE_0350.Civ5Save` (the run has no `<tag>-REF-HUGE_0350` file, so the stage takes the
run's newest save), copied it as `Release-5.4.8-REF-HUGE_0350.Civ5Save` and ran the two-turn check (`run_server.py --load
... --turns 352`, 14 s). The save loaded at turn 350 (pregame 13,392 B and body 43,571,283 B used up exactly). Then the AI
launched its spaceship on the first update ("queued LaunchSpaceship 1"), and the server reported `GAME OVER at turn 350:
winner team 1, victory type 1` (team 1 is the Maya, the leader in techs with 82 of 82; victory type 1 is the space race
[I, the order of `VictoryTypes`]): `result: game_over; turns 350 -> 350 (0 played)`. The stage failed: "does not load
and play: result game_over; ended at turn 350, target 352". **So on the D96 pins the fixed-seed huge game
(continents-huge-fixed-seed, seeds 1001/2002) ends in a Maya science victory at turn 350.** A save taken there cannot play
on to T+2, and it is no reference save. The stage's check caught it, so the adoption path works as written. A new
reference game would need a spec with the space race off (or every victory off) or an earlier reference turn: the
descriptor's owner decides (3501). (2026-11-14, ticket 0175: every victory off, by a Lua probe because the spec's list
cannot do it on the pins; W5.11.) On Release-5.4.8 nothing changes: the old save `E2000-HUGE_0350` passes P10 and stays
the reference. After the refusal, refsave ran again in `auto` mode: ok, origin descriptor (01:53:23-01:53:50). Not run:
an x86 load of `W5-HUGE_0350`.

### W5.3 The stages of the validated tier [C, the code; hermetic tests 2026-11-13]

`tools/release/stages/validated.py` registers `refsave` (CL 2), `instance` (CL 16-17), `wholegames` (CL 14) and `report`
after the candidate stages (3502's hook, README 2b). Order: the workstation checks first, so a failing route stops the
tier before the testbed hours. Settings the driver's command line does not carry are environment variables, recorded in
the stages' inputs (a change runs the stage again): `C5_RELEASE_REFSAVE=auto|new`, `C5_RELEASE_REFSAVE_RUN=<run folder>`
(adopt a finished game instead of playing it again), `C5_RELEASE_INSTANCE=<name>`, `C5_RELEASE_TESTBEDS=oldpc,laptop`.
Tools the driver's `default_tools()` lacks (`tb`, `pool`, `provision`, `arm`, `frontend`, `visualcheck`) come from
`validated.TOOLS`; a name in the driver's tool table wins (the tests' fakes).

- `refsave`: P10 first (`run_server.py --load <copy> --turns T+2`, gated), the old save stays the reference when it
  loads and plays; else (or `new`) the fixed-seed huge game (`tools/i0/specs/continents-huge-fixed-seed.json`) from turn
  0 to T with `--dll-log-flags 0`, then the same two-turn check on the new save. A new save is reported as
  `descriptor_update` (the descriptor is 3501's); the stage never edits it. A rerun after an interrupted game reuses a
  finished run folder (`game_run` in the partial outputs).
- `instance`: claims an instance (`pool.py claim`), checks that its MODS hold the descriptor's mods (`<folder> (v N).modinfo`;
  another release's mods = stop, CL 17), gives their `.modinfo` the VFS imports of the `.civ5proj` (0310, 2026-11-15:
  `stages/_modinfo.py`, backed up and restored by hash with the instance's `Civ5ModsDatabase.db`; checklist step 2),
  `set-dll` to the build's instance-aware DLL (the old DLL kept under
  `<work>/instance/kept/`), `set-proxy` from a **test pack** made with `pack.py` (`--test-x86-dll` = the instance's DLL,
  so the boot check accepts it with `pack=1`; README section 5), then copies the pack's `manifest.json` and `purity.tsv`
  into `VP64\` before the game starts (the MC's conditions of thread #263). Two arms (`proxy/tools/live/arm.py`, gated):
  Tiny save (`D:/c5-64bit-work/3500/inputs/REL548-TINY_0008.Civ5Save`, a copy of 9029's live Tiny turn-8 save, SHA-256
  `b2ceb663...9163`) and the refsave stage's huge save, each copied into the instance's `ModdedSaves/single` as
  `REL-<tag>-TINY|HUGE`, `local_answers=1 local_verify=1`, 10 AI turns, `--shot`. Pass per arm: arm exit 0, the proxy's
  report line with verified > 0, `MISMATCHES 0`, `LOCK ERRORS 0`, no `VERIFY MISMATCH` and no `boot: FALLBACK` line.
  The harness's own Visual Checks record is off for the arm (`C5_VISUAL_OFF=1`: arm.py's `cli.py shot` has no
  `--what`, so it would leave an undescribed entry); the stage records `end.png` itself with what, result and issue
  (`--issue` = the driver's `--who`). Restore always runs in reverse order (pack files, saves, front end, `set-proxy
  --original`, `set-dll` to the kept DLL), then `provision.py verify --quick` (under `quietlock.py run`; D110 of
  2026-11-13 23:40: a full verify at most once a day per instance until 0161, and short runs on the C: instances
  c5h-a/b/c, so set `C5_RELEASE_INSTANCE`; `--quick` checks the install by size and the user tree, where every file
  this stage touches lives, by hash), then the claim is
  released; a failed restore is a failure that names the instance. Exception: an arm exit 8 (a dialog open, D29) leaves
  the game, the layout and the claim untouched.
- `wholegames`: one `tb.py submit` per host (not `--quiet`), `longgames.py` with the build's x64 `bin/` and plain
  32-bit DLL shipped as `rel/x64/bin` and `rel/x86/...`, `--env C5_RELEASE=<tag>`; games (9045's seeds, all x64 = x86 on
  the D96 pins): Tiny Continents 5051/5253, Standard Fractal 5757/6868, Standard Pangaea 3333/4444, Small Pangaea
  1819/2021, to the end, round robin over the hosts. For `--major-civs` above 22 (ticket 0372, 2026-11-15) the stage plays the 30-major game on both arms instead; live on the release-43 builds x64 43 = x86 43 (16 tables, 3,613,758 rows): `research/03-dll-engine-boundary/player-limits.md` 8.3, `tools/release/README.md` 2c. Job ids are recorded at once (`ctx.partial`), so a rerun waits for
  the same jobs. Waits are `tb.py wait --timeout 170` re-armed with the stop file checked between. Parity per game:
  `identical`; `identical but known` (only `ReligionBeliefYields.IsReligionOwner` differs, same row counts:
  quiet-testbed.md 14.6); else `different`. Any `different` = stop, outputs `parity: failed`, and the report's verdict
  "candidate, parity failed" (decision 1, the owner's). A run without a pass verdict or a missing comparison = stop
  (`parity: incomplete`); a testbed database other than the descriptor's `test_db_sha256`, or a testbed x64 DLL other
  than the build's, = stop / failure.
- `report`: `<work>/validated-report.json` and `.md` (every stage's status, outputs and inputs, the builds, the
  settings, the verdict). Every validated stage rewrites it as it ends (`report.finish`), so a stop leaves a current
  report; the `report` stage stops unless the verdict is `validated`.

Tests: `python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_validated_tier*.py"`: **31 pass, 0
skipped** (2026-11-13 23:36, 47-69 s, under `quietlock.py run`), with `tests/fake_validated_tool.py` (fake testbed,
instance, pool, provision, arm, pack, server). Error paths covered: gate refusal (exit 3), P10 failure then a new game,
new game failure, hash mismatch, no save in the descriptor, adoption and adoption at the wrong turn, a bad mode; no free
instance, a launch gate refusing an arm's game start (a stop, restored), mismatches (restore still runs), failed verify after restore, another release's mods, a dialog open (nothing
restored), no pack stage, set-proxy failure (DLL restored), missing Tiny save; known column, a difference (parity
failed), known plus another column, a failed run, submit failure, rerun waits for the same jobs, another testbed
database, another x64 DLL, the stop file while waiting, one host; the report's verdicts and the parity rules. 3502's
`test_take_release.py` 45/45 with `validated.py` present.

### W5.4 The validated tier live on Release-5.4.8, standalone on the pins [L 2026-11-13/14]

3502's candidate run had not finished (its run resumed 23:22 at `testdata`), so the validated stages ran with
`take_release.py Release-5.4.8 --tier validated --only <stage> --who 3506 --fork D:/c5-64bit-work/3500/w1b/fork` in work
folders whose `build` (and a `pack` marker) were seeded with the pins' layouts (`deps/dll.lock` release
`x64-04b1211b-pa803776`, `deps/i0.lock` `i0ship-26234371` and its `-ia` build): `D:/c5-64bit-work/3500/w5/scripts/
seed_rel548.py`. The report of such a folder says "candidate, validated tier not reached (survey pending)": the candidate
stages were not run there. Folders: `wholegames` in `D:/c5-64bit-work/3500/w5/rel548` (light: logs only; the games run on
the testbeds), `refsave` and `instance` in `C:/c5-64bit-work/3500/3506/rel548` (D111 and the owner's order of 23:55: run
folders on C:).

- **refsave** 2026-11-13 23:59:55-00:00:35 EST: ok, origin `descriptor` (P10: `E2000-HUGE_0350` loaded at 350 and
  played to 352 on the pin), **40 s** with its run folder on C: against **201.6 s** for the same command at 22:58 with
  the run folder on D: (boot phase 107.5 s there: the database copies; `d-drive-load.md`) [L, one run each; the
  machine was loaded both times, so an indication, not a timing].
- **wholegames**: first submitted to the laptop only (23:28, my branch's `tb.py` read oldpc's runner as DOWN: the status
  format changed with 0156, so merge the team branch before reading runner states); stopped through the stop file (exit
  10 between two waits, the stage's stop path live) and the job cancelled; resubmitted 23:33 to both hosts: oldpc
  `20261113-233311-fa2c` (Tiny Continents 5051/5253, Standard Pangaea 3333/4444), laptop `20261113-233328-e2e8`
  (Standard Fractal 5757/6868, Small Pangaea 1819/2021). Result below when they end.
- **instance**: waits for a free C: instance (c5h-a, c5h-b, c5h-c all claimed at 00:00; D: instances barred by the MC at
  23:55): `instance_when_free.sh` polls the pool every 2 minutes. Result below. Note [C, the pin's build date]: the pin's
  proxy (`x64-04b1211b-pa803776/proxy/bin`, 2026-11-13 12:08) predates 3504's `proxy_pack.cpp`, so on the pin layout the
  test pack's `manifest.json` is laid and `pack=1` set, but the boot check and the purity load are only exercised by a
  proxy built from the team branch (the candidate tier's `build` stage).
- **The reference game** (W5.2): from turn 252 its turns took 65-240 s (working set falling 221 -> 201 MB; run folder
  and `stats.db` on the saturated D:); the MC let it run to its own end (turn 350 or its `--timeout`, 05:01).

### W5.5 The fallback ACCEPTANCE picture with the tag's DLL: not taken; an incident on the laptop [L 2026-11-14 00:18-00:22]

Asked by the MC (#361, D112): one ACCEPTANCE picture on a testbed of the full shipped system with `pack=1`, the 32-bit
fallback being the tag's own DLL (`262aee88...`, `git show Release-5.4.8:"(1) Community Patch/CvGameCore_Expansion2.dll"`
in the fork clone), one induced fault, the title notice, the whole screen. Job `tools/testbed/jobs/release_fallback.py`
(reuses `product_path.py`'s prep, stop and restore unchanged); builds 3504's `D:/c5-64bit-work/3500/w3/b3` (server and
proxy with `proxy_pack.cpp`), 0156's `shell_composite.dll`, a pack written by `pack.py` from b3 with the tag DLL.

- **Negative result [C, bytes; L, the job log]: the tag's published DLL has no Lua channel** (no `VPLuaExec` in its
  bytes), so with it as the fallback the harness cannot tell that the game is in game (`frontend.py ingame`: "Lua channel
  silent") and its stop is refused once the game is in (`turns.frontend_state`: an InGame context is sticky, so the
  front end is never idle; only `--coordinator-override` passes). A harness-driven job with the tag DLL therefore cannot
  end safely by itself. The job now refuses before any change when the fallback DLL has no channel. (Since 0176,
  2026-11-14: `cli.py stop` itself detects a mapped game DLL without the channel, the fallback included, and stops it
  the `stop_nolua.py` way; `run-and-drive-the-game.md` section 16.)
- **`tb.py cancel` ends the job's process tree at once** (`tools/testbed/remote/wrapper.py`: cancel.flag or the timeout
  kills the command's tree, except game EXEs), not "between steps": a job's own stop and restore do not run after a
  cancel. (`product_path.py`'s docstring says a cancel is honoured between steps; that holds only for its own
  `check_cancel()` calls, which the wrapper's kill pre-empts.)
- Jobs: laptop `20261114-001813-a560` (tb2): prep complete, game launched 00:18:59, cancelled 00:20:52: **tb2 left with the
  test layout and most likely the game running**, reported to the lead and the MC for a coordinator's stop and
  `product_path.py --phase restore --instance tb2 --from-job 20261114-001813-a560` (after removing
  `CvGameCore_Expansion2_tag.dll`, `VP64/manifest.json`, `VP64/purity.tsv`). oldpc `20261114-001912-c760` (tb1): killed after
  62 s, before its first step (no step in `steps.jsonl`): no change to tb1 [I].
- Also from the lead (2026-11-14 00:19): the laptop lacks Vibrant Textures (D94), so an ACCEPTANCE picture comes from
  oldpc.
- **The repair (MC #369, one stop granted) [L 2026-11-14 00:27]**: job laptop `20261114-002731-38c7`
  (`tools/testbed/jobs/release_tb_repair.py --phase stop-restore`): the save folders were still (newest file
  `ModdedSaves/single/TBPP-HUGE_0350.Civ5Save`, mtime 2026-11-10 08:22:06, no autosave), then `cli.py -i tb2 stop
  --coordinator-override "<#369's text>"` at 00:27:51: **refused (rc 2): a "Game Crash" dialog is open** (D29: nothing
  stopped, removed or restored). **The 32-bit game crashed at 00:21:33** with the tag DLL 5.4.8 as the fallback
  (`DLL-Version: Release-5.4.8-0-g3dc5bea6e Clean`), loading `TBPP-HUGE_0350` with the composite and `ServerRenderer =
  1` laid on: `Exception: 0xc0000005`, access violation write to `0x0000000c` at `CivilizationV_DX11.exe+0x004cc892`
  (file offset, as the crash box prints it: VA `0x8CD492` by AGENTS' rule, live `0x0138d492+0x004cd492`), the 32-bit
  address space nearly exhausted (Sub2G committed 1,806,212 KB, reserved 246,228 KB, free 44,648 KB; largest free block
  below 2 GB 13,760 KB), minidump `CvMiniDump_20261114_002133_5.4.8-0-g3dc5bea6e_Release.dmp` in tb2's `crashlogs`.
  Whether the shell parts (composite, ServerRenderer) still load on a fallback boot and add to the address space is
  [I], asked of 3504 by the lead. The box needs a coordinator's D29/D60 dismiss; `release_tb_repair.py` takes
  `--dismiss-button OK --requested-by WHO` for it, only on the MC's word.
- **tb1 on oldpc [L 2026-11-14 00:28]**: job `20261114-002749-3a41` (`--phase check`): no proxy layout, none of the three
  added files, `provision.py verify --quick tb1` rc 0: untouched (job c760's My Games backup had not written its
  manifest before the kill, so the check is against the instance's own manifests).

#### W5.4a The instance stage live on c5h-b [L 2026-11-14 00:11-00:35]

`take_release.py Release-5.4.8 --work C:/c5-64bit-work/3500/3506/rel548 --tier validated --only instance` with
`C5_RELEASE_INSTANCE=c5h-b` (the first C: instance that came free; c5h-b carried `i0ship-bbfacca1-ia` `e589306e...`):
claim 10 s, `set-dll` to `i0ship-26234371-ia` 6 s, the test pack (`pack.py` with `--test-x86-dll` = that DLL) 2 s,
`set-proxy` from the pack 7 s, then the two arms in verify mode (`local_answers=1 local_verify=1`, `pack=1`, 10 AI
turns + the checklist):

| Arm | Save | Wall | Verified answers | Mismatches | Lock errors | FALLBACK lines |
|---|---|---|---|---|---|---|
| tiny | `REL-Release-5.4.8-TINY` (9029's live Tiny, turn 8) | 427 s | 2,197,132 | 0 | 0 | 0 |
| huge | `REL-Release-5.4.8-HUGE` (`E2000-HUGE_0350`, the refsave stage's) | ~15 min | 985,300 | 0 | 0 | 0 |

Both end shots are in the Visual Checks feed as diagnostics (D112). `purity` is empty in both logs: the pin's proxy
predates `proxy_pack.cpp` (W5.4), so `pack=1` and the manifest had no effect there.

**The restore failed (fixed since)** [L]: `frontend.py restore` exited 1 (`No module named 'backup'`: run with
`python -I`, which implies safe-path, so the script's own folder is not on `sys.path`; harness scripts run without
`-I`, as `arm.py` runs them. Corrected 2026-11-15: since issue 0331 every harness script also runs under `-I`,
`run-and-drive-the-game.md` section 11.3); `set-proxy --original` refused ("run outputs present under ...VP64\run (48 files): pass
--runs-to DIR"); `set-dll` back refused (still proxied); `verify --quick` rc 1. The stage then released its claim on an
instance it had not put back: wrong. Repaired by hand at 00:37-00:40 under a new claim (`repair_c5hb.sh`: the same
steps, front end without `-I`, `--runs-to`), `verify --quick` ok, DLL `e589306e...` again, claim released. The stage
now runs the front end without `-I`, passes `--runs-to <work>/instance/vp64-runs`, and keeps the claim when a restore
fails (tests follow: the fake refuses `--original` without `--runs-to` while `VP64\run` exists). A second live run
started 00:41 to show the fixed restore end to end.

### W5.6 Results of 2026-11-14 00:40-01:20 [L]

**wholegames** (`D:/c5-64bit-work/3500/w5/rel548`, jobs oldpc `20261113-233311-fa2c`, laptop `20261113-233328-e2e8`;
the pins' builds `x64-04b1211b-pa803776/bin` and `i0ship-26234371`; AI logging off, the testbed database `bd9b8aa1...` =
the descriptor's on both hosts): stage **ok, parity passed**, 4 of 4 whole games x64 = x86, every table identical but
the known `ReligionBeliefYields.IsReligionOwner`:

| Host | Game | End (both arms) | Rows compared | `IsReligionOwner` rows |
|---|---|---|---|---|
| oldpc | Tiny Continents 5051/5253 | Cultural, turn 361 | 903,979 | 1,273 |
| oldpc | Standard Pangaea 3333/4444 | Cultural, turn 370 | 1,622,664 | 2,211 |
| laptop | Standard Fractal 5757/6868 | Science, turn 414 | 2,255,970 | 2,859 |
| laptop | Small Pangaea 1819/2021 | Science, turn 427 | 1,708,434 | 1,575 |

The row counts equal 9045's on the same seeds (upstream-fix-batch-1.md 8f): the same pins give the same games. oldpc's
job took 23:33-00:07 (34 min), the laptop's 23:33-01:08.

**instance, second live run** (00:41-01:09, c5h-b): both arms pass (tiny 1,949,623, huge 1,686,427 answers verified, 0
mismatches, 0 lock errors); the restore failed again, now on `--runs-to <existing folder>` ("must be new": the stage
created it with `ctx.dir`). The stage kept the claim this time (the first fix held); c5h-b repaired by hand (`set-proxy
--original --runs-to <new>`, `set-dll` back to `e589306e...`, `verify --quick` ok), claim released 01:1x. Fixed: a new
folder per call; the fake refuses an existing `--runs-to`. The stage takes no picture any more (D114: `arm.py`'s end
shot comes after its checklist with popups open; the four earlier entries carry a note).

**The crash at `CivilizationV_DX11.exe+0x004cc892`** (file offset as the crash box prints it; write to `0x0000000c`,
stack `exe+0x6365a, (+0x2570b0), +0x339af0, +0x2570e8, +0x3495a1, +0x4dbc6a, +0x228286`) [C, three crash logs]:

| When | Where | DLL | Save | Sub2G committed / free / largest free | Minidump |
|---|---|---|---|---|---|
| 2026-11-13 23:42:55 | workstation c5h-c (claimed by 3600) | `Release-5.4.8-39-g26234371e+ia440878983` (the pin's -ia) | ? | 1,646,200 / 98,512 / 13,996 KB | `CvMiniDump_20261113_234255_5.4.8-39-g26234371e+ia440878983_Release.dmp` (in c5h-c's crashlogs) |
| 2026-11-14 00:21:33 | laptop tb2 (fallback, `pack=1`) | the tag's `Release-5.4.8-0-g3dc5bea6e Clean` | huge `TBPP-HUGE_0350` | 1,806,212 / 44,648 / 13,760 KB | `CvMiniDump_20261114_002133_5.4.8-0-g3dc5bea6e_Release.dmp` (copy in `D:/c5-64bit-work/3500/evidence/`) |
| 2026-11-14 00:59:09 | oldpc tb2 (fallback, server.exe absent) | the tag's DLL | Tiny `TBPP-TINY_0008` | 1,528,972 / 299,448 / 138,544 KB | `CvMiniDump_20261114_005909_5.4.8-0-g3dc5bea6e_Release.dmp` (in oldpc tb2's crashlogs) |

So the crash is not the 32-bit address-space limit (the oldpc one had 138 MB free in one block) and not specific to the
tag DLL or the fallback route (c5h-c ran the pin's DLL). What the three share is [I] the product path's EXE-side
settings (`config.ini` D33 with Overlay on, `VP64Options.ini` ServerRenderer = 1; 3600 runs such settings on c5h-c):
the lead's hypothesis (#423) is a stock DLL in an EXE with world systems off crashing where the server would draw.
**Confirmed [L] 2026-11-14 02:05 for `config.ini` D33 alone (W5.7). `VP64Options.ini` is not needed for the crash.**
The control (`release_fallback.py --control [--d33]`: the tag DLL installed directly, no proxy, the same Tiny save,
with and without the D33 `config.ini`) separates the causes. Results in W5.7.

### W5.7 The controls for the crash at `exe+0x4cc892` (3514 item 1) [L, oldpc tb2, 2026-11-14]

Job `tools/testbed/jobs/release_fallback.py --control [--d33] --instance tb2 --stop-nolua --save TB-TINY_0008
--save-file` (MC #413 b, #430, #456). The tag DLL 262aee88 is installed directly with `provision.py set-dll` in place of
the instance's `e589306e...` (i0ship-bbfacca1-ia). No proxy, no pack, no composite, no `VP64Options.ini`. The save is the
same Tiny turn-8 save as job f5f8 (`b2ceb663...`, copy `TBPP-TINY_0008`), loaded by the front-end autoload with the
mods cp, vp and eui. The job waits for `InGame` or `LoadScreen` in `Lua.log`, waits 180 s more, and then reads the
windows of the game process for a "Game Crash" box. The stop goes through `stop_nolua.py --settle 60`. Then set-dll
back, the front end and the standard files are restored by hash, `verify --quick` runs, and My Games is compared by
SHA-256.

| Arm | Job | `config.ini` | Launch to `InGame` | Crash box | Stop | Restore |
|---|---|---|---|---|---|---|
| stock | `20261114-014620-5d23` (01:46-01:56) | the instance's own | 82.8 s | **none** (only the title "Sid Meier's Civilization V (DX11)" 3 min after `InGame`; no dialog at the stop, 4 min after) | `stop_nolua` rc 0 (61 s) | standard files 0 differ, `verify --quick` rc 0, My Games 21,874 files unchanged |
| D33 | `20261114-015935-9c3d` (01:59-02:08) | product_path's D33: `darkred.py config --arm d33 --on Overlay` at bea36ce3, i.e. `[DisableSystems]` off = OverlayDebug, Decal, Forest, River, FOW, FluidFOW, DistanceFog, Waves, YieldIcons, Arrows, MovementPath, Particle, Combat, Projectile, Minimap, ColorKeyEdit; on = Terrain, LandmarkSystem, City, Unit, LeaderHead, Audio, StrategicView, Overlay | 83.5 s | **"Game Crash" at 02:04:56** (launch 02:03:34): write to `0x0000000c` at `CivilizationV_DX11.exe+0x004cc892`, the same stack (`+0x6365a, +0x2570b0, +0x339af0, +0x2570e8, +0x3495a1, +0x4dbc6a`), Sub2G committed 1,532,876 KB, free 285,752 KB, largest free 138,544 KB; minidump `CvMiniDump_20261114_020456_5.4.8-0-g3dc5bea6e_Release.dmp` in tb2's `crashlogs` | refused (dialog, exit 8): left for the MC | after the MC's dismiss (#461, job `20261114-020915-4c72`), `release_tb_repair.py --phase control-restore` (job `20261114-021009-c8b3`): standard files 14 with 0 differ, `verify --quick` rc 0, My Games 21,874 with 0 differ |

**Result [L, two runs]: the crash comes from the D33 `config.ini` under a 32-bit DLL.** Neither the proxy, the pack,
the fallback route, the composite nor `ServerRenderer` is needed for it. The same EXE, DLL and save run cleanly with the
stock `config.ini`. With the D33 `[DisableSystems]` they crash at the moment the game enters `InGame`. With the 64-bit
server the same `config.ini` runs (the product path, 0156/3600). So the world systems that D33 turns off are drawn by
the server there, and the stock DLL path in the EXE breaks without them. Which system it is was not bisected. The c5h-c
crash (23:42:55, the pin's 32-bit `-ia` DLL, 3600's D33-type settings) fits the same cause [I]. **Product consequence: a
fallback boot leaves the player's `config.ini` with D33 in place, so every fallback into a game crashes. This is a
release blocker for the fallback (MC #430).** `config.ini` is read by the EXE at its start, before the DLL is loaded, so
the proxy cannot undo it at `DllGetGameContext`. A fix has to keep the D33 switches out of `config.ini` when the 32-bit
DLL runs, or apply them only once the 64-bit boot has succeeded. This is a design decision for the lead and the MC.
(Note from 3600, #449: once 3600 merges, Waves leaves D33_OFF. This run had Waves off.)


**The fallback itself works on the shipped path** [L, oldpc job `20261114-005346-f5f8`, the candidate layout
`x64-99fb2c1c-p0ea6fd7` of 3502's run, a `pack.py` pack of it]: `boot: FALLBACK to the 32-bit DLL ...
CvGameCore_Expansion2_tag.dll: the 64-bit boot failed: boot FAILED: server_exe ...VP64\server.exe: no such file`, the
notice line, and the window title "Sid Meier's Civilization V (DX11) - VP64 64-bit patch OFF: the 64-bit server did not
start"; Lua.log reached InGame 83 s after the launch. **No acceptance picture**: the whole-screen grab showed the host's
desktop with a Windows Firewall alert for the instance's game EXE and no game window (the harness's offscreen mode moves
the window off screen); the job's own check had passed it, so its entry was corrected to fail, DIAGNOSTIC (the check
must also see the game window on screen). The job also ran on oldpc's tb2 where tb1 was granted (it took the host's
first instance): the next jobs name the instance.

**The Windows Firewall alert on oldpc** (the owner pressed "Allow access", MC #418): it named "Sid Meier's Civilization V",
publisher Firaxis Games, path `E:\c5-testbed\instances\tb2\install\civilizationv_dx11.exe` (the screenshot of 01:02).
It is the game EXE's own networking from a new path, not the release's files: no source under `server/`, `ipc/`,
`proxy/` or `shell/` opens a socket (no `socket(`, `WSAStartup`, `bind(`, `listen(` or `ws2_32` in any `*.cpp`/`*.h`
[C, grep 2026-11-14]); server and proxy talk through shared memory.

**The rules on oldpc [L, job `20261114-011135-9440`, 2026-11-14 01:30:49, read-only `netsh advfirewall firewall show rule
name=all verbose`, 31,181 lines; nothing changed; read by 3514]:** four inbound rules named "Sid Meier's Civilization V"
for `E:\c5-testbed\instances\tb2\install\civilizationv_dx11.exe` (rule source Local Setting): **Allow** TCP and UDP on
the **Private** profile (any port, edge traversal "Defer to user") and **Block** TCP and UDP on the **Public** profile.
That is the set Windows writes when "Allow access" is pressed with only Private networks ticked. tb1's EXE
(`E:\c5-testbed\instances\tb1\install\civilizationv_dx11.exe`) has the same four rules, from an earlier prompt [I].
netsh does not record when a rule was made, so the time comes from the owner's report (#418, about 01:02-01:10). No
rule names `server.exe` or any other file of the release. Rules for oldpc's own installs also exist: `pure_vp`
`civilizationv.exe`, the SDK's `civilizationv_server.exe` and the Steam `LaunchPad.exe`.

### W5.8 The instance stage with the fixed restore, and the report stage live (3514 item 4) [L, 2026-11-14 01:50-02:16]

Work folder `C:/c5-64bit-work/3500/3514/rel548` (C:, D111). It was seeded with the pins as in W5.4 (`build` and the `pack`
marker), and 3506's live `wholegames` entry (W5.6, parity passed; the same seeded builds) was imported into its
`state.json` with an `imported_from` field, because the whole games take 34-95 min of testbed time. Driver `--fork
D:/c5-64bit-work/9045/fork`: the candidate run's recorded clone `D:/c5-64bit-work/3500/w1b/fork` was gone (02:00).

- `refsave` (auto): ok, origin descriptor (P10 of `E2000-HUGE_0350` in 27 s).
- **`instance` on c5h-a (`C5_RELEASE_INSTANCE=c5h-a`), 01:55:00-02:15:26: ok, the restore end to end.** The steps were
  claim, `set-dll` from `e589306e...` (i0ship-bbfacca1-ia) to the pin's `-ia` 3c2f47da... (5 s), the test pack (1 s),
  `set-proxy` (7 s), and two arms. Arm tiny: 413 s, **3,559,716** answers verified, 0 mismatches, 0 lock errors, no
  FALLBACK line. Arm huge: 760 s, **1,475,881** verified, 0 mismatches, 0 lock errors, no FALLBACK line. The restore ran
  in reverse order and every step went: front end (0 s), the two save copies and the two VP64 files removed,
  `set-proxy --original --runs-to <new folder>` (6 s), `set-dll` back to `e589306e...` (4 s), `verify --quick` rc 0
  (6 s). The claim was released. This is the third live run, and the first with no hand repair (runs 1 and 2: W5.4a,
  W5.6). The verified counts differ from run to run (tiny 2.20 M, 1.95 M, 3.56 M; huge 0.99 M, 1.69 M, 1.48 M): the
  arm plays 10 AI turns plus a checklist on a loaded machine, and the count follows how many frames ran, so it is no
  measure of anything.
- **`report`, 02:15:37: ran live and stopped as designed.** It wrote `validated-report.md` and `.json` with refsave,
  instance and wholegames ok, then stopped (exit 10) with the verdict "candidate, validated tier not reached (survey
  pending)", because the seeded folder never ran the candidate stages. A `validated` verdict needs the candidate stages
  and these four in one folder, i.e. the validated tier run in the candidate run's own folder
  (`C:/c5-64bit-work/3500/releases/Release-5.4.8`, which 3502/the lead drive). Not done here, so as not to write into
  another worker's state.

### W5.9 The fallback picture with the stock `config.ini`: a DIAGNOSTIC standing in for 3516 (3514 item 2) [L, oldpc tb2, 2026-11-14 02:14-02:24]

MC #471: the shipped layout writes the product path's D33 `config.ini`, and under it the fallback crashes (W5.7, blocker
3516). So the picture with the stock `config.ini` is a DIAGNOSTIC. It stands in for 3516's fix, and the ACCEPTANCE
fallback picture waits for that fix in the shipped layout. Job `20261114-021441-2a4c`: `release_fallback.py --fault server
--config stock --stop-nolua --save TB-TINY_0008 --save-file --instance tb2 --mode offscreen --park none --continue-key
ENTER` at b40aebf0, with 3506's files of job f5f8: the candidate layout `x64-99fb2c1c-p0ea6fd7` (proxy 3a227db5 with the
pack boot, server.exe 63d54cff renamed away), 3506's pack (manifest 56b38ec2), 0156's composite with ServerRenderer = 1,
`pack=1`, and `fallback_dll` = the tag DLL 262aee88.

- The proxy boot: `boot: FALLBACK ... server_exe ...\VP64\server.exe: no such file` 0.18 s into the boot, the notice 12
  ms later, no server child. `InGame` 83.2 s after the launch. After 180 s `cli.py key ENTER` (a posted message) closed
  the load screen through its own input handler (VPUI and UI_bc1 `LoadScreen.lua`: Enter or Esc once the load is
  complete). 30 s later the whole screen was grabbed.
- The window is visible with `--park none` (D63): rect 0,0-1920,1080 on a 1920x1080 screen, title "Sid Meier's
  Civilization V (DX11) - VP64 64-bit patch OFF: the 64-bit server did not start". The new checks: the window is on
  screen and there is no "Game Crash" box. Both passed. No crash in about 5 min in game (stock `config.ini`).
- The picture, Visual Check `vc-20261114-022300-oldpc-85ae` (PASS): the map at turn 9 (Honolulu) with the full interface
  and the title notice. No popup is over the map (D114, checked by eye: Choose Production and the unit panel are the
  always-on interface). The before-key grab shows the load screen. The PrintWindow shot `vc-20261114-022302-oldpc-4a82`
  is info only.
- Stop `stop_nolua.py` rc 0 (61 s, granted once by #471). Restore: `set-proxy --original` rc 0, the front end rc 0, 15
  standard files with 0 differ, `provision.py verify` rc 0, My Games 21,874 files unchanged.

### W5.10 Done C's idle-frame crossings with the derived purity file (3514 item 5) [L, c5h-b, 2026-11-14 02:31-02:43]

Results and method are in `research/03-dll-engine-boundary/proxy-design.md` section 16.9: **44.0** crossings per idle
Tiny frame with the derived file and **42.8** with the pin's compiled lists, in the same conditions, with the same mix
method by method. The derived file is equivalent to the compiled lists. Both are above 2026-11-11's 31-34, because of
the mirror misses at a lower frame rate and the plot reads [I], so the range is not met as written. One build note: a
`build_server.py` of the pin's source from a plain copy (`git archive`, the fingerprint equal to `deps/dll.lock`) gives
an x64 DLL whose version string ends "Copy" (`Release-5.4.8-51-g04b1211b8 Copy`), so its bytes (c6660200...) are not the
pin's 74588135.... The arm therefore used the pin's own x64 DLL with this branch's `server.exe` and proxy, and the
purity file was derived for 74588135.

### W5.11 The reference game with every victory off (ticket 0175) [C, L 2026-11-14]

> **MC note, 2026-11-15 00:31:** the direct-mode save gaps 0175 met are fixed on main: the toolkit's VP Modpack DLC is no longer listed by default (ticket 0197, `run_server.py` dlc-exclude default) and the save header lists the mod set (ticket 0303). A run from main does not need 0175's work-arounds for them.

Ticket `tracker/issues/0175-validated-tier-unattended-run.md`, branch `task-0175-validated-tier`. Why: W5.2's game (every
victory on) ends in a Maya space-race victory at turn 350 on the D96 pins, so its save cannot play on.

- **A spec's `"victories"` list cannot turn victories off on the pins [L, 2026-11-14 12:5x, server
  `x64-04b1211b-pa803776`].** `run_server.py` writes the list as `victories=<ids>` into `server.cfg`, and the server
  (`server/src/srv_main.cpp` `PhasePregame`) calls `setVictory(id, true)` for each listed id only. The DLL's
  `CvPreGame::resetGame` has already set every type valid (`s_victories.assign(s_numVictoryInfos, true)`, fork
  04b1211b8 `CvPreGame.cpp:1945`) [C]. So `"victories": []` gives `victories=` and still all five types valid: a 2-turn
  run of the huge spec with `"victories": []` printed `VICTORY_TIME`..`VICTORY_DIPLOMATIC` `valid true` from
  `Game.IsVictoryValid`. A server that should honour the list must also set the unlisted types false: a server change,
  not made here (it would move the server off the pin; reported to the MC with the hand-back).
- **What works on the pins: a Lua probe [L, 2026-11-14 12:5x].** `Game.SetVictoryValid(id, false)` (DLL
  `CvLuaGame::lSetVictoryValid` -> `CvGame::setVictoryValid` -> `CvPreGame::setVictory`) in a `run_server.py
  --lua-probe` file runs after the game is set up and before the first turn (`PhaseArm`). The pregame's victory list is
  written into the save (`CvPreGame.cpp:3662`), so a loaded save keeps it. Check: new huge game 0 -> 2 with
  `tools/release/specs/refsave-victories-off.lua` printed `refsave victories off: 5 of 5`, saved, then `--load` of that
  save 2 -> 4 with `refsave-victories-report.lua` printed `refsave victories valid: 0 of 5`; both `result: passed`,
  Lua errors 0 (scripts `C:/c5-64bit-work/0175/scripts/sanity2.sh`, deleted at hand-back).
- **The stage now** (`tools/release/stages/refsave.py`): the new game is the same spec
  (`tools/i0/specs/continents-huge-fixed-seed.json`, seeds 1001/2002) plus `--lua-probe
  tools/release/specs/refsave-victories-off.lua`; the game fails unless its `Logs/Lua.log` says `off: N of N` (N > 0),
  also for an adopted run (`C5_RELEASE_REFSAVE_RUN`: W5.2's run can no longer be adopted); the two-turn check of the new
  save runs `refsave-victories-report.lua` and fails unless it says `valid: 0 of N`. The old save's P10 check is
  unchanged (no probe). Both probe files are in the stage's inputs. Tests: `test_validated_tier.py` `RefSave` 11 pass
  (new: the probes are in the commands, a silent off-probe fails, a save with a valid victory fails, an adopted run
  without the off line fails).
- **First attempt, stopped for 0158's window [L, 2026-11-14 13:03:59-13:41:11 EST]**: `C5_RELEASE_REFSAVE=new
  take_release.py Release-5.4.8 --tier validated --fork C:/c5-64bit-work/0175/fork --work C:/c5-64bit-work/0175/rel548
  --scratch D:/c5-64bit-work/0175/scratch --who 0175 --gate-wait 60` (with the -ia build: the instance stage needs
  `i0ia_dll`, so `--skip-ia` is not possible in the validated tier). survey .. pack all ok: branches 20 s, testdata
  game copy 74 s / snapshot 8 s (exit 1, the accepted WB_Script failures) / dbbuild 7 s, build x64 721 s, i0 390 s,
  i0ia 339 s, census 364 s, model 152 s, purity 9 s, pack 1 s, offline `rt` 58 s. The MC-B gave ticket 0158 the
  workstation 14:05-15:00 for its capacity series (no new game-instance or headless work), so a STOP file at 13:40
  stopped the driver before the next offline command (exit 10). Not resumed: the acceptance is one invocation from an
  empty work folder, as W1b.7's, so the run below starts again in a new folder.
- Paused 14:06 for the owner's reboot (MC #722); resumed about 15:00 under D133 (D: storage only).
- **D133 changes for the run [C, tests; L for F:] (2026-11-14 15:0x)**: (a) the scratch goes to F:
  (`F:/c5-64bit-work/0175/scratch-run`). Defender and the offline test loader on F: [L, 15:1x]: `test_proxy_rt.py
  --build <the first attempt's x64 build> --scratch F:/c5-64bit-work/0175/probe-rt` 79/79 in 61 s, so F: does not get
  W1b.6's quarantine (one run; Defender's cloud verdicts can change). (b) The instance stage: `C5_RELEASE_INSTANCE` is
  a name or a comma list claimed with `--name` in order, else the pool's choice, and when none is free the stage waits
  up to `C5_RELEASE_INSTANCE_WAIT` minutes (90; retry every 120 s, the stop file checked) before its stop, so another
  ticket's short use of an instance does not stop an unattended run. (For about 30 minutes, 15:05-15:40, the default was
  the list `c5h-a,c5h-b,c5h-c`, because the pool still offered D: instances; 0180, merged 15:3x, retired those and
  rebuilt c5h-d..i on F:, so the pool's choice is safe again.) Tests `Instance` 12 pass (new: a list with its first
  name busy takes the second; the default claims without `--name`; two rounds then the stop). (c) Still read from D: (storage): the fork clone's objects
  (`--shared` clone of `D:/c5-64bit-work/9045/fork`) and the instance stage's Tiny save
  `D:/c5-64bit-work/3500/inputs/REL548-TINY_0008.Civ5Save`.
- **The unattended run [L, 2026-11-14, started 15:07:23 EST]**: `sh C:/c5-64bit-work/0175/scripts/drive2.sh` =
  `C5_JOB_OWNER=0175 C5_RELEASE_REFSAVE=new python -I tools/release/take_release.py Release-5.4.8 --tier validated
  --fork C:/c5-64bit-work/0175/fork --work C:/c5-64bit-work/0175/run --scratch F:/c5-64bit-work/0175/scratch-run --who
  0175 --gate-wait 60`, from the worktree of `task-0175-validated-tier` at 325bff37, an empty work folder, one
  invocation. During the run the worktree moved to a8838679 and two merges of `main`: of the files the driver loads
  later, only `stages/instance.py` changed (the default back to the pool's choice), before that stage started; the
  merges touched no file under `tools/release/`, `server/` or `tools/x64-dll/` that the run used. Candidate tier, all
  ok (15:07:23-16:19:56, 72 min): branches 37 s, testdata 58 + 6 + 7 s, build x64 738 s (config `release`, narrowing
  794 sites), i0 377 s (`99FE594B...`), i0ia 392 s (`B5555465...`), layout `x64-33ddf979-p325bff3`, census 424 s (9 and
  9 sites), model 152 s, purity 10 s (0 dropped), pack 1 s, offline 66 + 24 + 30 + 61 s (the loader ran on F:), savetest
  1,601 s (rows tiny 30,666, standard 351,496, huge 683,123 = the descriptor's reference counts), drift 41 s (no
  differing turn, 30,666 = 30,666), fixchecks 0096 28 s (the new build's PASS line), 0084 64 s, 0103 89 s, oldsave 53 s.
  refsave: P10 of the descriptor's save 36 s (ok); the new game started 16:20:32 with the probe, whose line in
  `Logs/Lua.log` is `refsave victories off: 5 of 5`.
- **The reference game with victories off [L, 2026-11-14 16:20:32-16:59:39 EST]: passed, turns 0 -> 350, Lua errors 0,
  peak working set 404 MB, wall 2,346 s (autoplay 2,326 s, 6.6 s/turn on average with the run folder on C:; W5.2's
  same game on the saturated D: took 10,123 s, 28.5 s/turn: not a timing claim, the machine was shared both times).
  No game over: with every type off nobody can win.** Save `Release-5.4.8-REF-HUGE_0350.Civ5Save`, 2,548,527 B,
  SHA-256 `23710a5c68b28f86732af502c6f6840bdb7a3738fd76922d004f1d7303750228` (W5.2's all-victories save was
  2,850,162 B: the two games part early, the probe changes the AI's grand strategies [I]). **It loads and plays on**:
  `run_server.py --load` 350 -> 352, `result: passed`, Lua errors 0, 40 s, and the loaded game holds `refsave
  victories valid: 0 of 5`. Stage `refsave` ok at 17:00:19, origin `new`, `descriptor_update` reported for 3501 (the
  descriptor is not edited). The instance stage then waited on its hold file for 0158's levels until 18:00.
- **The instance stage: tiny passed, huge refused by the real game's front end; the run ended `FAILED instance` [L,
  2026-11-14 18:00:20-18:28:49, c5h-a].** Claim c5h-a, set-dll to the run's -ia DLL `B5555465...`, test pack, set-proxy
  (each 1-2 s). Arm tiny 262 s: **178,841 answers verified, 0 mismatches, 0 lock errors, no FALLBACK**, and the first
  live `purity: loaded (pack=1)` line with this branch's proxy (114 method keys, 29 Lua names, bound to the run's x64
  DLL `7ae3e89a...`). Arm huge: the front end's autoload refused the new save, `Lua.log`: `REFUSING to load,
  CanLoadSavedGame = 2 (the save needs DLC that is not installed)`; `wait_game.py` timed out at 900 s, then `cli.py stop`
  refused ("Lua channel silent (front end ...); database journal present", rc 6, the proxy's server child holds
  `stats.db`) and `arm.py` exited 3. The restore removed the two save copies and the two pack files, then front end,
  `set-proxy --original` and `set-dll` back refused because the game still ran; the claim was KEPT, as designed. The
  game sat at the main menu with no game loaded; its stop needs the MC's word (asked on the 0175 thread).
- **Why the save is refused [C, header bytes, 2026-11-14 18:3x]**: the server writes a new game's DLC list from its
  game folder by the SteamApp rule minus `save_dlc_exclude` (`server-host.md`, "New-game DLC rule"); the descriptor's
  game folder is the headless toolkit's, whose `Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS/MPModsPack.Civ5Pkg` ("VP Modpack",
  GUID `{b5932ae4-0f4f-498f-9333-e2d31b20e095}`, `<SteamApp>235580`) is listed. The new save's header holds that GUID's
  16 bytes at offset 685 and the text "VP Modpack" at 709; the descriptor's 32-bit-written save holds neither. A test
  instance (and a player's game with Vox Populi as mods) has no such DLC, so `CanLoadSavedGame` returns 2. W5.2's
  all-victories save had the same list (written the same way) but never reached the instance stage. **Fix
  (`refsave.py`)**: the new game passes `--dlc-exclude` = DLC_SP_Maps plus the GUID of every
  `<game_dir>/Assets/DLC/VP_MODPACK*/*.Civ5Pkg`, and the new game fails if its save's first 8 KB still hold one of those
  GUIDs (also for an adopted run). Tests `RefSave` 12 pass. Live check [L, 18:3x]: a 2-turn new huge game with that
  `--dlc-exclude` writes a save whose first 8 KB hold neither the GUID nor "VP Modpack".
- **Can the product's saves carry it? No [C, grep of `server/src` 2026-11-14 19:2x; I for the EXE side].** The server
  writes a `.Civ5Save` header only in `srv_main.cpp` `PhaseSave` (direct mode: `run_server.py --save-out` /
  `--save-at-start`); `SrvSaveRaw` (`srv_save.cpp:303`) has no caller, and nothing in `server/src/remote/` writes a
  save. In the product (remote mode) the EXE writes the save file, header and DLC list from the player's own install
  [I, the product-path design]; a loaded game's header is carried over (`s_loadedHeader`). So only saves made by the
  headless tools from a server new game list the toolkit's modpack: any tool that hands such a save to the real game
  needs the same `--dlc-exclude` (or `run_server.py`'s default could add the modpack GUID: a server-tools change, not
  made here; told to the MC).
- **c5h-a put back [L, 2026-11-14 19:1x-19:2x]**: on the MC's word (#975, relayed by the dispatcher) `cli.py -i c5h-a stop
  --coordinator-override "<MC #938 text>"` (leftovers: clean), then the stage's restore by hand
  (`C:/c5-64bit-work/0175/scripts/repair_c5ha.sh`): `frontend.py restore` ok, `set-proxy --original --runs-to <new>`
  ok (the proxy layout removed, the -ia DLL `b5555465...` swapped back), `set-dll` to the kept DLL, `verify --quick`
  ok (no misses), DLL `e589306e...` again, claim released.
- **Second fresh run [L, 2026-11-14 18:36:33-20:16:37, 3aa50d61, work `C:/c5-64bit-work/0175/run2`, scratch
  `F:/c5-64bit-work/0175/scratch-run2`]: candidate all ok again (x64 795 s, i0 275 s, savetest rows = the reference
  counts), the reference game passed 0 -> 350 in 2,083 s with `--dlc-exclude` DLC_SP_Maps + VP Modpack, save
  `edc4bf2b...`, 2,548,362 B, its DLC list now the descriptor save's 13 entries; loads 350 -> 352 with 0 of 5 victories
  valid. Instance on c5h-b: tiny ok (258 s); huge: the front end accepted the save and began the load, then the EXE
  showed "Microsoft Visual C++ Runtime Library: Runtime Error! ... terminate it in an unusual way" [OK] 106 s after
  the launch (`arm.py` exit 8, a dialog: nothing restored, claim kept, D29). `FAILED instance` at 20:16:37.**
  The two reference games' pregames are byte-identical (`07784d4f...`); their bodies differ (`0ef93375...` vs
  `6d3ece51...`, two builds of the same source in two folders); not investigated [I: the build differs, or the game
  is not deterministic over 350 turns between two builds].
- **Why the EXE aborted [C, header + proxy log]**: the server writes a new game's header with an **empty mod list**
  (`srv_save.cpp`: "a new game: the DLC rule below, no mods"); the descriptor's save lists `(1) Community Patch` v151
  and `(2) Vox Populi` v17 (`server/tools/civ5save.py info`: otherwise the headers differ only in the game's own
  fields). The EXE, loading a save whose mod list is empty while CP and VP are active, turns the mods off for it: the
  proxy log shows the DLL released (`SetDLLIFace(NULL)`, an orderly `kRt_Shutdown` 30 s after the boot) and no second
  proxy boot, so the game then loads a Vox Populi save without Vox Populi and aborts [I for the last step]. **Fix
  (`refsave.py` `with_release_mods`)**: the new save is unpacked with the build's `civ5save.exe`, its header's mod list
  set to the descriptor's mods (`[guid, version, folder]`), and packed again (byte-exact zlib 1.2.3: an unpack + pack of
  the unmodified save gives the same bytes [L]); pregame and body unchanged (`civ5save.py compare`: identical); the
  stage fails if the repacked header does not list them. Tests `RefSave` 15 pass. **`run_server.py`'s direct-mode
  saves have the same two gaps** [C]: every new game is written with an empty mod list and, by the default
  `--dlc-exclude`, with the toolkit's VP Modpack DLC: such a save does not load in the real game with Vox Populi as
  mods. Loaded games keep their header (`s_loadedHeader`). Product saves are written by the EXE and are not affected.
  Told to the MC for a ticket of its own.
  (Added 2026-11-18 by the MC from ticket 0541: the last step, the EXE turning the mods off, has a direct sign: the harness's `lua_channel` read at the abort (20:13:16) found only the base game's DLL mapped, not the proxy; `crash-ledger-audit.md` 8.1.)
- **c5h-b put back [L, 20:2x]** on the MC's word (#1067): `cli.py dismiss --button OK` (it also refused without an
  override, "Lua channel silent"; given the MC's #1066 text), the EXE ended with the box (leftovers clean), then the
  stage's undo list by hand (`scripts/repair_c5hb.sh`): the two pack files and the two save copies removed, front end
  restored, `set-proxy --original`, `set-dll` back to `e589306e...`, `verify --quick` (no misses), claim released.
- **Probe before the third run [L, 2026-11-14 20:27-20:33, c5h-b, claim `0175-probe`]**: the instance stage's steps by
  hand (`scripts/probe_load.sh`: set-dll to run2's -ia DLL, set-proxy from run2's layout without a pack, the
  header-fixed save as `REL-0175-PROBE-HUGE`, `arm.py --mode on --local 1 --verify 1 --turns 2`): the front end loaded
  it, the game reached InGame and played 2 AI turns, `arm.py` rc 0, leftovers clean; restored by hash
  (`scripts/probe_restore.sh`, verify --quick no misses, DLL `e589306e...`), released. (Verified answers 0 there: no
  pack, so no purity list; the stage's arms use the pack.)
- **The third fresh run: the one unattended run of the acceptance [L, 2026-11-14, started 20:40:43 EST at 878ba353,
  `scripts/drive4.sh`, work `C:/c5-64bit-work/0175/run3`, scratch `F:/c5-64bit-work/0175/scratch-run3`, one
  invocation, nothing touched by hand].** Candidate tier all ok 20:40:43-21:52:34: branches 14 s, testdata 9 + 6 + 6
  s, build x64 581 s (layout `x64-430cb3a4-p878ba35`, narrowing 794), i0 304 s (`D0F69AB9...`), i0ia 325 s
  (`811B332A...`), census 347 s, model 1,037 s (the machine was loaded), purity 7 s, pack 0 s, offline 32 + 16 + 25 +
  47 s, savetest 1,279 s (rows 30,666 / 351,496 / 683,123 = the reference counts), drift 36 s (no differing turn),
  fixchecks 0096 24 s, 0084 59 s, 0103 67 s, oldsave ok. **refsave ok (21:52:34-22:30:52)**: P10 of the descriptor's
  save 30 s; the new game 0 -> 350 in 2,197 s, victories off 5 of 5, Lua errors 0; header rewritten (civ5save unpack 1 s,
  pack 2 s) to CP v151 / VP v17; save `Release-5.4.8-REF-HUGE_0350.Civ5Save`, 2,548,173 B, SHA-256
  `85ea3a7e5318d48070c29215717b4287e98021fb15213b91479ea549261b2d24`; loads and plays 350 -> 352, 0 of 5 victories
  valid. **instance ok (22:30:52-22:56:37, c5h-a, the pool's choice)**: tiny 266 s, **200,424 answers verified, 0
  mismatches, 0 lock errors, no FALLBACK**; huge (the new reference save, in the real game through the proxy) 1,236 s,
  **456,914 verified, 0 mismatches, 0 lock errors, no FALLBACK**; restore in order, every step exit 0, verify --quick
  ok, claim released. wholegames submitted 22:56:58 (oldpc `20261114-225637-dc67`: Tiny Continents 5051/5253, Standard
  Pangaea 3333/4444; laptop `20261114-225659-d337`: Standard Fractal 5757/6868, Small Pangaea 1819/2021).
  **wholegames ok (22:56:37-00:23:30), parity passed, 4 of 4 whole games x64 = x86, every table identical but the
  known `ReligionBeliefYields.IsReligionOwner`; the testbed database `bd9b8aa1...` = the descriptor's on both hosts:**

  | Host | Game | End (both arms) | Rows | `IsReligionOwner` rows |
  |---|---|---|---|---|
  | oldpc | Tiny Continents 5051/5253 | Cultural, turn 361 (the Inca) | 903,979 | 1,273 |
  | oldpc | Standard Pangaea 3333/4444 | Cultural, turn 370 (Arabia) | 1,622,664 | 2,211 |
  | laptop | Standard Fractal 5757/6868 | Science, turn 414 (the Aztecs) | 2,255,970 | 2,859 |
  | laptop | Small Pangaea 1819/2021 | Science, turn 427 | 1,708,434 | 1,575 |

  The same outcomes and row counts as W5.6 and 9045 on the same seeds. **report ok 00:23:30: verdict `validated`**,
  the driver's line `tier validated reached for Release-5.4.8`, exit 0, one entry in `state.json` `runs` (argv as above,
  20:40:44-00:23:30, 3 h 43 min wall on a shared workstation: not a timing claim). Evidence in the repository:
  `evidence/0175/` (`validated-report.md`, `validated-report.json`, `state.json`, `run3-driver.log`, `drive.sh`). The
  reference save is kept as long-lived data on F: (D141): `F:/c5-64bit-work/0175/reference/Release-5.4.8-REF-HUGE_0350.Civ5Save`
  (SHA-256 `85ea3a7e...2d24`). **The descriptor is not changed** (`descriptor_update` in the refsave outputs, for its
  owner, 3501/the MC): `E2000-HUGE_0350` stays Release-5.4.8's reference save, and it still passes P10 on this run.
- **Done for 0175 [L]**: one unattended validated-tier run for the current pin, one report (verdict `validated`), with a
  reference game that plays with every victory off; its save loads and plays on, headless (350 -> 352) and in the real
  game through the proxy (10 AI turns, 456,914 answers verified, 0 mismatches). What it took beyond the ticket's text:
  the victories via a Lua probe (the spec list cannot do it), and two save-header fixes for server-written new games
  (no toolkit modpack DLC; the release's mods listed). Not verified here: a 32-bit-only (no proxy) load of the new save
  in the real game; whether the two reference games' different bodies (run 1 and run 2, two builds) mean anything.

## W7 (3507): the dry run on upstream master, the 32-bit whole-game proof, the documents

Issue `tracker/issues/3507-dry-run-on-master-and-documents.md` (worker W7, 2026-11-14 00:04-). Scratch
`C:/c5-64bit-work/3500/3507/` (D111; scripts in `scripts/`, deleted at hand-back); the dry run's work folder
`C:/c5-64bit-work/3500/releases/master-ba8aafdc/` (state `state.json`, logs `logs/`).

### W7.1 Setup of the dry run [C, 2026-11-14 00:04-00:28 EST]

- **Fork clone** `C:/c5-64bit-work/3500/3507/fork`: `git clone --shared --no-checkout` of 9045's clone
  `D:/c5-64bit-work/9045/fork` (the objects stay on D: and are read on demand: no D: copy, D110), `origin` set to the
  fork on GitHub, the master port's pushed branches `i0-release-master-ba8aafdc-r2` 8be73b6f5 and
  `x64-release-master-ba8aafdc-r2` 76b3a5f70 fetched (they bring upstream master **ba8aafdca**, 2026-11-13 17:49 -0800,
  `Release-5.4.8-152-gba8aafdca`, "Clear the DLL's Lua finalizers before the DLL is unloaded (#13570)", with them).
- **The release stand-in.** `take_release.py` accepted only a tag matching `Release-x.y.z` that exists in the clone
  (corrected 2026-11-16: issue 3509 added `--base-commit HASH` with a release name, section 3509; was "accepts"), so
  master got a **local lightweight tag `Release-5.4.9` = ba8aafdca in the scratch clone only** (never pushed; the clone is
  deleted at hand-back; there is no real 5.4.9) and a stand-in descriptor (`--descriptor`, kept in the scratch folder):
  the 5.4.8 descriptor's fields with `upstream_commit` = ba8aafdca, `reference_counts` `{}` (a new release records its
  own), a `notes` line saying it is a stand-in. Facts read for it [C]: master's mod versions are still CP v151 / VP v17
  (premise P10); master's committed 32-bit DLL is still **5.4.8's `262aee88...`** (last changed by "5.4.8 Release"
  7307aefdf): upstream commits a new player DLL only with a release, so on master the pack stage binds the manifest to
  5.4.8's DLL, which a player of master would not have [I: a dry-run artefact, not a defect of the pack].
- **Command** (from `scripts/drive.sh`): `python -I tools/release/take_release.py Release-5.4.9 --work
  C:/c5-64bit-work/3500/releases/master-ba8aafdc --fork C:/c5-64bit-work/3500/3507/fork --descriptor
  <scratch>/descriptor/Release-5.4.9.json --who 3507 --gate-wait 60 --jobs 6`, `C5_JOB_OWNER=3507`.

### W7.2 The dry run, stage by stage [L, 2026-11-14]

| # | Stage (CL) | Result | Time | Issue |
|---|---|---|---|---|
| 1 | survey (1) | ok: 152 commits, 140 files in the DLL folders; flags boundary headers (5: `ICvDLLScriptSystem.h`, `CvDllGame.h`, `CvDllNetMessageExt.h`, `FFastVector.h`, `FLua.h`), serialization header `CvSerialize.h`, 14 new and 5 deleted sources (`Exe/`, `!CvLuaApi*.h`, `CvLuaExe`), build list, carry overlap 38 files, engine calls 260 -> 261 (`TryGameCoreLock`), Lua `Method()` 3,481 -> 3,489: the same as the checklist's section 3 read by hand on 16596561f | 94 s | - |
| 2 | branches (3) | **STOP** on the identity row 5201480f1: conflict in `CvGameCoreDLL_Expansion2/SqliteLogger.cpp` (as 3505 found, x64-dll-port 12.2); the stop's unblock says "rerun with `--series <copy>`", which the driver did not have (it has since 2026-11-16, 3509) | 440 s (3505's tool on its own D: clone: 53 s; here every object is read from D: through the shared clone, D: at 100 %) | **3509** (the driver: `--series`, a base that is not a tag) |
| 3 | branches (3), with the release copy | **STOP**: "7 series commit(s) not in the fork clone": the release copy `carry-master-ba8aafdc.tsv` names the five master port commits and the two resolutions by their hashes in 3505's deleted W4 clone; only their picks were pushed | 37 s | **3510** (mapping to the pushed picks in the issue; fixed 2026-11-16: the copy names the picks, `x64-dll-port.md` 12.4; the stop's unblock names this cause since 3509) |
| 4 | branches (3), picks substituted | ok: 23 applied, 19 dropped, 3 skipped (as 12.2); `i0-release-5.4.9-ce4e6aae0` 6038f0746 tree eb8ef8e73 = `i0-release-master-ba8aafdc-r2`'s, `x64-release-5.4.9-ce4e6aae0` 363c337ce tree ffb89e4fd = `x64-release-master-ba8aafdc-r2` 76b3a5f70's (local branches in the scratch clone, not pushed) | 187 s | - |
| 5 | testdata (2) | first **failed** on the team branch's driver (`Ctx.run()` takes no `text=`; fixed on 3502's branch f9c0b50e, which the dry-run copy then overlays: `take_release.py`, `stages/`, `tests/`). Then ok: the tag's CP + VP + `(3a)` EUI files; game folder with `VP_MODPACK_5_4_9` (8,650 entries); `snapshot` exit 1 with only the 18 accepted `WB_Script` failures; `dbbuild` `1b9b01cb...` (post-processed with the tag's committed DLL, which on master is 5.4.8's `262aee88`); against the 5.4.8 pinned database 681 of 829 tables identical, 148 differ; the candidate descriptor **keeps the 5.4.8 database** (same tag as its base) | 348 s (mods 243 s: `git archive` from D: objects; game copy 28 s; snapshot 25 s; dbbuild 18 s) | **3512** (the new release's descriptor: no later stage uses the candidate; fixed 2026-11-16: `--base-release`, section 3512) |
| 6 | lock (5) | ok: candidate `<work>/deps/dll.lock` and `i0.lock`; fingerprint not the pin's, so the x64 build takes a detached worktree with `--unlocked` | 16 s | - |
| 7 | build (6), x64 | **failed** at `build_all.py` step `stubs` (27 s): `closedlib-stubs/FLua/FLuaValue.cpp` and `lua51/FLua_luastate.cpp` include `FLua.h`, which upstream #13489 (dae4c612e) made `#error`. 3505's master builds never built stubs on master (its debug link reused 9045's 5.4.8 stubs) | 34 s | **3511** (fixed on the W7 branch under the MC's grant #400: the three stub files include FLua.h's five sub-headers) |
| 8 | build (6), x64, stubs fixed | **STOP (CL 7)**: the narrowing gate, 799 sites against 798, **10 new, 9 gone**: the same 10 `lua_tointeger` -> `int` Lua-binding sites 3505 triaged benign (x64-dll-port 12.2), before the link. 3502's driver took and released the team's link slot itself | 199 s | none: the stop worked as designed; the unblock (triage, baseline) belongs with the bugfix team's baseline update for the next release. The dry-run copy got the 10 rows added to its own `narrowing-baseline.tsv` (`s12_baseline.py`, the rows `--write-narrowing-baseline` would write) |

| 9 | build (6), x64, baseline rows added | **failed** at `build_all.py` step `smoke` (exit 2) after a good link (full LTO): `CacheGameDatabaseData` 0xC000001D in `CvPreconditionFailedFormat` (upstream's release-build precondition, `BUILTIN_TRAP()`), "no such table: UnitPromotions_TerrainHeal": the smoke ran build_all's hard-wired **5.4.8** database. The same DLL on master's own database (testdata's `1b9b01cb...`) passes every smoke step (CacheGameDatabaseData 2.0 s, 43 playable civilizations, 120 minor) | 600 s | **3513** (closed: 3502's 0fdebbbd, `C5_RELEASE_TEST_DB` = testdata's database) |
| 10 | build (6), x64, smoke on master's database | `build_all` **OK** in 554 s (stubs 27, cvgamedatabase 29, cvlocalization 14, dll 439 with the LTO link 345 s, host 40, smoke 6): **master's x64 release DLL `7d074d9fa4a8eea7...`, 16,531,968 B, 0 unresolved, narrowing 0 new (with the 10 rows) / 9 gone**, link inputs without time stamps (repeatability check OK); `server.exe` built; then `proxy/build_proxy.py` **failed**: `proxy_lua.cpp`, `proxy_luaexec.cpp` use the Lua C API without including `lua.h` (the DLL's `CvLuaSupport.h` included it at 5.4.8, not on master) | 638 s | **3518** |

| 11 | build (6), copy refreshed from the merged branch (3502's driver, 0159's link place, 3511 committed, 3518 patched, the stand-in descriptor in `releases/` with master's database) | x64 **ok** (513 s; `build_all` 460 s, its LTO link 279 s under 0159's machine-wide link place, peak private 9.6 GB; the DLL is again **`7d074d9f...`**: byte-identical to row 10's build in the same folder; server `6937dc71...`, proxy `f00d57dd...`); 32-bit plain **ok** (271 s, plus 3.5 min to make its worktree from the D: objects); then **STOP (CL 3)**: the instruments patch `ia-ee893bc18-440878983.patch` does not apply to the i0 branch: 6 files, among them `Lua/CvLuaSupport.cpp/.h`, which #13489 rewrote | 997 s | **3515** (a port; the candidate tier goes on with `--skip-ia`) |
| 12 | build (6), `--skip-ia` | **ok**, but the stage ran again from its first part: `--skip-ia` is one of its inputs, so the finished x64 and plain parts were not reused (a cost of the parts design). x64 634 s (DLL `7d074d9f...` a third time), 32-bit plain `DBCA1FD3...` (layout `i0ship-6038f074`); the outputs record `x64_config` `release` (MC #226) and `tools_commit` `unknown` (the dry run's copy is a `git archive`, not a checkout, so the layout is named `x64-363c337c-punknown`) | 943 s | - |
| 13 | narrowing (7), census (8) | ok, ok: census on master's x64 source **0 x64-only**, 9 sites on each build (as 3505's debug census of the same tree) | 0 s, 260 s | - |
| 14 | model (10) | **STOP (CL 10)**, as designed: "the model differs". Report: interfaces only moved lines (interfaces 15, 16), `slot-proof.json`, `policy-table.tsv` and the generated runtime **equal**, `CvAStar`'s x64 record lost `m_bHeapDirty`; but **`lua.json` has 0 methods**: `extract_lua.py` counts 3,489 `Method()` tokens and 0 compiled registrations (3,463 at the pin): master's Lua API refactor is not read | 137 s | **3519** (the extractor; `purity` and `pack` depend on `model`, so they wait) |
| 15 | offline (12), `--only` (it needs only `build`) | `test_proxy_rt.py` **79/79** on master's build (63 s); then **failed** at `build_hutest.py`: `proxy/tools/hutest/hutest_host.cpp` has the same missing `lua.h` as the proxy | 73 s | **3518** (extended to `hutest_host.cpp`) |
| 16 | savetest (13), `--only` | run_d10 **FAIL overall, from the huge part only**. **Tiny: parts 1, 2, 3 PASS** (writer identity under the version-string exception only; x86 direct = x64 direct, 16 tables, **35,034 rows**; cross-load gates identical). **Standard: parts 1, 2, 3 PASS** (**350,774 rows**). Huge: master's builds do not load the 5.4.8 reference save: the x64 server throws in `CvGame::Read` (`CvGame.cpp:11047`, a read past the end in `Serialize`); c5h with master's 32-bit DLL dies with 0xC0000005 in the load. The 16th table and the new counts are master's (5.4.8: 15 tables, 30,666 / 351,496). Tiny x64 0-100 took 26 s and Standard 0-200 195 s, beside the testbeds' and other teams' work (not timings) | 572 s | **3517** (the MC, #468: a release cut from master needs its own huge reference save through 3506's `refsave`; 5.4.8 stays the release for increment 2) |
| 17 | drift (14), `--only` | **ok**: Tiny 0-100 x86 = x64, **no differing turn** (33 s each side) | 35 s | - |
| 18 | fixchecks (15), `--only` | 0096: the new build's line `0096-RESULT PASS` (its own `VERDICT FAIL` is the known pin-vs-fix design, W1b.2; the stage reads the new build's line); **0084 VERDICT PASS** (59 s); **0103 new-off = new-on IDENTICAL** (16 tables, 35,801 rows, 43 s); then **failed** at `oldsave` (the huge 5.4.8 save loaded by master's server, exit 2 in 5 s): the same cause as row 16 | 118 s | **3517** (closed on MC #468) |
| 19 | model (10) again, the regenerated model put in the copy's `proxy/model/` (the stop's unblock, "regenerate and commit", done in the scratch copy only; `lua.json` with 0 methods, 3519) | **ok** (`equal`) | 116 s | - |
| 20 | purity (11) | **ok**: `derive_purity.py` on master's source and DLL keeps **99 of 114 method rows and 1 of 29 Lua rows**, drops 15 + 28 (report rows 144 with the header). Every drop is a callee check: 13 Lua rows on `BasicLuaMethod`, 8 on `GetInstance` (master's Lua API refactor), 7 method rows on `size`, 3 on `Destroy`, the rest on `begin`/`end`/`find` and one changed `TContainer.h` citation: the over-dropping of common names that 3503 described (proxy-design 16.9), in the safe direction | 10 s | - |
| 21 | pack (-) | **ok**: `pack/vp64pack/` with the seven files; `manifest.json` format 1, release `Release-5.4.9`, `x86_dlls` = the tag's committed DLL **`262aee88...` (5.4.8's: master commits no new 32-bit DLL)**, x64 DLL `7d074d9f...`, purity `57e679de...`, `built` commits `unknown` (the copy is not a git checkout) | 2 s | - (a pack from master is a test artefact only, `installer-notes.md` 7) |
| 22 | offline (12) again, `hutest_host.cpp` patched | `test_proxy_rt.py` 79/79, `build_hutest.py` **46 checks, 0 failed**, `--scenario all` ok (68 s); the verify leg (`run_proxy_game.py`, Tiny 0-50 through the proxy) **failed** after 26 s: in the 32-bit host the Lua globals `Game`, `Map`, `Players` are nil (6 Lua errors, `SetAIAutoPlay did not arm autoplay`). The x64 server alone runs master's Lua (save test, 0 Lua errors), so it is the proxy's Lua route on master | 136 s | **3519** (extended: the proxy's Lua surface on master) |

Item 4a of 3507 (the release build of the master port, taken over from 3505) is row 10's DLL: built by the driver's
`build` stage from `x64-release-5.4.9-ce4e6aae0` (tree = `x64-release-master-ba8aafdc-r2` 76b3a5f70), full LTO,
`C:/c5-64bit-work/3500/releases/master-ba8aafdc/build/x64-363c337c/deps/dll/` [L, 2026-11-14 01:07-01:16].

The first attempt (00:14) ran under a `timeout 170` of the agent's shell, which killed the driver inside `branches`
(git exit 143, recorded as `FAILED branches`): a driver run is always started in the background (W7's own mistake,
2 min). From stop 2 on, the dry run is driven from a `git archive` of the W7 branch's HEAD
(`C:/c5-64bit-work/3500/3507/repo`, `ARCHIVE_OF.txt` names the commit) whose `tools/release/series/carry.tsv` is
replaced by the release copy `carry-master-ba8aafdc.tsv` (3505's resolved series): the substitution 3509 removes
(done 2026-11-16: `--series FILE`, section 3509).
Later substitutions in that copy, each one a stop's unblock and each listed in `ARCHIVE_OF.txt`: the 7 picks of 3510;
3502's newer `take_release.py`, `stages/` and `tests/` (worktree branch f9c0b50e); the FLua.h fix of 3511 (the same
content as the W7 branch's commit); the 10 narrowing rows of row 8.

### W7.3 How to take a release (the operator's view, as of 2026-11-14)

What a person or agent does, in order, with what the dry run showed at each point:

1. **A fork clone of your own** under `C:/c5-64bit-work/<ticket>/` (D111): `git clone --shared` of an existing clone is
   enough (no copy of the 4.6 GB object store). Every git read then goes to that store: on 2026-11-14 with the store
   on a loaded D:, `branches` took 187-440 s instead of 39-53 s, and `testdata`'s mod archive took 243 s. Fetch the
   release tag, upstream master if needed, and the fork's branches that the series names.
2. **The release descriptor.** Corrected 2026-11-16 (3512): nobody writes it before the run any more. Start with
   `--base-release <previous tag>`; `testdata` writes the candidate `<work>/testdata/<tag>.json` and every later stage
   and tool reads it; after the take it is copied to `tools/release/releases/<tag>.json` by hand (`tools/release/
   README.md` section 1a; section 3512 below). (Replaced: "Today it must exist before the run (issue 3512). Copy the
   previous release's, then set tag, upstream_commit, the mod versions, player_dll_sha256, reference_counts {}".)
3. **The candidate tier, started in the background** (never under a shell timeout: it kills the driver mid-stage):
   `python -I tools/release/take_release.py <tag> --fork <clone> --gate-wait 60 [--work DIR] [--skip-ia]`. The
   default work folder is `C:/c5-64bit-work/3500/releases/<tag>/`. The x64 build stage waits for the machine-wide
   whole-program link place itself (`perf/quietlock/linkplace.py`, through `tools/x64-dll/build.py`); do not hold
   it by hand around the run. *Corrected 2026-11-21 (one-off 9080): the team's link slot `tools/release/linkslot.py`
   is retired since 0159 (see W1b.5); replaced "takes the team's link slot itself (`tools/release/linkslot.py`)".*
4. **At each stop** (exit 10, one line `take_release: STOP <stage> (CL step <n>): <what>; to unblock: <what>`), do
   what the line says and run the same command again. It resumes at the stopped stage; `ok` stages whose inputs did
   not change are not run again. (Added 2026-11-16, 3509: a carry conflict's release copy of the series is given with
   `--series <copy>`, recorded for later runs; a base that is not a tag, such as upstream master at a commit, with
   `--base-commit <hash>` and a release name such as `master-<8 hex>` instead of a local stand-in tag.) The stops met on master, in order: the carry conflicts (resolve by hand, write a
   release copy of the series, push the resolved commits before their clone goes), the closed-library stubs (port
   them to the release's headers), the narrowing gate (triage and baseline), and the instruments patch (W7.2).
5. **A failure** (exit 1, `FAILED <stage>`) is a tool or a compile that broke: read the log the line names
   (`<work>/logs/<stage>-<name>.log`); a compile error of new upstream code is checklist step 4 (port), by hand.
6. **The validated tier** (`--tier validated`, 3506) adds the reference game, whole games on the testbeds and the live
   route once the candidate tier is reached.

### W7.4 Times measured on master, and what the candidate tier costs [L, 2026-11-14 00:14-02:19]

Beside other teams' builds, testbed work and 0160's link series: these are not timing claims (PERFORMANCE.md), only
the size of each step. Machine time per stage on its last good run (the W7.2 table has every run):
survey 94 s; branches 187 s (39-53 s on a clone with local objects); testdata 348 s; lock 16 s; build 943 s (x64
release 634 s with an LTO link of 279-345 s, 32-bit plain 271 s, worktrees about 4 min); narrowing 0 s; census 260 s;
model 116-137 s; purity 10 s; pack 2 s; offline about 4 min (rt 64 s, hutest 37 s, scenarios 68 s, verify 31 s to
its failure); savetest 572 s (Tiny and Standard only); drift 35 s; fixchecks 118 s. **One clean pass of the candidate
tier on a release this size is about 1 h of machine time.** The dry run took 2 h 05 min of wall time from the first
start to the last stage, including the reruns its stops caused (the x64 build ran four times: stubs, smoke database,
proxy, `--skip-ia`). Agent time: about 2.5 h, most of it reading stops and writing issues.

What the stops cost, as a guide for the next release: each stop was found in the first 1-10 minutes of its stage. Each
unblock that was a known kind (series copy, baseline rows, the regenerated model) took minutes. The ones that were
ports (3511 stubs, 3518 Lua includes, 3515 instruments, 3519 Lua extractor) are the real work of a release, as the
checklist's step 4 and step 10 estimate.

### W7.5 The 32-bit whole-game proof of the pins on the testbeds (item 4b, done E of epic 3500)

Question (3505's W4.3, its open part): does the x64 pin's source 04b1211b8 built for 32-bit play the same whole games as
the pinned 32-bit build? Arm A **pin** = `deps/i0.lock` release `i0ship-26234371` (F92881A3...59D5, fork 26234371e);
arm B **src** = 04b1211b8 (= 26234371e + the six x64 commits) built for 32-bit by 3507: `build_vp_clang.py --config
release` in a detached worktree, 2026-11-14 00:12-00:20, **F8465BEF7EF63CD776F6D4F198A97B3AFB9AE68CCD1E39BBBD6EA4FC5EE7E2C7**,
17,143,808 B (3505's build of the same commit was B44729A6..., the same size: the 32-bit DLL is not byte-repeatable).
Job: `tools/testbed/jobs/longgames.py` x86:x86 pairs (`--x86-dll` = A, `--run-bin` = B per run; no new job file), whole
games to the end, AI logging off, the testbed's pinned database, one `--compare` per game (`tools/d10/statscmp.py`, every
table). Script `evidence/3507/scripts/s06_submit_x86pair.sh`. The nine games of bug 0116 (the ones known to be
sensitive), in two jobs:
- `oldpc` job `20261114-002112-3801` (00:21-): huge Pangaea 1717/2828, standard Archipelago 5859/6061, tiny Oval 2121/3131,
  and the sample tiny Fractal 1111/2222;
- `laptop` job `20261114-021520-571e` (02:15-): large SmallContinents 2627/2829, standard Pangaea 6263/6465 EPIC, small
  Archipelago 4141/5151, standard Fractal 5757/6868.

Results so far (per game: the outcome of each arm, then the comparison):

| Game (0116 set unless marked) | Host | pin (A) | src (B) | statscmp A vs B, every table |
|---|---|---|---|---|
| huge Pangaea 1717/2828 | oldpc | Diplomatic 359 (2,331 s) | Diplomatic 359 (2,346 s) | **IDENTICAL**, 16 tables, 2,970,630 rows |
| standard Archipelago 5859/6061 | oldpc | Science 418 (1,132 s) | Science 418 (1,146 s) | **IDENTICAL**, 2,474,168 rows |
| tiny Oval 2121/3131 | oldpc | Cultural 347 (293 s) | Cultural 347 (279 s) | **IDENTICAL**, 602,744 rows |
| tiny Fractal 1111/2222 (sample) | oldpc | Cultural 296 (179 s) | Cultural 296 (179 s) | **IDENTICAL**, 491,344 rows |
| large SmallContinents 2627/2829 | laptop | Diplomatic 357 (1,654 s) | Diplomatic 357 (1,606 s) | **IDENTICAL**, 16 tables, 2,041,019 rows |
| standard Pangaea 6263/6465 EPIC | laptop | Cultural 476 (1,012 s) | Cultural 476 (1,158 s) | **IDENTICAL**, 16 tables, 1,880,475 rows |
| small Archipelago 4141/5151 | laptop | Science 412 (710 s) | Science 412 (665 s) | **IDENTICAL**, 16 tables, 1,572,858 rows |
| standard Fractal 5757/6868 | laptop | Science 414 (1,189 s) | Science 414 (1,190 s) | **IDENTICAL**, 16 tables, 2,255,970 rows |

[L, oldpc 2026-11-14 00:21-02:33, evidence `evidence/3507/x86pair-oldpc/`; laptop 02:15-04:49 (job rc 0, fetched and filled by lead-3500 #2 at 04:50), evidence `evidence/3507/x86pair-laptop/`; both with the 32-bit pin F92881A3 against the source build 04b1211b8-for-x86 F8465BEF]. Every run was a c5h `victory`, rc 0, with no
Lua errors and no lost rows. The four games end as the x64 = x86 games of 9045's batch-2 re-validation did on the same
seeds (upstream-fix-batch-1.md 8f: the same outcomes, turns and row counts). So **on these eight whole games (four per host; the laptop four added 2026-11-14 04:50) the six x64
commits leave the 32-bit DLL's behaviour unchanged**. That extends 3505's Tiny and Standard save-test games (W4.3) to
whole games, and, with W4.3's writer and cross-load checks, closes done E's 32-bit proof for the eight. Done 04:50 by the lead; how it was finished, for the record: to finish the
laptop half (run from a worktree of this branch):
`python -I -X utf8 tools/testbed/tb.py --host laptop wait 20261114-021520-571e --timeout 170` (re-armed), then
`... fetch 20261114-021520-571e --out C:/c5-64bit-work/3500/<who>/laptop-571e` and
`sh research/06-tooling-and-workflows/evidence/3507/scripts/s23_x86pair_evidence.sh <that folder> laptop` (the script's
repository path names the W7 worktree; edit `R=` first). Fill this table's last row.

## 0351: the validated tier for the layout with 0343's .Civ5Map reader (a tools-line change)

Ticket `tracker/issues/0351-civ5map-layout-validated-tier.md`, branch `task-0351-civ5map-validated-tier` (account B).
Why: 0343 put a .Civ5Map reader into this repository's closed-library replacement
(`tools/x64-dll/closedlib-stubs/CvWorldBuilderMap/`), which is linked into the x64 DLL, so the DLL, server and proxy
bytes change at the unchanged DLL pin 04b1211b8: a `deps/dll.lock` `tools` line change. 0343 ran the quick checks on its
layout `F:/c5-64bit-work/0343/x64-04b1211b-p83eba391`; the save test and the whole-game parity (D127) had not run.

**Method.** One unattended run of the driver from an empty work folder, as 0175's (W5 above), with `--carry pins`
(the branches stage is skipped and the pins' own commits are built: x64 04b1211b8, i0 26234371e) and this repository's
tools at main bdbd0bb9 (0343 merged): `C5_JOB_OWNER=0351 python -I tools/release/take_release.py Release-5.4.8 --tier
validated --carry pins --fork C:/c5-64bit-work/0351/fork --work C:/c5-64bit-work/0351/run --scratch
F:/c5-64bit-work/0351/scratch --who 0351 --gate-wait 60` (fork clone `--shared --no-checkout` of
`D:/c5-64bit-work/9045/fork`; `C5_RELEASE_REFSAVE` unset = `auto`, so refsave is P10 of the descriptor's huge save).
Started 2026-11-15 06:10:10 EST. Expected: the candidate tier as 0175's run3 (purity 0 dropped, offline, save test rows
30,666 / 351,496 / 683,123 = the descriptor's reference counts, drift no differing turn, fix checks), then refsave ok,
the two instance arms with 0 mismatches and the four whole games x64 = x86 (identical but the known
`ReligionBeliefYields.IsReligionOwner`), verdict `validated`.

**The candidate tier: all ok [L, 2026-11-15 06:10:10-07:37:44 EST, work `C:/c5-64bit-work/0351/run`].** survey 2 s (no
flag; old = new = 7307aefdf), branches skipped (`--carry pins`), testdata ok, lock ok (fingerprint = the pin's: the x64
source is an archive of 04b1211b8), build: x64 849 s, i0 469 s, i0ia 477 s (the instruments patch picked by base,
9045's). Layout **`x64-04b1211b-pfdee656`** (tools = this branch's fdee6566 = main bdbd0bb9 plus the ticket's first log
line; no tool file differs from bdbd0bb9): `bin/CvGameCore_Expansion2.dll`
**29F58E174B354E02A3566841908E225F7678BB8A9BD1EA0774E1B560442A7B1D** (23,655,424 B; the pin's 74588135... was 23,616,000 B:
the reader adds 39,424 B), `bin/server.exe` **2915D3CEDFA6C6507F1DCA72366266B500B97DF5930A832155222F91B27BFEA7**
(3,285,504 B), `proxy/bin/CvGameCore_Expansion2.dll` **229CC0EBB06D42F15FBA6AC0C3C607D5AFB0E63C0A0E634811B0F96CAAEEBB08**
(1,097,216 B), `CvGameDatabase_x64.dll` 9C37B39E...466E, `CvLocalization_x64.dll` 35863EDE...7D8A, `lua51_x64.dll`
30724D6D...0F1A, `civ5save.exe` 409422EF...5B6C (the full list: `tools/d10/evidence/0351/layout-SHA256.txt`). 0343's own
layout `x64-04b1211b-p83eba391` (DLL 0df58cca) differs from this one by its tools commit, whose time is the build epoch
(`deps/dll.lock` `epoch`) [I]. Narrowing 794 against the baseline 798 (0 new, 4 gone). census 552 s (0 x64-only records),
model 181 s (`EQUAL`, generated runtime equal), **purity 10 s: 143 kept (114 method, 29 Lua), 0 dropped** (D104), pack 0 s,
offline: `test_proxy_rt` **87/87**, hutest **PASS** (47 checks, 0 failed, 0 unserved), `--scenario all` ok, verify leg
(Tiny 0-50 through the proxy) **PASS**. **Save test (run_d10, D24) PASS, 06:58:43-07:32:58, 2,056 s**: tiny 0-100, standard
0-200, huge 350-400, parts 1, 2, 3 pass in each, only the version-string exception; x86 direct = x64 direct **30,666 /
351,496 / 683,123 rows** = the descriptor's reference counts; the x86 arm is this run's i0ship build of 26234371e
(`b2f772e2...`; the 32-bit DLL is not byte-repeatable, W7.5) (report `tools/d10/evidence/0351/d10-report.txt`). drift Tiny
0-100 x86 = x64, no differing turn (50 s). fixchecks: 0096 the new build's `0096-RESULT PASS`, 0084 VERDICT PASS, 0103
new-off = new-on IDENTICAL (15 tables, 34,614 rows), oldsave (the 5.4.8 huge save 350 -> 352) exit 0. refsave `auto`: P10
of the descriptor's huge save ok (51 s), the old save stays the reference. At 07:38 every workstation instance was busy
(c5h-a..g running, h and i claimed by 0166-4107): the instance stage waits up to 90 min (`C5_RELEASE_INSTANCE_WAIT`).

**instance ok [L, 2026-11-15 07:38:35-07:59:59, c5h-g (F:), claimed at the second try, 07:42]**: tiny arm 275 s, **155,243
answers verified**; huge arm (the descriptor's `E2000-HUGE_0350` save, refsave `auto`, in the real game through the proxy)
about 12.5 min, **376,451 verified**; both 0 mismatches, 0 lock errors, no FALLBACK, `purity: loaded (pack=1)` bound to
DLL `29f58e17...` (114 method keys, 29 Lua names); guard logs and start-up watches clean (0 foreground polls, every
window cloaked at create); restore in order, every step exit 0, `verify --quick` rc 0, claim released. No picture is
taken by this stage (D114, `stages/instance.py`): `visualcheck.py list --issue 0351 --result undescribed` lists nothing.
wholegames submitted 07:59:59 (oldpc `20261115-075959-88c4`, laptop `20261115-080021-b50f`).

**A trap of this run: the layout's server.exe and proxy say `dirty=1` [L, C].** Their build line reads `build: repo
fdee65668 dirty=1 dll_pin 04b1211b8 src copy config release built 2026-11-15T06:10:52-05:00` (server.log and vpproxy.log
of the arms; the layout's `README.txt` says `repo_dirty=1`). Cause: at 06:12, while the x64 build stage ran, this file
had an uncommitted edit in the worktree that runs the driver, and `proxy/gen/build_info.py` counts tracked changes
(`git status --porcelain --untracked-files=no`; untracked files, such as `.implementer-worktree`, do not count). The
dirty flag is a string inside server.exe and the proxy, so a clean build of the same commit gives other bytes for those
two files; the x64 DLL does not embed it (its build time is the tools commit's time, `deps/dll.lock` `epoch`).
**Rule for the next run: keep the driver's worktree free of tracked changes until `stage build ok`** (write notes
elsewhere meanwhile, or run the driver from an archive copy as 3507 did, whose commit is then `unknown`).

**The clean rebuild of server.exe and the proxy at fdee6566 [L, 2026-11-15 08:03:47-08:09:18].** In this worktree,
clean (`git status --porcelain --untracked-files=no` empty), detached at fdee6566: `python -I server/build_server.py
--src <the run's source archive> --out C:/c5-64bit-work/0351/clean --config release --offline --deps <the run's
build_all deps>` (113 s; the DLL is the run's, **29F58E17...** again). server.exe
**5B254430F9AAC96CB6EC86B17064AE4C86D6E4C0A9D239B30D1E159D0C8640E5** (3,285,504 B), proxy
**75C40CECCF1E436F95AF84BA232F04CEB99103D11E44C01DC0CDB5B59D2854EC** (1,097,216 B), build line `repo fdee65668 dirty=0 ...
built 2026-11-15T06:10:52-05:00`. Byte comparison with the validated files (same sizes) [C]: server.exe differs in 49
bytes in 15 runs: the dirty string (`1` -> `0`), the PE time stamp and the PDB GUID (`/Brepro` derives both from the
content), and the one-character literals `"0"` and `"1"` of the string pool, which changed places (the dirty value is
one of them; in the validated file `"1"` sits after the build-info strings and `"0"` beside the `render_water` names,
in the clean one the other way round), so 9 RIP-relative references to them in `.text` changed with them; the proxy differs in 16 bytes
in 5 runs: time stamp, PE checksum, debug directory and one `push` of the dirty literal's address; `vp64helper.exe`
(B8AC9A64F86CE89CDEC30B8A219C86ABC904749FEE075F38B0D70325A2F68E0F clean, FEB7A37E... in the run's layout, which does not
list it in its `SHA256.txt`) differs in 20 bytes, time stamp and PDB GUID only. No other code or data differs [C]; so the two builds behave the same [I]. Checked live on the clean files [L, 08:06:45-08:09:18]:
`test_proxy_rt` 87/87, the verify leg (Tiny 0-50 through the proxy, strict) PASS, drift Tiny 0-100 x86 = x64 no differing
turn. (Script `C:/c5-64bit-work/0351/scripts/cleanbuild.sh`, `bytediff.py`: scratch, deleted at hand-back; the
comparison output is in `tools/d10/evidence/0351/clean-vs-validated.txt`.)

**The save test again, on the clean files and on main with `--save-mods none` [L, 2026-11-15 08:42:12-09:06:18, 1,445
s].** Ordered by the MC (#1644) after 0303's note that new-game saves of `run_server.py` list the descriptor's mods; the
MC then found full runs like this one unaffected (#1652, 0303's log 08:39) and kept the first pass. `run_d10.py` from
this branch at 668873a1 (main 36892819 merged, with 598211c5: new games get `--save-mods none`), `--srv-bin` = the clean
layout `F:/c5-64bit-work/0351/x64-04b1211b-pfdee656-clean/bin` (server.exe 5B254430...), the same x86 DLL, huge save and
options as the driver's run: **PASS**, tiny, standard, huge each parts 1, 2, 3, only the version-string exception, rows
30,666 / 351,496 / 683,123 (`tools/d10/evidence/0351/d10-report-clean-main.txt`). So the save test passes on both the
validated and the clean server.exe, with and without the `--save-mods none` change.

**wholegames ok, parity passed; verdict `validated` [L, 2026-11-15 07:59:59-09:27:06 EST].** Jobs oldpc
`20261115-075959-88c4` (done 08:34) and laptop `20261115-080021-b50f` (done 09:27); the testbed database `bd9b8aa1...` = the
descriptor's on both hosts; x64 arm = the layout's `bin/` (DLL 29F58E17..., server 2915D3CE...), x86 arm = the run's
plain i0ship build. 4 of 4 whole games x64 = x86, every table identical but the known
`ReligionBeliefYields.IsReligionOwner` (quiet-testbed.md 14.6):

| Host | Game | End (both arms) | Rows | `IsReligionOwner` rows |
|---|---|---|---|---|
| oldpc | Tiny Continents 5051/5253 | Cultural, turn 361 (the Inca) | 903,979 | 1,273 |
| oldpc | Standard Pangaea 3333/4444 | Cultural, turn 370 (Arabia) | 1,622,664 | 2,211 |
| laptop | Standard Fractal 5757/6868 | Science, turn 414 (the Aztecs) | 2,255,970 | 2,859 |
| laptop | Small Pangaea 1819/2021 | Science, turn 427 (Persia) | 1,708,434 | 1,575 |

The same outcomes, turns and row counts as 0175's run3 on the pin's layout (W5 above): the reader leaves whole games
unchanged, as expected, since no game in this set loads a .Civ5Map [I]. **report ok 09:27:06, verdict `validated`**,
`tier validated reached for Release-5.4.8`, exit 0, one entry in `state.json` `runs`, 3 h 17 min wall (06:10:10-09:27:06)
on a shared workstation (not a timing). Evidence: `evidence/0351/` (`validated-report.md`, `validated-report.json`,
`state.json`, `driver.log`); the save-test reports, the layout's hash list, the comparison of the clean rebuild and the
acceptance summary are in `tools/d10/evidence/0351/`. Kept on F: (D141): `F:/c5-64bit-work/0351/x64-04b1211b-pfdee656`
(the validated layout, dirty=1) and `F:/c5-64bit-work/0351/x64-04b1211b-pfdee656-clean` (proposed: the same DLL and
libraries, server.exe and proxy from the clean build). The `deps/dll.lock` tools-line change was proposed to the MC in
0351's hand-back and **adopted by ticket 0360 on 2026-11-15 with the clean layout** (section 0360 below; was "proposed
..., not applied").

What is not covered: the whole games and the instance arms ran on the dirty=1 server.exe and proxy; the clean files
have the offline checks, the drift pair and the save test (above), and differ only as the byte comparison shows. No
whole game or real-game arm loads a .Civ5Map map: 0343's own live runs (5 AI turns on the Hormigas map) are the reader's
live evidence.

## 0360: the clean layout adopted as the x64 release (deps/dll.lock tools line) [C, L 2026-11-15]

**What changed [C].** `deps/dll.lock` `tools`, `epoch`, `verified` and `release` now name 0351's clean layout (each value
keeps a "Replaced" trace of its old text); `commit` 04b1211b8, `branch`, `describe`, `fingerprint` and `repeat` are
unchanged (the repeat line is ticket 0358's). The new values: tools c5-64bit-patch **fdee65668** (0343's .Civ5Map reader),
epoch 2026-11-15T06:10:52-05:00, release folder **`<pins_dir>/dlls/x64-04b1211b-pfdee656`**: DLL 29F58E17...7B1D
(23,655,424 B), server.exe 5B254430...40E5 (3,285,504 B), proxy 75C40CEC...54EC (1,097,216 B), vp64helper B8AC9A64...8E0F.
The DLL pin did not move, but the x64 DLL's bytes did (the reader lives in the closed-library stubs it links), so
every tool that reads the `release` key (run_d10, run_pair, turnbench, the release driver's pins) now takes these files.
The previous release folder `x64-04b1211b-pa803776` (9045, DLL 74588135) is kept on disk, not retired.

**The copy [C, 09:36].** `F:/c5-64bit-work/0351/x64-04b1211b-pfdee656-clean` copied whole (191 MB, 19 files) to the
release folder; `tools/d10/evidence/0360/checkcopy.py`: SHA256.txt 8 of 8 OK (hash and size), 19 of 19 files equal to
the source, none extra (`checkcopy.txt`).

**Tests with the new lock [L, 09:36-09:44].** `python -m unittest discover -s tools/release/tests -p "test_*.py"`:
211 tests OK (399 s); `python -I -m pytest tools/x64-dll/tests`: 51 passed after one stale count was corrected
(`test_repro.py` counted two `+ repro` uses in `proxy/build_proxy.py`; there are three since 3520 added vp64helper,
2026-11-14 016e107f, so the test failed on main 61f0ca90 independently of the lock; own commit on task-0360);
`tools/d10/tests/test_pin_defaults.py` 4 passed (run_d10's defaults = the lock's `release`). Note: `python -m pytest`
without `-I` fails before collecting in this account's shell: the user-site package `dash` registers a pytest plugin
whose import raises `NotImplementedError("Cannot ")` (comm); `-I` skips the user site.

**Product-path smoke from the new folder [L, 2026-11-15 09:46:13-09:49:53, c5h-c].** Driver
`tools/d10/evidence/0360/smoke_0360.py` (shell/evidence/3308's prep / run / restore as 0307 uses it: ServerRenderer = 1,
the composite and its manifest, D33 with Overlay on, no server_config=), `--bin`/`--proxy` = the release folder, composite
04B9815E... built by `shell/build.cmd` from task-0360 (shell/ and render/ sources equal to fdee65668's: no non-evidence
change since bdbd0bb9), save copy C5H-0360-TINY_0018 of C5H-3000-TINY-T0018. In game at turn 18; 5 AI turns 18 -> 23 at
4.9-6.0 s each, every `end` and `wait` rc 0; lockstep exact in each turn (older 0-20, none 0); EXE private 1,229 -> 1,330
MB, server private 707 -> 723 MB; guard checks 0 / 0 / 0, start-up watch ok; safe stop rc 0; 0 LOUD lines in vpproxy.log,
server.log, render.log and shell_composite.log; server.log ends with the usual `the engine is gone: ... PeerDied` of a
harness stop. Restore: vpproxy.ini back to its recorded hash, set-proxy --original rc 0, backup restored and verified by
hash, save copy removed, `provision.py verify` rc 0. Evidence `tools/d10/evidence/0360/smoke/` (run.json, run.log,
server-report.txt, summary.txt). One DIAGNOSTIC end shot in the feed (vc-20261115-094924-workstation-2566, described;
D114: no picture was needed).

Not verified by 0360: the whole games and real-game arms on the clean server.exe/proxy (0351's note stands); a full DLL
rebuild from a clean checkout of fdee65668.

## 0358: the x64 DLL's release link is not repeatable; what the checks compare instead [C, L 2026-11-15]

Details and evidence: `x64-lto-link-repeatability.md` (ticket 0358).

**The finding [L].** lld-link 19.1.7's whole-program link of the DLL picks one of two code variants of
`CvPlayer::doInstantGreatPersonProgress` by chance, run to run. Relinks of one build's byte-equal inputs, in one folder
with one environment, gave both variants in turn. Per-pass IR dumps put the decision in LTO's dead store elimination
(`DSEPass`), which keeps or drops one dead store; both results are correct code. The environment changes the odds and the folder does not. So
`deps/dll.lock`'s hashes identify the build that was validated, not what a rebuild of the pin gives; its `repeat` entry
says so (replacing 0131's "repeatable across folders"). The driver's `x64_repeat_check` (0131's input check: no stamped
link input) stays useful. It shows that the *inputs* repeat, and that is all that can be shown by bytes.

**What changed in the build [C].** `tools/x64-dll/build.py`:
- links with a fixed minimal environment (`link_env`: `LIB`, `SYSTEMROOT`, `TEMP`, `TMP`, and `PATH` = LLVM + system32);
- passes `@link.rsp` by name;
- records `link_env` in `build-summary.json`.

The caller's session variables no longer reach the linker. This lowers the odds of the second variant (0 of 8 runs
without an extra variable) but does not remove them.

**What the checks compare instead.**
- **Purity (CL 11)** is derived for the DLL actually built: `purity.tsv` carries that DLL's SHA-256, and the proxy loads
  it only for that DLL [C, `proxy/src/proxy_purity.cpp`]. A rebuild gets its own purity file. The derivation reads the
  source and the frozen audit, never the DLL's code, so both variants give the same rows [C,
  `proxy/tools/derive_purity.py`].
- **Parity and the save test** compare game results (x64 against x86, rows, drift), not bytes. Both variants come from
  one optimised IR and differ in register and instruction choice inside one function, so they are expected to play the
  same games [I: no game was run on the second variant].
- **Byte identity of a rebuild.** Compare the link inputs: objects, `link.rsp`, and the stub and import libraries, as
  4252's `cmp22.py` and 0131's `cmp_inputs.py` did. A DLL that then differs from the recorded one only in
  `doInstantGreatPersonProgress` (its funclets, about 19 B of `.rdata` and the `/Brepro` stamp) is the known second
  variant. `research/06-tooling-and-workflows/evidence/0358/funcvariant.py` prints the function's hash:
  - `481C242B...` and `D39349FF...` at epoch 1794587302;
  - other epochs give other values, because `__DATE__`/`__TIME__` move the code.

A difference anywhere else is a real input difference.
- **Handouts (D127) and pins.** A layout is identified by its recorded hashes and validated as built, never "rebuilt to
  the same hash". To reproduce a pinned DLL, copy it from its folder; do not rebuild it.

## W8 (4252, epic 0334): the `--major-civs N` switch [C, the code and hermetic tests; L, real builds 2026-11-15]

Written 2026-11-15 by the 4252 worker (branch `task-4252-43civ-build`). The MC's decisions (thread 0334, #1614): G2, the
43-civilisation DLL is the unchanged pins built with `MOD_GLOBAL_MAX_MAJOR_CIVS=43` on the compiler line, no fork branch; G3,
one switch with a value, default 22, only 22 and 43 validated. Contract rows: `tools/release/README.md` section 2 and
`tools/x64-dll/README.md` "Options". Measurements and hashes: `research/03-dll-engine-boundary/player-limits.md` 8.1.

- **What 43 changes in a run.** `take_release.py <tag> --major-civs 43` (recorded in the state; default work folder
  `<root>/<tag>-m43`; a work folder of one count refuses another, exit 2) builds:
  - x64: `tools/x64-dll/build_all.py --major-civs 43` into `<build>/x64-<c8>-m43/deps` (gated), then
    `server/build_server.py --deps` of it. The server and the proxy compile without the define (G1). build_all fails
    unless the smoke run's DLL logs `Supporting 43 major civilizations` and its version string carries `+m43`.
  - 32-bit plain and `-ia`: git archives (never a worktree), the fork's `build_vp_clang.py` given the define in its
    `RELEASE_PREDEFS`/`DEBUG_PREDEFS` in the copy (`stages/build.py` `inject_x86_define`; the script has no define
    option, and clang-cl 19.1.7 ignores the `CL` and `_CL_` variables: tested 2026-11-15, the define did not arrive),
    version stubs `<describe>+m43 Copy` and `<describe>+ia<n>+m43 Copy`.
  - Names and keys: layouts `x64-<c8>-m43-p<t8>`, `i0ship-<c8>-m43`, `i0ship-<c8>-m43-ia`; candidate-lock keys
    `release-43`, `verified-43` (dll.lock), `release-43`, `instance-43` (i0.lock).
  - pack: the player's DLL is the tag's `(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll` (upstream's installer
    writes it over `(1)` for a 43 install), checked against the new descriptor field `player_dll_43_sha256`
    (Release-5.4.8: `01451bd7...5808`, `git show` of the tag tree, 16,841,728 B); `pack.py --major-civs 43` writes
    `"major_civs": 43` into the manifest. The (3b) modinfo and `AdvancedSetup.lua` are not carried in the pack: a
    43-civ player has them from upstream's installer, and the pack only replaces the DLL in `(1)`. The proxy reads the
    manifest by key, so the extra key is ignored there [C, `proxy/src/proxy_pack.cpp` JMap lookups].
- **Never a silent 22.** Every source's own `CustomModsGlobal.h` line is read before its build
  (`tools/x64-dll/build.py` `header_major_civs`, block and line comments skipped): a live line of another count is
  a stop of CL step 6 in the driver and exit 1 in build.py/build_all.py, before anything compiles; a live line equal
  to the count builds without a second `/D`.
- **The default path did not move.** For 22 every command line, folder name, lock key and stage inputs hash is the
  one before 4252 (tests `test_take_release_stages.TestMajorCivs.test_22_run_calls_and_names_as_before`,
  `tools/x64-dll/tests/test_major_civs.py` compile-line tests); the x64 22 DLL of a real build: player-limits.md 8.1.
- **Tests** (2026-11-15): `tools/x64-dll/tests/test_major_civs.py` 40 passed; `tools/release/tests/test_take_release_major.py`
  14 OK; `test_take_release_stages.py` (with the new `TestMajorCivs`, 7 tests) see player-limits.md 8.1 for the full-suite run.

## 0324: the first playable build is the pinned layout; one validated-tier run as a check of the driver on main [C, L 2026-11-15]

Ticket `tracker/issues/0324-first-playable-build.md`, branch `task-0324-first-playable` (account A).

**What ships.** MC ruling (thread 0324, #1911): the first playable build ships a copy of the pinned layout
`x64-04b1211b-pfdee656` (the bytes 0351 validated as built, section 0358: pins are copied, never rebuilt). Packaging:
the driver's pack and purity tools (`tools/release/pack.py`, `proxy/tools/derive_purity.py`, main c80733b0) were run on
the pinned layout by `evidence/0324/stage_first_playable.py`; the folder, its hashes and the install test are in
`installer-notes.md` section 11. No stage of the driver run below produced a shipped file.

**The check run.** `python -I tools/release/take_release.py Release-5.4.8 --tier validated --carry pins --fork
C:/c5-64bit-work/0324/fork --work C:/c5-64bit-work/0324/run --scratch F:/c5-64bit-work/0324/scratch --who 0324
--gate-wait 60` from a clean detached worktree at main **c80733b0** (no tracked change during the run, so no `dirty=1`
trap of section 0351), fork clone `--shared --no-checkout` of `D:/c5-64bit-work/9045/fork`, script
`C:/c5-64bit-work/0324/drive.sh` (scratch). Started 2026-11-15 15:22:23 EST.

- survey 2 s, branches skipped, testdata 164 s, lock 4 s; build 15:25:13-15:56:25 (x64 906 s, i0 473 s, i0ia 445 s);
  `tools_dirty` false; `repeat_check` ok (181 inputs, 0 stamped); narrowing 794 against 798 (0 new, 4 gone).
- x64 DLL of this build **F3F8D235C6FEA30FDDF8B3A7B28003FAD09A496EA779259EFE4E133D124DB502** (tools c80733b0; the pin's is
  29F58E17 at tools fdee65668: the tools commit's time is the build epoch, so the bytes differ by design, 0358); i0
  41D9D9E4...9A2A, i0ia F8A0C8D3...95DE (the 32-bit DLL is not byte-repeatable, W7.5).
- census 16:04:53, model 16:09:01 (equal), **purity 114 methods + 29 Lua kept, 0 dropped**, pack ok; offline 16:12:35:
  `test_proxy_rt` **106/106**, hutest **PASS** (47 checks, 0 failed, 0 unserved), scenarios ok, verify leg ok.
- **savetest ok 16:36:14** (1,419 s wall): rows **30,666 / 351,496 / 683,123** = the descriptor's reference counts;
  drift Tiny x86 = x64 (30,666 rows each, no differing turn); fixchecks ok 16:39:54; refsave 16:40:21 (P10: the
  descriptor's huge save `c499d3c6...` loaded at 350 and played to 352, 28 s).
- **instance ok 16:55:41** on c5h-d (F:): tiny arm **180,728** answers verified, huge arm **382,886**, both 0 mismatches,
  0 lock errors, 0 FALLBACK lines, `purity: loaded (pack=1)` bound to DLL f3f8d235; build line `repo c80733b04 dirty=0`.
- **wholegames ok 18:22:41**: oldpc job `20261115-165541-e5ec`, laptop job `20261115-165602-6b36`, testbed database
  `bd9b8aa1...` on both; **4 of 4 x64 = x86**, every table identical but the known `ReligionBeliefYields.IsReligionOwner`:
  Tiny Continents 5051/5253 Cultural 361 (903,979 rows), Standard Pangaea 3333/4444 Cultural 370 (1,622,664), Standard
  Fractal 5757/6868 Science 414 (2,255,970), Small Pangaea 1819/2021 Science 427 (1,708,434). The same outcomes, turns
  and row counts as 0175's and 0351's runs.
- **report ok, verdict `validated`**, exit 0, 3 h 00 min wall (15:22:23-18:22:41) on a shared workstation (not a timing).

Layout `C:/c5-64bit-work/0324/run/layout/x64-04b1211b-pc80733b` (scratch, deleted at hand-back; its hash list is kept):

| File | This run (tools c80733b0) | Shipped pin (tools fdee65668) |
|---|---|---|
| `bin/CvGameCore_Expansion2.dll` | `F3F8D235...B502`, 23,655,424 B | `29F58E17...7B1D`, 23,655,424 B |
| `bin/server.exe` | `98DB51A5...FB88`, 3,929,088 B | `5B254430...40E5`, 3,285,504 B |
| `proxy/bin/CvGameCore_Expansion2.dll` | `F7691028...3145`, 1,097,728 B | `75C40CEC...54EC`, 1,097,216 B |
| `proxy/bin/vp64helper.exe` | `B298F145...A017` | `B8AC9A64...8E0F` |
| `bin/CvGameDatabase_x64.dll`, `CvLocalization_x64.dll`, `lua51_x64.dll`, `civ5save.exe` | equal to the pin's | `9C37B39E...`, `35863EDE...`, `30724D6D...`, `409422EF...` |

The three libraries and `civ5save.exe` came out byte-equal at another tools commit; the x64 DLL differs (the epoch, 0358);
server.exe grew by 643,584 B and the proxy by 512 B with main's changes since fdee65668 (server-side renderer and
proxy work, not used by the thin client). The run's pack (`pack/vp64pack`, manifest `f2664745...`, purity `bdb0f9cc...`)
is bound to its own DLL f3f8d235 and is not the shipped one.

**What this says.** The one-command driver on main c80733b0 takes Release-5.4.8 on the pins from an empty work folder
to `validated` with no stop and no hand step, in 3 h; a run from a clean detached worktree carries `dirty=0`. It does
not validate the shipped bytes: those are 0351's (section 0351). Evidence `evidence/0324/driver/` (`driver.log`,
`state.json`, `validated-report.md` and `.json`, `layout-SHA256.txt`; user profile paths scrubbed).

## 0436: D162 in the driver: the HI release journeys gate a take for the owner [C, the code and hermetic tests 2026-11-16]

Ticket `tracker/issues/0436-hi-journeys-gate-builds-for-art.md`, branch `task-0436-hi-journey-gate` (account B).
Correction 2026-11-24 (ticket 0708): the options this section names were renamed `--for-owner`, `--not-for-owner` and
`--owner-check` (the old names below still work as hidden aliases); the summary line now says `for the owner: yes | NO`;
see section 0708.
D162 (the owner, 2026-11-16 15:44) [reworded for privacy]: no build is handed to the owner to play until both HI release journeys (release-basic
and release-large) have passed on that exact build, product and stock. Until then the `hi_journey` stage (4506) reported and did
not gate (`human-input-testing.md` section 8; that statement corrected there 2026-11-16).

**The marker of a build for the owner is a new option, `--for-art`** (not a new tier): `take_release.py <tag> --tier
validated --for-art ...`. It needs `--tier validated` (exit 2 otherwise, naming D162), is recorded in the state's
config and stays on for later runs of the same work folder until `--not-for-art`. Other takes are unchanged: the
stage reports only (`ok` with `verdict: failed`, or `skipped`), and the tier is reached as before.

**What passes D162** (`stages/hi_journey.py` `art_check(entry, build)`, one pure function used by the stage, the
driver, the summary, `--art-check` and the kit):
- the stage `ok`; `skipped` (no free instance, a start save missing or changed, no instance-aware DLL, an instance
  with a proxy layout) is refused: "not played is not passed";
- verdict `passed`; each of release-basic and release-large present, its verdict passed (`passed on rerun (known:
  ...)` after the one known-flake rerun counts, as 4506 defined it), both routes `product` and `stock` played
  (`C5_RELEASE_HI_JOURNEYS` or `C5_RELEASE_HI_ROUTES` subsets are refused);
- the index the journeys came from is the committed `tools/hi/journeys/journeys.json` (outputs `index`,
  `index_committed`; a `C5_RELEASE_HI_INDEX` planted copy never counts);
- the exact build: the stage now records `candidate.sha256` = SHA-256 of `proxy/bin/CvGameCore_Expansion2.dll`,
  `bin/CvGameCore_Expansion2.dll`, `bin/server.exe` (from the build stage's `x64_hashes`, else hashed from the
  files) and the instance-aware 32-bit DLL; they and the x64, i0 and tools commits must equal the build stage's
  outputs now. A result recorded before 0436 has no `sha256` or `index_committed` and is refused.

**Where it acts.**
1. The stage: on a take for the owner an `ok` or `skipped` result that art_check refuses becomes a **stop** (exit 10,
   `take_release: STOP hi_journey (CL step -): D162: a take for the owner and the HI release journeys did not pass on
   this build: <reasons>; to unblock: ... --for-art again`); the outputs keep everything and add `art` and
   `gating: true`; `report.md` says `gates: a take for the owner; D162 ...` and `For the owner (D162): yes | NO`. The tier's
   report verdict becomes `candidate, validated tier not reached (hi_journey stop: D162 ...)`. A stop and not a
   failure: exit 1 stays "a tool failed".
2. The driver: a stage module may define `again(ctx, entry)`; an ok stage with unchanged inputs runs again when it
   returns a reason. hi_journey returns one on a take for the owner whose recorded result would not pass (a report-only
   take that failed or was skipped on the same build), so the journeys are played again; a recorded pass on the same
   build is kept (hours of replays saved). At the tier's end a take for the owner is checked once more (a stop, `reached`
   not written), whatever the stage hook did.
3. The summary: every run of a tier with `hi_journey` prints `take_release: D162 HI release journeys of <tag>: verdict
   <v> (stage <status>); report <path>; for the owner: yes (...) | NO, refused by D162: <reasons>` (plus `(not a take for
   the owner: ...)` on other takes) and records it as `d162` in `state.json`.
4. The hand-out: `take_release.py <tag> --art-check --work <dir> [--build-folder <folder>]` reads the state only (no
   lock, no tool, nothing written): exit 0 yes, 10 no, 2 no state; `--build-folder` requires files there with the
   proxy, x64 DLL and server.exe SHA-256 the journeys were played on, and prints them (`played on <role> <sha>`).
5. The play-first-build kit (`evidence/0324/stage_first_playable.py`, touches extended by the MC, #2836): `stage`
   needs `--release-work <take_release work folder>` and runs the check on the layout before writing anything and on
   the staged folder at the end ("REFUSED by D162" either way); BUILD.txt carries the verdict line. `--not-for-art`
   stages a diagnostic hand-out for anybody but the owner, marked NOT FOR ART in BUILD.txt. `art --folder F
   --release-work W` checks a staged folder (exit 0 or 1).
6. `docs/INSTALL-FIRST-PLAYABLE.md`: a comment for whoever stages or hands out a build (the three steps above, "a build
   whose line does not say passed is not handed to the owner") and a visible line "Release journeys: not run on this build"
   for the 2026-11-15 build, which the owner played under D150 before D162.

**Not done here, not verified.** No journey was played on a build by this ticket (the next candidate's take does it,
ticket scope). The kit's full `stage` with `--release-work` on a passed take was not run (only its refusals and the
`art` check, in the tests). Live `take_release --for-art` on the workstation: not run.

**Tests** (`tools/release/tests/test_hi_journey.py`, class `ForArt`, 2026-11-16): passed / failed / skipped (no
instance, a save missing) / incomplete (a changed save) on a take for the owner and on a report-only take; one journey or
one route only; a planted index; a pass on rerun; a report-only fail then a take for the owner (played again) and a
report-only pass then a take for the owner (not played again, tier reached); the tier-end check with the stage hook
removed; a rebuilt proxy refused; `--art-check` with and without `--build-folder`, no state; `--for-art` with the
candidate tier; the kit's refusals and `art`. Results in the ticket's log.

## 3509: `--series FILE` and `--base-commit HASH` (a base that is not a tag) [C, hermetic tests 2026-11-16]

Issue `tracker/issues/3509-take-release-commit-base-and-series-option.md`, from W7.2 row 2: the dry run on master could
neither name a base that is not a `Release-x.y.z` tag nor follow the branches stop's own unblock ("rerun with --series
<copy>") through the driver; it worked around both with a local lightweight tag `Release-5.4.9` and a `git archive`
copy of the repository whose `carry.tsv` was the release copy (W7.1, W7.2). Both are now options of
`take_release.py` (contract: `tools/release/README.md` section 2, "--series FILE and --base-commit HASH"):

- **`--series FILE`.** Recorded in the state's `config` as `series` (the resolved path) and reused by later runs; a
  missing file is exit 2. `stages/branches.py` already read `config['series']` and hashed the file into its inputs, so
  a changed copy reruns `branches` (new content-addressed branch names) and every stage after it whose inputs change.
  A stop of `tools/release/branches.py` whose unblock says "rerun with --series <copy>" (a conflict row, or the
  conflicting merge of i0 into x64) is passed on as "rerun take_release.py <name> --series <copy> (recorded in the
  state: later runs keep it; the copy's commits must be in the fork clone <fork>)" (`branches.driver_unblock`).
- **`--base-commit HASH`** (7-40 hex, stored lower case). The release's base in the fork is this commit instead of
  the tag; the positional argument is then a release name that need not be a tag (`^[A-Za-z0-9][A-Za-z0-9._-]*$`,
  descriptor.py's tag rule: `master-ba8aafdc`, or a `Release-5.4.9` that is not tagged yet). Without the option the
  name must still match `Release-x.y.z` (exit 2, the message names `--base-commit`). Recorded as `base_commit`. The
  stages read the base as `ctx.ref` (= `base_commit` or the tag): `survey` resolves it and still requires it to be the
  descriptor's `upstream_commit` (its stops name the commit and `--base-commit`; its inputs gain `base_commit` only when
  set, so a tag's inputs hash is unchanged); `testdata` (`Stage(..., ref=)`) archives the mods at it; the pack
  fallbacks of `instance` and `hi_journey` pass it as `pack.py --tag`. Everything after `survey` already used the
  survey's `tag_commit` (branches `--base`, lock, pack's player DLL). Branch names are `<name without Release->-c<8
  hex>` (`5.4.8-c...` for a tag as before; `master-ba8aafdc-c...` for a name). The descriptor is still
  `releases/<name>.json` or `--descriptor FILE` with `tag` = the name (since 2026-11-16 also `--base-release
  <previous tag>`, section 3512, under which `survey` leaves out its descriptor check).

Scope addition (MC #3021, 2026-11-16): 3510's rule "a release copy names only pushed commits" (`x64-dll-port.md` 12.4)
is copied into `tools/release/README.md` section 3, and `branches.py`'s stop "N series commit(s) not in the fork clone"
now names that cause in its unblock (a release copy's commit never pushed before its clone was deleted: name the pushed
pick, or push the commit from a clone that still has it); `test_branches.py` checks the text.

A first full-suite run (2026-11-16 18:2x-18:56, 324 tests) failed 2 tests: the branches stop's rewrite used the fork
path as an `re.sub` replacement string and Windows backslashes (`\U...`) raised "bad escape"; fixed with a function
replacement [C].

The dry run's command would now read: `take_release.py master-ba8aafdc --base-commit ba8aafdca --descriptor
<master-ba8aafdc.json> --series tools/release/series/carry-master-ba8aafdc.tsv --fork <clone> ...`, with no local tag
and no repository copy for the series [I: not run live; 3510's pick substitution in that copy is still needed].

**Tests** (2026-11-16, `python -I -X utf8 -m unittest discover -s tools/release/tests`, under `quietlock.py run`):
`test_take_release.py` (`--series` recorded and reused, default none, missing file; a name with `--base-commit`
recorded and reused; a `Release-x.y.z` name with `--base-commit`; a name without it; a bad name; a bad hash; the bad-tag
message names `--base-commit`), `test_take_release_stages.py` (a full candidate run on a fork with the tag deleted, a
release name and `--base-commit` of 9 hex: survey's `tag_commit` is the full commit, branches get `--base <commit>` and
`--series <copy>`, branch `x64-release-master-test-c<8 hex>`, the pack's player DLL from the commit; survey stops on a
base commit missing in the fork and on one that is not the descriptor's; the branches conflict stop's unblock names
`take_release.py Release-9.9.9 --series <copy>` and is recorded in the state), `test_descriptor_testdata.py`
(`Stage(tag='master-test', ref=<9 hex>)` on a fork without the tag: candidate descriptor `master-test.json` with the
full commit, modpack `VP_MODPACK_MASTER_TEST`). Results in the ticket's log.

## 3512: a new release's descriptor: `--base-release`, the candidate read by every later stage and tool [C, hermetic tests 2026-11-16]

Issue `tracker/issues/3512-new-release-descriptor-flow.md`, from W7.2 row 5 (the dry run on master, 2026-11-14). What
was wrong, from the code [C] and the dry run [L]: `take_release.py` needed `releases/<tag>.json` (or `--descriptor`)
before any stage, while `testdata` is what writes a candidate; with the tag's own descriptor as its base `testdata` kept
the base's `test_db` unless the build reproduced it (on master it did not: `1b9b01cb...` against 5.4.8's `bd9b8aa1...`,
148 of 829 tables differ), so the candidate kept 5.4.8's database; no stage after `testdata` read the candidate
(`offline`, `savetest`, `drift`, `fixchecks` read `ctx.desc`, the descriptor given at the start); and the tools the
driver starts got `C5_RELEASE=<tag>` only when `releases/<tag>.json` existed, so with `--descriptor` alone they failed
or read another release. The flow now (contract: `tools/release/README.md` section 1a):

- **`--base-release TAG`** (`current` = `current.json`'s tag): a new release starts from the previous release's
  descriptor. Without it (and without `--descriptor`) a tag with no `releases/<tag>.json` is exit 2 and the message names
  the flow. Recorded as `base_release`, reused; `--descriptor` and `--base-release` exclude each other, and the one
  given later replaces the recorded one. `survey` does not compare the tag with the base's `upstream_commit` (its report
  says `descriptor_check: not made ...`). Since the base's tag is not the new tag, `testdata` writes the built database
  into the candidate (`test_db_source` "built ..."), the tag's mod versions, commit and game folder, and empty counts.
- **`Driver.desc_for(stage)`**: `ctx.desc` is now a property. Stages up to and including `testdata` read the run's
  descriptor (`ctx.descriptor`, testdata's base); every later stage reads `<work>/testdata/<tag>.json` from testdata's
  outputs, parsed with `expect_tag=<tag>` and read again when its size or time stamp changes. When testdata is ok
  without a candidate (an older state, the stage tests' stand-in) the run's own descriptor is used if it is this tag's;
  on a `--base-release` run a stage after testdata fails with "run it (take_release.py <tag> --from testdata)" instead
  of running on the previous release's descriptor.
- **`C5_RELEASE` may name a file** (`descriptor.env_file`: a value with `/` or `\` or ending in `.json`; `%VAR%`/`$VAR`
  expanded; `load()` without a tag reads that file, `current_tag()` returns its tag). `Ctx.run` (both the named runner
  and testdata's `subprocess.run`-like one) sets `C5_RELEASE` for every tool: the tag when the stage's descriptor is the
  repository's `releases/<tag>.json`, else the descriptor file's absolute path (`take_release.release_env`). An env
  passed by the stage with its own `C5_RELEASE` is kept. No tool needed a change: `run_d10.py`, `run_pair.py`,
  `run_server.py`, `build_all.py`, `test_proxy_rt.py`, `longgames.py` and the rest call `descriptor.load()` without a
  tag. `wholegames` ships a non-repository descriptor to a testbed job as `rel/release/<tag>.json` with
  `C5_RELEASE=%TESTBED_BIN%/rel/release/<tag>.json` (the host's runner passes the environment unexpanded,
  `tools/testbed/remote/runner.py` `job_env`) [C; not run on a testbed].
- `testdata` has `inputs()` now (the base's tag and content hash, the revision), so a changed `--base-release` or
  `--descriptor` writes the candidate again; on an existing work folder this reruns `testdata` once. The candidate also
  carries `player_dll_43_sha256` (the tag's `(3b) 43 Civs Community Patch` DLL from the fork, else the base's for the
  base release itself), so `pack` under `--major-civs 43` keeps its check. Test hooks: config `c5h`, `core_db`.
- A re-take of a release that has its descriptor (`Release-5.4.8`): the stages after testdata now read its candidate
  too (the pinned database, kept unless reproduced, W1a.2; `game_dir` = testdata's copy with `VP_MODPACK_5_4_8`;
  `stock_x86_dll` in that copy). Their inputs hashes change where they name these, so an existing 5.4.8 work folder
  reruns those stages once [I: not run live on 5.4.8 in this issue].
- Not changed (item 5 of the issue): `testdata` still post-processes the database with the tag's committed 32-bit DLL;
  on master that is 5.4.8's (`262aee88`), harmless unless the DLL adds tables or columns [I].
- After the take the candidate is made the release's descriptor by hand (README 1a step 5: copy it, move its files out
  of the busy work folder to F:, fill `reference_counts` from `savetest`, give the release its own reference saves when
  the previous ones do not load, as on master, 3517).

**Tests** (2026-11-16, under `quietlock.py run`): `test_take_release_new_tag.py` (new): a full candidate run of
`Release-10.0.0`, which has no `releases/` file, with `--base-release Release-9.9.9` on the stage tests' fake fork and
tools, with the **real `testdata` stage** on a fake `c5h.cmd` and game folder: candidate with the tag's commit and mod
versions 152/18, `test_db` the built database, empty counts; `build_server`'s `C5_RELEASE_TEST_DB`, `offline`'s
`run_proxy_game --db`, `fixchecks`' `run_server --db` all the built database, the previous release's database named
nowhere; every tool started got `C5_RELEASE` = the candidate's path and `descriptor.load()` there reads it; a rerun runs
nothing, an edited candidate reruns `offline`; `desc_for` before/after testdata, a changed, another-tag and a missing
candidate; `--only offline` without testdata fails naming `--from testdata` (5 tests, 27 s). `test_take_release.py`:
the exit 2 naming `--base-release`, recorded and reused, `current`, an unknown base, the two options exclusive, a later
`--descriptor` replacing a recorded base. `test_descriptor.py`: `C5_RELEASE` naming a file (any name, its own tag; a
named tag ignores it), `%VAR%` expansion, a missing or invalid file, the file/tag rule. Full-suite results in the
ticket's log.

## 0474: the x64 DLL's name in the measuring tools [C, hermetic tests 2026-11-17]

Every tool that hashes or checks the x64 DLL in an instance's `VP64\` finds it by the proxy's chain (2606,
`research/03-dll-engine-boundary/proxy-design.md`): the layout manifest's `x64_dll.file` (`VP64\manifest.json`, a plain
file name), else `CvGameCore_Expansion2_x64.dll` when present, else the old `CvGameCore_Expansion2.dll`. The same small
helper `x64_dll_name(vp64)` is in `tools/hi/session.py` (`build_ids` `dll_sha256`), `tools/harness/framecap.py`
(`game_dlls`), `tools/testbed/jobs/product_path.py` (`files_info` `x64_dll`) and `perf/profilekit/acceptance.py`
(`client_binaries` `dll`), copied rather than imported because each of those runs standalone (a testbed job ships alone;
Python 3.8 on oldpc); `tools/release/stages/hi_journey.py installed_x64_name` was the first. One difference from the
proxy: the tools read the manifest whatever `pack=` says (the proxy reads it only with `pack=1`); a development layout
has no `manifest.json`, so both agree there. Tests: `tools/hi/tests/test_session_x64_name.py`,
`tools/harness/tests/test_framecap_x64_name.py`, `tools/testbed/tests/test_product_path_x64_name.py`,
`perf/tests/test_profilekit_x64_name.py` (old name, new name, manifest naming a third name over a present `_x64`, a
manifest with a path or broken JSON ignored; 18 tests, 0.5 s).

## 0509: i2-rc1, the take of main with pin batch 3 and the increment 2 shell [L, 2026-11-17]

Ticket `tracker/issues/0509-take-i2-rc1.md`, branch `task-0509-take-i2-rc1` (account B). Decision D183: increment 2
is released on its own; i2-rc1 is the release driver's take of main once 0463 (pin batch 3), epic 2600 (installer)
and 0464 (the take lays out the compositor) are merged. Its acceptance is epic 0398's HI phase on the take's folder.

**The run's descriptor.** No repository descriptor names `shell_config` (installer-notes 12.4), and
`releases/Release-5.4.8.json` is not changed by this ticket (MC on thread 0509). The run starts from `--descriptor` =
that file plus `"shell_config": "i2"`, written into the work folder by `evidence/0509/s02_fork_and_descriptor.sh`
(SHA-256 `31b4d7dd18dfbed8cf2f5958129cc563e2b643ea7e5d8f95de515e4de5c0e4b0`). testdata's candidate carries it (0464),
so build lays out the compositor and pack writes `shell_config: i2` into the manifest, which the installer reads.

**The command** (`evidence/0509/s10_take.sh`, run from the worktree root): `take_release.py Release-5.4.8 --tier
validated --carry pins --for-art --descriptor C:/c5-64bit-work/0509/Release-5.4.8-i2.json --fork C:/c5-64bit-work/0509/fork --work
C:/c5-64bit-work/0509/run --scratch F:/c5-64bit-work/0509/scratch --who 0509 --gate-wait 60`, with
`C5_RELEASE_INSTANCE=c5h-c,c5h-d,c5h-e`, `C5_RELEASE_HI_INSTANCE=c5h-d,c5h-c,c5h-e`, `C5_RELEASE_TESTBEDS=local` (whole
games on the workstation; the laptop is epic 0437's). Fork clone `--shared --no-checkout` of the owner's clone with
`i0-ship-batch3` 39f17f754 and `x64-batch3` 4f4daac70 fetched; none of 6f2c2ee3, e29032ad, 90f1c4c0 is an ancestor of
either (s02 checks). Main was merged right before (79f41db5e, which carries 0487's proxy change) and the worktree had no
tracked change until `stage build ok` (0463's lesson).

**Stages** (started 17:00:47 EST, tools 7bf0d5799 = main 79f41db5e + 0509's scripts):
- survey, testdata, lock ok 17:00:47-17:01:45; branches skipped (`--carry pins`).
- **build ok 17:01:45-17:37:52** (x64 release 1,216 s on a busy machine, compositor 91 s, i0 412 s, -ia), `tools_dirty`
  false, repeat check ok (181 inputs, 0 stamped), narrowing 794 (0 new, 4 gone), 0 unresolved. Layout
  `x64-4f4daac7-p7bf0d57`: `bin/CvGameCore_Expansion2.dll` `87B071723D1FCBD1EB86B82427557E41DE0E26BED0EE0775C5238848D9FDBE4C`
  (23,669,760 B), `bin/server.exe` `54E873A2F317861944BF274E9037A4D953AECC1E04DD646D76763622C5C4B3D1`,
  `proxy/bin/CvGameCore_Expansion2.dll` `8D809D0154DAC111D6A4DAF66999B58C3C5E7A3324304EA59DAE8A5BE65F5F30`,
  `proxy/bin/shell_composite.dll` `8053F8C72D3D6ED7D5154B63BEBD223FE2CD719EAB1E93D5311BAAB7F5B7CE1D` with its `.ini`,
  `.sha256` and `MinHook-LICENSE.txt` (the first real i2 layout: 0464's stage live); i0 `577388C4...8CC8`, -ia
  `B71A9F31...E169` (0463's -ia patch picked by base).
- census 552 s, model 214 s (equal to the committed model), purity 166 kept (137 methods, 29 Lua), 0 dropped; **pack ok
  17:52:17**: manifest `shell_config` `i2`, `x64_dll.file` `CvGameCore_Expansion2_x64.dll`; manifest SHA-256
  `3fa8269f5e37d2fc3a773766d507763dfb85e331b2e2fccdf3beb3216327ce09`.
- offline ok 17:52:17-17:54:33: `test_proxy_rt` 113/113, hutest PASS (55 checks, 0 failed, 0 unserved), scenarios 19/19,
  verify leg PASS.
- **savetest ok 17:54:33-18:16:02** (1,288 s): rows **30,666 / 351,496 / 689,879** = the reference counts; drift Tiny
  x86 = x64 (30,666 each, no differing turn); fixchecks 0096, 0084, 0103, oldsave pass; refsave (P10: the huge save
  `c499d3c6...` loaded at 350, played to 352, 37 s).
- **instance ok 18:21:27-18:39:05** on c5h-d (F:): tiny **185,517** and huge **388,512** answers verified, 0 mismatches,
  0 lock errors, 0 FALLBACK lines, purity loaded (pack=1); restore ok, verify rc 0. Found: the stage's own pool claim
  (owner `0509-release`) names no ticket, so `pool.py list` shows it LOUD "NO TICKET" (a box would go to the MC's
  desk; D155 part 4); `C5_RELEASE_HI_TICKET` covers only hi_journey. Re-claimed by hand with `pool.py transfer c5h-d
  --by 0509-release --ticket 0509 --address dispatcher-B` at 18:31 [L].
- **wholegames ok 18:39:05-21:12:55** (local, on the workstation): **4 of 4 x64 = x86, identical**: Tiny Continents
  5051/5253 Cultural 361 (903,979 rows), Standard Fractal 5757/6868 Science 414 (2,255,970), Standard Pangaea 3333/4444
  Cultural 370 (1,622,664), Small Pangaea 1819/2021 Science 427 (1,708,434): the same endings, turns and rows as 0463's
  takes and 0324's run, test database `bd9b8aa1...`.
- **hi_journey ok 21:12:55-21:52:54** on c5h-d (window 1920x1080, D158), gating (`--for-art`): **release-basic PASSED**
  (product 63 steps 522 s, stock 63 steps 469 s, 6/6 checkpoints each) and **release-large PASSED** (product 21 steps
  414 s, stock 21 steps 406 s, 5/5); index the committed `tools/hi/journeys/journeys.json`. The stock game crashed at its
  exit after release-basic (21:33:31, `0xC0000005`, no box, no proxy crash record; evidence
  `F:/c5-instances/c5h-d/run/crash/20261117_213331_pid72604`): the known `-ia` exit crash of test builds (0463 3.4,
  bugs 0483/0484), which the stage passes over ("crashed at its exit without a box; nothing runs; going on"); reported on
  the ticket's thread (D155). Restore ok, claim released.
- **report ok, tier validated reached 21:52:54**, `TAKE-EXIT 0`, 4 h 52 min wall on a shared workstation (not a timing).
  `take_release.py Release-5.4.8 --art-check --work C:/c5-64bit-work/0509/run`: exit 0, "for the owner: yes (both journeys
  passed on this build, product and stock)", played on x64 DLL `87b07172...`, server `54e873a2...`, proxy `8d809d01...`,
  i0ia `b71a9f31...`. **What the D162 stage covers**: it lays the layout on the instance itself with `vpproxy.ini`
  `mode=on` and does not switch the compositor on (no `vp64helper on`, no `ServerRenderer = 1`), so it played the x64
  product path; D162 for i2-rc1 as a build for the owner is met only when both journeys also pass on the installed i2 folder
  with the compositor on, in epic 0398's phase (MC on thread 0509, 2026-11-17) [C: `stages/hi_journey.py`].

**The installer** (`python -I tools/installer/build_installer.py --work C:/c5-64bit-work/0509/run --out
F:/c5-64bit-work/0509/installer --release-name i2-rc1`, 21:5x EST, exit 0; both D162 checks passed, before staging and
on the staged folder): **`F:/c5-64bit-work/0509/installer/VP64-Patch-i2-rc1-for-VP5.4.8.exe`, 8,759,842 B, SHA-256
`6ed1dad659476ca701fcd4cecbaaf2452a3ec6aefe0b64ffab3a7168304cf51b`**; configuration `i2`, VP 5.4.8, variant std only
(no 43-civ build of batch 3), minimum VC++ runtime 14.43.34808 (grade C, from the x64 build's record). The payload is the
15 files of installer-notes 12.1, `shell_composite.dll`, `.sha256` and `.ini` among them. A `--test-build --stage-only`
dry run before D162 had passed (18:4x) staged the same file list. Not run: the installer itself on an instance (0398's
phase installs it).

**Kept** (D141): the whole work folder `F:/c5-64bit-work/0509/take/` (4.5 GB: state, reports, layout
`layout/x64-4f4daac7-p7bf0d57`, `layout/i0ship-39f17f75[-ia]`, pack, build trees), the run's descriptor beside it, and the
installer folder. The C: work folder `C:/c5-64bit-work/0509/run` stays until the ticket merges (D111; the installer and
`--art-check` read it by path). Evidence in the repository: `tools/release/evidence/0509/` (state.json, validated-report,
hi_journey report, installer build record and SHA256SUMS, layout-SHA256.txt; user profile paths written as
`%USERPROFILE%`), scripts s01-s30.

## 0533: a crash without a box in an HI replay is a crashed game; the huge reference save's durable copy [C, hermetic tests 2026-11-18]

Ticket `tracker/issues/0533-hi-stage-boxless-crash-and-huge-save-path.md`, branch `task-0533-hi-stage-crash` (account
B), from 0517's tool gaps 2 and 3 (`research/03-dll-engine-boundary/upstream-fix-batch-4.md` 9.5-9.7). No live game run.

**(1) `stages/hi_journey.py`: a crash without a box ends the replay, not the stage.** Before (0517, 2026-11-18 01:18 EST,
c5h-b): the EXE's farm-field crash at VA `0x77C6DA` (bug 0515) in release-large's product-route load ended the game with
no box; `hi.py replay` stopped with exit 8 ("the game process crashed without a box"), and the stage raised `LeftRunning`
for every exit 8, so the instance stayed laid with the release and the claim kept although nothing ran (restored by hand,
17 min). Now [C, the code; hermetic tests]:
- **What proves a crash without a box** (`crash_end`): the dialog watcher's marker of kind `crash` (0419; 0531's WER
  case ends as kind `crash` too) whose every entry is the crash's stand-in (no box, no patch popup carried), or, with no
  marker, the proxy's `exe-crash.txt` naming the replay's pid and not older than its launch (the watcher's own reader,
  `dialogwatch.game_crash_record`); and in both cases `cli.py status` says no game runs. It is checked after a replay
  with exit 8, after any other non-zero exit when the game is gone, and when the guard check or the mod activation fails.
- **What the stage does then**: the row gets `crash` {`signature`, `code`, `address` (`<module>+<RVA>` from the
  record), `va`, `record`, `marker`, `source`, `pid`, `collect`} and `crashed_when`; **crash.collect runs once**: the
  watcher's (its marker records `crash`: {dest, ...}) or, when the marker has none or there is no marker, the stage's
  `crash.py collect --instance NAME --since <launch time> --pid PID` (new tool name `crash` in the stage's tools and
  NEEDS); `cli.py leftovers` as after any end. The journey's verdict is `failed` (a product finding; also when the crash
  came before the replay ran, which was `incomplete`), the other routes and journeys are played, the outputs list
  `crashes` [{journey, route, ...}], the report names each crash ("Crashed without a box on <route>", COMMON 30), and the
  restore (every file back by hash, `set-proxy --original`, `set-dll` back, front end, `verify --quick`) and the
  release run as after a normal end. The stage stays `ok` on a report-only take; on a take for the owner D162 refuses the
  build (the journey failed).
- **Unchanged (D29, D155)**: a box, a crash marker that carries the patch's popup, a game that still runs, a refused stop
  or processes left raise `LeftRunning` as before (instance kept laid, claim kept). A game gone with no marker and no
  record of its pid (or a record naming another pid) is not a crash: the replay's own failure, no evidence run.
- Tests (`tools/release/tests/test_hi_journey.py`, class `BoxlessCrash`, 8 tests): the crash with the watcher's marker
  and collect (no second collect, restored, released, stock route still played), a marker without collect (one
  `crash.py collect` with `--pid 4242`), a crash proved by the record alone, a crash during mod activation (`failed`, not
  `incomplete`), D162 refusing it on a take for the owner; negative: a popup in the crash marker stays `LeftRunning` (no
  restore, no release, no collect), no marker and no record is no crash, a stale record of another pid is no crash. The
  existing box, refused-stop, leftovers and normal-end tests pass unchanged.

**(2) The descriptor's huge reference save is a durable copy.** `Release-5.4.8.json` `reference_saves.huge.path` named
`.../AppData/Local/Temp/claude/e2000/inputs/E2000-HUGE_0350.Civ5Save`, a temp folder of 2026-11-10 that is gone, so
`run_d10.py` without `--huge-save` dropped the huge game ("huge save missing", 0517 9.6). Since 2026-11-18 it names
**`F:/c5-64bit-work/reference-saves/Release-5.4.8-HUGE_0350.Civ5Save`** (2,227,934 B, SHA-256 `c499d3c6cf83b8ba...`,
copied from 0517's take input `C:/c5-64bit-work/0517/run/inputs/Release-5.4.8-HUGE_0350.Civ5Save` and checked by hash;
`SHA256.txt` beside it; the same bytes as the instance save library's `C5H-VP8P-HUGE_0350` on c5h-d) [C, 2026-11-18
05:31]. F: is the long-term store (D141); the folder is not a ticket's, so no ticket clean-up removes it. `run_d10.py`'s
default (`DEFAULT_HUGE` from the descriptor) resolves to it, file present and hash equal [C, 2026-11-18 05:32; no game
played]. `test_descriptor.py` `test_huge_reference_save_is_a_durable_copy`: the path is not under a temp folder, and
where its drive exists the file is there with the descriptor's hash (skipped with the reason on a machine without F:).
Still naming the gone temp copy (not in 0533's files): `perf/capacity/capacity.py` `HUGE_SAVE`.

## 0634: a STOP during a launched journey ends the game before the restore [C, the code; hermetic tests 2026-11-23]

Ticket `tracker/issues/0634-release-driver-stop-during-journey.md`, branch `task-0634-stop-during-journey` (account B).
No live game run.

**The fault (0626's rehearsal take, 2026-11-21 01:46, MC-B #8976).** The take's STOP file was written while the
hi_journey stage's release-large product game ran on c5h-new-a. `Ctx.run` checks the stop file before every named
command (`take_release.py`), so the next command of the replay (here the activation wait's `turns.py frontend`) raised
`StopRequested` with the game running; `_claimed` went straight to `_restore`, and `set-proxy --original`, `set-dll` back
and `frontend.py restore` were each refused ("CivilizationV_DX11.exe is running"): RESTORE FAILED, claim kept, the
instance stopped and restored by hand. The same held for a gate's refusal (`GateRefused`) and any exception after a
launch [C, the code at main 75187b01e].

**Reproduced offline [C, 2026-11-23 12:28].** The test fake (`test_hi_journey.py`'s `FAKE`) now refuses `provision.py
set-proxy`/`set-dll` and `frontend.py restore` while its game runs, as the real tools do, and `FAKE_STOP_FILE` makes the
activation wait write the take's STOP file with the game running. The new class `StopDuringJourney` run against main's
`stages/hi_journey.py` (a scratch copy, `C:/c5-64bit-work/0634/old/`): 5 failures, 1 error; the driver exits 1, not 10,
with `RESTORE OF c5h-t FAILED: ['set-proxy --original: exit 1', 'set-dll back: exit 1', 'frontend restore: exit 2',
"front end not original ..."]`: the incident's three refusals.

**The fix (`stages/hi_journey.py`, `end_before_restore`).** In `_claimed`, when the replays did not end ok (a raised
`StopRequested`/`GateRefused`, an exception, a failed or skipped result), before the restore and with the stop check off:
- `cli.py status`; no game runs -> the restore as before (a STOP with no game sends no stop).
- a game runs -> a box is never pressed (`dialogwatch.py marker` exit 8: `LeftRunning`, D29, D155); else the harness's
  stop, `cli.py -i <instance> stop --human-turn-hook tools.harness.turns:stop_readiness` (AGENTS.md 4: the save settle
  and the human-turn hook first; one terminate, no kill, 0700's bounded end wait of 600 s), then `cli.py status` every
  POLL_S until no game runs, at most `GAME_END_WAIT_S` = 120 s after the stop returned. A stop exit other than 0 or 4
  with the game still running is a refused stop (COMMON 22).
- any of those three -> the stage fails loud: `GAME ON <instance> NOT ENDED, NOTHING RESTORED: <why>; <instance> keeps
  the release layout (left changed: <the undo list>) and the claim (<owner>) is KEPT ...`; no restore, no release (a
  StopRequested is then a failure, exit 1, not a stop: the instance is not fit for the next user).
- the game ended -> `ended_before_restore` {stop_rc, stop_log, end_s, after} in the outputs and the report's Restore
  section; the restore runs; a STOP is then passed on (exit 10) as before.
- `_restore` now returns `left_changed` (the labels of every undo that did not come back: files checked by SHA-256 in
  `_put_back`, `set-proxy --original` and `set-dll` back by exit, the front end by `frontend.py status`, `provision.py
  verify --quick` by hash); a failed restore's error is `RESTORE OF <instance> FAILED: ...; left changed on <instance>:
  [...]; the claim ... is KEPT`, and the report gains a `LEFT CHANGED (claim kept)` section.

The tier path (0671, `tiers/stages/hi_journey.py`) plays both routes through this module's `_run` (`tierlib.original`),
so it gets the fix unchanged: the game is ended inside the original before its restore, and the tier's installer
removal then runs on a stopped game; a game not ended reaches the tier as `left_running` and its removal is refused as
before. `tools/release/stages/hi_journey.py` is one of the tier's `HARNESS_PARTS`, so a stock route recorded before this
change is not carried into a take after it (the harness hash differs).

Tests (`tools/release/tests/test_hi_journey.py`, class `StopDuringJourney`, 6 tests; `test_stop_file_restores_and_stops`
extended): STOP with the game running (one harness stop with the hook, before `set-proxy --original`; restored by hash;
exit 10), STOP with no game (no stop sent; restored; exit 10), a game that does not end within the bound (exit 1, `GAME ON
c5h-t NOT ENDED`, claim kept, no restore, no release, one stop and no second, the report names the instance and what is
left changed), a refused stop (COMMON 22, no restore), a box at that moment (never stopped or pressed), an exception after
a launch (ended, restored), and the incident with the fix taken out (the loud `RESTORE ... FAILED` naming `set-proxy
--original`, `set-dll back` and the front end, claim kept). The file: 84 tests OK in 830 s (2026-11-23 12:13-12:27);
the release-tiers suite (`tools/release/tests`, which holds the tier hi_journey and modpack tests that load this module):
856 passed, 0 failed, 0 skipped in 2935 s (2026-11-23 12:29-13:18, beside two other suites).

Not verified: no live take. The operator rule of 2026-11-21 (no STOP file while a journey's game runs) can be dropped
once this is merged.

## 0640: the instance stage's claim names the take's ticket and address; the installed layout plays the product route only [C, the code; hermetic tests 2026-11-23]

Ticket `tracker/issues/0640-release-driver-instance-claim-and-installed-stock-route.md`, branch
`task-0640-release-driver-stage-faults` (account B). Faults 4 and 3 of ticket 0626's rehearsal
([release-builds.md](release-builds.md) 7.7). No live take, no game.

**(a) The instance stage's claim (`stages/instance.py`, `claim_identity`, `claim`, `claim_one`).** Before 0640 `claim_one`
ran `pool.py claim --owner <who>-release --hours 3 --note ...` with no `--ticket`/`--address`, so `pool.py list` showed the
claim `!! NO TICKET` and a box on the instance went to the MC's desk (D155 part 4, ticket 0388); 0509 and 0626 re-claimed by
hand with `pool.py transfer`. Now [C]:
- the ticket is `C5_RELEASE_TICKET`, else `C5_RELEASE_HI_TICKET`; the address `C5_RELEASE_ADDRESS`, else
  `C5_RELEASE_HI_ADDRESS` (a take's operator sets the hi_journey pair already: 0509, 0574, 0626 scripts). The claim is
  `pool.py claim ... --ticket T --address A`; the stage's outputs carry `ticket` and `address`. Neither is in the stage's
  inputs (a recorded result is not invalidated; what the stage measures is unchanged).
- without either setting nothing is claimed: `claim()` returns at once (no `C5_RELEASE_INSTANCE_WAIT`, no HOLD-INSTANCE
  wait), and the stop reads `no test instance could be claimed (the instance stage claims only with the take's ticket and
  address (D155 part 4: ...); not set: C5_RELEASE_TICKET or C5_RELEASE_HI_TICKET)`, the unblock naming both pairs.
- a pool refusal that no wait cures (`FINAL_REFUSALS`: pool.py resolve()'s unknown or finished ticket, no owner, a dead
  or malformed address) ends the claim wait at once ("no wait cures that") instead of retrying it as "no free instance"
  for 90 min (the tier hi_journey stage's FINAL_REFUSALS of 2026-11-22 did the same for its own claim).
- the tier's instance stage (`tiers/stages/instance.py`) calls this `claim()`, so it gets the same rule (its stop's
  unblock text stays its own, the why names the settings).
- **Operator line:** a take now needs `C5_RELEASE_TICKET=<ticket> C5_RELEASE_ADDRESS=<live queue address>` (or the
  `C5_RELEASE_HI_*` pair) before the instance stage runs; `tools/release/README.md`, `docs/RELEASE-TIERS.md` and the
  runbook still list only the HI pair (the runbook lines were posted to the MC at the hand-back; the runbook is not edited
  by a ticket).

**(b) The journey stage on the installer-installed layout (`stages/hi_journey.py`, `settings`, `installed_routes`,
`InstalledStockRefused`, `_run`).** Before 0640, `C5_RELEASE_HI_INSTALLED=<instance>` played both routes (default
`product,stock`), and the stock route there (vpproxy.ini mode=off: the player's own VP DLL, no Lua channel, config.ini
left in the D33 form) could not pass (0626: exit 5 at the first `in_game` judge, a deep red map). Now [C]:
- with `C5_RELEASE_HI_INSTALLED` set and `C5_RELEASE_HI_ROUTES` unset the routes are `product`; any route list other than
  exactly `product` is refused by `installed_routes()` right after the index is read and **before the claim**: a stop
  (exit 10) `REFUSED, nothing claimed or played: routes '...' on the installer-installed layout of <I>: ... cannot be
  judged ...`, a `LOUD: hi_journey REFUSED` line, the report written with `verdict incomplete`, `refused_routes` and the
  layout. The unblock names the stage-laid way (after the installer's removal, `C5_RELEASE_HI_ROUTES=stock`
  `C5_RELEASE_HI_INSTANCE=<I>` without `C5_RELEASE_HI_INSTALLED`, or the validated plan's default tier path of 0671).
- the tier stage keeps its own `installed_env()` (same rule, same words; validation-tiers.md 11) and always passes
  `C5_RELEASE_HI_ROUTES=product` to the installed play, so its behaviour and order (product installed, removal, stock
  stage-laid) are unchanged; `test_tier_hi_journey.py` covers that order (`test_product_fails_stock_fresh`,
  `test_fresh_stock_is_played_after_the_removal_on_the_stage_laid_layout`). The tier was not made the one implementation:
  `tools/release/tiers/` was outside the ticket's files; the two refusals are one rule written twice.
- **D162 unchanged:** an installed-layout play alone is now never a D162 yes (`route stock not played`), as the rule asks
  for both routes; before 0640 the by-hand installed path could answer yes on a stock route that could never pass anyway.
- The report's layout section now reads `fallback (mode=off; the stock route is not played on this layout, 0640)`.
- `stages/hi_journey.py` is one of the tier's `HARNESS_PARTS`, so a stock route recorded before this change is not carried
  into a take after it (the harness hash differs), as with 0634.

**Tests** (hermetic, the fakes): `test_validated_tier.py` `Instance` (21 tests, 4 new: the claim carries `--ticket 0640
--address impl-test` and the outputs record them; the HI pair as fallback and the stage's own name first; without
ticket or address the stop names the settings, no claim, no provision call, no wait; an unknown ticket or a dead address
ends the wait after one claim), the fixture now sets `C5_RELEASE_TICKET`/`C5_RELEASE_ADDRESS` (the major-civ, intermediate
and carried tests inherit it), the fake pool's `FAKE_POOL_REFUSE`; `test_hi_journey.py` `Installed` (14 tests, 4 new: the
refusal for `stock`, `product,stock`, `stock,product` with nothing claimed, laid, launched or changed; the refusal on a
`--for-art` take; `product` named from outside plays; `installed_routes` and the settings' default; and the changed ones: the
installed play launches `on, on` only, and its D162 line on a `--for-art` take is NO with `route stock not played`). Targeted runs
2026-11-23 14:10-14:12: Installed 14 passed (31 s), Instance 21 passed (48 s). The release-tiers suite
(`tools/release/tests`, through `quietlock.py run`): 864 passed, 0 failed, 0 skipped in 2548 s (2026-11-23
14:14:56-14:57:26, a shared workstation); `test_safety_static.py` 55 passed (67 s).

Not verified: no live take on an instance (the claim's ticket/address against the real pool and queue board).

## 0708: the D162 options and strings say "the owner"; the old option names are aliases [C, the code and hermetic tests 2026-11-24]

Ticket `tracker/issues/0708-owner-name-out-of-release-tools.md`, branch `task-0708-owner-name` (account B). The tools in
`tools/release/` and `docs/RELEASE-TIERS.md` named the owner by first name in code strings, help texts, option names and
docs; the export gate passed them only because `tools/` is outside its name scope. Now (all of `tools/release/` but
`evidence/`, which keeps the records of past runs as they were written):

- Options: `--for-owner`, `--not-for-owner`, `--owner-check` (take_release.py; take_tier.py passes the first two through
  and refuses `--owner-check` as it refused the old name). The old names `--for-art`, `--not-for-art`, `--art-check` are
  argparse actions with `help=SUPPRESS` (not in `--help`'s usage or option list) that set the same `dest`; `main()` of
  both drivers prints one line per old name used, however often it was given:
  `take_release: note: --art-check is the old name of --owner-check; it still works, use --owner-check` (`take_tier:`
  in take_tier.py). The note is an extra stdout line only: `tools/installer/build_installer.py` reads the exit code of
  `take_release.py <tag> --art-check` and keeps its lines as text, so it keeps working unchanged (it still calls the old
  name; its own option `--art-check-waiver` is outside this ticket).
- Strings: the summary line's verdict is now `for the owner: yes | NO, refused by D162` (it named the owner); the report's
  `**For the owner (D162): ...**`; the gating text `gates: a take for the owner; ...`; tierraise's
  `ask the owner for the full tier`. A reader that matches the old text must change: none in the repository outside
  `tools/release/` does (searched 2026-11-24: `tools/harness/tests/test_installer.py` fakes its own line; research
  evidence folders hold old outputs).
- Identifiers: `owner_check`, `owner_line`, `for_owner`, `owner_check_main`, `own_clones`; `stages/hi_journey.py` keeps
  `art_check = owner_check` for `tools/hi/journeys/tests/test_m43_selection.py` (outside tools/release).
- State: the config key is `for_owner` and hi_journey's answer is outputs `owner` (the `d162` record's flag is
  `for_owner`). A state written before 0708 (`config.for_art`, outputs `art`) is still read by `build_config`, the
  `--owner-check` path, `hi_journey.for_owner()` and `tierreport.gate_line()`; the next run writes the new keys only.
  Stage inputs never held the config, so the rename makes no stage of a resumed take run again; a stage that USES
  hi_journey's outputs sees a changed hash only after hi_journey itself ran again.
- Not renamed: `tools/release/tests/test_tier_art_check.py` (its name is a target of `tools/suites/areas.json`
  `release-hotfix`, outside this ticket's files), the kit `research/06-tooling-and-workflows/evidence/0324/
  stage_first_playable.py` (its `art` command, `art_gate` and `--not-for-art` are evidence; the test that drives it still
  calls them), and the machine paths in `tools/release/releases/*.json` and the default paths in `tools/release/README.md` (a Windows
  user folder, not a reference to a person).

Tests: `test_take_release.py` `OldOptionNames` (6: each old name sets what its new name sets, the defaults, the last
marker wins across old and new names, one note for a name given twice, the old names absent from `--help`, a state with
`config.for_art` keeps its marker); `test_hi_journey.py` `ForOwner` (2 new: `--for-art` marks a take and `--art-check`
answers yes with one note; `--owner-check` on a state written with `for_art`); `test_tier_raise_report.py` (1 new: the
gate line reads outputs `art`). Runs 2026-11-24 on the workstation (other agents' loads beside them), on the branch's
first commit 4a6ddbe0f: `run_suites.py --suites release-hotfix,release-intermediate --lanes 3 --tier intermediate`
13:06-13:36: safety-static 55 passed (56 s), release-intermediate 165 passed (1,251 s), release-hotfix TIMEOUT at its
1,800 s limit with every test so far passing (test_hi_journey.py ran beside it); release-hotfix's ten files then as two
plain pytest processes side by side, 13:37-13:50: 76 + 251 = 327 passed, 0 skipped (787 s and 680 s);
`test_hi_journey.py` with `tools/hi/journeys/tests/test_m43_selection.py` 119 passed (2,071 s, 13:06-13:41);
`tools/export/check.py --rev HEAD --since origin/main` PASS. Record: `tools/release/evidence/0708/acceptance.txt`. [L]
for the hermetic runs; no take, no game.
