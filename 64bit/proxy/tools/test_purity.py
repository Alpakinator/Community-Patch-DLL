"""Tests of the purity file (issue 3503): the proxy's loader, the derivation, the audited lists' check.

    python -I proxy/tools/test_purity.py --src <DLL source at the pin> --dll <x64 DLL> --scratch <dir> [--no-asan] [-v]

1. unit     proxy/tools/rt_tests/purity_unit.cpp + proxy/src/proxy_purity.cpp with the proxy's toolchain (clang-cl -m32,
            VS 2008 environment, /MT): good files, every refusal of the loader (arguments, header, records, fields,
            shapes, duplicates, control bytes, NUL, sizes, counts, another DLL), Clear, the interim VP_PROXY_PURITY load.
2. asan     the same under MSVC /fsanitize=address (x86, memory cap 2 GB) with a canary that must be caught (COMMON 9).
3. derive   proxy/tools/derive_purity.py on --src: purity.tsv and the report carry exactly the rows the committed lists
            name (read from proxy/model/*.tsv by this test, no literal counts; issue 0478), all kept, the file bound
            to --dll's SHA-256; negative control: the comparison fails against a list copy with a row removed; on a
            scratch copy of the DLL sources (CvGameCoreDLL_Expansion2/, CvGameCoreDLLUtil/) with a seeded change: (a) a cited line changed drops its rows, and only rows citing it or calling the function;
            (b) a callee's body changed (a function that no row cites, only calls) drops exactly the rows calling it;
            (c) a macro changed drops its callers; (d) comments only and (e) lines moved drop nothing.
4. refuse   derive_purity.py exit 2 and nothing written for: no --src, --src not a DLL tree, --dll missing, --dll not a PE
            file, a bad --commit, the baseline missing, not JSON, of another format, a malformed citation, a TSV row
            changed since the baseline, a TSV row without a baseline entry, a baseline entry the TSVs lack, a TSV with an
            unknown verdict, a key twice, a wrong field count; --make-baseline without --fork.
5. check    gen_pure_list.py --check: exit 0 on the repository; exit 1 for proxy_pure.inc present, a kLuaPure array in
            proxy/src, a key not in the model, a TSV edited without --make-baseline.
6. regen    proxy/gen/regen_model.py's verdict (3502, 2026-11-14): the same tree under another commit and a longer source
            path (AST and preprocessed sizes) are EQUAL, a model difference is DIFFERENT. `--only regen` runs this
            section alone (no --src, --dll).
Exit 0 only when every check passed (none is skipped: --no-asan fails the run). Outputs only under --scratch.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'proxy' / 'gen'))
sys.path.insert(0, str(HERE))
import toolchain as tc  # noqa: E402
import derive_purity as dp  # noqa: E402

SRC = REPO / 'proxy' / 'src'
RT = HERE / 'rt_tests'
DERIVE = HERE / 'derive_purity.py'
GEN = HERE / 'gen_pure_list.py'
VCVARSALL = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat'
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + str(detail)[-1500:] + ']') if (detail and (VERBOSE or not ok))
                                                 else ''), flush=True)
    return ok


def py(argv, timeout=600):
    cp = subprocess.run([sys.executable, '-I'] + [str(a) for a in argv], capture_output=True, text=True,
                        errors='replace', timeout=timeout)
    return cp.returncode, cp.stdout + cp.stderr


# ---------------------------------------------------------------------------------------------- 1, 2 native
def unit_check(name, rc, out):
    m = re.search(r'purity_unit: (\d+)/(\d+) checks passed', out)
    for ln in out.splitlines():
        if ln.startswith('FAIL') or VERBOSE:
            print('    ' + ln)
    return check('%s: %s' % (name, m.group(0) if m else 'no summary (exit %d)' % rc),
                 rc == 0 and m and m.group(1) == m.group(2) and int(m.group(2)) >= 58, out[-1500:])


def test_unit(s):
    # The proxy's STL layout (proxy_rt_logic.h): _SECURE_SCL and _HAS_ITERATOR_DEBUGGING off before the first #include,
    # else std::vector<std::string> differs between TUs and the built proxy crashed (b2, 2026-11-13).
    text = (SRC / 'proxy_purity.cpp').read_text(encoding='utf-8')
    first_inc = text.find('#include')
    check('unit: proxy_purity.cpp sets the proxy STL layout before its first #include',
          0 <= text.find('#define _SECURE_SCL 0') < first_inc and 0 <= text.find('#define _HAS_ITERATOR_DEBUGGING 0') < first_inc)
    d = s / 'unit'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'purity_unit.exe'
    cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld', '/I' + str(SRC),
                               str(RT / 'purity_unit.cpp'), str(SRC / 'proxy_purity.cpp'), '/Fe' + str(exe)], cwd=str(d))
    if not check('unit: built with the proxy toolchain (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 cp.stdout[-2000:] + cp.stderr[-2000:]):
        return
    cp = subprocess.run([str(exe), str(d / 'files')], capture_output=True, text=True, errors='replace', timeout=120)
    unit_check('unit', cp.returncode, cp.stdout + cp.stderr)


def vcvars_env(arch):
    out = subprocess.run('call "%s" %s >nul && set' % (VCVARSALL, arch), shell=True, capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if out.returncode:
        return None
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def test_asan(s):
    env = vcvars_env('x86')
    if not check('asan: vcvarsall x86', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)'
                                  'malloc(16); p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n',
                                  encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(SRC)]
    for exe, srcs in (('canary.exe', [d / 'canary.cpp']), ('purity_unit.exe', [RT / 'purity_unit.cpp',
                                                                               SRC / 'proxy_purity.cpp'])):
        cp = subprocess.run([cl] + flags + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                     '/Fd' + str(d / (exe + '.pdb'))],
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = subprocess.run(memcap + [str(d / 'canary.exe')], cwd=str(d), env=env, capture_output=True, text=True,
                        errors='replace', timeout=120)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          cp.returncode != 0 and 'heap-buffer-overflow' in cp.stdout + cp.stderr, (cp.stdout + cp.stderr)[-800:])
    cp = subprocess.run(memcap + [str(d / 'purity_unit.exe'), str(d / 'files')], cwd=str(d), env=env,
                        capture_output=True, text=True, errors='replace', timeout=600)
    out = cp.stdout + cp.stderr
    ok = unit_check('asan: unit under /fsanitize=address', cp.returncode, out)
    check('asan: no AddressSanitizer report', ok and 'AddressSanitizer' not in out, out[-1500:])


# ---------------------------------------------------------------------------------------------- 3 derive
def read_purity(p):
    lines = Path(p).read_text(encoding='utf-8').split('\n')
    return lines, {tuple(l.split('\t', 1)) for l in lines if l.startswith(('method\t', 'lua\t'))}


def report_rows(p):
    rows = [l.split('\t') for l in Path(p).read_text(encoding='utf-8').splitlines()[1:]]
    return {(r[0], r[1]): (r[2], r[3]) for r in rows}


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        h.update(f.read())
    return h.hexdigest()


def derive(src, dll, out, extra=()):
    if out.exists():
        shutil.rmtree(out)
    return py([DERIVE, '--src', src, '--dll', dll, '--out', out] + list(extra))


def _writable_and_retry(fn, path, _exc):
    # a read-only file left by an earlier run (copied with copy2 from the read-only shared pins folder) is made
    # writable, then removed (ticket 0631; the rule of research/06-tooling-and-workflows/release-builds.md 6.6)
    os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
    fn(path)


def copy_sources(src, dst):
    if dst.exists():
        shutil.rmtree(dst, onerror=_writable_and_retry)
    # copyfile, not copy2: the copies must not take the read-only attribute of a shared or kept source folder
    # (C:/c5-64bit-work/keep/pins-0.1b is read-only; a second case's rmtree failed with WinError 5, ticket 0631)
    for top in ('CvGameCoreDLL_Expansion2', 'CvGameCoreDLLUtil'):
        shutil.copytree(src / top, dst / top, ignore=shutil.ignore_patterns('*.lib', '*.dll', '*.pdb', '*.obj'),
                        copy_function=shutil.copyfile)


def edit(path, fn):
    raw = path.read_bytes()
    crlf = b'\r\n' in raw
    text = raw.decode('latin-1').replace('\r\n', '\n')
    new = fn(text)
    assert new != text, 'the seeded edit changed nothing in %s' % path
    if crlf:
        new = new.replace('\n', '\r\n')
    path.write_bytes(new.encode('latin-1'))


# The rows the purity file carries, read here from the committed lists themselves and not from derive_purity.py (issue
# 0478: literal counts 114/143 went stale when 4452/4460/0439 added rows, and the derive checks failed on main from
# 2026-11-16 on). The rule is tools/release/README.md section 4's ("What it lists"): a method row whose verdict is pure
# or neutral (kRt_ keys never, D30), a Lua row whose verdict is pure.
LISTED = (('pure-methods.tsv', 'method', 'key', ('pure', 'neutral')),
          ('lua-pure-bindings.tsv', 'lua', 'binding', ('pure',)))


def listed_rows(model_dir):
    """{(kind, key)} of the rows the lists in model_dir put into purity.tsv."""
    out = set()
    for fname, kind, key_col, keep in LISTED:
        rows = (Path(model_dir) / fname).read_text(encoding='utf-8').replace('\r\n', '\n').split('\n')
        head = rows[0].split('\t')
        ki, vi = head.index(key_col), head.index('verdict')
        for r in rows[1:]:
            f = r.split('\t')
            if len(f) > max(ki, vi) and f[vi].strip() in keep and not (kind == 'method' and f[ki].startswith('kRt_')):
                out.add((kind, f[ki].strip()))
    return out


def counts(rows):
    return '%d method, %d lua' % (sum(k[0] == 'method' for k in rows), sum(k[0] == 'lua' for k in rows))


def disagreement(got, want):
    """'' when the two row sets are equal, else what differs (for the check's detail)."""
    if got == want:
        return ''
    miss, extra = sorted(want - got), sorted(got - want)
    return 'derived %s, the lists %s; missing %d %s; extra %d %s' % (counts(got), counts(want), len(miss), miss[:3],
                                                                     len(extra), extra[:3])


def test_derive(s, src, dll):
    model = REPO / 'proxy' / 'model'
    doc = json.loads((model / 'purity-baseline.json').read_text(encoding='utf-8'))
    entries = {(e['kind'], e['key']): e for e in doc['entries']}
    listed = listed_rows(model)
    check('derive: the lists name rows (%s, proxy/model/pure-methods.tsv and lua-pure-bindings.tsv)' % counts(listed),
          any(k[0] == 'method' for k in listed) and any(k[0] == 'lua' for k in listed))
    check('derive: the baseline has one entry per listed row', set(entries) == listed,
          disagreement(set(entries), listed))
    out = s / 'derive-pin'
    rc, log = derive(src, dll, out, ['--commit', 'pin-test'])
    if not check('derive: exit 0 on --src', rc == 0, log):
        return
    lines, keys = read_purity(out / 'purity.tsv')
    check('derive: header, binding, derived_from', lines[0] == '# vp64-purity 1' and
          lines[1] == 'dll_sha256\t' + sha256(dll) and lines[2] == 'derived_from\t%s\tpin-test' % doc['audited_at'],
          '\n'.join(lines[:3]))
    check('derive: every listed row kept at the pin (%s)' % counts(listed), keys == listed, disagreement(keys, listed))
    rep = report_rows(out / 'report.tsv')
    check('derive: report has one row per listed row, all kept', set(rep) == listed and
          all(v[0] == 'kept' for v in rep.values()), disagreement(set(rep), listed))
    check('derive: no kRt_ key in the file (D30)', not any(k[1].startswith('kRt_') for k in keys))

    # negative control (0478): the same comparison against a copy of the lists with one kept row removed must fail
    neg = s / 'model-row-removed'
    if neg.exists():
        shutil.rmtree(neg)
    shutil.copytree(model, neg)
    p = neg / 'pure-methods.tsv'
    rows = p.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n')
    i = next(i for i, r in enumerate(rows[1:], 1) if ('method', r.split('\t')[0].strip()) in listed)
    removed = rows.pop(i).split('\t')[0]
    p.write_text('\n'.join(rows), encoding='utf-8', newline='\n')
    d = disagreement(keys, listed_rows(neg))
    check('derive (negative): a copy of pure-methods.tsv without %s: the comparison fails' % removed,
          d != '' and removed in d, d or 'the comparison passed')

    # scratch copy with seeded changes; the definitions index tells where a callee is defined
    cp_dir = s / 'seeded-src'
    copy_sources(Path(src), cp_dir)
    tree = dp.DirTree(str(cp_dir))
    where = {}
    dp.index_definitions(tree, where)
    callers = {}
    for k, e in entries.items():
        for n in e['callees']:
            callers.setdefault(n, set()).add(k)
    cited = {c['path'] for e in entries.values() for c in e['citations']}

    def run_case(name, setup, expect_dropped, why_part, at_least=False):
        copy_sources(Path(src), cp_dir)
        setup()
        o = s / ('derive-' + name)
        rc, log = derive(cp_dir, dll, o)
        if not check('derive %s: exit 0' % name, rc == 0, log):
            return
        _l, ks = read_purity(o / 'purity.tsv')
        rp = report_rows(o / 'report.tsv')
        dropped = {k for k, v in rp.items() if v[0] == 'dropped'}
        check('derive %s: dropped %s %d expected row(s)' % (name, 'at least' if at_least else 'exactly',
                                                            len(expect_dropped)),
              (dropped >= set(expect_dropped) and dropped) if at_least else dropped == set(expect_dropped),
              'dropped %s, expected %s' % (sorted(dropped), sorted(expect_dropped)))
        check('derive %s: dropped rows absent from purity.tsv, the rest present' % name,
              all(k not in ks for k in dropped) and ks == listed - dropped, disagreement(ks, listed - dropped))
        parts = why_part if isinstance(why_part, tuple) else (why_part,)
        check('derive %s: the report names the reason (%s)' % (name, ' or '.join(parts)),
              all(any(w in rp[k][1] for w in parts) for k in dropped) if dropped else True,
              '; '.join('%s: %s' % (k[1], rp[k][1]) for k in sorted(dropped))[:600])

    # (a) a cited line changed: ICvPlayer1::IsAlive() const cites CvPlayer.h (the inline isAlive); found in the copy
    # the way the derivation finds it (it may sit at other lines than at the audit)
    e = entries[('method', 'ICvPlayer1::IsAlive() const')]
    cit = next(c for c in e['citations'] if c['path'].endswith('CvPlayer.h'))
    lines = dp.Lines(tree)
    ls = lines.get(cit['path'])
    at = next((i for i in lines.first_index(cit['path']).get(cit['first_sha1'], [])
               if dp.sha1('\n'.join(ls[i:i + cit['nlines']])) == cit['text_sha1']), None)
    if not check('derive a: the cited isAlive lines found in the copy', at is not None):
        return

    def seed_cited():
        def f(t):
            tl = t.split('\n')
            i = at + max(0, cit['nlines'] - 2)   # a line inside the cited range (before its closing line)
            tl[i] = tl[i] + ' int iSeeded3503 = 0; (void)iSeeded3503;'
            return '\n'.join(tl)
        edit(cp_dir / cit['path'], f)
    run_case('a-cited-line', seed_cited, {('method', 'ICvPlayer1::IsAlive() const')} | callers.get('isAlive', set()),
             ('CvPlayer.h', 'callee isAlive'), at_least=True)

    # (b) a callee's body changed: a function name some rows call, defined once, in a file no row cites. A .cpp
    # definition first; else a header's (ticket 0765: at the Vox Populi 5.5 pin a2627c8ac no such callee is left in a
    # .cpp: the one used before, luaL_optbool of Lua/CvLuaSupport.cpp, moved into the header Lua/!CvLuaApi.h with
    # upstream #13489). Only the scratch copy's text is edited, so a header serves the same.
    pick = None
    for exts in (('.cpp',), ('.h', '.inl')):
        for n in sorted(callers, key=lambda n: (len(callers[n]), n)):
            locs = where.get(n, [])
            if len(locs) == 1 and locs[0][0] not in cited and locs[0][0].endswith(exts) and len(callers[n]) <= 3 and \
                    n[0].isalpha() and not n.isupper():
                pick = n
                break
        if pick:
            break
    if not check('derive b: a callee defined once outside the cited files found', pick is not None):
        return
    rel, off = where[pick][0]

    def seed_callee():
        p = cp_dir / rel
        def f(t):
            st = dp.strip_code(t)   # offsets of the index are of the stripped text, which keeps every offset
            assert st[off] == '{'
            return t[:off + 1] + '\n\tint iSeeded3503 = 1; (void)iSeeded3503;' + t[off + 1:]
        edit(p, f)
    run_case('b-callee-body(%s)' % pick, seed_callee, callers[pick], 'callee %s changed' % pick)

    # (c) a macro changed: GET_PLAYER (a #define) - every row calling it drops
    macro = 'GET_PLAYER' if 'GET_PLAYER' in callers and len(where.get('GET_PLAYER', [])) == 1 else None
    if check('derive c: GET_PLAYER is a callee with one #define', macro is not None):
        mrel, moff = where[macro][0]

        def seed_macro():
            def f(t):
                nl = t.find('\n', moff)
                assert not t[:nl].rstrip().endswith('\\'), 'GET_PLAYER is a multi-line macro here'
                return t[:nl] + ' /* the comment does not count */ + 0' + t[nl:]
            edit(cp_dir / mrel, f)
        run_case('c-macro', seed_macro, callers[macro], 'callee GET_PLAYER')

    # (d) comments only inside a cited range: nothing dropped
    def seed_comment():
        def f(t):
            ls = t.split('\n')
            i = cit['line'] - 1
            ls[i] = ls[i] + ' // a comment added by test_purity.py (issue 3503)'
            return '\n'.join(ls)
        edit(cp_dir / cit['path'], f)
    run_case('d-comment-only', seed_comment, set(), '-')

    # (e) lines moved: 5 blank lines and a comment line at the top of a cited file
    def seed_move():
        edit(cp_dir / cit['path'], lambda t: '// moved by test_purity.py\n\n\n\n\n' + t)
    run_case('e-moved', seed_move, set(), '-')


# ---------------------------------------------------------------------------------------------- 4 refusals
def test_refusals(s, src, dll):
    model = REPO / 'proxy' / 'model'

    def refused(name, argv, expect, model_dir=None):
        out = s / 'refused-out'
        if out.exists():
            shutil.rmtree(out)
        args = [DERIVE] + argv + ['--out', out] + (['--model-dir', model_dir] if model_dir else [])
        rc, log = py(args)
        check('refuse: %s: exit 2, nothing written' % name, rc == 2 and not (out / 'purity.tsv').exists() and
              not (out / 'report.tsv').exists() and expect in log, log[-600:])

    base = ['--src', src, '--dll', dll]
    refused('no --src', ['--dll', dll], '--src is required')
    refused('--src not a DLL tree', ['--src', s, '--dll', dll], 'not a DLL source tree')
    refused('--dll missing', ['--src', src, '--dll', s / 'none.dll'], 'no such file')
    notpe = s / 'notpe.dll'
    notpe.write_bytes(b'XX' + b'\0' * 4096)
    refused('--dll not a PE file', ['--src', src, '--dll', notpe], 'not a PE file')
    refused('a bad --commit', base + ['--commit', 'a b'], '--commit')
    refused('baseline missing', base + ['--baseline', s / 'none.json'], 'no such file')
    bad = s / 'bad.json'
    bad.write_text('{not json', encoding='utf-8')
    refused('baseline not JSON', base + ['--baseline', bad], 'not JSON')
    bad.write_text(json.dumps({'format': 2, 'entries': [], 'definitions': {}}), encoding='utf-8')
    refused('baseline of another format', base + ['--baseline', bad], 'not a format-1 baseline')
    doc = json.loads((model / 'purity-baseline.json').read_text(encoding='utf-8'))
    d2 = json.loads(json.dumps(doc))
    d2['entries'][0]['citations'][0]['text_sha1'] = 'xyz'
    bad.write_text(json.dumps(d2), encoding='utf-8')
    refused('baseline with a malformed citation', base + ['--baseline', bad], 'malformed entry')
    d2 = json.loads(json.dumps(doc))
    d2['entries'] = d2['entries'] + [dict(d2['entries'][0], key='ICvGame1::Extra()')]
    bad.write_text(json.dumps(d2), encoding='utf-8')
    refused('baseline entry the TSVs lack', base + ['--baseline', bad], 'no longer lists')

    def model_copy(name, fn):
        d = s / ('model-' + name)
        if d.exists():
            shutil.rmtree(d)
        shutil.copytree(model, d)
        fn(d)
        return d

    def tsv_edit(fname, fn):
        def f(d):
            p = d / fname
            text = p.read_text(encoding='utf-8').replace('\r\n', '\n')
            p.write_text(fn(text), encoding='utf-8', newline='\n')
        return f

    def change_note(t):
        ls = t.split('\n')
        i = next(i for i, l in enumerate(ls) if l.split('\t')[1:2] == ['pure'])
        ls[i] = ls[i] + ' (edited)'
        return '\n'.join(ls)
    refused('a TSV row changed since the baseline', base,
            'changed in proxy/model since the baseline', model_copy('changed', tsv_edit('pure-methods.tsv', change_note)))

    def add_row(t):
        return t.rstrip('\n') + '\nICvGame1::NewThing() const\tpure\t\tCvGame.cpp:1-2\tadded\n'
    refused('a TSV row without a baseline entry', base, 'not in the baseline',
            model_copy('added', tsv_edit('pure-methods.tsv', add_row)))

    def bad_verdict(t):
        ls = t.split('\n')
        ls[1] = '\t'.join([ls[1].split('\t')[0], 'maybe'] + ls[1].split('\t')[2:])
        return '\n'.join(ls)
    refused('a TSV with an unknown verdict', base, 'unknown verdict',
            model_copy('verdict', tsv_edit('pure-methods.tsv', bad_verdict)))

    def dup_key(t):
        ls = t.split('\n')
        return '\n'.join(ls[:2] + [ls[1]] + ls[2:])
    refused('a TSV with a key twice', base, 'twice', model_copy('dup', tsv_edit('lua-pure-bindings.tsv', dup_key)))

    def short_row(t):
        ls = t.split('\n')
        ls[1] = '\t'.join(ls[1].split('\t')[:2])
        return '\n'.join(ls)
    refused('a TSV with a wrong field count', base, 'fields, the header has',
            model_copy('fields', tsv_edit('pure-methods.tsv', short_row)))
    rc, log = py([DERIVE, '--make-baseline'])
    check('refuse: --make-baseline without --fork: exit 2', rc == 2 and '--fork' in log, log[-300:])
    return model_copy


# ---------------------------------------------------------------------------------------------- 5 gen_pure_list
def test_check(s, model_copy):
    rc, log = py([GEN, '--check'])
    check('check: gen_pure_list.py --check on the repository: exit 0', rc == 0 and 'hold' in log, log)

    def src_copy(name, fn):
        d = s / ('src-' + name)
        if d.exists():
            shutil.rmtree(d)
        shutil.copytree(SRC, d)
        fn(d)
        return d
    d = src_copy('inc', lambda d: (d / 'proxy_pure.inc').write_text('static const char* const kPureKeys[] = {};\n',
                                                                    encoding='ascii'))
    rc, log = py([GEN, '--check', '--src-dir', d])
    check('check: proxy_pure.inc present: exit 1', rc == 1 and 'proxy_pure.inc exists' in log, log)
    d = src_copy('arr', lambda d: (d / 'x.cpp').write_text('const char* const kLuaPure[] = {"Plot.GetX"};\n',
                                                           encoding='ascii'))
    rc, log = py([GEN, '--check', '--src-dir', d])
    check('check: a kLuaPure array in proxy/src: exit 1', rc == 1 and 'a compiled purity list' in log, log)

    def unknown_key(m):
        p = m / 'interfaces.json'
        doc = json.loads(p.read_text(encoding='utf-8'))
        for e in doc['interfaces']:
            for x in e['methods']:
                if x['key'] == 'ICvCity1::GetOwner() const':
                    x['key'] = 'ICvCity1::GetOwnerRenamed() const'
        p.write_text(json.dumps(doc), encoding='utf-8')
    rc, log = py([GEN, '--check', '--model-dir', model_copy('unknown', unknown_key)])
    check('check: a listed key not in the model: exit 1', rc == 1 and 'ICvCity1::GetOwner() const is not a method' in log,
          log)

    def edited(m):
        p = m / 'lua-pure-bindings.tsv'
        t = p.read_text(encoding='utf-8').replace('\r\n', '\n').split('\n')
        t[1] = t[1] + ' (edited)'
        p.write_text('\n'.join(t), encoding='utf-8', newline='\n')
    rc, log = py([GEN, '--check', '--model-dir', model_copy('edited', edited)])
    check('check: a TSV edited without --make-baseline: exit 1', rc == 1 and 'since the baseline' in log, log)
    rc, log = py([GEN, '--check', '--src-dir', s / 'no-such-dir'])
    check('check: --src-dir not a folder: exit 2', rc == 2, log)


# ---------------------------------------------------------------------------------------------- 6 regen
def test_regen(s):
    """6. regen_model.py's verdict (3502, 2026-11-14, lead-3500): the same tree under another commit and a changed source
    path length are EQUAL; a model difference is DIFFERENT (regen_model exits 1 exactly when compare_json returns
    difference lines)."""
    import regen_model as rm
    base = {'generated_at': 'x', 'source': {'fork_path': 'D:/a', 'commit': 'a' * 40},
            'usage_scan_source': {'commit': 'a' * 40},
            'counts': {'ast': {'tus': [['A.cpp', 100], ['B.cpp', 200]]}, 'preprocessed_mb_total': 10.0, 'methods': 7},
            'interfaces': [{'name': 'ICvCity1', 'methods': [{'key': 'ICvCity1::GetOwner() const', 'slot': 3}]}],
            'lua': {'generated_at': 'y', 'source': {'fork_path': 'D:/a', 'commit': 'a' * 40},
                    'counts': {'ast': {'tus': [['L.cpp', 50]]}, 'preprocessed_mb_total': 5.0}}}
    d = s / 'regen'
    d.mkdir(parents=True, exist_ok=True)

    def verdict(name, edit):
        new = json.loads(json.dumps(base))
        edit(new)
        (d / 'ref.json').write_text(json.dumps(base), encoding='utf-8')
        (d / (name + '.json')).write_text(json.dumps(new), encoding='utf-8')
        return rm.compare_json(d / 'ref.json', d / (name + '.json'))

    def other_commit(m):
        m['source']['commit'] = m['usage_scan_source']['commit'] = m['lua']['source']['commit'] = 'b' * 40
        m['source']['fork_path'] = m['lua']['source']['fork_path'] = 'C:/c5-64bit-work/3500/releases/x/src/x64-b'
    diffs, info = verdict('commit', other_commit)
    check('regen: the same tree under another commit: EQUAL, commits as information', diffs == [] and
          any(i.startswith('source.commit: committed ' + 'a' * 40) for i in info), (diffs, info))

    def longer_path(m):
        m['counts']['ast']['tus'] = [['A.cpp', 130], ['B.cpp', 260]]
        m['counts']['preprocessed_mb_total'] = 10.9
        m['lua']['counts']['ast']['tus'] = [['L.cpp', 61]]
        m['lua']['counts']['preprocessed_mb_total'] = 5.4
    diffs, info = verdict('path', longer_path)
    check('regen: a longer source path (AST and preprocessed sizes): EQUAL, sizes as information', diffs == [] and
          sum('information only' in i for i in info) == 4, (diffs, info))

    def real(m):
        m['interfaces'][0]['methods'][0]['slot'] = 4
        m['counts']['methods'] = 8
    diffs, info = verdict('real', real)
    check('regen: a model difference (a slot, a count): DIFFERENT', len(diffs) == 2 and
          any('interfaces[0].methods[0].slot' in x for x in diffs), (diffs, info))


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', help='DLL source at the pin (git checkout or plain copy); required but with --only regen')
    ap.add_argument('--dll', help='an x64 CvGameCore_Expansion2.dll (only hashed); required but with --only regen')
    ap.add_argument('--scratch', required=True)
    ap.add_argument('--no-asan', action='store_true', help='leave out section 2 (the run then FAILS)')
    ap.add_argument('--only', choices=['regen'], help='run only section 6 (regen_model.py\'s verdict; pure Python)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = Path(a.scratch).resolve()
    if str(s).lower().startswith(str(REPO).lower()):
        print('test_purity: --scratch must be outside the repository')
        return 2
    s.mkdir(parents=True, exist_ok=True)
    if a.only == 'regen':
        test_regen(s)
        failed = [n for n, ok in RESULTS if not ok]
        print('test_purity: %d/%d checks passed (--only regen)' % (len(RESULTS) - len(failed), len(RESULTS)))
        return 1 if failed else 0
    if not a.src or not a.dll:
        ap.error('--src and --dll are required (but with --only regen)')
    src, dll = str(Path(a.src).resolve()), str(Path(a.dll).resolve())
    test_unit(s)
    if a.no_asan:
        check('asan: section 2 run (--no-asan given: skipped counts as failed)', False)
    else:
        test_asan(s)
    test_derive(s, src, dll)
    model_copy = test_refusals(s, src, dll)
    test_check(s, model_copy)
    test_regen(s)
    failed = [n for n, ok in RESULTS if not ok]
    print('test_purity: %d/%d checks passed%s' % (len(RESULTS) - len(failed), len(RESULTS),
                                                  '' if not failed else '; FAILED: ' + '; '.join(failed)))
    return 0 if not failed else 1


if __name__ == '__main__':
    sys.exit(main())
