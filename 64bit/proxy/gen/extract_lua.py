#!/usr/bin/env python
"""Regenerate proxy/model/lua.json: inventory of every Lua method the DLL registers.

Usage:  python -I proxy/gen/extract_lua.py --src <fork source> [--lock F | --unlocked --commit C] [--work DIR]
                                           [--out-dir DIR]
        --src as extract_model.py (issue 3503: checked by proxy/gen/toolchain.py resolve_source, exit 2 when refused;
        without it toolchain.FORK: VP_PROXY_GEN_SRC, else the one DLL source setting of tools/x64-dll/dll_source.py,
        unchecked; ticket 4708). The source reaches the worker processes through the environment
        (VP_PROXY_GEN_SRC, VP_PROXY_GEN_COMMIT). --out-dir: where lua.json goes (default proxy/model).

What it does (stdlib only):
  1. Preprocesses every CvGameCoreDLL_Expansion2/Lua/CvLua*.cpp with clang-cl /E in the DLL's x86
     configuration (VS2008 headers via vsvars32) and in the x64 server configuration (VS2022 via
     vcvars64).  Registrations are read from the macro-expanded text of lines that come from the
     Lua sources themselves (GNU '# N "file"' line markers), so only compiled registrations count.
     The same output also tells which #if segments of each Lua source are active.
  2. Dumps the clang AST (text form, x86 configuration) of each Lua TU and streams it: builds the
     table of every C++ member function declared in the TU (class, name, signature, const, static)
     and resolves every member call / function reference inside the CvLua* method bodies.
  3. Parses each C function body (active lines only, comments stripped) for the Lua-stack shape:
     arguments read, results pushed, special shapes.  This part is textual and marked [I].
  4. Joins measured traffic from research/sources/boundary-benchmark/results.json.
  5. Writes proxy/model/lua.json (indent=1, ensure_ascii, sorted by id).

Build outputs go to <toolchain.BUILD_ROOT>/lua (--work, else VP_PROXY_GEN_WORK, else <temp>/claude/team2100);
only the small filtered preprocessor files are kept there.
"""
import collections
import concurrent.futures
import datetime
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import toolchain  # noqa: E402

# Issue 3503: the source comes from --src (toolchain.use_source puts it into the environment, which the worker
# processes of step 2 read at import). Ticket 4708: without --src toolchain's FORK (VP_PROXY_GEN_SRC, else the one DLL
# source setting of tools/x64-dll/dll_source.py) and its scratch root, no path of this file's own.
FORK = toolchain.FORK
PINNED_COMMIT = toolchain.PINNED_COMMIT
LUA_DIR = os.path.join(FORK, r'CvGameCoreDLL_Expansion2\Lua')
OUT = os.path.join(toolchain.BUILD_ROOT, 'lua')
BENCH = os.path.join(REPO, r'research\sources\boundary-benchmark\results.json')
DEST = os.path.join(REPO, r'proxy\model\lua.json')

CLANG = r'"C:\Program Files\LLVM\bin\clang-cl.exe"'
ENV = {
    'x86': r'"C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat"',
    'x64': r'"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"',
}
INCLUDES = ['CvGameCoreDLL_Expansion2', r'CvWorldBuilderMap\include', r'CvGameCoreDLLUtil\include',
            r'CvLocalization\include', r'CvGameDatabase\include', r'FirePlace\include',
            r'FirePlace\include\FireWorks', r'ThirdPartyLibs\Lua51\include']
DEFINES = ['FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'CVGAMECOREDLL_EXPORTS',
           'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS', '_WINDLL', 'NDEBUG', 'STRONG_ASSUMPTIONS',
           'VPRELEASE_ERRORMSG']
ARCH_ARGS = {
    'x86': ['-m32'],
    'x64': ['-m64', '/std:c++14', '/D_HAS_AUTO_PTR_ETC=1', '/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS',
            '/DVP_OUT_OF_PROCESS'],
}

# Lua type name (the Lua-side library / instance table) per C++ wrapper class
LIBS = {
    'CvLuaArea': ('Area', 'CvArea', 'instance'),
    'CvLuaCity': ('City', 'CvCity', 'instance'),
    'CvLuaDeal': ('Deal', 'CvDeal', 'instance'),
    'CvLuaGame': ('Game', 'CvGame', 'static'),
    'CvLuaLeague': ('League', 'CvLeague', 'instance'),
    'CvLuaMap': ('Map', 'CvMap', 'static'),
    'CvLuaPlayer': ('Player', 'CvPlayerAI', 'instance'),
    'CvLuaPlot': ('Plot', 'CvPlot', 'instance'),
    'CvLuaTeam': ('Team', 'CvTeam', 'instance'),
    'CvLuaTeamTech': ('TeamTech', 'CvTeamTechs', 'instance'),
    'CvLuaUnit': ('Unit', 'CvUnit', 'instance'),
    'CvLuaFractal': ('Fractal', 'CvFractal', 'fractal'),
    # Issue 3519: new on upstream master (PR 13379); listed only for a source that has the file
    'CvLuaExe': ('Exe', None, 'static'),
}
LIB_GLOBALS = {
    'Area': 'LuaTypes.Area (instances from Map/Plot/Unit/City getters)',
    'City': 'LuaTypes.City (instances pushed by getters)',
    'Deal': 'LuaTypes.Deal (UI.GetScratchDeal etc.)',
    'Game': 'Game (global table)',
    'League': 'LuaTypes.League (Game.GetLeague...)',
    'Map': 'Map (global table)',
    'Player': 'Players[i] (pre-pushed for all MAX_PLAYERS), LuaTypes.Player, Player',
    'Plot': 'LuaTypes.Plot (Map.GetPlot...)',
    'Team': 'Teams[i] (pre-pushed), LuaTypes.Team, Team',
    'TeamTech': 'LuaTypes.TeamTech (Team:GetTeamTechs())',
    'Unit': 'LuaTypes.Unit (instances pushed by getters)',
    'Fractal': 'Fractal (global table) + per-object metatable on a Lua userdata',
    'Exe': 'Exe (global table; functions that call the running EXE, CvGameCoreDLL_Expansion2/Exe/)',
}
# Issue 3519: on master (#13489) Game and Map are full userdata with a metatable whose __index is the function table,
# instances are full userdata (LuaTaggedAutoPointer), and the type tables live in the Lua registry (vpdllinternals,
# keyed by type tag) instead of LuaTypes; GamecoreInit.lua (run on the DLL's own Lua thread) copies them into LuaTypes.
LIB_GLOBALS_MASTER = {
    'Game': 'Game (global full userdata; metatable __index = the function table; CvLuaStaticInstance)',
    'Map': 'Map (global full userdata; metatable __index = the function table; CvLuaStaticInstance)',
    'Player': 'Players[i] (pre-pushed full userdata for all MAX_PLAYERS, CvLuaPlayer::CustomRegister); type tables in '
              'the registry (vpdllinternals)',
    'Team': 'Teams[i] (pre-pushed full userdata, CvLuaTeam::CustomRegister); type tables in the registry',
}


def log(*a):
    print(*a, flush=True)


def rel(path):
    p = os.path.normpath(path)
    for root in (FORK, REPO):
        if p.lower().startswith(root.lower() + os.sep):
            return p[len(root) + 1:].replace('\\', '/')
    return p.replace('\\', '/')


def clang_cmd(arch, src, extra):
    args = ARCH_ARGS[arch] + ['/MD', '/EHsc', '/Zc:wchar_t', '-msse3', '-Wno-everything'] + \
        ['/D' + d for d in DEFINES] + ['/I' + os.path.join(FORK, i) for i in INCLUDES] + extra
    return 'cmd /d /s /c "%s >NUL && %s %s "%s""' % (ENV[arch], CLANG, ' '.join('"%s"' % a for a in args), src)


def clang_env():
    """Environment for a clang_cmd() process. VSCMD_SKIP_SENDTELEMETRY: vcvars otherwise starts a background
    PowerShell for its telemetry (bug 0108; issue 0147). vsvars32.bat (x86, VS 2008) ignores it."""
    return dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')


# ----------------------------------------------------------------------------------------------
# 1. preprocessing
# ----------------------------------------------------------------------------------------------
MARK = re.compile(r'^#\s*(\d+)\s+"((?:[^"\\]|\\.)*)"')


def preprocess(arch, src):
    """Run clang-cl /E; return {abs_lua_file_lower: {line: text}} for lines from the Lua dir."""
    cp = subprocess.run(clang_cmd(arch, src, ['/E']), stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=clang_env())
    if cp.returncode != 0:
        raise RuntimeError('clang /E failed for %s %s: %s' % (arch, src, cp.stderr[-2000:]))
    text = cp.stdout.decode('utf-8', 'replace')
    luadir = LUA_DIR.lower()
    res = collections.defaultdict(dict)
    cur, keep, ln = None, False, 0
    for line in text.split('\n'):
        m = MARK.match(line)
        if m:
            ln = int(m.group(1))
            cur = os.path.normpath(m.group(2).replace('\\\\', '\\')).lower()
            keep = os.path.dirname(cur) == luadir
            continue
        if keep and line.strip():
            res[cur][ln] = line.rstrip('\r')
        ln += 1
    size = len(cp.stdout)
    return dict(res), size


# ----------------------------------------------------------------------------------------------
# 2. AST stream (runs in worker processes)
# ----------------------------------------------------------------------------------------------
RE_REC = re.compile(r'(?:CXXRecordDecl|ClassTemplateSpecializationDecl|ClassTemplatePartialSpecializationDecl)'
                    r' (0x[0-9a-f]+)(?: parent 0x[0-9a-f]+)?(?: prev (0x[0-9a-f]+))?.*\b(?:class|struct|union) (\w+)')
RE_MDECL = re.compile(r'(CXXMethodDecl|CXXConstructorDecl|CXXDestructorDecl|CXXConversionDecl) (0x[0-9a-f]+)'
                      r'(?: parent (0x[0-9a-f]+))?(?: prev (0x[0-9a-f]+))?')
RE_MEMBER = re.compile(r"MemberExpr 0x[0-9a-f]+ .*(?:->|\.)(\S+) (0x[0-9a-f]+)\s*$")
RE_DREF = re.compile(r"DeclRefExpr 0x[0-9a-f]+ .* (CXXMethod|Function) (0x[0-9a-f]+) '([^']+)' '([^']*)'")
RE_VARDECL = re.compile(r"VarDecl (0x[0-9a-f]+) .*? (?:used |referenced )?(\w+) '([^']*)'")
RE_VARREF = re.compile(r"DeclRefExpr 0x[0-9a-f]+ .* Var (0x[0-9a-f]+) '")
RE_CONST = re.compile(r"\)\s*(?:__attribute__\(\(\w+\)\)\s*)?const\s*(?:&|&&)?\s*$")


def _depth(line):
    i = 0
    n = len(line)
    while i < n and line[i] in '| `-':
        i += 1
    return i // 2


def _split_decl(line):
    """name, type, rest of a *Decl line ('... col:13 used lIsWater 'int (lua_State *)' static')."""
    q = line.find("'")
    if q < 0:
        return None, '', ''
    head = line[:q].rstrip()
    name = head.rsplit(' ', 1)[-1]
    q2 = line.find("'", q + 1)
    typ = line[q + 1:q2]
    rest = line[q2 + 1:]
    if rest.startswith(":'"):
        rest = rest[rest.find("'", 2) + 1:]
    return name, typ, rest


def ast_worker(src):
    t0 = time.time()
    proc = subprocess.Popen(clang_cmd('x86', src, ['-fsyntax-only', '-Xclang', '-ast-dump']), env=clang_env(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=1 << 20)
    records = {}          # addr -> class name
    methods = {}          # addr -> (cls, name, type, static, const)
    stack = []            # [(depth, cls)]
    funcs = {}            # (cls, name) -> {'refs': [...], 'body': bool}
    cap = None            # (depth, key)
    local_vals = set()    # VarDecl addrs of value-typed locals in the captured function
    pending = None        # (ref list, index, depth) of a '.' MemberExpr whose base is not yet seen
    nbytes = 0
    for raw in proc.stdout:
        nbytes += len(raw)
        if cap is not None:
            line = raw.decode('utf-8', 'replace')
            d = _depth(line)
            if d > cap[0]:
                f = funcs[cap[1]]
                if d == cap[0] + 1 and 'CompoundStmt' in line[:d * 2 + 14]:
                    f['body'] = True
                if pending is not None:
                    if d <= pending[2]:
                        pending = None
                    elif 'ImplicitCastExpr' in line or 'ParenExpr' in line:
                        pass
                    else:
                        mv = RE_VARREF.search(line)
                        if mv and mv.group(1) in local_vals:
                            r = pending[0][pending[1]]
                            pending[0][pending[1]] = r + ('local',)
                        pending = None
                if 'VarDecl ' in line:
                    mv = RE_VARDECL.search(line)
                    if mv and not mv.group(3).rstrip().endswith(('*', '&')):
                        local_vals.add(mv.group(1))
                m = RE_MEMBER.search(line)
                if m:
                    f['refs'].append(('M', m.group(2), m.group(1)))
                    if '.' + m.group(1) + ' ' in line:
                        pending = (f['refs'], len(f['refs']) - 1, d)
                    continue
                m = RE_DREF.search(line)
                if m:
                    f['refs'].append(('D', m.group(2), m.group(3), m.group(1), m.group(4)))
                continue
            cap = None
            pending = None
            local_vals = set()
            # fall through: this line may itself be a decl
        if b'Decl ' not in raw:
            continue
        if not (b'CXXMethodDecl' in raw or b'RecordDecl' in raw or b'SpecializationDecl' in raw
                or b'CXXConstructorDecl' in raw or b'CXXDestructorDecl' in raw or b'CXXConversionDecl' in raw):
            continue
        line = raw.decode('utf-8', 'replace').rstrip('\r\n')
        d = _depth(line)
        body = line[d * 2:]
        m = RE_REC.match(body)
        if m:
            records[m.group(1)] = m.group(3)
            while stack and stack[-1][0] >= d:
                stack.pop()
            stack.append((d, m.group(3)))
            continue
        m = RE_MDECL.match(body)
        if not m:
            continue
        while stack and stack[-1][0] >= d:
            stack.pop()
        addr, parent, prev = m.group(2), m.group(3), m.group(4)
        if parent:
            cls = records.get(parent, '?')
        elif stack:
            cls = stack[-1][1]
        else:
            cls = '?'
        name, typ, rest = _split_decl(body)
        static = ' static' in rest
        if prev and prev in methods:
            static = static or methods[prev][3]
        methods[addr] = (cls, name, typ, static, bool(RE_CONST.search(typ)))
        if cls.startswith('CvLua') and m.group(1) == 'CXXMethodDecl':
            key = (cls, name)
            if key not in funcs or not funcs[key]['body']:
                funcs[key] = {'refs': [], 'body': False}
                cap = (d, key)
            else:
                cap = (d, ('__dup__',) + key)
                funcs[cap[1]] = {'refs': [], 'body': False}
    err = proc.stderr.read()
    rc = proc.wait()
    if rc != 0:
        raise RuntimeError('clang ast failed %s: %s' % (src, err[-2000:]))
    out = {}
    for key, f in funcs.items():
        if key[0] == '__dup__' or not f['body']:
            continue
        refs = []
        for r in f['refs']:
            if r[0] == 'M':
                mm = methods.get(r[1])
                if mm is None:
                    continue  # field access or unresolved
                refs.append((('local_member',) if len(r) > 3 else ('member',)) + mm)
            else:
                if r[3] == 'CXXMethod':
                    mm = methods.get(r[1])
                    if mm is None:
                        mm = ('?', r[2], r[4], True, bool(RE_CONST.search(r[4])))
                    refs.append(('ref',) + mm)
                else:
                    refs.append(('func', '', r[2], r[4], True, False))
        out['%s::%s' % key] = refs
    return {'src': src, 'funcs': out, 'bytes': nbytes, 'seconds': round(time.time() - t0, 1),
            'methods_in_tu': len(methods)}


# ----------------------------------------------------------------------------------------------
# 3. source text helpers
# ----------------------------------------------------------------------------------------------
def strip_comments(text):
    """Replace comments by spaces (keeping newlines); returns new text."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            i = j + 1
        else:
            i += 1
    return ''.join(out)


DIRECTIVE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')


def active_mask(lines, nonblank):
    """lines: source lines (0-based list); nonblank: set of 1-based line numbers with pp output.
    Returns (active list of bool per line, cond list: enclosing #if text per line or None)."""
    n = len(lines)
    active = [True] * n
    cond = [None] * n
    seg_start = 0
    stack = []
    bounds = []
    for i, l in enumerate(lines):
        m = DIRECTIVE.match(l)
        if m:
            bounds.append((seg_start, i, stack[-1] if stack else None))
            kw = m.group(1)
            if kw in ('if', 'ifdef', 'ifndef'):
                stack.append('%d: %s' % (i + 1, l.strip()))
            elif kw in ('elif', 'else'):
                if stack:
                    stack[-1] = stack[-1] + ' | %d: %s' % (i + 1, l.strip())
            elif kw == 'endif':
                if stack:
                    stack.pop()
            seg_start = i + 1
    bounds.append((seg_start, n, stack[-1] if stack else None))
    for a, b, c in bounds:
        on = any((k + 1) in nonblank for k in range(a, b))
        has_code = any(lines[k].strip() for k in range(a, b))
        for k in range(a, b):
            active[k] = on or not has_code
            cond[k] = c
    for i, l in enumerate(lines):
        if DIRECTIVE.match(l):
            active[i] = False
    return active, cond


def match_brace(text, i):
    """text[i] == '{' ; return index after the matching '}'."""
    depth = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            i = j
        i += 1
    return n


RE_DEF_CPP = re.compile(r'\bint\s+(CvLua\w+)\s*::\s*(l\w+)\s*\(\s*lua_State\s*\*\s*\w*\s*\)\s*\{')
RE_DEF_HELPER = re.compile(r'\bint\s+(CvLua\w+)\s*::\s*(\w+)\s*\(\s*lua_State\s*\*\s*\w*\s*,[^)]*\)\s*\{')
RE_DEF_H = re.compile(r'\bstatic\s+int\s+(l\w+)\s*\(\s*lua_State\s*\*\s*\w*\s*\)\s*\{')
RE_APIIMPL = re.compile(r'\bLUAAPIIMPL\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')


# Issue 3519: master's Lua API (#13489) spells the instance helpers differently. !CvLuaApi.h declares per type
# 'LuaTypes_DeclareInstanced(tag, Name, CLASS)', which makes the inline free functions luaEx_to<Name>(L, idx[, err])
# and luaEx_push<Name>(L, inst); each CvLua<Lib>.cpp binds CLASS to its wrapper with
# 'LuaTypes_DefineInstanced(CLASS, CvLua<Lib>)'. luaEx_exec[<types>](L, &Class::method) replaces the pin's
# BasicLuaMethod<ret, types>(L, &Class::method) (BasicLuaMethod stays as a compat template that calls it), and the
# generic luaEx_to<T>(L, idx) reads a scalar. normalise_body() rewrites these spellings into the pin's
# (CvLua<Lib>::GetInstance, CvLua<Lib>::Push, BasicLuaMethod, CvLuaArgs::toValue<T>), so that one textual parse reads
# both forms and a method master did not change keeps the pin's record. Pin sources contain none of them.
RE_DECLARE_INST = re.compile(r'^\s*LuaTypes_DeclareInstanced\s*\(\s*\w+\s*(?:/\*.*?\*/\s*)?,\s*(\w+)\s*,\s*(\w+)\s*\)',
                             re.M)
RE_DEFINE_INST = re.compile(r'^\s*LuaTypes_Define(?:Instanced|Static)\s*\(\s*(\w+)\s*,\s*(CvLua\w+)\s*\)', re.M)
TYPE_MAP = {}        # Lua type name of master's API (City, PlayerBase, TeamTechs, ...) -> CvLua wrapper class


def build_type_map(texts):
    """texts: {path: source text} of the Lua folder. Returns {Name: CvLua wrapper} from master's declarations
    (empty for a pin source)."""
    by_class, wrapper_of = {}, {}
    for t in texts.values():
        for m in RE_DECLARE_INST.finditer(t):
            by_class[m.group(2)] = m.group(1)
        for m in RE_DEFINE_INST.finditer(t):
            wrapper_of[m.group(1)] = m.group(2)
    return {name: wrapper_of[cls] for cls, name in by_class.items() if cls in wrapper_of}


def normalise_body(body, type_map=None):
    """Rewrite master's luaEx_* spellings into the pin's (see TYPE_MAP)."""
    tm = TYPE_MAP if type_map is None else type_map
    if 'luaEx_' not in body:
        return body

    def inst(m):
        w = tm.get(m.group(2))
        if not w:
            return m.group(0)
        return '%s::%s(' % (w, 'GetInstance' if m.group(1) == 'to' else 'Push')
    body = re.sub(r'\bluaEx_(to|push)([A-Z]\w*)\s*\(', inst, body)
    body = re.sub(r'\bluaEx_exec\b', 'BasicLuaMethod', body)
    body = re.sub(r'\bluaEx_to\s*<', 'CvLuaArgs::toValue<', body)
    return body


def find_functions(path, masked):
    """masked: comment-stripped text with inactive lines blanked.  Returns {cfunc: (line, body)}."""
    res = {}
    stem = os.path.splitext(os.path.basename(path))[0]
    pats = [RE_DEF_CPP, RE_DEF_HELPER] if path.endswith('.cpp') else [RE_DEF_H]
    for pat in pats:
        for m in pat.finditer(masked):
            if pat is not RE_DEF_H:
                cls, fn = m.group(1), m.group(2)
            else:
                cls, fn = stem, m.group(1)
            end = match_brace(masked, m.end() - 1)
            line = masked.count('\n', 0, m.start()) + 1
            res['%s::%s' % (cls, fn)] = (line, normalise_body(masked[m.end() - 1:end]))
    for m in RE_APIIMPL.finditer(masked):
        line = masked.count('\n', 0, m.start()) + 1
        body = '{ return BasicLuaMethod(L, &Cv%s::%s); }' % (m.group(1), m.group(2))
        res['CvLua%s::l%s' % (m.group(1), m.group(2))] = (line, body)
    return res


# ----------------------------------------------------------------------------------------------
# 4. classification
# ----------------------------------------------------------------------------------------------
READ_PREFIX = ('get', 'is', 'can', 'has', 'count', 'calc', 'calculate', 'find', 'at', 'first', 'next', 'last',
               'num', 'size', 'empty', 'plot', 'area', 'best', 'total', 'will', 'would', 'should', 'does', 'are',
               'need', 'needs', 'contains', 'any', 'c', 'begin', 'end', 'value', 'compute', 'estimate', 'predict',
               'how', 'which', 'eval', 'valid', 'pointer', 'lookup', 'query', 'was', 'were', 'must', 'might',
               'expected', 'max', 'min', 'average', 'sum', 'locale', 'localize', 'distance', 'plot', 'player',
               'team', 'at', 'index', 'operator', 'what', 'compare', 'look', 'test', 'within', 'within',
               'already', 'quick', 'have', 'did', 'raw', 'base', 'in', 'to', 'str', 'parse', 'pathfind',
               'path', 'generate', 'one', 'it', 'meets', 'passes', 'number', 'turns', 'cost', 'score',
               'percent', 'potential', 'current', 'ready', 'found_')
WRITE_PREFIX = ('set', 'change', 'do', 'init', 'kill', 'add', 'remove', 'push', 'pop', 'send', 'update', 'clear',
                'reset', 'create', 'found', 'move', 'acquire', 'raze', 'give', 'take', 'apply', 'process',
                'increment', 'decrement', 'toggle', 'assign', 'spawn', 'destroy', 'delete', 'erase', 'insert',
                'start', 'finish', 'cancel', 'make', 'convert', 'upgrade', 'join', 'leave', 'declare', 'buy',
                'purchase', 'sell', 'trade', 'gift', 'spread', 'liberate', 'disband', 'transfer', 'enact',
                'propose', 'vote', 'recalculate', 'recalc', 'refresh', 'invalidate', 'mark', 'activate',
                'deactivate', 'unlock', 'learn', 'grant', 'award', 'expend', 'consume', 'use', 'perform',
                'execute', 'handle', 'choose', 'select', 'queue', 'rebase', 'swap', 'jump', 'reveal', 'verify',
                'rand', 'cycle', 'fire', 'trigger', 'notify', 'log', 'cache', 'dirty', 'lock', 'unlock', 'ally',
                'annex', 'puppet', 'free', 'bully', 'calculate_and_set', 'scrap', 'disarm', 'rescue', 'retire',
                'promote', 'build', 'construct', 'plant', 'pillage', 'repair', 'heal', 'damage', 'attack',
                'assimilate', 'flip', 'demand', 'accept', 'reject', 'end_turn', 'recompute', 'calculate_',
                'normalize', 'stamp', 'place', 'recount', 'compute_', 'implement', 'resolve', 'conquer',
                'establish', 'expel', 'remove_', 'set_', 'unset', 'mod', 'modify', 'adjust', 'force', 'enable',
                'disable', 'open', 'close', 'restore', 'save', 'load', 'write', 'read', 'store', 'register',
                'unregister', 'request', 'post', 'broadcast', 'dispatch', 'recall', 'pay', 'spend', 'earn',
                'receive', 'collect', 'distribute', 'raise', 'lower', 'boost', 'set')
PLUMBING_FUNCS = {'CvAssertFailedFormat', 'CvPreconditionFailedFormat', 'OutputDebugStringA', 'ShortenFilePath',
                  'UnwrapUnitPointer', 'CvAssertFunc', 'CvAssertFailed', 'sort', 'stable_sort', 'find', 'swap',
                  'reverse', 'unique', 'make_pair', 'distance', 'advance', 'lower_bound', 'upper_bound'}
UI_NAMES = {'mouseoverUnit', 'DisplayTemporaryPopupTradeRoute', 'showPopupText'}
WRITE_NAMES = {'CityPurchase', 'cityPushOrder', 'ReviveActivePlayer', 'CopyModDataToMPMP', 'reloadCache', 'meet',
               'embark', 'plunderTradeRoute', 'greatperson', 'lead', 'AI_setUnitAIType', 'LinkUnits', 'UnlinkUnits',
               'rotateFacingDirectionClockwise', 'rotateFacingDirectionCounterClockwise',
               'increaseInterceptionCount', 'DefaultContinentStamper', 'nukeExplosion', 'revokePolicy',
               'EnhanceReligion', 'AdoptReligionFully', 'BlockTemporaryForPermanentTrade', 'fracInit',
               'ridgeBuilder', 'CompleteAccomplishment', 'Dismiss', 'LevelUpSpy'}
READ_NAMES = {'activePlayer', 'UnitIsActiveContractUnit'}
ENGINE_CLASS = re.compile(r'^I(Cv|Fire|Cv\w*)')
PLUMBING_CLASSES = {'CvString', 'FString', 'CvStringHelper', 'FStringFixedBuffer', 'FObjectHandle', 'FStaticVector',
                    'FFastVector', 'FAutoArchive', 'CvWeightedVector', 'FAutoVariableBase', 'FAutoVariable',
                    'CvLuaArgsHandle', 'CvSeeder', 'FFastSmallFixedList', 'FFastList'}
LUA_PLUMBING = {'Push', 'GetInstance', 'PushTypeTable', 'HandleMissingInstance', 'DefaultHandleMissingInstance',
                'GetTypeName', 'GetInstanceName', 'BasicLuaMethod', 'GetStartingArgIndex', 'RegisterMethod'}
HIDDEN_NAMES = re.compile(r'(?i)^(get|is)?active(player|team|civ\w*)$|^activePlayer$|^isLocalPlayer$|'
                          r'^getActivePlayer\w*|^GetActivePlayer\w*|^getActiveTeam|^GetActiveTeam|'
                          r'^isActive(Player|Team)\b|^IsActive(Player|Team)\b|^GetActiveLeague$')
RNG_NAMES = re.compile(r'(?i)jonrand|fakerand|randnum|^rand$|randrange|^randomize|^getRand')


def hidden_of(name):
    return 'active_player' if HIDDEN_NAMES.search(name) else None


def cls_kind(cls):
    # Issue 3519: master's instance templates (!CvLuaApiInstanceTemplates.h, !CvLuaApiExec.h) count as the wrapper
    # classes of the pin did.
    if cls.startswith('CvLua') or cls in ('LuaSupport', 'CvLuaMethodWrapper', 'CvLuaScopedInstance',
                                          'CvLuaStaticInstance', 'CvLuaDynamicInstance', 'LuaTaggedAutoPointer',
                                          'TypeGlobals', 'lua_StateEx'):
        return 'lua_wrapper'
    if cls in PLUMBING_CLASSES or cls.startswith('F') and not cls.startswith('Fire'):
        return 'plumbing'
    if ENGINE_CLASS.match(cls):
        return 'engine'
    if cls.startswith('Cv') or cls.startswith('Cv'):
        return 'game'
    if cls == '':
        return 'free'
    if cls[:1].islower() or cls.startswith('_'):
        return 'std'
    return 'other'


def name_class(name):
    n = name[2:] if name.startswith('m_') else name
    n = n.lstrip('_')
    if not n:
        return 'unknown'
    low = n[0].lower() + n[1:]
    m = re.match(r'([a-z]+)', low)
    if not m:
        return 'unknown'
    word = m.group(1)
    if word in READ_PREFIX:
        return 'read'
    if word in WRITE_PREFIX:
        return 'write'
    if low.startswith('operator'):
        return 'read'
    return 'unknown'


def callee_effect(kind, cls, name, typ, static, const):
    """Returns (effect, rule, hidden) for one referenced function.
    effect: plumbing|read|write|net|ui|unknown;  hidden: None|'active_player'|'ui_state'|'rng'."""
    if kind == 'local_member':
        return 'local', 'member_of_local_value_object[C]', None
    ck = cls_kind(cls) if kind != 'func' else 'free'
    hidden = None
    if name in PLUMBING_FUNCS or name.startswith(('luaCheck', 'LuaTo')):
        return 'plumbing', 'assert_or_lua_arg_helper', None
    if cls == 'CvGameTextMgr':
        return 'read', 'text_formatting_into_out_param', None
    if name.startswith('~'):
        return 'write', 'destructor', None
    if kind == 'ref' and name.startswith('operator') and cls_kind(cls) != 'engine':
        return 'local', 'operator_on_local_value[I]', None
    if name in READ_NAMES:
        return 'read', 'getter_by_explicit_name[I]', hidden_of(name)
    if 'NetMessage' in name or name.startswith('selectionList'):
        return 'net', 'net_message_by_name[I]', None
    if name in UI_NAMES:
        return 'ui', 'ui_call_by_name[I]', None
    if name in WRITE_NAMES:
        return 'write', 'mutator_by_explicit_name[I]', 'rng' if name in ('fracInit', 'ridgeBuilder') else None
    if HIDDEN_NAMES.search(name):
        hidden = 'active_player'
    if (cls == 'CvRandom' and not const) or RNG_NAMES.search(name):
        return 'write', 'rng_advance', 'rng'
    if kind == 'func':
        if name.startswith(('lua_', 'luaL_')) or name in ('toValue', 'pushValue'):
            return 'plumbing', 'lua_api', None
        # Issue 3519: master's free Lua helpers (luaEx_to<Name>, luaEx_push<Name>, luaEx_exec, luaVp_*) and its
        # free compat template BasicLuaMethod are the plumbing that the pin's CvLua* members were; so is
        # LuaSupport::TrackDllFinalizer (a namespace function: master's Fractal userdata registers its __gc there).
        if name.startswith(('luaEx_', 'luaVp_')) or name in ('BasicLuaMethod', 'TrackDllFinalizer'):
            return 'plumbing', 'lua_wrapper', None
        if name in ('GetLocalizedText', 'GetLocalizedTextUC', 'Localization', 'strcmp', 'strlen', 'sprintf',
                    'sprintf_s', '_snprintf', 'memset', 'memcpy', 'abs', 'min', 'max', 'sqrt', 'floor', 'ceil',
                    'getXINLINE', 'getYINLINE', 'plotDistance', 'coordRange', 'plotXYWithRangeCheck', 'plotXY',
                    'plotDirection', 'directionXY', 'estimateMovesToTarget'):
            return 'read', 'free_function_known_pure', hidden
        if name.startswith('CCall') or name in ('CallHook', 'CallAccumulator', 'CallTestAll', 'CallTestAny'):
            return 'write', 'lua_hook_call', hidden
        nc = name_class(name)
        if nc == 'write' and name.lower().startswith(('send', 'netmessage')):
            return 'net', 'free_send', hidden
        return nc, 'free_function_by_name[I]', hidden
    if ck in ('lua_wrapper',):
        return 'plumbing', 'lua_wrapper', None
    if ck in ('plumbing', 'std'):
        return 'plumbing', 'container_or_string', None
    if ck == 'engine':
        nc = name_class(name)
        if name.lower().startswith('send'):
            return 'net', 'engine_send', None
        if nc == 'read' or const:
            return 'read', 'engine_getter', 'ui_state' if 'UserInterface' in cls else hidden
        if cls.startswith('ICvEngineUtility') and name in ('GetGameCoreLock', 'ReleaseGameCoreLock',
                                                           'GetScriptSystem', 'GetFileSystem'):
            return 'plumbing', 'engine_plumbing', None
        return 'ui', 'engine_call', None
    if const:
        return 'read', 'const_member[C]', hidden
    if static:
        nc = name_class(name)
        return nc, 'static_member_by_name[I]', hidden
    if name in ('operator=',):
        return 'write', 'assignment', hidden
    nc = name_class(name)
    if nc == 'read':
        return 'read', 'nonconst_getter_by_name[I]', hidden
    if nc == 'write':
        if name.lower().startswith('send'):
            return 'net', 'send_by_name[I]', hidden
        return 'write', 'mutator_by_name[I]', hidden
    return 'unknown', 'nonconst_unknown_name', hidden


# ----------------------------------------------------------------------------------------------
# 5. Lua-stack shape (textual)
# ----------------------------------------------------------------------------------------------
ACCESSORS = {
    'lua_tointeger': 'integer', 'luaL_checkint': 'integer', 'luaL_checkinteger': 'integer',
    'luaL_optint': 'integer', 'luaL_optinteger': 'integer', 'lua_tonumber': 'number',
    'luaL_checknumber': 'number', 'luaL_optnumber': 'number', 'lua_toboolean': 'boolean',
    'luaL_optbool': 'boolean', 'lua_tostring': 'string', 'lua_tolstring': 'string',
    'luaL_checkstring': 'string', 'luaL_checklstring': 'string', 'luaL_optstring': 'string',
    'lua_istable': 'table', 'lua_objlen': 'table', 'lua_next': 'table', 'lua_rawgeti': 'table',
    'lua_getfield': 'table', 'lua_gettable': 'table', 'lua_rawget': 'table', 'lua_isfunction': 'function',
    'lua_isnil': 'presence', 'lua_isnoneornil': 'presence', 'lua_isnumber': 'type_test',
    'lua_isboolean': 'type_test', 'lua_isstring': 'type_test', 'lua_type': 'type_test',
    'luaL_checktype': 'type_check', 'lua_touserdata': 'userdata', 'lua_pushvalue': 'copied',
    'luaCheckUnitType': 'integer', 'LuaToTradeDomain': 'integer', 'GetFractalFlags': 'table', 'HasFlag': 'table',
}
# Issue 0309: the CvLua classes inherit CvLuaArgs, so the pin also spells the scalar read as the unqualified
# toValue<T>(L, n) (40 sites at 04b1211b8). It is read as CvLuaArgs::toValue<T>; a member access (x.toValue<,
# p->toValue<) or another class's qualification (Other::toValue<) is not counted.
RE_ACC = re.compile(r'\b(' + '|'.join(sorted(ACCESSORS, key=len, reverse=True)) +
                    r'|(?:CvLuaArgs::|(?<![.>:]))toValue\s*<[^>]*>)\s*\(\s*L\s*,\s*([^,()]+?)\s*([,)])')
RE_INST = re.compile(r'(?:\b(CvLua\w+)\s*::\s*)?\bGetInstance\s*\(\s*L\s*(?:,\s*([^,()]+?))?\s*(?:,\s*(\w+))?\s*\)')
RE_GETTOP = re.compile(r'\blua_gettop\s*\(\s*L\s*\)')
PUSHES = [
    (re.compile(r'\blua_pushinteger\s*\('), 'integer'), (re.compile(r'\blua_pushnumber\s*\('), 'number'),
    (re.compile(r'\blua_pushboolean\s*\('), 'boolean'), (re.compile(r'\blua_push[lf]?string\s*\('), 'string'),
    (re.compile(r'\blua_pushnil\s*\('), 'nil'), (re.compile(r'\blua_(?:create|new)table\s*\('), 'table'),
    (re.compile(r'\blua_pushcclosure\s*\('), 'closure'), (re.compile(r'\blua_pushlightuserdata\s*\('), 'userdata'),
    (re.compile(r'\blua_newuserdata\s*\('), 'userdata'),
]
RE_PUSH_INST = re.compile(r'(?:\b(CvLua\w+)\s*::\s*Push|(?<![\w:])Push)\s*\(\s*L\s*,')
RE_RETURN = re.compile(r'\breturn\b\s*([^;]*);')
RE_LUASTATE = re.compile(r'\b(lua_getglobal|lua_setglobal|LUA_REGISTRYINDEX|LUA_GLOBALSINDEX|lua_upvalueindex|'
                         r'lua_getstack|luaL_dostring|lua_pcall|lua_call|lua_setmetatable|lua_getmetatable|'
                         r'lua_newuserdata|lua_pushlightuserdata|LuaSupport::Call\w+|CallHook|CallAccumulator|'
                         r'lua_rawset\s*\(\s*L\s*,\s*LUA_|lua_settop|lua_replace|lua_remove|lua_insert)\b')
SCALAR = {'integer', 'number', 'boolean', 'string', 'nil'}


INSTANCE_BY_CPP = {v[1]: v[0] for v in LIBS.values()}
INSTANCE_BY_CPP['CvPlayer'] = 'Player'


def cpp_instance(t):
    m = re.match(r'^(?:const\s+)?(Cv\w+)\s*[*&]$', t.strip())
    return INSTANCE_BY_CPP.get(m.group(1)) if m else None


def cpp_type_kind(t):
    t = t.strip()
    if cpp_instance(t):
        return 'instance'
    if t.endswith(('*', '&')):
        return 'unsupported'
    if t == 'bool':
        return 'boolean'
    if t in ('const char *', 'char *'):
        return 'string'
    return 'integer'


def split_sig(typ):
    """'bool (PromotionTypes) __attribute__((thiscall)) const' -> ('bool', ['PromotionTypes'])."""
    i = typ.find('(')
    ret = typ[:i].strip()
    depth = 0
    args, cur = [], ''
    for c in typ[i + 1:]:
        if c == '(':
            depth += 1
        elif c == ')':
            if depth == 0:
                break
            depth -= 1
        if c == ',' and depth == 0:
            args.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip() and cur.strip() != 'void':
        args.append(cur.strip())
    return ret, args


def parse_shape(body, lib, kind, own_lib):
    """Textual parse of the C body.  Returns args, results, flags."""
    args = {}
    flags = set()
    first_push = None
    for p, _ in PUSHES:
        m = p.search(body)
        if m and (first_push is None or m.start() < first_push):
            first_push = m.start()
    m = RE_PUSH_INST.search(body)
    if m and (first_push is None or m.start() < first_push):
        first_push = m.start()
    for m in RE_INST.finditer(body):
        cls, pos, err = m.group(1), m.group(2), m.group(3)
        ilib = LIBS.get(cls, (own_lib,))[0] if cls else own_lib
        p = (pos or '1').strip()
        if p == 'false':
            p = '0'
        k = p
        if k.isdigit():
            k = int(k)
        if k not in args:
            args[k] = {'pos': k, 'accessor': (cls + '::' if cls else '') + 'GetInstance', 'kind': 'instance',
                       'instance': ilib, 'optional': err == 'false'}
    for m in RE_ACC.finditer(body):
        acc, pos = m.group(1), m.group(2).strip()
        acc = re.sub(r'\s+', '', acc)
        if acc.startswith('toValue'):
            acc = 'CvLuaArgs::' + acc
        if acc.startswith('CvLuaArgs::toValue'):
            akind = cpp_type_kind(acc[acc.find('<') + 1:-1])
        else:
            akind = ACCESSORS[acc]
        if re.match(r'^-\d+$', pos) or pos in ('t', 'top', 'tbl', 'iTable', 'table'):
            continue  # stack-relative: not an argument
        k = int(pos) if pos.isdigit() else pos
        if acc in ('lua_rawgeti', 'lua_getfield', 'lua_gettable', 'lua_rawget', 'lua_next', 'lua_objlen',
                   'lua_pushvalue') and not isinstance(k, int):
            continue
        if acc == 'lua_pushvalue':
            continue
        if acc == 'luaL_checktype':
            mm = re.match(r'\s*,\s*(LUA_T\w+)', body[m.end():])
            t = mm.group(1) if mm else ''
            akind = {'LUA_TTABLE': 'table', 'LUA_TFUNCTION': 'function', 'LUA_TUSERDATA': 'userdata',
                     'LUA_TNUMBER': 'number', 'LUA_TSTRING': 'string', 'LUA_TBOOLEAN': 'boolean'}.get(t, 'any')
        opt = acc.startswith('luaL_opt') or akind == 'presence'
        cur = args.get(k)
        if cur is None:
            args[k] = {'pos': k, 'accessor': acc, 'kind': akind if akind not in ('presence', 'type_test')
                       else 'any', 'optional': opt}
        else:
            if opt:
                cur['optional'] = True
            if cur['kind'] in ('any',) and akind not in ('presence', 'type_test', 'type_check'):
                cur['kind'] = akind
                cur['accessor'] = acc
            if akind == 'type_test':
                cur.setdefault('type_tested', True)
    if 'LUA_TFUNCTION' in body:
        flags.add('callback_arg')
    for a in args.values():
        if a['kind'] == 'function':
            flags.add('callback_arg')
        if a['kind'] == 'table':
            flags.add('table_arg')
        if not isinstance(a['pos'], int):
            flags.add('computed_arg_index')
    gm = RE_GETTOP.search(body)
    if gm and (first_push is None or gm.start() < first_push):
        flags.add('variable_args')
    # results
    kinds = collections.OrderedDict()
    for p, k in PUSHES:
        n = len(p.findall(body))
        if n:
            kinds[k] = n
    inst = []
    for m in RE_PUSH_INST.finditer(body):
        cls = m.group(1)
        inst.append(LIBS.get(cls, (own_lib,))[0] if cls else own_lib)
    counts = set()
    computed = False
    for m in RE_RETURN.finditer(body):
        v = m.group(1).strip()
        v = {'true': '1', 'false': '0'}.get(v, v)
        if re.match(r'^\d+$', v):
            counts.add(int(v))
        elif v.startswith('luaL_error') or v.startswith('INLINED_HELPER'):
            pass
        else:
            computed = True
    results = {'counts': sorted(counts), 'computed_count': computed}
    if 'luaL_error' in body:
        results['raises_lua_error'] = True
    tables = kinds.get('table', 0)
    top = []
    if 'closure' in kinds:
        flags.add('returns_closure')
        top = ['closure']
    elif tables:
        top = ['table']
        flags.add('table_result')
        if tables >= 2:
            flags.add('nested_table_result')
        results['table_elements'] = [k for k in kinds if k != 'table'] + (['instance'] if inst else [])
    else:
        top = list(kinds) + (['instance'] if inst else [])
    results['kinds'] = top
    if inst:
        results['instances'] = sorted(set(inst))
    if computed or len(counts - {0}) > 1:
        flags.add('variable_results')
    if counts == {0, 1}:
        results['zero_means_nil'] = True
    if RE_LUASTATE.search(body):
        found = sorted(set(re.sub(r'\s+', '', x) for x in RE_LUASTATE.findall(body)))
        results_state = [x for x in found if x not in ('lua_settop', 'lua_replace', 'lua_remove', 'lua_insert')]
        if results_state:
            flags.add('lua_state_access')
            results['lua_state'] = results_state
    arglist = sorted(args.values(), key=lambda a: (not isinstance(a['pos'], int),
                                                    a['pos'] if isinstance(a['pos'], int) else 0, str(a['pos'])))
    return arglist, results, flags


# ----------------------------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------------------------
# Issue 3519: upstream master's Lua API refactor (#13489, Lua/!CvLuaApi*.h) changed the expanded form of Method(Name).
# At the pin (Release-5.4.8 and the fork's x64 04b1211b8): 'lua_pushcclosure(L, lName, 0); lua_setfield(L, t, "Name");'
# in the instance libraries and 'RegisterMethod(L, lName, "Name");' in Game and Map. On master every library's
# CreateFunctable(L) fills the function table at stack index 1 ('lua_setfield(L, 1, "Name")'), and !CvLuaApi.h
# '#define lua_pushcclosure luaVp_pushcclosure' (every C function is wrapped in a protected closure with one upvalue,
# protectedWrapperAux in CvLuaSupport.cpp), so the expanded text reads 'luaVp_pushcclosure(L, lName, 0)'. Both forms
# count; the target of lua_setfield is the table variable 't' (pin) or the stack index 1 (master).
PUSHCC = r'\blua(?:Vp)?_pushcclosure\s*\('
REG_METHOD = re.compile(PUSHCC + r'\s*L\s*,\s*(l\w+)\s*,\s*0\s*\)\s*;\s*lua_setfield\s*\(\s*L\s*,\s*(?:t|1)\s*,'
                        r'\s*"(\w+)"\s*\)')
REG_REGISTER = re.compile(r'\bRegisterMethod\s*\(\s*L\s*,\s*(l\w+)\s*,\s*"(\w+)"\s*\)')
REG_CLOSURE = re.compile(PUSHCC + r'\s*L\s*,\s*([\w:]+)\s*,\s*(\d+)\s*\)')
REG_SETFIELD = re.compile(r'lua_setfield\s*\(\s*L\s*,\s*-2\s*,\s*"(\w+)"\s*\)')
RAW_METHOD = re.compile(r'Method\((\w*)\)')
# Issue 3519: master's CvLuaSupport.cpp streams the global table 'vputils' with the luafsq_* macros of
# !CvLuaApiExec.h (luafsq_func(Name) { ... } and luafsq_wrap(Name, Class::method[, types])); read from the active
# source text, because the expanded form hides the name in a local 'const char* mcN = "Name"'.
REG_LUAFSQ = re.compile(r'\bluafsq_(func|wrap)\s*\(\s*(\w+)\s*(?:,\s*([\w:]+))?')


def scan_registrations(lines, cls, lib, src):
    """Registrations in the preprocessed lines {line: text} of one Lua source (its own lines only).
    Returns (regs, others): regs [(lib, luaname, cfunc, src, line, form)] for Method(...) and RegisterMethod(...);
    others [(lib, luaname or None, cfunc, src, line, upvalues)] for every other lua_pushcclosure (a following
    lua_setfield(L, -2, "name") on the same or the next line names it)."""
    regs, others = [], []
    order = sorted(lines)
    for idx, ln in enumerate(order):
        text = lines[ln]
        spans = []
        for m in REG_METHOD.finditer(text):
            regs.append((lib, m.group(2), '%s::%s' % (cls, m.group(1)), src, ln, 'Method'))
            spans.append(m.span())
        for m in REG_REGISTER.finditer(text):
            regs.append((lib, m.group(2), '%s::%s' % (cls, m.group(1)), src, ln, 'RegisterMethod'))
            spans.append(m.span())
        for m in REG_CLOSURE.finditer(text):
            if any(x[0] <= m.start() < x[1] for x in spans):
                continue
            name = None
            mm = REG_SETFIELD.search(text, m.end())
            if not mm and idx + 1 < len(order):
                mm = REG_SETFIELD.search(lines[order[idx + 1]])
            if mm:
                name = mm.group(1)
            others.append((lib, name, '%s::%s' % (cls, m.group(1).split('::')[-1]), src, ln, int(m.group(2))))
    return regs, others


def scan_luafsq(masked_text):
    """Issue 3519: [(form, luaname, wrapped or None, line)] for the luafsq_func / luafsq_wrap entries of an active,
    comment-stripped source text (master's vputils table in CvLuaSupport.cpp)."""
    res = []
    for m in REG_LUAFSQ.finditer(masked_text):
        g = REG_SETGLOBAL.search(masked_text, m.end())
        res.append((m.group(1), m.group(2), m.group(3) if m.group(1) == 'wrap' else None,
                    masked_text.count('\n', 0, m.start()) + 1, g.group(1) if g else None))
    return res


REG_SETGLOBAL = re.compile(r'\blua_setglobal\s*\(\s*L\s*,\s*"(\w+)"\s*\)')


def configure(argv=None):
    """Parse the options; point FORK, PINNED_COMMIT, LUA_DIR, OUT and DEST at --src. Returns 0 or 2 (refused)."""
    global FORK, PINNED_COMMIT, LUA_DIR, OUT, DEST
    import argparse
    import sys
    sys.path.insert(0, HERE)
    import toolchain as tc
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    tc.add_source_args(ap, required=False)
    ap.add_argument('--out-dir', default=os.path.dirname(DEST))
    a = ap.parse_args(argv)
    if a.src:
        try:
            info = tc.resolve_source(a.src, a.lock, a.unlocked, a.commit)
        except tc.SourceError as ex:
            log('extract_lua: --src refused: %s' % ex)
            return 2
        tc.use_source(info, a.work)
        FORK, PINNED_COMMIT = tc.FORK, tc.PINNED_COMMIT
        LUA_DIR = os.path.join(FORK, r'CvGameCoreDLL_Expansion2\Lua')
        log('extract_lua: source %s (%s)' % (info['src'], info['note']))
    if a.work:
        OUT = os.path.join(os.path.abspath(a.work), 'lua')
        os.environ['VP_PROXY_GEN_WORK'] = os.path.abspath(a.work)
    DEST = os.path.join(os.path.abspath(a.out_dir), 'lua.json')
    return 0


def main(argv=None):
    rc = configure(argv)
    if rc:
        return rc
    t_start = time.time()
    os.makedirs(OUT, exist_ok=True)
    cpps = sorted(f for f in os.listdir(LUA_DIR) if f.startswith('CvLua') and f.endswith('.cpp'))
    srcs = [os.path.join(LUA_DIR, f) for f in cpps]

    # ---- 1. preprocess both arches (threads; work is in clang)
    pp = {}
    pp_bytes = 0
    with concurrent.futures.ThreadPoolExecutor(16) as ex:
        futs = {ex.submit(preprocess, a, s): (a, s) for a in ('x86', 'x64') for s in srcs}
        # ---- 2. AST in processes, started at the same time
        with concurrent.futures.ProcessPoolExecutor(6) as pex:
            ast_srcs = [s for s in srcs if os.path.basename(s)[:-4] in LIBS]
            afuts = [pex.submit(ast_worker, s) for s in ast_srcs]
            for f in concurrent.futures.as_completed(futs):
                a, s = futs[f]
                res, size = f.result()
                pp[(a, s)] = res
                pp_bytes += size
            log('preprocessed %d TUs (%.1f MB total), %.1fs' % (len(pp), pp_bytes / 1e6, time.time() - t_start))
            ast = {}
            ast_stats = []
            for f in concurrent.futures.as_completed(afuts):
                r = f.result()
                ast_stats.append((os.path.basename(r['src']), r['bytes'], r['seconds'], r['methods_in_tu']))
                for k, v in r['funcs'].items():
                    if k not in ast or len(v) > len(ast[k]):
                        ast[k] = v
            log('AST: %d TUs, %.0f MB streamed, %d CvLua function bodies, %.1fs' %
                (len(ast_stats), sum(x[1] for x in ast_stats) / 1e6, len(ast), time.time() - t_start))

    # keep filtered preprocessor output (small) for evidence
    for (a, s), res in pp.items():
        key = s.lower()
        lines = res.get(key, {})
        with open(os.path.join(OUT, 'pp_%s_%s.txt' % (a, os.path.basename(s)[:-4])), 'w', encoding='utf-8') as fh:
            for ln in sorted(lines):
                fh.write('%d\t%s\n' % (ln, lines[ln]))

    # ---- nonblank sets per (arch, lua file) (union over TUs that include it)
    nonblank = collections.defaultdict(set)
    for (a, s), res in pp.items():
        for f, lines in res.items():
            nonblank[(a, f)].update(lines.keys())

    lua_files = sorted(os.path.join(LUA_DIR, f) for f in os.listdir(LUA_DIR)
                       if f.startswith('CvLua') and f.endswith(('.cpp', '.h')))
    src_lines, masked, conds, stripped = {}, {}, {}, {}
    for f in lua_files:
        with open(f, encoding='utf-8', errors='replace') as fh:
            text = fh.read().replace('\r\n', '\n')
        st = strip_comments(text)
        stripped[f] = st.split('\n')
        src_lines[f] = text.split('\n')
        for a in ('x86', 'x64'):
            act, cond = active_mask(src_lines[f], nonblank.get((a, f.lower()), set()))
            sl = stripped[f]
            masked[(a, f)] = '\n'.join(sl[i] if act[i] else '' for i in range(len(sl)))
            conds[(a, f)] = (act, cond)

    # Issue 3519: master's type names -> wrapper classes, from every source of the Lua folder (!CvLuaApi.h included)
    api_texts = {}
    for fn in sorted(os.listdir(LUA_DIR)):
        if fn.endswith(('.h', '.cpp')):
            with open(os.path.join(LUA_DIR, fn), encoding='utf-8', errors='replace') as fh:
                api_texts[fn] = strip_comments(fh.read().replace('\r\n', '\n'))
    TYPE_MAP.clear()
    TYPE_MAP.update(build_type_map(api_texts))
    api_form = 'master_luaex' if TYPE_MAP else 'pin'
    if TYPE_MAP:
        log('Lua API form: master (#13489, luaEx_*); types %s' % ', '.join('%s=%s' % kv for kv in sorted(TYPE_MAP.items())))

    funcs = {}
    for a in ('x86', 'x64'):
        for f in lua_files:
            for k, v in find_functions(f, masked[(a, f)]).items():
                funcs.setdefault(k, {}).setdefault(a, (f,) + v)

    # ---- registrations
    regs = {'x86': [], 'x64': []}       # (lib, luaname, cfunc, file, line, form)
    others = {'x86': [], 'x64': []}
    for (a, s), res in pp.items():
        cls = os.path.basename(s)[:-4]
        lib = LIBS.get(cls, (cls.replace('CvLua', ''),))[0]
        r_regs, r_others = scan_registrations(res.get(s.lower(), {}), cls, lib, s)
        regs[a].extend(r_regs)
        others[a].extend(r_others)

    # ---- raw grep reconciliation
    raw_total, raw_linestart = 0, 0
    recon = []
    reg_index = {a: {(r[3].lower(), r[4]) for r in regs[a]} for a in regs}
    for f in sorted(x for x in lua_files if x.endswith('.cpp')):
        for i, l in enumerate(src_lines[f]):
            for m in RAW_METHOD.finditer(l):
                raw_total += 1
                if re.match(r'^\s*Method\(', l):
                    raw_linestart += 1
                ln = i + 1
                in_code = stripped[f][i][m.start():m.end()] == m.group(0)
                comp = {a: (f.lower(), ln) in reg_index[a] for a in ('x86', 'x64')}
                if comp['x86'] and comp['x64']:
                    continue
                entry = {'file': rel(f), 'line': ln, 'text': ' '.join(l.split())[:160], 'token': m.group(0),
                         'compiled': comp}
                if l.lstrip().startswith('#define'):
                    entry['kind'] = 'macro_definition'
                elif not in_code:
                    entry['kind'] = 'commented_out'
                elif not conds[('x86', f)][0][i] or not conds[('x64', f)][0][i]:
                    entry['kind'] = 'if_ed_out'
                    c86, c64 = conds[('x86', f)][1][i], conds[('x64', f)][1][i]
                    entry['condition'] = c86 or c64
                    entry['detail'] = 'inactive in %s' % ', '.join(
                        a for a in ('x86', 'x64') if not conds[(a, f)][0][i])
                elif not l.lstrip().startswith('Method('):
                    entry['kind'] = 'not_a_registration'
                else:
                    entry['kind'] = 'unexplained'
                recon.append(entry)

    # duplicates and renamed
    for a in ('x86', 'x64'):
        seen = collections.defaultdict(list)
        for r in regs[a]:
            seen[(r[0], r[1])].append(r)
            if r[2].split('::l', 1)[1] != r[1]:
                recon.append({'kind': 'registered_under_different_name', 'arch': a, 'file': rel(r[3]),
                              'line': r[4], 'lua_name': '%s.%s' % (r[0], r[1]), 'cfunc': r[2]})
        for k, lst in sorted(seen.items()):
            if len(lst) > 1:
                recon.append({'kind': 'duplicate_registration', 'arch': a, 'id': '%s.%s' % k,
                              'file': rel(lst[0][3]), 'lines': [x[4] for x in lst],
                              'detail': 'same Lua name set %d times in the table; last write wins' % len(lst)})

    # ---- methods
    methods = {}
    for a in ('x86', 'x64'):
        for lib, name, cfunc, s, ln, form in regs[a]:
            mid = '%s.%s' % (lib, name)
            m = methods.get(mid)
            if m is None:
                m = methods[mid] = {'id': mid, 'library': lib, 'name': name, 'cfunc': cfunc,
                                    'registration': form, 'registered_at': [],
                                    'compiled': {'x86': False, 'x64': False}}
            m['compiled'][a] = True
            loc = '%s:%d' % (rel(s), ln)
            if loc not in m['registered_at']:
                m['registered_at'].append(loc)
    # Fractal and other closures
    fractal_names = {'Create': 'static', 'CreateRifts': 'static', 'GetHeight': 'object', 'BuildRidges': 'object',
                     '__gc': 'object'}
    other_regs = []
    for a in ('x86', 'x64'):
        for lib, name, cfunc, s, ln, nup in others[a]:
            entry = {'library': lib, 'name': name, 'cfunc': cfunc, 'file': rel(s), 'line': ln, 'upvalues': nup}
            if entry not in other_regs:
                other_regs.append(entry)
            # Issue 3519: master's CvLuaExe.cpp (the global table Exe, upstream PR 13379) registers its functions as
            # Fractal does its static ones: lua_pushcclosure(L, lName, 0); lua_setfield(L, -2, "Name").
            exe = lib == 'Exe' and name and nup == 0
            if (lib == 'Fractal' and name in fractal_names) or exe:
                mid = '%s.%s' % (lib, name)
                m = methods.get(mid)
                if m is None:
                    m = methods[mid] = {'id': mid, 'library': lib, 'name': name, 'cfunc': cfunc,
                                        'registration': 'exe_global' if exe else 'fractal_%s' % fractal_names[name],
                                        'registered_at': [], 'compiled': {'x86': False, 'x64': False}}
                m['compiled'][a] = True
                loc = '%s:%d' % (rel(s), ln)
                if loc not in m['registered_at']:
                    m['registered_at'].append(loc)
    # Issue 3503: `others` follows the order in which the preprocessor threads finished; sorted, so that two runs on
    # one source give the same lua.json.
    other_regs.sort(key=lambda o: (o['file'], o['line'], o['library'], str(o['name']), o['cfunc'], o['upvalues']))

    callee_stats = collections.Counter()
    unknown_callees = collections.Counter()

    def resolve_refs(cfunc, seen):
        refs = []
        for r in ast.get(cfunc, []):
            kind, cls, name = r[0], r[1], r[2]
            if kind != 'func' and cls.startswith('CvLua') and name not in LUA_PLUMBING:
                sub = '%s::%s' % (cls, name)
                if sub in ast and sub not in seen:
                    seen.add(sub)
                    refs.extend(resolve_refs(sub, seen))
                    continue
            refs.append(r)
        return refs

    for mid, m in methods.items():
        cls = m['cfunc'].split('::')[0]
        lib, inst_type, kind = LIBS.get(cls, (m['library'], None, 'instance'))
        m['instance_type'] = inst_type if kind != 'fractal' else (
            None if m['registration'] == 'fractal_static' else 'CvFractal (userdata upvalue)')
        m['self'] = kind == 'instance' or m['registration'] == 'fractal_object'
        fdef = funcs.get(m['cfunc'], {})
        d = fdef.get('x86') or fdef.get('x64')
        m['file'] = rel(d[0]) if d else None
        m['line'] = d[1] if d else None
        body = d[2] if d else ''
        flags = set()
        # AST callees
        refs = resolve_refs(m['cfunc'], {m['cfunc']})
        wrapper = None
        dm = re.match(r'^\{\s*return\s+(?:(CvLua\w+)\s*::\s*)?(l\w+)\s*\(\s*L\s*\)\s*;\s*\}$', body)
        if dm:
            target = '%s::%s' % (dm.group(1) or cls, dm.group(2))
            td = funcs.get(target, {})
            td = td.get('x86') or td.get('x64')
            if td:
                m['delegates_to'] = target
                body = td[2]
        for _ in range(2):
            for hm in list(re.finditer(r'\breturn\s+(?:(CvLua\w+)\s*::\s*)?(\w+)\s*\(\s*L\b[^;]*;', body)):
                target = '%s::%s' % (hm.group(1) or cls, hm.group(2))
                td = funcs.get(target, {})
                td = td.get('x86') or td.get('x64')
                if td and target != m['cfunc'] and hm.group(2) != 'BasicLuaMethod':
                    m.setdefault('inlined_helpers', []).append(target)
                    body = body.replace(hm.group(0), 'return INLINED_HELPER;') + '\n' + td[2]
        wm = re.search(r'\bBasicLuaMethod\s*(?:<[^>]*>)?\s*\(\s*L\s*,\s*&\s*([\w:]+)', body)
        if wm and any(r[2] in ('BasicLuaMethod', 'luaEx_exec') for r in refs):   # luaEx_exec: master (3519)
            wname = wm.group(1).split('::')[-1]
            cand = [r for r in refs if r[0] == 'ref' and r[2] == wname]
            if cand:
                wrapper = cand[0]
        m['wrapper'] = ('%s::%s' % (wrapper[1], wrapper[2])) if wrapper else None
        calls = []
        seen_calls = set()
        effects = collections.OrderedDict()
        hidden = set()
        for r in refs:
            kind_r, rcls, rname, rtyp, rstatic, rconst = r
            eff, rule, hid = callee_effect(kind_r, rcls, rname, rtyp, rstatic, rconst)
            if hid:
                hidden.add(hid)
            if eff in ('plumbing', 'local'):
                continue
            fn = ('%s::%s' % (rcls, rname)) if rcls else rname
            if fn in seen_calls:
                continue
            seen_calls.add(fn)
            calls.append({'fn': fn, 'sig': rtyp, 'const': bool(rconst), 'static': bool(rstatic),
                          'effect': eff, 'rule': rule})
            effects.setdefault(eff, []).append(fn)
            callee_stats[eff] += 1
            if eff == 'unknown':
                unknown_callees[fn] += 1
        m['calls'] = calls
        # shape
        if wrapper:
            ret, wargs = split_sig(wrapper[3])
            start = 2 if kind == 'instance' else 1
            args = []
            if kind == 'instance':
                args.append({'pos': 1, 'accessor': 'GetInstance', 'kind': 'instance', 'instance': lib,
                             'optional': False})
            for i, t in enumerate(wargs):
                a_ = {'pos': start + i, 'accessor': 'CvLuaArgs::toValue<%s>' % t,
                      'kind': cpp_type_kind(t), 'optional': False}
                if a_['kind'] == 'instance':
                    a_['instance'] = cpp_instance(t)
                args.append(a_)
            results = {'counts': [0] if ret == 'void' else [1], 'computed_count': False,
                       'kinds': [] if ret == 'void' else ['boolean' if ret == 'bool' else 'integer'],
                       'cpp_return': ret}
            m['shape_rule'] = 'wrapper_signature[C]'
        elif body:
            args, results, flags = parse_shape(body, lib, kind, lib)
            m['shape_rule'] = 'textual_body_parse[I]'
        else:
            args, results = [], {'counts': [], 'kinds': []}
            m['shape_rule'] = 'no_body_found'
        m['args'] = args
        m['results'] = results
        # access
        side = [e for e in ('net', 'write', 'ui') if e in effects]
        returns_value = bool(results.get('kinds')) and results.get('kinds') != ['nil'] and \
            max(results.get('counts') or [0]) > 0
        if not m['calls'] and not body and not wrapper:
            access, rule = 'unknown', 'no_body_or_ast'
        elif side:
            first = effects[side[0]][0]
            access = 'mixed' if returns_value else 'write'
            rule = '%s:%s%s' % (side[0], first, ' (+returns values)' if returns_value else '')
        elif 'unknown' in effects:
            access, rule = 'unknown', 'unclassified_callee:%s' % effects['unknown'][0]
        elif 'read' in effects:
            allc = all(c['rule'] == 'const_member[C]' for c in calls)
            access, rule = 'read', ('const_members_only[C]' if allc else
                                    'getters(const or by name)[I]:' + ','.join(
                                        c['fn'] for c in calls if c['rule'] != 'const_member[C]')[:200])
        else:
            access, rule = 'read', 'no_game_calls'
        if m['library'] == 'Fractal':
            flags.add('lua_state_access')
        if access == 'mixed' and 'instance' in (results.get('kinds') or []) and any(
                re.match(r'(?i)(init|create|found|add|spawn|buy|purchase|convert|upgrade|acquire)', c['fn'].split(
                    '::')[-1]) for c in calls if c['effect'] in ('write', 'net')):
            flags.add('returns_new_instance')
        if lib == 'Map' and name_is(m['name'], ('Areas',)) or m['name'] in ('Cities', 'Units', 'UnitsInSquad'):
            flags.add('returns_iterator')
        m['access'] = access
        m['access_rule'] = rule
        m['special'] = sorted(flags)
        m['depends_on'] = sorted(hidden)
        arg_ok = all(a['kind'] in SCALAR | {'instance', 'any'} and isinstance(a['pos'], int) for a in args)
        res_ok = set(results.get('counts') or []) <= {0, 1} and \
            not results.get('computed_count') and \
            all(k in SCALAR | {'instance'} for k in results.get('kinds') or [])
        m['cacheable'] = access == 'read' and not (flags - {'variable_args'}) and arg_ok and res_ok
        m['pure_of_args'] = m['cacheable'] and not hidden

    # ---- traffic
    with open(BENCH, encoding='utf-8') as fh:
        bench = json.load(fh)
    scen_totals = {}
    for sc, v in bench.items():
        lm = v.get('lua_main', {})
        scen_totals[sc] = {'total': lm.get('total', 0), 'distinct': lm.get('distinct', 0),
                           'top_recorded': sum(x[1] for x in lm.get('top', [])), 'top_len': len(lm.get('top', []))}
        for name, calls, returning in lm.get('top', []):
            m = methods.get(name)
            if m is None:
                continue
            m.setdefault('traffic', {})[sc] = {'calls': calls, 'returning': returning}
    unmatched = sorted({x[0] for v in bench.values() for x in v.get('lua_main', {}).get('top', [])
                        if x[0] not in methods})
    for m in methods.values():
        m.setdefault('traffic', {})

    # ---- summary
    ids = sorted(methods)
    by_lib = collections.Counter(methods[i]['library'] for i in ids)
    by_access = collections.Counter(methods[i]['access'] for i in ids)
    by_special = collections.Counter(s for i in ids for s in methods[i]['special'])
    by_rule = collections.Counter(methods[i]['access_rule'].split(':')[0] for i in ids)
    by_depends = collections.Counter(s for i in ids for s in methods[i]['depends_on'])
    cacheable = sum(1 for i in ids if methods[i]['cacheable'])
    pure = sum(1 for i in ids if methods[i]['pure_of_args'])
    wrapped = sum(1 for i in ids if methods[i]['wrapper'])

    def share(sc):
        tot = scen_totals[sc]['total']
        top = bench[sc]['lua_main']['top']
        if not tot:
            return None
        rec = sum(x[1] for x in top)
        def s(pred):
            return round(sum(x[1] for x in top if x[0] in methods and pred(methods[x[0]])) / tot, 4)
        top10 = sorted(top, key=lambda x: -x[1])[:10]
        return {'lua_calls_total': tot, 'top25_recorded_calls': rec, 'top25_share_of_total': round(rec / tot, 4),
                'read_share': s(lambda m: m['access'] == 'read'),
                'cacheable_share': s(lambda m: m['cacheable']),
                'pure_of_args_share': s(lambda m: m['pure_of_args']),
                'hidden_state_share': s(lambda m: bool(m['depends_on'])),
                'write_or_mixed_share': s(lambda m: m['access'] in ('write', 'mixed')),
                'top10_share': round(sum(x[1] for x in top10) / tot, 4),
                'top10': [[x[0], x[1], methods[x[0]]['access'] if x[0] in methods else None,
                           methods[x[0]]['cacheable'] if x[0] in methods else None] for x in top10],
                'unrecorded_share': round(1 - rec / tot, 4)}

    traffic_summary = {sc: share(sc) for sc in ('f1_aiturn_350', 'f2_aiturn_351', 'b_panzoom', 'e1_tooltips',
                                                 'a_idle', 'c_cityview', 'e2_diplo_overview')}
    comp86 = collections.Counter(methods[i]['library'] for i in ids if methods[i]['compiled']['x86'])
    comp64 = collections.Counter(methods[i]['library'] for i in ids if methods[i]['compiled']['x64'])
    regs86 = collections.Counter(r[0] for r in regs['x86'])
    regs64 = collections.Counter(r[0] for r in regs['x64'])
    only86 = [i for i in ids if methods[i]['compiled']['x86'] and not methods[i]['compiled']['x64']]
    only64 = [i for i in ids if methods[i]['compiled']['x64'] and not methods[i]['compiled']['x86']]
    recon_kinds = collections.Counter(r['kind'] for r in recon)

    counts = {
        'raw_grep_Method_tokens': raw_total,
        'raw_grep_Method_tokens_at_line_start': raw_linestart,
        'compiled_registrations_x86': len(regs['x86']),
        'compiled_registrations_x64': len(regs['x64']),
        'compiled_registrations_x86_by_library': dict(sorted(regs86.items())),
        'compiled_registrations_x64_by_library': dict(sorted(regs64.items())),
        'distinct_methods_x86_by_library': dict(sorted(comp86.items())),
        'distinct_methods_x64_by_library': dict(sorted(comp64.items())),
        'distinct_methods_x86': sum(comp86.values()),
        'distinct_methods_x64': sum(comp64.values()),
        'methods_total_in_json': len(ids),
        'only_x86': only86,
        'only_x64': only64,
        'reconciliation_by_kind': dict(sorted(recon_kinds.items())),
        'reconciliation_formula': 'raw_grep_Method_tokens = compiled_registrations_x86 + entries of '
                                  'reconciliation with compiled.x86 false (macro definitions, commented out, '
                                  '#if-ed out, non-registrations); duplicate_registration entries are compiled '
                                  'registrations of one Lua name more than once',
        'other_registrations': other_regs,
        'data_tables': ['GameInfoTypes, GameInfoActions (CvLuaGameInfo.cpp: data only, no functions)',
                        'enum globals (CvLuaEnums.cpp: integer tables, no functions)',
                        'LuaTypes.<Type> tables and Players[]/Teams[] arrays (instance tables, see libraries)'],
        # issue 3503: the seconds per TU are left out (a run measurement, not a property of the source)
        'ast': {'tus': sorted([n, b, m] for n, b, _s, m in ast_stats),
                'note': 'x86 configuration only; clang text AST streamed, not stored; [TU, AST bytes, methods in TU]'},
        'preprocessed_mb_total': round(pp_bytes / 1e6, 1),
    }
    if api_form != 'pin':
        # Issue 3519: only for master's form, so that a pin source regenerates the committed lua.json unchanged.
        luafsq = []
        for f in lua_files:
            if not f.endswith('.cpp'):
                continue
            per = {}
            for a in ('x86', 'x64'):
                for form, name, wraps, line, glob in scan_luafsq(masked[(a, f)]):
                    e = per.setdefault((line, name), {'global': glob, 'name': name, 'form': 'luafsq_' + form,
                                                      'wraps': wraps, 'file': rel(f), 'line': line,
                                                      'compiled': {'x86': False, 'x64': False}})
                    e['compiled'][a] = True
            luafsq.extend(per[k] for k in sorted(per))
        counts['lua_api_form'] = {
            'form': api_form,
            'types': dict(sorted(TYPE_MAP.items())),
            'note': 'upstream #13489 (Lua/!CvLuaApi*.h): Method(Name) fills CreateFunctable\'s table at stack index 1; '
                    'lua_pushcclosure is #defined to luaVp_pushcclosure (every C function crosses as a C closure '
                    'with one upvalue, protectedWrapperAux); luaEx_to<Name>/luaEx_push<Name>/luaEx_exec are read as '
                    'the pin\'s CvLua<Lib>::GetInstance/Push/BasicLuaMethod (normalise_body)'}
        counts['luafsq_registrations'] = luafsq
    libraries = []
    for cls, (lib, inst, kind) in sorted(LIBS.items(), key=lambda x: x[1][0]):
        if not os.path.exists(os.path.join(LUA_DIR, cls + '.cpp')):
            continue   # issue 3519: CvLuaExe exists only on master
        glob = LIB_GLOBALS_MASTER.get(lib, LIB_GLOBALS.get(lib)) if api_form != 'pin' else LIB_GLOBALS.get(lib)
        libraries.append({'name': lib, 'cpp_class': cls, 'instance_type': inst, 'kind': kind,
                          'lua_global': glob, 'self_arg': kind == 'instance',
                          'file': 'CvGameCoreDLL_Expansion2/Lua/%s.cpp' % cls,
                          'methods_x86': comp86.get(lib, 0), 'methods_x64': comp64.get(lib, 0),
                          'other_functions': [o for o in other_regs if o['library'] == lib]})
    summary = {
        'by_library': dict(sorted(by_lib.items())),
        'by_access': dict(sorted(by_access.items())),
        'by_access_rule': dict(sorted(by_rule.items())),
        'by_special': dict(sorted(by_special.items())),
        'by_depends_on': dict(sorted(by_depends.items())),
        'wrapper_methods': wrapped,
        'cacheable_mechanically': cacheable,
        'cacheable_definition': 'access=read, no special shape except variable_args, every arg an integer/number/boolean/string/'
                                'instance at a fixed position, results 0 or 1 value of scalar or instance kind',
        'pure_of_args': pure,
        'pure_of_args_definition': 'cacheable and no call to an active-player / UI-state / RNG source; the result '
                                   'still depends on game state, so a cache is valid only until the next state '
                                   'change',
        'callee_effects': dict(sorted(callee_stats.items())),
        'unclassified_callees_top': unknown_callees.most_common(40),
        'access_definitions': {
            'read': 'all game calls are const members [C] or non-const getters by name [I]',
            'write': 'calls a mutator / net send / engine UI call and pushes no result: can be posted',
            'mixed': 'calls a mutator / net send / engine UI call and returns values: needs a synchronous round '
                     'trip',
            'unknown': 'calls a non-const member whose name is neither getter- nor mutator-like (listed in '
                       'access_rule)'},
        'traffic': traffic_summary,
        'traffic_note': 'results.json records only the top 25 methods per scenario (lua_main.top); shares are of '
                        'lua_main.total, so unrecorded_share is unattributed. Scenarios d_techtree and '
                        'e3_econ_overview recorded 0 Lua calls.',
        'traffic_unmatched_names': unmatched,
        'confidence': '[C] registrations (preprocessor), callee resolution and constness (clang AST); '
                      '[I] getter/mutator by name, argument/result shapes and special flags from the body text',
    }
    doc = {
        'generated_by': 'proxy/gen/extract_lua.py',
        'generated_at': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
        'source': {'fork_path': FORK.replace('\\', '/'), 'commit': PINNED_COMMIT},
        'counts': counts,
        'reconciliation': recon,
        'libraries': libraries,
        'methods': [methods[i] for i in ids],
        'summary': summary,
    }
    os.makedirs(os.path.dirname(DEST), exist_ok=True)
    text = json.dumps(doc, indent=1, ensure_ascii=True, sort_keys=False)
    bad = [c for c in text if ord(c) < 32 and c not in '\n']
    if bad:
        raise RuntimeError('control characters in JSON: %r' % bad[:5])
    with open(DEST, 'w', encoding='ascii', newline='\n') as fh:
        fh.write(text + '\n')

    log('raw grep Method(...) tokens: %d (%d at line start)' % (raw_total, raw_linestart))
    log('compiled registrations x86 %d, x64 %d; distinct methods x86 %d, x64 %d; only-x86 %d, only-x64 %d' %
        (len(regs['x86']), len(regs['x64']), counts['distinct_methods_x86'], counts['distinct_methods_x64'],
         len(only86), len(only64)))
    log('by library (x86):', dict(sorted(regs86.items())))
    log('reconciliation:', dict(sorted(recon_kinds.items())))
    log('access:', dict(sorted(by_access.items())))
    log('special:', dict(sorted(by_special.items())))
    log('depends_on:', dict(sorted(by_depends.items())))
    log('wrapper %d, cacheable %d, pure_of_args %d' % (wrapped, cacheable, pure))
    for sc, v in traffic_summary.items():
        if v:
            log('traffic %s: total %d read %.3f cacheable %.3f pure %.3f hidden %.3f top10 %.3f unrecorded %.3f' %
                (sc, v['lua_calls_total'], v['read_share'], v['cacheable_share'], v['pure_of_args_share'],
                 v['hidden_state_share'], v['top10_share'], v['unrecorded_share']))
    log('wrote %s (%d methods) in %.1fs' % (rel(DEST), len(ids), time.time() - t_start))
    return 0


def name_is(n, names):
    return n in names


if __name__ == '__main__':
    import sys as _sys
    _sys.exit(main())
