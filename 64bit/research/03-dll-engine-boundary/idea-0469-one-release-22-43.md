# Idea 0469, stage 1: one release for the 22- and 43-civilisation variants

Status: stage-1 study of ticket 0469 (D275), written 2026-11-26 21:17-21:33 EST by the 0469 implementer (account A),
branch `task-0469-one-release-22-43`. Reading only: the knowledge base, this repository at main 2c2b69fb5 and the DLL
source at upstream `Release-5.5` b0bce8c2f (the base of the x64 pin a2627c8ac; the pin commit itself is not in the local
fork clones). Nothing was built or run, and no product code was changed. Marks: `[C]` a source or document read by this
study (the line names it), `[L]` a live result taken over from the file named (its own mark), `[I]` inferred.

The question (the owner to the Architect, 2026-11-17, the ticket's scope): can one release serve the 22- and the
43-civilisation variants, with the menus, the DLL and the saves working for both, without changing an EXE instruction
(D228)?

## 0. Summary

1. **No EXE change is needed for any route.** The split is one DLL define inside the EXE's fixed 63 civ slots
   (`player-limits.md` 0 items 1-2) [C there]. The EXE's own 22-based places (network seating in slots 0-21, 3D leader
   scenes, one summary record) behave the same with either DLL; the leader scenes already have a run-time option,
   `[Proxy] LeaderScenes`, that does nothing with the 22 DLL (`docs/OPTIONS.md`) [C].
2. **Most of it is shared already.** One installer `.exe` carries both packs and installs the one whose `x86_dlls` holds
   the player's VP DLL hash (`installer-notes.md` 3 item 4, 4.3; built `std+m43`, 14.7 [L there]). One proxy and one
   `server.exe` serve both DLLs: the server reads the major count from `Defines` at run time and the split's names are
   poisoned in the proxy and server sources (`player-limits.md` 8.2 [L there]). The EXE's Lua `GameDefines` follows the
   loaded DLL on the product path (`player-limits.md` 8.4: `GameDefines` 43 / 20 / 63 with the 43 DLL [L there]). A save
   of the other variant is refused before the DLL reads it, with the D118 report and popup (`proxy-design.md` 30 [L
   there]).
3. **What still ties a player to one variant** is only the install: the pack on disk is the installed VP's variant, the
   pack check binds it to that one 32-bit DLL hash, and switching means reinstalling VP with the other component and the
   patch again (section 1).
4. **Recommended route: option (1), both x64 DLLs installed and chosen by one option at the proxy's boot** (section 2).
   It changes data and Python and a few lines of the proxy, no DLL and no EXE byte. Cost about 14-24 agent-hours over
   three step tickets (section 4). The DLL is chosen per EXE process (one proxy boot per mod activation), so a switch
   takes a restart of the game; a save of the other variant keeps being refused, and the popup names the option.
5. **Two limits stay with option (1)** [C reads; I effects]: the off switch and the patch-off path run the installed
   VP's 32-bit DLL only, so a game of the other variant cannot be played or loaded with the patch off; and on a 22
   install the city-state slider of Advanced Setup offers up to 41 city states with the 43 DLL, of which the DLL opens 20
   (section 3.2).
6. **A switch without a restart** (choose the DLL from the save at its load) is possible in principle (the server is a
   separate process), but every route found swaps the server under a running EXE session; research first, not
   recommended now (section 5, step S4).
7. **Option (3), a single DLL with the split as a run-time value stored in the save**, is an upstream DLL change of
   about 60-120 agent-hours plus parity for both values [I]. It is the only route that also serves the 32-bit stock
   game and the patch-off path. It waits for upstream's merge of the 64-bit work (D168, D280; step S5). Option (2), ship
   only 43, stays not recommended (20 city states at most; every 22 save refused).

## 1. What is per variant today

| Layer | Shared by both variants | Per variant | Source |
|---|---|---|---|
| EXE | everything (64 slots; 22-based places behave the same) | nothing | `player-limits.md` 3.3 [C there] |
| Proxy (`CvGameCore_Expansion2.dll`, 32-bit) | the program | `vpproxy.ini`'s `x64_dir` points at one pack | `proxy/src/proxy_rt_logic.h` `Settings::x64Dir` [C] |
| `server.exe` and the three x64 libraries | the program (run-time count, `SrvMajorCivs()`) | a copy in each pack (each take builds its own, same source) | `player-limits.md` 8.2; `installer-notes.md` 2 [C] |
| x64 DLL | - | `CvGameCore_Expansion2_x64.dll`, built with or without `/DMOD_GLOBAL_MAX_MAJOR_CIVS=43` | `player-limits.md` 8.1; `vp55-patch-side.md` 8.5 [L there] |
| Purity list | - | `purity.tsv`, bound to the x64 DLL's SHA-256 | `player-limits.md` 4.3 [C there] |
| Pack manifest | format 1 | `x86_dlls` = the variant's VP DLLs only; `"major_civs": 43` in the 43 pack | `proxy/src/proxy_pack.h` 1-18; `tools/release/pack.py` 345 [C] |
| Pack check at boot (`pack=1`) | - | `fallback_dll`'s SHA-256 must be one of the pack's `x86_dlls`, else `kOtherDll` and the 32-bit DLL boots | `proxy/src/proxy_pack.h` 12-15, 46 [C] |
| Fallback 32-bit DLL (`CvGameCore_Expansion2_x86.dll`) | - | the player's own VP DLL, of the variant VP's installer laid | `installer-notes.md` 3 item 2 [C] |
| Front end | VP's and EUI's screens read `GameDefines` | the (3b) mod's `AdvancedSetup.lua` (slider `MAX_CIV_PLAYERS - MAX_MAJOR_CIVS`) exists only in a 43 install; stock and 22 installs have the literal 41 | `player-limits.md` 1.2, 3.2 [C there] |
| Saves | header, GUID, version `"1.0.0"` | the body's layout (191 arrays without a count); our 43 builds add `+m43` to the version string | `player-limits.md` 5.1; `proxy-design.md` 30.1 [C there] |
| Release take | one tag | two takes (`take_release.py <tag>` and `--major-civs 43`); the installer is built from both work folders | `installer-notes.md` 2, 4.7 [C] |

So the release is already one file; what is not one is the installed game. A VP player who wants both variants today
reinstalls VP with the other component (VP's installer, `installer-notes.md` 1), then the patch (its installer picks the
other pack by the new hash).

## 2. Option (1): both x64 DLLs installed, chosen by one option

### 2.1 How it would work

- **Install both packs.** The installer lays `VP64\` (the installed VP's variant, as today) and the other variant's pack
  beside it, for example `VP64-m43\` or `VP64-m22\`. On disk that is one more x64 DLL (23.6 MB at 5.4.8 for the 43 build,
  `player-limits.md` 8.1), its purity file, and a second copy of `server.exe` and the libraries unless the packs are
  merged into one folder (manifest format 2: one `variants` list of `{major_civs, x64_dll, purity, x86_dlls}`). The
  installer file itself does not grow: both packs are already in it [C: `installer-notes.md` 3 item 4].
- **Bind each pack to both VP DLLs of the release.** `x86_dlls` of each pack lists the release's 22 and 43 VP DLLs
  (5.4.8: `262aee88...` and `01451bd7...`, `installer-notes.md` 1), so the pack check passes whichever variant VP's
  installer laid. This is a `tools/release/pack.py` and `tools/installer/build_installer.py` change; the proxy's check is
  unchanged [C: `proxy_pack.h` compares the fallback's hash with the list].
- **One option chooses.** A key in `VP64Options.ini` (D89), proposed `[Game] MajorCivs` with `0` = the installed VP's
  variant (the default), `22`, `43`. The installer's options page lists it like every other key (it is generated from
  the options table, `docs/OPTIONS.md` "The installer's options page"). At its boot the proxy resolves the key to the
  pack folder before the pack check and the server start; the server needs nothing new (`SrvMajorCivs()` reads the
  DLL's own `Defines`, `player-limits.md` 8.2). One `vpproxy.log` line names the variant and why.
- **When the choice takes effect.** The EXE loads the game DLL once per mod activation and unloads it at a mod-set
  change; every proxy load is a new boot and a new `server.exe` (`load-sequence.md` 6 [L there]; `farm-builder-guard.md`
  12.2; `proxy-design.md` 16.1, `GetScriptSystem` row). So the option is read at the boot that follows the activation:
  in practice, a change takes a restart of the game, as every other option does (`docs/OPTIONS.md`: "takes effect at
  the next start").

### 2.2 Menus

- **Everything that reads `GameDefines` follows the chosen DLL** with no file change: the DLL writes the `MAX_*` rows into
  `Defines` at post-processing and the server's writes are replayed into the EXE's database (`player-limits.md` 4.1, 4.3;
  live with the 43 DLL, 8.4) [C, L there]. In the VP 5.4.7 modpack 49 of 242 Lua files use the player constants (104
  uses of `MAX_MAJOR_CIVS`), with no literal loop bound of 21-64 except the cases in `player-limits.md` 3.2 [C there].
- **The city-state slider** (stock and VP `AdvancedSetup.lua` lines 1089 and 1097: `math.min(41, #MinorCivilizations)`)
  offers up to 41 city states with the 43 DLL on a 22 install. The DLL itself opens only the slots it has: at game start
  `CvGame::InitPlayers` makes slot `i` of the city-state range a computer player only while `i < MAX_MAJOR_CIVS +
  numMinorCivs`, over `MAX_MAJOR_CIVS..MAX_CIV_PLAYERS-1` (`CvGame.cpp:640, :849` at `Release-5.5` b0bce8c2f) [C], so
  41 asked gives the 20 there are. What the EXE's random minor-civ fill (0x50B090, minors from slot 22 on,
  `player-limits.md` 3.1) does to major slots 22-42 when 41 are asked is not traced [I]; VP's `onGameStarted` override
  and `InitPlayers`'s closed-slot handling (`CvGame.cpp` 660-700 at b0bce8c2f: a closed major slot becomes an observer)
  suggest it is harmless. Step S2 checks it live.
- **The 43 file works for both.** The (3b) mod's `AdvancedSetup.lua` computes the slider from `GameDefines` and gives 41
  with the 22 DLL, the stock number (`player-limits.md` 1.2) [C there]. So laying VP's own (3b) folder (its modinfo and
  that file, VP's bytes of the release tag) on a 22 install would make the slider right in both variants. It is a VP file
  derived from the game's front end: shipping it is the same kind of question as D225's VP Lua fixes, and needs the MC's
  (or the owner's) ruling under D83/D225 before it is built. Without it the mismatch is cosmetic (section 3.2).
- **Leader scenes** for majors 22-42: unchanged, the option `[Proxy] LeaderScenes` already works at run time and does
  nothing with the 22 DLL [C: `docs/OPTIONS.md`].

### 2.3 Saves

- **A save of the other variant is refused today, cleanly** (marked `+m43` saves always; unmarked saves by the layout of
  `CvGameTrade`'s two N x N arrays; live arms on c5h-c) [L: `proxy-design.md` 30.5]. Nothing changes for the safety of a
  load under option (1).
- **What changes is the message.** The refusal's popup says the save was made with the other variant
  (`proxy-design.md` 30.5, the helper's text). Under option (1) it names the cure instead: set `MajorCivs` to N and start
  the game again. Optionally the patch writes the switch itself (a one-shot "next start" line beside the options, never
  in the save) so the player only restarts and loads again. Both are step S3.
- **The save header cannot choose the DLL before the load**: the EXE writes the same DLL GUID and version `"1.0.0"` for
  both builds, and the header has no per-slot list (`player-limits.md` 3.1, 5.1) [C there]. The first point where the
  variant is known is the body's first read, after the DLL is loaded (`proxy-design.md` 30.1).

### 2.4 The off switch, the fallback and the modpack form

- With the patch off (`mode=off`, `vp64helper off`, D33 sessions) the proxy boots `CvGameCore_Expansion2_x86.dll`, the
  installed VP's own DLL. That DLL has one variant. A game of the other variant cannot be started or loaded with the
  patch off, as it cannot on that VP install today [C: `installer-notes.md` 9; `proxy_rt_logic.h` `fallbackDll`].
  Shipping VP's other 32-bit DLL would close the gap, but the installer ships no VP DLL (D28, D171 list item 2): not
  proposed. The beta notes say it in one line (step S1).
- After a failure the patch stays on and reports (D118, D240); nothing here changes that.
- **The modpack form** (VP inside `Assets\DLC\<name>`) is identified by the same DLL hash (`installer-notes.md` 4.3).
  Whether a modpack made from a 43 install carries the (3b) files is not known [I]; S1 treats the modpack form exactly
  as the mods form and S2 notes what the 43 slider does there.

### 2.5 Cost of option (1)

| Part | Where | Size [I] |
|---|---|---|
| Manifest: each pack's `x86_dlls` holds both VP DLLs of the release (or format 2 with `variants`) | `tools/release/pack.py`, its tests | 2-3 h |
| Installer: lay both packs, record both, remove both; the variant page line becomes "both variants, default <installed>"; write the option's default | `tools/installer/`, its hermetic tests | 4-6 h |
| Proxy: resolve `[Game] MajorCivs` to the pack folder before the pack check; one log line; options table row, `docs/OPTIONS.md` | `proxy/src/proxy_rt*.cpp`, options table, `test_proxy_rt.py` | 3-5 h |
| Live arms: a 22 install with 43 chosen (new game, AI turns, a 43 save loads, a 22 save refused with the new text), and the reverse; the D162 journeys of the release stay as they are | test instances (Goldberg) | 3-5 h |
| Refusal text naming the option (S3) | `vp64helper` popup text, report | 2-4 h |
| Slider check and, on a ruling, the (3b) files (S2) | live check; installer step | 2-4 h |

In all about 14-24 agent-hours, no DLL change, no EXE byte (D228), no new release take: the two takes exist already
(D166, D171). Disk on the player's machine grows by about 25-30 MB [I: one x64 DLL, its purity file, a server copy].

## 3. Options (2) and (3)

### 3.1 Option (2): ship only the 43 build

Not recommended, as the Architect said: 20 city states at most (`MAX_MINOR_CIVS` = 63 - 43), and every save of every
22 build (all of upstream's default installs) refused (`proxy-design.md` 30) [C there]. Nothing found in this study
changes that.

### 3.2 Option (3): one DLL, the split a run-time value stored in the save

- **Size of the change in the DLL** [C: `player-limits.md` 2.1 at 04b1211b8, counts good to a few percent]: 1,679 uses of
  `MAX_MAJOR_CIVS` (633 loop bounds, 543 range checks, 151 comparisons, 104 minor-loop starts and 10 index offsets, 198
  fixed arrays and 13 typedefs, 7 sized containers), 191 serialised arrays written without a count
  (`player-limits.md` 5.1), the `KnownPlayersBitArray` type that is `uint32` at 22 and `uint64` at 43 (it changes the
  pregame layout, `CvPreGame.h:297-304` at 04b1211b8), `OBSERVER_TEAM` = M - 1, and the three `#if
  defined(MOD_GLOBAL_MAX_MAJOR_CIVS)` blocks (start regions, pantheon reuse, a no-op).
- **Shape.** Arrays sized at the largest supported count (43, or 62), the loops and offsets on a run-time `M` chosen at
  game set-up and written into the save under a new save version; each serialised array writes `M` elements so a 22
  game's save keeps today's 22 layout and old saves of both variants still load (the reader takes M from the save, or
  from 0354's layout check for an old save). The `Defines` rows would have to be rewritten when M changes inside one
  DLL load (they are written once at post-processing today, `CvDllDatabaseUtility.cpp:267-274` at 04b1211b8), and the
  front end needs a control that sets M before the setup screen reads `GameDefines`: a VP Lua change too.
- **Cost** [I]: 60-120 agent-hours for the DLL and the Lua, plus x64 = x86 parity and save tests for both values, plus
  memory and time on the 32-bit stock game (arrays at the 43 size in every game: about 1 MB more save body before
  compression and 2-3 MB more memory, `player-limits.md` 2.2, 5.1). The Architect's earlier guess was 40-100 h; this
  study's read of the save and pregame side puts it higher.
- **Why it is the end state anyway:** it is the only route that also gives the 32-bit stock game and the patch-off path
  both variants, and it removes the second take, the second purity list and the second parity baseline. It is an
  upstream change (D104: 32-bit use unharmed), so it belongs after upstream has taken the 64-bit work (D168, D174, D280).

## 4. Recommendation

Option (1) now, in three small steps (S1-S3), when a worker place is free (priority 1, D275); option (3) proposed to
upstream after the 64-bit merge (S5); the no-restart switch (S4) only if the owner asks for it after S3. No route needs
an EXE instruction change, the Steam check is untouched (D65), and nothing from the EXE or the game's files is shipped
(D83), with the one open ruling on VP's (3b) front-end file (2.2).

## 5. Step tickets, drafted for the MC (D274: each ready or blocked; ids from the MC, rule 26)

| Step | Title | State | Blocker | Touches (proposed) | Size [I] | Acceptance |
|---|---|---|---|---|---|---|
| S1 | One install, both variants: both packs laid by the installer, each bound to both VP DLLs of the release, `[Game] MajorCivs` (0 = installed variant, 22, 43) chosen at the proxy's boot | ready | - | `tools/release/pack.py` and tests; `tools/installer/` and tests; `proxy/src/proxy_rt.cpp`, `proxy_rt_logic.*`, the options table, `docs/OPTIONS.md`; `research/06-tooling-and-workflows/installer-notes.md`; this file | 10-16 h | hermetic: pack and installer tests (both variants laid, recorded, removed by hash; a pack whose `x86_dlls` lacks the installed DLL still refused), proxy unit tests (each key value, a missing pack folder fails loudly to the D118 path); live on a Goldberg instance with a 22 install: `MajorCivs = 43` boots `server.exe` with the 43 DLL (`player limits: MAX_MAJOR_CIVS 43` in server.log), a 30-major new game plays 5 AI turns; the same instance with `MajorCivs = 0` plays the 22 game; the reverse on a 43 install; beta-notes line on the patch-off limit (2.4) |
| S2 | The city-state slider with the 43 DLL on a 22 install: what the EXE's fill and the DLL do with more than 20 city states asked; and, on the MC's ruling under D83/D225, VP's own (3b) `AdvancedSetup.lua` laid by the installer | blocked | S1 merged (needs the pack layout of S1; the D83/D225 ruling is asked in S2's first post) | `tools/installer/` (only on a yes); this file | 2-4 h | live: Advanced Setup on a 22 install with the 43 DLL, 41 asked: the city states alive, slots 22-42 read back (Lua summary), no assert; with the (3b) file (if ruled in): the slider stops at 20 with 43 and at 41 with 22; diagnostic pictures in the feed (D112) |
| S3 | The other variant's save: the refusal names the cure (set `MajorCivs` to N and restart), optionally a one-shot switch for the next start | blocked | S1 merged | `server/src/remote/srv_remote_savevariant.cpp` (the report's detail), `vp64helper` popup text, `proxy-design.md` 30 | 2-4 h | hermetic: helper text for both directions with the option named; hutest `variant-*` as in 0354 with a control on main; live: a 43 save on a 22-default install is refused with the new text, after the switch and a restart it loads and plays one AI turn |
| S4 | Research only: choosing the DLL from the save without a restart (a server swap at `ICvGame1::Read`, or the EXE's own DLL reload at a mod activation): what the EXE holds of the old server (canonical handles, the `Defines` replay, the generation counter of `proxy-design.md` 16.1), and a go or no-go | blocked | S3 merged and the owner asking for a switch without a restart | `research/03-dll-engine-boundary/` (a section of this file) | 4-8 h | a written go/no-go with the hazards marked [C]/[I]; no product code |
| S5 | Upstream proposal: one DLL with the major count chosen at set-up and stored in the save (option (3)); draft only, D95 | blocked | upstream's merge of the 64-bit work (the export PR of D168/D174/D280) | `research/03-dll-engine-boundary/upstream-drafts/` | 4-6 h for the draft; 60-120 h if upstream wants it built | a scrubbed issue text for upstream with the counts of 3.2, nothing sent without the owner's word |

## 6. Not verified

- The proxy's order at boot (options read before the pack check and the server start) is read from the code's
  description, not traced line by line; S1 confirms it [I].
- Nothing here was run: the cost figures are estimates [I]; the live facts are those of the files cited, at their dates
  and builds (5.4.8 pins for `player-limits.md` 8, the VP 5.5 pins for `vp55-patch-side.md` 8.5).
- The EXE fill of minor-civ types into major slots 22-42 when more than 20 city states are asked (2.2) [I].
- Whether a VP modpack made from a 43 install carries the (3b) files (2.4) [I].
