"""3904: keys absent, the picture bit for bit: render_host_test.exe of the base (3903's head without 3904) and of the task,
each scenario three times per build (the same view with a new frame stamp at 40 Hz, the 40th published frame), render_test_dump (colour BMP and depth) compared byte for byte. Epic 0301, task 3904, 2026-11-15 (GPL-3.0-or-later); a driver over
server/tests/render/renderfx.py (the rig of test_render_host.py), on a hidden desktop.

usage: python -I server/tests/render/scenebundle/recorder_bitcompare.py REPO_ROOT OUT BASE_BIN TASK_BIN EXE
(BASE_BIN and TASK_BIN: build_render_tests.py --out DIR/bin of the two trees; exit 0 when every scenario is equal)
"""
import hashlib
import sys
import time
from pathlib import Path

root, out, base, task, exe = (Path(a) for a in sys.argv[1:6])
sys.path.insert(0, str(root / 'server' / 'tests' / 'render'))
sys.path.insert(0, str(root / 'shell' / 'tests'))
import shellrun  # noqa: E402,F401
import desktops  # noqa: E402
import renderfx as fx  # noqa: E402
import gameinstall  # noqa: E402

# ticket 0589: the install is C5_GAME_DIR, else C5R_GAME_DIR, else the setting render_game_dir (was the Steam install,
# Firaxis' new build since 2026-11-19); it and EXE must be build 4390913 (C5_ALLOW_UNKNOWN_EXE_BUILD=1 overrides)
gameinstall.require_exe(exe)
GAME = gameinstall.require_install()

SCEN = {
    'tap': {},
    'bake_standin': {'render_tiles': 'bake', 'render_baker': 'standin', '_feed_plots': 8},
    'tap_overlay0': {'render_overlay': 0},
    # render_fow=0 (issue 3911): with the install's art the fog starts and holds every frame for its 10 s install wait (no
    # revealed plot, no overlay pair in this rig), so no frame was dumped after 0168 reached main (base and task alike)
    'water_freeze': {'render_water': 1, 'render_water_freeze': 1, 'render_fow': 0,
                     '_game_dir': GAME},
}


def one(desk, build, scen, rep):
    keys = dict(SCEN[scen])
    fp = keys.pop('_feed_plots', 0)
    gd = keys.pop('_game_dir', None)
    tmp = out / ('%s_%s_%d' % (build.parent.name, scen, rep))
    tmp.mkdir(parents=True, exist_ok=True)
    rig = fx.Rig(tmp, desk, build, exe, '%s%d%s' % (scen[:6], rep, build.parent.name[-4:]))
    try:
        rig.sec.write_camera(fx.synth_camera(frame=1))
        rig.tap(load_seq=1)
        p = rig.start(seconds=14 if gd else 8, feed_plots=fp, game_dir=gd, render_test_dump=str(rig.dump),
                      render_test_dump_after=40, **keys)
        rig.wait_log(r"state STARTING -> READY", timeout=30)
        for f in range(2, 120):   # the same view with a new frame stamp, 40 Hz: the dumped frame comes after the bakes
            rig.sec.write_camera(fx.synth_camera(frame=f))
            time.sleep(0.025)
        code = rig.finish(p)
        col = rig.dump.read_bytes() if rig.dump.exists() else b''
        dep = Path(str(rig.dump) + '.depth.f32')
        dpt = dep.read_bytes() if dep.exists() else b''
        rec = bool(__import__('re').search(r'recorder: |recorder off', rig.text()))
        return code, hashlib.sha256(col).hexdigest(), hashlib.sha256(dpt).hexdigest(), len(col), len(dpt), rec
    finally:
        rig.close()


def main():
    d = desktops.HiddenDesktop(prefix='C5REC3904')
    res = {}
    bad = []
    try:
        for scen in SCEN:
            for name, b in (('base', base), ('task', task)):
                for rep in (0, 1, 2):
                    r = one(d.name, b, scen, rep)
                    res[(scen, name, rep)] = r
                    print('%-13s %s rep %d: exit %d colour %s (%d B) depth %s (%d B)%s' % (scen, name, rep, r[0], r[1][:16], r[3],
                          r[2][:16], r[4], ' RECORDER LINE' if r[5] else ''), flush=True)
                    if r[0] != 0 or r[3] == 0 or r[5]:
                        bad.append('%s %s %d: exit %d, %d colour bytes, recorder line %s' % (scen, name, rep, r[0], r[3], r[5]))
            vals = {(res[(scen, n, k)][1], res[(scen, n, k)][2]) for n in ('base', 'task') for k in (0, 1, 2)}
            same = len(vals) == 1
            print('%-13s %s' % (scen, 'EQUAL (base deterministic, task == base, colour and depth)' if same else 'DIFFERENT: %s' % vals), flush=True)
            if not same:
                bad.append(scen + ' differs')
    finally:
        d.close()
    print('bitcompare: %s' % ('OK' if not bad else 'FAILED: ' + '; '.join(bad)), flush=True)
    return 0 if not bad else 1


if __name__ == '__main__':
    sys.exit(main())
