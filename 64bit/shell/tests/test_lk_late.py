"""Issue 0150 (9055 V4): the lockstep wait's late starts and overruns are counted apart (shell/composite/lk_late.h).

Runs lk_late_test.exe (shell/tests/lk_late_test.cpp, built by shell\\build.cmd): a late start without an overrun, a real
overrun, both at once, waits within the 0.5-ms contract, the boundaries, no deadline, the running maximum, and 3311's
shape in miniature (the old counter's 10 "late" are 9 late starts and 1 overrun). The live case is
test_lockstep.py::test_late_insertion_counts_late_starts_not_overruns. Skipped counts as failed (conftest.py).
"""
import re
import subprocess

import pytest

import shellrun


def test_lk_late_unit(built):
    exe = built / "lk_late_test.exe"
    if not exe.exists():
        pytest.fail("lk_late_test.exe missing in %s: rebuild with shell\\build.cmd" % built)
    p = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    assert p.returncode == 0, p.stdout + p.stderr
    m = re.search(r"lk_late_test: OK (\d+) checks", p.stdout)
    assert m and int(m.group(1)) >= 17, p.stdout


def test_composite_uses_the_two_counters():
    """The composite counts through lk_late.h and its `lockstep 1s:` line carries late_start / overrun /
    overrun_max_us; the old ambiguous `late=` key is gone."""
    src = (shellrun.SHELL / "composite" / "shell_composite.cpp").read_text(encoding="utf-8")
    assert '#include "lk_late.h"' in src
    assert "lklate::Count(g_lkLate, row.wait0, row.wait1, g_lkUvDeadline, g_freq.QuadPart)" in src
    assert "late_start=%d overrun=%d overrun_max_us=%.1f" in src
    assert " late=%d" not in src and "late_max_us" not in src
