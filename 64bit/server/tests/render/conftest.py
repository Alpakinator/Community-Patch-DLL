"""Tests of the renderer inside server.exe (issue 3301). Rule 14: every process starts on a hidden desktop the session
creates (shell/tests/shellrun.launch, tools/harness/tests/desktops.py; both imported, not modified). A run with skipped
tests counts as failed (COMMON 9).

Build first (gated): python -I server/tests/render/build_render_tests.py --out DIR [--asan]; then
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    set C5_RENDER_TEST_BUILD=DIR/bin      (DIR/bin-asan for the ASan variant)
    python -m pytest -q server/tests/render
Needs C5R_EXE (CivilizationV_DX11.exe, read-only; default the setting render_exe of tools/release/settings.py, as
render/build.py).

The ASan variant (issue 0149; both traps cost 9055 a run): no manual environment is needed.
- The runtime: `build_render_tests.py --asan` copies clang_rt.asan_dynamic-x86_64.dll (from cl.exe's folder,
  VC/Tools/MSVC/<ver>/bin/HostX64/x64) beside the binaries. Without it, or that folder on PATH, every test that starts
  render_host_test.exe fails with exit 3221225781 (0xC0000135, STATUS_DLL_NOT_FOUND); the session stops at once instead.
- The reports: C5_ASAN_LOG_DIR names the folder (default: <build>/../asan-logs; the session header prints it without
  -q; a test fails only on reports that appeared while it ran), and
  this conftest writes ASAN_OPTIONS' log_path itself, QUOTED: ASan splits its options at ':', so an unquoted
  `log_path=D:/...` ends at the drive colon ("AddressSanitizer: ERROR: expected '=' in ASAN_OPTIONS") and every process
  fails at start. Any log_path already in ASAN_OPTIONS is replaced; the other options are kept. A new report fails the
  test that was running (no_asan_reports).
"""
import importlib.util
import os
import re
import shutil
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "shell" / "tests"))

import shellrun  # noqa: E402  (shell/tests/shellrun.py, imported only)
import desktops  # noqa: E402  (tools/harness/tests/desktops.py, put on sys.path by shellrun)


ASAN_RUNTIME = "clang_rt.asan_dynamic-x86_64.dll"
DEFAULT_BUILD = "D:/c5-64bit-work/3300/w1/rt/bin"
# a log_path option in ASAN_OPTIONS: quoted ("..." or '...'), an unquoted Windows path (its drive colon), or up to ':'
_LOG_PATH = re.compile(r"""log_path=(?:"[^"]*"|'[^']*'|[A-Za-z]:[^:]*|[^:]*):?""")


def asan_options(log_dir, current=""):
    """ASAN_OPTIONS with log_path set to <log_dir>/asan, quoted for the drive colon; current's other options are kept."""
    rest = _LOG_PATH.sub("", current or "").strip(":")
    opt = 'log_path="%s"' % (Path(log_dir) / "asan").as_posix()
    return opt + (":" + rest if rest else "")


def is_asan_build(b):
    return b.name.endswith("-asan") or (b / ASAN_RUNTIME).exists()


def pytest_configure(config):
    """Issue 0149: the ASan variant's environment (module docstring), set before any process starts."""
    b = Path(os.environ.get("C5_RENDER_TEST_BUILD", DEFAULT_BUILD))
    if not is_asan_build(b):
        return
    if not (b / ASAN_RUNTIME).exists() and not shutil.which(ASAN_RUNTIME):
        raise pytest.UsageError("%s is neither beside the binaries in %s nor on PATH: every exe-starting test would fail "
                                "0xC0000135; rebuild with build_render_tests.py --asan (it copies the runtime)" % (ASAN_RUNTIME, b))
    d = os.environ.get("C5_ASAN_LOG_DIR") or str(b.parent / "asan-logs")   # beside the build (D:, COMMON 15)
    Path(d).mkdir(parents=True, exist_ok=True)
    os.environ["C5_ASAN_LOG_DIR"] = d
    os.environ["ASAN_OPTIONS"] = asan_options(d, os.environ.get("ASAN_OPTIONS", ""))
    config._c5_asan_line = "ASan build %s: ASAN_OPTIONS=%s" % (b, os.environ["ASAN_OPTIONS"])


def pytest_report_header(config):
    line = getattr(config, "_c5_asan_line", None)
    return [line] if line else []


def pytest_sessionfinish(session, exitstatus):
    tr = session.config.pluginmanager.get_plugin("terminalreporter")
    skipped = len(tr.stats.get("skipped", [])) if tr else 0
    if skipped and session.exitstatus == 0:
        if tr:
            tr.write_line("%d test(s) skipped: the run counts as failed (COMMON 9)" % skipped, red=True)
        session.exitstatus = 1


@pytest.fixture(autouse=True)
def no_asan_reports():
    """With the ASan build, C5_ASAN_LOG_DIR names the folder of ASAN_OPTIONS log_path: a new report fails the test."""
    d = os.environ.get("C5_ASAN_LOG_DIR")
    before = set(Path(d).glob("asan*")) if d else set()
    yield
    if d:
        new = sorted(set(Path(d).glob("asan*")) - before)
        assert not new, "ASan report(s): " + " || ".join("%s: %s" % (p.name, p.read_text(errors="replace")[:3000]) for p in new)


@pytest.fixture(scope="session")
def hidden_desktop():
    d = desktops.HiddenDesktop(prefix="C5RENDER")
    yield d.name
    d.close()


@pytest.fixture(scope="session")
def built():
    b = Path(os.environ.get("C5_RENDER_TEST_BUILD", DEFAULT_BUILD))
    missing = [n for n in ("render_host_test.exe", "fake_consumer.exe") if not (b / n).exists()]
    if missing:
        pytest.fail("not built (%s missing in %s): run server/tests/render/build_render_tests.py" % (", ".join(missing), b))
    return b


def release_setting(env, key):
    """Ticket 4716 (D174), as 4713's release_setting in shell/tests/test_lockstep.py: the environment variable `env` if
    it is set, else the path setting `key` of tools/release/settings.py (its own environment variable, else
    deps/local-settings.ini), as render/build.py reads it; with neither, a path that does not exist, so the test that
    needs it fails naming `env` (was a literal path in a user folder, the value the setting gives on the workstation)."""
    if env in os.environ:
        return Path(os.environ[env])
    spec = importlib.util.spec_from_file_location("c5_release_settings", REPO / "tools" / "release" / "settings.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    v = mod.get(key)
    return Path(v) if v is not None else Path("<%s not set: setting %s of tools/release/settings.py>" % (env, key))


@pytest.fixture(scope="session")
def exe():
    p = release_setting("C5R_EXE", "render_exe")
    if not p.exists():
        pytest.fail("C5R_EXE %s missing (CivilizationV_DX11.exe, read-only)" % p)
    import gameinstall   # ticket 0589 (D200): only build 4390913, never Firaxis' new build by accident
    why = gameinstall.exe_refusal(p)
    if why:
        pytest.fail("C5R_EXE: " + why)
    return p
