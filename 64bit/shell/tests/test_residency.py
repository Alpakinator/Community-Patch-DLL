"""Issue 3407 (epic 3400 scope 5): residency, bake-ahead, the radius option and invalidation in shell_server.exe --scene
terrain --bake standin, without the game.

The server runs as a product (its command line, log, counters, exit code) on the session's hidden desktop (rule 14). The
baker is the stand-in (server/standin_baker.h: one flat BC1 colour per tile, the contract's sizes) until 3404's TileBaker
lands. Fields are synthetic C5HF files; the camera is written into the section by the test; the tile generation block
(server/tilegen_block.h) is a section the test writes. Every negative case asserts the outputs and the exit code.

Needs: `cmd /c shell\\build.cmd` (C5_SHELL_BUILD) and C5R_EXE (CivilizationV_DX11.exe, read-only).
"""
import ctypes
import os
import re
from pathlib import Path
import struct
import threading
import time

import numpy as np
import pytest

import camblock
import shellrun
from test_lockstep import release_setting   # ticket 4713
from test_terrain_scene import EXE, Server, W, H, c5hf, look_at_rh, perspective_fov_rh, synth_camera  # noqa: F401

TX, TY = 32, 20            # synthetic map in tiles (= plots of 64 samples)
# The class window reaches 4 samples into the neighbours: samples of columns 0..2 are 0, so tile columns 0 and 1 are
# class 1 (highest < 2 in u16/255); samples of columns 29..31 are u16 2550 (10.0), so tile columns 30 and 31 are class 2;
# every other tile is class 3 (its window touches the hills).
DEEP_COLS = 2              # class-1 tile columns
MID_COLS = 2               # class-2 tile columns
CACHE_BYTES = 43680        # one BC1 256 x 256 cache with 6 mips


def field(tx=TX, ty=TY, bump=None):
    """Class 3 hills (u16 13000..15000), samples of columns 0..2 at 0, of columns tx-3..tx-1 at 2550.
    bump=(x, y): one sample raised in the middle of that tile."""
    X, Y = tx * 64, ty * 64
    yy, xx = np.mgrid[0:Y, 0:X].astype(np.float64)
    h = 14000 + 1000 * np.sin(xx / 50.0) * np.cos(yy / 37.0)
    h[:, :(DEEP_COLS + 1) * 64] = 0
    h[:, (tx - MID_COLS - 1) * 64:] = 2550
    h = np.round(h).astype("<u2")
    if bump is not None:
        h[bump[1] * 64 + 32, bump[0] * 64 + 32] += 500
    return h


def tile_centre(x, y, tx=TX, ty=TY):
    """World position of a tile's centre (x = c*sx - ox, y = r*sy - oy; c5hf's ox = tx*32, oy = ty*32*0.866)."""
    return (x * 64 + 32) - tx * 32.0, (y * 64 + 32) * 0.866 - ty * 32 * 0.866


def camera(frame, at, height=180.0, tx=TX, ty=TY):
    """A camera `height` above tile `at`, looking at it from the south (row-vector convention, as synth_camera)."""
    cx, cy = tile_centre(*at, tx=tx, ty=ty)
    view = look_at_rh((cx, cy - 0.6 * height, height), (cx, cy, 10.0), (0, 0, 1))
    proj = perspective_fov_rh(np.radians(45), W / H, 10.0, 4000.0)
    c = synth_camera(frame=frame, samples=(tx * 64, ty * 64))
    c["view"] = list(view.ravel()) + [0.0] * 48
    c["proj"] = list(proj.ravel())
    c["camWorld"] = list(np.linalg.inv(view).ravel())
    c["ox"], c["oy"] = tx * 32.0, ty * 32 * 0.866
    c["farZ"] = 4000.0
    return c


class CameraFeed:
    """Writes a new camera block every `period` s (a new frameStamp each time), at the tile `at` (changeable)."""

    def __init__(self, sec, at, period=0.03, height=180.0, tx=TX, ty=TY):
        self.sec, self.at, self.period, self.height, self.tx, self.ty = sec, at, period, height, tx, ty
        self.frame = 0
        self.stop = False
        self.t = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stop:
            self.frame += 1
            self.sec.write_camera(camera(self.frame, self.at, self.height, self.tx, self.ty))
            time.sleep(self.period)

    def __enter__(self):
        self.t.start()
        return self

    def __exit__(self, *a):
        self.stop = True
        self.t.join(2)


class TileGenSection:
    """The tile generation block (tilegen_block.h): 64-byte header + u32 per tile in the EXE's order x * tilesY + y."""

    def __init__(self, name, tx, ty, magic=0x4E454754, version=1):
        self.n = tx * ty
        self.tx, self.ty = tx, ty
        size = 64 + 4 * self.n
        k = camblock._k
        self.h = k.CreateFileMappingW(camblock.INVALID_HANDLE_VALUE, None, camblock.PAGE_READWRITE, 0, size, name)
        assert self.h
        self.p = k.MapViewOfFile(self.h, camblock.FILE_MAP_ALL_ACCESS, 0, 0, size)
        assert self.p
        self.gens = [19] * self.n
        self.seq = 0
        self.writes = 0
        self.magic, self.version = magic, version
        self.publish()

    def publish(self):
        self.seq += 1                      # odd: writing
        ctypes.memmove(self.p + 8, struct.pack("<I", self.seq), 4)
        hdr = struct.pack("<IIIIIIQI", self.magic, self.version, self.seq, self.tx, self.ty, 0, 0, self.writes)
        ctypes.memmove(self.p, hdr, len(hdr))
        ctypes.memmove(self.p + 64, struct.pack("<%dI" % self.n, *self.gens), 4 * self.n)
        self.seq += 1                      # even: stable
        self.writes += 1
        ctypes.memmove(self.p + 8, struct.pack("<I", self.seq), 4)

    def bump(self, tiles, value=57):
        for (x, y) in tiles:
            self.gens[x * self.ty + y] = value
        self.publish()

    def close(self):
        camblock._k.UnmapViewOfFile(self.p)
        camblock._k.CloseHandle(self.h)


@pytest.fixture
def srv(tmp_path, hidden_desktop, built, request):
    s = Server(tmp_path, hidden_desktop, built, "res_" + request.node.name[:24].replace("[", "_").replace("]", ""))
    yield s
    s.close()


@pytest.fixture(scope="session")
def exe():
    if not EXE.exists():
        pytest.fail("C5R_EXE %s missing (CivilizationV_DX11.exe, read-only)" % EXE)
    return EXE


def tapfile(tmp_path, name="field.c5hf", **kw):
    p = tmp_path / name
    tx, ty = kw.get("tx", TX), kw.get("ty", TY)
    p.write_bytes(c5hf(field(**kw), map_w=tx, map_h=ty))
    return p


def res_totals(text):
    m = re.search(r"residency totals: (.*)", text)
    assert m, text[-2500:]
    return {k.strip(): int(v) for k, v in re.findall(r"([a-z0-9 ]+?) (-?\d+)", m.group(1))}


def res_seconds(text):
    return [{k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d\.]+)", ln)} for ln in text.splitlines() if "residency 1s:" in ln]


def run_server(srv, exe, tmp_path, *extra, seconds=6, at=(16, 10), feed=True, tap=None, during=None, **kw):
    tap = tap or tapfile(tmp_path)
    args = ["--bake", "standin", "--tap-file", tap, "--options-file", tmp_path / "none.ini"] + list(extra)
    if feed:
        with CameraFeed(srv.sec, at, **kw) as f:
            p = srv.start(*args, exe=exe, seconds=seconds, dump=False)
            if during:
                during(f)
            rc = srv.finish(p)
    else:
        p = srv.start(*args, exe=exe, seconds=seconds, dump=False)
        if during:
            during(None)
        rc = srv.finish(p)
    return rc, srv.text()


# ---------------------------------------------------------------------------------------------------------------- tests

def test_whole_map_baked_and_resident(srv, exe, tmp_path):
    """Radius -1 (the options file is missing: default): every land tile (classes 2 and 3) baked once in the background,
    nothing evicted, no gray frame after the bake-ahead, class-1 tiles never baked."""
    rc, t = run_server(srv, exe, tmp_path, "--bake-trace", "40", seconds=6)
    assert rc == 0, t[-2500:]
    land = TX * TY - DEEP_COLS * TY
    assert "class 1 %d, class 2 %d, class 3 %d -> %d land tiles" % (DEEP_COLS * TY, MID_COLS * TY, land - MID_COLS * TY,
                                                                      land) in t, t[-2500:]
    assert "options file" in t and "missing" in t and "BakedTileCameraRadius = -1 (default)" in t
    assert "bake-ahead complete: %d tiles resident" % land in t, t[-2500:]
    r = res_totals(t)
    assert r["resident"] == land and r["bakes"] == land and r["failed"] == 0 and r["evicted"] == 0, r
    assert r["resident kb"] == land * 2 * CACHE_BYTES // 1024, r
    assert r["gray after ahead"] == 0 and r["frames"] > 20, r
    # order: the visible tiles first (ring 0), then rings in ascending order; never a class-1 tile
    bakes = re.findall(r"residency: bake \d+ tile \((\d+), (\d+)\) gen 1 ring (-?\d+) visible (\d) class (\d)", t)
    assert len(bakes) == 40, t[-2500:]
    rings = [int(b[2]) for b in bakes]
    assert rings[0] == 0 and rings == sorted(rings), rings
    assert all(int(b[4]) >= 2 for b in bakes)
    vis = [int(b[3]) for b in bakes]
    assert vis[:vis.index(0)] == [1] * vis.index(0) if 0 in vis else True


def test_visible_tiles_baked_before_first_draw(srv, exe, tmp_path):
    """Burst 64 >= the visible land tiles of the close camera: the first drawn frame has every visible tile baked
    (zero gray frames from the start) although the background rate is tiny."""
    # pump cap 200 ms: the first texture creations of a process are slow, and the default 4 ms cap would stop the
    # visible pass of the first frame early (that frame is then counted gray: the cap is a frame-time guard)
    rc, t = run_server(srv, exe, tmp_path, "--bake-burst", "64", "--bake-rate", "0.5", "--bake-pump-ms", "200", seconds=4)
    assert rc == 0, t[-2500:]
    r = res_totals(t)
    s = res_seconds(t)
    assert s and s[-1]["visible_land"] > 0, t[-2500:]
    assert r["gray frames"] == 0 and r["frames"] > 10, r
    assert r["resident"] < TX * TY - DEEP_COLS * TY, r     # the background barely started


def test_gray_frames_counted_when_budget_too_small(srv, exe, tmp_path):
    """Burst 1 and a slow background: the first frames draw visible land tiles without a bake, and the counter says so
    (the measure of acceptance C must be able to fail). Issue 0499: --bake-catch-up 0, since 0409 made the catch-up (every
    visible land tile without a bake baked before the draw when there are more than the burst) the default."""
    rc, t = run_server(srv, exe, tmp_path, "--bake-burst", "1", "--bake-rate", "1", "--bake-catch-up", "0", seconds=3)
    assert rc == 0, t[-2500:]
    r = res_totals(t)
    assert r["catch up"] == 0 and r["catch up draws"] == 0 and r["catch up bakes"] == 0, r
    assert r["gray frames"] > 5 and r["gray tile frames"] > r["gray frames"], r
    assert r["ahead done"] == 0, r


def test_catch_up_leaves_no_gray_frames_with_a_small_budget(srv, exe, tmp_path):
    """Issue 0499 (the measure of 0409's acceptance): the same burst 1 and slow background as the test above, with the
    default catch-up: the first draw bakes every visible land tile, so no frame is gray, and the catch-up is counted."""
    rc, t = run_server(srv, exe, tmp_path, "--bake-burst", "1", "--bake-rate", "1", seconds=3)
    assert rc == 0, t[-2500:]
    r = res_totals(t)
    assert r["catch up"] == 1, r
    assert r["catch up draws"] > 0 and r["catch up bakes"] > 1, r
    assert r["gray frames"] == 0 and r["gray tile frames"] == 0 and r["frames"] > 10, r
    assert r["ahead done"] == 0, r


def test_gpu_budget_limits_the_background(srv, exe, tmp_path):
    """--bake-gpu-ms-per-s 20 with a stand-in that reports 2 ms of GPU per bake: about 10 background bakes per second
    (rate 400 alone would allow far more); burst 0 so that only the background bakes."""
    rc, t = run_server(srv, exe, tmp_path, "--standin-ms", "2", "--bake-gpu-ms-per-s", "20", "--bake-burst", "0", seconds=4)
    assert rc == 0, t[-2500:]
    r = res_totals(t)
    s = res_seconds(t)
    assert 15 <= r["bakes"] <= 60, r                       # 4 s x 10/s, plus the first quarter second's allowance
    assert abs(s[-1]["bake_gpu_ms_total"] - 2.0 * s[-1]["bakes"]) <= 0.06, s[-1]   # printed with one decimal
    rc2, t2 = run_server(srv, exe, tmp_path, "--standin-ms", "2", "--bake-burst", "0", seconds=4)
    assert rc2 == 0
    assert res_totals(t2[len(t):])["bakes"] > 3 * r["bakes"], "without the GPU budget the background bakes far more"


@pytest.mark.parametrize("radius", [0, 5])
def test_radius_keeps_rings_and_evicts(srv, exe, tmp_path, radius):
    """Radius 0 keeps only the visible tiles, 5 the tiles within 5 rings; a camera move across the map evicts the
    tiles left behind; the radius comes from the options file."""
    ini = tmp_path / "VP64Options.ini"
    ini.write_text("[Renderer]\nBakedTileCameraRadius = %d\n" % radius)

    def move(f):
        time.sleep(3.0)
        f.at = (28, 4)

    # burst 200 > the visible tiles: the jump is baked before its first draw (with 16 the first frames there are gray,
    # which test_gray_frames_counted_when_budget_too_small covers)
    p_args = ["--bake", "standin", "--tap-file", tapfile(tmp_path), "--options-file", ini, "--bake-trace", "2000",
              "--bake-burst", "200", "--bake-pump-ms", "200"]
    with CameraFeed(srv.sec, (6, 14)) as f:
        p = srv.start(*p_args, exe=exe, seconds=6.5, dump=False)
        move(f)
        rc = srv.finish(p)
    t = srv.text()
    assert rc == 0, t[-2500:]
    assert "BakedTileCameraRadius = %d (from the file)" % radius in t, t[-2500:]
    r = res_totals(t)
    s = res_seconds(t)
    last = s[-1]
    assert r["radius"] == radius and r["evicted"] > 0 and r["gray after ahead"] == 0, r
    rings = [int(x) for x in re.findall(r"residency: bake \d+ tile \(\d+, \d+\) gen \d+ ring (-?\d+)", t)]
    assert rings and max(rings) <= radius and min(rings) >= 0, sorted(set(rings))
    if radius == 0:
        assert last["resident"] == last["visible_land"], last
    else:
        assert last["visible_land"] < last["resident"] < TX * TY - DEEP_COLS * TY, last


def test_rebake_on_raised_generation(srv, exe, tmp_path):
    """The tile generation block: after the baseline snapshot, three tiles change; they are re-baked (one visible, two
    not) and the time from the signal to the new bake is measured; a tile outside the block's map is ignored."""
    name = "Local\\VPTileGenTest_%d" % id(srv)
    tg = TileGenSection(name, TX, TY)
    try:
        def during(f):
            time.sleep(3.0)          # bake-ahead of 640 tiles at 400/s: done after about 1.6 s
            tg.bump([(16, 10), (3, 3), (30, 18)])
        rc, t = run_server(srv, exe, tmp_path, "--tilegen", name, seconds=5, during=during)
    finally:
        tg.close()
    assert rc == 0, t[-2500:]
    assert "tile generation block '%s' opened" % name in t, t[-2500:]
    assert re.search(r"tile generation block seq \d+: 3 tiles changed", t), t[-2500:]
    r = res_totals(t)
    assert r["raises"] == 3 and r["tilegen raised"] == 3 and r["rebakes"] == 3 and r["latency n"] == 3, r
    assert 0 <= r["latency max us"] < 500000, r                    # re-baked within 0.5 s of the signal
    assert r["tilegen errors"] == 0 and r["gray after ahead"] == 0, r


def test_field_change_rebakes_only_changed_tiles(srv, exe, tmp_path):
    """A new tap of the same size with one sample raised in tile (10, 8): only that tile gets a new generation and is
    re-baked; the others keep their bakes (the renderer's dropped caches are set again)."""
    from test_terrain_scene import TapSection, write_tap  # noqa: F401
    p1 = c5hf(field(), map_w=TX, map_h=TY)
    p2 = c5hf(field(bump=(10, 8)), map_w=TX, map_h=TY)
    with CameraFeed(srv.sec, (10, 8)) as f:
        srv.tap(load_seq=1, payload=p1, map_w=TX, map_h=TY)
        p = srv.start("--bake", "standin", "--options-file", tmp_path / "none.ini", exe=exe, seconds=6, dump=False)
        srv.wait_log(r"bake-ahead complete", timeout=10)
        srv.tap(load_seq=1, payload=p2, map_w=TX, map_h=TY, copy_seq=1)
        rc = srv.finish(p)
    t = srv.text()
    assert rc == 0, t[-2500:]
    assert "same size: compared" in t and "1 tiles changed" in t, t[-2500:]
    r = res_totals(t)
    land = TX * TY - DEEP_COLS * TY
    assert r["fields"] == 2 and r["field changed tiles"] == 1 and r["rebakes"] == 1 and r["bakes"] == land + 1, r
    assert r["resident"] == land and r["latency n"] == 1 and r["gray after ahead"] == 0, r


def test_failed_tile_stays_plain_until_its_content_changes(srv, exe, tmp_path):
    """--standin-fail 16,10 (a visible tile): logged once, counted, drawn plain (gray frames after the bake-ahead), not
    retried; a raised generation retries it (and it fails again: failed 2)."""
    name = "Local\\VPTileGenFail_%d" % id(srv)
    tg = TileGenSection(name, TX, TY)
    try:
        def during(f):
            time.sleep(3.0)
            tg.bump([(16, 10)])
        rc, t = run_server(srv, exe, tmp_path, "--standin-fail", "16,10", "--tilegen", name, seconds=5, during=during)
    finally:
        tg.close()
    assert rc == 0, t[-2500:]
    assert "bake of tile (16, 10) generation 1 failed: Unsupported: stand-in: forced failure" in t, t[-2500:]
    assert "bake of tile (16, 10) generation 2 failed" in t, t[-2500:]
    r = res_totals(t)
    land = TX * TY - DEEP_COLS * TY
    assert r["failed"] == 2 and r["resident"] == land - 1 and r["gray after ahead"] > 0, r


def test_out_of_memory_lowers_the_radius(srv, exe, tmp_path):
    """Arrays (default) with a cap of 13 MB (one chunk of 256 slices = 10.7 MB fits, two do not): radius -1 drops to 5
    (then lower if needed), tiles outside are evicted, the server keeps running and its arrays stay inside the cap."""
    rc, t = run_server(srv, exe, tmp_path, "--bake-memory-mb", "13", seconds=5)
    assert rc == 0, t[-2500:]
    assert re.search(r"out of memory \(OutOfMemory: array chunk beyond the memory limit of 13\.0+ MB\) with \d+ tiles resident: "
                     r"radius -1 -> 5", t), t[-2500:]
    r = res_totals(t)
    assert r["oom"] >= 1 and r["radius"] <= 5 and r["stopped"] == 0, r
    m = re.search(r"residency arrays: array kb (\d+) arrays (\d+)", t)
    assert m and int(m.group(1)) * 1024 <= 13 * 1048576 and int(m.group(2)) == 1, t[-1500:]


def test_out_of_memory_of_the_baker_without_arrays(srv, exe, tmp_path):
    """--cache-arrays 0: the stand-in's own textures with a budget of 150 tiles; its OutOfMemory lowers the radius."""
    budget_mb = 150 * 2 * CACHE_BYTES / 1048576.0
    rc, t = run_server(srv, exe, tmp_path, "--cache-arrays", "0", "--standin-vram-mb", "%.3f" % budget_mb, seconds=5)
    assert rc == 0, t[-2500:]
    assert re.search(r"out of memory \(OutOfMemory: stand-in: CreateTexture2D failed 0x8007000E \(E_OUTOFMEMORY\)\) with \d+ "
                     r"tiles resident: radius -1 -> 5", t), t[-2500:]
    r = res_totals(t)
    assert r["oom"] >= 1 and r["radius"] <= 5 and r["stopped"] == 0, r
    assert r["resident"] * 2 * CACHE_BYTES <= budget_mb * 1048576.0, r
    s = res_seconds(t)
    assert s[-1]["standin_live_mb"] <= budget_mb + 0.01, s[-1]
    assert "residency arrays: array kb 0 arrays 0" in t


def test_out_of_memory_at_radius_zero_stops_baking(srv, exe, tmp_path):
    """A cap smaller than one array chunk: the radius falls to 0, then baking stops (logged); unbaked visible tiles
    stay plain and every frame is counted gray; exit 0."""
    rc, t = run_server(srv, exe, tmp_path, "--bake-memory-mb", "0.5", "--bake-radius", "1", seconds=3)
    assert rc == 0, t[-2500:]
    assert "radius 1 -> 0" in t and "out of memory at radius 0" in t and "baking stopped" in t, t[-2500:]
    r = res_totals(t)
    assert r["stopped"] == 1 and r["gray frames"] == r["frames"] and r["frames"] > 0, r


def test_options_file_errors_give_the_default(srv, exe, tmp_path):
    """A bad value is warned about (line and reason) and the default -1 is used; --bake-radius overrides the file."""
    ini = tmp_path / "bad.ini"
    ini.write_text("[Renderer]\nBakedTileCameraRadius = abc\n")
    p = srv.start("--bake", "standin", "--tap-file", tapfile(tmp_path), "--options-file", ini, exe=exe, seconds=1.5, dump=False)
    rc = srv.finish(p)
    t = srv.text()
    assert rc == 0, t[-2500:]
    assert re.search(r"options: line 2: BakedTileCameraRadius = abc: .*default -1", t), t[-2500:]
    assert "BakedTileCameraRadius = -1 (default)" in t and "1 warnings" in t, t[-2500:]
    log2 = srv.log.read_text()
    p = srv.start("--bake", "standin", "--tap-file", tapfile(tmp_path), "--options-file", ini, "--bake-radius", "3", exe=exe,
                  seconds=1.5, dump=False)
    rc = srv.finish(p)
    t2 = srv.text()[len(log2):]
    assert rc == 0 and "BakedTileCameraRadius = 3 (--bake-radius)" in t2, t2[-1500:]
    assert res_totals(t2)["radius"] == 3


@pytest.mark.parametrize("kind", ["dims", "magic", "version", "missing"])
def test_tilegen_block_errors(srv, exe, tmp_path, kind):
    """A block of another map size, a bad magic or version, or no block: logged, counted, nothing raised, exit 0."""
    name = "Local\\VPTileGenBad_%d_%s" % (id(srv), kind)
    tg = None
    if kind == "dims":
        tg = TileGenSection(name, TX + 1, TY)
    elif kind == "magic":
        tg = TileGenSection(name, TX, TY, magic=0x12345678)
    elif kind == "version":
        tg = TileGenSection(name, TX, TY, version=2)
    try:
        rc, t = run_server(srv, exe, tmp_path, "--tilegen", name, seconds=2.5)
    finally:
        if tg:
            tg.close()
    assert rc == 0, t[-2500:]
    want = {"dims": "tiles 33x20 but the field has 32x20", "magic": "bad magic", "version": "version 2 (want 1)",
            "missing": "not found (retried every second)"}[kind]
    assert "tile generation block: " + want in t, t[-2500:]
    r = res_totals(t)
    assert r["tilegen errors"] >= 1 and r["raises"] == 0 and r["tilegen raised"] == 0, r
    assert t.count("tile generation block: " + want) == 1, "logged once"


GAME = release_setting("C5R_GAME_DIR", "render_game_dir")   # ticket 4713: else the setting render_game_dir
TINY_TAP = Path(os.environ.get("C5_TINY_TAP", "D:/c5-64bit-work/3400/ref/C5H-3401-TINY_0008/armA/heightfield.c5hf"))


def test_pipeline_bakes_the_tiny_field(srv, exe, tmp_path):
    """--bake pipeline (3404's PipelineBaker, the EXE's shaders, the install read-only) on 3401's Tiny tap: every land
    tile baked into the arrays, none failed; no camera (the bake-ahead runs from the map's centre). Inputs missing fail."""
    if not GAME.exists() or not TINY_TAP.exists():
        pytest.fail("install %s or Tiny tap %s missing" % (GAME, TINY_TAP))
    p = srv.start("--bake", "pipeline", "--game-dir", GAME, "--tap-file", TINY_TAP, "--options-file", tmp_path / "none.ini",
                  "--bake-rate", "2000", exe=exe, seconds=25, dump=False)
    srv.wait_log(r"bake-ahead complete", timeout=40)
    rc = srv.finish(p)
    t = srv.text()
    assert rc == 0, t[-2500:]
    assert "baker pipeline" in t, t[-2500:]
    r = res_totals(t)
    assert r["failed"] == 0 and r["resident"] == r["land"] and r["land"] > 1000, r
    m = re.search(r"residency arrays: array kb (\d+) arrays (\d+) copies (\d+)", t)
    # Since 3404 wave 3 (d597edf) the pipeline honours BakeTarget and writes into the array slice itself, so the
    # residency copies nothing (team-3400 lead, 2026-11-13, after merging 3404 and 3407).
    assert m and int(m.group(1)) > 0 and int(m.group(3)) == 0, "the pipeline writes into the slices: no copies"


def test_pipeline_without_material_bytes_is_refused(srv, exe, tmp_path):
    """A field without the material byte layer: the pipeline's setMap refuses (NotFound), the residency logs it and
    bakes nothing; the server runs on (plain tiles), exit 0."""
    if not GAME.exists():
        pytest.fail("install %s missing" % GAME)
    p = srv.start("--bake", "pipeline", "--game-dir", GAME, "--tap-file", tapfile(tmp_path), "--options-file",
                  tmp_path / "none.ini", exe=exe, seconds=3, dump=False)
    rc = srv.finish(p)
    t = srv.text()
    assert rc == 0, t[-2500:]
    assert "residency: baker setMap failed: NotFound" in t and "terrain: residency refused the field" in t, t[-2500:]


def test_pipeline_with_a_bad_install_exits_4(srv, exe, tmp_path):
    p = srv.start("--bake", "pipeline", "--game-dir", tmp_path / "no-install", "--tap-file", tapfile(tmp_path), exe=exe,
                  seconds=3, dump=False)
    rc = srv.finish(p)
    t = srv.text()
    assert rc == 4 and "residency init (pipeline baker" in t, (rc, t[-1500:])


@pytest.mark.parametrize("args,msg", [
    (["--bake", "foo"], "bad value for --bake"),
    (["--bake", "standin", "--bake-radius", "129"], "--bake-radius"),
    (["--bake", "standin", "--bake-radius", "-2"], "--bake-radius"),
    (["--bake", "standin", "--tile-plots", "2"], "needs tile plots 1"),
    (["--bake", "standin", "--color", "material"], "--color must be caches"),
    (["--bake-radius", "3"], "need --bake standin"),
    (["--tilegen", "x"], "need --bake standin"),
    (["--bake", "standin", "--standin-fail", "3"], "bad value for --standin-fail"),
    (["--bake", "standin", "--bake-rate", "0"], "--bake-rate"),
    (["--bake", "standin", "--bake-burst", "-1"], "--bake-burst"),
    (["--bake", "standin", "--bake-catch-up", "2"], "bad value for --bake-catch-up"),
    (["--bake", "standin", "--bake-catch-up", "-1"], "bad value for --bake-catch-up"),
    (["--bake", "standin", "--bake-catch-up", "yes"], "bad value for --bake-catch-up"),
    (["--bake", "standin", "--bake-catch-up", "0.5"], "bad value for --bake-catch-up"),
    (["--bake", "standin", "--options-file", ""], "--options-file"),
    (["--bake", "standin", "--bake-gpu-ms-per-s", "-1"], "--bake-gpu-ms-per-s"),
    (["--bake", "pipeline"], "needs --game-dir DIR or C5R_GAME_DIR"),
    (["--bake", "standin", "--game-dir", "C:\\x"], "--game-dir needs --bake pipeline"),
    (["--bake", "standin", "--cache-arrays", "2"], "--cache-arrays"),
    (["--bake", "standin", "--bake-memory-mb", "-1"], "--bake-memory-mb"),
])
def test_bad_command_line(tmp_path, hidden_desktop, built, exe, args, msg):
    """Exit 2 with the reason on stderr, nothing started (no log file written)."""
    out = tmp_path / "out.txt"
    log = tmp_path / "server.log"
    full = [str(built / "shell_server.exe"), "--section", "Local\\VPResBad_%d" % id(out), "--scene", "terrain", "--exe", str(exe),
            "--seconds", "1", "--log", str(log)] + [str(a) for a in args]
    p = shellrun.launch(full, hidden_desktop, console=True, out_path=out, unset=("C5R_GAME_DIR",))
    rc = p.wait(20)
    p.close()
    text = out.read_text(errors="replace") if out.exists() else ""
    assert rc == 2, (rc, text)
    assert msg in text, text
    assert not log.exists() or "shell_server start" not in log.read_text(errors="replace")


def test_bake_standin_needs_terrain_scene(tmp_path, hidden_desktop, built):
    out = tmp_path / "out.txt"
    full = [str(built / "shell_server.exe"), "--section", "Local\\VPResBad2_%d" % id(out), "--scene", "grid", "--bake", "standin",
            "--seconds", "1", "--log", str(tmp_path / "s.log")]
    p = shellrun.launch(full, hidden_desktop, console=True, out_path=out)
    rc = p.wait(20)
    p.close()
    assert rc == 2 and "--bake standin needs --scene terrain" in out.read_text(errors="replace")
