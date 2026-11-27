"""Corpus generator for the CvLocalization A/B (issue 1702).

    python gen_corpus.py --locdb MASTER --out DIR [--lua-root DIR] [--stats-x64 DB ...] [--stats-x86 DB ...]

Writes, from read-only inputs, everything every arm of the A/B uses (so all arms see the same bytes):
  DIR/Localization-AB.db   copy of the localization database plus: the 9 other languages enabled in `Languages`
                           (with LanguagePriorities rows), 18 plural-rule probe languages zz_R00..zz_R17 (PluralRule
                           0..17), and the TXT_KEY_AB_EDGE_* keys (en_US) used by the edge cases;
  DIR/corpus-<name>.txt    one command per line for locquery (tab separated, escaped: \\\\ \\t \\n \\r \\xNN), one
                           file per process run (CORPORA: main, nofallback, dictinit, reinit, cycles);
  DIR/stats-expect.json    for every stats.db column checked: the x86 run's distinct texts;
  DIR/corpus-counts.json   how many lines of each kind.
Deterministic: every list is sorted; no clock, no randomness.
"""
import argparse
import collections
import json
import re
import shutil
import sqlite3
import sys
from pathlib import Path

INTS = [0, 1, 2, 5, 11, 21, -1, -3, 1000, 1234567]
FLOATS = ['0.5', '1.0', '2.25', '-1.5', '1234.5678', '0.005', '2.675']
INT64S = ['0', '-1', '5000000000']
PLAIN = ['Plain text', '', 'Ünïcødé ünit ✓ 日本', 'TXT_KEY_AB_DOES_NOT_EXIST', 'a {1_X} b']

# Plural rule per language (Mozilla numbering, the closed library supports 1-16).
OTHER_LANGS = [('de_DE', 'Deutsch', 2), ('es_ES', 'Español', 2), ('fr_FR', 'Français', 2 + 1),
               ('it_IT', 'Italiano', 2), ('ja_JP', '日本語', 1), ('ko_KR', '한국어', 1), ('pl_PL', 'Polski', 9),
               ('ru_RU', 'Русский', 7), ('zh_Hant_HK', '繁體中文', 1)]
PROBE_RULES = list(range(0, 18))

NUMERIC_T = {'plural', 'number', 'spellout', 'ordinal', 'roman', 'percent', 'currency'}
KEY_T = {'textkey', 'gender'}

# Keys added to the test copy (en_US). Tag -> (Text, Gender, Plurality).
EDGE_KEYS = {
    'TXT_KEY_AB_EDGE_TWO_ARGS': ('Edge {1_Num} and {2_Name: textkey} / {@2_Name}', None, None),
    'TXT_KEY_AB_EDGE_FEM': ('Edgette', 'feminine', None),
    'TXT_KEY_AB_EDGE_FORMS': ('one form|two forms|three forms', 'masculine|feminine|neuter:an', '1|2|2'),
    'TXT_KEY_AB_EDGE_NEST1': ('L1 {TXT_KEY_AB_EDGE_NEST2}', None, None),
    'TXT_KEY_AB_EDGE_NEST2': ('L2 {TXT_KEY_AB_EDGE_NEST3}', None, None),
    'TXT_KEY_AB_EDGE_NEST3': ('L3 {TXT_KEY_AB_EDGE_NEST4}', None, None),
    'TXT_KEY_AB_EDGE_NEST4': ('L4 {TXT_KEY_AB_EDGE_NEST5}', None, None),
    'TXT_KEY_AB_EDGE_NEST5': ('L5 {1_X} end', None, None),
    'TXT_KEY_AB_EDGE_NEST_ARGS': ('outer {1_X} [{TXT_KEY_AB_EDGE_TWO_ARGS}]', None, None),
    'TXT_KEY_AB_EDGE_EMPTY': ('', None, None),
    'TXT_KEY_AB_EDGE_NULL': (None, None, None),
    'TXT_KEY_AB_EDGE_BAD_PLURALITY': ('a|b', None, 'x|'),
    'TXT_KEY_AB_EDGE_BAD_GENDER': ('a|b', 'nosuch|:', None),
    'TXT_KEY_AB_EDGE_PIPES': ('a||b|', None, None),
    'TXT_KEY_AB_EDGE_SELF': ('self {TXT_KEY_AB_EDGE_SELF}', None, None),
    'TXT_KEY_AB_EDGE_CYCLE_A': ('A {TXT_KEY_AB_EDGE_CYCLE_B}', None, None),
    'TXT_KEY_AB_EDGE_CYCLE_B': ('B {TXT_KEY_AB_EDGE_CYCLE_A}', None, None),
}

CORPORA = ['main', 'nofallback', 'dictinit', 'reinit', 'cycles']  # corpus-<name>.txt, in run order
CYCLES = {'TXT_KEY_AB_EDGE_SELF', 'TXT_KEY_AB_EDGE_CYCLE_A', 'TXT_KEY_AB_EDGE_CYCLE_B'}  # only in the last section

MALFORMED = [
    '{1_X}', '{0}', '{0_X}', '{1}', '{1', '1}', '{', '}', '{}', '{{1_X}}', '{1_X}}', '{{1_X}', '{ 1_X }', '{1 _X}',
    '{-1_X}', '{99_X}', '{1_X:}', '{1_X: }', '{1_X: nosuchtransform}', '{1_X: number}', '{1_X: number ""}',
    '{1_X: number "#,##0.00"}', '{1_X: number #}', '{1_X: plural}', '{1_X: plural 1?a;}', '{1_X: plural 1?a}',
    '{1_X: plural other?b;}', '{1_X: plural 1?a; 2?b; other?c}', '{1_X: gender}', '{1_X: gender feminine?f;}',
    '{1_X: gender *?any;}', '{@1}', '{@1_X}', '{@1: plural 1?a; other?b;}', '{@}', '{@0}', '{TXT_KEY_AB_DOES_NOT_EXIST}',
    '{TXT_KEY_AB_EDGE_FEM}', '{TXT_KEY_AB_EDGE_FEM:textkey}', '{TXT_KEY_}', '{txt_key_unit_settler}',
    '{1_X:textkey}', '{1_X: upper}', '{1_X: lower}', '{1_X: title}', '{1_X: spellout}', '{1_X: ordinal}',
    '{1_X: roman}', '{1_X: percent}', '{1_X: currency}', '{1_X: spellout "%spellout-ordinal"}',
    '{1_X[2]}', '{@1_X[2]}', '{1_X[0]}', '{1_X[99]}', '[NEWLINE]{1_X}[ICON_GOLD]', '\\{1_X\\}', '100%', '%s %d',
    '{1_X}{1_X}{2_X}', '{2_X} {1_X}', 'no placeholders at all',
]


def esc(s):
    o = []
    for ch in s:
        if ch == '\\':
            o.append('\\\\')
        elif ch == '\t':
            o.append('\\t')
        elif ch == '\n':
            o.append('\\n')
        elif ch == '\r':
            o.append('\\r')
        elif ord(ch) < 0x20 or ch == '\x7f':
            o.append('\\x%02x' % ord(ch))
        else:
            o.append(ch)
    return ''.join(o)


def top_segments(text):
    """Top-level {...} segments with nesting respected; returns inner strings."""
    out, depth, start = [], 0, None
    for i, ch in enumerate(text):
        if ch == '{':
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == '}' and depth > 0:
            depth -= 1
            if depth == 0:
                out.append(text[start:i])
    return out


SEG_RX = re.compile(r'^\s*(@?)\s*(\d+)\s*(?:_[^:\[\]}]*)?(?:\[\d+\])?\s*(?::\s*([A-Za-z]+))?')


def slot_types(text):
    """Arg index (1-based) -> 'num' | 'key' | 'any' for every placeholder in text (nested ones too)."""
    types = {}

    def visit(t):
        for seg in top_segments(t):
            m = SEG_RX.match(seg)
            if m:
                at, idx, tr = m.group(1), int(m.group(2)), (m.group(3) or '').lower()
                if at or tr in KEY_T:
                    kind = 'key'
                elif tr in NUMERIC_T:
                    kind = 'num'
                else:
                    kind = 'any'
                prev = types.get(idx)
                rank = {'key': 3, 'num': 2, 'any': 1}
                if prev is None or rank[kind] > rank[prev]:
                    types[idx] = kind
            visit(seg)
    visit(text or '')
    return types


def fmt_line(ident, mode, text, args):
    return '\t'.join(['FMT', esc(ident), mode, esc(text)] + [esc(a) for a in args])


def build_db(master, dst):
    shutil.copyfile(master, dst)
    c = sqlite3.connect(str(dst))
    c.execute('PRAGMA journal_mode=DELETE')
    for t, name, rule in OTHER_LANGS:
        c.execute('INSERT INTO Languages(Type, Name, TableName, PluralRule) VALUES (?,?,?,?)',
                  (t, name, 'Language_' + t.upper(), rule))
        c.execute('INSERT INTO LanguagePriorities VALUES (?,?,100)', (t, t))
        c.execute('INSERT INTO LanguagePriorities VALUES (?,?,50)', (t, 'en_US'))
    for r in PROBE_RULES:
        t = 'zz_R%02d' % r
        c.execute('INSERT INTO Languages(Type, Name, TableName, PluralRule) VALUES (?,?,?,?)',
                  (t, 'Probe rule %d' % r, 'Language_en_US', r))
        c.execute('INSERT INTO LanguagePriorities VALUES (?,?,100)', (t, 'en_US'))
    for tag in sorted(EDGE_KEYS):
        text, g, p = EDGE_KEYS[tag]
        c.execute('INSERT INTO LocalizedText(Language, Tag, Text, Gender, Plurality) VALUES (?,?,?,?,?)',
                  ('en_US', tag, text, g, p))
    c.commit()
    c.close()


def lua_calls(root):
    """(relpath, line, key, [arg tokens]) for every Locale.ConvertTextKey/Lookup with a literal first argument."""
    rx = re.compile(r'Locale\s*\.\s*(ConvertTextKey|Lookup)\s*\(')
    out = []
    files = sorted(p for p in root.rglob('*.lua') if p.is_file())
    for p in files:
        try:
            src = p.read_bytes().decode('utf-8', errors='replace')
        except OSError:
            continue
        for m in rx.finditer(src):
            i, depth, args, cur, n = m.end(), 1, [], [], len(src)
            ok = True
            while i < n and depth > 0:
                ch = src[i]
                if ch in '"\'':
                    j = i + 1
                    while j < n and src[j] != ch and src[j] != '\n':
                        j += 2 if src[j] == '\\' else 1
                    cur.append(src[i:j + 1])
                    i = j + 1
                    continue
                if src.startswith('--', i):  # comment inside a call
                    j = src.find('\n', i)
                    i = n if j < 0 else j
                    continue
                if ch in '([{':
                    depth += 1
                elif ch in ')]}':
                    depth -= 1
                    if depth == 0:
                        break
                if ch == ',' and depth == 1:
                    args.append(''.join(cur).strip())
                    cur = []
                else:
                    cur.append(ch)
                i += 1
            if depth != 0:
                ok = False
            args.append(''.join(cur).strip())
            if not ok or not args or not re.fullmatch(r'"[^"\\]*"|\'[^\'\\]*\'', args[0]):
                continue
            line = src.count('\n', 0, m.start()) + 1
            out.append((p.relative_to(root).as_posix(), line, args[0][1:-1], args[1:] if args[1:] != [''] else []))
    return out, files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--locdb', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--lua-root', type=Path)
    ap.add_argument('--stats-x64', type=Path, nargs='*', default=[])
    ap.add_argument('--stats-x86', type=Path, nargs='*', default=[])
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    dbp = a.out / 'Localization-AB.db'
    build_db(a.locdb, dbp)

    src = sqlite3.connect('file:%s?mode=ro' % a.locdb.as_posix(), uri=True)
    rows = collections.defaultdict(dict)  # lang -> tag -> (text, gender, plurality)
    for lang, tag, text, g, p in src.execute('SELECT Language, Tag, Text, Gender, Plurality FROM LocalizedText'):
        rows[lang][tag] = (text, g, p)
    for tag, v in EDGE_KEYS.items():
        rows['en_US'][tag] = v
    counts = collections.Counter()
    # Separate corpora, each run in its own process: Initialize after Shutdown and endless key recursion leave the
    # closed library unusable (crash, or a hang after the stack overflow), so they must not take the rest down.
    corpora = collections.OrderedDict((n, []) for n in CORPORA)
    cur = ['main']

    def use(name):
        cur[0] = name

    ids = collections.Counter()

    def add(kind, line):
        if line.startswith('FMT\t'):  # ids must be unique within a corpus (blocks are compared by id)
            f = line.split('\t')
            ids[(cur[0], f[1])] += 1
            if ids[(cur[0], f[1])] > 1:
                f[1] += '~%d' % ids[(cur[0], f[1])]
                line = '\t'.join(f)
        counts[kind] += 1
        corpora[cur[0]].append(line)

    def gender_keys(lang):
        """A few keys of `lang` with distinct Gender/Plurality values (most frequent first, then by tag)."""
        by = collections.defaultdict(list)
        for tag, (text, g, p) in rows[lang].items():
            if tag.startswith('TXT_KEY_AB_') or text is None or '{' in (text or ''):
                continue
            by[(g or '', p or '')].append(tag)
        order = sorted(by, key=lambda k: (-len(by[k]), k))
        picked = [min(by[k]) for k in order if k != ('', '')][:7]
        return picked, min(by[('', '')]) if by[('', '')] else 'TXT_KEY_UNIT_SETTLER'

    def synth_sets(types, lang_keys, none_key, nsets):
        """Argument lists for a key with the given slot types."""
        n = max(types) if types else 0
        g = lang_keys + [none_key] * 7
        base = [  # (num, key, any)
            ('i1', 's' + g[0], 'i1'),
            ('i2', 's' + g[1], 'nPlain text'),
            ('i21', 'k' + g[2], 'i5'),
            ('f2.25', 's' + none_key, 's' + none_key),
            ('i-3', 'sÜnïcødé ünit', 's'),
            ('d1234.5678', 's' + g[3], 'I1234567'),
            ('i1000', 's' + g[4], 'f0.005'),
        ][:nsets]
        sets = []
        for num, key, anyv in base:
            sets.append([{'num': num, 'key': key, 'any': anyv}[types.get(i, 'any')] for i in range(1, n + 1)])
        return sets

    def spec_sweep(lang, texts, lang_keys, none_key, numeric_only=False):
        specs = set()
        for text in texts:
            def visit(t):
                for seg in top_segments(t):
                    m = SEG_RX.match(seg)
                    if m and m.group(3):
                        specs.add(seg)
                    visit(seg)
            visit(text or '')
        nlines = 0
        for spec in sorted(specs):
            m = SEG_RX.match(spec)
            tr = (m.group(3) or '').lower()
            kind = 'key' if (m.group(1) or tr in KEY_T) else ('num' if tr in NUMERIC_T else 'any')
            if numeric_only and kind != 'num':
                continue
            idx = int(m.group(2))
            raw = '{' + spec + '}'
            if kind == 'num':
                vals = ['i%d' % v for v in INTS] + ['f' + f for f in FLOATS] + ['d' + f for f in FLOATS] + \
                       ['I' + v for v in INT64S] + ['sabc', 's12', 'k' + none_key]
            elif kind == 'key':
                vals = ['s' + k for k in lang_keys] + ['k' + k for k in lang_keys[:3]] + \
                       ['s' + none_key, 'i5', 'f1.5'] + ['s' + p for p in PLAIN[:3]]
            else:
                vals = ['i1', 'i-3', 'f2.25', 's' + lang_keys[0], 'sPlain']
            for v in vals:
                args = ['i1'] * (idx - 1) + [v]
                add('sweep-' + lang, fmt_line('sw:%s:%s:%s' % (lang, spec[:80], v[:40]), 'R', raw, args))
                nlines += 1
        return len(specs), nlines

    # ---- 1. en_US: every key, every key formatted, spec sweep --------------------------------------------------
    en_keys, en_none = gender_keys('en_US')
    add('meta', '# corpus 1: en_US gender keys %s, none %s' % (','.join(en_keys), en_none))
    add('meta', 'INIT\ten_US\t0')
    add('meta', 'SECTION\tkeys en_US')
    en_tags = sorted(t for t in rows['en_US'] if t not in CYCLES)
    for tag in en_tags:
        add('key-en_US', 'KEY\t' + esc(tag))
    add('meta', 'SECTION\tfmt en_US')
    for tag in en_tags:
        text = rows['en_US'][tag][0] or ''
        if '{' not in text:
            continue
        types = slot_types(text)
        sets = synth_sets(types, en_keys, en_none, 7)
        for j, args in enumerate(sets):
            add('fmt-en_US', fmt_line('%s#%d' % (tag, j), 'L', tag, args))
        add('fmt-en_US', fmt_line('%s#none' % tag, 'L', tag, []))  # too few
        add('fmt-en_US', fmt_line('%s#extra' % tag, 'L', tag, sets[0] + ['i9', 'sExtra']))  # too many
    add('meta', 'SECTION\tsweep en_US')
    spec_sweep('en_US', [v[0] for v in rows['en_US'].values()], en_keys, en_none)

    # ---- 2. stats.db keys --------------------------------------------------------------------------------------
    expect = {}
    if a.stats_x64:
        add('meta', 'SECTION\tstats')
        keys = collections.defaultdict(set)
        for p in a.stats_x64:
            c = sqlite3.connect('file:%s?mode=ro' % p.as_posix(), uri=True)
            for (t,) in c.execute("SELECT name FROM sqlite_master WHERE type='table'"):
                for col in [r[1] for r in c.execute('PRAGMA table_info("%s")' % t)]:
                    for (v,) in c.execute('SELECT DISTINCT "%s" FROM "%s" WHERE "%s" LIKE \'TXT_KEY%%\'' % (col, t, col)):
                        keys['%s.%s' % (t, col)].add(v)
            c.close()
        x86 = collections.defaultdict(set)
        for p in a.stats_x86:
            c = sqlite3.connect('file:%s?mode=ro' % p.as_posix(), uri=True)
            for tc in keys:
                t, col = tc.split('.', 1)
                try:
                    for (v,) in c.execute('SELECT DISTINCT "%s" FROM "%s" WHERE typeof("%s") = \'text\'' % (col, t, col)):
                        x86[tc].add(v)
                except sqlite3.Error:
                    pass
            c.close()
        for tc in sorted(keys):
            expect[tc] = sorted(x86[tc])
            for k in sorted(keys[tc]):
                add('stats', fmt_line('stat:%s:%s' % (tc, k), 'L', k, []))

    # ---- 3. Lua calls ------------------------------------------------------------------------------------------
    lua_files = []
    if a.lua_root:
        add('meta', 'SECTION\tlua')
        calls, lua_files = lua_calls(a.lua_root)
        counts['lua-calls-found'] = len(calls)
        seen = set()
        for rel, line, key, args in calls:
            types = slot_types((rows['en_US'].get(key) or ('',))[0] or '')
            out = []
            for i, tok in enumerate(args, 1):
                if re.fullmatch(r'-?\d+', tok):
                    out.append('i' + tok)
                elif re.fullmatch(r'-?\d*\.\d+', tok):
                    v = float(tok)
                    out.append(('i%d' % int(v)) if v == int(v) else ('d' + tok))
                elif re.fullmatch(r'"[^"\\]*"|\'[^\'\\]*\'', tok):
                    out.append('n' + tok[1:-1])
                elif tok in ('true', 'false'):
                    out.append('i1' if tok == 'true' else 'i0')
                elif tok == 'nil':
                    out.append('s')
                else:  # an expression: synthesize from the key's placeholder type
                    kind = types.get(i, 'any')
                    out.append({'num': 'i3', 'key': 'n' + en_keys[0], 'any': 'nLua text'}[kind])
            sig = (key, tuple(out))
            if sig in seen:
                continue
            seen.add(sig)
            add('lua', fmt_line('lua:%s:%d' % (rel, line), 'L', key, out))
        counts['lua-unique'] = len(seen)

    # ---- 4. edge cases -----------------------------------------------------------------------------------------
    add('meta', 'SECTION\tedge-fmt')
    for k, raw in enumerate(MALFORMED):
        for j, args in enumerate(([], ['i5'], ['i5', 'sTXT_KEY_AB_EDGE_FEM'], ['sTXT_KEY_AB_EDGE_FEM', 'i2'], ['0'])):
            add('edge', fmt_line('mal:%d:%d' % (k, j), 'R', raw, args))
    for tag in ['TXT_KEY_AB_DOES_NOT_EXIST', 'TXT_KEY_AB_EDGE_NEST1', 'TXT_KEY_AB_EDGE_NEST_ARGS',
                'TXT_KEY_AB_EDGE_TWO_ARGS', 'TXT_KEY_AB_EDGE_FORMS', 'TXT_KEY_AB_EDGE_EMPTY', 'TXT_KEY_AB_EDGE_NULL',
                'TXT_KEY_AB_EDGE_BAD_PLURALITY', 'TXT_KEY_AB_EDGE_BAD_GENDER', 'TXT_KEY_AB_EDGE_PIPES', '',
                'txt_key_unit_settler', 'TXT_KEY_UNIT_SETTLER ', ' TXT_KEY_UNIT_SETTLER']:
        for j, args in enumerate([[], ['i4'], ['i4', 'sTXT_KEY_AB_EDGE_FEM'], ['sabc', 'i7'],
                                  ['i4', 'kTXT_KEY_AB_EDGE_FORMS'], ['i4', 'rraw {1_X}'], ['f1.5', 'n'],
                                  ['i1', 'i2', 'i3', 'i4', 'i5']]):
            add('edge', fmt_line('edge:%s:%d' % (tag, j), 'L', tag, args))
    add('edge', fmt_line('edge:nested-raw', 'R', '{TXT_KEY_AB_EDGE_TWO_ARGS}', ['i4', 'sTXT_KEY_AB_EDGE_FEM']))
    add('edge', fmt_line('edge:nested-raw2', 'R', 'x {TXT_KEY_AB_EDGE_NEST_ARGS} y', ['i4', 'sTXT_KEY_AB_EDGE_FEM']))
    for k in ['TXT_KEY_UNIT_SETTLER', 'TXT_KEY_SPECIALIST_ARTIST', 'TXT_KEY_AB_EDGE_FORMS', 'TXT_KEY_AB_DOES_NOT_EXIST',
              '', 'TXT_KEY_AB_EDGE_FEM']:
        add('meta', 'DICTKEY\t' + esc(k))
    add('meta', 'FORMKEY\tTXT_KEY_AB_EDGE_FORMS')
    add('meta', 'EDGE')

    # ---- 5. the other languages --------------------------------------------------------------------------------
    for lang, _, _ in OTHER_LANGS:
        keys, none = gender_keys(lang)
        add('meta', '# %s gender keys %s, none %s' % (lang, ','.join(keys), none))
        add('meta', 'LANG\t' + lang)
        add('meta', 'SECTION\tkeys ' + lang)
        for tag in sorted(rows[lang]):
            add('key-other', 'KEY\t' + esc(tag))
        add('meta', 'SECTION\tfmt ' + lang)
        for tag in sorted(rows[lang]):
            text = rows[lang][tag][0] or ''
            if '{' not in text:
                continue
            for j, args in enumerate(synth_sets(slot_types(text), keys, none, 3)):
                add('fmt-other', fmt_line('%s:%s#%d' % (lang, tag, j), 'L', tag, args))
        add('meta', 'SECTION\tsweep ' + lang)
        spec_sweep(lang, [v[0] for v in rows[lang].values()], keys, none)
        # en_US-only keys through the fallback
        for tag in [t for t in en_tags if t not in rows[lang]][:200]:
            add('fallback', fmt_line('fb:%s:%s' % (lang, tag), 'L', tag, ['i2', 's' + keys[0]]))

    # ---- 6. plural-rule probe ----------------------------------------------------------------------------------
    choice = '{1: plural 1?one; 2?two; 3?three; 4?four; 5?five; 6?six; other?other;}'
    choice_at = '{1_N} {1_N: plural 1?one; 2?two; 3?three; 4?four; 5?five; 6?six; other?other;}'
    nvals = list(range(-3, 31)) + [100, 101, 102, 105, 111, 112, 121, 1000, 1001, 1000000]
    for r in PROBE_RULES:
        lang = 'zz_R%02d' % r
        add('meta', 'LANG\t' + lang)
        add('plural', 'PLURAL\t' + lang)
        for n in nvals:
            add('plural', fmt_line('pl:%s:%d' % (lang, n), 'R', choice, ['i%d' % n]))
        for f in FLOATS:
            add('plural', fmt_line('pl:%s:f%s' % (lang, f), 'R', choice_at, ['f' + f]))
        for k in ['TXT_KEY_AB_EDGE_FORMS', 'TXT_KEY_SPECIALIST_ARTIST', 'TXT_KEY_AB_EDGE_FEM']:
            add('plural', fmt_line('pl:%s:%s' % (lang, k), 'R', '{@1: plural 1?one; 2?two; other?other;} {1_X}',
                                   ['s' + k]))
    add('meta', 'LANG\ten_US')

    # ---- 7. fallback disabled (own process) ---------------------------------------------------------------------
    sample = en_tags[::150] + sorted(set(EDGE_KEYS) - CYCLES)
    en_only = [t for t in en_tags if t not in rows['de_DE']][:100]
    use('nofallback')
    add('meta', 'INIT\ten_US\t1')
    for tag in sample:
        add('nofallback', 'KEY\t' + esc(tag))
    add('meta', 'LANG\tde_DE')
    for tag in en_only + sorted(rows['de_DE'])[:100]:
        add('nofallback', fmt_line('nofb:de_DE:%s' % tag, 'L', tag, ['i2']))

    # ---- 8. ShutdownDictionary + InitializeDictionary (own process) --------------------------------------------
    use('dictinit')
    add('meta', 'INIT\ten_US\t0')
    add('dictinit', 'KEY\tTXT_KEY_UNIT_SETTLER')
    add('meta', 'DICTINIT\ten_US\t1')
    for tag in sample[:60]:
        add('dictinit', 'KEY\t' + esc(tag))
    add('meta', 'DICTINIT\tde_DE\t0')
    for tag in en_only[:50] + sorted(rows['de_DE'])[:50]:
        add('dictinit', fmt_line('dictinit-de:%s' % tag, 'L', tag, ['i2']))
    add('meta', 'DICTINIT\tno_SUCH\t0')
    add('dictinit', 'KEY\tTXT_KEY_UNIT_SETTLER')

    # ---- 9. Shutdown + Initialize again (own process) ----------------------------------------------------------
    use('reinit')
    add('meta', 'INIT\ten_US\t0')
    add('reinit', 'KEY\tTXT_KEY_UNIT_SETTLER')
    add('meta', 'REINIT\tde_DE\t0')
    for tag in en_only[:20] + sorted(rows['de_DE'])[:20]:
        add('reinit', fmt_line('reinit-de:%s' % tag, 'L', tag, ['i2']))

    # ---- 10. endless recursion (own process; may exhaust the stack and hang) -----------------------------------
    use('cycles')
    add('meta', 'INIT\ten_US\t0')
    for tag in ['TXT_KEY_AB_EDGE_SELF', 'TXT_KEY_AB_EDGE_CYCLE_A']:
        add('cycle', fmt_line('cycle:' + tag, 'L', tag, []))
        add('cycle', 'KEY\tTXT_KEY_UNIT_SETTLER')

    for name, lines in corpora.items():
        (a.out / f'corpus-{name}.txt').write_bytes(('\n'.join(lines) + '\n').encode('utf-8'))
    (a.out / 'stats-expect.json').write_text(json.dumps(expect, indent=0, ensure_ascii=False, sort_keys=True),
                                             encoding='utf-8')
    counts['lua-files'] = len(lua_files)
    (a.out / 'corpus-counts.json').write_text(json.dumps(dict(sorted(counts.items())), indent=1))
    print(json.dumps(dict(sorted(counts.items()))))


if __name__ == '__main__':
    sys.exit(main())
