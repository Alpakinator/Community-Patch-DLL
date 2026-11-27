"""Fake composite (producer half) and process helpers for the tests of the renderer inside server.exe (issue 3301).

The synthetic field, camera and tap helpers are copied from shell/tests/test_terrain_scene.py (this repository, commit
b4f7748; synth_field, c5hf, look_at_rh, perspective_fov_rh, synth_camera, TapSection, write_tap, tap_ack), so these
tests drive the server's renderer with exactly the inputs shell_server.exe --scene terrain is tested with. The camera
block goes through shell/tests/camblock.py (imported). The consumer half is fake_consumer.exe (C++, keyed mutex).
"""
import ctypes
import json
import os
import re
import struct
import time
from ctypes import wintypes
from pathlib import Path

import numpy as np

import camblock
import shellrun

TAP_OFFSET, BAKETAP_OFFSET, DEPTH_OFFSET = 2048, 2560, 3072
TAP_READY = 2
W, H = 320, 240        # target and camera size
MAP_W, MAP_H = 6, 6    # synthetic field, plots (MAP_H includes the margin rows, as C5HF does)
# vp_frame.h
FRAME_SERVER_FMT = "<IIIIIIqqqqqqqqI"   # magic .. deviceLostTotal (offsets 0..80), pacing (88, issue 3306)
PACE_EVENT, PACE_LEGACY = 1, 2
VPFRAME_MAGIC = 0x52465056
STATES = {0: "NONE", 1: "STARTING", 2: "READY", 3: "REBUILDING", 4: "FAILED", 5: "STOPPED"}
PUBLISH_OFFSET, RING_OFFSET, SLOTS_OFFSET, SLOT_BYTES = 256, 1024, 1536, 128
# ticket 0496: a run that ends through --stop-event (Rig.start_until) has --seconds only as its bound, and a wait for
# the event a test checks has WAIT_S. Both are far above what an unloaded run needs (a stand-in bake-ahead with 0365's
# 2 s decal hold: about 2.5 s plain, 4.5 s under ASan) so that a loaded or sanitized machine only makes a run longer
ASAN = bool(os.environ.get("C5_ASAN_LOG_DIR"))
BOUND_S = 300 if ASAN else 120
WAIT_S = 120.0 if ASAN else 60.0


def env_keys():
    """Ticket 0753: C5_RENDER_TEST_KEYS = "key=value;key=value" adds render keys to every config a Rig writes (a test's own
    keys win). The suite's arm "record ahead under the D3D debug layer" runs with
    C5_RENDER_TEST_KEYS="render_record_ahead=1;render_test_record_ahead_always=1;render_test_d3d_debug=1" and then reads
    every render.log for "LOUD: d3d debug:" lines (server/evidence/0753/debug_scan_0753.py)."""
    out = {}
    for kv in os.environ.get("C5_RENDER_TEST_KEYS", "").split(";"):
        if "=" in kv:
            k, v = kv.split("=", 1)
            if k.strip():
                out[k.strip()] = v.strip()
    return out


# ticket 0753: the device-creation flags the environment's keys add (D3D11_CREATE_DEVICE_DEBUG 0x2 with the debug layer)
ENV_DEVICE_FLAGS = 0x2 if env_keys().get("render_test_d3d_debug") == "1" else 0

_k = ctypes.WinDLL("kernel32", use_last_error=True)
_k.CreateEventW.restype = wintypes.HANDLE
_k.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
_k.SetEvent.argtypes = [wintypes.HANDLE]
_k.CloseHandle.argtypes = [wintypes.HANDLE]
_k.QueryPerformanceCounter.argtypes = [ctypes.POINTER(ctypes.c_int64)]


def qpc():
    v = ctypes.c_int64()
    _k.QueryPerformanceCounter(ctypes.byref(v))
    return v.value


def qpc_freq():
    v = ctypes.c_int64()
    _k.QueryPerformanceFrequency(ctypes.byref(v))
    return v.value


def fnv1a64(b: bytes) -> int:
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def synth_field(map_w=MAP_W, map_h=MAP_H):
    X, Y = map_w * 64, map_h * 64
    yy, xx = np.mgrid[0:Y, 0:X].astype(np.float64)
    z = 3.2 + 0.8 * np.sin(xx / 40.0) * np.cos(yy / 33.0)
    return np.round(z / 0.2 * 255.0).astype("<u2")


def c5hf(h, map_w=MAP_W, map_h=MAP_H, version=1, water_z=1.0, sx=1.0, sy=0.866, sz=0.2):
    X, Y = map_w * 64, map_h * 64
    ox, oy = map_w * 32 * sx, map_h * 32 * sy
    hdr = struct.pack("<4sIIIIII", b"C5HF", version, map_w, map_h, X, Y, 0)
    hdr += struct.pack("<6fI", sx, sy, sz, ox, oy, water_z, 0)
    hdr += struct.pack("<16f", *[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    hdr += struct.pack("<II", 0, 0)
    assert len(hdr) == 128
    return hdr + np.ascontiguousarray(h).tobytes()


def look_at_rh(eye, at, up):
    eye, at, up = (np.array(v, float) for v in (eye, at, up))
    z = eye - at
    z /= np.linalg.norm(z)
    x = np.cross(up, z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    return np.array([[x[0], y[0], z[0], 0], [x[1], y[1], z[1], 0], [x[2], y[2], z[2], 0],
                     [-x @ eye, -y @ eye, -z @ eye, 1]])


def perspective_fov_rh(fov_y, aspect, zn, zf):
    ys = 1.0 / np.tan(fov_y / 2)
    return np.array([[ys / aspect, 0, 0, 0], [0, ys, 0, 0], [0, 0, zf / (zn - zf), -1], [0, 0, zn * zf / (zn - zf), 0]])


def synth_camera(frame=1, present=None, grid_seq=1, samples=(MAP_W * 64, MAP_H * 64), pan=0.0):
    """A camera 300 units above the synthetic map, looking at its centre from the south; pan moves it along x."""
    view = look_at_rh((pan, -220, 300), (pan, 0, 3), (0, 0, 1))
    proj = perspective_fov_rh(np.radians(45), W / H, 10.0, 2000.0)
    cam_world = np.linalg.inv(view)
    ident = [1.0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    return {"status": 1, "presentCount": present if present is not None else frame, "frameStamp": frame, "readQpc": 0,
            "exeBase": 0x400000, "viewMode": 1, "numViews": 1, "flags": 0, "proj": list(proj.ravel()),
            "view": list(view.ravel()) + [0.0] * 48, "camWorld": list(cam_world.ravel()), "fovDeg": 45.0,
            "aspect": W / H, "nearZ": 10.0, "farZ": 2000.0, "vpX": 0, "vpY": 0, "vpW": W, "vpH": H, "vpMinZ": 0.0,
            "vpMaxZ": 1.0, "bbW": W, "bbH": H, "gridSeq": grid_seq, "gridValid": 1, "wrapX": 0,
            "wrapPeriod": float(samples[0] - 1), "aabbMin": [0, 0, 0], "aabbMax": [0, 0, 0], "worldHeight": 0.0,
            "w2h": ident, "h2w": ident, "sx": 1.0, "sy": 0.866, "heightScale": 0.2, "ox": MAP_W * 32.0,
            "oy": MAP_H * 32 * 0.866, "samplesX": samples[0], "samplesY": samples[1], "heightmapAddr": 0}


class TapSection:
    """A pagefile-backed section holding a C5HF image (what shell_composite.dll creates per load)."""

    def __init__(self, name, data: bytes, size=None):
        k = camblock._k
        self.size = size or len(data)
        self.h = k.CreateFileMappingW(camblock.INVALID_HANDLE_VALUE, None, camblock.PAGE_READWRITE, 0, self.size, name)
        assert self.h, "CreateFileMappingW(%s)" % name
        self.p = k.MapViewOfFile(self.h, camblock.FILE_MAP_ALL_ACCESS, 0, 0, self.size)
        assert self.p
        ctypes.memmove(self.p, data, min(len(data), self.size))

    def close(self):
        camblock._k.UnmapViewOfFile(self.p)
        camblock._k.CloseHandle(self.h)


def write_tap(sec, *, load_seq, name, payload, map_w=MAP_W, map_h=MAP_H, fnv=None, state=TAP_READY, copy_seq=0):
    o = TAP_OFFSET
    seq = struct.unpack("<I", sec.read(o, 4))[0]
    sec.write(o, struct.pack("<I", seq | 1 if seq & 1 else seq + 1))
    pb = len(payload)
    sec.write(o + 4, struct.pack("<IIIIIIIII", state, load_seq, 0, map_w, map_h, map_w * 64, map_h * 64, 0, os.getpid()))
    sec.write(o + 40, struct.pack("<QQQqf", pb, (pb + 0xFFFF) & ~0xFFFF, fnv1a64(payload) if fnv is None else fnv, 0, 1.0))
    sec.write(o + 76, struct.pack("<I", copy_seq))
    sec.write(o + 80, (name + "\0").encode("utf-16-le").ljust(128, b"\0"))
    seq = struct.unpack("<I", sec.read(o, 4))[0]
    sec.write(o, struct.pack("<I", seq + 1))


def tap_ack(sec):
    return struct.unpack("<IiI", sec.read(TAP_OFFSET + 208, 12))


class FrameSection(camblock.Section):
    """<base>.Frame (vp_frame.h), created or opened; read the server block, write the publish block as a composite."""

    def server(self):
        v = struct.unpack(FRAME_SERVER_FMT, self.read(0, struct.calcsize(FRAME_SERVER_FMT)))
        keys = ["magic", "version", "serverPid", "state", "failReason", "pad0", "qpcFrequency", "heartbeatQpc",
                "lastDrawnFrameId", "drawsTotal", "camerasSkipped", "skippedBusyTotal", "drawErrorsTotal", "deviceLostTotal",
                "pacing"]
        return dict(zip(keys, v))

    def publish(self, frame_id, bound_us=3400, lockstep=1, exe_pid=None):
        o = PUBLISH_OFFSET
        seq = struct.unpack("<I", self.read(o, 4))[0]
        seq = (seq | 1) if not seq & 1 else seq
        self.write(o, struct.pack("<I", seq))
        now = qpc()
        freq = qpc_freq()
        self.write(o + 4, struct.pack("<Iqqq", 1, frame_id, now, now + bound_us * freq // 1_000_000))
        self.write(o + 32, struct.pack("<II", bound_us, lockstep))
        self.write(o, struct.pack("<I", seq + 1))
        self.write(o + 40, struct.pack("<I", exe_pid if exe_pid is not None else os.getpid()))

    def tap_present(self, present):
        """Ticket 0713: the composite's tap number (VPFramePublish::tapPresent, offset 176): every tap of that Present written."""
        self.write(PUBLISH_OFFSET + 176, struct.pack("<q", present))

    def request(self, slots, width, height, sync):
        o = PUBLISH_OFFSET
        self.write(o + 40, struct.pack("<IIIII", os.getpid(), slots, width, height, sync))
        rs = struct.unpack("<I", self.read(o + 60, 4))[0]
        self.write(o + 60, struct.pack("<I", rs + 1))

    def ring(self):
        seq, gen, slots, w, h, cf, df, enc, sync, ack = struct.unpack("<10I", self.read(RING_OFFSET, 40))
        latest, = struct.unpack("<q", self.read(RING_OFFSET + 56, 8))
        return {"seq": seq, "generation": gen, "slotCount": slots, "width": w, "height": h, "sync": sync, "ackReqSeq": ack,
                "latestFrameId": latest}

    def slot(self, i):
        o = SLOTS_OFFSET + i * SLOT_BYTES
        seq, state, frame_id, fence = struct.unpack("<IIqQ", self.read(o, 24))
        draw_result, tiles = struct.unpack("<II", self.read(o + 64, 8))
        unbaked, counted = struct.unpack("<II", self.read(o + 88, 8))   # issue 0139 (vp_frame.h VPFrameSlot)
        return {"seq": seq, "state": state, "frameId": frame_id, "fenceValue": fence, "drawResult": draw_result, "tilesDrawn": tiles,
                "tilesUnbaked": unbaked, "unbakedCounted": counted}


class Event:
    def __init__(self, name):
        self.h = _k.CreateEventW(None, False, False, name)
        assert self.h

    def set(self):
        _k.SetEvent(self.h)

    def close(self):
        _k.CloseHandle(self.h)


class Rig:
    """One test: a section of its own, the fake composite's producer half (camera block, tap), the render host test
    program and the fake consumer, all on the hidden desktop."""

    def __init__(self, tmp, desktop, built, exe, tag):
        self.tmp, self.desktop, self.built, self.exe, self.tag = Path(tmp), desktop, built, exe, tag
        self.section = "Local\\VPRenderTest_%d_%s" % (os.getpid(), tag)
        self.texbase = "Local\\VPRenderTestTex_%d_%s" % (os.getpid(), tag)
        self.sec = camblock.Section(self.section)
        self.frame = FrameSection(self.section + ".Frame")
        self.log = self.tmp / ("render_%s.log" % tag)
        self.out = self.tmp / ("host_%s.out" % tag)
        self.dump = self.tmp / ("frame_%s.bmp" % tag)
        self.procs, self.taps, self.events = [], [], []
        self.stops = {}   # ticket 0496: id(process) -> its --stop-event (start_until)

    def config(self, **keys):
        # issue 3308: the server's own bake is the default now; the tests of 3301 / 3306 were written for the EXE-cache
        # tap and keep it (render_tiles=tap) unless a test names another source
        cfg = {"render_section": self.section, "render_texture": self.texbase, "render_exe": str(self.exe),
               "render_log": str(self.log), "render_size": "%dx%d" % (W, H), "render_tiles": "tap"}
        cfg.update(env_keys())   # ticket 0753: C5_RENDER_TEST_KEYS (the test's own keys below win)
        cfg.update({k: str(v) for k, v in keys.items()})
        p = self.tmp / ("server_%s.cfg" % self.tag)
        p.write_text("".join("%s=%s\n" % (k, v) for k, v in cfg.items() if v is not None and v != "None"), encoding="utf-8")
        return p

    def start(self, seconds=5, feed_events=0, out=None, feed_plots=0, options_file=None, game_dir=None, feed_delay_ms=0,
              extra=None, **keys):
        args = [str(self.built / "render_host_test.exe"), "--config", str(self.config(**keys)), "--seconds", str(seconds),
                "--run-dir", str(self.tmp), "--game-dir", str(game_dir or self.tmp)]
        if options_file:   # issue 3308: SrvOptions() of the test host
            args += ["--options-file", str(options_file)]
        if feed_events:
            args += ["--feed-events", str(feed_events)]
        if feed_plots:
            args += ["--feed-plots", str(feed_plots)]
        if feed_delay_ms:
            args += ["--feed-delay-ms", str(feed_delay_ms)]
        args += list(extra or [])
        p = shellrun.launch(args, self.desktop, console=True, out_path=out or self.out)
        self.procs.append(p)
        return p

    def consumer(self, frames, timeout=15, request=None, out=None):
        out = out or self.tmp / ("consumer_%s.jsonl" % self.tag)
        args = [str(self.built / "fake_consumer.exe"), "--section", self.section, "--frames", str(frames), "--timeout",
                str(timeout), "--out", str(out)]
        if request:
            args += ["--request", "%dx%d" % request]
        p = shellrun.launch(args, self.desktop, console=True)
        self.procs.append(p)
        return p, out

    def tap(self, load_seq=1, payload=None, **kw):
        payload = payload if payload is not None else c5hf(synth_field())
        name = "Local\\VPRenderTap_%d_%s_%d_%d" % (os.getpid(), self.tag, load_seq, kw.get("copy_seq", 0))
        self.taps.append(TapSection(name, payload))
        write_tap(self.sec, load_seq=load_seq, name=name, payload=payload, **kw)

    def text(self):
        return self.log.read_text(encoding="utf-8", errors="replace") if self.log.exists() else ""

    def stdout(self):
        return self.out.read_text(encoding="utf-8", errors="replace") if self.out.exists() else ""

    def stop_event(self):
        """Issue 0333: a named event for render_host_test.exe --stop-event; returns (event, the extra arguments)."""
        name = "Local\\VPRenderTestStop_%d_%s_%d" % (os.getpid(), self.tag, len(self.events))
        ev = Event(name)
        self.events.append(ev)
        return ev, ["--stop-event", name]

    def wait_second_line(self, pred, timeout=60.0, p=None):
        """Issue 0333: waits for a 'host 1s:' line (its counters as a dict, second_lines) for which pred is true and
        returns it: a test waits for a count instead of asserting what a fixed time gave on a loaded machine. Raises when
        the timeout passes or the process p ends first."""
        t_end = time.time() + timeout
        while True:
            ended = p is not None and not p.alive()   # read before the lines: a line written before the exit is seen
            for d in second_lines(self.text()):
                if pred(d):
                    return d
            if ended or time.time() >= t_end:
                break
            time.sleep(0.05)
        sec = second_lines(self.text())
        raise TimeoutError("no host 1s line met the condition (last %s): %s" % (sec[-1:], self.stdout()[-1500:]))

    def wait_log(self, pattern, timeout=WAIT_S, p=None, log=None):
        """Waits for pattern in render.log (log: another log file); raises when timeout passes, or (p given) when that
        process ends first. Ticket 0496: the default timeout is WAIT_S (was 20 s)."""
        return self._wait_text(_reader(log) if log else self.text, "render.log", pattern, timeout, p)

    def wait_stdout(self, pattern, timeout=WAIT_S, p=None):
        """Ticket 0496: as wait_log, in the host's stdout (its "result: ..." lines, the fatal hook's line)."""
        return self._wait_text(self.stdout, "stdout", pattern, timeout, p)

    def _wait_text(self, read, what, pattern, timeout, p):
        rx = re.compile(pattern)
        t_end = time.time() + timeout
        while True:
            ended = p is not None and not p.alive()   # read before the text: a line written before the exit is seen
            m = rx.search(read())
            if m:
                return m
            if ended or time.time() >= t_end:
                break
            time.sleep(0.05)
        raise TimeoutError("no %r in %s%s: %s" % (pattern, what, " (the process ended)" if ended else "", read()[-2000:]))

    def start_until(self, bound=None, extra=None, **kw):
        """Ticket 0496: start() with a --stop-event; --seconds is only the bound (BOUND_S). The test waits for the event
        it checks (wait_log, wait_residency, ...) and ends the run with stop(p): a fixed run length ended before 0365's
        2 s decal hold and the bake-ahead did on a loaded machine or under ASan (research/08-performance/
        bake-ahead-after-0365.md 6.5 and 6.6)."""
        ev, ex = self.stop_event()
        p = self.start(seconds=bound or BOUND_S, extra=list(extra or []) + ex, **kw)
        self.stops[id(p)] = ev
        return p

    def stop(self, p, timeout=90):
        """Ticket 0496: sets the --stop-event of a start_until process and returns its exit code (finish). The timeout
        leaves room for HostStop's 5 s and the test host's wait for a render thread HostStop left running (up to 60 s,
        render_host_test.cpp)."""
        self.stops[id(p)].set()
        return self.finish(p, timeout)

    def wait_residency(self, pred, timeout=WAIT_S, p=None, log=None, after=0):
        """Ticket 0496: waits for a residency part of a 'terrain 1s:' line (residency_lines) for which pred is true, from
        the after-th line on, and returns (its index, its counters). log: another render.log than the rig's. Raises when
        timeout passes or the process p ends first."""
        read = _reader(log) if log else self.text
        t_end = time.time() + timeout
        while True:
            ended = p is not None and not p.alive()
            lines = residency_lines(read())
            for i in range(after, len(lines)):
                if pred(lines[i]):
                    return i, lines[i]
            if ended or time.time() >= t_end:
                break
            time.sleep(0.05)
        lines = residency_lines(read())
        raise TimeoutError("no residency line met the condition%s (last %s): %s" % (" (the process ended)" if ended else "",
                           lines[-1:], self.stdout()[-1500:]))

    def wait_next_residency(self, p=None, n=1, pred=None, log=None):
        """Ticket 0496: waits for the n-th residency line after those already written (for which pred is true, if given):
        the renderer has taken what the test did before the call into its per-second counters. Returns its counters."""
        read = _reader(log) if log else self.text
        return self.wait_residency(pred or (lambda d: True), p=p, log=log, after=len(residency_lines(read())) + n - 1)[1]

    def wait_next_second(self, p=None, n=1):
        """Ticket 0496: waits for the n-th 'host 1s:' line after those already written (the render thread went on past
        what the test did before the call). Returns its counters."""
        n0 = len(second_lines(self.text()))
        return self.wait_second_line(lambda d: len(second_lines(self.text())) >= n0 + n, timeout=WAIT_S, p=p)

    def finish(self, p, timeout=90):   # ticket 0496: was 60 s (HostStop's 5 s + the test host's wait, render_host_test.cpp)
        code = p.wait(timeout)
        assert code is not None, "still running: " + self.text()[-1500:]
        return code

    def header_frame_counter(self):
        return struct.unpack("<q", self.sec.read(296, 8))[0]

    def close(self):
        for p in self.procs:
            p.kill()
            p.close()
        for t in self.taps:
            t.close()
        for e in self.events:
            e.close()
        self.frame.close()
        self.sec.close()


def second_lines(text):
    """The render thread's 'host 1s:' lines (srv_render_host.cpp SecondLine; cumulative counters) as dicts of ints."""
    return [dict((k, int(v)) for k, v in re.findall(r"(\w+)=(-?\d+)", ln.split("|")[0])) for ln in text.splitlines()
            if "host 1s:" in ln]


def _reader(path):
    """A function returning the text of path ('' while it does not exist)."""
    path = Path(path)
    return lambda: path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def residency_lines(text):
    """The residency part of the per-second terrain lines ('| residency land=... |'), as dicts of numbers (the parser of
    test_residency_source.py and test_bake_yield.py, ticket 0496)."""
    out = []
    for ln in text.splitlines():
        m = re.search(r"\| residency (land=.*?) \|", ln)
        if m:
            out.append({k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", m.group(1))})
    return out


def ahead_done(d):
    """Ticket 0496: a residency line of a completed bake-ahead (every land tile resident, none pending)."""
    return d.get("ahead_done") == 1 and d.get("pending") == 0


def summary(stdout):
    m = re.search(r"result: render: (.*)", stdout)
    assert m, stdout[-2000:]
    s = m.group(1)
    out = {"state": re.search(r"state (\w+)", s).group(1)}
    for k, v in re.findall(r"([a-z ]+?) (-?\d+)(?:,|$| )", s):
        out[k.strip()] = int(v)
    return out, s


def totals(text):
    m = re.search(r"terrain totals: (.*)", text)
    assert m, text[-1500:]
    return {k.strip(): int(v) for k, v in re.findall(r"([a-z ]+?) (-?\d+)", m.group(1))}


def frames(path):
    rows = []
    for ln in Path(path).read_text().splitlines():
        if not ln.startswith('{"frame"'):
            continue
        d = json.loads(ln)
        if "frame" in d:
            rows.append(d)
    return rows


def image(path):
    img = shellrun.Image(path)
    a = np.frombuffer(img.px, dtype=np.uint8).reshape(img.h, img.w, 4)
    return (a if img.top_down else a[::-1])[..., [2, 1, 0, 3]]   # RGBA
