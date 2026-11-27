"""The proxy build's ONE VC9 STL setting and its check (ticket 0609; research/06-tooling-and-workflows/proxy-build-settings.md).

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q proxy/tests/test_build_settings.py
    python -I proxy/tests/test_build_settings.py [--scratch <dir>] [--build-out <build_proxy.py --out dir>]

1. static   build_proxy.STL_SETTING is _SECURE_SCL=0 and _HAS_ITERATOR_DEBUGGING=0 (the DLL's CvGameCoreDLLPCH.h value);
            stl_setting_args() gives /D for both, -Werror=macro-redefined and a forced include with one #pragma
            detect_mismatch per macro; no x86 source of the proxy, vpipc, the options file, the farm guard or the helper
            #undefs either macro or #defines it to anything but 0 (an #undef is the one way past the compiler's error).
2. injected the 4798 spike's shape, compiled in the proxy's toolchain (clang-cl -m32, VS 2008 environment, /MD): two TUs
            that push_back into a std::vector<std::string>, one compiled with the build's setting, the injected one
            (a) with stl_setting_args but _SECURE_SCL=1 on the command line: lld-link fails with "mismatch detected for
                'vp_stl_SECURE_SCL'" and check_stl_objects fails;
            (b) without stl_setting_args (the spike's own case, the VC9 default 1, no marker): check_stl_objects fails
                (lld-link cannot see it);
            (c) with stl_setting_args and "#define _SECURE_SCL 1" in the file: the compile fails (macro redefined);
            and the two good TUs link and pass. Skipped where the VS 2008 environment or clang-cl is missing.
3. build    with --build-out (or C5_PROXY_BUILD_OUT): every object of that build (obj/proxy, obj/vpipc, obj/helper)
            passes check_stl_objects and build-proxy.json records the setting.
Outputs only under --scratch (default: a temporary folder, removed after a pass).
"""
import argparse
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'proxy'))
import build_proxy as bp  # noqa: E402
tc = bp.tc

# The x86 sources the build compiles from this repository (generated code carries no STL macro of its own).
MODULE_SOURCES = ['proxy/src/*.cpp', 'proxy/src/*.h', 'proxy/include/*.h', 'proxy/helper/*.cpp', 'proxy/helper/*.h',
                  'ipc/src/*.cpp', 'ipc/src/*.h', 'ipc/include/*.h', 'options/src/*.cpp', 'options/include/*.h',
                  'shell/composite/farm_guard.cpp', 'shell/composite/farm_guard.h']
MACRO_LINE = re.compile(r'^\s*#\s*(define|undef)\s+(_SECURE_SCL|_HAS_ITERATOR_DEBUGGING)\b\s*(\S*)', re.M)

GOOD = r'''
#include <string>
#include <vector>
int good_unit(int n) { std::vector<std::string> v; for (int i = 0; i < n; ++i) v.push_back(std::string("x")); return (int)v.size(); }
'''
# 4798's shape: the same std::vector<std::string> member functions instantiated in the injected unit.
INJECTED = r'''
#include <string>
#include <vector>
int injected_unit(int n) { std::vector<std::string> v; for (int i = 0; i < n; ++i) v.push_back(std::string("y")); return (int)v.size(); }
'''
REDEFINE = '#define _SECURE_SCL 1\n' + INJECTED
MAIN = r'''
int good_unit(int); int injected_unit(int);
int main() { return good_unit(3) + injected_unit(2) == 5 ? 0 : 1; }
'''


def module_files():
    return sorted({p for pat in MODULE_SOURCES for p in REPO.glob(pat) if p.is_file()})


def test_static_setting():
    assert bp.STL_SETTING == (('_SECURE_SCL', '0'), ('_HAS_ITERATOR_DEBUGGING', '0'))
    with tempfile.TemporaryDirectory() as d:
        args = bp.stl_setting_args(Path(d))
        assert args[:3] == ['/D_SECURE_SCL=0', '/D_HAS_ITERATOR_DEBUGGING=0', '-Werror=macro-redefined']
        hdr = Path(args[3][3:]).read_text(encoding='ascii')
        assert hdr.count('#pragma detect_mismatch(') == 2 and 'VP_STL_STR(_SECURE_SCL)' in hdr and \
            'VP_STL_STR(_HAS_ITERATOR_DEBUGGING)' in hdr


def test_static_sources_never_change_the_setting():
    files = module_files()
    assert len(files) > 50, 'the source globs found too few files: %d' % len(files)
    bad = []
    for p in files:
        for kind, name, value in MACRO_LINE.findall(p.read_text(encoding='utf-8', errors='replace')):
            if kind == 'undef' or value != '0':
                bad.append('%s: #%s %s %s' % (p.relative_to(REPO).as_posix(), kind, name, value))
    assert not bad, bad


def toolchain_present():
    return Path(tc.ENV_BAT['x86']).is_file() and Path(tc.CLANG).is_file() and Path(tc.LLD_LINK).is_file()


def _cc(d, name, text, extra):
    src = d / (name + '.cpp')
    src.write_text(text, encoding='ascii')
    obj = d / (name + '.obj')
    cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/nologo', '/c', '/MD', '/EHsc', '/O2', '-Wno-everything'] + extra +
                       [str(src), '/Fo' + str(obj)], cwd=str(d))
    return cp.returncode, (cp.stdout + cp.stderr), obj


def _link(d, objs, name):
    exe = d / (name + '.exe')
    cp = tc.run_in_env('x86', [tc.LLD_LINK, '/nologo', '/MACHINE:X86', '/OUT:' + str(exe)] + [str(o) for o in objs] +
                       ['msvcrt.lib', 'msvcprt.lib', 'kernel32.lib'], cwd=str(d))
    return cp.returncode, cp.stdout + cp.stderr


def _check_fails(objs):
    try:
        bp.check_stl_objects(objs, 'injected')
    except bp.BuildError as e:
        return str(e)
    return None


def run_injected(d, say=print):
    """Returns a list of (name, ok, detail)."""
    out = []
    args = bp.stl_setting_args(d)
    one = [a for a in args if not a.startswith('/D_SECURE_SCL=')]
    rc, t, good = _cc(d, 'good', GOOD, args)
    out.append(('good unit compiles with the setting', rc == 0, t.strip()))
    rc, t, main = _cc(d, 'main', MAIN, args)
    out.append(('main unit compiles with the setting', rc == 0, t.strip()))
    rc, t, ok_inj = _cc(d, 'injected_ok', INJECTED, args)
    rc2, t2 = _link(d, [main, good, ok_inj], 'same')
    out.append(('both units at the one setting: lld-link ok', rc == 0 and rc2 == 0, t2.strip()))
    out.append(('both units at the one setting: check_stl_objects ok', _check_fails([main, good, ok_inj]) is None, ''))
    # (a) the build's arguments with another command-line value
    rc, t, inj_a = _cc(d, 'injected_a', INJECTED, ['/D_SECURE_SCL=1'] + one)
    rc2, t2 = _link(d, [main, good, inj_a], 'mixed_a')
    out.append(('(a) injected _SECURE_SCL=1 with the marker: lld-link fails on the mismatch',
                rc == 0 and rc2 != 0 and "mismatch detected for 'vp_stl_SECURE_SCL'" in t2, t2.strip()))
    why = _check_fails([main, good, inj_a])
    out.append(('(a) injected _SECURE_SCL=1 with the marker: check_stl_objects fails', why is not None, why))
    # (b) the spike's case: no setting at all (VC9 default _SECURE_SCL 1), so no marker
    rc, t, inj_b = _cc(d, 'injected_b', INJECTED, [])
    rc2, t2 = _link(d, [main, good, inj_b], 'mixed_b')
    why = _check_fails([main, good, inj_b])
    out.append(('(b) injected unit without the setting: check_stl_objects fails (lld-link rc %d cannot see it)' % rc2,
                rc == 0 and why is not None and 'missing' in why, why))
    # (c) a file that defines another value itself
    rc, t, _ = _cc(d, 'injected_c', REDEFINE, args)
    out.append(('(c) "#define _SECURE_SCL 1" in the file: the compile fails (macro redefined)',
                rc != 0 and 'macro redefined' in t, t.strip()[-400:]))
    for name, ok, detail in out:
        say(('ok   ' if ok else 'FAIL ') + name + ('\n       ' + detail.replace('\n', '\n       ') if detail else ''))
    return out


def test_injected_mixed_unit(tmp_path):
    if not toolchain_present():
        import pytest
        pytest.skip('no VS 2008 environment or clang-cl/lld-link here')
    res = run_injected(tmp_path)
    assert all(ok for _, ok, _ in res), [n for n, ok, _ in res if not ok]


def check_build(out, say=print):
    objs = sorted((out / 'obj' / 'proxy').glob('*.obj')) + sorted((out / 'obj' / 'vpipc').glob('*.obj'))
    hobjs = sorted((out / 'obj' / 'helper').glob('*.obj'))
    facts = bp.check_stl_objects(objs, bp.DLL_NAME)
    hfacts = bp.check_stl_objects(hobjs, bp.HELPER_NAME)
    import json
    summ = json.loads((out / 'build-proxy.json').read_text(encoding='utf-8'))
    say('build %s: proxy %d objects, helper %d objects, all %s; build-proxy.json stl %s, helper stl %s' % (
        out, facts['objects'], hfacts['objects'], facts['setting'], summ.get('stl'), (summ.get('helper') or {}).get('stl')))
    return summ.get('stl', {}).get('objects') == len(objs) and (summ.get('helper') or {}).get('stl', {}).get('objects') == len(hobjs)


def test_build_objects():
    out = os.environ.get('C5_PROXY_BUILD_OUT')
    if not out:
        import pytest
        pytest.skip('C5_PROXY_BUILD_OUT not set')
    assert check_build(Path(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scratch', type=Path)
    ap.add_argument('--build-out', type=Path, default=os.environ.get('C5_PROXY_BUILD_OUT'))
    a = ap.parse_args()
    fails = 0
    for f in (test_static_setting, test_static_sources_never_change_the_setting):
        try:
            f()
            print('ok   ' + f.__name__)
        except AssertionError as e:
            print('FAIL %s: %s' % (f.__name__, e))
            fails += 1
    print('static: %d module source files scanned' % len(module_files()))
    if toolchain_present():
        scratch = a.scratch or Path(tempfile.mkdtemp(prefix='vp0609-'))
        scratch.mkdir(parents=True, exist_ok=True)
        res = run_injected(scratch)
        fails += sum(1 for _, ok, _ in res if not ok)
        if not fails and not a.scratch:
            shutil.rmtree(scratch, ignore_errors=True)
    else:
        print('skip injected: no VS 2008 environment or clang-cl/lld-link')
    if a.build_out:
        try:
            fails += 0 if check_build(Path(a.build_out)) else 1
        except bp.BuildError as e:
            print('FAIL build: %s' % e)
            fails += 1
    print('test_build_settings: %s' % ('PASS' if not fails else 'FAIL (%d)' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
