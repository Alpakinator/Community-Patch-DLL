"""The frame-jobs pool's suite (ticket 0711, J1): runs frame_jobs_test.exe and its negative children.

    C5_FRAME_JOBS_BUILD=DIR/bin PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q server/tests/render/frame_jobs

DIR from build_frame_jobs_tests.py (DIR/bin-asan for the ASan build). Without C5_FRAME_JOBS_BUILD every test FAILS (a run
with skipped tests counts as failed, COMMON rule 9).
"""
import os
import subprocess
from pathlib import Path

import pytest

NOWIN = 0x08000000
FATAL_EXIT = 0xF1   # kFrameJobsFatalExit


def exe():
    d = os.environ.get('C5_FRAME_JOBS_BUILD')
    if not d:
        pytest.fail('C5_FRAME_JOBS_BUILD is not set (the folder holding frame_jobs_test.exe)')
    p = Path(d) / 'frame_jobs_test.exe'
    if not p.exists():
        pytest.fail(f'{p} missing')
    return p


def run(*args, timeout=300):
    return subprocess.run([str(exe()), *args], capture_output=True, text=True, errors='replace', timeout=timeout,
                          creationflags=NOWIN)


def test_in_process_suite():
    cp = run()
    print(cp.stdout[-4000:])
    print(cp.stderr[-4000:])
    assert cp.returncode == 0, cp.stdout[-2000:] + cp.stderr[-2000:]
    assert 'frame_jobs_test: OK' in cp.stdout
    assert 'CHECK FAILED' not in cp.stderr


def test_owner_violation_is_fatal_with_checks_on():
    cp = run('--child', 'owner-fatal', timeout=60)
    assert cp.returncode == FATAL_EXIT, (cp.returncode, cp.stdout, cp.stderr)
    assert 'LOUD: frame jobs: UnitsHost::drawPass (child) called while a frame job owns the module units' in cp.stderr
    assert 'FATAL' in cp.stderr
    assert 'child owner-fatal' not in cp.stdout   # nothing ran after the violation


def test_owner_violation_is_refused_and_counted_without_fatal():
    cp = run('--child', 'owner-counted', timeout=60)
    assert cp.returncode == 0, (cp.returncode, cp.stdout, cp.stderr)
    assert 'child owner-counted: mayCall refused, violations 1' in cp.stdout
    assert 'LOUD: frame jobs: UnitsHost::drawPass (child)' in cp.stderr
    assert 'FATAL' not in cp.stderr


def test_join_on_another_thread_is_fatal_with_checks_on():
    cp = run('--child', 'join-thread', timeout=60)
    assert cp.returncode == FATAL_EXIT, (cp.returncode, cp.stdout, cp.stderr)
    assert 'joined on another thread than its fork' in cp.stderr
    assert 'not fatal' not in cp.stdout


def test_two_writers_at_once_is_fatal_with_checks_on():
    # MC #12834 (ticket 0711): route S's trigger section has one writer thread at a time
    cp = run('--child', 'writer-fatal', timeout=60)
    assert cp.returncode == FATAL_EXIT, (cp.returncode, cp.stdout, cp.stderr)
    assert 'child thread b writes the child section while thread' in cp.stderr
    assert 'FATAL' in cp.stderr
    assert 'not fatal' not in cp.stdout


def test_bad_arguments_exit_64():
    assert run('--child', 'nothing', timeout=60).returncode == 64
    assert run('--bogus', timeout=60).returncode == 64
