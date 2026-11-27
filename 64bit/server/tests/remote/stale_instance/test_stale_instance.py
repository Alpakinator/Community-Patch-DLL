"""Ticket 0576 (D185): hermetic tests of the stale-instance guard of the server's Lua bridge
(server/src/remote/srv_lua_stale.cpp; research/03-dll-engine-boundary/lua-stale-instance.md).

    python -I server/tests/remote/stale_instance/test_stale_instance.py --out DIR --stubs STUBS --src SRC
                                                                         [--asan] [--bench N] [-v]

STUBS: a tools/x64-dll/build_all.py --out folder's stubs/ (lua51_x64.lib and .dll); SRC: the DLL fork source at the
pin (its ThirdPartyLibs/Lua51/include). The unit: stale_unit.cpp + srv_lua_stale.cpp, clang-cl x64 /W3 /WX /O2 as the
server's own code, linked with lua51_x64. Its stand-ins have the DLL's shapes (CvLuaScopedInstance at the pin); a unit
or city lives alone in a VirtualAlloc region, so "freed" is unmapped, as the CvUnit of 0573's crash e711. Cases:
  freed-unguarded  (negative: without the guard) the freed unit's GetPlot faults: the process ends with 0xC0000005
  off-env          VP64_LUA_STALE_GUARD=0: the guard is off, the same fault (the switch works, the guard is what saves it)
  freed            guarded: nil, one LOUD line, "readable 0"; the live city beside it still answers
  deleted          out of its owner's list, memory intact: stale, readable, owner 3 id 8195, live 0
  garbage          the memory reused by something else: stale, owner out of range
  reused           a new unit at the same address with a new id: the kept table is that unit (as in stock): answered
  city-freed, city-deleted   the same for a city; the live unit still answers
  live             unit, city and unit:Distance(otherUnit) answered, nothing stale
  others           numbers, strings, nil, plain tables, a table with __instance but no metatable, a Plot instance and
                   a type table: never checked (0 checks); a freed unit in argument 2: stale, argument 2
  nolearn          no Player type: mask 0, nothing checked
  wrapped          the functions as closures of upstream master's wrapper shape: learnt through it, answered
  stack            CheckArgs leaves the stack as it was
  bench            the cost per call (rounds of N calls, default 1,000,000): unit:GetPlot() unguarded and guarded, a call
                   without instance arguments unguarded and guarded; printed, and the guarded extra must be < 2 us
--asan: the unit once more as an MSVC /fsanitize=address build (x64) with a canary that must be caught; every case but
bench runs there; the two unguarded negatives must end in ASan's access-violation report in UnitGetPlot, and no other
run may have an AddressSanitizer report (the guard's caught fault included).
Every program runs on a hidden desktop (COMMON rule 14). Exit 0 when every check passes. Outputs only under --out.
Start it through perf/quietlock/quietlock.py run.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / 'proxy' / 'tools'))
import test_proxy_rt as rt  # noqa: E402  (toolchain, hidden desktops, vcvarsall, check)

SRCS = [HERE / 'stale_unit.cpp', REPO / 'server' / 'src' / 'remote' / 'srv_lua_stale.cpp']
check = rt.check
DESKS = {}
AV = 0xC0000005


def desk():
    return DESKS.setdefault('d', rt.desktops.HiddenDesktop('C5T0576'))


def env_for(extra=None, base=None):
    e = {k: v for k, v in (base or os.environ).items() if k.upper() != 'VP64_LUA_STALE_GUARD'}
    e.update(extra or {})
    return e


def build_clang(out, stubs, src):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cp = rt.tc.run_in_env('x64', [rt.tc.CLANG, '-m64', '/MD', '/O2', '/Zi', '/EHsc', '/std:c++14', '/W3', '/WX',
                                  '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '-fuse-ld=lld',
                                  '/I' + str(REPO / 'server' / 'src'),
                                  '/I' + str(src / 'ThirdPartyLibs' / 'Lua51' / 'include')] +
                          [str(s) for s in SRCS] + ['/Fe' + str(out), str(stubs / 'lua51_x64.lib')],
                          cwd=str(out.parent))
    if cp.returncode == 0 and out.exists():
        shutil.copy2(stubs / 'lua51_x64.dll', out.parent / 'lua51_x64.dll')
    return cp.returncode == 0 and out.exists(), cp.stdout + cp.stderr


def run(exe, case, tag, env, extra=()):
    out = Path(exe).parent / ('%s-%s.txt' % (tag, case))
    p = rt.OnDesktop(desk().name, [str(exe), case] + list(extra), env, out, cwd=Path(exe).parent)
    p.wait(300)
    rc = p.code & 0xFFFFFFFF if p.code is not None else None
    return rc, rt.text(out)


def res(out, key):
    m = re.search(r'^RESULT %s (.*)$' % re.escape(key), out, re.M)
    return m.group(1).strip() if m else None


def loud(out):
    return len(re.findall(r'^LOUD: stale ', out, re.M))


def faulted(rc, out, asan, where='UnitGetPlot'):
    """The unguarded fault: our filter's 0xC0000005, or under ASan its own access-violation report in `where`."""
    if asan:
        return rc not in (0, None) and 'AddressSanitizer: access-violation' in out and where in out
    return rc == AV and 'UNHANDLED 0xC0000005' in out


def cases(exe, tag, env_base=None, bench_n=1000000, asan=False):
    E = env_for(base=env_base)
    t = tag + ': '

    rc, out = run(exe, 'freed-unguarded', tag, E)
    check(t + 'freed-unguarded (negative): GetPlot on the freed unit without the guard ends the process with an access '
          'violation (0xC0000005%s)' % ('; under ASan its report' if asan else ''),
          faulted(rc, out, asan) and 'RESULT unit' not in out, 'rc=%s %s' % (rc, out[-400:]))

    out_name = Path(exe).parent / ('%s-freed-unguarded-transport.txt' % tag)
    p = rt.OnDesktop(desk().name, [str(exe), 'freed-unguarded', 'transport'], E, out_name, cwd=Path(exe).parent)
    p.wait(300)
    rc, out = (p.code & 0xFFFFFFFF if p.code is not None else None), rt.text(out_name)
    check(t + 'freed-unguarded transport (negative; 0573\'s crash 9a2f): GetTransportUnit on the freed unit without the '
          'guard faults the same way', faulted(rc, out, asan, 'UnitGetTransportUnit') and 'RESULT transport' not in out,
          'rc=%s %s' % (rc, out[-400:]))

    rc, out = run(exe, 'off-env', tag, env_for({'VP64_LUA_STALE_GUARD': '0'}, env_base))
    check(t + 'off-env (negative): VP64_LUA_STALE_GUARD=0 turns the guard off: enabled 0, the same access violation',
          faulted(rc, out, asan) and res(out, 'enabled') == '0' and 'RESULT unit' not in out,
          'rc=%s %s' % (rc, out[-400:]))

    rc, out = run(exe, 'freed', tag, E)
    check(t + 'freed: exit 0, DONE', rc == 0 and 'DONE' in out, 'rc=%s %s' % (rc, out[-400:]))
    check(t + 'freed: mask 3 (Unit and City covered), enabled 1', res(out, 'mask') == '3' and res(out, 'enabled') == '1',
          out[:300])
    check(t + 'freed: the freed unit gives nil, its identity unreadable', res(out, 'unit') ==
          'nil(stale Unit readable=0 owner=-1 id=-1)', str(res(out, 'unit')))
    check(t + 'freed: GetTransportUnit (0573\'s crash 9a2f) on the same freed unit gives nil too',
          res(out, 'transport') == 'nil(stale Unit readable=0 owner=-1 id=-1)', str(res(out, 'transport')))
    check(t + 'freed: the live city still answers GetPlot (40*1000+41) and GetPopulation (41)',
          res(out, 'city') == '40041' and res(out, 'citypop') == '41', '%s %s' % (res(out, 'city'), res(out, 'citypop')))
    check(t + 'freed: two LOUD lines, counts checked 2/2 stale 2/0 faulted 2/0',
          loud(out) == 2 and res(out, 'counts') == 'checked 2/2 stale 2/0 faulted 2/0 loud 2', str(res(out, 'counts')))
    check(t + 'freed: the stack ends where it began', res(out, 'final top') == '0', str(res(out, 'final top')))

    rc, out = run(exe, 'deleted', tag, E)
    check(t + 'deleted: out of the list with its memory intact: stale, readable, owner 3 id 8195',
          rc == 0 and res(out, 'unit') == 'nil(stale Unit readable=1 owner=3 id=8195)' and loud(out) == 1 and
          'live 0000000000000000' in out, 'rc=%s %s' % (rc, out[-500:]))

    rc, out = run(exe, 'garbage', tag, E)
    check(t + 'garbage: memory reused by other data: stale, readable, owner out of range',
          rc == 0 and (res(out, 'unit') or '').startswith('nil(stale Unit readable=1 owner=-1515870811') and loud(out) == 1,
          'rc=%s %s' % (rc, out[-500:]))

    rc, out = run(exe, 'reused', tag, E)
    check(t + 'reused: a new unit at the same address answers through the kept table (7*1000+8), nothing stale',
          rc == 0 and res(out, 'same address') == '1' and res(out, 'unit') == '7008' and loud(out) == 0,
          'rc=%s %s' % (rc, out[-500:]))

    for c in ('city-freed', 'city-deleted'):
        rc, out = run(exe, c, tag, E)
        want = 'nil(stale City readable=%s' % ('0' if c == 'city-freed' else '1 owner=5 id=16390')
        check(t + '%s: the city gives nil to GetPlot and GetPopulation (%s), the live unit answers GetPlot (12034) and '
              'GetTransportUnit (nil: none), two LOUD lines' % (c, want),
              rc == 0 and (res(out, 'city') or '').startswith(want) and (res(out, 'citypop') or '').startswith(want) and
              res(out, 'unit') == '12034' and res(out, 'transport') == 'nil' and loud(out) == 2,
              'rc=%s %s' % (rc, out[-500:]))

    rc, out = run(exe, 'live', tag, E)
    check(t + 'live: unit 12034, city 40041, unit:Distance(otherUnit) 7, nothing stale, 4 checks',
          rc == 0 and res(out, 'unit') == '12034' and res(out, 'city') == '40041' and res(out, 'distance') == '7' and
          loud(out) == 0 and res(out, 'counts') == 'checked 3/1 stale 0/0 faulted 0/0 loud 0',
          'rc=%s %s' % (rc, out[-500:]))

    rc, out = run(exe, 'others', tag, E)
    check(t + 'others: values that are not Unit or City instances are not checked (0 checks), the call answers 42',
          rc == 0 and res(out, 'others') == '42' and res(out, 'checked') == '0 0', 'rc=%s %s' % (rc, out[-500:]))
    check(t + 'others: a freed unit in argument 2 of a live unit\'s method: stale, argument 2, nil',
          (res(out, 'arg2') or '').startswith('nil(stale Unit readable=0') and 'in argument 2' in out and loud(out) == 1,
          out[-500:])

    rc, out = run(exe, 'nolearn', tag, E)
    check(t + 'nolearn (negative): no Player type: mask 0, nothing checked, the live unit answers',
          rc == 0 and res(out, 'mask') == '0' and res(out, 'unit') == '12034' and
          res(out, 'counts') == 'checked 0/0 stale 0/0 faulted 0/0 loud 0', 'rc=%s %s' % (rc, out[-500:]))

    rc, out = run(exe, 'wrapped', tag, E)
    check(t + 'wrapped: functions as wrapper closures (upstream master): mask 3, unit 12034, city 40041, distance 7',
          rc == 0 and res(out, 'mask') == '3' and res(out, 'unit') == '12034' and res(out, 'city') == '40041' and
          res(out, 'distance') == '7' and loud(out) == 0, 'rc=%s %s' % (rc, out[-500:]))

    rc, out = run(exe, 'stack', tag, E)
    check(t + 'stack: CheckArgs finds the freed city in argument 2 and leaves the stack as it was',
          rc == 0 and res(out, 'stale') == '1 kind City arg 2 readable 0' and res(out, 'top unchanged') == '1' and
          res(out, 'below') == 'below', 'rc=%s %s' % (rc, out[-500:]))

    if bench_n and not asan:  # under ASan the times are the sanitizer's, not the guard's
        rc, out = run(exe, 'bench', tag, E, [str(bench_n)])
        rows = re.findall(r'^RESULT bench round \d: unit unguarded ([\d.]+) ns, guarded ([\d.]+) ns \(\+([-\d.]+)\); '
                          r'no instance unguarded ([\d.]+) ns, guarded ([\d.]+) ns \(\+([-\d.]+)\)$', out, re.M)
        for ln in re.findall(r'^RESULT (?:bench|guard).*$', out, re.M):
            print('     ' + ln)
        extra = [float(r[2]) for r in rows]
        check(t + 'bench: 3 rounds, the guard adds < 2000 ns per unit-method call (median %s ns)' %
              (sorted(extra)[1] if len(extra) == 3 else '?'), rc == 0 and len(rows) == 3 and sorted(extra)[1] < 2000,
              'rc=%s %s' % (rc, out[-600:]))
    return True


def test_asan(s, stubs, src, bench_n):
    env = rt.vcvars_env('x64')
    if not check('asan: vcvarsall x64', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan-build'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/O2', '/MD', '/fsanitize=address', '/DWIN32_LEAN_AND_MEAN',
             '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(REPO / 'server' / 'src'),
             '/I' + str(src / 'ThirdPartyLibs' / 'Lua51' / 'include')]
    for exe, srcs, fl, libs in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                ('stale_unit.exe', SRCS, flags, [str(stubs / 'lua51_x64.lib')])):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))] + libs,
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address (x64)' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    shutil.copy2(stubs / 'lua51_x64.dll', d / 'lua51_x64.dll')
    rc, out = run(d / 'canary.exe', 'canary', 'asan', env)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          rc != 0 and 'heap-buffer-overflow' in out, out[-600:])
    first = len(rt.RESULTS)
    cases(d / 'stale_unit.exe', 'asan', env_base=env, asan=True)
    # the two unguarded negatives fault on purpose (ASan reports that fault); every other run must have no report
    expected = {'asan-canary.txt', 'asan-freed-unguarded.txt', 'asan-freed-unguarded-transport.txt', 'asan-off-env.txt'}
    reports = [p for p in d.glob('asan-*.txt') if 'AddressSanitizer' in rt.text(p) and p.name not in expected]
    check('asan: no AddressSanitizer report in any guarded run of the ASan build (%d checks above)'
          % (len(rt.RESULTS) - first), not reports, ', '.join(str(p) for p in reports[:5]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--stubs', type=Path, required=True, help='a build_all.py --out folder\'s stubs/ (lua51_x64.lib/.dll)')
    ap.add_argument('--src', type=Path, required=True, help='the DLL fork source (ThirdPartyLibs/Lua51/include)')
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--bench', type=int, default=1000000)
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    shutil.rmtree(s, ignore_errors=True)
    s.mkdir(parents=True)
    stubs, src = a.stubs.resolve(), a.src.resolve()
    try:
        if check('inputs: %s has lua51_x64.lib and .dll, %s has the Lua headers' % (stubs, src),
                 (stubs / 'lua51_x64.lib').exists() and (stubs / 'lua51_x64.dll').exists() and
                 (src / 'ThirdPartyLibs' / 'Lua51' / 'include' / 'lua.h').exists()):
            unit = s / 'clang' / 'stale_unit.exe'
            ok, txt = build_clang(unit, stubs, src)
            if check('build: stale_unit.cpp + srv_lua_stale.cpp (clang-cl x64, /W3 /WX /O2)', ok, txt[-2000:]):
                cases(unit, 'clang', bench_n=a.bench)
                if a.asan:
                    test_asan(s, stubs, src, a.bench)
    finally:
        for dk in DESKS.values():
            dk.close()
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('test_stale_instance: %d/%d checks passed' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
