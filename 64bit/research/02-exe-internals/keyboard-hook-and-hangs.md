# The EXE's global keyboard hook and the patch's main-thread hangs: the owner's whole keyboard lags

Found by the MC 2026-11-23 22:29-22:38 EST on the workstation, from the owner's report [reworded for privacy]: the game intermittently and constantly
took focus briefly and interrupted the keyboard buffer, so typed text in other programs is lost; the owner suspected the
human-input test paths. Ticket 0720.

## 1. What was measured `[L]`

Running: the owner's own game, beta 3 (`VP64-Patch-0.1b3-std-only-for-VP5.4.8`) in the Steam install (pid 44488, no
focus guard: `Local\CivBgGuard_<pid>` absent), its server.exe from `MODS\(1) Community Patch\VP64`; the hidden test
instance c5h-a (0715's quiet hour, pids 27784 then 38192 then 52760, focus guard present) driven by
`server/evidence/0691/drive0691.py` (PostMessage only, tools/hi/actions.py); a build and the usual waiters.

- **Foreground and new windows** (a WinEvent hook, EVENT_SYSTEM_FOREGROUND and top-level EVENT_OBJECT_SHOW, 22:30-22:38,
  plus GetForegroundWindow polled every 10 ms): no test instance, harness tool, build or console window took the
  foreground. c5h-a's relaunches showed its window (22:30:52, 22:35:50) without activation. Every foreground change
  was the owner's own (Discord, Task Manager, Search, Medal, the beta 3 game) or **DWM's `Ghost` window for the beta 3
  game**: at 22:33:01 the game stopped answering while it was the foreground window, DWM showed a ghost and the
  foreground moved to it; more ghosts at 22:33:26 and 22:33:35.
- **Message responsiveness** (SendMessageTimeout WM_NULL every 250 ms, 5 s limit, 22:32:29-22:33:43): the beta 3
  game was hung (`IsHungAppWindow`) 22:33:03-22:33:21, 22:33:21-22:33:26, 22:33:30-22:33:35 and answered in 1.5-3.3 s
  at 22:33:38-22:33:42; 7 of 140 samples over 300 ms. c5h-a hung 3-5 s at a time during its load (22:32:29-22:33:06).

## 2. Why that breaks typing in every other program `[C]` + `[I]`

Civ 5 installs a global `WH_KEYBOARD_LL` hook (it swallows Alt+Enter in every application; tools/harness/native/src/
focus_guard.cpp header, the guard logs `refused SetWindowsHookExA(WH_KEYBOARD_LL)` twice at each launch, e.g.
research/03-dll-engine-boundary/evidence/0359/live/guard-48032.log). A low-level hook procedure runs on the thread that
installed it, through its message loop: while that thread does not pump messages, every keystroke in the whole session
waits up to `LowLevelHooksTimeout` before it reaches any program, and keys pile up or are dropped `[I]` (Windows
documentation; not set on the workstation, so the system default). The focus guard refuses the hook on test instances
for exactly this reason; the owner's real game has no guard, so the beta's main-thread stalls of seconds reach the owner's
keyboard everywhere, and the ghost windows move the focus while the game is in front.

Which wait holds the EXE's main thread for 5-20 s: section 5 (corrected 2026-11-23 by 0720; this line said "not
measured yet"): starting the game, the map load, the InGame UI load and leaving, not D242's lockstep wait. Whether stock
32-bit VP stalls the same way in the same situation: yes on Tiny and on Huge (section 5, tickets 0720 and 0722; this line
said "open until the live arm" until 2026-11-24); the owner's one 20-26 s frame after the load screen's button is not
reproduced in a hidden test game, stock or patch (section 5, 0722).

## 3. The EXE's hook: where it is installed, what it does, and the stock leak `[C]`

Read 2026-11-23 22:44-22:50 EST by ticket 0720 (implementer a60236f1): a headless Ghidra decompile of build 4390913
(runbook 3.x, `-readOnly -noanalysis`, output in scratch only, D83) and a byte read of the call sites in both golden
copies (`F:/c5-64bit-work/golden/civ5-steam-build{4390913,26577625}-ws/CivilizationV_DX11.exe`). The call sites, the
structure offsets and the hook procedure are the same in both builds (only the addresses move).

| What | Build 4390913 |
|---|---|
| import slot USER32!SetWindowsHookExA | 0x9D69A4 |
| import slot USER32!UnhookWindowsHookEx | 0x9D6938 |
| import slot USER32!CallNextHookEx (used only by the hook procedure) | 0x9D6A04 |
| the hook procedure (`LowLevelKeyboardProc`) | 0x81E040 |
| install 1: window create (the function that registers `WinClass_FXS` and calls `CreateWindowExA`) | call at 0x81DEDC |
| install 2: the window procedure's `WM_ACTIVATE` handler, `LOWORD(wParam) != 0` | call at 0x81DF47 |
| unhook 1: the same handler, `LOWORD(wParam) == 0` (deactivated) | call at 0x81DF56 |
| install 3: after a `SetWindowPos(hWnd, 0, ..., SWP_NOMOVE \| SWP_NOZORDER)` of the main window, then `CoInitialize` | call at 0x61A3BA |
| unhook 2: the main message loop at `WM_QUIT` | call at 0x81DD1B |

- **The handle lives in the app object**: every install stores its result at `[g_Civ5App] + 0x7C` (`cam_app` in the build
  table, 0xCC8E74 on 4390913; the 26577625 values stay local, D202); the install passes `[g_Civ5App] + 0xA0` (the EXE's HINSTANCE) as hMod; `[g_Civ5App] +
  0xA4` is the main window (`WinClass_FXS`). Every install is `SetWindowsHookExA(WH_KEYBOARD_LL = 13, proc, hInstance, 0)`.
- **The procedure**: `nCode >= 0 && wParam == WM_SYSKEYDOWN (0x104) && vkCode == VK_RETURN (0x0D)` returns 1 (the key
  is swallowed); everything else goes to `CallNextHookEx(NULL, ...)`. It does not look at the foreground window: while
  it is installed, Alt+Enter is swallowed in **every** program of the session, not only in the game.
- **The leak**: no install unhooks the previous handle first, and the deactivation unhook does not clear the field. So
  every install that follows another without a deactivation in between (create then first activation; install 3 at any
  time) overwrites a live handle, and that hook stays installed on the main thread for the life of the process (or until
  Windows drops it, below). On a normal start the window is created, then activated: at least one hook leaks before the
  game DLL (and so the proxy) is even loaded; install 3 leaks another when it runs while the game is active. `[C]` for
  the code; how many leak on a real start is `[I]`: 0720's live check could not count them (on a hidden test game the
  focus guard refuses every low-level hook and the window is never activated; an unguarded game would show it).
- **Consequence**: in stock too, a global low-level keyboard hook sits on the EXE's main thread whether the game is in
  front or not; every keystroke of the session passes through that thread. It costs nothing while the thread pumps
  every frame and everything while it does not. Windows' documentation of `LowLevelKeyboardProc` says that on Windows 7
  and later a hook that exceeds `LowLevelHooksTimeout` is removed silently; whether that bounds the owner's loss to one
  timeout per installed hook is `[I]` until measured (ticket 0720's live check).
- **What the guard log's two refusals were** (`refused SetWindowsHookExA(WH_KEYBOARD_LL)` twice at each launch): with
  the focus guard the window is never activated, so they are install 1 and install 3 `[I]` (from the table above).
- **The proxy loads too late to catch the startup installs**: the game DLL slot (the proxy) is loaded at mod activation
  in the front end, after the window exists and its hooks are installed (the guard log of 0359: the refusals at
  09:25:23, the DLL's first calls at 09:26:17; `ui-queue-guard-product.md` section 3). An import-slot thunk therefore
  sees only the installs after the boot (every activation, install 3 if it runs again); the startup ones must be taken
  over by handle.

## 4. The fix (ticket 0720, Part A): `proxy/src/proxy_kbhook.cpp`

Data writes only (D228): two of the EXE's import slots, USER32!SetWindowsHookExA and USER32!UnhookWindowsHookEx, found
by name in the import directory, point at thunks; no EXE code byte is read or written, no build-table row was added
(`cam_app` is already carried on both builds). Installed from `ProxyShell::OnBoot` (mode=on only; the EXE's main
thread, `DllGetGameContext`).

- **Thunks**: `SetWindowsHookExA(WH_KEYBOARD_LL, ..., threadId 0)` from the EXE returns a pseudo handle (an address in the
  proxy's image) and sets the request on; every other hook type and every thread hook is forwarded to the slot's previous
  value. `UnhookWindowsHookEx(pseudo)` (or of the handle taken over at boot, once) sets the request off; other handles
  are forwarded. Three installs in a row give one helper hook: the stock leak cannot happen after the boot.
- **The helper thread** does nothing but `GetMessageW`; it installs its own `WH_KEYBOARD_LL` hook (hMod = the proxy's
  image) while the request is on and removes it when it goes off. Its procedure is the EXE's rule with one change the MC
  asked for (#12349): Alt+Enter is swallowed only while the foreground window belongs to the game's process; with another
  program in front it passes (stock swallowed it everywhere while its hook was installed: a stock bug, dropped).
- **At boot**: the handle at `[g_Civ5App] + 0x7C` is removed with the real `UnhookWindowsHookEx` on the main thread (the
  thread that installed it); the request is on when the game's window is the foreground window. Layout checks before any
  write (pattern-free): known build, `cam_app` carried, the app object readable, `[app] + 0xA0` == the EXE's base,
  `[app] + 0xA4` a window, both slots found by name, bound and not already the thunks. Hooks the EXE leaked before the
  boot (section 3) have no readable handle and stay until Windows drops them `[I]`.
- **Ghosting**: `DisableProcessWindowsGhosting()` once at boot (MC #12349): a main thread that does not pump for 5 s no
  longer gets a DWM ghost window, which took the foreground in the owner's session (section 1).
- **Unload** (FreeLibrary at a mod change, loader lock): both slots given back by compare-and-swap (a hook another
  module wrote on top is left alone), the helper removes its hook and is terminated (as the view-mask watcher).
- **Log** (vpproxy.log): one line at boot (`kbhook: the EXE's low-level keyboard hook runs on the proxy's helper thread:
  active (slots ...; at boot the EXE's hook 0x... removed; the game was/was not in front)`), one ghosting line, then at
  most one change line per 10 s (requests, releases, Alt+Enter swallowed / passed, forwarded calls, key events and the
  largest hook lag seen by the helper, helper install errors); a failed helper install is one LOUD line.
- **Offline result** (2026-11-23 23:0x-23:1x EST, workstation, branch task-0720-keyboard-hook-stall): `python -I
  proxy/tools/test_proxy_rt.py --only-kbhook --asan`: 9/9 (unit 136/136 under clang-cl -m32 and under MSVC ASan x86, the
  negative path, the canary); `--only-units --asan` (the units and arc programs that link `proxy_shell.cpp`): 148/148;
  `build_server.py` with the proxy (pin 1b87dd185, release): OK, the proxy's import and purity checks passed.
- **A timing check of the unit that can fail once under load** `[L]` (ticket 0752, 2026-11-25 06:47, workstation, a
  debug `build_server` output, other teams' jobs running): in `test_proxy_rt.py`'s `kbhook unit (negative,
  --expect-fail)` run, `rt_tests/kbhook_unit.cpp` line 435 (`TestReport`: after a failed helper install,
  `KT::WaitSynced(2000)` then `KT::HelperHookErrors() == 1 && !KT::HelperHooked()`) failed besides the intended FAIL, so
  the check "one FAIL line" failed (234/235); the positive run of the same unit a moment earlier passed 136/136, and the
  next whole suite passed 235/235. Not looked into further [I: the helper thread had not counted the error within the
  wait]; a repeat is the first thing to try when this check alone fails.

- **Live check, first try: a crash of the test set-up, not of this module** `[L]` (2026-11-23 23:39:31 EST, c5h-new-b,
  build 26577625, pid 54012): the layout was laid without the composite (`lay.py prep` without `--composite-dll`) while
  `helper on` set `ServerRenderer = 1` in a D33 session, so the proxy's boot failed by design (ticket 0624: `boot FAILED:
  ... shell_composite.dll is missing ... DllGetGameContext returns NULL`; no `pack=1`, so no fallback) and 11 ms later
  the EXE raised 0xC0000005, a read of address 0 at an EXE offset of build 26577625 (kept local, D202) on the same (boot) thread. The kbhook
  module never ran (`OnBoot` is not reached on a failed boot; no kbhook line). That the faulting read is the EXE's use of
  the NULL game context is `[I]` (the order of the two lines, the address 0); a disassembly of build 26577625 at that
  site would confirm it. Filed by the MC as ticket 0721 (known crash cause K30, D118/D223). Trap for the next live check: a product-route layout with `helper on` needs the composite
  (`shell/evidence/0720/live_0720.sh` passes it now).
- **Live check** `[L]` (2026-11-23 23:47-23:53 EST, c5h-new-b, build 26577625, hidden with the focus guard, save copy
  `HI-4506-SAVED` (Tiny, a human game), this branch's server and proxy (`build_server.py`, pin 1b87dd185, release) and
  composite (runbook 1.5), the product layout by `lay.py` and `helper on`; evidence `shell/evidence/0720/live/`):
  - the boot line: `active (slots <slot> and <slot>; ...; at boot the EXE's hook 0x0C1BB00C not removed
    (UnhookWindowsHookEx error 1404: already gone); the game was not in front)`. The slots are the two import slots at
    the image base (their 26577625 RVAs stay local, D202); 0x0C1BB00C is the focus guard's fake handle
    (the guard refuses the EXE's startup installs in the background), so error 1404 is the expected answer here; with a
    real handle the removal is the unit test's case. Ghosting line: `window ghosting disabled for this process`.
  - WM_ACTIVATE posted twice on and off to the game window (`hangprobe.py activate`, no activation, no input): the EXE's
    own handler called SetWindowsHookExA / UnhookWindowsHookEx through the thunks: `asked for its hook 2 time(s) and
    released it 2 time(s) (request now off; helper hook not installed)`, 0 forwarded, 0 helper errors.
  - **Who installs**: the focus guard's log names the thread of each `SetWindowsHookExA(WH_KEYBOARD_LL)`: the EXE's two
    startup installs on its main thread (tid 49428, 23:48:13.989 and 23:48:14.196, before the proxy loaded), and after the
    boot only the proxy's helper thread (tid 28844, 23:51:17.282 and 23:51:26.812, the two posted activations). After the
    boot the EXE's main thread installed no low-level hook. (The guard refused the helper's installs too, as it refuses
    every low-level hook of a hidden game; "helper hook installed" in the log means a non-NULL handle came back.)
  - The load, the game and the stop ran as usual (in game after 146 s, `cli.py stop` exit 0, `lay.py restore ok`,
    `provision verify --quick` clean, no box). Not checked live: the real removal of a live EXE hook at boot and Alt+Enter
    in the game with the game in front (a hidden test game is never in front; that needs an unguarded game on a machine
    where input may be injected, or a human: the unit test covers both, `[C]` for the code paths only).

- **Live check on oldpc, input injected** `[L]` (MC #12421, MC-B's yes to #12427; 2026-11-24 00:20-00:57 EST; oldpc's tb1, EXE
  build 4390913 with this branch's server, proxy and composite laid by `tools/testbed/jobs/product_path.py`'s prep, save
  copy of TB-HUGE_0350; job `shell/evidence/0720/kbhook_oldpc.py`, key target `kbtarget.py` in its own process; the
  focus guard switched off on that instance after the launch (COMMON 24), so the EXE's installs are real; SendInput on
  oldpc only; `LowLevelHooksTimeout` not set in the session (the system default); evidence `shell/evidence/0720/oldpc/`):
  - **Real removal at boot**: run 1 `at boot the EXE's hook 0x0AF408A9 removed`, run 2 `0x229606FB removed` (a hook the
    EXE installed for real when a posted WM_ACTIVATE reached it with the guard off).
  - **Keys typed into another program while the game's main thread does not answer** (12 keys 60 ms apart into the
    target, each stall detected as 1 s without an answer to WM_NULL; the game still hung after the last key every
    time): runs 1 and 2, three stalls each, after the boot: every key received, in order, 0.3-1.0 ms after SendInput.
  - **Alt+Enter with the game in front** (run 2, in game, the game activated by a click): `Alt+Enter swallowed 1 (the
    game in front)`, the game's window rectangle and style unchanged 3 s later, the game answering (as stock: the EXE's
    own hook swallowed it too). With the key target in front: the target received WM_SYSKEYDOWN VK_RETURN (with the
    patch Alt+Enter is no longer swallowed in other programs).
  - The activation by a click failed while the front end was loading (runs 1-2: the click came during the start-up
    stall), so runs 1-2 had no leaked hook; run 3 leaks on purpose (a click that activated, then two posted
    WM_ACTIVATE(1) before the boot: three real installs, two handles overwritten). Run 3 (00:47-00:52): `at boot the
    EXE's hook 0x318A0657 removed; the game was in front`; three stalls, each begun with the game in front: every key
    in order, 0.4-1.3 ms.
  - **Stock arm, the same job** (`--arm stock`, the instance's own 32-bit DLL, 00:52-00:57; the same leak set-up):
    stalls begun with the game in front (0 and 2): the first key **304 ms** late, every following key **32 ms** late,
    all 12 received in order; stall 1, begun with the game not in front (the click failed): 0.3 ms. So in stock a
    stalled main thread delays every key typed into another program when the game was in front as the stall began
    (the hook installed by its activation; the deactivation's unhook waits for the stall to end): the owner's report,
    reproduced. 304 ms is the size of the default `LowLevelHooksTimeout` (not set on oldpc) `[I]`; why the following
    keys wait 32 ms rather than the full timeout is not known `[I]`.

    | Arm (oldpc, build 4390913, TB-HUGE_0350) | Stalls begun with the game in front | Key delay into another program |
    |---|---|---|
    | stock | 2 | first key 304 ms, then 32 ms per key |
    | patch, runs 1-3 | 3 (run 3), 0 (runs 1-2: the click failed) | 0.3-1.3 ms in all 9 stalls |

  - **Residual: hooks leaked before the proxy boots still swallow Alt+Enter in other programs**: run 3 and the stock arm
    (both with leaked hooks): the key target in front did NOT receive Alt+Enter; run 2 (no leak): it did. The leaked
    hooks did not delay keys in run 3's stalls nor in the stock arm's stall 1 (0.3-1.3 ms) `[L]`; why a leaked hook
    swallows but does not hold up keys on a stalled thread is not known `[I]`. On a player's machine the start-up leaks
    one hook when the window is activated as it is created (section 3), so after the patch Alt+Enter may still be
    swallowed in other programs while the game runs, as in stock; removing it would need the leaked handle, which no
    data of the EXE keeps (a follow-up only if wanted).

## 5. Part B: which waits hold the main thread (the owner's logs, 2026-11-23) `[L]`

Read 2026-11-23 23:0x EST from copies of the owner's own logs (read only; the composite's `shell_composite.*.log` per
process in `<My Games>\Logs`, the proxy's run folder), seven beta 3 sessions on build 26577625 between 18:23 and 22:57.
Per-second lines of the composite: `pacing 1s ... ft_max` (the longest time between two Presents of the EXE's main
thread in that second), `lockstep 1s ... wait_max_us` (D242's exact wait), `cut_tap: load a -> b` (load transitions).
Script: `python -I shell/evidence/0720/stalls.py <logs>`.

| Where in the session | Frames without a Present | Sessions |
|---|---|---|
| starting the game from the front end (before `load 1 -> 2`) | 9.3-25.0 s | 7 of 7 |
| the map load (`load 2 -> 3`) | 1.2-9 s, in 3-5 frames | 7 of 7 |
| **about 50-60 s after the map is in, once**: the InGame UI load (proxy run 26852: `ui bridge: gen 2 installed into the InGame context (... 181 contexts ...)` at the frame's end, 149.2 s after boot) | **20.5-26.5 s** in one frame | 5 times in 4 sessions (the other 3 left before it) |
| leaving, or loading another save (`load 3 -> 4`) | 2.9-37.7 s | 6 of 7 |
| in play, between those | at most 3.7 s (one session's turns: 1.0-3.7 s frames every few seconds) | |

- **D242's exact lockstep wait is not the stall**: its longest wait in all seven sessions was 1.17 s (one, at a load);
  every other second's wait_max stayed below 1 s.
- The owner's session the MC measured (pid 44488, 22:33:03-22:33:35 hung): the 25.0 s frame ending 22:33:20 (starting
  the game) and the 8.9 s and 6.1 s frames of the map load. The UI-load frame of that session is not in the log
  (the owner left at 22:37 from the load screen's state) `[I]`.
- **Proxied call time is a minority of the stall**: run 26852's whole turn 0 (the load and the UI load) spent 8.5 s in
  9.56 M proxied calls (proxy-calls-per-turn.tsv), its Lua calls 4.45 s in 3.55 M calls; the rest of the 20-26 s frames
  is the EXE's own work `[I]` (no per-frame split of the calls was logged).
- **Stock against the patch, the same save** `[L]` (2026-11-23 23:48-23:58 EST, c5h-new-b, build 26577625, hidden
  windowed against hidden windowed (D164), save copy `HI-4506-SAVED` (Tiny); patch arm as in section 4's live check;
  stock arm: the instance's own 32-bit DLL `ff0f6e86...` (the `-ia` build of the i0 pin, `+ia4968`, a labelled
  comparison arm, COMMON 18); one run each on the busy workstation: an indication, not a timing result). Measured from a
  second process (`shell/evidence/0720/hangprobe.py sample`: WM_NULL by SendMessageTimeoutW, 200 ms limit, every 250 ms,
  and IsHungAppWindow), from the launch to about 90 s in game:

  | Arm | Longest span without an answer | Spans >= 5 s | Unanswered samples | IsHungAppWindow spans |
  |---|---|---|---|---|
  | patch | 53.2 s (t = 61-114 s: mod activation, the proxy's boot and server start, the load) | 53.2, 17.0, 11.3, 8.9, 8.7, 7.3 s | 490 of 940 | 48.4, 12.1, 6.5, 4.1, 4.0, 2.6 s |
  | stock | 62.7 s (t = 56-118 s) | 62.7, 14.9, 10.6, 7.5 s | 448 of 911 | 50.6, 10.2, 5.7, 2.7, 2.4 s |

  Both arms: after the load screen's Continue nothing over 3.3 s until the stop. So on a Tiny save the main thread's
  start-up and load stalls of tens of seconds are **stock behaviour**, of the same size with the patch. The owner's
  20-26 s InGame UI-load frame (a 128 x 80 map, five times) did not show on Tiny; on the owner's map size: the next
  bullet (ticket 0722; this line said "open (a follow-up arm on a Huge save)" until 2026-11-24). Both arms' EXEs installed
  their two startup hooks on the main thread (the guard's log: tid 49428 patch, tid 57512 stock).
- **Huge, stock against the patch (ticket 0722)** `[L]` (2026-11-24 01:32-02:2x EST, c5h-new-b, build 26577625, hidden
  windowed against hidden windowed (D164), frame cap 30 fps (the harness default) in every arm; the patch = main 15bf0cb94's
  server and proxy (`build_server.py` release, pin 1b87dd185) and composite (runbook 1.5), laid by `lay.py` with the
  composite and `helper on`; stock = the instance's own 32-bit `-ia` DLL `ff0f6e86...`, a labelled comparison arm; the
  load screen closed through the Lua channel (`frontend.py dismiss`: the button's own handler, `Events.LoadScreenClose()`)
  the moment its button showed; `hangprobe.py` from the launch to 90 s after the dismiss; the busy workstation (5-18 heavy
  jobs registered): an indication, not a timing result (COMMON 23). Driver `shell/evidence/0722/live_0722.sh`, analysis
  `analyze_0722.py`, evidence `shell/evidence/0722/live/`):

  | Arm (order) | Game, patch options | Longest span (load) | The span after the map is in | Button seen (s from the probe) | Spans >= 1 s in the 90 s after the dismiss |
  |---|---|---|---|---|---|
  | patch-1 | save copy of `C5H-VP8P-HUGE_0350` (t350), the options file's defaults | 35.6 s | 22.0 s | 147.6 | none |
  | stock-2 | the same save | 33.8 s | 23.5 s | 146.3 | none |
  | stock-3 | the same save | 42.1 s | 24.0 s | 155.7 | none |
  | patch-4 | the same save, defaults | 34.3 s | 22.3 s | 150.8 | none |
  | patch-5 | the same save, **the owner's beta 3 options** | 38.6 s | none over 8.5 s | 136.2 | none |
  | patch-6 | the same save, the owner's options | 36.3 s | none over 9.0 s | 140.2 | none |
  | patch-7 | **a new Huge game** (Pangaea, 12 players), the owner's options | 64.4 s (map generation and load) | none over 4.1 s (the composite's frames) | 159.2 | none |
  | stock-8 | a new Huge game | 50.1 s (map generation and load) | none over 8.5 s (the InGame contexts load at its end) | 161.8 | none |

  - **On the Huge save the load stalls are the same in both arms**: the longest spans 33.8-42.1 s (stock) against
    34.3-35.6 s (patch), the span after the map is in 23.5-24.0 s against 22.0-22.3 s, the button 146-156 s against
    148-151 s after the probe's start. The span after the map is in is the EXE's farm-field layout (`load-sequence.md`
    1.1's "gap"): in the patch arms with the default options the proxy's `farm_guard: load 1 ... builder calls 1200
    (skipped 0)` closes it; in stock the game's `Lua.log` (stock-2, stock-3) shows the InGame contexts loading in 2.5 s
    just before it (t = 101.0-103.5 s, then nothing until 139.5 s). With the owner's options (`ServerFarmFields = 1`, the
    proxy's `builder calls 1200 (skipped 1200)`) the server lays the farms out and that span is gone: the button comes
    10-15 s earlier than in stock.
  - **No arm stalled after the dismiss**: in all eight arms nothing reached 1 s from the dismiss to 90 s later (the
    one 1.0-2.5 s span at the end of some arms is the stop). The patch arms' per-second counter snapshots
    (`snapper.py`; counters=1 is the proxy's default) show no in-game frame over 1.4 s.
  - **Where the owner's 20-26 s frame sits** `[L]` (the owner's own logs, read only, `owner_sessions.py`: the proxy's
    seconds since boot aligned on the composite's load line; `shell/evidence/0722/live/owner-sessions-frames.txt`): in
    five runs of 2026-11-23 it starts **4-50 s after the load ended** (the EXE's worker threads' first DLL call,
    `channel aux`, or the DLL's first fetch of the city plots; in the test runs both come within 1 s of the button
    showing) and contains the ui bridge's
    `gen 2 installed into the InGame context (... 180 or 181 contexts; the drain was silent) from a Lua call on thread
    <main>`; at the map-in `gen 1` saw 9 contexts. The bridge reinstalls when its per-frame drain (`LocalMachineAppUpdate`)
    has not run for a while and a Lua call into the DLL comes on the main thread: so InGame Lua ran on the main thread
    inside the frame, after the load screen's button showed `[I]` (whether the 180 contexts loaded in that frame or before
    it, the line does not say). 3 of the 5 were new games (Crescent, Pangaea), 2 loads of the owner's saves; one session
    (42968) had it twice, once per load. Three runs had no frame of 15 s or more after that marker although the game went
    on: 40696 (a load, 66 s more), 44488 (a new game, at least 143 s more: its farm-guard lines; it is the session the MC
    measured, section 1) and 2956 (2026-11-22, an older build, a new game, at least 213 s more). So the frame is
    frequent, not certain. (44488's farm-guard loads after the marker mean improvements changed, so the game ran
    unpaused; that does not fit the `[I]` above that its owner left from the load screen's state. Not settled.)
  - **What the test runs do not have** `[I]`: in every test arm the InGame contexts load during the load (stock-2's
    `Lua.log`), and the bridge never reinstalls (one `gen 1` install, no `gen 2`, in all five patch runs). The owner's
    runs differ in being visible, in front, uncapped, and closed by a real click on the button (the dismiss here calls the
    button's handler through the Lua channel). What the main thread runs in the owner's frame is not known; a hidden
    test game is never activated (the focus guard, section 3), so a cause tied to the window being shown or activated
    cannot show here.
- **What follows for the fix** `[I]`: the long stalls are inside the EXE's own start-up, load and UI-load code on its main
  thread, not in a wait of the patch (D242's wait stayed under 1.2 s). A message pump there would be a code write into
  the EXE (D228: not in the base product). With section 4 the stalls no longer reach the keyboard of other programs (no
  low-level hook on the main thread after the boot) and no longer move the focus (no ghost window); they still make the
  game's own window unresponsive during loads, as in stock. On Huge (ticket 0722, above) the load's stalls are stock's
  too, and with the owner's options the patch removes the largest one after the map is in (the farm-field layout,
  `ServerFarmFields = 1`). The owner's after-the-button frame did not occur in any hidden test arm, stock or patch; what
  would settle it: one visible, unguarded Huge new game per arm closed by a real click on the button (oldpc, input
  injected, as 0720's `kbhook_oldpc.py`), or the owner's `Lua.log` of a session with the frame (its stamps show which
  contexts or scripts ran in it).

## 6. A second cause of a dead keyboard: the installer tests take the foreground `[L]` (2026-11-24)

The owner, 2026-11-24 21:4x [reworded for privacy]: the keyboard was unusable. No game process ran, no visible window was hung and no
modifier key was held (GetAsyncKeyState of Shift, Ctrl, Alt, Win, Caps). The foreground window alternated every few seconds between the
owner's program and a window titled `Setup` owned by `p64test.tmp`, a new pid each time. These are the Inno Setup second
stage of the silent test installs that `tools/installer/tests` runs (`p64test.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART ...`),
here started by beta 4's take 0725 through `tools/suites/run_suites.py --tier intermediate` (suite `installer`, about
480 s). Keys typed while `Setup` is in front go to it. `/VERYSILENT` hides the wizard but not this foreground change.
Not a hook: the low-level hook cause of sections 1-5 needs a game process. Fix: ticket 0728 (section 6.1). The diagnosis script (hung
windows, held modifier keys, foreground window and its process, time since the last input) took under a second; run the
same checks first the next time the owner's keyboard dies.
This run: the suite ran 21:43:30 to about 21:49 (98 passed in 324 s); from then on the foreground stayed with the owner's program
(checked 22:02 every 2 s). A watcher that matches processes by the text `tools/installer/tests` in their command line also matches
itself; match `p64test` by image name only.

### 6.1 Which call takes the foreground (ticket 0728, 2026-11-25)

- **Source** `[C]` (Inno Setup 6.2.2, the pinned ISCC's version, upstream `jrsoftware/issrc` tag `is-6_2_2`, read
  2026-11-25): the second stage's main program (`Projects/Setup.dpr`) sets `Application.Title := 'Setup'` (not
  localized, hence the title) and then calls `ShowWindow(Application.Handle, SW_SHOW)` ("force it visible like
  Delphi 2") BEFORE `InitializeSetup`. Only `InitializeSetup` parses `/VERYSILENT`, and its `SetupInstallMode`
  (`Projects/Main.pas`) then sets `Application.ShowMainForm := False` and calls `SetTaskbarButtonVisibility(False)`
  (adds `WS_EX_TOOLWINDOW` to the application window and re-applies its frame). `SW_SHOW` activates the window. So
  `/VERYSILENT` cannot stop the first activation: it is read after it. The first stage (`Projects/SetupLdr.dpr`,
  `ExecAndWait`) starts the `.tmp` with an all-zero `STARTUPINFO` (no desktop named, so the `.tmp` inherits the first
  stage's desktop; no show flag passed on).
- **Live** `[L]` (oldpc, logged-on session, testbed job `20261125-002441-a242`, 2026-11-25 00:25-00:28 EST, the
  driver `p64test.exe` compiled on the workstation from `tools/installer/tests/p64test.iss` at branch commit
  5d1cbb35a, SHA-256 7dc582cf65b5..., `/ACTION=find` on an empty folder, 5 runs; a WinEvent hook on
  EVENT_SYSTEM_FOREGROUND plus a GetForegroundWindow poll; script `tools/installer/tests/evidence/0728/fg_probe.py`):
  each silent run took the foreground TWICE, both times the window of class `TApplication`, title `Setup`, image
  `p64test.tmp` (style 0x94CA0000, so visible):
  1. about 1.75 s after the start, with extended style 0x100 (no `WS_EX_TOOLWINDOW`): the `ShowWindow(SW_SHOW)` of
     `Setup.dpr`, before `SetupInstallMode` ran. In front 85-90 ms.
  2. 2 ms after it lost the foreground, with extended style 0x180 (`WS_EX_TOOLWINDOW` set): the re-show of the window
     by `SetTaskbarButtonVisibility(False)` `[I]` (the only code on the `/VERYSILENT` path that sets that style; its
     re-show call is inferred from the style change, not read). In front 63-73 ms.
  That is about 160 ms per test install in which typed keys go to `Setup`; the include tests run the driver many
  times (every fault point is one run), so on the workstation the foreground moved every few seconds (section 6).
  oldpc had no input lock (nobody typed there); on the workstation it took the foreground while the owner typed too
  (section 6), so the foreground lock does not stop it.
- **Fix** (ticket 0728): `tools/installer/tests/hidden_run.py` starts the driver on a hidden desktop of its own
  (`CreateDesktopW` through `tools/harness/tests/desktops.py`'s `HiddenDesktop`; `lpDesktop` in `STARTUPINFO`,
  `SW_HIDE`, `CREATE_NO_WINDOW`), suspended inside a job object with KILL_ON_JOB_CLOSE, then resumed. The `.tmp` and
  the stand-in helper it runs inherit that desktop, and a window on a desktop that is not the input desktop can never be
  the foreground window. A timeout ends the whole tree (the old `subprocess.run(timeout=180)` ended the first stage only
  and left the `.tmp`). `test_install_removal.Fixture.run` (used by `test_vpfix.py` too) was the only installer start
  on the interactive desktop; `test_vpfix_build.py` already used the harness's hidden desktop (`test_installer.Proc`).
  `test_no_foreground.py` fails when any function of `tools/installer/tests` builds a `/VERYSILENT` command line and
  starts it by `subprocess`/`os`, or when `Fixture.run` stops using `hidden_run.run` (negative control 2026-11-25: with
  the old `subprocess.run` line back, 3 of its 10 tests fail).
- **Fix verified** `[L]` (the same job): 5 runs through `hidden_run.run`: 0 foreground changes. Then
  `test_install_removal.py` (49 tests, OK, 104 s) and `test_vpfix.py` (20 tests, OK, 47 s) in full with
  `P64TEST_PREBUILT` (ISCC and clang are not on the test machines): 0 foreground changes in the hook's log over the 151 s.
  The poll asked for 10 ms and got 15.6 ms on average (Python's `time.sleep(0.01)` at the default timer resolution);
  the hook logs every change regardless. Not run on the workstation (the owner was playing). `test_vpfix_build.py` and
  `test_options_schema.py` (need ISCC/clang) were not in the job; by reading, they start no installer on the
  interactive desktop.
