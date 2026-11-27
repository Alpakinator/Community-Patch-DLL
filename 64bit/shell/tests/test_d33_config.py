"""Bug 3010, option A (MC decision 2026-11-13): the D33 configuration written by shell/tools/darkred.py keeps the
StrategicView system on. With it off, any route into view mode 2 (F10, the minimap button, ToggleStrategicView) hangs
the EXE's main thread (research/02-exe-internals/disable-systems-and-masks.md, 3008 reproduction). Offline: the tool
edits a config.ini in a temporary folder; no instance is touched.
"""
import argparse
import re
import subprocess
import sys

import pytest

import shellrun

sys.path.insert(0, str(shellrun.SHELL / "tools"))
import darkred  # noqa: E402


def make_ini(path, value):
    keys = darkred.D33_OFF + darkred.D33_ON
    body = "".join("%s = %d\r\n" % (k, value) for k in keys)
    path.write_bytes(("[Something]\r\nA = 1\r\n[DisableSystems]\r\n" + body + "[Other]\r\nB = 2\r\n").encode("latin-1"))


def section(path):
    txt = path.read_bytes().decode("latin-1")
    sec = re.search(r"\[DisableSystems\](.*?)(\r?\n\[|\Z)", txt, re.S).group(1)
    return {k: int(v) for k, v in re.findall(r"(?m)^(\w+)\s*=\s*(\d+)", sec)}


@pytest.fixture
def paths(tmp_path, monkeypatch):
    cfg, marker = tmp_path / "config.ini", tmp_path / "viewmask.enabled"
    monkeypatch.setattr(darkred, "CONFIG", cfg)
    monkeypatch.setattr(darkred, "MARKER", marker)
    return cfg, marker


@pytest.mark.parametrize("before", [0, 1])
def test_d33_keeps_strategic_view_on(paths, before):
    """From an all-on and from an all-off [DisableSystems] (the old D33 had StrategicView = 1): after `config --arm
    d33` StrategicView is 0 (on), every key of D33_OFF is 1, the other D33_ON keys 0, and the marker is written."""
    cfg, marker = paths
    make_ini(cfg, before)
    darkred.cmd_config(argparse.Namespace(arm="d33", on=[], whitemask=False))
    s = section(cfg)
    assert s["StrategicView"] == 0
    assert "StrategicView" not in darkred.D33_OFF and "StrategicView" in darkred.D33_NEVER_OFF
    assert all(s[k] == 1 for k in darkred.D33_OFF), s
    assert all(s[k] == 0 for k in darkred.D33_ON), s
    assert marker.read_text() == "clear=0x1\n"
    assert cfg.read_bytes().decode("latin-1").startswith("[Something]\r\nA = 1\r\n")


def test_on_strategic_view_is_accepted_and_changes_nothing(paths):
    """`--on StrategicView` (the 3010 live check's command line) is accepted and gives the same file as the default."""
    cfg, _ = paths
    make_ini(cfg, 1)
    darkred.cmd_config(argparse.Namespace(arm="d33", on=[], whitemask=False))
    plain = cfg.read_bytes()
    make_ini(cfg, 1)
    darkred.cmd_config(argparse.Namespace(arm="d33", on=["StrategicView"], whitemask=False))
    assert cfg.read_bytes() == plain


@pytest.mark.parametrize("before", [0, 1])
def test_d33_keeps_overlay_on(paths, before):
    """Bug 0127 / D93 (2026-11-13): Overlay stays on in D33 (with it off SerialEventHexHighlight faults at VA 0x574B03).
    From an all-on and an all-off [DisableSystems], `config --arm d33` writes Overlay = 0; `--on Overlay` changes
    nothing; OverlayDebug stays off."""
    cfg, _ = paths
    make_ini(cfg, before)
    darkred.cmd_config(argparse.Namespace(arm="d33", on=[], whitemask=False))
    s = section(cfg)
    assert s["Overlay"] == 0 and s["OverlayDebug"] == 1, s
    assert "Overlay" not in darkred.D33_OFF and "Overlay" in darkred.D33_NEVER_OFF
    plain = cfg.read_bytes()
    make_ini(cfg, before)
    darkred.cmd_config(argparse.Namespace(arm="d33", on=["Overlay"], whitemask=False))
    assert cfg.read_bytes() == plain


@pytest.mark.parametrize("before", [0, 1])
def test_d33_keeps_combat_systems_on(paths, before):
    """Issue 0368 (D147, 2026-11-15): Particle, Combat and Projectile stay on in D33 (the beta draws the EXE's own
    combat). From an all-on and an all-off [DisableSystems], `config --arm d33` writes 0 into the three; the rest of
    the D33 list is still 1."""
    cfg, _ = paths
    make_ini(cfg, before)
    darkred.cmd_config(argparse.Namespace(arm="d33", on=[], whitemask=False))
    s = section(cfg)
    for k in ("Particle", "Combat", "Projectile"):
        assert k in darkred.D33_ON and k not in darkred.D33_OFF, k
        assert s[k] == 0, (k, s)
    assert all(s[k] == 1 for k in ("Decal", "Forest", "FOW", "FluidFOW", "DistanceFog", "YieldIcons")), s


def test_cli_accepts_on_overlay():
    r = subprocess.run([sys.executable, str(shellrun.SHELL / "tools" / "darkred.py"), "config", "--arm", "d33",
                        "--on", "Overlay"], capture_output=True, text=True, timeout=60)
    assert r.returncode == 1 and "no instance" in r.stderr, (r.returncode, r.stderr[-300:])


def test_cli_accepts_on_strategic_view():
    r = subprocess.run([sys.executable, str(shellrun.SHELL / "tools" / "darkred.py"), "config", "--arm", "d33",
                        "--on", "StrategicView"], capture_output=True, text=True, timeout=60)
    # no instance given: refused after the command line was accepted (exit 1, not argparse's exit 2)
    assert r.returncode == 1 and "no instance" in r.stderr, (r.returncode, r.stderr[-300:])


# Lead's order 2026-11-13 (after an old copy of darkred.py wrote instance c5h-b, claimed by another worker): the tool
# refuses an instance that C5_POOL_OWNER has not claimed. Pool claims are faked here: nothing reads or writes an instance.
sys.path.insert(0, str(shellrun.REPO))
from tools.harness import pool  # noqa: E402


@pytest.mark.parametrize("claim, owner, ok", [
    (None, "me", False),                                   # not claimed
    ({"owner": "someone-else", "expires": None}, "me", False),
    ({"owner": "me", "expires": 1.0}, "me", False),        # expired
    ({"owner": "me", "expires": None}, "", False),         # C5_POOL_OWNER not set
    ({"owner": "me", "expires": None}, "me", True),
])
def test_instance_must_be_claimed_by_us(monkeypatch, claim, owner, ok):
    monkeypatch.setattr(pool, "read_claim", lambda name: claim)
    monkeypatch.setenv("C5_POOL_OWNER", owner)
    if ok:
        darkred.check_claim("c5h-x")
    else:
        with pytest.raises(SystemExit) as e:
            darkred.check_claim("c5h-x")
        assert "c5h-x" in str(e.value) and ("not claimed" in str(e.value) or "refused" in str(e.value))


def test_use_instance_checks_the_claim_first(monkeypatch):
    monkeypatch.setattr(pool, "read_claim", lambda name: {"owner": "someone-else", "expires": None})
    monkeypatch.setenv("C5_POOL_OWNER", "me")
    monkeypatch.setattr(darkred, "INST", None)
    with pytest.raises(SystemExit):
        darkred.use_instance("c5h-x")
    assert darkred.INST is None   # nothing was resolved, so nothing can be written
