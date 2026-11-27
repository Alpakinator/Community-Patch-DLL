"""Ticket 4801 (epic 0504 P1): the private-detail checks of a merged PGO profile (research/08-performance/
speedup-p1-pgo.md section 4).

    python -I tools/x64-dll/pgo/check_profile.py PROFILE.profdata --work DIR

1. `llvm-profdata show --all-functions`: every function name; none may hold an absolute path (a drive letter, a UNC
   or a POSIX root), a `Users` folder, or the user or machine name of this environment. The names of functions with
   internal linkage carry a source file prefix (`<file>;<name>`): every prefix must be a relative path under the
   DLL source's folders. The prefixes are counted and the first ones listed.
2. The same search over the file's raw bytes (Latin-1 and UTF-16LE views), so that no name escapes the tool's output.
3. `tools/export/check.py --tree` over a folder holding a copy of the profile (the export's pass-or-fail check of
   ticket 4700, run on this file; its own allow-list puts the file in the product group for the run).
4. `--detailed-summary` written to DIR for the record.

Matches are never printed in full (user and machine names are masked); exit 0 when every check passes, 1 otherwise.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PROFDATA = Path(r'C:\Program Files\LLVM\bin\llvm-profdata.exe')
SOURCE_TOPS = ('CvGameCoreDLL_Expansion2', 'CvGameCoreDLLUtil', 'CvGameDatabase', 'CvLocalization',
               'CvWorldBuilderMap', 'FirePlace', 'ThirdPartyLibs')

ABS_RES = {
    'drive-path': re.compile(r'(?<![A-Za-z0-9_])[A-Za-z]:[\\/]'),
    'unc-path': re.compile(r'\\\\[A-Za-z0-9_.-]+\\'),
    'users-folder': re.compile(r'(?i)[\\/](?:users|home)[\\/]'),
}
# In the raw bytes a lone "M:/" occurs by chance in hashes and counters (2 in the 13.5 MB profile of 2026-11-17); a
# path there has at least one folder name after the drive: X:\name\ or X:/name/.
RAW_RES = {
    'drive-path': re.compile(r'(?<![A-Za-z0-9_])[A-Za-z]:[\\/][A-Za-z0-9_ .$~-]{1,64}[\\/]'),
    'unc-path': ABS_RES['unc-path'],
    'users-folder': ABS_RES['users-folder'],
}


def private_terms() -> list:
    """This environment's user and machine names (never written anywhere; matches are masked)."""
    terms = set()
    for k in ('USERNAME', 'USER', 'COMPUTERNAME', 'HOSTNAME'):
        v = os.environ.get(k, '').strip()
        if len(v) >= 3:
            terms.add(v)
    home = Path.home().name
    if len(home) >= 3:
        terms.add(home)
    return sorted(terms)


def mask(s: str) -> str:
    return re.sub(r'[A-Za-z0-9]+', lambda m: m.group(0)[0] + '*' * (len(m.group(0)) - 1), s)


def function_names(profile: Path) -> list:
    cp = subprocess.run([str(PROFDATA), 'show', '--all-functions', str(profile)], capture_output=True, text=True,
                        errors='replace')
    if cp.returncode != 0:
        raise RuntimeError(f'llvm-profdata show rc {cp.returncode}: {cp.stderr.strip()}')
    names = []
    for line in cp.stdout.splitlines():
        if line.startswith('  ') and not line.startswith('   ') and line.endswith(':'):
            names.append(line[2:-1])
    return names


def scan(text: str, terms: list, res=ABS_RES) -> collections.Counter:
    """Path matches and private terms as whole words (not inside a longer identifier, as tools/export/check.py: a
    short user name is a substring of many C++ names)."""
    hits = collections.Counter()
    for k, rx in res.items():
        hits[k] += len(rx.findall(text))
    for t in terms:
        rx = re.compile(r'(?<![A-Za-z0-9_])' + re.escape(t) + r'(?![A-Za-z0-9_])', re.I)
        hits['private-term'] += len(rx.findall(text))
    return hits


def export_check(tree: Path, allow: Path) -> dict:
    cp = subprocess.run([sys.executable, '-I', str(REPO / 'tools' / 'export' / 'check.py'), '--tree', str(tree),
                         '--allowlist', str(allow)], capture_output=True, text=True, errors='replace')
    lines = (cp.stdout + cp.stderr).strip().splitlines()
    return {'rc': cp.returncode, 'findings': [l for l in lines if not l.startswith('export-check:')][:20],
            'tail': [l for l in lines if l.startswith('export-check:')]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('profile', type=Path)
    ap.add_argument('--work', type=Path, required=True)
    a = ap.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    terms = private_terms()
    rep = {'profile': a.profile.name, 'terms_checked': len(terms)}
    ok = True

    names = function_names(a.profile)
    rep['functions'] = len(names)
    name_hits = collections.Counter()
    bad_examples = []
    prefixes = collections.Counter()
    bad_prefix = []
    for n in names:
        h = scan(n, terms)
        if sum(h.values()):
            name_hits.update(h)
            if len(bad_examples) < 10:
                bad_examples.append(mask(n[:160]))
        if ';' in n:
            pre = n.split(';', 1)[0]
            prefixes[pre] += 1
            top = re.split(r'[\\/]', pre, 1)[0]
            if top not in SOURCE_TOPS:
                bad_prefix.append(mask(pre))
    rep['name_hits'] = dict(name_hits)
    rep['name_hit_examples_masked'] = bad_examples
    rep['local_functions'] = sum(prefixes.values())
    rep['prefix_files'] = len(prefixes)
    rep['prefix_examples'] = [f'{p} ({c})' for p, c in sorted(prefixes.items())[:12]]
    rep['prefixes_outside_source_tops'] = sorted(set(bad_prefix))[:20]
    print(f'1. function names: {len(names)} functions, {rep["local_functions"]} with a file prefix in '
          f'{len(prefixes)} files; path/user hits {dict(name_hits) or "none"}; prefixes outside the source folders '
          f'{len(set(bad_prefix))}')
    for e in rep['prefix_examples'][:6]:
        print(f'   e.g. {e}')
    if sum(name_hits.values()) or bad_prefix or not names:
        ok = False

    data = a.profile.read_bytes()
    text = data.decode('latin-1') + '\n' + data.decode('utf-16-le', errors='ignore')
    raw_hits = scan(text, terms, RAW_RES)
    rep['raw_hits'] = {k: v for k, v in raw_hits.items() if v}
    print(f'2. raw bytes ({len(data)} B): hits {rep["raw_hits"] or "none"}')
    if rep['raw_hits']:
        ok = False

    # 3. tools/export/check.py: (a) on the binary file as it is; (b) on its text form (`llvm-profdata merge --text`:
    # every name, hash and counter as text). The binary file is searched by check.py as Latin-1 and UTF-16 views, where
    # a mangled name followed by random bytes can look like an e-mail address; the text form has no random bytes, so
    # it decides. Each file in its own tree, the file alone as product.
    allow = a.work / 'export-check-allowlist.txt'
    allow.write_text('# ticket 4801: the profile file alone, as product\n[product]\n**\n', encoding='utf-8')
    res = {}
    for kind in ('binary', 'text'):
        tree = a.work / f'export-check-{kind}'
        if tree.exists():
            shutil.rmtree(tree)
        tree.mkdir()
        if kind == 'binary':
            shutil.copyfile(a.profile, tree / a.profile.name)
        else:
            cp = subprocess.run([str(PROFDATA), 'merge', '--text', '-o', str(tree / (a.profile.stem + '.proftext')),
                                 str(a.profile)], capture_output=True, text=True, errors='replace')
            if cp.returncode != 0:
                res[kind] = {'rc': 2, 'findings': [cp.stderr.strip()], 'tail': []}
                continue
        res[kind] = export_check(tree, allow)
        print(f'3{"a" if kind == "binary" else "b"}. tools/export/check.py --tree ({kind}): rc {res[kind]["rc"]}; '
              + ' | '.join(res[kind]['findings'][:3] + res[kind]['tail'][:1]))
    rep['export_check'] = res
    if res['text']['rc'] != 0:
        ok = False
    rep['export_check_binary_note'] = ('binary findings are reported, not failed: check.py reads the binary file but '
                                       'its text rules see random bytes beside names; the text form decides')

    summ = subprocess.run([str(PROFDATA), 'show', '--detailed-summary', str(a.profile)], capture_output=True,
                          text=True, errors='replace')
    (a.work / 'detailed-summary.txt').write_text(summ.stdout + summ.stderr, encoding='utf-8')
    rep['ok'] = ok
    (a.work / 'check_profile.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    print(('OK' if ok else 'FAILED') + f': {a.profile.name}; report {a.work / "check_profile.json"}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
