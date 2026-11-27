# ipc/ - vpipc, the shared-memory transport between the proxy and the server

`ipc/` is the transport library that joins the 32-bit proxy DLL inside the EXE (the **client**) to the 64-bit
**server** that hosts the x64 DLL. One named shared-memory section per client process, synchronous calls with
nesting in both directions, fire-and-forget posts, spin-then-block waiting, and peer-death, hang and restart
handling. It is a static library (`vpipc`) with no dependency beyond kernel32 and no C++ standard library type in
its core, so the same sources build for the EXE's VC9/msvcr90 world and for the server's MSVC 14 world.

| Read | For |
|---|---|
| [PROTOCOL.md](PROTOCOL.md) | the protocol, version 1.0: section layout, message kinds, ordering, marshalling, waits, lifecycle, toolchain and language rules (section 11). Normative; where it and `include/vpipc/layout.h` disagree, the header wins. |
| [include/vpipc/vpipc.h](include/vpipc/vpipc.h) | the public API (`Endpoint`, `Channel`, configs, statuses) |
| [include/vpipc/marshal.h](include/vpipc/marshal.h) | `Writer`, `Reader`, `Handle`; [stl.h](include/vpipc/stl.h): optional `std::string` helpers |
| [research/03-dll-engine-boundary/ipc-design-and-measurements.md](../research/03-dll-engine-boundary/ipc-design-and-measurements.md) | why the design is what it is, and every measurement: toolchains (7.3), latency, posts, spin budget, soak (7.4), robustness (7.5) |
| [results/](results/) | the raw output and reports of the runners below |

Layout of this folder: `include/vpipc/` public headers, `src/` the library, `tests/` unit tests (ctest), `testapps/`
the robustness client and server, `bench/` the bench and soak programs, `tools/` the three runners, `cmake/` the
two clang-cl toolchain files, `results/` run reports.

## Build

```
python ipc/build.py [--arch x86|x64|both] [--config Release|Debug|RelWithDebInfo] [--test] [--clean] [--target T]
```

Defaults: `--arch both`, `--config Release`. It captures the toolchain's environment, configures with CMake + Ninja
(every time; cheap with a cache) from the repository's top-level `CMakeLists.txt`, and builds. `--test` runs `ctest`
in each tree after building; `--clean` deletes the tree first; `--target` builds one target only.

| Tree (git-ignored) | Side | Toolchain | Language | CRT |
|---|---|---|---|---|
| `build/x86/<config>` | proxy / client | clang-cl `-m32`, VS 2008 (VC9) headers and libraries via `vsvars32.bat`, SDK `mt.exe` for the msvcr90 manifest | C++11 (clang-cl's default there: `__cplusplus` 201103, `_MSC_VER` 1500) | `/MD`, msvcr90 |
| `build/x64/<config>` | server | clang-cl `-m64`, MSVC 14.4x STL and Windows 10 SDK via `vcvarsall x64` (decision D15) | C++14 | `/MD`, MSVC 14 |

Requirements: LLVM in `C:\Program Files\LLVM\bin` (or `VP_LLVM_BIN`), VS 2022 Community (CMake, Ninja, vcvarsall),
VS 2008 tools (`VS90COMNTOOLS` or its default path). CMake option `C5_STATIC_CRT=ON` switches to `/MT`. Code in
`ipc/` is C++11 so it builds in both trees (rules: PROTOCOL.md section 11).

What it builds: the `vpipc` static library in both trees; unit tests `test_channel`, `test_layout`,
`test_lifecycle`, `test_marshal`, `test_ring`, `test_wait` (both trees); `vpipc_test_server` and
`vpipc_bench_server` (both trees); `vpipc_test_client` and `vpipc_bench_client` (x86 tree only, because the client
lives in the 32-bit EXE). Executables land in `build/<arch>/<config>/ipc/{tests,testapps,bench}/`.

## The three runners (`ipc/tools/`)

All three build nothing: run `python ipc/build.py` first. All write into `ipc/results/` by default.

| Runner | Runs | Options (defaults) | Output |
|---|---|---|---|
| `run_robustness.py` | the robustness scenarios of task 1204: the x86 `vpipc_test_client` against `vpipc_test_server` of each tree. s0 attach race; `functional` (ui/core/aux concurrently: every type, 13 payload sizes, nesting to depth 48, posts both ways, errors, 50 MB both ways); s1-s3 server crash in a handler / during a nested callback / while idle; s4 restart (+20 restarts); s5 client crash; s6 client close; s7 version, layout and schema refusal; s8 hung server; s9 no client. Each run is one client in its own Windows job object with a watchdog budget; a process left in the job 3 s after the client exits is a FAIL ("orphan"). | `--servers x64,x86`, `--scenarios s0,functional,s1,...,s9`, `--config Release`, `--iterations 3` (functional suite per thread), `--settle-ms 0` (old bug-B1 workaround, not needed), `--note TEXT`, `--out FILE` | `results/robustness-<date>.md`, logs in `results/robustness-<date>-logs/<server>/<scenario>/`. Exit 0 when every run passed. |
| `run_bench.py` | the latency and throughput measurements of task 1205 with `vpipc_bench_client` (x86) and `vpipc_bench_server`. Suites: `core` (empty and typed calls, strings, nested, blobs 1 MB / 50 MB, x64 and x86 server, pinned and unpinned), `posts` (posts + verifying call), `sweep` (spin budget grid x gaps, 60 s idle runs, flush-interval series), `quick` (smoke run, small counts). Aborts if Civ V runs; samples CPU load with typeperf and waits while it is above `--max-load`; a series taken above it is marked LOADED. | `--suite` (required), `--config Release`, `--passes 2`, `--calls 1000000`, `--blob-iters-1mb 200`, `--blob-iters-50mb 10`, `--pinned` (sweep: client CPU 2, server CPU 4), `--spins`, `--gaps`, `--idle-configs 200,50`, `--idle-s 60`, `--flushes 100,...,5000`, `--intervals 10,100,1000`, `--dur-ms 2000`, `--max-load 15`, `--load-retries 10`, `--civ-wait-min 120`, `--tag` (posts suite name suffix) | `results/bench-<stamp>.*`, `bench-posts[-<tag>]-<stamp>.*`, `spin-budget-<stamp>.*`, `bench-quick-<stamp>.*` (`.csv`, `-runs.csv`, `.md`; stamp `YYYY-MM-DD-HHMM`) |
| `run_soak.py` | the soak (task 1205, measurement 6): a game-shaped traffic model on all three channels (ui frames and bursts, core `Update` calls with tens of thousands of nested callbacks and posts, aux once a minute, idle gaps of 20-90 s), judged for failures, memory and handle growth and latency stability. | `--minutes 15`, `--config Release`, `--server x64` (or `x86`), `--seed 12345`, `--max-load 15`, `--load-retries 10`, `--extra "<client args>"` | `results/soak-<stamp>.csv` (per minute), `.log` (client output), `.md` (verdict) |

Latest results (2026-11-10): robustness all PASS on both servers (`results/robustness-2026-11-10-post1209.md`);
bench `results/bench-2026-11-10-0919.md` (empty call 0.21-0.31 us median); soak PASS
(`results/soak-2026-11-10-0939.md`). Summary and context: the research file, section 7.

## The API on one page

Everything is in namespace `vpipc`; full declarations in `include/vpipc/vpipc.h` and `marshal.h`.

**Endpoint** (one per side). The client calls `Endpoint::create_client(cfg, &ep)`: it creates the section
`Local\VPIpc.<clientPid>.<tag>` (tag `main` by default) and its wake events, starts the monitor thread and returns
at once; `ep->wait_for_server(ms)` waits until a server is connected. The server calls
`Endpoint::attach_server(cfg, &ep, timeoutMs)` with `cfg.sectionName` set (passed on its command line): it opens the
section, checks protocol version, layout hash and app schema hash (`cfg.appSchemaHash`, 0 on both sides = not
checked) and attaches; a mismatch is refused on both sides with `VersionMismatch` / `LayoutMismatch` /
`SchemaMismatch` and a readable text (`Endpoint::last_error_text()`). `close()` is the orderly end (the peer sees
`PeerClosed`). Hooks in `EndpointConfig`: `onLog`, `onPeerEvent` (`Attached`, `Died`, `Closed`, `Hung`, `Recovered`,
`Refused`); `set_resync_hook()` on the client. Timing in `EndpointConfig::wait` (`WaitConfig`): `replySpinUs` 100,
`idleSpinUs` 50, `tickMs` 100, `hungAfterMs` 5000, `callTimeoutMs` 0 (wait as long as the peer lives), `failOnHung`
false, `flushIntervalUs` 1000, `maxDepth` 64.

**Channels.** Three by default (`default_channels`), all initiated by the client:

| Index | Name | Thread | Shared |
|---|---|---|---|
| `kChannelUi` 0 | `ui` | the EXE's main/UI thread | no |
| `kChannelCore` 1 | `core` | the EXE's game-core thread | no |
| `kChannelAux` 2 | `aux` | any other client thread, serialised per top-level call by a client-side lock | yes |

Defaults per channel: `dataBytes` 256 KiB, `ringBytes` 64 KiB per direction (not yet sized against real traffic).
Up to 8 channels; a server-initiated one is a config row with `initiator = Side::Server`. A client thread picks its
channel with `ep->bind_current_thread(i)` (thread-local; an unbound thread uses the first shared channel) and gets
it with `ep->current_channel()`. Channels never block each other; the transport implements no game-core lock (that
is the server's job, PROTOCOL.md 2.3).

**Calls.** `Writer& w = ch.begin_call(method); w.i32(...); Reader r; Status s = ch.end_call(r);` On `Ok`, `r` reads
the RETURN payload, valid until this side's next send on the channel. `Writer`/`Reader` encode little-endian,
4-byte aligned, no type tags: `i32 u32 i64 u64 f32 f64 b`, `str`/`cstr` (null distinct from ""), `wstr`/`wcstr`,
`bytes`, `handle({type, id})`. A reader that runs past the end returns zeros and `ok()` turns false; check it once.
Large payloads are chunked transparently (tested to 50 MB).

**Handlers.** The serving side sets `ch.set_handlers(onCall, onPost, ctx)` and loops `ch.serve(timeoutMs)` (one
top-level CALL per return; posts are handled on the way) or `ch.poll()` (what is pending, no wait), one thread per
channel. `CallHandler(ctx, ch, method, args, ret)` writes its result into `ret`; to fail it calls
`ch.set_return_status(nonzero)` and writes a UTF-8 message, and the caller gets `RemoteError` with the code in
`last_remote_status()`.

**Nested calls.** A handler may call back over the same channel (`begin_call`/`end_call` inside the handler); the
other side services it inside its own wait. Both directions, to `maxDepth` 64 (depth 65 is refused with
`RemoteError`, the next call works).

**Posts.** `ch.begin_post(method)` + `ch.end_post()`: fire-and-forget into the channel's ring, payload at most
`ringBytes / 4` (16 KiB; larger is `PayloadTooLarge`). Delivered in order with the calls on that channel (the
receiver drains the ring before any synchronous message); no order across channels. Batched: published before the
next call, on `flush()`, before any wait, at 1/8 ring, or after `flushIntervalUs`. **End a burst of posts that is
not followed by a call with `flush()`**, or its last records stay invisible. **A post handler must not make a
synchronous call** on any channel: the peer is not waiting, so it could not be answered in turn; the library
returns `ProtocolError` (and asserts in debug builds). Anything whose handler must call back is a call, not a post.

**Statuses** (`Status`, `status_name()`): `Ok`, `RemoteError`, `PeerDied`, `PeerClosed`, `PeerHung`, `Timeout`,
`Disconnected`, `VersionMismatch`, `LayoutMismatch`, `SchemaMismatch`, `ProtocolError`, `PayloadTooLarge`,
`SystemError` (`last_error()` has the Win32 code and call), `InvalidArgument`. After a call fails with `Timeout`,
`PeerDied`, `PeerClosed` or `PeerHung` the channel stays broken until the next attach: every later call on it
returns the same status at once.

**Crash, hang and restart.** Each side's monitor thread writes a heartbeat and waits on the peer's process handle.
Peer death: every wait returns `PeerDied` within one tick (measured 2.5-3.1 ms for a server crash inside a handler;
the next call returns `Disconnected` in microseconds). Hang: the `Hung` event within `hungAfterMs + 3 x tickMs`;
calls keep waiting unless `failOnHung` (then `PeerHung` within `+ 4 x tickMs`) or `callTimeoutMs` is set; `Recovered`
if the heartbeat resumes. Client death: the server's waits return `PeerDied`; the library never exits a process, the
owner decides (the test server exits 10). Restart: after the server dies the client's top-level calls return
`Disconnected`; a new server attaches to the same section (generation + 1), and the first client thread that then
starts a top-level call runs the resync hook first (on that thread's channel only); measured kill to verified calls
113-114 ms, median of 21 restarts 123-125 ms (research 7.5). Who relaunches the server is the owner's decision. How the proxy uses all this (one server
per game, `on_server_death=exit` with exit code 3): `research/03-dll-engine-boundary/proxy-design.md` section 11.8.
