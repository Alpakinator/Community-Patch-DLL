"""Ticket 0490: one truth for the compositor's configuration.

shell_composite.cpp's built-in defaults (struct Config, ReadConfig) must equal the shipped shell/composite/
shell_composite.ini, so that a compositor installed without its ini (or with a key left out) runs the configuration the
tests and the product path run. The DLL logs every value it uses ("values used i/N: key=value ...") at load and at each
re-read, compares the ini with its built-in defaults, and names a missing, unreadable or sectionless ini once.

The comparison runs the DLL (test_host.exe on the hidden desktop, no server): once with the shipped ini, once without
any ini. It fails when either the ini or the C++ defaults change alone. Nothing of the game is used.
"""
import ctypes
import re
import shutil
import time
from ctypes import wintypes

import pytest

import shellrun

SHIPPED = shellrun.SHELL / "composite" / "shell_composite.ini"
HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after")
VALUES = re.compile(r"\] (config|config file changed): values used (\d+)/(\d+) \((\d+) keys\): (.*)$")
PAIR = re.compile(r'([A-Za-z0-9_]+)=("[^"]*"|\S*)')


def ini_keys(path):
    """[composite] keys of an ini as the Windows profile reader sees them: the first value of a key wins, comments and
    blank lines skipped, values stripped."""
    keys, section = {}, None
    for ln in path.read_text(encoding="utf-8").splitlines():
        s = ln.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("[") and s.endswith("]"):
            section = s[1:-1].strip().lower()
            continue
        if section == "composite" and "=" in s:
            k, v = s.split("=", 1)
            keys.setdefault(k.strip(), v.strip())
    return keys


def values_blocks(run):
    """Every logged block of values used, in order: [(what, {key: value})]."""
    blocks, cur, want = [], {}, None
    for ln in run.log_lines():
        m = VALUES.search(ln)
        if not m:
            continue
        what, i, total = m.group(1), int(m.group(2)), int(m.group(3))
        if i == 1:
            cur, want = {}, total
        for k, v in PAIR.findall(m.group(5)):
            cur[k] = v[1:-1] if v.startswith('"') else v
        if i == want:
            assert len(cur) == int(m.group(4)), (len(cur), m.group(4), ln)
            blocks.append((what, cur))
    return blocks


def start(run, seconds=4):
    run.start_host(*HOST, seconds=seconds)
    run.wait_log(r"\bconfig: values used \d+/\d+ .*\bmodel_fow=", timeout_s=15)


def finish(run, seconds=30):
    code = run.host.wait(seconds)
    assert code == 0, (code, shellrun.tail(run.host_log, 10), shellrun.tail(run.composite_log, 10))


def first_values(run):
    blocks = values_blocks(run)
    assert blocks and blocks[0][0] == "config", blocks[:1]
    return blocks[0][1]


@pytest.fixture
def plain(tmp_path, hidden_desktop, built, request):
    """A run with the shipped ini as it is (not the `run` fixture: that one sets the version-3 test keys)."""
    r = shellrun.Run(tmp_path / "run", hidden_desktop, request.node.name[:24].replace("[", "_").replace("]", ""))
    shutil.copy2(SHIPPED, r.ini)
    yield r
    r.stop()


def test_ini_is_a_profile_file_with_one_value_per_key():
    """The comparison below reads the shipped ini as Windows does; a key given twice would make that ambiguous."""
    text = SHIPPED.read_text(encoding="utf-8")
    seen = {}
    for m in re.finditer(r"(?m)^\s*([A-Za-z0-9_]+)\s*=", text):
        seen[m.group(1)] = seen.get(m.group(1), 0) + 1
    assert seen and all(n == 1 for n in seen.values()), {k: n for k, n in seen.items() if n != 1}
    assert re.search(r"(?m)^\[composite\]\s*$", text)


def test_shipped_ini_equals_the_built_in_defaults(plain):
    """With the shipped ini the DLL says its values equal the built-in defaults, logs no CONFIG ERROR, and every key of
    the ini is among the values it logs, with the ini's value (as parsed)."""
    start(plain)
    finish(plain)
    text = "\n".join(plain.log_lines())
    assert re.search(r"\bconfig: the ini's values equal the built-in defaults \(\d+ keys compared", text), \
        shellrun.tail(plain.composite_log, 20)
    assert "CONFIG ERROR" not in text, [ln for ln in plain.log_lines() if "CONFIG ERROR" in ln]
    used = first_values(plain)
    ini = ini_keys(SHIPPED)
    missing = sorted(set(ini) - set(used))
    assert not missing, "keys of the shipped ini the DLL does not log: %s" % missing
    # MC (0490): every key the compositor's ReadConfig reads is in the shipped ini; tag_anchor is left out on purpose
    # (absent = the tag rule's own default anchor, which no written value expresses for every rule)
    unwritten = sorted(set(used) - set(ini) - {"tag_anchor"})
    assert not unwritten, "keys the DLL reads that the shipped ini does not carry: %s" % unwritten
    # the values as the ini spells them (tag_match empty = the rule's default; section from VPSHELL_SECTION here)
    for k, v in ini.items():
        if k in ("tag_match", "tag_anchor", "section", "shot_dir"):
            continue
        assert used[k] == v, (k, used[k], v)


def test_without_ini_the_values_are_the_shipped_ini_values(plain, tmp_path, hidden_desktop, built):
    """The DLL without any ini uses exactly the values it uses with the shipped ini (the same environment), and says once
    that the ini is missing. Fails when the ini or a built-in default changes alone."""
    start(plain)
    finish(plain)
    with_ini = first_values(plain)

    bare = shellrun.Run(tmp_path / "bare", hidden_desktop, "bare_" + str(int(time.time()) % 100000))
    try:
        bare.ini.unlink()
        bare.section = plain.section          # the same VPSHELL_SECTION as the first run (it is in the values)
        bare.start_host(*HOST, seconds=4)
        bare.wait_log(r"\bconfig: values used \d+/\d+ .*\bmodel_fow=", timeout_s=15)
        finish(bare)
        lines = bare.log_lines()
        missing = [ln for ln in lines if "CONFIG ERROR" in ln and "shell_composite.ini is missing" in ln]
        assert len(missing) == 1, lines[:40]
        assert "takes the built-in default" in missing[0]
        assert not any("CONFIG ERROR" in ln for ln in lines if ln not in missing), [ln for ln in lines if "CONFIG ERROR" in ln]
        without = first_values(bare)
    finally:
        bare.stop()
    # shot_dir comes from VPSHELL_SHOT_DIR, which is each run's own folder
    diff = {k: (with_ini.get(k), without.get(k)) for k in set(with_ini) | set(without)
            if k != "shot_dir" and with_ini.get(k) != without.get(k)}
    assert not diff, "the built-in defaults differ from the shipped ini: %s" % diff


@pytest.mark.parametrize("key,value", [
    ("unit_filter", "auto"),   # issue 4752: the unit-draw filter follows the server's mode word (on/off are TEST ONLY)
    ("hook_units", "1"),       # issue 4752: the kill switch of the filter's hooks
    ("model_filter", "auto"),  # ticket 4874: the model filter follows the mode word's bits 1 and 2 (on/off are TEST ONLY)
    ("hook_models", "1"),      # ticket 4874: the kill switch of the model tap's own hooks
    ("model_tap_passed", "0"),  # ticket 4874: TEST ONLY, the passed draws are not tapped
    ("model_tap_slot_kb", "4096"),  # ticket 4874: one frame slot of the tap
])
def test_unit_filter_keys(plain, key, value):
    """Issue 4752: each key of the unit-draw filter is in the shipped ini with the built-in value, and logged as used."""
    assert ini_keys(SHIPPED).get(key) == value, (key, ini_keys(SHIPPED).get(key))
    start(plain)
    finish(plain)
    assert first_values(plain)[key] == value


def test_an_ini_value_that_differs_is_named(plain):
    """Negative: an ini whose value differs from the default is reported key by key (a changed ini cannot go unseen)."""
    plain.set_ini(tap="0", handover="v3")
    start(plain)
    finish(plain)
    ln = plain.wait_log(r"\bconfig: the ini differs from the built-in defaults in 2 of \d+ keys:", timeout_s=1)
    assert "tap=0 (default 1)" in ln and "handover=v3 (default ring)" in ln, ln
    used = first_values(plain)
    assert used["tap"] == "0" and used["handover"] == "v3", used


def test_an_environment_override_is_not_an_ini_difference(plain):
    """VPSHELL_<KEY> wins over the ini for the values used, but the ini-against-defaults line reads the ini alone."""
    plain.start_host(*HOST, seconds=4, env={"VPSHELL_TAP": "0"})
    plain.wait_log(r"\bconfig: values used \d+/\d+ .*\bmodel_fow=", timeout_s=15)
    finish(plain)
    assert plain.wait_log(r"\bconfig: the ini's values equal the built-in defaults", timeout_s=1)
    assert first_values(plain)["tap"] == "0"


def test_no_composite_section_is_named_once(plain):
    """Negative: an ini without [composite] is a CONFIG ERROR once, the values used are the defaults."""
    plain.ini.write_text("[other]\ntap=0\n", encoding="utf-8")
    start(plain)
    finish(plain)
    lines = plain.log_lines()
    hits = [ln for ln in lines if "has no [composite] section" in ln]
    assert len(hits) == 1 and "CONFIG ERROR" in hits[0], lines[:40]
    assert first_values(plain)["tap"] == "1"


_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.CreateFileW.restype = wintypes.HANDLE
_k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                           wintypes.DWORD, wintypes.HANDLE]
_k.CloseHandle.argtypes = [wintypes.HANDLE]
INVALID = wintypes.HANDLE(-1).value


def test_unreadable_ini_is_named_once_and_readable_again(plain, host_closer):
    """Negative: an ini held open without sharing (as an editor or a scanner can) while the DLL loads is a CONFIG
    ERROR "cannot be read" (error 32) once, with the values used; after the file is released and changed, the DLL
    says it is readable again and compares it with the defaults.
    Ticket 0702 (2026-11-23): the control thread watches the ini only after its dummy-device hook commit, which took
    7.9-9.1 s under CPU load (0.1-2.4 s quiet); the host's fixed 8-s life ended first and the re-read never came. The
    host now lives past the commit (test_lockstep's HOOK_COMMIT_WAIT_S backstop), the test waits for the commit line,
    then for the re-read, and ends the host by WM_CLOSE."""
    from test_lockstep import HOOK_COMMIT_WAIT_S
    h = _k.CreateFileW(str(plain.ini), 0x80000000, 0, None, 3, 0x80, None)   # GENERIC_READ, no sharing, OPEN_EXISTING
    assert h and h != INVALID, ctypes.get_last_error()
    try:
        plain.start_host(*HOST, seconds=8 + 2 * HOOK_COMMIT_WAIT_S)
        ln = plain.wait_log(r"\bconfig: CONFIG ERROR .*shell_composite\.ini cannot be read \(error 32\)", timeout_s=15)
        assert "takes the built-in default" in ln
        plain.wait_log(r"\bconfig: values used \d+/\d+ .*\bmodel_fow=", timeout_s=5)
    finally:
        _k.CloseHandle(h)
    plain.set_ini(tap="1")   # same value, new time stamp: the DLL re-reads (once its ini watch runs, after the commit)
    plain.wait_log(r"\bhooks committed \(dummy device\)", timeout_s=HOOK_COMMIT_WAIT_S)
    plain.wait_log(r"\bconfig file changed: .*shell_composite\.ini is readable again", timeout_s=10)
    plain.wait_log(r"\bconfig file changed: the ini's values equal the built-in defaults", timeout_s=5)
    assert host_closer(plain.host.pid) >= 1
    finish(plain)
    assert sum("cannot be read" in ln for ln in plain.log_lines()) == 1
