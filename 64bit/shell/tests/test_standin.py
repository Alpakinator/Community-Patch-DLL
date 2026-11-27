"""Issue 3001: the Gate 3 prototype runs from this repository on a hidden desktop.

shell_server.exe + test_host.exe with shell_composite.dll loaded (insert=clear, the stand-in clears the back buffer on
its immediate context), a few seconds; then from the composite log: the composite fired with 0 missed insertions, the
server published frames; and a back-buffer dump (.shot trigger) shows the server's palette in the free map area under
the host's opaque UI.
"""
import pytest

import desktops
import shellrun

# test_host's default 1280x720 frame: free of UI between the left panel (x < 380) and the centre dialog (y > 230),
# above the spinning quad (y > 176) and below the top bar (y < 36).
FREE = (400, 60, 1000, 170)
TOP_BAR = (0, 0, 800, 8)          # opaque bar colour (0.18, 0.20, 0.28) = (46, 51, 71)
GREY = (38, 38, 38)               # the host's clear colour (0.15)


def _bar(c):
    return abs(c[0] - 46) <= 2 and abs(c[1] - 51) <= 2 and abs(c[2] - 71) <= 2


def test_refuses_interactive_desktop(tmp_path):
    """The launcher refuses the owner's desktop before anything starts (rule 14b: a fake, non-existent binary, so nothing
    could start even if the check were missing)."""
    fake = str(tmp_path / "not_a_program.exe")
    for desk in (None, "Default", "input", "WinSta0\\Default"):
        with pytest.raises(desktops.InteractiveDesktopRefused):
            shellrun.launch([fake], desk, env={"C5_HARNESS_INTERACTIVE_DESKTOP": ""})


def test_composite_under_ui(run, monkeypatch):
    monkeypatch.delenv("C5_HARNESS_INTERACTIVE_DESKTOP", raising=False)
    run.set_ini(mode="composite", insert="clear", trace_frames="0")
    run.start_server("--rate", "60", seconds=20)
    run.start_host("--vsync", "0", "--load", "after", seconds=8)
    run.wait_log(r"opened shared texture", timeout_s=10)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    bmp = run.shot()
    assert run.host.wait(20) == 0, shellrun.tail(run.host_log, 10)

    rows = shellrun.stats(run.log_lines())
    assert len(rows) >= 3, "too few stats lines: %s" % shellrun.tail(run.composite_log, 10)
    steady = rows[1:]  # the first second includes adoption
    assert all(r["missed_ins"] == 0 for r in steady), [r["missed_ins"] for r in steady]
    assert sum(r["comp_n"] for r in steady) > 0
    assert sum(r["srv_new"] for r in steady) > 0
    assert not any(r["disabled"] for r in rows)
    server_lines = [ln for ln in run.log_lines("server") if " 1s: " in ln]
    assert any(int(ln.split("published ")[1].split()[0]) > 0 for ln in server_lines), server_lines[-3:]

    img = shellrun.Image(bmp)
    assert (img.w, img.h) == (1280, 720)
    free = list(img.region(*FREE))
    assert shellrun.fraction(free, shellrun.is_server) > 0.9
    assert shellrun.fraction(free, lambda c: c == GREY) < 0.01
    assert shellrun.fraction(img.region(*TOP_BAR, step=4), _bar) > 0.98


def test_passthrough_shows_only_the_host(run):
    """Negative control: hooks on but nothing drawn (mode=passthrough), server running: the same region is the host's
    clear grey, so the palette check above can fail."""
    run.set_ini(mode="passthrough", insert="clear", trace_frames="0")
    run.start_server("--rate", "60", seconds=20)
    run.start_host("--vsync", "0", "--load", "after", seconds=6)
    run.wait_log(r" stats ", timeout_s=10)
    bmp = run.shot()
    assert run.host.wait(20) == 0
    img = shellrun.Image(bmp)
    free = list(img.region(*FREE))
    assert shellrun.fraction(free, shellrun.is_server) < 0.01
    assert shellrun.fraction(free, lambda c: c == GREY) > 0.99
