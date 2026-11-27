# IPC design and measurements

What a cross-process call costs between a 32-bit client (the proxy DLL in the EXE) and a 64-bit server, and what the
measured boundary traffic would cost over it.

## 1. The microbenchmark (2026-11-08 23:17-23:32)

| Item | Value |
|---|---|
| Machine | Intel Core i9-13900KS: 8 P-cores with HT (logical 0-15), 16 E-cores (logical 16-31); 24 cores / 32 threads |
| OS / power plan | Windows 11 Home 10.0.26100, "Bitsum Highest Performance" |
| TSC | invariant, 3187.2 ticks/us, calibrated against QPC |
| Game | Civ V not running (checked before each run) |
| Bitness | verified at run time: client `sizeof(void*)` = 4, `server.exe` = 8, `server32.exe` = 4, written into the shared section by the server; section layout size checked equal |
| Transport | named section `Local\IpcBench` (`CreateFileMapping`), request and response each on their own 64-byte line; server returns a0+1 |
| Timing | `rdtsc` (lfence-fenced) around write-request -> publish -> wait -> read/verify |
| Volume | 10,000-call warm-up, then 1,000,000 calls (batches: 10,000 x N=100, 1,000 x N=1000); all results verified, 0 failures; two full passes |
| Pinned | client on logical 2, server on logical 4 (two different P-cores) |

Modes:

- **spin**: both sides busy-wait on the flag. Keeps one core busy on each side.
- **event**: one `SetEvent` + one blocking `WaitForSingleObject` per direction, every call.
- **hybrid**: spin 2000 x `_mm_pause`, then flag + event (Dekker handshake with `InterlockedExchange`).
- **batch**: N requests published under one spin flag or one event.

### Results (us, `pass1 / pass2`)

| Config | N | median | p90 | p99 | p99.9 | max | mean (rdtsc) | per-call median | blocked waits c/s |
|---|---|---|---|---|---|---|---|---|---|
| fnptr (same process, reference) | 1 | 0.040 / 0.040 | 0.041 / 0.041 | 0.041 / 0.041 | 0.044 / 0.048 | 190 / 156 | 0.038 / 0.039 | | 0/0 |
| inproc_spin_pinned | 1 | 0.197 / 0.200 | 0.237 / 0.203 | 0.277 / 0.281 | 0.740 / 0.489 | 5757 / 849 | 0.232 / 0.226 | | 0/0 |
| inproc_spin_unpinned | 1 | 0.221 / 0.281 | 0.306 / 0.311 | 0.332 / 0.331 | 0.450 / 0.460 | 193 / 302 | 0.251 / 0.272 | | 0/0 |
| spin_both_pinned | 1 | 0.204 / 0.183 | 0.208 / 0.186 | 0.286 / 0.245 | 0.649 / 0.607 | 1518 / 717 | 0.238 / 0.203 | | 0/0 |
| spin_server_pinned | 1 | 0.226 / 0.228 | 0.233 / 0.246 | 0.297 / 0.307 | 0.567 / 0.554 | 390 / 1448 | 0.227 / 0.260 | | 0/0 |
| spin_unpinned | 1 | 0.209 / 0.227 | 0.284 / 0.312 | 0.311 / 0.339 | 0.443 / 0.456 | 213 / 195 | 0.234 / 0.255 | | 0/0 |
| spin_smt_sibling (logical 2/3, shared L1) | 1 | 0.092 / 0.092 | 0.094 / 0.094 | 0.096 / 0.097 | 0.157 / 0.157 | 1300 / 753 | 0.105 / 0.104 | | 0/0 |
| event_unpinned | 1 | 13.162 / 13.242 | 16.940 / 16.570 | 36.396 / 34.792 | 186.408 / 184.019 | 473 / 666 | 13.665 / 13.628 | | 0/0 |
| event_pinned | 1 | 12.765 / 12.820 | 29.338 / 24.645 | 607.856 / 548.377 | 3690.008 / 3553.717 | 26619 / 31363 | 43.953 / 40.207 | | 0/0 |
| hybrid_unpinned | 1 | 0.235 / 0.211 | 0.304 / 0.288 | 0.323 / 0.327 | 0.510 / 0.552 | 217 / 215 | 0.255 / 0.232 | | 7/21, 8/12 |
| hybrid_pinned | 1 | 0.213 / 0.215 | 0.218 / 0.220 | 0.315 / 0.299 | 0.610 / 0.628 | 547 / 1083 | 0.234 / 0.237 | | 3/40, 8/42 |
| hybrid_gap200 (200 us busy gap, 100,000 calls) | 1 | 7.426 / 7.344 | 12.676 / 12.614 | 135.921 / 138.496 | 201.439 / 199.874 | 574 / 735 | 11.687 / 11.700 | | 1666/100887, 1724/100897 |
| batch100_spin | 100 | 0.719 / 0.700 | 0.805 / 0.762 | 1.010 / 0.931 | 6.393 / 2.877 | 66 / 18 | 0.745 / 0.710 | 0.0072 / 0.0070 | 0/0 |
| batch100_event | 100 | 9.668 / 9.591 | 16.510 / 16.460 | 33.160 / 32.529 | 190.231 / 183.699 | 224 / 220 | 11.699 / 11.721 | 0.0967 / 0.0959 | 0/0 |
| batch1000_spin | 1000 | 4.094 / 4.189 | 4.280 / 4.678 | 5.391 / 8.612 | 12.043 / 47.051 | 31 / 152 | 4.200 / 4.595 | 0.0041 / 0.0042 | 0/0 |
| batch1000_event | 1000 | 10.945 / 11.246 | 17.437 / 19.355 | 43.923 / 33.416 | 134.971 / 166.989 | 137 / 185 | 13.287 / 13.585 | 0.0109 / 0.0112 | 0/0 |
| batch100_spin_pinned | 100 | 0.816 / 0.787 | 0.923 / 0.868 | 1.005 / 0.970 | 19.688 / 20.461 | 189 / 210 | 0.897 / 0.888 | 0.0082 / 0.0079 | 0/0 |
| batch1000_spin_pinned | 1000 | 4.046 / 4.146 | 4.693 / 4.355 | 5.877 / 4.773 | 8.221 / 9.002 | 14 / 13 | 4.171 / 4.191 | 0.0040 / 0.0042 | 0/0 |
| srv32_spin_unpinned (32-bit server) | 1 | 0.233 / 0.232 | 0.312 / 0.304 | 0.335 / 0.341 | 0.502 / 0.505 | 255 / 216 | 0.261 / 0.256 | | 0/0 |
| srv32_event_unpinned (32-bit server) | 1 | 13.213 / 13.236 | 16.946 / 16.863 | 35.670 / 35.236 | 187.012 / 185.139 | 597 / 720 | 13.893 / 13.948 | | 0/0 |

fnptr without a per-call timer: 2.59 / 2.64 ns per call (the fnptr row above is dominated by the rdtsc pair).

Summary as REPORT.md states it:

| Mode | Median | p99 | p99.9 |
|---|---|---|---|
| direct function pointer, same process | 0.04 | - | - |
| spin, unpinned | 0.21-0.23 | 0.31-0.34 | 0.45 |
| spin, pinned | 0.18-0.20 | 0.25-0.29 | 0.61-0.65 |
| spin, hyperthread siblings | 0.092 | 0.097 | 0.157 |
| event, unpinned | 13.2 | 35-36 | 185 |
| hybrid, back-to-back | 0.21-0.24 | 0.32 | 0.51-0.55 |
| hybrid, 200 us idle gaps | 7.4 | 136-138 | 200 |
| batch of 100, spin flag | 0.70-0.72 per batch (7 ns per call) | 0.93-1.01 | - |
| batch of 1000, spin flag | 4.1-4.2 per batch (4 ns per call) | 5.4-8.6 | - |
| batch of 100 / 1000, event | 9.6 / 11.0 per batch (96 / 11 ns per call) | 33 / 34-44 | - |

Observations from the notes:

- Server bitness makes no measurable difference (srv32 rows).
- Hybrid back-to-back never sleeps, so it equals spin; with 200 us gaps the server blocks on every call and the median
  (7.4 us) is one event wake-up.
- Pinned event mode has very bad tails (p99 ~0.6 ms, mean 40+ us): a pinned thread waits for its specific core. Unpinned
  is the realistic event number.
- Spinning keeps one core busy on each side.

## 2. Projection onto the measured traffic

Modes (REPORT.md section 5):

- **(i)** every crossing synchronous.
- **(ii)** notifications, commands and `Destroy` batched; everything that returns a value synchronous; lock plumbing removed.
- **(ii')** as (ii) with ID handles resolved on the client, so lifetime and static-handle calls disappear.
- **(iii)** everything batched into one flush per frame (only works with a client-side read cache).

### Per frame, UI thread

| Scenario | fps | (i) calls | (ii) calls | (ii') calls | (i) spin / event | (ii) spin / event | (ii') spin / event | (iii) spin / event |
|---|---|---|---|---|---|---|---|---|
| a idle | 155.8 | 1,286 | 507 | 205 | 270 us / 17.0 ms | 107 us / 6.7 ms | 43 us / 2.70 ms | 10.0 us / 22.5 us |
| b pan/zoom | 153.7 | 1,930 | 975 | 557 | 405 us / 25.5 ms | 205 us / 12.9 ms | 117 us / 7.36 ms | 14.6 us / 28.9 us |
| c city view | 155.3 | 1,254 | 491 | 193 | 263 us / 16.6 ms | 103 us / 6.5 ms | 41 us / 2.55 ms | 9.7 us / 22.1 us |
| d tech tree | 140.6 | 1,297 | 516 | 210 | 272 us / 17.1 ms | 108 us / 6.8 ms | 44 us / 2.78 ms | 10.1 us / 22.6 us |
| e1 tooltips | 155.8 | 1,289 | 510 | 206 | 271 us / 17.0 ms | 107 us / 6.7 ms | 43 us / 2.72 ms | 10.0 us / 22.5 us |
| e2 diplo overview | 155.9 | 1,286 | 508 | 204 | 270 us / 17.0 ms | 107 us / 6.7 ms | 43 us / 2.70 ms | 10.0 us / 22.5 us |
| e3 econ overview | 155.2 | 1,286 | 508 | 204 | 270 us / 17.0 ms | 107 us / 6.7 ms | 43 us / 2.70 ms | 10.0 us / 22.5 us |

Worst 16.7 ms slot, naive (mode i):

| Scenario | slot median | p95 | p99 | max | p99 spin / event | max spin / event |
|---|---|---|---|---|---|---|
| a idle | 3,489 | 3,492 | 12,242 | 12,293 | 2.57 ms / 161.6 ms | 2.58 ms / 162.3 ms |
| b pan/zoom | 3,489 | 12,288 | 14,864 | 23,738 | 3.12 ms / 196.2 ms | 4.98 ms / 313.3 ms |

REPORT.md rounds this to "worst slot, naive (p99 12-15k calls, max 24k): 2.6-5.0 ms spin, 160-310 ms event". Against a
6.4 ms frame at ~155 fps, naive spin is 4-6% of the frame.

### Per AI turn

| Mode | f1 (350->351) | f2 (351->352) |
|---|---|---|
| (i) naive, both threads: calls | 4.8 M (main 3,330,191 + core 1,490,171) | 4.2 M (main 2,761,047 + core 1,392,003) |
| (i) naive, both threads: spin / event | 1.01 s / 63.6 s | 0.87 s / 54.8 s |
| (i) naive, core thread only: spin / event | 0.31 s / 19.7 s | 0.29 s / 18.4 s |
| (ii) UI thread synchronous (Lua reads + engine getters + nested reads) | 1,660,792 calls: 0.35 s / 21.9 s | 1,511,110 calls: 0.32 s / 19.9 s |
| (ii) core thread, mod Lua inside the server | 79,875 calls: 0.017 s / 1.05 s | 84,105 calls: 0.018 s / 1.11 s |
| (ii) core thread, hooks remote | 117,568 calls: 0.025 s / 1.55 s | 114,767 calls: 0.024 s / 1.51 s |
| (ii) fire-and-forget, batched | 25,175 notifications + 4,900 commands: < 1 ms | 36,775 notifications + 4,627 commands: < 1 ms |
| (ii) corrected 2026-11-10 (section 7.2): batched only if the handler makes no synchronous call back | flag-only notifications 15,602 batched (< 1 ms) + up to 9,573 synchronous (up to ~2-3 ms spin) | 27,586 batched + up to 9,189 synchronous |
| core thread busy (for scale) | 25.15 s | 26.81 s |

## 3. Why kernel events are ruled out

- Event round trip 13.2 us median vs 0.21 us spin: ~60x.
- Naive event IPC costs 17-25 ms per frame, i.e. **below 60 fps** on traffic that runs at ~155 fps in-process.
- Even with only reads synchronous (mode ii) events cost 6.7-12.9 ms per frame; with client-resolved handles (ii')
  still 2.7-7.4 ms per frame (REPORT.md headline: "2.7-13 ms per frame").
- A naive AI turn would add 55-64 s of event latency to 25-27 s of compute.
- Conclusion in every source: **spin, or hybrid spin-then-block, never plain events**. The hybrid only helps when calls
  are back-to-back; across a 200 us idle gap it degrades to one event wake (7.4 us median, 136-138 us p99), so the spin
  window length matters (open question below).

## 4. Batching

Measured: one spin flag over 1,000 requests costs 4 ns per call; over 100 requests 7 ns per call. Mode (iii), one
flush per frame, costs 10-15 us per frame even with events, **but only works with a client-side read cache**.

Fire-and-forget traffic is already batchable today: 25-37k void notifications per turn (`setDirty` ~24k) and ~5k
commands. REPORT.md and the study treat this as solved ("< 1 ms" per turn). **Correction 2026-11-10 (epic 1200,
section 7.2):** only the notifications and commands whose handler makes no synchronous call back across the
boundary can be batched. The flag-only `setDirty` and `SetSpecificCityInfoDirty` (15.6k / 27.6k per turn) qualify;
the other 9.2-9.6k notifications per turn may have to be synchronous until a measurement says which ones re-enter.
In time this costs little (~2-3 ms per turn spinning); it matters for the protocol. Previously: "25-37k
notifications ... solved, < 1 ms".

**Drain-interval idea (drain the notification buffer at 3-5x the frame interval):** this appears in the project brief
for this section; **it is not in any of the sources read** and has not been measured. Nothing in the sources says how
stale a batched notification may be before the UI shows wrong state; the closest data are the headless contract's net
loopback rules (`Response*` messages must be delivered after the current `Update` returns and before the next, with
measured delays of 0-2 ms for player options and 25-100 ms for minor-civ pledge/bully responses).

## 5. What the proxy can answer locally

As far as the sources go (REPORT.md bottlenecks, study section 5, REVIEW.md sections 2 and 5):

| Traffic | Local rule in the sources | Share it removes |
|---|---|---|
| Lock and thread plumbing (`IsGameCoreThread`, `Has`/`Get`/`ReleaseGameCoreLock`, `GetScriptSystem`) | Becomes server-local; never crosses | ~1.1 M per turn, 77% of core-thread crossings; the 317 lock calls per frame (= ~158 lock/unlock pairs, nearly all re-entries by the thread that holds the lock; ~2 first-level waits per frame in the slot poll; corrected 2026-11-10 by the librarian per D17, see `interfaces-and-traffic.md` sections 5 and 9.1) |
| Wrapper lifetime (`Destroy`, `QueryInterface`) | Handled by the proxy's own wrapper objects | 55% of inbound per-frame calls |
| Static handles (`GetGame`, `GetPreGame`, `GetPlayer`, `Map::GetPlot`, `Get*Info`) | ID-based handles resolved on the client (mode ii') | 24% of inbound calls; (ii') removes 80% of synchronous calls |
| Slot status (`slotStatus`, `IsHuman`, `GetGameTurn`, `HasTurnTimerExpired`, `IsPaused`) | Client-side mirror the server pushes when something changes | most of the ~830 slot-poll calls per frame |
| Lua object identity (37k Plot, 32k City, 11k Unit pushes per turn) | Handles become (type, id) pairs with client-side tables | push traffic |
| Notifications, commands, `Destroy` | Batched, fire-and-forget, **only where the handler makes no synchronous call back** (corrected 2026-11-10, section 7.2; previously all of them) | 15.6-27.6k flag-only notifications per turn certainly; up to 9.2-9.6k more need a measurement; ~5k commands likewise |
| GameEvents hooks (30-38k value-returning calls per turn) | Keep GameEvents Lua inside the server ("mod Lua inside the server" row) | 115-118k -> 80-84k synchronous core calls |
| UI Lua reads (1.1-1.2 M per AI turn) | "a read cache or a server-side Lua host for UI queries" - **undecided** | the dominant synchronous load |

What the sources explicitly leave open: "the proxy's caching rules (what is safe to answer locally, when to invalidate)
are the design work" (REVIEW.md section 5, item 5). REVIEW.md section 1 lists "the proxy's caching and batching design"
as not validated.

## 6. Open design questions

1. **UI-Lua read path.** Read cache in the client vs a server-side Lua host for UI queries. Up to 53k calls in one slot;
   `Unit.IsHasPromotion` alone 300-440k per turn. Invalidation rules unknown. *Still open (2026-11-10): not a transport
   question; belongs to I1-C.*
2. **Two channels, one lock.** The server must serve the UI-thread and core-thread channels with the EXE's lock semantics;
   `Update` on the core thread overlaps UI-thread calls. *Answered for the transport (2026-11-10, epic 1200): channels
   `ui`, `core`, `aux` never block each other and the transport implements no lock; the server emulates the lock and
   thread role per channel (`ipc/PROTOCOL.md` 2.2, 2.3). The emulation itself is I1-B's.*
3. **Re-entrancy.** Each side services incoming requests while waiting (CanHandleAction -> shiftKey 15k per turn;
   engine callbacks inside notifications ~60k per turn). Deadlock and stale-read risk. *Answered (2026-11-10, epic
   1200): strict per-channel turn order; a side waiting for its RETURN services nested CALLs to depth 64
   (`ipc/PROTOCOL.md` 3.1); tested to depth 64 in both directions and cross-process (section 7). Constraint: a post
   handler may not call (section 7.2).*
4. **Spin budget.** Spinning burns one core per side per channel. How long to spin before blocking (hybrid uses 2000 x
   `_mm_pause`) and what happens across AI leader screens (20-89 s of near-zero traffic) are unmeasured. *Mechanism
   answered (2026-11-10): spin for `replySpinUs` / `idleSpinUs`, then block on a wake event plus the peer process
   (`ipc/PROTOCOL.md` 6); the budgets are set from task 1205's sweep (section 7.4).*
5. **Protocol overhead.** The microbenchmark is a single request slot returning an integer; "a real protocol adds
   marshalling, queueing and two threads' worth of contention" (study caveat). C++ objects (`std::string`,
   `std::vector`, `CvString`, FireWorks types) cross the interfaces today with VC9 32-bit layout (headless
   `why-32-bit.md`), so they must be marshalled, not shared. *Answered (2026-11-10): marshalling in
   `ipc/PROTOCOL.md` 5; the proxy side builds on the VC9 toolchain and the core uses no STL (7.3); per-call and
   per-post costs measured (7.4).*
6. **Save/load handshake.** The EXE hands the DLL its own stream objects; the server would serialize into a buffer the
   client copies into the EXE's stream. New, untested. *Transport part answered (2026-11-10): payloads above the
   data area are chunked; a 50 MiB call runs at 0.7-0.9 GiB/s in one process (`ipc/PROTOCOL.md` 3.4). The stream
   adaptation is I1-C's.*
7. **Database agreement.** The server opens its own DB copy; the UI's GameInfo stays in the EXE; the two must agree.
8. **Server restart.** Demonstrated for the shared texture (Gate 3), not for game state (REVIEW.md section 4).
   *Transport part answered (2026-11-10): restart, client and server crash, refusal and hang scenarios pass for
   x64 and x86 servers (section 7.5, `ipc/PROTOCOL.md` 7). Reloading game state through the resync hook is I1-B's.*
9. **Drain interval** for batched notifications (see section 4): unmeasured. *Mechanism answered (2026-11-10):
   posts are published at the next call, flush, wait, 1/8-full ring or after `flushIntervalUs`
   (`ipc/PROTOCOL.md` 4); the interval is set from task 1205 (section 7.4).*

## 7. The transport library (epic 1200, `ipc/` in this repo)

Status 2026-11-10: protocol draft 1.0 written (`ipc/PROTOCOL.md`), implementation in progress (tasks 1201-1205).
Measured results replace the provisional numbers here when task 1205 has run.

### 7.1 Design as drafted (2026-11-10, coordinator) [I]

- One section `Local\VPIpc.<clientPid>.<tag>`, created and owned by the client (the proxy inside the EXE); per-PID
  names keep parallel Goldberg instances apart. Layout in `ipc/include/vpipc/layout.h`, fixed-width fields only,
  layout hash 0x244ED16D identical in x86 and x64 [C] (pinned in `ipc/tests/test_layout.cpp`).
- Channels `ui`, `core` (one EXE thread each) and `aux` (any other client thread, serialised per top-level call: the
  render workers' ~214 calls per 30 s). Each channel: two 64-byte message lines, one data area shared by both
  directions under a strict ping-pong turn rule (a side waiting for its RETURN services nested CALLs, which is how
  the re-entrant callbacks of `interfaces-and-traffic.md` section 7 are carried), chunked transfer above the data
  area (save streams), and two single-producer rings for posts.
- Waiting: spin for a budget, then the microbenchmark's Dekker sleeping flag, then
  `WaitForMultipleObjects({wake event, peer process})`, so a dead peer ends every wait within one tick.
- The transport does not implement the game-core lock; the server (I1-B) emulates the lock and the thread role per
  channel.

### 7.2 Constraint for the proxy generator (I1-C): what can be batched (2026-11-10)

**Rule [C] (from the protocol design):** posts are drained by the receiver before any synchronous message on the
same channel, so a notification posted inside an engine-to-DLL call runs on the client's waiting thread before that
call returns, where the EXE would have run it. But **a post handler cannot make a synchronous call**: the peer is not
waiting on the channel, so the call could not be answered in turn order. The same holds in the other direction for
engine-to-DLL commands posted to the server: if the DLL code they run calls a `gDLL` getter, they must be calls.

**What the benchmark data supports** (`research/sources/boundary-benchmark/results.json`, keys `out_core` and
`in_core`, huge save turns 350-352; recounted 2026-11-10):

| Per AI turn (core thread) | f1 350->351 | f2 351->352 | Batchable? |
|---|---|---|---|
| All void notifications (`out_core` class `notify`) | 25,175 | 36,775 | |
| `setDirty` | 13,620 | 23,727 | yes [I]: sets a UI dirty flag, no read-back in the sources |
| `SetSpecificCityInfoDirty` | 1,982 | 3,859 | yes [I]: same kind |
| Everything else: `GameplayUnitShouldDimFlag` (2,972 / 2,923), `GameplayUnitTeleported` (1,652 / 1,576), `GameplayFOWChanged` (1,412 / 1,369), `netMessageDebugLog` (973 / 986), `GameplayUnitVisibility` (687 / 623), `GameplayYieldMightHaveChanged` (445 / 250), `GameplayUnitFortify`, `GameplayUnitWork`, `GameplayUnitSetDamage` (~130-210 each), smaller ones | 9,573 | 9,189 | **unknown: needs a measurement** |
| Engine -> DLL calls on the core thread other than `Update` (`in_core`; all of them are nested inside some outbound call, because the core thread's only depth-0 call is `Update` plus wrapper `Destroy`) | ~169k | 173,868 | they are synchronous calls already |
| of which `CvDllUnit::GetOwner` / `GetID`, `CvDllPlot::GetPlotIndex` | 12,362 / 12,296 / 6,995 | similar | |

The counters recorded calls per method and thread, **not which outbound call each inbound call was nested in**. So
REPORT.md's "the engine calling `GetOwner`/`GetID`/`GetPlotIndex` from inside notifications, about 60k per turn" is
not attributable per notification type from this data [I]. The headless traces (`engine-contract.md` 6.7) name one
case: `CvDllMap::UpdateDeferredFog()` inside `GameplayUnitTeleported`/`Visibility`/`Created`/`Embark`/`Destroyed`
and `GameplayCityCreated` (4,999 in a 100-turn A/A run), and `CvDllUnit::SetupGraphical()` inside
`GameplayEraChanged`.

**Consequence for the cost model:** at most 9.2-9.6k notifications per turn move from "batched" to "synchronous".
The nested inbound calls inside them were already counted as synchronous in section 2's (ii) rows. At the measured
spin round trip (0.21-0.3 us) that is about 2-3 ms per AI turn, against 0.35 s of synchronous UI-thread traffic, so
it is negligible in time. With events it would be ~0.1 s. The batched remainder is 15.6-27.6k per turn, still under
1 ms. The real effect is on correctness: classifying a re-entering notification as a post is a protocol error.

**Measurement needed (I1-C):** with the boundary-benchmark instrumentation, record for each inbound core-thread call
the outbound method it is nested in (a per-thread "current outbound method" set in the generated proxies), and run
one AI turn. That gives the batchable set exactly. Until then the proxy generator should make every notification
other than the flag-only ones synchronous.

### 7.3 Toolchains for the two sides (2026-11-10, decision D15) [C]

Measured with probes and the `ipc/` build on this machine (i9-13900KS, Windows 11 26100):

| Item | Value |
|---|---|
| clang-cl 19.1.7 `-m32` in the VS 2008 environment (`vsvars32.bat`), no `-std` flag: the DLL's own build mode (`build_vp_clang.py`) | `__cplusplus` 201103 (C++11), `_MSC_VER` 1500; relaxed (C++14) `constexpr` is rejected |
| same with `/std:c++14` | compiles; links only with `/Zc:sizedDealloc-`, because msvcr90 has no `operator delete(void*, unsigned int)` (lld-link: undefined symbol) |
| clang-cl `-m64` with the MSVC 14.43 STL (D15), CMake `CXX_STANDARD 14` | `__cplusplus` 201402, `_MSC_VER` 1943 |
| CMake 4.1.1 with clang-cl reporting `_MSC_VER` 1500 | adds `/INCREMENTAL:YES` to Debug links; lld-link rejects it ("could not open '/INCREMENTAL:YES'") |
| x86 executables against msvcr90 | need the `Microsoft.VC90.CRT` side-by-side manifest embedded; CMake embeds it with `mt.exe -outputresource`, which llvm-mt does not implement (the executable then fails to load); the SDK v6.0A `mt.exe` works |
| msvcr90 in WinSxS | `x86_microsoft.vc90.crt_..._9.0.30729.9635`; the VC9 headers request 9.0.21022.8 (redirected by publisher policy) |

Consequence: everything in `ipc/` is C++11 language, and the library core uses no C++ standard library type (the
VC9 library has no C++11 headers, and STL objects must never cross between the two CRTs). Rules:
`ipc/PROTOCOL.md` section 11. Build: `python ipc/build.py` (two clang-cl trees, `build/x86` VC9 and `build/x64` MSVC
14, both `/MD`).

### 7.4 Cross-process latency, spin budget and soak (tasks 1205 and 1209, 2026-11-10) [L]

Machine and build for every number here: i9-13900KS (32 logical CPUs), Windows 11 10.0.26100, power plan "Bitsum
Highest Performance"; clang-cl 19.1.7, Release, x86 client from the VC9 tree (msvcr90, C++11), servers from the x64
tree (MSVC 14.43 STL, C++14) and the x86 tree; library = team branch with tasks 1206 (post batching) and 1207 (attach
fix), binaries built from commit 88b694d / 573637b (soak); default WaitConfig unless stated. Raw data and summaries:
`ipc/results/bench-2026-11-10-0456.md`, `bench-posts-{pre,post}1206-*.md`, `spin-budget-2026-11-10-0643.md`,
`soak-2026-11-10-0439.md`. The microbenchmark columns were re-run in the same session, interleaved with the library
series. **Load caveat:** other teams' builds and test runs kept the machine at 5-93% CPU (typically 25-60%) against
the runner's 15% limit; every such series is marked LOADED in the `-runs.csv` files. Civ V was not running during any
series (the runner checked before each). 0 failures in every series.

**Calls, cross-process, x86 client, before task 1209 (us; pass 1 / pass 2; superseded by the next table):**

| Case | vpipc median | micro median | vpipc p99 | micro p99 |
|---|---|---|---|---|
| Empty call, x64 server, unpinned | 0.436 / 0.421 | 0.209 / 0.227 | 0.585 / 0.634 | 0.311 / 0.339 |
| Empty call, x64 server, pinned to CPUs 2/4 (P-cores) | 0.440 / 0.356 | 0.204 / 0.183 | 0.634 / 0.488 | 0.286 / 0.245 |
| Empty call, x86 server, unpinned | 0.473 / 0.463 | 0.233 / 0.232 | 0.616 / 0.613 | 0.335 / 0.341 |
| 3 x i32 in, 1 out (unpinned / pinned, pass 2) | 0.464 / 0.388 | | 0.682 / 0.516 | |
| 64-byte string in (pass 2) | 0.478 / 0.473 | | 0.703 / 0.591 | |
| 1 KB string out (pass 2) | 0.542 / 0.578 | | 0.766 / 0.866 | |
| One nested callback (pass 2) | 0.822 / 0.736 | | 1.075 / 0.960 | |

Before task 1209 a library call cost about twice the microbenchmark's bare request slot (+0.15-0.25 us per round
trip). *Until 2026-11-10 09:40 this paragraph said the 0.2 us had not been profiled, guessed extra shared cache lines,
and called the latency criterion of the epic's definition of done not met; task 1209 measured and cut it, below.*

**Per-call overhead, attributed and cut (task 1209, worker 1205, 07:55-09:34)** [L]. Same machine; library 5a84a05,
merged into the team branch as 4af16c6; load 6-16% (the quietest series of the day). Timing method identical to the
microbenchmark's (lfence, rdtsc, lfence; TSC calibrated against QPC; timer overhead 0.02-0.04 us per sample) [C]; one
bench defect fixed on the way (the client compared the mode string inside the timed region). The microbenchmark was
rebuilt from its read-only source and run in every set. Raw data: `ipc/results/bench-1209-attrib-2026-11-10-*`.

Attribution: one feature removed at a time in an uncommitted experimental build; empty call, x64 server, median of 5
run medians, load 8-16%; same-session microbenchmark 0.203 / 0.211 us pinned (spin / hybrid):

| Removed | pinned us (delta) | unpinned us (delta) |
|---|---|---|
| nothing (library before 1209) | 0.398 | 0.391 |
| `SideCtlLine::activeDepth` written on every call, into the line that holds this side's sleeping flag, which the peer reads in every publish | 0.333 (-65 ns) | 0.288 (-103) |
| `ep_enter`/`ep_leave`: 2 TLS lookups and 2 locked operations per side | 0.344 (-54) | 0.340 (-51) |
| clock read before the spin | 0.379 (-19) | noise |
| clock reads in `serve`/`next_message` | 0.387 (-11) | noise |
| all four together | 0.288 (-110) | 0.292 (-99) |

The effects overlap: the activeDepth store misses, and the next locked operation waits for the line [C, code read].
Per round trip the library moves the same 2 shared message lines as the microbenchmark [C].

Changes made (cumulative, pinned, empty call): activeDepth written only when this side's deepest depth grows
(0.393-0.425 -> 0.345); fast admission on non-shared channels with no TLS and no locked operation, the monitor
calling `FlushProcessWriteBuffers()` before it reads the active counts (asymmetric Dekker, `ipc/PROTOCOL.md` 7.2)
(0.324); 32 pause-and-look steps before a wait's first clock read (0.300); no clock read in `serve()` without a
timeout (0.283-0.301); WaitConfig cached per channel and the publish and quick look inline (`ipc/src/wait_fast.h`)
(0.265); hot channel members `__forceinline` (0.250). No layout or protocol change; the layout hash is unchanged.

**Calls after task 1209** (`ipc/results/bench-2026-11-10-0919.md`, 09:19-09:29, x86 client; us, pass 1 / pass 2):

| Case | vpipc median | micro median | vpipc p99 | micro p99 | vpipc p99.9 | micro p99.9 |
|---|---|---|---|---|---|---|
| Empty call, x64 server, unpinned | 0.277 / 0.217 | 0.209 / 0.227 | 0.372 / 0.361 | 0.311 / 0.339 | 0.480 / 0.417 | 0.443 / 0.456 |
| Empty call, x64 server, pinned 2/4 | 0.251 / 0.309 | 0.204 / 0.183 | 0.379 / 0.386 | 0.286 / 0.245 | 0.485 / 0.609 | 0.649 / 0.607 |
| Empty call, x86 server, unpinned | 0.206 / 0.213 | 0.233 / 0.232 | 0.344 / 0.344 | 0.335 / 0.341 | 0.436 / 0.521 | 0.502 / 0.505 |
| Empty call, x86 server, pinned 2/4 | 0.255 / 0.281 | 0.204 / 0.183 | 0.350 / 0.348 | 0.286 / 0.245 | 0.422 / 0.435 | 0.649 / 0.607 |
| 3 x i32 in, 1 out (unpinned / pinned) | 0.198-0.274 / 0.250 | | 0.33-0.41 / 0.35 | | | |
| 64-byte string in | 0.345-0.371 | | 0.47-0.49 | | | |
| 1 KB string out | 0.426-0.509 | | 0.51-0.57 | | | |
| One nested callback | 0.386-0.628 | | 0.65-0.77 | | | |

A 7-pass A/B at 09:15 (load 10-14%) gave 0.249 us pinned / 0.268 unpinned after 1209 against 0.410 / 0.388 before,
microbenchmark 0.203-0.210 / 0.198-0.212. **Verdict on the definition of done** ("latencies matching the
microbenchmark"): the empty call is now within 0.02-0.07 us of the microbenchmark's median in every configuration
(within 0.05 us pinned in the A/B; 6-20 ns outside that unpinned, where both are bimodal by P- or E-core), and the
tails match (p99.9 0.42-0.61 vs 0.44-0.65 us). The remaining ~40-60 ns per round trip: 19-35 ns are calls into and
between the library's .cpp files, which ThinLTO removes (measured 0.230-0.232 us pinned on an intermediate build;
not enabled, because a static library holding bitcode can only be linked by lld-link, and the consumers' linkers
are I1-C's choice); ~20 ns is the protocol's per-message work (the drain-before-message check of the incoming ring,
`ipc/PROTOCOL.md` 4 rule 1; callId, depth and turn checks; the RETURN copy from the per-depth slot; stats) [L, not
separated further]. Server bitness makes no difference. Against the section 2 projection: per frame in mode (ii)
(491-975 synchronous calls) +0.02-0.07 ms over the microbenchmark; per AI turn the UI thread's 1.5-1.66 M synchronous
calls in mode (ii) cost ~0.37-0.42 s, against the projected 0.32-0.35 s (was ~0.65-0.75 s before 1209).

**Posts** (12-byte posts, N posts then one verifying call, per post = batch median / N, x64 server):

| | before 1206 | after 1206 | micro (batch under one flag) |
|---|---|---|---|
| 100 posts + 1 call | 0.145-0.165 us | 0.069 us | 0.007-0.008 us |
| 1000 posts + 1 call | 0.145-0.165 us | 0.058-0.059 us | 0.004 us |
| after 1209 (09:29, `bench-posts-post1209-2026-11-10-0929.md`): 100 / 1000 posts + 1 call | | 0.051-0.070 / 0.034-0.035 us | 0.007-0.008 / 0.004 us |

Unpinned results were bimodal in one pass (0.107 / 0.092 vs 0.068 / 0.058 us), P-core vs E-core as task 1206 saw in
process. The microbenchmark's 4 ns is not reached and is not the right target: it had no per-record framing,
marshalling or handler dispatch. At 25-37k posts per AI turn (section 7.2) the posts cost ~0.9-1.3 ms per turn after 1209 (~1.5-2.2 ms before).

**Large payloads** (chunked, PROTOCOL.md 3.4): 1 MB 5.7-7.5 GB/s; 50 MB 0.63-1.75 GB/s, ~6x slower per byte
(after 1209, at lower load: 1 MB 5.8-8.0 GB/s; 50 MB in 1.24-1.36, out 1.59-1.86 GB/s).
Probable cause [I]: the data-area spill buffers above 4 MiB are freed after each use (`kTrimBytes`), so every 50 MB
transfer first-touches two fresh 50 MB buffers. Not fixed; save-sized transfers (tens of MB, rare) are the only user.

**Spin budget** (7 x 7 replySpinUs x idleSpinUs pairs x 5 gaps, unpinned, one pass; plus a 60 s idle run and a
flush series). Defaults set from it (PROTOCOL.md 4 and 6, `vpipc.h`):
- `replySpinUs` **100** (was 200). Below 20 a call that finds the server asleep pays two event wakes (median 9-33 us
  at 1 ms gaps, 29-60 us at 10 ms gaps); from 50 to 1000 it pays one (8-10 us and 14-20 us) and the values do not
  differ; client cycles per call are equal because the client stops spinning when the reply comes. 100 covers the
  single wake's p99 (36-90 us under this load) and halves the spin before blocking on a slow handler.
- `idleSpinUs` **50** (kept). Calls 10 us apart need >= 20 (0.40-0.56 us instead of 7-17 us). Covering 100 us gaps
  would need 200 and costs ~204 us of server spin after every burst instead of ~55 us. The first call of each
  6.4 ms frame pays one wake.
- `flushIntervalUs` **1000** (kept). With dense posts the delivery delay is ~0.57 x flush at the median and ~1.05 x
  at p99; the consumer's busy time is 34% / 19% / 12% / 9.6% of a core at 100 / 250 / 500 / 1000 us, over a ~8.5%
  floor for handling the posts. Sparse posts were delayed up to 16 x their spacing by the 1206 code (bug 1208,
  fixed 2026-11-10, below).
- Fully idle for 60 s: server 46.9 ms CPU (0.078% of one core), client below the 15.6 ms timer resolution; the first
  call afterwards 34.6 us, the second 0.92 us.

**Soak, 15 minutes: PASS** (04:40:44-04:55:46, commit 573637b, x64 server, seed 12345, replySpinUs 200 as then
defaulted, machine 62% loaded at the start). ui channel: 112.4 M calls including 223 bursts of 12-24k calls in one
16.7 ms slot, 2.25 M posts checked in order. core channel: 2,214 `Update` calls of 100-500 ms carrying 79.4 M nested
callbacks (19.9 M of them depth-2 calls back into the server) and 39.7 M server posts, each `Update` checking that
all its posts arrived before its RETURN. aux: one call a minute. Idle gaps of 34, 90 and 74 s. Zero failures, both
processes exited 0. Per active minute: ui p50 0.44-0.50 us, p99 0.90-1.08 us; core p50 0.38-0.43 us, p99
0.75-1.14 us; max 0.5-1.0 ms except 8-16 ms in five minutes (preemption under load). Private bytes flat (+88 KB
client, +32 KB server), handles constant (86 / 59). CPU: client 180-194% of a core while active (the ui thread
busy-waits through each frame by design), server 98-105%; in the idle minute 0.1% / 0.0%. First call after each
gap 21-36 us.

**Soak on the final code, 15 minutes: PASS** (09:39:52-09:54:52, team branch 4af16c6 with tasks 1208 and 1209,
default WaitConfig incl. replySpinUs 100, x64 server, seed 12345, load 10.5% at the start, Civ V not running;
`ipc/results/soak-2026-11-10-0939.md`) [L]. ui: 112.8 M calls including 205 bursts of 12-24k calls, 2.26 M posts
checked in order. core: 2,215 `Update` calls carrying 79.5 M nested callbacks (19.9 M depth-2 calls back into the
server) and 39.7 M server posts. Idle gaps of 34, 90 and 74 s. Zero failures, both processes exited 0. Per active
minute: ui p50 0.33-0.35 us, p99 0.66-0.72 us; core p50 0.34-0.35 us, p99 0.60-0.81 us (first soak: 0.44-0.50 /
0.90-1.08 and 0.38-0.43 / 0.75-1.14); max 0.1-0.6 ms except 2-12 ms in two minutes. Private bytes flat (+88 KB
client, +32 KB server), handles constant (93 / 66). CPU as in the first soak (client 194%, server 102% of a core
while active; 0.1% idle). First call after each gap 21-33 us.

**maxDepth through real endpoints** [L]: depth 64 succeeds; 65 is refused by the sender-side check and surfaces as
RemoteError, the next call is Ok. Receiver side (client limit 64, server 63): the server refuses a CALL at depth 64,
the error unwinds as RemoteError, the next call is Ok, the server exits 0.

**Library defects task 1205 found:** (1) the first call after attach could be lost (independently of 1204's B1, 4 of
30 runs; after the 1202/1207 fixes 100 of 100 attach-then-call runs succeed without any workaround); (2) bug 1208,
sparse posts delayed up to 16 x their spacing: posts every 100 us had 0.5-0.8 ms median / 1.5 ms p99 delivery delay
at any flushIntervalUs from 100 to 1000; posts every 1 ms with flush 1000 had 7.1 ms median, 15.1 ms max. Cause
[C]: `push_post` read the clock only at the 1st and every 16th unpublished record. Fixed 2026-11-10 (bug 1208,
worker 1203): the clock is read at every post while fewer than 16 records are unpublished. Same repro after the fix
(x86 client, x86 server, posts every 1 ms, flush 1000): p50 16-18 us, p99 1035 us, max ~1.1 ms (was 7064 / 15070 /
15070 us); posts every 100 us: p50 517 us, p99 1035 us. Batch of 1000 posts + call unchanged (58-98 us per batch,
P-core vs E-core spread). Bound, while posts keep coming: ~flushIntervalUs + one spacing for sparse posts,
2 x flushIntervalUs worst case (comment above `ChannelImpl::begin_post`) [L, light load with other teams' tests].

### 7.5 Robustness (2026-11-10 04:16:39-04:17:22, task 1204's harness) [L]

x86 test client (VC9 build) against the x64 and the x86 test server, Release, team branch at 3eab756 plus
uncommitted documentation (library code identical to 40d52b1); i9-13900KS, Windows 11 26100; Civ V was running on
the machine (not started by the team). Full report and logs: `ipc/results/robustness-2026-11-10-final.md`. Every
scenario passed on both servers, no run hung, no process was left behind.

| Case | x64 server | x86 server |
|---|---|---|
| Server crash inside a handler: client call returns `PeerDied` | 3.1 ms | 2.5 ms |
| Next call after the crash returns `Disconnected` | 0.005 ms | 0.002 ms |
| Client crash: server exits | 1.9-4.1 ms | 3.6-6.1 ms |
| Orderly close: server exits | 2.6 ms | 3.0 ms |
| Restart: first call after a new server, resync hook once, 10,000 verified calls | 114 ms | 113 ms |
| 21 restarts, kill to verified calls: median / max | 125 / 249 ms | 123 / 222 ms |
| Version / layout / schema refusal, both sides, readable text: server exits | 62-79 ms | 92-211 ms |
| Hung (hungAfterMs 1000, tick 100): `Hung` event / `PeerHung` returned | 1206-1220 / 1317 ms | 1312-1321 / 1433 ms |
| No client: server times out (2000 ms) | 2304 ms | 2227 ms |
| Attach race s0: call the moment `wait_for_server` returns, 40 attempts | 0 lost, slowest 16.9 ms | 0 lost, slowest 16.4 ms |

Found and fixed on the way: **bug B1**, where the first call after an attach could be lost for ever (1-10 of 40
attempts), because the server reset its cursors from the live sequence words after the client had already called.
It is fixed twice over: a two-phase accept, and cursors that reset to zero (`ipc/PROTOCOL.md` 7.2). **Bug B2**: a
lost resync-hook call broke the channel and the next call reported `ProtocolError` instead of the cause; a broken
channel now keeps its failure status. The hung-detection bound is `hungAfterMs + 3 ticks` for the event and
`+ 4 ticks` for a blocked call (decision D1 in the epic, `ipc/PROTOCOL.md` 7.3).

Re-run on the code with task 1209's fast admission (team branch 4af16c6, 2026-11-10 09:38:55-09:39:30, settle 0,
no other load found): every scenario passed again on both servers (`ipc/results/robustness-2026-11-10-post1209.md`)
[L].

## Contradictions and uncertainty

- The study's prose says "roughly 0.25 s of IPC per AI turn"; its own projection table and REPORT.md give 0.32-0.35 s
  for the UI-thread synchronous part of mode (ii). The study's table also lists 0.35 s per turn for **both** (ii) and
  (ii'); REPORT.md gives no per-turn (ii') figure.
- Naive per-turn spin: REPORT.md "1.0 / 0.87 s", study "0.9 to 1.0 s", memory note "~1 s": same data, different rounding.
- Instrument overhead (30-50 ns per counted call) was estimated, not measured, and is inside every per-frame count.

Sources: `research/sources/boundary-benchmark/RESULTS.md`, `results_table.md`, `projection.txt`, `REPORT.md` sections 4-5
and Bottlenecks; `research/sources/thin-client-study.html` sections 3, 5, caveats; `research/sources/REVIEW.md` sections 1,
2, 4, 5; `~/Documents/civ5-headless/docs/engine-contract.md` section 6.4 and `docs/why-32-bit.md` (read in place).
