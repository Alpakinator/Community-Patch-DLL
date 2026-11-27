"""FixAirBases (option 26) in the offline scene harness: rules E2 and E9 (epic 0455, issue 4985; research
05-graphics-composition/extras-units.md section "4985 FixAirBases"). The extra's regression test on recorded scenes.

    set C5_SCENESHOT_EXE=<a sceneshot.exe that links server/src/extras/units/ and attaches the unit extras (4983's two lines)>
    set C5_SCENE_LIBRARY=<the library's bundles folder>
    set C5_GAME_DIR=<a game copy whose EXE is the bundles' pins.exe_sha256>   (never the Steam install)
    set C5R_EXE=<that copy's CivilizationV_DX11.exe>
    set C5_SCENE_MODS=<that copy's .../My Games/Sid Meier's Civilization 5/MODS>   (optional, as the extras' scene tests)
    set C5_SCENE_WORK=<a scratch folder on an SSD with 1 GB free>   (the synthetic bundles; default pytest's tmp folder)
    python -I perf/quietlock/quietlock.py run --who <id> --what "FixAirBases scenes" -- \
        python -I -m pytest server/tests/extras/units/test_fix_air_bases_scenes.py -q -p no:cacheprovider

Scenes: 4980's c5_air (fighter 1064, bomber 1065, jet fighter 1066 based in city A) and c6_carrier (fighters 1068, 1069
carried by carrier 1067), both recorded after a LOAD, so stock and the server draw every aircraft there [L, 4980].
  E2  each scene with FixAirBases = 0: every recorded frame equal (exit 0);
  E9a each scene with EnableBugfixes = 1 and FixAirBases = 1: the extra in effect and every frame still equal (exit 0):
      after a load nothing hides an aircraft, so the fix changes nothing there (the after-load state is the target);
  E9b the same scene with the aircraft's visible flag cleared in feed.bin's unit tables (synhide.py: what the DLL's
      updateCenterUnit leaves after a live rebase, a carrier move or a kill on their plot): with the flag 0 the frames
      differ from the recording (exit 10: stock does not draw them), with the flag 1 they equal it again (exit 0: drawn as
      after the load, at the same place, byte for byte against the server's own dumps of the after-load frames).
A missing variable, library or scene fails (never skipped). Pictures for the review: the same commands without --compare
(none is committed).
"""
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import synhide  # noqa: E402

# (bundle prefix, what it shows, the aircraft ids to hide in the synthetic copy)
SCENES = [
    ('C5H-4980-C5AIR_', 'three aircraft based in a city after a load', [1064, 1065, 1066]),
    ('C5H-4980-C6CARRIER_', 'two fighters carried by a carrier after a load', [1068, 1069]),
]
NOWIN = 0x08000000
ON = ['--option', 'EnableBugfixes=1', '--option', 'FixAirBases=1']
OFF = ['--option', 'FixAirBases=0']


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


@pytest.fixture(scope='module')
def work(tmp_path_factory):
    w = os.environ.get('C5_SCENE_WORK')
    return Path(w) if w else tmp_path_factory.mktemp('fixairbases')


def synthetic(prefix, ids, work):
    src = bundle(prefix)
    dst = work / (src.name + '-SYNHIDE')
    rc = synhide.make(src, dst, set(ids))
    assert rc == 0, f'synhide failed for {src.name}: {rc}'
    return dst


@pytest.mark.parametrize('prefix,what,ids', SCENES)
def test_off_is_unchanged(prefix, what, ids, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1', *OFF)
    assert rc == 0, f'{what}: E2 failed, exit {rc}: {text[-2000:]}'
    assert 'units extra FixAirBases: in effect' not in text, f'{what}: the extra ran with its flag 0'


@pytest.mark.parametrize('prefix,what,ids', SCENES)
def test_on_after_load_is_unchanged(prefix, what, ids, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'on', '--dumped', '--compare', '--tolerance', '1:1', *ON)
    assert 'units extra FixAirBases: in effect' in text, f'{what}: the extra did not start: {text[-2000:]}'
    assert 'FixAirBases: LOUD' not in text, f'{what}: the extra failed: {text[-2000:]}'
    assert rc == 0, f'{what}: after a load nothing is hidden, the frames must equal the recording (exit 0), got {rc}: {text[-2000:]}'


@pytest.mark.parametrize('prefix,what,ids', SCENES)
def test_hidden_off_differs_on_restores(prefix, what, ids, work, tmp_path):
    syn = synthetic(prefix, ids, work)
    rc, text = play(syn, tmp_path / 'syn-off', '--dumped', '--compare', '--no-pictures', *OFF)
    assert rc == 10, f'{what}, aircraft hidden, flag 0: the frames must differ from the after-load recording (exit 10), got {rc}: {text[-2000:]}'
    rc, text = play(syn, tmp_path / 'syn-on', '--dumped', '--compare', '--tolerance', '1:1', *ON)
    assert 'units extra FixAirBases: in effect' in text, f'{what}: the extra did not start: {text[-2000:]}'
    assert 'FixAirBases: LOUD' not in text, f'{what}: the extra failed: {text[-2000:]}'
    assert rc == 0, f'{what}, aircraft hidden, flag 1: the frames must equal the after-load recording (exit 0), got {rc}: {text[-2000:]}'
