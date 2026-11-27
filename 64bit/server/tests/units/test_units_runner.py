"""Exit codes and output of test_scene_units.exe (epic 0375, issue 4454; 2026-11-16). Own code.

    set C5_UNITS_TEST_EXE=C:/c5-64bit-work/0375/4454/build/bin/test_scene_units.exe   (or bin-asan)
    set C5R_GAME_DIR=<install>  &  set C5R_EXE=<install>/CivilizationV_DX11.exe
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/units/test_units_runner.py -q

Without C5_UNITS_TEST_EXE every test fails (a skipped test counts as failed).
"""
import os
import re
import subprocess
from pathlib import Path

EXE = os.environ.get('C5_UNITS_TEST_EXE', '')
REPO = Path(__file__).resolve().parents[3]


def run(args, env_extra=None, drop=()):
    assert EXE and Path(EXE).exists(), 'C5_UNITS_TEST_EXE does not name test_scene_units.exe'
    env = dict(os.environ)
    for k in drop:
        env.pop(k, None)
    env.update(env_extra or {})
    return subprocess.run([EXE] + args, capture_output=True, text=True, env=env, timeout=900)


def test_every_case_passes():
    cp = run([])
    assert cp.returncode == 0, cp.stdout + cp.stderr
    # Epic 0040 (MC #4032): N read from the output, so a new case needs no edit here; 48 cases at 4755 (2026-11-17).
    m = re.search(r'^test_scene_units: (\d+) of (\d+) cases passed$', cp.stdout, re.M)
    assert m, cp.stdout
    passed, total = int(m.group(1)), int(m.group(2))
    assert passed == total and total >= 48, cp.stdout
    assert len(re.findall(r'^CASE \S+ PASS$', cp.stdout, re.M)) == total, cp.stdout
    assert 'FAIL' not in cp.stdout and 'SKIP' not in cp.stdout


def test_unknown_case_exit_2():
    cp = run(['no_such_case'])
    assert cp.returncode == 2 and 'test_scene_units: no case matches' in cp.stdout, cp.stdout


def test_module_cases_without_the_install_exit_99():
    cp = run(['precheck_pass_draw'], drop=('C5R_GAME_DIR', 'C5R_EXE'))
    assert cp.returncode == 99 and 'C5R_GAME_DIR and C5R_EXE must name the install' in cp.stdout, cp.stdout


def test_core_cases_need_no_install():
    cp = run(['mapping', 'colour_unknown', 'switch', 'anim_mapping', 'drain_clock', 'recording_port', 'cull_of'],
             drop=('C5R_GAME_DIR', 'C5R_EXE'))
    assert cp.returncode == 0 and cp.stdout.count(' PASS') == 7, cp.stdout


def test_anim_cases_without_the_install_exit_99():
    """Issue 4456: the controller's module cases need the install (never a skip)."""
    cp = run(['anim_controller'], drop=('C5R_GAME_DIR', 'C5R_EXE'))
    assert cp.returncode == 99 and 'C5R_GAME_DIR and C5R_EXE must name the install' in cp.stdout, cp.stdout


def test_oracle_usage_and_inputs(tmp_path):
    cp = run(['oracle', '--scenes', str(tmp_path)])
    assert cp.returncode == 2 and 'oracle: needs --scenes --db --heightfield --out' in cp.stdout, cp.stdout
    cp = run(['oracle', '--bogus', 'x'])
    assert cp.returncode == 2 and 'oracle: unknown argument --bogus' in cp.stdout, cp.stdout
    cp = run(['oracle', '--scenes', str(tmp_path), '--db', str(tmp_path / 'none.db'), '--heightfield', 'x', '--out',
              str(tmp_path / 'o')])
    assert cp.returncode == 3 and 'oracle: database' in cp.stdout, cp.stdout


def test_precheck_usage_and_inputs(tmp_path):
    """Issue 4460: the pre-check mode (the scene's own pre-check on one database) refuses bad input by name."""
    cp = run(['precheck', '--db', str(tmp_path / 'x.db')])
    assert cp.returncode == 2 and 'precheck: needs --db --install --exe --out' in cp.stdout, cp.stdout
    cp = run(['precheck', '--bogus', 'x'])
    assert cp.returncode == 2 and "precheck: unknown or incomplete argument '--bogus'" in cp.stdout, cp.stdout
    cp = run(['precheck', '--db', str(tmp_path / 'none.db'), '--install', str(tmp_path), '--exe', str(tmp_path / 'e.exe'),
              '--out', str(tmp_path / 'o.tsv')])
    assert cp.returncode == 3 and 'precheck: the environment did not open' in cp.stdout, cp.stdout
