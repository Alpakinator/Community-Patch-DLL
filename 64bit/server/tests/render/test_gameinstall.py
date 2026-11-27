"""Ticket 0589: server/tests/render/gameinstall.py, on made-up files only (nothing is read from an install or the Steam
install): the install's order (C5_GAME_DIR, C5R_GAME_DIR, the setting render_game_dir), the placeholder naming the
variables when nothing is set (never the Steam path), and the EXE build check (exe_build.py's rule) with its override
C5_ALLOW_UNKNOWN_EXE_BUILD=1; require_install/require_exe fail loud. No build needed.

    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest server/tests/render/test_gameinstall.py -q -p no:cacheprovider
"""
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gameinstall  # noqa: E402

STAMP = 0x546CD0A8
SIZE = 16982584


def fake_pe(path, stamp, size=None):
    b = bytearray(512)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x80)
    b[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", b, 0x84, 0x14C, 1, stamp)
    with open(path, "wb") as f:
        f.write(bytes(b))
        if size is not None:
            f.truncate(size)
    return path


def env(tmp_path, **kv):
    """An environment with no install variable and an absent settings file, plus kv."""
    e = {"C5_SETTINGS_FILE": str(tmp_path / "absent.ini")}
    e.update(kv)
    return e


def test_nothing_set_is_a_placeholder_never_steam(tmp_path):
    g, origin = gameinstall.game_dir(env(tmp_path))
    assert origin == "nothing set" and "C5_GAME_DIR" in str(g) and "C5R_GAME_DIR" in str(g)
    assert "steamapps" not in str(g).lower() and not (g / gameinstall.EXE_NAME).exists()
    with pytest.raises(AssertionError, match="C5_GAME_DIR or C5R_GAME_DIR"):
        gameinstall.require_install(environ=env(tmp_path))


def test_order(tmp_path):
    s = tmp_path / "settings.ini"
    s.write_text("# c\nrender_game_dir = %s\n" % (tmp_path / "from-settings"), encoding="utf-8")
    e = env(tmp_path, C5_SETTINGS_FILE=str(s))
    assert gameinstall.game_dir(e)[0] == tmp_path / "from-settings"
    e["C5_RENDER_GAME_DIR"] = str(tmp_path / "from-render-env")
    assert gameinstall.game_dir(e)[0] == tmp_path / "from-render-env"
    e["C5R_GAME_DIR"] = str(tmp_path / "c5r")
    assert gameinstall.game_dir(e) == (tmp_path / "c5r", "C5R_GAME_DIR")
    e["C5_GAME_DIR"] = str(tmp_path / "c5")
    assert gameinstall.game_dir(e) == (tmp_path / "c5", "C5_GAME_DIR")


def test_exe_build_refusals_and_override(tmp_path):
    e = env(tmp_path)
    other = fake_pe(tmp_path / "other.exe", 0x12345678, SIZE)
    small = fake_pe(tmp_path / "small.exe", STAMP)
    text = tmp_path / "text.exe"
    text.write_bytes(b"not a program")
    for p, words in ((other, "a build outside the known list"), (small, "unknown size"), (text, "not a PE file"),
                     (tmp_path / "none.exe", "does not exist")):
        why = gameinstall.exe_refusal(p, e)
        assert why and words in why and "C5_ALLOW_UNKNOWN_EXE_BUILD=1" in why, (p, why)
    with pytest.raises(AssertionError, match="a build outside the known list"):
        gameinstall.require_exe(other, e)
    with pytest.raises(AssertionError, match="missing"):
        gameinstall.require_exe(tmp_path / "none.exe", e)
    for size in (16982584, 16982840):
        known = fake_pe(tmp_path / ("known%d.exe" % size), STAMP, size)
        assert gameinstall.exe_refusal(known, e) is None
        gameinstall.require_exe(known, e)
    assert gameinstall.exe_refusal(other, dict(e, C5_ALLOW_UNKNOWN_EXE_BUILD="0"))
    assert gameinstall.exe_refusal(other, dict(e, C5_ALLOW_UNKNOWN_EXE_BUILD="1")) is None


def test_require_install(tmp_path):
    inst = tmp_path / "inst"
    inst.mkdir()
    e = env(tmp_path, C5_GAME_DIR=str(inst))
    with pytest.raises(AssertionError, match="installed game is needed"):
        gameinstall.require_install(environ=e)
    fake_pe(inst / gameinstall.EXE_NAME, 0x12345678, SIZE)
    with pytest.raises(AssertionError, match="a build outside the known list"):
        gameinstall.require_install(environ=e)
    fake_pe(inst / gameinstall.EXE_NAME, STAMP, SIZE)
    assert gameinstall.require_install(environ=e) == inst
