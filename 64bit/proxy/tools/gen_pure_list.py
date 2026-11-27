"""Checks the audited purity lists and that the proxy carries no compiled purity list (issue 2700; since issue 3503
the lists are data: purity.tsv beside the x64 DLL, written by proxy/tools/derive_purity.py, read by
proxy/src/proxy_purity.cpp).

    python -I proxy/tools/gen_pure_list.py --check      (proxy/build_proxy.py step 0)
    python -I proxy/tools/gen_pure_list.py [--list]     print the keys the audited lists give (method and lua lines)

The epoch mirror (proxy/src/proxy_local.cpp, research/03-dll-engine-boundary/proxy-design.md sections 16 and 16.9)
treats an engine -> DLL crossing as pure only when the loaded purity.tsv lists its model key: the `pure` and `neutral`
rows of proxy/model/pure-methods.tsv, minus the four kRt_* rows (decision D30: every runtime message except kRt_Canon and
kRt_LockSync bumps the epoch, whatever its audit row says), and the UI-Lua mirror only the `pure` rows of
proxy/model/lua-pure-bindings.tsv, each kept only while derive_purity.py finds its source evidence unchanged.

--check fails (exit 1, one line per problem) when
  - proxy/src still holds a compiled list (proxy_pure.inc, a kPureKeys or kLuaPure array);
  - a TSV is malformed (field count, empty key, unknown verdict, a key twice), or proxy/model/purity-baseline.json does
    not match the TSVs' kept rows (an audit edited without --make-baseline);
  - a kept method key is not a method of proxy/model/interfaces.json, or a kept Lua name not a function of
    proxy/model/lua.json (the model moved under the list).
Exit 0 when everything holds.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import derive_purity as dp  # noqa: E402

ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'src')
COMPILED = re.compile(r'\b(kPureKeys|kLuaPure)\s*\[')


def keys():
    """{'method': [...], 'lua': [...]} of the audited lists (raises dp.Refused)."""
    out = {'method': [], 'lua': []}
    for spec in dp.LISTS:
        out[spec['kind']] += [k for k, _v, _e, _r in dp.kept_rows(spec)]
    return {k: sorted(v) for k, v in out.items()}


def problems():
    found = []
    if os.path.exists(os.path.join(SRC, 'proxy_pure.inc')):
        found.append('proxy/src/proxy_pure.inc exists: the method list must come from purity.tsv (issue 3503)')
    for f in sorted(os.listdir(SRC)):
        if f.endswith(('.cpp', '.h', '.inc')):
            with open(os.path.join(SRC, f), encoding='utf-8', errors='replace') as fh:
                for n, line in enumerate(fh, 1):
                    if COMPILED.search(line.split('//')[0]):
                        found.append('proxy/src/%s:%d: a compiled purity list (%s)' % (f, n, line.strip()[:80]))
    try:
        k = keys()
        dp.load_baseline(os.path.join(dp.MODEL, 'purity-baseline.json'))
    except dp.Refused as ex:
        return found + [str(ex)]
    with open(os.path.join(dp.MODEL, 'interfaces.json'), encoding='utf-8') as f:
        model_keys = {m['key'] for e in json.load(f)['interfaces'] for m in e['methods']}
    with open(os.path.join(dp.MODEL, 'lua.json'), encoding='utf-8') as f:
        lua_ids = {m['id'] for m in json.load(f)['methods']}
    found += ['pure-methods.tsv: %s is not a method of proxy/model/interfaces.json' % x for x in k['method']
              if x not in model_keys]
    found += ['lua-pure-bindings.tsv: %s is not a function of proxy/model/lua.json' % x for x in k['lua']
              if x not in lua_ids]
    return found


def main(argv=None):
    global SRC
    argv = sys.argv[1:] if argv is None else argv
    # --src-dir / --model-dir: other folders than proxy/src and proxy/model (the tests' copies, test_purity.py)
    for opt in ('--src-dir', '--model-dir'):
        if opt in argv:
            i = argv.index(opt)
            if i + 1 >= len(argv) or not os.path.isdir(argv[i + 1]):
                print('gen_pure_list: %s needs a folder' % opt)
                return 2
            if opt == '--src-dir':
                SRC = os.path.abspath(argv[i + 1])
            else:
                dp.MODEL = os.path.abspath(argv[i + 1])
    if '--check' in argv:
        p = problems()
        for x in p:
            print('gen_pure_list: ' + x)
        if p:
            return 1
        k = keys()
        print('gen_pure_list: the audited lists hold (%d method keys, %d Lua names; baseline matches; no compiled list '
              'in proxy/src)' % (len(k['method']), len(k['lua'])))
        return 0
    try:
        k = keys()
    except dp.Refused as ex:
        print('gen_pure_list: ' + str(ex))
        return 1
    for kind in ('method', 'lua'):
        for x in k[kind]:
            print('%s\t%s' % (kind, x))
    return 0


if __name__ == '__main__':
    sys.exit(main())
