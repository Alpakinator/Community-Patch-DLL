# A9: the AI's next desired wonder can be a wonder that is already built (ticket 4805, 2026-11-17)

Review item A9 (`research/08-performance/external-port-speedups-review.md`, row A9 and F10; D181: a core defect fix in
the fork on both pins, with the upstream report part of the fix). Pins: x64 `4f4daac70` (x64-batch3), i0-ship
`39f17f754` (i0-ship-batch3). The files read below are byte-identical at both pins (`git diff --stat 39f17f754
4f4daac70` over them is empty) [C, 2026-11-17].

**Result.** The sanity check does **not** reject built wonders [C], so the skip is not exact: it is a behaviour change,
a defect fix. On the pin, built wonders are 31 of 33 choices in M 150-160, 21 of 22 in H 300-305, 4 of 41 in S 0-50 [L];
over the D24 Standard game 0-200, 436 of 548; with the fix none. Fork `fix-a9-x64` caf579d9f and `fix-a9-i0ship` 6b8379894 (one commit each, same text); the fixed
x64 and x86 DLLs play the same game (D24 save test PASS); the fix changes every D24 game, so a pin batch carrying it
replaces the D24 references. Upstream draft for the owner: `upstream-drafts/a9-wonder-specialization-built.md` (nothing sent).

## 1. Reading [C, source at both pins]

**Where the choice is made.** `CvCitySpecializationAI::DoTurn` (`CvCitySpecializationAI.cpp` 303-371) calls
`CvWonderProductionAI::ChooseWonder(m_iNextWonderWeight)` only while `m_bChooseNewWonder` is set, and clears it after
the call. The flag is set by `SetSpecializationsDirty` (374-412) for three events: `WONDER_BUILT_BY_RIVAL`,
`CITY_FOUNDED`, `WONDER_BUILT_BY_US`. There is no other caller of `ChooseWonder`. The result is kept in
`m_eNextWonderDesired`, the total weight in `m_iNextWonderWeight` (both saved with the player).

**What `ChooseWonder` keeps** (`CvWonderProductionAI.cpp` 136-199): for every city of the player and every building of
the game, a building is a candidate when the city's best-for-wonder flag for its class is set
(`CvCity::IsBestForWonder`), its flavour weight re-weighted by turns is above 0, and
`CvBuildingProductionAI::CheckBuildingBuildSanity(eBuilding, iWeight, plotStats, allExistingBuildings, false, false,
true)` returns a positive value. There is no `canConstruct` test and no test whether the wonder still exists to be
built. The pick is `ChooseAbovePercentThreshold` over the sorted list, seeded by the player id and its wonder count.

**Where the flag comes from.** `CvPlayer::SetBestWonderCities` (`CvPlayer.cpp` 48066-48183, called every turn for
majors from `CvPlayer::doTurn` at 10211) scores only buildings that some city `canConstruct`, and for each scored class
sets the best city's flag to true. A class that no city can construct any more (the wonder was completed) is never
scored, so no line touches its flags: **a city's best-for-wonder flag stays set after the wonder is completed**, by
anyone. (The function's "reset the flag everywhere else" loop clears `eClass`, the class of the outer building loop,
not `it->second`; another oddity, not changed here.)

**The sanity check does not reject built wonders.** `CheckBuildingBuildSanity` (`CvBuildingProductionAI.cpp` 154-1399)
returns early only for: no city / no building / value < 1 (`SR_IMPOSSIBLE`), the city about to fall, a small non-capital
city and any wonder class, religion wonders without a religion (prophet, missionary, reformation buildings), not the
best wonder city (`bNoBestWonderCityCheck` false), resource use in a puppet / resistance / automated city, aluminium
for the spaceship, extra luxuries without luxuries, siege without defence value, plot theft from majors / friends /
embassies, maintenance that would bankrupt, and a final bonus below 1 (`SR_UNKNOWN`). None of them reads whether the
building class is maxed out for the game, the team or the player, or whether the city or player already has it
(`allExistingBuildings` is only used for yield values). **So the sanity check does not make the skip exact; the
review's question is answered no, and the item needs a behaviour change (a fix).**

**What follows when a built wonder is chosen** (consumers of `m_eNextWonderDesired`):
- No city can start it: production lists only offer buildings a city `canConstruct`. The `+min(1500, pop*100)` bonus
  that `CheckBuildingBuildSanity` gives the next desired wonder (255-266) never reaches a buildable wonder.
- `CvCityAI::AI_chooseProduction` (`CvCityAI.cpp` 108-128): when the desired wonder is a world wonder whose class has
  been created, it calls `SetSpecializationsDirty(WONDER_BUILT_BY_RIVAL)`, so the next `DoTurn` chooses again, from the
  same flags. National wonders built by us take no such path.
- The wonder specialization weight: `WeightSpecializations` (619) uses `m_iNextWonderWeight`, the total weight of the
  candidate list, built wonders included.
- `FindBestWonderCity` and `GetWonderBuildCity` (`CvCitySpecializationAI.cpp` 417-440, 967-985) and the great engineer's
  directive (`CvPlayerAI::GetDirectiveEngineer` 1655-1700; `CvHomelandAI` 4663-4790) look for a city producing the
  desired wonder, which none does.

## 2. Counts from headless runs [L, 2026-11-17 19:20-19:36 EST, workstation, headless]

**The counting build.** Local scratch branch `a9-count-scratch` 8d19a9438 of the fork clone (not pushed; the patch is
`perf/evidence/0504/4805/a9-count-instrument.patch`): the pin `4f4daac70` plus read-only lines, written only when
`VP_A9_COUNT_LOG` names a file, at `ChooseWonder` (`pick`: the pick, whether it is built, the list size and how many of
the candidates and how much of their weight are built wonders), `CvCitySpecializationAI::DoTurn` (`stale`: one line per
player-turn whose desired wonder is built), `AI_chooseProduction` (`repick`: the built-by-rival reset) and the great
engineer's wonder directive (`engineer`). `VP_A9_FIX=1` applies the fix's test at run time. "Built" is the fix's test:
the class maxed out for the game, the team or the player. x64 release, DLL `efdbf7e3...` (23,671,808 B).

**Runs.** `perf/evidence/0504/4805/a9runs.py` (headless `server/run_server.py`, pin layout `x64-4f4daac7-pf8e9f42/bin`
with only the DLL replaced, AI logging off, D49), the windows of ticket 4801: M = 0438's `TS0438-M-T150` (Standard
Communitu_79a) 150-160, H = `TS0438-H-T300` (Huge Continental Drift) 300-305, both with 0438's statistics-on database,
S = the D24 Standard fixed-seed game 0-50. Arms: **pin** (`2d737bd5...`), **count** (the counting DLL, fix off),
**countfix** (the counting DLL, `VP_A9_FIX=1`), **a9** (the fix branch's DLL `384a50e5...`), **f0508** (bug 0508's fix).
Work folder `C:/c5-64bit-work/4805/runs/` (log `a9runs.log`, summary `a9summary.json`, by `a9summary.py`).

**The instrument changes nothing:** pin = count on every window (M 31,126 rows, H 111,122 rows: IDENTICAL), and
countfix = a9 (M 31,141, H 111,099: IDENTICAL), so the counts below are the counts of the pin's and the fix's games.

| Window | Arm | Picks | of them built | Candidates, of them built | Weight built | Stale player-turns | Repicks |
|---|---|---|---|---|---|---|---|
| M 150-160 | count (pin) | 33 | **31** | 1,048, 944 (90.1 %) | 89.0 % | 65 | 68 |
| M 150-160 | countfix (fix) | 12 | **0** | 41, 0 | 0 % | 23 | 9 |
| H 300-305 | count (pin) | 22 | **21** | 1,129, 1,098 (97.3 %) | 99.9 % | 49 | 220 |
| H 300-305 | countfix (fix) | 9 (4 none) | **0** | 24, 0 | 0 % | 26 | 1 |
| S 0-50 | count (pin) | 41 (4 none) | **4** | 90, 17 (18.9 %) | 21.7 % | 7 | 5 |
| S 0-50 | countfix (fix) | 40 (4 none) | **0** | 70, 0 | 0 % | 2 | 3 |
| T 0-200 | count (pin) | 548 (4 none) | **436** | 9,977, 8,399 (84.2 %) | 85.6 % | 821 | 945 |
| T 0-200 | countfix (fix) | 263 (30 none) | **0** | 870, 0 | 0 % | 138 | 125 |

T is the S game played on to turn 200 (the D24 standard length; a second run 20:19-20:31 EST, work folder
`C:/c5-64bit-work/4805/runs-T/`; pin = count IDENTICAL, 351,496 rows). Its built picks by 25-turn block: turns 25-49
4 of 41, 50-74 76 of 85, 75-99 99 of 112, 100-124 85 of 102, 125-149 46 of 60, 150-174 43 of 58, 175-199 83 of 90:
**from turn 50 on, 74-92 % of the choices in each block are built wonders**; the most chosen built ones are Stonehenge 92, Statue of
Zeus 60, Temple of Artemis 56.

Built picks on the pin: M Great Wall 16, Stonehenge 10, Angkor Wat 2, Royal Library, Huey Teocalli, Ulticur 1 each; H
Statue of Liberty 10, Kremlin 5, Hagia Sophia 4, General Post Office, Nobel Committee 1 each. No `engineer` line in
either window.

**What happens [L with C].** Early in a game few wonders are built yet (S, turns 25-50: 4 of 41 picks, 19 % of the
candidates). By the mid and late game (M, H) almost every choice is a built wonder: the stale flags make up 90-97 % of the
candidate list and nearly all of its weight. A built world wonder then sends the player round a loop each turn: the
production chooser sees it created and calls `SetSpecializationsDirty(WONDER_BUILT_BY_RIVAL)`, the next `DoTurn` chooses
again from the same flags and again lands on a built wonder (M: 68 resets for 33 picks; H: 220 resets for 22 picks in
5 turns, one per city that chooses production). So the player never has a buildable desired wonder: the desired-wonder
bonus and the engineer's hurry never apply, and `m_iNextWonderWeight` is the weight of built wonders. With the fix the
lists hold only buildable wonders (M 41 candidates instead of 1,048), the picks fall from 33 to 12 (M) and 22 to 9 (H),
and the resets from 68 to 9 and 220 to 1. The `stale` lines left with the fix are desired wonders chosen before the
window and loaded from the save (Ulticur, Grand Temple, ...) or valid picks completed by someone else during the window;
they last until the player's next choice event, and a national wonder has no reset path (both as before; the fix only
keeps built wonders out of a new choice).

**The fix changes the game (as expected, a behaviour change):** pin against a9:
- M 150-160: DIFFERENT; first differing turn **154** (`BuildingYields` and `ReligionBeliefYields`), then
  `MilitarySummary` 155, `InstantYields` 156, `BuildingInstantYields`, `BuildingsOverview`, `HandicapYields`,
  `MapUnitsState` 157, `MapPlotsState`, `WorldStateLog` 158; `PolicyChoices`, `ReligionChoices`, `TechChoices`,
  `civ_turn_era` the same.
- H 300-305: DIFFERENT; first differing turn **302** (`BuildingInstantYields`, `BuildingYields`, `InstantYields`,
  `ReligionBeliefYields`, `WorldStateLog`), then `MapPlotsState`, `MapUnitsState`, `MilitarySummary`, `TechChoices` 303,
  `BuildingsOverview`, `HandicapYields` 304; `PolicyChoices`, `civ_turn_era` the same.
- S 0-50: DIFFERENT only at the last turn: `WorldStateLog.BasicNeedsMedian` at turn **50** (545 / 536); the other 14
  tables of 19,016 rows the same.
All fixed runs passed (result line "passed", Lua errors 0): the game otherwise runs.

**Bug 0508's fix on these windows:** pin = f0508 on M, H, S and T 0-200 (IDENTICAL; 351,496 rows on T; S and T found
pantheons, so on this game the fix leaves today's x64 release result unchanged, as 4801's scratch check found on the
batch-2 pin).

## 3. The fix

Fork branches `fix-a9-x64` (from `4f4daac70`) and `fix-a9-i0ship` (from `39f17f754`), one commit each with the same
text: in `ChooseWonder`, after the best-for-wonder test, skip a building whose class is maxed out for the game
(`CvGame::isBuildingClassMaxedOut`), the team (`CvTeam::isBuildingClassMaxedOut`) or the player
(`CvPlayer::isBuildingMaxedOut`): the same three tests `CvPlayer::canConstruct` makes before its visible-list line,
with no "making" count, so a wonder our own city is building is still a candidate.

- `fix-a9-x64` **caf579d9f**, `fix-a9-i0ship` **6b8379894** (pushed 2026-11-17 19:21 EST; 6 lines in
  `CvWonderProductionAI.cpp`, same text on both).
- x64 release build [C, 2026-11-17 18:50-19:04 EST]: `tools/x64-dll/build_all.py --src <worktree of fix-a9-x64>
  --config release --offline --unlocked` at this ticket's branch: every step rc 0, narrowing gate 794 against 798 (0 new),
  DLL `384a50e57cf5f9e93aa43bad0ae225f36252d6166e26a9347a1b7ddcc8da366c` (23,669,760 B), version
  "Release-5.4.8-58-gcaf579d9f5 Clean" (`C:/c5-64bit-work/4805/b-a9-x64/dll/`).
- 32-bit release build [C, 19:12-19:20 EST]: `build_vp_clang.py --config release` (clang-cl 19.1.7, LTO, link 330 s) in
  the worktree through `quietlock.py run`: DLL `d9a62a002a25258205a4839f8f7ad4ca8f6b0c8e2ad6d7e0bb81e7a5c482ddcd`
  (`C:/c5-64bit-work/4805/dlls/a9-i0/`). Not repeatable (as the pin's): the hash belongs to this build.
- The counting build (section 2) with `VP_A9_FIX=1` plays exactly the fix branch's game on M, H and S, so the counts of
  the fix are the fix branch's.

## 4. x64 = x86 parity and the save test on the fixed DLLs [L, 2026-11-17 19:57:07-20:19:22 EST, workstation]

`tools/d10/run_d10.py --x86-dll <fixed 32-bit DLL d9a62a00...> --srv-bin <pin layout bin with the fixed x64 DLL
384a50e5...> --huge-save <Release-5.4.8-HUGE_0350, c499d3c6...> --order-exception none --d18-x86 yes --d18-x64 yes --jobs
6`, AI logging off in both arms (D49): **PASS**, every part of tiny 0-100, standard 0-200 and huge 350-400 (writer,
behaviour, cross-load; only the version-string exception; versions "Release-5.4.8-45-g6b83798944 Clean" and
"Release-5.4.8-58-gcaf579d9f5 Clean"). **x86 direct = x64 direct** (the same game on both builds): 31,226 / 349,074 /
683,261 rows. The pins' own save test has 30,666 / 351,496 / 689,879 rows (0463), so the fix changes all three D24 games,
as a behaviour change must: **every D24 reference of the pins is replaced when the fix is pinned**. Report
`perf/evidence/0504/4805/d10-a9-report.txt`; run folder `F:/c5-64bit-work/4805/d10-a9`.

Bug 0508's fixed DLLs, the same test, 19:35:54-19:57:06 EST: PASS, 30,666 / 351,496 / 689,879 rows (the pins' counts);
details in `upstream-drafts/0508-pantheon-holy-city.md`.

## 5. Not verified

- The counts come from three windows of AI-only games (two loaded saves, one new game to turn 50), not a whole game.
- The fix leaves the stale best-for-wonder flags in place (only the choice ignores them) and does not touch the other
  oddity of `SetBestWonderCities` (its reset loop clears `eClass`, not `it->second`); whether fixing those would change
  more is not studied.
- Timing: the fix shortens `ChooseWonder`'s list (M: 41 candidates instead of 1,048 over the window) and removes most
  of the built-by-rival resets; no time was measured here (COMMON 23: any speed claim goes through the testbed).
- Not run on upstream master; the draft's statement that the change applies there rests on reading master's
  `ChooseWonder` (same code around the change).
