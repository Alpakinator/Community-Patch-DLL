"""Ticket 0574 (MC #7044/#7046): proxy/build_proxy.py step 7 when the pin drops audited purity rows. The full purity test
needs a source where every audited row is kept; a pin whose derivation drops rows is tested on the audited source when,
and only when, every dropped row is in the allow-list (C5_PURITY_ALLOWED_DROPS) and the audited source is given
(C5_PURITY_AUDITED_SRC); each dropped row is one LOUD line; any other case fails the build.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest proxy/tests/test_purity_drops.py -q

Hermetic: derive_purity.py is replaced by a stand-in that writes the report.tsv the case needs.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
PROXY = HERE.parent


def _load():
    spec = importlib.util.spec_from_file_location('build_proxy_0574', PROXY / 'build_proxy.py')
    mod = importlib.util.module_from_spec(spec)
    sys.modules['build_proxy_0574'] = mod
    spec.loader.exec_module(mod)
    return mod


bp = _load()
ROWS = [('method', 'ICvUnit1::GetFacingDirection(bool) const'), ('lua', 'Player.GetCapitalCity'),
        ('lua', 'Plot.GetPlotCity'), ('method', 'ICvPlayer1::GetUnit(int)')]
# The commit each of those rows' own citations are read at: ticket 0628 re-cited all four at the 0.1b pin 1b87dd185;
# ticket 0765 (2026-11-25) re-cited three of them again at the Vox Populi 5.5 pin a2627c8ac, whose TContainer::Get and
# Lua push changed (research/03-dll-engine-boundary/vp55-patch-side.md section 7).
ROW_CITED_AT = {('method', 'ICvUnit1::GetFacingDirection(bool) const'): '1b87dd185',
                ('lua', 'Player.GetCapitalCity'): 'a2627c8ac', ('lua', 'Plot.GetPlotCity'): 'a2627c8ac',
                ('method', 'ICvPlayer1::GetUnit(int)'): 'a2627c8ac'}


def fake_derive(dropped, rc=0):
    """A stand-in for derive_purity.py: writes <out>/report.tsv with the given rows dropped and two kept."""
    def run(cmd):
        out = Path(cmd[cmd.index('--out') + 1])
        out.mkdir(parents=True, exist_ok=True)
        if rc == 0:
            lines = ['kind\tkey\tdecision\treason', 'method\tICvColorInfo1::GetColor()\tkept\t-',
                     'lua\tGame.GetGameTurn\tkept\t-']
            lines += ['%s\t%s\tdropped\tcallee changed' % r for r in dropped]
            (out / 'report.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return rc, 'derive stand-in'
    return run


@pytest.fixture
def logs(monkeypatch):
    got = []
    monkeypatch.setattr(bp, 'log', lambda s='': got.append(s))
    return got


def allow(tmp_path, rows, extra=''):
    f = tmp_path / 'allowed.tsv'
    f.write_text('# allowed\n\n' + ''.join('%s\t%s\n' % r for r in rows) + extra, encoding='utf-8')
    return str(f)


def test_no_drop_tests_the_pin(tmp_path, logs):
    src = tmp_path / 'pin'
    got = bp.purity_test_source(src, tmp_path / 'x.dll', tmp_path / 's', env={}, run=fake_derive([]))
    assert got == (src, '', [])
    assert not any('LOUD' in x for x in logs)


def test_allowed_drops_test_the_audited_source(tmp_path, logs):
    audited = tmp_path / 'audited'
    audited.mkdir()
    env = {bp.ALLOWED_DROPS_ENV: allow(tmp_path, ROWS), bp.AUDITED_SRC_ENV: str(audited)}
    src, ident, dropped = bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env=env,
                                                run=fake_derive(ROWS))
    assert src == audited and ident.startswith(' audited ')
    assert [(k, n) for k, n, _ in dropped] == ROWS
    loud = [x for x in logs if 'LOUD' in x]
    assert len(loud) == 4 and all('in the allow-list' in x for x in loud)


def test_a_row_not_allowed_fails(tmp_path, logs):
    audited = tmp_path / 'audited'
    audited.mkdir()
    env = {bp.ALLOWED_DROPS_ENV: allow(tmp_path, ROWS[:3]), bp.AUDITED_SRC_ENV: str(audited)}
    with pytest.raises(bp.BuildError) as e:
        bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env=env, run=fake_derive(ROWS))
    assert 'not in the allow-list' in str(e.value) and 'ICvPlayer1::GetUnit(int)' in str(e.value)
    assert any('NOT in the allow-list' in x for x in logs)


def test_no_allow_list_fails(tmp_path, logs):
    with pytest.raises(bp.BuildError) as e:
        bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env={}, run=fake_derive(ROWS[:1]))
    assert bp.ALLOWED_DROPS_ENV in str(e.value)


def test_allowed_but_no_audited_source_fails(tmp_path, logs):
    for audited in ('', str(tmp_path / 'missing')):
        env = {bp.ALLOWED_DROPS_ENV: allow(tmp_path, ROWS), bp.AUDITED_SRC_ENV: audited}
        with pytest.raises(bp.BuildError) as e:
            bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env=env, run=fake_derive(ROWS))
        assert bp.AUDITED_SRC_ENV in str(e.value)


def test_derive_failure_fails(tmp_path, logs):
    with pytest.raises(bp.BuildError) as e:
        bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env={}, run=fake_derive([], rc=2))
    assert 'derive_purity.py at the pin failed (rc 2)' in str(e.value)


def test_a_bad_allow_list_row_fails(tmp_path, logs):
    env = {bp.ALLOWED_DROPS_ENV: allow(tmp_path, ROWS, extra='function\tsomething\n')}
    with pytest.raises(bp.BuildError) as e:
        bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env=env, run=fake_derive(ROWS))
    assert 'method|lua' in str(e.value)


# Ticket 0616 (MC #8594): a plain dev build at the pin needs no environment variable. The audited lists are re-baselined
# at the pin (the 4 rows above, which the derivation drops there, are `not-audited`), so the derivation keeps every
# audited row and step 7 tests --src itself. These checks catch a pin move without a re-baseline before a team's build.
REPO = PROXY.parent
sys.path.insert(0, str(PROXY / 'tools'))
import derive_purity  # noqa: E402
import gen_pure_list  # noqa: E402


def _lock_commit():
    for line in (REPO / 'deps' / 'dll.lock').read_text(encoding='utf-8').splitlines():
        k, sep, v = line.partition('=')
        if sep and not line[:1].isspace() and k.strip() == 'commit':
            return v.strip()
    raise AssertionError('deps/dll.lock names no commit')


def test_baseline_is_audited_at_the_lock_commit():
    base = derive_purity.load_baseline(derive_purity.BASELINE)
    assert base['audited_at'] == _lock_commit(), (
        'deps/dll.lock moved without a purity re-baseline (research release-builds.md 6.4): plain builds at the pin '
        'would fail build_proxy step 7')
    assert derive_purity.AUDITED_AT == base['audited_at']


def test_the_lists_hold_and_the_reaudited_rows_are_kept():
    """Ticket 0628 (2026-11-24): the 4 rows given up at 1b87dd185 by ticket 0616 were re-audited by hand at the pin and
    are kept again, each with its own `[cited at <commit>]` citations (research proxy-design.md 17.5b); ticket 0765
    re-cited three of them at a2627c8ac (ROW_CITED_AT)."""
    assert gen_pure_list.problems() == []
    entries = {(e['kind'], e['key']): e for e in derive_purity.load_baseline(derive_purity.BASELINE)['entries']}
    assert set(ROWS) <= set(entries)
    for k in ROWS:
        assert entries[k].get('cited_at') == ROW_CITED_AT[k] and not entries[k]['unresolved'], k


# Ticket 0628: a row's own citation commit, and file names with digits (VP64HiddenEmbark.inl).
def test_row_cited_at_marker():
    assert derive_purity.row_cited_at('CvPlayer.cpp:10-12') is None
    assert derive_purity.row_cited_at('[cited at 1b87dd185] CvPlayer.cpp:10-12') == '1b87dd185'
    assert derive_purity.row_cited_at('x [cited at %s]' % ('a' * 40)) == 'a' * 40


@pytest.mark.parametrize('ev', ['[cited at ] a.cpp:1-2', '[cited at 1b87dd18z] a.cpp:1-2', '[cited at 1b87dd] a.cpp:1-2',
                                '[cited at 1B87DD185] a.cpp:1-2', '[cited at %s] a.cpp:1-2' % ('a' * 41),
                                '[cited at 1b87dd185] [cited at 76992d235] a.cpp:1-2'])
def test_row_cited_at_refuses_a_bad_marker(ev):
    with pytest.raises(derive_purity.Refused):
        derive_purity.row_cited_at(ev)


def test_citation_file_names_may_hold_digits():
    got = derive_purity.citations('[cited at 1b87dd185] VP64HiddenEmbark.inl:404-418 -> CvUnit.h:644-647 1314-1317 ; '
                                  'x64-dll VP64HiddenEmbark.inl:70')
    assert got == [('VP64HiddenEmbark.inl', 404, 418), ('CvUnit.h', 644, 647), ('CvUnit.h', 1314, 1317),
                   ('VP64HiddenEmbark.inl', 70, 70)]


def test_a_plain_build_needs_no_environment(tmp_path, logs):
    """No dropped row: no variable is read and the full test runs on --src itself."""
    got = bp.purity_test_source(tmp_path / 'pin', tmp_path / 'x.dll', tmp_path / 's', env={}, run=fake_derive([]))
    assert got == (tmp_path / 'pin', '', [])
    assert not any('LOUD' in x for x in logs)
