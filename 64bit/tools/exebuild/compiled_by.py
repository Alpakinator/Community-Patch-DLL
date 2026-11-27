"""compiled_by.py - which test targets and scripts compile or include the given source files (ticket 0605, epic 0590).

    python -I tools/exebuild/compiled_by.py <file> [<file> ...] [--json] [--all] [--no-partners] [--root DIR]

A carry (or any change to a product source) lists in its acceptance every test target that compiles one of the touched
files, found by this tool, and shows each one passing in its hand-back (the rule's text: ticket 0605's log; the MC
numbers it in COMMON). The farm guard test did not compile from carry 4965 on and nobody noticed: no suite the carrier
ran built it (research/02-exe-internals/exe-build-table.md, the carry rule).

What it reads, statically (nothing is built or run; standard library only):
  - batch and shell scripts (*.cmd, *.bat, *.sh): every command line (with ^ / \\ continuations) that names a C or C++
    source; %VAR% / $VAR set in the same script are expanded (%~dp0 = the script's folder); /Fe, /OUT: and -o name the
    output;
  - Python scripts (*.py): path expressions the script can evaluate without running (Path(__file__), .parent,
    .parents[n], `/`, os.path.join, .glob('*.cpp'), repo-relative string constants) that end in a source or header;
  - CMake (CMakeLists.txt): a small interpreter of set, list(APPEND), file(GLOB), string(REPLACE), foreach,
    function/macro, add_subdirectory, add_executable/add_library/target_sources/target_link_libraries/add_test, with
    every if branch taken; a test target links a library that compiles the file -> listed;
  - #include "x" of every C/C++ file: a header, or a .cpp included by another file, reaches every file that includes it
    (transitively), and the targets compiling those;
  - runners: a script that names a built output (farm_guard_test.exe) runs that target; a script that names a CMake
    library (c5render) links it.
Partners (level 2, --no-partners turns them off): a test target that compiles the file together with another product
source P shares a contract with P's own tests, so P's test targets are listed too, marked "partner". Example:
proxy/tests/test_farm_guard_proxy.py compiles proxy/src/proxy_farm_guard.cpp with shell/composite/farm_guard.cpp, so
shell/build.cmd's farm_guard_test.exe (shell/tests/farm_guard_test.cpp, run by shell/tests/test_farm_guard.py) is listed
for proxy_farm_guard.cpp.

Not seen: a builder that takes the list of sources from another file or from the command line, f-strings, sources
written by a test at run time (canaries), code generators' outputs, and the Python builders that link a prebuilt
library by a path argument without naming the library (they are listed when they name it, e.g. --c5render). Folders
named `evidence`, and research/, tracker/, docs/ are not read. Exit 0 with the report; exit 2 when a given file is not
in the tree.
"""
from __future__ import annotations

import argparse
import ast
import fnmatch
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_DEFAULT = HERE.parents[1]

SRC_EXT = ('.c', '.cc', '.cpp', '.cxx')
HDR_EXT = ('.h', '.hh', '.hpp', '.hxx', '.inl')
CODE_EXT = SRC_EXT + HDR_EXT
SCRIPT_EXT = ('.cmd', '.bat', '.sh', '.py')
SKIP_PARTS = {'evidence', '.git', '__pycache__', 'node_modules', '.claude'}
SKIP_TOP = {'research', 'tracker', 'docs'}
SELF = {'tools/exebuild/compiled_by.py', 'tools/exebuild/tests/test_compiled_by.py'}   # name sources and outputs as examples: never read
PARTNER_MAX = 3           # partners come only from test targets with at most this many other product sources
CMAKE_COLLAPSE = 8      # more CMake test targets than this from one project are summarised (--all lists them)


# ---------------------------------------------------------------------------------------------------------------------
# The tree


class Tree:
    """The repository's files (repo-relative posix strings) and lookups by basename."""

    def __init__(self, root: Path):
        self.root = root.resolve()
        self.files = sorted(self._list())
        self.fileset = set(self.files)
        self.by_base = {}
        for f in self.files:
            self.by_base.setdefault(f.rsplit('/', 1)[-1].lower(), []).append(f)
        self._lower = {f.lower(): f for f in self.files}

    def _list(self):
        out = None
        if (self.root / '.git').exists():
            try:
                p = subprocess.run(['git', '-C', str(self.root), 'ls-files', '-z', '--cached', '--others',
                                    '--exclude-standard'], capture_output=True, timeout=120)
                if p.returncode == 0:
                    out = [x for x in p.stdout.decode('utf-8', 'replace').split('\0') if x]
            except (OSError, subprocess.SubprocessError):
                out = None
        if out is None:
            out = []
            for d, dirs, files in os.walk(self.root):
                dirs[:] = [x for x in dirs if x not in SKIP_PARTS]
                rel = Path(d).relative_to(self.root).as_posix()
                for f in files:
                    out.append(f if rel == '.' else rel + '/' + f)
        keep = []
        for f in out:
            parts = f.split('/')
            if parts[0] in SKIP_TOP or SKIP_PARTS.intersection(parts[:-1]):
                continue
            if (self.root / f).is_file():
                keep.append(f)
        return keep

    def rel(self, p) -> str | None:
        """Repo-relative path of an absolute or repo-relative path if it is a file of the tree (case-insensitive)."""
        try:
            pp = Path(os.path.normpath(str(p)))
            if pp.is_absolute():
                pp = Path(os.path.normpath(os.path.abspath(pp)))
                r = os.path.relpath(pp, self.root)
            else:
                r = str(pp)
        except ValueError:
            return None
        r = r.replace('\\', '/')
        if r.startswith('../'):
            return None
        return self._lower.get(r.lower())

    def glob(self, folder_rel: str, pattern: str, recursive=False):
        pre = '' if folder_rel in ('', '.') else folder_rel.rstrip('/') + '/'
        out = []
        for f in self.files:
            if not f.startswith(pre):
                continue
            rest = f[len(pre):]
            if not recursive and '/' in rest:
                continue
            name = rest if recursive and '/' in pattern else rest.rsplit('/', 1)[-1]
            if fnmatch.fnmatch(name.lower(), pattern.lower()):
                out.append(f)
        return out

    def read(self, rel: str) -> str:
        try:
            return (self.root / rel).read_text(encoding='utf-8', errors='replace')
        except OSError:
            return ''


def is_test_path(rel: str) -> bool:
    parts = rel.lower().split('/')
    if {'tests', 'test', 'rt_tests', 'testapps'}.intersection(parts[:-1]):
        return True
    b = parts[-1]
    return bool(re.match(r'test_|.*_test\.|.*_tests\.|.*selftest', b))


def is_test_name(name: str) -> bool:
    return bool(re.search(r'test', name, re.I))


class Target:
    """One build of one output: a script line, a Python script, or a CMake target."""

    def __init__(self, kind, script, line, name, sources, outputs=(), is_test=None, project=None):
        self.kind = kind              # 'cmd', 'py', 'cmake'
        self.script = script          # repo-relative file that defines it
        self.line = line
        self.name = name
        self.sources = set(sources)   # repo-relative files it compiles (or names, for py)
        self.outputs = set(outputs)   # basenames, lower case
        self.links = set()            # CMake: libraries linked
        self.uses = set()             # Python: other scripts it loads (a builder that reuses another's source list)
        self.library = False
        self.project = project        # CMake: the project's top CMakeLists folder
        self.registered_test = False
        self._is_test = is_test

    @property
    def is_test(self) -> bool:
        if self._is_test is not None:
            return self._is_test
        if self.kind == 'py':
            return is_test_path(self.script)
        if self.kind == 'cmake':
            return self.registered_test or (not self.library and is_test_path(self.script))
        return any(is_test_name(o) for o in self.outputs) or any(is_test_path(s) for s in self.sources)

    def label(self) -> str:
        if self.kind == 'py':
            return self.script
        if self.kind == 'cmake':
            return '%s (CMake target %s, %s)' % (self.name, self.name, self.script)
        return '%s:%d %s' % (self.script, self.line, self.name)

    def programs(self):
        """The test-only sources of the target (its test program files)."""
        return sorted(s for s in self.sources if is_test_path(s))


# ---------------------------------------------------------------------------------------------------------------------
# Batch and shell scripts

_VAR_BAT = re.compile(r'%~dp0|%([A-Za-z_][\w()]*)%')
_VAR_SH = re.compile(r'\$\{(\w+)\}|\$(\w+)')


def _logical_lines(text: str, cont: str):
    buf, start = '', 0
    for i, ln in enumerate(text.splitlines(), 1):
        if not buf:
            start = i
        s = ln.rstrip()
        if s.endswith(cont):
            buf += s[:-1] + ' '
            continue
        buf += s
        yield start, buf
        buf = ''
    if buf:
        yield start, buf


def _expand_bat(s: str, env: dict, script_dir: str) -> str:
    def rep(m):
        if m.group(0) == '%~dp0':
            return script_dir + '\\'
        return env.get(m.group(1).upper(), m.group(0))
    for _ in range(6):
        t = _VAR_BAT.sub(rep, s)
        if t == s:
            break
        s = t
    return s


def _expand_sh(s: str, env: dict, script_dir: str) -> str:
    s = s.replace('$(dirname "$0")', script_dir).replace('$(dirname $0)', script_dir)
    for _ in range(6):
        t = _VAR_SH.sub(lambda m: env.get(m.group(1) or m.group(2), m.group(0)), s)
        if t == s:
            break
        s = t
    return s


def _resolve_token(tree: Tree, tok: str, script_rel: str):
    """A path token of a script -> repo-relative file, trying it as is, from the script's folder and its ancestors."""
    tok = tok.strip().strip('"\'').rstrip(',;)').replace('\\', '/')
    tok = re.sub(r'^(/Tp|/Tc|-c)', '', tok)
    if not tok or tok.startswith('$<'):
        return None
    unresolved = re.search(r'%[^%]*%|\$\{?\w+\}?', tok)
    rest = tok[unresolved.end():].lstrip('/') if unresolved else tok
    if not unresolved and os.path.isabs(tok):
        return tree.rel(tok)
    d = Path(script_rel).parent.as_posix()
    bases = []
    while True:
        bases.append(d)
        if d in ('', '.'):
            break
        d = Path(d).parent.as_posix()
    for b in bases:
        r = tree.rel(rest if b in ('', '.') else b + '/' + rest)
        if r:
            return r
    return None


def scan_script_lines(tree: Tree, rel: str):
    text = tree.read(rel)
    bat = rel.lower().endswith(('.cmd', '.bat'))
    script_dir = str((tree.root / rel).parent)
    env = {}
    targets = []
    for line_no, ln in _logical_lines(text, '^' if bat else '\\'):
        s = ln.strip()
        low = s.lower()
        if not s or low.startswith(('rem ', '::', '#', '@rem')) or low == 'rem':
            continue
        if bat:
            m = re.match(r'(?i)^set\s+"?([A-Za-z_][\w()]*)=(.*?)"?\s*$', s)
            if m:
                env[m.group(1).upper()] = _expand_bat(m.group(2), env, script_dir)
                continue
            x = _expand_bat(s, env, script_dir)
        else:
            m = re.match(r'^(?:export\s+)?(\w+)=(.*)$', s)
            raw = m.group(2).strip() if m else ''
            if m and ' ' not in re.sub(r'\$\(dirname "?\$0"?\)', '', raw).strip('"\''):
                env[m.group(1)] = _expand_sh(raw, env, script_dir).strip('"\'')
                continue
            x = _expand_sh(s, env, script_dir)
        try:
            toks = shlex.split(x.replace('\\', '/'), posix=False)
        except ValueError:
            toks = x.replace('\\', '/').split()
        srcs, outs = set(), set()
        for i, t in enumerate(toks):
            tt = t.strip('"\'')
            m = re.match(r'(?i)^(?:/Fe:?|-Fe:?|/OUT:|-OUT:)"?(.+?)"?$', tt)
            if m:
                outs.add(m.group(1).replace('\\', '/').rsplit('/', 1)[-1].lower())
                continue
            if tt == '-o' and i + 1 < len(toks):
                outs.add(toks[i + 1].strip('"\'').replace('\\', '/').rsplit('/', 1)[-1].lower())
                continue
            if tt.lower().endswith(SRC_EXT):
                r = _resolve_token(tree, tt, rel)
                if r:
                    srcs.add(r)
        if srcs:
            name = ', '.join(sorted(outs)) if outs else 'line %d' % line_no
            targets.append(Target('cmd', rel, line_no, name, srcs, outs))
    return targets


# ---------------------------------------------------------------------------------------------------------------------
# Python scripts

class _PyEval:
    """Evaluates the path expressions a script can compute without running: values are absolute Path or str."""

    def __init__(self, tree: Tree, rel: str):
        self.tree = tree
        self.file = (tree.root / rel)
        self.env = {'__file__': str(self.file)}
        self.found = set()
        self.scripts = set()      # other Python scripts it loads by path (importlib) or imports from its own folder

    def ev(self, n):
        try:
            return self._ev(n)
        except (ValueError, TypeError, IndexError, RecursionError, OSError):
            return None

    def _ev(self, n):
        if isinstance(n, ast.Constant) and isinstance(n.value, str):
            return n.value
        if isinstance(n, ast.Name):
            return self.env.get(n.id)
        if isinstance(n, ast.BinOp) and isinstance(n.op, ast.Div):
            a, b = self._ev(n.left), self._ev(n.right)
            if isinstance(a, Path) and isinstance(b, (str, Path)):
                return a / b
            return None
        if isinstance(n, ast.Attribute):
            v = self._ev(n.value)
            if isinstance(v, Path) and n.attr == 'parent':
                return v.parent
            if isinstance(v, Path) and n.attr == 'parents':
                return ('parents', v)
            return None
        if isinstance(n, ast.Subscript):
            v = self._ev(n.value)
            idx = n.slice.value if isinstance(n.slice, ast.Constant) else None
            if isinstance(v, tuple) and v[0] == 'parents' and isinstance(idx, int):
                return v[1].parents[idx]
            return None
        if isinstance(n, ast.Call):
            f = n.func
            fname = f.attr if isinstance(f, ast.Attribute) else f.id if isinstance(f, ast.Name) else ''
            if fname in ('Path', 'PurePath', 'PureWindowsPath', 'WindowsPath') and len(n.args) == 1:
                v = self._ev(n.args[0])
                if isinstance(v, (str, Path)):
                    p = Path(v)
                    return p if p.is_absolute() else (self.file.parent / p if str(v) != '.' else None)
                return None
            if isinstance(f, ast.Attribute):
                recv = self._ev(f.value)
                if fname in ('resolve', 'absolute') and isinstance(recv, Path):
                    return recv
                if fname in ('dirname',) and len(n.args) == 1:
                    v = self._ev(n.args[0])
                    return str(Path(v).parent) if isinstance(v, (str, Path)) else None
                if fname in ('abspath', 'realpath', 'normpath') and len(n.args) == 1:
                    v = self._ev(n.args[0])
                    return str(v) if isinstance(v, (str, Path)) else None
                if fname == 'join' and isinstance(f.value, ast.Attribute) and f.value.attr == 'path':
                    parts = [self._ev(a) for a in n.args]
                    if parts and all(isinstance(p, (str, Path)) for p in parts):
                        return str(Path(*[str(p) for p in parts]))
                    return None
                if fname in ('glob', 'rglob') and isinstance(recv, Path) and len(n.args) == 1:
                    pat = self._ev(n.args[0])
                    if isinstance(pat, str):
                        folder = self.tree.rel_dir(recv)
                        if folder is not None:
                            for r in self.tree.glob(folder, pat.split('/')[-1], recursive=(fname == 'rglob' or '**' in pat)):
                                if r.lower().endswith(CODE_EXT):
                                    self.found.add(r)
                    return None
        return None

    def note(self, v, script_rel):
        if isinstance(v, Path):
            r = self.tree.rel(v)
            if r and r.lower().endswith(CODE_EXT):
                self.found.add(r)
            elif r and r.lower().endswith('.py'):
                self.scripts.add(r)
        elif isinstance(v, str) and v.lower().endswith(CODE_EXT) and '\n' not in v and len(v) < 260:
            p = v.replace('\\', '/')
            for cand in ((self.tree.root / p) if '/' in p else None, self.file.parent / p):
                if cand is not None:
                    r = self.tree.rel(cand)
                    if r:
                        self.found.add(r)
                        break


def _tree_rel_dir(self: Tree, p: Path):
    try:
        r = os.path.relpath(os.path.normpath(str(p)), self.root).replace('\\', '/')
    except ValueError:
        return None
    if r.startswith('..'):
        return None
    return '' if r == '.' else r


Tree.rel_dir = _tree_rel_dir


_PY_CODE_LITERAL = re.compile(r"""\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl)['"]""", re.I)


def scan_python(tree: Tree, rel: str):
    text = tree.read(rel)
    # A builder names a source or header literally ('x.cpp', or glob('*.cpp')); a script that names none is no builder
    # (it may still load one, but then it does not compile anything itself: not listed).
    if not _PY_CODE_LITERAL.search(text):
        return []
    try:
        mod = ast.parse(text)
    except (SyntaxError, ValueError):
        return []
    ev = _PyEval(tree, rel)
    events = []
    for n in ast.walk(mod):
        if isinstance(n, ast.Assign):
            if len(n.targets) == 1 and isinstance(n.targets[0], ast.Name):
                events.append((n.lineno, 0, n))
        elif isinstance(n, ast.AnnAssign):
            if isinstance(n.target, ast.Name) and n.value is not None:
                events.append((n.lineno, 0, n))
        elif isinstance(n, ast.BinOp):
            if isinstance(n.op, ast.Div):
                events.append((n.lineno, 1, n))
        elif isinstance(n, ast.Constant):
            if isinstance(n.value, str) and n.value[-4:].lower().endswith(CODE_EXT + ('.py',)):
                events.append((n.lineno, 1, n))
        elif isinstance(n, ast.Call):
            if isinstance(n.func, ast.Attribute) and n.func.attr in ('glob', 'rglob'):
                events.append((n.lineno, 1, n))
        elif isinstance(n, (ast.Import, ast.ImportFrom)):
            events.append((n.lineno, 1, n))
    events.sort(key=lambda e: (e[0], e[1]))
    for _, kind, n in events:
        if kind == 0:
            name = n.targets[0].id if isinstance(n, ast.Assign) else n.target.id
            v = ev.ev(n.value)
            if isinstance(v, (str, Path)):
                ev.env[name] = v
        else:
            ev.note(ev.ev(n), rel)
        if isinstance(n, (ast.Import, ast.ImportFrom)):
            names = [x.name for x in n.names] if isinstance(n, ast.Import) else [n.module or '']
            for m in names:
                r = tree.rel((tree.root / rel).parent / (m.split('.')[0] + '.py'))
                if r:
                    ev.scripts.add(r)
    found = {f for f in ev.found if f != rel}
    uses = {f for f in ev.scripts if f != rel}
    if not found:
        return []
    outs = {m.lower() for m in re.findall(r"['\"]([\w.-]+\.(?:exe|dll))['\"]", text)}
    t = Target('py', rel, 1, rel.rsplit('/', 1)[-1], found, outs)
    t.uses = uses
    return [t]


# ---------------------------------------------------------------------------------------------------------------------
# CMake

_CM_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|\[\[(.*?)\]\]|([^\s()"#]+)|(#[^\n]*)|(\()|(\))', re.S)


def _cmake_commands(text: str):
    """[(name, [args], quoted flags, line)] of a CMakeLists text."""
    out = []
    pos, n = 0, len(text)
    head = re.compile(r'([A-Za-z_]\w*)\s*\(')
    comment = re.compile(r'#(\[\[.*?\]\]|[^\n]*)', re.S)
    while pos < n:
        m = comment.match(text, pos)
        if m:
            pos = m.end()
            continue
        m = head.match(text, pos)
        if not m:
            pos += 1
            continue
        name, line = m.group(1).lower(), text.count('\n', 0, m.start()) + 1
        pos = m.end()
        depth, args = 1, []
        while pos < n and depth:
            t = _CM_TOKEN.match(text, pos)
            if not t:
                pos += 1
                continue
            pos = t.end()
            if t.group(5):
                depth += 1
            elif t.group(6):
                depth -= 1
            elif t.group(4):
                continue
            elif t.group(1) is not None:
                args.append((t.group(1), True))
            elif t.group(2) is not None:
                args.append((t.group(2), True))
            else:
                args.append((t.group(3), False))
        out.append((name, args, line))
    return out


class _CMake:
    def __init__(self, tree: Tree):
        self.tree = tree
        self.targets = {}            # (project, name) -> Target
        self.functions = {}
        self.reached = set()

    def run_project(self, cm_rel: str):
        project = Path(cm_rel).parent.as_posix()
        project = '' if project == '.' else project
        self._run_file(cm_rel, project, {'CMAKE_SOURCE_DIR': str(self.tree.root / project),
                                         'PROJECT_SOURCE_DIR': str(self.tree.root / project)}, 0)

    def _run_file(self, cm_rel, project, env, depth):
        if cm_rel in self.reached or depth > 12:
            return
        self.reached.add(cm_rel)
        d = str((self.tree.root / cm_rel).parent)
        scope = dict(env)
        scope['CMAKE_CURRENT_SOURCE_DIR'] = d
        scope['CMAKE_CURRENT_LIST_DIR'] = d
        self._exec(_cmake_commands(self.tree.read(cm_rel)), scope, cm_rel, project, depth)

    def _expand(self, s, env):
        for _ in range(8):
            t = re.sub(r'\$\{(\w+)\}', lambda m: env.get(m.group(1), ''), s)
            if t == s:
                break
            s = t
        return s

    def _args(self, args, env):
        out = []
        for a, quoted in args:
            v = self._expand(a, env)
            if quoted:
                out.append(v)
            else:
                out.extend(x for x in v.split(';') if x != '')
        return out

    def _blocks(self, cmds, i, opener, closer):
        depth, j = 1, i + 1
        while j < len(cmds):
            if cmds[j][0] == opener:
                depth += 1
            elif cmds[j][0] == closer:
                depth -= 1
                if depth == 0:
                    return j
            j += 1
        return j

    def _src(self, v, env):
        v = v.replace('\\', '/')
        if v.startswith('$<') or not v.lower().endswith(CODE_EXT):
            return None
        p = Path(v)
        if not p.is_absolute():
            p = Path(env['CMAKE_CURRENT_SOURCE_DIR']) / p
        return self.tree.rel(p)

    def _target(self, project, name, cm_rel, line):
        key = (project, name)
        t = self.targets.get(key)
        if t is None:
            t = Target('cmake', cm_rel, line, name, (), {name.lower() + '.exe', name.lower() + '.lib',
                                                        name.lower() + '.dll'}, project=project)
            self.targets[key] = t
        return t

    def _exec(self, cmds, env, cm_rel, project, depth):
        i = 0
        while i < len(cmds):
            name, raw, line = cmds[i]
            if name in ('function', 'macro'):
                j = self._blocks(cmds, i, name, 'end' + name)
                a = self._args(raw, env)
                if a:
                    self.functions[a[0].lower()] = (name, a[1:], cmds[i + 1:j])
                i = j + 1
                continue
            if name == 'foreach':
                j = self._blocks(cmds, i, 'foreach', 'endforeach')
                a = self._args(raw, env)
                body = cmds[i + 1:j]
                if a:
                    var, items = a[0], a[1:]
                    if items[:1] == ['RANGE']:
                        items = []
                    if items[:1] in (['IN'],):
                        rest, items = items[1:], []
                        mode = None
                        for x in rest:
                            if x in ('LISTS', 'ITEMS'):
                                mode = x
                            elif mode == 'LISTS':
                                items += [y for y in env.get(x, '').split(';') if y]
                            else:
                                items.append(x)
                    for it in items[:2000]:
                        env[var] = it
                        self._exec(body, env, cm_rel, project, depth)
                i = j + 1
                continue
            a = self._args(raw, env)
            self._command(name, a, env, cm_rel, project, depth, line)
            i += 1

    def _command(self, name, a, env, cm_rel, project, depth, line):
        if name == 'set' and a:
            vals = a[1:]
            for stop in ('CACHE', 'PARENT_SCOPE'):
                if stop in vals:
                    vals = vals[:vals.index(stop)]
            env[a[0]] = ';'.join(vals)
        elif name == 'list' and len(a) >= 2 and a[0] == 'APPEND':
            env[a[1]] = ';'.join([x for x in env.get(a[1], '').split(';') if x] + a[2:])
        elif name == 'get_filename_component' and len(a) >= 3:
            p = Path(a[1].replace('\\', '/'))
            env[a[0]] = {'NAME_WE': p.stem, 'NAME': p.name, 'DIRECTORY': str(p.parent), 'PATH': str(p.parent),
                         'EXT': p.suffix}.get(a[2], a[1])
        elif name == 'string' and len(a) >= 4 and a[0] == 'REPLACE':
            env[a[3]] = ''.join(a[4:]).replace(a[1], a[2])
        elif name == 'file' and len(a) >= 2 and a[0] in ('GLOB', 'GLOB_RECURSE'):
            var, rest, rel_to, pats = a[1], a[2:], None, []
            k = 0
            while k < len(rest):
                if rest[k] == 'RELATIVE' and k + 1 < len(rest):
                    rel_to = rest[k + 1]
                    k += 2
                    continue
                if rest[k] == 'LIST_DIRECTORIES' and k + 1 < len(rest):
                    k += 2
                    continue
                if rest[k] == 'CONFIGURE_DEPENDS':
                    k += 1
                    continue
                pats.append(rest[k])
                k += 1
            hits = []
            for pat in pats:
                p = Path(pat.replace('\\', '/'))
                if not p.is_absolute():
                    p = Path(env['CMAKE_CURRENT_SOURCE_DIR']) / p
                folder = self.tree.rel_dir(p.parent)
                if folder is None:
                    continue
                if any(ch in p.name for ch in '*?['):
                    files = self.tree.glob(folder, p.name, recursive=(a[0] == 'GLOB_RECURSE'))
                    dirs = sorted({f[len(folder) + 1 if folder else 0:].split('/')[0] for f in self.tree.files
                                   if (f.startswith(folder + '/') or not folder) and '/' in f[len(folder) + 1 if folder else 0:]
                                   and fnmatch.fnmatch(f[len(folder) + 1 if folder else 0:].split('/')[0], p.name)})
                    hits += [str(self.tree.root / f) for f in files]
                    hits += [str(self.tree.root / folder / x) if folder else str(self.tree.root / x) for x in dirs]
            if rel_to:
                hits = [os.path.relpath(h, rel_to).replace('\\', '/') for h in hits]
            env[var] = ';'.join(sorted(set(hits)))
        elif name == 'add_subdirectory' and a:
            p = Path(a[0].replace('\\', '/'))
            if not p.is_absolute():
                p = Path(env['CMAKE_CURRENT_SOURCE_DIR']) / p
            r = self.tree.rel(p / 'CMakeLists.txt')
            if r:
                self._run_file(r, project, env, depth + 1)
        elif name in ('add_executable', 'add_library') and a:
            t = self._target(project, a[0], cm_rel, line)
            t.library = t.library or name == 'add_library'
            for v in a[1:]:
                s = self._src(v, env)
                if s:
                    t.sources.add(s)
        elif name == 'target_sources' and a:
            t = self._target(project, a[0], cm_rel, line)
            for v in a[1:]:
                s = self._src(v, env)
                if s:
                    t.sources.add(s)
        elif name == 'target_link_libraries' and a:
            t = self._target(project, a[0], cm_rel, line)
            t.links.update(x for x in a[1:] if x not in ('PRIVATE', 'PUBLIC', 'INTERFACE') and not x.startswith('$<'))
        elif name == 'add_test' and a:
            if 'COMMAND' in a and a.index('COMMAND') + 1 < len(a):
                exe = a[a.index('COMMAND') + 1]
                t = self.targets.get((project, exe))
                if t is not None:
                    t.registered_test = True
        elif name in self.functions:
            kind, params, body = self.functions[name]
            local = dict(env) if kind == 'function' else env
            for k, pname in enumerate(params):
                local[pname] = a[k] if k < len(a) else ''
            local['ARGN'] = ';'.join(a[len(params):])
            local['ARGV'] = ';'.join(a)
            self._exec(body, local, cm_rel, project, depth + 1)


def scan_cmake(tree: Tree):
    cm = _CMake(tree)
    lists = sorted((f for f in tree.files if f.rsplit('/', 1)[-1] == 'CMakeLists.txt'), key=lambda f: (f.count('/'), f))
    for f in lists:
        if f not in cm.reached:
            cm.run_project(f)
    return list(cm.targets.values())


# ---------------------------------------------------------------------------------------------------------------------
# #include graph

_INC = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def include_graph(tree: Tree):
    """included file -> set of files that include it (all repo-relative)."""
    rev = {}
    for f in tree.files:
        if not f.lower().endswith(CODE_EXT):
            continue
        d = Path(f).parent.as_posix()
        for inc in _INC.findall(tree.read(f)):
            inc = inc.replace('\\', '/')
            r = tree.rel((d + '/' + inc) if d != '.' else inc)
            if r is None:
                cands = tree.by_base.get(inc.rsplit('/', 1)[-1].lower(), [])
                cands = [c for c in cands if c.lower().endswith(inc.lower())]
                if cands:
                    r = max(cands, key=lambda c: len(os.path.commonprefix([c, f])))
            if r and r != f:
                rev.setdefault(r, set()).add(f)
    return rev


# ---------------------------------------------------------------------------------------------------------------------
# The index and the query


class Index:
    def __init__(self, root: Path):
        self.tree = Tree(root)
        self.targets = []
        for f in self.tree.files:
            low = f.lower()
            if low.endswith(('.cmd', '.bat', '.sh')):
                self.targets += scan_script_lines(self.tree, f)
            elif low.endswith('.py') and f not in SELF:
                self.targets += scan_python(self.tree, f)
        self.targets += scan_cmake(self.tree)
        self.includers = include_graph(self.tree)
        self.by_source = {}
        for t in self.targets:
            for s in t.sources:
                self.by_source.setdefault(s, []).append(t)
        self.cmake_users = {}          # (project, lib) -> CMake targets that link it (transitively)
        cm = [t for t in self.targets if t.kind == 'cmake']
        names = {(t.project, t.name): t for t in cm}
        for t in cm:
            seen, todo = set(), list(t.links)
            while todo:
                lib = todo.pop()
                if lib in seen:
                    continue
                seen.add(lib)
                self.cmake_users.setdefault((t.project, lib), set()).add(t)
                lt = names.get((t.project, lib))
                if lt is not None:
                    todo += list(lt.links)
        self._words = None

    def script_words(self):
        """script -> set of words in it (outputs and library names are looked up here)."""
        if self._words is None:
            self._words = {}
            for f in self.tree.files:
                if f.lower().endswith(SCRIPT_EXT):
                    self._words[f] = set(w.lower() for w in re.findall(r'[\w.-]+', self.tree.read(f)))
        return self._words

    def runners(self, t: Target):
        """Scripts that name an output of t (exe/dll basename), other than t's own script."""
        if t.kind == 'py':
            return []
        outs = {o for o in t.outputs if o.endswith(('.exe', '.dll'))}
        if t.kind == 'cmake':
            outs = {t.name.lower() + '.exe'}
        out = []
        for f, words in self.script_words().items():
            if f != t.script and f not in SELF and outs & words:
                out.append(f)
        return sorted(out)

    def library_linkers(self, lib: str):
        """Scripts outside CMake that name a CMake library (c5render.lib, --c5render)."""
        lib = lib.lower()
        out = []
        for f, words in self.script_words().items():
            if f.lower().endswith('.py') and f not in SELF and (lib + '.lib' in words or '--' + lib in words):
                out.append(f)
        return sorted(out)

    def units_of(self, f: str):
        """f and every file that includes it, transitively: the compiled units that see f."""
        seen, todo = {f}, [f]
        while todo:
            x = todo.pop()
            for y in self.includers.get(x, ()):
                if y not in seen:
                    seen.add(y)
                    todo.append(y)
        return seen

    def query(self, f: str, partners=True):
        units = self.units_of(f)
        hits = {}                       # id(target) -> (target, level, reason)

        def add(t, level, reason):
            k = id(t)
            if k not in hits or hits[k][1] > level:
                hits[k] = (t, level, reason)

        libs = set()
        for u in sorted(units):
            how = 'compiles it' if u == f else 'compiles %s, which includes it' % u
            for t in self.by_source.get(u, ()):
                add(t, 1, how.replace('compiles', 'names', 1) if t.kind == 'py' else how)   # a script may read it as text too
                if t.kind == 'cmake' and t.library:
                    libs.add((t.project, t.name, how))
        # Python builders that load another builder (importlib by path, or a sibling import) and reuse its sources
        # (one hop, and only a test builder's list: build_sceneshot.py reuses build_scenebundle_tests.py's sources; a
        # product builder loaded for its helpers, as build_server.py loads build_proxy.py, does not count)
        py = {t.script: t for t in self.targets if t.kind == 'py'}
        direct = {k: h for k, h in hits.items()}
        for t in py.values():
            if id(t) in direct:
                continue
            for u in sorted(t.uses):
                ut = py.get(u)
                h = direct.get(id(ut)) if ut is not None else None
                if h is not None and ut.is_test:
                    add(t, 1, 'loads builder %s, which %s' % (u, h[2]))
                    break
        linked_scripts = {}
        for project, lib, how in sorted(libs):
            for t in self.cmake_users.get((project, lib), ()):
                add(t, 1, 'links library %s, which %s' % (lib, how))
            for s in self.library_linkers(lib):
                linked_scripts.setdefault(s, 'names library %s, which %s' % (lib, how))
        if partners:
            for t, level, _ in list(hits.values()):
                if level != 1 or not t.is_test or not (t.sources & units):
                    continue
                others = [p for p in sorted(t.sources - units)
                          if not is_test_path(p) and p.lower().endswith(SRC_EXT)]
                if len(others) > PARTNER_MAX:
                    continue      # a large test build is a build of a module, not a test of a pair's contract
                for p in others:
                    for t2 in self.by_source.get(p, ()):
                        if t2.is_test:
                            add(t2, 2, 'partner: compiles %s, which %s compiles with %s' % (p, t.label(), f))
        res = sorted(hits.values(), key=lambda h: (h[1], not h[0].is_test, h[0].kind == 'cmake', h[0].label()))
        return res, linked_scripts


def _entry(ix: Index, t: Target, level, reason):
    return {'target': t.label(), 'kind': t.kind, 'script': t.script, 'line': t.line, 'test': t.is_test,
            'level': level, 'reason': reason, 'programs': t.programs(), 'runners': ix.runners(t),
            'project': t.project}


def report(ix: Index, files, partners=True, show_all=False):
    out = {}
    for f in files:
        res, scripts = ix.query(f, partners)
        out[f] = {'targets': [_entry(ix, t, lv, why) for t, lv, why in res],
                  'library_scripts': [{'script': s, 'reason': r} for s, r in sorted(scripts.items())]}
    return out


def render_text(rep, show_all=False) -> str:
    lines = []
    for f, r in rep.items():
        lines.append(f)
        groups = (('tests that compile it (or name it: a script)', lambda e: e['test'] and e['level'] == 1),
                  ('partner tests (compile a file it is built with in a test)', lambda e: e['test'] and e['level'] == 2),
                  ('product and tool builds that compile or name it', lambda e: not e['test']))
        any_hit = False
        for title, pick in groups:
            es = [e for e in r['targets'] if pick(e)]
            if not es:
                continue
            any_hit = True
            lines.append('  %s:' % title)
            cm = {}
            for e in es:
                if e['kind'] == 'cmake':
                    cm.setdefault(e['project'], []).append(e)
            for e in es:
                if e['kind'] == 'cmake' and not show_all and len(cm[e['project']]) > CMAKE_COLLAPSE:
                    continue
                lines.append('    %s' % e['target'])
                lines.append('      why: %s' % e['reason'])
                if e['programs']:
                    lines.append('      test program: %s' % ', '.join(e['programs']))
                if e['runners'] and (e['test'] or show_all):
                    lines.append('      run by: %s' % ', '.join(e['runners']))
                elif e['runners']:
                    lines.append('      named by %d scripts (--all lists them)' % len(e['runners']))
            for proj, ce in sorted(cm.items()):
                if not show_all and len(ce) > CMAKE_COLLAPSE:
                    lines.append('    %d CMake targets of %s/ (%s ...), e.g. %s; --all lists them'
                                 % (len(ce), proj or '.', ', '.join(sorted(x['target'].split(' ')[0] for x in ce)[:4]),
                                    ce[0]['reason']))
        if r['library_scripts']:
            any_hit = True
            lines.append('  scripts that link a CMake library compiling it:')
            for s in r['library_scripts']:
                lines.append('    %s  (%s)' % (s['script'], s['reason']))
        if not any_hit:
            lines.append('  nothing in the tree compiles or includes it (statically)')
    return '\n'.join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+', help='source files (repo-relative or absolute)')
    ap.add_argument('--root', type=Path, default=REPO_DEFAULT, help='the repository root (default: this checkout)')
    ap.add_argument('--json', action='store_true', help='print JSON instead of text')
    ap.add_argument('--all', action='store_true', help='list every CMake target instead of a summary per project')
    ap.add_argument('--no-partners', action='store_true', help='leave out the partner tests (level 2)')
    a = ap.parse_args(argv)
    ix = Index(a.root)
    files, missing = [], []
    for f in a.files:
        p = Path(f)
        r = ix.tree.rel(p if p.is_absolute() else (Path.cwd() / p)) or ix.tree.rel(f)
        (files if r else missing).append(r or f)
    for m in missing:
        print('compiled_by: %s is not a file of the tree (--root; evidence/, research/, tracker/, docs/ are not read)' % m, file=sys.stderr)
    if missing:
        return 2
    rep = report(ix, files, partners=not a.no_partners)
    if a.json:
        print(json.dumps(rep, indent=1))
    else:
        print(render_text(rep, show_all=a.all))
    return 0


if __name__ == '__main__':
    sys.exit(main())
