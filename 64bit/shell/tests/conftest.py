"""Shared by the shell stand-in tests (issues 3001, 3006). Rule 14: nothing starts on the owner's interactive desktop.

- Every process is started by shellrun.launch() on a desktop the test session created (`hidden_desktop` fixture,
  tools/harness/tests/desktops.py HiddenDesktop); launch() refuses the interactive desktop through the harness's
  own check (desktops.check_desktop) unless C5_HARNESS_INTERACTIVE_DESKTOP carries the MC's permission.
- As in tools/harness/tests/conftest.py, every subprocess.run made during a test goes through
  desktops.check_command first. tools/harness/ is not modified; its modules are imported.
Run: `python -m pytest shell/tests` from the repository root (Python 3.11, without -I), after `cmd /c shell\\build.cmd`.
"""
import ctypes
import os
import subprocess
import sys
from ctypes import wintypes
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import shellrun  # noqa: E402
import desktops  # noqa: E402  (put on sys.path by shellrun)


@pytest.fixture(autouse=True)
def refuse_interactive_desktop(monkeypatch):
    monkeypatch.setitem(desktops.REAL, "run", subprocess.run)

    def checked_run(cmd, *a, **kw):
        desktops.check_command(cmd)
        return desktops.REAL["run"](cmd, *a, **kw)

    checked_run.c5_desktop_checked = True
    monkeypatch.setattr(subprocess, "run", checked_run)
    yield


def pytest_sessionfinish(session, exitstatus):
    """COMMON rule 9 (auditor cycle 2): a run with skipped tests counts as failed. A skip (missing EXE on disk, missing
    ground truth) turns the exit status into 1 unless C5_SHELL_ALLOW_SKIP=1 is set for a run that knowingly lacks them."""
    tr = session.config.pluginmanager.get_plugin("terminalreporter")
    skipped = len(tr.stats.get("skipped", [])) if tr else 0
    if skipped and os.environ.get("C5_SHELL_ALLOW_SKIP") != "1" and session.exitstatus == 0:
        if tr:
            tr.write_line("%d test(s) skipped: the run counts as failed (rule 9; C5_SHELL_ALLOW_SKIP=1 to accept)" % skipped,
                          red=True)
        session.exitstatus = 1


@pytest.fixture(autouse=True)
def no_asan_reports():
    """Issue 3013: with the ASan build (`shell\\build.cmd asan`, C5_SHELL_BUILD pointing at it) the run sets
    ASAN_OPTIONS=log_path=<dir>/asan and C5_ASAN_LOG_DIR=<dir>; every child process inherits it, and an ASan report
    (written as <dir>/asan.<pid>) fails the test that was running. test_host.exe is a GUI program without stderr, so the
    log file is the only place a report can go."""
    d = os.environ.get("C5_ASAN_LOG_DIR")
    before = set(Path(d).glob("asan*")) if d else set()
    yield
    if d:
        new = sorted(set(Path(d).glob("asan*")) - before)
        assert not new, "ASan report(s): " + " || ".join(
            "%s: %s" % (p.name, p.read_text(errors="replace")[:3000]) for p in new)


@pytest.fixture(scope="session")
def hidden_desktop():
    d = desktops.HiddenDesktop(prefix="C5SHELL")
    yield d.name
    d.close()


_u32 = ctypes.WinDLL("user32", use_last_error=True)
_WNDENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
_u32.EnumWindows.argtypes = [_WNDENUM, wintypes.LPARAM]
_u32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
_u32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
WM_CLOSE = 0x0010


def _close_windows_of(pid):
    """WM_CLOSE to every top-level window of `pid` on the calling thread's desktop; the number posted."""
    posted = []

    def cb(hwnd, _):
        owner = wintypes.DWORD()
        _u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _u32.PostMessageW(hwnd, WM_CLOSE, 0, 0):
            posted.append(hwnd)
        return True

    _u32.EnumWindows(_WNDENUM(cb), 0)
    return len(posted)


@pytest.fixture
def host_closer(hidden_desktop):
    """Ticket 0494: closer(pid) posts WM_CLOSE to the test host's window on the session's hidden desktop (test_host's
    Esc path: it ends normally, exit 0) and returns the number of windows posted to. Tests use it to end the host at a
    time of their own: test_host counts --seconds from its own start, and the composite's hook commit inside its first
    frames took up to 10.7 s under load (2026-11-17)."""
    return lambda pid: desktops.on_desktop(hidden_desktop, _close_windows_of, pid, timeout=10)


@pytest.fixture(scope="session")
def built():
    b = shellrun.bin_dir()
    missing = [n for n in ("shell_server.exe", "shell_composite.dll", "test_host.exe") if not (b / n).exists()]
    if missing:
        pytest.fail("not built (%s missing in %s): run `cmd /c shell\\build.cmd` from the repository root" % (
            ", ".join(missing), b))
    return b


# Issue 3312 (2026-11-13): the shipped shell_composite.ini says handover=ring lockstep=1 depth_write=1 (the product path,
# server.exe with render_handover=ring). The tests of this fixture run shell_server.exe's version-3 hand-over (keyed mutex,
# no publish block, no depth block unless --depth 1), which the old defaults described: they set those keys themselves.
V3_KEYS = dict(handover="v3", lockstep="0", depth_write="0")
# Issue 3313 (2026-11-13): the shipped ini says tap=1 too (server.exe's terrain needs the height field). The fixtures start
# from tap=0, the old default; a test of the tap sets tap=1 itself (test_tap.py's TapRun, test_lockstep, test_join_ring).
BASE_KEYS = dict(V3_KEYS, tap="0")


@pytest.fixture
def run(tmp_path, hidden_desktop, built, request):
    r = shellrun.Run(tmp_path / "run", hidden_desktop, request.node.name[:24].replace("[", "_").replace("]", ""))
    r.set_ini(**BASE_KEYS)
    yield r
    r.stop()
