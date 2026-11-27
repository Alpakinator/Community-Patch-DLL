# Speedup P2: a GameEvents raise in one IPC crossing (ticket 4804, epic 0504)

impl-4804, from 2026-11-17 18:10 EST, workstation, account A. Brief: `tracker/issues/4804-speedups-p2-gameevents-one-crossing.md`;
the Architect's review `../07-architecture-decisions/architect-2026-11-17-speedup-epic-review.md` section 4 (it decides
the shape); the counts it rests on: `speedup-p0-measurements.md` P0.1. Marks: `[C]` read in our source or the DLL's at
the pin, `[L]` seen in a run, `[I]` inferred. Pins (COMMON 18, since 2026-11-17 16:55): x64 `4f4daac70` (fork branch
`x64-batch3`), layout `x64-4f4daac7-pf8e9f42`.

This file is the design note (written before the code, section 1-8) and then the results (section 9 onward).

## 1. Today: n + 3 crossings per raise `[C]`

A raise with n arguments in the client: `CreateArgs` (1 crossing), n `Push` (1 each), the `Call*` (1), `DestroyArgs` (1).
All through the generated `RemoteGen_ICvEngineScriptSystem1` / `RemoteGen_ICvEngineScriptSystemArgs1`
(`server/src/remote/srv_remote_lua.cpp` overrides only `CreateLuaThread`, `FreeLuaThread`, `LoadFile`, `CallFunction`,
`CallCFunction` and `GetString`). P0.1: M 151-160 16,467 raises per turn (2.56 args each), H 301-305 160,126 (3.32).

## 2. Census: what the DLL does with an args object `[C, fork 4f4daac70, read 2026-11-17]`

- **Made and freed only by `CvLuaArgsHandle`** (`Lua/CvLuaArgsHandle.cpp`): `CreateArgs()` or `CreateArgs(uiReserve)` in
  the constructors, `DestroyArgs(m_Ptr)` in the destructor. No other DLL file calls `CreateArgs`/`DestroyArgs`.
- **Every raise passes `args.get()`** to one of the five `LuaSupport::Call*` functions (`Lua/CvLuaSupport.cpp`
  490-569); no site passes NULL or another pointer (a search of `LuaSupport::Call(Hook|TestAll|TestAny|Accumulator)(`
  finds no call without `args.get()`). The engine interface is never called another way (P0.1).
- **Push return values are never used:** 360 `args->Push(` and 1 `args->PushNULL(` sites, all in statement form (a
  search for a Push inside an expression finds none).
- **Getters: only in `DeferHook`** (`CvLuaSupport.cpp` 397-443, upstream #13395, fork commit c125463b1 "Defer Lua hooks
  raised while a city is captured or destroyed"). Inside a `LuaSupport::DeferredHookScope` (two scopes:
  `CvCity.cpp` 2226 and `CvPlayer.cpp` 3969, city capture and destruction), `CallHook` (only `CallHook`) reads the args
  back **before** any raise: `Count()`, `GetType(0)` (the index-base guess: `ARGTYPE_NONE` means 1-based),
  `GetType(i)`, `GetBool/GetInt/GetFloat/GetString(i, ...)`, then defers the hook and does not raise it; at the scope's
  end it replays each with a new `CvLuaArgsHandle`, pushes and `CallHook`. If a read fails it raises at once on the same
  object. The DLL reads no args object after a raise (agrees with P0.1 and the Architect's two-file check).
  The EXE's index base for the getters is not in the knowledge base (`upstream-fix-batch-2.md` 2.4: "not checked
  against the EXE"); the EXE's `Push` returns the 0-based index (engine-contract 10.1 `[M]`).
- **One site raises twice with one args object:** `CvUnitCombat.cpp` 3249-3262 (`MustAbortAttack` with `CallTestAll`,
  then `CombatResult` with `CallHook`), only with `EVENTS_RED_COMBAT_ABORT` and `_RESULT`, both 0 in VP's
  `NewCustomModOptions.xml` at the pin. (Script: `C:/c5-64bit-work/4804/census_reuse.py`; the other hit it printed,
  `CustomMods.cpp` 127, is two different functions.)

## 3. Design

### 3.1 Server side (`srv_remote_lua.cpp`)

- `LocalArgs`, a twin of the headless host's `SrvArgs` (`server/src/srv_lua.cpp` 359): the values in a vector (tag,
  32-bit value, string), `Push*` returns the 0-based index as the EXE does. `CreateArgs()` / `CreateArgs(n)` of
  `RemoteScriptSystem` return one when the option is on (no crossing; `NoteEngineCall` still counted: it is an engine
  method, D30 conditions 2 and 4); `DestroyArgs` deletes it (no crossing; counted the same way).
- **Getters: materialise.** The first getter on a `LocalArgs` makes the EXE's object the old way (generated
  `CreateArgs` or `CreateArgs(n)` and every push so far, crossing) and forwards that getter and every later call of
  this object to it (a `RemoteArgs`, so `GetString` stays as it is). A raise on a materialised object goes the old
  generated way with the EXE's object, and `DestroyArgs` destroys the EXE's object. So the deferral answers exactly
  what the EXE answers today, whatever its index base, at the cost of today's crossings for these objects only (about 5
  hooks per city capture, upstream's count). A materialisation that fails (no script system, `CreateArgs` returned
  null) is a D118 failure: the server reports and exits (`gameevents_raise:` reason).
- `CallHook`, `CallTestAll`, `CallTestAny`, `CallAccumulator(int&)`, `CallAccumulator(float&)` of `RemoteScriptSystem`:
  on a not-materialised `LocalArgs`: **`SrvDbFlush()`**, then one call `kRtE_LuaRaise` through `SrvRt::Begin/End`
  (so **`NoteEngineCall`** in `Begin` and the **lock-depth sync** `SrvRemote::LockSync` in `End`, e7ea828's fix, run as
  for every engine call); the reply sets `value` and the return. A failed call (any status, a short reply) is a D118
  failure: the EXE did not run the listeners, so the game cannot go on (report and exit, as `exe_exception` does).
- **Impure for the epoch mirror:** the mirror's taint test is the engine-call word that `NoteEngineCall` bumps
  (`proxy_local.cpp` `EngineCalls`, D30 condition 2); a read served while a raise happened is not stored. Kept because
  the raise goes through `SrvRt::Begin`. The listeners' own calls into the DLL are proxy -> server calls with their own
  purity, as today.
- The option off: everything goes to the generated base methods as today; the overrides only count (3.5).

### 3.2 The message `kRtE_LuaRaise = 0xFFF10008` (server -> proxy, `proxy/include/vp_wire.h`)

Codec and the proxy's sequence in one header both sides compile, `proxy/include/vp_luaraise.h` (C++03, needs only
`vpipc/marshal.h`; the sequence is a template over the script-system and args types so the offline tests drive it with
fakes):

```
args: u32 kind (0 CallHook, 1 CallTestAll, 2 CallTestAny, 3 CallAccumulator(int&), 4 CallAccumulator(float&))
      u32 flags (bit 0: an args object exists (else NULL is passed), bit 1: made with CreateArgs(reserve))
      u32 reserve
      str name (not null)
      u32 value (the initial value: bool 0/1, the int, or the float's bits)
      u32 n (<= 256), n x (u32 tag (0 null, 1 bool, 2 int, 3 float, 4 string), then u32 bits | str)
ret:  u32 handled (the Call*'s return: true iff a listener is registered, engine-contract 10.1), u32 value
```

The whole CALL is at most `kLuaMaxBytes` (4 MB); the server refuses to send a bigger one (D118, reason names the
event) and the proxy refuses one with `n > 256`, an unknown kind or tag, a null name, flag bits outside 0-1 or bytes
left over (`kStatusBadArguments`, before any EXE call).

### 3.3 The proxy's sequence (`proxy/src/proxy_lua.cpp`, served on the EXE thread that waits in the raise)

Decode everything from a private copy first (listeners call back into the server on the same channel, which reuses the
channel's payload area), validate, then on the calling EXE thread: `CreateArgs()` or `CreateArgs(reserve)` (none when
bit 0 is clear), each `Push` / `PushNULL` in order, the `Call*` with the name and `value`, `DestroyArgs`. Same order as
today and the same thread; only the time between `CreateArgs` and the call shrinks (the EXE's args object exists only
inside the raise). `CreateArgs` returning NULL with bit 0 set: `kStatusFailed`, no call. The proxy's served-call
counters get the row `rtE:LuaRaise` (`proxy_rt.cpp`, one line).

### 3.4 Version, option

- **Lua bridge version** `vplr::kVersion = 1`: the proxy sends it as the (new) argument of `kRt_LuaRegister` (the first
  Lua call of every session, before any game exists); the server compares. A different version, or none (an older proxy)
  while the option is on, is refused: the server reports and exits with reason `lua_bridge_version:` (D118). An older
  proxy with the option off is logged LOUD once and runs as today (it never receives `kRtE_LuaRaise`).
- **Option** `[Performance] GameEventsOneCrossing` in `VP64Options.ini` (`options/`), 0 = off (default), 1 = on; read by
  the server only (`SrvOptions()`), once, at the first `CreateArgs`. The proxy serves `kRtE_LuaRaise` whenever it comes.
  Id after `kServerUnits` on this branch; epic 0437's three `[Performance]` ids renumber at whichever merge comes later.

### 3.5 Per-name log, both arms

Every raise of either path is counted by (name, kind): raises, `handled` true, materialised raises, and the value
change count (`value` after != before). Written at the report as `gameevents-raises.tsv` in the run folder (columns
`name kind raises handled value_changed one old materialised`, one row per (name, kind); the last three split the raises
by path). The arms must agree row for row on name, kind, raises, handled and value_changed for the same game
(`python proxy/tools/calls_report.py --raises ARM_A ARM_B`, exit 1 on a difference). This is also the listener
presence per name the Architect asked for (handled = a listener exists). The report line `GameEvents (ticket 4804): ...`
in `server-report.txt` gives the totals, the materialisations, the second raises of one object and the proxy's Lua
bridge version.

### 3.6 Two raises on one args object

`CvUnitCombat.cpp` 3262 (off in VP): each raise sends the packed values and the proxy makes a fresh EXE args object, where
today the same EXE object is passed twice. Equal by the contract (every listener of a call gets the pushed arguments,
engine-contract 10.1 `[M]`: a call does not consume its args object) `[I]`. Counted (`reraise` in the report line) so a
run shows whether it happened.

## 4. Statics and globals added (the two-thread hazard, Architect section 2)

| Name | Where | Live across a raise? | Reachable from a ui-thread DLL call? | Why safe |
|---|---|---|---|---|
| `s_iOneCrossing` (option, -1 until read) | srv_remote_lua.cpp | read-only after the first read | yes (both service threads raise) | an idempotent read of a start-up value |
| raise table by (name, kind) + its SRW lock | srv_remote_lua.cpp | no: updated after the reply, the lock never held across a crossing | yes | own lock, held for a map update only |
| counters (`reraise`, materialisations, failures) | srv_remote_lua.cpp | no | yes | interlocked |
| `LocalArgs` objects | heap, per `CreateArgs` | yes (the DLL's own object, as today) | owned by the `CvLuaArgsHandle` of the raising frame | not shared: one per handle, as the EXE's object is today |

None is DLL game state. The proxy adds two interlocked log counters (`g_nRaises`, `g_nRaiseRefused` in
`proxy_lua.cpp`); the sequence's state is on the serving thread's stack.

## 5. Failure modes (all loud)

| Case | Where | Result |
|---|---|---|
| proxy refuses the CALL (malformed, n > 256, unknown kind/tag) | proxy | `kStatusBadArguments`, no EXE call; server: report + exit `gameevents_raise:` |
| `CreateArgs` returned NULL in the EXE | proxy | `kStatusFailed`; server as above |
| CALL above 4 MB | server | report + exit before sending |
| older proxy (no `kRtE_LuaRaise`) | proxy -> server | refused at `kRt_LuaRegister` (option on) |
| getter whose materialisation fails | server | report + exit |
| the EXE throws inside the raise | proxy OnCall | existing path: `kStatusEngineException`, `exe_exception` (issue 9036) |

## 6. Tests

- Offline (no game): `server/tests/luaraise/` builds `vp_luaraise.h` with a fake script system and fake args (x64 and,
  under ASan, MSVC `/fsanitize=address`): codec round trip of every kind and tag, the proxy sequence's call order
  (create, pushes, call, destroy; reserve; no args), handled/value per kind, and the negative tests: truncated CALL,
  bytes left over, n = 257, unknown kind, unknown tag, null name, unknown flags, oversize (> 4 MB refused by the encoder),
  `CreateArgs` returning NULL, an event name with no listener (handled false, value unchanged), exit codes checked.
- Proxy build (32-bit, VC9 headers) compiles the same header through `proxy_lua.cpp`; `proxy/tools/test_proxy_rt.py`
  stays green.
- Live: windowed S4 against S4 with the option on (M 151-160), stats.db IDENTICAL, `gameevents-raises.tsv` equal.
- Timing: client M turn with D165 options 2+3+4 on (0437's runner), a D178 dev pair, then grade A (6 per arm) on
  `oldpc`; metric U per turn from 0437's budget timers, the ui thread's lock wait and the EXE's frame times in AI turns.

## 7. LU12

`PushTypeTable` / `PushInstance` (`proxy_lua.cpp` 613-700) read `LuaTypes`, `LuaTypes[name]`, `"__instances"` and
write `"__instance"` with `lua_getglobal` / `lua_getfield` / `lua_pushstring` on every instance push: a `strlen`, then
Lua's own interning (`luaS_newlstr`: hash and string-table lookup). Lua 5.1 interns every string already, so what can go
is the `strlen`: LU12 pushes the literals with their length (`VP_PUSHLIT` = `lua_pushlstring`) and the type name with
`name.size()`, then `lua_gettable` (what `lua_getglobal` / `lua_getfield` do inside) `[C, Lua 5.1 lapi.c]`. Same strings,
same table operations: exact by construction. Keeping the key strings in the registry and pushing them with
`lua_rawgeti` would trade the hash for a registry lookup per state; not done (no measured gain `[I]`). Value low (9077:
about 0.02 s per early turn in another port's model `[I]`); no timing claim is made for LU12 alone.

## 8. Proxy-calls readers

`proxy-calls.tsv` (server) gains the row `kRtE_LuaRaise` and, with the option on, loses most `CreateArgs`, `Push*`,
`Call*`, `DestroyArgs` rows. `proxy/tools/calls_report.py` and 0437's `vp_budget.h` (class `script`: it lists the
`kRtE_Lua*` ids one by one, so `kRtE_LuaRaise` must be added there, or it lands in `other`) are updated; 0437's file is
on its unmerged branch, so the one-line change is in the merge note (section 10). The server's `MethodKey`
(`srv_rt.cpp`) names the row `kRtE_LuaRaise`; the proxy's counters name it `rtE:LuaRaise` (`proxy_rt.cpp`).
`calls_report.py --raises` reads `gameevents-raises.tsv` (section 3.5).

## 9. Results

### 9.1 Builds and offline tests `[L, 2026-11-17 18:10-18:30 EST, workstation]`

- Build: `server/build_server.py --src <git archive of 4f4daac70> --deps <copy of a release build_all of 4f4daac70>
  --config release --offline`, server 106 s + proxy 50 s, no warning (`C:/c5-64bit-work/4804/build`). The deps copy's
  x64 DLL is not the pin's (`CFA3F3C1...` against `2D737BD5...`; Lua, database and localization DLLs equal the pin's), so
  every live run uses `C:/c5-64bit-work/4804/layout-p2`: the pin layout `x64-4f4daac7-pf8e9f42` (DLL `2D737BD5...`) with
  this branch's `server.exe` (`8FE3D6C1...`) and proxy (`C29F3707...`), before LU12.
- `server/tests/luaraise/run_luaraise_test.py`: 20,231 checks, 0 failed, x64, x86 and MSVC `/fsanitize=address` (canary
  caught; peak 87 MB). Covers every kind and tag, reserve, no args object, an empty one, no listener, the 257-argument and
  every malformed case (each prefix of a CALL, bytes left over, unknown kind, flags, tag, null name, arguments without an
  args object), no script system, `CreateArgs` returning NULL, the size formula, 20,000 fuzzed CALLs (1,583 decoded and
  served).
- Options: 220/220 x86, x64 and ASan (`options/test_asan.py`). `proxy/tools/test_proxy_rt.py` 160/160,
  `test_proxy_session.py` 148/148 (without `--asan`), `proxy/tools/hutest/build_hutest.py` PASS (55 checks; the real
  proxy and server; `kRt_LuaRegister` now carries the version).
- LU12 (section 7) built after the identity pair (proxy `AA02CAC9...`, 18:43): `test_proxy_rt` 160/160, hutest PASS
  again. Not run live (the identity pair ran on the proxy before LU12).

### 9.2 Windowed identity pair (S4, M 151-160) `[L, 2026-11-17 18:29-18:41 EST, workstation, c5h-b]`

Driver `perf/evidence/0504/4804/idpair.sh c5h-b 10 F:/c5-64bit-work/4804/idpair-1` (`idarm.sh` per arm: the
acceptance driver `perf/profilekit/acceptance.py run` loads `TS0438-M-T150` with cp, vp, eui, autoplay 10 turns;
SQLITE_LOGGING on and `VP64Options.ini` `[Performance] GameEventsOneCrossing = 0` then `1`, both backed up and restored
by SHA-256; `set-proxy` of `layout-p2` before, `--original` after; `provision.py verify c5h-b --quick` ok, no dialog box).
Results in `F:/c5-64bit-work/4804/idpair-1/` (`statscmp.txt`, `raises-compare.txt`, `crossings.txt`, `perturn.txt`).

- **stats.db: IDENTICAL**, 15 tables, 32,293 rows, turns 150-160 (`tools/d10/statscmp.py`).
- **Raises by name: equal on every (name, kind) but two:** 74 pairs, handled 656 in both arms, value changed equal;
  `GameCoreUpdateBegin` and `GameCoreUpdateEnd` (hooks raised once per `ICvGame1::Update` call, so they count the EXE's
  update calls, which depend on wall time, not on the game) 611 against 682. Off: 161,767 raises, all the old way; on:
  161,909, all one crossing (difference 142 = 2 x 71 extra updates). 0 materialised (no city captured in these turns),
  0 second raises, 0 raises without args. Proxy and server agreed on Lua bridge version 1.
- **Crossings:** script crossings per turn 151-160 **78,617 -> 14,139** (5.6x fewer; `proxy-calls-per-turn.tsv`), whole
  session 897,865 -> 161,911.

So the identity requirement holds for this window. Not covered: a city capture (the materialising path, `DeferHook`);
H; the 43-civ build; a client soak with the HI sweeper (Architect section 2).

### 9.3 Where a raise's time goes: the crossings were not the cost `[L, indication only: workstation shared with 12 registered jobs, D106; the same two runs]`

The server's own clock per crossing (proxy-calls.tsv `seconds`, the round trip seen from the server, whole session):

| Row (off arm) | calls | seconds | us per call |
|---|---|---|---|
| `Push(int)` | 411,271 | 0.327 | 0.8 |
| `CreateArgs()` | 155,736 | 0.308 | 2.0 |
| `DestroyArgs` | 161,767 | 0.191 | 1.2 |
| `CallTestAll` | 150,138 | 7.135 | **47.5** |
| `CallHook` | 10,587 | 0.259 | 24.5 |
| all script rows | 897,865 | 8.305 | 9.2 (mean) |
| **on arm: `kRtE_LuaRaise`** | 161,909 | 7.805 | 48.2 |

Per turn 151-160 the script rows took 0.717 s (off) and 0.692 s (on): **about 0.025 s per M turn saved, not 0.78 s.**
P0.1's estimate (`speedup-p0-measurements.md`, "What P0.1 decides") multiplied the removed crossings by the mean cost per
crossing (10.4 us); the mean hides that the pushes, `CreateArgs` and `DestroyArgs` cost 1-2 us each on this route and
the `Call*` itself 25-48 us. The `Call*`'s time is spent in the EXE (its `GameEvents` dispatch) and in waiting for it
[I: candidates, not measured: the EXE's Lua lock, which the ui thread holds while it runs UI Lua (the DLL releases its
own lock around every raise for exactly this reason, `CvLuaSupport.cpp` comment), and the dispatch itself]. Nearly
every raise has no listener (656 of 161,767 handled), so **the time is in P15's part** (no crossing for a name without a
listener), not in P2's. The turn times of the acceptance driver (5 s poll, frame cap lifted) show no difference either
(151-160: 147.9 s and 147.2 s from the load). The grade-A series on `oldpc` (D178, 0437's budget timers) was not run;
see the hand-back for the recommendation.
*Answered 2026-11-17 by impl-4811 (section 11): the `Call*`'s time is almost all the wait for the EXE's Lua lock while
the main thread runs UI Lua (0.59 of 0.70 s a turn, in about 8 raises a turn that wait over 10 ms); the dispatch of a raise
without a listener is about 1.3 us; the EXE takes the lock before it looks for a listener.*

## 11. Where a `Call*` spends its time, and what P15 would save (ticket 4811)

impl-4811, 2026-11-17 21:27-22:30 EST, workstation, account A (lead-0504). Brief:
`tracker/issues/4811-gameevents-call-cost-in-the-exe.md`. Evidence: `perf/evidence/0504/4811/` (`m1/`, `exp1/`, the drivers
`arm.sh` and `exp.sh`, the readers `split.py` and `expcmp.py`). This section answers the open `[I]` of 9.3 (what the
25-48 us of a `Call*` are). **Workstation figures, an indication only (D106):** at every start `quietlock.py check`
showed the lock free and 3 heavy jobs of other owners (two game instances, a server or a build; an LTO link during the
first run). GPU driver 610.88 (RTX 4080) for every run (D175).

### 11.1 The EXE's `Call*` takes its Lua lock before it knows whether a listener exists `[C; L]`

Read with read-only headless Ghidra 12.1.4 on a scratch copy of project `civ5` (Steam DX11 EXE), nothing of it kept (D83);
described here by behaviour. The script system's vftable 0xA5502C (14 slots; slot 0 `CreateLuaThread`) has the five
`Call*` at slots 8 (`CallHook`, 0x71BFB0), 9 (`CallAccumulator(float&)`, 0x596990), 10 (`CallAccumulator(int&)`,
0x53A960), 11 (`CallTestAll`, 0x6407C0) and 12 (`CallTestAny`, 0x45A350). Slots 8 and 11 were read: each takes the main
Lua state (`[0x2DD207C] + 0x81900`) and calls a helper (0x5B36C0, 0x601270) that fills a small context (the name, the
method string, e.g. `"TestAll"`, the args object) and calls the shared protected-call helper 0x904E30. That helper
**enters a critical section** (0x813D50, a fastcall wrapper of `KERNEL32!EnterCriticalSection` through the EXE's import
slot), runs `lua_cpcall` of the C function 0x88E450, reports a Lua error if any, and **leaves the section** (0x813D60).
Inside the protected call 0x88E450 reads the global `GameEvents`, its field named by the event, that table's method
field; when it is a function it pushes the arguments through the args object's getters (0x88E330, by type) and makes
`lua_call` with one result: `handled` = the result is not nil, the value = its truth. So the lookup of the name and the
call of the Lua dispatcher run **inside the lock, for every raise, also without a listener** (the dispatcher returns nil
then, which is how `handled` false arises; engine-contract 10.1) `[C]`. Slots 9, 10 and 12 were not read; live, every
raise of every kind entered the same section first (161,273 of 161,273 in run m1, 666 of 666 in r6B) `[L]`.

**Answer for P15: the EXE answers "no listener" after taking the Lua lock and after a Lua call of the event's
dispatcher, never before.** A raise without a listener costs the EXE a section enter, `lua_cpcall` (it makes a C
closure), three table reads and one Lua call; measured 0.7 us (median) when the section is free (11.3).

### 11.2 Method: a measuring proxy and a P15 stand-in (scratch branches, never merged)

- `scratch-4811-measure` (a00d636c7, `proxy/src/proxy_lua.cpp` only): `ServeLuaRaise` runs a copy of `vplr::Serve` with
  QueryPerformanceCounter points around `CreateArgs` + pushes, the `Call*` and `DestroyArgs`, and the whole serve; the
  proxy writes the EXE's import slots of `KERNEL32!EnterCriticalSection`, `LeaveCriticalSection` and
  `TryEnterCriticalSection` (data writes to the import table, no code bytes) with timers. The first section the EXE
  enters inside a `Call*` is taken as the Lua section (11.1); per raise one row (wait = the time inside that first enter;
  dispatch = the rest of the `Call*`), per EXE thread the outermost hold times and the waits of that section. Rows in
  `C:/c5-64bit-work/4811/live*` (the raw raise rows, 10 MB a run, stay there), flushed at every `GameCoreTestVictory`
  raise (the DLL raises it once per turn: the turn marker).
- `scratch-4811-with0437` (c977a6430): the same plus a trial merge of `epic-0437-turn-study` for D165 options 2+3+4 and
  0437's budget timers (conflicts only in `options/`: 0437's option ids became 6-8 there; `kRtE_LuaRaise` added to
  `vp_budget.h`'s script class, the line section 8 names). Then 9b76ed56c: **the P15 stand-in** in
  `srv_remote_lua.cpp`: with `VP4811_SKIP=<file>` a raise whose name is listed returns at once on the server (handled
  false, value unchanged: what the EXE answers without a listener), with no `SrvDbFlush`, no crossing and no
  `NoteEngineCall`. The list: the 69 names with no listener in run m1 (`perf/evidence/0504/4811/skip-names.txt`; 68 from
  run r3B on, `GameCoreTestVictory` kept as the turn marker).
- Builds: `server/build_server.py` release, offline, fork source by `git archive 4f4daac70`, 0 warnings, into
  `C:/c5-64bit-work/4811/build-0437` (proxy `c1c4334e...`) and `build-exp` (server `8b0e7510...`, proxy `0c2a1d09...`);
  layouts `layout-m` and `layout-exp` = the pin layout `x64-4f4daac7-pf8e9f42` (DLL `2D737BD5...`) with these
  `server.exe` and proxy (SHA-256 lists in the evidence).
- Game: windowed S4 (the 32-bit EXE, the server behind the proxy) on `c5h-b` (pool claim on 0504), `TS0438-M-T150`
  (cp, vp, eui), autoplay 10 turns through `perf/profilekit/acceptance.py`, `VP64Options.ini` `[Performance]`
  `GameEventsOneCrossing = 1`, `AiUpdateTickMs = 0`, `AiSkipVisualDrain = 1`, `AiDrainUnordered = 1` (all four installed
  per `vpproxy.log`), the ini backed up and restored by SHA-256; set-proxy before, `--original` after; every run rc 0,
  no dialog box. Run m1 (21:46-21:50) without the budget switch; the experiment exp1 (21:54-22:19) with
  `VP64_TURN_BUDGET=1`, runs in the order A B B A A B (A: every raise crosses; B: the stand-in).

### 11.3 The split (run m1, M 151-160, D165 options 2+3+4 and P2 on) `[L, indication]`

The window is the raises up to the 10th `GameCoreTestVictory` (161,273 raises, 0 nested); per turn = / 10. IPC rest =
the server's mean per `kRtE_LuaRaise` (proxy-calls.tsv, 161,827 calls, 7.061 s, 43.63 us) minus the proxy's mean serve
(38.65 us); per name it is that mean `[I]`.

| Set | raises / turn | lock wait | EXE dispatch | create + destroy | proxy | IPC rest | total | per turn: wait | dispatch | total |
|---|---|---|---|---|---|---|---|---|---|---|
| all | 16,127 | 36.51 us | 1.47 us | 0.31 us | 0.36 us | 4.98 us | 43.63 us | **0.589 s** | 0.024 s | **0.704 s** |
| no listener | 16,062 | 36.07 | 1.29 | 0.31 | 0.36 | 4.98 | 43.00 | 0.579 | 0.021 | 0.691 |
| listener | 66 | 143.99 | 47.17 | 1.00 | 0.80 | 4.98 | 197.94 | 0.009 | 0.003 | 0.013 |

- **The time is the lock wait, and it sits in a few raises.** The median raise waits 0.1 us and its whole `Call*` takes
  0.7 us (no listener: p90 2.2 us, p99 13.2 us). 7.9 raises a turn wait more than 10 ms and carry 0.571 of the 0.589 s
  (12.1 a turn more than 1 ms, 0.585 s; the longest 193 ms). Without the wait a raise costs about 7 us end to end, of
  which the IPC is 5 us; the EXE's dispatch is 1.3 us without a listener and 47 us with one.
- **By name** (seconds a turn): `PlayerCanConstruct` 0.182 (5,561 raises, mean wait 26 us), `CanDeclareWar` 0.156 (555,
  273 us), `EconomicStrategyCanActivate` 0.102 (72, 1.39 ms), `PlayerCanTrain` 0.058, `MilitaryStrategyCanActivate`
  0.035 (20, 1.71 ms), `CanHavePromotion` 0.025, `UnitSetXY` 0.022; the full table is `m1/split.txt`. Names raised at a
  player's start (strategy checks) wait longest per raise `[I: where the main thread's passes begin]`. By kind:
  `testall` 0.648 s, `hook` 0.055 s, `testany` and `accum_int` 0.001 s.
- **Who holds the lock** (the change over turn markers 1-10, 9 turns): the EXE's main thread (the ui thread, the
  process's first) holds the section 12,256 times a turn for **0.861 s a turn**, 10.7 holds a turn longer than 10 ms, the
  longest 581 ms; it raised 62 events a turn itself (8 us each). The core thread (the one in `ICvGame1::Update`) raises
  16,065 a turn and **waits 0.574 s a turn** for the section; its own holds are short (mean 1.9 us), so the main thread
  waited only 0.05 s in the whole session. About two thirds of the main thread's Lua hold time is time the AI's thread
  stands still. The other sections the EXE entered inside a `Call*` (656, all in listener raises) cost 0.
- The server's mean (43.6 us) is below 9.3's 48.2 us; 9.3's runs had no D165 options.

### 11.4 The P15 stand-in: about half of the wait goes, the rest moves to the listener raises `[L, indication]`

Per turn, full turns 152-159 of each run (0437's `budget.tsv`; `exp1/expcmp.txt`):

| Run | arm | turn wall | into_exe.script | in update (U) | in lock (ui's DLL lock sessions) | core thread's Lua wait | main thread's Lua holds |
|---|---|---|---|---|---|---|---|
| r1A | A | 3.728 s | 0.708 s | 2.703 s | 0.007 s | 0.619 s | 0.957 s |
| r2B | B | 3.446 | 0.411 | 2.459 | 0.027 | not recorded (*) | not recorded (*) |
| r3B | B | 3.239 | 0.351 | 2.265 | 0.026 | 0.331 | 0.856 |
| r4A | A | 3.494 | 0.619 | 2.522 | 0.007 | 0.518 | 0.836 |
| r5A | A | 4.815 | 0.877 | 3.572 | 0.007 | 0.751 | 1.252 |
| r6B | B | 4.328 | 0.548 | 3.102 | 0.039 | 0.525 | 1.265 |
| mean A / B | | 4.012 / 3.671 | 0.735 / 0.436 | 2.933 / 2.609 | 0.007 / 0.031 | 0.629 / 0.428 | 1.015 / 1.061 |

(*) r2B skipped the turn-marker name too, so its proxy rows were never flushed; its means are over r3B and r6B. Run
pairs in time order (r1A-r2B, r3B-r4A, r5A-r6B): turn wall **-0.28, -0.26, -0.49 s, mean -0.34 s a turn** (B faster in
each; r5A and r6B ran slower together: load on the machine). `into_exe.script` -0.30 s a turn, U -0.32 s.

Where the rest of the wait went: in B the 66 listener raises a turn (`CityConstructed` 20, `PlayerDoTurn` 25,
`SetPopulation` 9, `CityBoughtPlot` 6, `TeamTechResearched` 6) wait 5.0 ms (r3B) and 7.9 ms (r6B) on average against
0.14 ms in A: `CityConstructed` 10-15 ms each, 0.20-0.30 s a turn. The core thread still meets the main thread's long Lua
passes, now at the listener raises; with fewer acquisitions it meets fewer of them. The ui thread's DLL lock sessions
(`in lock`) rose by 0.02 s a turn: the DLL's own lock release around each raise (`CvLuaSupport.cpp` 381-415) stays and
the ui thread's calls wait a little more for it. Game identity of the stand-in was not compared (it answers what the
EXE answers for a name without a listener; `stats.db` not written in these runs).

### 11.5 What P15 would save `[L for M as an indication; I for H]`

- **Client M turn** (D165 options 2+3+4 and P2 on, about 4.0 s a turn on the workstation): **about 0.34 s a turn**
  (8 %; three pairs above). Bounds: at most the 0.69 s a turn the raises without a listener cost in A (11.3), at least
  their wait-free part, 16,062 x 6.9 us = **0.11 s** a turn (IPC, proxy, create, destroy and the EXE's dispatch).
  About half of the wait stays as long as listeners run for hooks raised in AI turns (the five names above: CP, VP and
  EUI register them).
- **Client H turn** `[I]`: P0.1 counts 160,126 raises per H turn (301-305), 0.13 % with a listener by name. The
  wait-free part at M's per-raise cost: 160,000 x 6.9 us = **about 1.1 s a turn**. The wait part is not measured on H;
  on M it follows the main thread's UI Lua passes (their number and length in a turn), not the raise count, so it is not
  scaled here. Most of H's raises are one name (`CanLoadAt`, testany, 83,387 a turn), so a P15 for that name alone holds
  most of the wait-free part.
- What a P15 design has to keep (from the stand-in): the answer for a name without a listener (handled false, value
  unchanged); whether `NoteEngineCall` may be skipped for such a raise (the stand-in skipped it; D30's purity rule for the
  epoch mirror decides) `[I]`; how the server learns that a name gained a listener (a mod or the UI can add one at any
  time with `GameEvents.<name>.Add`) `[I]`.
- **The larger cost is not a crossing:** the AI's thread is serialised behind the main thread's UI Lua passes (0.84-1.27 s
  of Lua holds a turn, 11-18 of them longer than 10 ms). P15 removes about half of that coupling; the rest is in the
  listener raises. What those passes are (which UI Lua, on which events) is not identified here.

### 11.6 Not verified

- One run for the split and three pairs for the stand-in, on the shared workstation; no `oldpc` grade-A series. H not
  run. The absolute times include the measuring build's own cost (two QPC reads per hooked section enter and about six
  per raise).
- The Lua section is identified as the first section a `Call*` enters (11.1 and every raise live); its object (a heap
  address, `063BBAD4` in m1) was not traced to its owner, and slots 9, 10, 12 were not read.
- The stand-in's game identity; the main thread's long passes by source.

## 12. P15 design: the server answers raises for names without a listener (for the Architect; no code yet)

lead-0504 (third lead), 2026-11-17 23:00 EST, on the MC's request (#4811 on thread 0504). Rests on section 11
(`[C; L]` facts) and the Architect's review section 4 (P15's corrected premise). Every mechanism below is `[I]` until the
step that tests it; the facts it needs and does not yet have are listed in 12.8.

### 12.1 What P15 removes, and what stays

- **Removed:** for a raise whose name has no listener, the crossing and the EXE's `Call*` (the Lua lock enter, the
  `lua_cpcall` of the dispatcher, the `nil` answer). Those cost 43.0 us a raise on M, of which 36.1 us is the lock wait
  (11.3); the stand-in saved about 0.34 s a client M turn, bounds 0.11-0.69 s; H's wait-free part is about 1.1 s a turn
  `[I]` (11.5).
- **Kept, unchanged:** the DLL's own code around the raise (the game-core lock release and re-take of
  `CvLuaSupport.cpp` 381-415, `CvLuaArgsHandle`); P2's `LocalArgs` (P15 acts only on a not-materialised `LocalArgs`,
  so P15 requires `GameEventsOneCrossing = 1`); `NoteEngineCall` (D30 conditions 2 and 4: since issue 2801 every engine
  method the server answers itself is counted, `proxy-design.md` 16.4; the stand-in skipped it, P15 must not);
  `SrvDbFlush` (it costs nothing when nothing is pending; dropping it is a separate question, not P15's); the per-name
  log of 3.5 (a local answer is counted as such: a new column `local`).
- **The answer** for a name without a listener is the EXE's own (engine-contract 10.1 `[M]`, 11.1 `[C]`): `handled`
  false and the value unchanged, for all five `Call*`.

### 12.2 How the server learns listener presence: from the EXE's own answers, invalidated by a generation

Not a mirror of the listener lists (the Architect's sketch kept a count per name in shared memory; that needs the event
objects' layout and their per-context `Count` semantics, neither known, 12.8). Instead:

1. **The server learns from the crossings it makes today.** Every raise that crosses returns `handled` (P2's reply
   carries it). The server keeps, per service thread, a table name -> (`gen`, `handled`), where `gen` is the
   generation it read **before** that crossing.
2. **A local answer** is given only when the table says `handled` false for this name and the generation now equals
   the stored one. Otherwise the raise crosses and refreshes the entry. An entry with `handled` true always crosses.
3. **The generation** is one 32-bit word in the existing shared memory `Local\VPProxyLock_<pid>` (16 LONGs; slot 9 is
   free, slots 0-8 are described in `proxy/src/proxy_local.cpp` 79-84). 0 = not installed: the server answers nothing
   locally. The proxy sets it to 1 when its wrappers are in place and increments it (`InterlockedIncrement`, a full
   barrier) at every listener registration. The server reads it with an acquire load (on x64 a plain aligned load).
4. **What the proxy wraps:** the `Add` method of the `GameEvents` event objects in the EXE's Lua state (Lua heap data,
   D4; no EXE code), so that a registration first runs the original `Add` and then bumps the generation, before it
   returns to the registering script. `Remove` and `RemoveAll` are not needed for correctness (removing a listener can
   only turn a crossing raise into one that could be local: the safe direction); bumping on them too lets a name become
   local again after its listeners go, at one re-learning crossing per name. The design bumps on all three.

Why this is simpler than a count per name: no strings in shared memory, no per-context counting, no read of the EXE's
listener lists; a listener registered before the wrappers exist is learned from `handled` like any other. The only way
to be wrong is a registration that bypasses the wrapped `Add` (12.5 catches it).

### 12.3 Order between the ui thread's registrations and the core thread's raises

The claim: every local answer is one the stock EXE could have given at that moment, so P15 stays inside stock's own
timing freedom (the Architect's open question).

- In stock a raise and a registration are ordered by the EXE's Lua critical section (11.1: every `Call*` enters it
  before it reads `GameEvents`; ui Lua runs holding it, which is why the core thread waits 0.57 s a turn). A raise that
  wins the section before a registration sees no listener; one that comes after sees it. Which one wins is timing, not
  game logic: both orders are stock behaviour.
- With P15, take a local answer at time t, from an entry learned in crossing C with generation g read before C, and g
  again at t. Let R be any `Add` whose original call completed before t; R bumps after its original call. If R's bump
  came after the read of g, the generation at t is not g: no local answer. So R's bump, and with it R's original call,
  completed before C began, and the EXE's answer in C already saw R's listener (`handled` true): again no local answer.
  So no `Add` that completed before t has a listener the answer ignores.
- The only registration a local answer can miss is one still inside the wrapped `Add` at t (original call done, bump
  not yet). That `Add` has not returned to its script, so nothing in the game can depend on it yet: the answer is the
  one stock gives when the raise wins the section just before that registration. This needs no assumption about the
  Lua section, only that the bump comes after the original `Add` and before the wrapper returns.
- Causal chains across threads stay intact: if the ui thread registers and then calls into the DLL (or does anything
  the core thread later observes), the bump happened before that call; the observation reaches the core thread through
  the proxy's IPC and the game-core lock (both full barriers), so the core thread's next raise reads the new generation
  and crosses.

### 12.4 The option, the modes, fail-loud

- `[Performance] GameEventsNoListenerLocal` in `VP64Options.ini`: 0 = off (default), 1 = on, 2 = verify. Read by the
  server once, like P2's option; needs `GameEventsOneCrossing = 1`, else logged LOUD once and treated as 0. The proxy
  installs its wrappers when the server asks (at `kRt_LuaRegister`, Lua bridge version 2); an older proxy with the
  option on is refused there with reason `lua_bridge_version:` (D118), as P2 does.
- **Wrappers not installed** (the event objects are not shaped as 12.8's read found them; the generation word cannot be
  mapped): the generation stays 0, the server crosses every raise (the exact path) and both sides log a LOUD line with
  the reason; `server-report.txt` shows `P15 local answers 0, not installed: <reason>`. This is a speed option not
  engaging, not a broken world (D118's case), so the game goes on; an acceptance or timing run checks the report line
  and counts a not-installed P15 as a failed run.
- **A generation that goes back or to 0 after install:** impossible by construction; the server checks and marks the
  run FAILED (D118 report) if it sees one.

### 12.5 Verify mode and the controls

- **Mode 2 (verify):** the server computes the local answer, crosses anyway and uses the EXE's answer (the game is
  stock's). A raise it would have answered locally where the EXE says `handled` true is a **miss**: counted by name,
  logged with the generation and the turn, and the run is marked FAILED. Would-be-local raises are counted by name (the
  coverage of a verify run).
- **Negative control (bypass):** `VP64_P15_SPOIL_BUMP=<name>` makes the wrapper skip the bump for that name's first
  `Add` after turn N; a test mod (or the harness Lua channel) adds a listener to a hot name without one at turn N.
  Verify must report misses for that name; mode 1 must change `stats.db` (the listener is chosen to change a decision,
  e.g. `PlayerCanTrain` false for one unit class).
- **Control (late registration, no spoil):** the same mid-game `Add` without the spoil: mode 1 and mode 0 give
  IDENTICAL `stats.db`. This tests 12.2-12.3 directly: a registration after the name was learned as "no listener".

### 12.6 Identity, timing, acceptance

- Windowed S4 (the 32-bit EXE behind the proxy), `TS0438-M-T150`, M 151-160, P2 on: mode 0 vs 1 `stats.db` IDENTICAL;
  mode 2 0 misses; the two controls of 12.5. Per-name logs (3.5) equal row for row on name, kind, raises, handled and
  value_changed between the arms; the `local` column shows the coverage.
- Headless is untouched (the headless host's own Lua, `srv_lua.cpp`, never crosses; P15 is remote mode only): one
  headless M 150-160 IDENTICAL to the pin as a guard.
- H: one windowed H 301-305 pair (mode 0 vs 1) IDENTICAL: H's raises are mostly `CanLoadAt` (83,387 a turn, P0.1),
  where most of P15's value is.
- Timing: a D178 dev pair on `oldpc` (client, M 151-160, P2 and the D165 options on in both arms), then grade A by
  PERFORMANCE.md before default-on; the memory after 10 turns (D151; the tables hold a few hundred names). Every series
  names its GPU driver (D175).
- Before default-on (the Architect's section 2): the client soak with the HI sweeper opening screens during AI turns,
  verify mode on.

### 12.7 Statics and globals added (two-thread hazard)

| Name | Where | Live across a raise? | Reachable from a ui-thread DLL call? | Why safe |
|---|---|---|---|---|
| name -> (gen, handled) table | server, thread-local per service thread | yes (between raises) | the ui thread has its own | not shared; holds only answers the EXE gave |
| option value | server | read-only after the first read | yes | idempotent start-up read |
| local, miss, would-be-local counters | server | no | yes | interlocked |
| generation word (slot 9 of `Local\VPProxyLock_<pid>`) | shared memory | yes | written by the proxy on the registering thread, read by both service threads | one aligned 32-bit word; writer `InterlockedIncrement`, readers acquire; order argument 12.3 |
| the original `Add` / `Remove` / `RemoveAll` | references in the EXE's Lua registry, held by the proxy's wrappers | yes | touched only by Lua code running in the EXE | the EXE's Lua state, as P2's thunks are |

None is DLL game state; the DLL is not changed.

### 12.8 Open facts the first step must establish (read-only, before any code)

1. **The event objects' shape** in the EXE's Lua state: whether all `GameEvents.X` share one method table or metatable
   (one wrap of `Add` covers every name) or each object has its own methods (then the wrap goes on `GameEvents`'
   `__index`, which makes the objects on demand), and whether `Add` is a C function or Lua. Read with the existing Lua
   channel (`getmetatable`, `debug.getinfo(...).what`, `GameEvents.A.Add == GameEvents.B.Add`) on S4. The Architect's
   review left this open.
2. **One `GameEvents` or one per UI context.** `Events` is per context (154 tables, `proxy-design.md` 3305) and a raise
   reaches every context's listeners; if `GameEvents` is the same, every context's table needs the wrap (the proxy
   reaches every context through `_G.Threads`), installed when the context is made.
3. **The install point:** the wrappers must be in place before any script can keep a reference to an unwrapped `Add`
   (`local add = GameEvents.X.Add` at load). Candidate: the creation of the EXE's main Lua state, before the first
   context loads. A registration before the install is harmless (12.2: learned from `handled`); a reference kept from
   before the install and used later is a bypass, which verify catches.
4. Whether any registration reaches the event objects other than through `Add` (a script writing a listener list
   directly; the EXE itself). Verify over the M and H windows and the coverage saves answers this empirically.

### 12.9 Size, order, value

- Code: proxy (the wrappers, the install, the generation word: `proxy_lua.cpp`, `proxy_local.cpp`), server (the table,
  the local answer, verify, the report line: `srv_remote_lua.cpp`), `options/` and `docs/OPTIONS.md`, tests in
  `server/tests/luaraise/`. Estimate 6-10 h with the windowed identity runs, plus the `oldpc` series.
- Order: 12.8 first (about an hour, read-only), then a go/no-go to the MC if the shape needs more than the two wrap
  points above. P15 does not depend on the DLL pins (no DLL change).
- With 0518 (which ui Lua holds the EXE's Lua lock during AI turns, account B): if 0518's fix shortens the main thread's
  Lua holds, every raise's wait shrinks and P15's value tends to its wait-free floor (0.11 s an M turn, about 1.1 s an H
  turn `[I]`), so P15 then matters mostly on H. Without such a fix about half of the wait moves to the listener raises
  (11.4) and P15's M value stays near 0.3 s.
  *Corrected 2026-11-18 (ticket 4812, the Architect's review): the value claimed is the wait-free part only (about
  0.11 s per client M turn, about 1.1 s per H turn), because the gate (review change 1) keeps the wait; 0518/0522
  showed the wait is mostly the autoplay observer's (`../03-dll-engine-boundary/ui-lua-lock-holds.md` 2.2, 5.1).
  Replaced: "P15's M value stays near 0.3 s".*

## 13. P15 step 1: the read-only facts, and the design as built (ticket 4812)

**Status (lead-0504, 2026-11-18 20:40 EST): stopped by the ticket's rule (13.6: about 0.24 s a client H turn under O,
interval -0.52..+0.04 s, below half of the 1.1 s estimate). The P15 code is NOT merged into epic-0504-speedups: it
stays on branch `task-4812-p15` 987bdf7af (pushed; one tree: options, `docs/OPTIONS.md`, `proxy/include/vp_luaraise.h`,
`proxy/src/proxy_lua.cpp`, `server/src/remote/srv_remote_lua.cpp`, `server/tests/luaraise/`). Its option id 12 is the
MC's reservation for epic 0561's ServerCities (MC #6033, K4): if P15 is ever taken up again, ask the MC for an id and
renumber. Sections 13.1-13.6 below are the record; "the build" means that branch.**

impl-4812 (worker of lead-0504), 2026-11-18 17:26-18:10 EST, workstation, account A. Brief:
`tracker/issues/4812-speedups-p15-gameevents-no-listener-local.md`; the design is section 12 as changed by the
Architect's review `../07-architecture-decisions/architect-2026-11-17-p15-review.md` (eight changes; where they differ
the review wins). Probe run: windowed S4 on Goldberg instance `c5h-e` (pool claim on 0504), the batch-4 pin layout
`x64-c29442e8-p58724e0` (proxy `3DFD7521...`, server `FC73E60E...`, DLL `2B56A1CD...`; no `[Performance]` option),
`TS0438-M-T150` (`98fc454a...`, cp, vp, eui), 2 autoplay turns through `perf/profilekit/acceptance.py`
(`perf/evidence/0504/4812/probe_arm.sh`); read-only Lua through the harness Lua channel at turn 150
(`probe_shape.lua` in the states `_G`, `Main`, `InGame`; `probe_ctx.lua` in `_G`): `rawget`, `getmetatable`,
`debug.getinfo` and `:Count()` on objects that exist; no listener added, nothing written. Outputs:
`perf/evidence/0504/4812/probe1/`. *Order note: the probe's first try missed (its turn check matched the channel's
error text) and the game took 18 minutes to load on the shared machine, so the server side, the option and the
offline tests were written while it loaded; none of them depends on the shape facts except the install, written after
13.1.*

### 13.1 The shape of GameEvents in the EXE's Lua `[L, 2026-11-18 17:57-18:00 EST]`

- **`_LOADED._G.GameEvents`** (the table the EXE's `Call*` reads, 11.1) is a table with a metatable whose `__index` is
  a C function. Its raw keys are the event objects made so far: 13 at the first read (turn 150), **58 a minute later**
  during the AI turn, among them `CanDeclareWar`, `PlayerDoTurn`, `UnitSetXY`: raising a name makes its object
  (`__index` makes and stores it) `[L; I that it is the Call*'s own read of the field]`. The table's address did not
  change between the reads.
- **An event object** is a table with nine raw fields, all C functions (`debug.getinfo` what = `C`): `Add`, `Remove`,
  `RemoveAll`, `Count`, `Call`, `TestAll`, `TestAny`, `Accumulate`, `AccumulateINT`. **Each object has its own `Add`**
  (`GameEvents.A.Add ~= GameEvents.B.Add`) and its own (empty) metatable; the C functions have no upvalues
  (`debug.getupvalue` returns none). The listener lists are not visible as Lua data: a script can register only
  through an object's `Add` (12.8 item 4, as far as Lua can reach) `[L]`.
- **One GameEvents per UI context** (12.8 item 2). `_LOADED._G.Threads` maps 154 context threads to environments; **151
  have their own `GameEvents` table, 3 have none, none is `_G`'s; all 151 share `_G`'s metatable** (one table) **and
  so its one `__index`**. The listeners live in the contexts' objects: `PlayerDoTurn` (EUI `DiploCorner`),
  `CityConstructed` (`TechTree`), `SetPopulation` and `CityBoughtPlot` (`NotificationPanel`), `TeamTechResearched`
  (`GlobalCityBombardRange`) each count 1 in their context and **0 in `_G`'s object**, while a raise reaches them
  (11.4: `PlayerDoTurn` and `CityConstructed` are listener raises, `handled` true). So `Count` is per context, like
  `Events` (`../03-dll-engine-boundary/proxy-design.md` 24.2), and a raise through `_G`'s object reaches every
  context's listeners `[L]`.
- **Consequence for the wrappers** (review change 5): wrapping the **shared metatable's `__index`** once covers every
  event object made later in any context (and in a context made later, as long as its `GameEvents` gets the same
  metatable `[I]`); the objects that exist at install are wrapped by one pass over `_G`'s `GameEvents` and every
  context's (through `Threads`). Each object's `Add` is replaced in that object (a raw field), not in a shared table.
- **`Count` without making an object** (the review's addition): yes. `rawget(GameEvents, name)` returns nil for a name
  never indexed in that table and makes nothing; `obj:Count()` on an existing object answers. **The listener count of
  a name is the sum over `_G`'s and every context's object** (`_G`'s alone is 0 where a context holds the listener);
  verify reads it that way (up to 152 tables per read; verify mode only) `[L]`.
- A context's environment lacks some base functions (`rawget` is nil there; the probe takes them from `G`) `[L]`.

### 13.2 The other facts

- **How the server learns listener presence** (12.2): from `handled` of the crossings it makes; `handled` reflects the
  contexts' listeners (13.1), so learning from `_G`'s `Count` would be wrong and is not used `[L for handled; C for the
  design]`. Verify's second signal is the summed `Count` (13.1).
- **The generation** is P15's own word (`kShmGen` in `Local\VPLuaRaise_<pid>`, `proxy/include/vp_luaraise.h`), not the
  D30 epoch mirror's; the epoch mirror is unchanged: a local answer still calls `NoteEngineCall` (12.1, D30) `[C]`.
- **Order of ui-thread registrations against core-thread raises**: every `Call*` enters the EXE's Lua section before it
  reads `GameEvents` (11.1 `[C; L]`); a registration is Lua running in the EXE, which runs inside that section on the
  thread that runs it `[I until the build's own check: every wrapped call checks that the holder word names its own
  thread (word kShmAddUnheld, expected 0); a non-zero count ends P15 for the session]`. With the gate (review change 1)
  a local answer is given only while the section is free or held by the raising thread's own channel, so the order
  argument is the review's section 2; 12.3's is superseded by it.
- **When the EXE's Lua state is made and freed against the server session** (review change 6): the knowledge base has
  `RegisterScriptLibraries` once at the boot and once per new front end, also on a load from inside a game, and a new
  `InGame` environment after such a load (`proxy-design.md` 24.2, B1 and B4 `[L, 2026-11-14]`); whether the main
  `lua_State` itself is made anew then was not established (no load from inside a game in this probe) `[open]`. The
  build does not depend on it: the proxy sets the generation to 0 at every `RegisterScriptLibraries` and at every
  `lua_close` the EXE makes (its import slot, when imported), and arms a new generation (above every earlier one) only
  after it found or installed its wrappers in the state at hand (a marker in the shared metatable). 12.4's "a
  generation that returns to 0 is impossible by construction" is replaced by this (review change 6).
- **The section without an EXE address** (change 7): learned as the first critical section a served `Call*` enters
  (4811's method), checked on every crossing raise; a different one sets the generation to 0 for the session (LOUD).
  The holder word is set from the section's own recursion count (`RTL_CRITICAL_SECTION::RecursionCount` = 1 after the
  outermost enter, before the outermost leave), so a hold that began before the section was known still ends with the
  word free; until then the word is `kHolderUnknown` and every raise crosses `[C; the offline two-thread test]`.

### 13.3 The build (option id 12; off by default)

- `[Performance] GameEventsNoListenerLocal` = 0 off (default), 1 on, 2 verify (`options/`, id 12; `docs/OPTIONS.md`);
  needs `GameEventsOneCrossing = 1` (else LOUD, off). Both sides read it from `VP64Options.ini`: the proxy decides at
  the first `RegisterScriptLibraries` (makes the words, writes the import slots, installs the wrappers), the server at
  the first raise. Lua bridge version 2: `kRt_LuaRegister` carries the proxy's pid; the server maps the words there.
- **Server** (`srv_remote_lua.cpp`): `vplr::Decide` reads the holder word, then the generation (interlocked loads), and
  answers a raise itself only when the generation is not 0, this thread learned (name, kind) with the same generation
  and `handled` false, the kind allows it (`CallHook`: the value its learning crossing returned for the same value in;
  `TestAll`, `TestAny`, `Accumulator(int)`: only if that crossing returned the value unchanged; `Accumulator(float)`:
  never, review change 3) and the holder is free or this thread's channel. A crossing learns with the generation read
  before it. A local answer counts `NoteEngineCall` and flushes pending database writes as a crossing would. Verify:
  every raise crosses; a would-be local one asks the proxy for the summed `Count` (`kFlagCount`), and a `handled`, a
  different value or a count above 0 is a **miss** (LOUD, run FAILED). Per-name log: two new columns `local` and
  `held` in `gameevents-raises.tsv`; report line `GameEvents P15 (ticket 4812)` with the decisions, the misses and the
  proxy's words (generation, bumps, wrapped calls not holding the section, installs, section mismatches, enters).
- **Proxy** (`proxy_lua.cpp`, namespace `p15`): the words, the import-slot hooks (Enter, TryEnter, Leave; `lua_close`),
  the wrappers (C closures with the original as upvalue: a protected call of the original, then the bump, then the same
  error raised again; the shared `__index` wrapped so every new object is wrapped before a script sees it), the install
  at `RegisterScriptLibraries` (else at a later `FuncThunk`, at most every 100 ms), the section's learning and check in
  `ServeLuaRaise`, the `Count` read for verify on an anchored thread under the section.

### 13.4 Tests and results

- **Offline** `[L, 2026-11-18 18:06-18:09 EST, workstation]`: `server/tests/luaraise/run_luaraise_test.py` 22,263 checks,
  0 failed, x64, x86 and MSVC `/fsanitize=address` (canary caught): P2's suite plus `TestP15Decide` (read order holder
  then generation, not installed, learn, stale generation after a bump, a listener, value by kind, the float
  accumulator, the hook's value, verify's miss rule, the holder hooks' outermost-only rule) and `TestP15TwoThreads` (a
  second thread holds a real critical section, nested, through the proxy's holder hooks: 1,000 decisions all `held`;
  after its outermost leave the raise is local; the same with a hold that began before the section was known).
  Options: 404/404 x86 and x64, ASan OK (`perf/evidence/0504/4812/options_tests.sh`).
- Not tested offline: the proxy's Lua wrappers (they need the EXE's Lua state; the live verify run covers them).
- **Build** `[L, 2026-11-18 18:16-18:20 EST]`: `perf/evidence/0504/4812/prep_build.sh` (fork source by `git archive
  c29442e8c`, the batch-4 x64 pin; own `build_all` deps, release) then `build2.sh` (server and proxy only, those deps):
  0 warnings; server.exe `6D826CB5...`, proxy `6CFAA1C7...`. The first build's proxy failed to compile (`proxy_lua.cpp`
  lacked `vp64_options.h`; fixed). Layout `C:/c5-64bit-work/4812/layout-p15` = the pin layout `x64-c29442e8-p58724e0`
  (DLL `2B56A1CD...`) with these two (`make_layout.sh`, `SHA256-4812.txt` there).
- `proxy/tools/test_proxy_rt.py` 199/199; `proxy/tools/hutest/build_hutest.py` PASS (55 checks, the real proxy and
  server, `kRt_LuaRegister` with version 2 and the pid) `[L, 18:22-18:27 EST]`.
- **Windowed identity, M 151-160, option 0 against 1** `[L, 2026-11-18 18:25-19:09 EST, workstation, c5h-e]`:
  `perf/evidence/0504/4812/idpair.sh c5h-e C:/c5-64bit-work/4812/layout-p15 10 F:/c5-64bit-work/4812/id1` (per arm
  `idarm.sh`: `TS0438-M-T150`, cp, vp, eui, plain autoplay 10 turns through `acceptance.py`, `SQLITE_LOGGING` on,
  `VP64Options.ini` `[Performance]` `GameEventsOneCrossing = 1` and `GameEventsNoListenerLocal` 0 or 1, both files
  backed up and restored by SHA-256; every step rc 0, no dialog box). **stats.db IDENTICAL** (15 tables, 32,308 rows,
  turns 150-160, `tools/d10/statscmp.py`). Raises by name: 74 pairs, handled 658 in both arms, value changed equal;
  equal on every (name, kind) but `GameCoreUpdateBegin` / `GameCoreUpdateEnd` (one per `ICvGame1::Update` call, wall
  time; 2,229 against 918), as in 4804's pair (9.2). Arm 1's report line: **161,600 local answers** (16,160 a turn),
  739 learning crossings, **697 crossed because the section was held** (about 70 a turn: plain autoplay, so the
  observer's `TechPanel.lua` holds of 0518 are in it; the review's estimate was about 12 a turn under O), 6 bumps,
  generation 7, 1 install (at the first `FuncThunk` of the load, 361 s after the boot: 4 GameEvents tables, no event
  object yet), **0 wrapped calls with the holder word not naming their thread**, 0 first-section mismatches, 0
  generation going back; the Lua section learned at the first raise (`PlayerCanTrain`). 7,767,639 section enters
  went through the wrapped slots in the whole session (load and 10 turns; their cost per frame is not measured yet).
- **Verify (option 2), the same series' third arm** `[L, 2026-11-18 19:10-19:28 EST]`: **162,696 would-be local
  answers, 0 misses** (`handled`, the value and the summed `Count` over `_G`'s and every context's object agreed with
  "no listener" every time; count unknown 0), 1,181 crossed held, 0 unheld wrapped calls, 0 section mismatches;
  stats.db IDENTICAL to arm 0 (15 tables, 32,308 rows); raises equal but the per-Update hooks (2,229 against 1,708).
  The driver's exit 1 is only its strict raise compare of those two hooks (`perf/evidence/0504/4812/id1/`).
- **Not done yet**: the negative control (a skipped bump: verify must report misses, option 1 must change stats.db),
  the late-registration control, the headless guard, memory after 10 turns, the slots' cost per frame, the H identity
  pair, an M D178 dev pair. The client H pair under O (13.6) showed less than half of the estimate, so these stay
  undone by the ticket's own rule until the lead decides.

### 13.6 The client H pair under O: about 0.24 s a turn, less than half of the estimate (the stop rule) `[L, oldpc]`

- **Job** `20261118-191129-3fa7` on oldpc's `tb1`, quiet and headed (alone; rc 0; instance restored, `provision.py
  verify` rc 0), 2026-11-18 19:11-20:19 EST; NVIDIA GeForce RTX 2080, driver 32.0.16.1088 (2026-07-21) (D175); window
  1920x1080, `WaitForVSync = 1` as found. `perf/evidence/0504/4812/tb_h15.py` (from 0522's `tb_ovr.py`):
  `TS0438-H-T300` (`308a96bb...`), cp, vp, eui, `Game.SetAIAutoPlay(7, 0)` and the observer's interface held on player 0
  (`OVR_SET 0` in every run) in **both** arms; `[Performance]` `GameEventsOneCrossing = 1`, `AiUpdateTickMs = 0`,
  `AiSkipVisualDrain = 1`, `AiDrainUnordered = 1` and `GameEventsNoListenerLocal` 0 (arm 0) or 1 (arm 1); layout
  `layout-p15` (DLL `2B56A1CD...`, server `6D826CB5...`, proxy `6CFAA1C7...`); order `W,0,1,1,0,0,1,1,0`, 4 counted
  runs per arm, turns 301-305 by the turn clock (player 0's `PlayerDoTurn`, `os.clock()`). Grade B (4 per arm, D178
  dev series). `perf/evidence/0504/4812/h15-3fa7/` (`stats.txt` by `h15_stats.py` through 0522's `ovr_stats.py`,
  `tb_h15.json`, `gpu.txt`).
- **Result**: arm 0 24.911 s a turn (sd 0.187), arm 1 24.672 s (sd 0.089); **on - off = -0.239 s a turn (-0.96 %),
  Welch 95 % interval -0.518..+0.041, p 0.078** (Mann-Whitney exact p 0.114); the series could detect 0.376 s. Arm 1
  is faster in every one of the five turns (-0.05 to -0.32 s).
- **Against the claim**: the review's estimate is about 1.1 s an H turn (the wait-free part, 160,126 raises x 6.9 us,
  11.5 `[I]`); the stop rule of the ticket is "less than half" (0.55 s). Even the interval's far end (0.52 s) is below
  it. **So the ticket stops here and reports** (no M timing, no further controls) until lead-0504 / the MC decide.
- Why it is smaller `[I, not measured]`: 6.9 us a raise came from the workstation under load (11.3; IPC 5 us of it);
  on oldpc's quiet machine a crossing is likely cheaper, and on H most raises are one name (`CanLoadAt`) whose share of
  local answers against crossings held by the main thread's Lua was **not recorded**: the job kept no server report
  (the H runs' `server-report.txt` and `vpproxy.log` were not fetched: `set-proxy --original --runs-to` left no
  `vp64-runs` in the output), so the local-answer count of these runs is unknown; that P15 engaged rests on the option
  written per run (`tb_h15.json`) and arm 1 being faster in every turn. One H run with the report kept would say how
  many of the about 160,000 raises a turn were answered locally and how many crossed held.

### 13.5 Statics and globals added (two-thread hazard)

| Name | Where | Live across a raise? | Reachable from a ui-thread DLL call? | Why safe |
|---|---|---|---|---|
| `s_iP15` (mode) | server | read-only after the first read | yes | idempotent start-up read (CAS from -1) |
| `s_pP15` (the mapped words) | server | yes | yes | mapped once at `kRt_LuaRegister`, before any raise uses it; the words are read with interlocked loads only |
| `t_p15` (learned table), `t_p15LastGen` | server, `thread_local` | yes | the ui thread has its own | not shared; holds only answers the EXE gave |
| `s_lP15*` counters, `s_szP15NotInstalled` | server | no | yes | interlocked; the text written once behind a CAS |
| `kShmGen`, `kShmHolder`, the diagnostic words | shared memory | yes | written by the proxy (any EXE thread in Lua; the hooks), read by both service threads | aligned 32-bit words, interlocked stores and loads; order: review section 2 |
| `g_p15Mode`, `g_p15W`, the real CS and `lua_close` functions | proxy | read-only after the first `RegisterScriptLibraries` | the hooks run on every EXE thread | written once before the slots are written |
| `g_p15LuaCs` | proxy | yes | read by the hooks on every thread | written once by CAS; compared only |
| `g_p15GenMax`, `g_p15Dead` | proxy | yes | yes | interlocked; the generation is CAS-bumped, so it never goes back and 0 stays 0 |
| `g_p15Installed`, `g_p15T`, `g_p15TRef`, `g_p15LastTry` | proxy | yes | written inside the EXE's Lua section by the thread holding it (install, `lua_close`) | the section orders them; `g_p15LastTry` is a racy rate limit (harmless) |
| `g_p15Enters` | proxy | - | every hook | a racy diagnostic count, never read for a decision |
| `t_p15InRaise`, `t_p15FirstCs` | proxy, per thread | per raise | - | thread-local, saved and restored around nested raises |
| the wrappers' originals | upvalues of C closures in the EXE's Lua state | yes | only Lua running in the EXE | the EXE's Lua heap, as the UI bridge's thunks (D4) |

None is DLL game state; the DLL is not changed.
