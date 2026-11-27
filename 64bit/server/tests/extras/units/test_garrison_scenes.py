"""GarrisonVisible in the offline scene harness: rules E2 and E9 of the contribution guide for extras (issue 4983, epic
0455; research/05-graphics-composition/extras-units.md section 4983). The extra's regression test on recorded scenes:
4980's garrison bundles (a city with its garrison; a city with its garrison and a second unit that stock draws).

    set C5_SCENESHOT_EXE=<build_sceneshot.py --out DIR>/bin/sceneshot.exe   (a player that links server/src/extras/units/
                    and attaches the unit extras: 4983's two player hunks)
    set C5_SCENE_LIBRARY=<the library's bundles folder>
    python -I perf/quietlock/quietlock.py run --who <id> --what "garrison scenes" -- \
        python -I -m pytest server/tests/extras/units/test_garrison_scenes.py -q -p no:cacheprovider

Each bundle replays with its own install and mods folder (the bundle's asset_set and render_decal_mods), as recorded.
For each scene of SCENES (the newest bundle whose name starts with the prefix):
  E2   --dumped --compare --tolerance 1:1 with GarrisonVisible=0: every recorded frame equal (exit 0); the unit extras
       module is linked (attached or dropped by the other unit extras' defaults: FixAirBases is 1 since 2026-11-22),
       and GarrisonVisible says why it is off;
  E9   --dumped --compare --no-pictures with EnableVisualAdditions=1 and GarrisonVisible=1: the extra in effect, no LOUD
       line, and the frames differ from the recording (exit 10: the garrison is drawn);
  E9   the named camera's picture with the extra on differs from the one with it off in a region of the size of one
       formation (between MIN_PIXELS and MAX_PIXELS pixels), and nowhere else.
Ticket 4983 (D243, 2026-11-23): the same for PerimeterGarrisons (id 32, look (d) as the players' option): E2 with both
garrison keys at 0 says why each is off; the two on tests run once per key (KEYS).
A missing variable, library or scene fails (never skipped). Pictures for the review are drawn by the same commands
without --compare (none is committed; D112: DIAGNOSTIC).
"""
import os
import re
import subprocess
import zlib
from pathlib import Path

import pytest

# (bundle prefix, named camera, what it shows)
SCENES = [
    ('C5H-4980-C3AGARRISON_', 'c3a_garrison', 'a city whose only unit is its garrison (a spearman)'),
    ('C5H-4980-C3BGARRISON2_', 'c3b_garrison2', 'a coastal city with its garrison (a swordsman) and an archer stock draws'),
]
# the garrison options: GarrisonVisible (24, look (a), a test value since D243) and PerimeterGarrisons (32, look (d))
KEYS = ['GarrisonVisible', 'PerimeterGarrisons']
NOWIN = 0x08000000
# one formation at the case camera (2554 x 1411): 4983's round r4 measured 9,000-19,000 differing pixels per look
MIN_PIXELS, MAX_PIXELS = 2000, 60000


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
    cmd = [str(exe), '--bundle', str(b), '--out', str(out)] + list(args)
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


@pytest.mark.parametrize('prefix,camera,what', SCENES)
def test_off_is_unchanged(prefix, camera, what, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'off', '--dumped', '--compare', '--tolerance', '1:1', '--no-pictures',
                    '--option', 'GarrisonVisible=0', '--option', 'PerimeterGarrisons=0')
    assert rc == 0, f'{what}: E2 failed, exit {rc}: {text[-2000:]}'
    # the module's count of extras in effect depends on the other unit extras' defaults (FixAirBases defaults to 1 since
    # 2026-11-22, D235): only that the module is linked is asserted here, and the garrison extras' own lines
    assert re.search(r'units: extras: module linked, \d+ in effect: (dropped|attached)', text), \
        f'{what}: the unit extras not attached: {text[-2000:]}'
    for key in KEYS:
        assert f'units {key}: off: EnableVisualAdditions = 0 (default)' in text, f'{what}: no {key} off line: {text[-2000:]}'
        assert f'units extra {key}: in effect' not in text


@pytest.mark.parametrize('key', KEYS)
@pytest.mark.parametrize('prefix,camera,what', SCENES)
def test_on_draws_and_differs(prefix, camera, what, key, tmp_path):
    rc, text = play(bundle(prefix), tmp_path / 'on', '--dumped', '--compare', '--tolerance', '1:1', '--no-pictures',
                    '--option', 'EnableVisualAdditions=1', '--option', f'{key}=1')
    assert f'units extra {key}: in effect' in text, f'{what}: the extra did not start: {text[-2000:]}'
    assert re.search(r'units: extras: module linked, [1-9]\d* in effect: attached', text), \
        f'{what}: the unit extras not attached: {text[-2000:]}'
    assert f'{key}: LOUD' not in text, f'{what}: the extra failed: {text[-2000:]}'
    assert rc == 10, f'{what}: with the extra on the frames must differ from the recording (exit 10), got {rc}'


@pytest.mark.parametrize('key', KEYS)
@pytest.mark.parametrize('prefix,camera,what', SCENES)
def test_on_picture_differs_by_one_formation(prefix, camera, what, key, tmp_path):
    b = bundle(prefix)
    rc, text = play(b, tmp_path / 'off', '--camera', camera, '--option', f'{key}=0')
    assert rc == 0, f'{what}: off picture, exit {rc}: {text[-1500:]}'
    rc, text = play(b, tmp_path / 'on', '--camera', camera, '--option', 'EnableVisualAdditions=1', '--option',
                    f'{key}=1')
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
