"""Derive the purity file (purity.tsv) for a DLL source and a built x64 DLL from the last audited lists (issue 3503,
epic 3500 stage `purity`, checklist step 11; format tools/release/README.md section 4; research
research/03-dll-engine-boundary/proxy-design.md section 16.9).

    python -I proxy/tools/derive_purity.py --src <x64 source tree> --dll <x64 DLL> --out <dir> [--commit LABEL]
                                           [--baseline proxy/model/purity-baseline.json]
    python -I proxy/tools/derive_purity.py --make-baseline --fork <fork clone> [--baseline-out FILE]

The last audited lists are proxy/model/pure-methods.tsv (engine -> DLL methods; `pure` and `neutral` rows, without the
four kRt_* rows, which decision D30 keeps impure whatever their audit says) and proxy/model/lua-pure-bindings.tsv
(`pure` rows). Their audit is frozen in proxy/model/purity-baseline.json (--make-baseline, run once after each human
audit): for each kept row, every DLL-source citation of its evidence column, located in the audited commit (its file,
lines and a hash of its text), and for every function name the cited lines call, a hash of each definition of that
name in the audited source (function bodies and #define macros of CvGameCoreDLL_Expansion2/ and CvGameCoreDLLUtil/).

The derivation reads only the new DLL source (D83: nothing from the EXE or the game's files) and keeps a row only when
  - every citation's text is found in the new source (at the same lines or moved, text compared after comments and
    whitespace are folded), and
  - every function name its cited lines call has the same definitions as at the audit (any overload's body or any
    macro of that name changed, added or removed drops the row: the name-based callee check of issue 0111, stricter);
every other row is dropped with its reason. It then writes
  <out>/purity.tsv    "# vp64-purity 1", dll_sha256 of --dll, derived_from <audited commit> <--commit or the source's
                      git HEAD or "unknown">, the kept `method` and `lua` lines (sorted)
  <out>/report.tsv    one row per audited row: kind, key, kept|dropped, reason (all of them for a dropped row)
Exit codes: 0 written (rows may be dropped: counted on the last line); 1 failed (an unexpected error); 2 refused input
(missing or malformed source, DLL, baseline or TSV; a TSV row changed since the baseline was made): nothing written.

--make-baseline reads the fork clone with git (show, ls-tree, cat-file) at the commits the TSVs' citations were written
at (CITED_AT) and at the last audited commit (AUDITED_AT); a citation whose text is not found at AUDITED_AT marks its
row `unresolved` (the derivation then always drops it). A row re-audited at a later commit says so in its evidence
column with the marker `[cited at <commit>]` (7 to 40 hex digits; ticket 0628): its citations are read at that commit
instead of its list's `cited_at` (a malformed or unknown marker commit refuses the baseline, exit 2).
"""
import argparse
import csv
import hashlib
import io
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
MODEL = os.path.join(REPO, 'proxy', 'model')
BASELINE = os.path.join(MODEL, 'purity-baseline.json')
FORMAT_HEADER = '# vp64-purity 1'
# The last audit (issue 0111, 2026-11-13, research proxy-design.md 17.5a: "no verdict changed" at 76992d235) and the
# commits the evidence columns' line numbers were written at (issue 2701 at e9d0c4837, issue 2805 at c43ea1b9f).
# Re-baselined 2026-11-20 at the first beta's pin x64-0.1b 1b87dd185 (ticket 0616, MC #8594): the 4 rows the derivation
# drops there are `not-audited` in the lists; every kept row's citations and callee definitions are the same as at
# 76992d235 (research/06-tooling-and-workflows/release-builds.md 6.4). Was 76992d235996ebefb3821b4152aae06f23d9aa88.
# Re-baselined 2026-11-25 at the Vox Populi 5.5 pin x64-vp55 a2627c8ac (ticket 0765, D259): the 35 rows the derivation
# dropped there (upstream #13489's Lua plumbing BasicLuaMethod/GetInstance, #13643's TContainer id cache, new definitions
# of Destroy/Get/GetType, CvCombatInfo's static getMaxDamageMemberCount) were re-audited by hand and stay pure; the six
# whose cited text changed carry `[cited at a2627c8ac]` (research/03-dll-engine-boundary/vp55-patch-side.md section 7).
# Was 1b87dd1857e2b4e4ba87c76c7b481161b33e1368.
AUDITED_AT = 'a2627c8ac15610499a45614d2c5a3242ddbb4ee1'
LISTS = [
    {'file': 'pure-methods.tsv', 'kind': 'method', 'key': 'key', 'verdict': 'verdict', 'evidence': 'evidence',
     'keep': ('pure', 'neutral'), 'cited_at': 'e9d0c4837a7012bcd66e8756ab32f3532e932031'},
    {'file': 'lua-pure-bindings.tsv', 'kind': 'lua', 'key': 'binding', 'verdict': 'verdict', 'evidence': None,
     'keep': ('pure',), 'cited_at': 'c43ea1b9fbc9561387aa92481d34d5155c989e68'},
]
SOURCE_DIRS = ('CvGameCoreDLL_Expansion2/', 'CvGameCoreDLLUtil/')
SOURCE_EXTS = ('.cpp', '.h', '.inl', '.hpp')
# A file name may hold digits after its first character (VP64HiddenEmbark.inl; ticket 0628: before, the name was cut to
# "HiddenEmbark.inl" and the citation went unresolved).
CITE = re.compile(r'([A-Za-z_][A-Za-z0-9_/]*\.(?:cpp|h|inl)):(\d+)(?:-(\d+))?|(?<![\w.:])(\d{2,6})-(\d{2,6})(?![\w.])')
NOT_CALLS = {'if', 'for', 'while', 'switch', 'catch', 'return', 'sizeof', 'decltype', 'alignof', 'defined',
             'static_cast', 'const_cast', 'reinterpret_cast', 'dynamic_cast', 'typeid', 'throw', 'new', 'delete',
             'do', 'else', 'case', 'operator', 'noexcept', '__declspec', '__forceinline', 'template'}
QUALIFIERS = ('const', 'override', 'noexcept', 'final', 'volatile', 'OVERRIDE', 'FINAL')


class Refused(Exception):
    """Bad input: exit 2, nothing written."""


def sha1(text):
    return hashlib.sha1(text.encode('utf-8', 'surrogateescape')).hexdigest()


def is_source(path):
    return path.startswith(SOURCE_DIRS) and path.endswith(SOURCE_EXTS)


# ---------------------------------------------------------------------------------------------- source trees
class DirTree:
    """A DLL source tree on disk (a git checkout or a plain copy)."""

    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.label = self.root
        self.files = []
        for top in SOURCE_DIRS:
            base = os.path.join(self.root, top)
            for dp, _dn, fn in os.walk(base):
                for f in fn:
                    rel = os.path.relpath(os.path.join(dp, f), self.root).replace('\\', '/')
                    if is_source(rel):
                        self.files.append(rel)
        self.files.sort()
        self._cache = {}

    def text(self, rel):
        if rel not in self._cache:
            with open(os.path.join(self.root, rel), 'rb') as f:
                self._cache[rel] = f.read().decode('latin-1').replace('\r\n', '\n')
        return self._cache[rel]


class GitTree:
    """A commit of a fork clone (read with git ls-tree and one git cat-file --batch)."""

    def __init__(self, fork, commit):
        self.label = '%s@%s' % (fork, commit[:9])
        ls = subprocess.run(['git', '-C', fork, 'ls-tree', '-r', '-z', commit], capture_output=True)
        if ls.returncode != 0:
            raise Refused('git ls-tree %s in %s failed: %s' % (commit, fork, ls.stderr.decode('utf-8', 'replace')[:200]))
        objs = []
        for rec in ls.stdout.split(b'\0'):
            if not rec:
                continue
            meta, rel = rec.split(b'\t', 1)
            rel = rel.decode('utf-8')
            _mode, kind, sha = meta.split()
            if kind == b'blob' and is_source(rel):
                objs.append((rel, sha.decode()))
        cat = subprocess.run(['git', '-C', fork, 'cat-file', '--batch'], input=''.join(o[1] + '\n' for o in objs).encode(),
                             capture_output=True, check=True).stdout
        self._texts, pos = {}, 0
        for rel, _sha in objs:
            nl = cat.index(b'\n', pos)
            size = int(cat[pos:nl].split()[2])
            self._texts[rel] = cat[nl + 1:nl + 1 + size].decode('latin-1').replace('\r\n', '\n')
            pos = nl + 1 + size + 1
        self.files = sorted(self._texts)

    def text(self, rel):
        return self._texts[rel]


def resolve(tree, name):
    """A cited file name ("CvPlayer.cpp", "Lua/CvLuaCity.cpp") -> its path in the tree, preferring
    CvGameCoreDLL_Expansion2/ (as 0111_cite_check.py); None when absent."""
    base = name.split('/')[-1]
    c = [p for p in tree.files if p.endswith('/' + base) and ('/' not in name or p == name or p.endswith('/' + name))]
    pref = [p for p in c if p.startswith('CvGameCoreDLL_Expansion2/')]
    return (pref or c or [None])[0]


# ---------------------------------------------------------------------------------------------- C++ text
def strip_code(text):
    """Comments, string and character literals blanked (newlines kept, so line numbers stay)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j, n)
            closed = j < n and text[j] == c        # unterminated at a line end: the newline is kept
            body = re.sub(r'[^\n]', ' ', text[i + 1:j])
            out.append(c + body + (c if closed else ''))
            i = j + 1 if closed else j
        else:
            out.append(c)
            i += 1
    res = ''.join(out)
    if len(res) != len(text) or res.count('\n') != text.count('\n'):   # offsets and lines must stay
        raise RuntimeError('strip_code changed the text length or its lines')
    return res


def norm_lines(stripped_lines):
    return [' '.join(l.split()) for l in stripped_lines]


def _skip_ws_back(s, j):
    while j >= 0 and s[j].isspace():
        j -= 1
    return j


def _ident_back(s, j):
    """The identifier ending at s[j] (j included): (start, name) or (j + 1, '')."""
    k = j
    while k >= 0 and (s[k].isalnum() or s[k] == '_'):
        k -= 1
    name = s[k + 1:j + 1]
    if name and k >= 0 and s[k] == '~':
        return k, '~' + name
    return k + 1, name


def function_at(s, i):
    """If the '{' at s[i] opens a function body, (name, index of its declaration line start); else None."""
    j = _skip_ws_back(s, i - 1)
    for _ in range(64):
        moved = True
        while moved:          # trailing qualifiers: ") const {", ") override {", ") throw() {"
            moved = False
            st, w = _ident_back(s, j)
            if w in QUALIFIERS:
                j, moved = _skip_ws_back(s, st - 1), True
        if j < 0 or s[j] != ')':
            return None
        depth, k = 0, j
        while k >= 0:
            if s[k] == ')':
                depth += 1
            elif s[k] == '(':
                depth -= 1
                if depth == 0:
                    break
            k -= 1
        if k < 0:
            return None
        e = _skip_ws_back(s, k - 1)
        st, name = _ident_back(s, e)
        if not name or name in NOT_CALLS:
            if name == 'throw':       # ") throw() {": the exception specification, go on to the parameter list
                j = _skip_ws_back(s, st - 1)
                continue
            return None
        p = _skip_ws_back(s, st - 1)
        if p >= 0 and (s[p] == ',' or (s[p] == ':' and (p == 0 or s[p - 1] != ':'))):
            j = _skip_ws_back(s, p - 1)   # a constructor's initializer list: the parameter list is further back
            continue
        return name.lstrip('~') if name.startswith('~') else name, s.rfind('\n', 0, st) + 1
    return None


def match_brace(s, i):
    depth = 0
    for k in range(i, len(s)):
        if s[k] == '{':
            depth += 1
        elif s[k] == '}':
            depth -= 1
            if depth == 0:
                return k
    return len(s) - 1


DEFINE = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)', re.M)


def index_definitions(tree, where=None):
    """name -> sorted list of hashes of every definition of that name (function bodies with their declaration line,
    #define macros with their continuation lines), over the tree's DLL sources. `where` (a dict, for the tests): name ->
    [(file, offset of the body's '{' or of the #define)]."""
    idx = {}
    for rel in tree.files:
        s = strip_code(tree.text(rel))
        for m in DEFINE.finditer(s):
            e = m.end()
            while True:
                nl = s.find('\n', e)
                if nl < 0:
                    nl = len(s)
                    break
                if s[nl - 1:nl] == '\\' or s[max(0, nl - 2):nl].rstrip().endswith('\\'):
                    e = nl + 1
                    continue
                break
            idx.setdefault(m.group(1), []).append(sha1(' '.join(s[m.start():nl].split())))
            if where is not None:
                where.setdefault(m.group(1), []).append((rel, m.start()))
        pos = 0
        while True:
            i = s.find('{', pos)
            if i < 0:
                break
            f = function_at(s, i)
            if f is None:
                pos = i + 1
                continue
            end = match_brace(s, i)
            idx.setdefault(f[0], []).append(sha1(' '.join(s[f[1]:end + 1].split())))
            if where is not None:
                where.setdefault(f[0], []).append((rel, i))
            pos = end + 1
    return {k: sorted(v) for k, v in idx.items()}


def called_names(stripped_text):
    return sorted({m.group(1) for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', stripped_text)} - NOT_CALLS)


def find_window(lines, want, at):
    """Index (0-based) where the normalized lines `want` occur in `lines`: `at` first, else the nearest; None."""
    n = len(want)
    if n == 0:
        return None
    if 0 <= at and lines[at:at + n] == want:
        return at
    hits = [i for i in range(len(lines) - n + 1) if lines[i] == want[0] and lines[i:i + n] == want]
    return min(hits, key=lambda i: abs(i - at)) if hits else None


class Lines:
    """Normalized (comment-stripped, whitespace-folded) lines of each file of a tree, cached."""

    def __init__(self, tree):
        self.tree = tree
        self._c = {}

    def get(self, rel):
        if rel not in self._c:
            self._c[rel] = norm_lines(strip_code(self.tree.text(rel)).split('\n'))
        return self._c[rel]

    def first_index(self, rel):
        """SHA-1 of a normalized line -> the line indexes holding it (candidates for a moved citation)."""
        k = ('first', rel)
        if k not in self._c:
            d = {}
            for i, ln in enumerate(self.get(rel)):
                d.setdefault(sha1(ln), []).append(i)
            self._c[k] = d
        return self._c[k]


# ---------------------------------------------------------------------------------------------- the audited lists
def read_tsv(name):
    path = os.path.join(MODEL, name)
    if not os.path.isfile(path):
        raise Refused('%s: no such file' % path)
    with open(path, encoding='utf-8', newline='') as f:
        text = f.read().replace('\r\n', '\n')
    rows = list(csv.reader(io.StringIO(text), delimiter='\t', quoting=csv.QUOTE_NONE))
    if not rows:
        raise Refused('%s: empty' % path)
    return rows[0], rows[1:]


def kept_rows(spec):
    """The rows of one list that the purity file may carry: [(key, verdict, evidence, row text)]. Raises Refused on a
    malformed row (field count, empty key, unknown verdict, a key twice)."""
    header, rows = read_tsv(spec['file'])
    for col in (spec['key'], spec['verdict']):
        if col not in header:
            raise Refused('%s: no column %r' % (spec['file'], col))
    ki, vi = header.index(spec['key']), header.index(spec['verdict'])
    ei = header.index(spec['evidence']) if spec['evidence'] else next(
        (i for i, h in enumerate(header) if h.startswith('evidence')), None)
    if ei is None:
        raise Refused('%s: no evidence column' % spec['file'])
    known = {'pure', 'neutral', 'conditional', 'impure', 'not-audited'}
    seen, out = set(), []
    for n, r in enumerate(rows, 2):
        if not any(c.strip() for c in r):
            continue
        if len(r) != len(header):
            raise Refused('%s line %d: %d fields, the header has %d' % (spec['file'], n, len(r), len(header)))
        key, verdict = r[ki].strip(), r[vi].strip()
        if not key:
            raise Refused('%s line %d: empty key' % (spec['file'], n))
        if verdict not in known:
            raise Refused('%s line %d: unknown verdict %r' % (spec['file'], n, verdict))
        if key in seen:
            raise Refused('%s line %d: key %r twice' % (spec['file'], n, key))
        seen.add(key)
        if verdict in spec['keep'] and not (spec['kind'] == 'method' and key.startswith('kRt_')):
            out.append((key, verdict, r[ei], '\t'.join(r)))
    return out


def citations(evidence):
    """(cited name, first line, last line) of each DLL-source citation; a bare range belongs to the last named file."""
    out, last = [], None
    for m in CITE.finditer(evidence):
        if m.group(1):
            name, a, b = m.group(1), int(m.group(2)), int(m.group(3) or m.group(2))
            last = name
        elif last:
            name, a, b = last, int(m.group(4)), int(m.group(5))
        else:
            continue
        if b < a or b - a > 5000:
            continue
        out.append((name, a, b))
    return out


CITED_AT_MARK = re.compile(r'\[cited at ([^\]]*)\]')


def row_cited_at(evidence):
    """The commit of a row's own `[cited at <commit>]` marker (ticket 0628), or None when the row has none. Raises
    Refused for a malformed marker or more than one."""
    marks = CITED_AT_MARK.findall(evidence)
    if not marks:
        return None
    if len(marks) > 1 or not re.fullmatch(r'[0-9a-f]{7,40}', marks[0].strip()):
        raise Refused('evidence %r: one marker [cited at <commit>] with 7 to 40 lower-case hex digits' % evidence[:120])
    return marks[0].strip()


def make_baseline(fork, out_path):
    audited = GitTree(fork, AUDITED_AT)
    a_lines = Lines(audited)
    cited_trees = {}

    def cited_lines(commit):
        if commit not in cited_trees:
            cited_trees[commit] = Lines(GitTree(fork, commit))
        return cited_trees[commit]

    entries, called = [], set()
    for spec in LISTS:
        for key, verdict, ev, row in kept_rows(spec):
            own = row_cited_at(ev)
            cited_at = own or spec['cited_at']
            c_lines = cited_lines(cited_at)
            e = {'kind': spec['kind'], 'key': key, 'verdict': verdict, 'row_sha1': sha1(row), 'citations': [],
                 'callees': [], 'unresolved': []}
            if own:
                e['cited_at'] = own
            names = set()
            cites = citations(ev)
            if not cites:
                e['unresolved'].append('no DLL-source citation in the evidence column')
            for name, a, b in cites:
                pc = resolve(c_lines.tree, name)
                pa = resolve(audited, name)
                if not pc or not pa:
                    if name.startswith('srv_') or name in ('vp_wire.h',):
                        e['unresolved'].append('%s:%d-%d cites this repository, not the DLL' % (name, a, b))
                    else:
                        e['unresolved'].append('%s:%d-%d: file not in the DLL source' % (name, a, b))
                    continue
                want = c_lines.get(pc)[a - 1:b]
                at = find_window(a_lines.get(pa), want, a - 1)
                if at is None or len(want) != b - a + 1:
                    e['unresolved'].append('%s:%d-%d: text at %s not found at %s' % (name, a, b, cited_at[:9],
                                                                                     AUDITED_AT[:9]))
                    continue
                e['citations'].append({'cite': '%s:%d-%d' % (name, a, b), 'path': pa, 'line': at + 1,
                                       'nlines': len(want), 'text_sha1': sha1('\n'.join(want)),
                                       'first_sha1': sha1(want[0])})
                names.update(called_names('\n'.join(want)))
            e['callees'] = sorted(names)
            called |= names
            entries.append(e)
    idx = index_definitions(audited)
    doc = {'format': 1, 'generated_by': 'proxy/tools/derive_purity.py --make-baseline', 'audited_at': AUDITED_AT,
           'lists': [{k: s[k] for k in ('file', 'kind', 'cited_at')} | {'keep': list(s['keep'])} for s in LISTS],
           'note': 'one entry per kept row: its citations located at audited_at (path, first line, line count, SHA-1 of '
                   'the comment-stripped, whitespace-folded lines) and the function names those lines call; '
                   '`definitions`: per name, the SHA-1s of every definition of that name at audited_at',
           'entries': entries,
           'definitions': {n: idx.get(n, []) for n in sorted(called)}}
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, indent=1, sort_keys=False)
        f.write('\n')
    unres = [e for e in entries if e['unresolved']]
    print('derive_purity: baseline %s: %d entries (%d method, %d lua), %d citations, %d callee names; %d unresolved'
          % (out_path, len(entries), sum(e['kind'] == 'method' for e in entries),
             sum(e['kind'] == 'lua' for e in entries), sum(len(e['citations']) for e in entries), len(called),
             len(unres)))
    for e in unres:
        print('  unresolved %s %s: %s' % (e['kind'], e['key'], '; '.join(e['unresolved'])))
    return 0


def load_baseline(path):
    """Reads and checks the baseline against the TSVs. Returns the document; raises Refused."""
    if not os.path.isfile(path):
        raise Refused('baseline %s: no such file' % path)
    try:
        with open(path, encoding='utf-8') as f:
            doc = json.load(f)
    except ValueError as ex:
        raise Refused('baseline %s: not JSON (%s)' % (path, ex))
    if not isinstance(doc, dict) or doc.get('format') != 1 or not isinstance(doc.get('entries'), list) or \
            not isinstance(doc.get('definitions'), dict) or not re.fullmatch(r'[0-9a-f]{40}', str(doc.get('audited_at'))):
        raise Refused('baseline %s: not a format-1 baseline (format, audited_at, entries, definitions)' % path)
    by = {}
    for e in doc['entries']:
        if not isinstance(e, dict) or e.get('kind') not in ('method', 'lua') or not isinstance(e.get('key'), str) or \
                not isinstance(e.get('citations'), list) or not isinstance(e.get('callees'), list) or \
                not isinstance(e.get('unresolved', []), list) or \
                not all(isinstance(c, dict) and isinstance(c.get('cite'), str) and isinstance(c.get('path'), str) and
                        isinstance(c.get('line'), int) and isinstance(c.get('nlines'), int) and c['nlines'] > 0 and
                        re.fullmatch(r'[0-9a-f]{40}', str(c.get('text_sha1'))) and
                        re.fullmatch(r'[0-9a-f]{40}', str(c.get('first_sha1'))) for c in e['citations']):
            raise Refused('baseline %s: a malformed entry %r' % (path, str(e)[:120]))
        by[(e['kind'], e['key'])] = e
    want = {}
    for spec in LISTS:
        for key, _v, _ev, row in kept_rows(spec):
            want[(spec['kind'], key)] = sha1(row)
    for k, h in want.items():
        if k not in by:
            raise Refused('%s %s is listed in proxy/model but not in the baseline: run --make-baseline after its audit'
                          % k)
        if by[k].get('row_sha1') != h:
            raise Refused('%s %s changed in proxy/model since the baseline was made: run --make-baseline after its audit'
                          % k)
    extra = [k for k in by if k not in want]
    if extra:
        raise Refused('the baseline has %d entries that proxy/model no longer lists (first: %s %s): run --make-baseline'
                      % (len(extra), extra[0][0], extra[0][1]))
    for n in {c for e in doc['entries'] for c in e['callees']}:
        if n not in doc['definitions']:
            raise Refused('baseline %s: callee %s has no definitions entry' % (path, n))
    return doc


def derive(doc, tree):
    """[(entry, [reasons])]: no reason = kept."""
    lines = Lines(tree)
    idx = index_definitions(tree)
    out = []
    for e in doc['entries']:
        why = list('not located at the audit: ' + u for u in e.get('unresolved', []))
        for c in e['citations']:
            p = resolve(tree, c['cite'].split(':')[0])
            if p is None:
                why.append('%s: the file is gone' % c['cite'])
                continue
            ls, firsts = lines.get(p), lines.first_index(p)
            n, at = c['nlines'], c['line'] - 1
            cands = ([at] if 0 <= at else []) + firsts.get(c['first_sha1'], [])
            if not any(sha1('\n'.join(ls[i:i + n])) == c['text_sha1'] for i in cands if len(ls[i:i + n]) == n):
                why.append('%s (at %s:%d at the audit): the cited text changed' % (c['cite'], c['path'], c['line']))
        for n in e['callees']:
            if idx.get(n, []) != doc['definitions'][n]:
                was, now = len(doc['definitions'][n]), len(idx.get(n, []))
                why.append('callee %s changed (%d definition(s) at the audit, %d now; a body or macro differs)'
                           % (n, was, now) if was == now else 'callee %s: %d definition(s) at the audit, %d now'
                           % (n, was, now))
        out.append((e, why))
    return out


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def source_label(src, commit):
    if commit:
        return commit
    if os.path.exists(os.path.join(src, '.git')):
        cp = subprocess.run(['git', '-C', src, 'rev-parse', 'HEAD'], capture_output=True, text=True)
        if cp.returncode == 0 and cp.stdout.strip():
            return cp.stdout.strip()
    return 'unknown'


def write_outputs(out, doc, results, dll_sha, label):
    os.makedirs(out, exist_ok=True)
    kept = [e for e, why in results if not why]
    lines = [FORMAT_HEADER, 'dll_sha256\t%s' % dll_sha, 'derived_from\t%s\t%s' % (doc['audited_at'], label),
             '# derived by proxy/tools/derive_purity.py (issue 3503): %d of %d audited rows kept' % (
                 len(kept), len(results))]
    lines += ['method\t%s' % k for k in sorted(e['key'] for e in kept if e['kind'] == 'method')]
    lines += ['lua\t%s' % k for k in sorted(e['key'] for e in kept if e['kind'] == 'lua')]
    for ln in lines:
        if '\r' in ln or '\n' in ln or (ln.count('\t') > 2):
            raise Refused('a key with a line break or a tab: %r' % ln)
    tmp = os.path.join(out, 'purity.tsv.tmp')
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(tmp, os.path.join(out, 'purity.tsv'))
    with open(os.path.join(out, 'report.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('kind\tkey\tdecision\treason\n')
        for e, why in results:
            f.write('%s\t%s\t%s\t%s\n' % (e['kind'], e['key'], 'dropped' if why else 'kept',
                                          ' | '.join(w.replace('\t', ' ') for w in why) or '-'))
    return len(kept)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', help='the x64 DLL source tree the DLL was built from (git checkout or plain copy)')
    ap.add_argument('--dll', help='the built x64 CvGameCore_Expansion2.dll (its SHA-256 binds the file)')
    ap.add_argument('--out', help='output folder: purity.tsv and report.tsv')
    ap.add_argument('--commit', help='label of --src recorded in derived_from (default: its git HEAD, else "unknown")')
    ap.add_argument('--baseline', default=None, help='default: <model dir>/purity-baseline.json')
    ap.add_argument('--model-dir', default=None, help='the audited lists (default proxy/model; the tests use copies)')
    ap.add_argument('--make-baseline', action='store_true')
    ap.add_argument('--fork', help='--make-baseline: a fork clone holding the audited and cited commits')
    ap.add_argument('--baseline-out', default=None, help='--make-baseline: where to write (default --baseline)')
    a = ap.parse_args(argv)
    global MODEL
    if a.model_dir:
        MODEL = os.path.abspath(a.model_dir)
    baseline = os.path.abspath(a.baseline) if a.baseline else os.path.join(MODEL, 'purity-baseline.json')
    try:
        if a.make_baseline:
            if not a.fork or not os.path.isdir(a.fork):
                raise Refused('--make-baseline needs --fork <fork clone>')
            return make_baseline(a.fork, os.path.abspath(a.baseline_out) if a.baseline_out else baseline)
        for opt in ('src', 'dll', 'out'):
            if not getattr(a, opt):
                raise Refused('--%s is required' % opt)
        src = os.path.abspath(a.src)
        if not os.path.isdir(os.path.join(src, 'CvGameCoreDLL_Expansion2')):
            raise Refused('--src %s: not a DLL source tree (no CvGameCoreDLL_Expansion2)' % src)
        if not os.path.isfile(a.dll) or os.path.getsize(a.dll) < 1024:
            raise Refused('--dll %s: no such file (or under 1 KiB)' % a.dll)
        with open(a.dll, 'rb') as f:
            if f.read(2) != b'MZ':
                raise Refused('--dll %s: not a PE file' % a.dll)
        if a.commit is not None and not re.fullmatch(r'[0-9A-Za-z._/-]{1,80}', a.commit):
            raise Refused('--commit %r: one word of letters, digits, . _ / - (at most 80)' % a.commit)
        doc = load_baseline(baseline)
        tree = DirTree(src)
        if not tree.files:
            raise Refused('--src %s: no DLL sources under %s' % (src, ' or '.join(SOURCE_DIRS)))
        results = derive(doc, tree)
        n = write_outputs(os.path.abspath(a.out), doc, results, sha256_file(a.dll), source_label(src, a.commit))
    except Refused as ex:
        print('derive_purity: refused: %s' % ex)
        return 2
    dropped = len(results) - n
    for e, why in results:
        if why:
            print('derive_purity: dropped %s %s: %s' % (e['kind'], e['key'], ' | '.join(why)))
    print('derive_purity: wrote %s: %d kept (%d method, %d lua), %d dropped of %d audited rows; report %s'
          % (os.path.join(a.out, 'purity.tsv'), n, sum(1 for e, w in results if not w and e['kind'] == 'method'),
             sum(1 for e, w in results if not w and e['kind'] == 'lua'), dropped, len(results),
             os.path.join(a.out, 'report.tsv')))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception as ex:   # exit 1: failed (the driver's contract), with the traceback for the log
        import traceback
        traceback.print_exc()
        print('derive_purity: FAILED: %r' % ex)
        sys.exit(1)
