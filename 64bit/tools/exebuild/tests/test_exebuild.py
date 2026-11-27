"""Tests of tools/exebuild (ticket 4960, epic 0590, D202): the build list, the catalogue, gen_table.py, emit_header.py, the
run-time headers, and that no generated table can reach git.

    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 && python -I -m pytest -q tools/exebuild/tests

Needs the golden EXE copies (read only) and the local tables of both builds in the exe_tables folder (setting exe_tables,
C5_EXE_TABLES); a test whose input is absent FAILS naming it (a skipped test counts as a failed run, COMMON rule 9).
Assertion messages name symbols and counts, never a VA of a new build (D202).
"""
from __future__ import annotations

import csv
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
EXEBUILD = HERE.parent
REPO = EXEBUILD.parents[1]
sys.path.insert(0, str(EXEBUILD))
import table  # noqa: E402
from table import builds  # noqa: E402
import gen_table  # noqa: E402
import emit_header  # noqa: E402

GOLDEN = Path('F:/c5-64bit-work/golden')
OLD_EXE = GOLDEN / 'civ5-steam-build4390913-ws' / 'CivilizationV_DX11.exe'
NEW_EXE = GOLDEN / 'civ5-steam-build26577625-ws' / 'CivilizationV_DX11.exe'
NEW = '26577625'
CLANG = r'C:\Program Files\LLVM\bin\clang-cl.exe'
VS2008 = r'C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat'
VCVARS64 = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
INCLUDE = REPO / 'exebuild' / 'include'


def need(p: Path, what: str) -> Path:
    if not Path(p).exists():
        pytest.fail(f'{what} missing: {p}')
    return Path(p)


def run_py(script, args, env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    cp = subprocess.run([sys.executable, '-I', str(EXEBUILD / script)] + [str(a) for a in args], capture_output=True,
                        text=True, errors='replace', env=e, cwd=str(REPO))
    return cp.returncode, cp.stdout + cp.stderr


# ------------------------------------------------------------------------------------------------- the build list
def test_builds_py_equals_vp_exe_build_h():
    h = (INCLUDE / 'vp_exe_build.h').read_text(encoding='utf-8')
    rows = re.findall(r'B\((\d+), "(\d+)",\s*(0x[0-9A-Fa-f]+)u, (0x[0-9A-Fa-f]+)u, "([^"]*)"\)', h)
    assert len(rows) == len(builds.KNOWN) >= 2
    for (i, sb, ts, soi, label), b in zip(rows, builds.KNOWN):
        assert (int(i), sb, int(ts, 16), int(soi, 16), label) == (b.id, b.steam_build, b.stamp, b.size_of_image, b.label)
        assert re.search(r'kVpExeBuild%s = %d,' % (sb, b.id), h)
    assert re.search(r'kVpExeBuildCount = %d' % len(builds.KNOWN), h)
    assert builds.ANCHOR.steam_build == '4390913'


def test_product_ready_equals_vp_exe_build_h():
    """VpExeBuildProductReady (the header's `return id == kVpExeBuildN || ...;`) names the builds of PRODUCT_READY."""
    h = (INCLUDE / 'vp_exe_build.h').read_text(encoding='utf-8')
    m = re.search(r'inline bool VpExeBuildProductReady\(int id\)\s*\{\s*return ([^;]*);', h)
    assert m, 'VpExeBuildProductReady not found in vp_exe_build.h'
    named = re.findall(r'id == kVpExeBuild(\d+)', m.group(1))
    assert sorted(named) == sorted(builds.PRODUCT_READY), (named, builds.PRODUCT_READY)
    assert all(builds.by_steam_build(sb) for sb in builds.PRODUCT_READY)
    assert builds.product_ready(builds.ANCHOR) and builds.product_ready('4390913')
    # every build named ready is known; a build not named is not ready (the 0588 path), until the lead flips it
    for b in builds.KNOWN:
        assert builds.product_ready(b) == (b.steam_build in named)


def test_unknown_header_is_unknown():
    assert builds.from_header(0x12345678, 0x06519000) is None
    assert builds.from_header(0x546CD0A8, 0x06000000) is None   # the stamp alone is not enough
    assert builds.from_header(0x546CD0A8, 0x06519000).steam_build == '4390913'


def test_golden_exes_are_the_known_builds():
    assert builds.of_file(need(OLD_EXE, 'golden EXE of build 4390913')).steam_build == '4390913'
    assert builds.of_file(need(NEW_EXE, 'golden EXE of build 26577625')).steam_build == NEW


# --------------------------------------------------------------------------------------------------- the catalogue
def test_catalogue_loads_and_pilot_is_complete():
    syms = table.symbols()
    names = [s.name for s in syms]
    assert len(names) == len(set(names))
    cam = re.findall(r'#define VPCAM_VA_\w+ (0x[0-9A-Fa-f]+)u', (REPO / 'shell/common/exe_camera.h').read_text())
    anchors = {s.anchor for s in syms}
    missing = [v for v in cam if int(v, 16) not in anchors]
    assert not missing, f'exe_camera.h VPCAM_VA_ values not in the catalogue: {missing}'
    kinds = {s.kind for s in syms}
    assert {'code', 'data', 'iat', 'vtable'} <= kinds
    assert table.anchor('cam_app') == 0xCC8E74


def write_cat(d: Path, rows, area='tst'):
    d.mkdir(parents=True, exist_ok=True)
    (d / f'{area}.tsv').write_text('# test\n' + '\t'.join(table.HEADER) + '\n' +
                                   ''.join('\t'.join(r) + '\n' for r in rows), encoding='utf-8')
    return d


@pytest.mark.parametrize('rows, why', [
    ([['Tst_fn', 'code', '0x401000', 'x', '']], 'not <area>_<rest>'),
    ([['tstfn', 'code', '0x401000', 'x', '']], 'not <area>_<rest>'),
    ([['tst_fn', 'code', '0x401000', 'x', ''], ['tst_fn', 'data', '0x402000', 'x', '']], 'already at'),
    ([['tst_fn', 'func', '0x401000', 'x', '']], 'kind'),
    ([['tst_fn', 'code', '401000', 'x', '']], 'hex VA'),
    ([['tst_iat', 'iat', '0x401000', 'x', 'InterlockedExchangeAdd']], 'DLL!Function'),
    ([['tst_a', 'array', '-', 'x', 'file=nope.inc;cols=code']], 'not found'),
    ([['tst_fn', 'code', '0x401000', '', '']], 'no consumer'),
    ([['tst_fn', 'code', '0x401000', 'x']], 'columns'),
])
def test_catalogue_refuses(tmp_path, rows, why):
    with pytest.raises(table.CatalogueError, match=re.escape(why)):
        table.symbols(write_cat(tmp_path / 'c', rows))


# ------------------------------------------------------------------------------------------------ the local tables
def tables_dir():
    try:
        return table.tables_dir()
    except table.TableError as e:
        pytest.fail(str(e))


def test_identity_table_equals_anchors():
    t = table.load('4390913', tables_dir(), chash=table.catalogue_hash())
    assert t['complete'] and t['mode'] == 'identity' and not t['unresolved']
    bad = [s.name for s in table.symbols() if s.anchor is not None and table.va('4390913', s.name) != s.anchor]
    assert not bad, f'identity table differs from the anchors at {bad}'
    assert all(p['result'] == 'OK' for p in t['patterns'])


def test_new_table_complete_and_camera_patterns_ok():
    t = table.load(NEW, tables_dir(), chash=table.catalogue_hash())
    assert t['complete'] and not t['unresolved'], f'unresolved: {t["unresolved"]}'
    cam = [p for p in t['patterns'] if p['file'] == 'shell/common/exe_camera.h']
    assert len(cam) == 11 and all(p['result'] == 'OK' for p in cam), [p['name'] for p in cam if p['result'] != 'OK']
    assert t['catalogue_sha256'] == table.catalogue_hash(), 'the local table is stale: run gen_table.py again'


def test_pilot_values_agree_with_0582_map():
    """0582's map (D202: local copy beside the table, never in git) and this generator agree on every pilot symbol it
    has; reports counts and names only."""
    m = tables_dir() / NEW / 'map-product-vas-from-0582.csv'
    if not m.is_file():
        pytest.fail(f'0582 map missing: {m} (setting `{table.SETTING}` / {table.SETTING_ENV}; the lead keeps the local '
                    f'copy there)')
    mp = {}
    with open(m, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            if row['new_va']:
                mp[int(row['old_va'], 16)] = int(row['new_va'], 16)
    compared, differ, known = 0, [], []
    for s in table.symbols():
        if s.anchor in mp:
            compared += 1
            if table.va(NEW, s.name) != mp[s.anchor]:
                (known if s.name in MAP_0582_KNOWN_WRONG else differ).append(s.name)
    assert compared >= 20, f'only {compared} pilot symbols are in the 0582 map'
    assert not differ, f'{len(differ)} of {compared} differ from 0582: {differ}'
    # the map's known errors must still differ (the table holds the hand answer, not the map's carry)
    assert sorted(known) == sorted(n for n in MAP_0582_KNOWN_WRONG if n in {s.name for s in table.symbols()}), known


# Symbols where 0582's map is known to be wrong (the table's hand answer is right): ticket 4963 found the two
# near-identical uidraw wrappers swapped by the same matcher (research/02-exe-internals/exe-build-table.md 11.2,
# cases 5 and 6: the CCommandStream destructor's own calls name them).
MAP_0582_KNOWN_WRONG = ('uid_delete_array', 'uid_free_aligned')


def seed_hand(tables):
    """Copy the local hand answers of the new build, when there are any, into a scratch tables folder (answers for
    symbols another catalogue has are skipped by gen_table with a note)."""
    hand = tables_dir() / NEW / 'hand-review.tsv'
    (Path(tables) / NEW).mkdir(parents=True, exist_ok=True)
    if hand.is_file():
        shutil.copy(hand, Path(tables) / NEW / 'hand-review.tsv')


def test_gen_table_reproduces_both_tables(tmp_path):
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', tmp_path])
    assert rc == 0, out
    seed_hand(tmp_path)
    rc, out = run_py('gen_table.py', ['--old', old, '--new', new, '--steam-build', NEW, '--tables', tmp_path])
    assert rc == 0, out
    m = re.search(r'patterns (\d+) of (\d+) OK', out)   # every catalogue's patterns (11 camera rows in the pilot)
    assert m and m.group(1) == m.group(2) and int(m.group(1)) >= 11, out
    assert not re.search(r'0x[0-9A-F]{6,8}', out.replace('0x546CD0A8', '')), 'gen_table printed an address'
    for b in ('4390913', NEW):
        mine = json.loads((tmp_path / b / 'table.json').read_text())['symbols']
        ref = table.load(b, tables_dir(), chash=table.catalogue_hash())['symbols']
        # values only: the methods may differ from a table made by an older generator (4960c's look-alike routes)
        differ = [n for n in ref if mine.get(n, {}).get('va') != ref[n]['va']]
        assert not differ and set(mine) == set(ref), f'build {b}: not reproducible at {differ[:10]}'


def test_array_kind_carries_cells(tmp_path):
    """An array row (here a small list over pilot anchors and two .shader blobs of the create-site table) is carried per
    column kind; `keep` is copied; the cells equal the single symbols' values."""
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    inc = tmp_path / 'tst_sites.inc'
    inc.write_text('// test\n{0x51DF70u, 5u, 0xCC8E74u, 0x0619b000u},\n{0x8E4A80u, 6u, 0xD90FA4u, 0x0619b2a8u},\n',
                   encoding='utf-8')
    cat = tmp_path / 'cat'
    shutil.copytree(table.SYMBOLS_DIR, cat)
    with open(cat / 'render.tsv', 'a', encoding='utf-8') as f:
        f.write(f'render_tst_sites\tarray\t-\trender/x.cpp\tfile={inc.as_posix()};cols=code,keep,data,shader\n')
    t = tmp_path / 't'
    seed_hand(t)
    rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', t, '--symbols', cat])
    assert rc == 0, out
    ident =json.loads((t / '4390913' / 'table.json').read_text())['arrays']['render_tst_sites']['rows']
    assert ident == [['0x51DF70', 5, '0xCC8E74', '0x619B000'], ['0x8E4A80', 6, '0xD90FA4', '0x619B2A8']]
    rc, out = run_py('gen_table.py', ['--old', old, '--new', new, '--steam-build', NEW, '--tables', t, '--symbols', cat])
    assert rc == 0, out
    doc = json.loads((t / NEW / 'table.json').read_text())
    a = doc['arrays']['render_tst_sites']
    s = doc['symbols']
    assert a['unresolved_cells'] == 0 and a['cols'] == ['code', 'keep', 'data', 'shader']
    assert a['rows'][0][:3] == [s['cam_fn_updateviews']['va'], 5, s['cam_app']['va']]
    assert a['rows'][1][:3] == [s['cam_fcamera_getviewproj']['va'], 6, s['cam_numviews']['va']]
    shift = int(a['rows'][0][3], 16) - 0x619B000
    assert shift == int(a['rows'][1][3], 16) - 0x619B2A8 and shift != 0
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen', '--tables', t, '--symbols', cat])
    n_arrays = 1 + sum(s.kind == 'array' for s in table.symbols())
    assert rc == 0 and f'{n_arrays} arrays' in out, out
    assert 'VPXA_render_tst_sites' in (tmp_path / 'gen' / emit_header.GEN_NAME).read_text()


# ---- ticket 4960c: look-alike wrappers (W2's finding on build 26577625) and the C(name) call-target token
WRAPPERS = {'delete_array': 0x813D10, 'free_aligned': 0x813CF0, 'dtor': 0x8CCD20}   # build-4390913 anchors (4963)


def wrapper_catalogue(tmp_path):
    """This catalogue plus the CCommandStream destructor and its two callee wrappers (proxy/src/proxy_uidraw.cpp). A
    wrapper the catalogue already has (4963's uid_* rows) keeps its name: one row per anchor. Returns (dir, names)."""
    cat = tmp_path / 'cat-wrappers'
    shutil.copytree(table.SYMBOLS_DIR, cat)
    have = {s.anchor: s.name for s in table.symbols(cat)}
    names, rows = {}, []
    for k, va in WRAPPERS.items():
        names[k] = have.get(va) or f'uidt_{k}'
        if va not in have:
            rows.append(f'uidt_{k}\tcode\t0x{va:X}\tproxy/src/proxy_uidraw.cpp\ttest row (4960c)\n')
    with open(cat / 'proxy.tsv', 'a', encoding='utf-8') as f:
        f.writelines(rows)
    return cat, names


def wrapper_patterns(tmp_path, names):
    """The destructor's calls: delete[] at +0x13 and +0x2A, the aligned free at +0x42 (exe-build-table.md 11.2 of 4963)."""
    hdr = tmp_path / 'wrapper_patterns.h'
    d, a, f = names['dtor'], names['delete_array'], names['free_aligned']
    hdr.write_text('#define T(X) \\\n'
                   f'\tX({d}, "tst dtor calls delete[] at +0x13", "{"?? " * 0x13}E8 C({a})") \\\n'
                   f'\tX({d}, "tst dtor calls delete[] at +0x2A", "{"?? " * 0x2A}E8 C({a})") \\\n'
                   f'\tX({d}, "tst dtor calls the aligned free at +0x42", "{"?? " * 0x42}E8 C({f})")\n',
                   encoding='utf-8')
    return hdr


def hand_value(name_suffix):
    """W2's answer for a wrapper in the shared hand-review.tsv (uid_<suffix>), or a failure naming the file."""
    p = tables_dir() / NEW / 'hand-review.tsv'
    for line in p.read_text(encoding='utf-8').splitlines() if p.is_file() else []:
        c = line.split('\t')
        if c[0] == f'uid_{name_suffix}':
            return int(c[1], 16)
    pytest.fail(f'{p}: no answer for uid_{name_suffix} (W2, ticket 4963)')


def test_look_alike_wrappers_are_not_carried_by_their_bytes(tmp_path):
    """Regression (W2, 4963): uid_delete_array and uid_free_aligned are 0x20-byte wrappers whose masked bytes stand twice
    in the old .text; the byte windows carried them swapped. Now their bodies count as ambiguous and only the callers'
    vote may carry them: it must give W2's hand answers, and the C(name) patterns of the destructor must hold."""
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    cat, names = wrapper_catalogue(tmp_path)
    hdr = wrapper_patterns(tmp_path, names)
    t = tmp_path / 't'
    # the shared hand answers without the wrappers' own: the matcher alone decides the wrappers
    seed_hand(t)
    hp = t / NEW / 'hand-review.tsv'
    hp.write_text(''.join(ln for ln in hp.read_text(encoding='utf-8').splitlines(True)
                          if not ln.startswith(('uid_delete_array\t', 'uid_free_aligned\t'))), encoding='utf-8')
    rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', t, '--symbols', cat, '--patterns', hdr])
    assert rc == 0 and 'tst dtor calls the aligned free' in out, out
    rc, out = run_py('gen_table.py', ['--old', old, '--new', new, '--steam-build', NEW, '--tables', t, '--symbols', cat,
                                      '--patterns', hdr])
    assert rc in (0, 4), out
    doc = json.loads((t / NEW / 'table.json').read_text())
    for k in ('delete_array', 'free_aligned'):
        e = doc['symbols'].get(names[k])
        if e is None:   # not carried: it must be in the review list, not carried wrongly
            assert names[k] in doc['unresolved']
            continue
        assert 'body ambiguous' in e['method'], (k, e['method'])
        assert int(e['va'], 16) == hand_value(k), f'{k} carried to another wrapper'
    pats = {p['name']: p['result'] for p in doc['patterns'] if p['name'].startswith('tst dtor')}
    if all(names[k] in doc['symbols'] for k in ('delete_array', 'free_aligned')):
        assert set(pats.values()) == {'OK'}, pats


WRAPPER_CALL_SITES = [('cam_fcamera_dtor', 0x11), ('decal_add_batches', 0xE5), ('decal_add_chunk', 0xA4),
                      ('decal_texunload_mgr', 0xE3), ('decal_texunload_node', 0xC0), ('decal_farmread_variants', 0x96),
                      ('cut_freed', 0xC), ('forest_layout_count', 0x4E), ('forest_layout_count', 0xB5),
                      ('fg_shutdown_flag', 0x1F)]   # lead-0590's independent check, 2026-11-20 (the 11th: the destructor)


def test_wrapper_answers_agree_with_carried_call_sites():
    """lead-0590's check as a test: at each call site inside another carried function, the old call goes to one of the
    two look-alike wrappers and the new call to that wrapper's hand answer; match_code now gives the same answers."""
    import addr_match as am
    old, new = am.Img(str(need(OLD_EXE, 'golden old EXE'))), am.Img(str(need(NEW_EXE, 'golden new EXE')))
    want = {0x813D10: hand_value('delete_array'), 0x813CF0: hand_value('free_aligned')}
    have = {s.name for s in table.symbols()}
    checked = 0
    for name, off in WRAPPER_CALL_SITES:
        if name not in have:
            continue
        o = table.anchor(name) + off - old.base - old.t_rva
        n = table.va(NEW, name) + off - new.base - new.t_rva
        ot, nt = am._call_target(old, o), am._call_target(new, n)
        assert ot in want, (name, off, 'old call target not a wrapper')
        assert nt == want[ot], (name, off, 'new call target is not the hand answer of the same wrapper')
        checked += 1
    assert checked >= 8, checked
    for anchor_va, value in want.items():
        assert am.match_code(old, new, anchor_va)[0] == value


def test_lookalike_vtable_entries_not_carried_elsewhere():
    """lead-0590's material: two entries (54, 82) of pac_util_vftable are functions whose first 32 masked bytes stand at
    9 places in the old .text; the old byte windows carried them `back16 high` to addresses that are not the new
    vtable's entries. With the gate they are carried to the new vtable's entry at the same index, or not at all."""
    import addr_match as am
    if 'pac_util_vftable' not in {s.name for s in table.symbols()}:
        pytest.fail('pac_util_vftable not in the catalogue (proxy carry 4963)')
    old, new = am.Img(str(need(OLD_EXE, 'golden old EXE'))), am.Img(str(need(NEW_EXE, 'golden new EXE')))
    ov, nv = table.anchor('pac_util_vftable'), table.va(NEW, 'pac_util_vftable')
    for k in (54, 82):
        oe = old.pe.u32(ov - old.base + 4 * k)
        ne = new.pe.u32(nv - new.base + 4 * k)
        assert am.body_ambiguous(old, oe)
        r = am.match_code(old, new, oe)
        assert r[0] in (None, ne), (k, r[1])


def test_call_target_pattern_refuses_a_swap(tmp_path):
    """Negative: hand answers that swap the two wrappers make the destructor's C(name) patterns BAD: refused (exit 1)."""
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    cat, names = wrapper_catalogue(tmp_path)
    hdr = wrapper_patterns(tmp_path, names)
    t = tmp_path / 't'
    seed_hand(t)   # every other answer as shared: only the swap may make anything BAD
    a, f = hand_value('delete_array'), hand_value('free_aligned')
    hp = t / NEW / 'hand-review.tsv'
    keep = ''.join(ln for ln in hp.read_text(encoding='utf-8').splitlines(True)
                   if not ln.startswith(('uid_delete_array\t', 'uid_free_aligned\t')))
    hp.write_text(keep + f'{names["delete_array"]}\t0x{f:X}\ttest: swapped on purpose\n'
                  f'{names["free_aligned"]}\t0x{a:X}\ttest: swapped on purpose\n', encoding='utf-8')
    rc, out = run_py('gen_table.py', ['--old', old, '--new', new, '--steam-build', NEW, '--tables', t, '--symbols', cat,
                                      '--patterns', hdr])
    assert rc == 1 and 'REFUSED' in out, out
    m = re.search(r'REFUSED: (\d+) BAD', out)
    assert m and m.group(1) == '3', out   # exactly the destructor's three C() rows
    for p in ('tst dtor calls delete[] at +0x13', 'tst dtor calls delete[] at +0x2A',
              'tst dtor calls the aligned free at +0x42'):
        assert re.search(r'BAD .*' + re.escape(p), out), (p, out)
    assert out.count('BAD') >= 3, out
    assert not (t / NEW / 'table.json').exists()


def test_call_target_token_syntax():
    """C(...) must follow a literal E8/E9; its rel32 is from the end of the instruction."""
    with pytest.raises(gen_table.GenError, match='must follow a literal E8 or E9'):
        gen_table.parse_template('8B C(cam_app)')
    toks = gen_table.parse_template('90 E8 C(cam_fn_updateviews)')
    exp = gen_table.expected_bytes(toks, lambda o: 0x51DF70, 0x401000)
    assert exp[:2] == [0x90, 0xE8]
    assert int.from_bytes(bytes(exp[2:6]), 'little') == (0x51DF70 - (0x401000 + 6)) & 0xFFFFFFFF


# ------------------------------------------------------------------------------------------- negative exit codes
def test_gen_table_refuses_a_bad_pattern(tmp_path):
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    hdr = tmp_path / 'bad_patterns.h'
    hdr.write_text('#define T(X) \\\n\tX(cam_fn_updateviews, "tst wrong bytes", "55 8B EC 00") \\\n'
                   '\tX(0x51DF70, "tst unknown operand", "55 R(tst_nope_operand)")\n', encoding='utf-8')
    seed_hand(tmp_path / 't')   # the hand answers (a carrier's catalogue can need them for its patterns' operands)
    args = ['--old', old, '--new', new, '--steam-build', NEW, '--tables', tmp_path / 't', '--patterns', hdr]
    rc, out = run_py('gen_table.py', args)
    assert rc == 1, out
    assert 'REFUSED' in out and 'tst wrong bytes' in out and 'tst unknown operand' in out
    assert not (tmp_path / 't' / NEW / 'table.json').exists()
    rc, out = run_py('gen_table.py', args + ['--allow-bad', 'tst wrong bytes'])
    assert rc == 1 and 'tst unknown operand' in out
    rc, out = run_py('gen_table.py', args + ['--allow-bad', 'tst wrong bytes', '--allow-bad', 'tst unknown operand'])
    assert rc == 4, out   # written, but a BAD pattern makes it incomplete
    t = json.loads((tmp_path / 't' / NEW / 'table.json').read_text())
    assert t['complete'] is False


def test_gen_table_refuses_wrong_inputs(tmp_path):
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    rc, out = run_py('gen_table.py', ['--old', new, '--new', old, '--steam-build', NEW, '--tables', tmp_path])
    assert rc == 2 and 'not the DX11 EXE of the anchor build' in out
    rc, out = run_py('gen_table.py', ['--old', old, '--new', new, '--steam-build', '4390913', '--tables', tmp_path])
    assert rc == 2 and '--steam-build says' in out
    rc, out = run_py('gen_table.py', ['--old', tmp_path / 'nope.exe', '--identity', '--tables', tmp_path])
    assert rc == 2 and 'not found' in out


def test_hand_review_overrides_skips_other_catalogues_and_refuses_bad_names(tmp_path):
    old = need(OLD_EXE, 'golden old EXE')
    t = tmp_path / 't'
    (t / '4390913').mkdir(parents=True)
    (t / '4390913' / 'hand-review.tsv').write_text('name\tva\treference\nNot A Name\t0x401000\tx.md\n')
    rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', t])
    assert rc == 2 and 'is not a symbol name' in out
    # an answer for another checkout's symbol (the folder is shared): skipped with a note (ticket 4966)
    (t / '4390913' / 'hand-review.tsv').write_text('name\tva\treference\ntst_nobody\t0x401000\tx.md\n')
    rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', t])
    assert rc == 0 and 'symbols not in this catalogue skipped: tst_nobody' in out, out
    (t / '4390913' / 'hand-review.tsv').write_text('name\tva\treference\ncam_app\t0xCC8E74\texe-build-table.md\n')
    rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', t])
    assert rc == 0, out
    e = json.loads((t / '4390913' / 'table.json').read_text())['symbols']['cam_app']
    assert e['confidence'] == 'hand' and e['method'] == 'hand: exe-build-table.md'


def no_settings_env(tmp_path):
    empty = tmp_path / 'empty-settings.ini'
    empty.write_text('# no keys\n', encoding='utf-8')
    return {'C5_SETTINGS_FILE': str(empty), 'C5_EXE_TABLES': ''}


def test_emit_without_setting_fails_naming_it(tmp_path):
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen'], env=no_settings_env(tmp_path))
    assert rc == 2, out
    for word in ('exe_tables', 'C5_EXE_TABLES', 'gen_table.py', 'deps/local-settings.ini'):
        assert word in out, (word, out)
    assert not (tmp_path / 'gen' / emit_header.GEN_NAME).exists()
    rc, out = run_py('gen_table.py', ['--identity', '--old', need(OLD_EXE, 'golden old EXE')],
                     env=no_settings_env(tmp_path))
    assert rc == 2 and 'exe_tables' in out and 'C5_EXE_TABLES' in out


def test_emit_with_a_missing_table_fails(tmp_path):
    (tmp_path / 't' / '4390913').mkdir(parents=True)
    t0 = table.load('4390913', tables_dir(), chash=table.catalogue_hash())
    (tmp_path / 't' / '4390913' / 'table.json').write_text(json.dumps(t0), encoding='utf-8')
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen', '--tables', tmp_path / 't'])
    assert rc == 2 and f'no address table for build {NEW}' in out and 'gen_table.py' in out and 'exe_tables' in out
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen'], env={'C5_EXE_TABLES': str(tmp_path / 't')})
    assert rc == 2 and 'C5_EXE_TABLES' in out


def test_emit_prefers_the_table_of_its_own_catalogue(tmp_path):
    """Ticket 4966: two checkouts with different catalogues share the exe_tables folder: table.json is whichever was
    generated last, table-<catalogue hash>.json each one's own. The emitter takes its own."""
    for b in ('4390913', NEW):
        mine = table.load(b, tables_dir(), chash=table.catalogue_hash())
        (tmp_path / 't' / b).mkdir(parents=True)
        other = dict(mine, catalogue_sha256='0' * 64)
        (tmp_path / 't' / b / 'table.json').write_text(json.dumps(other), encoding='utf-8')
        (tmp_path / 't' / b / f'table-{mine["catalogue_sha256"][:16]}.json').write_text(json.dumps(mine),
                                                                                         encoding='utf-8')
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen', '--tables', tmp_path / 't'])
    assert rc == 0, out
    for b in ('4390913', NEW):
        (tmp_path / 't' / b / f'table-{table.catalogue_hash()[:16]}.json').unlink()
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen', '--tables', tmp_path / 't'])
    assert rc == 2 and 'stale' in out


def two_catalogues(tmp_path):
    """Catalogue A = this checkout's, catalogue B = A plus one row (another branch's carrier)."""
    a = tmp_path / 'cat-a'
    b = tmp_path / 'cat-b'
    shutil.copytree(table.SYMBOLS_DIR, a)
    shutil.copytree(table.SYMBOLS_DIR, b)
    with open(b / 'server.tsv', 'a', encoding='utf-8') as f:
        f.write('srv_tst_other_branch\tdata\t0xCC8E78\tserver/x.cpp\ttest row of another branch\n')
    return a, b


def test_two_catalogues_share_one_folder_and_each_emitter_picks_its_own(tmp_path):
    """Ticket 4960b: two branches' catalogues, both builds' tables generated into ONE exe_tables folder (B last, so
    table.json is B's); the emitter of each finds its own table-<hash>.json; the headers differ by B's row."""
    old, new = need(OLD_EXE, 'golden old EXE'), need(NEW_EXE, 'golden new EXE')
    a, b = two_catalogues(tmp_path)
    t = tmp_path / 'shared'
    seed_hand(t)
    for cat in (a, b):
        rc, out = run_py('gen_table.py', ['--identity', '--old', old, '--tables', t, '--symbols', cat])
        assert rc == 0, out
        rc, out = run_py('gen_table.py', ['--old', old, '--new', new, '--steam-build', NEW, '--tables', t,
                                          '--symbols', cat])
        assert rc in (0, 4), out   # B's test row may not carry; A must
    ha, hb = table.catalogue_hash(a), table.catalogue_hash(b)
    assert ha != hb
    for build in ('4390913', NEW):
        names = sorted(p.name for p in (t / build).glob('table*.json'))
        assert names == sorted(['table.json', f'table-{ha[:16]}.json', f'table-{hb[:16]}.json']), names
        assert json.loads((t / build / 'table.json').read_text())['catalogue_sha256'] == hb   # the last generated
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen-a', '--tables', t, '--symbols', a])
    assert rc == 0, out
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen-b', '--tables', t, '--symbols', b])
    assert rc == 0, out
    assert 'VPX_srv_tst_other_branch' not in (tmp_path / 'gen-a' / emit_header.GEN_NAME).read_text()
    assert 'VPX_srv_tst_other_branch' in (tmp_path / 'gen-b' / emit_header.GEN_NAME).read_text()


def test_only_another_catalogues_table_is_refused_with_the_commands(tmp_path):
    """Negative: the folder holds only catalogue B's tables; A's emitter refuses (exit 2) and names the two exact
    gen_table.py commands that make A's tables."""
    a, b = two_catalogues(tmp_path)
    t = tmp_path / 'only-b'
    hb = table.catalogue_hash(b)
    for build in ('4390913', NEW):
        doc = table.load(build, tables_dir(), chash=table.catalogue_hash())
        doc = dict(doc, catalogue_sha256=hb)
        (t / build).mkdir(parents=True)
        for name in ('table.json', f'table-{hb[:16]}.json'):
            (t / build / name).write_text(json.dumps(doc), encoding='utf-8')
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen', '--tables', t, '--symbols', a])
    assert rc == 2, out
    assert f'catalogue (hash {table.catalogue_hash(a)[:16]})' in out
    for cmd in table.gen_commands():
        assert cmd in out, (cmd, out)
    assert table.gen_commands() == [
        'python -I tools/exebuild/gen_table.py --identity --old '
        'F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe',
        'python -I tools/exebuild/gen_table.py --old F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/'
        'CivilizationV_DX11.exe --new F:/c5-64bit-work/golden/civ5-steam-build26577625-ws/CivilizationV_DX11.exe '
        '--steam-build 26577625']
    assert not (tmp_path / 'gen' / emit_header.GEN_NAME).exists()


def test_emit_with_a_stale_table_fails(tmp_path):
    cat = tmp_path / 'cat'
    shutil.copytree(table.SYMBOLS_DIR, cat)
    with open(cat / 'server.tsv', 'a', encoding='utf-8') as f:
        f.write('srv_tst_new\tcode\t0x401000\tserver/x.cpp\ttest row\n')
    for b in ('4390913', NEW):   # this checkout's tables as table.json: the only difference is the new row
        (tmp_path / 't' / b).mkdir(parents=True)
        (tmp_path / 't' / b / 'table.json').write_text(
            json.dumps(table.load(b, tables_dir(), chash=table.catalogue_hash())), encoding='utf-8')
    rc, out = run_py('emit_header.py', ['--out', tmp_path / 'gen', '--symbols', cat, '--tables', tmp_path / 't'])
    assert rc == 2 and 'stale' in out and 'srv_tst_new' in out


def test_emit_writes_only_when_changed(tmp_path):
    p = emit_header.emit(tmp_path / 'gen')
    m = p.stat().st_mtime_ns
    os.utime(p, ns=(m - 10_000_000_000, m - 10_000_000_000))
    emit_header.emit(tmp_path / 'gen')
    assert p.stat().st_mtime_ns == m - 10_000_000_000


# ----------------------------------------------------------------------------------------- nothing reaches git (D202)
def git(*args):
    return subprocess.run(['git', '-C', str(REPO)] + list(args), capture_output=True, text=True, errors='replace')


def test_no_generated_table_is_tracked():
    assert table.tracked_forbidden() == []


@pytest.mark.parametrize('path', ['tools/exebuild/tests/x/table.json', 'x/table-28b2a4bc1c630e9a.json',
                                  'exebuild/hand-review.tsv', 'review.tsv',
                                  'proxy/gen/vp_exe_tables.gen.h', 'x/exe-builds/26577625/a.csv',
                                  'research/02-exe-internals/evidence/0582/map-product-vas-4390913-to-26577625.csv'])
def test_table_names_are_ignored(path):
    assert git('check-ignore', '-q', '--no-index', path).returncode == 0, f'{path} is not ignored'


def test_a_table_json_cannot_be_staged():
    d = HERE / '_tmp_stage_probe'
    d.mkdir(exist_ok=True)
    f = d / 'table.json'
    f.write_text('{"format": 1}\n', encoding='utf-8')
    try:
        cp = git('add', '--dry-run', str(f.relative_to(REPO)).replace('\\', '/'))
        assert cp.returncode != 0 and 'ignored' in (cp.stdout + cp.stderr)
        assert table.tracked_forbidden() == []
    finally:
        shutil.rmtree(d, ignore_errors=True)


def test_my_files_hold_no_new_build_address():
    """The committed files of this ticket hold no VA of the new build's table (anchors only)."""
    t = table.load(NEW, tables_dir(), chash=table.catalogue_hash())
    t0 = table.load('4390913', tables_dir(), chash=table.catalogue_hash())
    new_vals = {int(e['va'], 16) for e in t['symbols'].values()}
    new_vals |= {int(c, 16) for a in t.get('arrays', {}).values() for row in a['rows'] for c in row if isinstance(c, str)}
    new_vals -= {s.anchor for s in table.symbols() if s.anchor is not None}
    new_vals -= {int(c, 16) for a in t0.get('arrays', {}).values() for row in a['rows'] for c in row
                 if isinstance(c, str)}
    files = [p for p in git('ls-files', 'exebuild', 'tools/exebuild', 'research/02-exe-internals/exe-build-table.md',
                            'render/src/shaders', 'render/include/c5r/shaders', 'render/tests/shaders',
                            'server/src/render/srv_liveterrain_exefield.cpp',
                            'server/src/render/srv_liveterrain_exefield.h', 'server/src/remote/srv_exe_crash.cpp')
             .stdout.splitlines()]
    hits = []
    for rel in files:
        text = (REPO / rel).read_text(encoding='utf-8', errors='replace')
        for tok in re.findall(r'(?:0x)?([0-9A-Fa-f]{6,8})\b', text):
            if int(tok, 16) in new_vals:
                hits.append(rel)
                break
    assert not hits, f'files holding a new-build value: {hits}'


# ------------------------------------------------------------------------------- the headers compile and run (C++)
FAKE_ROWS = [['tst_fn', 'code', '0x401000', 'test', ''], ['tst_data', 'data', '0x402000', 'test', ''],
             ['tst_missing', 'code', '0x401100', 'test', ''], ['tst_vtbl', 'vtable', '0x403000', 'test', '']]


def fake_setup(tmp_path):
    """A fake catalogue (tst_*, one array) and fake tables of both builds; the new build marked incomplete."""
    inc = tmp_path / 'sites.inc'
    inc.write_text('// fake\n{0x401000u, 7u, 0x402000u},\n{0x401100u, 9u, 0x402000u},\n', encoding='utf-8')
    rows = FAKE_ROWS + [['tst_sites', 'array', '-', 'test', f'file={inc.as_posix()};cols=code,keep,data']]
    cat = write_cat(tmp_path / 'cat', rows)
    chash = table.catalogue_hash(cat)
    newv = {'tst_fn': 0x401800, 'tst_data': 0x402400, 'tst_vtbl': 0x403100}
    for b in builds.KNOWN:
        ident = b.id == builds.ANCHOR.id
        syms = {}
        for name, kind, av, _, _ in FAKE_ROWS:
            v = int(av, 16) if ident else newv.get(name)
            if v:
                syms[name] = {'va': '0x%X' % v, 'kind': kind, 'method': 'test', 'votes': '', 'confidence': 'high'}
        arr_rows = [['0x401000', 7, '0x402000'], ['0x401100', 9, '0x402000']] if ident else \
            [['0x401800', 7, '0x402400'], [0, 9, '0x402400']]
        doc = {'format': 1, 'steam_build': b.steam_build, 'stamp': '0x%08X' % b.stamp,
               'size_of_image': '0x%08X' % b.size_of_image, 'catalogue_sha256': chash, 'symbols': syms,
               'unresolved': [] if ident else ['tst_missing'], 'complete': ident,
               'arrays': {'tst_sites': {'file': inc.as_posix(), 'cols': ['code', 'keep', 'data'], 'rows': arr_rows,
                                        'unresolved_cells': 0 if ident else 1}}}
        (tmp_path / 'tables' / b.steam_build).mkdir(parents=True, exist_ok=True)
        (tmp_path / 'tables' / b.steam_build / 'table.json').write_text(json.dumps(doc), encoding='utf-8')
    gen = emit_header.emit(tmp_path / 'gen', tmp_path / 'tables', cat).parent
    return gen


def compile_run(arch, src, gen, out_dir, extra=()):
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / f'{Path(src).stem}_{arch}.exe'
    obj = out_dir / f'{Path(src).stem}_{arch}.obj'
    inc = ['/I' + str(INCLUDE), '/I' + str(gen)]
    if arch == 'x86':   # the proxy's toolchain: clang-cl -m32 in the VS 2008 environment, here held to C++03
        need(Path(VS2008), 'VS 2008 environment')
        argv = [CLANG, '-m32', '/clang:-std=c++03', '/MT', '/EHsc', '/W4', '/WX', '-Wno-unused-parameter',
                '-fuse-ld=lld'] + inc + list(extra) + [str(src), '/Fe' + str(exe), '/Fo' + str(obj)]
        bat = VS2008
    else:               # an x64 MSVC cl (the server's and render/'s compiler family)
        need(Path(VCVARS64), 'VS 2022 vcvars64')
        argv = ['cl', '/nologo', '/MT', '/EHsc', '/W4', '/WX'] + inc + list(extra) + \
            [str(src), '/Fe' + str(exe), '/Fo' + str(obj)]
        bat = VCVARS64
    cmd = 'cmd /d /s /c ""%s" >NUL && %s"' % (bat, ' '.join('"%s"' % a for a in argv))
    # Ticket 4966 (lead's report of one failure at 23:16 under load, text not kept): the output folder is pytest's own
    # tmp_path (never shared between runs); the compile gets 600 s and one retry when the link could not write or open
    # its output (a scanner holding the new file: LNK1104 / "cannot open"), and every failure message names its cause.
    why = ''
    for attempt in (1, 2):
        try:
            cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', cwd=str(out_dir), timeout=600,
                                env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
        except subprocess.TimeoutExpired:
            pytest.fail(f'{arch} compile of {Path(src).name} timed out after 600 s (machine under load?)')
        text = cp.stdout + cp.stderr
        if cp.returncode == 0 and exe.is_file():
            break
        why = f'{arch} compile of {Path(src).name} failed (rc {cp.returncode}, attempt {attempt}):\n{text[-4000:]}'
        if attempt == 2 or not re.search(r'LNK1104|LNK1168|cannot open|Permission denied|access is denied', text, re.I):
            pytest.fail(why)
    try:
        run = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace', timeout=120)
    except subprocess.TimeoutExpired:
        pytest.fail(f'{exe.name} did not end within 120 s')
    return run.returncode, run.stdout + run.stderr


@pytest.mark.parametrize('arch', ['x86', 'x64'])
def test_runtime_headers_on_a_fake_image(tmp_path, arch):
    gen = fake_setup(tmp_path)
    rc, out = compile_run(arch, HERE / 'native' / 'exe_tables_test.cpp', gen, tmp_path / 'bin')
    assert rc == 0 and re.search(r'exe_tables_test: (\d+)/\1 checks passed', out), out


@pytest.mark.parametrize('arch', ['x86', 'x64'])
def test_real_generated_header_compiles(tmp_path, arch):
    gen = emit_header.emit(tmp_path / 'gen').parent
    src = tmp_path / 'use_real.cpp'
    src.write_text('#include "vp_exe_patterns.h"\n#include <stdio.h>\n'
                   'int main() { int n = 0; for (int b = 0; b < kVpExeBuildCount; ++b) n += VpExeBuildSupported(b);\n'
                   '  printf("supported %d of %d, symbols %d\\n", n, (int)kVpExeBuildCount, (int)kVpExeSymCount);\n'
                   '  return (n == kVpExeBuildCount && VpExeVa(0, VPX_cam_app) == 0xCC8E74u && '
                   'VpExeVa(1, VPX_cam_app) != 0) ? 0 : 1; }\n', encoding='utf-8')
    rc, out = compile_run(arch, src, gen, tmp_path / 'bin')
    assert rc == 0 and f'supported {len(builds.KNOWN)} of {len(builds.KNOWN)}' in out, out
