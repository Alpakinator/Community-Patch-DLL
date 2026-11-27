"""Ticket 4701 (plan 4.8): the DLL source location as one setting (tools/x64-dll/dll_source.py).

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest tools/x64-dll/tests/test_dll_source.py -q -p no:cacheprovider

Offline, no build: the order VP_DLL_SRC > deps/dll-source.ini > the upstream layout `..`; the upstream layout on a
crafted DLL repository with the tools under 64bit/; build.py's DEFAULT_SRC and resolve_src messages follow the
setting; no script of tools/x64-dll names the fork clone by path any more.
"""
from __future__ import annotations

import importlib.util
import re
import shutil
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent
REPO = X64.parents[1]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.fixture
def ds():
    return _load('dll_source_t', X64 / 'dll_source.py')


def test_order_env_then_file_then_upstream(ds, tmp_path):
    ini = tmp_path / 'dll-source.ini'
    ini.write_text('# comment\n\nsrc = D:/somewhere/fork\n', encoding='utf-8')
    assert ds.setting({'VP_DLL_SRC': 'E:/env/fork'}, ini) == ('E:/env/fork', 'environment variable VP_DLL_SRC')
    assert ds.setting({'VP_DLL_SRC': '  '}, ini)[0] == 'D:/somewhere/fork'          # blank env = not given
    assert ds.setting({}, ini) == ('D:/somewhere/fork', str(ini))
    assert ds.setting({}, tmp_path / 'absent.ini') == ('..', 'the upstream layout (no setting given)')
    ini.write_text('other = 1\n', encoding='utf-8')                                 # no src line = not given
    assert ds.setting({}, ini)[0] == '..'


def test_relative_value_from_the_repository_root(ds, tmp_path):
    ini = tmp_path / 'dll-source.ini'
    ini.write_text('src = ../fork-x64\n', encoding='utf-8')
    assert ds.dll_source({}, ini) == (ds.ROOT / '..' / 'fork-x64').resolve()
    assert ds.dll_source({}, tmp_path / 'absent.ini') == ds.ROOT.parent


def test_bad_setting_file_is_an_error(ds, tmp_path):
    ini = tmp_path / 'dll-source.ini'
    ini.write_text('src C:/x\n', encoding='utf-8')
    with pytest.raises(ds.SettingError, match='not a `key = value` line'):
        ds.setting({}, ini)
    ini.write_text('src =\n', encoding='utf-8')
    with pytest.raises(ds.SettingError, match='has no value'):
        ds.setting({}, ini)


def test_upstream_layout(tmp_path):
    """The tools as upstream would hold them: <dll repo>/64bit/tools/x64-dll/, no deps/dll-source.ini. The default is
    the DLL repository itself, where ../CvGameCoreDLL_Expansion2 is seen from 64bit/."""
    dllrepo = tmp_path / 'Community-Patch-DLL'
    (dllrepo / 'CvGameCoreDLL_Expansion2').mkdir(parents=True)
    tools = dllrepo / '64bit' / 'tools' / 'x64-dll'
    tools.mkdir(parents=True)
    shutil.copy2(X64 / 'dll_source.py', tools / 'dll_source.py')
    up = _load('dll_source_up', tools / 'dll_source.py')
    assert up.ROOT == (dllrepo / '64bit').resolve()
    assert not up.SETTING_FILE.exists()
    assert up.dll_source({}) == dllrepo.resolve()
    assert (up.dll_source({}) / 'CvGameCoreDLL_Expansion2').is_dir()
    assert up.origin({}) == 'the upstream layout (no setting given)'


def test_this_repository_has_its_value(ds):
    """Here the setting file gives the long-lived fork clone, the path build.py hard-coded before ticket 4701."""
    value, where = ds.setting({}, ds.SETTING_FILE)
    assert where == 'deps/dll-source.ini'
    assert Path(value).is_absolute()


def test_build_py_reads_the_setting(monkeypatch, tmp_path):
    monkeypatch.setenv('VP_DLL_SRC', str(tmp_path / 'not-a-tree'))
    b = _load('x64build_t', X64 / 'build.py')
    assert b.DEFAULT_SRC == (tmp_path / 'not-a-tree').resolve()
    assert b.DEFAULT_SRC_ORIGIN == 'environment variable VP_DLL_SRC'
    with pytest.raises(b.SrcError, match=r'\(the default, from environment variable VP_DLL_SRC\): not a DLL fork'):
        b.resolve_src(None)
    with pytest.raises(b.SrcError) as e:                     # an explicit --src is never called "the default"
        b.resolve_src(tmp_path)
    assert 'the default' not in str(e.value)


def test_build_py_without_env_uses_the_file(monkeypatch):
    monkeypatch.delenv('VP_DLL_SRC', raising=False)
    b = _load('x64build_t2', X64 / 'build.py')
    ds = _load('dll_source_t2', X64 / 'dll_source.py')
    assert b.DEFAULT_SRC == ds.dll_source({})
    assert b.DEFAULT_SRC_ORIGIN == ds.origin({})


FORK_PATH = re.compile(r'GitHub[\\/]+Community-Patch-DLL', re.I)


def test_no_tool_names_the_fork_clone():
    """The scripts of tools/x64-dll (not evidence/, not tests/) take the DLL source from the setting only."""
    hits = []
    for p in sorted(X64.rglob('*')):
        rel = p.relative_to(X64).as_posix()
        if not p.is_file() or rel.startswith(('evidence/', 'tests/')) or p.suffix not in ('.py', '.bat', '.ps1', '.cmd'):
            continue
        for n, line in enumerate(p.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
            if FORK_PATH.search(line):
                hits.append(f'{rel}:{n}: {line.strip()}')
    assert not hits, '\n'.join(hits)
