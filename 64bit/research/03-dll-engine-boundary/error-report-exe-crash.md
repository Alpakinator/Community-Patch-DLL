# D118 for an EXE crash: the server writes the report and starts the popup (ticket 0418, 2026-11-16, account B)

Ticket 0418 is proposal (1a) of 0410 (`research/02-exe-internals/load-sequence.md`, the weld loop at 0x77C1C0 that ends
about 1 of 31 huge product-path loads with no box). The EXE's own code cannot be fixed (CEG, D4), and D118 asks for a
popup and a report on every failure. Before this ticket the server only wrote `server-report.txt` and exited with code 3
when the EXE died. The proxy writes every other D119 report (proxy-design.md section 22), but it runs inside the EXE, so
an EXE crash takes it down too. From this ticket on, the server writes that report itself. Sibling ticket 0419 (the
proxy's chained exception filter, the harness's boxless-crash handling) extends this file.

Code: `server/src/remote/srv_exe_crash.{h,cpp}`. The wiring is in `srv_remote_main.cpp` (`OnAttach` after the
attach, `NoteBoot` at `kRt_Boot`, `OnPeerGone` before `ReportAndExit(3)` in every service thread) and in
`srv_remote_savevariant.cpp` (`NoteLoad` at the start of `ICvGame1::Read`). The helper's first line for the new stage
is in `proxy/helper/vp64helper.cpp`. Tests: `server/tests/remote/` (section 5). Live check: `server/evidence/0418/`
(section 6).

## 1. What the server does (as built) [C]

1. **At the attach.** The server opens its vpipc peer, which is the EXE because the proxy runs inside it:
   `OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION)`, plus `PROCESS_TERMINATE` only when `test_exe_kill`
   is set. The server keeps that handle for the life of the process. The exit code then stays readable after the EXE
   has ended, and a reused pid cannot mislead it. It also reads the image path (`QueryFullProcessImageNameW`) and logs
   `remote: exe-crash: watching the EXE (pid N, <path>)`. If the open fails, it logs one LOUD line and an EXE crash
   goes unreported.
2. **When the engine is gone.** `SrvRemote::ReportAndExit(3, ...)` is where every "engine is gone" path ends: a service
   thread's `serve()` failing, or a call the DLL made returning PeerDied, PeerClosed, Disconnected or PeerHung
   (`srv_rt.cpp`). It calls `SrvExeCrash::OnPeerGone()` first, inside the one exiting thread; every other thread that
   reaches `ReportAndExit` sleeps until the process ends. `OnPeerGone` is also safe from several threads at once: the
   first caller does the work, and the others wait at most 60 s and get the same result.
   **Corrected 2026-11-16 09:20 [L]:** the first build called `OnPeerGone` from the service threads before their
   `ReportAndExit(3)`. In the live run, the core thread was in a `kRtE_StreamRead` call (the save load's pull) when the
   EXE ended. It saw PeerDied in that call and went straight to `ReportAndExit(3, "the engine is gone (PeerDied in
   kRtE_StreamRead)")`, which ended the process while the service thread was still writing the report: there was no
   report.txt and no helper (section 7, run 1). The check now sits inside `ReportAndExit`.
3. **The exit code.** The server waits up to 5 s (`kPeerEndWaitMs`) for the EXE to end and then reads its exit code
   with `GetExitCodeProcess`:
   - If the EXE is still running after 5 s, the server logs "still running" and writes no report.
   - If the code is not a crash code, the server logs "not a crash code; no exe-crash report" and writes nothing new.
   - If the code is a crash code (section 2), the server goes on.
4. **The switch.** `exe_crash_report` in server.cfg is `auto` by default: the report is on when the proxy's
   `vpproxy.ini` says `pack=1` (the shipped layout). This is the same rule as the proxy's own reports, which exist only
   with `pack=1`. `0` and `1` force the report off or on. When the report is off, one LOUD line says why.
5. **The report folder.** The folder has the proxy's form (proxy-design.md 22.2) and goes under the same root:
   `<player folder>\VP64\reports\<yyyymmdd-hhmmss>[-n]\`.
   - `popup-summary.txt` reads: "The game itself crashed (exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION); the game
     had to end. The 64-bit server was still running and wrote this report."
   - `report.txt` has these sections:
     - `[failure]`: `stage: exe-crash`; `code: 0x%08X <name>`; the EXE's image and pid; when the EXE started and ended
       and how long it ran (`GetProcessTimes`); the time since the last save load began and the number of loads in this
       session; the time since `kRt_Boot` and since the attach; the first line of the proxy's crash record (section 4);
       and server.log's last FAILED or LOUD line (issue 0186's rule), which is the exe-crash line itself.
     - `[patch]`: the server's build line.
     - `[hashes]`: SHA-256 of the 32-bit fallback DLL, the x64 DLL, the proxy, server.exe and the EXE. The expected
       values are in `manifest.json` among the attachments.
     - `[session]`: the player folder and how it was found. The masks cannot be read once the EXE has ended.
     - `[exe]`: the PE stamp, compared with Steam build 4390913 (0x546CD0A8), the file size and the exit code.
     - `[mods]`: not read; `Lua.log` names the mods.
     - `[system]`: the Windows version, processors, memory, and each display with its driver and mode.
     - `[attachments]`, `[server minidump]` (any `*.dmp` is named, never copied) and `[left out]`.
   - The tails, the last 64 KB of each: every `*.log` in the run folder (`server.log`, `render.log`, `vpproxy.log`)
     and `<player folder>\Logs\Lua.log`.
   - Whole copies: `vpproxy.ini`, `manifest.json` (from server.exe's folder), the options file, `server.cfg`, and
     `exe-crash.txt` when it exists.
   - Every text goes through the proxy's scrub (the profile folder becomes `%USERPROFILE%`, the user name `<user>`,
     the computer name `<computer>`, and a SteamID64 `<steamid>`).
6. **The helper.** The server starts `<proxy dir>\vp64helper.exe popup --report "<folder>" --options
   "<vpproxy.ini>"` (`--options` only when that file exists). It starts the helper detached, in a new process group,
   and out of any job when the job allows it, as `proxy_session.cpp StartHelper` does. The helper inherits the
   server's desktop, which is the EXE's. The verb is always `popup`, never `failed --pid`, even in a D33 session
   (MC #2476): the crash is the EXE's own, so the patch stays on and config.ini stays as it is. If no folder could be
   made, the helper gets the root and its popup says that the report could not be found.
7. **The end.** The FAILED reason becomes `the engine is gone: channel N serve() returned PeerDied; the EXE ended with
   exception code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)`, and the process exits with code 3 as before. After a
   normal exit the reason is unchanged.

Where things are found:
- **The proxy folder.** server.cfg `proxy_dir`, or else the parent of server.exe's folder (D28:
  `<proxy dir>\VP64\server.exe`).
- **vpproxy.ini.** `VPPROXY_INI` (the proxy's own override, inherited from the EXE), or else
  `<proxy dir>\vpproxy.ini`.
- **The player folder** (the folder of config.ini, MODS and Logs), in this order:
  - `VPPROXY_TEST_CONFIG_INI`'s folder (the proxy's test override, inherited);
  - the run folder, when it has issue 0148's form `<player>\Logs\VP64\run\<boot>` (the shipped layout);
  - server.cfg `options_file`'s folder, unless `VP64_OPTIONS` moved the file;
  - `SHGetFolderPathW(CSIDL_PERSONAL)` of the server's own process.

  A test instance's Documents redirection hooks that last call only inside the EXE. That is why the run folder comes
  first: the proxy made it inside the EXE, so it already names the instance's folder.

## 2. Which exit codes count as a crash [C]

`IsCrashCode` decides:
- **Crash codes.**
  - Every NTSTATUS code of error severity (`(code & 0xC0000000) == 0xC0000000`), except `STATUS_CONTROL_C_EXIT`
    0xC000013A, which is a console close or Ctrl+C. This takes in 0xC0000005, 0xC0000409 (fail fast), 0xC00000FD,
    0xC0000374 and 0xE06D7363 (an unhandled MSVC C++ exception).
  - The three warning-severity exception codes `EXCEPTION_DATATYPE_MISALIGNMENT`, `EXCEPTION_BREAKPOINT` and
    `EXCEPTION_SINGLE_STEP` (0x80000002 to 0x80000004).
  - 26 codes have names; every other error code is called "NTSTATUS error".
- **Not crash codes.**
  - 0, the game's own quit.
  - 3, which is the proxy's own `Fatal(3)` / `on_server_death=exit` after a server death (and also a CRT `abort()`;
    that one comes with a box, which the dialog watcher handles).
  - 5 and 6, the proxy's other fatal codes.
  - 15, psutil's terminate in the harness's `stop`, and 1, `TerminateProcess(...,1)` in the tests.
  - 0x8000DEAD, which hardware breakpoints leave behind (proxy-design.md 23).
  - 0x40010004, the debugger's terminate.
- **Corrected 2026-11-17 (ticket 0519):** a code that is not a crash code, other than 0, still ends in this report when
  the proxy's crash record `<run>\exe-crash.txt` names this EXE's pid and was written after the EXE started. That is
  the case of Windows Error Reporting's "has stopped working" box, which ends a crashed EXE with **255** after "Close
  program" (found by 0515, `research/02-exe-internals/crashes-huge-load.md` 8.4). The report then takes its code from
  the record and says the exit code and the proof: `server-host.md` section 19. (Replaced: "these codes never write a
  report".)

## 3. TEST ONLY switches [C]

These are server.cfg keys. The proxy writes none of them; a live test sets them through vpproxy.ini
`server_cfg_extra=` (issue 0307).
- `test_exe_kill=attach|boot|load:<ms>[:<hex code>]`: `<ms>` after the event, the server ends the EXE with
  `TerminateProcess(code)`; the default code is C0000005. `load` is the start of `ICvGame1::Read`. Every step logs a
  `LOUD: TEST ONLY` line, and a malformed value is LOUD "not understood" and does nothing. This is the ticket's
  "test-only switch (not a real EXE patch)". A process that `TerminateProcess` ends with a code looks the same to the
  server as one that crashed with it.
- `exe_crash_helper_args=<text>`: appended to the helper's command line (LOUD).
- `proxy_dir`, `report_root`, `exe_crash_report`: the overrides from section 1. They are not marked TEST ONLY, but
  only tests use them.

## 4. The crash record of ticket 0419 (the contract) [C]

When a file `<run folder>\exe-crash.txt` exists, the server copies it, scrubbed, into the report and quotes its first
line under `[failure]` as `the proxy's crash record (exe-crash.txt, ticket 0419): <line>`. When there is none, the line
says "none in the run folder". 0419's proxy filter writes that file: the exception code, the faulting module and RVA,
and the thread id, one fact per line with the most useful line first, and no EXE code bytes and no memory (D83). The
server reads it after the EXE has ended, so the file must be complete before the process dies; the filter runs before
the process ends, so it is. **Since 2026-11-16 (ticket 0419) the proxy writes it: section 10** (format, limits, tests;
the end-to-end case through the real proxy and server.exe passes). **Since 2026-11-17 (ticket 0519) the record is also
proof:** when the EXE's exit code is not a crash code (255 after WER's box), the server reads the record's `process:`
line (must be the EXE's pid), its write time (not before the EXE started) and its `code:` line (the report's code);
`server-host.md` section 19.

## 5. Tests [L offline, 2026-11-16]

`python -I server/tests/remote/test_exe_crash_report.py --out DIR --build-out B [--asan]`. Start it through
`quietlock.py run`. Every program runs on a hidden desktop.
- **Unit.** `exe_crash_unit.cpp` links `srv_exe_crash.cpp` with stand-ins for SrvLog, SrvCfgStr and the like. It is
  clang-cl x64 with `/W3 /WX`, as the server's own code. It plays the server, with three threads that call
  `OnPeerGone` at once, and a child "EXE" that ends itself with a given code. Its 19 cases are listed in the script's
  docstring:
  - a crash, and a second crash into the same root;
  - normal exits with 0, 3, 0xC000013A and 0x77;
  - 0xC0000409 with `VPPROXY_TEST_CONFIG_INI` and the 0419 record;
  - the player folder from `options_file`;
  - pack=0, no vpproxy.ini, the report forced off, the report forced on;
  - no helper; a reports root under a file; `proxy_dir`;
  - the EXE still running;
  - `test_exe_kill` attach, boot (with its own code) and a malformed value;
  - the helper's extra arguments;
  - the code table.

  Every case checks the exit code, the files, server.log's lines and that no private string is left (the profile
  folder, the user and computer names and a SteamID64 are planted in the logs).
- **e2e.** The real `server.exe` from the build, with `exe_crash_standin.cpp` (a vpipc client, as `vp_selftest`). The
  stand-in starts the server, waits for the attach, and then ends with 0xC0000005, ends with 0, or holds while
  `test_exe_kill=attach:500` ends it. The checks: server.exe exits 3 every time; `server-report.txt`'s result names
  the EXE's code (or, after exit 0, does not); one report folder and one helper start after a crash; none after
  exit 0.
- **The real helper.** `B/proxy/bin/vp64helper.exe popup --report <folder> --no-ui` on the unit's report and on the
  e2e's report. Its first line is "The game itself crashed (the game's own process, not the 64-bit server), so the game
  had to end.", the server's summary line follows, and no line says "goes on".
- `proxy/tools/test_vp64helper.py`: the stage cases for exe-crash (report.txt with and without the summary, a longer
  stage name, report.txt winning over a server-death summary).

Results: see section 7.

## 6. Live check [L, see section 7]

Driver: `server/evidence/0418/live_0418.py` (prep / run / restore), built on 0359's driver (proxy-design.md 27.2).
- **Instance and layout.** c5h-c, a Goldberg instance on C: with its own stock config.ini, so the session is a stock
  session. The layout is a test pack from `tools/release/pack.py --layout <build>`, with the instance's own 32-bit DLL
  as its test-only x86 entry. `set-proxy` installs it with the build's proxy, and `manifest.json` and `purity.tsv` go
  into `VP64\`.
- **The helper.** A FAKE `vp64helper.exe`, which records its command line and shows nothing (COMMON rule 14). The real
  helper's text is printed afterwards with `--no-ui`.
- **The run.** `proxy/tools/live/arm.py` loads `C5H-3600-TINY_0008` (read only; its SHA-256 is compared before and
  after) with `pack=1` and `server_cfg_extra=<work>\extra.cfg`, which holds `test_exe_kill=load:1500`.

## 7. Results

### 7.1 Offline [L, 2026-11-16, workstation]

Build `C:/c5-64bit-work/0418/b`: `server/build_server.py --src` a plain copy of the pin 04b1211b8, `--deps` the pin's
release deps copied from 0408's folder, `--config release --offline`. It built OK in 178 s; server.exe `9edfdba2...`,
proxy `5881f483...`, x64 DLL `74588135...` (the pinned bytes). Every run went through `quietlock.py run`.
- `test_exe_crash_report.py --build-out b --asan` (09:20-09:22): **122/122**. That is 53 unit checks under clang, 5 e2e,
  2 real-helper checks, and the same unit cases under MSVC `/fsanitize=address`. The ASan canary was caught, and no
  run of the ASan build produced an AddressSanitizer report.
  - Before the fix of 1.2, the suite passed 66/66 (09:06). It had no case for a call's PeerDied; e2e `incall` now
    covers it. In that case the first engine call the DLL made back was `ICvEngineUtility1::InitGlobals()`, and the
    report was complete.
  - The case `proxy-dir` failed once (66/67). The cause was the test: the detached fake helper wrote its record after
    the check had read it. The test now waits up to 10 s for the record, and the no-helper cases also check that
    server.log has no "helper: started".
- `proxy/tools/test_vp64helper.py` (the stage cases with exe-crash added): **127/127**, and **252/252** with `--asan`.

Logs: `server/evidence/0418/offline/`.

### 7.2 Live [L, 2026-11-16, workstation, c5h-c, the pack=1 test layout of section 6]

**Run 1, 09:13-09:15: failed, the race of 1.2.**
- The server's switch ended the EXE 1500 ms after the save's read began. The server saw exit code 0xC0000005 and logged
  the exe-crash LOUD line.
- The core thread's PeerDied in `kRtE_StreamRead` then ended the server through `srv_rt.cpp`'s
  `ReportAndExit(3, "the engine is gone (PeerDied in kRtE_StreamRead)")` while the service thread was still copying the
  tails. The folder held the tails, `popup-summary.txt` and the copies, but no `report.txt`, and no helper was started.
- The real helper read that folder as a boot failure ("goes on as the normal (32-bit) game"), because report.txt was
  missing.

Evidence: `server/evidence/0418/live-run1/`.

**Run 2, 09:24:26-09:27:27, with the fix: 15/15 facts.**
- **The setup.**
  - The EXE started at 09:24:29 and the proxy booted the 64-bit path with pack=1 (pack check ok).
  - server.cfg had the proxy's `# TEST ONLY: server_cfg_extra` comment and `test_exe_kill=load:1500`.
- **The load and the kill** (times in server.log):

  | Time | What happened |
  |---|---|
  | 41.544 s | The save `C5H-3600-TINY_0008` began to load (`save variant: ... 22 ... loads`). |
  | 43.057 s | The server ended the EXE with 0xC0000005. |
  | 44.300 s | The server saw exit code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION), 1.24 s after the TerminateProcess. That is the time vpipc took to report PeerDied. |
  | 44.525 s | The report was written to `<instance Documents>\...\VP64\reports\20261116-092706` (report.txt and 10 attachment lines). |
  | 44.580 s | The helper was started as `vp64helper.exe popup --report "<that folder>" --options "<vpproxy.ini>"`, out of the server's job. |

  The report took 242 ms in all.
- **The server's result.** `result: FAILED: the engine is gone: channel 2 serve() returned PeerDied; the EXE ended
  with exception code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)`, exit 3.
- **report.txt.**
  - `[failure]`: `stage: exe-crash`, `code: 0xC0000005 EXCEPTION_ACCESS_VIOLATION`; the EXE ran 156.8 s; the save load
    began 2.7 s before the EXE was seen gone; `kRt_Boot` 42.7 s before.
  - `[exe]`: the PE stamp 0x546CD0A8 = Steam build 4390913.
  - `[system]`: the three displays with their driver.
  - Tails: server.log, vpproxy.log, stubcalls.log and Lua.log.
  - Copies: `vpproxy.ini`, `manifest.json`, `VP64Options.ini` and `server.cfg`.
  - Not attached:
    - `CvAssert.log`, with error 32 (a sharing violation): the DLL inside the server holds it open without read
      sharing.
    - `render.log`: there was none, because the server's renderer is off in this layout.
  - No path holds the user name (`%USERPROFILE%` in every text).
- **The helper and the instance.**
  - The fake helper recorded that command line.
  - The real helper's `--no-ui` text: "The game itself crashed (the game's own process, not the 64-bit server), so the
    game had to end.", then the summary line, the report's path, the hand-in line and the public-issue line, and the
    buttons "Open folder | Close".
  - The save was unchanged (SHA-256 before = after). The harness saw the EXE gone (wait rc 3), the start-up watch was
    ok and the guard was ok.
  - Restore: set-proxy --original, frontend restore, the backup restored by hash and `verify --quick`, all rc 0
    (09:28); the claim was released.

Evidence: `server/evidence/0418/live/` (facts.json, server.log, server-report.txt, server.cfg, helper-calls.txt,
helper-popup-noui.txt, the report folder, and the prep, run and restore logs). Paths are scrubbed to `%USERPROFILE%` by
`collect.py`.

## 8. Not verified

- The real popup window and its buttons: only the `--no-ui` text was checked, because a window may not be shown on the
  workstation.
- A real EXE fault, such as 0410's weld loop. The live check ends the EXE with `TerminateProcess(0xC0000005)`. The
  server sees the same exit code either way, but a real fault may leave a WER dialog or Windows Error Reporting may
  delay the process's end. ~~A process that WER holds longer than 5 s gets "still running" and no report [I]~~
  **Corrected 2026-11-17 (ticket 0519, from 0515's laptop crash, `crashes-huge-load.md` 8.1 and 8.4):** while WER's box
  holds the EXE, its channel does not fail (the server only logs "peer ... hung"), so the 5 s wait does not start; after
  "Close program" the EXE ends with exit code **255**, which needed the record as proof (`server-host.md` section 19).
  The 31-load series of 0410 saw no box.
- A D33 session: the code path does not depend on the session's kind, and the verb is always `popup`.
- The `SHGetFolderPathW` fallback for the player folder (tests never let it run: it would write into the real
  Documents folder).
- The report on a huge save (0410's case). The live check loaded a Tiny save; the code path does not depend on the
  save's size. On a huge save the tails are still capped at 64 KB each [I].
- The proxy's `exe-crash.txt` record: only test files have been read so far (the stackbuf case); 0419 writes the real
  ones. **Corrected 2026-11-16 (0419):** the real proxy's record has been read by the real server in the offline
  end-to-end case (section 10.4); on a game instance it is still not verified (10.5).
- The server ends with the EXE. A report that cannot be written within that process's life (for example on a full disk)
  leaves the "report: FAILED" line in server.log and the helper's "could not be found" popup [C, the bad-root case].

## 9. D118 for the server's DLL stopping on a failed check: stage `dll-fatal` (ticket 0422, 2026-11-16, account B)

Finding of 0392 (4510, #2482): a 3 s hover over plot 51,13 of the Tiny save HI4510-TINY_0008 (= C5H-3600-TINY_0008,
b2ceb663) with EUI from DLC UI_bc1 and mods cp,vp but without VP's EUI compatibility mod makes EUI's PlotHelpManager call
`Plot:IsResourceConnectedByImprovement(-1)`; `CvLuaPlot::lIsResourceConnectedByImprovement` passes -1 to
`CvGlobals::getImprovementInfo`, whose `PRECONDITION(eImprovementNum > -1)` stops the DLL. Stock 32-bit: the DLL's own
fatal "Error" box (CvGlobals.cpp:4244 in the 32-bit pin). Product route before this ticket: the game vanished with no
message. Ticket 0421 fixes the DLL's side (a guard in the Lua plot methods, fork branches for the next pin); this
ticket is the patch's side for every such stop. Code: `server/src/remote/srv_dll_fatal.{h,cpp}`, the shared report in
`srv_exe_crash.{h,cpp}` (`ReportServerFailure`), the context texts in `srv_rt.cpp` (`ServedCallsText`) and
`srv_remote_lua.cpp` (`LuaCallText`), the wiring in `srv_remote_main.cpp`, the helper's line in
`proxy/helper/vp64helper.cpp`, the proxy's skip in `proxy/src/proxy_rt.cpp` (`CallFailed`).

### 9.1 Why the product route showed nothing [C, 2026-11-16, fork 04b1211b8 and the 4510 run folder]

- The x64 DLL is built with `VP_OUT_OF_PROCESS`, which sets `g_bPopupsDisabledByUser = true` (CvGlobals.cpp, "The
  server has no UI and must never take focus or block on a modal dialog"). `CvPreconditionDlg` then returns at once
  (`AreErrorPopupsSuppressed()`), and `CvPreconditionFailedFormat` executes `BUILTIN_TRAP()` = `__builtin_trap()` = `ud2`:
  exception 0xC000001D.
- `CvPreconditionDlg` writes nothing to `CvAssert.log` (only `CvAssertDlg`, the non-fatal ASSERT, logs there); the 4510
  run folder's `CvAssert.log` is 0 bytes although `crashes.log` says "see CvAssert.log for the failed check". So the
  check's text exists nowhere on disk on this route (an upstream-worthy gap: the crashes.log line points at an empty file).
- `CvGlobals` init calls `SetUnhandledExceptionFilter(CustomFilter)`, which replaces the server's own filters (the
  remote mode's `Unhandled` and 3522's crash-dump filter) and does not chain: it writes `crashlogs\crashes.log`
  ("Crash class: Deliberate stop: a failed ASSERT/PRECONDITION ...") and its own minidump into the server's working
  directory (the run folder), shows no box (popups off), returns `EXCEPTION_EXECUTE_HANDLER`, and the process ends with
  0xC000001D. server.log has no line about it (4510: the last line is 10 minutes older than the stop).
- The proxy sees PeerDied and ends the EXE (`on_server_death=exit`, exit 3). With `pack=1` it would write a
  `server-death` report and start `vp64helper popup` (proxy-design 22.1), but without the check's text; the 4510 runs
  used a test layout with `pack=0`, so there was no report at all.

### 9.2 Reading the check at the trap [C, the 4510 minidump `crash-082444`, cdb, 2026-11-16]

At the `ud2` (`CvPreconditionFailedFormat+0x9e`, x64 DLL file offset 0x78D6BE) the registers were rbx = the expression
"eImprovementNum > -1", rdi = the file "CvGameCoreDLL_Expansion2\CvGlobals.cpp", esi = 0x10B7 = line 4279 (the x64 pin's
line; the 32-bit pin's box said 4244), r14 = the message ("" here). Both overloads of `CvPreconditionFailedFormat` end
the same way in the pin: `mov rcx,rbx; mov rdx,rdi; mov r8d,esi; mov r9,r14; call CvPreconditionDlg; ud2` (bytes
`48 89 D9 48 89 FA 41 89 F0 4D 89 F1 E8 rel32 0F 0B`). The argument registers are volatile, but their sources are
callee-saved and still hold the values at the trap. The server's reader (`ReadCheck`):
1. checks for `E8 rel32` just before the `ud2`;
2. walks back over register-to-register moves (REX + 89/8B with ModRM mod 11, or the 2-byte form) and keeps, for rcx,
   rdx, r8 and r9, the last move into each;
3. reads the sources from the exception's CONTEXT. The expression and file must point into the DLL's image (string
   literals) and be printable NUL-terminated texts, the file must end in a source extension, and the line must be
   1..10^6. Otherwise the report says that the text could not be read, and why.

Memory is read with `ReadProcessMemory` on the server's own process, so a bad pointer cannot fault.
**Limit [I]:** the moves are the compiler's choice. A rebuilt DLL (another pin, LTO) may set the arguments up
differently, and the report then says "the text could not be read (...)" and still gives the place (module + RVA). The
live check of each new pin shows which. **Negative result (2026-11-16):** a test stand-in whose dialog function had
internal linkage did not work. Clang folded its arguments into one constant, and no moves were left to read. The
fork's functions have external linkage (declared in CvGameCoreUtils.h), so this does not apply to the DLL.

### 9.3 What the server does (as built) [C]

1. **At kRt_Boot.** After the DLL loads, `SrvDllFatal::Install(hDll)` adds a first vectored exception handler and
   records the DLL's image range. server.log says "dll-fatal: vectored handler installed for CvGameCore_Expansion2.dll
   (image 0x..., N bytes)". A vectored handler runs before the DLL's `CustomFilter`, which never chains.
2. **The handler** acts only on EXCEPTION_ILLEGAL_INSTRUCTION at a `ud2` (0F 0B) inside the DLL's image. Everything else
   (an access violation in the DLL, a `ud2` elsewhere) passes untouched, and the existing routes stay as they were. It
   acts once per process. A second thread that stops at the same time waits up to 30 s until the first report is done,
   because the DLL's filter that it goes on to would otherwise end the process in the middle of that report.
3. **On the stopped thread:**
   - It reads the check (9.2).
   - It logs `remote: LOUD: dll-fatal: the DLL stopped on a failed check (exception 0xC000001D at a ud2,
     CvGameCore_Expansion2.dll+0xRVA ...): <expr> (<file>:<line>); Lua call: ...`. This is the line the report quotes
     as server.log's last FAILED or LOUD line.
   - It writes the D119 report through 0418's code: same folder form, root, switch (`exe_crash_report`, auto = vpproxy.ini
     `pack=1`), scrub and tails.
   - It starts `vp64helper popup --report <folder> --options <vpproxy.ini>`, the same verb in a D33 session (as MC #2476
     for exe-crash: a failed DLL check fails the same way on the 32-bit DLL, so turning the patch off would not help;
     MC agreed 2026-11-16).
   - With the report on, it writes `<run>\dll-fatal.txt` (first line "dll-fatal: the server wrote the D119 report for
     this stop (ticket 0422)", then `report: <folder>`, `check: ...`).
   - It marks server-report.txt FAILED with the check (`SrvRemote::Fail` + `WriteReport`).
   - It returns EXCEPTION_CONTINUE_SEARCH. The DLL's `CustomFilter` then writes `crashes.log` and its minidump as before,
     and the process ends with 0xC000001D.
4. **report.txt** has `stage: dll-fatal` and `code: 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION (a ud2: the DLL's deliberate
   stop after a failed check)`, then these lines:
   - `check:`, `file:`, `line:` and `message:` (or "check: the text could not be read (why)");
   - `how the check was read:` (the register moves, e.g. `rcx<-rbx, rdx<-rdi, r8<-rsi, r9<-r14`);
   - `where:` (module + RVA; crashes.log's "Location (in file)" is the RVA minus 0xC00);
   - `the Lua call that led there:` (the innermost kRt_LuaCall on the thread: the function's registered name, up to 8
     arguments described (numbers, booleans, strings cut at 60 characters, "a Plot object", "a table (n entries)"), and
     the channel);
   - `the engine calls being served on this thread (outermost first):` (OnCall's per-thread stack, e.g.
     `ICvGame1::Update > runtime::kRt_LuaCall`);
   - `thread:`;
   - then 0418's lines: the EXE and its start ("and was still running"), the times since the last load, kRt_Boot and the
     attach ("before the DLL stopped"), and server.log's last FAILED or LOUD line.

   The other sections are 0418's, with these changes:
   - `[exe]` says the EXE was still running and that the proxy ends it.
   - `[server minidump]` adds that the DLL writes `crashlogs\crashes.log` and its minidump into the run folder just after
     the report. They are not in the folder; the same privacy rule applies.

   popup-summary.txt: "The game stopped on an internal check in the game's DLL (dll-fatal: <expr> at <file
   name>:<line>); the game had to end. The 64-bit server wrote this report."
5. **The cost in the hot path.** OnCall pushes and pops the method id on a 16-deep per-thread array (two stores).
   kRt_LuaCall links a 5-word record on the stack into a per-thread list (two stores). The texts are made only when the
   DLL stops. No timing was measured [I: a few nanoseconds against a crossing of about 1 us].

### 9.4 The proxy and the helper [C]

- `proxy_rt.cpp CallFailed`: with pack=1, when `<run>\dll-fatal.txt` exists, the proxy logs "the server reported this
  failure itself (dll-fatal: its DLL stopped on a failed check; report: <folder>; ticket 0422): no second report and no
  second popup". It writes no report and starts no helper. A D33 session still ends with `Fatal(3)`, and
  `on_server_death` decides as before.
- `vp64helper popup`: for stage `dll-fatal`, the first line is "The game stopped on an internal check in the game's DLL,
  so the game had to end." The server's summary line follows it.

### 9.5 Tests [L offline, 2026-11-16, workstation]

`python -I server/tests/remote/test_dll_fatal_report.py --out DIR [--build-out B] [--asan]`, started through
`quietlock.py run`.
- **The stand-in.** `dll_fatal_unit.cpp` links `srv_dll_fatal.cpp` and `srv_exe_crash.cpp` with stand-ins for SrvLog and
  the like. `dll_fatal_standin.cpp`, a clang-cl /O2 DLL, has the PRECONDITION shape: a noinline failure function, a
  formatting call, the "dialog" call, then `ud2`. The unit installs the handler for the stand-in, then a filter like the
  DLL's `CustomFilter` (it records whether the report and the marker already exist and returns EXECUTE_HANDLER).
- **Cases:** check, msg, no-lua, twice and bare; and the negatives ok, av (0xC0000005 in the stand-in), own (a `ud2`
  outside the image), pack-off and no-helper (the script's docstring lists every check).
- **The real helper.** `B/proxy/bin/vp64helper.exe popup --no-ui` reads the check's report.
- `proxy/tools/test_vp64helper.py`: the dll-fatal stage cases. `proxy/tools/test_proxy_session.py`: `death-dll-fatal`
  and `death-dll-fatal-d33` (the loader writes dll-fatal.txt into the newest run folder and ends the real server with
  0xC000001D; the proxy writes no report and starts no helper, and exits 3).

Results: 9.7.

### 9.6 Live check [L, see 9.7]

`server/evidence/0422/live_0422.py` (prep / run / restore) is 0418's driver with one change: in place of
`test_exe_kill`, arm.py's `--probe server/evidence/0422/probe_4510.lua` runs `Map.GetPlot(51, 13):
IsResourceConnectedByImprovement(-1)` in the UI state, the call EUI's PlotHelpManager makes on the hover. Like the
hover's call, it crosses to the server as kRt_LuaCall. Set-up: c5h-c, the pack=1 test layout of this build (the DLL is
the pin 04b1211b8, which has no 0421 guard), the save C5H-3600-TINY_0008 (the same bytes as HI4510-TINY_0008), a FAKE
helper.

### 9.7 Results

**Offline [L, 2026-11-16, workstation].**
- **The build.** `C:/c5-64bit-work/0422/b`, made by `server/build_server.py --src` from a `git archive` of the pin
  04b1211b8, with `--deps` the pin's release deps (copied from 0420's folder), `--config release --offline`. OK in 154 s,
  and again in 99 s after `main` was merged (no warning with /WX).
- `test_dll_fatal_report.py --build-out b --asan`: **95/95**. That is 47 clang checks, 1 real-helper check, and 47 under
  MSVC `/fsanitize=address` with the canary caught. In the ASan build the `av` case is ASan's own deadly signal (exit 1)
  before any filter, and the test accepts that.
- The first run had 37/47. The stand-in's dialog had internal linkage (9.2's negative result); it was fixed in the test.
- The regression suites. Each passed on the first build, then again on the final build (10:51-10:56):
  - `test_exe_crash_report.py --asan` (0418's): 122/122.
  - `test_vp64helper.py` with the dll-fatal stage cases: 131/131.
  - `test_proxy_session.py`, with `death-dll-fatal` and `death-dll-fatal-d33`: 135/135.
  - `tools/harness/tests/test_safety_static.py`: 53 passed.

**Live run 1 [L, 2026-11-16 10:37-10:42, c5h-a, the pack=1 test layout of build b, the save copied as HI0422-TINY_0008].**
- **The setup.**
  - At kRt_Boot server.log logged "vectored handler installed for CvGameCore_Expansion2.dll (image 0x7FFFC3F70000,
    26292224 bytes)".
  - The save loaded; arm.py's probe ran 34.1 s after the load began.
- **The stop (server.log; the server's time 69.541 s).**
  - The LOUD line: `CvGameCore_Expansion2.dll+0x7869FE ... eImprovementNum > -1 (CvGlobals.cpp:4279); Lua call:
    Plot.IsResourceConnectedByImprovement(a Plot object, -1) on the core channel`.
  - The report folder was written by 69.664.
  - The helper started at 69.882 (out of the server's job). The report and the helper took 327 ms.
  - dll-fatal.txt was written at 69.908, and the hand-on to the DLL's filter came at 70.009.
- **After the stop.**
  - The DLL's own `crashes.log` said "Deliberate stop" and named its minidump.
  - At 70.663 vpproxy.log said "the server reported this failure itself (dll-fatal ...): no second report and no second
    popup", then "FATAL: the server failed a call (on_server_death=exit)". The EXE was gone.
- **report.txt.**
  - `[failure]`:
    - `check: eImprovementNum > -1`, `file: CvGameCoreDLL_Expansion2\CvGlobals.cpp`, `line: 4279`, `message: (none)`;
    - the moves `rcx<-rbx, rdx<-rdi, r8<-rsi, r9<-r14`, as in the 4510 dump;
    - `the engine calls being served on this thread (outermost first): runtime::kRt_LuaCall`;
    - the save load began 34.1 s before the stop.
  - The tails of server.log, vpproxy.log and Lua.log, and the copies.
  - One report folder.
- **The helper.** The fake helper was started once with that folder. The real helper's `--no-ui` first line was "The game
  stopped on an internal check in the game's DLL, so the game had to end."
- **The rest.** server-report.txt's result was `FAILED: the DLL stopped on a failed check: eImprovementNum > -1 ...`. The
  save was unchanged. Restore: set-proxy --original, frontend restore, the backup restored by hash, `verify --quick`, all
  rc 0. No box at any check.
- **19/20 facts.** The one miss was the driver's expectation, not the code. The driver expected the ui channel, but the
  Lua channel runs its chunk on the game's update thread, so the probe's call crosses on the **core** channel. EUI's
  hover made the same call on the ui channel (4510's vpproxy.log: "call rt:LuaCall (0xFFF00012) on channel 0"). The
  driver now accepts either channel.

**Live run 2 [L, 2026-11-16 10:44-10:49, the same set-up]: 20/20 facts.**
- **The stop.** At 68.785 s server.log has the same LOUD line (`+0x7869FE`, `eImprovementNum > -1 (CvGlobals.cpp:4279)`).
  The DLL's `crashes.log` says "Location (in file): CvGameCore_Expansion2.dll+0x785dfe" = the RVA minus 0xC00.
- **What followed.**
  - One report folder (`...\VP64\reports\20261116-104701`).
  - One helper start, `popup --report <that folder> --options <vpproxy.ini>`.
  - vpproxy.log: "the server reported this failure itself" and no "report: written", then FATAL (exit 3).
  - server-report.txt: `result: FAILED: the DLL stopped on a failed check: eImprovementNum > -1 (CvGlobals.cpp:4279) at
    CvGameCore_Expansion2.dll+0x7869FE ...`.
- **The real helper's `--no-ui` popup.** "The game stopped on an internal check in the game's DLL, so the game had to
  end." / "The game stopped on an internal check in the game's DLL (dll-fatal: eImprovementNum > -1 at
  CvGlobals.cpp:4279); the game had to end. The 64-bit server wrote this report." / the report path / the hand-in and
  public-issue lines / "Open folder | Close".
- **Restore.** All rc 0, as in run 1. The save copy was removed, `verify --quick` was ok, and the claim was released.

Evidence: `server/evidence/0422/` (live run 2 under `live/`, collected and scrubbed by `collect.py`; the offline logs
under `offline/`).

### 9.8 Not verified

- **The real mouse hover with EUI without VP's compatibility mod.** The live check makes the hover's call through the Lua
  channel (the same function, the same argument, the same DLL check; core channel in place of ui). The hover itself,
  replayed through the HI runner, was not run.
- **The real popup window.** Only the `--no-ui` text was checked (COMMON rule 14).
- **A real ASSERT.** In the server, ASSERT never stops: popups are off, so `CvAssertDlg` auto-ignores. Only the
  PRECONDITION / UNREACHABLE path (and other `BUILTIN_TRAP` sites, which get "the text could not be read") reaches the
  handler.
- **A D33 session live.** It is covered by the session suite's `death-dll-fatal-d33` case only.
- **CvAssert.log is not attached.** The DLL holds it open without read sharing (0418's finding), and it has no line for
  a precondition anyway (9.1).
- **No timing.** The cost of the per-thread stacks in the hot path was not measured (9.3.5).

## 10. The proxy's chained exception filter: `exe-crash.txt` (ticket 0419, 2026-11-16, account B)

Proposal (1b) of 0410. The proxy runs inside the EXE, so it cannot write a report when the EXE dies; it can, while the
process is dying, write the few facts the server cannot see from outside: where the fault was. Code:
`proxy/src/proxy_crash_filter.{h,cpp}`; wiring in `proxy/src/proxy_rt.cpp` (`Boot`: `Install` right after the run
folder and vpproxy.log exist, before the server starts; `DetachOnUnload`: `Uninstall`). Tests:
`proxy/tests/test_crash_filter.py` with the stand-in `proxy/tests/crash_filter_standin.cpp`, and the end-to-end case
`exe-crash` of `proxy/tools/test_proxy_session.py` (loader scenario `exe-crash` in `proxy/tools/rt_tests/loader.cpp`).
The harness's side (1c) is in `06-tooling-and-workflows/run-and-drive-the-game.md` 17.3.

### 10.1 What the proxy does (as built) [C]

1. **At the boot** (mode=on, every layout): `SetUnhandledExceptionFilter(Filter)` once per proxy image; vpproxy.log
   says `boot: exe-crash filter: installed (ticket 0419); record <run>\exe-crash.txt; it hands on to the previous filter
   <module>+0x<RVA>` (or "none: the system's default"). With `server_config=` there is no run folder: the filter writes
   only its log line. Before it returns, `Install` resolves `K32GetMappedFileNameA` (kernel32; psapi's
   `GetMappedFileNameA` as the fallback) and reads the EXE's preferred ImageBase from the EXE's **file** header.
2. **On an unhandled exception** (only when no debugger is attached: the system skips top-level filters under one), the
   first thread to arrive writes the record and one LOUD line; the filter then calls the filter it replaced and returns
   that filter's answer (no previous filter: `EXCEPTION_CONTINUE_SEARCH`, the system's default). A second thread that
   faults meanwhile waits up to 5 s for the record before it hands on (found by the stand-in case `two`: without the
   wait, the second thread's chain ended the process before the first thread had written its log line). A fault on the
   recording thread itself (a fault inside the filter) does not wait.
3. **The record** (`<run>\exe-crash.txt`, CRLF, one fact per line, under 2 KB). The stand-in's `ud2` case, 2026-11-16
   11:45:
   ```
   exception 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION at crash_filter_standin.exe+0x1000 (VA 0x00401000), thread 49724 (the proxy's boot thread)
   code: 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION
   where: crash_filter_standin.exe+0x1000
   module base: 0x00D60000 (the EXE; its file's ImageBase 0x00400000)
   rva: 0x00001000
   va at the EXE's ImageBase: 0x00401000 (the knowledge base's address form)
   thread: 49724 (the proxy's boot thread)
   process: 45144
   time: 2026-11-16 11:45:51.103 (local)
   flags: 0x0
   written by: the proxy's chained unhandled-exception filter (ticket 0419); no code bytes, no stack and no memory of the process (D83)
   ```
   Other lines: `access: read of|write to|execution at 0x...` for 0xC0000005 and 0xC0000006 (with the in-page NTSTATUS);
   `address: 0x...` when the address is in no module ("an address in no module (0x...)" in the first line); `flags: 0x1
   (noncontinuable); a nested exception record follows (not read)`. The first line is what the server quotes under
   `[failure]` and what the harness's signature uses (10.3). The VA form is given only for the EXE; a DLL's place is
   module + RVA. Module names are file names only (no path, nothing private).
4. **vpproxy.log**: `[ t ] [tid] LOUD: exe-crash: an unhandled exception in the game's process (ticket 0419): <first
   line>; record <path>; handed on to the previous filter` (or "the record ... could not be written (error N)", or "no
   previous filter: the system's default handling follows"). The line is written straight to the log handle (one
   `WriteFile`, no lock: the crashed thread may have held the log lock).
5. **At the unload** (FreeLibrary at a mod change: the EXE unloads the game DLL, proxy-design.md): `Uninstall` drops the
   log handle (the proxy closes it next) and puts the previous filter back when the proxy's is still on top. When a later
   filter replaced it, that one stays, and the log says LOUD that a later filter which chains to the proxy's would call
   into the unloaded image. A new load of the proxy installs again (fresh statics).
6. **What the filter may do** (the process is crashing): no heap, no CRT formatting (its own appenders into static
   buffers), no loader lock (the module from `VirtualQuery`'s allocation base, its name from `K32GetMappedFileNameA`; never
   `GetModuleFileName`/`GetModuleHandleEx`), small stack (the 2 KB record and the log line are static), and **no read of
   the process's memory at all** (D83). File work: `CreateFileA` + `WriteFile` + `FlushFileBuffers` + `CloseHandle`
   (`FILE_FLAG_WRITE_THROUGH`), so the record is complete before the previous filter can end the process.

### 10.2 Findings and limits [L stand-in, 2026-11-16, workstation]

1. **Fail fast never reaches a filter.** `int 0x29` (`__fastfail`, what /GS's 0xC0000409 uses) ends the process with
   0xC0000409 and calls no top-level filter: no record, no previous filter. The server still reports the code from the
   exit code; its report then says "none in the run folder" for the record. Heap corruption that Windows ends by fail
   fast (0xC0000374) is the same [I].
2. **An unhandled C++ exception** (0xE06D7363) is recorded at `KernelBase.dll+0x...` (`RaiseException`), not at the
   `throw`: the place says only that a C++ exception escaped.
3. **The mapped PE header's ImageBase is the load address, not the preferred one.** The Windows loader rewrites
   `OptionalHeader.ImageBase` in memory to where it mapped a relocated (ASLR) image. The first build read the mapped
   header and wrote "(VA 0x00A91000)" and "its ImageBase 0x00A90000" for loader.exe loaded at 0x00A90000 (e2e case,
   11:41); the filter now reads the EXE's **file** header at Install (0x00400000 for the stand-ins, as for the real DX11
   EXE) [C, L stand-in 11:45].
4. **A stack overflow is recorded**: the filter ran on what was left of the overflowed thread's stack (stand-in
   `stack`, 0xC00000FD, record and log line complete).
5. **A filter installed after the proxy's that does not chain replaces it**: the 32-bit VP DLL's `CvGlobals::init`
   (`CustomFilter`, crashes-huge-load.md 4) when the proxy falls back to it. Then the stock "Game Crash" box comes back,
   which is the stock behaviour.
6. **Where it sits on the real EXE [I, order from 02-exe-internals/crashes-huge-load.md 4]:** the EXE's static
   initialiser installs its filter 0x42DEA0 at start, Goldberg's crash printer installs its own at the emulator's settings
   parse (test instances), and the proxy's comes last, at `DllGetGameContext`; so the chain is proxy -> Goldberg's printer
   -> the EXE's 0x42DEA0 -> (none). Not seen on an instance yet: the install line names the previous filter, so the first
   product-path boot with this proxy shows it.

### 10.3 What reads the record

- The server (section 4): quotes the first line under `[failure]` and copies the file, scrubbed, into the report.
- The harness (run-and-drive-the-game.md 17.3): `dialogwatch.proxy_record` finds `<player>\Logs\VP64\run\<stamp>-<pid>-<n>
  \exe-crash.txt` by the game's pid (the run folder's name carries it, `proxy_rt_logic.cpp RunFolderName`) and builds the
  crash's ledger signature from the first line's place: `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION at
  CivilizationV_DX11.exe+0x37C6DA`, so repeated crashes at one place count as one bug.

### 10.4 Tests and results [L offline, 2026-11-16, workstation]

Build `C:/c5-64bit-work/0419/b`: `server/build_server.py --src` a copy of the pin 04b1211b8 (0420's `src-x64f`), `--deps`
the pin's release deps (0420's `deps-release`), `--config release --offline`, OK in 231 s (proxy `059b3e68...`, the
first build); after the ImageBase fix (10.2 point 3) the proxy was rebuilt with `proxy/build_proxy.py --gen b/gen`.
Every run went through `quietlock.py run`.
- `python -I proxy/tests/test_crash_filter.py --scratch DIR` (clang-cl -m32, VC9 headers, /MT, /W3; ASan x86 for the
  unit mode): **124/124** (11:45). Unit: the code table, `DescribeAddress` (the EXE with the file's ImageBase, kernel32,
  a heap address, NULL, a cut buffer), `FormatRecord` (first line, every line a known fact, access line, rva, thread,
  flags, an unknown code in no module, noncontinuable and nested, in-page error, no record, a cut at cap - 1), `Install`
  (the chain, a second call, a too-long path), `Uninstall` (the previous filter back; a later filter left in place, LOUD).
  Fault cases, each in its own stand-in on a hidden desktop with a "previous" filter that returns
  `EXCEPTION_EXECUTE_HANDLER`: av-read, av-write, av-exec (no module), ud2 (exact RVA and VA), dll-ud2 (exact DLL RVA, no
  VA), div0, cpp, stack, two (one record, one log line), and the negatives nolog, badpath (error 3 in the log, the chain
  still runs), uninstalled (no record, the previous filter alone), noprev (the system's default ends the process with the
  code, no WER box under `SEM_NOGPFAULTERRORBOX`), fastfail (no record, 10.2 point 1). Each checks the exit code, that the
  previous filter ran with the code, the record and the log line. The first full run had 114/117: the unit's module names
  (resolved only at Install; now also on the first `DescribeAddress` outside the filter), `address:` missing from the
  test's list of known lines, and the second-thread race of 10.1 point 2.
- `proxy/tools/test_proxy_session.py --build b --asan`: **147/147** (11:40, first build), with the new case `exe-crash`:
  the loader installs its own filter (the EXE's), boots the real proxy with the real server.exe (pack=1), then executes a
  `ud2` at loader.exe+0x1000. The process ended with 0xC000001D after the previous filter ran; the proxy had logged the
  install line naming `loader.exe+0x...` as the previous filter; `<run>\exe-crash.txt`'s first line named
  `loader.exe+0x1000`; the server logged its exe-crash LOUD line 0.1 s after kRt_Boot, wrote one report folder with
  `stage: exe-crash`, `code: 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION`, the line `the proxy's crash record (exe-crash.txt,
  ticket 0419): exception 0xC000001D ...` and a copy of the record, and started the fake helper once with `popup --report
  <that folder>`; the proxy wrote no report of its own; no report file holds the seeded user or computer name. The first
  run failed 2 of the test's own checks (it split the record on CRLF after reading it in text mode).
- `proxy/tools/test_proxy_rt.py --build b`: **106/106** (11:43, first build; the loader's boot-orderly, boot-abrupt and
  reload scenarios unload the proxy, so `Uninstall` runs there).
- On the rebuilt proxy (the ImageBase fix): see 10.6.

### 10.5 Not verified

- **A real EXE fault on a game instance.** No live run: the real EXE cannot be made to fault without an EXE code patch
  (D4), and no TEST ONLY fault switch was added to the proxy. The e2e case has the same chain with the loader as the EXE.
  The first real crash on the product path (0410's weld loop, 1 in 31 huge loads) will show the record.
- **The previous filter on a real instance** (10.2 point 6) and whether Goldberg's printer still writes its line after
  the proxy's filter (it is called, by the chain).
- **WER**: with the EXE's filter returning `EXCEPTION_CONTINUE_SEARCH`, Windows' default handling follows; 0410 saw no
  WER record for the real crash, and the filter does not change that.
- **A crash after a mod change's unload and reload** of the proxy (the stand-in checks `Uninstall` and a second
  `Install` in one image; the loader's reload scenario passes).

### 10.6 Results on the final proxy [L offline, 2026-11-16, workstation]

The proxy rebuilt after the ImageBase fix (`build_proxy.py`, OK in 59 s, `2b3056c4...`), with the first build's
server.exe and gen:
- `test_proxy_session.py --build b --asan`: **147/147** (11:47-11:52); the `exe-crash` case's record now reads
  `exception 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION at loader.exe+0x1000 (VA 0x00401000), thread 59384 (the proxy's
  boot thread)` with `module base: 0x00260000 (the EXE; its file's ImageBase 0x00400000)`: the loader was mapped at
  0x00260000 by ASLR and the VA is the file's form.
- `test_proxy_rt.py --build b`: **106/106** (11:52-11:53).
- `test_crash_filter.py`: 124/124 (11:45, 10.4).
