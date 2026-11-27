# Stale DLL instances in Lua: the server's guard (ticket 0576, D185)

impl-0576, 2026-11-19. Server code: `server/src/remote/srv_lua_stale.{h,cpp}` (the guard), wired into
`server/src/remote/srv_remote_lua.cpp` (`kRt_LuaCall`, `HookGet`, `RebuildTypeNames`, `LuaReport`). Tests:
`server/tests/remote/stale_instance/`. Live driver and evidence: `server/evidence/0576/`.
DLL: the batch-4 pin x64 `c29442e8c` (`2B56A1CD...`), unchanged; the guard is in the server only.

## 1. The crash this fixes

0573's laptop crash e711 (`research/08-performance/hidden-embarks.md` 7.5 on 0573's branch, impl-0573's `[C]`: the DLL's
minidump read with cdb, 2026-11-19): server.exe ended with 0xC0000005 in `CvUnit::getX` inlined in
`CvLuaUnit::lGetPlot+0x15` (`mov r9d,[rax+48h]`, the unit pointer's page unmapped), called by `lua_pcall` from
`SrvRemote::HandleLuaRt` for `rt:LuaCall` on channel 0, the EXE main thread's interface Lua. `[I]` (from that stack and
the code below): a script of the interface kept a Unit instance table, the DLL deleted the unit, and the next method
call through the table read the freed CvUnit.

Why the DLL cannot see it `[C: CvLuaScopedInstance.h at c29442e8c, read by impl-0576]`: an instance is a Lua table
`{__instance = light userdata}` with the type table `LuaTypes.<Type>` as metatable, cached in the weak table
`LuaTypes.<Type>.__instances` by pointer. `GetInstance(L, idx)` returns the light userdata unchecked; nothing removes or
marks a table when its object is deleted. In stock (32-bit, in process) the same read usually hits freed heap that is
still committed and returns garbage without a fault `[I]`; in the 64-bit server it faulted because the page was gone.

## 2. Which instance kinds can go stale `[C: DLL source at c29442e8c, impl-0576]`

| Kind (`LuaTypes` name) | Object lifetime | Stale in a game? | Guarded |
|---|---|---|---|
| Unit (`CvUnit`) | per unit, in its owner's `FFreeListTrashArray`; freed by `CvPlayer::deleteUnit` (death, `Kill`, upgrade, founding with a settler, ...) | yes: any script that keeps a unit across its deletion | **yes** |
| City (`CvCity`) | per city; freed when it is razed, destroyed or captured (capture makes a new CvCity with a new id) | yes, the same way | **yes** |
| Player (`CvPlayerAI`) | the static `s_players` (`CvPlayerAI::initStatics` .. `freeStatics`, DLL load to unload); `Players[i]` built once by `CvLuaPlayer::pRegister` | no | no |
| Plot (`CvPlot`) | `CvMap::m_pMapPlots`, one `FNEW(CvPlot[iNumPlots])` in `CvMap::InitPlots` (CvMap.cpp 597-600, called from `CvMap::init` and the map's load at 1797), freed only in `CvMap::uninit` (806): map reset, load or game teardown | only across a map reset or load, when the EXE's interface Lua states are rebuilt too `[I]` | no (it would cost on the most frequent calls for no case seen) |
| Deal (`CvDeal`) | pushed from `CvDllScriptSystemUtility::PushCvDealInstance` and `CvLuaDeal.h`; deals live in the game's deal lists and the scratch deal | possible in principle `[I]`; there is no owner/id lookup to check against | no |

Instances nested inside a table argument, and instances given to a server-side closure that does not pass them on to a
DLL function, are not checked (the DLL reads only the arguments it is called with; a DLL function that walks a table of
instances is not known at the pin `[I]`).

## 3. The guard

Where: right before the server hands values to a DLL function, on the thread and under the lock of that call:
- `kRt_LuaCall` (`HandleLuaRt`): every top-level argument (self included) that is a table whose metatable is this state's
  `LuaTypes.Unit` or `LuaTypes.City` and whose raw `__instance` is a light userdata. On a stale one the DLL function is
  not called: the reply is one `nil` result, and server.log gets one LOUD line (first 200, then every 1000th).
- `HookGet` for `ICvScriptSystemUtility1::GetCvUnitInstance` / `GetCvCityInstance` (the engine asking the DLL for the
  object of a Lua value, e.g. `UI.SelectUnit(unit)`): a stale table is replaced by `nil` before the DLL reads it, so the
  engine gets what a script's nil gets (with `bErrorOnFail` the DLL's "Not a valid instance" error).

How (`SrvLuaStale::CheckArgs`), never dereferencing the pointer itself:
1. The identity is read by the DLL's own getters `Unit.GetID` / `Unit.GetOwner` (`City.GetID` / `City.GetOwner`),
   called directly in a fresh Lua frame under a structured-exception guard (`__try`/`__except` for
   EXCEPTION_ACCESS_VIOLATION and EXCEPTION_IN_PAGE_ERROR only). Each reads one field after `GetInstance` has restored
   the stack, so a fault there leaves no Lua call half done `[C: CvLuaUnit.cpp lGetID 4642, lGetOwner 5569,
   CvLuaCity.cpp lGetID 2715, lGetOwner 4549 (BasicLuaMethod on CvCity::getOwner)]`.
2. `Players[owner]:GetUnitByID(id)` / `GetCityByID(id)`, the DLL's own lookup in the owner's list (an owner outside
   0..255 is stale at once), must return a table whose `__instance` is the same pointer.
3. Anything else is stale: the read faulted (the object's memory is gone: the e711 case), the owner is out of range,
   the list holds nothing for that id, or another object.

Not a stale case by design: when the freed memory now holds another live object of the same kind (a new unit at the
same address), the kept table stands for that object, as in stock. The DLL's `GetUnitByID` maps an id below 1000 to
the n-th unit; that answer is still compared by pointer, so it cannot make a stale table pass.

The functions are learnt per process from `LuaTypes` when `RebuildTypeNames` runs (a closure of upstream master's
`protectedWrapperAux` gives its inner function, as issue 0313); the Players table is kept by a registry reference per
channel. server.log names what each channel covers: `remote: Lua stale-instance guard (ticket 0576) on channel N:
Unit checked, City checked`. A missing function turns the guard off for that kind and says "NOT checked".
`VP64_LUA_STALE_GUARD=0` in the server's environment turns it off (a test and measurement switch, not a product option).
The end-of-run report (`LuaReport`) has one line: values checked per kind, calls, mean ns per checking call, stale
per kind and how many of them were unreadable, calls not made.

## 4. Tests (`server/tests/remote/stale_instance/test_stale_instance.py`) `[C: run by impl-0576, 2026-11-19]`

`stale_unit.cpp` links `srv_lua_stale.cpp` with lua51_x64 and plays the DLL's Lua side with stand-ins of the pin's
shapes (type tables, weak `__instances`, instance tables, `Players`, getters that read through `GetInstance(L)`); each
unit and city lives alone in a VirtualAlloc region, so "freed" is unmapped as in e711. Command (stubs and source of
impl-0576's own `build_all` of the pin, `C:/c5-64bit-work/0576/`):

    python -I perf/quietlock/quietlock.py run --who impl-0576 --what "0576 test" -- python -I server/tests/remote/stale_instance/test_stale_instance.py --out C:/c5-64bit-work/0576/t3 --stubs C:/c5-64bit-work/0576/deps/stubs --src C:/c5-64bit-work/0576/src/x64-c29442e8 --asan

Result 2026-11-19 03:13 (`t4`): **51/51 checks** (clang-cl x64 /O2 and MSVC /fsanitize=address x64; 47/47 at 03:00
before the methods of crash 9a2f were added). The negatives fail without the guard: `freed-unguarded` (the same call
with no check) faults in the stand-in's `GetPlot` (e711's method) and, run with `transport`, in `GetTransportUnit`
(9a2f's method); `off-env` (`VP64_LUA_STALE_GUARD=0`) faults the same way; each ends with 0xC0000005 (under ASan:
ASan's access-violation report in that function). With the guard: freed unit nil and unreadable through `GetPlot` and
`GetTransportUnit`; deleted with memory intact nil, owner 3 id 8195; memory reused by other data nil (owner
0xA5A5A5A5); a new unit at the same address answered; freed and deleted city nil through `GetPlot` and `GetPopulation` while the
live unit answers; live unit,
city and `unit:Distance(otherUnit)` answered; numbers, strings, nil, plain tables, a table with `__instance` but no
metatable, a Plot instance and a type table never checked; a freed unit in argument 2 found; no Player type: mask 0;
wrapper closures learnt; the stack unchanged. No AddressSanitizer report in any guarded run (the caught fault included).
First run, 02:37: 21/22, the one failure the test's own: the stand-in plot was allocated after the unit was freed and
took its region (then the unit read as player 0's id 1: still stale, but not the unreadable case the check named).

## 5. Cost per call

Offline (the test's `bench`, 1,000,000 calls per arm, 3 rounds, 2026-11-19 03:00, workstation outside a quiet window,
COMMON 23: an indication): `unit:GetPlot()` through the stand-ins 512-715 ns unguarded, 1,330-1,427 ns guarded:
**+712 to +818 ns per call that carries a Unit or City instance** (median +747); a call with no instance argument
+23 to +29 ns (two rounds; one round -47 ns, noise). The guard's own timer: mean 617 ns per checking call. The 03:13
run (`t4`) gave +640 / +718 / +849 ns and a mean of 586 ns: the same. Where it goes `[I]`: one `lua_pcall` frame, the two
getters' `GetInstance` field reads, the owner's `GetUnitByID` with its `CvLuaUnit::Push` (four table reads) and the
constant-string pushes; the identity read cannot be made cheaper without the CvUnit layout, which the guard avoids. The stand-ins'
lookup does what the DLL's does (`CvLuaUnit::Push`: four table reads); the live number is in section 6.

## 6. Live run on the product route `[L, impl-0576, 2026-11-19]`

Instance c5h-f (F:, Goldberg, mods cp/vp/eui), `provision.py set-proxy` with impl-0576's layout
`C:/c5-64bit-work/0576/layout` (the pin's x64 DLL `2B56A1CD...` and libraries; server.exe `012795d6...` and proxy
`576c1cd8...` from this branch at d99318d88, built with `server/build_server.py --deps` of impl-0576's own build_all of
the pin; server.log says dirty=1 because a test-only file was uncommitted at the build). F: before the launch: 12
samples over 60 s, read latency 0 ms (no reads), writes 0.7-26 ms, idle 80-99 % (`tools/diskwatch`, 02:43-02:44).
Save copy C5H-0366-TINY_0008 (turn 9). server.log: the guard learnt on channel 0 (ui) at 9.1 s and channel 1 (core) at
98.6 s, Unit and City checked.

Step A (`live_0576.py c5h-f kill`, InGame state through the harness Lua channel, which runs on channel 1): a Warrior
(owner 0, id 1002) kept in a global, `Kill(false, -1)`; `GetUnitByID(1002)` true before, false after; then `GetPlot`,
`GetID`, `GetX` through the kept table: **nil, nil, nil**, three LOUD lines ("owner 0, id 1002: the owner's list holds
no object for that id ... Unit.GetPlot on channel 1"). The freed CvUnit's memory was still readable here (no fault).
Step A' (the crash's channel): a one-shot `Events.LocalMachineAppUpdate` listener in InGame (the EXE main thread's
interface Lua) called `GetPlot`, `GetMoves`, `IsDead` on the kept table: nil, nil, nil, three LOUD lines **on channel 0**.
Step B (`again`): `GetPlot`, `GetMoves`, `GetOwner` nil; `UI.SelectUnit(kept)`: the engine's `GetCvUnitInstance` got nil
(LOUD line "GetCvUnitInstance on channel 1"), the DLL's "Not a valid instance" error came back to the script's pcall,
no unit selected. No box, no crash.

The turn series (barbarian warriors spawned beside player 0's units each turn, then the turn ended; `C:/c5-64bit-work/
0576/turns.sh`): see 6.1.

### 6.1 Turn series

Tiny, turns 9 -> 21 (12 turns, 02:59:44-03:03:08), 38 barbarian warriors spawned beside player 0's units over the
series (2-4 a turn), Quick Combat and Quick Movement as saved; player 0 went from 2 units to 1. No box, no crash, no
LOUD line beyond the ten of steps A, A' and B. The end-of-run report (`server/evidence/0576/live-tiny/
server-report-lua.txt`): 132,563 `kRt_LuaCall` in the session (load and 12 turns), **44,160 of them carried a Unit or
City instance and were checked (33 %): 9,750 Unit and 34,410 City values, mean 942 ns per checked call**, 10 stale
(all 10 ours), 0 unreadable, 10 calls not made. That is 41.6 ms of checking over the session, about 3.5 ms per turn of
this Tiny game. For scale, the checked calls' own server time in `proxy-lua-calls.tsv` (which includes the check):
`City.IsHasBuilding` 15,560 calls at 1.71 us, `Unit.IsDead` 1,972 at 3.18 us, `Unit.GetPlot` 1,240 at 4.90 us,
`Unit.GetTransportUnit` 674 at 5.21 us (ui channel, epoch mirror open).

### 6.2 Huge save: the race met naturally `[L, impl-0576, 2026-11-19 03:13-03:25]`

The same layout and instance, save copy C5H-VP8P-HUGE_0350 (turn 350, 13 cities and 79 units for player 0). Step A for
a unit (owner 0, id 14743) and step C for a city (`City:Kill()` on the non-capital Memphis, owner 0, id 1567):
`GetCityByID` false afterwards, `GetPopulation` and `Plot` through the kept table nil, LOUD lines "stale City
instance ... City.GetPopulation on channel 1". Then 4 turns, 350 -> 354 (03:20:02-03:22:48; 4 barbarian warriors
spawned beside player 0's units each turn), no box, no crash.

**In those AI turns the EXE's interface Lua used deleted units by itself, on channel 0 (the crash's channel), through
the methods of both crashes.** The 200 LOUD lines server.log keeps (its cap; excerpt in `server/evidence/0576/
live-huge/server-log-0576-lines.txt`) name `Unit.GetTransportUnit` 76 times, `Unit.GetPlot` 76, `Unit.IsEmbarked` 15,
`Unit.IsGarrisoned` 14 and `Unit.GetFortifyTurns` 14, on more than 20 different units of players 1-7 and 31 (for
example owner 5 id 17288 20 times, owner 1 id 17309 15 times), each "the owner's list holds no object for that id":
deleted units whose memory was still readable, so without the guard these were reads of freed memory that happened
not to fault. Which script does it is `[I]`: that method set is what a unit flag update reads (EUI's
UnitFlagManager keeps unit objects in its flag tables); the C function is all that reaches the server. The
end-of-run report (`live-huge/server-report-lua.txt`): 3,215,921 `kRt_LuaCall` in the session (load, steps A and C,
4 turns); **1,293,808 checked (40 %): 206,309 Unit and 1,087,722 City values, mean 814 ns per checked call, 343 stale
Unit and 2 stale City (all readable), 345 calls not made.** `proxy-calls-per-turn.tsv` gives 0.74-1.02 M runtime
calls from the EXE per turn, so about 0.3-0.4 M checked calls and **about 0.26 s of server time per Huge turn** for the
check (the turns took 32-59 s of wall time with the UI; workstation outside a quiet window: an indication, COMMON 23).
City values are 84 % of the checks (city banners and EUI's city views); see section 7 for a cheaper form.

### 6.3 The laptop repro of e711 and 9a2f with the guard (ticket 0578)

impl-0578, 2026-11-19. The game both crashes died in: M from `TS0438-M-T150` (Standard Communitu, VP with EUI), 28
turns 151-178, options 2+3 (`AiUpdateTickMs 0`, `AiSkipVisualDrain 1`, `AiDrainUnordered 0`), Quick Movement on as
saved, the autoplay observer following player 0, laptop instance tb1; without the guard 2 of the 5 runs that reached
turn 172 crashed there (0573's jobs e711 r02-p1 and 9a2f r03-p1). Build (`perf/evidence/0578/build.sh`, 2026-11-19
03:59-04:05): server.exe `9AB7D548...`, proxy `9B459488...`, vp64helper.exe `16AA0053...` from main 99b3a624e (after
5e1a16e15) with `server/build_server.py --config release --offline`, packed as S4b4 with the pin's x64 DLL
`2B56A1CD...` (no hidden-embark DLL): arm S4g of `perf/turnstudy/runner/arms.json`, variant `f0_skip1_tick0` of
`scenarios/M.json` (0573's `he_off_skip1_tick0` without the hidden-embark key). The first job
(`20261119-110050-a966`, `submit.py run --host laptop --scenario M --arms S4g:f0_skip1_tick0 --runs 6 --order fixed
--turns 28 --slots 2 --idle-gap 180 --grade D`) played nothing: all 6 launches were refused by the harness's 6 GB
free-commit floor (`perf/quietlock/gate.py` `require_memory`; free commit 6.0 and 5.9 GB). A read-only census of the
laptop (job `20261119-113348-728b`, `perf/evidence/0578/commit-census-laptop-20261119-1134.txt`) `[L, 11:34]`:
commit 12.35 of 18.20 GB, the processes' commit charge only 4.68 GB (Steam's web helper 815 MB, the testbed runner's
Python 652 MB, Defender 400 MB; no game, server or helper left over), kernel paged pool 3.06 GB; up since
2026-11-11 14:41. The missing ~3 GB against 2026-11-13's 9.3 GB free is most likely kernel paged pool `[I]`. The
series ran as job `20261119-113534-bda0` with the job's environment `C5_MIN_FREE_COMMIT_GB=5` (the MC's grant, #6961;
`perf/evidence/0578/submit_floor5.sh`; a game on M takes about 2.5 GB of commit).

Counting: `perf/evidence/0578/count_guard.py <fetch>` over each run's `server-server.log` (the guard's set-up lines and
the LOUD stale lines per kind, method and channel: `perf/evidence/0578/guard-bda0.txt`); the totals from each server
run folder's `server-report.txt`, which the runner does not copy, through the read-only job `20261119-123957-2c41`
(`perf/evidence/0578/report_copy.py`, `report_lines.py` -> `server-report-lua-bda0.txt`); identity by
`perf/turnstudy/runner/analyze.py` and `tools/d10/statscmp.py`.

**Result `[L, laptop, 2026-11-19 11:39-12:40 EST, job bda0, DLL pin batch 4 2B56A1CD]`: 6 of 6 runs played all 28 turns
(151-178, exit 0); no server crash, no crashlogs folder, no box; every server ended because the runner's stop ended
the EXE (exit code 15, "not a crash code").** 9.37 s per turn (sd 0.05), grade D, an indication only.

| Run | `kRt_LuaCall` | checked calls (Unit / City values) | mean ns per checked call | stale Unit (unreadable) | stale City |
|---|---|---|---|---|---|
| r01 | 2,026,320 | 587,794 (289,949 / 297,904) | 946 | 583 (0) | 0 |
| r02 | 2,030,661 | 596,428 (289,881 / 306,606) | 948 | 545 (0) | 0 |
| r03 | 2,027,257 | 594,652 (289,869 / 304,842) | 975 | 545 (0) | 0 |
| r04 | 1,959,588 | 576,962 (289,831 / 287,190) | 978 | 501 (0) | 0 |
| r05 | 2,004,316 | 594,585 (289,652 / 304,992) | 964 | 693 (0) | 0 |
| r06 | 1,997,401 | 591,309 (289,768 / 301,600) | 935 | 582 (0) | 0 |

- **3,449 calls in the six runs were not made because an argument was a deleted unit** (501-693 per run; every
  one "not made", the reply nil). All on channel 0, the EXE main thread's interface Lua, the channel of both crashes.
- The LOUD lines server.log keeps (its first 200 per run; no run reached the 1000th stale call, so none after) name the
  methods: `Unit.GetTransportUnit` 346 and `Unit.GetPlot` 345 (**the two crash methods**), `Unit.IsEmbarked` 171,
  `Unit.IsGarrisoned` 169, `Unit.GetFortifyTurns` 169 over the six runs; 34-37 distinct units per run, owners 0-7;
  1,194 "the owner's list holds no object for that id" and 6 (r06) "the owner's list holds another object for that id".
  The same method set as 6.2's Huge turns (EUI's unit flags `[I]`).
- **0 unreadable**: in these runs the memory of every deleted unit was still mapped when it was asked for, so none of
  the 3,449 reads would have faulted; the e711/9a2f condition (the page gone) did not arise. Without the guard, 2 of
  5 runs that reached turn 172 hit it; 0 in 6 at that rate has a probability of about 0.6^6 = 4.7 % `[I]`. The DLL
  binary differs from 0573's (the pin's 2B56A1CD against the b2 hidden-embark DLL BCE6677E), and the heap's release of
  pages need not follow the game's identity `[I]`; why the condition did not return is not known. What these runs show
  is that the guard catches this game's stale units on the crash methods and that a stale call costs the game nothing
  (identity below); the fault path itself is proven by 0576's hermetic test (section 4), not here.
- **When**: the 200 kept lines fall at server seconds 61-154 of each run (the load and about the first turn
  `[I]`: the server log has no wall-clock anchor); so the turn-172 stale calls are inside the totals but not
  printed, and stale calls at turn 172 itself are not confirmed per turn.
- **Identity**: IDENTICAL within the six runs (analyze.py, BuildingYields 51,566 rows each). The same game as 0573's:
  r01 against 9a2f's complete off-run r01-p1 IDENTICAL, 15 tables, 111,862 rows
  (`perf/evidence/0578/statscmp-bda0r01-vs-9a2f-r01p1.txt`); against 9a2f's crashed run r03-p1 every table differs only by
  rows of turns 173 and later that the crashed run never wrote ("0 only in B"; `statscmp-bda0r01-vs-9a2f-r03p1-crashed.txt`).
- Cost: about 590,000 checked calls per run at 935-978 ns, about 0.56 s of server time per run, 0.02 s per M turn
  (laptop, grade D).
- Machine: laptop tb1; GPU drivers as recorded at D175 (2026-11-17 04:37: NVIDIA Quadro P3000 32.0.15.7322, Intel HD
  Graphics 630 27.20.100.9320), not re-read here.

## 7. What is open

- The cost (section 6.2: about 0.26 s of server time per Huge turn, 84 % of it on City values). The check cannot be
  skipped per call without knowing that nothing was deleted since the last one, and the server has no such signal
  today `[I]`. Ways to make it cheap, for a follow-up ticket (not done here): (a) a deletion generation counter that
  the DLL bumps in `CvPlayer::deleteUnit` / `deleteCity` and the server reads (a DLL change behind a define; then a
  pointer checked in the current generation needs no lookup); (b) the proxy sends its UI-Lua mirror epoch with each
  call, and a pointer checked inside the same epoch is not checked again; (c) a per-pointer cache of (owner, id) that
  skips the two getter calls and keeps the lookup (about a fifth of the cost `[I]`).
- The instance kinds not guarded (Plot, Deal, nested instances): section 2 says why; a stale one would still reach the
  DLL.
- The script of the interface that kept the unit in e711 is not known (only the C function reaches the server).
- The laptop repro target impl-0573 named (#6790) ran in ticket 0578 (section 6.3, 2026-11-19): 6 of 6 runs
  clean, 3,449 stale-unit calls stopped, 0 unreadable, so the faulting condition itself did not recur there. (Replaced
  2026-11-19: "Not run here: the laptop repro target ...".)
- 0573's second laptop crash (job 9a2f r03-p1, hidden embarks OFF, 2026-11-19, impl-0573's reading, MC #6786;
  `perf/evidence/0573/crash-9a2f/` on 0573's branch): the same class through another method,
  `CvLuaUnit::lGetTransportUnit` (the first member read after `GetInstance`). The guard does not depend on the method:
  it runs in `kRt_LuaCall` before ANY DLL C function gets its arguments, which is the one point where every call from
  the EXE's Lua (interface scripts, GameEvents handlers, the harness channel) resolves its instances on the server; the
  hermetic test has that method too (`freed-unguarded transport` faults without the guard; with it nil), and a city
  method (`GetPopulation`). Paths that never reach a DLL function with an EXE-held instance: the proxy's UI-Lua mirror
  (answers from its cache, no dereference), `kRt_LuaIndex` (indexes a server value), the DLL's own Lua threads.
