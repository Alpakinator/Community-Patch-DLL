"""Ticket 0805: proxy/tools/test_proxy_rt.py's case "d28: abrupt unload" ALONE (the script has no per-case switch; 0784
met the same case failing in whole runs and passing alone, farm-builder-guard.md). Uses the script's own helpers (layout,
OnDesktop on its hidden desktop, slot_of, the loader it built in an earlier whole run under --loader-from) with the case's
set-up: a d28 layout, keep_runs=2, a reload, then boot-abrupt with LOADER_PLANT_DB. Nothing is played; no game.

    python -I proxy/tests/reload_after_game/d28_alone.py --build <build_server out> --scratch <new dir> --loader-from <whole run's scratch> [--runs 3]

Exit 0 when every run passes "d28: abrupt unload: the server is gone, the process lives on" (and its reload set-up).
"""
import argparse
import importlib.util
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
_spec = importlib.util.spec_from_file_location('test_proxy_rt', REPO / 'proxy' / 'tools' / 'test_proxy_rt.py')
T = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(T)


def one(s, build_dir, exe, lua_dir, n):
    proxy = build_dir / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll'
    slot = T.slot_of(build_dir, 'ICvGameContext1::SetDLLIFace(ICvEngineUtility1 *)')
    env = dict(__import__('os').environ)
    env.pop('VPPROXY_INI', None)
    env['VP64_OPTIONS'] = str(s / 'VP64Options.ini')
    desk = T.LOADER_DESKTOPS.setdefault('loader', T.desktops.HiddenDesktop('C5T2502'))

    def drive(scenario, d, label, extra_env=None):
        e = dict(env)
        e.update(extra_env or {})
        p = T.OnDesktop(desk.name, [exe, scenario, d / 'CvGameCore_Expansion2.dll', slot, lua_dir], e,
                        s / ('run%d-%s.txt' % (n, label)), cwd=exe.parent, detached=True)
        rc = p.wait(120)
        return (-1 if rc is None else rc), p.text()

    d = T.layout(s, 'd28-%d' % n, proxy, build_dir, ini='mode=on\nkeep_runs=2\n')
    run_root = d / 'VP64' / 'run'
    run_root.mkdir()
    (run_root / 'keep-me').mkdir()
    rc, out = drive('reload', d, 'reload')
    ok_reload = rc == 0 and 'loader: PASS' in out
    rc, out = drive('boot-abrupt', d, 'abrupt', extra_env={'LOADER_PLANT_DB': str(run_root)})
    ok = rc == 0 and 'loader: PASS' in out
    print('%s run %d: reload %s; d28: abrupt unload: the server is gone, the process lives on: %s'
          % ('ok  ' if ok and ok_reload else 'FAIL', n, 'ok' if ok_reload else 'FAIL', 'ok' if ok else 'FAIL'))
    if not ok:
        print('    ' + out[-600:].replace('\n', '\n    '))
    return ok and ok_reload


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--build', type=Path, required=True)
    ap.add_argument('--scratch', type=Path, required=True)
    ap.add_argument('--loader-from', type=Path, required=True, help='a whole run\'s --scratch (its loader/loader.exe)')
    ap.add_argument('--lua-dir', default=T.DEFAULT_LUA_DIR)
    ap.add_argument('--runs', type=int, default=3)
    a = ap.parse_args()
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    src = a.loader_from.resolve() / 'loader'
    dst = s / 'loader'
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    exe = dst / 'loader.exe'
    passed = 0
    try:
        for n in range(1, a.runs + 1):
            passed += 1 if one(s, a.build.resolve(), exe, a.lua_dir, n) else 0
    finally:
        for desk in T.LOADER_DESKTOPS.values():
            desk.close()
    print('d28_alone: %d/%d runs passed' % (passed, a.runs))
    return 0 if passed == a.runs else 1


if __name__ == '__main__':
    sys.exit(main())
