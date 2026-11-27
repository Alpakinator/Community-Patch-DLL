"""Extract the EXE<->DLL interface model from the pinned DLL fork by driving clang (epic 2100, task 2101).

What it does (all outputs under BUILD_ROOT\\model, the model into proxy/model/interfaces.json):
 1. Writes a probe TU that includes the DLL's PCH header, every CvDll*.h wrapper header and the FireWorks
    interface headers, and for every polymorphic interface class a derived class with an out-of-line
    constructor (that forces the compiler to lay out and emit the vftable).
 2. x86 (VC9 environment, the DLL's own flags): clang AST JSON, filtered per name family, gives every class,
    its bases and every virtual method with return type, parameter types and names, const-ness, pure-ness and
    declaration line.
 3. x86 and x64: -fdump-vtable-layouts gives each virtual's vftable slot as the compiler lays it out
    ("VFTable indices" blocks); -fdump-record-layouts gives size and field offsets of every record type that
    crosses the boundary by value or by reference.
 4. Joins the three, decides each interface's direction (implemented by a CvDll* wrapper = engine->DLL,
    otherwise DLL->engine), and writes interfaces.json.
Usage: python -I proxy/gen/extract_model.py --src <fork source> [--lock F | --unlocked --commit C] [--work DIR]
                                            [--out-dir DIR]
  --src is checked by toolchain.resolve_source (issue 3503: a git checkout at the lock commit or a plain copy with the
  lock's source fingerprint; exit 2 otherwise); without --src the old fixed clone toolchain.FORK_DEFAULT is read
  unchecked (as before 3503). --out-dir: where interfaces.json goes (default proxy/model); --work: compiler scratch
  (default toolchain.BUILD_ROOT\\model).
"""
import argparse
import json
import os
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolchain as tc  # noqa: E402

OUT = os.path.join(tc.BUILD_ROOT, 'model')
DEST = os.path.join(tc.PROXY, 'model', 'interfaces.json')
EXP2 = os.path.join(tc.FORK, 'CvGameCoreDLL_Expansion2')

# Name families for -ast-dump-filter (substring of the qualified name).
FILTERS = ['ICv', 'FIFileSystem', 'FILogFile', 'CvDLLInterfaceIFaceBase', 'CvDll']
# Engine-implemented classes that are not ICv*: what the DLL reaches through gDLL / host exports.
ENGINE_EXTRA = ['FIFileSystem', 'FILogFileMgr', 'FILogFile', 'CvDLLInterfaceIFaceBase']


def log(*a):
    print(*a, flush=True)


def probe_headers():
    hdrs = sorted(f for f in os.listdir(EXP2) if re.match(r'CvDll\w+\.h$', f))
    lines = ['#include "CvGameCoreDLLPCH.h"', '#include "ICvDLLDatabaseUtility.h"',
             '#include <FireWorks/FIFile.h>', '#include <FireWorks/FILogFile.h>']
    lines += ['#include "%s"' % h for h in hdrs]
    return lines


# ---------------------------------------------------------------------------------------------- AST
def iter_json_objects(text):
    dec = json.JSONDecoder()
    i, n = 0, len(text)
    while i < n:
        while i < n and text[i] in ' \r\n\t':
            i += 1
        if i >= n:
            break
        obj, j = dec.raw_decode(text, i)
        yield obj
        i = j


class LocTracker:
    """Clang's JSON omits file/line when unchanged from the previous node: track them in document order."""

    def __init__(self):
        self.file = None
        self.line = None

    def see(self, loc):
        if not loc:
            return
        if 'spellingLoc' in loc:
            loc = loc['expansionLoc'] if 'expansionLoc' in loc else loc['spellingLoc']
        if 'file' in loc:
            self.file = loc['file']
        if 'line' in loc:
            self.line = loc['line']


def walk_record(node, trk, classes):
    trk.see(node.get('loc'))
    if node.get('kind') != 'CXXRecordDecl' or not node.get('completeDefinition') or not node.get('name'):
        for ch in node.get('inner', []) or []:
            if isinstance(ch, dict):
                walk_record_any(ch, trk)
        return
    name = node['name']
    rec = {'name': name, 'tag': node.get('tagUsed'), 'file': tc.rel(trk.file or ''), 'line': trk.line,
           'bases': [b['type']['qualType'] for b in node.get('bases', [])], 'methods': []}
    for ch in node.get('inner', []) or []:
        if not isinstance(ch, dict):
            continue
        trk.see(ch.get('loc'))
        mline = trk.line
        if ch.get('kind') == 'CXXMethodDecl' and ch.get('virtual'):
            params = []
            for p in ch.get('inner', []) or []:
                trk.see(p.get('loc'))
                if p.get('kind') == 'ParmVarDecl':
                    params.append({'name': p.get('name', ''), 'type': p['type']['qualType'],
                                   'default': any(isinstance(x, dict) for x in p.get('inner', []) or [])})
                else:
                    walk_record_any(p, trk)
            ftype = ch['type']['qualType']
            ret = ftype.split('(')[0].strip()
            rec['methods'].append({'name': ch['name'], 'ftype': ftype, 'ret': ret, 'params': params,
                                   'const': bool(re.search(r'\)\s*const\s*$', re.sub(
                                       r'\s*__attribute__\(\(\w+\)\)', '', ftype))),
                                   'variadic': ftype.rstrip().endswith('...)') or '...)' in ftype,
                                   'callconv': ('stdcall' if 'stdcall' in ftype else 'cdecl' if
                                                'cdecl' in ftype or '...)' in ftype else 'thiscall'),
                                   'pure': bool(ch.get('pure')), 'line': mline})
        elif ch.get('kind') == 'CXXDestructorDecl' and ch.get('virtual'):
            rec['methods'].append({'name': '~' + name, 'ftype': 'dtor', 'ret': 'void', 'params': [],
                                   'const': False, 'variadic': False, 'pure': bool(ch.get('pure')),
                                   'line': mline, 'dtor': True})
        else:
            walk_record_any(ch, trk)
    classes.setdefault(name, rec)


def walk_record_any(node, trk):
    trk.see(node.get('loc'))
    for ch in node.get('inner', []) or []:
        if isinstance(ch, dict):
            walk_record_any(ch, trk)


def ast_dump(flt):
    src = os.path.join(OUT, 'probe_ast.cpp')
    cp = tc.clang('x86', src, ['-fsyntax-only', '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter=' + flt],
                  cwd=OUT)
    if cp.returncode != 0:
        raise RuntimeError('AST dump %s failed: %s' % (flt, cp.stderr[-2000:]))
    classes = {}
    trk = LocTracker()
    for obj in iter_json_objects(cp.stdout):
        walk_record(obj, trk, classes)
    return flt, classes, len(cp.stdout)


# ---------------------------------------------------------------------------------------------- vtables
RE_VFT_IDX = re.compile(r"^VFTable indices for '([^']+)' \((\d+) entr")
RE_VFT_ROW = re.compile(r'^\s*(\d+) \| (.*)$')
RE_REC_HDR = re.compile(r'^\*\*\* Dumping AST Record Layout')


def vt_dump(arch, wanted_records):
    src = os.path.join(OUT, 'probe_vt.cpp')
    obj = os.path.join(OUT, 'probe_vt_%s.obj' % arch)
    cp = tc.clang(arch, src, ['/c', '/Fo' + obj, '-Xclang', '-fdump-vtable-layouts', '-Xclang',
                              '-fdump-record-layouts'], cwd=OUT)
    if cp.returncode != 0:
        raise RuntimeError('vtable dump %s failed: %s' % (arch, cp.stderr[-2000:]))
    text = cp.stdout
    with open(os.path.join(OUT, 'vtables_%s.txt' % arch), 'w', encoding='utf-8') as f:
        # keep only the vftable part on disk (record layouts are large)
        keep = []
        on = False
        for line in text.splitlines():
            if line.startswith('VFTable'):
                on = True
            elif RE_REC_HDR.match(line):
                on = False
            if on:
                keep.append(line)
        f.write('\n'.join(keep))
    slots = {}
    cur = None
    for line in text.splitlines():
        m = RE_VFT_IDX.match(line)
        if m:
            cur = slots.setdefault(m.group(1), [])
            continue
        if cur is not None:
            m = RE_VFT_ROW.match(line)
            if m:
                cur.append((int(m.group(1)), m.group(2).strip()))
                continue
            cur = None
    records = parse_records(text, wanted_records)
    return arch, slots, records


def parse_records(text, wanted):
    """Return {record name: {'size','align','fields':[(offset, text)]}} for wanted names (top-level dumps)."""
    out = {}
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        if RE_REC_HDR.match(lines[i]):
            j = i + 1
            m = re.match(r'^\s*0 \| (?:class|struct|union) (.+?)\s*$', lines[j]) if j < len(lines) else None
            name = m.group(1) if m else None
            body = []
            j += 1
            size = align = None
            while j < len(lines) and not RE_REC_HDR.match(lines[j]) and not lines[j].startswith('VFTable'):
                s = re.search(r'\[sizeof=(\d+), (?:dsize=\d+, )?align=(\d+)', lines[j])
                if s:
                    size, align = int(s.group(1)), int(s.group(2))
                    j += 1
                    break
                body.append(lines[j])
                j += 1
            if name and name in wanted and name not in out:
                fields = []
                for b in body:
                    fm = re.match(r'^\s*(\d+) \|(\s+)(.*)$', b)
                    if fm and len(fm.group(2)) <= 3:  # direct members of the record only
                        fields.append([int(fm.group(1)), fm.group(3).strip()])
                out[name] = {'size': size, 'align': align, 'fields': fields}
            i = j
        else:
            i += 1
    return out


def norm_sig(s):
    s = re.sub(r'\s*\[(pure|scalar deleting|vector deleting)\]', '', s)
    s = re.sub(r'__attribute__\(\(\w+\)\)\s*', '', s)
    s = s.replace('__stdcall ', '').replace('__thiscall ', '')
    return re.sub(r'\s+', ' ', s).strip()


def method_key(cls, m):
    ps = ', '.join(p['type'] for p in m['params'])
    if m['variadic']:
        ps = (ps + ', ...') if ps else '...'
    return '%s::%s(%s)%s' % (cls, m['name'], ps, ' const' if m['const'] else '')


def dump_sig_key(sig):
    """'ICvPlayer1 *ICvGame1::GetPlayer(const PlayerTypes) const' -> 'ICvGame1::GetPlayer(const PlayerTypes) const'"""
    sig = norm_sig(sig)
    m = re.search(r'(\w+(?:::\w+)*::~?\w+)\((.*)\)( const)?(?: &| &&)?$', sig)
    if not m:
        return sig
    return '%s(%s)%s' % (m.group(1), m.group(2), m.group(3) or '')


# ---------------------------------------------------------------------------------------------- main
def configure(argv=None):
    """Parse the options; point OUT, DEST, EXP2 and the toolchain at --src. Returns 0 or 2 (refused)."""
    global OUT, DEST, EXP2
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    tc.add_source_args(ap, required=False)
    ap.add_argument('--out-dir', default=os.path.dirname(DEST))
    a = ap.parse_args(argv)
    if a.src:
        try:
            info = tc.resolve_source(a.src, a.lock, a.unlocked, a.commit)
        except tc.SourceError as ex:
            log('extract_model: --src refused: %s' % ex)
            return 2
        tc.use_source(info, a.work)
        log('extract_model: source %s (%s)' % (info['src'], info['note']))
    OUT = os.path.join(a.work, 'model') if a.work else os.path.join(tc.BUILD_ROOT, 'model')
    DEST = os.path.join(os.path.abspath(a.out_dir), 'interfaces.json')
    EXP2 = os.path.join(tc.FORK, 'CvGameCoreDLL_Expansion2')
    return 0


def main(argv=None):
    rc = configure(argv)
    if rc:
        return rc
    t0 = time.time()
    os.makedirs(OUT, exist_ok=True)
    hdr = probe_headers()
    with open(os.path.join(OUT, 'probe_ast.cpp'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(hdr) + '\n')
    with ThreadPoolExecutor(max_workers=len(FILTERS)) as ex:
        results = list(ex.map(ast_dump, FILTERS))
    classes = {}
    for flt, cl, nbytes in results:
        log('AST filter %-26s %4d records, %6.1f MB' % (flt, len(cl), nbytes / 1e6))
        for k, v in cl.items():
            classes.setdefault(k, v)

    def is_iface(n):
        c = classes.get(n)
        return c is not None and (n.startswith('ICv') or n in ENGINE_EXTRA) and             c['file'].startswith(('CvGameCoreDLLUtil/', 'FirePlace/')) and \
            (c['methods'] or any(is_iface(b) for b in c['bases']))

    ifaces = sorted(n for n in classes if is_iface(n))
    wrappers = sorted(n for n in classes if n.startswith('CvDll') and classes[n]['bases'])

    # The chain of an interface (base first), single inheritance in every ICv* interface.
    def chain(n):
        out = []
        while n:
            out.insert(0, n)
            bs = [b for b in classes[n]['bases'] if b in classes]
            if len(bs) > 1:
                raise RuntimeError('multiple inheritance in interface %s' % n)
            n = bs[0] if bs else None
        return out

    # Types whose layout matters: by-value / by-reference class types in signatures.
    wanted = set()
    for n in ifaces:
        for m in classes[n]['methods']:
            for t in [m['ret']] + [p['type'] for p in m['params']]:
                base = re.sub(r'\b(const|volatile|struct|class|enum)\b', '', t).replace('&', '').replace('*', '').strip()
                if base and base[0].isupper() and not base.startswith('ICv') and '::' not in base:
                    wanted.add(base)
    wanted |= {'CvString', 'GUID', 'FAutoArchive', 'FDataStream', 'CvPopupInfo', 'FStringA', 'FStringW',
               'CvWorldBuilderMap', 'CvReplayInfo', 'CvAStar', 'CvPlotIndicatorData'}
    vt = ['\n'.join(hdr)]
    for n in ifaces:
        vt.append('struct P_%s : %s { P_%s(); };\nP_%s::P_%s() {}' % (n, n, n, n, n))
    sz = []
    for i, t in enumerate(sorted(wanted)):
        sz.append('#if __has_include("CvGameCoreDLLPCH.h")\nint g_sz_%d = (int)sizeof(::%s);\n#endif' % (i, t))
    with open(os.path.join(OUT, 'probe_vt.cpp'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(vt) + '\n')
    # sizeof probes go in a separate pass: unknown names are dropped one by one until the TU compiles.
    with ThreadPoolExecutor(max_workers=2) as ex:
        vres = dict((a, (s, r)) for a, s, r in ex.map(lambda a: vt_dump(a, wanted), ['x86', 'x64']))

    model_ifaces = []
    unmatched = []
    impl_by_iface = {}
    for w in wrappers:
        for b in classes[w]['bases']:
            if b in classes and is_iface(b):
                for c in chain(b):
                    impl_by_iface.setdefault(c, set()).add(w)
    for n in ifaces:
        ch = chain(n)
        methods = []
        for c in ch:
            for m in classes[c]['methods']:
                methods.append((c, m))
        entry = {'name': n, 'kind': classes[n]['tag'], 'file': classes[n]['file'], 'line': classes[n]['line'],
                 'chain': ch, 'implemented_by': sorted(impl_by_iface.get(n, [])),
                 'direction': 'engine_to_dll' if impl_by_iface.get(n) else 'dll_to_engine',
                 'own_virtuals': len(classes[n]['methods']), 'virtuals': len(methods), 'methods': []}
        for arch in ('x86', 'x64'):
            # slots: union of the 'VFTable indices' blocks of the chain
            idx = {}
            for c in ch:
                for slot, sig in vres[arch][0].get(c, []):
                    idx.setdefault(dump_sig_key(sig), []).append(slot)
            entry['_idx_' + arch] = idx
        for decl_index, (c, m) in enumerate(methods):
            key = method_key(c, m) if not m.get('dtor') else '%s::~%s()' % (c, c)
            me = {'name': m['name'], 'declared_in': c, 'decl_index': decl_index, 'line': m['line'],
                  'ret': m['ret'], 'params': m['params'], 'const': m['const'], 'variadic': m['variadic'],
                  'callconv': m.get('callconv', 'thiscall'),
                  'pure': m['pure'], 'key': key, 'slot': {}}
            for arch in ('x86', 'x64'):
                s = entry['_idx_' + arch].get(key)
                if s is None:
                    unmatched.append((arch, n, key))
                else:
                    me['slot'][arch] = s[0]
            entry['methods'].append(me)
        for arch in ('x86', 'x64'):
            del entry['_idx_' + arch]
        entry['slot_order_differs_from_declaration'] = any(
            me['slot'].get('x86') != me['decl_index'] for me in entry['methods'])
        entry['slots_equal_x86_x64'] = all(me['slot'].get('x86') == me['slot'].get('x64') for me in entry['methods'])
        model_ifaces.append(entry)

    recs = {}
    for arch in ('x86', 'x64'):
        for k, v in vres[arch][1].items():
            recs.setdefault(k, {})[arch] = v
    out = {
        'generated_by': 'proxy/gen/extract_model.py',
        'generated_at': time.strftime('%Y-%m-%dT%H:%M:%S'),
        'source': {'fork_path': tc.FORK.replace('\\', '/'), 'commit': tc.PINNED_COMMIT},
        'toolchains': {'x86': 'clang-cl 19.1.7 -m32, VS 2008 (VC9) headers via vsvars32.bat, DLL release defines',
                       'x64': 'clang-cl 19.1.7 -m64, MSVC 14.4x STL via vcvars64.bat, /std:c++14, VP_OUT_OF_PROCESS'},
        'slot_method': 'clang -Xclang -fdump-vtable-layouts, "VFTable indices" blocks, per arch [C]',
        'interfaces': model_ifaces,
        'wrappers': {w: {'bases': classes[w]['bases'], 'file': classes[w]['file']} for w in wrappers},
        'records': recs,
        'unmatched_slots': unmatched,
    }
    os.makedirs(os.path.dirname(DEST), exist_ok=True)
    with open(DEST, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(out, f, indent=1, ensure_ascii=True)
    nvirt = sum(e['virtuals'] for e in model_ifaces)
    nown = sum(e['own_virtuals'] for e in model_ifaces)
    log('interfaces %d (engine->DLL %d, DLL->engine %d); virtuals own %d, with inherited %d; wrappers %d' % (
        len(model_ifaces), sum(e['direction'] == 'engine_to_dll' for e in model_ifaces),
        sum(e['direction'] == 'dll_to_engine' for e in model_ifaces), nown, nvirt, len(wrappers)))
    log('records with layouts: %d of %d wanted; unmatched slots: %d' % (len(recs), len(wanted), len(unmatched)))
    for u in unmatched[:20]:
        log('  unmatched', u)
    log('wrote %s in %.1f s' % (DEST, time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
