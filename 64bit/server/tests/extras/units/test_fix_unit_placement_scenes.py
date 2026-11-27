"""FixUnitPlacement (option 23) in the offline scene harness: rules E2 and E9 of the contribution guide for extras (issue
4982, epic 0455; research/05-graphics-composition/extras-units.md section 4982). The extra's regression test on recorded
scenes: 4980's c1_mountain bundle (a horseman, a spearman and a catapult at rest on three mountain plots) and its c4_port
bundle (a coastal city with a trireme and a worker on flat ground).

    set C5_SCENESHOT_EXE=<build_sceneshot.py --out DIR>/bin/sceneshot.exe   (a player that links server/src/extras/units/
                    and attaches the unit extras: 4983's two player lines)
    set C5_SCENE_LIBRARY=<the library's bundles folder>
    set C5_SCENE_MODS=<a MODS folder>   (optional: replaces the bundle's recorded mods folder, an instance's live user
                    folder that another run may be writing; W11's C4PORT ReplayFailed, extras-units.md 4982.9)
    set C5_GAME_DIR=<an install outside the pool>, C5R_EXE=<its EXE>   (optional: replace the recorded install, which
                    the player refuses while its pool instance is claimed: exit 9, 4982.11)
    python -I perf/quietlock/quietlock.py run --who <id> --what "placement scenes" -- \
        python -I -m pytest server/tests/extras/units/test_fix_unit_placement_scenes.py -q -p no:cacheprovider

Each bundle replays with its own install and mods folder (the bundle's asset_set and render_decal_mods), as recorded.
Every other option keeps its default (FixAirBases = 1 since D235, so the module may have another extra in effect).
  E2   (both scenes) --dumped --compare --tolerance 1:1 with FixUnitPlacement=0: every recorded frame equal (exit 0); the
       unit extras module linked, and FixUnitPlacement says why it is off;
  E9   (mountain) --dumped --compare --no-pictures with FixUnitPlacement=1: the extra in effect, no LOUD line, and the
       frames differ from the recording (exit 10: the formations moved and their figures raised out of the rock);
  E9   (mountain) the named camera's picture with the extra on differs from the one with it off by the three units' pixels
       (between MIN_PIXELS and MAX_PIXELS);
  E9   (port) with the extra on and no land unit on rough ground: the extra in effect and every frame still equal (exit 0).
A missing variable, library or scene fails (never skipped). Pictures for the review are drawn by the same commands
without --compare (none is committed; D112: DIAGNOSTIC).
"""
import os
import subprocess
import zlib
from pathlib import Path

import pytest

# (bundle prefix, named camera, what it shows)
MOUNTAIN = ('C5H-4980-C1MOUNTAIN_', 'c1_mountain', 'a horseman, a spearman and a catapult at rest on three mountain plots')
PORT = ('C5H-4980-C4PORT_', 'c4_port', 'a coastal city with a trireme and a worker on flat ground')
NOWIN = 0x08000000
# the moved units at the case camera (2554 x 1411): 79,118 pixels in the first run, 2026-11-20 (extras-units.md 4982.8)
MIN_PIXELS, MAX_PIXELS = 20000, 160000


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
    cmd = [str(exe), '--bundle', str(b), '--out', str(out)]
    # the recorded install may lie in a pool instance another run has claimed (the player refuses it, exit 9): an install
    # outside the pool, as test_air_intercept_scenes.py gives
    if os.environ.get('C5_GAME_DIR'):
        cmd += ['--install', str(env_path('C5_GAME_DIR'))]
    if os.environ.get('C5R_EXE'):
        cmd += ['--exe', str(env_path('C5R_EXE'))]
    if os.environ.get('C5_SCENE_MODS'):
        cmd += ['--mods', str(env_path('C5_SCENE_MODS'))]
    cmd += list(args)
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, timeout=1500)
    return cp.returncode, cp.stdout + cp.stderr


def png_rgb(path):
    """The RGB rows of an 8-bit RGB or RGBA PNG (no third-party module): (width, height, bytes per pixel, rows)."""
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'{path} is not a PNG'
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n = int.from_bytes(data[pos:pos + 4], 'big')
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h = int.from_bytes(body[0:4], 'big'), int.from_bytes(body[4:8], 'big')
            depth, ctype, interlace = body[8], body[9], body[12]
            assert depth == 8 and ctype in (2, 6) and interlace == 0, f'{path}: PNG type {ctype}/{depth}/{interlace}'
            bpp = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


@pytest.mark.parametrize('prefix,camera,what', [MOUNTAIN, PORT])
def test_off_is_unchanged(prefix, camera, what, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1', '--no-pictures',
                    '--option', 'FixUnitPlacement=0')
    assert rc == 0, f'{what}: E2 failed, exit {rc}: {text[-2000:]}'
    assert 'units: extras: module linked' in text, f'{what}: the unit extras not linked: {text[-2000:]}'
    assert 'units FixUnitPlacement: off:' in text, f'{what}: no off line: {text[-2000:]}'
    assert 'units extra FixUnitPlacement: in effect' not in text


def test_on_moves_and_differs(tmp_path):
    prefix, camera, what = MOUNTAIN
    rc, text = play(bundle(prefix), tmp_path / 'on', '--dumped', '--compare', '--tolerance', '1:1', '--no-pictures',
                    '--option', 'FixUnitPlacement=1')
    assert 'units extra FixUnitPlacement: in effect' in text, f'{what}: the extra did not start: {text[-2000:]}'
    assert 'units: extras: module linked, ' in text and ' in effect: attached' in text
    assert 'FixUnitPlacement: LOUD' not in text, f'{what}: the extra failed: {text[-2000:]}'
    assert rc == 10, f'{what}: with the extra on the frames must differ from the recording (exit 10), got {rc}'


def test_on_picture_differs_by_the_units(tmp_path):
    prefix, camera, what = MOUNTAIN
    b = bundle(prefix)
    rc, text = play(b, tmp_path / 'off', '--camera', camera, '--option', 'FixUnitPlacement=0')
    assert rc == 0, f'{what}: off picture, exit {rc}: {text[-1500:]}'
    rc, text = play(b, tmp_path / 'on', '--camera', camera, '--option', 'FixUnitPlacement=1')
    assert rc == 0, f'{what}: on picture, exit {rc}: {text[-1500:]}'
    w0, h0, p0, off = png_rgb(tmp_path / 'off' / f'{camera}.png')
    w1, h1, p1, on = png_rgb(tmp_path / 'on' / f'{camera}.png')
    assert (w0, h0) == (w1, h1)
    differing = 0
    for y in range(h0):
        if off[y] == on[y]:
            continue
        a, c = off[y], on[y]
        for x in range(w0):
            i, j = x * p0, x * p1
            if max(abs(a[i] - c[j]), abs(a[i + 1] - c[j + 1]), abs(a[i + 2] - c[j + 2])) > 8:
                differing += 1
    assert MIN_PIXELS <= differing <= MAX_PIXELS, f'{what}: {differing} pixels differ (want {MIN_PIXELS}-{MAX_PIXELS})'


def test_on_flat_ground_is_unchanged(tmp_path):
    prefix, camera, what = PORT
    rc, text = play(bundle(prefix), tmp_path / 'on', '--dumped', '--compare', '--tolerance', '1:1', '--no-pictures',
                    '--option', 'FixUnitPlacement=1')
    assert 'units extra FixUnitPlacement: in effect' in text, f'{what}: the extra did not start: {text[-2000:]}'
    assert 'FixUnitPlacement: LOUD' not in text, f'{what}: the extra failed: {text[-2000:]}'
    assert rc == 0, f'{what}: no land unit on rough ground, so every frame must equal the recording (exit 0), got {rc}: ' \
                    f'{text[-1500:]}'
