# Leaks and growth

## 1. The leak: CitySites AI logging (found 2026-09-05, fixed and verified 2026-09-07)

### What it was

`CvPlayer::GetBestSettlePlot` (`CvGameCoreDLL_Expansion2/CvPlayer.cpp:47572`) builds one CSV line per plot into a
`std::stringstream` when AI logging is on, then writes it to a log named
`"CitySites_" << getCivilizationAdjective() << "_" << turn << ".txt"` via `LOGFILEMGR.GetLog(...)`, `Msg`, `Close()`.

**The filename embeds the turn number.** `FILogFileMgr::GetLog` creates-if-necessary and caches by name, so every turn
creates a brand-new `FILogFile` for every civ. `Close()` closes the OS handle but keeps the object and its write buffer,
grown to fit the ~330 KB message, until the game is torn down. `FILogFileMgr::DeleteLog()` existed
(`FirePlace/include/FireWorks/FILogFile.h:49-50`) and was never called.

### Evidence

- Pre-fix symptom (GameId 2, huge 128x80, 14 majors): same save at t235 was 3528 MB committed before the crash and
  **1991 MB** after a reload (largest free block 1.5 -> 1608 MB): 1536.6 MB returned. Post-reload growth 6.51 MB/turn x 235
  = 1530 MB, matching to 0.4%.
- 72-82% of heap growth was one class, 256-512 KB: ~9-11 blocks/turn of ~330 KB (329 -> 1743 blocks, +459 MB over 130 turns),
  0.8-0.9 blocks per alive major per turn.
- Block fingerprint `[capacity][length][0]["0,0,"...]`; `0x2C302C30` = little-endian ASCII "0,0,". The `length` word matched
  `CitySites_*.txt` body lengths **byte for byte** (363124, 361795, 347904, 336280, 343983). 9 new blocks and 9 files per turn.
- Freed only at teardown: -629 MB heap busy, -1.26 M blocks. The logs folder held 4,861 CitySites files, 1.5 GB.
- Controlled A/B (2026-09-06, GameId 25, 79x53 = 4187 plots, 12 majors, same t15 save, 30 turns):

| | AILog=1 | AILog=0 |
|---|---|---|
| CitySites files written | 159 (21.4 MB) | **0** |
| 64K-256K blocks, t16 -> t45 | 596 -> **751** | 596 -> 599 |
| new-block samples with the "0,0," fingerprint | **52 of 58** | **0 of 7** |
| heap busy growth | 1.236 MB/turn | 0.761 MB/turn |

  152 retained blocks vs 159 files; 18.8 MB retained vs 21.4 MB written. Scales with plots and files: 0.63 MB/turn on that
  map vs 2.84 on the huge map (predicted 0.68).
- **Side finding:** AI logging changes AI decisions (the `bLogging` branch calls `PlotFoundValue(...)` fresh instead of the
  cached value; the two runs diverged after turn 25). Never benchmark AI behaviour with `AILog=1`.

### The fix: commit a26506ae8 (branch `bugfix/fix-delete-log-leak`)

*Release turn-named debug logs instead of retaining them forever* - `LOGFILEMGR.DeleteLog(pLog)` after `Close()` at all four
turn-named sites: `CvPlayer::GetBestSettlePlot` (also gained a null check), `CvGame::debugSyncChecksum` (was not even calling
`Close()`), `CvGame::SetClosestCityMapDirty` (debug-gated), `CvUnit::DumpDangerInNeighborhood` (debug-gated). Log output
unchanged. No-rebuild workaround for older builds: `AILog = 0` or `LoggingEnabled = 0` in `config.ini`.

Verification, GameId 47 (224 turns, stock huge 128x80, 12 majors / 37 alive, one session, AILog=0):

| measure | before the fix | after |
|---|---|---|
| heap growth | +4.05 MB/turn | **+0.63 MB/turn** (6.4x lower) |
| committed growth | +6.54 MB/turn (late) | **+1.48 MB/turn** |
| reload of the same state returns | 1170.7 MB (47% of heap) | **14.7 MB (0.9%)** |
| fingerprint `0x2C302C30` in sampled new blocks | 85 of 123 | **0 of 114** |

After the fix growth spreads across all 15 size classes, none above 23%.

### No second leak

The reload gap is a **fixed offset, not a rate**: 13.5 MB after 30 turns, 15.8 MB after 200 (small map, AILog=0, GameIds 25
and 40), 14.7 MB after 224 on a huge map. A 200-turn run (GameId 40) grew linearly (heap busy 0.486 MB/turn, flat within ±10%
across four phases; committed 1.006 MB/turn). The pre-fix huge-map crash reconstructs from map size and logging alone to within
14% (3528 - 1971 map gen - 655 spike = 902 MB / 235 turns = 3.84 MB/turn, against a predicted 4.36). Genuine serialized state
growth is ~0.088 MB/turn (load costs 1929.5 / 1934.5 / 1945.6 MB at t15 / t45 / t199). The "~870 MB unexplained, factor of 76"
claim of 2026-09-06 is **retracted** (it compared a 30-turn early window against a 235-turn game).

**Pre-fix headline on the 10,240-plot map:** removing CitySites cut growth from ~3.8 to ~1.5 MB/turn, moving the wall from turn
~235 to past turn 600 (log section 1).

## 2. Post-fix growth: 2.476 MB/turn on the 20,340-plot map, and who owns it

Run 15, GameId 62 (180x113 = 20,340 plots, 20 majors / 45 alive), crashed t317, 2026-09-10. Committed was 2768.7 MB at t20
growing **2.476 MB/turn**; against a ~3575 MB ceiling that predicts death at t~346, it died at t317. Fragmentation cost ~29
turns; the game was going to die between t320 and t350 however tidy the heap was.

### Per-turn components (crash run, t20-317)

| component | MB/turn | share |
|---|---|---|
| heap busy (live data) | 1.148 | 46% |
| **heap free-list** (committed, freed, never returned to the OS) | **0.723** | **29%** |
| heap overhead (allocator metadata) | 0.085 | 3% |
| non-heap private (VirtualAlloc) | 0.520 | 21% |

Heap blocks grew +8,239/turn (2.06 M -> 4.40 M); 97% of that growth is blocks of 256 bytes or less.

### Owners (reload run RunId 1789075474, fully tracked; the crash run's hook saturated)

- Blocks: heap blocks grow 10,668/turn; the hook accounts for **10,653 = 99.9% DLL**.
- Bytes: **56% DLL** (0.499 of 0.898 MB/turn heap busy); the rest is engine-side blocks in the 1-8 MB classes.

| subsystem | MB/turn | blocks/turn |
|---|---|---|
| DangerPlots | 0.305 | **7,555** |
| HomelandAI | 0.198 | 2,351 |
| PlayerTurn | 0.061 | 1,631 |
| TacticalAI | 0.119 | 1,135 |
| CityTurn | 0.082 | 575 |
| EconomicAI | 0.084 | 555 |
| Serialization | **-0.326** | **-2,234** |

**Qualified 2026-11-10 (epic 1300; replaced the unqualified "the largest growth site" reading):** the 71% figure below is for GameId 62's reload run (RunId 1789075474, t317-344). That run starts with `m_DangerPlots` empty (it is not serialized), so part of DangerPlots' rate there is likely refill after the load [I]. In the same run the PlayerTurn tag totalled only 1,631 blocks/turn, so DangerPlots really was the largest there. In fresh headless games (t20-200, see "Headless baselines" below) the largest site is PlayerTurn's map-node insert `00114AE3` (42% Huge, 52% Standard) and DangerPlots is second. Which is largest depends on the game phase and on whether the game was reloaded; both are targets.

**One call site is 71% of all heap-block growth in the process:** RVA `00100458`, tagged DangerPlots, 459,788 live blocks of
~38 bytes, +7,618/turn; it symbolises (with the `/OPT:ICF` alias caveat) to `std::vector<std::pair<PlayerTypes,int>>::_Insert_n`
- the per-plot vectors inside `CvDangerPlotContents`. Across all tags the site is 510,495 blocks and 80% of block growth.

**Mechanism, by design:** `CvDangerPlotContents::reset()` (CvDangerPlots.h:42) does `m_apUnits.clear()` and only reallocates when
`capacity() > 5`, then `reserve(5)` - so a vector that was ever written keeps a 40-byte heap block for the rest of the game; and
`m_apCities` is never pruned ("cities are fairly static so don't care"). Ceiling on this map 20,340 plots x 33 alive x 2 =
**1.34 M blocks**; 510 K at t344 (38%), still climbing linearly. Not a leak (reachable, serves a cache) but it behaves like one for
fragmentation. `m_DangerPlots` is not serialized (CvDangerPlots.h:126), so a reload rebuilds it empty: 565,053 of the 1.10 M blocks a
reload drops (51%) are these vectors.

### Earlier, smaller-map view (GameId 47 t241-250, 2026-09-08)

Heap busy +1.26 MB/turn, of which the hook named **0.45 MB/turn (36% DLL)**: DangerPlots 0.26, HomelandAI 0.12, TacticalAI 0.10,
PlayerTurn 0.07, CityTurn 0.06, Serialization -0.21. DLL live at t250: XmlDatabaseLoad 60.0 MB (fixed), Serialization 28.6 (shrinking),
DangerPlots 27.1 (23.1 MB of it is 37 x 640 KB per-player plot arrays), Untagged 25.9, TacticalAI 3.0 live / **44.0 peak**, HomelandAI
2.0 / 3.1. DLL live 151.8 MB vs peak 190.2 MB: ~38 MB of transient headroom, mostly TacticalAI.

Churn: 14.1 M DLL allocations and ~1.4 GB per turn on the 10,240-plot map (0.0276% survives); **59.9 M and 5.13 GB per turn** at t344 on
the 20,340-plot map (~0.03% survives). EconomicAI's `vector<int>::_Buy` alone moved 1.27 GB over ten turns while holding 362 KB. A
performance finding, and the reason the LFH is on (and therefore why UMDH failed).

## 3. The free-list tax

Nearly a third of per-turn committed cost (0.723 of 2.476 MB/turn) is free-list: memory the allocator holds and the program does not
use. Heap committed-but-unused at t317 was **327 MB** (275.0 free-list + 52.2 overhead, 12.7% of the heap, up from 87.4 MB / 4.4% at t20);
of that, genuine can't-give-it-back fragmentation is **~120-135 MB** (the confound is the DLL's transient headroom, which grew 1 -> 82 MB
over t20-255 as TacticalAI's spike grew). As a share of the ceiling it is the 332.6 MB of heap slack at t314, ~8% of the address space.

The mechanism (measured 2026-09-17): free lists grow when a **peak** forces new heap segments and the memory is then freed - one AI turn
left the CRT heap with -0.2 MB live data but +22 MB of free list and 3 more segments. Small, numerous blocks set this tax, which is why
the DLL's liability is block count, not bytes.

**What a reload does** (G62, t317 pre-crash vs t318 one turn after reload; a bare load has not yet paid the first-played-turn spike, which
on this map is **+1105 MB committed in one turn**):

| measure | t317 pre-crash | t317 loaded | t318 (1 turn in) |
|---|---|---|---|
| committed | 3504 MB | 2009 MB | 3114 MB (-391) |
| heap busy | 2255 MB | 1303 MB | 2157 MB (-98) |
| heap blocks | 4,399,836 | 1,885,224 | 3,299,600 (**-1,100,236**) |
| heap free-list | 275 MB | 49 MB | 115 MB (-160) |
| largest free hole | **3.97 MB** | 1584 MB | **338.69 MB** (85x) |

The reload returned only ~390 MB but rebuilt contiguity; it deferred the wall by tens of turns and did not remove it (the accumulator
refills at the same rate; after reload committed rose ~1.8 MB/turn). At 2.476 MB/turn the 160 MB free-list + 98 MB data a reload
returns is worth **~100 turns**: cutting block growth both cuts the tax and the growth rate that sets the death turn.

### First-played-turn spike (one-time per load, does not compound)

+655 MB (huge pre-fix, twice), +400.9 (small t45->46), +486.7 (small t199->200), +443.6 MB heap / +606.3 committed (G47 t240->241,
of which DLL +30.1 MB = 6.8%), heap +446 on G47 t227->228 (CRT heap 4 +371.9, process heap +39.4, Lua +33.3, heap 10 +1.7; ~86% engine
memory), +1105 MB committed on the 20k map. The first measurement showed no CSV strings in it - it is not the leak.

### Headless baselines and the full site list (epic 1300, 2026-11-10) [L]

Headless host (`civ5-headless`, `c5h`), Release-5.4.8 DLL with the memory-diagnostics instruments (eeb9305e1, plus
local bf9a05b2d adding `VP_MEMHOOK_SITES` so all sites are logged, not the top 64 by bytes), `VP_MEMIMPORTS=0`
(MemoryImports crashes the host at shutdown, cause not investigated), Continents fixed seeds 1001/2002, 200 turns,
slopes over t20-200. The instruments are behaviour-neutral: base vs instrumented `stats.db` identical on Standard
(356,105 rows) and Huge (603,643 rows). Detail: `tracker/issues/1301-i0-ab-harness-and-baseline.md`; tools `tools/i0/`.

| | Standard 80x52, 8 majors | Huge 128x80, 12 majors |
|---|---|---|
| committed MB/turn | 0.315 | 0.378 |
| heap busy blocks/turn | 1,516 | 2,668 |
| DLL live blocks/turn (share of heap block growth) | 1,499 (99%) | 2,610 (98%) |
| per tag, blocks/turn | PlayerTurn 710, DangerPlots 337, HomelandAI 175, TacticalAI 159 | PlayerTurn 1,034, DangerPlots 717, HomelandAI 414, TacticalAI 263 |

- Same ranking among the three AI tags as GameId 62, ~10x lower absolute rates (half the plots, fewer players, earlier
  turns, no EXE).
- **New: in these runs the largest single site is not DangerPlots.** RVA `00114AE3`, a `map<ImprovementTypes,int>`-layout node insert
  (24-byte nodes; `/OPT:ICF` may alias the type), under the PlayerTurn scope: 1,109 blocks/turn on Huge, **42% of DLL
  block growth (52% on Standard)**. The September runs logged only the top 64 sites by bytes, which missed most
  small-block sites (at Standard t14 they covered 0.6% of DangerPlots' live blocks); tag totals were complete.
  **Source (2026-11-10, tracker 1306) [C source, L numbers]: `CvPlayer::m_ReplayData`**, the per-player replay history (`std::map<CvString, std::map<unsigned int,int>>`, CvPlayer.h:3696; ICF folds its node insert with `map<ImprovementTypes,int>`). `CvPlayer::doTurn` -> `GatherPerTurnReplayStats` (CvPlayer.cpp:10514, 49332) adds one 24-byte node per dataset per turn: 28 datasets x every alive player (majors, city-states, barbarian), never erased: Huge 1,022 blocks/turn measured vs 28 x mean alive players = 1,020; Standard 699 vs 699.5. `CvGame::updateScore` (CvGame.cpp:1894) adds `REPLAYDATASET_SCORE` for all 63 slots, alive or not (~63/turn, the Untagged share; attribution by source and arithmetic). At Huge t200 replay history is 224,874 of the site's 237,927 live blocks: **27% of the DLL's live blocks but 3.7% of its bytes** (~5.3 MB data, ~7.2 MB with heap headers). **It is live data, not a leak:** saved (CvPlayer.cpp:45871), copied into the end-game replay (CvReplayInfo.cpp:111-170), read by Lua (`GetReplayData`, `GetScoreHistory`) and by espionage (military might 10 turns back, CvEspionageClasses.cpp:2091). It grows linearly with turns x alive players for the whole game (inferred ~550k nodes by t500 on Huge). A block-count fix would store each dataset contiguously instead of one map node per value, with the save format kept.
  **Fixed 2026-11-10 (epic 1300, task 1307) [L headless Standard]:** DLL fork branch `i0-replaydata` (64b889b8a, only `CvPlayer.{h,cpp}`): `TurnData` is a vector of (turn, value) pairs sorted by turn, with the map's read interface, and it serialises to the same bytes as the generic `std::map` path. Site `00114AE3` fell from 777.4 to 14.5 blocks/turn on Standard (live blocks at t200 164,791 -> 12,212). DLL live-block growth halved, from 1,499 to 736 blocks/turn. Huge is inferred at about -1,080 blocks/turn, not measured. Gates: A/B identical (356,105 rows); every player's replay-history bytes are identical in base and fix saves at t20/160/200; each DLL loads the other's saves at t20 and t200 and continues identically; the Lua readers are identical (152,579 values); the dumped `CvReplayInfo` is byte-identical (631,382 B). Whole saves are never byte-identical between two host processes, base vs base included: game GUID, heap garbage, great-person strings and the DLL version string differ. Detail: `tracker/issues/1307-i0-replay-history-storage.md`.
- DangerPlots' vector site `00105158` (an ICF alias named `vector<OptionWithScore<int>>`) also grows under HomelandAI
  and TacticalAI (~124 blocks/turn on Huge) through `AddKnownAttacker` -> `UpdateDangerSingleUnit` (CvPlot.cpp:11663,
  CvUnit.cpp:6333, CvCity.cpp:34992) [C path, I attribution].

## 4. Recommended in-place fixes (status 2026-11-10: items 2 and 3 and the replay-history site done, epic 1300)

**Combined result, epic 1300 (2026-11-10) [L headless]:** DLL fork branch `i0-growth-fixes` @ b9311e2ee (pushed to
the DLL fork, pinned in `deps/i0.lock`): DangerPlots inline lists, replay history in one block per
dataset, pathfinder child lists, tactical child positions. Six A/B diffs against Release-5.4.8 identical (Standard
seeds 1001/2002 and 3003/4004, Huge, instrumented builds; 348,876-603,643 rows each). Instrumented, slopes t20-200:

| | Standard before | after | Huge before | after |
|---|---|---|---|---|
| DLL live blocks/turn | 1,498.7 | 381.3 (-75%) | 2,610.0 | 757.1 (-71%) |
| heap busy blocks/turn | 1,512.5 | 395.0 | 2,668.5 | 815.5 |
| DLL live blocks at t200 | 524,141 | 290,101 | 830,449 | 418,447 |
| committed MB/turn | 0.354 | 0.271 | 0.378 | 0.341 |

Huge per tag: DangerPlots 717 -> 69, PlayerTurn (replay) 1,034 -> 16, Pathfinder 20 -> 0, TacticalAI 263 -> 206,
HomelandAI 414 -> 369 (the rest is live data, see item 3). The separate fixes add up (-1,853 vs -1,852.9 combined).
Bytes saved are small (heap busy at t200 -3 to -6 MB): the win is block count, i.e. the free-list tax and
fragmentation (section 3), not megabytes. Wall time unchanged (fixed 0.3-1.1% faster, within noise). Not measured:
maps beyond Huge, more than 200 turns, the GameId 62 20,340-plot map.

**Re-run on the pinned ship branch (2026-11-10 15:05-15:29 EST, task 2404) [L headless]:** the growth fixes still
hold on DLL fork branch `i0-ship` @ e6f904ad6 (the five growth commits + crash fixes 040b063ea/27176dbfe +
50a389682 AttackKey, 1512a94b4 squad order, 5810b3c71 GameInfoTypes order, e6f904ad6 replay count). Instrumented
build by 1305's recipe: local throwaway branch `throwaway-2400-c-measure` 9168772a0 = `i0-instr-sites` (bf9a05b2d)
merged with e6f904ad6 (clean merge, never pushed), release, DLL SHA-256 `1E8F665D...F662C`
(`<pins_dir>\dlls\measure-2404-9168772a\`). Same specs, seeds 1001/2002, 200 turns, `VP_MEMIMPORTS=0
VP_MEMHOOK_SITES=2048`, `memgrowth.py --from 20`; before = 1305's before (1301's `base-instr-sites` runs, reused
unchanged). Runs `<pins_dir>\runs\2400c-measure-{std,huge}200\measure`; readouts
`tools/d10/evidence/2404/measure-{std,huge}200-growth.txt`.

| | Standard before | i0-growth-fixes (1305) | i0-ship (2404) | Huge before | 1305 | 2404 |
|---|---|---|---|---|---|---|
| DLL live blocks/turn | 1,498.7 | 381.3 (-75%) | **372.9 (-75.1%)** | 2,610.0 | 757.1 (-71%) | **775.3 (-70.3%)** |
| heap busy blocks/turn | 1,512.5 | 395.0 | 384.1 | 2,668.5 | 815.5 | 839.8 |
| DLL live blocks at t200 | 524,141 | 290,101 | 263,475 | 830,449 | 418,447 | 419,014 |
| committed MB/turn | 0.354 | 0.271 | 0.254 | 0.378 | 0.341 | 0.323 |
| DangerPlots / PlayerTurn / Pathfinder blocks/turn | 337.3 / 709.7 / 13.7 | 54.1 / 8.8 / 0.0 | 52.4 / 9.9 / 0.0 | 717.1 / 1,034.3 / 20.1 | 68.9 / 15.7 / 0.0 | 73.8 / 15.3 / 0.0 |

Caveat: i0-ship's `AttackKey` fix changes the game from turn 38 (Standard) and turn 34 (Tiny) on, so the 2404 runs
are a different game from turn 38 on than the before and 1305 runs (same map and seeds); the -75 % / -70 % hold for
that game, one run per arm. The instrumented i0-ship build plays the Standard game identically to the uninstrumented
i0-ship DLL (`ab.py diff`, 15 tables, 348,086 rows, IDENTICAL); on Huge that identity was not checked.

**Live check (2026-11-10, task 1305) [L]:** on Goldberg instance c5h-b, the huge save copy (t350), the fixed DLL and
the base DLL (both on instance-aware-instruments 440878983) each loaded and played 4 AI turns 350 -> 354 with the
same leader screens in the same order. Process memory at the turn boundaries (committed MB, fixed / base): t350 2407 /
2435, t351 2490 / 2483, t352 2490 / 2504, t353 2511 / 2511, t354 2501 / 2510: within +-28 MB run noise; 4 turns of
process memory cannot resolve a ~0.04-0.08 MB/turn difference. The live run proves load and identical play, not the
growth number. Turn 355 on this save halts any 5.4.8 DLL in a precondition dialog (tracker bug 0080: unguarded
`getCityEventChoiceInfo(-1)` in `CvPlayerEspionage::LogSpyStatus`, CvEspionageClasses.cpp:554, logging on or off).
Evidence: `<pins_dir>\evidence\1305\`; plan and steps `tools/i0/live-plan-1305.md`.

(2026-11-10: epic 1300 is implementing items 2 and 3 plus the replay-history site above; results are recorded below each item.)

**Item 2 done 2026-11-10 (epic 1300, task 1302) [L headless]:** DLL fork branch `i0-dangerplots` (ffecf47b9, plus
c20761449, a push_back aliasing guard), only `CvDangerPlots.h`. The three per-plot `std::vector`s became
`CvInlineVector<pair<PlayerTypes,int>,1>`: one entry is stored inline, longer lists spill to the heap, and `clear()`
releases the spill. That is option (b) with N = 1. The entry stays 64 B, so per-player arrays do not grow. Why N = 1: 90-97% of lists are empty at any
moment and 59-97% of the rest hold one entry; N = 2 would add ~14.6 MB on the 20,340-plot map. A/B identical to
Release-5.4.8 on Standard (356,105 rows) and Huge (603,643 rows), final state and replay files byte-identical; no
measurable wall-time cost (5.163 vs 5.170 s/turn concurrent). Block growth, slope t20-200:

| | Standard before | after | Huge before | after |
|---|---|---|---|---|
| DangerPlots blocks/turn | 337.3 | 54.1 | 717.1 | 68.9 |
| DangerPlots live blocks at t200 | 68,281 | 7,396 | 170,837 | 22,369 |
| DLL live blocks/turn, all tags | 1,498.7 | 1,178.5 (-21%) | 2,610.0 | 1,872.2 (-28%) |

The residual DangerPlots slope is spill blocks that track current wars and are freed at the next update, not
retention. Committed MB/turn moved within run noise (0.378 -> 0.369 Huge). Not verified: the 20,340-plot map, a
live game. Detail: `tracker/issues/1302-i0-dangerplots-block-growth.md`.

From the log's next-steps (2026-09-08, revised 2026-09-10) and the 2026-11-09 architecture note, which says the same growth is
fixable in place without the 64-bit move:

1. **Count blocks, not megabytes.** The whole DangerPlots site is 16.8 MB, nearly worthless as bytes, but worth ~100 turns as committed growth.
2. **Fix `CvDangerPlotContents::reset()`** - one site, 71% of block growth. Options in rough order: (a) free the vector in `reset()` when
   empty for N turns; (b) replace the three vectors with a fixed-capacity inline array (~27 MB more bytes in the per-player arrays, zero small
   blocks - measure, the large-block classes were also starving); (c) allocate `CvDangerPlotContents` lazily. Also question whether
   city-states need a full per-plot danger map (`m_DangerPlots`: 37 x 640 KB on the huge map, one per alive player incl. minors).
3. HomelandAI (2,351 blocks/turn) and TacticalAI (1,135 blocks/turn, 44 MB transient peak) are the next targets.
   **Done 2026-11-10 (epic 1300, task 1303) [L headless Huge, C source]:** most of these two tags' growth is **live game
   state or bounded caches, not retention.** A depth-2 hook build (throwaway, `VP_MEMHOOK_DEPTH=2` + `/Oy-`) named
   each STL site's caller. **Per-tag slopes mislead:** a block reallocated under tag X frees one of tag Y. For example,
   CvPlot's invisible-visibility vectors are +26.5/turn under HomelandAI but 0 net over all tags. Judge a site by its
   all-tag slope. Two retention sites were fixed (fork commits a4678f41b and b9311e2ee on `i0-growth-fixes`):
   - pathfinder node children: each `CvAStarNode` kept its `m_apChildren` capacity after `clear()` (Pathfinder tag
     20.1/turn, 24,018 blocks at t200); now one buffer per pathfinder, Pathfinder 0.0/turn;
   - tactical-position `childPositions` not wiped on a hard reset (14.0/turn).

   Explained, not fixed (Huge, blocks/turn, all-tag net):
   - `CvMap::GetPlotsAtRangeX` line-of-sight caches, 133.7. This is 1301's site 0011DA54 under HomelandAI, not the
     pathfinder. Bounded at 4 x plots, 97% full at t200.
   - CvEconomicAI exploration sets, 74.3.
   - Per-city and per-unit arrays for new cities and units created under the TacticalAI and HomelandAI scopes,
     ~85 + ~78.
   - CvBuilderTaskingAI route maps, ~35, rebuilt each turn.
   - The danger vectors (1302) and the replay history (1307).

   Huge before -> after with 1303 alone: TacticalAI 263.3 -> 248.5 blocks/turn; HomelandAI unchanged at 413.6; DLL live
   blocks at t200 830,449 -> 802,316. A/B identical on Standard and Huge. The 44 MB TacticalAI transient peak was not
   investigated; no cheap behaviour-neutral change was found. Detail:
   `tracker/issues/1303-i0-homeland-tactical-growth.md`.
4. Reduce allocation rate (14-60 M allocations/turn) - a performance win, starting from `MemHookSites.TotalMB`.
5. Cheap signals not yet built (next-step 12): split `MemFreeBlockHistogram` low/high; log `LargestFreeKB` against the largest live block;
   name the flat 367 MB non-heap reservation; move the instrument's reserves `MEM_TOP_DOWN` (the tables were moved 2026-09-15).
6. Lua: the EUI DB-row cache (~17 MB of copies, never evicts) is the tractable Lua target; a trade-off, not an obvious win.
7. Ruled out as memory fixes: replay data (8.12 MB, 0.23% of committed) *as bytes*; **corrected 2026-11-10:** as block count it is the largest DLL growth site in fresh games (27% of DLL live blocks at Huge t200, ~1,000 blocks/turn; section 2, "Headless baselines"), so by item 1's rule it is a target after all; per-plot arrays (already slab-allocated, CvMap.cpp:605).

Relevance to the 64-bit project (2026-11-09 note): moving the DLL out frees ~300 MB and removes the DLL's 56% of growth bytes / 99.9%
of new blocks; the wall moves from ~t320 to past ~t700 on the GameId 62 map.

Sources: `research/sources/memory-notes/civ5-citysites-log-leak.md`, `civ5-memory-leak-findings.md` (superseded in part, see its
header), `civ5-memory-investigation-log.md` (sections 1, 3 "The DLL owns 8.9%", "Run 15", "Fragmentation"; section 4; section 6),
`civ5-64bit-architecture-options.md`.

## 5. Two crash bugs in stock 5.4.8 found on the way, and their fixes (one-off 9005, 2026-11-10)

Both are in Release-5.4.8 and in upstream master 59c50bc00 (the fork's `upstream/master` as last fetched,
2026-11-06; read from that ref, not re-fetched) [C]. Fixed on DLL fork branch `i0-bugfixes` (`deps/i0-bugfixes.lock`),
one commit each, on top of `i0-growth-fixes` b9311e2ee. Not shipped; the owner decides (tracker 0081). DLLs: base =
b9311e2ee (`<pins_dir>\dlls\i0head-b9311e2e\`, SHA-256 `D0E0A5B1...`), fixed = 27176dbfe
(`<pins_dir>\dlls\i0bugfixes-27176dbf\`, `AE14D5D7...`). Evidence: `<pins_dir>\evidence\9005\`.

**Bug 0080, counterspy without a focus (fix 040b063ea).** Cause [C]: `CvPlayerEspionage::DoTurn` calls
`LogSpyStatus()` on every game turn divisible by 5, logging on or off (the text is formatted before
`LogEspionageMsg` checks `GC.getLogging()`). For `SPY_STATE_COUNTER_INTEL` under `MOD_BALANCE_VP` it formats
`GC.getCityEventChoiceInfo(eMission)->GetDescription()` with `eMission = pSpy->GetSpyFocus()`, unguarded
(CvEspionageClasses.cpp:554); a counterspy with no focus has `NO_EVENT_CHOICE_CITY` (-1), and the precondition
`e > -1` in `CvGlobals::getCityEventChoiceInfo` ends the game. Fix: the counter-intel case checks
`eMission != NO_EVENT_CHOICE_CITY` and otherwise logs "COUNTERING INTEL in <city>. No Current Mission.", as the
gathering-intel case already did. The precondition is unchanged.
- Reproduced [L headless]: the huge save copy `C5H-VP8P-HUGE_0350` (from `evidence\1305\live\R1\collect\saves\`,
  SHA-256 `C499D3C6...`, its own two mods, host defaults) with the base DLL: turns 350-354 play, then the host dies
  with 0xC000001D (the precondition's trap; the run's `nopopups.please` suppresses the dialog) in turn 355;
  symbolised: `CvPreconditionFailedFormat` <- `getCityEventChoiceInfo` <- `LogSpyStatus` (:554) <- `DoTurn` (:435)
  <- `CvPlayer::doTurnPostDiplomacy`. The 32-bit headless host reaches it by play; no Lua construct was needed.
- The state [L]: checkpoint extra `tools/i0/counterspies.lua` on the fixed run: player 0 (not human in the
  autoplay) had one counterspy (agent 3, Tzintzuntzen) with focus -1 on turns 351-354; from turn 355, none. Why that
  AI counterspy had no focus was not investigated. The Standard 200-turn fixed-seed game never reaches the state
  with a 5th-turn log (its base run finishes) [L].
- Fixed [L]: the fixed DLL plays the same save 350 -> 361, through turns 355 and 360.

**Bug 0078, divide by zero after a load (fix 27176dbfe).** Cause [C]: `CvWorldInfo::Serialize` does not include
`m_iEstimatedNumCities`, which `CacheResults` reads from the `Worlds` table (`EstimatedNumCities`: 13, 26, 39, 52,
80, 132 for Duel..Huge). On load, `CvPreGame::readArchive` reads `s_worldInfo` with `CvWorldInfo::readFrom`, which
leaves that field as it was before the load, and `CvPreGame::worldInfo()` re-caches from the database only when the
world size ID differs. In the headless host it was the constructor's 0, and `CvReligionAI::ScoreBeliefForPlayer`
(CvReligionClasses.cpp:10086; also `ScoreBelief`, :7921) divides by it. In the real game it is whatever the pregame
held before the load: 0 if nothing had cached a world size, or another size's value [I, not checked live], so
religion AI after a load can differ from continuous play. Fix: `CvWorldInfo::readFrom` sets
`m_iEstimatedNumCities` from the `Worlds` row of the loaded ID (`DB.SelectAt(kResult, "Worlds", GetID())`); the save
format and every save byte are unchanged. Second line: both divisors use `max(1, ...)`, which cannot change a
result while the value is valid (every row is >= 13).
- Reproduced [L headless]: epic 1300's turn-160 save (`runs\1307-ab-std200\base\saves\Turn_0160 AD-1000.Civ5Save`,
  `8D78FE5D...`, toolkit game dir, `--no-mods`) with the base DLL: 0xC0000094 at DLL+0xA55771 in the first turn,
  symbolised to `ScoreBeliefForPlayer` :10086 <- :10616 <- `ScoreBelief` :7859.
- Fixed [L]: the fixed DLL plays it 160 -> 172. A throwaway build with the `readFrom` change only (no `max`) also
  plays 160 -> 172, and its `stats.db` is identical to the full fix's (15 tables, 41,431 rows): the reload alone
  fixes it and the clamp never engaged.

**No change elsewhere [L headless, 2026-11-10].**
- A/B (`tools/i0/ab.py`), Standard Continents, seeds 1001/2002, 200 turns, base vs fixed: IDENTICAL, 15 tables,
  356,105 rows; neither run met either bug. 2.693 / 2.677 s/turn.
- Round trip (`tools/i0/roundtrip.py`, `../06-tooling-and-workflows/headless-save-comparison.md` part 1: load, save
  before any turn, compare the pre-body raw and the inflated body, only the DLL version string skipped): identical
  between the two DLLs on the Standard saves of turns 20, 160 (the base host then crashes in turn 161, after the
  resave) and 200, and on the huge t350 save. Pre-body 15,149-15,202 bytes, bodies 16.9-22.1 MB.

Not verified: either fix in the real game; whether the real front end leaves `s_worldInfo` stale or zero before a
load; why the AI counterspy had no focus.

**On the pinned builds (2026-11-10 15:00-15:42 EST, task 2404) [L headless].** x86 `i0-ship` e6f904ad6
(`2C216FC9...`) and x64 `x64` 29f15ac38 (server bin of the team-2400 build, DLL `E3F5A4DE...`):
- 0078: the turn-160 save (copy `E2400C-STD_0160`, `8D78FE5D...`) plays 160 -> 172 in c5h (toolkit game dir,
  `--no-mods`) and in the x64 server (`run_server.py --load ... --turns 172`). No unfixed x64 control was run.
- 0080: the huge save copy (`E2400C-HUGE_0350`, `C499D3C6...`, the epic 2000 setup of `tools/drift/run_huge.py`)
  plays 350 -> 400 on x86 i0-ship and 350 -> 361 in the server. Control: the official stock 5.4.8 DLL (`4E91C58A...`)
  dies the same way as 9005's base, 0xC000001D after the turn-353 update (host log), stats rows up to turn 354 plus
  partial 355 rows, last checkpoint 352. x86 i0-ship and the x64 server agree on every stats table for turns 350-360.
