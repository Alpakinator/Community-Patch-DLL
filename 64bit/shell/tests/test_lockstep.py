"""Issue 3304 (epic 3300): lockstep, the hand-over behind vpframe::FrameSource and the depth write of shell_composite.dll,
on the stand-in. Every process runs on the session's hidden desktop (rule 14, shellrun.launch).

- The publish point: test_host.exe --fake-exe --fake-uv lays out UpdateViews' call site, its import thunk and import
  slot in the fake image exactly as the composite's hook checks them (lockstep-probe.md 1.1) and calls it once per frame
  after writing the camera, before the frame's lists: the hook on d3dx9_42!D3DXMatrixInverse fires there, as in the game.
- The server: lockstep_server.exe (shell/tests/lockstep_server.cpp), a version-3 server whose picture names the camera it
  was drawn with (colour = frame id F, depth = --depth-value inside --region); slow, stalled and killed on request.
- handover=ring: shell/frame's ring_producer.exe (C5_FRAME_BUILD=<shell\\frame\\build.cmd output>), issue 3302.
Each negative case checks the composite's outputs (the per-frame rows of shell_composite.lockstep.csv, the "lockstep 1s:"
and CONFIG ERROR lines) and the exit codes after the failure. Skipped counts as failed (conftest.py).
"""
import csv
import ctypes
import importlib.util
import os
import re
import shutil
import struct
import threading
import time
from ctypes import wintypes
from pathlib import Path

import pytest

import camblock
import shellrun
import desktops  # noqa: E402  tools/harness/tests/desktops.py, put on the path by shellrun (ticket 0691)

W, H = 1280, 720
HOST = ("--game", "--fake-exe", "--fake-uv", "--vsync", "0", "--occluded-sleep", "0", "--load", "after",
        "--uv-work-us", "1500", "--size", "%dx%d" % (W, H))
INI = dict(mode="composite", insert="tagged", tag_rule="after", tag_match="clear_bb", blend="alpha", trace_frames="0",
           stats_ms="500", lockstep="1", lockstep_csv="1", lockstep_mode="bounded")
# Ticket 0691 (D242, 2026-11-23): the shipped lockstep_mode is exact (no bound, no give-up). The cases written for the
# bound and the give-up (3304, 0150, 0494, 0675) keep testing them with lockstep_mode=bounded (design point 7 g); the
# exact cases below set INI_EXACT.
INI_EXACT = dict(INI, lockstep_mode="exact")
# Ticket 0675 (2026-11-22): the shipped bound became 8000 us. The slow-server and late-insertion cases need a server
# (8 ms per frame) and an insertion (5 ms after UpdateViews) later than the bound, so they keep the old 3400 us bound.
INI_BOUND_3400 = dict(INI, lockstep_bound_us="3400")
EXACT, OLDER, NONE, NOPUB, NOINS = range(5)
HELD = 5   # ticket 0691 (MC #11169): a frame the server held for EXE inputs (not waited for, no server world)
# Ticket 0490 (MC): the taps that read the real EXE refuse the stand-in's fake EXE and log "DISABLED", which finish()
# refuses; since water_tap became 1 by default (3608, 2026-11-14 03:27) and decal, overlay, shadow, ice and cut followed,
# 28 of these 36 tests failed on main. The stand-in runs with them off; finish() still refuses any other DISABLED line.
EXE_TAPS_OFF = dict(water_tap="0", cut_tap="0", decal_tap="0", overlay_tap="0", shadow_tap="0", ice_tap="0",
                    model_fow="0", forest_tap="0", bake_tap="0", tap="0")


# Ticket 0494 (2026-11-17): test_host counts --seconds from its own start, and the composite's hook commit (inside the
# host's first frames after the load) took 9.07 s under load (6.5 s on 2026-11-12, 9038 V1; normally well under 1 s):
# a 10-s host then quit 4 s after the commit, before the lockstep CSV was written. So LsRun's host lives `seconds`
# after the commit line (and, with lockstep on, after the UpdateViews hook's own enable, a second commit): it starts with
# 2 x HOOK_COMMIT_WAIT_S more as a backstop and is closed by WM_CLOSE (its Esc path, exit 0; conftest.py's host_closer)
# `seconds` after them.
HOOK_COMMIT_WAIT_S = 18
# ... and the servers (lockstep_server, ring_producer, shell_server) live SERVER_S on their own clocks, ended by the
# fixture after the test: a ring_producer of 25 s ran out while an 18-s host freeze under load lasted (0494's series9)
SERVER_S = 120


class LsRun(shellrun.Run):
    """shellrun.Run with lockstep_server.exe (or ring_producer.exe) in place of shell_server.exe."""

    closer = None
    closed = None   # windows the closer posted WM_CLOSE to (0 when the host had none on the desktop)
    close_fn = None  # conftest.py's host_closer
    shooting = False

    def start_host(self, *extra, seconds=8, env=None):
        """The host lives `seconds` after the composite's hook commit (see HOOK_COMMIT_WAIT_S)."""
        host = super().start_host(*extra, seconds=2 * HOOK_COMMIT_WAIT_S + seconds, env=env)
        self.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
        # the UpdateViews hook is enabled by a second commit, which froze the host for 5.4 s under load (0494 series6
        # run 6: 122 frames in the host's 5 s, no CSV yet): with lockstep on, its outcome is awaited too (not asserted)
        if re.search(r"(?m)^\s*lockstep\s*=\s*1\s*$", self.ini.read_text(errors="replace")):
            try:
                self.wait_log(r"lsprobe: (hook ENABLED|hook DISABLED|REFUSED|MH_CreateHook)", timeout_s=HOOK_COMMIT_WAIT_S)
            except TimeoutError:
                pass
        self.closer = threading.Timer(seconds, self.close_host)
        self.closer.daemon = True
        self.closer.start()
        return host

    def close_host(self):
        """WM_CLOSE to the host's window now (the closer's timer does it `seconds` after the commit). A shot still
        waited for defers it (0494: under load the composite's control thread took the shot trigger more than 2.4 s
        late, and the host ended first)."""
        host = self.host
        if self.shooting:
            self.closer = threading.Timer(0.5, self.close_host)
            self.closer.daemon = True
            self.closer.start()
            return
        if host and host.h and host.alive():
            self.closed = self.close_fn(host.pid)

    def shot(self, timeout_s=15.0):
        """shellrun.Run.shot with the host kept alive until the dump is there (up to 15 s, the host's backstop 18 s)."""
        self.shooting = True
        try:
            return super().shot(timeout_s)
        finally:
            self.shooting = False

    def stop(self):
        if self.closer:
            self.closer.cancel()
        super().stop()

    def start_lockstep_server(self, *extra, seconds=30, wait_ready=True):
        args = [str(shellrun.bin_dir() / "lockstep_server.exe"), "--section", self.section, "--name", self.texname,
                "--seconds", str(seconds), "--log", str(self.server_log), *[str(a) for a in extra]]
        self.server = shellrun.launch(args, self.desktop, console=True)
        t_end = time.time() + 15
        while wait_ready:
            if self.server_log.exists() and "texture generation" in self.server_log.read_text(errors="replace"):
                break
            assert self.server.alive() and time.time() < t_end, shellrun.tail(self.server_log, 8)
            time.sleep(0.05)
        return self.server

    def rows(self):
        p = self.dir / "shell_composite.lockstep.csv"
        assert p.exists(), "no shell_composite.lockstep.csv; composite log: " + shellrun.tail(self.composite_log, 12)
        lines = [ln for ln in p.read_text().splitlines() if not ln.startswith("#")]
        out = []
        for r in csv.DictReader(lines):
            if None in r.values() or "" in r.values():
                continue   # the last row, cut off when the host process ended while the control thread was writing it
            out.append({k: (float(v) if "." in v else int(v)) for k, v in r.items()})
        return out

    def ls_lines(self):
        rows = []
        for ln in self.log_lines():
            if "lockstep 1s:" not in ln:
                continue
            d = {}
            for k, v in re.findall(r"(\w+)=(\S+)", ln.split("lockstep 1s:", 1)[1]):
                try:
                    d[k] = float(v)
                except ValueError:
                    d[k] = v
            rows.append(d)
        return rows

    def finish(self, timeout=40):
        code = self.host.wait(timeout)
        assert code == 0, (code, shellrun.tail(self.host_log, 10), shellrun.tail(self.composite_log, 12))
        time.sleep(0.8)   # the control thread flushes the rows every 500 ms
        lines = self.log_lines()
        assert not any("DISABLED" in ln or "EXCEPTION" in ln for ln in lines), [ln for ln in lines if "DISABLED" in ln or "EXCEPTION" in ln][:5]
        return lines


@pytest.fixture
def lrun(tmp_path, hidden_desktop, built, request, host_closer):
    if not (built / "lockstep_server.exe").exists():
        pytest.fail("lockstep_server.exe missing in %s: rebuild with shell\\build.cmd" % built)
    r = LsRun(tmp_path / "run", hidden_desktop, request.node.name[:24].replace("[", "_").replace("]", ""))
    r.close_fn = host_closer
    # issue 3312: the shipped ini is on ring / lockstep / depth now; lockstep_server.exe is a version-3 server, so these
    # tests start from the version-3 keys (each test sets lockstep, handover=ring or depth_write itself where it wants them)
    r.set_ini(handover="v3", lockstep="0", depth_write="0", **EXE_TAPS_OFF)   # issue 3313: tap=0; ticket 0490: the EXE taps off
    yield r
    r.stop()


def _ini_keys(path):
    keys = {}
    for ln in Path(path).read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$", ln)
        if m:
            keys[m.group(1)] = m.group(2)
    return keys


def test_shipped_ini_is_the_product_path():
    """Issue 3312: shell/composite/shell_composite.ini ships the product path's hand-over (the proxy writes
    render_handover=ring for server.exe): the ring, lockstep and the depth write, each key exactly once."""
    p = shellrun.SHELL / "composite" / "shell_composite.ini"
    k = _ini_keys(p)
    assert (k.get("handover"), k.get("lockstep"), k.get("depth_write")) == ("ring", "1", "1"), k
    text = p.read_text(encoding="utf-8")
    for key in ("handover", "lockstep", "depth_write"):
        assert len(re.findall(r"(?m)^\s*%s\s*=" % key, text)) == 1, key
    assert k.get("insert") == "tagged" and k.get("tag_rule") == "after", k   # the insertion the three need


def test_shipped_ini_has_the_height_field_tap_on():
    """Issue 3313: the product path needs the height-field tap (server.exe's terrain is drawn from it; 3308's and 3311's
    product runs set tap=1 by hand): the shipped ini says tap=1, exactly once."""
    p = shellrun.SHELL / "composite" / "shell_composite.ini"
    assert _ini_keys(p).get("tap") == "1"
    assert len(re.findall(r"(?m)^\s*tap\s*=", p.read_text(encoding="utf-8"))) == 1


def test_fixture_starts_from_the_version3_keys(lrun):
    """Issue 3312: the version-3 tests of this file set the old keys themselves (the shipped ini says ring); issue 3313:
    and tap=0 (the shipped ini says tap=1)."""
    k = _ini_keys(lrun.ini)
    assert (k["handover"], k["lockstep"], k["depth_write"], k["tap"]) == ("v3", "0", "0", "0"), k


def test_run_fixture_starts_from_tap_off(run):
    """Issue 3313: the shell tests' common fixture starts from tap=0 (the old default) over the shipped tap=1."""
    k = _ini_keys(run.ini)
    assert (k["tap"], k["handover"]) == ("0", "v3"), k


def qpc_freq(run):
    head = (run.dir / "shell_composite.lockstep.csv").read_text().splitlines()[0]
    return int(re.search(r"qpc_freq=(\d+)", head).group(1))


def uv_rows(rows, skip=60):
    """Rows of frames whose camera was published at UpdateViews, after the first `skip` of them (start-up)."""
    r = [x for x in rows if x["pub_src"] == 1]
    return r[skip:]


# Ticket 0494 (2026-11-17): what the wall-clock asserts of this file check on a shared machine. The composite's wait
# (LkBoundedWait) ends at its deadline: timer slices of at most 0.25 ms, then a spin over the last 0.5 ms, so a wait ends
# within LATE_MS (0.5 ms, the contract) of it. Measured on the stand-in, 2026-11-17 (0494's series beside 5 to 10 heavy
# jobs at 57-91 % CPU) [L]: (1) where every wait runs to its deadline (killed server) about half the waits end 0.37-0.50 ms
# past it, the last timer slice overshooting into the spin window [cause inferred]: the contract has no margin there, so a
# wait counts as late past LATE_MS + LATE_SLACK_MS; (2) load makes waits late because the render thread does not run
# after the spin's last clock read, in episodes: up to 30 of 248 waits of one test late (12 %), the largest 118.9 ms
# (9 heavy jobs, CPU up to 100 %). The old checks (1 % past 0.5 ms, none past 2 ms) failed by load alone, and so did
# 10 % / 25 ms and 10 % / 100 ms in this series. A defect makes waits late as a rule: a wait that ignores or misplaces
# its deadline is late in every wait of the slow-server and late-insertion cases (by about 4.6 ms: the server's 8 ms less
# the 3.4-ms bound). So the check is that at most half of a test's waits are late (LATE_SHARE; LATE_FLOOR for short runs)
# and none by a second (LATE_FAR_MS, a stuck wait). Not caught on a shared machine: a defect that makes only some waits
# late (GetData in every spin, 2026-11-13: 14 of 183 waits up to 49 ms) looks like load; a claim about the wait's tail
# needs a quiet machine (COMMON 23).
LATE_MS = 0.5
LATE_SLACK_MS = 0.5
LATE_SHARE, LATE_FLOOR = 0.5, 3
LATE_FAR_MS = 1000.0


def timing_record(what, values_ms, **extra):
    """C5_SHELL_TIMING_LOG=<file>: one JSON line per check (the test, its count, the share past LATE_MS, the largest
    values), so a series under load can show the margins the checks keep (ticket 0494)."""
    p = os.environ.get("C5_SHELL_TIMING_LOG")
    if not p:
        return
    import json
    v = sorted(values_ms)
    rec = dict(test=os.environ.get("PYTEST_CURRENT_TEST", "").split(" ")[0], what=what, n=len(v),
               over=sum(x > LATE_MS for x in v), late=sum(x > LATE_MS + LATE_SLACK_MS for x in v),
               top=[round(x, 4) for x in v[-5:]], **extra)
    with open(p, "a", encoding="utf-8") as f:
        f.write(json.dumps(rec) + "\n")


def check_late(late_ms, what):
    """late_ms: how far past its limit each wait ended (ms). The share past LATE_MS + LATE_SLACK_MS and the far bound."""
    timing_record(what, late_ms)
    over = [x for x in late_ms if x > LATE_MS + LATE_SLACK_MS]
    assert len(over) <= max(LATE_FLOOR, LATE_SHARE * len(late_ms)), (what, len(over), len(late_ms), sorted(late_ms)[-5:])
    assert not any(x > LATE_FAR_MS for x in late_ms), (what, sorted(late_ms)[-5:])


def check_bound(rows, freq):
    """The contract: a wait never ends later than its deadline + 0.5 ms; on a shared machine checked as check_late does."""
    waits = [r for r in rows if r["wait0_qpc"]]
    check_late([(r["wait1_qpc"] - r["deadline_qpc"]) * 1000.0 / freq for r in waits], "wait end - deadline")
    return waits


def check_misses_justified(rows, share=0.05, unjustified=0.01):
    """The rule, independent of machine load: a lockstep frame takes its own frame unless the composite waited for it
    up to the deadline, or had given up (wait_off). The share of exact frames depends on the load (other teams' games
    and builds share the machine): only a floor is asserted, against a composite that waits but never takes its own
    frame (near 0). 0494: 0.05 (was 0.8); under load the share fell to 0.43, and to 0.198 at 99 % CPU (13 heavy jobs)
    (test_frames_without_updateviews_do_not_wait; ring cpu), every miss justified."""
    exact = [x for x in rows if x["kind"] == EXACT]
    assert all(x["taken_f"] == x["pub_f"] for x in exact)
    bad = [x for x in rows if x["kind"] != EXACT and not x["wait_off"] and not (x["wait0_qpc"] and x["wait1_qpc"] >= x["deadline_qpc"])]
    timing_record("exact share", [], rows=len(rows), exact=len(exact), unjustified=len(bad), floor=share)
    assert len(bad) <= max(2, unjustified * len(rows)), (len(bad), len(rows), bad[:2])
    assert len(exact) >= share * len(rows), (len(exact), len(rows))
    return exact


def decode_shot(run, x, y):
    img = shellrun.Image(run.shot())
    r, g, b = img.at(x, y)
    return r, g, b


# ---------------------------------------------------------------------------------------------------------------
# publish -> wait -> exact

@pytest.mark.parametrize("build,dll", [("4390913", "d3dx9_42.dll"), ("26577625", "d3dx9_43.dll")])
def test_publish_at_updateviews_exact(lrun, build, dll):
    """lockstep=1, a fast server woken by <base>.Cam: the camera is published at the fake UpdateViews (hook on), the
    insertion takes the frame drawn from that very camera in all but a few frames, no wait passes its deadline + 0.5 ms,
    and the picture on screen is the frame of a taken id. Ticket 4964: on a fake image of each build, the hook on the
    D3DXMatrixInverse of the d3dx9 DLL that build's import slot points into."""
    lrun.set_ini(**INI)
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, "--fake-exe-build", build, seconds=7)
    created = lrun.wait_log(r"lsprobe: \S+!D3DXMatrixInverse .* hook created", timeout_s=10)
    assert ("lsprobe: %s!" % dll) in created.lower(), created
    time.sleep(4.5)
    r, g, b = decode_shot(lrun, 1000, 300)
    lrun.finish()
    rows = lrun.rows()
    uv = uv_rows(rows)
    assert len(uv) > 300, len(uv)
    check_misses_justified(uv)
    check_bound(uv, qpc_freq(lrun))
    lines = lrun.ls_lines()
    assert lines and all(d["hook"] == "on" for d in lines[2:]), lines[:3]
    assert sum(d["pub_uv"] for d in lines) > 300
    # the screen: the server's colour is the frame id (R = F >> 16 = 0 here), one of the ids the composite took
    assert r == 0 and (g, b) != (0, 0), (r, g, b)
    assert g * 256 + b in {x["taken_f"] for x in rows}, (r, g, b)
    # issue 0139: the version-3 source has no slot record: no frame carries a gray-tile count
    assert all(x["unbaked"] == -1 for x in rows), {x["unbaked"] for x in rows}
    assert all(d["unbaked_n"] == 0 and d["gray_frames"] == 0 for d in lines)


def test_tap_event_after_the_last_tap_of_each_present(lrun):
    """Ticket 0713 (the prepare phase): with the frame section open the composite opens <base>.Tap and, in every Present
    hook after its last tap, writes the Present's number into the publish block (VPFramePublish::tapPresent, offset 176)
    and sets the event. A waiter on the event sees the number grow with the Presents (one per frame, never back)."""
    lrun.set_ini(**INI)
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=7)
    lrun.wait_log(r"lockstep: event '\S+\.Tap' open: set after the last tap of each Present", timeout_s=15)
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateEventW.restype = wintypes.HANDLE
    k32.OpenFileMappingW.restype = wintypes.HANDLE
    k32.MapViewOfFile.restype = ctypes.c_void_p
    k32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
    k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    k32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    ev = k32.CreateEventW(None, False, False, lrun.section + ".Tap")   # created or opened, auto-reset (vp_frame.h)
    mp = k32.OpenFileMappingW(0x0004, False, lrun.section + ".Frame")  # FILE_MAP_READ
    assert ev and mp, ctypes.get_last_error()
    view = k32.MapViewOfFile(mp, 0x0004, 0, 0, 4096)
    assert view, ctypes.get_last_error()
    try:
        vals, t_end = [], time.time() + 3.0
        while time.time() < t_end:
            if k32.WaitForSingleObject(ev, 500) == 0:
                vals.append(struct.unpack("<q", ctypes.string_at(view + 256 + 176, 8))[0])
    finally:
        k32.UnmapViewOfFile(view)
        k32.CloseHandle(mp)
        k32.CloseHandle(ev)
    lrun.finish()
    assert len(vals) >= 30, len(vals)
    assert vals[0] > 0 and all(b >= a for a, b in zip(vals, vals[1:])), vals[:10]
    assert vals[-1] - vals[0] >= len(vals) - 1, (vals[0], vals[-1], len(vals))   # a new number at (nearly) every wake
    assert sum(1 for a, b in zip(vals, vals[1:]) if b > a) >= 0.9 * (len(vals) - 1), vals[:20]


def test_frames_without_updateviews_do_not_wait(lrun):
    """--fake-uv-every 2: every other frame has no UpdateViews; its camera is published at Present (source 2), it does
    not wait and takes the newest frame (kind nopub); the UpdateViews frames stay exact."""
    lrun.set_ini(**INI)
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, "--fake-uv-every", "2", seconds=6)
    lrun.finish()
    rows = lrun.rows()[100:]
    nopub = [x for x in rows if x["kind"] == NOPUB]
    uv = [x for x in rows if x["pub_src"] == 1]
    assert len(nopub) > 100 and len(uv) > 100, (len(nopub), len(uv))
    assert all(x["pub_src"] == 2 and x["wait0_qpc"] == 0 and x["deadline_qpc"] == 0 for x in nopub)
    check_misses_justified(uv)
    lines = lrun.ls_lines()
    assert sum(d["pub_present"] for d in lines) > 100


def release_setting(env: str, key: str) -> Path:
    """Ticket 4713 (D174): the environment variable `env` if it is set, else the path setting `key` of
    tools/release/settings.py (its own environment variable, else deps/local-settings.ini), as render/build.py reads
    it; with neither, a path that does not exist, so the test that needs it fails naming `env` (was a literal path in
    a user folder, the value the setting gives on the workstation)."""
    if env in os.environ:
        return Path(os.environ[env])
    spec = importlib.util.spec_from_file_location("c5_release_settings",
                                                  shellrun.REPO / "tools" / "release" / "settings.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    v = mod.get(key)
    return Path(v) if v is not None else Path("<%s not set: setting %s of tools/release/settings.py>" % (env, key))


EXE = release_setting("C5R_EXE", "render_exe")


def test_todays_shell_server_terrain(lrun):
    """Today's shell_server.exe --scene terrain --depth 1 (it polls the camera block about every 0.5 ms and names the
    camera of each frame in the depth block): fed by the composite's synthetic height-field tap (tap=1 tap_test=synth),
    it draws the terrain for the cameras published at UpdateViews, and the insertion takes them exactly in most frames,
    depth written. The EXE is read-only, for its shaders (C5R_EXE, as test_terrain_scene.py)."""
    if not EXE.exists():
        pytest.fail("C5R_EXE %s missing (CivilizationV_DX11.exe, read-only)" % EXE)
    lrun.set_ini(**dict(INI, tap="1", tap_test="synth", depth_write="1"))
    lrun.start_server("--scene", "terrain", "--exe", str(EXE), "--depth", "1", "--fallback", "constant",
                      "--constant-rgba", "0xFFFF00FF", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=10)
    lines = lrun.finish()
    srv = lrun.server_log.read_text(errors="replace")
    assert re.search(r"terrain: state \S+ -> OK", srv), srv[-2000:]
    rows = uv_rows(lrun.rows())
    drawn = [x for x in rows if x["taken_f"] > 0]
    assert len(drawn) > 500, len(drawn)
    tail = drawn[-1000:]
    # the share depends on shell_server.exe's turnaround (polling + terrain draw + GPU wait), longer than the host's
    # 1.5 ms under ASan or load (27 % exact under ASan, 08:58): the rule is what is checked
    exact = len(check_misses_justified(tail, share=0.2))
    check_bound(rows, qpc_freq(lrun))
    assert sum(x["depth"] for x in tail) >= 0.2 * len(tail)


# ---------------------------------------------------------------------------------------------------------------
# a slow, a stalled, a killed server

def test_slow_server_previous_picture_and_giveup(lrun):
    """The server needs 8 ms per frame (> the 3.4 ms bound): every lockstep frame misses; each wait ends at its deadline
    (+0.5 ms at most), the previous picture is drawn (kind older, age >= 1), and after lockstep_giveup (8) misses in a row
    the composite stops waiting (wait_off) and, the server never catching up, never waits again."""
    lrun.set_ini(**INI_BOUND_3400)
    lrun.start_lockstep_server("--delay-us", "8000", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=6)
    lines = lrun.finish()
    rows = lrun.rows()
    uv = uv_rows(rows, skip=0)
    waits = check_bound(uv, qpc_freq(lrun))
    freq = qpc_freq(lrun)
    assert waits, "no lockstep wait at all"
    check_late([(x["wait1_qpc"] - x["wait0_qpc"]) * 1000.0 / freq - 3.4 for x in waits], "wait length - bound")   # 0494
    giveups = sum("misses in a row" in ln for ln in lines)
    recovered = sum("recovered" in ln for ln in lines)
    # each give-up comes after exactly 8 waited misses; a recovery (a frame ready without a wait: only when the host
    # itself stalls for longer than the server's 8 ms) re-arms the wait. 0494: under load a waited frame can also arrive
    # in time (exact), which restarts the count (39 waits for 4 give-ups and 3 recoveries, 2026-11-17), so the rule is
    # checked on the rows: never more than 8 waited misses in a row, and 8 right before each give-up
    assert giveups >= 1 and recovered <= giveups and 8 * giveups <= len(waits), (len(waits), giveups, recovered)
    row_miss, before_off = 0, []
    for i, x in enumerate(uv):
        if x["wait_off"]:
            if i and not uv[i - 1]["wait_off"]:
                before_off.append(row_miss)
            row_miss = 0
        elif x["kind"] == EXACT:
            row_miss = 0
        elif x["wait0_qpc"]:
            row_miss += 1
            assert row_miss <= 8, ("more than lockstep_giveup waited misses in a row", i)
    assert before_off and all(n == 8 for n in before_off), before_off
    off = [x for x in uv if x["wait_off"]]
    assert len(off) > 200 and all(x["wait0_qpc"] == 0 for x in off)
    older = [x for x in uv if x["kind"] == OLDER]
    # taken_f 0: a frame whose camera is not named (drawn before the depth block matched), only at the start
    known = [x for x in older if x["taken_f"] > 0]
    first = next(i for i, x in enumerate(older) if x["taken_f"] > 0)
    assert len(known) > 200 and all(x["age"] >= 1 for x in known) and all(x["taken_f"] > 0 for x in older[first:])


def test_late_insertion_counts_late_starts_not_overruns(lrun):
    """Issue 0150 (9055 V4): the EXE reaches the insertion 5 ms after UpdateViews (--uv-work-us 5000, past the 3.4-ms
    bound) and the server needs 8 ms per frame, so every wait starts after its deadline and returns at once. The
    `lockstep 1s:` lines count them as late_start, not as overrun (the pre-0150 `late` counter, end - deadline > 0.5 ms,
    counted every one of them); in the records each wait ends within 0.5 ms of max(deadline, its start)."""
    lrun.set_ini(**INI_BOUND_3400)
    lrun.start_lockstep_server("--delay-us", "8000", seconds=SERVER_S)
    lrun.start_host(*HOST, "--uv-work-us", "5000", seconds=5)
    lrun.finish()
    freq = qpc_freq(lrun)
    waits = [x for x in uv_rows(lrun.rows(), skip=0) if x["wait0_qpc"]]
    assert len(waits) >= 8, len(waits)   # at least the lockstep_giveup (8) waited misses before the give-up
    assert all(x["wait0_qpc"] >= x["deadline_qpc"] for x in waits), [x for x in waits if x["wait0_qpc"] < x["deadline_qpc"]][:2]
    old = [x for x in waits if (x["wait1_qpc"] - x["deadline_qpc"]) * 1000.0 / freq > 0.5]
    assert len(old) == len(waits), (len(old), len(waits))   # what the old counter would have called late
    past = [(x["wait1_qpc"] - max(x["deadline_qpc"], x["wait0_qpc"])) * 1000.0 / freq for x in waits]
    over = [v for v in past if v > LATE_MS]
    lines = lrun.ls_lines()
    assert lines and all("late_start" in d and "overrun" in d and "overrun_max_us" in d and "late" not in d for d in lines), lines[:2]
    late_start, overrun, n = (sum(d[k] for d in lines) for k in ("late_start", "overrun", "waits"))
    assert late_start == n and late_start >= 8, (late_start, n)
    # the counter equals the records; how many overran is machine load only (a host stall inside a wait), checked as
    # check_late does (0494: was at most 1)
    assert overrun == len(over), (overrun, len(over))
    check_late(past, "wait end - max(deadline, start)")


def test_stalled_server_giveup_and_recovery(lrun):
    """The server draws nothing for 1.5 s, from 1.5 s after the UpdateViews hook is enabled: exact before, misses and
    give-up during the stall (at most lockstep_giveup waits), then "recovered" and exact frames again once a frame is
    ready without a wait. Ticket 0494: the stall started at 3.0 s on the server's clock (then after 600 drawn frames)
    until under load the hook commit and the UpdateViews hook's enable put the host's first lockstep frames after it
    (2026-11-17); now the test starts it (lockstep_server --stall-on: a file it makes) once lockstep frames have run."""
    lrun.set_ini(**INI)
    go = lrun.dir / "stall.go"
    lrun.start_lockstep_server("--stall-on", str(go), "--stall-for", "1.5", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=12)
    lrun.wait_log(r"lsprobe: hook ENABLED", timeout_s=HOOK_COMMIT_WAIT_S)
    time.sleep(1.5)
    go.write_text("go")
    lines = lrun.finish()
    srv = lrun.server_log.read_text(errors="replace")
    assert "stall: no frames from" in srv and "stall over at" in srv, srv[-1500:]
    rows = uv_rows(lrun.rows())
    check_bound(rows, qpc_freq(lrun))
    off = [i for i, x in enumerate(rows) if x["wait_off"]]
    assert off, "no wait_off frame during the stall"
    assert sum("misses in a row" in ln for ln in lines) >= 1 and sum("recovered" in ln for ln in lines) >= 1
    # the stall's episode: the longest run of wait_off frames (1.5 s, hundreds of frames; a loaded machine, e.g. under
    # ASan, adds short episodes before or after it, each again 8 waited misses, a give-up and a recovery)
    runs, i = [], 0
    while i < len(rows):
        if rows[i]["wait_off"]:
            j = i
            while j + 1 < len(rows) and rows[j + 1]["wait_off"]:
                j += 1
            runs.append((i, j))
            i = j + 1
        else:
            i += 1
    first, end = max(runs, key=lambda r: r[1] - r[0])
    before, after = rows[:first], rows[end + 1:]
    assert len(before) > 100 and len(after) > 100, (len(before), len(after), runs[:5])
    check_misses_justified(before)
    check_misses_justified(after)
    # the misses in a row right before the give-up (back to the last exact frame): exactly lockstep_giveup of them waited
    i, waited = first - 1, 0
    while i >= 0 and rows[i]["kind"] != EXACT:
        waited += 1 if rows[i]["wait0_qpc"] else 0
        i -= 1
    assert waited == 8, waited
    assert all(not x["wait0_qpc"] for x in rows[first:end + 1])


def test_killed_server(lrun):
    """The server dies after 400 frames (TerminateProcess, exit 9): the composite runs on (the host exits 0, nothing
    disabled), shows the last frame (older, the same id every time), waits no longer than the bound and gives up."""
    lrun.set_ini(**INI)
    srv = lrun.start_lockstep_server("--exit-after", "400", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=6)
    assert srv.wait(20) == 9, shellrun.tail(lrun.server_log, 5)
    lines = lrun.finish()
    rows = uv_rows(lrun.rows(), skip=0)
    check_bound(rows, qpc_freq(lrun))
    last = max(x["taken_f"] for x in rows)
    tail = rows[-200:]
    assert all(x["kind"] == OLDER and x["taken_f"] == last for x in tail), tail[:3]
    # after the kill: the give-up comes and stays (earlier ones may come from machine load, each with its recovery)
    gu = [i for i, ln in enumerate(lines) if "misses in a row" in ln]
    assert gu and not any("recovered" in ln for ln in lines[gu[-1]:])
    assert any("STALE" in ln for ln in lines)


# ---------------------------------------------------------------------------------------------------------------
# ticket 0691 (D242): lockstep_mode=exact, the design's stand-in tests (tracker/issues/0691 point 7)

def frame_ms(rows, freq):
    """Present-to-Present times (ms) of consecutive rows."""
    return [(b["present_qpc"] - a["present_qpc"]) * 1000.0 / freq for a, b in zip(rows, rows[1:])]


def exact_counts(lrun):
    ls = lrun.ls_lines()
    assert ls and all(d["mode"] == "exact" for d in ls), ls[:2]
    return ls, {k: int(sum(d[k] for d in ls)) for k in ("hung_ends", "dead_ends", "giveups", "older", "exact")}


def hung_lines(lines):
    return [ln for ln in lines if "LOUD: lockstep: the server is HUNG" in ln]


def stall_run(lrun, stall_s, *server_extra, seconds, after_go_s=1.5):
    """A stand-in server that stalls for stall_s once lockstep frames have run (the --stall-on file, as 0494's test)."""
    go = lrun.dir / "stall.go"
    lrun.start_lockstep_server("--stall-on", str(go), "--stall-for", str(stall_s), *server_extra, seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=seconds)
    lrun.wait_log(r"lsprobe: hook ENABLED", timeout_s=HOOK_COMMIT_WAIT_S)
    time.sleep(after_go_s)
    go.write_text("go")
    return go


def test_exact_slow_server_every_frame_its_own(lrun):
    """(a) The server needs 30 ms per frame (--delay-us 30000, far past the bounded mode's 8 ms) and the camera pans:
    every lockstep frame takes the frame of its own camera (0 older, no give-up), the frame time follows the server
    (about 30 ms), no hung or dead end. The waits have no deadline of their own: the publish block's deadline is the
    hung limit (the server's Busy retry), so no wait ends at it."""
    lrun.set_ini(**INI_EXACT)
    lrun.start_lockstep_server("--delay-us", "30000", seconds=SERVER_S)
    lrun.start_host(*HOST, "--fake-cam-pan", "400", seconds=7)
    lines = lrun.finish()
    freq = qpc_freq(lrun)
    uv = uv_rows(lrun.rows(), skip=10)
    assert len(uv) > 60, len(uv)
    bad = [x for x in uv if x["kind"] != EXACT or x["taken_f"] != x["pub_f"]]
    assert not bad, (len(bad), len(uv), bad[:2])
    assert len({x["cam_x"] for x in uv}) > 30   # the camera moved
    ft = sorted(frame_ms(uv, freq))
    med = ft[len(ft) // 2]
    timing_record("exact frame ms", ft, median=med)
    assert 28.0 <= med <= 80.0, (med, ft[:3], ft[-3:])
    assert all((x["wait1_qpc"] - x["pub_qpc"]) * 1000.0 / freq < 3000.0 for x in uv if x["wait0_qpc"])
    ls, n = exact_counts(lrun)
    # n["older"] is not asserted: the first UpdateViews frame (pub_f 51, skipped above as start-up) may take frame 1 after
    # the version-3 keyed-mutex retry (100 ms; 2026-11-23 01:12, one row) - the rows after the start-up are the check
    assert n["hung_ends"] == 0 and n["dead_ends"] == 0 and n["giveups"] == 0, n
    assert not hung_lines(lines) and not any("misses in a row" in ln for ln in lines)
    assert any("lockstep_mode=exact lockstep_hung_ms=30000" in ln for ln in lines)


def test_exact_camera_at_rest_still_waits(lrun):
    """(f) The camera does not move (no --fake-cam-pan): not exempt (design point 3), every lockstep frame still waits for
    and takes its own frame; with a 30-ms server the frame time is about 30 ms, as when panning."""
    lrun.set_ini(**INI_EXACT)
    lrun.start_lockstep_server("--delay-us", "30000", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=6)
    lrun.finish()
    freq = qpc_freq(lrun)
    uv = uv_rows(lrun.rows(), skip=10)
    assert len(uv) > 60 and len({(x["cam_x"], x["cam_y"]) for x in uv}) == 1, len(uv)
    assert all(x["kind"] == EXACT and x["taken_f"] == x["pub_f"] for x in uv), [x for x in uv if x["kind"] != EXACT][:2]
    assert sum(1 for x in uv if x["wait0_qpc"]) > 0.9 * len(uv)
    ft = sorted(frame_ms(uv, freq))
    assert ft[len(ft) // 2] >= 28.0, ft[len(ft) // 2]
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 0 and n["dead_ends"] == 0, n   # older: see test_exact_slow_server_every_frame_its_own


def test_exact_two_second_stall_no_failure(lrun):
    """(b) A 2-s stall (heartbeat ticking): every lockstep frame exact, one long frame of about 2 s, no hung end, no
    failure route."""
    lrun.set_ini(**dict(INI_EXACT, hung_sink_test="1"))
    stall_run(lrun, 2.0, seconds=10)
    lines = lrun.finish()
    freq = qpc_freq(lrun)
    srv = lrun.server_log.read_text(errors="replace")
    assert "stall: no frames from" in srv and "stall over at" in srv, srv[-1500:]
    uv = uv_rows(lrun.rows())
    assert len(uv) > 200
    assert all(x["kind"] == EXACT and x["taken_f"] == x["pub_f"] for x in uv), [x for x in uv if x["kind"] != EXACT][:2]
    ft = frame_ms(uv, freq)
    long_ = [v for v in ft if v > 1000.0]
    timing_record("stall frame ms", sorted(ft)[-5:], long=len(long_))
    assert len(long_) == 1 and 1800.0 <= long_[0] <= 4000.0, (long_, sorted(ft)[-3:])
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 0 and n["dead_ends"] == 0, n   # older: see test_exact_slow_server_every_frame_its_own
    assert not hung_lines(lines) and not any("hung_sink_test: ServerHung(" in ln for ln in lines)


@pytest.mark.parametrize("heartbeat", ["stopped", "alive"])
def test_exact_hung_server_takes_the_failure_route_once(lrun, heartbeat):
    """(c) NEGATIVE CONTROL: a stall longer than lockstep_hung_ms (3000 here). heartbeat=stopped (--stall-heartbeat-stop,
    a render thread blocked): the wait ends at the hung limit by the heartbeat's age; heartbeat=alive (the server ticks
    but draws nothing; lead-0673's no-progress limit): by no new frame for that long. Either way once: one LOUD line,
    hungEnds 1, the proxy's call (hung_sink_test: logged, no proxy here), no wait until the server draws again, then
    "recovered" and exact frames. The host runs on (exit 0)."""
    lrun.set_ini(**dict(INI_EXACT, lockstep_hung_ms="3000", hung_sink_test="1"))
    extra = ("--stall-heartbeat-stop",) if heartbeat == "stopped" else ()
    stall_run(lrun, 6.0, *extra, seconds=14)
    lines = lrun.finish()
    loud = hung_lines(lines)
    assert len(loud) == 1, loud
    if heartbeat == "stopped":
        m = re.search(r"server drew no frame for ([\d.]+) s \(heartbeat age ([\d.]+) s\)", loud[0])
        assert m and 2.9 <= float(m.group(2)) <= 5.0, loud[0]
    else:
        m = re.search(r"no frame delivered for ([\d.]+) s, heartbeat alive \(heartbeat age ([\d.]+) s\)", loud[0])
        assert m and 2.9 <= float(m.group(1)) <= 5.0 and float(m.group(2)) < 1.0, loud[0]
    sink = [ln for ln in lines if "hung_sink_test: ServerHung(" in ln]
    assert len(sink) == 1 and m.group(0) in sink[0], (sink, loud)
    rec = [i for i, ln in enumerate(lines) if "lockstep: exact: recovered" in ln]
    hung_at = lines.index(loud[0])
    assert rec and rec[0] > hung_at, rec
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 1 and n["dead_ends"] == 0, n
    uv = uv_rows(lrun.rows())
    off = [x for x in uv if x["wait_off"]]
    assert off and all(not x["wait0_qpc"] for x in off)   # the frames after the hung end did not wait
    after = uv[max(i for i, x in enumerate(uv) if x["wait_off"]) + 1:]
    assert len(after) > 50 and all(x["kind"] == EXACT for x in after), (len(after), [x for x in after if x["kind"] != EXACT][:2])


def test_exact_hung_server_fatal_end_writes_the_totals(lrun):
    """Ticket 0697: (c)'s negative control ending as a D33 session does: test_host --fatal-sink plays the proxy's failure
    route (VPShell_FlushTotals("fatal", ...), then TerminateProcess(3): no DLL_PROCESS_DETACH). The composite's log ends
    with "pacing: the process ends (why=fatal)" and the run-long line "pacing totals: why=fatal" (the format of why=exit,
    presents counted), no why=exit line, no "pacing 1s:" after it; the host's exit code is 3; the call returned 1 within its
    1000-ms bound; a second call (a second Fatal) returns 0 and writes nothing."""
    lrun.set_ini(**dict(INI_EXACT, lockstep_hung_ms="3000"))
    go = lrun.dir / "stall.go"
    lrun.start_lockstep_server("--stall-on", str(go), "--stall-for", "6.0", "--stall-heartbeat-stop", seconds=SERVER_S)
    lrun.start_host(*HOST, "--fatal-sink", seconds=14)
    lrun.wait_log(r"lsprobe: hook ENABLED", timeout_s=HOOK_COMMIT_WAIT_S)
    time.sleep(1.5)
    go.write_text("go")
    code = lrun.host.wait(40)
    assert code == 3, (code, shellrun.tail(lrun.host_log, 10), shellrun.tail(lrun.composite_log, 12))
    lines = lrun.log_lines()
    assert len(hung_lines(lines)) == 1, hung_lines(lines)
    end = [i for i, ln in enumerate(lines) if "pacing: the process ends (why=fatal): exit=3 stage=server-hung code=no-frame" in ln]
    tot = [i for i, ln in enumerate(lines) if " pacing totals: " in ln]
    assert len(end) == 1 and "the ring drained" in lines[end[0]], [lines[i] for i in end]
    assert tot and tot[-1] > end[0] and " pacing totals: why=fatal " in lines[tot[-1]], [lines[i] for i in tot[-2:]]
    last = lines[tot[-1]]
    fields = dict(re.findall(r"(\w+)=(\S+)", last.split(" pacing totals: ", 1)[1]))
    assert int(fields["presents"]) > 100 and int(fields["ft_n"]) > 100 and fields["ft_bins"].count("/") == 8, fields
    assert not any("pacing totals: why=exit" in ln for ln in lines)
    assert not any("pacing 1s:" in ln for ln in lines[tot[-1]:]), lines[tot[-1]:][:5]
    host = lrun.log_lines("host")
    m = [re.search(r"fatal-sink: VPShell_FlushTotals returned (-?\d+) in ([\d.]+) ms", ln) for ln in host]
    m = [x for x in m if x]
    assert len(m) == 1 and m[0].group(1) == "1" and float(m[0].group(2)) < 1000.0, host[-6:]
    # once per process: a second call (a second Fatal) returns 0 at once and writes nothing
    assert any(re.search(r"fatal-sink: the second VPShell_FlushTotals returned 0 in [\d.]+ ms", ln) for ln in host), host[-6:]
    assert sum(" pacing totals: why=fatal " in ln for ln in lines) == 1 and not any("the second call" in ln for ln in lines)


_u32 = ctypes.WinDLL("user32", use_last_error=True)
_WNDENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
_u32.EnumWindows.argtypes = [_WNDENUM, wintypes.LPARAM]
_u32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
_u32.IsHungAppWindow.argtypes = [wintypes.HWND]
_u32.IsWindowVisible.argtypes = [wintypes.HWND]
_u32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT,
                                     wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
_u32.SendMessageTimeoutW.restype = ctypes.c_size_t


def _host_window(pid):
    found = []

    def cb(hwnd, _):
        owner = wintypes.DWORD()
        _u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _u32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    _u32.EnumWindows(_WNDENUM(cb), 0)
    return found[0] if found else None


def _watch_hung(pid, seconds, out):
    """On the hidden desktop: IsHungAppWindow of the host's window every 100 ms for `seconds`."""
    hwnd = _host_window(pid)
    out["hwnd"] = hwnd
    t0, samples = time.perf_counter(), []
    while hwnd and time.perf_counter() - t0 < seconds:
        samples.append((time.perf_counter(), bool(_u32.IsHungAppWindow(hwnd))))
        time.sleep(0.1)
    out["samples"] = samples


def _send_null(pid, out):
    """On the hidden desktop: SendMessageTimeoutW(WM_NULL) to the host's window; when it came back and whether it was
    handled (a cross-thread sent message is handled only when the window's thread asks for messages)."""
    hwnd = _host_window(pid)
    res = ctypes.c_size_t()
    out["t_send"] = time.perf_counter()
    ok = _u32.SendMessageTimeoutW(hwnd, 0x0000, 0, 0, 0x0000, 20000, ctypes.byref(res)) if hwnd else 0
    out["t_back"] = time.perf_counter()
    out["ok"] = bool(ok)


def test_exact_long_stall_no_reentry(lrun, hidden_desktop):
    """(d) A 7-s stall with lockstep_hung_ms=20000: the main thread waits for 7 s inside one frame, with no message pump
    (lead-0673's choice after this test, 2026-11-23: a PeekMessageW(PM_NOREMOVE | PM_QS_POSTMESSAGE) every 500 ms neither
    kept IsHungAppWindow FALSE - TRUE at 4.92 s, 4.72 s without it - nor kept a cross-thread SendMessage out: answered at
    2.51 s, i.e. the window procedure ran in the middle of the frame). Regression test: a SendMessage(WM_NULL) from
    another thread 2 s into the stall is answered only after the long frame was presented (no re-entry), no hung or
    dead end. When Windows marked the window "not responding" (IsHungAppWindow, read every 100 ms from another thread)
    is recorded, not asserted (expected about 5 s into the stall)."""
    lrun.set_ini(**dict(INI_EXACT, lockstep_hung_ms="20000"))
    stall_run(lrun, 7.0, seconds=16)
    t_go = time.perf_counter()
    watch, send = {}, {}
    tw = threading.Thread(target=lambda: desktops_on(hidden_desktop, _watch_hung, lrun.host.pid, 9.5, watch), daemon=True)
    tw.start()
    time.sleep(2.0)
    ts = threading.Thread(target=lambda: desktops_on(hidden_desktop, _send_null, lrun.host.pid, send), daemon=True)
    ts.start()
    tw.join(30)
    ts.join(30)
    lrun.finish()
    srv = lrun.server_log.read_text(errors="replace")
    assert "stall: no frames from" in srv and "stall over at" in srv, srv[-1500:]
    assert watch.get("hwnd") and watch.get("samples"), watch
    freq = qpc_freq(lrun)
    uv = uv_rows(lrun.rows())
    gaps = [(b["present_qpc"] - a["present_qpc"]) * 1000.0 / freq for a, b in zip(uv, uv[1:])]
    long_i = [i for i, v in enumerate(gaps) if v > 1000.0]
    assert len(long_i) == 1 and gaps[long_i[0]] >= 6500.0, [gaps[i] for i in long_i]   # the one long frame of the stall
    frame_start = uv[long_i[0]]["present_qpc"] / freq            # time.perf_counter() is the same clock (QPC)
    frame_end = uv[long_i[0] + 1]["present_qpc"] / freq
    hung = [t - t_go for t, h in watch["samples"] if h]
    rec = dict(hung_samples=len(hung), first_hung_s=round(hung[0], 2) if hung else None, send_ok=send.get("ok"),
               send_at_s=round(send.get("t_send", 0) - t_go, 2), send_back_s=round(send.get("t_back", 0) - t_go, 2),
               frame_start_s=round(frame_start - t_go, 2), frame_end_s=round(frame_end - t_go, 2))
    timing_record("hung window", [], **rec)
    print("0691 (d):", rec)
    assert send.get("ok"), send
    assert frame_start < send["t_send"] < frame_end, rec              # sent inside the long frame
    assert send["t_back"] >= frame_end - 0.002, rec                   # answered only after it: no re-entry
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 0 and n["dead_ends"] == 0, n


def desktops_on(name, fn, *a):
    return desktops.on_desktop(name, fn, *a, timeout=60)   # tools/harness/tests/desktops.py (shellrun puts it on the path)


def test_exact_killed_server_dead_route(lrun):
    """(e) The server dies after 400 frames (TerminateProcess, exit 9): the exact wait ends by the dead route at once
    (its process handle), logged once; later frames do not wait (no 30-s freeze), the last frame stays on screen (older,
    one id), no hung end, the host runs on (exit 0)."""
    lrun.set_ini(**dict(INI_EXACT, hung_sink_test="1"))
    srv = lrun.start_lockstep_server("--exit-after", "400", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=6)
    assert srv.wait(20) == 9, shellrun.tail(lrun.server_log, 5)
    lines = lrun.finish()
    freq = qpc_freq(lrun)
    dead = [ln for ln in lines if "lockstep: exact: the server is dead (the server process ended" in ln]
    assert len(dead) == 1, [ln for ln in lines if "is dead" in ln]
    rows = uv_rows(lrun.rows(), skip=0)
    last = max(x["taken_f"] for x in rows)
    tail = rows[-150:]
    assert all(x["kind"] == OLDER and x["taken_f"] == last for x in tail), tail[:3]
    waits = [(x["wait1_qpc"] - x["wait0_qpc"]) * 1000.0 / freq for x in tail if x["wait0_qpc"]]
    assert all(w < 100.0 for w in waits), sorted(waits)[-3:]
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 0 and n["dead_ends"] >= 100, n
    assert not hung_lines(lines) and not any("hung_sink_test: ServerHung(" in ln for ln in lines)


def held_lines(lines):
    return [ln for ln in lines if "LOUD: lockstep: held_skip:" in ln]


def hold_run(lrun, presents, *server_extra, seconds, after_go_s=1.5):
    """Ticket 0691 (MC #11169): a stand-in server that HOLDS the first camera after the --hold-on file appears (and every
    camera after it) until the EXE has presented `presents` more frames (-1: never), as server.exe's fog holds the first
    map frame of a load for the feed's plot table and the overlay pair (design 3.2's live deadlock)."""
    go = lrun.dir / "hold.go"
    lrun.start_lockstep_server("--hold-for-exe", "1,%d" % presents, "--hold-on", str(go), *server_extra, seconds=SERVER_S)
    lrun.start_host(*HOST, "--fake-cam-pan", "400", seconds=seconds)
    lrun.wait_log(r"lsprobe: hook ENABLED", timeout_s=HOOK_COMMIT_WAIT_S)
    time.sleep(after_go_s)
    go.write_text("go")


def hold_start(rows):
    held = [i for i, x in enumerate(rows) if x["kind"] == HELD]
    return held[0] if held else None


def test_exact_held_frames_are_not_waited_for(lrun):
    """Ticket 0691 (MC #11169), the REGRESSION test of the live load deadlock (design 3.2): the server holds a frame until
    the EXE has presented 40 more frames and reports the hold (vp_frame.h heldFrameId / holdReasons). The composite does
    not wait for a held frame: each one is a held_skip (one LOUD line with the reason bits, kind held, no frame taken: no
    server world, never an older one), the EXE presents on, the server draws again and the frames after it are exact; no
    hung end, no proxy call, no fault. On the head before the fix (2026-11-23, 4b1936ea9) the first held frame waited for
    lockstep_hung_ms (3000 here) and took the hung route (hung_ends 1), as the live load did after 30 s."""
    lrun.set_ini(**dict(INI_EXACT, lockstep_hung_ms="3000", hung_sink_test="1"))
    hold_run(lrun, 40, seconds=8)
    lines = lrun.finish()
    freq = qpc_freq(lrun)
    srv = lrun.server_log.read_text(errors="replace")
    assert "hold: frame" in srv and "hold: over at present" in srv, srv[-1500:]
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 0 and n["dead_ends"] == 0, (n, hung_lines(lines)[:2])
    assert not hung_lines(lines) and not any("hung_sink_test: ServerHung(" in ln for ln in lines)
    assert not any("HOLD is STUCK" in ln for ln in lines)
    uv = uv_rows(lrun.rows(), skip=0)
    i0 = hold_start(uv)
    assert i0 is not None, "no held frame"
    held = [x for x in uv if x["kind"] == HELD]
    i1 = max(i for i, x in enumerate(uv) if x["kind"] == HELD)
    # one run of held frames: the 40 presents of the hold (the frame that ends it may be held or drawn)
    assert all(x["kind"] == HELD for x in uv[i0:i1 + 1]), [x["kind"] for x in uv[i0:i1 + 1]]
    assert 35 <= len(held) <= 45, len(held)
    assert all(x["taken_f"] == 0 for x in held), held[:2]   # no server world in a held frame
    waits = [(x["wait1_qpc"] - x["wait0_qpc"]) * 1000.0 / freq for x in held if x["wait0_qpc"]]
    timing_record("held frame wait ms", sorted(waits), held=len(held))
    assert not waits or max(waits) < LATE_FAR_MS, sorted(waits)[-3:]   # no wait until the hung limit
    loud = held_lines(lines)
    assert len(loud) == len(held), (len(loud), len(held))
    assert all("reasons 0x80000001 fog-no-table test" in ln for ln in loud), loud[:2]
    assert int(sum(d["held_skip"] for d in ls)) == len(held) and int(sum(d["held_faults"] for d in ls)) == 0
    assert any("lockstep: held run ended at frame" in ln for ln in lines)
    after = uv[i1 + 1:]
    assert len(after) > 50 and all(x["kind"] == EXACT and x["taken_f"] == x["pub_f"] for x in after), (len(after), [x for x in after if x["kind"] != EXACT][:2])
    before = uv[10:i0]
    assert before and all(x["kind"] == EXACT for x in before), [x for x in before if x["kind"] != EXACT][:2]


def test_exact_unreported_hold_deadlocks_into_the_hung_route(lrun):
    """Ticket 0691: the same hold NOT reported (--hold-unreported: a server before the report, i.e. the live deadlock): the
    first held frame waits for its frame, which the server draws only after 40 more EXE presents, so neither moves until
    the no-progress limit (lockstep_hung_ms=3000) ends the wait: one hung end and the proxy's call. Shows the stand-in's
    hold is a real deadlock and the report is what removes it."""
    lrun.set_ini(**dict(INI_EXACT, lockstep_hung_ms="3000", hung_sink_test="1"))
    hold_run(lrun, 40, "--hold-unreported", seconds=12)
    lines = lrun.finish()
    loud = hung_lines(lines)
    assert len(loud) == 1 and "heartbeat alive" in loud[0], loud
    assert len([ln for ln in lines if "hung_sink_test: ServerHung(" in ln]) == 1
    assert not held_lines(lines)
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 1, n


def test_exact_stuck_hold_takes_the_failure_route_once(lrun):
    """Ticket 0691 (MC #11169) NEGATIVE CONTROL: a hold that never clears (--hold-for-exe 1,-1) while the EXE presents: every
    frame is a held_skip (no server world, never an older one, no wait), and once the run of held frames passes
    lockstep_hung_ms (3000 here): one LOUD "HOLD is STUCK" line with the reason bits, held_faults 1, and the proxy's
    failure route (hung_sink_test: the call logged once, with that reason). No hung end (the wait itself never hung)."""
    lrun.set_ini(**dict(INI_EXACT, lockstep_hung_ms="3000", hung_sink_test="1"))
    hold_run(lrun, -1, seconds=8)
    lines = lrun.finish()
    stuck = [ln for ln in lines if "LOUD: lockstep: the server's HOLD is STUCK" in ln]
    assert len(stuck) == 1, stuck
    m = re.search(r"server held frame (\d+) for EXE inputs for ([\d.]+) s while the EXE presented (\d+) frames "
                  r"\(hold reasons 0x80000001 fog-no-table test\)", stuck[0])
    assert m and 3.0 <= float(m.group(2)) <= 4.5 and int(m.group(3)) > 20, stuck[0]
    sink = [ln for ln in lines if "hung_sink_test: ServerHung(" in ln]
    assert len(sink) == 1 and m.group(0) in sink[0], (sink, stuck)
    ls, n = exact_counts(lrun)
    assert n["hung_ends"] == 0 and int(sum(d["held_faults"] for d in ls)) == 1, n
    uv = uv_rows(lrun.rows(), skip=0)
    i0 = hold_start(uv)
    assert i0 is not None
    rest = uv[i0:]
    assert len(rest) > 100 and all(x["kind"] == HELD and x["taken_f"] == 0 for x in rest), (len(rest), [x for x in rest if x["kind"] != HELD][:2])
    assert held_lines(lines)


@pytest.mark.parametrize("key,value,want,shown", [
    ("lockstep_mode", "fast", "lockstep_mode='fast' is neither exact nor bounded - exact used", ("exact", 30000)),
    ("lockstep_hung_ms", "500", "lockstep_hung_ms='500' is outside 2000..120000 - clamped to 2000", ("exact", 2000)),
    ("lockstep_hung_ms", "x", "lockstep_hung_ms='x' is not a number - the default 30000 used", ("exact", 30000)),
])
def test_exact_config_values_refused(lrun, key, value, want, shown):
    """Ticket 0691's keys: a value out of range or not a word/number is clamped or replaced, one CONFIG ERROR line, and
    the value in force is what the 1 s lines show."""
    lrun.set_ini(**dict(INI_EXACT, **{key: value}))
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=4)
    lines = lrun.finish()
    assert sum(("CONFIG ERROR " + want) in ln for ln in lines) == 1, [ln for ln in lines if "CONFIG" in ln]
    ls = lrun.ls_lines()
    assert ls and all((d["mode"], int(d["hung_ms"])) == shown for d in ls), ls[:1]


# ---------------------------------------------------------------------------------------------------------------
# configuration errors

@pytest.mark.parametrize("key,value,want,bound", [
    ("lockstep_bound_us", "-5", "lockstep_bound_us='-5' is outside 0..10000 us - clamped to 0", 0),
    ("lockstep_bound_us", "99999", "lockstep_bound_us='99999' is outside 0..10000 us - clamped to 10000", 10000),
    ("lockstep_bound_us", "abc", "lockstep_bound_us='abc' is not a number - the default 8000 us used", 8000),   # 0675
    ("lockstep_bound_us", "12x", "lockstep_bound_us='12x' is not a number - the default 8000 us used", 8000),
    ("lockstep_giveup", "0", "lockstep_giveup='0' is outside 1..1000 - clamped to 1", 8000),   # the shipped ini's bound
    ("handover", "bogus", "handover='bogus' is neither v3 nor ring - v3 used", 8000),
])
def test_config_values_refused(lrun, key, value, want, bound):
    """A bound out of range or not a number is clamped or replaced by the default, logged as a CONFIG ERROR, and the
    bound in force is the one the 1 s line and the rows show; bound 0 never waits (the newest picture)."""
    lrun.set_ini(**dict(INI, **{key: value}))
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=4)
    lines = lrun.finish()
    assert sum(("CONFIG ERROR " + want) in ln for ln in lines) == 1, [ln for ln in lines if "CONFIG" in ln]
    ls = lrun.ls_lines()
    assert ls and all(d["bound_us"] == bound for d in ls) and all(d["src"] == "v3" for d in ls if d["open"])
    rows = uv_rows(lrun.rows(), skip=30)
    assert len(rows) > 100
    if bound == 0:
        assert all(x["wait0_qpc"] == 0 and x["deadline_qpc"] == 0 for x in rows)
    if key == "lockstep_giveup":
        assert all(d["giveup"] == 1 for d in ls)


def test_inlist_rule_keeps_the_v3_path(lrun):
    """tag_rule=inlist with lockstep=1: CONFIG ERROR, the version-3 path draws as before (no lockstep line counts)."""
    lrun.set_ini(**dict(INI, tag_rule="inlist"))
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=4)
    lines = lrun.finish()
    assert any("CONFIG ERROR lockstep, handover=ring and depth_write need the insertion on the immediate context" in ln for ln in lines)
    assert not lrun.ls_lines()
    st = shellrun.stats(lines)[1:]
    assert st and sum(r["srv_new"] for r in st) > 50


def test_no_hook_publishes_at_present(lrun):
    """No UpdateViews call site in the fake image (no --fake-uv): the hook is refused, every camera is published at
    Present (source 2) and no frame waits; the composite draws on."""
    lrun.set_ini(**INI)
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*[a for a in HOST if a != "--fake-uv"], seconds=5)
    lines = lrun.finish()
    assert any("lsprobe: REFUSED" in ln for ln in lines)
    assert any("was published at Present" in ln for ln in lines)
    ls = lrun.ls_lines()
    assert ls and sum(d["pub_uv"] for d in ls) == 0 and sum(d["waits"] for d in ls) == 0 and all(d["hook"] == "refused" for d in ls)
    rows = lrun.rows()[50:]
    assert rows and all(x["pub_src"] == 2 and x["kind"] == NOPUB for x in rows)


def test_updateviews_on_another_thread_not_published(lrun):
    """UpdateViews on a thread that does not present the target (--fake-uv-thread): refused there (logged once, counted
    wrong_thread), the camera goes out at Present, no wait; the composite draws on."""
    lrun.set_ini(**INI)
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, "--fake-uv-thread", seconds=5)
    lines = lrun.finish()
    assert sum("lockstep: UpdateViews ran on thread" in ln for ln in lines) == 1
    ls = lrun.ls_lines()
    assert sum(d["wrong_thread"] for d in ls) > 100 and sum(d["pub_uv"] for d in ls) == 0 and sum(d["waits"] for d in ls) == 0
    assert sum(d["pub_present"] for d in ls) > 100
    rows = lrun.rows()[50:]
    assert rows and all(x["pub_src"] == 2 for x in rows)


def test_exception_in_the_updateviews_publish(lrun):
    """fault_test=uv: one access violation inside the UpdateViews publish. The guard catches it (logged), publishing at
    UpdateViews stops for good, the Present publishes from then on, nothing is disabled and the host exits 0."""
    lrun.set_ini(**dict(INI, fault_test="uv"))
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=5)
    assert lrun.host.wait(40) == 0, shellrun.tail(lrun.host_log, 10)
    time.sleep(0.8)
    lines = lrun.log_lines()
    assert sum("EXCEPTION 0xC0000005 in the UpdateViews publish" in ln for ln in lines) == 1, [ln for ln in lines if "EXCEPTION" in ln]
    assert not any("DISABLED" in ln for ln in lines)
    ls = lrun.ls_lines()
    assert sum(d["pub_uv"] for d in ls) == 0 and sum(d["pub_present"] for d in ls) > 100
    st = shellrun.stats(lines)[1:]
    assert st and sum(r["comp_n"] for r in st) > 100


# ---------------------------------------------------------------------------------------------------------------
# premise 3: the camera at UpdateViews against the camera at Present

@pytest.mark.parametrize("move", [False, True], ids=["same", "moved-after-uv"])
def test_camera_at_updateviews_equals_present(lrun, move):
    """lockstep_check=1 reads the camera globals again at Present: equal in every frame on the stand-in; when view 0 is
    moved after UpdateViews (the negative case) every compared frame differs, in `view` only, and the log says so."""
    lrun.set_ini(**dict(INI, lockstep_check="1"))
    lrun.start_lockstep_server(seconds=SERVER_S)
    lrun.start_host(*HOST, *(("--fake-cam-move-after-uv",) if move else ()), seconds=5)
    lines = lrun.finish()
    ls = lrun.ls_lines()
    n, d = sum(x["cmp_n"] for x in ls), sum(x["cmp_diff"] for x in ls)
    assert n > 300, n
    rows = [x for x in lrun.rows() if x["pub_src"] == 1 and x["cmp"] >= 0]
    if move:
        assert d == n and all(x["cmp"] == 4 for x in rows), (n, d, {x["cmp"] for x in rows})
        assert any("differs from the UpdateViews read in view" in ln for ln in lines)
    else:
        assert d == 0 and all(x["cmp"] == 0 for x in rows), (n, d)


# ---------------------------------------------------------------------------------------------------------------
# depth: premise 4 on the stand-in

def read_depth(path):
    b = Path(path).read_bytes()
    assert b[:4] == b"C5DD", b[:4]
    w, h = struct.unpack_from("<II", b, 4)
    vals = struct.unpack_from("<%dI" % (w * h), b, 12)
    return w, h, lambda x, y: vals[y * w + x] & 0xFFFFFF


@pytest.mark.parametrize("depth_write", [1, 0])
def test_depth_written_into_the_scene_clear_dsv(lrun, tmp_path, depth_write):
    """The server draws x < 580 at depth 0.5 (1.0 elsewhere). With depth_write=1 that depth is in the EXE's D24S8 after
    the insertion: the host's units list (LESS, after the insertion) loses where it is behind 0.5 (the yellow z 0.8, the
    cyan z 0.6 inside the region) and wins in front (orange z 0.3) and outside the region; read back from the depth
    buffer and seen on screen. depth_write=0: the depth stays 1.0 where no unit drew, units drawn everywhere."""
    lrun.set_ini(**dict(INI, depth_write=str(depth_write)))
    lrun.start_lockstep_server("--region", "0,0,580,720", "--depth-value", "0.5", seconds=SERVER_S)
    dump, go = tmp_path / "depth.c5dd", tmp_path / "dump.go"
    # 0494: the dump 3 s after the hook commit (the test makes the trigger file), not at 3.5 s of the host's clock,
    # which a commit slower than about 3 s (9.07 s seen under load) would put before the composite's first frame
    lrun.start_host(*HOST, "--depth-dump", str(dump), "--depth-dump-on", str(go), seconds=7)
    time.sleep(3.0)
    go.write_text("go")
    time.sleep(1.5)
    img = shellrun.Image(lrun.shot())
    lines = lrun.finish()
    w, h, d = read_depth(dump)
    assert (w, h) == (W, H)
    D = lambda z: round(z * 0xFFFFFF)  # noqa: E731
    near = lambda v, z: abs(v - D(z)) <= 2  # noqa: E731
    yellow_in, cyan_in, cyan_out, orange, free_in, free_out = (500, 550), (550, 620), (590, 550), (650, 600), (450, 505), (1000, 300)
    ls = lrun.ls_lines()
    if depth_write:
        assert near(d(*free_in), 0.5) and near(d(*yellow_in), 0.5) and near(d(*cyan_in), 0.5), (d(*free_in), d(*yellow_in), d(*cyan_in))
        assert d(*free_out) == 0xFFFFFF and near(d(*cyan_out), 0.6) and near(d(*orange), 0.3)
        assert sum(x["depth"] for x in ls) > 100 and sum(x["depth_refused"] + x["depth_nodsv"] + x["depth_nosrc"] for x in ls[3:]) == 0, ls[-1]
        assert any("depth target: the scene-clear DSV" in ln for ln in lines)
        # on screen: the server's colour where the yellow and the inner cyan unit lost, the cyan outside the region
        for p in (yellow_in, cyan_in):
            r, g, b = img.at(*p)
            assert r == 0 and (r, g, b) not in ((255, 255, 0), (0, 255, 255)), (p, (r, g, b))
        assert img.at(*cyan_out) == (0, 255, 255)
        rows = lrun.rows()
        assert sum(x["depth"] for x in rows) > 100
    else:
        assert d(*free_in) == 0xFFFFFF and near(d(*yellow_in), 0.8) and near(d(*cyan_in), 0.6), (d(*free_in), d(*yellow_in))
        assert img.at(*yellow_in) == (255, 255, 0) and img.at(*cyan_in) == (0, 255, 255)
        assert sum(x["depth"] for x in ls) == 0


def test_depth_refused_without_a_depth_block(lrun):
    """The server publishes no depth block (shell_server.exe without --depth 1): frame ids are unknown, so lockstep never
    finds its frame (logged once, it gives up), depth is never written (depth_nosrc counted), and the picture still shows."""
    lrun.set_ini(**dict(INI, depth_write="1"))
    lrun.start_lockstep_server("--no-depth-block", seconds=SERVER_S)
    lrun.start_host(*HOST, seconds=5)
    lines = lrun.finish()
    assert sum("frame ids unknown" in ln for ln in lines) == 1
    assert sum("misses in a row" in ln for ln in lines) == 1
    ls = lrun.ls_lines()
    assert sum(x["depth"] for x in ls) == 0 and sum(x["depth_nosrc"] for x in ls) > 100
    st = shellrun.stats(lines)[1:]
    assert sum(r["srv_new"] for r in st) > 100


# ---------------------------------------------------------------------------------------------------------------
# handover=ring (issue 3302's source)

@pytest.fixture
def frame_bin():
    d = os.environ.get("C5_FRAME_BUILD")
    p = Path(d) / "bin" if d else None
    if not p or not (p / "ring_producer.exe").exists():
        pytest.fail("C5_FRAME_BUILD must name a shell\\frame\\build.cmd output with bin\\ring_producer.exe")
    return p


@pytest.mark.parametrize("sync", ["fence", "cpu"])
def test_ring_handover_lockstep(lrun, frame_bin, sync):
    """handover=ring: the composite opens the ring source, asks for its back buffer's size before its first publish, and
    takes the slot of the camera it published at UpdateViews (ring_producer draws F's colour); a ResizeBuffers (--churn)
    closes the source first (logged), a new generation follows and frames are exact again afterwards."""
    sec = camblock.Section(lrun.section)   # the version-3 section holds the camera block; ring_producer does not make it
    try:
        lrun.set_ini(**dict(INI, handover="ring", ring_sync=sync))
        log = lrun.dir / "producer.log"
        prod = shellrun.launch([str(frame_bin / "ring_producer.exe"), "--base", lrun.section, "--sync", sync, "--seconds", str(SERVER_S),
                                "--log", str(log)], lrun.desktop, console=True)
        lrun.server = prod
        lrun.start_host(*HOST, "--churn", "3", seconds=20)
        time.sleep(2.5)
        # 0494: --churn resizes every 3 s of the host's clock, and right after a resize (ring closed and re-opened) the
        # host's own gray is on screen; a shot that fell there (8 ms after one, 2026-11-17) is taken again, at most twice
        for _ in range(3):
            r, g, b = decode_shot(lrun, 1000, 300)
            if r == 0 and (g, b) != (0, 0):
                break
            time.sleep(0.7)
        # 0494: the host ends 1.5 s after its second resize (as the 8-s host of its own clock did, resizes at 3 and 6 s),
        # so the last 100 rows below follow the last resize whatever the hook commit took
        t_end = time.time() + 20
        while lrun.host_log.read_text(errors="replace").count("[churn]") < 2:
            assert lrun.host.alive() and time.time() < t_end, shellrun.tail(lrun.host_log, 6)
            time.sleep(0.1)
        time.sleep(1.5)
        lrun.close_host()
        lines = lrun.finish()
        rows = lrun.rows()
        uv = uv_rows(rows)
        assert len(uv) > 300
        exact = check_misses_justified(uv)
        check_bound(uv, qpc_freq(lrun))
        assert all(x["slot"] >= 0 for x in exact)
        assert r == 0 and g * 256 + b in {x["taken_f"] for x in rows}, (r, g, b)
        assert any("ring source open" in ln for ln in lines) and any("ring source closed (ResizeBuffers)" in ln for ln in lines)
        resize = max(i for i, ln in enumerate(lines) if "ring source closed (ResizeBuffers)" in ln)
        assert any("ring source: " in ln and "requested" in ln for ln in lines[resize:])
        ls = lrun.ls_lines()
        assert max(d.get("ring_gen", 0) for d in ls) >= 2
        # exact again after the last resize: the last 100 lockstep rows
        tail = uv[-100:]
        check_misses_justified(tail)
    finally:
        sec.close()


@pytest.mark.parametrize("unbaked", ["7", "frame", None])
def test_ring_record_carries_the_unbaked_count(lrun, frame_bin, unbaked):
    """Issue 0139: the slot record carries the count of land tiles the server drew without their bake, and the composite's
    per-frame record (the CSV's `unbaked` column) and its "lockstep 1s:" line carry the count of the slot it took.
    ring_producer --unbaked 7 reports 7 for every frame; --unbaked frame reports F % 1000, so each row must carry the count
    of the very frame it took (older takes included), not of another slot; without --unbaked the record says "not counted"
    (-1, unbaked_n 0). Rows that took nothing carry -1."""
    sec = camblock.Section(lrun.section)
    try:
        lrun.set_ini(**dict(INI, handover="ring", ring_sync="fence"))
        log = lrun.dir / "producer.log"
        extra = ["--unbaked", unbaked] if unbaked else []
        lrun.server = shellrun.launch([str(frame_bin / "ring_producer.exe"), "--base", lrun.section, "--sync", "fence",
                                       "--seconds", str(SERVER_S), "--log", str(log), *extra], lrun.desktop, console=True)
        lrun.start_host(*HOST, seconds=5)
        lrun.finish()
        rows = lrun.rows()
        taken = [x for x in rows if x["kind"] in (EXACT, OLDER, NOPUB) and x["taken_f"] > 0]
        empty = [x for x in rows if not (x["kind"] in (EXACT, OLDER, NOPUB) and x["taken_f"] > 0)]
        assert len(taken) > 200, len(taken)
        assert all(x["unbaked"] == -1 for x in empty), [x for x in empty if x["unbaked"] != -1][:2]
        if unbaked == "7":
            want = lambda x: 7
        elif unbaked == "frame":
            want = lambda x: x["taken_f"] % 1000
        else:
            want = lambda x: -1
        bad = [x for x in taken if x["unbaked"] != want(x)]
        assert not bad, (len(bad), bad[:2])
        if unbaked == "frame":
            assert len({x["unbaked"] for x in taken}) > 100   # the counts differ from frame to frame
        # the "lockstep 1s:" sums: frames with a count, of them gray, their tiles (the last interval may not be logged)
        ls = lrun.ls_lines()
        n = sum(int(d["unbaked_n"]) for d in ls)
        gf = sum(int(d["gray_frames"]) for d in ls)
        gt = sum(int(d["gray_tiles"]) for d in ls)
        if not unbaked:
            assert (n, gf, gt) == (0, 0, 0), (n, gf, gt)
        else:
            # every frame that took a slot has a count. Both ends can be cut: the CSV misses the rows after the control
            # thread's last 0.5 s flush, the 1s lines miss the frames after the render thread's last stats interval
            # (ticket 0490: with the stand-in's EXE taps off these tests ran again for the first time since 2026-11-14,
            # and the old `n >= len(taken)` lost that race in 4 of 6 runs under load). So the rows may exceed the
            # sums by at most one interval's frames.
            per = [int(d["exact"]) + int(d["older"]) + int(d["nopub"]) for d in ls]
            took = sum(per)
            assert n == took and len(taken) > 200 and n >= len(taken) - max(per), (n, took, len(taken), max(per))
            assert 0 < gf <= n and gt >= gf, (n, gf, gt)
        if unbaked == "7":
            assert gf == n and gt == 7 * n, (n, gf, gt)
    finally:
        sec.close()


@pytest.mark.parametrize("value", ["-1", "x", "100001", ""])
def test_ring_producer_refuses_a_bad_unbaked(lrun, frame_bin, value):
    """Issue 0139, the stand-in's own validation: --unbaked takes 0..100000 or `frame`; anything else exits 2 before it
    maps anything, with the message on stderr."""
    p = shellrun.launch([str(frame_bin / "ring_producer.exe"), "--base", lrun.section, "--unbaked", value, "--seconds", "5"],
                        lrun.desktop, console=True)
    assert p.wait(10) == 2


def test_shipped_ini_runs_the_ring_with_lockstep_and_depth(lrun, frame_bin):
    """Issue 3312: the shipped ini as it is (copied over the fixture's version-3 keys), only the stand-in's own keys added
    (insertion, alpha blend, the CSV): the composite opens the ring with the fence, publishes at UpdateViews and takes the
    slot of its own camera, with the depth write on."""
    shutil.copy2(shellrun.SHELL / "composite" / "shell_composite.ini", lrun.ini)
    stand_in = {k: v for k, v in INI.items() if k not in ("lockstep", "lockstep_mode")}   # 0691: the shipped mode (exact)
    lrun.set_ini(**stand_in, **EXE_TAPS_OFF)   # ticket 0490: the taps that need the real EXE stay off on the stand-in
    k = _ini_keys(lrun.ini)
    assert (k["handover"], k["lockstep"], k["depth_write"], k["ring_sync"]) == ("ring", "1", "1", "fence"), k
    sec = camblock.Section(lrun.section)
    try:
        log = lrun.dir / "producer.log"
        lrun.server = shellrun.launch([str(frame_bin / "ring_producer.exe"), "--base", lrun.section, "--sync", "fence",
                                       "--seconds", str(SERVER_S), "--log", str(log)], lrun.desktop, console=True)
        lrun.start_host(*HOST, seconds=6)
        lines = lrun.finish()
        uv = uv_rows(lrun.rows())
        assert len(uv) > 200
        exact = check_misses_justified(uv)
        assert all(x["slot"] >= 0 for x in exact)
        assert any("ring source open" in ln for ln in lines), lines[-20:]
        assert not any("CONFIG ERROR" in ln for ln in lines), [ln for ln in lines if "CONFIG ERROR" in ln]
        # ticket 0691: the shipped mode is exact: every lockstep frame took its own camera's slot, no hung or dead end
        assert k["lockstep_mode"] == "exact", k
        ls = lrun.ls_lines()
        assert ls and all(d["mode"] == "exact" for d in ls) and sum(d["hung_ends"] + d["dead_ends"] for d in ls) == 0, ls[-1:]
        assert len(exact) == len(uv), (len(exact), len(uv), [x for x in uv if x["kind"] != EXACT][:2])
    finally:
        sec.close()
