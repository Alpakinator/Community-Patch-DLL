"""VisibleAreaOutline in the offline scene harness: rules E2 and E9 of the contribution guide for extras (ticket 4938,
epic 0454; research/05-graphics-composition/extras.md section X10). The extra's regression test on recorded scenes.

    set C5_SCENESHOT_EXE=<build_sceneshot.py --out DIR>/bin/sceneshot.exe   (a player that links server/src/extras/)
    set C5_SCENE_LIBRARY=<the library's bundles folder>
    set C5_GAME_DIR=<a game copy whose EXE is the bundles' pins.exe_sha256>   (never the Steam install)
    set C5R_EXE=<that copy's CivilizationV_DX11.exe>
    set C5_SCENE_MODS=<that copy's .../My Games/Sid Meier's Civilization 5/MODS>   (optional: its mods database must have the
                    mods of the recording active, or the unit pre-check fails, exit 8)
    python -I perf/quietlock/quietlock.py run --who <id> --what "extras scenes" -- \
        python -I -m pytest server/tests/extras/test_visible_outline_scenes.py -q -p no:cacheprovider

For each scene of SCENES (the newest bundle whose name starts with the prefix):
  E2   --dumped --compare --tolerance 1:1 with VisibleAreaOutline=0: every recorded frame equal (exit 0), and the scene
       extras module does not run (no "extras: VisibleAreaOutline" line and no "LOUD: extras:" line; the unit extras'
       attach line "units: extras: ..." of a units-on bundle is allowed, epic 0455's landing);
  E9   --dumped --compare --no-pictures with EnableVisualAdditions=1 and VisibleAreaOutline=1: the extra in effect; on
       the huge late game (edges to draw) the frames differ from the recording (exit 10, Differs: the negative of E2);
       on the Tiny game at turn 8 (28 visible plots, no seen plot: nothing to outline, 2026-11-19 [L]) they stay equal
       (exit 0) with "first draw: 0 edges";
  and the refusals of --option (exit 2): an unknown key, a key outside [Extras], a value out of range.
A missing variable, library or scene fails (the harness's rule: never skipped). Pictures for the review are drawn by the
same commands without --compare (none is committed).
"""
import os
import subprocess
from pathlib import Path

import pytest

# (bundle prefix, what it shows, whether the outline has edges to draw there)
SCENES = [
    ('C5H-0301-TINY_', 'a small map early in a game (no seen plot yet)', False),
    ('C5H-0301-HUGE_', 'a huge map late in a game', True),
]
NOWIN = 0x08000000


def env_path(name):
    v = os.environ.get(name, '')
    assert v, f'{name} is not set (see this file\'s docstring)'
    p = Path(v)
    assert p.exists(), f'{name}={v} does not exist'
    return p


def bundle(prefix):
    lib = env_path('C5_SCENE_LIBRARY')
    found = sorted(d for d in lib.iterdir() if d.is_dir() and d.name.startswith(prefix))
    assert found, f'no bundle {prefix}* in {lib}'
    return found[-1]


def play(b, out, *args):
    exe = env_path('C5_SCENESHOT_EXE')
    cmd = [str(exe), '--bundle', str(b), '--out', str(out), '--install', str(env_path('C5_GAME_DIR')),
           '--exe', str(env_path('C5R_EXE'))]
    if os.environ.get('C5_SCENE_MODS'):   # the mods folder (else the bundle's render_decal_mods, the recording instance's)
        cmd += ['--mods', str(env_path('C5_SCENE_MODS'))]
    cmd += list(args)
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, timeout=1500)
    return cp.returncode, cp.stdout + cp.stderr


@pytest.mark.parametrize('prefix,what,edges', SCENES)
def test_off_is_unchanged(prefix, what, edges, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1',
                    '--option', 'VisibleAreaOutline=0')
    assert rc == 0, f'{what}: E2 failed, exit {rc}: {text[-2000:]}'
    assert 'extras: VisibleAreaOutline' not in text and 'LOUD: extras:' not in text, f'{what}: the scene extras module ran with the extra off'


@pytest.mark.parametrize('prefix,what,edges', SCENES)
def test_on_draws_and_differs(prefix, what, edges, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'on', '--dumped', '--compare', '--no-pictures',
                    '--option', 'EnableVisualAdditions=1', '--option', 'VisibleAreaOutline=1')
    assert 'extras: VisibleAreaOutline: in effect' in text, f'{what}: the extra did not start: {text[-2000:]}'
    assert 'off for this session' not in text and 'not drawn:' not in text, f'{what}: the extra failed: {text[-2000:]}'
    assert 'VisibleAreaOutline: first draw:' in text, f'{what}: the extra never drew: {text[-2000:]}'
    if edges:
        assert 'first draw: 0 edges' not in text, f'{what}: no edge drawn: {text[-2000:]}'
        assert rc == 10, f'{what}: with the extra on the frames must differ from the recording (exit 10), got {rc}'
    else:
        assert 'first draw: 0 edges' in text, f'{what}: edges where none were expected: {text[-2000:]}'
        assert rc == 0, f'{what}: nothing to outline, so the frames must equal the recording (exit 0), got {rc}'


@pytest.mark.parametrize('opt', ['NoSuchExtra=1', 'ServerRenderer=1', 'VisibleAreaOutline=2', 'VisibleAreaOutline', '=1'])
def test_option_refusals(opt, tmp_path):
    rc, text = play(bundle(SCENES[0][0]), tmp_path / 'bad', '--dumped', '--option', opt)
    assert rc == 2, f'--option {opt}: exit {rc} (want 2): {text[-1500:]}'
    assert '--option' in text
