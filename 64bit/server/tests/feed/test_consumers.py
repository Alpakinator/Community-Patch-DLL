"""Tests of the consumer column (issue 3303): proxy/gen/classify.py load_consumers / consumer_of and
proxy/gen/apply_consumers.py, on copies of the committed model in a scratch folder.

    python -I server/tests/feed/test_consumers.py --out DIR

Positive: the committed consumers.tsv loads; the committed policy-table.tsv and model.json are up to date with it
(--check exit 0); apply on a copy is idempotent. Negative (each must exit 1 and leave both model files byte-identical):
unknown method, a consumer value outside exe/renderer/both, an empty reason, a duplicate row, a bad header, a row with
3 fields, a missing consumers.tsv, an engine_to_dll method named; --check on a stale copy exits 1. Exit 0 when every case
passes, 1 otherwise.
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
MODEL = REPO / 'proxy' / 'model'
APPLY = REPO / 'proxy' / 'gen' / 'apply_consumers.py'
HEADER = 'interface\tmethod\tconsumer\treason\n'
GOOD = 'ICvEngineUtility1\tGameplayPlotStateChange\tboth\tfeed\n'


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def run_apply(model_dir, *extra):
    return subprocess.run([sys.executable, '-I', str(APPLY), '--model-dir', str(model_dir)] + list(extra),
                          capture_output=True, text=True, errors='replace')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    out = a.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    passed = failed = 0

    def check(name, ok, detail=''):
        nonlocal passed, failed
        if ok:
            passed += 1
            print('PASS', name)
        else:
            failed += 1
            print('FAIL', name, detail)

    # committed state
    cp = run_apply(MODEL, '--check')
    check('committed model is up to date with consumers.tsv (--check exit 0)', cp.returncode == 0, cp.stdout + cp.stderr)

    def fresh(name):
        d = out / name
        d.mkdir()
        for f in ('policy-table.tsv', 'model.json', 'consumers.tsv'):
            shutil.copy2(MODEL / f, d / f)
        return d

    d = fresh('idem')
    before = (sha(d / 'policy-table.tsv'), sha(d / 'model.json'))
    cp = run_apply(d)
    check('apply on a copy: exit 0', cp.returncode == 0, cp.stdout + cp.stderr)
    cp = run_apply(d, '--check')
    check('apply is idempotent (--check exit 0 after apply)', cp.returncode == 0, cp.stdout)
    # content: the listed rows say both, an unlisted dll_to_engine row exe, an engine_to_dll row '-'
    rows = [l.split('\t') for l in (d / 'policy-table.tsv').read_text(encoding='utf-8').splitlines()]
    hdr = rows[0]
    check('header ends with consumer', hdr[-1] == 'consumer' and len(hdr) == 12, hdr)
    by = {(r[0], r[1], r[2]): r[-1] for r in rows[1:]}
    check('GameplayPlotStateChange is both', by.get(('dll_to_engine', 'ICvEngineUtility1', 'GameplayPlotStateChange')) == 'both')
    check('UpdateCountryBorder (ICvUserInterface1) is both',
          by.get(('dll_to_engine', 'ICvUserInterface1', 'UpdateCountryBorder')) == 'both')
    # 4470: TryGameCoreLock, was GameplayUnitMoved (listed `both` since epic 0375's unit calls)
    check('an unlisted dll_to_engine row is exe', by.get(('dll_to_engine', 'ICvEngineUtility1', 'TryGameCoreLock')) == 'exe')
    check('engine_to_dll rows are -', all(r[-1] == '-' for r in rows[1:] if r[0] == 'engine_to_dll'))
    check('every row has 12 fields', all(len(r) == 12 for r in rows))
    del before

    bad_cases = {
        'unknown method': HEADER + 'ICvEngineUtility1\tGameplayNoSuchThing\tboth\tx\n',
        'bad value': HEADER + 'ICvEngineUtility1\tGameplayPlotStateChange\tEXE\tx\n',
        'empty reason': HEADER + 'ICvEngineUtility1\tGameplayPlotStateChange\tboth\t \n',
        'duplicate row': HEADER + GOOD + GOOD,
        'bad header': 'iface\tmethod\tconsumer\treason\n' + GOOD,
        'three fields': HEADER + 'ICvEngineUtility1\tGameplayPlotStateChange\tboth\n',
        'engine_to_dll method': HEADER + 'ICvGame1\tUpdate\tboth\tx\n',
        'no header (comments only)': '# nothing\n',
    }
    for name, text in bad_cases.items():
        d = fresh('bad-' + name.replace(' ', '_').replace('(', '').replace(')', ''))
        (d / 'consumers.tsv').write_text(text, encoding='utf-8')
        h0 = (sha(d / 'policy-table.tsv'), sha(d / 'model.json'))
        cp = run_apply(d)
        h1 = (sha(d / 'policy-table.tsv'), sha(d / 'model.json'))
        check(f'negative: {name}: exit 1, refused, files untouched',
              cp.returncode == 1 and 'refused' in cp.stdout and h0 == h1, f'exit {cp.returncode}: {cp.stdout[-400:]}')
    d = fresh('bad-missing')
    (d / 'consumers.tsv').unlink()
    h0 = (sha(d / 'policy-table.tsv'), sha(d / 'model.json'))
    cp = run_apply(d)
    check('negative: missing consumers.tsv: exit 1, files untouched',
          cp.returncode == 1 and 'missing' in cp.stdout and h0 == (sha(d / 'policy-table.tsv'), sha(d / 'model.json')),
          cp.stdout)
    # stale: a row changed to renderer -> --check exits 1, apply then makes it renderer
    d = fresh('stale')
    t = (d / 'consumers.tsv').read_text(encoding='utf-8').replace(
        'GameplayWallCreated\tboth', 'GameplayWallCreated\trenderer')
    (d / 'consumers.tsv').write_text(t, encoding='utf-8')
    cp = run_apply(d, '--check')
    check('negative: --check on a stale model exits 1', cp.returncode == 1 and 'NOT up to date' in cp.stdout, cp.stdout)
    cp = run_apply(d)
    rows = [l.split('\t') for l in (d / 'policy-table.tsv').read_text(encoding='utf-8').splitlines()]
    check('apply writes renderer when listed', any(r[2] == 'GameplayWallCreated' and r[-1] == 'renderer' for r in rows))
    print(f'test_consumers: {passed} passed, {failed} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
