# options/ - the patch's options file (one-off 9042, decisions D89 and D88)

The loader of `VP64Options.ini`, shared by the x86 proxy and the x64 server. Players' description: `docs/OPTIONS.md`.
Design, rules with reasons and the census of older settings: `research/07-architecture-decisions/options-file.md`.

| Path | What |
|---|---|
| `include/vp64_options.h` | The API: the option ids and their table (`Definition`), `Options` (values in force), `Parse`, `DefaultFileText`, `LoadFile`, `CreateDefaultFile`, `DefaultPath`, UTF-8 conversions. No STL type, no C++11 library header |
| `src/vp64_options.cpp` | The implementation and the table of options (key, section, default, range, help text written into new files) |
| `tests/test_options.cpp` | Unit tests, 1170 checks on 2026-11-26 (ticket 0808: the `RoadLayout` help line's bridge sentence of ticket 0806 removed again, bridges now follow the layout; no check added; x86 and x64 trees); 838 checks since one-off 9081 (`TestTitles`, `UiDrawListGrowth` made a reserved row; 2026-11-22 EST, both trees, not ASan); 749 checks since ticket 4783's `ServerCombat` (row 28: its range, default 1, warnings, the default file and the documentation; 2026-11-20 19:59 EST, both trees and ASan); 724 checks since epic 0455's rows 23-27 (2026-11-20 11:20 EST, both trees, the 0455 landing: `FixUnitPlacement`, `GarrisonVisible`, `ShipsMoored`, `FixAirBases`, `AirInterceptPatrol`, their states, ranges, `Needs ServerUnits = 1` in the help, the block's order, `GarrisonVisible`'s log lines; 716 with rows 23-26 only, in 4983's scratch copy); 681 since ticket 4934's `FixRoadLayout` (2026-11-20, both trees: the first bugfix row, its states and log line); 673 since ticket 4938's `VisibleAreaOutline` (2026-11-19, both trees: the first extra row, its states, warnings and log line; 639 since ticket 4931's `[Extras]` (2026-11-19, both trees and ASan: the extras table rules with 24 broken-table negatives and a control, the two switches (both off or on leave every other option at its default), `ExtraOf`, `Extra`, `DescribeExtra`, the `[Extras]` block of the default file; every default-text buffer 32768, the bound `n < 30000`); this file said 164 checks before (160 until issue 0185 made `WhiteFogMask` default to 1, D136, 2026-11-14; 143 until issue 0172 added `[Renderer] WhiteFogMask`, 2026-11-14; 126 until issue 3305 added `[Renderer] ServerRenderer`, 2026-11-13); arguments: `docs/OPTIONS.md` (every key must be described) and a scratch folder |
| `CMakeLists.txt` | Library `vp64options` and `test_options` in both trees of the root `CMakeLists.txt` |
| `test_asan.py` | The tests under MSVC `/fsanitize=address`, with a canary that must be caught |
| `evidence/0185/acceptance.txt` | Issue 0185 (D136): `WhiteFogMask` on by default, the acceptance and the test output |

Who compiles `src/vp64_options.cpp`: `proxy/build_proxy.py` (with the proxy's flags), `server/build_server.py`
(`obj/options`, /W4 /WX), and the CMake trees for the tests. Callers: `proxy/src/proxy_options.cpp`,
`server/src/srv_options.cpp`.

## Tests

```
python ipc/build.py --target test_options
build/x86/Release/options/test_options.exe docs/OPTIONS.md build/x86/Release/options/scratch
build/x64/Release/options/test_options.exe docs/OPTIONS.md build/x64/Release/options/scratch
python -I perf/quietlock/quietlock.py run --who <id> --what "options ASan" -- python options/test_asan.py
```

`python ipc/build.py --test` runs them through ctest with the rest of the tree. Start the executables through
`quietlock.py run` (COMMON rule 16) with absolute paths. Result 2026-11-13: 126 passed in both trees and under ASan.

## Adding an option

1. An id before `kCount` in `include/vp64_options.h`.
2. Its row in `kDefs` in `src/vp64_options.cpp` (section, key, type, default, range, help lines under 100 characters,
   category, needsKey, title). An option outside `[Extras]` has `kCatNone, NULL` and its key does not start with
   `Enable`, `Fix` or `Experiment`. Every row with a key has a short `title` (one-off 9081, the owner 2026-11-21: 1-45
   characters, no trailing period; `TestTitles`): the label of the installer's options page, which takes it from
   `server.exe --write-options-schema` (`tools/installer/options_schema.py`); it is not written into the options file.
   Write the help's value lines as `  <n> = ...` or `  <a> to <b> = ...` covering the whole range (an option that is
   not 0-1 is refused by the installer build otherwise), and "needs <Key> = 1" when it only works under a 0-1 option.
2a. An extra (ticket 4931, epic 0454; `docs/extras/flags-and-messages-design.md` section 1): it fills one of the ids the
   MC reserved (an empty-key `[Extras]` row; ids 18-21 today) or takes a new one from the MC (K4). Section `Extras`;
   `category` `kCatBugfix` (key starts with `Fix`) or `kCatVisual` (key starts with neither `Fix` nor `Enable`); an
   `Experiment...` key is either, defaults to 0 and is renamed before the maintainer approves it; `needsKey` names the extra it
   needs (an `[Extras]` bugfix or visual row, no cycle), or `NULL`. Default 0, and a range that holds 0 (0 = stock),
   until the maintainer accepts it: only then its key goes into `kApprovedDefaults` in `tests/test_options.cpp` with the decision
   number (E4). Its consumer asks `Vp64Opt::Extra(o, id).on` (never the raw value) once, at start. The default file
   writes it in the `[Extras]` block, after the switches and by category; the start log names its state by itself.
   `TestExtra` pins the extra rows that exist (since epic 0455: `FixRoadLayout`, `VisibleAreaOutline`, `FixUnitPlacement`,
   `GarrisonVisible`, `ShipsMoored`, `FixAirBases` and `AirInterceptPatrol`; ids 23-26 by MC #7491 and #7901, 27 by
   MC #8114; 28 is `[Renderer] ServerCombat`, ticket 4783, MC #8627; 29 `GuardsWhenOff`; 30-31 `RoadJunction` and
   `RoadJitter`; 32 `PerimeterGarrisons`, ticket 4983, MC #11877; the next free id 33): each new one changes that pin.
   A test value that players are not offered sets `Def::testOnly` = 1 (ticket 4983, D243: `GarrisonVisible`): it is
   read and logged like any row but left out of the default file and the installer's page.
3. Its section in `docs/OPTIONS.md`, with the line `Key = default` (the unit test checks it).
4. Its consumer reads `SrvOptions()` (server, `server/src/srv_options.h`) or `ProxyRtInt::ProxyOptions()` (proxy).
5. A row in `research/07-architecture-decisions/options-file.md` section 2.
