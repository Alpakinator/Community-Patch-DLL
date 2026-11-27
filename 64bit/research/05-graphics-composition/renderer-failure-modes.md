# Renderer failure modes: how the server's renderer fails and how the game ends (ticket 0600)

Ticket 0600, 2026-11-20 (account B). Decisions: D118 (the owner, 2026-11-14: when the 64-bit path fails, an error popup and a
diagnostic report, not a game with a broken world), D125 (the route: the report, the popup, the end of the game and
config.ini's restore of D119), D213 (MC, 2026-11-20, answer #7753 on thread 0600: a renderer that dies in play takes the
same route). Code: `server/src/render/srv_render_host.{h,cpp}`, `server/src/remote/srv_remote_main.cpp` (StartRenderer).

## 1. The gap (before 0600)

Found live by epic 0590's 4967 part 1 (`research/02-exe-internals/new-exe-build-26577625.md` section C.2a, 2026-11-20
03:29-03:44 EST, instance c5h-new-b with the developer switch): the renderer's init failed (RENDERER: the shader section
of another EXE build), render.log ended at `state STARTING -> FAILED reason RENDERER`, and the game reached the map and
played turn 0 to 1 with a grey map without terrain (D33 has the EXE's terrain off), no box, no report.

Cause [C, code read by the 0600 implementer 2026-11-20, matching C.2a's log lines]: `RunCpp` called `CreateDevice`; on
its failure `Fail()` logged `LOUD: renderer FAILED (...): ...; the game goes on without the server's terrain`, set
`VPFS_FAILED` and returned; the render thread left before `Loop()`, so none of the scene's fatal steps (overlay, fog,
decal, shadow, ice, units, models, forest: the existing D125 route through `HostConfig::fatal`) ever ran. The same held
for every other `Fail()` and for every refusal of `HostStart` and `ParseHostConfig` (logged, "the game goes on").
3301's design rule "Failure inside the render thread never ends the game" (`renderer-in-server-design.md`) was the
intent; it is corrected there (2026-11-20, D213).

## 2. Every failure, before and after [C, code; module tests in section 4]

The route ("D125") = `HostConfig::fatal(5, text)` once; in server.exe that is `SrvRemote::ReportAndExit(5, text)`: the
server's own report (its `FAILED:` line carries the text), `HostAbandon` marks the frame section, the process ends with
exit code 5; the proxy's next call fails (`server-death`, exit code 5 in the report's details, server.log's last FAILED
or LOUD line in report.txt), the proxy writes the D119 report folder and starts the popup ("the game had to end"), and in
a D33 session ends the game (`Fatal(3)`, D125). Exit code 5 is the code the scene's fatal steps already use ("the 64-bit
path cannot start its picture"). In a test host without a hook (`fatal == nullptr`) every case below is LOUD only and the
process goes on, as before.

| # | Where | Reason / trigger | Before 0600 | Now |
|---|---|---|---|---|
| 1 | `StartRenderer` (server.exe) | `ParseHostConfig` refuses a malformed or out-of-range `render_*` key of server.cfg | `render: LOUD: not started: server.cfg ...`, game goes on | the same line, then `ReportAndExit(5, "the server's renderer could not start: server.cfg <key>=<value>: ...")` |
| 2 | `HostStart` | "already started" (a second `HostStart` in one process) | refused, game goes on | unchanged (no route: the first renderer runs; StartRenderer runs once per kRt_Boot [I: one kRt_Boot per server process]) |
| 3 | `HostStart` | the D3D imports cannot be bound (`d3d11.dll`, `dxgi.dll`, `D3DCOMPILER_47.dll`) | LOUD `not started`, game goes on | the route: `the server's renderer could not start: the D3D imports could not be bound: ...` |
| 4 | `HostStart` | the version-3 section cannot be mapped (`Scene::attach`) | no LOUD line at all, game goes on | LOUD `not started` (new), the route |
| 5 | `HostStart` | `<base>.Frame` cannot be mapped | LOUD, game goes on | the route |
| 6 | `HostStart` | the section is served by another live server (pid) | LOUD, game goes on | the route (this process's renderer cannot draw) |
| 7 | `HostStart` | the unit scene refuses its options (`render_units=1`) | LOUD, game goes on | the route |
| 8 | `HostStart` | the model module refuses (`render_models`/`render_landmarks`) | LOUD, game goes on | the route |
| 9 | `HostStart` | `CreateThread` fails | LOUD, game goes on | the route |
| 10 | render thread, at init | VPFSF_ADAPTER: no adapter, or none with `render_luid` | `Fail()`, FAILED, game goes on (grey map) | `Fail()` then `FailFatalStep`: the route, text `FAILED at init (VPFSF_ADAPTER): no adapter; draws 0, ...` |
| 11 | render thread, at init | VPFSF_DEVICE: `D3D11CreateDevice` failed (its HRESULT and flags in the text) | as 10 | the route |
| 12 | render thread, at init | VPFSF_SECTION: the sink cannot open its section | as 10 | the route |
| 13 | render thread, at init | VPFSF_RENDERER: the scene's init (`terrain renderer: <c5r status>`): the EXE file missing, a PE without the `.shader` section, a truncated or damaged EXE, a blob not at its VA (another EXE build; after 0590's 4966 also ShaderSection's build check: a known build without its carried create-site array, an uncarried cell) | as 10 (the live C.2a case) | the route |
| 14 | render thread, at init | VPFSF_EXCEPTION: a C++ or SEH exception before the first READY (recorder start, device, scene init) | as 10 | the route; the text carries the exception (C++ `what()`, or the SEH code, address and module) |
| 15 | render thread, in play | the composite moved to another adapter and the device cannot be created there (ADAPTER/DEVICE/SECTION/RENDERER) | FAILED, game goes on | the route (D213), `FAILED in play (VPFSF_...)` |
| 16 | render thread, in play | VPFSF_REBUILDS: the 3rd lost device within 5 minutes | FAILED, game goes on | the route (D213); the text carries `lost devices 3, rebuilds 2` |
| 17 | render thread, in play | a rebuild's 6 creation attempts all failed (`rebuild failed: <text>`) | FAILED, game goes on | the route (D213) |
| 18 | render thread, in play | VPFSF_EXCEPTION: a C++ or SEH exception in the loop or a draw | FAILED, game goes on | the route (D213), the exception's text and `exceptions 1` |

The route's text (rows 10-18) is `the server's renderer FAILED <at init|in play> (VPFSF_<reason>): <Fail text>; draws N,
published N, lost devices N, rebuilds N, exceptions N`; render.log and server.log get `LOUD: D125 route: <text>` before
the call, after the `LOUD: renderer FAILED (<reason>): <text>; at init: the 64-bit path cannot draw the world, the server
ends (D118, D125: report and popup)` line (in play: `... in play: the world has no terrain from here, the server ends
(D118, D125, D213: ...)`). The call is made on the render thread after the device is released and the recorder is
finished (`ThreadProc`'s FAILED branch), once (`failFatalDone`). "At init" = before the first `VPFS_READY` (`initDone`).

Not failures, nothing ends [C]:
- **A lost device the renderer recovers from** (`VPFS_REBUILDING`, then `READY` again): no `Fail()`, no route (D213
  condition 1). **The limit, 3 lost devices within 5 minutes (`kMaxRebuildsInWindow`, `kRebuildWindowMs`), and 6
  creation attempts per loss (`kMaxCreateAttempts`), are numbers to revisit from testers' reports** (D213 condition 6):
  before D213 the limit only stopped drawing; now it ends the game.
- **A bad options file** (`VP64Options.ini`): the loader (`options/src/vp64_options.cpp`) warns per bad line and keeps
  the default (`BakedTileCameraRadius` -1, the whole map); the renderer starts. A missing or unreadable file: every
  option at its default (`server/src/srv_options.cpp`). So "a bad options file" in 0600's acceptance is no renderer
  failure; it is listed here so that nobody looks for it.
- **D132 R6 is unchanged**: a single hand-over's fault in play (a stale minimap frame, a missing arrow) is logged and
  counted and the game goes on. The scene's own fatal steps (a tap that cannot install at game start) keep their route.
- `render=0`: no renderer, nothing to fail.

What the player keeps and loses (D213 condition 3): the game ends at once; the moves since the newest autosave are lost.
0600 changes no popup or report text: the popup says "the game had to end" (proxy-design.md 22.2) and promises no save.
Whether an autosave exists at the moment of an in-play failure was not checked by 0600 (not verified).

## 3. The test key `render_test_init_fail` (TEST ONLY)

server.cfg `render_test_init_fail=adapter|device|renderer|section|exception` (`ParseHostConfig`; any other value is a
config refusal): the first `CreateDevice` is replaced by that reason (`render_test_init_fail=<REASON> (TEST ONLY)` as the
text), or a C++ exception is thrown in its place. Never in a product config. Live it reaches server.cfg through
vpproxy.ini `server_cfg_extra=` (also TEST ONLY, issue 0307). Rows 1, 10 (`render_luid`), 13 (`render_exe`) and 6 can be
forced without it, with real configuration values.

## 4. Module tests (`server/tests/render/test_render_host.py`, ticket 0600 block)

With the test host's `--fatal-hook 1` (prints `result: fatal hook code C: WHY`, returns, counts):
`test_init_failure_takes_d125_route` (rows 10-14 by the test key; one call, code 5, the reason, `draws 0`, the frame
block's state 4 and reason), `test_init_failure_without_hook_is_loud_only`, `test_init_fail_key_refused`,
`test_missing_exe_takes_d125_route`, `test_damaged_exe_takes_d125_route` (a PE without `.shader`, the first 64 KiB of the
EXE copied to a scratch file), `test_absent_adapter_luid_takes_d125_route`, `test_refused_start_takes_d125_route` (row 6;
the first server calls nothing), `test_in_play_rebuild_limit_takes_d125_route` (row 16), `test_in_play_exception_takes_
d125_route` (row 18, C++ and access violation), `test_recovered_device_loss_calls_nothing`,
`test_bad_options_file_is_no_renderer_failure`. Rows 3-5, 7-9, 15 and 17 have no module test of their own (no way to
make the mapping, the imports or CreateThread fail in the test host without a fault injector); they share
`StartRefused` / `Fail()` with the tested rows [C]. Row 1 is server.exe only (StartRenderer); it is covered by the live
check. Results: section 5.

## 5. Results

**Module tests [C, 2026-11-20, workstation].** Build `server/tests/render/build_render_tests.py --out
C:/c5-64bit-work/0600/rt` (clang-cl x64 /O2) at branch `task-0600-render-init-fail-popup`; `C5R_EXE` = the EXE of the
Goldberg instance c5h-i (build 4390913, read only). First run 04:57-05:00 EST (-k selection, 47 tests): 41 passed, 6
failed, all six in-play or READY-reaching hook tests: with the hook set, the scene's OTHER fatal steps (the fog: no game
install in the rig, "the cloud planes could not start"; the ice) fired first, and the fog's FAILED state blocks every
draw. Fixed in the tests by turning those modules off (`OFF = render_fow=0, render_overlay=0, render_ice=0,
render_shadow=0`, as `test_decal_wiring.py` does since issue 4310); not a product defect (in server.exe those steps have
their own D125 route). Rerun 6/6 passed (05:04). Whole file 05:29-05:34 EST: **76 passed in 278 s** (the 3301-3312 tests,
the refusals and the new 0600 block; no skip). After merging main (2211a7127, 06:21) rebuilt and run again 06:25-06:30:
76 passed in 283 s.

**ASan [C, 2026-11-20 05:36-05:42 EST].** `build_render_tests.py --asan` (MSVC /fsanitize=address), whole file: 75
passed, 1 failed, no ASan report: `test_tile_source_asks_for_another_tile_size` (a 3308 test 0600 does not touch, no hook,
no failure path) timed out waiting for `bake manifest seq 1 applied` inside its fixed `--seconds` bound while the
instance's align548/verify read F: and C:; run alone at 06:19 it passed (1/1, 6.7 s). A load-bound timing of that test
[I], not 0600's.

**Live check [L, 2026-11-20 05:47-06:17 EST, workstation].** Instance c5h-h (F:, Goldberg, build 4390913, EXE stamp
0x546CD0A8 checked; MC-B grant #7819), claimed by impl-0600, `provision.py align548` and `verify` (ok) first; F: read
latency ~0-0.6 ms (diskwatch). Layout: this branch's release build (`server/build_server.py --config release`, DLL pin
1fd51353e from a `git archive`; server.exe `be46d82f...`, proxy `f0351af3...` "repo 8747242f5 dirty=0"), the composite
of this branch (`56af4ada...`), `vp64helper on` (config.ini A'', `ServerRenderer = 1`), a TEST pack (`tools/release/
pack.py`, release `0600-test`, x86_dlls = c5h-h's own fallback DLL `3c2f47da...`) with vpproxy.ini `pack=1` as the
installer writes it, and vpproxy.ini `server_cfg_extra=` (TEST ONLY) per arm; a new Tiny game each launch
(`server/evidence/0600/prepare_tiny.py`); tools `server/evidence/0600/lay.py` (prep, helper, cfgextra, pack, collect,
restore). Without `pack=1` (first try, 05:54) the server side was the same (exit 5) but the proxy took
`on_server_death=exit` (exit 3) with no report and no popup: the shipped pack mode is what writes them (proxy-design.md
22.1); every arm below ran with it.

| Arm | Forced by (TEST ONLY) | Server (server.log) | Proxy / player | Popup (Visual Checks, DIAGNOSTIC) |
|---|---|---|---|---|
| A (05:59) | `render_exe=<a file that does not exist>` | `LOUD: renderer FAILED (RENDERER): terrain renderer: Io: cannot open file (error 2); at init: ... the server ends`, `LOUD: D125 route: ...`, exit code 5 at 1.6 s | report `stage server-death, code PeerDied`, "server pid ...: exit code 5", the server's reason; "a D33 session: no play, the game ends now (D125)", EXE exit 3 before the map; config.ini back to its hash before `on` (`63fff09d...`), vpproxy.ini `failed=1` | `vc-20261120-060145-workstation-5d14` |
| B (06:06) | `render_size=10x10` | `render: LOUD: not started: server.cfg render_size=10x10: WxH, 16..16384 each`, `FAILED: the server's renderer could not start: ...`, exit code 5 at 0.9 s | report `stage handshake, code boot-call` (the server ended inside kRt_Boot), the server's reason; no play (D125), EXE exit 3 | `vc-20261120-060836-workstation-7253` |
| C (06:12) | `render_test_device_lost=1`, `render_rebuild_backoff_ms=20` | READY, then at the first draws (the new game's load, 45.7 s) 3 lost devices: `LOUD: renderer FAILED (REBUILDS): 3 lost devices in 5 minutes; in play: ... the server ends (D118, D125, D213 ...)`, exit code 5 | report `server-death / PeerDied`, the reason with `draws 3, published 0, lost devices 3, rebuilds 2`; no play, EXE exit 3 | `vc-20261120-061353-workstation-d12f` |

Each time: the dialog watcher's marker (kind `patch`, `VP64HelperPopup`, buttons Open folder / Try again next start /
Close), the popup's first line "The game's 64-bit server stopped with this error: <the server's reason>", then "The 64-bit
part of the patch failed, so the game was ended. Your config.ini is back to your own settings ...". No grey-map game in
any arm; no other box; `crash.collect` found no crash. Each popup closed by the claim's owner after its game had ended
(`cli.py dismiss --button Close --patch-popup`, COMMON 29). Arm C's game had no autosave of its own (the failure came at
its turn 0 load), so what it shows about saves is only that none is promised: the popup text names no save [L].
Restore: `lay.py restore` (backup by SHA-256: every file `equal`; `set-proxy --original`; `<user>\VP64` moved to the work
folder), then `frontend.py restore` (the last arm's prepared front end) and `provision.py verify c5h-h` ok (full);
released 06:17. Work folder `C:/c5-64bit-work/0600/live/a` (logs, reports, pictures; not committed).

Not verified: rows 3-5, 7-9, 15, 17 live (they share the code path of the rows run); an exception in play live (module
test only); a real device removal (TDR) rather than the test switch; the player's loss in play with an autosave on disk.

## 6. Row 18 seen in a player's game: the ice tap's poll faulted after a save-mod-mismatch load (ticket 0741, 2026-11-25)

**The report** [C: the report folder of 2026-11-23 23:27:24, copied to the work folder; nothing of it is committed].
Beta 3 (cand5, cut d56fbd94f, DLL pin 1b87dd185, release pack Release-5.4.8), the owner's own game, EXE build 26577625,
modpack VP with 7 activated mods. server.log: `render: LOUD: renderer FAILED (EXCEPTION): SEH exception 0xC0000005 at
<image>+0x109769 ... (access violation); in play: the world has no terrain from here, the server ends`, then `LOUD: an
exception while releasing the device after a failure: objects leaked`, the D125 route text (`draws 0, published 0, lost
devices 0, rebuilds 0, exceptions 1`) and `server-fatal.txt written (exit 5, code render)`, all at 1449.518-1449.522 s.
The proxy then saw the server die by exception code 0xC0000005 (not exit 5) about 0.3 s later [I: the two logs' clocks
lined up by stream session 69], inside its call `ICvPlayer1::Read`; it discarded server-fatal.txt ("not this server's
reason", ticket 0641) and reported stage `server-death`, code `PeerDied`. No minidump.

**The faulting line** [C, 2026-11-25: the installed server.exe's SHA-256 `f8853558...a0c893` equals cand5's built
server.exe (`tier3/build/x64-1b87dd18/bin`, the pack's and the layout's copy); `llvm-symbolizer --relative-address
0x109769` over it and its PDB (`c08d4c7e...`); `llvm-objdump -d` of the function]: `srvrender::IceTapReader::poll`
(`server/src/render/srv_ice_tap.cpp:434`, the call of `SnapControl`), inlined `SnapControl` at line 59,
`const uint32_t s1 = *seq;`: the first read of the ice tap's control block (`Impl::block`, the server's own 4 KiB view
of `Local\VPShellIceTap.<exe pid>`) on the path where the block is already open. The instruction is the load through
the pointer read from `Impl` (`m_impl` at reader +0x28, `block` at Impl +0x38) right after its null test; the
`polls++` write into the reader one instruction earlier did not fault.

**What the renderer was doing** [C: render.log and server.log tails]. Nothing: the server had started at 23:03, the
renderer reached READY at 42 s (ring sink, taps' readers made, ice control section opened at 42.204 s) and had drawn
nothing the whole session (`draws 0, published 0`): the player was in the front end for 23 minutes. In the last 10 s
every module was WAITING or HIDDEN, the pass ran 10 times a second (its idle pace), and render.log has no line other
than the per-second counters. At 1444.3 s the player loaded a save; the server logged the DLL's game data hash
mismatch (`save mods: LOUD`, 0681: the save was made with other mods), 67 stream sessions of the load, at 1449.411 s
`the DLL reports a deserialization error (a sentinel of the save did not match)`, and 0.107 s later the render
thread's fault.

**Cause** [I, strong; no dump]: the DLL's misread of that save, on the server's main thread, wrote over memory of the
server process, and the idle render thread was the first to read a damaged pointer. Why not the renderer's own fault
[C, code at d56fbd94f]: `Impl::block` is written once, in `openControl` (42.204 s here), and freed only in
`~IceTapReader`, which runs only when `Scene::icePoll` sees a new EXE pid (no `ice: the EXE's pid changed` line) or
when the Scene ends; the reader is reached only through `Scene::m_iceSrc` on the render thread; the section is
`SEC_COMMIT` 4096 bytes (`shell/composite/ice_tap.cpp`), and the server's own view of it stays readable whatever the
EXE does with its handle. The same read had run without a fault since 42 s, in up to 126,927 passes (`passes_run`; one `icePoll` per pass [I]).
For the damage: three faults on two threads within about 0.3 s after 1400 s with none (the poll, the device release,
the process's end by 0xC0000005 with no `exception` line of the server's filter in the log tail), starting 0.1 s after
the DLL said its read was out of step. K29's cause (`../03-dll-engine-boundary/save-mod-mismatch.md`) in a second
signature: there the misread ran off the stream's end (a C++ exception, exit 6); here it damaged memory first.
What would confirm it: the server in direct mode (`server/run_server.py --load`, 0681's `repro_offline.sh` form) on a
copy of the same save with the crash session's database, under ASan or page heap, which names the DLL's writing
statement. The save is in the owner's saves and was not copied (0741 was read only).

**What the player saw**: the generic server-death popup, not K29's "this save was made with a different set of mods"
line, for two reasons [C, code at d56fbd94f]: (1) `OverrideFatal` (`server/src/remote/srv_remote_savemods.cpp`) leaves
every `render*` code alone ("a render module's end is the renderer's own failure, not the load's"), so the record said
`code render`; (2) the process then died by an exception before `TerminateProcess(5)`, and the proxy uses
server-fatal.txt only when the exit code matches.

**Beta 4 (dc259f0b1)** [C]: `srv_ice_tap.cpp` and `srv_remote_savemods.cpp` are byte-identical to d56fbd94f's,
`Scene::icePoll` is unchanged (same text hash), `deps/` is unchanged (same DLL pin, so the same save read), and no load
is refused. So the case is still reachable: a save loaded with mods that change the info tables can still end this
way, and in beta 4 a damaged pointer may surface in any thread (0711's frame jobs add render workers). The fix
proposal (MC-B, thread 0741) and the row: `../06-tooling-and-workflows/known-crash-causes.md` K31.

**The fix** (ticket 0744, 2026-11-25, offline [C], not yet in a beta): the server ends at the DLL's first
Deserialization Error of a load (exit 6, save-mod-mismatch); a render end after such an error is recorded as
save-mod-mismatch; the proxy uses a server-fatal.txt of this server's pid when the server then dies by an exception
code (`../03-dll-engine-boundary/save-mod-mismatch.md` 6).
