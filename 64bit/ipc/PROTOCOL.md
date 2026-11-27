# vpipc protocol, version 1.0 (DRAFT)

Status: **1.0, 2026-11-10** (revised the same day for decision D15: section 11), epic 1200 (package I1-D, tracker 0023), team coordinator. Sections 3, 4
and 7 describe the implemented library (tasks 1201-1209); the wait and flush defaults in sections 4 and 6 were
measured by task 1205 (2026-11-10, `ipc/results/spin-budget-2026-11-10-0643.md`). *The first version of this line
said "Nothing below is measured yet; every number marked provisional is a default that task 1205 measures".* The
channel sizes (section 2.2) are still unmeasured defaults. The authoritative byte layout is `include/vpipc/layout.h`; where this file and the
header disagree, the header wins and this file is wrong.

The transport joins two processes:

- the **client**: the 32-bit proxy DLL inside the EXE (`CivilizationV_DX11.exe`), long-lived;
- the **server**: the 64-bit process that hosts the DLL (also built 32-bit for tests); may die and be
  restarted while the client lives.

Decision D3 governs: one shared-memory section, spin-then-block waiting, batching of fire-and-forget
traffic, no kernel object on the hot path. Measured basis (2026-11-08, i9-13900KS): spin round trip 0.21 us
median, kernel event 13.2 us, batch of 1000 under one flag 4 ns per call
(`research/03-dll-engine-boundary/ipc-design-and-measurements.md` section 1).

## 1. Terms

| Term | Meaning |
|---|---|
| side | client or server |
| channel | an independent pair of message slots plus two fire-and-forget rings; used by one thread per side at a time |
| initiator | the side allowed to start a top-level (depth 0) call on a channel; fixed per channel |
| call / return | a synchronous message and its reply |
| nested call | a call made by the side that is currently handling a call on the same channel (a DLL callback into the engine inside an engine-to-DLL call, or the reverse) |
| post | a fire-and-forget record in a channel's ring (notifications, commands, `Destroy`) |
| generation | count of successful server attaches to one section; 1 for the first server |

## 2. Section and kernel objects

One named, pagefile-backed section, created by the **client**, which owns it for its whole life.

| Object | Name | Created by |
|---|---|---|
| section | `Local\VPIpc.<clientPid>.<tag>`; `tag` defaults to `main`. The server receives the full name on its command line (`--ipc <name>`) | client |
| wake event per channel and side | `<section>.w<ch>.c` (client waits on it), `<section>.w<ch>.s` (server waits on it); auto-reset | client |
| process handles | each side opens the peer process with `SYNCHRONIZE` (plus `PROCESS_QUERY_LIMITED_INFORMATION`) | each side |

Per-PID names make several game instances on one machine (Goldberg, D9) collision-free by construction.

The events are only used after a side has spun out its budget (section 6). Nothing else is a kernel
object; there is no mutex in the section.

### 2.1 Layout

All fields are fixed-width integers; no pointer, `size_t`, `bool` or C++ object is stored in the section,
so the layout is identical in x86 and x64 builds (checked by `static_assert` and by a layout hash both sides
compare at attach). Every independently written field group sits on its own 64-byte line.

```
offset 0      Header (4096 bytes, one page)
                line 0  identity     magic 'VPIP', version major.minor, header size, total size,
                                     layout hash, channel count, app schema hash, QPC frequency
                line 1  client       pid, pointer bits, state, heartbeat QPC and counter
                line 2  server       pid, pointer bits, state, version, layout and schema hash it was
                                     built with, refusal reason, heartbeat QPC and counter
                line 3  connection   connection state, generation, attach-requesting pid
                lines 4-11           channel table (one line per channel, up to 8): offset, initiator,
                                     flags, data bytes, ring bytes, name
                bytes 1024-2047      server refusal text (UTF-8, NUL-terminated)
                bytes 2048-3071      client refusal text
offset 4096   Channel block 0, then 1, ... (each 64-byte aligned)
```

Channel block:

```
line 0   MsgLine client->server     the client's outgoing message header (seq written last)
line 1   MsgLine server->client
line 2   client control             client sleeping flag, client deepest depth reached (diagnostic)
line 3   server control             server sleeping flag, server deepest depth reached (diagnostic)
line 4   ring c->s write index      written by the client
line 5   ring c->s read index       written by the server
line 6   ring s->c write index      written by the server
line 7   ring s->c read index       written by the client
         data area                  dataBytes, shared by both directions (section 3.3)
         ring c->s data             ringBytes (power of two)
         ring s->c data             ringBytes
```

`MsgLine` (64 bytes): `seq u32` (publish word), `kind u16`, `flags u16`, `method u32`, `callId u32`,
`depth u32`, `length u32`, `totalLength u32`, `status u32`, `inline[32]`.

### 2.2 Default channels

| # | Name | Initiator | Used by | Shared |
|---|---|---|---|---|
| 0 | `ui` | client | the EXE's main/UI thread | no |
| 1 | `core` | client | the EXE's game-core thread (`CvDllGame::Update`) | no |
| 2 | `aux` | client | any other client thread (render workers: ~214 calls in 30 s), serialised by a client-side lock held for a whole top-level call | yes |

Up to 8 channels; a server-initiated channel (for a server-driven turn loop, package I1-B) is one more row
with initiator = server. Defaults (not measured against real traffic; I1-C sizes them): `dataBytes` 256 KiB, `ringBytes` 64 KiB per direction, so
the three default channels map ~1.2 MiB into the EXE's address space.

The transport does not implement the game-core lock. Channels never block each other; the lock and the
thread-role emulation (`IsGameCoreThread`, `HasGameCoreLock`) are the server's (I1-B), and the server
knows the calling role from the channel a call arrived on.

### 2.3 Guarantees other epics rely on

These two statements are part of the protocol, not implementation details; the server (I1-B) and the test
infrastructure (epic 1100) are designed against them.

1. **Several game instances on one machine never collide.** Every kernel object name carries the client's process id:
   the section is `Local\VPIpc.<clientPid>.<tag>` and the wake events derive from it. Two EXEs (Goldberg parallel
   instances, D9) therefore always have distinct sections and events, and a server attaches only to the section named
   on its command line. Nothing in the transport uses a fixed global name.
2. **The game-core lock is the server's job, per channel.** The transport implements no lock and channels never
   block each other. The EXE's lock and thread-role semantics (`GetGameCoreLock`/`ReleaseGameCoreLock`,
   `HasGameCoreLock` as a recursive depth, `IsGameCoreThread` true only inside `CvDllGame::Update`) are emulated in
   the server, which knows the calling role from the channel a call arrived on (`ui`, `core`, `aux`). Lock plumbing
   never crosses the boundary.

## 3. Synchronous calls

### 3.1 The ping-pong rule

On one channel, messages strictly alternate in direction. A side sends a message only when it holds the
**turn**: the initiator holds it when the channel is idle; receiving any message gives the receiver the
turn. Consequently at most one unconsumed message exists per channel, which is what lets the channel use
one data area for both directions.

A side that has sent a CALL waits for the next incoming message, which is either:

- a **RETURN** with the same `callId`: the call is complete;
- a **CALL** (a nested call): it handles it with its registered handler, sends the RETURN, and keeps
  waiting for its own RETURN;
- a **CHUNK** or **CHUNK_ACK** (section 3.4).

Nesting is unbounded in the protocol and limited to `maxDepth` (default 64) by the library; `depth` in
`MsgLine` is the depth of the message. The sender refuses a CALL deeper than `maxDepth` locally
(`ProtocolError`, nothing is sent). The receiver checks the depth too and answers a bad CALL with a RETURN of
status 3, which keeps the turn order intact. A top-level CALL from the non-initiator side is a protocol error.
A RETURN with the wrong `callId` or depth, a sequence gap, a malformed chunk sequence, or a failed wait inside a
call marks the channel **broken**: every later call, `serve()` and `poll()` fails at once with the status that
broke it (`ProtocolError`, or the failed wait's `Timeout` / `PeerDied` / `PeerClosed` / `PeerHung`) until the channel
is reset at the next (re)attach (`channel_reset_local`). *(Task 1203; failure status kept since bug 1207,
2026-11-10.)*

### 3.2 Message kinds

| kind | value | payload |
|---|---|---|
| CALL | 1 | marshalled arguments; `method` = method id (opaque to the transport) |
| RETURN | 2 | marshalled result; `status` 0 = ok, 1 = unknown method, 2 = handler error, 3 = protocol error; on non-zero status the payload is one marshalled `str` (UTF-8 message). Statuses 1 and 2 surface as `Status::RemoteError`, 3 as `Status::ProtocolError` |
| CHUNK | 3 | one piece of a large CALL or RETURN; `totalLength` = full size |
| CHUNK_ACK | 4 | empty; the receiver has copied the previous chunk |

`callId` is a per-channel, per-side counter; a RETURN carries the id of the CALL it answers. A mismatch is
a protocol error.

### 3.3 Where the payload lives

- `length <= 32`: in `MsgLine.inline`, flag `INLINE`; one cache line moves per message, as in the
  microbenchmark.
- `32 < length <= dataBytes`: in the channel's data area.
- `length > dataBytes`: chunked (3.4).

**Validity rule** (as implemented, 2026-11-10): a received payload is valid until this side's next
`begin_call`, `begin_post` or send on that channel; `begin_call` already writes into the data area. A caller must
copy strings out of a RETURN before its next call on that channel. A handler, however, may keep reading its
arguments while it writes its result: a handler's RETURN is built in a separate per-depth buffer and copied into
the data area when the handler returns. A handler that makes a nested call must decode its arguments first.
On a **shared** channel (`aux`) the RETURN of a top-level call is copied into a per-thread buffer (fiber-local
storage) before the channel lock is released, and stays valid until that thread's next call; otherwise another
thread could overwrite the data area at once. Debug builds make a read after the rule expires fault.

Payload lengths are always multiples of 4 (section 5), so a 33-byte argument list travels as 36 bytes.

### 3.4 Large payloads (save streams, long strings)

A payload larger than `dataBytes` is sent as CHUNK messages of exactly `dataBytes` each, each carrying the
final message's `method`, `callId` and `depth`; after each chunk the receiver copies it into a heap buffer of
`totalLength` bytes and answers CHUNK_ACK; the last piece (which may be inline) is sent as the real CALL or RETURN
with flag `CHUNKED`. The receiver's handler sees one contiguous payload. No handler runs between chunks. Measured
by task 1203 (2026-11-10, in one process, under load): a 50 MiB chunked CALL at 727 MiB/s (x86) and 925 MiB/s
(x64), i.e. ~55-70 ms for a 50 MB save stream, dominated by the copies.

### 3.5 Publishing and memory order

The sender writes the payload, then the `MsgLine` fields, then stores `seq` with `InterlockedExchange`
(a full fence: required by the sleeping-flag handshake in section 6), then wakes the peer if the peer's
sleeping flag is set. The receiver reads `seq`, then the other fields. Both builds are x86 or x64
(TSO), so the only fences needed are the interlocked publish and compiler barriers.

## 4. Fire-and-forget posts

Each channel has one single-producer, single-consumer byte ring per direction. A record is
`u32 length, u32 method, payload`, padded to 8 bytes; a record that does not fit before the end of the
ring is preceded by a pad record (`method = 0xFFFFFFFF`) and starts at offset 0. The maximum post payload is
`ringBytes / 4` (16 KiB by default); larger is `PayloadTooLarge` (use a call). The producer advances the write
index after the record is written. The consumer copies the record out and advances the read index **before** the
handler runs, so draining is re-entrant: a post handler may itself post, even into a full ring. A top-level post
goes through the same connection check as a call, so the first post after a reattach resets the ring cursors.

Ordering rules:

1. **Before handling any synchronous message on a channel, the receiver drains that channel's incoming
   ring.** The sender publishes ring records before the `seq` of a later call or return, so posts are
   delivered in order with the calls on the same channel and thread. A notification posted inside an
   engine-to-DLL call is executed on the client's waiting thread before the call's RETURN is processed,
   which is where the EXE would have run it.
2. There is no ordering between different channels.
3. **A post handler must not make a synchronous call** on any channel. The peer is not waiting on the
   channel, so the call could not be answered in turn order. The library returns `ProtocolError` and
   asserts in debug builds. Consequence for the proxy generator (I1-C): a notification whose engine
   handler calls back into the DLL (the ~60k nested `GetOwner`/`GetID`/`GetPlotIndex` per turn inside
   `GameplayUnit*` notifications, `UpdateDeferredFog` inside unit notifications) must be a synchronous
   call, not a post.

**Batching** (task 1206, 2026-11-10). A post is written into the ring at once but becomes visible to the consumer
only when the producer publishes the write index, which it does: before it sends any synchronous message on the
channel (so rule 1 holds), on `flush()`, before it blocks in any wait, when the unpublished records reach
`ringBytes / 8`, when the ring is full, and at a post made at least `flushIntervalUs` (default 1000 us,
measured by task 1205: with dense posts the delivery delay is ~0.57 x flushIntervalUs at the median and ~1.05 x at
p99; the consumer's busy time is 34% / 19% / 12% / 9.6% of a core at 100 / 250 / 500 / 1000 us over a ~8.5%
floor for handling the posts) after its last post-time publish, so the first post after a quiet period goes out at once. Each such
publish wakes a sleeping consumer (`wake_peer`, whose locked compare-exchange is the fence the release-store publish
needs). **A producer that posts and then neither calls, flushes, waits nor posts again leaves its last records
invisible: end such a burst with `flush()`.** A consumer that is spinning watches the write index and drains what is
published; a full ring makes the producer wait (spin, then block) for the consumer. The `1/8` threshold (not `1/2`)
lets the consumer drain one part while the producer writes the next: with `1/2` a batch of 1000 small posts in a
64 KiB ring is never published before the call and is drained serially afterwards (59-70 ns per post instead of
36-50).

Cost, ns per 12-byte post, each batch followed by one empty call, one process, unpinned, i9-13900KS, 2026-11-10
(tasks 1203 and 1206; light background load; results bimodal by P-core vs E-core, so indicative):

| | before 1206, x86 / x64 | after 1206, x86 / x64 |
|---|---|---|
| consumer spinning, batch 1000, batch + call | 161 / 157 | 40-50 / 36-46 |
| consumer spinning, batch 100, batch + call | 157 / 174 | 54-79 / 48-65 |
| consumer sleeping (300 us gap), batch 1000, batch + call | 99 / 111 | 42-52 / 48-49 |
| no consumer thread, batch 1000: producer + flush / consumer `poll()` | 24 / 13 and 49 / 24 | 22-45 / 12-26 and 41 / 24 |

The microbenchmark's ~4 ns per call (a batch of 1000 under one flag) is **not** reached and is not the right target:
it had no per-record framing or dispatch. What remains here is the channel path per post, ~22 ns for the producer
(connection check, marshalling, record copy) and ~12 ns for the consumer (copy out, pop, handler dispatch) on a
P-core. At the measured 25-37k posts per AI turn that is about 1.5 ms per turn.

## 5. Marshalling

Little-endian, every value 4-byte aligned in the payload, no type tags. The two sides agree on each
method's signature through the app schema hash (section 7.2), not at run time.

| C++ type | Encoding |
|---|---|
| `int`, enums, `int32_t`, `uint32_t`, `DWORD` | 4 bytes |
| `bool` (and Win32 `BOOL`) | 4 bytes, 0 or 1 |
| `int64_t`, `uint64_t` | 8 bytes (4-byte aligned, copied with `memcpy`) |
| `float` / `double` | 4 / 8 bytes IEEE 754 |
| narrow string (`CvString`, `std::string`, `const char*`) | `u32 length` (0xFFFFFFFF = null pointer), bytes, NUL, pad to 4 |
| wide string (`wchar_t*`, UTF-16) | `u32 units` (0xFFFFFFFF = null), units, NUL unit, pad to 4 |
| byte buffer | `u32 length`, bytes, pad to 4 |
| handle (an object the other side owns: player, unit, city, plot, wrapper) | `u32 type, i32 id` |

No C++ object crosses by layout. The EXE hands the proxy VC9 (`msvcr90`) `std::string`/`CvString` objects;
reading those layouts is the proxy's job (I1-C); the transport takes `(pointer, length)`. A reader that
runs past the end of the payload sets a failure flag and returns zeros; the stub checks `ok()` once.

## 6. Waiting: spin, then block

Every wait (for a RETURN, for the next CALL on a serving thread, for ring space) is:

1. **Spin** on the watched words (`seq` of the incoming `MsgLine`, the incoming ring's write index) with
   `_mm_pause`, for a time budget:
   - `replySpinUs` while waiting for the reply to one's own call (the peer is working): default 100 us;
   - `idleSpinUs` while a serving thread waits for the next top-level call: default 50 us.

   Before the budget starts counting, a wait looks up to 32 times (`_mm_pause` between looks, about 1-1.5 us)
   without reading the clock, because most waits for a reply end within a few hundred ns and a
   `QueryPerformanceCounter` costs 24-34 ns (task 1209, 2026-11-10: -24 ns per empty round trip). A wait with a
   zero budget does not pre-spin.

   Both measured by task 1205 (2026-11-10, i9-13900KS, x86 client / x64 server, unpinned, machine 25-60% loaded;
   `ipc/results/spin-budget-2026-11-10-0643.md`). `replySpinUs` below 20 makes a call that finds the server asleep
   pay two event wakes (median 9-33 us at 1 ms gaps); from 50 to 1000 it pays one (8-10 us) and the values do not
   differ, and the client stops spinning when the reply arrives, so client cycles per call are equal; 100 covers the
   single wake's p99 (36-90 us under that load) and halves the spin before blocking on a slow handler (was 200
   until 2026-11-10). `idleSpinUs` 50 keeps calls 10 us apart at 0.40-0.56 us (instead of 7-17 us); covering
   100 us gaps would need 200 and costs ~204 us of server spin after every burst instead of ~55 us. Fully idle for
   60 s: server 46.9 ms CPU (0.08% of one core), client below timer resolution; the first call afterwards takes
   ~35 us.
2. **Announce sleep:** `InterlockedExchange(mySleeping, 1)`, then re-check the watched words; if changed,
   clear the flag and return.
3. **Block** on `WaitForMultipleObjects({myWakeEvent, peerProcess}, tickMs)` (tick default 100 ms). On
   each tick: check the connection state and the peer heartbeat (section 7.3), then block again.

The publisher (section 3.5) stores `seq` with a full fence, then
`InterlockedCompareExchange(peerSleeping, 0, 1)`; if it was 1 it calls `SetEvent(peerWakeEvent)`. This is
the microbenchmark's Dekker handshake (hybrid mode), which never lost a wake-up in 2 x 1 M calls.

Cost model from the microbenchmark: back-to-back calls never reach step 3 and cost a spin round trip
(0.21 us; through this library an empty call costs 0.21-0.31 us median cross-process after task 1209, 0.36-0.47 us
before it, see the research file section 7.4); a call after a gap longer than the peer's spin budget costs one event wake (median 7.4 us,
p99 ~137 us measured with a 200 us gap). Across an AI leader screen (20-89 s with almost no traffic) both
sides block and use no CPU. Spinning costs one core per spinning thread for at most the budget after each
message. Task 1205 measured latency and CPU per side over gap lengths and budgets (2026-11-10); the defaults above
are its recommendation.

## 7. Lifecycle

### 7.1 Connection states (header line 3, `connState`)

As implemented by task 1202 (2026-11-10); this section was revised to match the code the same day.

| State | Meaning | Set by |
|---|---|---|
| `NO_SERVER` (0) | section created or reset, no server attached | client |
| `ATTACH_REQUESTED` (1) | a server claimed the section, wrote its line and asks to attach | server (compare-exchange from `NO_SERVER`) |
| `CONNECTED` (2) | attach accepted; `generation` incremented | client (compare-exchange from `ATTACH_REQUESTED`) |
| `PEER_DEAD` (3) | one side saw the other's process exit | the surviving side; on the client transient (below) |
| `CLOSING` (4) | orderly shutdown by either side | the closing side |
| `REFUSED` (5) | version, layout or schema mismatch; refusal text written | the side that refused |

On the client, `PEER_DEAD` and a server's `CLOSING` are transient: the client's monitor returns the state to
`NO_SERVER` as soon as no client thread is inside a call, so a new server can attach.

### 7.2 Attach and version check

1. The client creates the section and events, fills the header, sets `NO_SERVER`, and starts its
   monitor thread. It may start the server process itself or leave that to its owner.
2. The server opens the section (retrying until its timeout if the client is not up yet) and **claims** it:
   compare-exchange `conn.attachPid` from 0 to its own pid while the state is `NO_SERVER`. Only the claimant
   writes the server line or a refusal, so a refusing server can never overwrite another server's line. The
   client's monitor clears a claim whose process has exited.
3. The server compares line 0 (protocol version, major and minor equal in 1.x; layout hash; app schema hash,
   0 = not checked) with its own. On a mismatch it writes its values and a one-line text into the server refusal
   area, sets `REFUSED`, and returns `VersionMismatch`, `LayoutMismatch` or `SchemaMismatch`. The client's monitor
   logs the text verbatim, reports `Refused` through the peer-event hook, and returns the state to `NO_SERVER`.
4. Otherwise the server writes its line (pid, pointer bits, its version and hashes) and compare-exchanges
   `NO_SERVER -> ATTACH_REQUESTED`.
5. The client's monitor checks the server's line the same way, so an old server that does not check is still
   refused. Client-side refusal: the client writes `clientRefusal` and its reason, sets `REFUSED`; the server
   acknowledges by setting its line's state to refused; the client returns to `NO_SERVER` on that acknowledgement,
   on the server's exit, or after 2 s.
6. The client opens the server process and accepts **in two phases** (fix of 2026-11-10, task 1202 follow-up, after
   bug B1 below). Phase 1, once **no client thread is inside a call** on any channel (*how this is seen:* a call on a
non-shared channel in the steady state is admitted on a fast path that only stores the channel's active count and
then reads the connection state and generation, with no locked operation; the monitor calls
`FlushProcessWriteBuffers()` before it reads the active counts, so either it sees the call or the call sees the
new state and takes the full admission path; task 1209, 2026-11-10): the client resets every
   channel block (sequence numbers, ring indices, sleeping flags) and its own channel cursors, and publishes
   `generation + 1`, but leaves the state at `ATTACH_REQUESTED`, so client calls still fail fast. Phase 2: the
   server sees the new generation, resets its own cursors on the zeroed blocks and sets its line's state to ready;
   only then does the client compare-exchange `ATTACH_REQUESTED -> CONNECTED`. If the server withdraws or dies
   between the phases (a timed-out server sets `ATTACH_REQUESTED -> NO_SERVER` itself), the client undoes the
   generation increment. Every local cursor reset (`channel_reset_local`) sets the cursors to 0 rather than reading
   the shared words, which is valid because it always follows the zeroing for the same generation (bug 1207).

   **Bug B1 (found 2026-11-10 by task 1204's scenario s0, fixed by both measures above):** in the first version the
   server reset its cursors after `CONNECTED` by reading the live sequence words; a client that called the moment
   `wait_for_server` returned had its first CALL taken as already seen and waited for ever (1-10 of 40 attempts).
   After the fix s0 loses 0 of 40 on both servers with no pause after attach.
7. The server waits for `CONNECTED` and starts serving.

Measured (2026-11-10, i9-13900KS, under load from parallel builds, indicative): `attach_server` 22-55 ms; attach
after the last call leaves 15-46 ms.

### 7.3 Liveness

- Each side runs a **monitor thread** that writes its heartbeat (QPC and a counter) into its header line, checks
  the peer process handle, and handles attach requests (client). It polls every `tickMs` while connected, at most
  every 10 ms otherwise, and every 1 ms while an attach waits for calls to drain.
- **Peer death** (process handle signalled): the survivor sets `PEER_DEAD`. Every wait on that side
  returns `PeerDied` within one tick, because waits block on the peer process handle as well as on their
  event. No wait can hang on a dead peer. Measured 2026-11-10: a wait on a killed peer returned in 1.5-2.6 ms;
  client killed -> server `PeerDied` 2.3-2.4 ms; server killed -> client `Died` event 2.2-2.7 ms.
- **Peer hung** (process alive, its heartbeat counter unchanged for `hungAfterMs` by the local clock, default
  5000 ms; no shared clock is needed). **Bound** (coordinator decision 2026-11-10, from task 1204's s8): the
  `Hung` event arrives within `hungAfterMs + 3 x tickMs`, and a blocked call sees `PeerHung` within
  `hungAfterMs + 4 x tickMs`, because the monitor ages the counter from when it saw it change, checks once per tick,
  and a blocked wait sees the verdict at its next tick. Measured with 1000 ms / 100 ms: `Hung` 1196-1310 ms,
  `PeerHung` 1307-1420 ms. At the 5000 ms default the extra 0.3-0.4 s does not matter, so the code stays; the
  task's first criterion (`hungAfterMs + tick`) was too tight. Reported once
  through the peer-event hook (`Hung`, then `Recovered` if it resumes). Calls keep waiting unless
  `failOnHung` is set (tests) or `callTimeoutMs` is non-zero. Long calls are legitimate (one `Update` can
  take seconds). `callTimeoutMs` applies only to waits made without an
  explicit timeout (replies); a serving thread's idle wait passes explicit timeouts and is never cut by it.
  It bounds each wait for the peer's next message within a call (a nested call, a chunk or the RETURN), so a long
  call that keeps calling back is never cut.
- **Client default bound (bug 0113, 2026-11-13).** The heartbeat comes from the monitor thread, so a server whose
  serving thread is stuck (a deadlock, an endless loop) stays "alive and beating" and `PeerHung` never fires. On the
  client side a `callTimeoutMs` of 0 is therefore replaced by `clientCallTimeoutMs` (default 600,000 ms; 0 = no
  bound, the old behaviour): a client call whose server sends nothing for 10 minutes ends with `Timeout`, and the
  channel stays broken as for any failed wait. The server side keeps no default bound (the EXE may legitimately sit in
  a dialog). Tests: `test_lifecycle` `hung_server_call_is_bounded_slow_calls_are_not` (a living server whose handler
  never returns: `Timeout` after the bound; a 4 s call with nested calls every 300 ms and a quiet 800 ms call under a
  1.5 s bound: Ok), `hung_server_without_bound_waits_until_killed` (bound 0: still waiting after 4 s, `PeerDied` once
  the server is killed), `client_call_timeout_default`.
- **Orderly close:** `close()` sets `CLOSING`; the peer's waits return `PeerClosed`.

### 7.4 Server restart (client side)

After `PEER_DEAD` (or the server's `CLOSING`) the client's top-level calls fail fast with `Disconnected`; a wait
already in progress returns `PeerDied` (`PeerClosed` for an orderly close) (the proxy decides what the engine
sees). When a new server attaches (7.2) the generation changes. The first client thread that then starts a
top-level call runs the **resync hook** `(endpoint, generation)` before its call; the hook may make calls
on that thread's channel only (to reload the game into the new server, re-push mirrored state); calling on
another channel could deadlock against an `aux` caller holding its lock. It also runs for generation 1, the
first attach; a hook that only cares about restarts skips that one. Other threads
that start a call during the hook wait for it to finish. Who relaunches the server is the owner's
decision, not the transport's.

### 7.5 Client crash or exit (server side)

The server's waits return `PeerDied` (or `PeerClosed`); the peer-event hook fires. The library never exits
the process; the server's policy (I1-B) is expected to log and exit, because DLL code cannot be unwound
safely from inside a call. The test server exits with a distinct code.

## 8. Library API (summary)

Full declarations: `include/vpipc/vpipc.h` and `include/vpipc/marshal.h`.

```cpp
// client (proxy)
vpipc::EndpointConfig cfg;                      // channels default to ui/core/aux
cfg.appSchemaHash = kGeneratedSchemaHash;
vpipc::Endpoint* ep = nullptr;
vpipc::Endpoint::create_client(cfg, &ep);       // caller deletes ep
ep->bind_current_thread(vpipc::kChannelUi);     // once per EXE thread role

// a generated proxy method
int CvDllPlayerProxy::GetGold() const {
    vpipc::Channel& ch = ep->current_channel();
    vpipc::Writer& w = ch.begin_call(kM_Player_GetGold);
    w.i32(m_playerId);
    vpipc::Reader r;
    if (ch.end_call(r) != vpipc::Status::Ok) return 0;  // policy is the proxy's
    return r.i32();
}

// server
vpipc::Endpoint::attach_server(cfg, &ep, 10000);
ep->channel(i).set_handlers(&dispatch_call, &dispatch_post, ctx);   // switch on method id
while (ep->channel(i).serve(INFINITE) == vpipc::Status::Ok) {}      // one thread per channel
```

## 9. Versioning rules

- `kVersionMajor.kVersionMinor` = 1.0. In 1.x both sides must match exactly; any change to `layout.h`,
  message kinds, marshalling or the state machine bumps the minor (or the major for an incompatible
  redesign) and updates this file in the same commit.
- The layout hash is computed at compile time from the sizes and offsets in `layout.h`; it catches a
  layout change that forgot the version bump.
- The app schema hash belongs to the generated interface tables (I1-C) and catches a proxy and server
  built from different tables.

## 10. Open points

1. ~~Spin budgets and the flush interval (6, 4)~~: measured by task 1205 and set (2026-11-10). Still open: the
   defaults were measured unpinned on a loaded machine; I1-B / I1-C may re-tune them against real game traffic.
2. Whether `aux` needs its own server thread or can share `ui`'s: depends on how the server emulates the
   EXE's lock (I1-B).
3. A server-initiated channel's client-side servicing (a dedicated thread vs. `poll()` from the EXE's frame):
   decided when I1-B owns the turn loop.
4. Pipelined or batched synchronous reads (mode iii in the projection) are not in 1.0; posts are the only
   batching.

## 11. Toolchains and language rules (2026-11-10, after decision D15)

The library is compiled into two very different modules:

| Side | Module | Toolchain | Language | CRT |
|---|---|---|---|---|
| client | the 32-bit proxy DLL, loaded into the EXE in the game DLL's slot | clang-cl 19.1.7 `-m32` in the VS 2008 environment (`vsvars32.bat`): VC9 headers and libraries, SDK v6.0A/v7.0A, the same as `build_vp_clang.py` | C++11: clang-cl's default there (`__cplusplus` 201103, `_MSC_VER` 1500, measured 2026-11-10) | `/MD`, msvcr90 (the EXE's heap) |
| server | the 64-bit server, sharing a heap with the x64 DLL | clang-cl 19.1.7 `-m64` with the MSVC 14.43 STL and SDK 10.0.26100 (D15) | C++14 (`_MSC_VER` 1943) | `/MD`, MSVC 14 |

The proxy must present and consume VC9-layout objects (`CvString`, STL, FireWorks) on the EXE's msvcr90 heap,
so it cannot use a newer toolchain. Hence, for everything under `ipc/include` and `ipc/src` (the core):

1. **C++11 language.** No C++14: no relaxed `constexpr` (single-return only), no generic lambdas, no `auto` return
   deduction, no digit separators or binary literals. Nothing from C++17.
2. **No C++ standard library types or headers.** Allowed: the C headers (`stdint.h` comes from clang, VC9 has none),
   `<new>`, `<windows.h>`, `<intrin.h>`, `<emmintrin.h>`. Not `<string>`, `<vector>`, `<memory>`, `<atomic>`,
   `<thread>`, `<mutex>`, `<chrono>`, `<algorithm>`, `<functional>`. Optional STL conveniences are header-only in
   `vpipc/stl.h`, compiled with whichever STL the including module has.
3. **Runtime features VC9's CRT lacks:** no `thread_local` (use `TlsAlloc`), no function-local static with a
   dynamic initializer (thread-safe statics need the VS 2015+ CRT), no sized deallocation (msvcr90 has no
   `operator delete(void*, size_t)`; it only appears from C++14 on, so C++11 avoids it), no exceptions thrown.
4. **Concurrency:** `Interlocked*` / `_Interlocked*` intrinsics (`InterlockedCompareExchange64` for 64-bit fields
   on x86), `volatile` plus `_ReadWriteBarrier`, `SRWLOCK` / `CRITICAL_SECTION`, `CreateThread`, `malloc` / `free`.
   Windows 7 API level.
5. **Tests and tools** (`ipc/tests`, `ipc/testapps`, `ipc/bench`) build in both trees: C++11 language, and may use
   `<string>`, `<vector>`, `<algorithm>` with their C++03 API only (no `vector::data()`, `emplace_back`,
   `unique_ptr`, `std::thread`, `std::atomic`, `std::chrono`, `std::function`, `std::to_string`, `unordered_*`).

Build: `python ipc/build.py [--config Release|Debug] [--test]` captures each environment, then CMake + Ninja +
clang-cl + lld-link with the toolchain files `ipc/cmake/clang-cl-x86.cmake` / `clang-cl-x64.cmake`, into
`build/<arch>/<config>`. Two quirks found on 2026-11-10: with `_MSC_VER` 1500 CMake adds `/INCREMENTAL:YES` to Debug
links, which lld-link rejects (rewritten in the top-level `CMakeLists.txt`); and x86 executables need the msvcr90
side-by-side manifest embedded with the SDK's `mt.exe` (llvm-mt cannot embed), set in `clang-cl-x86.cmake`.
