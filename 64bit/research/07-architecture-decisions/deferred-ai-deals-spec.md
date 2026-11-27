# Deferred AI diplomacy: an Extra (spec, one-off 9086, 2026-11-25)

**The owner's statement of intent** (to the MC, 2026-11-25 22:3x EST, relayed by the MC). The MC framed it as "AI
turn processing should never wait on human input; queue order, notifications and what the AI assumes about pending
deals follow from that; the change likely lives in the DLL behind a define or option." The owner replied [reworded for privacy]: they agreed;
AI turn processing must never wait on human input, which is the core of the option, and it is to be an .ini option. Two consequences:

- **G1:** no AI turn ever blocks on a human answer. Every edge case below is judged against this goal.
- **G2:** the switch is a `VP64Options.ini` key, offered by the installer (D255).

How the interview answers measure against G1: every answer in section 1 is consistent with it. Under the queue route
the AI never waits: contacts are queued, the AI treats them as unanswered, and the answers take effect at the human's
turn start. Back-to-back screens, the re-check and the saved queue all run on the human's turn, never on the AI's. Hot
seat never blocked an AI turn in stock (`isGameMultiPlayer()`), and with the Extra its contacts also go to the queue.
One addition follows from G1 alone. With the switch on, the block in `CvPlayer::doTurn` (`CvPlayer.cpp:10355`) must
not fire on either of its conditions: a queued request, or `isDiploActive()`, a leader screen already up for any
reason. Under G1 an AI turn always goes straight on to `doTurnPostDiplomacy` (section 3.1 item 1).

The owner proposed an Extra on 2026-11-25. One-off 9086 interviewed the owner the same evening (22:30-22:55 EST),
read the DLL source, and wrote this spec and tickets 0786-0787. Nothing is implemented. Source read: the VP DLL at tag
`Release-5.5` (D259 moves the pins to VP 5.5), read in a local clone of the fork with `git show Release-5.5:<file>`.
The diplomacy files differ from the pin `1b87dd185` (5.4.8) only in small edits: 8 lines in
`CvDiplomacyRequests.cpp`, about 150 in `CvDiplomacyAI.cpp`. Line numbers below are at `Release-5.5`.

## 1. The owner's words

The owner, 2026-11-25 22:2x EST (the idea) [reworded for privacy]: all deals to the player are deferred until the start of that
player's turn, so that all processing happens without interruption until the player addresses all diplomacy at once; aimed
mostly at longer games with turn times of minutes. The owner acknowledged that this makes an asymmetry between the PC and
the human player (the human gets no information about their deals until the start of their turn) and accepted it. The owner
noted the idea is still vague and asked for a subsession to interview them about the edge cases and their expectations.

The interview, in three rounds. Each answer is the option the owner picked, with that option's text as it was put to
them:

| Time (EST) | Question | The owner's answer |
|---|---|---|
| 22:42 | Route: (A) queue, the AI decides and acts in its own turn as in stock and only the screen and the answer wait, or (B) hot-seat style, AI diplomacy toward the human moves to the human's turn start | **"A: queue (Recommended)"** |
| 22:42 | Which contacts wait | **"All leader screens (Recommended)"**: every AI-initiated leader screen: trade, friendship, defensive pact and research offers, demands, threats, peace offers, requests for help, denouncements, war declarations, comments and warnings |
| 22:42 | How they reach the human | **"Back to back (Recommended)"**: at the start of the turn the leader screens open one after another, in the order they arrived, until the queue is empty |
| 22:42 | A waiting offer that is no longer possible | **"Re-check, else withdraw (Recommended)"**: VP's multiplayer rule: an impossible item withdraws the offer with a 'deal withdrawn' notice; a changed value makes the AI rebalance or withdraw; a peace offer is re-asked |
| 22:4x | What the AI assumes while its contact waits | **"No answer yet (Recommended)"**: stock code as it is; the AI's reaction comes when the human answers and feeds into its next turn |
| 22:4x | How many waiting contacts per AI | **"As stock, no extra cap (Recommended)"** |
| 22:4x | Game types | **"Single player and hot seat"** (not the recommendation, which was single player only) |
| 22:4x | Key, default, category | **"DeferAIDiplomacy, off (Recommended)"**: `[Extras] DeferAIDiplomacy = 0`, in a new "gameplay change" group on the installer page, unticked |
| 22:5x | Which effects of the queue route are not accepted (peace offer while still attacking; offers rebalanced; pacts start a turn later; AI reacts a turn later) | [reworded for privacy] all of these are accepted, provided the installer help text documents them |
| 22:5x | Switching the option, and saves | **"Read at load; queue saved (Recommended)"** |
| 22:5x | Upstream | **"Offer later, on your word (Recommended)"** |
| 22:5x | Priority | **"p6, beta 6 or later (Recommended)"** |

## 2. Current behaviour (evidence)

### 2.1 Why a leader screen holds up the AI turn [C, source]

- In single player, each AI runs `GetDiplomacyAI()->DoTurn(DIPLO_ALL_PLAYERS)` in `CvPlayer::doTurn`
  (`CvPlayer.cpp:10319-10341`). `DoTurn` (`CvDiplomacyAI.cpp:9285`) refreshes the AI's opinions and approaches,
  then calls `DoUpdateDemands`, `MakeWar`, `DoContactMinorCivs` and `DoContactMajorCivs` (9346-9350). Through
  `DoContactPlayer`, those contacts reach a human by `CvDiplomacyRequests::SendRequest` or `SendDealRequest`.
- `SendRequest` in single player (`CvDiplomacyRequests.cpp`): when the target is the active player, it calls `Send`,
  which calls `gDLL->GameplayDiplomacyAILeaderMessage` at once, so the leader screen opens in the middle of the AI's
  turn. For any other target it calls `Add`, which only queues the request. `SendDealRequest` puts the deal in the
  UI's scratch deal (`SetScratchDeal`) before sending. For a target that is not the active player, the deal is kept
  only when `bRenew` is set (it is stored as a proposed deal); otherwise it is dropped.
- After the diplomacy step: `if ((bHasActiveDiploRequest || isDiploActive()) && !isGameMultiPlayer() && !isHuman)`
  then `SetWaitingForBlockingInput(m_eID)` (`CvPlayer.cpp:10355`). While that is set, `CvGame::update` returns at
  its top on every update until `isDiploActive()` turns false, and only then runs `doTurnPostDiplomacy()`
  (`CvGame.cpp:1626-1637`). `doTurnPostDiplomacy` is the rest of that AI's turn: tactical map, economic, military,
  religion, trade and league AI, votes, golden age, every city's turn, gold and culture.
- **What follows:** in stock single player the AI turn stops at each AI that talks to the human, and that AI's
  economy and military run only after the human has answered. The answer can change what that AI does in the same
  turn. `isGameMultiPlayer()` is `network || PBEM || hot seat` (`CvGame.cpp:4719`), so hot seat and multiplayer
  never block here.
- Measured cost: `research/03-dll-engine-boundary/interfaces-and-traffic.md` section 8 (2026-11-09, late game, turn
  350): 89.1 s of a 127.1 s AI turn and 19.9 s of a 58.3 s one were spent waiting on three leader screens (a peace
  offer, a missionary demand, a denouncement), answered by hand.

### 2.2 The two deferral modes the DLL already has [C, source]

1. **Hot seat** (`CvPlayer.cpp:10328-10333`, `CvGame.cpp:9278-9292`, `CvDiplomacyRequests::DoAIDiplomacy`): during
   their own turns, AIs run `DoTurn(DIPLO_AI_PLAYERS)`. While a human's turn is active, `CvGame` calls
   `DoAIDiplomacy(active human)` whenever no modal window, popup or city screen is up. That runs
   `DoTurn(DIPLO_SPECIFIC_PLAYER, human)` for the next AI in player order and stops as soon as a request is pending.
   This is the owner's route B, which the owner did not choose. Side note [I, not verified]: each such call runs the
   whole of `DoTurn`, `DoCounters` included (9364), so in hot seat an AI's diplomacy counters may advance more than
   once per turn. Not in this ticket's scope. With the Extra on, hot seat no longer uses this path (section 3.5).
2. **VP's multiplayer active diplomacy** (`MOD_ACTIVE_DIPLOMACY`, used when `isReallyNetworkMultiPlayer()`). About
   88 guarded sites in `CvDiplomacyAI.cpp` (40), `CvDealClasses.cpp` (9), `CvNotifications.cpp` (9),
   `CvDiplomacyRequests.*` (11), `CvGame.cpp` (7) and others. AI contacts go into the human's `CvDiplomacyRequests`
   queue as `NOTIFICATION_PLAYER_DEAL_RECEIVED` notifications ("X wants to talk"). The deal is stored as a proposed
   deal (`CvGameDeals::AddProposedDeal`). When the human opens one, `ActivateNext()` takes the deal back, checks
   `AreAllTradeItemsValid()`, and asks the AI `IsDealWithHumanAcceptable`. If the deal is no longer acceptable, the
   AI tries `DoEqualizeDeal` twice and otherwise withdraws it with `TXT_KEY_DEAL_WITHDRAWN_BY_THEM`. A peace treaty is
   rebuilt by `IsOfferPeace`, or withdrawn. A renewal keeps its terms with a "want more" line. At the end of an AI's
   `DoTurn`, the multiplayer path cancels only the proposed deals with AI players
   (`DoCancelAllProposedMPDealsWithPlayer(GetID(), DIPLO_AI_PLAYERS)`). The single-player path cancels all of that
   AI's proposed deals (`DoCancelAllProposedDealsWithPlayer`, 9351-9358), which would delete a deal queued for a human.
   This is the re-check rule the owner chose.

### 2.3 What is already saved [C, source]

`CvDiplomacyRequests::Serialize` writes the player and the request list: from-player, UI state, message, animation,
turn, lookup index, extra data. It does not write `m_bRequestActive`. `CvGameDeals::Serialize` writes
`m_ProposedDeals`, `m_CurrentDeals` and `m_HistoricalDeals`. A queue of waiting contacts and their deals is therefore
saved by today's format. The Extra adds no save field (D239).

### 2.4 Out of scope: contacts that never hold up the AI turn [C, source]

World Congress, city-state messages and quests, and the other notifications do not block. Direct
`GameplayDiplomacyAILeaderMessage` calls outside the request queue are all reactions to something the human did in
their own turn: `CvDealAI::DoHumanOfferDealToThisAI` and `DoAcceptedDeal` (trade-screen replies),
`CvCity::BuyPlot` (a tile bought next to the AI), `CvUnit::PerformCultureBomb`, and Lua `DoForceDenounce`. These
stay immediate.

## 3. Design

### 3.1 Behaviour with `[Extras] DeferAIDiplomacy = 1`

1. During an AI's turn, every AI-initiated contact toward a human player (trade, friendship, defensive pact and
   research offers, demands, threats, peace offers, requests for help, denouncements, war declarations, comments) is
   queued in that human's `CvDiplomacyRequests`. No leader screen opens, and the AI turn is never blocked (G1):
   `doTurn` goes straight on to `doTurnPostDiplomacy`. Neither a queued request nor `isDiploActive()` sets
   `SetWaitingForBlockingInput` while the switch is on.
2. The AI decides and acts in its own turn exactly as in stock: it declares war, denounces or makes demands then.
   While a contact waits, the AI treats it as unanswered ("No answer yet"). There is no cap beyond stock's own rules.
3. At the start of that human's turn, once no modal window or turn-start popup is up, the queued screens open one
   after another in arrival order until the queue is empty. That order is the AIs' player order, then the order in
   which each AI sent them.
4. Each deal is re-checked as it opens, by the multiplayer rule of section 2.2: still valid and acceptable, shown;
   rebalanced by `DoEqualizeDeal`; peace re-asked; otherwise withdrawn with the "deal withdrawn" notice.
5. The human's answer runs the stock answer path (`CvDllPlayer` `ActiveRequestComplete`, the AI's
   `DoFromUIDiploEvent` handlers), so the AI's reaction happens at the human's turn start and feeds into its next turn.

### 3.2 Where the change lives

- **The DLL**, on a fork branch from the VP 5.5 pin, behind a compile-time define (proposed name
  `VP64_DEFER_AI_DIPLOMACY`; D104, D168). The 32-bit build without the define is byte for byte unchanged. The change
  consists of:
  - a runtime switch (one static bool, off by default);
  - in single player and hot seat with the switch on: `SendRequest` and `SendDealRequest` take the multiplayer
    branch's "store the proposed deal and `Add`" path in place of `Send` and the scratch deal;
  - `doTurn` never sets `SetWaitingForBlockingInput` for a queued request;
  - hot seat AIs run `DoTurn(DIPLO_ALL_PLAYERS)` in place of `DIPLO_AI_PLAYERS`, and `DoAIDiplomacy` is skipped;
  - the AI's end-of-`DoTurn` cancel keeps human-directed proposed deals, as the multiplayer branch does;
  - the drain at the human's turn start opens the queue back to back, using `ActivateNext`'s re-check. The
    multiplayer code's notification is replaced by an automatic open whenever the modal stack is empty.
  
  Reuse the multiplayer code by widening its condition (a helper such as `IsQueuedAIDiplomacy()` =
  `(isReallyNetworkMultiPlayer() && MOD_ACTIVE_DIPLOMACY) || deferSwitch`) only where the behaviour must be the same.
  Each site of the about 88 is checked on its own: much of that code deals with network messages and must not run in
  single player.
- **The switch's value** comes from `VP64Options.ini`, read by the server at game start and at each load ("Read at
  load"), and handed to the DLL through one small export under the same define, called by the server after it loads
  the DLL and before each game starts or loads. It is not a game option stored in the save: the owner chose "read at
  load", so a save carries no setting. Toggling the key takes effect from the next AI turn after a load. Whether the
  server calls the export, or the DLL asks the server through the existing host interface, is the implementer's
  choice. Name the route in `options-file.md`.
- **Options file** (`options/src/vp64_options.cpp`): `[Extras] DeferAIDiplomacy`, range 0 to 1, default 0, in a new
  Extras category "gameplay change" (neither `kCatBugfix` nor a visual addition). The MC records the decision for the
  category: D169 limited Extras to bugfixes and visuals, while D255 requires behaviour changes to be configurable.
- **Installer**: an unticked checkbox in a "Gameplay changes" group. The owner asked for help text that states each
  effect the owner accepted:
  - an AI that offers peace keeps attacking for the rest of its turn;
  - offers may be rebalanced or withdrawn by the player's turn;
  - pacts and deals start at the player's turn, not during the AI's;
  - the AI reacts to answers one turn later;
  - the player learns of AI contacts only at the start of their turn.
  `docs/OPTIONS.md` carries the same text.

### 3.3 Saves (D239)

No new save field (section 2.3). A save made with contacts waiting reopens them at the human's turn start in any
patch build of the same VP version, with the Extra on or off. The drain and its deal restore must therefore run
whenever the queue holds requests, whatever the switch says. Only the "queue in place of show" side is switched.
Open point O2: the player's own 32-bit DLL (stock VP, D225) has no such drain.

### 3.4 Autoplay, observers, AI-only games

With no human player (AI autoplay, observer, headless runs), nothing is queued, so the Extra does nothing. A human slot
under AI control (`isHuman(ISHUMAN_AI_DIPLOMACY)` false) is not a human target, as in stock.

### 3.5 Hot seat

With the Extra on, hot seat follows the same route. AIs do all their diplomacy, toward humans included, in their own
turn, and queue each human's contacts in that human's queue. Each human's queue opens at the start of that human's
turn. Today's turn-start diplomacy in hot seat (`DoAIDiplomacy`) is off, so an AI runs `DoTurn` once per turn. With
the Extra off, hot seat stays exactly as in stock. Network multiplayer is unchanged either way, because it already
has VP's own queue.

## 4. Risks

1. **AI code that expects an immediate answer.** In stock, the AI's own `doTurnPostDiplomacy` runs after the answer.
   With the Extra on, it runs before. Handlers that set state when the human answers (demand accepted, help given,
   peace signed) then run during the human's turn, which is the timing the multiplayer path already uses. The
   implementer lists each `DoFromUIDiploEvent` case and checks that it needs nothing that only exists mid-AI-turn.
   The multiplayer path is evidence that the code can tolerate this, not proof, since it sees less play than single
   player.
2. **Multiplayer-only code in single player.** Some `MOD_ACTIVE_DIPLOMACY` branches send network messages or assume
   simultaneous turns. Widening a condition site by site, with each site written down, is required.
3. **The UI.** The leader screen's Lua (`LeaderHeadRoot.lua`, `DiscussionDialog`, `DiploTrade`) is opened by the same
   `GameplayDiplomacyAILeaderMessage` either way. Opening it at the human's turn start rather than mid-AI-turn needs a
   live check: turn-start popups, the leader scene option (`[Proxy] LeaderScenes`), and the server's lockstep (D242).
4. **Determinism and x86/x64 identity.** With the Extra off, game results must be identical to the build without it.
   The x86/x64 identity runs (D164) keep it off, because the 32-bit DLL does not have it. With the Extra on, games
   differ from stock by design. The queue order is player order, so a run is repeatable.
5. **Turn time.** The point of the Extra is to remove the human wait from the AI turn. The ticket measures the AI-turn
   wall time on a save where AIs contact the human, with the Extra on and off (`docs/briefs/PERFORMANCE.md` for any
   claim). Re-checking at the human's turn start adds a few `DoEqualizeDeal` calls there.
6. **Upstream.** Written to suit upstream as a VP switch (the define, a plain bool). Nothing is sent without the owner's
   word, and not before D130/D257 allow it.
7. **A leader screen already up during an AI turn (G1).** Stock also blocks on `isDiploActive()`. A comment in
   `DoContactMajorCivs` warns of "a nasty bug where the AI will continue making decisions as the diplo screen is
   firing up". With the switch on, that block is gone (G1), so the implementer lists every way a leader screen can
   still be up while an AI plays its turn: Lua `GameplayDiplomacyAILeaderMessage`, a screen left open from the human's
   turn, the hot-seat hand-over. For each one, check that the AI's turn changes nothing that screen's trade or answer
   relies on. If one does, the fix makes the screen re-check or close. It never makes the AI wait.

## 5. Open points

- **O1 (MC):** a decision for the new Extras category "gameplay change": D169 says Extras are bugfixes or visuals;
  D255 says behaviour changes are configurable. Also the category's group switch, if it has one (such as
  `EnableGameplayChanges`, default 1 or 0), or none.
- **O2 (implementer, then MC):** a save made with contacts waiting and loaded by the player's own 32-bit VP DLL (patch
  turned off, D225). That DLL's single-player `Update` shows the front request with `Send` and sets no scratch deal.
  Measure where the turn-start autosave falls relative to the drain. If an autosave can hold queued trade requests,
  either drop pending trade requests before such a save, or note it.
- **O3 (implementer):** the full list of the about 88 `MOD_ACTIVE_DIPLOMACY` sites, each marked "widen", "keep
  multiplayer-only" or "not relevant", in this file. *Answered 2026-11-26 by 0786: section 7.3 (89 lines, 67 run-time
  sites) and 7.4 (the sites outside that list).*
- **O4 (implementer):** the side note of 2.2.1 (whether hot seat's turn-start `DoTurn` advances counters twice),
  checked for the record. Fixing it is not this ticket's scope. *Answered 2026-11-26 by 0786: section 7.6 (no).*

## 6. Tickets

- `tracker/issues/0786-extra-deferred-ai-diplomacy.md`: part 1, the DLL (the define, the runtime switch and its
  export, the queue and drain, O3, O4, headless identity). Parent 0006, p6, beta 6 or later, ready.
- `tracker/issues/0787-extra-deferred-ai-diplomacy-wiring.md`: part 2, the options key through the server, the
  installer row and help text, docs, the live acceptance on a Goldberg instance, O2. Blocked on 0786 and O1.
- `tracker/issues/9086-interview-deferred-ai-deals.md`: this interview.

## 7. Implementation (ticket 0786, part 1: the DLL), 2026-11-25/26

impl-0786 (account B). Line numbers in this section are at fork `i0-vp55` dc6b8ad48 (Release-5.5 plus the carried i0
commits; the diplomacy files are Release-5.5's), not at the branch. Marks: `[C]` read in the source by impl-0786,
`[L]` a build or run on the workstation, `[I]` inferred.

### 7.1 Branches and files

| Fork branch | Head | What |
|---|---|---|
| `vp64-defer-ai-diplomacy` | b3932ad6c | the change, one commit on `i0-vp55` dc6b8ad48 (lead-0763's choice, thread 0763 #16200) |
| `x64-vp55-defer-ai-diplomacy` | 25c0e3808 | `x64-vp55` a2627c8ac merged with it (automatic, no conflict); the x64 builds below |

New files `CvGameCoreDLL_Expansion2/VP64DeferAIDiplomacy.h` (declarations, the switch's rules, the stats block) and
`VP64DeferAIDiplomacy.inl` (implementation, included at the end of `CvDiplomacyRequests.cpp`, as `VP64HiddenEmbark.inl`
is at the end of `CvUnit.cpp`); the header is included from `CvGameCoreDLLPCH.h` under the define. Hooks in
`CvDiplomacyRequests.cpp/.h`, `CvPlayer.cpp`, `CvGame.cpp`, `CvDiplomacyAI.cpp`, `Lua/CvLuaPlayer.cpp`: 121 lines added,
none removed or changed. Every hook is `#if defined(VP64_DEFER_AI_DIPLOMACY)`, with the original line in the `#else`
where a line is replaced. No `#line` directives (as on x64-vp55, whose x64 layer has none): a define-off build
differs from the base only in later `__LINE__` values (`PRECONDITION`, the logging macros), so "unchanged without the
define" is shown by games (7.8), not by bytes. No tracker or decision ids in the source or the commit message (D95).

**Switch** (`VP64DeferAIDiplomacy.h` top comment): (1) the export `int VP64DeferAIDiplomacyControl(int iSet,
VP64DeferAIDiplomacyStats* pOut)` (iSet 0/1 sets it, any other value only reads; returns the version, 1; fills
`pOut` up to its `iSize`, as `VP64HiddenEmbarkControl` does); (2) else the environment variable
`VP64_DEFER_AI_DIPLOMACY` = 0/off or 1/on, read once at first use; (3) else `VP64_DEFER_AI_DIPLOMACY_DEFAULT` (0).
The value is read whenever an AI turn runs, so a host sets it only between games (0787: after loading the DLL and
before each start or load, "read at load"). It is not saved. **In effect** (`IsDeferred()`) = switch on and neither
`CvPreGame::isReallyNetworkMultiPlayer()` nor `isPlayByEmail()`: single player and hot seat only. Tests:
`VP64_DEFER_AI_DIPLOMACY_DIR` = a folder; at DLL unload `DeferAIDiplomacy.txt` there lists the switch, its source and
the counters (queued, shown, withdrawn, shown_as_stock, blocks_skipped, handovers_skipped).

**Product builds:** `tools/x64-dll/build.py`'s marker table (MARKER_DEFINES) does not have the new header yet, so a
product build (`build_all.py`, the take) compiles the feature out. Turning it on in the product is one row
`('VP64DeferAIDiplomacy.h', ('VP64_DEFER_AI_DIPLOMACY',))` (0787's or the pin step's; not in 0786's touches). The
x64 define-on DLLs of this ticket are built with `perf/speedkit/kitbuild.py --define ...` (7.8).

### 7.2 What the change does (with the switch in effect)

1. **Queueing** (`VP64DeferAIDiplomacy::QueueFor(eFrom, eTo)`): eFrom is an AI (`!CvPreGame::isHuman`), eTo is a human
   (`isHuman(ISHUMAN_AI_DIPLOMACY)`), and it is **not eTo's own turn as the active player** (eTo active and turn-active).
   In the human's own turn nothing waits on a screen, so replies to what the human does (trade-screen answers,
   reactions inside `DoFromUIDiploEvent`, a war declared on a refused demand) open at once as in stock. Queued:
   `Queue()` stores the deal as a proposed deal (an empty `CvDeal(eFrom, eTo)` for a contact without one, so every
   queued request has exactly one stored deal, in the same order) and `Add`s the request. `SendRequest` and
   `SendDealRequest` check `QueueFor` first (before their multiplayer branch).
2. **AI turn never waits:** `CvPlayer::doTurn` does not set `SetWaitingForBlockingInput` (neither for
   `bHasActiveDiploRequest` nor for `isDiploActive()`; `SkipBlock()` counts each turn stock would have held), and
   `CvGame::CheckPlayerTurnDeactivate` does not hold the sequential turn handover on `isDiploActive()`
   (`SkipHandover()`, counted). Queued requests are never "active", so `HasActiveDiplomacyRequests` (the other
   handover condition) stays false for the AI.
3. **The AI's end-of-`DoTurn` cancel** keeps deals with humans: `CancelProposedDealsWithAIs` is
   `CvGameDeals::DoCancelAllProposedDealsWithPlayer` with the human players skipped (so an AI-only game runs the stock
   code path's exact calls).
4. **Hot seat:** AIs run `DoTurn(DIPLO_ALL_PLAYERS)` in their own turn; `CvGame::updateTimers`' turn-start
   `DoAIDiplomacy` is off; contacts to any human (not only the active one) are queued for that human.
5. **No queue clear at the game turn:** `CvGame::doTurn`'s `ClearAllRequests` loop (runs whenever
   `MOD_ACTIVE_DIPLOMACY`, single player included) is skipped; it would delete the contacts queued in the AI turns
   before the humans' turns start.
6. **No "X wants to talk" notifications:** `CvDiplomacyRequests::BeginTurn` (it runs in single player too) does not
   turn queued requests into `NOTIFICATION_PLAYER_DEAL_RECEIVED`; the drain opens them by itself.
7. **The drain** (`CvDiplomacyRequests::VP64Drain`, which replaces the single-player branch of `Update()` with the
   define on, **whatever the switch**, so a save made with contacts waiting opens them in any build, D239). Each
   update, for the active turn-active human, the front request:
   - from an AI **with a stored deal** of that pair: only when no request is active and the modal stack is empty, no
     diplomacy screen or popup waits and no city screen is up (`DoAIDiplomacy`'s condition). A sender that died: its
     deal and the request are dropped. A demand, request for help or gift whose items are no longer possible
     (`AreAllTradeItemsValid`) is withdrawn with `TXT_KEY_DEAL_WITHDRAWN_BY_THEM` (the multiplayer re-check checks
     only offers; added here because the human's state can change between the AI's turn and theirs). Otherwise the
     deal (the oldest of the pair: requests and deals were stored in one order) is made the newest and
     `ActivateNext()` runs: VP's multiplayer re-check of an offer (valid and acceptable: shown; else `DoEqualizeDeal`
     twice; a peace offer rebuilt by `IsOfferPeace`; a renewal keeps its terms with "want more"; else withdrawn with
     the notice), `SetScratchDeal`, the leader screen. The next request opens after the EXE reports the screen closed
     (`ActiveRequestComplete`) and the modal stack is empty again: back to back, in arrival order.
   - any other request (from a human, or one the stock code queued without a deal): the stock code (`Send` if the
     sender is alive, then pop), with the switch off exactly as stock; with it on, one at a time with the same screen
     condition, and a trade screen without its deal (an identical deal of the same pair was taken by an earlier
     request: `RemoveProposedDeal` erases all equal copies) is dropped.
8. **Direct leader screens of an AI turn** go through the queue too (7.4).

### 7.3 The `MOD_ACTIVE_DIPLOMACY` sites (O3)

89 lines at i0-vp55 `[C]`. **22 are not run-time tests:** the definition (`CustomMods.h:45`), a comment
(`CvDiplomacyAI.cpp:406`) and 20 `#if defined(MOD_ACTIVE_DIPLOMACY)` guards, always true because the name is a macro
(`CvDealClasses.cpp` 4302, 4333, 5624, 5927; `CvDealClasses.h:199`; `CvDiplomacyAI.h:1161`; `CvDiplomacyRequests.cpp`
16, 216, 225; `CvDiplomacyRequests.h` 46, 78, 88; `CvGame.cpp` 8468, 8686, 9295; `CvNotifications.cpp` 17, 264, 893,
1714, 1742). The 67 run-time sites:

| Site | What it guards | Verdict |
|---|---|---|
| `CvDiplomacyRequests.cpp:130` `Update` | MP: activate the requests of one sender; else SP: `Send` the front request | **widen (drain):** the SP branch is `VP64Drain` (7.2 item 7) whatever the switch |
| `CvDiplomacyRequests.cpp:217` `Add` | MP: `BeginTurn` (notifications) and `Update` at once | keep MP-only: the deferred queue makes no notification and opens nothing during the AI turn |
| `CvDiplomacyRequests.cpp:458` `HasActiveRequestFrom` | MP: a queued request counts as active | keep MP-only: widening it would hold the AI turn (`CheckPlayerTurnDeactivate` waits while `HasActiveDiplomacyRequests`), against G1 |
| `CvDiplomacyRequests.cpp:509` `SendRequest` | MP: store an empty deal and `Add` | **widen:** `QueueFor` first, the same storing in `Queue()` |
| `CvDiplomacyRequests.cpp:550` `SendDealRequest` | MP: store the deal and `Add` | **widen:** `QueueFor` first (the MP branch's from/to swap kept) |
| `CvPlayer.cpp:10330` `doTurn` | MP: AIs `DoTurn(DIPLO_AI_PLAYERS)` | keep MP-only; inside its `else`, hot seat's `DIPLO_AI_PLAYERS` is **widened** to `DIPLO_ALL_PLAYERS` when deferred (7.4) |
| `CvPlayer.cpp:34570` `setTurnActive` | MP: `s_aDiploHumans` | keep MP-only |
| `CvPlayer.cpp:45835` `HasActiveDiplomacyRequests` | not MP: also scan others' active requests from this player | not relevant: queued requests are not active |
| `CvGame.cpp:1725`, `8687` | MP: `DoAIMPDiplomacyWithHumans` | keep MP-only |
| `CvGame.cpp:8470` `doTurn` | MOD (SP too): `ClearAllRequests` of every major | **off when deferred** (7.2 item 5) |
| `CvGame.cpp:9296` `updateTimers` | not MP: hot seat's turn-start `DoAIDiplomacy` | **off when deferred** (inner `isHotSeat()` test) |
| `CvDiplomacyAI.cpp:731` `update` | SP: greet the active player from `m_aGreetPlayers` through `SendRequest` | not relevant: unchanged, `SendRequest` queues it when it comes in an AI turn |
| `CvDiplomacyAI.cpp:7507` `ChangeNumCiviliansReturnedToMe` | MP: `SendRequest`; SP: direct screen to the active player | keep: the human's own action, shown in the human's turn |
| `CvDiplomacyAI.cpp:9353` `DoTurn` | MP: cancel only deals with AIs; SP: cancel all | **widen (equivalent):** a deferred branch cancels deals with AIs only (7.2 item 3) |
| `CvDiplomacyAI.cpp:14064`, `14088`, `14113`, `14327` | MP: `IsHasMet` flag, teammates' and humans' trade priority (`DoUpdateHumanTradePriority`) | keep MP-only: AI opinion and contact order; the Extra keeps the AI's decisions as stock |
| `CvDiplomacyAI.cpp:27581` `MakeWar` | MP: every target is a UI target | not relevant: deferred hot seat runs `DIPLO_ALL_PLAYERS`, where `IsValidUIDiplomacyTarget` passes every target |
| `CvDiplomacyAI.cpp:27737` `DeclareWar` | MP: `SendRequest` the war declaration; SP: direct screen to the active player | **widen:** the SP direct screen goes through `LeaderMessage` and the gate adds any human `QueueFor` names (hot seat) |
| `CvDiplomacyAI.cpp:33223` `DoFirstContact` | MP: `SendRequest` the greeting; SP: the greet list | keep: the SP greet list reaches `SendRequest` (row 731) |
| `CvDiplomacyAI.cpp:33341` `DoKilledByPlayer` | MP: `SendRequest`; SP: direct "defeated" screen | keep: a dead AI's farewell, shown at once (7.5); queued, the drain would drop it with its dead sender |
| `CvDiplomacyAI.cpp:34109` `DoSendStatementToPlayer` (friend's denouncement) | MP: `SendRequest` to any human target; SP: to the active player | **widen:** the SP gate adds any human `QueueFor` names |
| `CvDiplomacyAI.cpp` 33772, 33796, 33821, 33846, 33872, 33897, 34213, 34333, 34359, 34384, 34409, 34436, 34462, 34488, 34532, 35088, 35189, 35213, 35237, 35284, 35307, 35331, 35418 (23 sites in `DoSendStatementToPlayer`) | an offer to an AI player: MP `FinalizeMPDeal`; SP `AddProposedDeal` + `FinalizeDeal` | keep MP-only: AI to AI, no human; the AI-only identity runs depend on it |
| `CvDiplomacyAI.cpp:35446` `DoContactMajorCivs` | MP: humans by trade priority, one at a time | keep MP-only: the SP branch already contacts every human after the AIs |
| `CvDiplomacyAI.cpp:36919` `DoBeginDiploWithHuman` | MP: open the queued requests of that AI when the human clicks its leader | keep MP-only: the drain empties the queue while the modal stack is empty, before the human can click a leader `[I]` |
| `CvDiplomacyAI.cpp:55488` `DetermineVassalToLiberate` | MP without MOD: skip humans | not relevant |
| `CvDiplomacyAI.cpp:58069` `DoLiberatedFromVassalage` | MP: `SendRequest`; SP: direct screen to the active player | **widen:** `QueueFor` takes the MP branch (which calls `SendRequest`, which queues) |
| `CvDealAI.cpp:299` `DoAcceptedDeal`, `585` `DoAcceptedDemand` | MP: `FinalizeMPDeal`; SP: `AddProposedDeal` + `FinalizeDeal` | keep: the human's answer runs in the human's turn on the local path; the queued deal is already out of the proposed list (`ActivateNext` took it into the scratch deal) |
| `CvDealAI.cpp:6235` trade screen closed | MP: a net message clears the renewal id | keep |
| `CvDealClasses.cpp:4303` `AddProposedDeal` | MP: one human-to-human deal per pair | keep (human to human, network) |
| `CvDealClasses.cpp:5398` `HasMadeProposal` | MP: loop even when empty | not relevant (same result) |
| `CvDealClasses.cpp:5928` | MP: deal log column | keep (logging) |
| `CvDealClasses.cpp:6373`, `6478` `GetHistoricDeal*` | MP: newest first | keep (UI list order) |
| `CvDllGameDeals.cpp:93`, `120`, `129` | MP: the MP deal functions for an AI-human pair | keep: SP's local functions; while the queue drains no trade screen of another AI can be opened, and after it no AI-to-human proposed deal of the queue is left `[I]` |
| `CvDllNetMessageHandler.cpp:764` | MP: `FinalizeMPDealLatest` | keep |
| `CvNotifications.cpp:268`, `894`, `1716`, `1745` | MP: `NOTIFICATION_PLAYER_DEAL_RECEIVED` handling | not relevant: the deferred queue makes no such notification (7.2 item 6) |

### 7.4 Sites outside that list (changed)

| Site | Stock | With the switch in effect |
|---|---|---|
| `CvPlayer.cpp:10362` `doTurn` | SP: `SetWaitingForBlockingInput` on a request or `isDiploActive()` | never (`SkipBlock`) |
| `CvPlayer.cpp:10336` `doTurn` | hot seat AIs `DoTurn(DIPLO_AI_PLAYERS)` | `DIPLO_ALL_PLAYERS` |
| `CvGame.cpp:1817` `CheckPlayerTurnDeactivate` | the next player waits while `isDiploActive()` | does not wait (`SkipHandover`) |
| `CvDiplomacyRequests.cpp:171` `BeginTurn` | queued requests become notifications | not for the deferred queue |
| `CvDiplomacyAI.cpp:28001` `DoAttackedCityStateStatement`, `28115` `DoBulliedCityStateStatement` | after declaring war over a protected city-state, a direct screen to the active player (runs in the AI's `DoContactPlayer`) | `LeaderMessage`: queued outside the human's turn; the gate adds any human `QueueFor` names |
| `CvDiplomacyAI.cpp:42973` `DoWarnCoopWarTarget` | direct screen to the active human target, after `SetForceDiscussionModeQuitOnBack(true)` | queued (without the back-button flag: set at queue time it would apply to whatever screen came next) when `QueueFor`; else stock |
| `Lua/CvLuaPlayer.cpp:12280` `lDoForceDenounce` | direct screen to the active player (a script; may run in an AI turn) | `LeaderMessage` |

### 7.5 Leader screens that can still be up during an AI turn (risk 7, G1)

None of these waits any more (7.2 item 2); none carries a deal, so no answer relies on state the AI turn changes
`[C]` unless noted:

- `DoKilledByPlayer`'s "defeated" screen (row 33341): the sender is dead; text only.
- Lua `Game.GameplayDiplomacyAILeaderMessage` (`CvLuaGame.cpp:2019`): a script's screen, default root, text "TEMP";
  not routed (no target player in the call).
- Screens of the human's own actions that the stock code opens directly (`CvCity::BuyPlot` and the embassy culture
  bomb, `CvUnit::PerformCultureBomb`, `ChangeNumCiviliansReturnedToMe`, `CvDealAI` trade replies,
  `DoFromUIDiploEvent` replies): gated on the acting human being the active player, so they come in that human's turn
  `[C]`; an AI turn starts only after the human ends the turn, which the modal screen prevents `[I, not verified
  live; 0787]`.
- Requests the EXE adds through `CvDllPlayer::AddDiplomacyRequest` (`CvDllPlayer.cpp:408`): go to the queue; without
  a stored deal the drain shows them the stock way `[I: the EXE's callers were not traced]`.
- Hot seat: a human's queue opens only while that human is active and turn-active; the "next player" screen is modal,
  so the drain waits for it `[I]`.

### 7.6 O4: hot seat's turn-start `DoTurn` and the counters

`[C]` `CvDiplomacyAI::DoCounters` (`CvDiplomacyAI.cpp:31659`) at Release-5.5 increments nothing: it ends a
denouncement or a friendship when `getGameTurn()` minus the stored turn reaches the relationship duration, so a second
call in the same turn changes nothing after the first. The other `DoTurn` steps recompute opinions, approaches and
war states; they were not checked one by one for effects of a repeated call. With the Extra on, hot seat runs each
AI's `DoTurn` once per turn (7.2 item 4).

### 7.7 Effects to name in the installer help text (0787), found in the code `[C]`

In addition to the five of section 3.2:

- A deal renewal offer arrives after the expiring deal ended: `CvGameDeals::DoTurn` (in `CvGame::doTurn`, between
  the AI turns and the human's) ends it and clears the renewal ids (`ClearAllRenewDealIDs`), so at the human's turn
  the renewal is re-checked as a new offer (rebalanced or withdrawn like any other) instead of keeping its terms.
- Answers are stamped with the human's turn, one turn after the AI asked (for example
  `SetOtherPlayerSidedWithProtectedMinorTurn` in the minor-civ responses): the AI's memory of the answer lasts from
  then.
- A demand, request for help or gift whose items became impossible is withdrawn with the notice (7.2 item 7).
- A war the AI declares on a refused demand is declared during the human's turn (the AI's attack starts in its next
  turn, as in stock).

`DoFromUIDiploEvent` (`CvDiplomacyAI.cpp:38609`, the risk-1 check) `[C]`: the human-started cases (`HUMAN_*`,
`DENOUNCE`, `COOP_WAR_OFFER`, `HUMAN_DEMAND`, `HUMAN_REQUEST`, `HUMAN_ENDS_VASSALAGE`, `HUMAN_DISCUSSION_SHARE_*`,
`HUMAN_MOVE_TROOPS_RESPONSE`) run in the human's own turn as before. The answers to AI contacts (`DEMAND_HUMAN_REFUSAL`,
`REQUEST_HUMAN_REFUSAL`, `AGGRESSIVE_MILITARY_WARNING_RESPONSE`, `I_ATTACKED_YOUR_MINOR_CIV_RESPONSE`,
`I_BULLIED_YOUR_MINOR_CIV_RESPONSE`, `ATTACKED/KILLED/BULLIED_MINOR_RESPONSE`, the expansion and plot-buying warning
responses, `WORK_WITH_US_RESPONSE`, `HUMAN_END_WORK_WITH_US_RESPONSE`, `MEAN_RESPONSE`, `COOP_WAR_RESPONSE`,
`PLAN_RA_RESPONSE`, `AI_REQUEST_DENOUNCE_RESPONSE`, `CAUGHT_YOUR_SPY_RESPONSE`, `KILLED_MY_SPY_RESPONSE`,
`STOP_CONVERSIONS`, `STOP_DIGGING`) read the stored diplomacy state, `eFromPlayer` and the UI's arguments at answer time
and re-test what they act on (`canDeclareWar`, sanity targets, bankruptcy, the bluff roll); none reads
`m_eDiploMode`, `m_eTargetPlayer` or the temp deal, the values that exist only inside the AI's `DoTurn`.
`COOP_WAR_NOW_RESPONSE` is commented out at Release-5.5. The minor-civ responses assert that the pledge is still active
(`ASSERT`, debug builds); a pledge broken in between only fires that assert `[I]`.

### 7.8 Builds and identity runs `[L, 2026-11-26 00:00-01:20 EST, workstation, shared machine]`

Evidence and the scripts as run: `server/evidence/0786/` (`RESULTS.md`). Commands are runbook 1.7 (`build_all.py`),
1.2 (`build_server.py --deps`), 1.8 (kitbuild, the define-on DLL), 1.9 (`build_vp_clang.py`), 3.11 / the take's
drift stage (`tools/drift/run_pair.py`), with the scripts' exact arguments.

| Build | Source | Result |
|---|---|---|
| x64 product path: `build_all.py --config release --unlocked --offline` | x64 branch 25c0e3808 (marker defines on, the new define off) | OK in 559 s (DLL step 385 s with the LTO link); DLL `81FA639A...`, 16,672,256 B (the same size as 0764's x64-vp55 DLL F6AC13DC); narrowing gate 0 new sites; smoke on the 5.5 database ok, "Supporting 22 major civilizations" |
| x64 server and proxy: `server/build_server.py --deps` of it, release | the same | `build_server: OK` in 218 s; proxy `9A471EEC...` |
| x64 define on: `kitbuild.py --config release --define VP64_HIDDEN_EMBARK --define VP64_SPEED_DANGER --define VP64_SPEED_DANGER_DG7 --define VP64_DEFER_AI_DIPLOMACY` | x64 branch, final head (RESULTS.md) | compiles without warnings in the new code; LTO link with the repeatability check OK; export `VP64DeferAIDiplomacyControl` present (llvm-objdump), absent from the define-off DLL |
| 32-bit, define off: `build_vp_clang.py --config release` in the fork worktree | feature branch 2384fed58 | DLL `0B4BF3C6...`, 13,474,304 B, 379 s |

The define-off content of the later feature commits is unchanged (they touch only `VP64DeferAIDiplomacy.inl`, which a
define-off build does not include), so the define-off builds at 25c0e3808 / 2384fed58 stand for the final heads `[C]`.

**Identity** (Tiny fixed-seed Continents, seeds 30017/40017, turns 0-100, on the Release-5.5 database of 0764's take,
d27ffe6b...; every run 32,901 statistics rows; `compare_stats.py` with the drift stage's two excluded columns):

| Pair | x86 arm | x64 arm | Result |
|---|---|---|---|
| P1 | the branch, define off (0B4BF3C6) | x64-vp55 without the define (0764's take, F6AC13DC) | IDENTICAL |
| P2 | i0-vp55 (0764's take, 645BBE96) | define on, switch off (no environment) | IDENTICAL |
| P3 | the branch, define off | define on, `VP64_DEFER_AI_DIPLOMACY=1` | IDENTICAL; the DLL's report: enabled 1, source 2 (environment), every counter 0 (AI-only game: nothing queued, no wait skipped) |
| P4 | (none) | the branch's product-path DLL (81FA639A) in its own layout | IDENTICAL to P2's x86 |

Final define-on DLL E570D75B (x64 branch a98c277ed); P2 and P3 ran with it, P1-P4 first with the earlier 8E1D56A9
(the same code but for the export's read-only path and the report file), all IDENTICAL. In the second P3 the x86 arm
(the branch's define-off DLL, which the variable cannot reach) stalled in c5h's boot on the loaded machine and was not
repeated; that DLL is IDENTICAL in three other runs. All arms compared against P2's x86 arm (i0-vp55, the pin): IDENTICAL. So: the 32-bit build without the define
plays as the pin; the x64 define-off build, the define-on build with the switch off and with the switch on (no human)
play as x64-vp55; x86 = x64 holds for the combined pair with the define off and on (lead-0763's point 2, #16200).

Not run here: the save test (no 5.5 reference saves yet: 0766/0771); a `--shorten` narrowing gate on the define-on
build (kitbuild has none; the new code has no 64-to-32 conversion `[C]`); any game with a human (0787, live);
`/fsanitize=address` (no native test suite exists for this DLL code; the live check is 0787's).
Lost on the way: the first fast build on F: copied its source at about 2 MB/min (F: has no Defender exclusion) and was
stopped; builds moved to `C:/c5-64bit-work/0786/` (excluded). A `ctypes` load of the DLL outside the server died in the
loader (exit 127), so the export is checked statically. The server ends with `TerminateProcess` (no DLL static
destructors), so the first test report written at unload never appeared; it is now written at each event.
