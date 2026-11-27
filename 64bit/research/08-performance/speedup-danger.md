# The danger package (ticket 4807, epic 0504): the enemy-reach cache as product code, DG7, DG3, DG4, MB5

impl-4807 (account A), 2026-11-17. Package `VP64_SPEED_DANGER` on fork branch `perf/speed-danger`, cut from the x64 pin
`4f4daac70` (pin batch 3, `deps/dll.lock`). Built from epic 0437's prototype (b) (ticket 4674, fork branch
`x64-proto-0437-reach` `0d15296d0`, `VP64_PROTO_REACH`) and its hand-over
`perf/evidence/0437/4680/handover-b-danger-reach-cache.md` (on `epic-0437-turn-study`; cited below as "HO" with its
section). **Since 2026-11-18 the package's current branch is `perf/speed-danger-b4` on pin batch 4 `c29442e8c`**
(section 8.5; `perf/speed-danger` stays the batch-3 branch the earlier evidence names). The product code is written fresh; nothing of the prototype is merged. Marks: `[C]` read in the code at the
pin `4f4daac70`, `[L]` seen in a run, `[I]` inferred. Line numbers are the pin's unless said otherwise.

Section 1 is the design (written before the code, as the ticket asks). Results go to section 8 as they come.

## 1. What is cached and what is not changed

`CvDangerPlots::UpdateDangerSingleUnit` asks, for every enemy unit an observer can see,
`TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pUnit->plot(), iFlags, 0, pUnit->maxMoves(), zoc)`. The question
carries nothing of the observer except the ZOC list (empty in VP: `COMBATAI_TWO_PASS_DANGER` is 0), so all observers of
one unit ask the same question in one turn (HO 3). The package keeps the answer (`ReachablePlots`) per unit and returns it
while nothing the search read has changed. The danger values built from it (`AssignUnitDangerValue`, capture lists,
fog) are not touched by the cache item (DG7 does that, section 5).

## 2. Ownership and the two-thread hazard (the Architect's review section 2)

**Rule: the cache entries are used only on the game-core thread, through the core thread's own path finder.**
`CvGlobals::GetPathFinder()` returns `m_pathFinder` when `gDLL->IsGameCoreThread()` and `m_interfacePathFinder`
otherwise (`CvGlobals.cpp:3405`) `[C]`. The server answers `IsGameCoreThread` true only on the thread inside the dispatch
of `ICvGame1::Update` (headless: `srv_ifaces.cpp` `t_bInUpdate`, set by `srv_main.cpp` `CoreThread`; client:
`SrvRemote::InUpdate()`) `[C]`. Every AI turn, and so every danger update of a turn, runs inside `Update`.
`VP64Danger::Reach` checks `gDLL->IsGameCoreThread()` first; on any other thread it runs the original search and counts
the call as `uncacheable_thread`, so a ui-thread danger update (the load's reconstruction is one: `CvDangerPlots.cpp`
"called from ui thread!") behaves exactly as stock, with its own finder, and never reads or writes an entry. So the
entries are owned by the core path finder: only its searches fill them, and only the thread that owns that finder reads
them.

**No entry is held across a search.** The prototype kept a `std::map` iterator across the search on a miss; a nested
call could invalidate it. The product code finds the entry, decides hit or miss, and on a miss runs the search, then
looks the key up again before it stores. Three more guards against nesting (none expected: the danger flags carry
`MOVEFLAG_IGNORE_DANGER`, so the search makes no lazy danger update `[C]`):
- a depth counter: a `Reach` entered while another `Reach` is inside its search runs uncached and stores nothing, and
  the outer one stores nothing either (`not_stored_nested`);
- the region is read from the finder's closed list only when the finder's generation id moved by exactly one during
  the search (one `FindPathWithCurrentConfiguration`, no nested search, no failed `Configure`, which returns an empty
  result and leaves the previous search's closed list) (`not_stored_gen`);
- the event counter and the epochs are read **before** the search (the prototype read the counter after it): a change
  made during the search, by any thread, has a later stamp and voids the entry at its first lookup.

**Hook raises inside the search.** The search raises a Lua hook only through three options, all 0 in VP
(`NewCustomModOptions.xml`) `[C]`: `EVENTS_CAN_MOVE_INTO` with a unit type flagged `SendCanMoveIntoEvent`
(`CvUnit::canMoveInto`, `CvUnit.cpp:5237`), `EVENTS_AIRLIFT` (`getAirliftFromPlot` `:8369`, reached from
`UpdateNodeCacheData` for every node) and `EVENTS_SEALIFT`. A hook answer is an input no stamp can follow (Lua state),
and the raise frees the game-core lock. So a unit is **uncacheable** while `MOD_EVENTS_AIRLIFT` or `MOD_EVENTS_SEALIFT`
is on, or `MOD_EVENTS_CAN_MOVE_INTO` is on and its unit type sends the event. This is a gap the hand-over did not list
(new item 10 of section 3). With those excluded no hook is raised between the lookup and the store.

**The invalidation state is shared, and that is correct.** Stamps and epochs describe game state, and a game-state
write on any thread must void entries. The event counter is incremented with `InterlockedIncrement` (one unique,
increasing value per event even when the ui thread writes game state outside the lock, as stock lets some Lua calls
do); a stamp is one aligned 32-bit store. A race can only make an entry look changed (a later stamp), never unchanged:
the counter is read before the search, every bump after that read writes a larger value `[I, x64 ordering: the
interlocked increment is a full barrier and a later load is not moved before an earlier one]`.

**Arrays are allocated only where no other thread can write them.** The stamp arrays are allocated at the first
`TurnStart` after a map exists (core thread, inside `Update`) and freed only in `CvMap::uninit` (load, new game, exit:
no turn runs then, and stock frees the plots at the same point). A bump before the allocation is dropped; nothing is
cached before the first turn start after an init or a load, so a dropped bump cannot keep a wrong entry.

### 2.1 Every static or global the package adds, with its two-thread answer

| Static | What | Written by | Read by | Live across a hook raise and reachable from a ui-thread call? |
|---|---|---|---|---|
| `g_state.cache` (map unit key -> entry) | the kept results | core thread only (`Reach`, `TurnStart`) | core thread only | No: `Reach` uses it only when `IsGameCoreThread`; no setter hook touches the map (a unit event bumps the unit's plot, below) |
| `g_state.plotStamp[numPlots]`, `g_state.teamPlotStamp[numPlots * MAX_TEAMS]` | last event per plot, per plot and team | any thread (setter hooks), one aligned 32-bit store | core thread (`Reach`) | Yes, by design: shared invalidation of shared game state; a write can only void entries (section 2) |
| `g_state.uEvent` | the event counter | any thread, `InterlockedIncrement` | core thread | as above |
| `g_state.teamEpoch[MAX_TEAMS]`, `g_state.playerEpoch[MAX_PLAYERS]` | non-plot inputs | any thread, `InterlockedIncrement` | core thread | as above |
| `g_state.bPartial`, `numPlots`, array pointers | caching off until the first turn start; sizes | `TurnStart` (core), `CvMap::uninit` (load/exit) | core thread; setter hooks read the size and pointer | The pointer and size change only at a turn start (allocation, published size last) and in `CvMap::uninit`; a setter on the ui thread at a turn start sees size 0 (bump dropped, harmless) or the new arrays |
| `g_state.stats` (per-turn counters) | coverage and attribution | core thread (lookups); event counts only in stats builds | written to a CSV by the core thread at `TurnStart` | Not read for any decision; a lost event count is a statistics error only |
| `t_iDepth` (`thread_local` int) | nesting depth of `Reach` | owning thread | owning thread | No: per thread |
| `VP64SpeedVerify.h` logger (`vp64speed::Log`) | verify and spoil builds only | any thread, under its own SRW lock | | No: locked (the kit's design) |
| DG7 (`VP64_SPEED_DANGER_DG7`, added 2026-11-18): `Entry::dg7` (the kept tiles, their snapshot, region and stamps) | the derived attack and capture tiles | core thread only (`Tiles`) | core thread only | No: inside the cache's entries; `Tiles` returns false off the core thread (stock derivation); the derivation raises no hook |
| DG7: `g_state.uDg7Key`, `bDg7Valid` | which entry the last `Reach` hit or stored | core thread (`Reach`, `TurnStart`, `GameReset`) | core thread (`Tiles`) | No: core thread only |
| DG7: `g_state.landEpoch` | landmass ids recalculated | any thread (`CvMap::recalculateLandmasses`), `InterlockedIncrement` | core thread | Yes, by design, like the epochs: a bump only voids |

A unit's own state change (promotion, embark, transport, death) must void its entry. The prototype erased the entry
from the map in the setter hook; a ui-thread setter must not touch the map, so `UnitEvent` only bumps the stamp of the
unit's plot. Every region holds the unit's own plot (added explicitly, besides the start node in the closed list), so
the next lookup misses (`region`). No setter hook touches the map.

## 3. The gaps of HO 4.6, and the new ones, closed

The product hooks are the prototype's (HO 4.1-4.5, re-read at the pin `4f4daac70`: the pin's delta from `04b1211b8`
touches no reader of HO 4 and adds no writer of an HO 4 input; `setImprovementType` gains two `pOwningCity` null checks,
line numbers in `CvPlot.cpp` move by +3 after 8313 and +5 after 8764; `DoAlterWorkingPlot` may now give a plot to a city
through `setOwningCity`, which is hooked) plus:

| HO 4.6 item | Closed by |
|---|---|
| 1, hooks before the change completes | Every setter that runs more than its member write after the hook bumps again at its end, through a scope guard (`VP64Danger::PlotScope`, `PlayerScope`, `TeamScope`, `UnitScope`) whose destructor bumps on every return path: `setHasPromotion`, `kill` (the guard holds the plot index and unit key, never `this`, since the unit can be deleted), `SetNumRealBuildingTimed`, `SetNumFreeBuilding`, `SetPolicy`, `setImprovementType`, `setOwner`, `setRouteType`, `SetRoutePillaged` (the last three raise Lua hooks mid-change) and `setHasTech` (already both ends). Entries stored inside a setter (none expected) then have an earlier stamp than the end bump |
| 2, `InitPlayerTraits` outside a tech or policy change (`setLeaderType`, religion) | `PlayerScope` in `CvPlayerTraits::InitPlayerTraits` (start and end) |
| 2a, policy branch blocked or unblocked | `PlayerScope` in `CvPlayer::processPolicies` (covers `SetPolicyBranchBlocked` and `setHasPolicy`) |
| 3, revealed owner (human units only) | team-plot stamp in `CvPlot::setRevealedOwner` when the value changes |
| 4, units added or removed outside `setXY` | the plot stamp in `CvPlot::addUnit` and `CvPlot::removeUnit` themselves (covers `CvDllPlot.cpp:330, 336` and every other caller), and in `CvPlot::Validate` after its unit-list repair when it deleted a node |
| 5, rivers | `setNEOfRiver`, `setWOfRiver`, `setNWOfRiver`: the plot and its six neighbours (a crossing is read from both sides) |
| 6, a second load in one process | `VP64Danger::GameReset()` in `CvMap::uninit` (every load, new game and exit): empties the cache, frees the arrays, sets `bPartial`; nothing is cached until the next game turn start |
| 7, the generation counter (A10) and the stale reads | not closable inside the package: section 4 |
| 8, team games | `PlayerEvent` bumps the player's team epoch too, so a team member's policy or building voids the team's entries (one player per team: no extra misses) |
| 9, not cached | unchanged: explorers, unwelcome evangelists, a non-empty ZOC list, a search that reached a sealift or admiral-port node |
| **10 (new)**, Lua hooks inside the search | uncacheable while `EVENTS_AIRLIFT` or `EVENTS_SEALIFT` is on, or `EVENTS_CAN_MOVE_INTO` with a unit type that sends it (section 2) |
| **11 (new)**, event counter wrap | a 32-bit counter; at a turn start (cache empty) above `0xF0000000` the arrays are zeroed and the counter restarts, so a stamp never wraps under a live entry |
| **12 (new)**, a ui-thread danger update | uncached (section 2) |
| **13 (new, found 2026-11-18 by the long verify window, section 8.2a)**, delayed visibility: with `CORE_DELAYED_VISIBILITY` (on in VP) `isVisible()` reads the turn's maximum, which `flipVisibility()` lowers when a team's visibility expires | team-plot stamp in `changeVisibilityCount` when the count's or the maximum's visible state changes, and in `flipVisibility` when the maximum's does (fork `64fd4c7cc`) |

## 4. A10 and the stale reads of P0.2 (HO 4.6 item 7)

A hit skips a search on the core finder. Two consequences, neither visible to the verify mode (a verify build runs the
search on every hit, so its finder history is stock's):

1. **Generation ids shift.** Every later search of the turn gets an id lower by the number of hits. An A10 alias
   (a node stamped one 65,534-search cycle earlier and read as fresh) then falls on other searches than in stock. P0.2
   saw no alias in 5 wraps `[L]`; A10's fix (4806, pin batch 4) removes the case. Until batch 4, the package is exact
   only as far as no alias occurs; re-prove on batch 4.
2. **The finder's leftover node data differ.** P0.2 counted reads of node data not refreshed by the current search
   (`PathValid` 4,667, `CanEndTurnAtNode` 2,125, `DestinationReached` 148 per H turn, and fewer elsewhere; none of
   `PathValid` and `CanEndTurnAtNode` on M) `[L, P0.2]`. Such a read sees what an earlier search left (or zeros after a
   `Reset`). A skipped search leaves no data and clears nothing, so a later search's not-refreshed read can see another
   earlier search's data than in stock. This is a dependency of stock's own results on the order of searches, which the
   cache changes; A10's fix does not remove it (P0.2: "not part of A10"). So **the only test of this is the `on` build's
   identity runs** (M, H, the fixed-seed E game, and the long game). The verify mode proves the cached result equals a
   fresh search's; it cannot prove the rest of the turn sees the same leftovers. Proposed addition to the acceptance:
   the long fixed-seed game 0-300 also in `on` mode against the pin, not only in verify mode. If an `on` run ever
   differs while verify is clean, this is the first suspect; the fix belongs to the finder (refresh before those reads,
   P5/P14), not to the cache. **Observed 2026-11-18** (section 8.2b): D24 standard part 3 leaves stock at turn 182 in
   `on` mode while the verify build (every cached reach equal to a fresh one) plays stock on the same trajectory; with
  A10's fix added the `on` game is unchanged, so it is this second consequence, not the alias (8.2b, A10 isolation).

## 5. DG7, DG3, DG4, MB5 (outline; each its own define block, counter and identity run; not in milestone A)

- **DG7, the derive step shared across observers.** After the reach, `UpdateDangerSingleUnit` derives, per observer,
  the attack tiles (ranged: `GetPlotsUnderRangedAttackFrom(unit, reach, false, false)`; melee: the native-domain reach
  plots) and the capture tiles (`isNativeDomain`, `isEnemyCity`). Only `AssignUnitDangerValue` and the capture push
  depend on the observer's `m_DangerPlots`; the tile lists do not, except `isEnemyCity(*pLoopUnit)` (the unit's own
  war state: team epoch). DG7 keeps the two tile lists in the entry beside the reach. The ranged list reads plots up
  to the unit's range beyond the reach plots (line of sight: terrain, features, heights), so a DG7 entry gets its own
  region: the reach plots plus `range` rings, stamped like the reach region; the unit's range and indirect-fire
  promotions are in the snapshot or voided by `setHasPromotion`. Verify: every hit derives again and compares the two
  lists. Counter `DANGER_DG7` hits/misses. Value: about 0.3 s of H `[I, review DG7]`. Measured share of what DG7 skips
  on a hit (2026-11-18, DG4's profiles below): the ranged call from the danger update 0.37-0.94 % of H, 0.08-0.11 % of
  M, plus the melee and capture loops (inside `UpdateDangerSingleUnit`'s self, 0.23 % of H in the new profile); at the
  reach's hit rate (H 83 %) about **0.4-0.9 % of H** `[I]`: above the floor on H.
  What the ranged derivation reads, from `CvUnit::canEverRangeStrikeAt(x, y, pSourcePlot, false)` at the pin `[C,
  2026-11-18]`: `GetRange()` (distance and the LOS radius), `visibilityRange()`, `getFacingDirection(true)` (unit state
  the reach does not read: add it to the DG7 snapshot), `GetSeeThrough()`, `IsRangeAttackIgnoreLOS()`, the domain,
  `CanEverEmbark()`, `getUnitInfo().IsRangeAttackOnlyInDomain()`; per target: `isVisible(team)` (team-plot stamp),
  `canSeePlot` from the base plot (plot type, terrain, feature and heights of the plots between: plot stamps over the
  reach plots plus `GetRange()` rings), `needsEmbarkation`, `getLandmass()` (fixed after load), and for VP a city's
  `HasAccessToLandmassOrOcean` (the city plot's stamp; buildings bump it). `isNativeDomain` of the base plot reads the
  plot's water state (plot stamp). The base plots come from the reach result with `iMovesLeft > 0`, so a DG7 entry is
  valid only while its reach entry is.

  **DG7 design (impl-4807, 2026-11-18 04:40 EST, written before the code)** `[C, read at fork 64fd4c7cc]`:
  - **Where.** Define `VP64_SPEED_DANGER_DG7`, only inside `VP64_SPEED_DANGER`. In `UpdateDangerSingleUnit`, after the
    reach line, one block: `if (VP64Danger::Tiles(pLoopUnit, reachablePlots, vAttack, vCapture)) { assign every attack
    tile, push every capture tile, return true; }` then `#line 75`; when `Tiles` returns false the original code runs
    unchanged. `Tiles` returns false off the game-core thread and when the reach had no valid entry (uncacheable,
    nested, not stored): those calls derive as stock. The melee loop of stock interleaves `AssignUnitDangerValue` and
    the capture push per plot; the block runs all assigns, then all pushes: exact, since the first writes only
    `m_apUnits` and the second only `m_apCaptureUnits` of a plot (`CvDangerPlots.cpp:546-560`), and the order within
    each plot's vector (one element per unit) is unchanged.
  - **The lists.** Ranged (`IsCanAttackRanged()`): attack = `GetPlotsUnderRangedAttackFrom(unit, reach, false, false)`
    (sorted, unique), capture = the reach plots with `isNativeDomain`, in reach order. Melee: attack = the reach plots
    with `isNativeDomain`; capture = those of them that are not `isEnemyCity(*unit)`. Both kept in the reach entry
    (`Entry::dg7`), so a reach miss, which replaces the entry, drops them: valid only while the reach entry is.
  - **Which reach entry.** `Reach` records, on the core thread, the key of the entry it just hit or stored
    (`State::uDg7Key`, `bDg7Valid`; cleared at every `Reach` lookup); `Tiles` uses the entry only if that flag names this
    unit. So `Tiles` never trusts an entry the last `Reach` did not validate.
  - **DG7 snapshot** (compared by value at every lookup; unit state the reach's `Snap` does not hold): `IsCanAttackRanged`,
    `GetRange`, `visibilityRange`, `getFacingDirection(true)` (NO_DIRECTION today), `GetSeeThrough`,
    `IsRangeAttackIgnoreLOS`, `isNoAttackInOcean`, `IsEmbarkDeepWater`, `canMoveAllTerrain`, `isConvertUnit`,
    `getAlwaysHostileCount() > 0` (`getCombatOwner` in `isEnemyCity`). The unit type, domain, cargo and embark state are
    in the reach `Snap` already.
  - **DG7 region** (ranged only; melee reads only reach plots, which the reach region holds): every plot within
    `GetRange() + 1` hex steps of any reach plot (a breadth-first dilation through `plotDirection`, so wrap is the
    map's): the targets, the line-of-sight plots between base and target (`CvTargeting::CanSeeDisplacementPlot` reads
    only `seeThroughLevel` of plots on and beside the line), the target city's neighbours
    (`getAllAdjacentLandmasses`). Stamped like the reach region: plot stamp and the unit team's team-plot stamp above
    `uDCreated` (the event counter read **before** the derivation) is a miss.
  - **New writers (gaps of DG7's inputs, hooked only with the DG7 define):** (a) `CvPlot::changeReconCount` (a recon
    count raises `seeFromLevel` to 10; written by `CvUnit::setReconPlot`) bumps the plot; (b) landmass ids
    (`getLandmass`, read for `IsRangeAttackOnlyInDomain` units and by `HasAccessToLandmassOrOcean`) change only in
    `CvMap::recalculateLandmasses` (from `recalculateAreas`: a plot type change at run time, and map init/load), which
    bumps a new `State::landEpoch` (interlocked) kept in the DG7 data; the same call rewrites every plot's water flags.
    The team's `isExtraWaterSeeFrom` (`seeFromLevel`) changes inside `processTech` under `setHasTech`'s team bumps; war
    (`isEnemyCity`) bumps both teams; a promotion bumps the unit's plot (the reach entry misses).
  - **Counters** (in `VP64DangerStats.csv`, DG7 builds only, columns after the reach's): `dg7_lookups`, `dg7_hits`,
    misses `dg7_miss_{none,snap,land,region,region_team}`, `dg7_noentry` (no valid reach entry: stock derivation),
    `dg7_stores`, `dg7_verified`, `dg7_spoiled`, `dg7_region_plots`.
  - **Verify** (`VP64_SPEED_DANGER_DG7_VERIFY`, built together with `VP64_SPEED_DANGER_VERIFY`): every DG7 hit derives
    both lists again from the reach the call plays, compares each element in order, logs one
    `VP64_SPEED_CHECK_K("DANGER_DG7", "attack" | "capture", ...)` per list (a difference: the two lists' hashes, owner,
    unit id, the first differing index, the sizes) and plays the fresh lists. Package name `DANGER_DG7`, so
    `verifyrun.py --package DANGER_DG7` reads its own row (the reach keeps `DANGER`).
  - **Negative control** `VP64_SPEED_DANGER_DG7_SPOIL=<n>` (with verify: must be caught) and `_DG7_SPOIL_EVERY=<n>`
    (no verify, identity negative): at the n-th (every n-th) DG7 hit the kept lists in the entry are spoilt before use
    (the last attack tile dropped; if none, the last capture tile; if none, the unit's plot added as an attack tile).
  - **Two-thread answers** (section 2.1 additions): `Entry::dg7`, `State::uDg7Key`, `bDg7Valid`: core thread only (as the
    cache). `State::landEpoch`: any thread, `InterlockedIncrement`, read on the core thread; a bump only voids. No new
    state is live across a hook raise: the derivation raises none (`canEverRangeStrikeAt` and `isNativeDomain` read
    only).
- **DG4, ranged-attack tiles as a stamped array.** Corrected 2026-11-18 from the code at the pin `[C]`: the
  ReachablePlots overload of `TacticalAIHelpers::GetPlotsUnderRangedAttackFrom` (`CvTacticalAI.cpp`) **no longer uses
  a set**: it already collects into a `vector<int>` with a per-call `vector<bool>` bitmap of `numPlots` bits ("like the
  std::set this replaced") and sorts the output. What is left for DG4 is the bitmap's allocation and clearing per call
  (about 1.3 KB on H), which a `thread_local` stamped `vector<u32>` would avoid, and its LOS checks per base plot. It is
  likely below the value floor on its own: measure its share before coding it (the review's ~0.1 s of H assumed the
  set). Two-thread answer if built: `thread_local`. (Replaced: "collects into a set container; a stamped array
  replaces it".)
  **Measured 2026-11-18 (impl-4807): below the floor, NOT BUILT** `[L, workstation, shared: indications (D106)]`.
  Method of `speedup-p0-shares.md` (sampled CPU cycles, folded stacks), `perf/evidence/0504/4807/dg4share.py` (the
  outermost `GetPlotsUnderRangedAttackFrom` frame, its caller and the frame below it). Inputs: 4668's profiles of the
  pin `04b1211b8` (M 151-160 m1/m2/m3, H 301-305 h1; `CvTacticalAI.cpp` and `CvDangerPlots.cpp` are unchanged between
  `04b1211b8` and the batch-3 pin) and a new H profile of the danger package's release on build `EF04545E` (fork
  `2f75a751c`), H 300 -> 304, 10 ms tick, 26,628 walks, 2026-11-18 04:31-04:34 EST
  (`perf/evidence/0504/4807/profile_h.sh`, `ARMS=cand`; `F:/c5-64bit-work/4807/dg7/prof-h/cand/prof/`). Share of the
  turn, the danger caller (`CvDangerPlots.cpp:84`) only:

  | Profile | whole call | `operator new`/free below it | self (inlined bitmap clear, loop) | LOS `canEverRangeStrikeAt` | result growth (`_Emplace_reallocate`, folded) | sort |
  |---|---|---|---|---|---|---|
  | H h1 (pin) | 0.936 % | 0.106 % | 0.118 % | 0.419 % | 0.114 % | 0.128 % |
  | H danger on (new) | 0.370 % | 0 | 0.093 % | 0.161 % | 0.047 % | 0.011 % |
  | M m1 / m2 / m3 (pin) | 0.108 / 0.090 / 0.079 % | 0 | 0 / 0 / 0.024 % | 0.054 / 0.090 / 0.055 % | m1 0.055 % | 0 |

  DG4 removes only the bitmap's allocation and clearing: at most the `operator new` share plus all of self, **<= 0.22 %
  of H and <= 0.024 % of M**, below 0.3 % in every profile (and DG7, below, removes the whole call on its hits). The LOS
  checks are DG7's, not DG4's (a stamped array does not skip them). Not built; DG4 leaves the package.
- **DG3, a per-plot turn-damage table.** `UpdateDangerInternal` sums feature and terrain turn damage per revealed plot
  per observer through two info lookups; a table `[feature + 1][terrain + 1]` built at the first use after a database
  load (`thread_local` or built under the lock and never changed after) answers it. Small (`<= 12.5 ms` in the review);
  it enters by the value floor's third clause (a function another accepted item rewrites).
- **MB5, small buffers in the attacker vectors.** `CvDangerPlotContents::reset()` clears its vectors every update; keep
  their capacity (or reserve a few slots once) so the per-observer refill does not reallocate. Exact by construction;
  memory: the capacity stays for the turn (measure D151's metric).

## 6. Counters, verify mode and the negative control

- **Counters** (`VP64SpeedDanger.inl`, per game turn, written to `%VP64_SPEED_DIR%\VP64DangerStats.csv` in builds with
  `VP64_SPEED_DANGER_STATS` or `_VERIFY`): lookups, hits, misses by reason (cold, key, unit snapshot, team epoch, player
  epoch, region, region-team), uncacheable by reason (thread, partial, explorer/evangelist, ZOC, Lua hook option),
  stores, not stored (jump, nested, generation), entries and bytes peak, stamp bytes; in stats builds the events by kind.
  `VP64SpeedVerify.h`'s totals (`VP64SpeedTotals.csv`) carry package `DANGER`: checks = verified hits.
- **Verify** (`VP64_SPEED_DANGER_VERIFY`): every hit runs the search again, compares the cached and the fresh result
  (the plot set in order, and per plot turns, moves left and normalised distance) and **plays the fresh result** (the
  kit's rule: a verify game is the pin's game). One `VP64_SPEED_CHECK_K("DANGER", "reach", ...)` per hit: equal results
  give fast = orig; a difference logs the two results' hashes with the keys owner, unit id, the first differing plot and
  the two sizes.
- **Negative control** (`VP64_SPEED_DANGER_SPOIL=<n>`): the kept result is spoilt in the cache **at the n-th hit**,
  before it is used (since fork `2f75a751c`: the last reachable plot dropped; a result of one plot gets its moves left
  plus one), so the spoilt value is always compared; a verify run must count spoils >= 1 and differences >= 1.
  `VP64_SPEED_DANGER_SPOIL_EVERY=<n>` (no verify) spoils every n-th hit for the identity job's negative test.
  Corrected 2026-11-17 23:49 (was: the n-th stored entry; the 1000th store on M was never hit, section 8.2).

## 7. Files on the fork branch

New: `VP64SpeedDanger.h` (declarations), `VP64SpeedDanger.inl` (implementation, included at the end of
`CvDangerPlots.cpp`), `VP64SpeedVerify.h` (from `perf/speed-kitdemo`, with the three `DANGER` lines in `WriteInfo`).
Hooks in `CvDangerPlots.cpp`, `CvGameCoreDLLPCH.h`, `CvAStar.h` (closed-list accessor), `CvGame.cpp`, `CvMap.cpp`,
`CvPlot.cpp`, `CvUnit.cpp`, `CvTeam.cpp`, `CvPlayer.cpp`, `CvPolicyClasses.cpp`, `CvBuildingClasses.cpp`, `CvCity.cpp`,
`CvMinorCivAI.cpp`, `CvTraitClasses.cpp`. Every block `#if defined(VP64_SPEED_DANGER)` ... `#endif` is followed by
`#line N`; a `#else` keeping an original line is followed by `#line N` too. `perf/evidence/0504/4807/linecheck.py`
strips the blocks, compares the rest with the pin's file line for line and checks every `N` (fast; `defineoff.py` is the
byte proof).

## 8. Results (milestone A, workstation, shared machine: no timing claims)

Fork clone `C:/c5-64bit-work/4807/fork`, branch `perf/speed-danger`. Builds and runs through the speed kit
(`perf/speedkit/`, its README) and `perf/evidence/0504/4807/run_a.sh` (fast builds verify, spoil, on; `verifyrun.py`
clean on M and H, caught on M; `idjob.py` M and H). Outputs: builds `C:/c5-64bit-work/4807/b-<tag>-*`, runs
`F:/c5-64bit-work/4807/<tag>/`; copies of the reports in `perf/evidence/0504/4807/`.

### 8.1 Define off

`python -I perf/speedkit/defineoff.py --fork C:/c5-64bit-work/4807/fork --branch perf/speed-danger --work
C:/c5-64bit-work/4807/defineoff --jobs 6` `[L, 2026-11-17 23:15-23:26 EST]`, fork `cf8fb366b`, epoch 1794921170
(tools `f8e9f424b`), release, stubs built from the pin's plain copy: DLL
`A10DB6EEB1B137D11AD3D224E68C5294CE116FBD1E4DB9D3E6FDCE18F5B1C91A` (23,669,760 B) against the pin's
`2D737BD5...97438B` (23,669,760 B): only the 20 build-identity bytes differ (PE time stamp, the CODEVIEW and REPRO time
stamps, the PDB GUID): **`defineoff: IDENTICAL-EXCEPT-BUILD-ID`**, exit 0. So all 56 blocks and their `#line`
directives leave the define-off DLL the pin's in code, data, imports, exports and relocations. Report:
`perf/evidence/0504/4807/defineoff/`. Again at the branch head `2f75a751c` `[L, 2026-11-18 00:36-00:44 EST]`
(`--deps` the first take's stubs): the same DLL, `A10DB6EE...C91A`, `IDENTICAL-EXCEPT-BUILD-ID`
(`perf/evidence/0504/4807/defineoff-a4/`): the later commits touched only the package's own files, which a define-off
build never opens. And at `64fd4c7cc` (the delayed-visibility fix moved one block in `CvPlot.cpp` and added one with a
`#else`) `[L, 2026-11-18 03:41-03:49 EST]`: DLL `437E18F84E71750F02F1DC9301B1F8A9B0645ACD0A22CDD5A198382541E37FB2`,
`IDENTICAL-EXCEPT-BUILD-ID` (`perf/evidence/0504/4807/defineoff-a9/`).

### 8.2 Verify, negative control, identity `[L, 2026-11-17 23:26-23:47 EST, fast builds of fork cf8fb366b]`

Builds (`kitbuild.py`, fast, about 3 min each): verify (`DANGER`, `DANGER_VERIFY`) `599EC960...0532`; spoil
(`+ DANGER_SPOIL=1000`, store-time spoil) `06FF10A7...AFAB`; on (`DANGER`) `D05BFC5E...171C`.

| Run | Window | Result |
|---|---|---|
| `verifyrun.py --expect clean` (verify build) | M 150-161 | **PASS**: 8,205 checks in the kit's totals (151-160), 0 differences, 0 spoils, game IDENTICAL; no `VP64SpeedDiffs.csv` written at all, so 161's 117 hits had none either |
| | H 300-306 | **PASS**: 49,406 checks in the kit's totals (301-304), 0 differences, game IDENTICAL; no differences file: all 60,086 hits of 301-305 compared equal |
| `verifyrun.py --expect caught` (spoil 1000, store-time) | M | **FAIL**: spoils 1, differences 0: the 1000th stored entry was never hit (replaced or emptied at the turn start first). Fixed in `9a1b9e34a`: the controls spoil the kept result at the n-th hit, so the spoilt value is always used (section 6) |
| `idjob.py` (on build) | M 150..160 | **IDENTICAL** (15 tables, 32,838 rows) |
| | H 300..305 | **IDENTICAL** (14 tables, 125,210 rows) |

Coverage and counters (verify build's `VP64DangerStats.csv`, the same in `on` mode: a hit does not depend on the
mode):

| Window | lookups | hits | hit rate | misses: cold / key / unit / team / player / region / region-team | uncacheable | entries peak | bytes peak | stamp arrays |
|---|---|---|---|---|---|---|---|---|
| M 151-160 | 10,704 | 8,205 | 76.7 % | 1,619 / 78 / 69 / 175 / 0 / 450 / 44 | 64 (explorers, evangelists) | 182 | 0.21 MB | 1.11 MB |
| H 301-305 | 72,692 | 60,086 | 82.7 % | 2,899 / 377 / 475 / 3,512 / 0 / 5,040 / 268 | 35 (explorers, evangelists) | 611 | 2.06 MB | 3.25 MB |

Stores: M 2,435, H 12,571; region plots summed over the stores: M 146,582 (60 per entry), H 4,989,218 (397 per entry).
Events (stats build): H 262,919 team-plot, 15,946 unit, 273 plot (most plot changes come as unit moves), 846 team,
822 player, 79 all-plots events over five turns.

On H the lookups and hits are **exactly the prototype's** (72,692 and 60,086, HO 1, at the pin `04b1211b8`), on the new
pin and with the closed gaps: the batch-3 changes and the extra end-of-setter bumps cost no hit there. No lookup ran off
the game-core thread, before the first turn start in a counted turn, nested, or with a Lua hook option; no store was
refused for a jump, nesting or the generation check.

**More windows, verify** `[L, 2026-11-18 00:18-00:31 EST]`: verify build of fork `2f75a751c` (fast)
`339E030B57FE654572902B70FCE55D2FB0213ABD24479E0CA139B825E3DE8AF7`; `verifyrun.py --expect clean --windows E` plus
4803's coverage windows (README item 5): **PASS** on all five, 0 differences (no differences file in any window), games
IDENTICAL:

| Window | hits verified (all turns, from the stats file) | lookups | hit rate | entries / bytes peak |
|---|---|---|---|---|
| E, a new fixed-seed Standard game 0-51 | 11,324 | 14,699 | 77.0 % | 78 / 0.06 MB |
| CONGM, M 180-184 (World Congress 182-183) | 3,111 | 4,323 | 72.0 % | 169 / 0.24 MB |
| CONGH, H 305-309 (Congress 306-307) | 41,802 | 50,218 | 83.2 % | 671 / 2.24 MB |
| SPECH, H 310-314 (special session 311-312) | 23,947 | 30,098 | 79.6 % | 641 / 2.14 MB |
| CORPM, M 240-246 (the first corporation, 242) | 3,710 | 5,701 | 65.1 % | 186 / 0.37 MB |

**Release confirmation** `[L, 2026-11-18 00:04-00:09 EST]`: the define-on **release** build of fork `2f75a751c`
(`kitbuild.py --config release`, LTO, 489 s) `EF04545EE99008C9A42F069AD8C45A5138C62301124713033FBE5883296462C4`
(23,688,704 B): `idjob.py` M 150..160 **IDENTICAL** (15 tables, 32,838 rows), H 300..305 **IDENTICAL** (14 tables,
125,210 rows). (Run wall times on the shared workstation, one run each, no claim: H 133.0 s pin, 119.6 s danger.)
The same release build on E and the coverage windows `[L, 2026-11-18 00:29-00:36 EST]`: E 0..50 IDENTICAL (15 tables,
20,626 rows), CONGM 180..183 (16,607 rows), CONGH 305..308 (82,186), SPECH 310..313 (82,704), CORPM 240..245 (42,826):
all **IDENTICAL**.

**The negative controls after the fix** `[L, 2026-11-17 23:47-23:57 EST]`:

| Build | Run | Result |
|---|---|---|
| spoil at the 1000th hit (first plot's moves left + 1), verify on: fork `9a1b9e34a`, `3E4945F0...FD93` | `verifyrun.py --expect caught`, M | **PASS**: spoils 1, differences 7 (the spoilt entry was hit 7 times in turn 152, each caught), game IDENTICAL |
| every 1000th hit spoilt (the last reachable plot dropped), no verify: fork `2f75a751c`, `A62F1B02...F734` | `idjob.py` M | IDENTICAL: **not caught**: about 8 results each missing one plot changed no decision of M 150-160 |
| every 20th hit spoilt, the same: `E2E4F6CB...BB95` (`SPOIL_EVERY=20`) | `idjob.py` M | **DIFFERENT** (caught, exit 1): `MapUnitsState` first differs at turn 156 (a unit's `plotX` 14 against 13), 5 tables differ. The identity job's negative test for this package uses every 20th hit |

So one, or a handful of, wrong reach results do not reach a decision on M: the danger maps are coarse (a plot's danger
from one more or one fewer unit seldom flips a choice). The verify mode, not the game identity, is what proves each kept
result; the identity runs prove the history effect of section 4.

**Narrowing gate on the define-on code** `[L, 2026-11-18 00:13-00:17 EST]`: `perf/evidence/0504/4807/narrow.sh
2f75a751c ...` (a plain copy through `build_as_pin.py ... --config fast --compile-only --shorten`, with `CL` =
`DANGER`, `DANGER_VERIFY`, `DANGER_STATS`, `DANGER_SPOIL_EVERY=50`, so every line of the package compiles): 794 sites,
baseline 798, **0 new**, 4 gone (the pin's own result). Evidence `perf/evidence/0504/4807/narrow/`.

### 8.2a The long window: the first wrong hit (found, root cause fixed in `64fd4c7cc`)

`[L, 2026-11-18 00:44-00:58 EST]` `verifyrun.py --expect clean --window LONGM=TS0438-M-T150.Civ5Save:150:300` with
the verify build `339E030B` (fork `2f75a751c`): **FAIL**: 128,450 hits verified over 151-300 (193,565 lookups, 66.4 %),
**7 differences, all one entry**: turn 275, owner 0, unit id 10149, the cached reach 44 plots, a fresh search 43; the
first differing cached plot is index 3201 (x 1, y 40 on the 80-wide Standard map); the 7 hits of that entry in turn 275
all differ the same way. The game stayed IDENTICAL (a verify build plays the fresh result). M 150-160 in the same run:
PASS. Evidence `perf/evidence/0504/4807/a4/vr-long/`.

So the cache is not exact on M 150-300 as built: one entry stayed valid by every stamp and epoch while the search's
answer changed. Two explanations, to be told apart by the diagnosis below:
1. an input the search reads that no hook stamps (a missed writer: a gap of HO 4 not yet listed);
2. the search depends on the path finder's leftover node data (P0.2's not-refreshed reads, section 4): stock's own
   answer to the same question then changes with the searches made in between, which no invalidation can follow.

**The same long window in `on` mode** `[L, 2026-11-18 01:14-01:27 EST]` (`idjob.py`, the release on build
`EF04545E`): LONGM 150..300 **IDENTICAL** (15 tables, 1,023,604 rows); M, H, E IDENTICAL again in the same job. So the
one wrong entry of turn 275 (seven uses of a reach with one plot too many) changed no decision on this trajectory, and
150 turns of hits with their shifted finder history (section 4) gave the stock game.

**D24 with the release on build** `[L, 2026-11-18 01:05-01:14 EST]` (run by `idjob.py`'s default windows, the
three-part save test against `deps/i0.lock`'s 32-bit build `13CFB6D1`; report `perf/evidence/0504/4807/a4/id-long-d24/`):
tiny parts 1-3 PASS (bug 0508's `IsReligionOwner` did not show here); standard part 1 PASS, **part 2 PASS** (x86 direct
0->200 = x64 direct 0->200 with the cache on, 351,496 rows), **part 3 FAIL**: "x86 continues x64-T50" against "x64
continues x64-T50" DIFFERENT in 13 of 15 tables from **turn 182** (`MapUnitsState` first: a unit's `plotX` 61 against
60), while "x64 continues x86-T50" = "x86 continues x86-T50". The pin's own build passes all three parts (`deps/dll.lock`
`verified`). Huge did not run (its input save is missing from the temp folder the test reads:
`.../Temp/claude/e2000/inputs/E2000-HUGE_0350.Civ5Save`, an environment gap, not the build). So **with the cache on,
one continuation of 132 turns after a load leaves the stock game at turn 182**, the same kind of result as the long
window's wrong hit; one 200-turn game without a load stayed identical. The part's controls (`part3-*.txt`) show
that in this run the two T50 saves (x86-written and x64-written, identical but for the DLL version string, part 1) also
lead the **same** DLL to different continuations ("x86 continues x86-T50" against "x86 continues x64-T50": DIFFERENT;
x64 on continuing x64-T50 against x86 continuing x86-T50: DIFFERENT from turn 65), so only the gate pair (two DLLs,
one save) speaks to the cache: x86 and x64-on continuing the same `D10-STANDARD-x64-T50` part ways at 182. Next
diagnostic queued: the verify build on that save, 50..200 (`STD50`), which names any wrong hit on that trajectory.
Result `[L, 2026-11-18 01:58-02:05 EST]`: `verifyrun.py --window STD50=D10-STANDARD-x64-T50.Civ5Save:50:200` (verify
build `742be6831`): **PASS**, 77,459 checks, 0 differences, game IDENTICAL to the pin. But `idjob.py` plays with 0438's
databases and game folder, not the D24 harness's, so this is another trajectory from the same save: the D24 part-3
divergence is **not reproduced** outside the D24 harness and stays open (`perf/evidence/0504/4807/a5/vr-std50/`).

Diagnosis (fork `742be6831`): verify builds now write `VP64DangerMismatch.csv` (the differing plots, whether they lie
in the entry's region, their plot and team stamps against the entry's creation, and what the plot holds now), and
`VP64_SPEED_DANGER_DIAG` asks every stored question a second time at once (a different answer = explanation 2).
Played on `LONG276` (M 150 -> 277), `perf/evidence/0504/4807/diag.sh` `[L, 2026-11-18 01:27-01:58 EST]`:
- The wrong hit **reproduces exactly** (verify build `742be6831`): turn 275, owner 0, unit 10149 (unit type 52, AI type 8,
  at (46,40), not human, not embarked), kept 44 plots, fresh 43; only in the kept answer: plot 3201 = (41,40). The plot
  **is in the entry's region**, and neither its plot stamp (3,742,312) nor its team stamp (3,739,854) moved after the
  entry's creation (3,745,632; the counter at the hit 3,752,128); the unit's team and player epochs did not move either.
  The plot now: owner 5 (team 5), no city, no unit, revealed, **not visible** to the unit's team, a pillaged road, a
  pillaged improvement 4, plot type 1, terrain 1. Evidence `perf/evidence/0504/4807/a5/`.
- **DIAG**: every stored question of 151-276 asked a second time at once gave the same answer (no `store_again` line),
  and the wrong hit of turn 275 appeared again unchanged although DIAG's extra searches gave the finder another history.
  So the reach search did not show a dependence on its finder's leftover data here: explanation 1 (an input no hook
  stamps) is the likely one, and it lies outside plot 3201's own stamps and the unit's team and player epochs (a
  neighbour's state on the path, or the plot owner's team: team 5's epoch is not checked).
- Second diagnostic (fork `9635eaf53`, `diag2.sh`) `[L, 2026-11-18 02:05-02:20 EST]`: the kept answer had plot 3201 at
  **0 turns, 0 moves left** (entered with the unit's last movement this turn, normalised distance 160). In the fresh
  search the node of 3201 was refreshed (its cache carries the current generation) but **never entered the open or
  closed list** (no parent, cost 0); its own flags allow ending the turn there (terrain, territory, no city, no visible
  unit). Every other plot of the answer has the same turns and moves in both. So the move from 3201's neighbour into it
  became invalid while the neighbour itself and 3201 kept their stamps. **Epochs that moved since the search: teams 6
  and 7, players 6 and 7** (the unit's own team 0 and player 0 did not), i.e. something of players 6 and 7 changed (a
  player bump moves its team too: a city changing hands, a policy, a building, traits, or a minor's ally).
- Third diagnostic (fork `eb63f2acd`, `diag2.sh a7`) `[L, 2026-11-18 02:22-02:37 EST]`: the kept search entered 3201
  from **plot 3202 = (42,40)**, owned by **player 35 (a city-state)**, a road, one unit, **not visible** to the unit's
  team, reached with 0 turns and 20 moves left in both searches; its plot and team stamps did not move after the
  search either. In `PathValid` a move into a plot the team cannot see (3201) is allowed only from a plot where the
  unit may end its turn (`bCanEnterTerrainPermanent`, `bCanEnterTerritoryPermanent` of the parent), so the likely flip
  is the unit's right to end its turn in the city-state's territory (`CvUnit::canEnterTerritory(team 35, true)`), which
  reads the minor's state for player 0 (war, friendship, intrusion permission, met, the trait) and, for some branches,
  other players: an input whose writer bumped players and teams 6 and 7 but not the minor's plots.
- Fourth diagnostic (fork `2e1e1adaa`, `diag2.sh a8`) `[L, 2026-11-18 02:43-02:57 EST]`: the only flag that differs
  for 3201 and for 3202 is **"visible to the unit's team"**: both plots were visible at the search and are not at the
  hit (3201: 63 against 61; 3202: 1079 against 1077), while their team stamps never moved.

**Root cause (fixed in fork `64fd4c7cc`)** `[C]`: with `MOD_CORE_DELAYED_VISIBILITY` = 1 (CP/VP's
`NewCustomModOptions.xml`), `CvPlot::isVisible()` reads `getVisibilityCount()`, which returns the turn's maximum
`m_aiVisibilityCountThisTurnMax[team]`, not the live `m_aiVisibilityCount[team]` (`CvPlot.h` `getVisibilityCount`).
That maximum is raised in `changeVisibilityCount` and **lowered only in `CvPlot::flipVisibility(team)`**, called for
every plot when a team's visibility expires (`CvPlayer.cpp` at the end of a player's turn, "Visibility expires now!",
and after delayed deaths of its units; `CvGame.cpp` for the observer). The hand-over's hook (HO 4.4: "isVisible reads
only the count") followed the live count only, so a plot that went dark at a flip kept old entries valid. Here: the
unit's path ran through a city-state plot (3202) where it may not end its turn into 3201; `PathValid` allows a move into
a plot the team cannot see only from a plot where the unit could stop, so the move was valid while 3201 was visible and
invalid after the flip. **Correction to HO 4.4**: the visible input has two writers, `changeVisibilityCount` (the
maximum and the count) and `flipVisibility`; both now stamp the plot for the team when the visible state they set
changes. The players 6 and 7 whose epochs moved were coincidence (their own turns' changes), not the cause.
Re-check: `perf/evidence/0504/4807/fixcheck.sh a9` (LONG276 verify, M and H verify, D24 with the on build), builds of
fork `64fd4c7cc` `[L, 2026-11-18 03:01-03:3x EST]`:
- LONG276 verify (verify build, pin's run reused): **PASS, 99,938 checks in the totals, 0 differences** (no differences
  file: the turn-275 wrong hit is gone), game IDENTICAL. Cost of the extra stamps: hits over 151-276 100,361 before,
  100,343 after (18 fewer, 0.02 %; hit rate 65.9 % both); team-plot events 3,117,377.
- M and H verify (verify build `92BBC547...9F0E`): **PASS**, M 8,205 checks (as before), H 49,404 in the totals
  (301-304; 49,406 before the fix), 0 differences, games IDENTICAL.
- Identity of the fast on build `DF97AE30...4DBA` `[L, 2026-11-18 03:50-03:58 EST]`: M 150..160, H 300..305, E 0..50
  **IDENTICAL** (`perf/evidence/0504/4807/a9/id-mhe/`).
- D24 with the same on build `[L, 03:28-03:40]`: tiny 1-3 PASS, standard 1-2 PASS, **standard part 3 still FAIL at
  turn 182**, the same as before the fix (`a9/id-d24/report.txt`): a cause other than delayed visibility, or not the
  cache at all. Section 8.2b.

### 8.2b D24 standard part 3: the finder-history effect of section 4, observed

`perf/evidence/0504/4807/d24diag.sh F:/c5-64bit-work/4807/a10` `[L, 2026-11-18 03:59-04:08 EST]`: the D24 harness's
continuation replayed exactly (`run_server.py --load D10-STANDARD-x64-T50.Civ5Save --turns 200 --dll-log-flags 0`,
the harness's defaults, the a9 D24 layout), and each `stats.db` compared with the 32-bit continuation of the same save
from that D24 run:

| DLL | against the x86 continuation | against the on build's continuation |
|---|---|---|
| the pin `2D737BD5` (control) | **IDENTICAL** (15 tables, 348,762 rows) | DIFFERENT |
| verify build `92BBC547` (fork `64fd4c7cc`): 64,382 hits over 51-200 (60.9 %), every one searched again: **0 differences** | **IDENTICAL** (348,762 rows) | DIFFERENT |
| on build `DF97AE30` (the a9 D24 run) | DIFFERENT from turn 182 | |

So on this trajectory **every kept reach equals a fresh search, yet the `on` game leaves stock at turn 182**, while the
verify build, whose only other difference from `on` is that it still runs the searches it caches, plays stock. The
cause is therefore not the cache's answers but the **searches it skips**: they change the core path finder's history
(the generation id of every later search, and which nodes hold which earlier search's data), and some later search,
not a reach search, gives another answer for it: an A10 alias at a generation wrap (the 16-bit id wraps every 65,534
searches; a standard game makes thousands per turn, so 132 turns hold several wraps) or a not-refreshed node read
(P0.2's `PathValid`, `CanEndTurnAtNode`, `DestinationReached`, `CommonNeighborIsPassable` sites). This is section 4's
hazard, now observed `[L]`. **It is not the A10 alias** (A10 isolation below, 2026-11-18): the cause is the finder's
history through not-refreshed node reads (or another history dependence of the core finder), still to be traced.
(Traced 2026-11-18, 8.2c: `CommonNeighborIsPassable` in ring-2 searches; the first stats.db turn is 182, not 191.)

**A10 isolation** `[L, 2026-11-18 04:25-04:44 EST, lead-0504]`: `perf/evidence/0504/4807/a10iso.sh
F:/c5-64bit-work/4807/a10iso on a10` (log `perf/evidence/0504/4807/a10iso.log`), fork scratch branch
`scratch-4807-a10iso` (fast builds, not for a pin) = `perf/speed-danger` `64fd4c7cc` + A10's fix (fork `fix-a10-x64`
`2915322be`, cherry-picked as `2d7b8a9b7`) + `95c912515`, which widens the package's own generation check to A10's 32-bit
id. Same continuation as `d24diag.sh` (D10-STANDARD-x64-T50 to turn 200, compared with the x86 continuation):

| DLL | against the x86 continuation | against the batch-3 on build's continuation |
|---|---|---|
| `a10`: package off (a define nothing reads) = batch 3 + A10, `832ACD2B...` | **IDENTICAL** (348,762 rows) | DIFFERENT |
| `on`: package on + A10, `66453920...` | DIFFERENT (GsCulture from turn 191, MinorConquest at 200) | **IDENTICAL** (348,867 rows) |

So with no 16-bit wrap possible the on game is still the batch-3 on game, row for row: the divergence does not come from
an alias. A10 alone does not change this trajectory either (as 4806 found for the D10 games).

**Rebase trap found on the way** `[C]`: the package's miss path checks that the reach search used exactly one generation
(`NextGeneration(uGenBefore)` in `VP64SpeedDanger.inl`), written for the 16-bit id (`unsigned short`, wrap at 0xFFFF). On
a pin with A10 (batch 4) `GetCurrentGenerationID()` returns 32 bits: the truncated compare fails after the 65,535th
search of the finder and every later miss is then treated as unstorable, silently lowering the hit rate (exact, but
slower). The rebase onto batch 4 must take `95c912515`'s change (an `unsigned int` id, 0 set back to 1).

Consequences `[I]`:
- The package cannot be exact against stock on long runs while the core finder's answers depend on its history. The
  identity windows (M, H, E, the coverage windows, LONGM 150-300, D24 part 2's direct 0-200) happen not to meet it.
- A10's fix (4806, pin batch 4) removes the alias case, and the isolation above shows the alias is not this case: the
  not-refreshed reads (or another dependence of a later search on the finder's history) must be traced and fixed in
  the finder (P5/P14's owners) before this package can be default-on. (Replaced 2026-11-18: "Re-run `d24diag.sh` on
  batch 4 first: it tells the two apart cheaply"; batch 4 also carries A9, which changes D10 games, so the A10-only
  test on batch 3 was the cleaner separation.)
- The verify mode cannot see this class (it runs every search); only `on`-mode identity can, and only on long runs:
  the acceptance list must keep long `on`-mode games (D24 part 3, a fixed-seed 0-300+ in `on` mode), as section 4
  proposed.
- (Added 2026-11-18, impl-4807) Traced in 8.2c: the dependence is `CommonNeighborIsPassable` reading a node that the
  search has not refreshed; a refresh before that read is the proposed finder change.

### 8.2c The trace: which search answers differently, and why (milestone B slice "trace", impl-4807, 2026-11-18)

**Method** `[L, 2026-11-18 06:35-07:09 EST, workstation (shared; functional runs, no timing)]`. Fork scratch branch
`scratch-4807-trace` (pushed; never a product build) = `perf/speed-danger` `64fd4c7cc` + `0df057ced`, which adds
`VP64_TRACE_FINDER` (`VP64Trace.h/.inl`, hooks in `CvAStar.cpp`/`CvAStar.h` and two lines in `VP64SpeedDanger.inl`):
every search of `GC.GetPathFinder()` on the game-core thread writes one line to `%VP64_TRACE_DIR%/VP64Trace.csv` (turn,
number in the turn, inside `Reach` or not, player, unit, path type, flags, start, destination, a hash of the inputs, a
hash of the answer: success, best node and every closed node with turns, moves, known cost and parent), plus per read
site the reads of node cache data whose stamp is not the current search's (P0.2's sites: `PathValid` node and parent,
`PathCost` node and parent, `CanEndTurnAtNode`, `CommonNeighborIsPassable`, the native-domain read in
`DestinationReached`, `UnitPathGetExtraChildren`, `AddStopNodeIfRequired`'s next node) and a hash of the data those reads
saw (flags, move flags, cost terms, end-turn danger; not the stamp itself, which shifts with the generation ids). The
verify build's repeats of a cached reach are not logged or numbered, so both arms number the same searches.
`VP64_TRACE_DETAIL="turn:seq"` writes every such read and every closed node of the named search, with the search that
last wrote each node's data and the search whose `Reset` last cleared it. Logging only: both trace builds play their
arm's game row for row (verify trace build `24D73537...`: IDENTICAL to the x86 continuation, 348,762 rows; on trace
build `6959B11D...`: IDENTICAL to the a9 on build's continuation, 348,867 rows), and a second run of each gave a
byte-identical trace. Scripts: `perf/evidence/0504/4807/trace/trace.sh`, `align.py`, `sitesum.py`; records
`align-verify-on.txt`, `detail-181-1613-{verify,on}.csv` there; the traces (31.8 MB each) in
`F:/c5-64bit-work/4807/trace/`. Input: the D24 standard part 3 continuation of 8.2b (`D10-STANDARD-x64-T50` to turn 200,
`run_server.py --dll-log-flags 0`, a9's `bin-cand` with the DLL swapped).

**The true first stats.db turn is 182, not 191** `[L]`: `statscmp.py` over the a9 on continuation against the x86 one
names `MapUnitsState.plotX` first at turn 182 (183 `unitCurrHP`, 184 several tables); 8.2b's "GsCulture from 191" was the
first column of the last table printed by `tail`.

**Not-refreshed reads on this trajectory** `[L]`: over turns 50-200 (228,228 searches outside `Reach`, 41,708 reach
searches that both arms run) the **only** site with reads of not-refreshed data is `CommonNeighborIsPassable` (CNIP):
9,407 reads in 6,618 searches, every one with `MOVEFLAG_APPROX_TARGET_RING2` (flags 0x50010 5,303 reads, 0x830200 2,241,
0x10200 595, 0x230000 542). `PathValid`, `CanEndTurnAtNode`, `DestinationReached` and the other sites: 0 (P0.2's H counts
of those sites are another game; not traced here).

**(a) The searches that answer differently** `[L]` (`align.py --all`): the two arms make the same 217,175 searches with
equal inputs up to turn 181 search 1618; before that **10 searches answer differently**, each with equal inputs and
only CNIP reads that saw other data:
- the first: **turn 98, search 361**: player 3, unit 1851, `PT_UNIT_MOVEMENT`, flags 0x50010 (`APPROX_TARGET_RING2 |
  IGNORE_ZOC | IGNORE_STACKING_SELF`), (54,18) -> (57,18), success in both, 5 closed nodes in both, another end plot.
  It changed no later search's inputs. Detail `[L, 07:31-07:40, detail-98-361-{verify,on}.csv]`: CNIP reads (56,18);
  in stock it was **cleared** by the `Reset` of the second cached-reach repeat after search 354 (impassable), so the
  search ends at (55,17); in `on` that repeat did not run and (56,18) still holds data written as a neighbour by search
  98:329 (another unit, move flags 0xC00) after search 325's `Reset`: passable, the search ends at (55,18). The opposite
  direction to 181:1613: stock reads the cleared node here, `on` the leftover.
- the one that splits the game: **turn 181, search 1613**: player 5, unit 2008, flags 0x50010, (60,14) -> (66,14):
  verify (stock) ends at (64,14) after 13 closed nodes, 0 turns; on ends at (65,12) after 65 closed nodes, **3 turns**.
  Five searches later the sequences part (verify searches unit 2008's reach at search 1619, on does not): the stats
  difference of turn 182.
- Caller `[I]`: flags 0x50010 with a destination are used only by `CvUnit::TurnsToReachTarget` from
  `CvAIOperation.cpp` (recruitment, about line 478) and `CvArmyAI.cpp` (checkpoint progress about line 414, a new
  slot about line 613); at turn 181 three units of player 5 (3918, 4584, 2008) are asked for the same target (66,14) in a
  row, so the turns to the muster point differ (0 against 3) and the army or operation treats unit 2008 differently.
  Not confirmed by a stack.

**(b) The read that saw different data** `[L, detail logs]`: in search 181:1613, `PathCost(parent (63,14), node (64,14))`
asks `IsPathDest(64,14)`; with RING2 `DestinationReached` checks `CommonNeighborIsPassable(node (64,14), node (66,14))`,
whose only common neighbour is **(65,14)** (main layer). That node is two steps from the expanded node and this search
has not refreshed it yet (it is refreshed only when (64,14) is added, after `PathCost`):
- verify (stock): (65,14) holds data **written as a neighbour by a reach search of the danger update**, the 63rd
  cached-reach repeat after search 1610 (move flags 0x22040110, another unit), after the 53rd had cleared it: flags
  0x79 (revealed, terrain and territory enterable): passable, so (64,14) counts as the destination.
- on: those 72 reach searches were cache hits and did not run; (65,14) was **cleared** (stamp 0, all flags false) by
  the `Reset` of search 1610 (it was on reach search 1609's list) and nothing wrote it since: not passable, so (64,14)
  is not the destination in `PathCost`. CNIP then went on for another candidate and read (65,13), left by search 1612
  (unit 4584, the same flags and target).
Stamp class: verify an earlier generation's neighbour data (another unit's search); on a cleared node.

**(c) Why stock's answer depends on it** `[C, CvAStar.cpp at 64fd4c7cc]`: `PathCost` computes `bIsPathDest =
finder->IsPathDest(to)` before `NodeAddedToPath(to)` refreshes `to`'s neighbours, and for RING1/RING2 targets
`DestinationReached` reads `CanEndTurnAtNode(to)` (fresh: `to` is a neighbour of the expanded node) and, for RING2,
`CommonNeighborIsPassable(to, dest)`, whose candidates are `to`'s neighbours, refreshed only if this search has already
added one of their neighbours. When `iMovesLeft == 0` at `to`, `bIsPathDest == false` adds `PathEndTurnCost` (here the
end-turn danger at (64,14) is at its cap, `iEndTurnDanger` 2147483647 in the verify node), so (64,14) sinks in the open
list and the search finds a 3-turn ring-2 plot first; the main loop's own `IsPathDest(m_pBest)` would have accepted
(64,14) with fresh data `[I: in verify it did, after (64,14)'s addition refreshed (65,14)]`, but it never pops it. So stock's answer for a ring-2 target depends on which earlier search,
of which unit, last left data in the common neighbour, or whether a `Reset` cleared it; the cache, by skipping
searches, changes exactly that. `CvAStarNode::clear()` zeroes the cache data of the previous search's open and closed
nodes only; nodes filled as neighbours keep the data of whatever unit filled them.

**Proposed finder change** (a proposal: no product branch changed): in `CvTwoLayerPathFinder::CommonNeighborIsPassable`
call `UpdateNodeCacheData(check, pUnit, this)` (pUnit from the scratch buffer) on each candidate before reading its
flags. It is a no-op for a node already current, computes the flags for this unit from the live plot otherwise, and a
later refresh in the same search would compute the same values (the game state does not change inside a search), so
the only behaviour change is that CNIP answers from this unit's data instead of leftovers: every search's answer
becomes independent of earlier searches at this site. Clearing in `Reset` instead is not enough (neighbour-filled
nodes are on no list) and clearing every touched node would make every not-yet-refreshed candidate impassable, a larger
change to stock. The refresh changes stock's own answers in the searches that today read leftovers (6,618 of 228,228
here), so it changes game results like A9: a pin-batch change for P5/P14's owners, with upstream's view. Cost: one
`UpdateNodeCacheData` per not-refreshed read (about 60 per turn here).

**The change tested** `[L, 2026-11-18 07:09-07:31 EST]`: scratch define `VP64_TRACE_FIX_CNIP` (fork `28c5598fc`),
`perf/evidence/0504/4807/trace/trace_fix.sh F:/c5-64bit-work/4807/trace-fix`, same continuation, both arms with the
trace: `fixverify` (fix + package + verify, `4CB79472...`) and `fixon` (fix + package on, `119B9CDB...`): **stats.db
IDENTICAL** (15 tables, 342,270 rows), **all 261,917 searches equal** (`align.py`: no difference), and **0 reads of
not-refreshed data at every site** in both. So on this trajectory the refresh removes the package's whole effect on the
game: with it, `on` plays what the verify build (stock's search history) plays. As expected, both differ from the
unfixed x86 continuation (the fix changes stock's answer first at turn 50, search 20: player 0, unit 1002, flags 0x10200,
22 closed nodes against 19). Not shown: other games (P0.2's H has stale reads at `PathValid`, `CanEndTurnAtNode`,
`DestinationReached` that this game does not, so the same trace must run on H before the change is called complete);
the change's effect on AI quality (it makes ring-2 targets judged with the moving unit's own data, which looks like the
intent of the code, `[I]`); any timing cost.

### 8.2d The trace on H, pin batch 4: do the other not-refreshed sites split searches? (slice "rebase", impl-4807, 2026-11-18)

**Question** (ticket 4813's open point): P0.2 counted, on H 301-309, not-refreshed reads at `PathValid` (550 + 4,117 per
turn), `CanEndTurnAtNode` (469 + 1,656), `DestinationReached` (2 + 146) and others, which the D24 standard
continuation of 8.2c never had (there only `CommonNeighborIsPassable` read such data). Do those sites make a search's
answer depend on the finder's history, so that the package's skipped searches split a search on H, and does the CNIP
refresh alone remove every split?

**Method** `[L, workstation (shared): functional runs, no timing]`: fork scratch branch `scratch-4807-trace-b4`
(pushed; never a product build) = `perf/speed-danger-b4` `aeae7c8e5` + the trace commit (`0df057ced` as `3c34a91e4`) + the
CNIP refresh (`28c5598fc` as `9c0562065`), both cherry-picked clean onto batch 4 (the trace's stamp compares are like
types under A10's 32-bit id). `perf/evidence/0504/4807/b4/trace_h.py`: four fast builds with the package and DG7 and
`VP64_TRACE_FINDER` (`verify`: + both verify defines; `on`; `fixverify` and `fixon`: the same + `VP64_TRACE_FIX_CNIP`),
each playing 0438's H save `TS0438-H-T300` to turn 311 exactly as `idjob.py` plays its H window (same command, the
batch-4 pin's layout `x64-c29442e8-p58724e0` with the DLL swapped, the same databases), then `statscmp.py`, `align.py
--all` and `sitesum.py` over each pair. Runs `F:/c5-64bit-work/4807/b4/trace-h/`; reports
`perf/evidence/0504/4807/b4/trace-h/`. `align.py` stops where the two search sequences first part; the new
`perf/evidence/0504/4807/b4/siteaudit.py` aligns each turn on its own (a turn whose games still agree starts aligned
again) and counts, per set of sites with not-refreshed reads, the searches, those whose reads saw other data (the
trace's `stalehash`, one hash over all sites of a search) and those whose answer differs.

**Without the fix: verify (stock's finder history) against on** `[L, 11:30-11:45 EST; DLLs verify trace `4FF1853E...`,
on trace `C6250CCD...`]`:
- **Not-refreshed reads on H do occur at every P0.2 site** (verify arm, H 300-310, 74,326 searches outside `Reach` and
  35,940 reach searches; `sitesum.py`): `PathValid` node 62,135 (flags 0x400 22,958, 0x8010 17,260, 0x400200 10,590,
  0x8400000 3,361), `CanEndTurnAtNode` 23,856 (0x8010 17,260, 0x50010 3,274), `CommonNeighborIsPassable` 3,163 (0x50010
  2,073), `DestinationReached` 1,788 (0x428200 1,670), `PathCost` node 1,205, `UnitPathGetExtraChildren` 1,110,
  `PathValid` parent 166, `PathCost` parent 28, stop-node 0; all in 2,775 searches outside `Reach` (none in a reach
  search). The same order as P0.2's H 301-309 counts per turn.
- **stats.db verify vs on: IDENTICAL** (14 tables, 247,239 rows, H 300..311): the package changed no game result here.
- **Searches** (`siteaudit.py`): the sequences agree in 10 of 11 turns and in turn 304 up to search 1986 (then the
  order of the AI's searches differs, without a game difference); 102,843 searches aligned. Answers that differ: **10**,
  every one in a search that read not-refreshed data; none without such a read.

  | Sites with not-refreshed reads in the search | searches | reads saw other data | answer differs |
  |---|---|---|---|
  | `CommonNeighborIsPassable` only | 2,172 | 19 | **9** |
  | `PathValid` node only | 220 | 0 | 0 |
  | `CanEndTurnAtNode` + `PathValid` node | 128 | 0 | 0 |
  | CNIP + `CanEndTurnAtNode` + `PathValid` node | 27 | 0 | 0 |
  | `UnitPathGetExtraChildren` + `PathCost` node + `PathValid` node | 16 | 1 | **1** |
  | eight other mixed sets (`PathValid`/`PathCost` parent, `DestinationReached`, ...) | 52 | 0 | 0 |

  The nine CNIP splits are 8.2c's class (flags 0x50010 and 0x10200, ring-2 targets: 303:998, 303:9134, 304:1982,
  305:6354, 305:6740, 305:8106, ...). **The tenth is not CNIP**: turn 310 search 6522, player 11, unit 21171, flags
  0x200400 (`PRETEND_ALL_REVEALED | NO_ENEMY_TERRITORY`), (120,47) -> (128,24), success in both, 5,759 closed nodes in
  verify against 5,757 in on; its not-refreshed reads: `PathValid` node 25 / 20, `PathCost` node 19 / 20,
  `UnitPathGetExtraChildren` 19 / 20 (verify / on), no CNIP read.
- **The read that saw other data in 310:6522** `[L, 12:05-12:16 EST, detail runs (VP64_TRACE_DETAIL=310:6522) of both
  arms, which again played IDENTICAL games and the same trace; detailcmp.py, perf/evidence/0504/4807/b4/trace-h/
  detailcmp-310-6522.txt]`: every not-refreshed read of this search is of an **airlift target**: nodes linked as extra
  children of the airlift city (116,52) (parent 116/52, turn 1, the unit at 0 moves), read by `PathValid` (node), then
  `PathCost` (node) at the link and `UnitPathGetExtraChildren` when the target is expanded. 27 of 69 (site, plot) reads
  differ between the arms or happen in one arm only. What they saw: data written by **danger reach searches** (move
  flags 0x22040110) or zeros from a reach search's `Reset`; in verify the writer or clearer is one of the cached-reach
  repeats after search 6519 that `on` skips (e.g. (106,47): verify cleared by the 46th repeat, flags 0; on written by
  search 6519, flags 0x3027b; (86,52) the other way round). Same closed plots in both, other turns/moves/parents on some
  (5,759 against 5,757 closed nodes over both layers).
- `[C, CvAStar.cpp at 9c0562065]` Why those nodes are not refreshed: `CvAStar::CreateChildren` links the extra children
  that `GetExtraChildren(node)` returns now with `udFunc(udValid, node, check)` (`PathValid`) and `LinkChild`
  (`PathCost`) and no `UpdateNodeCacheData(check)` first. `NodeAddedToPath` refreshes a node's extra children too, but
  only those `GetExtraChildren` returned **when the node was added** (`ASNC_INITIALADD`/`ASNC_NEWADD`; an open or closed
  update does not refresh). `UnitPathGetExtraChildren`'s answer depends on the node's moves (airlift only with full or
  zero moves) and on the finder's airlift-checked flags, so the set can be empty at the add and not at the expansion
  `[I: which of these held for (116,52) was not logged]`; then the targets are judged on leftovers. Only a unit path
  finder has node cache data for these children (the city-connection finders' extra children read none).

**With the CNIP refresh: fixverify against fixon** `[L, 11:45-12:04 EST; DLLs fixverify `2FCADC97...`, fixon
`8840D448...`]`:
- stats.db fixverify vs fixon **IDENTICAL** (14 tables, 247,239 rows, H 300..311).
- **All 110,332 searches aligned** (the sequences never part), **0 CNIP reads of not-refreshed data**; every other
  site still reads such data (`PathValid` node 62,051, `CanEndTurnAtNode` 23,751, `DestinationReached` 1,788, `PathCost`
  node 1,185, `UnitPathGetExtraChildren` 1,087, `PathValid` parent 148, `PathCost` parent 28), and those reads saw the
  same data in both arms in every search **but one**: turn 310 search 6569, the same question as the unfixed 310:6522
  (player 11, unit 21171, flags 0x200400, (120,47) -> (128,24)), 6,554 closed nodes in fixverify against 6,553 in fixon,
  sites `PathValid` node 21 / 20, `PathCost` node 19 / 20, `UnitPathGetExtraChildren` 19 / 20: **answer differs**.
- So the CNIP refresh removes every CNIP split here (9 of the 10), and one split remains, at the extra-children sites.
  `PathValid`, `CanEndTurnAtNode` and `DestinationReached` reads outside that search saw the same data in both arms over
  these 11 turns (not proof of independence: a read of the same leftover in both arms is still a history dependence,
  only one the package did not change here).
- The fix changes stock's game on H too: verify vs fixverify (both stock's history) DIFFERENT from turn 303
  (`MapUnitsState` first; 10 of 14 tables), as on the D24 continuation (8.2c): a pin-batch change.

**The second refresh tested: CNIP + extra children** `[L, 12:18-12:4x EST]`: scratch `d313b0b18`
(`VP64_TRACE_FIX_EXTRA`, pushed): in `CvAStar::CreateChildren`'s extra-children loop, when the finder's extra-children
function is `UnitPathGetExtraChildren`, `UpdateNodeCacheData(check, unit, finder)` before `PathValid` (a no-op for a node
already current). Arms `fix2verify` (`DDA4EA3A...`) and `fix2on` (`49C681BF...`), both refreshes, the same H run:
- `fix2verify` (stock's history with both refreshes): **0 not-refreshed reads at every site** (73,758 searches outside
  `Reach`, 36,436 reach searches): the 62,135 `PathValid`, 23,856 `CanEndTurnAtNode`, 1,788 `DestinationReached`,
  1,205 `PathCost` and 1,110 extra-children reads of the unfixed arm were all reads of extra children linked without a
  refresh, or of their consequences; with CNIP's 3,163 those were every not-refreshed read on H.
- The extra-children refresh changes stock's game on H as well: fixverify vs fix2verify DIFFERENT from turn 302
  (`MapUnitsState`), verify vs fix2verify DIFFERENT from 302.
- **fix2verify vs fix2on: stats.db IDENTICAL** (14 tables, 247,036 rows) and **all 110,194 searches equal**
  (`align.py`: no difference; `siteaudit.py`: 0 not-refreshed reads in either arm). With both refreshes the package's
  skipped searches change nothing a later search sees on this window.

**Verdict for ticket 4813** `[L on H 300-310 and (CNIP) the D24 continuation; I elsewhere]`: the CNIP refresh alone is
not enough on H: one search in 11 turns still answers differently through the **extra children** (airlift targets,
port changes) that `CvAStar::CreateChildren` links without a refresh. `PathValid`, `CanEndTurnAtNode`,
`DestinationReached` and `PathCost` need no fix of their own here: every not-refreshed read P0.2 counted at them on H
is a read of such an extra child (or of what it led to), and they drop to 0 with the extra-children refresh. So
4813's fix covers **two sites**: `UpdateNodeCacheData` on each `CommonNeighborIsPassable` candidate (fork `28c5598fc`)
and on each unit-path extra child before `PathValid` in `CreateChildren` (fork `d313b0b18`; the product form needs no
helper if the refresh is written where `UnitPathCacheData` is visible, for example as a check in the unit path's own
`udValid`, `[I]`). Both change stock's results (H from turn 302), so both go into a pin batch with A9's kind of
statement. Not shown: other games and long runs with both refreshes (only H 300-310 and, for CNIP alone, the D24
continuation); the extra fix's cost (one `UpdateNodeCacheData` per linked extra child, a no-op when current); AI
quality.

### 8.3 Memory after 10 turns and a first timing signal (D178 dev pairs on oldpc)

`[L, 2026-11-18 00:08-00:29 EST]` `perf/speedkit/pair.py --a pin=<the pin's DLL 2D737BD5> --b danger=<release
define-on EF04545E> --runs 3 --scenario M --turns 15` (job `20261118-000454-654a`) and `--scenario H --turns 11` (job
`20261118-000553-3b2b`), quiet (alone, undisturbed, foreign cores 0.058 and 0.063), the pin's layout
`x64-4f4daac7-pf8e9f42` (server `ED829BDA`), AI logging off, interleaved. GPU: NVIDIA GeForce RTX 2080, driver
32.0.16.1088 (2026-07-21) (D175). Development signal only (D178, n = 3); default-on needs grade A.

| Scenario | pin s/turn | danger s/turn | difference (95 % CI) | memory after 10 timed turns, private MB (per run) | growth MB/turn | stats.db across arms |
|---|---|---|---|---|---|---|
| H 301-311 | 15.262 (sd 0.015) | 14.292 (sd 0.032) | **-0.970 s (-6.36 %)**, [-1.058, -0.883], significant | pin 368.6 / 371.3 / 368.7 (mean 369.5); danger 377.6 / 377.6 / 369.9 (mean 375.0): **+5.5 MB** | pin 2.76, danger 2.09 | IDENTICAL (14 tables, 269,984 rows) |
| M 151-165 | 1.005 (sd 0.026) | 0.970 (sd 0.001) | -0.035 s (-3.5 %), [-0.100, +0.030], not significant | pin 258.6 / 252.5 / 253.0 (254.7); danger 259.9 / 260.3 / 259.7 (260.0): +5.3 MB | pin 0.61, danger 0.67 | IDENTICAL (15 tables, 52,688 rows) |

The H gain matches the prototype's laptop pair (-1.22 s, -6.9 %, HO 1) on another machine. The memory cost is the
stamp arrays (H 3.25 MB, M 1.11 MB) and the entries (peak H 2.1-2.2 MB, M 0.2-0.4 MB) `[L, section 8.2]`; M's +5.3 MB is
more than those 1.5 MB, within the single sample's known noise of several MB (`speedup-test-kit.md` section 5). Evidence:
`perf/evidence/0504/4807/oldpc-654a/`, `oldpc-3b2b/` (analysis, series, compare, GPU, quiet summary).

**Kit finding** `[L]`: `VP64SpeedTotals.csv` misses the last played turn (the server process ends without the DLL's
static destructor writing the totals again: M shows 151-160 of 151-161, H 301-304 of 301-306). `verifyrun.py`'s
verdict therefore does not cover the last turn's checks; the differences file is written at once and is the complete
record (none here). Reported to lead-0504 for the kit's keeper.

### 8.4 DG7 (milestone B, impl-4807, 2026-11-18; workstation, shared: no timing claims)

Fork clone `C:/c5-64bit-work/4807/fork-dg7`, branch `perf/speed-danger`: `10f45eee6` (DG7: `VP64SpeedDanger.h/.inl`,
`VP64SpeedVerify.h`'s info lines, and three blocks with `#line`: `CvDangerPlots.cpp` (the tiles), `CvMap.cpp`
(`recalculateLandmasses` -> `LandmassEvent`), `CvPlot.cpp` (`changeReconCount` -> `PlotEvent`)), `fcb8394d5` (the
miss-reason enum moved out of the statistics `#if`, which the on build needs; no behaviour change). `linecheck.py
--define VP64_SPEED_DANGER,VP64_SPEED_DANGER_DG7`: OK on 14 files. Runs: `perf/evidence/0504/4807/run_dg7.sh` (TAG b1;
builds `C:/c5-64bit-work/4807/b-dg7-b1-*`, runs `F:/c5-64bit-work/4807/dg7/b1/`).

Fast builds (`kitbuild.py`): verify (`DANGER`, `DANGER_VERIFY`, `DANGER_DG7`, `DANGER_DG7_VERIFY`; `10f45eee6`)
`B56682B7E6262AA3F6FA8C938BB23D3E3575F93E9EC01E593E1829508F23F0BE`; on (`DANGER`, `DANGER_DG7`; `fcb8394d5`)
`33AD381E21A9CC0D4C7E724395AB2C7F46A8BFD8338951CA29E8E3851617AFF8`; spoil (verify + `DANGER_DG7_SPOIL=1000`;
`fcb8394d5`) `EBAED46D43C3F9256599D96D6F726AAAB27F41DD21C1CA737079865B74E2E563`; every (on +
`DANGER_DG7_SPOIL_EVERY=20`; `fcb8394d5`) `B6F359543C91CEADC6B756DC7820C9C1AF1C71498BA06A04A0F8D587545168B1`. Records:
`perf/evidence/0504/4807/builds/dg7-b1-*.json`. Not built: a release on build with DG7 (timing arms are the lead's).

**The long window, verify** `[L, 2026-11-18 05:50-06:12 EST]`: `verifyrun.py --package DANGER_DG7 --expect clean
--window LONGM=TS0438-M-T150.Civ5Save:150:300` (verify build `B56682B7`): **PASS**: DANGER_DG7 243,150 checks in the
totals (151-299), DANGER 123,790, 0 differences in both, no differences file (so 300 too), game IDENTICAL (15 tables,
1,023,604 rows). Over 151-300: 126,083 DG7 hits verified of 193,565 lookups (65.1 %; the reach's 128,427 hits),
misses: none 63,797, snapshot 0, landmass 0, region 2,256, region-team 88; no valid reach entry 1,341; cache peak
1.35 MB. Report `perf/evidence/0504/4807/dg7/vr-long.txt`.

**Not verified for DG7** (2026-11-18): on-mode long games (LONGM in `on` mode, D24), the coverage windows (CONGM, CONGH, SPECH, CORPM), E identity (blocked by 0508 on batch 3; on batch 4 E 0..50 is IDENTICAL, 8.5), a windowed or client run (the
two-thread rule is met by construction: `Tiles` returns false off the core thread), a game with a recon unit or a
run-time landmass recalculation (both hooks are unexercised: 0 snapshot and 0 landmass misses), timing and memory after
10 turns (oldpc pairs: the lead's plan). The batch-4 pin (A10) is not in this branch.

| Run | Window | Result |
|---|---|---|
| `verifyrun.py --package DANGER_DG7 --expect clean` (verify build: every reach hit and every DG7 hit checked) `[L, 05:05-05:13 EST]` | M 150-161 | **PASS**: DANGER_DG7 16,348 checks (8,174 hits x 2 lists, 151-160), 0 differences; DANGER 8,205 checks, 0 differences; no differences file (161 clean too); game IDENTICAL (32,838 rows) |
| | H 300-306 | **PASS**: DANGER_DG7 96,120 checks (301-304), 0 differences; DANGER 49,404, 0 differences; no differences file; game IDENTICAL (125,210 rows) |
| `verifyrun.py --package DANGER_DG7 --expect caught` (spoil at the 1000th DG7 hit) `[L, 05:13-05:14]` | M | **PASS**: spoils 1, differences 14 (the spoilt entry's 14 hits in turn 152: owner 63, unit 1496, attack list 3 kept against 4 fresh), game IDENTICAL |
| `idjob.py` (on build `33AD381E`: the reach cache and DG7 both playing their kept values) `[L, 05:14-05:21]` | M 150..160 | **IDENTICAL** (15 tables, 32,838 rows) |
| | H 300..305 | **IDENTICAL** (14 tables, 125,210 rows) |
| `idjob.py`, the identity negative: every 20th DG7 hit spoilt, no verify (`DANGER`, `DANGER_DG7`, `DANGER_DG7_SPOIL_EVERY=20`, `B6F35954...68B1`) `[L, 05:43-05:49]` | M 150..160 | **DIFFERENT** (caught, exit 1): `MapUnitsState` first differs at turn 153 (a unit's `plotX` 68 against 67), 11 tables differ |
| `defineoff.py --deps C:/c5-64bit-work/4807/defineoff/build` (release, no define, fork `fcb8394d5`) `[L, 05:21-05:36]` | | **IDENTICAL-EXCEPT-BUILD-ID**: DLL `A8299FDA80635DCB61BA25F76CE050F5D80EAC22BAC6E840CA88AF870A0D2FF0` (23,669,760 B) against the pin's `2D737BD5...438B`: only the PE and debug-directory time stamps and the PDB GUID differ. (The kit warned that `tools/x64-dll` changed since the lock's tools commit `f8e9f424b`; the result is identical anyway, so no `--control` run was needed.) Report `perf/evidence/0504/4807/dg7/defineoff.txt` |
| `verifyrun.py --windows E` (verify build) and `idjob.py --windows E` (on build) `[L, 05:41-05:43]` | E 0-51 (fixed-seed Standard) | DG7 **0 differences** in 22,134 checks (reach 0 too), but the game is **DIFFERENT for both builds in one statistics column only**: `ReligionBeliefYields.IsReligionOwner` from turn 26 (reference 1, candidate 0); the 14 other tables IDENTICAL. `[I]` This is bug 0508 (a pantheon's holy-city coordinates are never initialised, so the statistics logger reads stack garbage that moves with any code-layout change): the verify build runs stock's logic at every DG7 and reach hit and still differs, so no kept value is the cause. Milestone A's builds happened to pass E. E identity for this package waits for 0508's fix in the pin |
| Narrowing gate (`narrow.sh` with `NARROW_CL` = every DANGER and DG7 define: `DANGER`, `_VERIFY`, `_STATS`, `_SPOIL_EVERY=50`, `_DG7`, `_DG7_VERIFY`, `_DG7_SPOIL=1000`, `_DG7_SPOIL_EVERY=50`; fast, compile-only, `--shorten`) `[L, 05:36-05:40]` | | **0 new**: 794 sites, baseline 798, 4 gone (the pin's own, as in 8.2). The DG7 verify code was compiled in (the string `DANGER_DG7` is in that build's `CvDangerPlots.obj`, and not in the on build's) |

Coverage (verify build's `VP64DangerStats.csv`; copies in `perf/evidence/0504/4807/dg7/`):

| Window | DG7 lookups | hits | hit rate | misses: none (fresh reach entry) / snapshot / landmass / region / region-team | no valid reach entry (stock) | stores | DG7 region plots per store | bytes peak (cache with DG7) |
|---|---|---|---|---|---|---|---|---|
| M 151-160 | 10,704 | 8,174 | 76.4 % | 2,435 / 0 / 0 / 31 / 0 | 64 | 2,466 | 34 | 0.38 MB (0.21 without DG7) |
| H 301-305 | 72,692 | 58,415 | 80.4 % | 12,573 / 0 / 0 / 1,533 / 136 | 35 | 14,242 | 339 | 3.14 MB (2.06 without DG7) |

So DG7 hits on 97 % of the reach's hits (H: 58,415 of 60,084); the rest are its own region's changes (a plot within
range of the reach changed). No snapshot or landmass miss in either window. The kept tiles add about 1.1 MB at H's peak.

### 8.5 The package on pin batch 4 (milestone B slice "rebase", impl-4807, 2026-11-18)

**Branch** `[C]`: fork `perf/speed-danger-b4` (clone `C:/c5-64bit-work/4807/fork-b4`) = the x64 batch-4 pin `c29442e8c`
+ the ten package commits of `perf/speed-danger` `fcb8394d5` (`cf8fb366b` .. `fcb8394d5`, cherry-picked with `-x`, all
clean; `perf/speed-danger` itself is unchanged, batch-3 evidence points at it) + `1d86a3318` (the generation-check
widening of scratch `95c912515`: `NextGeneration` and `uGenBefore` as `unsigned int`, 0 set back to 1, as A10's
`FindPathWithCurrentConfiguration` does) + `aeae7c8e5` (the `#line` numbers of batch 4). Scripts:
`perf/evidence/0504/4807/b4/` (`setup_fork.sh`, `rebase.sh`, `commit_line.sh`, `run_b4.sh`, `trace_branch.sh`,
`trace_h.py`).

**The batch-3 -> batch-4 pin diff read against the package** `[C, git diff 4f4daac70 c29442e8c, 17 files]`:

| Pin change | Functions | Does the package read, hook or depend on it? |
|---|---|---|
| A10 (`c29442e8c`) | `CvAStar::m_iCurrentGenerationID`, `GetCurrentGenerationID()` (now `unsigned int`), the wrap in `FindPathWithCurrentConfiguration` (0 -> 1), `CvPathNodeCacheData::iGenerationID` (`unsigned int`) | **Yes**: the miss path's one-generation check (section 2) read the id as 16 bits: fixed by `1d86a3318` (the rebase trap of 8.2b). The verify detail log's and the trace's compares (`iGenerationID == GetCurrentGenerationID()`, `== 0`) compare like types and need nothing. No package code copies a node's cache data or stores an id |
| upstream #13522 (`71381245e`): `CvDatabaseUtility::Share2DArrayIfZero` | `CacheResults` of `CvBuildingEntry`, `CvPolicyEntry`, `CvTraitEntry`, `CvBeliefEntry`, `CvTechEntry`, `CvUnitEntry`, `CvCorporationEntry`, `CvImprovementEntry`, `CvInfos` entries | No reader or writer change: database tables, read-only after the load; no package hook writes them. **But** its inserted lines (15, 16, 21) sit above four package blocks: their `#line N` moved (`CvBuildingClasses.cpp` 5970 -> 5986 and 6255 -> 6271, `CvPolicyClasses.cpp` 4269 -> 4284, `CvTraitClasses.cpp` 4385 -> 4406), fixed in `aeae7c8e5`; a cherry-pick keeps the old numbers silently, only `linecheck.py` against the new pin (or `defineoff.py`) sees it |
| D176 (`2fbf08f9a`) | `CvUnitCombat::AttackAir`: a copy of the combat info for the engine's animation | No: the game's combat result and every game-state write are unchanged; no reach or DG7 input |
| 0508 (`ceadc7549`) | `CvReligion::CvReligion()`: the holy-city coordinates initialised to -1 | No reach or DG7 input; it removes the `IsReligionOwner` statistics noise that blocked E identity (8.4) |
| A9 (`15d5f1e7d`) | `CvWonderProductionAI::ChooseWonder` | No: AI wonder choice; it changes game results, so batch-3 results are not compared with batch-4 ones (COMMON 18) |

So A10 is the only pin change the package's logic meets; the hand-over's input list (HO 4) and the gaps of section 3 are
unchanged by batch 4.

**Results on batch 4** `[L, 2026-11-18, workstation (shared): functional runs, no timing]`
(`perf/evidence/0504/4807/b4/run_b4.sh`, log `C:/c5-64bit-work/4807/run_b4.log`; builds `C:/c5-64bit-work/4807/b-b4-*`,
runs `F:/c5-64bit-work/4807/b4/`):
- **Define off** `[10:51-11:01 EST]`: `defineoff.py --work C:/c5-64bit-work/4807/defineoff-b4` (new batch-4 stubs from
  the pin's plain copy; every later build of this slice uses `defineoff-b4/build/stubs`), release, epoch 1794969988
  (tools `58724e072`): DLL `B2D1AFD82D6AB0D6F00AFA55B9E328E0AD71E2ECFE1BCCDED017DC1E4030910F` (23,681,024 B) against the
  pin's `2B56A1CD...038E` (23,681,024 B): **IDENTICAL-EXCEPT-BUILD-ID** (PE and debug-directory stamps, PDB GUID).
  Report `perf/evidence/0504/4807/b4/defineoff.txt`.
- **Fast builds** (`kitbuild.py`, `aeae7c8e5`): verify (`DANGER`, `DANGER_VERIFY`, `DANGER_DG7`, `DANGER_DG7_VERIFY`)
  `F834B3D7...9293` and on (`DANGER`, `DANGER_DG7`) `33774843...E650`; `perf/evidence/0504/4807/b4/builds.txt`.
- **Verify** `[11:08-11:13 EST]` (`verifyrun.py --package DANGER_DG7 --expect clean --windows M,H`): **PASS** on both, games
  IDENTICAL to the batch-4 pin's (M 15 tables, 32,836 rows; H 14 tables, 125,197 rows), 0 differences, 0 spoils:

  | Window | DANGER checks (totals) | DANGER_DG7 checks | lookups (stats) | reach hits | hit rate | DG7 hits | not stored for the generation check |
  |---|---|---|---|---|---|---|---|
  | M 151-160 | 8,448 | 16,830 | 10,996 | 8,448 | 76.8 % | 8,415 | 0 |
  | H 301-305 (totals 301-304) | 50,940 | 99,190 | 70,259 | 58,328 | 83.0 % | 56,718 | **0** |

  H's core finder passes its 65,535th search after the load in turn 303 (P0.2: about 21,000 searches per H turn), so
  the old 16-bit check would have refused every later store there; `notstored_gen` 0 shows `1d86a3318` works `[L]`.
  The counts differ from batch 3's (8.2: M 10,704 / 8,205, H 72,692 / 60,086) because A9 changes the games.
- **Identity of the on build** `[11:13-11:21 EST]` (`idjob.py --windows M,H,E`, against the pin's DLL `2B56A1CD` in its
  layout `x64-c29442e8-p58724e0`; `F:/c5-64bit-work/4807/b4/id-on/idjob.txt`): M 150..160 **IDENTICAL** (15 tables,
  32,836 rows), H 300..305 **IDENTICAL** (14 tables, 125,197 rows), **E 0..50 IDENTICAL** (15 tables, 20,619 rows): with
  0508's fix in the pin the `IsReligionOwner` column no longer moves with the code layout, so E now proves the package
  (8.4's E block is resolved; batch 3 had it DIFFERENT for both builds).
- **Narrowing gate** `[11:21-11:25 EST]` (`narrow.sh aeae7c8e5 C:/c5-64bit-work/4807/narrow-b4` with `STUBS` = the batch-4
  stubs and every DANGER and DG7 define: `DANGER`, `_VERIFY`, `_STATS`, `_SPOIL_EVERY=50`, `_DG7`, `_DG7_VERIFY`,
  `_DG7_SPOIL=1000`, `_DG7_SPOIL_EVERY=50`; fast, compile-only, `--shorten`): 794 sites, baseline 798, **0 new**, 4 gone.
  (`narrow.sh` takes `STUBS` from the environment since this slice; it was fixed to the batch-3 stubs.)
- `linecheck.py --define VP64_SPEED_DANGER,VP64_SPEED_DANGER_DG7 --base c29442e8c`: OK on 14 files (after `aeae7c8e5`).

### 8.6 The package on the fixed stock: batch 4 + 4813's finder fix (milestone B slice "fixb4", impl-4807, 2026-11-18)

**Why this slice**: before ticket 4813's fix, the package's `on` game left stock at turn 182 of the D24 standard part 3
continuation because skipped searches changed what later searches read (8.2b, 8.2c, 8.2d). 4813's fix
(`../03-dll-engine-boundary/cnip-not-refreshed-neighbour-read.md`) refreshes the two read sites, so a search's answer no
longer depends on the finder's history; with it the package's games should equal the fixed stock's everywhere. The fix
goes into the next pin batch (the MC's); until then the fixed builds stand in for that pin: x64 `fix-cnip-x64`
`909f73bd1`, release `af148133...` (`C:/c5-64bit-work/4813/rel-x64`), x86 `fix-cnip-i0ship` `02749f1b9`, release
`690ce2b5...` (`C:/c5-64bit-work/4813/rel-i0`).

**Branch** `[C]`: fork scratch `scratch-4807-fixb4` `5bb46bd96` (clone `C:/c5-64bit-work/4807/fork-b4`; pushed) =
`perf/speed-danger-b4` `aeae7c8e5` + `909f73bd1` (cherry-pick `-x`, clean: the fix touches `CvAStar.cpp`/`CvAStar.h`, the
package's `CvAStar.h` block keeps its `#line`). `linecheck.py --base 909f73bd1 --define
VP64_SPEED_DANGER,VP64_SPEED_DANGER_DG7`: **OK, 14 files** (every `#line N` names the fixed stock's line). Scripts:
`perf/evidence/0504/4807/fixb4/` (`setup_fork.sh`, `run_fixb4.sh`, `push_fork.sh`); log
`C:/c5-64bit-work/4807/fixb4/run.log`; builds `C:/c5-64bit-work/4807/b-fixb4-*`; runs `F:/c5-64bit-work/4807/fixb4/`.

**Method** (`run_fixb4.sh`, workstation (shared): functional runs, no timing):
- builds: on release (`kitbuild.py --config release`, `DANGER` + `DANGER_DG7`); verify fast (+ both verify defines);
  define off: the branch built exactly as 4813 built `af148133` (plain copy, `build_as_pin.py --describe
  Release-5.4.8-64-g909f73bd1`, release, `--shorten`, `SOURCE_DATE_EPOCH=1795025581`, read from `af148133`'s
  `__DATE__`/`__TIME__` "Nov 18 2026 18:13:01" UTC, as `defineoff.py` cannot: it compares with `deps/dll.lock`'s pin), then
  `dllcmp.py --allow-build-id` against `af148133` (expect IDENTICAL-EXCEPT-BUILD-ID); the narrowing gate on every
  DANGER and DG7 define (expect 0 new).
- reference layout `layout-fix`: 4813's layout (the batch-4 pin layout bin with `af148133`'s DLL and PDB), copied; every
  `idjob.py`/`verifyrun.py` here takes it as `--layout-bin`, so the reference arm is the fixed stock.
- D24: `tools/d10/run_d10.py --x86-dll <690ce2b5> --srv-bin <layout-on = layout-fix with the on release DLL> 
  --order-exception none --d18-x86 yes --d18-x64 yes --jobs 8` (expect parts 1-3 PASS; part 3 is the case that failed).
- verify (`verifyrun.py --package DANGER_DG7 --expect clean`): LONGM (M 150-300) and 4803's coverage windows CONGM,
  CONGH, SPECH, CORPM (expect 0 differences, games IDENTICAL to the fixed stock).
- identity (`idjob.py`, the on release build): M, H, E 0-50 (expect IDENTICAL to the fixed stock), then in `on` mode LONGM
  and the coverage windows too, reusing the verify run's references (`--ref-from`).
- timing and memory: one `oldpc` D178 dev pair (quiet), fixed stock `af148133` against fixed + package on (release), H
  301-311, 3 per arm; the private memory after 10 turns per arm (D151).

**Builds** `[C, 2026-11-18, fork 5bb46bd96]`:

| Build | Defines | Config | DLL SHA-256 | Notes |
|---|---|---|---|---|
| define off | none | release, `build_as_pin.py` as 4813, epoch 1795025581 | `D718021CF59EDBB8BB367C0E56A9D923D39AB679CA7BBFD436B1AEDB26C53D10` (23,681,536 B) | **IDENTICAL-EXCEPT-BUILD-ID** against `af148133` (PE and debug-directory stamps, PDB GUID; checksum equal), `perf/evidence/0504/4807/fixb4/defineoff.txt`; narrowing 794 / baseline 798, 0 new; link 382 s, 14:23-14:30 EST |
| on | `DANGER`, `DANGER_DG7` | release (`kitbuild.py`, version `Release-5.4.8-76-g5bb46bd96c+speed`) | `9EA6E98BFF305DEF967539A1917FE726D98F22BD24DE0BBD87803F630294F04B` (23,706,112 B) | 14:09-14:17 EST; the timing arm and the identity candidate |
| verify | + `DANGER_VERIFY`, `DANGER_DG7_VERIFY` | fast | `1617FE69EDFC6F6A888E962E6593E53F1FB1483B543786DE3D933B0D92D99BC0` (23,548,416 B) | 14:17-14:22 EST |

So the epoch trick works: a define-off build of a speed branch on an unpinned base can be compared with that base's
release DLL by reading the base DLL's `__DATE__`/`__TIME__` strings ("Nov 18 2026", "18:13:01", UTC) and passing the
same `SOURCE_DATE_EPOCH` and describe; `defineoff.py` only knows `deps/dll.lock`'s pin `[L]`.

**D24 three-part save test: PASS** `[L, 2026-11-18 14:30:51-14:53:48 EST, wall 1,377 s, workstation]`
(`tools/d10/run_d10.py --x86-dll <690ce2b5> --srv-bin <layout-on: the batch-4 pin layout with the on release DLL
9EA6E98B> --order-exception none --d18-x86 yes --d18-x64 yes --jobs 8`, AI logging off in both arms; report
`perf/evidence/0504/4807/fixb4/d24-report.txt`, user paths as `%USERPROFILE%`; run folder `F:/c5-64bit-work/4807/fixb4/d24`):

| Game | part 1 writer | part 2: x86 direct = x64 direct (rows) | part 3 cross-load gates (rows) |
|---|---|---|---|
| tiny 0-100 | pass (only the version-string exception) | pass, 30,569 | pass: x64 continues x86-T25 = x86's (26,228); x86 continues x64-T25 = x64's (26,513); round trips identical |
| standard 0-200 | pass | pass, 359,261 | **pass**: x64 continues x86-T50 = x86's (337,919); **x86 continues x64-T50 = x64 continues x64-T50 (341,149)**; round trips identical |
| huge 350-400 | pass | pass, 685,488 | pass: 357,729 / 357,240; round trips identical |

Standard part 3 is the case that failed before the fix (the batch-3 `on` x64 continuation of x64-T50 left stock at turn
182, 8.2b). Part 2's row counts are the fixed stock's own (4813's parity run: 30,569 / 359,261 / 685,488), so the
package's direct plays equal the fixed stock's x86 and x64 direct plays row for row. (The non-gating comparisons whose
A/A control differs behave as in every D24 run: a continuation is not the direct play.)

**Timing and memory: oldpc D178 dev pair** `[L, 2026-11-18 14:18:17-14:36:11 EST, oldpc, quiet: alone, undisturbed,
foreign cores 0.033; NVIDIA GeForce RTX 2080, driver 32.0.16.1088 of 2026-07-21 (D175)]`: `perf/speedkit/pair.py --a
fix=<af148133> --b danger=<9EA6E98B> --scenario H --runs 3 --turns 11 --issue 4807` (job `20261118-141743-e19e`, both arms
in the batch-4 pin's layout, server `fc73e60e`, AI logging off, interleaved; `perf/tbjobs/analyze.py --ref fix`;
evidence `perf/evidence/0504/4807/fixb4/oldpc-e19e/`, job manifest left out):

| Measure | fix (3 runs) | fix + danger package on (3 runs) | difference |
|---|---|---|---|
| s/turn, H 301-311 | 14.741 (sd 0.038) | 13.862 (sd 0.034) | **-0.880 s (-5.97 %)**, 95 % CI [-0.974, -0.786], significant (detectable about 0.083) |
| turn 301 | 14.367 | 13.060 | -1.307 s (-9.1 %) |
| private MB after 10 turns (D151), per run | 325.5 / 325.3 / 320.6 (mean 323.8) | 336.0 / 332.9 / 334.9 (mean 334.6) | **+10.8 MB** |
| growth MB/turn | 2.12 / 2.58 / 1.60 | 2.76 / 2.78 / 2.72 | |
| peak working set | 259 MB | 268 MB | +9 MB |
| stats.db across arms (round 1) | | | **IDENTICAL** (14 tables, 270,241 rows) |

Why the numbers moved: unlike 4813's pair (where the fix played another game from turn 302, so its -1.10 % was not a
speedup), **both arms play the same game here** (stats.db IDENTICAL), so the -0.88 s is the package's: the reach searches
it does not run (H about 83 % hit rate, 8.5) and DG7's kept tile lists. It is the batch-3 result's size (-0.970 s,
-6.36 %, 8.3; another game, A9 changed H since) on the fixed stock. Memory: the batch-3 pair (no DG7) measured +5.5 MB;
the package's known tables at H's peak are the stamp arrays (3.25 MB), the reach entries (2.1 MB) and DG7's lists (about
1.1 MB, 8.4), about 6.5 MB; the remaining about 4 MB of the +10.8 MB are not accounted for `[I: heap growth from the
entries' churn, or the single-sample noise of several MB that speedup-test-kit.md section 5 names; n = 3, the fix arm's
spread is 4.9 MB]`. (Corrected 2026-11-18 in 8.7: the Welch interval of this +10.8 MB is [+4.9, +16.7] and contains
the 6.5 MB of tables, the difference is already there at the load sample and does not grow; no unaccounted part is
shown.) Development signal only (D178, n = 3); default-on needs grade A and the MC's word.

**Verify against the fixed stock: PASS** `[L, 2026-11-18 14:53-15:23 EST]` (`verifyrun.py --package DANGER_DG7 --expect
clean --layout-bin <layout-fix>`, verify build `1617FE69`; every reach hit and every DG7 hit searched or derived again
and compared; `perf/evidence/0504/4807/fixb4/verifyrun.txt`, `summary.txt`, `VP64DangerStats-*.csv`):

| Window | DANGER checks (totals) | DANGER_DG7 checks | differences | game vs the fixed stock | lookups / reach hits (stats) | hit rate | DG7 hits | not stored (generation) | cache bytes peak |
|---|---|---|---|---|---|---|---|---|---|
| LONGM M 150-300 (totals 151-299; no differences file, so 300 too) | 146,263 | 287,526 | **0** | IDENTICAL (15 tables, 1,033,788 rows) | 215,028 / 147,583 | 68.6 % | 145,031 | 0 | 0.98 MB |
| CONGM M 180-183 | 3,004 | 5,970 | 0 | IDENTICAL (16,612 rows) | 4,297 / 3,079 | 71.7 % | 3,060 | 0 | 0.40 MB |
| CONGH H 305-308 | 31,561 | 61,350 | 0 | IDENTICAL (82,168 rows) | 50,063 / 41,647 | 83.2 % | 40,416 | 0 | 3.36 MB |
| SPECH H 310-313 | 24,469 | 47,116 | 0 | IDENTICAL (82,689 rows) | 30,640 / 24,470 | 79.9 % | 23,559 | 0 | 3.26 MB |
| CORPM M 240-245 | 4,138 | 8,140 | 0 | IDENTICAL (42,812 rows) | 6,583 / 4,439 | 67.4 % | 4,371 | 0 | 0.58 MB |

(The stats files run one turn past the window, as each run plays one turn more. Stamp arrays: M 1.11 MB, H 3.25 MB.)

**Identity of the on release build against the fixed stock: IDENTICAL** `[L, 2026-11-18 15:23-15:32 EST]` (`idjob.py
--dll <9EA6E98B> --layout-bin <layout-fix> --windows M,H,E`, reference `af148133`; `perf/evidence/0504/4807/fixb4/id-on.txt`):
M 150..160 IDENTICAL (15 tables, 32,815 rows), H 300..305 IDENTICAL (14 tables, 125,209 rows), E 0..50 IDENTICAL (15
tables, 20,619 rows).

**On-mode identity over the long window and the coverage windows: IDENTICAL** `[L, 2026-11-18 15:36-16:03 EST]` (`idjob.py
--dll <9EA6E98B> --layout-bin <layout-fix> --window <W> --ref-from <the verify run's idjob folder of W>`: the fixed-stock
reference runs of the verify step reused, only the candidate played; `summary.txt`): LONGM 150..300 IDENTICAL (15
tables, 1,033,788 rows), CONGM 180..183 (16,612), CONGH 305..308 (82,168), SPECH 310..313 (82,689), CORPM 240..245
(42,812) all IDENTICAL. This is the class verify cannot see (the skipped searches' effect on the finder's history, 8.2b):
in `on` mode the package plays the fixed stock's game over 150 M turns, and over D24's standard 50-200 (part 3 above).

**Narrowing gate** `[L, 15:32-15:36 EST]` (`narrow.sh 5bb46bd96` with the batch-4 stubs and every DANGER and DG7 define:
`DANGER`, `_VERIFY`, `_STATS`, `_SPOIL_EVERY=50`, `_DG7`, `_DG7_VERIFY`, `_DG7_SPOIL=1000`, `_DG7_SPOIL_EVERY=50`; fast,
compile-only, `--shorten`): 794 sites, baseline 798, **0 new**, 4 gone.

**Verdict** `[L on the windows named; I elsewhere]`: on the fixed stock the package is exact in every test this ticket's
acceptance names for this slice: define off = the fixed stock's DLL except the build identity, verify 0 differences
(about 500,000 checks over LONGM and the four coverage windows), `on` identity on M, H, E, LONGM and the coverage
windows, and the D24 save test (all three games, all three parts, standard part 3 included) against the fixed x86. The
finder-history divergence of 8.2b is gone with 4813's fix, as 4813's trace predicted (all searches equal there). The
timing gain stands on the same game: H -0.880 s/turn (-5.97 %) on `oldpc`. So the package's default-on now waits only on
the fix being pinned (the MC's next pin batch; the package must then be rebased onto that pin and its define-off check
rerun with `defineoff.py`), grade-A timing, the PGO arms and the acceptance items below, and the MC's word.

**Not verified in this slice** (2026-11-18; the PGO-arm pair, the 43-civ variant, the stress map, the windowed pair and
the memory question were done in 8.7): the PGO-arm pair (each arm its own trained profile, P1's tooling); the
43-civ variant; the D157 stress map (50 turns); the windowed S4 vs S4 pair (M 151-160); the client soak with the HI
sweeper; a fixed-seed 0-300+ game in verify and `on` mode (LONGM 150-300 and D24's standard 0-200 stand in); DG3, MB5
(not built) and OT5 (not started); a game with a recon unit or a run-time landmass recalculation (DG7's hooks still
unexercised: 0 snapshot and 0 landmass misses); the +10.8 MB's unaccounted part (about 4 MB) and M's memory on this
base; the batch-3 branch `perf/speed-danger` and `perf/speed-danger-b4` themselves (not changed: the fix lives only on
`scratch-4807-fixb4` until the pin carries it).

### 8.7 Acceptance items on the fixed stock: PGO arms, 43-civ, the D157 stress map, windowed S4 (slice "accept", impl-4807, 2026-11-18)

Same base as 8.6: the fixed stock (x64 batch 4 + 4813's finder fix, fork `909f73bd1`, release `af148133`) stands in for
pin batch 5 (ticket 0551, not pinned yet; MC #5793); the package is fork scratch `scratch-4807-fixb4` `5bb46bd96`
(`DANGER` + `DANGER_DG7`). Scripts `perf/evidence/0504/4807/accept/`; scratch `C:/c5-64bit-work/4807/accept`, runs
`F:/c5-64bit-work/4807/accept`.

**Method**:
- PGO arms (`pgo_arms.sh`): per arm a `pgo-instr` build (`build_as_pin.py ... -- --config pgo-instr --shorten`, batch-4
  stubs; the arm's defines in `CL` as `kitbuild.py` sets them), training with `tools/x64-dll/pgo/train.py --sets H,M,S`
  (4801's set: H 300-305, M 150-160 from 0438's saves and statistics-on database, the Standard fixed-seed game 0-50;
  headless, AI logging off, in a copy of 4813's fixed layout with the arm's instrumented DLL), then `release --profile
  <the arm's own profile>`. fix = `909f73bd1` (no define, describe `Release-5.4.8-64-g909f73bd1`), on = `5bb46bd96`
  (describe `Release-5.4.8-76-g5bb46bd96c+speed`). Lock records `pgo-fix.lock`, `pgo-on.lock` (train.py's format; the
  profiles themselves are not committed, MC #4429 (2)). Then the oldpc D178 pair as in 8.6 (`pair.py --scenario H --runs
  3 --turns 11`, quiet). Note: the H training window (300-305) lies inside the timed window (301-311), as in 4801.
- 43-civ (`m43.sh`): both arms built `fast` (functional, no link place) with `--major-civs 43`, run in copies of the
  fixed layout (one `server.exe` serves both player counts, `../03-dll-engine-boundary/player-limits.md`); the 30-major
  Large fixed-seed game (`server/tests/specs/continents-large-30majors-fixed-seed.json`, 4254's parity game: slots past
  22 in use) 0 -> 100, AI logging off; `statscmp.py`.
- D157 stress map (`m43.sh`): the same two 43-civ DLLs; 9073's stress game `TS1.20-T200` (42 AI majors, 20 city states,
  Tectonic, Marathon) -> 250, the VP neutral toolkit game folder, 9074's no-victory probe, 9071's database copied and
  made statistics-on (`perf/evidence/0438/dbinfo.py --make-stats-on`; 9071's own has `SQLITE_LOGGING = 0`).
- Windowed S4 (`winpair.sh`, `winarm.sh`, from 4804's `idpair.sh`/`idarm.sh`): Goldberg instance `c5h-e` (claimed), per
  arm `provision.py set-proxy --bin <layout-fix or layout-on of 8.6> --proxy <the batch-4 pin's proxy>`, `TS0438-M-T150`
  loaded through `perf/profilekit/acceptance.py run --config inc1-thin` (no-focus launch, dialog watcher), 10 AI turns,
  `SQLITE_LOGGING` switched on for the run (backed up and restored by SHA-256), `set-proxy --original` after each arm;
  `statscmp.py` of the two `stats.db`.

**Windowed S4 pair, M 150 -> 160: IDENTICAL** `[L, 2026-11-18 16:23-16:54 EST, workstation, c5h-e (Goldberg), no-focus
launch (civ_bg guard: veto ok, self-test ok), dialog watcher: no box]`: arm fix ran DLL `af148133`, arm on `9ea6e98b`
(both read back from `acceptance-run.json` `binaries_sha256`), the same server `fc73e60e`, proxy `3dfd7521`, EXE
`04cfdc71`, save `98fc454a` (`TS0438-M-T150`). `statscmp.py`: **15 tables, 32,293 rows, IDENTICAL**
(`perf/evidence/0504/4807/accept/win-statscmp.txt`). The same row count as 4804's windowed pair of the batch-3 pin
(32,293). So the package's two-thread rule holds in the client: the shell's UI-thread calls during 10 AI turns did not
change the game (the reach cache and DG7 answer only on the core thread, section 2). Both instance files touched
(`NewCustomModOptions.xml`) restored by SHA-256 (`backup.py restore` rc 0 per arm), `set-proxy --original` and
`provision.py verify --quick` rc 0 at the end. The load took 340-350 s per arm and turns 36-83 s (the workstation was
running the PGO links and the 43-civ games; no timing claim). First attempt refused before launch: `acceptance.py`'s
`--client-build` takes no comma (fixed in `winarm.sh`); `set-proxy` refuses an instance that is already proxied, so
`winpair.sh` returns to the original between arms.

**43-civ variant: IDENTICAL** `[L, 2026-11-18 16:32-16:36 EST, workstation (shared), headless]`: fast 43-civ builds
(`build_as_pin.py ... -- --config fast --major-civs 43`, batch-4 stubs): fix43 `909f73bd1` `308955D4...E71C`
(23,498,752 B), on43 `5bb46bd96` + `DANGER` + `DANGER_DG7` `F333CF91...6137` (23,520,256 B), each in a copy of
`layout-fix` (server `fc73e60e`). The 30-major Large fixed-seed game 0 -> 100, AI logging off: both arms exit 0
(254 s, 250 s), `statscmp.py`: **15 tables, 164,780 rows, IDENTICAL** (`g43-statscmp.txt`). CvAssert.log: 2 lines in each
arm, the same asserts. This is the closest test the variant supports for two x64 DLLs (22-civ saves do not load in a
43-civ build, so idjob's M/H cannot run; 4254's parity game is the variant's own fixed-seed game with slots past 22 in
use).

**D157 stress map, 50 turns: no crash, IDENTICAL** `[L, 2026-11-18 16:36-16:42 EST, workstation (shared), headless]`:
the same two 43-civ DLLs, `TS1.20-T200` (`c8e96ca4...4e5f`; 42 AI majors, 20 city states, Tectonic, Marathon, 180 x 112)
-> 250, 9071's database made statistics-on (content `02b5faa4...` before the switch), VP neutral toolkit game folder,
9074's no-victory probe, AI logging off: both arms exit 0 (330 s, 334 s; about 6.6 s per turn), turn 250 reached,
**`statscmp.py`: 15 tables, 173,173 rows, IDENTICAL** (turns 201-250; `stress-statscmp.txt`); CvAssert.log 2 lines in
each arm, the same (`CvGame.cpp:5989` "Failed to generate diplo response query", stock behaviour, both arms). No crash in
either arm, so nothing for D185.

**D157 stress map in verify mode: 0 differences** `[L, 2026-11-18 16:45-17:01 EST, workstation (shared), headless]`: a
43-civ verify fast build (`DANGER`, `DANGER_VERIFY`, `DANGER_DG7`, `DANGER_DG7_VERIFY`; `5bb46bd96`)
`7C30DA1D...88D8` (23,540,224 B) over the same window (`TS1.20-T200` -> 250, `VP64_SPEED_DIR` set for the run; wall
695 s): `VP64SpeedTotals.csv` DANGER **306,187 checks, 0 differences**, DANGER_DG7 **612,094 checks, 0 differences**,
0 spoils (to turn 248: the totals miss the last turns, 8.3's kit finding), **no `VP64SpeedDiffs.csv`** (so 249-250 clean
too), game **IDENTICAL** to fix43's (173,173 rows). Coverage (`VP64DangerStats.csv`, 201-250, copied as
`perf/evidence/0504/4807/accept/VP64DangerStats-stress43.csv`): 336,009 lookups, **312,458 hits (93.0 %)**, all
verified; DG7 312,315 hits (92.9 %); `notstored_gen` 0, Lua-hook uncacheable 0, DG7 snapshot and landmass misses 0;
cache peak 0.58 MB, stamp arrays 5.32 MB (180 x 112 = 20,160 plots; H's are 3.25 MB). So the 43-civ map with its 62 players
exercises the cache more than H (83 %): about 6,700 reach lookups per turn in Marathon's early game (techs 10-16).
`[I]` The higher hit rate fits Marathon's slow turns: fewer units move per turn, so fewer entries go stale.

**Item 5, the memory: where the +10.8 MB of 8.6 arises** `[L data of job 20261118-141743-e19e; analysis I]`
(`perf/evidence/0504/4807/accept/memturns.py` on that job's `series.json`, the server's private MB per turn, mean of 3
runs per arm; table `memturns-e19e.txt`):

| turn (sample after it) | 300 (after the load) | 301 | 302 | 303 | 304 | 305 | 306 | 307 | 308 | 309 | 310 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| on - fix, private MB | +7.5 | +5.7 | +7.9 | -2.0 | +6.2 | +5.8 | +11.0 | +7.1 | +6.5 | +9.5 | +10.8 |

- The difference is there **at the first sample after the load (+7.5 MB)**, before any timed turn, and moves between -2
  and +11 MB from turn to turn after it; the growth over the 10 turns (+3.3 MB, 7.5 -> 10.8) is inside that turn-to-turn
  noise. So there is no sign of a part that grows with play (no leak, no heap creep from the entries' churn) at n = 3.
- Welch's 95 % interval of the after-10-turns difference is **[+4.9, +16.7] MB** (df 3.2); the package's known tables at
  H's peak (stamp arrays 3.25 MB + reach entries 2.1 MB + DG7 lists about 1.1 MB = about 6.5 MB) lie inside it. So the
  "about 4 MB not accounted for" of 8.6 is **not a measured quantity**: with 3 runs per arm the interval is about 12 MB
  wide. `[I]` The +7.5 MB at the load sample: the stamp arrays are allocated at game start and the danger maps of every
  player are rebuilt during the load (reach searches fill the cache before turn 301), so most of the tables already exist
  there.
- What would settle the rest: the DLL's own byte counters at the 10-turn sample (`VP64DangerStats.csv` `bytes_peak` +
  `stamp_bytes` per turn, written by a statistics build) next to the server's private bytes in the same run, or 6+ runs
  per arm. Not done in this slice.

**PGO arms: builds and profiles** `[C, 2026-11-18 16:14-16:51 EST, workstation; pgo_arms.sh, log
C:/c5-64bit-work/4807/accept/pgo_arms.log]` (each whole-program link took the one link place, waited 0 s):

| Arm | pgo-instr DLL (link s, peak private) | training (H 300-305 / M / S wall s; instrumented H s/turn) | profile (SHA-256, B) | release + own profile DLL (link s, peak private) | PGO diagnostics |
|---|---|---|---|---|---|
| fix (`909f73bd1`) | `C6EAFDF9...BDF8E`, 48,837,120 B (394 s, 16,363 MB) | 214 / 34 / 34; 39.9 | `677470b7...0702`, 13,505,584 | **`88A78571...66CD`**, 24,741,888 B (383 s, 14,678 MB) | 0 hash mismatches, 0 missing |
| on (`5bb46bd96` + `DANGER`, `DANGER_DG7`) | `F5ACF755...05E8`, 48,875,008 B (433 s, 16,323 MB) | see `pgo-on.lock` | `bb14d915...adde`, 13,517,568 | **`876ED103...7667`**, 24,805,376 B (397 s, 14,678 MB) | 0 hash mismatches, 0 missing |

Lock records `perf/evidence/0504/4807/accept/pgo-fix.lock`, `pgo-on.lock` (train.py's format: inputs and raw profiles by
SHA-256, LLVM 19.1.7, the profile runtime's SHA-256); profiles kept at `F:/c5-64bit-work/4807/accept/profile-{fix,on}/`,
not committed (MC #4429 (2)). The link peaks match 4801's (instrumented 16.3 GB, profile-consuming 14.7 GB).

**PGO-arm oldpc D178 pair** `[L, 2026-11-18 16:54:40-17:10:21 EST, oldpc, quiet: alone, undisturbed, foreign cores 0.038;
NVIDIA GeForce RTX 2080, driver 32.0.16.1088 of 2026-07-21 (D175)]`: `perf/speedkit/pair.py --a fixpgo=<88A78571> --b
dangerpgo=<876ED103> --scenario H --runs 3 --turns 11 --issue 4807` (job `20261118-165347-35b8`, both arms in the batch-4
pin's layout, server `fc73e60e`, AI logging off, interleaved; `perf/tbjobs/analyze.py --ref fixpgo`; evidence
`perf/evidence/0504/4807/accept/oldpc-35b8/`, job manifest and process lists left out):

| Measure | fix, PGO (3 runs) | fix + danger, PGO (3 runs) | difference |
|---|---|---|---|
| s/turn, H 301-311 | 12.843 (sd 0.028) | 12.150 (sd 0.017) | **-0.693 s (-5.40 %)**, 95 % CI [-0.752, -0.633], significant (detectable about 0.052) |
| turn 301 | 12.479 | 11.439 | -1.040 s (-8.3 %) |
| private MB after 10 turns (D151), per run | 323.8 / 324.9 / 326.7 (mean 325.1) | 332.5 / 328.2 / 333.5 (mean 331.4) | **+6.3 MB**, Welch 95 % CI [+0.4, +12.1] |
| peak working set | 261 MB | 268 MB | +7 MB |
| stats.db across arms (round 1) | | | **IDENTICAL** (14 tables, 270,241 rows: the plain pair's game of 8.6, row for row in count) |

Why the numbers moved: both arms play the same game, so the -0.693 s is the package's under PGO. It is smaller than on
plain release arms (-0.880 s, 8.6) because PGO speeds up the reach searches the package skips: the fixed stock gains
-1.898 s/turn from PGO alone (14.741 -> 12.843, -12.9 %; two jobs on the same quiet host 30 minutes apart, the same
layout and driver: a cross-job comparison, so indicative `[I]`), the package then saves 5.4 % of the faster turn. Package
+ PGO against plain fixed stock: 14.741 -> 12.150 (-17.6 %, cross-job `[I]`). Development signal only (D178, n = 3).

**Memory over both pairs** (item 5 continued; `memturns-35b8.txt`): the PGO pair's difference after 10 turns is +6.3 MB
[+0.4, +12.1]; the plain pair's +10.8 MB [+4.9, +16.7]. Both intervals contain the package's known tables (about 6.5 MB
at H's peak); the PGO pair's per-turn difference runs from -12.2 MB (the load sample) to +12.5 MB. **Verdict
`[I]`: no unaccounted memory is shown**: the 8.6 "about 4 MB" was the plain pair's sampling noise; the best estimate of
the package's cost is its own table bytes (stamp arrays + entries + DG7 lists, about 6.5 MB on H, 1.3-1.5 MB on M,
about 5.9 MB on the stress map at 0.58 MB peak entries + 5.32 MB stamps).

**Verdict of the slice** `[L on the runs named]`: with the package on, on the fixed stock: windowed S4 IDENTICAL, the
43-civ variant IDENTICAL over 100 turns, the D157 stress map 50 turns without a crash, IDENTICAL, and verify-clean
(918,281 checks, 0 differences), the PGO-arm pair -0.693 s/turn (-5.40 %) on the same game. The ticket's acceptance list
is now met for these items on the stand-in for pin batch 5.

**Not verified in this slice** (2026-11-18): the client soak with the HI sweeper (epic 0392 is done and its sweeper
`tools/hi/sweep.py` is on main; not in this slice's scope); a fixed-seed 0-300+ game in verify and `on` mode (LONGM
150-300, D24's standard 0-200 and the stress map stand in); DG3, MB5 (not built), OT5 (not started); grade-A timing (6+
runs per arm) and a same-job plain-vs-PGO comparison; the 43-civ variant and the stress map in a release build (both ran
fast builds: like with like, functional only); the stress map past turn 250 or with recon units / a run-time landmass
recalculation (DG7's snapshot and landmass misses still 0); everything on the real pin batch 5 (0551) once pinned: rebase,
`defineoff.py`, idjob M/H/E, D24.


### 8.8 The fixed-seed Standard game 0-300 in verify and `on` mode (lead-0504, 2026-11-18)

**Why**: the ticket's acceptance asks for verify with 0 differences on one fixed-seed game of 0-300 turns or more, and the
Architect's acceptance additions ask for a long `on`-mode game (section 4: the skipped searches' effect on the finder's
history is visible only in `on` mode). Until now LONGM 150-300 and D24's standard 0-200 stood in (8.6, 8.7).

**Method** `[L, 2026-11-18 21:09-21:37 EST, workstation (shared): functional, no timing]`: headless only, no game
instance. `perf/evidence/0504/4807/g300/g300.py` (uses `perf/speedkit/idjob.py`'s play, trim and statscmp, and
`verifyrun.py`'s verdict) plays `server/specs/continents-standard-fixed-seed.json` (idjob's E game) to turn 301 three
times, each in a copy of 8.6's `layout-fix` bin (the batch-4 pin layout, server `fc73e60e...`) with one DLL swapped:
ref = the fixed stock `af148133` (batch 4 + 4813's finder fix), verify = `1617FE69` (DANGER, DANGER_DG7 and both
`_VERIFY` defines, fast; `VP64_SPEED_DIR` set), on = `9EA6E98B` (release); AI logging off, jobcap 8 GB; then statscmp
0..300 of each against ref. Run folder `C:/c5-64bit-work/4807/g300`; evidence `perf/evidence/0504/4807/g300/`
(`g300.txt`, `VP64SpeedTotals.csv`, `statscmp-verify.txt`, `statscmp-on.txt`).

| Arm | wall (s) | vs the fixed stock, turns 0..300 | DANGER checks / differences | DANGER_DG7 checks / differences |
|---|---|---|---|---|
| ref `af148133` | 504 | (reference) | | |
| verify `1617FE69` | 635 | **IDENTICAL** (15 tables, 971,505 rows) | 129,411 / **0** | 253,976 / **0** |
| on `9EA6E98B` | 556 | **IDENTICAL** (15 tables, 971,505 rows) | | |

(The totals file's last turn is 299: it is rewritten at each turn's end, and the run stops at the start of 301; the
statistics compare 0..300.) **Verdict**: the package is verify-clean over a whole 300-turn game from turn 0 and plays the
fixed stock's game in `on` mode over all of it. The walls are not a timing claim (shared workstation, n = 1).

**Not verified here**: the same on pin batch 5 (0551) once its branches exist; the client soak (its own section).

### 8.9 The client soak: ui-thread screens during AI turns, verify build (impl-4807-soak, 2026-11-18, MC #6441)

The Architect's last two-thread item before default-on (review section 2; epic brief, the two-thread bullet): a client
(the 32-bit EXE as the shell, the proxy, the x64 server) running the package's **verify** build, with screens opened by
HI input during AI turns, so the ui thread calls the DLL while an AI computation is suspended at a hook raise.

**Method.** Scripts `perf/evidence/0504/4807/soak/`; scratch `C:/c5-64bit-work/4807-soak/`.
- Build: the verify fast build of 8.6, `C:/c5-64bit-work/4807/b-fixb4-verify/dll` **`1617FE69...9BC0`** (fork
  `scratch-4807-fixb4` `5bb46bd96` = batch-4 pin + the package + 4813's finder fix; `DANGER`, `DANGER_DG7`, both
  `_VERIFY`), put into a copy of 8.6's `layout-fix` (`mklayout.sh`; server `fc73e60e`, list in `layout-SHA256.txt`),
  installed in Goldberg instance **`c5h-i`** (F:) with `provision.py set-proxy --bin <that copy> --proxy <the batch-4
  pin's proxy>` and returned with `set-proxy --original` at the end (`soakrun.sh`).
- F: read latency before the claim and at the start (`flat.py`: 40 unbuffered 64 KiB reads at random offsets of the
  instance's largest files): 21:22 0.4 ms mean (max 0.5 ms), 21:27 0.5 ms mean (max 5.0 ms); the MC's limit is 50 ms.
  0569's F: load window had ended at 21:15:30 (thread 0136 #6464).
- Game: a renamed copy of 0438's M save (`SOAK4807-M-T150`, the content of `TS0438-M-T150`, `98fc454a...a0c1`),
  loaded as `perf/profilekit/acceptance.py` loads it (the harness's no-focus launch, guard check, start-up watch,
  `wait_game`, `ensure_in_game`); AI leader screens answered by `tools/harness/diplo.py` (refusals only).
  `VP64_SPEED_DIR` set in the launching environment.
- The soak (`soak.py`): 22 (run 1) and 30 (run 2) human turns, each ended by `turns.end_turn` (forced, the human's units idle). While the AI
  players' turns run (the active player's turn not active), cycles of: open a screen, hover 8 points of it (tooltips,
  `tools/hi/actions.hover`, 500 ms each), close it (ESC posted, then the game's own popup queue, `popups.lua`). The
  screens in turn: the city screen (the HI sweeper's own opening call `UI.DoSelectCityAtPlot`, its `SETUP_OPEN`
  "open_city", rotating over the player's cities), the military overview (F3, posted input), the trade route overview
  (the game's popup event `BUTTONPOPUP_TRADE_ROUTE_OVERVIEW`, it has no key), the economic overview (F2). A cycle counts
  as "during AI" when the turn was not active both before the screen opened and after it closed. Every action checks
  the dialog watcher's box marker first (D155). **Not `sweep.py run` itself**: the sweeper reloads its save to restore a
  screen's starting state and sleeps the player's units, which a run of AI turns cannot have; the soak uses its action
  layer and its screen-opening recipes instead.

**Result: PASS, 52 AI turns in two runs, 0 differences, no crash, no box, no hang** `[L, 2026-11-18 21:27-21:59 EST,
workstation (shared), c5h-i (F:), no-focus launch (guard self-test ok, 4 vetoes ok), dialog watcher: no box]`.
Binaries read back from the instance (`soak.json` `binaries_sha256`): EXE `04cfdc71`, proxy `3dfd7521` (the batch-4
pin's), DLL **`1617fe69`** (the verify build), server `fc73e60e`; save `98fc454a` (`SOAK4807-M-T150`). Evidence:
`perf/evidence/0504/4807/soak/run1/`, `run2/` (`soak.json`, `screens.csv`, the DLL's verify files, `server.log`,
`server-report.txt`); summary by `summary.py`.

| | run 1 (21:27-21:40) | run 2 (21:42-21:59) |
|---|---|---|
| turns played (forced human ends) | 22 (150 -> 172) | 30 (150 -> 180) |
| load (launch to Lua channel) | 124.7 s | 123.1 s |
| AI turn wall, end turn -> human turn again (screens running) | 17.2-32.8 s, mean 26.4 | 20.3-31.6 s, mean 26.3 |
| screen cycles started during AI turns | 60 (city 19, military 18, trade 19, economic 4) | 60 (15 each) |
| screens open during AI turns | 38 (city 19, trade 19) | 59 (city 15, military 15, trade 14, economic 15) |
| ... of which still not the human's turn when closed | 19 (city 16, trade 3) | 31 (city 9, military 8, trade 6, economic 8) |
| hovers on open screens (tooltips) | 304 | 472 |
| `VP64SpeedTotals.csv` DANGER checks / differences / spoils | **14,592 / 0 / 0** (to turn 170) | **20,874 / 0 / 0** (to turn 178) |
| DANGER_DG7 checks / differences / spoils | **29,006 / 0 / 0** | **41,526 / 0 / 0** |
| `VP64SpeedDiffs.csv` | none | none |
| `VP64DangerStats.csv` lookups / hits, all verified | 20,605 / 15,237 (turns 151-171) | 29,552 / 21,627 (151-179) |
| uncacheable on the ui thread (`unc_thread`), Lua hook (`unc_lua`), `notstored_gen` | 0, 0, 0 | 0, 0, 0 |
| end | turn 172 reached, harness stop rc 0 | turn 180 reached, harness stop rc 0 |

- **No crash, no assert, no hang**: both games ended by the harness's stop at the human turn with the newest autosave
  settled (the EXE's exit code 15 is that stop's; `server.log`: "not a crash code; no exe-crash report"), no minidump in
  either run folder, no `CvAssert.log`, the dialog watcher's marker empty after each run; no AI turn took longer than
  33 s. So nothing for D185.
- **Military and economic overviews**: the posted F3 and F2 opened nothing during AI turns in run 1 (18 + 4 tries,
  0 opened; not investigated whether the in-game key handler ignores them while the turn is not active or EUI maps them
  elsewhere `[I]`). Run 2 opens them, when the key has shown nothing after 1 s, by the game's own popup event
  (`BUTTONPOPUP_MILITARY_OVERVIEW`, `BUTTONPOPUP_ECONOMIC_OVERVIEW`): 15 of 15 each opened that way.
- `unc_thread` = 0 in both runs: no ui-thread call reached the package's reach lookup. The screens' DLL calls (city,
  unit, trade and yield queries, tooltips) do not run reach searches, so in this soak the ui thread was active during AI
  turns without ever touching the cache: the package's state stayed the core thread's, as section 2 designs, and the AI
  turns verified clean around that activity.
- Instance state after each run: `set-proxy --original` rc 0, `provision.py verify --quick` rc 0, the save copy removed
  by hash; c5h-i released at 22:02.

**Verdict**: the danger package's verify build passes the Architect's client soak on M: 52 AI turns with 97 screens
opened during AI processing and 776 hovers, 35,466 DANGER and 70,532 DG7 verified hits, 0 differences, no crash, box or
hang.

**Not verified here**: that a ui-thread DLL call ran *at a hook raise inside* an AI computation rather than between two
AI players' turns (the proxy does not log the interleaving; the screens were open while the AI turns ran, so both kinds
occur `[I]`); `tools/hi/sweep.py run` itself (see the method); an H save (only M); the release `on` build 9EA6E98B in a
client (only the verify build); the full-length 8 h soak kind; a second instance or machine; the windowed identity of
these runs (not compared: the human ends were forced and the screens add UI-side calls; S4 of 8.7 is the identity proof);
pin batch 5 (0551) once pinned.

### 8.10 The package on pin batch 5 (slice "b5", impl-4807-b5, 2026-11-18; MC #6441, #6453)

What the MC asked for 0.1b's default-on (#6453): correctness of the package on pin batch 5 (define off identical except
the build identity, identity M/H/E, D24's three parts, verify LONGM 0 differences) next to the clean soak (8.9).
Scripts `perf/evidence/0504/4807/b5/`; scratch and runs `C:/c5-64bit-work/4807-b5/` (headless only, nothing on F: but
the read-only 0438 saves).

**The base** `[C, 2026-11-18 22:20-22:35 EST]`: ticket 0551 (account B, claimed 21:22) had already pushed its fork
branches when this slice began: `x64-batch5` **`1fd51353e`** = `x64-batch4` c29442e8c + `fca1f5b56` (4813's finder fix)
+ `1fd51353e` (x64-ifver, ticket 4719); `i0-ship-batch5` **`5d18d3d6c`** = `i0-ship-batch4` e96457fd5 + the finder fix.
No barred ancestor in either (`setup_fork.sh`). The lead's pre-batch-5 base, made anyway as `scratch-4807-b5base`
`6f43e48df` (c29442e8c + cherry-pick -x 909f73bd1 + 344bdff8e), has **the same tree as `x64-batch5`** (`fd043a50...`,
`git diff` empty); `i0-ship-batch5` has the same tree as 4813's `fix-cnip-i0ship` 02749f1b9 (`656a1eff...`); the
finder fix has one patch-id on all four commits (`3e180c82...`; `trees.txt`). So the package goes straight onto 0551's
branches: **`perf/speed-danger-b5` `5d4a90aa5`** = `x64-batch5` + the 12 commits of `perf/speed-danger-b4` (cherry-pick
-x, clean); the package's patch-id (`7cfd62cd...`) is the same on batch 4, on `scratch-4807-fixb4` and here, and
`linecheck.py` against `x64-batch5` is OK in all 14 files (`mk_branches.sh`).

**Method** (`run_b5.sh`): `deps/dll.lock` still names batch 4, so the reference is our own release build of
`x64-batch5` made by `defineoff.py` (the batch-4 pin's stubs built from its plain copy, its version string
`Release-5.4.8-63-gc29442e8c` and epoch 1794969988; that build's compare with the batch-4 pin's DLL is DIFFERENT by
design). Define off: `defineoff.py` on `perf/speed-danger-b5` with the same stubs (`--deps`), its DLL compared with the
reference by `dllcmp.py --allow-build-id`. On (release) and verify (fast) builds by `kitbuild.py`. The x64 arms run in a
copy of 8.6's `layout-fix` bin (the batch-4 pin layout: **server `fc73e60e`**) with the reference DLL (`layout-ref`) or
the on build (`layout-on`); 0551 has no batch-5 layout yet. idjob M/H/E (on vs reference), verifyrun LONGM (M 150-300),
D24 with the x86 release DLL of `i0-ship-batch5` built by 4813's recipe (`build_vp_clang.py --config release`), the
narrowing gate (`narrow.sh`, every DANGER and DG7 define).

**Reference and define off** `[C, 2026-11-18 22:29-23:02 EST, workstation (shared)]`: the reference, `x64-batch5`'s
release DLL, **`72693B8E...1556`** (23,681,536 B; link 400 s, repeatability check OK; 512 B larger than the batch-4 pin's
`2B56A1CD`, the interface-version export and the fix). The define-off build of `perf/speed-danger-b5` **`00467893...D1FF`**
against it: **IDENTICAL-EXCEPT-BUILD-ID** (only the PE time stamp, the two debug-directory time stamps and the PDB GUID
differ; `defineoff.txt`).

**Builds** `[C]`: on (release, `kitbuild.py`, `DANGER` + `DANGER_DG7`) **`524EB854...9561`** (1094 s on a busy
workstation); verify (fast, both `_VERIFY` too) **`C9856B13...D718`**; x86 release of `i0-ship-batch5` (4813's `rel-i0`
recipe, version `Release-5.4.8-50-g5d18d3d6c Copy`) **`95FBCBA6...0D97`** (17,152,512 B; its bytes differ from 4813's
`690ce2b5` of the same tree by the version string, which names the commit). Layouts: `layout-ref` and `layout-on` = 8.6's
`layout-fix` bin (server **`fc73e60e`**, the batch-4 pin's) with `72693B8E` or `524EB854` (`layout-*-SHA256.txt`). The
batch-4 server ran the batch-5 DLLs in every arm below without a fault: the x64-ifver export is an addition it does not
read `[L]`.

**Identity M/H/E** `[L, 2026-11-18 23:27-23:34 EST, headless]`: `idjob.py` on `524EB854` against `72693B8E` in
`layout-ref`: **M 150..160 IDENTICAL (15 tables, 32,815 rows), H 300..305 IDENTICAL (14, 125,209), E 0..50 IDENTICAL
(15, 20,619); `idjob: IDENTICAL`** (`id-on.txt`). The row counts equal 8.6's on the fixed stock.

**Verify LONGM** `[L, 23:33-23:54 EST, headless]`: `verifyrun.py` (verify `C9856B13`, `layout-ref`, M 150 -> 301,
compared 150..300): **PASS: DANGER 146,263 checks, DANGER_DG7 287,526, 0 differences, 0 spoils, no
`VP64SpeedDiffs.csv`, game IDENTICAL (15 tables, 1,033,788 rows)** (`verifyrun.txt`, `vr-VP64SpeedTotals.csv`,
`vr-VP64DangerStats.csv`). The same counts as 8.6's LONGM on the fixed stock, as a tree-equal game should give.

**D24** `[L, 2026-11-19 00:07-00:35 EST, headless, wall 1724 s]`: `run_d10.py` with x86 `95FBCBA6` (`i0-ship-batch5`)
and x64 `524EB854` in `layout-on` (server `fc73e60e`), order exception none, D18 on both: **PASS, all three parts on
tiny, standard and huge** (`d24-report.txt`; walls 555, 922, 1706 s on the shared workstation).

**Narrowing gate** `[C, 00:36-00:42 EST]`: `narrow.sh` (fast compile-only, every DANGER and DG7 define incl. the
statistics, spoil and verify ones): **794 unique `-Wshorten-64-to-32` sites, baseline 798, 0 new**, 4 gone (`narrow.txt`).

**Branch heads re-read at the end** (00:42, `git ls-remote`): `x64-batch5` `1fd51353e`, `i0-ship-batch5` `5d18d3d6c`,
`perf/speed-danger-b5` `5d4a90aa5`, unchanged since the start. `deps/dll.lock` on main still names batch 4.

**Verdict**: on pin batch 5 as 0551 pushed it, the package (`perf/speed-danger-b5` `5d4a90aa5`) passes every
correctness item the MC set for 0.1b's default-on (#6453): define off IDENTICAL-EXCEPT-BUILD-ID against the batch-5
release, idjob M/H/E IDENTICAL, D24 PASS on all three parts, verify LONGM 0 differences (146,263 + 287,526 checks), the
narrowing gate 0 new; with 8.9's clean client soak (batch 4 + the same finder fix, same package patch-id).

**Not verified here**: the reference is our own build of `x64-batch5` with the batch-4 pin's version string and epoch,
not 0551's released DLL (none was published when this ran): when 0551 lays out batch 5 and moves `deps/dll.lock`, run
`defineoff.py` once against the lock (expect IDENTICAL-EXCEPT-BUILD-ID, the same commit tree) and, if the layout's
server differs from `fc73e60e`, idjob M once in the new layout; the client soak on batch 5 itself (8.9 ran batch 4 + the
fix, the same game code and package); the coverage windows (CONG/CORP, 8.6) and E/H verify on batch 5; timing on batch 5
(the oldpc pairs of 8.6/8.7 were on the fixed stock).

### 8.11 Grade-A timing on pin batch 5, plain release arms (lead-0504, 2026-11-19)

`[L, 2026-11-19 00:58:52-01:35:02 EST, oldpc, quiet: alone, undisturbed, foreign cores 0.07; NVIDIA GeForce RTX 2080,
driver 32.0.16.1088 of 2026-07-21 (D175)]`. `perf/speedkit/pair.py --a b5=<x64-batch5 release 72693B8E> --b
danger=<perf/speed-danger-b5 on release 524EB854> --scenario H --runs 6 --turns 11 --issue 4807` (job
`20261119-005757-7e47`; both arms in the batch-4 pin layout's bin, server `fc73e60e`; AI logging off; interleaved rounds,
order rotated; `perf/tbjobs/analyze.py <out> --ref b5`). Both DLLs are 8.10's. Evidence
`perf/evidence/0504/4807/b5/oldpc-7e47/` (job manifest and process lists left out).

| Measure | x64-batch5 (6 runs) | + danger package on (6 runs) | difference |
|---|---|---|---|
| s/turn, H 301-311 | 14.862 (sd 0.066) | 14.052 (sd 0.265) | **-0.810 s (-5.45 %)**, 95 % CI [-1.096, -0.524], significant (detectable about 0.31) |
| private MB after 10 turns (D151), mean of 6 | 325.4 | 331.9 | +6.5 MB |
| peak working set | 259 MB | 267 MB | +8 MB |
| stats.db, round 1 | | | **IDENTICAL** (14 tables, 270,241 rows) |

Grade A by `docs/briefs/PERFORMANCE.md` rule 7 (one interleaved series, one machine, the same scenario, save and
logging, six runs per arm, quiet lock held, the same game). Why the numbers moved: both arms play the same game, so the
-0.81 s is the reach searches the package does not run (H about 83 % hit rate, 8.6) and DG7's kept tile lists; it agrees
with the batch-4 + fix D178 pair of 8.6 (-0.880 s, -5.97 %, n = 3). Memory: +6.5 MB, the package's known tables at H
(stamp arrays 3.25 MB, reach entries 2.1 MB, DG7 lists about 1.1 MB; 8.6, 8.7). The PGO arms on batch 5: 8.12 (-5.08 %).

**M, the same arms** `[L, 2026-11-19 03:48:20-03:52:02 EST, oldpc, quiet; same GPU and driver]`: job
`20261119-013816-bd86` (`pair.py ... --scenario M --runs 6 --turns 15`; evidence `perf/evidence/0504/4807/b5/oldpc-bd86/`):
M 151-165, 0.993 s/turn (sd 0.004) against 0.981 (sd 0.003): **-0.012 s (-1.20 %)**, 95 % CI [-0.016, -0.008],
significant; stats.db IDENTICAL (15 tables, 52,706 rows); private after 10 turns 214.3 against 215.0 MB (+0.7 MB). M's
turns are short and few of its searches are reach searches (M hit rate about 69 %, 8.6), so the gain is small in
seconds; grade A as H.

### 8.12 PGO arms on pin batch 5, each arm its own profile (slice "pgo5", impl-4807-pgo5, 2026-11-19)

**Method** (8.7's, on batch 5; script `perf/evidence/0504/4807/pgo5/pgo5_arms.sh`, scratch `C:/c5-64bit-work/4807-pgo5`):
arm **a** = `x64-batch5` `1fd51353e` (the tree of the b5 reference `72693B8E`, no define, describe
`Release-5.4.8-65-g1fd51353e`); arm **b** = `perf/speed-danger-b5` `5d4a90aa5` + `VP64_SPEED_DANGER` +
`VP64_SPEED_DANGER_DG7`. Per arm: `pgo-instr` build (`build_as_pin.py`, stubs of 8.10's reference build; whole-program
link, the one link place), training with `tools/x64-dll/pgo/train.py --sets H,M,S` (4801's set: H 300-305, M 150-160,
the Standard fixed-seed game 0-50; headless; in a copy of 8.10's `layout-ref` bin, server `fc73e60e`), then `release
--profile <the arm's own profile>`; lock records `pgo-a.lock`, `pgo-b.lock` (profiles not committed, MC #4429 (2)).
Then one oldpc D178 pair: `perf/speedkit/pair.py --a b5pgo=<arm a> --b dangerpgo=<arm b> --scenario H --runs 6 --turns
11 --issue 4807` (quiet; both arms in the batch-4 pin layout's bin, server `fc73e60e`), analysed with
`perf/tbjobs/analyze.py <out> --ref b5pgo`.

**Expected** (written before the job ran): about -0.7 s/turn (-5.4 %) on H 301-311, the PGO pair of 8.7 on batch 4 +
the same finder fix (12.843 -> 12.150 s, n = 3), and stats.db IDENTICAL across the arms (the same game as 8.11's plain
pair, 14 tables, 270,241 rows); private after 10 turns about +6.5 MB (the package's tables). Batch 5 differs from 8.7's
base only by x64-ifver (an export), so a result far from these would point at the profiles or the host.

**Results** `[L, 2026-11-19 03:52:04-04:23:36 EST, oldpc, quiet; NVIDIA GeForce RTX 2080, driver 32.0.16.1088 of
2026-07-21 (D175)]` (job `20261119-025730-4f6b`, filled by lead-0504; evidence `perf/evidence/0504/4807/pgo5/oldpc-4f6b/`):

| Measure | a: x64-batch5 + own profile `9D40E9EB` (6 runs) | b: + package, own profile `A8FEE5CE` (6 runs) | difference |
|---|---|---|---|
| s/turn, H 301-311 | 12.917 (sd 0.030) | 12.260 (sd 0.079) | **-0.657 s (-5.08 %)**, 95 % CI [-0.741, -0.572], significant (detectable about 0.10) |
| private MB after 10 turns, mean of 6 | 324.6 | 330.7 | +6.1 MB |
| peak working set | 260 MB | 268 MB | +8 MB |
| stats.db, round 1 | | | **IDENTICAL** (14 tables, 270,241 rows) |

As expected: the same game, the package's gain under PGO (-5.08 %) is close to 8.7's -5.40 % on batch 4 + fix and a
little below the plain arms' -5.45 % (8.11), as in 8.7 (PGO speeds the base's search code too, so the skipped searches
are worth slightly less); memory = the package's tables. Grade A by the same rule as 8.11 (both arms PGO-built, each
with its own profile). The PGO base itself is 13.1 % faster than the plain base (12.917 vs 14.862 s, two jobs of the
same night on the same host: an indication, not a same-series comparison).

### 8.13 How the package reaches the 0.1b DLL: the marker defines (agreed 2026-11-19 with 0574, thread 0574 #6812/#6817)

The 0.1b x64 DLL (ticket 0574, the owner's D195) is composed as x64-0.1b = x64-batch5 + this package's 12 commits
(`perf/speed-danger-b5` 5d4a90aa5) + 0573's hidden-embark commit, and gets the package's defines byte-stably the way
0573's does: `tools/x64-dll/build.py` adds exactly `/DVP64_SPEED_DANGER /DVP64_SPEED_DANGER_DG7` (in that order, after
`/DVP64_HIDDEN_EMBARK` when both markers are present) to cl_args **only when the source has
`CvGameCoreDLL_Expansion2/VP64SpeedDanger.h`** (the package's own file, absent from every pin, so pin hashes do not move);
never the `_VERIFY`, `_STATS` or `_SPOIL*` defines. `build-summary.json` records them under `marker_defines`.
**Consequence for anyone building the package's source**: from that build.py change on, every build of a branch carrying
the package has the defines on unless `VP64_MARKER_DEFINES=0` is set in the environment (recorded as
`marker_defines_opt_out: true`). Define-off builds (`perf/speedkit/defineoff.py`, `build_as_pin.py`) and any kitbuild
arm that wants a subset of the defines must set it. The package has no i0-side commits. (Implementation: 0574, on top
of 0573's build.py line; state on 2026-11-19 03:30: agreed, not yet on main.)

### 8.14 The package on pin batch 5 in the 0.1b form: the fixed-seed game 0-300 and the client soak (impl-4809 round 4, items B and D, 2026-11-19)

**The form** `[C, lead-0504 2026-11-19 19:05]`: fork `perf/speed-danger-b5` is now **`a70ebe15c`** = `5d4a90aa5` (8.10)
with its 66 `#line N` lines removed and nothing else; x64-0.1b `1b87dd185` = `a70ebe15c` + exactly the hidden-embark
change (ticket 0574's line-for-line diff). Without `#line` a define-off build is no longer the pin's bytes (every later
`__LINE__` moves), which 0.1b does not need: it ships the defines on. The two marker defines 0.1b ships are
`VP64_SPEED_DANGER` and `VP64_SPEED_DANGER_DG7` (8.13).

**Method** (scripts `perf/evidence/0504/4807/g300-b5/`: `run_g300b5.sh` steps `on verify narrow g300`, `g300b5.py`;
scratch `C:/c5-64bit-work/4807-b5/r4-*`, headless): from `a70ebe15c` (fetched into 4807 b5's fork clone as
`gh/perf/speed-danger-b5`) with the kit's `kitbuild.py` and 8.10's reference stubs (`ref/build/stubs`, read only): **on**,
release, `DANGER` + `DANGER_DG7`; **verify**, fast, the same plus both `_VERIFY`. The narrowing gate (`build.py
--shorten`, keyed by physical lines since ticket 4810, so it passes with or without `#line`): a fast compile-only build
with every DANGER and DG7 define (statistics, verify and spoil ones too). Then 8.8's game, unchanged code
(`g300/g300.py`, imported; only its three DLLs and the layout replaced): `server/specs/continents-standard-fixed-seed.json`
to turn 301 in three arms, each in its own copy of 8.10's `layout-ref` bin (batch-5 reference `72693B8E`, server
`fc73e60e`; the lock's batch-5 layout was not laid out): ref = `72693B8E`, verify, on; statscmp 0..300 against ref.

*Expected, written before the runs* `[I]`: verify 0 differences in both packages with checks of 8.8's size (DANGER
about 129,000, DG7 about 254,000: batch 5 is 8.8's base, batch 4 + the finder fix, plus an export, so very likely the
same game, 971,505 rows, and the same counts); verify and on IDENTICAL to ref over 0..300; the narrowing gate 794
sites, baseline 798, 0 new, as 8.10.

**Builds** `[C, 2026-11-19 19:51-20:02 EST]`: on (release) **`1318CC29...4CFC`** (kitbuild 452 s, repeatability check
OK); verify (fast) **`01C08FB4...9EF8`** (`on-SHA256.txt`, `verify-SHA256.txt`). **Narrowing gate** (20:02-20:05): **794
unique sites, baseline 798, 0 new**, 4 gone (`narrowing.txt`): the gate passes on the form without `#line`, as ticket
4810's physical-line keys intend.

**The game** `[L, 2026-11-19 20:05:06-20:27:13 EST, workstation (shared): functional, no timing]`:

| Arm | wall (s) | vs ref `72693B8E`, turns 0..300 | DANGER checks / differences | DANGER_DG7 checks / differences |
|---|---|---|---|---|
| ref `72693B8E` | 399 | (reference) | | |
| verify `01C08FB4` | 518 | **IDENTICAL** (15 tables, 971,505 rows) | 129,411 / **0** | 253,976 / **0** |
| on `1318CC29` | 385 | **IDENTICAL** (15 tables, 971,505 rows) | | |

(`g300.txt`, `VP64SpeedTotals.csv`, `statscmp-verify.txt`, `statscmp-on.txt`; spoils 0, no `VP64SpeedDiffs.csv`; the
totals' last turn is 299 as in 8.8.) The rows and both checks counts equal 8.8's exactly: batch 5 plays 8.8's batch 4 +
fix game, as expected.

**Verdict of item B**: `perf/speed-danger-b5` `a70ebe15c` (the 0.1b form, x64-0.1b less the hidden-embark change) is
verify-clean over a whole fixed-seed 300-turn game from turn 0 (382,387 checks, 0 differences) and plays the batch-5
reference game in `on` mode with the two defines 0.1b ships; the narrowing gate passes. Not verified here: the hidden-embark
change itself (0574's), the x64-0.1b tree as built by 0574, the lock's own batch-5 layout (not laid out; server
`fc73e60e` ran every arm). The client soak: below.

**The client soak on batch 5 in the 0.1b form (item D)** `[L, 2026-11-19 20:28:31-20:43:49 EST, workstation (shared),
c5h-a (C:, a free Goldberg pool instance; never c5h-b, c5h-c, c5h-d), no-focus launch, dialog watcher: no box]`: 8.9's
method unchanged (`soak/soak.py`, renamed copy of 0438's M save `98fc454a`, AI leader screens refused by `diplo.py`, city,
military, trade and economic screens with 8 hovers each during AI turns), one run of 30 forced human turns, script
`perf/evidence/0504/4807/soak-b5/soakrun_b5.sh` (the layout: a copy of 4807 b5's `layout-ref` bin with the **verify build
`01C08FB4`** of `a70ebe15c`, server `fc73e60e`, and the batch-4 pin's proxy `3dfd7521`, the pair 8.9 ran; installed with
`provision.py set-proxy`, returned with `--original` rc 0, `verify --quick` rc 0; `layout-SHA256.txt`). Binaries read back
from the instance (`soak.json`): EXE `04cfdc71`, proxy `3dfd7521`, DLL `01c08fb4`, server `fc73e60e`. Evidence
`perf/evidence/0504/4807/soak-b5/run1/`.

| | run 1 (20:28-20:44) |
|---|---|
| turns played (forced human ends) | 30 (150 -> 180), load 106.5 s |
| AI turn wall, end turn -> human turn again (screens running) | 19.8-30.3 s, mean 25.1 |
| screen cycles started during AI turns | 60 (15 each) |
| screens open during AI turns | 59 (city 15, military 15, trade 14, economic 15; military and economic by the popup event after the key) |
| ... of which still not the human's turn when closed | 30 (city 8, military 8, economic 8, trade 6) |
| hovers on open screens | 472 |
| `VP64SpeedTotals.csv` DANGER checks / differences / spoils | **20,871 / 0 / 0** (to turn 178) |
| DANGER_DG7 checks / differences / spoils | **41,520 / 0 / 0** |
| `VP64SpeedDiffs.csv` | none |
| `VP64DangerStats.csv` lookups / hits, all verified (turns 151-179) | 29,522 / 21,624; DG7 21,512 hits verified |
| `unc_thread`, `unc_lua`, `notstored_gen` | 0, 0, 0 |
| end | turn 180 reached, harness stop rc 0; EXE exit 15 (the stop's, "not a crash code"), no minidump, no box |

**Verdict of item D**: the 0.1b form's verify build passes 8.9's client soak on pin batch 5: 30 AI turns with 59 screens
opened during AI processing and 472 hovers, 62,391 verified checks, 0 differences, no crash, box or hang; the counts are
8.9's run 2's within a few (that run was batch 4 + the same finder fix, the same save and turn count). As in 8.9,
`unc_thread` = 0: no ui-thread call reached the cache. **Not verified here**: the release `on` build `1318CC29` in a
client; a second run or an H save; whether a ui-thread call ran at a raise inside an AI computation (8.9's open point);
the batch-5 server `A6026D53` and proxy `5BAB1B6C` of 0551's layout (the batch-4 server and proxy ran the batch-5 DLL, as
in every headless arm here); x64-0.1b's own tree (with hidden embarks).

## 9. Danger rebuilds equal to the last one: a census before any victory (ticket 5040, 2026-11-24)

impl-5040 (account A), 2026-11-24. The Architect's draft T10 (`architect-2026-11-21-full-exe-optimizations.md` 3.4 and 4)
for the third speedup list's VU1 and VU2, with VU3, VU5 and VU6 counted beside them. The list's sizes for VU1 and VU2 were
taken on turns right after a victory and a war wave (its own caution); this section counts the same things on our own
saves, before any victory, at the current x64 pin (1b87dd185, `x64-0.1b`, with the 0.1b marker defines: the danger
package of sections 1-8 is ON, so every rebuild below already uses its reach cache and DG7). **Measurement only; no cut
is built.** Marks: `[C]` read in the code at the pin, `[L]` seen in a run, `[I]` inferred.

### 9.1 What a rebuild is at the pin `[C]`

- `CvDangerPlots::UpdateDanger` rebuilds a player's whole map (every plot reset, every visible enemy unit and city
  assigned again, fog, citadels and plot damage). Its own three cases: reload (`m_iTurnBuilt == -1`, after a load),
  turn change (first build in a new game turn) and war change (another build in the same turn).
- It is started explicitly (`CvPlayer::UpdateDangerPlots`, once per player turn in `CvPlayer::doTurnPostDiplomacy`,
  after that player's diplomacy pass, `CvPlayer.cpp` near 10346;
  `CvPlayer::gameStartInit` after a load) or lazily: the six `CvPlayer` readers (`GetPlotDanger` x3,
  `GetPossibleAttackers`, `IsKnownAttacker`, `AddKnownAttacker`) rebuild first when the map is dirty.
- Only two writers set it dirty: `CvTeam` at a declaration of war (every major on the declaring team,
  `CvTeam.cpp` near 2067) and `CvPlayerManager` when the war state changed (every living player, `CvPlayerManager.cpp`
  near 37). `CvPlayer::IsVanishedUnit` reads the vanished set without a dirty check (not counted).

### 9.2 The counting build

Scratch fork clone `C:/c5-64bit-work/5040/fork` (`git clone --shared` of the local fork clone), local branch
`count-5040` = the pin 1b87dd185 + one commit `91df50e7d` (never pushed, never merged): `VP64Census5040.h/.inl` and
hooks behind `VP64_COUNT_5040`, read-only (no game state written):

- around `UpdateDanger`: the state before (every plot's flat damage, citadel damage, fog count, unit, city and capture
  lists in order, the known and vanished unit sets) is encoded, the clock (QPC) runs only around the rebuild itself,
  then the result is encoded. **eq_live** = the result equals the state it replaced (full compare: what a skipped
  rebuild would have kept), **eq_prev** = it equals the previous build's result (64-bit FNV hash; differs from eq_live
  only when `AddKnownAttacker` added to the map in between);
- the trigger: `explicit`, or the kind of read that found the map dirty;
- the reads each build served until the map went dirty again or was rebuilt: count, kinds, distinct plots, and
  whether a city read was the plain one (no pretend garrison, no damage list): VU2's "read once for one plot";
- VU3: `ScorePlotBuild` in `CvHomelandAI::ExecuteWorkerMoves`' feature-change branch (calls, time, result equal to the
  kept score); VU5: the two `CvCity::updateYield` calls after a garrison change in `CvUnit::setXY`; VU6: the two
  `CvPlot::changeAdjacentSight` calls in `CvUnit::setXY` (old plot, new plot; inclusive time);
- the wall time between two game turn starts (`CvGame` after `incrementGameTurn`): a turn's share.

Output in the server's run folder: `VP64Census5040Rebuilds.csv` (a row per build, written when its read period ends) and
`VP64Census5040Turns.csv` (a row per turn). Build: `bash perf/evidence/5040/kb.sh <name> [--config release]`
(perf/speedkit/kitbuild.py with the three marker defines plus `VP64_COUNT_5040`). Runs: `bash perf/evidence/5040/id.sh
<build> <job>` (perf/speedkit/idjob.py, windows M = TS0438-M-T150 played to 161 and H = TS0438-H-T300 played to 306,
against the pin's release DLL in the layout `F:/c5-64bit-work/5040/server-build`, made by `server/build_server.py` from
the shared pin folder `C:/c5-64bit-work/keep/pins-0.1b`), then `perf/evidence/5040/census.py` over the candidate's run
folders. Whole turns only: M 151-160, H 301-305 (the load's turn is reported apart).

### 9.3 The counting build plays the pin's game `[L, 2026-11-24 16:09-16:15 EST, workstation, shared, headless]`

`bash perf/evidence/5040/id.sh cnt-fast id-fast`: the fast counting DLL `7DE2AAF9...` (fork `91df50e7d`, defines
`VP64_HIDDEN_EMBARK VP64_SPEED_DANGER VP64_SPEED_DANGER_DG7 VP64_COUNT_5040`) against the pin's release DLL `b84aa3e9...`
in the layout `F:/c5-64bit-work/5040/server-build`: **M 150..160 IDENTICAL (15 tables, 32,815 rows), H 300..305
IDENTICAL (14 tables, 125,209 rows)**, the row counts of 4808's 0.1b runs. So the counters change no game result and
every count below is the pin's game. Evidence `perf/evidence/5040/id-fast/`.

The release counting DLL `E0823090...` (`kb.sh cnt-rel --config release`, 715 s with the LTO link) is IDENTICAL as well
(M 32,815 rows, H 125,209 rows; `perf/evidence/5040/id-rel/`, 16:29-16:33 EST), and its counts equal the fast build's
exactly (the same rebuilds, reads, calls: the game is deterministic); only the times differ.

### 9.4 Results `[L, 2026-11-24, workstation, shared (11-14 heavy jobs of other teams registered: indications, not a timing result, COMMON 23), headless, release counting DLL E0823090..., pin layout server-build, AI logging off]`

Longer runs without a reference (`long.sh cnt-rel ...`; the identity job above is the proof that these are the pin's
games): M 150 -> 171 (whole turns 151-170) and H 300 -> 331 (whole turns 301-330). No victory in either (M: all 8
majors alive at the end; H: 11 of 12, Byzantium already dead at 306 in the identity run; no victory line in the logs). Per whole turn, means; "share" = of the turn's wall time in the same
run (the turn includes the counters' own cost, about 1.1 M danger reads a turn through one lock on H: a few ms):

| per whole turn | M 151-170 (20 turns) | H 301-330 (30 turns) |
|---|---|---|
| turn wall time (0.3 % of it) | 1,567 ms (4.7 ms) | 20,970 ms (62.9 ms) |
| danger rebuilds, all | 28.8, 31.1 ms, 1.99 % | 80.0, 1,219.6 ms, 5.82 % |
| ... turn change (explicit, one per player turn) | 25.0, 27.4 ms | 30.5, 589.4 ms |
| ... war change (another build in the same turn: a dirty map read) | 3.8, 3.8 ms | 49.5, 630.2 ms |
| ... started by a plain city danger test | 3.0, 2.7 ms | 44.7, 401.3 ms |
| **VU1: rebuilds equal to the state they replaced** | **2.4, 1.06 ms, 0.067 %** | **8.5, 50.0 ms, 0.239 %** |
| ... VU1 by turn: median share; turns above 0.3 %; largest | 0.000 %; 3 of 20; T156 9.8 ms (0.66 %, 42 war-change rebuilds) | 0.001 %; 4 of 30; T314 605.7 ms (2.85 %, 241 war-change rebuilds), T315 429.9 ms (1.78 %) |
| VU1 equal to the previous build's result (hash) | the same 2.4 | the same 8.5 |
| **VU2: lazy builds read once for one plot** | **1.0 (20 of 76), 0.35 ms, 0.022 %** | **3.7 (111 of 1,485), 22.0 ms, 0.105 %** |
| ... of them the read was a plain city test | 20 of 20 | 110 of 111 |
| VU2 wider: lazy builds whose every read was a plain city test (any count, any cities) | 2.5 (50 of 76), 2.06 ms, 0.131 % | 37.9 (1,137 of 1,485), 315.2 ms, **1.503 %** |
| explicit builds never read before the next build | 0 of 475 | 0 of 884 |
| danger reads; `SetDirty` calls (on a clean map) | 188,032; 111.0 (8.1) | 1,123,288; 1,107.2 (58.2) |
| **VU3: worker feature re-score** (`ScorePlotBuild` in the feature branch) | 55.7 calls, 1.57 ms, 0.100 %; equal to the kept score 35.9 calls, 0.94 ms, 0.060 % | 877.1 calls, 16.3 ms, 0.078 %; equal 613.6 calls, 9.5 ms, 0.045 % |
| **VU5: `updateYield` after a garrison change in `setXY`** | 31.9 calls, 3.14 ms, 0.200 % | 113.2 calls, 18.4 ms, 0.088 % |
| **VU6: `changeAdjacentSight` in `setXY`** (old plot + new plot, inclusive) | 724 + 730 calls, 3.8 + 11.1 ms, **0.946 %** | 3,070 + 3,078 calls, 14.8 + 48.3 ms, **0.301 %** |
| the load's turn (apart): reload rebuilds | 38, 37.7 ms | 41, 662.4 ms |

Evidence: `perf/evidence/5040/long-rel/` (M 151-170 and H 301-315, 16:32-16:41 EST) and `perf/evidence/5040/long-h30/`
(H 301-330, 16:42-16:53 EST): each run's `VP64Census5040Turns.csv`, `VP64Census5040Rebuilds.csv`, `turn-times.tsv`,
`census.txt`/`census.json`, the machine load at the start (`machine-load.txt`, from `quietlock.py check`). The 15-turn H run alone gave VU1
0.401 % (the T314-315 cascade is a larger part of 15 turns than of 30); the 5-turn windows of the identity jobs gave
M 0.168 % / H 0.215 % (release) and M 0.105 % / H 0.246 % (fast), all below 0.3 %.

Two counts that bear on the list's own description:

- **The cascade exists before any victory.** At H T314 a chain of war and peace changes dirtied every map about ten
  times: 26 players rebuilt about ten times each, 241 war-change rebuilds, 121 of them equal to the state they replaced;
  118 of those 121 served only plain city tests. T315 continues it (134, 64 equal). Outside such turns VU1 is near zero
  (median 0.001 % of an H turn). The list's post-victory turns (L290 T293-T297) are this case.
- **The equal rebuilds are nearly all inside VU2's wider form.** Of H's 30 turns, 199 of the 1,137 city-test-only lazy
  builds were equal; in the cascade 118 of 121 equal rebuilds were city-test-only. A per-city answer while the map stays
  dirty would remove those builds whether or not they are equal, without a closed input set.

### 9.5 Where the dirty rebuilds come from on H `[C, L]`

- A declaration of war (`CvTeam::declareWar`) and every war-or-peace change (`CvTeam::DoNowAtWarOrPeace`) call
  `CvPlayerManager::Refresh(true)`, which sets **every living player's** map dirty, involved in the war or not. On the
  H save (22 majors, 41 city-states, 140 x 88) that is 1,107 `SetDirty` calls per turn, 58 of them on a clean map
  (turns 301-330); on M 111 and 8.1 (151-170).
- The next read of each dirtied map rebuilds it. On H most of these first reads are a city's plain danger test
  (`CvPlayer::GetPlotDanger(pCity)` with the default arguments, no pretend garrison and no damage list; its callers at
  the pin: `CvCity::IsInDanger`, `IsInDangerFromPlayers`, `isInDangerOfFalling`, `CvEconomicAI.cpp` near 826 and
  `CvPlayer::HasCityInDanger`; which of them reads first was not counted), 44.7 of the 49.6 lazy rebuilds per turn.
- Most dirty rebuilds are **not** equal to the state they replace (H: 8.3 of 49.5 per turn are): between two builds
  the other players have moved their units, so the war-change rebuild also refreshes a map that was stale. A rebuild
  can be skipped exactly only when it would be equal (VU1's rule); that is the eq_live count.
- **VU2's wider form.** The list's VU2 counts builds read once for one plot. Here many dirty builds serve several reads,
  but every one of them a plain city test (several cities, or the same city again) before the map goes dirty again:
  on H 1,137 of the 1,485 closed lazy builds of turns 301-330, 315 ms per turn, 1.50 % of the turn; on M 50 of 76,
  2.1 ms, 0.13 %. Answering those tests per city without the full build is VU2's cut widened from "one read" to "only
  plain city reads"; it needs the same two proofs the list names (the per-plot answer equals the map's, and the changed
  search sequence of the shared path finder: here the reach cache of 4807 answers most unit reaches without a search,
  which may make the second proof smaller) `[I]`. What a per-city answer must compute (every visible enemy unit's attack
  reach to that one plot, city bombardment, fog and citadel terms) was not read in this ticket.

### 9.6 Verdict: go or no-go on closing the input set (VU1)

**No-go** at the value floor (0.3 % of an H or M turn; `architect-2026-11-21-full-exe-optimizations.md` section 4):

- The most an exact skip of equal rebuilds could save, before the cost of the check that proves "nothing changed", is
  **0.239 % of an H turn** (30 turns, 50.0 ms of 20,970) and **0.067 % of an M turn** (20 turns, 1.06 ms of 1,567),
  both below the floor. The shorter windows agree (0.105-0.246 %) except the 15-turn H stretch 301-315 (0.401 %),
  which holds the one war cascade of the 30 turns in half the turns.
- The gain is not spread: the median H turn has 0.001 %, and 2 of 30 turns (T314-T315, a war cascade) hold 1,036 ms of
  the 1,501 ms that 30 turns give. A cut that pays only in cascade turns is the post-victory case the list measured; on
  our saves before any victory it occurs about once in 30 H turns.
- Closing the input set is the expensive part (every input of a full rebuild: unit positions and states of every
  visible enemy, war state and VP's ignore rules, visibility with delayed visibility (gap 13), citadels, feature and
  terrain damage, deep-water damage, known and vanished units), and a mistake changes AI decisions silently.

**What the census shows instead, for the MC to weigh (no ticket opened here):**

1. **VU2's wider form** (answer plain city danger tests per city while the map stays dirty) is the one item above the
   floor on H: up to **1.50 %** of an H turn (315 ms), 0.13 % of M, and it contains nearly all of VU1's cascade gain
   (118 of the 121 equal rebuilds of T314 served only city tests). It does not need a closed input set, but the two
   proofs the list names (equal answers; the path finder's search sequence) and a reading of what the per-city answer
   computes. A bound before that cost, like VU1's.
2. **VU6** (`setXY`'s two sight updates): **0.95 %** of an M turn and 0.30 % of an H turn, inclusive time. The list's
   missing proof (re-show, city meetings, trade-cache invalidation hang on it) is unchanged; touching only the
   difference would save only part of this.
3. VU3 (0.08-0.10 %; the part equal to the kept score 0.045-0.060 %) and VU5 (0.09-0.20 %) are below the floor.
4. Rebuilds in all cost 2.0 % of an M turn and 5.8 % of an H turn at the pin with the danger package on; the explicit
   per-player-turn build is never wasted (0 of 1,359 unread before the next build).

### 9.7 Not verified

- Times are from the shared workstation (11-14 other heavy jobs registered at the starts); they are indications. The
  shares compare two times taken in the same run, which load moves less than either, but no quiet or testbed run was
  made (the ticket asked for counts with an instrumented ms figure). Counts are exact and equal between the fast and
  the release build.
- The turn's wall time includes the counters' cost (the full encoding of a map before and after each rebuild sits
  outside the rebuild's clock but inside the turn: about 80 x 2 encodings of 12,320 plots per H turn), so the shares are
  slightly low.
- One M and one H game, headless, AI only, no human seat; other saves (more city-states, other map types, a game with
  a human at war) were not counted. The cascade frequency (one in 30 H turns) is one sample.
- `CvPlayer::IsVanishedUnit` reads the vanished set without a dirty check and is not counted as a read.
- A rebuild's equality covers the map, the known and the vanished sets; it does not cover the path finder's leftover
  node data that a skipped rebuild would leave different (section 4's second consequence): an exact skip would have to
  prove that too.
- No cut was built; the bounds assume a free check.
