"""Ticket 0744 (K31): hermetic tests of the end at the DLL's first deserialization error of a load
(server/src/remote/srv_remote_savemods.cpp), the save-mod-mismatch record after a render end (a), and the proxy's rule
for a server-fatal.txt of this server's pid after an exception death (b, proxy/src/proxy_report.h UseFatalRecord).

    python -I -X utf8 server/tests/remote/test_savemods.py --out DIR [--asan] [-v]

The unit: server/tests/remote/savemods_unit.cpp + srv_remote_savemods.cpp + srv_exe_crash.cpp (clang-cl x64, /W3 /WX as
the server's own code, test_exe_crash_report.py's build). Each case runs with its own run folder and C5_DOCUMENTS
naming an empty folder under DIR (the active mods cannot be read there; nothing reaches the real Documents folder),
on a hidden desktop (COMMON rule 14). Cases:
  end            (c) a load, the DLL's game data mismatch, its first "Deserialization Error": the process ends with exit
                 6 from inside that log call (RETURNED never printed); server-fatal.txt: exit 6, this pid, code
                 save-mod-mismatch, the player's words, the reason names both hashes, "after the game data mismatch",
                 "deserialization errors 1" and that the server ended at the DLL's first deserialization error;
                 server.log has the LOUD end line
  end-nohash     (c) the same without the mismatch line: exit 6, code save-mod-mismatch, the reason says there was no
                 game data hash mismatch before it
  direct         direct mode (no end hook): the load reads on (RETURNED), one deserialization line logged for two; (a) a
                 later render end (exit 5, ice) gets code save-mod-mismatch and "what ended the server (code render-ice)"
  direct-nohash  (a) the same with no mismatch line, a units end: save-mod-mismatch, code render-units named
  render-nosentinel (negative) the mismatch alone, then a render end: the record keeps render-ice (0681's rule)
  noload         (negative) a deserialization line before any load began: no end, one line says so
  same           (negative, D239) a load with no mismatch and no deserialization error: no end, the K29 exit 6 keeps
                 engine-exception
  second         a hook that returns: called once for two deserialization lines
  fit            (b) UseFatalRecord: exit codes equal -> used (1); a running server -> used; an exception death
                 (0xC0000005, 0xE06D7363, 0x80000003) with the record's pid = the server's -> used by pid (2); no pid,
                 another pid, an unknown server pid, 0xC000013A, exit 1 or 6 -> not used (0) with the reason
--asan: the cases once more with an MSVC /fsanitize=address build (x64) after a canary is caught; no ASan report.
Exit 0 when every check passes. Outputs only under --out. Start it through perf/quietlock/quietlock.py run.
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
_spec.loader.exec_module(ecr)  # 0418's build, hidden desktop and checks
rt = ecr.rt
check = rt.check

SRCS = [HERE / 'savemods_unit.cpp', REPO / 'server' / 'src' / 'remote' / 'srv_remote_savemods.cpp',
        REPO / 'server' / 'src' / 'remote' / 'srv_exe_crash.cpp']
SUMMARY = ('This save was made with a different set of mods (or mod versions) than the mods active now, so the game '
           'could not load it; this is not a failure of the 64-bit patch, and the same save fails in Vox Populi without '
           'the patch too. Load the save with the mods it was made with.')
SAVE_HASH = '0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49'
GAME_HASH = '0x4510836f-0x6b3aac37-0x0fa52c26-0x20ec3e4f'


def record(run):
    p = run / 'server-fatal.txt'
    if not p.exists():
        return None
    out = {}
    for ln in p.read_bytes().decode('utf-8', errors='replace').split('\r\n'):
        k, sep, v = ln.partition(': ')
        if sep:
            out.setdefault(k, v)
    return out


def _run_case(s, unit, name, tag, env_base):
    d = s / tag / 'cases' / name
    run, docs = d / 'run', d / 'docs'
    run.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)
    rc, out = ecr.run_on_desk(unit, ['case', name, run], d / 'out.txt', ecr.safe_env({'C5_DOCUMENTS': str(docs)}, env_base),
                              cwd=d, timeout=60)
    rc = rc & 0xFFFFFFFF if rc is not None else None
    m = re.search(r'(?m)^PID (\d+)\r?$', out)
    slog = ecr.text(run / 'server.log') if (run / 'server.log').exists() else ''
    return rc, out, record(run), (m.group(1) if m else None), slog


def cases(s, unit, tag='clang', env_base=None):
    def run_case(s, unit, name):
        return _run_case(s, unit, name, tag, env_base)

    def chk(name, ok, detail=''):
        return check('%s %s' % (tag, name), ok, detail)
    # (c) the end at the first deserialization error
    rc, out, rec, pid, slog = run_case(s, unit, 'end')
    chk('end (c): the process ends with exit 6 from inside the DLL\'s log call (the hook ran once, RETURNED never)',
          rc == 6 and out.count('END 6') == 1 and 'RETURNED' not in out, '%s %s' % (rc, out[-1500:]))
    chk('end (c): server-fatal.txt: exit 6, this pid, code save-mod-mismatch, the player\'s words',
          rec is not None and rec.get('exit') == '6' and pid is not None and rec.get('pid') == pid and
          rec.get('code') == 'save-mod-mismatch' and rec.get('summary') == SUMMARY, '%s pid %s' % (rec, pid))
    r = (rec or {}).get('reason', '')
    chk('end (c): the reason names both hashes, the mismatch before the error, one error and the deliberate end',
          r.startswith('save-mod-mismatch: the game\'s DLL reports that the save\'s game data hash [%s] differs from the '
                       'running game\'s [%s]' % (SAVE_HASH, GAME_HASH)) and
          '; deserialization errors 1; the server ended at the DLL\'s first deserialization error (ticket 0744): '
          'save-mod-mismatch: the game\'s DLL reports a deserialization error in the save\'s read (' in r and
          ', after the game data mismatch): its read is out of step with the save, so the server ends before the misread '
          'can damage its memory (ticket 0744, K31)' in r and 'body version "Release-5.4.8-unit"' in r, r)
    chk('end (c): server.log has the mismatch, the deserialization line and the LOUD end line, then the result line',
          'save mods: LOUD: the game\'s DLL reports that the save\'s game data hash' in slog and
          'the DLL reports a deserialization error (a sentinel of the save did not match) after the game data mismatch' in slog
          and 'save mods: LOUD: the server ends at the DLL\'s first deserialization error of the load (exit 6, code '
          'save-mod-mismatch; ticket 0744)' in slog and 'server-fatal.txt written (exit 6, code save-mod-mismatch)' in slog
          and 'result: FAILED: save-mod-mismatch: ' in slog, slog[-2500:])

    rc, out, rec, pid, slog = run_case(s, unit, 'end-nohash')
    r = (rec or {}).get('reason', '')
    chk('end-nohash (c): exit 6, code save-mod-mismatch, the player\'s words; the reason says no game data hash mismatch '
          'came before the error',
          rc == 6 and 'RETURNED' not in out and rec is not None and rec.get('exit') == '6' and rec.get('pid') == pid and
          rec.get('code') == 'save-mod-mismatch' and rec.get('summary') == SUMMARY and
          r.startswith('save-mod-mismatch: the game\'s DLL reports a deserialization error in the save\'s read (a sentinel '
                       'of the save did not match) without a game data hash mismatch before it') and
          'the active mods could not be read' in r and 'after the game data mismatch' not in r and
          '; the server ended at the DLL\'s first deserialization error (ticket 0744): ' in r,
          '%s %s %s' % (rc, rec, out[-800:]))

    # (a) render* after a deserialization error, direct mode (no end hook)
    rc, out, rec, pid, slog = run_case(s, unit, 'direct')
    r = (rec or {}).get('reason', '')
    chk('direct: no end hook (direct mode): the load reads on, one deserialization line for two, exit 0',
          rc == 0 and 'RETURNED' in out and 'END ' not in out and
          slog.count('the DLL reports a deserialization error') == 1 and 'the server ends at the DLL' not in slog,
          '%s %s' % (rc, out[-1200:]))
    chk('direct (a): a later render end (exit 5, ice) is recorded as save-mod-mismatch with the render code as what '
          'ended the server',
          'WROTE 1' in out and rec is not None and rec.get('exit') == '5' and rec.get('code') == 'save-mod-mismatch' and
          rec.get('summary') == SUMMARY and '; deserialization errors 2; what ended the server (code render-ice): ice: the '
          'render pass faulted' in r, '%s %s' % (rec, out[-800:]))
    rc, out, rec, pid, slog = run_case(s, unit, 'direct-nohash')
    r = (rec or {}).get('reason', '')
    chk('direct-nohash (a): no mismatch line, two deserialization lines, a units end: save-mod-mismatch, code '
          'render-units named',
          rc == 0 and 'RETURNED' in out and rec is not None and rec.get('code') == 'save-mod-mismatch' and
          'without a game data hash mismatch before it' in r and
          '; what ended the server (code render-units): units: the unit readiness was lost' in r, '%s %s' % (rec, out[-800:]))

    # negatives
    rc, out, rec, pid, slog = run_case(s, unit, 'render-nosentinel')
    chk('render-nosentinel (negative): the mismatch alone, then a render end: no end, the record keeps code render-ice '
          '(0681\'s rule)',
          rc == 0 and 'RETURNED' in out and 'END ' not in out and rec is not None and rec.get('code') == 'render-ice' and
          rec.get('reason') == 'ice: the render pass faulted', '%s %s' % (rec, out[-800:]))
    rc, out, rec, pid, slog = run_case(s, unit, 'noload')
    chk('noload (negative): a deserialization line before any load: no end, no record, one line says so',
          rc == 0 and 'RETURNED' in out and 'END ' not in out and rec is None and
          'that deserialization error came before any save\'s read began: the server does not end on it (ticket 0744)'
          in slog, '%s %s' % (rec, slog[-800:]))
    rc, out, rec, pid, slog = run_case(s, unit, 'same')
    chk('same (negative, D239): a load with no mismatch and no deserialization error: no end, no save-mods line but the '
          'load\'s, the K29 exit 6 keeps engine-exception',
          rc == 0 and 'RETURNED' in out and 'END ' not in out and rec is not None and rec.get('code') == 'engine-exception'
          and rec.get('reason', '').startswith('exe_exception: ') and 'LOUD' not in slog and
          slog.count('save mods:') == 1 and 'save mods: load of \'(no file name)\'' in slog, '%s %s' % (rec, slog[-800:]))
    rc, out, rec, pid, slog = run_case(s, unit, 'second')
    chk('second: a hook that returns is called once for two deserialization lines',
          rc == 0 and out.count('HOOK 6') == 1 and 'RETURNED hooks=1' in out, out[-800:])

    # (b) the proxy's rule
    d = s / tag / 'fit'
    d.mkdir(parents=True, exist_ok=True)
    rc, out = ecr.run_on_desk(unit, ['fit'], d / 'out.txt', ecr.safe_env({'C5_DOCUMENTS': str(d)}, env_base), cwd=d,
                              timeout=60)
    got = dict((m.group(1), (int(m.group(2)), m.group(3))) for m in re.finditer(r'(?m)^FIT (\S+) (\d) (.*?)\r?$', out))
    want = {
        'match': (1, '-'), 'match-nopid': (1, '-'), 'running': (1, '-'),
        'av-pid': (2, '-'), 'cxx-pid': (2, '-'), 'bp-pid': (2, '-'),
        'av-nopid': (0, 'the record names no pid'), 'av-otherpid': (0, 'the record is of another server process'),
        'av-pid-unknown': (0, 'the server\'s pid is unknown'),
        'ctrlc-pid': (0, 'the server\'s exit code is not an exception code'),
        'exit1-pid': (0, 'the server\'s exit code is not an exception code'),
        'exit6-pid': (0, 'the server\'s exit code is not an exception code'),
    }
    for k, v in want.items():
        chk('fit (b): %s -> %d (%s)' % (k, v[0], v[1]), rc == 0 and got.get(k) == v, '%s: %s' % (k, got.get(k)))


def test_asan(s):
    """The unit once more as an MSVC /fsanitize=address build (x64) after a canary is caught; no ASan report."""
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
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
             '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(REPO / 'server' / 'src')]
    for exe, srcs, fl, libs in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                ('savemods_unit.exe', SRCS, flags, ecr.LIBS)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))] + libs,
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address (x64)' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    rc, out = ecr.run_on_desk(d / 'canary.exe', [], d / 'canary.txt', env)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          rc != 0 and 'heap-buffer-overflow' in out, out[-600:])
    first = len(rt.RESULTS)
    cases(s, d / 'savemods_unit.exe', 'asan', env)
    reports = [q for q in (s / 'asan').rglob('*.txt') if 'AddressSanitizer' in ecr.text(q)]
    check('asan: no AddressSanitizer report in any run of the ASan build (%d checks above)' % (len(rt.RESULTS) - first),
          not reports, ', '.join(str(q) for q in reports[:5]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true', help='also the MSVC /fsanitize=address build of the unit')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    shutil.rmtree(s, ignore_errors=True)
    s.mkdir(parents=True)
    try:
        unit = s / 'clang' / 'savemods_unit.exe'
        ok, txt = ecr.build_clang(unit, SRCS)
        if check('build: savemods_unit.cpp + srv_remote_savemods.cpp + srv_exe_crash.cpp (clang-cl x64, /W3 /WX)', ok,
                 txt[-2500:]):
            cases(s, unit)
        if a.asan:
            test_asan(s)
    finally:
        for dk in ecr.DESKS.values():
            dk.close()
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('test_savemods: %d/%d checks passed' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
