# Upstream draft: the AI's next desired wonder can be a wonder that is already built (for the owner; NOT sent)

Ticket 4805, 2026-11-17. Nothing below has been sent (D130: drafts wait for the owner's yes to the exact text, after the
beta). It names no internal ids, decisions, project names or cherry-pick lines (D95). The finding, the counts and the
checks it rests on are in `../a9-wonder-specialization.md`.

**Checked first [C, 2026-11-17 18:37 EST]:** upstream `LoneGazebo/Community-Patch-DLL` has no issue or pull request on
it (`gh search issues` and `gh search prs` for `ChooseWonder`, `IsBestForWonder`, `BestForWonder`,
`SetBestWonderCities`, "wonder already built", "AI wants wonder already built", "next desired wonder", "best wonder
city": no match on the topic; the only hit, #4142 "another questionable building choice" of 2018, is about another
choice). Master `499b958b4` (2026-11-17) still has the code as described: `ChooseWonder` has no built test, and
`SetBestWonderCities` sets the flag only. The fix commit's change applies to master's `ChooseWonder` as is (the only
nearby difference is `!m_Buildables.empty()` for `m_Buildables.size() > 0`, outside the change).

If the owner says yes: an issue, and the pull request below from a fork branch on master.

**Issue title:** AI can pick an already built wonder as its next desired wonder

**Issue text:**

> `CvPlayer::SetBestWonderCities` sets a city's best-for-wonder flag for each wonder some city can still construct,
> but it never clears the flag once the wonder is completed: a completed class is no longer scored, so nothing touches
> its flags again. `CvWonderProductionAI::ChooseWonder` takes every building whose class carries that flag in some city
> and filters only through `CheckBuildingBuildSanity`, which has no "can still be built" test (no maxed-out check, no
> `canConstruct`). So a world wonder completed by anyone, or a national wonder we already have, stays a candidate and
> can be picked as `m_eNextWonderDesired`.
>
> What follows: no city can start it, so the desired-wonder bonus in `CheckBuildingBuildSanity` never reaches a
> buildable wonder; its weight is part of `m_iNextWonderWeight`, which feeds the wonder specialization weight; when it
> is a world wonder, `AI_chooseProduction` sees it as "built by a rival" and asks for a new pick, which can fall on a
> built wonder again.
>
> How often, counted with a build that logs every choice (5.4.8, AI-only autoplay): in a Standard Communitu game at
> turns 150-160, 31 of 33 choices were already built wonders, and built wonders made up 90 % of the candidate list
> (944 of 1,048 entries, 89 % of its weight); in a Huge Continental Drift game at turns 300-305, 21 of 22 choices
> (97 % of the candidates); in a new Standard Continents game played to turn 200, 436 of 548 (4 of 41 before turn 50,
> then 74-92 % in every 25-turn block). A built world wonder also makes a loop: every
> city that chooses production sees it as built by a rival and asks for a new pick, and the next turn's pick lands on
> a built wonder again (68 such requests in the Standard window, 220 in the five Huge turns).

**Pull request title:** Skip already built wonders when choosing the next desired wonder

**Pull request text:**

> `ChooseWonder` now skips a building whose class is maxed out for the game, the team or the player
> (`CvGame::isBuildingClassMaxedOut`, `CvTeam::isBuildingClassMaxedOut`, `CvPlayer::isBuildingMaxedOut`), the same
> tests `CvPlayer::canConstruct` makes for the construction list. A wonder that one of our cities is building is still
> a candidate (no "making" count is added). The stale best-for-wonder flags themselves are left as they are.
>
> Tested on 5.4.8 with a build that logs every choice (AI-only autoplay, same saves, release builds): with the change
> no built wonder is chosen in any of the games above, and the games otherwise play normally. It changes AI
> production choices from the first affected turn, so games from the same save diverge after a few turns.
