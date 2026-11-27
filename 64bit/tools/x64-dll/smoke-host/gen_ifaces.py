"""Generate the smoke host's logging implementations of the engine-side interfaces (issue 1007).

Reads the interface classes from the DLL fork's headers and writes `<out>/host_ifaces.gen.h` with, per host
interface:
  * a table of every pure virtual in declaration order (declaring class, name, declaration text),
  * a class `<Host>Gen` deriving from the interface that overrides every pure virtual with a logging body
    (`HostIfaceHit(iface, index)`, then `HostDefault<R>::Get()`), except the methods listed in CUSTOM, which
    stay pure and are written by hand in the host,
  * a template `<Host>ResolveSlots<T>(int*)` that takes `&T::method` for every method and decodes the vtable
    slot from the MS-ABI vcall thunk (`HostSlotOf`), so the report names slots as the compiler laid them out.

The derived class is typed (real return types), so float returns, CvString returned by hidden pointer and
reference returns are all correct on x64, which an untyped "return 0" thunk would not be.

Usage: python gen_ifaces.py --src <fork checkout> --out <dir>
"""
import argparse
import re
import sys
from pathlib import Path

# (host iface enum, host class, interface chain base-first: (header, class))
IFACES = [
    ('HI_ENGINE', 'HostEngineUtility', 'ICvEngineUtility4', [
        (r'CvGameCoreDLLUtil\include\CvDllInterfaces.h', 'ICvUnknown'),
        (r'CvGameCoreDLLUtil\include\ICvDLLUtility.h', 'ICvEngineUtility1'),
        (r'CvGameCoreDLLUtil\include\ICvDLLUtility.h', 'ICvEngineUtility2'),
        (r'CvGameCoreDLLUtil\include\ICvDLLUtility.h', 'ICvEngineUtility3'),
        (r'CvGameCoreDLLUtil\include\ICvDLLUtility.h', 'ICvEngineUtility4'),
    ]),
    ('HI_UI', 'HostUserInterface', 'ICvUserInterface2', [
        (r'CvGameCoreDLLUtil\include\CvDllInterfaces.h', 'ICvUnknown'),
        (r'CvGameCoreDLLUtil\include\ICvDLLUserInterface.h', 'ICvUserInterface1'),
        (r'CvGameCoreDLLUtil\include\ICvDLLUserInterface.h', 'ICvUserInterface2'),
    ]),
    ('HI_SCRIPT', 'HostScriptSystem', 'ICvEngineScriptSystem1', [
        (r'CvGameCoreDLLUtil\include\ICvDLLScriptSystem.h', 'ICvEngineScriptSystem1'),
    ]),
    ('HI_FILESYS', 'HostFileSystem', 'FIFileSystem', [
        (r'FirePlace\include\FireWorks\FIFile.h', 'FIFileSystem'),
    ]),
    ('HI_LOGMGR', 'HostLogFileMgr', 'FILogFileMgr', [
        (r'FirePlace\include\FireWorks\FILogFile.h', 'FILogFileMgr'),
    ]),
    ('HI_LOGFILE', 'HostLogFile', 'FILogFile', [
        (r'FirePlace\include\FireWorks\FILogFile.h', 'FILogFile'),
    ]),
]

# Methods the host writes by hand (left pure in <Host>Gen). Key: (host class, method name); all overloads.
CUSTOM = {
    ('HostEngineUtility', 'QueryInterface'), ('HostEngineUtility', 'Destroy'),
    ('HostEngineUtility', 'GetScriptSystem'), ('HostEngineUtility', 'GetCacheFolderPath'),
    ('HostUserInterface', 'QueryInterface'), ('HostUserInterface', 'Destroy'),
    ('HostScriptSystem', 'CreateLuaThread'), ('HostScriptSystem', 'FreeLuaThread'),
    ('HostLogFileMgr', 'GetLog'), ('HostLogFileMgr', 'IsLoggingEnabled'),
    ('HostLogFile', 'Msg'), ('HostLogFile', 'DebugMsg'), ('HostLogFile', 'WarningMsg'),
    ('HostLogFile', 'ErrorMsg'), ('HostLogFile', 'GetFileName'),
}

DROP_TOKENS = {'DECLSPEC_DEPRECATED', '__checkReturn', '_Ret_maybenull_', '_Ret_notnull_'}


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    text = re.sub(r'^\s*#[^\n]*', ' ', text, flags=re.M)
    return text


def class_body(text: str, name: str) -> str:
    m = re.search(r'\bclass\s+' + re.escape(name) + r'\b[^;{]*\{', text)
    if not m:
        raise RuntimeError(f'class {name} not found')
    i = m.end()
    depth = 1
    j = i
    while depth:
        c = text[j]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        j += 1
    return text[i:j - 1]


def statements(body: str):
    """Top-level statements of a class body; nested {...} blocks (inline bodies, enums) are dropped."""
    out, cur, i = [], [], 0
    while i < len(body):
        c = body[i]
        if c == '{':
            depth, i = 1, i + 1
            while depth and i < len(body):
                depth += {'{': 1, '}': -1}.get(body[i], 0)
                i += 1
            out.append(''.join(cur))
            cur = []
            continue
        if c == ';':
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    return [' '.join(s.split()) for s in out if s.strip()]


def split_params(params: str):
    parts, depth, cur = [], 0, []
    for c in params:
        if c in '(<[':
            depth += 1
        elif c in ')>]':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if ''.join(cur).strip():
        parts.append(''.join(cur).strip())
    return parts


def strip_default(p: str) -> str:
    depth = 0
    for i, c in enumerate(p):
        if c in '(<[':
            depth += 1
        elif c in ')>]':
            depth -= 1
        elif c == '=' and depth == 0:
            return p[:i].strip()
    return p


def parse_virtual(stmt: str):
    s = re.sub(r'^(public|protected|private)\s*:\s*', '', stmt)
    if 'virtual' not in s.split() or not re.search(r'=\s*0\s*$', s):
        return None
    s = re.sub(r'=\s*0\s*$', '', s).strip()
    pre, post = s.split('virtual', 1)
    # SAL tokens before 'virtual' are dropped (_Ret_maybenull_ etc.)
    s = post.strip()
    # the parameter list: the last top-level (...) group
    depth, close = 0, None
    for i in range(len(s) - 1, -1, -1):
        if s[i] == ')':
            if depth == 0:
                close = i
            depth += 1
        elif s[i] == '(':
            depth -= 1
            if depth == 0:
                open_ = i
                break
    head, params, tail = s[:open_].strip(), s[open_ + 1:close].strip(), s[close + 1:].strip()
    m = re.match(r'^(.*?)(~?\w+)$', head)
    ret_cc, name = m.group(1).strip(), m.group(2)
    if name.startswith('~'):
        return None  # destructors: the derived class's implicit destructor overrides them
    toks = [t for t in ret_cc.split() if t not in DROP_TOKENS]
    callconv = ''
    if toks and toks[-1] in ('DLLCALL', '__stdcall', '__cdecl'):
        callconv = toks.pop()
    ret = ' '.join(toks)
    is_const = tail == 'const'
    if params == 'void':
        params = ''
    plist = split_params(params)
    return {
        'name': name, 'ret': ret, 'cc': callconv, 'params': params, 'const': is_const,
        'params_nodef': ', '.join(strip_default(p) for p in plist),
        'decl': ' '.join((ret + ' ' + (callconv + ' ' if callconv else '') + name + '(' + params + ')'
                          + (' const' if is_const else '')).split()),
    }


def cstr(s: str) -> str:
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def generate(src: Path) -> str:
    cache = {}
    out = ['// Generated by tools/x64-dll/smoke-host/gen_ifaces.py from the DLL fork headers. Do not edit.',
           f'// Source: {src}', '#pragma once', '']
    for enum, host, iface, chain in IFACES:
        methods = []
        typedefs = {}
        for header, cls in chain:
            path = src / header
            if path not in cache:
                cache[path] = strip_comments(path.read_text(encoding='latin-1'))
            for st in statements(class_body(cache[path], cls)):
                td = re.match(r'^(?:(?:public|protected|private)\s*:\s*)?typedef\b.*?(\w+)$', st)
                if td:
                    typedefs[td.group(1)] = cls
                v = parse_virtual(st)
                if v:
                    v['cls'] = cls
                    methods.append(v)
        out.append(f'// ---- {host}: {iface}, {len(methods)} pure virtuals ----')
        out.append(f'static const HostMethodInfo s_a{host}Methods[{len(methods)}] = {{')
        for m in methods:
            out.append(f'\t{{ {cstr(m["cls"])}, {cstr(m["name"])}, {cstr(m["decl"])} }},')
        out.append('};')
        out.append(f'enum {{ {host}_NUM_METHODS = {len(methods)} }};')
        seen = set()
        for k, m in enumerate(methods):
            if m['name'] not in seen:
                out.append(f'enum {{ HM_{host}_{m["name"]} = {k} }};')
                seen.add(m['name'])
        out.append(f'class {host}Gen : public {iface}')
        out.append('{')
        out.append('public:')
        for k, m in enumerate(methods):
            if (host, m['name']) in CUSTOM:
                continue
            cc = (m['cc'] + ' ') if m['cc'] else ''
            const = ' const' if m['const'] else ''
            out.append(f'\t{m["ret"]} {cc}{m["name"]}({m["params"]}){const} override '
                       f'{{ HostIfaceHit({enum}, {k}); return HostDefault<{m["ret"]}>::Get(); }}')
        out.append('};')
        out.append(f'template<class T> void {host}ResolveSlots(int* piSlots)')
        out.append('{')
        for k, m in enumerate(methods):
            cc = (m['cc'] + ' ') if m['cc'] else ''
            const = ' const' if m['const'] else ''
            for td, tcls in typedefs.items():  # class-scope typedefs: the resolver is not a member
                m['params_nodef'] = re.sub(r'(?<!::)\b' + td + r'\b', tcls + '::' + td, m['params_nodef'])
            out.append(f'\tpiSlots[{k}] = HostSlotOf(static_cast<{m["ret"]} ({cc}T::*)({m["params_nodef"]}){const}>'
                       f'(&T::{m["name"]}));')
        out.append('}')
        out.append('')
    return '\n'.join(out) + '\n'


def main():
    import importlib.util
    spec = importlib.util.spec_from_file_location('x64build', Path(__file__).resolve().parents[1] / 'build.py')
    x64build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(x64build)
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source, checked against deps/dll.lock (build.py resolve_src); default '
                         f'{x64build.DEFAULT_SRC} (from {x64build.DEFAULT_SRC_ORIGIN}; dll_source.py) if it is at the '
                         'locked commit')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    try:
        a.src = x64build.resolve_src(a.src, a.unlocked)['src']
    except x64build.SrcError as e:
        sys.exit(f'gen_ifaces: FAILED: {e}')
    a.out.mkdir(parents=True, exist_ok=True)
    text = generate(a.src)
    (a.out / 'host_ifaces.gen.h').write_text(text, encoding='utf-8')
    print(f'wrote {a.out / "host_ifaces.gen.h"}')


if __name__ == '__main__':
    main()
