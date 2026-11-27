"""Tests of run_all.py's scene-bundle timeout handling and build_scenebundle_tests.py's timeouts (ticket 0800). Own code,
2026-11-26 (GPL-3.0-or-later). No build, no game: the modules are imported and their functions called.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider server/tests/render/test_run_all.py

What it checks: (1) run_all forwards --sb-timeout / --sb-timeout-scale to build_scenebundle_tests.py --run-only, and the
builder scales sceneshot_test's TIMEOUTS entry (7200 s) by --timeout-scale; (2) a program that times out is reported as
a TIMEOUT naming it, NOT A RESULT: the builder exits 4 (NO VERDICT) when timeouts are the only problem, run_all marks the
suite TIMEOUT (never FAIL) and ends with `run_all: NO VERDICT: ...` and exit 4; a real failure still wins (exit 1).
"""
import importlib.util
import subprocess
import sys
import types
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ra = _load('run_all_0800', HERE / 'run_all.py')
sbt = _load('build_scenebundle_tests_0800', HERE / 'scenebundle' / 'build_scenebundle_tests.py')


def test_sceneshot_test_floor_is_7200_and_scales():
    assert sbt.TIMEOUTS['sceneshot_test'] == 7200
    assert sbt.timeout_for('sceneshot_test', 600) == 7200
    assert sbt.timeout_for('sceneshot_test', 600, 2.0) == 14400
    assert sbt.timeout_for('sceneshot_test', 9000) == 9000          # a larger --timeout still wins
    assert sbt.timeout_for('recorder_test', 600) == 600
    assert sbt.timeout_for('recorder_test', 600, 1.5) == 900
    assert sbt.timeout_for('recorder_test', 600, 0.0001) == 1       # never 0 s


def _args(**kw):
    a = types.SimpleNamespace(sb_timeout=None, sb_timeout_scale=1.0)
    a.__dict__.update(kw)
    return a


def test_run_all_forwards_timeout_options(tmp_path):
    c = ra.Ctx(tmp_path, False, None)
    base = ra.sb_argv(c, _args())
    assert '--run-only' in base and '--timeout' not in base and '--timeout-scale' not in base
    argv = ra.sb_argv(c, _args(sb_timeout=900, sb_timeout_scale=2.0))
    assert argv[argv.index('--timeout') + 1] == '900'
    assert argv[argv.index('--timeout-scale') + 1] == '2'
    asan = ra.sb_argv(ra.Ctx(tmp_path, True, None), _args(sb_timeout_scale=1.5))
    assert '--asan' in asan and asan[asan.index('--timeout-scale') + 1] == '1.5'


def test_run_all_options_parse():
    a = ra.parse_args(['--list', '--sb-timeout', '900', '--sb-timeout-scale', '2'])
    assert a.sb_timeout == 900 and a.sb_timeout_scale == 2.0
    a = ra.parse_args(['--list'])
    assert a.sb_timeout is None and a.sb_timeout_scale == 1.0


def _fake_run(timeout_stems):
    """subprocess.run for run_tests: TimeoutExpired for the stems named, else a passing program."""
    def run(cmd, **kw):
        stem = Path(cmd[0]).stem
        if stem in timeout_stems:
            raise subprocess.TimeoutExpired(cmd, kw.get('timeout'))
        return subprocess.CompletedProcess(cmd, 0, stdout='RESULT: 3 passed, 0 failed, 0 skipped\n', stderr='')
    return run


def _builder_run(monkeypatch, tmp_path, capsys, timeout_stems, fail_stems=(), scale=1.0):
    monkeypatch.setattr(sbt.subprocess, 'run', _fake_run(timeout_stems))
    exes = [tmp_path / (s + '.exe') for s in ('a_test', 'recorder_test', 'sceneshot_test')]
    failures, timeouts = sbt.run_tests(tmp_path, tmp_path / 'work', exes, False, 600, scale)
    failures += [f'{s}.exe: exit 1' for s in fail_stems]
    with pytest.raises(SystemExit) as e:
        sbt.finish_run(tmp_path, exes, failures, timeouts)
        raise SystemExit(0)
    return e.value.code, capsys.readouterr().out


def test_builder_timeout_is_no_verdict(monkeypatch, tmp_path, capsys):
    code, out = _builder_run(monkeypatch, tmp_path, capsys, {'sceneshot_test'}, scale=2.0)
    assert code == sbt.EXIT_NO_VERDICT == 4
    assert 'build_scenebundle_tests: TIMEOUT sceneshot_test.exe: no verdict after 14400 s (NOT A RESULT' in out
    assert 'build_scenebundle_tests: FAIL ' not in out
    assert 'build_scenebundle_tests: SUMMARY 2 passed, 0 failed, 1 timed out of 3 test program(s)' in out
    assert out.rstrip().splitlines()[-1].startswith('build_scenebundle_tests: NO VERDICT: 1 of 3')
    # run_all reads that output as a TIMEOUT suite, never a failed one
    n = ra.sb_counts(out)
    assert n['passed'] == 2 and n['failed'] == 0 and n['timeouts'] == ['sceneshot_test.exe after 14400 s']
    reasons, timeouts = ra.verdict(n, code, 'sbrun')
    assert reasons == [] and timeouts == ['sceneshot_test.exe after 14400 s']


def test_builder_failure_still_fails(monkeypatch, tmp_path, capsys):
    code, out = _builder_run(monkeypatch, tmp_path, capsys, {'sceneshot_test'}, fail_stems=('a_test',))
    assert code == 1
    assert 'SUMMARY 1 passed, 1 failed, 1 timed out of 3' in out
    assert 'timed out (no verdict, NOT A RESULT): sceneshot_test.exe after 7200 s' in out
    n = ra.sb_counts(out)
    reasons, _ = ra.verdict(n, code, 'sbrun')
    assert n['failed'] == 1 and 'exit 1' in reasons


def test_builder_all_pass(monkeypatch, tmp_path, capsys):
    code, out = _builder_run(monkeypatch, tmp_path, capsys, set())
    assert code == 0 and 'build_scenebundle_tests: OK: 3 test program(s) passed' in out
    n = ra.sb_counts(out)
    assert n == {'passed': 3, 'failed': 0, 'skipped': 0, 'errors': 0, 'timeouts': []}


def test_sb_counts_old_builder_output():
    out = 'build_scenebundle_tests: FAIL x.exe: exit 1\nbuild_scenebundle_tests: FAILED: 1 failure(s) in 3 test program(s)\n'
    assert ra.sb_counts(out) == {'passed': 0, 'failed': 1, 'skipped': 0, 'errors': 0, 'timeouts': []}


def test_exit_4_without_timeouts_is_a_failure():
    reasons, _ = ra.verdict({'passed': 3, 'failed': 0, 'skipped': 0, 'errors': 0, 'timeouts': []}, 4, 'sbrun')
    assert 'exit 4' in reasons
    reasons, _ = ra.verdict({'passed': 3, 'failed': 0, 'skipped': 0, 'errors': 0}, 4, 'pytest')
    assert 'exit 4' in reasons


def test_finish_lines_and_exit(tmp_path):
    c = ra.Ctx(tmp_path, False, None)
    lines = []
    assert ra.finish(c, [('render-host', 'PASS'), ('scenebundle-programs', 'TIMEOUT')], 'plain', lines) == 4
    assert lines[-1].startswith('run_all: NO VERDICT: 1 of 2 suite(s) timed out (plain): scenebundle-programs')
    assert (tmp_path / 'summary.txt').read_text(encoding='utf-8').strip().endswith(lines[-1])
    lines = []
    assert ra.finish(c, [('render-host', 'FAIL'), ('scenebundle-programs', 'TIMEOUT')], 'plain', lines) == 1
    assert lines[-1] == ('run_all: FAILED: 1 of 2 suite(s) (plain): render-host; timed out (no verdict): '
                         'scenebundle-programs')
    lines = []
    assert ra.finish(c, [('render-host', 'PASS')], 'plain', lines) == 0
    assert lines[-1] == 'run_all: OK: 1 suite(s) passed (plain)'


if __name__ == '__main__':
    sys.exit(pytest.main([__file__, '-q']))
