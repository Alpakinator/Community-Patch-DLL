"""Issue 3006: the tagged insertion point with MinHook inline hooks, on the stand-in (hidden desktop).

test_host --game renders like the game: every list recorded on its own deferred context (on four worker threads),
executed with ExecuteCommandList; the scene clear (back buffer + D24S8) is in a deferred list; a "terrain" pre-pass
into an offscreen target; a "units" list that draws a landmark and two units with a depth buffer; a UI list; and with
--vary 1 the terrain position and 0-3 extra offscreen lists change from frame to frame, so list indices move.

Checked: each tag rule (after / before / inlist) puts the server image above the clear and under the units and the UI
in the dumped back buffer, with 0 missed insertions and 0 insertions outside the clear..3D window; the prototype's
fixed exec index lands outside that window when the order varies; negative cases are caught (a rule that matches
nothing, no server, a killed server with and without hide_when_stale, every kill switch and mode=off, read back from
the host's memory); a swap chain created through the factory is adopted at its first Present.
"""
import re
import time

import pytest

import shellrun

FREE = (400, 60, 1000, 170)       # map area free of UI and units (1280x720)
TOP_BAR = (0, 0, 800, 8)          # opaque UI bar (46, 51, 71)
GREY = (38, 38, 38)               # the scene clear
# (x, y) -> colour the units list must leave there (depth-tested: orange z .3 < cyan z .6 < yellow landmark z .8)
UNITS = {(440, 520): (255, 255, 0), (580, 620): (0, 255, 255), (540, 560): (0, 255, 255),
         (620, 600): (255, 127, 0), (680, 650): (255, 127, 0)}
GAME = ("--game", "--vary", "1", "--mt", "1", "--vsync", "0", "--occluded-sleep", "0", "--load", "after")


def near(c, want, tol=3):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def check_units_and_ui(img):
    for (x, y), want in UNITS.items():
        assert near(img.at(x, y), want), ((x, y), img.at(x, y), want)
    assert shellrun.fraction(img.region(*TOP_BAR, step=4),
                             lambda c: near(c, (46, 51, 71), 2)) > 0.98


def free_area(img):
    free = list(img.region(*FREE))
    return shellrun.fraction(free, shellrun.is_server), shellrun.fraction(free, lambda c: c == GREY)


def game_run(run, ini, seconds=6, server=True, shot_after=r" stats ", host_extra=()):
    run.set_ini(**ini)
    if server:
        run.start_server("--rate", "60", seconds=seconds + 15)
    run.start_host(*GAME, *host_extra, seconds=seconds)
    run.wait_log(shot_after, timeout_s=15)
    time.sleep(1.2)
    bmp = run.shot()
    assert run.host.wait(seconds + 20) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert len(rows) >= 3, shellrun.tail(run.composite_log, 10)
    return shellrun.Image(bmp), rows[1:]   # the first interval contains hook installation and adoption


@pytest.mark.parametrize("rule", ["after", "before", "inlist"])
def test_tagged_rule_under_units_and_ui(run, rule):
    img, rows = game_run(run, dict(mode="composite", insert="tagged", tag_rule=rule, trace_frames="2"))
    frames = sum(r["frames"] for r in rows)
    assert frames > 100
    assert sum(r["missed_ins"] for r in rows) == 0, [r["missed_ins"] for r in rows]
    assert sum(r["ins_wrong"] for r in rows) == 0
    assert sum(r["ins_checked"] for r in rows) == frames
    assert sum(r["untagged"] for r in rows) == 0
    assert not any(r["disabled"] for r in rows)
    assert min(r["lists_pf"] for r in rows) >= 4.0
    srv, grey = free_area(img)
    assert srv > 0.9 and grey < 0.01, (srv, grey)
    check_units_and_ui(img)
    log = "\n".join(run.log_lines())
    marker = {"after": "INSERT after (tagged)", "before": "INSERT before (tagged)", "inlist": "INSERT inside (inlist)"}[rule]
    assert marker in log
    if rule == "inlist":
        assert sum(r["rec_n"] for r in rows) > 0      # the draw was recorded on the worker threads


def test_exec_index_is_fragile(run):
    """The prototype's fixed index (exec nth=2) with a varying order: insertions land outside the clear..3D window in
    a large share of frames (counted from the tags), which the tagged rule never does."""
    _img, rows = game_run(run, dict(mode="composite", insert="exec", nth="2", trace_frames="0"))
    checked = sum(r["ins_checked"] for r in rows)
    wrong = sum(r["ins_wrong"] for r in rows)
    assert checked > 100 and wrong / checked > 0.2, (wrong, checked)


@pytest.mark.parametrize("match", ["dsv_fmt=999", "bogus_tag"])
def test_wrong_rule_is_caught(run, match):
    """A rule that matches nothing: every frame counted as a missed insertion (reason nomatch), nothing drawn over
    units or anywhere else."""
    img, rows = game_run(run, dict(mode="composite", insert="tagged", tag_rule="after", tag_match=match, trace_frames="0"))
    frames = sum(r["frames"] for r in rows)
    assert sum(r["missed_ins"] for r in rows) == frames
    assert sum(r["miss_nomatch"] for r in rows) == frames
    assert sum(r["comp_n"] for r in rows) == 0
    srv, grey = free_area(img)
    assert srv < 0.01 and grey > 0.99, (srv, grey)
    check_units_and_ui(img)
    if match == "bogus_tag":
        assert any("UNKNOWN TOKEN" in ln for ln in run.log_lines())


def test_missing_server_is_reported(run):
    """No server: reported (no section, STALE); the insertion point still fires; nothing is drawn."""
    img, rows = game_run(run, dict(mode="composite", insert="tagged", tag_rule="after", trace_frames="0"), server=False)
    lines = run.log_lines()
    assert any("no server: section" in ln for ln in lines)
    assert any("server STALE" in ln for ln in lines)
    assert all(r["stale"] == 1 for r in rows)
    assert sum(r["missed_ins"] for r in rows) == 0
    assert sum(r["nodraw"] for r in rows) > 0
    srv, grey = free_area(img)
    assert srv < 0.01 and grey > 0.99
    check_units_and_ui(img)


@pytest.mark.parametrize("hide", [0, 1])
def test_killed_server_last_frame_or_nothing(run, hide):
    """Server killed while running: STALE after stale_ms; hide_when_stale=0 keeps drawing the last frame, 1 draws
    nothing. Only our own test server is terminated (by handle)."""
    run.set_ini(mode="composite", insert="tagged", tag_rule="inlist", hide_when_stale=str(hide), stale_ms="500", trace_frames="0")
    run.start_server("--rate", "60", seconds=30)
    run.start_host(*GAME, seconds=9)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=15)
    run.server.kill()
    run.wait_log(r"server STALE", timeout_s=5)
    time.sleep(0.5)
    img = shellrun.Image(run.shot())
    assert run.host.wait(30) == 0
    srv, grey = free_area(img)
    if hide:
        assert srv < 0.01 and grey > 0.99, (srv, grey)
    else:
        assert srv > 0.9 and grey < 0.01, (srv, grey)
    check_units_and_ui(img)


HOOK_LINE = re.compile(r"hook (\S+) var (\d) target ([0-9A-F]+) \((\S+)\) orig ((?:[0-9A-F]{2} ){4}[0-9A-F]{2}) switch hook_(\w+)")
SWITCHES = ["present", "resize", "create_swapchain", "create_deferred", "clear", "clear_dsv", "omset", "finish", "exec",
            "units",   # issue 4752: hook_units, every hook of the unit-draw filter
            "models"]  # ticket 4874: hook_models, the model tap's own hooks


def hook_targets(lines):
    t = []
    for ln in lines:
        m = HOOK_LINE.search(ln)
        if m:
            t.append(dict(name=m.group(1), addr=int(m.group(3), 16), orig=bytes.fromhex(m.group(5)), sw=m.group(6)))
    return t


def set_switch(run, key, value, timeout_s=5.0):
    """Write hook_<key>=value and wait for the DLL's next `hooks:` line that shows it."""
    n0 = len(run.log_lines())
    run.set_ini(**{"hook_" + key: str(value)})
    run.wait_log(r"hooks: .*\b%s=%d\b" % (re.escape(key), value), timeout_s=timeout_s, start=n0)


def test_kill_switches_and_mode_off(run):
    """Every hook_<x>=0 disables exactly its targets (their first bytes read back from the host equal the original
    prologue again) and =1 re-enables them; the effect is visible in the stats; mode=off restores every target."""
    run.set_ini(mode="composite", insert="tagged", tag_rule="after", trace_frames="0", stats_ms="300")
    run.start_server("--rate", "60", seconds=90)
    run.start_host(*GAME, seconds=60)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=15)
    targets = hook_targets(run.log_lines())
    assert {t["sw"] for t in targets} == set(SWITCHES), targets
    host = run.host

    def patched(t):
        return host.read_memory(t["addr"], 5) != t["orig"]

    assert all(patched(t) for t in targets)
    state = {s: 1 for s in SWITCHES}
    for sw in SWITCHES:
        state[sw] = 0
        set_switch(run, sw, 0)
        for t in targets:
            assert patched(t) == bool(state[t["sw"]]), (sw, t["name"], hex(t["addr"]))
        if sw in ("exec", "finish"):
            n0 = len(run.log_lines())
            time.sleep(1.0)
            rows = shellrun.stats(run.log_lines()[n0:])[1:]
            assert rows, "no stats while hook_%s=0" % sw
            if sw == "exec":
                assert all(r["missed_ins"] == r["frames"] and r["miss_noexec"] == r["frames"] for r in rows), rows
            else:
                assert all(r["missed_ins"] == r["frames"] and r["untagged"] > 0 for r in rows), rows
        state[sw] = 1
        set_switch(run, sw, 1)
        assert all(patched(t) for t in targets)
    n0 = len(run.log_lines())
    run.set_ini(mode="off")
    run.wait_log(r"hooks: present=0 resize=0 create_swapchain=0", timeout_s=5, start=n0)
    assert not any(patched(t) for t in targets)
    time.sleep(0.5)
    n_stats = len([ln for ln in run.log_lines() if " stats " in ln])
    time.sleep(1.5)
    assert len([ln for ln in run.log_lines() if " stats " in ln]) == n_stats   # no hook runs: nothing is counted
    assert host.alive()
    n0 = len(run.log_lines())
    run.set_ini(mode="composite")
    run.wait_log(r"hooks: present=1 resize=1", timeout_s=5, start=n0)
    assert all(patched(t) for t in targets)
    run.wait_log(r" stats .*mode=composite", timeout_s=5, start=n0)
    host.kill()


def _ts(line):
    h, m, s = line.split()[0].split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


@pytest.mark.parametrize("adopt", [1, 0])
def test_swap_chain_adopted_at_creation(run, adopt):
    """--recreate-at: the host destroys its window and swap chain and creates new ones (the game's resolution change).
    adopt_on_create=1: the new chain is composited from its first Present; 0: the prototype's 2 s silence rule."""
    run.set_ini(mode="composite", insert="tagged", tag_rule="after", adopt_on_create=str(adopt), trace_frames="0")
    run.start_server("--rate", "60", seconds=30)
    run.start_host(*GAME, "--recreate-at", "4", seconds=9)
    rec = run.wait_log(r"recreate: new window", which="host", timeout_s=15)
    adopted = run.wait_log(r"adopted swap chain .*(first Present after creation|old target fell silent)", timeout_s=8)
    assert run.host.wait(30) == 0
    gap = _ts(adopted) - _ts(rec)
    if adopt:
        assert "first Present after creation" in adopted
        assert gap < 0.3, gap
    else:
        assert "fell silent" in adopted
        assert 1.8 < gap < 3.0, gap
    lines = run.log_lines()
    after = [ln for ln in lines if " stats " in ln and _ts(ln) > _ts(adopted) + 1.1]
    rows = shellrun.stats(after)
    assert rows and sum(r["missed_ins"] for r in rows) == 0
