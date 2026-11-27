"""RoadLayout (ticket 4934's FixRoadLayout; ticket 4940) in the offline scene harness: rule A / E2 (off is unchanged) and
E9 (on draws and differs) of the contribution guide for extras (epic 0454; docs/extras/roads.md 4b, 4g). The extra's
regression test on recorded
scenes; the environment is test_visible_outline_scenes.py's:

    set C5_SCENESHOT_EXE=<build_sceneshot.py --out DIR>/bin/sceneshot.exe   (a player that links server/src/extras/)
    set C5_SCENE_LIBRARY=<the library's bundles folder>
    set C5_GAME_DIR=<a game copy whose EXE is the bundles' pins.exe_sha256>   (never the Steam install)
    set C5R_EXE=<that copy's CivilizationV_DX11.exe>
    set C5_SCENE_MODS=<that copy's MODS folder>   (optional)
    python -I perf/quietlock/quietlock.py run --who <id> --what "roads scenes" -- \
        python -I -m pytest server/tests/extras/test_roads_scenes.py -q -p no:cacheprovider

For each scene (the newest bundle whose name starts with the prefix):
  E2   --dumped --compare --tolerance 1:1 with RoadLayout=0, and with RoadLayout=1 and RoadLayout=2 while
       EnableVisualAdditions is 0 (its default: every value is a visual addition, so stock): every recorded frame equal (exit 0), and no road
       producer runs (no "roadlayout:" line): rule A, the option off changes no picture;
  E9   --dumped --compare --no-pictures with RoadLayout=1 and EnableVisualAdditions=1, and with RoadLayout=2,
       EnableVisualAdditions=1, RoadJunction=1, RoadJitter=3 (ticket 4940): the producer names its layout, lays out the
       roads from the forest's recorded plot table, drops the tap's route curves and adds its own; on the huge late game the
       frames differ from the recording (exit 10, the negative of E2), the EXE's route cells to clear are counted (MC #7664's
       count), and all connections draws more road decals than the tidy layout (no edge is left out by the ring rule);
  and the refusal of an out-of-range value (exit 2): RoadLayout=3, RoadJunction=4, RoadJitter=8.
A missing variable, library or scene fails (never skipped). Pictures for the review are drawn by the same commands
without --compare (none is committed).
"""
import os
import re
import subprocess
from pathlib import Path

import pytest

# (bundle prefix, what it shows, whether the scene has routes the layout changes in view)
SCENES = [
    ('C5H-0301-TINY_', 'a small map early in a game', False),
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
    if os.environ.get('C5_SCENE_MODS'):
        cmd += ['--mods', str(env_path('C5_SCENE_MODS'))]
    cmd += list(args)
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, timeout=1500)
    return cp.returncode, cp.stdout + cp.stderr


# (the --option lines, what the player must say it took, what the producer must say it lays out) per layout (ticket 4940)
OFF = [
    (('RoadLayout=0',), 'road layout 0, junction 0, jitter 0 (--option)'),
    # RoadLayout is a visual addition (every value, the owner 2026-11-23, one-off 9081): EnableVisualAdditions at its default 0
    # gives stock, for 1 and for 2
    (('RoadLayout=1',), 'road layout 0, junction 0, jitter 0 (--option)'),
    (('RoadLayout=2', 'RoadJunction=1', 'RoadJitter=3'), 'road layout 0, junction 0, jitter 0 (--option)'),
]
ON = [
    (('RoadLayout=1', 'EnableVisualAdditions=1'), 'road layout 1, junction 0, jitter 0 (--option)',
     'layout 1 (tidy, variant (e))'),
    (('RoadLayout=2', 'EnableVisualAdditions=1', 'RoadJunction=1', 'RoadJitter=3'),
     'road layout 2, junction 1, jitter 3 (--option)', 'layout 2 (all connections), junction cross, jitter 3'),
]


def options(lines):
    out = []
    for line in lines:
        out += ['--option', line]
    return out


@pytest.mark.parametrize('prefix,what,routes', SCENES)
@pytest.mark.parametrize('lines,took', OFF)
def test_off_is_unchanged(prefix, what, routes, lines, took, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1', *options(lines))
    assert rc == 0, f'{what} {lines}: E2 failed, exit {rc}: {text[-2000:]}'
    assert took in text, f'{what}: the player did not take the option: {text[-2000:]}'
    assert 'roadlayout:' not in text, f'{what} {lines}: the road producer ran with the extra off'


def lay_out(prefix, what, routes, lines, took, says, out):
    rc, text = play(bundle(prefix), out, '--dumped', '--compare', '--no-pictures', *options(lines))
    assert took in text, f'{what}: the player did not take the option: {text[-2000:]}'
    assert 'road layout from the forest\'s recorded plot table' in text, f'{what}: no input for the producer: {text[-2000:]}'
    assert 'LOUD: roadlayout' not in text, f'{what}: the producer failed: {text[-2000:]}'
    assert says in text, f'{what}: the producer did not name its layout ({says}): {text[-2000:]}'
    m = re.search(r'roadlayout: (\d+) route plots -> (\d+) decals \(\d+ vertices\), (\d+) edges left out', text)
    assert m, f'{what}: the producer never laid out the roads: {text[-2000:]}'
    if routes:
        assert int(m.group(1)) > 0 and int(m.group(2)) > 0, f'{what}: no route plot: {m.group(0)}'
        dropped = re.search(r'roadlayout: tap set generation \d+: \d+ decals, (\d+) route curves dropped, (\d+) road decals added', text)
        assert dropped and int(dropped.group(1)) > 0 and int(dropped.group(2)) > 0, f'{what}: no route curve replaced: {text[-2000:]}'
        clear = re.search(r"the EXE's route cells to clear: (\d+) cells on (\d+) plots from (\d+) route curves; (\d+) cells on (\d+) "
                          r"plots kept", text)
        assert clear and int(clear.group(1)) > 0, f'{what}: no route cell counted to clear: {text[-2000:]}'
        assert rc == 10, f'{what}: with the extra on the frames must differ from the recording (exit 10), got {rc}'
    else:
        assert rc in (0, 10), f'{what}: exit {rc}: {text[-2000:]}'
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


@pytest.mark.parametrize('prefix,what,routes', SCENES)
def test_on_lays_out_and_differs(prefix, what, routes, tmp_path):
    (lines1, took1, says1), (lines2, took2, says2) = ON
    plots1, decals1, left1 = lay_out(prefix, what, routes, lines1, took1, says1, tmp_path / 'tidy')
    plots2, decals2, left2 = lay_out(prefix, what, routes, lines2, took2, says2, tmp_path / 'all')
    assert plots1 == plots2, f'{what}: the two layouts read different route plots ({plots1}, {plots2})'
    assert left2 == 0, f'{what}: all connections left {left2} edges out'
    if routes:
        assert left1 > 0 and decals2 > decals1, \
            f'{what}: all connections should draw more ({decals2} decals) than the tidy layout ({decals1}, {left1} edges left out)'


@pytest.mark.parametrize('line', ['RoadLayout=3', 'RoadJunction=4', 'RoadJitter=8', 'FixRoadLayout=1'])
def test_option_refusal(line, tmp_path):
    rc, text = play(bundle(SCENES[0][0]), tmp_path / 'bad', '--dumped', '--option', line)
    assert rc == 2, f'--option {line}: exit {rc} (want 2): {text[-1500:]}'
    assert '--option' in text
