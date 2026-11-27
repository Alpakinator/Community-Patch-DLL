"""AirInterceptPatrol (option 27) in the offline scene harness: rules E2 and E9 (epic 0455, issue 4986; research
05-graphics-composition/extras-units.md section "4986"). The extra's regression test on recorded scenes.

    set C5_SCENESHOT_EXE=<a sceneshot.exe that links server/src/extras/units/ with AirInterceptPatrol registered and attaches
                          the unit extras (4983's two lines)>
    set C5_SCENE_LIBRARY=<the library's bundles folder>
    set C5_GAME_DIR=<a game copy whose EXE is the bundles' pins.exe_sha256>   (never the Steam install)
    set C5R_EXE=<that copy's CivilizationV_DX11.exe>
    set C5_SCENE_MODS=<that copy's .../My Games/Sid Meier's Civilization 5/MODS>   (optional, as the extras' scene tests)
    python -I perf/quietlock/quietlock.py run --who <id> --what "AirInterceptPatrol scenes" -- \
        python -I -m pytest server/tests/extras/units/test_air_intercept_scenes.py -q -p no:cacheprovider

Scenes: 4980's c7_intercept (fighter 1070 of the active player on intercept, activity 5 sent at the load's setup; stock and
the server draw it parked at its city [L, 4980]) and c5_air (three aircraft based in city A, none on intercept).
  E2  each scene with AirInterceptPatrol = 0: every recorded frame equal (exit 0), the extra not started;
  E9a c7 with EnableVisualAdditions = 1 and AirInterceptPatrol = 1: the extra in effect, no LOUD line, every frame differs
      from the recording (exit 10: the fighter flies its ring instead of standing at the city);
  E9b c5 with the flag 1: in effect and every frame still equal (exit 0): no aircraft on intercept, nothing moves.
A missing variable, library or scene fails (never skipped). Pictures for the review: the same commands without --compare
(none is committed).
"""
import os
import subprocess
from pathlib import Path

NOWIN = 0x08000000
ON = ['--option', 'EnableVisualAdditions=1', '--option', 'AirInterceptPatrol=1']
OFF = ['--option', 'AirInterceptPatrol=0']
C7 = 'C5H-4980-C7INTERCEPT_'
C5 = 'C5H-4980-C5AIR_'


def env_path(name):
    v = os.environ.get(name, '')
    assert v, f'{name} is not set (see this file\'s docstring)'
    p = Path(v)
    assert p.exists(), f'{name}={v} does not exist'
    return p


def bundle(prefix):
    lib = env_path('C5_SCENE_LIBRARY')
    found = sorted(d for d in lib.iterdir() if d.is_dir() and d.name.startswith(prefix) and not d.name.endswith('-SYNHIDE'))
    assert found, f'no bundle {prefix}* in {lib}'
    return found[-1]


def play(b, out, *args):
    exe = env_path('C5_SCENESHOT_EXE')
    cmd = [str(exe), '--bundle', str(b), '--out', str(out), '--install', str(env_path('C5_GAME_DIR')),
           '--exe', str(env_path('C5R_EXE'))]
    if os.environ.get('C5_SCENE_MODS'):
        cmd += ['--mods', str(env_path('C5_SCENE_MODS'))]
    cmd += list(args)
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, timeout=1500)
    return cp.returncode, cp.stdout + cp.stderr


def test_off_is_unchanged_c7(tmp_path):
    rc, text = play(bundle(C7), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1', *OFF)
    assert rc == 0, f'c7: E2 failed, exit {rc}: {text[-2000:]}'
    assert 'units extra AirInterceptPatrol: in effect' not in text, 'c7: the extra ran with its flag 0'


def test_off_is_unchanged_c5(tmp_path):
    rc, text = play(bundle(C5), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1', *OFF)
    assert rc == 0, f'c5: E2 failed, exit {rc}: {text[-2000:]}'


def test_on_flies_the_interceptor(tmp_path):
    rc, text = play(bundle(C7), tmp_path / 'on', '--dumped', '--compare', '--no-pictures', *ON)
    assert 'units extra AirInterceptPatrol: in effect' in text, f'c7: the extra did not start: {text[-2000:]}'
    assert 'AirInterceptPatrol: LOUD' not in text, f'c7: the extra failed: {text[-2000:]}'
    assert rc == 10, f'c7: the fighter on intercept must fly (every frame differs, exit 10), got {rc}: {text[-2000:]}'


def test_on_leaves_based_aircraft(tmp_path):
    rc, text = play(bundle(C5), tmp_path / 'on', '--dumped', '--compare', '--tolerance', '1:1', *ON)
    assert 'units extra AirInterceptPatrol: in effect' in text, f'c5: the extra did not start: {text[-2000:]}'
    assert 'AirInterceptPatrol: LOUD' not in text, f'c5: the extra failed: {text[-2000:]}'
    assert rc == 0, f'c5: no aircraft on intercept, the frames must equal the recording (exit 0), got {rc}: {text[-2000:]}'
