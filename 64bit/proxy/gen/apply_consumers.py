"""Issue 3303: write the consumer column of proxy/model/consumers.tsv into the committed policy-table.tsv and model.json
without regenerating them.

    python -I proxy/gen/apply_consumers.py [--model-dir DIR] [--check]

Why it exists (issue 3303): the committed policy-table.tsv carried a hand edit (one-off 9029: the TryGameCoreLock row is
server_local since upstream fix 6cc672569) that classify.py could not reproduce from the fork clone its toolchain names
(toolchain.FORK at e9d0c4837). Since issue 0125 (2026-11-13) that is fixed: `classify.py --src <fork at the
deps/dll.lock commit>` reproduces both files, the TryGameCoreLock rule text included (classify.RULE_NOTES), and
`classify.py --src ... --check` / proxy/tools/test_policy_table.py check it. A full regeneration is the normal way now;
this script stays as the quick way to rewrite only the consumer column after an edit of consumers.tsv (no fork source
needed). classify.py writes the same column (load_consumers and consumer_of are shared).

What it does: validates consumers.tsv (classify.load_consumers: unknown method, bad value, empty reason, duplicate row
-> exit 1, files untouched), then sets the last column `consumer` of every policy-table.tsv row (adds the column when the
header lacks it; dll_to_engine rows: the listed value or `exe`; engine_to_dll rows: `-`) and the `consumer` field
({value, reason}) of every dll_to_engine method in model.json's interfaces (formatting as classify.py writes it). Nothing
else changes. --check: write nothing; exit 1 when either file is not up to date. Exit 0 otherwise.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import classify  # noqa: E402


def apply(model_dir, check):
    tsv_path = os.path.join(model_dir, 'policy-table.tsv')
    json_path = os.path.join(model_dir, 'model.json')
    cons_path = os.path.join(model_dir, 'consumers.tsv')
    with open(json_path, encoding='utf-8') as f:
        model = json.load(f)
    d2e = {(m['declared_in'], m['name']) for e in model['interfaces'] if e['direction'] == 'dll_to_engine'
           for m in e['methods']}
    try:
        consumers = classify.load_consumers(cons_path, d2e)
    except classify.ConsumerError as ex:
        print('apply_consumers: consumers.tsv refused: %s' % ex)
        return 1
    # model.json
    for e in model['interfaces']:
        if e['direction'] != 'dll_to_engine':
            continue
        for m in e['methods']:
            c = consumers.get((m['declared_in'], m['name']))
            m['consumer'] = {'value': c[0] if c else 'exe',
                             'reason': c[1] if c else 'default: not listed in proxy/model/consumers.tsv'}
    new_json = json.dumps(model, indent=1, ensure_ascii=True)
    # policy-table.tsv
    with open(tsv_path, encoding='utf-8', newline='') as f:
        lines = f.read().replace('\r\n', '\n').split('\n')   # a checkout with core.autocrlf has CRLF
    if lines and lines[-1] == '':
        lines.pop()
    head = lines[0].split('\t')
    has = head[-1] == 'consumer'
    if not has and len(head) != 11:
        print('apply_consumers: policy-table.tsv header has %d columns (11 expected without consumer)' % len(head))
        return 1
    out = ['\t'.join((head[:-1] if has else head) + ['consumer'])]
    seen = set()
    for n, line in enumerate(lines[1:], 2):
        cols = line.split('\t')
        if len(cols) != len(head):
            print('apply_consumers: policy-table.tsv line %d: %d columns, %d expected' % (n, len(cols), len(head)))
            return 1
        base = cols[:-1] if has else cols
        out.append('\t'.join(base + [classify.consumer_of(consumers, base[0], base[1], base[2])]))
        if base[0] == 'dll_to_engine':
            seen.add((base[1], base[2]))
    missing = sorted(set(consumers) - seen)
    if missing:
        print('apply_consumers: consumers.tsv rows with no policy-table.tsv row: %s' % missing)
        return 1
    new_tsv = '\n'.join(out) + '\n'
    with open(json_path, encoding='utf-8') as f:
        old_json = f.read()
    with open(tsv_path, encoding='utf-8', newline='') as f:
        old_tsv = f.read().replace('\r\n', '\n')
    stale = [p for p, a, b in ((tsv_path, old_tsv, new_tsv), (json_path, old_json, new_json)) if a != b]
    if check:
        print('apply_consumers: %s' % ('up to date' if not stale else 'NOT up to date: ' + ', '.join(stale)))
        return 1 if stale else 0
    with open(json_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new_json)
    with open(tsv_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new_tsv)
    counts = {}
    for line in out[1:]:
        v = line.rsplit('\t', 1)[1]
        counts[v] = counts.get(v, 0) + 1
    print('apply_consumers: wrote %s (%s) and %s' % (tsv_path, ', '.join('%s %d' % kv for kv in sorted(counts.items())),
                                                    json_path))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--model-dir', default=classify.MODEL_DIR)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    return apply(a.model_dir, a.check)


if __name__ == '__main__':
    sys.exit(main())
