# The turn-time and memory study (epic 0437, D163)

The study of `research/07-architecture-decisions/architect-2026-11-16-turn-time-study-epic.md`: four setups (S1 the
stock DLL headless 32-bit in c5h, S2 the stock DLL in the stock EXE, S3 the x64 DLL headless in `server.exe` direct
mode, S4 the x64 DLL in the shell), two scenarios, 25 turns per run. Each package adds its own section. Marks: `[C]`
read in code or records, `[L]` verified live, `[I]` inferred.

## Scenarios (ticket 0438, W1; 2026-11-16)

Written 2026-11-16 by the implementer of 0438 (account B), 15:51-17:25 EST. Both saves made and checked in all four
setups (the tag's DLL in the stock EXE: load only, see below).

### What the scenarios are

| | H | M |
|---|---|---|
| Save | `TS0438-H-T300.Civ5Save` | `TS0438-M-T150.Civ5Save` |
| Spec | `perf/turnstudy/scenarios/H-huge-cdrift.spec.json` | `perf/turnstudy/scenarios/M-standard-communitu.spec.json` |
| Map script | VP 5.4.8's `(2) Vox Populi/Mapscripts/Continental_Drift.lua` ("Continental Drift - v3.1", sha256 `a0d07be2...`) | VP 5.4.8's `(2) Vox Populi/Mapscripts/Communitu_79a.lua` ("Communitu_79a v3.2.0", sha256 `99b2b406...`; not the base game's `Assets/Maps` copy `9de4dd14...`) |
| Size | Huge, 128 x 80 | Standard, 80 x 52 |
| Players | the size's defaults: 12 majors, 24 city states (Worlds.DefaultPlayers / DefaultMinorCivs of the client's database) | 8 majors, 16 city states |
| Speed, era, difficulty | Standard, Ancient, Emperor | the same |
| Map options | every option set to the script's DefaultValue (MapScriptOptions of the client's database) | the same |
| Game options | GAMEOPTION_QUICK_COMBAT and GAMEOPTION_QUICK_MOVEMENT on (the MC's answer, 2026-11-16: identical in all four setups; W0 may add an arm with them off) | the same |
| Victories, turn limit | all victory types, the speed's limit (500) | the same |
| Seeds | map 4380, sync 4381 | map 4382, sync 4383 |
| Mods | the save header: CP v151, VP v17 (the descriptor's, `run_server.py --save-mods descriptor`); the clients load it with cp, vp, eui (`(3a) VP - EUI Compatibility Files`), exclusive | the same |

The spec's script is given by file name: the server's `LoadFile` resolves it through the VFS by name (bug 0100's
fix), which loads VP's copy from the toolkit game folder [L, server.log of the M run: `LoadFile("Communitu_79a.lua")
-> ...\(2) Vox Populi\Mapscripts\Communitu_79a.lua`]. The toolkit's copies of both scripts are byte-identical to the
instances' (same SHA-256) [C, 2026-11-16]. The map options are given explicitly because the pregame's default lookup
goes by the script's FileName, which in a client's database is the instance's absolute MODS path [C: MapScripts rows].

### The client's database (the lesson of 9071)

A save made on the server's descriptor database can hold a civilisation the client lacks and stops the stock client in
the DLL's assert `eLeaderHeadNum > -1` (`metrics-ledger.md` 8.2). So both games are drawn from a client's own database:

- Made on c5h-a (C:, VP 5.4.8 in mods form, instance-aware pin `i0ship-26234371-ia` installed; the DLL does not build
  the database) on 2026-11-16 15:56-15:58 EST: the harness front end (`frontend.prepare --mods cp,vp,eui`, exclusive)
  activated the three mods ("activating 3 mods - this rebuilds the game database" at 15:57, "requested mods are already
  active" at 15:58:32); `cache/Civ5DebugDatabase.db` copied after its last write (15:58:31) with the game at the main
  menu, then the game stopped (`stop --human-turn-hook tools.harness.turns:stop_readiness`) and the front end restored.
  Script `perf/evidence/0438/clientdb.sh`.
- `F:/c5-64bit-work/0438/clientdb/Civ5DebugDatabase.db` sha256 `2122c08bac635be251430b065398a46c95ff025035be239dc58856522cab15cd`
  (6,816,768 B; integrity ok; 832 tables; 43 playable civilisations, 120 minor civilisations; SQLITE_LOGGING 0), with
  `Localization-Merged.db` `b2bbe365...` and `Civ5ModsDatabase.db` `e64ab402...` beside it.
- **Content fingerprint**: `02b5faa409b715fa0e0334c801cdc22aa9c5a0f4a2d255d3fa4b117e5344476a`
  (`perf/evidence/0438/dbinfo.py`: SHA-256 over every table's sorted rows, ScannedFiles left out, the instance's
  absolute path before `MODS\` folded, so that another machine's client database can be compared). 9071's
  `F:/c5-64bit-work/9071/lc-db/Civ5DebugDatabase.db` (made on c5h-d on 2026-11-15) has the **same fingerprint**: the
  file bytes differ only in MapScripts, MapScriptOptions and MapScriptOptionPossibleValues, which hold the instance
  path [C, 2026-11-16]. 0423's `align548` (Squads and the 5.4.8 release files, after 9071) did not change what cp, vp,
  eui build [I from the equal fingerprint; c5h-a's alignment state not separately checked].
- **The identity copy**: `F:/c5-64bit-work/0438/clientdb/stats-on/Civ5DebugDatabase.db` (sha256 `9335186b...`), the same
  with `CustomModOptions.SQLITE_LOGGING = 1`, so the headless hosts write `stats.db` for statscmp. The statistics option
  changes no game result (D24, D35). The games were made on this copy.

### How the saves were made

`perf/evidence/0438/make.sh TAG SPEC TURN`: the x64 pin `x64-04b1211b-pfdee656` (`server/run_server.py SPEC --turns
TURN --db <identity copy> --game-dir <toolkit> --dll-log-flags 0 --save-out NAME`), the save copied to
`F:/c5-64bit-work/0438/saves/` with its SHA-256; then S3's check: the same server loads that save and plays 25 turns
(`--load NAME --turns TURN+25`), which also shows that the 25-turn window of the study plays without a victory or a
crash. The server arms autoplay itself and sets Quick Combat and Quick Movement on at its arm step (`server-host.md`
step 6) [C].

### The saves `[L, 2026-11-16, workstation, shared: times are indications, not results (D106)]`

Kept on `F:/c5-64bit-work/0438/saves/`, never committed. Not on the laptop: the MC (2026-11-16) left the laptop copy
to the epic's lead, as 4602's real-input runs hold the laptop now.

| | TS0438-H-T300 | TS0438-M-T150 |
|---|---|---|
| SHA-256 | `308a96bb1404e68eb42672ecd51777c791dca151de482a1e90819c9b7df7e584` | `98fc454a5b87d0d8752696895b9ff7fd5dd1858023f1fa426542e9e7a7e7a0c1` |
| Made | 16:02:45-16:37:00, 300 turns in 2,054 s (autoplay 6.79 s/turn mean, peak working set 439 MB, Lua errors 0) | 16:00:16-16:01:59, 150 turns in 98 s (0.55 s/turn, 247 MB, 0 Lua errors) |
| Map as generated | 140 x 88 (the script scales the Huge grid) | 79 x 53 |
| State at the save's turn | 11 of 12 majors alive (Byzantium dead), 20 of 24 city states; Iroquois runaway: 45 cities, population 1,027 | all 8 majors and 16 city states alive; 3-11 cities per major |
| The study's 25 turns, headless (S3) | 300 -> 325 played, no victory, no crash, 0 Lua errors; 20.9 s/turn mean (17-31 s) | 150 -> 175 played, 1.62 s/turn mean |
| Header mods | CP v151, VP v17 | the same |

Evidence (committed, our server's own text output): `perf/evidence/0438/H/` and `perf/evidence/0438/M/` (the server
configuration, per-turn times of the making and of the 25-turn continuation, the final summaries with each player's
civilisation, cities, population, techs and units at the save's turn and 25 turns later).

### Load checks and the identity trace (S1, S3)

Each setup loads the save and plays one AI turn on the identity copy of the client's database; its `stats.db` is
trimmed to the turns of the save and the one after (`perf/evidence/0438/idtrace.py LO HI`: rows with Turn outside
LO..HI deleted in every table with a Turn column, then `tools/d10/statscmp.py --from-turn LO` on every pair). The
window holds what the DLL logs for the turn played: M 2,830 rows (1,329 at turn 150, 1,501 at 151), H 21,961 rows
(13,676 at 300, 8,285 at 301). A run that continues past T+1 logs more rows under turn T+1 (the 25-turn S3 run of M:
2,867 at turn 151), so only runs that stop at T+1 are compared.

| Setup | Binary | M 150 -> 151 | H 300 -> 301 |
|---|---|---|---|
| S3 | x64 pin `x64-04b1211b-pfdee656` (DLL `29F58E17...`, server `5B254430...`), `perf/evidence/0438/s3one.sh` | loads, 1 turn, 1.27 s | loads, 1 turn, 21.6 s |
| S1 (i0-ship) | `i0ship-26234371` (`f92881a3...`) in c5h, `perf/evidence/0438/s1.sh` | loads, 1 turn, 1.19 s; **identical to S3** | loads, 1 turn, 25.4 s; **identical to S3** |
| S1 (tag) | the tag's own DLL `262aee88...` (Release-5.4.8, what a player installs) in c5h | loads, 1 turn, 2.08 s; **differs** | loads, 1 turn, 26.7 s; **differs** |

`[L, 2026-11-16 16:45-16:48]`. c5h ran with `--db <identity copy> --mods-dir <c5h-a's MODS> --mod CP:151 --mod VP:17
--no-addins --host-arg=--no-game-logging`; the server with `--dll-log-flags 0`. Comparisons:
`F:/c5-64bit-work/0438/checks/idtrace-{M,H}-headless/`.

- **The x64 DLL and i0-ship play the same game from both saves** (S3 = S1 i0-ship, every table, both scenarios).
- **The tag's DLL plays a different game from the first turn after the load** (premise P1 of W0): M: MapUnitsState
  differs in 2 rows at turn 151 (one unit 64,14 against 65,12); H: MapUnitsState 10 rows at turn 301 and
  MilitarySummary 1 row at turn 300 (RecLandOffensive 2 against 1). i0-ship carries upstream fixes that change game
  results (`research/03-dll-engine-boundary/upstream-fix-batch-1.md` section 8, COMMON rule 18), which is the
  expected cause [I]. So an S1/S2 arm on the tag's DLL is a different game from S3/S4 at once; W0's P1 decides which
  DLL is "stock" (the MC's answer, 2026-11-16) and the study labels its comparisons accordingly.

### Load checks and the identity trace (S2, S4) `[L, 2026-11-16 16:48-17:19, workstation]`

Driver: `perf/evidence/0438/cc.sh` = `perf/profilekit/acceptance.py run INSTANCE --save ... --turns 1 --no-ledger`
(save copied in by hash, front end loads it with cp, vp, eui, `Game.SetAIAutoPlay(1, 0)`, frame cap lifted, one memory
row and one picture per turn, safe stop, front end restored, save copy removed). For the trace, SQLITE_LOGGING was set
to 1 in the instance's copy of CP's `Database Changes/NewCustomModOptions.xml` (backed up by SHA-256 with
`tools/harness/backup.py`, restored after each run), and a `stats.db` left in the instance's cache was moved aside first
(the DLL appends to an existing one). In S4 the server writes `stats.db` into the engine's cache folder, the same
place as the 32-bit DLL (`server.log`: "the engine's cache folder ... is a write root too") [L].

| Setup | Instance and binaries | M: load / turn 150 -> 151 | H: load / turn 300 -> 301 |
|---|---|---|---|
| S2 (i0-ship) | c5h-a: stock EXE + `i0ship-26234371-ia` (`3c2f47da...`) | 95.9 s / 17.0 s, autoplay handed back at 151 | 151.3 s / 85.5 s, handed back at 301 |
| S4 (pin layout) | c5h-b: `provision.py set-proxy` of `x64-04b1211b-pfdee656` (proxy `75C40CEC...`, server `5B254430...`, x64 DLL `29F58E17...`, fallback the tag DLL; `mode=on`, server run folder present) | 106.7 s / 17.6 s, handed back at 151 | 168.0 s / 91.5 s, handed back at 301 |
| S2 (tag) | c5h-a: stock EXE + the tag's `262aee88...` (`provision.py set-dll`, no Lua channel), front-end auto-load, load screen closed by a harness click | loads: in game at turn 150 as Songhai; **turn not played** | loads: in game at turn 300 as Maya; **turn not played** |

Times are the driver's indications (a 5 s poll of the Lua channel, frame cap lifted, workstation shared with other
teams; D106): not for timing. No dialog box on either instance at any turn (`dialogwatch.py marker`).

**The identity trace** (`idtrace.py`, windows of one turn; `perf/evidence/0438/idtrace/{M,H}-client-t*/`):

| Window | M | H |
|---|---|---|
| The loaded turn (T), rows the client logs right after the load | S2 = S4, **identical** (2,501 rows) | S2 = S4, **identical** (20,388 rows) |
| T+1, after the turn played | S2 = S4 **identical**; both **differ from S3 and S1 i0-ship**: MapUnitsState 50 of 293 rows, MapPlotsState 1 of 873 | S2 = S4 **identical**; both **differ from S3**: MapUnitsState 1,436 rows against 1,431 (332 / 337 rows only on one side), BuildingYields 1 row, WorldStateLog |

- **Both clients play the same game as each other** from both saves, the x64 server behind the proxy included, and the
  **headless hosts play a different one from the first turn after the load** (premise P4 of W0: the clients and the
  headless hosts are not one game for any number of turns from these saves, so the four-way comparison pairs S2 with S4
  and S1 with S3 when it needs the same game). Cause (corrected 2026-11-16 23:10 by ticket 4655, section "Why the headless hosts and the clients diverge"): the headless hosts never call `ICvMap1::RecalculateLandmasses()` after a load, so the plots' cached water flags stay false; with that one call the headless hosts play the client's game [L]. Was: "Cause not traced [I: the client's load path (the human seat moved to the
  observer by `SetAIAutoPlay` in the client, the UI's own calls) against the headless hosts' arm step].
- The client logs MapPlotsState, MapUnitsState, WorldStateLog and civ_turn_era for the loaded turn at the load; the
  headless hosts do not (their first rows of those tables are T+1) [L].
- `S2 (i0-ship) = S4 = the same game` holds for i0-ship only: the tag's DLL already plays differently headless (above).
- **The tag's DLL in the stock EXE** (pure S2, nothing of ours in the process): the save loads into the game
  (pictures in Visual Checks, issue 0438, 17:08-17:17), but the turn could not be ended without a Lua channel: the
  loaded seat is the human's (player 0: Songhai in M, Maya in H; the headless server moved the human to the observer
  only while it played), the end-turn button was blocked ("PROMOTE A UNIT" in M, "CHOOSE PRODUCTION" in H), and the
  harness's `key SHIFT+ENTER` did nothing (the guard hooks GetKeyState [I]). So a client arm on the tag's DLL needs a
  way to arm autoplay that does not use the DLL's channel; W0's P1 decides whether such an arm is needed.
- Seen, not traced: the S4 server's `server.log` reports "check after CacheGameDatabaseData: engine 832 tables 44674
  rows ..., server ... 44044 rows ...: DIFFERENT" at 30.6 s (after the load), while the first check at 10.2 s was
  IDENTICAL (`Logs/VP64/run/20261116-165806-31924-1` of c5h-b, M). The game played identically to S2 regardless.

### State after the checks

c5h-a and c5h-b were restored by SHA-256 (mod options file, `set-dll --dll pin`, `set-proxy --original`, front ends),
`provision.py verify` ok on both, no leftovers, both released by 17:22. Their `stats.db` files are as before the ticket
(c5h-a's of 2026-11-14 back in place; c5h-b had none); the ones the checks wrote are in
`F:/c5-64bit-work/0438/checks/instance-stats/`. Everything the checks wrote is under `F:/c5-64bit-work/0438/checks/`
(per-arm folders with acceptance-run.json, per-turn.csv, run.log, pictures, stats.db). The headless run folders
(making, S1, S3, with their full `stats.db`, server.log and turn files) were moved from `C:/c5-64bit-work/0438/runs/`
to `F:/c5-64bit-work/0438/runs/` at the hand-back (568 MB); the C: folder is deleted.

### For the epic's lead (what W1 leaves)

- The saves: `F:/c5-64bit-work/0438/saves/` (SHA-256 above), to be copied to the laptop by the lead.
- The client database and its identity copy: `F:/c5-64bit-work/0438/clientdb/` (fingerprint above; compare the
  laptop's client database with `dbinfo.py` before using the saves there: the same fingerprint means the same game data).
- The tools: `make.sh` (a scenario from a spec), `s1.sh`, `s3one.sh`, `cc.sh` (S2/S4 through the acceptance driver),
  `idtrace.py` (the identity windows), `dbinfo.py`, `s2tag.py` (the tag DLL without a channel), all in
  `perf/evidence/0438/`: scripts of this ticket with fixed paths, a start for the scenario kit of section 5, not the kit.

### Not verified

- Nothing on the laptop: neither the saves there nor its client database's fingerprint.
- One turn per setup only; the 25-turn window was played headless (S3) only.
- The tag's DLL in the stock EXE played no turn (above).
- S4 ran the pin layout through `set-proxy` on a test instance, not the installed release candidate; the epic's S4
  build is named when the study starts (section 4.1 of the proposal).
- ~~Why the clients and the headless hosts diverge at once.~~ Answered by ticket 4655 (2026-11-16): the missing `RecalculateLandmasses()` after a load in the headless hosts.

## The turn budget (W4a, ticket 4652; 2026-11-16)

Written 2026-11-16 by the implementer of 4652 (account A, worker of lead-0437), 17:35-. The product side of Q2: timers
in server.exe (remote mode) and in the proxy that split every S4 turn into rows summing to the turn's wall time, the
server's own per-turn line in remote mode, and a reader. Marks: `[C]` read in code, `[L]` verified live, `[I]` inferred.

### The switch

`VP64_TURN_BUDGET=1` in the environment of the EXE: the proxy reads it at its boot, server.exe at the start of remote
mode (it inherits the EXE's environment: the proxy's `CreateProcessA` passes no environment block, `proxy_rt.cpp`) [C].
Off when absent or anything but exactly `1` (`vp_budget.h` `SwitchValueOn`; host-tested: absent, `0`, empty, ` 1`,
`11`, `true`, a value longer than the reader's buffer). Off, every hook is one test of a global bool, nothing is
created, opened, written or reported, and server-report.txt and vpproxy.log are as before [C]. Why an environment
variable rather than a server.cfg key: one value reaches both processes; it is set per launch (the harness launch
inherits the caller's environment, `tools/harness/winapi.py` `build_env`; `extra_env` of `launch.launch`), so no file in
an instance changes and nothing has to be backed up and restored; a server.cfg key would need `server_cfg_extra=` (test
only) and a second setting for the proxy. Nothing the product ships sets it.

### Files (in the session's run folder; with the switch on only)

| File | Writer | Content |
|---|---|---|
| `turn-times.tsv` | server.exe (remote mode) | `turn seconds updates`, the columns of direct mode (`srv_main.cpp`). A row per turn at its end; the first turn after the load carries a 4th field `(first)` (it starts at the end of the first Update, not at the turn's start), the last partial one `(end)`, as direct mode's last line carries `(idle)` |
| `budget.tsv` | server.exe | `# qpc_hz F`, then `turn kind name seconds count ticks`: `turn wall` (count = Update calls), `turn load/first/end`, 14 `row` lines (they sum to wall in ticks), `in`/`out` per call class (calls, inclusive time), `info hidden_msgs / overlap / flush_prev` (and `anomalies`, `deep`, `unsynced` when not 0) |
| `budget-proxy.tsv` | the proxy (beside vpproxy.log; in the product that is the same run folder) | the same columns: `turn wall` (from the server's published boundaries), `row exe_alone` (count = idle intervals), `row busy`, `out` (EXE -> DLL round trips) and `in` (DLL -> EXE handler times) per class |
| `budget-table.csv`, `.json` | `perf/turnstudy/budget/budget.py RUN_DIR [--proxy FILE]` | the joined table per turn: rows after transport, their sum, residual, the turn clock, the cross-checks |

Turn -1 is the load phase (from the attach to the end of the first Update after the load); it and the first and last
(partial) turns are labelled and left out of the reader's means.

### How a turn is cut into rows (the accounting) [C, host-tested]

- **Turn boundary.** The end of the `ICvGame1::Update` the server serves in which `GetGameTurn()` changed (the place the
  server already read the turn for `proxy-calls-per-turn.tsv`, `SrvRt::LeaveUpdate`). Direct mode's turn line uses the
  same event (after the Update in which the turn changed). The server publishes each boundary (QueryPerformanceCounter
  value, the turn that begins) in the section `Local\VPBudget_<EXE pid>`, which the proxy creates at its boot; the
  proxy cuts its own file at the same instants (QPC is one clock for all processes of the machine).
- **One state word.** server.exe keeps one 64-bit word: the clock of its last change (48 bits of ticks), a turn
  parity bit, and for each service thread (ui, core, aux) its current class in 4 bits: 0 waiting for work; `update`
  inside Update on the DLL's own work; `lua`, `plot`, `lock`, `other` serving another engine -> DLL call; `script`, `ui`,
  `engine`, `file`, `db`, `stream`, `other` waiting for a DLL -> engine call it made; `save` anything inside an
  `AutoSave`/`SaveGame`/`QuickSave` it made, nested calls included. Each frame change (a served call begins or ends,
  a call into the EXE is sent or returns) and each boundary replaces the word by one compare-and-swap and charges the
  interval since the word's last change to one row: the class of the busy thread of highest priority in the old word
  (core, then ui, then aux), or `exe_alone` when none is busy. So the rows of a turn partition its wall time exactly,
  in integer ticks, whatever the threads do; time with more than one thread busy is counted beside (`info overlap`).
- **Accumulators** per channel and parity, written only by that channel's thread; a boundary flips the parity, waits
  (bounded, 200 ms) until no other thread is between its compare-and-swap and its update, then reads and zeroes the
  closed parity and writes the turn's lines (0.2-0.4 ms per turn headless, `info flush_prev`, charged to the next turn's
  game logic). No lock, allocation or I/O per call (PERFORMANCE.md 5).
- **Messages.** Each frame change counts the message (a CALL or a RETURN) whose transit lies in the interval just ended
  or just begun into the row that interval goes to, when that row is this thread's own or `exe_alone`; else it is
  `hidden` (it crossed while a higher-priority thread's work was charged).
- **Inclusive times** per class (served: handler time; made: round trip around `end_call`) by the turn in which the call
  ended, on both sides. They give the transport directly: (proxy round trip - server handler time) over the EXE -> DLL
  calls plus (server round trip - proxy handler time) over the DLL -> EXE calls. The reader divides it by the turn's
  messages (2 per call) and takes `messages x cost` out of each row that holds messages (never more than the row), so
  the rows still sum to the wall time.
- Class of a method: `vp_budget.h` `ClassOfMethod` / `ClassOfRuntime`, one table per side built once at the switch's
  start from the generated method keys (`vpk_iface_name`, `vpk_method_key`).

### Definitions (registered in `perf/ledger/registry.json`; reviews 7015 and 7016)

Replaced 2026-11-17 by ticket 4659 with the registered ids after the performance team's reviews 7015 (B1-B9) and 7016
(C1-C11); was the proposed addendum with `budget.wall_s@1`, `budget.exe_alone_s@1`, `budget.exe_alone_proxy_s@1` and
`budget.cost_us_per_msg@1`, which 7015 did not register (B1, B4, B7). The full text of each id is its registry entry;
the series' design is the plan's 9.7 (`turn-time-study-analysis.md`).

All per turn of S4 (configuration `inc1-thin`), remote mode, `VP64_TURN_BUDGET=1` only; unit seconds unless marked;
source `budget.tsv` + `budget-proxy.tsv` through `perf/turnstudy/budget/budget.py` (ticks of QueryPerformanceCounter,
10 MHz on the workstation) into `budget-table.csv`, and in a run record `series-budget.csv`. Row N runs from the
boundary at which game turn N begins to the one at which N+1 begins. The load phase (-1) and the rows flagged `(first)`
(the first turn after the load) and `(end)` (the last partial turn) are not turns: labelled, never in a mean. The rows
`game_logic`, `into_exe`, `from_exe`, `save`, `transport` and `dll_idle` sum to `turn.server_remote_s@1` of the same
turn exactly (`budget.py` exit 3 otherwise). "The top thread": the busy service thread of server.exe of highest priority
(core = the EXE's game-core thread, which calls Update; then ui = the EXE's boot/UI thread; then aux).

| Registered id | Definition |
|---|---|
| `turn.server_remote_s@1` (the wall) | server.exe's own per-turn line in remote mode (`turn-times.tsv`): the interval between two turn boundaries, each the end of the `ICvGame1/2::Update` served by server.exe in which `GetGameTurn()` changed, QueryPerformanceCounter on the core thread; `budget.py` checks it against `budget.tsv`'s wall to 1.5 ms (exit 5). Written only with the switch on, so it is not in the Q1 timed series. Never paired turn by turn with `turn.doturn_s@1` (another event on another clock) |
| `budget.game_logic_s@1` | the top thread is inside Update running DLL code (not waiting for a call it made into the EXE, not serving a nested engine call), less the transport of its messages; includes the previous turn's flush (`info flush_prev`) and most of the instrument's own cost |
| `budget.into_exe_s@1`, `budget.into_exe.<c>_s@1` (c = script, ui, engine, file, db, stream, other) | the top thread waits for a DLL -> EXE call of class c that it made (CALL sent to RETURN read), outside nested served calls, saves apart, less its transport; each class names its method list (`vp_budget.h` at `926f16593`, 7015 B5) |
| `budget.from_exe_s@1`, `budget.from_exe.<c>_s@1` (c = lua, plot, lock, other) | the top thread serves an EXE -> DLL call of class c other than Update (handler time on server.exe, DLL code included), outside the calls it makes into the EXE, less its transport |
| `budget.save_s@1` | the top thread is inside an `AutoSave`, `SaveGame` or `QuickSave` call it made into the EXE, everything nested included, less the transport of the crossings inside it; charged to the turn whose Update called it |
| `budget.transport_s@1` | the turn's measured transport ((the proxy's round trip - server.exe's handler time) over the EXE -> DLL calls + (server.exe's round trip - the proxy's handler time) over the DLL -> EXE calls), apportioned to the rows at the turn's mean cost per message (each row gives up min(row, its messages x cost)); hidden messages are in no row (7015 B6) |
| `budget.dll_idle_s@1` | no service thread of server.exe is busy, less the transport of the messages that crossed then (`budget.tsv` row `exe_alone`; "EXE alone" in `inc1-thin`, frame-pacing waits included); never called the EXE's work without the EXE's CPU time of the turn beside it (7015 B4) |
| `budget.dll_idle_proxy_s@1` (diagnostic) | proxy side: no EXE thread has a call to server.exe in flight; a cross-check of `budget.dll_idle_s@1` only (`remainder_check` column) |
| `budget.overlap_s@1` (diagnostic) | more than one service thread busy (charged to the top one) |
| `budget.updates@1` (count, context) | Update calls served in the turn (the `turn wall` line's count) |
| `budget.messages@1`, `budget.hidden_msgs@1` (counts, diagnostics) | 2 x the calls that ended in the turn (the denominator of the cost per message); the messages that crossed while a higher-priority thread's work was charged |
| `budget.<row>_mean_s@1` (row = game_logic, into_exe, from_exe, save, transport, dll_idle; scalars) | the mean of the row over exactly the window T+1..T+25 named in the run record (`budget.py --run-record`; exit 6 when a window turn is missing or labelled) |

Not registered: `budget.wall_s` (7015 B1: one method, one id; the wall is `turn.server_remote_s@1`),
`budget.exe_alone_s` and `budget.exe_alone_proxy_s` (renamed `dll_idle`, B4) and `budget.cost_us_per_msg` (a
`budget-table.csv` column only: the registry has no `us` unit, B7). Shares are analysis and get no id: sum(row) /
sum(wall) over the window, with the interval across runs (`budget.py --across`, B2). A budget is compared only with S4
switch-on runs of one `series_id` on one machine (B8).

Not in the rows: DLL work on threads other than the service threads (none today: the DLL runs only inside served
calls, D30 condition 4; the renderer thread runs no DLL code) [C].

### Tests and results so far

Build `C:/c5-64bit-work/4652/b1` (`python -I server/build_server.py --src <git archive of the fork at 04b1211b8> --out
C:/c5-64bit-work/4652/b1 --config release --offline --deps <a copy of 9045's deps>`, 18:09-18:13, repo 926f16593):
server.exe `994651B6B8A910D4...`, proxy `7DB09E568197E36A...`. Layout for the runs `C:/c5-64bit-work/4652/layout`: the pin
`x64-04b1211b-pfdee656`'s bin (x64 DLL `29F58E17...` unchanged) with this server.exe, and this proxy (`SHA256.txt` there).

- **Host test** `server/tests/budget/run_budget_test.py --asan` [L 2026-11-16 18:40]: 208 checks pass in each of four builds (209 after the settle rule, below)
  (clang-cl x64 /O2, MSVC /fsanitize=address x64 and x86 with their canaries caught, MSVC x86 /O2): the switch's default
  and values; the class of representative methods; exact sequences on a fake clock (a nested Update / script / Lua
  turn, priority core > ui > aux with overlap and hidden messages, the save with nested calls, a boundary inside an
  Update frame, 70 nested frames, unbalanced leaves); a stress of three threads on the real clock (about 3,000-7,600
  boundaries in 1.5 s: every turn's rows equal its wall time in ticks, no backwards delta); the proxy's buckets from a
  fake turn block (load, full, partial, lost and late boundaries, an entry being written); a two-thread proxy stress.
- **The reader's tests** `perf/turnstudy/budget/tests/test_budget.py`: 24 pass: a consistent run (transport from both
  sides, taken out of the rows that hold the messages, residual 0, means over full turns only, the exe_alone
  cross-check), `--no-proxy`, and the negative cases with their exit codes and no table written: a missing
  budget.tsv / budget-proxy.tsv / turn-times.tsv (2), not a budget file (2), rows that do not sum (3), a turn on one side
  only and a wall time that differs (4), turn-times.tsv that disagrees (5).
- **test_proxy_rt.py** on b1, switch off: 107/107 (18:14-18:16).
- **Headless proxy route, scenario M, 150 -> 175** (`perf/evidence/4652/ab.sh`: c5h with the proxy, server.exe remote
  mode, `proxy/tools/run_proxy_game.py --mode proxy`, the identity copy of 0438's client database, AI logging off)
  [L 2026-11-16 18:25-18:35, workstation, shared]: pin (switch off), new off, new on, new off, new on, pin: **every
  `stats.db` IDENTICAL to the first pin run** (`tools/d10/statscmp.py --from-turn 150 --expect-turns 150 175`: 15 tables,
  88,369 rows). Host seconds per turn (indications, D106): pin 2.328 / 3.521, new off 2.360 / 2.586, new on 2.313 /
  2.557: no difference visible in the workstation's noise. With the switch on: budget.tsv, turn-times.tsv and
  budget-proxy.tsv in every run; every turn's rows = its wall time; turn-times.tsv's mean (2.313 / 2.557 s over 24
  turns) = the host's own s/turn; per-class call counts of the server and the proxy equal in every full turn; direct
  transport 0.43-1.42 us per message (medians 0.54 and 0.63 in the two runs), about 240,000 messages per turn; headless M per turn (run 3, means
  of 24): game logic 2.05 s, calls into the host 0.12 s, transport 0.13 s, calls from the host 0.008 s, host alone
  0.005 s, save 0 (c5h does not save). The headless host is the EXE's stand-in here, so these rows say nothing about
  the real EXE. Run folders `C:/c5-64bit-work/4652/hl/ab-t175/` (summary `C:/c5-64bit-work/4652/hl/ab-175.log`).
- **The timers' cost** [L host test, single thread, uncontended, 2026-11-16]: server 45-110 ns per frame change (two per
  call); proxy 280-320 ns per top-level call it makes (two clock reads, the busy word twice, the bucket, two adds) and
  120-160 ns per call it serves or makes nested. So about 0.21-0.54 us per call on both sides together, a call being two
  messages: 0.025-0.065 s per headless M turn of 120,000 calls (240,000 messages; 1.1-2.8 % of 2.3 s, not seen in the
  A/B above), 0.11-0.35 s per S4 M turn of 0.54-0.64 M calls (1-3 % of the turn, but 7-28 % of `game_logic` and 19-71 %
  of `transport`, plan 9.7 B3) and 0.15-0.38 s per huge AI turn of 1.41 M crossings (0.7 M calls; 0.16-0.45 % of 85-92
  s) `[I]`: upper bounds from uncontended micro-benchmarks; W5's first series measures the cost as switch on minus off
  (plan 9.7 B3). (Corrected 2026-11-17 by ticket 4659 from 7015 B3; was "0.25-0.55 us per crossing", which mixed the
  per-call and the per-message readings: 0.06-0.13 s per headless M turn, 0.35-0.8 s per huge turn.) The
  rows charge this cost to the rows the hooks sit in (mostly game logic and calls into the EXE); the switch is a
  study instrument, off in every timed comparison that is not about the budget.
- **S4 parity pair** [L 2026-11-16 18:56-19:20, workstation, instance c5h-h (F:, its own DLL `3c2f47da...`
  i0ship-26234371-ia as the fallback), `perf/evidence/4652/s4.sh` / `s4series.sh`: `provision.py set-proxy` of the arm,
  SQLITE_LOGGING on in the instance's CP copy backed up and restored by SHA-256, `perf/profilekit/acceptance.py run` (copy
  of `TS0438-M-T150` by hash, cp + vp + eui, autoplay, frame cap lifted, safe stop), stats.db and the run folder copied
  out, `set-proxy --original`; `provision.py verify c5h-h` ok at 19:36, claim released]: 25 turns of M (150 -> 175),
  the pin layout `x64-04b1211b-pfdee656` (server `5B254430`, proxy `75C40CEC`) against b1 (server `994651B6`, proxy
  `7DB09E56`), switch off in both: **statscmp IDENTICAL**, 15 tables, 90,457 rows, `--expect-turns 150 175`
  (`perf/evidence/4652/s4/statscmp-pin-vs-new-off.txt`). Driver turn times (5 s poll; indication): pin 18.06 s/turn mean
  (12.2-22.6), b1 off 17.85 (13.5-22.4).
- **S4 with the switch on** (5 turns of M; b1 18:20-19:25, and b2 19:30-19:35, which only changes when the proxy writes
  a turn, below): the same game as the pin run over turns 150-154 (`perf/evidence/0438/idtrace.py 150 154`: 14,398 rows
  IDENTICAL, pin = b1 off = b1 on = b2 on). b1's budget-proxy.tsv lacked turn 154: the proxy wrote a turn only after
  the NEXT boundary, and the harness's stop ends the EXE without the proxy's shutdown, so the last full turn was lost
  (budget.py exit 4, "turn mismatch", as designed). Fixed in b2 (proxy `B73E1D25`, server `4AF51826`, helper `5DFFEF62`;
built 2026-11-16 19:28 from repo commit `4d921fc6a`, repo_dirty=0, per `F:/c5-64bit-work/4652/b2.log`'s build id, read
by ticket 4664; layout `F:/c5-64bit-work/4652/layout-b2`; not "RC + 4652": `git diff fdee65668 4d921fc6a` (the RC's tools commit against b2's) has 78 product files under `server/src`, `proxy/src` and `proxy/include`, 33 of them in remote mode or the proxy (crash filter, the D118 fatal report, leader scene, save variant, remote feed, besides the budget) [C, ticket 4664, 2026-11-17], so b2 against the RC is not the timers' cost; the budget series uses bb = RC + 4652's commits only (arm S4b), plan 9.7 B3): a turn is written
  50 ms after its end boundary. b2's run joins: every full turn's rows sum to its wall (residual 0), per-class call
  counts equal on both sides in 151-154, turn-times.tsv = wall. **First S4 budget of M** (b2, turns 151-154, means;
  workstation, shared, frame cap lifted, ONE run: an indication only, D106): wall 11.74 s; the EXE alone 8.03 s (68 %);
  game logic 1.42 s (12 %); calls from the EXE 0.78 s (UI Lua 0.68); calls into the EXE 0.63 s (GameEvents and the
  DLL's Lua in the engine 0.58); transport 0.53 s (about 1.1 M messages at 0.46-0.50 us); save 0.34 s (one autosave per
  turn, 0.44-0.49 s, charged to the turn whose Update called it; turn 154's fell after the run's last boundary). Overlap
  (two service threads busy) 0.04-0.05 s per turn; 36,000-71,000 messages per turn hidden behind the core thread's work.
  The b1 run (frame cap lifted, a busier minute) gave wall 19.1 s, the EXE alone 13.9 s, game logic 2.36 s, calls from
  the EXE 1.39 s, into the EXE 1.09 s, save 0.42 s (`--no-proxy`). Evidence `perf/evidence/4652/s4/`.
- The acceptance driver records one picture per turn labelled "Acceptance picture (D151, D112)" (58 entries, issue 4652);
  they are DIAGNOSTIC (test layouts through set-proxy, frame cap lifted), not acceptance pictures; `visualcheck.py amend`
  refused their ids ("no entry ... in D:/c5-64bit-work/visual-checks/index.jsonl"), so they are not relabelled.
- **Final checks on b2** [L 2026-11-16 19:36-19:42]: test_proxy_rt 107/107 with the switch off and 107/107 with
  `VP64_TURN_BUDGET=1` (the loader cases then write budget.tsv and budget-proxy.tsv into the same D28 run folder);
  `run_budget_test.py --asan` 209/209 in each of the four builds (the settle rule's checks added), canaries caught.

### Not verified (4652)

- Nothing on the laptop or oldpc; every number above is from the shared workstation (indications, D106), one S4 run
  with the switch on per build. The budget's shares need W5's series on the laptop.
- The S4 parity pair is b1 against the pin; b2 differs from b1 only in when the proxy writes a turn (code that runs
  with the switch on); b2 off was checked by test_proxy_rt and b2 on by the identity trace over 150-154, not by a
  25-turn pair.
- Scenario H; a human seat; the stock-session (pack=1) layout; a long session (the 48-bit word wraps after about 325 days
  at 10 MHz; the proxy's 56-bit word is wider).
- The timers' cost inside a real S4 turn: estimated from the host-test micro-benchmarks and the crossing counts (above);
  the headless A/B showed nothing beyond the workstation's noise; no grade-A A/B was run.
- Whether `save` should also hold the EXE's work after `AutoSave` returns (none is expected: the EXE writes inside the
  call) and the attribution of a turn's autosave to the turn that ends with it (the save is called from the Update that
  changes the turn, before the boundary).

## The series tool and premise P3 (ticket 4651, W2; 2026-11-16)

Written 2026-11-16 by the implementer of 4651 (account A), from 17:35 EST. The tool: `perf/turnstudy/runner/`
(`series_job.py` the testbed job, `submit.py` the submit and stop helper, `clientarm.py`, `headlessarm.py`,
`sampler.py`, `runrecord.py`, `analyze.py`, `arms.json`, `scenarios/M.json` and `H.json`) and
`perf/turnstudy/clock/clock.py`; how to run it is in `perf/turnstudy/README.md`. Times are EST, laptop clock.

### P3: an unattended client run on the laptop `[L, 2026-11-16]`

Job `20261116-180929-afc5` (commit f8adb780; fetched to `F:/c5-64bit-work/4651/`): `submit.py run --host laptop
--scenario M --arms S2i --runs 1 --turns 3 --quiet`, i.e. `tb.py submit --headed --quiet`, instance `tb1` (the laptop's
own Steam EXE), S2i = the stock EXE with `i0ship-26234371-ia` (`3c2f47da...`, the instance's own DLL, checked by hash).

| Step | Result |
|---|---|
| Quiet and headed together | **yes**: a headed job is submitted `--quiet` as well (4509 and 4602 did the same); it runs in the owner's logged-on session through the session helper and holds both runner slots, so nothing else starts on the laptop meanwhile. Quiet proof: alone = true, undisturbed = false, foreign cores 0.091 (try 1: 0.117; the busiest others at the run's ends: the runner's Python, Defender `MsMpEng` 13.8 % for one second, `audiodg`). The runner needs nothing more than `--quiet --headed` and the epic's `quiet@laptop` place. |
| Save | `TS0438-M-T150` shipped by `--file` (content-addressed), copied into the instance as `TS4651-TS0438-M-T150` by SHA-256 `98fc454a...`, removed by hash at the end |
| Client database | the instance's `cache/Civ5DebugDatabase.db` after the load with cp, vp, eui: 0438's `dbinfo.py` fingerprint **`02b5faa4...` = 0438's** (integrity ok, 832 tables, 43 playable civilisations, 120 minor, SQLITE_LOGGING 0): the same game data as the saves |
| Window, frame cap | `GraphicsSettingsDX11.ini` WindowResX/Y 1920 x 1080 (was 1536 x 864; backed up and restored by hash), FullScreen 0, WaitForVSync 1 as found; a timing launch (`cli.py launch --timing`: no frame cap), assert policy `hold`, the harness's dialog watcher (D155) |
| Autosave | `UserSettings.ini` [AutoSave] TurnsBetweenAutosave 1, NumAutosavesKept 999 (already so) |
| Launch -> window | 12.1 s |
| Boot + load (launch to in game, load screen closed) | **113.5 s** (try 1: 118.4 s); the channel is silent during the load, so the end is seen within about 12 s |
| Turns (autoplay 3, `Game.SetAIAutoPlay(3, 0)`) | 150 -> 153, handed back; per turn (channel poll clock) **28.9, 16.2, 18.3 s**; the EXE's CPU 40.1, 15.3, 17.5 s per turn; effective clock 95-132 % of the rated 2808 MHz |
| Memory (EXE, 32-bit) | private 1710 -> 1846 MB, working set 1388 -> 1533 MB, free address space 1659 -> 1536 MB, largest free block 1605 -> 1485 MB; machine free commit 5.7-5.9 GB |
| Stop | the harness's safe stop with the human-turn hook, rc 0 in 23.1 s |
| Restore | front end restored, the backup (16 files) restored by hash, save copy and the run's autosaves moved out, `provision.py verify` rc 0 (5.1 s) |
| Whole job | 18:09:55 -> 18:13:31, 3 min 36 s for 3 turns; power: AC online, battery 100 %, plan Balanced (read only) |

So **a client series can run unattended on the laptop**. Estimate for a 25-turn client run on M: ~10 min (load 2 min,
25 x ~17.5 s, stop and restore 30 s). On H (0438: 85-92 s per client turn on the workstation): about 45-50 min.

### The client's turn clock in autoplay (input for P2, ticket 4650) `[L, 2026-11-16, both P3 jobs]`

- **No regular autosave times the autoplayed turns.** With TurnsBetweenAutosave 1, autoplay armed at 150 wrote one
  `AutoSave_0150` 25.4 s after the arm (the end of the first AI turn) and no `AutoSave_NNNN` after it, also none when the
  human turn came back at 153 (try 1 `20261116-175321-809b`, 10 min watched; try 2 the same).
- **The DLL's post-turn autosave is written every autoplayed turn**: `AutoSave_Post_0150` +0.9 s after the arm,
  `AutoSave_Post_0151` +27.3 s, `AutoSave_Post_0152` +43.9 s, about 1.3-1.6 s before the channel's 1 s poll saw the
  new turn (151 at +28.9 s, 152 at +45.1 s, 153 at +63.4 s). It comes from `CvPlayer::setTurnActive` of the first
  alive player in single player (`CvPlayer.cpp`, "create autosaves after the first player's (human) turn") [C]; the
  regular one is the EXE's. None closes the last played turn: autoplay hands back without one.
- So the plan's autosave clock (section 4, `turn.study_s_per_turn@1`) works in S2/S4 only through the `_Post_`
  saves, and only for turns T..T+N-2 unless one extra turn is played. The runner records three clocks for every client
  run and puts the first complete one into `turns.tsv`: a Lua handler (`os.clock()` at the first
  `GameEvents.PlayerDoTurn` of each turn), the `_Post_` autosave times, the 1 s channel poll (each also in its own file).
- **`GameEvents` is nil in the channel's default state (Main)** [L, try 2: `attempt to index global 'GameEvents'`];
  the handler is installed in the InGame state since commit (after f8adb780). `os.clock()` answers in Main (117.4 s
  at the arm = the EXE's age).
- **The three clocks agree** (dry round S2i, 25 turns, below): the handler and the `_Post_` saves differ by -0.20 to
  +0.18 s per turn (median 0.01 s) on turns 151-173; the first turn differs by 0.8 s (the handler starts at the arm's
  `os.clock()`, the save clock at the runner's own time of the arm, one channel round trip earlier); the 1 s poll
  differs by up to 2.1 s per turn but its 25-turn sum is 0.8 s from the handler's (528.99 against 528.21 s). The handler
  in the InGame state timed all 25 turns in S2i and S4 [L, job `20261116-181943-971a`].

### The dry round of M (functional, not timing) `[L, 2026-11-16 18:20-18:45, laptop]`

Job `20261116-181943-971a` (commit c0bfb641, `--quiet --headed --keep-going`, grade D; fetched to
`F:/c5-64bit-work/4651/laptop-20261116-181943-971a/`): every available arm once on M, 25 turns, in the order S1t, S1i,
S3, S2i, S4 (S2t refuses until ticket 4650). One run per arm: **indications for P5, no claim** (PERFORMANCE.md;
the plan's review is not in).

| Arm | Exit | Boot + load | s/turn (25) | CPU s/turn (main) | Memory at the end (main) | Run wall |
|---|---|---|---|---|---|---|
| S1t (c5h, the tag's DLL `262aee88...`) | **5: crash** | 5.0 s | turns 150-151: 1.19 | - | - | 11.7 s |
| S1i (c5h, `i0ship-26234371` `f92881a3...`) | 0 | 4.5 s | 1.514 (check job: 1.529) | 1.66 | 196 MB private, free VA 3789 MB | 47.0 s |
| S3 (`server.exe` direct, `x64-04b1211b-pfdee656`) | 0 | 3.4 s | 1.278 (check job: 1.294) | 1.26 | 260 MB private (check job) | 38.8 s |
| S2i (stock EXE + `i0ship-26234371-ia`, tb1) | 0 | 115.2 s | 21.13 (handler clock) | EXE 20.8 | EXE 1896 MB private, free VA 1485 MB, largest block 1447 MB | 680.5 s |
| S4 (EXE + proxy `75C40CEC...` + server, pin layout via `set-proxy`) | 0 | 123.5 s | 21.41 | EXE 18.9, server 4.56 | EXE 1715 MB, server 205 MB | 700.0 s |

- **S1t crashes after the load**: c5h exit `0xC0000094` (integer divide by zero) during turn 152, 7 s after the load
  (`out/runs/r01-p1-S1t/console.txt`, `host exit 3221225620`). That is bug 0078's signature: the stock 5.4.8 DLL
  divides by the world's EstimatedNumCities, which a load does not restore
  (`research/04-memory-investigation/leaks-and-growth.md`, "Bug 0078"; fixed in i0-ship by 27176dbfe) [I: exit code
  and turn; not symbolised]. So **the tag's DLL cannot play the study's 25 turns from these saves in c5h**; in the
  stock EXE the pregame may hold another value and play on with a different religion AI (the same section), which
  fits 0438's "the tag's DLL plays a different game from the first turn". For P1 (ticket 4650): S1t as defined
  cannot run; S2t (refused here) may run but with that defect. `arms.json` notes it.
- The server's own per-turn line in remote mode (`turn-times.tsv`, ticket 4652) was not there in S4 (pin layout
  04b1211b); the runner notes "not available" and reads it when it exists.
- The EXE's working set fell from 1526 to 756 MB over S2i's 25 turns (1366 to 624 MB in S4) while its private bytes
  rose 1837 to 1896 MB [L]: Windows trims the game window's process in the owner's session (screen locked or off [I]); the
  working set of a client on the laptop is not the game's need.
- First turn after the load: 27.5 s in S2i (EXE CPU 41.9 s), against 16.7-25.6 s for the turns after; the per-turn
  medians will show it.
- Headless fixes after the round: the server's last row came at its shutdown (private 65 MB against 257 MB a turn
  earlier); hosts now play one untimed turn more (check job `20261116-184631-7d7a`: S3 last row 259.9 MB), and the
  first timed turn's CPU (which holds boot and load) is left out of the headless CPU means. The runner's own process is
  sampled as role `runner`: 0.00-0.03 CPU s per headless turn [L, check job].

**P5 (wall time on the laptop, indication):** a round of M with S1i, S3, S2i, S4 takes about 25 min (47 + 39 + 681 +
700 s plus the series' gaps); 6 rounds about 2.5 h, 12 about 5 h. H is not measured: from 0438's times (headless 21-25
s/turn, client 85-92 s/turn on the workstation) about 2 h per round on the laptop [I], 6 rounds about 12 h.

## Series of the study (ticket 4653, W2b; from 2026-11-16)

Written by the implementer of 4653 (account A) from 2026-11-16 20:19 EST. The runner now follows section 9 of
`turn-time-study-analysis.md` (7014's R1-R20); what each run does is in `perf/turnstudy/README.md` ("What every run
does"). Times EST; machine `laptop` (rated 2808 MHz, 8 logical processors). Every job here was a quiet testbed job.

### R18: the effective clock moves with busy cores `[L, 2026-11-16 20:45-20:50, laptop]`

Job `20261116-204444-e62e` (commit 5f662337, `perf/turnstudy/clock/clockcheck.py --phase-s 30 --cores 0,1,2,4,1,4,0`;
quiet proof: alone, undisturbed, foreign cores 0.028). `\Processor Information(_Total)\% Processor Performance` read
once a second through PDH without administrator rights, the first 5 s of each phase left out, 25 reads per phase; a
busy core is a pure-Python spin process. Evidence: `perf/evidence/4653/clockcheck.txt` and `.json`.

| Busy cores | % Processor Performance mean (sd, min-max) | Effective clock (x 2808 MHz) | % Processor Time |
|---|---|---|---|
| 0 (first) | 79.8 (29.2, 32.5-116.8) | 2.24 GHz | 2.5 |
| 1 | 133.1 (1.4, 131.0-134.8) | 3.74 GHz | 13.4 |
| 2 | 127.3 (0.7, 125.1-128.4) | 3.57 GHz | 26.2 |
| 4 | 122.1 (0.3, 121.4-122.7) | 3.43 GHz | 51.1 |
| 1 (after 4) | 126.9 (5.0, 121.2-134.8) | 3.56 GHz | 21.7 |
| 4 | 122.4 (0.5, 121.7-123.3) | 3.44 GHz | 51.4 |
| 0 (last) | 77.2 (28.6, 34.4-113.1) | 2.17 GHz | 1.2 |

- **The counter moves**: one busy core 133 %, four 122 %, about 8 % less clock with four cores busy [L]; a single core
  that follows four busy cores starts at 121 % and climbs back (sd 5.0 against 1.4): the chip's heat carries over,
  which is what R7's balanced order and 60 s idle gap are for. Idle it swings 32-117 % (the counter is a mean over
  sometimes-idle cores). So `machine.processor_performance_pct@1` x the rated 2808 MHz is the effective clock on this
  machine; the runner records it at every turn boundary and at each run's start (R7).
- `\Processor Information(_Total)\Processor Frequency` stays at 2808 MHz in every phase: it is the rated clock, not
  the running one, and is not used.
- **Temperature is readable** without administrator rights: `\Thermal Zone Information(*)\Temperature` gives one zone
  (`\_TZ.THM0`, 63.9 C at the start); the runner records it at each run's start. (The workstation has no thermal-zone
  instance: PDH_NO_DATA.)
- **`w32tm /query /status` fails on the laptop**: "The service has not been started (0x80070426)" [L]. The Windows Time
  service is not running there, so it cannot step the clock during a run; the runner records the answer before and
  after each run as R5 asks, and a different answer at the end (the service started) would show.
- Not done: a GHz metric id. `perf/ledger/record.py` has no GHz unit, so `machine.effective_clock_ghz@1` (plan 9.5)
  cannot be registered without a change to that shared file; the records carry `machine.processor_performance_pct@1`
  and the rated MHz, the analysis computes GHz.

### R12: what statistics logging costs S1i on M `[L, 2026-11-16 21:00-21:26, laptop; grade A by PERFORMANCE.md 2.7's treatment exception]`

Job `20261116-205826-5ab6` (commit f8cf3692; `submit.py run --scenario M --arms S1i:stats_on,S1i:stats_off --runs 6
--order williams --treatment stats_logging --quiet --grade A --series-id 4653-R12-M-logging`; quiet proof: alone,
undisturbed, foreign cores 0.061 for the job). c5h with `i0ship-26234371` (`f92881a3...`), AI logging off, the window
151-175 of `TS0438-M-T150` (played 150-176); logging on = 0438's identity copy of the database (SQLITE_LOGGING 1,
`9335186b...`), off = the plain client database (`2122c08b...`); order AB, BA, ... (a Williams square of two), a 60 s
idle gap before every run. Fetched to `F:/c5-64bit-work/4653/laptop-20261116-205826-5ab6/`; the 14 sealed records are in
`F:/c5-64bit-work/0437/runs/2026-11/`; the analysis text in `perf/evidence/4653/r12-analysis.txt`.

| Arm | Runs counted | s/turn mean (sd) | 95 % interval | CPU s/turn | Peak private | Growth |
|---|---|---|---|---|---|---|
| S1i, logging off | 5 of 7 | 1.568 (0.006) | 1.560-1.575 | 1.555 | 202 MB | 0.43 MB/turn |
| S1i, logging on | 7 of 7 | 1.632 (0.006) | 1.626-1.637 | 1.616 | 211 MB | 0.54 MB/turn |

- **Statistics logging costs S1i on M +0.064 s per turn (+4.1 %), Welch 95 % interval +0.056 to +0.072 s (v 8.8;
  ratio 1.041, 1.036-1.046 by the log-scale Welch interval); detectable difference 0.011 s.** CPU per turn +0.061 s
  (+0.051 to +0.072): the cost is the DLL's own work on the game thread, not waiting for the disk (D35's pragmas are in
  i0-ship). Memory growth +0.11 MB/turn (+0.06 to +0.15); the peak's difference (+8.7 MB) is not resolved.
- So the player's workload (VP's default: logging off) for S1i on M is the measured logging-on mean less 0.064 s/turn
  (interval above). This is measured for S1i on M only; it is not carried over to the clients or to H without a
  measurement (the DLL's statistics work grows with the game; on H it was +13 % in P1b's confounded workstation
  indication with the tag's DLL).
- **Identity:** every logging-on run wrote the same `stats.db` byte for byte (SHA-256 `9dea0074...`, 4,211,712 bytes,
  turns 150-177); statscmp within the arm: identical.
- **Exclusions (R8):** two logging-off runs (round 1's and the re-run's) were excluded by the runner's first disturbance
  measure (machine CPU % x cores less the run's sampled processes): 0.272 and 0.298 cores against the limit 0.25. That
  measure counts kernel time and c5h's wrapper and read 0.17-0.30 in every run while the testbed's own proof for the job
  read 0.061, so it was replaced (commit e151f69f3) by the testbed's measure per run (CPU of processes outside the
  job's tree, from process snapshots at the run's start and end). The two excluded runs were the slowest logging-off
  runs (1.581, 1.591 s): with all 14 runs the cost is +0.059 s (+3.7 %), interval +0.048 to +0.069 s, the same
  conclusion.
- Start conditions recorded per run: the thermal zone 46-54 C, the effective clock at the start 1.5-3.0 GHz (one
  second of a mostly idle machine: not the clock under load, which the per-turn rows give: 131 % = 3.69 GHz in both
  arms), power AC, 100 %, Balanced as found; w32tm "service not started" before and after every run.

### The functional round of M with every arm (not timing) `[L, 2026-11-16 21:33-22:19, laptop]`

Job `20261116-212635-c52b` (commit b2025c03, grade D, records `not_for_timing`; `--arms S1i,S3,S2i,S4,S2t --runs 1
--keep-going --count-arms S1i,S3 --count-targets window`; instance `tb1`; fetched to
`F:/c5-64bit-work/4653/laptop-20261116-212635-c52b/`; `perf/evidence/4653/functional-round-analysis.txt`). Every arm
ended with exit 0, its window T+1..T+25 = 151-175 timed in full by its own clock, the instance restored and
`provision.py verify` rc 0 after each client run. One run per arm: **indications, no claim**.

| Arm | Clock | s/turn (151-175) | Boot + load | CPU s/turn | Peak private | Save (R4) |
|---|---|---|---|---|---|---|
| S1i (c5h, logging on) | host line | 1.658 | 5.2 s | 1.645 | 220 MB | - |
| S3 (server direct, logging on) | host line | 1.392 | 3.2 s | 1.375 | 262 MB | - |
| S2i (stock EXE + i0-ship-ia, logging on) | PlayerDoTurn | 21.31 (autosave clock 21.31) | launch to in game 113.8 s; process to T+1 145 s | EXE 20.51 | EXE 1925 MB | 0.45 s |
| S4 (the RC, logging on) | PlayerDoTurn | 21.38 (autosave clock 21.38) | 123.7 s; process to T+1 155 s | EXE 18.23, server 4.68 | EXE 1748, sum 1952 MB | 0.23 s |
| S2t (stock EXE + the tag's DLL, add-in, logging off) | autosave | 20.63 | 119.5 s | EXE 19.29 | EXE 1882 MB | 0.56 s |

What the round established `[L]`:
- **S4 is the RC as a player has it**: `set-proxy` with the RC's own `vpproxy.ini` as the template and the tag's DLL as
  the fallback, plus the RC's `manifest.json`, `purity.tsv` and `vp64helper.exe`, gives the player's `(1) Community
  Patch` layout byte for byte (11 files checked against the staged folder: proxy `75c40cec`, fallback `262aee88`,
  `vpproxy.ini` `70f2c066`, VP64's seven files, helper `b8ac9a64`); the proxy booted the shipped pack (`boot: pack ...
  ok: release Release-5.4.8, 32-bit DLL 262aee88... matches x86_dlls`, `ServerRenderer = 0 (default)`, server attached
  after 1.06 s), no FALLBACK, 0 LOUD lines; the extras removed by hash and `set-proxy --original` rc 0 afterwards. No
  change outside the runner was needed.
- **S2t runs**: `provision.py set-dll` put the tag's DLL in for the run and `set-dll --previous` put `3c2f47da...` back
  (hash checked); the test-only add-in armed 27 autoplay turns (`AutoSave_Post_0150` written), the window was timed by
  the `AutoSave_Post_N` files (25 of 25), the harness's no-channel stop (`stop_nolua`, 62 s) ended it, the add-in was
  removed by hash. Its client database fingerprint is `02b5faa4` (logging off) as the scenario says.
- **Logging on in the clients**: the client database's fingerprint with SQLITE_LOGGING 1 is `4e4620b2...`, the one
  computed beforehand from 0438's identity copy, in S2i and S4 [L]; every client run moved the instance's stats.db aside
  (there was none) and kept its own.
- **Identity (R1, R12)**: statscmp of the runs' own `stats.db`, turns 150-177: **S1i = S3 identical; S2i = S4
  identical** (one run each).
- **No channel call in the window**: S2i and S4 took their boundaries from the `AutoSave_Post_N` files; the
  PlayerDoTurn handler timed all 25 turns; the clock tie drifted 7.4 ms (S2i) and 1.5 ms (S4) between the start and the
  end (round trips 43-69 ms); autoplay handed back 27-28 s after the window's last save (turn 176 played untimed).
  The PlayerDoTurn clock and the autosave clock agree on the 25-turn mean to 0.004-0.005 s.
- **Memory at a fixed tick**: 2 s in every arm (295-299 ticks per client run, 21-26 per headless run); the memory
  column of a boundary was at most 2.0 s old.
- **R2, engine calls per window turn** (count runs to 151 and 176, the difference over 25 turns): **S1i 405,281,
  S3 359,242**. With c5h's counting cost of 0.2-0.6 us per call, c5h's own overhead is 0.08-0.24 s of S1i's 1.66 s
  turn (5-15 %) [I: the per-call cost is PERFORMANCE.md 4's range, not measured here].
- **R9 window state: not met in this round.** The window was on screen and not minimised in every boundary row, but the
  foreground belonged to another window in all of them (`fg` 0). Since commit 8268ff744 the runner brings the game's
  window to the foreground before the window (`tools/testbed/jobs/steam_launch.py`'s method, topmost then not
  topmost, an Alt key press, SetForegroundWindow; refused off a testbed machine by that file's real-input fence) and
  records whether it held at every boundary; checked in the next section.
- The save's duration (R4) here is the listing's first sight to the last write (0.23-0.56 s); the listing runs every
  0.5 s in the window, so it understates P2's 0.5-1.0 s; P2's numbers stand for R4.

### R9: the game window in the foreground (check) `[L, 2026-11-16 22:23-22:34, laptop; one run per arm, 2 turns: indication]`

Job `20261116-222311-8a92` (commit 8268ff74, `--arms S2i,S2t --turns 2`, grade D). The runner brought the game's window
to the foreground (S2t at once after the launch, S2i after the arm): `SetForegroundWindow` accepted and the game's
window was the foreground window at every boundary of both runs (`fg` 1, `iconic` 0). The same turns as the
functional round, where another window held the foreground:

| Turn | S2i background (functional round) | S2i foreground | S2t background | S2t foreground |
|---|---|---|---|---|
| 151 | 16.91 s | 12.43 s | 16.48 s | 11.92 s |
| 152 | 19.47 s | 13.37 s | 19.55 s | 13.06 s |

- **A client turn is about 26-33 % shorter with the game's window in the foreground** [L, one run of 2 turns per arm and
  state: an indication]; the EXE's CPU per turn rose (S2i 27.2 CPU s for 12.9 s of wall time, against 20.5 for 21.3 in
  the background): Windows gives the foreground process more of the machine. R9 (7014's review) named this; the client
  times of P3, P5 and 4651's dry round were all taken in the background and are not comparable with the timed series.
- The analysis reports each client run's foreground share over the window (`fg_share`); a run that loses the foreground
  is not an R8 exclusion as R8 is written: a proposal for the lead (it changes a client turn by a quarter).

### The M timed series (done 2026-11-17 about 04:36, rc 0; results below) `[L, 2026-11-16 22:35 - 2026-11-17 04:36, laptop]`

Job `20261116-223507-e8da` (commit fb0ecaef9; `submit.py
run --scenario M --arms S1i,S2i,S3,S4 --runs 12 --order williams --tag-arms S2t --tag-rounds 4,8,12 --count-arms
S1i,S3 --count-targets all --quiet --grade A --series-id 0437-M-timed-1 --timeout 36000`): one quiet headed job, the
testbed having no limit on a job's time; 3 Williams squares of S1i, S2i, S3, S4 (round 1 S1i, S2i, S4, S3; round 2
S2i, S3, S1i, S4; ...), S2t last in rounds 4, 8 and 12, a 60 s idle gap before every run, then the count runs of S1i
and S3 to every turn 151-176 (R2, per-turn engine calls). Expected end about 05:00 EST 2026-11-17 (about 26 min per
round with the window in the foreground, 3 S2t runs, 52 count runs).

**Round 1 checked** (fetched with `--include "runs/r01-*"` to `F:/c5-64bit-work/4653/laptop-20261116-223507-e8da-r01/`):
every run exit 0, 25 of 25 window turns by its own clock, no exclusion, no dialog, the instance restored and verified
after each client run, the game's window the foreground at 25 of 25 boundaries in S2i and S4, the client database
`4e4620b2` (logging on) in both; statscmp of the runs' own `stats.db`: **S2i = S4 identical** (15 tables, 99,202 rows)
and **S1i = S3 identical** (96,874 rows). Round 1 values (one round, no claim): S1i 1.674, S2i 15.517, S4 15.554, S3
1.427 s/turn. The analysis of the whole series is the lead's next step (`analyze.py <fetched job> --records-to
F:/c5-64bit-work/0437`).

**GPU drivers and Windows Update during the series** `[L, 2026-11-17 01:56 EST, laptop, one read-only SSH query of
the registry and the System event log while the series ran]`. After Windows Update replaced oldpc's NVIDIA driver at
01:38-01:40 (`../06-tooling-and-workflows/quiet-testbed.md` 25): the laptop has **two GPUs**, Intel HD Graphics 630
driver 27.20.100.9320 (2021-04-07) and NVIDIA Quadro P3000 driver 32.0.15.7322 (2025-05-11) (plus Microsoft Remote
Display Adapter). The run records' `start_conditions.gpu`/`gpu_driver` name only the Intel one (27.20.100.9320 at
22:49, r01-p3-S4), because `perf/ledger/record.py` (machine facts) keeps the first display-class adapter whose name
is not "basic" and stops [C]; so **which GPU the game renders on is not recorded** [to settle before H; every series
names both drivers; a list of all adapters in `record.py` is asked of the MC, outside 0437's touches]. Windows Update since 2026-11-16 22:30: one Defender security-intelligence update (KB2267602, 1.463.337.0),
download and install 2026-11-17 01:23:15-01:23:29 EST; no driver install (no UserPnp events), no reboot pending. So no
driver changed by 01:56; the run in progress at 01:23 gets a note at the fetch (a Defender definition update can
rescan files) [I for its effect]. The check at the series' end repeats the query (MC #3470: the MC then sets the
laptop's no-driver-updates policy, D175, before H).

#### Results of the M timed series `[L, laptop, 2026-11-16 22:36 - 2026-11-17 04:11 EST (timed runs); analysed 2026-11-17 05:05 EST by lead-0437 (fourth lead); grade A by PERFORMANCE.md for the pairs marked so]`

Inputs: job `20261116-223507-e8da` (commit fb0ecaef9), fetched to `F:/c5-64bit-work/0437/M-timed-1-e8da/` (default
fetch, then the `stats.db` files with `--include`); the 51 sealed records
rewritten to R19's ids (`runrecord.py --rewrite`, 51 of 51 ok) and copied by `analyze.py --records-to
F:/c5-64bit-work/0437`; output `F:/c5-64bit-work/0437/M-timed-1-e8da/out/analysis.{json,txt}`. Scenario M (Standard
Communitu_79a from `TS0438-M-T150`, window 151-175, 25 turns). Machine: laptop, GPU drivers Intel HD Graphics 630
27.20.100.9320 and NVIDIA Quadro P3000 32.0.15.7322 (all 51 records name the Intel one, the first adapter, at
27.20.100.9320: no driver change in the series; which GPU renders is not recorded, ticket 0476). Statistics logging on
in every arm (client database `4e4620b2`). Builds as named in the series' records (`binaries_sha256`).

| arm | n | s/turn mean | 95 % CI | cv | CPU s/turn | peak private MB | growth MB/turn |
|---|---|---|---|---|---|---|---|
| S1i (stock DLL, 32-bit headless) | 12 | 1.665 | [1.659, 1.671] | 0.58 % | 1.652 | 220 | 0.40 |
| S3 (x64 DLL, headless) | 12 | 1.424 | [1.403, 1.446] | 2.33 % | 1.409 | 262 | 0.52 |
| S2i (stock DLL in the stock EXE) | 12 | 15.440 | [15.407, 15.472] | 0.33 % | 28.974 | 1837 | 2.23 |
| S4 (x64 DLL in our shell, increment 1 RC) | 12 | 15.266 | [15.189, 15.342] | 0.79 % | 24.589 (server 5.849) | 1672 (+ server: 1877) | 1.15 |
| S2t (stock DLL, the tag's game; description only) | 3 | 14.645 | [14.519, 14.772] | 0.35 % | 27.811 | 1796 | 2.47 |

The primary comparisons (R13; Welch on run means, 95 % CI):

- **S3 - S1i (the DLL headless, x64 against the stock 32-bit; same game, grade A): -0.240 s per turn, -14.4 %, CI
  [-0.262, -0.219]**, ratio 0.855 [0.843, 0.868]. `analyze.py` names a c5h counting overhead of 0.081-0.243 s per turn
  for this pair (the plan's section 9); how it splits between the arms is not settled here. Peak private +41.6 MB (+18.9 %,
  [+32.1, +51.2]); the growth slope is not resolved (+0.13 MB/turn, CI crosses 0). Identity: every S1i and S3 run
  IDENTICAL within its arm and S1i = S3 (statscmp, turns 150-177).
- **S4 - S2i (the whole client, our shell and the x64 DLL against the stock game; same game, grade A on time): -0.174 s
  per turn, -1.13 %, CI [-0.254, -0.093]**, ratio 0.989 [0.984, 0.994]. The player's peak private memory, S4's EXE +
  server (1877 MB) against S2i's EXE (1837 MB), +40.2 MB (+2.2 %); S4's EXE alone peaks 165 MB lower (1672 MB); the
  player's growth (S4's sum 1.66 against S2i's EXE 2.23 MB/turn) -0.57 MB/turn (-25.7 %), S4's EXE alone 1.15 MB/turn.
  CPU per turn -4.4 s (-15 %, description). *(Corrected 2026-11-17 by impl-4667: `analyze.py` pairs S4's EXE +
  server sum with S2i's EXE (lines 242-247); was "Peak private of the EXE +40.2 MB (+2.2 %) (the server adds about
  205 MB: S4's sum 1877 MB); EXE growth -0.57 MB/turn (-25.7 %)".)* Identity: S2i's 12 runs IDENTICAL among themselves, S4's 12 likewise, and S2i = S4 IDENTICAL (statscmp,
  turns 150-177; the S2i databases fetched 2026-11-17 05:30 with `--include "runs/*/stats.db"` in a laptop gap; that
  fetch also overwrote the rewritten records with the job's originals, so the rewrite was run again: re-run
  `runrecord.py --rewrite` after any later fetch of this job).
- Descriptions (different games, no claim): S2i is 9.27 times S1i (+13.77 s per turn); S4 is 10.72 times S3. **The
  client's share of an M turn is about 89 % in the stock game (13.8 of 15.4 s) and about 91 % in ours (13.8 of 15.3 s);
  the x64 DLL's 14 % gain headless shows up as 1.1 % in the client**, because the turn is paced by the EXE (D165 section
  below: one AI player per `Update`, the 0.1 s tick and queue B's drain), not by the DLL.
- S2i - S2t +0.79 s (+5.4 %): the tag's game is a different game (R12's logging is on in both), description only.

Runs marked (none excluded; the conclusions do not move without them):

- **r07-p2-S4** (2026-11-17 01:20:27-01:30:16) ran while Windows Update installed a Defender definition update
  (01:23:15-01:23:29): 15.356 s per turn against the S4 median 15.219, machine minus ours 0.589 cores (typical 0.46).
  Without it S4 - S2i is -0.182 s (-1.18 %), CI [-0.268, -0.096].
- **r08-p3-S3** (01:56:29-01:57:14) at the time of the third lead's read-only SSH query (01:56): 1.414 s per turn, the
  S3 median; no visible effect.
- **r03-p1-S3** (2026-11-16 23:27:08-23:27:57): 1.526 s per turn, +8 % over the S3 median, machine minus ours 0.628
  cores (typical 0.1); the cause is not known [I: a laptop background task]. It alone makes S3's cv 2.33 % (0.62 % without it); without it S3 - S1i is -0.250 s
  (-15.0 %), CI [-0.258, -0.242].

Engine calls per window turn (count runs): S1i 405,281, S3 359,242.

**Report v1 for the owner** (ticket 4667, 2026-11-17): `perf/evidence/0437/4667/report-v1-M.html`, written by
`perf/evidence/0437/4667/make-report-v1.sh` (`perf/turnstudy/report/report.py` with this series, R12's series, 4662's
option 3 series and `context-v1.json`); the H section is a placeholder that the same command fills once
`F:/c5-64bit-work/0437/H-timed-1-*/out/analysis.json` exists. The report grades R12's logging cost B by
PERFORMANCE.md 2.6 (its logging-off arm has 5 counted runs, A needs 6), where the R12 section above says A.

### The H timed series (ready, NOT submitted) `[ticket 4664, impl-4664, 2026-11-17 05:10-, workstation; nothing run on the laptop]`

**S3 on H runs ticket 0452's server; S3 on M stayed on the pin.** MC #3318 ordered H's S3 on 0452's layout (the server's
direct mode calls `ICvMap1::RecalculateLandmasses()` after the load, as the EXE does: 4655's set-up fault fixed, source
`a91e0c3b3`). `arms.json` S3 carries it as a per-scenario override (`"scenario_binaries": {"H": {"x64_bin": ...}}`):
on H the folder `F:/c5-64bit-work/0452/layout-new/bin` is shipped as `ts/x64-0452/bin` with server.exe `FC68B75B...`
and the pin's x64 DLL `29F58E17...`, `lua51_x64.dll` `30724D6D...`, `CvGameDatabase_x64.dll` `9C37B39E...`,
`CvLocalization_x64.dll` `35863EDE...` (each checked by SHA-256 at the submit and again in the job); on M the pin's
folder with server `5B254430...` as before. Chosen over a new base arm because the arm keeps its name `S3`:
`analyze.py`'s grade A pair S1i-S3, its labels and the identity check need no change, and the run record still says
which server ran (`binaries_sha256["x64_bin/server.exe"]`, plus `binaries_scenario: "H"` in `run.json`). Code:
`tslib.for_scenario()` / `arms_for_scenario()` (an override may only replace a role the arm has; `load_arms` refuses
anything else), applied by `submit.files_for` and by `series_job` before any check or run [C, offline tests in
`perf/tests/turnstudy/test_turnstudy.py`, 2026-11-17]. Note: the layout's `build.json` is the pin's, copied by 0452,
and names the old server `5b254430`; the folder's `SHA256.txt` is the right one.

**The c5h side (S1i, S1t).** S1i = S3 holds on H only when the laptop's c5h base is 0452's build (exe `bb8d3542...`,
`server-host.md` 17.5): without it c5h plays without fresh water, lakes and coast adjacency and S1i is another game
than S3 (4655). Before the submit: the laptop's `base/civ5-headless` carries that exe (the lead ships it; not checked
here). The run records do not hash c5h's exe, so the lead names it in the series' log line.

**The command** (from a checkout of `task-4664-h-prep` or the epic branch after its merge; `--print-only` checked
2026-11-17 05:28 EST at `95438c28d` and 05:53 EST at `0fc11b87a` (with the memory cap), rc 0 both;
`perf/evidence/0437/4664/h-print-only.txt`):

```
python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario H --arms S1i,S2i,S3,S4 --runs 12 \
    --order williams --tag-arms S1t,S2t --tag-rounds 4,8,12 --count-arms S1i,S3 --count-targets window \
    --quiet --expect-min 1600 --grade A --series-id 0437-H-timed-1 --issue 0437 --timeout 129600 \
    --native C:/c5-64bit-work/0437-lead4/old/tools/harness/native/bin
```

- Rounds (plan 9.3 R6, R7): 12 Williams rounds (3 squares) of S1i, S2i, S3, S4; the tag arms S1t and S2t last in
  rounds 4, 8 and 12 (3 runs each, fixed positions); the 60 s idle gap before every run; R8's exclusions with the
  excluded rounds re-run at the end. S1t runs on H (bug 0078 refuses it on M only).
- Count runs `--count-targets window` (2 per arm: to turns 301 and 326), not M's `all` (26 per arm): on H a count run to
  turn N plays N-300 turns, so `all` would add about 6 h of untimed play; `analyze.py` takes the window's mean calls per
  turn from the two totals (`window_mean_from_totals`) for R2's overhead line, which is all the report uses of them on M.
  Per-turn call counts on H are therefore not produced [decision of impl-4664; the lead may switch back to `all`].
- What it ships (print-only, every hash checked): S1i `f92881a3`, S2i `3c2f47da`, S3's `ts/x64-0452/bin`, S4's RC
  (`ts/rc/VP64`, proxy `75C40CEC`, `vpproxy.ini` `70F2C066`, helper `B8AC9A64`), the tag's DLL `262aee88` (S1t, S2t,
  S4's fallback), the save `TS0438-H-T300` `308a96bb...`, the three databases of 0438, the harness natives and the
  front-end assets.
- **The harness natives: built before `adcc99d1f` (option (a), accepted by lead-0437).** The laptop is Windows 10 19045:
  0473's child guard (merged at `adcc99d1f`) fails closed there (ticket 0486). Reading the code [C, 2026-11-17]: after
  `adcc99d1f` the harness Python (`tools/harness/winapi.py` `helper_launch`) only sets `C5_CHILD_GUARD` when
  `focus_guard.dll` exists; the check of `child_guard` is in the NEW `bg_helper32.exe` (`shimVerdict`) and the new
  guard; no Python code reads `child_guard` (only the harness's own tests). The natives at
  `C:/c5-64bit-work/0437-lead4/old/tools/harness/native/bin` (built from `13f4eb15a`, which does not contain
  `adcc99d1f`; `bg_helper32.exe` `65356E26...`, `focus_guard.dll` `388121DA...`; neither contains the string
  `child_guard`) ignore the variable, so a checkout of this branch with `--native` pointing there launches as before
  0473. Option (b), a checkout at `13f4eb15a` plus the arm changes, was not taken: more to carry, the same natives.
- **Expected length** `[I]`: plan 9.3's H estimates per run (S1i and S3 about 12 min, S2i about 50, S4 about 37 min,
  all from background-window runs; M's clients ran about a quarter shorter in the foreground, R9) give about 115 min per
  round with the gaps, 23 h for 12 rounds, 3.2 h for the 6 tag runs and about 0.5 h for the 4 count runs: **about
  26.7 h (`--expect-min 1600`)**, plus about 2 h per excluded round re-run. `--timeout 129600` (36 h). The same series
  as three jobs of one Williams square each (`--runs 4 --first-round 1`, `5`, `9`, the same `--series-id`, `--tag-rounds
  4,8,12` in each, `--count-arms` only in the third; about 8-9 h each, `--expect-min 540`) lets the lead check a square
  before the next; `analyze.py` takes the three job folders together.
- **GPU drivers to name** (the M series' state, `[L, 2026-11-17 01:56 EST]`): Intel HD Graphics 630 27.20.100.9320 and
  NVIDIA Quadro P3000 32.0.15.7322. Before the submit: query them again (D175's no-driver-updates policy is the MC's,
  #3470) and write both into the series' log line; since ticket 0476 `perf/ledger/record.py` records every display
  adapter (`gpus`) [C], so H's records name both.
- Not verified: the H series on the laptop (nothing submitted); H's per-run times (the expected length is from plan 9.3's
  estimates); the laptop's c5h base; the old natives on the laptop (read, not run); S2i, S4, S1i, S1t and S2t on H
  through this commit (only S3 ran, on the workstation); the job-object cap under c5h and on Windows 10 (job objects
  with a process memory limit exist since Windows 2000, nested jobs since Windows 8 [I from the API's documentation,
  not run there]).

**Functional check of S3 on H on the workstation** `[L, 2026-11-17 05:40:35-05:42:25 EST, workstation, shared (other
agents' server.exe and games running; the run's foreign cores 6.4, so its time is no indication at all), grade D, no
timing claim (D106)]`: `series_job.py --scenario H --arms S3 --runs 1 --turns 2 --grade D` from a job folder laid out
as a testbed job's (`perf/evidence/0437/4664/fc_setup_and_run.sh`), the binaries copied from `arms.json`'s sources.
Exit 0, window turns 301-302 both played, load 0.55 s, server peak private 370 MB; `run.json`
`binaries_sha256["x64_bin/server.exe"] = fc68b75b...`, `binaries_scenario: "H"` (`perf/evidence/0437/4664/fc-run.json`).
Identity: its `stats.db` against 0452's own H run (`F:/c5-64bit-work/0452/runs/s3new-H10`, which is IDENTICAL to 4650's
client run S2i-H-1), both cut to turn 303: **IDENTICAL**, 14 tables, 66,562 rows, turns 301-303 (turn 304 is cut: the
host stops inside it, so its rows are partial) (`perf/evidence/0437/4664/fc-S3H-vs-0452-s3new-H10.txt`). So the arm,
through the runner, plays the client's game on H.

**Incident: a runaway server.exe on the workstation, 2026-11-17 05:34:32-05:38:25 EST** `[L]`. The first try of this
check ran with forward-slash `TESTBED_*` paths, so `server.cfg` held mixed separators (`run_dir=C:/c5-64bit-work/
4664/fc/work\host-r01-p1-S3`, `perf/evidence/0437/4664/fc-try1-server.cfg`). The server's run-folder check then refused
`FIFileSystem::Create` of `cache\Civ5SavedGameDatabase.db` ("only files inside the run folder"), the DLL asserted
"Cannot open Civ5SavedGameDatabase.db for write!" (CvGame.cpp:11141), and the load did not fail but allocated: server.exe
(0452's `fc68b75b`, pid 24176) reached about **131 GB private bytes, 37.8 GB peak working set** before a C++ exception
(0xE06D7363, bad_alloc's signature [I]) ended the load phase after 230 s (`fc-try1-server.log`). Cause of the refusal:
the separators [I, strong: 0452's own H run with backslash paths logs no refusal and loads in 0.48 s, and the retry with
backslash paths loaded in 0.55 s]; why a refused create becomes an unbounded allocation is not traced. Other agents may
have seen commit pressure in that span. Reported to lead-0437 and the MC (05:41); the MC opened **ticket 0488** (a
refused create fails loud, the run-folder check on normalised paths, a job-object memory cap for headless server.exe).
Until 0488 lands, every headless server.exe run of 0437 runs with Windows paths and under a memory cap: the retry ran
under `perf/evidence/0437/4664/memcap.py` (12 GB, ends server.exe above it), and the runner itself now caps every
headless host (below). A testbed job's paths are Windows paths, so the laptop's M series did not meet this [I].

**The runner's memory cap** (lead-0437's addition, the MC's rule until 0488): `perf/turnstudy/runner/tsjobcap.py` (*renamed from `jobcap.py` on 2026-11-17 by lead-0437: `tools/harness/jobcap.py` of 0488 took the module name and three suite tests failed when the whole folder ran; the H series job of 07:41 shipped the old name*) puts
every process of a headless run (server.exe; c5h's wrapper and civ5headless.exe, which inherit the job) into a Windows
job object with a per-process commit limit (`JOB_OBJECT_LIMIT_PROCESS_MEMORY`), default **8192 MB** (scenario key
`headless.memory_cap_mb` overrides; H's S3 peaked at 370 MB above, M's S1i at 220 MB and S3 at 262 MB, so a normal run
never comes near it). Standard library only (ctypes; Python 3.8 grammar). An allocation past the cap fails inside the
host; the runner reads the job's limit messages (`JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT` on a completion port) and peak
per-process commit, records `memory_cap` in `run.json` (`mb`, `method`, `assigned` pids, `peak_process_mb`,
`limit_messages`, `hit`), and a run that hit it ends as exit 5 with the error "memory cap: ...", which `series_job`
excludes as "crash: memory cap ..." (R8) [C, offline tests 2026-11-17: a child refused at a 200 MB cap is a hit, one
under it is not; a headless run ended by the cap is an exclusion and its `run.json` names the cap]. A single refused
large allocation does not raise the peak (the peak counts only what was committed: seen in the tests), which is why the
messages decide. Live with server.exe `[L, 2026-11-17 05:48:05-05:50:01 EST, workstation, the same functional check again]`:
the server in the job (`assigned` its pid, no errors), peak per-process commit 371 MB, no limit message, `hit` false,
exit 0, turns 301-302, and the run's `stats.db` again IDENTICAL to 0452's H run over 301-303
(`perf/evidence/0437/4664/fc-run-capped.json`). c5h under the cap was not run live (offline only).

#### The partial H set (D178: the series stopped after round 2) `[L, laptop, job 20261117-074118-7b02, runs 2026-11-17 07:42-11:33 EST; analysed 12:45 EST by lead-0437 (sixth lead); n = 2 per arm: grade B, no claim beyond n]`

The owner's order D178 (2026-11-17 11:20, MC #4194): the H baseline stops, the testbeds go to the fixes; the set can be
completed later in quiet time. The job was cancelled at 11:33:28 in the runner's 60 s idle gap right after
`r02-p4-S4` ended (exit 0; no client running at the cut; the wrapper's cancel never ends game EXEs). Kept: rounds 1-2,
S1i, S2i, S3, S4 twice each (8 runs, every run exit 0, 25 of 25 window turns 301-325); S1t and S2t never ran (their
rounds 4, 8, 12). Fetched to `F:/c5-64bit-work/0437/H-timed-partial-7b02` (`--include "runs/*/stats.db"`), records
rewritten (`runrecord.py --rewrite`, 8 ok), `analyze.py` with the plan's pairs; outputs
`perf/evidence/0437/d178/analysis-H-partial-7b02.{json,txt}`. Laptop, Intel HD Graphics 630 27.20.100.9320 and NVIDIA
Quadro P3000 32.0.15.7322 (D175); the series' builds as "The H timed series" above (S3 on 0452's server).

| Arm | s per turn, mean (min-max) | CPU s per turn | peak private MB | growth MB per turn |
|---|---|---|---|---|
| S1i (stock DLL, c5h, 32-bit) | 22.03 (21.83-22.22) | 21.89 | 331 | 1.67 |
| S3 (x64 DLL, server, headless) | 18.71 (18.70-18.72) | 18.57 | 399 | 1.49 |
| S2i (stock DLL in the stock EXE) | 95.39 (95.26-95.53) | 134.97 | 2,435 | 8.34 |
| S4 (increment 1 RC: shell + server) | 89.32 (89.10-89.53) | 97.36 (server 34.69) | 2,144 (sum with the server 2,483) | 6.24 |

- **S3 - S1i -15.1 %** per turn (-3.32 s, 95 % CI [-5.72, -0.92], n 2+2); **S4 - S2i -6.4 %** (-6.08 s, CI [-7.38,
  -4.78]). Identity: S1i = S3 and S2i = S4 IDENTICAL over 300-327 (389,674 BuildingYields rows); headless and clients
  differ (D164, as on M).
- **The EXE's share is as large on H as on M:** a client turn is 4.3x (S2i/S1i) and 4.8x (S4/S3) the headless turn of
  the same DLL (M: about 10x on a 1.4 s headless turn). On H the clients spend about 70 s of each turn outside the
  DLL's own work.
- With n = 2 the intervals are wide (S1i +-11 %); every number here is grade B; the full set (12 rounds, plus S1t and
  S2t) is for later quiet time (D178).
- **Foreground**: one of the two runs of each client arm recorded `foreground_is_game: false` at the window check
  (`r01-p2-S2i`, `r02-p4-S4`), the other true; their turn times differ by under 0.5 % (S2i 95.53 against 95.26, S4
  89.10 against 89.53), unlike the 26-33 % 4653 measured on M with a background window. Not explained [I: the check is a
  snapshot at the window's start; the window may have come to the front later].

### The M budget series on oldpc (W5) `[ticket 4666, impl-4666, 2026-11-17 08:00-]`

Plan 9.7 (B1-B9; bb = RC + 4652, ticket 4664). Written by the implementer of 4666 (account A, worker of lead-0437).
Labelled comparison: **"treatment: turn budget switch, ruling of 7015"** (the runs carry `series_id` and the arm names,
no `identity.treatment`: `perf/ledger/record.py` accepts only `ai_logging` and `stats_logging`). **No budget number in
this subsection is a result before 7016's registry review is accepted** (9.7); the workstation numbers below are
functional indications only (D106).

#### The runner's budget arms (what 4666 added) `[C, offline tests 2026-11-17]`

- Variants `budget_on` (`turn_budget: true`) and `budget_off` (`turn_budget: false`) in `scenarios/M.json` and
  `H.json`; the key is new in `tslib.VARIANT_KEYS`. `true` is refused (exit 2, at the arm list and in the settings) on
  any arm without `"budget_timers": true` in `arms.json`; only `S4b` has it (the RC and S4k have no timers, so an "on"
  run there would measure nothing).
- `clientarm.run_env`: each client run gets its own copy of the job's environment; `VP64_TURN_BUDGET=1` is put in only
  for a `turn_budget: true` run and taken out for every other run, so no run inherits the switch from the job. The
  record says `turn_budget` true/false.
- After the safe stop, a budget-on run copies `budget.tsv`, `budget-proxy.tsv` and `turn-times.tsv` from the session's
  run folder (pack=1: `<user>/Logs/VP64/run/<newest>`) into `<run>/budget/` and runs `budget.py <run>/budget --window
  T+1 T+N` there (`budget/budget-table.csv`, `.json`); `run.json` `budget` holds the exit code. It never changes the
  run's exit code: the analysis reads the files again after the fetch.
- `runrecord.write` (and `--rewrite`) adds `series-budget.csv` (every turn of the table, the labelled ones with their
  label; columns = `budget.SERIES_IDS`, all registered series ids) and the six `budget.<row>_mean_s@1` scalars of the
  window, and attaches `budget-table.json`.
- `budget.py`'s `series_values` no longer uses `dict | dict` (Python 3.9): the testbed runs Python 3.8.
- Tests: `perf/tests/turnstudy/test_budgetseries.py` (11, on 4652's recorded b2 files: budget.py exit 0 over 151-154
  with 4652's means wall 11.738 s and DLL idle 8.031 s, exit 6 for 151-175, a missing file named).
- Workstation driver: `perf/turnstudy/budget/wscheck.py INSTANCE ARM[:VARIANT] --out DIR --turns 5` (4657's no-focus
  launch pieces; the clocks and a grade-D record as a testbed job writes them).

#### Step 1: bb in a client on the workstation `[L, 2026-11-17 08:18-08:3x EST, workstation, shared (5-7 heavy jobs of other owners registered), c5h-c (claimed under 0437), window cloaked (the harness's no-focus launch), M from TS0438-M-T150 by hash, window 151-155 (7 autoplay turns), statistics logging on (client database 4e4620b2), one run per arm: functional, an indication only (D106), no timing claim]`

Driver `perf/turnstudy/budget/wscheck.py c5h-c ARM --out C:/c5-64bit-work/4666/ws --turns 5`; evidence
`perf/evidence/0437/4666/ws/` (each run's `run.json`, clocks, `budget/` files and table, `series-budget.csv`, the
driver's log; statscmp outputs). Every run exit 0, restored, `provision.py verify` rc 0, no dialog.

- **bb runs in a client, switch on** (`S4b:budget_on`, 08:18-08:24): the product path (pack=1; server.exe a child
  of the EXE; the server's log names the build `repo a4bc303f6... (archive)`, `dll_pin 04b1211b8`). The proxy logs
  `budget: on (VP64_TURN_BUDGET=1, ticket 4652): section Local\VPBudget_<pid> created, ... budget-proxy.tsv opened`;
  the server `budget: on ... budget.tsv, turn-times.tsv in the run folder; qpc 10000000 Hz`, `turn block ... opened
  (the proxy's budget is on)` and at the end `9 turn lines written (the load and the last, partial turn included);
  boundaries published to the proxy: 8`. The runner copied the three files; **`budget.py <run>/budget --window 151
  155`: exit 0** (the rows sum to the wall in every turn, else exit 3; server and proxy turns match, else 4;
  `turn-times.tsv` agrees with `budget.tsv` to 1.5 ms, else 5; the window complete and unflagged, else 6): 9 turns,
  -1 `load`, 150 `first`, 151-156 full, 157 `end`. The record carries `series-budget.csv` (9 rows; 23 metric
  columns, all registered series ids) and the six `budget.*_mean_s@1` scalars. (The values are not quoted here: functional only, and 9.7 holds
  every budget number until 7016's review.)
- **bb, switch off** (`S4b:budget_off`, 08:25-08:30): the same build (log `repo a4bc303f6`), no `budget:` line in
  either log, no `turn-times.tsv` (the clock note: "not written (VP64_TURN_BUDGET off)"): the switch is off when the
  variable is absent, as 4652 built it.
- **Same game, off against on:** `statscmp.py off/stats.db on/stats.db --from-turn 150 --expect-turns 150 155`:
  **15 tables, 22,351 rows compared: IDENTICAL** (`statscmp-off-vs-on.txt`).
- **Same game, off against the RC (S4)** (`S4`, 08:30-08:36, the RC's layout, server `5B254430`, proxy `75C40CEC`):
  `statscmp.py off/stats.db S4/stats.db --from-turn 150 --expect-turns 150 155`: **15 tables, 22,351 rows: IDENTICAL**
  (`statscmp-off-vs-rc.txt`); on against the RC likewise IDENTICAL (`statscmp-on-vs-rc.txt`). So over 151-155 bb
  plays the RC's game with the switch off and on; the series checks it over 151-175 per run.
- Turn clocks (doturn, 151-155; one run each on a shared machine, cloaked: not a timing result, D106): on 11.60, off
  11.79, RC 13.65 s per turn.

#### Step 2: the frame cap in oldpc's instance (9.7 B4) `[L, read-only, 2026-11-17 08:35 EST]`

- Read from the run records of the last client job on oldpc in the runner's instance (`tb1`, oldpc's own EXE; job
  `20261117-050102-fa54`, 4662's S4k pair, runs 07:06-07:26 EST, fetched to `F:/c5-64bit-work/0437/4662-oldpc`):
  `GraphicsSettingsDX11.ini` after the runner's window keys (`prep.graphics_now`, all four runs): **`WaitForVSync = 1`**,
  `FullScreen = 0`, `WindowResX/Y = 1920 x 1080`, `FSResID = 132`; the runner writes only the window keys (`FullScreen`,
  `WindowResX`, `WindowResY`) and leaves `WaitForVSync` as found (the scenario's `frame_cap.WaitForVSync: "as found"`).
  The launch is a timing launch (`--timing`: the harness adds no cap) with the window on screen (`--mode offscreen
  --park none`). `config.ini ThrottleOnLossOfFocus` is 0 in the instance copies read so far
  (`research/06-tooling-and-workflows/run-and-drive-the-game.md`; `tb1` on oldpc not read by 4666 `[I]`).
- So the game caps itself at the display's refresh: on oldpc a window on screen with `WaitForVSync = 1` is
  **vsync-bound at 60.0 fps** `[L, 2026-11-15, 0095's A/A job 20261115-130206-8961, every 15 s block 60.0]`
  (`research/08-performance/idle-frame-rate.md` 1.2). Nothing is changed for the series (B4: the frame-cap state is the
  same in all three arms); every run of the series records `prep.graphics_now` again, so the state is checked per run.
- **Not like the laptop's numbers and not like the workstation's:** the workstation's harness windows are cloaked
  and never vsync-synced (113-338 fps); 4652's numbers (cap lifted, workstation) do not carry over (B4). The laptop's
  instance state is not read here; the set is never compared run against run with the laptop's Q1 numbers (D106).
- The machine: oldpc, Intel Core i7-6700K, 15.9 GB, NVIDIA GeForce RTX 2080 driver 32.0.16.1088, Windows 10
  10.0.19045.6466 (the run record's machine facts of the same job); D175: Windows Update installs no drivers there.

#### Step 3: the series (submitted 2026-11-17 08:38:39 EST; ran 08:39:12-11:05:57, rc 0; results below)

`tb.py --host oldpc status` at 08:37: no job running or queued (0485's quiet job done). From the worktree at commit
`184c79209` (branch `task-4666-budget-series-m`; current natives, 0486 merged):

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host oldpc --scenario M \
        --arms S4b:budget_on,S4b:budget_off,S4 --runs 6 --order williams --quiet --expect-min 180 --grade A \
        --series-id 0437-M-budget-oldpc-1 \
        --label "4666 W5 budget series: S4b on/off + S4 RC (treatment: turn budget switch, ruling of 7015)" \
        --issue 4666 --timeout 14400

**Job `20261117-083839-4101`** (quiet, headed, slots 2; 102 files, 4.8 MB shipped: bb's layout, proxy and helper, the
RC's layout, the tag's DLL, the save, the natives). 18 runs: 6 rounds in Williams order over the three arms (round 1
on, off, S4; 2 off, S4, on; 3 S4, on, off; 4 S4, off, on; 5 on, S4, off; 6 off, on, S4: each arm first twice), 27
autoplay turns each (window 151-175), the 60 s idle gap, R8's exclusions with one re-run round at the end. **Planned
length 180 min (end about 11:38 EST)**: from oldpc's last client job (4662's S4k pair, 2026-11-17 07:06-07:26: a run of
12 autoplay turns at 9.8 s per turn took 4 min 10 s, so about 130 s of load, stop and restore per run), a run of 27
turns is about 6.6 min plus the 60 s gap, 18 runs about 137 min; plus 30 % for the budget-on runs, the ship and a
re-run. 0435 (quiet, oldpc) queues behind it (the MC, 07:57).

First run `[L, oldpc, read by one read-only `tb.py logs JOB --tail 400` at 08:51:13 EST: a disturbance of the quiet job
to note against run r01-p2 in the analysis]`: `r01-p1-S4b-budget_on` exit 0, 25 of 25 window turns (doturn and
autosave clocks), wall 437.4 s, load 94.2 s, not excluded, and **`turns-server.tsv` 25 rows**: the switch reached
server.exe on oldpc (it writes `turn-times.tsv` only with `VP64_TURN_BUDGET=1`). Its `budget.rc` is read after the
fetch.

#### Step 4: the analysis, ready (tested 2026-11-17 08:40-08:45 on step 1's runs, where they apply)

Labelled **"treatment: turn budget switch, ruling of 7015"**; no budget number is quoted as a result before 7016's
review is accepted. Machine oldpc only: the set is never compared run against run with the laptop's Q1 numbers.

    # 0. never fetch inside another team's quiet job (and not inside this one: its own quiet job ends first)
    python -I tools/testbed/tb.py --host oldpc status
    # 1. fetch: the budget files (each < 20 KB) come by default; stats.db (about 6 MB) by --include
    python -I -X utf8 tools/testbed/tb.py --host oldpc fetch 20261117-083839-4101 \
        --out F:/c5-64bit-work/0437/M-budget-oldpc-1 --include "runs/*/stats.db"
    # 2. the records to the current ids, no rerun (series-budget.csv and the budget scalars come with it)
    python -I perf/turnstudy/runner/runrecord.py --rewrite F:/c5-64bit-work/0437/M-budget-oldpc-1
    # 3. per arm, the cost pairs by Welch (95 % interval, detectable difference, R10) and identity per run
    python -I perf/turnstudy/runner/analyze.py F:/c5-64bit-work/0437/M-budget-oldpc-1 \
        --pairs "S4/S4b:budget_off,S4b:budget_off/S4b:budget_on" \
        --out F:/c5-64bit-work/0437/M-budget-oldpc-1/analysis --records-to F:/c5-64bit-work/0437
    # 4. the budget per on-run again with the analysis commit's reader, then across the on-runs
    for r in F:/c5-64bit-work/0437/M-budget-oldpc-1/out/runs/*-S4b-budget_on; do
        python -I perf/turnstudy/budget/budget.py "$r/budget" --run-record "$r/run.json" --out "$r/budget" --quiet; done
    python -I perf/turnstudy/budget/budget.py --across F:/c5-64bit-work/0437/M-budget-oldpc-1/out/runs/*-S4b-budget_on/budget/budget-table.json \
        --out F:/c5-64bit-work/0437/M-budget-oldpc-1/budget-across.json
    # 5. identity per run, explicitly (analyze.py does it too: each run against its arm's first, and the arms' firsts)
    REF=$(ls -d F:/c5-64bit-work/0437/M-budget-oldpc-1/out/runs/*-S4 | head -1)
    for r in F:/c5-64bit-work/0437/M-budget-oldpc-1/out/runs/r*; do
        python -I tools/d10/statscmp.py "$REF/stats.db" "$r/stats.db" --from-turn 150 --expect-turns 150 175 | tail -1; done

- **The cost pairs (R10):** `analyze.py`'s pair `S4/S4b:budget_off` gives off minus the RC (`s_per_turn` = player 0's
  PlayerDoTurn clock, `turn.doturn_mean_s`, the same in all three arms), the pair `S4b:budget_off/S4b:budget_on` on
  minus off, each with Welch's 95 % interval, v and the detectable difference; plan 9.7: a budget share is set beside
  Q1's S4 seconds only with the off - RC difference beside it, and every row with the measured on - off cost beside it.
- **What 4666 changed in `analyze.py` for this:** `--pairs` takes `A/B` whenever an arm has a variant (the old rule
  split `S4/S4b:budget_off` at its one colon); every requested pair outside the grade A pairs compares its arms' first
  `stats.db` (with the within-arm comparisons: every run against one reference) and its label says "same game by
  stats.db" or "different games"; `statscmp` runs with the series' window (`--from-turn 150 --expect-turns 150 175` on
  M). Checked on the Q1 M series (`F:/c5-64bit-work/0437/M-timed-1-e8da`, read only): every within-arm comparison
  IDENTICAL with the window flags, S1i-S2i and S3-S4 differ as D164 says.
- **Tested on step 1's runs** (a series-shaped copy, `C:/c5-64bit-work/4666/anatest`): `runrecord.py --rewrite` exit
  0 (3 rewritten; the on-run's record keeps `series-budget.csv`, `budget-table.json` and the 6 budget scalars, 12
  metrics); `analyze.py` with the two pairs exit 0, identity `S4-S4b:budget_off` and `S4b:budget_off-S4b:budget_on`
  IDENTICAL, the Welch lines "needs at least 2 runs per arm" (1 run each there); `budget.py --run-record` exit 0;
  `budget.py --across` of one table exit 7 (refused, at least 2 runs), of two tables exit 0. The fetch was not tried
  (the series' own quiet job holds oldpc).

#### Results (ticket 4670) `[L, oldpc, 2026-11-17 08:39-11:06 EST, job 20261117-083839-4101, series 0437-M-budget-oldpc-1, 6 Williams rounds of S4b:budget_on, S4b:budget_off, S4 (18 runs, all counted), quiet, alone; NVIDIA GeForce RTX 2080 driver 32.0.16.1088, Windows 10 19045, i7-6700K rated 4001 MHz (effective 4.18 GHz in every arm), High performance plan; M from TS0438-M-T150, window 151-175, statistics logging on; builds: bb = DLL 29F58E17, server.exe 14D22095, proxy 09D09D7C, helper EBFB6AF0; RC = DLL 29F58E17, server.exe 5B254430, proxy 75C40CEC, helper B8AC9A64 (both with the RC's libraries, vpproxy.ini 70F2C066 and the tag's fallback DLL 262AEE88); analysed 2026-11-17 14:24-14:26 EST by impl-4670; grade A as marked]`

Fetched after 4756's quiet job ended (the MC's slot, #4293) with `perf/evidence/0437/4670/analyse-budget-set.sh`, which
runs step 4's commands (the analysis written to `F:/c5-64bit-work/0437/M-budget-oldpc-1/analysis.json`, not step 4's
`analysis` file name; ends non-zero on a failed step or a statscmp comparison that is not identical): fetch 430 files,
`runrecord.py --rewrite` 18 rewritten, `analyze.py` rc 0, `budget.py --run-record` rc 0 for all 6 on-runs, `--across`
rc 0, statscmp 18 of 18 IDENTICAL. Committed outputs: `perf/evidence/0437/4670/set/` (`analysis.json`, `analysis.txt`,
`budget-across.json` and `.txt`, each on-run's `budget-table.csv`/`.json`, `statscmp-per-run.txt`, `summary.txt`,
`driver.log`); the fetched set (runs, `stats.db`, logs) stays on `F:/c5-64bit-work/0437/M-budget-oldpc-1`.

- **Seconds per turn** (player 0's PlayerDoTurn clock, `turn.doturn_mean_s`, 6 runs each): RC (S4) **10.676 s**
  [10.624, 10.728] (cv 0.47 %), bb off **10.695 s** [10.643, 10.748] (cv 0.47 %), bb on **10.809 s** [10.738, 10.881]
  (cv 0.63 %).
- **The instrument's cost (on - off), grade A** (treatment pair: one build, one environment switch, every run the same
  game; the team's ruling of 9.7): **+0.114 s per turn (+1.07 %), 95 % CI [+0.036, +0.192]**, Welch v 9.2, detectable
  0.109 s: resolved. Process CPU +0.191 s per turn [+0.050, +0.332] (+1.2 %); the server's CPU 3.589 -> 3.761 s per
  turn; peak private and growth: no difference resolved. **As a share of the budget's wall: 1.05 % (95 % upper end
  1.78 %)**: each share below is uncertain by about that much (B3, R16) `[I]`. It lies at the low end of 9.7 B3's
  estimate (0.11-0.35 s per turn from micro-benchmarks times call counts) `[L against I]`.
- **The budget build against the RC with the timers off (off - RC), grade A, no difference resolved**: **+0.019 s per
  turn (+0.18 %), 95 % CI [-0.045, +0.083]**, v 10.0, detectable 0.089 s (0.84 %); CPU, peak private and growth also
  unresolved. So bb with the switch off plays and paces like the RC to within 0.09 s per turn on this machine
  (B3-a: "budget-off = RC" measured, not inferred).
- **Same game**: every run against the RC's first run (`r01-p3-S4`), `statscmp --from-turn 150 --expect-turns 150 175`:
  **18 of 18 IDENTICAL, 15 tables, 99,202 rows each**; `analyze.py`: within each arm 5 of 5 identical, S4 against bb off
  and bb off against bb on identical.
- **The turn budget of S4 (bb, switch on), grade A** (6 on-runs, declared grade A, window 151-175 complete in every run;
  shares = sum(row) / sum(wall) per run, mean and Student-t 95 % interval across the 6 runs): wall
  (`turn.server_remote_s@1`, server.exe's own turn line) **10.813 s** [10.741, 10.886].

  | Row | Share | 95 % CI | s per turn | 95 % CI |
  |---|---|---|---|---|
  | `game_logic` (the DLL's own work on server.exe's thread) | **14.4 %** | 14.1-14.7 % | 1.557 | 1.514-1.599 |
  | `into_exe` (the DLL waits for its calls into the EXE) | 4.9 % | 4.8-4.9 % | 0.529 | 0.521-0.537 |
  | `from_exe` (the DLL serves the EXE's calls: UI Lua, plots, locks) | 6.3 % | 6.2-6.4 % | 0.681 | 0.672-0.690 |
  | `save` | 3.5 % | 3.4-3.5 % | 0.373 | 0.371-0.375 |
  | `transport` (the crossings) | 3.4 % | 3.3-3.4 % | 0.362 | 0.359-0.366 |
  | `dll_idle` (no service thread of the server busy: the EXE alone, its waits included) | **67.6 %** | 67.4-67.9 % | 7.311 | 7.286-7.337 |

  Beside every row: the instrument's cost +0.114 s per turn [+0.036, +0.192] (1.05 % of the wall). Beside `dll_idle`
  (B4): the EXE process's CPU in the same runs is 16.16 s per turn (all threads, so above the wall) and the server's
  3.76 s: the row holds the EXE's frames between `Update` calls and its waits (on oldpc the window is vsync-bound at
  60 fps), not 7.3 s of EXE work. The budget's wall (10.813 s) and the on-arm's doturn clock (10.809 s) are two
  events on two clocks (B8); they agree to 4 ms over the window.
- **Disturbances.** Run r01-p2 (`S4b:budget_off`) had one read-only `tb.py logs` over SSH at 08:51:13 while it ran
  (4666): it took 10.620 s per turn, the lowest of its arm (10.620-10.760), counted. The job's quiet proof says
  `alone: true`, `undisturbed: false`: foreign CPU 0.582 cores on average over the job's 8,808 s (5,130 s of CPU by
  processes outside the job's tree; which processes is not read); each run's own measure is 0.018-0.021 foreign cores
  (arm means; R8 excludes a run above 0.25) and no run was excluded or rerun. The first run of the set
  (`r01-p1-S4b-budget_on`, 10.930 s) is its arm's slowest (others 10.747-10.847).

**Q2, the product side (for the lead; S4 on oldpc, grade A):** of a 10.8 s AI turn of M in our client, the 64-bit DLL's
own work is 14.4 % (1.56 s); calls between the DLL and the EXE (both directions) are 11.2 % (1.21 s) and the crossings
themselves 3.4 % (0.36 s); the save 3.5 % (0.37 s); and for **67.6 % (7.3 s) of the turn no thread of the server is
busy**: the EXE alone, pacing and showing the AI players' moves (D165: the tick wait and the queue-B drain). Measuring
costs 1.1 % of the turn and the budget build is the RC's equal with the timers off (within 0.09 s). So the server side
(game logic, calls, crossings, save: 32.4 %, 3.5 s) is not what an M turn waits for; options 3 and 2 aim at the EXE's
67.6 %. S2's stock side (4654) is a grade C indication on another machine and method (EXE alone about 86 %), set
beside, not compared (B8); W5's S2 side is ticket 4671. Not compared with the laptop's Q1 seconds (D106).

**Caveat on the EXE's Lua share (added 2026-11-17 23:40 EST by the ninth lead, from MC #4857, ticket 0518 merged at
c30192820, `research/03-dll-engine-boundary/ui-lua-lock-holds.md`) `[L, S4 M 151-160]`:** the AI thread's wait on the
EXE's Lua lock (about 0.59 s a turn with D165's options, 0.65 s stock) is almost all one Community Patch UI listener
(TechPanel.lua on `Events.AIProcessingStartedForPlayer`, rebuilding tooltips per major AI) that runs in observer mode
(autoplay) only; `Game.SetObserverUIOverridePlayer` cut that wait to 0.016 s and `into_exe.script` from 0.70 to 0.15 s
(the wall did not fall in that one loaded pair). Every series of this study is autoplay, so the `into_exe` and
`from_exe` Lua shares above (and any gain stated for P15) may not be a player's. Ticket 0522 (account B) checks a human
active player and the override with 3 trials; until it reports, read the Lua shares with this caveat. The running
setups are not changed (the MC's order). 0522 part (1) (MC #4885, 2026-11-17 23:55) `[L, c5h-a, S4 M 151-160]`: with a
human active player the listener runs 25 times a turn and returns at once (0.0007 s a turn) and the AI thread's Lua
wait is 0.020 s a turn against 0.590 s in observer autoplay: the autoplay EXE-Lua wait is an observer artefact, not a
player's cost. **Current statement (0522 merged, MC #4974, 2026-11-18 01:28; replaces the "may not be a player's"
caveat above; `ui-lua-lock-holds.md` section 5) `[L, grade B]`:** on oldpc, autoplay with the observer's interface held
on player 0 (`SetObserverUIOverridePlayer`) is +1.17 s a turn (+12 %) slower than observer autoplay (pin, stock
pacing) and changes the save bytes, so observer autoplay understates a player's AI phase. The study keeps its arms and
series as they are (0522's recommendation 5.3, adopted by the MC). Wherever this study states the EXE's Lua share or
the AI thread's wait "for a player": the autoplay arm (A) contains 0.55-0.6 s a turn of observer-only tooltip rebuild
and lacks the player's view; a player-like number is given only as its own labelled O arm (identity within O, never
A against O).

Report v2's generator (`perf/turnstudy/report/report.py`, ticket 4670) reads this set through `--budget-series` (the
analysis) and `--budget` (`budget-across.json`): every row with the on - off cost beside it and that cost over the
wall, off - RC beside the set's own S4 seconds, the EXE's CPU beside `dll_idle`; a budget without its cost pair is
refused (exit 3), a grade below A is shown as such. Command: `perf/evidence/0437/4670/make-report-v2.sh`; the page
`perf/evidence/0437/4670/report-v2.html` (written 2026-11-17 14:29 EST, SHA-256 29e8a555...6fb6d as written, from the M series, R12, 4662's option 3 series,
this set, 9.8's ranking `perf/evidence/0437/4670/q3-ranking.json` and `context-v2.json`; H, option 2's laptop series and
4669's counted bounds are pending lines the same command fills). Every chart on it is DIAGNOSTIC.

**Report v2.1** (ticket 4679, 2026-11-17 15:26 EST): `perf/evidence/0437/4679/report-v2.1.html`, SHA-256
02afc579c0013ae1f7d4fc1121c85620fe1b96cb324aacfb03f99ce82c4c4dd6 as written, by `perf/evidence/0437/4679/make-report-v2.1.sh`
(report.py's new `--fix KEY=PATH` input, rows oriented off -> on, and `context-v2.1.json`): v2 plus the partial H set
(labelled PARTIAL, grade B), 4665's and 4675's D178 series, Quick Movement off, U's excess, the two prototypes' laptop
pairs and a decisions block; it reads the lead's 15:00 analyses, which this file does not yet record: 4673's pair
(`F:/c5-64bit-work/0437/4673-H-proto-c8ac/analysis`: S3 17.79 -> S3p 17.01 s per H turn, -0.79 s [-1.46, -0.11], -4.4 %,
grade B, IDENTICAL) and 4674's stats.db identity (S3r-S3 IDENTICAL); only 4669's counted bounds are still pending.

#### Not verified (4666)

- ~~That the runner's budget arms work on oldpc (Python 3.8)~~ Answered by 4670 (2026-11-17): every on-run's
  `run.json` has `budget.rc` 0 and its window 151-175 complete ("Results (ticket 4670)" above).
- The frame cap of `tb1` was read from the 07:06-07:26 job's records, not inside this series; each run records it again.
- `tb1`'s `config.ini ThrottleOnLossOfFocus` (not read; the window is on screen and the job's foreground share is
  recorded per run).
- ~~Whether the budget-on runs are slower (the cost) and whether bb-off equals the RC in time~~ Answered by 4670
  (2026-11-17, grade A): on - off +0.114 s per turn [+0.036, +0.192]; off - RC +0.019 s [-0.045, +0.083], no difference
  resolved (detectable 0.089 s).
- `perf/tests/turnstudy` shows 3 failures in `test_turnstudy.py`'s jobcap tests when the whole folder runs (each
  passes alone): another test module puts `tools/harness` first on `sys.path`, so `import jobcap` finds 0488's
  `tools/harness/jobcap.py`; present before 4666's changes (checked with `--ignore` of 4666's test file), not fixed here.
  *(Corrected 2026-11-17 by impl-4671: gone since 4664 renamed the runner's copy to `tsjobcap.py` (225 passed at
  4671's start); 4671 then moved `headlessarm.py` onto `tools/harness/jobcap.py` itself and removed `tsjobcap.py`,
  section "The stock side in W5" below.)*

## Why the headless hosts and the clients diverge (ticket 4655; 2026-11-16)

Written by impl-4655 (20:10-21:05 EST), placed here by lead-0437. Evidence `perf/evidence/4655/`; kept data `F:/c5-64bit-work/4655/`.

(4650's findings were moved by lead-0437 on 2026-11-16 20:05 into `research/08-performance/turn-time-study-analysis.md`
section 7.1.) 4655's findings, impl-4655, 2026-11-16 20:10-21:10 EST, workstation (c5h-a, i0ship-26234371-ia
`3c2f47da`; S3 the pin `x64-04b1211b-pfdee656`; S1 c5h with `i0ship-26234371` `f92881a3`; identity database of 0438),
for the lead to write into `research/08-performance/turn-time-study.md`; evidence `perf/evidence/4655/results.txt`:

- **Cause [L, M and H]: the headless hosts' load path never computes the plots' water flags.** `CvPlot::m_bIsAdjacentToLand`,
  `m_bIsLake`, `m_bIsFreshwater` are not saved; `CvPlot::updateWaterFlags` computes them, and readers with the default
  `bUseCachedValue = true` read the cached value. After a load the EXE calls `ICvMap1::RecalculateLandmasses()` once
  (0438's S4 M run, c5h-b `Logs/VP64/run/20261116-165806-31924-1/proxy-calls.tsv`: 1 call), whose first step is
  `updateWaterFlags` on every plot (`CvMap::recalculateLandmasses`, CvMap.cpp at 26234371e) [C]. The server's load
  (`srv_main.cpp` PhaseLoad and the game-started steps; `server-host.md` section 3 "Load") and c5h's `loadgame` do not
  call it [C: neither source names it]. Cached counts at the arm: S3 M adjacent-to-land 0, lake 0, fresh water 0 of
  4,187 plots; the client (S2i M, armed while paused, before any update) 712 / 15 / 392; S3 H 0 / 0 / 0 of 12,320,
  1,907 / 62 / 796 once recomputed.
- **Where it shows first [L].** Both sides play player 0 (the seat the arm makes AI) first; its first 10 unit steps are
  equal, the 11th differs (Liburna 2973: S3 goes on to 54,44, the client stops at 53,45). With AI logs on (both sides,
  neither changes its own game): Songhai's tactical log differs at line 6, "tactsim around (55:30) ... started with 0
  units" headless against "1 units" in the client (a Liburna in its city), and Songhai's approach logs differ at their
  first line. That coast-bound ships fail the tactical sim's `canEndTurnAtPlot` through the stale adjacent-to-land flag
  is [I]; that fresh water feeds city yields (Byzantium's growth and Education at turn 150 differ) is [I].
- **The set-up change makes them one game [L].** `perf/evidence/4655/probe-waterfix.lua` (a `--lua-probe` at the arm:
  `Plot:IsAdjacentToLand(false)` on every plot, i.e. `updateWaterFlags` everywhere; the landmass ids are not
  recomputed) makes the headless hosts play the client's game: S3 M one turn, event for event (460 player-turn and unit
  -move events) and every stats.db table at turns 150 and 151; S1 (c5h, i0-ship) M the same; S3 H every table at 300 and
  301 against 0438's client; S3 M 25 turns against 4650's S2i-M-1 at every turn 150..175. The only rows left are the
  ones the client logs at the load (MapPlotsState, MapUnitsState, WorldStateLog, civ_turn_era at turn T): the EXE calls
  `ICvGame1::LogGameState` once after the load (same proxy-calls.tsv) [L]; their SQLite part reads state only [C].
- **So it is the harness's set-up, not a difference between headless and windowed play.** The proper fix is the EXE's
  call in the hosts' load path (`RecalculateLandmasses()` after `Map::Read`, server direct mode and c5h): shared
  server/host files, not changed here. Until then a headless run from a save can pass the probe (server
  `--lua-probe`, c5h `--lua-probe`) to play the player's game. S4 (the product) is not affected: the EXE makes the call.
- **Ruled out [L]:** the arm's timing and form (the client armed while the load screen pauses, with SetOption quick
  combat and movement, as `server-host.md` step 6: the same game as the plain client); quick combat/movement (on in both
  at the arm); AI logging on (unchanged on either side); c5h's UI add-ins and profiles (unchanged); per-unit state of
  player 0 at the arm (equal except the client's selected unit).
- **Consequence beyond the study [I, not measured]:** every headless run that starts from a save (S1/S3 `--load`,
  c5h `loadgame`) has played without fresh water, lakes and coast adjacency until something recomputed a plot's flags
  (readers with `bUseCachedValue = false` refresh single plots), so such runs are not the game a player gets from that
  save. New games are not affected (map generation runs `recalculateAreas`, which calls `recalculateLandmasses`) [C].
- **The random-number log is not a usable tool here [L]:** c5h `loadgame --rand-log 1` (game logging on, AI logs off)
  wrote no `RandCalls.csv`; in the client, 0438's runs with `LoggingEnabled = 1` and `RandLog = 1` left the instance's
  `RandCalls.csv` untouched (dated 12:51). Not investigated further; the event-order trace and the AI logs were enough.
- **Not verified:** the landmass-id part of `RecalculateLandmasses` (not reproduced by the probe; it made no difference
  on these saves); H past turn 301 with the probe; other saves; the tag DLL; the other EXE load calls the hosts skip
  (`ICvGame1::InitScoreCalculation`, `ICvMap1::Validate`, `ICvGame1::SetFinalInitialized`,
  `ICvPlayer1::SetBusyUnitUpdatesRemaining`), which did not matter for identity on these saves.
- **Statements this corrects:** `turn-time-study.md` "Load checks and the identity trace (S2, S4)" ("Cause not traced
  [I: the client's load path ... against the headless hosts' arm step]") and its "Not verified" line "Why the clients
  and the headless hosts diverge at once"; P4 in `turn-time-study-analysis.md` 7.1 holds as measured but its headless
  side changes once the hosts make the call; `server-host.md` section 3 "Load" should name the missing call.

## The stock side of the turn budget (W4b, ticket 4654; 2026-11-16)

Written by lead-0437 from impl-4654's hand-back (2026-11-16 20:10-21:40 EST). Tools: `perf/turnstudy/budget/stock/`
(`stockthreads.py` names threads by Win32 start address and builds the per-turn thread table; `exeprofile.py` maps
`perf/sampler` stacks to thread roles, modules and knowledge-base subsystems; `profile_job.py`, `submit_profile.py`;
`tests/test_stock.py`, 80 checks). Committed outputs (shares and seconds only) in `perf/evidence/4654/`; raw samples on
`F:/c5-64bit-work/4654/` (never committed, D83). Factor definitions: `perf/turnstudy/factors/factors.json`.

**Set-up.** Laptop, instance `tb1`, the laptop's Steam EXE (TimeDateStamp 0x546CD0A8, the knowledge base's build) [L];
scenario M from `TS0438-M-T150`, cp,vp,eui, Quick Combat and Quick Movement on, 1920 x 1080, WaitForVSync as found, AI
and statistics logging off, autoplay from 150, turns 153-157 sampled, quiet headed jobs `20261116-202815-1442` and
`-204315-9e81` (S2i, CPU mode), `-205218-2a62` (S4, the pin layout, CPU mode), `-210025-9e46` (S2i, wall mode). One run
per job: **grade C, indications**. **Caution:** these jobs ran before 4653 found that the game window was not in the
foreground (section "Series of the study": foreground client turns are 26-33 % shorter), so the absolute seconds are
background-window times; the shares are the finding.

**Which thread plays the DLL's turn [L].** S2i has 40 threads: one game-core thread (start `CivilizationV_DX11.exe+0x42A360`),
the main thread (`+0x527035`), 3 workers started through `MSVCR90.dll+0x2345E`, 18 graphics-driver threads, audio,
thread-pool, Steam and harness threads; the knowledge base's two start addresses hold on the laptop's EXE. Per S2i turn
(means over the unsampled turns 151, 152, 158, 159): wall 18.35 s, game-core thread CPU 2.52 s (10-16 % of a turn),
main 5.86 s, workers 8.12 s, others 1.18 s; **the core thread is idle 15.83 s (86 %)**. Wall mode confirms it: the
core thread is blocked 85 % of the turn, 82 % of it in `ZwSuspendThread`, the wait between `Update` calls
(`research/02-exe-internals/game-core-thread.md`). S4: the EXE's core thread 0.4-1.05 s (4-5 %), main 5.6-7.7 s, workers
5.8-8.9 s; server.exe 3.6-4.7 s CPU per turn.

**Where the CPU goes [L; the subsystem mapping I].** S2i (job 9e81), per turn: the core thread (3.1 s): the DLL 39 %,
**zlib 32 % (0.99 s): the autosave's compression runs on the game-core thread**, the EXE 20 % (92 % of it with the DLL
on the stack), OS 5 %, C runtime 3 %. Main thread (5.9 s): the EXE 56 %, OS 12 %, the game database DLL 9.7 %, graphics
runtime 8.5 %, Lua 6.6 %. Workers (8.4 s): the EXE 74 %, graphics runtime 12 %, OS 11 % (spinning in
`NtYieldExecution`). Wall mode: the main thread is blocked 67 % of the turn, 61 % inside waits called from the graphics
runtime (Present, vertical blank or GPU synchronisation [I]); the workers are blocked 90 %. S4 (job 2a62): core thread
0.93 s, **zlib 67 %: the save's compression stays in the EXE in the product**, the proxy 28 %; main thread 7.2 s, the EXE
47 %, the proxy 17 % (1.2 s, its calls now crossings to the server), game database 9 %, Lua 7.3 %; workers 8.3 s, the
EXE 80 %. Inside the EXE, 95.8 % of EXE leaf samples have no named function in `research/02-exe-internals/addresses.md`
within 0x600 bytes: the knowledge base names start-up, crash sites and frame entry points, not the code hot in an AI
turn. Counting every frame of a stack: the game-core step 0x4D8BF0 23 % of walks, the 4 MB queue pump call 0x4B4067
9 %, `UpdateWorldCamera` 0x6FBA80 7 %, `GameViewState::Update` 0x7253E0 7 %, the city per-frame update 0x537BF0 6.5 %,
the queue-B pump call 0x4B4029 4 %, the farm-field builder 0x748FC0 4 %; worker stacks pass near 0x7BF260 and 0x81D790
in 86 % of their walks (named in the knowledge base as the TerrainDecalSystem geometry chain from load-time samples;
more likely the workers' job dispatch [I]).

**Method corrections.** In CPU mode at the real tick rate a walk often finds the thread already blocked: the first run
charged 62 % of cycles to `NtWaitForSingleObject`; walks that find a thread blocked in a kernel wait are now left out.
**The sampler's slowdown [L]:** +11.9 % (9e81), +15.2 % (1442), +9.6 % (wall mode) for S2i, +7.9 % for S4 against the
unsampled dry round; a tick took 105-125 ms instead of 10 ms, because a walk of a 32-bit thread costs 4-5 ms (81-87 % of
the sampler's time); each busy thread is walked 8-10 times a second.

**The S2 turn budget, first indication (S2i, M, ~22 s sampled turns, background window).** Game logic (the DLL's own
code on the core thread) about 1.3 s (6 %); calls into the EXE about 0.6 s (3 %); save (compression on the core thread;
P2: 1.0 s) about 1.0 s (4.5 %); calls from the EXE 0.15-0.3 s (1 %); **EXE alone (wall minus core CPU) about 19 s (86 %)**,
mostly the main thread waiting in the graphics runtime and the core thread suspended between `Update` calls. The same
direction as 4652's S4 indication (EXE alone 68 %). What paces the `Update` calls (the frame-time accumulator, the
vertical-blank wait, `CanAdvanceTurn` which the DLL asks before `doTurn` [C, `CvGame::update`]) is what the factor
experiments test.

**Factor experiments, defined (not run).** Variants `quick_off`, `fps60`, `minimised`, `no_eui`, `camera_busy`,
`camera_empty` (need a runner key), `autosave_10` against `poll5`, `stats_off`; 3 runs per level on M, all levels of one
client in one interleaved series with the bare arm as base: about 6.3 h (S2i) + 6.5 h (S4) of quiet laptop time [I].
Not defined, with reasons in the file: "UI event handlers stubbed" (no mechanism yet) and "human seat on autoplay"
(`Game.SetAIAutoPlay` always moves the seat to an observer [C]; the end-turn-to-control series covers the human seat).

**Not verified.** One run per arm and mode; no H; stacks through EXE frames have no symbols, so "the DLL on the stack"
is a lower bound; the subsystem mapping is by nearest named row; whether `TurnsBetweenAutosave` stops the per-turn
`AutoSave_Post_N` (the DLL asks for it every turn with no interval test of its own [C]). *(Settled 2026-11-17 by
ticket 4671, next section: it does.)*

## The stock side in W5: the runner's thread budget, the camera key and the factor screening (ticket 4671; 2026-11-17)

Written by impl-4671 (worker of lead-0437, account A), branch `task-4671-stock-side-runner`. Everything below is
functional work on the workstation (shared, the game window cloaked): no timing claim (D106); the series that measure
are ready, not submitted.

### The thread budget switch `thread_budget` `[C, offline tests; L, workstation runs below]`

- **What it records.** Variant key `thread_budget` (`tslib.VARIANT_KEYS`; variants `threads_on` / `threads_off` in
  `scenarios/M.json` and `H.json`; client arms only, refused on headless arms). Off for every existing arm and variant
  (a test walks them all). On: at every turn boundary the sampler writes (`sampler.Sampler(on_boundary=...)`, called
  after the boundary's rows with the thread ids it read), `perf/turnstudy/runner/threadbudget.py` maps the threads of
  the EXE and, in S4, of server.exe: the first boundary takes `stockthreads.snapshot(pid)` (4654's tool: each thread's
  Win32 start address -> module, offset, role; core `CivilizationV_DX11.exe+0x42A360`, main `+0x527035`, workers
  `MSVCR90.dll+0x2345E`), later boundaries map only threads not seen before (a thread's start never changes; modules are
  read again only when a new start lies outside them). server.exe's threads are added to the sampler's thread roles,
  so `threads.tsv` carries both processes (the EXE's threads are read in every client run, as before).
- **Per run, after the window:** `threadmap-exe.json` / `threadmap-server.json` (merged maps), `stock-threads-exe.tsv`
  / `.json` and `-server` (per turn: wall, core CPU, main, workers, other, the whole process's CPU, core idle = wall -
  core CPU, core share; means over T+1..T+N), and `run.json` `thread_budget` (maps, tables, the switch's cost per
  boundary: full and incremental). It never changes the run's exit code. `stockthreads.read_threads_tsv` got a `pid`
  filter (the file holds two processes in S4).
- **Readers.** `analyze.py`: per arm `thr_core_cpu_s`, `thr_main_cpu_s`, `thr_workers_cpu_s`, `thr_other_cpu_s`,
  `thr_core_idle_s`, `thr_process_cpu_s`, `thr_server_cpu_s`, `thr_snapshot_ms` (recomputed from the run's files by
  `threadbudget.observation`), a `threads` line per arm in `analysis.txt`, and Welch pairs of those rows where both
  arms carry them. `runrecord.py` attaches the six files and puts the window means in its summary; **no ledger metric
  ids** for them yet (the per-thread rows need the performance team's registry review, as 4666's budget rows did
  before 7016; `perf/ledger/registry.json` is outside 4671's touches).
- **The switch's cost (bound) `[L, workstation, 2026-11-17]`.** A full snapshot took 190 ms mean, 274 ms max per
  boundary in run 1 (82 threads), almost all of it two system-wide toolhelp walks (about 620 processes on the
  workstation: the thread walk 70-170 ms, the process walk 15-35 ms; the module walk 0.4-0.7 ms, the start queries
  2-12 ms; `C:/c5-64bit-work/4671/snapcost.py`). The sampler's own per-boundary `psutil` thread read, done in every
  client run since 4651, costs the same 70-200 ms. Hence the incremental maps after the first boundary: runs 2 and 3
  below took 143-385 ms for the one full snapshot and **1.7-1.9 ms mean, 6.9 ms max** for each later boundary of the
  EXE (server.exe 0.01 ms: its 6 threads do not change). All of it runs in the runner's process (its CPU is the `runner` row of `per-turn.tsv`), once per
  turn, outside the game's threads; the game sees `OpenThread(THREAD_QUERY_INFORMATION)` and one module walk.
  Bound: one full snapshot (0.15-0.4 s, at the boundary before the window) + 26 incremental (about 2 ms each) over a
  27-turn run: under 0.5 s of the runner's CPU per run, under 0.02 % of a 15 s turn inside the window. The series'
  `threads_on` / `threads_off` pair measures the effect on the turn.

### The camera key and the quick_off read-back `[C, offline tests; L, workstation]`

- Variant key `camera` ("busy", "empty"; variants `camera_busy`, `camera_empty`; refused on headless arms and on the
  add-in arm S2t). `clientarm.set_camera`, before the arm, in the InGame state: a passive counter of
  `Events.CameraViewChanged` (no Lua getter of the camera exists, `research/02-exe-internals/camera-and-picking.md`
  8), then `factors.json` `camera_lua.<level>` read from the file at run time, the answer into `run.json`
  `camera.plot`, exit 2 on -1 or a malformed answer, a 3 s settle, the counter read again; after the window the
  counter once more: `camera.events_after_arm`, `camera.events_in_window` (player 0's PlayerDoTurn of T+1 .. of T+N+1)
  and `camera.counter_sees_lookat`. `series_job.exclusion` excludes a run whose counter saw the LookAt and counts an
  event inside the window ("camera moved during the window").
- quick_off's read-back (before the arm, before T+1) now records `Game.IsOption` for both options and
  `PreGame.GetQuickCombat()` / `GetQuickMovement()` (`run.json` `quick_readback`); a PreGame value that differs refuses
  the run (exit 2); "none" (no PreGame getter in the channel's state) is recorded, not refused.

### Workstation runs `[L, 2026-11-17, workstation, shared (7 heavy jobs of other owners registered), c5h-b (claimed under 0437), window cloaked (the harness's no-focus launch: guard self-test ok, start-up watch ok), M from TS0438-M-T150 by hash, statistics logging on (client database 4e4620b2), one run per arm: functional, an indication only (D106), no timing claim]`

Driver `perf/evidence/0437/4671/wscheck_4671.py c5h-b ARM:VARIANT --out C:/c5-64bit-work/4671/ws --turns N`
(4666's driver with this ticket's label and the precondition read-outs); evidence `perf/evidence/0437/4671/ws/` (each
run's `run.json`, clocks, `per-turn.tsv`, `threads.tsv`, `stock-threads-*`; the driver's logs); the thread maps (start
addresses) and the whole run folders on `F:/c5-64bit-work/0437/4671-ws/` (not committed, D83). Every run exit 0, no
dialog, the client database `4e4620b2` (logging on), the instance restored and `provision.py verify` rc 0 after each
run (the S4 run also `set-proxy --original` rc 0); boot and load 136-160 s.

| run | arm and variant | window (played to) | doturn s/turn | EXE per turn: core / main / workers / other / EXE total / core idle (s) | server.exe CPU |
|---|---|---|---|---|---|
| 1, 10:17:56-10:23:48 | `S2i:check4671` (TurnsBetweenAutosave 10, progress poll:5, camera busy, thread budget; the old full snapshot at every boundary) | 151-158 (160) | 13.65 | 3.24 (23.2 %) / 12.83 / 19.24 / 3.01 / 38.31 / 10.71 of 13.95 | - |
| 2, 10:26:34-10:43:15 | `S2i:check4671_b` (quick combat and movement off, camera empty, thread budget) | 151-153 (155) | 154.45 | 4.10 (2.7 %) / 112.19 / 379.59 / 80.18 / 576.07 / 150.49 of 154.59 | - |
| 3, 10:45:52-10:51:23 | `S4:check4671` (as run 1, in the RC; server.exe's threads mapped too) | 151-153 (155) | 14.36 | 0.62 (4.7 %) / 11.30 / 15.42 / 2.30 / 29.64 / 12.70 of 13.32 | 5.52 (6 threads) |

Workstation values with the window cloaked and the frame rate uncapped (113-338 fps on this machine): the workers'
share is larger than on the laptop (4654: workers 8.12 of 18.35 s), so these are not Q2's numbers; they show the
switch works in both clients. In run 2 "other" is mostly the graphics driver's threads (`nvwgf2um.dll` 72.4 s per
turn). 82-87 threads were mapped in the EXE, every core thread at `+0x42A360` found, the EXE's PE time stamp the
knowledge base's (0x546CD0A8).

### The preconditions of the factor screening (factors README "Preconditions")

- **autosave_10: settable `[L, runs 1 and 3]`.** With `[AutoSave] TurnsBetweenAutosave = 10` in the instance's
  UserSettings.ini the only autosaves of turns 150-160 (run 1) and 150-155 (run 3) were `AutoSave_0150` and
  `AutoSave_Post_0150`, written at the end of turn 150; with the interval 1 (run 2) an `AutoSave_Post_N` came every
  turn (150-154). So the EXE applies the interval to the DLL's per-turn `gDLL->AutoSave(false, true)` too (the DLL has
  no interval test of its own, `CvPlayer::setTurnActive` [C, 4654]), and the factor `autosave` stays in the screening.
  In M's window 151-175 the level saves at the ends of turns 160 and 170 `[I: the runs fit "turn number a multiple of
  10"; whether the count could instead start at the load is not separated, the first save at 150 fits both]`.
- **The camera levels' premise holds `[L, runs 1 and 3, quick combat and movement on]`.** `Events.CameraViewChanged`
  fires on every frame while the camera moves (69 and 62 events in the 0.6-1.0 s after `UI.LookAt`, then none in the
  3 s settle) and **not once after the arm** over 10 (run 1) and 5 (run 3) autoplayed observer turns: nothing moves
  the camera with quick combat and movement on. The exclusion stays as a guard. The busy plot at turn 150 is (13, 44)
  with 9 units within radius 1 (both runs), the empty plot (0, 0); neither answer was -1.
- **quick_off moves the camera and is slow `[L, run 2]`.** With both options off the counter saw 6,448 events after the
  arm (about 2,000 per turn): the camera follows the fights (the DLL's `lookAt` behind `!quickCombat()` [C, 4654]).
  The first version of the counter kept a list of at most 500 event times and so counted 0 in the window; it now
  counts per game turn (fixed and tested the same day). Turns 151-153 took 151-154 s (doturn 154.45 s per turn)
  against 13.65 s in run 1 on the same instance with quick on: about 11 times as long (an indication). A 27-turn
  quick_off run would take about 70 min, so the screening plays quick_off in its own job with a 5-turn window.
- **quick_off's read-back `[L, run 2]`.** `Game.IsOption` answered false/false for both options after
  `Network.SendGameOptions` and 2 s; `PreGame.GetQuickCombat` / `GetQuickMovement` are not reachable in the channel's
  default (Main) state (recorded "none").
- **What D165's root cause does to the factors `[I]`.** None becomes pointless on the laptop: the turn is paced by U +
  D_B + T_tick per AI player, all frame-driven, so the frame setting (fps60, minimised), what is animated (quick_off,
  camera) and the UI Lua per frame (no_eui) are exactly what can move it; stats_off and autosave_10 move work on the
  game-core thread (U). `fps60` is pointless on oldpc (vsync-bound at 60 fps, 4666 step 2) and is left out if the
  screening runs there.

### The submit commands (ready, NOT submitted)

All in `perf/evidence/0437/4671/submit-commands.sh` (`--dry-run` checks every one, `--submit NAME` sends one). Checked
2026-11-17 with `submit.py run ... --dry-run` (new in 4671: every local check of `--print-only`, then the 3.8 grammar
of the 13 modules a series job runs, their import in a child Python, and no uncommitted change under `perf/turnstudy/`;
it never calls `tb.py submit`; output `perf/evidence/0437/4671/dryrun.log`): all jobs ok. Lengths `[I]` from the laptop's
M timed series (S2i 579 s, S4 585 s per run + the 60 s gap = 10.7 / 10.8 min per run; fps60 about +50 s a run;
quick_off about 23 min a run of a 5-turn window). Host laptop, quiet, headed; one laptop state per series (COMMON 23).

    # (a) the factor screening of S2i and S4 on M (grade B, 3 runs per level; factors.json). One job per round, the order
    #     rotated by round (--first-round), so the rounds can sit between other jobs. 9 runs a job, about 100 min each:
    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M \
        --arms S2i,S2i:fps60,S2i:minimised,S2i:no_eui,S2i:stats_off,S2i:camera_busy,S2i:camera_empty,S2i:poll5,S2i:autosave_10 \
        --runs 1 --first-round 1 --order rotate --quiet --expect-min 115 --grade B --series-id 0437-M-screen-S2i-1 \
        --issue 4671 --timeout 10800 --label "4671 W5 factor screening S2i round 1 of 3 (factors.json, grade B)"
    #     then --first-round 2 and 3 (labels "round 2 of 3", "round 3 of 3"); the same three for S4 with S4 in place of
    #     S2i and --series-id 0437-M-screen-S4-1. 6 jobs, about 10 h.
    # (a') quick_off apart (a 25-turn quick_off run would take about 70 min): 12 runs, about 180 min
    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M \
        --arms S2i,S2i:quick_off,S4,S4:quick_off --turns 5 --runs 3 --order williams --quiet --expect-min 200 \
        --grade B --series-id 0437-M-screen-quick-1 --issue 4671 --timeout 18000 \
        --label "4671 W5 factor screening quick_off, window 151-155 (grade B)"
    # (b) the S2i + S4 per-thread budget (Q2's stock side, grade A) with the switch-off pair for its cost:
    #     24 runs in 6 Williams rounds (4 arms: rounds 1-4 one full square, 5-6 its first two rows), about 260 min
    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M \
        --arms S2i:threads_on,S2i:threads_off,S4:threads_on,S4:threads_off --runs 6 --order williams --quiet \
        --expect-min 290 --grade A --series-id 0437-M-threads-1 --issue 4671 --timeout 21600 \
        --label "4671 W5 per-thread budget S2i and S4 (thread_budget on/off; Q2 stock side)"

Total about 19.5 h of quiet laptop time (the README's 13 h for the screening assumed quick_off ran as fast as the
base). An excluded run makes the job play its round again at the end (up to about one more round per job). On oldpc:
`--host oldpc` and without `fps60`.

The analysis after the fetch (`tb.py fetch JOB --out F:/c5-64bit-work/0437/<name> --include "runs/*/stats.db"`, then
`runrecord.py --rewrite` of the folder), per series:

    python -I perf/turnstudy/runner/analyze.py F:/c5-64bit-work/0437/screen-S2i-r1 F:/c5-64bit-work/0437/screen-S2i-r2 \
        F:/c5-64bit-work/0437/screen-S2i-r3 --out F:/c5-64bit-work/0437/screen-S2i-analysis.json \
        --pairs "S2i/S2i:fps60,S2i/S2i:minimised,S2i/S2i:no_eui,S2i/S2i:stats_off,S2i/S2i:camera_busy,S2i/S2i:camera_empty,S2i:poll5/S2i:autosave_10" \
        --records-to F:/c5-64bit-work/0437
    python -I perf/turnstudy/runner/analyze.py F:/c5-64bit-work/0437/screen-quick-1 --pairs "S2i/S2i:quick_off,S4/S4:quick_off"
    python -I perf/turnstudy/runner/analyze.py F:/c5-64bit-work/0437/threads-1 \
        --pairs "S2i:threads_off/S2i:threads_on,S4:threads_off/S4:threads_on,S2i:threads_on/S4:threads_on"

A factor's level goes to grade A (H and M, 6 runs per level) when the far end of the 95 % interval of
`diff_pct` exceeds 5 % (factors README). The threads series' `S2i:threads_on/S4:threads_on` pair gives Welch rows for
core CPU, main, workers, other, core idle and the EXE's CPU (`analyze.py` adds the thread rows to a pair whose two arms
carry them); its `threads_off/threads_on` pairs give the switch's cost in seconds per turn.

### Results of the threads series (W5, the stock side's per-thread budget; laptop, grade A) `[L, 2026-11-18]`

Series `0437-M-threads-1`, laptop job `20261117-202507-17a0` (2026-11-17 20:37 to 2026-11-18 00:57 EST), commit
8744bed78, 6 Williams rounds, 24 runs, M (save `TS0438-M-T150`, window 151-175), stock pacing, Quick Movement and
Quick Combat on, autoplay (observer). S2i = the stock EXE with the i0-ship DLL `i0ship-26234371-ia`; S4 = the
increment 1 RC (`VP64-first-playable-VP5.4.8-x64-04b1211b-pfdee656`). GPU driver (D175, as 0428's job of the same
evening recorded it on the laptop): Quadro P3000 32.0.15.7322 / Intel HD 630 27.20.100.9320. Fetched to
`F:/c5-64bit-work/0437/threads-1` (analysis in `analysis` and `analysis.txt`). No run excluded; every run IDENTICAL
within and across arms (stats.db, turns 150-177). Quiet proof: alone, not undisturbed (job level 0.174 foreign cores;
the start was held by a game left outside the testbed's jobs by 0428's crashed load, which ended before the job
started); per-run foreign cores 0.054-0.076. The first run (`r01-p1-S2i-threads_on`, 0.076 cores, 16.04 s) is its
arm's slowest (others 15.58-15.73 s), counted under R8.

| Arm | s per M turn (mean, 95 % CI) | EXE CPU s/turn | server CPU s/turn | EXE peak private | growth MB/turn |
|---|---|---|---|---|---|
| S2i:threads_off | 15.650 [15.586, 15.714] | 29.42 | - | 1838 MB | 2.17 |
| S2i:threads_on | 15.712 [15.535, 15.889] | 29.59 | - | 1841 MB | 2.32 |
| S4:threads_off | 15.439 [15.362, 15.515] | 24.73 | 5.94 | 1669 MB (sum with the server 1877) | 1.07 |
| S4:threads_on | 15.460 [15.405, 15.516] | 24.88 | 5.92 | 1671 MB (sum 1879) | 1.11 |

- **The switch's cost is not resolved**: S2i on - off +0.062 s per turn (+0.40 %), 95 % CI [-0.115, +0.240],
  detectable 0.243 s; S4 on - off +0.022 s (+0.14 %) [-0.061, +0.105], detectable 0.116 s. One snapshot costs 2.7 ms
  (S2i) / 5.3 ms (S4) per client turn boundary. Peak private, growth and CPU differences also unresolved.
- **Where the EXE's threads spend an M turn (threads_on, per turn, Welch S4 - S2i, n 6 + 6):**

  | Row | S2i | S4 | S4 - S2i, 95 % CI |
  |---|---|---|---|
  | wall | 15.709 s | 15.465 s | -0.252 s (-1.60 %) [-0.429, -0.075] |
  | core thread CPU (the thread that runs the DLL's turn in S2i, the proxy's calls in S4) | 5.261 s | 1.129 s | -4.132 s (-78.5 %) [-4.223, -4.041] |
  | core thread idle (wall minus its CPU) | 10.448 s | 14.336 s | +3.888 s (+37.2 %) [+3.785, +3.991] |
  | main thread CPU (frames, UI, the EXE's draining of the AI's visual records) | 8.688 s | 9.600 s | +0.912 s (+10.5 %) [+0.779, +1.044] |
  | worker threads CPU | 11.961 s | 11.101 s | -0.860 s (-7.2 %) [-1.057, -0.664] |
  | other threads CPU | 3.676 s | 3.043 s | -0.633 s (-17.2 %) [-0.706, -0.559] |
  | EXE process CPU (all threads) | 29.586 s | 24.873 s | -4.713 s (-15.9 %) [-5.143, -4.284] |
  | server.exe CPU | - | 5.924 s | - |

- **Reading** `[L numbers, I cause]`: in the stock game (S2i) the DLL's thread is busy only 5.3 s of a 15.7 s M turn
  and idle 10.4 s (67 %); in our shell it is idle 14.3 s of 15.5 s (93 %) while the server's CPU is 5.9 s. So in both
  clients the AI turn's wall is not the DLL's CPU but the EXE's pacing of `Update` calls (D165, the 0.62 s per
  `Update` interval above): the same conclusion as the oldpc budget (S4, `dll_idle` 67.6 %) and 4654's grade C stock
  indication (core idle about 86 %), now at grade A on the laptop with both clients on one method. Moving the DLL to
  the server removes 4.1 s of CPU from the EXE's core thread but shortens the wall by only 0.25 s, because that
  thread was waiting anyway; the main thread does 0.9 s more CPU per turn in S4 (the proxy's work on the main thread
  and its handling of queue B are the candidates, not measured apart). Both processes together use 30.8 s of CPU per
  S4 turn against 29.6 s in S2i (+1.2 s, cross-process sum; not a Welch row). Memory: the EXE's peak private is
  170 MB lower in S4, the sum with the server 37 MB higher (+2.0 %, [+17.6, +56.6] MB) than S2i alone; the EXE's
  growth halves (2.32 -> 1.11 MB/turn; with the server's growth about 1.5 MB/turn).
- **Caveat** (0518, 0522, above under "Q2, the product side"): these runs are observer autoplay; the core thread's
  CPU in S2i holds the observer-only TechPanel listener's work (0.55-0.6 s a turn of observer-only rebuild, MC #4974),
  and they lack the player's view (+1.17 s a turn on oldpc with the observer's interface held on player 0, grade B),
  so neither the core thread's CPU nor the wall is a player's number `[I]`; no O arm was run. The conclusion that the
  wall is pacing, not the DLL's CPU, does not depend on it.
- Not compared with the oldpc set or other laptop series (D106); grade A by docs/briefs/PERFORMANCE.md (declared
  before the run, 6 runs per arm, Williams order).

### Results of the factor screening, S2i (W5; laptop, grade B) `[L, 2026-11-18]`

Series `0437-M-screen-S2i-1`, laptop jobs `20261117-204212-32e2` (round 1), `20261118-005943-9cf2` (round 2),
`20261118-052035-6210` (round 3), 2026-11-18 00:59-09:16 EST, commits af19b0649 / 13eb09b2c / 09aedca5f (no change
under perf/turnstudy/ or tools/ between them), one run per level per round in rotated order, n = 3 per level, M window 151-175,
the base of `perf/turnstudy/factors/factors.json` (QC and QM on, uncapped, 1920 x 1080 on screen, autosave every turn,
statistics on, observer autoplay). GPU driver as in the threads series (D175). No run excluded; per-run foreign
cores 0.035-0.064. Every level IDENTICAL to the base (stats.db, turns 150-177) except `stats_off`, which writes no
stats.db and so cannot be checked. Analysis: `F:/c5-64bit-work/0437/screen-S2i-analysis.json` / `.txt` (command in
"The analysis after the fetch" above).

| Level | s per M turn (n 3) | level - base, 95 % CI | EXE CPU s/turn, level - base |
|---|---|---|---|
| base (S2i) | 15.602 | - | - |
| **minimised** (`SW_SHOWMINNOACTIVE` after the arm) | **10.554** | **-5.048 s (-32.4 %) [-5.317, -4.778]** | +0.36 (n.s.) |
| **autosave_10** (TurnsBetweenAutosave 10) | 14.813 | **-0.789 s (-5.1 %) [-1.130, -0.448]** | -1.91 [-2.80, -1.01] |
| **stats_off** (statistics logging off) | 15.040 | **-0.562 s (-3.6 %) [-0.855, -0.270]** | -0.53 (n.s.) |
| camera_busy (the busiest plot in view) | 16.173 | +0.571 s (+3.7 %) [-0.011, +1.153] | **+4.97 [+3.51, +6.42]** |
| camera_empty (an empty water plot in view) | 15.659 | +0.057 s (+0.4 %) [-0.724, +0.839] | **-4.60 [-6.54, -2.67]** |
| no_eui | 15.475 | -0.127 s (-0.8 %) [-0.422, +0.168] | n.s. |
| fps60 (the harness's 60 fps cap) | 15.543 | -0.059 s (-0.4 %) [-0.318, +0.200] | n.s. |
| poll5 | 15.634 | +0.032 s (+0.2 %) [-0.218, +0.283] | n.s. |

Reading `[L numbers, I cause]`:
- **A minimised stock window plays an M AI turn a third faster** (5.05 s of 15.60 s), with the same game and the same
  CPU. With nothing drawn the EXE's frames are short, so its `Update` requests come at a shorter interval: direct
  support for D165's finding that the interval between `Update` calls, set by the EXE's frame, paces the AI turn in
  the client (the threads series: the DLL's thread idles 67 % of the turn). Peak private +116 MB [+9, +223] (n 3,
  wide).
- **Per-turn autosave costs about 0.8 s of an M turn** in the stock client (its own save line says 0.6 s; the rest
  is the EXE's work around it), and the statistics log about 0.56 s. Both are settings a player chooses; the study's
  base keeps both on (the timed series' cross-check clock and identity check need them).
- **The camera moves CPU, not the wall**: looking at the busiest plot adds 5.0 s of EXE CPU per turn and the wall
  +0.57 s (not resolved); an empty view takes 4.6 s of CPU off with no change to the wall. What is drawn costs CPU
  on other threads, but the AI turn's length is held by the pacing.
- fps60, no_eui and poll5: no effect resolved (detectable 0.33-0.37 s).
- Screening rule (factors README): a level goes to grade A when the far end of its interval passes 5 %:
  **minimised, autosave_10, stats_off (-5.5 %), camera_busy (+7.4 %)**, and camera_empty only by its width (+5.4 %).
  The S4 side follows (`0437-M-screen-S4-1`); the grade A follow-up is for the lead and the MC to schedule.
- Autoplay caveat (0522, "Q2, the product side" above) applies: these are observer turns.

### Results of the factor screening, S4 (W5; laptop, grade B) `[L, 2026-11-18]`

Series `0437-M-screen-S4-1`, laptop jobs `20261117-204249-a868` (round 1), `20261118-051955-4879` (round 2) and
`20261118-103320-98b1` (round 3, played again 10:59-12:28 at commit 3bab1e45a after the first round-3 job
`20261118-095742-2120` lost 7 of 9 runs to the harness's commit floor, below; its two good camera runs are left out so
every level has n = 3), same base and method as the S2i screening above; S4 = the increment 1 RC
(`VP64-first-playable-VP5.4.8-x64-04b1211b-pfdee656`), stock pacing (no D165 option set). No run excluded; per-run
foreign cores 0.032-0.063. Every level IDENTICAL to the base (stats_off: no stats.db). Analysis
`F:/c5-64bit-work/0437/screen-S4-analysis.json` / `.txt` (r1 + r2 + r3b). (Corrected 2026-11-18 12:30: replaces the
interim table of 3bab1e45a, which had n = 2 for seven levels.)

| Level | s per M turn (n 3) | level - base, 95 % CI | EXE CPU s/turn, level - base |
|---|---|---|---|
| base (S4) | 15.333 | - | - |
| **minimised** | **10.034** | **-5.299 s (-34.6 %) [-5.483, -5.114]** | -1.79 [-2.20, -1.37] |
| **autosave_10** | 14.734 | **-0.599 s (-3.9 %) [-0.766, -0.432]** | -1.21 [-1.64, -0.79] |
| **no_eui** | 14.887 | **-0.445 s (-2.9 %) [-0.546, -0.345]** | +0.77 [+0.34, +1.19] |
| stats_off | 15.220 | -0.113 s (-0.7 %) [-0.180, -0.045] | n.s. |
| camera_busy | 15.635 | +0.302 s (+2.0 %) [+0.104, +0.500] | **+3.44 [+2.99, +3.89]** |
| camera_empty | 15.285 | -0.047 s (-0.3 %) [-0.330, +0.235] | **-3.85 [-4.28, -3.43]** |
| fps60 | 15.330 | -0.003 s (-0.0 %) [-0.120, +0.115] | n.s. |
| poll5 | 15.424 | +0.092 s (+0.6 %) [-0.117, +0.300] | n.s. |

Reading `[L numbers, I cause]`: the same picture as S2i. **Minimised: -5.30 s (-34.6 %)** in our shell too, with
1.8 s less EXE CPU and peak private +142 MB [+132, +153]: the EXE's frame paces the AI turn in both clients. The
camera moves 3.4-3.9 s of EXE CPU and at most 0.3 s of wall. Per-turn autosave costs 0.60 s (S2i: 0.79 s). The
statistics log costs 0.11 s in S4 against 0.56 s in S2i: the server writes it on its own thread, off the EXE's
`Update` `[I]`. Without EUI the S4 turn is 0.45 s shorter with 0.77 s more EXE CPU (in S2i -0.13 s, not resolved).
Not compared with S2i's seconds as a Welch row (separate jobs and days). Screening rule: minimised (-35.8 % far end)
goes to grade A; autosave_10 (-5.0 %) at the edge; no_eui (-3.6 %), stats_off, camera_busy (+3.3 %) do not in S4.

### Results of screen-quick: Quick Combat and Quick Movement off, observer autoplay (W5; laptop, grade B) `[L, 2026-11-18]`

Series `0437-M-screen-quick-1`, laptop job `20261118-103350-ba1f` (2026-11-18 12:48-15:19 EST; submitted by the ninth
lead), arms S2i, S2i:quick_off, S4, S4:quick_off, 3 rotated rounds, n = 3 per arm, **M window 151-155 (5 turns)**,
base as the screenings (stock pacing, observer autoplay: every plot seen, so every AI move and fight is animated);
quick_off turns both options off for the game process before the window (read back). Every run launched (no
commit-floor refusal; per-run foreign cores 0.055-0.085). Every run IDENTICAL within its arm, S2i = S4, and each
quick_off arm = its base (stats.db, turns 150-157): the quick options do not change the game. Analysis
`F:/c5-64bit-work/0437/screen-quick-1/analysis` (+ `.txt`, copy in `perf/evidence/0437/v3/screen-quick-analysis.txt`).

| Arm | s per M turn (n 3), 95 % CI | EXE CPU s/turn | server CPU s/turn | peak private (S4: + server) | growth MB/turn | clock |
|---|---|---|---|---|---|---|
| S2i | 14.135 [13.828, 14.442] | 28.4 | - | 1792 MB | 1.65 | 110 % |
| S2i:quick_off | **162.788** [162.253, 163.323] | 254.0 | - | 1891 MB | 23.81 | 83 % |
| S4 | 14.281 [14.221, 14.342] | 24.4 | 5.4 | 1648 MB (1850) | 1.03 | 105 % |
| S4:quick_off | **162.111** [161.879, 162.344] | 244.6 | 15.2 | 1726 MB (1928) | 21.47 | 85 % |

Reading `[L numbers, I cause]`: with the observer seeing every plot, turning the quick options off makes an M AI turn
**11.5 times longer** in both clients (+148.7 s stock, +147.8 s ours) and the two clients equal (162.8 against 162.1
s): the EXE's animation of every AI move is the whole turn, and our server's gain disappears in it. The CPU is about
nine times higher (the clock falls to 83-85 %), and memory grows 21-24 MB a turn against 1.0-1.7 (peak +100 MB in S2i,
CI [+24, +176]; +79 MB in S4, not resolved). An upper bound, not a player's view: from player 0's view the DLL already
teleports moves no human sees (ticket 4678), and the D178 series following player 0 gave 37.6 s per M turn at stock
pacing and 14.75 s with options 2 + 3 ("Result of the laptop series with Quick Movement off"). Window 151-155 only:
not comparable in seconds with the 151-175 series.

**The laptop's memory on 2026-11-18 10:31 EST** (read-only check over SSH by lead-0437) `[L counters, I cause]`:
commit 13.2 of 19.5 GB used (free 5.9 GB), RAM 15.8 GB with 6.8 GB free; the processes' private commit sums to
6.4 GB; **paged pool 3.03 GB, nonpaged pool 0.89 GB** (several times their usual size), uptime since 2026-11-11 14:41
(7 days); the largest process is the testbed runner's Python (`runner.py serve`, since 2026-11-16, 1.4 GB). About
4-7 GB of commit is held outside the processes: a slow kernel-pool or handle leak over the week is the likely cause
(poolmon, which needs administrator rights, would name the pool tag; not run). The harness's 6 GB floor (set after
the 2026-11-11 commit-limit crash) refuses game launches at this level, so a timing job submitted to the laptop in
this state loses its runs. Asked of the MC (#5611). The MC (#5612, 10:35) restarted the testbed runner
(`commission.py restart-runner`, pid 1908 -> 7228; no system action; a reboot only on the owner's word): idle free commit
between jobs then 7.8 GB (10:54), with a game running 14.2-16.0 GB of 19.5 GB used; the pools unchanged (paged
2.9-3.1 GB, nonpaged 0.89 GB) (samples every 5 min 10:34-11:04, MC #5638). The replayed round 3 launched every run.
The runner's 1.4 GB was the margin: if the pools keep growing, launches are refused again until a reboot.

### The runner's memory cap on tools/harness/jobcap.py (acceptance 5) `[C, tests 2026-11-17]`

`headlessarm.py` now starts every headless host with `tools/harness/jobcap.py`'s `popen` (ticket 0488: the host
starts suspended, joins the job object, then runs; children join too; a host past the cap gets 10 s, then jobcap ends
it), loaded by path as the module `harness_jobcap` (as `tools/testbed/jobs/hostruns.py` does), so no folder put first
on `sys.path` by another test module can shadow it. `tsjobcap.py` is removed; 4664's cap size
(`headlessarm.default_cap_mb`, 8192 MB unless the scenario says) and hit rule (`hit_rule`: a jobcap event, or the peak
within 1 % / 64 MB of the cap) are kept; `run.json` `memory_cap` names the method, jobcap's events and the peak. The
three jobcap failures of a whole-folder run had already gone with 4664's rename (baseline 2026-11-17 09:5x: 225
passed); the live tests now run the harness's jobcap (a 400 MB child under a 200 MB cap exits 3, `hit`; a 20 MB child
does not). One race fixed on the way: a host that ended before the sampler opened it raised NoSuchProcess (a usage
error); it is now recorded as `sampler_error` and judged by its exit (the cap test then gives exit 5, "memory cap").

`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest perf/tests/turnstudy -q`: **244 passed** (2026-11-17 10:57,
workstation, Python 3.11; 225 at 4671's start; new: `test_threads4671.py` 18 tests and one jobcap test);
`python -I perf/turnstudy/budget/stock/tests/test_stock.py`: 0 failed (4654's script, after the `pid` filter).

### Not verified (4671)

- Nothing of 4671 ran on the laptop or oldpc: the thread budget, the camera key and the read-back ran on the
  workstation only (one run each); the 3.8 grammar and the imports were checked by `--dry-run` with the workstation's
  Python 3.11, not with a 3.8 interpreter.
- quick_off in S4 (the options call in the product) and the camera levels with quick_off on the laptop were not run.
- Whether `TurnsBetweenAutosave` counts by turn number or from the load (the first save at 150 fits both).
- The per-turn camera counter was checked live only with no event after the arm (run 3); its counting of many events
  per turn is tested offline.
- The thread budget's effect on the turn time (the threads series' on/off pair measures it); its per-thread rows have
  no ledger ids yet (registry review needed).
- PreGame's quick values in the InGame Lua state (only Main was asked).
- The check runs' `stats.db` were not compared with statscmp (functional runs; the series check identity per run).

## What paces an AI turn in the client (D165; tickets 4656 and 4657; 2026-11-16/17)

Written by lead-0437 (second lead) 2026-11-17 00:15 EST from the hand-backs of impl-4656 (static reading, 2026-11-16
23:25-23:41, the Steam EXE TimeDateStamp 0x546CD0A8 with cdb 10.0.26100, one 0317 stall dump, the DLL fork at
04b1211b; full text with marks `perf/evidence/0437/4656/update-pacing.md`) and impl-4657 (per-Update frame trace,
workstation check 2026-11-16 23:43 - 2026-11-17 00:09, below). Nothing from the EXE is committed (D83).

### In short

- **[L] An M turn is exactly 25 `Update` calls in every host**: headless S3 (`turn-times.tsv` `updates`), S4 through
  the proxy (4652's b2 run) and, by 4657's trace, S2i, S2t and S4 (one AI player step per `Update`: one
  `PlayerPreAIUnitUpdate` and one `PlayerDoTurn` of the next player each; `AIProcessingStarted/EndedForPlayer` 25 times).
- **[C] The DLL plays one AI player per `Update`**: `CvGame::updateMoves()` takes only the first turn-active AI player
  ("we do them sequentially"), that player's whole turn runs in that `Update`, `CheckPlayerTurnDeactivate()` then
  activates the next one. The DLL does not wait for the EXE between players (AI mission timers are 0; `IsBusy` is the
  mission timer or `isInCombat`).
- **[C] The EXE asks for an `Update` only on a 0.1 s tick**: the main thread's `GameViewState::Update(dt)` (0x7253E0)
  adds the frame's dt to the `.data` float 0xCCF91C; when it exceeds `ICvEngineUtility1::getSecsPerTurn()` (engine
  utility vftable 0x9EA718 slot 102, 0x704B60, which returns the pooled `.rdata` constant 0.1 at 0x9E402C) the frame
  calls the request 0x53AF10 at 0x7258E5 and sets the accumulator to 0 (a reset, not a subtraction). At 60 fps a tick
  is 7 frames (0.117 s). The request is SKIPPED (the tick lost) while an `Update` is still running (`IsGameCoreExecuting`,
  0x603580) and, in AI turns, while the first turn-active player differs from `GameViewState+0x8CC` and queue B (the
  DLL's visual events, `0x2A10E08 + 0x4C*[0x2A10200]`) still holds records. Queue B is pumped only on frames with no
  `Update` pending (0x4B3FF4 / 0x4B4029). So per AI player the period is **U (the `Update`) + D_B (draining that
  player's visual events) + T_tick (to the next tick)**, strictly in series. `[I]`: D_B is the largest part.
- **[L, workstation, grade C] Many ordinary frames between player steps, not one slow frame**: median 12-15 frames per
  step, mean frame time 12-20 ms; no step shorter than 0.097-0.101 s (the tick); after a long `Update` (major civs, the
  turn change) the wait is 0.42-0.54 s of ordinary frames, which the tick alone does not explain (consistent with D_B
  [I]). Per M turn about 2.7-4.1 s inside `Update` and 5.6-7.4 s waiting. A 60 fps cap moved the wait (5.60 -> 7.38 s per
  turn), not the pattern. S4's `Update`s are shorter than S2i's (2.7 against 4.1 s per turn) but its waits longer, so
  the turn takes the same time.
- **Consequence for D165 [I]**: the EXE's share of an AI turn is the per-player serialisation U + D_B + T_tick, 24 times
  per M turn, not slow EXE code. The laptop job `4657-M-trace` (S2i and S4, as-run and fps60, 5 turns) decides the split
  on the laptop; an external memory poller (4656's list: pending `[[0x2DD202C]+0x40]`, the accumulator, queue B's fill,
  `GameViewState+0x8CC`, the AI-turn byte 0x2DE0447, the hold 0x2DE044C) would measure U, D_B and T_tick directly.

### Amelioration options by approved means (4656's ranking; none implemented, 2026-11-17)

*Corrected 2026-11-17 01:39 by impl-4660: option 1 was built (ticket 4660) and **deadlocks the windowed client** in the
first AI batch [L, 2 of 2 runs]: the batch holds the game-core lock while UI Lua on the EXE's main thread waits for it
and the DLL waits for a GameEvents Lua call on the EXE's core thread. Not viable in this form; rank 4 is expected to
deadlock the same way [I]. Details: subsection "The fix candidate `ai_players_per_update`" at the end of this section.
The ranking below is 4656's, kept as written.*

*Added 2026-11-17 04:25 by impl-4662: option 3 is built (ticket 4662, `[Performance] AiUpdateTickMs`, off by default);
on the workstation it removed T_tick (1.82 -> 0.002 s per M turn) with the game identical. Details: subsection "Option
3 built" at the end of this section. The frame rate did not fall (impl-4663, 2026-11-17: the poll cannot count frames with the option on;
the proxy counts 49.1 fps): subsection "The frame rate with AiUpdateTickMs = 0".*

1. **The server plays several AI players per EXE request** (`server/src/remote/srv_rt.cpp`, around the served
   `Update`): call the DLL's `Update` again while the first turn-active player is an alive non-human, the game turn is
   unchanged, the game is neither paused nor over, nothing waits for input and no `GameplayDiplomacyAILeaderMessage`
   crossed. Gain up to 24 of 25 per-player waits, about 7-11 s of a 15.5 s laptop M turn `[I]`. Identity expected (the
   DLL sees the same `Update` sequence); needs the D164 windowed statscmp pair plus a turn with a leader message. The
   player sees AI moves arrive in batches. Option `ai_players_per_update` (1 = stock). Product only (S4).
2. **Write `GameViewState+0x8CC` (a heap field) after each AI `Update`** so the player-change gate does not wait for
   queue B: removes D_B, keeps T_tick; the next player starts while the previous one's moves still show. Proxy-side.
3. **A shorter tick in AI turns** through a copied engine utility vtable on the heap object whose slot 102 answers
   about 0 for the frame's call site only during AI turns: about 1.5-2.5 s per M turn `[I]`; combines with 2.
4. **Option 1 inside the DLL** (`CvGame::update` repeats while only AI players move): also helps the 32-bit stock DLL
   and could go upstream (D104); identity only if each repetition is a whole `update` with its Lua hooks.
5. Not recommended: clearing queue B's ordering byte (events out of order). Not allowed: writing 0x9E402C (`.rdata`,
   shared constant) or any test in `.text`.
   *Corrected 2026-11-17 by impl-4672 (subsection "What the drain is made of"): with the byte cleared the move still
   walks and dispatches in posting order [C]; what is lost is the wait for held records (combat simulations: none with
   Quick Combat on, no record was held in 125,768 seen) and the one-frame spacing of one unit's keyed records. The
   ordering is what makes the drain frame-bound (about 180 frames per M turn with options 2+3), so this option is back
   on the list (`d165-pacing-fix-design.md` section 10), guarded and with a picture check.*

### The per-Update frame trace (ticket 4657)


Text for `research/08-performance/turn-time-study.md` (a new section "The per-Update frame trace (ticket 4657, D165)"),
written by impl-4657 2026-11-17 00:15 EST. Evidence: `perf/evidence/0437/4657/` (per run: `update-trace.tsv`,
`trace.txt`, `trace.json`, the step and turn tables, `wscheck.log`); raw run folders `F:/c5-64bit-work/0437/4657-ws/`.

#### The instrument

- **Off by default.** Scenario variants `trace` (the series' frame setting: a timing launch, no cap) and `trace_fps60`
  (fps60's capped launch, then the harness cap at 60) set the new variant key `update_trace` (`tslib.VARIANT_KEYS`); a
  run without it has no `update_trace` key in its settings and the runner does nothing new (the add-in file is 4653's
  byte for byte, a test checks it). Never in a timed series: the trace is an instrument (grade D runs).
- **What it records** (`clientarm.trace_lua`): a per-frame handler on `Events.LocalMachineAppUpdate` (main thread)
  counts frames, sums and maxes the frame delta argument and keeps the largest `os.clock()` step between two frames; a
  row at each `GameEvents.GameCoreUpdateBegin` (B) and `GameCoreUpdateEnd` (E) (the DLL raises both at the start and end
  of every `CvGame::update`, `CvGame.cpp` [C]), `PlayerDoTurn` (D), `PlayerPreAIUnitUpdate` (P),
  `Events.AIProcessingStartedForPlayer` (S) and `AIProcessingEndedForPlayer` (X): kind, player, game turn, `os.clock()`,
  frames so far, delta sum so far, the largest delta and clock step since the previous row. Arrays only, no string or
  I/O while rows are taken; rows stop after the turn that closes the window (T+N+1). Channel arms (S2i, S4) install it
  in the InGame state beside the clock handler before the arm and read it back after the window in chunks of 400 rows
  (R9); add-in arms (S2t) carry it in the add-in, which writes the rows once into its `Modding.OpenUserData` database
  at autoplay's hand-back (T+N+2); the runner waits for the file, reads it after the stop and removes it by hash
  (`ModUserData/<add-in id>-1.db`) [L].
- **A step = one Update** (B to the next B), split into the Update itself (B to E: the game-core thread inside
  `CvGame::update`) and the wait (E to the next B: the core thread suspended until the main thread's next request,
  `game-core-thread.md` 0). `perf/turnstudy/trace/trace.py` gives per step, turn and run: steps per turn, frames per
  step (mean, median, quantiles, bins), seconds per step, the Update and wait parts, mean frame time (delta sum / frames
  and clock / frames), the slowest frame's share of its step; with `run.json` the turn seconds against the run's
  PlayerDoTurn clock, with `server-turn-times.tsv` the steps against the server's `updates`. Exit 0; 2 a missing or
  malformed trace (nothing written); 3 a turn's steps differ from `--expect-steps`; 4 a window turn without steps or not
  closed. Tests: `perf/tests/turnstudy/test_trace.py`, 26 (the turnstudy suite 98 with 4653's 72).

#### Workstation check `[L, 2026-11-16 23:43 - 2026-11-17 00:09 EST, workstation, shared (7 other heavy jobs registered), c5h-a, window cloaked (the harness's no-focus launch), one run of 3 window turns per arm: grade C, an indication, no claim]`

`perf/turnstudy/trace/wscheck.py c5h-a ARM:VARIANT --turns 3` (the runner's ClientRun with the workstation's launch):
M from `TS0438-M-T150` by hash, window 151-153, statistics logging on (client database `4e4620b2`; S2t off, `02b5faa4`),
every run exit 0, restored by hash, `provision.py verify` rc 0, no dialog. Per-turn means over 151-153:

| Run | s/turn | steps/turn | frames/step median (mean) | s/step median | in Updates / waiting per turn (wait share) | shortest step | short Updates (< 50 ms): n, s/step median | long Updates: n, wait after them median | frame ms mean | slowest frame / step median |
|---|---|---|---|---|---|---|---|---|---|---|
| S2i:trace (uncapped) | 9.71 | 25 | 15 (31.5) | 0.135 | 4.11 / 5.60 s (58 %) | 0.100 | 49: 0.112 | 26: 0.417 | 12.3 | 0.18 |
| S2i:trace_fps60 | 10.88 | 25 | 13 (21.2) | 0.217 | 3.50 / 7.38 s (68 %) | 0.100 | 50: 0.117 | 25: 0.518 | 20.5 | 0.17 |
| S4:trace (the RC) | 9.76 | 25 | 12 (23.9) | 0.213 | 2.74 / 7.02 s (72 %) | 0.100 | 50: 0.146 | 25: 0.525 | 16.3 | 0.31 |
| S4p:trace (4652's b2 budget build, VP64_TURN_BUDGET=1) | 9.98 | 25 = server `updates` (151-154) | 12 (24.0) | 0.219 | 2.88 / 7.10 s (71 %) | 0.101 | 51: 0.149 | 24: 0.541 | 16.7 | 0.31 |
| S2t:trace (add-in, the tag's DLL) | 10.68 | 25 | 13 (28.2) | 0.217 | 4.08 / 6.59 s (62 %) | 0.097 | 48: 0.119 | 27: 0.486 | 15.1 | 0.20 |

- **25 Updates per M turn in every client arm, one player per Update** [L]: each Update raises B, one
  `PlayerPreAIUnitUpdate(p)` and one `PlayerDoTurn(p+1)` (the next player's turn start), then E; S and X fire 25 times a
  turn each. The order in a turn: P1/D2 ... P7/D22 ... P37/D63, P63 (no D), then the turn-change Update (P0, D0 and
  D1; 1.3-2.1 s in S2i). In S4p the steps equal the server's own `updates` column turn by turn (25 in 151-154) [L]. The
  step clock's turn length matches the run's PlayerDoTurn clock within 0.03-0.45 s per turn (different boundary points:
  the turn's first Update against player 0's PlayerDoTurn); the server's turn seconds (8.69, 10.20, 10.93) match the
  PlayerDoTurn clock (8.62, 10.26, 10.93) within 0.07 s.
- **The EXE asks for an Update at most every ~0.1 s** [L]: no two Updates closer than 0.097-0.101 s in any run; before
  the arm (the human's turn, nothing to do) the Updates come every 0.101-0.119 s (13-15 frames apart uncapped). The 16
  city states' Updates (under 50 ms each) therefore take 0.11-0.15 s per step: the pacing floor, about 2 s of a turn.
  This agrees with engine-contract 6.1's median gap of 105 ms (headless host) and is what 4656's static reading of the
  frame-time accumulator should explain.
- **Between two steps the EXE draws many ordinary frames, not one slow frame** [L, workstation]: median 12-15 frames
  per step, the slowest frame 17-31 % of its step (median), mean frame 12-20 ms. The core thread spends 58-72 % of a
  turn waiting for the next request. After a long Update (the 7 major civilisations, the turn change) the wait is not
  the 0.1 s floor but 0.42-0.54 s (median; 0.17-0.89 s) of ordinary frames: something besides the accumulator holds the
  next request after a busy Update [I: e.g. the main thread working off what the Update queued (unit moves, UI events)
  before it asks again; not read].
- **The frame cap moves the wait, not the pattern** (one run each, indication): at 60 fps (about 49 fps achieved) S2i's
  turn is 10.88 s against 9.71 uncapped, with less time in Updates (3.50 against 4.11 s) and longer waits (7.38 against
  5.60 s); the pattern stays "many frames per step".
- **S4 against S2i on the workstation** (indication): S4's Updates are shorter (2.7-2.9 against 4.1 s per turn: the
  64-bit DLL) but its waits longer (7.0-7.1 against 5.6 s), so the turn is the same (9.8-10.0 against 9.7 s): the EXE's
  pacing absorbs the server's gain.

#### Not verified (4657)

- Only the workstation, cloaked, shared, 3 turns, one run per arm: frame times and waits on the laptop (foreground,
  0.62 s per Update) may differ; the laptop job decides the D165 question there.
- That one `LocalMachineAppUpdate` call is one presented frame (the counter is the main loop's; 0350 counts it as the
  frame rate).
- The delta argument can exceed the wall gap between two handler calls (turn 150 of S2i: largest delta 2.891 s,
  largest clock step 1.975 s in the same step) [L, unexplained]; `gap_max` (os.clock) is the one used for the shares.
- The trace's own cost (a Lua call per frame, about 260 rows per turn) is not measured; trace runs are not timing runs.
- What holds the request after a long Update (above) is not read.

### The fix candidate `ai_players_per_update` (option 1, ticket 4660): built, and it deadlocks the client `[2026-11-17, impl-4660]`

Lead's note (lead-0437, 2026-11-17 01:50 EST): the code of this candidate (server/src/remote/srv_ai_batch*, the loop in
`srv_rt.cpp`, its tests and runner arm S4x) stays on branch `task-4660-ai-players-per-update` (pushed) as evidence; it is
NOT merged into the epic branch (a prototype that failed; D165: nothing that fails merges). Only this text and
`perf/evidence/0437/4660/` are.

Written by impl-4660 2026-11-17 01:39 EST (account A, worker of lead-0437). Branch `task-4660-ai-players-per-update`.
Marks: `[C]` read in code or in a stack dump, `[L]` seen live, `[I]` inferred.

**Result in one line.** Option 1 as specified (the server calls the DLL's `Update` again inside the EXE's request while
only AI players move) **deadlocks a windowed client in the first AI batch, with or without instrumentation** `[L, 2 of 2
runs]`; the product's watchdog turns it into the D118 popup after 10 minutes. The option is NOT viable in this form, and
no laptop timing job should be submitted for it. With the option off, the new server plays the RC's game (identity
below).

**What was built** (server only; the DLL, the proxy, the shell and the harness unchanged):
- `server/src/remote/srv_ai_batch_core.h` (header-only rules), `srv_ai_batch.{h,cpp}` (the DLL reads, the stop-call
  table, the counters), hooks in `srv_rt.cpp` (`EnterUpdate`/`LeaveUpdate`: the loop runs after the first DLL `Update`'s
  bookkeeping; `Begin`/`End`: the stop calls) and `srv_remote_main.cpp` (`SrvAiBatch::Init` after `SrvBudget::Init`).
- **Value** (prototype): the environment variable `VP64_AI_PLAYERS_PER_UPDATE` of the EXE (server.exe inherits it, as
  `VP64_TURN_BUDGET`): absent or `1` = stock (nothing else runs: one int test per hook), `2`..`64`, `-1` = 64; anything else
  (empty, `0`, `65`, `01`, ` 2`, `abc`, longer than 63 characters) ends the server through `ReportAndExit(1)` at start-up
  (D118, no silent stock). The options file (`VP64Options.ini`, D89) is not used: its loader is shared with the proxy
  (`options/`), and D89's rules make a bad value a warning and the default, not a refusal (see "the option path" below).
- **Conditions, checked before each extra DLL `Update`** (first failing one is the stop reason): no stop call crossed in
  this request (`GameplayDiplomacyAILeaderMessage`, whose EXE handler sets the update hold, `game-core-thread.md` 4;
  `AddPopup` both overloads, `setForcePopup`, `NotifySpecificAILeaderInGame`, `DoClearDiplomacyTradeTable`; and
  `isDiploActive` answered true, the DLL's blocking-input test); the game turn equals the one at the request's start;
  `IsPaused` false; `GetGameState` ON or EXTENDED; the first alive, turn-active player exists and is not human; no later
  alive, turn-active player is human. The turn change therefore always gets its own EXE request (`CanAdvanceTurn` asked at
  stock pace). "Waiting for blocking input" (`CvGame::IsWaitingForBlockingInput`) is on no DLL interface; the leader
  message and `isDiploActive` that cause it are the proxies for it `[C, CvPlayer.cpp 10321, CvDiplomacyRequests.cpp 410]`.
- **Fail loud**: a nested served `Update` (depth 2 on one thread) or a second request on another thread while one is
  served, the game unreadable at the request's start, or an extra `Update` impossible: `ReportAndExit(1,
  "ai_players_per_update: ...")`.
- **Counters** (option on only): `ai-per-update.tsv` in the run folder, a row per EXE request (`turn request updates
  stop stop_call`), flushed at each turn's first request and at the report; a summary in server-report.txt (requests,
  DLL `Update`s, requests by their number of `Update`s, by stop). The runner copies both into the run folder.
- Why not a `ServerHook` for `Update`: `gen_runtime.py` emits no hook declaration for special methods, so a
  `ServerHook_ICvGame2_Update` would not compile without a generator change (a proxy-side file) `[C]`.

**Tests** `[L 2026-11-17]`:
- `server/tests/aibatch/run_aibatch_test.py --asan`: **176 checks pass** in clang-cl x64 /O2 and MSVC /fsanitize=address
  x64 (canary caught): the value (default; 7 accepted forms and -1; 20 refused forms, none leaves a count), the
  environment variable (absent, set, empty with a stale last error, a 200-character value, a short buffer), the stop-call
  classes (8 positives, 14 negatives such as `AddPopupText`, `isDiploOrPopupWaiting`, `NotifyAILeadersInGame`), every
  condition alone and their order, the loop against a fake DLL (stock makes no DLL read; cap; round end; stop call in the
  first or third `Update`; a human seat; an impossible `Update`), and a simulated 5-turn sequence whose DLL `Update` order is
  the same for N = 1, 2, 3, 7, 24, 25, 64 (125 `Update`s; 25 requests per turn stock, 2 with N >= 25).
- `perf/tests/turnstudy`: 116 pass (5 new in `test_aioption.py`: the variant key off by default, `app25`/`app1`, refused
  values, the environment set only by the variant, arm S4x).

**Build b1** (`python -I server/build_server.py --src <git archive of the fork at 04b1211b8> --out
C:/c5-64bit-work/4660/b1 --config release --offline --deps <copy of 0441's deps-release>`, 00:37-00:39, repo 2c31c8039,
clean): **server.exe `dc7fb51ba2f065a4e8ea3b9350b95d0e114a07bd275df00038319142b55335ff`**, proxy `6b709d68...`
(unchanged source), vp64helper `a7f1f46b...`. Kept: `F:/c5-64bit-work/4660/b1-bin`, `b1-proxy`, `layout-rc/VP64` (the RC's
VP64 files with this server).

**The RC's proxy does not pair with a server of the current tree** `[L 2026-11-17 00:47-01:02, c5h-b]`: arm S4x first ran
the RC's proxy 75C40CEC with the b1 server; the attach succeeded (same schema hash 0x765d76dcdc4e55b2) but every
`ICvGame1::SetTimeStr(char *, int, bool)` failed on the server with "bad arguments" (status 17; vpproxy.log), so the
autosaves were named `AutoSave_Post_.Civ5Save` without their turn and the runner saw "no new turn for 600 s" (exit 4)
while the game played to turn 157. The schema hash does not cover that method's wire. S4x was changed to carry the b1
proxy and helper (commit b3a23c562).

**Identity of the option off** `[L 2026-11-17 01:05-01:11, workstation, c5h-b, cloak launch, S4x:trace = the RC layout with
b1's server, proxy and helper, option unset, M from TS0438-M-T150 by hash, statistics logging on]`: the run played
151-156 (exit 0, 25 `Update`s per turn, 11.06 s/turn: an indication); `perf/evidence/0438/idtrace.py 150 154` against
4657's S4:trace run (the RC itself, `F:/c5-64bit-work/0437/4657-ws/runs/ws-S4-trace/stats.db`): **IDENTICAL, 15 tables,
14,398 rows**. So the b1 build with the option off plays the RC's game.

**The deadlock** `[L 2026-11-17 01:14 (S4x:trace, option 25) and 01:30 (S4x without the trace, option 25), c5h-b]`: both on
runs stopped in turn 150 right after autoplay was armed; server.exe idle (CPU flat), the EXE drawing frames.
`ai-per-update.tsv` of the first: 51 requests of 1 `Update` each before the arm (stops `paused` x6 during the load, then
`human_active` x45: the conditions held the batch back as designed), then the first AI request never ended.
Non-invasive cdb stacks (`cdb -pv`; excerpt `perf/evidence/0437/4660/deadlock-stacks.txt`, full dumps and run folders `F:/c5-64bit-work/4660/logs/`):
1. server core thread: `Dispatch_ICvGame2` (the served `Update`) > `SrvRt::LeaveUpdate` > `SrvAiBatch::RequestEnd` >
   `ExtraDllUpdate` > the DLL > `RemoteGen_ICvEngineScriptSystem1::CallTestAll` > `SrvRt::End`: waiting for the EXE to
   run a GameEvents Lua hook, while holding the server's game-core lock (`s_csCore`, taken for the EXE core thread's
   game wrapper by `LockSync` for the whole served call);
2. EXE core thread: `Proxy_ICvGame2::Update` > serving `CallTestAll` > the EXE's script system > `EnterCriticalSection`
   (a lock the main thread holds);
3. EXE main thread: inside `lua_cpcall` (UI Lua run from the frame's job processing) > a Lua binding >
   `Proxy_ICvGameContext3::GetGame` > `ProxyLocal::Answer` > `LockEnter` > `kRt_LockSync`: waiting for the server;
4. server ui thread: `OnCall` > `SrvRemote::LockSync` > `EnterCriticalSection(s_csCore)`: waiting for thread 1.
A cycle. At 01:24:04 (10 min) the proxy timed out `rt:LockSync`, wrote the D119 report (stage `server-death`, code
`Timeout`, `perf/evidence/0437/4660/d118-report.txt`) and started the error popup; the game ended. The fail-loud path works.

**Why stock does not deadlock** `[I]`: the main thread's UI Lua that reads the game runs between `Update` requests
(issue 2805: 98.3 % of UI-Lua calls in AI turns are made with no `Update` in flight); the EXE's pacing (the tick, the wait
for queue B) is exactly what keeps that window open. Option 1 removes the windows: the UI Lua that the DLL's events of
player p triggered then runs while the batch plays player p+1 under the lock, and the first GameEvents hook of that
`Update` closes the cycle. So the "EXE's share" of an AI turn is partly the time the main thread needs the game for its
own Lua, and a server-side batch cannot remove it without changing who may hold the game-core lock.

**Consequences for the other options** `[I]`:
- Rank 4 (the DLL loops inside one `CvGame::update`) holds the lock just as long: expect the same deadlock.
- Ranks 2 and 3 (proxy: mark the player change as seen; a shorter tick in AI turns) keep one DLL `Update` per request and a
  gap between requests, so the main thread keeps its windows; they shorten the gap, which raises the share of UI Lua
  made with an `Update` in flight (stock: 0.3 %). Whether that is safe is a live question for whoever builds them: count
  `proxy-lua-calls.tsv`'s `in_flight` phase.
- A server-only variant of option 1 that yields `s_csCore` between DLL `Update`s (as `kRt_CoreLockYield` does for the
  harness chunk) only helps when the main thread is already waiting at the yield; UI Lua that starts during a DLL
  `Update` still closes the cycle. Yielding during every made call of an extra `Update` would let UI Lua read the DLL
  mid-`Update` (shared caches such as the pathfinder, CvAStar's own lock use): not safe without a study.

**The option path for a product version** (not done; for the lead and the MC): `[Performance] AiPlayersPerUpdate`
(default 1, range -1..64) in `options/src/vp64_options.cpp` `kDefs` and `options/include/vp64_options.h` (shared with the
proxy: the proxy needs a rebuild only for its own warnings and the default file's text; it already writes
`options_file=` into server.cfg), `docs/OPTIONS.md`, `research/07-architecture-decisions/options-file.md` section 2;
`SrvAiBatch::Init` reads `SrvOptions().Get(kAiPlayersPerUpdate)` instead of the environment. Under D89's rules a bad
value becomes a warning and the default, not a refusal; the runner variant then writes the instance's `VP64Options.ini`
(backed up and restored by hash) instead of the environment. Moot while the deadlock stands.

**The laptop job (prepared, NOT submitted; not recommended)**: from this branch's HEAD,
`python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4x,S4x:app25 --runs 6 --order
williams --quiet --grade A --issue 4660 --series-id 4660-M-app25 --label "4660 D165 ai_players_per_update off-on"
--timeout 14400` (`--print-only` checked every shipped hash, 2026-11-17 00:45). Every `S4x:app25` run would hang for
10 min and end in the D118 popup on the laptop's screen; expected length otherwise about 2 h (12 runs of about 10 min
with 60 s gaps).

**Incident** `[L]`: the product's error popup of the first on run (vp64helper, "Vox Populi 64-bit patch: error report",
pid 33292) was visible on the workstation's desktop from 01:24:04 until impl-4660 closed it with `WM_CLOSE` at about
01:25 (no kill); the harness's cloak covers the game window, not the helper the proxy starts. `cli.py dismiss` refused
(the EXE had ended). Whether it took the focus was not checked.

#### Not verified (4660)

- The identity of the option ON: no on run completed a turn; the windowed statscmp pair (D164) was not obtained.
- Which UI script's Lua held the main thread (the stack names only `lua_cpcall` from the EXE's job processing and a Lua
  binding calling `GetGame`); which EXE lock the core thread waited on (inferred: the Lua VM's).
- A headless host (c5h) with the option: it has no UI Lua of this kind, so it would likely not deadlock; it says nothing
  about the client and was not run.
- The DLL frames in the server's stack dump (the PDB beside it is from a different build of the same source); only the
  server's and the proxy's frames are read.

### The pacing poll: U, D_B and T_tick measured (ticket 4661)

impl-4661, 2026-11-17 00:18-00:56 EST, workstation. Text for `research/08-performance/turn-time-study.md` (this
subsection "The pacing poll (ticket 4661)" under "What paces an AI turn in the client (D165)"). Values and addresses
only, nothing from the EXE (D83). Marks: `[C]` read in the EXE (4656), `[L]` seen live, `[I]` inferred.

#### The instrument

- `perf/turnstudy/pacing/poller.py`: a separate Python process that opens the EXE with `PROCESS_QUERY_INFORMATION |
  PROCESS_VM_READ` only and reads, about every 1 ms (QPC times; a high-resolution waitable timer; its own sampling
  thread above normal priority), 4656's values (`update-pacing.md` 5): update pending `[[0x2DD202C]+0x40]`, the
  accumulator float `[0xCCF91C]`, queue B's dispatch and write halves (`0x2A10E08 + 0x4C*[0x2A10200]`, flip `+0x20`),
  the frame's accepted player `[[[0xCC8E74]+0xC0]+0x8CC]` ("seen"), the AI-turn byte `[0x2DE0447]`, the hold
  `[0x2DE044C]`, and two turn words `[0x2DE0440]`, `[0xC98DE4]`; all rebased by the EXE's base - 0x400000. No write, no
  hook, no suspend, no thread handle, no administrator rights. Guards, each a non-zero exit with nothing written: 3 not
  a running 32-bit `CivilizationV_DX11.exe`; 4 PE TimeDateStamp not 0x546CD0A8, not i386, or an image too small; 5 a
  vftable check failed (thread object 0xA55BA0, GameViewState 0xA20908, engine utility 0x9EA718); 6 a start probe of
  300 samples more than 5 % out of range. While running it follows a moved thread or GameViewState object (re-checked
  every 250 samples) and ends early (exit 7, data kept and flagged) on 200 consecutive insane samples or a freed thread
  object. Writes `pacing-samples.tsv.gz`, `pacing-run.json` (layout, intervals, its own CPU, end) and
  `pacing-periods.tsv`.
- `perf/turnstudy/pacing/pacing.py`: a period runs from one request (pending 0 -> 1) to the next. **U** = request to
  the Update's end (pending 1 -> 0); **D_B** = the Update's end to the gate's opening, the first sample where `seen`
  changes (the frame takes the next player only when queue B is empty); **T_tick** = the rest, to the next request.
  U + D_B + T_tick = the period exactly. Also ticks (accumulator resets), lost ticks, frames (accumulator steps), frame
  rate, `drain_s` (to the first empty queue B), whether a request came on a tick and whether queue B was empty at the
  next request. Turns from 4657's trace (requests matched to `GameCoreUpdateBegin` rows) or from the run's
  PlayerDoTurn clock put on the poll's clock by the run's channel tie. Exits 0; 2 usage or no samples or no turn source;
  3 periods per turn differ from `--expect-periods`; 4 a window turn missing or not closed; 5 the poll not trustworthy
  (ended early, a sampling gap over 50 ms).
- Runner: variant key `pacing_poll` (variants `pacing`, `pacing_trace` in M and H; off by default, an instrument, never
  in a timed series): `clientarm.py` starts the poller with no console window once the game is in (before the arm),
  fails the run before the arm if the poller refuses, and stops it by its stop file after autoplay's hand-back; the
  poller's pid counts as the job's own CPU (STEP_PIDS). Tests `perf/tests/turnstudy/test_pacing.py`, 44 (a fake process
  image, synthetic samples, a fake poller; the only real process touched is the test's own, as the wrong-process case);
  the turnstudy suite 155 pass.

#### Workstation check `[L, 2026-11-17 00:46-00:51 EST, workstation, shared (six heavy jobs of other owners registered), c5h-a, window cloaked (the harness's no-focus launch), S2i:pacing_trace (the poller and 4657's trace), M from TS0438-M-T150 by hash, window 151-153, statistics logging on (client database 4e4620b2), one run: grade C, an indication, no claim]`

`perf/turnstudy/pacing/wscheck.py c5h-a S2i:pacing_trace --out ... --turns 3`: run exit 0, restored and verified
(`provision.py verify` rc 0), no dialog; the poller exit 0 (stop file), 77,000 samples in 78.5 s, interval median 1.04
ms, p99 2.24 ms, max 4.5 ms; the poller's own CPU 11.6 % of one core. `pacing.py --expect-periods 25`: exit 0.

- **25 periods per M turn** (151, 152, 153), the same as the trace's 25 steps; the trace's B rows matched 137 of 143
  requests (95.8 % of the B rows in the poll's span; the rest before the trace's install), and the PlayerDoTurn-clock
  mapping gave the same turn for all 137 [L]. U against the trace's Update (B to E): median +0.4 ms, p90 |diff| 2.1 ms:
  the poll's edges are the trace's Updates [L].
- **Every request came on a tick** (75 of 75 within 3 ms of an accumulator reset) [L], confirming 4656 1 [C].
- **The gate is the drain of queue B** [L]: queue B emptied 0-5 ms (median about 1 ms) before the frame accepted the
  next player, in all 57 periods where the poll saw it empty; in the others it emptied and refilled within a sample. The
  frame that accepts the next player sets the AI-turn byte to 1 and posts a record (464 bytes in the write half) that is
  still there at the next request in 69 of 75 periods: queue B being empty is a condition of the gate only, not of
  the request.
- **Per turn (mean of 3, seconds of 12.39 per turn): U 5.28 (43 %), D_B 5.92 (48 %), T_tick 1.20 (10 %)** [L, indication].
  D_B is the largest part, as 4656 inferred, but U is close behind on this loaded machine; T_tick is small (median 0.044
  s per period, 0-0.105 s; 7 of 75 periods 0: the queue emptied on a tick frame). 3.7 ticks per period on average, 2.7
  of them lost while the gate or the Update held the request. Frame rate 43.8 fps (dt mean 17.5 ms).

| Player class (seen at the request) | periods per turn | median P / U / D_B / T_tick (s) | per turn U / D_B / T_tick (s) | frames, ticks per period (median) |
|---|---|---|---|---|
| majors (1-7) | 7 | 0.940 / 0.360 / 0.486 / 0.042 | 2.40 / 3.66 / 0.29 | 38, 6 |
| city states (22-37) | 16 | 0.125 / 0.022 / 0.076 / 0.048 | 0.38 / 1.50 / 0.81 | 7, 1 |
| barbarians (63) | 1 | 0.331 / 0.071 / 0.237 / 0.037 | 0.07 / 0.29 / 0.04 | 21, 3 |
| turn change (seen 64: no turn-active player) | 1 | 2.824 / 2.336 / 0.472 / 0.026 | 2.42 / 0.47 / 0.05 | 118, 19 |

- **4657's "0.42-0.54 s waits after long Updates" are the drain**: periods with U >= 50 ms (29) wait D_B 0.436 s and
  T_tick 0.042 s (medians); periods with U < 50 ms (46) wait D_B 0.076 s and T_tick 0.047 s [L].
- **What the amelioration ranks could remove here** [I, from this one run]: rank 1 (several AI players per request)
  the 24 per-player gaps D_B + T_tick, about 6.8 of 12.4 s per turn, minus the drain work the main thread still has to
  do (queue B is pumped only while no Update is pending, so a longer Update batch defers it; the picture runs further
  behind); rank 2 (mark the player change as seen) D_B, about 5.9 s, with the same caveat; rank 3 (a shorter tick)
  T_tick, about 1.2 s.

##### Live facts about the values `[L, same run and the first check, 2026-11-17]`

- `GameViewState+0x8CC` ("seen") takes the next turn-active AI player in slot order (1-7, 22-37, 63) and **64** at the
  end of a turn (no turn-active player; the turn-change Update starts with 64).
- `[0x2DE0440]` and `[0xC98DE4]` are not plain game turns: after a load at turn 150, `[0xC98DE4]` was 150 and
  `[0x2DE0440]` -1 until that turn's change, then `[0xC98DE4]` -1 and `[0x2DE0440]` 150. The poller records them; the
  analysis uses them only when they behave as turns.
- AI-turn byte `[0x2DE0447]`: 0 in the human's turn; in autoplay 1 from the acceptance of a player to the end of its
  Update, 0 from the Update's end to the next acceptance.
- The hold `[0x2DE044C]` stayed 0 (no leader message in 151-153).

##### The first check `[L, 2026-11-17 00:36-00:43 EST, same set-up]` (negative, kept in `ws1-S2i-pacing_trace-seen64/`)

The poller passed its guards live (EXE at 0xC70000) and then ended itself after 28.6 s (exit 7) at turn 150's change,
because `seen` read 64 (its range was -1..63); its sampling thread at normal priority had p99 11.5 ms, max 50 ms
between samples on the loaded machine. Both fixed (64 allowed; the thread above normal: p99 2.2 ms in check 2). Its
38 periods showed the D_B rule above (a "queue B empty until the request" rule gave T_tick = 0 for most AI periods).

#### The laptop job (ready, NOT submitted)

From this branch's HEAD (the testbed ships the checkout's HEAD; `tools/harness/native/build.bat` built in the
submitting checkout):

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S2i:pacing,S4:pacing --runs 1 --turns 5 --quiet --keep-going --no-rerun --grade D --label "4661 pacing poll" --issue 4661 --series-id 4661-M-pacing --timeout 3600
    python -I -X utf8 tools/testbed/tb.py --host laptop fetch JOB --out F:/c5-64bit-work/0437/4661-laptop-JOB --all
    python -I perf/turnstudy/pacing/pacing.py F:/c5-64bit-work/0437/4661-laptop-JOB --expect-periods 25

Two client runs of 7 autoplay turns with the 60 s idle gap each: about 15-20 min (4657's four such runs took 30-40
min). `--print-only` checked every binary and the save on 2026-11-17 00:47. The variant `pacing` runs the poller
without 4657's per-frame Lua trace (turns from the PlayerDoTurn clock through the tie); `S2i:pacing_trace,
S4:pacing_trace` adds the trace for a cross-check at the cost of a Lua call per frame.

#### Not verified

- Everything above is one workstation run, loaded and cloaked: the laptop's split (foreground, about 0.62 s per Update)
  may differ; the laptop job decides it. S4 was not run with the poller (only S2i).
- The poller's own effect on the game: 11.6 % of one core at above-normal priority; not measured against a run
  without it.
- That the 464-byte record posted at the acceptance is `AIProcessingStartedForPlayer` (or which event it is).
- The meaning of `[0x2DE0440]` and `[0xC98DE4]` beyond the one turn change seen.
- Whether a leader message (the hold) shows as expected: none happened.
- Edges are timed at the first sample showing them (about 1 ms error); two frames inside one sample count as one.

### Option 3 built: a shorter update tick in AI turns (ticket 4662, D165)

impl-4662, 2026-11-17 02:20 onwards, workstation (build, offline tests) and the testbed (live runs). Design:
`d165-pacing-fix-design.md` sections 3-7 (option 3 only; option 2 not built, MC #3503). Code: `proxy/src/proxy_pacing.{h,cpp}`
(hooked in `proxy_update.cpp` BeforeUpdate and `proxy_rt.cpp` DetachOnUnload), the option in `options/`, the offline
test `proxy/tools/rt_tests/pacing_unit.cpp` (runner `perf/evidence/0437/4662/run_pacing_unit.py`). Evidence:
`perf/evidence/0437/4662/`. Values and addresses only, nothing from the EXE (D83). Marks: `[C]` read in the EXE file or
our source, `[L]` seen live, `[I]` inferred.

#### What it does

- **The option** `VP64Options.ini [Performance] AiUpdateTickMs`: -1 = stock (the default; the proxy reads and writes
  nothing of the EXE and logs one line `pacing: off ...`), 0..100 = the tick in milliseconds in AI turns. Parse errors
  and values out of range give the default with one warning (the options loader's rules). `AiSkipVisualDrain` (option
  2) is not reserved: the table does not need it.
- **The install** (first `ICvGame1::Update` of a proxy load with the engine utility known, on the game-core thread):
  PE TimeDateStamp 0x546CD0A8, machine i386, SizeOfImage covering the layout; getSecsPerTurn 0x704B60 and the call site
  0x7258C1 in the executable section; the vftable 0x9EA718 in a read-only data section; g_Civ5App 0xCC8E74, the AI-turn
  byte 0x2DE0447 and the hold 0x2DE044C in a writable data section; `[[g_Civ5App]+0x1460]` equal to the engine utility
  the EXE gave the DLL; its vptr the EXE's vftable (or a copy an earlier proxy load left: taken back first, issue 3308);
  the entry before slot 0 is 0 or a data pointer of the image; slots 0-263 in `.text`; slot 102 = getSecsPerTurn; the
  entry after slot 263 not a code pointer. Then a copy (header "VP64TICK", the original vptr and slot, the RTTI entry
  and 264 slots) on its own page, made read-only, slot 102 = the proxy's function, and one interlocked
  compare-exchange of the object's vptr. Every other slot stays the EXE's own function.
- **The replacement** (a `__fastcall` stand-in for the `__thiscall` float method): the option's tick when the return
  address is the rebased 0x7258C1 (see the correction below), the thread is the EXE's main thread (the process's earliest-created thread, found
  once with Toolhelp at the install and logged), the AI-turn byte is exactly 1 and the hold is 0; else the EXE's
  function through the saved entry. The hold condition is an addition to the design's three: under the hold no
  `Update` runs, so a faster tick would only wake the core thread every frame [I].
- **Each `Update`**: g_Civ5App's engine utility still the object, the vptr still the copy; the vptr back to the EXE's
  own = installed again (logged); another engine utility from the EXE = the old one put back, the new one installed.
- **Unload** (`DetachOnUnload`): the vptr put back when it is still the copy, the copy's slot 102 the EXE's again; the
  copy's page stays mapped (4 KB per proxy load), so a thread that read the old vptr an instant before still calls EXE
  code.
- **Fail loud** (MC #3503): a failed check at the install = a loud `pacing: FAILED check <name> at the install` line,
  the D118 report (stage `pacing`, code `install-<check>`, through `ProxyReport::Write`) and the helper's popup
  (`vp64helper popup`, the stock-session verb: a pacing failure does not switch the patch off); a failed check later =
  the loud line and the report, no popup. In both cases the vptr goes back and the game goes on with the EXE's own
  tick (the stock game). One report per proxy load. `proxy_rt.cpp`'s own `ReportFailure` is private to that file and
  claims the 64-bit path's report flag; the pacing report does not share that flag (a later failure of the 64-bit
  path still gets its own report) [C].
- **Counters** in vpproxy.log every 60 s while active and in the unload's line: frame-site calls (shortened, stock),
  frame-site calls on another thread, other callers, checks, installs.

#### The vftable's length `[C, 2026-11-17 02:25, the Steam EXE file CivilizationV_DX11.exe SHA-256 b2ce96ae...0c88e, read with a scratch Python script, nothing kept]`

Slots 0-263 (VA 0x9EA718-0x9EAB34) all point into `.text` (VA 0x401000-0x9D5D30); the entry before slot 0 (VA
0x9EA714) is **0**, not an RTTI locator (the design's "RTTI entry at index -1" is that 0, copied as it is); the entry
after slot 263 (VA 0x9EAB38) is not a pointer into the image. So the table is 264 entries, as `addresses.md` row 0x9EA718
says (260 interface slots and 4 more). The install checks the same on the live object every time.

#### Offline tests `[C, 2026-11-17 02:50; counts of 04:20 at 14e238d12, workstation]`

- `pacing_unit`: **95/95** with the proxy's toolchain (clang-cl -m32, VC9 headers, /O2) and **95/95** under MSVC
  /fsanitize=address x86 (the canary's heap-buffer-overflow caught first). A fake EXE (a reservation of the real
  SizeOfImage with the real section layout and only the read pages committed; getSecsPerTurn's place a 5-byte jump to
  the test's stand-in returning 0.1): off reads nothing (every page PAGE_GUARD, still guarded after 5 Updates); each
  VerifyExe and install check refuses (boot report, nothing written); the swap and the copy's contents; the four gates
  each negative; the re-check cases (the EXE's vptr again, a new object, the app's pointer changed, someone else's vptr,
  an access fault); the unload's restore, a second load after it, and a second load after an unload without restore.
- **Negative result worth keeping:** clang-cl's x86 SEH catches an access violation only at a call inside the `__try`
  body, not in an instruction of the body itself. The plain build crashed (0xC0000005) in the decommitted-vftable case
  where MSVC's build caught it; the guarded reads now sit in `__declspec(noinline)` functions called inside `__try`, as
  `proxy_leaderscene.cpp` does with WalkRaw [L, both builds].
- `options/tests/test_options.cpp`: 200/200 in x86 and x64 (`ipc/build.py --arch both --test --target test_options`)
  and under MSVC ASan x64 (`options/test_asan.py`).
- `proxy/tools/test_proxy_rt.py` (the proxy's existing rt runner: unit, loader, shell, guard): **107/107** on build b2
  and on build b4.
- The turn-study runner suite (`perf/tests/turnstudy`): **172 pass** (new: `test_tickoption.py` 8; the poller's copy
  case in `test_pacing.py`).

#### Correction: the frame's call returns to 0x7258C1, not 0x7258BF `[C, L, 2026-11-17 03:52]`

The first live run with the option on installed cleanly (every check passed on the real EXE) and **shortened nothing**:
0 frame-site calls, about 36 "other callers" a second, the frame rate. A tally of the other callers' return addresses
(now part of the counters line) showed all of them at **EXE+0x3258C1**. In the EXE file the frame loads the engine
utility object from g_Civ5App (+0x1460), takes slot 102 (+0x198) of its vtable and calls it with a 2-byte indirect call
**at 0x7258BF**, so the return address is **0x7258C1** (D83 edit 2026-11-25, 0772: the frame's instruction sequence
was replaced by this description). 4656 had written the call's own address as the
return address; `addresses.md` row 0x704B60 and the design's section 3 are corrected in place. The offline test could
not catch it: it measures its own call site. In the 10 turns no other code called getSecsPerTurn through the EXE's
object (other callers 0 after the fix).

#### Live functional checks on the workstation `[L, 2026-11-17 03:21-04:15 EST, workstation (shared: 4-6 heavy jobs of other owners registered), c5h-b, window cloaked, M from TS0438-M-T150 by hash, window 151-160, statistics logging on, arm S4kp, one run per arm: functional and an indication only (D106), no timing claim]`

- **Why on the workstation and why S4kp:** oldpc had no logged-on session until about 04:15 (`tb.py status`:
  headed=held) and the laptop was in 4653's quiet series with three quiet jobs queued. S4kp is build b4 by `provision.py
  set-proxy` (pack=0, provision's vpproxy.ini, the instance's own DLL as the fallback, **no vp64helper.exe beside the
  proxy**): neither the proxy's D118 path (`ReportFailure` needs pack=1), nor the pacing report (`StartHelper` finds no
  helper), nor the server's exe-crash path (the helper beside the proxy) can start a popup there. It is not the product
  path; the S4 pair runs on a test machine (below). Driver: 4661's `perf/turnstudy/pacing/wscheck.py c5h-b
  S4kp:pacing_tick_off|S4kp:pacing_tick0 --turns 10` (the poller in both runs); every run restored, `provision.py
  verify` rc 0, no dialog, no report folder.
- **The poller and the option:** 4661's poller refused the first on run (its guard wanted the vftable 0x9EA718 on the
  object). It now accepts the proxy's copy when the copy's header names the rebased original and records `eu_vtbl` exe
  or copy (`perf/turnstudy/pacing/poller.py`, tested).
- **Identity:** build b4, AiUpdateTickMs -1 against 0: `statscmp.py --from-turn 150 --expect-turns 150 162`:
  **15 tables, 39,246 rows compared: IDENTICAL** (`perf/evidence/0437/4662/statscmp-ws-b4-off-vs-on.txt`). Also
  build b2 off against the b2 on run whose site never matched (the vptr swapped, every call through the copy and on to
  the EXE's function): IDENTICAL, 39,246 rows.
- **The option acts as designed:** in the on run's vpproxy.log the install line names the main thread (the process's
  first) and the counters at 245 s read frame-site calls 8,181 (shortened 3,099, stock 5,082), another thread 0, other
  callers 0, checks 537 (one per Update), installs 1; no failure line.
- **The poll** (`pacing.py --expect-periods 25`, 25 periods in every turn):

| build b4, S4kp | s per turn | U | D_B | T_tick | median T_tick per period | ticks per period (lost) | frame rate |
|---|---|---|---|---|---|---|---|
| AiUpdateTickMs -1 | 13.50 | 3.97 | 7.71 | **1.82** | 0.084 s | 3.73 (2.73) | 36.5 fps |
| AiUpdateTickMs 0 | 11.39 | 3.88 | 7.51 | **0.002** | 0.000 s | 2.47 (1.60) | 24.8 fps |

  T_tick goes from 1.82 s to 0.002 s per M turn: the next request comes on the first frame after the frame accepts the
  next player [L, one run each, indication]. The turn time fell by 2.1 s, about T_tick; U and D_B moved by less than
  their run-to-run spread on this machine. The off run's poll had two sampling gaps over 50 ms (64 ms; `pacing.py`
  exit 5, the analysis kept); the on run none. *Corrected 2026-11-17 04:50 by impl-4663: the on run's "24.8 fps" is
  not a frame rate. With a threshold of 0 every frame of the AI-turn phase adds its dt and resets the accumulator
  within itself, so the poll, which counts accumulator changes, sees none of U's frames; the proxy's own frame-site
  counter in the same run gives **49.1 fps** [L]. Details: subsection "The frame rate with AiUpdateTickMs = 0" below.
  (Replaced: "The lower frame rate of the on run is not explained: a request every frame in AI turns, or the shared
  machine [I]; the laptop series must report the frame rate per arm.")*
- Earlier runs kept for the record: b2 off 11.54 s per turn with T_tick 1.68 s; the b2 on run (no site match) 15.74 s
  per turn with T_tick 2.16 s (median 0.105 s, the full stock tick: the swap alone changes nothing).

#### On the test machines `[2026-11-17 04:19 EST]`

- **oldpc, the S4 identity pair and the poller runs (submitted, waiting):** job `20261117-041911-6962` (commit
  14e238d12): `submit.py run --host oldpc --scenario M --arms S4k:tick_off,S4k:tick0,S4k:pacing_tick_off,S4k:pacing_tick0
  --runs 1 --order fixed --turns 10 --keep-going --no-rerun --grade D --issue 4662 --series-id 4662-M-identity`, queued
  behind oldpc's quiet job 0450 and the jobs 0461 and 0470. S4k is the RC layout with build b4's server, proxy and
  helper (pack=1: the product path). To read it: `tb.py --host oldpc fetch 20261117-041911-6962 --out
  F:/c5-64bit-work/0437/4662-oldpc --all`, then `statscmp.py <tick_off run>/stats.db <tick0 run>/stats.db --from-turn
  150 --expect-turns 150 162` and `pacing.py <run> --expect-periods 25` for the two poller runs. Not verified: that the
  turn-study runner works on oldpc (Python 3.8: the runner and poller parse with the 3.8 grammar; no run there yet).
- **Result of the oldpc pair** `[L, 2026-11-17 07:07-07:26 EST, oldpc (Windows 10 19045), instance tb1, the job alone
  (slots 2), job 20261117-050102-fa54 from 13f4eb15a with natives built there; the first submission 041911-6962 failed
  at launch on the memory floor beside 0461's EXE job, nothing of the patch]`: all four runs exit 0, 10 of 10 turns.
  **S4k tick_off = tick0 IDENTICAL** (statscmp from turn 150, 15 tables, 39,246 rows) on the product path (pack=1, the
  proxy's log: `options: [Performance] AiUpdateTickMs = 0 (from the file)`, `pacing: on: ... a copy of the EXE's
  vftable 0x0135a718 (264 entries and the RTTI slot, checked)`, counters active, no D118 report; the off arm logs
  `pacing: off ... nothing installed`). Turn times (one run each, functional job, an indication only): tick_off 9.81,
  tick0 8.64 s per turn; the poller runs 9.97 and 8.69. `pacing.py` (25 periods): T_tick median per period 0.048 s off
  -> 0.000 s on; D_B unchanged (U < 50 ms periods 0.091 -> 0.090 s, U >= 50 ms 0.522 -> 0.535 s); the on run's poll
  fps flagged blind as 4663 says. Fetched to `F:/c5-64bit-work/0437/4662-oldpc`.
- **The laptop timing job (submitted 2026-11-17 05:34 as `20261117-053434-a9a8`, planned end 08:05; from 13f4eb15a
  with pre-adcc99d1f natives, the 0486 workaround, planned length as `TESTBED_EXPECT_MIN=150`):** from a checkout at 14e238d12 or later with the harness natives
  built:

      python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4k:tick_off,S4k:tick0 --runs 6 --order williams --quiet --grade A --series-id 4662-M-tick --label "4662 AiUpdateTickMs -1 vs 0 (D165 option 3)" --issue 4662 --keep-going --timeout 14400

  12 S4 runs of 27 autoplay turns (window 151-175), Williams order, 60 s idle gap each: about 2 h 15 min (4653's
  series: about 26 min per round of four arms, S4 at 15.5 s per turn; the on arm about 1-2 s per turn shorter). GPU
  drivers on the laptop: Intel HD Graphics 630 27.20.100.9320 and NVIDIA Quadro P3000 32.0.15.7322 (the run records
  name only the first adapter: which one renders is not recorded). Report T_tick, the frame rate and the turn time per
  arm; the poller stays out of the timed series.

#### Result of the laptop timing series `[L, laptop, 2026-11-17 05:35-07:40 EST, job 20261117-053434-a9a8, series 4662-M-tick, 6 Williams rounds of S4k:tick_off and S4k:tick0 (12 runs), quiet, alone; build b4 (server FC36E577, proxy 14941D2C, helper 51A0311C) in the RC's layout, pack=1; M from TS0438-M-T150, window 151-175; GPU drivers Intel HD Graphics 630 27.20.100.9320 and NVIDIA Quadro P3000 32.0.15.7322; grade A by PERFORMANCE.md's treatment exception: one build, one setting differs, the same game]`

- **S4k:tick0 - S4k:tick_off: -1.459 s per turn, -9.55 %, 95 % CI [-1.534, -1.383]** (Welch on run means, v 9.6;
  ratio 0.905 [0.900, 0.909]). Means: tick_off 15.277 s (cv 0.34 %, CI [15.222, 15.331]; the M series' S4, the RC,
  was 15.266), tick0 13.818 s (cv 0.47 %, CI [13.751, 13.886]). CPU per turn -1.00 s (-4.1 %); server CPU 5.88 -> 4.97
  s per turn; peak private unchanged (+5 MB, CI crosses 0); EXE growth +0.35 MB/turn (CI +0.02..+0.68, description).
- **The game is identical**: each arm's 6 runs IDENTICAL among themselves (`analyze.py`), and tick_off = tick0
  IDENTICAL (statscmp r01-p1 against r01-p2 from turn 150: 15 tables, 99,202 rows).
- **Why the numbers moved**: the option removes T_tick, the wait for the EXE's 0.1 s update tick after each `Update`
  (25 per M turn, one per AI player; this section's "What paces an AI turn"); the oldpc pair's poll shows T_tick 0.048 ->
  0.000 s per period and D_B unchanged, and 4663 showed the frame time per phase unchanged on the workstation. 1.46 s
  per turn is 58 ms per period, about half of the stock tick on average, as a uniform wait for the next 0.1 s tick
  predicts [I].
- **Frame rate**: the on arm's proxy frame-site counter gives 38.5-39.1 calls per second over the first about 300 s of
  play in each of the 6 runs (the counter lines stop after that in the log); the off arm installs nothing and has no
  counter, so this series gives no on-against-off frame rate. The poll is not used (4663).
- What remains of the EXE's share: D_B (the drain of queue B after each AI player), 57-66 % of a turn by 4661's poll.
- Fetched to `F:/c5-64bit-work/0437/4662-laptop-a9a8` (records rewritten to R19 ids, copied to `F:/c5-64bit-work/0437`).

#### Not verified

- The D118 popup path of a real failure on the product path (the pack=1 layout itself is checked: oldpc pair above, 2026-11-17).
- A turn with an AI leader message (the hold) and a human turn with a unit move: none in 151-160 (autoplay, no hold
  seen); the stock answer while the AI-turn byte is 0 or the hold is set is shown offline only.
- A second proxy load in one process (3308) and an exit to the menu live: covered offline only.
- ~~Whether the frame rate drop in the on run is caused by the option.~~ Answered by 4663 (2026-11-17): there is no
  drop; the poll cannot count frames with the option on (next subsection).
- The workstation numbers above: one run per arm, grade C at best (the laptop series is the grade A result).

### The frame rate with AiUpdateTickMs = 0 (ticket 4663, D165)

impl-4663, 2026-11-17 04:31-04:50 EST, from the data of 4662's workstation pair only (build b4, S4kp, c5h-b, M from
TS0438-M-T150, window 151-160, statistics logging on, shared workstation, window cloaked, one run per arm; raw samples
`F:/c5-64bit-work/4662/ws4-runs/runs/ws-S4kp-pacing_tick{_off,0}/`). No new game run: the existing data explain the
number. Evidence and the scratch scripts: `perf/evidence/0437/4663/`. Marks: `[L]` read from the live runs' data,
`[I]` inferred. Grade C (one loaded pair, PERFORMANCE.md): indications, no timing claim.

**In short: the frame rate did not fall. The poll's "frame rate" is a count of accumulator changes, and with a
threshold of 0 the frames of the Update phase do not change the accumulator. The proxy's own frame-site counter in the
same on run gives 49.1 fps; the off run's poll gave 36.5 (about 40 corrected, below). The option removes the tick wait
T_tick; the larger part of the EXE's share, the queue-B drain D_B, is untouched.**

#### Why the poll goes blind `[L]`

- The frame adds its dt to the accumulator and resets it to 0 at the threshold, with or without a request
  (`perf/evidence/0437/4656/update-pacing.md` 1, 3). The poll counts a frame when the accumulator changes between two
  samples (an increase, or a drop at a tick). With the option's 0 while the AI-turn byte is 1 (from the frame's
  acceptance of a player to the end of its `Update`, so U), every frame goes 0 -> dt -> 0 inside itself: the poll
  reads 0 throughout.
- Per phase (`phase-frames.txt`; "seen" = accumulator changes per second of the phase, both arms, 10 turns):

| phase | off: s per turn | off: seen per s | off: acc = 0 | on: s per turn | on: seen per s | on: acc = 0 |
|---|---|---|---|---|---|---|
| U | 3.968 | 48.7 | 21 % of samples | 3.881 | **2.7** (10.6 per turn, all of them drops) | **100 %** |
| D_B | 7.714 | 35.3 | 20 % | 7.509 | 36.0 | 21 % |
| T_tick | 1.820 | 14.9 | 2 % | 0.002 | (too short) | |
| turn | 13.502 | 36.5 | | 11.392 | 24.8 | |

  In D_B (AI-turn byte 0: the EXE's own 0.1 s) the two arms see the same rate (35.3 against 36.0 a second; mean dt of
  a seen frame 0.0199 against 0.0200 s; per class majors 31.6 / 32.7, city states 42.4 / 43.2, the turn change 31.3 /
  29.5). The on arm's 24.8 is D_B's frames spread over a turn of which a third (U) shows none: 270.6 + 10.6 seen
  per 11.39 s.
- **The true count, from the proxy** (`proxy-counter-vs-poll.txt`): the on run's vpproxy.log counts every call of
  slot 102 from the frame's call site (one per frame that reaches the tick test). Its clock was put on the poll's by
  the `Update` counts (the counter's checks, one per `Update`, against the poll's requests: an offset of 87.22-87.51 s
  matches both 61 s spans exactly, 139 and 126 `Update`s). Over 121.65 s of AI turns (proxy 124.0-245.7 s, poll
  36.65-158.30 s): **5,971 frames = 49.1 fps** (50.7 and 47.4 in the two minutes); 2,768 shortened frames in the
  42.7 s with the AI-turn byte 1 (**64.8 a second, U**) and 3,203 stock frames in the 79.0 s with it 0 (**40.6 a
  second, D_B**). The poll saw 2,952 changes in the span: 92 % of the stock frames.
- **The other 8 %: frames with dt 0.** The accumulator moves in steps of 1/64 s (0.0156, 0.031, 0.047, ...: the
  EXE's dt has GetTickCount's 15.6 ms resolution, as 4657 noted for the Lua delta), so a frame that ends in the same
  timer step as the one before adds 0 and is invisible in both arms. Hence the off arm's true rate is about 36.5 / 0.92
  = **about 40 fps** [I: the share measured in the on arm's stock phases, applied to the off arm], against 49.1 fps on.
  The time between seen changes matches the increments (median ratio 1.00, `frames-around-acceptance.txt`): no
  other loss.
- So frames are **faster** in U than in D_B (about 65 against 41 a second): while an `Update` runs the main thread does
  not pump queue B (`../02-exe-internals/game-core-thread.md` 6.3). The lead's averaging hypothesis (slower frames in
  U) is the wrong way round: a larger U share raises the turn average. In the off arm T_tick was the slowest phase
  (14.9 seen a second: from a player's acceptance to the next visible frame takes a median 76 ms, a long frame [L];
  why it is long is not known [I: the dispatch of the record the acceptance posts]); the option removes that phase.

#### The alternatives ruled out `[L unless marked]`

- **Extra requests or empty `Update`s**: 25 periods (requests) per turn in both arms, every turn; the proxy's
  `Update` count matches the poll's requests one for one (265 in 121.65 s). The option asks only on frames the EXE's
  own skips allow; while an `Update` is pending every shortened tick is lost, as in stock.
- **The core thread resumed every frame**: no; a request needs no `Update` pending and the gate open, and there are
  still 25 per turn. Under the hold the proxy answers the EXE's 0.1 (no hold occurred in 151-160).
- **The replacement's cost**: about 49 calls a second, each a return-address, thread-id and two byte compares [I:
  negligible against a 20 ms frame]; other callers 0, calls on another thread 0.
- **The poller's cost**: 12.3 % (off) and 10.9 % (on) of one core: the same in both arms, so not the difference.

#### Removed or moved: what the option does to the EXE's share

- **T_tick is removed** [L, one pair]: 1.820 -> 0.002 s per M turn; the request now comes on the frame that accepts
  the next player (acceptance to request: median 84 ms off, 0 ms on). The turn fell 2.11 s (13.50 -> 11.39), about
  T_tick, with U (3.97 -> 3.88) and D_B (7.71 -> 7.51) both slightly lower.
- **No sign of it moving into D_B per period**: the game is identical in both arms (4662's statscmp), so the 250
  periods pair one to one (same turn and player; all 250 matched). The change of a period's D_B does not depend on the
  wait removed just before it (correlation -0.02, slope -0.06 s per s over 249 pairs); city-state periods median
  dD_B +0.8 ms, majors +25 ms (mean -21 ms) (`paired-per-turn-and-period.txt`).
- **Per turn this pair cannot decide it**: the machine's load swamps it. Turns 151-155: on - off = turn -0.32 s, U
  +0.29, D_B +1.14 (T_tick removed 1.75); turns 156-160: turn -3.90, U -0.46, D_B -1.55 (the off run's turns 157-159
  were its slowest, with its two sampling gaps). U, which the option does not touch, moves with D_B in both halves:
  load, not the option [I]. The laptop series (4662's job, 6 runs per arm, Williams order) decides the turn-level
  effect.
- **What stays**: D_B, the drain of queue B, 7.5-7.7 s of the 11.4-13.5 s turn (57-66 %), is not touched by option 3.
  Option 3 takes the tick part of the EXE's share (about 13 % of the off turn here) and leaves the drain; the frame
  rate in D_B is the same in both arms, so no main-thread work is pushed into the frames.

#### For the laptop series and later analyses

- Do not read `pacing.py`'s frame rate for an arm with the option on. `pacing.py` now gives per period `frames_U`
  and `U_acc0` and in the summary `U_acc0_share`, `frames_U_per_s` and `fps_blind` (U's accumulator 0 in 90 % or more
  of its samples), and the text report then says the frame rate is not one; its frame-rate line now reads
  "accumulator changes seen; frames with dt 0 are not" (test `perf/tests/turnstudy/test_pacing.py`
  `test_frames_in_U_and_the_blind_flag`; the turnstudy suite 173 pass, 2026-11-17). Re-run on 4662's two runs:
  `perf/evidence/0437/4663/pacing-tick{_off,0}/` (the on run flagged: acc 0 in 100 % of U, 0.0 changes a second
  seen in U).
- A frame rate per arm needs a counter that does not depend on the threshold: 4657's per-frame Lua trace
  (`Events.LocalMachineAppUpdate`, variants `trace` / `pacing_trace`) in both arms, or the proxy's frame-site
  counters, which exist only with the option on (an off arm with `AiUpdateTickMs = 100` would count frames at a 0.1 s
  tick [I: not tried; it is the stock arm only if 100 ms as a float equals the EXE's 0.1]).

#### Not verified (4663)

- That slot 102 is called exactly once per frame at the call site (the counter's rates agree with 4657's frame trace
  and with the frame times seen, but no per-frame cross-count was made).
- The off arm's true frame rate: about 40 fps is the poll's 36.5 corrected by the on arm's 92 %; it was not counted.
- Why the frame after a player's acceptance is long in the off arm.
- The turn-level split (removed against moved into D_B) beyond the per-period pairing: one loaded pair; the laptop
  series decides.

## Where the server's time goes today (W6 phase 1, ticket 4668; 2026-11-17)

Written by impl-4668, 2026-11-17 08:46-09:40 EST. Q3 of the proposal (section 4.5): what the x64 DLL spends its time
on in S3 today, so that the memory-for-speed candidates can be ranked by measurement. The ranked list itself is in
`turn-time-study-analysis.md` section 9.8. No DLL change, no prototype, no timing claim: every share below is an
indication from a shared workstation (D106). Evidence `perf/evidence/0437/4668/`; raw profiles (`folded.txt`,
`samples.json`), the runs' `stats.db`, server logs: `F:/c5-64bit-work/4668/runs/`.

### How it was measured `[L, 2026-11-17 08:56-09:07 EST, workstation, shared]`

- **Builds**: S3's binaries of the Q1 series. M: the pin layout `x64-04b1211b-pfdee656` (server `5B254430...`, x64 DLL
  `29F58E17...`). H: 0452's layout `F:/c5-64bit-work/0452/layout-new/bin` (server `FC68B75B...`, the same DLL). Both
  release configuration (the timing tools' configuration; PDBs beside the images). The runs' hashes are in
  `*-window.json`.
- **Set-up**: `perf/evidence/0437/4668/profile_s3.py` writes `server.cfg` with `headlessarm.py`'s keys (0438's identity
  database with statistics logging on, `dll_log_flags 0`, the toolkit game folder, Windows paths only: the 4664 incident),
  starts server.exe under `tools/harness/jobcap.py` (16 GB cap), and attaches `perf/sampler/sampler.py`'s `Sampler` (CPU
  mode: each walked stack weighted by the cycles its thread used since the last tick) when the server's own
  `turn-times.tsv` line of the last untimed turn appears; it stops at the line of the window's last turn. So the samples
  cover exactly M 151-160 and H 301-305 (boot, load and turn T are left out). Driver `run_profile.sh` (registered with
  `quietlock.py run`, owner 0437; `quietlock.py check` before and after: `*-load.txt`); attribution `families.py`
  (inclusive families from the stacks, self by leaf frame, an exclusive phase split; `families.txt`, `families.json`).
- **Runs**: M three times (m1 10 ms tick, 2,445 stack walks; m2 10 ms, 1,718 walks; m3 2 ms tick, 5,374 walks), H once
  (10 ms, 21,011 walks over 210 s). The sampler's own stack walks took 15-16 % of the sampled wall time at the 10 ms tick and 37 % at 2 ms (the target
is slowed by about this). Load: 5-7 other
  owners' heavy jobs registered at every check (games, ASan suites, builds; the m3 window overlapped a release server
  build); the sampled turns took M 1.4-3.1 s and H 38.6-51.4 s, against 1.424 s (M series, laptop) and about 21 s
  (0438's headless H run, workstation), so the load inflated the cycle counts (m1 68.7 G cycles for the 10 turns, m3
  42.7 G) and memory-bound code most.
- **The sampled runs play the arm's game** `[L]`: m1 = m2 = m3 (statscmp, 37,868 rows, turns 150-162); m3 = the M
  timed series' S3 run `r01-p4-S3` over 150-160 (32,631 rows); h1 = 0452's `s3new-H10` over 300-305 (125,210 rows), which
  is the client's game (4664). `identity.txt`.

### Shares by function family `[L, shares of the window's sampled cycles; inclusive = a stack counts once for each family on it]`

| Family (DLL function) | M m1 / m2 / m3 | H h1 | 2026-11-11 huge, old pins (turn-time-baselines.md 2) |
|---|---|---|---|
| unit AI (`CvPlayerAI::AI_unitUpdate`) | 52.6 / 53.8 / 53.4 | **44.3** | 54 |
| - homeland (`CvHomelandAI`) | 31.2 / 32.0 / 30.8 | 26.0 | 27 |
| - tactical (`CvTacticalAI`, `TacticalAIHelpers`) | 26.2 / 27.0 / 28.5 | 29.2 | |
| - dominance zones (`ProcessDominanceZones`) | 21.2 / 21.4 / 22.4 | 17.8 | 27 |
| - unit-assignment search (`FindAndExecuteBestUnitAssignments`) | 7.3 / 6.1 / 7.4 | 9.6 | 19 |
| - operations (`CvAIOperation`) | 7.9 / 6.3 / 7.5 | 6.4 | 13 |
| path search (`CvAStar::FindPathWithCurrentConfiguration`) | 42.5 / 39.0 / 42.2 | **53.3** | 41 |
| - single-unit paths (`CvPathFinder::GetPath`) | 23.5 / 21.3 / 22.5 | 10.3 | |
| - reachable sets (`CvPathFinder::GetPlotsInReach`) | 16.1 / 14.5 / 17.0 | **24.9** | |
| - multi-destination (`CvPathFinder::GetMultiplePaths`) | 4.0 / 4.7 / 4.0 | **24.8** | |
| - turns-to-target estimates (`CvUnit::TurnsToReachTarget`) | 9.5 / 12.3 / 12.0 | 4.5 | |
| - step cost (`CvUnitMovement::MovementCost`) | 10.8 / 7.6 / 6.5 | 3.4 | |
| trade-path cache (`CvGameTrade::UpdateTradePathCache`) | 4.9 / 5.0 / 4.2 | **27.2** | 21 |
| religion's city connections (`CvGameReligions::IsCityConnectedToCity`) | 1.0 / 1.6 / 0.8 | 10.7 (mostly through the trade-path cache) | 9-10 |
| danger maps (`CvDangerPlots`) | 4.3 / 3.6 / 4.2 | 12.7 | |
| - enemy units' reach (`UpdateDangerSingleUnit`) | 2.7 / 1.9 / 2.1 | 10.2 | |
| trade connection values (`CvPlayerTrade::UpdateTradeConnectionValues`) | 1.5 / 1.9 / 0.9 | 8.2 | |
| tactical analysis map (`CvTacticalAnalysisMap`) | 2.7 / 3.2 / 3.3 | 7.3 | |
| diplomacy AI | 8.2 / 6.8 / 6.2 | 9.3 | |
| economic AI | 8.1 / 8.6 / 10.6 | 4.7 | |
| city production AI (`CvCityStrategyAI`, `*ProductionAI`) | 8.8 / 10.0 / 11.9 | 3.7 | |
| religion AI | 8.4 / 7.6 / 7.4 | 4.9 | |
| builder AI | 6.8 / 6.2 / 6.0 | 4.2 | |
| military AI | 4.3 / 3.9 / 3.9 | 6.2 | |
| settle-plot search (`GetBestSettlePlot`, `updatePlotFoundValues`) | 2.7 / 1.6 / 1.3 | 1.9 | 6 (AI logging on) |
| statistics log (`SqliteLogger`; on in the study, R12; off for players) | 1.4 / 2.6 / 1.7 | 0.7 | |
| game database (`CvGameDatabase_x64`, SQLite) | 2.6 / 3.5 / 3.6 | 0.7 | |
| external-pause mutex (`ExternalPause` -> `OpenMutexA`, every `CvGame::update`) | 1.3 / 0.2 / 0.4 | 0.0 | |
| **Self (leaf frame)**: heap allocation and free (ntdll, ucrtbase, `operator new/delete`) | 6.0 / 7.3 / 6.5 | 4.6 | 4.4 (3 + 1.4) |
| self: `plotDistance` | 2.7 / 2.4 / 2.0 | 2.2 | 3.5 |
| self: A* core and its open-list heap (`CvAStar::*`, `std::_Pop_heap...`) | 5.2 / 6.7 / 6.9 | 12.2 | 4-5 (heap ops) |
| self: path callbacks (`*Cost`, `*Valid`, `CvUnitMovement`, `UpdateNodeCacheData`) | 22.1 / 14.5 / 15.6 | 22.1 | |
| self: waits (`NtWaitForSingleObject`, `ZwDelayExecution`) | 1.5 / 2.7 / 1.2 | 0.4 | 3 (`ZwDelayExecution`) |
| leaf inside the DLL | 86.2 / 84.2 / 86.5 | 92.9 | |

Phases (exclusive, sum 100 %): M unit moves (`CvGame::updateMoves`) 52.7-53.8 %, player turn starts
(`CvPlayer::setTurnActive`, `doTurnPostDiplomacy` the larger part) 43-44 %, game turn (`CvGame::doTurn`) 1-2 %, host 1-3 %;
H player turn starts **51.9 %**, unit moves 44.4 %, game turn 3.5 %, host 0.3 %. One thread holds 99.8 % of the cycles in
every run (the server's core thread). The heaviest single self function on H is `TradePathWaterValid` (8.2 %).

What the numbers say `[L for the shares; I for the reading]`:

- **M (mid game, Standard)**: unit AI is half the turn; path search is 39-42 % inclusive, mostly single-unit paths and
  reachable sets asked by the tactical and homeland AI (`FindUnitsForHarassing` -> `TurnsToReachTarget` 7.5 % alone in m3,
  explorer moves `GetBestExploreTarget` -> `GetPlotsInReach` 3.8 % in m1). The trade-path cache is small (4-5 %). Heap
  allocation is 6-7 % self. The three M runs agree within about 3 points on every family above 5 %; the self split of
  the path callbacks moved most (22.1 % in the loaded m1 against 14.5-15.6 %), as the load inflates memory-bound code.
- **H (late game, Huge)**: the trade-path cache is the largest single family, **27.2 %**, all of it its
  `GetMultiplePaths` rebuilds (water validity alone 8.2 % self); its callers that trigger the first rebuild of a turn
  are religion's `IsCityConnectedToCity` (10.4 %), `GetValidTradeRoutePathLength` (8.2 %) and `CanCreateTradeRoute`
  (5.6 %). Reachable sets are 24.9 %, of which 8.4 points come from the danger maps' enemy reach
  (`UpdateDangerSingleUnit`) and 4.5 from worker moves. The danger maps are 12.7 %, rebuilt also from diplomacy
  (`IsInDangerFromPlayers` from `DoUpdateWarStates` and others, 5.1 points). The A* core's open-list heap is 5.7 % self.
- **Against the 2026-11-11 profile** (Huge Continents 350-355, x64, the old pins, AI logging on): the trade-path cache
  went up (21 -> 27 %; another save, map and turn), path search went up (41 -> 53 %), dominance zones and the
  unit-assignment search went down (27 -> 18 %, 19 -> 10 %), operations down (13 -> 6 %), the settle-plot search down
  (6 -> 2 %, AI logging now off), the `Sleep(1)` yields down (3 % -> 0.1 % `ZwDelayExecution`; the calls are still in
  `CvTacticalAI.cpp` lines 12429 and 12868 [C]). Which of these moved with the pins and which with the save is not
  separated [I]. The trade-path cache code changed since then: it stores per destination an `STradePathInfo` (cost,
  distance, turns, path length, danger and terrain scores), not the path, and builds a full path on demand
  (`ComputeTradePath`) [C, `CvTradeClasses.cpp` at 04b1211b8].
- Small, not memory-for-speed: `CvGame::update` calls `ExternalPause()` (the `EXTERNAL_PAUSING` define of the build,
  `CvGame.cpp:1596`), an `OpenMutexA("TurnByTurn")` on every update, 0.2-1.3 % on M (25 updates per turn) and nothing
  measurable on H [L share, C code]. Dropping the define from the server's build would remove it [I; a build change].

### The DLL's AI_PERF log `[C, 2026-11-17, source 04b1211b8 and tools/x64-dll/build.py]`

- **The S3 build does not write it.** `AI_PERF`, `AI_PERF_FORMAT` and `AI_PERF_FORMAT_NESTED` expand to a `cvStopWatch`
  only `#if !defined(FINAL_RELEASE) || defined(VPDEBUG)` (`CvGameCoreDLLPCH.h:158-167`); otherwise to `((void)0)`. The
  x64 build defines `FINAL_RELEASE` in every configuration (`SHARED_PREDEFS`) and `VPDEBUG` only in `debug`. So in the
  release (and `fast`) DLL the macros are compiled out, and `dll_log_flags` bit 2 (`SetAIPerfLogging`) only sets
  `CvGlobals::m_bAIPerfLogging`, which nothing reads.
- **Switching it on needs another DLL**: a `debug` build (VPDEBUG, unoptimised: its times say nothing about the release
  DLL) or a release build without `FINAL_RELEASE` (which also changes the other `FINAL_RELEASE` code paths). Neither was
  made (no DLL change in this ticket). The flag also needs `SetLogging(true)`, and logging on is a different game over
  whole games (D49's correction, 2026-11-12), so even then it would be checked with statscmp.
- **It would not answer Q3 anyway**: the pin has four `AI_PERF` points, all in `CvPlayerAI.cpp`: `AI_chooseResearch`
  (583), `AI_ChoosePolicy` (639), `AI_considerAnnex` (687), `AI_considerRaze` (840); one more is commented out
  (`CvVotingClasses.cpp:10280`). None covers unit AI, path search, the trade-path cache or the danger maps, which hold
  most of the turn above. The other stopwatches (`xml-perf.log`, `CvDllDatabaseUtility.cpp`) time the database load
  only; `CvAStar.cpp:384` constructs a disabled `cvStopWatch`.

### Not verified (4668)

- Shares on a quiet machine: every run here shared the workstation with 5-7 heavy jobs; the shares are indications.
- H's shares from more than one run (one run of 21,011 walks; the M runs agree within about 3 points per family).
- The proportion of S1i's (the 32-bit DLL in c5h) time in the same families: not profiled here.
- Absolute seconds per family: the shares are of cycles while sampled; the bounds in the analysis's 9.8 multiply them by
  the series' turn times.

### Option 2 built: the proxy marks each AI player change as seen (ticket 4665, D165)

impl-4665, 2026-11-17 07:52 onwards, workstation. Design: `d165-pacing-fix-design.md` sections 2, 5, 6 and 7 (option 2,
granted by the MC 2026-11-17 07:50, #3965). Code: `proxy/src/proxy_pacing.{h,cpp}` (beside option 3), the hook
`ProxyRtInt::AfterUpdate` (`proxy_update.cpp`, called from `proxy_rt.cpp` `End()` for the outermost `ICvGame1::Update`),
the option in `options/`, the offline tests in `proxy/tools/rt_tests/pacing_unit.cpp` (runner
`perf/evidence/0437/4665/run_pacing_unit.py`). Evidence: `perf/evidence/0437/4665/`. Values and addresses only, nothing
from the EXE (D83). Marks: `[C]` read in the EXE file or our source, `[L]` seen live, `[I]` inferred.

*Changed 2026-11-23 by ticket 0718 (D246): option 2 now writes only while Quick Combat and Quick Movement are both on.
The DLL's two settings are read after each AI `Update` that reaches the decision. With either off nothing is written,
which is the stock wait. See "Options 2 and 4 follow Quick Combat and Quick Movement" at the end of this file.*

#### Step 0: what the frame does at a player change, and skip 3 `[C, L, 2026-11-17 07:58-08:10]`

Read statically in the Steam EXE (cdb on a scratch copy; working notes `perf/evidence/0437/4665/notes.md`):

- **Skip 3 never waits on its own.** In `GameViewState::Update` the per-frame flag F starts at 1; while an `Update` is
  pending F is set to the AI-turn byte 0x2DE0447 and nothing else happens (no scan, no transition). After an AI
  `Update`, the first frame sees the player change with queue B full: skip 2 sets the skip flag **and clears F**; the
  next test (F against the byte) then makes the byte's 1 -> 0 transition. Every later frame: skip 2 again, F 0 = byte
  0. When queue B is empty the frame stores the player into +0x8CC, F stays 1, and the 0 -> 1 transition follows. So
  the byte's drop after each `Update` is caused by skip 2 itself: in 4662's kept samples (S4 on M, turns 151-162) the
  byte dropped a median 8 ms (at most 116 ms) after the `Update`'s end in 299 of 300 cases, never at the end itself
  [L]. With +0x8CC already holding the next player, F stays 1 = the byte and neither skip fires [C]. **No write of the
  byte 0x2DE0447 is needed** (the MC's grant (A) of 07:57 for that write is not used).
- **What else the acceptance does (a).** The acceptance itself is only the store into +0x8CC. With it come, once per AI
  player in stock, the byte's 1 -> 0 -> 1 round trip: at the drop 0x50A170(0, 0) releases queue B's flag 0x2A10F00,
  clears the byte and appends one 0x60-byte record (header words 0x60, 0x98) to queue B; at the rise 0x50A170(1, 0)
  takes the flag, sets the byte and increments the counter 0x2DE0448, then the frame calls InterfaceBuddy 0x401C80(0)
  on `[[0xCC8E74]+0xD0]`; both transitions first call 0x76EC50 on the engine utility (it waits for the game-core
  thread only while an `Update` executes) [C]. **With option 2 these happen once per AI turn** (the byte rises at the
  first AI acceptance after the human's turn and drops when the human's turn begins) instead of once per AI player;
  the request's argument stays 1 in AI turns. What the listeners of the 0x98 record and of 0x401C80 do per player was
  not read [I]; the pictures below show what the player sees. Side effect to know [C]: queue B's flag 0x2A10F00 is held
  for the whole AI turn; its only other taker, 0x6E2AA0 (called only by 0x475E80, the queue-index switch), would spin
  until the human's turn; the paths that tear the game view down call 0x50A170(0, ...) first.

#### What it does `[C]`

- **The option** `VP64Options.ini [Performance] AiSkipVisualDrain`: 0 = stock (the default; the proxy reads and writes
  nothing of the EXE and logs one line `pacing: option 2 off ...`), 1 = on. Parse errors and values out of range give
  the default with one warning. With `host=headless` it is off with its own line (no EXE frame to pace).
- **The install** (the first `ICvGame1::Update` of a proxy load, game-core thread): the EXE build (VerifyExe, as option
  3: TimeDateStamp 0x546CD0A8, sections, the AI-turn byte, the hold and g_Civ5App in writable data), g_Civ5App readable,
  `[[g_Civ5App]+0xC0]` readable with the GameViewState vptr 0xA20908 (rebased) and +0x8CC 4-byte aligned. A failure is a
  boot failure (below).
- **Each Update's start** (`BeforeUpdate`): the AI-turn byte is sampled (one byte read).
- **After each outermost Update** (`End()` of `ICvGame1::Update`, after its epoch has ended, so the proxy's mirror is not
  in flight; not while an exception unwinds): when the byte was 1 at the start and the hold 0x2DE044C is 0 now, the
  re-check (g_Civ5App, the GameViewState pointer and vptr, alignment), then the scan: `GetActivePlayer`, and for slots
  0-63 `GetPlayer(i)`, `IsEverAlive`, `IsTurnActive` (the frame's two calls) through the proxy's own DLL interface
  objects, stopping at the first alive turn-active player; `IsHuman` only when that player is the active one. When a
  player is found and it is not the active human, one aligned `InterlockedExchange` of +0x8CC with its id. No player
  (the turn's end: seen 64 in the frame) = no write, so the last gap of an AI turn still waits for queue B: **the
  backlog of the whole AI turn drains there, before the turn-change `Update`** [C]. Nothing is restored (the frame
  writes the field itself in the human's turn: the active human's id).
- **Fail loud** exactly as option 3 (MC #3503): a failed check at the install = a loud `pacing: FAILED check skip-<name>
  at the install` line, the D118 report (stage `pacing`, code `install-skip-<name>`, the summary naming
  `AiSkipVisualDrain`) and the helper's popup; a failed check later (the re-check, an access fault on the byte, the hold
  or the write) = the loud line and the report, no popup; then no more writes in this proxy load (the stock wait). One
  report per proxy load, shared with option 3. The two options are independent: either failing leaves the other.
- **Counters** in vpproxy.log every 60 s while active and at unload (`detach: pacing: option 2 counters: ...`): Updates
  (starting in an AI turn), scans (DLL calls, microseconds, max), writes (already that player), not written by reason
  (human turn, hold, no turn-active player, the active human, scan failed), checks.
- **The scan's calls.** Right after an `Update` the epoch mirror is stale (the `Update` is impure), so the scan's
  getters cross to the server once and refill the mirror for the frame's own scan of the same values; whether the
  frame then hits depends on what impure calls the main thread makes in between. Counted per run: the proxy's DLL calls
  of the scan (counters line) and the per-method local/out counts of `IsEverAlive` and `IsTurnActive` in the proxy's
  call counters (live results below).

#### Offline tests `[C, 2026-11-17, workstation]`

- `pacing_unit` (option 3's 95 checks and option 2's): **159/159** with the proxy's toolchain (clang-cl -m32, VC9
  headers, /O2) and **159/159** under MSVC /fsanitize=address x86 (the canary's heap-buffer-overflow caught first). The
  fake EXE gets a GameViewState on its own page; a fake scan says what the DLL interface would answer. Option 2: off
  reads nothing (header, data and GameViewState pages PAGE_GUARD, still guarded after 6 Updates; also with
  `host=headless`); each install check refuses as a boot failure (timestamp, g_Civ5App, no game view state, +0x8CC not
  aligned, the wrong vptr, the object unreadable) with nothing written and the scan never called; the write (players
  0, 7, 63, the active player when not human) and each negative (the byte 0 or 2 at the start, the hold, no player, the
  active human, the scan failing, an After without a Before); the byte sampled at the start; the re-check's play
  failures (vptr, g_Civ5App, decommitted object, the byte's page); both options together (each failing leaves the
  other); a second proxy load installs again.
- `proxy/tools/test_proxy_rt.py --build C:/c5-64bit-work/4665/b2`: **110/110** (151 s); no PowerShell process left (11
  before, 11 after).
- `options/tests/test_options.cpp`: **234/234** in x86 and x64 and under MSVC ASan x64 (`TestAiSkipVisualDrain`; the
  old "unknown key" check replaced).
- The turn-study suite (`perf/tests/turnstudy`): 192 pass; new `test_skipoption.py` (9) and `test_pacing.py`'s
  `TestTurnEndBacklog` (3). Three `Headless` jobcap tests of 4664 fail only in the full run while the machine is loaded
  and pass alone (not touched by 4665).

#### Build b2 `[C, 2026-11-17 08:32-08:36 EST]`

`server/build_server.py --config release --offline` from task-4665-ai-skip-visual-drain at 924d2fea6 (clean), DLL
source the fork's pin 04b1211b8 by `git archive`, deps a copy of 4662's build_all: server.exe **4A460B62**, the proxy
**B3EE3F33**, vp64helper.exe **11FDEBED** (`perf/evidence/0437/4665/build-b2.txt`). Arm **S4s** = the RC's layout as S4
with these three (`perf/turnstudy/runner/arms.json`; layout `F:/c5-64bit-work/4665/b2-layout`, `b2-proxy`); variants
of M: `skip_off` (both options written stock), `skip1`, `skip0_tick0` (option 3 alone), `skip1_tick0`, and the
`pacing_` forms of `skip_off`, `skip1`, `skip1_tick0` (4661's poller). `pacing.py` reports per turn queue B's backlog
at the end of the AI phase (`turn_end_backlog`: bytes after the last AI player's `Update`, the time until queue B is
empty, the gap before the turn-change request, the same after the turn-change `Update`).

#### Workstation functional set `[L, 2026-11-17 08:48-09:09 EST, workstation (shared: 5-7 heavy jobs of other owners registered), c5h-b, window cloaked (the harness's no-focus launch), the product path S4s (pack=1, the RC's vpproxy.ini, vp64helper.exe beside the proxy: 0473 and 0475 have merged), M from TS0438-M-T150 by hash, window 151-160, statistics logging on, 4661's poller in every run, one run per arm: functional and an indication only (D106), no timing claim]`

Driver: `perf/turnstudy/pacing/wscheck.py c5h-b S4s:<variant> --turns 10` (script `C:/c5-64bit-work/4665/scripts/wsset.sh`).
Every run exit 0, restored and verified, no dialog box. The first attempt (08:43) failed closed at the launch: c5h-b's
import wrapper predated 0473 (`the guard ignores C5_CHILD_GUARD`); `tools/harness/native/wrapper.py install` on its
install, `provision.py verify c5h-b --quick` rc 0, then the set ran. Evidence `perf/evidence/0437/4665/ws-S4s-*/`.

- **Identity**: `statscmp.py <off> <on> --from-turn 150 --expect-turns 150 160`: off against skip1 **IDENTICAL** (15
  tables, 39,246 rows), off against skip1_tick0 **IDENTICAL** (15 tables, 39,246 rows)
  (`statscmp-ws-off-vs-skip1.txt`, `statscmp-ws-off-vs-skip1_tick0.txt`).
- **The option acts as designed** (the proxy's 60 s counters): skip1 at 205 s: Updates 316 (starting in an AI turn
  225), scans 225 (15,309 DLL calls, 34 ms in all, at most 0.47 ms), writes 216, no turn-active player 9 (the turn
  ends), the active human 0, scan failed 0, no failure line; skip1_tick0 at 182 s: 300 scans, 287 writes, 12 turn ends,
  the active human 1 (the hand-back of turn 162), at most 1.18 ms per scan. The off arm logs both `pacing: off` lines.
- **The poll** (`pacing.py --expect-periods 25`; seconds per M turn, means of turns 151-160):

| S4s variant | s per turn | U | D_B | T_tick | median D_B per period | backlog after the last AI Update | its drain | queue B max per turn |
|---|---|---|---|---|---|---|---|---|
| pacing_skip_off | 14.29 | 4.07 | 8.04 | 2.19 | 0.170 s | 20,088 B | 0.27 s | 78,136 B |
| pacing_skip1 | 11.80 | 4.38 | 5.57 | 1.85 | **0.000 s** | **475,376 B** | **5.56 s** | 480,872 B |
| pacing_skip1_tick0 | 7.90 | 3.59 | 3.99 | 0.32 | 0.000 s | 474,504 B | 3.77 s | 478,224 B |

  (Backlog = queue B's bytes at the end of the last AI player's `Update`, the barbarians'; drain = from there to the
  first empty queue B; both medians over the 10 turns.) The off run's poll had one 55 ms sampling gap (turn 154,
  `pacing.py` exit 5, kept); the skip1_tick0 poll counted 23 and 24 periods in turns 154 and 155 (exit 3: with the tick
  at 0 two requests can fall within one frame and the poll's edges merge, 4663; the proxy's counters and the
  identical statistics show every `Update` ran).
- **Why the numbers moved (the deferral, PERFORMANCE.md)** [L, I]: with option 2 the gate opens at once after each AI
  player (D_B median per period 0.000 s against 0.170 s); queue B is still pumped only while no `Update` is pending, so
  the AI players' visual events pile up (queue B grows to about 475 KB) and are drained in one block at the end of the
  AI phase, where the proxy writes nothing (no turn-active player) and the frame waits for queue B as in stock: about
  5.6 s per turn on skip1 (3.8 s with the tick at 0), against 0.27 s for the barbarians alone in stock. The wait for
  queue B (D_B) falls from 8.0 to 5.6 s per turn (skip1) and the time with queue B not empty after an `Update` (D_B
  busy) from 10.2 to 7.4 s: part of the drain now runs in the tick gaps between `Update`s, and one block drains faster
  than 25 gated pieces [I: the per-record cost of the drain was not measured]. **Nothing is left at the human turn's start**: the turn-change `Update`
  starts only after the backlog is drained, and after it queue B holds about 77-82 KB in every arm (the turn-change
  `Update`'s own events), the same as stock. So the player waits for the drain inside the AI phase, not after it: the
  turn-time figures already contain it. Turn times here are indications from one loaded workstation run each (D106).

#### UI Lua call latency and a second identity set `[L, 2026-11-17 09:10-09:28 EST, workstation (shared), c5h-b, cloaked, S4s on M window 151-160, no poller, VP64_TURN_BUDGET=1 (4652's timers), one run per arm: indication only]`

Driver: `perf/turnstudy/trace/wscheck.py c5h-b S4s:<skip_off|skip1|skip1_tick0> --turns 10 --turn-budget` (script
`C:/c5-64bit-work/4665/scripts/budgetset.sh`; the runs' exit 0; the driver's own exit 2 only because these variants
carry no frame trace), the session folders' `budget-proxy.tsv`, `budget.tsv`, `turn-times.tsv`, `proxy-lua-calls.tsv`
copied out. Readers `perf/evidence/0437/4665/lua_latency.py`, `lua_calls_compare.py`; outputs `lua-latency-wsb.txt`,
`lua-calls-off-vs-*.txt`, `wsb-S4s-*/`.

- **Identity without the instrument**: off against skip1 and off against skip1_tick0 **IDENTICAL** (15 tables, 39,246
  rows each, `statscmp-wsb-*.txt`).
- **Turn times** (the server's `turn-times.tsv`, turns 152-160): off 11.07, skip1 9.89, skip1_tick0 8.34 s per turn
  [L, one loaded run each, no claim].
- **UI Lua latency** (the proxy's per-call timers: class `lua` of `budget-proxy.tsv`, `out` = the round trip of every
  UI Lua call into the DLL, mean per call per turn): off **4.31 us** (per turn 3.90-4.61), skip1 **4.05 us**
  (3.62-4.92), skip1_tick0 **4.60 us** (3.57-5.94). The server's handler time of the ui channel's calls made while an
  impure call (an `Update`) is in flight: 3.4, 4.0 and 3.9 us per call, about 150,000 such calls per session in every
  arm. No deadlock, no stall: the gaps between `Update` calls get shorter but every frame still runs its UI Lua between
  two requests (the 4660 lesson holds: one `Update` per request). The timers keep means only, no distribution.
- **The UI makes far fewer Lua calls per turn with option 2** [L]: 323,571 per turn off, 108,279 with skip1, 78,131
  with skip1_tick0 (whole sessions: 4.09 M, 1.51 M, 1.18 M). The drops are in functions the UI calls when it refreshes
  panels and city banners: `Player.IsMinorCivDisplayedQuestForPlayer` 499 k -> 189 k, `Deal.IsPossibleToTradeItem` 398 k
  -> 150 k, `Game.GetResourceUsageType` 373 k -> 140 k, and the city-banner reads (`City.IsCapital`,
  `GetStrengthValue`, `GetMaxHitPoints`, `GetOwner`) about 36-71 k -> 2.5-5 k. So in stock some UI refresh happens once
  per AI player (most likely set off by the frame's per-player byte transitions, the 0x98 record or InterfaceBuddy
  0x401C80, which option 2 makes once per AI turn [I]); with option 2 the city banners and those panels are refreshed
  much less often during the AI phase. At the human turn's start the screen is the same (pictures below); what a
  player watching the AI phase would see is banners updating in bigger steps [I: not seen at 3 s sampling].

#### What the player sees (DIAGNOSTIC pictures, D112) `[L, the poller runs of the functional set above]`

Shots every 3 s by the harness (`cli.py shot`, PrintWindow of the cloaked window) beside each run; the chosen ones are
in the Visual Checks feed (`--result info --issue 4665`, every `--what` starting "DIAGNOSTIC (D112)"): stock
vc-20261117-091152-workstation-2911, -091153-workstation-1477, -091155-workstation-99e7; skip1
vc-20261117-091156-workstation-1a82, -091157-workstation-f5f3, -091159-workstation-ff02; skip1_tick0
vc-20261117-091200-workstation-a558, -091202-workstation-f673, -091203-workstation-9116. The same camera in all (autoplay
in observer mode does not move it); Quick Combat and Quick Movement are on in M, so no move or combat animation plays in
any arm and a 3 s sampling cannot show single moves.
- **During the AI phase**: in stock the "Processing Turn For <player>" banner steps through the AI players while each
  one's events reach the map before the next starts. With option 2 the banner runs ahead: about 4 s into turn 155 it
  reads "City States" (stock was still on the majors), and from about 5-7 s it reads "Barbarians" while the turn's
  events are delivered under it; a wonder notification of turn 155 appears only when the next turn's banner is up
  (skip1_tick0). The map is updated in a burst at the end of the AI phase instead of player by player. No unit was
  seen drawn in the wrong place, no stuck animation and no missing notification in the chosen shots [L, diagnostic, at
  the shots' resolution].
- **At the human turn's start** (the autoplay's hand-back of turn 162): the same screen in all three arms (NEXT TURN,
  CITY CAN ATTACK, the turn's notifications); no backlog plays after the hand-back.
- Not seen: whether with Quick Movement off the AI's unit moves would visibly pile up or be shortened in the end-of-phase
  burst (M has it on); a human game (not autoplay) with the player watching the AI phase.

#### The laptop series (ready, NOT submitted) `[2026-11-17 09:20 EST]`

Not before the H series ends (about 2026-11-18 10:21); submitted by the lead from the epic branch once 4665 has merged
(the testbed ships the submitting checkout's HEAD; `tools/harness/native/build.bat` built there). `--print-only`
checked every binary and the save on 2026-11-17 08:46 (from 74c727cd8). The increment of option 2 over option 3 and
both over stock, one interleaved set:

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4s:skip_off,S4s:skip0_tick0,S4s:skip1_tick0 --runs 6 --order williams --quiet --expect-min 190 --grade A --series-id 4665-M-skip --label "4665 AiSkipVisualDrain (D165 option 2): stock vs option 3 vs options 2+3" --issue 4665 --keep-going --timeout 18000

18 S4 runs of 27 autoplay turns (window 151-175), Williams order over 3 arms (6 rounds), 60 s idle gap each: about
3 h (4662's series: 12 runs in 2 h 05 min, 10.4 min per run; the option-2 arm is shorter per turn). Then, as a
separate instrument job after it (the poller never in a timed series), one poller run per arm for U, D_B, T_tick and
the end-of-phase backlog:

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4s:pacing_skip_off,S4s:pacing_tick0,S4s:pacing_skip1_tick0 --runs 1 --order fixed --turns 10 --slots 2 --keep-going --no-rerun --grade D --series-id 4665-M-skip-pacing --label "4665 pacing poll, D165 options 2 and 3" --issue 4665 --timeout 5400

(3 runs, about 30 min; `pacing_tick0` writes only AiUpdateTickMs, and AiSkipVisualDrain is absent from the instance's
options file, whose backup is restored by hash after each run, so it is 0.) To read them: `tb.py --host laptop fetch JOB
--out F:/c5-64bit-work/0437/4665-laptop-JOB --all`, `analyze.py` for the timed set (pairs skip0_tick0 - skip_off,
skip1_tick0 - skip0_tick0), `statscmp.py --from-turn 150` across arms, and `pacing.py RUN --expect-periods 25` for the
instrument runs (the `turn_end_backlog` lines). GPU drivers on the laptop: Intel HD Graphics 630 27.20.100.9320 and
NVIDIA Quadro P3000 32.0.15.7322 (D175: no driver updates; the run records name only the first adapter). The UI Lua
latency needs `VP64_TURN_BUDGET=1` (budget-proxy.tsv), which the runner does not set: from the workstation's budget
runs above, not from the laptop.

#### Result of the D178 laptop series `[L, laptop, job 20261117-113457-9d65, 2026-11-17 11:35-12:41 EST; analysed 12:44 EST by lead-0437 (sixth lead); grade B: 3 runs per arm, the owner's D178 dev-series size; no default-on claim from it (PERFORMANCE.md 2.6)]`

The owner's D178 (2026-11-17 11:20): 2-3 runs per arm of 10-20 turns are enough for development. One build (b2: server
4A460B62..., proxy B3EE3F33..., vp64helper.exe 11FDEBED..., as "Build b2" above; the proxy and server hashes checked
again 2026-11-17 12:49 on the shipped files), three settings, M from `TS0438-M-T150`, window
151-165 (15 turns), rotate order, quiet, statistics logging on; the laptop and its two GPU drivers (Intel HD Graphics 630
27.20.100.9320, NVIDIA Quadro P3000 32.0.15.7322; D175). Command as "The laptop series" above with `--runs 3 --turns 15
--order rotate --expect-min 75 --grade B --series-id 4665-M-skip-d178`. Outputs
`perf/evidence/0437/d178/analysis-4665-d178-9d65.{json,txt}`; raw `F:/c5-64bit-work/0437/4665-laptop-d178-9d65`.

| Arm | Settings | s per turn, mean (min-max) | against stock |
|---|---|---|---|
| `S4s:skip_off` | stock pacing | **14.49** (14.44-14.51) | - |
| `S4s:skip0_tick0` | option 3 (AiUpdateTickMs 0) | **13.00** (12.93-13.06) | -1.48 s, **-10.2 %** (CI of the difference [1.35, 1.61] s) |
| `S4s:skip1_tick0` | options 2 + 3 (AiSkipVisualDrain 1) | **9.16** (9.13-9.23) | -5.32 s, **-36.7 %** (CI [5.21, 5.43] s) |

- Option 2 on top of option 3: -3.84 s per turn (-29.5 %, CI [3.71, 3.98] s). CPU per turn falls with the time
  (EXE + server 23.4 -> 16.7 s): fewer frames drawn while the AI plays.
- **Same game**: every run IDENTICAL to its arm's first and the arms to each other over 150-167 (statscmp, 25,118
  BuildingYields rows): the options change only pacing.
- Option 3's gain here (-10.2 %) agrees with 4662's grade A series on 151-175 (-9.55 %).
- Memory and its growth: no difference resolved (peak private 1,660-1,678 MB, growth 1.17-1.21 MB per turn).
- What option 2 changes for the player (the map catches up in a burst at the AI phase's end; DIAGNOSTIC pictures of
  4665) is unchanged by this series; the default of either option is the owner's call at the epic's merge, with a grade A
  series if a default-on is proposed.

#### The laptop pacing poll of options 2 and 3 `[L, laptop, job 20261117-113554-41f1, 2026-11-17 12:42-13:00 EST; one run per arm, M 151-160 (10 turns of a 10-turn window); an instrument run (the poller), not timing: grade D]`

Analysed 2026-11-17 13:05 EST by lead-0437 (sixth lead) with `pacing.py RUN --expect-periods 25`; reports in
`perf/evidence/0437/d178/pacing-41f1/`, raw `F:/c5-64bit-work/0437/4665-laptop-pacing-41f1`. Per M turn on the laptop:

| Arm | turn (poll) | U | D_B | T_tick | queue B at the AI phase's end (median): bytes, drained in |
|---|---|---|---|---|---|
| stock pacing (`pacing_skip_off`) | 14.06 s | 3.57 s | 9.07 s | 1.42 s | 20,088 B, 0.39 s |
| option 3 (`pacing_tick0`) | 12.90 s | 3.78 s | 9.11 s | 0.01 s | 19,760 B, 0.35 s |
| options 2 + 3 (`pacing_skip1_tick0`) | 9.09 s | 3.66 s | 4.54 s | 0.89 s | **468,128 B, 5.22 s** |

- On the laptop the end-of-phase drain with options 2 + 3 is **5.2 s of the 9.1 s turn** (workstation 4.14 s, 4672):
  that is what option 4 (ticket 4675, `AiDrainUnordered`) and rank 2 (ticket 4676, hidden embarks) aim at. U is the
  same in all three arms (3.6-3.8 s).
- With option 2 on, `pacing.py` reports 23-24 periods in two turns (exit 3, "expected 25"): option 2 lets the next
  player's request go before queue B is empty, so two players' periods can fall in one frame gap; not a fault of the
  game (every run identical in the timed series).
- With option 3 on, the poll's frame rate is not a frame rate (the accumulator stays 0; 4663).

#### Not verified (4665)

- Every time figure: one run per arm on a loaded, shared workstation (D106); the laptop series decides the gain.
- A game with a human player watching the AI phase (autoplay only), Quick Movement or Quick Combat off, a turn with an
  AI leader message (the hold: no write then; none happened in 151-160), a second proxy load in one process (offline
  test only), an exit to the menu during an AI phase (queue B's flag 0x2A10F00 is then held; 0x580C89 releases it).
- What the listeners of the 0x98 record and InterfaceBuddy 0x401C80 do per AI player, and which UI refresh the 3x
  fewer UI Lua calls are (the function list only).
- The distribution (not only the mean) of UI Lua latency: the budget's timers keep means.
- The boot-failure popup of option 2 on the real EXE (offline tests only; the install passed on c5h-b in all runs).

### What the drain is made of: queue B by record type (ticket 4672, D165)

impl-4672, 2026-11-17 10:23-11:40 EST, workstation. The owner's question of 10:20 (relayed by the MC): are the AI's moves
animated, and can they be batched to bring the client's turn close to the headless one? Animations are off in M (Quick
Combat and Quick Movement on); this subsection says what the remaining wait is made of. Values and addresses only,
nothing from the EXE (D83). Marks: `[C]` read in the EXE (cdb on a scratch copy of the Steam EXE, SHA-256
`b2ce96ae...`, TimeDateStamp 0x546CD0A8) or in the DLL's source (fork 04b1211b8), `[L]` seen live, `[I]` inferred.
Evidence `perf/evidence/0437/4672/`; raw runs `F:/c5-64bit-work/4672/ws/`.

**In one paragraph.** The drain is **frame-bound, not dispatch-bound**. The handlers that queue B's records call cost
0.56 s of a 4.1 s end-of-phase drain with options 2+3, and 1.2 s of 6.9 s of D_B with stock pacing; the rest is
ordinary frames (about 25 ms each on this loaded machine), because the queue's move rule lets **one keyed record per
unit through per frame**, and every record with flag bit 2 waits for a frame in which nothing before it was blocked.
`GameplayUnitEmbark` posts six such records (flags 0x5) per embark or disembark, so the about 24 embarks of an M turn
pass **one record per frame, strictly in series across all units**: about 145 of the 180 frames of the end-of-phase
drain. A model of the rule run on the recorded records reproduces the frames seen within 10 % and says what each
remedy would save (below; the ranked list is in `d165-pacing-fix-design.md` section 10).

#### The queue's structure and rule `[C, 2026-11-17 10:25-10:55, static reading]`

- **A half** (write and dispatch, `q + (p&1)*0x10` and `q + ((p-1)&1)*0x10`, p = `[q+0x20]`) is a byte vector: +4 begin,
  +8 end, +0xC capacity. **A record**: +0 size (records are size-chained), +4 type (the dispatch table's index), header
  +0x10..+0x43 (13 words, `combat-choreography.md` 3.2-3.3), payload from +0x44 with the event id first. Each type has its
  own post function (it reserves the space, which moves the end pointer first, then writes size, type, header and
  payload under the queue's lock +0x34). `q+0x24` is a kept dispatch offset, `q+0x28` the event-id counter, `q+0x2C`
  the dispatch offset, `q+0x30` the ordering byte.
- **The pump** 0x785AF0 (on the main thread, only while no `Update` is pending): the GC 0x457060 (erases dispatched
  records whose hold counter, header word 11, is 0; `q+0x2C` := the kept size), the move 0x4AC910, then the dispatch loop
  0x63ABA0 (one call of the handler `[table + 8*type]` per record from offset `q+0x2C`), then `q+0x2C` := the dispatch
  half's size.
- **The move** (ordering byte set) walks the write half in posting order. A record is checked by 0x60EB10 against
  **every record in the dispatch half** (the held ones **and those moved earlier in this same pass**) and every record
  before it still in the write half, with the predicate 0x727A30 (`combat-choreography.md` 3.2: an earlier record with
  bit 1 blocks all; with bit 0 it blocks a later one with bit 1 or one sharing a key: words 0/1 cross-compared, the
  owner/id pairs at words 2, 4, 6 cross-compared, values < 0 never match, or the later one's parent is its event id). A
  blocked record stays (with bit 1 it ends the pass); after a blocked record, a bit-2 record stays and a bit-3 record ends
  the pass; every other record moves, in posting order. *Correction to `combat-choreography.md` 3.2 (2026-11-17):* the
  check covers the records moved in the same pass, not only the held ones; that is why one unit's keyed records pass one
  per frame even when nothing is held.
- **Which call posts which type** [C, the post function behind each `Gameplay*` slot 144-204 of vftable 0x9EA718; L where
  the census's count equals the session's call count in `proxy-calls.tsv`]: `perf/turnstudy/pacing/qbtypes.tsv`. The ones
  that matter here: 0x4F `GameplayUnitTeleported` [L 7,726 = 7,726], 0x53 `GameplayUnitShouldDimFlag`, 0x72
  `GameplayFOWChanged` [L 16,405 = 16,405], 0x6F `ICvUserInterface1::SetSpecificCityInfoDirty` [L 7,661 = 7,661; flags 4],
  0x49 Fortify/Work/Activate, 0x51 Visibility, and **`GameplayUnitEmbark` = six records 0x46, 0x45, 0x50, 0x48, 0x53, 0x4A
  (the model destroyed and created again), each with flags 0x5** [C; L: 292 of each with flags 5 in the census].
- **The DLL's side** [C, 04b1211b8]: with Quick Movement on, `CvUnit::move` calls `SetPosition` for every plot step
  (`GameplayUnitTeleported`), whether or not the human can see the plot (`CvUnit.cpp` 20986-21003);
  `embark()`/`disembark()` call `GameplayUnitEmbark` unconditionally (7120, 7139).

#### The instrument (`perf/turnstudy/pacing/qbcensus.py`, off by default)

Inside 4661's poller, only when the environment variable `TURNSTUDY_QB_CENSUS=1` (the runner is unchanged: the poller
inherits the driver's environment). Per sample after the pacing values: queue B's control block (0x34 bytes); new
records of the write half (incremental reads; a full read when it shrinks or moves); the dispatch half when its begin,
end, `q+0x2C` or first event id change. Records are identified by event id; a record is accepted only when its size
chain holds and its id is above every known one and not above the counter (the post function moves the end pointer
before it writes the record: 85-5,296 retries per run, all resolved). Per record: type, size, flags, hold counter, keys
0/1, owner/id, parent, the sample it was first seen in, the sample its move was seen in. A pump = the move batches seen
before `q+0x2C` reaches their end: move time = last batch - first batch, dispatch-loop time = end - last batch (1 ms
edges; a loop that ends inside one sample counts 0). **Cost** [L]: the census's own time 3.1 % (stock pacing) and 6.5 %
(options 2+3, where it reads the 475 KB backlog at every move batch: 754 MB read in 122 s) of the poll's time; the
poller's CPU 18.1 % of one core against 4661's 11.6 %; sample interval p99 2.37 ms. Records whose move was missed: 214
and 392 (0.3-0.6 %, single-record pumps of equal size in a row; the key now includes the first event id, after these
runs). Tests `perf/turnstudy/pacing/test_qbcensus.py` (4) and 4661's 49 pass. Analysis `qbanalyze.py`, model `qbsim.py`.

#### The runs `[L, 2026-11-17 10:37-10:50 EST, workstation, shared (9 heavy jobs of other owners registered: games, builds, suites), c5h-a (claimed as impl-4672 under 0437), window cloaked, arm S4s (4665's build b2), M from TS0438-M-T150 by hash, window 151-160, statistics logging on, poller with the census, one run per arm: indication only (D106), no timing claim]`

`C:/c5-64bit-work/4672/runset.sh` (copy in the evidence): `perf/turnstudy/pacing/wscheck.py c5h-a S4s:<variant> --turns
10` with `TURNSTUDY_QB_CENSUS=1`; both runs exit 0, restored, `provision.py verify` rc 0, no dialog. 4661's reading of
the same runs: options 2+3 (`pacing_skip1_tick0`) 8.35 s per turn, U 3.86, D_B 4.13 (all of it after the last AI
player), T_tick 0.36; stock pacing (`pacing_skip_off`) 12.32 s, U 3.55, D_B 6.88, T_tick 1.89.

**Records per M turn** (means over 151-160, both arms post the same game; the census counts every record, the 25
`Update`s' and the frame's own):

| Type | Posted by | per turn | KB/turn | share |
|---|---|---|---|---|
| 0x53 | `GameplayUnitShouldDimFlag` (and set 5 of an embark) | 1,438 | 157 | 33 % |
| 0x72 | `GameplayFOWChanged` | 663 | 62 | 15 % |
| 0x6F | `SetSpecificCityInfoDirty` (flags 4) | 662 | 62 | 15 % |
| 0x4F | `GameplayUnitTeleported` (Quick Movement: one per plot step) | 648 | 81 | 15 % |
| 0x49 | `GameplayUnitFortify` / `Work` / `Activate` | 217 | 24 | 5 % |
| 0x51 | `GameplayUnitVisibility` | 174 | 22 | 4 % |
| 0x48, 0x45, 0x50, 0x46, 0x4A | `SetDamage`, `UnitCreated`, `UnitDestroyed`, and **24 embarks of 6 records each** | 78, 53, 53, 46, 24 | 5-10 each | 1-2 % each |
| others (38 types) | plots, cities, techs, war, met team, the frame's own 0x98, ... | 214 (options 2+3) / 330 (stock: the per-player records 0x98, 0x65, 0x66, 0x62, 0x6D come 25 times instead of once) | 24 / 35 | 5-8 % |
| **all** | | **4,269 / 4,385** | **465 / 476** | |

Majors post 63-64 %, the city states 15-16 %, the barbarians 4-5 %, the turn-change `Update` 16-17 % (by the period the
record was posted in). **No record was ever held** (hold counter 0 in all 125,768 records): with Quick Combat on there is no
combat simulation to hold one. With options 2+3 the backlog at the last AI player's end is 4,229 records, 461 KB (4665's
475 KB), by type 0x53 34 %, 0x6F 16 %, 0x72 15 %, 0x4F 15 %.

**Where the drain's time goes** (per turn, means of 151-160):

| | options 2+3: the end-of-phase drain | stock pacing: D_B of the 24 AI players |
|---|---|---|
| wall | 4.14 s | 6.88 s (D_B busy 8.73 s) |
| pumps (= frames that moved something) | 163 (+ about 21 single-record pumps the census missed) | 229-251 |
| move (0x4AC910) | 0.145 s (the first pass over 1,450-2,000 records takes 60-90 ms) | 0.013 s |
| dispatch loop (the handlers) | **0.556 s** (the first pump's loop 0.13-0.33 s) | **1.195 s** |
| the rest: ordinary frames (render, UI per frame) | **about 3.4 s** (about 25 ms per frame) | **about 5.7 s** |

The first pump moves 1,450-2,000 records; then 200-500, then tens, then **one or a few records per frame for 2-3 s**:
the tail is the embark sets (0x46, 0x45, 0x50, 0x48, 0x53, 0x4A of one unit after another) interleaved with teleports
and dim flags of the units with long move chains [L, turn 156 listed with `C:/c5-64bit-work/4672/explore2.py`].

**Dispatch cost per record** (non-negative least squares of each pump's loop time on its counts by type; options 2+3:
1,632 pumps, R2 0.96; stock: 2,514 pumps, R2 0.73): on average 0.13 ms per record; FOW changes 0.13 ms (0.09 s per
turn), plot state 0x2D/0x3B 3.5-9 ms each (13-21 per turn), city population 1.7-5 ms, unit created 0.26-0.75 ms; teleports
0-0.31 ms. In stock pacing one record type, **0x6D (poster not identified), costs about 81 ms each, 7 per turn (one per
major civilisation; 0.57 s per turn)**; with option 2 it comes about once per AI turn [L, I: the per-player UI refresh
4665 saw in the UI Lua call counts]. No type is worth a dispatch-side fix: the whole loop is 0.56 s per turn with
options 2+3.

**The model** (`qbsim.py`: the rule above run pass by pass on the records the census saw, in posting order) needs 180
passes per turn for the end-of-phase backlog (options 2+3) against 163 pumps seen + about 21 missed (model/seen 1.10),
and 253 passes for stock pacing's per-player drains against 229 seen (1.10); its first passes move 1,460 / 390 / 299 / 351
records against 1,493 / 396 / 303 / 356 seen (turn 151). What-ifs, in passes per turn and seconds at the run's own 25-27
ms per drain frame [I, model; workstation frame time]:

| What-if | options 2+3 (180 passes) | stock pacing (253) |
|---|---|---|
| teleports: only the last per unit | -7 (-0.2 s) | -42 (-1.1 s) |
| teleports, dim flags, visibility, fortify/work: the last per unit and type | -43 (-1.1 s) | -75 (-2.1 s) |
| embark sets never posted (the upper bound of "not for units the human cannot see") | **-163 (-4.1 s)** | **-127 (-3.5 s)** |
| the same for AI units only (owner not 0; a rough stand-in for "unseen") | -154 (-3.9 s) | -127 (-3.5 s) |
| embark sets never posted + last per unit and type | -172 (-4.4 s) | -179 (-4.9 s) |
| embark records without bit 2 (an EXE flag: a bound, not an option) | -157 (-4.0 s) | -82 (-2.2 s) |
| city info dirty (0x6F) never posted | 0 | 0 |
| the ordering byte `q+0x30` cleared: every record moves in the first pass | -179 (-4.5 s, less the loop's 0.56 s, which stays) | -229 (-6.2 s, less 1.2 s) |

Each unit embarked or disembarked at most once per turn (240 embark sets, 240 unit-turns), so "an embark undone in the
same drain" saves nothing; the human's own units are 10 % of the embarks.

#### Where U's time goes, against a headless turn (c) `[L, 4665's budget runs: 2026-11-17 09:10-09:28, workstation, S4s:skip1_tick0 with VP64_TURN_BUDGET=1, M 152-160, one run: indication]`

From 4652's rows (`perf/evidence/0437/4672/u_excess.py`, `budget-table-wsb-S4s-skip1_tick0.csv`), per M turn: the
served `Update`s 3.76 s = the DLL's own work 2.14 s + calls into the EXE 0.94 s (GameEvents Lua: 90,164 `CallTestAll` and
friends, about 10 us per round trip; their Lua calls back into the DLL included) + the turn change's autosave 0.56 s +
UI and engine calls 0.11 s; transport (about 0.37 s per turn at 0.6-0.8 us per message) lies inside these rows. The same
machine's headless proxy route (4652, 2026-11-16, M): DLL work 2.05 s + calls into the host 0.12 s + transport 0.13 s =
2.31 s. **So U's excess over a headless turn on this machine is about 1.45 s: GameEvents Lua in the EXE +0.8 s, the
autosave +0.56 s (headless hosts do not save), transport +0.2 s; the DLL's own work is the same (2.14 against 2.05 s).**
Calls from the EXE (UI Lua) are 0.27 s per turn and run between `Update`s (overlap of two service threads 0.08 s per
turn): **no wait on the game-core lock or on UI Lua inside U** [L]. The laptop's "U about 3.6 s against 1.62 s headless" splits
the same way [I]: a headless turn has no EXE Lua and no autosave.

#### Option 1 against (b) (d)

With options 2+3 the gaps between AI `Update`s are already near nothing (D_B before the last AI player 0.00 s, T_tick
0.36 s per turn) and the whole drain waits at the end of the AI phase. Option 1 (several AI players per request) would
remove at most those 0.36 s per turn [I] and **not the end drain**: the backlog is the same and drains before the
turn-change request either way. A deadlock-free form (4660) needs the main thread's UI Lua to run between DLL
`Update`s, i.e. the server would yield the game-core lock and wait for the frame between players, which brings the gaps
back: its gain on top of 2+3 tends to zero. The drain remedies of (b) act on the part option 1 cannot reach.

#### Not verified (4672)

- Every time figure: one run per arm on a loaded, shared workstation (D106; 9 heavy jobs of other owners registered at
  both arms' `quietlock.py check`); the two arms are **not** an on/off timing comparison (that is a testbed pair), only
  each run's own split; the model's seconds use this machine's 25-27 ms frames (the laptop's frame time differs). The record counts and the model's pass counts do not depend on the machine.
- Which of the 24 embarks per turn the human can see (the census does not read visibility); "AI units only" is a rough
  stand-in. *Answered 2026-11-17 by ticket 4676 ("Embarks the human cannot see"): in an autoplay run the observer sees
  every plot; from player 0's view 78 % of the sets are hidden, worth -134 frames per M turn against "AI units only"'s
  -154.*
- What the handlers would do with one unit's records dispatched in the same frame (the ordering byte cleared): not run.
- That the ordering byte `q+0x30` is set once (at the queue's creation) and not rewritten per frame: not read.
- The poster of 0x6D (81 ms each in stock pacing), 0x69, 0x65 and 0x1D.
- Scenario H, a human game watching the AI phase, Quick Movement or Quick Combat off (then combats hold records and the
  ordering matters).

### Option 4 built: the end-of-phase drain in one frame (ticket 4675, D165)

impl-4675, 2026-11-17 11:31 onwards, workstation. Design: `d165-pacing-fix-design.md` section 10, rank 1 (the MC's yes
and conditions: #4203). Code: `proxy/src/proxy_pacing.{h,cpp}` (beside options 2 and 3), the option in `options/` (id
5), the offline tests in `proxy/tools/rt_tests/pacing_unit.cpp` (runner `perf/evidence/0437/4675/run_pacing_unit.py`).
Evidence: `perf/evidence/0437/4675/`. Values and addresses only, nothing from the EXE (D83). Marks: `[C]` read in the
EXE file or our source, `[L]` seen live, `[I]` inferred.

#### Who writes the ordering byte `[C, 2026-11-17 11:35-11:40, cdb 10.0.26100 x86 on a scratch copy of the Steam EXE (SHA-256 b2ce96ae...), scripts in C:/c5-64bit-work/4675/re, not kept]`

4672's open point ("is `q+0x30` rewritten per frame?"). Every absolute operand in `.text` that points into queue B's
block 0x2A10E00-0x2A10F04 was listed (a scanner over the section's bytes) and the code after each of the about 340
references to the buffer base 0x2A10E08, and after each of the 1,527 references to the queue object 0x2A10200, was
read for a store to `+0x30` (byte, word or dword) or to `+0xC38` from 0x2A10200:

- **Only two stores, both in `LoadGameState::Update` 0x403170** (its state machine on `[this+0x10]`): its first state
  writes 0 at 0x4031F7 and a later state writes 1 at 0x403492, each to `[0x2A10E38 + 0x4C * [0x2A10200]]` (the current
  queue's byte). So the byte is 0 while a game loads (the load's records pass unordered) and 1 from then on.
- The pump 0x785AF0, the GC 0x457060, the move 0x4AC910 and the frame (`GameViewState::Update`) never write it. The
  move reads it once per record (`cmp byte ptr [ebp+30h], 0` at 0x4AC9A2): with 0 it skips the blocking check 0x60EB10
  and the "blocked earlier" state stays clear, so **every record of the write half moves, in posting order, in one
  pass**; held records and flag bit 1 no longer stop anything.
- Every other store to an offset 0x30 near these references writes into a freshly reserved record (header word 8) or
  into another object (0x7A8D37: a new 0x13C-byte object).

**So nothing in play sets the byte back by itself; the design of section 10 stands unchanged** (no question to the
lead). The live check (the census reads the byte at every poll sample) is below.

#### What it does `[C]`

- **The option** `VP64Options.ini [Performance] AiDrainUnordered`: 0 = stock (the default; one line `pacing: option 4
  off ...` when option 2 is on, nothing read or written), 1 = on. With `AiSkipVisualDrain` 0, or `host=headless`, option
  4 is off with one warning line and reads nothing.
- **The install** (with option 2's, at the first `ICvGame1::Update` of a proxy load): option 2's checks, then queue
  B's index 0x2A10200 and buffers 0x2A10E08 in a writable, non-executable data section, the index in 0..2 (a fourth
  buffer would overlap the flag set at 0x2A10F00), the control block readable and the ordering byte 1. A failure is a
  boot failure of option 4 alone (check names `drain-*`; the D118 report and the popup; option 2 goes on).
- **At the end-of-phase drain**: option 2's scan after an AI `Update` found no turn-active player (the turn's end), so
  the frame will wait for queue B before the turn-change request. The `Update` is still pending, so the main thread
  does not pump queue B (6.3) [C]. Then: the hold 0x2DE044C is 0 (option 2 does not scan otherwise), the DLL's
  `CvPreGame::quickCombat()` and `quickMovement()` (ICvPreGame1 slots 84 and 85, the settings that decide whether
  the DLL posts combats and animated moves) are both true, queue B's two halves are walked (size-chained records,
  three tries when the control block moves or a record is half written, else "not cleared this time"): no record in
  either half has a hold counter (header word 11), none of the write half has flag bit 1 (header word 12; a dispatched
  record with bit 1 that is not held is erased by the pump's GC before the move); the byte is 1. Then one byte write: 0.
- **The set-back**: at the top of the next `Update` (the turn-change `Update`; its request set the pending count, so no
  pump has run since the frame's last one [C: the request sets `+0x40` = 1 before it resumes the thread,
  `game-core-thread.md` section 1]) the byte must still be 0, and is set to 1. Also at option 2's or 4's failure and at
  the proxy's unload.
- **Fail loud** as options 2 and 3: a failed read or write, the index out of range, a half that is not a byte vector, the
  byte not 1 before the clear or not 0 at the set-back (the byte is then left as found) = a loud `pacing: FAILED check
  drain-<name> during play` line and the report without a popup; no more clears in that proxy load (the EXE's order).
  Option 2 failing stops option 4 (byte set back).
- **Counters** (`pacing: option 4 counters: ...` every 60 s with option 2's and at unload): end-of-phase drains, cleared,
  set back, not cleared by reason (Quick Combat off, Quick Movement off, settings not answered, held, flag bit 1, queue
  changing), records walked, the walk's microseconds.
- **TEST switch** (MC #4203 (a)): with the environment variable `VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1` the Quick
  Movement guard is not applied (named in the install line); only for the DIAGNOSTIC pictures with Quick Movement off.

#### Offline tests `[C, 2026-11-17 11:50-11:56, workstation]`

- `pacing_unit`: **256/256** with the proxy's toolchain (clang-cl -m32, VC9 headers) and **256/256** under MSVC
  /fsanitize=address x86 (the canary's heap-buffer-overflow caught first) (`pacing_unit-run.txt`). Options 3 and 2's 159
  checks unchanged; option 4's 97: off (queue B's page and the records PAGE_GUARD through an AI turn and two ends of
  phase: untouched, no settings asked; with option 2 off or headless the warning line), each install refusal (index 3,
  the byte 0 or 2, the queue's page in a read-only section, the page decommitted), the clear and the set-back (an empty
  queue, the third buffer), each guard negative (Quick Combat off, Quick Movement off, the settings not answered, a held
  record in either half, a bit-1 record in the write half, the hold, the human's turn, a half-written record: not
  cleared, no failure), each play failure (index 7, the byte 0 before the clear, 1 or 7 at the set-back, an access
  fault in the walk, a half that is not a byte vector, the page gone at the end and while held), option 2 failing while
  held, the unload's set-back, a second proxy load, options 2, 3 and 4 together.
- `options/tests/test_options.cpp`: **269/269** in x86 and x64 and under MSVC ASan x64 (`TestAiDrainUnordered`, 35 checks;
  `test_options-run.txt`).
- `perf/turnstudy/pacing/test_qbcensus.py` 5 pass (the ordering byte's samples and changes).
- The turn-study suite (`perf/tests/turnstudy`, `perf/turnstudy/pacing`): 258 pass (new `test_drainoption.py`, 10).

#### Build b2 and the arm `[C, 2026-11-17 12:03-12:07 EST]`

`server/build_server.py --config release --offline` from task-4675-ai-drain-unordered at 68e93313d (clean; `main`
merged), DLL source the fork's pin 04b1211b8 by `git archive`, deps a copy of 4662's build_all: server.exe
**A3CB598D**, the proxy **8B9B05D8**, vp64helper.exe **D78F3289** (`build-b2.txt`). Arm **S4d** = the RC's layout as S4
with these three (`perf/turnstudy/runner/arms.json`; layout `F:/c5-64bit-work/4675/b2-layout`, `b2-proxy`). Variants
of M: `drain0` (options 2+3 on, AiDrainUnordered = 0 written), `drain1` (2+3+4), their `pacing_` forms (4661's poller;
with `TURNSTUDY_QB_CENSUS=1` the census too), and the Quick Movement off forms `pacing_qmoff_drain0`,
`pacing_qmoff_drain1` and `pacing_qmoff_drain1_any` (the TEST switch). New variant keys `ai_drain_unordered` (0/1; 1
refused without `ai_skip_visual_drain` 1) and `drain_test_any_movement` (true/false; sets the environment variable).
(Build b1 at c0daa8f79 had `repo_dirty=1` and was used for nothing.)

#### Workstation functional pair `[L, 2026-11-17 12:12-12:25 EST, workstation (shared: 7-9 heavy jobs of other owners registered at each start), c5h-b (claimed impl-4675 under 0437), window cloaked, arm S4d, M from TS0438-M-T150 by hash, window 151-160, statistics logging on, poller with the census, harness shots about every 1.8 s beside each run (not recorded; chosen ones recorded), one run per arm: functional and an indication only (D106), no timing claim]`

Driver `perf/evidence/0437/4675/runset.sh S4d:pacing_drain0 S4d:pacing_drain1` (`perf/turnstudy/pacing/wscheck.py`
per arm); both runs exit 0, restored and verified (`provision.py verify` rc 0), no dialog box; `pacing.py` exits 3 and
5 (a turn with 22-24 periods: two requests in one frame with the tick at 0, 4663; sampling gaps of 58-88 ms on the
loaded machine), kept. Evidence `perf/evidence/0437/4675/ws-S4d-pacing_drain{0,1}/`, `qb/` (`qbanalyze.py`).

- **Identity**: `tools/d10/statscmp.py <drain0> <drain1> --from-turn 150 --expect-turns 150 160`: **IDENTICAL** (15
  tables, 39,246 rows; `statscmp-ws-drain0-vs-drain1.txt`).
- **The option acts as designed** (the proxy's counters at 184 s): end-of-phase drains 12, **cleared 12, set back 12**, not
  cleared 0 by every reason, no failure line; the walk saw at most 4,890 records, at most 484 us (3.5 ms in all).
- **The byte is not rewritten per frame** [L]: the census read `q+0x30` at every sample. Option 4 off: 1 in all 114,300
  samples (10 turns). Option 4 on: 24 changes in 83,000 samples, exactly the proxy's 12 clears and 12 set-backs: 1 -> 0
  at the first sample after the barbarians' `Update` (seen 63), 0 -> 1 at the turn-change `Update`'s top (pending,
  seen 64); between them the byte stayed 0 for 0.42-2.17 s (and 4.5 s in the first turn after the load) while the
  frames ran; the EXE never set it back.
- **The drain** (per M turn, means of 151-160; `qbanalyze.py`):

| | option 4 off (2+3) | option 4 on (2+3+4) |
|---|---|---|
| backlog after the last AI `Update` | 4,227 records, 461 KB | 4,218 records, 459 KB |
| end-of-phase drain, wall | 4.25 s | **1.13 s** |
| pumps (frames that moved records) in it | **180** | **2** |
| the move (0x4AC910) | 0.154 s | 0.009 s |
| the dispatch loop (the handlers) | 0.590 s | 0.439 s |
| the rest (ordinary frames; with option 4 the long frame around the dispatch) | about 3.5 s | about 0.68 s |
| seconds per turn (pacing.py; one loaded run each, no claim) | 8.43 | 5.87 |

  The first pump moves the whole backlog (3,700-4,900 records) and dispatches it in one frame (loop 0.17-0.74 s); the
  second moves the frame's own record of the AI-turn byte's drop (0x98). So the 180 frames of the one-keyed-record-per-
  unit rule are gone, as 4672's model said (179 of 180). What is left of the drain (1.13 s against the model's 0.56 s
  loop) is the loop itself and about 0.7 s of frame time around it (the frame that dispatches 4,000 records and the
  next one or two, until the frame sees queue B empty and asks for the turn change) [I: not split further]. The
  per-turn U (3.84 -> 4.25 s) and T_tick (0.34 -> 0.50 s) moved by load noise on the shared workstation [I]; the
  laptop pair decides any gain (D106).

#### What the player sees (DIAGNOSTIC pictures, D112) `[L, the runs above]`

Feed entries (`--result info --issue 4675`, every `--what` starting "DIAGNOSTIC (D112)"): vc-20261117-123214-workstation-8a20
(option 4 on, turn 151 "Processing Turn For Barbarians" 0.25 s before the clear: the backlog not yet shown),
vc-20261117-123217-workstation-d8be (2.4 s later, turn 152: the whole backlog shown; reference = the next entry),
vc-20261117-123220-workstation-b463 (option 4 off, turn 152 at the same offset after the turn's autosave). The same camera
(autoplay in observer mode); Quick Combat and Quick Movement on in M, so the burst contains teleports, dim flags, FOW,
embark model swaps and city updates, no animation. **The map content after the burst equals option 4 off's** (the
city of Tegdaoust's new model, every unit in view in the same place, the same notifications); no unit in a wrong
place and no stuck or missing model at this resolution [L, diagnostic, PrintWindow of the cloaked window]. The banner
runs ahead with option 4 (turn 152's "Sweden" against "The Maya" at the same offset: the AI phase is further along).

#### Quick Movement off: the guard stays (MC #4203 (a)) `[L, 2026-11-17 12:31-12:54 EST, workstation (shared), c5h-b, cloaked, S4d on M window 151-155 (5 turns), Quick Movement turned off by the in-game options call before each arm (read back: game option and PreGame false, Quick Combat true), poller with the census, shots beside each run, one run per arm: indication only]`

Three arms: `pacing_qmoff_drain0` (option 4 off), `pacing_qmoff_drain1` (option 4 on, its guard active) and
`pacing_qmoff_drain1_any` (the TEST switch lets option 4 act). *The first `_any` run (12:39) ran without the TEST
switch: the pacing workstation driver launched the EXE with its own environment, not the run's
(`perf/turnstudy/pacing/wscheck.py`, fixed: `clientarm.run_env`); it is kept as the guard's run
(`ws-S4d-pacing_qmoff_drain1-guarded/`, shots in `F:/c5-64bit-work/4675/shots/S4d-pacing_qmoff_drain1_any`), and the
`_any` arm ran again at 12:48 (`ws2`, `shots2`). The separate `pacing_qmoff_drain1` arm was not run (the guarded run is
it).*

| | option 4 off | option 4 on, guard active | option 4 acting (TEST switch) |
|---|---|---|---|
| seconds per turn (pacing.py; no claim) | 28.68 | 28.44 | 11.16 |
| end-of-phase backlog / its drain (medians) | 849 KB / 29.6 s | 847 KB / 23.1 s | 848 KB / 6.6 s |
| option 4's counters | - | 7 ends, 0 cleared, "Quick Movement off" 7 | 7 ends, **7 cleared**, held 0, bit 1 0 |
| statscmp against option 4 off (15 tables, 22,351 rows) | - | IDENTICAL | **IDENTICAL** |
| records held at some time (all GameplayUnitMoved 0x4D, 1,913, and 8-28 of 0x49) | 1,941 | - | 1,921 |
| one unit's 0x4D moves dispatched in the same frame (`samepump.py`) | **0** | - | **207 groups of 2-4 (497 records) in 14 pumps** |

- **Why the other guards do not stop it** [L, C]: at the end of the last AI `Update` nothing of the AI phase has been
  dispatched yet (option 2 deferred it), so no record is held then; the holds begin when the first pump dispatches the
  move records and their animations start. With the byte 0 the move has no blocking check, so every later record moves
  in the same pass: **a unit's second, third and fourth moves start while its first is still animated**, and all units
  animate at once (the drain shrinks from about 30 s to 6.6 s because the animations overlap, not because they are cut).
  With Quick Movement on the same happens to teleports (2,097 groups of 2-12 `GameplayUnitTeleported` of one unit in one
  frame in the 10-turn pair), which are instant, so only the final position is drawn either way.
- **Verdict: the Quick Movement clause stays.** The pictures cannot clear it: stills about 2.5 s apart show a single
  ship's move under way and no broken model, but the census shows the overlap by construction in every turn. Gray
  untextured terrain appeared in 2-3 shots of every Quick Movement off arm, option 4 off included (and in the first shot
  after each load with Quick Movement on): the camera panning to new places, not option 4 (`grayscan.py`).
- DIAGNOSTIC pictures (feed, `--issue 4675`): vc-20261117-130024-workstation-da8f (option 4 off, turn 151 in the drain:
  the camera following the animated moves at Vancouver), vc-20261117-130028-workstation-9572 (option 4 acting by the TEST
  switch, turn 153, 0.95 s after the clear; reference the previous entry), vc-20261117-130030-workstation-8b85 (2.6 s
  later: a ship's move under way; the verdict in its text).
- With a human watching the AI phase and Quick Movement off, a later remedy could clear the byte only for records the
  queue's rule would let pass anyway (not built; the guard is the MC's condition (a)).
- *Note 2026-11-17 (impl-4678): these arms' observer sees every plot, so every AI move is animated. For a human
  player 0 the DLL already teleports the moves he cannot see. With the observer following player 0, the same arm
  takes about 12.2 s per M turn, not 28.7 s; see "Quick Movement off: the animated moves of units player 0 cannot
  see" below.*

Evidence: `ws-S4d-pacing_qmoff_drain0/`, `ws-S4d-pacing_qmoff_drain1-guarded/`, `ws-S4d-pacing_qmoff_drain1_any/`,
`statscmp-ws-qmoff-*.txt`, `samepump.txt` (also the hold counts and the gray scan), scripts `samepump.py`, `holds.py`,
`grayscan.py`, `record1.sh`, `record2.sh`.

#### Not verified (4675)

- Every time figure: one run per arm on a loaded, shared workstation (D106; 7-9 heavy jobs of other owners at every
  start); the laptop pair decides the gain. The split of the remaining 1.13 s of drain (the loop's 0.44 s and about
  0.7 s of frame time around it) is inferred, not measured per frame.
- The unload's set-back (`DrainRestore` at `DetachOnUnload`) and the boot-failure popup of option 4 on the real EXE:
  offline tests only (the harness stops the EXE by terminating it, so `DetachOnUnload` does not run; every install
  passed on c5h-b).
- A turn with an AI leader message (the hold), an exit to the menu or a load during the drain (LoadGameState writes the
  byte; the set-back then finds 0 or 1 and reports), a human game watching the AI phase, Quick Combat off (combats hold
  records; the guard asks the DLL's `quickCombat()`), scenario H.
- What a player would see of one unit's teleports of a turn arriving in one frame with Quick Movement on (only the
  final position is drawn in both orders [I]; the pictures at 1.8-2.5 s spacing show the map before and after).

#### Result of the laptop pair (D178) `[L, 2026-11-17 13:12-14:16 EST, laptop, job 20261117-131234-0901, series 4675-M-drain-d178, submitted by lead-0437 (sixth lead) from the epic branch at 3be9807a9 (build b2), analysed 14:52 by lead-0437 (seventh lead); grade B (D178: n=3 per arm, no default-on claim)]`

M 151-165 (15 turns), S4d with options 2+3 on, option 4 off (`drain0`) against on (`drain1`), 3 runs each, Williams
order, quiet job alone (undisturbed false at job level; per-run foreign cores 0.051-0.060). Fetched to
`F:/c5-64bit-work/0437/4675-M-drain-0901` (`--all`), records rewritten, `analyze.py --pairs S4d:drain1/S4d:drain0`.

| | options 2+3 (`drain0`) | options 2+3+4 (`drain1`) | difference |
|---|---|---|---|
| seconds per turn, mean [95 % CI] | 9.181 [8.991, 9.370], cv 0.83 % | **4.749** [4.599, 4.899], cv 1.27 % | **-4.43 s (-48.3 %)**, CI of the difference [4.27, 4.59], ratio 1.93 [1.88, 1.98] |
| client CPU s/turn | 16.92 | 9.30 | -7.62 s |
| server CPU s/turn | 3.47 | 3.01 | |
| effective clock (R18) | 106.3 % (2.99 GHz) | 119.0 % (3.34 GHz) | fewer busy cores let the laptop boost: part of the effect, not a separate factor |
| peak private (S4 sum) | 1872 MB | 1912 MB | +40 MB (+2.1 %) |
| growth per turn | 1.37 MB | 3.59 MB | +2.2 MB/turn [1.4, 2.9] (n=3; not explained; watch in a longer series) |
| identity (stats.db, 15 tables) | IDENTICAL within | IDENTICAL within | **IDENTICAL across** |

Against 4665's D178 series on the same machine (stock pacing 14.49 s, options 2+3 9.16 s; another job, so a cross-series
reading, not a pair): options 2+3+4 take an M AI turn from about 14.5 s to about 4.7 s (-67 %), the game unchanged. All
with Quick Movement and Quick Combat on, autoplay observer (see "Quick Movement off" below for the other case).

#### The laptop pair (D178; the command; submitted 13:12 as job 20261117-131234-0901) `[2026-11-17 12:28 EST]`

Submitted by the lead from the epic branch once 4675 has merged (the testbed ships the submitting checkout's HEAD;
`tools/harness/native/build.bat` built there), after 4665's series. `--dry-run` passed on 2026-11-17 12:28 from 931f4c6e1
(8 files checked by SHA-256, 13 modules parsed and imported; `laptop-pair-dryrun.txt`): option 4 off against
on, both with options 2+3, one interleaved set:

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4d:drain0,S4d:drain1 --runs 3 --order williams --turns 15 --quiet --expect-min 50 --grade A --series-id 4675-M-drain --label "4675 AiDrainUnordered (D165 option 4): options 2+3 against 2+3+4" --issue 4675 --keep-going --timeout 7200

6 S4 runs of 15 autoplay turns (window 151-165), Williams order over 2 arms (3 rounds), 60 s idle gap each: about 40-50
min [I: 4662's laptop runs took 10.4 min for 25 turns at stock pacing; with options 2+3 a turn is about 40 % shorter].
Read it with `tb.py --host laptop fetch JOB --out F:/c5-64bit-work/0437/4675-laptop-JOB --all`, `analyze.py` (the pair
drain1 - drain0) and `statscmp.py --from-turn 150` across the arms. An instrument run on the laptop (one poller run per
arm, never in the timed series), if the lead wants the drain's frames on that machine:

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4d:pacing_drain0,S4d:pacing_drain1 --runs 1 --order fixed --turns 10 --slots 2 --keep-going --no-rerun --grade D --series-id 4675-M-drain-pacing --label "4675 pacing poll, D165 option 4" --issue 4675 --timeout 3600

(the census needs `TURNSTUDY_QB_CENSUS=1` in the job's environment, which the runner does not set: without it the poll
gives U, D_B, T_tick and the end-of-phase backlog and drain time, not the pump count.)

### Options 2 + 3 + 4 on pin batch 4 (ticket 0563, 2026-11-18; the MC's check before the options' defaults go to the owner, D178)

impl-0563 (account B), 2026-11-18 17:48 onwards. Every number above was measured on study builds of pin batch 2
(x64 DLL 29F58E17); A9 of batch 4 changes game results (COMMON 18), so this pair re-measures stock pacing against
options 2 + 3 + 4 on main's code with the batch-4 x64 DLL, as one interleaved set. Evidence `perf/evidence/0563/`.

#### The build and the packed layout (arm `S4b4`) `[C, 2026-11-18, workstation]`

0437's batch-4 build (`C:/c5-64bit-work/0437-b4/out2`) was deleted at the epic's merge, and every S4x layout and the
RC stage are on F:, which the MC closed to this ticket's work (MC #6049; one read of the M save allowed, #6074). So the
layout was made again on C: (`C:/c5-64bit-work/0563/`, scripts `perf/evidence/0563/build.sh` and `pack.sh`):

- server.exe **2BC94943...**, the proxy **631E4C3E...** (carries the keys AiUpdateTickMs, AiSkipVisualDrain and
  AiDrainUnordered) and vp64helper.exe **2AF96926...**: `server/build_server.py --config release --offline` from this
  branch (build id `ab6c50709`, repo_dirty 0 = main a13b82271 + 0563's tracker/research/evidence commits; no file
  under server/, proxy/, ipc/, render/, shell/, deps/ or tools/x64-dll/ differs from main), DLL source a `git
  archive` of the fork's pin c29442e8c (deps/dll.lock `commit`); 752 s, 2026-11-18 17:53-18:06 EST. Its own DLL
  build was used only for linking: the layout carries the pin's DLL bytes.
- VP64 = `tools/release/pack.py` (the release driver's pack stage) over the pin layout's x64 DLL **2B56A1CD...** and
  its three libraries (the pin layout `x64-c29442e8-p58724e0/bin`, deps/dll.lock `release`) with the build's
  server.exe; the x64 DLL is named `CvGameCore_Expansion2_x64.dll` in the pack (issue 2606); `x86_dlls` = the tag
  Release-5.4.8's committed `(1) Community Patch/CvGameCore_Expansion2.dll` (262AEE88..., `git show` of the fork's
  tag tree, as a shipped pack); purity.tsv from `proxy/tools/derive_purity.py` on the pin source and DLL.
- vpproxy.ini: the RC's text (mode=on, the tag DLL as `CvGameCore_Expansion2_x86.dll`, x64_dir VP64, pack=1).
- Not the RC's increment 1 layout (S4): no compositor (thin), the shell configuration of S4/S4d.
- Pack: manifest.json **B408DFFD...** (x64_dll 2B56A1CD..., purity 10D7EB41..., x86_dlls the tag's 262AEE88...,
  `built` fork c29442e8c / tools ab6c50709), purity.tsv 166 of 166 audited rows kept (137 method, 29 lua), 0 dropped;
  vpproxy.ini 70F2C066... (byte-equal to the RC's). Every hash is in arms.json `S4b4` and `layout/SHA256.txt`.

#### The pair (laptop job 20261118-181423-b273, submitted 2026-11-18 18:14 EST)

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4b4:skip_off,S4b4:drain1 --runs 3 --order williams --turns 10 --quiet --expect-min 60 --grade B --series-id 0563-M-b4-d178 --label "..." --issue 0563 --keep-going --timeout 5400 --saves-dir C:/c5-64bit-work/0563/saves

Shipped commit 8b11b873e (`--dry-run` ok: 8 files by SHA-256, 13 modules). Arms: `skip_off` = stock pacing
(AiUpdateTickMs -1 and AiSkipVisualDrain 0 written, AiDrainUnordered absent = its default 0), `drain1` = options 2 + 3
+ 4 (0, 1, 1), the same variants as 0437's 4665 and 4675 pairs. M from TS0438-M-T150 (98FC454A..., copied once from
its F: folder, MC #6074), window 151-160, statistics logging on, Quick Movement and Quick Combat on (the save's),
autoplay observer, quiet and alone, Williams order over 3 rounds. Grade B by PERFORMANCE.md (3 runs per arm, D178): no
default-on claim from it alone.

#### Result `[L, laptop, job 20261118-181423-b273, runs 2026-11-18 18:15-18:50 EST, rc 0, 6 of 6 runs exit 0, none excluded; analysed 18:51 by impl-0563; grade B (D178: n=3 per arm)]`

Quiet job alone (no other job sampled; `undisturbed` false at job level for the logged-on session, as in every 0437
laptop job; per-run foreign cores 0.050-0.072). Laptop: Windows 10.0.19045.6466; GPU drivers Intel HD Graphics 630
**27.20.100.9320** and NVIDIA Quadro P3000 **32.0.15.7322** (the run records' `gpus`), unchanged since 0437 (D175: no
driver change). Every run's proxy log: the pack booted (`release 0563-batch4-c29442e8`, x64 DLL 2b56a1cd, tag DLL
262aee88 matched, purity bound to 2b56a1cd), and the options as written: stock arm `AiUpdateTickMs = -1`,
`AiSkipVisualDrain = 0` from the file, `AiDrainUnordered = 0 (default)`; options arm 0 / 1 / 1 from the file.
Fetched to `C:/c5-64bit-work/0563/fetch-b273` (15 MB), records rewritten, `analyze.py --pairs
"S4b4:drain1/S4b4:skip_off"`; outputs `perf/evidence/0563/analysis-b273.{json,txt}`, sealed run records
`perf/evidence/0563/records/`.

| run (Williams order) | arm | s per turn (151-160) | wall s | boot + load s |
|---|---|---|---|---|
| r01-p1 | stock | 14.413 | 351.9 | 123.5 |
| r01-p2 | 2+3+4 | 4.616 | 227.6 | 120.6 |
| r02-p1 | 2+3+4 | 4.695 | 230.0 | 120.7 |
| r02-p2 | stock | 14.353 | 347.3 | 120.8 |
| r03-p1 | stock | 14.214 | 343.1 | 120.4 |
| r03-p2 | 2+3+4 | 4.595 | 229.7 | 121.0 |

| | stock pacing (`skip_off`) | options 2+3+4 (`drain1`) | difference (options - stock) |
|---|---|---|---|
| seconds per turn, mean [95 % CI] | 14.326 [14.073, 14.580], sd 0.102, cv 0.71 % | **4.635** [4.505, 4.765], sd 0.052, cv 1.13 % | **-9.69 s (-67.6 %)**, CI of the difference [-9.90, -9.48], smallest detectable 0.28 s; ratio stock/options 3.09 [3.02, 3.16] |
| client CPU s/turn | 23.92 | 9.39 | -14.53 s [-15.00, -14.06] |
| server CPU s/turn | 5.39 | 2.92 | |
| effective clock (R18) | 104.2 % (2.93 GHz) | 118.2 % (3.32 GHz) | fewer busy cores let the laptop boost: part of the effect |
| peak private (S4 sum) | 1853 MB | 1866 MB | +12 MB [-51, +76]: none resolved |
| growth per turn | 2.90 MB | 6.24 MB | +3.28 MB/turn [1.16, 5.41] (n=3; not explained, see below) |
| identity (stats.db, 15 tables, 39,257 rows over 150-162) | IDENTICAL within | IDENTICAL within | **IDENTICAL across** (`statscmp-across-b273.txt`) |

- **Against 0437's batch-2 numbers** (laptop, same drivers; other jobs, other builds, window 151-165, so a
  cross-series reading, not a pair): stock 14.49 s (4665's D178 series) -> 14.33 s here; options 2+3+4 4.749 s (4675's
  pair) -> 4.635 s here; the effect -67 % (cross-series, batch 2) -> **-67.6 % within one interleaved set on batch 4**.
  Pin batch 4 and main's code leave the options' effect where 0437 measured it; A9's different game does not move it.
- **Why the numbers moved** (0437's D165 analysis, sections "What paces an AI turn in the client" and options 2-4): an
  M AI turn in the client is paced by the EXE's frame loop, not by the DLL: per AI player one `Update` (U), the wait for
  queue B to drain after the player change (D_B) and the 0.1 s update tick (T_tick). Option 3 removes T_tick, option 2
  the D_B wait, option 4 lets the end-of-phase backlog that option 2 leaves drain in one frame. The client draws far
  fewer frames per turn (client CPU -61 %); the server's CPU per turn falls too (5.39 -> 2.92 s) [I: its share of the
  waits; not measured here]. U itself is unchanged by the options (4665's poll).
- **Memory growth** +3.3 MB/turn with the options (6.24 against 2.90 MB/turn over 10 turns, n=3) repeats 0437's sign
  (2+3+4 against 2+3: 3.59 against 1.37 MB/turn, n=3, unexplained). Peak private does not differ. It is not a
  per-time growth either: per second of play the options arm grows faster still (6.24 / 4.635 = 1.35 MB/s against
  2.90 / 14.33 = 0.20 MB/s). Whether it is a level that settles within the window or growth that goes on needs a
  longer series [I, open]; worth one look before a default-on proposal.
- No crash, dialog box or refused launch in the job (job.log; COMMON 30: nothing to root-cause). No screenshot was
  taken (the runner takes none; the Visual Checks feed has no entry for 0563).

#### Not verified (0563)

- Grade B only (D178, n=3): the default-on question itself wants a grade A series (PERFORMANCE.md 2.6; 4665's note).
- The window is 151-160 (the ticket's), not 0437's 151-165: the cross-batch comparison mixes windows as well as builds.
- Quick Movement off, a human player watching (autoplay observer only), the H scenario, the workstation's GPU.
- The memory growth difference over more than 10 turns.
- The layout is not the increment 1 RC: main's server and proxy, thin (no compositor), the pin's DLL; it is a test
  pack, not a player's build.

### Option 3 alone on pin batch 4 (ticket 0570, 2026-11-18; D191's check before 0566 makes AiUpdateTickMs 0 the default)

impl-0570 (account B), 2026-11-18 19:29 onwards. The owner's D191 (2026-11-18): option 3 on by default once confirmed on pin
batch 4. 0563's pair measured options 2 + 3 + 4 together, not option 3 alone; this pair measures stock pacing against
option 3 alone on the same build, layout and save, as one interleaved grade A series. Evidence `perf/evidence/0570/`.

#### The build, the layout and the save `[C, 2026-11-18 19:33, workstation]`

0563's packed layout `S4b4` (section above; `C:/c5-64bit-work/0563/layout`, kept until 0570 by MC note) was still on
C: and every one of its 11 files matched `perf/evidence/0563/layout/SHA256.txt` (server.exe 2BC94943..., proxy
631E4C3E..., vp64helper.exe 2AF96926..., x64 DLL 2B56A1CD..., manifest.json B408DFFD..., purity.tsv 10D7EB41...,
vpproxy.ini 70F2C066..., tag DLL 262AEE88...): nothing was rebuilt; arms.json `S4b4` unchanged. The M save
TS0438-M-T150 beside it hashed 98FC454A... (0563's copy).

#### The pair (laptop job 20261118-201930-d1ea, submitted 2026-11-18 20:19 EST)

    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4b4:skip_off,S4b4:skip0_tick0 --runs 6 --order williams --turns 10 --quiet --expect-min 80 --grade A --series-id 0570-M-b4-tick0b --label "..." --issue 0570 --keep-going --timeout 9000 --saves-dir C:/c5-64bit-work/0563/saves

Shipped commit 7966773ab (`--dry-run` ok: 8 files by SHA-256, 13 modules), which carries 0570's harness fix. The
first submission (job `20261118-193521-36ab`, series `0570-M-b4-tick0`, 19:35) ended with exit 6 after 2 of 12 runs:
the dialog watcher was adopted as a child of the game through a reused pid and never ended
(`research/06-tooling-and-workflows/run-and-drive-the-game.md` 17.8; the laptop was recovered by job
`20261118-200539-7d4f`). Its two runs are not in the result: r01 stock 14.355 s, option 3 alone 12.896 s per M turn,
stats.db IDENTICAL (n=1, a note only). Arms: `skip_off` = stock pacing
(AiUpdateTickMs -1, AiSkipVisualDrain 0 written, AiDrainUnordered absent = 0), `skip0_tick0` = option 3 alone
(AiUpdateTickMs 0, AiSkipVisualDrain 0 written, AiDrainUnordered absent = 0): the variants of 0437's 4665 series. M
from TS0438-M-T150, window 151-160, statistics logging on, Quick Movement and Quick Combat on (the save's), autoplay
observer, quiet and alone, Williams order over 6 rounds (12 runs). Grade A by PERFORMANCE.md section 2 item 7 (one
series, one machine, six runs per arm, quiet lock) if every run is kept.

#### Result `[L, laptop, job 20261118-201930-d1ea, runs 2026-11-18 20:20-21:39 EST, rc 0, 12 of 12 runs exit 0, none excluded, no rerun; analysed 22:02 by impl-0570; grade A]`

Quiet job alone (`alone` true, no other job sampled; `undisturbed` false for the logged-on session, as in every 0437
and 0563 laptop job; per-run foreign cores 0.055-0.089). Laptop: GPU drivers Intel HD Graphics 630
**27.20.100.9320** and NVIDIA Quadro P3000 **32.0.15.7322** (the run records' `gpus`), the same as 0437 and 0563
(D175: no driver change). Every run's proxy log: the pack booted (`release 0563-batch4-c29442e8`, tag DLL 262aee88
matched), and the options as written: stock arm `AiUpdateTickMs = -1` and `AiSkipVisualDrain = 0` from the file,
`AiDrainUnordered = 0 (default)`; option 3 arm `AiUpdateTickMs = 0`, `AiSkipVisualDrain = 0` from the file,
`AiDrainUnordered = 0 (default)`, the engine utility on the vftable copy, "option 2 off". 12 stops rc 0, 12 `leftovers`
empty, no dialog, crash or refusal in the job log (COMMON 30: nothing to root-cause). Fetched to
`C:/c5-64bit-work/0570/fetch-d1ea`, records rewritten, `analyze.py --pairs "S4b4:skip0_tick0/S4b4:skip_off"`
(`perf/evidence/0570/fetch_analyze.sh`); outputs `perf/evidence/0570/analysis-d1ea.{json,txt}`, run records
`perf/evidence/0570/records/`, the queue record `queue-record-d1ea.json`.

| run (Williams order) | arm | s per turn (151-160) | wall s | boot + load s |
|---|---|---|---|---|
| r01-p1 | stock | 14.362 | 356.4 | 127.1 |
| r01-p2 | option 3 | 12.768 | 328.9 | 121.5 |
| r02-p1 | option 3 | 12.779 | 327.7 | 120.8 |
| r02-p2 | stock | 14.198 | 349.2 | 124.3 |
| r03-p1 | stock | 14.315 | 346.5 | 121.3 |
| r03-p2 | option 3 | 12.743 | 323.2 | 116.4 |
| r04-p1 | option 3 | 12.686 | 320.6 | 115.0 |
| r04-p2 | stock | 14.148 | 340.4 | 117.1 |
| r05-p1 | stock | 14.228 | 342.1 | 118.1 |
| r05-p2 | option 3 | 12.597 | 319.8 | 115.8 |
| r06-p1 | option 3 | 12.705 | 320.7 | 115.2 |
| r06-p2 | stock | 14.216 | 338.5 | 116.1 |

| | stock pacing (`skip_off`) | option 3 alone (`skip0_tick0`) | difference (option 3 - stock) |
|---|---|---|---|
| seconds per turn, mean [95 % CI] | 14.245 [14.161, 14.328], sd 0.079, cv 0.56 % | **12.713** [12.642, 12.783], sd 0.067, cv 0.53 % | **-1.532 s (-10.75 %)**, CI of the difference [-1.627, -1.437] s ([-11.42, -10.09] % of stock), Welch df 9.7; **smallest detectable 0.132 s** (0.93 %); ratio stock/option 3 1.120 [1.113, 1.128] |
| client CPU s/turn | 23.78 | 22.40 | -1.38 s [-1.88, -0.88] |
| server CPU s/turn | 5.35 | 4.43 | |
| effective clock (R18) | 104.7 % (2.94 GHz) | 103.1 % (2.89 GHz) | about the same: not a boost effect |
| peak private, EXE + server | 1832 MB | 1836 MB | +3.4 MB [-1.7, +8.5]: none resolved |
| growth per turn, EXE (mean [CI]) | 2.25 [1.80, 2.70] MB | 1.98 [1.15, 2.82] MB | |
| growth per turn, EXE + server | 3.02 MB | 2.92 MB | -0.10 MB/turn [-1.14, +0.94], smallest detectable 1.45: none resolved |
| identity (stats.db, 15 tables, 39,257 rows over 150-162) | IDENTICAL within (6 runs) | IDENTICAL within (6 runs) | **IDENTICAL across** (`statscmp-across-d1ea.txt`, r01-p1 against r01-p2) |

- **Grade A** by PERFORMANCE.md section 2 item 7: one series (`0570-M-b4-tick0b`), one machine, the same scenario, save
  and logging settings, six runs per arm, the quiet lock held, no run excluded. The arms differ only in the option
  (one build, one layout), and the games are identical, so the difference is the option's.
- **Against the earlier option 3 numbers** (cross-series readings, other builds and windows): 4662's grade A laptop
  series on pin batch 2 (151-175) -1.46 s, -9.55 %; 4665's D178 series (151-165) -1.48 s, -10.2 %; here, on pin batch
  4 and main's code, **-1.53 s, -10.75 %**. Pin batch 4 leaves option 3's effect where 0437 measured it.
- **Why the number moved** (0437's D165 analysis, "What paces an AI turn in the client", and 4662/4663): per AI player
  the EXE's frame loop waits for its 0.1 s update tick (T_tick) before it asks the game core for the next `Update`;
  with `AiUpdateTickMs = 0` the proxy makes the frame's request on the next frame. The laptop poll of 4665 measured
  T_tick 1.42 s per M turn at stock pacing and 0.01 s with option 3; the measured gain (1.53 s) is that tick plus a
  little less client CPU (fewer frames drawn while waiting: -1.38 s of client CPU per turn). The queue-B wait (D_B)
  and `Update` itself are unchanged by option 3 (4665's poll). The server's CPU per turn is lower too (5.35 -> 4.43
  s) [I: its share of the waits; not measured here].
- **Memory**: no difference resolved in peak or growth (unlike options 2 + 3 + 4 in 0563, +3.3 MB/turn): option 3 alone
  does not carry 0563's open growth question.
- **Same game as 0563**: this series' stock r01-p1 against 0563's stock r01-p1 (job b273): IDENTICAL (15 tables,
  39,257 rows, checked 2026-11-18 22:05).
- D191's condition for ticket 0566 (option 3 confirmed on pin batch 4: identical game, resolved saving): met.

#### Not verified (0570)

- Quick Movement off, a human player watching (autoplay observer only), the H scenario, the workstation's GPU, oldpc.
- The layout is 0563's test pack (main's server and proxy, thin, the pin's DLL), not a player's build; 0566's default
  change itself is not measured here.
- Whether the harness fix's per-parent walk changes anything for children started in the same clock tick as their
  parent (psutil's non-recursive rule is `>=`, the same as before for direct children).

### Option 3 on by default (ticket 0566, D191; 2026-11-19)

impl-0566 (account B), 2026-11-19 21:04 onwards, branch `task-0566-ai-tick-default`. The owner (D191) [reworded for privacy]: option 3 is approved,
to be switched on once confirmed; 0570 confirmed it (above). **`[Performance] AiUpdateTickMs` defaults to 0 (was -1)** in the
options table, the default file the proxy writes (`; AiUpdateTickMs = 0`) and `docs/OPTIONS.md`. Range unchanged (-1 to
100); an explicit `-1` still gives stock pacing; an out-of-range or unreadable value gives the new default 0 with one
warning. `AiSkipVisualDrain` and `AiDrainUnordered` stay 0. No timing claim here beyond 0570's (-1.53 s, -10.75 % per M
turn on pin batch 4) and 0563's.

**What an install or an upgrade does with an existing `VP64Options.ini`** `[C, this branch's code, 2026-11-19]`:
- The proxy creates the file only when it is missing (`proxy/src/proxy_options.cpp` `OptionsBoot` ->
  `Vp64Opt::CreateDefaultFile`) and never rewrites an existing one. Its default file lists every key commented out at
  its default, so files written by builds before 0566 carry `; AiUpdateTickMs = -1`: a comment, not a value. Such a
  file gives **the new default 0** with a 0566 build (unit test: `[Performance]\n; AiUpdateTickMs = -1\n` -> 0, no
  warning). The stale comment stays in the player's file and still shows -1; deleting the file gives a fresh copy.
- A file with its own uncommented `AiUpdateTickMs = ...` line keeps that value (the loader's last-line rule), **-1
  included**. Only the player or a tool writes such a line: the installer never does.
- The installer (`tools/installer/install.iss`): an update first removes the old version by its record with
  `RemoveLogs` false (`P64Install` -> `P64Remove(MG, False, ...)`), which keeps `VP64Options.ini`; that removal runs
  `vp64helper off` only when side files exist, and `off` puts back the file's bytes from before `on` (or only its
  `ServerRenderer` line, or removes the file when `on` created it; `proxy/helper/proxy_cfgini.cpp` `RendererOff`). The
  new install's `vp64helper on` (configuration `i2` only) changes or inserts just `[Renderer] ServerRenderer = 1`
  (`RendererOn`; a missing file becomes a header plus that one line, so option 3 then takes the compiled default 0).
  Nothing in `tools/installer`, `tools/release` or `proxy/helper` names `AiUpdateTickMs` (grep, main fdc19066,
  2026-11-19). An uninstall with "remove logs" deletes `VP64Options.ini` (`P64RemoveLogs`). So **no upgrade path keeps
  -1 unless the player wrote it**. 0574's release configuration (D197: the installer writing `AiUpdateTickMs = 0`) is
  not on main (0.1b cancelled by D200).
- **For the turn-time runner** (`perf/turnstudy/runner`): an arm without an `ai_update_tick_ms` variant leaves the
  instance's file untouched (`tslib.settings`), so with a proxy built after 0566 such an arm runs **with option 3 on**.
  A stock-pacing reference must write -1 (the M variants `tick_off`, `skip_off`, `f0_qmoff_stock` do). Layouts built
  before 0566 (every pin layout so far, `x64-1fd51353-p1e09fc7` included) keep -1 as their default.
- With the default on, option 3's boot checks run for every player without the key: on an EXE other than Steam build
  4390913 a failed check writes the D118 report and raises the popup. The MC wants that (#7259; D118/D125, ticket 0588).

**Tests** `[C, 2026-11-19, workstation]`: `test_options` x86 453 passed, x64 453 passed, MSVC ASan 453 passed (canary
caught, peak 65 MB): the new default, an empty file, a file without the key, a commented line, explicit -1 (alone and
beside other keys), out-of-range (101, -2, ...) and parse errors each to 0 with one warning naming "the default 0", the
default file's line and help text. Evidence `options/evidence/0566/`.

**Live smoke** `[L, 2026-11-19 21:44-21:53 EST, workstation, c5h-a (DX11 EXE stamp 0x546CD0A8), M from TS0438-M-T150,
2 turns per run, functional only]`: a packed layout of this branch on pin batch 5 (proxy 0ED11F12..., x64 DLL 78355A75;
`options/evidence/0566/build.sh`), driven by `options/evidence/0566/run_smoke.py`. With no `AiUpdateTickMs` line in the
instance's file the proxy logged `options: [Performance] AiUpdateTickMs = 0 (default)` and `pacing: on: ...
AiUpdateTickMs = 0 ...` (slot 102 answers 0.000 s); with `AiUpdateTickMs = -1` written for the run it logged `(from the
file)` and `pacing: off ([Performance] AiUpdateTickMs = -1): nothing installed`. Both runs exit 0 with no box; lines in
`options/evidence/0566/pacing-lines.txt`.

**Not verified (0566)**: an installer upgrade run (the upgrade text above is from the code); the H scenario, Quick
Movement off, a human player; the fail-loud path on Steam build 26577625 (ticket 0588).

### Rank 3: Quick Movement coalescing in the DLL, measured and designed (ticket 4677, D165)

impl-4677, 2026-11-17 13:22-13:47 EST. Rank 3 of `d165-pacing-fix-design.md` section 10: under Quick Movement, the
DLL would post one `GameplayUnitTeleported` per move segment instead of one per plot step (step A), and also keep only
the last dim-flag, visibility and fortify/work call per unit between publish points (step B). This subsection gives
the records each step removes, the modelled gain under three pacings, and the design. **Result: with option 4 on, the
gain left is at most about 0.1 s per M turn and probably near 0. Nothing was built.** The proposal went to lead-0437
as #4338, with an MC copy as #4339. The MC's view (#4342, 13:36): under D178, do not build it now; park steps A and
A+B as a ready idea, whose value is stock pacing and the 32-bit game; revisit it if the owner keeps option 2 off by default
at the epic's merge. Step A's code was written before that answer and stays unbuilt (below). Marks: `[C]` read in the DLL source (fork 04b1211b8), `[L]` from
4672's census runs, `[I]` model or inference.

#### Records removed `[L, 4672's census of M 151-160 (both runs post the same game), offline 2026-11-17 13:28]`

Tool `perf/turnstudy/pacing/qmove.py` (8 tests in `test_qmove.py`); outputs in `perf/evidence/0437/4677/qmove/`.
The DLL can coalesce only inside one `Update` (one AI player's turn or the turn change), per unit. The model keeps a
unit's records in posting order and ends a segment at any record of that unit outside the coalesced classes. The
prototype design below flushes before every other engine call about the unit, so this matches it. The kept record
stays at the place of the last one it replaces. The "-U" rows are the per-`Update` upper bounds, with no segment end.

Posted per M turn in the window's `Update`s: 648 teleports (0x4F), 1,373 dim flags (0x53 without flags 5; set 5 of
an embark is not a dim flag), 174 visibility (0x51), 216 fortify/work/activate (0x49), 1,742 others.

| Step | Removed per M turn (of 4,269 records) | by class |
|---|---|---|
| A: one teleport per segment (a dim flag does not end it) | **283 (6.6 %)** | teleports 283 of 648 |
| A-U | 377 | teleports 377 |
| B: A + dim flag, visibility, 0x49 last-wins per unit and class | **1,150 (27 %)** | teleports 356, dim 675, visibility 42, 0x49 77 |
| B-U | 1,248 | teleports 377, dim 731, visibility 56, 0x49 84 |

A unit's records under Quick Movement alternate teleport, dim flag, teleport, dim flag (`setXY` then `setMoves` per
step). That is why 4672's "next record of the unit is of the same type" rule found only 22 teleports per turn, while
a segment rule finds 283. The 0x49 figure is an upper bound: `GameplayUnitActivate` posts the same type, and it is an
event that step B never drops.

#### Modelled gain `[I, qbsim.py's move rule on the census records x the run's own drain frame: 25.3 ms (options 2+3) and 27.3 ms (stock) on the loaded workstation]`

| Pacing | Model passes per M turn | Step A | Step B | upper bounds A-U / B-U |
|---|---|---|---|---|
| stock (also the 32-bit game, D104): each AI player's drain | 253 | -30 passes = **-0.83 s** | -50 = **-1.36 s** | -1.13 / -1.47 s |
| options 2+3: the end-of-phase backlog | 180 | -5 = -0.13 s | -16 = -0.41 s | -0.17 / -0.46 s |
| options 2+3+4: the drain is 2 pumps (4675) | 2 | the dispatch loop's cost of the dropped records only | | |

*Correction to 4672's what-ifs (2026-11-17, impl-4677):* "-1.1 s alone ... [-2.1 s]" for rank 3 (section 10's
table) kept the last record per unit across the whole drain. That is a window the DLL cannot reach, because a drain
holds the records of many `Update`s. Within one `Update` and one segment, the figures are the ones above.

**With option 4** there is no frame left to save. 4672's NNLS fit of the dispatch loop with options 2+3 (1,632
pumps, R2 0.96) gives 0 ms per record for 0x4F, 0x53 and 0x49 and 0.03 ms for 0x51, so the saving is about 0 s.
Taking stock pacing's teleport fit as a bound (0.31 ms per record, R2 0.73) gives A <= 0.09 s and B <= 0.12 s per M
turn, against 5.87 s per turn with 2+3+4 (4675, one loaded run). Proxy transport is 0.6-0.8 us per call: about 1 ms
per turn. The DLL's own cost of the dropped calls (a `CvDllUnit` wrapper and one engine call each) is not measured.
**With Quick Movement off** the steps do nothing: moves are animated `GameplayUnitMoved` records (`QueueMoveForVisualization`,
already one per segment of up to 20 plots), and 4675's Quick Movement guard keeps option 4 off then.

#### The DLL's side and the design (not built) `[C, 04b1211b8]`

- **Who posts what.** `CvUnit::setXY` (CvUnit.cpp 20986-21003) calls `SetPosition` (29832: `GameplayUnitTeleported`)
  for every plot step when `CvPreGame::quickMovement()`, else `QueueMoveForVisualization`. `setMoves` (21668) posts a
  dim flag at every change of the unit's moves. Under Quick Movement `UnitMove` never publishes (30498:
  `bExecuteMove = false`), and `m_unitMoveLocs` stays empty. `SetPosition` has one other caller (CvPlayer.cpp 13178,
  in `receiveGoody`: a unit placed by a goody hut).
- **Step A.** In `setXY`'s Quick Movement branch, the plot goes into a new per-unit member (pending teleport) instead
  of `SetPosition`. It is transient: not serialised, and not part of `HasQueuedVisualizationMoves()`. That function
  decides game logic in `CvUnitMission::ContinueMission` (CvUnitMission.cpp 364), so counting the new member there
  would change the game.
- **Points that must post the pending teleport:**
  - The existing `PublishQueuedVisualizationMoves` calls: embark and disembark in `CvUnit::move` (5465, 5472), combat
    (CvUnitCombat 549, 1324, 3546), mission end (`ContinueMission`, CvUnitMission 935), `finishMoves` (21750),
    `ClearMissionQueue` (31256), `UnitAttackWithMove` (30422), `UnitPathTo`'s reject (30589) and `DoFallBack` (31984,
    31991).
  - Before every other engine call about the unit: about 65 `Gameplay*` call sites in CvUnit, CvUnitCombat,
    CvUnitMission, CvPlot, CvCity and CvTeam. The ones that matter most are a unit coming into or leaving view
    (`GameplayUnitVisibility`), destroyed, damage, activate, fortify, work, garrison and embark. The DLL calls the
    engine's vtable directly, but every one of these calls first builds a `CvDllUnit` wrapper of the unit. That
    wrapper's constructor is therefore the one central flush point. `setMoves` suppresses the flush around its dim
    flag's wrapper, so that dim flags do not end a segment.
  - At the end of every DLL `Update`. This covers the turn's end and the autosave (it runs inside the turn-change
    `Update`). Because queue B is pumped only between `Update`s (`game-core-thread.md` 6.3), no frame ever sees a
    stale position.
  - On the server, EXE calls into the DLL run on another service thread and overlap the `Update` (4665: 0.08 s per M
    turn) [I]. A flush must therefore never run from such a call (a thread check), or the pending member races. This
    also covers the human's own commands: if they reach the DLL outside `Update`, they post as stock.
- **Step B** adds pending dim, visibility (with its arguments) and fortify/work values per unit, posted at the same
  points in the original call order of the last values. `GameplayUnitActivate` is an event and is never coalesced.
- **What the player sees.**
  - Stock pacing: a unit that crossed several plots used to hop one plot per frame, because queue B's rule lets one
    keyed record per unit through per frame. With step A it jumps once per segment.
  - Options 2+3, or 2+3+4: the same final map after the burst.
  - Nothing differs at a frame boundary.
- **Identity.** Every dropped call is a void engine call, so stats.db is expected IDENTICAL [I]. That is untested.
- **Step A as code: written, NOT compiled or run.** It is on fork branch `x64-proto-0437-qmove`, head `dc8b2f062`
  (pushed to the fork's remote), from 04b1211b8. It is 106 lines in four files behind
  its own define `VP64_PROTO_QMOVE_COALESCE`, separate from 4676's embark define:
  - `CvUnit.h`: the transient member and five functions.
  - `CvUnit.cpp`: `reset` clears the member; `setXY`'s Quick Movement branch defers; `setMoves` suppresses the flush;
    the flush, and the end-of-`Update` loop over every player's units.
  - `CvDllUnit.cpp`: the flush in the constructor.
  - `CvDllGame.cpp`: `Update` begins and ends the deferral.

  Only the thread running `Update` defers; any other thread posts as stock. `VP64_PROTO_QMOVE_MODE=0` turns it off at
  run time. Step B would add pending dim, visibility and fortify/work values in the same places.

#### Not verified (4677)

- Every gain figure is a model on the workstation's loaded drain frame (D106). The record counts do not depend on the
  machine.
- The dispatch cost of teleports and dim flags: the two NNLS fits disagree (0 against 0.31 ms per teleport).
- Scenario H, a human game, and Quick Combat off.
- The DLL's own saving from the dropped calls.
- Whether flushing before every unit call keeps the EXE's model state exact. Static reading only; nothing ran.
- Step A's code: it was not compiled. The code also flushes when a wrapper is built for a purpose other than a
  posted call, so its segments can only be shorter than the model's. Its count of removed teleports is therefore at
  most the model's 283 per M turn; a census of a built prototype would give the real figure.

### Quick Movement off: the animated moves of units player 0 cannot see (ticket 4678, D165)

impl-4678, 2026-11-17 13:51 onwards, workstation. MC #4353: the Quick Movement OFF case is likely the case most players
meet. 4675 measured it at 28.68 s per M turn with options 2+3 (end-of-phase drain 29.6 s, 1,913 `GameplayUnitMoved`
records held in 5 turns), in an autoplay run whose observer sees every plot. This subsection says how much of that a
human player 0 would meet. Marks: `[C]` read in the DLL source (fork 04b1211b8), `[L]` seen live, `[I]` inferred.
Evidence `perf/evidence/0437/4678/`; raw runs `F:/c5-64bit-work/4678/ws/`.

**In one paragraph.** The DLL already animates a move step only when a watching human can see its old or new plot;
other steps are teleports. The study's autoplay observer sees every plot, so in the study's Quick Movement off arms
every AI step is animated. With the observer following player 0's view (4676's switch), the DLL's own rule acts as it
would for a human player 0. **The animated moves fall from 278 to 83 per M turn. The turn falls from 29.9 s to 12.2 s
with options 2+3, and from 73.3 s to 34.0 s at stock pacing (one run each on the loaded workstation, an
indication).** The game is IDENTICAL. What stays
animated but unseen is mostly trade units, whose steps skip the visibility rule. They cost about 0 with options 2+3,
and about 5.5 s per M turn at stock pacing (model).

#### What the DLL does `[C, 04b1211b8]`

- `CvUnit::UnitMove` calls `move(plot, bShow = (distance == 1), ...)` (CvUnit.cpp 30483). `move` calls `setXY(x, y,
  true, true, bShow && new plot isVisibleToWatchingHuman(), bShow)` (5496, 5509). In `setXY`, with graphics up, the
  step is queued for animation (`QueueMoveForVisualization`, published as one `GameplayUnitMoved` per segment of up to
  20 plots) when its old or new plot is visible to a watching human and Quick Movement is off. Otherwise it is a
  `GameplayUnitTeleported` (`SetPosition`) (20985-21003). `SetPosition` also clears the unit's queued steps without
  publishing them (29840): a unit that walks out of view jumps from its last published point.
- `CvPlot::isVisibleToWatchingHuman` (CvPlot.cpp 4736) asks every alive human player and every observer slot whose
  claim is ASSIGNED. Autoplay's observer is one: `setAIAutoPlay` sets its claim ASSIGNED (CvGame.cpp 5549). Without
  an observer UI override player it calls `SetAllPlotsVisible` for the observer's team (5511-5513), so every plot is
  visible to a watching human. With `Game.SetObserverUIOverridePlayer(0)` before `SetAIAutoPlay`, the observer's
  team copies player 0's visibility (5514-5532) and keeps mirroring it (CvPlot.cpp 11700; 4676 checked 282 of 284
  views equal).
- **Steps animated whatever anyone sees:**
  - A trade unit's step: `CvGameTrade::StepUnit` calls `setXY(..., true, false, true, true)`, with bShow true and no
    visibility check (CvTradeClasses.cpp 2187). `MoveUnit`'s `finishMoves` publishes the steps (2100, CvUnit.cpp 21750).
  - `DoFallBack` (CvUnit.cpp 31970, 31983).
  - The attacker's return after a combat (CvUnitCombat.cpp 3545).
  - A paradrop whose plot the active team cannot see (9021).
- Game logic reads the queue at one place: `CvUnitMission::ContinueMission` tests `!HasQueuedVisualizationMoves()`
  (CvUnitMission.cpp 364). That branch acts only for automated units and human interrupts. The identity check below
  found no game effect of the switch.

#### The runs `[L, 2026-11-17 14:02-14:38 EST, workstation, shared (12 jobs of other owners registered: 0463's games, 0496's load jobs, a server, 4676's DLL build), c5h-a (claimed as impl-4678 under 0437), window cloaked, arm S4d (4675's build b2), M from TS0438-M-T150 by hash, window 151-155, Quick Movement turned off by the runner before the arm (read back false after it), Quick Combat on, statistics logging on, poller with the census, one run per arm: indication only (D106)]`

Driver `perf/turnstudy/pacing/movevis.py` (4661's workstation run plus a read-only `GameEvents.UnitSetXY` listener; the
pattern is 4676's `embarkvis.py`). For every `setXY` it records the old and new plot, `Plot:IsVisibleToWatchingHuman()`
at both, `Plot:IsVisible(player 0's team)` at both, `Unit:IsTrade()` and whether the unit is player 0's team's.
`--follow0` sets `Game.SetObserverUIOverridePlayer(0)` before the arm (read back 0). Analysis
`perf/turnstudy/pacing/moveshare.py`, tests `test_moveshare.py` (7 pass). Variants: `M:pacing_qmoff_drain0` (options
2+3, as 4675) and the new `M:pacing_qmoff_skip_off` (stock pacing: the 32-bit game's pacing, D104). Script
`perf/evidence/0437/4678/runset.sh`. All four runs exit 0; no dialog box.

Each `GameplayUnitMoved` (0x4D) record is classed by its unit's one-plot steps in the same game turn:

- own: player 0's team's unit.
- seen: a step touched a plot player 0's team could see.
- hidden: no step did.
- trade_seen / trade_hidden: the same split for a trade unit.

The class is per unit-turn. A unit with seen and hidden segments in one turn counts as seen [I].

The listener saw the same 4,171 `setXY` calls in every run (3,979 one-plot steps in 5 turns): own 530, seen 253, hidden
2,815, trade seen 22, trade hidden 359.

| per M turn (151-155) | options 2+3, observer sees all (the study's arm) | options 2+3, observer follows player 0 | stock pacing, observer sees all | stock pacing, observer follows player 0 |
|---|---|---|---|---|
| `GameplayUnitMoved` posted | 278.2 | **82.6** | 278.2 | 82.6 |
| of them: own / seen / hidden / trade seen / trade hidden | 13.1 / 10.1 / **69.2** / 0.9 / **6.8 %** | 44.1 / 28.8 / 1.2 / 2.9 / **23.0 %** | as 2+3 (the same game) | as 2+3 |
| held (the EXE animates them) | all 1,391 | all 413 | all 1,391 | all 413 |
| seconds per turn (pacing.py; no claim) | 29.86 | **12.25** | 73.35 | **34.03** |
| drain (2+3: end of phase, median; stock: D_B per turn) | 24.2 s | **9.47 s** | 67.40 s | 29.06 s |
| AI drains (stock) whose animated moves are all hidden | - | - | 67 of 120: 37.40 s per turn | 16 of 120 (15 only hidden trade units): 6.40 s per turn |
| statscmp (15 tables, 22,351 rows) | - | IDENTICAL to the observer run | IDENTICAL to the 2+3 observer run | IDENTICAL to the stock observer run |

- **So 76 % of the animated moves in the study's Quick Movement off arm (69.2 % hidden plus 6.8 % hidden trade units)
  belong to units player 0 cannot see**, and the DLL already teleports all of them except the trade units. The
  study's 28.7 s per M turn (4675) is the observer's case. A human player with Quick Movement off waits about 12 s
  on this machine with options 2+3, and about 34 s at stock pacing (one run each, a loaded workstation: an
  indication, D106).
- Even the follow-0 figure is high for a human game. In autoplay, player 0 is played by the AI inside the AI phase, so
  its own units' moves (44 % of the 83) fall in the drain. A human moves them in his own turn instead [I].
- **What the EXE does with a hidden unit's move** `[L]`: every hidden trade unit's `GameplayUnitMoved` was held (95 of
  95 in the follow-0 runs). The EXE plays the move's animation, and holds the unit's later records for its length,
  although the unit stands on plots the watching team cannot see. Stock pacing shows the cost: in each of players 1, 3
  and 4's turns the only animated moves were 3-6 hidden trade units. Those drains took 1.8-3.4 s (median 1.97 s), the
  records dispatched in the first 30-50 ms. AI drains without any animated move took a median 0.28 s.
- **Options 2+3, follow 0: the hidden trade moves do not set the drain's end.** In each of the 5 turns, the last
  hidden trade unit's move was dispatched 1.9-3.8 s into the drain. The last seen move was dispatched 6.1-9.2 s in,
  and the drain ended at 7.6-10.6 s.

#### The remedy and its modelled gain (a candidate, not built) `[I]`

Proposed to lead-0437 as #4387 (MC copy #4390). The MC's answer (#4403, 2026-11-17): not now. The study takes options
2+3 to defaults, and there the remedy is worth about 0. It stays a recorded candidate (the `StepUnit` visibility gap,
the define's shape below, the indication), to revisit only if options 2+3 do not become defaults. No fork branch was
made.

`VP64_PROTO_QMOFF_HIDDEN_TRADE`, on a fork branch from 04b1211b8, in `CvGameTrade::StepUnit`: give a trade unit's step
the rule `CvUnit::move` already has. A step whose old and new plots no watching human can see first publishes the
unit's queued visible steps, so the walk to the edge of view is kept. It is then a teleport (`setXY(..., bShow false,
bCheckPlotVisible false)`). Any other step stays as stock. With Quick Movement on nothing changes: `setXY` already
teleports, and nothing is queued then. About 10 lines in one file.

| Pacing (Quick Movement off, player 0's view) | Gain per M turn |
|---|---|
| Stock (the 32-bit game, D104) | **about -5.5 s of 34.0 s.** 16 of 120 AI drains in 5 turns had only hidden moves (15 only hidden trade units): 6.40 s per turn. Without them they would take about the 0.28 s of a drain without an animated move. |
| Options 2+3 | about 0: the drain's end is set by moves player 0 sees. |
| Options 2+3+4 | option 4 does not act with Quick Movement off (4675's guard). |

Risks:

- A trade unit coming into view: its first visible step is animated as stock. The one plot between the last hidden
  and first visible step is a jump the player does not see.
- Leaving view: the walk to the edge, then the jump.
- Combat and cargo: trade units do not fight and are not cargo; their capture is a separate destroy record.
- Hot seat and multiplayer: every human's view counts (`isVisibleToWatchingHuman`).
- The server's unit feed already receives teleports.

What the player sees: nothing different for trade units out of view, which are not drawn.

The other always-animated paths (fallback, the attacker's return, paradrop) were 5 records in 5 turns. They are not
worth a define.

#### Result of the laptop series with Quick Movement off (D178) `[L, 2026-11-17 15:37-16:32 EST, laptop, job 20261117-153718-32ae, series 0437-M-qmoff-f0-d178, submitted by lead-0437 (seventh lead) from 04601485a, analysed 16:33 by lead-0437 (eighth lead); grade B (D178: n=3 per arm, no default-on claim); GPU drivers Intel 27.20.100.9320, NVIDIA 32.0.15.7322 (D175), Windows 10.0.19041.6216]`

MC #4431's series for the owner's QM-off number. Arm S4d (4675's build b2) with Quick Movement off and the autoplay observer
following player 0 (`observer_follow` 0, read back 0 in all 18 reads), M 151-160 (10 turns per run), 3 runs per arm,
Williams order, quiet (per-arm foreign cores 0.065). Variants `f0_qmoff_stock` (stock pacing) and `f0_qmoff_skip1_tick0`
(options 2+3) in `perf/turnstudy/runner/scenarios/M.json`. Analysis copy `perf/evidence/0437/d178/laptop-32ae-qmoff-f0-analysis.txt`;
full fetch `F:/c5-64bit-work/0437/0437-M-qmoff-f0-32ae`.

| Arm | s per M turn (95 % CI) | CPU s/turn | server CPU s/turn | growth MB/turn | peak private MB |
|---|---|---|---|---|---|
| stock pacing | **37.60** [37.29, 37.91] | 63.9 | 7.82 | 2.78 | 1670 |
| options 2+3 | **14.75** [14.60, 14.91] | 28.9 | 4.37 | 0.66 | 1638 |

- **Options 2+3 save 22.85 s per M turn with Quick Movement off (-60.8 %; stock / 2+3 ratio 2.549 [2.527, 2.572])**,
  95 % CI of the difference [22.59, 23.11] s. stats.db IDENTICAL between the arms over turns 150-162 and within each arm.
  *Superseded as product behaviour 2026-11-23 by ticket 0718 (D246). With Quick Movement off, option 2 no longer acts:
  its gain is the overlap of animated moves that the owner rejected. With today's proxy this arm runs at stock pacing,
  plus option 3. The numbers stay valid for the build they were taken with.*
- Against Quick Movement on (job 9d65, M 151-165, observer sees every plot: stock 14.49 s, 2+3 9.16 s; cross-series,
  different window and observer, an indication only): Quick Movement off costs about 23 s per M turn under stock pacing
  and about 5.6 s with options 2+3 [I].
- The workstation's one-run indications (34.0 s and 12.25 s, 4678) had the same order; the laptop is about 10-20 % slower
  per turn on this load.
- Not settled here: option 4 (AiDrainUnordered) with Quick Movement off, and rank 2 (4676's hidden embarks), whose
  QM-off pairs (ab01 with options 2+3, bf69 with stock pacing) are queued behind this series.

#### Not verified (4678)

- Every time figure: one run per arm on a loaded, shared workstation (D106). The gain is a model: the drains of
  hidden-only players replaced by the median of drains without animated moves. A built prototype with a laptop pair
  decides.
- The class is per unit-turn, not per record. A record's own path is not read (0x4D records are all 1,168 bytes: the
  path lives in a vector the census does not follow).
- The listener reads visibility at the end of `setXY`, after the DLL's check. The two can differ only for a unit of
  the watching team [I].
- Scenario H, a human game (the follow-0 observer stands in for one), and Quick Combat off (combats hold records too).

### Embarks the human cannot see: the share and what they cost (ticket 4676, D165 rank 2)

impl-4676, 2026-11-17 13:10-15:30 EST, workstation. Rank 2 of `d165-pacing-fix-design.md` section 10: do not post
`GameplayUnitEmbark` (six keyed records with flags 0x5 that pass one per frame) for a unit the human cannot see, and
send its state when it comes into view. This subsection is the measurement, the proposal (MC #4314: a proposal before
any build) and, after the yes, the DLL prototype with its identity runs, census, pictures and laptop pairs. Marks: `[C]` read in the DLL's source (fork 04b1211b8,
the fork's x64 worktree), `[L]` seen live, `[I]` inferred or modelled. Evidence
`perf/evidence/0437/4676/`; raw runs `F:/c5-64bit-work/4676/ws/`.

**In one paragraph.** In the study's runs nobody is hidden from the active team: autoplay makes the active player an
observer that sees every plot. From player 0's view (what a human watching the AI phase would see), **78 % of the embark
sets in the AI phase's backlog are of units player 0's team cannot see** (90 % of the AI units' embarks), and 74 % stay
out of view through the turn. Dropping them saves **134 of the 180 drain frames per M turn: about -3.9 s on top of
options 2+3 at the laptop's frame** (model). They are the same frames option 4 (`AiDrainUnordered`, 4675) removes: with
option 4 acting, rank 2 saves only their dispatch, about -0.03 s per turn. Without option 4 (off, refused under Quick
Movement off or a hold) and in stock pacing (the 32-bit game's case: about -3.1 s per M turn) it keeps its value.

#### Autoplay's observer sees every plot `[C; L]`

- `CvGame::setAIAutoPlay` (CvGame.cpp 5428-5536) [C]: on activation it moves the active player to an observer slot (a
  free observer slot, else a closed major slot made one), `setActivePlayer(observer)`, and with no observer UI override
  player calls `SetAllPlotsVisible(observer's team)` (CvGame.cpp 501-519: every plot's visibility count +1 for that
  team, revealed). With an override player set (`Game.SetObserverUIOverridePlayer`, Lua), it copies that player's
  team's visibility and revealed state to the observer instead (5515-5532), and `CvPlot::changeVisibilityCount` /
  `setRevealed` mirror every later change of that team to the observer (CvPlot.cpp 11700-11705, 12430-12435).
- Live [L, run below]: before `Game.SetAIAutoPlay(13, 0)` the active player is 0 (team 0); 3 s after it the active
  player is 8, team 21, `IsObserver()` true, override player -1. The listener found the active team seeing 236 of 236
  matched embark plots.
- So a remedy keyed on the active team's view never acts in any autoplay arm of the study, and an arm that is to show
  it needs the observer to follow player 0 (the override; checked live below).

#### The instrument (`perf/turnstudy/pacing/embarkvis.py`, `embarkshare.py`)

- **Where an embark is posted** [C]: `CvUnit::move` (CvUnit.cpp 5444-5476) calls `embark(pOldPlot)` or
  `disembark(pOldPlot)` before `setXY` moves the unit; both post `GameplayUnitEmbark` unconditionally (7108-7140);
  `jumpToNearestValidPlot` (5678, 5767) embarks and then calls `setXY`. `setXY` ends with the GameEvents hook
  `UnitSetXY(owner, id, x, y)` (21242-21253). Other posters: `setupGraphical` at a load (1773), a captured embarked
  unit (2873), `CvTeam::UpdateEmbarkGraphics` at an embark tech (CvTeam.cpp 3982).
- **The listener** (InGame Lua state, installed through the Lua channel before the arm; getters only): keeps every
  unit's embarked flag and plot (453 units at install); at each `UnitSetXY` of a unit whose flag changed it records an
  embark at the previous plot with the active team's and player 0's team's `IsVisible` of that plot, player 0's view of
  the new plot, and whether the unit is player 0's team's; a unit player 0's team could not see is then followed until
  it moves onto a plot player 0's team sees (`V`), its plot is visible at player 0's next `PlayerDoTurn` (`W`) or it
  leaves the map (`K`). Read back after the window (311 rows, 0 listener errors).
- **The join** (`embarkshare.py`): each window turn's backlog at the AI phase's end (`qbsim.py --mode phase`; or each AI
  player's drain, `--mode player`), its embark sets (the six types with flags 0x5, by owner and unit) joined to the
  listener's row of the same unit and turn; then `qbsim.passes` with the hidden sets removed. Tests
  `perf/turnstudy/pacing/test_embarkshare.py` (20; negatives for every refusal of the row readers).
- **Cost of the listener**: one Lua call per `setXY` with a few calls into the DLL, inside U: U was 4.41 s per turn
  against 3.86 s in 4672's run (one loaded run each: not a measurement of the listener). It changes nothing in the
  game [L]: statscmp of this run against 4672's census run of the same arm: **IDENTICAL**, 15 tables, 39,246 rows
  (`statscmp-4672-census-vs-4676-listener.txt`).

#### The run `[L, 2026-11-17 13:29-13:36 EST, workstation, shared (13 heavy jobs of other owners at the start: games, builds, suites), c5h-b (claimed as impl-4676 under 0437), window cloaked, arm S4s:pacing_skip1_tick0 (4665's build b2, options 2+3), M from TS0438-M-T150 by hash, window 151-160, statistics logging on, poller with the census, one run: functional, an indication (D106)]`

`C:/c5-64bit-work/4676/runset.sh` (copy in the evidence); exit 0, restored, `provision.py verify` rc 0, no dialog. The
poll: 9.91 s per turn, U 4.41, D_B 5.17 (end-of-phase backlog 473 KB, drained in 4.79 s), model passes 180.4 per turn
against 180.5 pumps seen.

**Embark sets in the AI phase's backlogs** (10 turns; `share/embarkshare-ev-S4s-pacing_skip1_tick0-phase.txt`):

| Class | sets | share | per turn |
|---|---|---|---|
| hidden from player 0's team, out of view through the turn and the window | 134 | 55.4 % | 13.4 |
| hidden, the unit left the map before it came into view | 45 | 18.6 % | 4.5 |
| hidden, came into view in a later turn | 9 | 3.7 % | 0.9 |
| hidden, came into view in the same turn (a deferral posts it later in the same drain) | 1 | 0.4 % | 0.1 |
| seen by player 0's team | 22 | 9.1 % | 2.2 |
| player 0's own units (autoplay moves them as AI in the AI phase; a human would move them in his own turn) | 25 | 10.3 % | 2.5 |
| no listener row (load, capture, embark tech, or a unit the listener did not know) | 6 | 2.5 % | 0.6 |
| **all** | **242** | | **24.2** |

**What they cost** [I, model: `qbsim.py`'s rule on this run's records; seconds at this run's 28.7 ms per drain frame and
at the laptop's 28.9 ms = the laptop poll's 5.22 s end drain of options 2+3 / this run's 180.4 model passes]:

| What-if (per M turn) | frames | this run | laptop |
|---|---|---|---|
| hidden embark sets never posted | -133.8 | -3.83 s | **-3.87 s** |
| hidden sets that stay out of view through the turn never posted (a deferral flushed on coming into view) | -133.1 | -3.81 s | -3.85 s |
| 4672's stand-in "AI units only" | -153.9 | -4.41 s | -4.45 s |
| 4672's upper bound: every embark set never posted | -162.7 | -4.66 s | -4.71 s |
| with option 4 acting (the drain is 2 frames, 4675): the dropped records' dispatch only (4672's per-record cost: 0x45 0.75 ms, 0x50 0.30, 0x4A 0.43, the others 0) | - | -0.028 s | |

- **Stock pacing** (each AI player's drain; 4672's census run `ws-S4s-pacing_skip_off`, the same game: the same units
  embark in the same turns; `share/embarkshare-ws-S4s-pacing_skip_off-player.txt`): hidden sets never posted -114.5 of
  253.1 frames per turn = **-3.1 s per M turn** at that run's 27.3 ms. This is the case of the 32-bit game, where the
  proxy's options do not exist [I: the 32-bit EXE drains the same queue with the same rule; its frame time is not the
  64-bit client's].
- **Quick Movement off** (4675's census run `ws-S4d-pacing_qmoff_drain0`, options 2+3, turns 151-156, the same game:
  per-turn classes equal to this run's): the same 73 % of the sets are hidden and out of view; the model gives -120 of
  164 passes, but that drain is held by animations (244 pumps seen, 0.10 s per frame, 4675) and the model has no holds,
  so no seconds are given.

#### Proposal (posted to lead-0437 as #4346, copy to the MC #4347, 13:44)

**(ii), the DLL**, behind a define `VP64_PROTO_HIDDEN_EMBARK` on fork branch `x64-proto-0437-embark` from 04b1211b8; not
(i) the proxy. Reasons: the DLL knows visibility exactly when it posts and already has the rule,
`CvPlot::isVisibleToWatchingHuman` (any living human or assigned observer; CvPlot.cpp 4730-4745; `setXY` uses it to decide
whether a move is animated, CvUnit.cpp 20974-20977), which gives the same answer on every machine; no extra IPC; and it
helps the 32-bit game (D104). The proxy variant (i) would need per-call unit identity and plot callbacks across the IPC
(the EXE borrows the unit argument only for the call, `proxy_rt.cpp` `kBorrowedArgMethods`), a unit-to-plot map kept from
teleports and animated moves, a re-acquired handle (`ICvPlayer1::GetUnit`) at the flush, and the FOW calls as the only
sign of a reveal (which the DLL may defer, `updateFog(true)`).

Design of (ii) [I, not built]: `embark()`/`disembark()` post only when the plot is visible to a watching human or the unit
is a watching human's own; otherwise a transient member (not serialised) keeps the model state the EXE has. Flush (post
`GameplayUnitEmbark(isEmbarked())` when it differs from what the EXE has): in `setXY` before `SetPosition` /
`QueueMoveForVisualization` when the new plot is visible to a watching human (queued moves published first); in
`CvPlot::changeVisibilityCount` when a plot turns visible for a watching human's team (beside its `updateFog`), for the
units on it; for both units before a combat's visualisation (beside `CvUnitCombat`'s `GameplayUnitVisibility` calls); for
every unit at an active-player change (`CvMap::updateFog`). Dropped at the unit's death; nothing at a load
(`setupGraphical` posts the true state). A verify counter: a plot revealed to a watching human with a unit still pending
(must be 0). Risks: a reveal path not covered shows a land model on water until the unit's next embark change; combat;
hotseat and multiplayer (the union of the humans' views). What the player sees: nothing different out of sight; a unit
coming into view with its true model. **Answer:** the MC's yes (#4348: arms with Quick Movement on and off, under options
2+3 and stock pacing, because option 4 acts only with Quick Movement on) and the lead's yes (#4350: the conditions of
`x64-count-0437`; the watching team said exactly; identity headless and windowed; census; pictures; laptop pairs
prepared).

#### The prototype as built `[C, 2026-11-17]`

Fork branch **`x64-proto-0437-embark`**, one commit on the pin 04b1211b8: build 2 **`3fd5255a1`** (pushed to the
project's fork, 2026-11-17 15:00; build 1 was `eef1a99d0`, amended before any push) (clone `C:/c5-64bit-work/4676/fork`
of the local fork clone; message in `perf/evidence/0437/4676/msg-fork.txt`). New files
`CvGameCoreDLL_Expansion2/VP64ProtoHiddenEmbark.h` and `.inl` (included at the end of `CvUnit.cpp`); hooks, each inside
`#if defined(VP64_PROTO_HIDDEN_EMBARK)`, in `CvUnit::embark/disembark` (the call is not posted when `OnEmbark` defers it),
`CvUnit::setXY` (`OnMove`, at the top of the graphics block, before `SetPosition` / `QueueMoveForVisualization`),
`CvUnit::kill` (`OnUnitGone`), `CvPlot::changeVisibilityCount` (`OnPlotVisible`, in the branch "could not see the plot
before but can now"), `CvCombatInfo::setVisualizeCombat(true)` (`OnCombatVisualized`: the attacker, defender and
interceptor; every visualised combat passes there), `CvPlayer::setTurnActive(false)` (`Sweep`), and the PCH include. 300
lines added, none removed.

- **Who watches** [C]: `CvPlot::isVisibleToWatchingHuman` (CvPlot.cpp 4730-4745): any living human player's team, or a
  slot that is `SS_OBSERVER` with `SLOTCLAIM_ASSIGNED` (the observer autoplay creates). A unit whose team has a human
  (`CvTeam::isHuman`) is never deferred. In the study's autoplay arms the observer sees every plot, so nothing is
  deferred unless the observer follows player 0 (`observer_follow 0`: `Game.SetObserverUIOverridePlayer(0)` before
  `Game.SetAIAutoPlay`; the observer then sees what player 0's team sees: 282 of 284 embark plots equal [L], and the game
  is IDENTICAL with and without it [L, `statscmp-listener-vs-follow0.txt`, 39,246 rows]).
- **State**: a map (owner, id) -> the model state the EXE has (embarked or not), not serialised; a flush posts
  `GameplayUnitEmbark(unit, isEmbarked())` **always** (build 2; build 1 posted only when the state differed, which kept
  the load's model with its old facing, below), and counts apart the flushes whose state the EXE already had; a watched call removes the entry
  (it carries the true state). Nothing at a load (`setupGraphical` posts every unit's true state).
- **Counters** per game turn in `%VP64_PROTO_HIDDEN_EMBARK_DIR%\HiddenEmbark.csv` (`perf/turnstudy/pacing/hecount.py`
  sums a window; exit 3 on a missed reveal = a unit found pending on a watched plot by the sweep). Switch
  `VP64_PROTO_HIDDEN_EMBARK_MODE` = `on` (default) / `off` (posts as stock, counts what `on` would defer).
- **Guard check** (`perf/evidence/0437/4674/guardcheck.py`, 4674's, with `--define VP64_PROTO_HIDDEN_EMBARK`): OK, 5
  files modified, 2 new files included only inside the define; negative (the same check against `VP64_PROTO_REACH`)
  fails with 7 problems (`guardcheck.txt`, `guardcheck-negative.txt`).
- **Builds** (`C:/c5-64bit-work/4676/build_proto.sh`: `tools/x64-dll/build_all.py --config release --offline
  --unlocked` with `CL=-DVP64_PROTO_HIDDEN_EMBARK`; every step rc 0, narrowing gate 794 against 798 (0 new), smoke host
  ok): build 1 (13:54-14:07 EST, "Release-5.4.8-52-geef1a99d07 Clean") DLL
  `3445F8455A8672D31FA6CAFE5BC835BBB28496D2951DFF10B316115DFE80CB28`, superseded; **build 2** (14:45-14:56,
  "Release-5.4.8-52-g3fd5255a19 Clean") DLL **`A3CB17A91FB9C723174DE4479179E2C8D5A0A884ECD9BD4D9EB4CEA69DFAAA19`**
  (23,677,952 B each; `build-proto-b1.txt`, `build-proto-b2.txt`). The guard check is OK on both commits.
- **Arm S4e** (`perf/turnstudy/runner/arms.json`, flag `hidden_embark_proto`): 4675's S4d with the x64 DLL replaced;
  because the proxy's pack check (pack=1) compares the x64 DLL and the purity file with `manifest.json`
  (`proxy_pack.cpp` 600-620), the layout `F:/c5-64bit-work/4676/layout/VP64` carries its own `manifest.json` (the DLL's
  and the purity file's hashes) and `purity.tsv` = the RC's 143 rows bound to this DLL (`proxy/tools/derive_purity.py`
  keeps all 166 audited rows for the prototype's source, so each of the 143 holds; `make_pack_files.py`). The runner
  refuses any file in the layout folder that the RC does not have (the PDB lies one level up).
- **Runner** (separate commits): variant keys `hidden_embark` (on/off into the EXE's environment, the counters' folder
  `<run>/hidden-embark`; required on S4e, refused elsewhere) and `observer_follow` (channel client arms only); 18 M
  variants `he_{off,on}_[qmoff_]{skip1_tick0,stock}`, `he_{off,on}_drain1`, and their `pacing_` forms. Tests
  `perf/tests/turnstudy/test_hiddenembark.py` (8, negatives for every refusal), `perf/turnstudy/pacing/test_embarkshare.py`
  (23, with `hecount.py`'s), the turnstudy suite 286 pass (2026-11-17 14:10).

#### Identity, counters and the census `[L, 2026-11-17, workstation, shared (13-15 heavy jobs of other owners at each start), c5h-b (impl-4676 under 0437), one run per arm: functional, indications (D106)]`

- **Headless S3** (14:15, `run_he.sh M s3-on on`, the pin layout's server with the prototype DLL, M 151-160):
  **IDENTICAL** to the M series' `r01-p4-S3` and to 4668's m1 (15 tables, 32,631 rows each). Counters: 253 calls, all
  posted at once (the headless observer sees every plot), 0 deferred. Build 2 (14:57, `s3-on2`): IDENTICAL to both
  again, the same counters (`perf/evidence/0437/4676/s3-on`, `s3-on2`).
- **Windowed, Quick Movement on, options 2+3** (`S4e:pacing_he_off_skip1_tick0` 14:18-14:25 and
  `pacing_he_on_skip1_tick0` 14:25-14:31, both with the observer following player 0, the poller with the census, the
  embark listener): **IDENTICAL** (15 tables, 39,246 rows, `statscmp-ws-he-off-vs-on-qmon.txt`). Counters over 151-160:
  239 calls; off: 51 watched, 188 would defer (79 %, the listener's 78 %); on: **188 deferred**, flushed and posted 5
  (move 3, reveal 2, combat 0), came into view with the state the EXE had 5, dropped with their unit 18, **missed reveals
  0**, at most 65 pending.
- **Census** (`embarkshare.py` on both runs): embark sets in the AI phase's end backlog **24.2 -> 5.6 per turn**; drain
  frames **180.5 -> 48.2 per turn** (the model: 180.4 -> 48.0); queue B's end drain (medians) **4.06 s -> 2.30 s**, the
  poll's turn 7.65 -> 6.09 s, D_B 4.12 -> 2.37 s (one loaded run each: not a timing claim). The frames removed are the
  drain's cheap tail frames (about 13 ms each here, against the 23 ms mean of the off arm's drain frames), so the model's
  "frames x mean frame" (-3.8 s on this machine, -3.9 s at the laptop's frame) overstates the seconds; the laptop pair
  decides.
- These windowed runs are **build 1** (DLL 3445F845, fork `eef1a99d0`, unpushed and amended); evidence
  `perf/evidence/0437/4676/b1-*`. **Build 2** (DLL A3CB17A9; `S4e:pacing_he_off_skip1_tick0` 14:58-15:05,
  `pacing_he_on_skip1_tick0` 15:05-15:10, the same set-up): **IDENTICAL** (15 tables, 39,246 rows; and its off arm
  IDENTICAL to build 1's); 188 of 239 calls deferred, flushed and posted 10 (move 6, reveal 4; 5 of them with the state
  the EXE had), dropped 18, never posted in the window 160, **missed reveals 0**, at most 65 pending; embark sets in the
  end backlog **24.2 -> 6.0 per turn**, drain frames **179.0 -> 51.6**, end drain **3.20 s -> 2.22 s**, the poll's turn
  7.04 -> 5.73 s (one loaded run each; evidence `b2-ws-he-*-qmon`).
- **Build 2, Quick Movement off**, options 2+3, the observer following player 0 (`pacing_he_off_qmoff_skip1_tick0`
  15:10-15:18, `pacing_he_on_qmoff_skip1_tick0` 15:18-15:25): **IDENTICAL** (15 tables, 39,246 rows); the same counters
  as with Quick Movement on (188 deferred, 10 flushed, 0 missed reveals); embark sets in the end backlog 24.2 -> 6.0 per
  turn; pumps seen 224.2 -> 100.0 per turn (the model, which has no holds, 174 -> 46); end drain **11.23 s -> 8.42 s**,
  the poll's turn 13.83 -> 11.55 s (one loaded run each; evidence `b2-ws-he-*-qmoff`). With the observer following
  player 0 the off arm's turn is 13.8 s, not 4675's 28.7 s with the all-seeing observer: the DLL animates a step only
  when a watching human sees it (4678), so most AI steps are teleports there too.

#### The pictures: the right model at the right plot; the formation's facing can differ `[L, 2026-11-17 14:23-15:16 EST]`

DIAGNOSTIC pictures (D112: a test arm, not the shipped system; no popup over the map, D114, checked by eye), taken by
`embarkvis.py --shots 3` after autoplay's hand-back (turn 162): the camera on each unit that embarked or disembarked out
of player 0's sight in the window and came into view later, the same units in both arms of each build; and a fresh load
of the same game as a reference (`refshot.py`: c5h-b's own install, 32-bit DLL in the stock EXE, the off arm's
`AutoSave_Post_0161`, one autoplay turn, the camera on the unit). Comparison figure `F:/c5-64bit-work/4676/ws/compare4-2-3596.png`
(off / build 1 on / build 2 on / fresh load).

- **Unit 2/3596 (Byzantine cataphract)**, embarked at 156 and disembarked at 157 out of sight, in view from 161 at 59,32:
  in all four pictures **the same cataphract model, on land, at 59,32**; the formation's facing differs: the off arm and
  the fresh load face away from the camera, build 1's on arm faces the camera, build 2's on arm faces west.
  *Corrected 15:16: at 14:41 this was read as "another model" (the off arm's riders seen from behind against armoured
  horses seen from the side) and build 1 was marked fail; the four-way comparison shows the same model. Replaced text:
  "the on arm draws armoured horses ... the on arm was wrong ... the unit's art had changed since the load".*
  Build 1 posted nothing when a unit's deferred calls cancelled out (an embark and a disembark both out of sight), so
  the EXE kept the model of the turn-150 load with its facing then [C for the code]; stock posted both calls, each of
  which makes the EXE destroy and build the model (the six records include `UnitDestroyed` and `UnitCreated`), the last
  at the turn-157 disembark. Build 2 (a flush always posts) builds it again at the flush, so the model is new but faces
  the way it faced at that moment [I: which facing the EXE's handler reads was not traced].
- Units 2/2018 (spearman stack) and 63/4106 (barbarian spearman): the same model at the same plot in both arms of build
  2; the barbarians' formation faces slightly further left.
- **Quick Movement off** (build 2, 15:16-15:24, the same three units): off and on equal, facing included
  (`F:/c5-64bit-work/4676/ws/compare-b2-qmoff.png`): the animated move turns the model in both arms.
- **Verdict on the MC's criterion (the right state: the embarked or land model, the position): met** in build 2 (and,
  as corrected, in build 1), with Quick Movement on and off. **Named difference** (D66): with Quick Movement on, a unit
  that comes into view after embark changes out of sight can face another direction than in stock. Making it equal would need the flush to post with the facing of the last
  deferred call (stock's last rebuild), a temporary change of the unit's facing around the call: not done.
- Feed (issue 4676, none undescribed): build 2 on vc-20261117-150909-workstation-6edd, -150917-...-024b (the
  cataphract), -150925-...-be5a (pass); build 2 off vc-20261117-150316-workstation-a9ca, -150323-...-677a,
  -150336-...-fef8 (pass, the references); Quick Movement off on vc-20261117-152335-workstation-4826, -152343-...-2f95,
  -152350-...-6e87 and off -151639-...-5e84, -151647-...-9fc4, -151655-...-9954 (pass); build 1 vc-20261117-142920-workstation-8ec1 (on, info, corrected),
  -142334-...-f6f9 (off) and four more (info); the fresh load vc-20261117-144019-workstation-154e.
- A rule for any later deferral of visual calls: a call that rebuilds a model is not only a state change; deferring it
  changes when (and so with which facing) the model is built.

#### The laptop pairs (D178-sized: 3 runs x 15 turns per arm, Williams order; prepared here, submitted 2026-11-17 as grade B, results below) `[2026-11-17 15:00 EST, --dry-run rc 0 each on build 2's hashes]`

`perf/evidence/0437/4676/laptop_pairs.sh` (output `laptop-dryrun.txt`); grade A by PERFORMANCE.md's treatment
exception (one build, one environment switch differs, the same game). Submit from the epic branch after the merge (the
testbed ships the submitting checkout's HEAD). The common part of each command:
`python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --runs 3 --order williams --turns 15
--quiet --grade A --issue 4676 --keep-going` and then:

| Pair | --arms | --series-id | --expect-min | --timeout |
|---|---|---|---|---|
| Quick Movement on, options 2+3 | `S4e:he_off_skip1_tick0,S4e:he_on_skip1_tick0` | `4676-M-he-qmon-23` | 50 | 7200 |
| Quick Movement off, options 2+3 | `S4e:he_off_qmoff_skip1_tick0,S4e:he_on_qmoff_skip1_tick0` | `4676-M-he-qmoff-23` | 120 | 14400 |
| Quick Movement on, stock pacing | `S4e:he_off_stock,S4e:he_on_stock` | `4676-M-he-qmon-stock` | 60 | 7200 |
| Quick Movement off, stock pacing | `S4e:he_off_qmoff_stock,S4e:he_on_qmoff_stock` | `4676-M-he-qmoff-stock` | 150 | 14400 |
| the check with option 4 (gain about 0) | `S4e:he_off_drain1,S4e:he_on_drain1` | `4676-M-he-drain1` | 45 | 7200 |

each with `--label "4676 hidden embarks (D165 rank 2) off/on, <pair>"`. Every arm has the observer following player 0,
so these arms are not comparable with the study's other series (whose observer sees every plot); compare only inside a
pair. The expected minutes are estimates from the workstation's turn times [I].

#### Results of the laptop pairs (D178) `[L, laptop, submitted by lead-0437 (seventh lead: ab01; eighth lead: bf69, 56d0, 5950) from the epic branch, grade B (D178: n=3 per arm, 3 runs x 15 turns, M 151-165, Williams order, quiet); every arm follows player 0 (observer_follow read back 0 in every read); GPU drivers Intel 27.20.100.9320, NVIDIA 32.0.15.7322 (D175), Windows 10.0.19041.6216]`

Submitted as grade B, not the grade A the commands above declare (D178's development size). Arm S4e (4676's build 2);
the off arm is the stock DLL behaviour, the on arm the prototype (`hidden_embark` on). Analysis copies
`perf/evidence/0437/d178/laptop-<job>-he-*-analysis.txt`; full fetches `F:/c5-64bit-work/0437/4676-M-he-*-<job>`.

| Pair (job) | off s per M turn [95 % CI] | on | difference (off - on) [95 % CI] | % of off | CPU s/turn off -> on | identity |
|---|---|---|---|---|---|---|
| Quick Movement off, options 2+3 (`20261117-153820-ab01`, 16:32-17:19 EST) | 14.705 [14.658, 14.752] | 11.983 [11.889, 12.077] | **2.722 [2.644, 2.801]** | -18.5 % | 28.5 -> 24.2 | IDENTICAL |
| Quick Movement off, stock pacing (`20261117-162147-bf69`, 17:19-18:47 EST) | 38.565 [38.291, 38.838] | 37.508 [37.289, 37.727] | **1.056 [0.826, 1.287]** | -2.7 % | 65.7 -> 65.0 (difference not resolved) | IDENTICAL |
| Quick Movement on, options 2+3 (`20261117-162229-56d0`, 18:47-19:23 EST) | 8.529 [8.403, 8.654] | 5.816 [5.745, 5.887] | **2.713 [2.609, 2.816]** | -31.8 % | 16.4 -> 11.6 | IDENTICAL |
| Quick Movement on, stock pacing (`20261117-162308-5950`, 19:23-20:10 EST) | 15.051 [14.749, 15.353] | 12.761 [12.566, 12.955] | **2.290 [2.042, 2.539]** | -15.2 % | 22.4 -> 18.6 | IDENTICAL |

- QM off with options 2+3: the prototype saves 2.72 s per M turn (-18.5 %), the same game; peak private -3 MB (no cost).
  The off arm (14.71 s) agrees with 32ae's options 2+3 arm (14.75 s, M 151-160, build b2) [L].
- QM off with stock pacing: 1.06 s per M turn (-2.7 %), the same game. Without option 2 the EXE plays each embark
  while the AI plays, spread over the phase, so the prototype saves little; with option 2 the held records drain
  at the phase's end and the prototype removes 2.7 s of that drain [I: from the two pairs].
- QM on with options 2+3: 2.71 s per M turn (-31.8 %), the same game: the same 2.7 s as with QM off, a larger share
  of the shorter turn. Memory growth was higher with the prototype on (arm means 0.31 -> 0.58 MB/turn; the analysis' paired difference 0.40, CI [0.02, 0.78], n = 3;
  not explained, to watch in a longer series). The off arm (8.53 s, following player 0) is shorter than 9d65's
  options 2+3 arm (9.16 s, observer sees every plot, M 151-165): cross-series, an indication only.
- QM on with stock pacing: 2.29 s per M turn (-15.2 %), the same game; memory not resolved either way.
- In all four pairs the prototype keeps the game identical and saves time; the saving is about 2.7 s wherever
  option 2 holds the records to the phase's end, 2.3 s with QM on and stock pacing, and 1.1 s with QM off and
  stock pacing. Why the last is smallest is not measured (QM off animates each visible move, so hidden embarks
  are a smaller share of the frames) [I]. Nothing here is a default-on claim (grade B, n = 3).

#### Not verified (4676)

- Every second above: the model figures (the move rule on the recorded records) and the workstation's one loaded run
  per arm are indications; only the laptop pairs decide. The share is one game (M, turns 151-160) seen from player 0, an
  autoplayed AI: a human's own moves and sight differ (his units do not move in the AI phase).
- Stock pacing on the workstation (the pairs are prepared; no functional run was made with `he_*_stock`), option 4 with
  the prototype (`he_*_drain1`), scenario H, a human game, Quick Combat off (combats hold records; the combat flush is
  untested live: 0 combat flushes in the window), hotseat and multiplayer.
- Which facing the EXE's embark handler gives a rebuilt model (the named difference with Quick Movement on).
- The 32-bit game: the prototype was built and run only as the x64 DLL; the stock-pacing figure uses the 64-bit client's
  frame time.

### Design note: skip or thin the EXE's drawing during the AI phase (D165; lead-0437, 2026-11-18; for the Architect via the MC)

Written by the ninth lead at the MC's request (#5692, 2026-11-18 12:33) from the factor screening above. **Nothing is
built**; no code before the Architect's answer. Marks: `[L]` measured, `[C]` read in our source or the knowledge base,
`[I]` inferred.

**1. The evidence.** In the factor screening (grade B, n 3, laptop, M 151-175, stock pacing, QC and QM on, observer
autoplay; sections "Results of the factor screening, S2i / S4" above), minimising the window after the arm
(`SW_SHOWMINNOACTIVE`, the harness's `minimised` level) took **S2i 15.60 -> 10.55 s (-32.4 %) and S4 15.33 -> 10.03 s
(-34.6 %) per M turn** `[L]`, the game IDENTICAL, with the EXE's CPU about equal (S2i +0.36 s n.s.) or slightly lower
(S4 -1.79 s); peak private +116 / +142 MB. The 60 fps cap changed nothing (S2i -0.06, S4 -0.00 s), and the camera
moved 3.4-5.0 s of EXE CPU but at most 0.3-0.6 s of wall. The threads series (grade A) has the EXE's core thread idle
67 % (S2i) / 93 % (S4) of the turn. Together `[I]`: the AI turn is paced by the **elapsed time per EXE frame**, not by
CPU work: per AI player the period is U + D_B + T_tick (section "What paces an AI turn in the client"), and D_B and
T_tick are counted in frames (queue B is pumped one step per frame without a pending `Update`; one keyed record per
unit per frame, ticket 4672). A frame that does not draw is shorter, so the same number of frames passes sooner.
**Why a minimised frame is shorter is not established**: the CPU barely changes, so the saving is not CPU work that
was skipped `[I]`; candidates are the GPU-bound part of a frame on the laptop's Quadro P3000 (the EXE's command
submission or `Present` waiting on the GPU) or an EXE path that skips its scene pass while the window is iconic or
its `Present` reports `DXGI_STATUS_OCCLUDED` (the stand-in host dropped to 33 fps when minimised:
`../05-graphics-composition/shared-texture-and-compositor.md` "Robustness"). Neither is read in the EXE yet.

**2. What is not yet known, and the first step (no code).** (a) Whether minimising still helps **on top of options
2+3+4** (4.75 s per M turn on the laptop, ticket 4675; D_B is then one frame at the end of the phase, so the
remaining frames are U's and the human turn's): a D178 pair `S4d:drain1` against `S4d:drain1` + `minimised` (the
runner has both; whether the variant composes with that arm is to be checked by `submit.py --dry-run`; 3 x 15 turns, grade B, about 2 h on the laptop) answers it. If the gain there is small, this note's
value is for players who keep stock pacing, and options 2-4 already cover the case. (b) The per-frame split: the
4657 frame trace with and without `minimised` on S4 (mean frame time, frames per AI step) tells GPU wait from skipped
work. (c) A static reading of the EXE's frame function for an iconic/occluded branch (`IsIconic`, `WM_SIZE`
`SIZE_MINIMIZED`, `WM_ACTIVATEAPP`, the `Present` result) names the switch an approved means could set. (a) and (b)
are runner jobs only; (c) is a reading ticket.

**3. Candidate means (D165's approved means only; no EXE code bytes, `.text`/`.rdata` untouched).**

| | Means | What it does during the AI phase | Risk |
|---|---|---|---|
| M1 | A heap or `.data` write of the EXE's own "window inactive / occluded" state, if (c) finds one the frame reads | the EXE takes its own minimised path while the window stays on screen | the same state may stop input or audio handling; set only between the AI phase's start and end, restored before the human turn |
| M2 | The proxy's existing `Present` hook on the swap chain (dxgi; a vtable on a heap object, `../05-graphics-composition/hooks-overlays-and-ceg.md`) returns `DXGI_STATUS_OCCLUDED` without presenting | the EXE sees an occluded window; whether it then skips its scene pass is what (c) must show | if the EXE draws anyway, only the GPU-side present is saved (a share (b) measures) |
| M3 | Thinning: present one EXE frame in N (N 2-8) during the AI phase, the others occluded as in M2 | a moving but slower picture | partial gain; the frame count per AI player is unchanged |
| M4 | `IsIconic` / `GetWindowPlacement` answered "minimised" through the EXE's user32 import slots, if (c) finds the frame asks | as M1 | the slot also answers other callers (the UI's own layout code) |

All of them only during the AI phase (the AI-turn state the proxy already reads for options 2 and 4), off by default as a
`[Performance]` option, with the same D118 fail-loud rule as options 2-4: the original state is restored at the next
`Update` after the phase or on any check failure.

**4. What the player sees.** The picture freezes (M1, M2, M4) or updates at 1/N rate (M3) from the end of the human
turn until the AI phase ends; the EXE's own UI (turn banner, "Processing Turn For ..." text, the end-turn button's
waiting state) freezes with it, so a long freeze can look like a hang. With options 2+4 the AI's visible moves already
play as a burst at the phase's end, so the frozen interval shows nothing the player would otherwise follow, but the
window must stay responsive (messages pumped every frame; the EXE's frame loop keeps running in every candidate). A
small "AI turns in progress" overlay drawn by our compositor over the frozen frame (the composite path can draw after
the EXE's frame, `insertion-point-and-frame-structure.md`) is the obvious answer; a pixel-faithful build (D66) keeps it
off by default. Quick Movement off (ticket 4678): the player watches AI moves, so the option should hold only with QM
on, as option 4 does.

**5. Interaction with the server renderer and D33.** Increment 2 (D33: no model work) keeps units, cities and the UI
in the EXE's frame, and the server composites terrain into it at the insertion point. If the EXE does not present
(M1, M2, M4), the composite has no frame to land in: the last presented frame stays on screen, which is consistent
(terrain and units frozen together). With M3 the composite lands in every Nth frame. The server's own renderer is not
slowed by any of this (it renders on its own thread and never waits for the client:
`shared-texture-and-compositor.md` "Never blocks"). After increment 3's state (ii) (D144), more of the frame is the
server's: a live map could keep moving while only the EXE's UI pass is thinned, which needs the EXE's UI and world
passes to be separable (open; `renderer-in-server-design.md`).

**6. Size of the prize `[I]`.** At stock pacing up to about 5.3 s of a 15.3 s M turn on the laptop (the measured
minimised gain). With options 2+3+4 unknown until step 2(a); the upper bound is the frames that remain in an AI turn
(U's frames and the tick, at most about 4 s of 4.75 s). H (huge, late game) not measured with `minimised`.

**7. Recommendation to the Architect.** Run step 2(a) and 2(b) first (runner jobs, no code, about 3 h of laptop
time); open (c) as a reading ticket only if 2(a) shows a gain on top of options 2+3+4 worth a new option. If it does,
M2 is the first candidate (it reuses the proxy's existing `Present` hook and needs no new EXE address), M1/M4 only if
(c) shows the EXE's minimised path is what saves the time.

**8. The Architect's view and the measurements (2026-11-18).** The Architect's answer is
`research/07-architecture-decisions/architect-2026-11-18-ai-phase-drawing.md` (MC #5707): (1) run (b) under options
2+3+4 as well as at stock pacing, so the remaining 4.75 s per M turn splits into server CPU, the one-frame delay per
Update x 25, and the drain frame plus the turn change; (2) **closing rule for (a): a gain below 0.5 s per M turn (about
10 %) closes this line as "covered by options 2 to 4"**, recorded here as a negative result; an H pair only if M passes;
(3) the minimised arm's +116 / +142 MB peak private needs its own explanation before any option ships; nothing goes to
the owner before (a). Its reading of section 2: an occluded `Present` returns at once (`research/sources/shell-gates/
nofocus-README.md`, a stand-in), so minimised the EXE's loop likely spins faster than it draws `[I]`.

Submitted by lead-0437 (tenth lead) 2026-11-18 12:49 behind screen-quick, on the MC's yes (#5701, #5717: **study builds
on the batch 2 pins** (the RC layout `x64-04b1211b-pfdee656`; S4d = 4675's build b2 in it), compared within each pair
only, no cross-batch reading), from commit 28740eb90 (four new M variants in `perf/turnstudy/runner/scenarios/M.json`:
`drain1_minimised`, `trace_minimised`, `drain1_trace`, `drain1_trace_minimised`; no product code):

    # (a) job 20261118-124910-f4cb, series 0437-M-min-d178
    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4d:drain1,S4d:drain1_minimised --runs 3 --order williams --turns 15 --quiet --expect-min 50 --grade B --series-id 0437-M-min-d178 --label "..." --issue 0437 --keep-going --timeout 7200
    # (b) job 20261118-124951-a4de, series 0437-M-trace-min (an instrument, grade D)
    python -I -X utf8 perf/turnstudy/runner/submit.py run --host laptop --scenario M --arms S4:trace,S4:trace_minimised,S4d:drain1_trace,S4d:drain1_trace_minimised --runs 2 --order williams --turns 10 --quiet --expect-min 30 --grade D --series-id 0437-M-trace-min --label "..." --issue 0437 --keep-going --no-rerun --timeout 7200

The laptop before the submit (read-only, 12:48, screen-quick's game running): free commit 3.45 of 18.20 GB (the
commit limit 1.3 GB lower than the 19.5 GB of 10:31: the page file is system-managed), paged pool 2.70 GB, nonpaged
0.84 GB.

**Result of (a)** `[L, 2026-11-18 15:00-15:51 EST, laptop, job 20261118-124910-f4cb, series 0437-M-min-d178, commit
28740eb90, analysed 15:56 by lead-0437 (tenth lead); grade B (D178: n = 3 per arm, Williams order, no default-on
claim); study builds on the batch 2 pins (S4d = the RC layout with 4675's b2: server A3CB598D, proxy 8B9B05D8);
GPU drivers Quadro P3000 32.0.15.7322 / Intel HD 630 27.20.100.9320 (D175; read on the laptop 2026-11-18 16:00,
read-only)]`. Fetched to `F:/c5-64bit-work/0437/0437-M-min-d178-f4cb` (analysis +
`.txt`, copy `perf/evidence/0437/v3/min-a-f4cb-analysis.txt`). 6 of 6 runs exit 0, none refused, per-run foreign
cores 0.034-0.063; every run IDENTICAL within its arm and across the arms (stats.db, turns 150-167).

| Arm (options 2+3+4 on) | s per M turn (n 3), 95 % CI | EXE CPU s/turn | server CPU s/turn | peak private (+ server) | growth MB/turn | clock |
|---|---|---|---|---|---|---|
| S4d:drain1 (on screen) | 4.770 [4.683, 4.857] | 9.52 | 3.02 | 1702 MB (1910) | 3.29 | 118.5 % |
| S4d:drain1_minimised | 4.306 [4.284, 4.328] | 11.02 | 2.74 | 1817 MB (2025) | 8.26 | 120.1 % |
| minimised - on screen | **-0.464 s (-9.7 %) [-0.544, -0.383]**, detectable 0.10 | **+1.50 [+1.27, +1.74]** | | **+116 MB [+103, +128]** | **+5.0 [+3.9, +6.1]** | |

Reading `[L numbers, I cause]`: on top of options 2+3+4 a minimised window saves **0.46 s of a 4.77 s M turn**
(-9.7 %); the on-screen arm repeats 4675's 4.75 s (0901) within 0.02 s. Against the Architect's closing rule (0.5 s,
about 10 %): the point estimate is below it and the interval [0.38, 0.54] straddles it. It costs more CPU (+1.5 s
of EXE CPU per turn: the minimised loop spins rather than waits, as section 8's reading expects), **+116 MB of peak
private and more than twice the memory growth per turn** (3.3 -> 8.3 MB). So almost all of what a minimised window
saved at stock pacing (5.3 s) is already taken by options 2+3+4; what is left is about the size of the Architect's
estimated frame-bound remainder (0.4-0.5 s for one frame of delay per Update x 25) and comes with a memory cost that
is not explained. **Recommendation (lead-0437): close the line as "covered by options 2 to 4" (a negative result);
no reading ticket, no M2 experiment, no H pair.** The memory growth with a minimised window (here and in both
screenings) stays an open question for the report, not for an option. **Closed by the MC 2026-11-18 15:56 (#5915), by
the Architect's own rule: "covered by options 2 to 4", a negative result; no reading ticket, no M2, no H pair.**

**Result of (b), the frame trace minimised against not** `[L, 2026-11-18 15:51-16:35 EST, laptop, job
20261118-124951-a4de, series 0437-M-trace-min, commit 28740eb90, M 151-160, 2 Williams rounds, n = 2 per arm, an
instrument (grade D: indications, no claim); same builds and GPU drivers as (a); analysed 16:40 by lead-0437]`.
8 of 8 runs exit 0, none refused; every run IDENTICAL within and across the four arms. `trace.py` per run
(`--expect-steps 25`: 25 steps in every turn), outputs and the series analysis in `perf/evidence/0437/v3/trace-b/`.
The trace arms repeat the timed results: stock pacing 14.33 -> 9.42 s minimised (-34 %), options 2+3+4 4.63 -> 4.22 s
(-0.41 s). Per M turn (mean of the two runs):

| Arm | s/turn | inside the 25 Updates | of which the turn-change Update | waits between Updates | frames per wait (median) | frame time mean | EXE CPU s/turn | peak private (+ server) | growth MB/turn |
|---|---|---|---|---|---|---|---|---|---|
| S4:trace (stock pacing, on screen) | 14.33 | 3.56 s | 1.27 s | 10.79 s | 9 | 28.6 ms | 23.7 | 1657 MB (1862) | 2.7 |
| S4:trace_minimised | 9.42 | 3.16 s | 1.15 s | 6.29 s | 9.5 | 13.0 ms | 21.3 | 1789 MB (1995) | 7.7 |
| S4d:drain1_trace (options 2+3+4, on screen) | 4.63 | 3.42 s | 1.13 s | 1.23 s | 1 | 35.5 ms | 9.3 | 1691 MB (1896) | 6.3 |
| S4d:drain1_trace_minimised | 4.22 | 3.09 s | 1.10 s | 1.15 s | 1 | 17.9 ms | 10.8 | 1799 MB (2005) | 10.7 |

Reading `[L numbers, I cause]`:
- **At stock pacing the wait is a frame count, not a time**: about 9 frames between two Updates in both window states
  (the tick's 0.1 s and the queue-B drain, D_B, counted in frames); a minimised frame takes 13 ms against 28.6 ms on
  screen, so the same frames pass in 6.3 s instead of 10.8 s. The minimised EXE does not spin at thousands of frames a
  second as the 2026-11-09 stand-in did (section 8's reading): it still spends about 13 ms per frame (77 fps).
- **The Architect's split of the remaining 4.6 s with options 2+3+4** (on screen): **3.42 s inside the Updates**
  (74 %; the server's CPU is 2.9-3.0 s a turn, (a)'s table, and the turn-change Update alone 1.13 s) and **1.23 s of
  waiting**: one frame between two Updates (median 1, mean 5 frames per step with the turn change's drain), 24 x
  about 50 ms. The drain frame does not show as a separate wait: with option 4 its single long frame (up to 0.8 s by
  the clock) falls inside a step. The Architect estimated 0.4-0.5 s for one frame of delay x 25; at the laptop's
  35 ms frames it is about 1.2 s.
- **With options 2+3+4 the minimised gain is mostly inside the Updates** (3.42 -> 3.09 s, -0.33 s), hardly in the
  waits (1.23 -> 1.15 s, -0.08 s), although the minimised frame is half as long. So a shorter frame no longer buys
  much; what a minimised window saves there is time inside the server's Updates, likely less contention with the
  EXE's drawing for the CPU (clock 120 % against 118 %) or for the GameEvents crossings the main thread serves `[I]`.
  This agrees with (a)'s closure.
- **Memory**: the minimised arms grow faster again (+5.0 and +4.4 MB a turn, not resolved at n = 2) and peak +110 to
  +133 MB. Frames per turn rise with minimising (stock pacing about 500 -> 730, options 2+3+4 about 130 -> 240), so
  more frames per turn may carry the growth, but the ratios do not match (growth x1.7-2.8 against frames x1.5-1.8) and
  nothing was read per frame `[I]`; open (the report's not-verified list).
- The next lever, if any, is the 1.2 s of one-frame waits (the EXE's frame between an Update's end and the next
  request) and the turn-change Update's 1.1 s; neither is opened here (the line is closed, #5915).

## What the server asks again: the counting runs (W6, ticket 4669; 2026-11-17)

Written by impl-4669, 2026-11-17 09:27-. Q3 of the proposal (section 4.5), the step after 4668's profile: the three
counting designs of `turn-time-study-analysis.md` 9.8, built and run. Evidence only (the proposal's hard rule): the
counters live on a fork branch, nothing merges into the product DLL, nothing goes upstream (D95). Every number from a
run is an indication from the shared workstation (D106); the identity of each run's game with the arm's is the pass
condition. Evidence `perf/evidence/0437/4669/`; run folders with the CSV files: `F:/c5-64bit-work/4669/runs/`.

### The counting build `[C, 2026-11-17]`

- **Branch** `x64-count-0437` of the DLL fork, from the pin `04b1211b8`, in a clone of the local fork clone
  (`C:/c5-64bit-work/4669/fork`; pushed to the project's fork on GitHub for safety, never upstream). Commits:
  `f481a4c76` (the shared code `CvGameCoreDLL_Expansion2/VP64Count.h` and `VP64Count.inl`, the input-event hooks, the
  trade-path counter), `add4d257e` (the reachable-set counter), `c32469ccb` (the parallel-premise counter). The shared
  code holds all three counters' parts, each behind its own define; the commits add the call sites.
- **Defines**: `VP64_COUNT_TRADEPATH`, `VP64_COUNT_REACH`, `VP64_COUNT_PARALLEL`; the shared part (events, turn rows,
  call stacks, CSV files) compiles when any of the three is set (`VP64_COUNT_ANY`, made by the header). None is set by
  any product build: they are absent from `tools/x64-dll/build.py` (`PREDEFS`), `server/`, `proxy/` and from every
  property sheet, project and build script of the fork. **Compiled out**: `guardcheck.py` checks the diff
  04b1211b8..x64-count-0437: no line removed or changed, every added line inside a block that opens with
  `#if defined(VP64_COUNT_...)`, the two new files included only under such a block (`guardcheck.txt`: OK; the same check
  against 76992d235, a diff with real changes, fails with 350 problems: the negative test). Without the defines the
  preprocessor removes every change; what remains is the shift of later line numbers (`__LINE__` in assert texts). Not
  shown by a build of the product configuration from the branch (not made: the defines' absence and the guard check are
  the acceptance's second option).
- **One counting build with all three defines**, not one per define: the counters only read and hash; each counter
  measures its own time (QPC around its own code, `counter_ms` per turn) and every timed window (a trade-path rebuild, a
  danger update) subtracts the counters' time spent inside it, so one counter's cost does not enter another's numbers;
  and the identity check decides whether the build plays the arm's game. Each define alone and none were compiled
  (`-fsyntax-only` of the 9 changed translation units with the release flags): rc 0, no `-Wshorten-64-to-32` warning in
  the counter code.
- **Build** `python -I tools/x64-dll/build_all.py --src C:/c5-64bit-work/4669/fork --out C:/c5-64bit-work/4669/build-count
  --config release --offline --unlocked` with `CL=-DVP64_COUNT_TRADEPATH -DVP64_COUNT_REACH -DVP64_COUNT_PARALLEL` (clang-cl
  reads the `CL` environment variable; the `-D` form because MSYS rewrites `/D`), at this repository's
  `task-4669-w6-counting` (epic 0437 head 179ded3b3 merged), 2026-11-17 09:58:31-10:10:03 EST: every step rc 0 (stubs,
  cvgamedatabase, cvlocalization, dll 539 s, host, smoke), narrowing gate 794 against 798 (0 new), version string
  "Release-5.4.8-54-gc32469ccbe Clean". **Counting DLL SHA-256
  `ED0BB1A72C343CE12E3B7C468615DFE7A69674FA9EC1CE75DABCD33ADBE18144`** (23,714,304 B; the pin's is 23,655,424 B).
- **What is counted** (the code is the reference; the CSV columns are in `VP64Count.inl`):
  - input events per turn (`VP64CountTurns.csv`): plot owner, route (and route pillage), improvement (and pillage),
    feature, `setRevealed`, `setRevealedImprovementType`, `setRevealedRouteType` (per team), every visibility-count
    change (`changeVisibilityCount`, `flipVisibility`; per team), war and peace (`CvTeam::setAtWar` on a change), city
    founded, acquired, killed, unit moved (`setXY`), created, killed, a danger map finished. Every event advances the
    reach counter's world epoch; a unit event is also counted for each team that sees its old or new plot.
  - trade paths (`TradePathCount.csv`, one row per rebuild): in `CvGameTrade::UpdateTradePathCache`, per origin city the
    time of its two `GetMultiplePaths` searches and cache adds, a hash of the paths (destination, cost, normalised
    distance, turns and every node) and of the cached `STradePathInfo` (all seven fields, so the danger and yield scores
    too), compared with the same city's hash at its previous rebuild; the rebuild's reason (`turn`: the counter was an
    earlier turn; `invalidated`: reset by an invalidation; `inv_same_turn`: reset after a rebuild in the same turn); the
    call stacks of `InvalidateTradePathCache(PlayerTypes)` for majors (`TradePathInvalidations.csv`).
  - reachable sets (`ReachCount.csv` per turn and call stack): every `CvPathFinder::GetPlotsInReach(x, y, data)` (both
    overloads and `GetAllPlotsInReachThisTurn` end there), keyed by the finder object, the start plot, every field of
    `SPathFinderUserData` and the ZOC list; **repeat A** = same key and same epoch as an earlier call of the turn (nothing
    the counter knows of changed); **equal B** = same key, any epoch, result equal to the key's previous result (the most
    any exact invalidation could reuse); the result hash covers every plot's index, turns, moves left and distance. An
    unequal repeat A means the epoch misses an input (`ReachUnequal.csv` keeps samples).
  - parallel premise (`ParallelPremise.csv`): every trade-path rebuild and every `CvDangerPlots::UpdateDanger`, with
    whether it is the player's first of the game turn and the events since the game turn started (the mark is right
    after `incrementGameTurn` in `CvGame::doTurn`): plot events (any player), the player's team's view events, unit
    events seen by the team.
  - call stacks: 10 return addresses (`RtlCaptureStackBackTrace`) as offsets in the DLL (`VP64CountStacks.csv`),
    symbolised afterwards with LLVM 19.1.7's `llvm-symbolizer --inlines` against the build's PDB (`reduce_count.py`).
- **Runs**: `run_count.sh M|H NAME` (registered with `quietlock.py run`, owner 0437; load before and after in
  `load-*.txt`) -> `run_count.py`: the arm S3's set-up of 4668 (`profile_s3.py`'s server.cfg keys: 0438's statistics-on
  database, `dll_log_flags 0`, the toolkit game folder, Windows paths only) with a copy of the layout's bin folder whose
  DLL and PDB are the counting build's (M: the pin layout `x64-04b1211b-pfdee656`, server `5B254430...`; H: 0452's
  `layout-new`, server `FC68B75B...`), under `tools/harness/jobcap.py` (16 GB), `VP64_COUNT_DIR` = the run's `count/`;
  plays to the window's last turn + 2. Windows as 4668's: M 151-160, H 301-305. Identity: `perf/evidence/0438/idtrace.py`
  over the window against the uncounted arm's games (M: the M series' `r01-p4-S3` and 4668's `m1`; H: 0452's `s3new-H10`
  and 4668's `h1`).

### Results `[L, 2026-11-17 10:10-10:15 EST, workstation, shared: 6-8 other owners' heavy jobs at each start (load.txt)]`

Both runs **play the arm's game** (`identity.txt`): c-m1 = the M series' `r01-p4-S3` = 4668's `m1` over turns 150-160
(32,631 rows, IDENTICAL); c-h1 = 0452's `s3new-H10` = 4668's `h1` over 300-305 (125,210 rows, IDENTICAL). So the
counters change nothing the statistics log sees, and neither run is void. The counters' own time: 1.1 % of the M turn,
0.9 % of the H turn (subtracted from every share below). Counting-run turns (net of the counters): M 1.41 s, H 26.5 s
(the series' S3: M 1.424 s on the laptop; H about 21 s headless on the workstation, 0438).
Reductions `python -I -X utf8 perf/evidence/0437/4669/reduce_count.py --dll
F:/c5-64bit-work/4669/build/CvGameCore_Expansion2.dll --out perf/evidence/0437/4669/reduced --run
M=F:/c5-64bit-work/4669/runs/c-m1:151:160:1.424 --run H=F:/c5-64bit-work/4669/runs/c-h1:301:305:20.9` (the PDB beside the
DLL symbolises the stacks) -> `reduced/reduce-M.txt`, `reduced/reduce-H.txt`, `reduced/reduce.json`.

**Input events per turn** (M / H): unit moves 672 / 3,049; units created 30 / 59, killed 45 / 86; visibility-count
changes 29,293 / 163,855; revealed 261 / 15 plots, revealed improvements 63 / 124, revealed routes 34 / 91; improvements
7 / 21 (pillaged 2 / 5); routes 3 / 14; owners 8 / 33; war 4 / 17 and peace 1 / 17 `setAtWar` changes; danger maps
finished 31 / 103.

**1. Trade paths (`VP64_COUNT_TRADEPATH`).**

| | M (151-160) | H (301-305) |
|---|---|---|
| rebuilds (all players' `UpdateTradePathCache` that did work) | 420 (42 per turn) | 439 (88 per turn) |
| of them after an invalidation in the same turn (`inv_same_turn`) | 340 (529 ms of 650) | **384 (29.9 s of 33.7 s)** |
| origin-city rebuilds | 2,588 | 7,118 |
| with paths equal to the city's previous rebuild | **2,299 (88.9 %)** | **6,101 (85.7 %)** |
| with the cached info (incl. danger and yield scores) equal | 1,708 (66.1 %) | 3,509 (49.3 %) |
| rebuild share of the counting turn (4668's profile: M 4.2-5.0 %, H 27.2 %) | 4.6 % | 25.0 % |
| share in cities with equal paths -> bound per S3 turn | 3.7 % -> **0.052 s** | 20.8 % -> **4.34 s** |
| the same scaled by 4668's share (equal fraction of the rebuild time x 4668's share x S3 turn) | 0.048-0.057 s | 4.73 s |
| share in cities with equal info -> bound | 2.6 % -> 0.037 s | 10.2 % -> 2.13 s |
| cache at the window's end: destinations (x 28 B) / path nodes (x 10 B) | 1,023 / 15,877 | 18,072 (0.5 MB) / 830,760 (8.3 MB) |

Who invalidates (H window, calls of `InvalidateTradePathCache(PlayerTypes)` for living majors, by caller):
`CvPlayerManager::Refresh` 2,717 (every `setAtWar` change anywhere calls `Refresh(true)`, which invalidates **every**
player: `CvPlayerManager.cpp:40`, `CvTeam.cpp:1196, 2098`), `CvPlot::changeVisibilityCount` 1,985 (a city's plot comes
into view: both teams, `CvPlot.cpp` near the meeting code), `CvUnit::build` 1,925 (every finished worker build anywhere
invalidates every player, `CvUnit.cpp:13624`), `CvCity::init` 22, `CvBarbarians::DoCamps` 22 [C for the call sites at
04b1211b8; L for the counts]. M has the same three at the top (984 visibility, 848 build, 424 refresh). So the H cache is
rebuilt 5-8 times per major per turn, and 86 % of the cities find the same paths again.

**2. Reachable sets (`VP64_COUNT_REACH`).**

| | M | H |
|---|---|---|
| `GetPlotsInReach` calls | 26,724 (2,672 per turn) | 105,575 (21,115 per turn) |
| share of the counting turn (4668: M 14.5-17.0 %, H 24.9 %) | 15.2 % | 25.8 % |
| repeats with nothing changed (same key and epoch) | 1,218 (4.6 %), equal 1,215, **unequal 3** | 1,965 (1.9 %), all equal |
| -> bound (a cache with a global epoch) | 0.39 % -> **0.006 s** | 0.21 % -> **0.043 s** |
| same key, any epoch, result equal to the key's previous one | 10,423 (39.0 %) | **73,343 (69.5 %)** |
| -> bound (the most an exact invalidation could reuse) | 2.2 % -> 0.032 s | **9.3 % -> 1.94 s** |
| of it the danger maps' enemy reach (`UpdateDangerSingleUnit`): calls, equal B, bound | 9,996, 8,394, 0.022 s | **72,692, 68,409 (94 %), 1.72 s** |
| tactical (`CvTacticalAI`, `TacticalAIHelpers`, `CvTacticalPosition`) | 7,233, 1,373, 0.007 s | 15,193, 3,441, 0.21 s |
| cache size per turn: keys A / plots; keys B / plots (x 16 B) | 2,291 / 0.50 M (7.9 MB); 1,618 / 0.32 M (5.2 MB) | 28,079 / 6.0 M (96 MB); 6,315 / 4.4 M (70 MB) |

The other large callers never repeat a key in a turn: worker moves (`CvHomelandAI::ExecuteWorkerMoves`, H 5.1 s of calls
in 5 turns), explorers (`GetBestExploreTarget`), city connections (`CvCityConnections::UpdateRouteInfo`, 3 % repeats),
`TacticalAIHelpers::UpdatePlotDistanceToTarget` (5.6 s, 13 % equal B). The 3 unequal repeats on M are all in
`CvTacticalAI::FindUnitsWithinStrikingDistance`: something that call's reach depends on is not among the counted events
(not identified) [L]. Reading `[I]`: the danger maps recompute each visible enemy unit's reach for every observing
player at every rebuild; between two of them some unit somewhere has moved, so a global epoch never matches, but the
enemy unit and its surroundings are mostly unchanged.

**3. Parallel premise (`VP64_COUNT_PARALLEL`).**

| | M | H |
|---|---|---|
| every trade-path rebuild: count, share of turn | 420, 4.6 % | 439, 25.0 % |
| every danger map: count, share | 306, 2.7 % | 517, 11.0 % |
| first-of-turn trade rebuilds (one per major): share; with no event since the turn start (plots, own view, units seen) | 80, 0.9 %; **0** | 55, 2.8 %; **10** |
| first-of-turn danger maps: share; with no event | 250, 2.1 %; 6 | 160, 3.2 %; 11 |
| bound on 4 cores, (share of no-event first builds) x 3/4 x S3 turn: trade + danger | 0.000 + 0.001 s | **0.075 + 0.046 s** |
| the same with only the map inputs (plots, view; units ignored) | 0.000 + 0.001 s | 0.177 + 0.060 s |
| if every first build could move to the turn start | 0.009 + 0.022 s | 0.44 + 0.51 s |
| first trade rebuilds' cities with paths equal to their previous rebuild (x 3/4) [I] | 0.008 s | 0.37 s |

What it says: only a player's **first** build of a turn could move to the turn's start, and those are 2.8 % (trade) and
3.2 % (danger) of the H turn; the rest of the 25.0 % and 11.0 % are rebuilds after invalidations and dirty flags later in
the turn, which depend on what earlier players did. By the time a player's first build runs, some input event has
happened for nearly all of them (45 of 55 trade, 149 of 160 danger on H). The premise of 9.8's rank 1 fails for all but
a sliver.

### Not verified (4669)

- One run per scenario, a shared workstation: the ms shares are indications (the counters' bounds use shares of the
  counting run's own turn, so the machine's load moves numerator and denominator together, but not equally for memory-
  bound code).
- The event list's completeness: 3 unequal repeats on M show the reach epoch misses an input of
  `FindUnitsWithinStrikingDistance`; the trade-path "equal paths" counts results, not inputs, so it is an upper bound of
  what an exact invalidation could keep.
- A cache's own cost (lookup, invalidation bookkeeping) is not in any bound; nor is a parallel build's cost (a step finder
  per thread, synchronisation).
- The memory figures are the counted key and value sizes, without container overhead.
- A product-configuration build of the branch without the defines (the acceptance's first option); the guard check and
  the defines' absence from every build stand for it.

## W6 phase 2: prototype (a), the trade-path searches reused while their inputs hold (ticket 4673; 2026-11-17)

Written by impl-4673, 2026-11-17 10:42-. Prototype (a) of 9.8 (the trade-path invalidation made exact), on the fork
branch `x64-proto-0437` from the pin `04b1211b8` (MC grant: queue thread 0437 entry 4153): behind its own define
`VP64_PROTO_TRADEPATH`, off in every product build, evidence only, never in master, i0-ship, x64 or an upstream-tracking
branch, nothing upstream (D95). Every time below is an indication from the shared workstation (D106); the identity of
each run's game with the arm's is the pass condition. Evidence `perf/evidence/0437/4673/` (per run under `runs/`:
run.json with the binaries' hashes, turn times, server.cfg, the prototype's `ProtoTradePath.csv`, the load before and
after); run folders with the `stats.db` files and profiles `C:/c5-64bit-work/4673/runs/` (8 GB, kept until the lead
confirms the merge, D111); the testbed layouts `F:/c5-64bit-work/4673/layout-b{2,3}-{M,H}/`.

### Why the invalidations cannot simply be narrowed `[C, source 04b1211b8]`

The ticket's first idea was to keep the three callers' invalidations (`CvPlayerManager::Refresh` from every `setAtWar`
change, the global `InvalidateTradePathCache()` in `CvUnit::build` at `CvUnit.cpp:13618`, `InvalidateTradePathTeamCache`
of both teams when a city plot comes into view, `CvPlot.cpp:11637`) but invalidate fewer players. That changes the game,
by construction of the cache:

- An invalidation only resets the player's turn stamp (`CvGameTrade::InvalidateTradePathCache(PlayerTypes)`,
  `CvTradeClasses.cpp:357`); the rebuild happens at the next query and **reads the state of that moment**, not of the
  invalidation.
- A rebuild has two parts: per origin city two `GetMultiplePaths` searches (water, land), and the scoring
  `AddTradePathToCache` of every path found: `aiTeamDanger` (war, every team's visible approach towards the player,
  wars declared, plotting, denouncing, city-state allies, vassals), each path plot's visibility, visible enemy defenders,
  enemy-dominated zones of the tactical analysis map, passing-route yields and terrain yields.
- The scoring's inputs change **without any invalidation** (approaches move in every player's diplomacy turn, units
  move, the dominance zones are rebuilt). So a player whose invalidation is skipped keeps scores of an older moment, and
  4669 counted that only 49.3 % (H) of the origin-city rebuilds have equal scored info, against 85.7 % with equal paths.
  Skipping a whole player's rebuild would therefore change AI decisions in about half of the cases where the paths are
  the same.

### The narrowing that keeps the game: the searches, not the rebuilds `[C, the prototype's code]`

So the prototype keeps every rebuild the stock code does, at the same moment, with the scoring unchanged, and narrows
the **searches** inside it: a city's search runs again only if something it read has changed. Per (player, origin city
plot, trade path type) it keeps the last `GetMultiplePaths` result with:

- a fingerprint of the search-level inputs: every `SPathFinderUserData` field (incl. the trade range
  `iMaxNormalizedDistance`, which depends on the city, techs, policies and buildings), the origin plot, the player's team,
  `CanCrossOcean`, `CanEmbark`, `CanCrossMountain`, `CanCrossIce`, the two traits `TradePathInitialize` reads
  (`IsRiverTradeRoad`, `IsWoodlandMovementBonus`), the player's era (`TradePathLandCost` switches the improvement route
  yield at era 4), and the destination list (every city plot, in the order built);
- the list of every plot the four callbacks (`TradePathLandCost`, `TradePathLandValid`, `TradePathWaterCost`,
  `TradePathWaterValid`) read as a "to" or a "from" plot during that search, each with a 64-bit hash of every plot
  property those callbacks and the predicates they call read for this player: revealed (team), revealed improvement
  (team; the barbarian camp test), owner, team, the player's team at war with it, route type, route pillaged, feature,
  terrain, improvement type, its passability and pillage, water, deep and shallow water, ice, flatland, river, city,
  `isValidMovePlot(player, false)`, `isCoastalCityOrPassableImprovement(player, false, false)`, the resource visible to
  the team (the canal shortcut), and for a city plot its `GetYieldFromPassingTR` for every yield.

At the next rebuild, `GetMultiplePaths` returns the stored result when the search fingerprint and every touched plot's
hash are equal (each plot's hash computed once per rebuild); otherwise it searches, records what it touches, and stores.
Why this is exact `[I, by the code; the identity runs and the verify mode decide]`: the A* search is deterministic and
reads the map only through these callbacks (the heuristics read coordinates only; the multi-destination search runs with
no destination and filters the closed nodes by the destination list afterwards; the step finder's `CanEndTurnAtNode`
returns true); a plot the search never touched was never adjacent to an expanded node, so a change there cannot alter
the expansion. The stored `SPath` carries the turn slice of its first search (`iTurnSliceGenerated`), which
`STradePathInfo` does not copy, so the cache contents are equal. Rivers are fixed map data (hashed anyway).

What this does to each of 4669's three callers (the rebuild still runs; which **searches** run again):

| Caller (H window: invalidations of majors) | What changes for a player's searches | Searches that run again |
|---|---|---|
| `setAtWar` -> `CvPlayerManager::Refresh(true)` -> every player (2,717) | only the war bit of plots owned by the two teams, and only for the players of those two teams (`isAtWar(myTeam, plotTeam)` in both Valid callbacks); `isValidMovePlot` is called with `bCheckTerritory = false` | the two teams' cities whose searches touched the other team's territory; every other player reuses all its searches (and still rescores with the new `aiTeamDanger`) |
| a finished worker build, `CvUnit::build` -> every player (1,925) | route, improvement, feature, passability and pillage of one plot (and what follows: `isValidMovePlot` with a route) | the cities of any player whose search touched that plot |
| a city plot comes into view, `changeVisibilityCount` -> both teams (1,985) | the revealed state and revealed improvement of the plots the team now sees | the two teams' cities whose searches touched a plot whose revealed state changed (the meeting itself changes no plot) |
| a city founded, taken or razed (`CvCity::init` and others: global) | the destination list and the city plot | every search (the destination list is in the fingerprint; conservative) |

### Runtime modes and counters `[C]`

`VP64_PROTO_TRADEPATH_MODE`: `0` the stock path (the define compiled in, nothing done: the define's own cost and a
same-binary control), `1` reuse (default), `2` verify (decide as in 1 but always search and keep the fresh result, so
the game is the stock one; count the reuses whose stored result differs from the fresh one: a missing input, and the
recomputes whose result equals the stored one: lost reuse). With `VP64_PROTO_DIR` set the DLL writes
`ProtoTradePath.csv`, one row per rebuild: searches, reused, recomputed, no entry yet, verify counts, plots checked,
validation, search and store time, the store's entries and bytes (keys, the touched lists and the stored paths).

### The prototype build `[C, 2026-11-17]`

- **Branch** `x64-proto-0437` of the DLL fork, from the pin `04b1211b8`, in a clone of the local fork clone
  (`C:/c5-64bit-work/4673/fork`; pushed to the project's fork on GitHub for safety, never upstream). One commit
  `9cc06b16f`: the new file `CvGameCoreDLL_Expansion2/VP64ProtoTradePath.inl` (included at the end of `CvAStar.cpp`),
  the touch calls in the four trade-path callbacks, the lookup at the start and the store before the final return of
  `CvPathFinder::GetMultiplePaths` (`CvAStar.cpp`), and `VP64ProtoTradeBegin`/`End` around the origin-city loop of
  `CvGameTrade::UpdateTradePathCache` (`CvTradeClasses.cpp`).
- **Compiled out**: `perf/evidence/0437/4673/guardcheck.py` (4669's check with the define prefix and the allowed new file
  as arguments) over 04b1211b8..x64-proto-0437: no line removed or changed, every added line inside an
  `#if defined(VP64_PROTO_...)` block, the new file included only under one (`guardcheck.txt`: OK, rc 0); the negative
  test against `x64-count-0437` (other prefix, other new files) fails with rc 1 and 96 lines outside a block. The define
  is in no product build file.
- **Build** `python -I tools/x64-dll/build_all.py --src C:/c5-64bit-work/4673/fork --out C:/c5-64bit-work/4673/build-proto
  --config release --offline --unlocked` with `CL=-DVP64_PROTO_TRADEPATH` (`C:/c5-64bit-work/4673/scripts/build_proto.sh`),
  at this repository's `task-4673-proto-tradepath` (epic head 1fb561bcb), 2026-11-17 11:01-11:10 EST: every step rc 0
  (stubs, cvgamedatabase, cvlocalization, dll 541 s of which the whole-program link 435 s, host, smoke), narrowing gate
  794 against 798 (0 new). **Prototype DLL SHA-256 `663AFFC8A20FA329B2B46B4B45D50F3627A4DC76A397C74077E865DF5D88E8A4`**
  (23,682,048 B; the pin's `29F58E17...` is 23,655,424 B).
- **Runs**: `perf/evidence/0437/4673/run_proto.sh M|H NAME proto|pin MODE` -> `run_proto.py` (4669's `run_count.py`
  set-up: the arm S3's server.cfg keys, a copy of the layout's bin folder with the chosen DLL and PDB; server.exe under
  `jobcap.popen`, 16 GB cap, its peak process commit recorded; optional sampler of 4668 with `--sample-from`), M with
  the pin layout `x64-04b1211b-pfdee656` (server `5B254430...`), H with 0452's `layout-new` (server `FC68B75B...`).
  Identity: `identity.sh` (`perf/evidence/0438/idtrace.py` over M 150-160 and H 300-305 against the M series'
  `r01-p4-S3` and 4668's `m1`, 0452's `s3new-H10` and 4668's `h1`, the references 4669 used).

### Identity and the verify mode, build b1 `[L, 2026-11-17 11:10-11:19 EST, workstation, shared: 7-8 other owners' heavy jobs at each start (load/)]`

All four runs of the first build (b1, `663AFFC8...`, the "wide" fingerprint) **play the arm's game**
(`identity.txt`): M 150-160, 32,631 rows, and H 300-305, 125,210 rows, IDENTICAL to both references, in the reuse mode
(`i-m-reuse`, `i-h-reuse`) and in the verify mode (`i-m-verify`, `i-h-verify`). The verify mode found **no reuse whose
stored result differs from a fresh search** (M 3,427 of 3,427, H 7,620 of 7,620 equal): the fingerprint misses no input
the searches of these windows depended on.

| Window (summed over the window's rebuilds) | M 151-160 | H 301-305 |
|---|---|---|
| rebuilds (`UpdateTradePathCache` that did work) | 419 | 436 |
| `GetMultiplePaths` searches asked | 5,168 | 14,158 |
| reused (reuse mode) | 3,427 (66.3 %) | 7,620 (53.8 %) |
| recomputed, of them equal to the stored result (verify mode: reuse lost to a too-wide fingerprint) | 1,741, 1,440 equal (83 %) | 6,538, 5,448 equal (83 %) |
| time in the searches, all run (verify) / reuse | 0.465 s / 0.241 s | 21.47 s / 15.38 s |
| validation / store (reuse mode) | 0.049 s / 0.027 s | 0.65 s / 1.37 s |
| store at the window's end (entries; keys, touched lists with hashes, stored paths) | 104; 0.79 MB | 312; 29.0 MB |
| peak process commit (jobcap) reuse / verify | 261 / 261 MB | 407 / 407 MB |

Reading `[L for the counts; I for the reading]`: b1 reuses about half the searches on H and saves about 6.1 s of search
over the 5 turns, against 2.0 s of validation and storing: about 0.8 s per H turn net, before any timing comparison.
83 % of the searches it ran again found the same paths, so the fingerprint is wider than the searches need: a second
build (b2) narrows it to what the callbacks can tell apart, with the verify mode classifying every miss.

### Timing indications, build b1 `[L, 2026-11-17 11:18-11:49 EST, workstation, shared: 6-8 other owners' heavy jobs at every start (runs/*/load.txt): indications only, no timing claim (D106)]`

**These arms should not have run** (correction 2026-11-17 12:10): COMMON 23 lets an arm that compares time on the
workstation run only while `quietlock.py check` lists no other heavy job, and lead-0437 said so at 11:15 (queue thread
0437); the worker read it after the series. From 12:04 `run_proto.sh` refuses such arms on a busy machine (widened at
12:57 after b3's series named its arms differently and one more pair started: `v-h-1-pin`, `v-h-2-proto`, not used).
The numbers below are kept as recorded; they support no conclusion.

`series_time.sh`: H and M each in the order pin, b1, b1, pin, pin, b1 (the pin's DLL `29F58E17...` against b1 in the
reuse mode, same server and set-up), then one sampled run of each on H (4668's sampler, 10 ms). Every b1 run's game is
IDENTICAL to both references (`identity.txt`). Seconds per turn over the window (the server's own turn lines); from
`t-m-4-pin` on, `run_proto.py` also reads the process's CPU time and cycle count at each turn line:

| Run order | H 301-305, s/turn | M 151-160, s/turn |
|---|---|---|
| 1 pin | 22.02 | 1.480 |
| 2 b1 | 24.12 | 1.725 |
| 3 b1 | 33.31 | 1.520 |
| 4 pin | 30.90 | 1.521 (CPU 1.478) |
| 5 pin | 23.18 | 1.419 (CPU 1.383) |
| 6 b1 | 27.25 | 1.521 (CPU 1.459) |
| mean pin / b1 | 25.37 / 28.23 | 1.473 / 1.589 |
| sampled pair (H) pin / b1 | 34.38 (CPU 30.31) / 27.08 (CPU 24.00) | - |

**No conclusion on the turn time can be drawn from these** `[L]`: the same build varied by 9 s per H turn between runs
(22.0 to 30.9 for the pin), several times any possible saving (4669's bound: 4.34 s); in the three adjacent pairs b1
was the slower one, in the sampled pair the faster one. The machine's load moved the wall time and, less, the CPU time.
The real numbers need the testbed pair (below).

What the runs do show, from the prototype's own counters `[L]`: the window's 14,158 H searches cost 21.5 s with every
search run (verify, `i-h-verify`) and 15.4-25.1 s for the 6,538 run again in the reuse runs (load-dependent); the reused
searches are the cheaper ones (about 0.8 ms each against 2.4 ms for a search run again: the long water searches touch
the most plots and see a change most often). The family shares of the sampled pair (`families-b1.txt`, 4668's
`families.py`): trade-path cache **26.1 % (pin) -> 24.2 % (b1)**, multi-destination searches 22.9 -> 21.8 %, path search
overall 52.9 -> 51.7 %. Memory: peak process commit **+35 MB** on H (372 -> 407 MB; the store is 29.0 MB at turn 305),
+1-2 MB on M.

### The narrow fingerprint, build b2 `[C, the code; why each narrowing is exact]`

b2 keeps b1's fingerprint as a runtime choice (`VP64_PROTO_TRADEPATH_FP=wide`) and makes a narrower one the default.
Each narrowing hashes only what the four callbacks can distinguish (the callbacks' code at 04b1211b8):

- **Unrevealed plots**: only the revealed bit. Both Valid callbacks return FALSE for a plot the team has not revealed
  before they read anything else; such a plot is never expanded, so it is never read as a "from" plot either (the
  origin is a revealed city of the player). A culture border, a road or a farm appearing in the fog no longer forces a
  search.
- **Owner and team**: `isOwned`, owner == the player, plot team == the player's team, and the war bit, instead of the raw
  owner and team. The callbacks read the owner only as `isOwned` (ice, enemy cities), `== player` (passing-route and
  improvement yields in `TradePathLandCost`, the canal shortcut) and the team only as `== team` (the woodland trait) and
  through `isAtWar(team, plotTeam)`; `isValidMovePlot` and `isCoastalCityOrPassableImprovement` stay hashed as results.
  Borders moving between two other players no longer force a search.
- **Revealed improvement**: only "is a barbarian camp" (`TradePathLandValid`'s one test). A farm revealed by a scout no
  longer forces a search.
- **Improvement type, visible resource, a city's passing-route yields**: only on the player's own plots (the only place
  `TradePathLandCost` and the canal shortcut read them); elsewhere the improvement matters only through passability,
  pillage and the two predicates, which stay hashed.
- **No destination list**: `GetMultiplePaths` searches with no destination and returns a path for every closed node
  that is a city plot (every city plot is in the destination list); every closed node is a touched plot, and "is a
  city" is in its hash, so a city founded, taken or razed changes the result only through a touched plot's hash.

In verify mode b2 also records why each stored search was not reused (the first mismatch: no entry, search inputs with
or without the destination list, or a touched plot's group: revealed, owner and war, terrain and improvement, city) and
which of those recomputes found the same paths.

**Build b2** `[C, 2026-11-17 11:49-11:59 EST]`: fork commit `a0bc356e8` (only `VP64ProtoTradePath.inl` changed; syntax
check of both changed translation units first), same command with `--out C:/c5-64bit-work/4673/build-b2`
(`C:/c5-64bit-work/4673/scripts/build_b2.sh`): every step rc 0, dll 523 s (link 413 s), narrowing 0 new. **b2 DLL SHA-256
`B991D9C8AAE141952256ADF5B771A527AE721D4A86B974B34E6584241A7A64EB`** (23,685,120 B). Guard check of
04b1211b8..`a0bc356e8`: OK (rc 0); negative test rc 1. Branch pushed.

### Results of b2 `[L, 2026-11-17 12:00-12:19 EST, workstation, shared: 7-9 other owners' heavy jobs registered]`

Every b2 run **plays the arm's game** (`identity.txt`): M reuse and verify, H reuse, verify and verify with b1's
fingerprint (`VP64_PROTO_TRADEPATH_FP=wide`), and the sampled H run: IDENTICAL to both references. The verify mode found
**no false reuse** (M 3,854 of 3,854; H 8,594 of 8,594). The wide fingerprint in b2 reproduces b1's decisions exactly (M
3,427 and H 7,620 would-reuse, H 5,448 equal recomputes), so the two builds differ only by the narrowing.

| | M 151-160, b1 (wide) -> b2 (narrow) | H 301-305, b1 (wide) -> b2 (narrow) |
|---|---|---|
| searches reused | 3,427 (66.3 %) -> **3,854 (74.6 %)** | 7,620 (53.8 %) -> **8,594 (60.7 %)** |
| run again, of them equal to the stored result | 1,741, 1,440 equal -> 1,314, 1,013 equal | 6,538, 5,448 equal -> 5,564, 4,474 equal |
| first mismatch of the searches run again (equal ones in brackets), wide | destination list 269 (230), search inputs 59 (13), owner and war 415 (378), revealed 379 (351), terrain and improvement 593 (451), city 20 (17) | destination list 928 (543), search inputs 38 (15), owner and war 1,745 (1,646), revealed 976 (891), terrain and improvement 2,847 (2,353) |
| the same, narrow | search inputs 59 (13), owner and war 279 (237), revealed 268 (234), terrain and improvement 684 (514), city 18 (15) | search inputs 38 (15), owner and war 1,834 (1,618), revealed 488 (412), terrain and improvement 3,200 (2,429) |
| store at the window's end | 0.8 MB | 29.0 MB (peak commit 408 MB against the pin's 372) |

(A first mismatch is classified by the first changed plot found; with the narrow hash a search that had a revealed or
destination change first may now stop at a later terrain or owner change, so the groups move between columns.)

**The trade-path family's share** (sampled H 301-305, attribution, not timing: `families-b2.txt`, the same sampler and
`families.py` as 4668): pin 26.1 % -> b1 24.2 % -> **b2 23.3 %**; its multi-destination searches 22.9 -> 21.8 ->
20.6 %; path search overall 52.9 -> 51.7 -> 50.9 % (one sampled run each, under 6-9 other heavy jobs). So b2 removes
about 2.8 points of the H turn, about 0.6-0.7 s of a 21-25 s turn `[I: share x turn]`, against 4669's counted bound of
20.8 % (4.34 s).

Why the saving is so much smaller than the reuse count `[L for the numbers, I for the reading]`:

- The rebuild's own time inside the runs: b2 reuse `j-h-reuse` 28.0 s over the window's 436 rebuilds, against about
  32.6 s for the same rebuilds with every search run (`j-h-verify`'s 47.2 s less its verify-only store of 13.8 s and
  validation of 0.8 s): about 4.6 s per 5 turns, 0.9 s per turn, in line with the share. The searches that still run
  cost more each in the reuse run (17.5 s for 5,564) than a fit over the verify run's rows gives them (1.9 ms each,
  10.7 s; `split-cost.txt`): the step finder's node grid is no longer warm from the reused searches before it, so the
  count of skipped searches overstates the time saved; the fit itself (which puts 60 % of the verify run's search time
  on the would-reuse searches) is not trusted for that reason.
- What still forces a search is mostly real input changes on touched plots: terrain and improvement (worker builds,
  roads), owner and war (borders, the 17 war and 17 peace changes per H turn), revealed plots. 80 % of the searches run
  again still find the same paths: an input-exact check cannot know that a changed plot was not on any path, so most of
  4669's "equal paths" (85.7 %) is out of its reach.
- The scoring (`AddTradePathToCache`) and the per-player danger setup stay in every rebuild by design.

### Build b3: each owner bit only where a callback reads it `[C, 2026-11-17]`

The owner-and-war group was the second cause of runs again on H (1,834, of them 1,618 equal). b3 (fork commit
`5a6c1a25d`, only `VP64ProtoTradePath.inl`; `VP64_PROTO_TRADEPATH_FP`: `wide` = b1, `narrow1` = b2, default level 2)
hashes each bit only on the plots where one of the callbacks reads it (code at 04b1211b8):

- the war bit only on a city or a plot where `isCoastalCityOrPassableImprovement(player)` holds: the two "from" plot
  tests (`TradePathLandValid`: an enemy city; `TradePathWaterValid`: an enemy coastal city or passable improvement) are
  its only readers; `isValidMovePlot` is called without the territory check;
- `isOwned` only on ice (unowned ice is refused), cities and such plots; owner == player only on cities and improved
  plots (`TradePathLandCost` reads the owner only to add the passing-route and improvement yields); team == team only on
  forest and jungle (the woodland trait);
- route and improvement pillage only through `isValidMovePlot` and `isCoastalCityOrPassableImprovement`, which stay
  hashed; the visible resource left out, and searches with `MOVEFLAG_PRETEND_CANALS` (its only reader; never set by
  `UpdateTradePathCache`) are not handled at this level.

Build as b2's (`C:/c5-64bit-work/4673/scripts/build_b3.sh`, 12:20-12:40 EST, of it 4.8 min waiting for the link place
behind 4674's link): every step rc 0, narrowing 0 new. **b3 DLL SHA-256
`163DEFCFFB028615C4486AF6380B62999DE6234EE84EC2A77898D94D14F18150`** (23,685,632 B). Guard check of
04b1211b8..`5a6c1a25d` OK; pushed.

### Results of b3, and the three builds side by side `[L, 2026-11-17 12:41-13:03 EST, workstation, shared: 4-9 other owners' heavy jobs]`

Every b3 run **plays the arm's game**: M reuse and verify, H reuse, verify and the sampled run, IDENTICAL to both
references (`identity.txt`). Verify mode: **no false reuse** (M 3,913 of 3,913; H 9,166 of 9,166).

| H 301-305 (M 151-160 in brackets) | b1 (wide) | b2 (narrow1) | b3 (level 2) |
|---|---|---|---|
| searches reused of 14,158 (5,168) | 53.8 % (66.3 %) | 60.7 % (74.6 %) | **64.7 % (75.7 %)** |
| run again, of them with the same paths (verify) | 6,538, 5,448 | 5,564, 4,474 | 4,992, 3,902 |
| false reuses (verify) | 0 | 0 | 0 |
| trade-path family's sampled share (pin 26.1 %) | 24.2 % | 23.3 % | **22.4 %** |
| multi-destination searches' share (pin 22.9 %) | 21.8 % | 20.6 % | 20.3 % |
| rebuild time as a share of the turn, reuse run against the same build's verify run (verify less its store and validation) | 21.6 % / 24.1 % | 21.8 % / 23.3 % | 21.2 % / 23.3 % |
| store at turn 305; peak process commit (pin 372 MB) | 29.0 MB; 407 MB | 29.0 MB; 408 MB | 29.0 MB; 408 MB |

b3's first mismatches on H (equal ones in brackets): owner and war 2,672 (2,224), terrain and improvement 2,240 (1,626),
revealed 38 (37), search inputs 38 (15). With 17 war and 17 peace changes per H turn (4669), the war bit of enemy
cities, which the callbacks do read, still forces many searches.

**The saving** (attribution and within-run shares, not timing): b3 takes the trade-path family from 26.1 % to 22.4 % of
the sampled H turn, **3.7 points, about 0.8-0.9 s of a 21-25 s H turn** `[I: share x turn]`; the rebuilds' share of
the turn falls by 1.5-2.5 points between a reuse and a verify run of the same build (each pair at a different load).
Against 4669's bound for this design (20.8 % of the turn, 4.34 s) that is about a fifth. On M the bound is 0.05 s per
turn and the prototype's effect is not separable from noise (its searches cost 0.5 s over the whole window).


### A timed pair on a test machine (prepared, NOT submitted) `[2026-11-17 12:05 EST]`

The lead asks the MC first. What is ready:

- Layouts on F: (`perf/evidence/0437/4673/make_layouts.sh C:/c5-64bit-work/4673/build-b3/dll b3`):
  `F:/c5-64bit-work/4673/layout-b3-M/bin` (the pin layout `x64-04b1211b-pfdee656` with the b3 DLL and PDB) and
  `F:/c5-64bit-work/4673/layout-b3-H/bin` (0452's `layout-new` with the b3 DLL and PDB), each with `SHA256.txt`. (The b2
  layouts `layout-b2-M`, `layout-b2-H` exist too; the pair uses b3.)
- The arm: `perf/evidence/0437/4673/arm-S3p.json`, an `S3p` entry for `perf/turnstudy/runner/arms.json` (S3 with the b3
  DLL `163DEFCF...`, the same servers and libraries, `scenario_binaries` for H as S3's). Not added to `arms.json` (no
  harness change in this ticket); the lead adds it on the epic branch, the testbed ships the submitting checkout's HEAD.
- The commands, sized by D178 (MC 11:20: 2-3 trials per arm, 10-20 turns, for development work): one interleaved set
  per scenario, the pin's arm S3 against S3p, nothing else differs; dry runs (`--print-only`, with S3p put into
  `arms.json` temporarily and the file restored by its blob hash `9e3f5757...`: `dryrun-pair.txt`) rc 0 on 2026-11-17
  12:52 with the b3 layouts:

      python -I -X utf8 perf/turnstudy/runner/submit.py run --host oldpc --scenario H --arms S3,S3p --runs 3 --turns 15 --order williams --quiet --expect-min 110 --grade B --series-id 4673-H-proto --label "4673 prototype (a) b3: trade-path searches reused while their inputs hold, S3 vs S3p" --issue 4673 --keep-going --timeout 9000
      python -I -X utf8 perf/turnstudy/runner/submit.py run --host oldpc --scenario M --arms S3,S3p --runs 3 --turns 15 --order williams --quiet --expect-min 25 --grade B --series-id 4673-M-proto --label "4673 prototype (a) b3: trade-path searches reused while their inputs hold, S3 vs S3p" --issue 4673 --keep-going --timeout 3600

  H: 6 runs of 15 turns (window 301-315) at about 21-25 s per turn plus about 90 s of load each and the 60 s idle gaps:
  about 50 min on the workstation's speed, `--expect-min 110` for an older machine. M: 6 runs of about 1 min. Grade B,
  not A: the arms differ by a DLL build, not by one setting of one build (4662's treatment exception); the lead or the
  performance team decides. The testbed ships the submitting checkout's HEAD, so `S3p` must be committed into
  `arms.json` there first. With n = 3 per arm only a large difference shows (PERFORMANCE.md's statistics still bind any
  default-on claim; this prototype is evidence only).
- Identity across the arms: `analyze.py` compares each run with its arm's first and the arms' firsts (statscmp from turn
  300); every S3p run must be IDENTICAL to S3's.

### Result of the laptop pair (D178) `[L, 2026-11-17 13:41-15:00 EST, laptop, job 20261117-131326-c8ac, series 4673-H-proto-d178, submitted by lead-0437 (sixth lead) from 3be9807a9, analysed 15:01 by lead-0437 (seventh lead); grade B (D178: n=3 per arm, evidence only)]`

H 301-315 (15 turns), headless S3 against S3p (build b3, reuse mode by default), 3 runs each, Williams order, quiet job
alone and undisturbed (foreign cores 0.041). Fetched with the stats.db files to `F:/c5-64bit-work/0437/4673-H-proto-c8ac`.

| | S3 | S3p | difference |
|---|---|---|---|
| seconds per turn, mean [95 % CI] | 17.793 [17.010, 18.576], cv 1.77 % | 17.006 [16.678, 17.333], cv 0.78 % | **-0.79 s (-4.4 %)**, CI [0.11, 1.46], ratio 1.046 [1.008, 1.086] |
| CPU s/turn | | | -0.79 s |
| peak private | | | **+35.5 MB** [29.5, 41.4] (the search store; 4673 estimated +35 MB) |
| identity (stats.db) | | | **IDENTICAL** over 300-317, each arm identical within |

The attribution above expected 0.8-0.9 s; the pair agrees. For epic 0504's go/no-go (MC #4431): both prototypes keep
the game identical and pay on H; (b) (-1.22 s, -6.9 %, no memory cost) is the stronger; (a) (-0.79 s, -4.4 %) costs
about 35 MB on H. The hand-overs per prototype are the lead's next step.

### Not verified (4673)

- **Any turn-time saving.** No timed comparison on a quiet machine was made (COMMON 23; the testbed pair is prepared,
  not submitted). The saving above is the sampled family share (one sampled run per build, under load) and within-run
  rebuild shares; the 0.8-0.9 s per H turn is an attribution, not a measurement.
- Exactness beyond these windows: the verify mode found no false reuse in M 151-160 and H 301-305 (about 13,000 checked
  reuses per build); the exactness argument is by reading the callbacks at 04b1211b8 `[I]`. Other saves, map scripts,
  the canal shortcut (not handled at level 2), the UI thread's cache (not handled) and a code change to the callbacks
  are not covered; a pin change needs the argument re-read.
- The verify mode's classification names the first changed plot's group only; a search with several changes counts once.
- Memory beyond turn 305 (the store holds one entry per player, origin and domain; it grows with cities, not turns) and
  the store's container overhead (estimated in the byte count, not measured).
- The b1 timing arms (run against COMMON 23) and the stopped `v-h-*` pair: kept, not used.
- A product-configuration build of the branch without the define (the guard check stands for it, as for 4669).

## W6 phase 2: prototype (b), a per-turn cache of the danger maps' enemy reach (ticket 4674; 2026-11-17)

Written by impl-4674, 2026-11-17 11:08-. The second prototype of 9.8 (`turn-time-study-analysis.md`): the danger maps'
enemy reach (H: 72,692 calls in 5 turns, 94 % equal to the same key's previous result, bound 1.72 s per S3 turn, 4669)
kept for the game turn and dropped per unit and per region. Evidence only: fork branch `x64-proto-0437-reach` from the
pin `04b1211b8`, define `VP64_PROTO_REACH`, never in a product build, nothing upstream (D95). Every time below is an
indication from the shared workstation (D106). Evidence `perf/evidence/0437/4674/`.

### What an enemy unit's reach reads (the design) `[C, source 04b1211b8]`

`CvDangerPlots::UpdateDangerSingleUnit` (`CvDangerPlots.cpp:60`) asks
`TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pUnit->plot(), flags, 0, pUnit->maxMoves(), zoc)`
(`CvTacticalAI.cpp:5559`) with flags `IGNORE_STACKING_SELF | IGNORE_STACKING_NEUTRAL | IGNORE_ENEMIES | IGNORE_DANGER` and
`IGNORE_ZOC` (`SELECTIVE_ZOC` only with `MOD_COMBATAI_TWO_PASS_DANGER`, which is 0 in `NewCustomModOptions.xml` and not
changed by VP). The data is `SPathFinderUserData(pUnit, flags, 1)` (`CvAStar.cpp:4177`): path type `PT_UNIT_MOVEMENT`,
the **unit's** owner and id, max turns 1, start moves = `maxMoves()`. So the question carries nothing of the observing
player (the observer only filters with `ShouldIgnoreUnit`, outside the search, and through the ZOC list, which is empty):
every player that sees the same enemy unit asks the same question, which is why 94 % of the calls repeat a result.

The search (`CvPathFinder::GetPlotsInReach`, `CvAStar.cpp:3333`; node data `UpdateNodeCacheData`, `CvAStar.cpp:940`;
`PathValid` 1560, `PathCost` 1393, `PathEndTurnCost` 1209, `CvUnitMovement::GetCostsForMove`; the result keeps plot,
turns, moves left and the normalised distance from the known cost, so end-turn costs count too) reads:

1. **The unit**: its plot, moves (`isOutOfAttacks()` reads them), max moves, base moves on land and embarked, damage, type,
   AI type, domain, embark and cargo state, human or automated, `CanEverEmbark`, promotions (terrain and feature costs,
   rivers, amphibious, hover, rough terrain, extra move discount, slow in enemy land, ranged or melee).
2. **Its team and player**: techs (route movement changes, bridge building, embarkation costs, passability), war and
   peace (enemy cities and units, `canEnterTerritory`, enemy roads), open borders, vassals, disengagement turns,
   policies and traits, buildings (border obstacles of the plot owner's team, player and city).
3. **Each plot the search reaches and each neighbour it tests**: terrain, plot type, feature, route and pillage,
   improvement and pillage (`defenseModifier`, unit limit), owner and owning city, a city on it, every unit on it (enemy,
   neutral, defender, stacking, delayed death, cargo, invisibility), and what the unit's **team** sees and has revealed
   there (visibility, revealed, revealed route and improvement, invisible-unit visibility). A unit that can embark also
   reads enemy ships next to a water plot it might enter (`GetNumEnemyUnitsAdjacent` in `PathValid`): one ring further.
4. **Anywhere**: a minor civ's ally (`MOD_GLOBAL_CS_OVERSEAS_TERRITORY` in `canEnterTerritory`, friendly territory),
   border obstacle counters, and for the jump nodes (airlift, sealift, admiral port) every city of the owner and its
   allied minors.
5. **The game turn**: disengagement turns, the barbarian release turn.

Not cached, always searched: explorers (`UNITAI_EXPLORE`, `UNITAI_EXPLORE_SEA`: their end-turn cost reads city-state
friendship, which moves with influence every turn), units with `PROMOTION_UNWELCOME_EVANGELIST`, a call with a non-empty
ZOC list, a search whose closed nodes include a sealift or admiral-port node (none on M or H), and every call before the
session's first game turn start (the load sets state without the hooked setters; build 1 showed one wrong hit in M's
first turn). *Corrected 2026-11-17 12:30 (build 3): searches that reach an airlift city were first not cached either;
build 2 measured them at 14,508 of H's 72,692 calls and 9.94 s of its 12.8 s of danger reach (7.7 % of the H turn),
so build 3 caches them (below).*

Inputs found by the verify runs and the code reading after the first design (build 2-3): `canEnterTerritory` for a major's
unit ending its move in a minor's land reads `isHasMet` (`CvTeam::makeHasMet`: both teams' epochs), the minor's
`IsFriends` (`CvMinorCivAI::SetFriends`) and `m_bAllowMajorsToIntrude` (`DoIntrusion`,
`DoUpdateNumThreateningBarbarians`), and the unit's exotic goods (`getNumExoticGoods`, in the snapshot); build 1's 9
wrong hits on H were all one player's units and ended with the snapshot field. A minor's ally, friends and intrusion
changes and the border obstacles bump **the plots that player owns** (a loop over the map; first a global epoch, which
build 1 counted at 24 bumps in H 301-305, each emptying the whole cache), plus the minor's team and, for an ally
change, the old and new ally's player epochs (airlift).

**Airlift** (build 3): `UnitPathGetExtraChildren` adds, once per search, every plot around every airlift-capable city of
the owner and of its allied minors that `canAirliftAt` accepts; that reads the target plot's units and terrain, the
city's `CanAirlift` (an airport) and its owner and ally, and the units on the six plots around the target. An airlift
entry's region therefore also holds the two rings around **every** city of the owner and its allied minors (whether or
not it can airlift today), a building change in any city bumps the city's plot (plus its owner's epoch), a city gained or
lost (`setIsCity`, `setOwner` on a city plot) bumps the owner's (both owners') player epoch, and an ally change the old
and new ally's. Airlift from an improvement adds no children at 04b1211b8 ("todo" in `UnitPathGetExtraChildren`).

**The key and the invalidation** (what the prototype does; the identity runs decide whether it is complete):

- **Key**: (owner, unit id) -> one entry; it matches only if the start plot and every `SPathFinderUserData` field used
  (`ePath`, `iFlags`, `iStartMoves`, `iMinMovesLeft`, `iMaxTurns`) are equal, and the **unit snapshot** (1.) is equal
  field by field. A promotion change, embark, transport change or death drops the unit's entry at once.
- **Team and player epochs** (2.): a counter per team, bumped by a tech change (`CvTeam::setHasTech`, at the start and
  the end), a `setAtWar` change (both teams), open borders (both), a vassal change (both), a building change in a city of
  the team (`CvCityBuildings::SetNumRealBuildingTimed`, `SetNumFreeBuilding`); a counter per player bumped by a policy
  change and the same building changes. An entry holds the values of its unit's team and player.
- **Region stamps** (3.): an event counter; a plot event writes the next value into the plot's stamp, a view event into
  the plot's stamp for that team. The region of an entry is every plot the search **closed** (`m_closedNodes`, incl.
  nodes later dropped by `CanEndTurnAtNode`) plus one ring (the neighbours `PathValid` tested), two rings for a unit
  that can embark. A hit needs every region stamp, and the region's stamps for the unit's team, at or below the counter
  value at the entry's search. Plot events: owner (`setOwner` after the member), owning city, city flag (`setIsCity`),
  plot type, terrain, feature, improvement and its pillage, route and its pillage, a unit leaving or entering
  (`CvUnit::setXY`, after the coordinates change and again after `addUnit`), a unit's death (`kill`, also delayed),
  embark, transport, promotion. Team view events: visibility switching on or off for the team
  (`changeVisibilityCount`, only when `count > 0` changes: `isVisible` reads only that count; 4669 counted 163,855
  count changes per H turn, most of them not a switch), `setRevealed`, revealed improvement and route, invisible-unit
  visibility switching (both counters).
- **Global epoch** (4.): a minor's ally change (`CvMinorCivAI::SetAllyInternal`), any border obstacle counter (team,
  player, city land and water). *Replaced in build 3 (2026-11-17 12:30): these now bump the plots the minor (the
  obstacle's owner) owns, see "Inputs found by the verify runs" above; the global epoch has no caller left.*
- **Turn** (5.): the cache is emptied at each game turn start (`CvGame::doTurn` after `incrementGameTurn`).

Why the war hook is per team and not global: a war between teams A and B changes only what units of A and B may enter
or attack; a unit of a third team reads A's and B's relations to itself, not to each other (minor allies, the one
indirect path, are in the global epoch). Bumping every entry on each of H's 34 war and peace changes per turn would
leave few hits.

**Run-time switch** `VP64_PROTO_REACH_MODE`: `on` (default) uses the cache; `verify` searches again on every hit,
compares the two results field by field (plot, turns, moves left, normalised distance, order) and plays the fresh one, so
a verify run plays the uncached game and lists every input the invalidation missed (`ReachCacheMismatch.csv`: the unit,
the first differing plot, whether it lies in the region, its stamps); `off` calls the search directly. Per game turn
`ReachCache.csv`: lookups, hits, misses by reason (cold, key, unit, team, player, global, region, region-team), not
cacheable, jump nodes, ms of all calls, of the misses' searches, of the hits' fresh searches (verify only: the time a
hit skips), the overhead (lookups, region building, copies), peak entries and bytes (entries: map node, region, result
and its index; plus the stamp arrays), events by kind; from build 2 also the jump searches' ms and kinds, from build 3
the airlift entries' stores, hits, and their searches' ms.

### The builds `[C, 2026-11-17]`

Fork branch **`x64-proto-0437-reach`** (MC #4179), one commit on the pin `04b1211b8`, amended while unpushed; pushed to the
project's fork on GitHub at **`0d15296d0`** (build 3), never upstream. Clone `C:/c5-64bit-work/4674/fork` (a clone of the
local fork clone). Files: `CvGameCoreDLL_Expansion2/VP64ProtoReach.h`, `VP64ProtoReach.inl` (included at the end of
`CvDangerPlots.cpp`), the call in `UpdateDangerSingleUnit` (`#if defined(VP64_PROTO_REACH) ... #else <the original line>
#endif`), `CvAStar.h` (an accessor of `m_closedNodes`), the PCH include, and hooks in `CvPlot.cpp`, `CvUnit.cpp`,
`CvTeam.cpp`, `CvPlayer.cpp`, `CvCity.cpp`, `CvGame.cpp`, `CvMinorCivAI.cpp`, `CvPolicyClasses.cpp`,
`CvBuildingClasses.cpp`. The commit message still describes build 1 (airlift searches "not cached"); the code comment
and this section are current (the message was not amended after the push, COMMON 22).

**Guard check** `perf/evidence/0437/4674/guardcheck.py` (4669's idea, stricter and general): every touched file of
`04b1211b8..x64-proto-0437-reach`, resolved as if `VP64_PROTO_REACH` were absent (blocks on the define dropped, an
`#else` branch kept), equals the pin's text line for line; the two new files are included only inside such blocks; no
project, property sheet or script changes. **OK** at `0d15296d0` (12 files). Negative test: the same check of
`x64-count-0437` against `VP64_COUNT_REACH` fails (its other defines' blocks and new files remain). 4669's own
`guardcheck.py` is hard-wired to `VP64_COUNT_...` and the `VP64Count` files, so it does not apply to this branch as
written. No product build sets the define: it is passed only through `CL=-DVP64_PROTO_REACH` in
`C:/c5-64bit-work/4674/scripts/build_proto.sh`.

| Build | Fork commit | DLL SHA-256 | What changed |
|---|---|---|---|
| 1 | `0b5e924a1` (unpushed, amended) | `EC5FC0852AB23115A66A37EB89B863BBA056042B34D51F005A341AA9B1BE6065` | the first design |
| 2 | `bc17d244e` (unpushed, amended) | `4BFE43C0E3BB37F8D536209F82478BD2A22325C232BAAB64E624117B014FF7CC` | exotic goods in the snapshot; nothing cached before the first turn start; jump timing; mismatch diagnostics |
| 3 | **`0d15296d0`** (pushed) | **`F09FAD8BCA00BA4CDB785DAC35F20D8CB815D982AFEECA4F89984E0FD9552C7F`** | airlift searches cached; minor ally, friends, intrusion and border obstacles bump the player's plots; `makeHasMet`; city gained or lost and building changes for airlift |

Each: `python -I tools/x64-dll/build_all.py --src C:/c5-64bit-work/4674/fork --out C:/c5-64bit-work/4674/build-proto
--config release --offline --unlocked` with `CL=-DVP64_PROTO_REACH`, every step rc 0, narrowing gate 794 against 798
(0 new), version "Release-5.4.8-52-g0d15296d0f Clean" for build 3 (23,688,704 B; the pin's DLL is 23,655,424 B). Build 3's
DLL and PDB: `C:/c5-64bit-work/4674/b3/`; with the arm S3's other files in `F:/c5-64bit-work/4674/layout-M/bin` and
`layout-H/bin` (`SHA256.txt` in each).

### Runs `[L, 2026-11-17 11:45-12:52 EST, workstation, shared: 5-10 heavy jobs of other owners registered at each start (load files in the evidence)]`

`perf/evidence/0437/4674/run_reach.sh SCEN NAME MODE` -> `run_reach.py` (4669's `run_count.py` with the prototype's
switches: the arm S3's set-up of 4668, the layout bin with the DLL replaced, under `tools/harness/jobcap.py` 16 GB, plays
to the window's last turn + 2; `--profile-from` adds 4668's sampler at 10 ms); identity `identity.sh` =
`perf/evidence/0438/idtrace.py` over M 150-160 against the M series' `r01-p4-S3` and 4668's `m1`, over H 300-305 against
0452's `s3new-H10` and 4668's `h1` (the references 4669 used). Evidence: `perf/evidence/0437/4674/b1/`, `b2/`, `b3/`
(per run the reduction `.txt`/`.json`, `ReachCache.csv`, `ReachCacheMismatch.csv`, `run.json`, turn times, load;
`identity.txt`; `families.txt`); run folders `C:/c5-64bit-work/4674/runs/`.

**Identity: every run of every build plays the arm's game** (`statscmp` IDENTICAL against both references; M 15 tables,
32,631 rows; H 14 tables, 125,210 rows): build 1 M verify, M on, H verify; build 2 H verify; build 3 M verify, M on, H
verify, H on, and the four profiled runs (M and H, modes on and off) [L]. No variant is void.

**What the verify runs found** (a hit's cached result against a fresh search, field by field):
- build 1: M 1 wrong hit (turn 150, the first turn after the load: an entry made during the load); H 9 wrong hits, all
  units of player 1 (one unit 16504 four times in turn 304 and four in 306, one more in 302), the first differing plot
  in the region with no event since the entry. Both runs' games stayed identical (a verify run plays the fresh
  result; the M `on` run was identical too, so that wrong set did not change a decision there).
- build 2 (exotic goods in the snapshot, no caching before the first turn start): **0 wrong hits** on H. Build 3: **0**
  on M and on H. So the event list of build 3 holds on these windows; the 3 unequal repeats 4669 saw in
  `FindUnitsWithinStrikingDistance` are another caller and not cached here.

**Hits, misses and the time the hits skip** (build 3; the verify run measures the skipped searches; shares are of the
run's own turn time, so the machine's load moves both: attribution, not timing, D106):

| | M 151-160 | H 301-305 |
|---|---|---|
| danger reach lookups (= 4669's `UpdateDangerSingleUnit` calls) | 9,996 | 72,692 |
| hits (rate) | 7,802 (78.1 %) | **60,086 (82.7 %)**, of them 10,483 on airlift entries |
| misses by reason: cold / key / unit / team / player / region / region-team | 1,481 / 47 / 64 / 118 / 19 / 397 / 24 | 2,985 / 341 / 449 / 2,723 / 767 / 5,038 / 268 |
| not cached (explorers, evangelists) | 44 | 35 |
| wrong hits (verify) | 0 | 0 |
| hits' searches, skipped in `on` (verify run) | 0.206 s, **1.39 %** of the turn | **9.80 s, 6.99 %** (airlift 7.16 s, 5.10 %) |
| misses' searches (verify run) | 0.091 s, 0.61 % | 3.75 s, 2.67 % (airlift 2.97 s) |
| the cache's own cost in `on` (lookups, regions, copies) | 0.052 s, 0.24 % | 0.61 s, **0.43 %** |
| all danger reach calls, `on` run | 0.89 % (0.85 % profiled run) | **3.12 %** (3.17 %) |
| all danger reach calls, `off` run (profiled; no cache) | 2.00 % | **9.09 %** |
| cache peak: entries, bytes (incl. regions, results); stamp arrays | 184, 0.20 MB; 1.1 MB | 589, **1.94 MB**; 3.25 MB |
| server peak working set `on` / `off` | 197 / 197 MB | 312-313 / 309 MB |

In S3 seconds `[I]`: H's danger reach falls from 9.1 % to 3.1 % of the run's turn, about **1.25 s of a 20.9 s S3 turn**
(the skipped 6.99 % less the 0.43 % overhead gives about 1.37 s; 4669's bound for the danger reach was 1.72 s); M about
1.1 % of 1.424 s = **0.016 s** (bound 0.022 s). Memory: about 5 MB on H (4669's estimate of 70-96 MB counted every
key's plots for every call; one entry per unit, results of 20-60 plots and regions of a few hundred plots are far
smaller). Without airlift (build 1) the H hits skipped only 1.79 % of the turn: the airlift searches (an enemy unit in
or next to an airport city reaches every airport city's surroundings) are about 5 of the 7 points.

**Sampled family shares** (4668's `families.py` on the profiled runs, the same DLL in modes `off` and `on`, 10 ms
sampler over H 301-305 and M 151-160; 15,499 / 15,833 walks on H, 1,766 / 1,692 on M; attribution, not timing):

| family (inclusive) | H off | H on | M off | M on |
|---|---|---|---|---|
| enemy units' reach (`CvDangerPlots::UpdateDangerSingleUnit`) | 9.4 % | **3.8 %** | 3.0 % | 0.8 % |
| danger plots (`CvDangerPlots`) | 11.9 % | 7.0 % | 3.9 % | 2.5 % |
| reachable sets (`CvPathFinder::GetPlotsInReach`) | 24.8 % | 19.2 % | 16.1 % | 17.6 % |

(4668's h1 had the enemy reach at 10.2 % and the reachable sets at 24.9 %; M's 1.7k walks are too few for a point or
two.) The ranged units' attack plots (`GetPlotsUnderRangedAttackFrom`) stay outside the cache and are part of the
remaining 3.8 %.

**Timing arms: none** `[2026-11-17 12:52:59 EST]`: lead-0437's rule of #4190 (a workstation arm that compares time runs
only when `quietlock.py check` lists no other owner's heavy job or window, at most 30 minutes of attempts): the check
listed 8 heavy jobs of other owners, so no interleaved timing pair ran; the numbers above are attribution inside each
run. The timing is the testbed pair's (below).

### The testbed pair (prepared, NOT submitted)

For the lead to take to the MC: S3 against **S3r** (S3 with build 3's DLL; the cache is on by default, no environment
needed) on H, where the saving is. The arm is proposed in `perf/evidence/0437/4674/arms-S3r.json` (not applied:
`perf/turnstudy/runner/arms.json` is the epic's file); with it added to `arms.json`:

```
python -I -X utf8 perf/turnstudy/runner/submit.py run --host oldpc --scenario H --arms S3,S3r --runs 6 \
    --order williams --quiet --expect-min 200 --grade B --series-id 4674-H-reach \
    --label "4674 prototype (b): danger-reach cache (S3r) vs S3, H 301-325" --issue 4674 --keep-going --timeout 21600
```

12 runs of 25 turns (H's scenario window); about 9 min per run on the workstation [I], oldpc's speed on H not measured,
hence 200 min planned. Expected difference about 6 % of the turn [I from the attribution above]; oldpc's run-to-run cv
of 0.15-0.22 % (`quiet-testbed.md` 5) resolves it with 6 runs per arm. Before the submit: add the arm, run `--print-only`
(it checks every hash: the layouts' `SHA256.txt` agree with the arm's list), and identity of S3r against S3 by `statscmp`
over the window (the runner's count runs or a `statscmp.py --from-turn 300` of one run each). Not run: `--print-only`
(the arm is not in `arms.json` on this branch).

### Result of the laptop pair (D178) `[L, 2026-11-17 13:04-13:40 EST, laptop, job 20261117-130341-b874, series 4674-H-reach-d178, submitted by lead-0437 (sixth lead), analysed 14:55 by lead-0437 (seventh lead); grade B (D178: n=3 per arm, evidence only)]`

H 301-315 (15 turns), headless S3 against S3r (build 3), 3 runs each, Williams order, quiet job alone and undisturbed
(foreign cores 0.026-0.028). Fetched to `F:/c5-64bit-work/0437/4674-H-reach-b874`; the stats.db files followed at 15:01 and
the rerun analysis finds S3r against S3 **IDENTICAL** over 300-317 (statscmp, 15 tables), each arm identical within.
*(Corrected 2026-11-17 15:40 by lead-0437; it said "identity on the laptop not yet checked".)*

| | S3 | S3r | difference |
|---|---|---|---|
| seconds per turn, mean [95 % CI] | 17.756 [16.906, 18.607], cv 1.93 % | 16.534 [15.823, 17.245], cv 1.73 % | **-1.22 s (-6.9 %)**, CI [0.50, 1.95], ratio 1.074 [1.030, 1.119] |
| CPU s/turn | 17.62 | 16.41 | -1.21 s |
| peak private | 392 MB | 393 MB | no difference resolved |

The attribution above expected about 6 % (about 1.25 s); the pair agrees. Not a default-on claim (n=3).

### Not verified (4674)

- Any timing: no interleaved pair (the machine was never free of other owners' jobs); every share is from a shared
  workstation inside one run; the S3 seconds are those shares times the series' turn [I].
- Windows other than M 151-160 and H 301-305: an input the hooks miss could show elsewhere (sealift and admiral-port
  searches never occurred here and are not cached; the two-pass danger option, explorers and evangelists are not
  cached). The verify mode is the tool for another window: `run_reach.sh SCEN NAME verify`.
- A product-configuration build of the branch without the define (the guard check stands for it).
- The commit message of `0d15296d0` describes build 1 (see above).

## W6 phase 2: the hand-overs of prototypes (a) and (b) to epic 0504 (ticket 4680; 2026-11-17)

Written by impl-4680, 2026-11-17 15:49-16:15 EST, for lead-0437 (MC #4431; the Architect's review of the speedup epic,
section 6: 0437 says go or no-go and hands each prototype over; 0504 writes the product code). No code, build or run;
a reading of the two prototype branches and of the DLL at the pin `04b1211b8`. The hand-overs are the files 0504 starts
from:

- **(b) the danger-reach cache** (ticket 4674): `perf/evidence/0437/4680/handover-b-danger-reach-cache.md`. **GO**
  (lead-0437): laptop pair b874 H S3r -1.22 s (-6.9 %), CI [0.50, 1.95], IDENTICAL, no memory difference resolved; fork
  `x64-proto-0437-reach` `0d15296d0`, DLL `F09FAD8B...`.
- **(a) the trade-path search reuse** (ticket 4673): `perf/evidence/0437/4680/handover-a-trade-path-search-reuse.md`.
  **GO, with +35 MB on H** (lead-0437): laptop pair c8ac H S3p -0.79 s (-4.4 %), CI [0.11, 1.46], IDENTICAL, peak
  private +35.5 MB [29.5, 41.4]; fork `x64-proto-0437` `5a6c1a25d`, DLL `163DEFCF...`.

Each file holds the laptop pair and the workstation attribution, the fork branch, commits and DLL hashes, every input of
the cached result with its writers at the pin (file:line), how each writer is covered (by value, an epoch or a stamp,
or the turn start) and why the rest cannot change the result where it ran, the verify mode (how to run it, what it
compares, 0 false reuses or wrong hits where measured), the tests and runner arms (`S3p`, `S3r`), how 0504 should
re-prove identity, and the known gaps. The laptop pairs' analysis texts are copied beside them
(`laptop-c8ac-analysis.txt`, `laptop-b874-analysis.txt`).

Findings of the writer census that the prototype sections above do not state `[C, the pin's source; I for the effect]`:

- **(b), writers without a hook**: `SetPolicyBranchBlocked` (`CvPolicyClasses.cpp:5529`) applies or removes policy
  effects through `processPolicies` without `SetPolicy`; `InitPlayerTraits` from `setLeaderType` and the four religion
  calls (it recomputes the same values while leader and techs are unchanged); `setRevealedOwner`
  (`CvPlot.cpp:11781`), read by `canMoveInto` for **human** units only (`CvUnit.cpp:5228`), so human games are not
  covered; units added to a plot outside `setXY` (`CvDllPlot.cpp:330, 336`, `CvPlot::Validate` `CvPlot.cpp:14561`);
  river edits. Several hooks fire before the setter's own effects (`setHasPromotion`, the building setters,
  `SetPolicy`, `kill`, `setImprovementType` between its two `SetImprovementPassable` writes): the product should bump
  at the end. The cache is emptied only at a turn start, and nothing-before-the-first-turn holds once per process, so a
  second load in the client keeps stale entries.
- **(b) and the A10 generation wrap**: the danger searches run on `GC.GetPathFinder()`, the finder of every core-thread
  unit path; a hit skips a search, so every later search's 16-bit generation id shifts and a stale node read at a wrap
  (`CvAStar.cpp:355-357`, `:946`) falls on a different search than in stock. Not seen in the identity runs; A10 (D181)
  removes it. (a) is not affected: the step finder reads no generation-stamped node data.
- **(a), inputs read but not hashed**: the river edges (`isRiverCrossing` of the from plot, `IsAlongSameRiver` ->
  `IsRiverSide` of both plots), hashed only as `isRiver`; writers are the river setters, the map load and three Lua
  methods (`Lua/CvLuaPlot.cpp:1390, 1402, 1414`). The store is never emptied (game start, load) and the lookup runs
  before `GetMultiplePaths` takes the game core lock, with only a global scope flag: a ui-thread trade rebuild while the
  core thread's scope is open would share the core thread's store (the Architect's two-thread rule).

Not verified (4680): nothing was run; the census covers the member writes and setters named in the files (grep of the
pin), not every caller of every counter behind the unit's moves, which the snapshot compares by value; whether the
CP, VP and EUI Lua edits rivers in play, and whether the EXE calls the plot interface's `AddUnit` during a turn.

## Option 2 value 2: drain after each AI player (ticket 0571, D191; 2026-11-19)

impl-0571 (account B), 2026-11-19 17:33- EST, branch `task-0571-skipvisualdrain-2`. The design was 0568's variant F1
(`d165-pacing-fix-design.md` section 11.4, the MC's answers in 11.7): hold the next AI request in option 3's slot-102
replacement while queue B drains. **Built first as designed (the "hold form", commit cd62e2f06, arm `S4v2`), it drains
nothing**; on the MC's yes (#7167, 2026-11-19 18:15) value 2 is now **F1 with the EXE's own gate** (the "gate form",
commit 2aca6e6b2, arm `S4v2g`). A prototype, off by default; the default and any release of value 2 follow a laptop
set graded by PERFORMANCE.md and come back to the MC. 0.1b keeps `AiSkipVisualDrain = 1` (D197).

### Why the hold cannot drain: queue B moves nothing while the AI-turn byte is 1 `[L, 2026-11-19, oldpc]`

The hold form ran in oldpc job `20261119-175726-ef97` (M 151-160, pin batch 5, one run per arm, 4661's poller, not
timing): every one of its 258 holds in 10 turns ended at the 8-frame cap, none on "queue B empty and dispatched";
the undispatched records at a gap averaged 3,216 (max 4,581: the whole turn's backlog). The poller shows why: **queue
B's write half never shrinks while the AI-turn byte 0x2DE0447 is 1, even with no `Update` pending**: 0 shrinks over
3,018 (options 2+3), 3,934 (2+3+4) and 41,793 (the hold form) samples with the byte 1, no `Update` pending and records
waiting; every shrink (3,465 / 74 / 73) came with the byte 0. During a hold the samples show the frame accumulator
stepping by one frame's dt eight times with `qd` = 0 and `qw` unchanged (136,560 bytes). Option 2's write of
`GameViewState+0x8CC` keeps the byte at 1 through the whole AI turn (4665), so nothing of queue B moves until the byte
drops at the phase's end, whatever the proxy holds. This corrects 0568's 11.1 [I] ("the main thread waits in UI Lua"),
corrected in place there. Which effect of the byte gates the pump or its move (the byte, or queue B's flag 0x2A10F00
that 0x50A170 holds while the byte is 1) was not read [I]; `../02-exe-internals/game-core-thread.md` 6.3's "only while
no `Update` is pending" is true but not the whole gate.

Result of the hold form (oldpc, one run each, an indication): seconds per M turn 2+3 6.55, 2+3+4 3.21, hold form
6.42: the 8 frames per gap only added wait.

### The gate form `[C, the code]`

`[Performance] AiSkipVisualDrain = 2` (options range 0-2; 3 and anything else gives the default 0 with one warning):

- **At each AI gap** (option 2's scan found the next turn-active AI player, not the active human; in `SkipAfter` on
  the game-core thread while the `Update` still counts as pending) the proxy walks queue B once (option 4's stable walk:
  two control-block reads around the walk, at most 3 attempts) and counts the **undispatched records**: every record of
  the write half plus the dispatch half's records at or after the dispatch offset `q+0x2C`.
- **More than `kHoldFloorRecords` = 100**: option 2 does **not** write +0x8CC. The frame's own gate (game-core-thread.md
  6.2 skip 2) then sees the player change, drops the AI-turn byte, waits until queue B is empty and accepts the next
  player, as the EXE does. With option 4 active and its guards passing at that gap (Quick Combat and Quick Movement on,
  no held record, no flag-bit-1 record in the write half, the byte 1) the proxy also clears the ordering byte `q+0x30`
  there, so that drain is one pump; it is set back at the next `Update`'s top, as at the end of the phase. Option 4
  refusing leaves that gap to the EXE's ordered drain.
- **At most 100** (most city states), or the walk unstable: +0x8CC written as with value 1.
- No hold, no slot-102 copy: value 2 is independent of `AiUpdateTickMs`.
- Value 2 without `AiDrainUnordered = 1`: one warning line that it gains little (big gaps drain in the EXE's order).
- **Fail loud**: a failed read of queue B at a gap is option 2's failure during play (checks `skip-gap-read`,
  `skip-gap-queue`: the D118 report without popup; no more writes or clears in that proxy load; option 4 stops with it,
  its byte set back first).
- **Counters** (every 60 s, at unload, in a D118 report): `pacing: option 2 value 2 counters: gaps N (to the EXE's gate
  N; written: at most 100 records N, queue changing N); undispatched records at a gap max N (all gaps N); walks N us
  (max N us); option 4 at gated gaps: cleared N, not cleared: settings N, held N, flag bit 1 N`.

Cost of a gated gap [I, 0568's model]: the per-player UI refresh and the byte's round trip that option 2 removes (G,
about 61 ms on the laptop) plus the drain's frames.

Files: `proxy/src/proxy_pacing.{h,cpp}`, `options/` (range, help text), `docs/OPTIONS.md` (value 2, the floor),
`proxy/tools/rt_tests/pacing_unit.cpp`; runner: `perf/turnstudy/runner/tslib.py` (variant value 2; option 4 with 1 or
2), arms `S4v2` (hold form) and `S4v2g` (gate form) in `arms.json`, M variants `skip2_drain1`, `pacing_skip2_drain1`,
`v0571_drain1`, `v0571_skip2`; `perf/tests/turnstudy/test_turnstudy.py` (the arms set, with 0578's `S4g`, which main
lacked), `test_skipoption.py` (2 accepted, 3 refused).

### Tests `[L, 2026-11-19, workstation]`

- `pacing_unit`: **315/315** for the gate form (349/349 for the hold form) with the proxy's toolchain (clang-cl -m32,
  VC9 headers) and under MSVC x86 `/fsanitize=address` (canary caught, no report): value 1 never reads queue B at a gap
  (pages guarded); the floor (100 written, 101 not written); dispatched records before `q+0x2C` not counted, an offset
  past the half's end counts none; no gap in the human's turn, at the end of the phase, for the active human, with the
  hold set; an unstable walk writes; option 4 at a gated gap and each of its guards refusing; the end of the phase as
  built; every failure path with its report (`perf/evidence/0571/run_pacing_unit.txt`).
- `test_options` x86 and x64: **447/447** each, and under ASan (`options/test_asan.py`):
  value 2 accepted, 3 refused with the default and one warning ("not a whole number from 0 to 2").
- Turn-study runner tests (`test_turnstudy`, `test_skipoption`, `test_drainoption`, `test_tickoption`): 117 passed.

### The builds `[C, 2026-11-19, workstation]`

`perf/evidence/0571/build.sh` (0578's script; the pin's source as a plain copy whose fingerprint matches
`deps/dll.lock`, because the worktree guard refuses a git command on the fork clone; the tag DLL as a file):
`server/build_server.py --config release --offline`, **DLL pin batch 5** (1fd51353e; the build_all deps a copy of batch
4's, as 0579 did: the server is built from `--src`, the deps DLL is not packed); VP64 = `tools/release/pack.py` over the
pin layout `x64-1fd51353-p1e09fc7`'s x64 DLL **78355A75...** and libraries; the RC's vpproxy.ini text, the tag DLL
262AEE88 as the fallback.

| Build | Commit (product code) | server.exe | proxy | vp64helper.exe | Layout | Arm |
|---|---|---|---|---|---|---|
| b1, hold form | 9af6133dc (cd62e2f06) | 75975883 | ED758941 | 63FB326C | `C:/c5-64bit-work/0571/b1/layout` | `S4v2` |
| b2, gate form | aafedfbeb (2aca6e6b2) | 9EEFED0E | 014A6533 | 5022EF5D | `C:/c5-64bit-work/0571/b2/layout` | `S4v2g` |

Hashes: `perf/evidence/0571/layout-SHA256.txt`, `layout-b2-SHA256.txt`. Batch 5's finder fix changes M from turn 157
against batch 4 (COMMON 18): no number here compares with 0563's or 0570's.

### The gate form on oldpc `[L, 2026-11-19 18:22-18:30 EST, oldpc job 20261119-182234-778c, one run per arm, poller on: an indication, not timing]`

| Arm (S4v2g) | s per M turn | queue B after the last AI player's `Update` | end-of-phase gap | longest single wait (median / max over 151-160) | queue B max per turn |
|---|---|---|---|---|---|
| 2+3+4 (`pacing_drain1`) | 3.315 | 471 KB | 0.592 s | 0.592 / 1.111 s (the end of the phase) | 645 KB |
| value 2 + 3 + 4 (`pacing_skip2_drain1`) | 4.500 | 22 KB | 0.075 s | 0.297 / 0.557 s (between two AI players) | 80 KB |

(2+3 on the same machine, job ef97: 6.55 s, its end-of-phase gap 3.97 s median, 5.06 s max.) "Longest single wait" =
the longest gap between an AI `Update`'s end and the next request (D_B + T_tick) in a turn's AI phase
(`perf/evidence/0571/longest_wait.py` on `perf/turnstudy/pacing/pacing.py`'s periods; `longest-wait-oldpc.txt`).

Proxy counters of the value-2 run (`pacing-lines-oldpc.txt`): 287 gaps in about 11.5 turns, 152 left to the EXE's gate
(each with option 4's clear), 135 written (at most 100 records); undispatched records at a gap max 809 (all gaps
55,113), against 4,581 for the hold form: queue B now drains between the AI players. Option 4's end-of-phase walk found
at most 326 records (4,353-4,706 with the hold form). So the gate form does what D191 asked: the map is current after
each major civ, the long end-of-phase wait is gone (0.075 s), the longest single wait halves, for about +1.2 s per M
turn over 2+3+4 here (+36 %) and still about 2 s under 2+3. The laptop set below decides the numbers.

**Game identity [L]**: statscmp IDENTICAL (15 tables, 39,229 rows, turns 150-162) for value 2 (gate form) against
2+3+4 and against 2+3, the hold form against 2+3+4, and 2+3 against 2+3+4 (`perf/evidence/0571/statscmp-oldpc.txt`;
windowed vs windowed, D164).

### What the player sees: the video `[L, 2026-11-19 18:23-18:38 EST, oldpc job 20261119-182305-de4f, DIAGNOSTIC]`

0567's recorder (`perf/evidence/0567/vidsubmit.py`, the job's run records name issue 0567: the script's fixed issue,
not a mistake) on arm `S4v2g` with the variants `v0571_drain1` (2+3+4) and `v0571_skip2` (value 2 + 3 + 4), Quick
Movement on, the observer following player 0, M from turn 150, turn 152's AI phase recorded at 12 frames a second;
side by side by `perf/evidence/0571/make_video.sh`: `F:/c5-64bit-work/0571/videos/0571-234-vs-v2.mp4` (6,108,725 B, not
in git). Turn 152's AI phase: 2+3+4 2.62 s, value 2 4.88 s (one run each). What it shows: with 2+3+4 the banner runs
through the players to "Processing Turn For Barbarians" by 2.5 s with the map unchanged, then everything appears at
once and the human turn starts at 2.6 s; with value 2 the banner steps through the players ("Processing Turn For
Greece" at 2.5 s) and the units of each major civ move on the map right after its turn, with no burst at the end. Stills
in Visual Checks `vc-20261119-183937-workstation-6d4b` (2.5 s into the turn) and `vc-20261119-183938-workstation-3773`
(4.0 s: 2+3+4 already in the human turn), `--result info`.

### Laptop set 1: the hold form (D178) `[L, 2026-11-19 17:56-18:47 EST, laptop, job 20261119-175628-bbc0, series 0571-M-skip2; grade B (D178: n=3 per arm)]`

Arm `S4v2` (b1), M 151-160 (save turn 150), 3 runs x 10 turns per arm, Williams order, quiet and alone (foreign cores
0.05-0.07), statistics logging on, AI logging off, Quick Combat and Quick Movement on as saved; GPU drivers (D175, run
records): Intel HD Graphics 630 27.20.100.9320, NVIDIA Quadro P3000 32.0.15.7322; no driver change.
`perf/evidence/0571/analysis-bbc0.txt` (`perf/turnstudy/runner/analyze.py`).

| Arm | s per M turn: mean (sd; 95 % CI) | memory growth MB/turn | CPU s/turn |
|---|---|---|---|
| 2+3 (`skip1_tick0`) | 8.916 (0.067; [8.750, 9.082]) | 2.70 | 16.86 |
| 2+3+4 (`drain1`) | 4.505 (0.047; [4.388, 4.622]) | 6.53 | 9.04 |
| hold form + 3 + 4 (`skip2_drain1`) | 8.926 (0.100; [8.677, 9.174]) | 1.43 | 17.31 |

Hold form against 2+3: -0.010 s (-0.11 %), 95 % CI [-0.215, +0.195], smallest detectable 0.27 s: no difference
resolved. Against 2+3+4: +4.421 s (+98 %), CI [+4.211, +4.631]. Why: every gap was held the full 8 frames (287 of 287
gaps released by the cap, none by "empty", undispatched records at a gap up to 4,824; the proxy's counters), queue B
did not move during the holds (the poller section above), and option 4's end-of-phase clear still came every turn
(12 of 12 in a run): the extra time is the holds themselves, about 26 gaps x 8 frames x the laptop's frame (2,296 held
frames in a run's 3 minutes; about 21 ms each [I]) = about 4.4 s per turn. That it lands on 2+3's time is a
coincidence of sizes [I]. Identity: IDENTICAL within and across the three arms (statscmp, turns
150-162). The hold form is not a candidate; the gate form's set follows.

### Laptop set 2: the gate form (D178) `[L, 2026-11-19 18:47-19:42 EST, laptop, job 20261119-182324-9e21, series 0571-M-gate; grade B (D178: n=3 per arm)]`

Arm `S4v2g` (b2), the same set-up as set 1 (M 151-160, 3 x 10, Williams, quiet and alone, foreign cores 0.05-0.06,
statistics logging on, AI logging off, Quick Combat and Quick Movement on; GPU drivers Intel HD Graphics 630
27.20.100.9320, NVIDIA Quadro P3000 32.0.15.7322, unchanged). `perf/evidence/0571/analysis-9e21.txt`.

| Arm | s per M turn: mean (sd; 95 % CI) | memory growth MB/turn | CPU s/turn |
|---|---|---|---|
| 2+3 (`skip1_tick0`) | 8.950 (0.068; [8.782, 9.118]) | 2.49 | 17.09 |
| 2+3+4 (`drain1`) | 4.513 (0.058; [4.369, 4.657]) | 6.31 | 9.09 |
| **value 2 (gate form) + 3 + 4** (`skip2_drain1`) | **6.524** (0.039; [6.428, 6.620]) | 4.83 | 11.53 |

- Value 2 against 2+3+4: **+2.011 s per M turn (+44.6 %)**, 95 % CI [+1.893, +2.130], smallest detectable 0.16 s.
- Value 2 against 2+3: **-2.426 s (-27.1 %)**, CI [-2.564, -2.287], smallest detectable 0.18 s.
- Memory growth: value 2 against 2+3+4 -1.21 MB/turn, CI [-3.13, +0.71]: not resolved (n = 3, 10 turns).
- Identity: IDENTICAL within and across the three arms (statscmp, turns 150-162).
- Counters (each value-2 run): 211-214 gaps, 114-115 left to the EXE's gate (every one with option 4's clear), 97-99
  written; undispatched records at a gap at most 809; the gap walks 1.5-1.9 ms in all per run (max 160 us).

**Why it moved [L for the parts, I for the split]**: the oldpc poller (section above) shows the work moving: D_B (the
drain after an `Update`) grows from 0.62 to 2.11 s per turn because each gated gap now waits for queue B and its own
drain, while the end-of-phase drain falls from 0.59 s to 0.075 s and queue B never holds more than 80 KB. About 11-13
gaps per M turn go to the gate, so each costs about 0.15-0.18 s on the laptop [I: 2.0 s / 11-13]: the AI-turn byte's
round trip and the per-player UI refresh that option 2 otherwise skips (0568's G, modelled 61 ms), the pump of that
player's records with the ordering byte cleared, and the frames until the next request. 0568's model said 6.70 s for A3
(the stock gate at every gap) and 5.0-5.3 s for the hold variants, which cannot drain (above); the measured 6.52 s with
the floor sits just under A3's figure.

**Where this leaves value 2**: it does what D191 asked (each major civ's moves shown right after its turn, no catch-up
burst at the phase's end, the longest single wait about 0.3 s against 0.6 s with 2+3+4 and 4 s with 2+3; the video),
and it costs about 2.0 s per M turn against 2+3+4 while staying 2.4 s under 2+3. Off by default; whether to offer it,
and as what default, is the MC's and the owner's (MC #7167: the default and any release come back to the MC). Not measured:
Quick Movement or Quick Combat off (option 4 then refuses at the gaps and value 2 drains them in the EXE's order: about
stock pacing for big gaps [I]), the huge map H, a human watching the AI phase.

## The frame between two AI Updates, with a human seat (ticket 5030, D229; 2026-11-22)

impl-5030 (account B), 2026-11-22 20:48 onwards, branch `task-5030-frame-between-updates`; the measuring proxy on the
scratch branch `scratch-5030-framesplit` (never merged; deleted after the hand-back, MC #10686). The question (the
Architect's draft T1, first step; `research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md`
sections 2, 4 and 5): with options 2+3+4 one frame still separates two AI Updates, 1.23 s of the 4.63 s laptop M turn
in 0437's trace (b) (section "Design note" above, job a4de), taken with autoplay's observer seat. What is that frame made
of (render, the UI update pass, Lua, the frame cap) when a human sits in the seat, on M and H, and is the waiting per M
turn 0.8 s or more (the Architect's threshold for raising ticket 5031)? Measurement only, no product change.

### The method `[C, the code; 2026-11-22]`

- **The seat.** Variant key `human_seat` (runner, `perf/turnstudy/runner/clientarm.py` `play_human`): no
  `Game.SetAIAutoPlay`; after the load the runner checks that the active player is human, not an observer, its turn
  active and autoplay 0 (`HUMAN_SEAT_LUA`); then for each turn t = T .. T+N it ends the human's turn with the harness
  (`tools/harness/turns.py end`: idle units get no moves, an empty city queue takes wealth, a missing research is chosen,
  `CONTROL_FORCEENDTURN`; ticket 0522's way) and makes **no channel call during the AI phase**: the phase is over when
  the game's own autosave of turn t (`AutoSave_<t>`, written when the AI part of turn t is done, `memory-sampler.md`) has
  appeared and stopped growing; only then the channel confirms the human turn t+1 is active. The diplomacy answerer of
  the harness (`diplo.py arm`, as in every channel arm) closes AI leader screens. The comparison arm `f5030_observer`
  is the same run with autoplay's observer seat (0437's and 0563's way).
- **The layout (arm `S4f`).** The product as the installer's configuration lays it (lay.py's steps,
  `research/02-exe-internals/evidence/4967/lay.py`): `provision.py set-proxy` (the D28 layout, the instance's own 32-bit
  DLL as the fallback), `vp64helper.exe on` (the patch's config.ini form, `[Renderer] ServerRenderer = 1`), the composite
  with the shell build's shipped `shell_composite.ini` and its manifest; options 2+3+4 written as the release writes them
  (D197: `AiUpdateTickMs = 0`, `AiSkipVisualDrain = 1`, `AiDrainUnordered = 1`). DLL pin 1b87dd185 (x64-0.1b,
  `deps/dll.lock`; the shared pin build `C:/c5-64bit-work/keep/pins-0.1b`), server.exe and the measuring proxy built by
  runbook 1.2 from `scratch-5030-framesplit` 32fde3764, the composite by runbook 1.5. Hashes: x64 DLL B84AA3E9, server
  59FF4796, measuring proxy 3144592A, vp64helper 993E7373, composite 6931C56A, its ini EC89AAB4
  (`C:/c5-64bit-work/5030/layout/SHA256.txt`; `perf/turnstudy/runner/arms.json` `S4f`). NOT a player's build: the proxy
  carries the timers below (off without their switch), and this is not the 0.1b take.
- **Three clocks, one QPC.**
  - *The composite's per-frame probe* (existing, `shell_composite.ini` `lockstep_probe=1`, rows in
    `shell_composite.lsprobe.csv`; `VPSHELL_OUT_DIR` points it into the run folder): per Present of the game's swap
    chain, the previous Present's return, the first deferred-context clear or bind since then (render recording
    begins), UpdateViews and the Present's entry. A frame = one Present's return to the next; **update pass** = return
    to the first deferred-context clear or bind; **render** = from there to Present's entry (recording on the worker
    contexts and the main thread's submission, UI list included); **present** = inside Present: the vertical-blank wait
    (the frame cap; the runner's timing launch has no harness cap, `WaitForVSync` as found) and the driver's waits.
  - *The measuring proxy* (`proxy/src/proxy_framesplit.cpp` on the scratch branch; on only with
    `VP64_FRAMESPLIT_DIR`): at the first `ICvGame1::Update` it writes the EXE's import slots of `lua51_Win32.dll`
    `lua_pcall`, `lua_call` and `lua_cpcall` (data writes to the import table, no code byte; the method of 0518 and
    4811) with wrappers that time the outermost call per thread into 1 ms bins (main thread = every thread but the
    game-core one; the core thread apart); a Lua error unwinding past a `lua_call` wrapper is closed by the enclosing
    protected wrapper. And QPC at the top and the end of every `ICvGame1::Update` (`BeforeUpdate`/`AfterUpdate`).
  - *4657's frame trace* (`update_trace`): B/E per Update and the D/P/S/X player events, to classify each Update (an AI
    Update holds a D, P, S or X row) and its turn; each trace B row is matched to the proxy Update that contains it,
    after the two clocks are aligned by the median offset of the nearest begins (the DLL raises its begin event up to
    270 ms after the proxy's top on H, so a nearest-begin match left 1-2 % unmatched there; corrected 2026-11-22 23:10;
    both series below were analysed again with it).
- **Definitions** (`perf/evidence/5030/framesplit.py`): a *wait* runs from an AI Update's end to the next Update's
  begin when that next Update is an AI Update too (the human's own turn, and the gap from the turn-change Update into
  it, are not waits); each wait is cut by its overlap with the frames' update pass, render and present, and the
  main-thread Lua inside it is reported beside (it is part of the update pass or render). Per turn: AI Updates, time
  inside them, waits and their parts, frames in the waits, the AI phase's frames and median frame, main-thread Lua in
  the AI phase and over the whole turn (human turn included), core-thread Lua inside the Updates.
- **Instrument grade.** The trace adds one Lua call per frame and the proxy two QPC reads per outermost Lua call: these
  are instrument runs (grade D for the parts); the waits are reported with their spread over three runs per seat on the
  quiet machine (oldpc, the runner's quiet job).

### Smokes `[L, oldpc, 2026-11-22 21:20-21:51 EST, jobs 20261122-212045-9626 and -214527-470a, functional]`

The first human-seat run (9626) played the AI phase of turn 150 but waited out its 600 s turn timeout: the runner's check
that the autosave had stopped growing restarted at every scan (fixed in `play_human`). The second smoke (470a, human
only, M 150-153) found each turn's boundary from `AutoSave_<t>` 5.6-6.8 s after the end-turn call and the human turn
active 0.04-0.14 s after the autosave settled. The observer run of 9626 gave 3.05 s per M turn.

### Results on M `[L, oldpc, job 20261122-215511-f861, runs 2026-11-22 21:56-22:23 EST, 6 of 6 runs exit 0; analysed 22:30 by impl-5030; instrument runs (grade D for the parts), statistics over 3 runs per seat x 10 turns]`

Series `5030-M-framesplit`, arms `S4f:f5030_human` and `S4f:f5030_observer`, Williams order over 3 rounds, quiet and
alone (the runner's quiet job); M from `TS0438-M-T150`, window 151-160, statistics logging on, AI logging off, Quick
Combat and Quick Movement on (the save's), the window 1920 x 1080 on screen, a timing launch (no harness frame cap),
`WaitForVSync = 1` as found on oldpc's `tb1`. oldpc: i7-6700K, 16 GB, Windows 10.0.19045.6466, **NVIDIA GeForce RTX
2080, driver 32.0.16.1088** (the run records' `machine.gpus`; unchanged, D175); the EXE is `tb1`'s copy of oldpc's
Steam build 4390913. Free commit at the first start 3.1 GB. Every trace Update matched the proxy Update that contains
it (581-588 per human run, 315-316 per observer run; clock offset 0.8-2.6 ms). Turn clock (player 0's PlayerDoTurn):
human 6.49-6.53 s per M turn (the harness's end of the human turn, about 1.9 s, and the holds below included),
observer 3.02 s. `perf/evidence/5030/analysis-M-f861.{txt,json}` (`framesplit.py`).

Per M turn, mean over the 3 runs [95 % CI]:

| | human seat | observer seat (autoplay) |
|---|---|---|
| AI Updates; time inside them | 27; 1.57 s [1.47, 1.68] | 25; 2.19 s [2.12, 2.25] |
| **waiting between AI Updates, all** | **1.050 s [1.027, 1.072]** | 0.863 s [0.812, 0.915] |
| of it: the player waits (one between two AI players each; 24.4 / 24 a turn) | 0.475 s [0.458, 0.493] | 0.297 s [0.288, 0.305] |
| of it: the end-of-phase wait (before the turn-change Update) | 0.575 s [0.567, 0.582] | 0.567 s [0.524, 0.610] |
| frames in the player waits (Present returns crossed) | 31.0 (median 1 per wait) | 23.9 (median 1 per wait) |
| player waits: update pass / render / present / unframed | 0.256 / 0.007 / 0.212 / 0.000 s | 0.152 / 0.003 / 0.141 / 0.000 s |
| player waits: main-thread Lua (inside the update pass) | 0.095 s | 0.038 s |
| end-of-phase wait: frames; update pass; Lua in it; present | 4; 0.558 s; 0.133 s; 0.014 s | 4; 0.555 s; 0.160 s; 0.010 s |
| holds (an AI leader's screen until the harness's answerer closes it [I]) | 1.6 a turn, 3.89 s | none |
| AI phase without holds; frames in it | 2.41 s; 105.3 | 2.22 s; 79.2 |
| frame in the AI phase: median (mean) | 16.67 ms (22.1) | 21.4 ms (30.6) |
| median frame's parts: update pass / render / present / Lua | 2.82 / 0.79 / 13.03 / 0.07 ms | 9.56 / 0.74 / 10.84 / 0.65 ms |
| main-thread Lua in the AI phase (holds out) | 0.163 s | 0.509 s |
| main-thread Lua over the whole turn (the human's turn included) | 0.494 s | 0.662 s |
| core-thread Lua inside the AI Updates | 0.013 s | 0.017 s |

The spread between runs is small (the 95 % intervals are within 2-10 % of the means), so these per-turn figures are
the machine's, not one run's. What the parts say `[L for the times; I for the causes]`:

- **The frame between two AI players is mostly the frame cap.** A frame in the AI phase with a human seat is 16.67 ms
  (60 Hz): 2.8 ms of update pass, 0.8 ms of render (recording and submission are cheap: the server draws the world),
  and 13.0 ms inside Present waiting for the vertical blank. With option 3 the EXE asks for the next Update in the next
  frame's update pass, so a player wait is the rest of the frame in which the Update ended plus the next frame's update
  pass up to its request: median 1 frame, 8-10 ms. 24 of them make 0.30 s (observer) to 0.48 s (human) per M turn.
- **Why the human seat's player waits are longer**: a few waits of 100-240 ms a turn after a major civilisation's
  Update (the first after the human's own turn above all), spent in the update pass with little Lua (for example 2.6
  ms to UpdateViews, then 57 ms before render recording starts, 6 ms of Lua): the EXE working off what that player
  showed the human [I: not read]. The observer seat does not have them.
- **The end-of-phase wait is not a frame-pacing wait.** It is option 4's one long drain frame (0.43-1.03 s per turn,
  4 frames in it): 97 % of it in the update pass (queue B's backlog of the whole AI phase dispatched in one pass) with
  0.13-0.16 s of Lua; the same size in both seats. Making frames lighter does not shorten it: it is the work of showing
  the AI phase's moves.
- **The observer's tech panel** (0518): main-thread Lua in the AI phase 0.509 s with the observer against 0.163 s with
  a human seat; the observer's Updates take 0.61 s longer per turn (2.19 against 1.57 s) [I: the AI's raises waiting
  for that Lua, as 0518 measured (0.59 s); not measured here].
- **Holds**: with a human seat the AI phase stops about 1.6 times a turn for 2.4 s while an AI leader's screen is up
  (the harness's in-game answerer closes it; with a person in the seat the hold lasts as long as they read). They are
  not frames between Updates and are left out of every row above but their own.

### Results on H `[L, oldpc, job 20261122-222342-00f4, runs 2026-11-22 22:24-23:09 EST; 5 of 6 runs counted (the third human run ended at the load, below); analysed 23:15 by impl-5030; instrument runs (grade D for the parts)]`

Series `5030-H-framesplit`: the M series' set-up on H from `TS0438-H-T300`, window 301-306 (6 turns: the H AI turn is
long), 3 runs per seat in Williams order; same machine, driver, layout and options. Every trace Update matched (270-271
per observer run, 328-329 per human run; clock offset 7-16 ms). The third human run (r03-p1) **ended at the load**:
the composite's cut-mask tap took a fault during the tree grid's growth for a permanent failure, and with
`render_forest` 1 the server ended the 64-bit path (exit 5; the EXE ended by the proxy). It is a product bug, not
this ticket's instrument (the measuring proxy installs at the first Update, which never came): root cause and fix in
`tracker/issues/0690-cut-tap-fault-during-grid-growth-ends-huge-load.md` and `../06-tooling-and-workflows/known-crash-causes.md`
K28 (COMMON rule 30). The human seat's figures are therefore n = 2 (their intervals use t with 1 degree of freedom).
`perf/evidence/5030/analysis-H-00f4.{txt,json}`.

Per H turn, mean over the runs [95 % CI]:

| | human seat (n = 2) | observer seat (n = 3) |
|---|---|---|
| AI Updates; time inside them | 34; 18.67 s [18.64, 18.70] | 32; 20.07 s [19.86, 20.27] |
| **waiting between AI Updates, all** | **2.081 s [2.018, 2.144]** | 2.095 s [2.069, 2.121] |
| of it: the player waits (30.7 / 31 a turn) | 0.652 s [0.601, 0.703] | 0.314 s [0.282, 0.346] |
| of it: the end-of-phase wait | 1.429 s [1.417, 1.442] | 1.781 s [1.741, 1.821] |
| **frames in the player waits per H turn** | **37.0** (median 1 per wait) | **30.7** (median 1 per wait) |
| player waits: update pass / render / present | 0.392 / 0.016 / 0.244 s | 0.102 / 0.009 / 0.203 s |
| player waits: main-thread Lua | 0.132 s | 0.018 s |
| end-of-phase wait: frames; update pass; Lua; present | 4; 1.418 s; 0.378 s; 0.009 s | 4; 1.771 s; 0.535 s; 0.007 s |
| holds (leader screens [I]) | 2.3 a turn, 8.2 s | none |
| AI phase without holds; frames in it | 19.1 s; 1,034 | 18.4 s; 937 |
| frame in the AI phase: median (mean) | 16.67 ms (18.2) | 16.67 ms (19.8) |
| median frame's parts: update pass / render / present / Lua | 3.40 / 1.60 / 11.63 / 0.04 ms | 3.71 / 1.40 / 11.41 / 0.04 ms |
| main-thread Lua in the AI phase (holds out) | 0.70 s (0.62 and 0.78) | 1.34 s [1.27, 1.41] |
| core-thread Lua inside the AI Updates | 0.064 s | 0.070 s |

- **The frames per H turn**: the note's inference was "about one frame per player, 46 or more a turn". Measured: 31
  waits a turn between AI players on this save (34 AI Updates with the human seat, 32 with the observer), each a median
  of one frame; 37 (human) and 31 (observer) frame boundaries in them, plus 4 in the end-of-phase wait. The AI phase
  as a whole holds about 940-1,030 frames, because the EXE keeps drawing at 60 Hz while each AI Update runs (18.7-20.1 s
  of Updates a turn).
- On H too the player waits are short (0.31-0.65 s a turn, 2-3 % of the AI phase) and mostly the vertical-blank wait
  and the update pass; the end-of-phase drain (1.4-1.8 s, 7-10 % of the AI phase) is queue B's dispatch with 0.4-0.5 s
  of Lua; inside the Updates is 18.7-20.1 s.

### Beside the observer-seat figures of the Architect's note, section 2

| Figure | Note's section 2 (laptop, 0437 (b), observer seat, thin layout, batch 2) | oldpc, observer seat (S4f) | **oldpc, human seat (S4f)** |
|---|---|---|---|
| frames per M turn in the waits | 24 x 1 (median 1 per step) | 23.9 player + 4 end-of-phase | **31.0 player + 4 end-of-phase** |
| the median frame | 35.5 ms | 21.4 ms | **16.67 ms** (60 Hz) |
| waiting per M turn | 1.23 s | 0.86 s (0.30 player + 0.57 end) | **1.05 s (0.48 player + 0.57 end)** |
| UI Lua per M turn (main thread) | 0.96 s of Lua holds (0518, workstation) | 0.51 s in the AI phase, 0.66 s a turn | **0.16 s in the AI phase, 0.49 s a turn** |
| frames per H turn in the waits | "46 or more" `[I]` | 30.7 + 4 | **37.0 + 4** |

Not like with like in every column: the laptop's figure is a thin layout (no composite, the EXE draws the world) on a
Quadro P3000 at 35 ms frames; S4f is the product layout (the server draws the world, the composite joins it) on an RTX
2080 at the 60 Hz vertical blank. The laptop was not used (MC-B's machine choice); the same pair there is open.

### The answer to the threshold (D229, ticket 5031)

**Yes, with a human seat the waiting between AI Updates is 0.8 s or more per M turn on oldpc: 1.050 s [1.027, 1.072]
(3 runs x 10 turns, quiet).** But it is two different things, and only one of them is what T1's light frame addresses:

1. **The player waits: 0.475 s [0.458, 0.493] per M turn** (0.65 s per H turn): one frame each, made of the
   vertical-blank wait inside Present (0.21 s), the update pass up to the next request (0.26 s, of it 0.10 s Lua) and
   almost no render (0.007 s). Taken alone this part is **under the 0.8 s threshold**. A frame that skips the world and
   UI render would save little of it here (render is 0.8 ms of a 16.7 ms frame); what bounds it is presenting at the
   vertical blank. A shorter frame between AI players would need the present not to wait for the blank (or no present)
   while an AI Update has just ended `[I: not tried; the means would be the composite's or the proxy's own Present
   path, no EXE code write]`, worth at most about 0.2 s per M turn on this machine `[I]`.
2. **The end-of-phase wait: 0.575 s per M turn** (1.4-1.8 s per H turn): option 4's single drain frame, the EXE
   dispatching the whole AI phase's queue-B backlog (unit moves and their UI, 0.13 s of Lua on M). It is the same with
   either seat and is not a frame-pacing cost; T1's light frame would not shorten it.

So the figure the Architect set the threshold on (1.23 s on the laptop) is confirmed in size with a human seat (1.05 s
on oldpc), but its frame-bound part, which ticket 5031 would attack, is about 0.48 s per M turn here. Whether that
raises 5031 is the MC's call with this split.

### Not verified (5030)

- The laptop (the note's machine, 35 ms frames): not run (MC-B's machine choice); with longer frames the player waits
  there would be longer `[I]`.
- A person in the seat: the human seat here is the harness's (end of turn by `CONTROL_FORCEENDTURN`, idle units, leader
  screens closed at once). A player who reads a leader screen, scrolls or selects during the AI phase changes the
  update pass and the holds.
- The holds' cause is inferred (the EXE's update hold behind an AI leader's screen, `../02-exe-internals/game-core-thread.md`
  0.3-0.4); the harness's diplomacy log was not read for each hold.
- The update pass and render split rests on the composite's "first deferred-context clear or bind" as the start of
  render recording `[I]`; UpdateViews (also in the rows) sits 0.2-2.6 ms into the update pass.
- Instrument cost: one Lua call per frame (the trace) and two QPC reads per outermost Lua call (the proxy); not
  measured; the turn times here are not timing results (grade D for the parts; the spreads are given).
- H human seat n = 2 (one run lost to the cut-tap fault above); window 6 turns on H.
- Build 26577625: not run (oldpc's `tb1` carries 4390913).

## Options 2 and 4 follow Quick Combat and Quick Movement (ticket 0718, D246; 2026-11-23)

The owner's beta 3 report (2026-11-23 20:3x): with `AiSkipVisualDrain` on and animations on, the barbarians' and the
major civs' fights and moves run at random and at the same time. These options only make sense with Quick Combat and
Quick Movement on. His rule (D246, 2026-11-23 20:4x): `AiSkipVisualDrain` and `AiDrainUnordered`, when set on in the
options file, take effect only while BOTH quick modes are on. The state is read at each AI turn's start, never mid-drain,
so toggling the quick modes in game switches them. With either off (animations on) the stock drain and order run.
Implementer ac85451d (account B), branch `task-0718-ai-drain-quick-modes`.

### Why the options overlap the animations `[C, the code and game-core-thread.md 6.2-6.3]`

Option 2 writes `GameViewState+0x8CC` after each AI `Update`, so the frame's gate (skip 2) never sees a player change.
The AI-turn byte then stays 1 for the whole AI phase, and queue B is not dispatched while it is 1 (0571's poller, 6.3).
Every AI player's records therefore wait until the phase's end and are dispatched there in one stretch. With the quick
modes on these are teleports and instant results. With animations on they are walks and fight scenes of many players,
started together as the queue's move rule lets one record per unit through per frame (4672). That is what the owner
saw. Option 4 (the end-of-phase clear) removes even the per-unit order. 4675 had already found this for option 4 with
Quick Movement off ("Quick Movement off: the guard stays"), and option 4 kept a Quick Combat and Quick Movement guard.
Option 2 had no such guard until 0718.

### What the proxy does since 0718 `[C, proxy/src/proxy_pacing.cpp]`

- **One read per decision.** After each AI `Update` that reaches option 2's decision, the proxy asks the DLL's own
  `CvPreGame::quickCombat()` and `quickMovement()` once, through the same glue option 4 used (`GlueQuick`,
  `ICvPreGame1`). On the product path it crosses to the server's DLL. A decision means the AI-turn byte was 1 at the
  `Update`'s start, the hold 0x2DE044C is 0, the scan ran, and the scan found either the next AI player or none (the
  end of the phase). These are game-wide values, the same ones the DLL reads at each attack and move to decide between
  animated and instant (research 02 `community-nuke-explosion-skipped.md` 1). The in-game options menu changes them
  with `Network.SendGameOptions`.
- **When: never mid-drain.** The read runs on the game-core thread after the `Update` has returned, while the `Update`
  is still pending. The main thread does not pump queue B then (6.3). For an AI gap this is the next AI player's turn
  start; at the end of the phase it is the end-of-phase drain's start. A drain already under way is never changed:
  option 4's byte is set back at the next `Update`'s top as before, whatever the settings are by then.
- **Both on:** options 2 and 4 behave as built (value 1, value 2 with its gate, option 4 at the end of the phase and at
  gated gaps).
- **Either off, or not answered:** option 2 does not write +0x8CC, so the frame's own gate sees the player change and
  drains queue B in the EXE's order before the next player, as stock does. Value 2 does not walk queue B. Option 4
  clears nothing, neither at the end of the phase (its own counters, as before) nor at a gap.
- **Switching in game.** The read is not latched per phase: each AI player's start reads again. A switch to "off"
  during the AI phase takes effect at the next AI player. The records already deferred are then drained by the frame's
  gate in the EXE's order. A switch to "on" takes effect at the next AI player too.
- **Lines and counters.** Each change of the read state is one line in `vpproxy.log`. The first read of a load counts
  as a change:
  `pacing: D246: Quick Combat and Quick Movement on: [Performance] AiSkipVisualDrain and AiDrainUnordered act from this AI player on (ticket 0718)`,
  `pacing: D246: animations on (Quick Combat off, Quick Movement on): ... rest; the frame waits for queue B in the EXE's order from this AI player on`,
  or `... could not be asked of the DLL ...`. Option 2's counters line gains `animations on N (Quick Combat off N, Quick
  Movement off N, settings unknown N)` and `quick reads N (changes N, N us, max N us)`. The time of the reads is the
  cost D246 adds per AI `Update`.
- **TEST switch.** `VP64_PACING_TEST_DRAIN_ANY_MOVEMENT=1` (option 4's diagnostic switch, MC #4203 (a)) now waives the
  Quick Movement half of the rule for both options. Quick Combat stays a condition.
- Option 3 (`AiUpdateTickMs`) is not touched. It changes only when the next `Update` is asked for, not what is shown.

### Tests `[L, offline]`

`proxy/tools/rt_tests/pacing_unit.cpp`, new section `TestQuickModes` (its header lists the cases), plus the earlier
sections' expectations where 0718 changes them. A gap's write now asks the settings once. Value 2's gated-gap refusals
by Quick Combat, Quick Movement or unknown settings became option 2's "animations on" (no walk, no gate count).
2026-11-23 23:0x, workstation, after merging main 0f49cb9a7: `python -I -X utf8 proxy/tools/test_proxy_rt.py --build <any> --scratch
<dir> --only-offline pacing` (the proxy's toolchain, both EXE builds' layouts) **696/696 checks passed** (9 s). The same unit
under MSVC 2022 x86 `/fsanitize=address`, with the canary caught first: `python -I -X utf8 server/evidence/0718/asan_pacing_0718.py
--scratch <dir>` **696/696, no AddressSanitizer report**. The older runner `perf/evidence/0437/4675/run_pacing_unit.py` passes no include flags for
the build table that 4963 added. It was not run here, so whether it still compiles is unknown `[I]`. The 0718 runner
adds the flags.

### Live check on the product path `[L, 2026-11-23 23:41-23:55 EST, workstation (shared), c5h-new-a (C:, Goldberg, EXE build 26577625, mods548 CP+VP+EUI 5.4.8), window hidden by the harness, layout from this branch at 6d637b3e5 built on the 0.1b pins (proxy 4f5190ca..., server.exe 64ddaf7b..., x64 DLL b84aa3e9..., composite 2aeec94f...), laid by lay.py with the helper on; save C5H-0718-KIT_0009 = a copy of the 4980 kit (Tiny, turn 9, a human seat); AiSkipVisualDrain = 1, AiDrainUnordered = 1, AiUpdateTickMs = 0; one run: functional, the times are indications (D106)]`

Driver `server/evidence/0718/smoke_0718.sh run` (with `arm_0718.py`). Each phase sets the quick modes with the in-game
options menu's call (`Network.SendGameOptions`) and reads them back. It then ends 4 turns (`turns.py end` + `wait`)
and waits 65 s in the human turn, so that the proxy writes its 60-s counters lines. Lines:
`F:/c5-64bit-work/0718/out2/run/pacing-lines.txt`; turn times `turns.txt`. The save loads with PreGame Quick Combat
and Quick Movement both true. `Game.IsOption(GAMEOPTION_QUICK_MOVEMENT)` read false throughout, even with PreGame's
Quick Movement true (read back in all four phases) `[L]`. The DLL's `quickMovement()` is PreGame's value, so the
driver compares PreGame only. The first launch stopped at that check and was stopped and restarted.

| Phase (turns) | PreGame QC / QM | option 2 writes | animations on (QC off / QM off) | option 4 cleared (not: QC off / QM off) | D246 line | s per turn (end to wait) |
|---|---|---|---|---|---|---|
| on (9-12) | true / true | 0 -> 28 | 0 | 0 -> 4 | "on" at 104.2 s (the first read) | 8.73, 30.64, 21.95, 17.18 |
| off (13-16) | false / false | 28 (none) | +28 (28 / 0) | 4 (none; +4 / 0) | "animations on (Quick Combat off, Quick Movement off)" at 274.6 s | 25.20, 23.53, 24.26, 23.72 |
| qm (17-20) | true / false | 28 (none) | +29 (0 / 29) | 4 (none; 0 / +4) | none (still resting) | 23.83, 22.30, 24.11, 20.35 |
| on2 (21-24) | true / true | 28 -> 60 | +0 | 4 -> 8 | "on" at 617.9 s | 16.79, 15.71, 16.27, 15.25 |

- **The rule holds in play.** With either quick mode off, option 2 wrote nothing at any AI gap: 57 gaps over 8 turns.
  So the frame's own gate waited for queue B after each AI player, as stock does, and no two AI players' animations
  could be dispatched together. Option 4 cleared nothing at the 8 ends of phase. Switched back on, both acted again
  from the next AI player.
- Each switch in game took effect within the first turn after it. The log's clock is the proxy's, so this is placed
  by aligning the first D246 line with the first turn of phase "on". With that alignment the "off" line falls about
  11 s into the off phase's first turn, and the "on" line about 11 s into on2's first turn `[I]`.
- **The D246 line marks acting and resting, not each setting.** Going from off/off to on/off logged no new line,
  because the options rested in both states. The counters name which setting was off.
- **What D246 costs `[L]`.** 133 reads took 2,039 us in all: 15.3 us on average, at most 39 us. That is one read per AI
  `Update` that reaches the decision, about 8 per Tiny turn here, so about 0.12 ms per turn. On an M turn of 25 AI
  `Update`s it would be about 0.4 ms against 4.6 s (0563) `[I]`. So the gain of options 2+3+4 with the quick modes on
  is unchanged within any measurement's resolution. Why: the code path with both on is the one built before. Only the
  read is new, and option 4 already made it at the end of the phase.
- **The base launch** (`smoke_0718.sh base`, 23:55-00:04): the same save, both options 0 (`AiUpdateTickMs` 0), both
  quick modes on, turns 9-12 took 6.52, 21.07, 24.47 and 26.44 s, 78.5 s in all. Phase "on" took 78.5 s for the same
  turns. On a Tiny map at turn 9, one run per arm, these end-to-wait times do not resolve the options' gain: the AI
  phase is a small part of them, and the human turn's popups and the harness's waits make up the rest. No timing
  claim is made here. The gain with the quick modes on rests on 0563's laptop series (options 2+3+4 4.64 s against
  14.33 s per M turn) and on the cost above.
- No LOUD line, no FAILED check, no report and no box in either launch. The game was stopped by the harness's stop,
  then the front end was restored.

### Not verified (0718)

- Hot seat and multiplayer: the settings are game-wide in the DLL (`CvPreGame` statics). Which player's menu changes
  them in hot seat was not looked at.
- Ticket 0716 (Quick Combat and Quick Movement not following the player after a seat change) is a separate bug. If the
  DLL's values go stale after a seat change, these options follow the stale values too.

## Small EXE-side items of the third speedup list, measured (ticket 5044, the Architect's T14; 2026-11-24)

impl-5044 (account B), 2026-11-24 21:14 onwards, branch `task-5044-small-exe-items-measure`. The question (the
Architect's note `research/07-architecture-decisions/architect-2026-11-21-full-exe-optimizations.md` section 3.2, rows
EX9, EX11, EX13, FR3 and FR7, and draft T14): one figure each on the real EXE and the product path; each item closed with
its number or raised as its own ticket draft above 0.3 % of an H or M turn (for FR3 and FR7: a frame-time or first-paint
figure). Measurement only: no product change, nothing built into the product.

### The method `[C, the code; 2026-11-24]`

- **The layout (arm `S4x5044`, `perf/evidence/5044/`).** The product as the installer's configuration lays it, in the
  form of 5030's `S4f` and 5032's `S4s5032`: `provision.py set-proxy`, `vp64helper.exe on` (`[Renderer] ServerRenderer =
  1`), the composite with the shell build's shipped `shell_composite.ini`; DLL pin 1b87dd185 (the shared pin build
  `C:/c5-64bit-work/keep/pins-0.1b`); server.exe, proxy and helper built by runbook 1.2 from this branch (main 9bda43beb +
  evidence only, `build.sh`), the composite by runbook 1.5. Hashes in `layout-SHA256.txt` (server 04F56271, proxy
  AE17794A, helper 51CF3D3F, composite DFB258AB, x64 DLL B84AA3E9). The arm's copy of the composite's ini adds only
  `lockstep_probe=1` (the composite's per-frame probe, `shell_composite.lsprobe.csv`). NOT a player's build. The arm and
  its variant live in `perf/evidence/5044/arms5044.json` and `variants5044.json`; `ts5044.py` wraps the runner's
  `tslib.load_arms` and `load_scenario` so the shared `arms.json` and `scenarios/M.json` stay as they are.
- **The EXE: build 4390913.** oldpc's `tb1` carries only its own Steam EXE, build 4390913 (5032's and 5035's deviation,
  MC #13221). The acceptance names 26577625; nothing here ran on it.
- **Job 1, the turn study (EX11, EX13; and EX9's per-class figure).** Variant `x5044_human` on M (`TS0438-M-T150`,
  window 151-160): 5030's human seat (no autoplay; each human turn ended by the harness's `end_turn`), options 2+3+4 as
  the release writes them (D197), Quick Combat and Quick Movement on (the save's), 4657's frame trace, the turn budget
  (`VP64_TURN_BUDGET=1`), and 4661's pacing poller with 4672's queue-B census (`TURNSTUDY_QB_CENSUS=1` in the job's
  environment). An instrument: counts and parts, not a timed series (grade D). Analysis `ex_series.py` on 4672's own
  readers (`perf/turnstudy/pacing/qbanalyze.py`).
- **Job 2, the cover job (FR3, FR7, EX9).** `perf/evidence/5044/cover_job.py`, built on `tools/testbed/jobs/product_path.py`
  as 5032's `save_job.py` is (prep, safe stop and restore by hash are product_path's), on TB-HUGE_0350 with options
  2+3+4 added to `VP64Options.ini` and `lockstep_probe=1`: in the human's turn, 3 rounds of the map alone (U, 25 s) and
  the tech tree open over the whole screen (C, 25 s), then one AI leader's screen opened twice (L1: the first time that
  scene is shown in the process; L2: again), each held 20 s by checklist.py's close timer; then 4 AI turns. Per phase:
  the composite's `pacing 1s:` and `pacing freeze:` lines, the server's `render.log` `host 1s:` and `pass 1s:` lines, the
  EXE's and server.exe's CPU seconds (psutil) and the probe's frame intervals. Analysis `ex_cover.py`.

### Job 1: the M census with a human seat `[L, oldpc, job 20261124-212731-5662, runs 2026-11-24 21:29-21:38 EST, 2 of 2 runs exit 0, provision verify rc 0; quiet and alone (foreign 0.017 cores); analysed 21:40 by impl-5044; an instrument (grade D), counts and dispatch times]`

Series `5044-M-census`, arm `S4x5044:x5044_human`, 2 runs x 10 turns (151-160). Both runs post the same game (the
same record counts per turn), so the counts are the game's and the times have the spread of 2 x 10 turns. EXE build
4390913 (`vpproxy.log`: "known and supported"). Turn clock (player 0's PlayerDoTurn, the harness's end of the human turn
and the leader-screen holds included): **6.66 and 6.60 s per M turn**; 0.3 % of it is 20 ms. Census: 43,691 records per
run, no event id missing, its own time 3.0 % of the poll's. Each run's own NNLS fit of the pumps' dispatch-loop time on
their records by type (472 pumps, R2 0.919 and 0.940, intercept 0.24-0.29 ms per pump). Outputs
`perf/evidence/5044/series-5662.{txt,json}` (`ex_series.py`), `pumps_with.py` for the 0x66 check below.

Per M turn, mean over 20 turns [95 % half-width, normal]: 4,034 [±207] records, 47 pumps, dispatch loop 0.362 s
[±0.064], move 0.008 s.

**EX11, superseded records delivered anyway: 0.011 s per M turn [±0.002] (0.17 % of the turn). Closed.**

- Counted the widest window the EXE could use: inside each pump, a teleport (0x4F), dim flag (0x53 without flags 5),
  visibility (0x51) or fortify/work/activate (0x49) record is superseded when a later record of the same unit and class
  is dispatched in the same pump. **1,100 [±131] of the 4,034 records per turn** (27 %): dim flags 691, teleports 260,
  visibility 79, 0x49 71 (an upper bound: `GameplayUnitActivate` is an event in the same type).
- Their dispatch time, three ways: **0.011 s** [±0.002] by the run's own per-record costs (teleport 0.032-0.036 ms,
  visibility 0.009-0.044 ms, dim flag and 0x49 0 ms); 0.033 s [±0.004] as each pump's measured loop time times its
  superseded share (a loose bound: the mean record of a pump includes the expensive types); 0.083 s [±0.015] with
  4672's stock-pacing bound of 0.31 ms per teleport (a bound from a fit of R2 0.73).
- So an EXE that dropped them at the drain would save about 0.011 s of the 6.6 s turn, at most 0.033 s (0.5 %). The
  first review's model (4677, rows A11 and A12: at most about 0.1 s with option 4) is confirmed in size and narrowed.
  With option 4 the backlog is dispatched in few pumps (15-20 per turn from turn 154 on) and the handlers of these
  types cost almost nothing. Below the value floor: **closed with this number**, no ticket.

**EX13, turn-start records before the first input: 0.123 s of dispatch [±0.018] in the human's first 0.31 s
[±0.05] of each M turn (1.9 % of the turn). Above the floor: raised as a ticket draft (below).**

- The turn-change `Update` (the period whose frame player is NO_PLAYER; 0.070 s [±0.005] inside it) posts **101
  [±11] records** per turn (0x53 dim flags about 32, 0x6F city info 4-30, 0x79, 0x80 tech acquired 16 in some turns,
  0x74, 0x69, 0x4F). They are dispatched in **4.5 pumps**, the first 0.040 s after that `Update` ends, the last done
  **0.313 s [±0.047]** after it (0.18-0.61 s); the pumps' dispatch loop is **0.123 s [±0.018]**, the move 0. These pumps
  run inside the frames' update pass on the main thread, so the human's first 0.3 s of the turn are 3-4 long frames
  (median pump loop 47-60 ms) before the first input is handled in a short one `[I: the frames were not timed here]`.
- **One record type carries most of it: 0x66** (poster not identified; it comes with 0x62, 0x65, 0x6D and 0x1E, the
  per-player records that 4672 saw 25 times a turn at stock pacing). Both runs' fits give it **60.5-62.5 ms per record**;
  every one of the 36 pumps that holds a 0x66 takes 47-154 ms (median 56 ms), the 537 small pumps without it a median of
  0 ms `[L, pumps_with.py]`. The 0x62/0x65/0x66/0x6D group comes 3.7 times per M turn (turn change 1, the human's turn
  1.5, majors 0.7, barbarians 0.5): **about 0.23 s per M turn in all**, 0.06 s of it at the human's turn start. In stock
  pacing 4672 measured the same group's 0x6D at about 81 ms each (turn-time-study.md "What the drain is made of"); the
  fit cannot split a group whose members always come together, so "0x66" stands for the group `[I]`.
- What a fix could save is the wait before the first input (about 0.3 s per turn), and whatever of the group's 60 ms
  handler is repeated work; the records themselves are the turn's own news to the UI. Draft below (T14-EX13).

### Job 2: the cover job on the huge save `[L, oldpc, job 20261124-214108-2c96, 2026-11-24 21:42-21:51 EST, quiet and alone, foreign 0.144 cores (over the 0.10 limit: svchost 30 s and Defender 24 s in the job's 9 min; the job's own game is counted outside its tree); all phases and 4 AI turns played, restored by hash (7 files equal, 8 expected absent); analysed 21:56 by impl-5044]`

TB-HUGE_0350 (turn 350, the human's turn), product path as above, window 1920 x 1080 on oldpc's screen (RTX 2080,
`WaitForVSync` as found), build 4390913. The job's exit is 5 only for LOUD lines: 200 of ticket 0576's stale-Unit-instance
guard in server.log (they appear in every product-path huge run: `../05-graphics-composition/extras.md`,
`unit-combat-server.md`) and 2 lockstep `held_skip` lines at the load; the end shot is not flat (Visual Checks
`vc-20261124-215034-oldpc-349a`, `-215031-oldpc-325d`, DIAGNOSTIC). One job, one machine: the phases are compared with
each other only. Outputs `perf/evidence/5044/cover-2c96.{txt,json}` (`ex_cover.py`), the job's `cover_job.json` and the
server's call tables (copied out of the instance's run folder by `grab_calls.py`, job `20261124-215233-9132`) in
`perf/evidence/5044/cover-2c96/`.

Per phase (seconds 1.5 s after each open or close to the phase's end; U = the map alone, C = the tech tree open over it,
L = an AI leader's screen, 3 rounds of U and C):

| | map alone (U1-U4) | tech tree over the map (C1-C3) | leader screen (L1, L2) |
|---|---|---|---|
| presents per second | 29.9-30.0 | 29.0 | 30.5 |
| frame (probe, Present to Present): median / p95 | 33.70-33.76 / 35.2-35.5 ms | 34.65-34.66 / 36.3-36.7 ms | 33.37-33.40 / 34.4-34.6 ms |
| server: frames drawn and published per second | 29.4-29.5 | 28.7 | 29.8-29.9 |
| server: render thread CPU per second (`host 1s:` rt_cpu_ms) | 87.0-91.7 ms | 82.9-86.3 ms | 69.1-71.5 ms |
| server: render pass busy, mean per pass (`pass 1s:`, 58-61 passes/s) | 1.390-1.400 ms | 1.385-1.401 ms | 1.214-1.227 ms |
| server.exe CPU (psutil) | 0.166-0.176 cores | 0.166-0.181 cores | 0.139-0.144 cores |
| the EXE's CPU (psutil) | 0.338-0.361 cores | 0.364-0.380 cores | 0.261-0.270 cores |
| the composite's lockstep waits for the server's frame | 8.1-9.1 per s, mean 154-185 us | 0-0.04 per s | 1.6 per s, mean 44-47 us |

**FR3, the world drawn while something covers it: the server keeps drawing every frame under the tech tree (28.7 frames
a second, 1.39 ms per render pass, 0.083-0.086 cores of its render thread) and under a leader screen (29.8 a second,
0.069-0.072 cores); the frame time it costs is 0 ms. Closed.** While the world is covered the composite does not wait for
the server at all (0-0.04 waits a second against 8-9 with the map alone), and the frame is the EXE's own: 34.7 ms under
the tech tree (the panel costs the EXE about 1 ms more per frame than the map alone), 33.4 ms under the leader screen, on
a 30 Hz cadence (two vertical blanks at 60 Hz `[I]`). Skipping the server's world while covered would free about 0.08 cores of
CPU and the server's GPU time per frame (not measured), and no frame time, on this machine. Below any frame-time floor:
**closed with this number**, no ticket. `[I]`: on a GPU-bound machine the server's GPU work could slow the EXE's frames;
not tried.

**FR7, texture work at first paint and in leader scenes: about 1.0 s of extra frame time once per load when the map
first shows (one 308 ms frame, two of 146-153 ms, about 20 of 55-73 ms, then the steady 33.7 ms after 2.5 s), and about
0.2 s at the first open of each leader scene (frames of 193 and 90 ms; 53 and 54 ms when the same scene opens again);
the tech tree shows no first-open cost. Closed.**

- First paint (the load screen closed at about 21:44:25 by the harness's dismiss): from 21:44:25.5, 141 frames in 6 s
  and **1.03 s of frame time above the steady 33.7 ms** (the probe's frame list in `cover-2c96.json`, `after_dismiss`);
  the longest frame 307.7 ms at 21:44:25.725. During the load screen before it the frames already ran at 13-15 a second
  (55-100 ms each, the world being prepared behind the screen) `[L]`. What the long frames do (texture decode, the
  first draw of each UI panel, the server's first bake) is not split here `[I]`. Once per load of the huge map (165 s from
  the launch to in game on oldpc in this job): under 1 % of the load, and never per turn.
- Leader scene of player 2 (the same scene twice): the request to the first frame shown 0.30 s the first time, 0.10 s the
  second (the EXE's `os.clock()`, the close timer's record); the long frames after the request 193.2 + 90.5 ms the first
  time, 53.3 + 53.8 ms the second: **about 0.18 s more once per leader and process**. A game meets at most 21 (43)
  leaders.
- Tech tree, three opens: 0.33-0.36 s from the request to the first poll that saw it shown (the poll's own 0.3 s per Lua
  call included) each time, and no frame over 51 ms in the 5 s around any of the opens: no first-open cost.
- Both figures are one-off hitches, not per-turn costs; they are below the value floor of a turn: **closed with these
  numbers**. A frame-pacing epic that collects load and first-open hitches can take them up (the first paint's 308 ms
  frame is the largest).

**EX9, local sends that wait for the next frame: about 0.5 pledges per H turn and 0.4 per M turn, each answered within
one frame; at most 17 ms per H turn (0.08 %) and 7 ms per M turn (0.1 %). Closed.** `[L, the server's proxy-calls.tsv of
the cover job: 4 AI turns of TB-HUGE_0350, 350-353]`

- The DLL's sends in the 4 AI turns (and the load): `sendMinorPledgeProtection` 2 (0.11 ms), `sendPlayerOption` 12 (the
  load), `sendTurnComplete` 4, `sendPlayerInitialAIProcessed` 5, `SendAICivsProcessed` 5; the EXE answered each pledge
  with one `ResponsePledgeMinorProtection` (2 calls, 30.9 ms of the DLL's own work in them, the pledge's game logic, which
  any route pays). In the M series the census and the turn budget saw no per-method split; the whole-game counts of
  `../03-dll-engine-boundary/server-host.md` (24-77 pledges a game, Standard 200 turns: 77) give 0.39 a turn.
- The wait: the EXE answers a local send at its next frame's message drain. With options 2+3+4 a frame already lies
  between two AI Updates (5030: one frame per player wait), so a pledge's answer comes in a frame the turn has anyway;
  the bound charges one whole frame per send (33.7 ms on the huge map, 16.7 ms on M): 0.5 x 33.7 = 17 ms per H turn,
  0.39 x 16.7 = 6.5 ms per M turn. The per-turn protocol sends (`sendTurnComplete`, `SendAICivsProcessed`,
  `sendPlayerInitialAIProcessed`, one each a turn) are answered in the turn-change frames 5030 measured as part of the
  end-of-phase wait `[I: not split]`.
- The turn budget of job 1 bounds all of them on M: every call the DLL made into `ICvEngineUtility*` (sends included)
  took 0.008-0.015 s per M turn of round trips (`budget-table.csv` `into_exe.engine_s`, turns 151-160 of both runs)
  `[L]`.

### The five items, and the draft raised `[2026-11-24]`

| Item | Figure on the product path (oldpc, build 4390913, options 2+3+4, Quick Combat and Movement on) | Verdict |
|---|---|---|
| EX9 | 0.39 pledges per M turn and 0.5 per H turn, each answered within one frame: at most 6.5 ms per M turn (0.1 %), 17 ms per H turn (0.08 %) | closed |
| EX11 | 1,100 superseded records per M turn (27 % of 4,034); their dispatch 0.011 s [±0.002] per M turn (0.17 % of 6.6 s), bound 0.033 s | closed |
| EX13 | 101 records of the turn-change Update dispatched in the human's first 0.31 s of each M turn: 0.123 s [±0.018] of dispatch (1.9 %), of it about 0.06 s one per-player record group (0x62/0x65/0x66/0x6D, about 60 ms each, 3.7 a turn: 0.23 s per M turn in all) | **raised: draft T14-EX13** |
| FR3 | covered by the tech tree or a leader screen, the server still draws 28.7-29.9 frames a second (0.07-0.09 cores of its render thread); frame time it costs: 0 ms (the composite does not wait while covered) | closed |
| FR7 | first paint: about 1.0 s of extra frame time once per load (one 308 ms frame); first open of a leader scene: about 0.18 s once per leader; tech tree: none | closed |

**Draft T14-EX13 for the MC (priority 2, epic 0647): "The per-player queue-B record group (0x62, 0x65, 0x66, 0x6D)
and the human's turn start."** Scope and first step: name the poster and the handler of queue-B types 0x62, 0x65, 0x66
and 0x6D on the build table's builds (static reading, as 4672's `perf/turnstudy/pacing/qbtypes.tsv`; behaviour only,
D83) and split the group's about 60 ms per occurrence (UI Lua, a panel rebuild, the EXE's own work) with a human seat on
M and H; count how often one player's turn posts it. Then decide: (a) work the handler repeats with unchanged input
(an exact skip), (b) for the human's turn start, the first input handled before the turn-start records (the list's
EX13; it changes when the UI shows the turn's news, so it is an Extras candidate at most). Done when: poster and handler
named, the 60 ms split, a go or no-go with the reachable figure (bound: 0.23 s per M turn for the whole group; 0.06 s
and the 0.3 s wait before the first input at the human's turn start). Option: open (an exact skip in a handler would be
a code write behind `FullExeOptimizations`; a UI Lua cause would not).

### Not verified (5044)

- Build 26577625: nothing ran on it (oldpc has 4390913 only); the queue-B census reads build 4390913's addresses.
- M only for the census (EX11, EX13); H not run with it. The H figures of EX9 come from 4 AI turns of the huge save
  (2 pledges): a small count.
- The EX13 frames: the census gives the pumps' dispatch time on the poller's clock; the composite's frames of those
  pumps were not matched to them (the trace and the probe are on other clocks), so "3-4 long frames" is inferred.
- The 0x66 group's cost is a fit over pumps in which its members always come together; which member costs what is not
  known. 4672's 0x6D at 81 ms (stock pacing) and this 60 ms are the same group `[I]`.
- FR3: the server's GPU time per frame was not measured (no GPU timer in these logs); the frame-time verdict holds for
  oldpc's RTX 2080 at 30 Hz.
- FR7: what the long frames at first paint and at the first leader open do was not split (texture decode, panel builds,
  the server's first bake); the list's "off-view effects" part was not measured.
- The harness's human seat (no person; leader screens closed by the answerer) as in 5030.
