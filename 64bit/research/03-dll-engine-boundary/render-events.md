# Render messages from the DLL to the server (ticket 4932, epic 0454 X2)

Built 2026-11-19 by W2 of team-0454 from the design `docs/extras/flags-and-messages-design.md` section 2, accepted by
the MC at C1 (#7273, 21:43 EST; Q7: the `VP64` prefix stays). Marks: `[C]` read in code, `[L]` run here, `[I]` inferred.

## 1. What it is

The DLL built with the define `VP64_RENDER_EVENTS` tells the server's renderer about unit states that the EXE's
interface never carries. Two kinds exist, 1000 and 1001, in the feed's reserved range 1000-1999
(`server/src/feed/srv_feed.h`):

| Kind | Name | plot | a[0] | a[1] | a[2] | a[3] |
|---|---|---|---|---|---|---|
| 1000 | `kUnitActivityChanged` / `VP64_RENDER_EVENT_UNIT_ACTIVITY` | the unit's plot index, -1 without one | owner | unit id | new `ActivityTypes` (0 awake, 1 hold, 2 sleep, 3 heal, 4 sentry, 5 intercept, 6 mission) | the old one; equal to a[2] when sent at the unit's setup for display (a load or a creation) |
| 1001 | `kUnitCarrierChanged` / `VP64_RENDER_EVENT_UNIT_CARRIER` | as 1000 | owner | unit id | the transport's unit id, -1 = no longer cargo | the transport's owner, -1 = none |

The event is the feed's 32-byte `Event`: `seq` from `SrvFeed::Push`, `flags` 0, `turn` the game turn at the send (set
by `SrvRemoteFeed::AtLeaveUpdate` in remote mode, -1 before the first one). The events are ordered with every stock
event by `seq` and recorded into scene bundles as they are (feed.bin's DRAIN records hold `Event[nEvents]`; the reader
does not look at kinds) [C]. The unit scene skips them (`srv_units_core.cpp` `IsUnitKind`), so they do not end ticket
0502's load-setup run; a consumer that makes them unit kinds adds them to `LoadSetupGoesOn` [C].

## 2. The DLL side (fork branch `x64-render-events`)

- Branch `x64-render-events` = `x64-batch5` 1fd51353e + **one commit `addd57b8bf3f`** "Add render messages for the
  out-of-process build" (message `docs/extras/evidence/4932/msg-fork.txt`, scrubbed: no project ids). Made in an own
  clone `C:/c5-64bit-work/0454/4932/fork` (`git clone --shared` of the long-lived clone, `origin` the fork on GitHub);
  the edits by `docs/extras/evidence/4932/scripts/hooks.py` (anchors must match once). It reaches the pins with the next
  x64 pin batch by `cherry-pick -x`, whose ticket adds the `port` row to `tools/release/series/carry.tsv`; `i0-ship`
  takes nothing (MC #7273).
- New `CvGameCoreDLL_Expansion2/VP64RenderEvents.h`: the version (1), the kinds with the meaning of each value, and
  under the define the function type, `extern VP64RenderEventFn g_pfnVP64RenderEvent`, `VP64_RENDER_EVENTS_ACTIVE()` and
  `VP64_RENDER_EVENT(kind, plot, a0, a1, a2, a3)`, which calls through the pointer when it is set. Without the define
  both macros expand to nothing and evaluate no argument [L: stand-in `standin_none`].
- New `VP64RenderEvents.inl`, included at the very end of `CvGameCoreDLL.cpp` inside `#if defined(VP64_RENDER_EVENTS)`:
  the pointer, a static initialiser that does `GetProcAddress(GetModuleHandleW(NULL), "VP64RenderEvent")` once while
  the DLL loads, and the export `DllGetRenderEventsVersion` (not in the shared `.def`).
- Send sites, all in `CvUnit.cpp`, each in an `#if` block, no `#line` (the 0573 practice, MC #6589):
  1. `SetActivityType`, right after `m_eActivityType = eNewValue;` (only reached when the value changes);
  2. `setTransportUnit`, once after both arms of the change (after the cargo's and the transport's own activity sends);
  3. `setupGraphical`, at its end, when `VP64_RENDER_EVENTS_ACTIVE()`: the activity (old = new) unless awake, the
     carrier when cargo. Not reached without graphics (its early return), so never in direct mode;
  4. `DoUpgradeTo`'s direct copy `pNewUnit->m_eActivityType = m_eActivityType;` (after the new unit's `setupGraphical`):
     a kind 1000 when the value changed.
  Arguments are members and plain getters (`plot()`, `GetPlotIndex()`, `getOwner()`, `GetID()`, the `m_transportUnit`
  IDInfo, never `getTransportUnit()`). Writes of the two members at the pin [C, grep at 1fd51353e]: `CvUnit.cpp` 1587 and
  1628 (`reset`, a new unit: awake, not cargo, its `setupGraphical` follows), 14442 (site 4), 26007 and 26023 (site 2),
  29906 (site 1). Loading a save fills them by serialisation; site 3 then sends the state.
- The define is set by `tools/x64-dll/build.py` `cl_args` for a source that has the header (as hidden embarks), so pins
  without it keep their command line; `--no-render-events` (build.py, build_all.py, server/build_server.py) leaves it
  out for the identity test; `build-summary.json` / `build_all.json` record `render_events` on, off or absent.

## 3. The server side

`server/src/feed/srv_render_events.h/.cpp`:
- `extern "C" __declspec(dllexport) void __cdecl VP64RenderEvent(int kind, int plot, int a0, int a1, int a2, int a3)`:
  counts every call (interlocked; also while the feed is off), refuses a kind outside 1000-1999 (counted, dropped:
  so a stock kind can never be forged), pushes the rest with `SrvFeed::Push` (which returns at once while the feed is
  off). Never calls the DLL.
- The numbers are checked at compile time against the DLL source's header when the server is built against one
  (`__has_include("VP64RenderEvents.h")` + `static_assert`, the `srv_dll_interface.h` pattern); without the header the
  server compiles and nothing calls the export.
- `SrvRenderEvents::Apply(hDll)` after the DLL's load (`srv_main.cpp` direct mode, `srv_remote_main.cpp` kRt_Boot
  after the context): one line `render-events: the DLL sends render messages, version 1 ...`, or `... has no
  DllGetRenderEventsVersion export (built without VP64_RENDER_EVENTS) ...`, or LOUD on another version. `DllSends()`
  tells an extra whether it can rely on the messages (E6). `Summary()` at shutdown: calls per kind, setup sends,
  unknown and refused kinds. Direct mode writes `render-events-per-turn.tsv` (turn, kind 1000, kind 1001, setup, calls).
- The interface version (`VP_OUT_OF_PROCESS_INTERFACE_VERSION`) is not raised: a DLL with the define under a server
  without the export, or a server with it and a DLL without the define, both run as before.

## 4. Tests and evidence

Everything under `docs/extras/evidence/4932/` (scripts in `scripts/`, run outputs in `C:/c5-64bit-work/0454/4932/`).
Builds [L, 2026-11-19 21:54-22:02 EST, workstation, shared with other teams' jobs]: `s04_build.sh`: the DLL of
`addd57b8b` in config `fast` with the define (`dll-on`, sha256 `6eaa9d4a...68b8cea9`) and with `--no-render-events`
(`dll-off`, `365123d4...d8002215`), both 23,506,944 bytes; narrowing gate 0 new (794 sites, baseline 798) in both;
export tables: `DllGetGameContext`, `DllGetOutOfProcessInterfaceVersion` and, in `dll-on` only,
`DllGetRenderEventsVersion`; server.exe (release, against the branch: the static_asserts held) `7b7fb431...caaf2260`,
which exports `VP64RenderEvent` beside the 22 FireWorks names.

### 4.1 Unit tests [L, 2026-11-19 22:09 EST]

`python -I server/tests/feed/run_render_events_test.py --out DIR --dll-src <x64-render-events checkout> --asan --bench`
(through `quietlock.py run`): 39/39 checks of `test_render_events.cpp` (clang-cl /W4 /WX) and 38/38 again under MSVC
`/fsanitize=address` (canary caught, no ASan report); the runner's parts: stand-ins built from the fork's real header
and `.inl` (on: the static initialiser found the test EXE's export, the sends arrive in the feed in order with their
values; none: no version export, the empty macro evaluates no argument; v2: refused LOUD), a host EXE without the
export (pointer NULL, sends do nothing), two negative compiles (carrier kind 1002, version 2: each fails on its
static_assert text), a compile without the header (a pin before the branch: builds, `SourceHasHeader` 0), and the
injected failing check that must fail the binary. Refused kinds checked: 999, 2000, -1, 66536 (1000 as a `uint16_t`),
0 and a stock kind 11. Four threads x 100,000 sends with a draining reader: every send counted, drained + dropped =
sent, `seq` increasing. Output: `docs/extras/evidence/4932/unit-tests.txt`.
`server/tests/feed/test_render_events_define.py` (pytest, 2026-11-19 22:19): the define only for a source with the header,
the one flag the only change of the compile line, `--no-render-events` drops it, both feature defines once with hidden
embarks, the switch passed through by `build_all.py` and `build_server.py`; with `tools/x64-dll/tests` and
`tools/harness/tests/test_safety_static.py`: 171 passed.

### 4.2 32-bit unharmed [L, 2026-11-19 22:03 EST]

`s05_pp32.sh` (4719's `pp32.py`: `build_vp_clang.py`'s Release flags under the VS 2008 vars, `/EP`, one worktree so
`__FILE__` is the same) at 1fd51353e and at `addd57b8b`: `CvGameCoreDLL.cpp` byte-identical (sha256 `a2500d75...`);
`CvUnit.cpp` the same 147,942 non-blank lines, of which the only differences are 1,172 integer literals moved by +3,
+15, +23, +27, +31 (`__LINE__` in the assert macros below each inserted block; `pp32cmp.py`), and no line names the new
symbols. So the 32-bit compiler sees the same code with other line numbers in its assert strings (the 0573 situation,
accepted by the MC, #6589). `docs/extras/evidence/4932/pp32.txt`.

## 5. Results: E1, the define changes nothing in the game [L, 2026-11-19 22:02-22:17 EST, workstation]

Headless direct mode (D164 like with like), `bin-on` vs `bin-off` (the same server.exe, only the DLL differs), the
release descriptor's toolkit game folder (never the Steam install), no mods, the DLL's AI logging off in both arms.
`docs/extras/evidence/4932/e1.txt` has every line.

| Run | statscmp on vs off | savecmp on vs off |
|---|---|---|
| Tiny new game 0-100 (`s06_e1.sh`) | IDENTICAL, 15 tables, 30,569 rows | (not the check: see the control) |
| Standard new game 0-200 | IDENTICAL, 15 tables, 359,261 rows | (not the check) |
| Tiny loaded at T100, saved at once, played to 130 (`s08_e1_saves.sh`) | IDENTICAL `--from-turn 100`, 23,812 rows | IDENTICAL at the load and at T130 |
| Standard loaded at T200, saved at once, played to 220 (`s10_e1_standard_load.sh`) | IDENTICAL `--from-turn 200`, 14 tables, 99,605 rows | IDENTICAL at the load and at T220 |

**Two new games are never byte-equal saves, with one DLL as with two** [L]: the control (`s08`) played the Tiny game
twice with each DLL; off vs off2 differ in 526 runs (1,261 B), on vs on2 in 656 runs (1,635 B), the same kind of runs
as on vs off (513 runs): the game's two GUIDs (not drawn from the seeded RNG; `statscmp` skips `uuid_hex` for the same
reason) and a block near the end of the body that holds per-process bytes. So the save check is the D10 method
(`run_d10.py` parts 1 and 3): both arms load one input save and write it at once and after more turns.

**R4, events per turn** (`render-events-per-turn.tsv` of the on arm; kind 1000; kind 1001: 0, no transport was used in
these games): Tiny 0-99 median 309 per turn (max 440, turn 91); Standard 0-199 median 927.5 (mean 898), the last 20
turns median 1,510 (max 1,829, turn 196). The loaded Tiny game sent 100 kind-1000 events at the load (site 3: the units'
activity at setup; direct mode with `--load` sets the graphics up, a new game sends none because new units are awake);
the loaded Standard game 316 at its load and 33,326 in 20 turns (1,650 a turn).

**R5, the cost of a send** [L, n = 1 run of 2,000,000 sends, busy machine: an indication, not a PERFORMANCE.md
figure]: 62.6 ns through the DLL's macro with the feed on and a reader draining every millisecond, 14.2 ns with the
feed off (direct mode, and remote mode without `render`). At Standard's late 1,829 sends per turn that is about 0.11 ms
per turn against 1.06 s per turn of autoplay (about 0.01 %). The E1 runs' autoplay times (on 212.4 s, off 211.1 s for
200 turns; Tiny 15.5 s and 15.6 s) are one run each on a shared machine and show no difference beyond their noise.

## 5a. The windowed pair: remote mode, the EXE, a scene bundle [L, 2026-11-19 23:00-23:39 EST, c5h-b]

c5h-b (old build, pool claim w2-4932 for 0454), the product path with this branch's server.exe, proxy and composite
(`s11_build_product.sh`), `ServerRenderer = 1`, a copy of the huge save `C5H-VP8P-HUGE_0350`, 2 AI turns (350, 351)
per run, the scene recorder from the first published frame (`w4932_windowed.py` over `run_3308.py`, `s12_windowed.sh`).
Four runs: define on three times (r1, r2, r3), off once. Every restore by hash, frontend restore and provision verify
returned 0. The save copy was removed. Evidence: `docs/extras/evidence/4932/windowed.txt`, `windowed-saves.txt`.

- **The kinds in a bundle:** run r3's bundle (format 9, 194 members) holds **471 kind-1000 events in feed.bin**, in
  `seq` order with the stock events of the load (9,808 plot states, 124 city creations, ...). All 471 are site 3's
  setup sends (old == new): hold 319, mission 150, sentry 2. They carry turn -1, because the first `AtLeaveUpdate` comes
  after the load. The off arm's bundle has the same stock kinds and counts and no kind 1000.
  **Kind 1001: 0.** No unit of that save is cargo at its load, and none was loaded in the two AI turns, so kind 1001 is
  shown only by the unit tests.
- **The server's counts are the same in every define-on run:** 5,326 kind-1000 calls, 475 of them at setup.
- **The game is the same** (D164: windowed against windowed). Each run's four autosaves (0350, Post_0350, 0351,
  Post_0351) were compared by `savecmp` and `slicediff.py`, for on vs off and for the control on r1 vs on r2.
  - Every comparison differs only in integers that are all offset by one constant per save: the turn-slice counter
    `CvGame::m_iTurnSlice` (body offset 86, the 8th int of `CvGame::Serialize`) and the copies of it the game stores
    per player and per unit. The offsets were 20, 57, 11 and 39 ticks on vs off, and 9 and 18 ticks on vs on.
  - The counter counts update calls, which the EXE paces by frames, so two windowed runs of ONE DLL differ in it in
    the same way. Nothing else differs.
  - The DLL's statistics database is not written on this product path, so there is no windowed `statscmp`.
- **Recorder lesson (rule B tooling):** started at frame 1 of a load, the recorder's span ends at the first redraw of a
  frame (13 to 101 frames into the load). With `dump=first,last` (r1) or `every:20` (r2) fewer than two frames were
  dumped, and the bundle was left without members (`incomplete: 1 dumped frame(s)`). `dump=all` (r3) gave a whole
  bundle.
- **The fast configuration in remote mode:** every run's report ends `result: FAILED: database: the DLL imports
  database members the write recording does not cover`, because of `?Bind@Results@Database@@QEAA_NHM@Z`. The pin's
  release (LTO) DLL does not import it; both `fast` DLLs do (`llvm-readobj --coff-imports`) [C]. Whole-program
  optimisation removes the unused caller, so a `fast` DLL in remote mode always gets this LOUD verdict. The game played
  on regardless, with the define and without it. *Corrected 2026-11-25: since ticket 0752 the server hooks this import
  (`upstream-fix-batch-2.md`, "Fixed by ticket 0752"); a fast or debug DLL no longer gets the verdict.*

**Kind 1001 live (ticket 4939, MC #7393)** [L, 2026-11-20 00:10-00:30 EST, c5h-b, the same define-on layout]. Evidence
and scripts: `docs/extras/evidence/4939/` (`result.txt`).
- **Run A:** load the huge save, then run `stage_cargo.lua` through the Lua channel.
  - It made a carrier (0/17542) on the empty sea plot (106,27) next to Pi-Ramesses (105,27), fighter f1 (17543) in the
    city and fighter f2 (17544) on the carrier's plot. f2 was loaded at its creation (transport 0/17542, carrier cargo 1).
  - Neither fighter could rebase in that turn (`CanRebaseAt` false: a new unit has no moves).
  - The server counted **2 kind-1001 calls**. The counters do not keep values, so the two calls are not told apart [I].
  - Then the game was saved by the harness checklist's `save_game` (main thread).
- **Run B:** that save loaded with the recorder over the load (`dump=all`; bundle format 9, 19 frames, 71 members).
  - feed.bin holds **one kind-1001 event: seq 207, plot 3562 (= (106,27) on the 128-wide map), owner 0, unit 17544,
    transport id 17542, owner 0.** That is site 3's setup send for the cargo fighter, with the right unit and carrier.
  - Then `unload_cargo.lua` moved f2 to its city by `SetXY` (`setXY` drops a transport that is not on the new plot):
    transport before 0/17542, after -1/-1.
  - The server counted 2 kind-1001 calls in run B: the setup send and the live unload. The unload came after the span
    had ended, so it is counted but not in the bundle.
- Every restore by hash, frontend restore and provision verify returned 0. The staged save was moved out of the
  instance into the work folder.

## 6. Not verified

- Kind 1001 from a live change in a recorded bundle: the live load (run A) and unload (run B) were counted by the
  server, but no recorder span reached them (the spans end inside a load); the recorded kind-1001 is the setup send.
  An old transport is not carried by the kind (design R10).
- Kind 1000 from site 1 in a recorded bundle: the bundles' spans end inside the load, so they hold site 3's sends only;
  site 1's sends were counted by the server (4,851 per define-on run in the two AI turns) but no span reached them.
- The release (LTO) build of the define-on DLL: only `fast` was built (same code paths, D107/0159).
- The bundle was not run through the offline checker or the player (`recorder_test.exe --check`, `sceneshot`).
