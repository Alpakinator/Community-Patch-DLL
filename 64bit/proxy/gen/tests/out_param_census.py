r"""Census of the generated methods that return values through a parameter (issue 0380; proxy-design 31.4, 31.5).

    python -I proxy/gen/tests/out_param_census.py [--out <file.tsv>]

Offline, under 2 s, no source tree. For the product generator (gen_runtime.py: every interface, both directions) it
lists each mechanical method parameter whose codec has an out half (the callee's value goes back to the caller), and
for the proof generator (gen_proxy.py, proxy/proof) each out_scalar / out_str / out_array_i32 parameter; each row is
classed: fixed by in/out (0380), in/out before 0380, or not affected, with the reason. Hand-written hooks are not
generated and are not listed (their wire is their own).

Columns: generator, direction, interface (the most derived one the generated code serves), method key, parameter,
type, codec, in half (yes / no), class, reason. The test test_gen_inout.py checks that no product row is copy-out.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import gen_proxy as gp  # noqa: E402
import gen_runtime as gr  # noqa: E402

# Why the old copy-out of a caller buffer differed from in-process, per method [C: the DLL at the pin 04b1211b8,
# CvDllContext.cpp, CvDllPreGame.cpp, CvDllGame.cpp, CvGlobals.cpp, CvDllNetInitInfo.cpp].
CALLER_BUFFER_WHY = {
    'GetDefineSTRING': 'returns false without writing when the define is missing: the caller got "" over its first byte '
                       'instead of its own bytes',
    'GetGameOption': 'by index: returns false without writing (index out of range, name longer than the buffer): the '
                     'caller got "" instead of its own bytes',
    'GetMapOption': 'by index: returns false without writing (index out of range, name longer than the buffer): the '
                    'caller got "" instead of its own bytes',
    'GetHexDebugLayerString': 'returns true without writing for a water plot and an unknown layer name (CvGlobals.cpp '
                              'GetHexDebugLayerString): the caller got "" where it kept its own bytes',
    'GetGreatWorkAudio': 'returns false without writing (index -1, no great work type, name too long): the caller got '
                         '"" instead of its own bytes',
    'SetTimeStr': 'writes whenever the pointer is set (strcpy_s 256): no difference before 0380 either',
    'GetEvaluatedMapScriptPath': 'DLL -> engine; the one DLL caller (CvDllNetInitInfo.cpp) passes a zeroed 1040-byte '
                                 'buffer and uses it whatever the result: no difference before 0380 either',
}


def product_rows():
    m = gr.Model()
    rows = []
    for n in m.mds:
        it = gr.Iface(m, n)
        for x in it.meths:
            if x.reasons or x.special:
                continue
            for c in x.params:
                if c is None or type(c).callee_out is gr.P.callee_out:
                    continue
                has_in = bool(c.caller_enc('proxy' if x.call_dir == 'e2d' else 'server'))
                kind = type(c).__name__
                if kind == 'PStream':
                    cls, why = 'not affected', ('a stream object, not a value: a Read* stream is consumed, a Write* '
                                                'stream appended to (ProxyRt/SrvRt Stream*), as in-process')
                elif kind in ('PScalarRef', 'PScalarPtr', 'PCvStringRef'):
                    cls, why = 'in/out before 0380', 'in/out since d345f35c (2026-11-10, task 2301)'
                    if x.key == 'ICvPreGame1::GetMapOption(const char *, int &)':
                        why += ('; the blank values of proxy-design 31 came from the missing map-script rows, fixed by '
                                'the server hook ServerHook_ICvPreGame2_GetMapOption_s37 (0380)')
                elif kind == 'PCallerBuf':
                    w = CALLER_BUFFER_WHY.get(x.name)
                    if w is None:
                        cls, why = 'not reviewed', 'caller buffer not in CALLER_BUFFER_WHY'
                    elif 'no difference before 0380 either' in w:
                        cls, why = 'not affected (in/out since 0380)', 'was copy-out from a zeroed buffer; ' + w
                    else:
                        cls, why = 'fixed by in/out (0380)', 'was copy-out from a zeroed buffer; ' + w
                elif kind == 'POutArray':
                    cls = 'fixed by in/out (0380)'
                    why = ('was copy-out from a zeroed array; CvDllTeam::GetTechs writes only the techs it finds and '
                           'returns their count: the elements past the count came back 0 instead of the caller\'s')
                else:
                    cls, why = 'not reviewed', kind
                if not has_in and kind != 'PStream':
                    cls, why = 'COPY-OUT', 'no in half: the callee does not start from the caller\'s value'
                rows.append(('gen_runtime', x.call_dir, n, x.key, c.n, c.t, kind, 'yes' if has_in else 'no', cls, why))
    return rows


def proof_rows():
    with open(gp.MODEL, encoding='utf-8') as f:
        import json
        model = json.load(f)
    rows = []
    for k, e in enumerate(model['interfaces']):
        it = gp.Iface(e, k + 1)
        for x in it.methods:
            if x['local'] or not x['c']['mechanical']:
                continue
            for p in x['c']['params']:
                if p['codec'] not in ('out_scalar', 'out_str', 'out_array_i32'):
                    continue
                has_in = gp.enc_in(p, p['name']) is not None
                if p['codec'] == 'out_array_i32':
                    cls, why = 'not affected', ('proof generator only (proxy/proof builds ICvRandom1 and ICvTeam1, '
                                                'compile-only); copy-out kept there; the product codec is in/out')
                else:
                    cls, why = 'fixed by in/out (0380)', 'proof generator only; proxy-design 31.1 fault 1'
                rows.append(('gen_proxy', e['direction'], e['name'], x['m']['key'], p['name'], p['type'], p['codec'],
                             'yes' if has_in else 'no', cls, why))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', help='write the TSV here (default: stdout)')
    a = ap.parse_args()
    head = ('generator', 'direction', 'interface', 'method', 'parameter', 'type', 'codec', 'in_half', 'class', 'reason')
    rows = product_rows() + proof_rows()
    text = '\n'.join('\t'.join(r) for r in [head] + rows) + '\n'
    if a.out:
        with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    counts = {}
    for r in rows:
        counts[(r[0], r[8])] = counts.get((r[0], r[8]), 0) + 1
    for k in sorted(counts):
        print('%s: %s: %d' % (k[0], k[1], counts[k]), file=sys.stderr)
    return 1 if any(r[8] in ('COPY-OUT', 'not reviewed') for r in rows if r[0] == 'gen_runtime') else 0


if __name__ == '__main__':
    sys.exit(main())
