"""One-off 9035 (2026-11-13): negative tests for the shell defects found by the independent verification of epic 3000
(one-off 9031, research/05-graphics-composition/shell-foundations-verification.md section 3): bugs 3014 (V2), 3015
(V1), 3016 (V3), 3017 (V4) and the group 3018 (V5 here; V6 in test_errors.py, V7 below, V8 in test_build_cmd.py).

Each test reaches the failing state the way the product does (bugfix charter rule 2): the ini value is written into the
DLL's ini, the hostile camera value into the fake EXE image of test_host (WriteProcessMemory, as a broken or racing
EXE would leave it), the extra swap chains are created through the hooked factory by the host, and the server really
changes its texture format. The helpers for the camera cases follow the verifier's own hostile cases
(shell/evidence/9031/hostile/hostile_cases.py, written by 9031, same repository). Everything runs on the hidden
desktop (rule 14).
"""
import ctypes
import math
import os
import re
import struct
import subprocess
import sys
import threading
import time
from ctypes import wintypes
from pathlib import Path

import pytest

import camblock
import shellrun

PLAIN = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after")
GAME = ("--game", "--vary", "1", "--mt", "1", "--vsync", "0", "--occluded-sleep", "0", "--load", "after")
FAKE = PLAIN + ("--fake-exe",)
FREE = (400, 60, 1000, 170)

VA_BASE = 0x400000
VA_APP, VA_NUMVIEWS, VA_PROJ, VA_VIEW = 0xCC8E74, 0xD90FA4, 0xDA12D0, 0xDA1310
NAN, INF = float("nan"), float("inf")

_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.OpenProcess.restype = wintypes.HANDLE
_k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
_k.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
_k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
_k.CloseHandle.argtypes = [wintypes.HANDLE]


class Mem:
    """Write into the test host's own fake EXE image (never into any other process)."""

    def __init__(self, pid):
        self.h = _k.OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, False, pid)
        assert self.h, ctypes.get_last_error()

    def write(self, addr, data):
        n = ctypes.c_size_t()
        assert _k.WriteProcessMemory(self.h, ctypes.c_void_p(addr), data, len(data), ctypes.byref(n)), ctypes.get_last_error()

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        assert _k.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
        return buf.raw

    def close(self):
        _k.CloseHandle(self.h)


def f32(*v):
    return struct.pack("<%df" % len(v), *v)


# Ticket 0494 (2026-11-17): the frame-rate floor of the timeout tests on a shared machine. A frame that waits timeout_ms
# (at most 50 ms, the clamp) gives at most stats_ms / 50 = 10 frames per 0.5 s row: the defect these tests guard against.
# A row at full rate has hundreds; a loaded machine can halve one row. So every row must exceed twice the defect's
# ceiling, and the median row the old floor of 50.
ROW_FLOOR, ROW_MEDIAN_FLOOR = 20, 50
# ... and the host of these tests lives until the test has its rows: closed by conftest.py's host_closer, with
# HOST_BACKSTOP_S on its own clock; the hook commit inside its first frames took up to 10.7 s under load (0494's series).
HOST_BACKSTOP_S, HOOK_COMMIT_WAIT_S = 45, 18


def full_rate(rows):
    f = sorted(r["frames"] for r in rows)
    return bool(f) and f[0] > ROW_FLOOR and f[len(f) // 2] > ROW_MEDIAN_FLOOR


# Ticket 0729 (2026-11-25): the rule of test_timeout_ms_limited_and_killed_server after the kill. The composite's line when
# kTimeoutGiveUp (8) waits in a row timed out (shell_composite.cpp AcquireAndCopy; logged once per process), and the
# stats row by which the give-up must have come: the kill's row is rows[0], the 8 clamped waits take 8 x 50-62.5 ms
# (0.4-0.5 s, one row), so the give-up falls into rows[1] or rows[2]; GIVEUP_LAST_ROW = 3 leaves one more row (0.5 s)
# for a host frame of a few hundred ms (0507's run 2: 215 ms outside the composite in one frame). An unclamped wait
# (4000000000 ms) never returns, and waits that are not counted never give up: both fail here.
GIVEUP_RX = re.compile(r"AcquireSync timed out (\d+) times in a row \(timeout_ms=(\d+)\)")
GIVEUP_COUNT, GIVEUP_LAST_ROW = 8, 3


def killed_server_rule(lines, used):
    """What test_timeout_ms_limited_and_killed_server proves after the kill, from the composite log's lines logged after
    the kill (the kill's own stats row first). used=0: no frame waits, so no give-up line, and every row after the
    kill's row is at full rate. used>0: the composite gives up after GIVEUP_COUNT waits of the clamped timeout (its one
    give-up line names `used`, at most GIVEUP_LAST_ROW rows after the kill's row), and the frame rate comes back: every
    row after the row the give-up fell into is at full rate (full_rate) with no frame waiting (acq_mean under 5 ms).

    Ticket 0729: until 2026-11-25 the rule was full_rate(rows[1:]) for every value. The give-up takes 8 x ~62.4 ms
    (~0.5 s, one whole row) once the killed server's 1-ms timer period is gone [I], and in 0507's run 2 one host frame
    of 279 ms (~215 ms of it outside the composite) stretched it over the whole row after the kill's row: 6 frames, and
    the test failed though the composite did what it must. The rows are counted from the give-up now, not from the
    kill; test_killed_server_rule_replays_0507_run2 replays that log. Returns the rows checked; raises AssertionError."""
    rows = shellrun.stats(lines)
    shown = [(r["frames"], r["ft_mean"]) for r in rows]
    giveups = [(i, m) for i, ln in enumerate(lines) for m in [GIVEUP_RX.search(ln)] if m]
    if not used:
        assert not giveups, ("timeout 0: no frame waits, so nothing times out", [lines[i] for i, _ in giveups])
        after = rows[1:]
    else:
        assert len(giveups) == 1, ("one give-up line after the kill", [lines[i] for i, _ in giveups], shown)
        i, m = giveups[0]
        assert (int(m.group(1)), int(m.group(2))) == (GIVEUP_COUNT, used), lines[i]
        g = len(shellrun.stats(lines[:i]))            # the index of the row the give-up fell into
        assert g <= GIVEUP_LAST_ROW, ("the give-up came %d rows after the kill's row (at most %d)" % (g, GIVEUP_LAST_ROW),
                                      shown)
        after = rows[g + 1:]
        assert all(r["acq_mean"] < 5.0 for r in after), [(r["frames"], r["acq_mean"]) for r in rows]
    assert len(after) >= 5, shown
    assert full_rate(after), shown
    return after


def wait_rows(run, start, n, timeout_s):
    """The stats rows logged from line `start` on, once there are n of them (stats_ms=500: one per 0.5 s of the host's
    clock); up to timeout_s, so a start-up slowed by load does not leave fewer rows before the host's end (0494)."""
    t_end = time.time() + timeout_s
    while True:
        rows = shellrun.stats(run.log_lines()[start:])
        if len(rows) >= n or time.time() >= t_end:
            return rows
        time.sleep(0.1)


def finish(run, seconds=40):
    code = run.host.wait(seconds)
    if code is None:
        run.host.kill()
    assert code == 0, ("host did not end normally (None = still running: hung)", code,
                       shellrun.tail(run.host_log, 6), shellrun.tail(run.composite_log, 8))


# ==============================================================================================================
# 3014 (V2): timeout_ms without a limit hung the presenting thread; a dead server cost every frame the timeout
# ==============================================================================================================

@pytest.mark.parametrize("value, used, note", [
    ("-1", 0, "is outside 0..50 ms - clamped to 0"),
    ("0", 0, None),
    ("4000000000", 50, "is outside 0..50 ms - clamped to 50"),
    ("abc", 0, "is not a number - 0 used"),
])
def test_timeout_ms_limited_and_killed_server(run, host_closer, value, used, note):
    """timeout_ms from the ini, a server that first asks for a resize (new texture generation, where -1 used to hang
    for good) and is then killed. The value is clamped and logged; the host keeps presenting at full rate after the
    kill (every 0.5 s stats row once the kill's own row is over above ROW_FLOOR frames, the median row above 50) and ends
    normally. Ticket 0494: the rows are awaited, not slept for, and the host lives until they are there (closed by
    host_closer; HOST_BACKSTOP_S on its own clock), after a hook commit awaited for up to HOOK_COMMIT_WAIT_S: under load
    the start-up took long enough that the 8-s host ended one row after the kill (0490's run, 2026-11-17), and a 9.7-s
    commit outlasted the 10-s wait for the config line (0494's series). Ticket 0729 (2026-11-25): the rows after the
    kill follow killed_server_rule: with the clamp at 50 they are counted from the composite's give-up, not from the
    kill's row, so a host frame of a few hundred ms during the 8 waits no longer fails the test (0507's run 2); and
    stale_ms=5000 (as test_timeouts_in_a_row_stop_the_wait), so the default 1-s STALE never ends the waits first: the
    give-up after 8 clamped timeouts is what stops them, and the test requires its line."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", timeout_ms=value, stale_ms="5000")
    run.start_server("--rate", "60", seconds=40)          # 1920x1080: the DLL asks for 1280x720, the server makes gen 2
    run.start_host(*PLAIN, seconds=HOST_BACKSTOP_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    cfg = run.wait_log(r"\bconfig: mode=", timeout_s=10)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    time.sleep(1.0)
    n0 = len(run.log_lines())
    run.server.kill()
    rows = wait_rows(run, n0, 9, timeout_s=10)           # the kill's own row and 8 more (4 s of the host's clock)
    lines = run.log_lines()[n0:]                         # 0729: read before the close, which may end a short row
    host_closer(run.host.pid)
    finish(run, 30)
    assert "timeout=%d " % used in cfg, cfg
    errs = [ln for ln in run.log_lines() if "CONFIG ERROR timeout_ms" in ln]
    if note:
        assert len(errs) == 1 and ("timeout_ms='%s' %s" % (value, note)) in errs[0], errs
    else:
        assert not errs, errs
    killed_server_rule(lines, used)
    assert not any(r["disabled"] for r in rows)


# Ticket 0729: killed_server_rule on recorded and made-up logs (no build, no process).
RUN2_LOG = Path(__file__).resolve().parents[1] / "evidence" / "0507" / "fail_run2" / "composite_rows.txt"


def _row(frames, ft_mean, acq_mean=0.0):
    return ("12:00:00.000 [1] stats el=500ms mode=composite ins=clear rule=after frames=%d ft_mean=%.3f acq_n=%d "
            "acq_mean=%.4f" % (frames, ft_mean, frames, acq_mean))


def _giveup(n=GIVEUP_COUNT, ms=50):
    return ("12:00:00.000 [1] AcquireSync timed out %d times in a row (timeout_ms=%d): frames no longer wait for the "
            "server until it delivers a frame again (logged once; later fallbacks are counted as wait_off)" % (n, ms))


KILL_ROW, FULL = _row(31, 18.1, 17.8), [_row(10000 + k, 0.05) for k in range(6)]


def test_killed_server_rule_replays_0507_run2():
    """0507's run 2 (2026-11-24 18:25, under the quiet lock): the composite log from the kill's row on (the kill came
    after the 18:25:05.426 row: the failure's rows begin with (31, 18.12)). The old rule, full_rate(rows[1:]), fails on
    it as it did then (the row after the kill's: 6 frames, ft_mean 98.553, one 279.3-ms frame); the new rule passes:
    the give-up (8 timeouts, timeout_ms=50) fell into rows[2], and rows[3:] are at full rate."""
    text = RUN2_LOG.read_text(encoding="utf-8").splitlines()
    n0 = next(k for k, ln in enumerate(text) if ln.startswith("18:25:05.426 ")) + 1
    lines = text[n0:]
    rows = shellrun.stats(lines)
    assert [(r["frames"], r["ft_mean"]) for r in rows[:3]] == [(31, 18.12), (6, 98.553), (8216, 0.061)]
    assert rows[1]["ft_max"] == 279.302 and rows[1]["comp_max"] == 63.9604
    assert not full_rate(rows[1:])                       # the old rule: the failure of 2026-11-24
    after = killed_server_rule(lines, 50)
    assert [r["frames"] for r in after[:2]] == [10218, 10538] and len(after) == len(rows) - 3


def test_killed_server_rule_cases():
    """The rule passes what the composite must do and fails each way it could go wrong."""
    slow = _row(8, 62.4, 62.3)                           # a 0.5-s row of frames that each wait the 50-ms clamp
    killed_server_rule([KILL_ROW, slow, _giveup(), _row(9000, 0.06, 61.5)] + FULL, 50)
    # a host frame of a few hundred ms during the waits: the give-up only reaches rows[3]
    killed_server_rule([KILL_ROW, slow, _row(2, 290.0, 62.0), _giveup(), _row(40, 12.0, 62.0)] + FULL, 50)
    killed_server_rule([KILL_ROW] + FULL, 0)
    bad = {
        "no give-up: every frame keeps waiting": ([KILL_ROW] + [slow] * 8, 50),
        "the give-up names another timeout": ([KILL_ROW, slow, _giveup(ms=4000), _row(9000, 0.06)] + FULL, 50),
        "the give-up after another count": ([KILL_ROW, slow, _giveup(n=9), _row(9000, 0.06)] + FULL, 50),
        "the give-up too late": ([KILL_ROW, slow, slow, slow, slow, _giveup(), _row(9000, 0.06)] + FULL, 50),
        "two give-up lines": ([KILL_ROW, slow, _giveup(), _row(9000, 0.06), _giveup()] + FULL, 50),
        "slow after the give-up": ([KILL_ROW, slow, _giveup(), _row(9000, 0.06)] + [slow] * 6, 50),
        "frames still wait after the give-up": ([KILL_ROW, slow, _giveup(), _row(9000, 0.06)] + [_row(9000, 0.06, 30.0)] * 6, 50),
        "too few rows after the give-up": ([KILL_ROW, slow, _giveup(), _row(9000, 0.06)] + FULL[:4], 50),
        "timeout 0 but something timed out": ([KILL_ROW, _giveup(ms=0)] + FULL, 0),
        "timeout 0 but a slow row after the kill's": ([KILL_ROW, slow] + FULL, 0),
    }
    for why, (lines, used) in bad.items():
        with pytest.raises(AssertionError):
            killed_server_rule(lines, used)
            pytest.fail("passed: " + why)


_nt = ctypes.WinDLL("ntdll")
_nt.NtSuspendProcess.argtypes = [wintypes.HANDLE]
_nt.NtResumeProcess.argtypes = [wintypes.HANDLE]


def test_timeouts_in_a_row_stop_the_wait(run, host_closer):
    """timeout_ms=50 and the server frozen (NtSuspendProcess on our own test server; heartbeat not yet stale with
    stale_ms=5000): after 8 timeouts in a row the DLL stops waiting, logs it once and counts it (wait_off); the frame
    rate stays up; after the resume the server's frames are taken again."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", timeout_ms="50", stale_ms="5000")
    run.start_server("--rate", "60", "--size", "1280x720", seconds=40)
    run.start_host(*PLAIN, seconds=HOST_BACKSTOP_S)   # 0494: closed below, after the resume's srv_new row
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    time.sleep(0.5)
    n0 = len(run.log_lines())
    _nt.NtSuspendProcess(run.server.h)
    try:
        line = run.wait_log(r"AcquireSync timed out 8 times in a row \(timeout_ms=50\)", timeout_s=5, start=n0)
        time.sleep(2.0)
        rows = shellrun.stats(run.log_lines()[n0:])
    finally:
        _nt.NtResumeProcess(run.server.h)
    n1 = len(run.log_lines())
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=5, start=n1)
    host_closer(run.host.pid)
    finish(run, 20)
    assert "frames no longer wait for the server until it delivers a frame again" in line
    assert sum(r["wait_off"] for r in rows) >= 1
    # no frame waits after the give-up: the acquire takes well under the 50-ms timeout on average in every later row
    # (0494: was "timeouts <= 9", but when the suspension lands between the server's frameCounter++ and its ReleaseSync,
    # every later frame tries AcquireSync(1, 0) and counts its immediate WAIT_TIMEOUT: 3,863 per row, acq_mean 0.05 ms)
    # ... counted from the row after the one in which the 8th 50-ms timeout fell (the eight can straddle two rows)
    k, n = 0, 0
    while k < len(rows) and n < 8:
        n += rows[k]["timeouts"]
        k += 1
    after = rows[k:]
    assert n >= 8 and len(after) >= 2, [(r["timeouts"], r["acq_mean"]) for r in rows]
    assert all(r["acq_mean"] < 5.0 for r in after), [(r["timeouts"], r["acq_mean"]) for r in rows]
    assert full_rate(after), [r["frames"] for r in rows]
    assert len([ln for ln in run.log_lines() if "times in a row" in ln]) == 1


# ==============================================================================================================
# 3015 (V1): a NaN in the EXE's camera was published as OK
# ==============================================================================================================

# test_host's line, after the log's time stamp: "fake EXE: <build label> image at 0x<8 hex> (...". The label is there
# since 4964 (4a818131d, on main with 7cb3e57f8, 2026-11-20). Before it the line was "fake EXE: image at 0x...". Ticket 0627:
# for that reason the old pattern matched no line, and the 9 nonfinite cases were red. The pattern allows the label and
# nothing else: the line's message starts with "fake EXE:" and the capture is the hex after "image at 0x".
FAKE_BASE_RX = r"^(?:\S+ )?fake EXE: (?:.*? )?image at 0x([0-9A-F]{8})\b"


def fake_base_of(ln):
    m = re.search(FAKE_BASE_RX, ln)
    return int(m.group(1), 16) if m else None


def fake_base(run):
    return fake_base_of(run.wait_log(FAKE_BASE_RX, which="host", timeout_s=10))


def test_fake_base_line_pattern():
    """Ticket 0627: the pattern takes test_host's line in both forms, and no other line."""
    assert fake_base_of("22:47:59.461 fake EXE: Steam build 4390913 (2014-11-19 EXE, with CEG) image at 0x02EE0000 "
                        "(43855824 bytes reserved), camera target (0.0, 0.0)") == 0x02EE0000
    assert fake_base_of("22:47:59.461 fake EXE: image at 0x10150000, bad=none") == 0x10150000
    for ln in ("22:47:59.461 fake EXE: writer thread wrote the camera 12 times",
               "22:47:59.461 fake EXE: a camera pattern does not expand for Steam build 26577625",
               "22:47:59.461 camera: EXE image at 0x02EE0000 (VPSHELL_EXE_BASE) byte patterns match",
               "22:47:59.461 composite: the fake EXE: image at 0x02EE0000",
               "22:47:59.461 fake EXE: Steam build 4390913 image at 0x2EE0000 (7 hex digits)"):
        assert fake_base_of(ln) is None, ln


def fa(base, va):
    return base + va - VA_BASE


OFF = camblock.offsets()


def read_block(sec):
    raw = sec.read(camblock.CAMERA_OFFSET, OFF["_end"])
    d = {}
    for name, fmt in camblock.FIELDS:
        vals = struct.unpack_from("<" + fmt, raw, OFF[name])
        d[name] = vals if len(vals) > 1 else vals[0]
    return d


def finite_ok(d):
    """The test's own check of a block: every matrix the server draws with is finite."""
    nv = max(1, min(int(d["numViews"]), 4))
    return all(math.isfinite(x) for x in list(d["proj"]) + list(d["camWorld"]) + list(d["view"][:16 * nv]))


def sample_blocks(run, seconds, every=0.002):
    sec = camblock.Section(run.section)
    st = {"n": 0, "by_status": {}, "ok_nonfinite": 0}
    t_end = time.time() + seconds
    try:
        while time.time() < t_end:
            s1 = struct.unpack("<I", sec.read(camblock.CAMERA_OFFSET, 4))[0]
            if s1 & 1 or s1 == 0:
                continue
            d = read_block(sec)
            if d["seq"] != s1:
                continue
            name = camblock.STATUS[d["status"]] if d["status"] < len(camblock.STATUS) else str(d["status"])
            st["n"] += 1
            st["by_status"][name] = st["by_status"].get(name, 0) + 1
            if name == "OK" and not finite_ok(d):
                st["ok_nonfinite"] += 1
            time.sleep(every)
    finally:
        sec.close()
    return st


CAM_CASES = {
    "nan_view0_translation": ("view", 12 * 4, f32(NAN)),
    "nan_view0_all": ("view", 0, f32(*[NAN] * 16)),
    "inf_view0_translation": ("view", 12 * 4, f32(INF)),
    "nan_wrap_view1": ("view", 64 + 12 * 4, f32(NAN)),
    "inf_wrap_view1": ("view", 64 + 0, f32(-INF)),
    "nan_proj_all": ("proj", 0, f32(*[NAN] * 16)),
    "inf_proj0": ("proj", 0, f32(INF)),
    "nan_camworld": ("camworld", 12 * 4, f32(NAN)),
    "inf_camworld": ("camworld", 0, f32(INF)),
}


@pytest.mark.parametrize("case", list(CAM_CASES))
def test_nonfinite_camera_never_published_ok(run, case):
    """A NaN or infinity in g_View[0], a wrap view, g_Proj or the GVS camWorld (written into the fake EXE image the way
    a broken EXE state would hold it): the DLL publishes INCONSISTENT, never OK, and the server's grid is not OK."""
    which, off, data = CAM_CASES[case]
    wrap = "wrap" in case
    host = ["--fake-cam-views", "2", "--fake-cam-x", "1700"] if wrap else []
    if which != "camworld":
        host += ["--fake-cam-freeze-at", "1.0"]          # the fake writer leaves the view globals alone from 1 s
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--scene", "grid", "--rate", "60", seconds=40)
    run.start_host(*FAKE, *host, seconds=9)
    base = fake_base(run)
    run.wait_log(r"camera: first OK read", timeout_s=10)
    time.sleep(1.5)
    mem = Mem(run.host.pid)
    stop = threading.Event()
    try:
        if which == "camworld":
            # camWorld keeps being written by the fake EXE every frame: write the bad value continuously (a racing
            # writer), so that a share of the reads sees it
            app = struct.unpack("<I", mem.read(fa(base, VA_APP), 4))[0]
            gvs = struct.unpack("<I", mem.read(app + 0xC0, 4))[0]

            def hammer():
                while not stop.is_set():
                    mem.write(gvs + 0x78 + off, data)

            t = threading.Thread(target=hammer, daemon=True)
            t.start()
            st = sample_blocks(run, 3.0)
            stop.set()
            t.join(5)
        else:
            mem.write(fa(base, VA_VIEW if which == "view" else VA_PROJ) + off, data)
            time.sleep(0.3)
            st = sample_blocks(run, 2.5)
    finally:
        stop.set()
        mem.close()
    srv_states = [ln.split("grid: state ", 1)[1] for ln in run.log_lines("server") if "grid: state " in ln]
    finish(run)
    assert st["n"] > 50, st
    assert st["ok_nonfinite"] == 0, st
    assert st["by_status"].get("INCONSISTENT", 0) > 0, st
    if which != "camworld":
        assert "OK" not in st["by_status"], st
        assert srv_states and not srv_states[-1].split("->")[1].strip().startswith("OK"), srv_states[-4:]
    assert not any("EXCEPTION" in ln for ln in run.log_lines())


# ==============================================================================================================
# 3016 (V3): a tag rule that is empty after parsing, or not convertible, matched every list
# ==============================================================================================================

TAG_CASES = {
    "match_parses_to_nothing": (dict(tag_match=" , "), "tag_match: no token after parsing", "miss_nomatch"),
    "match_not_ascii": (dict(tag_match="clear_bb€"), "tag_match: character U+", "miss_nomatch"),
    "anchor_parses_to_nothing": (dict(tag_rule="before", tag_anchor=" + "), "tag_anchor: no token after parsing", "miss_noanchor"),
}


@pytest.mark.parametrize("case", list(TAG_CASES))
def test_unusable_tag_rule_matches_nothing(run, case):
    """The game-like stand-in with a tag rule that is present but unusable. The rule matches nothing: no insertion at
    all (so none outside the clear..3D window, ins_wrong 0), every frame a counted miss with the expected reason,
    nothing composited, one CONFIG ERROR line naming the cause. The ini is written as UTF-8, as an editor saves it."""
    over, said, reason = TAG_CASES[case]
    cfg = dict(mode="composite", insert="tagged", tag_rule="after", trace_frames="0", stats_ms="500")
    cfg.update(over)
    run.set_ini(**cfg)   # set_ini writes UTF-8 (the euro sign becomes E2 82 AC; the DLL reads the ini as ANSI)
    run.start_server("--rate", "60", seconds=30)
    run.start_host(*GAME, seconds=6)
    run.wait_log(r" stats .*frames=[1-9]", timeout_s=15)
    finish(run)
    lines = run.log_lines()
    rows = shellrun.stats(lines)[1:]
    errs = [ln for ln in lines if "CONFIG ERROR tag_" in ln]
    frames = sum(r["frames"] for r in rows)
    assert frames > 100
    sums = {k: sum(r[k] for r in rows) for k in ("ins_checked", "ins_wrong", "comp_n", "missed_ins", reason)}
    assert sums["ins_wrong"] == 0 and sums["comp_n"] == 0, (frames, sums)
    assert sums["missed_ins"] == frames and sums[reason] == frames, (frames, sums)
    assert len(errs) == 1 and said in errs[0], errs


# ==============================================================================================================
# 3017 (V4): any swap chain created through the factory took the compositing target; the log flooded
# ==============================================================================================================

@pytest.mark.parametrize("extra", [1, 10])
def test_other_windows_swap_chains_do_not_take_the_target(run, extra):
    """The host's main chain presents; at 2 s it creates `extra` more windows with swap chains through the hooked
    factory, all presenting every frame. The main chain stays the target (one adoption only, the composite keeps
    drawing into the 1280x720 back buffer), the candidate chain is refused with a reason, 8 other chains are named once
    each and the rest counted in one line, and the log stays small."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--rate", "60", seconds=40)
    run.start_host(*PLAIN, "--extra-chains", str(extra), seconds=7)
    run.wait_log(r"extra chain %d: sc " % (extra - 1), which="host", timeout_s=10)
    time.sleep(2.0)
    shot = run.shot()
    finish(run)
    lines = run.log_lines()
    adopted = [ln for ln in lines if "adopted swap chain" in ln]
    refused = [ln for ln in lines if "NOT adopted, the target stays" in ln]
    other = [ln for ln in lines if "another swap chain presents" in ln]
    beyond = [ln for ln in lines if "beyond the 8 listed" in ln]
    assert len(adopted) == 1 and "1280x720" in adopted[0], adopted
    # only the newest chain created before a Present is an adoption candidate (g_pendingAdopt): one refusal line
    assert len(refused) == 1, refused
    assert len(other) == min(extra, 8), len(other)
    assert len(beyond) == (1 if extra > 8 else 0), beyond
    assert len(lines) < 400 and run.composite_log.stat().st_size < 200_000, (len(lines), run.composite_log.stat().st_size)
    img = shellrun.Image(shot)
    assert (img.w, img.h) == (1280, 720)
    assert shellrun.fraction(img.region(*FREE), shellrun.is_server) > 0.9
    last = [ln for ln in lines if " stats " in ln][-1]
    assert re.search(r"\bbb=1280x720\b", last) and re.search(r"\bsrv_new=[1-9]", last), last


def test_new_chain_on_the_same_window_is_adopted(run):
    """The game's resolution change: a new swap chain on the same window. It is adopted at its first Present."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--rate", "60", seconds=30)
    run.start_host(*PLAIN, "--recreate-at", "3", "--recreate-same", "1", seconds=7)
    run.wait_log(r"recreate: new window", which="host", timeout_s=10)
    line = run.wait_log(r"adopted swap chain .*same window as the old target", timeout_s=5)
    finish(run)
    lines = run.log_lines()
    assert "NOT adopted" not in "\n".join(lines)
    rows = shellrun.stats(lines[lines.index(line) + 1:])
    assert rows and sum(r["srv_new"] for r in rows) > 0


def test_new_chain_at_the_old_chains_address_is_adopted(run):
    """Issue 0141 (negative case of the test above): the new swap chain on the same window gets the released old
    chain's address (heap reuse; the host forces it with --recreate-same-address). By address alone the composite took
    it for its current target and logged no adoption (the flake of test_new_chain_on_the_same_window_is_adopted); with
    the creation number as the second key it is a new chain, adopted at its first Present with a fresh back buffer, and
    the composite keeps drawing the server's frame into it."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--rate", "60", seconds=30)
    # Under ASan (build.cmd asan) a freed block waits in the allocator's quarantine and is not handed out again, so the
    # old address never comes back (2026-11-13: 100 tries); the host runs with the quarantine off. Plain builds ignore it.
    asan = os.environ.get("ASAN_OPTIONS", "")
    run.start_host(*PLAIN, "--recreate-at", "3", "--recreate-same", "1", "--recreate-same-address", "40", seconds=10,
                   env={"ASAN_OPTIONS": (asan + ":" if asan else "") + "quarantine_size_mb=0"})
    # A round that does not get the old address back ends with a new chain at another address (adopted as usual) and
    # the host tries again from it 0.5 s later, up to 8 rounds.
    forced = run.wait_log(r"recreate: old address (reused after|NOT reused in .*no more rounds)", which="host", timeout_s=20)
    assert "reused after" in forced, forced   # the host could not force the case: the test proves nothing
    print("host:", forced)
    old = re.search(r"\((0x)?([0-9A-Fa-f]+)\)$", forced).group(2).upper().lstrip("0")
    # The behaviour first (this is what fails on the unfixed composite): a second adoption at the old address, the new
    # chain's, at its first Present. By address alone the composite took the new chain for its target: no adoption.
    rx_old = re.compile(r"adopted swap chain 0*%s " % old)
    t_end, hits = time.time() + 5, []
    while time.time() < t_end and len(hits) < 2:
        hits = [ln for ln in run.log_lines() if rx_old.search(ln)]
        time.sleep(0.1)
    assert len(hits) == 2, ("the new chain at the old address was not adopted", old, hits)
    line = hits[1]
    assert "same window as the old target" in line, line
    reuse = run.wait_log(r"has the address of the target .*which was released", timeout_s=5)
    time.sleep(1.5)
    shot = run.shot()
    finish(run)
    lines = run.log_lines()
    assert sum("has the address of the target" in ln for ln in lines) == 1, [ln for ln in lines if "address of the target" in ln]
    assert "NOT adopted" not in "\n".join(lines)
    i_reuse, i_new = lines.index(reuse), lines.index(line)
    before = [ln for ln in lines[:i_reuse] if "adopted swap chain" in ln]
    assert before, lines[:i_reuse][-20:]
    a_old = re.search(r"adopted swap chain (\w+) creation #(\d+)", before[-1])
    a_new = re.search(r"adopted swap chain (\w+) creation #(\d+)", line)
    assert a_old and a_new, (before[-1], line)
    # the same address, two chains: the new one has a higher creation number (0: a chain made before the hooks)
    assert a_old.group(1).upper().lstrip("0") == a_new.group(1).upper().lstrip("0") == old, (old, before[-1], line)
    assert int(a_new.group(2)) > int(a_old.group(2)) >= 0, (before[-1], line)
    assert i_reuse < i_new and not [ln for ln in lines[i_reuse:i_new] if "adopted swap chain" in ln], (i_reuse, i_new)
    after = lines[i_new + 1:]
    assert any(" back buffer resource " in ln for ln in after[:3]), after[:3]
    rows = shellrun.stats(after)
    assert rows and sum(r["srv_new"] for r in rows) > 0 and sum(r["comp_n"] for r in rows) > 0, rows
    img = shellrun.Image(shot)
    assert shellrun.fraction(img.region(*FREE), shellrun.is_server) > 0.9


# ==============================================================================================================
# 3018 V5: a server texture of another format at the same size
# ==============================================================================================================

def test_texture_format_change_at_same_size(run):
    """Server A (BGRA8, the test scene) at the host's size, killed; server B (RGBA8, the grid scene with no camera: a
    transparent frame) at the same size. The DLL recreates its local texture for the new format: the test scene's last
    frame is no longer drawn (CopyResource between the old local texture and B's would copy nothing)."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_host(*PLAIN, seconds=10)
    run.start_server("--rate", "60", "--size", "1280x720", seconds=40)
    run.wait_log(r"opened shared texture .* 1280x720 fmt 87 ", timeout_s=10)
    time.sleep(1.0)
    before = shellrun.fraction(shellrun.Image(run.shot()).region(*FREE), shellrun.is_server)
    run.server.kill()
    run.server.close()
    run.start_server("--rate", "60", "--size", "1280x720", "--scene", "grid", "--tex-format", "rgba", seconds=40)
    run.wait_log(r"opened shared texture .* 1280x720 fmt 28 ", timeout_s=10)
    time.sleep(1.0)
    after = shellrun.fraction(shellrun.Image(run.shot()).region(*FREE), shellrun.is_server)
    finish(run)
    assert before > 0.9, before
    assert after < 0.05, after
    assert any("shared texture format changed 87 -> 28 at the same size 1280x720" in ln for ln in run.log_lines())


# ==============================================================================================================
# 3018 V7: the live tools take the instance as an argument (no hard-coded c5h-b)
# ==============================================================================================================

TOOLS = shellrun.SHELL / "tools"


@pytest.mark.parametrize("tool", ["darkred.py", "live3008.py", "stock_acc.py", "mapchange.py", "turns_soak.py", "i2_capture.py"])
def test_live_tools_name_the_instance(tool):
    """Each tool offers --instance, and its source names no instance."""
    r = subprocess.run([sys.executable, str(TOOLS / tool), "-h"], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0 and "--instance" in r.stdout, (r.returncode, r.stdout[-400:], r.stderr[-400:])
    code = "\n".join(ln for ln in (TOOLS / tool).read_text(encoding="utf-8").splitlines() if not ln.lstrip().startswith("#"))
    assert not re.search(r"[\"'/\\]c5h-b[\"'/\\]", code), tool


@pytest.mark.parametrize("tool, args", [
    ("live3008.py", ["--live", "x", "stats"]),
    ("stock_acc.py", ["--live", "x", "zoomto", "--plot", "1", "1", "--zoom", "11"]),
    ("mapchange.py", ["exit"]),
    ("turns_soak.py", ["turns", "--live", "x", "--out", "y"]),
    ("i2_capture.py", ["--arm", "stock", "--game", "huge", "--out", "y"]),
])
def test_live_tools_refuse_without_instance(tool, args):
    """Without --instance the tool stops at its command line (exit 2) before it touches any instance."""
    r = subprocess.run([sys.executable, str(TOOLS / tool), *args], capture_output=True, text=True, timeout=60)
    assert r.returncode == 2 and "instance" in r.stderr, (r.returncode, r.stderr[-400:])


def test_darkred_refuses_without_instance():
    r = subprocess.run([sys.executable, str(TOOLS / "darkred.py"), "config", "--arm", "stock"], capture_output=True,
                       text=True, timeout=60)
    assert r.returncode == 1 and "no instance: give --instance NAME" in r.stderr, (r.returncode, r.stderr[-400:])
