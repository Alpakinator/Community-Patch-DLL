"""Issue 3205 (epic 3200): shell_server.exe --scene terrain without the game.

A fake producer stands in for shell_composite.dll: the test writes the camera block (camblock.py, a camera of its own or
the ground-truth camera of epic 3000), the tap block and a C5HF section of its own (a synthetic field), and for the cache
tap a second shell_server.exe in `--scene bakefake` creates the manifest and solid-colour shared cache textures. Every
process runs on the session's hidden desktop (rule 14). Each negative case asserts the server's outputs (ackResult in the
section, log lines, counters, the dump) and its exit code. Skipped counts as failed (conftest.py).

Needs: `cmd /c shell\\build.cmd`, C5R_EXE (CivilizationV_DX11.exe, read-only; default the setting render_exe of
tools/release/settings.py, as render/build.py; ticket 4713) and, for the ground-truth case, D:/c5-64bit-work/i2-ground-truth (C5_GROUND_TRUTH).
"""
import json
import os
import re
import struct
import time
from pathlib import Path

import numpy as np
import pytest

import camblock
import shellrun

from test_lockstep import release_setting   # ticket 4713

EXE = release_setting("C5R_EXE", "render_exe")   # else the setting render_exe of tools/release/settings.py
GT = Path(os.environ.get("C5_GROUND_TRUTH", "D:/c5-64bit-work/i2-ground-truth"))

TAP_OFFSET, BAKETAP_OFFSET, DEPTH_OFFSET = 2048, 2560, 3072
# VPShellTap (shell_shared.h; checked against --print-layout by test_layout_v3)
TAP = {"seq": 0, "state": 4, "loadSeq": 8, "failReason": 12, "mapW": 16, "mapH": 20, "samplesX": 24, "samplesY": 28,
       "flags": 32, "exePid": 36, "payloadBytes": 40, "sectionBytes": 48, "fnv1a64": 56, "readyQpc": 64, "copyMs": 72,
       "sectionName": 80, "ackLoadSeq": 208, "ackResult": 212, "ackServerPid": 216}
DEPTH = {"seq": 0, "state": 4, "generation": 8, "width": 12, "height": 16, "dxgiFormat": 20, "encoding": 24,
         "frameCounter": 32, "textureName": 40}
BAKETAP = {"manifestSeq": 40, "ackManifestSeq": 200, "ackServerPid": 204}
TAP_READY = 2
ERR = {"Truncated": 2, "BadMagic": 3, "BadVersion": 4, "BadLayout": 5}
ACK_HASH, ACK_OPEN, ACK_STALE = -1, -2, -3
W, H = 320, 240        # server target and camera size
MAP_W, MAP_H = 6, 6    # synthetic field, plots (MAP_H includes the margin rows, as C5HF does)


# ---------------------------------------------------------------------------------------------------------------
# fake producers

def fnv1a64(b: bytes) -> int:
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def synth_field(map_w=MAP_W, map_h=MAP_H):
    """Smooth hills between z 2.4 and 4.0 (u16 3060..5100), no sample under the water height 1.0."""
    X, Y = map_w * 64, map_h * 64
    yy, xx = np.mgrid[0:Y, 0:X].astype(np.float64)
    z = 3.2 + 0.8 * np.sin(xx / 40.0) * np.cos(yy / 33.0)
    return np.round(z / 0.2 * 255.0).astype("<u2")


def c5hf(h, map_w=MAP_W, map_h=MAP_H, version=1, wrap=False, water_z=1.0, h2w=None, sx=1.0, sy=0.866, sz=0.2):
    X, Y = map_w * 64, map_h * 64
    ox, oy = map_w * 32 * sx, map_h * 32 * sy
    hdr = struct.pack("<4sIIIIII", b"C5HF", version, map_w, map_h, X, Y, 1 if wrap else 0)
    hdr += struct.pack("<6fI", sx, sy, sz, ox, oy, water_z, 0)
    hdr += struct.pack("<16f", *(h2w if h2w is not None else [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]))
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


def synth_camera(frame=1, present=None, grid_seq=1, samples=(MAP_W * 64, MAP_H * 64)):
    """A camera 300 units above the synthetic map, looking at its centre from the south (row-vector convention)."""
    view = look_at_rh((0, -220, 300), (0, 0, 3), (0, 0, 1))
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
        import ctypes
        ctypes.memmove(self.p, data, min(len(data), self.size))

    def close(self):
        camblock._k.UnmapViewOfFile(self.p)
        camblock._k.CloseHandle(self.h)


def write_tap(sec, *, load_seq, name, payload, map_w=MAP_W, map_h=MAP_H, fnv=None, state=TAP_READY, payload_bytes=None,
              copy_seq=0, fail_reason=0):
    """Publish a VPShellTap block (seqlock odd, fields, even) as the DLL does."""
    o = TAP_OFFSET
    seq = struct.unpack("<I", sec.read(o, 4))[0]
    sec.write(o, struct.pack("<I", (seq | 1) + 0 if seq & 1 else seq + 1))
    pb = len(payload) if payload_bytes is None else payload_bytes
    sec.write(o + 4, struct.pack("<IIIIIIIII", state, load_seq, fail_reason, map_w, map_h, map_w * 64, map_h * 64, 0,
                                 os.getpid()))
    sec.write(o + 40, struct.pack("<QQQqf", pb, (pb + 0xFFFF) & ~0xFFFF, fnv1a64(payload[:pb]) if fnv is None else fnv, 0, 1.0))
    sec.write(o + 76, struct.pack("<I", copy_seq))
    sec.write(o + 80, (name + "\0").encode("utf-16-le").ljust(128, b"\0"))
    seq = struct.unpack("<I", sec.read(o, 4))[0]
    sec.write(o, struct.pack("<I", seq + 1))


def tap_req_copy(sec):
    """(requestSeq, ackCopySeq) of the tap block (shell_shared.h offsets 220, 224)."""
    return struct.unpack("<II", sec.read(TAP_OFFSET + 220, 8))


def tap_ack(sec):
    seq, res, pid = struct.unpack("<IiI", sec.read(TAP_OFFSET + TAP["ackLoadSeq"], 12))
    return seq, res, pid


# ---------------------------------------------------------------------------------------------------------------
# the server

@pytest.fixture(scope="session")
def exe():
    if not EXE.exists():
        pytest.fail("C5R_EXE %s missing (CivilizationV_DX11.exe, read-only)" % EXE)
    return EXE


class Server:
    def __init__(self, tmp, desktop, built, tag):
        self.tmp, self.desktop, self.built, self.tag = Path(tmp), desktop, built, tag
        self.section = "Local\\VPShellTerrain_%d_%s" % (os.getpid(), tag)
        self.texname = "Local\\VPShellTerrainTex_%d_%s" % (os.getpid(), tag)
        self.sec = camblock.Section(self.section)
        self.log = self.tmp / ("server_%s.log" % tag)
        self.out = self.tmp / ("server_%s.out" % tag)
        self.dump = self.tmp / ("frame_%s.bmp" % tag)
        self.procs, self.taps = [], []

    def start(self, *extra, exe=None, seconds=6, dump=True, size=(W, H), unset=()):
        args = [str(self.built / "shell_server.exe"), "--section", self.section, "--name", self.texname,
                "--scene", "terrain", "--size", "%dx%d" % size, "--seconds", str(seconds), "--log", str(self.log)]
        if exe is not None:
            args += ["--exe", str(exe)]
        if dump:
            args += ["--dump", str(self.dump), "--dump-after", "1"]
        p = shellrun.launch(args + [str(a) for a in extra], self.desktop, console=True, out_path=self.out, unset=unset)
        self.procs.append(p)
        return p

    def bakefake(self, *extra, seconds=20):
        log = self.tmp / ("bakefake_%s.log" % self.tag)
        args = [str(self.built / "shell_server.exe"), "--section", self.section, "--scene", "bakefake",
                "--seconds", str(seconds), "--log", str(log)] + [str(a) for a in extra]
        p = shellrun.launch(args, self.desktop, console=True)
        self.procs.append(p)
        t_end = time.time() + 15
        while time.time() < t_end:
            if log.exists() and "ACTIVE" in log.read_text(errors="replace"):
                return p
            assert p.alive(), shellrun.tail(log, 8)
            time.sleep(0.05)
        raise TimeoutError(shellrun.tail(log, 8))

    def tap(self, load_seq=1, payload=None, **kw):
        payload = payload if payload is not None else c5hf(synth_field())
        name = "Local\\VPShellTapTest_%d_%s_%d_%d" % (os.getpid(), self.tag, load_seq, kw.get("copy_seq", 0))
        size = kw.pop("section_size", None)
        if not kw.pop("no_section", False):
            self.taps.append(TapSection(name, payload, size))
        write_tap(self.sec, load_seq=load_seq, name=name, payload=payload, **kw)

    def text(self):
        return self.log.read_text(encoding="utf-8", errors="replace") if self.log.exists() else ""

    def wait_log(self, pattern, timeout=20.0):
        rx = re.compile(pattern)
        t_end = time.time() + timeout
        while time.time() < t_end:
            m = rx.search(self.text())
            if m:
                return m
            time.sleep(0.05)
        raise TimeoutError("no %r in the server log: %s" % (pattern, self.text()[-1500:]))

    def finish(self, p, timeout=60):
        code = p.wait(timeout)
        assert code is not None, "server still running: " + self.text()[-1500:]
        return code

    def frame_counter(self):
        return struct.unpack("<q", self.sec.read(296, 8))[0]   # VPShellHeader::frameCounter (static_assert 40 + 256)

    def close(self):
        for p in self.procs:
            p.kill()
            p.close()
        for t in self.taps:
            t.close()
        self.sec.close()


@pytest.fixture
def srv(tmp_path, hidden_desktop, built, request):
    s = Server(tmp_path, hidden_desktop, built, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield s
    s.close()


def image(path):
    img = shellrun.Image(path)
    a = np.frombuffer(img.px, dtype=np.uint8).reshape(img.h, img.w, 4)
    return (a if img.top_down else a[::-1])[..., [2, 1, 0, 3]]   # RGBA


def totals(text):
    m = re.search(r"terrain totals: (.*)", text)
    assert m, text[-1500:]
    return {k.strip(): int(v) for k, v in re.findall(r"([a-z ]+?) (-?\d+)", m.group(1))}


def seconds_lines(text):
    rows = []
    for ln in text.splitlines():
        if "terrain 1s:" in ln:
            rows.append({k: v for k, v in re.findall(r"(\w+)=([-\w\.]+)", ln)})
    return rows


# ---------------------------------------------------------------------------------------------------------------
# layout

def test_layout_v3(tmp_path, hidden_desktop, built):
    out = tmp_path / "layout.json"
    p = shellrun.launch([str(built / "shell_server.exe"), "--print-layout", str(out)], hidden_desktop, console=True)
    assert p.wait(20) == 0
    p.close()
    lay = json.loads(out.read_text())
    assert lay["version"] == 3
    for k, v in TAP.items():
        assert lay["tap"][k] == v, ("tap", k)
    assert lay["tap"]["offset"] == TAP_OFFSET and lay["tap"]["sizeof"] == 288
    for k, v in DEPTH.items():
        assert lay["depth"][k] == v, ("depth", k)
    assert lay["depth"]["offset"] == DEPTH_OFFSET
    for k, v in BAKETAP.items():
        assert lay["baketap"][k] == v, ("baketap", k)
    assert lay["baketap"]["offset"] == BAKETAP_OFFSET and lay["bakeslot"]["sizeof"] == 96 and lay["bakeslot"]["offset"] == 64


# ---------------------------------------------------------------------------------------------------------------
# positive

def test_tap_block_drawn(srv, exe):
    """The tap block's C5HF section is read, hashed, acknowledged (ackResult 0, then ackLoadSeq), and the field is drawn
    with the camera block's camera: the dump shows terrain where the map is."""
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start(exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    assert tap_ack(srv.sec)[:2] == (1, 0)
    t = srv.text()
    assert re.search(r"tap load seq 1 read: 6x6 plots, \d+ bytes, hash ok", t), t[-1500:]
    assert "field 6x6 plots (384x384 samples" in t and "as 3x3 tiles of 2 plots" in t
    a = image(srv.dump)
    assert a.shape == (H, W, 4)
    drawn = a[..., :3].max(axis=2) > 0
    # the map fills the middle of the view; the corners above the horizon line stay transparent
    assert drawn[H // 3:2 * H // 3, W // 3:2 * W // 3].mean() > 0.95, drawn.mean()
    assert drawn.mean() > 0.3


def test_overlay_grid_over_transparent_terrain(srv, exe):
    """--overlay-grid 1 (pan-lag set-up of 3207): the grid's yellow hex outlines are drawn over the terrain with the
    terrain's camera; with a constant colour of alpha 0 and no water the terrain itself stays transparent, so the
    composite (blend=alpha, insert=present) shows only the outlines over the stock frame."""
    cam = synth_camera()
    # hexes of 64 samples placed in the middle of the synthetic map (the identity h2w makes 1-sample hexes in a corner)
    cam["h2w"] = [64.0, 0, 0, 0, 32.0, 64.0, 0, 0, 0, 0, 1, 0, 32.0, 160.0, 0, 1]
    srv.sec.write_camera(cam)
    srv.tap(load_seq=1)
    p = srv.start("--overlay-grid", "1", "--color", "constant", "--constant-rgba", "0x00000000", "--water", "0",
                  "--flat-z", "0", exe=exe)
    assert srv.finish(p) == 0, srv.text()[-1500:]
    t = srv.text()
    assert "grid: built for grid seq 1" in t, t[-1500:]
    a = image(srv.dump).astype(int)
    yellow = (a[..., 0] > 200) & (a[..., 1] > 200) & (a[..., 2] < 60) & (a[..., 3] > 200)
    assert yellow.sum() > 300, yellow.sum()
    other = (a[..., 3] > 0) & ~yellow
    assert other.sum() < 0.01 * a[..., 3].size, other.sum()


def test_ack_carries_copy_seq(srv, exe):
    """Issue 3206 [L 2026-11-11]: the DLL accepts an ack only with ackCopySeq == copySeq; the server writes it."""
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, copy_seq=3)
    p = srv.start(exe=exe)
    assert srv.finish(p) == 0, srv.text()[-1500:]
    assert tap_ack(srv.sec)[:2] == (1, 0) and tap_req_copy(srv.sec) == (0, 3)


@pytest.mark.parametrize("state,reason", [(3, 0), (4, 5)])
def test_new_copy_requested_when_tap_gone(srv, exe, state, reason):
    """Step 4 of the tap protocol (issue 3206): a server started after the tap was RELEASED (or FAILED ACK_TIMEOUT)
    asks once for a new copy (requestSeq + 1), then reads the DLL's new copy and acknowledges it with its copySeq."""
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, copy_seq=1, state=state, fail_reason=reason, no_section=True)
    p = srv.start(exe=exe, seconds=8)
    srv.wait_log(r"tap of load seq 1 is %s \(copy 1\) and this server has no field of it: new copy requested "
                 r"\(requestSeq 1\)" % ("RELEASED" if state == 3 else "FAILED ACK_TIMEOUT"))
    time.sleep(0.5)
    assert tap_req_copy(srv.sec)[0] == 1          # asked once, not once per poll
    srv.tap(load_seq=1, copy_seq=2)
    assert srv.finish(p) == 0, srv.text()[-1500:]
    assert tap_ack(srv.sec)[:2] == (1, 0) and tap_req_copy(srv.sec) == (1, 2)
    assert totals(srv.text())["tap loads"] == 1


def test_no_request_for_other_failures(srv, exe):
    """FAILED for another reason (here GRID_INVALID) is not asked again: the DLL could not copy the field."""
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, copy_seq=1, state=4, fail_reason=1, no_section=True)
    p = srv.start(exe=exe, seconds=3, dump=False)
    assert srv.finish(p) == 0
    assert tap_req_copy(srv.sec)[0] == 0 and "new copy requested" not in srv.text()


def test_reload_on_new_load_seq_and_pacing(srv, exe):
    """Pacing (D58 6): one draw per new camera block, none while the block does not change; a new loadSeq reloads the
    field (second ack). GPU time and the counters appear in the stats line."""
    srv.sec.write_camera(synth_camera(frame=1))
    srv.tap(load_seq=1)
    p = srv.start(exe=exe, seconds=6, dump=False)
    srv.wait_log(r"tap load seq 1 read")
    srv.wait_log(r"state NO_FIELD -> OK|state - -> OK|state NO_CAMERA -> OK")
    # 60 new blocks over about 1 s
    for f in range(2, 62):
        srv.sec.write_camera(synth_camera(frame=f))
        time.sleep(1 / 60)
    time.sleep(1.2)   # a static camera now: nothing is drawn
    srv.sec.write_camera(synth_camera(frame=62, grid_seq=2))
    srv.tap(load_seq=2)
    srv.wait_log(r"tap load seq 2 read")
    assert srv.finish(p) == 0, srv.text()[-2000:]
    t = srv.text()
    tot = totals(t)
    assert tot["drawn"] <= tot["camera blocks"] + 3, tot      # never more draws than blocks (+ field changes)
    assert tot["drawn"] >= 40, tot                            # most blocks drawn
    assert tot["tap loads"] == 2 and tot["tap errors"] == 0, tot
    assert tap_ack(srv.sec)[:2] == (2, 0)
    rows = seconds_lines(t)
    assert rows and any(float(r["gpu_ms"]) > 0 for r in rows), rows
    # a second with no new block draws nothing
    assert any(r["blocks"] == "0" and r["drawn"] == "0" for r in rows), rows
    assert srv.frame_counter() == tot["drawn"], (srv.frame_counter(), tot)


def test_field_of_old_load_not_drawn(srv, exe):
    """A map change: the camera block's grid moves to load 2 while the field is load 1's: nothing is drawn (NO_FIELD,
    the last frame kept) until load 2's tap arrives, then drawing resumes."""
    srv.sec.write_camera(synth_camera(frame=1))
    srv.tap(load_seq=1)
    p = srv.start(exe=exe, seconds=5, dump=False)
    srv.wait_log(r"-> OK")
    time.sleep(0.2)
    before = srv.frame_counter()
    for f in range(2, 20):
        srv.sec.write_camera(synth_camera(frame=f, grid_seq=2))
        time.sleep(0.02)
    srv.wait_log(r"OK -> NO_FIELD")
    time.sleep(0.2)
    assert srv.frame_counter() == before == 1
    srv.tap(load_seq=2)
    srv.wait_log(r"tap load seq 2 read")
    srv.wait_log(r"NO_FIELD -> OK")
    for f in range(20, 30):
        srv.sec.write_camera(synth_camera(frame=f, grid_seq=2))
        time.sleep(0.02)
    assert srv.finish(p) == 0
    assert srv.frame_counter() >= 5, srv.frame_counter()
    assert tap_ack(srv.sec)[:2] == (2, 0)


def test_cache_textures_used(srv, exe):
    """The bake-tap block (ACTIVE) and its manifest: each slot's shared textures are opened and set on its tile; the
    EXE's pass-1 PS draws the cache colour (red on even tiles, blue on odd ones)."""
    srv.bakefake("--fake-tiles", "3x3", "--tile-plots", "2")
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start(exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    t = srv.text()
    assert re.search(r"bake manifest seq 1 applied: 9 slots, 9 opened, 0 missing", t), t[-1500:]
    assert struct.unpack("<I", srv.sec.read(BAKETAP_OFFSET + BAKETAP["ackManifestSeq"], 4))[0] == 1
    a = image(srv.dump).astype(int)
    red = (a[..., 0] > 200) & (a[..., 1] < 40) & (a[..., 2] < 40)
    blue = (a[..., 2] > 200) & (a[..., 0] < 40) & (a[..., 1] < 40)
    assert red.sum() > 2000 and blue.sum() > 2000, (red.sum(), blue.sum())


def test_cache_textures_typeless(srv, exe):
    """Issue 3206 [L 2026-11-11]: the live bake tap shares its copies in the EXE cache's own format BC1_TYPELESS; the
    server's view must name a typed format (before the fix every live slot was 'missing'). Stand-in: R8G8B8A8_TYPELESS."""
    srv.bakefake("--fake-tiles", "3x3", "--tile-plots", "2", "--fake-typeless", "1")
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start(exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    t = srv.text()
    assert re.search(r"bake manifest seq 1 applied: 9 slots, 9 opened, 0 missing", t), t[-1500:]
    a = image(srv.dump).astype(int)
    red = (a[..., 0] > 200) & (a[..., 1] < 40) & (a[..., 2] < 40)
    assert red.sum() > 2000, red.sum()


def test_depth_published(srv, exe):
    """--depth 1: the VPShellDepth block (R32_FLOAT, NDC z) follows the colour frames; the dump's depth copy holds 1 where
    nothing is drawn and z/w in (0, 1) on the terrain."""
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start("--depth", "1", exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    d = np.fromfile(str(srv.dump) + ".depth.f32", dtype="<f4").reshape(H, W)
    a = image(srv.dump)
    drawn = a[..., :3].max(axis=2) > 0
    assert np.all(d[~drawn] == 1.0)
    assert np.all((d[drawn] > 0) & (d[drawn] < 1)), (d[drawn].min(), d[drawn].max())
    # a running server: the block's frameCounter is the colour frame's
    srv.log = srv.tmp / "server_depth_running.log"
    srv.sec.write_camera(synth_camera(frame=5))
    p = srv.start("--depth", "1", exe=exe, seconds=3, dump=False)
    srv.wait_log(r"depth generation 1")
    for f in range(6, 20):
        srv.sec.write_camera(synth_camera(frame=f))
        time.sleep(0.03)
    assert srv.finish(p) == 0
    seq, state, gen, w, h, fmt, enc = struct.unpack("<7I", srv.sec.read(DEPTH_OFFSET, 28))
    fc = struct.unpack("<q", srv.sec.read(DEPTH_OFFSET + 32, 8))[0]
    name = srv.sec.read(DEPTH_OFFSET + 40, 256).decode("utf-16-le").split("\0")[0]
    assert seq % 2 == 0 and state == 0 and (w, h, fmt, enc) == (W, H, 41, 1), (seq, state, w, h, fmt, enc)
    assert fc == srv.frame_counter() and fc >= 10, (fc, srv.frame_counter())
    assert name.startswith(srv.texname + "Depth_")
    # issue 9039: the camera the frame was drawn with (shell_shared.h camFrameStamp, proj22/proj32; was always 0)
    stamp = struct.unpack("<q", srv.sec.read(DEPTH_OFFSET + 296, 8))[0]
    p22, p32, zn, zf = struct.unpack("<4f", srv.sec.read(DEPTH_OFFSET + 304, 16))
    assert 6 <= stamp <= 19, stamp
    assert p22 < 0 and p32 < 0 and 0 < zn < zf, (p22, p32, zn, zf)


def test_ground_truth_tiny_coverage(srv, exe, tmp_path):
    """End to end with the EXE's own camera: Tiny's tapped field (ground truth of epic 3000) as a --tap-file, the
    ground-truth camera through the camera block, drawn at the back-buffer size. The share of terrain against water
    pixels equals terrainshot's at the same camera (terrain-surface.md 3: 2,152,064 of 3,603,694 px, tess 8)."""
    d = GT / "tiny"
    if not (d / "camera_v1_city_default_before.json").exists():
        pytest.fail("ground truth %s missing" % d)
    cam = json.loads((d / "camera_v1_city_default_before.json").read_text())
    grid = json.loads((d / "grid_constants.json").read_text())
    info = json.loads((d / "heightmap_info.json").read_text())
    X, Y = info["samples_x"], info["samples_y"]
    h = np.fromfile(str(d / ("heightmap_u16le_%dx%d.raw" % (X, Y))), dtype="<u2").reshape(Y, X)
    water = struct.unpack("<f", bytes.fromhex(grid["heightmap_header_hex"][:16])[4:8])[0]
    h2w = [float(v) for row in grid["M_h2w"] for v in row]
    tapf = tmp_path / "tiny.c5hf"
    tapf.write_bytes(c5hf(h, X // 64, Y // 64, wrap=grid["wrap_x"] == 1, water_z=water, h2w=h2w, sx=grid["grid_sx"],
                          sy=grid["grid_sy"], sz=grid["grid_height_scale"]))
    srv.sec.write_camera(camblock.block_from_ground_truth(cam, grid))
    bw, bh = cam["backbuffer"]
    p = srv.start("--tap-file", tapf, "--color", "constant", "--tess", "8", exe=exe, size=(bw, bh), seconds=60)
    assert srv.finish(p, 120) == 0, srv.text()[-2000:]
    a = image(srv.dump).astype(int)
    wat = (np.abs(a[..., 0] - 0x2A) <= 2) & (np.abs(a[..., 1] - 0x4A) <= 2) & (np.abs(a[..., 2] - 0x6E) <= 2)
    drawn = a[..., :3].max(axis=2) > 0
    terr = drawn & ~wat
    share, ref = terr.sum() / float(bw * bh), 2152064 / 3603694.0
    print("TINY v1: terrain %d water %d none %d (share %.4f, terrainshot %.4f)" % (terr.sum(), wat.sum(), (~drawn).sum(), share, ref))
    assert abs(share - ref) < 0.015, (share, ref)
    assert drawn.mean() > 0.99


# ---------------------------------------------------------------------------------------------------------------
# negative: each refused input is acknowledged with its reason, nothing is drawn, and the requested dump is missing
# (exit 1, "requested but not written"); the server itself keeps running to --seconds.

def _refused(srv, exe, code, pattern, seconds=3):
    p = srv.start(exe=exe, seconds=seconds)
    rc = srv.finish(p)
    t = srv.text()
    assert rc == 1 and "exit 1: --dump" in t, (rc, t[-1500:])
    assert re.search(pattern, t), t[-1500:]
    ack = tap_ack(srv.sec)
    assert ack[0] == 1 and ack[1] == code and ack[2] == p.pid, ack
    assert srv.frame_counter() == 0 and not srv.dump.exists()
    tot = totals(t)
    assert tot["drawn"] == 0 and tot["tap errors"] == 1 and tot["tap loads"] == 0, tot
    return t


def test_wrong_c5hf_version(srv, exe):
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, payload=c5hf(synth_field(), version=2))
    _refused(srv, exe, ERR["BadVersion"], r"tap load seq 1 refused \(ackResult 4\): BadVersion")


def test_bad_magic(srv, exe):
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, payload=b"XXXX" + c5hf(synth_field())[4:])
    _refused(srv, exe, ERR["BadMagic"], r"refused \(ackResult 3\): BadMagic")


def test_truncated_section(srv, exe):
    """payloadBytes past the end of the section."""
    data = c5hf(synth_field())
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, payload=data, section_size=65536, payload_bytes=len(data), fnv=1)
    _refused(srv, exe, ERR["Truncated"], r"refused \(ackResult 2\): payload \d+ bytes, section view 65536 bytes")


def test_truncated_payload(srv, exe):
    """The C5HF header announces more rows than the payload holds."""
    data = c5hf(synth_field())[:-1000]
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, payload=data)
    _refused(srv, exe, ERR["Truncated"], r"refused \(ackResult 2\): Truncated")


def test_stale_tap(srv, exe):
    """The camera block's grid belongs to load 2; the tap still holds load 1."""
    srv.sec.write_camera(synth_camera(grid_seq=2))
    srv.tap(load_seq=1)
    _refused(srv, exe, ACK_STALE, r"refused \(ackResult -3\): stale: the camera block's grid seq is 2, the tap's load seq 1")


def test_wrong_map_size_tap_block(srv, exe):
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, map_w=7)
    _refused(srv, exe, ERR["BadLayout"], r"refused \(ackResult 5\): map size: C5HF 6x6 plots, the tap block 7x6")


def test_wrong_map_size_camera(srv, exe):
    srv.sec.write_camera(synth_camera(samples=(512, 384)))
    srv.tap(load_seq=1)
    _refused(srv, exe, ERR["BadLayout"], r"refused \(ackResult 5\): map size: C5HF 384x384 samples, the camera block's grid 512x384")


def test_hash_mismatch(srv, exe):
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, fnv=0x1234)
    _refused(srv, exe, ACK_HASH, r"refused \(ackResult -1\): FNV-1a 64 [0-9A-F]{16}, the block says 0000000000001234")


def test_section_missing(srv, exe):
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, no_section=True)
    _refused(srv, exe, ACK_OPEN, r"refused \(ackResult -2\): cannot open section")


def test_missing_cache_texture(srv, exe):
    """Slot 0's textures do not exist: counted and logged, tile (0, 0) falls back, every other tile keeps its cache;
    the frame is drawn (exit 0)."""
    srv.bakefake("--fake-tiles", "3x3", "--tile-plots", "2", "--fake-missing", "0")
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start(exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    t = srv.text()
    assert re.search(r"bake slot 0: cache texture 'Local\\VPShellBakeTex\.\d+\.0\.0' missing", t), t[-1500:]
    assert "9 slots, 8 opened, 1 missing" in t
    tot = totals(t)
    assert tot["cache missing"] == 1 and tot["cache opened"] == 8 and tot["drawn"] == 1, tot
    a = image(srv.dump).astype(int)
    blue = (a[..., 2] > 200) & (a[..., 0] < 40) & (a[..., 1] < 40)
    assert blue.sum() > 2000


def test_fallback_colour_for_uncached_tile(srv, exe):
    """MC 2026-11-11 13:26/13:36: a tile without a cache is drawn in the fallback colour, here magenta (--fallback
    constant --constant-rgba 0xFFFF00FF), so terrain_compare --fallback-rgb can take it out of the colour mask."""
    srv.bakefake("--fake-tiles", "3x3", "--tile-plots", "2", "--fake-missing", "0")
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start("--fallback", "constant", "--constant-rgba", "0xFFFF00FF", exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    assert "fallback colour source 2, constant 0xFFFF00FF" in srv.text()
    a = image(srv.dump).astype(int)
    mag = (a[..., 0] > 240) & (a[..., 1] < 15) & (a[..., 2] > 240)
    red = (a[..., 0] > 200) & (a[..., 1] < 40) & (a[..., 2] < 40)
    blue = (a[..., 2] > 200) & (a[..., 0] < 40) & (a[..., 1] < 40)
    # tile (0, 0) is the near-left ninth of the map: magenta there, the caches elsewhere
    assert mag.sum() > 1000 and red.sum() > 1000 and blue.sum() > 1000, (mag.sum(), red.sum(), blue.sum())
    ys, xs = np.nonzero(mag)
    assert xs.mean() < W / 2 and ys.mean() > H / 2, (xs.mean(), ys.mean())


def test_wrong_manifest_version(srv, exe):
    """A manifest of version 2 is refused as a whole: no cache is used, the terrain is drawn with the fallback."""
    srv.bakefake("--fake-tiles", "3x3", "--tile-plots", "2", "--fake-manifest-version", "2")
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start(exe=exe)
    assert srv.finish(p) == 0, srv.text()[-2000:]
    t = srv.text()
    assert "bake manifest (seq 1) refused: manifest version 2 (want 1)" in t, t[-1500:]
    tot = totals(t)
    assert tot["manifest errors"] >= 1 and tot["cache opened"] == 0 and tot["drawn"] == 1, tot
    a = image(srv.dump).astype(int)
    red = (a[..., 0] > 200) & (a[..., 1] < 40) & (a[..., 2] < 40)
    assert red.sum() == 0 and (a[..., :3].max(axis=2) > 0).mean() > 0.3


def test_stale_camera_never_drawn(srv, exe):
    """The block is 100 Presents old from the start: STALE, nothing drawn, the stale presents counted; exit 1 because
    the requested dump was never written."""
    srv.sec.write_camera(synth_camera(frame=1, present=101))
    srv.tap(load_seq=1)
    p = srv.start(exe=exe, seconds=3)
    srv.wait_log(r"state - -> STALE|-> STALE")
    pc = camblock.offsets()["presentCount"]
    for n in range(102, 132):
        srv.sec.write(camblock.CAMERA_OFFSET + pc, struct.pack("<q", n))
        time.sleep(0.02)
    rc = srv.finish(p)
    t = srv.text()
    assert rc == 1 and "exit 1: --dump" in t
    tot = totals(t)
    assert tot["drawn"] == 0 and tot["stale presents"] >= 25, tot
    assert srv.frame_counter() == 0


def test_stale_camera_keeps_last_frame(srv, exe):
    """Good blocks are drawn; when the publisher stops while the game presents, the last frame is kept (no new frame
    published) and every such Present is counted."""
    srv.sec.write_camera(synth_camera(frame=1))
    srv.tap(load_seq=1)
    p = srv.start(exe=exe, seconds=4, dump=False)
    srv.wait_log(r"-> OK")
    for f in range(2, 12):
        srv.sec.write_camera(synth_camera(frame=f))
        time.sleep(0.03)
    time.sleep(0.3)
    published = srv.frame_counter()
    pc = camblock.offsets()["presentCount"]
    for n in range(12, 62):
        srv.sec.write(camblock.CAMERA_OFFSET + pc, struct.pack("<q", n))
        time.sleep(0.01)
    srv.wait_log(r"OK -> STALE")
    time.sleep(0.3)
    assert srv.frame_counter() == published
    assert srv.finish(p) == 0
    tot = totals(srv.text())
    assert tot["stale presents"] >= 30 and tot["drawn"] == published, (tot, published)


def test_save_tap_writes_acked_payload(srv, exe, tmp_path):
    """--save-tap (issue 3206, definition of done 4): the acknowledged payload is written byte for byte as read."""
    data = c5hf(synth_field())
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, payload=data)
    p = srv.start("--save-tap", tmp_path / "acked", exe=exe)
    assert srv.finish(p) == 0, srv.text()[-1500:]
    assert tap_ack(srv.sec)[:2] == (1, 0)
    out = tmp_path / "acked_l1.c5hf"
    assert out.read_bytes() == data
    assert re.search(r"--save-tap '.*acked_l1\.c5hf': written \(%d bytes\)" % len(data), srv.text())
    assert totals(srv.text())["tap errors"] == 0


def test_save_tap_not_written_for_refused_tap(srv, exe, tmp_path):
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1, fnv=0x1234)
    p = srv.start("--save-tap", tmp_path / "acked", exe=exe, seconds=3)
    assert srv.finish(p) == 1
    assert tap_ack(srv.sec)[1] == ACK_HASH
    assert not (tmp_path / "acked_l1.c5hf").exists() and "--save-tap" not in srv.text()


def test_save_tap_unwritable_counted_not_fatal(srv, exe, tmp_path):
    """A path that cannot be written: logged, one tap error, the field is still drawn."""
    srv.sec.write_camera(synth_camera())
    srv.tap(load_seq=1)
    p = srv.start("--save-tap", tmp_path / "no_such_dir" / "acked", exe=exe)
    assert srv.finish(p) == 0, srv.text()[-1500:]
    t = srv.text()
    assert re.search(r"--save-tap '.*acked_l1\.c5hf': NOT written", t), t[-1500:]
    tot = totals(t)
    assert tot["tap errors"] == 1 and tot["tap loads"] == 1 and tot["drawn"] >= 1, tot


def test_tap_file_invalid(srv, exe, tmp_path):
    bad = tmp_path / "bad.c5hf"
    bad.write_bytes(c5hf(synth_field(), version=9))
    p = srv.start("--tap-file", bad, exe=exe)
    assert srv.finish(p) == 5
    assert re.search(r"--tap-file '.*bad\.c5hf' refused: BadVersion", srv.text())
    p = srv.start("--tap-file", tmp_path / "none.c5hf", exe=exe)
    assert srv.finish(p) == 5 and "cannot open --tap-file" in srv.text()


def test_exe_missing(srv, tmp_path):
    p = srv.start(exe=tmp_path / "no.exe")
    assert srv.finish(p) == 4
    assert re.search(r"renderer init with '.*no\.exe' failed: Io", srv.text()), srv.text()[-800:]


@pytest.mark.parametrize("extra", [["--color", "rainbow"], ["--tile-plots", "4"], ["--depth", "2"], ["--tess", "65"],
                                   ["--fake-tiles", "0x3"], ["--fallback", "caches"], ["--constant-rgba", "zz"],
                                   ["--constant-rgba", "0x1FFFFFFFF"], ["--save-tap", ""], []])
def test_command_line_refused(srv, exe, extra):
    """Bad values exit 2 before anything starts; without --exe and C5R_EXE the terrain scene refuses too."""
    p = srv.start(*extra, exe=exe if extra else None, unset=("C5R_EXE",))
    assert srv.finish(p) == 2
    out = srv.out.read_text(errors="replace")
    assert ("bad value" in out) if extra else ("needs --exe PATH or C5R_EXE" in out), out
    assert not srv.log.exists()


# ---------------------------------------------------------------------------------------------------------------
# offline tools of issue 3206: terrain_compare.py and terrain_live.py (called in-process; nothing is launched)

import sys  # noqa: E402

sys.path.insert(0, str(shellrun.SHELL / "tools"))
import terrain_compare as tc  # noqa: E402
import terrain_live as tl  # noqa: E402

CW, CH = 400, 300


def write_bmp(path, rgba):
    """32-bit top-down BMP as shell_server.exe --dump writes it (BGRA)."""
    h, w = rgba.shape[:2]
    bgra = rgba[..., [2, 1, 0, 3]].astype(np.uint8)
    hdr = struct.pack("<2sIHHI", b"BM", 54 + w * h * 4, 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    Path(path).write_bytes(hdr + bgra.tobytes())


def write_png(path, rgba):
    from PIL import Image
    Image.fromarray(rgba.astype(np.uint8), "RGBA").save(path)


def textured(seed=1, w=CW, h=CH):
    from scipy import ndimage
    r = np.random.default_rng(seed).random((h, w, 3)) * 255
    t = np.stack([ndimage.gaussian_filter(r[..., k], 2.0) for k in range(3)], axis=2)
    t = (t - t.min()) / (t.max() - t.min()) * 200 + 20
    return np.concatenate([t, np.full((h, w, 1), 255.0)], axis=2).astype(np.uint8)


@pytest.fixture
def cmpin(tmp_path):
    """A synthetic comparison: stock = texture, server = stock + 3, passthrough = clear colour with one 'model' blob."""
    stock = textured()
    server = stock.copy()
    server[..., :3] = np.clip(stock[..., :3].astype(int) + 3, 0, 255)
    pt = np.zeros_like(stock)
    pt[..., :3] = tc.CLEAR
    pt[..., 3] = 255
    pt[140:160, 190:210, :3] = (200, 30, 30)   # a model
    p = {k: tmp_path / v for k, v in {"server": "server.bmp", "stock": "stock.png", "pt": "pt.png", "cam": "cam.json",
                                      "cap": "capture.json"}.items()}
    write_bmp(p["server"], server)
    write_png(p["stock"], stock)
    write_png(p["pt"], pt)
    p["cam"].write_text(json.dumps({"backbuffer": [CW, CH]}))
    p["cap"].write_text(json.dumps({"plots": "1,2,-,-,-,0,,rev,2;3,4,ROUTE_ROAD,IMPROVEMENT_FARM,-,0,,hid,2,FEATURE_FOREST"}))
    p["out"] = tmp_path / "res.json"
    p["arrays"] = (server, stock, pt)
    return p


def cmp_args(p, *extra, drop=()):
    a = {"--server": p["server"], "--stock": p["stock"], "--passthrough": p["pt"], "--camera": p["cam"],
         "--capture": p["cap"], "--out": p["out"], "--min-mask": "1000"}
    out = []
    for k, v in a.items():
        if k not in drop:
            out += [k, str(v)]
    return out + [str(x) for x in extra]


def test_compare_synthetic(cmpin, capsys):
    rc = tc.main(cmp_args(cmpin, "--judge-colour"))
    assert rc == 0, capsys.readouterr()
    r = json.loads(cmpin["out"].read_text())
    assert r["mask"]["base"] == "passthrough clear colour"
    # the 20 x 20 model plus 16 px around it and the 16-px frame are out
    # (a 52 x 52 square whose four 16-px corners are quarter discs: 52^2 - 4 * (256 - pi * 256 / 4) = 2484 px)
    expect = (CW - 32) * (CH - 32) - 2484
    assert abs(r["mask"]["mask_px"] - expect) < 120, (r["mask"]["mask_px"], expect)   # integer disc: 72 px more
    assert r["colour"]["mad_rgb"] == [3.0, 3.0, 3.0] and r["colour"]["pass"] and r["colour"]["within16_all_channels"] == 1.0
    assert r["mask"]["plots_not_removed"].startswith("no --gt-dir")


def test_compare_colour_fails_when_judged(cmpin, capsys):
    server, stock, pt = cmpin["arrays"]
    s2 = server.copy()
    s2[..., 0] = np.clip(stock[..., 0].astype(int) + 30, 0, 255)
    write_bmp(cmpin["server"], s2)
    assert tc.main(cmp_args(cmpin)) == 0                    # reported, not judged
    assert tc.main(cmp_args(cmpin, "--judge-colour")) == 5
    assert "colour fails" in capsys.readouterr().err


def test_compare_fallback_excluded_from_colour(cmpin, capsys):
    """MC 2026-11-11 13:36: pixels of the server's fallback colour (uncached tiles) leave the colour mask; the cached
    share of the mask is reported; median and max of the largest channel difference are given (D66)."""
    server, stock, pt = cmpin["arrays"]
    s2 = server.copy()
    s2[50:150, 50:150, :3] = (255, 0, 255)
    write_bmp(cmpin["server"], s2)
    assert tc.main(cmp_args(cmpin, "--judge-colour")) == 5          # magenta counted: fails
    assert tc.main(cmp_args(cmpin, "--judge-colour", "--fallback-rgb", "255,0,255")) == 0
    r = json.loads(cmpin["out"].read_text())
    m = r["mask"]
    assert m["uncached_px_in_mask"] == 100 * 100 and m["colour_mask_px"] == m["mask_px"] - 10000
    assert abs(m["cached_share_of_mask"] - (1 - 10000 / m["mask_px"])) < 1e-9
    c = r["colour"]
    assert c["median_max_channel"] == 3 and c["max_max_channel"] == 3 and c["pixels"] == m["colour_mask_px"]
    capsys.readouterr()
    assert tc.main(cmp_args(cmpin, "--fallback-rgb", "255,0")) == 2
    assert "want R,G,B" in capsys.readouterr().err


def test_compare_geometry_shift_recovers_known_shift(cmpin):
    server, stock, pt = cmpin["arrays"]
    sh = np.roll(np.roll(stock, 1, axis=0), 2, axis=1)       # the server picture 2 px right, 1 px down
    write_bmp(cmpin["server"], sh)
    assert tc.main(cmp_args(cmpin, "--geometry", "shift")) == 0
    g = json.loads(cmpin["out"].read_text())["geometry"]
    assert g["blocks"] >= 10 and abs(g["median_px"] - np.hypot(2, 1)) < 0.2, g


def test_compare_geometry_edges(cmpin):
    server, stock, pt = cmpin["arrays"]
    s2, p2 = server.copy(), pt.copy()
    s2[:, :150, :3] = tc.WATER_RGB                           # server water left of x 150
    p2[:, :153, :3] = (30, 60, 160)                          # EXE water left of x 153: 3 px apart
    write_bmp(cmpin["server"], s2)
    write_png(cmpin["pt"], p2)
    assert tc.main(cmp_args(cmpin, "--geometry", "edges")) == 0
    g = json.loads(cmpin["out"].read_text())["geometry"]
    assert g["matched"] > 100 and abs(g["median_px"] - 3) <= 1, g


@pytest.mark.parametrize("case", ["missing_file", "sizes", "camera_size", "no_plots", "plot_fields", "plot_rev",
                                  "not_passthrough", "mask_small", "world_size", "bmp_bpp", "bad_json"])
def test_compare_refuses(cmpin, tmp_path, capsys, case):
    args, code, msg = cmp_args(cmpin), 3, None
    if case == "missing_file":
        args, code, msg = cmp_args(cmpin, drop=("--stock",)) + ["--stock", str(tmp_path / "none.png")], 2, "no such file"
    elif case == "sizes":
        write_png(cmpin["stock"], textured(w=CW + 2))
        msg = "image sizes differ"
    elif case == "camera_size":
        cmpin["cam"].write_text(json.dumps({"backbuffer": [CW, CH + 1]}))
        msg = "camera back buffer"
    elif case == "no_plots":
        cmpin["cap"].write_text(json.dumps({"views": []}))
        msg = "no plot list"
    elif case == "plot_fields":
        cmpin["cap"].write_text(json.dumps({"plots": "1,2,-,-,-,0,,rev"}))
        msg = "has 8 fields"
    elif case == "plot_rev":
        cmpin["cap"].write_text(json.dumps({"plots": "1,2,-,-,-,0,,maybe,2"}))
        msg = "is not rev or hid"
    elif case == "not_passthrough":
        write_png(cmpin["pt"], textured(seed=3))
        msg = "not a D33 passthrough"
    elif case == "mask_small":
        args, code, msg = cmp_args(cmpin, drop=("--min-mask",)) + ["--min-mask", "10000000"], 4, "fewer than --min-mask"
    elif case == "world_size":
        w = tmp_path / "w.f32"
        np.zeros((10, 10, 4), "<f4").tofile(str(w))
        Path(str(w) + ".json").write_text(json.dumps({"width": 10, "height": 10}))
        args += ["--world", str(w)]
        msg = "world image"
    elif case == "bmp_bpp":
        b = bytearray(cmpin["server"].read_bytes())
        b[28:30] = struct.pack("<H", 24)
        cmpin["server"].write_bytes(bytes(b))
        msg = "not a 32-bit BMP"
    elif case == "bad_json":
        cmpin["cap"].write_text("{plots: ")
        msg = "JSON malformed"
    rc = tc.main(args)
    err = capsys.readouterr().err
    assert rc == code and msg in err, (rc, err)
    assert not cmpin["out"].exists()


def test_compare_ground_truth_plots(tmp_path):
    """Tiny's capture.json plots outlined with the ground-truth camera and height map: every class found (stock shot
    as both inputs, so the colour difference is 0)."""
    d = GT / "tiny"
    if not (d / "grid_constants.json").exists():
        pytest.fail("ground truth %s missing" % d)
    out = tmp_path / "gt.json"
    rc = tc.main(["--server", str(d / "shot_v1_city_default.png"), "--stock", str(d / "shot_v1_city_default.png"),
                  "--camera", str(d / "camera_v1_city_default_before.json"), "--capture", str(d / "capture.json"),
                  "--gt-dir", str(d), "--out", str(out)])
    assert rc == 0
    r = json.loads(out.read_text())
    assert r["mask"]["plots_removed_by_class"] == {"model": 6, "water": 7, "unrevealed": 2, "route": 6, "farm": 1}, r["mask"]
    assert r["colour"]["mad_rgb"] == [0.0, 0.0, 0.0] and r["mask"]["mask_px"] > 500000


# terrain_live.py: everything before the game is touched

@pytest.fixture
def livein(tmp_path):
    g = tmp_path / "gt"
    g.mkdir()
    (g / "plan.json").write_text(json.dumps({"views": [{"name": "v1_city_default", "plot": [8, 23], "ticks": 0},
                                                       {"name": "v2_city_zoomin", "plot": [8, 23], "ticks": 4}]}))
    for v in ("v1_city_default", "v2_city_zoomin"):
        (g / ("camera_%s_before.json" % v)).write_text("{}")
        (g / ("shot_%s.png" % v)).write_bytes(b"x")
    m = tmp_path / "markers"
    m.mkdir()
    (m / "RELEASED").write_text("")
    srv = tmp_path / "shell_server.exe"
    srv.write_bytes(b"")
    exe = tmp_path / "Civ.exe"
    exe.write_bytes(b"")
    base = ["--gt-dir", str(g), "--out", str(tmp_path / "out"), "--server-exe", str(srv), "--exe", str(exe),
            "--marker-dir", str(m)]
    return {"gt": g, "markers": m, "base": base, "out": tmp_path / "out"}


def test_live_dry_run(livein, capsys):
    assert tl.main(livein["base"] + ["--dry-run", "--compare", "--depth"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert [v["name"] for v in plan["views"]] == ["v1_city_default", "v2_city_zoomin"]
    assert plan["views"][1]["ticks"] == 4
    s = plan["server"]
    assert s[1:5] == ["--scene", "terrain", "--section", "Local\\VPShellSection"] and "--depth" in s
    assert "--passthrough" in plan["compare"][0] and plan["compare"][0][plan["compare"][0].index("--capture") + 1].endswith(
        "v1_city_default_plots.json")
    assert not livein["out"].exists()   # a dry run writes nothing


def test_live_plots_lua():
    s = tl.plots_lua(8, 23, 7)
    assert "local cx, cy, r = 8, 23, 7" in s and "GetFeatureType" in s and "ToggleStrategicView" not in s
    assert "@" not in s
    assert tl.highlight_points([8, 23]) == [(8, 23), (9, 23), (8, 24), (7, 22), (8, 22)]


@pytest.mark.parametrize("case,code,msg", [
    ("stop", 3, "STOP-GAMES"), ("unreleased", 3, "RELEASED"), ("no_plan", 2, "plan.json missing"),
    ("bad_plan", 2, "malformed"), ("unknown_view", 2, "views not in plan.json"), ("no_server", 2, "--server-exe"),
    ("no_camera", 2, "camera_v2_city_zoomin_before.json missing"), ("radius", 2, "--radius 40 outside")])
def test_live_refuses(livein, capsys, case, code, msg):
    args = livein["base"] + ["--dry-run"]
    if case == "stop":
        (livein["markers"] / "STOP-GAMES").write_text("")
    elif case == "unreleased":
        (livein["markers"] / "RELEASED").unlink()
    elif case == "no_plan":
        (livein["gt"] / "plan.json").unlink()
    elif case == "bad_plan":
        (livein["gt"] / "plan.json").write_text(json.dumps({"views": [{"plot": [1, 2]}]}))
    elif case == "unknown_view":
        args += ["--views", "v9"]
    elif case == "no_server":
        args[args.index("--server-exe") + 1] = str(livein["gt"] / "none.exe")
    elif case == "no_camera":
        (livein["gt"] / "camera_v2_city_zoomin_before.json").unlink()
    elif case == "radius":
        args += ["--radius", "40"]
    assert tl.main(args) == code
    assert msg in capsys.readouterr().err


# issue 3206 (W5): geometry by the EXE's hex highlight and the live frame against the offline draw; terrain_live's
# load view, mode switching and server arguments

PX = 400.0 / 60.0   # synthetic top-down camera: 6.667 px per world unit, screen = world * PX


def synth_hex_scene(shift_px=0.0, plot=(2, 2), band_px=4.0, scale=0.9):
    """A hex lattice (centre spacing 10 world units), an orthographic top-down camera, the world image of a flat
    surface at z 12 and a magenta band of +-band_px around plot's outline at `scale`, shifted by shift_px in x."""
    sys.path.insert(0, str(shellrun.SHELL.parent / "render" / "tools" / "terrainshot"))
    import highlight_accuracy as ha
    gc = {"M_h2w": [[10, 0, 0, 0], [5, 8.66, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], "grid_sx": 1.0, "grid_sy": 1.0,
          "grid_ox": 0.0, "grid_oy": 0.0}
    cam = {"views": [np.eye(4).tolist()], "proj": [[PX / (CW / 2), 0, 0, 0], [0, -PX / (CH / 2), 0, 0], [0, 0, 1, 0],
                                                    [-1, 1, 0, 1]],
           "viewport": {"X": 0, "Y": 0, "Width": CW, "Height": CH}, "backbuffer": [CW, CH]}
    yy, xx = np.mgrid[0:CH, 0:CW]
    world = np.zeros((CH, CW, 4), "<f4")
    world[..., 0], world[..., 1], world[..., 2], world[..., 3] = (xx + 0.5) / PX, (yy + 0.5) / PX, 12.0, 1.0
    corners = ha.outline(gc, plot[0], plot[1], scale)
    q = np.stack([(xx.ravel() + 0.5 - shift_px) / PX, (yy.ravel() + 0.5) / PX], axis=1)
    d = ha.nearest_on_outline(q, corners)[0]
    band = (d * PX <= band_px).reshape(CH, CW)
    plain = np.zeros((CH, CW, 4), np.uint8)
    plain[..., :3], plain[..., 3] = tc.CLEAR, 255
    hl = plain.copy()
    hl[band, :3] = (255, 0, 255)
    return gc, cam, world, hl, plain


def test_geometry_highlight_aligned_and_shifted():
    """The band method recovers an aligned outline (median 0) and sees a 3-px sideways shift (the fitted scale cannot
    absorb it): the shifted median is well above 1 px."""
    gc, cam, world, hl, plain = synth_hex_scene()
    r = tc.geometry_highlight(hl, plain, world, cam, gc, [(2, 2)])
    assert r["sections"] >= 60 and r["median_px"] < 0.3 and r["within1"] > 0.95, r
    assert "hex-highlight band" in r["method"]
    gc, cam, world, hl, plain = synth_hex_scene(shift_px=3.0)
    r = tc.geometry_highlight(hl, plain, world, cam, gc, [(2, 2)])
    assert r["median_px"] > 1.0 and r["within1"] < 0.6, r


def test_geometry_highlight_no_band():
    gc, cam, world, hl, plain = synth_hex_scene()
    r = tc.geometry_highlight(plain, plain, world, cam, gc, [(2, 2)])
    assert r["result"] == "too few band pixels on the drawn terrain" and r["magenta_px"] == 0
    with pytest.raises(tc.InputError) as e:
        tc.geometry_highlight(hl[:-1], plain, world, cam, gc, [(2, 2)])
    assert e.value.code == 3


def test_geometry_live_vs_offline_waterline():
    """A live frame whose waterline sits 2 px right of the offline draw's: median chamfer 2 px both ways, and the
    class agreement drops by the 2-px strip."""
    world = np.zeros((CH, CW, 4), "<f4")
    world[..., 3] = 1.0
    world[:, :200, 3] = 2.0                       # water left of x 200
    pt = np.zeros((CH, CW, 4), np.uint8)
    pt[..., :3], pt[..., 3] = tc.CLEAR, 255
    live = pt.copy()
    live[..., :3] = (120, 100, 60)
    live[:, :202, :3] = tc.WATER_RGB
    r = tc.geometry_live_vs_offline(live, pt, world, np.ones((CH, CW), bool))
    assert r["live_to_offline"]["median_px"] == 2.0 and r["offline_to_live"]["median_px"] == 2.0, r
    assert abs(r["class_agreement"] - (1 - 2.0 / CW)) < 1e-6, r


def test_compare_geometry_options_refused(cmpin, capsys, tmp_path):
    for extra, msg in ((["--geometry", "ruler"], "--geometry 'ruler'"),
                       (["--geometry", "highlight"], "--geometry highlight needs"),
                       (["--geometry", "live"], "--geometry live needs --passthrough and --world")):
        drop = ("--passthrough",) if extra[1] == "live" else ()
        assert tc.main(cmp_args(cmpin, *extra, drop=drop)) == 2
        assert msg in capsys.readouterr().err
    w = tmp_path / "w.f32"
    np.zeros((CH, CW, 4), "<f4").tofile(str(w))
    Path(str(w) + ".json").write_text(json.dumps({"width": CW, "height": CH}))
    (tmp_path / "grid_constants.json").write_text("{}")
    base = ["--geometry", "highlight", "--world", str(w), "--gt-dir", str(tmp_path), "--hl-on", str(cmpin["pt"]),
            "--hl-off", str(cmpin["pt"])]
    assert tc.main(cmp_args(cmpin, *base, "--hl-points", "1;2")) == 2
    assert "--hl-points '1;2'" in capsys.readouterr().err
    base[base.index("--hl-on") + 1] = str(tmp_path / "none.png")
    assert tc.main(cmp_args(cmpin, *base, "--hl-points", "1,2")) == 2
    assert "--hl-on" in capsys.readouterr().err and not cmpin["out"].exists()


def test_compare_live_geometry_end_to_end(cmpin, tmp_path):
    """--geometry live through main: the synthetic passthrough is clear except its model, the server frame is
    textured (no water): every clear pixel is 'drawn' in the live frame and in a world image of terrain only."""
    w = tmp_path / "w.f32"
    a = np.zeros((CH, CW, 4), "<f4")
    a[..., 3] = 1.0
    a.tofile(str(w))
    Path(str(w) + ".json").write_text(json.dumps({"width": CW, "height": CH}))
    assert tc.main(cmp_args(cmpin, "--geometry", "live", "--world", str(w))) == 0
    r = json.loads(cmpin["out"].read_text())["geometry"]
    assert r["class_agreement"] > 0.99 and r["live_to_offline"]["result"] == "no waterline on one side", r


def test_live_server_args_and_loadview(livein, capsys, tmp_path):
    lv = tmp_path / "lv"
    (lv / "d33").mkdir(parents=True)
    (lv / "stock").mkdir()
    (lv / "d33" / "loadview_cam_before.json").write_text("{}")
    (lv / "stock" / "loadview.png").write_bytes(b"x")
    assert tl.main(livein["base"] + ["--dry-run", "--loadview", str(lv), "--save-tap"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert [v["name"] for v in plan["views"]] == ["loadview", "v1_city_default", "v2_city_zoomin"]
    assert plan["views"][0]["plot"] is None and plan["views"][0]["plots_at"] == [100, 30]
    s = plan["server"]
    assert s[s.index("--fallback") + 1] == "constant" and s[s.index("--constant-rgba") + 1] == "0xFFFF00FF"
    assert s[s.index("--save-tap") + 1].endswith("acked")
    (lv / "stock" / "loadview.png").unlink()
    assert tl.main(livein["base"] + ["--dry-run", "--loadview", str(lv)]) == 2
    assert "loadview.png missing for the load view" in capsys.readouterr().err
    assert tl.main(livein["base"] + ["--dry-run", "--loadview", str(livein["gt"]), "--loadview-plot", "x"]) == 2
    capsys.readouterr()
    assert tl.main(livein["base"]) == 2                                  # live without --ini
    assert "--ini None: no such file" in capsys.readouterr().err
    assert not livein["out"].exists()


def test_live_set_mode(tmp_path):
    """The composite's mode is switched through its ini; the switch waits for the DLL's log line and refuses a bad
    mode or an ini without exactly one mode= line."""
    import threading
    ini = tmp_path / "shell_composite.ini"
    ini.write_text("[composite]\nmode=composite\ntap=1\n")
    lg = tmp_path / "shell_composite.log"
    lg.write_text("config file changed: mode=composite\n")

    def dll():
        time.sleep(0.6)
        with lg.open("a") as f:
            f.write("config file changed: mode=passthrough insert=tagged\n")
    th = threading.Thread(target=dll)
    th.start()
    assert tl.set_mode(ini, "passthrough", settle=0) is True
    th.join()
    assert ini.read_text().splitlines()[1] == "mode=passthrough" and "tap=1" in ini.read_text()
    with pytest.raises(tl.Refused) as e:
        tl.set_mode(ini, "off")
    assert e.value.code == 2
    ini.write_text("mode=composite\nmode=passthrough\n")
    with pytest.raises(tl.Refused) as e:
        tl.set_mode(ini, "composite")
    assert e.value.code == 2 and "2 'mode=' lines" in str(e.value)


# issue 3207 preparation (W5): the series script's offline checks (nothing is launched)

sys.path.insert(0, str(shellrun.SHELL / "evidence" / "3200"))
import w5_series_3207 as s7  # noqa: E402


@pytest.fixture
def s7in(tmp_path):
    m = tmp_path / "markers"
    m.mkdir()
    (m / "RELEASED").write_text("")
    b = tmp_path / "build"
    (b / "bin").mkdir(parents=True)
    (b / "bin" / "shell_server.exe").write_bytes(b"")
    live = tmp_path / "live"
    live.mkdir()
    (live / "shell_composite.dll").write_bytes(b"dll")
    (live / "shell_composite.ini").write_text("[composite]\nmode=composite\ninsert=tagged\ntag_rule=after\n"
                                               "tag_match=clear_bb\ntap=1\nbake_tap=1\n")
    exe = tmp_path / "Civ.exe"
    exe.write_bytes(b"")
    root = tmp_path / "inst"
    cl = root / "c5h-x" / "install" / "crashlogs"
    cl.mkdir(parents=True)
    (cl / "shell.helper").write_text(str(live / "shell_composite.dll") + "\r\n")
    user = root / "c5h-x" / "Documents" / "My Games" / "Sid Meier's Civilization 5"
    (user / "ModdedSaves" / "single").mkdir(parents=True)
    (user / "ModdedSaves" / "single" / "C5H-3200-HUGE_0350.Civ5Save").write_bytes(b"s")
    (user / "ModdedSaves" / "single" / "Private.Civ5Save").write_bytes(b"s")
    dll = user / "MODS" / "(1) Community Patch" / "CvGameCore_Expansion2.dll"
    dll.parent.mkdir(parents=True)
    dll.write_bytes(b"pinned")
    lock = tmp_path / "x.lock"
    lock.write_text("release    = SHA-256 %s (6 B)\n" % s7.sha256(dll))
    base = ["--instance", "c5h-x", "--instance-root", str(root), "--build", str(b), "--live", str(live), "--exe",
            str(exe), "--out", str(tmp_path / "out"), "--marker-dir", str(m), "--dll-lock", str(lock)]
    return {"base": base, "markers": m, "ini": live / "shell_composite.ini", "dll": dll, "out": tmp_path / "out",
            "helper": cl / "shell.helper"}


def test_s7_rotation_and_dry_run(s7in, capsys):
    assert s7.rotation(["a", "b", "c"], 3) == [["a", "b", "c"], ["b", "c", "a"], ["c", "a", "b"]]
    assert s7.main(["relaunch"] + s7in["base"] + ["--save", "C5H-3200-HUGE_0350", "--dry-run"]) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["arms"] == ["stock", "d33t"] and len(plan["order"]) == 6 and plan["order"][1] == ["d33t", "stock"]
    assert plan["view_plot"] == [100, 30]
    assert not s7in["out"].exists()


@pytest.mark.parametrize("case,code,msg", [
    ("stop", 3, "STOP-GAMES"), ("unreleased", 3, "RELEASED"), ("arm", 2, "unknown ['x']"),
    ("rounds", 2, "at least six"), ("no_build", 2, "--build is required"), ("art_save", 2, "series' own copies"),
    ("no_save", 2, "save "), ("ini_dup", 2, "key mode found 2 times"), ("helper", 2, "does not name"),
    ("dll", 2, "not the pinned test DLL"), ("plot", 2, "--view-plot"), ("window", 2, "--window at least 10 s")])
def test_s7_refusals(s7in, capsys, case, code, msg):
    args = ["relaunch"] + s7in["base"] + ["--save", "C5H-3200-HUGE_0350"]
    if case == "stop":
        (s7in["markers"] / "STOP-GAMES").write_text("")
    elif case == "unreleased":
        (s7in["markers"] / "RELEASED").unlink()
    elif case == "arm":
        args += ["--arms", "stock,x"]
    elif case == "rounds":
        args += ["--rounds", "5"]
    elif case == "no_build":
        i = args.index("--build")
        del args[i:i + 2]
    elif case == "art_save":
        args[args.index("--save") + 1] = "Private"
    elif case == "no_save":
        args[args.index("--save") + 1] = "C5H-3200-NONE"
    elif case == "ini_dup":
        s7in["ini"].write_text(s7in["ini"].read_text() + "mode=passthrough\n")
    elif case == "helper":
        s7in["helper"].write_text("C:\\elsewhere\\shell_composite.dll\r\n")
    elif case == "dll":
        s7in["dll"].write_bytes(b"other")
    elif case == "plot":
        args += ["--view-plot", "1"]
    elif case == "window":
        args += ["--window", "5"]
    assert s7.main(args) == code
    assert msg in capsys.readouterr().err
    assert not s7in["out"].exists()


def test_s7_set_ini(tmp_path):
    ini = tmp_path / "a.ini"
    ini.write_text("mode=composite\ntap=1\n")
    s7.set_ini(ini, {"mode": "passthrough", "tap": "0"})
    assert ini.read_text() == "mode=passthrough\ntap=0\n"
    with pytest.raises(s7.Refused) as e:
        s7.set_ini(ini, {"bake_tap": "1"})
    assert e.value.code == 2


def test_plots_visibility_field_and_fogged_class():
    """11th plot field (issue 3206): vis|fog; --exclude-fogged removes fogged plots as a diagnostic class."""
    s = tl.plots_lua(8, 23, 7)
    assert "IsVisible(Game.GetActiveTeam(), false)" in s and '"vis" or "fog"' in s
    p = tc.parse_plots({"plots": "1,2,-,-,-,0,,rev,2,-,fog;3,4,-,-,-,0,,rev,2,-,vis;5,6,-,-,-,0,,rev,2"})
    assert [x["visible"] for x in p] == [False, True, None]
    assert tc.plot_class(p[0], True) is None and tc.plot_class(p[0], True, fogged_out=True) == "fogged"
    assert tc.plot_class(p[1], True, fogged_out=True) is None and tc.plot_class(p[2], True, fogged_out=True) is None
    with pytest.raises(tc.InputError) as e:
        tc.parse_plots({"plots": "1,2,-,-,-,0,,rev,2,-,maybe"})
    assert e.value.code == 3 and "vis or fog" in str(e.value)
    with pytest.raises(tc.InputError):
        tc.parse_plots({"plots": "1,2,-,-,-,0,,rev,2,-,vis,extra"})


def test_compare_exclude_fogged_reported(cmpin, capsys):
    cmpin["cap"].write_text(json.dumps({"plots": "1,2,-,-,-,0,,rev,2,-,fog"}))
    assert tc.main(cmp_args(cmpin, "--exclude-fogged")) == 0
    r = json.loads(cmpin["out"].read_text())
    assert r["mask"]["fogged_plots_excluded"] is True
