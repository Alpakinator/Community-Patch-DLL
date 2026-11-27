# A third Extras category for extras that change rules or saves (idea 0458, stage 1 study)

Ticket 0458, written 2026-11-26 20:40-21:06 EST by its implementer (account A, from the queue, D275). A study: nothing
in the product is changed, nothing was built or run. Its recommendations are for the MC; the decisions it needs are
listed in section 7, and the step tickets are drafted in section 8 for the MC to number (COMMON rule 26) and file
ready or blocked (D274).

Marks: `[C]` read by this ticket in the source or the documents named on the line; `[I]` inferred, with what would
confirm it; `[C, by <file>]` a fact another ticket recorded, cited, not re-checked here. Nothing here is `[L]`.

DLL source read: the VP 5.5 pin `x64-vp55` a2627c8ac, the kept read-only copy
`C:/c5-64bit-work/keep/pins-5.5/src-x64-a2627c8ac/CvGameCoreDLL_Expansion2/` (line numbers below are in that copy).

## 1. Where the idea comes from, and why it is no longer only an idea

- **The owner, 2026-11-16** (to the Architect, `architect-2026-11-16-increment-4-extras-epic.md` section 1): [reworded for privacy] yes, visuals only
  for now; a third category of extras flags may be explored for extras that change rules or saved state, or in
  any way make the game incompatible with mainline in saves or at run time (not display-only). D169 reserved the name
  `EnableGameplayChanges` (proposal 4.1) and said nothing more; its design "would cover a marker in saves and a rule for
  multiplayer peers" (4.5).
- **The code keeps the reservation** [C]: `options/include/vp64_options.h:220-228` (enum `Category`: `kCatNone`,
  `kCatSwitch`, `kCatBugfix`, `kCatVisual`; the comment says the third category is "reserved by name only ... no value,
  no switch, no row"); `CONTRIBUTING-extras.md:29-32`, `docs/OPTIONS.md:587-588`, `docs/extras/flags-and-messages-design.md:66,180`
  (a test checks `FindKey("EnableGameplayChanges")` gives `kCount`).
- **D255 (2026-11-25)** widened the rule that a feature has a key: anything that "alters base game behavior" is
  configurable in `VP64Options.ini` and on the installer's page. D169's "extras are display only" was not withdrawn. The
  two now meet in one real feature:
- **The first rule-changing extra exists in code.** `[Extras] DeferAIDiplomacy` (one-off 9086, the owner's interview
  2026-11-25; `deferred-ai-deals-spec.md`): AI contacts to a human are queued for that human's turn start. "With the
  Extra on, games differ from stock by design" (spec 4.4). Its DLL part 0786 is **done** (fork branches
  `vp64-defer-ai-diplomacy` 8d1455fe0, `x64-vp55-defer-ai-diplomacy` a98c277ed; merged in this repository a57f06873)
  [C, by `tracker/issues/0786-*.md`]. Its wiring 0787 is `state: ready` on main, but its acceptance says the key goes
  into "a new Extras category gameplay change only after the MC's decision on spec O1" [C, `git show main:tracker/issues/0787-*.md`
  at 048c19abf]. Spec O1 asks exactly this ticket's question (spec section 5). **So this study is on 0787's path:**
  O1 should be decided before 0787 is claimed, or 0787's implementer stops at its first step.

## 2. What a "gameplay change" is, against the other two categories

| | Bugfixes (`EnableBugfixes`, default 1) | Visual additions (`EnableVisualAdditions`, default 0) | **Gameplay changes (`EnableGameplayChanges`, proposed default 0)** |
|---|---|---|---|
| Changes | the picture, where stock draws wrongly | the picture, new polish | **what the DLL decides**: rules, AI, timing of decisions, what is saved |
| Rule E1 (same game with the flag on and off gives identical statistics) | holds | holds | **replaced by G1-G3 (section 6)**: identical with the flag off; differs by design with it on |
| Lives in | the server (renderer), sometimes the proxy | the server | **the DLL**, behind a compile-time define (D104, D168), switched at run time by the server |
| Per machine in multiplayer | yes: display only (`quick-combat-extra.md` "Multiplayer" [C]) | yes | **no**: must be one value for every peer (section 5) |
| Marked in the save | no | no | **yes** (section 4) |

Not gameplay changes, though they touch the AI turn or the DLL [C, `docs/OPTIONS.md` 295-432]: `[Performance]
AiUpdateTickMs`, `AiSkipVisualDrain`, `AiDrainUnordered`, `GameEventsOneCrossing` and `HiddenEmbarks` each state "the
game's results do not change"; HiddenEmbarks is a DLL change switched by the server through the export
`VP64HiddenEmbarkControl` (`server/src/srv_hidden_embark.cpp`), the same mechanism a gameplay change uses, but it holds
E1. They stay where they are. `QuickCombatAnimations` (D270) is display only.

## 3. Candidate extras for the category

| Candidate | Source | Fits? | State |
|---|---|---|---|
| **`DeferAIDiplomacy`** | `deferred-ai-deals-spec.md`; 0786 done, 0787 ready | **Yes, the first user.** Changes when the AI's diplomacy takes effect; single player and hot seat only (in effect = switch on and not `isReallyNetworkMultiPlayer()` and not `isPlayByEmail()`, spec 7.1 [C, by 0786]); "read at load", the save carries no setting (the owner's interview answer, spec 1) | waits on O1 |
| A1 battle-simulation candidate lists reused (the largest AI speedup of the other port) | `research/08-performance/speedup-epic-proposal.md` section 6 item 3 | Yes: "changes how the AI fights by design". The proposal's recommendation: "revisit only as an option of the reserved third Extras category if late-game turns are still too slow" | not wanted now |
| A2 one diplomacy re-evaluation per war cascade | same, item 4 | Yes (small drift later) | not wanted now |
| A4 skip the single-unit opportunity attack with nothing near | same, item 5 | Yes, but "a small saving ... for a change in unit decisions"; recommendation no | not wanted |
| A9, A10 (wonder pick, path-finder stamp) | D181 | **No**: taken as core DLL defect fixes on both pins, no flag | done as fixes |
| 43-civ variant, more civilisations (0595), massive maps (0594), one release for 22/43 (0469) | ideas | **No**: these are build variants or map limits, not a switch a player turns on in a running install; a save of one civ-count variant is read badly by the other [C, by `first-beta-known-issues-and-gaps.md` A1.5]. They need a variant mark, which can share the mechanism of section 4 (a named game option), but not this category's switch | ideas at p1 |
| Turn-time quick modes, hidden embarks | D191-D197, D246 | **No**: results unchanged (section 2) | shipped |

So the category has one real member today and three shelved ones. The design below is sized for that: small, generic
where it costs nothing, and no machinery the first member does not need.

## 4. The marker in saves

### 4.1 What a save can carry, and what reads it [C]

The save layout (`research/sources/civ5-headless/saves.md` section 2): an uncompressed header (the save-affecting mods,
the DLL's GUID and version string), then the **pregame** as `ICvPreGame1::write` writes it, then the zlib body
(`Game::Write` with the embedded save database, map, teams, players).

| Place | Who checks it on a load | Effect of adding to it |
|---|---|---|
| Header mod list (a mod with `AffectsSavedGames` 1) | **the EXE**: it activates exactly the save's list on a load (`save-mod-mismatch.md` 2) | a "VP64 gameplay changes" mod would have to be installed and activated; the EXE would demand it even with the patch off. Heavy, and changes the mod set the player chose. **Not recommended** |
| New fields in the DLL's body (`CvGame::Write` ...) | the DLL; a DLL without them misreads the rest (the 0681 class of failure: a hash warning, then `Deserialization Error`, then a read past the end) | breaks D239 (one VP version's builds interchangeable) and the player's own 32-bit DLL (D225). **Ruled out** |
| The embedded save database (`Civ5SavedGameDatabase.db`, `Modding.OpenSaveData()`) | nobody; every DLL carries it untouched | possible, but only Lua and the DLL read it, after the body; the server would need to open the embedded SQLite |
| **A named game option in the pregame** | nobody refuses it; every DLL keeps it | **recommended**, below |

**Named game options** [C]: the pregame keeps every game option as a `(name, int value)` pair in
`std::vector<CustomOption> s_GameOptions` (`CvPreGame.cpp:262`). `SetGameOption(const char*, int)` adds any name, with
no database row needed (1006-1031). `GetGameOption(const char*, int&)` looks the name up in the vector and falls back to
the `GameOptions` table's default (920-951): a name in neither gives 0 and false. The whole vector is written into the
pregame archive (`saveTo << s_GameOptions`, 3673) and read back on a load (`loadFrom >> s_GameOptions`, 1886): **a
name the reading DLL does not know is carried through and ignored.** The host gives a joining multiplayer peer the
same vector through the net init info (`CvDllNetInitInfo.cpp:35, 168, 205, 253`: `m_aGameOptions` taken from the
pregame, streamed, and set with `CvPreGame::SetGameOptions`). The DLL's pregame interface has
`GetGameOptionCount`, `GetGameOption(index, name buffer, ...)` and `SetGameOption(name, value)`
(`CvDllPreGame.h:57-60, 159`), and the server already sets named game options through it in direct mode
(`server/src/srv_main.cpp:685-692` from its `gameoption_<NAME>` settings, and 797-799). **So the marker needs no DLL
change and no new save field.**

Not checked [I]: that the EXE's save path writes the pregame the server's DLL holds at the moment of the save (it calls
`ICvPreGame1::write` through the proxy; the 0681 and 0354 work read the body and the header, not the pregame's
options). One live save on a Goldberg instance confirms it (step ticket S3).

### 4.2 The proposed marker

- **Name:** one game option per gameplay change, `VP64GC_<Key>` (for example `VP64GC_DeferAIDiplomacy`), value 1. The
  prefix keeps the names out of the `GAMEOPTION_` space of VP and of mods; no `GameOptions` row is added, so the setup
  screen never lists it and the database (and its game data hash, `save-mod-mismatch.md` 1) is unchanged.
- **Meaning: "this game has been played with this change in effect at some time".** The server sets it when the change
  is in effect at a new game's start or after a load, and never clears it: a later load with the key off leaves the
  mark (the game's history had the change). This is what a bug report needs: a save sent to VP's tracker can be seen to
  carry a non-stock rule, whatever the options file of the reporter says now.
- **What sets the behaviour is a separate, per-extra choice**, recorded in the extra's options row:
  - *per load* (the ini decides at each start and load): `DeferAIDiplomacy`, as the owner chose ("Read at load"). The
    mark only records;
  - *per game* (fixed at the game's creation, the saved option decides, the ini only sets new games): the form any
    later change that must stay constant through a game would use (an AI rule that a mid-game switch would make
    incoherent). The server then reads `VP64GC_<Key>` after a load and hands that value to the DLL, and logs when the
    ini differs. No candidate needs it today; build it with the first that does.
- **The log and the report:** at each start and load the server logs one line `gameplay changes: in effect <list>;
  save marks <list>; unknown marks <list>` and the same into its fatal report's reason, so every crash report from a
  game with a rule change says so (D118 report, `report.txt`).

### 4.3 Refusal on a build without the change

What a load can meet, and the proposed rule:

| Loaded by | Sees the mark? | Proposed |
|---|---|---|
| A patch build that has the change (any key value) | yes | loads; *per load*: the ini decides; *per game*: the saved value decides |
| A patch build of the same VP version **without** the change (older or newer) | the server lists its options (`GetGameOptionCount`), finds a `VP64GC_` name it does not know | **loads, with one loud log line and the name in reports; no refusal.** Because no gameplay change may add a save field (rule G4, section 6), the save is readable; the game simply goes on with stock rules from there. A refusal would only block the player (D239 is about loading) |
| The player's own 32-bit VP DLL (patch off, D225) | nothing checks it | loads and plays on with stock rules. Nothing can refuse there without the header-mod route of 4.1, which is not recommended. Each extra states what its leftovers do under stock (DeferAIDiplomacy: spec O2, contacts left in the queue) |
| A multiplayer peer | see section 5 | |

So "refusal" becomes: **no gameplay change may need a refusal.** The rules of section 6 (no save field; state only in
what stock already saves; a drain that runs whatever the switch, as 0786 did, spec 7.2 item 7) keep every save
loadable everywhere. If a future candidate cannot meet that, it is not an extra but a variant (section 3) and comes to
the owner as such.

## 5. The rule for multiplayer peers

Facts [C]:
- **Network multiplayer does not run on the product today.** The sync archives (`GetSyncArchive`, `send*SyncCheck`)
  are "multiplayer-only: loud stubs" on the proxy route (`research/03-dll-engine-boundary/proxy-design.md:478-483`) and
  stubs in the server host (`server-host.md` table, `sendUnitSyncCheck` ... "no response; multiplayer"). Multiplayer and
  hot seat have never been tried with the patch (`first-beta-known-issues-and-gaps.md` A1.4, C11). Mixed 32/64-bit
  lockstep is broken by design: every peer must run the same build (`x64-dll-port.md:341`).
- Game options are the one per-game setting that every peer already shares: the host's vector reaches each peer (4.1).

The rule, proposed for when multiplayer is supported (and stated now, so no extra is built against it):

1. **One value for every peer.** A gameplay change in network multiplayer or play-by-e-mail takes its value from the
   game (the host's `VP64GC_<Key>` option, carried to peers by the net init info), never from each machine's ini.
2. **A *per load* change is off in network multiplayer and play-by-e-mail**, whatever the ini says, with one log
   line. `DeferAIDiplomacy` does this already in the DLL (`IsDeferred()`, spec 7.1); network multiplayer has VP's own
   queue anyway (`MOD_ACTIVE_DIPLOMACY`). The server's generic rule repeats it, so a later extra cannot forget it.
3. **Hot seat** is one machine and one ini: a *per load* change applies to every seat, as 0786 built it (spec 3.5).
4. **Peers run the same patch build** (already required, `x64-dll-port.md:341`); with rule 1 that gives the same
   rules. The DLL's own sync checks, once served, catch any divergence.

Until the product runs network multiplayer, rules 1 and 4 have nothing to act on; rule 2 is the only one with code.

## 6. Rules for a gameplay change (proposed, beside E1-E12 of the proposal 4.2)

| # | Rule | How it is checked |
|---|---|---|
| G1 | **Off is stock.** With its key off (or its category off) the game is identical to the build without the change | the D10 statistics identity, like with like (D164): x86 pin = x64 with the define off = x64 with the define on and the key off (0786's runs are the model, spec 7.8 [C, by 0786]) |
| G2 | **On is deterministic.** The same save with the key on gives the same game twice on one build | an A/A pair of headless runs |
| G3 | **Different by design, and stated.** Every effect a player would notice is in the installer's help text and `docs/OPTIONS.md` (the owner's word for DeferAIDiplomacy, spec 1) | review |
| G4 | **No save field.** State lives only in what stock already saves (the pregame's options, existing containers, the save database); any code that consumes such state runs whatever the switch (0786's drain) | review; a save made with the key on loads in the 32-bit pin's DLL headless (the cross-build save test of D239) |
| G5 | **Marked.** The server sets `VP64GC_<Key>` while the change is in effect (4.2) | the server's unit test; one live save |
| G6 | **One value for every peer** (section 5) | review; a test of the server's multiplayer rule on a pregame set to network multiplayer |
| G7 | **In the DLL behind a define**, the 32-bit build unchanged without it (D104, D168); switched by the server through an export, never by a setting stored in the DLL's own files | build with and without the define |
| G8 | **Default 0, always**, unless the owner rules otherwise for a named change (section 7, decision 2) | review |
| G9 | **Its cost and saving are measured** (the point of the candidates is turn time) by `docs/briefs/PERFORMANCE.md` | a run record per arm |

E1 (display only) does not apply; E2 (off is stock), E3 (one flag, one document), E4/G8, E5 (independent), E6 (failure
falls back to stock), E7 (nothing hidden is shown), E8, E10, E12 apply unchanged. E9 (pictures) and E11 (messages to the
renderer) do not apply. Statistics comparisons (the metrics ledger, D10, parity) run with `EnableGameplayChanges = 0`,
as the stock comparisons already set both other switches to 0 (proposal 4.2).

## 7. What has to be decided (for the MC; the MC numbers the decisions and asks the owner where it is the owner's)

1. **Open the category** (answers spec O1): `EnableGameplayChanges` becomes a real switch, category value 4
   (`kCatGameplay`), with `DeferAIDiplomacy` its first member. Recommended: yes. The alternative, putting
   `DeferAIDiplomacy` in `[Performance]` like HiddenEmbarks, hides a rule change among options that promise "results do
   not change". D169's "visuals only" is then read as "bugfixes and visual additions are visuals only"; the owner's
   words of 2026-11-16 already invited this category. Whether this needs the owner's word or is the MC's under D255 is
   the MC's call; the owner's interview answer for DeferAIDiplomacy already named [reworded for privacy] a new group for gameplay changes on the
   installer page (spec 1).
2. **The switch's default and the members' defaults.** Recommended: switch 1, every member 0 (G8). Reason: the switch
   is then a master "off" for players who want plain VP, while each change still needs its own tick; a switch at 0
   would make a player tick two boxes for one change. The alternative (switch 0, as `EnableVisualAdditions`) is the
   safer reading of "gameplay off by default" (D169's first words: "gameplay off"). Either way no change is on until
   the player ticks it. **This is a product default, so the owner's.** Until decided, S1 builds the switch with
   default 1 and members 0, which leaves every game stock.
3. **No separate installer prompt.** The visual additions have one prompt that sets their switch (proposal 4.1);
   rule changes are chosen one by one on the options page, in a group "Gameplay changes" (the owner's choice for
   DeferAIDiplomacy). Recommended: no prompt.
4. **No refusal; marks are records** (4.3). Recommended: yes. It keeps D239 and D225 whole.
5. **Per load and per game** both allowed, named per extra (4.2); only per load is built now. Recommended: yes.

## 8. Step tickets (drafts for the MC; ids by the MC, COMMON rule 26)

Hours are estimates [I]. Each is filed ready or blocked with its blocker (D274), parent 0006, increment 4.

**File overlap with 0787** [C, 0787's `touches` on main at 048c19abf]: 0787 already touches every options and
installer file S1 needs (`options/include/vp64_options.h`, `options/src/vp64_options.cpp`,
`options/tests/test_options.cpp`, `tools/installer/options_schema.py` and its test, `docs/OPTIONS.md`) and
`server/src/srv_main.cpp`, which S2 needs. Two writers never touch one file (COMMON 6), so the order is S1 -> 0787 ->
S2 -> S3. **Recommended instead: fold S1 into 0787** (0787 creates the category together with its first member; about
3 h more on its 8, one merge fewer), then S2 and S3 as below. Both forms are drafted; the MC picks.

**S1. The options category `EnableGameplayChanges`** (state: blocked, blocker: the MC's decision on section 7 items
1-2; then ready; priority as 0787's, 6, since 0787 needs it; size 4-6 h; trust any; needs none; or folded into 0787).
Touches: `options/include/vp64_options.h`, `options/src/vp64_options.cpp`, `options/tests/test_options.cpp`,
`tools/installer/options_schema.py`, `tools/installer/tests/test_options_schema.py`, `docs/OPTIONS.md`,
`docs/extras/flags-and-messages-design.md`, `research/07-architecture-decisions/options-file.md`.
Acceptance: `kCatGameplay = 4` and the switch row `[Extras] EnableGameplayChanges` (range 0-1, default per decision 2,
`kCatSwitch`), the category order of the default file and of `Extra()`/`DescribeExtra` extended, the schema's
`CAT_GAMEPLAY` with the group "Gameplay changes" under its switch, the reserved-name test changed into a test of the
row; options unit tests, their ASan arm (`options/test_asan.py`) and the installer schema tests pass, with a negative
test for a gameplay extra whose help names another category's switch; no member row yet (0787 adds the first).
`CONTRIBUTING-extras.md` is public text: S1 drafts its new paragraph in its hand-back for the owner (D95, proposal
4.6) and does not commit it.

**S2. The server's gameplay-change mark and multiplayer rule** (state: blocked, blocker: 0787 merged (with S1 in it,
or S1 and 0787); size 6-10 h; trust any; needs none for the hermetic part).
Touches: `server/src/` (a new `srv_gameplay_marks.cpp/.h`, the start and load hooks in `srv_main.cpp` beside
`SrvHiddenEmbark::Apply` (line 552), and `remote/srv_remote_main.cpp` if the remote load hook lives there),
`server/tests/`, `research/07-architecture-decisions/idea-0458-third-extras-category.md`.
Acceptance: for each `kCatGameplay` extra in effect, after a new game's start and after each load the server sets
`VP64GC_<Key>` = 1 through `ICvPreGame1::SetGameOption` and never clears one; at each load it lists the save's
`VP64GC_` options (`GetGameOptionCount`, `GetGameOption(index, ...)`) and logs one line `gameplay changes: in effect
...; save marks ...; unknown marks ...`, and the fatal report's reason carries the same; in network multiplayer or
play-by-e-mail every per-load gameplay extra is off with one log line; a hermetic test of the pure part (the lists,
the unknown-name rule, the multiplayer rule) and a negative test (an unknown mark loads, logs, never refuses); one
offline direct-mode load (`server/run_server.py --load`) of a headless save made with a `gameoption_VP64GC_Test=1`
setting shows the mark read back. The mark is generic over every `kCatGameplay` row, so `DeferAIDiplomacy` gets it
without a change of its own.

**S3. Live: the mark survives the EXE's save and loads everywhere** (state: blocked, blocker: S2 merged;
size 2-4 h; trust any; needs a Goldberg game instance, never the Steam install or the owner's saves).
Acceptance: on the product, a new game with `DeferAIDiplomacy = 1`, saved by the game's own Save; the save's pregame
holds `VP64GC_DeferAIDiplomacy` (read back by the server's load line in a second session with the key 0: mark listed,
change off); the same save loads in a patch build without the change (its unknown-mark line) and headless in the
32-bit pin's DLL (G4, D239). Confirms the [I] of 4.1.

**Amendment to 0787 (the MC's ticket, not a new one):** its acceptance (1) reads "in category `kCatGameplay` under
`EnableGameplayChanges` (decision <n>)" in place of "only after the MC's decision on spec O1"; if S1 is folded in, S1's
acceptance is added to it and its size goes to about 11 h; its `blocker` is the MC's decision on section 7.

Totals: 12-20 h of work for S1-S3 [I] (S1 3-6 of them); 0787 can be claimed as soon as the decision is recorded.

Not drafted (no candidate needs them now): the *per game* mode (4.2), built with its first user; a reader of a save's
pregame options for offline tools (the server's load line covers bug reports); the header-mod enforcement of 4.1
(not recommended).

## 9. Not verified

- That the EXE's save path writes the server's pregame options into the file and that a load restores them before the
  server's load hook runs [I; S3 checks it live]. The DLL side is [C].
- That no part of the EXE or of a front-end screen lists or rewrites unknown named game options on a load (the setup
  screen lists `GameOptions` rows only [I]); S3 sees it.
- The hours.
