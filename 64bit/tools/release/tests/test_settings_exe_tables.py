"""The exe_tables setting of tools/release/settings.py (ticket 4960, epic 0590, D202): the folder of the local per-build
EXE address tables. PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_settings_exe_tables.py
"""
import importlib.util
from pathlib import Path

import pytest

SETTINGS = Path(__file__).resolve().parents[1] / 'settings.py'


def load():
    spec = importlib.util.spec_from_file_location('settings_under_test', SETTINGS)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_key_and_variable():
    st = load()
    assert st.ENV['exe_tables'] == 'C5_EXE_TABLES'


def test_from_file_and_environment(tmp_path):
    st = load()
    f = tmp_path / 's.ini'
    f.write_text('exe_tables = C:/somewhere/exe-builds\n', encoding='utf-8')
    v, origin = st.lookup('exe_tables', {'C5_SETTINGS_FILE': str(f)})
    assert v == Path('C:/somewhere/exe-builds') and '`exe_tables`' in origin
    v, origin = st.lookup('exe_tables', {'C5_SETTINGS_FILE': str(f), 'C5_EXE_TABLES': 'D:/other'})
    assert v == Path('D:/other') and origin == 'environment variable C5_EXE_TABLES'


def test_no_default_and_require_names_it(tmp_path):
    st = load()
    env = {'C5_SETTINGS_FILE': str(tmp_path / 'none.ini')}
    assert st.get('exe_tables', env) is None
    with pytest.raises(st.SettingError, match='C5_EXE_TABLES') as e:
        st.require('exe_tables', 'the EXE address tables', env)
    assert 'exe_tables = <value>' in str(e.value)


def test_repository_settings_file_names_it():
    st = load()
    assert 'exe_tables' in st.read_file(st.DEFAULT_FILE)


def test_core_db_has_no_default(tmp_path):
    # ticket 0612: the game's live cache is no longer the default core database; the testdata stage refuses without one
    st = load()
    assert st.lookup('core_db', {'C5_SETTINGS_FILE': str(tmp_path / 'none.ini')}) == (None, 'not set')
