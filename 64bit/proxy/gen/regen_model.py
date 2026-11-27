"""Regenerate the whole proxy model from a DLL source and compare it with the committed one (issue 3503, epic 3500
stage `model`, checklist step 10).

    python -I proxy/gen/regen_model.py --src <fork source> --out <dir> [--lock F | --unlocked --commit C]
                                       [--against-dir proxy/model] [--compile-check] [--jobs N] [--write]

Runs, each as its own process, into <out>/model (compiler scratch in <out>/work):
  1. extract_model.py   interfaces.json (clang AST and vtable dumps, x86 and x64)
  2. verify_slots.py    slot-proof.json (MSVC vcall thunks against the clang slots)
  3. extract_lua.py     lua.json
  4. classify.py        model.json and policy-table.tsv (--inputs-dir <out>/model; consumers.tsv stays the committed one)
  5. gen_runtime.py     the generated runtime twice, from the regenerated model (<out>/gen-new) and from --against-dir
                        (<out>/gen-ref), compared file by file; --compile-check compiles gen-new (no link)
then compares the five model files with --against-dir and writes <out>/model-report.txt and model-report.json.

What is masked in the comparison (it names the run, not the model): `generated_at`, `source.fork_path`,
`source.commit` and `usage_scan_source.commit`, and in model.json the same three of its embedded `lua` (the source
commits are reported on their own line: a model regenerated from the same tree under another commit is equal). Also left
out of the verdict and printed as information (3502, 2026-11-14): `counts.ast.tus` and `counts.preprocessed_mb_total`
(and the same under `lua`), which grow with the source folder's path length. Everything else counts: a JSON value anywhere, a TSV row (by its key), a generated file.

--src is checked by toolchain.resolve_source: a git checkout at the lock's commit or a plain copy with the lock's source
fingerprint (deps/dll.lock, or --lock for a release candidate's lock); --unlocked takes any fork-shaped tree (label it
with --commit). --write copies the regenerated model files into --against-dir when every step passed (the commit of a
regenerated model). A file <out>/STOP (or --stop-file) ends the run before the next step (exit 5).

Exit codes: 0 every step passed and the model equals --against-dir; 1 every step passed, differences listed;
2 refused input (source, lock, folders; nothing run); 4 a step failed (its log is named); 5 stopped by the stop file.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import toolchain as tc  # noqa: E402

MODEL_DIR = os.path.join(tc.PROXY, 'model')
FILES = ['interfaces.json', 'slot-proof.json', 'lua.json', 'model.json', 'policy-table.tsv']
MASK = [('generated_at',), ('source', 'fork_path'), ('source', 'commit'), ('usage_scan_source', 'commit'),
        ('lua', 'generated_at'), ('lua', 'source', 'fork_path'), ('lua', 'source', 'commit')]   # model.json embeds lua.json
# 3502, 2026-11-14 (lead-3500 after the 5.4.8 release run): the sizes of the clang AST dumps and of the preprocessed
# text grow with the length of the source folder's path (__FILE__, #line), so they name the run, not the model. They
# are printed as information and left out of the verdict.
INFO_MASK = [('counts', 'ast', 'tus'), ('counts', 'preprocessed_mb_total'),
             ('lua', 'counts', 'ast', 'tus'), ('lua', 'counts', 'preprocessed_mb_total')]
MAX_PATHS = 40


def log(*a):
    print(*a, flush=True)


# ---------------------------------------------------------------------------------------------- comparison
def _masked(doc):
    """A copy of doc with the MASK and INFO_MASK paths removed; returns (copy, {path: value removed})."""
    doc = json.loads(json.dumps(doc))
    gone = {}
    for path in MASK + INFO_MASK:
        node = doc
        for k in path[:-1]:
            node = node.get(k) if isinstance(node, dict) else None
            if node is None:
                break
        if isinstance(node, dict) and path[-1] in node:
            gone['.'.join(path)] = node.pop(path[-1])
    return doc, gone


def json_paths(a, b, path='', out=None):
    """Paths where a and b differ (at most MAX_PATHS + 1 collected)."""
    out = [] if out is None else out
    if len(out) > MAX_PATHS:
        return out
    if type(a) is not type(b):
        out.append('%s: type %s -> %s' % (path or '<root>', type(a).__name__, type(b).__name__))
    elif isinstance(a, dict):
        for k in sorted(set(a) | set(b), key=str):
            p = '%s.%s' % (path, k) if path else str(k)
            if k not in a:
                out.append('%s: only in the regenerated file' % p)
            elif k not in b:
                out.append('%s: only in the committed file' % p)
            else:
                json_paths(a[k], b[k], p, out)
    elif isinstance(a, list):
        if len(a) != len(b):
            out.append('%s: %d items -> %d' % (path, len(a), len(b)))
        for i in range(min(len(a), len(b))):
            json_paths(a[i], b[i], '%s[%d]' % (path, i), out)
    elif a != b:
        ra, rb = json.dumps(a), json.dumps(b)
        out.append('%s: %s -> %s' % (path, ra[:100], rb[:100]))
    return out


def compare_json(ref_path, new_path):
    """Returns (difference lines, info lines)."""
    with open(ref_path, encoding='utf-8') as f:
        ref = json.load(f)
    with open(new_path, encoding='utf-8') as f:
        new = json.load(f)
    ref_m, ref_gone = _masked(ref)
    new_m, new_gone = _masked(new)
    info = []
    for k in ('source.commit', 'usage_scan_source.commit'):
        if k in ref_gone or k in new_gone:
            info.append('%s: committed %s, regenerated %s' % (k, ref_gone.get(k, '-'), new_gone.get(k, '-')))
    for path in INFO_MASK:
        k = '.'.join(path)
        if (k in ref_gone or k in new_gone) and ref_gone.get(k) != new_gone.get(k):
            n = len(json_paths(ref_gone.get(k), new_gone.get(k), k))
            info.append('%s: %d value(s) differ (information only: they depend on the source path)' % (k, n))
    if ref_m == new_m:
        return [], info
    paths = json_paths(ref_m, new_m)
    if len(paths) > MAX_PATHS:
        paths = paths[:MAX_PATHS] + ['... (more differences not listed)']
    return paths, info


def compare_tsv(ref_path, new_path):
    def rows(p):
        with open(p, 'rb') as f:
            lines = f.read().replace(b'\r\n', b'\n').decode('utf-8', 'replace').split('\n')
        out = {}
        for n, line in enumerate(lines, 1):
            if line:
                c = line.split('\t')
                out.setdefault('\t'.join(c[:4]) if n > 1 else '<header>', (n, line))
        return out
    a, b = rows(ref_path), rows(new_path)
    diffs = []
    for key in list(a) + [k for k in b if k not in a]:
        ra, rb = a.get(key), b.get(key)
        if ra and rb and ra[1] == rb[1]:
            continue
        label = key.replace('\t', ' ')
        if ra is None:
            diffs.append('row only in the regenerated table (line %d): %s' % (rb[0], label))
        elif rb is None:
            diffs.append('row only in the committed table (line %d): %s' % (ra[0], label))
        else:
            ca, cb = ra[1].split('\t'), rb[1].split('\t')
            cols = [i + 1 for i in range(max(len(ca), len(cb))) if ca[i:i + 1] != cb[i:i + 1]]
            diffs.append('row differs (columns %s): %s' % (','.join(map(str, cols)), label))
    return diffs[:MAX_PATHS] + (['... (more rows not listed)'] if len(diffs) > MAX_PATHS else []), []


def compare_trees(ref_dir, new_dir):
    """Generated runtime: every file byte for byte (CRLF folded)."""
    def files(d):
        out = {}
        for root, _dirs, names in os.walk(d):
            for n in names:
                p = os.path.join(root, n)
                out[os.path.relpath(p, d).replace('\\', '/')] = p
        return out
    a, b = files(ref_dir), files(new_dir)
    diffs = []
    for rel in sorted(set(a) | set(b)):
        if rel not in b:
            diffs.append('only from the committed model: %s' % rel)
        elif rel not in a:
            diffs.append('only from the regenerated model: %s' % rel)
        else:
            with open(a[rel], 'rb') as f:
                x = f.read().replace(b'\r\n', b'\n')
            with open(b[rel], 'rb') as f:
                y = f.read().replace(b'\r\n', b'\n')
            if x != y:
                xl, yl = x.split(b'\n'), y.split(b'\n')
                first = next((i for i in range(min(len(xl), len(yl))) if xl[i] != yl[i]), min(len(xl), len(yl)))
                diffs.append('differs: %s (first at line %d)' % (rel, first + 1))
    return diffs


# ---------------------------------------------------------------------------------------------- steps
class Stopped(Exception):
    pass


def run_step(name, argv, logs, stop_file, steps):
    if stop_file and os.path.exists(stop_file):
        raise Stopped(name)
    lp = os.path.join(logs, name + '.log')
    t0 = time.time()
    with open(lp, 'w', encoding='utf-8', errors='replace') as f:
        cp = subprocess.run([sys.executable, '-I'] + argv, stdout=f, stderr=subprocess.STDOUT, cwd=tc.REPO)
    steps.append({'name': name, 'rc': cp.returncode, 'seconds': round(time.time() - t0, 1), 'log': lp})
    log('regen_model: %-14s rc %d  %5.1f s  %s' % (name, cp.returncode, time.time() - t0, lp))
    return cp.returncode


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', required=True)
    ap.add_argument('--lock')
    ap.add_argument('--unlocked', action='store_true')
    ap.add_argument('--commit')
    ap.add_argument('--out', required=True, help='work folder (outside the repository)')
    ap.add_argument('--against-dir', default=MODEL_DIR)
    ap.add_argument('--compile-check', action='store_true')
    ap.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 4))
    ap.add_argument('--write', action='store_true', help='copy the regenerated files into --against-dir')
    ap.add_argument('--stop-file')
    a = ap.parse_args(argv)
    out = os.path.abspath(a.out)
    if (out + os.sep).lower().startswith(tc.REPO.lower() + os.sep):
        log('regen_model: --out %s is inside the repository' % out)
        return 2
    against = os.path.abspath(a.against_dir)
    missing = [f for f in FILES if not os.path.isfile(os.path.join(against, f))]
    if missing:
        log('regen_model: --against-dir %s lacks %s' % (against, ', '.join(missing)))
        return 2
    try:
        info = tc.resolve_source(a.src, a.lock, a.unlocked, a.commit)
    except tc.SourceError as ex:
        log('regen_model: --src refused: %s' % ex)
        return 2
    log('regen_model: source %s (%s)' % (info['src'], info['note']))
    model, work, logs = os.path.join(out, 'model'), os.path.join(out, 'work'), os.path.join(out, 'logs')
    for d in (model, work, logs):
        os.makedirs(d, exist_ok=True)
    stop = a.stop_file or os.path.join(out, 'STOP')
    src = ['--src', info['src']] + (['--lock', a.lock] if a.lock else []) + \
        (['--unlocked', '--commit', info['head']] if a.unlocked else [])
    g = os.path.join(HERE, '%s.py')
    steps = []
    plan = [
        ('extract_model', [g % 'extract_model'] + src + ['--work', work, '--out-dir', model]),
        ('verify_slots', [g % 'verify_slots'] + src + ['--work', work, '--model-dir', model, '--out-dir', model]),
        ('extract_lua', [g % 'extract_lua'] + src + ['--work', work, '--out-dir', model]),
        ('classify', [g % 'classify'] + src + ['--inputs-dir', model, '--out-dir', model]),
        ('gen_new', [g % 'gen_runtime', '--out', os.path.join(out, 'gen-new'), '--src', info['src'],
                     '--model-dir', model] +
         (['--compile-check', '--objdir', os.path.join(work, 'obj-new'), '--jobs', str(a.jobs)] if a.compile_check
          else [])),
        ('gen_ref', [g % 'gen_runtime', '--out', os.path.join(out, 'gen-ref'), '--src', info['src'],
                     '--model-dir', against]),
    ]
    t0 = time.time()
    try:
        for name, argv_ in plan:
            rc = run_step(name, argv_, logs, stop, steps)
            if rc != 0:
                write_report(out, info, steps, None, 'step %s failed (rc %d, log %s)' % (name, rc, steps[-1]['log']))
                log('regen_model: FAILED at %s (rc %d); log %s' % (name, rc, steps[-1]['log']))
                return 4
    except Stopped as ex:
        write_report(out, info, steps, None, 'stopped by %s before step %s' % (stop, ex))
        log('regen_model: STOPPED by %s before step %s' % (stop, ex))
        return 5
    result = {}
    for f in FILES:
        ref, new = os.path.join(against, f), os.path.join(model, f)
        result[f] = (compare_tsv if f.endswith('.tsv') else compare_json)(ref, new)
    result['generated runtime'] = (compare_trees(os.path.join(out, 'gen-ref'), os.path.join(out, 'gen-new')), [])
    ndiff = sum(len(v[0]) for v in result.values())
    write_report(out, info, steps, result, None)
    for name, (diffs, infos) in result.items():
        for i in infos:
            log('regen_model: %s: %s' % (name, i))
        log('regen_model: %s: %s' % (name, 'equal' if not diffs else '%d difference(s)' % len(diffs)))
        for d in diffs:
            log('    ' + d)
    if a.write:
        for f in FILES:
            shutil.copyfile(os.path.join(model, f), os.path.join(against, f))
        log('regen_model: --write: the regenerated model copied into %s' % against)
    log('regen_model: %s in %.0f s; report %s' % ('EQUAL' if not ndiff else 'DIFFERENT (%d)' % ndiff,
                                                   time.time() - t0, os.path.join(out, 'model-report.txt')))
    return 0 if not ndiff else 1


def write_report(out, info, steps, result, failure):
    doc = {'source': {'path': info['src'], 'commit': info['head'], 'mode': info['mode'], 'note': info['note']},
           'steps': steps, 'failure': failure,
           'equal': None if result is None else not any(v[0] for v in result.values()),
           'files': None if result is None else {k: {'differences': v[0], 'info': v[1]} for k, v in result.items()}}
    with open(os.path.join(out, 'model-report.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, indent=1)
    lines = ['regen_model report', 'source: %s (%s)' % (info['src'], info['note'])]
    lines += ['step %-14s rc %d %6.1f s %s' % (s['name'], s['rc'], s['seconds'], s['log']) for s in steps]
    if failure:
        lines.append('FAILURE: ' + failure)
    if result is not None:
        for k, (d, i) in result.items():
            lines.append('%s: %s' % (k, 'equal' if not d else '%d difference(s)' % len(d)))
            lines += ['  info: ' + x for x in i] + ['  ' + x for x in d]
    with open(os.path.join(out, 'model-report.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    sys.exit(main())
