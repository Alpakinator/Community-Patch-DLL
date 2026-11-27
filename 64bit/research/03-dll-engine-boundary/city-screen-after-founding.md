# The city screen after founding a city (bug 0569, 2026-11-18)

Bug 0569: on i2-rc1 (the product, installer-installed, mods form, compositor on), HI acceptance item setup:5 (Duel,
Prince, Marathon pace, seeds 3/3, client 1914x1051, mods cp, vp, eui) stopped at step 19 twice on instance c5h-d: key B
founds the first city (Moscow), but the city screen does not open within the judge's 20 s; the stock control on c5h-a
opened it in 93 ms. This file holds what opens that screen, what was measured, and the verdict.

## 1. What opens the city screen after a founding [C]

1. **VP-EUI's CityView.lua opens it, from a notification.** `(3a) VP - EUI Compatibility Files/LUA/CityView.lua` (the
   instances' copy of VP 5.4.8, lines 2831-2847) registers, inside its `Events.SequenceGameInitComplete` handler (line
   27), an `Events.NotificationAdded` listener: for `NOTIFICATION_PRODUCTION` of the active player, it walks
   `g_activePlayer:Cities()`, finds the city whose `TXT_KEY_NOTIFICATION_NEW_CONSTRUCTION` text equals the
   notification's summary, and when `city:GetGameTurnFounded() == Game.GetGameTurn()` and `not UI.IsCityScreenUp()`
   calls `UI.DoSelectCityAtPlot(city:Plot())`, which opens the screen. (Proxy-design 29.1 first found this, ticket 0345.)
2. **The DLL sends that notification inside the founding.** Line numbers here and below are from the local fork clone
   `Community-Patch-DLL-x64` at e9d0c4837 (older than the i2-rc1 pin; proxy-design 29.3 found `CvPlayer::found`,
   `CvCity::chooseProduction` and `CvNotifications` unchanged between the pins and VP 5.4.8). `CvPlayer::foundCity` (`CvPlayer.cpp`
   13872-14000) for a human player: at turn 0 it first runs the player's economic, military, religion, espionage and
   trade AI `DoTurn`s, then `CvCity::chooseProduction` (`CvCity.cpp` 7800-7833) adds `NOTIFICATION_PRODUCTION`, and
   `CvNotifications::Add` (`CvNotifications.cpp` 236-334) calls `ICvUserInterface1::AddNotification` at once when the
   game is final-initialised and the player is the active one. On the product route that call crosses from the server
   to the EXE as a `sync` call (`proxy/model/policy-table.tsv` line 517).
3. Nothing else in the DLL opens the screen on a founding (no `selectCity` in `foundCity`, `CvUnit::foundCity` or
   `initCity`); the only other opener in that Lua is the `BUTTONPOPUP_CHOOSEPRODUCTION` popup, which comes only when the
   player activates the production notification.
4. **The settler's death and the unit cycle.** `CvUnit::foundCity` (`CvUnit.cpp` 10790-10870) ends with `kill(true)`,
   a delayed death; when the unit is really deleted (`CvUnit::kill`, 2527-2540), if it is still selected and the only
   unit in the selection, and the interface is not focused, has no city selected (`isCitySelection`) and no popup
   waiting, `CvGame::updateSelectionList` (`CvGame.cpp` 2011-) cycles the selection to the next unit. VP-EUI's
   `CityView.lua` also leaves the screen on Enter or Escape (its input handler, line 407).

## 2. The failing and passing runs, read offline [L, records of epic 0398]

Records under `F:/c5-64bit-work/0398/4609/setup/` (config-5-h1, -h2, -i1, stock-5-s1), read with a step-timing reader.
Times are milliseconds from the replay's start; "+" is the time since the step before.

| run | instance | route | step 16 load screen wait | step 17 (+) | step 19 (+) | step 19 result |
|---|---|---|---|---|---|---|
| config-5-h1, 16:42 | c5h-d (F:) | product | 55.9 s | 1,471,388 (+94.6 s) | 1,555,600 (+59.0 s) | deadline 27.6 s |
| config-5-h2, 18:37 | c5h-d (F:) | product | 10.5 s | 797,036 (+29.3 s) | 827,429 (+7.8 s) | deadline 22.5 s |
| config-5-i1, 19:06 | c5h-b (C:) | product | 4.7 s | 223,754 (+12.7 s) | 251,914 (+5.2 s) | CityView in 421 ms |
| stock-5-s1, 18:45 | c5h-a (C:) | stock (-ia b71a9f31) | 3.4 s | 210,223 (+12.5 s) | 237,222 (+4.6 s) | CityView in 93 ms |

- The whole replay to step 19 took 1,556 s and 827 s on c5h-d against 252 s and 237 s on the C: instances: c5h-d was
  3-6 times slower at every step (F: saturated that afternoon; the machine's diskwatch log
  `<ops folder>/diskwatch/diskwatch-20261118.jsonl` (`tools/diskwatch`) shows F: idle near 3 % with read latency above 300 ms at
  19:35).
- At the deadline (h2) the judge reads: city founded (end-turn block `ENDTURN_BLOCKING_PRODUCTION`), `city_screen`
  false, no screens, the selection on the warrior (unit 1002 at 27,15). Steps 17 and 18 judge the same on both routes.
- Lua.log: the stock run prints `CityView: GameInfo.UnitClasses[-1] is undefined` 28 s after its in-game load (the
  city view's first update with no production); the product h2 copy has no line after the in-game UI load, so the
  screen's update never ran there.
- Ticket 0345 (proxy-design 29.3) saw the same end state with the **stock 5.4.8 DLL** on c5h-g (F:) on 2026-11-15: after
  B, in 2 of 3 trials, the map with the warrior selected and the end-turn button reading CHOOSE PRODUCTION, no city
  view; 1 of 3 opened it. So the screen not opening after a founding is not new to the product route.

## 3. Live arms (2026-11-18, workstation)

Set-up of every arm: the HI phase tool of epic 0398 (`tools/hi/phase/phase.py options --config 5`, trace
`F:/c5-64bit-work/0398/4609/setup/rec-setup-4604.jsonl`) from a copy of branch `task-4609i-hi-phase-modelfree` at
6d7f5e88f (the product arms with `tools/hi/deny.py` from `epic-0398-hi-kit` 6cbe354b4, the 5 s busy bound of MC
#6282); instance settings as 4609h/i set them: window 1920x1080 (client 1914x1051), `SinglePlayerAutoEndTurnEnabled 0`,
`AutoUnitCycle 1`, `MapRandSeed`/`SyncRandSeed` 3; every changed file backed up by SHA-256 in
`F:/c5-64bit-work/0569/backup/<instance>/` and put back by hash after the run. F: and C: load from the machine's
diskwatch log (`<ops folder>/diskwatch/diskwatch-20261118.jsonl` (`tools/diskwatch`), 5 s samples, PhysicalDisk counters) over each
replay's window. Passive Lua listeners (`probe_install.lua`, installed through the Lua channel in the `InGame` state once
the replay has passed step 17: `NotificationAdded`, `SerialEventCityCreated`, `SerialEventEnterCityScreen`/`Exit`,
`UnitSelectionChanged`, `SerialEventGameMessagePopup`, `SerialEventUnitDestroyed`, and every main-thread frame gap over
250 ms from `LocalMachineAppUpdate`; `os.clock()` stamps, read back after the replay).

**F: load at each run** [L, diskwatch]:

| window (EST) | run | F: idle % | F: reads/s | F: read MB/s | F: mean read latency (max) | C: mean read latency |
|---|---|---|---|---|---|---|
| 16:42-17:10 | config-5-h1, product, c5h-d: FAIL | 2.7 | 65.6 | 1.68 | 539 ms (1,103) | 0.4 ms |
| 18:37-18:52 | config-5-h2, product, c5h-d: FAIL | 7.0 | 49.1 | 2.04 | 408 ms (1,076) | 0.3 ms |
| 18:45-18:53 | stock-5-s1, stock, c5h-a (C:): pass 93 ms | 9.3 | 50.1 | 1.70 | 384 ms (1,019) | 0.3 ms |
| 19:06-19:13 | config-5-i1, product, c5h-b (C:): pass 421 ms | 19.1 | 84.6 | 2.99 | 337 ms (952) | 0.4 ms |
| 19:53-20:03 | stock-5-c5h-g-s1 (this ticket), stock, c5h-g (F:) | 31.2 | 98.4 | 5.40 | 94 ms (355) | 0.4 ms |
| 19:55-20:09 | product-5-c5h-d-p1 (this ticket), product, c5h-d: stopped at step 17 | 32.9 | 29.7 | 1.32 | 194 ms (859) | 0.4 ms |
| 20:15-20:25 | product-5-c5h-d-p2 (this ticket), product, c5h-d: pass 140 ms | 93.9 | 12.1 | 0.76 | 28 ms (2,526); 1.2 ms over 20:19:30-20:21:30 | 0.5 ms |

**Stock on F: (c5h-g, -ia DLL b71a9f31, 19:53-20:03)** [L]: step 19 **passes**: key B founds Moscow and the judge reads
CityView open after **5,875 ms** (93 ms on c5h-a, C:); judge at step 19: city Moscow, `city_screen` true, block UNITS.
Step 17's in-game wait took 22 s (0.5 s on c5h-a). The replay later stopped at step 36 on the old 1 s deny bound (a
harness stop, MC #6282). The listener install failed on that arm (a fault in the probe script), so it has no event
timings. Record `C:/c5-64bit-work/0569/runs/stock-5-c5h-g-s1/` (pictures, trace). c5h-g restored: three ini files by
hash, DLL 3c2f47da by `set-dll`, `provision.py verify` rc 0.

**Product p1 on c5h-d (19:55-20:09)** [L]: did not reach step 19: step 17's ENTER came before the load ended and the load
screen ("Begin Your Journey") was still up when the in-game wait ran out (300 s; picture `stop_0017.png`), the F: fault
4609i met on setup:3-h2 and 4-h2. Note: the Lua channel answered (judge state read, settler 1001 selected, block NO)
while that load screen was still up. Re-run as p2 with `setup-variants-i.json` (step 17's wait before ENTER 60 s
instead of 20 s, lead-0398's edit; nothing else changes).

**Product p2 on c5h-d (20:15-20:25)** [L]: the same instance, build and settings as the two failures (config-5-h1,
-h2), with F: nearly idle (94 % idle, 1.2 ms read latency around the founding): step 19 **passes**, CityView open in
**140 ms** (judge: city Moscow, `city_screen` true, block UNITS); the replay completed all 46 steps (exit 0). So the
failure follows c5h-d's disk load, not the build or the instance: the same c5h-d and i2-rc1 pass with F: quiet and failed
twice with F: saturated (408 and 539 ms mean read latency). The listeners were installed (step 17) but their log was
read only after step 44's save load, which drops the Lua state (the read came back empty); the probe now reads every 5 s
from step 17 to step 22. Record `C:/c5-64bit-work/0569/runs/product-5-c5h-d-p2/`; c5h-d's three ini files restored by
hash; the run's own `HI-PH-SETUP-1` save moved to `F:/c5-64bit-work/0569/runs-saves/`.

**Event order on a pass, both routes, and an injected main-thread stall that flips nothing** [L, 20:35-20:45]. Arm x1 on
each route: the same replay with, from step 17 on, the listeners and a `LocalMachineAppUpdate` handler that busy-waits
400 ms on every frame until 20 s after the first city exists (`stall_install.lua`: frames at about 2.5 per second, as
when the main thread waits on a disk). Stock on c5h-a (C:, -ia b71a9f31): step 19 **passes**, CityView after 3,000 ms.
Product on c5h-d (F: quiet): step 19 **passes**, CityView after 2,516 ms; both replays completed (46 steps). The
listeners' log (os.clock seconds; IDs as in the game):

| event | stock c5h-a x1 | product c5h-d x1 |
|---|---|---|
| `SerialEventCityCreated` city 1015 (head unit already 1003, no city selected) | 355.671 | 439.727 |
| `NotificationAdded` id 0, `NOTIFICATION_PRODUCTION`, summary "Moscow is ready for a new construction project." (city 1015 already selected, `UI.IsCityScreenUp()` true: VP-EUI's listener, registered earlier, ran first) | 356.620 | 440.195 |
| `NotificationAdded` id 1, Choose Research | 356.621 | 440.195 |
| `SerialEventEnterCityScreen` | 357.021 | 440.595 |
| `UnitSelectionChanged` settler 1001 deselected | 357.133 | 440.735 |
| settler 1001 deselected again, then `SerialEventUnitDestroyed` 1001 (the delayed death) | 376.341 / 376.397 | 447.352 / 447.798 |

So on both routes the production notification reaches the UI 0.5-1 s after the city's creation event, VP-EUI's
listener opens the screen at once, and the settler's delayed death comes seconds later; the summary text matches the
city's (`same=true`), turn founded 0 = game turn 0. A uniformly slow UI thread delays the screen (2.5-3 s) but does not
lose it. Lua.log: every pass (p2, both x1, stock c5h-g) prints `CityView: GameInfo.UnitClasses[-1] is undefined` once at
the founding (VP-EUI's city view updating with no production); the failing h2 Lua.log has no line after the in-game UI
load, so in the failures VP-EUI's city view never ran for Moscow, while the stop picture shows the production and
research notifications in the panel and the end-turn button reading CHOOSE PRODUCTION.

**The paired run under a made F: load (MC 21:08 on #6400, one run)** [L, 21:00-21:15]. Stock on c5h-g and product on
c5h-d started together (both with `setup-variants-i.json`, listeners only, no stall); from 21:04 to 21:15:30 a reader of
our own 1 GB file on `F:/c5-64bit-work/0569/` (`fload.py`, 16 threads, unbuffered random 64 KB reads; the file's
creation alone, at about 2 MB/s, already saturated F:) loaded F:. F: over the window: idle 2.8 %, queue 12.3, mean read
latency **396 ms** (max 1,062), as in h1/h2 (408-539 ms). The reader was stopped by its stop file and the file deleted.

- **Stock (c5h-g)** did not reach step 19: the start screen's ENTER came before the load had finished even with the 60 s
  wait, and "Begin Your Journey" was still up when the in-game wait ran out (300 s; `stop_0017.png`); a harness stop.
- **Product (c5h-d)** reached step 19: B at about 21:12:42; the judge's first read was refused ("channel busy", the 5 s
  bound; exit 2, a harness stop), but the listeners kept recording and show **the city screen opened**:

| os.clock | event (product, F: at about 400 ms) |
|---|---|
| 830.030 | `SerialEventCityCreated` city 1015 (no unit, no city selected) |
| 830.066 | frame gap 1.8 s |
| 838.020 | `NotificationAdded` production (city 1015 already selected, screen up: VP-EUI's listener ran), Choose Research; settler 1001 deselected; unit 1003 selected |
| 838.021 | **frame gap 7.955 s** (the UI thread blocked about 8 s after the founding), then `SerialEventEnterCityScreen` |
| 841.684-854.489 | frame gaps 3.7 s, 0.6 s, 0.6 s, 10.7 s; unit 1003 deselected; settler destroyed at 855.546 |
| 924.344 | read: city 1015 selected, `UI.IsCityScreenUp()` true |

So on a saturated F: the UI thread stalls for 2-11 s at a time (the founding's 8 s stall is where the EXE loads what the
new city needs), the production notification and the screen come about 8 s after the founding, and the product still
opens the screen.

## 4. Verdict (2026-11-18 21:20)

1. **Not a product defect on the evidence.** The failure (city founded, warrior selected, no city screen, CHOOSE
   PRODUCTION) was seen only on c5h-d while F: was saturated (h1, h2). The same instance, build and settings pass with F:
   quiet (p2, 140 ms), with a slowed UI thread (x1, 2.5 s) and with F: saturated again to the same read latency (L1,
   screen open after about 8 s). The same end state, word for word, was seen with the **stock VP 5.4.8 DLL** on the F:
   instance c5h-g on 2026-11-15 (proxy-design 29.3: trials 1 and 3 of 3, keys only, "the Warrior selected and the end-turn
   button reading CHOOSE PRODUCTION, no city view") [L]. Recommendation: reclassify bug 0569 as the game's own behaviour
   (VP-EUI's founding screen, which can fail to open), met by the HI judge on a saturated disk; not a product defect.
2. **Mechanism, as far as it is established.** The screen after a founding comes only from VP-EUI's `NotificationAdded`
   listener (section 1) [C]; on every pass, on both routes, that listener selects the city and the screen opens in the
   same frame, 0.5-8 s after the city's creation event [L]; in the failures VP-EUI's city view never ran for Moscow (no
   `UnitClasses[-1]` print) while the production notification was shown [L, h2's Lua.log and picture]. The DLL side is
   the same on both routes (`foundCity` -> `chooseProduction` -> `AddNotification`, settler's delayed death later; when the
   settler dies while selected and no city is selected, the DLL's `updateSelectionList` selects the next unit: `CvUnit.cpp`
   2527-2540) [C].
3. **Not determined [open]:** which step of the EXE's or VP-EUI's path is skipped in the failing case (the listener not
   called, its tests failing, or `UI.DoSelectCityAtPlot`'s selection replaced by the unit cycle before the screen is
   entered). It was not reproduced in 5 runs today (stock 2, product 3 that reached the step) with the listeners in place;
   a reproduction needs a run that fails with them installed (the 0345 save-based keys-only trial on stock, where 2 of 3
   failed, is the cheapest candidate). The HI harness itself does not work on a saturated F: (ENTER lost on the start
   screen twice, the judge's channel refused once, the old 1 s deny bound once): F: instances should not carry timed HI
   replays while F: is saturated.

## 5. Files

`server/evidence/0569/`: the listeners (`probe_install.lua`, `probe_read.lua`), the stall (`stall_install.lua`), the
poller that installs and reads them through the Lua channel (`probe_poll.py`), the F: load reader (`fload.py`), the
diskwatch window reader (`fread.py`, `DISKWATCH_DIR`), the step-timing reader (`tr.py`), and the listeners' logs of
stock x1, product x1 and product L1. Run records (pictures, traces, Lua.log copies) stay outside the repository in the
ticket's work folder (`C:/c5-64bit-work/0569/runs/`); each run's own `HI-PH-SETUP-1` save went to
`F:/c5-64bit-work/0569/runs-saves/`.
