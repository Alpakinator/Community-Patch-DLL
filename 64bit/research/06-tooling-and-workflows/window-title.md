# The game window's title: the `[VP64] ` prefix (ticket 0680)

Written 2026-11-22 by ticket 0680 (account B, branch `task-0680-window-title`; builds, links and live smoke added
2026-11-24 by its finishing pass, branch `task-0680-finish`, section 4), from the owner's request of the same
day: while the patch runs, the game window's title reads `[VP64] Sid Meier's Civilization V (DX11)`, configurable in
the options file. Code: `proxy/src/proxy_pack.{h,cpp}` (the title rule and the keeper), `proxy/src/proxy_rt.cpp` (start
and stop), `options/` (the option, `research/07-architecture-decisions/options-file.md` section 9). Player text:
`docs/OPTIONS.md` `[Shell] WindowTitlePrefix`.

## 1. What the patch does

- **Option**: `[Shell] WindowTitlePrefix`, text, default `"[VP64] "`; `""` = the EXE's title unchanged (no keeper).
- **When**: after a good `mode=on` boot (the EXE's first `DllGetGameContext` at mod activation, about 50-110 s after
  the launch: `run-and-drive-the-game.md`, `human-input-testing.md` 4.6). Never with the patch off (`mode=off`,
  `failed=1`, the pack fallback, an unsupported EXE build): there the title is the EXE's own, and the fallback's notice
  (` - VP64 64-bit patch OFF: ...`, `release-driver.md` W3) stays as it was. A notice after a prefixed title (a 64-bit
  boot earlier in the same process, then a fallback at a later load of the DLL) first takes the prefix off
  (`ShowNotice(text, stripPrefix)`; BootOff reads the prefix in the same options read as `GuardsWhenOff`).
- **How** (no code write to the EXE, D228): the window's own `WM_SETTEXT`, as the off notice. A keeper thread of the
  proxy (`ProxyPack::StartTitleKeeper`, every 1000 ms) finds the game window, reads its title with
  `InternalGetWindowText` (no message to the window's thread), and when the title does not start with the prefix sets
  `prefix + title` with `SendMessageTimeoutW(WM_SETTEXT, SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 250 ms)`. The window's
  thread makes the change at its next message check; the keeper never blocks the game and activates nothing.
  `user32` is loaded by name (the proxy's import list is checked by `build_proxy.py`).
- **The title rule** (`PrefixedTitle`): prefix + current, except when the prefix is empty or the current title already
  starts with the prefix: no double prefix, also after the EXE sets its title back (the keeper puts it on again, once).
  A title of 1023 characters or more is left alone.
- **Which window** (`FindGameWindow`): this process's top-level window of class `WinClass_FXS` (the EXE's main window
  class, `headless-and-parallel-testing.md` section on `FindWindowA("WinClass_FXS", ...)`) without an owner, the largest
  if there are several, visible or not. Never a dialog box: `tools/harness/dialogwatch.py` classifies boxes by their
  exact titles ("Assertion Failed", "Error", "Render Error", "Game Crash", the C runtime's title), so a prefixed box
  title would break the fatal-box rules of D155. The off notice still takes the largest visible window of any class
  (unchanged).
- **Unload**: `DetachOnUnload` stops the keeper first (`StopTitleKeeper`: waits up to 1.5 s for a tick in progress,
  then ends the thread, as the shell's watcher threads; logs nothing under the loader lock), then logs
  `detach: title: keeper stopped, prefix "[VP64] ": N ticks (M without a game window), K title changes (the first and
  K-1 re-applications), F failed sends`. The title stays as it is. Each change and the first three failed sends are
  logged in `vpproxy.log` as `title: ...` lines from the keeper's thread.

Why a thread and not the proxy's Update hook: `ICvGame1::Update` runs on the EXE's game-core thread, not the window's
thread (`research/03-dll-engine-boundary/interfaces-and-traffic.md` section 1), so a `WM_SETTEXT` from there is a
cross-thread send anyway, and a send that waits for a window thread that waits for the core thread would stall a
frame. From the keeper's own thread a slow window costs only the keeper (250 ms, then the next tick). The UI thread
has no per-frame proxy hook of its own [C, proxy_update.cpp; proxy_rt.cpp DllGetGameContext].

A known limit [I]: a send that timed out stays queued for the window's thread and is handled later; if the EXE set a
new title in between, that late message would put `prefix + the old title` back. The EXE is not known to change its
title at all (2.2), so this was not handled.

## 2. Findings

### 2.1 No tool finds the game window by its title [C: git grep, 2026-11-22 20:2x EST, main 759c06bd1]

Searched: `git grep -I "(DX11)\|FindWindow\|WinClass_FXS\|GetWindowText"`, `git grep -I -i "Civilization V\|Sid Meier"`
over `*.py *.cpp *.h *.ps1 *.json *.ini *.lua` outside research and evidence, title comparisons in non-test Python
(`title ==`, `startswith`, `in title`), the user's skills (`civ5-game-ui`, `vp-dll-dev`). Result:

| Tool | How it finds or reads the game window | With the prefix |
|---|---|---|
| `tools/harness/winapi.py` `main_window`, `main_hwnd`, `wait_window` (every harness launch, shot, click, watch) | the process's largest visible top-level window, or the focus guard's recorded `main_hwnd` (by pid) | unchanged |
| `tools/harness/native/src/focus_guard.cpp` (the no-focus guard, `civ_bg.py`'s library form) | class `WinClass_FXS` for the `FindWindow*` isolation; titles only logged | unchanged |
| `server/src/srv_guard.cpp` (the server's child guard) | titles only logged (`InternalGetWindowText`) | unchanged |
| `shell/composite/shell_composite.cpp` | the swap chain's `OutputWindow`; title only logged | unchanged |
| `tools/harness/crash.py` (crash windows), the skill's `vp_common.crash_window_title` | `"crash" in title.lower()` over the process's windows | unchanged (the prefix has no "crash") |
| `tools/harness/dialogwatch.py`, `crash.py` box kinds | exact titles of boxes (class `#32770`), never of the game window | unchanged: the keeper never touches another class |
| `tools/testbed/jobs/release_fallback.py` | `"VP64 64-bit patch OFF" in title` (the off notice) | unchanged: no prefix with the patch off |
| `tools/testbed/jobs/steam_launch.py` `game_window` | by pid | unchanged |
| tests: `tools/harness/tests/test_ops_crash.py` (`GAME_WINDOW` fixture), `test_dialogwatch*.py` (WER box title) | data, not a lookup | unchanged |
| `research/sources/shell-gates/tools/civ_bg.py` | prints the title | unchanged |

So no tool needed a change. The focus guard keeps working: it never reads the game window's title.

### 2.2 Does the EXE set its title again? Not during a save load [L 2026-11-24, build 26577625]

The EXE sets `"Sid Meier's Civilization V (DX11)"` (string at 0xA0E8F4 on 4390913, `headless-and-parallel-testing.md`)
and the title was the same on every screen, in game too (`human-input-testing.md` 4.6, [L] 2026-11-16). On build
26577625 the live smoke of 2026-11-24 (section 4) shows that the EXE does **not** set its title again during a save
load started from the front end (the mod activation, the load screen, its dismissal and 2 min in game): the keeper set
the prefix once, the title kept one prefix to the end, and `vpproxy.log` has no `set it again` line. Still not observed:
a return to the main menu, a second load from inside a game, a change of the window mode. The keeper's log answers
those too: every `title: the title had lost its prefix: set it again ...` line is one reset, with the seconds since
the keeper's start.

### 2.3 The first sends fail while the EXE loads the mods [L 2026-11-24, build 26577625]

The keeper starts 1.0 s after the proxy's boot (the `boot: title: keeper thread ...` line), while the EXE's window
thread is still busy with the mod activation and the load. The first three `WM_SETTEXT` sends then fail at once with
`error 0`, not after the 250 ms timeout with error 1460. This matches `SMTO_ABORTIFHUNG`: Windows treats a thread
that has not read its messages for about 5 s as hung, and the call returns at once without setting an error [I: the
Win32 documentation of `SendMessageTimeout`; the timing of the lines]. The keeper tries again each second, and the
prefix came 35.1 s after the keeper's start, while the load screen was still up. That is about 15 s before the
dismissal and 20 s before the game. The failures after the third are only counted. Their number is in the `detach:`
line, which a harness stop never writes (next paragraph). This costs nothing: the keeper's thread waits, the game
never does. The title has no prefix for the first half-minute of a load.

The `detach: title: keeper stopped ...` line is written only when the DLL is unloaded. `cli.py stop` ends the EXE
with TerminateProcess, so no DLL is unloaded and no `detach:` line is written. It shows only after a normal exit of
the game, through its menu [L 2026-11-24: no `detach:` line in the run's `vpproxy.log` after `cli.py stop`].

## 3. Tests [L 2026-11-22 20:2x EST, branch `task-0680-window-title`]

- `proxy/tools/test_proxy_pack.py --only unit --asan` (the proxy's toolchain, clang-cl -m32 with the VC9 headers, on a
  hidden desktop): `pack_unit` 93/93, and 93/93 under MSVC ASan with no report. New: the title rule (prefix once, empty
  prefix, already prefixed, prefix without its space, another prefix, non-ASCII, unprefix), `Widen`, `FindGameWindow`
  (none; the largest unowned `WinClass_FXS` window, not a smaller one, not an owned one, not a larger window of another
  class), the keeper (empty and invalid prefix start nothing, a second start refused, the prefix set within one tick,
  once after more ticks, the other windows unchanged, no activation, one log line; an EXE-style reset gets it again,
  once; a window thread that does not pump for 700 ms makes the sends time out (error 1460, counted and logged) and
  the keeper goes on; after `StopTitleKeeper` (twice) a reset title stays as set), and the off notice on a prefixed
  title (the prefix comes off, the suffix goes on).
- Options: see `options-file.md` section 9 (879/879 in both trees and under ASan on the doc copy).
- `tools/harness/tests/test_safety_static.py`: 55 passed (46.8 s).

## 4. The finishing pass: builds, links and the live smoke (2026-11-24)

On 2026-11-22 the build was refused by the implementer session's permission system, and the build, the links and the
live smoke were left undone (thread entries 10609, 10610). D238 now allows account B the server and proxy builds. The
finishing pass ran on branch `task-0680-finish`: the 0680 head d1e5e19bf with main 01068b8a0 merged in (MC #12841,
MC-B's grant #12862). The merge had two conflicts, both resolved keeping both sides: in `proxy_rt.cpp`, ticket 0721's
`LOUD` line for a NULL context from the 32-bit DLL comes after the one options read that gives `GuardsWhenOff` and the
prefix; in `options-file.md`, 0718's section 8 stays and the text options become section 9. Scripts:
`server/evidence/0680/allb_0680.sh 1|2|3` (the builders), `live_0680.sh prep|run|end <inst>` (the smoke) and
`title_now.py <inst> [--expect-one]` (the title from `cli.py status --json`).

**Builders** [L 2026-11-24 13:16-14:02 EST, every step through `quietlock.py run`, owner `impl-0680f`]: all 18 builders
of runbook 2.10's `options/src/vp64_options.cpp` row exit 0:
- `build_server.py` release on the 0.1b pins, with the proxy (`build_proxy.py`): 361 s, the proxy 109 s. So
  `proxy_rt.cpp` and `proxy_options.cpp` now compile.
- `build_render_tests.py` (which builds c5render).
- With that c5render: units, units extras, unitfeed, farmfields, forest, fow, overlay scene, scenebundle, scene shadow
  and `build_sceneshot.py --smoke`.
- `run_extras_test.py` (0 parts failed), `run_hidden_embark_test.py` (PASS) and `shell/build.cmd` (BUILD OK).

The proxy's tests:
- `test_proxy_rt.py --build`: 235/235.
- `test_proxy_pack.py --build --asan`, all stages: 79/79 checks; `pack_unit` 93/93, and 93/93 under ASan with no
  report.
- `test_options_boot.py`: 13/13.

Options:
- `server/evidence/0680/options_tests.py` (after `ipc/build.py --arch both --target test_options`): PASS. The x86 and
  x64 trees each give 943 passed, 0 failed on the doc copy with the entry. On main's `docs/OPTIONS.md` they give 942
  passed, 1 failed: the WindowTitlePrefix docs check, expected until the MC writes the entry. The count rose from 879
  because main's options tests grew.
- The options ASan run (`options/test_asan.py`, `asan_doc_copy.py`) was not repeated after the merge.

`test_safety_static.py`: 55 passed. No step logged 0xC0000142.

**Live smoke** [L 2026-11-24 14:04-14:10 EST, c5h-new-b, EXE build 26577625, mods548, hidden (cloaked), focus guard and
D155 watcher on, 30 fps cap, the harness defaults]:
- Setup: the product layout of this build was laid by `server/evidence/0600/lay.py prep` (with the composite) and
  `helper on`. The save was a copy of `HI-4506-SAVED`, put in as `C5H-0680-TITLE` by hash.
- Before the mod activation the title was `Sid Meier's Civilization V (DX11)`. From 14:06:38, during the load screen,
  it was `[VP64] Sid Meier's Civilization V (DX11)`. It kept one prefix in every sample (every 5-12 s): through the
  dismissal (14:06:51), in game (14:06:59) and to the stop (14:09:4x). `title_now.py --expect-one` after the load
  exited 0.
- `vpproxy.log`: the keeper's start line 1.0 s after the boot, three failed sends (2.3), and one `title: set the
  prefix` line 35.1 s after the keeper's start. There was no `set it again` line and no `LOUD` line about the title.
- The harness kept working: `frontend.py ingame` and `dismiss`, and Lua (turn 9, autoplay 0) answered. The focus guard
  kept working: enabled, the window cloaked, never in the foreground, `seen_by_art` false, `cli.py watch 30` exit 0,
  `startup_watch` ok.
- `cli.py stop` exit 0, after the newest save had settled.
- Restore: `lay.py restore` ok, the front end and the saves restored by hash (the copy moved out), `provision.py verify
  --full` exit 0, `leftovers` 0, no box. The instance was released at 14:1x.
- No screenshot was taken; the title was read from the window.

Still open: the title after a return to the main menu, after a second load from inside a game and after a window-mode
change (2.2), and the `detach:` line after a normal exit through the menu (2.3).
