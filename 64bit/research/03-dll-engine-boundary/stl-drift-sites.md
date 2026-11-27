# STL drift sites: full inventory (epic 1600, 2026-11-10)

Every place in the DLL where behaviour defined by the standard library (or by the C runtime) can differ between the
32-bit DLL (VC9 / MSVC 2008 headers, `msvcr90.dll`, `msvcp90.dll`) and the x64 DLL (MSVC 14.43 STL, UCRT; D15), and
what each one feeds. Summary, high-risk sites and the fix epic: `x64-dll-port.md` section 9. This file is the full
list.

Source: the DLL fork, branch `x64` @ `e9d0c4837` (`%USERPROFILE%\Documents\GitHub\Community-Patch-DLL-x64`), all of
`CvGameCoreDLL_Expansion2`, `CvGameCoreDLLUtil`, `FirePlace`, `CvGameDatabase`, `CvLocalization`, `CvWorldBuilderMap`
(`*.cpp`, `*.h`, `*.inl`). Library sources compared: VS 9.0 `VC\include\algorithm`, `xtree`, `xhash`, `functional`
and VS 2022 `VC\Tools\MSVC\14.43.34808\include\algorithm`, `__msvc_heap_algorithms.hpp`, `xtree`. Read 2026-11-10
03:52-04:16 EST by the epic 1600 background agent. Marks: `[C]` confirmed by reading the source (or, where said, the
disassembly), `[I]` inferred. Line numbers are those of `e9d0c4837`.

Risk classes: **outcome** = can change a gameplay decision, a synchronized RNG draw or game state; **save** = changes
save bytes only; **Lua** = changes an order visible to Lua scripts; **none** = cannot differ between the builds (reason
given). "Pre-existing" = already differs between two runs of the same x86 build.

## 0. Key result: the algorithms are the same

Read side by side, the VC9 and MSVC 14.43 implementations of the order-producing algorithms make the same comparisons
and the same swaps on the same input, so they produce the same permutation for any comparator that is a strict weak
order `[C]`:

| Algorithm | VC9 (`algorithm`) | MSVC 14.43 | Same result? |
|---|---|---|---|
| `std::sort` | `_Sort` 3080: introsort, `_ISORT_MAX` 32, ideal budget `I/2 + I/4`, `_Unguarded_partition` 3023 (fat pivot), `_Median` 3008 (ninther when count > 40, step `(n+1)/8`), `_Med3` 2996, insertion sort 2966, heap-sort fallback | `_Sort_unchecked` 8339, `_ISORT_MAX` 32, `(I>>1)+(I>>2)`, `_Partition_by_median_guess_unchecked` 8268, `_Guess_median_unchecked` 8251 (same ninther, `(n+1)>>3`), `_Med3_unchecked` 8235, insertion sort 8211 | Yes. Only difference: MSVC 14 skips `_Med3`'s third compare when no swap happened, which is a no-op for a strict weak order |
| `push_heap` / `pop_heap` / `make_heap` / `sort_heap` | `_Push_heap` 2011, `_Adjust_heap` 2101ff, `_Make_heap` 2184ff | `_Push_heap_by_index`, `_Pop_heap_hole_by_index`, `_Make_heap_unchecked` (`__msvc_heap_algorithms.hpp`) | Yes: same child choice (`right < left` picks left), same "only child" case, same sift-up. So `std::priority_queue` (push_back + push_heap, pop_heap + pop_back on both) also pops ties in the same order |
| `nth_element` | `_Nth_element` 3733 | `nth_element` 9324 | Yes (same partition, same insertion-sort tail) |
| `random_shuffle(first, last, rng)` | `_Random_shuffle` 1801: for index 2..N, `swap(next, first + rng(index) % index)` | `_Random_shuffle1` (via 5845): for index 2..N, `rng(index)`, swap unless equal | Yes: N-1 draws with the same arguments in the same order, same permutation |
| `stable_sort` | merge sort | merge sort | Yes by definition: the output of a stable sort is fully determined by the comparator |
| `multimap` / `multiset` insert of an equal key | `xtree` 628: goes left only on strictly-less, so after the equal keys | `_Find_upper_bound` for `_Multi` (`xtree` 1019) | Yes: equal keys stay in insertion order |

What does differ: the hash functions and bucket logic of `stdext::hash_*` / `std::tr1::unordered_*` (VC9 `xhash` 75
`(size_t)key ^ 0xdeadbeef` then `ldiv(..., 127773)`; `functional` 1263; MSVC 14: FNV-1a over the key bytes), so
**iteration order of unordered containers differs** `[C]`; addresses of heap objects (differ between any two runs, and
between the builds); and the CRT math functions (`msvcr90` vs UCRT).

Not checked: behaviour of these algorithms with a comparator that is *not* a strict weak order (none was found, see
category 2), and the heap-sort fallback of `std::sort` on adversarial input (only reachable from the two integer sorts,
which have unique results anyway).

## 1. Unordered containers (`stdext::hash_*`, `std::tr1::unordered_*`)

Search: `\b(stdext::)?hash_(map|set|multimap|multiset)\s*<` (8 hits) and
`\b(tr1::|std::)?unordered_(map|set|multimap|multiset)\s*<` (44 hits), then every use of each declared member, typedef
and variable (`begin(`, `find(`, `operator[]`, range insert, serializers). No range-`for` exists (the code is VC9
compatible). 25 containers in 15 groups plus one serializer; 8 iteration sites.

| # | Container (declared) | Iterated at | Order feeds | Risk | Fix / why safe |
|---|---|---|---|---|---|
| U1 | `CvCity::m_miInstantYieldsTotal` `tr1::unordered_map<YieldTypes,int>` (`CvCity.h:2093`; sync var `CvCity.h:2508`) | save `CvCity.cpp:32427` and the sync-var delta, both through `FStlContainerSerialization.h:194` | save bytes, network delta bytes | none since D18 | written through a `std::map` copy (fork commit e9d0c4837 / 3ac5819ba) `[C]`, headless-tested in issue 1008 `[L]` |
| U2 | `CvExplorationPlotSet::m_aValues` `tr1::unordered_map<int,ExploreEntry>` (`CvEconomicAI.h:88`), 3 per player | save `CvEconomicAI.cpp:4765`; `append()` `CvEconomicAI.h:81` (range insert) | save bytes; append: nothing | none since D18 | save sorted (D18); range insert of unique keys gives the same contents in any order `[C]` |
| U3 | `CvGame::m_AdvisorMessagesViewed` `tr1::unordered_set<uint>` (`CvGame.h:899`) | save `CvGame.cpp:10887`; lookups `CvGame.cpp:5089-5103` | save bytes | none since D18 | save sorted (D18) |
| U4 | `CvGlobals::m_infosMap` `stdext::hash_map<std::string,int>` (`CvGlobals.h:169`, member `:2693`) | `Lua/CvLuaGameInfo.cpp:104` builds the global `GameInfoTypes` | insertion order of a Lua table, hence `pairs()` order | **Lua** (low) | No Lua file in the fork iterates it (`pairs\s*\(\s*GameInfoTypes` over `*.lua`: 0 hits) `[C]`; a third-party mod that does, inside synced gameplay code, would behave differently. Fix: copy into a `std::map` (or sort the pairs) before `lua_setfield`. Changes the x86 `pairs()` order too. **Fixed 2026-11-10 (D25, epic 2400): fork commit 5810b3c71 on `i0-ship`, merged into `x64` 29f15ac38: pushed from a sorted vector** |
| U5 | `CvDatabaseUtility::m_storedResults` `stdext::hash_map<std::string,Results*>` (`CvDatabaseUtility.h:113`) | `CvDatabaseUtility.cpp:28` | `delete` of each cached result | none | deletion has no ordered side effect |
| U6 | `TContainer<T>::m_items` `tr1::unordered_map<int,T*>` (`TContainer.h:86`): `CvMap` areas, landmasses, continents, rivers (`CvMap.h:482-485`); player cities, units, armies (`CvPlayer.h:3689-3691`) | destructor `TContainer.h:99`, `RemoveAll` `:159` | `delete` order | none direct | game order comes from `m_order` (vector). Deletion order only shapes the heap free lists, so later allocation addresses: visible only to the pointer-ordered sites P4 and P11 (themselves pre-existing run-to-run). `~CvCity` calls `CvCityManager::OnCityDestroyed` (`CvCity.cpp:540`): removal from lists, order-independent result |
| U7 | `CvBuilderTaskingAI::m_futureYieldBonuses` nested `tr1::unordered_map` (`CvBuilderTaskingAI.h:281`) | never (`find` at `CvBuilderTaskingAI.cpp:2494`, `:2498`) | - | none | lookup only |
| U8 | `CvBuilderTaskingAI::m_extraPlotsForXAdjacentImprovements` `unordered_map<ImprovementTypes, unordered_set<const CvPlot*>>` (`.h:295`) | never (`find` at `.cpp:1256`, `2564`, `4773`, `4794`, `4810`) | - | none | lookup only (pointer keys are hashed, never ordered) |
| U9 | `CvDealAI::m_dealItemValues` (`CvDealAI.h:229`) | never (`CvDealAI.cpp:933-934`, `1205`) | - | none | lookup cache |
| U10 | `CitySet` = `tr1::unordered_set<int>` (`CvHomelandAI.h:180`): `aProcessedCities` (`CvHomelandAI.cpp:1232`), `aWorkerRegionCities` (`:1246`), `SWorkerRegion::m_aCities` (`.h:186`) | never (`find` at `.cpp:1242`, `7378`) | - | none | lookup only |
| U11 | `gReachablePlotsLookup`, `gRangeAttackPlotsLookup`, `gDistanceToTargetPlots`, `gSafePlotCount` (`CvTacticalAI.cpp:46-48`, `56`; typedefs `CvTacticalAI.h:879-882`) | never | - | none | lookup caches |
| U12 | `dangerStats`, `attackStats` (`CvTacticalAI.h:1102`, `1112`) | never (`CvTacticalAI.cpp:6788`, `6819`, `6849`) | - | none | lookup caches (sample-checked) |
| U13 | `CvTypes::MissionNameLookup` (`CvTypes.cpp:92`) and three function-local `LookupTable`s (`:109`, `:147`, `:173`) | never | - | none | lookup only |
| U14 | `FVariableHash` (`FVariableSystem.h:49`) | `GetFirst/NextVariableName` | - | none | not used by the DLL (0 references) |
| U15 | closed DB library: `Database.h:172-173`, `DatabaseResults.h:125`, `ResultsCache.h:38` | inside the library only | - | none `[I]` | lookup-by-name tables. The x64 replacement (epic 1400) must not take any output order from them |
| U16 | `operator<<(FDataStream&, const stdext::hash_map&)` (`FStlContainerSerialization.h:185`) | writes in hash order | save bytes, if anything used it | none today | 0 callers: no saved member is a `stdext::hash_map`. Hygiene: give it the D18 sorted write too |

## 2. Sorts, heaps, selection

Search: `(?<![\w.>:])(std::)?sort\s*\(` (4), `\bstable_sort\s*\(` (132), `\.StableSortItems\s*\(` (113),
`\.SortItems\s*\(` (0), `\bpartial_sort\s*\(` (0), `\bnth_element\s*\(` (10),
`\b(make_heap|push_heap|pop_heap|sort_heap)\s*\(` (9), `\bpriority_queue\s*<` (5 hits, 1 container),
`\b(min_element|max_element|minmax_element)\s*\(` (0), `std::partition` (0), `qsort` (0). Comparators reviewed: all 60
`operator<` / `operator()` definitions in `CvGameCoreDLL_Expansion2` and `CvGameCoreDLLUtil` and the 12 free-function
comparators passed to `stable_sort` (functors among them counted above; `CounselSort`, `SortThemingBonus`, `sortHotkeyPriority`, `ScoreCityEntryHighEval`,
`SortTR`, `SortByThreatLevel`, `OpinionEval`, `LeaderWithNumTechsEval`, `CvHomelandUnitAuxIntSort`,
`CvHomelandUnitAuxIntReverseSort`, `SortByExpectedTargetDamageDescending`, `PrSortCombatFirst`). All compare integers,
enums or floats with `<` / `>`, with or without tie-breaks; none compares pointers or uses `<=` `[C]`. Every two-argument
`stable_sort` (99 calls) sorts values (`int`, `pair<int,...>`, `OptionWithScore<T>`, `SPlotWithScore`, ...), never raw
pointers `[C]`.

| # | Site | Order feeds | Risk | Why |
|---|---|---|---|---|
| S1 | `CvWeightedVector::SortItems` `std::sort` (`CvGameCoreDLLUtil/include/CvWeightedVector.h:153`) | - | none | 0 callers; the template member is never instantiated `[C]` |
| S2 | `FArray::Sort` `std::sort` (`FirePlace/include/FireWorks/FArray.h:385`) | - | none | 0 callers |
| S3 | `CvCityConnections.cpp:691` `sort` of plot indices | route-change sets | none | `vector<int>` (`PlotIndexStore`): result unique |
| S4 | `CvTacticalAI.cpp:5639` `std::sort` of `vector<int>` (`GetPlotsUnderRangedAttackFrom`, `:5596`) | attackable plot list | none | ints: result unique (sample-checked) |
| S5 | 245 stable sorts (132 `stable_sort`, 113 `StableSortItems`) | AI choices everywhere | none | stable sort output is fully determined by the comparator; all comparators are strict weak orders. Ties keep input order, which comes from vectors and ID loops |
| S6 | `nth_element`: `CvGame.cpp:10557`, `10625`, `10626`, `10627`, `10628`, `10629`; `CvMinorCivAI.cpp:12071`, `12247`, `12457`; `CvTacticalAI.cpp:12312` | medians | none | only the nth value is read, and the algorithm is identical anyway |
| H1 | A* open list: `CvAStar.cpp:496` (`make_heap`), `:500` (`pop_heap`), `:704` (`push_heap`), comparator `PrNodeIsBetter` (`CvAStarNode.h:359`) | paths of every pathfinder | none | total order: cost, heuristic, Y, X, layer; and identical heap algorithms |
| H2 | tactical simulation heaps: `CvTacticalAI.cpp:9561`, `11483` (`push_heap`), `12392`, `12822` (`pop_heap`), `12399`, `12829` (`make_heap`), comparator `PrPositionSortHeapGeneration` (`CvTacticalAI.h:1216`) | tactical moves | none | ties broken by position ID; identical algorithms (sample-checked) |
| H3 | `priority_queue<SPlotWithScore>` `CvPlayerAI.cpp:3197` (returned at `:3195`, consumed `:3425`, `:3453`) | culture-bomb plot choice | none | ties on equal score among distinct plots pop in an order fixed by the (identical) heap algorithm and the deterministic plot loop that fills it |

The Q14 concern "binary-heap and `std::priority_queue` tie order for equal priorities" is therefore closed: the
libraries agree element for element.

## 3. Shuffles and other RNG-driven library code

Search: `\brandom_shuffle\s*\(` (1), `\bstd::shuffle\s*\(` (0), `\bshuffleArray\s*\(` (4: definition + 3 calls),
`\.Shuffle\s*\(` (0 callers), `<random>` engines and distributions (0), `(?<![\w.>:])s?rand\s*\(` (0).

| # | Site | Feeds | Risk | Why |
|---|---|---|---|---|
| R1 | `TContainer<T>::Shuffle` `std::random_shuffle(m_order, GetJonRand)` (`TContainer.h:219`; `GetJonRand` `CvGame.cpp:84`) | would draw the synchronized RNG | none | 0 callers, so never instantiated `[C]`. Even if called, both libraries draw N-1 values with arguments 2..N and produce the same permutation (section 0). Note: `random_shuffle` is removed in C++17; the build is C++14 |
| R2 | `shuffleArray` (`CvGameCoreUtils.h:295`), called at `CvGame.cpp:8577`, `CvMap.cpp:2480`, `CvPlot.cpp:6454` | map RNG | none | the DLL's own Fisher-Yates on `CvRandom`; no library code |

No RNG draw happens inside an iteration over an unordered or pointer-ordered container (categories 1 and 4 list every
such iteration).

## 4. Containers ordered by pointer value

Search: `\b(std::)?(multi)?(set|map)\s*<\s*(const\s+)?[A-Za-z_][\w:]*\s*\*` (47 hits), `std::less\s*<` (3),
explicit pointer `<` comparisons (`\bp[A-Z]\w*\s*<\s*p[A-Z]\w*`: 2, both in `FFastAllocator.h` range checks). 15
containers.

Two facts decide most of these `[C]`: all `CvPlot` objects live in one array (`m_pMapPlots = FNEW(CvPlot[iNumPlots])`,
`CvMap.cpp:600`), so `CvPlot*` order is plot-index order on every build and run; and tactical positions come from a
preallocated array handed out in sequence (`CvTactPosStorage`, `CvTacticalAI.h:1507-1524`, `peekNext`/`consumeOne`),
so their pointer order is creation order. `CvCity` and `CvUnit` objects are separate heap allocations
(`TContainer<T>::Add`, `new T`, `TContainer.h:183`), so their pointer order is arbitrary on every build.

| # | Container | Iterated / used | Feeds | Risk | Fix / why safe |
|---|---|---|---|---|---|
| P1 | 5 `map<CvPlot*,...>` (`CvBuilderTaskingAI.cpp:1843-1847`) | iterated `:1886` | route directives | none | plot array: index order |
| P2 | `set<CvPlot*>` `sPlotsToCheck`, `sNewPlotsToCheck` (`CvBuilderTaskingAI.cpp:4733`, `4744`) | iterated `:4745` | extra-adjacency plot sets | none | plot array |
| P3 | `m_worstCityPlotValues` `map<const CvCity*,int>` (`CvBuilderTaskingAI.h:280`) | `find` only (`.cpp:4578`) | - | none | lookup only |
| P4 | `ms_kCityMap` `std::map<CvCity*,CityList>` (`CvCityManager.cpp:13`, `15`) | iterated `:65`, `:86`, `:100` | order of equal-distance cities in each city's "nearby cities" list (`AddToSortedList` inserts before the first city at `<=` distance, `:46`) | none (pre-existing address order) | only consumer `CvPlayerTrade::CanCreateTradeRoute` (`CvTradeClasses.cpp:4502`) returns whether *any* route exists; the path check reads a cache. Decision-neutral `[C]`. Optional hygiene: key by city ID |
| P5 | `map<CvPlot*,ReachablePlots>` (`CvHomelandAI.cpp:1687`) | `find`/`[]` only (`:1692`, `:1717`) | - | none | lookup only (and plot array) |
| P6 | `map<CvUnit*,ReachablePlots>` `allWorkersReachablePlots` (`CvHomelandAI.cpp:3401`) | `find` only (`:3216`) | - | none | lookup only |
| P7 | `DeferredFogPlots` `set<CvPlot*,PrSortByPlotIndex>` (`CvMap.h:182`) | yes | fog updates | none | explicit index comparator (`CvPlot.h:1009`) |
| P8 | `set<CvPlot*,PrSortByPlotIndex>` `nonCityZonePlots` (`CvTacticalAnalysisMap.cpp:659`) | yes | dominance zones | none | explicit index comparator |
| P9 | `set<CvUnit*,PrSortByUnitId>` (`CvTacticalAI.cpp:2589`), `set<CvPlot*,PrSortByPlotIndex>` (`:2631`) | iterated `:2632`, `:2648`, `:2663` | tactical targets | none | explicit ID / index comparators (`:2580`) |
| P10 | `map<const CvTacticalPosition*,const CvTacticalPosition*>` `nextAttackPosition` (`CvTacticalAI.cpp:12749`) | `begin()->first` at `:12811`; `find` at `:11419` | which combat position the support-move search starts from | none | all keys are the parent chain of a position from `gTactPosStorage` (callers `:9536`, `:12467`): array-backed, so the lowest address is the earliest-created position on both builds `[C]` |
| P11 | `std::map<CvUnit*,CvPlot*>` `unitToPlotMap` (`CvUnit.cpp:15441`, returned by `DoSquadPlotAssignments`) | iterated `CvUnit.cpp:15542` in `DoSquadMovement` | **order of `sendPushMission` move orders for a human squad move** (Squads for VP), hence which unit moves first, path blocking, ZOC, the order of combat if a move attacks | **outcome** (pre-existing: heap-address order differs between any two runs) | iterate in unit-ID order: key the map by `GetID()` (or copy to a vector and `stable_sort` by ID) before sending. Saves unaffected; the x86 order changes from address order to ID order. **Fixed 2026-11-10 (D25, epic 2400): fork commit 1512a94b4 (sorted copy by unit ID) on `i0-ship`, merged into `x64` 29f15ac38. Shown [L] in the x64 server with a probe (`tools/d10/evidence/2404/squad-order-x64.txt`): without the fix three runs gave three orders, with it unit-ID order every time; not observable in the 32-bit host, whose `sendPushMission` is a stub** |
| P12 | `std::set<CvPlot*>` `uniqueDestPlots` (`CvUnit.cpp:15566`) | iterated `:15574` | squad move preview plots (UI) | none | plot array; UI only |
| P13 | `CvAssert.h:73` `m_deletedObjects` | debug object tracking | - | none | debug builds only |
| P14 | `DestructionNotification::m_targets` (`FObjectHandle.h:60`) | destructor `:39` | `removeTarget()` on each handle | none | filled only under `TRACK_DESTRUCTION_NOTIFICATION`; result order-independent |
| P15 | `FAutoArchive::m_deltas` `std::set<FAutoVariableBase*>` (`FAutoArchive.h:80`; walked by the replacement `tools/x64-dll/closedlib-stubs/FireWorks/FAutoArchive.cpp:46`, `:67`) | `saveDelta` | order of changed variables in a network sync message | none | every variable of one archive lives inside one `CvSyncArchive<T>::SyncVars` object (`CvSerialize.h:394-483`; `GetStorage` uses `offsetof`, `:496`), so pointer order = member declaration order, the same on both builds and in every run `[C]`. Corrects the earlier "depends on heap addresses" `[I]` (`x64-dll-port.md` 4.1, `tools/x64-dll/save-layout-audit.md` 6) |

## 5. Hash values used as data

Search: `(tr1::|std::)hash\s*<` (15), `hash_value`, `FStringHash`, `stringHash`/`hasher(`, `GetHash\(`.

| # | Site | Feeds | Risk | Why |
|---|---|---|---|---|
| X1 | `stringHash` (`CvGame.cpp:86`), own FNV-1 over every character, `uint` | saved: `m_AdvisorMessagesViewed`, `m_aszDestroyedCities` (`CvGame.cpp:8373-8381`), `m_aszGreatPeopleBorn` (`:8388-8403`) | none | own code, 32-bit on both builds (pinned by issue 1004) |
| X2 | `FStringHash` = `g_CRC32.Calc` (`CvGlobals.cpp:70-78`) | saved info-type hashes (`CvInfosSerializationHelper.cpp:300`, `CvPromotionClasses.cpp:4109`), notification types, game-option map | none from the STL | the CRC lives in the closed FireWorks library; its x64 replacement must match (final xor check pending, `save-layout-audit.md` section 5) |
| X3 | `tr1::hash<int/bool>` inside `SDealItemValueParamsHash` (`CvDealAI.h:213-221`), `SPathFinderStartPosHash`, `SIntPairHash` (`CvTacticalAI.h:843-875`) | bucket placement of U9, U11 | none | keys compare by value; no hash is stored or iterated |
| X4 | `SUnitIDValueContainer::GetHash()` (`CvUnit.h:294`), `size_t`, stored in `DefendKey::iDamageHash` (`CvTacticalAI.h:1025-1037`, set at `CvTacticalAI.cpp:6774`, `6785`) and part of the cache key's equality | which cached danger value is reused | none in practice `[I]` | width, not STL: `(k << 4) ^ v` truncates on x86 only when a unit ID is >= 2^28 or negative; with non-negative IDs below 2^28 and non-negative damage the values are equal on both builds |
| X5 | `std::map` keyed by CRC values: `EntryHashTable` (`CvNotificationClasses.h:68`), `InfosHashMap` (`CvGlobals.h:170`), `HashToOptionMap` (`CvPreGame.cpp:269`) | lookups | none | ordered by value, not by library hash |

## 6. Lua-visible order

| # | Site | Risk |
|---|---|---|
| L1 | `GameInfoTypes` built by iterating U4 (`Lua/CvLuaGameInfo.cpp:97-112`) | Lua (low): see U4 |

No other Lua binding pushes elements of an unordered or pointer-ordered container (the only iterations of those
containers are the ones listed in categories 1 and 4) `[C]`.

## 7. C runtime and other implementation-defined behaviour

Search: `\b(pow|powf|exp|expf|log|logf|log10|log10f|sqrt|sqrtf|sin|cos|tan|atan|atan2|acos|asin|fmod)\s*\(` (46 lines),
`\b(floor|ceil|floorf|ceilf)\s*\(` (11), time sources (19, all logging, timers or seeding a new game when the seed is
0), `multimap` (34, equal keys keep insertion order on both, section 0).

The official 5.4.8 x86 DLL imports `pow`, `log`, `log10`, `atan2`, `sqrt`, `floor`, `ceil` from `MSVCR90.dll`
(import table, `llvm-objdump -p`, 2026-11-10) `[C]`; the x64 DLL gets them from the UCRT. `sqrt` is correctly rounded
in both and `floor`/`ceil` are exact, so those agree. `pow`, `log`, `log10` and `atan2` are not required to be
correctly rounded and are different implementations: results may differ in the last bit `[I]`, which changes an `int`
result only when the exact value lies within one ulp of an integer boundary. 29 lines:

| # | Lines | Feeds | Risk |
|---|---|---|---|
| M1a | `pow` (21): `CvUnitCombat.cpp:3105` (`DoDamageMath`, `pow(r, 4.0)`: combat damage); `CvPlayer.cpp:46538` (`getGrowthThreshold`); `CvPolicyClasses.cpp:4842` (policy cost); `CvGameCoreUtils.cpp:1271` (purchase cost, `+1e-9` guard), `:1406` (upgrade cost); `CvTreasury.cpp:539`, `:989` (maintenance); `CvMinorCivAI.cpp:10484` (influence decay), `:15554` (gold gift); `CvCity.cpp:16820` (real population, display), `:17325`, `:27519`, `:27838`; `CvUnitClasses.cpp:1784`, `:1787`, `:1795` (unit power); `CvDealAI.cpp:6791`; `CvDiplomacyAI.cpp:49181`; `CvAdvisorRecommender.cpp:225`; `CvCityStrategyAI.cpp:2264`; `CvTechAI.cpp:392` | game state (damage, growth, costs, influence) and AI weights | **outcome** (rare, near integer boundaries) [I]; measured 2026-11-10 (section 11): 1-ulp differences at the combat site only, no integer changed on two played games and a 7.6 M-pair sweep |
| M1b | `log` (6 lines): `CvGame.cpp:5740`, `:5749` (diplomatic victory votes needed); `CvTradeClasses.cpp:6922`, `6923`, `7311`, `7312` (trade-route religious pressure) | game state | **outcome** (rare) |
| M1c | `log10`: `CvFlavorManager.cpp:350` (AI flavour adjustment by map density) | AI weights | **outcome** (rare) |
| M1d | `atan2`: `CvPlot.cpp:2326` | angle test | **outcome** (rare) `[I]` |
| M2 | `sqrt`/`sqrtf` only (17 lines, e.g. `CvDealAI.cpp:1590`, `CvTradeClasses.cpp:2777`, `CvTacticalAI.cpp:12000`) and `floor`/`ceil` (11, e.g. `CvGame.cpp:5756`, `CvUnit.cpp:18051`) | various | none: correctly rounded / exact |
| M3 | `rand()`, `<random>`, `std::shuffle` | - | none: 0 uses |

These overlap the float drift of `x64-dll-port.md` section 5 (corrected 2026-11-10 by this audit): the official x86 DLL already does
its float arithmetic in SSE, so the CRT functions above are the main remaining float difference.

## 8. Counts

| Category | Hits (pattern) | Distinct sites | Outcome | Save | Lua | None |
|---|---|---|---|---|---|---|
| 1 Unordered containers | 52 | 25 containers (15 groups) + 1 serializer; 8 iterations | 0 | 0 (3 fixed by D18) | 1 | 14 groups + serializer |
| 2 Sorts, heaps, selection | 4 sort, 245 stable, 10 nth, 9 heap, 1 priority_queue | 269 calls | 0 | 0 | 0 | all |
| 3 Shuffles / RNG | 1 + 4 | 2 | 0 | 0 | 0 | 2 |
| 4 Pointer-ordered | 47 | 15 containers | 1 (P11) | 0 | 0 | 14 (P4 address-ordered but decision-neutral) |
| 5 Hash values as data | 15 + named | 5 groups | 0 | 0 | 0 | 5 (X4 width only) |
| 6 Lua order | - | 1 (= U4) | 0 | 0 | 1 | 0 |
| 7 CRT math | 46 + 11 | 29 lines transcendental, 28 exact | 29 lines (rare) | via state | 0 | 28 |

## 9. Sample check (2026-11-10)

Ten sites drawn with `random.Random(20261110).sample` from the 51 site IDs above and re-read at the cited lines: U12,
S4, S5 (`CvMilitaryAI.cpp:2616-2618`, `PrSortByExperience`), H2, P4, P10, P15, X1, X4, M2 (`CvGame.cpp:5756`). All ten
match the table (one correction made while checking: S4's `resultSet` is the `vector<int>` of
`GetPlotsUnderRangedAttackFrom` at `:5596`, not the function at `:5576`).

## 10. How to re-run

Counts above come from a scan of the six source folders at `e9d0c4837` with line comments (`//...`) stripped, one
regex per line as listed under each category. Equivalent with git (from the fork, no checkout needed):

```
git grep -nP '<pattern>' e9d0c4837 -- CvGameCoreDLL_Expansion2 CvGameCoreDLLUtil FirePlace CvGameDatabase CvLocalization CvWorldBuilderMap
```

(`git grep` does not strip comments, so its counts can be a few higher.) Then, for every unordered or pointer-keyed
container found, list its uses by name and classify each `begin(` / range insert / serializer; for every sort, check
the comparator for pointer comparisons and for non-strict `<=`.

## 11. CRT maths measured on real inputs (epic 1800, 2026-11-10)

Harness: `tools/drift/` (`instrument.py` wraps every `pow`/`log`/`log10`/`atan2`/`exp`/`sin`/`cos`/`tan` call of the
DLL in a throwaway build and logs arguments and result as IEEE bits; `mathcheck.cpp` built twice by
`build_mathcheck.py`, 32-bit like the shipping DLL (clang-cl `-m32 -msse3 /fp:precise /MD`, VC9 headers, msvcr90
9.0.30729.9635) and x64 like the x64 DLL (MSVC 14.43 + UCRT); `harness.py` evaluates every captured input in both).
Validation [L]: each harness program reproduces its own DLL's logged result bit for bit on every call (Standard: x86
1,007,890 of 1,007,890, x64 984,463 of 984,463), so it calls the same function the DLL calls.

Real inputs, Standard Continents 1001/2002 turns 0-102 (release instrumented builds; x86 log), per call line [L]:

| Site | Function (arg types) | Calls | Distinct inputs | Inputs differing | Max ulp | Integer flips at the call site |
|---|---|---:|---:|---:|---:|---|
| `CvUnitCombat.cpp:3105` | `pow(double, 4.0)` | 856,555 | 12,771 | 13 | 1 | 0 (every logged `DoDamageMath` of turns 95-102 recomputed: 137,711 calls, 0 damage differences) |
| `CvTreasury.cpp:539` | `pow(d, d)` | 75,762 | 558 | 0 | 0 | n/a (summed as double) |
| `CvCity.cpp:27838` | `pow(d, 1.5)` | 27,591 | 136 | 0 | 0 | 0 |
| `CvGameCoreUtils.cpp:1271` | `pow(d, d)` | 16,087 | 66 | 0 | 0 | 0 |
| `CvCityStrategyAI.cpp:2264` | `pow(d, d)` | 12,484 | 54 | 0 | 0 | n/a |
| `CvPlayer.cpp:46538` | `pow(d, d)` | 6,678 | 12 | 0 | 0 | 0 |
| `CvMinorCivAI.cpp:15554` | `pow(d, d)` | 4,780 | 1 | 0 | 0 | 0 |
| `CvCity.cpp:17325` | `pow(d, d)` | 3,279 | 12 | 0 | 0 | n/a |
| `CvPolicyClasses.cpp:4842` | `pow(d, d)` | 2,734 | 7 | 0 | 0 | 0 |
| `CvTechAI.cpp:392` | `pow(d, d)` | 621 | 23 | 0 | 0 | n/a |
| `CvGameCoreUtils.cpp:1406` | `pow(d, d)` | 416 | 4 | 0 | 0 | 0 |
| `CvUnitClasses.cpp:1784/1787/1795` | `pow(d, d)` | 215 each | 52 / 44 / 1 | 0 | 0 | 0 / 0 / n/a |
| `CvGame.cpp:5740`, `:5749` | `log(float)` | 128, 102 | 8, 1 | 0 | 0 | n/a |
| `CvTradeClasses.cpp:6922`, `:6923` | `log(double)` | 6, 6 | 3, 4 | 0 | 0 | n/a |
| `CvMinorCivAI.cpp:10484` | `pow(d, d)` | 8 | 5 | 0 | 0 | n/a |
| `CvFlavorManager.cpp:350` | `log10(float)` | 8 | 1 | 0 | 0 | n/a |
| others of section 7 (`CvDealAI`, `CvDiplomacyAI`, `CvAdvisorRecommender`, `CvTreasury.cpp:989`, `CvCity.cpp:16820/27519`, `CvTradeClasses.cpp:7311/7312`, `CvPlot.cpp:2326` atan2) | | 0 | - | - | - | not reached in these games |

Per function: `pow` 1,007,640 calls, 13,746 distinct inputs, 13 differ (all at the combat site, 1 ulp); `log` 242 / 16 /
0; `log10` 8 / 1 / 0; `atan2` not called. Tiny 0-36: `pow` 94,651 / 2,093 / 2, `log` 86 / 5 / 0, `log10` 4 / 1 / 0.
Sweep of the combat site, every strength pair 1.00 <= def <= att <= 40.00 (7,610,851 pairs) x 1,200 damage rolls
(`mathcheck sweep`, `sweep.py`): 7,522 pairs differ by 1 ulp, **0 damage flips** on either side. Line numbers in the
logs are of the instrumented source (`CvUnitCombat.cpp:3109` = 3105 here).
Conclusion [L]: on these games the CRT maths difference never changes game state; the x86/x64 divergence has another
cause (`x64-dll-port.md` 9.5: the `AttackKey` equality bug). Not covered: sites not reached (atan2, vassal, vote
counts late in a game), strengths above 40, games longer than 102 turns with logging.

## 12. Huge late-game save, turns 350-400 (epic 2200, 2026-11-10)

The first x86/x64 difference on the huge save (turn 354, Spain's `RecNavyOffensive`, `server-host.md` 10) is **not** a
site of this inventory and not a DLL build difference: the server did not loop back `sendMoveGreatWorks`, which VP's
`NetMessageExt` uses for the AI's vassal tax (`x64-dll-port.md` 9.7) [L]. Nothing was missed by the audit for this
case; the inventory covers library and build behaviour, not host interface gaps. With the server loopback added and
the `AttackKey` fix, x86 and x64 agree on all 14 stats tables for turns 350-400 [L], so no further site of this list
changed game state on that save.

Measured on the way (instrumented debug builds, turns 350-355) [L]: `CvTreasury.cpp:539` (M1a, unit maintenance)
`pow(0x4071BE665EC00000, 0x3FF245A1C0000000)` = `0x4083C94B8D5588F4` (msvcr90) vs `0x4083C94B8D5588F3` (UCRT), 1 ulp,
Egypt turn 353 in `GetSoftSupplyCap`'s probe of 85 units; the resulting unit cost is 569 on both, so no integer
changed. This is the first measured `pow` difference outside the combat site (section 11 saw 0 of 558 inputs at this
line on Standard 0-102). Logs: `tools/drift/evidence/2200/i3-*-supply-net.txt`.
