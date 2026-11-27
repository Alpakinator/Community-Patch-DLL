# Player limits: the 43-civilisation build and where every player limit lives (epic 0334, D146)

Status: Part A of epic 0334, written 2026-11-15 06:58-07:36 EST by lead-0334 (sections 0, 6, 7; issue 4251) and its
three workers (sections 1-2: 4256; section 3: 4257; sections 4-5: 4258), source and EXE reads only, nothing run live.
Sent to the MC for review (the Part A gate) before Part B (4252-4255). Confidence marks `[C]` decompile, dump or
source read, `[L]` live, `[I]` inferred. Source reads name the fork commit (pin x64 `04b1211b8` unless named) and the
file and line; EXE addresses are DX11 VAs at ImageBase 0x400000.

## 0. Summary (lead, 4251)

1. **The 43 build is one define [C, 1.1].** Upstream uncomments `#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)` in
   `CvGameCoreDLLUtil/include/CustomModsGlobal.h:11` (its comment allows 2 to 62; `scripts/release.py:401-434` does the
   edit, builds, and comments it out again). It moves only the major/minor split inside the fixed 63 civilisation slots:
   `MAX_MAJOR_CIVS` 22 -> 43, `MAX_MINOR_CIVS` 41 -> 20, `OBSERVER_TEAM` 21 -> 42. `MAX_CIV_PLAYERS` 63, `MAX_PLAYERS`
   and `MAX_TEAMS` 64, the barbarian in slot 63 and `REALLY_MAX_PLAYERS` 80 stay. The 43 build also widens
   `KnownPlayersBitArray` from `uint32` to `uint64` and compiles three `#if` blocks: start regions, pantheon reuse, and one
   no-op. The database is unchanged: the DLL writes the `MAX_*` rows into `Defines` at load. The front end needs one
   file: the (3b) mod's `AdvancedSetup.lua`, whose city-state slider reads 20 instead of 41. Upstream's instruction texts
   name the wrong define, and release tags 3.8.1, 3.10.11 and 4.2.5 were cut with the switch on.
2. **The EXE fixes 64 slots, hard; 22 majors only softly [C, 3].** The per-frame slot poll 0x6A1DC0 and its 64 x 0x18
   table at 0x2DDFE40, the network session (fewer than 63 players, 64 flags) and the save loops stop at 64 (or 63).
   These are code immediates or fixed static sizes: no data write moves them (D4), and there is nothing to put to the owner in
   D136's form. Below 64 the EXE has six places tied to slots 0-21. None of them stops a 43-major single-player game:
   - network seating of humans covers slots 0-21 only;
   - the 3D leader scenes are for players 0-21; VP shows a static picture above that;
   - a per-turn summary record covers majors 0-21 (its consumer is not traced);
   - the EXE's random minor-civ fill starts at slot 22 (VP's DLL overrides it);
   - the observer fill and the 22/41 map defaults.

   Up to 62 majors nothing new breaks in the EXE.
3. **The product needs almost nothing for 43 [C, 4].** The proxy and the remote-mode server use only the totals
   (`MAX_PLAYERS`, `MAX_TEAMS`, `MAX_CIV_PLAYERS`), which do not change. No interface signature or record depends on
   the split. The layout-changing streams pass through the proxy as opaque bytes. Only four lines of the server's
   headless host depend on the split (`server/src/srv_main.cpp:431, :471, :525, :536`).

   **Recommendation for Part B:** one proxy and one server.exe serve both DLLs. The server reads
   `Defines.MAX_MAJOR_CIVS` once at run time, after post-processing, as c5h already does. Only the x64 DLL, its purity
   list and its parity baseline are built per variant (4.2).
4. **Saves do not cross [C layout, I symptom, 5].** 191 serialised arrays sized by the split are written without a
   count, and nothing in the header or the body records the major count. Neither the EXE nor the DLL refuses a save from
   the other build: the DLL only logs a line. A 22 save in the 43 build should crash early in `Game::Read`; a 43 save in
   the 22 build probably crashes, and a silent misread cannot be ruled out. This is upstream's behaviour too. 4254 checks
   it live and decides whether a refusal is worth adding (5.3).
5. **Cost of 43 [I, 2.2].** About 2-3 MB more DLL memory for the same game; pairwise diplomacy work grows with live
   majors squared, about 3.8x from 22 to 43 live majors. Part B measures the real per-turn memory and time.
6. **The Hormigas map pack does not fit the 43 build in full [C, arithmetic].** It has start positions for 43
   civilisations and 52 city states, but 43 majors leave 20 city-state slots (63 - 43). An AI autoplay run also needs
   one major slot for the observer (`giant-stress-game.md` item 3). So the stress game is at most 42 AI majors, the
   observer and 20 city states. The pinned VP database has 44 playable civilisations, enough for 43.
7. **Beyond 43 and beyond 64 (section 6).** Up to 62 majors is the same work as 43 again, with fewer city states
   (63 - M). More than 64 players in all is not possible with this EXE without code bytes; only a redesign in which the
   EXE never sees the extra slots could do it (section 6.2). Not recommended now.
8. **Part B as built (2026-11-15, section 8) [L].**
   - The release driver builds the 43 variant with `--major-civs 43` from the unchanged pins (a `/D` define, MC G2).
     The x64 43 build that ships is `release-43` in `deps/dll.lock`, layout `x64-04b1211b-m43-p9aa30f1`, DLL
     CF981126.... The 32-bit 43 DLLs are in `deps/i0.lock` `release-43` and `instance-43` (8.1).
   - One proxy and one server.exe serve both DLLs: the major count is read at run time, with a boot guard (8.2).
   - Parity x64 43 = x86 43: a whole 30-major game to turn 387, 3,613,758 rows identical. The save test passes. The
     purity list keeps 143 of 143 rows (8.3).
   - Cross-variant saves fail at once. Through the proxy the failure is a CRT abort box (bug 0359); the refusal is
     ticket 0354 (8.3).
   - The Hormigas game (42 AI majors + the observer + 15-17 city states) ran 180 AI turns clean on the product path
     and on stock. A leader above slot 21 does not stall the AI turn (8.4).

## 1. How upstream makes the 43-civilisation variant (worker A1, 4256)

### 1.1 The switch and every constant it changes

Written 2026-11-15 07:22 EST by the 4256 worker from a source read of the fork at the x64 pin `04b1211b8` (all
file:line below are at that commit unless named otherwise). Evidence and scripts: `evidence/0334/a1/`.

**The switch [C].** One preprocessor define, `MOD_GLOBAL_MAX_MAJOR_CIVS`, in
`CvGameCoreDLLUtil/include/CustomModsGlobal.h:7-12`: three commented lines for 34, 43 and 62; the comment says "valid
values are 2 to 62" and "for the default value of 22, just comment the line out". The 43 build is that file with the
43 line uncommented and nothing else changed. Upstream's release script does exactly this text substitution
(`scripts/release.py:401-434`, `enable_43_civ_build` and `disable_43_civ_build`, step 7 at `:871-888`), builds release
and debug, copies the release DLL into the `(3b) 43 Civs Community Patch` folder (`:483-489`), then comments the line
out again. `DEVELOPMENT.md:15-18` gives the same manual step.

**The derived constants (`CvGameCoreDLLUtil/include/CvDLLUtilDefines.h`) [C].** With M = `MAX_MAJOR_CIVS`:

| Line | Constant | Definition | 22 build | 43 build |
|---|---|---|---|---|
| 21-22 | `REALLY_MAX_PLAYERS`, `REALLY_MAX_TEAMS` | literal; comment at :20: "do not change this or have MAX_PLAYERS exceed it - this will require the engine to be rebuilt" | 80, 80 | 80, 80 |
| 24-25 | `MAX_CIV_PLAYERS`, `MAX_CIV_TEAMS` | literal 63; teams = players | 63, 63 | 63, 63 |
| 27-32 | `MAX_MAJOR_CIVS` | `MOD_GLOBAL_MAX_MAJOR_CIVS` if defined, else 22 | 22 | 43 |
| 28 | `MAX_PREGAME_MAJOR_CIVS` | 22, defined only when the switch is on | (undefined) | 22 |
| 33 | `MAX_MINOR_CIVS` | 63 - M | 41 | 20 |
| 35-36 | `MAX_PLAYERS`, `MAX_TEAMS` | `MAX_CIV_PLAYERS` + 1 | 64, 64 | 64, 64 |
| 37-38 | `BARBARIAN_PLAYER`, `BARBARIAN_TEAM` | slot 63 | 63 | 63 |
| 40 | `OBSERVER_TEAM` | M - 1 (comment: works only because observer mode is multiplayer/debug only) | 21 | 42 |

So the switch moves only the major/minor boundary inside a fixed 63 civ slots plus the barbarian: majors are slots
0..M-1, city states M..62, the barbarian 63. Totals (`MAX_PLAYERS`, `MAX_TEAMS`, `REALLY_MAX_*`) do not change [C].
`MAX_PREGAME_MAJOR_CIVS` is defined but has no use left in the DLL (the listing finds only its define); its last uses,
in `CvGame.cpp`, were removed by upstream `47eba2237` (2023-10-08, #10342): until then the 43 build shifted every
city-state slot by `MAX_PREGAME_MAJOR_CIVS - MAX_MAJOR_CIVS` when it read the city-state type from the pregame data,
which says the front end placed city-state types as if there were 22 majors [C: the diff; I: the reason]. Today the
DLL assigns city-state slots itself (`CvGame.cpp:806-835`).

**Types that depend on M [C].** `KnownPlayersBitArray` is `uint32` for M <= 32 and `uint64` for M <= 64, with an
`#error` above 64 (`CvGameCoreDLL_Expansion2/CvPreGame.h:297-304`, the same block in `CvDllNetInitInfo.h:83-90`). The 43
build therefore stores the "known players" table (one entry per major, `CvPreGame.cpp:3896-3910`) as 43 x 8 bytes
instead of 22 x 4, in the pregame part of the save (`saveSlotHints`, `CvPreGame.cpp:446-470`, no version change; the
save consequences are section 5's) and in the net-init info object.

**Code compiled only in the variant (`#if defined(MOD_GLOBAL_MAX_MAJOR_CIVS)`) [C].** Three sites:
- `CvStartPositioner.cpp:403-424`: start regions are subdivided by a small general rule instead of the hard-coded
  table, which covers 2..22 divisions only (`:425-470`). Above 32 divisions it halves.
- `CvReligionClasses.cpp:2204-2209` (`IsPantheonBeliefAvailable`): once every possible pantheon has been founded, a
  pantheon belief already taken by another civilisation becomes available again, so that the later civilisations of a
  43-civ game can still found one.
- `CvMinorCivAI.cpp:4487-4491` (`Init`): reads the city-state type through `GetMinorCivType()` instead of
  `CvPreGame::minorCivType`; both read the same pregame value today (`:4723-4725`), so it is a no-op.

**Other places that read M at run time [C].** `CvDllContext.cpp:248` logs "Supporting %i major civilizations" at DLL
start. `CvDllDatabaseUtility.cpp:267-274` writes `MAX_CIV_PLAYERS`, `MAX_CIV_TEAMS`, `MAX_MAJOR_CIVS`,
`MAX_MINOR_CIVS`, `MAX_PLAYERS`, `MAX_TEAMS`, `BARBARIAN_PLAYER` and `BARBARIAN_TEAM` into the `Defines` table at every
database load (`INSERT OR REPLACE`): this is where Lua's `GameDefines.MAX_MAJOR_CIVS` comes from, so the Lua sees 43
without any database file change. The pinned 22-major database (`Civ5DebugDatabase.db`, `bd9b8aa1...`) holds
`MAX_MAJOR_CIVS` 22, `MAX_MINOR_CIVS` 41, `MAX_CIV_PLAYERS` 63 [C: `evidence/0334/a1/count_civs.py`, 2026-11-15].

**Literals that silently assume 22, 41, 63 or 64 [C, listing].** None in the DLL's C++. The scan of every line in
`CvGameCoreDLL_Expansion2/`, `CvGameCoreDLLUtil/` and `CvWorldBuilderMap/` with 22, 41, 42, 43, 62, 63, 64 or 80 next to
a player word (`listing/literals.tsv`, 22 rows) finds only the defines themselves, the `KnownPlayersBitArray` block,
64-byte string buffers and alignments, a path length of 42 and threshold values of 80 [C, each row read]. The player
counts go through the constants everywhere. The only literal player limits found are in the World Builder map format
(`CvWorldBuilderMap/include/CvWorldBuilderMap.h:33-37`: `MaxPlayers` 32, `MaxCityStates` 64, its own team and
barbarian numbering), a file format independent of the switch (section 2.3).

**The instruction texts are wrong, the script is right [C].** `NEW RELEASE INSTRUCTIONS.txt:122` (step A) and
`NEW TEST VERSION INSTRUCTIONS.txt:32` say to uncomment `#define MAX_MAJOR_CIVS (43)` in `CustomModsGlobal.h`; no such
line exists, the line is `// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)` (`CustomModsGlobal.h:11`). Someone who added the
text literally would get `MAX_MAJOR_CIVS` defined twice: `CvDLLUtilDefines.h:31` redefines it to 22 after the include,
so MSVC warns (C4005) and builds a 22-major DLL [I: preprocessor rule, not built]. `scripts/release.py` and
`DEVELOPMENT.md` name the right define. A one-line upstream text fix (D95 rules apply); drafted in
`upstream-drafts-3505.md` section F1 (2026-11-24, still present on upstream master b0bce8c2f, not sent).

**Release tags that carry the switch on [C, `evidence/0334/a1/tags_43_state.py` over all 200 `Release-*` tags,
`listing/tags_43_state.tsv`].** `Release-3.8.1`, `Release-3.10.11` and `Release-4.2.5` have the 43 line uncommented
(the old instructions told the releaser to amend the release commit with it); upstream re-commented it in the next
commit to the file each time (`03388505a` 3.8.2 Release, `f4fb91289`, `5a07a1f7e`; the same slip earlier: `205919fd0`
2019-01-29 "fixed 43 switch", `47c383c39` 2020-08-24 "fix 43 define") [C: `git log` of the file].
Every other tag, `Release-5.4.8` included, has it commented. A build from one of those three tags would be a 43 build
without asking: our release driver should read the line, not assume it.

### 1.2 What else changes: database, UI and Lua, savegame layout

**Database: nothing in files [C].** The `(3b) 43 Civs Community Patch` folder at `Release-5.4.8` holds a modinfo, the
ModBuddy project files, `MANUAL INSTALL.txt`, `AdvancedSetup.lua` and the prebuilt 43 DLL (tree listing only; the
binary is not taken or run). No SQL or XML. No SQL or XML anywhere in the mod folders names a player constant. The
only database change is the `Defines` rows the DLL writes at load (1.1). The modinfo
(`(3b) 43 Civs Community Patch (v 1).modinfo`) depends on `(1) Community Patch` (minversion 151), sets
`AffectsSavedGames` 0, imports `AdvancedSetup.lua` and `CvGameCore_Expansion2.dll` into the VFS, and describes the mod
as "supports up to 43 major civilizations. The maximum number of total players (civilizations and City-States) is 63"
[C]. The pinned VP 5.4.8 database has 44 rows with `Playable` = 1 in `Civilizations` and 120 `MinorCivilizations` rows
[C: `count_civs.py`], so 43 majors and 20 city states can all be filled from VP's own data.

**UI and Lua: one front-end file [C].** The (3b) `AdvancedSetup.lua` is the game's own front-end setup screen with one
change, on two lines (1089 and 1097): the city-state slider's upper limit is computed as `MAX_CIV_PLAYERS -
MAX_MAJOR_CIVS` (20 in the 43 build) instead of the game's literal 41 [C: a diff of the two files, 2026-11-15, the
game's file read from the Steam install, read only, nothing copied]. Without it the slider would offer up to 41 city
states for 20 slots. Every other loop over majors in that file already reads `GameDefines.MAX_MAJOR_CIVS`. The rest of
the UI needs no change because it reads `GameDefines`: the listing (`listing/lua.tsv`) finds 217 uses of
`GameDefines.MAX_*` / `BARBARIAN_PLAYER` in 64 Lua files of the fork (VP 59 and CP 43 `MAX_MAJOR_CIVS`, CP/VP/EUI 17
each `MAX_CIV_PLAYERS`, EUI `UI_bc1` 14 + 9, ...) and no `for` loop with a literal bound of 21, 22, 41, 42, 43, 62, 63 or
64. Which screens of the base game have their own constants is the EXE's side (section 3.2, A2). History: until
`Release-1.0` (2022-02-19) the 43-civ mod carried about 200 overridden UI files (`43 Civs CP/43 Civs CP/{CP Only,EUI,
No-EUI}/...`), first committed 2017-01-26 (`1481ddd7c`); on 2022-03-11 (`fbfd33f1a`, "Rename 43 Civs CP") it shrank to
`AdvancedSetup.lua`, the other screens having moved to `GameDefines` in the main mods [C: tree listings].

**Savegame [C].** No version marker and no header field says which build made the save. M itself is written in one
place: the per-city espionage record writes `MAX_MAJOR_CIVS` as a count before each of its per-major arrays
(`CvEspionageClasses.cpp:7489-7621`, `operator<<(FDataStream&, const CvCityEspionage&)`; upstream fix `30a2349ed`
#13527 concerned these vectors, `research/sources/0081-upstream/0081-upstream-fixes-for-mc.md:32`). Otherwise what
changes is layout: every array dimensioned by `MAX_MAJOR_CIVS` or `MAX_MINOR_CIVS` that is written element by element
has a different length, and the pregame's known-players table changes element width (1.1). Which arrays (2.1) are
serialised, with or without a count, and how a 22-major save reads in the 43 build is section 5 (A3, 4258).

### 1.3 Since which upstream release, and how the installer ships it

**Origin [C, `evidence/0334/a1/history.py` output, `git log -S MOD_GLOBAL_MAX_MAJOR_CIVS`].** The switch came with
whoward69's Pick'N'Mix DLL collection, the base of the Community Patch: `0d366b7c7` and `8969fda0d`, 2014-06-10
("PNM-DLL-v51 Whoward's PNM Mod collection added - base of Community Patch"), which already has
`CustomModsGlobal.h` with the 34, 43 and 62 lines and the 2-62 comment, and the `#if` blocks in `CvDLLUtilDefines.h`,
`CvGame.cpp`, `CvMinorCivAI.cpp`, `CvStartPositioner.cpp`, `CvDllContext.cpp`. Later uses: `CvReligionClasses.cpp`
(2017-07-28, `01151c5f1`), the `CvGame.cpp` blocks removed in 2023 (`47eba2237`), `scripts/release.py`
(2026-02-10, `ebd11d2b2`). So the variant predates every `Release-*` tag (the first, `Release-1.0`, is 2022-02-19);
every tag has the define, commented or not (1.1), and every tag has a 43-civ folder (`43 Civs CP` in 23 tags up to
early 2022, `(3b) 43 Civs Community Patch` since).

**34 and 62 [C/I].** The 34 line is the PNM author's "accepted improved value" (comment, `CustomModsGlobal.h:8`); 62
is the largest value that leaves one city-state slot (63 - 62 = 1; 63 would leave `MAX_MINOR_CIVS` 0 and two
zero-length arrays, 2.3). Neither is built or shipped by upstream: the release script and the installer know only 43
[C: `release.py`, `VPSetupData.iss`]. Why 43: the Brave New World ruleset with all DLC has 43 playable civilisations
[I; the pinned VP database counts 44 `Playable` rows, 1.2].

**Installer [C: `VPSetupData.iss` at `04b1211b8`; in the repository root since `Release-4.9` (2024-06-15,
`372ca12de` moved it in)].** Three of the installer's mutually exclusive components are 43-civ: `Civ43CPOnly` ("43 Civ
Community Patch only"), `Civ43NoEUI` ("43 Civ Vox Populi (no EUI)") and `Civ43EUI` ("43 Civ Vox Populi (with EUI)")
(`:71-73`, types `:80-82`). Each installs exactly what its 22-major twin installs (`(1)`, `(2)`, `(3a)`, `(4a)`, `(5)`,
`UI_bc1`, `VPUI` per component, `:46-58`), plus `(3b) 43 Civs Community Patch\(3b) ... .modinfo` and `AdvancedSetup.lua`
into `MODS\(3b) 43 Civs Community Patch\` (`:49-50`), and writes the 43 DLL from the build's (3b) folder over
`MODS\(1) Community Patch\CvGameCore_Expansion2.dll` (`:51`). So in a 43 install there is one DLL, in `(1)`; the
(3b) folder carries no DLL, although its modinfo lists one (`MANUAL INSTALL.txt` says the same for a manual install:
replace the DLL in `(1)` with the one from (3b)). `[InstallDelete]` removes old `(3b)` and `(6c) 43 Civs ...` folders
(`:95`, `:113-114`). `NEW RELEASE INSTRUCTIONS.txt:122-135` and `scripts/release.py` step 7 also keep the 43 release
and debug DLLs and PDBs in `Debug/43 Civ/{Release,Debug}` of the release's debug archive.

For our build (Part B): the variant is the same source with one define; our release driver can set
`MOD_GLOBAL_MAX_MAJOR_CIVS=43` on the compiler command line instead of editing the header, since
`CvDLLUtilDefines.h:27` only tests `defined(...)` and `CustomModsGlobal.h` defines nothing else [I: not built here;
4252 confirms]. The installer layout to copy is "the 22-major product plus the (3b) modinfo and `AdvancedSetup.lua`,
with the 43 DLL in `(1)`".

## 2. The DLL: constants and fixed-size arrays (worker A1, 4256)

### 2.1 Every use of the player constants, grouped (arrays sized by them, loops, literals, masks, types)

Written 2026-11-15 07:27 EST by the 4256 worker [C: source read at `04b1211b8`, machine listing]. Command (from the
repository root; the tree is `git archive 04b1211b8` extracted to scratch):
`python -I research/03-dll-engine-boundary/evidence/0334/a1/list_player_constants.py <tree> research/03-dll-engine-boundary/evidence/0334/a1/listing`.
Outputs in `evidence/0334/a1/listing/`: `uses.tsv` (one row per use: file, line, constant, kind, member), `arrays.tsv`
(every fixed array with a player dimension, its class and its bytes at 22 and 43 majors), `nested.tsv` (functions with
player loops nested inside player loops), `literals.tsv`, `lua.tsv`, `summary.txt`. The listing is our own text (file,
line, kind, member name), no source lines. Scope: `CvGameCoreDLL_Expansion2/`, `CvGameCoreDLLUtil/`,
`CvWorldBuilderMap/`; comment-only mentions (20) excluded. The kinds are assigned by patterns on one line, so a
multi-line statement can land in `other` or `comparison`; the counts are good to a few percent [I], the sites named
below were each read.

**2,918 uses in 86 files** (CvDiplomacyAI.cpp 723, CvMinorCivAI.cpp 272, CvGame.cpp 256, CvTeam.cpp 231, CvPlayer.cpp
203, CvDiplomacyAI.h 178, CvPreGame.cpp 141, CvEspionageClasses.cpp 101, CvVotingClasses.cpp 91, CvCity.cpp 69, ...):

| Constant | loop bound | range check (`PRECONDITION`, `ASSERT`) | comparison | fixed array | sized container | minor loop start / index offset | fixed slot | other | total |
|---|---|---|---|---|---|---|---|---|---|
| `MAX_MAJOR_CIVS` | 633 | 543 | 151 | 198 (+13 typedefs) | 7 | 104 / 10 | 2 | 10 (+4 `#if`, 4 define) | 1,679 |
| `MAX_PLAYERS` | 205 | 52 | 87 | 2 (+1 typedef) | 64 | - | - | 6 | 419 |
| `MAX_CIV_PLAYERS` | 260 | 77 | 31 | 19 | 8 | - | - | 9 | 409 |
| `MAX_TEAMS` | 63 | 65 | 7 | 1 | 1 | - | - | 6 | 144 |
| `BARBARIAN_PLAYER` | - | 1 | 48 | - | - | - | 49 | - | 99 |
| `MAX_CIV_TEAMS` | 55 | 17 | 12 | 5 | - | - | - | 1 | 92 |
| `BARBARIAN_TEAM` | - | - | 8 | - | - | - | 17 | - | 26 |
| `REALLY_MAX_TEAMS` | 1 | 2 | - | 17 | 1 | - | - | - | 22 |
| `REALLY_MAX_PLAYERS` | 3 | 4 | - | 1 | 6 | - | - | - | 15 |
| `MAX_MINOR_CIVS` | 1 | - | 2 | 2 | - | - | - | 4 | 10 |
| `OBSERVER_TEAM`, `MAX_PREGAME_MAJOR_CIVS` | - | - | - | - | - | - | 1 | - | 3 (with defines) |

The per-kind meaning and the sites that matter:

- **Fixed arrays (263 declarations; `arrays.tsv`).** Almost all are dimensioned by `MAX_MAJOR_CIVS`. By class, bytes per
  instance at 22 -> 43 majors: `CvDiplomacyAI` 177 arrays, 22,450 -> 46,348 B (`CvDiplomacyAI.h:1924-2165`, largest
  `m_aaiTurnStatementLastSent[MAX_MAJOR_CIVS][NUM_DIPLO_STATEMENT_TYPES]` 9,944 -> 19,436 B, and the squared
  `m_aaeCoopWarState`, `m_aaiCoopWarStateChangeTurn[MAX_MAJOR_CIVS][MAX_MAJOR_CIVS]` :1965-1966); `CvMinorCivAI` 27
  arrays, 1,717 -> 2,935 B plus `m_IncomingUnitGifts[MAX_MAJOR_CIVS]` (`CvMinorCivAI.h:900`, a class of about 150 B
  with two `std::map`s, not sized by the script); `CvCityEspionage` 19 members through the `Firaxis::Array<...,
  MAX_MAJOR_CIVS>` typedefs of `CvEspionageClasses.h:437-449`, 1,474 -> 2,881 B **per city** (`:516-538`);
  `CvPlayerCulture` 264 -> 516 B; `CvGrandStrategyAI` 176 -> 344 B; `CvMilitaryAI` 88 -> 172 B; `CvTeam` 20 arrays
  3,296 -> 3,464 B (17 are `Firaxis::Array<..., REALLY_MAX_TEAMS>`, 80 entries, `CvTeam.h:537-596`; two per-major
  vassal arrays :599-600). Game singletons: `CvGameTrade::m_aaiTechDifference`, `m_aaiPolicyDifference`
  `[MAX_MAJOR_CIVS][MAX_MAJOR_CIVS]` (`CvTradeClasses.h:267-268`) and `CvGameDeals::m_aaiRenewDeal` (same shape,
  `CvDealClasses.h:363`), 1,936 -> 7,396 B each. Fixed by `MAX_PLAYERS`/`MAX_TEAMS` and so unchanged by the switch:
  `CvEnumMap<PlayerTypes|TeamTypes, T>` (fixed count `MAX_PLAYERS`/`MAX_TEAMS`, `CvEnumsUtil.h:372-389`; the player
  objects themselves, `CvPlayerAI.cpp:44`, 11 maps in `CvGame.h`), `CvPlayerBitfield` = `CvFixedBitfield<MAX_PLAYERS>`
  (`CvBitfield.h:213`, used by `CvPlot::m_bfRevealed`, `CvPlot.h:878`), the pregame's 38 `std::vector`s of
  `MAX_PLAYERS` (`CvPreGame.cpp:183-296`), and the team objects (`FNEW(CvTeam[REALLY_MAX_TEAMS])`, `CvTeam.cpp:58`: 80
  teams are allocated although only 64 are used). Two arrays are dimensioned by `MAX_MINOR_CIVS`:
  `CvDiplomacyAI::m_aabSentAttackMessageToMinorCivProtector[MAX_MAJOR_CIVS][MAX_MINOR_CIVS]` and
  `m_abWantToRouteToMinor[MAX_MINOR_CIVS]` (`CvDiplomacyAI.h:1925`, `:1937`), indexed by `eMinor - MAX_MAJOR_CIVS`
  (`CvDiplomacyAI.cpp:2946-2953`, `:3805-3811`).
- **Sized containers (87).** `std::vector(MAX_PLAYERS)`, `resize`, `assign`, `FNEW(...[N])`: mostly the pregame
  vectors and per-call scratch vectors; by `MAX_MAJOR_CIVS`: `CvPlayer::m_aiWarWeariness` (`CvPlayer.cpp:1889`),
  `CvGoodyHuts` (`CvGoodyHuts.cpp:73`), the known-players table (`CvPreGame.cpp:3898`), scratch vectors
  (`CvPlayer.cpp:3835`, `CvUnit.cpp:12090`, `CvEspionageClasses.cpp:10527`). By `REALLY_MAX_PLAYERS` (80):
  `CvCity` per-player vectors (`m_aiNumTimesAttackedThisTurn`, `m_aiNumTimesOwned`, `m_abEverLiberated`,
  `m_aiNumTimesCultureBombed`, `m_abTraded`, `CvCity.cpp:1329-1567`) and `CvUnit::m_aiNumTimesAttackedThisTurn`
  (`CvUnit.cpp:1683`).
- **Loops (1,221 with the constant as the bound; 104 more start at `MAX_MAJOR_CIVS`).** `for (i = 0; i <
  MAX_MAJOR_CIVS; ...)` over majors, `< MAX_CIV_PLAYERS` over every civ, `< MAX_PLAYERS` with the barbarian. Nearly every
  loop tests `isAlive()` (or met, or at war) in its first statement and skips dead slots, so the work per turn follows
  the live players, not the constant [C for the loops read, I for all].
- **Minors follow majors (104 loop starts, 10 index offsets, and the comparisons).** City states are found by index:
  `for (i = MAX_MAJOR_CIVS; i < MAX_CIV_PLAYERS; ...)` (for example `CvGame.cpp:4229`, `CvTeam.cpp:849`,
  `CvLuaPlayer.cpp:9143`, `CvPreGame.cpp:2606`), `ownerID >= MAX_MAJOR_CIVS && ownerID != BARBARIAN_PLAYER` as "is a
  city state" (`CvPlot.cpp:2358`, `:6672`, `:6878`), `eMinor - MAX_MAJOR_CIVS` as an index (above), scenario city
  states placed at `i + MAX_MAJOR_CIVS` (`CvWorldBuilderMapLoader.cpp:392`, `:583`, `:1014`). With the constant moving
  these all move together; they also mean the split is compiled in: a build cannot seat 30 majors and 33 city states
  without a different M.
- **Range checks and comparisons (761 + 346).** `PRECONDITION(ePlayer < MAX_MAJOR_CIVS)` and friends guard the
  per-major arrays (543 for majors alone). The network message handlers reject any player id `>= MAX_PLAYERS`
  (`CvDllNetMessageExt.cpp:10-13`, `CvDllNetMessageHandler.cpp:17-20`) and `CvDllGameContext::GetTeam` any team `>=
  MAX_TEAMS` (`CvDllContext.cpp:335-340`); `CvPlayerAI::getPlayer` maps an out-of-range id to the barbarian
  (`CvPlayerAI.cpp:50-53`), `CvTeam::getTeam` to the barbarian team (`CvTeam.cpp:47-51`).
- **Fixed slots and sentinels.** `BARBARIAN_PLAYER`/`BARBARIAN_TEAM` (slot 63) in 125 places; `OBSERVER_TEAM` (M - 1)
  once, to give a closed slot an observer team (`CvGame.cpp:666`); `(PlayerTypes)MAX_MAJOR_CIVS` used as "unknown
  player" in the espionage notification (`CvEspionageClasses.cpp:2066`, comment "hack"): it is the first city-state slot
  in both builds.
- **Masks and integer types that cap the count.** `KnownPlayersBitArray` (`uint32`/`uint64` by M, `#error` above 64
  majors; 1.1). `CvPlayer::ConqueredByBoolField` is an array of 32-bit words sized from `MAX_PLAYERS`
  (`CvPlayer.h:2990-3003`): scales. Player ids stored as `char` in `CvPlot` (`m_eOwner` :870,
  `m_ePlayerThatClearedBarbCampHere` :938, `m_aiRevealedOwner` :892): a signed char holds up to 127; team visibility
  counts are `uint8` per team per plot (`:889-890`), sized at run time by `MAX_TEAMS` [C]. No `static_assert` on any
  player constant, no player bit shift into a 32- or 64-bit integer other than the known-players table [C: listing and
  `grep` for shifts by player or team ids].
- **Literals:** none (1.1).

### 2.2 Memory and time that scale with the constants (per-major, per-major-squared structures)

[I: computed from the declarations in `arrays.tsv`, x64 sizes (int 4, bool and char 1, pointer 8), not measured; one
instance of each per-player class per player slot, 64 slots, because `CvPlayer`'s constructor allocates them for every
slot (`CvPlayer.cpp:766-794`, `CvPlayerAI.cpp:44`).]

| Holder | Instances | Bytes each, 22 build | Bytes each, 43 build | Total 22 | Total 43 |
|---|---|---|---|---|---|
| `CvDiplomacyAI` fixed arrays | 64 | 22,450 | 46,348 | 1.44 MB | 2.97 MB |
| `CvMinorCivAI` fixed arrays (+ unit gifts, about 150 B x M) | 64 | 1,717 (+3,300) | 2,935 (+6,450) | 0.32 MB | 0.60 MB |
| `CvPlayerCulture`, `CvGrandStrategyAI`, `CvMilitaryAI` | 64 each | 528 | 1,032 | 0.03 MB | 0.07 MB |
| `CvTeam` | 80 | 3,296 | 3,464 | 0.26 MB | 0.28 MB |
| `CvGameTrade` + `CvGameDeals` (M x M) | 1 | 5,808 | 22,188 | 0.01 MB | 0.02 MB |
| `CvCityEspionage` | per city | 1,474 | 2,881 | 1.2 MB at 800 cities | 2.3 MB at 800 cities |

So the 43 build costs about 2 to 3 MB more DLL memory for the same game [I], against a process that holds
gigabytes in a large game: memory is not a reason against the 43 build. What grows with majors squared: per player
`CvDiplomacyAI`'s two `[M][M]` co-op-war arrays (2,420 -> 9,245 B each player, 64 players) and the three game-level
`[M][M]` tables; over the whole game that is 64 x M^2 + 3 x M^2 entries, still under 1 MB at 43. The real memory of a
43-civilisation game is in what 43 live civilisations own (cities, units, plots revealed, AI caches), which no constant
sets; Part B measures it per turn (epic brief item 6).

**Time [C for the loop shapes, I for the cost].** Loops over `MAX_MAJOR_CIVS` cost the slot count only in their skip
test; the work follows the live majors. The per-turn shapes that grow with live majors squared: every AI player's
diplomacy turn (`CvPlayer.cpp:10289-10300` -> `CvDiplomacyAI::DoTurn`, `CvDiplomacyAI.cpp:9281`) updates its opinion of
every live met major (`DoUpdateOpinions`, `:14057-14066`), its approaches and its co-op-war states
(`DoUpdateCoopWarStates`, `:9537`, a major x major loop inside a per-player call, so M^3 over the game) and victory
block levels (`:10568`). `nested.tsv` lists 53 functions with a player loop inside a player loop (49 at depth 2, 4 at
depth 3: `CvGame::updateScore` teams x teams x players, `CvTeam::DoDeclareWar` and `CvTeam::SetHasDefensivePact`
majors x majors x majors, `CvTeam::DoBecomeVassal`); the depth-3 ones in `CvTeam` run on events (war, pact, vassal),
not every turn. Going from 22 to 43 live majors multiplies the pairwise work by (43/22)^2 = 3.8 and the per-pair third
party work by (43/22)^3 = 7.5 [I]. Loops over `MAX_CIV_PLAYERS`, `MAX_PLAYERS` and `MAX_TEAMS` have the same bound in
both builds (the slot total does not change) and grow only with the number of live players.

### 2.3 What raising the DLL's limits would take (beyond 43 majors; beyond 64 players)

**More majors inside 63 civ slots (44..62) [C unless marked].** The DLL is written for any M from 2 to 62: every
per-major array, loop and offset follows `MAX_MAJOR_CIVS`; the `#if` blocks of 1.1 already cover more than 22; the
known-players mask stays `uint64` up to 64. What changes with M is the city-state room: `MAX_MINOR_CIVS` = 63 - M, so
44 majors leave 19 city states and 62 leave 1. M = 63 would make `MAX_MINOR_CIVS` 0 and the two arrays of
`CvDiplomacyAI.h:1925` and `:1937` zero-length, which C++ does not allow (MSVC error C2466) [I: language rule, not
compiled]: that is why the comment says 62. Other DLL-side limits that bite before the constants do: the World Builder
map format carries at most 32 majors and 64 city states (`CvWorldBuilderMap.h:33-37`), so a scenario map cannot place
more than 32 majors whatever M is (a plain map with a map script, such as the Hormigas pack's, is not limited by
this); the scenario loader clamps the map's city states to `MAX_MINOR_CIVS` (`CvWorldBuilderMapLoader.cpp:381`, `:580`,
`:1011`). `OBSERVER_TEAM` is the last major team (M - 1): with every major slot filled there is no slot left for the
autoplay observer (`research/08-performance/giant-stress-game.md` item 3, measured there at 22). The database needs at
least M playable civilisations to fill the slots (44 in the pinned VP database, 1.2): beyond 44 only with custom
civilisation mods [C for the count]. The front end and the EXE's own player slots for M > 22 are sections 3.1-3.3; the
fact that upstream ships a 43 build that plays (VP's 43-civ users) says the EXE side holds at least to 43 [I].

**More than 64 players in all (raising `MAX_CIV_PLAYERS` above 63) [C for the DLL's code, I for the outcome].** In the
DLL itself the constants are consistent and would follow a larger value: `MAX_PLAYERS`, `MAX_TEAMS`, the barbarian
slot, the enum maps, bitfields and pregame vectors are all derived; player ids in `CvPlot` are `char` (to 127); the
known-players mask depends on majors only. The DLL's own ceiling is `REALLY_MAX_PLAYERS`/`REALLY_MAX_TEAMS` = 80
(`CvDLLUtilDefines.h:20-22`, "do not change this or have MAX_PLAYERS exceed it - this will require the engine to be
rebuilt"): `CvCity` and `CvUnit` per-player vectors and `CvTeam`'s per-team arrays and allocation already use 80
(2.1), so up to 80 players nothing in the DLL needs resizing beyond the one define. The comment, written by Firaxis,
says the limit is the engine's, that is the EXE's. The DLL-to-EXE interface fixes the count implicitly: it has no call
that tells the EXE how many players or teams exist (`CvGameCoreDLLUtil/include/CvDllInterfaces.h`, searched for
player-count methods: none), so the EXE drives its per-player work (pregame slots through `CvPreGame`, network
messages, the save header, the UI) with constants compiled into it, and the DLL rejects any id `>= MAX_PLAYERS` in
network messages (2.1). The DLL tells the database and Lua (`Defines` rows, 1.1), which only the Lua reads. So the
question for more than 64 players is whether the EXE's slots go to 80 and where it has 64 compiled in: sections 3.1
and 3.3 (A2, 4257); the save's fixed-size per-player data is 5.1 (A3, 4258). The scenario map format's 32 + 64 limit
(above) stays in any case.

## 3. The EXE (worker A2, 4257; from the knowledge base and the EXE's described behaviour, D83)

Written 2026-11-15 07:05-07:35 EST by worker A2 (issue 4257, account B). Method: the knowledge base first, then
read-only headless Ghidra 12.1.4 passes on a scratch copy of project `civ5` (the Steam DX11 EXE, SHA-256 `b2ce96ae...`,
TimeDateStamp 0x546CD0A8; `C:/c5-64bit-work/0334/a2`, deleted at hand-back), a byte-pattern scan of the EXE copy for its
Lua registrations, and source reads of the fork clone at `04b1211b8`. Behaviour is described in our own words; no
decompiler output and nothing from the EXE or the game's files is committed (D83). Scripts in
`evidence/0334/a2/` (`gh.sh` + `Lim.java` run the passes, `luareg.py` lists the EXE's Lua C functions by name,
`vtoff.py` turns `ICvPreGame1` vtable offsets into method names, `lua_scan.py` counts the UI Lua's player loops).
VAs are DX11 at ImageBase 0x400000. PreGame calls below are named from `vtoff.py` over
`CvGameCoreDLLUtil/include/CvDllInterfaces.h` at `04b1211b8` (`slotStatus` +0x2FC agrees with the KB's 1304 decompile).

**The short answer.** The EXE was compiled against the same `CvDLLUtilDefines.h` as the stock DLL: 64 players
(`MAX_PLAYERS`), 63 civilisation players, 22 majors, 41 minors. It keeps almost no player data of its own (pre-game,
colours, civilisations and leaders all live in the DLL and are read through `ICvPreGame1` per frame), but its loops and
a few fixed tables carry the three numbers as immediates in `.text` or as fixed `.data` sizes. **64 is the hard limit:**
the EXE's per-slot table, its per-frame slot poll, its network session and its save loop stop at 64, and every one is
code or a fixed-size static, so no data write can move it (section 3.3). **22 is a soft limit:** the EXE treats slots
0-21 as the majors in six places, none of which stops a 43-major single-player game; they cost the network-join seat
search, the 3D leader screens, one per-turn summary record and some set-up defaults for majors 22-42. **63** (civ
players; the barbarian in slot 63) is fixed with 64.

### 3.1 The EXE's own player slots and arrays (pre-game, player colours, network messages, the save header)

**Pre-game slots are the DLL's, not the EXE's [C].** Every pre-game array (slot status and claim, civilisation,
leader, colour, team, handicap, minor-civ type, nicknames, ...) is a `std::vector` of `MAX_PLAYERS` in the DLL
(`CvPreGame.cpp:183-296`; every accessor checks `p >= 0 && p < MAX_PLAYERS`). The EXE's `PreGame.*` Lua functions
pass the Lua index straight through to `ICvPreGame1` with no range check of their own (`GetSlotStatus` 0x7A6F60 ->
+0x2FC, `SetSlotStatus` 0x4E5240 -> +0x2C4, `SetNumMinorCivs` 0x738480 -> +0x284, `ResetSlots` 0x76FAD0 -> +0x180;
registration table 0x51A007-0x51AA78) [C]. Of the 766 Lua C functions the EXE registers by name (`luareg.py`), only
three compare an argument with 63/64: `GetPlayerList` 0x5BA200 (loops to 63), `TestLeaderMessage` 0x500350 (both
player arguments < 64) and `BroadcastPlayerInfo` 0x435140 (< 63); none compares with 21/22, 41-43 [C, `Lim.java g:`,
CMP immediates only; checks in callees not counted].

**Where the EXE fixes the slot count itself:**

| EXE site | What it does with the player count | Bound | Mark |
|---|---|---|---|
| 0x6A1DC0 per-frame slot poll | walks slots 0-63 every frame (KB `interfaces-and-traffic.md` section 5, `tracker/issues/1304` Finding 2); the loop ends on the address 0x2DE0440, the end of the per-slot table below, not on a counter | 64 | C |
| 0x2DDFE40, 64 x 0x18 B (.data) | per-slot table stepped by the slot poll and reset by 0x51AFD0 (`game-core-thread.md` section 2 calls it "per-leader"; it is indexed by slot). 14 other functions reference it (0x44EBC0, 0x492890, 0x506C50, 0x575E90, 0x5B9740, 0x658DF0, 0x660D40, 0x6636D0, 0x6A0880, 0x6DE690, 0x7036A0, 0x75D540, 0x75DC20, 0x7B1710) | 64, fixed size | C |
| 0x50B090 random set-up fill (`"Choosing Civ"`, `"Choosing Leader"`, `"Choosing Minor Civ"`) | at game launch (called from 0x770820, which also loads the map, and from the network launch 0x58CBC0): slots 0-62 with civilisation or leader -1 get a random one (duplicates checked over 0-62); then minor-civ types for slots **22 .. 22 + `numMinorCivs` - 1** whose type is -1 (start index set at 0x50B71D, compare at 0x50B72A) | 63, and minors from **22** | C |
| 0x5DC5D0 (from the random-world-size path 0x6CDEC0, "Choosing world size at random") | does nothing if any slot 0-62 is `SS_COMPUTER`; otherwise counts the human or computer slots over 0-62, turns `SS_CLOSED` slots into computer (single player) or open (hot seat, pitboss, e-mail) until the world's default player count is reached, then sets every remaining slot up to **21** to `SS_OBSERVER` | 63, majors to **21** | C |
| 0x475C40 (launch, before 0x50B090) | a pass over slots 0-62 | 63 | C |
| 0x418C30 (from 0x6B2C70) | builds one record per major for slots **0-21** (0x84 B each: a name of up to 128 B and two shorts), counts over 64 teams, and posts the whole as one 0x900-byte event (type 0xA8) on the game-to-UI queue; it also reads the `YIELD_SCIENCE`/`YIELD_GOLD`/`YIELD_PRODUCTION` infos. What consumes the event is not traced [I: a per-turn status or statistics summary] | majors **0-21**, teams 64 | C (purpose I) |
| 0x4223C0 | writes default 22 into a field named `MaxPlayerCount` and 41 into `MaxCityStateCount` of a map or map-script description object (no direct caller: a vtable method) [I: the map browser's limits for maps that do not state them] | 22 / 41 | C (use I) |
| save body | the EXE writes and reads Game, Map, teams 0-63, players 0-63, supporting data (`research/sources/civ5-headless/saves.md` section 2 item 4; the headless host reproduces the sequence and cross-loads with the real game) | 64 | C |

**Player colours [C].** The EXE has no per-player colour table: colours are the DLL's (`s_playerColors`, `MAX_PLAYERS`)
and reach the renderer through `ICvPlayer`/pre-game getters (`playerColor` is one of the 24 getters the proxy mirrors,
`proxy-design.md` 16.1). The EXE's own colour reads are of the `PlayerColors` table by ID for the save header
(0x6C2960 writes, 0x77AF90 reads: the active player's colour type only) and the `CIV5PlayerColors.xml` load list
(0x46A240). The number of colours is the database's, not a constant of the EXE.

**Network [C unless marked].** The EXE's network layer (`Civ5NetMessage.cpp`) has its own message classes with a
player id: `NetPlayerInfo` (0x59B680 formats it: id, slot status and claim, team, colour as 32-bit fields of the
message object; ready and turn-notify flags as bytes), `NetChangePlayerId`, `NetSetPlayerDesiredSlot`,
`NetSlotReassignment`, `NetPlayerReady` (0x6CEE60). The wire width of the id was not traced [I: the in-memory fields are
32-bit]. Limits found:
- **Join seat search, slots 0-21 only.** 0x76B5D0 (called from the connection-attempt callback 0x6FE2D0) picks the
  slot for a joining human: it scores each slot by a nickname match with the joining player, its status (open,
  observer, computer, taken) and its claim, and returns the best, over **slots 0-21** in both of its loops (bounds at
  0x76B798 and 0x76BA2C). A network
  player can therefore only be seated in slots 0-21, whatever the DLL's `MAX_MAJOR_CIVS`.
- **63-player cap on a session.** 0x6FE2D0 accepts a connection only while the session's player count (0x533DB0) is
  below 63; 0x6C3D40 (the per-slot test of the seat search) treats any slot above 63 as unavailable.
- **64 per-player flags** in the session object at +0x18C (0x6DA0F0, the drop handler, checks `player < 64` before
  reading them).
- In-world object keys (0x7AEB10) pack the owner into the top 8 bits of a 32-bit key (`id & 0xFFFFFF | player << 24`),
  after a `player < 64` check: wide enough for 256, so not a limit of its own.

**Save header [C].** The header the EXE writes (0x6C2960) and reads (0x77AF90) holds the active player's civilisation,
handicap, era, leader and civilisation names and colour, the game settings, DLC and mods (`saves.md` section 2 item 1).
It has **no per-slot list and no player count**; the pre-game block that follows is written by the DLL
(`ICvPreGame1::write`), and the body loops over 64 teams and 64 players (table above). So a save's header carries
nothing that depends on `MAX_MAJOR_CIVS`; whether the DLL's blocks do is section 5.

### 3.2 Lua UI loops and the leader screens; what reads `GameDefines.MAX_*` and what has its own constant

**Where `GameDefines.MAX_*` comes from [C].** The EXE builds the Lua table `GameDefines` from the database's `Defines`
table (registration 0x711C80: `GameInfo` and `GameDefines` accessors; 0x72EB00 clears them). The player constants are
not in the EXE (no `MAX_MAJOR_CIVS` string in it): the DLL writes them into `Defines` while it caches the database
(`CvDllDatabaseUtility.cpp:262-278` at `04b1211b8`: `MAX_CIV_PLAYERS`, `MAX_CIV_TEAMS`, `MAX_MAJOR_CIVS`,
`MAX_MINOR_CIVS`, `MAX_PLAYERS`, `MAX_TEAMS`, `BARBARIAN_PLAYER`, `BARBARIAN_TEAM`, `INSERT OR REPLACE`). So every Lua
loop written against `GameDefines.MAX_*` follows the DLL's build: in the 43 build it sees 43 / 20 / 63 / 64.

**Counts [C, `lua_scan.py` 2026-11-15; the Goldberg install `%USERPROFILE%/Desktop/pure_vp` (VP 5.4.7 modpack), read
only; the fork's Lua by `git grep` at `04b1211b8`]:**

| Lua set | .lua files | files using player constants | uses of `MAX_MAJOR_CIVS` / `MAX_CIV_PLAYERS` / other player constants |
|---|---|---|---|
| stock `Assets/UI` | 173 | 30 | 55 / 15 / 8 (`MAX_PLAYERS` 4, `MAX_CIV_TEAMS` 3, `BARBARIAN_PLAYER` 1) |
| stock `DLC/Expansion/UI` | 48 | 16 | 15 / 14 / 6 |
| stock `DLC/Expansion2/UI` | 73 | 23 | 34 / 21 / 6 |
| `DLC/UI_bc1` (EUI) | 42 | 9 | 14 / 9 / 0 |
| VP modpack (`DLC/MP_AUTOPLAY_VP_5_4_7`) | 242 | 49 | 104 / 50 / 13 |
| fork Lua, all (`04b1211b8`) | - | 65 | 144 / 62 / 12 (`MAX_PLAYERS` 5, `MAX_MINOR_CIVS` 3, `MAX_CIV_TEAMS` 3, `BARBARIAN_PLAYER` 1) |

(The scan also counted `MAX_HIT_POINTS` and `MAX_CITY_HIT_POINTS`, which are not player constants; they are left out
above.) Loops and checks with **their own literal** instead of a constant (all instances found):
- **The leader-screen fallback, `iPlayer > 21`**: Community Patch `LUA/LeaderHeadRoot.lua:94`,
  `Core Files/Overrides/DiscussionDialog.lua:111`, `Core Files/Overrides/Includes/TradeLogic.lua:224`, Vox Populi
  `Core Files/Overrides/DiscussionDialog.lua:111`, EUI compatibility `LUA/LeaderHeadRoot.lua:121` and
  `LUA/TradeLogic.lua:238`: for a player id above 21 the screen prints "Player ID > 21" and shows a full-screen static
  texture (a per-leader `.dds`, else `loadingbasegame_9.dds`) instead of the 3D leader scene. In the fork since at least
  2021-02-26 (`4706558c4`; `git log -S`). Community reports: 22 animated leader heads, every leader above slot 22
  static [I, public forum/Steam comments, not checked in the EXE].
- **Stock `AdvancedSetup.lua:1089, 1097`**: the city-state slider's maximum is `math.min(41, #MinorCivilizations)`. The
  43-civ mod `(3b) 43 Civs Community Patch/AdvancedSetup.lua` differs from the stock file in exactly these two lines
  (`math.min(GameDefines.MAX_CIV_PLAYERS - GameDefines.MAX_MAJOR_CIVS, ...)`); it is the mod's only Lua.
- **Vox Populi `VictoryProgress.lua:339`**: loops city states from `MAX_MAJOR_CIVS` to `MAX_MINOR_CIVS - 1` (22-40 in
  the 22 build, so slots 41-62 are missed; empty in the 43 build, 43..19). A VP bug in both builds (the bound should be
  `MAX_CIV_PLAYERS - 1`); it only affects a count shown on the victory screen [C source; effect I]. Upstream draft:
  `upstream-drafts-3505.md` section F2 (2026-11-24, still present on upstream master b0bce8c2f, not sent).
- **Vox Populi `AssignStartingPlots.lua:1484`**: `iNumCivs <= 22` gates the empty-region tables (22 entries each);
  with more civilisations no empty regions are added. Design, not a fault.
- Not a player bound: `UnitPanel.lua` `civPortraitIndex > 21` (CP `LUA/UnitPanel.lua:624`, VP `LUA/UnitPanel.lua:648`,
  EUI `UnitPanel.lua:1258/1429`, stock `UnitPanel.lua:585-618`) is an icon-atlas index; `LeagueOverview.lua`
  `math.max(64, ...)` is a size.

**The leader screens.** What the EXE does: an AI leader's message reaches `GameplayDiplomacyAILeaderMessage` (0x51B1E0,
`game-core-thread.md`), which holds the game update while the screen is up; leader scenes are loaded from their scene
files by name and cached (loaders 0x41C960, 0x56CE80, 0x473E0B, callers 0x5985B0, 0x634200; current scene pointer
0xDA8F8C) [C]. The EXE keeps the string "NO LEADER SCENE LOADED FOR PLAYER ID: %u. Somebody forgot to send the
NotifyAILeaderInGame event." with no code reference (a stripped debug path) and registers the Lua event
`NotifyAILeaderInGame` (0x6BD8CD). **Where the 22 comes from in the EXE was not found** in this pass (none of the
loaders above compares with 21/22); VP's Lua fallback above is the evidence that ids above 21 get no 3D scene [C source;
EXE cause I]. Follow-up: trace the `NotifyAILeaderInGame` listener (0x440850 builds the event at 0x6BD8D2).
*Found 2026-11-15 (ticket 0355): the EXE's `NotifyAILeadersInGame` (0x694160) loops players 0-21 only; the hold is
dropped by leaving the leader view whatever the scene; see `research/02-exe-internals/leader-scene-player-limit.md`.*

### 3.3 Which EXE limit is hard, and what raising it would take (data writes only, D4; D136 precedent; D65)

| Limit | Where | Can it move without code bytes? | What it costs at 43 majors | Beyond 43 majors (<= 62) | Beyond 64 players |
|---|---|---|---|---|---|
| **64 slots** | slot poll 0x6A1DC0 + its 64 x 0x18 table 0x2DDFE40; network session flags (+0x18C, `< 64`); save body loops; Lua checks `< 64` | **No.** The loop ends are immediates or an address in `.text`, the table a fixed `.data` block with neighbours at 0x2DE0440-0x2DE044C (the update hold among them) | none (43 + 20 + 1 = 64) | none: still 64 | **hard**: slots 64+ would never be polled, saved or loaded by the EXE, and the table would overrun into the update hold |
| **63 civ players / barbarian = 63** | launch passes 0x475C40, 0x50B090, 0x5DC5D0 (0-62); session cap 0x6FE2D0 (< 63); `BroadcastPlayerInfo` | No (immediates) | none | none | hard, with 64 |
| **22 majors: network seating** | 0x76B5D0 (slots 0-21) | No (immediates at 0x76B798, 0x76BA2C) | humans in a network game sit only in 0-21; AI majors 22-42 are unaffected | same | - |
| **22 majors: minor-civ fill from slot 22** | 0x50B090 (start 0x16 at 0x50B71D) | No | the EXE writes random minor-civ types into major slots 22..; VP's DLL overrides the EXE's random picks in `CvPreGame::onGameStarted` (`CvPreGame.cpp:2502-2822`, "Hack to manually override the engine's random selection of civilizations and City-States", since `47eba2237f` 2023-10-08, #10342), which chooses majors over `0..MAX_MAJOR_CIVS-1` and city states over `MAX_MAJOR_CIVS..62` with the DLL's constants [C source]; that it runs before the EXE's fill is [I] (the source comment; order of the two calls not traced) | same | - |
| **22 majors: hot-seat / random world size slot adjust** | 0x5DC5D0 | No | slots 22-42 are not set to observer when the world's default player count is below 43; they keep the set-up screen's status [I effect] | same | - |
| **22 majors: summary record** | 0x418C30 | No (immediate; and the record is a fixed 0x900-byte queue event) | majors 22-42 are missing from that per-turn record [I effect: consumer not traced] | same | - |
| **22 majors: 3D leader screens** | EXE cause not found (3.2) | unknown | VP shows a static picture for leaders 22-42 (its own Lua) | same | - |
| **22 / 41 defaults** | 0x4223C0 `MaxPlayerCount`/`MaxCityStateCount` | No (immediates) | map-description defaults only [I] | same | - |

**Data writes (D136 form).** No data write was found that moves any of these limits: every bound above is an
immediate in `.text` (D4: never) or the size of a static block whose end is itself encoded in code. There is no EXE
variable holding "number of majors" or "number of players" to write. So there is **nothing to put to the owner in D136's
form**; the Steam check (D65) is untouched by all of this. The 22-based behaviours can only be worked around from the
DLL or the Lua side (as VP already does for the minor-civ fill and the leader screens), and the network seat search
only by seating humans in slots 0-21.

**Costs and conclusion for section 6.** Up to 62 majors (63 civ players, the DLL comment's maximum in
`CustomModsGlobal.h:8-12`) the EXE limits stay as at 43: nothing new breaks in the EXE, the 22-based degradations
above apply to every major from 22 on, and the 63/64 limits are untouched. **More than 64 players in total is not
possible with this EXE** without code changes (slot poll, its table, the network session, the save loops), which D4
forbids; that is the hard limit. It would need the EXE's role in those paths replaced (for the slot poll and saves the
proxy and the server already stand between the EXE and the DLL, section 4), which is an architecture question for
the lead's section 6, not a data write.

What is not verified: the wire width of the network player id; the consumer of 0x418C30's event; the EXE site of the
22-leader limit; the order of VP's `onGameStarted` and the EXE's 0x50B090 fill (a live 43-major start would show it:
Part B). Nothing here was run live.

## 4. The proxy's interface model and the server (worker A3, 4258)

Written 2026-11-15 07:05-07:25 EST by worker A3 (issue 4258), research only: no build, no game. This repository at main
975a36c9 (line numbers below; the epic branch f409f061 changed no code), the fork clone at x64 pin `04b1211b8`.
Search command (code sites; `research/`, `tracker/`, `docs/` and every `evidence/` folder left out):
`rg -n "MAX_PLAYERS|MAX_CIV_PLAYERS|MAX_MAJOR_CIVS|MAX_MINOR_CIVS|MAX_TEAMS|MAX_CIV_TEAMS" proxy server shell tools`,
then `for \w+ ?= ?[01], ?(21|22|42|43|61|62|63|64)` and `range\((22|43|63|64)\)` for literals (the 115/141 raw hits
are mostly in evidence folders and documents).

**The one fact that decides this section** [C, `CvDLLUtilDefines.h:24-36` at `04b1211b8`]: the 43 build changes
`MAX_MAJOR_CIVS` (22 -> 43), `MAX_MINOR_CIVS` (41 -> 20) and `OBSERVER_TEAM` (21 -> 42) only. `MAX_CIV_PLAYERS` (63),
`MAX_CIV_TEAMS` (63), `MAX_PLAYERS` (64), `MAX_TEAMS` (64), `BARBARIAN_PLAYER`/`BARBARIAN_TEAM` (63) and
`REALLY_MAX_PLAYERS` (80) stay. So every site that uses only the totals is right for both builds unchanged.

### 4.1 Every site in `proxy/` and `server/` that compiles or assumes a player constant

Where the value comes from. All three builds compile against the fork tree given as `--src` (checked against
`deps/dll.lock` by `tools/x64-dll/build.py` `resolve_src`, lines 224-262) with `CvGameCoreDLLUtil\include` on the
include path: the DLL `tools/x64-dll/build.py:48-57` (`INCLUDE_DIRS`, used at :438), the server
`server/build_server.py:353` (`/external:I` over the same `INCLUDE_DIRS`), the proxy `proxy/build_proxy.py:372` over
`proxy/gen/toolchain.py:116-118` (`INCLUDES`). `CvDLLUtilDefines.h:9` includes `CustomModsGlobal.h`, so **the
upstream switch (uncommenting `#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)` in `CustomModsGlobal.h:11`, which upstream's
`scripts/release.py:408-426` does as a text edit) reaches the proxy and the server automatically when they are built
from the variant's source** [C]. A `/D` switch instead would not: the proxy takes its defines from `toolchain.py:119`
(`DEFINES`), the server from `x64build.PREDEFS['debug']` (`build_server.py:350`), the DLL from `PREDEFS[config]`
(`build.py:58-73`), three lists [C]. The lock: a variant source (a branch commit, or the pin with that line edited)
fails `resolve_src` unless `--unlocked` or a second lock is used; this binds all three builds if they all take the
variant's source, only the DLL's build otherwise (4.2) [C].

Code sites (every site; `right` = right with 43 majors unchanged):

| Site (main 975a36c9) | Uses | Value from | Right with 43? |
|---|---|---|---|
| `proxy/src/proxy_update.cpp:138` (log), `:174` (test slot poll loop) | `MAX_PLAYERS` | fork header, compiled | right (64 both) [C] |
| `proxy/src/proxy_update.cpp:231, :240` (Lua test leg `for i = 0, 63`); `:227` is a plot loop | literal 63 | literal = `MAX_PLAYERS - 1` | right [C] |
| `server/src/srv_main.cpp:416` (`setPlayable` every civ slot) | `MAX_CIV_PLAYERS` | compiled | right [C] |
| `server/src/srv_main.cpp:431` (`slot<i>_civ` / `slot<i>_team` from the config), `:471` (slot status: 0 human, `< majors` computer, else closed), `:525` (leader choice), `:536` (city-state report from slot `MAX_MAJOR_CIVS`) | `MAX_MAJOR_CIVS` | compiled | **only if built with the variant's constant.** A server built at 22 against the 43 DLL leaves slots 22-42 untouched (`resetPlayers` defaults), so a spec with more than 22 majors cannot be played, and `:536` labels slots 22-42 city states [C code, I effect]. **Corrected 2026-11-15 08:11 (lead, 4253 merged): the four sites now read `SrvMajorCivs()` at run time, so they are right with both DLLs; see 8.2.** |
| `server/src/srv_main.cpp:557, :577` (`LaterInit`, `SetupGraphical`) | `MAX_PLAYERS` | compiled | right [C] |
| `server/src/srv_main.cpp:815` (summary Lua) | `GameDefines.MAX_CIV_PLAYERS` | database, run time | right [C] |
| `server/src/srv_ifaces.cpp:147` (`PlayerSlotTaken` bound) | `MAX_PLAYERS` | compiled | right [C] |
| `server/src/srv_save.cpp:190, :196` (load), `:269, :275` (save): teams and players 0-63; comment `:5` | `MAX_TEAMS`, `MAX_PLAYERS` | compiled | right [C] |
| `server/tools/probes/pledge-declaration.lua:15, :18`; `proxy/tools/live/probe.lua:17`, `eq_stats.lua:66`; `tools/harness/checklist.py:253-301` (Lua), `tools/harness/diplo.py:382`; `tools/stressmap/ptwe.py:127-166, :200, :308-311, :346`, `calibrate.py:50`; `tools/i0b/run_1900.py:295`; `tools/i0/counterspies.lua:7`; `tools/humanplay/lua_chunks.py:79, :384` | `GameDefines.MAX_*` | the database's `Defines`, run time | right [C]: the DLL writes its compiled values into `Defines` (`CvDllDatabaseUtility.cpp:267-274`, `InsertGameDefine`), and in remote mode the server's database writes are replayed on the EXE's database (`server/src/remote/srv_remote_db.cpp:1-28`), so the EXE's Lua also reads 43 [C code, I live] |
| `tools/drift/instrument_huge.py:68` | `MAX_MAJOR_CIVS` | C++ patched into the DLL source | right (compiled with the DLL) [C] |
| `tools/d10/savecmp.py:71-72` (`MAX_PLAYERS = 64`, `MAX_CIV_PLAYERS = 63`), `:298, :314-318, :382` | Python literals | copies | right: both unchanged (section 5.2) [C] |
| `tools/stressmap/ptwe.py:19-25` (docstring "`MAX_MAJOR_CIVS` = 22"), `tools/stressmap/mappack.py:13, :38` (21 majors + observer, `DEFAULT_CIVS`) | prose, a default list | the 22-major plan | the code reads `GameDefines` and is right; the plan and the default list are the 22-build's (the 43 build allows 42 AI majors + the observer). Not a bug |
| `tools/harness/tests/test_diplo_rearm.py:64` (`MAX_MAJOR_CIVS = 22` in a mock), `test_checklist_lua.py:127`, `test_diplo.py:198` (text of a guard) | test fixtures | - | right (they test the code's form, not the value) |
| `proxy/tools/dbfp_unit.cpp:44, :254` | the string `"MAX_CIV_PLAYERS"` as a `Defines` row name | unit-test data | not a limit |
| `proxy/model/model.json:138378`, `proxy/model/lua.json:586`, `proxy/gen/extract_lua.py:88, :102` | "pre-pushed for all MAX_PLAYERS" | description text | not a limit |

Nothing in `shell/` (code), `tools/x64-dll/`, `tools/harness/` (C++/Python logic), `proxy/gen/` (generator logic) or
`server/src/remote/` uses a player constant or a literal player count [C, the searches above]. The proxy's per-player
state is keyed, not sized: canonical handles and the getter mirror are maps keyed by (code, index) or handle
(`proxy/src/proxy_local.cpp:280-288, :470-500`; `server/src/remote/srv_remote_objects.cpp:540-575`) [C].

**Result**: in the product path (remote mode) neither the proxy nor the server has a site whose value differs between
the 22 and the 43 build. The four `MAX_MAJOR_CIVS` sites are in the server's headless host (`srv_main.cpp` phases,
used by `run_server.py`, the D10 test and the harness), not in remote mode [C].

### 4.2 Built against the constants or read at run time: which is cleaner, and why

**Build time** (proxy and server built per variant from the variant's source):
- Cost: two proxy builds and two server builds per release; both must pass `resolve_src` with the variant's source
  (a second lock or `--unlocked`, 4.1); two of every proxy/server test run; the release driver selects and packs per
  variant; the server and proxy build ids (`build_info`, issue 2704) name different DLL sources. The proxy binary
  would come out identical in code (it uses no changed constant), so its second build buys nothing [C 4.1].
- Gain: none in hot code: no hot loop uses a changed constant. `proxy_update.cpp:174` is `MAX_PLAYERS` (64 in both)
  and runs only under the headless test setting `test_slot_poll` (`proxy_update.cpp:43-60`) [C].

**Run time** (one proxy, one server.exe for both DLLs):
- The value exists at run time without a new interface: the DLL inserts `MAX_MAJOR_CIVS`, `MAX_MINOR_CIVS`,
  `MAX_CIV_PLAYERS`, `MAX_PLAYERS`, `MAX_TEAMS`, `BARBARIAN_PLAYER` into `Defines` during
  `PerformDatabasePostProcessing` (`CvDllDatabaseUtility.cpp:267-274`) [C]. The server holds that connection
  (`s_pDb`; headless `srv_main.cpp:383`, remote `srv_remote_db.cpp:896-905`). The 32-bit headless host c5h already
  works this way (`civ5-headless/host/setup/HHNewGame.cpp:15-31`, `HHQueryMaxMajorCivs`: `SELECT Value FROM Defines
  WHERE Name = 'MAX_MAJOR_CIVS'`, 22 if missing or out of range) [C].
- Cost: the four `srv_main.cpp` sites take an `int` read once after post-processing (one query at boot, nothing per
  turn). The pack still differs per variant in what is per-DLL anyway: the x64 DLL, its purity list (bound by
  `dll_sha256`, `proxy/src/proxy_purity.cpp:9`) and the manifest's `x86_dlls` (the 32-bit fallback DLL the pack fits,
  `proxy/src/proxy_pack.h:5-15`); those two differences exist under either choice.
- A guard is needed because one binary would meet both DLLs: after post-processing the server compares `Defines`
  `MAX_PLAYERS`, `MAX_TEAMS`, `MAX_CIV_PLAYERS` with its compiled values and fails loudly on a mismatch (a DLL with
  more than 64 players, section 6); and the server and proxy sources stop compiling `MAX_MAJOR_CIVS`/`MAX_MINOR_CIVS`
  at all (an `#undef` plus a poisoning `#define` after the fork includes in `srv_common.h` and the proxy's common
  header), so no later code can bake 22 in silently [I: proposed].

**Recommendation: run time.** One proxy and one server.exe serve both DLLs; the four headless-host sites in
`server/src/srv_main.cpp` (:431, :471, :525, :536) read `Defines.MAX_MAJOR_CIVS` once after post-processing (c5h's
pattern), a boot guard checks the totals that the binaries do compile, and `MAX_MAJOR_CIVS`/`MAX_MINOR_CIVS` are
poisoned in proxy and server code. Reason: the product path already has no site that depends on the split (4.1), the
only dependent sites are four lines of the headless host, and the value is in the database the server already holds;
building per variant would double the proxy and server builds, tests and lock handling for no change in their code.
Only the DLL (and its purity list and parity baseline, 4254) is per variant. [I: recommendation from the source read;
4253 confirms by building the server once and playing a game with more than 22 majors in the headless host.]

### 4.3 The interface traffic: anything in the EXE<->DLL interface whose layout or count depends on the constants

- **Signatures and records: nothing** [C]. No interface header uses a player constant: `git grep` of
  `CvGameCoreDLLUtil/include` at `04b1211b8` finds the constants only in `CvDLLUtilDefines.h` and the commented lines
  of `CustomModsGlobal.h`. The model's 11 records (`proxy/model/model.json` `records`: FDataStream, CvString,
  CvCombatMemberEntry, FMemoryStream, FAutoArchive, IDInfo, CvAStar, UnitMoveRate, FFileTimestamp,
  CvDLLInterfaceIFaceBase, FILogFile) have no array sized by players; the only array field is `CvAStar`'s
  `char[512] m_ScratchBuffer`. Of 3,463 methods, the one fixed-shape array is `ICvTeam1::GetTechs(TechTypes*, uint)`
  (`out_array_i32`, length passed by the caller). Listing:
  `python -I research/03-dll-engine-boundary/evidence/0334/a3/model_player_shapes.py proxy/model/model.json OUT`
  -> `evidence/0334/a3/model-player-shapes.txt`. So the model, the slot proof and the generated code are the same for
  both builds.
- **Counts the EXE loops over**: the EXE's per-frame slot poll walks 64 slots (`proxy/src/proxy_rt_logic.h:197-200`,
  interfaces-and-traffic.md section 5) = `MAX_PLAYERS`, unchanged. Whether the EXE has other per-player counts of
  its own (pre-game, colours, the leader screens) is section 3's.
- **Bytes whose layout depends on the split, carried opaquely** [C]. Every `Read/Write(FDataStream&)` on Game, Player,
  Team, Map, PreGame, NetInitInfo, Deal, and the sync archives (the model's `stream` shape, 60 methods) is written
  and read by the DLL on the server side; the proxy moves the bytes without parsing them (proxy-design.md 11.6). Their
  layout does change with the split (section 5.1; for example `CvPreGame`'s and `CvDllNetInitInfo`'s
  `KnownPlayersBitArray` is `uint32` for at most 32 majors and `uint64` up to 64, `CvPreGame.h:297-304`,
  `CvDllNetInitInfo.h:83-90`), but both ends are the same DLL, so nothing in the proxy changes.
- **The `Defines` rows** reach the EXE through the database-write replay (`srv_remote_db.cpp:1-28`): the EXE's
  `GameDefines.MAX_MAJOR_CIVS` reads 43 with the 43 DLL in remote mode as it does in-process [C code; I live, 4255
  checks the setup screen].
- **Purity** (D104): the list is bound to one x64 DLL's SHA-256, so the 43 DLL needs its own `purity.tsv`. The 43
  switch changes three compiled function bodies under `#if defined(MOD_GLOBAL_MAX_MAJOR_CIVS)`
  (`CvMinorCivAI.cpp:4487`, `CvReligionClasses.cpp:2204`, `CvStartPositioner.cpp:403`) besides every use of the
  constants; 4254's derivation should run on the variant's source rather than reuse the 22 list [I].

## 5. The save format and compatibility with the 22-major build (worker A3, 4258)

Written 2026-11-15 07:05-07:25 EST by worker A3 (issue 4258), source read of the fork at `04b1211b8`; nothing run.
Section 2 lists the DLL's uses of the constants; this section covers only what is written into a save.

### 5.1 What in the DLL's save depends on `MAX_MAJOR_CIVS` / `MAX_MINOR_CIVS` (fixed-size serialised arrays, versions)

Listing (every member declared with the constants, where it is streamed, and every use of the constants inside a
serialise/read/write function):
`python -I research/03-dll-engine-boundary/evidence/0334/a3/serialised_major_minor.py %USERPROFILE%/Documents/GitHub/Community-Patch-DLL 04b1211b8 OUT.tsv`
(read-only `git grep`/`git show`, about 1 minute) -> `evidence/0334/a3/serialised-major-minor.tsv`; bytes per build:
`save_bytes_per_build.py serialised-major-minor.tsv` -> `evidence/0334/a3/save-bytes-per-build.txt`.

How the stream writes them [C]:
- A C array `T a[N]` (`FirePlace/include/FireWorks/FDataStream.h:200-220`), a `tr1::array<T, N>`
  (`CvSerialize.h:512-521` via `CvSpanSerialization.h:9-25`) and a `Firaxis::Array<T, N>` (`FDataStream.h:537-555`)
  are written as N elements **with no count**; the reader reads its own compiled N. `ArrayWrapper` also writes no count
  (`FDataStream.h:479-526`; only `writeCounted`, not used here, does).
- `std::vector` is written with its count (`FStlContainerSerialization.h:226-263`), and the reader resizes to the
  stream's count.

What is in the save [C, the TSV]:
- **213 members** are fixed-length arrays sized by `MAX_MAJOR_CIVS` or `MAX_MINOR_CIVS`. 3 are not streamed
  (`CvDealClasses.h:363` `m_aaiRenewDeal`, `CvDiplomacyAI.h:2165` `m_aTradePriority`, `CvMinorCivAI.h:870`
  `m_abSentUnitForQuest`). 19 are streamed **with a count**: the `CvCityEspionage` lists (`CvEspionageClasses.h:516-534`,
  `Firaxis::Array<.., MAX_MAJOR_CIVS>` typedefs `:437-449`), written as `MAX_MAJOR_CIVS` then the elements
  (`CvEspionageClasses.cpp:7490-7620`) and read with the stream's count and no bound check
  (`CvEspionageClasses.cpp:7338-7488`; `Firaxis::Array::operator[]` is unchecked, `FArray.h:155-165`).
  **191 are streamed without a count**: `CvDiplomacyAI` 156 (declared `CvDiplomacyAI.h:1924-2165`, streamed from
  `CvDiplomacyAI::Serialize`, `CvDiplomacyAI.cpp:412` on; one is `[MAX_MAJOR_CIVS][MAX_MINOR_CIVS]`), `CvMinorCivAI` 21 (`CvMinorCivAI.h:852-900`,
  `CvMinorCivAI.cpp:4623-4668`), `CvPlayerCulture` 3 (`CvCultureClasses.h:311-313`, `.cpp:5780-5782`),
  `CvGrandStrategyAI` 2 (`.h:103-104`, `.cpp:197-198`), `CvMilitaryAI` 1 (`.h:324`, `.cpp:392`), `CvTeam` 2
  (`Firaxis::Array`, `CvTeam.h:599-600`, `CvTeam.cpp:9301-9302`), `CvGameTrade` 2 (`int [MAX_MAJOR_CIVS][MAX_MAJOR_CIVS]`,
  `CvTradeClasses.h:267-268`, `CvTradeClasses.cpp:2406-2407`).
- **3 run-time sized vectors** (count in the stream, size `MAX_MAJOR_CIVS` from the build): `CvPlayer::m_aiWarWeariness`
  (`CvPlayer.cpp:1889`, streamed `:45793`), `CvCity::m_abIgnoredForExpansionBickering` (`CvCity.cpp:1568`, `:32473`),
  the pregame's `s_knownPlayersTable` (`CvPreGame.cpp:3898`, written `:470`; non-empty only with the game option
  `GAMEOPTION_KEEP_UNMET_PLAYERS_UNKNOWN`, `:466-469`; its element type is `uint32` at 22 majors and `uint64` at 43,
  `CvPreGame.h:297-304`, so with that option even the pregame differs).
- **Stream structure by slot index**: `CvPlayer::Serialize` writes the notifications and the diplomacy-requests blocks
  only for `GetID() < MAX_MAJOR_CIVS` (`CvPlayer.cpp:45900-45910, :46022-46030`): players 22-42 carry these blocks in
  a 43 save and not in a 22 save.
- All of `CvPlayer`'s AI objects are streamed for all 64 players, alive or not (`CvPlayer.cpp:45869-45894`).

Size [C declarations; I sum]: the sized members put 772,448 bytes into the uncompressed body of a 22 save and
1,744,328 into a 43 save (+971,880; 64 players x 11,833 -> 26,680 bytes, plus `CvGameTrade` 3,872 -> 14,792 and 64
teams x 176 -> 344); not sized: the three `[MAX_MAJOR_CIVS][NUM_*]` diplomacy arrays and `m_IncomingUnitGifts`
(structs). Mostly zero bytes, so the compressed growth is smaller [I, not measured].

**Nothing records the build's major count** [C]:
- `CvGame::Write` writes the save version (`SAVE_VERSION_LATEST` = 0, `CvGlobals.h:164-167`), the game data hash (the
  database's) and `CURRENT_GAMECORE_VERSION` (`CvGame.cpp:11094-11100`). `MOD_SERIALIZE_INIT_WRITE` writes
  `MOD_DLL_VERSION_NUMBER` (151, `CustomMods.h:26, :1480`). None depends on the switch.
- `CURRENT_GAMECORE_VERSION` is the fork's `git describe` and a Clean/Dirty/Copy word (`update_commit_id.bat`;
  ours: `tools/x64-dll/build.py:278-290`). Upstream makes the 43 build by editing `CustomModsGlobal.h` in place
  (`scripts/release.py:408-426`), so its string says "Dirty" at best, never "43".
- The DLL GUID (`CustomMods.h:24` `MOD_DLL_GUID`, returned by `CvDllContext.cpp:209-212`) and the version string
  `"1.0.0"` (`CvDllVersion.h:23`) that the EXE writes into the save header are the same for both builds.
- On load, `CvGame::Read` compares the data hash and the version string and only writes a log line on a mismatch
  (`CvGame.cpp:10998-11020`, `CUSTOMLOG` "Potential savefile format mismatch"); it never refuses.

### 5.2 Our save tools (`tools/d10/savecmp.py`, `server/src/srv_save.cpp`, the save test) and the pinned layouts

- **`server/src/srv_save.cpp`: no change** [C]. It loops teams 0-63 and players 0-63 (`:190-201`, `:269-280`;
  `MAX_TEAMS`/`MAX_PLAYERS`, 64 in both builds) and builds the header from the pregame, the GUID and the version
  (`:215-247`), none of which carries the major count. Its load check (`:203-211`: pregame and body "used up
  exactly") is the cheapest detector of a save from the other build (5.3).
- **`tools/d10/savecmp.py`: no change** for two saves of the same build [C source; I until run on a 43 pair]. Its
  constants (`:71-72`, `MAX_PLAYERS = 64`, `MAX_CIV_PLAYERS = 63`) are unchanged; its three anchors do not cross a
  major-sized member: the `CvGame::Serialize` walk to `m_AdvisorMessagesViewed` (no `CvGame` member is in the TSV),
  the `CvEconomicAI` blocks (64 per save; `CvPlayerPolicies`, `CvEconomicAI`, `CvCitySpecializationAI` hold no
  major-sized member) and the city blocks (`m_aiEconomicValue` is 63 ints; the major-sized
  `m_abIgnoredForExpansionBickering` comes after the anchor, `CvCity.cpp:32435-32445` against `:32473`). Exception (a)
  (`dll-version-string`) still cuts the version string at body 0x14. A 22 save against a 43 save is not a valid
  comparison (different layouts); the tool would report "different".
- **The save identity test (`tools/d10/run_d10.py`, `research/06-tooling-and-workflows/d10-save-identity-test.md`)**:
  1. The x86 arm is c5h (`%USERPROFILE%/Documents/civ5-headless`, `run_d10.py:78`), which reads
     `Defines.MAX_MAJOR_CIVS` at run time (`host/setup/HHNewGame.cpp:15-31`) and otherwise compiles only
     `MAX_PLAYERS`/`MAX_CIV_PLAYERS` (`host/engine/HHEngineServices.cpp:314, :484, :493`; `HHNewGame.cpp:113, :679,
     :715`): it runs a 43 DLL unchanged [C grep; I live].
  2. The defaults (`run_d10.py:113-116`) come from the `release` keys of `deps/i0.lock` and `deps/dll.lock`, so a 43
     run passes `--x86-dll` (a 32-bit 43 build of the ship branch) and `--srv-bin` (a bin folder holding the x64 43
     DLL) explicitly, or the locks get a variant key (4252/4254 decide).
  3. The games (`run_d10.py:121-124`: tiny and standard specs, the huge reference save) seat 22 majors or fewer, so
     they would pass without one major in slots 22-42. A 43 run needs a spec with more than 22 majors (and for the
     x64 arm the headless host fix of 4.2, `srv_main.cpp:431, :471, :525`), else the new slots are never exercised.
     The huge reference save is a 22-build save and cannot be an input (5.3).
  4. Part 3 (cross-load) compares the x86 and x64 builds of the same variant; it never crosses variants.

### 5.3 Can a 22-major save load in the 43 build, and the reverse (source read; the live check is proposed for Part B)

**Correction 2026-11-15 (ticket 0354; proxy-design.md section 30).** "Nothing refuses the load" below holds for the EXE,
the DLL and the stock route; it no longer holds on the product path. The server now refuses a save of the other variant
before the DLL reads its game section (hook on `ICvGame1::Read`), with D118's report (stage `save-variant`) and the
popup. What is refused: a save whose version string carries our mark `+m<N>` (our 43 builds) of another count than the
running DLL's, always; a save without the mark (our 22 builds, every upstream build, upstream's 43 build included) when
its game section's layout (`CvGameTrade`'s two N x N arrays, anchored between `m_routesPerPlayer` and the embedded save
database) fits exactly one count other than the running one. So upstream's own 43 saves load in our 43 build and are
refused in our 22 build, by the layout [L offline: 20 saves, 5 of them 43 bodies with the mark removed]. What is not
refused: a save whose layout fits no count or several (never seen) and one whose embedded database is empty (never
seen; it ends in 0359's `exe_exception` instead); the stock 32-bit route (upstream behaviour). Replaced: "Nothing
refuses the load" as a statement about the product path.

**No, in neither direction** [C layout; I for what the player sees]. Nothing refuses the load (5.1): the EXE finds the
same DLL GUID, version and mod list (the installer's 43 variants replace only the DLL inside the Community Patch mod,
`research/06-tooling-and-workflows/installer-notes.md:18-19`; the EXE's own checks are section 3's), and the DLL only
logs. The body is then read with the wrong lengths. In body order (`Game::Write`, `Map::Write`, teams, players,
supporting classes; `research/sources/civ5-headless/saves.md` section 2) the first fixed-length major array is
`CvGameTrade::m_aaiTechDifference`, inside `CvGame::Serialize` (`CvGame.cpp:10977`, `CvTradeClasses.cpp:2406`); the
pregame matches unless `GAMEOPTION_KEEP_UNMET_PLAYERS_UNKNOWN` is on (5.1).
- **22 save in the 43 build**: the reader takes 43 x 43 x 4 = 7,396 bytes where 1,936 were written, so it swallows the
  policy array, `m_iNextID`, corporations, contracts, the game id, the embedded save database's size and the start of
  the database file; `m_aaiPolicyDifference` and everything after read SQLite page bytes as counts and values.
  Expected: a huge allocation from a garbage count (out of memory, crash) or a read past the stream's end, before the
  map is read [I]. Through our product the server crashes or fails, which is the D118 error path (popup and report),
  not a 32-bit fallback.
- **43 save in the 22 build**: the reader takes 1,936 of the 7,396 bytes, then reads the rest of the trade arrays as
  the following fields. Mostly zeros, so some reads may pass as empty containers (silent misread) until `Map::Read`
  takes its sizes from wrong bytes; a crash is likely, a playable but wrong game cannot be excluded from the source
  alone [I]. If it got as far as the cities, the counted `CvCityEspionage` lists (43 entries) would overrun their
  22-entry arrays (5.1).
- **Even a byte-correct conversion would be wrong**: a 22 save has its city states in slots 22-62; the 43 build
  treats slots 0-42 as majors and indexes minors as `eMinor - MAX_MAJOR_CIVS` (`CvDiplomacyAI.cpp:2946, :3805`;
  slot ranges in `CvPreGame.cpp:2509, :2606`). A converter would have to renumber every player and team reference in
  the save: not worth building.
- The same holds for the 32-bit DLLs (upstream's 22 and 43 builds share this source), so it is upstream behaviour,
  not something the 64-bit patch adds [I].

**Proposed live check for 4254** (copies of saves with their own names only; Goldberg instances from the pool, D135;
pictures labelled diagnostic, D112):
1. Offline, server headless: load a copy of a 22-build save (the D10 tiny T25 save) in `run_server.py` with the 43
   DLL, and a 43-build save (a 30-major game on a large map, T25) with the 22 DLL; controls 22->22 and 43->43. Record:
   the server's exit code and minidump, the `load:` lines of `srv_save.cpp:186, :206-209` ("used up exactly" or not),
   the DLL's `CUSTOMLOG` mismatch line. Expected: the controls pass, both crosses fail before turn play.
2. Live, the EXE through the proxy with the 43 pack: load the 22 save copy from the load menu and record what the
   player sees (crash, hang, the D118 popup and report, or a game); the reverse with the 22 pack; the stock arm the
   same with the two 32-bit DLLs, to show the symptom is upstream's.
3. If a cross-load does not fail at once (a running but wrong game), that is a defect worth a refusal. Two options
   for the lead and the MC: (a) the DLL writes `MAX_MAJOR_CIVS` into the save under a new save version and refuses a
   mismatch (an upstream-fit DLL change, D104/D95); (b) our builds mark the variant in `CURRENT_GAMECORE_VERSION` and
   the proxy refuses at the first body `Read`, where it already holds the whole stream (proxy-design.md 11.6), with
   the D118 error. (b) catches only saves of our builds, (a) all saves written after it ships.

## 6. More than 43 majors, more than 64 players: hard limits, costs, recommendation (lead, 4251)

Written 2026-11-15 07:33-07:36 EST by lead-0334 from sections 2.3, 3.3, 4.2 and 5. No new reads; every mark is
carried from the section named.

### 6.1 More majors inside the 64 slots (44 to 62)

| Layer | Limit | Holds to | What breaks or costs above 43 | Source |
|---|---|---|---|---|
| DLL | `MOD_GLOBAL_MAX_MAJOR_CIVS` | 62 | nothing in code; 63 would leave `MAX_MINOR_CIVS` 0 and two zero-length arrays (compile error) | 2.3 [C; I for the error] |
| DLL `KnownPlayersBitArray` | `uint64` | 64 majors | none below 64 | 1.1 [C] |
| City-state room | `MAX_MINOR_CIVS` = 63 - M | - | 44 majors leave 19 city states, 62 leave 1 | 1.1 [C] |
| Database | playable civilisations | 44 in VP 5.4.8 | above 44 majors, only with custom civilisation mods | 1.2 [C] |
| World Builder scenario format | 32 majors, 64 city states | 32 | a scenario map cannot seat more than 32 majors; a plain map plus a map script (such as Hormigas) can | 2.3 [C] |
| Autoplay observer | needs one free major slot | M - 1 AI | an autoplay run seats at most M - 1 AI majors | 2.3, `giant-stress-game.md` item 3 [C] |
| EXE | six places tied to slots 0-21 | 62 | same as at 43: humans seat only in 0-21 in network games; no 3D leader scene above 21; one summary record covers only 0-21 | 3.3 [C; effects I] |
| EXE | 64 slots, 63 civilisation players | 62 majors | none below 63 civilisation players | 3.3 [C] |
| Proxy and server | totals only, plus the run-time read of 4.2 | 62 | none, if 4.2's run-time read is adopted | 4.1, 4.2 [C; I] |
| Save | layout per M | - | each M is its own save format, incompatible with every other | 5.3 [C; I] |
| Time and memory | O(M^2) diplomacy | - | (M/22)^2 pairwise work: 62 live majors would be about 7.9x the 22 cost; memory a few MB | 2.2 [I] |

**Cost per extra value of M:** the same as Part B for 43, minus the design work. That is one x64 DLL build, one 32-bit
build for its stock arm, a purity list and a parity baseline (D127), and a save test. Each value is a separate build
and save format; nothing is shared between values at run time.

**Recommendation [I].** 4252's switch takes a value (`--major-civs N`, default 22), so a later value costs a build and
its checks, not a change to the driver. Only 22 and 43 are validated and shipped. Another value (34 and 62 are
upstream's other commented lines) is a new ticket with its own parity baseline, and only if the owner asks for it. Above 44
majors it also needs custom civilisation mods to fill the slots.

### 6.2 More than 64 players in all

- **The DLL is ready to 80 [C, 2.3].** `MAX_CIV_PLAYERS` could rise to 79; `REALLY_MAX_PLAYERS` and `REALLY_MAX_TEAMS`
  are 80 and already size the per-player containers of `CvCity`, `CvUnit` and `CvTeam`. Firaxis' comment at
  `CvDLLUtilDefines.h:20` says exceeding it "will require the engine to be rebuilt".
- **The EXE is the hard limit [C, 3.3].** These all stop at 64, or at 63 civilisation players:
  - the per-frame slot poll and its fixed 64-entry table;
  - the launch passes;
  - the network session cap and flags;
  - the save body loops over 64 teams and 64 players;
  - the Lua checks.

  They are code immediates or fixed static blocks. Moving them needs EXE code bytes, which D4 forbids, so no data write
  does it.
- **What it would take [I, lead's assessment].** The DLL would have to keep players the EXE never sees, so the EXE
  goes on with 64 slots. On the product path the proxy stands between the EXE and the server:
  1. The proxy would map the EXE's 64 slots onto a subset of the server's players, and hide slot ids of 64 and above
     from every interface call and Lua value that carries a `PlayerTypes`. That is thousands of methods
     (`proxy/model/`).
  2. The barbarian would move out of slot 63, which the EXE treats as the last civilisation player.
  3. The players the EXE does not save would need their own save path. The EXE writes the body by looping over 64
     players (3.1), so the extra players would ride in the server's own stream.
  4. Every UI screen that lists players would show at most 64.

  It would never work on the stock 32-bit game, so it cannot be an upstream change fit for 32-bit use (D104). It would
  rule out network play.

  A rough size: an epic of 100-200 agent-hours with high risk, of the same kind as the proxy's interface model.
- **Recommendation.** Do not pursue. Fit the stress game inside 64 slots: 42 AI majors, the observer and 20 city states
  with the 43 build (section 0 item 6). If the owner wants more than 64 players, it is a question for the Architect, not a
  ticket.

## 7. Proposed follow-up tickets and decisions at the gate (lead, 4251)

**Decisions asked of the MC with the review (moderate, handoff 4.7). My recommendation is listed first.**

- **G1. Constants at run time (4.2).**
  - Recommended: one proxy and one server.exe for both DLLs. The four headless-host lines in `srv_main.cpp` read
    `Defines.MAX_MAJOR_CIVS` after post-processing. A boot guard compares the totals the binaries compile.
    `MAX_MAJOR_CIVS` and `MAX_MINOR_CIVS` are poisoned in proxy and server code.
  - Alternative: build the proxy and the server per variant. That doubles their builds and tests and changes no code.
- **G2. Where the variant's source comes from.**
  - Recommended: a fork branch of its own, `x64-43civ`: the x64 pin plus one commit that uncomments
    `CustomModsGlobal.h:11`. This is upstream's own method, and the brief asks for a fork branch. A 32-bit twin,
    `i0-ship-43civ`, does the same for the stock arm. `deps/dll.lock` and `deps/i0.lock` get a variant key, so that
    `resolve_src` accepts them.
  - Alternative: `/D MOD_GLOBAL_MAX_MAJOR_CIVS=43` on the DLL's compiler line. This needs no fork commit, but it is not
    upstream's form, and the define would have to be added to three define lists (4.1).
  - With either, the driver reads the line in `CustomModsGlobal.h` instead of assuming 22, because three upstream
    release tags were cut with the switch on (1.1).
- **G3. The switch takes a value** (`--major-civs N`, default 22). Only 22 and 43 are validated (6.1).
- **G4. Part B's file grants** (the issue's log, 02:33, says these are granted at this gate):
  - the new fork branches;
  - `tools/x64-dll/` (build and lock reading), `tools/release/` (the switch) and `deps/dll.lock`, `deps/i0.lock`
    (variant keys);
  - `server/src/srv_main.cpp`, `server/src/srv_common.h` and the proxy's common header (G1);
  - `tools/d10/run_d10.py` (a spec with more than 22 majors, 5.2);
  - `tools/stressmap/` (a plan of 42 AI majors, the observer and 20 city states for the map pack).

**New tickets proposed (the MC numbers them, rule 26).** None blocks Part B.

| # | Ticket | Why | Size | Priority (proposal) |
|---|---|---|---|---|
| F1 | Upstream draft: the release instructions name `MAX_MAJOR_CIVS (43)` where the line is `MOD_GLOBAL_MAX_MAJOR_CIVS` (`NEW RELEASE INSTRUCTIONS.txt:122`, `NEW TEST VERSION INSTRUCTIONS.txt:32`) | a literal reading builds a 22-major DLL with a warning (1.1) | 0.5 h, a draft only; the owner's yes before anything is sent (D95). **Drafted 2026-11-24 (0356): `upstream-drafts-3505.md` section F, F1**; re-checked on upstream master b0bce8c2f, unsent | low |
| F2 | Upstream draft: `VictoryProgress.lua:339` loops city states from `MAX_MAJOR_CIVS` to `MAX_MINOR_CIVS - 1` | it misses slots 41-62 in the 22 build and is empty in the 43 build (3.2) | 0.5 h draft (D95). **Drafted 2026-11-24 (0356): `upstream-drafts-3505.md` section F, F2** (bound `MAX_CIV_PLAYERS-1`); re-checked on upstream master b0bce8c2f, unsent | low |
| F3 | Refuse a save from the other variant: (a) the DLL writes `MAX_MAJOR_CIVS` under a new save version, upstream-fit; or (b) our builds mark the variant and the proxy refuses at the first body read, with the D118 popup | only if 4254's live check shows a cross-load that does not fail at once (5.3) | 3-6 h | decided after 4254 |
| F4 | EXE trace: where the 3D leader scene stops at player 21 (start from the `NotifyAILeaderInGame` event at 0x440850), and what consumes 0x418C30's 0-21 summary event | two of the EXE's 22-based places are not explained (3.2, 3.3) | 1-2 h, research | low; only if the leader screens for majors 22-42 matter |
| F5 | For epic 0330: the Hormigas stress game on the 43 build is at most 42 AI majors, the observer and 20 city states, not 43 and 52 | the slot arithmetic in section 0 item 6 | a note to 0330's lead, not a ticket of ours | with 4255 |

Not proposed: more than 64 players (6.2, an Architect question only if the owner wants it); other values of M (6.1, only on
the owner's request); network play with humans in slots 22-42 (an EXE limit, 3.1).

## 8. Part B as built (one owner per subsection)

Part A was accepted by the MC on 2026-11-15 07:37 EST (thread 0334, #1614). These are the MC's decisions:
- G1: the major count is read at run time.
- G2 (changed from 7): a `/D MOD_GLOBAL_MAX_MAJOR_CIVS=N` define on the unchanged pins. There is no fork branch.
- G3: `--major-civs N`, default 22. Only 22 and 43 are validated.
- G4: the file grants as listed in 7.
- Follow-ups: F1 and F2 = 0356 (drafts in `upstream-drafts-3505.md` section F, 2026-11-24, not sent), F3 = 0354, F4 = 0355. F5 was relayed to epic 0330.

### 8.1 The 43 DLL builds and the release driver's switch (4252)

Written 2026-11-15 by the 4252 worker, branch `task-4252-43civ-build`. Evidence and scripts:
`evidence/0334/4252/` (`build_x64.py`, `build_x86.py`, `builds-2026-11-15.txt`). The operator's view and the
contract are in `research/06-tooling-and-workflows/release-driver.md` W8, `tools/release/README.md` section 2 and
`tools/x64-dll/README.md` "Options".

**The switch [C, code; tests].**
- `tools/x64-dll/build.py --major-civs N` and `build_all.py --major-civs N`, default 22.
  - Only 22 and 43 pass. 2..62 pass with `--unvalidated-major-civs`; 63 and above are always refused (6.1).
  - 22 means `MOD_GLOBAL_MAX_MAJOR_CIVS` is not defined at all. This matters: `=22` would compile the three
    `#if defined(...)` blocks of 1.1.
  - Any other N appends `/DMOD_GLOBAL_MAX_MAJOR_CIVS=N` to the DLL compile, after the configuration's defines. It is
    never added to `PREDEFS`, which `server/build_server.py` and the proxy read, so they stay without the define (G1).
  - The version string becomes `<describe>+mN <Clean|Dirty|Copy>`, in the same form as the instance-aware
    `+ia<n>`.
  - `build-summary.json` and `build_all.json` record `major_civs`, `major_civs_define` and `major_civs_header`.
  - The smoke step fails, even when it exits 0, unless the DLL's own start lines (`CvDllContext.cpp:248-250` in
    `run/work/Logs/CustomMods.log`) say `Supporting N major civilizations` and carry `+mN`. The result is recorded as
    `smoke_major_civs`.
- **The header line is read, never assumed.** `header_major_civs` reads the source's `CustomModsGlobal.h`, skipping
  `//` and `/* */` comments.
  - A live line that is not N refuses the build before any compile: exit 1 in build.py and build_all.py, a stop of
    CL 6 in the driver.
  - A live line equal to N builds with no second `/D`.
  - Two live lines, or a value that is not a number, are refused.
- **32-bit.** The fork's `build_vp_clang.py` has no define option, and clang-cl 19.1.7 ignores the `CL` and `_CL_`
  environment variables. Tested 2026-11-15: a test file's `#error` fired with `CL=/D...` set.
  - So the driver gives the define to the script's `RELEASE_PREDEFS` and `DEBUG_PREDEFS` in the git-archive copy
    (`tools/release/stages/build.py` `inject_x86_define`). It adds three lines after `DEBUG_PREDEFS = ...`, before
    `PREDEFS = {` reads the lists, plus a printed line naming the define.
  - The DLL source is the commit's own; nothing is committed to the fork.
  - The 43 plain 32-bit build therefore comes from a git archive, not a worktree (a worktree would report "Dirty"),
    with the stub `<describe>+m43 Copy`. The `-ia` stub is `<describe>+ia<n>+m43 Copy`.
- **take_release.py --major-civs N.** Its effects in a run are listed in release-driver.md W8:
  - x64: build_all, then `build_server.py --deps`;
  - the names `-m43` and the lock keys `release-43` / `verified-43` / `instance-43`;
  - the pack bound to the tag's `(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll`, with the descriptor field
    `player_dll_43_sha256` (5.4.8: `01451bd73d0baf8e...d635808`, 16,841,728 B, `git show` of the tag tree, never run);
  - manifest `"major_civs": 43`.
  - For 22, every command, name, key and stage-inputs hash is unchanged.
- **The pack and the (3b) files (decision, 4252).** The pack does not carry the (3b) modinfo or `AdvancedSetup.lua`.
  - A 43-civ player has them already: upstream's installer puts them in `MODS\(3b) 43 Civs Community Patch\` (1.3).
  - Our installer step only replaces the DLL in `(1)` and keeps the player's DLL as the fallback.
  - A failed 64-bit boot on a 43 install shows the D118 error popup like any other. The fallback DLL the manifest
    names is that player's (3b) DLL.

**Real builds [L, 2026-11-15, workstation; pins x64 `04b1211b8`, i0 `26234371e`].** The x64 sources are a
`git archive` copy whose fingerprint matches deps/dll.lock. The builds are release, offline, `--jobs 8`, with
`SOURCE_DATE_EPOCH=1794587302` (the epoch of the pin's own build, deps/dll.lock `epoch`). One LTO link ran at a time,
through `perf/quietlock/linkplace.py`.

| Build | When (EST) | Result | DLL SHA-256, size |
|---|---|---|---|
| x64 43, `build_all.py --major-civs 43` at tools 3dd1c972 | 07:52-08:05 (800 s: dll 700 s = compile 119 s + link 534 s, link peak private 13.3 GB) | every step rc 0, 0 unresolved, narrowing 794 sites / 0 new / 4 gone (the 43 build adds no site), repeat check ok (181 inputs, 0 stamped); smoke: "Supporting 43 major civilizations", version "Release-5.4.8-51-g04b1211b8+m43 Copy"; compile line ends `... /DVPRELEASE_ERRORMSG /DMOD_GLOBAL_MAX_MAJOR_CIVS=43` | `3E862705C4BD7A2D523F5DF93EA7C9310F447E9458E0C9F9642FA2AD0281D631`, 23,625,728 B |
| x64 22 (no option) at tools 3dd1c972, same epoch | 08:07-08:23 | every step rc 0; smoke "Supporting 22 major civilizations", version "Release-5.4.8-51-g04b1211b8 Copy" | `125E2E2AD09BE19648ECC9AAA04DF452AB97638536C8B23BF166C122D961BB96`, 23,655,424 B |
| x64 22 control: main's tools b906731b (before 4252), same source and epoch | 08:24-08:40 (946 s: dll 845 s) | every step rc 0; smoke "Supporting 22 major civilizations"; tools exported with `git archive b906731b tools/x64-dll tools/release deps perf/quietlock` | `DCE69943A65D896C568C770D786DF7F6ED8DB4B3F9AE7F9FB102FFFB8E7320CE`, 23,655,424 B |
| 32-bit 43 plain, `build_x86.py` (the driver's helpers) | 08:05-08:13 (569 s with the link-place wait) | rc 0; the script printed `/DMOD_GLOBAL_MAX_MAJOR_CIVS=43 added`; version "Release-5.4.8-39-g26234371e+m43 Copy" found in the DLL | `6E2A7BE1D13552EF402368D83B60CEA842734FA3789698B0678DD4A7E33DEDD5`, 17,111,040 B (the 22 pin: 17,143,296 B) |
| 32-bit 43 instance-aware, + `ia-ee893bc18-440878983.patch` | 08:13-08:30 (995 s, most of it the link-place wait) | rc 0; version "Release-5.4.8-39-g26234371e+ia440878983+m43 Copy" in the DLL; it carries the `VPLuaExec` strings as `i0ship-26234371-ia` does | `204FBFCC11C98070D5267C79E97A6E594DA0C51127266C7B585578957C25E1B8`, 17,451,008 B |

- **The default path did not move.** The pin's own DLL (`74588135...`, 23,616,000 B) was built at tool commit a803776cf.
  Our 22 build at 3dd1c972 gives other bytes. So a second 22 build used main's tools from before this issue (b906731b), with the same source, epoch and machine. **The compile and link inputs of the two 22 builds are byte-equal**: all 173 objects (LTO bitcode), `link.rsp`, every stub and import library, and `commit_id.inc` ("Release-5.4.8-51-g04b1211b8 Copy"). The compile lines differ only in the order of the two `-ffile-prefix-map` flags, which are sorted by folder length, and the output folder names differ (`m22`, `m22base`). That proves 4252 does not move the default path [C, `evidence/0334/4252/cmp22.py`].

  **But the two DLLs differ** (`125E2E2A...` and `DCE69943...`, both 23,655,424 B) in three places:
  - the `/Brepro` header stamp;
  - 629 bytes of `.text`, all inside `CvPlayer::doInstantGreatPersonProgress` (RVA 0xD7BEE6-0xD7C83F; the function starts at 0xD7BDB0 in the map);
  - 19 bytes of `.rdata`.

  This is the same function and kind of change as 0131's two code variants (upstream-fix-batch-1.md 8c). **This corrects 8c's conclusion** that the variant is chosen by the stub libraries' COFF time stamps and that the link repeats once those are 0. Here every stub member's stamp is 0 (repeat check: 181 inputs, 0 stamped, in both builds) and every link input is byte-equal, yet the variant differs. The choice depends on something outside the link inputs: the output folder (path or length), the time, or the machine's load [L, 1 pair; the cause is not bisected]. So "the 22 build gives the pin's DLL bytes" cannot be shown by a hash, and two release links of the pin are not guaranteed to give one hash. The pin's `74588135...` was also built at an older tool commit (a803776cf), and stub and library sources changed since (for example 0343's CvWorldBuilderMap read side), so its bytes are not expected anyway. Follow-up for the MC: a repeatability ticket (relink one build's inputs into folders of other names and lengths, and bisect).
- **The 43 build costs no new narrowing site** and links in the same time and memory as the 22 build. Its DLL is
  29,696 B smaller than this 22 build. The 32-bit 43 DLL is 32,256 B smaller than the 22 pin [C, sizes].
- Not verified here: that the 32-bit 43 DLL reports 43 at run time. The static evidence is the define on its compile
  line (the printed line) and its version string; the live proof is 4255's stock arm (`CustomMods.log`).

**Layouts, for 4253, 4254, 4255 (not pins).**
- `F:/c5-64bit-work/0334/4252/layout/x64-04b1211b-m43-p11b2c60/`:
  - bin/: the 43 DLL above, plus `server.exe` `DB26A0EB31E5862A7494839C1CF707E226D7FC5E9B14E2C06FD8B4AFB450E0B3`;
  - `proxy/bin/CvGameCore_Expansion2.dll` `93ADCAD1EC09293DCCD904CAA58AC65FD9C7AC06EE0BE68E208F39C2BEA3DB69`;
  - both built by `build_server.py --deps` at 11b2c6022 (dirty=0), with 4253's run-time server.
- `.../layout/i0ship-26234371-m43/` and `.../i0ship-26234371-m43-ia/`.
- **A trap (2026-11-15).** The first layout, `x64-04b1211b-m43-p3dd1c97`, named the commit read when the script
  started. Its server had been built at 90363f307 with a dirty tree, because I committed during the run (4255 found the
  mismatch in server.log). The layout script and the release build stage now take the commit from the server's build
  id, or fail when the repository moves during the build. The old layout's README states this.

**Tests [C, 2026-11-15].**
- `tools/x64-dll/tests/test_major_civs.py`: 40 passed.
  - The value check, the header reader, the compile line (22 identical to before, 43 adds one flag), the version
    string, the smoke check.
  - build.py from the command line: 63, an unvalidated value, a live line of another count, and `--link-only` across
    counts each give exit 1 with the reason.
- `tools/release/tests/test_take_release_major.py`: 14 OK.
  - The command line exits 2 on 63 and on 34; the work folder of a count; pack.py on 43, 22 (manifest unchanged), 34,
    and a tag without (3b) (exit 10); the 32-bit define injection.
- `test_take_release_stages.TestMajorCivs`: 7 OK.
  - A full 43 candidate run (names, lock keys, archives, stubs, predefines, `--deps`, the (3b) DLL);
  - the 22 run as before;
  - 43 after 22;
  - a live line of another count is a stop; a live line equal to the count builds;
  - the descriptor's 43 hash;
  - a build_all failure.
- The whole release suite (232 tests) ran with one failure: `test_descriptor.test_wrong_types` did not know the new
  field. It was fixed, and `test_descriptor*.py` then gave 58 OK.
- The x64-dll suite (91 tests) ran with one failure, `test_repro.py::test_every_link_and_compile_of_the_wrappers_uses_them`.
  It counts `+ repro` in `proxy/build_proxy.py` and expects 2; main b906731b has 3, so it fails on main as well and is
  not 4252's.

**The build that ships: the rebuild at the team head [L, lead-0334, 2026-11-15 09:55-10:07 EST; MC #1702, #1707].**
- Why: main 74118dfc (0360) moved `deps/dll.lock` `release` to tools fdee65668 with 0343's .Civ5Map reader.
- How: one rebuild at the team head 9aa30f190, with main merged and dirty=0:
  `python -I research/03-dll-engine-boundary/evidence/0334/4252/build_x64.py --work F:/c5-64bit-work/0334/rebuild
  --arms m43 --epoch 1794754471` (the head's commit time). Log `F:/c5-64bit-work/0334/rebuild/run-1.log`.
- Layout `F:/c5-64bit-work/0334/rebuild/layout/x64-04b1211b-m43-p9aa30f1` (this is `release-43` in `deps/dll.lock`):

  | File | SHA-256 | Size |
  |---|---|---|
  | DLL | CF9811262633EC3181FC86CCF95E40B52A1D2720AA24395FB4A4FEF83FC89F16 | 23,625,728 B |
  | server.exe | D529D5D0... | 3,697,664 B |
  | proxy | D6A43056... | 1,097,728 B |

- build_all: six steps rc 0, 0 unresolved, narrowing gate 794 against baseline 798 (0 new). The link took 421 s. The
  smoke run logged 43.
- Correction to the reason given at the time (#1701, corrected in #1706): 4252's DLL 3E862705 already carried 0343's
  reader. Its tools commit 3dd1c972 contains the reader's commit 00051595. The two 43 DLLs have PE sections of equal
  sizes; their bytes differ only by the epoch and the LTO link's non-repeatability (ticket 0358). The newer parts are
  the server and the proxy (current main). Parity and the save test of 8.3 ran on p9aa30f1.

### 8.2 The run-time major count in the server; guard and poisoning (4253)

Written 2026-11-15 07:43-08:10 EST by the 4253 worker on branch `task-4253-runtime-majors`, from epic-0334-43civ
68e57f32. Evidence and scripts: `evidence/0334/4253/`. One proxy and one server.exe now serve both DLLs (G1). Nothing
in either is built per variant.

**What was built** [C code; L tests and runs below]:
- **The run-time read** (`server/src/srv_main.cpp`, block `BEGIN/END 4253 PLAYER LIMITS`, namespace `vplimits`).
  - `SrvCheckPlayerLimits(db, why)` reads `Defines` `MAX_PLAYERS`, `MAX_TEAMS`, `MAX_CIV_PLAYERS`, `MAX_MAJOR_CIVS` and
    `MAX_MINOR_CIVS` (`SELECT Value FROM Defines WHERE Name = ?`).
  - It fails, with a message that names the row and the value, when:
    - a row is missing, or its value is not an integer (NULL, text, a real);
    - a total differs from the server's compiled value;
    - `MAX_MAJOR_CIVS` is outside 2..62 (`CustomModsGlobal.h:8`; 63 does not compile, 6.1);
    - majors + minors is not `MAX_CIV_PLAYERS`.
  - On success `SrvMajorCivs()` returns the count. Unlike c5h (`HHNewGame.cpp:15-31`), there is no silent fallback to 22.
- **Headless mode** (`PhaseDatabase`): the check runs right after `PerformDatabasePostProcessing`, the step in which the
  DLL writes those rows (`CvDllDatabaseUtility.cpp:265-274`, `INSERT OR REPLACE`).
  - The report line is `player limits: MAX_MAJOR_CIVS 22, MAX_MINOR_CIVS 41 (the DLL's, read from Defines); ...`.
  - A failure is `FAILED: player limits: ...`, then `result: FAILED` and exit 2.
  - The four former `MAX_MAJOR_CIVS` sites (`:431, :471, :525, :536` at 975a36c9) use `SrvMajorCivs()`: slot civs and
    teams, slot status, the leader choice, and the city-state report from slot `SrvMajorCivs()`.
  - New: `majors=` in server.cfg above the DLL's major slots is a loud failure. Before, slots above 21 were silently
    left closed.
- **The summary.** The Lua summary (`t000-summary.txt`, `final-summary.txt`) gains the line `limits MAX_MAJOR_CIVS ..
  MAX_MINOR_CIVS .. MAX_CIV_PLAYERS ..` (the DLL's `GameDefines` as its Lua sees them). Each player line gains a last
  word, `major` or `city-state` (`IsMinorCiv`): this is the slot read-back.
- **Remote mode.** The only place where remote mode sees the DLL's `Defines` right after post-processing is the
  `ICvDLLDatabaseUtility1::PerformDatabasePostProcessing` hook in `server/src/remote/srv_remote_db.cpp`.
  - The check runs there on the server's copy of the game database (`s_game`), after every post-processing call.
  - On success it writes a `server-report.txt` line. On failure it calls `SrvRemote::ReportAndExit(1, why)`: the
    server writes its report and exits, the proxy's call fails, and the proxy takes the D118/D119 path (error report
    and popup).
  - Granted by lead-0334, #1626.
  - **Not run live** in remote mode. Remote mode never uses the major count otherwise (4.1).
- **Poisoning.** After the fork's includes, `server/src/srv_common.h` and `proxy/src/proxy_rt.h` `#undef` and redefine
  `MAX_MAJOR_CIVS`, `MAX_MINOR_CIVS`, `OBSERVER_TEAM` and `MAX_PREGAME_MAJOR_CIVS` as
  `_Pragma("GCC error \"... (issue 4253)\"") 0`.
  - Any use in code, or in an `#if`, stops clang-cl with the message, for example `error: MAX_MAJOR_CIVS is read at
    run time in the server: use SrvMajorCivs() (issue 4253)`.
  - `proxy_rt.h` is the proxy's real common header: the only proxy source file that includes the fork's
    `CvGameCoreDLLPCH.h`. `vp_gen_common.h`, the generated code's first include, only includes it.
  - Every server translation unit that sees fork headers does so through `srv_common.h`; the renderer's units see none.
  - The full server and proxy builds, the generated code included, compile with the poison in place. So no server,
    proxy or generated code uses the split's names.

**Tests** (`server/tests/limits/run_player_limits_test.py`, `test_player_limits.cpp`; the block is compiled from
`srv_main.cpp`'s own text) [L, 2026-11-15 07:58]:
- `python -I server/tests/limits/run_player_limits_test.py --out C:/c5-64bit-work/0334/4253/limits1 --src <git
  archive of 04b1211b8> --asan` -> `PASS (45 checks, 0 failed)`, `evidence/0334/4253/limits-test.log`.
- Database cases, each checking the exit code and the output line:
  - `MAX_MAJOR_CIVS` 22, 43, 2 and 62 pass with that count;
  - fail with the value named: missing row, 0, 1, 63, 70, `'forty-three'`, NULL, `MAX_PLAYERS` 80, `MAX_TEAMS` 80,
    `MAX_CIV_PLAYERS` 79, majors 43 + minors 41, no `Defines` table.
- Compile probes on the server side (x64, `srv_common.h`) and the proxy side (x86 in the VS 2008 environment,
  `vp_gen_common.h`): a totals-only control compiles; uses of the four names in code and in `#if` fail with the
  message.
- The database cases again under MSVC `/fsanitize=address` (x64), after a heap-overflow canary is caught: no report.
- The existing suites on this build [L, 08:00-08:06], logs in `suites/`:
  - `proxy/tools/test_proxy_rt.py`: 106/106;
  - hutest: 47 checks, 0 failed; `--scenario all`: 8 as expected;
  - `server/tests/run_preinit_report_test.py`: 5/5;
  - `run_power_test.py --server-exe`: pass;
  - `db/run_db_write_test.py`: pass (its control fails as it must); its unit tests: 5 OK.

**Build** [L, 2026-11-15 07:52-07:54]:
- Command: `python -I server/build_server.py --src C:/c5-64bit-work/0334/4253/src-x64 --out C:/c5-64bit-work/0334/4253/b1
  --config release --offline --deps <copy of the pin's release deps, 9045's build_all of 04b1211b8>`, 117 s.
- No LTO link: only the DLL is linked with LTO, and it comes unchanged from the deps (bin DLL 74588135..., the pin's).
- server.exe `8E6BB832CCD017F8005E289AD38A0340248F97E31A63D5387485E31ECDECFD27`, proxy
  `16E3521198AF506DB45BD3D5BB6D47042BE99E63CC051907F0616C4DD6415B79` (`build-b1-summary.txt`). Not a release build of
  the epic.

**The 22 DLL is unchanged** [L, 2026-11-15 07:59-08:01, tiny spec `continents-tiny-fixed-seed.json` turns 0-10, pin DLL
74588135, `run_server.py --save-out end --fail-on-unserved`]:
- Arms: this server (runs `mine`, `mine2`) and the pin's server.exe B7A4D8CC... of `x64-04b1211b-pa803776` (`pin`,
  `pin2`).
- All four runs: `result: passed`, the same updates per turn (14, 13 x 9, 10 idle), and the same final summary (the new
  `limits` line and the kind word aside).
- The statistics databases (`cache/stats.db`) are identical in every row of every table except
  `uuid_dictionary.uuid_hex`, the game id (`run22-stats-diffs.txt`).
- The saved bodies differ by 1.7-2.9 KB in every pair, pin against pin included (`run22-body-diffs.txt`): the
  run-to-run noise named in `research/06-tooling-and-workflows/d10-save-identity-test.md` 4b.3 (`CoCreateGuid` ids and
  buffer areas). `run22.log` says `VERDICT FAIL` only because its first criterion was equal raw bodies; the two control
  runs (`run22b.py`) show that criterion cannot hold.
- `limits MAX_MAJOR_CIVS 22 MAX_MINOR_CIVS 41 MAX_CIV_PLAYERS 63`. Players 0-3 are majors, 22-29 city states.
- Negative: `server/tests/specs/continents-large-30majors-fixed-seed.json` (30 majors) on the 22 DLL exits 2 with
  `FAILED: majors=30 in server.cfg, but this DLL has 22 major slots (Defines MAX_MAJOR_CIVS; issue 4253)`
  (`run30-on22/`).

**The 43 DLL with the same server.exe** [L, 2026-11-15 08:07-08:08]:
- DLL: 4252's x64 43 build `F:/c5-64bit-work/0334/4252/m43/deps/dll/CvGameCore_Expansion2.dll`, SHA-256 3E862705...,
  version `Release-5.4.8-51-g04b1211b8+m43 Copy`, release configuration (8.1).
- Bin folder: this branch's `b1/bin` (server.exe 8E6BB832..., the same file as the 22 runs, and the pin's libraries)
  with that DLL in place (`run43.py`).
- Spec: `server/tests/specs/continents-large-30majors-fixed-seed.json`: Continents, Large (104 x 64), 30 majors,
  16 city states, seeds 43017/43018. Turns 0-5.
- Result:
  - `run_server.py` exit 0, `result: passed`, 0 Lua errors, 246 updates (47-48 a turn), no unserved method outside the
    allow-list.
  - Report: `player limits: MAX_MAJOR_CIVS 43, MAX_MINOR_CIVS 20 (the DLL's, read from Defines); MAX_PLAYERS 64,
    MAX_TEAMS 64, MAX_CIV_PLAYERS 63`.
  - The leader report seats a civ and leader in slots 0-29 exactly.
  - `t000-summary.txt` and `final-summary.txt`: `limits MAX_MAJOR_CIVS 43 MAX_MINOR_CIVS 20 MAX_CIV_PLAYERS 63`; the
    30 majors are players 0-29, nobody is in 30-42, and the 16 city states are players 43-58.
- `run43.log` `VERDICT PASS` (11 checks); `run43/`.
- The turn times (about 0.5-0.7 s a turn after turn 0) are a workstation indication only (COMMON 23).
- The report's own `slot N: city-state` lines (printed only for `SS_COMPUTER` minor slots) appear in no run, the
  pin's server included. The slot read-back is therefore the Lua summary.

**Not verified:**
- The remote-mode guard has not run live: no proxy game was played with either DLL. Its code path compiles, and
  test_proxy_rt and hutest pass with it.
- No AI turns beyond turn 5 on the 43 DLL here (4255 plays the long games).
- No ASan run of server.exe itself. The guard's code ran under ASan in the test program (the same text).
- The pin's own server.exe against the 43 DLL was not run. By the source read (4.1) it would seat only 22 majors.
### 8.3 Purity, parity baseline, save test and the 22/43 cross-load (4254)

Written 2026-11-15 08:13-11:30 EST by the 4254 worker on branch `task-4254-43civ-parity`, from epic-0334-43civ
76e50452. Evidence and scripts: `evidence/0334/4254/`.

Builds used:
- **Release-43, the build that ships (MC #1702, #1707), for the parity proof and the save test (parts 2 and 3):** the
  lead's rebuild `F:/c5-64bit-work/0334/rebuild/layout/x64-04b1211b-m43-p9aa30f1/`, made at team head 9aa30f19 with
  dirty=0. DLL `CF981126...9F16` (smoke 43, version `+m43`), server.exe `D529D5D0...`, proxy `D6A43056...`. The lead
  notes that the DLL code is that of `3E862705`: same section sizes, bytes differing only by epoch and LTO.
- x64 for the purity list, the cross-load, the proxy arm and the first parity pair: 4252's clean 43 layout
  `F:/c5-64bit-work/0334/4252/layout/x64-04b1211b-m43-p11b2c60/`. DLL `3E862705...D631` (release,
  `/D MOD_GLOBAL_MAX_MAJOR_CIVS=43` on the pin 04b1211b8), server.exe `DB26A0EB...` and proxy `93ADCAD1...`, built at
  repository 11b2c602 with dirty=0, which carries 4253's run-time count. Results on it are an indication for the
  release-43 build, and are labelled so.
- 32-bit: 4252's plain 43 DLL `i0ship-26234371-m43` (`6E2A7BE1...`).
- The 22 comparison arms: the pin's x64 DLL `74588135...` (`x64-04b1211b-pa803776`) put in place of the 43 DLL in a copy
  of the same bin folder, so both arms run the same server.exe.

**Verdict for ticket 0354** [L, 2026-11-15; source C]. A save does not cross between the variants: every cross-load
fails at once, and no run loads into a wrong game.
- Headless, the server reports the failure cleanly (exit 2).
- Through the proxy, the player gets the EXE's CRT abort box ("Runtime Error! ... terminate it in an unusual way").
  There is no D118 popup and no report. This is the wrong path.
- The abort site: the server's DLL reads past the end of the save. The server then pulls more bytes from the engine
  stream with `kRtE_StreamRead`. The proxy serves that call with the EXE stream's `ReadIt`
  (`proxy/src/proxy_rt.cpp:913-930`) outside the try/catch that issue 9036 put around every other served call
  (`:940-977`). The EXE's C++ exception therefore reaches `terminate()`.
- The MC's decision for 0354 (#1657, forwarded by lead-0334 2026-11-15 08:59): a cross-variant load must be refused
  before the body reaches the DLL, with the D118 popup and report; a CRT abort is the wrong path.
- A separate small defect, whatever 0354 does: the `kRtE_StreamRead` branch needs the same catch, so that any overrun
  of a pull stream ends in `exe_exception` and the D118 path.
- **Correction 2026-11-15 (tickets 0359 and 0354).** The CRT abort above is history (pins before 0359). 0359 put the
  `kRtE_StreamRead` branch under the catch (proxy-design.md 27), and 0354 refuses the cross-variant load itself: the
  server's hook on `ICvGame1::Read` reads the save's variant (our `+m<N>` mark, else the game section's layout) before the
  DLL reads a byte of the game section, and a mismatch ends in D118's report (stage `save-variant`, a summary naming both
  versions) and the popup; the DLL's "Potential savefile format mismatch" line no longer appears (proxy-design.md 30, live
  results 30.5). Not refused: saves whose layout fits no count (never seen), saves with an empty embedded database
  (never seen), the stock route. Replaced: "the player gets the EXE's CRT abort box" as the current behaviour.

**1. Purity (D104)** [L, 2026-11-15 08:19].
- Command: `evidence/0334/4254/purity43.sh`, which runs `proxy/tools/derive_purity.py --src <4252's plain copy of the
  pin, F:/c5-64bit-work/0334/4252/src-x64-04b1211b> --dll <43 DLL> --out ... --commit 04b1211b8...` and the same for the
  pin's 22 DLL.
- Both lists keep **143 of 143** audited rows (114 method keys, 29 Lua names). The two `purity.tsv` files differ in one
  line, `dll_sha256`, and the two `report.tsv` files are equal (`purity-m22-vs-m43.diff`).
- The 43 list: `purity.tsv` SHA-256 `82C09EF2...`, kept in `F:/c5-64bit-work/0334/4254/purity43/` and as
  `evidence/0334/4254/purity-m43.tsv`.
- Why nothing else differs [C]: derive_purity hashes source text and never runs the preprocessor, and the 43 build
  has the pin's files byte for byte (the define is on the compiler line, 8.1). It never sees the define, and it does
  not need to.
- What the define changes in those rows (`purity_split_check.py`, written for this): the source has four `#if` blocks
  that test `MOD_GLOBAL_MAX_MAJOR_CIVS`: `CvDLLUtilDefines.h:27-32` (the constants), `CvMinorCivAI.cpp:4487-4491`,
  `CvReligionClasses.cpp:2204-2209` and `CvStartPositioner.cpp:403-498` (4.3 names the last three).
  - No audited row cites or calls into any of them.
  - **One row names a split constant**: the Lua binding `Player.IsMinorCivDisplayedQuestForPlayer` cites
    `CvMinorCivAI::IsDisplayedQuestForPlayer` (`CvMinorCivAI.cpp:7544-7563`). There `MAX_MAJOR_CIVS` appears only in
    a `PRECONDITION` bound; the body is a read-only loop over `m_QuestsGiven[ePlayer]`. It is pure in both builds;
    only the bound moves from 22 to 43.
  - So the 22 audit holds for the 43 DLL, and the list carries over unchanged with its own hash
    (`purity-split-check.tsv`).
- Where the pack takes it: the release driver needs no change. Its `purity` stage derives the file from `x64_src` and
  the DLL that the `build` stage made (`tools/release/stages/purity.py`), and with `--major-civs 43` (4252, release
  driver W8) both are the 43 build's. A hand-made pack uses
  `tools/release/pack.py --layout <43 layout> --purity F:/c5-64bit-work/0334/4254/purity43/purity.tsv`.
- Not run: the proxy's verify leg (`local_verify=1`) with this file on the 43 DLL.

**2. Parity baseline (D127)** [L]: **passed on release-43**.
- **Corrected 2026-11-15 (issue 0372):** the stage now plays this 30-major game (`l30`) on both arms whenever
  `--major-civs` is above 22, records the majors each arm seated and refuses a 43 run that played only 22-slot games;
  it also runs on the workstation (host `local`). Contract: `tools/release/README.md` section 2c. What follows in
  this item, down to the 0372 live run, was true before 0372.
- **The stage's own live run on release-43 (issue 0372)** [L, 2026-11-15 20:18:22-22:25:06 EST, workstation, not for
  timing (D106); evidence `evidence/0372/`]:
  - Command (`evidence/0372/live43.sh`): `take_release.py Release-5.4.8 --only wholegames --tier validated
    --major-civs 43 --work C:/c5-64bit-work/0372/rel43 --scratch F:/c5-64bit-work/0372/scratch`, with
    `C5_RELEASE_TESTBEDS=local` and `C5_RELEASE_WHOLEGAMES_GAMES=l30`. The work folder's `build` stage was seeded ok
    with the release-43 builds above (`evidence/0372/seed_rel43.py` checks x64 DLL `cf981126...`, server.exe
    `d529d5d0...`, x86 `6e2a7be1...`); no other stage was run, so the report's verdict is "validated tier not reached
    (survey pending)" by design, and the subset makes `wholegames` a stop in any case.
  - The x86 spec the stage resolved for 43 slots (`sha256 17a4c567...`) is byte-identical to 4254's
    `large30-x86-spec-resolved.json`.
  - x64: `pass: game over (Science, turn 387; clean end, rc 0)`, 3,303 s. x86 (c5h): `pass: game over (Science,
    turn 387; state completed, host passed, end reason victory)`, 4,254 s, peak working set 351 MB. Both: Korea,
    score 3,470, 0 Lua errors, **30 majors seated** (stats.db `civ_turn_era` / `GameResult`).
  - statscmp: **IDENTICAL, 16 tables, 3,613,758 rows**, the same row count on each arm (`rows_x64` = `rows_x86` =
    3,613,758), the same total as 4254's hand run. Stage outputs: `major_civs 43`, `majors_played 30`,
    `parity: passed on a subset`. The database: `bd9b8aa1...` = the descriptor's `test_db_sha256`.
  - A first attempt (16:16-17:04) played the x64 arm to the same end but its x86 arm never started: longgames.py named
    `<c5h>/bin/civ5headless.exe`, a testbed kit's path, and the workstation's c5h is a repository whose host
    `out/latest.txt` names. Its missing stats.db was then read as "parity failed". Both fixed in 0372 (README 2c).
- The release driver's stage `tools/release/stages/wholegames.py` cannot test the variant as it is:
  - it sends `tools/testbed/jobs/longgames.py` to the testbeds with games `new:SIZE:SCRIPT:MAP:SYNC`, which seat the
    world's default player count (at most 12 on Huge), so slots 22-42 are never used;
  - the job has no field for a major count;
  - this ticket has no `machines` field, so it stays on the workstation.
- So the stage's comparison runs **by hand** on the workstation: `evidence/0334/4254/parity43.py` (`parity43.sh`).
  - The 30-major Large game (`server/tests/specs/continents-large-30majors-fixed-seed.json`), x64 server against c5h
    with the plain 32-bit 43 DLL (the plain DLL is what the stage ships).
  - Both arms to turn 1000, which means to the end of the game, with AI logging off and the command lines of run_d10's
    hosts.
  - Then `tools/d10/statscmp.py` over every table, judged by the stage's own `parity()`.
- **On the old 43 DLL `3E862705` (indication, MC #1707)** [L, 2026-11-15 08:57:39-10:11:32, workstation shared with
  other teams, `F:/c5-64bit-work/0334/4254/parity43/`, evidence `parity-p11b2c60/`]:
  - Both games ended by themselves at **turn 387 with the same Science victory (Korea)**. x64: `result: game_over`,
    3,510 s, peak working set 328 MB. x86 (c5h): `completed`, end reason victory, 4,390 s. 0 Lua errors in both.
  - statscmp over every table: **IDENTICAL, 3,613,758 rows in 16 tables**. The stage's `parity()` says `identical`,
    with no known exception needed: `ReligionBeliefYields`, whose `IsReligionOwner` column is the known exception,
    is equal too.
  - So the whole-game x64 = x86 identity holds with 30 majors in slots 0-29, to the end of the game.
- **On release-43 `CF981126` (the proof): parity passed** [L, 2026-11-15 10:19:03-11:26:59,
  `F:/c5-64bit-work/0334/4254/parity43-p9aa30f1/`, evidence `parity-p9aa30f1/`].
  - Builds: x64 DLL `cf981126...`, server.exe `d529d5d0...`; x86 `6e2a7be1...`.
  - Both games ended at **turn 387 with the same Science victory (Korea)**, the same end as on the old DLL. x64:
    `game_over`, 3,128 s, peak working set 327 MB. x86: `completed`, end reason victory, 4,034 s. 0 Lua errors in both.
  - statscmp over every table: **IDENTICAL, 3,613,758 rows in 16 tables**; `parity()` says `identical`.
  - The verdict is "parity passed": the release-43 variant may be handed out as far as D127 goes.
  - The times are workstation indications only (COMMON 23), taken while other teams worked.

**3. The save test with more than 22 majors** [L]. Details in
`research/06-tooling-and-workflows/d10-save-identity-test.md` section 4e.
- `run_d10.py` has a new game, `large30` (the same 30-major spec, early turn 25, final turn 100), and an option
  `--major-civs N`. The x86 arm gets a spec resolved for N slots, because c5h's resolver stops at 21 majors.
- Command: `evidence/0334/4254/d10_large30.sh OUT SRVBIN`, with both builds named.
- **On release-43 `CF981126` (the proof), 2026-11-15 10:11:49-10:18:55, 426 s: PASS in all three parts**
  (`evidence/0334/4254/d10-p9aa30f1/report.txt`).
  - Part 1: the four inputs are identical with only the version-string exception.
  - Part 2: x86 direct = x64 direct, turns 0-100, 164,110 rows.
  - Part 3: the gates are identical (146,025 and 144,085 rows), and the four round trips are byte-identical.
- The same on the old DLL `3E862705`, 08:36:50-08:45:24, PASS with exactly the same row counts (`d10-run2/`).
- A first run failed parts 1 and 3 for a reason in the tool: c5h was given `--mods-dir None` for the x64 saves. Since
  issue 0303 those saves list the mods, and run_d10 makes its mods folder only for the huge game. Fixed in `a841b235`
  (section 4e there).
- So the 43 variant keeps the save test's three identities, with players 22-29 seated as majors.

**4. The 22/43 cross-load, server headless** [L, 2026-11-15 08:27-08:29; `evidence/0334/4254/xload.py`, `xload/`].
- Inputs, copies with their own names:
  - a 22 save: `X4254-22-D10-TINY-x64-T25`, a copy of 9045's `D10-TINY-x64-T25` (Tiny, turn 25, pin DLL);
  - a 43 save: `X4254-43-LARGE30-T25`, the 30-major game played by the server with the 43 DLL to turn 25, 706,721 B.
- Each arm runs `run_server.py --load <copy> --turns <turn + 1>` under `shell/tools/memcap.py` (6 GB).

| Arm | Result | `load:` lines (srv_save.cpp) | DLL log (custommods.log) | Where it stops (symbolised, `xload/symbolized-frames.txt`) |
|---|---|---|---|---|
| 22 save, 22 DLL (control) | exit 0, passed, turn 26 played | Game::Read used 14,257 B; pregame and body "used up exactly" | none | - |
| 22 save, **43 DLL** | **exit 2**, `FAILED: exception in phase load`, 13.6 s | no "Game::Read used" line | "Potential savefile format mismatch, gamecore is version ...+m43 Copy" | C++ exception 0xE06D7363 inside `CvGame::Read` -> `readSaveGameDB` (`CvGame.cpp:11133`): the save database's size, read from the wrong bytes, asks the stream for more than the body holds; the server's `FMemoryStream::ReadIt` raises it (`srv_save.cpp:185`) |
| 43 save, **22 DLL** | **exit 2**, same line, 14.2 s | **Game::Read used 16,073 B** of the 30,061 it should: the game section is misread silently | the same line, version "...g04b1211b8 Copy" | the next read, `CvTeam::Read` (team 0's `m_pavProjectArtTypes` vector, `CvTeam.cpp:9258`): a count taken from the wrong bytes runs the stream past its end (`srv_save.cpp:193`) |
| 43 save, 43 DLL (control) | exit 0, passed, turn 26 played | Game::Read used 30,061 B; "used up exactly" | none | - |

- No minidump in any arm: these are C++ exceptions that the server catches; it reports them, it does not crash. The
  memory cap was never reached (peak working set 150-200 MB).
- This matches 5.3. The 22 save fails in the 43 DLL before the map is read. The 43 save is misread silently in the
  22 DLL for the whole game section and fails one structure later. With the server's own stream it is never a
  playable wrong game.
- The DLL's only reaction is the log line (5.1).

**5. The same through the proxy and the EXE** [L, 2026-11-15 08:46-08:50, instance c5h-c (Goldberg, hidden desktop,
claimed and restored); `live_xload.py`, `live/`].
- Set-up: the 43 layout installed with `provision.py set-proxy` (development layout, no pack, no purity file).
  The load, from the load menu by the harness, was `X4254-22-TINY_0008`, a copy of the instance's 22-build save
  `C5H-3600-TINY_0008` (`b2ceb663...`, CP v151 + VP v17, turn 8).
- What happened, in order:
  - The proxy booted the server. The server read `MAX_MAJOR_CIVS 43` (8.2) and the database replay was identical.
  - The body was pulled as stream session 3 at 36.8 s after the boot. The server's DLL logged the mismatch line.
  - The EXE's main thread called `terminate()`, then `abort()`. The CRT box "Microsoft Visual C++ Runtime Library:
    Runtime Error! ... terminate it in an unusual way" was open at 76 s after the launch (`wait_game.py` exit 8).
  - The server stayed alive, blocked in `kRtE_StreamRead`. When the box was answered, it logged `PeerDied` and exited
    with code 3.
  - No `VP64/reports` folder was written, and no D118 popup was shown.
- The picture is `vc-20261115-084920-workstation-def1` (DIAGNOSTIC, result fail). The window still shows the main
  menu frame from which the load started; the box is a separate window, cloaked on this test desktop.
- The dump of the EXE with the box open: `F:/c5-64bit-work/0334/4254/dumps/c5h-c-x86-28800-20261115-085021.dmp`
  (1.84 GB, SHA-256 `62de1439...`, kept on F: and not committed: it holds game state).
  - Main thread: `terminate` <- EXE 0x42DEDB, then `abort`, then `MessageBoxA`. VAs at ImageBase 0x400000; the module
    was at 0x890000 in this process.
  - In the stack memory above that frame [C dump words; I order]: `msvcr90!CxxThrowException` called from EXE code at
    0x82B4E5 (via 0x830D3A). Above it, the proxy's `OnCall` (+0x2e1, +0x308) and `vpipc::wait_change` /
    `Channel::end_call`, that is a served call on the EXE's main thread (`dump-thread0-above-esp.txt`).
  - With the server log (the call it was in was `kRtE_StreamRead`) and the source [C], the path is: the proxy's
    `kRtE_StreamRead` branch (`proxy_rt.cpp:913-930`), then `FDataStream::ReadIt` of the engine stream (size unknown,
    a pull session), then the EXE's stream throws on the read past the end. That branch has no catch, so the
    exception unwinds into the EXE's frames, and the EXE calls `terminate`.
  - The issue 9036 catch (`exe_exception`, then the D118 path) covers only the `else` branch of `OnCall`.
  - [I] The EXE throws at a read past the end the way the server's stub does (`FDataStream::RaiseException`). The
    exception's type was not read from the dump.
- Closing it took the MC's word: `cli.py dismiss` refused ("needs a decision": the Lua channel is silent during a
  load). The given `cli.py stop --coordinator-override` refused because a dialog was open. `cli.py dismiss --button OK
  --coordinator-override "lead-0334 on the MC's word: ..."` (D60, #1657) exited 0. The instance was restored by hash:
  verify ok, the snapshot diff empty, every standard backup entry equal.
- Not run: the reverse through the proxy (43 save, 22 layout) and the stock arm with the two 32-bit DLLs (the
  brief's "if time allows"). By the headless result and 5.3, the reverse would stop in the same branch one structure
  later [I].

**Not verified:**
- Whether a pack with a purity file changes the proxy arm. It should not: the failure is in the stream pull, before
  any purity use [I].
- How upstream's 32-bit game behaves on a cross-variant load. The stock arm was not run.
- The parity pair on other maps or major counts than the one 30-major Large game: the stage's four 22-or-fewer games were not rerun on the 43 build.

### 8.4 The 43-civilisation games, the stock arm and the leader-scene stall check (4255)

Written 2026-11-15 07:47-13:30 EST by the 4255 worker (account B). Workstation runs on the Goldberg instance c5h-h
(F:, pool owner 4255); timing and memory here are workstation indications (COMMON 23, D106), not product claims.
Evidence and scripts: `evidence/0334/4255/` (`leader_stall.py`, `pp_4255.py`, `summarize.py`); run folders under
`F:/c5-64bit-work/0334/4255/` (not committed: `per-turn.csv`, `memory.csv`, `run.json`, `summary.json`, logs; the
turn-0 save copies in `saves/` with `SHA256.txt`).

**Summary [L].** On the Hormigas map with the 43 build: **42 AI majors + the observer + 15-17 city states** (20 asked;
the pack removes the ones without a start plot), **180 AI turns clean on both arms**: the D33 product path (team head
layout `x64-04b1211b-m43-p9aa30f1`) and the stock 32-bit EXE with the 32-bit 43 DLL. At turn 180 the EXE holds 1,749 MB
private through the proxy against 2,551 MB on stock (free address space 1,613 against 646 MB); the x64 server holds
2,790 MB. A smaller map first (Huge Continents, 30 majors + observer + 20 city states, 5 turns) and a 22-build dry run
(21 + observer, 4 turns) ran clean. **The leader-scene stall check: no stall** on either arm: a major above slot 21
shows VP's static picture, holds the game update while it is up (like a 3D leader), and the AI turn completes once
the screen is closed through the game's own means (8.4.3, 8.4.9, 8.4.11).

**8.4.1 The plan follows the DLL's limits at run time (`tools/stressmap/`) [C: code; L: section 8.4.2].** The map
pack's front end (`ptwe.py` `c5StartMapPack`, driven by `mappack.py prepare` and `calibrate.py --pack`) no longer seats
the 21-civilisation PTWE plan of 9069. With `civs=auto` (the default) it reads `GameDefines.MAX_MAJOR_CIVS` /
`MAX_MINOR_CIVS` in the front end's Lua (the DLL writes them into `Defines` at load, 3.2) and seats, in slot order, the
pack's `Civilizations_GiantStartPosition` rows that the database has as playable, at most `MAX_MAJOR_CIVS - 1` of them;
the top major slot stays closed, which is the slot VP's `setAIAutoPlay` takes for the observer (it searches closed
major slots from the top, `CvGame.cpp` `setAIAutoPlay`). `minors=max` asks for `MAX_MINOR_CIVS` city states. The DLL
draws them (`CvPreGame::onGameStarted`, which draws only for slots left at `NO_MINORCIV`, `CvPreGame.cpp:2606-2612` at
`04b1211b8`), and the pack's in-game script removes every city state without a start plot (`MapPackInGame.lua`
`FinalizeCityState`), so fewer are alive than asked. **They cannot be preset from the front end [L, 2026-11-15
07:55]:** the EXE's Lua `PreGame` has no `SetMinorCivType` (its civilisation functions are `Get/SetCivilization*`,
`Get/SetNumMinorCivs` and the `Civilization*` text and package keys, listed live on c5h-h), so a first version that
preset the slots logged "PreGame.SetMinorCivType is missing" and was removed. Of the 45 city states in the database
33 to 37 have a start plot (8.4.2), so a draw of 20 leaves about 16 alive [I]. The pack's start table has 43 civilisations in this order: America, Arabia, Aztec, China, Egypt, England, France, Germany,
Greece, India, Iroquois, Japan, Ottoman, Persia, Rome, Russia, Siam, Songhai, Babylon, Mongolia, Inca, Spain, Polynesia,
Denmark, Korea, Austria, Byzantium, Carthage, Celts, Ethiopia, Huns, Maya, Netherlands, Sweden, Portugal, Venice,
Poland, Brazil, Assyria, Indonesia, Morocco, Zulu, Shoshone; its city-state table 55 rows (some under names the
database no longer has). So the 22 build gets the first 21 (America to Inca) + the observer + 41 city states asked,
the 43 build the first 42 (all but Shoshone) + the observer + 20 asked. The front end logs one line `c5 4255: plan
MAX_MAJOR_CIVS .. MAX_MINOR_CIVS .. MAX_CIV_PLAYERS ..: N majors in slots 0-.., first closed major slot .., M city
states asked` in `Lua.log`,
and `calibrate.py` records `GameDefines.MAX_*` with the turn-0 facts. Per turn it also samples the EXE's child
`server.exe` when there is one (the product path: `server_private_mb`, `server_ws_mb`, `server_peak_ws_mb`) and writes
`turn_s`, the wall time between two turn rows.

**8.4.2 The 22 dry run (item 3) [L, 2026-11-15 07:50-07:55 EST].** Stock route on c5h-h: the stock EXE with the pinned
instance-aware 32-bit DLL `i0ship-26234371-ia` (sha256 3c2f47da...), mods cp, vp, eui and the pack, Huge, Standard,
Prince, victories off, max turns 2000. `python -I tools/stressmap/calibrate.py c5h-h --pack --out
F:/c5-64bit-work/0334/4255/dry22-stock --turns 4 --save-name 4255-DRY22-STOCK-T000` (an earlier commit of the tools,
with the preset, which logged the missing setter and changed nothing else). Exit 0: plan line `MAX_MAJOR_CIVS 22
MAX_MINOR_CIVS 41 MAX_CIV_PLAYERS 63: 21 majors in slots 0-20, first closed major slot 21, 41 city states`; at turn 0
21 majors alive (America to Inca in slots 0-20, the table order) and 33 city states (the pack's script: "Requested 41,
Actual 33"; 8 "No TSL" lines); autoplay armed (4), 4 AI turns, handed back at turn 4, the harness's safe stop rc 0.
Load 141 s; turn rows 32.1 / 11.6 / 15.6 / 11.6 s; the EXE's private bytes 2,293 MB at turn 0 -> 2,354 MB at turn 4,
free address space 920 -> 849 MB (largest block 874 -> 802 MB). No server child (stock), so the server columns are
empty, as they should be. The tooling is sound on the 22 build; the turn-0 save `4255-DRY22-STOCK-T000` stays in the
instance's `ModdedSaves/single` until the restore.

**8.4.3 The leader-scene stall check: method and the 22 control (item 6) [L, 2026-11-15 08:02-08:10 EST].** Driver
`evidence/0334/4255/leader_stall.py` on a running game with the human in slot 0. Stimulus
`Players[N]:DoForceDenounce(0)` (`CvLuaPlayer::lDoForceDenounce`: the denouncement, then
`gDLL->GameplayDiplomacyAILeaderMessage(N, DIPLO_UI_STATE_BLANK_DISCUSSION_MEAN_AI, ...)` when the human is active),
fired from a `GameEvents.PlayerDoTurn` listener at player N's own turn, i.e. inside the AI turn (the 0335 case). The
listener must be added in the `InGame` Lua state: `GameEvents` is nil in the channel's default state. If N has not met
the human, the driver makes the teams meet first (`Teams[..]:Meet(.., true)`), and **VP's first-contact greeting is a
leader message of its own** (`DIPLO_UI_STATE_DEFAULT_ROOT`, "We are pleased to meet you" / "I bid you welcome"): the
channel went silent at once and stayed silent until the screen was closed, so the meet is measured as a phase of its
own. The screen is closed by the harness's in-game diplomacy handler (`tools/harness/diplo.py`, armed over the channel)
after `--hold` seconds, through the screen's own close function; the driver polls the channel every 2 s and records the
silent span. Control on the 22 stock game (save copy `4255-DRY22-STOCK-T000`, turns 0-2), ids <= 21 (3D scenes):
- England (5), meet, no handler armed (the first version armed it after the meet): the 3D scene with Elizabeth
  (diagnostic shot `stall22-stock-p5/after-meet.png`), the channel silent until one Esc from the harness
  (`cli.py key {ESC}`) closed it; the channel answered 3 s later.
- Germany (7), meet, hold 30 s: silent 4.0 -> 31.9 s, the handler answered `DEFAULT_ROOT` after 30.0 s, done after
  32.0 s; shot `stall22-stock-p7/leader-p7-meet.png`.
- Germany (7), AI turn, hold 30 s: the hook fired, the channel went silent 8.3 s after the end of turn and answered
  again at 36.2 s (held 27.9 s); the handler answered `BLANK_DISCUSSION_MEAN_AI` ("The answer is still no.") after 30.0
  s; the AI turn then completed (turn 2, human active) at 48.7 s. Without a screen the same AI turn (0 -> 1) took about
  20 s with the channel answering throughout.
So on the 22 build a leader message holds the game update for exactly as long as its screen is up, and the turn
completes once the screen is closed through the game's own means. The 43 runs (8.4.6) repeat this for ids above 21.

**8.4.4 The product path set-up for the 43 games.** `evidence/0334/4255/pp_4255.py prep` (run_3308.py's prep without
its save copy; restore unchanged): the D33 product path, a **development layout** (`provision.py set-proxy`, `pack=0`;
not the shipped pack with `pack=1`): proxy + `server.exe` from the `--bin` folder, `VP64Options.ini` `[Renderer]
ServerRenderer = 1`, the composite (built from this branch, `shell/build.cmd`, sha256 a866c5e9...) with its manifest,
config.ini D33 with Overlay on (D93), `vpproxy.ini` mode=on local_answers=1. The x64 DLL is 4252's 43 build (layout
`F:/c5-64bit-work/0334/4252/layout/x64-04b1211b-m43-p3dd1c97/bin`, sha256 3e862705...d631, `+m43`). Frame cap on
(the harness's launch), lifted by calibrate during autoplay as for every memory run. **Correction to that layout's
README [L, 2026-11-15 08:13]:** its `server.exe` (sha256 d8943d85...) logs `build: repo 90363f307 dirty=1 ... built
2026-11-13T16:28:22+0000` as its first `server.log` line, not repository commit 3dd1c972 as the README says; it was
used only for item 2 below. The 43 games after it use a server and proxy built from this branch at the team branch's
4253 merge (8.4.5).

**8.4.5 A smaller map first (item 2) [L, 2026-11-15 08:11-08:16 EST].** Product path as 8.4.4 with that layout's own
server and proxy (proxy sha256 b52d6f1d...). `calibrate.py c5h-h --new-game
"map=Continents;size=WORLDSIZE_HUGE;ais=29;minors=20;maxturns=2000" --out F:/c5-64bit-work/0334/4255/small43-pp
--turns 5 --save-name 4255-SMALL43-PP-T000`. Exit 0. **The EXE's Lua reads the 43 DLL's limits through the remote
database [L]:** `GameDefines.MAX_MAJOR_CIVS` 43, `MAX_MINOR_CIVS` 20, `MAX_CIV_PLAYERS` 63 in the turn-0 facts (4.3's
"I live" is now confirmed). Huge Continents (128 x 80), 30 majors alive in slots 0-29 (ids 22-29: England, Korea,
Denmark, Mongolia, Persia, America, Spain, Polynesia) and 20 city states; autoplay armed (5) with the observer in a
closed slot above 29; 5 AI turns, handed back, safe stop rc 0. Load 138 s. Turn rows 21.1 / 10.7 / 15.6 / 10.5 / 11.7 s.
The EXE: private 1,485 -> 1,536 MB, free address space 1,885 -> 1,830 MB (largest block 1,838 -> 1,784 MB); the server:
private 1,601 -> 1,619 MB, working set 607 -> 622 MB (363 MB private right after the load, before the first turn's
sample). 0 LOUD lines in `server.log`, `render.log`, `vpproxy.log`; `Lua.log` only the known `VPUI_loader.lua` noise
(run-and-drive-the-game.md 2). Workstation indications only (COMMON 23).

**8.4.6 The Hormigas game on the product path, old tools line (item 4, first run) [L, 2026-11-15 08:21-09:56 EST].**
Product path as 8.4.4 with `server.exe` and the proxy built from this branch at `085ed87d` (the team branch's 4253
merge `abd4b000`; release, 4252's m43 deps, `dirty=0`; server sha256 ff885784..., proxy 7afbaeeb...; x64 DLL
3e862705..., m43). **Labelled "old tools line": built before main 74118dfc's server and proxy changes.** While it
ran, the MC decided (#1702, relayed by the lead) that the product-path result must use the team head's rebuild
(layout `x64-04b1211b-m43-p9aa30f1`, 8.4.7). (Corrected 2026-11-15 10:08 on the lead's word: the first label said
"before main's 0343 `.Civ5Map` reader"; the DLL of this run already carried that reader, since 4252 built it from a
main that had 0343.) `calibrate.py c5h-h --pack --out F:/c5-64bit-work/0334/4255/pp43-hormigas --turns 400
--turn-timeout 600 --save-name 4255-PP43-HORMIGAS-T000`, ended at turn 177 by `Game.SetAIAutoPlay(1, 0)` (the MC's
decision above), handed back at turn 178, the harness's safe stop, exit 0.
- `server.log`: `player limits: MAX_MAJOR_CIVS 43, MAX_MINOR_CIVS 20 (the DLL's, read from Defines); MAX_PLAYERS 64,
  MAX_TEAMS 64, MAX_CIV_PLAYERS 63` (4253's boot line). The EXE's Lua: `GameDefines` 43 / 20 / 63.
- Turn 0: **42 majors** alive in slots 0-41 (America ... Zulu, the pack table's first 42), observer in slot 42,
  **17 city states** alive of 20 asked (3 drawn without a start plot, removed by the pack); load 124 s. Turn 178: 42
  majors, 16 city states, 247 cities, 1,520 units.
- **178 AI turns clean**: no crash, no stall (no turn over 68 s; the 600 s stall limit never came near), 0 LOUD lines
  in `server.log`, `render.log`, `vpproxy.log` (copies in the run folder's `vp64-run/`). `Lua.log` was not kept (the
  next launch overwrote it; later runs copy it).
- Turn time (wall time between turn rows, autoplay, frame cap lifted; the workstation busy with other teams' games and
  builds): mean 14.6 s (turns 1-20), 22.9 s (21-100), 43.4 s (101-178), max 68.0 s.
- Memory per turn (`per-turn.csv`; slopes by `evidence/0334/4255/summarize.py`, OLS, MB per turn +- one standard
  error): the EXE (32-bit, through the proxy) private 1,527 MB at turn 0 -> 1,617 (t20) -> 1,685 (t100) -> 1,762 MB
  (t178), slope 0.82 +- 0.02 (20-100) and 1.09 +- 0.03 (100-178); free address space 1,833 -> 1,551 MB, largest free
  block 1,710 -> 1,476 MB (-1.06 MB per turn over 100-178). The server (x64) private 2,112 MB at turn 0 -> 2,822 (t20)
  -> 2,863 (t100) -> 2,904 MB (t178), 0.50-0.51 MB per turn after turn 20; working set 887 -> 1,022 MB. Most of the
  server's growth comes in the first turns (+710 MB by turn 20); this ticket does not say what fills it [I: the
  renderer's bake and caches are the likely candidates, not checked].

**8.4.7 The product-path layout of the result.** The team head's rebuild, `F:/c5-64bit-work/0334/rebuild/layout/
x64-04b1211b-m43-p9aa30f1` (lead-0334; team head `9aa30f190`, `dirty=0`; x64 DLL sha256 cf981126..., smoke "43";
`server.exe` d529d5d0... logging `build: repo 9aa30f190 dirty=0`; proxy d6a43056...), used as it is with
`pp_4255.py prep` (8.4.4: development layout, `pack=0`, D33, composite a866c5e9...; no `shell/` change between the
composite's commit and `9aa30f19`).

**8.4.8 The stock arm (item 5; a comparison arm, COMMON 18) [L, 2026-11-15 09:58-11:38 EST].** The stock 32-bit EXE
on c5h-h with the 32-bit 43 DLL `i0ship-26234371-m43-ia` (4252; sha256 204fbfcc..., version `Release-5.4.8-39-
g26234371e+ia440878983+m43 Copy`, instance-aware; installed by `provision.py set-dll`, put back afterwards), the same
plan (`calibrate.py c5h-h --pack --out F:/c5-64bit-work/0334/4255/stock43-hormigas --turns 180 --save-name
4255-STOCK43-HORMIGAS-T000`). (4252's first 32-bit 43 build `i0ship-26234371-m43`, 6e2a7be1..., is not
instance-aware, 0 `VPLuaExec_` strings, so the harness's Lua channel cannot reach it; the `-ia` twin was built for this
arm.) Exit 0. Plan line `MAX_MAJOR_CIVS 43 MAX_MINOR_CIVS 20 MAX_CIV_PLAYERS 63: 42 majors in slots 0-41, first closed
major slot 42, 20 city states asked`; turn 0: the same 42 civilisations in the same slots, **16 city states** alive
(the draw differs per game). **180 AI turns clean** (no crash, no turn over 62.4 s, handed back at 180, safe stop);
turn 180: 41 majors (one eliminated), 13 city states, 246 cities, 1,470 units. Load 107 s. `Lua.log` (copied to the run
folder): about 14,900 runtime errors, all of the known UI kinds (`TechButtonInclude.lua:199` `GetHelpTextForUnit`
10,149, `:234` `GetHelpTextForBuilding` 4,730, `VPUI_core.lua:151` `DB` 26, the pack's `MapPackFunctions.lua:90`
`Events` 24, `VPUI_loader.lua` 2; civ5map-format.md), none from the DLL's game script; the product path's rerun
(8.4.10) has the same kinds in the same numbers. (Corrected 2026-11-15 13:18: the first text said "50 tracebacks",
which counted `stack traceback` lines, not errors.)
- Turn time: mean 13.5 s (1-20), 22.8 s (21-100), 47.2 s (101-180), max 62.4 s.
- The EXE's memory (32-bit, the whole game in one process): private 2,249 MB at turn 0 -> 2,330 (t20) -> 2,439 (t100)
  -> 2,551 MB (t180); slope 1.55 +- 0.07 MB per turn (20-100), 1.68 +- 0.06 (100-180). Free address space 1,021 ->
  861 -> 774 -> 646 MB, largest free block 966 -> 646 -> 615 -> 501 MB (-2.48 +- 0.20 MB per turn over 100-180).
  The 4 GB ceiling was not reached in 180 turns. A straight line from turn 180 (646 MB free, -2.07 MB per turn over
  100-180) to 9069's death line of about 70 MB free (giant-stress-game.md 5) gives about turn 460 [I: a linear
  extrapolation; growth speeds up later in a game, so the real death is likely earlier].
- Against the product path's old-tools-line run (8.4.6, same plan, 42 + 17): at turn 178-180 the EXE's private bytes are
  2,551 MB on stock against 1,762 MB through the proxy (-789 MB), and its free address space is 646 against 1,551 MB; the x64
  server holds 2,904 MB private on its own side. Turn times were within a few seconds of each other in every window
  (workstation indications under other teams' load, not a speed claim, COMMON 23).

**8.4.9 The leader-scene stall check on stock with the 43 DLL (item 6) [L, 2026-11-15 11:39-11:44 EST].** Driver of
8.4.3 on the stock arm's turn-0 save copy `4255-STOCK43-HORMIGAS-T000` (the stock EXE, `i0ship-26234371-m43-ia`),
hold 30 s, `--mode ai`:
- **Attila (Huns), player 30, id above 21.** Meet: the first-contact greeting (`DEFAULT_ROOT`) held the channel 4.0 ->
  32.1 s (28.1 s), the handler answered after 30.0 s. AI turn: the hook's denouncement (`BLANK_DISCUSSION_MEAN_AI`,
  "We've been over this before. The answer is still no.") held it 15.0 -> 40.5 s (25.5 s), answered after 30.0 s;
  **the AI turn completed** (turn 1, human active) at 53.3 s. `Lua.log`: `DiscussionDialog`, `DiploTrade` and
  `LeaderHeadRoot` each logged `LeaderMessageHandler: Player ID > 21` for both messages, i.e. VP's static-picture
  path (3.2). What the screen shows (diagnostic shots `stall43-stock-p30/leader-p30-meet.png`, `leader-p30-ai.png`,
  Visual Checks feed): a full-screen picture of Attila in his throne room, the leader name, the message and its two
  answer buttons ("You'll pay for this in time.", "Very well."), i.e. a complete diplomacy screen, not a blank or
  missing one.
- **Elizabeth (England), player 5, control in the same game.** Meet held 27.4 s, AI-turn denouncement 24.8 s, both
  answered after 30 s, turn completed at 49.6 s; then the harness's safe stop (rc 0).
**Answer for stock: no stall.** A major above slot 21 holds the game update while its (static) screen is up, exactly
like a 3D leader below 22, and the AI turn completes once the screen is closed through the game's own means. The 0335
stall (a leader screen that fails to appear) did not occur for ids 22-42 on the 43 build.

**8.4.10 The Hormigas game on the product path, the result (item 4) [L, 2026-11-15 11:45-13:16 EST].** Layout of
8.4.7. `calibrate.py c5h-h --pack --out F:/c5-64bit-work/0334/4255/pp43c-hormigas --turns 180 --turn-timeout 600
--save-name 4255-PP43C-HORMIGAS-T000` (180 turns: the stock arm's count). Exit 0.
- `server.log`: the 4253 boot line `player limits: MAX_MAJOR_CIVS 43, MAX_MINOR_CIVS 20 (the DLL's, read from
  Defines) ...`; the EXE's Lua `GameDefines` 43 / 20 / 63; plan line 42 majors in slots 0-41, slot 42 closed, 20 asked.
- Turn 0: **42 AI majors** (America ... Zulu) + the observer + **15 city states** alive (5 of the 20 drawn had no start
  plot). Turn 180: 42 majors, 14 city states, 254 cities, 1,515 units. Load 107 s.
- **180 AI turns clean**: no crash, no stall (longest turn 89.9 s, against the 600 s limit), handed back at turn 180,
  the harness's safe stop rc 0; 0 LOUD lines in `server.log`, `render.log`, `vpproxy.log` (copies in the run folder's
  `vp64-run/`); `Lua.log` the same UI noise as stock (8.4.8), nothing from the game script.
- Turn time (indication, the workstation busy): mean 11.6 s (1-20), 18.1 s (21-100), 45.5 s (101-180).
- Memory per turn (`per-turn.csv`, `summary.json` by `summarize.py`): **the EXE** private 1,518 MB at turn 0 -> 1,592
  (t20) -> 1,653 (t100) -> 1,749 MB (t180), slope 0.85 +- 0.02 MB per turn (20-100) and 1.05 +- 0.04 (100-180); free
  address space 1,847 -> 1,613 MB, largest free block 1,743 -> 1,545 MB. **The server** private 2,256 -> 2,701 ->
  2,742 -> 2,790 MB, 0.52-0.54 MB per turn after turn 20; working set 769 -> 899 MB.
- **Against the stock arm at turn 180** (8.4.8, the same 42 civilisations, 16 city states there): the EXE's private
  bytes 1,749 MB against 2,551 MB (-802 MB), free address space 1,613 against 646 MB, largest free block 1,545
  against 501 MB; the EXE's free space falls 1.37 MB per turn (100-180) against 2.07 on stock. The x64 server holds
  2,790 MB private outside the 4 GB window. Turn times are within a few seconds of stock in every window. Against the
  old-tools-line run (8.4.6) the numbers agree within 15-140 MB (different city-state draws and AI choices).

**8.4.11 The leader-scene stall check on the product path (item 6) [L, 2026-11-15 13:17-13:23 EST].** Layout of
8.4.7, the result run's turn-0 save copy `4255-PP43C-HORMIGAS-T000`, hold 30 s, `--mode ai`, as 8.4.9:
- **Attila (Huns), player 30:** meet held 4.0 -> 31.7 s (27.7 s), AI-turn denouncement held 14.9 -> 42.8 s (27.9 s),
  both answered by the handler after 30.0 s, **the AI turn completed** (turn 1, human active) at 52.0 s. `Lua.log`:
  six `LeaderMessageHandler: Player ID > 21` lines (three screens, two messages). The screen (diagnostic shots
  `stall43-pp-p30/leader-p30-meet.png`, `leader-p30-ai.png`; whole window through PrintWindow, not flat, the
  interface's top-right icons present) is the same as on stock: Attila's full-screen picture with the message and its
  two answer buttons.
- **Elizabeth (England), player 5, control:** meet 28.2 s, AI turn 24.2 s, turn completed at 48.3 s; safe stop rc 0.
**Answer for the product path: no stall,** the same behaviour as stock (8.4.9) and as the 22 build (8.4.3). Ticket
0355's question (does a leader that cannot show a 3D scene stall the AI turn on the 43 build?) is answered no for
both arms; the EXE-side trace F4 is still open [I: why the EXE shows no 3D scene above 21 is not traced here].

Sources: the fork clone `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL` (read only; read at named commits with
`git -C <clone> show <commit>:<path>`), this repository's `proxy/`, `server/`, `tools/d10/`, the knowledge base files
named in each section.
