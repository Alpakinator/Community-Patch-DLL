"""Issue 0358: the whole-program link's code depends on the linker process's environment (one extra variable changed
CvPlayer::doInstantGreatPersonProgress), so build.py links with a fixed minimal environment. Offline: no compiler, no
linker (a stand-in for subprocess.Popen records what the link would get).

    python -m pytest tools/x64-dll/tests/test_link_env.py -q -p no:cacheprovider
"""
from __future__ import annotations

import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
X64 = HERE.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


x64build = _load("x64build_0358", X64 / "build.py")

VCVARS = {
    'LIB': r'C:\VS\lib\x64;C:\SDK\um\x64', 'SYSTEMROOT': r'C:\WINDOWS', 'TEMP': r'C:\Users\u\AppData\Local\Temp',
    'TMP': r'C:\Users\u\AppData\Local\Temp', 'PATH': r'C:\LLVM\bin;C:\VS\bin;C:\other', 'INCLUDE': r'C:\VS\include',
}


def test_only_the_fixed_keys_reach_the_linker():
    e = x64build.link_env(VCVARS)
    assert sorted(e) == ['LIB', 'PATH', 'SYSTEMROOT', 'TEMP', 'TMP']
    assert e['LIB'] == VCVARS['LIB'] and e['TEMP'] == VCVARS['TEMP']
    assert e['PATH'] == str(x64build.LLVM_BIN) + ';' + r'C:\WINDOWS\system32'


def test_caller_variables_do_not_change_it():
    """The variables that differ between sessions and runs (the 0358 finding: one extra variable flipped the code)."""
    noisy = dict(VCVARS, CLAUDE_CODE_SESSION_ID='abc', PWD='C:/some/worktree/of/any/length', C5_QUIETLOCK_JOB='1-2.json',
                 C5_0358_PAD='0123456789' * 5, PATH=r'C:\a\much\longer\path;' + VCVARS['PATH'])
    assert x64build.link_env(noisy) == x64build.link_env(VCVARS)
    assert x64build.link_env_record(x64build.link_env(noisy)) == x64build.link_env_record(x64build.link_env(VCVARS))


def test_link_options_from_the_environment_are_never_passed():
    e = x64build.link_env(dict(VCVARS, LINK='/OPT:NOREF', _LINK_='/DEBUG:NONE'))
    assert 'LINK' not in e and '_LINK_' not in e


def test_keys_found_without_regard_to_case_and_windir_fallback():
    v = {'Lib': r'C:\L', 'SystemRoot': r'D:\WIN', 'Temp': r'C:\T', 'Tmp': r'C:\T'}
    e = x64build.link_env(v)
    assert e['LIB'] == r'C:\L' and e['SYSTEMROOT'] == r'D:\WIN' and e['PATH'].endswith(r'D:\WIN\system32')
    e = x64build.link_env({'LIB': 'x', 'windir': r'E:\W'})
    assert e['SYSTEMROOT'] == r'E:\W' and e['TEMP'] == ''


def test_record_names_size_and_hash():
    r = x64build.link_env_record(x64build.link_env(VCVARS))
    assert r['names'] == ['LIB', 'PATH', 'SYSTEMROOT', 'TEMP', 'TMP'] and len(r['sha256']) == 64 and r['chars'] > 20
    r2 = x64build.link_env_record(x64build.link_env(dict(VCVARS, TEMP=r'C:\other')))
    assert r2['sha256'] != r['sha256']


def test_link_runs_lld_with_the_minimal_environment(tmp_path, monkeypatch):
    """link() hands lld-link link_env(env), never the build's environment (negative: a caller variable is absent)."""
    seen = {}

    class FakePopen:
        def __init__(self, cmd, **kw):
            seen['cmd'], seen['env'], seen['cwd'] = cmd, kw.get('env'), kw.get('cwd')
            self.pid, self.returncode = 4242, 0

        def communicate(self):
            return 'ok\n', None

    monkeypatch.setattr(x64build.subprocess, 'Popen', FakePopen)
    monkeypatch.setattr(x64build, 'process_peaks', lambda proc: {})
    src = tmp_path / 'src'
    (src / Path(x64build.DEF_FILE)).parent.mkdir(parents=True)
    (src / x64build.DEF_FILE).write_text('LIBRARY x\n')
    out = tmp_path / 'out'
    out.mkdir()
    env = dict(VCVARS, CLAUDE_CODE_SESSION_ID='abc', SOURCE_DATE_EPOCH='1')
    rc, text, dt, peaks = x64build.link(env, out, src, 'release', False, None, [out / 'obj' / 'a.obj'], True,
                                        out / 'CvGameCore_Expansion2.dll')
    assert rc == 0 and seen['cwd'] == str(out)
    assert seen['cmd'] == [str(x64build.LLD_LINK), '@link.rsp'], seen['cmd']   # no folder on the command line
    assert (out / 'link.rsp').is_file()
    assert seen['env'] == x64build.link_env(env)
    assert 'CLAUDE_CODE_SESSION_ID' not in seen['env'] and 'SOURCE_DATE_EPOCH' not in seen['env']


# Issue 0373: the setting that makes LTO's dead store elimination decide the same way every run is on the release link
# line (and only there). Negative: build.py without it fails the first test.
LTO_SETTING = '/mllvm:-dse-memoryssa-scanlimit=1000'


def _lines(tmp_path, config):
    out = tmp_path / config
    out.mkdir()
    return x64build.link_lines(out, config, False, None, [out / 'obj' / 'a.obj'], True, out / 'x.dll', out / 'x.def')


def test_release_link_line_carries_the_dse_setting(tmp_path):
    assert x64build.LTO_DETERMINISM_LINK == [LTO_SETTING]
    lines = _lines(tmp_path, 'release')
    assert LTO_SETTING in lines, lines
    assert lines.index(LTO_SETTING) < lines.index(f'"{x64build.link_path(tmp_path / "release" / "obj" / "a.obj", tmp_path / "release")}"')


def test_dse_setting_only_on_whole_program_links(tmp_path):
    for config in ('debug', 'fast'):
        assert not [l for l in _lines(tmp_path, config) if l.startswith('/mllvm:')], config


def test_link_writes_the_setting_into_link_rsp(tmp_path, monkeypatch):
    """The response file lld-link reads (not only link_lines) has the setting for release."""
    class FakePopen:
        def __init__(self, cmd, **kw):
            self.pid, self.returncode = 4243, 0

        def communicate(self):
            return 'ok\n', None

    monkeypatch.setattr(x64build.subprocess, 'Popen', FakePopen)
    monkeypatch.setattr(x64build, 'process_peaks', lambda proc: {})
    src = tmp_path / 'src'
    (src / Path(x64build.DEF_FILE)).parent.mkdir(parents=True)
    (src / x64build.DEF_FILE).write_text('LIBRARY x\n')
    out = tmp_path / 'out'
    out.mkdir()
    x64build.link(dict(VCVARS), out, src, 'release', False, None, [out / 'obj' / 'a.obj'], True,
                  out / 'CvGameCore_Expansion2.dll')
    assert LTO_SETTING in (out / 'link.rsp').read_text(encoding='utf-8').splitlines()
