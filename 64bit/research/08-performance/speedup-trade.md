# Speedups, the trade package (ticket 4808): design note (part one, sections 1-3) and the product code with its results (part two, section 4)

Summary (2026-11-23): fork `perf/speed-trade` `42ca322ec` (from the x64 pin `1b87dd185`) carries TR3 + TR4 + TR7 + TR8
behind `VP64_SPEED_TRADE` (off in every product build). Define off: byte-identical to the pin's DLL except the build ids.
Define on: identical games everywhere tested (M, H, E, D24, a 0-300 game, the 43-civ variant, the stress map, windowed
S4, a client soak), 0 false reuses in verify mode; on oldpc -1.37 s per H turn (-9.87 %; PGO arms -10.32 %), memory
+8 MB after 10 turns (the prototype: +35.5 MB). Grade B timing only (section 4.8).

Written by impl-4809 (round 4, item C, for lead-0504 and the MC), 2026-11-19 (committed 19:31 EST). No product code: this is the
design answer the ticket's acceptance asks for before any code (ticket `tracker/issues/4808-speedups-trade-package.md`).
Inputs: 0437's prototype (a), fork branch `x64-proto-0437` **`5a6c1a25d`** (b3, `VP64_PROTO_TRADEPATH`,
`VP64ProtoTradePath.inl`; read, not merged), its hand-over `perf/evidence/0437/4680/handover-a-trade-path-search-reuse.md`
(below "the hand-over"), 0437's run records `perf/evidence/0437/4673/runs/k-*` (build b3), the review rows TR3, TR4, TR7,
TR8 (`external-port-speedups-review.md`), the Architect's review section 2
(`research/07-architecture-decisions/architect-2026-11-17-speedup-epic-review.md`). Line numbers are the x64 pin batch 5
**`1fd51353e`** unless said otherwise. Marks: `[C]` read in the code or computed from recorded data by this note's script,
`[L]` measured in a run, `[I]` inferred.

## 1. The memory

### 1.1 What the store holds and how the prototype counts it `[C, VP64ProtoTradePath.inl at 5a6c1a25d]`

One entry per (player, origin city plot, path type water or land) in a `std::unordered_map`. An entry holds the search's
fingerprint (two 64-bit values), the touched list (`std::vector<std::pair<int, unsigned long long>>`: plot index and its
64-bit property hash, 16 B per plot), in verify mode a diagnostics vector (4 x 32-bit group hashes per touched plot), and
the result exactly as `GetMultiplePaths` returned it (`std::map<int, SPath>`: per destination city a map node and an
`SPath` of 120 B on x64 with its own `vPlots` heap buffer of 10 B per path node; `SPath` carries `sConfig`, an 80-B copy
of `SPathFinderUserData` with an empty vector). The prototype's own count (`VP64PTEntryBytes`):
`sizeof(VP64PTEntry) + 32` per entry (88 + 32 B), `16 x touched.capacity()`, `4 x diag.capacity()` and per path
`48 + sizeof(SPath) + 10 x vPlots.capacity()`. It writes the store's entries and bytes after every rebuild
(`ProtoTradePath.csv`).

### 1.2 The +35.5 MB on H broken down by part `[C: computed from 0437's recorded b3 runs; the runs are L]`

0437 recorded a reuse run and a verify run of b3 on each window (`perf/evidence/0437/4673/runs/k-h-reuse`, `k-h-verify`,
`k-m-reuse`, `k-m-verify`; both IDENTICAL to the references, so the same game and the same searches). A verify run stores
every search again with `diag` = 4 x touched, so at the same row of the two CSVs the difference of `store_bytes` is
16 B x the touched plots of every stored entry. `perf/evidence/0504/4808/store_parts.py` (from `git show` of the CSVs,
no new run) gives:

| | H 300-307 (Huge 128 x 80 = 10,240 plots, 12 majors) | M 150-162 (Standard 80 x 52 = 4,160 plots, 8 majors) |
|---|---|---|
| first rebuild of each player (every search new): entries, counted bytes | 308 entries of 11 players, 23.13 MiB | 98 entries of 8 players, 0.50 MiB |
| of it: **touched lists with hashes** | **14.73 MiB (63.7 %)**, 965,666 plots, 3,135 per entry (31 % of the map) | 0.32 MiB (64.0 %), 213 per entry |
| of it: **stored paths** | **8.36 MiB (36.1 %)**, 28,458 B per entry | 0.17 MiB (33.8 %), 1,801 B per entry |
| of it: **entries and keys** | 0.035 MiB (0.15 %) | 0.011 MiB (2.3 %) |
| window end: entries, counted bytes | 312, **30.02 MiB** (turn 307; 28.97 at turn 305) | 104, 0.80 MiB |
| of it: touched lists (verify minus reuse) | 1,373,900 plots, **20.96 MiB (70 %)**, 4,404 per entry | 35,965 plots, 0.55 MiB (69 %) |
| of it: stored paths and entries (the rest) | **9.06 MiB (30 %)** | 0.25 MiB |

The measured **+35.5 MB peak private** on H (laptop pair `c8ac`, the hand-over's section 1) is the counted 29-30 MiB plus
`[I]` the allocator's per-block overhead of many small blocks (one map node and one `vPlots` buffer per stored path, one
hash node and two vectors per entry: roughly 16-32 B each on the Windows heap) and the per-rebuild memo arrays (4 + 8 + 4
B per plot, 160 KB on H). Not measured separately.

### 1.3 Growth over turns and on the 43-civ variant

**Over turns** `[C from the same CSVs]`: H, store at each turn's end, 300: 308 entries / 25.97 MiB; 301: 308 / 26.38; 302:
310 / 28.89; 303: 312 / 28.97; 304: 312 / 28.90; 305: 312 / 28.97; 306: 312 / 30.02; 307: 312 / 30.02, so **+4.05 MiB in
7 turns (about 0.58 MiB per turn)** with 4 more entries. M 150 -> 162: 0.54 -> 0.80 MiB (about 0.02 MiB per turn). Two
drivers `[I]`: entries grow with the cities of the players that rebuild (2 per city: water and land), and an entry grows
as its searches touch more plots (more revealed plots, longer trade range, more routes) and reach more cities. Both
saturate: the touched list cannot exceed the map's plots and a search's closed set the plots in range. **Bound of the
prototype's form on H** `[I]`: 312 entries x 10,240 plots x 16 B = 48.8 MiB of touched lists alone at full reveal, plus
paths; so the store would keep growing past turn 307 towards roughly 50-70 MiB on this map in a long game. The prototype
also never removes an entry (razed or captured cities stay; the hand-over's section 5).

**The 43-civ variant** `[I, not measured]`: entries scale with the cities of the majors that rebuild; with 43 majors
instead of 12 (11 rebuilding in H) and similar cities per major, about 3.5-4 times H's entries, on a map at least as large,
so **the prototype's form would hold about 100-120 MiB** at a comparable stage (and its bound above scales the same
way). The design below brings that to about 12-30 MiB. The acceptance's run on the 43-civ variant measures it (the
statistics define of section 1.5 writes the parts per entry).

### 1.4 The design that cuts it (a reasoned case to keep it is not made: 64-70 % of the store is a per-plot hash that the decision does not need, and the rest is a copy of data in a wasteful form)

**(a) The touched set as a bitset, with one 64-bit hash per block of touched plots instead of one per plot.** The
reuse decision needs the touched set and whether every touched plot's property hash is unchanged; it does not need each
plot's own stored hash (only the prototype's diagnostics, which classify the first mismatching plot, do). Store per entry:
a bitset over the map's plots (`numPlots / 8` B: 1,280 B on H, 520 B on M) and, walking the set bits in plot-index order,
one combined 64-bit hash (`VP64PTMix` over the plots' memoised property hashes) per block of 64 touched plots
(`ceil(touched / 64) x 8` B). Validation walks the bits in the same order, mixes the memoised hashes and compares each
block's hash: a mismatch stops at its block, as the prototype stops at its plot. Exactness is the prototype's: a block
hash equal by chance is the same 2^-64-per-comparison collision as a per-plot hash `[I]`. H: 4,404 touched per entry ->
1,280 + 69 x 8 = 1,832 B per entry instead of 70,464 B, **20.96 MiB -> 0.55 MiB**; bounded by `numPlots / 8 + numPlots / 8`
per entry whatever the reveal (2.5 KB on H). The order of the touched plots is no longer the search's order: the
combined hashes are over index order, which both the store and the validation use. Verify builds keep the prototype's
per-plot lists beside it for the mismatch classification (statistics only).

**(b) The stored paths as the search's tree, without the per-path copies.** The one reader of the result inside the
lookup's scope is `UpdateTradePathCache` (`CvTradeClasses.cpp`:509-539), which reads per destination `it->first`, and
through `AddTradePathToCache` (:169-221) and `STradePathInfo` (`CvTradeClasses.h`:126) only `vPlots[i].x`, `.y`,
`vPlots.size()`, `iTotalCost`, `iNormalizedDistanceRaw` and `iTotalTurns` `[C]`. `GetMultiplePaths` (`CvAStar.cpp`:3408-3475)
builds each path by walking the closed node's parent chain, so all paths of one search are branches of one tree `[C]`.
Store per entry: the tree's nodes that lie on any returned path (each node's x, y, turns, moves, stop flag and its parent's
index: 12-16 B, contiguous), and per destination its plot index, its node, cost, normalised distance and turns (16-20 B).
A reuse rebuilds `map<int, SPath>` exactly as the search does (the same walk, the same 10,000-node failsafe, reversed), with
`sConfig` and `iTurnSliceGenerated` set from the finder's current data and the current turn slice, i.e. what a fresh search
would set (the prototype returned the stored, older values; no reader reads them `[C]`). Per destination this replaces a
map node (about 160-176 B with the heap header), an `SPath` (120 B) and a `vPlots` buffer (10 B x length + header) by about
20 B plus the nodes it does not share with the destinations nearer the origin. H's 9.06 MiB of paths fall to **about 2.3-6.3
MiB** `[I]`: 30 % less with no shared nodes at all (path length 20), about 75 % less if a node is shared by three
destinations on average; the statistics define measures it (1.5). The verify comparison (`VP64PTSamePaths`: the destination
keys, cost, distance, turns and every node of every path) runs on the rebuilt map against the fresh one, so the tree form is
checked by the same verify mode.

**(c) The store's lifetime.** Emptied at every game init, every load and at the DLL's game uninit (the hand-over's
section 5). At the end of each player's rebuild scope, entries of that player whose origin plot was not visited in the
scope (the city was razed, captured or lost) are erased, so the store holds at most 2 entries per live city of the
players that rebuild.

**(d) A cap.** A compile-time cap on the store's counted bytes (`VP64_SPEED_TRADE_CAP_MB`, MB2's pattern in 4809; default
32): over it, a search's result is not stored (the stored entry of that key, if any, is erased) and the search is played as
stock. Exact by construction (a missing entry only loses a reuse); counted.

**Expected on H 305** `[I]`: touched 0.55 MiB + paths 2.3-6.3 MiB + entries 0.04 MiB, **about 3-7 MiB counted instead of
29-30**, with about 3 heap blocks per entry instead of one per stored path, so the peak private difference should fall from
+35.5 MB to roughly +4 to +9 MB; the bound at full reveal on H about 1 MiB of touched data plus the paths. 43-civ: about
12-30 MiB.

### 1.5 What the statistics define records (so the parts are measured, not inferred, on the product form)

Behind `VP64_SPEED_TRADE_STATS` (the product form `VP64_SPEED_TRADE` alone writes nothing; 4809's `VP64_SPEED_P3_STATS`
pattern): per rebuild scope the prototype's counters, and the store's entries, bitset bytes, block-hash bytes, tree node
bytes, destination bytes, and the allocations held; a run of M, H, the 43-civ variant and the stress map then gives the
table of 1.2 for the product form directly.

## 2. The lookup: under the game-core lock, on the core thread only

### 2.1 What the prototype does `[C, 5a6c1a25d; the hand-over's section 5]`

The lookup sits at the top of `CvPathFinder::GetMultiplePaths` **before** `gDLL->GetGameCoreLock()`, and checks only a
global scope flag and the player. The scope is opened by `VP64ProtoTradeBegin` only on the core thread
(`gDLL->IsGameCoreThread()`), but a ui-thread `GetMultiplePaths` for the same player while the core scope is open would
read and write the core's store, memo and touched list. The touch hook in the four callbacks records whenever the global
`recording` flag is set, whatever thread runs the search. The memo of plot hashes lives for the whole rebuild loop.

### 2.2 The design

1. **Lookup and store inside the locked region.** In `GetMultiplePaths` the lookup moves after the lock is taken and
   `Configure(data)` succeeds; the store runs before the lock is released. No other thread can run a search or change
   game state between the lookup and the store unless the core thread releases the lock inside the search, and the
   search raises nothing: no Lua hook in `CvAStar.cpp` at all and none in `CvPlot::isValidMovePlot` (:14915) or
   `isCoastalCityOrPassableImprovement` (:4919), the predicates the four callbacks call `[C, batch 5]`.
2. **Core thread only.** The scope (player, team, open flag) is opened only by the core thread's `UpdateTradePathCache`
   and records that thread's id; the lookup and the store act only when the scope is open and `GetCurrentThreadId()` is
   the scope's thread; the touch hook records only then. A ui-thread `UpdateTradePathCache` (its own cache
   `m_lastTradePathUpdateUi`, `CvTradeClasses.cpp`:108-136) and every ui-thread search run the stock path and never read
   or write the package's state.
3. **The memo across a hook raise.** The rebuild's other work (the scoring, `GetTradeRouteRange`) runs between two
   searches and was not read for raises; the DLL releases the game-core lock by one level around every Lua hook raise
   (`CvLuaSupport.cpp`, the Architect's review section 2), and a ui-thread DLL call can then change game state. So the
   memo is valid only while no raise happened: a global raise counter, incremented (interlocked) where the DLL releases
   the lock around a raise (inside `#if defined(VP64_SPEED_TRADE)`), is read when a memo generation starts and at every
   lookup; if it moved, a new memo generation starts (every plot hash recomputed on demand). The store itself compares
   values, so a change made during a raise is seen by the fresh hashes.
4. **No run-time switches.** The prototype's environment reads (`VP64_PROTO_TRADEPATH_MODE`, `_FP`, `VP64_PROTO_DIR`)
   become compile-time defines: `VP64_SPEED_TRADE` (reuse), `VP64_SPEED_TRADE_VERIFY` (search always, play the fresh
   result, count false reuses), `_STATS`, `_SPOIL` / `_SPOIL_EVERY` (the negative controls), and the kit's logger.

### 2.3 Every static or global and its two-thread answer

| State | Kind | Read / written by | Two-thread answer |
|---|---|---|---|
| the store (entries: bitset, block hashes, tree, destinations) | global | the core thread, inside `GetMultiplePaths` under the lock, in an open scope of its own thread; cleared at init, load, uninit (core thread) | never reachable from a ui-thread call (thread check); live across raises only as values compared at the next lookup, so a change made during a raise makes it miss |
| the memo (stamps, hashes, generation, the raise count it started at) | global | the core thread in its scope | regenerated when the raise count moved (2.2 item 3); the ui thread never reads it |
| the touch recorder (plot list, stamps, flag, recording thread id) | global | set and cleared inside one `GetMultiplePaths` call under the lock; the four callbacks record only on the recording thread | the search raises nothing (2.2 item 1); a ui-thread search does not record (thread check) |
| the scope (player, team, open flag, thread id) | global | opened and closed by the core thread's `UpdateTradePathCache` | the thread id makes it invisible to the ui thread |
| the raise counter | global, interlocked | incremented by any thread at a raise; read by the core thread | a bump can only make the memo regenerate (exact either way) |
| the statistics (counters, per-part bytes) and the kit's logger | global (statistics builds only) | the core thread; the logger takes its own SRW lock | not in the product form |
| TR7, TR8 (later in the ticket) | none planned | | exact removals of work on the core thread; any static they add gets a row here |

The prototype's other statics (`s_iNarrow`, `g_vp64pt.mode`, the CSV handle) go with the run-time switches.

## 3. What this note does not settle

- The parts of 1.2 are the prototype's own byte count; the heap overhead (+35.5 MB measured against about 29-30 MiB
  counted) is inferred, not measured per part.
- The tree form's saving depends on how many path nodes destinations share: estimated 30-75 % of the paths part, measured
  by the statistics define on the product form.
- The 43-civ and long-game figures are estimates from H's counts; the acceptance's 43-civ run and the stress map's 50 turns
  measure them.
- The validation's cost with the block hashes (a full block is hashed before a mismatch is seen, at most 63 plots more
  than the prototype hashes per failing entry; each plot's hash is memoised per rebuild either way) is not timed.
- TR4 (a narrower search-relevant signature) goes on top with the verify mode deciding, as the ticket says; TR7 and TR8 are
  not designed here.
- Re-reading the hand-over's section 4 against pin batch 5 (its line numbers are batch 2's `04b1211b8`) is the first step of
  the code, not of this note; the reads above (`CvTradeClasses.cpp`:169-221, 509-539, `CvAStar.cpp`:3408-3475, the
  callbacks at :3672-3900) are batch 5's.

## 4. Part two: the product code (impl-4808, account B, from 2026-11-22)

Written by impl-4808 from 2026-11-22 21:00 EST. Fork clone `C:/c5-64bit-work/4808/fork`, branch **`perf/speed-trade`** cut
from the x64 pin **`1b87dd185`** (`deps/dll.lock`, branch x64-0.1b: batch 5 + the danger package + hidden embarks, the
`#line` lines removed). Line numbers in this section are the pin's. Marks as above.

### 4.1 Re-read of the hand-over's section 4 at the pin `[C]`

The four callbacks (`CvAStar.cpp`:3721-3930), `TradePathInitialize` (:3672), `GetMultiplePaths` (:3408-3484) and
`UpdateTradePathCache` (`CvTradeClasses.cpp`:363-552) read what the hand-over's tables 4.1 and 4.2 list; nothing new is
read. Since batch 2 the pin's `UpdateTradePathCache` gained the political danger table and `AddTradePathToCache` the
scoring (`:169-240`): both read the search's result only (`vPlots[i].x/.y`, size, cost, distance, turns), never the
finder. `GetTradeRouteRange` (`:5310-5381`) raises no hook. The reuse argument of the hand-over's section 3 holds at
the pin.

### 4.2 What the code does (fork `perf/speed-trade`; files `VP64SpeedTrade.h`, `VP64SpeedTrade.inl`, 7 edited files)

| Define | What |
|---|---|
| `VP64_SPEED_TRADE` | TR3 with part one's design: per (player, path type, origin plot) one entry: the search fingerprint (the prototype's `VP64PTCfg` without the destination list), the touched set as a bitset over the map's plots with one 64-bit hash per 64 touched plots in plot-index order, the result as the search tree (`SPathNode` + parent index per node on a returned path) and per destination its plot, node, cost, distance and turns. A reuse rebuilds `map<int,SPath>` with the walk and the reverse of `GetMultiplePaths`, `iTurnSliceGenerated` = now and `sConfig` = the finder's configuration, as a fresh search sets them. Plot signature: the prototype's level 2 plus the river edges (NE/W/NW flags, the six crossing bits, `isRiver`), closing the hand-over's 4.3 item 1. |
| `VP64_SPEED_TRADE_TR4` | per path type signatures of what the callbacks **derive**: water (revealed; unowned ice; deep, shallow, water; the coastal predicate; owned + at war + coastal predicate together; city; passable improvement on a land non-city plot) and land (revealed; city, else water, else camp, else `isValidMovePlot`, each only while the earlier pass; an owned city at war; route class none/other/road/railroad; woodland; the terrain and feature penalty as the callback decides it; flatlands; the plot-team test only with the woodland trait without the alternate trait on forest and jungle; the river edges; the extra-yield discount `min(20, ...)` as `TradePathLandCost` computes it, which replaces the owner bit, the improvement type, the city's passing-route yields and the era); the era leaves the fingerprint. |
| `VP64_SPEED_TRADE_TR7` | `UpdateFurthestPossibleTradeRoute` walks only the destinations of the origin city's row of this thread's cache (`GetAllPotentialTradeRoutesFromCity`), calling the same `CanCreateTradeRoute` and `GetValidTradeRoutePathLength`; every other city's length is -1 or 0 there. The longest length does not depend on the order, and every valid length is at most `iMaxRange` x base, so the stock loop's break never skipped a longer one. Stock loop when `MOD_EVENTS_TRADE_ROUTES` is on (a `PlayerCanCreateTradeRoute` listener would miss raises; the option is 0 in VP, `NewCustomModOptions.xml`:1101) or AI logging is on (log order). |
| `VP64_SPEED_TRADE_TR8` | `GetTradeRouteTurnMod` walks `GetAllBuildingsHere()` of each city (every building with `GetNumBuilding` > 0 is in it, once: `SetNumRealBuildingTimed`, `SetNumFreeBuilding`, `Read`), keeps a building only when it is the city's building of its class and skips zero modifiers. |
| `_VERIFY` | TR3/TR4: search always, play the fresh result, compare the rebuilt kept result field by field (`VP64_SPEED_CHECK_K("TRADE", ...)`), count re-run searches whose result equals the kept one; TR7, TR8: both answers computed, the stock one returned (`TRADE_TR7`, `TRADE_TR8`). |
| `_SPOIL=n`, `_SPOIL_EVERY=n` | the n-th kept result's first destination distance +1 (verify must catch it); every n-th kept result's distances x4+1 (the identity job must see DIFFERENT). |
| `_STATS` | one row per rebuild scope in `%VP64_SPEED_DIR%/VP64SpeedTrade.csv`: the counters, the store's entries and bytes by part (bitset, block hashes, tree nodes, destinations, entries), tree nodes against the fresh result's path nodes, TR7 fast/fallback and TR8 calls. |
| `_CAP_MB` | cap on the store's counted bytes, default 32: over it the result is not kept (exact: a reuse is lost). |

Hook points: `GetMultiplePaths` after `Configure` (lookup) and before the final release (store); a touch in each of the
four callbacks; `VP64Trade::Begin`/`End` around the origin-city loop; `VP64Trade::GameReset` in `CvMap::uninit`;
`VP64Trade::LockYield` before each of the five `LuaSupport::Call*` releases (`Lua/CvLuaSupport.cpp`:490-570) and the two
tactical-AI hand-overs (`CvTacticalAI.cpp`:12426, 12865), the seven places where the DLL lets go of a lock it holds (every
other `ReleaseGameCoreLock` is the `!bHadLock` pattern, `[C]` grep at the pin). The header is included from
`CvGameCoreDLLPCH.h` only under the package defines; `VP64SpeedVerify.h` names the defines in `VP64SpeedInfo.txt`. Every
block is followed by `#line N` (`perf/evidence/0504/4807/linecheck.py --define VP64_SPEED_TRADE,VP64_SPEED_TRADE_TR7,VP64_SPEED_TRADE_TR8`:
OK, 7 files, 27 blocks). No marker header: `tools/x64-dll/build.py` adds no define for it.

### 4.3 Statics added, with the two-thread answer (completes 2.3)

| State | Two-thread answer |
|---|---|
| `g_s` (store, memo arrays and generation, touch recorder, tree scratch, scope with thread id and depth) | used only inside an open scope by the thread that opened it (`IsGameCoreThread` at `Begin`, `GetCurrentThreadId` at every lookup, store and touch); lookup and store under the lock; freed in `CvMap::uninit` (no turn runs) |
| `g_iLockYield` (LONG, `InterlockedIncrement`) | bumped by any thread before it lets go of a held lock; read by the scope thread at every lookup; a bump only restarts the memo |
| `g_stats`, `g_pCsv` | counters of the scope thread (TR7/TR8 counters: any thread, statistics only, never read for a decision); the CSV only in `_STATS` builds |
| `vp64speed::Log` (`VP64SpeedVerify.h`) | verify and spoil builds only; its own SRW lock |
| TR7, TR8 | no state: each call computes from the game state |

### 4.4 Results (workstation, shared machine, headless: functional, no timing claims)

Layout: `server/build_server.py --src <keep>/src-x64-1b87dd185 --deps <keep>/deps-x64-release --config release --no-proxy
--out F:/c5-64bit-work/4808/server-build` (169 s; bin with the pin's DLL `b84aa3e9...`, server `43c1cc20...`): the
reference arm of every identity run below. Builds: `perf/evidence/0504/4808/kb.sh <name> <defines>` (kitbuild `fast`
with the pin's three marker defines added, since `build_as_pin.py` switches markers off; about 5 min).

**v1: TR3 + TR7 + TR8 in verify mode, statistics on** `[L, 2026-11-22 21:20-21:30 EST]`: fork `1b1871dfe`, DLL
`7876AC60...9B6B` (fast). `idjob.py --windows M,H --layout-bin <server-build>/bin`: **M 150..160 IDENTICAL (15 tables,
32,815 rows), H 300..305 IDENTICAL (14 tables, 125,209 rows)**; CvAssert.log the same lines as the reference.
Verify totals: **TRADE M 3,976 checks / 0 differences, H 10,712 / 0**; TRADE_TR7 M 5,476 / 0, H 16,956 / 0; TRADE_TR8
M 18 / 0, H 21 / 0 (`perf/evidence/0504/4808/id-v1/`).

| | M 150-161 | H 300-306 |
|---|---|---|
| searches | 5,490 | 16,882 |
| would be reused (TR3 signature) | 3,987 (72.6 %) | 10,886 (64.5 %) |
| false reuses | **0** | **0** |
| re-run searches whose result equals the kept one (TR4's room) | 1,080 | 4,515 |
| misses: no entry / fingerprint / touched plot | 106 / 77 / 1,320 | 312 / 100 / 5,584 |
| memo restarted after a lock yield | 0 | 2 |
| store at the end: entries, counted MiB | 106, 0.20 | **310, 4.07** (bitsets 0.46, block hashes 0.15, tree nodes 3.06, destinations 0.36, entries 0.04) |
| tree nodes / path nodes the same results hold as SPath copies | 6,646 nodes for 1,163 destinations | 200,597 nodes for 18,886 destinations |

The store on H holds **4.07 MiB against the prototype's 28.97-30.02 MiB** at the same window (part one's estimate: 3-7
MiB) `[L]`. The would-reuse share is the prototype's b3 share (H 64.7 %, M 75.7 % there; the pin and the window's
turns differ). The tree form: 200,597 nodes for 18,886 kept destinations, about 10.6 nodes per destination.

**v2: TR4 on top (TR3 + TR4 + TR7 + TR8), verify mode, statistics on** `[L, 2026-11-22 21:41-21:49 EST]`: fork
`42ca322ec` (v1 + the statistics rows reset after writing, so TR7/TR8 counts made between scopes are kept), DLL
`B88DE477...5339` (fast). `idjob.py --windows M,H,E` (references of v1 reused for M and H): **M IDENTICAL (32,815 rows),
H IDENTICAL (125,209 rows), E 0..50 IDENTICAL (15 tables, 20,619 rows)**. Verify: **TRADE M 4,777 / 0, H 13,774 / 0,
E 493 / 0**; TRADE_TR7 M 5,476 / 0, H 16,956 / 0, E 590 / 0; TRADE_TR8 M 18 / 0, H 21 / 0 (`perf/evidence/0504/4808/id-v2/`).
So verify decides for TR4: **no false reuse** while it takes the would-reuse share from 72.6 % to **87.4 % on M** and from
64.5 % to **82.6 % on H**:

| | M: TR3 | M: TR3 + TR4 | H: TR3 | H: TR3 + TR4 |
|---|---|---|---|---|
| would be reused | 3,987 (72.6 %) | **4,799 (87.4 %)** | 10,886 (64.5 %) | **13,948 (82.6 %)** |
| touched-plot misses | 1,320 | 514 | 5,584 | 2,535 |
| of the re-run searches, result equal to the kept one | 1,080 | 268 | 4,515 | 1,453 |
| false reuses | 0 | 0 | 0 | 0 |

The store is the same in both (the same results are kept; the signature changes only the decision). TR7 ran its fast
path on every call (E 590, M 5,476, H 17,126 calls; 0 fallbacks: VP has `EVENTS_TRADE_ROUTES` 0 and AI logging is off).
TR8 is called rarely (M 18, H 21 calls in the windows): its saving is small by count `[L]`.

**on1: the product form TR3 + TR4 + TR7 + TR8 (no verify, no statistics)** `[L, 2026-11-22 21:54-22:42 EST]`: fork
`42ca322ec`, DLL `0746373D...98FA` (fast, 246 s). `idjob.py --windows M,H,E,D24` (M, H, E references reused from v2):
**M IDENTICAL (32,815 rows), H IDENTICAL (125,209 rows), E IDENTICAL (20,619 rows), D24 three-part save test PASS** (tiny,
standard, huge: every part pass; 32-bit side `i0ship-f33fc46a`, deps/i0.lock's build) (`perf/evidence/0504/4808/id-on1/`).
So the reuse mode plays the pin's game on every window and keeps the save identity. (Wall times of the window runs,
an indication only: shared workstation, a fast build against the release pin, n = 1: H 154 s against 200 s, M 23.7
against 28.8 s, E 27.4 against 36.6 s.)

**Negative controls** `[L, 2026-11-22 22:50-23:02 EST]`:
- verify run's control, `VP64_SPEED_TRADE_SPOIL=300` (TRADE + TR4 + VERIFY + STATS; DLL `51AAC844...`): the 300th kept
  result gets its first destination's distance +1. **Caught on both windows**: M `TRADE` 4,777 checks, **1 spoil, 1
  difference** (turn 150, player 3, land search from plot 3805, 7 destinations), H 13,774 checks, **1 spoil, 1
  difference** (turn 300, player 3, plot 6603, 47 destinations); both games IDENTICAL to the pin (the verify build plays
  the fresh result) (`perf/evidence/0504/4808/id-spoil/`).
- identity job's control, `VP64_SPEED_TRADE_SPOIL_EVERY=10` (TRADE + TR4, no verify; DLL `68C990F6...`): every 10th kept
  result has every distance x4+1 (67 spoils on M). **idjob: DIFFERENT** on M: `MapUnitsState` first differs at turn
  151 (unit ids 3923 / 3924), 10 tables differ (`perf/evidence/0504/4808/id-spoilevery/`). So the identity job sees a
  reuse that plays wrong results.

**Release build of the product form** `[L, 2026-11-22 23:41-23:55 EST]`: `kb.sh b6-onrel --config release VP64_SPEED_TRADE
VP64_SPEED_TRADE_TR4 VP64_SPEED_TRADE_TR7 VP64_SPEED_TRADE_TR8` (whole-program link, the link place, MC #10871): DLL
**`55ACD8E9...E1B1`**, 23,750,656 B (the pin's 23,725,056 B: +25,600 B), 879 s, link repeatability check OK. The arm `on` of
the oldpc pairs.

**The fixed-seed Standard game 0-300, verify and `on`** `[L, 2026-11-22 23:01-2026-11-23 00:11 EST, headless]`:
`perf/evidence/0504/4808/arms.py` (idjob's play, trim and statscmp) plays `server/specs/continents-standard-fixed-seed.json`
(idjob's E game) to turn 301 with three DLLs in copies of the pin layout: ref = the pin `b84aa3e9`, verify = v2's
`B88DE477` (TRADE + TR4 + TR7 + TR8, verify, statistics; `VP64_SPEED_DIR` set), on = on1's `0746373D` (the product form,
fast); AI logging off. **verify 0..300: IDENTICAL (15 tables, 971,505 rows); on 0..300: IDENTICAL (15 tables, 971,505
rows).** Verify totals over the game: **TRADE 85,068 checks / 0 differences**, TRADE_TR7 100,224 / 0, TRADE_TR8 414 / 0.
Statistics (turns 23-301): 99,310 searches, would-reuse 85,106 (85.7 %), touched-plot misses 13,788, fingerprint
misses 300, no entry 116, entries erased for lost cities 24, no cap hit, no store refused; the store's peak 0.37 MiB
(92 entries at turn 300, Standard map). (The ref arm ran 23:01-23:13, the verify arm 23:27-23:40 and the on arm
23:56-00:11; between them the runs paused for 0630's barrier.) Evidence `perf/evidence/0504/4808/g300/`.

**Define off: byte-identical to the pin's DLL except the build identity** `[L, 2026-11-23 00:13-00:26 EST]`:
`perf/evidence/0504/4808/defoff.sh` (MC #10948). `perf/speedkit/defineoff.py` does not fit this pin as it stands (it
compares with `deps/dll.lock` `release`, still batch 5's `1fd51353` layout, and switches marker defines off, while the 0.1b
pin's DLL has the three marker defines on), so the script does its steps by hand: a plain copy of `perf/speed-trade`
`42ca322ec`, `build_as_pin.py --describe pin` (version string "Release-5.4.8-80-g1b87dd185 Copy"), release, `CL` = exactly
the pin's marker defines `VP64_HIDDEN_EMBARK VP64_SPEED_DANGER VP64_SPEED_DANGER_DG7`, the keep folder's stubs, and the
pin build's `SOURCE_DATE_EPOCH` = 1795214922 (the DLL's own `__DATE__ __TIME__`, "Nov 20 2026" "22:48:42", read from
`b84aa3e9`). Result DLL `1D88FF10...0C1F`, 23,725,056 B; `dllcmp.py --allow-build-id` against `b84aa3e9`:
**IDENTICAL-EXCEPT-BUILD-ID** (the PE TimeDateStamp, the two debug-directory stamps and the PDB GUID differ; the checksum
and every other byte equal) (`perf/evidence/0504/4808/defoff/`). So the `#line` after each of the 27 blocks keeps every
`__LINE__`, and with the define off the package adds nothing to the product DLL.

### 4.5 Timing and memory on oldpc (D178 development pair, plain release arms)

`[L, 2026-11-22 23:58-2026-11-23 00:20 EST, oldpc, quiet job 20261122-235726-c21d: alone, undisturbed, foreign cores
0.035; GPU NVIDIA GeForce RTX 2080, driver 32.0.16.1088 (2026-07-21)]`: `perf/speedkit/pair.py --a pin=<b84aa3e9> --b
on=<55ACD8E9> --scenario H --runs 3 --turns 15 --layout-bin <server-build>/bin` (MC #10948), H timed 301-315, both arms
in the pin layout (server `43c1cc20`), AI logging off, 0438's statistics-on database; `perf/tbjobs/analyze.py --ref pin`.

| Arm | DLL | s/turn mean (sd, cv) | 95 % CI | memory after 10 turns (MB) | peak private (MB) | growth MB/turn |
|---|---|---|---|---|---|---|
| pin | `b84aa3e9` | 13.909 (0.029, 0.21 %) | [13.836, 13.982] | 331.3 / 331.7 / 332.7, mean 331.9 | 357.5 / 358.3 / 359.1 | 1.25 / 1.87 / 2.17 |
| on (TRADE + TR4 + TR7 + TR8) | `55ACD8E9` | **12.536** (0.015, 0.12 %) | [12.498, 12.575] | 338.2 / 340.0 / 341.6, mean **339.9** | 364.2 / 365.2 / 366.4 | 1.52 / 1.83 / 1.44 |

**on - pin: -1.373 s per H turn (-9.87 %), 95 % CI [-1.434, -1.312], significant** (detectable about 0.054 s). The
statistics of the two arms' first runs: **IDENTICAL** (14 tables, 362,705 rows, 300-316). Memory: **+8.0 MB after 10
turns** (D151's metric) and +7.0 MB peak private, against the prototype's +35.5 MB peak on H (laptop, 0437); part one's
estimate was +4 to +9 MB. This is a D178 development pair (n = 3 per arm), grade B by `docs/briefs/PERFORMANCE.md`: a
default-on claim needs grade A (6 or more interleaved runs per arm). Why the numbers moved `[I, from 4.4]`: on H 82.6 %
of the trade-path searches are answered from the store (each a validation over about 3,900 touched plots, memoised per
rebuild, plus the rebuild of the result from the tree), and the store keeps about 4 MiB; the prototype (TR3 alone, 64.7
% reused, laptop) gave -0.79 s (-4.4 %). TR7's and TR8's shares are not separated (one arm carries all four). Evidence
`perf/evidence/0504/4808/oldpc-c21d/`.

**PGO arms, each with its own trained profile** `[L, builds 2026-11-23 00:27-01:22 EST on the workstation; pair 01:23-01:43
EST, oldpc, quiet job 20261123-012245-e6b7: alone, undisturbed, foreign cores 0.028; same GPU and driver as above]`:
`perf/evidence/0504/4808/pgo.sh` (4807's method, one heavy job at a time, MC #10881): per arm a `pgo-instr` build, the
training `tools/x64-dll/pgo/train.py --sets H,M,S` in a copy of the pin layout, then `release --profile <its own
profile>`; pin = `1b87dd185` + the marker defines (profile `10613E88...`, DLL **`004B94A6`**), on = `42ca322ec` + markers +
TRADE TR4 TR7 TR8 (DLL **`EFA4A7A0`**); lock records `perf/evidence/0504/4808/pgo/` (profiles not committed). Pair as above,
H 301-315, 3 per arm:

| Arm | s/turn mean (sd, cv) | 95 % CI | memory after 10 turns (MB) | peak private (MB) |
|---|---|---|---|---|
| pin, PGO | 12.162 (0.016, 0.13 %) | [12.122, 12.203] | 334.1 / 335.6 / 334.5, mean 334.7 | 359.7 / 360.6 / 357.0 |
| on, PGO | **10.907** (0.006, 0.05 %) | [10.893, 10.921] | 339.5 / 340.2 / 337.4, mean **339.0** | 368.3 / 365.4 / 365.8 |

**on - pin: -1.255 s per H turn (-10.32 %), 95 % CI [-1.299, -1.212], significant**; statistics **IDENTICAL** (14
tables, 362,705 rows); memory +4.3 MB after 10 turns. So the package keeps its share under PGO (plain -9.87 %, PGO
-10.32 %); PGO itself takes about 1.7 s off the pin's H turn here (13.909 -> 12.162: across two jobs, not one set, so
not a claim) (`perf/evidence/0504/4808/oldpc-e6b7/`). Grade B (n = 3), as above.

### 4.6 The 43-civ variant and the D157 stress map (headless, workstation, functional)

Fast 43-civ builds (`kb43.sh`: `build_as_pin.py --config fast --major-civs 43`, the pin's markers): pin43 `65FA9C6A`,
on43 `6F9BBDBC` (TRADE TR4 TR7 TR8), verify43 `C3F99BBD` (+ `_VERIFY`, `_STATS`), each in a copy of the pin layout
(one `server.exe` serves both player counts).

**The 30-major Large fixed-seed game 0 -> 101** (`server/tests/specs/continents-large-30majors-fixed-seed.json`, 4807's
g43) `[L, 2026-11-23 01:34-01:46 EST]`: **on 0..100 IDENTICAL, verify 0..100 IDENTICAL (15 tables, 179,253 rows)**;
verify TRADE 14,191 checks / 0 differences, TRADE_TR7 17,848 / 0, TRADE_TR8 54 / 0; would-reuse 80.1 %; store 0.34 MiB
(170 entries) at turn 100 (`perf/evidence/0504/4808/g43/`).

**The D157 stress map, 50 turns** `[L, 2026-11-23 01:46-02:07 EST]`: 9073's `TS1.20-T200` (42 AI majors, 20 city states,
Tectonic, Marathon) -> 251, compared 200..250, 9071's database made statistics-on (`perf/evidence/0438/dbinfo.py
--make-stats-on`, content `02b5faa4...` before the switch), the VP neutral toolkit game folder, 9074's no-victory probe,
the three 43-civ DLLs: **on IDENTICAL, verify IDENTICAL (15 tables, 174,844 rows)**; verify TRADE 18,779 checks / 0
differences, TRADE_TR7 21,454 / 0, TRADE_TR8 7 / 0; would-reuse 87.7 %; the store 0.96 MiB (272 entries; bitsets 0.65 MiB
of it: the map has 20,160 plots) at turn 250. **No crash in any arm**; CvAssert.log the same two asserts in every arm
(`CvGame.cpp:5992` "Failed to generate diplo response query", `:6086`, stock behaviour, 4807's 8.7) (`perf/evidence/0504/4808/stress/`).
Memory on the 43-civ variant: part one's estimate for the prototype's form was 100-120 MiB; the product form holds
0.34 MiB (Large, 30 majors, turn 100) and 0.96 MiB (stress map, 42 majors, turn 250) `[L]`.

### 4.7 The client: windowed S4 pair and the soak (the two-thread items of the Architect's review section 2)

**Windowed S4 pair, M 150 -> 160: IDENTICAL** `[L, 2026-11-23 02:10-02:26 EST, workstation, Goldberg instance c5h-a
(EXE build 4390913, claimed by 4808, MC #11090 / MC-B #11093), no-focus launch through perf/profilekit/acceptance.py,
dialog watcher: no box]`: `perf/evidence/0504/4808/winpair.sh` (4807's winpair/winarm, unchanged method): per arm
`provision.py set-proxy c5h-a --bin <arm bin> --proxy <the 0.1b pin's proxy>`, `TS0438-M-T150` loaded, 10 AI turns,
`SQLITE_LOGGING` on for the run (backed up and restored by SHA-256, restore rc 0 per arm), `set-proxy --original` after each
arm (rc 0; the final extra call refuses, "not proxied", as in 4807). The proxy was built for this pin:
`proxy/build_proxy.py --src <keep>/src-x64-1b87dd185 --out F:/c5-64bit-work/4808/proxy` (65 s, purity test passed before
for these inputs) `452A1FE3...EEF1`. Binaries read back from `acceptance-run.json`: EXE `04cfdc71`, proxy `452a1fe3`,
server `43c1cc20`, DLL pin `b84aa3e9` / on `55acd8e9` (release). `statscmp.py`: **15 tables, 32,293 rows, IDENTICAL** (the
row count of 4807's and 4804's windowed pairs). `provision.py verify c5h-a --quick` rc 0 after (`perf/evidence/0504/4808/win/`).
So in the client, with the EXE's ui thread calling the DLL during the AI turns, the package plays the pin's game.

**Client soak with the verify build: PASS, 25 AI turns, 0 differences, no crash, no box** `[L, 2026-11-23 02:27-02:42 EST,
c5h-a, the same claim]`: `perf/evidence/0504/4808/soakrun.sh` (4807's `soak/soakrun.sh` and `soak.py` unchanged in
method: the verify layout set into the instance with the pin's proxy, a renamed copy of `TS0438-M-T150`, 25 human turns
ended by `turns.end_turn`, and during the AI turns cycles of open a screen, hover 8 points of it, close it: city screen,
military overview, trade route overview, economic overview). Binaries read back: EXE `04cfdc71`, proxy `452a1fe3`, DLL
**`b88de477`** (TRADE + TR4 + TR7 + TR8 + VERIFY + STATS), server `43c1cc20`. 49 screens opened during AI turns (trade
10, city 14, military 12, economic 13), 25 of them still not the human's turn when closed; 392 hovers; AI turn walls
21.9-43.2 s (mean 28.3). `VP64SpeedTotals.csv` (to turn 174): **TRADE 11,588 checks / 0 differences**, TRADE_TR7 13,488 / 0,
TRADE_TR8 43 / 0, no `VP64SpeedDiffs.csv`; statistics: 12,892 searches, would-reuse 90.0 %, memo restarted after a lock
yield 0 times (no raise inside a rebuild scope even in the client). The game ended by the harness's stop at turn 175 (EXE
exit 15: "not a crash code"), no minidump; dialog marker empty. One `CvAssert.log` line: `CvPlayer.cpp:34145`, end-turn
blocker 10 (`ENDTURN_BLOCKING_CITY_RANGE_ATTACK`) still set when the harness forced the human's end turn `[I: made by the
forced end turn with a city able to fire; the verify build plays the stock results, so the package cannot change it]`
(`perf/evidence/0504/4808/soak/`). After the run: `set-proxy --original` rc 0, `provision.py verify c5h-a --quick` ok;
c5h-a released 02:45.

### 4.8 Acceptance status (2026-11-23 02:45)

| Item | State |
|---|---|
| design note part one accepted by the lead and the MC | yes (lead MC #7185, MC #10871) |
| reuse with verify, negative control caught | yes: 0 false reuses on M, H, E, 0-300, 43-civ game, stress map, client soak; SPOIL caught on M and H; SPOIL_EVERY DIFFERENT |
| gaps closed: river edges hashed, store emptied at init/load | yes (`RiverBits`; `GameReset` in `CvMap::uninit`) |
| TR4 with verify deciding; TR7, TR8 each with its counter | yes |
| define off byte-identical except build ids | yes, IDENTICAL-EXCEPT-BUILD-ID against `b84aa3e9` |
| define on IDENTICAL M 150-160, H 300-305, E 0-50; D24 | yes: fast build `0746373D` M, H, E, D24 PASS; the release build `55ACD8E9` M, H, E IDENTICAL too (`[L, 2026-11-23 02:47-02:52 EST]`, `perf/evidence/0504/4808/id-onrel/`) |
| verify 0 false reuses on M, H, a fixed-seed game 0-300 | yes |
| coverage per item | yes: the statistics rows (searches, reuses, misses by kind; TR7 fast/fallback; TR8 calls) |
| 43-civ once, D157 stress map 50 turns | yes, IDENTICAL, no crash |
| memory after 10 turns per arm (D151) | +8.0 MB (plain), +4.3 MB (PGO) on H, oldpc |
| oldpc D178 pairs, plain and PGO arms | -9.87 % and -10.32 % per H turn, significant, grade B (n = 3) |
| windowed S4 vs S4 M 151-160 | IDENTICAL |
| client soak with the HI sweeper's screens | PASS (4807's soak method on the sweeper's action layer, as 4807 did) |
| not done | grade A timing (6 or more runs per arm; needed before default-on, PERFORMANCE.md); D24 on the release build (done on the fast build); the 43-civ and stress-map runs and the 0-300 game used fast builds; M timing pair (H only); TR7's and TR8's separate shares of the saving |
