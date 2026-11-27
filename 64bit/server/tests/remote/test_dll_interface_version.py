"""Ticket 4719 (plan 4.5 of the upstream export): hermetic tests of server/src/remote/srv_dll_interface.h, the interface
version between the DLL and the server (design: research/03-dll-engine-boundary/upstream-export-e4-dll-commit.md 8).

    python -I server/tests/remote/test_dll_interface_version.py --out DIR [--dll-src FORK] [--asan] [-v]

The C++ sources are written into DIR by this script (the unit and the stand-in DLL), so the test has no other file.
Built with clang-cl x64 /W3 /WX as the server's own code (server/tests/remote/test_exe_crash_report.py's build):
  stand-ins  three DLLs: v1 (exports DllGetOutOfProcessInterfaceVersion = 1), v2 (= 2), none (no such export, only
             DllGetGameContext as today's pins)
  unit       srv_dll_interface.h compiled twice: "unversioned" (no CvOutOfProcessInterface.h on the include path: today's
             pins) and "versioned" (a fake DLL include folder whose header says 1); each loads every stand-in and runs
             SrvDllInterface::Check, and prints the Decide table
Cases (each checks the verdict, ok, the text with both numbers, and the unit's exit code):
  match      v1 accepted by both builds: "the DLL's is 1, as this server expects"
  mismatch   v2 refused by both builds: "interface version is 2, this server expects 1"
  missing    none: refused by the versioned build ("has no DllGetOutOfProcessInterfaceVersion export, this server
             expects interface version 1"), accepted LOUD by the unversioned one
  null       Check(NULL) as a DLL without the export (no crash)
  compile    (negative) a fake DLL header that says 2: the compile fails on the static_assert, whose diagnostic names
             both numbers ("2 == 1"); a header that says 1 compiles (the positive arm is the versioned unit)
With --dll-src FORK (a checkout of the fork's x64-ifver or export-e4-dll-r3): the unit compiled against that source's
real CvGameCoreDLL_Expansion2/CvOutOfProcessInterface.h (versioned, match).
--asan: the unit once more as an MSVC /fsanitize=address build (x64; both include variants) with a canary that must be
caught; every case again; no AddressSanitizer memory-error report.
--e2e B [--pinned-dll DLL] (B: a build_server.py --out tree built against a versioned DLL source, x64-ifver): the real
server.exe --remote, booted by 0418's stand-in EXE (server/tests/remote/exe_crash_standin.cpp, "incall"), with server.cfg's
dll = B's own DLL (match: kRt_Boot ok 1), the v2 stand-in (mismatch: ok 0, "FAILED: kRt_Boot: interface version mismatch
... is 2, this server expects 1"), the none stand-in and DLL (missing: ok 0, refused); on a refusal nothing is installed
into the DLL (no dll-fatal handler line).
Every program runs on a hidden desktop (COMMON rule 14). Exit 0 when every check passes. Outputs only under --out.
Start it through perf/quietlock/quietlock.py run.
"""
import argparse
import importlib.util
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
_spec = importlib.util.spec_from_file_location('test_exe_crash_report', HERE / 'test_exe_crash_report.py')
ecr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ecr)  # 0418's builds, hidden desktop and checks
rt = ecr.rt
check = rt.check
REMOTE = REPO / 'server' / 'src' / 'remote'
EXPORT = 'DllGetOutOfProcessInterfaceVersion'

STANDIN = r'''// stand-in for the x64 DLL (ticket 4719): STANDIN_VERSION < 0 -> no interface-version export
#include <windows.h>
extern "C" __declspec(dllexport) void* DllGetGameContext() { return 0; }
#if STANDIN_VERSION >= 0
extern "C" __declspec(dllexport) unsigned int DllGetOutOfProcessInterfaceVersion() { return STANDIN_VERSION; }
#endif
BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID) { return TRUE; }
'''

UNIT = r'''// unit of srv_dll_interface.h (ticket 4719): iface_unit <dll>... -> one RESULT line per DLL, then the Decide table
#include "srv_dll_interface.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char** argv)
{
	printf("SOURCE_VERSIONED %d EXPECTED %u\n", SRV_DLL_SOURCE_VERSIONED, SRV_DLL_INTERFACE_VERSION_EXPECTED);
	int refused = 0;
	for(int i = 1; i < argc; ++i)
	{
		HMODULE h = strcmp(argv[i], "NULL") == 0 ? NULL : LoadLibraryA(argv[i]);
		if(!h && strcmp(argv[i], "NULL") != 0)
		{
			printf("LOADFAIL %s %lu\n", argv[i], GetLastError());
			return 3;
		}
		const SrvDllInterface::Result r = SrvDllInterface::Check(h);
		printf("RESULT %s verdict=%s ok=%d export=%d dll=%u expected=%u text=%s\n", argv[i],
		       SrvDllInterface::VerdictName(r.verdict), r.ok ? 1 : 0, r.hasExport ? 1 : 0, r.dllVersion, r.expected,
		       r.text.c_str());
		if(!r.ok)
			++refused;
	}
	const unsigned vers[] = {1u, 2u, 0u};
	for(int sv = 0; sv < 2; ++sv)
		for(int e = 0; e < 2; ++e)
			for(int k = 0; k < 3; ++k)
			{
				if(!e && k)
					continue;
				const SrvDllInterface::Result r = SrvDllInterface::Decide(e != 0, vers[k], 1u, sv != 0);
				printf("DECIDE source=%d export=%d dll=%u -> %s ok=%d\n", sv, e, vers[k], SrvDllInterface::VerdictName(r.verdict),
				       r.ok ? 1 : 0);
			}
	printf("DONE refused=%d\n", refused);
	return refused;
}
'''


def fake_header(d, version):
    d.mkdir(parents=True, exist_ok=True)
    (d / 'CvOutOfProcessInterface.h').write_text('#pragma once\n#define VP_OUT_OF_PROCESS_INTERFACE_VERSION %d\n' % version,
                                                 encoding='ascii')
    return d


def results(out):
    r = {}
    for m in re.finditer(r'^RESULT (\S+) verdict=(\S+) ok=(\d) export=(\d) dll=(\d+) expected=(\d+) text=(.*)$', out, re.M):
        r[Path(m.group(1)).stem if m.group(1) != 'NULL' else 'NULL'] = {
            'verdict': m.group(2), 'ok': m.group(3) == '1', 'export': m.group(4) == '1', 'dll': int(m.group(5)),
            'expected': int(m.group(6)), 'text': m.group(7).strip()}
    return r


def decide_table(out):
    return {(int(m.group(1)), int(m.group(2)), int(m.group(3))): (m.group(4), m.group(5) == '1')
            for m in re.finditer(r'^DECIDE source=(\d) export=(\d) dll=(\d+) -> (\S+) ok=(\d)', out, re.M)}


WANT_DECIDE = {
    (0, 1, 1): ('match', True), (0, 1, 2): ('mismatch', False), (0, 1, 0): ('mismatch', False),
    (0, 0, 1): ('missing-accepted', True),
    (1, 1, 1): ('match', True), (1, 1, 2): ('mismatch', False), (1, 1, 0): ('mismatch', False),
    (1, 0, 1): ('missing-refused', False),
}


def unit_cases(s, unit, standins, versioned, tag, env=None):
    args = [standins['v1'], standins['v2'], standins['none'], 'NULL']
    rc, out = ecr.run_on_desk(unit, args, s / ('run-%s.txt' % tag), env or ecr.safe_env(), cwd=s)
    rc = rc & 0xFFFFFFFF if rc is not None else None
    r = results(out)
    want_refused = 3 if versioned else 1
    check('%s: the unit ran to the end, SOURCE_VERSIONED %d, exit = number refused (%d)' % (tag, versioned, want_refused),
          rc == want_refused and ('SOURCE_VERSIONED %d EXPECTED 1' % versioned) in out and
          ('DONE refused=%d' % want_refused) in out, '%s %s' % (rc, out[-1500:]))
    v1, v2, none, nul = r.get('dll_iface_v1'), r.get('dll_iface_v2'), r.get('dll_iface_none'), r.get('NULL')
    check('%s match: v1 accepted, text "the DLL\'s is 1, as this server expects"' % tag,
          v1 is not None and v1['verdict'] == 'match' and v1['ok'] and v1['export'] and v1['dll'] == 1 and
          v1['text'] == "interface version: the DLL's is 1, as this server expects", str(v1))
    check('%s mismatch: v2 refused with both numbers' % tag,
          v2 is not None and v2['verdict'] == 'mismatch' and not v2['ok'] and v2['dll'] == 2 and
          v2['text'].startswith("interface version mismatch: the DLL's interface version is 2, this server expects 1: "),
          str(v2))
    if versioned:
        check('%s missing: a DLL without the export refused, naming the export and the expected number' % tag,
              none is not None and none['verdict'] == 'missing-refused' and not none['ok'] and not none['export'] and
              none['text'].startswith('interface version missing: the DLL has no %s export, this server expects interface '
                                      'version 1' % EXPORT), str(none))
    else:
        check('%s missing: a DLL without the export accepted with a LOUD line' % tag,
              none is not None and none['verdict'] == 'missing-accepted' and none['ok'] and not none['export'] and
              none['text'].startswith('interface version: LOUD: the DLL has no %s export; accepted because this server '
                                      'was built against a DLL source without one' % EXPORT), str(none))
    check('%s null: Check(NULL) is a DLL without the export (no crash)' % tag,
          nul is not None and nul['verdict'] == ('missing-refused' if versioned else 'missing-accepted'), str(nul))
    check('%s: the Decide table, every combination' % tag, decide_table(out) == WANT_DECIDE, str(decide_table(out)))
    return out


def build_unit(s, sub, extra_inc, env_cl=None, asan=False):
    d = s / sub
    d.mkdir(parents=True, exist_ok=True)
    src = s / 'src' / 'iface_unit.cpp'
    exe = d / 'iface_unit.exe'
    inc = ['/I' + str(REMOTE)] + ['/I' + str(p) for p in extra_inc]
    if not asan:
        cp = rt.tc.run_in_env('x64', [rt.tc.CLANG, '-m64', '/MD', '/O2', '/Zi', '/EHsc', '/std:c++14', '/W3', '/WX',
                                      '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '-fuse-ld=lld'] +
                              inc + [str(src), '/Fe' + str(exe)], cwd=str(d))
        return cp.returncode == 0 and exe.exists(), cp.stdout + cp.stderr, exe
    cl = shutil.which('cl', path=env_cl.get('PATH', env_cl.get('Path', '')))
    cp = subprocess.run([cl, '/nologo', '/EHsc', '/W3', '/WX', '/Zi', '/Oy-', '/fsanitize=address', '/std:c++14',
                         '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'] + inc +
                        [str(src), '/Fe' + str(exe), '/Fo' + str(d) + '\\', '/Fd' + str(d / 'iface_unit.pdb')],
                        cwd=str(d), env=env_cl, capture_output=True, text=True, errors='replace')
    return cp.returncode == 0 and exe.exists(), cp.stdout + cp.stderr, exe


def test_compile_negative(s):
    inc2 = fake_header(s / 'inc-v2', 2)
    ok, txt, _ = build_unit(s, 'neg-v2', [inc2])
    check('compile (negative): against a DLL header that says 2 the build FAILS on the static_assert, whose diagnostic '
          'names both numbers (2 == 1)',
          not ok and 'interface version mismatch: the DLL source' in txt and re.search(r"'2[uU]? == 1[uU]?'", txt) is not None,
          txt[-1500:])
    ok1, txt1, _ = build_unit(s, 'pos-v1', [fake_header(s / 'inc-v1', 1)])
    check('compile: against a DLL header that says 1 the build succeeds', ok1, txt1[-1500:])


def test_asan(s, standins):
    env = rt.vcvars_env('x64')
    if not check('asan: vcvarsall x64', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan-canary'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    cp = subprocess.run([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', str(d / 'canary.cpp'), '/Fe' + str(d / 'canary.exe'),
                         '/Fo' + str(d) + '\\'], cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
    if not check('asan: canary compiled', cp.returncode == 0, (cp.stdout + cp.stderr)[-1500:]):
        return
    rc, out = ecr.run_on_desk(d / 'canary.exe', [], d / 'canary.txt', env)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          rc != 0 and 'heap-buffer-overflow' in out, out[-600:])
    outs = []
    for sub, inc, versioned in (('asan-unversioned', [], 0), ('asan-versioned', [s / 'inc-v1'], 1)):
        ok, txt, exe = build_unit(s, sub, inc, env_cl=env, asan=True)
        if check('asan: %s unit compiled with MSVC /fsanitize=address (x64, /W3 /WX)' % sub, ok, txt[-1500:]):
            outs.append(unit_cases(s, exe, standins, versioned, sub, env=ecr.safe_env(None, env)))
    kinds = r'AddressSanitizer: (heap|stack|global|dynamic|container|use-after|double-free|attempting free|memcpy)'
    check('asan: no AddressSanitizer memory-error report in the unit runs', not any(re.search(kinds, o) for o in outs),
          ' | '.join(o[-400:] for o in outs if re.search(kinds, o)))


def e2e(s, b, standins, pinned_dll):
    """The real server.exe --remote (built by build_server.py against a VERSIONED DLL source) booted by 0418's stand-in
    EXE (exe_crash_standin.exe "incall": kRt_Boot, then SetDLLIFace/Init; it ends itself with exit 0 at the first
    engine call or after Init), with each DLL as server.cfg's dll."""
    exe, txt = ecr.build_standin(b)
    if not check('e2e: build: 0418\'s stand-in EXE (exe_crash_standin.cpp) against %s' % b.name, exe is not None,
                 txt[-1500:]):
        return
    fake = s / 'fake' / 'vp64helper.exe'
    ok, txt = ecr.build_clang(fake, [ecr.FAKE_HELPER])
    if not check('e2e: build: the fake vp64helper.exe', ok, txt[-1500:]):
        return
    bins = b / 'bin'
    root = s / 'e2e'
    cases = [('match', bins / 'CvGameCore_Expansion2.dll', 1,
              "boot: interface version: the DLL's is 1, as this server expects"),
             ('mismatch', standins['v2'], 0,
              "FAILED: kRt_Boot: interface version mismatch: the DLL's interface version is 2, this server expects 1"),
             ('missing-standin', standins['none'], 0,
              'FAILED: kRt_Boot: interface version missing: the DLL has no %s export, this server expects interface '
              'version 1' % EXPORT)]
    if pinned_dll:
        cases.append(('missing-pinned', pinned_dll, 0,
                      'FAILED: kRt_Boot: interface version missing: the DLL has no %s export' % EXPORT))
    for name, dll, want_ok, want_line in cases:
        c = ecr.Case(root, name, exe, fake)
        for f in ('server.exe', 'lua51_x64.dll', 'CvGameDatabase_x64.dll', 'CvLocalization_x64.dll'):
            shutil.copy2(bins / f, c.proxy / 'VP64' / f)
        (c.run / 'server.log').unlink()
        c.cfg.update({'dll': str(dll), 'lua_dll': str(c.proxy / 'VP64' / 'lua51_x64.dll'),
                      'db_dll': str(c.proxy / 'VP64' / 'CvGameDatabase_x64.dll'),
                      'loc_dll': str(c.proxy / 'VP64' / 'CvLocalization_x64.dll'), 'loc_db': '', 'dll_logs': '0',
                      'fail_on_unexpected': '0', 'connect_timeout_ms': '30000'})
        cfg = c.write_cfg()
        go = c.d / 'go'
        go.write_text('go\n', encoding='ascii')
        p = rt.OnDesktop(ecr.desk().name, [str(c.exe), str(c.proxy / 'VP64' / 'server.exe'), str(cfg), '0', str(go),
                                           'incall'], ecr.safe_env(), c.d / 'standin.txt', cwd=c.d)
        src = p.wait(90)
        out = p.text()
        rep = ecr.text(c.run / 'server-report.txt')
        slog = ecr.text(c.run / 'server.log')
        m = re.search(r'kRt_Boot ok (\d+)', out)
        okv = int(m.group(1)) if m else None
        nothing_installed = 'dll-fatal: vectored handler installed' not in slog
        check('e2e %s: kRt_Boot answers ok %d; server-report.txt / server.log: "%s"; %s' %
              (name, want_ok, want_line[:70], 'the DLL got its handlers' if want_ok else 'nothing installed into the DLL'),
              okv == want_ok and want_line in (rep + '\n' + slog) and (nothing_installed != bool(want_ok)),
              'stand-in exit %s ok %s\n%s\n--- report\n%s\n--- log\n%s' % (src, okv, out[-600:], rep[:1200], slog[-1200:]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--e2e', type=Path, help='a build_server.py --out tree built against a versioned DLL source')
    ap.add_argument('--pinned-dll', type=Path, help='with --e2e: a DLL without the export (the pinned x64-batch4 DLL)')
    ap.add_argument('--dll-src', type=Path, help='a fork checkout with CvGameCoreDLL_Expansion2/CvOutOfProcessInterface.h')
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    shutil.rmtree(s, ignore_errors=True)
    (s / 'src').mkdir(parents=True)
    (s / 'src' / 'standin.cpp').write_text(STANDIN, encoding='ascii')
    (s / 'src' / 'iface_unit.cpp').write_text(UNIT, encoding='ascii')
    try:
        standins = {}
        for name, ver in (('v1', 1), ('v2', 2), ('none', -1)):
            dll = s / 'standin' / ('dll_iface_%s.dll' % name)
            ok, txt = ecr.build_clang(dll, [s / 'src' / 'standin.cpp'], defines=['/LD', '/DSTANDIN_VERSION=%d' % ver])
            if check('build: stand-in dll_iface_%s.dll (clang-cl x64 /W3 /WX)' % name, ok, txt[-1500:]):
                standins[name] = dll
        if len(standins) == 3:
            ok, txt, exe = build_unit(s, 'clang-unversioned', [])
            if check('build: unit, unversioned (no CvOutOfProcessInterface.h on the include path)', ok, txt[-1500:]):
                unit_cases(s, exe, standins, 0, 'clang-unversioned')
            ok, txt, exe = build_unit(s, 'clang-versioned', [fake_header(s / 'inc-v1', 1)])
            if check('build: unit, versioned (a fake DLL header that says 1)', ok, txt[-1500:]):
                unit_cases(s, exe, standins, 1, 'clang-versioned')
            test_compile_negative(s)
            if a.dll_src:
                real = a.dll_src.resolve() / 'CvGameCoreDLL_Expansion2'
                if check('dll-src: %s has CvOutOfProcessInterface.h' % real.name,
                         (real / 'CvOutOfProcessInterface.h').exists()):
                    ok, txt, exe = build_unit(s, 'clang-real-src', [real])
                    if check('build: unit against the fork\'s real header (versioned, its number = the expected one)',
                             ok, txt[-1500:]):
                        unit_cases(s, exe, standins, 1, 'clang-real-src')
            if a.asan:
                test_asan(s, standins)
            if a.e2e:
                e2e(s, a.e2e.resolve(), standins, a.pinned_dll.resolve() if a.pinned_dll else None)
    finally:
        for dk in ecr.DESKS.values():
            dk.close()
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('test_dll_interface_version: %d/%d checks passed' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
