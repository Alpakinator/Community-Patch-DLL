# Upstream fixes made only in Vox Populi's mod Lua: #13463, #13467, #13539 (queue ticket 0462, 2026-11-17)

Tracker `tracker/issues/0462-upstream-mod-lua-fixes.md`. Source of the three: `upstream-fix-batch-2.md` section 2
(one-off 9075), which marked them "not applicable (mod files)" because the DLL pin batches cannot carry them. This file
answers, for each: what it fixes, whether the Lua it changes runs on our product path, whether Vox Populi 5.4.8 (our
pinned VP) has it, and how a player gets it. Reading only; no game was run. Marks: [C] confirmed by source or file,
[L] verified live, [I] inferred.

## 1. Result

| Upstream | Fixes | On our product path | In VP 5.4.8 | Recommendation |
|---|---|---|---|---|
| #13463 (475bd4997, merged 2026-11-09) | crash when loading a game from inside a game: UI callbacks registered while another context's Lua runs are released after that context is gone | **yes**, the same as on the stock route: the files are the EXE's UI Lua, which the product leaves in the EXE | **no** | **a known-issue line in the beta notes** (text in section 3.3), removed when the pinned VP has the fix; the fix itself reaches players with VP's next release |
| #13467 (14d16fa3d, merged 2026-11-10) | Lua queries left on their first row keep statements open on the game database; a mod-change rebuild then fails half built | **yes**; already handled by ticket 0184 | **no** | **nothing new**: 0184's known-issue line (`../06-tooling-and-workflows/installer-notes.md` section 8a) already covers it; remove it when the pinned VP has the fix |
| #13539 (aa7a29b38, merged 2026-11-10) | `Events.ClearHexHighlights` on maps wider than 181 plots writes past the EXE's plot highlight grid (crash) | **yes**, the same as on the stock route, but only on a map wider than 181 plots, which no standard map size reaches | **no** | **nothing for players** (waiting for VP's next release); for the project: keep test and stress maps at most 181 plots wide (section 4.4) |

None of the three can be carried by the DLL pin: all are in mod files (`.lua`) that Vox Populi's own installer puts in
place. The patch does not ship Vox Populi's files (MC decision on ticket 0184, 2026-11-14; the installer's shipping list,
`../06-tooling-and-workflows/installer-notes.md` section 3, D171), so a player gets each fix only from VP's next release.

**Changed 2026-11-21 (the owner, D225; ticket 0638):** the patch may ship these three fixes if the override is clean. The
recommendation column above is history from 2026-11-17. Section 7 has the delivery scope: which files, which copy each
install form loads, how to deliver them, what happens when VP is updated, and the verdict. Until 0638's part 2 has
merged, the patch still ships no VP file and the known-issue lines stay.

## 2. Method [C]

- Pull requests read on 2026-11-17 with `gh pr view` and `gh pr diff` (LoneGazebo/Community-Patch-DLL).
- Whether VP 5.4.8 has them: GitHub's compare of `Release-5.4.8` (commit 7307aefdf, 2026-10-16) against each merge
  commit gives `ahead` with `behind_by = 0` (475bd4997: 43 ahead, 14d16fa3d: 90, aa7a29b38: 79), so none of the three
  is in 5.4.8. 5.4.8 is still VP's latest release (`gh release list`, 2026-11-17). The 5.4.8 source tarball's Lua
  files (Release-5.4.8, scratch copy outside the repository, not committed, D83) carry every line the three pull
  requests change, in the form before the fix (lines named below).
- The installed copies: a VP 5.4.8 install read only (CP modinfo version 151, EUI from `UI_bc1`, VP's EUI
  compatibility files): the same pre-fix lines [C, file read 2026-11-17].
- Product path: the knowledge base's description of what runs where (sections 3.2, 4.2 and 5).

## 3. #13463: crash when loading a game from inside a game

### 3.1 What it fixes [C, the pull request; mechanism I from its text]

A UI Lua callback (a `LuaEvents`/`Events` handler or a control callback) belongs to the Lua context whose Lua was
running when it was registered. When context A's code runs while context B's Lua is on the stack (B raised a
`LuaEvents` that A listens to), what A registers there belongs to B. When a game is loaded from inside a game, the
contexts are torn down; a callback released after its owning context is gone calls `luaL_unref` on a freed
`lua_State`, and the EXE crashes. Three places do this in 5.4.8:

| File (5.4.8) | Line | What registers in a foreign context | The fix |
|---|---|---|---|
| `(1) Community Patch/LUA/DiploCorner.lua` | 84 | `LuaEvents.RequestRefreshAdditionalInformationDropdownEntries.Add(RefreshAdditionalInformationEntries)`: the refresh builds the dropdown's callbacks on the Lua of whichever context requested it (CP's EventOverview, EspionageOverview, LeagueOverview and ReligionOverview request it) | the event only sets a flag; DiploCorner refreshes in its own `ContextPtr:SetUpdate` |
| `(3a) VP - EUI Compatibility Files/LUA/DiploCorner.lua` | 81 | the same | the same; EUI's diplo corner buttons are built from that refresh |
| `(3a) VP - EUI Compatibility Files/LUA/NotificationPanel.lua` | 1834, 1837 | requests the refresh at start-up (so EUI's buttons were built on NotificationPanel's Lua) | lays out again after the deferred refresh; `GameplayAlertMessage` handler added always and checks the alert button inside |
| `(4a) Squads for VP/UI/SquadNames.lua` | 130-131 | click callbacks on `/InGame/GameMenu/QuickSaveButton` and `SaveGameButton`, which the game menu destroys after Squads is gone | `ContextPtr:SetShutdown` clears them first |
| `(4a) Squads for VP/UI/Squads.lua` | 64, 75, 82 | `Events.SerialEventMouseOverHex` added and removed from `SetSquadsMode`, which can run from WorldView's events (so the handler belonged to WorldView) | added once at file load; the handler returns at once when no squad mode is on |

It is a use after free, so whether a given load crashes depends on what the freed memory holds [I]. Which UI the player
uses decides which file matters: with EUI the `(3a)` copies run; without EUI the `(1)` DiploCorner; Squads only with the
optional `(4a)` component. (On the EUI install read here, CP's `LUA/DiploCorner.lua` is not on disk although CP's modinfo
lists it; the `(3a)` copy is the one present [C, file read].)

### 3.2 On our product path [C for the split, I for the crash]

The product keeps all UI in the EXE (D80): every UI Lua context is the EXE's own, made, run and destroyed by the EXE
as on the stock route; the patch changes no UI Lua file and no context's life cycle. So the defect is on the product
path exactly as on the stock route. Nothing in the server is involved (the DLL's Lua state is not a UI context).

Our own Lua code in the EXE already follows the rule the fix introduces: the proxy's UI bridge registers its `Events`
handlers with `lua_cpcall` on the `InGame` context's own thread (`proxy-design.md` section 24.3), and a load from inside
the game re-installed it in the new `InGame` context with 0 stale calls [L, ticket 0178, 2026-11-14].

A crash that fits this defect was seen before this project began: a `luaL_unref` writing through a bad pointer during
UI teardown after an in-game load (`../sources/memory-notes/civ5-exe-message-queue-overflow.md`, the 12:44 crash; not
analysed then) [I that it is this defect].

### 3.3 Recommendation

The fix reaches players with VP's next release; the patch does not ship VP's files. Because loading a save from the
game menu is a common action and a beta tester would blame the new patch for the crash, add one known-issue line to
the beta notes, beside 0184's (text below); remove it once the pinned VP release contains 475bd4997.
For the project's own testing: a crash in UI teardown during an in-game load, on either route, is this stock defect
until shown otherwise; a human-input journey that loads from the game menu should expect it on VP 5.4.8 [I].

**Proposed known-issue line for #13463** (for the beta notes, beside section 8a; the MC decides; no internal ids, D95):

> With Vox Populi 5.4.8, loading a save from the game menu while a game is running can crash the game, with or
> without the 64-bit patch. Vox Populi has fixed this for its next release. Until then, quit to the main menu and load
> the save from there.

## 4. #13539: `Events.ClearHexHighlights` on maps wider than 181 plots

### 4.1 What it fixes [I from the pull request's text; the EXE side is not decompiled here]

The pull request: `Events.ClearHexHighlights` clears width x width plots of the EXE's plot highlight grid, which holds
32,768 plots; with a width above 181 (182 x 182 = 33,124) it writes past the grid. On a 200 x 100 map it zeroed a
water-wave setting and the game then divided by it. The fix replaces each call with `Events.ClearHexHighlightStyle(...)`
for the styles that call site draws, which writes only to plots holding that style. Tested upstream on 208 x 104.

The 5.4.8 call sites that are live (not commented out) [C, 5.4.8 tarball]:

| File (5.4.8) | Line | When it runs |
|---|---|---|
| `(1) Community Patch/Core Files/Overrides/MiniMapPanel.lua` | 357 | a modular map overlay is chosen |
| `(2) Vox Populi/Core Files/New Lua/GlobalArchaeologistDigSites.lua` | 15 | dig-site highlights cleared |
| `(2) Vox Populi/Core Files/New UI/OverlayAntiquities.lua` | 116 | the antiquities overlay cleared |
| `(2) Vox Populi/Core Files/Overrides/WorldView.lua` | 95 | waypoint path drawn from a waypoint to the mouse |
| `(3a) VP - EUI Compatibility Files/LUA/CityView.lua` | 2750 | leaving the city screen (EUI) |
| `UI_bc1/CityView/CityView.lua` | 2499 | leaving the city screen (EUI itself; the `(3a)` copy replaces it when VP's EUI files are installed) |
| `(4a) Squads for VP/UI/Squads.lua` | 69, 448, 460, 469 | squad modes and highlights |

The base game's own calls (`UI/InGame/InGame.lua`, `UI/InGame/WorldView/WorldView.lua` and their Expansion copies) are
replaced by CP's and VP's overrides, in which every call is commented out [C, file read]. EUI's
`CityBannerManager.lua` has a local function of the same name, not the event.

### 4.2 On our product path [C for the split, I for the write]

The EXE keeps its Overlay system and the highlight handling in the product's configuration: under D33 the Overlay
system still runs and renders every highlight style (`../05-graphics-composition/overlay-system.md` section 1, P1-P2
[C + L]); the `SerialEventHexHighlight` handler and its per-plot table are the EXE's
(`../02-exe-internals/disable-systems-and-masks.md`, bug 0127 [C]). So `Events.ClearHexHighlights` does on the product
path what it does on the stock route [I], and the defect is the same on both.

### 4.3 Whether a player reaches it [C, L from 9073]

- No standard map size is wider than 128 plots; only a map script option or a custom map gets past 181.
- The stock EXE loads maps up to about 20,000 plots whatever their shape: 198 x 101 (19,998 plots) loads
  (`../02-exe-internals/allocations-and-memory-map.md` section 6.1 [L, 9073]). So a width of 182-198 is reachable
  without any EXE change when the height keeps the map under about 20,000 plots, and #13539's defect then fires at the
  first call above (for example, leaving the city screen with EUI) [I: 9073 only loaded the 198 x 101 save, it did not
  play it]. The pull request's own 200 x 100 case agrees.
- Whether the product's client loads the same maps is not measured (section 6.1 says so); the client is the same EXE.

### 4.4 Recommendation

Nothing for players: such maps are rare, the behaviour is VP 5.4.8's on both routes, and VP's next release carries the
fix. For the project: test and stress maps stay at most 181 plots wide while the pinned VP is 5.4.8. The acceptance
stress map of D157/D160 (180 x 112 Tectonic) is 180 wide, under the limit (180 x 180 = 32,400) [C arithmetic]; a
future larger stress map must not be wider than 181 or it measures this defect instead of memory.

## 5. #13467: database statements left open (already handled)

What it fixes and how it behaves on both routes is ticket 0184's finding: `proxy-design.md` section 23.7 [C, L] and
`upstream-drafts/cpk-query-step-to-end.md`. In short: CP's `CPK.Misc.CustomModOptionEnabled.lua` line 26
`return query(name)() ~= nil` (and `CPK.Util.Color.lua` line 134, both present in 5.4.8 [C]) leave one statement per
front-end context on its first row; a mod-change rebuild then cannot change the schema and the database is left half
built. On the product path the server fails and D125's popup and report appear (exit 3) [L, 0184]. The project's fork
branch `fix-cpk-query-step-to-end` holds upstream's commit for tests; the patch does not ship it. The beta notes' known
issue is `../06-tooling-and-workflows/installer-notes.md` section 8a. Nothing new is needed.

## 6. Not verified

- No game was run: neither crash was reproduced on either route for this ticket.
- The EXE-side mechanism of #13539 (grid size, the width x width loop) is the pull request's description, not our
  decompile.
- Whether the product's client loads maps of 182-198 plots wide (section 4.3).

## 7. Shipping the three fixes with the patch: the scope (ticket 0638 part 1, D225, 2026-11-21)

Reading and offline checks only; no game was run and nothing was built. Sources: the project's clone of upstream
(`Community-Patch-DLL`, remote `upstream` = LoneGazebo), read with `git show`, `git log`, `git merge-base` and
`git ls-remote` only; the 5.4.8 modpack's manifest `F:\c5-64bit-work\2600\modpack548\VP_MODPACK.manifest.tsv`
(installer-notes 14.1) and the mods548 instance's manifests `F:\c5-instances\c5h-d\{user,install}.manifest.tsv` (hash
lists, read only). The scratch scripts that made the tables are in `C:\c5-64bit-work\0638\` (not committed). Nothing of
Vox Populi's or the game's files is in this repository (D83).

### 7.1 The installed bytes are the tag's blob with CRLF line ends [C]

VP's `.gitattributes` at `Release-5.4.8` says `*.lua eol=crlf`. VP's installer packs a ModBuddy `Build\` folder made
from a checkout, so an installed `.lua` file is the git blob with every LF turned into CRLF (none of the 18 blobs below
holds a CR). Checked by SHA-256: every fixed file present in the 5.4.8 modpack (14 under `Mods\`, plus the two `UI\`
copies) and in the mods548 instance (the (3a) and (4a) files and `UI_bc1\CityView\CityView.lua`) has exactly the
CRLF form's hash. The `md5` that each `.modinfo` lists for these files is the CRLF form's MD5 too (22 of 22 entries
match). So **a hash table for the installer is computed on the CRLF form**, and the fixed files are placed in the CRLF
form as VP's next installer would place them. (Installer-notes 14.1 says the instance's files are "the tag's byte for
byte": true for `git archive`, which applies `.gitattributes`; not for `git show`'s raw blob.)

### 7.2 The files [C]

18 file changes in 17 files (`Squads.lua` has two fixes). Hashes are of the installed CRLF form, first 16 hex digits
(the part 2 generator writes the full values). "5.4.8 = parent": the file at the fix's parent commit is 5.4.8's, so the
file at the fix commit is 5.4.8 plus that fix and nothing else. "master = fixed": upstream `master` (c434d45da,
2026-11-20) still has the fixed bytes. Release-5.4.8 is still VP's latest release (`git ls-remote --tags upstream`,
2026-11-21 02:40 EST: no `Release-5.4.9`, `5.5*` or `6*`); none of the three commits is an ancestor of the tag.

| File (5.4.8 path) | 5.4.8 installed | Size B | Fix | Fixed | 5.4.8 = parent | master = fixed |
|---|---|---|---|---|---|---|
| `(1) Community Patch/LUA/DiploCorner.lua` | `6148a1a07594af37` | 24,547 | 475bd4997 | `3009940320c1d15f` | yes | no |
| `(3a) VP - EUI Compatibility Files/LUA/DiploCorner.lua` | `4ad27e0410188305` | 24,373 | 475bd4997 | `ba15367676782edd` | yes | no |
| `(3a) VP - EUI Compatibility Files/LUA/NotificationPanel.lua` | `45a075af1158862a` | 79,752 | 475bd4997 | `90e4a03742fdae1e` | yes | no |
| `(4a) Squads for VP/UI/SquadNames.lua` | `e912091955d364df` | 5,636 | 475bd4997 | `f306aa8d763d7aba` | yes | yes |
| `(4a) Squads for VP/UI/Squads.lua` | `8f7727e55b3da57e` | 35,927 | 475bd4997, then aa7a29b38 | `1e0c4fb80cecf64a` | yes (both in a row) | yes |
| `(1) Community Patch/Core Files/Overrides/Includes/InfoTooltipInclude.lua` | `f06b1d81e1f0bb5e` | 242,695 | 14d16fa3d | `042a5bec622926f1` | **no** | no |
| `(1) Community Patch/Core Files/VPUI/VPUI_core.lua` | `67c2d18fed645699` | 6,642 | 14d16fa3d | `5df82c2803e94b29` | yes | yes |
| `(1) Community Patch/Kit/Misc/CPK.Misc.CustomModOptionEnabled.lua` | `4b01919bdb3e5e88` | 1,046 | 14d16fa3d | `2fab11447eba3fd9` | yes | yes |
| `(1) Community Patch/Kit/Util/CPK.Util.Color.lua` | `609dc54ef199e04a` | 3,195 | 14d16fa3d | `9f1a91b3f2ad1305` | yes | yes |
| `(2) Vox Populi/Core Files/Overrides/AssignStartingPlots.lua` | `1dde66859d35a909` | 421,175 | 14d16fa3d | `97a1775668e704b0` | yes | yes |
| `(2) Vox Populi/Core Files/Overrides/PlotMouseoverInclude.lua` | `861d0e15dd3b8218` | 13,730 | 14d16fa3d | `8a9fb0e51f1c2f35` | yes | yes |
| `(3a) VP - EUI Compatibility Files/LUA/PopulateUniques.lua` | `ca51df45795786c3` | 9,253 | 14d16fa3d | `1f194603245c1e37` | yes | yes |
| `(1) Community Patch/Core Files/Overrides/MiniMapPanel.lua` | `09707d3cb34e041e` | 27,643 | aa7a29b38 | `b99c9084f8ba322e` | yes | yes |
| `(2) Vox Populi/Core Files/New Lua/GlobalArchaeologistDigSites.lua` | `e907ac0e47137cfe` | 2,457 | aa7a29b38 | `c5f47f7166540183` | yes | yes |
| `(2) Vox Populi/Core Files/New UI/OverlayAntiquities.lua` | `afc589cdf66075c8` | 4,842 | aa7a29b38 | `a2b1b84e02108eaa` | yes | yes |
| `(2) Vox Populi/Core Files/Overrides/WorldView.lua` | `50f79d0f54445499` | 49,675 | aa7a29b38 | `156324afb56e6580` | yes | no |
| `(3a) VP - EUI Compatibility Files/LUA/CityView.lua` | `29a8668f72619e07` | 116,395 | aa7a29b38 | `43631ae39b2fb48c` | yes | no |
| `UI_bc1/CityView/CityView.lua` (game folder `Assets\DLC\UI_bc1`) | `b53da4b9cce6a19c` | 108,330 | aa7a29b38 | `825ceebc5cd5f571` | yes | yes |

The one exception: `InfoTooltipInclude.lua` was changed between the tag and the fix by 87e3d4ab0 (Full Recon Rework,
one tooltip line reading `kUnitInfo.XPFromExploration` and `TXT_KEY_PRODUCTION_UNIT_XP_FROM_EXPLORATION`, which 5.4.8's
database does not have [I that it lacks them]). Upstream's file at 14d16fa3d therefore carries an unrelated gameplay
change. The fix's own hunk (lines 4-17, eight `GameInfo.Defines{...}()` reads turned into a loop) does not overlap that
line, so a backport would apply, but its bytes would exist in no upstream commit. Upstream's commit text says that
none of the include-time statements in this file was ever seen to block a rebuild: the fix there is a precaution.

Each fix uses only the EXE's UI API (`ContextPtr:SetUpdate`, `SetShutdown`, `LookUpControl`, `ClearCallback`,
`Events.ClearHexHighlightStyle`) and plain Lua; none needs a DLL change, so the fixed files work with VP 5.4.8's DLL
and with the patch's x64 DLL alike [C: the diffs read with `git show -U0`].

### 7.3 Which copy the game loads, by install form [C for what is on disk; I for the VFS order]

What VP 5.4.8's installer puts on disk (`VPSetupData.iss`, section 1 of installer-notes): (1) and (2) are installed
**without** their `LUA\` folders for the EUI components and with them for the no-EUI ones; (3a) and `UI_bc1` only for
EUI; (4a) Squads for every VP component (activated only if the player turns it on); Community Patch only = (1) with
`LUA\`, no (2)/(3a)/(4a). The 43-civ components install what their twins install plus (3b), which holds none of these
files. The modpack form (installer-notes 14.1) copies each activated mod to `<pack>\Mods\<mod>\` and, in addition, copies
`MiniMapPanel.lua` and `CityView.lua` of an activated mod into `<pack>\UI\` (byte copies: same hash, checked on the
5.4.8 modpack).

| File | Mods form, EUI | Mods form, no EUI | Modpack form | Notes |
|---|---|---|---|---|
| (1) `LUA/DiploCorner.lua` | not on disk | loaded | loaded if made from a no-EUI install | |
| (3a) `DiploCorner.lua`, `NotificationPanel.lua` | loaded | not on disk | loaded if EUI | (3a)'s mod files override `UI_bc1`'s and the base game's |
| (4a) `SquadNames.lua`, `Squads.lua` | loaded when (4a) is on | the same | only if (4a) was on when the pack was made | |
| (1) `InfoTooltipInclude`, `VPUI_core`, CPK `CustomModOptionEnabled`, CPK `Color` | loaded | loaded | loaded | the two CPK files are the ones that cause K6 (ticket 0184) |
| (2) `AssignStartingPlots`, `PlotMouseoverInclude`, `GlobalArchaeologistDigSites`, `OverlayAntiquities`, `WorldView` | loaded | loaded | loaded | (2)'s `AssignStartingPlots` and `WorldView` win over (1)'s same-named files, which the fixes do not change |
| (3a) `PopulateUniques.lua` | loaded (front end) | not on disk | loaded if EUI | |
| (1) `MiniMapPanel.lua` | loaded | loaded | `Mods\` copy and `UI\MiniMapPanel.lua` | both modpack copies have 5.4.8's hash |
| (3a) `CityView.lua` | loaded | not on disk | `Mods\` copy and `UI\CityView.lua` if EUI | a no-EUI pack's `UI\CityView.lua` is (1)'s, which no fix changes |
| `UI_bc1/CityView/CityView.lua` | **not loaded** while (3a) is on | not on disk | not loaded (the pack's `UI\CityView.lua` is (3a)'s) | EUI's own file; runs only with EUI present and (3a) off, a set-up the patch does not serve |

The installed hashes of the (1) `LUA\` copy were not seen on disk (no no-EUI instance exists here); they are the tag's
CRLF form [I, from 7.1].

### 7.4 Ways to deliver them

1. **Replace VP's own copies in place** (the recommendation). For each row of a per-release table: a file that holds
   exactly 5.4.8's installed hash is backed up by SHA-256 into the install record's backup folder and replaced by
   upstream's fixed file (temporary name, hash check, rename, as for the DLL); a file that already holds the fixed hash
   is left and logged ("already fixed"); a file with any other hash is left and named on the finished page and in the
   log ("changed by another mod or by hand: not replaced"); a missing file is skipped (component not installed). In the
   modpack form both copies of `MiniMapPanel.lua` and `CityView.lua` are handled. Removal restores each from its
   backup by hash. Cost: one table, a generator, a build step, one new step kind in `install.iss`, tests (part 2 of
   0638, 6-8 h as estimated). Payload: 16 files, about 0.6 MB (without `InfoTooltipInclude` and `UI_bc1`).
2. **Files of the patch's own that take precedence through the game's file system.** Not clean. In the mods form a
   mod's imported file beats a DLC package's, so only another mod could override VP's files; it would have to be
   activated by the player, its precedence over VP's own imports of the same name depends on the mod load order, which
   the patch does not control [I], and an activated mod is written into every save's mod list, so saves made with the
   patch would ask for that mod after the patch is removed and on other players' machines [I, from how Civ V records
   activated mods]. In the modpack form a DLC package with priority above the pack's 300 would work by priority, but
   the mods form has no such way. Two mechanisms for two forms, both untested, against one that the installer already
   does for the DLL.
3. **None** (the known-issue lines). D223 says a known fix goes in before players get a build, so this is the fallback
   only if part 2 fails.

### 7.5 When the player updates Vox Populi, or already has a newer one [C for VP's installer; I for the timing]

- **A newer VP already installed:** the patch's installer refuses it before anything is changed (the DLL's hash is not
  one of the pack's `x86_dlls`, installer-notes 4.3), so no Lua file is touched. A later patch release for a VP
  release that contains the fixes gets no rows: the generator writes a row only when the fix commit is **not** an
  ancestor of the release tag (`git merge-base --is-ancestor`), and the table is keyed by VP release.
- **VP updated over an installed patch:** VP's installer deletes the whole `(1)`, `(2)`, `(3a)`, `(4a)`, `(5)` folders,
  `UI_bc1` and `VPUI`, then copies its own (`[InstallDelete]`, section 1 of installer-notes). Every replaced file is
  then VP's new file and the proxy is gone.
- **The danger found here:** 12 of the 18 fixed files are byte-identical on upstream `master` (table above), so VP's
  next release will very likely place the very bytes the patch placed. Today's removal treats a record as live when any
  `replaced` file still holds what the install placed (`P64RecordLive`, `install.iss` line 933), and restores such a
  file from its backup. Recorded as ordinary `replaced` lines, the Lua files would make a record live after a VP
  update and the removal would **put 5.4.8's buggy files over the newer VP's**: a downgrade. So part 2 needs a step
  kind of its own (`vpfix`) that `P64RecordLive` ignores, and a `vpfix` line is restored only when the record is live
  by its own DLL lines (the proxy still in place over the recorded VP DLL) and the file holds exactly what was placed.
  A stale record restores no Lua file and only deletes its backups. Tests must include this case.
- **VP 5.4.8 reinstalled (repair) over the patch:** the files are 5.4.8's again, the record is stale by its DLL lines,
  nothing is restored; installing the patch again replaces them again.
- **A modpack made after the patch was installed** copies the fixed files (and, already today, the patch's files in
  `(1)`) from `MODS`; that is an existing property of the modpack maker, not new here.

### 7.6 Other mods and packs that replace the same files [I]

The in-place way changes only VP's own copies. A mod that overrides one of these files through the file system (its
own `DiploCorner.lua`, `CityView.lua`, `WorldView.lua`, `InfoTooltipInclude.lua`, `AssignStartingPlots.lua`, ...) wins
as it did before, with or without the patch: the player runs that mod's file, without the fix, as on VP 5.4.8 alone.
A mod or a hand edit that wrote over VP's file on disk leaves a hash that is not 5.4.8's, and the installer leaves it
and says so. A mod that `include`s one of these files gets the same functions (no fix removes or renames one). The
Steam Workshop and other mod sites were not surveyed.

### 7.7 Licence [C]

VP's files are GPL-3.0 (`License.rtf` at the tag: the Community Patch Project's EULA preamble, then GPL version 3), as
this repository (D22). The notice (`tools/installer/notices.py`) today says "no file of Vox Populi" is included; part
2 changes that sentence and adds a section naming each replaced file, its origin
(LoneGazebo/Community-Patch-DLL, the Community Patch Project), the commit and the upstream pull request. The files are
taken at build time from the clone (`git show <commit>:<path>`, CRLF, hash-checked against the table), like the
third-party texts, not committed.

### 7.8 Verdict: CLEAN, by way 1, with two files left out [C for the bytes; I for the VFS order and the md5 point]

- **Ship 16 files** (17 file changes): every one byte-identical to upstream's file at the named fix commit (`Squads.lua`
  at aa7a29b38, which holds both of its fixes), each of which is 5.4.8 plus that fix only.
- **Leave out `InfoTooltipInclude.lua`** (its upstream file carries the unrelated Recon Rework line; its part of #13467
  is a precaution by upstream's own account) and **`UI_bc1/CityView/CityView.lua`** (not loaded in any VP set-up that
  has (3a) on; it lives in the game folder in the mods form, where the installer writes nothing today). Both are
  choices for the MC; a hunk-only backport of `InfoTooltipInclude.lua` is possible but its bytes are in no upstream
  commit.
- **Must hold in part 2:** the `vpfix` step kind (7.5), replacement only on 5.4.8's exact hash, both modpack copies,
  the newer-VP downgrade case tested, the notice and `docs/INSTALL.md` changed.
- **Risk to settle live in part 2:** each `.modinfo` lists an MD5 of each file, which the replaced files no longer
  match. Hand-edited VP files are common and load [I]; whether the game checks these MD5s anywhere (the mods browser,
  multiplayer) is not verified. Part 2's live checks show whether the replaced files load. **Settled 2026-11-21 [L],
  section 8.4:** the game neither refuses nor warns; the mods are listed enabled and the game plays (single player;
  multiplayer not tried).

## 8. As built (ticket 0638 part 2, MC #9044, 2026-11-21)

MC rulings (#9044): `InfoTooltipInclude.lua` and `UI_bc1/CityView/CityView.lua` left out ("upstream's bytes or
nothing"); way 1 with a step kind of its own; conditions (a)-(j). Branch `task-0638-vp-lua-crash-fixes`.

### 8.1 The pieces [C]

| Piece | What it does |
|---|---|
| `tools/installer/vp_lua_fixes.py` | generator (read only on a Community-Patch-DLL clone): for one release tag, `dll` rows (the tag's 22- and 43-civ DLL SHA-256), `file` rows (mod folder, path, SHA-256 installed = CRLF form, SHA-256 fixed, size, commits, pull requests, modpack UI copy name) and `left` rows with the reason. A file is a `file` row only when the release's file is exactly the file at the first fix's parent and each later fix of it follows directly ("upstream's bytes or nothing"), else `left`; `UI_bc1` is in its `LEFT_OUT` list (MC ruling). `--check` compares with the committed table |
| `tools/installer/vp_lua_fixes.tsv` | the committed table for Release-5.4.8: 16 `file` rows, 2 `left` rows (InfoTooltipInclude by the rule, UI_bc1 by the list), DLLs `262aee88...` (22) and `01451bd7...` (43). Hashes only |
| `build_installer.py` | when the table has a release whose DLL a variant accepts: `payload/vpfix/` (shared by both variants; the variant folders and the D162 art-check are unchanged) with upstream's files taken from the clone (`--vp-clone`, default the project's clone) by `git show <last commit>:<path>`, LF to CRLF, each checked against the table (and the release's own file against its installed hash); `vpfix.tsv` for the installer; `payload.iss` gets `P64HasVpFix`, `P64VpFixCount`, `P64VpFixPRs` and the DLL list; `build-record.json` gets `vp_fixes`. `--vp-fix-table` for test builds only. A build for a VP release without rows is as before |
| `install.iss`, install | `P64PlaceVpFixes` (step 2b, right after the proxy's `replaced` line): nothing when `P64VpFixDir` is empty (the box off) or the VP DLL found is not one of `vpfix.tsv`'s `dll` rows (MC (d)); per row: absent = not installed; exactly VP's hash (in the modpack form also the pack's `UI\<name>` copy) = backed up by SHA-256 and replaced, journaled as `vpfix` (H1 VP's = the backup's name, H2 the fixed); exactly the fixed hash = left ("already fixed"); anything else, or the two modpack copies not both VP's = left and named (MC (b), (c)). The summary and every left file are appended as `#` lines to `vpproxy.ini`, which every error report copies whole (`proxy_rt.cpp` `in.copies`), so a report says which VP files the patch replaced (MC (b), D118) |
| `install.iss`, removal | `vpfix` lines are not counted by `P64RecordLive`; a `vpfix` file is put back only while `P64DllLive` (the proxy still holds the recorded place of VP's DLL) and it still holds exactly H2, else it is left and named; a stale record (VP reinstalled) restores no Lua file. The removal checks the backups it will need before anything changes |
| `patch64.iss` | the checks page shows, when the fixes apply, one box "Put in Vox Populi's own fixes for three crashes (recommended)", ticked, with a line saying what it replaces and that removal puts them back (MC (a), D27); `/VPFIXES=0` unticks it in a silent run; the ready page and the finished page say what was (or was not) done; the fixes' folder is extracted only when used and checked by SHA-256 |
| `notices.py` | with fixes: "no file of Vox Populi other than the fixed Lua files named below" and a section listing each file, its upstream commit and pull request, GPL-3.0 (MC (e)) |
| `docs/INSTALL.md` | section 2a (what, which files, both cases of the box); the two known-issue lines replaced by one line for players who untick the box |

### 8.2 Hermetic tests [L, 2026-11-21, through `quietlock.py run`]

- `tools/installer/tests/test_install_removal.py` 37/37 OK (behaviour without fixes unchanged).
- `tools/installer/tests/test_vpfix.py` 20/20 OK: install and removal by hash, foreign hash left and named, already
  fixed, unknown DLL touches nothing, box off, newer VP refused before anything, **VP updated over the patch then the
  removal does not downgrade**, a live record with a changed DLL does not either, a changed file left, damaged backup
  refuses the removal, update over an installed patch, update with the box off, a fault at every step (27 points, each
  rolled back), no-EUI with Squads, modpack both copies, a modpack copy that differs or is missing (neither replaced),
  a no-EUI pack.
- `tools/installer/tests/negative_control.py`: 12 of 12 mutations caught, 5 of them new (no `P64DllLive` check, `vpfix`
  counted as live, no original-hash check, not both copies, no DLL-table check). **Found:** the old mutation
  `no-backup-hash-check` had not run since ticket 0574 added a second line with its text (the script counts that as a
  miss); its text is now the replaced-file check's own line (8 spaces) and it is caught again.
- `tools/installer/tests/test_vpfix_build.py` 8/8 OK: the committed table's rows, the generator's rules on a synthetic
  upstream clone (a file with a later unrelated change becomes `left`, `--check`), the build's staging and its refusals
  (a clone that does not match the table, no clone, `--vp-fix-table` without `--test-build`), a build without matching
  rows unchanged, and the built installer's silent runs with the real `install.iss` (box on: replaced and put back by
  hash; `/VPFIXES=0`: nothing touched, nothing extracted).
- `tools/harness/tests/test_installer.py` 51/51 OK and `test_safety_static.py` 55 passed (the existing installer and
  harness suites with the changed `patch64.iss`, `install_stub.iss` and `build_installer.py`).

### 8.3 The TEST installer of the live checks [C]

Built from a copy of the 0626 rehearsal take's work folders (state.json, pack, proxy bin and build summaries copied to
`C:/c5-64bit-work/0638/take0626/`; nothing written into 0626's folder): layouts **`x64-1b87dd18-pa4c1c84`** (std) and
**`x64-1b87dd18-m43-pf3a6693`** (m43), configuration i3, no stage-5 code (no ServerCombat). `--test-build --release-name
0638-vpfix-on-0626`, the real table and the project's clone: `VP64-Patch-0638-vpfix-on-0626-for-VP5.4.8-TEST.exe`,
SHA-256 `e0fcaa9d34921fe8cbd8a27a526773b6c6a85eb18d179cfaa7f45a4e533e43b8`, 17,684,720 B; `payload/vpfix` 16 files plus
`vpfix.tsv`. ISCC only, no link.

### 8.4 Live checks on Goldberg instances [L, 2026-11-21]

| Form | Instance (EXE build), time | Result |
|---|---|---|
| mods, no EUI (VP 5.4.8's FullNoEUI component laid for the test: `(3a)` and `UI_bc1` moved aside, the `(1)`/`(2)` `LUA\` folders written from the tag in the installed form; all undone by hash) | c5h-b (4390913), 03:21-03:45 | box on: **12 of 16 replaced, 4 not installed** (the (3a) files); mods activated in the MODS menu (Squads ticked); game on the 64-bit path; the diplomacy list, the espionage overview (one of the contexts that request the diplo corner's refresh) and the city screen worked; **K5: two loads of a save from the in-game menu, no crash**; removal restored all 12 by hash; verify ok |
| modpack (EUI) | c5h-mpk (4390913, stamp 0x546CD0A8), 10:55-11:04 | box on: **13 of 16 replaced, 3 not installed** ((1) LUA DiploCorner, the (4a) files), **both UI copies replaced** with their Mods copies (15 `vpfix` steps); `server attached`, `kRt_Boot ok`; an autosave loaded from the main menu, a city founded, the EUI city screen opened and left (the fixed `CityView.lua` of the pack's UI copy); no Lua error from a replaced file; removal restored all 15 by hash; verify ok |
| mods, EUI | c5h-new-a (26577625), 03:46-03:52 | box off: "not put in: the box was unticked" in the log and in `vpproxy.ini`, no VP file changed; game in play, exit to the main menu; cut by account B's weekly limit before the mod change; restored by hash at 10:51 |
| mods, EUI, **K6** | c5h-new-b (26577625), 11:36-11:56 | the same flow in both arms (0184's: the Tiny save `C5H-0167-TINY_0008` (a copy) loaded with CP+VP+EUI, Exit to Main Menu, MODS, untick EUI, NEXT, Back; the harness's front end then activates CP+VP+EUI again). **Box off** (VP 5.4.8's Lua): `Database.log` 68 `is locked` / `already in use` lines and `no such column: PrimaryVictoryPursuit`, the server's DLL stopped on a failed check in `CacheGameDatabaseData`, the proxy `FATAL ... exit code 3` (D125, no play), report `VP64\reports\20261121-114413` (copy: `C:/c5-64bit-work/0638/evidence/`); the popup closed on MC-B's word (11:45). **Box on** (15 of 16 replaced: Squads present, (1) `LUA\DiploCorner.lua` not installed): **0 lock lines, no missing column, no FATAL**; after the rebuilds the game was back at the main menu with the mods active (a new proxy run started for the reactivation) and stopped normally; removal restored all 15 by hash; verify ok |

**K6 is fixed by the shipped files on the 64-bit path** [L]: the same flow ends in NO PLAY without them and goes
through with them, on the same instance and build. **K5**: two loads from the in-game menu with the fixes (c5h-b), no
crash; no try without them, so the crash itself was **not reproduced on demand** (a use after free, 3.1).

Changed modinfo MD5s: no refusal and no warning in either form; the mods are listed enabled and the game plays with the
replaced files [L]. `Error loading VPUI_loader.lua` in `Lua.log` (three times per start) is in the logs of instances
without the patch's files too (c5h-a, c5h-c, c5h-new-a) [L]: not related. In the modpack run one `Runtime Error ...
(3a) VP - EUI Compatibility Files\LUA\UnitFlagManager.lua:531: attempt to compare number with nil` followed the
founding of a city: a file the patch does not touch [C]; not followed up here.

K7: no save wider than 181 plots exists on these EXEs: **not reproduced on demand** (MC-B 03:16 accepted that).

### 8.5 For a release take

A release take that builds the installer needs the project's Community-Patch-DLL clone with the three fix commits
(`--vp-clone`, default `~/Documents/GitHub/Community-Patch-DLL`); `build-record.json` names the files and commits under
`vp_fixes`. The installed-layout journeys (D162, `tools/release/stages/hi_journey.py`) then run with the fixes placed
(the box is ticked by default); their layout check reads the proxy, the x64 files, the fallback DLL and `vpproxy.ini`
by hash before and after the run, all unchanged by the fixes during a run [C: `installed_layout`], and the
installer's removal puts VP's files back. `provision.py verify` of an instance with the patch installed would list
the replaced VP files as changed, as it would the patch's own files; the journeys verify only after the removal.
Known-crash rows K5-K7 (`../06-tooling-and-workflows/known-crash-causes.md`) change from "NOT in" to "in, by the
installer, box ticked by default; with the box unticked as before" once this is merged (that file is not 0638's).
