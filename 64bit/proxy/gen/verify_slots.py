"""Second, independent proof of the vftable slots in proxy/model/interfaces.json (epic 2100, task 2101).

The model's slots come from clang's vtable-layout dump. This script asks a different compiler, Microsoft's own
cl.exe (VS 2022, MSVC 14.4x; x86 through vcvars32.bat and x64 through vcvars64.bat), for the same numbers: for
every virtual it takes a pointer to member (`&ICvGame1::GetGameTurn`, overloads disambiguated by their exact
type), which MSVC compiles as a reference to a "vcall thunk" `??_9Class@@$B<offset>A<cc>`; the mangled name
encodes the byte offset into the vftable. Offset / pointer size = slot. The Microsoft vftable layout for single
inheritance is the one the VC9-built EXE uses (it has not changed since VC6) [C for MSVC 14.4x; I for VC9:
VC9's own cl.exe cannot parse the fork's C++11 headers, see the research note].

It also compares the x64 slots against the table the x64 smoke host measured at run time
(tools/x64-dll/evidence/2026-11-10-smoke/run3/interface-slots.tsv, issue 1007, [L]).
Writes proxy/model/slot-proof.json and prints a summary.
Usage: python -I proxy/gen/verify_slots.py --src <fork source> [--lock F | --unlocked --commit C] [--work DIR]
                                           [--model-dir DIR] [--out-dir DIR]
  --src as extract_model.py (issue 3503; exit 2 when refused; without it the old fixed clone, unchecked);
  --model-dir: where interfaces.json is read (default proxy/model); --out-dir: where slot-proof.json goes (default
  proxy/model).
"""
import argparse
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolchain as tc  # noqa: E402

OUT = os.path.join(tc.BUILD_ROOT, 'slots')
MODEL = os.path.join(tc.PROXY, 'model', 'interfaces.json')
DEST = os.path.join(tc.PROXY, 'model', 'slot-proof.json')
SMOKE = os.path.join(tc.REPO, r'tools\x64-dll\evidence\2026-11-10-smoke\run3\interface-slots.tsv')
VCVARS = {
    'x86': r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat',
    'x64': r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
}
HEADERS = ['CvGameCoreDLLPCH.h', 'ICvDLLDatabaseUtility.h', 'FireWorks/FIFile.h', 'FireWorks/FILogFile.h']


def mangled_offset(code):
    """MS mangling number: a digit d = d+1; else hex digits A-P terminated by '@'."""
    if code[0].isdigit():
        return int(code[0]) + 1, 1
    n = 0
    i = 0
    while code[i] != '@':
        n = n * 16 + (ord(code[i]) - ord('A'))
        i += 1
    return n, i + 1


def build_probe(model):
    lines = ['#include "%s"' % h for h in HEADERS]
    items = []
    for e in model['interfaces']:
        for m in e['methods']:
            if m['name'].startswith('~'):
                continue  # a destructor has no pointer to member; its slot is checked by the clang dump only
            n = len(items)
            ps = ', '.join(p['type'] for p in m['params'])
            if m['variadic']:
                ps = ps + ', ...' if ps else '...'
            mcc = {'stdcall': '__stdcall ', 'cdecl': '__cdecl ', 'thiscall': ''}[m['callconv']]
            cls = m['declared_in']
            # Declared in a class derived from the interface, so nested type names in the signature resolve.
            lines.append('struct H_%d : %s { typedef %s (%s%s::*T)(%s)%s; static T p; }; H_%d::T H_%d::p = &%s::%s;'
                         % (n, cls, m['ret'], mcc, cls, ps, ' const' if m['const'] else '', n, n, cls, m['name']))
            items.append((e['name'], m))
    return '\n'.join(lines) + '\n', items


def compile_listing(arch, src):
    asm = os.path.join(OUT, 'probe_%s.asm' % arch)
    argv = ['cl', '/nologo', '/c', '/EHsc', '/MD', '/std:c++14', '/Zc:wchar_t', '/bigobj', '/w', '/FAs',
            '/D_HAS_AUTO_PTR_ETC=1', '/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS'] + ['/D' + d for d in tc.DEFINES] + [
            '/Fa' + asm, '/Fo' + os.path.join(OUT, 'probe_%s.obj' % arch)]
    argv += ['/I' + os.path.join(tc.FORK, i) for i in tc.INCLUDES] + [src]
    cmd = 'cmd /d /s /c ""%s" >NUL && %s"' % (VCVARS[arch], ' '.join('"%s"' % a for a in argv))
    import subprocess
    # VSCMD_SKIP_SENDTELEMETRY: vcvars otherwise starts a background PowerShell for its telemetry (bug 0108; 0147)
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', cwd=OUT,
                        env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if cp.returncode != 0:
        raise RuntimeError('cl %s failed:\n%s' % (arch, (cp.stdout + cp.stderr)[-4000:]))
    with open(asm, encoding='latin-1') as f:
        return f.read()


def parse_listing(text, arch):
    """g_N -> vftable byte offset, from the data definitions `?g_N@@3...` pointing at `??_9...$B<off>A..`."""
    out = {}
    for m in re.finditer(r'\?p@H_(\d+)@@2[^\s]*\s+D[DQ]\s+(?:FLAT:)?(\?\?_9[^\s]+)', text):
        thunk = m.group(2)
        k = thunk.index('$B')
        off, _ = mangled_offset(thunk[k + 2:])
        out[int(m.group(1))] = off // (4 if arch == 'x86' else 8)
    return out


def configure(argv=None):
    """Parse the options; point OUT, MODEL, DEST and the toolchain at --src. Returns 0 or 2 (refused)."""
    global OUT, MODEL, DEST
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    tc.add_source_args(ap, required=False)
    ap.add_argument('--model-dir', default=os.path.dirname(MODEL))
    ap.add_argument('--out-dir', default=os.path.dirname(DEST))
    a = ap.parse_args(argv)
    if a.src:
        try:
            info = tc.resolve_source(a.src, a.lock, a.unlocked, a.commit)
        except tc.SourceError as ex:
            print('verify_slots: --src refused: %s' % ex)
            return 2
        tc.use_source(info, a.work)
        print('verify_slots: source %s (%s)' % (info['src'], info['note']))
    OUT = os.path.join(a.work, 'slots') if a.work else os.path.join(tc.BUILD_ROOT, 'slots')
    MODEL = os.path.join(os.path.abspath(a.model_dir), 'interfaces.json')
    DEST = os.path.join(os.path.abspath(a.out_dir), 'slot-proof.json')
    return 0


def main(argv=None):
    rc = configure(argv)
    if rc:
        return rc
    t0 = time.time()
    os.makedirs(OUT, exist_ok=True)
    with open(MODEL, encoding='utf-8') as f:
        model = json.load(f)
    src_text, items = build_probe(model)
    src = os.path.join(OUT, 'probe.cpp')
    with open(src, 'w', encoding='utf-8') as f:
        f.write(src_text)
    result = {'generated_by': 'proxy/gen/verify_slots.py', 'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
              'method': 'MSVC 14.4x cl.exe /FAs listing, vcall-thunk offsets of pointers to members',
              'checked': {}, 'mismatches': [], 'smoke_x64': {}}
    for arch in ('x86', 'x64'):
        got = parse_listing(compile_listing(arch, src), arch)
        ok = bad = missing = 0
        for n, (iface, m) in enumerate(items):
            if n not in got:
                missing += 1
                result['mismatches'].append([arch, iface, m['key'], 'no thunk in listing'])
            elif got[n] != m['slot'][arch]:
                bad += 1
                result['mismatches'].append([arch, iface, m['key'], 'clang %d, msvc %d' % (m['slot'][arch], got[n])])
            else:
                ok += 1
        result['checked'][arch] = {'pointers_to_member': len(items), 'agree': ok, 'differ': bad,
                                   'missing': missing}
        print('%s: %d pointers to member, %d agree with the clang dump, %d differ, %d missing' % (
            arch, len(items), ok, bad, missing))
    # runtime-measured x64 slots of the smoke host
    by = {e['name']: e for e in model['interfaces']}
    agree = differ = 0
    with open(SMOKE, encoding='utf-8') as f:
        next(f)
        for line in f:
            iface, _, slot, decl_in, name, decl = line.rstrip('\n').split('\t')[:6]
            e = by.get(iface)
            cands = [m for m in e['methods'] if m['name'] == name and m['declared_in'] == decl_in] if e else []
            if any(m['slot']['x64'] == int(slot) for m in cands):
                agree += 1
            else:
                differ += 1
                result['mismatches'].append(['x64-smoke', iface, name, 'smoke %s' % slot])
    result['smoke_x64'] = {'rows': agree + differ, 'agree': agree, 'differ': differ}
    print('x64 smoke-host table: %d rows, %d agree, %d differ' % (agree + differ, agree, differ))
    with open(DEST, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(result, f, indent=1)
    print('wrote %s in %.1f s' % (DEST, time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
