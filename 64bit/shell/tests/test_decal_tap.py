"""Issue 4102 (epic 0166): the decal tap of shell_composite.dll (composite/decal_tap.cpp), its sections
(common/shell_decal_tap.h), on its test entry decal_tap_host.exe (shell/tests/decal_tap_host.cpp: the tap's own
OnPresent loop without D3D; a console program started without a window) and, once hooked up, inside the composite on
the stand-in.

decal_test=<mode> feeds a synthetic EXE (a TerrainDecalSystem header, two batches with pools, lists and vertex chunks, a
resource manager with its maps, a farm-type table; decal_tap.cpp BuildTestWorld) through the real poll, read, name,
diff and publication code. Asserted for each case: the control block's state and failReason, the LOUD log line and, for
the good cases, every byte of the image against the synthetic EXE recomputed here. Negatives (COMMON 9): a record past
its chunk, a texture handle without a name, an absurd batch count, an invalid rectangle, an unmapped pointer (SEH: the
tap disables itself, the host goes on), the real path on an EXE of no known build (PATTERN), an EXE the composite did
not validate (PATTERN), the kill switch, the image limit, a refused ack. Ticket 4965: decal_test=patterns on fake images of
both known builds, a broken pattern, a header of no build, a symbol not carried.

Issue 4110 adds decal_test=huge (17,800 decals in 19 batches, 256 names in a 3,000-node texture map: the huge save's
size): the publication spread over Presents within decal_budget_ms (the host logs every Present's CPU time), its image
byte-equal to a one-shot build, and two negatives (a batch changed during the build; a change during every build).

Needs C5_SHELL_BUILD (shell/build.cmd, which builds decal_tap_host.exe). A missing binary fails the test (no skip).
"""
import math
import os
import re
import shutil
import struct
import subprocess
import time
from pathlib import Path

import pytest

import shellrun
import tapcheck

STATES = ["NONE", "WRITING", "READY", "FAILED", "OFF"]
FAILS = ["NONE", "PATTERN", "SEH", "LAYOUT", "TOO_BIG", "SECTION", "NAMES", "ACK_TIMEOUT"]
MAGIC = 0x54443543
NO_TEXTURE = 0xFFFFFFFF
CTL_BYTES = 336


def host_exe() -> Path:
    p = shellrun.bin_dir() / "decal_tap_host.exe"
    assert p.exists(), "decal_tap_host.exe not built in %s: run `cmd /c shell\\build.cmd`" % p.parent
    return p


def parse_control(raw):
    f = struct.unpack_from("<8I", raw, 0)
    d = dict(seq=f[0], magic=f[1], state=f[2], failReason=f[3], exePid=f[4], generation=f[5], loadSeq=f[6], decalCount=f[7])
    d["imageBytes"], d["fnv1a64"], d["readyQpc"] = struct.unpack_from("<QQq", raw, 32)
    d["copyMs"], d["pollMs"] = struct.unpack_from("<ff", raw, 56)
    d["publishCount"], d["refusedReads"], d["sehCount"] = struct.unpack_from("<3I", raw, 64)
    d["imageName"] = raw[80:208].decode("utf-16-le").split("\0", 1)[0]
    d["ackGeneration"], d["ackResult"], d["ackServerPid"] = struct.unpack_from("<IiI", raw, 208)
    d["changedSince"], d["added"], d["removed"], d["heldSections"] = struct.unpack_from("<4I", raw, 220)
    d["areaSet"], d["areaTilesX"], d["areaTilesY"] = struct.unpack_from("<3I", raw, 236)   # 4111
    d["walkSmall"], d["walkLarge"], d["walkMisses"] = struct.unpack_from("<3I", raw, 248)   # 0366
    d["walkHolds"] = struct.unpack_from("<I", raw, 260)[0]   # 0406
    d["stateName"] = STATES[d["state"]] if d["state"] < len(STATES) else "?"
    d["failName"] = FAILS[d["failReason"]] if d["failReason"] < len(FAILS) else "?"
    return d


def parse_image(b):
    h = struct.unpack_from("<14I4fQ8I", b, 0)
    keys = ["magic", "version", "headerBytes", "decalBytes", "generation", "loadSeq", "decalCount", "nameCount",
            "vertexCount", "changedCount", "decalOffset", "nameOffset", "vertexOffset", "changedOffset"]
    img = dict(zip(keys, h[:14]))
    img["worldRect"] = list(h[14:18])
    img["imageBytes"] = h[18]
    img["changedSince"], img["hashOffset"] = h[19], h[20]
    img["areaOffset"], img["areaTilesX"], img["areaTilesY"] = h[21], h[22], h[23]   # 4111
    img["walkOffset"] = h[24]   # 0366
    img["walk"] = None
    if img["walkOffset"]:
        wo = img["walkOffset"]
        ox, oy, ix, iy, cols, rows, ns, nl = struct.unpack_from("<4f4I", b, wo)
        img["walk"] = dict(origin=(ox, oy), inverse=(ix, iy), cols=cols, rows=rows, small=ns, large=nl,
                           entries=[struct.unpack_from("<2I2f", b, wo + 32 + 16 * i) for i in range(h[6])])
    n = img["areaTilesX"] * img["areaTilesY"]
    a = b[img["areaOffset"]:img["areaOffset"] + (n + 7) // 8] if n else b""
    img["area"] = {(k % img["areaTilesX"], k // img["areaTilesX"]) for k in range(n) if a[k >> 3] >> (k & 7) & 1}
    img["areaPadding"] = (a[-1] >> (n % 8)) if n % 8 else 0
    recs = []
    for i in range(img["decalCount"]):
        o = img["decalOffset"] + 48 * i
        r = struct.unpack_from("<5f7I", b, o)
        recs.append(dict(rect=list(r[0:4]), priority=r[4], flags=r[5], texture=list(r[6:9]), firstVertex=r[9],
                         triangles=r[10], listOrder=r[11]))
    names = []
    for i in range(img["nameCount"]):
        o = img["nameOffset"] + 200 * i
        hnd = struct.unpack_from("<I", b, o)[0]
        names.append((hnd, b[o + 8:o + 200].split(b"\0", 1)[0].decode()))
    changed = [list(struct.unpack_from("<4f", b, img["changedOffset"] + 16 * i)) for i in range(img["changedCount"])]
    hashes = [struct.unpack_from("<Q", b, img["hashOffset"] + 8 * i)[0] for i in range(img["decalCount"])]
    verts = b[img["vertexOffset"]:img["vertexOffset"] + 8 * img["vertexCount"]]
    return img, recs, names, changed, hashes, verts


# ======================================================================================================================
# the synthetic EXE, recomputed (decal_tap.cpp BuildTestWorld / TestChange)

TEX = {100: "RoadsandRails_new_D.dds", 101: "RoadsandRails_new_H.dds", 102: "RoadsandRails_new_SREF.dds",
       103: "riverbank_D.dds", 104: "grey.dds", 105: "CityBase_Diff_anc.dds", 106: "CityBase_HT.dds",
       107: "CityBase_SREF.dds", 200: "@farm_types.xml/Crops_Africa/0/albedo",
       201: "@farm_types.xml/Crops_Africa/0/heightmap", 202: "blank_texture.dds", 1975: "blank_texture_spec_decal.dds"}
# id, batch, priority, pass, handles, rect, triangles, live
DECALS = [
    (1, 0, 0.7, 0, (100, 101, 102), (10.0, 20.0, 74.5, 60.25), 4, True),
    (2, 0, 0.5, 0, (200, 201, 202), (100.0, 100.0, 140.0, 132.0), 2, True),
    (3, 0, 0.6, 0, (0, 0, 1975), (96.0, 90.0, 150.0, 140.0), 3, True),
    (9, 1, 0.0, 0, (105, 106, 107), (0.0, 0.0, 1.0, 1.0), 1, False),
    (4, 1, 0.4, 0, (103, 104, 1975), (300.0, 200.0, 420.0, 230.0), 5, True),
    (5, 1, 0.0, 0, (105, 106, 107), (500.0, 500.0, 564.0, 564.0), 2, True),
    (6, 1, 0.1, 1, (103, 103, 1975), (600.0, 600.0, 610.0, 612.0), 1, True),
]
NEW_ROAD = (7, 1, 0.7, 0, (100, 101, 102), (700.0, 300.0, 760.0, 340.0), 2, True)

# 4111: the synthetic tile grid (decal_tap.cpp BuildTestGrid): 56 x 40 tiles; +0x300 = 1 under every live decal but the
# river bed (priority 0.4), on tile (40, 30), and (after a change) under the removed and the added decal's rectangles
TG_TX, TG_TY = 56, 40


def mark(rect):
    x0, y0, x1, y1 = (int(math.floor(v / 64.0)) for v in rect)
    return {(x, y) for y in range(max(y0, 0), y1 + 1) for x in range(max(x0, 0), x1 + 1) if x < TG_TX and y < TG_TY}


def area_start():
    a = {(40, 30)}
    for d in DECALS:
        if d[7] and f32(d[2]) != f32(0.4):
            a |= mark(d[5])
    return a


def area_after_change(a):
    return a | mark(DECALS[4][5]) | mark(NEW_ROAD[5])


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def verts_of(i, tris):
    return bytes((13 * j + 7 * i + 1) & 0xFF for j in range(24 * tris))


def content_hash(d):
    i, _, prio, pas, tex, rect, tris, _ = d
    b = struct.pack("<If3I4fI", pas, prio, *tex, *rect, tris) + verts_of(i, tris)
    return tapcheck.fnv1a64(b)


def expected(decals):
    """The live decals in the tap's order: batch, then the EXE's list order (here: the order of the spec list)."""
    live = [d for d in decals if d[7]]
    return [d for b in (0, 1) for d in live if d[1] == b]


# 0366: the synthetic spatial index (decal_tap.cpp TestIndexBuild): the live decals added in the order of the spec list; a
# small one (both sides at most 256) at the head of the list of the cell of its centre, a large one at the large list's
# tail; the grid cols x rows over the world 0 .. 3583 x 0 .. 2559, single precision as the EXE (terrain-decals.md 18.1)
LARGE = 0xFFFFFFFF
LARGE_SIDE = 256.0


def walk_cell(p, origin, inv, n):
    v = f32(f32(f32(p) - f32(origin)) * inv)
    c = int(v) if -2147483648.0 < v < 2147483648.0 else -2147483648
    return max(0, min(c, n - 1))


def walk_expected(order, cols, rows, world=(0.0, 0.0, 3583.0, 2559.0)):
    """order: the live decals in add order, each (key, rect); returns ({key: (cell, rank, px, py)}, inverse)."""
    invx = f32(f32(cols) / f32(world[2] - world[0]))
    invy = f32(f32(rows) / f32(world[3] - world[1]))
    cells, large, out = {}, [], {}
    for key, r in order:
        if f32(f32(r[2]) - f32(r[0])) > LARGE_SIDE or f32(f32(r[3]) - f32(r[1])) > LARGE_SIDE:
            large.append(key)
            continue
        px, py = f32(f32(f32(r[0]) + f32(r[2])) * 0.5), f32(f32(f32(r[1]) + f32(r[3])) * 0.5)
        c = walk_cell(py, world[1], invy, rows) * cols + walk_cell(px, world[0], invx, cols)
        cells.setdefault(c, []).insert(0, key)
        out[key] = (c, None, px, py)
    for c, keys in cells.items():
        for rank, key in enumerate(keys):
            out[key] = (c, rank, out[key][2], out[key][3])
    for rank, key in enumerate(large):
        out[key] = (LARGE, rank, 0.0, 0.0)
    return out, (invx, invy)


def check_walk(img, decals):
    """The walk block of the small world against TestIndexBuild recomputed (14 x 10 cells)."""
    w = img["walk"]
    assert w is not None and img["walkOffset"] % 16 == 0
    live = [(k, d) for k, d in enumerate(decals) if d[7]]
    want, inv = walk_expected([(k, d[5]) for k, d in live], 14, 10)
    order = [k for b in (0, 1) for k, d in live if d[1] == b]   # positions in the spec list, in expected()'s order
    assert (w["cols"], w["rows"], w["origin"], w["inverse"]) == (14, 10, (0.0, 0.0), inv), w
    assert (w["small"], w["large"]) == (sum(1 for v in want.values() if v[0] != LARGE),
                                        sum(1 for v in want.values() if v[0] == LARGE))
    for i, k in enumerate(order):
        assert w["entries"][i] == want[k], (i, decals[k], w["entries"][i], want[k])
    return w


# ======================================================================================================================
# running the host

class Host:
    def __init__(self, tmp: Path, env: dict, args=(), seconds=6.0):
        self.dir = tmp
        self.exe = tmp / "decal_tap_host.exe"
        shutil.copy(host_exe(), self.exe)
        asan = shellrun.bin_dir() / "clang_rt.asan_dynamic-i386.dll"
        if asan.exists():
            shutil.copy(asan, tmp / asan.name)
        self.log = tmp / "host.log"
        e = dict(os.environ)
        e.update({k: str(v) for k, v in env.items()})
        self.p = subprocess.Popen([str(self.exe), "--seconds", str(seconds), "--log", str(self.log), *args], env=e,
                                  creationflags=0x08000000)
        self.name = "Local\\VPShellDecalTap.%d" % self.p.pid
        self.m = None

    def control(self):
        if not self.m:
            t_end = time.time() + 5
            while not self.m:
                try:
                    self.m = tapcheck.Mapping(self.name)
                except OSError:
                    if time.time() > t_end or self.p.poll() is not None:
                        raise
                    time.sleep(0.02)
        raw = tapcheck.read_block(self.m, 0, CTL_BYTES)
        return parse_control(raw) if raw else {}

    def wait(self, pred, timeout_s=10.0, what=""):
        t_end = time.time() + timeout_s
        c = {}
        while time.time() < t_end:
            c = self.control()
            if c and pred(c):
                return c
            time.sleep(0.02)
        raise TimeoutError("decal tap: %s not reached: %s; log %s" % (what, c, self.lines()[-8:]))

    def wait_state(self, state, timeout_s=10.0, **kw):
        return self.wait(lambda c: c["stateName"] == state and all(c[k] == v for k, v in kw.items()), timeout_s,
                         "%s %s" % (state, kw))

    def image(self, c):
        with tapcheck.Mapping(c["imageName"], write=False) as m:
            b = m.read(0, c["imageBytes"])
        assert tapcheck.fnv1a64(b) == c["fnv1a64"]
        return b

    def ack(self, generation, result=0, pid=4242):
        self.m.write(212, struct.pack("<iI", result, pid))
        self.m.write(208, struct.pack("<I", generation))

    def trigger(self, name):
        (self.dir / name).write_text("")

    def lines(self):
        return self.log.read_text(errors="replace").splitlines() if self.log.exists() else []

    def wait_log(self, pattern, timeout_s=5.0):
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            for ln in self.lines():
                if re.search(pattern, ln):
                    return ln
            time.sleep(0.05)
        raise TimeoutError("log line %r not found; log %s" % (pattern, self.lines()[-10:]))

    def finish(self, timeout_s=20):
        rc = self.p.wait(timeout_s)
        if self.m:
            self.m.close()
        assert rc == 0, (rc, self.lines()[-10:])
        assert any("decal_tap_host: exit after" in ln for ln in self.lines())

    def kill(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait(5)
        if self.m:
            self.m.close()


@pytest.fixture
def host(tmp_path):
    made = []

    def make(env, args=(), seconds=6.0):
        d = tmp_path / ("h%d" % len(made))
        d.mkdir()
        h = Host(d, env, args, seconds)
        made.append(h)
        return h
    yield make
    for h in made:
        h.kill()


def check_image(b, decals, generation, changed_since, changed_want, area=None, walk=True):
    img, recs, names, changed, hashes, verts = parse_image(b)
    if walk:   # 0366: the walk block exact (walk False: none expected)
        check_walk(img, decals)
    else:
        assert img["walkOffset"] == 0 and img["walk"] is None
    if area is not None:   # 4111: the area block (None: not checked; "none": no block expected)
        if area == "none":
            assert (img["areaOffset"], img["areaTilesX"], img["areaTilesY"]) == (0, 0, 0)
        else:
            assert (img["areaTilesX"], img["areaTilesY"]) == (TG_TX, TG_TY) and img["areaOffset"] % 16 == 0
            assert img["areaOffset"] >= img["hashOffset"] + 8 * img["decalCount"]
            assert img["imageBytes"] >= img["areaOffset"] + (TG_TX * TG_TY + 7) // 8
            assert img["area"] == area, sorted(img["area"] ^ area)
            assert img["areaPadding"] == 0
    want = expected(decals)
    assert (img["magic"], img["version"], img["headerBytes"], img["decalBytes"]) == (MAGIC, 1, 112, 48)
    assert img["generation"] == generation and img["imageBytes"] == len(b)
    assert img["worldRect"] == [0.0, 0.0, 3583.0, 2559.0]
    assert img["decalCount"] == len(want)
    assert img["changedSince"] == changed_since
    for off in ("decalOffset", "nameOffset", "vertexOffset", "changedOffset", "hashOffset"):
        assert img[off] % 16 == 0, off
    name_of = {i: (h, p) for i, (h, p) in enumerate(names)}
    assert sorted(h for h, _ in names) == sorted({t for d in want for t in d[4] if t}), names
    for h, p in names:
        assert TEX[h] == p, (h, p)
    nv = 0
    for k, (r, d) in enumerate(zip(recs, want)):
        i, _, prio, pas, tex, rect, tris, _ = d
        assert r["rect"] == [f32(x) for x in rect] and r["priority"] == f32(prio) and r["flags"] == pas, (r, d)
        for t, hnd in zip(r["texture"], tex):
            if hnd == 0:
                assert t == NO_TEXTURE
            else:
                assert name_of[t][0] == hnd
        assert (r["firstVertex"], r["triangles"], r["listOrder"]) == (nv, tris, k)
        assert verts[8 * nv:8 * (nv + 3 * tris)] == verts_of(i, tris)
        assert hashes[k] == content_hash(d)
        nv += 3 * tris
    assert img["vertexCount"] == nv
    assert sorted(map(tuple, changed)) == sorted(tuple(f32(x) for x in r) for r in changed_want)
    return img


# ======================================================================================================================
# the good cases

def test_synthetic_copied_exactly_acked_and_closed(host):
    h = host(dict(VPSHELL_DECAL_TEST="ok"))
    c = h.wait_state("READY", generation=1)
    assert c["magic"] == MAGIC and c["exePid"] == h.p.pid and c["decalCount"] == 6 and c["failName"] == "NONE"
    assert c["imageName"] == "Local\\VPShellDecal.%d.1" % h.p.pid and c["changedSince"] == 0 and c["added"] == 6
    assert c["heldSections"] == 1 and c["copyMs"] > 0
    check_image(h.image(c), DECALS, 1, 0, [d[5] for d in expected(DECALS)], area=area_start())
    assert (c["areaSet"], c["areaTilesX"], c["areaTilesY"]) == (len(area_start()), TG_TX, TG_TY)
    h.wait_log(r"decal_tap: READY generation 1 \(load 1, first copy\): 6 decals, 12 names, 51 vertices, 6 changed rectangles "
               r"\(\+6 -0 since generation 0\), area 56 x 40 tiles, %d with \+0x300" % len(area_start()))
    h.ack(1)
    c = h.wait(lambda c: c["heldSections"] == 0, what="section closed after the ack")
    assert c["stateName"] == "READY" and c["generation"] == 1
    h.wait_log(r"decal_tap: generation 1 acknowledged by pid 4242; its section closed")
    assert not tapcheck.exists(c["imageName"])
    time.sleep(1.5)
    assert h.control()["generation"] == 1          # no change: no new generation (the safety net found nothing)
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())


def test_change_makes_one_generation_with_its_rectangles(host):
    """After generation 1 is acknowledged, the EXE removes the river bed and adds a road piece: generation 2 within a
    second, changedSince 1, the two rectangles, every record again exact."""
    h = host(dict(VPSHELL_DECAL_TEST="ok"), seconds=8)
    h.wait_state("READY", generation=1)
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    t0 = time.time()
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    assert time.time() - t0 < 1.0
    assert (c["changedSince"], c["added"], c["removed"], c["decalCount"]) == (1, 1, 1, 6)
    after = [d for d in DECALS if d[0] != 4] + [NEW_ROAD]
    check_image(h.image(c), after, 2, 1, [NEW_ROAD[5], DECALS[4][5]], area=area_after_change(area_start()))
    # generation 2 not acknowledged: a further change (a second road piece, equal to the first) is diffed against
    # generation 1 still, the server's last applied one: its rectangles are a superset of the change since 2
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=3, timeout_s=3)
    assert (c["changedSince"], c["added"], c["removed"], c["decalCount"]) == (1, 2, 1, 7)
    check_image(h.image(c), after + [NEW_ROAD], 3, 1, [NEW_ROAD[5], NEW_ROAD[5], DECALS[4][5]])
    assert not tapcheck.exists(r"Local\VPShellDecal.%d.1" % h.p.pid)   # acknowledged and closed
    h.finish()


def test_grid_not_valid_reads_nothing(host):
    h = host(dict(VPSHELL_DECAL_TEST="ok"), args=("--grid", "0"), seconds=3)
    time.sleep(1.5)
    c = h.control()
    assert c["stateName"] == "NONE" and c["generation"] == 0
    h.finish()


def test_kill_switch_off_then_on(tmp_path):
    d = tmp_path / "h0"
    d.mkdir()
    (d / "decal_tap.off").write_text("")
    h = Host(d, dict(VPSHELL_DECAL_TEST="ok"), seconds=6)
    try:
        c = h.wait_state("OFF")
        assert c["generation"] == 0 and c["failName"] == "NONE"
        h.wait_log(r"decal_tap: OFF \(decal_tap=0\)")
        time.sleep(0.5)
        assert h.control()["generation"] == 0
        (d / "decal_tap.off").unlink()
        h.wait_state("READY", generation=1)
        (d / "decal_tap.off").write_text("")
        h.wait_state("OFF", generation=1)
        h.finish()
    finally:
        h.kill()


def test_refused_ack_is_counted_and_loud(host):
    h = host(dict(VPSHELL_DECAL_TEST="ok"))
    h.wait_state("READY", generation=1)
    h.ack(1, result=-1)
    c = h.wait(lambda c: c["refusedReads"] == 1, what="refused counted")
    assert c["heldSections"] == 1                   # the section stays for a new try
    h.wait_log(r"decal_tap: LOUD: the server refused generation 1 \(ackResult -1\)")
    h.finish()


def test_layout_break_keeps_the_last_good_image(host):
    h = host(dict(VPSHELL_DECAL_TEST="ok", VPSHELL_DECAL_NET_MS="200"), seconds=8)
    c1 = h.wait_state("READY", generation=1)
    h.trigger("shell_composite.decalbreak")
    c = h.wait_state("FAILED", timeout_s=5)
    assert c["failName"] == "LAYOUT" and c["generation"] == 1 and c["imageName"] == c1["imageName"]
    assert tapcheck.exists(c1["imageName"])
    h.wait_log(r"decal_tap: LOUD: FAILED LAYOUT: batch 1 slot \d+: a record past its chunk .* - the last good image stays; retried")
    h.finish()


# ======================================================================================================================
# the huge world (issue 4110): the publication spread over Presents within decal_budget_ms, equal to a one-shot build

HW_NB, HW_N, HW_TOGGLE = 19, 17800, 5 + 19 * 7
_PAT = bytes((13 * j) & 0xFF for j in range(256))
_ADD = [bytes((x + c) & 0xFF for x in range(256)) for c in range(256)]


def hw_verts(g, tris):
    n = 24 * tris
    return (_PAT * (n // 256 + 1))[:n].translate(_ADD[(7 * g + 1) & 0xFF])


def hw_decal(g):
    """decal_tap.cpp HugeDecal: (id, batch, priority, pass, handles, rect, triangles)."""
    k = g % 148
    if k == 147:
        tex = (0, 0, 1975)
    else:
        tex = (100 + (2 * k) % 256, 100 + (2 * k + 1) % 256, 1975 if k & 1 else 100 + (5 * k) % 256)
    x0, y0 = float((37 * g) % 3300), float((53 * g) % 2300)
    rect = (x0, y0, x0 + 10 + g % 200 + 0.5, y0 + 10 + (3 * g) % 150 + 0.25)
    return (g, g % HW_NB, (k % 8) * 0.125, 1 if k == 3 else 0, tex, rect, 1 + (11 * g) % 28)


def hw_expected(toggled):
    """The live decals in the tap's record order: batch, then the list (= position) order."""
    return [hw_decal(g) for b in range(HW_NB) for g in range(b, HW_N, HW_NB) if not (toggled and g == HW_TOGGLE)]


def hw_name(h):
    return "blank_texture_spec_decal.dds" if h == 1975 else "hw_texture_%04u.dds" % h


def check_huge_image(b, toggled, hash_every=37, generation=1):
    img, recs, names, changed, hashes, verts = parse_image(b)
    want = hw_expected(toggled)
    per_batch = [sum(3 * d[6] for d in want if d[1] == bb) for bb in range(HW_NB)]
    assert max(per_batch) < 0xFFFF                 # the generator fits the EXE's chunks
    assert (img["magic"], img["version"], img["generation"], img["changedSince"]) == (MAGIC, 1, generation, 0)
    assert img["decalCount"] == len(want) and img["imageBytes"] == len(b)
    order = []
    for d in want:
        for t in d[4]:
            if t and t not in order:
                order.append(t)
    assert [h for h, _ in names] == order and all(p == hw_name(h) for h, p in names)
    idx = {h: i for i, h in enumerate(order)}
    nv = 0
    for k, (r, d) in enumerate(zip(recs, want)):
        g, _, prio, pas, tex, rect, tris = d
        assert r["rect"] == [f32(x) for x in rect] and r["priority"] == f32(prio) and r["flags"] == pas, (k, r, d)
        assert r["texture"] == [idx[t] if t else NO_TEXTURE for t in tex], (k, r, d)
        assert (r["firstVertex"], r["triangles"], r["listOrder"]) == (nv, tris, k), (k, r, d)
        vb = hw_verts(g, tris)
        assert verts[8 * nv:8 * (nv + 3 * tris)] == vb, k
        if k % hash_every == 0 or g in (HW_TOGGLE - HW_NB, HW_TOGGLE + HW_NB):
            assert hashes[k] == tapcheck.fnv1a64(struct.pack("<If3I4fI", pas, prio, *tex, *rect, tris) + vb), k
        nv += 3 * tris
    assert img["vertexCount"] == nv and len(verts) == 8 * nv
    assert sorted(map(tuple, changed)) == sorted(tuple(f32(x) for x in d[5]) for d in want)
    # 0366: the walk block (HugeIndex: added in the order of g; 56 x 40 cells; every decal small)
    w = img["walk"]
    assert w is not None and (w["cols"], w["rows"], w["small"], w["large"]) == (56, 40, len(want), 0), w
    ww, _ = walk_expected(sorted(((d[0], d[5]) for d in want), key=lambda e: e[0]), 56, 40)
    assert all(w["entries"][k] == ww[d[0]] for k, d in enumerate(want))


def is_asan():
    return (shellrun.bin_dir() / "clang_rt.asan_dynamic-i386.dll").exists()


def huge_run(host, budget_ms, midbuild=0, seconds=5.0):
    """One host on the huge world: generation 1's control block and image bytes, the tap's and the host's cost lines."""
    h = host(dict(VPSHELL_DECAL_TEST="huge", VPSHELL_DECAL_BUDGET_MS=budget_ms, VPSHELL_DECAL_TEST_MIDBUILD=midbuild),
             seconds=seconds)
    c = h.wait_state("READY", generation=1, timeout_s=seconds)
    with tapcheck.Mapping(c["imageName"], write=False) as m:
        b = m.read(0, c["imageBytes"])
    h.finish()
    lines = h.lines()
    pub = [ln for ln in lines if "decal_tap: publication cost (READY, generation 1)" in ln]
    assert len(pub) == 1, lines[-12:]
    m = re.search(r": (\d+) Presents from the first read, per Present max ([\d.]+) ms, mean ([\d.]+) ms "
                  r"\(decal_budget_ms (\d+)\), (\d+) build restarts", pub[0])
    cost = dict(presents=int(m.group(1)), max=float(m.group(2)), mean=float(m.group(3)), restarts=int(m.group(5)))
    m = re.search(r"decal_tap_host: Present cost over \d+ Presents after the first: CPU max ([\d.]+) ms", "\n".join(lines))
    cost["hostCpuMax"] = float(m.group(1))
    assert not any("LOUD" in ln for ln in lines), [ln for ln in lines if "LOUD" in ln]
    return c, b, cost, lines


# The per-Present limit asserted: decal_budget_ms plus the largest single step (each step is about 0.1-0.35 ms on the
# workstation's plain build, 2026-11-15; ASan's are larger) and some noise. The host's figure is the thread's CPU time
# (QueryThreadCycleTime): time the thread was not running does not count (COMMON 23: an indication, not a result).
def per_present_limit(budget_ms):
    return budget_ms + (3.0 if is_asan() else 1.5)


def test_huge_spread_equals_one_shot_and_stays_in_budget(host):
    """The huge world's generation 1 built within decal_budget_ms = 2 per Present over many Presents is byte-equal to the
    same image built in one Present (budget 1000); the one-shot Present is far above the limit (the measure is real)."""
    c1, b1, cost1, _ = huge_run(host, 1000)
    c2, b2, cost2, lines2 = huge_run(host, 2)
    assert c1["decalCount"] == c2["decalCount"] == HW_N and c1["fnv1a64"] == c2["fnv1a64"]
    assert b1 == b2
    assert tapcheck.fnv1a64(b2) == c2["fnv1a64"]
    check_huge_image(b2, toggled=False)
    assert parse_image(b2)[0]["area"] == {(i // TG_TY, i % TG_TY) for i in range(0, TG_TX * TG_TY, 5)}   # 4111
    assert cost1["presents"] == 1 and cost1["hostCpuMax"] > 4 * per_present_limit(2), cost1      # the negative
    assert cost2["presents"] >= 10 and cost2["restarts"] == 0, cost2
    assert cost2["max"] <= per_present_limit(2) + 1.0, cost2                                    # wall, in the tap
    assert cost2["hostCpuMax"] <= per_present_limit(2), cost2                                   # CPU, the whole run
    assert re.search(r"READY generation 1 \(load 1, first copy\): 17800 decals, 257 names, 774300 vertices", "\n".join(lines2))


def test_huge_batch_changed_mid_build_restarts_the_build(host):
    """Negative: the EXE changes batch 5 while the spread build is half done (decal_test_midbuild=1). The build starts
    again from the read; generation 1 is the changed world exactly (one consistent snapshot, not a mix of the two), and
    byte-equal to a one-shot build of it."""
    c, b, cost, lines = huge_run(host, 2, midbuild=1)
    text = "\n".join(lines)
    assert re.search(r"decal_tap: test: batch 5 changed during the build \(decal %d freed\)" % HW_TOGGLE, text)
    assert re.search(r"decal_tap: build restarted at step 9 \(a batch moved during the build; restart 1, at most 3 in a row\)", text)
    assert cost["restarts"] == 1 and c["decalCount"] == HW_N - 1
    check_huge_image(b, toggled=True)
    assert cost["hostCpuMax"] <= per_present_limit(2), cost
    c1, b1, cost1, _ = huge_run(host, 1000, midbuild=1)
    assert b1 == b and c1["fnv1a64"] == c["fnv1a64"]


def test_huge_restart_limit_publishes_the_consistent_snapshot(host):
    """Negative: the batch changes during every build (decal_test_midbuild large). After 3 restarts in a row the build
    goes on from its snapshot (the tap's copies, which no read changes during a build) and publishes it: the world after
    the third change, exactly; the next build finds that set again and makes no generation."""
    c, b, cost, lines = huge_run(host, 2, midbuild=100, seconds=6.0)
    text = "\n".join(lines)
    assert len(re.findall(r"decal_tap: build restarted at step 9 \(a batch moved during the build; restart [123], at most 3", text)) >= 3
    assert "decal_tap: a batch moved during the build after 3 restarts: the snapshot is published, the batch is read after it" in text
    assert cost["restarts"] == 3 and c["decalCount"] == HW_N - 1
    check_huge_image(b, toggled=True)
    assert re.search(r"decal_tap: publication cost \(no change of the content set, generation 1\)", text)
    assert not re.search(r"READY generation 2", text)


# ======================================================================================================================
# 4111: the area block (the EXE's tile+0x300 of every tile)

def test_flags_alone_make_a_generation(host):
    """A tile's +0x300 changes without a decal change (a terrain edit invalidates through 0x4387D0 too): the idle sweep
    finds it within a safety-net tick and publishes generation 2 with the same decals and the new bit; nothing more."""
    h = host(dict(VPSHELL_DECAL_TEST="ok", VPSHELL_DECAL_NET_MS="200"), seconds=6)
    h.wait_state("READY", generation=1)
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    time.sleep(0.6)
    assert h.control()["generation"] == 1            # the idle sweeps found nothing new
    h.trigger("shell_composite.areachange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    assert (c["changedSince"], c["added"], c["removed"], c["decalCount"]) == (1, 0, 0, 6)
    want = area_start() | {(20, 20)}
    assert c["areaSet"] == len(want)
    check_image(h.image(c), DECALS, 2, 1, [], area=want)
    h.wait_log(r"decal_tap: tile flags changed without a decal change \(56 x 40 tiles, %d with \+0x300; published 56 x 40, %d\)"
               % (len(want), len(area_start())))
    h.wait_log(r"READY generation 2 \(load 1, tile flags changed\)")
    time.sleep(0.8)
    assert h.control()["generation"] == 2            # no further generation
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())


def test_no_terrain_renderer_publishes_without_the_block(host):
    """TS+0x75FA4 = 0 (no TerrainRenderer): the decals are published, the area block is absent (the server keeps its own
    rule)."""
    h = host(dict(VPSHELL_DECAL_TEST="notr"), seconds=3)
    c = h.wait_state("READY", generation=1)
    assert (c["areaSet"], c["areaTilesX"], c["areaTilesY"]) == (0, 0, 0)
    check_image(h.image(c), DECALS, 1, 0, [d[5] for d in expected(DECALS)], area="none")
    h.wait_log(r"READY generation 1 .* area 0 x 0 tiles, 0 with \+0x300")
    h.finish()


@pytest.mark.parametrize("mode,text", [
    ("badgrid", r"tile grid out of range: 56 x 40 tiles, count 2241, pointers 0x[0-9A-F]+"),
    ("badtile", r"tile \(3, 5\): \+0x300 = 7, not 0 or 1"),
])
def test_bad_tile_grid_fails_without_a_generation(host, mode, text):
    """Negatives: a tile grid whose count is not x * y, a tile whose +0x300 is neither 0 nor 1: FAILED LAYOUT, no
    generation, one LOUD line, not disabled (retried)."""
    h = host(dict(VPSHELL_DECAL_TEST=mode, VPSHELL_DECAL_NET_MS="300"), seconds=4)
    c = h.wait_state("FAILED")
    assert c["failName"] == "LAYOUT" and c["generation"] == 0 and c["sehCount"] == 0, c
    h.wait_log(r"decal_tap: LOUD: FAILED LAYOUT: %s - the last good image stays; retried" % text)
    time.sleep(1.0)
    assert h.control()["generation"] == 0
    assert len([ln for ln in h.lines() if "LOUD" in ln]) == 1
    h.finish()
    assert not any("DISABLED" in ln for ln in h.lines())


# ======================================================================================================================
# 0366: the walk block (the EXE's spatial index and large list: the draw's tie order)

WALK_EXTRA = [
    (10, 0, 0.6, 0, (100, 101, 102), (1000.0, 800.0, 1400.0, 900.0), 2, True),   # large (400 wide): the large list
    (11, 1, 0.7, 0, (100, 101, 102), (20.0, 30.0, 60.0, 50.0), 1, True),         # decal 1's cell, added later: its head
    (12, 1, 0.7, 0, (100, 101, 102), (270.0, 30.0, 300.0, 50.0), 1, True),       # the next cell
]


def test_walk_block_exact_with_ties_and_a_large_decal(host):
    """decal_test=walk: every record's cell and rank in the index (14 x 10 cells; decal 11 at the head of decal 1's cell)
    and the large decal in the large list; then a change (the river bed removed, a road added): the road at the head of
    its cell, the river bed gone, the ranks of the others unchanged."""
    h = host(dict(VPSHELL_DECAL_TEST="walk"), seconds=6)
    decals = DECALS + WALK_EXTRA
    c = h.wait_state("READY", generation=1)
    assert (c["walkSmall"], c["walkLarge"], c["walkMisses"]) == (8, 1, 0), c
    w = check_walk(parse_image(h.image(c))[0], decals)
    by_id = {d[0]: e for d, e in zip(expected(decals), w["entries"])}
    # cell 0 holds decals 1, 2, 3 and 11 (added in that order): 11 at the head, 1 last
    assert by_id[11][:2] == (0, 0) and by_id[1][:2] == (0, 3) and by_id[12][0] == 1 and by_id[10][:2] == (LARGE, 0)
    h.wait_log(r"READY generation 1 .* walk block 8 small \+ 1 large;")
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    after = [d for d in decals if d[0] != 4] + [NEW_ROAD]
    check_image(h.image(c), after, 2, 1, [NEW_ROAD[5], DECALS[4][5]])
    assert (c["walkSmall"], c["walkLarge"], c["walkMisses"]) == (8, 1, 0), c
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())


def test_walk_miss_publishes_without_the_block(host):
    """Negative: an index item names a slot that holds no live record (decal_test=walkmiss): the generation goes out
    without the walk block (the server keeps its own tie rule), counted in walkMisses, one log line, not FAILED."""
    h = host(dict(VPSHELL_DECAL_TEST="walkmiss"), seconds=3)
    c = h.wait_state("READY", generation=1)
    assert c["walkMisses"] >= 1 and (c["walkSmall"], c["walkLarge"]) == (0, 0) and c["failName"] == "NONE", c
    assert c["walkHolds"] == 0, c                  # 0406: nothing published yet, nothing to keep: no hold
    check_image(h.image(c), DECALS, 1, 0, [d[5] for d in expected(DECALS)], walk=False)
    h.wait_log(r"decal_tap: no walk block for this generation: cell \d+: item \d+ names batch 1 slot 15, not a record of the "
               r"snapshot \(nothing published yet\)")
    h.wait_log(r"READY generation 1 .* walk none 0 small \+ 0 large;")
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())


def test_walk_fault_turns_off_only_the_walk(host):
    """Negative: the index's item pool pointer is unmapped (decal_test=walkfault): one LOUD line, no walk block for the
    life of the process, the decals still published (no SEH failure of the tap); a later change publishes without it
    and without a new attempt."""
    h = host(dict(VPSHELL_DECAL_TEST="walkfault"), seconds=6)
    c = h.wait_state("READY", generation=1)
    assert c["walkMisses"] == 1 and c["sehCount"] == 0 and c["failName"] == "NONE", c
    check_image(h.image(c), DECALS, 1, 0, [d[5] for d in expected(DECALS)], walk=False)
    h.wait_log(r"decal_tap: LOUD: walk: the index's item pool at 0x[0-9A-F]+ unreadable - no walk block for the life of the "
               r"process \(the decals go on\)")
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    assert c["walkMisses"] == 1, c
    check_image(h.image(c), [d for d in DECALS if d[0] != 4] + [NEW_ROAD], 2, 1, [NEW_ROAD[5], DECALS[4][5]], walk=False)
    h.finish()
    assert len([ln for ln in h.lines() if "LOUD" in ln]) == 1


# ======================================================================================================================
# 0406: a walk miss in play holds the build instead of publishing without the block (terrain-decals-walk.md)

D2_REWRITTEN = (104.0, 98.0, 146.0, 136.0)   # decal_tap.cpp TestChange, decal_test=walkstale


def stale_after():
    """The world after walkstale's change: decal 2 rewritten in place, the river bed removed, a road added."""
    out = [d for d in DECALS if d[0] != 4] + [NEW_ROAD]
    return [d[:5] + (D2_REWRITTEN,) + d[6:] if d[0] == 2 else d for d in out]


def stale_changed():
    return [NEW_ROAD[5], DECALS[4][5], DECALS[1][5], D2_REWRITTEN]


def test_walk_stale_record_holds_and_publishes_with_the_walk(host):
    """0365's in-play miss (terrain-decals-walk.md 1): the EXE rewrites decal 2 in batch 0 in place with the batch's
    counters equal (the poll cannot see it; the safety net is set to 60 s here so it does not either) while another batch
    changes. The build's snapshot holds the old decal 2, the index the new one: the walk check misses. The tap holds the
    build, reads batch 0 again and publishes generation 2 WITH the walk block: the rewritten decal exact, its old and new
    rectangles in the changed block; no generation without the block; walkMisses 1, walkHolds 1, both lines logged."""
    h = host(dict(VPSHELL_DECAL_TEST="walkstale", VPSHELL_DECAL_NET_MS="60000"), seconds=6)
    c = h.wait_state("READY", generation=1)
    check_image(h.image(c), DECALS, 1, 0, [d[5] for d in expected(DECALS)])
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    assert (c["walkMisses"], c["walkHolds"]) == (1, 1) and (c["walkSmall"], c["walkLarge"]) == (6, 0), c
    assert (c["changedSince"], c["added"], c["removed"], c["decalCount"]) == (1, 2, 2, 6), c
    check_image(h.image(c), stale_after(), 2, 1, stale_changed())
    h.wait_log(r"decal_tap: walk check missed: cell \d+: item \d+ \(batch 0 slot 1\) has the point 125, 117; the record's "
               r"centre 120, 116, small, its cell \d+ - the build is held: the batch it names is read again and the build "
               r"repeated \(hold 1 of 4; the published generation 1 keeps its walk block\)")
    h.wait_log(r"READY generation 2 .* walk block 6 small \+ 0 large;")
    time.sleep(0.5)
    assert h.control()["generation"] == 2
    h.finish()
    text = "\n".join(h.lines())
    assert "no walk block for this generation" not in text and "walk none" not in text
    assert not any("LOUD" in ln for ln in h.lines())


def test_walk_stale_record_without_holds_publishes_without_the_walk(host):
    """Teeth of the test above: with decal_walk_holds = 0 (the tap of 0366) the same change publishes generation 2
    without the walk block (the tie rule would change on the server), the miss logged and counted; the safety net is
    off, so nothing re-reads batch 0 and the stale decal 2 stays in that generation."""
    h = host(dict(VPSHELL_DECAL_TEST="walkstale", VPSHELL_DECAL_NET_MS="60000", VPSHELL_DECAL_WALK_HOLDS="0"), seconds=5)
    h.wait_state("READY", generation=1)
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    assert (c["walkMisses"], c["walkHolds"]) == (1, 0) and (c["walkSmall"], c["walkLarge"]) == (0, 0), c
    stale = [d for d in DECALS if d[0] != 4] + [NEW_ROAD]   # decal 2 as the snapshot holds it: the old one
    check_image(h.image(c), stale, 2, 1, [NEW_ROAD[5], DECALS[4][5]], walk=False)
    h.wait_log(r"decal_tap: no walk block for this generation: cell \d+: item \d+ \(batch 0 slot 1\) has the point 125, 117.*"
               r"\(the walk check missed after the holds\)")
    h.finish()


def test_walk_miss_after_a_walk_holds_then_publishes_without(host):
    """Negative: a miss that does not go away (decal_test=walkmisslate: from the first change on, an index item names a
    slot without a live record): 4 holds, each reading batch 1 again, then generation 2 without the walk block
    ("after the holds"); a later change misses again and publishes at once (the published generation has no walk
    either: the tie rule does not change, nothing to hold for). Not FAILED, no LOUD line."""
    h = host(dict(VPSHELL_DECAL_TEST="walkmisslate", VPSHELL_DECAL_NET_MS="60000"), seconds=7)
    c = h.wait_state("READY", generation=1)
    assert (c["walkMisses"], c["walkHolds"], c["walkSmall"]) == (0, 0, 6), c
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=4)
    assert (c["walkMisses"], c["walkHolds"]) == (5, 4) and (c["walkSmall"], c["walkLarge"]) == (0, 0), c
    assert c["failName"] == "NONE", c
    check_image(h.image(c), [d for d in DECALS if d[0] != 4] + [NEW_ROAD], 2, 1, [NEW_ROAD[5], DECALS[4][5]], walk=False)
    for k in range(1, 5):
        h.wait_log(r"decal_tap: walk check missed: cell \d+: item \d+ names batch 1 slot 15, not a record of the snapshot - the "
                   r"build is held: the batch it names is read again and the build repeated \(hold %d of 4;" % k)
    h.wait_log(r"decal_tap: no walk block for this generation: cell \d+: item \d+ names batch 1 slot 15, not a record of the "
               r"snapshot \(the walk check missed after the holds\)")
    h.ack(2)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=3, timeout_s=3)
    assert (c["walkMisses"], c["walkHolds"]) == (6, 4), c
    h.wait_log(r"\(the published generation has no walk block either\)")
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())


def test_huge_storm_before_the_walk_publishes_without_the_block(host):
    """The load's case (terrain-decals-walk.md 1): the EXE changes batch 5 before the walk of every build until the
    restart limit is reached (decal_test=huge, trigger shell_composite.decalstorm: a change, then one before the walk of
    each of the next 4 builds). The 4th build goes on from its snapshot (by design, for progress) while the index already
    has the next change: the walk misses with a batch moved, and the generation goes out without the block at once, no
    hold (named in the log); the next build, calm, has the walk again and the world exact."""
    h = host(dict(VPSHELL_DECAL_TEST="huge", VPSHELL_DECAL_BUDGET_MS="2", VPSHELL_DECAL_NET_MS="60000"), seconds=10)
    c = h.wait_state("READY", generation=1, timeout_s=6)
    assert c["walkSmall"] == HW_N, c
    h.trigger("shell_composite.decalstorm")
    c = h.wait_state("READY", generation=2, timeout_s=6)
    assert (c["walkMisses"], c["walkHolds"]) == (1, 0) and (c["walkSmall"], c["walkLarge"]) == (0, 0), c
    assert c["decalCount"] == HW_N, c              # the snapshot of the 4th build: the toggled decal back
    h.wait_log(r"decal_tap: no walk block for this generation: 1 of 17800 records are in neither the index nor the large list "
               r"\(the first: record \d+, batch 5 slot \d+\) \(a batch moved after the build began and the restart limit was "
               r"reached: the snapshot is older than the index\)")
    c = h.wait_state("READY", generation=3, timeout_s=6)
    assert (c["walkMisses"], c["walkHolds"], c["walkSmall"], c["decalCount"]) == (1, 0, HW_N - 1, HW_N - 1), c
    with tapcheck.Mapping(c["imageName"], write=False) as m:
        b = m.read(0, c["imageBytes"])
    check_huge_image(b, toggled=True, generation=3)
    h.finish()
    text = "\n".join(h.lines())
    assert len(re.findall(r"decal_tap: test: batch 5 changed before the walk", text)) == 4
    assert "a batch moved during the build after 3 restarts" in text
    assert "walk check missed" not in text
    assert not any("LOUD" in ln for ln in h.lines())


# ======================================================================================================================
# 0649: records clamped outside the world's x range (a decal past the wrap seam) are left out, not a refusal

SEAM_FIRST = r"batch 0 slot 3, rectangle \(3590.25, 1505.4, 3583, 1564.42\)"


def test_seam_records_are_left_out_of_the_first_set(host):
    """decal_test=seam: three live records as AddDecal leaves a decal whose vertices all lie past the world's east or west
    edge (min x > max x, the clamped side exactly at the world's edge; one of them 300 high, so in the large list). The
    first set is accepted (READY generation 1, every other record exact, the walk block exact: their index item and
    large-list node take no rank), one line names the count and the first rectangle, no LOUD line; a change keeps them out."""
    h = host(dict(VPSHELL_DECAL_TEST="seam"), seconds=6)
    c = h.wait_state("READY", generation=1)
    assert c["decalCount"] == 6 and c["failName"] == "NONE", c
    assert (c["walkSmall"], c["walkLarge"], c["walkMisses"]) == (6, 0, 0), c
    check_image(h.image(c), DECALS, 1, 0, [d[5] for d in expected(DECALS)], area=area_start())
    h.wait_log(r"decal_tap: generation 1 leaves out 3 decal record\(s\) outside the world's x range 0 \.\. 3583 \(the EXE's "
               r"draw never takes them\); the first: " + SEAM_FIRST)
    h.ack(1)
    h.wait(lambda c: c["heldSections"] == 0)
    h.trigger("shell_composite.decalchange")
    c = h.wait_state("READY", generation=2, timeout_s=3)
    assert (c["changedSince"], c["added"], c["removed"], c["decalCount"]) == (1, 1, 1, 6), c
    check_image(h.image(c), [d for d in DECALS if d[0] != 4] + [NEW_ROAD], 2, 1, [NEW_ROAD[5], DECALS[4][5]])
    h.wait_log(r"decal_tap: generation 2 leaves out 3 decal record\(s\) .*; the first: " + SEAM_FIRST)
    h.finish()
    lines = h.lines()
    assert not any("LOUD" in ln for ln in lines), [ln for ln in lines if "LOUD" in ln]
    assert len([ln for ln in lines if "leaves out" in ln]) == 2      # one line per generation, never per Present


def test_seam_form_in_y_still_fails_the_layout_check(host):
    """Negative (decal_test=seamy): the same clamped form in y (a rectangle past the north edge) is not the seam form: the
    set is refused as before (FAILED LAYOUT, no generation, one LOUD line)."""
    h = host(dict(VPSHELL_DECAL_TEST="seamy", VPSHELL_DECAL_NET_MS="300"), seconds=4)
    c = h.wait_state("FAILED")
    assert c["failName"] == "LAYOUT" and c["generation"] == 0, c
    h.wait_log(r"decal_tap: LOUD: FAILED LAYOUT: batch 0 slot 3: rectangle \(1000, 2570, 1040, 2559\) or priority 0.7 not "
               r"valid - the last good image stays; retried")
    h.finish()
    assert not any("leaves out" in ln for ln in h.lines())


# ======================================================================================================================
# negatives

@pytest.mark.parametrize("mode,fail,text", [
    ("pastchunk", "LAYOUT", r"batch 1 slot 1: a record past its chunk \(first vertex 9000 \+ 3 x 5 triangles, 24 vertices used\)"),
    ("noname", "NAMES", r"texture handle 150 has no name \(not in the resource map, not a blank spec, not a farm type\)"),
    ("hugecount", "LAYOUT", r"absurd batch count 100000 \(capacity 4, vector 0x[0-9A-F]+\)"),
    ("badrect", "LAYOUT", r"batch 0 slot 0: rectangle \(10, 20, -5, 60.25\) or priority 0.7 not valid"),
])
def test_bad_records_fail_without_a_generation(host, mode, fail, text):
    """A failed check publishes nothing: FAILED with its reason and one LOUD line, the host goes on, the tap is not
    disabled (it reads again once per safety-net tick and would publish once the records pass)."""
    h = host(dict(VPSHELL_DECAL_TEST=mode, VPSHELL_DECAL_NET_MS="300"), seconds=4)
    c = h.wait_state("FAILED")
    assert c["failName"] == fail and c["generation"] == 0 and c["imageName"] == "" and c["sehCount"] == 0, c
    h.wait_log(r"decal_tap: LOUD: FAILED %s: %s - the last good image stays; retried" % (fail, text))
    time.sleep(1.0)
    loud = [ln for ln in h.lines() if "LOUD" in ln]
    assert len(loud) == 1, loud                    # the same text again is rate-limited
    h.finish()
    assert not any("DISABLED" in ln for ln in h.lines())


def test_unmapped_pointer_disables_the_tap(host):
    h = host(dict(VPSHELL_DECAL_TEST="garbage"), seconds=4)
    c = h.wait_state("FAILED")
    assert c["failName"] == "SEH" and c["sehCount"] == 1 and c["generation"] == 0
    h.wait_log(r"decal_tap: LOUD: FAILED SEH: fault reading 2 batches at 0x[0-9A-F]+ - DISABLED for the life of the process")
    time.sleep(1.0)
    assert h.control()["sehCount"] == 1            # no further read
    h.finish()                                     # the host (the EXE) goes on and exits normally


def test_image_limit(host):
    h = host(dict(VPSHELL_DECAL_TEST="ok", VPSHELL_DECAL_MAX_KB="1"), seconds=3)
    c = h.wait_state("FAILED")
    assert c["failName"] == "TOO_BIG" and c["generation"] == 0
    h.wait_log(r"decal_tap: LOUD: FAILED TOO_BIG: image of \d+ bytes, the limit is 1024")
    h.finish()


def test_real_path_refuses_an_exe_of_no_known_build(host):
    """No decal_test: the real path, with the host's own image as the EXE: its header names no build the patch knows
    (ticket 4965, epic 0590): FAILED PATTERN before any read, for good."""
    h = host(dict(VPSHELL_DECAL_TEST=""), args=("--exe", "self"), seconds=3)
    c = h.wait_state("FAILED")
    assert c["failName"] == "PATTERN" and c["generation"] == 0
    h.wait_log(r"decal_tap: LOUD: FAILED PATTERN: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage "
               r"0x[0-9A-F]{8}\) - DISABLED for the life of the process")
    h.finish()


def test_real_path_refuses_an_unvalidated_exe(host):
    h = host(dict(VPSHELL_DECAL_TEST=""), args=("--exe", "self", "--unvalidated"), seconds=3)
    c = h.wait_state("FAILED")
    assert c["failName"] == "PATTERN"
    h.wait_log(r"decal_tap: LOUD: FAILED PATTERN: the composite did not validate the EXE image - the tap never reads it - DISABLED")
    h.finish()


BUILDS = ["4390913", "26577625"]


def pattern_symbols():
    """The symbol of every row of decal_tap.cpp's pattern table, in order."""
    src = (shellrun.SHELL / "composite" / "decal_tap.cpp").read_text(encoding="utf-8")
    body = src[src.index("kDtSigs[] = {"):src.index("};", src.index("kDtSigs[] = {"))]
    return re.findall(r'X\(VPX_(\w+),', body)


@pytest.mark.parametrize("build", BUILDS)
def test_patterns_all_match_in_an_image_of_each_build(host, build):
    """decal_test=patterns (ticket 4965): the real pattern check on a fake image of each known build (its header and
    every pattern at that build's addresses from the build table), then nothing is read."""
    h = host(dict(VPSHELL_DECAL_TEST="patterns", VPSHELL_DECAL_TEST_BUILD=build), seconds=2)
    h.wait_log(r"decal_tap: test image of Steam build %s " % build)
    h.wait_log(r"decal_tap: test: EXE patterns match \(45\) on Steam build %s .* - nothing is read" % build)
    h.finish()
    assert not any("LOUD" in ln for ln in h.lines())
    assert len(pattern_symbols()) == 45


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("k", [0, 16, 24, 44])   # the TDS site, the farm init (two string operands), the 8 farm categories, the tile bake
def test_a_broken_pattern_is_refused(host, build, k):
    h = host(dict(VPSHELL_DECAL_TEST="patterns", VPSHELL_DECAL_TEST_BUILD=build, VPSHELL_DECAL_TEST_PATTERN=str(k)), seconds=2)
    c = h.wait_state("FAILED")
    assert c["failName"] == "PATTERN" and c["generation"] == 0
    h.wait_log(r"decal_tap: LOUD: FAILED PATTERN: pattern '.+?' at %s does not match on Steam build %s .* - DISABLED"
               % (pattern_symbols()[k], build))
    h.finish()


def test_an_image_of_no_known_build_is_refused(host):
    h = host(dict(VPSHELL_DECAL_TEST="patterns", VPSHELL_DECAL_TEST_BUILD="0"), seconds=2)
    c = h.wait_state("FAILED")
    assert c["failName"] == "PATTERN"
    h.wait_log(r"decal_tap: LOUD: FAILED PATTERN: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage "
               r"0x06519000\)")
    h.finish()


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("sym,text", [
    ("decal_farm_owner", r"symbol decal_farm_owner is not carried to Steam build %s "),
    ("decal_farm_cats", r"pattern '.+?': symbol decal_farm_cats is not carried to Steam build %s "),
])
def test_a_symbol_not_carried_is_refused(host, build, sym, text):
    h = host(dict(VPSHELL_DECAL_TEST="patterns", VPSHELL_DECAL_TEST_BUILD=build, VPSHELL_DECAL_TEST_DROP=sym), seconds=2)
    c = h.wait_state("FAILED")
    assert c["failName"] == "PATTERN"
    h.wait_log(r"decal_tap: LOUD: FAILED PATTERN: " + text % build)
    h.finish()


def test_unknown_test_mode_is_named(host):
    h = host(dict(VPSHELL_DECAL_TEST="bogus"), args=("--exe", "self"), seconds=2)
    h.wait_log(r"decal_tap: decal_test=bogus is not a known mode - ignored")
    h.finish()


# ======================================================================================================================
# static

def test_pattern_table_shape():
    """45 rows (ticket 4965): 32 of 4102 with the 8 farm category strings as one row (was 8), 4 of 4111 (the tile grid and
    tile+0x300), 16 of 0366 (the index walk). Each row against the EXE of each known build: test_exe_tables_shell_b.py
    (from the local tables that gen_table.py checked against the builds' golden EXEs; the Steam install is not read)."""
    syms = pattern_symbols()
    assert len(syms) == 45 and len(set(syms)) == 45, syms
    text = (shellrun.SHELL / "composite" / "decal_tap.cpp").read_text(encoding="utf-8")
    cats = re.search(r'X\(VPX_decal_farm_cats, "[^"]*", "([^"]*)"\)', text).group(1).split()
    assert len(cats) == 7 * 32 + 13   # 8 names 32 bytes apart, the last one 13 bytes with its NUL
    for k, name in enumerate(["Crops_Half_Built", "Crops_Africa", "Crops_America", "Crops_Asia", "Crops_Europe", "Crops_Wheat",
                              "Crops_Wheat_HB", "Crops_Damage"]):
        assert bytes(int(t, 16) for t in cats[32 * k:32 * k + len(name) + 1]) == name.encode() + b"\0", name


def test_header_layout_matches_this_reader():
    """The offsets this file reads are the header's (the static_asserts of shell_decal_tap.h say the same)."""
    text = (shellrun.SHELL / "common" / "shell_decal_tap.h").read_text(encoding="utf-8")
    assert "offsetof(VPShellDecalTapControl, changedSince) == 220 && sizeof(VPShellDecalTapControl) == 336" in text
    assert "offsetof(VPShellDecalImage, changedSince) == 80" in text
    assert "offsetof(VPShellDecalImage, areaOffset) == 88 && offsetof(VPShellDecalImage, areaTilesY) == 96" in text
    assert "offsetof(VPShellDecalTapControl, areaSet) == 236" in text
    assert "offsetof(VPShellDecalTapControl, walkSmall) == 248 && sizeof(VPShellDecalTapControl) == 336" in text   # 0366
    assert "offsetof(VPShellDecalImage, walkOffset) == 100 && sizeof(VPShellDecalWalkHeader) == 32" in text
    assert "offsetof(VPShellDecalTapControl, walkHolds) == 260 && sizeof(VPShellDecalTapControl) == 336" in text   # 0406
    assert "sizeof(VPShellDecalImage) == 112" in text and "sizeof(VPShellDecalRecord) == 48" in text


# ======================================================================================================================
# inside the composite on the stand-in (test_host --fake-exe: the composite validates the fake EXE and reads its camera,
# so gridValid is 1); the kill switch through the composite's ini

COMPOSITE_HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")
HOOK_COMMIT_WAIT_S = 18   # bug 0120: the first Present after the hook commit can take seconds under load


def composite_control(run, timeout_s=20.0, pred=lambda c: True):
    name = "Local\\VPShellDecalTap.%d" % run.host.pid
    t_end = time.time() + timeout_s
    c, m = {}, None
    try:
        while time.time() < t_end:
            if m is None:
                try:
                    m = tapcheck.Mapping(name, write=False)
                except OSError:
                    time.sleep(0.05)
                    continue
            raw = tapcheck.read_block(m, 0, CTL_BYTES)
            c = parse_control(raw) if raw else {}
            if c and pred(c):
                return c
            time.sleep(0.05)
    finally:
        if m:
            m.close()
    raise TimeoutError("composite decal tap: %s; log %s" % (c, shellrun.tail(run.composite_log, 8)))


def composite_finish(run, seconds=40):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert rows and not any(r["disabled"] for r in rows)


def main_section(run):
    """The main section as server.exe would create it (the composite publishes its camera block there)."""
    m = tapcheck.Mapping(run.section, create=4096)
    m.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
    return m


def test_composite_runs_the_tap_and_its_kill_switch(run):
    main = main_section(run)
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", decal_tap="1", decal_test="ok")
    run.start_host(*COMPOSITE_HOST, seconds=25)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    c = composite_control(run, pred=lambda c: c["stateName"] == "READY")
    assert c["generation"] == 1 and c["decalCount"] == 6 and c["exePid"] == run.host.pid
    with tapcheck.Mapping(c["imageName"], write=False) as m:
        b = m.read(0, c["imageBytes"])
    check_image(b, DECALS, 1, 0, [d[5] for d in expected(DECALS)])
    run.wait_log(r"decal_tap: READY generation 1 \(load \d+, first copy\): 6 decals", timeout_s=2)
    run.set_ini(decal_tap="0")
    composite_control(run, pred=lambda c: c["stateName"] == "OFF")
    run.wait_log(r"decal_tap: OFF \(decal_tap=0\)", timeout_s=5)
    composite_finish(run)
    main.close()


def test_composite_real_path_refuses_the_fake_exe(run):
    """No decal_test: the fake EXE passes the composite's validation but has no decal system (none of the tap's
    patterns): FAILED PATTERN before any read, LOUD, and the composite and the host run on."""
    main = main_section(run)
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", decal_tap="1", decal_test="")
    run.start_host(*COMPOSITE_HOST, seconds=20)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    c = composite_control(run, pred=lambda c: c["stateName"] == "FAILED")
    assert c["failName"] == "PATTERN" and c["generation"] == 0
    # ticket 4965: refused for its build (test_host's fake header carries no SizeOfImage before 4964) or by the first pattern
    run.wait_log(r"decal_tap: LOUD: FAILED PATTERN: (not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage "
                 r"0x[0-9A-F]{8}\)|pattern 'TDS .*' at decal_rm_tds_site does not match.*) - DISABLED for the life of the process",
                 timeout_s=10)
    composite_finish(run)
    main.close()
