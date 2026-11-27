"""Ticket 0688 (epic 0686 P1): the composite's pacing instruments on the stand-in (shell/common/vp_pacing.h; ini pacing).

lockstep_server.exe stalls for 1 s (--stall-on: a file the test makes once lockstep frames run) while test_host.exe pans
its fake camera (--fake-cam-pan) or holds it still. Checked from the composite's outputs:
- panning: the stall is ONE freeze of about its length: one "pacing freeze:" line, the run-long "pacing totals:" line at
  the process's end (why=exit) with it as freeze_max, "pacing 1s:" lines with world=1, the per-frame record's `wu` and
  `world_age_ms` columns, and perf/pacing/pacing_report.py finding the same freeze in the record (same QPC bounds);
- a still camera: the same stall is no freeze (a long world-update interval, wui_max, but freezes=0);
- pacing=0: no pacing line, no vidmem line, wu = -1 in every row (the cost arm's configuration);
- the EXE's memory and video budget: one "vidmem 10s:" line at the start with the adapter's budget.
Every process on the hidden desktop (rule 14). Skipped counts as failed (conftest.py). About 25 s a case.
Ticket 0701 (2026-11-23): the stall's freeze is found by its time (the QPC of the stall file's write), not as the only
long freeze of the run: under CPU load the UpdateViews hook's commit suspends the host's threads for about 1 s (a second
freeze of >= 800 ms before the stall) and the starved stand-in delivers a world picture every 110-150 ms (world ages of
100+ ms). Both are true measurements, not faults; research/08-performance/frame-pacing-instruments.md section 5.
"""
import ctypes
import re
import sys
import time

import pytest

import shellrun
from test_lockstep import EXE_TAPS_OFF, HOOK_COMMIT_WAIT_S, HOST, INI, SERVER_S, lrun  # noqa: F401 (lrun: the fixture)

sys.path.insert(0, str(shellrun.SHELL.parent / "perf" / "pacing"))
import pacing_report  # noqa: E402

KV = re.compile(r"(\w+)=(\S+)")


def kv(line, key):
    d = {}
    for k, v in KV.findall(line.split(key, 1)[1]):
        try:
            d[k] = float(v)
        except ValueError:
            d[k] = v
    return d


def lines_of(lines, key):
    return [kv(ln, key) for ln in lines if key in ln]


def qpc():
    """QueryPerformanceCounter and its frequency: the clock of the composite's `qpc0`/`qpc1` and the record's columns."""
    c, f = ctypes.c_int64(), ctypes.c_int64()
    ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(c))
    ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(f))
    return c.value, f.value


def stall_freeze(fz, go_qpc, freq, stall_s):
    """Ticket 0701: the stall's freeze among the composite's freezes `fz` (dicts of `pacing freeze:` lines). The window
    is [the stall file's write, + the stall + 1 s]; in it exactly one freeze of >= 800 ms, and it covers the stall (it
    starts at most 0.5 s after the write and ends at least 0.8 x the stall after it). Long freezes outside the window (the
    hook commit's thread suspension under load) are not the stall's. A double freeze or a stall split in two fails."""
    w0, w1 = go_qpc, go_qpc + int((stall_s + 1.0) * freq)
    long_ = [f for f in fz if f["ms"] >= 800 and f["qpc1"] > w0 and f["qpc0"] < w1]
    assert len(long_) == 1, (long_, fz)
    f = long_[0]
    assert f["qpc0"] <= go_qpc + 0.5 * freq and f["qpc1"] >= go_qpc + 0.8 * stall_s * freq, (f, go_qpc, freq)
    return f


def stall_run(lrun, pan, pacing="1", stall_s="1.0"):
    lrun.set_ini(**dict(INI, pacing=pacing))
    go = lrun.dir / "stall.go"
    lrun.start_lockstep_server("--stall-on", str(go), "--stall-for", stall_s, seconds=SERVER_S)
    extra = ("--fake-cam-pan", "400") if pan else ()
    lrun.start_host(*HOST, *extra, seconds=7)
    lrun.wait_log(r"lsprobe: hook ENABLED", timeout_s=HOOK_COMMIT_WAIT_S)
    time.sleep(2.0)
    lrun.go_qpc, lrun.qpc_freq = qpc()   # ticket 0701: the stall starts at most 5 ms after this (the server looks every 5 ms)
    go.write_text("go")
    lines = lrun.finish()
    srv = lrun.server_log.read_text(errors="replace")
    assert "stall: no frames from" in srv and "stall over at" in srv, srv[-1500:]
    return lines


def test_shipped_ini_has_pacing_on():
    p = shellrun.SHELL / "composite" / "shell_composite.ini"
    text = p.read_text(encoding="utf-8")
    assert re.findall(r"(?m)^\s*pacing\s*=\s*(\S+)\s*$", text) == ["1"]


def test_stall_while_panning_is_one_freeze(lrun):
    lines = stall_run(lrun, pan=True)
    fz = lines_of(lines, "pacing freeze: ")
    f = stall_freeze(fz, lrun.go_qpc, lrun.qpc_freq, 1.0)
    assert f["ms"] < 3000 and f["frames"] > 20 and f["qpc1"] > f["qpc0"] > 0, f
    secs = lines_of(lines, "pacing 1s: ")
    assert len(secs) >= 4 and all(s["world"] == 1 for s in secs[1:]), secs[:3]
    # the instruments' own main-thread time per Present (a bound, not a timing claim: a loaded machine stretches it)
    per = sorted(s["self_ns_per_present"] for s in secs if s["presents"] > 0)
    assert per and 0 < per[len(per) // 2] < 20000, per
    assert sum(s["freezes50"] for s in secs) >= 1 and max(s["freeze_max"] for s in secs) >= 800
    assert sum(s["moving"] for s in secs) > 100
    tot = lines_of(lines, "pacing totals: ")
    assert tot and tot[-1]["why"] == "exit", [ln for ln in lines if "pacing totals" in ln][-2:]
    t = tot[-1]
    # 0701: freeze_max may be a longer hitch outside the stall (the hook commit under load); the age's median is bounded by
    # the stall's freeze, not by a time (a starved stand-in honestly shows world ages of 100+ ms; an age not reset by the
    # world updates, or taken on another clock, still fails)
    assert t["freezes50"] >= 1 and t["freeze_max"] >= f["ms"] - 0.2 and t["ft_n"] > 300 and t["wui_max"] >= 800, t
    assert t["age_n"] > 300 and 0 <= t["age_p50"] < f["ms"] / 2 and t["freeze_ms_per_min"] > 0, t
    assert t["ft_bins"].count("/") == 8
    vid = lines_of(lines, "vidmem 10s: ")
    assert vid and vid[0]["process"] == "exe" and vid[0]["private_mb"] > 0, vid[:1]
    assert vid[0]["adapter"] == 1 and vid[0]["local_budget_mb"] > 0, vid[:1]
    # the per-frame record: wu and world_age_ms; the reader finds the same freeze in it (QPC bounds equal)
    rows = lrun.rows()
    assert {r["wu"] for r in rows} <= {0, 1} and sum(r["wu"] for r in rows) > 100
    assert any(r["world_age_ms"] > 0 for r in rows)
    rec = pacing_report.read_csv(lrun.dir / "shell_composite.lockstep.csv")
    big = [x for x in rec["freezes"] if x["ms"] >= 800]
    assert sum(x["qpc0"] == int(f["qpc0"]) and x["qpc1"] == int(f["qpc1"]) for x in big) == 1, (big, f)
    # every long freeze of the record is one of the composite's lines (0701: under load there can be more than the stall's)
    assert {(x["qpc0"], x["qpc1"]) for x in big} == {(int(x["qpc0"]), int(x["qpc1"])) for x in fz if x["ms"] >= 800}, (big, fz)


SHOWN = (0, 1, 3)   # the CSV's kind: exact, older, nopub (a world picture drawn; shell_composite.cpp kLkKindNames)


def test_stall_longer_than_256_presents_keeps_world_ages(lrun):
    """Ticket 0705: a stall longer than the composite's 256-entry publish table (g_lkPubTab) keeps the shown picture's
    world age. The v3 source looked the picture's publish QPC up again at every Present, so a picture shown more than 256
    Presents after its publish got srv_cam_pub_qpc=0 and world_age_ms=-1 and dropped out of the age histogram (a 1-s
    stall's ages stopped at 570 ms). A 2-s stall at the host's uncapped frame rate (--vsync 0): every stalled Present
    keeps its age, the ages reach the stall's length, and the run's age histogram counts every shown frame."""
    lines = stall_run(lrun, pan=True, stall_s="2.0")
    fz = lines_of(lines, "pacing freeze: ")
    f = stall_freeze(fz, lrun.go_qpc, lrun.qpc_freq, 2.0)
    freq = lrun.qpc_freq
    rows = lrun.rows()
    stalled = [r for r in rows if f["qpc0"] < r["present_qpc"] < f["qpc1"]]
    # the case is only a case when the stall outlasts the table (else it cannot fail on the old composite)
    assert f["frames"] > 256 and len(stalled) > 256, (f, len(stalled))
    assert all(r["kind"] in SHOWN and r["taken_f"] > 0 for r in stalled), [r for r in stalled if r["kind"] not in SHOWN][:3]
    lost = [r for r in stalled if r["world_age_ms"] < 0 or r["srv_cam_pub_qpc"] <= 0]
    assert not lost, (len(lost), len(stalled), lost[:2])
    # the shown picture was published before the freeze's first Present, so the last stalled Present's age is at least
    # the time from that Present to it (the stall's length less one frame)
    reach = (max(r["present_qpc"] for r in stalled) - f["qpc0"]) * 1000.0 / freq
    age_max = max(r["world_age_ms"] for r in stalled)
    assert reach >= 0.8 * 2000 and age_max >= reach - 0.01, (age_max, reach, f)
    t = lines_of(lines, "pacing totals: ")[-1]
    assert t["age_max"] >= reach - 0.1, (t["age_max"], reach)
    # every shown frame of the run is in the age histogram: no shown row without an age, and the Presents the histogram
    # lacks are exactly the record's rows without a picture (the record's first rows, before the first world picture; its
    # last ~0.5 s is not flushed when the host ends, but those Presents all show a picture, so they count on both sides)
    shown = [r for r in rows if r["kind"] in SHOWN and r["taken_f"] > 0]
    assert all(r["world_age_ms"] >= 0 for r in shown), [r for r in shown if r["world_age_ms"] < 0][:2]
    assert sum(r["world_age_ms"] >= 0 for r in rows) == len(shown)
    assert t["presents"] - t["age_n"] == len(rows) - len(shown), (t["presents"], t["age_n"], len(rows), len(shown))


def test_stall_freeze_check_refuses_a_double_or_split_freeze():
    """Ticket 0701's negative control of stall_freeze (no process): the stall's freeze passes, a long freeze outside the
    window (the hook commit's hitch under load) is allowed, a second long freeze in the window, a stall split in two
    and a long freeze that does not cover the stall fail."""
    F, go = 10_000_000, 1_000_000_000

    def fr(t0, t1, frames=100):
        return {"ms": (t1 - t0) * 1000.0, "frames": frames, "qpc0": go + int(t0 * F), "qpc1": go + int(t1 * F)}

    stall = fr(-0.002, 1.003)
    assert stall_freeze([fr(-3.1, -1.9, 4), stall, fr(1.2, 1.35)], go, F, 1.0) is stall
    for bad in ([stall, fr(1.1, 1.95)],                   # a double freeze: a second long one in the window
                [fr(-0.002, 0.5), fr(0.5, 1.003)],        # the stall split in two (two ~500 ms freezes)
                [fr(-1.5, -0.1), fr(0.9, 1.003)],         # no long freeze covers the stall
                [fr(0.7, 1.6)]):                         # a long freeze that starts after the stall began
        with pytest.raises(AssertionError):
            stall_freeze(bad, go, F, 1.0)


def test_stall_with_a_still_camera_is_no_freeze(lrun):
    lines = stall_run(lrun, pan=False)
    assert not [ln for ln in lines if "pacing freeze: " in ln and kv(ln, "pacing freeze: ")["ms"] >= 800]
    secs = lines_of(lines, "pacing 1s: ")
    assert max(s["wui_max"] for s in secs) >= 800, [s["wui_max"] for s in secs]
    assert sum(s["moving"] for s in secs) <= 2   # the camera of the first Presents only
    t = lines_of(lines, "pacing totals: ")[-1]
    assert t["freezes50"] == 0 and t["wui_max"] >= 800, t


def test_pacing_off_writes_nothing(lrun):
    lines = stall_run(lrun, pan=True, pacing="0", stall_s="0.3")
    ours = ("pacing 1s:", "pacing totals:", "pacing freeze:", "vidmem 10s:")
    assert not [ln for ln in lines if any(k in ln for k in ours)], [ln for ln in lines if any(k in ln for k in ours)][:3]
    assert any("pacing=0" in ln for ln in lines)   # the values-used line says so
    rows = lrun.rows()
    assert rows and all(r["wu"] == -1 and r["world_age_ms"] == -1 for r in rows), rows[:2]


def test_cost_arms_pacing_on_off(tmp_path, hidden_desktop, built, host_closer):
    """MC #10822 condition 2: the composite's frame with pacing=1 against pacing=0 on the stand-in (lockstep_server, the
    camera panning, vsync off: the host presents as fast as it can, so the instruments' cost would show in its frame
    time). C5_PACING_COST_ROUNDS rounds (default 1: the arms' plumbing only), arms alternating, order rotated per round;
    per run the mean of the composite's `stats` ft_mean after the first 2 lines, and with pacing=1 the instruments' own
    self_ns_per_present. C5_PACING_COST_OUT=<file.json>: the raw values (statistics in research/08 frame-pacing-
    instruments.md 4). Asserted: every run produced its numbers; nothing about time (a loaded machine)."""
    import json
    import os
    from test_lockstep import LsRun
    rounds = int(os.environ.get("C5_PACING_COST_ROUNDS", "1"))
    res = {"rounds": rounds, "ft_mean": {"0": [], "1": []}, "self_ns_per_present": []}
    for rnd in range(rounds):
        for arm in (("0", "1") if rnd % 2 == 0 else ("1", "0")):
            r = LsRun(tmp_path / ("r%d_%s" % (rnd, arm)), hidden_desktop, "cost%d%s" % (rnd, arm))
            r.close_fn = host_closer
            try:
                r.set_ini(handover="v3", lockstep="0", depth_write="0", **EXE_TAPS_OFF)
                r.set_ini(**dict(INI, stats_ms="1000", lockstep_csv="0", pacing=arm))
                r.start_lockstep_server(seconds=SERVER_S)
                r.start_host(*HOST, "--fake-cam-pan", "400", seconds=8)
                lines = r.finish()
            finally:
                r.stop()
            st = shellrun.stats(lines)[2:]
            assert len(st) >= 4, (arm, len(st))
            res["ft_mean"][arm].append(sum(x["ft_mean"] for x in st) / len(st))
            if arm == "1":
                secs = [s for s in lines_of(lines, "pacing 1s: ")[2:] if s["presents"] > 0]
                assert secs, "no pacing 1s line with presents"
                res["self_ns_per_present"].append(sum(s["self_ns_per_present"] for s in secs) / len(secs))
    out = os.environ.get("C5_PACING_COST_OUT")
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1)
    assert len(res["ft_mean"]["0"]) == rounds and len(res["ft_mean"]["1"]) == rounds
